#pragma once

#include "engine/models/ModelData.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace engine::models {

inline nlohmann::json encodeModelBillboards(const engine::models::MeshData& mesh) {
    nlohmann::json records = nlohmann::json::array();
    for (const auto& billboard : mesh.animatedBillboards) {
        nlohmann::json clips = nlohmann::json::array();
        for (const auto& curve : billboard.clipScales) {
            nlohmann::json keys = nlohmann::json::array();
            for (const auto& key : curve.keys) keys.push_back({key.timeSec, key.value});
            clips.push_back(std::move(keys));
        }
        records.push_back({{"node", billboard.nodeIndex},
            {"offset", {billboard.offset.x, billboard.offset.y, billboard.offset.z}},
            {"quad_offset", {billboard.quadOffset.x, billboard.quadOffset.y, billboard.quadOffset.z}},
            {"up", {billboard.up.x, billboard.up.y, billboard.up.z}},
            {"size", {billboard.size.x, billboard.size.y}},
            {"texture", {{"width", billboard.texture.width}, {"height", billboard.texture.height},
                {"rgba", billboard.texture.rgba}}}, {"clip_scales", std::move(clips)},
            {"clip_loops", billboard.clipLoops}});
        if (!billboard.clipOffsets.empty()) {
            auto offsets = nlohmann::json::array();
            for (const auto& curves : billboard.clipOffsets) {
                auto keys = nlohmann::json::array();
                for (std::size_t key = 0; key < curves[0].keys.size(); ++key)
                    keys.push_back({curves[0].keys[key].timeSec, curves[0].keys[key].value,
                        curves[1].keys[key].value, curves[2].keys[key].value});
                offsets.push_back(std::move(keys));
            }
            records.back()["clip_offsets"] = std::move(offsets);
        }
    }
    return records;
}

inline void decodeModelBillboards(const nlohmann::json& records, engine::models::MeshData& mesh) {
    if (!records.is_array()) throw std::runtime_error("Model billboards must be an array.");
    for (const auto& record : records) {
        engine::models::AnimatedBillboard billboard;
        billboard.nodeIndex = record.at("node").get<int>();
        const auto offset = record.at("offset").get<std::array<float, 3>>();
        const auto size = record.at("size").get<std::array<float, 2>>();
        const auto up = record.value("up", std::array<float, 3>{0, 1, 0});
        const auto quadOffset = record.value("quad_offset", std::array<float, 3>{0, 0, 0});
        billboard.offset = {offset[0], offset[1], offset[2]};
        billboard.size = {size[0], size[1]};
        billboard.up = {up[0], up[1], up[2]};
        billboard.quadOffset = {quadOffset[0], quadOffset[1], quadOffset[2]};
        billboard.texture.width = record.at("texture").at("width").get<int>();
        billboard.texture.height = record.at("texture").at("height").get<int>();
        if (billboard.nodeIndex < 0 || static_cast<std::size_t>(billboard.nodeIndex) >= mesh.nodesDefault.size() ||
            !std::isfinite(size[0]) || !std::isfinite(size[1]) || size[0] <= 0 || size[1] <= 0 ||
            !std::isfinite(offset[0]) || !std::isfinite(offset[1]) || !std::isfinite(offset[2]) ||
            !std::isfinite(up[0]) || !std::isfinite(up[1]) || !std::isfinite(up[2]) ||
            !std::isfinite(quadOffset[0]) || !std::isfinite(quadOffset[1]) || !std::isfinite(quadOffset[2]) ||
            glm::dot(billboard.up, billboard.up) < 1e-10f ||
            billboard.texture.width <= 0 || billboard.texture.width > 1024 ||
            billboard.texture.height <= 0 || billboard.texture.height > 1024 ||
            !record.at("clip_scales").is_array() || record.at("clip_scales").size() != mesh.animations.size() ||
            !record.at("clip_loops").is_array() || record.at("clip_loops").size() != mesh.animations.size()) {
            throw std::runtime_error("Model billboard attachment is invalid.");
        }
        const auto& rgba = record.at("texture").at("rgba");
        if (!rgba.is_array() || rgba.size() != static_cast<std::size_t>(billboard.texture.width) * billboard.texture.height * 4u)
            throw std::runtime_error("Model billboard texture size is invalid.");
        for (const auto& value : rgba) {
            const int byte = value.get<int>();
            if (byte < 0 || byte > 255) throw std::runtime_error("Model billboard texture byte is invalid.");
            billboard.texture.rgba.push_back(static_cast<unsigned char>(byte));
        }
        billboard.texture.wrapS = billboard.texture.wrapT = 33071;
        billboard.clipLoops = record.at("clip_loops").get<std::vector<bool>>();
        if (record.contains("clip_offsets") && (!record.at("clip_offsets").is_array() ||
                record.at("clip_offsets").size() != mesh.animations.size()))
            throw std::runtime_error("Model billboard offset clips are invalid.");
        for (std::size_t clip = 0; clip < mesh.animations.size(); ++clip) {
            engine::models::MaterialAnimationCurve curve;
            float previous = -1.0f;
            for (const auto& key : record.at("clip_scales").at(clip)) {
                const auto values = key.get<std::array<float, 2>>();
                if (!std::isfinite(values[0]) || !std::isfinite(values[1]) || values[0] < 0 ||
                    values[0] <= previous || values[0] > mesh.animations[clip].durationSec + 1e-4f || values[1] < 0)
                    throw std::runtime_error("Model billboard scale key is invalid.");
                curve.keys.push_back({values[0], values[1]});
                previous = values[0];
            }
            billboard.clipScales.push_back(std::move(curve));
            if (record.contains("clip_offsets")) {
                std::array<engine::models::MaterialAnimationCurve, 3u> offsets;
                previous = -1.0f;
                for (const auto& key : record.at("clip_offsets").at(clip)) {
                    const auto values = key.get<std::array<float, 4>>();
                    if (!std::isfinite(values[0]) || !std::isfinite(values[1]) ||
                        !std::isfinite(values[2]) || !std::isfinite(values[3]) || values[0] < 0 ||
                        values[0] <= previous || values[0] > mesh.animations[clip].durationSec + 1e-4f)
                        throw std::runtime_error("Model billboard offset key is invalid.");
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        offsets[axis].keys.push_back({values[0], values[axis+1]});
                    previous = values[0];
                }
                billboard.clipOffsets.push_back(std::move(offsets));
            }
        }
        mesh.animatedBillboards.push_back(std::move(billboard));
    }
}

} // namespace engine::models
