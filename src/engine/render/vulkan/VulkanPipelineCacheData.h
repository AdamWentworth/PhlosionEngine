#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace engine::render::vulkan_backend {
inline constexpr std::size_t kMaxPipelineCacheBytes = 64u * 1024u * 1024u;
inline constexpr std::uint64_t kPipelineCacheFileVersion = 0x50484c4f564b0001ull;
using PipelineCacheFileHeader = std::array<std::uint64_t,4>;
inline std::uint64_t pipelineCacheChecksum(std::span<const std::uint8_t> data) {
    std::uint64_t hash=14695981039346656037ull;
    for(auto byte:data) hash=(hash^byte)*1099511628211ull;
    return hash;
}
inline bool compatiblePipelineCache(std::span<const std::uint8_t> data,const VkPhysicalDeviceProperties& device) {
    if(data.size()<sizeof(VkPipelineCacheHeaderVersionOne)||data.size()>kMaxPipelineCacheBytes) return false;
    VkPipelineCacheHeaderVersionOne header{};std::memcpy(&header,data.data(),sizeof(header));
    return header.headerSize==sizeof(header)&&header.headerVersion==VK_PIPELINE_CACHE_HEADER_VERSION_ONE&&
        header.vendorID==device.vendorID&&header.deviceID==device.deviceID&&
        std::memcmp(header.pipelineCacheUUID,device.pipelineCacheUUID,VK_UUID_SIZE)==0;
}
inline std::span<const std::uint8_t> decodePipelineCache(std::span<const std::uint8_t> file,const VkPhysicalDeviceProperties& device) {
    if(file.size()<sizeof(PipelineCacheFileHeader)||file.size()>kMaxPipelineCacheBytes+sizeof(PipelineCacheFileHeader)) return {};
    PipelineCacheFileHeader header{};std::memcpy(header.data(),file.data(),sizeof(header));
    const auto data=file.subspan(sizeof(header));
    if(header[0]!=kPipelineCacheFileVersion||header[1]!=device.driverVersion||header[2]!=data.size()||
        header[3]!=pipelineCacheChecksum(data)||!compatiblePipelineCache(data,device)) return {};
    return data;
}
inline std::vector<std::uint8_t> encodePipelineCache(std::span<const std::uint8_t> data,const VkPhysicalDeviceProperties& device) {
    if(!compatiblePipelineCache(data,device)) return {};
    const PipelineCacheFileHeader header{kPipelineCacheFileVersion,device.driverVersion,data.size(),pipelineCacheChecksum(data)};
    std::vector<std::uint8_t> file(sizeof(header)+data.size());
    std::memcpy(file.data(),header.data(),sizeof(header));std::memcpy(file.data()+sizeof(header),data.data(),data.size());
    return file;
}
}
