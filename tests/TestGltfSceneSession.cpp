#include "gltf_import/GltfSceneTestSupport.h"
#include "engine/render/gltf/GltfSceneSession.h"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace sc = engine::render::gltf::scene;
namespace ph = engine::assets::phlosion;

namespace engine::render::gltf::scene::detail {
struct GltfSceneSessionTestAccess {
    static std::unique_ptr<GltfSceneSession> create(
        std::shared_ptr<const engine::IAssetStore> store, ph::AssetResolver resolver,
        GltfSceneSessionOptions options, GltfSceneSession::Decoder decoder) {
        return std::unique_ptr<GltfSceneSession>(new GltfSceneSession(
            std::move(store), std::move(resolver), options, std::move(decoder)));
    }
};
}

namespace {
using Bytes = std::vector<std::uint8_t>;
using namespace std::chrono_literals;

void require(bool value, const std::string& diagnostic) {
    if (!value) throw std::runtime_error(diagnostic);
}

struct Store : engine::IAssetStore {
    std::map<std::string, Bytes> files;
    mutable std::mutex mutex;
    mutable std::vector<std::thread::id> readers;
    bool readBytes(const std::string& path, Bytes& out, std::string* error) const override {
        std::lock_guard lock(mutex);
        readers.push_back(std::this_thread::get_id());
        const auto found = files.find(path);
        if (found == files.end()) { if (error) *error = "missing " + path; return false; }
        out = found->second;
        return true;
    }
    bool readText(const std::string& path, std::string& out, std::string* error) const override {
        Bytes bytes;
        if (!readBytes(path, bytes, error)) return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }
    bool exists(const std::string& path) const override { return files.contains(path); }
};

Bytes textBytes(const std::string& text) { return {text.begin(), text.end()}; }

std::vector<ph::SceneArchiveFile> sceneFiles(const gltf_scene_contract::SceneSource& source) {
    return {{"models/scene.gltf", textBytes(source.document.dump())},
            {"models/scene.bin", source.binary},
            {"models/base_color.png", gltf_scene_contract::authoredBaseColorPngBytes()}};
}

std::shared_ptr<Store> archiveStore() {
    auto store = std::make_shared<Store>();
    auto source = gltf_scene_contract::makeValidSceneSource();
    std::string error;
    require(ph::encodeSceneArchive("a", sceneFiles(source), store->files["a"], &error), error);
    source.document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] =
        nlohmann::json::array({0.1f, 0.8f, 0.3f, 1.0f});
    require(ph::encodePrefabArchive("b", "scene", "{}", sceneFiles(source), store->files["b"], &error), error);
    require(ph::encodePrefabArchive("dependency", "scene", "{}", sceneFiles(source),
        {{"missing-required", 0u, engine::assets::phrc::kDependencyRequired}},
        store->files["dependency"], &error), error);
    source.document["meshes"][0]["primitives"][0]["material"] = 999999u;
    require(ph::encodeSceneArchive("bad", sceneFiles(source), store->files["bad"], &error), error);
    store->files["corrupt"] = {1, 2, 3};
    return store;
}

ph::AssetResolver resolver() {
    return [](const std::string& id, ph::AssetLocation& location, std::string*) {
        location = {id, ph::AssetKind::Phrc}; return true;
    };
}

void pumpUntil(sc::GltfSceneSession& session, sc::GltfSceneSessionState wanted) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (session.state() != wanted && std::chrono::steady_clock::now() < deadline) {
        session.pump();
        if (session.state() == sc::GltfSceneSessionState::Failed && wanted != session.state())
            throw std::runtime_error(session.error());
        std::this_thread::yield();
    }
    require(session.state() == wanted, "scene session timed out: " + session.error());
}

struct Gate {
    std::mutex mutex;
    std::condition_variable wake;
    bool entered = false, released = false;
    std::thread::id thread;
    void open() { std::lock_guard lock(mutex); released = true; wake.notify_all(); }
    void waitEntered() {
        std::unique_lock lock(mutex);
        require(wake.wait_for(lock, 5s, [&] { return entered; }), "decode gate did not enter");
    }
};
struct OpenOnExit { std::shared_ptr<Gate> gate; ~OpenOnExit() { gate->open(); } };

auto gatedDecoder(const std::shared_ptr<Gate>& gate) {
    return [gate](const engine::IAssetStore& store, const std::string& path,
                  std::shared_ptr<const sc::GltfSceneAsset>& out, std::string* error,
                  const sc::GltfSceneLoadOptions& options) {
        {
            std::unique_lock lock(gate->mutex);
            if (!gate->entered) {
                gate->thread = std::this_thread::get_id();
                gate->entered = true;
                gate->wake.notify_all();
                if (!gate->wake.wait_for(lock, 10s, [&] { return gate->released; })) {
                    if (error) *error = "decode gate timed out";
                    return false;
                }
            }
        }
        return sc::loadGltfSceneAsset(store, path, out, error, options);
    };
}
}

bool test_gltf_scene_session_contract(std::string& outFail) {
    try {
        gltf_scene_contract::ScopedDecoderDiagnostics diagnostics;
        auto source = gltf_scene_contract::makeValidSceneSource();
        Store loose;
        for (auto& file : sceneFiles(source)) loose.files.emplace(file.virtualPath, std::move(file.bytes));
        std::shared_ptr<const sc::GltfSceneAsset> imported;
        std::string error;
        require(sc::loadGltfSceneAsset(loose, "models/scene.gltf", imported, &error), error);
        require(imported->primitives.size() == 2u && imported->animations.size() == 1u,
                "store import lost authored geometry/animation");
        require(imported->materials[0].baseColor.rgba == gltf_scene_contract::authoredBaseColorDecodedPixels(),
                "store image decode differs from authored PNG");
        gltf_import_contract::FixtureDirectory directory("scene-session-contract");
        require(directory.ready(), "temporary directory failed");
        for (const auto& [path, bytes] : loose.files) {
            const auto file = directory.file(std::filesystem::path(path).filename().string());
            std::ofstream stream(file, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            require(stream.good(), "could not write comparison fixture");
        }
        std::shared_ptr<const sc::GltfSceneAsset> disk;
        require(sc::loadGltfSceneAsset(directory.file("scene.gltf").string(), disk, &error), error);
        require(disk->sourceIdentity == imported->sourceIdentity &&
                disk->stats.retainedAssetBytes == imported->stats.retainedAssetBytes &&
                disk->primitives[0].geometryKey == imported->primitives[0].geometryKey,
                "store/filesystem imports differ");
        const auto preserved = imported;
        loose.files.erase("models/scene.bin");
        require(!sc::loadGltfSceneAsset(loose, "models/scene.gltf", imported, &error) && imported == preserved,
                "missing external buffer changed output");
        loose.files["models/scene.bin"] = source.binary;
        for (const auto* uri : {"../../escape.bin", "/escape.bin", "C:/escape.bin",
                                "//host/file", "https://host/file", "scene.bin?x", "scene.bin#x",
                                "%ZZ.bin", "%2e%2e/%2e%2e/escape.bin", "%2fescape.bin",
                                "scene%00.bin", "scene.bin%3fx", "scene.bin%23x"}) {
            auto invalid = source.document;
            invalid["buffers"][0]["uri"] = uri;
            loose.files["models/scene.gltf"] = textBytes(invalid.dump());
            require(!sc::loadGltfSceneAsset(loose, "models/scene.gltf", imported, &error) && imported == preserved,
                    std::string("accepted escaping/unsupported URI: ") + uri);
        }
        // fastgltf decodes URI escapes before the store validates/normalizes paths.
        for (const auto* uri : {"../models/scene.bin", "scene%2ebin"}) {
            auto relative = source.document;
            relative["buffers"][0]["uri"] = uri;
            loose.files["models/scene.gltf"] = textBytes(relative.dump());
            require(sc::loadGltfSceneAsset(loose, "models/scene.gltf", imported, &error), error);
        }

        auto embedded = source.document;
        embedded["buffers"][0]["uri"] = "data:application/octet-stream;base64," +
            gltf_texture_contract::encodeBase64(source.binary);
        embedded["images"][0]["uri"] = "data:image/png;base64," +
            gltf_texture_contract::encodeBase64(gltf_scene_contract::authoredBaseColorPngBytes());
        Store selfContained;
        selfContained.files["embedded.gltf"] = textBytes(embedded.dump());
        require(sc::loadGltfSceneAsset(selfContained, "embedded.gltf", imported, &error), error);
        require(imported->stats.retainedAssetBytes == preserved->stats.retainedAssetBytes,
                "embedded data URI import changed payload");
        auto glbDocument = source.document;
        glbDocument["buffers"][0].erase("uri");
        auto jsonBytes = textBytes(glbDocument.dump());
        while (jsonBytes.size() % 4u) jsonBytes.push_back(' ');
        Bytes binary = source.binary;
        while (binary.size() % 4u) binary.push_back(0);
        Bytes glb;
        const auto appendU32 = [&](std::uint32_t value) {
            for (unsigned shift = 0u; shift < 32u; shift += 8u)
                glb.push_back(static_cast<std::uint8_t>(value >> shift));
        };
        appendU32(0x46546c67u); appendU32(2u);
        appendU32(static_cast<std::uint32_t>(28u + jsonBytes.size() + binary.size()));
        appendU32(static_cast<std::uint32_t>(jsonBytes.size())); appendU32(0x4e4f534au);
        glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
        appendU32(static_cast<std::uint32_t>(binary.size())); appendU32(0x004e4942u);
        glb.insert(glb.end(), binary.begin(), binary.end());
        loose.files["models/scene.glb"] = std::move(glb);
        require(sc::loadGltfSceneAsset(loose, "models/scene.glb", imported, &error), error);
        require(imported->stats.retainedAssetBytes == preserved->stats.retainedAssetBytes,
                "GLB store import changed payload");

        auto host = archiveStore();
        sc::GltfSceneSession session(host, resolver());
        require(session.begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
        require(host->readers.empty(), "begin did provider I/O");
        pumpUntil(session, sc::GltfSceneSessionState::Staged);
        require(!session.active() && session.staged()->encodedGraphBytes == host->files.at("a").size(),
                "staged candidate was published or byte accounting differs");
        require(session.activate(), "could not activate A");
        auto retained = session.active();
        const auto aBytes = retained->scene->stats.retainedAssetBytes;
        for (const auto* bad : {"bad", "corrupt", "missing"}) {
            require(session.begin(bad, ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
            pumpUntil(session, sc::GltfSceneSessionState::Failed);
            require(session.active() == retained && !session.staged() && !session.activate(),
                    "failed candidate changed active");
            session.discard();
        }
        require(session.begin("dependency", ph::ArchiveKind::Prefab, "models/scene.gltf", &error), error);
        pumpUntil(session, sc::GltfSceneSessionState::Failed);
        require(session.active() == retained, "missing required dependency changed active");
        session.discard();
        require(session.begin("b", ph::ArchiveKind::Prefab, "models/scene.gltf", &error), error);
        require(session.active() == retained, "begin replaced active");
        pumpUntil(session, sc::GltfSceneSessionState::Staged);
        require(session.decodedResidentBytes() == aBytes + session.staged()->scene->stats.retainedAssetBytes,
                "active plus staged accounting differs");
        require(session.activate() && session.active() != retained, "replacement failed");
        session.unload();
        require(!session.active() && session.decodedResidentBytes() == 0u, "unload retained session payload");
        sc::GltfScenePose pose;
        require(sc::sampleGltfScenePose(retained->scene, 1.f, 0, pose, &error), error);
        {
            std::lock_guard lock(host->mutex);
            for (auto thread : host->readers)
                require(thread != std::this_thread::get_id(), "archive read ran on requesting thread");
        }

        sc::GltfSceneSessionOptions budget;
        budget.maxDecodedResidentBytes = aBytes;
        sc::GltfSceneSession limited(host, resolver(), budget);
        require(limited.begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
        pumpUntil(limited, sc::GltfSceneSessionState::Staged);
        require(limited.activate(), "budget rejected exact fit");
        auto budgetActive = limited.active();
        require(limited.begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
        pumpUntil(limited, sc::GltfSceneSessionState::Failed);
        require(limited.active() == budgetActive && limited.decodedResidentBytes() == aBytes,
                "combined budget failed to preserve active");
        budget.maxDecodedResidentBytes = aBytes - 1u;
        sc::GltfSceneSession tooSmall(host, resolver(), budget);
        require(tooSmall.begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
        pumpUntil(tooSmall, sc::GltfSceneSessionState::Failed);
        require(tooSmall.decodedResidentBytes() == 0u, "rejected output remained resident");

        auto gate = std::make_shared<Gate>();
        auto controlled = sc::detail::GltfSceneSessionTestAccess::create(host, resolver(), {}, gatedDecoder(gate));
        OpenOnExit cleanup{gate};
        require(controlled->begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
        pumpUntil(*controlled, sc::GltfSceneSessionState::Decoding);
        gate->waitEntered();
        require(gate->thread != std::this_thread::get_id(), "glTF decode ran on owner thread");
        require(controlled->cancel() && controlled->state() == sc::GltfSceneSessionState::Cancelled,
                "cancellation did not return while decode was gated");
        controlled->discard();
        require(controlled->begin("b", ph::ArchiveKind::Prefab, "models/scene.gltf", &error), error);
        pumpUntil(*controlled, sc::GltfSceneSessionState::Decoding);
        gate->open();
        pumpUntil(*controlled, sc::GltfSceneSessionState::Staged);
        require(controlled->staged()->archive->kind == ph::ArchiveKind::Prefab && controlled->activate(),
                "stale decode published into replacement");
        auto afterDestruction = controlled->active();
        controlled.reset();
        require(sc::sampleGltfScenePose(afterDestruction->scene, 1.f, 0, pose, &error), error);

        auto throwing = sc::detail::GltfSceneSessionTestAccess::create(host, resolver(), {},
            [](const engine::IAssetStore&, const std::string&, std::shared_ptr<const sc::GltfSceneAsset>&,
               std::string*, const sc::GltfSceneLoadOptions&) -> bool { throw std::runtime_error("authored exception"); });
        require(throwing->begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
        pumpUntil(*throwing, sc::GltfSceneSessionState::Failed);
        require(throwing->error().find("authored exception") != std::string::npos, "lost worker exception");

        sc::GltfSceneSession queued(host, resolver());
        require(queued.begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error) && queued.cancel(),
                "queued cancellation failed");
        queued.pump();
        require(queued.state() == sc::GltfSceneSessionState::Cancelled && !queued.staged(),
                "cancelled queued candidate published");

        auto stopGate = std::make_shared<Gate>();
        auto ownedProvider = archiveStore();
        std::weak_ptr<const engine::IAssetStore> providerLifetime = ownedProvider;
        auto stopping = sc::detail::GltfSceneSessionTestAccess::create(
            ownedProvider, resolver(), {}, gatedDecoder(stopGate));
        ownedProvider.reset();
        require(stopping->begin("a", ph::ArchiveKind::Scene, "models/scene.gltf", &error), error);
        pumpUntil(*stopping, sc::GltfSceneSessionState::Decoding);
        stopGate->waitEntered();
        std::promise<void> destroyEntered;
        auto entered = destroyEntered.get_future();
        auto destroyed = std::async(std::launch::async, [owned = std::move(stopping), &destroyEntered]() mutable {
            destroyEntered.set_value();
            owned.reset();
        });
        OpenOnExit stopCleanup{stopGate};
        require(entered.wait_for(5s) == std::future_status::ready, "destructor thread did not start");
        require(destroyed.wait_for(20ms) == std::future_status::timeout && !providerLifetime.expired(),
                "session destructor did not retain/join its active worker");
        stopGate->open();
        require(destroyed.wait_for(5s) == std::future_status::ready, "session worker did not stop");
        destroyed.get();
        require(providerLifetime.expired(), "stopped session retained provider");
        return true;
    } catch (const std::exception& error) {
        outFail = error.what();
        return false;
    }
}
