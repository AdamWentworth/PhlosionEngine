#pragma once
#include "engine/render/IRenderBackend.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <stdexcept>
namespace engine::render::texture_quality {
struct CpuMipLevel {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

inline float srgbByteToLinear(unsigned char value) {
    const float c = static_cast<float>(value) / 255.0f;
    return c <= 0.04045f
        ? c / 12.92f
        : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline unsigned char linearToSrgbByte(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    const float srgb = c <= 0.0031308f
        ? c * 12.92f
        : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<unsigned char>(std::clamp(
        static_cast<int>(std::lround(srgb * 255.0f)),
        0,
        255));
}

inline int wrapTexelIndex(int index, int size, int wrapMode) {
    if (size <= 1) return 0;
    if (wrapMode == 33071) {
        return std::clamp(index, 0, size - 1);
    }
    if (wrapMode == 33648) {
        const int period = size * 2;
        int wrapped = index % period;
        if (wrapped < 0) wrapped += period;
        return wrapped >= size ? period - 1 - wrapped : wrapped;
    }
    int wrapped = index % size;
    if (wrapped < 0) wrapped += size;
    return wrapped;
}

inline std::vector<CpuMipLevel> buildRgbaMipChain(
    const unsigned char* pixels,
    int width,
    int height,
    int wrapS,
    int wrapT,
    bool srgb) {
    std::vector<CpuMipLevel> chain;
    if (!pixels || width <= 0 || height <= 0) return chain;
    chain.push_back(CpuMipLevel{
        width,
        height,
        std::vector<unsigned char>(
            pixels,
            pixels + static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) * 4u)});
    while (chain.back().width > 1 || chain.back().height > 1) {
        const CpuMipLevel& previous = chain.back();
        CpuMipLevel next;
        next.width = std::max(previous.width / 2, 1);
        next.height = std::max(previous.height / 2, 1);
        next.rgba.resize(
            static_cast<std::size_t>(next.width) *
            static_cast<std::size_t>(next.height) * 4u);
        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                float sums[4]{};
                for (int oy = 0; oy < 2; ++oy) {
                    const int sourceY = wrapTexelIndex(
                        y * 2 + oy,
                        previous.height,
                        wrapT);
                    for (int ox = 0; ox < 2; ++ox) {
                        const int sourceX = wrapTexelIndex(
                            x * 2 + ox,
                            previous.width,
                            wrapS);
                        const std::size_t sourceOffset =
                            (static_cast<std::size_t>(sourceY) *
                                 static_cast<std::size_t>(previous.width) +
                             static_cast<std::size_t>(sourceX)) * 4u;
                        for (int channel = 0; channel < 3; ++channel) {
                            sums[channel] += srgb
                                ? srgbByteToLinear(
                                      previous.rgba[sourceOffset + channel])
                                : static_cast<float>(
                                      previous.rgba[sourceOffset + channel]) /
                                      255.0f;
                        }
                        sums[3] += static_cast<float>(
                            previous.rgba[sourceOffset + 3u]) / 255.0f;
                    }
                }
                const std::size_t targetOffset =
                    (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(next.width) +
                     static_cast<std::size_t>(x)) * 4u;
                for (int channel = 0; channel < 3; ++channel) {
                    const float average = sums[channel] * 0.25f;
                    next.rgba[targetOffset + channel] = srgb
                        ? linearToSrgbByte(average)
                        : static_cast<unsigned char>(std::clamp(
                              static_cast<int>(
                                  std::lround(average * 255.0f)),
                              0,
                              255));
                }
                next.rgba[targetOffset + 3u] =
                    static_cast<unsigned char>(std::clamp(
                        static_cast<int>(
                            std::lround(sums[3] * 0.25f * 255.0f)),
                        0,
                        255));
            }
        }
        chain.push_back(std::move(next));
    }
    return chain;
}

// Keep the owned generated chain alive until upload completes. Explicitly
// called only for material images; shader lookup/mask layouts keep every texel.
struct Selection {
    std::vector<CpuMipLevel> generated;
    std::vector<IRenderBackend::WorldTextureMipLevel> views;
    void reduce(unsigned drop,const unsigned char*& pixels,int& width,int& height,
                int wrapS,int wrapT,bool srgb,
                const IRenderBackend::WorldTextureMipLevel*& levels,std::uint32_t& count) {
        if(drop>2)throw std::invalid_argument("Texture mip drop must be 0, 1 or 2");
        if(!drop)return;
        if(!pixels||width<=0||height<=0)throw std::invalid_argument("Invalid material texture");
        if(levels&&count) {
            if(count>32)throw std::invalid_argument("Invalid material mip count");
            int w=width,h=height;
            for(unsigned i=0;i<count;++i) {
                if(!levels[i].rgba||levels[i].width!=w||levels[i].height!=h)
                    throw std::invalid_argument("Invalid authored material mip chain");
                w=std::max(w/2,1);h=std::max(h/2,1);
            }
        } else {
            generated=buildRgbaMipChain(pixels,width,height,wrapS,wrapT,srgb);
            for(const auto& mip:generated)views.push_back({mip.rgba.data(),mip.width,mip.height});
            levels=views.data();count=static_cast<std::uint32_t>(views.size());
        }
        const auto skip=std::min(drop,count-1);
        levels+=skip;count-=skip;
        pixels=levels[0].rgba;width=levels[0].width;height=levels[0].height;
    }
};
}
