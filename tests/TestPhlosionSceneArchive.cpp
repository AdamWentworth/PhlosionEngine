#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include <cstdint>
#include <string>
#include <vector>

bool test_phlosion_scene_archive_contract(std::string& outFail) {
    using engine::assets::phlosion::SceneArchiveFile;
    using engine::assets::phlosion::SceneArchiveStore;

    const std::string sceneJson =
        R"({"schema_version":1,"kind":"test_scene"})";
    std::vector<SceneArchiveFile> files;
    files.push_back(
        SceneArchiveFile{
            "cache/lgpe/test/scene.json",
            std::vector<std::uint8_t>(
                sceneJson.begin(),
                sceneJson.end())});
    files.push_back(
        SceneArchiveFile{
            "cache/lgpe/test/geometry.bin",
            {0u, 1u, 2u, 3u, 255u}});

    std::vector<std::uint8_t> encoded;
    std::string error;
    if (!engine::assets::phlosion::encodeSceneArchive(
            "route1-test",
            std::move(files),
            encoded,
            &error)) {
        outFail = "encode failed: " + error;
        return false;
    }

    SceneArchiveStore store;
    if (!store.loadBytes(encoded, &error)) {
        outFail = "load failed: " + error;
        return false;
    }
    if (store.sceneId() != "route1-test" ||
        store.fileCount() != 2u ||
        !store.exists("cache/lgpe/test/scene.json") ||
        store.exists("../scene.json")) {
        outFail = "archive identity or virtual path contract changed";
        return false;
    }
    std::string decodedJson;
    std::vector<std::uint8_t> decodedGeometry;
    if (!store.readText(
            "cache/lgpe/test/scene.json",
            decodedJson,
            &error) ||
        !store.readBytes(
            "cache/lgpe/test/geometry.bin",
            decodedGeometry,
            &error) ||
        decodedJson != sceneJson ||
        decodedGeometry !=
            std::vector<std::uint8_t>({0u, 1u, 2u, 3u, 255u})) {
        outFail = "archive file bytes did not round-trip";
        return false;
    }

    std::vector<std::uint8_t> corrupt = encoded;
    corrupt.back() ^= 0xffu;
    if (store.loadBytes(corrupt, nullptr)) {
        outFail = "archive integrity check accepted corruption";
        return false;
    }
    return true;
}
