#pragma once

#include "engine/core/IAssetStore.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::assets::lgpe {

struct CanonicalVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 4> tangent{};
    std::array<float, 4> bitangent{};
    std::array<std::array<float, 2>, 4> texcoords{};
    std::array<std::array<float, 4>, 4> colors{};
    float normalW = 1.0f;
    std::array<std::int32_t, 4> joints{};
    std::array<float, 4> weights{};
};

struct VertexAttribute {
    std::uint32_t vertexType = 0u;
    std::string semanticHint;
    std::uint32_t bufferFormat = 0u;
    std::uint32_t elementCount = 0u;
};

struct PolygonGroup {
    std::uint32_t materialIndex = 0u;
    std::string primitiveType;
    std::vector<std::uint32_t> indices;
};

struct Mesh {
    std::uint32_t sourceIndex = 0u;
    std::string name;
    std::array<float, 16> transform{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> boundsMinimum{};
    std::array<float, 3> boundsMaximum{};
    std::vector<VertexAttribute> attributes;
    std::vector<CanonicalVertex> vertices;
    std::vector<std::uint8_t> sourceRawVertexData;
    std::vector<PolygonGroup> polygonGroups;
};

struct TextureBinding {
    std::string textureName;
    std::string samplerName;
    std::string textureType;
    std::int32_t textureUnit = 0;
    std::string wrapS;
    std::string wrapT;
    std::string wrapW;
    std::string minFilter;
    std::string magFilter;
    std::array<float, 2> scale{1.0f, 1.0f};
    std::array<float, 2> translate{};
};

struct Material {
    std::uint32_t sourceIndex = 0u;
    std::string name;
    std::string shaderGroup;
    bool skipMainRendering = false;
    std::string sourceMetadataJson;
    std::vector<TextureBinding> textureBindings;
};

struct Bone {
    std::uint32_t sourceIndex = 0u;
    std::string name;
    std::int32_t parentIndex = -1;
    bool hasSkinning = false;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

struct TextureSubresource {
    std::uint32_t arrayLevel = 0u;
    std::uint32_t mipLevel = 0u;
    std::uint32_t depthLevel = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::vector<std::uint8_t> rgba8;
};

struct Texture {
    std::string name;
    std::string sourceContainerRelativePath;
    std::string sourceFormat;
    bool sourceIsSrgb = false;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t depth = 0u;
    std::uint32_t arrayCount = 0u;
    std::uint32_t mipCount = 0u;
    std::vector<TextureSubresource> subresources;
};

struct CanonicalScene {
    std::uint32_t schemaVersion = 0u;
    std::string profileId;
    std::string sourceModelSha256;
    std::uint64_t triangleRecordCount = 0u;
    std::uint64_t uniqueMaterialIndexedTriangleCount = 0u;
    std::uint64_t duplicateMaterialIndexedTriangleRecordCount = 0u;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Bone> bones;
    std::vector<Texture> textures;
};

// Loads the transparent, provisional directory emitted by the LGPE source
// cooker. This is a canonical source representation, not the final runtime
// cache format and not a GLB/PACMDL compatibility path.
bool loadCanonicalScene(const engine::IAssetStore& store,
                        const std::string& virtualRoot,
                        CanonicalScene& out,
                        std::string* outError = nullptr);

} // namespace engine::assets::lgpe
