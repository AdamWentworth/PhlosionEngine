#include "engine/render/vulkan/VulkanPipelineCacheData.h"
#include <iostream>
#include <stdexcept>
using namespace engine::render::vulkan_backend;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() try {
    VkPhysicalDeviceProperties device{};device.vendorID=4318;device.deviceID=42;device.driverVersion=7;device.pipelineCacheUUID[0]=3;
    VkPipelineCacheHeaderVersionOne header{sizeof(VkPipelineCacheHeaderVersionOne),VK_PIPELINE_CACHE_HEADER_VERSION_ONE,4318,42,{3}};
    std::vector<std::uint8_t> data(sizeof(header)+128,23);std::memcpy(data.data(),&header,sizeof(header));
    const auto file=encodePipelineCache(data,device);
    const auto decoded=decodePipelineCache(file,device);
    check(decoded.size()==data.size()&&std::memcmp(decoded.data(),data.data(),data.size())==0,"Round-trip cache changed");
    for(std::size_t size:{0u,7u,31u,32u,64u}) check(decodePipelineCache(std::span(file).first(size),device).empty(),"Truncated cache accepted");
    auto corrupt=file;corrupt.back()^=1;check(decodePipelineCache(corrupt,device).empty(),"Corrupt payload reached driver");
    corrupt=file;corrupt[0]^=1;check(decodePipelineCache(corrupt,device).empty(),"Unknown file version accepted");
    auto other=device;++other.driverVersion;check(decodePipelineCache(file,other).empty(),"Different driver accepted");
    other=device;++other.deviceID;check(decodePipelineCache(file,other).empty(),"Different GPU accepted");
    other=device;++other.vendorID;check(decodePipelineCache(file,other).empty(),"Different vendor accepted");
    other=device;++other.pipelineCacheUUID[0];check(decodePipelineCache(file,other).empty(),"Different cache UUID accepted");
    header.headerSize=0;std::memcpy(data.data(),&header,sizeof(header));
    check(encodePipelineCache(data,device).empty(),"Invalid native header accepted");
    std::cout<<"PASS pipeline cache payload integrity, version, truncation and device/driver compatibility\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
