#pragma once
// Private v2 transport blocks: explicit little-endian scalars, never native
// structure layouts. CBOR carries the small metadata and bounded byte strings.
#include "WorldIndirectLighting.h"
#include <nlohmann/json.hpp>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace engine::render::indirect_codec {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559 && sizeof(int)==4);
struct Writer {
    std::vector<std::uint8_t> bytes;
    void operator()(std::uint32_t v) { for(unsigned i=0;i<4;++i)bytes.push_back(std::uint8_t(v>>(i*8))); }
    void operator()(int v) { (*this)(std::bit_cast<std::uint32_t>(v)); }
    void operator()(float v) { (*this)(std::bit_cast<std::uint32_t>(v)); }
    void operator()(bool v) { bytes.push_back(v?1:0); }
    void operator()(std::uint8_t v) { bytes.push_back(v); }
    void operator()(const glm::vec3& v) { (*this)(v.x);(*this)(v.y);(*this)(v.z); }
    template<class T,std::size_t N> void operator()(const std::array<T,N>& v) { for(const auto& x:v)(*this)(x); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t offset=0;
    void operator()(std::uint32_t& v) {
        if(bytes.size()-offset<4)throw std::runtime_error("Truncated indirect block");
        v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes[offset++])<<(i*8);
    }
    void operator()(int& v) { std::uint32_t u;(*this)(u);v=std::bit_cast<int>(u); }
    void operator()(float& v) { std::uint32_t u;(*this)(u);v=std::bit_cast<float>(u); }
    void operator()(std::uint8_t& v) {
        if(offset==bytes.size())throw std::runtime_error("Truncated indirect block");
        v=bytes[offset++];
    }
    void operator()(bool& v) { std::uint8_t u;(*this)(u);if(u>1)throw std::runtime_error("Invalid indirect flag");v=u!=0; }
    void operator()(glm::vec3& v) { (*this)(v.x);(*this)(v.y);(*this)(v.z); }
    template<class T,std::size_t N> void operator()(std::array<T,N>& v) { for(auto& x:v)(*this)(x); }
};
template<class A,class T> void fields(A& io,T& v) {
    using V=std::remove_cv_t<T>;
    if constexpr(std::is_same_v<V,IndirectSurfel>) {
        io(v.position);io(v.normal);io(v.albedo);io(v.emission);io(v.closestProbe);io(v.channels);io(v.emissionChannel);io(v.permanentShadow);
    } else if constexpr(std::is_same_v<V,IndirectBrick>) { io(v.center);io(v.first);io(v.count);
    } else if constexpr(std::is_same_v<V,IndirectProbe>) { io(v.position);io(v.first);io(v.count);io(v.sky);io(v.volume);
    } else if constexpr(std::is_same_v<V,IndirectFactor>) { io(v.brick);io(v.weight);
    } else if constexpr(std::is_same_v<V,IndirectTetra>) { io(v.probes);io(v.neighbor);io(v.inverse);io(v.visibility);
    } else if constexpr(std::is_same_v<V,IndirectTerrainSample>) { io(v.normal);io(v.albedo);io(v.height);io(v.sky);
    } else if constexpr(std::is_same_v<V,IndirectTerrainMaterial>) { io(v.roughness);io(v.specular);io(v.dominantWeight);io(v.selectedWeight);io(v.dominantLayer);
    } else { static_assert(std::is_same_v<V,int>);io(v); }
}
template<class T> nlohmann::json encode(const std::vector<T>& values,std::size_t stride) {
    Writer w;w.bytes.reserve(values.size()*stride);
    for(const auto& v:values)fields(w,v);
    if(w.bytes.size()!=values.size()*stride)throw std::logic_error("Indirect block stride");
    return nlohmann::json::binary(std::move(w.bytes));
}
template<class T> void decode(const nlohmann::json& block,std::vector<T>& values,
                             std::size_t stride,std::size_t limit) {
    if(!block.is_binary())throw std::runtime_error("Indirect block must be binary");
    const auto& bytes=block.get_binary();
    if(bytes.size()%stride || bytes.size()/stride>limit)throw std::runtime_error("Indirect block count/stride");
    values.resize(bytes.size()/stride);Reader r{bytes};
    for(auto& v:values)fields(r,v);
    if(r.offset!=bytes.size())throw std::runtime_error("Trailing indirect block data");
}
}
