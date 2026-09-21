#include "MaterialFixtureAssets.h"

#include "engine/core/Environment.h"
#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfMaterial.h"
#include "engine/render/gltf/ModelFastGltfTextures.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace engine::material_fixture {
namespace {

using engine::render::gltf::model::CPUTexture;
using engine::render::gltf::model::decodeBaseColorTextureFast;
using engine::render::gltf::model::decodeEmissiveTextureFast;
using engine::render::gltf::model::decodeMetallicRoughnessTextureFast;
using engine::render::gltf::model::decodeNormalTextureFast;
using engine::render::gltf::model::decodeOcclusionTextureFast;
using engine::render::gltf::model::resolveMaterialRenderInfo;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

constexpr int kAuthoredMapSize = 8;

// The authored five-map set. Every role map differs from the others, and every
// map differs across its own vertical centre line, so role identity and spatial
// variation are both measurable. The metallic-roughness map keeps green and
// blue apart in both halves and swaps which one is larger, so swapped
// roughness/metallic routing cannot pass.
const std::array<AuthoredRoleMap, kFixtureRoleCount> kRoleMaps{{
    {FixtureRole::BaseColor,
     "base_color.png",
     kAuthoredMapSize,
     kAuthoredMapSize,
     1u,
     {200u, 60u, 60u, 255u},
     {70u, 120u, 200u, 255u}},
    {FixtureRole::Emissive,
     "emissive.png",
     kAuthoredMapSize,
     kAuthoredMapSize,
     1u,
     {240u, 140u, 40u, 255u},
     {40u, 140u, 240u, 255u}},
    {FixtureRole::Normal,
     "normal.png",
     kAuthoredMapSize,
     kAuthoredMapSize,
     0u,
     {180u, 90u, 220u, 255u},
     {70u, 150u, 210u, 255u}},
    {FixtureRole::MetallicRoughness,
     "metallic_roughness.png",
     kAuthoredMapSize,
     kAuthoredMapSize,
     0u,
     {200u, 64u, 192u, 255u},
     {120u, 208u, 32u, 255u}},
    {FixtureRole::Occlusion,
     "occlusion.png",
     kAuthoredMapSize,
     kAuthoredMapSize,
     0u,
     {96u, 96u, 96u, 255u},
     {224u, 224u, 224u, 255u}},
}};

std::string floatText(float value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
    return std::string(buffer);
}

std::string quote(const std::string& value) {
    std::string out = "\"";
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            out.push_back('\\');
        }
        out.push_back(character);
    }
    out.push_back('"');
    return out;
}

std::string factorsJson(const FixtureMaterialFactors& factors) {
    std::ostringstream stream;
    stream << "        \"metallicFactor\": "
           << floatText(factors.metallicFactor) << ",\n"
           << "        \"roughnessFactor\": "
           << floatText(factors.roughnessFactor);
    return stream.str();
}

std::string emissiveFactorJson(const FixtureMaterialFactors& factors) {
    return "[" + floatText(factors.emissiveFactor[0]) + ", " +
        floatText(factors.emissiveFactor[1]) + ", " +
        floatText(factors.emissiveFactor[2]) + "]";
}

std::string mapsEnvelope(const std::string& materialName,
                         const FixtureMaterialFactors& factors,
                         const std::string& imagesJson,
                         const std::string& bufferJson) {
    std::string json = "{\n  \"asset\": { \"version\": \"2.0\" },\n";
    json += bufferJson;
    json += imagesJson;
    json += "  \"textures\": [ { \"source\": 0 }, { \"source\": 1 }, "
            "{ \"source\": 2 }, { \"source\": 3 }, { \"source\": 4 } ],\n";
    json += "  \"materials\": [\n    {\n      \"name\": " + quote(materialName) +
        ",\n";
    json += "      \"pbrMetallicRoughness\": {\n";
    json += "        \"baseColorTexture\": { \"index\": 0 },\n";
    json += "        \"metallicRoughnessTexture\": { \"index\": 3 },\n";
    json += "        \"baseColorFactor\": [1.0, 1.0, 1.0, 1.0],\n";
    json += factorsJson(factors) + "\n      },\n";
    json += "      \"normalTexture\": { \"index\": 2, \"scale\": " +
        floatText(factors.normalScale) + " },\n";
    json += "      \"occlusionTexture\": { \"index\": 4, \"strength\": " +
        floatText(factors.occlusionStrength) + " },\n";
    json += "      \"emissiveTexture\": { \"index\": 1 },\n";
    json += "      \"emissiveFactor\": " + emissiveFactorJson(factors) + "\n";
    json += "    }\n  ]\n}\n";
    return json;
}

std::string authoredMapsDocument() {
    const FixtureMaterialFactors factors =
        fixtureMaterialFactors(FixtureCase::AuthoredMaps);
    std::string images = "  \"images\": [\n";
    for (std::size_t index = 0u; index < kRoleMaps.size(); ++index) {
        const std::string name(kRoleMaps[index].fileName);
        images += "    { \"name\": " + quote(name) + ", \"uri\": " + quote(name) +
            " }";
        images += index + 1u < kRoleMaps.size() ? ",\n" : "\n";
    }
    images += "  ],\n";
    return mapsEnvelope("AuthoredMaps", factors, images, std::string{});
}

std::string emptyImageBytesDocument() {
    const FixtureMaterialFactors factors =
        fixtureMaterialFactors(FixtureCase::EmptyImageBytes);
    std::string buffer =
        "  \"buffers\": [ { \"uri\": \"empty_input.bin\", \"byteLength\": 8 } ],\n";
    buffer += "  \"bufferViews\": [\n";
    for (int index = 0; index < kFixtureRoleCount; ++index) {
        buffer += "    { \"buffer\": 0, \"byteOffset\": " +
            std::to_string(index) + ", \"byteLength\": 0 }";
        buffer += index + 1 < kFixtureRoleCount ? ",\n" : "\n";
    }
    buffer += "  ],\n";
    std::string images = "  \"images\": [\n";
    for (int index = 0; index < kFixtureRoleCount; ++index) {
        images += "    { \"name\": \"empty_" +
            std::string(fixtureRoleName(static_cast<FixtureRole>(index))) +
            "\", \"bufferView\": " + std::to_string(index) +
            ", \"mimeType\": \"image/png\" }";
        images += index + 1 < kFixtureRoleCount ? ",\n" : "\n";
    }
    images += "  ],\n";
    return mapsEnvelope("EmptyImageBytes", factors, images, buffer);
}

std::string absentMapsDocument() {
    const FixtureMaterialFactors factors =
        fixtureMaterialFactors(FixtureCase::AbsentMaps);
    std::string json = "{\n  \"asset\": { \"version\": \"2.0\" },\n";
    json += "  \"materials\": [\n    {\n      \"name\": \"AbsentMaps\",\n";
    json += "      \"pbrMetallicRoughness\": {\n";
    json += "        \"baseColorFactor\": [1.0, 1.0, 1.0, 1.0],\n";
    json += factorsJson(factors) + "\n      },\n";
    json += "      \"emissiveFactor\": " + emissiveFactorJson(factors) + "\n";
    json += "    }\n  ]\n}\n";
    return json;
}

bool writeBytes(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes,
                std::string* outError) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return fail(outError,
                    "could not open fixture input for writing: " + path.string());
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        return fail(outError, "could not write fixture input: " + path.string());
    }
    return true;
}

bool writeText(const std::filesystem::path& path,
               const std::string& text,
               std::string* outError) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return fail(outError,
                    "could not open fixture document for writing: " + path.string());
    }
    output << text;
    if (!output) {
        return fail(outError, "could not write fixture document: " + path.string());
    }
    return true;
}

// Process-local scoped override for the decoder's diagnostic environment. The
// decoder can write debug PNGs relative to the process working directory and
// can add logging when these flags are set, so the sample keeps them off for the
// duration of a decode and restores whatever the caller had on every exit path.
// This is not a global state change: only this process's environment is touched,
// and only for the scope below.
class ScopedDecoderDiagnostics {
public:
    ScopedDecoderDiagnostics() {
        for (std::size_t index = 0u; index < kNames.size(); ++index) {
            const char* name = kNames[index];
            const std::optional<std::string> previous = engine::env::get(name);
            previous_[index] = previous;
            applied_[index] = set(name, "0");
        }
    }

    ScopedDecoderDiagnostics(const ScopedDecoderDiagnostics&) = delete;
    ScopedDecoderDiagnostics& operator=(const ScopedDecoderDiagnostics&) = delete;

    ~ScopedDecoderDiagnostics() {
        for (std::size_t index = 0u; index < kNames.size(); ++index) {
            if (!applied_[index]) {
                continue;
            }
            if (previous_[index].has_value()) {
                set(kNames[index], previous_[index]->c_str());
            } else {
                set(kNames[index], nullptr);
            }
        }
    }

private:
    static bool set(const char* name, const char* value) {
#if defined(_MSC_VER)
        return _putenv_s(name, value != nullptr ? value : "") == 0;
#else
        if (value == nullptr) {
            return ::unsetenv(name) == 0;
        }
        return ::setenv(name, value, 1) == 0;
#endif
    }

    static constexpr std::array<const char*, 3> kNames{
        "PHLOSION_GLTF_DEBUG_ALL",
        "PHLOSION_GLTF_DEBUG_MATCH",
        "PHLOSION_GLTF_DUMP_TEXTURES"};

    std::array<std::optional<std::string>, kNames.size()> previous_{};
    std::array<bool, kNames.size()> applied_{};
};

CPUTexture decodeRole(FixtureRole role,
                      const fastgltf::Asset& asset,
                      const std::filesystem::path& baseDir,
                      int materialIndex,
                      const std::string& modelPath,
                      int* outTexCoordIndex) {
    switch (role) {
        case FixtureRole::BaseColor:
            return decodeBaseColorTextureFast(asset, baseDir, materialIndex, false,
                                              modelPath, outTexCoordIndex);
        case FixtureRole::Emissive:
            return decodeEmissiveTextureFast(asset, baseDir, materialIndex, false,
                                             modelPath, outTexCoordIndex);
        case FixtureRole::Normal:
            return decodeNormalTextureFast(asset, baseDir, materialIndex, false,
                                           modelPath, outTexCoordIndex);
        case FixtureRole::MetallicRoughness:
            return decodeMetallicRoughnessTextureFast(asset, baseDir, materialIndex,
                                                      false, modelPath,
                                                      outTexCoordIndex);
        case FixtureRole::Occlusion:
            return decodeOcclusionTextureFast(asset, baseDir, materialIndex, false,
                                              modelPath, outTexCoordIndex);
        default:
            return CPUTexture{};
    }
}

bool validStagingPrefix(const std::string& prefix) {
    if (prefix.empty() || prefix.size() > 64u) {
        return false;
    }
    return std::all_of(prefix.begin(), prefix.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' || character == '_';
    });
}

std::string stagingToken() {
    static std::mt19937_64 generator([] {
        std::random_device device;
        std::uint64_t seed = (static_cast<std::uint64_t>(device()) << 32u) ^
            static_cast<std::uint64_t>(device());
        seed ^= static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return seed;
    }());
    char buffer[32] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%016llx",
                  static_cast<unsigned long long>(generator()));
    return std::string(buffer);
}

} // namespace

const char* fixtureRoleName(FixtureRole role) {
    switch (role) {
        case FixtureRole::BaseColor: return "baseColor";
        case FixtureRole::Emissive: return "emissive";
        case FixtureRole::Normal: return "normal";
        case FixtureRole::MetallicRoughness: return "metallicRoughness";
        case FixtureRole::Occlusion: return "occlusion";
        default: return "unknown";
    }
}

std::optional<FixtureRole> parseFixtureRole(const std::string& name) {
    for (int index = 0; index < kFixtureRoleCount; ++index) {
        const FixtureRole role = static_cast<FixtureRole>(index);
        if (name == fixtureRoleName(role)) {
            return role;
        }
    }
    return std::nullopt;
}

const std::array<AuthoredRoleMap, kFixtureRoleCount>& authoredRoleMaps() {
    return kRoleMaps;
}

const AuthoredRoleMap& authoredRoleMap(FixtureRole role) {
    for (const AuthoredRoleMap& map : kRoleMaps) {
        if (map.role == role) {
            return map;
        }
    }
    return kRoleMaps[0];
}

std::vector<std::uint8_t> authoredRolePixels(FixtureRole role) {
    const AuthoredRoleMap& map = authoredRoleMap(role);
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(map.width) *
            static_cast<std::size_t>(map.height) * 4u,
        0u);
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const std::array<std::uint8_t, 4>& half =
                x * 2 < map.width ? map.left : map.right;
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(map.width) +
                 static_cast<std::size_t>(x)) *
                4u;
            for (std::size_t channel = 0u; channel < 4u; ++channel) {
                pixels[offset + channel] = half[channel];
            }
        }
    }
    return pixels;
}

std::string authoredGltfDocument(FixtureCase fixtureCase) {
    switch (fixtureCase) {
        case FixtureCase::AuthoredMaps: return authoredMapsDocument();
        case FixtureCase::EmptyImageBytes: return emptyImageBytesDocument();
        case FixtureCase::AbsentMaps:
        default: return absentMapsDocument();
    }
}

FixtureStagingDirectory::FixtureStagingDirectory(std::string prefix) {
    if (!validStagingPrefix(prefix)) {
        error_ = "staging directory prefix must be one safe path component";
        return;
    }
    const std::filesystem::path root = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::filesystem::path candidate =
            root / (prefix + "-" + stagingToken());
        std::error_code error;
        // create_directory only reports success for the caller that actually
        // created the directory, so ownership is established atomically.
        if (std::filesystem::create_directory(candidate, error)) {
            path_ = candidate;
            return;
        }
        if (error && error != std::errc::file_exists) {
            error_ = "could not create a staging directory: " + error.message();
            return;
        }
    }
    error_ = "no free staging directory name was available";
}

FixtureStagingDirectory::~FixtureStagingDirectory() {
    if (path_.empty()) {
        return;
    }
    // Removes only the directory this instance created; never a glob.
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

FixtureStagingDirectory::FixtureStagingDirectory(
    FixtureStagingDirectory&& other) noexcept
    : path_(std::move(other.path_)), error_(std::move(other.error_)) {
    other.abandonOwnership();
}

FixtureStagingDirectory& FixtureStagingDirectory::operator=(
    FixtureStagingDirectory&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (!path_.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    path_ = std::move(other.path_);
    error_ = std::move(other.error_);
    other.abandonOwnership();
    return *this;
}

void FixtureStagingDirectory::abandonOwnership() noexcept {
    path_.clear();
}

std::filesystem::path FixtureStagingDirectory::file(
    const std::string& name) const {
    if (path_.empty() || name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        return {};
    }
    return path_ / name;
}

bool FixtureStagingDirectory::contains(
    const std::filesystem::path& candidate) const {
    if (path_.empty() || candidate.empty()) {
        return false;
    }
    const std::filesystem::path owned = path_.lexically_normal();
    const std::filesystem::path candidateNormal = candidate.lexically_normal();
    auto ownedPart = owned.begin();
    auto candidatePart = candidateNormal.begin();
    while (ownedPart != owned.end()) {
        if (candidatePart == candidateNormal.end() || *ownedPart != *candidatePart) {
            return false;
        }
        ++ownedPart;
        ++candidatePart;
    }
    return true;
}

bool stageFixtureAssets(FixtureCase fixtureCase,
                        const std::filesystem::path& directory,
                        std::string* outError) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return fail(outError,
                    "staging directory does not exist or is not a directory: " +
                        directory.string());
    }
    if (fixtureCase == FixtureCase::AuthoredMaps) {
        for (const AuthoredRoleMap& map : kRoleMaps) {
            // Genuine PNG payloads written by the existing stb_image_write
            // dependency; the encoder's return value is checked.
            const std::vector<std::uint8_t> pixels = authoredRolePixels(map.role);
            const int wrote = stbi_write_png((directory / map.fileName).string().c_str(),
                                             map.width,
                                             map.height,
                                             4,
                                             pixels.data(),
                                             map.width * 4);
            if (wrote == 0) {
                return fail(outError,
                            std::string("the PNG writer failed for role ") +
                                fixtureRoleName(map.role));
            }
        }
    } else if (fixtureCase == FixtureCase::EmptyImageBytes) {
        // Five zero-length bufferViews inside one external buffer file. This is
        // a bounded malformed-input case, not a valid glTF asset.
        const std::vector<std::uint8_t> emptyInput(8u, 0u);
        if (!writeBytes(directory / "empty_input.bin", emptyInput, outError)) {
            return false;
        }
    }
    if (!writeText(directory / "material_fixture.gltf",
                   authoredGltfDocument(fixtureCase),
                   outError)) {
        return false;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

FixtureMaterialFactors fixtureMaterialFactors(FixtureCase fixtureCase) {
    FixtureMaterialFactors factors;
    factors.emissiveFactor = fixtureCaseInfo(fixtureCase).emissiveFactor;
    if (fixtureCase == FixtureCase::AuthoredMaps) {
        factors.metallicFactor = 0.6f;
        factors.roughnessFactor = 0.8f;
        factors.normalScale = 1.25f;
        factors.occlusionStrength = 0.5f;
    }
    return factors;
}

const char* fixtureRoleOrigin(FixtureCase fixtureCase, FixtureRole role) {
    if (fixtureCase == FixtureCase::EmptyImageBytes) {
        return "empty-image-bytes";
    }
    if (fixtureCase == FixtureCase::AbsentMaps) {
        return role == FixtureRole::BaseColor ? "base-color-factor"
                                             : "role-fallback";
    }
    return "authored-map";
}

const FixtureRoleTexture& FixtureTextureSet::role(FixtureRole role) const {
    for (const FixtureRoleTexture& entry : roles) {
        if (entry.role == role) {
            return entry;
        }
    }
    return roles.at(0);
}

std::uint32_t FixtureTextureSet::presenceFlags() const {
    std::uint32_t flags = 0u;
    if (role(FixtureRole::Normal).cpu.width > 0u &&
        role(FixtureRole::Normal).cpu.height > 0u) {
        flags |= 1u << 0u;
    }
    if (role(FixtureRole::MetallicRoughness).cpu.width > 0u &&
        role(FixtureRole::MetallicRoughness).cpu.height > 0u) {
        flags |= 1u << 1u;
    }
    if (role(FixtureRole::Occlusion).cpu.width > 0u &&
        role(FixtureRole::Occlusion).cpu.height > 0u) {
        flags |= 1u << 2u;
    }
    if (role(FixtureRole::Emissive).cpu.width > 0u &&
        role(FixtureRole::Emissive).cpu.height > 0u) {
        flags |= 1u << 3u;
    }
    return flags;
}

std::filesystem::path FixtureTextureSet::assetDirectory() const {
    return staging ? staging->path() : std::filesystem::path{};
}

std::uint32_t fixtureContentHash(FixtureCase fixtureCase,
                                 FixtureRole role,
                                 const CPUTexture& texture) {
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash](std::uint8_t byte) {
        hash ^= static_cast<std::uint32_t>(byte);
        hash *= 16777619u;
    };
    for (const char character : std::string(fixtureCaseName(fixtureCase))) {
        mix(static_cast<std::uint8_t>(character));
    }
    for (const char character : std::string(fixtureRoleName(role))) {
        mix(static_cast<std::uint8_t>(character));
    }
    for (const std::uint8_t byte : texture.rgba) {
        mix(byte);
    }
    return hash;
}

bool decodeFixtureTextures(FixtureCase fixtureCase,
                           FixtureTextureSet& outTextures,
                           std::string* outError) {
    outTextures = FixtureTextureSet{};
    const FixtureCaseInfo& info = fixtureCaseInfo(fixtureCase);
    auto staging = std::make_shared<FixtureStagingDirectory>(
        std::string("phlosion-material-fixture-") + info.name);
    if (!staging->valid()) {
        // The owner's destructor removes whatever it reserved; a failed
        // reservation leaves nothing behind.
        return fail(outError, "could not reserve a staging directory: " +
                                  staging->error());
    }
    if (!stageFixtureAssets(fixtureCase, staging->path(), outError)) {
        // The shared owner releases the partially staged directory here.
        return false;
    }
    const std::filesystem::path documentPath =
        staging->file("material_fixture.gltf");
    if (documentPath.empty()) {
        return fail(outError, "the staging directory has no fixture document");
    }

    // The decoder's diagnostic flags are process-local and would otherwise let
    // an ambient setting write dumps relative to the caller's working directory.
    const ScopedDecoderDiagnostics diagnostics;

    const auto loaded =
        engine::render::gltf::loader::tryLoad(documentPath.string());
    if (!loaded.has_value()) {
        return fail(outError, "the material fixture glTF did not load: " +
                                  documentPath.string());
    }
    const fastgltf::Asset& asset = loaded->asset;
    if (asset.materials.empty()) {
        return fail(outError, "the material fixture glTF has no material");
    }
    constexpr int kMaterialIndex = 0;
    const std::string modelPath = documentPath.filename().string();

    outTextures.fixtureCase = fixtureCase;
    outTextures.roles.clear();
    outTextures.roles.reserve(static_cast<std::size_t>(kFixtureRoleCount));
    for (int index = 0; index < kFixtureRoleCount; ++index) {
        const FixtureRole role = static_cast<FixtureRole>(index);
        FixtureRoleTexture entry;
        entry.role = role;
        entry.roleName = fixtureRoleName(role);
        entry.cpu = decodeRole(role,
                               asset,
                               loaded->baseDir,
                               kMaterialIndex,
                               modelPath,
                               nullptr);
        entry.textureSrgb = authoredRoleMap(role).textureSrgb;
        entry.contentHash = fixtureContentHash(fixtureCase, role, entry.cpu);
        entry.key = std::string("fixture/mat/") + info.name + "/" + entry.roleName +
            "/" + std::to_string(entry.contentHash);
        entry.cacheKey = entry.key;
        outTextures.roles.push_back(std::move(entry));
    }

    const ::Model::CPUTexture& basePixels =
        outTextures.role(FixtureRole::BaseColor).cpu;
    const auto resolved =
        resolveMaterialRenderInfo(asset, kMaterialIndex, basePixels, false);
    outTextures.factors.metallicFactor = resolved.metallicFactor;
    outTextures.factors.roughnessFactor = resolved.roughnessFactor;
    outTextures.factors.normalScale = resolved.normalScale;
    outTextures.factors.occlusionStrength = resolved.occlusionStrength;
    outTextures.factors.emissiveFactor = {resolved.emissiveFactor.x,
                                          resolved.emissiveFactor.y,
                                          resolved.emissiveFactor.z};
    outTextures.factors.alphaMode = static_cast<std::uint8_t>(resolved.alphaMode);
    // Ownership of the staged inputs moves to the returned texture set; it is
    // released when the last copy of the set goes away.
    outTextures.staging = std::move(staging);
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace engine::material_fixture
