#pragma once

// Shared helpers for the glTF import CPU contracts
// (tests/TestGltfSceneImport.cpp and tests/TestGltfMaterialImport.cpp).
//
// These contracts author a tiny public glTF file plus explicit little-endian
// binary data in a uniquely owned temporary directory, load it through the
// production `engine::render::gltf::loader::tryLoad` boundary and inspect
// production importer output. Nothing here reimplements glTF parsing or
// importer math, and no downloaded or private asset is involved.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gltf_import_contract {

inline bool expect(bool condition, std::string message, std::string& outFail) {
    if (!condition) {
        outFail = std::move(message);
        return false;
    }
    return true;
}

inline std::string floatText(float value) {
    std::ostringstream stream;
    stream.precision(9);
    stream << value;
    return stream.str();
}

inline std::string vecText(const glm::vec3& value) {
    return "(" + floatText(value.x) + ", " + floatText(value.y) + ", " +
           floatText(value.z) + ")";
}

inline std::string vecText(const glm::vec4& value) {
    return "(" + floatText(value.x) + ", " + floatText(value.y) + ", " +
           floatText(value.z) + ", " + floatText(value.w) + ")";
}

// glTF stores rotations as (x, y, z, w); glm::quat exposes the same fields even
// though its constructor takes (w, x, y, z).
inline std::string quatXyzwText(const glm::quat& value) {
    return "xyzw(" + floatText(value.x) + ", " + floatText(value.y) + ", " +
           floatText(value.z) + ", " + floatText(value.w) + ")";
}

inline std::string matText(const glm::mat4& value) {
    std::string text = "columns[";
    for (int column = 0; column < 4; ++column) {
        if (column != 0) text += ", ";
        text += vecText(glm::vec4(
            value[column][0], value[column][1], value[column][2], value[column][3]));
    }
    return text + "]";
}

inline bool nearlyEqual(float actual, float expected, float epsilon = 1e-5f) {
    return std::fabs(actual - expected) <= epsilon;
}

inline bool nearlyEqual(const glm::vec3& actual, const glm::vec3& expected,
                        float epsilon = 1e-5f) {
    return nearlyEqual(actual.x, expected.x, epsilon) &&
           nearlyEqual(actual.y, expected.y, epsilon) &&
           nearlyEqual(actual.z, expected.z, epsilon);
}

inline bool nearlyEqual(const glm::vec4& actual, const glm::vec4& expected,
                        float epsilon = 1e-5f) {
    return nearlyEqual(actual.x, expected.x, epsilon) &&
           nearlyEqual(actual.y, expected.y, epsilon) &&
           nearlyEqual(actual.z, expected.z, epsilon) &&
           nearlyEqual(actual.w, expected.w, epsilon);
}

inline bool nearlyEqual(const glm::quat& actual, const glm::quat& expected,
                        float epsilon = 1e-5f) {
    return nearlyEqual(actual.x, expected.x, epsilon) &&
           nearlyEqual(actual.y, expected.y, epsilon) &&
           nearlyEqual(actual.z, expected.z, epsilon) &&
           nearlyEqual(actual.w, expected.w, epsilon);
}

inline bool nearlyEqual(const glm::mat4& actual, const glm::mat4& expected,
                        float epsilon = 1e-5f) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!nearlyEqual(actual[column][row], expected[column][row], epsilon)) {
                return false;
            }
        }
    }
    return true;
}

// A directory prefix must be one safe path component: no separators, no dots,
// no drive or parent references, no whitespace.
inline bool validDirectoryPrefix(const std::string& name) {
    if (name.empty() || name.size() > 64u || name == "." || name == "..") {
        return false;
    }
    for (const char character : name) {
        const bool allowed =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

// A child fixture name may carry an extension but must stay a single component.
inline bool validFileName(const std::string& name) {
    if (name.empty() || name.size() > 128u || name == "." || name == "..") {
        return false;
    }
    for (const char character : name) {
        const bool allowed =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_' || character == '.';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

// Process-unique salt for directory names. random_device is seeded once; the
// atomic sequence keeps simultaneous instances in one process distinct, and
// create_directory still detects a cross-process collision.
inline std::uint64_t fixtureNameSalt() {
    static const std::uint64_t salt = [] {
        std::random_device device;
        const std::uint64_t randomBits =
            (static_cast<std::uint64_t>(device()) << 32u) ^
            static_cast<std::uint64_t>(device());
        const std::uint64_t ticks = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return randomBits ^ (ticks * 0x9E3779B97F4A7C15ull);
    }();
    return salt;
}

inline std::string fixtureNameSuffix() {
    static std::atomic<std::uint64_t> sequence{0u};
    std::uint64_t mixed = fixtureNameSalt() +
                          sequence.fetch_add(1u, std::memory_order_relaxed) *
                              0x9E3779B97F4A7C15ull;
    mixed ^= mixed >> 29u;
    mixed *= 0xBF58476D1CE4E5B9ull;
    mixed ^= mixed >> 32u;
    std::ostringstream stream;
    stream << std::hex << mixed;
    std::string text = stream.str();
    while (text.size() < 16u) {
        text.insert(text.begin(), '0');
    }
    return text;
}

// The system temporary directory without a trailing separator. Some platforms
// report it with one, which would break stable parent-path comparisons.
inline std::filesystem::path normalizedTemporaryRoot() {
    std::filesystem::path root =
        std::filesystem::temp_directory_path().lexically_normal();
    std::string text = root.string();
    while (text.size() > 1u &&
           (text.back() == '\\' || text.back() == '/') &&
           text[text.size() - 2u] != ':') {
        text.pop_back();
    }
    return std::filesystem::path(text);
}

// Owns one uniquely named directory beneath the system temporary directory.
//
// The name is reserved with create_directory, which only reports success for
// the caller that actually created the directory; collisions retry with a new
// suffix. No preexisting path is inspected for content or removed, and the
// destructor removes only the directory this instance created.
class FixtureDirectory {
public:
    static std::filesystem::path temporaryRoot() {
        return normalizedTemporaryRoot();
    }

    explicit FixtureDirectory(std::string prefix) {
        if (!validDirectoryPrefix(prefix)) {
            error_ = "fixture prefix must be one safe path component";
            return;
        }
        const std::filesystem::path root = temporaryRoot();
        for (int attempt = 0; attempt < 64; ++attempt) {
            const std::filesystem::path candidate =
                root / (prefix + "-" + fixtureNameSuffix());
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                created_ = true;
                return;
            }
            if (error) {
                error_ = "could not create fixture directory: " + error.message();
                return;
            }
        }
        error_ = "no free fixture directory name was available";
    }

    FixtureDirectory(const FixtureDirectory&) = delete;
    FixtureDirectory& operator=(const FixtureDirectory&) = delete;

    ~FixtureDirectory() {
        if (!created_) {
            return;
        }
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    bool ready() const { return created_; }

    const std::filesystem::path& path() const { return path_; }

    const std::string& error() const { return error_; }

    std::filesystem::path file(const std::string& name) const {
        if (!created_ || !validFileName(name)) {
            return {};
        }
        return path_ / name;
    }

    bool writeText(const std::string& name, const std::string& text) const {
        const std::filesystem::path target = file(name);
        if (target.empty()) {
            return false;
        }
        std::ofstream output(target, std::ios::binary);
        output << text;
        return output.good();
    }

    bool writeBytes(const std::string& name,
                    const std::vector<std::uint8_t>& bytes) const {
        const std::filesystem::path target = file(name);
        if (target.empty()) {
            return false;
        }
        std::ofstream output(target, std::ios::binary);
        if (!bytes.empty()) {
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        }
        return output.good();
    }

private:
    std::filesystem::path path_;
    std::string error_;
    bool created_ = false;
};

// Writes authored fixture data as explicit little-endian bytes instead of raw
// struct memory, so the fixtures stay portable and reviewable.
class LittleEndianBuffer {
public:
    void addU8(std::uint8_t value) { bytes_.push_back(value); }

    void addU16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    }

    void addF32(float value) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) {
            bytes_.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFu));
        }
    }

    void addVec3(const glm::vec3& value) {
        addF32(value.x);
        addF32(value.y);
        addF32(value.z);
    }

    void addVec4(const glm::vec4& value) {
        addF32(value.x);
        addF32(value.y);
        addF32(value.z);
        addF32(value.w);
    }

    // MAT4 accessors are column-major in glTF, which is also glm::mat4 storage.
    void addMat4Columns(const glm::mat4& value) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                addF32(value[column][row]);
            }
        }
    }

    std::size_t size() const { return bytes_.size(); }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

}  // namespace gltf_import_contract
