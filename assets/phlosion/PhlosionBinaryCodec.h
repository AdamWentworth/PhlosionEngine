#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engine::assets::phrc {

class BinaryWriter {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void u16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffu));
        bytes_.push_back(
            static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    }

    void u32(std::uint32_t value) {
        for (std::uint32_t shift = 0u; shift < 32u; shift += 8u) {
            bytes_.push_back(
                static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void u64(std::uint64_t value) {
        for (std::uint32_t shift = 0u; shift < 64u; shift += 8u) {
            bytes_.push_back(
                static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void i32(std::int32_t value) {
        u32(std::bit_cast<std::uint32_t>(value));
    }

    void f32(float value) {
        u32(std::bit_cast<std::uint32_t>(value));
    }

    void string(const std::string& value) {
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void raw(const std::uint8_t* bytes, std::size_t byteCount) {
        if (byteCount != 0u) {
            bytes_.insert(bytes_.end(), bytes, bytes + byteCount);
        }
    }

    const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }

    std::vector<std::uint8_t> take() {
        return std::move(bytes_);
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class BinaryReader {
public:
    explicit BinaryReader(const std::vector<std::uint8_t>& bytes)
        : bytes_(bytes) {}

    bool u8(std::uint8_t& out) {
        return raw(&out, sizeof(out));
    }

    bool u16(std::uint16_t& out) {
        std::uint8_t values[2]{};
        if (!raw(values, sizeof(values))) return false;
        out = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(values[0]) |
            (static_cast<std::uint16_t>(values[1]) << 8u));
        return true;
    }

    bool u32(std::uint32_t& out) {
        std::uint8_t values[4]{};
        if (!raw(values, sizeof(values))) return false;
        out =
            static_cast<std::uint32_t>(values[0]) |
            (static_cast<std::uint32_t>(values[1]) << 8u) |
            (static_cast<std::uint32_t>(values[2]) << 16u) |
            (static_cast<std::uint32_t>(values[3]) << 24u);
        return true;
    }

    bool u64(std::uint64_t& out) {
        std::uint8_t values[8]{};
        if (!raw(values, sizeof(values))) return false;
        out = 0u;
        for (std::uint32_t index = 0u; index < 8u; ++index) {
            out |= static_cast<std::uint64_t>(values[index])
                << (index * 8u);
        }
        return true;
    }

    bool i32(std::int32_t& out) {
        std::uint32_t bits = 0u;
        if (!u32(bits)) return false;
        out = std::bit_cast<std::int32_t>(bits);
        return true;
    }

    bool f32(float& out) {
        std::uint32_t bits = 0u;
        if (!u32(bits)) return false;
        out = std::bit_cast<float>(bits);
        return true;
    }

    bool string(std::string& out, std::uint32_t maxBytes = 16u * 1024u * 1024u) {
        std::uint32_t byteCount = 0u;
        if (!u32(byteCount) ||
            byteCount > maxBytes ||
            byteCount > remaining()) {
            return false;
        }
        out.resize(byteCount);
        return raw(out.data(), out.size());
    }

    bool raw(void* out, std::size_t byteCount) {
        if (byteCount > remaining()) return false;
        if (byteCount != 0u) {
            std::memcpy(out, bytes_.data() + cursor_, byteCount);
        }
        cursor_ += byteCount;
        return true;
    }

    std::size_t remaining() const noexcept {
        return cursor_ <= bytes_.size()
            ? bytes_.size() - cursor_
            : 0u;
    }

    bool finished() const noexcept {
        return cursor_ == bytes_.size();
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t cursor_ = 0u;
};

} // namespace engine::assets::phrc
