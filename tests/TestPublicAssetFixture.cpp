#include "engine/assets/AsyncAssetLoader.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using engine::assets::AssetLoadId;
using engine::assets::AssetLoadResult;
using engine::assets::AssetLoadState;
using engine::assets::AsyncAssetLoader;
using engine::assets::phlosion::PrefabArchiveStore;
using engine::assets::phlosion::SceneArchiveFile;
using engine::assets::phlosion::SceneArchiveStore;

constexpr std::chrono::milliseconds kWaitTimeout{15000};
constexpr std::chrono::milliseconds kPollInterval{1};

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

// Immutable in-memory provider: synthetic archive bytes are added before use
// and every read returns a copy, so loads stay independent of each other.
class InMemoryAssetStore final : public engine::IAssetStore {
public:
    void add(std::string virtualPath, std::vector<std::uint8_t> bytes) {
        assets_.insert_or_assign(std::move(virtualPath), std::move(bytes));
    }

    std::size_t assetCount() const {
        return assets_.size();
    }

    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError = nullptr) const override {
        std::vector<std::uint8_t> bytes;
        if (!readBytes(virtualPath, bytes, outError)) {
            return false;
        }
        if (bytes.empty()) {
            outText.clear();
        } else {
            outText.assign(
                reinterpret_cast<const char*>(bytes.data()),
                bytes.size());
        }
        return true;
    }

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError = nullptr) const override {
        const auto found = assets_.find(virtualPath);
        if (found == assets_.end()) {
            if (outError) {
                *outError = "test asset store has no asset: " + virtualPath;
            }
            return false;
        }
        outBytes = found->second;
        return true;
    }

    bool exists(const std::string& virtualPath) const override {
        return assets_.contains(virtualPath);
    }

private:
    std::map<std::string, std::vector<std::uint8_t>> assets_;
};

const char* stateName(AssetLoadState state) {
    switch (state) {
    case AssetLoadState::Unknown: return "Unknown";
    case AssetLoadState::Queued: return "Queued";
    case AssetLoadState::Loading: return "Loading";
    case AssetLoadState::Ready: return "Ready";
    case AssetLoadState::Failed: return "Failed";
    case AssetLoadState::Cancelled: return "Cancelled";
    }
    return "invalid";
}

bool waitForState(
    AsyncAssetLoader& loader,
    AssetLoadId id,
    AssetLoadState expected,
    std::string& outFail) {
    const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (loader.state(id) == expected) {
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    outFail = "timed out waiting for load " + std::to_string(id) +
        " to reach " + stateName(expected) + "; last state " +
        stateName(loader.state(id));
    return false;
}

} // namespace

bool test_public_asset_fixture_contract(std::string& outFail) {
    const std::string sceneJson =
        R"({"schema_version":1,"kind":"demo_scene"})";
    const std::vector<std::uint8_t> sceneGeometry{0u, 1u, 2u, 255u};
    const std::string prefabMetadata =
        R"({"size":[1,2,3],"material":"demo/metal"})";
    const std::string materialJson = R"({"shader":"demo/json"})";
    const std::vector<std::uint8_t> prefabMesh{7u, 8u, 9u};

    std::vector<SceneArchiveFile> sceneFiles;
    sceneFiles.push_back(
        SceneArchiveFile{"canonical/scene.json", bytesOf(sceneJson)});
    sceneFiles.push_back(
        SceneArchiveFile{"canonical/geometry.bin", sceneGeometry});
    std::vector<std::uint8_t> sceneArchive;
    std::string error;
    if (!engine::assets::phlosion::encodeSceneArchive(
            "demo/board-scene",
            std::move(sceneFiles),
            sceneArchive,
            &error)) {
        outFail = "synthetic scene archive encode failed: " + error;
        return false;
    }

    std::vector<SceneArchiveFile> prefabFiles;
    prefabFiles.push_back(SceneArchiveFile{
        "canonical/material.json",
        bytesOf(materialJson)});
    prefabFiles.push_back(
        SceneArchiveFile{"canonical/mesh.bin", prefabMesh});
    std::vector<std::uint8_t> prefabArchive;
    if (!engine::assets::phlosion::encodePrefabArchive(
            "demo/crate-a",
            "DemoProp",
            prefabMetadata,
            std::move(prefabFiles),
            prefabArchive,
            &error)) {
        outFail = "synthetic prefab archive encode failed: " + error;
        return false;
    }

    const std::shared_ptr<InMemoryAssetStore> store =
        std::make_shared<InMemoryAssetStore>();
    store->add("scenes/demo-board.phscene", sceneArchive);
    store->add("prefabs/demo-crate-a.phlo", prefabArchive);
    if (store->assetCount() != 2u ||
        !store->exists("scenes/demo-board.phscene") ||
        store->exists("scenes/absent.phscene")) {
        outFail = "the synthetic asset store did not expose fixture content";
        return false;
    }

    AsyncAssetLoader loader(store);
    const AssetLoadId sceneLoad = loader.request("scenes/demo-board.phscene");
    if (sceneLoad == 0u ||
        !waitForState(loader, sceneLoad, AssetLoadState::Ready, outFail)) {
        return false;
    }
    AssetLoadResult sceneResult;
    if (!loader.take(sceneLoad, sceneResult) ||
        sceneResult.state != AssetLoadState::Ready ||
        sceneResult.bytes != sceneArchive ||
        !sceneResult.error.empty()) {
        outFail = "the loader did not return the encoded scene archive bytes";
        return false;
    }

    SceneArchiveStore sceneStore;
    if (!sceneStore.loadBytes(sceneResult.bytes, &error) ||
        sceneStore.sceneId() != "demo/board-scene" ||
        sceneStore.fileCount() != 2u) {
        outFail = "caller-side scene archive decode failed: " + error;
        return false;
    }
    std::string decodedJson;
    std::vector<std::uint8_t> decodedGeometry;
    if (!sceneStore.readText(
            "canonical/scene.json",
            decodedJson,
            &error) ||
        decodedJson != sceneJson ||
        !sceneStore.readBytes(
            "canonical/geometry.bin",
            decodedGeometry,
            &error) ||
        decodedGeometry != sceneGeometry ||
        sceneStore.exists("canonical/missing.bin")) {
        outFail = "decoded scene content did not match the fixture";
        return false;
    }

    const AssetLoadId prefabLoad = loader.request("prefabs/demo-crate-a.phlo");
    if (prefabLoad == 0u ||
        prefabLoad <= sceneLoad ||
        !waitForState(loader, prefabLoad, AssetLoadState::Ready, outFail)) {
        return false;
    }
    AssetLoadResult prefabResult;
    if (!loader.take(prefabLoad, prefabResult) ||
        prefabResult.state != AssetLoadState::Ready ||
        prefabResult.bytes != prefabArchive) {
        outFail = "the loader did not return the encoded prefab archive bytes";
        return false;
    }
    PrefabArchiveStore prefabStore;
    if (!prefabStore.loadBytes(prefabResult.bytes, &error) ||
        prefabStore.prefabId() != "demo/crate-a" ||
        prefabStore.prefabKind() != "DemoProp" ||
        prefabStore.fileCount() != 2u) {
        outFail = "caller-side prefab archive decode failed: " + error;
        return false;
    }
    try {
        if (nlohmann::json::parse(prefabStore.metadataJson()) !=
            nlohmann::json::parse(prefabMetadata)) {
            outFail = "decoded prefab metadata did not match the fixture";
            return false;
        }
    } catch (const nlohmann::json::exception& exception) {
        outFail = "decoded prefab metadata was not valid JSON: " +
            std::string(exception.what());
        return false;
    }
    std::string decodedMaterial;
    std::vector<std::uint8_t> prefabMeshDecoded;
    if (!prefabStore.readText(
            "canonical/material.json",
            decodedMaterial,
            &error) ||
        decodedMaterial != materialJson ||
        !prefabStore.readBytes(
            "canonical/mesh.bin",
            prefabMeshDecoded,
            &error) ||
        prefabMeshDecoded != prefabMesh) {
        outFail = "decoded prefab content did not match the fixture";
        return false;
    }

    // Reload after release: the same virtual path resolves again and retained
    // bytes are returned to the budget.
    const AssetLoadId reload = loader.request("scenes/demo-board.phscene");
    if (reload == 0u ||
        reload <= prefabLoad ||
        !waitForState(loader, reload, AssetLoadState::Ready, outFail) ||
        loader.residentBytes() != sceneArchive.size()) {
        outFail = "a released archive path did not reload";
        return false;
    }
    if (!loader.release(reload) ||
        loader.state(reload) != AssetLoadState::Unknown ||
        loader.residentBytes() != 0u) {
        outFail = "releasing a loaded archive did not free retained bytes";
        return false;
    }

    const AssetLoadId missing = loader.request("scenes/absent.phscene");
    if (missing == 0u ||
        !waitForState(loader, missing, AssetLoadState::Failed, outFail)) {
        return false;
    }
    AssetLoadResult missingResult;
    if (!loader.take(missing, missingResult) ||
        missingResult.state != AssetLoadState::Failed ||
        !missingResult.bytes.empty() ||
        missingResult.error.empty() ||
        loader.residentBytes() != 0u) {
        outFail = "missing fixture content did not fail with a diagnostic";
        return false;
    }

    // Corrupt archive bytes still transfer, so rejection stays with the archive
    // decoder on the consuming thread.
    std::vector<std::uint8_t> corruptArchive = sceneArchive;
    corruptArchive.back() ^= 0xffu;
    const std::shared_ptr<InMemoryAssetStore> corruptStore =
        std::make_shared<InMemoryAssetStore>();
    corruptStore->add("scenes/corrupt.phscene", corruptArchive);
    AsyncAssetLoader corruptLoader(corruptStore);
    const AssetLoadId corrupt = corruptLoader.request("scenes/corrupt.phscene");
    if (corrupt == 0u ||
        !waitForState(
            corruptLoader,
            corrupt,
            AssetLoadState::Ready,
            outFail)) {
        return false;
    }
    AssetLoadResult corruptResult;
    if (!corruptLoader.take(corrupt, corruptResult) ||
        corruptResult.bytes != corruptArchive ||
        corruptResult.state != AssetLoadState::Ready) {
        outFail = "corrupt archive bytes were not transferred verbatim";
        return false;
    }
    SceneArchiveStore corruptScene;
    error.clear();
    if (corruptScene.loadBytes(corruptResult.bytes, &error) || error.empty()) {
        outFail = "a corrupt synthetic scene archive was accepted";
        return false;
    }
    return true;
}
