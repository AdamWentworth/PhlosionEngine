#include "FixtureData.h"
#include "FixtureImageGuard.h"
#include "FixtureSession.h"

#include "engine/assets/phlosion/PhlosionArchiveSession.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using engine::assets::phlosion::ArchiveKind;
using engine::assets::phlosion::ArchiveSessionState;
using engine::assets::phlosion::PhlosionArchiveSession;
using engine::fixture::FixtureBundle;
using engine::fixture::FixtureCase;
using engine::fixture::FixtureContent;
using engine::fixture::FixtureContentVariant;
using engine::fixture::FixtureCrop;
using engine::fixture::FixtureImage;
using engine::fixture::FixtureRoleRect;
using engine::fixture::FixtureSession;

constexpr std::array<std::uint8_t, 3> kBackground{140u, 238u, 163u};
constexpr std::array<std::uint8_t, 3> kHidden{64u, 148u, 74u};
constexpr std::array<std::uint8_t, 3> kOccluder{40u, 70u, 220u};
constexpr std::array<std::uint8_t, 3> kBlend{222u, 199u, 122u};
constexpr std::array<std::uint8_t, 3> kMarker{245u, 245u, 245u};
constexpr std::array<std::uint8_t, 3> kMagenta{230u, 80u, 220u};
constexpr std::array<std::uint8_t, 3> kCyan{80u, 220u, 230u};
constexpr std::array<std::uint8_t, 3> kOrange{235u, 150u, 60u};
constexpr std::array<std::uint8_t, 3> kYellow{235u, 225u, 110u};

struct SyntheticFrame {
    FixtureImage image;
    std::map<std::string, FixtureRoleRect> rects;
};

SyntheticFrame makeSyntheticFrame(bool contentB, int logicalFrame) {
    SyntheticFrame frame;
    std::string error;
    if (!engine::fixture::buildSyntheticFixtureCapture(
            contentB ? "b" : "a",
            logicalFrame,
            320,
            180,
            frame.image,
            frame.rects,
            &error)) {
        throw std::runtime_error(error);
    }
    return frame;
}

void fillRect(
    FixtureImage& image,
    const FixtureRoleRect& rect,
    const std::array<std::uint8_t, 3>& color) {
    const int x0 = static_cast<int>(rect[0] * static_cast<float>(image.width));
    const int y0 = static_cast<int>(rect[1] * static_cast<float>(image.height));
    const int x1 = static_cast<int>(
        (rect[0] + rect[2]) * static_cast<float>(image.width));
    const int y1 = static_cast<int>(
        (rect[1] + rect[3]) * static_cast<float>(image.height));
    for (int y = std::max(0, y0); y < std::min(image.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(image.width, x1); ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) * 4u;
            image.rgba[offset + 0u] = color[0];
            image.rgba[offset + 1u] = color[1];
            image.rgba[offset + 2u] = color[2];
            image.rgba[offset + 3u] = 255u;
        }
    }
}

void fillBoardQuadrants(
    FixtureImage& image,
    const FixtureRoleRect& rect,
    const std::array<std::uint8_t, 3>& topLeft,
    const std::array<std::uint8_t, 3>& topRight,
    const std::array<std::uint8_t, 3>& bottomLeft,
    const std::array<std::uint8_t, 3>& bottomRight) {
    const FixtureRoleRect left{
        rect[0], rect[1], rect[2] * 0.5f, rect[3] * 0.5f};
    const FixtureRoleRect right{
        rect[0] + rect[2] * 0.5f,
        rect[1],
        rect[2] * 0.5f,
        rect[3] * 0.5f};
    const FixtureRoleRect leftBottom{
        rect[0],
        rect[1] + rect[3] * 0.5f,
        rect[2] * 0.5f,
        rect[3] * 0.5f};
    const FixtureRoleRect rightBottom{
        rect[0] + rect[2] * 0.5f,
        rect[1] + rect[3] * 0.5f,
        rect[2] * 0.5f,
        rect[3] * 0.5f};
    fillRect(image, left, topLeft);
    fillRect(image, right, topRight);
    fillRect(image, leftBottom, bottomLeft);
    fillRect(image, rightBottom, bottomRight);
}

bool expect(bool condition, const char* message, std::string& outFail) {
    if (!condition) {
        outFail = message;
        return false;
    }
    return true;
}

bool validateSynthetic(
    const SyntheticFrame& frame,
    bool contentB,
    int logicalFrame,
    engine::fixture::ExpectedContentResult& outResult,
    std::string& outFail) {
    std::string error;
    if (!engine::fixture::validateFixtureExpectedContent(
            frame.image,
            contentB ? "b" : "a",
            logicalFrame,
            frame.rects,
            outResult,
            &error)) {
        outFail = "synthetic expected-content validation failed to run: " + error;
        return false;
    }
    if (!outResult.ok && !outResult.failures.empty()) {
        outFail = outResult.failures.front();
    }
    return true;
}

bool expectRejected(
    const SyntheticFrame& frame,
    bool contentB,
    int logicalFrame,
    const char* message,
    std::string& outFail) {
    engine::fixture::ExpectedContentResult result;
    if (!validateSynthetic(frame, contentB, logicalFrame, result, outFail)) {
        return false;
    }
    if (result.ok) {
        outFail = message;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Archive tampering helpers
// ---------------------------------------------------------------------------

std::uint64_t hashChunk(const std::vector<std::uint8_t>& bytes) {
    return engine::fixture::fnv1a64(bytes);
}

bool repairFixtureArchive(
    nlohmann::json& scene,
    engine::assets::phrc::Document& document,
    nlohmann::json& manifest,
    std::string& outError) {
    auto findChunkIndex = [&manifest, &outError](
                              const std::string& path,
                              std::size_t& outIndex) {
        for (auto& record : manifest.at("files")) {
            if (record.value("path", std::string{}) == path) {
                outIndex = record.at("chunk").get<std::size_t>();
                return true;
            }
        }
        outError = "fixture manifest has no record for " + path;
        return false;
    };
    const auto readU32 = [](const std::vector<std::uint8_t>& bytes,
                            std::size_t offset) {
        std::uint32_t value = 0u;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };

    std::uint64_t contentHash = engine::fixture::kFnvOffset;
    for (auto& object : scene.at("objects")) {
        const std::string geometryPath = object.at("geometry").get<std::string>();
        const std::string texturePath = object.at("texture").get<std::string>();
        std::size_t geometryIndex = 0u;
        std::size_t textureIndex = 0u;
        if (!findChunkIndex(geometryPath, geometryIndex) ||
            !findChunkIndex(texturePath, textureIndex)) {
            return false;
        }
        const auto& geometryBytes = document.chunks[geometryIndex].bytes;
        if (geometryBytes.size() < 16u) {
            outError = "fixture geometry chunk is too small";
            return false;
        }
        object["geometry_vertices"] = readU32(geometryBytes, 8u);
        object["geometry_indices"] = readU32(geometryBytes, 12u);
        object["geometry_hash"] = hashChunk(geometryBytes);
        object["texture_hash"] = hashChunk(document.chunks[textureIndex].bytes);
        contentHash = engine::fixture::fnv1a64(geometryBytes, contentHash);
        contentHash = engine::fixture::fnv1a64(
            document.chunks[textureIndex].bytes, contentHash);
    }
    scene["content_hash"] = engine::fixture::hashHex(contentHash);

    std::size_t sceneIndex = 0u;
    if (!findChunkIndex("fixture/scene.json", sceneIndex)) {
        return false;
    }
    const std::string sceneText = scene.dump(2);
    document.chunks[sceneIndex].bytes.assign(
        sceneText.begin(), sceneText.end());
    for (auto& record : manifest.at("files")) {
        const std::size_t index = record.at("chunk").get<std::size_t>();
        if (index >= document.chunks.size()) {
            outError = "fixture manifest points outside the chunk table";
            return false;
        }
        record["bytes"] = document.chunks[index].bytes.size();
        record["content_hash"] =
            engine::assets::phrc::contentHash64(document.chunks[index].bytes);
    }
    document.manifestJson = manifest.dump();
    return true;
}

bool tamperFixtureArchive(
    const FixtureBundle& bundle,
    const std::function<void(nlohmann::json&, engine::assets::phrc::Document&)>& mutate,
    std::vector<std::uint8_t>& outBytes,
    std::string& outError) {
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(bundle.archiveBytes, document, &outError)) {
        return false;
    }
    nlohmann::json manifest =
        nlohmann::json::parse(document.manifestJson);
    std::size_t sceneIndex = 0u;
    for (auto& record : manifest.at("files")) {
        if (record.value("path", std::string{}) == "fixture/scene.json") {
            sceneIndex = record.at("chunk").get<std::size_t>();
            break;
        }
    }
    nlohmann::json scene = nlohmann::json::parse(
        document.chunks[sceneIndex].bytes.begin(),
        document.chunks[sceneIndex].bytes.end());
    mutate(scene, document);
    if (!repairFixtureArchive(scene, document, manifest, outError)) {
        return false;
    }
    return engine::assets::phrc::encode(document, outBytes, &outError);
}

bool pumpSessionUntilTerminal(
    PhlosionArchiveSession& session,
    std::string& outError) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        session.pump();
        const ArchiveSessionState state = session.state();
        if (state == ArchiveSessionState::Staged ||
            state == ArchiveSessionState::Failed ||
            state == ArchiveSessionState::Cancelled) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    outError = "timed out waiting for a fixture candidate";
    return false;
}

std::shared_ptr<engine::fixture::FixtureAssetStore> makeFixtureStore(
    const FixtureBundle& bundle,
    const std::vector<std::uint8_t>& archiveBytes) {
    auto store = std::make_shared<engine::fixture::FixtureAssetStore>();
    engine::fixture::addFixtureDependency(*store);
    store->add(bundle.virtualPath, archiveBytes);
    return store;
}

bool expectTamperedArchiveRejected(
    const FixtureBundle& bundle,
    const std::function<void(nlohmann::json&, engine::assets::phrc::Document&)>& mutate,
    const std::string& expectedText,
    std::string& outFail) {
    std::vector<std::uint8_t> tampered;
    std::string error;
    if (!tamperFixtureArchive(bundle, mutate, tampered, error)) {
        outFail = "fixture archive tamper failed: " + error;
        return false;
    }
    const std::shared_ptr<engine::fixture::FixtureAssetStore> store =
        makeFixtureStore(bundle, tampered);
    PhlosionArchiveSession session(store, engine::fixture::fixtureAssetResolver);
    if (!session.begin(bundle.rootAssetId, ArchiveKind::Scene, &error)) {
        outFail = "tampered fixture root was not admitted: " + error;
        return false;
    }
    if (!pumpSessionUntilTerminal(session, error)) {
        outFail = error;
        return false;
    }
    if (session.state() != ArchiveSessionState::Staged) {
        outFail = "tampered fixture archive did not reach Staged: " + session.error();
        return false;
    }
    FixtureContent rejected;
    if (engine::fixture::validateFixtureSnapshot(
            *session.staged(), rejected, &error)) {
        outFail = "fixture validation accepted a malformed archive";
        return false;
    }
    if (error.find(expectedText) == std::string::npos) {
        outFail =
            "fixture validation diagnostic did not contain '" + expectedText +
            "': " + error;
        return false;
    }
    if (session.active() != nullptr) {
        outFail = "a rejected candidate published an active snapshot";
        return false;
    }
    session.discard();
    return true;
}

bool caseFixtureLifecycle(std::string& outFail) {
    {
        FixtureSession session(FixtureCase::AFrame0);
        if (!session.prepare(&outFail)) {
            return false;
        }
        if (session.fixtureCase() != FixtureCase::AFrame0 ||
            session.activeContent().contentId != "a" ||
            session.activationCount() != 1 ||
            session.transition().attempted ||
            engine::fixture::fixtureCaseLogicalFrame(FixtureCase::AFrame0) != 0) {
            outFail = "initial A frame 0 lifecycle is wrong";
            return false;
        }
        if (!session.attemptReplacement(&outFail) ||
            session.transition().attempted) {
            outFail = "initial-only case should not attempt a replacement";
            return false;
        }
    }
    {
        FixtureSession session(FixtureCase::AFrame30);
        if (!session.prepare(&outFail) ||
            session.activeContent().contentId != "a" ||
            session.activeContent().rootAssetId != engine::fixture::kFixtureRootA) {
            outFail = "initial A frame 30 preparation failed";
            return false;
        }
    }
    {
        FixtureSession session(FixtureCase::FailedReplacement);
        if (!session.prepare(&outFail)) {
            return false;
        }
        const std::string beforeHash = session.activeContent().contentHash;
        if (!session.attemptReplacement(&outFail)) {
            outFail = "failed replacement was not handled as expected: " + outFail;
            return false;
        }
        if (!session.transition().attempted ||
            !session.transition().failed ||
            session.transition().succeeded ||
            session.transition().outcome != "graph_failed" ||
            session.transition().error.empty()) {
            outFail = "failed replacement transition fields are wrong";
            return false;
        }
        if (session.activeContent().contentId != "a" ||
            session.activeContent().contentHash != beforeHash ||
            session.activationCount() != 1) {
            outFail = "failed replacement changed the active content";
            return false;
        }
        if (session.transition().error.find("fixture/leaf/missing") ==
            std::string::npos) {
            outFail =
                "failed replacement diagnostic does not name the missing dependency: " +
                session.transition().error;
            return false;
        }
    }
    {
        FixtureSession session(FixtureCase::ValidReplacementB);
        if (!session.prepare(&outFail)) {
            return false;
        }
        const std::string hashA = session.activeContent().contentHash;
        const engine::fixture::FixtureObject* boardA =
            session.activeContent().findRole("board");
        const engine::fixture::FixtureObject* markerA =
            session.activeContent().findRole("marker");
        if (boardA == nullptr || markerA == nullptr) {
            outFail = "fixture A has no board object";
            return false;
        }
        const std::string geometryKeyA = boardA->geometryCacheKey;
        const std::string textureKeyA = boardA->textureKey;
        const std::string textureCacheKeyA = boardA->textureCacheKey;
        const std::vector<std::uint8_t> textureA = boardA->textureRgba;
        const float markerAFromX = markerA->motion.from[0];
        if (!session.attemptReplacement(&outFail)) {
            outFail = "valid replacement failed: " + outFail;
            return false;
        }
        const FixtureContent& contentB = session.activeContent();
        if (contentB.contentId != "b" ||
            contentB.rootAssetId != engine::fixture::kFixtureRootB ||
            contentB.contentHash == hashA ||
            session.activationCount() != 2 ||
            !session.transition().succeeded ||
            session.transition().outcome != "activated") {
            outFail = "valid replacement did not activate content B";
            return false;
        }
        const engine::fixture::FixtureObject* boardB =
            contentB.findRole("board");
        if (boardB == nullptr ||
            boardB->geometryCacheKey == geometryKeyA ||
            boardB->textureKey != textureKeyA ||
            boardB->textureCacheKey == textureCacheKeyA ||
            boardB->textureRgba == textureA) {
            outFail =
                "content B does not keep the logical texture identity while distinguishing cache keys";
            return false;
        }
        if (contentB.findRole("marker") == nullptr ||
            contentB.findRole("marker")->motion.from[0] <= 0.0f ||
            markerAFromX >= 0.0f) {
            outFail = "marker motion does not differ between content A and B";
            return false;
        }
    }
    {
        // Malformed candidate content must fail fixture validation. The
        // archive plumbing is repaired so the typed decoder still accepts the
        // snapshot and the consumer-side gate is what rejects it.
        const FixtureBundle bundle = engine::fixture::buildFixtureBundle(
            FixtureContentVariant::A);
        if (!expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["camera"]["fov_degrees"] = 200.0;
                },
                "field of view",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["camera"]["position"][0] = 1.0e39;
                },
                "camera values must be finite",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["objects"][5]["motion"]["from"][0] = 1.0e39;
                },
                "motion values must be finite",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["objects"][1]["id"] = "background";
                },
                "object id is duplicated",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["objects"][1]["role"] = "background";
                },
                "object role is duplicated",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["objects"][0]["texture_width"] = 8;
                },
                "texture dimensions do not match",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["objects"][0]["material_mode"] = 9;
                },
                "material values are unsupported",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["objects"][0]["id"] = "";
                },
                "non-empty id",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["camera"]["near"] = 0.0;
                },
                "clip planes",
                outFail)) {
            return false;
        }
        const std::function<void(nlohmann::json&, engine::assets::phrc::Document&)>
            nonFiniteVertex = [](nlohmann::json&, engine::assets::phrc::Document& document) {
                const float value = std::numeric_limits<float>::quiet_NaN();
                const std::size_t vertexOffset = 16u;
                std::memcpy(
                    document.chunks[1].bytes.data() + vertexOffset,
                    &value,
                    sizeof(value));
            };
        if (!expectTamperedArchiveRejected(
                bundle,
                nonFiniteVertex,
                "non-finite vertex value",
                outFail)) {
            return false;
        }
    }
    {
        // A validation failure during replacement must leave the previously
        // active snapshot untouched.
        const FixtureBundle bundleA = engine::fixture::buildFixtureBundle(
            FixtureContentVariant::A);
        const FixtureBundle bundleB = engine::fixture::buildFixtureBundle(
            FixtureContentVariant::B);
        std::vector<std::uint8_t> tamperedB;
        std::string error;
        if (!tamperFixtureArchive(
                bundleB,
                [](nlohmann::json& scene, engine::assets::phrc::Document&) {
                    scene["objects"][0]["texture_width"] = 8;
                },
                tamperedB,
                error)) {
            outFail = "candidate tamper failed: " + error;
            return false;
        }
        const std::shared_ptr<engine::fixture::FixtureAssetStore> store =
            std::make_shared<engine::fixture::FixtureAssetStore>();
        engine::fixture::addFixtureDependency(*store);
        store->add(bundleA.virtualPath, bundleA.archiveBytes);
        store->add(bundleB.virtualPath, tamperedB);
        PhlosionArchiveSession session(store, engine::fixture::fixtureAssetResolver);
        FixtureContent initialContent;
        if (!session.begin(bundleA.rootAssetId, ArchiveKind::Scene, &error) ||
            !pumpSessionUntilTerminal(session, error) ||
            session.state() != ArchiveSessionState::Staged ||
            !engine::fixture::validateFixtureSnapshot(
                *session.staged(), initialContent, &error) ||
            !session.activate()) {
            outFail = "valid A activation failed: " + error;
            return false;
        }
        const std::shared_ptr<const engine::assets::phlosion::ArchiveSnapshot>
            activeA = session.active();
        if (!activeA ||
            activeA->graph.rootAssetId != engine::fixture::kFixtureRootA) {
            outFail = "active A snapshot was not published";
            return false;
        }
        if (!session.begin(bundleB.rootAssetId, ArchiveKind::Scene, &error) ||
            !pumpSessionUntilTerminal(session, error) ||
            session.state() != ArchiveSessionState::Staged) {
            outFail = "tampered B did not stage: " + error + session.error();
            return false;
        }
        FixtureContent rejected;
        if (engine::fixture::validateFixtureSnapshot(
                *session.staged(), rejected, &error)) {
            outFail = "tampered B passed fixture validation";
            return false;
        }
        session.discard();
        if (session.active() != activeA ||
            session.active()->graph.rootAssetId !=
                engine::fixture::kFixtureRootA) {
            outFail = "a failed validation replaced the active A snapshot";
            return false;
        }
    }
    return true;
}

bool caseExpectedContentGuards(std::string& outFail) {
    engine::fixture::ExpectedContentResult result;
    const SyntheticFrame a0 = makeSyntheticFrame(false, 0);
    const SyntheticFrame a30 = makeSyntheticFrame(false, 30);
    const SyntheticFrame b0 = makeSyntheticFrame(true, 0);
    const SyntheticFrame b30 = makeSyntheticFrame(true, 30);
    if (!validateSynthetic(a0, false, 0, result, outFail) || !result.ok ||
        !validateSynthetic(a30, false, 30, result, outFail) || !result.ok ||
        !validateSynthetic(b0, true, 0, result, outFail) || !result.ok ||
        !validateSynthetic(b30, true, 30, result, outFail) || !result.ok) {
        outFail = "expected-content guard rejected a valid synthetic frame: " + outFail;
        return false;
    }

    SyntheticFrame blank = a0;
    for (std::size_t index = 0u; index < blank.image.rgba.size(); index += 4u) {
        blank.image.rgba[index + 0u] = 30u;
        blank.image.rgba[index + 1u] = 30u;
        blank.image.rgba[index + 2u] = 30u;
        blank.image.rgba[index + 3u] = 255u;
    }
    if (!expectRejected(blank, false, 0, "blank frame was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame missingMarker = a0;
    fillRect(missingMarker.image, missingMarker.rects.at("marker"), kBackground);
    if (!expectRejected(
            missingMarker, false, 0, "missing marker was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame missingRing = a0;
    fillRect(missingRing.image, missingRing.rects.at("hidden"), kBackground);
    if (!expectRejected(
            missingRing, false, 0, "missing hidden ring was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame depthFailure = a0;
    fillRect(depthFailure.image, depthFailure.rects.at("occluder"), kHidden);
    if (!expectRejected(
            depthFailure, false, 0, "depth-order failure was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame missingBlend = a0;
    fillRect(missingBlend.image, missingBlend.rects.at("blend"), kBackground);
    if (!expectRejected(
            missingBlend, false, 0, "missing alpha blend was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame opaqueBlend = a0;
    fillRect(
        opaqueBlend.image,
        opaqueBlend.rects.at("blend"),
        {237u, 60u, 50u});
    if (!expectRejected(
            opaqueBlend, false, 0, "opaque blend was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame solidBoard = a0;
    fillRect(solidBoard.image, solidBoard.rects.at("board"), kMagenta);
    if (!expectRejected(
            solidBoard, false, 0, "solid-color board was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame swappedBoard = a0;
    fillBoardQuadrants(
        swappedBoard.image,
        swappedBoard.rects.at("board"),
        kMagenta,
        kCyan,
        kCyan,
        kMagenta);
    if (!expectRejected(
            swappedBoard, false, 0, "wrong checker orientation was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame staleA = b30;
    fillBoardQuadrants(
        staleA.image,
        staleA.rects.at("board"),
        kCyan,
        kMagenta,
        kMagenta,
        kCyan);
    if (!expectRejected(
            staleA, true, 30, "stale content A inside a B frame was not rejected", outFail)) {
        return false;
    }

    SyntheticFrame wrongFrame = makeSyntheticFrame(false, 0);
    if (!expectRejected(
            wrongFrame, false, 30, "wrong-frame marker position was not rejected", outFail)) {
        return false;
    }
    return true;
}

bool caseViewportAndComparisons(std::string& outFail) {
    FixtureImage capture;
    capture.width = 200;
    capture.height = 120;
    capture.rgba.assign(
        static_cast<std::size_t>(capture.width) *
            static_cast<std::size_t>(capture.height) * 4u,
        0u);
    for (std::size_t index = 0u; index < capture.rgba.size(); index += 4u) {
        capture.rgba[index + 0u] = 35u;
        capture.rgba[index + 1u] = 38u;
        capture.rgba[index + 2u] = 42u;
        capture.rgba[index + 3u] = 255u;
    }
    const FixtureRoleRect viewportRect{0.20f, 0.15f, 0.55f, 0.65f};
    fillRect(capture, viewportRect, {60u, 200u, 90u});
    FixtureCrop detected;
    std::string error;
    if (!engine::fixture::detectFixtureViewport(capture, detected, &error)) {
        outFail = "viewport detection failed on a synthetic editor capture: " + error;
        return false;
    }
    const int expectedX = static_cast<int>(0.20f * 200.0f);
    const int expectedY = static_cast<int>(0.15f * 120.0f);
    const int expectedWidth = static_cast<int>(0.55f * 200.0f);
    const int expectedHeight = static_cast<int>(0.65f * 120.0f);
    if (std::abs(detected.x - expectedX) > 1 ||
        std::abs(detected.y - expectedY) > 1 ||
        std::abs(detected.width - expectedWidth) > 2 ||
        std::abs(detected.height - expectedHeight) > 2) {
        outFail = "viewport detection did not recover the synthetic green rectangle";
        return false;
    }
    FixtureImage chromeOnly;
    chromeOnly.width = 64;
    chromeOnly.height = 64;
    chromeOnly.rgba.assign(
        static_cast<std::size_t>(chromeOnly.width) *
            static_cast<std::size_t>(chromeOnly.height) * 4u,
        40u);
    if (engine::fixture::detectFixtureViewport(chromeOnly, detected, &error)) {
        outFail = "viewport detection accepted an image with no fixture pixels";
        return false;
    }

    const SyntheticFrame a30 = makeSyntheticFrame(false, 30);
    const SyntheticFrame b30 = makeSyntheticFrame(true, 30);
    const SyntheticFrame a0 = makeSyntheticFrame(false, 0);
    engine::fixture::ImageCompareMetrics metrics;
    if (!engine::fixture::compareFixtureImages(
            a30.image, a30.image, 8, metrics, &error) ||
        metrics.maxChannelDiff != 0 ||
        metrics.normalizedMae != 0.0 ||
        metrics.changedPixelRatio != 0.0) {
        outFail = "identical synthetic frames did not compare exactly: " + error;
        return false;
    }
    if (!engine::fixture::compareFixtureImages(
            a30.image, b30.image, 8, metrics, &error) ||
        metrics.changedPixelRatio <= 0.05 ||
        metrics.normalizedMae <= 0.0) {
        outFail = "pixel comparison did not detect visibly different content";
        return false;
    }
    if (!engine::fixture::compareFixtureImages(
            a0.image, a30.image, 8, metrics, &error) ||
        metrics.changedPixelRatio <= 0.0) {
        outFail = "pixel comparison did not detect the moving marker";
        return false;
    }
    return true;
}

} // namespace

bool test_public_asset_fixture_rendering_contract(std::string& outFail) {
    outFail.clear();
    if (!caseFixtureLifecycle(outFail)) {
        return false;
    }
    if (!caseExpectedContentGuards(outFail)) {
        return false;
    }
    if (!caseViewportAndComparisons(outFail)) {
        return false;
    }
    return true;
}
