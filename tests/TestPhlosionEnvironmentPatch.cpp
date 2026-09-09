#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"

#include <string>

bool test_phlosion_environment_patch_contract(std::string& outFail) {
    using namespace engine::assets::phlosion;
    EnvironmentPatchVertex a{};
    EnvironmentPatchVertex b{};
    EnvironmentPatchVertex c{};
    b.position[0] = 100.0f;
    c.position[2] = 100.0f;
    a.normal = b.normal = c.normal = {0.0f, 1.0f, 0.0f};
    a.tangent = b.tangent = c.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    a.bitangent = b.bitangent = c.bitangent = {0.0f, 0.0f, 1.0f, 1.0f};
    EnvironmentPatchDocument source{
        .source = EnvironmentPatchSourceLock{
            .profileId = "route-test",
            .modelSha256 = std::string(64u, 'a'),
            .geometrySha256 = std::string(64u, 'b'),
            .coordinateSystem = "source_centimetres_xyz_y_up"},
        .meshes = {EnvironmentPatchMesh{
            .id = "patch/test-triangle",
            .displayName = "Test Triangle",
            .vertices = {a, b, c},
            .materialGroups = {EnvironmentPatchMaterialGroup{
                .materialIndex = 7u,
                .indices = {0u, 1u, 2u}}}}},
        .terrainReplacement = EnvironmentPatchTerrainReplacement{
            .tileSizeCm = 100.0f,
            .cells = {{17, -4}, {18, -4}}}};
    std::string error;
    if (!validateEnvironmentPatchDocument(source, &error)) {
        outFail = "valid patch failed: " + error;
        return false;
    }
    EnvironmentPatchDocument decoded;
    if (!parseEnvironmentPatchDocument(
            serializeEnvironmentPatchDocument(source), decoded, &error)) {
        outFail = "patch round trip failed: " + error;
        return false;
    }
    if (decoded.meshes.size() != 1u ||
        decoded.meshes.front().vertices.size() != 3u ||
        decoded.meshes.front().materialGroups.front().materialIndex != 7u ||
        decoded.source.geometrySha256 != source.source.geometrySha256 ||
        !decoded.terrainReplacement ||
        decoded.terrainReplacement->cells !=
            source.terrainReplacement->cells) {
        outFail = "patch fields changed during round trip";
        return false;
    }
    EnvironmentPatchDocument binaryDecoded;
    const auto binary = serializeEnvironmentPatchBinary(source);
    if (!parseEnvironmentPatchBinary(binary, binaryDecoded, &error) ||
        binaryDecoded.meshes.size() != source.meshes.size() ||
        binaryDecoded.meshes.front().vertices.size() != 3u ||
        !binaryDecoded.terrainReplacement ||
        binaryDecoded.terrainReplacement->cells !=
            source.terrainReplacement->cells) {
        outFail = "binary patch round trip failed: " + error;
        return false;
    }
    decoded.meshes.front().materialGroups.front().indices = {0u, 0u, 2u};
    if (validateEnvironmentPatchDocument(decoded, nullptr)) {
        outFail = "patch accepted a degenerate triangle";
        return false;
    }
    decoded = source;
    decoded.terrainReplacement->cells.push_back({17, -4});
    if (validateEnvironmentPatchDocument(decoded, nullptr)) {
        outFail = "patch accepted a duplicate terrain replacement cell";
        return false;
    }
    return true;
}
