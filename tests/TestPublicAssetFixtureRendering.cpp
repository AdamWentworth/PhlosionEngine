#include "FixtureData.h"
#include "FixtureImageGuard.h"
#include "FixturePresentation.h"
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
// Archive and typed mesh tampering helpers
// ---------------------------------------------------------------------------

std::uint64_t meshDocumentHash(const std::vector<std::uint8_t>& bytes) {
    return engine::assets::phrc::contentHash64(
        bytes.data() + engine::assets::phrc::kHeaderBytes,
        bytes.size() - engine::assets::phrc::kHeaderBytes);
}

// One tampered scene archive plus the typed mesh resources it must be decoded
// against. Container integrity (chunk hashes, manifest records, dependency
// hashes and the document content hash) is repaired, so only the intended
// semantic change can reject the candidate.
struct TamperedFixture {
    std::vector<std::uint8_t> archiveBytes;
    std::vector<engine::fixture::FixtureMeshResource> meshes;
};

using FixtureMutation = std::function<void(
    nlohmann::json& scene,
    std::vector<engine::assets::phrc::Document>& meshes)>;

bool findChunkIndex(
    const nlohmann::json& manifest,
    const std::string& path,
    std::size_t& outIndex) {
    for (const auto& record : manifest.at("files")) {
        if (record.value("path", std::string{}) == path) {
            outIndex = record.at("chunk").get<std::size_t>();
            return true;
        }
    }
    return false;
}

bool tamperFixtureBundle(
    const FixtureBundle& bundle,
    const FixtureMutation& mutate,
    TamperedFixture& out,
    std::string& outError) {
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(bundle.archiveBytes, document, &outError)) {
        return false;
    }
    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(document.manifestJson);
    } catch (const nlohmann::json::exception& exception) {
        outError = std::string("fixture manifest is not valid JSON: ") +
            exception.what();
        return false;
    }
    std::size_t sceneIndex = 0u;
    if (!findChunkIndex(manifest, "fixture/scene.json", sceneIndex)) {
        outError = "fixture manifest has no scene document record";
        return false;
    }
    nlohmann::json scene = nlohmann::json::parse(
        document.chunks[sceneIndex].bytes.begin(),
        document.chunks[sceneIndex].bytes.end());

    std::vector<engine::assets::phrc::Document> meshes;
    meshes.reserve(bundle.meshes.size());
    for (const engine::fixture::FixtureMeshResource& resource : bundle.meshes) {
        engine::assets::phrc::Document meshDocument;
        if (!engine::assets::phrc::decode(
                resource.bytes,
                meshDocument,
                &outError)) {
            return false;
        }
        meshes.push_back(std::move(meshDocument));
    }
    mutate(scene, meshes);
    if (scene.at("objects").size() != meshes.size()) {
        outError = "fixture scene and mesh resource counts disagree";
        return false;
    }

    out.meshes = bundle.meshes;
    std::uint64_t contentHash = engine::fixture::kFnvOffset;
    std::vector<engine::assets::phrc::Dependency> dependencies;
    engine::assets::phrc::Dependency leaf;
    leaf.assetId = bundle.dependencyAssetId;
    leaf.expectedContentHash = bundle.dependencyHash;
    leaf.flags = engine::assets::phrc::kDependencyRequired;
    dependencies.push_back(std::move(leaf));
    for (std::size_t index = 0u; index < meshes.size(); ++index) {
        if (!engine::assets::phrc::encode(
                meshes[index],
                out.meshes[index].bytes,
                &outError)) {
            return false;
        }
        out.meshes[index].contentHash =
            meshDocumentHash(out.meshes[index].bytes);
        scene["objects"][index]["mesh_hash"] =
            engine::fixture::hashHex(out.meshes[index].contentHash);
        engine::assets::phrc::Dependency meshDependency;
        meshDependency.assetId = out.meshes[index].assetId;
        meshDependency.expectedContentHash = out.meshes[index].contentHash;
        meshDependency.flags = engine::assets::phrc::kDependencyRequired;
        dependencies.push_back(std::move(meshDependency));
        contentHash = engine::fixture::fnv1a64(
            out.meshes[index].bytes,
            contentHash);
        for (const auto& slot : scene["objects"][index]["material_slots"]) {
            const std::string texturePath =
                slot.at("texture").get<std::string>();
            std::size_t textureIndex = 0u;
            if (!findChunkIndex(manifest, texturePath, textureIndex) ||
                textureIndex >= document.chunks.size()) {
                outError = "fixture manifest has no texture record for " +
                    texturePath;
                return false;
            }
            contentHash = engine::fixture::fnv1a64(
                document.chunks[textureIndex].bytes,
                contentHash);
        }
    }
    scene["content_hash"] = engine::fixture::hashHex(contentHash);
    document.dependencies = std::move(dependencies);
    const std::string sceneText = scene.dump(2);
    document.chunks[sceneIndex].bytes.assign(
        sceneText.begin(),
        sceneText.end());
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
    return engine::assets::phrc::encode(document, out.archiveBytes, &outError);
}

// Mutates the typed mesh documents without repairing the scene's dependency
// constraints or content hash, so a changed mesh must fail the graph instead of
// the consumer gate.
bool tamperMeshBytesOnly(
    const FixtureBundle& bundle,
    const std::function<void(
        std::vector<engine::assets::phrc::Document>&)>& mutate,
    TamperedFixture& out,
    std::string& outError) {
    std::vector<engine::assets::phrc::Document> meshes;
    meshes.reserve(bundle.meshes.size());
    for (const engine::fixture::FixtureMeshResource& resource : bundle.meshes) {
        engine::assets::phrc::Document meshDocument;
        if (!engine::assets::phrc::decode(
                resource.bytes,
                meshDocument,
                &outError)) {
            return false;
        }
        meshes.push_back(std::move(meshDocument));
    }
    mutate(meshes);
    out.archiveBytes = bundle.archiveBytes;
    out.meshes = bundle.meshes;
    for (std::size_t index = 0u; index < meshes.size(); ++index) {
        if (!engine::assets::phrc::encode(
                meshes[index],
                out.meshes[index].bytes,
                &outError)) {
            return false;
        }
        out.meshes[index].contentHash =
            meshDocumentHash(out.meshes[index].bytes);
    }
    return true;
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
    const std::vector<std::uint8_t>& archiveBytes,
    const std::vector<engine::fixture::FixtureMeshResource>& meshes) {
    auto store = std::make_shared<engine::fixture::FixtureAssetStore>();
    engine::fixture::addFixtureDependency(*store);
    store->add(bundle.virtualPath, archiveBytes);
    for (const engine::fixture::FixtureMeshResource& mesh : meshes) {
        store->add(mesh.virtualPath, mesh.bytes);
    }
    return store;
}

bool expectTamperedArchiveRejected(
    const FixtureBundle& bundle,
    const FixtureMutation& mutate,
    const std::string& expectedText,
    std::string& outFail) {
    TamperedFixture tampered;
    std::string error;
    if (!tamperFixtureBundle(bundle, mutate, tampered, error)) {
        outFail = "fixture archive tamper failed: " + error;
        return false;
    }
    const std::shared_ptr<engine::fixture::FixtureAssetStore> store =
        makeFixtureStore(bundle, tampered.archiveBytes, tampered.meshes);
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

std::shared_ptr<engine::fixture::FixtureAssetStore> makePairStore(
    const FixtureBundle& bundleA,
    const FixtureBundle& bundleB,
    const TamperedFixture& tamperedB) {
    auto store = std::make_shared<engine::fixture::FixtureAssetStore>();
    engine::fixture::addFixtureDependency(*store);
    store->add(bundleA.virtualPath, bundleA.archiveBytes);
    for (const engine::fixture::FixtureMeshResource& mesh : bundleA.meshes) {
        store->add(mesh.virtualPath, mesh.bytes);
    }
    store->add(bundleB.virtualPath, tamperedB.archiveBytes);
    for (const engine::fixture::FixtureMeshResource& mesh : tamperedB.meshes) {
        store->add(mesh.virtualPath, mesh.bytes);
    }
    return store;
}

// Activates A, stages the tampered B candidate, requires fixture validation to
// reject it with `expectedText`, then proves the discarded candidate left the
// published A snapshot untouched.
bool expectCandidateKeepsActiveA(
    const FixtureBundle& bundleA,
    const FixtureBundle& bundleB,
    const TamperedFixture& tamperedB,
    const std::string& expectedText,
    std::string& outFail) {
    std::string error;
    const std::shared_ptr<engine::fixture::FixtureAssetStore> store =
        makePairStore(bundleA, bundleB, tamperedB);
    PhlosionArchiveSession session(store, engine::fixture::fixtureAssetResolver);
    FixtureContent initialContent;
    if (!session.begin(bundleA.rootAssetId, ArchiveKind::Scene, &error) ||
        !pumpSessionUntilTerminal(session, error) ||
        session.state() != ArchiveSessionState::Staged ||
        !engine::fixture::validateFixtureSnapshot(
            *session.staged(),
            initialContent,
            &error) ||
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
            *session.staged(),
            rejected,
            &error)) {
        outFail = "tampered B passed fixture validation";
        return false;
    }
    if (error.find(expectedText) == std::string::npos) {
        outFail = "tampered B diagnostic did not contain '" + expectedText +
            "': " + error;
        return false;
    }
    session.discard();
    if (session.active() != activeA ||
        session.active()->graph.rootAssetId != engine::fixture::kFixtureRootA) {
        outFail = "a failed validation replaced the active A snapshot";
        return false;
    }
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
        const std::string meshAssetIdA = boardA->meshAssetId;
        const std::string meshIdA = boardA->mesh.meshId;
        const std::uint64_t meshHashA = boardA->meshDocumentHash;
        const std::string textureKeyA =
            boardA->materialBindings.front().textureKey;
        const std::string textureCacheKeyA =
            boardA->materialBindings.front().textureCacheKey;
        const std::vector<std::uint8_t> textureA =
            boardA->materialBindings.front().textureRgba;
        const float markerAFromX = markerA->motion.from[0];
        if (boardA->mesh.submeshes.empty() ||
            boardA->mesh.materialSlots !=
                std::vector<std::string>{
                    engine::fixture::kFixtureMeshMaterialSlot}) {
            outFail = "content A board mesh has no typed submesh or material slot";
            return false;
        }
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
            boardB->meshAssetId == meshAssetIdA ||
            boardB->mesh.meshId != meshIdA ||
            boardB->meshDocumentHash == meshHashA ||
            boardB->materialBindings.front().textureKey != textureKeyA ||
            boardB->materialBindings.front().textureCacheKey ==
                textureCacheKeyA ||
            boardB->materialBindings.front().textureRgba == textureA) {
            outFail =
                "content B does not keep the logical mesh/texture identity while distinguishing versioned resources and cache keys";
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
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["camera"]["fov_degrees"] = 200.0;
                },
                "field of view",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["camera"]["position"][0] = 1.0e39;
                },
                "camera values must be finite",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][5]["motion"]["from"][0] = 1.0e39;
                },
                "motion values must be finite",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][1]["id"] = "background";
                },
                "object id is duplicated",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][1]["role"] = "background";
                },
                "object role is duplicated",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][0]["material_slots"][0]["texture_width"] = 8;
                },
                "texture dimensions do not match",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][0]["material_slots"][0]["material_mode"] = 9;
                },
                "material values are unsupported",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][0]["id"] = "";
                },
                "non-empty id",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["camera"]["near"] = 0.0;
                },
                "clip planes",
                outFail)) {
            return false;
        }
        // Scene-side mesh references and mesh-side semantics are both checked.
        if (!expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][2]["mesh"] = "fixture/mesh/other-a";
                },
                "mesh reference does not match the object role",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][2]["mesh_id"] = "fixture/mesh/other";
                },
                "mesh id does not match the document",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][3]["material_slots"][0]["slot"] = "other";
                },
                "has no material binding for mesh slot",
                outFail)) {
            return false;
        }

        // A valid PHRC document whose typed mesh semantics are wrong must be
        // rejected by the mesh decoder, not by the container.
        if (!expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json&,
                   std::vector<engine::assets::phrc::Document>& meshes) {
                    const float value =
                        std::numeric_limits<float>::quiet_NaN();
                    std::memcpy(
                        meshes[0].chunks[0].bytes.data(),
                        &value,
                        sizeof(value));
                },
                "position must be finite",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json&,
                   std::vector<engine::assets::phrc::Document>& meshes) {
                    nlohmann::json manifest =
                        nlohmann::json::parse(meshes[1].manifestJson);
                    manifest["bounds"]["max"][1] = 5.0;
                    meshes[1].manifestJson = manifest.dump();
                },
                "bounds do not match the vertex extents",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json&,
                   std::vector<engine::assets::phrc::Document>& meshes) {
                    nlohmann::json manifest =
                        nlohmann::json::parse(meshes[1].manifestJson);
                    manifest["submeshes"][0]["first_index"] = 3;
                    meshes[1].manifestJson = manifest.dump();
                },
                "partition the index list in order",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json&,
                   std::vector<engine::assets::phrc::Document>& meshes) {
                    nlohmann::json manifest =
                        nlohmann::json::parse(meshes[1].manifestJson);
                    manifest["submeshes"][0]["material_slot"] = 4;
                    meshes[1].manifestJson = manifest.dump();
                },
                "references an invalid material slot",
                outFail) ||
            !expectTamperedArchiveRejected(
                bundle,
                [](nlohmann::json&,
                   std::vector<engine::assets::phrc::Document>& meshes) {
                    nlohmann::json manifest =
                        nlohmann::json::parse(meshes[1].manifestJson);
                    manifest["skin"] = {{"skeleton", "characters/demo"}};
                    meshes[1].manifestJson = manifest.dump();
                },
                "mesh.skin' is unsupported",
                outFail)) {
            return false;
        }

        // Tampering only the mesh bytes leaves the scene-to-mesh dependency
        // hash constraint intact, so the graph itself must fail.
        TamperedFixture unhashedTamper;
        std::string tamperError;
        if (!tamperMeshBytesOnly(
                bundle,
                [](std::vector<engine::assets::phrc::Document>& meshes) {
                    meshes[2].chunks[1].bytes[0] = 1u;
                },
                unhashedTamper,
                tamperError)) {
            outFail = "mesh-only tamper failed: " + tamperError;
            return false;
        }
        {
            const std::shared_ptr<engine::fixture::FixtureAssetStore> store =
                makeFixtureStore(
                    bundle,
                    unhashedTamper.archiveBytes,
                    unhashedTamper.meshes);
            PhlosionArchiveSession session(
                store,
                engine::fixture::fixtureAssetResolver);
            if (!session.begin(bundle.rootAssetId, ArchiveKind::Scene, &tamperError) ||
                !pumpSessionUntilTerminal(session, tamperError) ||
                session.state() != ArchiveSessionState::Failed) {
                outFail =
                    "a mesh dependency whose hash constraint no longer matches did not fail the graph";
                return false;
            }
            if (session.error().find(
                    engine::fixture::fixtureMeshAssetId("occluder", "a")) ==
                std::string::npos) {
                outFail =
                    "the mesh hash diagnostic does not name the dependency: " +
                    session.error();
                return false;
            }
            session.discard();
        }
    }
    {
        // A candidate whose typed mesh dependency is valid PHRC content but
        // semantically invalid, and a candidate whose scene document fails
        // validation, must both leave the active A snapshot untouched.
        const FixtureBundle bundleA = engine::fixture::buildFixtureBundle(
            FixtureContentVariant::A);
        const FixtureBundle bundleB = engine::fixture::buildFixtureBundle(
            FixtureContentVariant::B);
        TamperedFixture sceneTamper;
        std::string error;
        if (!tamperFixtureBundle(
                bundleB,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    scene["objects"][0]["material_slots"][0]["texture_width"] =
                        8;
                },
                sceneTamper,
                error)) {
            outFail = "candidate tamper failed: " + error;
            return false;
        }
        if (!expectCandidateKeepsActiveA(
                bundleA,
                bundleB,
                sceneTamper,
                "texture dimensions do not match",
                outFail)) {
            return false;
        }

        // The mesh document keeps a valid container and a matching dependency
        // edge, so only the typed mesh semantics reject the candidate.
        TamperedFixture meshTamper;
        if (!tamperFixtureBundle(
                bundleB,
                [](nlohmann::json&,
                   std::vector<engine::assets::phrc::Document>& meshes) {
                    nlohmann::json manifest =
                        nlohmann::json::parse(meshes[1].manifestJson);
                    manifest["bounds"]["max"][1] =
                        manifest["bounds"]["max"][1].get<double>() + 1.0;
                    meshes[1].manifestJson = manifest.dump();
                },
                meshTamper,
                error)) {
            outFail = "candidate mesh tamper failed: " + error;
            return false;
        }
        if (!expectCandidateKeepsActiveA(
                bundleA,
                bundleB,
                meshTamper,
                "bounds do not match the vertex extents",
                outFail)) {
            return false;
        }
    }
    return true;
}

// Consumer-level regression for the typed mesh bridge: a candidate whose board
// mesh declares two submeshes and two material slots (both bindings share the
// object's texture file) must validate and activate, the consumer must map every
// submesh to the binding for its own slot label, and the renderer adapter must
// forward every static attribute, including tangent w.
bool caseMultiSlotMeshConsumer(std::string& outFail) {
    const auto twoSubmeshTwoSlotBoard =
        [](nlohmann::json& scene,
           std::vector<engine::assets::phrc::Document>& meshes) {
            nlohmann::json manifest =
                nlohmann::json::parse(meshes[1].manifestJson);
            const nlohmann::json first = {
                {"first_index", 0},
                {"index_count", 3},
                {"material_slot", 0}};
            const nlohmann::json second = {
                {"first_index", 3},
                {"index_count", 3},
                {"material_slot", 1}};
            manifest["submeshes"] = nlohmann::json::array({first, second});
            manifest["material_slots"] =
                nlohmann::json::array({"surface", "overlay"});
            manifest["counts"]["submeshes"] = 2;
            manifest["counts"]["material_slots"] = 2;
            meshes[1].manifestJson = manifest.dump();

            scene["objects"][1]["mesh_submeshes"] = 2;
            scene["objects"][1]["mesh_material_slots"] = 2;
            const nlohmann::json surface =
                scene["objects"][1]["material_slots"][0];
            nlohmann::json overlay = surface;
            overlay["slot"] = "overlay";
            scene["objects"][1]["material_slots"] =
                nlohmann::json::array({surface, overlay});
        };

    const FixtureBundle bundleA =
        engine::fixture::buildFixtureBundle(FixtureContentVariant::A);
    const FixtureBundle bundleB =
        engine::fixture::buildFixtureBundle(FixtureContentVariant::B);

    {
        TamperedFixture twoSlot;
        std::string error;
        if (!tamperFixtureBundle(
                bundleB,
                twoSubmeshTwoSlotBoard,
                twoSlot,
                error)) {
            outFail = "two-slot candidate tamper failed: " + error;
            return false;
        }
        const std::shared_ptr<engine::fixture::FixtureAssetStore> store =
            makePairStore(bundleA, bundleB, twoSlot);
        PhlosionArchiveSession session(
            store,
            engine::fixture::fixtureAssetResolver);
        FixtureContent contentA;
        if (!session.begin(bundleA.rootAssetId, ArchiveKind::Scene, &error) ||
            !pumpSessionUntilTerminal(session, error) ||
            session.state() != ArchiveSessionState::Staged ||
            !engine::fixture::validateFixtureSnapshot(
                *session.staged(),
                contentA,
                &error) ||
            !session.activate()) {
            outFail = "valid A activation failed: " + error;
            return false;
        }
        if (!session.begin(bundleB.rootAssetId, ArchiveKind::Scene, &error) ||
            !pumpSessionUntilTerminal(session, error) ||
            session.state() != ArchiveSessionState::Staged) {
            outFail = "two-slot candidate did not stage: " + error +
                session.error();
            return false;
        }
        FixtureContent contentB;
        if (!engine::fixture::validateFixtureSnapshot(
                *session.staged(),
                contentB,
                &error)) {
            outFail = "two-slot candidate failed fixture validation: " + error;
            return false;
        }
        const engine::fixture::FixtureObject* board =
            contentB.findRole("board");
        if (board == nullptr) {
            outFail = "two-slot candidate has no board object";
            return false;
        }
        if (board->mesh.submeshes.size() != 2u ||
            board->mesh.materialSlots !=
                std::vector<std::string>({"surface", "overlay"}) ||
            board->materialBindings.size() != 2u) {
            outFail = "two-slot candidate lost its submeshes, slots or bindings";
            return false;
        }
        if (board->mesh.submeshes[0].firstIndex != 0u ||
            board->mesh.submeshes[0].indexCount != 3u ||
            board->mesh.submeshes[1].firstIndex != 3u ||
            board->mesh.submeshes[1].indexCount != 3u ||
            board->mesh.submeshes[0].materialSlot ==
                board->mesh.submeshes[1].materialSlot) {
            outFail =
                "two-slot candidate submeshes do not partition the index list across two slots";
            return false;
        }
        for (const engine::assets::phlosion::MeshSubmesh& submesh :
             board->mesh.submeshes) {
            const std::string& slot =
                board->mesh.materialSlots[submesh.materialSlot];
            const engine::fixture::FixtureMaterialBinding* binding =
                board->findMaterialBinding(slot);
            if (binding == nullptr || binding->slotLabel != slot ||
                binding->textureKey != "fixture/tex/board" ||
                binding->textureRgba.empty()) {
                outFail =
                    "the consumer did not resolve submesh slot '" + slot +
                    "' to its own material binding";
                return false;
            }
        }
        if (!session.activate() || !session.active() ||
            session.active()->graph.rootAssetId !=
                engine::fixture::kFixtureRootB) {
            outFail = "the valid two-slot candidate did not activate";
            return false;
        }
        session.unload();
    }

    {
        // A duplicated binding for a single-slot mesh must be rejected even
        // though the container and every hash are repaired.
        TamperedFixture duplicated;
        std::string error;
        if (!tamperFixtureBundle(
                bundleB,
                [](nlohmann::json& scene,
                   std::vector<engine::assets::phrc::Document>&) {
                    const nlohmann::json surface =
                        scene["objects"][1]["material_slots"][0];
                    scene["objects"][1]["material_slots"] =
                        nlohmann::json::array({surface, surface});
                },
                duplicated,
                error)) {
            outFail = "duplicated binding tamper failed: " + error;
            return false;
        }
        if (!expectCandidateKeepsActiveA(
                bundleA,
                bundleB,
                duplicated,
                "must bind each mesh material slot exactly once",
                outFail)) {
            return false;
        }
    }

    {
        // A two-slot mesh with only one binding must be rejected.
        TamperedFixture missing;
        std::string error;
        if (!tamperFixtureBundle(
                bundleB,
                [&twoSubmeshTwoSlotBoard](
                    nlohmann::json& scene,
                    std::vector<engine::assets::phrc::Document>& meshes) {
                    twoSubmeshTwoSlotBoard(scene, meshes);
                    const nlohmann::json surface =
                        scene["objects"][1]["material_slots"][0];
                    scene["objects"][1]["material_slots"] =
                        nlohmann::json::array({surface});
                },
                missing,
                error)) {
            outFail = "missing binding tamper failed: " + error;
            return false;
        }
        if (!expectCandidateKeepsActiveA(
                bundleA,
                bundleB,
                missing,
                "has no material binding for mesh slot 'overlay'",
                outFail)) {
            return false;
        }
    }

    // The adapter forwards every static attribute; dropping tangent w here
    // would silently rewrite authored geometry at the renderer boundary.
    engine::assets::phlosion::MeshVertex vertex;
    vertex.position = {1.5f, -2.5f, 3.5f};
    vertex.normal = {0.25f, -0.5f, 0.75f};
    vertex.tangent = {0.5f, -0.25f, 0.125f, -1.0f};
    vertex.texcoord = {0.25f, 0.75f};
    vertex.color = {0.1f, 0.2f, 0.3f, 0.4f};
    const engine::render::backend::WorldMeshVertex world =
        engine::fixture::fixtureWorldVertex(vertex);
    if (world.x != 1.5f || world.y != -2.5f || world.z != 3.5f ||
        world.u != 0.25f || world.v != 0.75f ||
        world.r != 0.1f || world.g != 0.2f ||
        world.b != 0.3f || world.a != 0.4f ||
        world.nx != 0.25f || world.ny != -0.5f || world.nz != 0.75f ||
        world.tx != 0.5f || world.ty != -0.25f ||
        world.tz != 0.125f || world.tw != -1.0f) {
        outFail = "the fixture world-vertex adapter dropped a static attribute";
        return false;
    }

    // The fixture's own meshes carry an authored tangent, so the presented
    // vertices must reach the renderer with `tw = +1` instead of a default.
    const std::shared_ptr<engine::fixture::FixtureAssetStore> store =
        makePairStore(
            bundleA,
            bundleB,
            TamperedFixture{bundleB.archiveBytes, bundleB.meshes});
    PhlosionArchiveSession session(store, engine::fixture::fixtureAssetResolver);
    FixtureContent contentA;
    std::string error;
    if (!session.begin(bundleA.rootAssetId, ArchiveKind::Scene, &error) ||
        !pumpSessionUntilTerminal(session, error) ||
        session.state() != ArchiveSessionState::Staged ||
        !engine::fixture::validateFixtureSnapshot(
            *session.staged(),
            contentA,
            &error)) {
        outFail = "fixture content for the tangent check failed: " + error;
        return false;
    }
    for (const engine::fixture::FixtureObject& object : contentA.objects) {
        for (const engine::assets::phlosion::MeshVertex& meshVertex :
             object.mesh.vertices) {
            const engine::render::backend::WorldMeshVertex mapped =
                engine::fixture::fixtureWorldVertex(meshVertex);
            if (meshVertex.tangent !=
                    std::array<float, 4>{1.0f, 0.0f, 0.0f, 1.0f} ||
                mapped.tx != 1.0f || mapped.ty != 0.0f ||
                mapped.tz != 0.0f || mapped.tw != 1.0f) {
                outFail =
                    "fixture presentation lost the authored mesh tangent";
                return false;
            }
        }
    }
    session.discard();
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
    if (!caseMultiSlotMeshConsumer(outFail)) {
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
