#pragma once

#include "engine/render/WorldEnvironmentLighting.h"
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace engine::render {
struct WorldEnvironmentPacket {
    WorldEnvironmentUniforms environment{};
    WorldReflectionResponseUniforms reflectionResponse{};
    WorldPointLightUniforms pointLights{};
    WorldRectLightUniforms rectLights{};
    WorldPuddleUniforms puddles{};
    WorldSkyUniforms sky{};
};

inline WorldEnvironmentPacket packWorldEnvironmentPacket(const WorldEnvironmentLighting& input) {
    WorldEnvironmentPacket out;
    out.environment = packWorldEnvironment(input);
    out.reflectionResponse = packWorldReflectionResponse(input);
    out.pointLights = packWorldPointLights(input, out.environment);
    out.rectLights = packWorldRectLights(input, out.environment);
    out.puddles = packWorldPuddles(input);
    if(out.environment[0][3]>.5f) {
        out.sky=packSkyModel(input.sky,input.sunDirection,input.timeOfDayHours,input.sky.indirect);
        // Reserved sky row: shared directional response, including probe relighting.
        for(unsigned i=0;i<3;++i) {
            const float tint=input.globalSpecularTint[i];
            if(!std::isfinite(tint)||tint<0||tint>64)throw std::invalid_argument("Invalid global specular tint");
            out.sky[21][i]=tint;
        }
        out.sky[21][3]=1;
    }
    return out;
}

// Backend-owned, bounded CPU data only. Complete value snapshots keep per-draw
// weather/material overrides live; no caller addresses or native resources are
// retained. Compare copied object representations: padding may cause a miss,
// never a false hit, and signed zero/NaN bits cannot alias another input.
class WorldEnvironmentPacketCache {
public:
    static constexpr std::size_t capacity = 8;
    const WorldEnvironmentPacket& resolve(const WorldEnvironmentLighting* input) {
        static const WorldEnvironmentLighting disabled;
        return resolve(input ? *input : disabled);
    }
    std::uint64_t packedSnapshots() const { return packedSnapshots_; }
    const WorldEnvironmentPacket& resolve(const WorldEnvironmentLighting& authored) {
        if(authored.lightChannels>255)throw std::invalid_argument("Invalid receiver channels");
        WorldEnvironmentLighting input;
        std::memcpy(&input,&authored,sizeof(input));input.lightChannels=255;
        if (matches(entries_[last_], input)) return receiver(entries_[last_],authored.lightChannels);
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (i != last_ && matches(entries_[i], input)) {
                last_ = i;
                return receiver(entries_[i],authored.lightChannels);
            }
        }
        last_ = next_;
        next_ = (next_ + 1) % entries_.size();
        auto& entry = entries_[last_];
        entry.packet = packWorldEnvironmentPacket(input);
        ++packedSnapshots_;entry.receivers=entry.packet.pointLights[1][3]!=0;
        std::memcpy(entry.input.data(), &input, sizeof(input));
        entry.valid = true;
        return receiver(entry,authored.lightChannels);
    }
private:
    static_assert(std::is_trivially_copyable_v<WorldEnvironmentLighting>);
    struct Entry {
        std::array<std::byte, sizeof(WorldEnvironmentLighting)> input{};
        WorldEnvironmentPacket packet{};
        bool valid = false,receivers = false;
    };
    static bool matches(const Entry& entry, const WorldEnvironmentLighting& input) {
        return entry.valid && std::memcmp(entry.input.data(), &input, sizeof(input)) == 0;
    }
    static const WorldEnvironmentPacket& receiver(Entry& entry,std::uint32_t mask) {
        if(entry.receivers)entry.packet.pointLights[1][3]=float((std::uint32_t(entry.packet.pointLights[1][3])&65535u)|(mask<<16));
        return entry.packet;
    }
    std::uint64_t packedSnapshots_=0;
    std::array<Entry, capacity> entries_{};
    std::size_t last_ = 0, next_ = 0;
};
} // namespace engine::render
