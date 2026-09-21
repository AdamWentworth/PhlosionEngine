#include "MaterialFixturePresentation.h"

#include "MaterialFixtureCase.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace engine::material_fixture {
namespace {

// The renderer's world material debug parameter gate, matching the material
// profile contract checked in tests/CheckMaterialDebugViewContract.cmake.
constexpr std::uint32_t kMaterialMode = 2u;

using engine::render::backend::WorldMeshVertex;
using engine::render::backend::WorldTextureData;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

std::array<float, 16> toArray(const glm::mat4& matrix) {
    std::array<float, 16> out{};
    const float* source = glm::value_ptr(matrix);
    std::copy(source, source + 16, out.begin());
    return out;
}

WorldMeshVertex patchVertex(float x, float y, float z, float u, float v) {
    WorldMeshVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.u = u;
    vertex.v = v;
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;
    // One shared UV set and identity transforms; every quad faces +Z.
    vertex.nx = 0.0f;
    vertex.ny = 0.0f;
    vertex.nz = 1.0f;
    vertex.tx = 1.0f;
    vertex.ty = 0.0f;
    vertex.tz = 0.0f;
    vertex.tw = 1.0f;
    return vertex;
}

std::vector<WorldMeshVertex> patchVertices(std::size_t patchIndex) {
    const std::array<float, 2> center = fixturePatchCenter(patchIndex);
    const float halfWidth = fixturePatchHalfWidth();
    const float halfHeight = fixturePatchHalfHeight();
    const float depth = fixturePatchDepth();
    const float left = center[0] - halfWidth;
    const float right = center[0] + halfWidth;
    return {
        patchVertex(left, -halfHeight, depth, 0.0f, 0.0f),
        patchVertex(right, -halfHeight, depth, 1.0f, 0.0f),
        patchVertex(right, halfHeight, depth, 1.0f, 1.0f),
        patchVertex(left, halfHeight, depth, 0.0f, 1.0f),
    };
}

// The camera fields the world PBR path consumes. The drawing matrix is
// orthographic and independent of these values; they drive the composed lit
// region's shading only.
void applyCamera(WorldTextureData& texture) {
    const std::array<float, 3> position = fixtureCameraPosition();
    const std::array<float, 3> target = fixtureCameraTarget();
    texture.cameraPosX = position[0];
    texture.cameraPosY = position[1];
    texture.cameraPosZ = position[2];
    texture.cameraTargetX = target[0];
    texture.cameraTargetY = target[1];
    texture.cameraTargetZ = target[2];
    const glm::vec3 forward = glm::normalize(glm::vec3(target[0] - position[0],
                                                       target[1] - position[1],
                                                       target[2] - position[2]));
    texture.cameraForwardX = forward.x;
    texture.cameraForwardY = forward.y;
    texture.cameraForwardZ = forward.z;
}

// One WorldTextureData carrying all five decoded roles, their own color-space
// flags and the scalar material factors. Every pointer refers to the
// caller-owned FixtureTextureSet, which stays alive for the whole host run.
WorldTextureData makeTextureData(const FixtureTextureSet& textures) {
    WorldTextureData texture;
    const FixtureRoleTexture& base = textures.role(FixtureRole::BaseColor);
    texture.key = base.key.c_str();
    texture.cacheKey = base.cacheKey.c_str();
    texture.rgba = base.cpu.rgba.data();
    texture.width = static_cast<int>(base.cpu.width);
    texture.height = static_cast<int>(base.cpu.height);
    texture.textureSrgb = static_cast<std::uint8_t>(base.textureSrgb);

    const FixtureRoleTexture& normal = textures.role(FixtureRole::Normal);
    texture.normalKey = normal.key.c_str();
    texture.normalCacheKey = normal.cacheKey.c_str();
    texture.normalRgba = normal.cpu.rgba.data();
    texture.normalWidth = static_cast<int>(normal.cpu.width);
    texture.normalHeight = static_cast<int>(normal.cpu.height);
    texture.normalTextureSrgb = static_cast<std::uint8_t>(normal.textureSrgb);

    const FixtureRoleTexture& mr = textures.role(FixtureRole::MetallicRoughness);
    texture.metallicRoughnessKey = mr.key.c_str();
    texture.metallicRoughnessCacheKey = mr.cacheKey.c_str();
    texture.metallicRoughnessRgba = mr.cpu.rgba.data();
    texture.metallicRoughnessWidth = static_cast<int>(mr.cpu.width);
    texture.metallicRoughnessHeight = static_cast<int>(mr.cpu.height);
    texture.metallicRoughnessTextureSrgb =
        static_cast<std::uint8_t>(mr.textureSrgb);

    const FixtureRoleTexture& occlusion = textures.role(FixtureRole::Occlusion);
    texture.occlusionKey = occlusion.key.c_str();
    texture.occlusionCacheKey = occlusion.cacheKey.c_str();
    texture.occlusionRgba = occlusion.cpu.rgba.data();
    texture.occlusionWidth = static_cast<int>(occlusion.cpu.width);
    texture.occlusionHeight = static_cast<int>(occlusion.cpu.height);
    texture.occlusionTextureSrgb =
        static_cast<std::uint8_t>(occlusion.textureSrgb);

    const FixtureRoleTexture& emissive = textures.role(FixtureRole::Emissive);
    texture.emissiveKey = emissive.key.c_str();
    texture.emissiveCacheKey = emissive.cacheKey.c_str();
    texture.emissiveRgba = emissive.cpu.rgba.data();
    texture.emissiveWidth = static_cast<int>(emissive.cpu.width);
    texture.emissiveHeight = static_cast<int>(emissive.cpu.height);
    texture.emissiveTextureSrgb =
        static_cast<std::uint8_t>(emissive.textureSrgb);

    const FixtureMaterialFactors& factors = textures.factors;
    texture.materialMode = static_cast<std::uint8_t>(kMaterialMode);
    texture.materialFlags = static_cast<float>(textures.presenceFlags());
    texture.alphaMode = factors.alphaMode;
    texture.depthTestEnabled = 1u;
    texture.normalScale = factors.normalScale;
    texture.metallicFactor = factors.metallicFactor;
    texture.roughnessFactor = factors.roughnessFactor;
    texture.occlusionStrength = factors.occlusionStrength;
    texture.emissiveFactorR = factors.emissiveFactor[0];
    texture.emissiveFactorG = factors.emissiveFactor[1];
    texture.emissiveFactorB = factors.emissiveFactor[2];
    applyCamera(texture);
    return texture;
}

void drawBackground(IRenderBackend& renderer,
                    const FixtureTextureSet& textures,
                    const std::array<float, 16>& viewProjection,
                    int width,
                    int height) {
    const std::array<float, 2> halfExtent = fixtureWorldHalfExtent();
    const std::array<float, 3> color = fixtureBackgroundColor();
    const float depth = fixtureBackgroundDepth();
    const std::array<std::array<float, 2>, 4> corners{{
        {-halfExtent[0], -halfExtent[1]},
        {halfExtent[0], -halfExtent[1]},
        {halfExtent[0], halfExtent[1]},
        {-halfExtent[0], halfExtent[1]},
    }};
    const std::array<std::array<float, 2>, 4> uvs{{
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    std::vector<WorldMeshVertex> vertices;
    vertices.reserve(4u);
    for (std::size_t index = 0u; index < corners.size(); ++index) {
        WorldMeshVertex vertex = patchVertex(corners[index][0],
                                             corners[index][1],
                                             depth,
                                             uvs[index][0],
                                             uvs[index][1]);
        vertex.r = color[0];
        vertex.g = color[1];
        vertex.b = color[2];
        vertices.push_back(vertex);
    }
    static const std::uint32_t kQuadIndices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    static const unsigned char kWhitePixel[4] = {255u, 255u, 255u, 255u};
    const std::string key = std::string("fixture/mat/background/") +
        fixtureCaseName(textures.fixtureCase);
    WorldTextureData texture;
    texture.key = key.c_str();
    texture.cacheKey = key.c_str();
    texture.rgba = kWhitePixel;
    texture.width = 1;
    texture.height = 1;
    texture.textureSrgb = 1u;
    texture.alphaMode = 0u;
    texture.depthTestEnabled = 1u;
    texture.materialMode = 0u;
    renderer.drawWorldIndexedMeshTexturedCached(key.c_str(),
                                                vertices.data(),
                                                vertices.size(),
                                                kQuadIndices,
                                                6u,
                                                &texture,
                                                viewProjection.data(),
                                                width,
                                                height);
}

} // namespace

std::array<float, 16> fixtureViewProjection(int surfaceWidth, int surfaceHeight) {
    (void)surfaceWidth;
    (void)surfaceHeight;
    const std::array<float, 2> halfExtent = fixtureWorldHalfExtent();
    // Pixel-exact orthographic mapping: the guard projects the same authored
    // layout with the same extents, so no camera-fit heuristic is involved.
    return toArray(glm::ortho(-halfExtent[0],
                              halfExtent[0],
                              -halfExtent[1],
                              halfExtent[1],
                              -1.0f,
                              1.0f));
}

std::map<std::string, FixturePatchRect> computeFixturePatchRects(
    int surfaceWidth,
    int surfaceHeight) {
    (void)surfaceWidth;
    (void)surfaceHeight;
    // The authored layout is already a normalized top-left rectangle: the
    // orthographic world extent maps linearly onto the surface.
    std::map<std::string, FixturePatchRect> rects;
    const std::array<float, 2> halfExtent = fixtureWorldHalfExtent();
    const float halfWidth = fixturePatchHalfWidth();
    const float halfHeight = fixturePatchHalfHeight();
    for (std::size_t index = 0u;
         index < static_cast<std::size_t>(kFixturePatchCount);
         ++index) {
        const std::array<float, 2> center = fixturePatchCenter(index);
        const float leftNdc = (center[0] - halfWidth) / halfExtent[0];
        const float rightNdc = (center[0] + halfWidth) / halfExtent[0];
        const float bottomNdc = -halfHeight / halfExtent[1];
        const float topNdc = halfHeight / halfExtent[1];
        const FixturePatchRect rect{
            (leftNdc + 1.0f) * 0.5f,
            (1.0f - topNdc) * 0.5f,
            (rightNdc - leftNdc) * 0.5f,
            (topNdc - bottomNdc) * 0.5f};
        rects[fixturePatchName(static_cast<FixturePatch>(index))] = rect;
    }
    return rects;
}

bool renderMaterialFixture(const FixtureTextureSet& textures,
                           IRenderBackend& renderer,
                           int surfaceWidth,
                           int surfaceHeight,
                           FixtureDrawStats* outStats,
                           std::string* outError) {
    const int width = std::max(1, surfaceWidth);
    const int height = std::max(1, surfaceHeight);
    if (textures.roles.size() != static_cast<std::size_t>(kFixtureRoleCount)) {
        return fail(outError, "material fixture needs all five decoded roles");
    }
    for (const FixtureRoleTexture& entry : textures.roles) {
        if (entry.cpu.rgba.size() !=
            static_cast<std::size_t>(entry.cpu.width) *
                static_cast<std::size_t>(entry.cpu.height) * 4u) {
            return fail(outError,
                        "material fixture role '" + entry.roleName +
                            "' has a size that does not match its pixels");
        }
    }

    const std::array<float, 16> viewProjection =
        fixtureViewProjection(width, height);
    FixtureDrawStats stats{};
    drawBackground(renderer, textures, viewProjection, width, height);
    ++stats.drawCalls;
    stats.triangles += 2;

    const WorldTextureData texture = makeTextureData(textures);
    for (std::size_t index = 0u;
         index < static_cast<std::size_t>(kFixturePatchCount);
         ++index) {
        const FixturePatch patch = static_cast<FixturePatch>(index);
        const int debugView = fixturePatchDebugView(patch);
        renderer.setWorldMaterialDebugView(debugView);
        if (debugView > 0) {
            ++stats.debugDraws;
        }
        const std::string key = fixturePatchGeometryKey(
            textures.fixtureCase,
            index,
            textures.role(FixtureRole::BaseColor).contentHash);
        const std::vector<WorldMeshVertex> vertices = patchVertices(index);
        static const std::uint32_t kQuadIndices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
        renderer.drawWorldIndexedMeshTexturedCached(key.c_str(),
                                                    vertices.data(),
                                                    vertices.size(),
                                                    kQuadIndices,
                                                    6u,
                                                    &texture,
                                                    viewProjection.data(),
                                                    width,
                                                    height);
        ++stats.drawCalls;
        stats.triangles += 2;
    }
    // Reset the transient debug state so no later draw inherits a diagnostic
    // view.
    renderer.setWorldMaterialDebugView(0);
    stats.debugStateAfterReset = renderer.worldMaterialDebugView();

    if (outStats) {
        *outStats = stats;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

FixtureHandshake buildFixtureHandshake(std::string host,
                                       std::string requestedBackend,
                                       const FixtureTextureSet& textures,
                                       const IRenderBackend& renderer,
                                       int surfaceWidth,
                                       int surfaceHeight,
                                       const FixtureDrawStats& stats) {
    FixtureHandshake handshake;
    handshake.schema = "phlosion-material-fixture-handshake-v1";
    handshake.sampleIdentity = std::string("phlosion-material-fixture-v1/") +
        fixtureCaseName(textures.fixtureCase);
    handshake.host = std::move(host);
    handshake.caseName = fixtureCaseName(textures.fixtureCase);
    handshake.requestedBackend = std::move(requestedBackend);
    handshake.actualBackend = fixtureBackendName(renderer);
    handshake.gpu = renderer.activeGpuName();
    handshake.gpuDiscrete = renderer.activeGpuIsDiscrete();
    handshake.surfaceWidth = surfaceWidth;
    handshake.surfaceHeight = surfaceHeight;
    handshake.logicalFrame = fixtureCaseLogicalFrame(textures.fixtureCase);
    handshake.assetDirectory = textures.assetDirectory().string();
    std::error_code error;
    const std::filesystem::path assetDirectory = textures.assetDirectory();
    if (!assetDirectory.empty()) {
        for (const auto& entry :
             std::filesystem::directory_iterator(assetDirectory, error)) {
            (void)entry;
            ++handshake.assetFileCount;
        }
    }
    handshake.materialMode = kMaterialMode;
    handshake.presenceFlags = textures.presenceFlags();
    handshake.alphaMode = textures.factors.alphaMode;
    handshake.factors = textures.factors;
    for (const FixtureRoleTexture& entry : textures.roles) {
        FixtureRoleRecord record;
        record.role = entry.roleName;
        record.origin = fixtureRoleOrigin(textures.fixtureCase, entry.role);
        record.width = static_cast<int>(entry.cpu.width);
        record.height = static_cast<int>(entry.cpu.height);
        record.textureSrgb = entry.textureSrgb;
        record.contentHash = entry.contentHash;
        record.key = entry.key;
        record.cacheKey = entry.cacheKey;
        record.wrapS = entry.cpu.wrapS;
        record.wrapT = entry.cpu.wrapT;
        record.minFilter = entry.cpu.minF;
        record.magFilter = entry.cpu.magF;
        record.pixelBytes = entry.cpu.rgba.size();
        for (std::size_t channel = 0u; channel < 4u; ++channel) {
            record.firstPixel[channel] =
                entry.cpu.rgba.size() > channel ? entry.cpu.rgba[channel] : 0u;
        }
        handshake.roles.push_back(std::move(record));
    }
    const std::map<std::string, FixturePatchRect> rects =
        computeFixturePatchRects(surfaceWidth, surfaceHeight);
    for (std::size_t index = 0u;
         index < static_cast<std::size_t>(kFixturePatchCount);
         ++index) {
        const FixturePatch patch = static_cast<FixturePatch>(index);
        FixturePatchRecord record;
        record.patch = fixturePatchName(patch);
        record.debugView = fixturePatchDebugView(patch);
        record.geometryKey = fixturePatchGeometryKey(
            textures.fixtureCase,
            index,
            textures.role(FixtureRole::BaseColor).contentHash);
        record.rect = rects.at(record.patch);
        handshake.patches.push_back(std::move(record));
    }
    handshake.debugStateReset = stats.debugStateAfterReset == 0;
    handshake.drawCalls = stats.drawCalls;
    return handshake;
}

bool writeFixtureHandshake(const std::string& path,
                           const FixtureHandshake& handshake,
                           std::string* outError) {
    if (path.empty()) {
        return fail(outError, "material fixture handshake path must not be empty");
    }
    nlohmann::json document;
    document["schema"] = handshake.schema;
    document["sample_identity"] = handshake.sampleIdentity;
    document["host"] = handshake.host;
    document["case"] = handshake.caseName;
    document["requested_backend"] = handshake.requestedBackend;
    document["actual_backend"] = handshake.actualBackend;
    document["gpu"] = handshake.gpu;
    document["gpu_discrete"] = handshake.gpuDiscrete;
    document["surface"] = {
        {"width", handshake.surfaceWidth},
        {"height", handshake.surfaceHeight}};
    document["logical_frame"] = handshake.logicalFrame;
    document["asset_directory"] = handshake.assetDirectory;
    document["asset_file_count"] = handshake.assetFileCount;
    document["material"] = {
        {"mode", handshake.materialMode},
        {"presence_flags", handshake.presenceFlags},
        {"alpha_mode", handshake.alphaMode},
        {"metallic_factor", handshake.factors.metallicFactor},
        {"roughness_factor", handshake.factors.roughnessFactor},
        {"normal_scale", handshake.factors.normalScale},
        {"occlusion_strength", handshake.factors.occlusionStrength},
        {"emissive_factor", handshake.factors.emissiveFactor}};
    nlohmann::json roles = nlohmann::json::array();
    for (const FixtureRoleRecord& record : handshake.roles) {
        roles.push_back({
            {"role", record.role},
            {"origin", record.origin},
            {"width", record.width},
            {"height", record.height},
            {"texture_srgb", record.textureSrgb},
            {"content_hash", record.contentHash},
            {"key", record.key},
            {"cache_key", record.cacheKey},
            {"wrap_s", record.wrapS},
            {"wrap_t", record.wrapT},
            {"min_filter", record.minFilter},
            {"mag_filter", record.magFilter},
            {"first_pixel", record.firstPixel},
            {"pixel_bytes", record.pixelBytes}});
    }
    document["roles"] = std::move(roles);
    nlohmann::json patches = nlohmann::json::array();
    for (const FixturePatchRecord& record : handshake.patches) {
        patches.push_back({
            {"patch", record.patch},
            {"debug_view", record.debugView},
            {"geometry_key", record.geometryKey},
            {"rect", record.rect}});
    }
    document["patches"] = std::move(patches);
    document["presentation"] = {
        {"debug_state_reset", handshake.debugStateReset},
        {"draw_calls", handshake.drawCalls}};

    try {
        const std::filesystem::path outputPath(path);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output) {
            return fail(outError,
                        "could not open material fixture handshake path: " + path);
        }
        output << document.dump(2) << '\n';
        if (!output) {
            return fail(outError,
                        "could not write material fixture handshake path: " + path);
        }
    } catch (const std::exception& exception) {
        return fail(outError,
                    std::string("material fixture handshake write failed: ") +
                        exception.what());
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

const char* fixtureBackendName(const IRenderBackend& renderer) noexcept {
    const char* backend = renderer.backendId();
    return backend && backend[0] != '\0' ? backend : "unknown";
}

} // namespace engine::material_fixture
