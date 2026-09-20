#include "engine/assets/phlosion/PhlosionMeshAsset.h"

#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using engine::assets::phlosion::MeshAsset;
using engine::assets::phlosion::MeshAssetLimits;
using engine::assets::phlosion::MeshBounds;
using engine::assets::phlosion::MeshSubmesh;
using engine::assets::phlosion::MeshVertex;

using DocumentMutation = std::function<void(
    nlohmann::json& manifest,
    engine::assets::phrc::Document& document)>;

bool expect(bool condition, std::string message, std::string& outFail) {
    if (!condition) {
        outFail = std::move(message);
        return false;
    }
    return true;
}

MeshBounds computeBounds(const MeshAsset& asset) {
    MeshBounds bounds;
    if (asset.vertices.empty()) {
        return bounds;
    }
    bounds.min = asset.vertices.front().position;
    bounds.max = asset.vertices.front().position;
    for (const MeshVertex& vertex : asset.vertices) {
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            bounds.min[channel] =
                std::min(bounds.min[channel], vertex.position[channel]);
            bounds.max[channel] =
                std::max(bounds.max[channel], vertex.position[channel]);
        }
    }
    return bounds;
}

MeshVertex makeVertex(
    const std::array<float, 3>& position,
    const std::array<float, 4>& color) {
    MeshVertex vertex;
    vertex.position = position;
    vertex.normal = {0.0f, 0.0f, 1.0f};
    vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    vertex.texcoord = {0.0f, 0.0f};
    vertex.color = color;
    return vertex;
}

MeshAsset makeQuadMesh(
    std::string meshId,
    float halfWidth,
    float halfHeight,
    float z) {
    MeshAsset asset;
    asset.meshId = std::move(meshId);
    asset.vertices = {
        makeVertex({-halfWidth, -halfHeight, z}, {1.0f, 1.0f, 1.0f, 1.0f}),
        makeVertex({halfWidth, -halfHeight, z}, {1.0f, 1.0f, 1.0f, 1.0f}),
        makeVertex({halfWidth, halfHeight, z}, {1.0f, 1.0f, 1.0f, 1.0f}),
        makeVertex({-halfWidth, halfHeight, z}, {1.0f, 1.0f, 1.0f, 1.0f})};
    asset.vertices[0].texcoord = {0.0f, 0.0f};
    asset.vertices[1].texcoord = {1.0f, 0.0f};
    asset.vertices[2].texcoord = {1.0f, 1.0f};
    asset.vertices[3].texcoord = {0.0f, 1.0f};
    asset.indices = {0u, 1u, 2u, 0u, 2u, 3u};
    asset.submeshes = {MeshSubmesh{0u, 6u, 0u}};
    asset.materialSlots = {"surface"};
    asset.bounds = computeBounds(asset);
    return asset;
}

// Two submeshes and two material slots over one shared index list.
MeshAsset makeMultiSubmeshMesh() {
    MeshAsset asset = makeQuadMesh("fixture/mesh/multi", 1.0f, 1.0f, 0.0f);
    asset.indices = {0u, 1u, 2u, 0u, 2u, 3u, 3u, 2u, 1u, 3u, 1u, 0u};
    asset.submeshes = {
        MeshSubmesh{0u, 6u, 0u},
        MeshSubmesh{6u, 6u, 1u}};
    asset.materialSlots = {"front", "back"};
    asset.bounds = computeBounds(asset);
    return asset;
}

MeshAsset makeGoldenMesh() {
    MeshAsset asset;
    asset.meshId = "fixture/mesh/golden";
    MeshVertex first;
    first.position = {1.0f, -2.5f, 3.25f};
    first.normal = {0.0f, 1.0f, 0.0f};
    first.tangent = {-1.0f, 0.0f, 0.0f, -1.0f};
    first.texcoord = {0.5f, 0.25f};
    first.color = {1.0f, 0.5f, 0.25f, 0.75f};
    MeshVertex second;
    second.position = {-4.0f, 0.0f, 0.0f};
    second.normal = {0.0f, 0.0f, 1.0f};
    second.tangent = {0.0f, 1.0f, 0.0f, 1.0f};
    second.texcoord = {0.0f, 1.0f};
    second.color = {0.0f, 0.0f, 0.0f, 1.0f};
    MeshVertex third;
    third.position = {0.0f, 4.0f, -0.5f};
    third.normal = {0.0f, 1.0f, 0.0f};
    third.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    third.texcoord = {1.0f, 0.5f};
    third.color = {0.25f, 0.5f, 0.75f, 1.0f};
    asset.vertices = {first, second, third};
    asset.indices = {0u, 2u, 1u};
    asset.submeshes = {MeshSubmesh{0u, 3u, 0u}};
    asset.materialSlots = {"surface"};
    asset.bounds = computeBounds(asset);
    return asset;
}

// Fixed little-endian payload for `makeGoldenMesh`, written as literal bytes so
// the layout, field order and endianness of the encoder cannot drift together
// with a second copy of the same writing logic. Byte 0 is the first float of
// vertex 0's position and the last byte is the last component of its color.
constexpr std::array<std::uint8_t, 64> kGoldenVertex0{
    0x00u, 0x00u, 0x80u, 0x3fu, // position[0] = 1.0f
    0x00u, 0x00u, 0x20u, 0xc0u, // position[1] = -2.5f
    0x00u, 0x00u, 0x50u, 0x40u, // position[2] = 3.25f
    0x00u, 0x00u, 0x00u, 0x00u, // normal[0] = 0.0f
    0x00u, 0x00u, 0x80u, 0x3fu, // normal[1] = 1.0f
    0x00u, 0x00u, 0x00u, 0x00u, // normal[2] = 0.0f
    0x00u, 0x00u, 0x80u, 0xbfu, // tangent[0] = -1.0f
    0x00u, 0x00u, 0x00u, 0x00u, // tangent[1] = 0.0f
    0x00u, 0x00u, 0x00u, 0x00u, // tangent[2] = 0.0f
    0x00u, 0x00u, 0x80u, 0xbfu, // tangent[3] = -1.0f
    0x00u, 0x00u, 0x00u, 0x3fu, // texcoord[0] = 0.5f
    0x00u, 0x00u, 0x80u, 0x3eu, // texcoord[1] = 0.25f
    0x00u, 0x00u, 0x80u, 0x3fu, // color[0] = 1.0f
    0x00u, 0x00u, 0x00u, 0x3fu, // color[1] = 0.5f
    0x00u, 0x00u, 0x80u, 0x3eu, // color[2] = 0.25f
    0x00u, 0x00u, 0x40u, 0x3fu}; // color[3] = 0.75f

constexpr std::array<std::uint8_t, 12> kGoldenIndices{
    0x00u, 0x00u, 0x00u, 0x00u,
    0x02u, 0x00u, 0x00u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u};

bool sameMesh(const MeshAsset& left, const MeshAsset& right) {
    if (left.meshId != right.meshId ||
        left.materialSlots != right.materialSlots ||
        left.bounds.min != right.bounds.min ||
        left.bounds.max != right.bounds.max ||
        left.vertices.size() != right.vertices.size() ||
        left.indices != right.indices ||
        left.submeshes.size() != right.submeshes.size()) {
        return false;
    }
    for (std::size_t index = 0u; index < left.submeshes.size(); ++index) {
        const MeshSubmesh& a = left.submeshes[index];
        const MeshSubmesh& b = right.submeshes[index];
        if (a.firstIndex != b.firstIndex ||
            a.indexCount != b.indexCount ||
            a.materialSlot != b.materialSlot) {
            return false;
        }
    }
    for (std::size_t index = 0u; index < left.vertices.size(); ++index) {
        const MeshVertex& a = left.vertices[index];
        const MeshVertex& b = right.vertices[index];
        if (a.position != b.position ||
            a.normal != b.normal ||
            a.tangent != b.tangent ||
            a.texcoord != b.texcoord ||
            a.color != b.color) {
            return false;
        }
    }
    return true;
}

// A deliberately unrelated asset used to prove that every failure path leaves
// the caller's output untouched.
MeshAsset sentinelMesh() {
    MeshAsset asset;
    asset.meshId = "sentinel/unchanged";
    asset.vertices = {MeshVertex{}};
    asset.indices = {0u, 0u, 0u};
    asset.submeshes = {MeshSubmesh{0u, 3u, 0u}};
    asset.materialSlots = {"sentinel"};
    asset.bounds = computeBounds(asset);
    return asset;
}

bool isSentinel(const MeshAsset& asset) {
    return sameMesh(asset, sentinelMesh());
}

bool reencodeMesh(
    const std::vector<std::uint8_t>& source,
    const DocumentMutation& mutate,
    std::vector<std::uint8_t>& outBytes,
    std::string& outError) {
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(source, document, &outError)) {
        return false;
    }
    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(document.manifestJson);
    } catch (const nlohmann::json::exception& exception) {
        outError = std::string("mesh manifest is not valid JSON: ") +
            exception.what();
        return false;
    }
    mutate(manifest, document);
    document.manifestJson = manifest.dump();
    std::string encodeError;
    if (!engine::assets::phrc::encode(document, outBytes, &encodeError)) {
        outError = "phrc re-encode failed: " + encodeError;
        return false;
    }
    return true;
}

bool expectDocumentRejected(
    const std::vector<std::uint8_t>& source,
    const DocumentMutation& mutate,
    const std::string& expectedText,
    std::string& outFail,
    const MeshAssetLimits& limits = {}) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!reencodeMesh(source, mutate, bytes, error)) {
        outFail = "mesh document mutation failed: " + error;
        return false;
    }
    MeshAsset decoded = sentinelMesh();
    error.clear();
    if (engine::assets::phlosion::decodeMeshAsset(bytes, decoded, limits, &error)) {
        outFail = "mesh decoder accepted a malformed document expected to fail "
            "with '" + expectedText + "'";
        return false;
    }
    if (!isSentinel(decoded)) {
        outFail = "a failed mesh decode modified its output for '" +
            expectedText + "'";
        return false;
    }
    if (error.find(expectedText) == std::string::npos) {
        outFail = "mesh diagnostic did not contain '" + expectedText +
            "': " + error;
        return false;
    }
    return true;
}

bool expectAssetRejected(
    const MeshAsset& asset,
    const std::string& expectedText,
    std::string& outFail,
    const MeshAssetLimits& limits = {}) {
    std::string error;
    if (engine::assets::phlosion::validateMeshAsset(asset, limits, &error)) {
        outFail = "mesh validation accepted malformed authored data expected to "
            "contain '" + expectedText + "'";
        return false;
    }
    if (error.find(expectedText) == std::string::npos) {
        outFail = "mesh validation diagnostic did not contain '" +
            expectedText + "': " + error;
        return false;
    }
    std::vector<std::uint8_t> bytes{0xAAu, 0xBBu};
    const std::vector<std::uint8_t> before = bytes;
    error.clear();
    if (engine::assets::phlosion::encodeMeshAsset(asset, bytes, limits, &error)) {
        outFail = "mesh encoder accepted malformed authored data expected to "
            "contain '" + expectedText + "'";
        return false;
    }
    if (bytes != before || error.empty()) {
        outFail = "a failed mesh encode modified its output or cleared its diagnostic";
        return false;
    }
    return true;
}

class InMemoryStore final : public engine::IAssetStore {
public:
    void add(std::string virtualPath, std::vector<std::uint8_t> bytes) {
        assets_.insert_or_assign(std::move(virtualPath), std::move(bytes));
    }

    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError = nullptr) const override {
        std::vector<std::uint8_t> bytes;
        if (!readBytes(virtualPath, bytes, outError)) {
            return false;
        }
        outText.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError = nullptr) const override {
        const auto found = assets_.find(virtualPath);
        if (found == assets_.end()) {
            if (outError) {
                *outError = "mesh test store has no asset: " + virtualPath;
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

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

bool caseDeterministicRoundTrip(std::string& outFail) {
    const MeshAsset multi = makeMultiSubmeshMesh();
    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> second;
    std::string error;
    if (!engine::assets::phlosion::encodeMeshAsset(multi, first, {}, &error) ||
        !engine::assets::phlosion::encodeMeshAsset(multi, second, {}, &error)) {
        outFail = "multi-submesh mesh encode failed: " + error;
        return false;
    }
    if (first != second || first.empty() || !error.empty()) {
        outFail = "mesh encode is not deterministic or returned a diagnostic";
        return false;
    }

    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(first, document, &error)) {
        outFail = "encoded mesh is not a valid PHRC document: " + error;
        return false;
    }
    if (document.magic != engine::assets::phrc::magic("PHMS") ||
        document.schemaVersion != 1u ||
        document.flags != 0u ||
        !document.dependencies.empty() ||
        document.chunks.size() != 2u ||
        document.chunks[0].type != engine::assets::phrc::magic("VTX0") ||
        document.chunks[1].type != engine::assets::phrc::magic("IDX0")) {
        outFail = "mesh container header or chunk layout is wrong";
        return false;
    }
    for (const engine::assets::phrc::Chunk& chunk : document.chunks) {
        if (chunk.flags != 0u || chunk.alignment != 16u) {
            outFail = "mesh chunks must declare zero flags and 16-byte alignment";
            return false;
        }
    }

    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(document.manifestJson);
    } catch (const nlohmann::json::exception& exception) {
        outFail = "mesh manifest is not valid JSON: " +
            std::string(exception.what());
        return false;
    }
    if (manifest.at("root_type").get<std::string>() != "Mesh" ||
        manifest.at("schema_version").get<std::uint32_t>() != 1u ||
        manifest.at("mesh_id").get<std::string>() != multi.meshId ||
        manifest.at("vertex_layout").get<std::string>() != "pntc-f32-v1" ||
        manifest.at("coordinate_system").get<std::string>() !=
            "right-handed-y-up" ||
        manifest.at("topology").get<std::string>() != "triangles" ||
        manifest.at("cooker").get<std::string>() != "phlosion-mesh-v1" ||
        manifest.at("target_profile").get<std::string>() != "portable") {
        outFail = "mesh manifest identity or schema fields are wrong";
        return false;
    }
    if (manifest.at("counts").at("vertices").get<std::size_t>() != 4u ||
        manifest.at("counts").at("indices").get<std::size_t>() != 12u ||
        manifest.at("counts").at("submeshes").get<std::size_t>() != 2u ||
        manifest.at("counts").at("material_slots").get<std::size_t>() != 2u) {
        outFail = "mesh manifest counts do not match the asset";
        return false;
    }

    MeshAsset decoded;
    error = "stale";
    if (!engine::assets::phlosion::decodeMeshAsset(first, decoded, {}, &error)) {
        outFail = "mesh round trip decode failed: " + error;
        return false;
    }
    if (!sameMesh(decoded, multi) || !error.empty()) {
        outFail = "mesh round trip lost data";
        return false;
    }
    if (decoded.submeshes[1].firstIndex != 6u ||
        decoded.submeshes[1].materialSlot != 1u ||
        decoded.materialSlots != std::vector<std::string>({"front", "back"})) {
        outFail = "mesh round trip lost submesh or material slot mapping";
        return false;
    }

    if (!engine::assets::phlosion::validateMeshAsset(decoded, {}, &error) ||
        !error.empty()) {
        outFail = "round-tripped mesh failed validation: " + error;
        return false;
    }

    // Decoded assets are validated exactly like authored data: a hand-built
    // document whose manifest counts, bounds or partition disagree is rejected.
    MeshAsset authored = multi;
    authored.bounds.max[1] += 1.0f;
    if (!expectAssetRejected(
            authored,
            "bounds do not match the vertex extents",
            outFail)) {
        return false;
    }
    return true;
}

bool caseGoldenPayload(std::string& outFail) {
    const MeshAsset golden = makeGoldenMesh();
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!engine::assets::phlosion::encodeMeshAsset(golden, bytes, {}, &error)) {
        outFail = "golden mesh encode failed: " + error;
        return false;
    }
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(bytes, document, &error)) {
        outFail = "golden mesh container decode failed: " + error;
        return false;
    }
    if (document.chunks[0].bytes.size() != 3u * kGoldenVertex0.size()) {
        outFail = "VTX0 must carry 64 bytes per vertex";
        return false;
    }
    if (!std::equal(
            document.chunks[0].bytes.begin(),
            document.chunks[0].bytes.begin() +
                static_cast<std::ptrdiff_t>(kGoldenVertex0.size()),
            kGoldenVertex0.begin(),
            kGoldenVertex0.end())) {
        outFail =
            "the first VTX0 vertex does not match the fixed little-endian golden bytes";
        return false;
    }
    if (!std::equal(
            document.chunks[1].bytes.begin(),
            document.chunks[1].bytes.end(),
            kGoldenIndices.begin(),
            kGoldenIndices.end())) {
        outFail =
            "the IDX0 payload does not match the fixed little-endian golden bytes";
        return false;
    }
    // The remaining components stay covered by a real round trip rather than a
    // second copy of the encoder's writer.
    MeshAsset goldenDecoded;
    if (!engine::assets::phlosion::decodeMeshAsset(
            bytes,
            goldenDecoded,
            {},
            &error) ||
        !sameMesh(goldenDecoded, golden)) {
        outFail = "the golden mesh did not survive decode: " + error;
        return false;
    }
    return true;
}

bool caseAuthoredValidation(std::string& outFail) {
    const MeshAsset quad = makeQuadMesh("fixture/mesh/quad", 1.0f, 1.0f, 0.0f);
    std::string error;
    if (!engine::assets::phlosion::validateMeshAsset(quad, {}, &error) ||
        !error.empty()) {
        outFail = "valid quad was rejected: " + error;
        return false;
    }

    MeshAsset emptyId = quad;
    emptyId.meshId.clear();
    if (!expectAssetRejected(emptyId, "mesh id must be non-empty", outFail)) {
        return false;
    }
    MeshAsset longId = quad;
    longId.meshId = std::string(1025u, 'x');
    if (!expectAssetRejected(longId, "mesh id exceeds", outFail)) {
        return false;
    }
    MeshAsset twoVertices = quad;
    twoVertices.vertices.resize(2u);
    if (!expectAssetRejected(
            twoVertices,
            "at least three vertices",
            outFail)) {
        return false;
    }
    MeshAsset notTriangles = quad;
    notTriangles.indices = {0u, 1u, 2u, 0u};
    if (!expectAssetRejected(
            notTriangles,
            "multiple of three",
            outFail)) {
        return false;
    }
    MeshAsset shortIndices = quad;
    shortIndices.indices = {0u, 1u};
    if (!expectAssetRejected(
            shortIndices,
            "at least one triangle",
            outFail)) {
        return false;
    }
    MeshAsset outOfRange = quad;
    outOfRange.indices[0] = 9u;
    if (!expectAssetRejected(
            outOfRange,
            "index 0 is out of range",
            outFail)) {
        return false;
    }
    MeshAsset nonFinite = quad;
    nonFinite.vertices[1].position[0] =
        std::numeric_limits<float>::quiet_NaN();
    if (!expectAssetRejected(
            nonFinite,
            "position must be finite",
            outFail)) {
        return false;
    }
    MeshAsset colour = quad;
    colour.vertices[2].color[1] = 1.5f;
    if (!expectAssetRejected(
            colour,
            "color channel must be inside [0, 1]",
            outFail)) {
        return false;
    }
    MeshAsset tangentW = quad;
    tangentW.vertices[3].tangent[3] = 0.5f;
    if (!expectAssetRejected(
            tangentW,
            "tangent w must be exactly -1 or +1",
            outFail)) {
        return false;
    }
    MeshAsset normal = quad;
    normal.vertices[0].normal = {0.0f, 0.0f, 0.0f};
    if (!expectAssetRejected(
            normal,
            "normal must have nonzero length",
            outFail)) {
        return false;
    }
    MeshAsset tangent = quad;
    tangent.vertices[0].tangent = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!expectAssetRejected(
            tangent,
            "tangent xyz must have nonzero length",
            outFail)) {
        return false;
    }
    MeshAsset noSlots = quad;
    noSlots.materialSlots.clear();
    if (!expectAssetRejected(
            noSlots,
            "at least one material slot",
            outFail)) {
        return false;
    }
    MeshAsset duplicateSlots = quad;
    duplicateSlots.materialSlots = {"surface", "surface"};
    if (!expectAssetRejected(
            duplicateSlots,
            "material slot label is duplicated",
            outFail)) {
        return false;
    }
    MeshAsset emptySlot = quad;
    emptySlot.materialSlots = {""};
    if (!expectAssetRejected(
            emptySlot,
            "material slot labels must be non-empty",
            outFail)) {
        return false;
    }
    MeshAsset noSubmeshes = quad;
    noSubmeshes.submeshes.clear();
    if (!expectAssetRejected(
            noSubmeshes,
            "at least one submesh",
            outFail)) {
        return false;
    }
    MeshAsset gap = makeMultiSubmeshMesh();
    gap.submeshes = {MeshSubmesh{0u, 3u, 0u}, MeshSubmesh{3u, 3u, 1u}};
    if (!expectAssetRejected(
            gap,
            "must cover the whole index list",
            outFail)) {
        return false;
    }
    MeshAsset unordered = makeMultiSubmeshMesh();
    unordered.submeshes = {
        MeshSubmesh{6u, 6u, 1u},
        MeshSubmesh{0u, 6u, 0u}};
    if (!expectAssetRejected(
            unordered,
            "partition the index list in order",
            outFail)) {
        return false;
    }
    MeshAsset emptySubmesh = makeMultiSubmeshMesh();
    emptySubmesh.submeshes = {MeshSubmesh{0u, 12u, 0u}, MeshSubmesh{12u, 0u, 1u}};
    if (!expectAssetRejected(
            emptySubmesh,
            "is empty",
            outFail)) {
        return false;
    }
    MeshAsset unaligned = makeMultiSubmeshMesh();
    unaligned.submeshes = {MeshSubmesh{0u, 2u, 0u}, MeshSubmesh{2u, 10u, 1u}};
    if (!expectAssetRejected(
            unaligned,
            "is not triangle aligned",
            outFail)) {
        return false;
    }
    MeshAsset badSlot = makeMultiSubmeshMesh();
    badSlot.submeshes[1].materialSlot = 7u;
    if (!expectAssetRejected(
            badSlot,
            "references an invalid material slot",
            outFail)) {
        return false;
    }
    MeshAsset overrun = makeMultiSubmeshMesh();
    overrun.submeshes = {MeshSubmesh{0u, 12u, 0u}, MeshSubmesh{12u, 6u, 1u}};
    if (!expectAssetRejected(
            overrun,
            "must not overlap or exceed the index list",
            outFail)) {
        return false;
    }
    return true;
}

bool caseUtf8Names(std::string& outFail) {
    const MeshAsset quad = makeQuadMesh("fixture/mesh/quad", 1.0f, 1.0f, 0.0f);

    // Valid multibyte names survive the round trip byte for byte.
    MeshAsset multibyte = quad;
    multibyte.meshId = std::string("fixture/mesh/") + "\xc3\xa9" +
        "\xe3\x83\x9c" + "\xf0\x9f\x98\x80";
    multibyte.materialSlots = {std::string("surface-") + "\xe2\x98\x83"};
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!engine::assets::phlosion::encodeMeshAsset(
            multibyte,
            bytes,
            {},
            &error)) {
        outFail = "valid multibyte mesh names failed to encode: " + error;
        return false;
    }
    MeshAsset decoded;
    if (!engine::assets::phlosion::decodeMeshAsset(
            bytes,
            decoded,
            {},
            &error)) {
        outFail = "valid multibyte mesh names failed to decode: " + error;
        return false;
    }
    if (decoded.meshId != multibyte.meshId ||
        decoded.materialSlots != multibyte.materialSlots) {
        outFail = "multibyte mesh names were replaced during the round trip";
        return false;
    }

    // The 1024-byte bound counts bytes, not code points.
    std::string fourByteCharacters;
    for (std::size_t index = 0u; index < 256u; ++index) {
        fourByteCharacters += "\xf0\x9f\x98\x80";
    }
    MeshAsset exact = quad;
    exact.meshId = fourByteCharacters;
    if (exact.meshId.size() != 1024u ||
        !engine::assets::phlosion::validateMeshAsset(exact, {}, &error) ||
        !error.empty()) {
        outFail = "a valid 1024-byte mesh id was rejected: " + error;
        return false;
    }
    MeshAsset overlong = quad;
    overlong.meshId = fourByteCharacters + "a";
    if (!expectAssetRejected(overlong, "mesh id exceeds", outFail)) {
        return false;
    }

    // Malformed sequences are rejected with a diagnostic; nothing is replaced.
    const char* const malformedNames[] = {
        "fixture/mesh/\xff",             // lone continuation-range byte
        "fixture/mesh/\x80",             // lone continuation byte
        "fixture/mesh/\xe2\x82",         // truncated three-byte sequence
        "fixture/mesh/\xc0\xaf",         // overlong two-byte sequence
        "fixture/mesh/\xed\xa0\x80",     // UTF-16 surrogate
        "fixture/mesh/\xf5\x80\x80\x80", // above U+10FFFF
        "fixture/mesh/\xf8\x88\x80\x80\x80"}; // five-byte sequence
    for (const char* const name : malformedNames) {
        MeshAsset malformed = quad;
        malformed.meshId = name;
        if (!expectAssetRejected(
                malformed,
                "mesh id must be valid UTF-8",
                outFail)) {
            return false;
        }
    }

    // Slot labels use the same rule and report the same way.
    MeshAsset malformedSlot = quad;
    malformedSlot.materialSlots = {
        "surface",
        std::string("\xf0\x9f")};
    if (!expectAssetRejected(
            malformedSlot,
            "mesh material slot label 1 must be valid UTF-8",
            outFail)) {
        return false;
    }

    // A container whose manifest carries invalid UTF-8 must fail through the
    // documented diagnostic path instead of escaping as an exception.
    std::vector<std::uint8_t> quadBytes;
    if (!engine::assets::phlosion::encodeMeshAsset(quad, quadBytes, {}, &error)) {
        outFail = "quad encode failed: " + error;
        return false;
    }
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(quadBytes, document, &error)) {
        outFail = "quad container decode failed: " + error;
        return false;
    }
    document.manifestJson =
        std::string("{\"mesh_id\":\"") + "\xff" + "\"}";
    std::vector<std::uint8_t> brokenBytes;
    if (!engine::assets::phrc::encode(document, brokenBytes, &error)) {
        outFail = "invalid UTF-8 manifest fixture encode failed: " + error;
        return false;
    }
    MeshAsset untouched = sentinelMesh();
    error.clear();
    if (engine::assets::phlosion::decodeMeshAsset(
            brokenBytes,
            untouched,
            {},
            &error) ||
        !isSentinel(untouched) ||
        error.find("mesh manifest is not valid JSON") == std::string::npos) {
        outFail =
            "a manifest with invalid UTF-8 did not fail with a diagnostic";
        return false;
    }
    return true;
}

bool caseLimits(std::string& outFail) {
    const MeshAsset quad = makeQuadMesh("fixture/mesh/quad", 1.0f, 1.0f, 0.0f);
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!engine::assets::phlosion::encodeMeshAsset(quad, bytes, {}, &error)) {
        outFail = "quad encode failed: " + error;
        return false;
    }

    MeshAssetLimits exact;
    exact.maxEncodedBytes = bytes.size();
    exact.maxVertices = quad.vertices.size();
    exact.maxIndices = quad.indices.size();
    exact.maxSubmeshes = quad.submeshes.size();
    exact.maxMaterialSlots = quad.materialSlots.size();
    MeshAsset decoded;
    if (!engine::assets::phlosion::decodeMeshAsset(bytes, decoded, exact, &error) ||
        !engine::assets::phlosion::validateMeshAsset(quad, exact, &error)) {
        outFail = "mesh limits rejected an exact-fit asset: " + error;
        return false;
    }

    MeshAssetLimits tooFewBytes = exact;
    tooFewBytes.maxEncodedBytes = bytes.size() - 1u;
    MeshAsset untouched = sentinelMesh();
    error.clear();
    if (engine::assets::phlosion::decodeMeshAsset(
            bytes,
            untouched,
            tooFewBytes,
            &error) ||
        !isSentinel(untouched) ||
        error.find("encoded byte limit") == std::string::npos) {
        outFail = "mesh encoded byte limit did not reject the document";
        return false;
    }
    // The encoded-byte cap is enforced while writing and reading a document;
    // authored validation has no encoded size to measure.
    std::vector<std::uint8_t> encoded = {0xAAu, 0xBBu};
    const std::vector<std::uint8_t> encodedBefore = encoded;
    error.clear();
    if (engine::assets::phlosion::encodeMeshAsset(
            quad,
            encoded,
            tooFewBytes,
            &error) ||
        encoded != encodedBefore ||
        error.find("encoded byte limit") == std::string::npos) {
        outFail = "mesh encode did not enforce the encoded byte limit";
        return false;
    }

    MeshAssetLimits tooFewVertices = {};
    tooFewVertices.maxVertices = 3u;
    if (!expectDocumentRejected(
            bytes,
            [](nlohmann::json&, engine::assets::phrc::Document&) {},
            "vertex count exceeds the limit",
            outFail,
            tooFewVertices)) {
        return false;
    }
    MeshAssetLimits tooFewIndices = {};
    tooFewIndices.maxIndices = 5u;
    if (!expectDocumentRejected(
            bytes,
            [](nlohmann::json&, engine::assets::phrc::Document&) {},
            "index count exceeds the limit",
            outFail,
            tooFewIndices)) {
        return false;
    }
    MeshAssetLimits tooFewSubmeshes = {};
    tooFewSubmeshes.maxSubmeshes = 0u;
    if (!expectDocumentRejected(
            bytes,
            [](nlohmann::json&, engine::assets::phrc::Document&) {},
            "submesh count exceeds the limit",
            outFail,
            tooFewSubmeshes)) {
        return false;
    }
    MeshAssetLimits tooFewSlots = {};
    tooFewSlots.maxMaterialSlots = 0u;
    if (!expectDocumentRejected(
            bytes,
            [](nlohmann::json&, engine::assets::phrc::Document&) {},
            "material slot count exceeds the limit",
            outFail,
            tooFewSlots)) {
        return false;
    }
    return true;
}

bool caseProviderLoading(std::string& outFail) {
    const MeshAsset quad = makeQuadMesh("fixture/mesh/quad", 1.0f, 1.0f, 0.0f);
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!engine::assets::phlosion::encodeMeshAsset(quad, bytes, {}, &error)) {
        outFail = "quad encode failed: " + error;
        return false;
    }
    InMemoryStore store;
    store.add("meshes/quad.phmesh", bytes);

    MeshAsset loaded;
    if (!engine::assets::phlosion::loadMeshAsset(
            store,
            "meshes/quad.phmesh",
            loaded,
            {},
            &error) ||
        !sameMesh(loaded, quad) || !error.empty()) {
        outFail = "mesh load through a provider failed: " + error;
        return false;
    }

    MeshAsset untouched = sentinelMesh();
    error.clear();
    if (engine::assets::phlosion::loadMeshAsset(
            store,
            "meshes/absent.phmesh",
            untouched,
            {},
            &error) ||
        !isSentinel(untouched) ||
        error.find("mesh read failed") == std::string::npos) {
        outFail = "a missing mesh asset did not fail with a read diagnostic";
        return false;
    }

    std::vector<std::uint8_t> corrupt = bytes;
    corrupt.back() ^= 0x40u;
    store.add("meshes/corrupt.phmesh", corrupt);
    error.clear();
    if (engine::assets::phlosion::loadMeshAsset(
            store,
            "meshes/corrupt.phmesh",
            untouched,
            {},
            &error) ||
        !isSentinel(untouched) ||
        error.find("mesh container decode failed") == std::string::npos) {
        outFail = "a corrupt mesh document was accepted";
        return false;
    }

    error.clear();
    if (engine::assets::phlosion::loadMeshAsset(
            store,
            "",
            untouched,
            {},
            &error) ||
        !isSentinel(untouched) ||
        error.find("virtual path must not be empty") == std::string::npos) {
        outFail = "an empty mesh virtual path was accepted";
        return false;
    }
    return true;
}

bool caseMalformedDocuments(std::string& outFail) {
    const MeshAsset quad = makeQuadMesh("fixture/mesh/quad", 1.0f, 1.0f, 0.0f);
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!engine::assets::phlosion::encodeMeshAsset(quad, bytes, {}, &error)) {
        outFail = "quad encode failed: " + error;
        return false;
    }

    const std::vector<std::pair<std::string, DocumentMutation>> cases = {
        {"mesh container magic is not PHMS",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.magic = engine::assets::phrc::magic("PHLO");
         }},
        {"mesh container schema version is unsupported",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.schemaVersion = 2u;
         }},
        {"mesh container flags must be zero",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.flags = 1u;
         }},
        {"mesh v1 must not declare dependencies",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.dependencies.push_back(engine::assets::phrc::Dependency{
                 "materials/surface", 0u, engine::assets::phrc::kDependencyRequired});
         }},
        {"mesh document requires exactly two chunks",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.chunks.pop_back();
         }},
        {"mesh document requires the VTX0 chunk first",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             std::swap(document.chunks[0], document.chunks[1]);
         }},
        {"mesh document requires the IDX0 chunk second",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.chunks[1].type = engine::assets::phrc::magic("VTX0");
         }},
        {"mesh document requires the VTX0 chunk first",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.chunks[0].type = engine::assets::phrc::magic("SKIN");
         }},
        {"mesh chunks must declare zero flags",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.chunks[0].flags = 2u;
         }},
        {"mesh chunks must declare 16-byte alignment",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.chunks[1].alignment = 8u;
         }},
        {"VTX0 byte count does not match the manifest vertex count",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.chunks[0].bytes.push_back(0u);
         }},
        {"IDX0 byte count does not match the manifest index count",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             document.chunks[1].bytes.resize(document.chunks[1].bytes.size() + 4u);
         }},
        {"manifest schema version is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["schema_version"] = 2;
         }},
        {"manifest root type is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["root_type"] = "SkinnedMesh";
         }},
        {"vertex layout is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["vertex_layout"] = "pntcb-f32-v1";
         }},
        {"coordinate system is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["coordinate_system"] = "left-handed-z-up";
         }},
        {"topology is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["topology"] = "triangle-strips";
         }},
        {"target profile is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["target_profile"] = "windows-d3d12";
         }},
        {"mesh.skin' is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["skin"] = {{"skeleton", "characters/demo"}};
         }},
        {"mesh.lods' is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["lods"] = nlohmann::json::array();
         }},
        {"mesh.counts.joint_count' is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["counts"]["joint_count"] = 0;
         }},
        {"mesh.bounds' is missing",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest.erase("bounds");
         }},
        {"mesh.submeshes[0].primitive' is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["submeshes"][0]["primitive"] = "strip";
         }},
        {"mesh.counts.vertices' must be an integer",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["counts"]["vertices"] = "4";
         }},
        {"mesh.counts.indices' must be an integer, not a fraction",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["counts"]["indices"] = 6.5;
         }},
        {"mesh.counts.submeshes' must be a nonnegative integer",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["counts"]["submeshes"] = -1;
         }},
        {"mesh.counts.material_slots' must be an integer",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["counts"]["material_slots"] = nlohmann::json(nullptr);
         }},
        {"vertex count exceeds the limit",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["counts"]["vertices"] = 4294967296ull;
         }},
        {"mesh.submeshes[0].first_index' exceeds uint32",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["submeshes"][0]["first_index"] = 4294967296ull;
         }},
        {"material slot count does not match the manifest counts",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["material_slots"] = nlohmann::json::array({"surface", "extra"});
         }},
        {"submesh count does not match the manifest counts",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["submeshes"] = nlohmann::json::array();
         }},
        {"mesh.mesh_id' must be a string",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["mesh_id"] = 7;
         }},
        {"mesh.bounds.min' must be an array of 3 numbers",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["bounds"]["min"] = nlohmann::json::array({0.0, 0.0});
         }},
        {"mesh.bounds.max[1]' must be a finite float32 value",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["bounds"]["max"][1] = 1.0e39;
         }},
        {"mesh.bounds.max[0]' must be a number",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["bounds"]["max"][0] = "1.0";
         }},
        {"mesh material slot labels must be non-empty",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["material_slots"] = nlohmann::json::array({""});
         }},
        {"material slot label is duplicated",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             const std::string label =
                 manifest["material_slots"][0].get<std::string>();
             manifest["material_slots"].push_back(label);
             manifest["counts"]["material_slots"] = 2;
         }},
        {"mesh id must be non-empty",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["mesh_id"] = "";
         }},
        {"mesh manifest root type is unsupported",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest.clear();
             manifest["root_type"] = "NotMesh";
         }},
        {"mesh vertex 0 position must be finite",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             const std::uint32_t bits = 0x7fc00000u;
             std::memcpy(document.chunks[0].bytes.data(), &bits, sizeof(bits));
         }},
        {"mesh vertex 1 color channel must be inside [0, 1]",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             const float value = 2.0f;
             std::memcpy(
                 document.chunks[0].bytes.data() + 64u + 48u,
                 &value,
                 sizeof(value));
         }},
        {"mesh vertex 2 tangent w must be exactly -1 or +1",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             const float value = 0.25f;
             std::memcpy(
                 document.chunks[0].bytes.data() + 128u + 36u,
                 &value,
                 sizeof(value));
         }},
        {"mesh vertex 2 normal must have nonzero length",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             std::fill(
                 document.chunks[0].bytes.begin() + 128u + 12u,
                 document.chunks[0].bytes.begin() + 128u + 24u,
                 0u);
         }},
        {"mesh vertex 0 tangent xyz must have nonzero length",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             std::fill(
                 document.chunks[0].bytes.begin() + 24u,
                 document.chunks[0].bytes.begin() + 36u,
                 0u);
         }},
        {"mesh index 1 is out of range",
         [](nlohmann::json&, engine::assets::phrc::Document& document) {
             std::uint32_t index = 9u;
             std::memcpy(
                 document.chunks[1].bytes.data() + 4u,
                 &index,
                 sizeof(index));
         }},
        {"mesh bounds do not match the vertex extents",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["bounds"]["max"][2] = 9.0;
         }},
        {"mesh submeshes must partition the index list in order",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             nlohmann::json record = {
                 {"first_index", 6},
                 {"index_count", 3},
                 {"material_slot", 0}};
             manifest["submeshes"] = nlohmann::json::array({record});
             manifest["counts"]["submeshes"] = 1;
         }},
        {"mesh submesh 0 references an invalid material slot",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["submeshes"][0]["material_slot"] = 3;
         }},
        {"mesh submesh 0 is not triangle aligned",
         [](nlohmann::json& manifest, engine::assets::phrc::Document&) {
             manifest["submeshes"][0]["index_count"] = 2;
         }}}
    ;

    for (const auto& [expectedText, mutation] : cases) {
        if (!expectDocumentRejected(
                bytes,
                mutation,
                expectedText,
                outFail)) {
            return false;
        }
    }

    // A container whose manifest is not JSON at all is rejected without
    // touching the caller's output.
    engine::assets::phrc::Document invalidManifest;
    if (!engine::assets::phrc::decode(bytes, invalidManifest, &error)) {
        outFail = "mesh container redecode failed: " + error;
        return false;
    }
    invalidManifest.manifestJson = "{";
    std::vector<std::uint8_t> invalidManifestBytes;
    if (!engine::assets::phrc::encode(
            invalidManifest,
            invalidManifestBytes,
            &error)) {
        outFail = "invalid manifest fixture encode failed: " + error;
        return false;
    }
    MeshAsset untouched = sentinelMesh();
    error.clear();
    if (engine::assets::phlosion::decodeMeshAsset(
            invalidManifestBytes,
            untouched,
            {},
            &error) ||
        !isSentinel(untouched) ||
        error.find("mesh manifest is not valid JSON") == std::string::npos) {
        outFail = "a mesh document with an invalid manifest was accepted";
        return false;
    }

    // Truncated and trailing-byte containers fail the shared PHRC integrity
    // check before any mesh semantics are considered.
    for (const std::vector<std::uint8_t>& broken :
         {std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 64),
          std::vector<std::uint8_t>{},
          std::vector<std::uint8_t>{'n', 'o', 't', ' ', 'a', ' ', 'm', 'e', 's', 'h'}}) {
        error.clear();
        if (engine::assets::phlosion::decodeMeshAsset(
                broken,
                untouched,
                {},
                &error) ||
            !isSentinel(untouched) ||
            error.find("mesh container decode failed") == std::string::npos) {
            outFail = "a truncated or non-container mesh document was accepted";
            return false;
        }
    }
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0u);
    error.clear();
    if (engine::assets::phlosion::decodeMeshAsset(trailing, untouched, {}, &error) ||
        !isSentinel(untouched)) {
        outFail = "a mesh document with trailing bytes was accepted";
        return false;
    }
    return true;
}

} // namespace

bool test_phlosion_mesh_asset_contract(std::string& outFail) {
    outFail.clear();
    if (!caseDeterministicRoundTrip(outFail)) {
        return false;
    }
    if (!caseGoldenPayload(outFail)) {
        return false;
    }
    if (!caseAuthoredValidation(outFail)) {
        return false;
    }
    if (!caseUtf8Names(outFail)) {
        return false;
    }
    if (!caseLimits(outFail)) {
        return false;
    }
    if (!caseProviderLoading(outFail)) {
        return false;
    }
    if (!caseMalformedDocuments(outFail)) {
        return false;
    }
    return true;
}
