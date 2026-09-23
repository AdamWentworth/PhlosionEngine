#include "GltfSceneAsset.h"

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "FastGLTFLoader.h"
#include "ModelFastGltfLoaderHelpers.h"
#include "ModelFastGltfSceneData.h"
#include "ModelFastGltfTextures.h"
#include "engine/render/ModelPoseSampler.h"
#include "engine/core/IAssetStore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engine::render::gltf::scene {
namespace {

using engine::render::gltf::model::BaseColorDecodePolicy;
using engine::render::gltf::model::CPUTexture;

std::uint64_t fnv1a64(const void* data, std::size_t bytes, std::uint64_t seed) {
    const auto* cursor = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    for (std::size_t index = 0; index < bytes; ++index) {
        hash ^= static_cast<std::uint64_t>(cursor[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    static const char* kDigits = "0123456789abcdef";
    std::string text(16u, '0');
    for (int index = 15; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = kDigits[value & 0xFull];
        value >>= 4u;
    }
    return text;
}

bool checkedAdd(std::size_t left, std::size_t right, std::size_t& out) {
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        return false;
    }
    out = left + right;
    return true;
}

bool checkedMul(std::size_t left, std::size_t right, std::size_t& out) {
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    out = left * right;
    return true;
}

bool finite(const glm::vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finite(const glm::vec4& value) {
    return finite(glm::vec3(value)) && std::isfinite(value.w);
}

bool finite(const glm::mat4& value) {
    for (int column = 0; column < 4; ++column) {
        if (!finite(value[column])) {
            return false;
        }
    }
    return true;
}

std::size_t componentByteSize(fastgltf::ComponentType type) {
    switch (type) {
        case fastgltf::ComponentType::Byte:
        case fastgltf::ComponentType::UnsignedByte:
            return 1u;
        case fastgltf::ComponentType::Short:
        case fastgltf::ComponentType::UnsignedShort:
            return 2u;
        case fastgltf::ComponentType::Int:
        case fastgltf::ComponentType::UnsignedInt:
        case fastgltf::ComponentType::Float:
            return 4u;
        case fastgltf::ComponentType::Double:
            return 8u;
        default:
            return 0u;
    }
}

std::size_t accessorComponentCount(fastgltf::AccessorType type) {
    switch (type) {
        case fastgltf::AccessorType::Scalar:
            return 1u;
        case fastgltf::AccessorType::Vec2:
            return 2u;
        case fastgltf::AccessorType::Vec3:
            return 3u;
        case fastgltf::AccessorType::Vec4:
            return 4u;
        case fastgltf::AccessorType::Mat2:
            return 4u;
        case fastgltf::AccessorType::Mat3:
            return 9u;
        case fastgltf::AccessorType::Mat4:
            return 16u;
        default:
            return 0u;
    }
}

bool bufferByteSize(const fastgltf::Buffer& buffer, std::size_t& out) {
    if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&buffer.data)) {
        out = vector->bytes.size();
        return true;
    }
    if (const auto* array = std::get_if<fastgltf::sources::Array>(&buffer.data)) {
        out = array->bytes.size();
        return true;
    }
    if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&buffer.data)) {
        out = view->bytes.size();
        return true;
    }
    out = 0u;
    return false;
}

std::string attributeName(const fastgltf::Attribute& attribute) {
    return std::string(attribute.name.begin(), attribute.name.end());
}

std::string meshLabel(int meshIndex, int primitiveIndex) {
    return "mesh " + std::to_string(meshIndex) + " primitive " +
           std::to_string(primitiveIndex);
}

bool accessorTypeIs(const fastgltf::Accessor& accessor,
                    fastgltf::AccessorType expected) {
    return accessor.type == expected;
}

bool allowedAttribute(const std::string& name, bool hasSkin) {
    static const char* kAllowed[] = {"POSITION",  "NORMAL",   "TANGENT",
                                     "TEXCOORD_0", "COLOR_0", "JOINTS_0",
                                     "WEIGHTS_0"};
    for (const char* candidate : kAllowed) {
        if (name == candidate) {
            if (!hasSkin && (name == "JOINTS_0" || name == "WEIGHTS_0")) {
                return false;
            }
            return true;
        }
    }
    return false;
}

bool materialHasUnsupportedExtension(const fastgltf::Material& material,
                                     std::string& outFeature) {
    struct Feature {
        bool present;
        const char* name;
    };
    const Feature features[] = {
        {material.anisotropy != nullptr, "KHR_materials_anisotropy"},
        {material.clearcoat != nullptr, "KHR_materials_clearcoat"},
        {material.diffuseTransmission != nullptr,
         "KHR_materials_diffuse_transmission"},
        {material.iridescence != nullptr, "KHR_materials_iridescence"},
        {material.sheen != nullptr, "KHR_materials_sheen"},
        {material.specular != nullptr, "KHR_materials_specular"},
        {material.transmission != nullptr, "KHR_materials_transmission"},
        {material.volume != nullptr, "KHR_materials_volume"},
        {material.packedNormalMetallicRoughnessTexture.has_value(),
         "MSFT_packing_normalRoughnessMetallic"},
        {material.packedOcclusionRoughnessMetallicTextures != nullptr,
         "MSFT_packing_occlusionRoughnessMetallic"},
        {material.unlit, "KHR_materials_unlit"},
    };
    for (const Feature& feature : features) {
        if (feature.present) {
            outFeature = feature.name;
            return true;
        }
    }
    if (material.emissiveStrength != 1.0f) {
        outFeature = "KHR_materials_emissive_strength (non-default strength)";
        return true;
    }
    if (material.ior != 1.5f) {
        outFeature = "KHR_materials_ior (non-default index of refraction)";
        return true;
    }
    if (material.dispersion != 0.0f) {
        outFeature = "KHR_materials_dispersion";
        return true;
    }
    return false;
}

bool samplerIsSupported(const fastgltf::Asset& asset,
                        const fastgltf::Texture& texture,
                        std::string& outDetail) {
    if (!texture.samplerIndex.has_value()) {
        return true;
    }
    const std::size_t samplerIndex = texture.samplerIndex.value();
    if (samplerIndex >= asset.samplers.size()) {
        outDetail = "sampler index " + std::to_string(samplerIndex) +
                    " is out of range";
        return false;
    }
    const fastgltf::Sampler& sampler = asset.samplers[samplerIndex];
    if (sampler.wrapS != fastgltf::Wrap::Repeat ||
        sampler.wrapT != fastgltf::Wrap::Repeat) {
        outDetail = "non-default sampler wrap mode";
        return false;
    }
    // The shared world path has no per-texture filter field, so it always
    // supplies linear magnification and renderer-controlled mip filtering.
    // Anything else would be silently ignored, so it is rejected instead.
    if (sampler.minFilter.has_value()) {
        outDetail =
            "unsupported minification filter; the shared world path always "
            "owns the minification policy; omit minFilter";
        return false;
    }
    if (sampler.magFilter.has_value() &&
        sampler.magFilter.value() != fastgltf::Filter::Linear) {
        outDetail =
            "unsupported magnification filter; the shared world path always "
            "supplies linear magnification";
        return false;
    }
    return true;
}

bool textureInfoSupported(const std::string& materialName,
                          const char* role,
                          const fastgltf::TextureInfo& info,
                          std::string& outError) {
    if (info.texCoordIndex != 0u) {
        outError = "material '" + materialName + "' binds " + role +
                   " to TEXCOORD_" + std::to_string(info.texCoordIndex) +
                   "; only the shared UV0 set is supported";
        return false;
    }
    if (info.transform != nullptr) {
        outError = "material '" + materialName + "' applies a " + role +
                   " texture transform; KHR_texture_transform is unsupported";
        return false;
    }
    return true;
}

GltfSceneTexture makeImplicitWhite() {
    GltfSceneTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.wrapS = 10497;
    texture.wrapT = 10497;
    texture.textureSrgb = 1u;
    texture.authored = false;
    texture.rgba = {255u, 255u, 255u, 255u};
    texture.contentHash = fnv1a64(texture.rgba.data(), texture.rgba.size(),
                                  1469598103934665603ull);
    return texture;
}

GltfSceneTexture fromCPU(const CPUTexture& cpu,
                         bool authored,
                         std::string keyPrefix) {
    GltfSceneTexture texture;
    texture.width = static_cast<int>(cpu.width);
    texture.height = static_cast<int>(cpu.height);
    texture.wrapS = cpu.wrapS;
    texture.wrapT = cpu.wrapT;
    texture.rgba = cpu.rgba;
    texture.authored = authored;
    std::uint64_t hash = fnv1a64(texture.rgba.data(), texture.rgba.size(),
                                 1469598103934665603ull);
    const std::uint32_t dimensions[2] = {cpu.width, cpu.height};
    hash = fnv1a64(dimensions, sizeof(dimensions), hash);
    texture.contentHash = hash;
    keyPrefix += "/";
    keyPrefix += hex64(hash);
    texture.key = keyPrefix;
    texture.cacheKey = keyPrefix;
    return texture;
}

struct LoadState {
    const fastgltf::Asset* asset = nullptr;
    fastgltf::DefaultBufferDataAdapter* adapter = nullptr;
    std::filesystem::path baseDir;
    std::string sourcePath;
    std::string sourceIdentity;
    std::uint64_t generation = 0u;
    const GltfSceneLoadOptions* options = nullptr;
    // Sorted so a diagnostic is stable regardless of traversal order.
    std::string error;
    std::size_t vertexCount = 0u;
    std::size_t indexCount = 0u;
    std::size_t retainedTextureBytes = 0u;
    std::size_t retainedGeometryBytes = 0u;
    std::size_t textureCount = 0u;

    bool fail(std::string message) {
        if (error.empty()) {
            error = std::move(message);
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// Raw preflight. Every reference, range, stride, type and count is validated
// against the parsed document and the actually loaded bytes before any helper
// indexes an array or iterates an accessor.
// ---------------------------------------------------------------------------

bool accessorPayloadValid(LoadState& state,
                          const fastgltf::Accessor& accessor,
                          const std::string& label) {
    const fastgltf::Asset& asset = *state.asset;
    if (accessor.sparse.has_value()) {
        return state.fail(label +
                          " uses a sparse accessor; sparse accessors are "
                          "unsupported by this importer");
    }
    if (!accessor.bufferViewIndex.has_value()) {
        return state.fail(label + " has no buffer view");
    }
    const std::size_t viewIndex = accessor.bufferViewIndex.value();
    if (viewIndex >= asset.bufferViews.size()) {
        return state.fail(label + " references out-of-range buffer view " +
                          std::to_string(viewIndex));
    }
    const fastgltf::BufferView& view = asset.bufferViews[viewIndex];
    if (view.meshoptCompression != nullptr) {
        return state.fail(label +
                          " uses EXT_meshopt_compression, which is unsupported");
    }
    if (view.bufferIndex >= asset.buffers.size()) {
        return state.fail(label + " references out-of-range buffer " +
                          std::to_string(view.bufferIndex));
    }
    std::size_t bufferBytes = 0u;
    if (!bufferByteSize(asset.buffers[view.bufferIndex], bufferBytes)) {
        return state.fail(label + " buffer payload is unavailable");
    }
    std::size_t viewEnd = 0u;
    if (!checkedAdd(view.byteOffset, view.byteLength, viewEnd) ||
        viewEnd > bufferBytes) {
        return state.fail(label +
                          " buffer view range exceeds the loaded buffer");
    }
    const std::size_t componentSize = componentByteSize(accessor.componentType);
    const std::size_t components = accessorComponentCount(accessor.type);
    if (componentSize == 0u || components == 0u) {
        return state.fail(label + " has an unsupported component type or shape");
    }
    std::size_t elementSize = 0u;
    if (!checkedMul(componentSize, components, elementSize)) {
        return state.fail(label + " element size overflows the range check");
    }
    std::size_t stride = elementSize;
    if (view.byteStride.has_value()) {
        stride = view.byteStride.value();
        if (stride < elementSize || stride % componentSize != 0u ||
            stride % 4u != 0u) {
            return state.fail(label + " buffer view stride is invalid");
        }
    }
    if (accessor.byteOffset % componentSize != 0u ||
        view.byteOffset % componentSize != 0u) {
        return state.fail(label + " offset is not component-aligned");
    }
    if (accessor.count == 0u) {
        return state.fail(label + " has no elements");
    }
    if (accessor.count > 0u) {
        std::size_t span = 0u;
        if (!checkedMul(accessor.count - 1u, stride, span) ||
            !checkedAdd(span, elementSize, span) ||
            !checkedAdd(accessor.byteOffset, span, span)) {
            return state.fail(label + " element range overflows");
        }
        if (span > view.byteLength) {
            return state.fail(
                label + " element range exceeds its buffer view (" +
                std::to_string(accessor.count) + " elements, stride " +
                std::to_string(stride) + ", view length " +
                std::to_string(view.byteLength) + ")");
        }
    }
    return true;
}

bool floatAccessorValid(LoadState& state,
                        const fastgltf::Accessor& accessor,
                        std::size_t expectedCount,
                        fastgltf::AccessorType expectedType,
                        const std::string& label) {
    if (accessor.componentType != fastgltf::ComponentType::Float) {
        return state.fail(label + " must use FLOAT components");
    }
    if (accessor.type != expectedType) {
        return state.fail(label + " has an unexpected accessor shape");
    }
    if (accessor.count != expectedCount) {
        return state.fail(label + " has " + std::to_string(accessor.count) +
                          " elements; expected " +
                          std::to_string(expectedCount));
    }
    return accessorPayloadValid(state, accessor, label);
}

const std::array<const char*, 24> kUnsupportedUsedExtensions{
    "KHR_texture_transform",
    "KHR_texture_basisu",
    "MSFT_texture_dds",
    "EXT_texture_webp",
    "KHR_mesh_quantization",
    "EXT_meshopt_compression",
    "KHR_draco_mesh_compression",
    "KHR_materials_variants",
    "EXT_mesh_gpu_instancing",
    "MSFT_packing_normalRoughnessMetallic",
    "MSFT_packing_occlusionRoughnessMetallic",
    "KHR_materials_unlit",
    "KHR_materials_emissive_strength",
    "KHR_materials_specular",
    "KHR_materials_ior",
    "KHR_materials_iridescence",
    "KHR_materials_volume",
    "KHR_materials_transmission",
    "KHR_materials_clearcoat",
    "KHR_materials_sheen",
    "KHR_materials_anisotropy",
    "KHR_materials_dispersion",
    "KHR_materials_pbrSpecularGlossiness",
    "KHR_lights_punctual",
};

bool validateExtensionPolicy(LoadState& state) {
    const fastgltf::Asset& asset = *state.asset;
    // This bounded path implements no extension, so a required extension is
    // always a rejection rather than an approximation.
    if (!asset.extensionsRequired.empty()) {
        const std::string name(asset.extensionsRequired.front().begin(),
                               asset.extensionsRequired.front().end());
        return state.fail("the asset requires the unsupported extension '" +
                          name + "'");
    }
    for (const auto& used : asset.extensionsUsed) {
        const std::string name(used.begin(), used.end());
        for (const char* unsupported : kUnsupportedUsedExtensions) {
            if (name == unsupported) {
                return state.fail("the asset uses the unsupported extension '" +
                                  name + "'");
            }
        }
    }
    return true;
}

std::size_t selectedSceneIndex(LoadState& state, bool& ok) {
    const fastgltf::Asset& asset = *state.asset;
    ok = true;
    if (asset.scenes.empty()) {
        state.fail("the asset declares no scene");
        ok = false;
        return 0u;
    }
    std::size_t sceneIndex = 0u;
    if (asset.defaultScene.has_value()) {
        sceneIndex = asset.defaultScene.value();
        if (sceneIndex >= asset.scenes.size()) {
            state.fail("the default scene index " +
                       std::to_string(sceneIndex) + " is out of range");
            ok = false;
            return 0u;
        }
    }
    return sceneIndex;
}

bool validateRawHierarchy(LoadState& state) {
    const fastgltf::Asset& asset = *state.asset;
    const std::size_t nodeCount = asset.nodes.size();
    std::vector<int> parent(nodeCount, -1);
    for (std::size_t node = 0u; node < nodeCount; ++node) {
        for (const std::size_t child : asset.nodes[node].children) {
            if (child >= nodeCount) {
                return state.fail("node " + std::to_string(node) +
                                  " has out-of-range child " +
                                  std::to_string(child));
            }
            if (parent[child] != -1) {
                return state.fail(
                    "node hierarchy is not a single-parent tree: node " +
                    std::to_string(child) + " has parents " +
                    std::to_string(parent[child]) + " and " +
                    std::to_string(node));
            }
            parent[child] = static_cast<int>(node);
        }
    }
    for (std::size_t node = 0u; node < nodeCount; ++node) {
        std::size_t steps = 0u;
        int cursor = static_cast<int>(node);
        while (cursor != -1) {
            if (steps++ > nodeCount) {
                return state.fail("node hierarchy contains a cycle at node " +
                                  std::to_string(node));
            }
            cursor = parent[static_cast<std::size_t>(cursor)];
        }
    }
    return true;
}

bool validateRawNodes(LoadState& state) {
    const fastgltf::Asset& asset = *state.asset;
    for (std::size_t node = 0u; node < asset.nodes.size(); ++node) {
        const fastgltf::Node& raw = asset.nodes[node];
        if (raw.meshIndex.has_value() &&
            raw.meshIndex.value() >= asset.meshes.size()) {
            return state.fail("node " + std::to_string(node) +
                              " references out-of-range mesh " +
                              std::to_string(raw.meshIndex.value()));
        }
        if (raw.skinIndex.has_value() &&
            raw.skinIndex.value() >= asset.skins.size()) {
            return state.fail("node " + std::to_string(node) +
                              " references out-of-range skin " +
                              std::to_string(raw.skinIndex.value()));
        }
        if (!raw.weights.empty()) {
            return state.fail("node " + std::to_string(node) +
                              " carries morph weights; morph targets are "
                              "unsupported");
        }
        if (!raw.instancingAttributes.empty()) {
            return state.fail("node " + std::to_string(node) +
                              " uses EXT_mesh_gpu_instancing");
        }
        if (const auto* transform = std::get_if<fastgltf::TRS>(&raw.transform)) {
            if (!std::isfinite(transform->translation[0]) ||
                !std::isfinite(transform->translation[1]) ||
                !std::isfinite(transform->translation[2]) ||
                !std::isfinite(transform->scale[0]) ||
                !std::isfinite(transform->scale[1]) ||
                !std::isfinite(transform->scale[2])) {
                return state.fail("node " + std::to_string(node) +
                                  " has a non-finite transform");
            }
            const float qx = transform->rotation[0];
            const float qy = transform->rotation[1];
            const float qz = transform->rotation[2];
            const float qw = transform->rotation[3];
            if (!std::isfinite(qx) || !std::isfinite(qy) ||
                !std::isfinite(qz) || !std::isfinite(qw)) {
                return state.fail("node " + std::to_string(node) +
                                  " has a non-finite rotation");
            }
            const float lengthSquared =
                qx * qx + qy * qy + qz * qz + qw * qw;
            if (!std::isfinite(lengthSquared) || !(lengthSquared > 1e-8f)) {
                return state.fail("node " + std::to_string(node) +
                                  " has a degenerate rotation");
            }
        } else if (const auto* matrix =
                       std::get_if<fastgltf::math::fmat4x4>(&raw.transform)) {
            const float* values = matrix->data();
            for (int index = 0; index < 16; ++index) {
                if (!std::isfinite(values[index])) {
                    return state.fail("node " + std::to_string(node) +
                                      " has a non-finite matrix transform");
                }
            }
        }
    }
    return true;
}

bool validateRawSceneGraph(LoadState& state) {
    const fastgltf::Asset& asset = *state.asset;
    std::vector<bool> hasParent(asset.nodes.size(), false);
    for (const auto& node : asset.nodes) {
        for (const auto child : node.children) {
            if (child < hasParent.size()) hasParent[child] = true;
        }
    }
    for (std::size_t scene = 0u; scene < asset.scenes.size(); ++scene) {
        for (const std::size_t root : asset.scenes[scene].nodeIndices) {
            if (root >= asset.nodes.size()) {
                return state.fail("scene " + std::to_string(scene) +
                                  " has out-of-range root node " +
                                  std::to_string(root));
            }
            if (hasParent[root]) return state.fail("scene root node has a parent");
        }
    }
    return true;
}

bool validateRawSkins(LoadState& state);
bool validateRawAnimations(LoadState& state);

bool preflightRawAsset(LoadState& state) {
    // Image buffer views also reach the shared decoder; validate their ranges
    // even when no geometry accessor refers to them.
    for (const auto& view : state.asset->bufferViews) {
        std::size_t bytes = 0u;
        std::size_t end = 0u;
        if (view.bufferIndex >= state.asset->buffers.size() ||
            !bufferByteSize(state.asset->buffers[view.bufferIndex], bytes) ||
            !checkedAdd(view.byteOffset, view.byteLength, end) || end > bytes) {
            return state.fail("buffer view range exceeds its loaded buffer");
        }
    }
    return validateExtensionPolicy(state) &&
           validateRawNodes(state) &&
           validateRawHierarchy(state) &&
           validateRawSceneGraph(state) &&
           validateRawSkins(state) &&
           validateRawAnimations(state);
}

bool validateHierarchy(LoadState& state,
                       const std::vector<std::vector<int>>& nodeChildren) {
    const std::size_t nodeCount = nodeChildren.size();
    std::vector<int> parent(nodeCount, -1);
    for (std::size_t node = 0u; node < nodeCount; ++node) {
        for (const int child : nodeChildren[node]) {
            if (child < 0 || static_cast<std::size_t>(child) >= nodeCount) {
                return state.fail("node " + std::to_string(node) +
                                  " has out-of-range child " +
                                  std::to_string(child));
            }
            const std::size_t childIndex = static_cast<std::size_t>(child);
            if (parent[childIndex] != -1) {
                return state.fail(
                    "node hierarchy is not a single-parent tree: node " +
                    std::to_string(child) + " has parents " +
                    std::to_string(parent[childIndex]) + " and " +
                    std::to_string(node));
            }
            parent[childIndex] = static_cast<int>(node);
        }
    }
    for (std::size_t node = 0u; node < nodeCount; ++node) {
        std::size_t steps = 0u;
        int cursor = static_cast<int>(node);
        while (cursor != -1) {
            if (steps++ > nodeCount) {
                return state.fail("node hierarchy contains a cycle at node " +
                                  std::to_string(node));
            }
            cursor = parent[static_cast<std::size_t>(cursor)];
        }
    }
    return true;
}

bool validateSkins(LoadState& state,
                   const std::vector<engine::render::model_types::SkinData>& skins,
    std::size_t nodeCount) {
    for (std::size_t index = 0u; index < skins.size(); ++index) {
        const auto& skin = skins[index];
        if (skin.joints.size() > kMaxSkinJoints) {
            return state.fail("skin " + std::to_string(index) + " has " +
                              std::to_string(skin.joints.size()) +
                              " joints; the supported maximum is " +
                              std::to_string(kMaxSkinJoints));
        }
        if (skin.joints.empty()) {
            return state.fail("skin " + std::to_string(index) +
                              " has no joints");
        }
        if (skin.inverseBind.size() != skin.joints.size()) {
            return state.fail(
                "skin " + std::to_string(index) + " has " +
                std::to_string(skin.joints.size()) + " joints but " +
                std::to_string(skin.inverseBind.size()) +
                " inverse bind matrices");
        }
        for (std::size_t joint = 0u; joint < skin.joints.size(); ++joint) {
            const int jointNode = skin.joints[joint];
            if (jointNode < 0 || static_cast<std::size_t>(jointNode) >= nodeCount) {
                return state.fail("skin " + std::to_string(index) +
                                  " joint " + std::to_string(joint) +
                                  " references out-of-range node " +
                                  std::to_string(jointNode));
            }
            if (!finite(skin.inverseBind[joint])) {
                return state.fail("skin " + std::to_string(index) +
                                  " inverse bind matrix " +
                                  std::to_string(joint) + " is not finite");
            }
        }
    }
    return true;
}

// Validates skin counts against the parsed document, before the shared scene
// builder applies its identity-inverse-bind default. A present inverse-bind
// accessor whose element count does not match the joint list is rejected
// rather than silently repaired.
bool validateRawSkins(LoadState& state) {
    const fastgltf::Asset& asset = *state.asset;
    for (std::size_t index = 0u; index < asset.skins.size(); ++index) {
        const fastgltf::Skin& skin = asset.skins[index];
        if (skin.joints.empty()) {
            return state.fail("skin " + std::to_string(index) +
                              " has no joints");
        }
        if (skin.joints.size() > kMaxSkinJoints) {
            return state.fail("skin " + std::to_string(index) + " has " +
                              std::to_string(skin.joints.size()) +
                              " joints; the supported maximum is " +
                              std::to_string(kMaxSkinJoints));
        }
        for (std::size_t joint = 0u; joint < skin.joints.size(); ++joint) {
            if (skin.joints[joint] >= asset.nodes.size()) {
                return state.fail(
                    "skin " + std::to_string(index) + " joint " +
                    std::to_string(joint) + " references out-of-range node " +
                    std::to_string(skin.joints[joint]));
            }
        }
        if (!skin.inverseBindMatrices.has_value()) {
            // The glTF default is an identity inverse bind matrix per joint.
            continue;
        }
        const std::size_t accessorIndex = skin.inverseBindMatrices.value();
        if (accessorIndex >= asset.accessors.size()) {
            return state.fail(
                "skin " + std::to_string(index) +
                " references an out-of-range inverse bind matrix accessor");
        }
        const fastgltf::Accessor& accessor = asset.accessors[accessorIndex];
        if (!floatAccessorValid(state, accessor, skin.joints.size(),
                                fastgltf::AccessorType::Mat4,
                                "skin " + std::to_string(index) +
                                    " inverse bind matrices")) {
            return false;
        }
        std::vector<glm::mat4> matrices;
        engine::render::gltf::model::readMat4(asset, accessor, matrices,
                                              *state.adapter);
        if (matrices.size() != skin.joints.size()) {
            return state.fail("skin " + std::to_string(index) +
                              " inverse bind matrices could not be read");
        }
        for (const glm::mat4& matrix : matrices) {
            if (!finite(matrix)) {
                return state.fail("skin " + std::to_string(index) +
                                  " has a non-finite inverse bind matrix");
            }
        }
    }
    return true;
}

bool validateRawAnimations(LoadState& state) {
    const fastgltf::Asset& asset = *state.asset;
    for (std::size_t clipIndex = 0u; clipIndex < asset.animations.size();
         ++clipIndex) {
        const fastgltf::Animation& clip = asset.animations[clipIndex];
        const std::string label = "animation " + std::to_string(clipIndex);
        for (std::size_t samplerIndex = 0u;
             samplerIndex < clip.samplers.size();
             ++samplerIndex) {
            const fastgltf::AnimationSampler& sampler =
                clip.samplers[samplerIndex];
            const std::string samplerLabel =
                label + " sampler " + std::to_string(samplerIndex);
            if (sampler.inputAccessor >= asset.accessors.size()) {
                return state.fail(samplerLabel +
                                  " references an out-of-range input accessor");
            }
            if (sampler.outputAccessor >= asset.accessors.size()) {
                return state.fail(samplerLabel +
                                  " references an out-of-range output accessor");
            }
            const fastgltf::Accessor& input =
                asset.accessors[sampler.inputAccessor];
            if (input.count == 0u) {
                return state.fail(samplerLabel + " has no key times");
            }
            if (!floatAccessorValid(state, input, input.count,
                                    fastgltf::AccessorType::Scalar,
                                    samplerLabel + " input")) {
                return false;
            }
            const fastgltf::Accessor& output =
                asset.accessors[sampler.outputAccessor];
            if (sampler.interpolation != fastgltf::AnimationInterpolation::Linear &&
                sampler.interpolation != fastgltf::AnimationInterpolation::Step &&
                sampler.interpolation != fastgltf::AnimationInterpolation::CubicSpline) {
                return state.fail(samplerLabel + " has unsupported interpolation");
            }
            if (output.type != fastgltf::AccessorType::Vec3 &&
                output.type != fastgltf::AccessorType::Vec4) {
                return state.fail(samplerLabel +
                                  " output must be VEC3 or VEC4");
            }
            if (output.componentType != fastgltf::ComponentType::Float) {
                return state.fail(samplerLabel + " output must use FLOAT");
            }
            std::size_t expectedOutputs = input.count;
            if (sampler.interpolation ==
                fastgltf::AnimationInterpolation::CubicSpline) {
                if (!checkedMul(input.count, 3u, expectedOutputs)) {
                    return state.fail(samplerLabel +
                                      " cubic output count overflows");
                }
            }
            if (output.count != expectedOutputs) {
                return state.fail(samplerLabel + " has " +
                                  std::to_string(output.count) +
                                  " output elements; expected " +
                                  std::to_string(expectedOutputs));
            }
            if (!accessorPayloadValid(state, output,
                                      samplerLabel + " output")) {
                return false;
            }
            std::vector<float> times;
            engine::render::gltf::model::readScalarFloat(asset, input, times,
                                                         *state.adapter);
            if (times.size() != input.count) {
                return state.fail(samplerLabel +
                                  " key times could not be read");
            }
            for (std::size_t key = 0u; key < times.size(); ++key) {
                if (!std::isfinite(times[key]) || times[key] < 0.0f) {
                    return state.fail(samplerLabel + " key time " +
                                      std::to_string(key) + " is not finite");
                }
                if (key > 0u && !(times[key] > times[key - 1u])) {
                    return state.fail(samplerLabel +
                                      " key times are not strictly increasing");
                }
            }
        }
        for (std::size_t channelIndex = 0u;
             channelIndex < clip.channels.size();
             ++channelIndex) {
            const fastgltf::AnimationChannel& channel =
                clip.channels[channelIndex];
            const std::string channelLabel =
                label + " channel " + std::to_string(channelIndex);
            if (!channel.nodeIndex.has_value()) {
                return state.fail(channelLabel + " has no target node");
            }
            if (channel.nodeIndex.value() >= asset.nodes.size()) {
                return state.fail(channelLabel +
                                  " targets an out-of-range node");
            }
            if (channel.samplerIndex >= clip.samplers.size()) {
                return state.fail(channelLabel +
                                  " references an out-of-range sampler");
            }
            switch (channel.path) {
                case fastgltf::AnimationPath::Translation:
                case fastgltf::AnimationPath::Rotation:
                case fastgltf::AnimationPath::Scale:
                    break;
                default:
                    return state.fail(channelLabel + " targets an unsupported "
                                                    "channel path");
            }
            const auto& sampler = clip.samplers[channel.samplerIndex];
            const auto& output = asset.accessors[sampler.outputAccessor];
            const bool rotation = channel.path == fastgltf::AnimationPath::Rotation;
            if (output.type != (rotation ? fastgltf::AccessorType::Vec4
                                        : fastgltf::AccessorType::Vec3)) {
                return state.fail(channelLabel + " output shape does not match its path");
            }
            if (rotation) {
                bool valid = true;
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset, output, [&](glm::vec4 value, std::size_t key) {
                        const bool tangent = sampler.interpolation ==
                            fastgltf::AnimationInterpolation::CubicSpline && key % 3u != 1u;
                        const float length = glm::dot(value, value);
                        if (!finite(value) || (!tangent &&
                            (!std::isfinite(length) || length < 1e-8f))) valid = false;
                    }, *state.adapter);
                if (!valid) return state.fail(channelLabel + " contains an invalid rotation");
            }
        }
    }
    return true;
}

bool validateAnimations(
    LoadState& state,
    const std::vector<engine::render::model_types::AnimationClip>& animations,
    std::size_t nodeCount) {
    for (std::size_t clipIndex = 0u; clipIndex < animations.size(); ++clipIndex) {
        const auto& clip = animations[clipIndex];
        if (!std::isfinite(clip.durationSec)) {
            return state.fail("animation " + std::to_string(clipIndex) +
                              " has a non-finite duration");
        }
        for (std::size_t samplerIndex = 0u;
             samplerIndex < clip.samplers.size();
             ++samplerIndex) {
            const auto& sampler = clip.samplers[samplerIndex];
            if (sampler.inputs.empty()) {
                return state.fail(
                    "animation " + std::to_string(clipIndex) + " sampler " +
                    std::to_string(samplerIndex) + " has no key times");
            }
            for (std::size_t key = 0u; key < sampler.inputs.size(); ++key) {
                if (!std::isfinite(sampler.inputs[key])) {
                    return state.fail(
                        "animation " + std::to_string(clipIndex) + " sampler " +
                        std::to_string(samplerIndex) + " key time " +
                        std::to_string(key) + " is not finite");
                }
                if (key > 0u &&
                    !(sampler.inputs[key] > sampler.inputs[key - 1u])) {
                    return state.fail(
                        "animation " + std::to_string(clipIndex) + " sampler " +
                        std::to_string(samplerIndex) +
                        " key times are not strictly increasing");
                }
            }
            for (const glm::vec4& output : sampler.outputs) {
                if (!finite(output)) {
                    return state.fail(
                        "animation " + std::to_string(clipIndex) + " sampler " +
                        std::to_string(samplerIndex) +
                        " has a non-finite output value");
                }
            }
        }
        for (const auto& channel : clip.channels) {
            if (channel.targetNode < 0 ||
                static_cast<std::size_t>(channel.targetNode) >= nodeCount) {
                return state.fail(
                    "animation " + std::to_string(clipIndex) +
                    " channel targets out-of-range node " +
                    std::to_string(channel.targetNode));
            }
            if (channel.samplerIndex < 0 ||
                static_cast<std::size_t>(channel.samplerIndex) >=
                    clip.samplers.size()) {
                return state.fail(
                    "animation " + std::to_string(clipIndex) +
                    " channel references out-of-range sampler " +
                    std::to_string(channel.samplerIndex));
            }
        }
    }
    return true;
}

std::string materialKeyPrefix(const LoadState& state,
                              std::size_t materialIndex,
                              const char* role) {
    return "gltf-scene/" + state.sourceIdentity + "/m" +
           std::to_string(materialIndex) + "/" + role;
}

bool decodeMaterialRole(LoadState& state,
                        std::size_t materialIndex,
                        const char* role,
                        const CPUTexture& cpu,
                        bool authored,
                        std::uint8_t textureSrgb,
                        GltfSceneTexture& out) {
    std::size_t bytes = 0u;
    if (!checkedMul(cpu.rgba.size(), 1u, bytes)) {
        return state.fail("decoded " + std::string(role) +
                          " payload size overflows the byte accounting");
    }
    if (!checkedAdd(state.retainedTextureBytes, bytes,
                    state.retainedTextureBytes)) {
        return state.fail(
            "retained decoded texture bytes overflow the byte accounting");
    }
    if (state.retainedTextureBytes > state.options->maxTextureBytes) {
        return state.fail(
            "retained decoded texture bytes exceed the configured limit of " +
            std::to_string(state.options->maxTextureBytes) + " bytes");
    }
    out = fromCPU(cpu, authored,
                  materialKeyPrefix(state, materialIndex, role));
    out.textureSrgb = textureSrgb;
    ++state.textureCount;
    return true;
}

bool importMaterial(LoadState& state,
                    std::size_t materialIndex,
                    GltfSceneMaterial& out) {
    const fastgltf::Asset& asset = *state.asset;
    const fastgltf::Material& material = asset.materials[materialIndex];
    const std::string name(material.name.begin(), material.name.end());
    const std::string label =
        name.empty() ? ("material " + std::to_string(materialIndex)) :
                       ("material '" + name + "'");

    std::string feature;
    if (materialHasUnsupportedExtension(material, feature)) {
        return state.fail(label + " enables the unsupported material feature " +
                          feature);
    }
    if (material.alphaMode != fastgltf::AlphaMode::Opaque) {
        return state.fail(label +
                          " uses alphaMode MASK/BLEND; only OPAQUE is supported");
    }
    if (material.doubleSided) {
        return state.fail(label +
                          " is double-sided; only single-sided materials are "
                          "supported");
    }

    struct TextureRoute {
        const char* role;
        const fastgltf::TextureInfo* info;
        bool* present;
    };
    const TextureRoute routes[] = {
        {"baseColor", material.pbrData.baseColorTexture.has_value()
                          ? &material.pbrData.baseColorTexture.value()
                          : nullptr,
         &out.hasBaseColorMap},
        {"metallicRoughness",
         material.pbrData.metallicRoughnessTexture.has_value()
             ? &material.pbrData.metallicRoughnessTexture.value()
             : nullptr,
         &out.hasMetallicRoughnessMap},
        {"normal", material.normalTexture.has_value()
                       ? &material.normalTexture.value()
                       : nullptr,
         &out.hasNormalMap},
        {"occlusion", material.occlusionTexture.has_value()
                          ? &material.occlusionTexture.value()
                          : nullptr,
         &out.hasOcclusionMap},
        {"emissive", material.emissiveTexture.has_value()
                         ? &material.emissiveTexture.value()
                         : nullptr,
         &out.hasEmissiveMap},
    };
    for (const TextureRoute& route : routes) {
        if (route.info == nullptr) {
            continue;
        }
        if (!textureInfoSupported(name, route.role, *route.info, state.error)) {
            return false;
        }
        if (route.info->textureIndex >= asset.textures.size()) {
            return state.fail(label + " binds " + route.role +
                              " to out-of-range texture " +
                              std::to_string(route.info->textureIndex));
        }
        std::string detail;
        if (!samplerIsSupported(asset, asset.textures[route.info->textureIndex],
                                detail)) {
            return state.fail(label + " " + route.role + " texture has an " +
                              detail);
        }
        *route.present = true;
    }

    out.name = name;
    out.baseColorFactor = {material.pbrData.baseColorFactor[0],
                           material.pbrData.baseColorFactor[1],
                           material.pbrData.baseColorFactor[2],
                           material.pbrData.baseColorFactor[3]};
    out.metallicFactor = material.pbrData.metallicFactor;
    out.roughnessFactor = material.pbrData.roughnessFactor;
    out.normalScale = material.normalTexture.has_value()
                          ? material.normalTexture->scale
                          : 1.0f;
    out.occlusionStrength = material.occlusionTexture.has_value()
                                ? material.occlusionTexture->strength
                                : 1.0f;
    out.emissiveFactor = {material.emissiveFactor[0],
                          material.emissiveFactor[1],
                          material.emissiveFactor[2]};

    // Bounded material subset: finite factors in their legal ranges and an
    // opaque alpha factor, which is what the shared opaque path can honour.
    const auto finiteInRange = [&state, &label](float value, float minimum,
                                                float maximum,
                                                const char* what) {
        if (!std::isfinite(value) || value < minimum || value > maximum) {
            return state.fail(label + " " + what +
                              " is outside the supported range [" +
                              std::to_string(minimum) + ", " +
                              std::to_string(maximum) + "]");
        }
        return true;
    };
    for (int channel = 0; channel < 4; ++channel) {
        if (!finiteInRange(out.baseColorFactor[static_cast<std::size_t>(channel)],
                           0.0f, 1.0f, "baseColorFactor")) {
            return false;
        }
    }
    if (out.baseColorFactor[3] != 1.0f) {
        return state.fail(label +
                          " has a non-opaque baseColorFactor alpha; only alpha "
                          "factor 1 is supported");
    }
    for (int channel = 0; channel < 3; ++channel) {
        if (!finiteInRange(out.emissiveFactor[static_cast<std::size_t>(channel)],
                           0.0f, 1.0f, "emissiveFactor")) {
            return false;
        }
    }
    if (!finiteInRange(out.metallicFactor, 0.0f, 1.0f, "metallicFactor") ||
        !finiteInRange(out.roughnessFactor, 0.0f, 1.0f, "roughnessFactor") ||
        !finiteInRange(out.normalScale, 0.0f, 100.0f, "normal scale") ||
        !finiteInRange(out.occlusionStrength, 0.0f, 1.0f,
                       "occlusion strength")) {
        return false;
    }

    int texCoord = 0;
    const CPUTexture baseCPU =
        engine::render::gltf::model::decodeBaseColorTextureFast(
            asset,
            state.baseDir,
            static_cast<int>(materialIndex),
            false,
            state.sourcePath,
            &texCoord,
            BaseColorDecodePolicy::PreserveSource);
    if (!decodeMaterialRole(state, materialIndex, "baseColor", baseCPU,
                            out.hasBaseColorMap, 1u, out.baseColor)) {
        return false;
    }

    const CPUTexture normalCPU =
        engine::render::gltf::model::decodeNormalTextureFast(
            asset,
            state.baseDir,
            static_cast<int>(materialIndex),
            false,
            state.sourcePath,
            &texCoord);
    if (!decodeMaterialRole(state, materialIndex, "normal", normalCPU,
                            out.hasNormalMap, 0u, out.normal)) {
        return false;
    }

    const CPUTexture mrCPU =
        engine::render::gltf::model::decodeMetallicRoughnessTextureFast(
            asset,
            state.baseDir,
            static_cast<int>(materialIndex),
            false,
            state.sourcePath,
            &texCoord);
    if (!decodeMaterialRole(state, materialIndex, "metallicRoughness", mrCPU,
                            out.hasMetallicRoughnessMap, 0u,
                            out.metallicRoughness)) {
        return false;
    }

    const CPUTexture aoCPU =
        engine::render::gltf::model::decodeOcclusionTextureFast(
            asset,
            state.baseDir,
            static_cast<int>(materialIndex),
            false,
            state.sourcePath,
            &texCoord);
    if (!decodeMaterialRole(state, materialIndex, "occlusion", aoCPU,
                            out.hasOcclusionMap, 0u, out.occlusion)) {
        return false;
    }

    if (out.hasEmissiveMap) {
        // A present map keeps the existing decoder behaviour, including its
        // black fallback for a corrupt payload.
        const CPUTexture emissiveCPU =
            engine::render::gltf::model::decodeEmissiveTextureFast(
                asset,
                state.baseDir,
                static_cast<int>(materialIndex),
                false,
                state.sourcePath,
                &texCoord);
        if (!decodeMaterialRole(state, materialIndex, "emissive", emissiveCPU,
                                true, 1u, out.emissive)) {
            return false;
        }
    } else {
        // A genuinely absent emissive map is implicit white so the separate
        // emissiveFactor still contributes. This is not a decode failure.
        std::size_t bytes = 0u;
        const GltfSceneTexture implicitWhite = makeImplicitWhite();
        if (!checkedMul(implicitWhite.rgba.size(), 1u, bytes) ||
            !checkedAdd(state.retainedTextureBytes, bytes,
                        state.retainedTextureBytes)) {
            return state.fail(
                "retained decoded texture bytes overflow the byte accounting");
        }
        if (state.retainedTextureBytes > state.options->maxTextureBytes) {
            return state.fail(
                "retained decoded texture bytes exceed the configured limit of " +
                std::to_string(state.options->maxTextureBytes) + " bytes");
        }
        out.emissive = implicitWhite;
        out.emissive.key =
            materialKeyPrefix(state, materialIndex, "emissive") + "/implicit";
        out.emissive.cacheKey = out.emissive.key;
        ++state.textureCount;
    }

    out.bindKey = "gltf-scene/" + state.sourceIdentity + "/m" +
                  std::to_string(materialIndex) + "/" + out.baseColor.key + "/" +
                  out.emissive.key + "/" +
                  hex64(static_cast<std::uint64_t>(materialIndex));
    return true;
}

// The legal glTF default material: neutral opaque metallic-roughness with no
// maps, owned by the published asset so a primitive without an explicit
// material index stays drawable.
bool makeDefaultMaterial(LoadState& state, GltfSceneMaterial& out) {
    const std::size_t materialIndex = state.asset->materials.size();
    const auto makePixel = [](std::array<std::uint8_t, 4> pixel) {
        CPUTexture texture;
        texture.width = 1;
        texture.height = 1;
        texture.wrapS = 10497;
        texture.wrapT = 10497;
        texture.minF = 9987;
        texture.magF = 9729;
        texture.rgba = {pixel[0], pixel[1], pixel[2], pixel[3]};
        return texture;
    };
    const CPUTexture white = makePixel({255u, 255u, 255u, 255u});
    const CPUTexture flatNormal = makePixel({128u, 128u, 255u, 255u});
    out = GltfSceneMaterial{};
    out.name = "gltf-default";
    out.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    out.emissiveFactor = {0.0f, 0.0f, 0.0f};
    out.metallicFactor = 1.0f;
    out.roughnessFactor = 1.0f;
    out.normalScale = 1.0f;
    out.occlusionStrength = 1.0f;
    out.hasBaseColorMap = false;
    out.hasNormalMap = false;
    out.hasMetallicRoughnessMap = false;
    out.hasOcclusionMap = false;
    out.hasEmissiveMap = false;
    if (!decodeMaterialRole(state, materialIndex, "baseColor", white, false, 1u,
                            out.baseColor) ||
        !decodeMaterialRole(state, materialIndex, "normal", flatNormal, false, 0u,
                            out.normal) ||
        !decodeMaterialRole(state, materialIndex, "metallicRoughness", white,
                            false, 0u, out.metallicRoughness) ||
        !decodeMaterialRole(state, materialIndex, "occlusion", white, false, 0u,
                            out.occlusion)) {
        return false;
    }
    std::size_t bytes = 0u;
    const GltfSceneTexture implicitWhite = makeImplicitWhite();
    if (!checkedMul(implicitWhite.rgba.size(), 1u, bytes) ||
        !checkedAdd(state.retainedTextureBytes, bytes,
                    state.retainedTextureBytes)) {
        return state.fail(
            "retained decoded texture bytes overflow the byte accounting");
    }
    if (state.retainedTextureBytes > state.options->maxTextureBytes) {
        return state.fail(
            "retained decoded texture bytes exceed the configured limit of " +
            std::to_string(state.options->maxTextureBytes) + " bytes");
    }
    out.emissive = implicitWhite;
    out.emissive.key =
        materialKeyPrefix(state, materialIndex, "emissive") + "/implicit";
    out.emissive.cacheKey = out.emissive.key;
    ++state.textureCount;
    out.bindKey = "gltf-scene/" + state.sourceIdentity + "/m" +
                  std::to_string(materialIndex) + "/default";
    return true;
}

bool importPrimitive(LoadState& state,
                     int nodeIndex,
                     int meshIndex,
                     int primitiveIndex,
                     int materialIndex,
                     int skinIndex,
                     GltfScenePrimitive& out) {
    const fastgltf::Asset& asset = *state.asset;
    const fastgltf::Primitive& primitive =
        asset.meshes[static_cast<std::size_t>(meshIndex)]
            .primitives[static_cast<std::size_t>(primitiveIndex)];
    const std::string label = meshLabel(meshIndex, primitiveIndex);

    if (primitive.type != fastgltf::PrimitiveType::Triangles) {
        return state.fail("unsupported primitive topology on " + label +
                          ": only TRIANGLES is supported");
    }
    if (!primitive.targets.empty()) {
        return state.fail("unsupported morph targets on " + label);
    }

    const bool hasSkin = skinIndex >= 0;
    for (const fastgltf::Attribute& attribute : primitive.attributes) {
        const std::string name = attributeName(attribute);
        if (!allowedAttribute(name, hasSkin)) {
            if (name == "JOINTS_1" || name == "WEIGHTS_1") {
                return state.fail("unsupported extra joint set '" + name +
                                  "' on " + label);
            }
            if (name == "JOINTS_0" || name == "WEIGHTS_0") {
                return state.fail("" + label + " has " + name +
                                  " but its node declares no skin");
            }
            return state.fail("unsupported attribute '" + name + "' on " +
                              label);
        }
        if (attribute.accessorIndex >= asset.accessors.size()) {
            return state.fail("attribute '" + name + "' on " + label +
                              " references out-of-range accessor " +
                              std::to_string(attribute.accessorIndex));
        }
        if (!accessorPayloadValid(state, asset.accessors[attribute.accessorIndex],
                                  name + " on " + label)) return false;
        const auto& data = asset.accessors[attribute.accessorIndex];
        const bool unsignedSmall = data.componentType == fastgltf::ComponentType::UnsignedByte ||
                                   data.componentType == fastgltf::ComponentType::UnsignedShort;
        const bool isFloat = data.componentType == fastgltf::ComponentType::Float && !data.normalized;
        const bool validComponents = name == "JOINTS_0" ? (unsignedSmall && !data.normalized) :
            ((name == "COLOR_0" || name == "WEIGHTS_0" || name == "TEXCOORD_0")
                ? (isFloat || (unsignedSmall && data.normalized)) : isFloat);
        if (!validComponents) return state.fail(name + " uses unsupported components on " + label);
    }

    const auto findAccessor = [&](const char* name, std::size_t& outAccessor) {
        const auto found = primitive.findAttribute(name);
        if (found == primitive.attributes.end()) {
            return false;
        }
        outAccessor = found->accessorIndex;
        return true;
    };

    std::size_t positionAccessor = 0u;
    if (!findAccessor("POSITION", positionAccessor)) {
        return state.fail("" + label + " has no POSITION attribute");
    }
    const fastgltf::Accessor& position = asset.accessors[positionAccessor];
    if (!accessorTypeIs(position, fastgltf::AccessorType::Vec3) ||
        position.componentType != fastgltf::ComponentType::Float) {
        return state.fail("POSITION on " + label +
                          " must be a float VEC3 accessor");
    }
    const std::size_t vertexCount = position.count;
    if (vertexCount == 0u) {
        return state.fail("" + label + " has no vertices");
    }
    std::size_t vertexTotal = 0u;
    if (!checkedAdd(state.vertexCount, vertexCount, vertexTotal)) {
        return state.fail("vertex count overflows the import accounting");
    }
    if (vertexTotal > state.options->maxVertices) {
        return state.fail("asset vertices exceed the configured limit of " +
                          std::to_string(state.options->maxVertices));
    }

    const auto attributeCountMatches = [&](const char* name,
                                           std::size_t expected) {
        std::size_t accessorIndex = 0u;
        if (!findAccessor(name, accessorIndex)) {
            return true;
        }
        return asset.accessors[accessorIndex].count == expected;
    };
    for (const char* name :
         {"NORMAL", "TANGENT", "TEXCOORD_0", "COLOR_0", "JOINTS_0",
          "WEIGHTS_0"}) {
        if (!attributeCountMatches(name, vertexCount)) {
            return state.fail("attribute " + std::string(name) + " on " + label +
                              " has a different element count than POSITION");
        }
    }

    std::vector<glm::vec3> positions;
    positions.reserve(vertexCount);
    bool localFailure = false;
    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        asset, position,
        [&](glm::vec3 value, std::size_t) {
            if (!finite(value)) {
                localFailure = true;
                return;
            }
            positions.push_back(value);
        },
        *state.adapter);
    if (localFailure) {
        return state.fail("POSITION on " + label + " contains a non-finite value");
    }
    if (positions.size() != vertexCount) {
        return state.fail("POSITION on " + label +
                          " did not decode every element");
    }

    std::vector<std::uint32_t> indices;
    if (primitive.indicesAccessor.has_value()) {
        const std::size_t indexAccessor = primitive.indicesAccessor.value();
        if (indexAccessor >= asset.accessors.size()) {
            return state.fail("indices on " + label +
                              " reference an out-of-range accessor");
        }
        const fastgltf::Accessor& accessor = asset.accessors[indexAccessor];
        if (!accessorPayloadValid(state, accessor, "indices on " + label)) return false;
        if (accessor.type != fastgltf::AccessorType::Scalar) {
            return state.fail("indices on " + label +
                              " must be a SCALAR accessor");
        }
        if (accessor.count % 3u != 0u) {
            return state.fail("index count " +
                              std::to_string(accessor.count) + " on " + label +
                              " is not a multiple of three");
        }
        std::size_t indexTotal = 0u;
        if (!checkedAdd(state.indexCount, accessor.count, indexTotal)) {
            return state.fail("index count overflows the import accounting");
        }
        if (indexTotal > state.options->maxIndices) {
            return state.fail("asset indices exceed the configured limit of " +
                              std::to_string(state.options->maxIndices));
        }
        indices.reserve(accessor.count);
        switch (accessor.componentType) {
            case fastgltf::ComponentType::UnsignedByte: {
                fastgltf::iterateAccessorWithIndex<std::uint8_t>(
                    asset, accessor,
                    [&](std::uint8_t value, std::size_t) {
                        indices.push_back(value);
                    },
                    *state.adapter);
                break;
            }
            case fastgltf::ComponentType::UnsignedShort: {
                fastgltf::iterateAccessorWithIndex<std::uint16_t>(
                    asset, accessor,
                    [&](std::uint16_t value, std::size_t) {
                        indices.push_back(value);
                    },
                    *state.adapter);
                break;
            }
            case fastgltf::ComponentType::UnsignedInt: {
                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset, accessor,
                    [&](std::uint32_t value, std::size_t) {
                        indices.push_back(value);
                    },
                    *state.adapter);
                break;
            }
            default:
                return state.fail("indices on " + label +
                                  " use an unsupported component type; UNSIGNED_"
                                  "BYTE, UNSIGNED_SHORT and UNSIGNED_INT are "
                                  "supported");
        }
        if (indices.size() != accessor.count) {
            return state.fail("indices on " + label +
                              " did not decode every element");
        }
        for (std::size_t index = 0u; index < indices.size(); ++index) {
            if (indices[index] >= vertexCount) {
                return state.fail("index " + std::to_string(indices[index]) +
                                  " at position " + std::to_string(index) +
                                  " on " + label + " is out of range " +
                                  "(vertexCount " +
                                  std::to_string(vertexCount) + ")");
            }
        }
    } else {
        // Generate only after the vertex/index admission limits are checked.
        if (vertexCount % 3u != 0u ||
            vertexCount > std::numeric_limits<std::uint32_t>::max()) {
            return state.fail("non-indexed TRIANGLES require a multiple of three vertices");
        }
        std::size_t indexTotal = 0u;
        if (!checkedAdd(state.indexCount, vertexCount, indexTotal)) {
            return state.fail("index count overflows the import accounting");
        }
        if (indexTotal > state.options->maxIndices) {
            return state.fail("asset indices exceed the configured limit of " +
                              std::to_string(state.options->maxIndices));
        }
        indices.resize(vertexCount);
        for (std::size_t index = 0u; index < vertexCount; ++index) {
            indices[index] = static_cast<std::uint32_t>(index);
        }
    }

    std::vector<glm::vec2> uvs;
    std::size_t uvAccessor = 0u;
    const bool hasUv = findAccessor("TEXCOORD_0", uvAccessor);
    if (hasUv) {
        if (!accessorTypeIs(asset.accessors[uvAccessor],
                            fastgltf::AccessorType::Vec2)) {
            return state.fail("TEXCOORD_0 on " + label +
                              " must be a VEC2 accessor");
        }
        uvs.reserve(vertexCount);
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset, asset.accessors[uvAccessor],
            [&](glm::vec2 value, std::size_t) {
                if (!finite(value)) {
                    localFailure = true;
                    return;
                }
                uvs.push_back(value);
            },
            *state.adapter);
        if (localFailure) {
            return state.fail("TEXCOORD_0 on " + label +
                              " contains a non-finite value");
        }
    } else {
        // Missing UVs are only accepted when no authored map needs them, which
        // is checked once the primitive's material is known. A planar fallback
        // keeps the vertex layout uniform.
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(-std::numeric_limits<float>::max());
        for (const glm::vec3& value : positions) {
            minimum = glm::min(minimum, value);
            maximum = glm::max(maximum, value);
        }
        const glm::vec3 extent = maximum - minimum;
        uvs.reserve(vertexCount);
        for (const glm::vec3& value : positions) {
            uvs.emplace_back(extent.x > 1e-6f ? (value.x - minimum.x) / extent.x
                                              : 0.0f,
                             extent.y > 1e-6f ? (value.y - minimum.y) / extent.y
                                              : 0.0f);
        }
    }

    std::vector<glm::vec3> normals(vertexCount, glm::vec3(0.0f, 1.0f, 0.0f));
    bool hasAuthoredNormals = false;
    std::size_t normalAccessor = 0u;
    if (findAccessor("NORMAL", normalAccessor)) {
        if (!accessorTypeIs(asset.accessors[normalAccessor],
                            fastgltf::AccessorType::Vec3)) {
            return state.fail("NORMAL on " + label +
                              " must be a VEC3 accessor");
        }
        normals.clear();
        normals.reserve(vertexCount);
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset, asset.accessors[normalAccessor],
            [&](glm::vec3 value, std::size_t) {
                if (!finite(value)) {
                    localFailure = true;
                    return;
                }
                normals.push_back(value);
            },
            *state.adapter);
        if (localFailure) {
            return state.fail("NORMAL on " + label +
                              " contains a non-finite value");
        }
        hasAuthoredNormals = true;
    }

    std::vector<glm::vec4> tangents(vertexCount,
                                    glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    std::size_t tangentAccessor = 0u;
    if (findAccessor("TANGENT", tangentAccessor)) {
        if (!accessorTypeIs(asset.accessors[tangentAccessor],
                            fastgltf::AccessorType::Vec4)) {
            return state.fail("TANGENT on " + label +
                              " must be a VEC4 accessor");
        }
        tangents.clear();
        tangents.reserve(vertexCount);
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, asset.accessors[tangentAccessor],
            [&](glm::vec4 value, std::size_t) {
                if (!finite(value)) {
                    localFailure = true;
                    return;
                }
                const glm::vec3 xyz = glm::vec3(value);
                const float lengthSquared = glm::dot(xyz, xyz);
                const glm::vec3 normalised =
                    lengthSquared > 1e-12f ? xyz / std::sqrt(lengthSquared)
                                           : glm::vec3(0.0f);
                tangents.emplace_back(normalised, value.w < 0.0f ? -1.0f : 1.0f);
            },
            *state.adapter);
        if (localFailure) {
            return state.fail("TANGENT on " + label +
                              " contains a non-finite value");
        }
    }

    std::vector<glm::vec4> colors(vertexCount, glm::vec4(1.0f));
    std::size_t colorAccessor = 0u;
    if (findAccessor("COLOR_0", colorAccessor)) {
        const fastgltf::Accessor& accessor = asset.accessors[colorAccessor];
        colors.clear();
        colors.reserve(vertexCount);
        if (accessor.type == fastgltf::AccessorType::Vec3) {
            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset, accessor,
                [&](glm::vec3 value, std::size_t) {
                    if (!finite(value)) {
                        localFailure = true;
                        return;
                    }
                    colors.emplace_back(value, 1.0f);
                },
                *state.adapter);
        } else if (accessor.type == fastgltf::AccessorType::Vec4) {
            fastgltf::iterateAccessorWithIndex<glm::vec4>(
                asset, accessor,
                [&](glm::vec4 value, std::size_t) {
                    if (!finite(value)) {
                        localFailure = true;
                        return;
                    }
                    colors.push_back(value);
                },
                *state.adapter);
        } else {
            return state.fail("COLOR_0 on " + label +
                              " must be a VEC3 or VEC4 accessor");
        }
        if (localFailure) {
            return state.fail("COLOR_0 on " + label +
                              " contains a non-finite value");
        }
        // The bounded subset is an opaque path: vertex alpha cannot be honoured
        // and any out-of-range color would be clamped silently, so both are
        // rejected instead of approximated.
        for (std::size_t index = 0u; index < colors.size(); ++index) {
            const glm::vec4& color = colors[index];
            for (int channel = 0; channel < 4; ++channel) {
                if (color[channel] < 0.0f || color[channel] > 1.0f) {
                    return state.fail("COLOR_0 on " + label + " vertex " +
                                      std::to_string(index) +
                                      " is outside [0, 1]");
                }
            }
            if (color.a != 1.0f) {
                return state.fail("COLOR_0 on " + label + " vertex " +
                                  std::to_string(index) +
                                  " has a non-opaque alpha; only alpha 1 is "
                                  "supported");
            }
        }
    }

    std::vector<glm::u16vec4> joints;
    std::vector<glm::vec4> weights;
    std::size_t jointAccessor = 0u;
    std::size_t weightAccessor = 0u;
    const bool hasJoints = findAccessor("JOINTS_0", jointAccessor);
    const bool hasWeights = findAccessor("WEIGHTS_0", weightAccessor);
    if (hasJoints != hasWeights) {
        return state.fail("" + label +
                          " must carry both JOINTS_0 and WEIGHTS_0");
    }
    if (hasJoints) {
        const fastgltf::Accessor& accessor = asset.accessors[jointAccessor];
        if (accessor.type != fastgltf::AccessorType::Vec4) {
            return state.fail("JOINTS_0 on " + label +
                              " must be a VEC4 accessor");
        }
        joints.reserve(vertexCount);
        if (accessor.componentType == fastgltf::ComponentType::UnsignedByte) {
            fastgltf::iterateAccessorWithIndex<glm::u8vec4>(
                asset, accessor,
                [&](glm::u8vec4 value, std::size_t) {
                    joints.emplace_back(value.x, value.y, value.z, value.w);
                },
                *state.adapter);
        } else if (accessor.componentType ==
                   fastgltf::ComponentType::UnsignedShort) {
            fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
                asset, accessor,
                [&](glm::u16vec4 value, std::size_t) {
                    joints.push_back(value);
                },
                *state.adapter);
        } else {
            return state.fail("JOINTS_0 on " + label +
                              " must use an unsigned component type");
        }

        const fastgltf::Accessor& weightData = asset.accessors[weightAccessor];
        if (weightData.type != fastgltf::AccessorType::Vec4) {
            return state.fail("WEIGHTS_0 on " + label +
                              " must be a VEC4 accessor");
        }
        weights.reserve(vertexCount);
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, weightData,
            [&](glm::vec4 value, std::size_t) {
                if (!finite(value)) {
                    localFailure = true;
                    return;
                }
                weights.push_back(value);
            },
            *state.adapter);
        if (localFailure) {
            return state.fail("WEIGHTS_0 on " + label +
                              " contains a non-finite value");
        }
        if (joints.size() != vertexCount || weights.size() != vertexCount) {
            return state.fail("joint or weight data on " + label +
                              " did not decode every element");
        }
        const std::size_t jointCount =
            static_cast<std::size_t>(
                state.asset->skins[static_cast<std::size_t>(skinIndex)]
                    .joints.size());
        for (std::size_t index = 0u; index < vertexCount; ++index) {
            const glm::u16vec4 joint = joints[index];
            const glm::vec4 weight = weights[index];
            const float weightSum = weight.x + weight.y + weight.z + weight.w;
            if (!std::isfinite(weightSum) || weightSum <= 1e-4f) {
                return state.fail("WEIGHTS_0 on " + label + " has an invalid weight sum");
            }
            const std::uint16_t components[4] = {joint.x, joint.y, joint.z,
                                                 joint.w};
            bool zeroWeightRow = true;
            for (int influence = 0; influence < 4; ++influence) {
                if (weight[influence] < 0.0f) {
                    return state.fail("WEIGHTS_0 on " + label +
                                      " contains a negative weight");
                }
                if (weight[influence] > 0.0f) {
                    zeroWeightRow = false;
                }
                if (components[influence] >= jointCount) {
                    return state.fail(
                        "joint index " + std::to_string(components[influence]) +
                        " on " + label + " vertex " + std::to_string(index) +
                        " is out of range for skin " +
                        std::to_string(skinIndex) + " (" +
                        std::to_string(jointCount) + " joints)");
                }
            }
            if (zeroWeightRow && components[0] >= jointCount) {
                // The zero-weight fallback binds the whole vertex to the first
                // influence, so that index must be valid before it is used.
                return state.fail(
                    "joint index " + std::to_string(components[0]) + " on " +
                    label + " vertex " + std::to_string(index) +
                    " is out of range for the zero-weight fallback of skin " +
                    std::to_string(skinIndex) + " (" +
                    std::to_string(jointCount) + " joints)");
            }
        }
    } else if (hasSkin) {
        return state.fail("" + label +
                          " is bound to skin " + std::to_string(skinIndex) +
                          " but has no JOINTS_0/WEIGHTS_0 data");
    }

    if (!hasAuthoredNormals) {
        engine::render::gltf::model::computeNormalsFromGeometry(positions,
                                                                indices,
                                                                normals);
    }
    bool hasAuthoredTangents = false;
    for (const glm::vec4& tangent : tangents) {
        if (glm::dot(glm::vec3(tangent), glm::vec3(tangent)) > 1e-10f) {
            hasAuthoredTangents = true;
            break;
        }
    }
    if (!hasAuthoredTangents) {
        engine::render::gltf::model::computeTangentsFromGeometry(
            positions, uvs, normals, indices, tangents);
        for (glm::vec4& tangent : tangents) {
            tangent.w = 0.0f;
        }
    }

    out = GltfScenePrimitive{};
    out.nodeIndex = nodeIndex;
    out.meshIndex = meshIndex;
    out.primitiveIndex = primitiveIndex;
    out.materialIndex = materialIndex;
    out.skinIndex = skinIndex;
    out.vertices.resize(vertexCount);
    for (std::size_t index = 0u; index < vertexCount; ++index) {
        WorldMeshVertex& vertex = out.vertices[index];
        vertex.x = positions[index].x;
        vertex.y = positions[index].y;
        vertex.z = positions[index].z;
        vertex.u = uvs[index].x;
        vertex.v = uvs[index].y;
        vertex.r = colors[index].r;
        vertex.g = colors[index].g;
        vertex.b = colors[index].b;
        vertex.a = colors[index].a;
        vertex.nx = normals[index].x;
        vertex.ny = normals[index].y;
        vertex.nz = normals[index].z;
        vertex.tx = tangents[index].x;
        vertex.ty = tangents[index].y;
        vertex.tz = tangents[index].z;
        vertex.tw = tangents[index].w;
        if (!joints.empty()) {
            const glm::u16vec4 joint = joints[index];
            glm::vec4 weight = weights[index];
            const float sum = weight.x + weight.y + weight.z + weight.w;
            if (!(sum > 1e-4f)) {
                weight = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            } else {
                weight /= sum;
            }
            vertex.joint0 = static_cast<float>(joint.x);
            vertex.joint1 = static_cast<float>(joint.y);
            vertex.joint2 = static_cast<float>(joint.z);
            vertex.joint3 = static_cast<float>(joint.w);
            vertex.weight0 = weight.x;
            vertex.weight1 = weight.y;
            vertex.weight2 = weight.z;
            vertex.weight3 = weight.w;
        } else {
            vertex.weight0 = 1.0f;
        }
    }
    out.indices = std::move(indices);
    out.triangleCount = out.indices.size() / 3u;

    std::uint64_t geometryHash = fnv1a64(
        out.vertices.data(),
        out.vertices.size() * sizeof(WorldMeshVertex),
        1469598103934665603ull);
    geometryHash = fnv1a64(out.indices.data(),
                           out.indices.size() * sizeof(std::uint32_t),
                           geometryHash);
    out.geometryKey = "gltf-scene/" + state.sourceIdentity + "/g/" +
                      std::to_string(nodeIndex) + "/" +
                      std::to_string(meshIndex) + "/" +
                      std::to_string(primitiveIndex) + "/" +
                      hex64(geometryHash);

    state.vertexCount = vertexTotal;
    std::size_t indexTotal = 0u;
    if (!checkedAdd(state.indexCount, out.indices.size(), indexTotal)) {
        return state.fail("index count overflows the import accounting");
    }
    state.indexCount = indexTotal;
    std::size_t geometryBytes = 0u;
    if (!checkedMul(out.vertices.size(), sizeof(WorldMeshVertex),
                    geometryBytes) ||
        !checkedAdd(geometryBytes,
                    out.indices.size() * sizeof(std::uint32_t),
                    geometryBytes)) {
        return state.fail("geometry byte accounting overflow");
    }
    if (!checkedAdd(state.retainedGeometryBytes, geometryBytes,
                    state.retainedGeometryBytes)) {
        return state.fail("geometry byte accounting overflow");
    }
    return true;
}

bool buildAsset(LoadState& state, GltfSceneAsset& out) {
    const fastgltf::Asset& asset = *state.asset;
    if (asset.nodes.size() > state.options->maxNodes ||
        asset.nodes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return state.fail("asset has " + std::to_string(asset.nodes.size()) +
                          " nodes; the configured limit is " +
                          std::to_string(state.options->maxNodes));
    }
    // Every reference, range, stride, type and count is validated against the
    // parsed document and the loaded bytes before any helper indexes an array
    // or iterates an accessor.
    if (!preflightRawAsset(state)) {
        return false;
    }
    bool sceneIndexValid = false;
    const std::size_t selectedScene = selectedSceneIndex(state, sceneIndexValid);
    if (!sceneIndexValid) {
        return false;
    }

    // Only mesh nodes reachable from the selected scene are submitted. The
    // node/skin arrays stay index-compatible with the document so palettes and
    // poses keep referring to the same node indices.
    std::vector<bool> reachable(asset.nodes.size(), false);
    std::vector<std::size_t> pending;
    for (const std::size_t root : asset.scenes[selectedScene].nodeIndices) {
        pending.push_back(root);
    }
    while (!pending.empty()) {
        const std::size_t node = pending.back();
        pending.pop_back();
        if (node >= reachable.size() || reachable[node]) {
            continue;
        }
        reachable[node] = true;
        for (const std::size_t child : asset.nodes[node].children) {
            pending.push_back(child);
        }
    }

    std::vector<engine::render::model_types::NodeTRS> nodesDefault;
    std::vector<std::string> nodeNames;
    std::vector<std::vector<int>> nodeChildren;
    std::vector<int> nodeMesh;
    std::vector<int> nodeSkin;
    std::vector<int> sceneRoots;
    std::vector<engine::render::model_types::SkinData> skins;
    std::vector<engine::render::model_types::AnimationClip> animations;
    engine::render::gltf::model::buildSceneData(asset,
                                                *state.adapter,
                                                nodesDefault,
                                                &nodeNames,
                                                nodeChildren,
                                                nodeMesh,
                                                nodeSkin,
                                                sceneRoots,
                                                skins,
                                                animations);

    if (!validateHierarchy(state, nodeChildren)) {
        return false;
    }
    if (!validateSkins(state, skins, nodesDefault.size())) {
        return false;
    }
    if (!validateAnimations(state, animations, nodesDefault.size())) {
        return false;
    }

    std::size_t primitiveTotal = 0u;
    for (std::size_t node = 0u; node < nodeMesh.size(); ++node) {
        const int meshIndex = nodeMesh[node];
        if (meshIndex < 0 || !reachable[node]) {
            continue;
        }
        if (!checkedAdd(primitiveTotal,
                        asset.meshes[static_cast<std::size_t>(meshIndex)]
                            .primitives.size(),
                        primitiveTotal)) {
            return state.fail("primitive count overflow");
        }
    }
    if (primitiveTotal > state.options->maxPrimitives) {
        return state.fail("asset has " + std::to_string(primitiveTotal) +
                          " primitives; the configured limit is " +
                          std::to_string(state.options->maxPrimitives));
    }

    // Materials first: a primitive's absent-UV policy depends on whether its
    // material binds maps.
    out.materials.resize(asset.materials.size());
    for (std::size_t materialIndex = 0u; materialIndex < asset.materials.size();
         ++materialIndex) {
        if (!importMaterial(state, materialIndex, out.materials[materialIndex])) {
            return false;
        }
    }
    // The legal glTF default material: a primitive without a material index
    // binds one shared owned neutral material rather than an unusable -1.
    std::size_t defaultMaterialIndex = asset.materials.size();
    bool needsDefaultMaterial = false;
    for (std::size_t node = 0u;
         node < nodeMesh.size() && !needsDefaultMaterial;
         ++node) {
        const int meshIndex = nodeMesh[node];
        if (meshIndex < 0 || !reachable[node]) {
            continue;
        }
        for (const fastgltf::Primitive& primitive :
             asset.meshes[static_cast<std::size_t>(meshIndex)].primitives) {
            if (!primitive.materialIndex.has_value()) {
                needsDefaultMaterial = true;
                break;
            }
        }
    }
    if (needsDefaultMaterial) {
        GltfSceneMaterial material;
        if (!makeDefaultMaterial(state, material)) {
            return false;
        }
        out.materials.push_back(std::move(material));
    } else {
        defaultMaterialIndex = 0u;
    }

    out.primitives.clear();
    out.primitives.reserve(primitiveTotal);
    for (std::size_t node = 0u; node < nodeMesh.size(); ++node) {
        const int meshIndex = nodeMesh[node];
        if (meshIndex < 0 || !reachable[node]) {
            continue;
        }
        const int skinIndex = nodeSkin[node];
        const fastgltf::Mesh& mesh =
            asset.meshes[static_cast<std::size_t>(meshIndex)];
        for (std::size_t primitiveIndex = 0u;
             primitiveIndex < mesh.primitives.size();
             ++primitiveIndex) {
            const fastgltf::Primitive& primitive =
                mesh.primitives[primitiveIndex];
            int materialIndex = -1;
            if (primitive.materialIndex.has_value()) {
                if (primitive.materialIndex.value() >= asset.materials.size()) {
                    return state.fail("primitive references out-of-range material index");
                }
                materialIndex = static_cast<int>(primitive.materialIndex.value());
            } else {
                materialIndex = static_cast<int>(defaultMaterialIndex);
            }
            if (materialIndex >= 0 &&
                static_cast<std::size_t>(materialIndex) >=
                    out.materials.size()) {
                if (!primitive.materialIndex.has_value()) {
                    return state.fail("" +
                                      meshLabel(static_cast<int>(meshIndex),
                                                static_cast<int>(primitiveIndex)) +
                                      " has no material binding");
                }
                return state.fail("" +
                                  meshLabel(static_cast<int>(meshIndex),
                                            static_cast<int>(primitiveIndex)) +
                                  " references out-of-range material " +
                                  std::to_string(materialIndex));
            }
            GltfScenePrimitive imported;
            if (!importPrimitive(state,
                                 static_cast<int>(node),
                                 meshIndex,
                                 static_cast<int>(primitiveIndex),
                                 materialIndex,
                                 skinIndex,
                                 imported)) {
                return false;
            }
            if (materialIndex >= 0) {
                const GltfSceneMaterial& material =
                    out.materials[static_cast<std::size_t>(materialIndex)];
                const bool bindsMaps = material.hasBaseColorMap ||
                                       material.hasNormalMap ||
                                       material.hasMetallicRoughnessMap ||
                                       material.hasOcclusionMap ||
                                       material.hasEmissiveMap;
                const fastgltf::Primitive& source = primitive;
                const bool hasUv =
                    source.findAttribute("TEXCOORD_0") !=
                    source.attributes.end();
                if (bindsMaps && !hasUv) {
                    return state.fail(
                        meshLabel(static_cast<int>(meshIndex),
                                  static_cast<int>(primitiveIndex)) +
                        " has no TEXCOORD_0 but its material binds a map; "
                        "missing UVs are accepted only when no authored map "
                        "needs them");
                }
            }
            out.primitives.push_back(std::move(imported));
        }
    }

    out.nodes.clear();
    out.nodes.resize(nodesDefault.size());
    for (std::size_t node = 0u; node < nodesDefault.size(); ++node) {
        GltfSceneNode& target = out.nodes[node];
        target.name = node < nodeNames.size() ? nodeNames[node] : std::string{};
        target.local = nodesDefault[node];
        target.meshIndex = nodeMesh[node];
        target.skinIndex = nodeSkin[node];
        target.children = nodeChildren[node];
    }
    for (std::size_t node = 0u; node < out.nodes.size(); ++node) {
        for (const int child : out.nodes[node].children) {
            out.nodes[static_cast<std::size_t>(child)].parent =
                static_cast<int>(node);
        }
    }

    out.sceneRoots = sceneRoots;
    out.skins = std::move(skins);
    out.animations = std::move(animations);
    out.nodesDefault = std::move(nodesDefault);
    out.nodeNames = std::move(nodeNames);
    out.nodeChildren = std::move(nodeChildren);

    // The default (bind) globals come from the shared sampler so the published
    // static transform matches what a bind-pose sample produces.
    {
        const engine::render::animation::PoseSource source{
            out.nodesDefault,
            out.nodeChildren,
            out.sceneRoots,
            out.skins,
            out.animations,
            out.nodeNames};
        std::vector<engine::render::model_types::NodeTRS> bindLocal;
        std::vector<glm::mat4> bindGlobal;
        engine::render::animation::samplePose(
            source, 0.0f, kBindPoseClip, bindLocal, bindGlobal);
        for (std::size_t node = 0u; node < out.nodes.size(); ++node) {
            if (!finite(bindGlobal[node])) {
                return state.fail("node " + std::to_string(node) + " has a non-finite global transform");
            }
            out.nodes[node].defaultGlobal = bindGlobal[node];
        }
    }
    return true;
}

}  // namespace

std::uint64_t gltfSceneContentHash(const void* data, std::size_t bytes) noexcept {
    return fnv1a64(data, bytes, 1469598103934665603ull);
}

bool gltfScenePoseMatchesAsset(const GltfSceneAsset& asset,
                               const GltfScenePose& pose) noexcept {
    return pose.assetGeneration == asset.generation &&
           pose.assetIdentity == asset.sourceIdentity;
}

namespace {

bool virtualScenePath(const std::string& base, const std::string& path,
                      std::string& out) {
    if (path.empty() || path.front() == '/' ||
        path.find_first_of("\\:%?#") != std::string::npos ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32u; }))
        return false;
    std::vector<std::string> parts;
    const std::string combined = base + path;
    std::size_t begin = 0u;
    while (begin <= combined.size()) {
        const auto end = combined.find('/', begin);
        const auto part = combined.substr(begin, end == std::string::npos
            ? std::string::npos : end - begin);
        if (part == "..") {
            if (parts.empty()) return false;
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        begin = end + 1u;
    }
    if (parts.empty()) return false;
    out.clear();
    for (const auto& part : parts) {
        if (!out.empty()) out += '/';
        out += part;
    }
    return true;
}

bool resolveStoreSources(fastgltf::Asset& asset, const engine::IAssetStore& store,
                         const std::string& path, std::string& error) {
    const auto slash = path.rfind('/');
    const auto base = slash == std::string::npos ? std::string{} : path.substr(0, slash + 1u);
    const auto resolve = [&](fastgltf::DataSource& source) {
        const auto* uri = std::get_if<fastgltf::sources::URI>(&source);
        if (!uri) return true;
        // Embedded data URIs are handled by the parser/shared image decoder.
        if (uri->uri.isDataUri()) return true;
        std::string resolved;
        if (uri->fileByteOffset != 0u ||
            !virtualScenePath(base, std::string(uri->uri.string()), resolved)) {
            error = "unsupported or escaping glTF store resource path";
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (!store.readBytes(resolved, bytes, &error)) {
            error = "could not read glTF store resource '" + resolved + "': " + error;
            return false;
        }
        fastgltf::sources::Vector owned;
        owned.mimeType = uri->mimeType;
        owned.bytes.resize(bytes.size());
        if (!bytes.empty()) std::memcpy(owned.bytes.data(), bytes.data(), bytes.size());
        source = std::move(owned);
        return true;
    };
    for (auto& buffer : asset.buffers) if (!resolve(buffer.data)) return false;
    for (auto& image : asset.images) if (!resolve(image.data)) return false;
    return true;
}

bool loadSceneImpl(const engine::IAssetStore* store, const std::string& path,
                        std::shared_ptr<const GltfSceneAsset>& out,
                        std::string* error,
                        const GltfSceneLoadOptions& options) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (path.empty()) {
        return fail("glTF scene path must not be empty");
    }

    const auto started = std::chrono::steady_clock::now();
    const std::filesystem::path requested(path);
    std::error_code pathError;
    const std::filesystem::path absolute = store ? requested :
        std::filesystem::absolute(requested, pathError);
    const std::filesystem::path usePath = store ? requested : (pathError ? requested : absolute);

    std::vector<std::uint8_t> fileBytes;
    if (store) {
        std::string virtualPath;
        if (!virtualScenePath({}, path, virtualPath)) return fail("invalid glTF virtual document path");
        std::string readError;
        if (!store->readBytes(virtualPath, fileBytes, &readError))
            return fail("could not read glTF store document: " + readError);
        if (fileBytes.empty()) return fail("glTF store document is empty");
    } else {
        std::ifstream input(usePath, std::ios::binary);
        if (!input) {
            return fail("could not open glTF scene source: " +
                        usePath.string());
        }
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        if (size <= 0) {
            return fail("glTF scene source is empty: " + usePath.string());
        }
        input.seekg(0, std::ios::beg);
        fileBytes.resize(static_cast<std::size_t>(size));
        if (!input.read(reinterpret_cast<char*>(fileBytes.data()), size)) {
            return fail("could not read glTF scene source: " + usePath.string());
        }
    }

    auto data = fastgltf::GltfDataBuffer::FromBytes(
        reinterpret_cast<const std::byte*>(fileBytes.data()), fileBytes.size());
    if (data.error() != fastgltf::Error::None) {
        return fail("could not read the glTF container (" +
                    std::string(engine::render::gltf::loader::errorName(
                        data.error())) +
                    "): " + usePath.string());
    }

    const fastgltf::Options parseOptions = store ? fastgltf::Options::None :
        (fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);
    fastgltf::Parser parser(engine::render::gltf::loader::kSupportedExtensionsMask);
    auto parsed = parser.loadGltf(
        data.get(), store ? std::filesystem::path{} : usePath.parent_path(),
        parseOptions, fastgltf::Category::All);
    if (parsed.error() != fastgltf::Error::None) {
        return fail("glTF parse failed (" +
                    std::string(engine::render::gltf::loader::errorName(
                        parsed.error())) +
                    "): " + usePath.string());
    }

    fastgltf::Asset& asset = parsed.get();
    if (store) {
        std::string normalized, resolutionError;
        if (!virtualScenePath({}, path, normalized) ||
            !resolveStoreSources(asset, *store, normalized, resolutionError))
            return fail(resolutionError);
    }
    fastgltf::DefaultBufferDataAdapter adapter{};
    static std::atomic<std::uint64_t> generationCounter{0u};

    LoadState state;
    state.asset = &asset;
    state.adapter = &adapter;
    state.baseDir = usePath.parent_path();
    state.sourcePath = usePath.string();
    state.sourceIdentity =
        hex64(fnv1a64(fileBytes.data(), fileBytes.size(),
                      1469598103934665603ull));
    state.generation = generationCounter.fetch_add(1u) + 1u;
    state.options = &options;

    auto built = std::make_shared<GltfSceneAsset>();
    built->sourcePath = state.sourcePath;
    built->sourceIdentity = state.sourceIdentity;
    built->generation = state.generation;

    if (!buildAsset(state, *built)) {
        // `out` and its current shared payload are untouched on failure.
        return fail(state.error.empty()
                        ? "glTF scene import was rejected"
                        : ("glTF scene import was rejected: " + state.error));
    }

    const auto finished = std::chrono::steady_clock::now();
    built->stats.nodeCount = built->nodes.size();
    built->stats.primitiveCount = built->primitives.size();
    built->stats.vertexCount = state.vertexCount;
    built->stats.indexCount = state.indexCount;
    built->stats.materialCount = built->materials.size();
    built->stats.textureCount = state.textureCount;
    built->stats.skinCount = built->skins.size();
    built->stats.clipCount = built->animations.size();
    built->stats.sourceFileBytes = fileBytes.size();
    built->stats.retainedGeometryBytes = state.retainedGeometryBytes;
    built->stats.retainedTextureBytes = state.retainedTextureBytes;
    built->stats.retainedAssetBytes =
        static_cast<std::uint64_t>(state.retainedGeometryBytes) +
        static_cast<std::uint64_t>(state.retainedTextureBytes);
    built->stats.loadSeconds =
        std::chrono::duration<double>(finished - started).count();

    out = std::move(built);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace

bool loadGltfSceneAsset(const std::string& path,
                        std::shared_ptr<const GltfSceneAsset>& out,
                        std::string* error, const GltfSceneLoadOptions& options) {
    return loadSceneImpl(nullptr, path, out, error, options);
}

bool loadGltfSceneAsset(const engine::IAssetStore& store, const std::string& path,
                        std::shared_ptr<const GltfSceneAsset>& out,
                        std::string* error, const GltfSceneLoadOptions& options) {
    try {
        return loadSceneImpl(&store, path, out, error, options);
    } catch (const std::exception& exception) {
        if (error) *error = std::string("glTF store import failed: ") + exception.what();
        return false;
    }
}

namespace {

bool buildPosePalettes(const GltfSceneAsset& asset,
                       GltfScenePose& pose,
                       std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    for (const auto& matrix : pose.global) {
        if (!finite(matrix)) return fail("sampled node transform is not finite");
    }
    pose.palettes.resize(asset.primitives.size());
    for (std::size_t primitiveIndex = 0u;
         primitiveIndex < asset.primitives.size();
         ++primitiveIndex) {
        const GltfScenePrimitive& primitive = asset.primitives[primitiveIndex];
        if (primitive.skinIndex < 0) continue;
        const auto& skin = asset.skins[static_cast<std::size_t>(primitive.skinIndex)];
        const std::size_t nodeIndex = static_cast<std::size_t>(primitive.nodeIndex);
        const glm::mat4& meshGlobal = pose.global[nodeIndex];
        const float determinant = glm::determinant(meshGlobal);
        if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12f)
            return fail("the mesh node transform for primitive " +
                        std::to_string(primitiveIndex) + " is not invertible");
        const glm::mat4 inverseMesh = glm::inverse(meshGlobal);
        std::vector<glm::mat4>& palette = pose.palettes[primitiveIndex];
        palette.resize(skin.joints.size());
        for (std::size_t joint = 0u; joint < skin.joints.size(); ++joint) {
            const std::size_t jointNode = static_cast<std::size_t>(skin.joints[joint]);
            const glm::mat4 matrix = inverseMesh * pose.global[jointNode] *
                                     skin.inverseBind[joint];
            if (!finite(matrix))
                return fail("the skin palette for primitive " +
                            std::to_string(primitiveIndex) + " joint " +
                            std::to_string(joint) + " is not finite");
            palette[joint] = matrix;
        }
    }
    return true;
}

} // namespace

bool sampleGltfScenePose(const std::shared_ptr<const GltfSceneAsset>& asset,
                         float timeSec,
                         int clipIndex,
                         GltfScenePose& out,
                         std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) *error = std::move(message);
        return false;
    };
    if (!asset) {
        return fail("cannot sample a pose without a glTF scene asset");
    }
    if (!std::isfinite(timeSec)) {
        return fail("pose time must be finite");
    }
    if (clipIndex != kBindPoseClip &&
        (clipIndex < 0 ||
         static_cast<std::size_t>(clipIndex) >= asset->animations.size())) {
        return fail("pose clip index " + std::to_string(clipIndex) +
                    " is outside the asset's " +
                    std::to_string(asset->animations.size()) + " clips");
    }

    const auto started = std::chrono::steady_clock::now();
    GltfScenePose pose;
    pose.assetIdentity = asset->sourceIdentity;
    pose.assetGeneration = asset->generation;
    pose.clipIndex = clipIndex;
    pose.timeSec = clipIndex == kBindPoseClip ? 0.0f : timeSec;

    const engine::render::animation::PoseSource source{
        asset->nodesDefault,
        asset->nodeChildren,
        asset->sceneRoots,
        asset->skins,
        asset->animations,
        asset->nodeNames};
    engine::render::animation::samplePose(source,
                                          pose.timeSec,
                                          clipIndex,
                                          pose.local,
                                          pose.global);

    if (!buildPosePalettes(*asset, pose, error)) return false;

    const auto finished = std::chrono::steady_clock::now();
    pose.sampleSeconds =
        std::chrono::duration<double>(finished - started).count();
    out = std::move(pose);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool sampleGltfSceneBlendedPose(
    const std::shared_ptr<const GltfSceneAsset>& asset,
    const engine::render::animation::PoseBlendRequest& request,
    GltfScenePose& out,
    std::string* error) {
    if (!asset) {
        if (error) *error = "cannot blend a pose without a glTF scene asset";
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    const engine::render::animation::PoseSource source{
        asset->nodesDefault, asset->nodeChildren, asset->sceneRoots,
        asset->skins, asset->animations, asset->nodeNames};
    GltfScenePose pose;
    if (!engine::render::animation::blendPose(
            source, request, pose.local, pose.global, error)) return false;
    pose.assetIdentity = asset->sourceIdentity;
    pose.assetGeneration = asset->generation;
    pose.clipIndex = request.baseClipIndex;
    pose.timeSec = request.baseClipIndex == kBindPoseClip
                       ? 0.0f : request.baseTimeSec;
    if (!buildPosePalettes(*asset, pose, error)) return false;
    pose.sampleSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    out = std::move(pose);
    if (error) error->clear();
    return true;
}

}  // namespace engine::render::gltf::scene
