#pragma once
#include "engine/core/Environment.h"
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace engine::render {
// Windows LUIDs identify the same physical adapter in DXGI and Vulkan.
// They are boot-local identities, not NVIDIA indices or permanent preferences.
inline std::uint64_t parseGpuLuid(std::string_view text) {
    if(text.size()!=16)
        throw std::invalid_argument("PHLOSION_GPU_LUID must contain exactly 16 hexadecimal digits");
    std::uint64_t value=0;
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value,16);
    if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size())
        throw std::invalid_argument("PHLOSION_GPU_LUID must contain exactly 16 hexadecimal digits");
    return value;
}
inline std::optional<std::uint64_t> requestedGpuLuid() {
    const auto text=engine::env::get("PHLOSION_GPU_LUID");
    return text ? std::optional<std::uint64_t>(parseGpuLuid(*text)) : std::nullopt;
}
inline std::string gpuLuidText(std::uint64_t value) {
    std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return out.str();
}
}
