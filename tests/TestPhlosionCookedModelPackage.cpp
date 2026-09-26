#include "engine/assets/phlosion/PhlosionCookedModelPackage.h"
#include "engine/assets/phlosion/PhlosionBinaryCodec.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using engine::assets::phrc::Chunk;
using engine::assets::phrc::Dependency;
using engine::assets::phrc::Document;

bool writeDocument(
    const fs::path& path,
    Document& document,
    std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!engine::assets::phrc::encode(document, bytes, &error) ||
        !engine::assets::phrc::decode(bytes, document, &error)) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

struct TempObject {
    fs::path directory;
    ~TempObject() {
        for (const char* name : {
                 "sample.phlo", "model.phmesh", "model.phskel",
                 "model.phanim", "model.phmat"}) {
            std::error_code ignored;
            fs::remove(directory / name, ignored);
        }
        std::error_code ignored;
        fs::remove(directory, ignored);
    }
};

} // namespace

bool test_phlosion_cooked_model_package_contract(std::string& outFail) {
    using namespace engine::assets;
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TempObject temporary{
        fs::temp_directory_path() /
        ("phlosion_cooked_model_package_test_" + std::to_string(nonce))};
    std::error_code ec;
    fs::create_directory(temporary.directory, ec);
    if (ec) {
        outFail = "Could not create cooked-model test directory: " + ec.message();
        return false;
    }
    std::string error;
    struct Child {
        const char* filename;
        const char* magic;
    };
    const Child children[] = {
        {"model.phmesh", "PHME"}, {"model.phskel", "PHSK"},
        {"model.phanim", "PHAN"}, {"model.phmat", "PHMT"}};
    Document root;
    root.magic = phrc::magic("PHLO");
    root.schemaVersion = 3u;
    root.manifestJson = R"json({"schema_version":3,"root_type":"Prefab","object_id":"test/model","prefab_kind":"Character","counts":{"vertices":3,"indices":3,"submeshes":1,"nodes":1,"skins":0,"animations":2},"resources":{"mesh":"model.phmesh","skeleton":"model.phskel","animations":"model.phanim","materials":"model.phmat"}})json";
    for (const Child& child : children) {
        Document typed;
        typed.magic = phrc::magic(child.magic);
        typed.schemaVersion = 3u;
        typed.manifestJson = "{}";
        std::vector<std::uint8_t> payload{1u, 2u, 3u};
        if (std::string(child.magic) == "PHAN") {
            phrc::BinaryWriter animation;
            animation.u32(2u);
            for (const char* clipName : {"Idle", "Run"}) {
                animation.string(clipName);
                animation.f32(1.0f);
                animation.u32(0u); // samplers
                animation.u32(0u); // channels
            }
            payload = animation.take();
        }
        typed.chunks.push_back(Chunk{
            phrc::magic("DATA"), 0u, 16u, std::move(payload)});
        if (!writeDocument(temporary.directory / child.filename, typed, error)) {
            outFail = "Could not create typed cooked-model fixture: " + error;
            return false;
        }
        root.dependencies.push_back(Dependency{
            child.filename, typed.contentHash, phrc::kDependencyRequired});
    }
    const fs::path rootPath = temporary.directory / "sample.phlo";
    if (!writeDocument(rootPath, root, error)) {
        outFail = "Could not create cooked-model root fixture: " + error;
        return false;
    }
    phlosion::CookedModelPackage loaded;
    if (!phlosion::loadCookedModelPackage(rootPath, loaded, &error) ||
        loaded.objectId != "test/model" ||
        loaded.vertexCount != 3u ||
        loaded.animationCount != 2u ||
        loaded.clips.size() != 2u ||
        loaded.clips[1].name != "Run" ||
        loaded.materials.magic != phrc::magic("PHMT")) {
        outFail = "Engine package loader lost the typed model graph: " + error;
        return false;
    }
    root.dependencies[0].expectedContentHash ^= 1u;
    if (!writeDocument(rootPath, root, error)) {
        outFail = "Could not corrupt model dependency fixture: " + error;
        return false;
    }
    if (phlosion::loadCookedModelPackage(rootPath, loaded, &error) ||
        error.find("hash mismatch") == std::string::npos ||
        loaded.objectId != "test/model") {
        outFail = "Engine package loader accepted a stale child or changed output on failure.";
        return false;
    }
    root.manifestJson.replace(
        root.manifestJson.find("model.phmesh"),
        std::string("model.phmesh").size(), "../escape.phmesh");
    if (!writeDocument(rootPath, root, error)) {
        outFail = "Could not create unsafe child-path fixture: " + error;
        return false;
    }
    if (phlosion::loadCookedModelPackage(rootPath, loaded, &error) ||
        error.find("unsafe asset ID") == std::string::npos ||
        loaded.objectId != "test/model") {
        outFail = "Engine package loader accepted an escaping child path.";
        return false;
    }

    return true;
}
