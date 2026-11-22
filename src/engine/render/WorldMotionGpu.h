#pragma once
#include "engine/render/WorldMotionHistory.h"
#include <array>
#include <bit>
#include <vector>

namespace engine::render {
inline std::size_t validateWorldMotionSamples(std::span<const WorldMotionSample> samples) {
    if(samples.size()>WorldMotionHistory::maxObjects)throw std::invalid_argument("Motion instance budget exceeded");
    std::size_t wordCount=1+samples.size();
    for(const auto& s:samples) {
        if(!s.previous || !s.imageIdentity || !validWorldTemporalMaterial(s.previous->temporalMaterial))throw std::invalid_argument("Missing previous motion pose/identity");
        const auto& p=*s.previous;
        if(p.matrixCount>128 || p.skinningMode>1 ||
            p.deformation.size()!=std::size_t(p.matrixCount)*(p.skinningMode?32:16))
            throw std::invalid_argument("Invalid motion palette packet");
        if(p.positions.size()>WorldMotionHistory::maxPositionVertices || (!p.positions.empty() && p.matrixCount))
            throw std::invalid_argument("Invalid CPU motion position packet");
        for(const auto& position:p.positions)for(float value:position)if(!std::isfinite(value))
            throw std::invalid_argument("Nonfinite CPU motion position");
        wordCount+=16+p.deformation.size()/4+(p.positions.empty()?0:1+p.positions.size());
    }
    return wordCount;
}
// One float4 count, one float4 offset per instance, then owned previous-pose
// packets. Offsets are float4 words. IDs use two exact 16-bit float components.
// Reuse the backend's packet storage; validate first so rejection leaves it intact.
inline void packWorldMotionDraw(
    const WorldMotionMatrix& previousViewProjection, std::span<const WorldMotionSample> samples,
    std::vector<std::array<float,4>>& words,
    std::span<const std::uint32_t> paletteWordOffsets={}) {
    const auto inlineWords=validateWorldMotionSamples(samples);
    if(!paletteWordOffsets.empty() && paletteWordOffsets.size()!=samples.size())
        throw std::invalid_argument("Motion palette offset count mismatch");
    for(auto offset:paletteWordOffsets)if(offset>16777216u || offset%4u)
        throw std::invalid_argument("Invalid motion palette word offset");
    auto wordCount=paletteWordOffsets.empty()?inlineWords:1+samples.size()*17;
    if(!paletteWordOffsets.empty())for(const auto& s:samples)
        if(!s.previous->positions.empty())wordCount+=1+s.previous->positions.size();
    if(wordCount>16777216u)throw std::invalid_argument("Motion word offset exceeds exact float range");
    words.reserve(wordCount);
    words.clear();words.resize(1+samples.size());
    words[0][0]=static_cast<float>(samples.size());
    const auto appendMatrix=[&](const WorldMotionMatrix& m) {
        for(unsigned i=0;i<4;++i)words.push_back({m[i*4],m[i*4+1],m[i*4+2],m[i*4+3]});
    };
    for(std::size_t i=0;i<samples.size();++i) {
        const auto& s=samples[i];
        const auto& p=*s.previous;
        words[i+1][0]=static_cast<float>(words.size());
        if(!paletteWordOffsets.empty()) {
            words[i+1][1]=float(paletteWordOffsets[i]);words[i+1][2]=1.f;
        }
        appendMatrix(p.model);appendMatrix(p.instance);appendMatrix(previousViewProjection);
        for(unsigned j=0;j<3;++j)words.push_back({p.materialParameters[j*4],p.materialParameters[j*4+1],
            p.materialParameters[j*4+2],p.materialParameters[j*4+3]});
        words.push_back({float(p.matrixCount),float(p.skinningMode),
            float(s.imageIdentity&65535u),float((s.imageIdentity>>16)+(s.valid?65536u:0u)+(std::uint32_t(p.temporalMaterial)<<17))});
        if(paletteWordOffsets.empty())for(std::size_t j=0;j<p.deformation.size();j+=4)
            words.push_back({p.deformation[j],p.deformation[j+1],p.deformation[j+2],p.deformation[j+3]});
        if(!p.positions.empty()) {
            words[i+1][3]=float(words.size());
            words.push_back({float(p.positions.size()),0,0,0});
            for(const auto& position:p.positions)words.push_back({position[0],position[1],position[2],1});
        }
    }
}
inline std::vector<std::array<float,4>> packWorldMotionDraw(
    const WorldMotionMatrix& previousViewProjection, std::span<const WorldMotionSample> samples) {
    std::vector<std::array<float,4>> words;
    packWorldMotionDraw(previousViewProjection,samples,words);
    return words;
}
struct WorldMotionImage {
    int width=0,height=0;
    // Signed previousUV-currentUV, previous depth 0..1, validity.
    std::vector<std::array<float,4>> velocity;
    std::vector<std::uint32_t> identity;
    std::vector<std::uint32_t> identityMetadata; // unmodified second integer word, including compact probe diagnostics
    std::vector<std::uint8_t> temporalMaterial;
    // Optional diagnostic data from the same surface draw: 0 empty, 1 opaque
    // evaluated, 2 masked evaluated, 3 motion-only; packed RGBA8 metal/F0/AO/roughness.
    std::vector<std::uint8_t> materialClass;
    std::vector<std::uint32_t> albedo; // RGB10A2 sqrt albedo; alpha 0 standard, 1 specialized forward, 2 composable standard
    std::vector<std::uint32_t> normal; // packed RGB10A2 mapped normal/class
    std::vector<std::uint32_t> material;
    std::uint64_t surfaceTexelBytes=0; // all six planes, excluding depth/alignment
};
inline float decodeMotionHalf(std::uint16_t value) {
    const auto sign=std::uint32_t(value&0x8000u)<<16;
    int exponent=(value>>10)&31;std::uint32_t mantissa=value&1023;
    if(exponent==0) {
        if(!mantissa)return std::bit_cast<float>(sign);
        exponent=1;
        while(!(mantissa&1024u)){mantissa<<=1;--exponent;}
        mantissa&=1023u;
    } else if(exponent==31)return std::bit_cast<float>(sign|0x7f800000u|(mantissa<<13));
    return std::bit_cast<float>(sign|(std::uint32_t(exponent+112)<<23)|(mantissa<<13));
}
}
