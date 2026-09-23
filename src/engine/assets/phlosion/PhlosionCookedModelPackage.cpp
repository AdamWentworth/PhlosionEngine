#include "engine/assets/phlosion/PhlosionCookedModelPackage.h"
#include "engine/assets/phlosion/PhlosionBinaryCodec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::assets::phlosion {
namespace {

namespace fs = std::filesystem;
constexpr std::uint32_t kOldestSchema = 1u;
constexpr std::uint32_t kNewestSchema = 3u;
constexpr std::uint64_t kMaxResourceBytes = 128ull * 1024ull * 1024ull;

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

bool safeChildId(const std::string& id) {
    if (id.empty()) return false;
    const fs::path path(id);
    if (path.is_absolute() || path.has_root_name() ||
        path.has_root_directory()) return false;
    for (const auto& component : path) {
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
    }
    return true;
}

bool readDocument(
    const fs::path& path,
    std::string_view expectedMagic,
    phrc::Document& out,
    std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return fail(error, "Could not open cooked model resource: " + path.string());
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > kMaxResourceBytes) {
        return fail(error, "Cooked model resource exceeds the size limit: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), length);
    }
    if (!input || !phrc::decode(bytes, out, error)) {
        if (input) return false;
        return fail(error, "Could not read cooked model resource: " + path.string());
    }
    if (out.magic != phrc::magic(expectedMagic) ||
        out.schemaVersion < kOldestSchema ||
        out.schemaVersion > kNewestSchema) {
        return fail(error, "Cooked model resource has an unexpected type or schema: " + path.string());
    }
    if (!phrc::findChunk(out, "DATA") && expectedMagic != "PHLO") {
        return fail(error, "Cooked model resource has no DATA chunk: " + path.string());
    }
    return true;
}

bool loadChild(
    const fs::path& directory,
    const phrc::Document& root,
    const std::string& id,
    std::string_view magic,
    phrc::Document& out,
    std::string* error) {
    if (!safeChildId(id)) {
        return fail(error, "Cooked model child has an unsafe asset ID: " + id);
    }
    const auto dependency = std::find_if(
        root.dependencies.begin(), root.dependencies.end(),
        [&](const phrc::Dependency& item) { return item.assetId == id; });
    if (dependency == root.dependencies.end()) {
        return fail(error, "Cooked model child is absent from PHLO dependencies: " + id);
    }
    std::error_code ec;
    const fs::path resolvedDirectory = fs::weakly_canonical(directory, ec);
    if (ec) return fail(error, "Could not resolve cooked model directory: " + ec.message());
    const fs::path resolvedChild = fs::weakly_canonical(directory / id, ec);
    const fs::path relative = resolvedChild.lexically_relative(resolvedDirectory);
    if (ec || relative.empty() || relative.is_absolute() ||
        *relative.begin() == "..") {
        return fail(error, "Cooked model child escapes the object directory: " + id);
    }
    if (!readDocument(resolvedChild, magic, out, error)) return false;
    if (dependency->expectedContentHash != out.contentHash) {
        return fail(error, "Cooked model dependency hash mismatch: " + id);
    }
    return true;
}

bool readClipHeaders(
    const phrc::Document& animation,
    std::uint32_t expectedCount,
    std::vector<CookedModelClip>& out,
    std::string* error) {
    const phrc::Chunk* chunk = phrc::findChunk(animation, "DATA");
    if (!chunk) return fail(error, "PHAN has no DATA chunk.");
    phrc::BinaryReader reader(chunk->bytes);
    std::uint32_t count = 0u;
    if (!reader.u32(count) || count != expectedCount || count > 16'384u) {
        return fail(error, "PHAN clip count differs from the PHLO manifest.");
    }
    out.clear();
    out.reserve(count);
    for (std::uint32_t clip = 0u; clip < count; ++clip) {
        CookedModelClip header;
        std::uint32_t samplerCount = 0u;
        if (!reader.string(header.name, 4096u) ||
            !reader.f32(header.durationSeconds) ||
            !std::isfinite(header.durationSeconds) ||
            header.durationSeconds < 0.0f ||
            !reader.u32(samplerCount) || samplerCount > 1'000'000u) {
            return fail(error, "PHAN clip header is invalid.");
        }
        for (std::uint32_t sampler = 0u; sampler < samplerCount; ++sampler) {
            std::uint32_t inputCount = 0u;
            std::uint32_t outputCount = 0u;
            std::string interpolation;
            std::uint8_t isVec4 = 0u;
            if (!reader.u32(inputCount) ||
                inputCount > reader.remaining() / 4u ||
                !reader.skip(static_cast<std::size_t>(inputCount) * 4u) ||
                !reader.u32(outputCount) ||
                outputCount > reader.remaining() / 16u ||
                !reader.skip(static_cast<std::size_t>(outputCount) * 16u) ||
                !reader.string(interpolation, 64u) ||
                !reader.u8(isVec4) || isVec4 > 1u) {
                return fail(error, "PHAN sampler payload is invalid.");
            }
        }
        std::uint32_t channelCount = 0u;
        if (!reader.u32(channelCount) || channelCount > 1'000'000u ||
            channelCount > reader.remaining() / 9u ||
            !reader.skip(static_cast<std::size_t>(channelCount) * 9u)) {
            return fail(error, "PHAN channel payload is invalid.");
        }
        out.push_back(std::move(header));
    }
    if (!reader.finished()) {
        return fail(error, "PHAN payload has trailing data.");
    }
    return true;
}

} // namespace

bool loadCookedModelPackage(
    const fs::path& phloPath,
    CookedModelPackage& outPackage,
    std::string* outError) {
    CookedModelPackage candidate;
    if (!readDocument(phloPath, "PHLO", candidate.root, outError)) return false;
    try {
        const auto manifest = nlohmann::json::parse(candidate.root.manifestJson);
        const auto schema = manifest.at("schema_version").get<std::uint32_t>();
        if (manifest.at("root_type") != "Prefab" ||
            schema < kOldestSchema || schema > kNewestSchema ||
            schema != candidate.root.schemaVersion) {
            return fail(outError, "PHLO prefab manifest has an unsupported schema.");
        }
        candidate.objectId = manifest.at("object_id").get<std::string>();
        candidate.prefabKind = manifest.at("prefab_kind").get<std::string>();
        if (candidate.objectId.empty() || candidate.prefabKind.empty()) {
            return fail(outError, "PHLO prefab identity is empty.");
        }
        const auto& counts = manifest.at("counts");
        candidate.vertexCount = counts.at("vertices").get<std::uint32_t>();
        candidate.indexCount = counts.at("indices").get<std::uint32_t>();
        candidate.submeshCount = counts.at("submeshes").get<std::uint32_t>();
        candidate.nodeCount = counts.at("nodes").get<std::uint32_t>();
        candidate.skinCount = counts.at("skins").get<std::uint32_t>();
        candidate.animationCount = counts.at("animations").get<std::uint32_t>();
        const auto& resources = manifest.at("resources");
        const fs::path directory = phloPath.parent_path();
        const std::array<std::pair<const char*, const char*>, 4> children{{
            {"mesh", "PHME"}, {"skeleton", "PHSK"},
            {"animations", "PHAN"}, {"materials", "PHMT"}}};
        std::array<phrc::Document*, 4> outputs{{
            &candidate.mesh, &candidate.skeleton,
            &candidate.animations, &candidate.materials}};
        for (std::size_t index = 0; index < children.size(); ++index) {
            if (!loadChild(
                    directory, candidate.root,
                    resources.at(children[index].first).get<std::string>(),
                    children[index].second, *outputs[index], outError)) {
                return false;
            }
        }
        if (!readClipHeaders(
                candidate.animations, candidate.animationCount,
                candidate.clips, outError)) return false;
    } catch (const std::exception& exception) {
        return fail(outError, "Invalid PHLO prefab manifest: " + std::string(exception.what()));
    }
    outPackage = std::move(candidate);
    if (outError) outError->clear();
    return true;
}

} // namespace engine::assets::phlosion
