#include "engine/assets/lgpe/LgpeCanonicalScene.h"

#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace engine::assets::lgpe {
namespace {

using Json = nlohmann::json;

constexpr std::uint32_t kCanonicalSchemaVersion = 1u;
constexpr std::uint32_t kCanonicalVertexStride = 188u;
constexpr std::uint32_t kMaxMeshes = 100'000u;
constexpr std::uint64_t kMaxVertices = 2'000'000u;
constexpr std::uint64_t kMaxIndices = 12'000'000u;
constexpr std::uint32_t kMaxTextures = 10'000u;
constexpr std::uint64_t kMaxBlobBytes = 512ull * 1024ull * 1024ull;

bool fail(std::string* outError, std::string message) {
    if (outError) *outError = std::move(message);
    return false;
}

std::string joinVirtualPath(const std::string& root, std::string_view leaf) {
    std::string joined = root;
    while (!joined.empty() && (joined.back() == '/' || joined.back() == '\\')) {
        joined.pop_back();
    }
    if (!joined.empty()) joined.push_back('/');
    joined.append(leaf);
    return joined;
}

bool isSafePayloadFileName(const std::string& value) {
    return !value.empty() &&
           value != "." &&
           value != ".." &&
           value.find('/') == std::string::npos &&
           value.find('\\') == std::string::npos &&
           value.find(':') == std::string::npos;
}

class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes)
        : bytes_(bytes) {}

    bool range(std::uint64_t offset, std::uint64_t size) const {
        return offset <= bytes_.size() &&
               size <= static_cast<std::uint64_t>(bytes_.size()) - offset;
    }

    bool readU16(std::uint64_t& offset, std::uint16_t& out) const {
        if (!range(offset, 2u)) return false;
        out = static_cast<std::uint16_t>(bytes_[static_cast<std::size_t>(offset)]) |
              static_cast<std::uint16_t>(
                  bytes_[static_cast<std::size_t>(offset + 1u)] << 8u);
        offset += 2u;
        return true;
    }

    bool readU32(std::uint64_t& offset, std::uint32_t& out) const {
        if (!range(offset, 4u)) return false;
        out = 0u;
        for (std::uint32_t byte = 0u; byte < 4u; ++byte) {
            out |= static_cast<std::uint32_t>(
                       bytes_[static_cast<std::size_t>(offset + byte)])
                   << (byte * 8u);
        }
        offset += 4u;
        return true;
    }

    bool readI32(std::uint64_t& offset, std::int32_t& out) const {
        std::uint32_t value = 0u;
        if (!readU32(offset, value)) return false;
        out = std::bit_cast<std::int32_t>(value);
        return true;
    }

    bool readU64(std::uint64_t& offset, std::uint64_t& out) const {
        if (!range(offset, 8u)) return false;
        out = 0u;
        for (std::uint32_t byte = 0u; byte < 8u; ++byte) {
            out |= static_cast<std::uint64_t>(
                       bytes_[static_cast<std::size_t>(offset + byte)])
                   << (byte * 8u);
        }
        offset += 8u;
        return true;
    }

    bool readFloat(std::uint64_t& offset, float& out) const {
        std::uint32_t bits = 0u;
        if (!readU32(offset, bits)) return false;
        out = std::bit_cast<float>(bits);
        return true;
    }

    bool readBytes(std::uint64_t offset,
                   std::uint64_t size,
                   std::vector<std::uint8_t>& out) const {
        if (!range(offset, size) ||
            size > static_cast<std::uint64_t>(
                       std::numeric_limits<std::size_t>::max())) {
            return false;
        }
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
        out.assign(begin, begin + static_cast<std::ptrdiff_t>(size));
        return true;
    }

    bool magic(std::uint64_t& offset, std::string_view expected) const {
        if (!range(offset, expected.size())) return false;
        const auto* data = bytes_.data() + static_cast<std::size_t>(offset);
        if (std::memcmp(data, expected.data(), expected.size()) != 0) {
            return false;
        }
        offset += expected.size();
        return true;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
};

template <std::size_t N>
bool readFloatArray(const ByteReader& reader,
                    std::uint64_t& offset,
                    std::array<float, N>& out) {
    for (float& value : out) {
        if (!reader.readFloat(offset, value)) return false;
    }
    return true;
}

template <std::size_t N>
std::array<float, N> jsonVector(const Json& value) {
    static constexpr std::array<const char*, 4> kNames{"x", "y", "z", "w"};
    std::array<float, N> result{};
    for (std::size_t index = 0; index < N; ++index) {
        result[index] = value.at(kNames[index]).get<float>();
    }
    return result;
}

std::array<float, 16> jsonMatrix(const Json& value) {
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4u; ++row) {
        const auto values = jsonVector<4>(
            value.at("row" + std::to_string(row)));
        for (std::size_t column = 0; column < 4u; ++column) {
            result[row * 4u + column] = values[column];
        }
    }
    return result;
}

bool readCanonicalVertex(const ByteReader& reader,
                         std::uint64_t& offset,
                         CanonicalVertex& out) {
    if (!readFloatArray(reader, offset, out.position) ||
        !readFloatArray(reader, offset, out.normal) ||
        !readFloatArray(reader, offset, out.tangent) ||
        !readFloatArray(reader, offset, out.bitangent)) {
        return false;
    }
    for (auto& texcoord : out.texcoords) {
        if (!readFloatArray(reader, offset, texcoord)) return false;
    }
    for (auto& color : out.colors) {
        if (!readFloatArray(reader, offset, color)) return false;
    }
    if (!reader.readFloat(offset, out.normalW)) return false;
    for (std::int32_t& joint : out.joints) {
        if (!reader.readI32(offset, joint)) return false;
    }
    return readFloatArray(reader, offset, out.weights);
}

bool switchEnabled(const Json& metadata, std::string_view name) {
    if (!metadata.contains("Switches") ||
        !metadata.at("Switches").is_array()) {
        return false;
    }
    for (const Json& entry : metadata.at("Switches")) {
        if (entry.value("Name", std::string{}) == name &&
            entry.value("Value", false)) {
            return true;
        }
    }
    return false;
}

bool readStoreBytes(const engine::IAssetStore& store,
                    const std::string& path,
                    std::vector<std::uint8_t>& out,
                    std::string* outError) {
    std::string storeError;
    if (!store.readBytes(path, out, &storeError)) {
        return fail(
            outError,
            "unable to read canonical payload '" + path + "'" +
                (storeError.empty() ? std::string{} : ": " + storeError));
    }
    if (out.size() > kMaxBlobBytes) {
        return fail(outError, "canonical payload exceeds the safety limit");
    }
    return true;
}

} // namespace

bool loadCanonicalScene(const engine::IAssetStore& store,
                        const std::string& virtualRoot,
                        CanonicalScene& out,
                        std::string* outError) {
    out = {};
    std::string sceneText;
    std::string storeError;
    const std::string scenePath = joinVirtualPath(virtualRoot, "scene.json");
    if (!store.readText(scenePath, sceneText, &storeError)) {
        return fail(
            outError,
            "unable to read LGPE canonical scene '" + scenePath + "'" +
                (storeError.empty() ? std::string{} : ": " + storeError));
    }

    try {
        const Json root = Json::parse(sceneText);
        if (root.at("schema_version").get<std::uint32_t>() !=
                kCanonicalSchemaVersion ||
            root.at("kind").get<std::string>() !=
                "lgpe_canonical_scene_directory" ||
            root.at("stability").get<std::string>() !=
                "provisional_not_a_frozen_runtime_cache") {
            return fail(outError, "unsupported LGPE canonical scene contract");
        }

        const Json& sourceManifest = root.at("source_manifest");
        if (sourceManifest.at("ingestion").at("mode").get<std::string>() !=
                "direct_source" ||
            sourceManifest.at("ingestion")
                    .at("canonical_bridge")
                    .get<std::string>() != "none" ||
            !sourceManifest.at("validation").at("passed").get<bool>()) {
            return fail(outError, "LGPE canonical scene provenance is invalid");
        }

        const Json& sceneSummary = sourceManifest.at("scene");
        const std::uint32_t expectedMeshCount =
            sceneSummary.at("mesh_count").get<std::uint32_t>();
        const std::uint64_t expectedVertexCount =
            sceneSummary.at("vertex_count").get<std::uint64_t>();
        const std::uint64_t expectedTriangleRecords =
            sceneSummary.at("triangle_record_count").get<std::uint64_t>();
        const std::uint64_t expectedUniqueTriangles =
            sceneSummary.at("unique_material_indexed_triangle_count")
                .get<std::uint64_t>();
        const std::uint64_t expectedDuplicateTriangles =
            sceneSummary
                .at("duplicate_material_indexed_triangle_record_count")
                .get<std::uint64_t>();
        if (expectedMeshCount > kMaxMeshes ||
            expectedVertexCount > kMaxVertices ||
            expectedTriangleRecords > kMaxIndices / 3u) {
            return fail(outError, "LGPE canonical scene exceeds safety limits");
        }

        const Json& payloads = root.at("payloads");
        const Json& geometryDescriptor = payloads.at("geometry");
        const std::string geometryFile =
            geometryDescriptor.at("file_name").get<std::string>();
        if (!isSafePayloadFileName(geometryFile)) {
            return fail(outError, "unsafe LGPE geometry payload file name");
        }
        std::vector<std::uint8_t> geometryBytes;
        if (!readStoreBytes(
                store,
                joinVirtualPath(virtualRoot, geometryFile),
                geometryBytes,
                outError)) {
            return false;
        }
        if (geometryDescriptor.at("size_bytes").get<std::uint64_t>() !=
            geometryBytes.size()) {
            return fail(outError, "LGPE geometry payload size mismatch");
        }

        const ByteReader geometryReader(geometryBytes);
        std::uint64_t headerOffset = 0u;
        std::uint32_t geometryVersion = 0u;
        std::uint32_t vertexStride = 0u;
        std::uint32_t headerMeshCount = 0u;
        std::uint32_t reserved = 0u;
        std::uint64_t headerVertexCount = 0u;
        std::uint64_t headerIndexCount = 0u;
        if (!geometryReader.magic(headerOffset, "LGPEGEOM") ||
            !geometryReader.readU32(headerOffset, geometryVersion) ||
            !geometryReader.readU32(headerOffset, vertexStride) ||
            !geometryReader.readU32(headerOffset, headerMeshCount) ||
            !geometryReader.readU32(headerOffset, reserved) ||
            !geometryReader.readU64(headerOffset, headerVertexCount) ||
            !geometryReader.readU64(headerOffset, headerIndexCount) ||
            geometryVersion != kCanonicalSchemaVersion ||
            vertexStride != kCanonicalVertexStride ||
            headerMeshCount != expectedMeshCount ||
            headerVertexCount != expectedVertexCount ||
            headerIndexCount != expectedTriangleRecords * 3u) {
            return fail(outError, "LGPE geometry header is invalid");
        }

        const Json& manifestMeshes = sourceManifest.at("meshes");
        const Json& payloadMeshes = geometryDescriptor.at("meshes");
        if (manifestMeshes.size() != expectedMeshCount ||
            payloadMeshes.size() != expectedMeshCount) {
            return fail(outError, "LGPE mesh descriptor count mismatch");
        }

        CanonicalScene decoded;
        decoded.schemaVersion = root.at("schema_version").get<std::uint32_t>();
        decoded.profileId = root.at("profile_id").get<std::string>();
        decoded.sourceModelSha256 =
            sourceManifest.at("source")
                .at("model")
                .at("sha256")
                .get<std::string>();
        decoded.triangleRecordCount = expectedTriangleRecords;
        decoded.uniqueMaterialIndexedTriangleCount = expectedUniqueTriangles;
        decoded.duplicateMaterialIndexedTriangleRecordCount =
            expectedDuplicateTriangles;
        decoded.meshes.reserve(expectedMeshCount);

        std::uint64_t decodedVertexTotal = 0u;
        std::uint64_t decodedTriangleTotal = 0u;
        std::uint64_t decodedUniqueTriangleTotal = 0u;
        for (std::uint32_t meshIndex = 0u;
             meshIndex < expectedMeshCount;
             ++meshIndex) {
            const Json& manifestMesh = manifestMeshes.at(meshIndex);
            const Json& payloadMesh = payloadMeshes.at(meshIndex);
            if (manifestMesh.at("index").get<std::uint32_t>() != meshIndex ||
                payloadMesh.at("index").get<std::uint32_t>() != meshIndex ||
                manifestMesh.at("name").get<std::string>() !=
                    payloadMesh.at("name").get<std::string>()) {
                return fail(outError, "LGPE mesh identity mismatch");
            }

            Mesh mesh;
            mesh.sourceIndex = meshIndex;
            mesh.name = manifestMesh.at("name").get<std::string>();
            mesh.transform = jsonMatrix(manifestMesh.at("transform"));
            mesh.boundsMinimum =
                jsonVector<3>(manifestMesh.at("bounds").at("minimum"));
            mesh.boundsMaximum =
                jsonVector<3>(manifestMesh.at("bounds").at("maximum"));
            for (const Json& attribute : manifestMesh.at("attributes")) {
                mesh.attributes.push_back(VertexAttribute{
                    attribute.at("vertex_type").get<std::uint32_t>(),
                    attribute.at("semantic_hint").get<std::string>(),
                    attribute.at("buffer_format").get<std::uint32_t>(),
                    attribute.at("element_count").get<std::uint32_t>(),
                });
            }

            const std::uint64_t vertexCount =
                payloadMesh.at("vertex_count").get<std::uint64_t>();
            const std::uint64_t vertexOffset =
                payloadMesh.at("decoded_vertices_offset_bytes")
                    .get<std::uint64_t>();
            const std::uint64_t vertexBytes =
                payloadMesh.at("decoded_vertices_size_bytes")
                    .get<std::uint64_t>();
            if (vertexCount > kMaxVertices ||
                vertexBytes != vertexCount * kCanonicalVertexStride ||
                !geometryReader.range(vertexOffset, vertexBytes)) {
                return fail(outError, "LGPE decoded vertex range is invalid");
            }
            mesh.vertices.resize(static_cast<std::size_t>(vertexCount));
            std::uint64_t vertexCursor = vertexOffset;
            for (CanonicalVertex& vertex : mesh.vertices) {
                if (!readCanonicalVertex(
                        geometryReader,
                        vertexCursor,
                        vertex)) {
                    return fail(outError, "unable to decode LGPE vertex");
                }
            }

            const std::uint64_t rawOffset =
                payloadMesh.at("source_raw_vertices_offset_bytes")
                    .get<std::uint64_t>();
            const std::uint64_t rawBytes =
                payloadMesh.at("source_raw_vertices_size_bytes")
                    .get<std::uint64_t>();
            if (!geometryReader.readBytes(
                    rawOffset,
                    rawBytes,
                    mesh.sourceRawVertexData)) {
                return fail(outError, "LGPE raw vertex range is invalid");
            }

            const Json& manifestGroups = manifestMesh.at("polygon_groups");
            const Json& payloadGroups = payloadMesh.at("polygon_groups");
            if (manifestGroups.size() != payloadGroups.size()) {
                return fail(outError, "LGPE polygon-group count mismatch");
            }
            std::unordered_set<std::uint64_t> triangleKeys;
            for (std::size_t groupIndex = 0u;
                 groupIndex < payloadGroups.size();
                 ++groupIndex) {
                const Json& manifestGroup = manifestGroups.at(groupIndex);
                const Json& payloadGroup = payloadGroups.at(groupIndex);
                const std::uint32_t materialIndex =
                    payloadGroup.at("material_index").get<std::uint32_t>();
                const std::uint64_t indexCount =
                    payloadGroup.at("index_count").get<std::uint64_t>();
                const std::uint64_t indicesOffset =
                    payloadGroup.at("indices_offset_bytes")
                        .get<std::uint64_t>();
                const std::uint64_t indicesBytes =
                    payloadGroup.at("indices_size_bytes")
                        .get<std::uint64_t>();
                if (materialIndex !=
                        manifestGroup.at("material_index")
                            .get<std::uint32_t>() ||
                    indexCount % 3u != 0u ||
                    indexCount > kMaxIndices ||
                    indicesBytes != indexCount * 2u ||
                    !geometryReader.range(indicesOffset, indicesBytes)) {
                    return fail(outError, "LGPE polygon-group range is invalid");
                }

                PolygonGroup group;
                group.materialIndex = materialIndex;
                group.primitiveType =
                    manifestGroup.at("primitive_type").get<std::string>();
                group.indices.resize(static_cast<std::size_t>(indexCount));
                std::uint64_t indexCursor = indicesOffset;
                for (std::uint32_t& index : group.indices) {
                    std::uint16_t sourceIndex = 0u;
                    if (!geometryReader.readU16(indexCursor, sourceIndex) ||
                        sourceIndex >= vertexCount) {
                        return fail(outError, "LGPE mesh index is invalid");
                    }
                    index = sourceIndex;
                }
                for (std::size_t index = 0u;
                     index < group.indices.size();
                     index += 3u) {
                    const std::uint64_t key =
                        (static_cast<std::uint64_t>(materialIndex) << 48u) |
                        (static_cast<std::uint64_t>(group.indices[index]) << 32u) |
                        (static_cast<std::uint64_t>(group.indices[index + 1u]) << 16u) |
                        static_cast<std::uint64_t>(group.indices[index + 2u]);
                    triangleKeys.insert(key);
                    ++decodedTriangleTotal;
                }
                mesh.polygonGroups.push_back(std::move(group));
            }
            decodedUniqueTriangleTotal += triangleKeys.size();
            decodedVertexTotal += vertexCount;
            decoded.meshes.push_back(std::move(mesh));
        }
        if (decodedVertexTotal != expectedVertexCount ||
            decodedTriangleTotal != expectedTriangleRecords ||
            decodedUniqueTriangleTotal != expectedUniqueTriangles ||
            decodedTriangleTotal - decodedUniqueTriangleTotal !=
                expectedDuplicateTriangles) {
            return fail(outError, "LGPE canonical topology accounting failed");
        }

        const Json& manifestMaterials = sourceManifest.at("materials");
        decoded.materials.reserve(manifestMaterials.size());
        for (const Json& sourceMaterial : manifestMaterials) {
            Material material;
            material.sourceIndex =
                sourceMaterial.at("index").get<std::uint32_t>();
            material.name = sourceMaterial.at("name").get<std::string>();
            material.shaderGroup =
                sourceMaterial.at("shader_group").get<std::string>();
            const Json& metadata = sourceMaterial.at("source_metadata");
            material.skipMainRendering =
                switchEnabled(metadata, "SkipMainRendering");
            material.sourceMetadataJson = metadata.dump();
            for (const Json& sourceBinding :
                 sourceMaterial.at("texture_bindings")) {
                TextureBinding binding;
                binding.textureName =
                    sourceBinding.at("texture_name").get<std::string>();
                binding.samplerName =
                    sourceBinding.at("sampler_name").get<std::string>();
                binding.textureType =
                    sourceBinding.at("texture_type").get<std::string>();
                binding.textureUnit =
                    sourceBinding.at("texture_unit").get<std::int32_t>();
                binding.wrapS = sourceBinding.at("wrap_s").get<std::string>();
                binding.wrapT = sourceBinding.at("wrap_t").get<std::string>();
                binding.wrapW = sourceBinding.at("wrap_w").get<std::string>();
                binding.minFilter =
                    sourceBinding.at("min_filter").get<std::string>();
                binding.magFilter =
                    sourceBinding.at("mag_filter").get<std::string>();
                binding.scale =
                    jsonVector<2>(sourceBinding.at("scale"));
                binding.translate =
                    jsonVector<2>(sourceBinding.at("translate"));
                material.textureBindings.push_back(std::move(binding));
            }
            decoded.materials.push_back(std::move(material));
        }

        const Json& manifestBones =
            sourceManifest.at("skeleton").at("bones");
        decoded.bones.reserve(manifestBones.size());
        for (const Json& sourceBone : manifestBones) {
            decoded.bones.push_back(Bone{
                sourceBone.at("index").get<std::uint32_t>(),
                sourceBone.at("name").get<std::string>(),
                sourceBone.at("parent_index").get<std::int32_t>(),
                sourceBone.at("has_skinning").get<bool>(),
                jsonVector<3>(sourceBone.at("position")),
                jsonVector<4>(sourceBone.at("rotation")),
                jsonVector<3>(sourceBone.at("scale")),
            });
        }

        const Json& textureDescriptor = payloads.at("textures");
        const std::string textureFile =
            textureDescriptor.at("file_name").get<std::string>();
        if (!isSafePayloadFileName(textureFile)) {
            return fail(outError, "unsafe LGPE texture payload file name");
        }
        std::vector<std::uint8_t> textureBytes;
        if (!readStoreBytes(
                store,
                joinVirtualPath(virtualRoot, textureFile),
                textureBytes,
                outError)) {
            return false;
        }
        if (textureDescriptor.at("size_bytes").get<std::uint64_t>() !=
            textureBytes.size()) {
            return fail(outError, "LGPE texture payload size mismatch");
        }
        const ByteReader textureReader(textureBytes);
        std::uint64_t textureHeaderOffset = 0u;
        std::uint32_t textureVersion = 0u;
        std::uint32_t textureCount = 0u;
        if (!textureReader.magic(textureHeaderOffset, "LGPETEXS") ||
            !textureReader.readU32(textureHeaderOffset, textureVersion) ||
            !textureReader.readU32(textureHeaderOffset, textureCount) ||
            textureVersion != kCanonicalSchemaVersion ||
            textureCount !=
                sceneSummary.at("required_texture_count")
                    .get<std::uint32_t>() ||
            textureCount > kMaxTextures) {
            return fail(outError, "LGPE texture header is invalid");
        }
        const Json& payloadTextures = textureDescriptor.at("textures");
        if (payloadTextures.size() != textureCount) {
            return fail(outError, "LGPE texture descriptor count mismatch");
        }
        decoded.textures.reserve(textureCount);
        for (const Json& payloadTexture : payloadTextures) {
            Texture texture;
            texture.name = payloadTexture.at("name").get<std::string>();
            texture.sourceContainerRelativePath =
                payloadTexture.at("source_container_relative_path")
                    .get<std::string>();
            texture.sourceFormat =
                payloadTexture.at("source_format").get<std::string>();
            texture.sourceIsSrgb =
                payloadTexture.at("source_is_srgb").get<bool>();
            texture.width = payloadTexture.at("width").get<std::uint32_t>();
            texture.height = payloadTexture.at("height").get<std::uint32_t>();
            texture.depth = payloadTexture.at("depth").get<std::uint32_t>();
            texture.arrayCount =
                payloadTexture.at("array_count").get<std::uint32_t>();
            texture.mipCount =
                payloadTexture.at("mip_count").get<std::uint32_t>();
            for (const Json& payloadSubresource :
                 payloadTexture.at("subresources")) {
                TextureSubresource subresource;
                subresource.arrayLevel =
                    payloadSubresource.at("array_level").get<std::uint32_t>();
                subresource.mipLevel =
                    payloadSubresource.at("mip_level").get<std::uint32_t>();
                subresource.depthLevel =
                    payloadSubresource.at("depth_level").get<std::uint32_t>();
                subresource.width =
                    payloadSubresource.at("width").get<std::uint32_t>();
                subresource.height =
                    payloadSubresource.at("height").get<std::uint32_t>();
                const std::uint64_t offset =
                    payloadSubresource.at("offset_bytes")
                        .get<std::uint64_t>();
                const std::uint64_t size =
                    payloadSubresource.at("size_bytes").get<std::uint64_t>();
                const std::uint64_t expectedSize =
                    static_cast<std::uint64_t>(subresource.width) *
                    static_cast<std::uint64_t>(subresource.height) * 4u;
                if (size != expectedSize ||
                    !textureReader.readBytes(
                        offset,
                        size,
                        subresource.rgba8)) {
                    return fail(
                        outError,
                        "LGPE texture subresource range is invalid");
                }
                texture.subresources.push_back(std::move(subresource));
            }
            decoded.textures.push_back(std::move(texture));
        }

        out = std::move(decoded);
        if (outError) outError->clear();
        return true;
    } catch (const std::exception& error) {
        return fail(
            outError,
            std::string("invalid LGPE canonical scene: ") + error.what());
    }
}

} // namespace engine::assets::lgpe
