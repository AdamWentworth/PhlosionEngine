#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <string>
#include <vector>

bool test_phlosion_resource_container_contract(std::string& outFail) {
    using namespace engine::assets::phrc;

    Document source;
    source.magic = magic("PHLO");
    source.schemaVersion = 3u;
    source.flags = 9u;
    source.manifestJson =
        R"json({"asset_id":"characters/bulbasaur","root_type":"CharacterPrefab"})json";
    source.dependencies = {
        Dependency{
            "characters/bulbasaur/body",
            0x123456789abcdef0ull,
            kDependencyRequired},
        Dependency{
            "materials/lgpe/pokemon_skin",
            0u,
            0u}};
    source.chunks = {
        Chunk{magic("MESH"), 4u, 16u, {1u, 2u, 3u, 4u}},
        Chunk{magic("SKEL"), 0u, 64u, {8u, 9u, 10u}}};

    std::vector<std::uint8_t> encoded;
    std::string error;
    if (!encode(source, encoded, &error)) {
        outFail = "PHRC encode failed: " + error;
        return false;
    }
    if (encoded.size() < kHeaderBytes ||
        encoded[0] != 'P' ||
        encoded[1] != 'H' ||
        encoded[2] != 'L' ||
        encoded[3] != 'O') {
        outFail = "PHRC should write the profile magic and fixed header.";
        return false;
    }

    Document decoded;
    if (!decode(encoded, decoded, &error)) {
        outFail = "PHRC decode failed: " + error;
        return false;
    }
    if (decoded.magic != source.magic ||
        decoded.schemaVersion != source.schemaVersion ||
        decoded.flags != source.flags ||
        decoded.manifestJson != source.manifestJson ||
        decoded.dependencies.size() != 2u ||
        decoded.chunks.size() != 2u ||
        decoded.contentHash == 0u) {
        outFail = "PHRC round trip lost container metadata.";
        return false;
    }
    if (decoded.dependencies[0].assetId !=
            "characters/bulbasaur/body" ||
        decoded.dependencies[0].expectedContentHash !=
            0x123456789abcdef0ull ||
        decoded.dependencies[1].flags != 0u) {
        outFail = "PHRC round trip lost dependency metadata.";
        return false;
    }
    const Chunk* mesh = findChunk(decoded, "MESH");
    const Chunk* skeleton = findChunk(decoded, "SKEL");
    if (!mesh || !skeleton ||
        mesh->bytes != std::vector<std::uint8_t>({1u, 2u, 3u, 4u}) ||
        skeleton->alignment != 64u) {
        outFail = "PHRC round trip lost typed chunk data.";
        return false;
    }

    std::vector<std::uint8_t> corrupted = encoded;
    corrupted.back() ^= 0x80u;
    if (decode(corrupted, decoded, &error)) {
        outFail = "PHRC should reject corrupted content.";
        return false;
    }
    if (error.find("hash mismatch") == std::string::npos) {
        outFail = "PHRC corruption diagnostic should identify the hash.";
        return false;
    }
    return true;
}
