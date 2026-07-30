#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace engine::assets::phrc {
namespace {

constexpr std::uint32_t kMaxDependencies = 1'000'000u;
constexpr std::uint32_t kMaxChunks = 1'000'000u;
constexpr std::uint32_t kMaxStringBytes = 16u * 1024u * 1024u;
constexpr std::uint64_t kMaxManifestBytes = 256ull * 1024ull * 1024ull;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::uint32_t shift = 0u; shift < 32u; shift += 8u) {
        out.push_back(
            static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void appendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (std::uint32_t shift = 0u; shift < 64u; shift += 8u) {
        out.push_back(
            static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void patchU64(
    std::vector<std::uint8_t>& out,
    std::size_t offset,
    std::uint64_t value) {
    for (std::uint32_t shift = 0u; shift < 64u; shift += 8u) {
        out[offset + shift / 8u] =
            static_cast<std::uint8_t>((value >> shift) & 0xffu);
    }
}

std::uint64_t alignUp(std::uint64_t value, std::uint32_t alignment) {
    const std::uint64_t safeAlignment =
        std::max<std::uint32_t>(1u, alignment);
    const std::uint64_t remainder = value % safeAlignment;
    return remainder == 0u
        ? value
        : value + safeAlignment - remainder;
}

void padTo(std::vector<std::uint8_t>& out, std::uint64_t offset) {
    if (offset > out.size()) {
        out.resize(static_cast<std::size_t>(offset), 0u);
    }
}

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::uint64_t cursor = 0u;
    std::uint64_t limit = 0u;

    bool readBytes(void* out, std::size_t byteCount) {
        if (byteCount > limit ||
            cursor > limit - static_cast<std::uint64_t>(byteCount)) {
            return false;
        }
        if (byteCount != 0u) {
            std::memcpy(
                out,
                bytes.data() + static_cast<std::size_t>(cursor),
                byteCount);
        }
        cursor += static_cast<std::uint64_t>(byteCount);
        return true;
    }

    bool readU16(std::uint16_t& out) {
        std::uint8_t values[2]{};
        if (!readBytes(values, sizeof(values))) return false;
        out = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(values[0]) |
            (static_cast<std::uint16_t>(values[1]) << 8u));
        return true;
    }

    bool readU32(std::uint32_t& out) {
        std::uint8_t values[4]{};
        if (!readBytes(values, sizeof(values))) return false;
        out =
            static_cast<std::uint32_t>(values[0]) |
            (static_cast<std::uint32_t>(values[1]) << 8u) |
            (static_cast<std::uint32_t>(values[2]) << 16u) |
            (static_cast<std::uint32_t>(values[3]) << 24u);
        return true;
    }

    bool readU64(std::uint64_t& out) {
        std::uint8_t values[8]{};
        if (!readBytes(values, sizeof(values))) return false;
        out = 0u;
        for (std::uint32_t index = 0u; index < 8u; ++index) {
            out |= static_cast<std::uint64_t>(values[index])
                << (index * 8u);
        }
        return true;
    }
};

struct ChunkRecord {
    std::array<char, 4> type{};
    std::uint32_t flags = 0u;
    std::uint32_t alignment = 1u;
    std::uint64_t offset = 0u;
    std::uint64_t byteCount = 0u;
    std::uint64_t uncompressedByteCount = 0u;
    std::uint64_t hash = 0u;
};

} // namespace

Magic magic(std::string_view fourCharacters) {
    Magic out{};
    if (fourCharacters.size() == out.size()) {
        std::copy(fourCharacters.begin(), fourCharacters.end(), out.begin());
    }
    return out;
}

std::string magicString(const Magic& value) {
    return std::string(value.begin(), value.end());
}

std::uint64_t contentHash64(
    const void* data,
    std::size_t byteCount,
    std::uint64_t seed) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    for (std::size_t index = 0u; index < byteCount; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t contentHash64(const std::vector<std::uint8_t>& bytes) {
    return contentHash64(bytes.data(), bytes.size());
}

bool encode(
    const Document& document,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError) {
    outBytes.clear();
    if (document.manifestJson.size() > kMaxManifestBytes) {
        return fail(outError, "PHRC manifest exceeds the safety limit.");
    }
    if (document.dependencies.size() > kMaxDependencies) {
        return fail(outError, "PHRC dependency count exceeds the safety limit.");
    }
    if (document.chunks.size() > kMaxChunks) {
        return fail(outError, "PHRC chunk count exceeds the safety limit.");
    }

    std::vector<std::uint8_t> dependencies;
    for (const Dependency& dependency : document.dependencies) {
        if (dependency.assetId.empty() ||
            dependency.assetId.size() > kMaxStringBytes) {
            return fail(outError, "PHRC dependency asset ID is invalid.");
        }
        appendU32(
            dependencies,
            static_cast<std::uint32_t>(dependency.assetId.size()));
        dependencies.insert(
            dependencies.end(),
            dependency.assetId.begin(),
            dependency.assetId.end());
        appendU64(dependencies, dependency.expectedContentHash);
        appendU32(dependencies, dependency.flags);
    }

    constexpr std::uint64_t kChunkRecordBytes = 48u;
    const std::uint64_t manifestOffset = kHeaderBytes;
    const std::uint64_t manifestBytes = document.manifestJson.size();
    const std::uint64_t dependencyOffset =
        alignUp(manifestOffset + manifestBytes, 8u);
    const std::uint64_t dependencyBytes = dependencies.size();
    const std::uint64_t chunkTableOffset =
        alignUp(dependencyOffset + dependencyBytes, 8u);
    const std::uint64_t chunkTableBytes =
        static_cast<std::uint64_t>(document.chunks.size()) *
        kChunkRecordBytes;
    const std::uint64_t payloadOffset =
        alignUp(chunkTableOffset + chunkTableBytes, 16u);

    std::vector<ChunkRecord> records;
    records.reserve(document.chunks.size());
    std::uint64_t nextPayloadOffset = payloadOffset;
    for (const Chunk& chunk : document.chunks) {
        if (chunk.alignment == 0u ||
            (chunk.alignment & (chunk.alignment - 1u)) != 0u ||
            chunk.alignment > 4096u) {
            return fail(
                outError,
                "PHRC chunk alignment must be a power of two up to 4096.");
        }
        nextPayloadOffset = alignUp(nextPayloadOffset, chunk.alignment);
        if (chunk.bytes.size() >
            std::numeric_limits<std::uint64_t>::max() - nextPayloadOffset) {
            return fail(outError, "PHRC payload size overflow.");
        }
        ChunkRecord record;
        record.type = chunk.type;
        record.flags = chunk.flags;
        record.alignment = chunk.alignment;
        record.offset = nextPayloadOffset;
        record.byteCount = chunk.bytes.size();
        record.uncompressedByteCount = chunk.bytes.size();
        record.hash = contentHash64(chunk.bytes);
        records.push_back(record);
        nextPayloadOffset += chunk.bytes.size();
    }
    const std::uint64_t payloadBytes =
        nextPayloadOffset >= payloadOffset
        ? nextPayloadOffset - payloadOffset
        : 0u;
    if (nextPayloadOffset >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return fail(outError, "PHRC output exceeds the host address space.");
    }

    outBytes.reserve(static_cast<std::size_t>(nextPayloadOffset));
    outBytes.insert(
        outBytes.end(),
        document.magic.begin(),
        document.magic.end());
    appendU16(outBytes, kContainerVersion);
    appendU16(outBytes, kHeaderBytes);
    appendU32(outBytes, document.schemaVersion);
    appendU32(outBytes, document.flags);
    appendU32(
        outBytes,
        static_cast<std::uint32_t>(document.dependencies.size()));
    appendU32(
        outBytes,
        static_cast<std::uint32_t>(document.chunks.size()));
    appendU64(outBytes, manifestOffset);
    appendU64(outBytes, manifestBytes);
    appendU64(outBytes, dependencyOffset);
    appendU64(outBytes, dependencyBytes);
    appendU64(outBytes, chunkTableOffset);
    appendU64(outBytes, payloadOffset);
    appendU64(outBytes, payloadBytes);
    const std::size_t contentHashOffset = outBytes.size();
    appendU64(outBytes, 0u);
    appendU64(outBytes, 0u);
    padTo(outBytes, kHeaderBytes);

    outBytes.insert(
        outBytes.end(),
        document.manifestJson.begin(),
        document.manifestJson.end());
    padTo(outBytes, dependencyOffset);
    outBytes.insert(
        outBytes.end(),
        dependencies.begin(),
        dependencies.end());
    padTo(outBytes, chunkTableOffset);
    for (const ChunkRecord& record : records) {
        outBytes.insert(
            outBytes.end(),
            record.type.begin(),
            record.type.end());
        appendU32(outBytes, record.flags);
        appendU32(outBytes, record.alignment);
        appendU32(outBytes, 0u);
        appendU64(outBytes, record.offset);
        appendU64(outBytes, record.byteCount);
        appendU64(outBytes, record.uncompressedByteCount);
        appendU64(outBytes, record.hash);
    }
    padTo(outBytes, payloadOffset);
    for (std::size_t index = 0u; index < document.chunks.size(); ++index) {
        padTo(outBytes, records[index].offset);
        outBytes.insert(
            outBytes.end(),
            document.chunks[index].bytes.begin(),
            document.chunks[index].bytes.end());
    }
    padTo(outBytes, nextPayloadOffset);

    const std::uint64_t hash = contentHash64(
        outBytes.data() + kHeaderBytes,
        outBytes.size() - kHeaderBytes);
    patchU64(outBytes, contentHashOffset, hash);
    return true;
}

bool decode(
    const std::vector<std::uint8_t>& bytes,
    Document& outDocument,
    std::string* outError) {
    outDocument = Document{};
    if (bytes.size() < kHeaderBytes) {
        return fail(outError, "PHRC file is smaller than its header.");
    }

    Reader header{bytes, 0u, bytes.size()};
    Magic fileMagic{};
    std::uint16_t containerVersion = 0u;
    std::uint16_t headerBytes = 0u;
    std::uint32_t schemaVersion = 0u;
    std::uint32_t flags = 0u;
    std::uint32_t dependencyCount = 0u;
    std::uint32_t chunkCount = 0u;
    std::uint64_t manifestOffset = 0u;
    std::uint64_t manifestBytes = 0u;
    std::uint64_t dependencyOffset = 0u;
    std::uint64_t dependencyBytes = 0u;
    std::uint64_t chunkTableOffset = 0u;
    std::uint64_t payloadOffset = 0u;
    std::uint64_t payloadBytes = 0u;
    std::uint64_t storedContentHash = 0u;
    std::uint64_t reserved = 0u;
    if (!header.readBytes(fileMagic.data(), fileMagic.size()) ||
        !header.readU16(containerVersion) ||
        !header.readU16(headerBytes) ||
        !header.readU32(schemaVersion) ||
        !header.readU32(flags) ||
        !header.readU32(dependencyCount) ||
        !header.readU32(chunkCount) ||
        !header.readU64(manifestOffset) ||
        !header.readU64(manifestBytes) ||
        !header.readU64(dependencyOffset) ||
        !header.readU64(dependencyBytes) ||
        !header.readU64(chunkTableOffset) ||
        !header.readU64(payloadOffset) ||
        !header.readU64(payloadBytes) ||
        !header.readU64(storedContentHash) ||
        !header.readU64(reserved)) {
        return fail(outError, "PHRC header is truncated.");
    }
    if (containerVersion != kContainerVersion ||
        headerBytes != kHeaderBytes) {
        return fail(outError, "PHRC container version is unsupported.");
    }
    if (dependencyCount > kMaxDependencies || chunkCount > kMaxChunks) {
        return fail(outError, "PHRC table count exceeds the safety limit.");
    }
    if (manifestBytes > kMaxManifestBytes) {
        return fail(outError, "PHRC manifest exceeds the safety limit.");
    }
    const auto rangeValid = [&](std::uint64_t offset, std::uint64_t length) {
        return offset <= bytes.size() &&
            length <= bytes.size() - offset;
    };
    if (!rangeValid(manifestOffset, manifestBytes) ||
        !rangeValid(dependencyOffset, dependencyBytes) ||
        !rangeValid(payloadOffset, payloadBytes)) {
        return fail(outError, "PHRC section range is outside the file.");
    }
    constexpr std::uint64_t kChunkRecordBytes = 48u;
    const std::uint64_t chunkTableBytes =
        static_cast<std::uint64_t>(chunkCount) * kChunkRecordBytes;
    if (!rangeValid(chunkTableOffset, chunkTableBytes)) {
        return fail(outError, "PHRC chunk table is outside the file.");
    }
    const std::uint64_t computedContentHash = contentHash64(
        bytes.data() + headerBytes,
        bytes.size() - headerBytes);
    if (computedContentHash != storedContentHash) {
        return fail(outError, "PHRC content hash mismatch.");
    }

    Document decoded;
    decoded.magic = fileMagic;
    decoded.schemaVersion = schemaVersion;
    decoded.flags = flags;
    decoded.contentHash = storedContentHash;
    decoded.manifestJson.assign(
        reinterpret_cast<const char*>(
            bytes.data() + static_cast<std::size_t>(manifestOffset)),
        static_cast<std::size_t>(manifestBytes));

    Reader dependenciesReader{
        bytes,
        dependencyOffset,
        dependencyOffset + dependencyBytes};
    decoded.dependencies.reserve(dependencyCount);
    for (std::uint32_t index = 0u; index < dependencyCount; ++index) {
        std::uint32_t assetIdBytes = 0u;
        Dependency dependency;
        if (!dependenciesReader.readU32(assetIdBytes) ||
            assetIdBytes == 0u ||
            assetIdBytes > kMaxStringBytes ||
            assetIdBytes > dependenciesReader.limit -
                dependenciesReader.cursor) {
            return fail(outError, "PHRC dependency record is invalid.");
        }
        dependency.assetId.resize(assetIdBytes);
        if (!dependenciesReader.readBytes(
                dependency.assetId.data(),
                dependency.assetId.size()) ||
            !dependenciesReader.readU64(
                dependency.expectedContentHash) ||
            !dependenciesReader.readU32(dependency.flags)) {
            return fail(outError, "PHRC dependency record is truncated.");
        }
        decoded.dependencies.push_back(std::move(dependency));
    }
    if (dependenciesReader.cursor != dependenciesReader.limit) {
        return fail(
            outError,
            "PHRC dependency section contains trailing bytes.");
    }

    Reader chunkTableReader{
        bytes,
        chunkTableOffset,
        chunkTableOffset + chunkTableBytes};
    std::vector<ChunkRecord> records;
    records.reserve(chunkCount);
    for (std::uint32_t index = 0u; index < chunkCount; ++index) {
        ChunkRecord record;
        std::uint32_t reserved32 = 0u;
        if (!chunkTableReader.readBytes(
                record.type.data(),
                record.type.size()) ||
            !chunkTableReader.readU32(record.flags) ||
            !chunkTableReader.readU32(record.alignment) ||
            !chunkTableReader.readU32(reserved32) ||
            !chunkTableReader.readU64(record.offset) ||
            !chunkTableReader.readU64(record.byteCount) ||
            !chunkTableReader.readU64(
                record.uncompressedByteCount) ||
            !chunkTableReader.readU64(record.hash)) {
            return fail(outError, "PHRC chunk record is truncated.");
        }
        if (record.alignment == 0u ||
            (record.alignment & (record.alignment - 1u)) != 0u ||
            record.offset % record.alignment != 0u ||
            !rangeValid(record.offset, record.byteCount) ||
            record.offset < payloadOffset ||
            record.byteCount != record.uncompressedByteCount) {
            return fail(outError, "PHRC chunk record is invalid.");
        }
        records.push_back(record);
    }

    decoded.chunks.reserve(records.size());
    for (const ChunkRecord& record : records) {
        Chunk chunk;
        chunk.type = record.type;
        chunk.flags = record.flags;
        chunk.alignment = record.alignment;
        chunk.bytes.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(record.offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(
                record.offset + record.byteCount));
        if (contentHash64(chunk.bytes) != record.hash) {
            return fail(outError, "PHRC chunk hash mismatch.");
        }
        decoded.chunks.push_back(std::move(chunk));
    }

    outDocument = std::move(decoded);
    return true;
}

const Chunk* findChunk(
    const Document& document,
    std::string_view fourCharacterType) {
    const Magic expected = magic(fourCharacterType);
    const auto found = std::find_if(
        document.chunks.begin(),
        document.chunks.end(),
        [&](const Chunk& chunk) {
            return chunk.type == expected;
        });
    return found == document.chunks.end() ? nullptr : &*found;
}

} // namespace engine::assets::phrc
