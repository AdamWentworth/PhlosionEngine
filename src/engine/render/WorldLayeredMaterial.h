#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <memory>
#include <string>
#include <vector>

namespace engine::render {
inline constexpr std::uint32_t kWorldLayerLimit=32;
// Stable immutable GPU record. Texture indices address the prepared material's
// texture table; they are never pointers or source-game resource identifiers.
struct alignas(16) WorldLayerRecord {
    std::array<float,4> tintOpacity{1,1,1,1};
    std::array<float,4> roughnessRemap{1,0,1,0};
    std::array<float,4> metalnessRemap{1,0,1,0};
    std::array<float,4> colorMaskRemap{1,0,1,0};
    std::array<float,4> detail{1,1,1,0}; // layer tile, micro tile, inverse contrast, border strength
    std::array<float,4> microOffset{};
    std::array<float,4> layerOffsetNormal{0,0,1,0};
    std::array<std::uint32_t,4> textures{}; // micro; diffuse|normal<<16; rough|metal<<16; reserved
};
static_assert(sizeof(WorldLayerRecord)==128);

struct WorldLayerMaskTile {
    std::uint32_t firstRecord=0, layers=0;
};
struct WorldLayerMaskData {
    std::array<std::uint32_t,2> highResolution{},lowResolution{},atlasExtent{};
    std::uint32_t tileSize=0;
    // Ascending layer-bit order within each tile. Packed locations use X/Y
    // ten-bit atlas tile indices and X/Y four-bit resolution shifts.
    std::vector<WorldLayerMaskTile> high,low;
    std::vector<std::uint32_t> records;
    std::vector<std::uint8_t> atlas;
};
struct WorldLayerMaskSelection {
    std::uint32_t active=0, highLayers=0, lowLayers=0;
    std::uint32_t highFirst=0, lowFirst=0;
    std::array<float,2> flippedUv{};
};
bool validateWorldLayerSetup(std::span<const WorldLayerRecord>,std::uint32_t textureCount,
                            std::uint32_t& active,std::string* error=nullptr);
bool validateWorldLayerMasks(const WorldLayerMaskData&,std::uint32_t layerCount,
                            std::string* error=nullptr);
// Call only on validated immutable data. Invalid coordinates fail explicitly;
// sampling must never access a different layer because an input was NaN.
WorldLayerMaskSelection selectWorldLayerMasks(const WorldLayerMaskData&,
                                             std::uint32_t setupMask,
                                             std::array<float,2> uv);
std::uint32_t worldLayerMaskRecord(const WorldLayerMaskData&,
                                  const WorldLayerMaskSelection&,std::uint32_t layer);
std::array<float,2> worldLayerMaskUv(const WorldLayerMaskData&,
                                   std::uint32_t packed,std::array<float,2> flippedUv);
// Scene-owned, immutable texture table. Every entry has an explicit colour space
// and a complete authored mip chain. Index zero is valid; absent optional maps
// use kWorldLayerNoTexture. Backends upload these once in the resource scope.
inline constexpr std::uint32_t kWorldLayerTextureLimit=512;
inline constexpr std::uint32_t kWorldLayerNoTexture=~0u;
struct WorldLayerTextureMip {
    std::uint32_t width=0,height=0;
    std::vector<std::uint8_t> rgba;
};
struct WorldLayerTexture {
    std::string identity;
    bool srgb=false;
    std::vector<WorldLayerTextureMip> mips;
};
struct WorldLayerMaterial {
    std::vector<WorldLayerRecord> layers;
    WorldLayerMaskData masks;
    std::uint32_t maskTexture=kWorldLayerNoTexture;
    // Terrain uses Z-up dominant planar projection; mesh materials use UV0.
    bool terrain=false;
    std::uint32_t macroColor=kWorldLayerNoTexture,globalNormal=kWorldLayerNoTexture;
    std::uint32_t tilingMap=kWorldLayerNoTexture;
    std::array<float,16> tiling{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    std::array<std::uint32_t,2> selectedLayers{0,31};
    std::array<float,2> uvScale{1,1};
    float normalStrength=1.6f;
    float surfaceAspect=1.f,normalYSign=1.f,globalNormalIntensity=1.f;
    std::array<float,2> globalNormalUvScale{1,1},globalNormalUvBias{};
    bool terrainDistanceFade=true;
    std::array<float,2> normalGradient{1,1},microGradient{1,1};
};
struct WorldLayerMaterialSet {
    std::string identity;
    std::vector<WorldLayerTexture> textures;
    std::vector<WorldLayerMaterial> materials;
};
struct WorldLayerPackedSet {
    // Word addresses, never native pointers. Header: version, material count,
    // texture count, reserved. Then one 32-word header per material.
    std::vector<std::uint32_t> words;
    std::uint64_t textureBytes=0;
};
WorldLayerPackedSet packWorldLayerMaterialSet(const WorldLayerMaterialSet&);
}
