#include "engine/render/WorldPlanarCapture.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldDirectionalShadowPolicy.h"
#include "engine/render/WorldBlendPolicy.h"
#include "engine/render/WorldSurfaceComposition.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif


engine::render::backend::WorldPreparedViewToken D3D12RenderBackend::prepareWorldViewData(const WorldTextureData& texture) {
#if defined(_WIN32)
    return prepareWorldViewDataInternal(&texture);
#else
    return {};
#endif
}
void D3D12RenderBackend::beginWorldIndexedBatchSubmission() {
#if defined(_WIN32)
    preparedWorldViews_.beginBatch();
#endif
}
void D3D12RenderBackend::endWorldIndexedBatchSubmission() {
#if defined(_WIN32)
    preparedWorldViews_.endBatch();
    if (!preparedWorldViews_.inBatch()) flushWorldDrawPackets();
#endif
}
#if defined(_WIN32)
engine::render::backend::WorldPreparedViewToken D3D12RenderBackend::prepareWorldViewDataInternal(const WorldTextureData* texture) {
    return preparedWorldViews_.prepare(engine::render::WorldPreparedViewInput(texture), [&] {
        if (!worldVsConstantMappedData_) throw std::runtime_error("Prepared view upload buffer unavailable");
        std::array<float,1120+4*engine::render::worldPuddleCapacity*2+sizeof(engine::render::WorldSkyUniforms)/sizeof(float)> data{};
        const auto& packet = worldEnvironmentPackets_.resolve(texture ? &texture->environment : nullptr);
        std::memcpy(data.data(), packet.environment.data(), sizeof(packet.environment));
        std::memcpy(data.data()+1112, packet.reflectionResponse.data(), sizeof(packet.reflectionResponse));
        std::memcpy(data.data()+1120, packet.puddles.data(), sizeof(packet.puddles));
        std::memcpy(data.data()+1120+4*engine::render::worldPuddleCapacity*2,packet.sky.data(),sizeof(packet.sky));
        std::memcpy(data.data()+80, packet.pointLights.data(), sizeof(packet.pointLights));
        std::memcpy(data.data()+180, packet.rectLights.data(), sizeof(packet.rectLights));
        const bool receives = !worldShadowActive_ && texture && texture->environment.enabled && texture->directionalShadowReceiver;
        const auto shadow = worldShadowReady_ && receives
            ? engine::render::packWorldDirectionalShadow(worldShadowView_)
            : engine::render::WorldDirectionalShadowState{};
        std::memcpy(data.data()+160, &shadow, sizeof(shadow));
        const auto atlas = worldLocalShadows_.directionalUv();
        std::memcpy(data.data()+628, atlas.data(), sizeof(atlas));
        if (receives) std::memcpy(data.data()+632, worldLocalShadows_.uniforms.data(), sizeof(worldLocalShadows_.uniforms));
        data[76] = texture ? texture->cameraPosX : 0.f;
        data[77] = texture ? texture->cameraPosY : 7.f;
        data[78] = texture ? texture->cameraPosZ : 9.f;
        const auto offset = alignUp(std::size_t(worldVsConstantFrameOffset_), 256u);
        const auto size = alignUp(sizeof(data), 256u);
        const auto end = std::size_t(worldVsConstantFrameBaseOffset_) + worldVsConstantBufferBytesPerFrame_;
        if (offset + size > end) throw std::runtime_error("Prepared view upload frame budget exceeded");
        std::memcpy(worldVsConstantMappedData_+offset, data.data(), sizeof(data));
        worldVsConstantFrameOffset_ = static_cast<std::uint32_t>(offset+size);
        return PreparedWorldView{worldVsConstantBufferGpuAddress_+offset, packet};
    });
}
const D3D12RenderBackend::PreparedWorldView& D3D12RenderBackend::resolvePreparedWorldView(const WorldTextureData* texture) {
    if (texture) if (auto* view = preparedWorldViews_.get(texture->preparedView)) return *view;
    return *preparedWorldViews_.get(prepareWorldViewDataInternal(texture));
}
#endif

#if defined(_WIN32)
engine::render::d3d12_internal::WorldDiffuseBindings D3D12RenderBackend::prepareWorldDiffuseBindings(bool haze, bool clustered, bool composable, bool additiveFog) {
    engine::render::d3d12_internal::WorldDiffuseBindings bindings;
    bindings.wind=worldWindAddress();
    bindings.layered=worldLayeredAddress();bindings.layeredTextures=worldLayeredTextures();
    using namespace engine::render::d3d12_internal;
    static_assert(sizeof(engine::render::WorldDiffuseProbeUniforms)+sizeof(engine::render::WorldContactUniforms)+sizeof(engine::render::WorldScreenReflectionUniforms)+sizeof(engine::render::WorldLocalReflectionUniforms)+sizeof(engine::render::WorldClusteredConsumerUniforms)+sizeof(engine::render::WorldFogUniforms)+sizeof(engine::render::WorldWetUniforms)+sizeof(engine::render::WorldCloudUniforms)+sizeof(engine::render::WorldSunShadowUniforms)+sizeof(engine::render::WorldCloudRows)<=kDiffuseProbePacketStride);
    auto cluster=worldShadowActive_ || worldSurfaceActive_ || haze || !clustered ? engine::render::WorldClusteredConsumerUniforms{} : worldClusteredView_;
    if(worldSurfaceActive_ && composable)cluster[4][2]=1;
    auto fog=worldShadowActive_ ? engine::render::WorldFogUniforms{} : worldFog_;fog[2][3]=additiveFog?1.f:0.f;
    const auto skyCloud=worldShadowActive_ ? engine::render::WorldCloudRows{} : worldSkyCloud_;
    const auto sun=worldShadowActive_ ? engine::render::WorldSunShadowUniforms{} : worldSunPlan_.uniforms;
    const auto cloud=worldShadowActive_ ? engine::render::WorldCloudUniforms{} : worldClouds_;
    const auto wet=worldShadowActive_ ? engine::render::WorldWetUniforms{} : worldWet_;
    const auto contact=worldShadowActive_ ? engine::render::WorldContactUniforms{} : worldContact_;
    const auto screen=worldShadowActive_ ? engine::render::WorldScreenReflectionUniforms{} : worldScreenReflection_;
    const auto local=worldShadowActive_ ? engine::render::WorldLocalReflectionUniforms{} : worldLocalReflection_;
    if (!worldDiffuseProbeMapped_) throw std::runtime_error("Diffuse probe upload buffer unavailable");
    if (worldDiffuseProbePacketCount_==0 || lastWorldDiffuseProbes_!=worldDiffuseProbes_ || lastWorldContact_!=contact || lastWorldScreenReflection_!=screen || lastWorldLocalReflection_!=local || lastWorldClusteredView_!=cluster || lastWorldFog_!=fog || lastWorldWet_!=wet || lastWorldClouds_!=cloud || lastWorldSun_!=sun || lastWorldSkyCloud_!=skyCloud) {
        // Alpha/additive effects alternate a few view states many times.
        // Reuse immutable packets throughout this frame, not only consecutive draws.
        std::array<std::byte,kDiffuseProbePacketStride> packet{};
        std::memcpy(packet.data(),worldDiffuseProbes_.data(),sizeof(worldDiffuseProbes_));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_),contact.data(),sizeof(contact));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact),screen.data(),sizeof(screen));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact)+sizeof(screen),local.data(),sizeof(local));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact)+sizeof(screen)+sizeof(local),cluster.data(),sizeof(cluster));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact)+sizeof(screen)+sizeof(local)+sizeof(cluster),fog.data(),sizeof(fog));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact)+sizeof(screen)+sizeof(local)+sizeof(cluster)+sizeof(fog),wet.data(),sizeof(wet));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact)+sizeof(screen)+sizeof(local)+sizeof(cluster)+sizeof(fog)+sizeof(wet),cloud.data(),sizeof(cloud));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact)+sizeof(screen)+sizeof(local)+sizeof(cluster)+sizeof(fog)+sizeof(wet)+sizeof(cloud),sun.data(),sizeof(sun));
        std::memcpy(packet.data()+sizeof(worldDiffuseProbes_)+sizeof(contact)+sizeof(screen)+sizeof(local)+sizeof(cluster)+sizeof(fog)+sizeof(wet)+sizeof(cloud)+sizeof(sun),skyCloud.data(),sizeof(skyCloud));
        const auto address=worldDiffusePackets_.resolve(packet,[&]() -> std::optional<std::uint64_t> {
            if(worldDiffuseProbePacketCount_>=kDiffuseProbePacketsPerFrame)return {};
            const auto offset=(frameIndex_*kDiffuseProbePacketsPerFrame+worldDiffuseProbePacketCount_++)*kDiffuseProbePacketStride;
            std::memcpy(worldDiffuseProbeMapped_+offset,packet.data(),packet.size());
            return worldDiffuseProbeBuffer_->GetGPUVirtualAddress()+offset;
        });
        if(!address)throw std::runtime_error("Diffuse probe view budget exceeded");
        worldDiffusePacketAddress_=*address;
        lastWorldSkyCloud_=skyCloud;
        lastWorldSun_=sun;
        lastWorldClouds_=cloud;lastWorldWet_=wet;
        lastWorldFog_=fog;
        lastWorldClusteredView_=cluster;
        lastWorldLocalReflection_=local;
        lastWorldScreenReflection_=screen;
        lastWorldContact_=contact;
        lastWorldDiffuseProbes_=worldDiffuseProbes_;
    }
    bindings.constants=worldDiffusePacketAddress_;
    const auto planar=worldShadowActive_ ? engine::render::WorldPlanarReflectionUniforms{} : worldPlanarReflection_;
    const auto planarAddress=frameMaterialConstants_.resolve(std::as_bytes(std::span(planar)),[&]() -> std::optional<std::uint64_t> {
        const auto planarOffset=alignUp(std::size_t(worldVsConstantFrameOffset_),256u);
        const auto end=std::size_t(worldVsConstantFrameBaseOffset_)+worldVsConstantBufferBytesPerFrame_;
        if(planarOffset+256>end)return {};
        std::memcpy(worldVsConstantMappedData_+planarOffset,planar.data(),sizeof(planar));
        worldVsConstantFrameOffset_=static_cast<std::uint32_t>(planarOffset+256);
        return worldVsConstantBufferGpuAddress_+planarOffset;
    });
    if(!planarAddress)throw std::runtime_error("Planar constant frame budget");
    bindings.planar=*planarAddress;
    const auto descriptor=haze ? worldHazeDescriptor_ : worldPlanarDescriptor_!=0xffffffffu ? worldPlanarDescriptor_ : ensureFallbackSpriteTexture()->descriptorIndex;
    auto handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr+=static_cast<UINT64>(descriptor)*srvDescriptorSize_;
    bindings.tables[(8)-8]=handle;
    const auto momentDescriptor=!worldShadowActive_ && worldMomentSrv_!=0xffffffffu
        ? worldMomentSrv_+2 : ensureFallbackSpriteTexture()->descriptorIndex;
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr+=static_cast<UINT64>(momentDescriptor)*srvDescriptorSize_;
    bindings.tables[(9)-8]=handle;
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    const bool screenDepth=worldScreenReflection_[8][3]>.5f || worldScreenReflection_[8][3]<-1.5f;
    const auto depth=(worldContact_[8][3]>.5f || screenDepth) ? worldDepthSnapshotDescriptor_ : ensureFallbackSpriteTexture()->descriptorIndex;
    handle.ptr+=static_cast<UINT64>(depth)*srvDescriptorSize_;
    bindings.tables[(10)-8]=handle;
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr+=static_cast<UINT64>(worldScreenReflectionDescriptor_!=0xffffffffu ? worldScreenReflectionDescriptor_ : ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
    bindings.tables[(11)-8]=handle;
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr+=static_cast<UINT64>(worldLocalReflectionDescriptor_!=0xffffffffu ? worldLocalReflectionDescriptor_ : ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
    bindings.tables[(12)-8]=handle;
    for(unsigned i=0;i<2;++i) {
        const auto slot=worldFogDescriptors_[i];
        handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr+=UINT64(slot!=0xffffffffu?slot:ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
        bindings.tables[28+i-8]=handle;
    }
    for(unsigned i=0;i<4;++i) {
        const auto slot=worldSurfaceReady_ && !worldSurfaceActive_ ? worldStoredSurfaceDescriptors_[i] : 0xffffffffu;
        handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr+=UINT64(slot!=0xffffffffu?slot:ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
        bindings.tables[(20+i)-8]=handle;
    }
    for(unsigned i=0;i<3;++i) {
        const auto slot=worldClusteredDescriptors_[i];
        handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr+=UINT64(slot!=0xffffffffu?slot:ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
        bindings.tables[(17+i)-8]=handle;
    }
    for(unsigned i=0;i<3;++i) {
        const auto slot=worldContactFilterDescriptors_[i];
        handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr+=static_cast<UINT64>(slot!=0xffffffffu ? slot : ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
        bindings.tables[(13+i)-8]=handle;
    }
    for(unsigned i=0;i<2;++i) {
        const auto slot=worldIndirectDescriptors_[i];
        handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr+=UINT64(slot!=0xffffffffu?slot:ensureWorldIndirectNull())*srvDescriptorSize_;
        bindings.tables[25+i-8]=handle;
    }
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr+=UINT64(worldIndirectReceiver_!=0xffffffffu?worldIndirectReceiver_:ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
    bindings.tables[27-8]=handle;
    auto wetDescriptor=worldWetDescriptor_;
    if(wetDescriptor==0xffffffffu) {
        const auto found=worldWetFallbackDescriptors_.find(worldScopes_.active());
        if(found!=worldWetFallbackDescriptors_.end())wetDescriptor=found->second;
    }
    if(wetDescriptor==0xffffffffu) {
        constexpr unsigned char white[]{255,255,255,255};
        auto* fallback=ensureWorldTextureRaw("__wet_fallback",nullptr,white,1,1,1,1,false);
        if(!fallback)throw std::runtime_error("Wet fallback texture");
        const auto d=fallback->descriptorIndex;
        wetDescriptor=ensureWorldMaterialDescriptorBlock(d,d,d,d,d,d,d,d);
        if(wetDescriptor==0xffffffffu)throw std::runtime_error("Wet fallback descriptors");
        worldWetFallbackDescriptors_[worldScopes_.active()]=wetDescriptor;
    }
    if(worldSunDescriptor_==0xffffffffu) {
        if(nextSrvDescriptorIndex_>=kMaxSrvDescriptors)throw std::runtime_error("Sun descriptor budget");
        worldSunDescriptor_=nextSrvDescriptorIndex_++;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        auto cpu=srvHeap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(worldSunDescriptor_)*srvDescriptorSize_;
        device_->CreateShaderResourceView(nullptr,&srv,cpu);
    }
    {
        std::array<std::uint32_t,5> inputs=worldWetInputs_;
        if(worldWetDescriptor_==0xffffffffu) {
            constexpr unsigned char white[]{255,255,255,255};
            const auto* fallback=ensureWorldTextureRaw("__wet_fallback",nullptr,white,1,1,1,1,false);
            if(!fallback)throw std::runtime_error("Cloud fallback input");inputs.fill(fallback->descriptorIndex);
        }
        wetDescriptor=ensureWorldMaterialDescriptorBlock(inputs[0],inputs[1],inputs[2],inputs[3],inputs[4],
            worldCloudOwner_&&!worldShadowActive_?worldCloudDescriptors_[0]:inputs[0],
            worldCloudOwner_&&!worldShadowActive_?worldCloudDescriptors_[1]:inputs[0],worldSunDescriptor_);
        if(wetDescriptor==0xffffffffu)throw std::runtime_error("Cloud consumer descriptors");
    }
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();handle.ptr+=UINT64(wetDescriptor)*srvDescriptorSize_;
    bindings.tables[30-8]=handle;
    auto* noiseTexture = [&] {
        engine::render::WorldResourceScopeState::SharedResources shared(worldScopes_);
        const auto& noise=engine::render::worldSunShadowNoise();
        return ensureWorldTextureRaw("__temporal_reflection_noise_u16_v2",noise.data(),64,4096,1,1,false);
    }();
    if(!noiseTexture)throw std::runtime_error("Sun blue noise allocation");
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();handle.ptr+=UINT64(noiseTexture->descriptorIndex)*srvDescriptorSize_;
    bindings.tables[31-8]=handle;
    handle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr+=UINT64(worldSkyModelDescriptor_!=0xffffffffu&&!worldShadowActive_?
        worldSkyModelDescriptor_:ensureFallbackSpriteTexture()->descriptorIndex)*srvDescriptorSize_;
    bindings.fogSky=handle;
    return bindings;
}
#endif
void D3D12RenderBackend::bindWorldDiffuseProbes(bool haze, bool clustered, bool composable, bool additiveFog) {
#if defined(_WIN32)
    engine::render::d3d12_internal::recordWorldDiffuseBindings(*commandList_.Get(), prepareWorldDiffuseBindings(haze, clustered, composable, additiveFog));
#else
    (void)haze; (void)clustered; (void)composable;
#endif
}
#if defined(_WIN32)
D3D12_GPU_VIRTUAL_ADDRESS D3D12RenderBackend::prepareWorldMaterialConstants(const void* data, std::size_t viewOffset, bool outline) {
    static_assert(sizeof(WorldPsConstants)<=256);
    (void)outline;
    const auto result=frameMaterialConstants_.resolve(
        {reinterpret_cast<const std::byte*>(data),sizeof(WorldPsConstants)},[&]() -> std::optional<std::uint64_t> {
            // The object's constants already own the first block. Allocate a
            // separate material block only on a new frame/stage value.
            const auto offset=alignUp(std::max(std::size_t(worldVsConstantFrameOffset_),
                viewOffset+kWorldViewConstantStride),256u);
            if(offset+256u>std::size_t(worldVsConstantFrameBaseOffset_)+worldVsConstantBufferBytesPerFrame_)
                throw std::runtime_error("Frame material constant budget exhausted");
            std::memcpy(worldVsConstantMappedData_+offset,data,sizeof(WorldPsConstants));
            worldVsConstantFrameOffset_=static_cast<UINT>(offset+256u);
            return worldVsConstantBufferGpuAddress_+offset;
        });
    if(!result)throw std::runtime_error("Frame material publication failed");
    return *result;
}
#endif
void D3D12RenderBackend::bindWorldMaterialConstants(const void* data, std::size_t viewOffset, bool outline) {
#if defined(_WIN32)
    commandList_->SetGraphicsRootConstantBufferView(1,prepareWorldMaterialConstants(data,viewOffset,outline));
#else
    (void)data;(void)viewOffset;(void)outline;
#endif
}

#if defined(_WIN32)

namespace {
using Clock = std::chrono::steady_clock;

void packWorldVsConstants(const float* viewProjectionMatrix4x4,
                          const float* modelMatrix4x4,
                          bool skinningEnabled,
                          std::uint32_t skinMatrixCount,
                          std::uint32_t skinningMode,
                          float clipSpaceDepthBias,
                          float* out40) {
    if (!out40) return;
    static constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const float* vp = viewProjectionMatrix4x4 ? viewProjectionMatrix4x4 : kIdentity;
    const float* model = modelMatrix4x4 ? modelMatrix4x4 : kIdentity;
    std::memcpy(out40, vp, sizeof(float) * 16u);
    std::memcpy(out40 + 16u, model, sizeof(float) * 16u);
    out40[32] = skinningEnabled ? 1.0f : 0.0f;
    out40[33] = static_cast<float>(skinMatrixCount);
    out40[34] = static_cast<float>(skinningMode);
    out40[35] = 0.0f;
    out40[36] = (clipSpaceDepthBias > 0.0f) ? clipSpaceDepthBias : 0.0f;
    out40[37] = 0.0f;
    out40[38] = 0.0f;
    out40[39] = 0.0f;
}

bool pbrBindingLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PHLOSION_BACKEND_PBR_BIND_LOG");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

int pbrDebugViewMode() {
    static const int mode = []() -> int {
        const auto env = engine::env::get("PHLOSION_BACKEND_PBR_DEBUG_VIEW");
        if (!env.has_value()) return 0;
        try {
            return std::clamp(std::atoi(env->c_str()), 0, 8);
        } catch (...) {
            return 0;
        }
    }();
    return mode;
}

int pbrBindingLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PHLOSION_BACKEND_PBR_BIND_LOG_MAX");
        if (!env.has_value()) return 64;
        try {
            return (std::max)(1, std::atoi(env->c_str()));
        } catch (...) {
            return 64;
        }
    }();
    return maxEntries;
}

constexpr std::uint32_t kMaxGpuSkinMatrices = 128u;

bool hasPerInstanceSkinning(const IRenderBackend::WorldMeshInstance& instance) {
    return instance.gpuSkinning != 0u &&
           instance.skinMatrices != nullptr &&
           instance.skinMatrixCount > 0u;
}

std::size_t skinMatrixFloatCount(std::uint32_t skinMatrixCount, std::uint32_t gpuSkinningMode) {
    return static_cast<std::size_t>(skinMatrixCount) *
           static_cast<std::size_t>(gpuSkinningMode == 1u ? 32u : 16u);
}

float toMs(const Clock::time_point& start, const Clock::time_point& end) {
    return static_cast<float>(
        std::chrono::duration<double, std::milli>(end - start).count());
}

bool worldDrawPerfLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PHLOSION_BACKEND_WORLD_DRAW_PERF_LOG");
        if (!env.has_value()) return false;
        return engine::env::flagEnabled("PHLOSION_BACKEND_WORLD_DRAW_PERF_LOG");
    }();
    return enabled;
}

float worldDrawPerfLogThresholdMs() {
    static const float threshold = []() -> float {
        const auto env = engine::env::get("PHLOSION_BACKEND_WORLD_DRAW_PERF_THRESHOLD_MS");
        if (!env.has_value()) return 0.5f;
        return (std::max)(0.0f, static_cast<float>(std::atof(env->c_str())));
    }();
    return threshold;
}

int worldDrawPerfLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PHLOSION_BACKEND_WORLD_DRAW_PERF_LOG_MAX");
        if (!env.has_value()) return 24;
        return (std::max)(1, std::atoi(env->c_str()));
    }();
    return maxEntries;
}

bool consumeWorldDrawPerfLogSlot() {
    static int emitted = 0;
    if (emitted >= worldDrawPerfLogMaxEntries()) return false;
    ++emitted;
    return true;
}

const char* worldTextureKeyForLog(const IRenderBackend::WorldTextureData* texture) {
    if (!texture) return "<none>";
    if (texture->key && texture->key[0] != '\0') return texture->key;
    if (texture->cacheKey && texture->cacheKey[0] != '\0') return texture->cacheKey;
    return "<generated>";
}

void maybeLogWorldDrawPerf(std::uint64_t frameCounter,
                           const char* path,
                           const char* outcome,
                           const char* reason,
                           const char* geometryKey,
                           const IRenderBackend::WorldTextureData* texture,
                           std::size_t instanceCount,
                           bool hadCachedMesh,
                           float materialPrepMs,
                           float meshPrepareMs,
                           float instancePrepMs,
                           float internalDrawMs,
                           float totalMs) {
    if (!worldDrawPerfLogEnabled() ||
        frameCounter == 0u ||
        totalMs < worldDrawPerfLogThresholdMs() ||
        !consumeWorldDrawPerfLogSlot()) {
        return;
    }

    std::ostringstream msg;
    msg << std::fixed << std::setprecision(3)
        << "[WorldDrawPerf][D3D12] path=" << (path ? path : "<unknown>")
        << " outcome=" << (outcome ? outcome : "<unknown>")
        << " reason=" << (reason ? reason : "ok")
        << " frame=" << frameCounter
        << " geometry=" << ((geometryKey && geometryKey[0] != '\0') ? geometryKey : "<dynamic>")
        << " texture=" << worldTextureKeyForLog(texture)
        << " inst=" << instanceCount
        << " had_cached_mesh=" << (hadCachedMesh ? 1 : 0)
        << " alpha=" << static_cast<int>(texture ? texture->alphaMode : 0u)
        << " blend=" << static_cast<int>(texture ? texture->blendMode : 0u)
        << " material=" << static_cast<int>(texture ? texture->materialMode : 0u)
        << " material_ms=" << materialPrepMs
        << " mesh_ms=" << meshPrepareMs
        << " instance_ms=" << instancePrepMs
        << " internal_ms=" << internalDrawMs
        << " total_ms=" << totalMs;
    std::cout << msg.str() << "\n";
}

void maybeLogPbrBindingD3D12(const IRenderBackend::WorldTextureData* texture,
                             bool hasBase,
                             bool hasNormal,
                             bool hasMetallicRoughness,
                             bool hasOcclusion,
                             bool hasEmissive) {
    if (!pbrBindingLogEnabled()) return;
    if (!texture || texture->materialMode < 2u) return;
    static int sPrinted = 0;
    if (sPrinted >= pbrBindingLogMaxEntries()) return;
    ++sPrinted;
    std::cout
        << "[PBRBind][D3D12] key=" << (texture->key ? texture->key : "<null>")
        << " mode=" << static_cast<int>(texture->materialMode)
        << " has(base/norm/mr/occ/emi)="
        << (hasBase ? "1" : "0") << "/"
        << (hasNormal ? "1" : "0") << "/"
        << (hasMetallicRoughness ? "1" : "0") << "/"
        << (hasOcclusion ? "1" : "0") << "/"
        << (hasEmissive ? "1" : "0")
        << " texSize=" << texture->width << "x" << texture->height
        << " normSize=" << texture->normalWidth << "x" << texture->normalHeight
        << " mrSize=" << texture->metallicRoughnessWidth << "x" << texture->metallicRoughnessHeight
        << " roughF=" << texture->roughnessFactor
        << " metalF=" << texture->metallicFactor
        << " occF=" << texture->occlusionStrength
        << "\n";
}

void packWorldInstanceVertexData(const IRenderBackend::WorldMeshInstance& instance,
                                 WorldInstanceVertexData& out) {
    out.model0x = instance.modelMatrix[0];
    out.model0y = instance.modelMatrix[1];
    out.model0z = instance.modelMatrix[2];
    out.model0w = instance.modelMatrix[3];
    out.model1x = instance.modelMatrix[4];
    out.model1y = instance.modelMatrix[5];
    out.model1z = instance.modelMatrix[6];
    out.model1w = instance.modelMatrix[7];
    out.model2x = instance.modelMatrix[8];
    out.model2y = instance.modelMatrix[9];
    out.model2z = instance.modelMatrix[10];
    out.model2w = instance.modelMatrix[11];
    out.model3x = instance.modelMatrix[12];
    out.model3y = instance.modelMatrix[13];
    out.model3z = instance.modelMatrix[14];
    out.model3w = instance.modelMatrix[15];
    out.colorR = instance.vertexColorMulR;
    out.colorG = instance.vertexColorMulG;
    out.colorB = instance.vertexColorMulB;
    out.colorA = instance.vertexColorMulA;
    out.skinEnabled = 0u;
    out.skinMatrixCount = 0u;
    out.skinningMode = 0u;
    out.skinFloat4Offset = 0u;
}
} // namespace

#endif

#if defined(_WIN32)
unsigned D3D12RenderBackend::worldPipelineVariant(const WorldTextureData* textureData) {
    const auto blendState = engine::render::world_blend::resolve(
        textureData ? textureData->alphaMode : 0u,
        textureData ? textureData->blendMode : 0u,
        !textureData || textureData->depthTestEnabled != 0u,
        textureData && textureData->dualSourceBlendEnabled != 0u,
        true);
    if (!blendState.enabled) return 0;

    if (blendState.mode == engine::render::world_blend::Mode::PremultipliedAdditive)
        return blendState.depthTestEnabled ? 11 : 12;
    if (blendState.dualSourceEnabled) {
        if (blendState.depthTestEnabled) {
            return blendState.mode == engine::render::world_blend::Mode::Additive
                ? 8 : 7;
        }
        return blendState.mode == engine::render::world_blend::Mode::Additive
            ? 10 : 9;
    }

    if (blendState.depthTestEnabled) {
        if (blendState.mode == engine::render::world_blend::Mode::Additive) {
            return 2;
        }
        if (blendState.mode == engine::render::world_blend::Mode::Premultiplied) {
            return 3;
        }
        return 1;
    }
    if (blendState.mode == engine::render::world_blend::Mode::Additive) {
        return 5;
    }
    if (blendState.mode == engine::render::world_blend::Mode::Premultiplied) {
        return 6;
    }
    return 4;
}

ID3D12PipelineState* D3D12RenderBackend::ensureWorldPointLightPipeline(
    float mode, unsigned variant) {
    if (!std::isfinite(mode)) throw std::runtime_error("Non-finite local-lit world material mode.");
    if (worldSurfaceActive_) variant = 64u;
    if (worldSceneColorPassActive_) variant |= 16u;
    if (mode == 2.f && worldSceneColorPassActive_ && screenSourceSpecialization_ &&
        worldScreenReflection_[8][3] == -1.f) variant |= 32u;
    const auto key = std::uint64_t(std::bit_cast<std::uint32_t>(mode)) * 4096 + variant;
    const auto found = worldPointLightPipelines_.find(key);
    if (found != worldPointLightPipelines_.end()) return found->second.Get();
    if (!worldPointLightPipelineFactory_)
        throw std::runtime_error("World local-light pipeline has not been initialized.");
    return worldPointLightPipelines_.emplace(key, worldPointLightPipelineFactory_(mode, variant)).first->second.Get();
}

ID3D12PipelineState* D3D12RenderBackend::selectWorldPipelineState(
    const WorldTextureData* textureData, float materialMode, const engine::render::WorldEnvironmentPacket& packet) {

    if (textureData && worldMaterialProfile_.transparentFamilyIndex(textureData->materialMode) >= 0) {
        const auto variant = worldPipelineVariant(textureData);
        if (variant == 0 || worldSurfaceActive_ || worldShadowActive_ || worldSurfaceComposing_ ||
                worldScreenReflection_[8][3] == 2.f || worldContact_[8][3] > 1.5f ||
                (textureData->characterInkingEnabled || textureData->environment.skyDraw))
            throw std::runtime_error("Transparent material family requires a blended color pass.");
        return ensureWorldPointLightPipeline(materialMode, variant);
    }
    if (worldSurfaceComposing_) return ensureWorldPointLightPipeline(2.f,1024u|(worldSurfaceFoliageComposing_?256u:0u));
    if (worldSurfaceActive_) return ensureWorldPointLightPipeline(materialMode,0);
    if (worldScreenReflection_[8][3]==2.f) return ensureWorldPointLightPipeline(2.f,128u | (worldScreenReflection_[10][3]==8.f?512u:0u));
    if (worldShadowActive_) return worldShadowPipelineState_.Get();
    if (worldContact_[8][3]>1.5f) return worldContactFilterPipelineState_.Get();
    if (textureData && textureData->environment.enabled && textureData->environment.skyDraw &&
        packet.environment[0][3] > 0.5f)
        return worldSceneColorPassActive_ ? worldSceneSkyPipelineState_.Get() : worldSkyPipelineState_.Get();
    const unsigned variant = worldPipelineVariant(textureData);
    if(worldHazeStencilActive_ && (materialMode==246.f || materialMode==248.f)) {
        commandList_->OMSetStencilRef(materialMode==248.f?0:1);
        return ensureWorldPointLightPipeline(materialMode,variant|2048u);
    }
    if((!worldSurfaceCompositionFinished_ || worldSurfaceComposed_) && variant==0 && (materialMode==2.f || (textureData && textureData->materialMode<256 && worldMaterialProfile_.modes[textureData->materialMode].sharedLocalLighting && textureData->environment.localHazeDeferred && worldDiffuseProbes_[0][3]>1.5f)) && worldSceneColorPassActive_ &&
       worldSurfaceReady_ && worldClusteredView_[4][3]>.5f && textureData &&
       textureData->environment.enabled && textureData->directionalShadowReceiver &&
       textureData->alphaMode<2 && (worldPlanarReflection_[4][3]>=0 || worldSurfacePlanar_) &&
       worldLocalReflection_[0][3]>=0 && worldScreenReflection_[8][3]>=0)
        return ensureWorldPointLightPipeline(materialMode,256u);
    if (variant >= 11) return ensureWorldPointLightPipeline(materialMode, variant);
    if (worldContact_[8][3] != 0.0f || worldScreenReflection_[8][3] != 0.0f || worldLocalReflection_[0][3] != 0.0f)
        return ensureWorldPointLightPipeline(materialMode, variant);
    if (materialMode == 249.0f || (textureData && IWorldDepthControl::isSnapshotTexture(textureData->lightProjectionKey)))
        return ensureWorldPointLightPipeline(materialMode, variant);
    // Unlit world surfaces still see illuminated air between them and the eye.
    // The legacy dispatcher compiles rectangle uniforms to constant zero.
    if (textureData && materialMode < 249.5f &&
        packet.environment[18][2] > 0)
        return ensureWorldPointLightPipeline(materialMode, variant);
    if (textureData && materialMode >= 1.5f && (materialMode < 249.5f || materialMode == 254.0f) &&
        !(materialMode > 2.5f && materialMode < 3.5f)) {
        if (textureData->environment.enabled && worldDiffuseProbes_[0][3]>0)
            return ensureWorldPointLightPipeline(materialMode,variant);
        if (worldShadowReady_ && textureData->directionalShadowReceiver && textureData->environment.enabled)
            return ensureWorldPointLightPipeline(materialMode, variant);
        const auto& rectLights = packet.rectLights;
        for (unsigned slot=0;slot<engine::render::worldRectLightCapacity;++slot) {
            const auto& color=rectLights[slot*4+3];
            if (color[0]>0 || color[1]>0 || color[2]>0)
                return ensureWorldPointLightPipeline(materialMode,variant);
        }
        const auto& lights = packet.pointLights;
        for (unsigned slot = 0; slot < 4; ++slot) {
            const auto& color = lights[slot * 2 + 1];
            if (color[0] > 0 || color[1] > 0 || color[2] > 0)
                return ensureWorldPointLightPipeline(materialMode, variant);
        }
    }
    const std::array states{worldPipelineState_.Get(), worldBlendPipelineState_.Get(),
        worldAdditiveBlendPipelineState_.Get(), worldPremultipliedBlendPipelineState_.Get(),
        worldNoDepthBlendPipelineState_.Get(), worldNoDepthAdditiveBlendPipelineState_.Get(),
        worldNoDepthPremultipliedBlendPipelineState_.Get(), worldDualSourceBlendPipelineState_.Get(),
        worldDualSourceAdditiveBlendPipelineState_.Get(), worldNoDepthDualSourceBlendPipelineState_.Get(),
        worldNoDepthDualSourceAdditiveBlendPipelineState_.Get()};
    return worldSceneColorPassActive_ ? worldScenePipelines_[variant].Get() : states[variant];
}
#endif

void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const WorldTextureData* texture,
    const WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
#if defined(_WIN32)
    const bool perfLog = worldDrawPerfLogEnabled();
    Clock::time_point materialPrepStart{};
    if (perfLog) materialPrepStart = Clock::now();
    std::uint32_t materialDescriptorBlockIndex = 0u;
    float useTexture = 0.0f;
    if (!prepareWorldMaterialDescriptorBlock(
            texture,
            /*logPbrBinding=*/false,
            materialDescriptorBlockIndex,
            useTexture)) {
        return;
    }
    const float materialPrepMs = perfLog ? toMs(materialPrepStart, Clock::now()) : 0.0f;

    drawWorldIndexedMeshTexturedCachedPreparedInstanced(
        geometryKey,
        vertices,
        vertexCount,
        indices,
        indexCount,
        materialDescriptorBlockIndex,
        texture,
        useTexture,
        instances,
        instanceCount,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight,
        materialPrepMs);
#else
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)texture;
    (void)instances;
    (void)instanceCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::beginWorldDeformationBatch() {
#if defined(_WIN32)
    if(!recording_)throw std::logic_error("Deformation admission outside a frame");
    frameSkinPalettes_.beginBatch();
#endif
}
void D3D12RenderBackend::cancelWorldDeformationBatch() noexcept {
#if defined(_WIN32)
    frameSkinPalettes_.cancelBatch();
#endif
}
void D3D12RenderBackend::commitWorldDeformationBatch() {uploadPreparedWorldDeformations({},true);}
void D3D12RenderBackend::prepareWorldDeformations(std::span<const engine::render::WorldSkinPalette> p) {
    uploadPreparedWorldDeformations(p,false);
}

void D3D12RenderBackend::uploadPreparedWorldDeformations(
    std::span<const engine::render::WorldSkinPalette> palettes,bool commit) {
#if defined(_WIN32)
    if(palettes.empty() && !commit)return;
    if(!recording_ || !worldSkinMatrixMappedData_)throw std::logic_error("Deformation upload outside a frame");
    if(commit)frameSkinPalettes_.commitBatch();
    else frameSkinPalettes_.prepare(palettes);
#else
    (void)palettes;(void)commit;
#endif
}

#if defined(_WIN32)
bool D3D12RenderBackend::uploadWorldSkinPalette(
    const float* matrices, std::size_t floatCount, std::uint64_t& address,
    engine::render::WorldDeformation version) {
    if(!matrices || !floatCount)return false;
    const auto result=frameSkinPalettes_.resolve(std::as_bytes(std::span(matrices,floatCount)),std::move(version));
    if(!result)return false;
    address=result->value;return true;
}

void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedPreparedInstanced(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    std::uint32_t materialDescriptorBlockIndex,
    const WorldTextureData* texture,
    float useTexture,
    const WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight,
    float materialPrepMs) {
    if(worldSurfaceComposed_ && worldMaterialDebugView()==0 && worldSurfaceCompositionCapture_ && engine::render::worldSurfaceCompositionMaterial(texture,worldCompositionLighting,texture && texture->materialMode<256 && worldMaterialProfile_.modes[texture->materialMode].foliageComposition))return;
    const bool perfLog = worldDrawPerfLogEnabled();
    Clock::time_point totalStart{};
    if (perfLog) totalStart = Clock::now();
    const bool hadCachedMesh =
        perfLog &&
        geometryKey && geometryKey[0] != '\0' &&
        cachedWorldMeshes_.find(worldScopes_.assetKey(geometryKey)) != cachedWorldMeshes_.end();
    float meshPrepareMs = 0.0f;
    float instancePrepMs = 0.0f;
    float internalDrawMs = 0.0f;
    const auto maybeLog = [&](const char* outcome, const char* reason) {
        if (!perfLog) return;
        maybeLogWorldDrawPerf(
            frameCounter_,
            "cached_instanced",
            outcome,
            reason,
            geometryKey,
            texture,
            instanceCount,
            hadCachedMesh,
            materialPrepMs,
            meshPrepareMs,
            instancePrepMs,
            internalDrawMs,
            materialPrepMs + toMs(totalStart, Clock::now()));
    };

    const auto drawFallback=[&] {
        // A serial fallback must select its authored motion record; every draw
        // has SV_InstanceID=0. Retain the ordinary cached/native instanced path.
        auto savedMotion=std::move(worldMotionWords_);
        const auto savedGpu=worldMotionGpu_;
        const auto restore=[&] {worldMotionWords_=std::move(savedMotion);worldMotionGpu_=savedGpu;};
        try {
            for(std::size_t i=0;i<instanceCount;++i) {
                worldMotionWords_.assign(1,{0,0,0,0});worldMotionGpu_=0;
                if(worldSurfaceActive_ && !savedMotion.empty() && i<std::size_t(savedMotion[0][0])) {
                    const auto begin=std::size_t(savedMotion[i+1][0]);
                    const auto count=std::size_t(savedMotion[0][0]);
                    const auto end=i+1<count?std::size_t(savedMotion[i+2][0]):savedMotion.size();
                    worldMotionWords_[0][0]=1;
                    worldMotionWords_.push_back(savedMotion[i+1]);worldMotionWords_[1][0]=2;
                    if(worldMotionWords_[1][3]>0)worldMotionWords_[1][3]+=2.f-float(begin);
                    worldMotionWords_.insert(worldMotionWords_.end(),savedMotion.begin()+begin,savedMotion.begin()+end);
                }
                auto t=texture?*texture:WorldTextureData{};
                const auto& instance=instances[i];
                const auto base=t.modelMatrix;
                for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row) {
                    float value=0;
                    for(unsigned k=0;k<4;++k)value+=base[k*4+row]*instance.modelMatrix[col*4+k];
                    t.modelMatrix[col*4+row]=value;
                }
                t.vertexColorMulR*=instance.vertexColorMulR;t.vertexColorMulG*=instance.vertexColorMulG;
                t.vertexColorMulB*=instance.vertexColorMulB;t.vertexColorMulA*=instance.vertexColorMulA;
                if(instance.gpuSkinning && instance.skinMatrices && instance.skinMatrixCount) {
                    t.gpuSkinning=1;t.gpuSkinningMode=instance.gpuSkinningMode;
                    t.skinMatrices=instance.skinMatrices;t.skinMatrixCount=instance.skinMatrixCount;
                    t.deformation=instance.deformation;
                }
                if(geometryKey && *geometryKey)
                    drawWorldIndexedMeshTexturedCached(geometryKey,vertices,vertexCount,indices,indexCount,
                        &t,viewProjectionMatrix4x4,surfaceWidth,surfaceHeight);
                else drawWorldIndexedMeshTextured(vertices,vertexCount,indices,indexCount,
                        &t,viewProjectionMatrix4x4,surfaceWidth,surfaceHeight);
            }
        } catch(...) {restore();throw;}
        restore();
    };

    if (!instances || instanceCount == 0u) {
        maybeLog("fallback", "empty_instances");
        drawWorldIndexedMeshTexturedCached(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    if (!geometryKey || geometryKey[0] == '\0' || !worldInstanceBuffer_ || !worldInstanceMappedData_) {
        maybeLog("fallback", "uncached_or_no_instance_buffer");
        drawFallback();
        return;
    }

    Clock::time_point meshPrepareStart{};
    if (perfLog) meshPrepareStart = Clock::now();
    CachedWorldMesh* cached =
        ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
    if (perfLog) meshPrepareMs = toMs(meshPrepareStart, Clock::now());
    if (!cached || !cached->valid) {
        if (worldScopes_.active()) return;
        maybeLog("fallback", "mesh_cache_prepare_failed");
        drawFallback();
        return;
    }

    if (instanceCount > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
        maybeLog("fallback", "instance_count_overflow");
        drawFallback();
        return;
    }

    bool anyPerInstanceSkinning = false;
    bool allInstancesCarrySkinning = true;
    for (std::size_t i = 0; i < instanceCount; ++i) {
        const WorldMeshInstance& instance = instances[i];
        const bool instanceHasSkinning = hasPerInstanceSkinning(instance);
        if (instance.gpuSkinning != 0u && !instanceHasSkinning) {
            maybeLog("fallback", "missing_per_instance_skinning");
            drawFallback();
            return;
        }
        anyPerInstanceSkinning |= instanceHasSkinning;
        allInstancesCarrySkinning &= instanceHasSkinning;
        if (!instanceHasSkinning) {
            continue;
        }
        if (instance.skinMatrixCount > kMaxGpuSkinMatrices ||
            instance.gpuSkinningMode > 1u) {
            drawFallback();
            return;
        }
    }
    if (anyPerInstanceSkinning &&
        (!allInstancesCarrySkinning || (texture && texture->gpuSkinning != 0u))) {
        maybeLog("fallback", "mixed_skinning_modes");
        drawFallback();
        return;
    }

    const std::size_t instanceBytes = instanceCount * sizeof(WorldInstanceVertexData);
    const std::size_t instanceWriteOffset = static_cast<std::size_t>(worldInstanceFrameOffset_);
    const std::size_t instanceFrameEnd =
        static_cast<std::size_t>(worldInstanceFrameBaseOffset_) +
        static_cast<std::size_t>(worldInstanceBufferBytesPerFrame_);
    if (instanceWriteOffset + instanceBytes > instanceFrameEnd) {
        maybeLog("fallback", "instance_ring_exhausted");
        drawFallback();
        return;
    }

    static thread_local std::vector<std::uint64_t> skinAddresses;
    if (anyPerInstanceSkinning) {
        skinAddresses.resize(instanceCount);
        for (std::size_t i = 0; i < instanceCount; ++i) {
            if (uploadWorldSkinPalette(instances[i].skinMatrices,
                    skinMatrixFloatCount(instances[i].skinMatrixCount, instances[i].gpuSkinningMode),
                    skinAddresses[i],instances[i].deformation)) continue;
            maybeLog("fallback", "skin_ring_exhausted");
            drawFallback();
            return;
        }
    }

    auto* instanceData =
        reinterpret_cast<WorldInstanceVertexData*>(worldInstanceMappedData_ + instanceWriteOffset);
    Clock::time_point instancePrepStart{};
    if (perfLog) instancePrepStart = Clock::now();
    for (std::size_t i = 0; i < instanceCount; ++i) {
        packWorldInstanceVertexData(instances[i], instanceData[i]);
        if (!anyPerInstanceSkinning) {
            continue;
        }
        const WorldMeshInstance& instance = instances[i];
        const std::uint8_t gpuSkinningMode = std::min<std::uint8_t>(instance.gpuSkinningMode, 1u);
        instanceData[i].skinEnabled = 1u;
        instanceData[i].skinMatrixCount = instance.skinMatrixCount;
        instanceData[i].skinningMode = gpuSkinningMode;
        instanceData[i].skinFloat4Offset = static_cast<std::uint32_t>(
            (skinAddresses[i] - worldSkinMatrixBufferGpuAddress_ -
             worldSkinMatrixFrameBaseOffset_) / (sizeof(float) * 4u));
    }
    const D3D12_GPU_VIRTUAL_ADDRESS instanceDataGpuAddress =
        worldInstanceBufferGpuAddress_ + static_cast<std::uint64_t>(instanceWriteOffset);
    worldInstanceFrameOffset_ =
        static_cast<std::uint32_t>(instanceWriteOffset + instanceBytes);

    IRenderBackend::WorldTextureData perInstanceTexture{};
    const WorldTextureData* drawTexture = texture;
    if (anyPerInstanceSkinning && texture) {
        perInstanceTexture = *texture;
        perInstanceTexture.gpuSkinning = 0u;
        perInstanceTexture.gpuSkinningMode = 0u;
        perInstanceTexture.skinMatrixCount = 0u;
        perInstanceTexture.skinMatrices = nullptr;
        drawTexture = &perInstanceTexture;
    }

    if (perfLog) instancePrepMs = toMs(instancePrepStart, Clock::now());
    Clock::time_point internalDrawStart{};
    if (perfLog) internalDrawStart = Clock::now();
    drawWorldIndexedMeshTexturedCachedInternal(
        *cached,
        vertices,
        vertexCount,
        indices,
        indexCount,
        materialDescriptorBlockIndex,
        drawTexture,
        useTexture,
        viewProjectionMatrix4x4,
        instanceDataGpuAddress,
        static_cast<std::uint32_t>(instanceCount),
        surfaceWidth,
        surfaceHeight);
    if (perfLog) internalDrawMs = toMs(internalDrawStart, Clock::now());
    maybeLog("ok", "record");
}
#endif

void D3D12RenderBackend::drawWorldIndexedMeshInternal(const WorldMeshVertex* vertices,
                                                      std::size_t vertexCount,
                                                      const std::uint32_t* indices,
                                                      std::size_t indexCount,
                                                      std::uint32_t materialDescriptorBlockIndex,
                                                      const WorldTextureData* textureData,
                                                      float useTexture,
                                                      const float* viewProjectionMatrix4x4,
                                                      int surfaceWidth,
                                                      int surfaceHeight) {
#if defined(_WIN32)
    flushWorldDrawPackets();
#endif
#if defined(_WIN32)
    if(worldSurfaceComposed_ && worldMaterialDebugView()==0 && worldSurfaceCompositionCapture_ && engine::render::worldSurfaceCompositionMaterial(textureData,worldCompositionLighting,textureData && textureData->materialMode<256 && worldMaterialProfile_.modes[textureData->materialMode].foliageComposition))return;
    if (worldPlanarReflection_[4][3]<0 && !worldShadowActive_ && !worldSurfaceComposing_ && !engine::render::isWorldLocalHazePass(textureData)) {
        viewProjectionMatrix4x4=worldPlanarReflectionView_.viewProjection.data();
        surfaceWidth=worldPlanarReflectionView_.width; surfaceHeight=worldPlanarReflectionView_.heightPixels;
    }
    if (worldShadowActive_) {
        if (!engine::render::worldDirectionalShadowCaster(textureData)) return;
        viewProjectionMatrix4x4 = worldShadowView_.viewProjection.data();
        surfaceWidth = surfaceHeight = static_cast<int>(worldShadowView_.resolution);
    }
    if (!recording_ || !vertices || !indices || vertexCount == 0 || indexCount == 0 || !viewProjectionMatrix4x4) {
        return;
    }
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ ||
        !worldRootSignature_ ||
        !worldVertexBuffer_ ||
        !worldIndexBuffer_ ||
        !worldVsConstantBuffer_ ||
        !worldSkinMatrixBuffer_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
    }
    if (!worldVertexMappedData_ ||
        !worldIndexMappedData_ ||
        !worldVsConstantMappedData_ ||
        !worldSkinMatrixMappedData_) {
        return;
    }

    const std::size_t maxVertexCapacity =
        static_cast<std::size_t>(worldVertexBufferBytesPerFrame_) / sizeof(WorldVertex);
    const std::size_t maxIndexCapacity =
        static_cast<std::size_t>(worldIndexBufferBytesPerFrame_) / sizeof(std::uint32_t);
    const std::size_t vertexFrameEnd =
        static_cast<std::size_t>(worldVertexFrameBaseOffset_) +
        static_cast<std::size_t>(worldVertexBufferBytesPerFrame_);
    const std::size_t indexFrameEnd =
        static_cast<std::size_t>(worldIndexFrameBaseOffset_) +
        static_cast<std::size_t>(worldIndexBufferBytesPerFrame_);
    const std::size_t vsConstantsFrameEnd =
        static_cast<std::size_t>(worldVsConstantFrameBaseOffset_) +
        static_cast<std::size_t>(worldVsConstantBufferBytesPerFrame_);
    const std::size_t safeVertexCount = (std::min)(vertexCount, maxVertexCapacity);
    const std::size_t safeIndexCount = (std::min)(indexCount, maxIndexCapacity);
    if (safeVertexCount == 0 || safeIndexCount < 3u) return;
    // Refuse truncated indexed draws instead of rendering corrupted geometry when
    // a mesh exceeds the shared world dynamic buffer capacity.
    if (safeVertexCount != vertexCount || safeIndexCount != indexCount) {
        return;
    }

    const std::size_t vertexBytes = safeVertexCount * sizeof(WorldVertex);
    const std::size_t indexBytes = safeIndexCount * sizeof(std::uint32_t);
    if (vertexBytes == 0 || indexBytes == 0) return;
    const std::size_t vertexWriteOffset = alignUp(static_cast<std::size_t>(worldVertexFrameOffset_), 256u);
    const std::size_t indexWriteOffset = alignUp(static_cast<std::size_t>(worldIndexFrameOffset_), 256u);
    const auto& preparedView = resolvePreparedWorldView(textureData);
    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (vertexWriteOffset + vertexBytes > vertexFrameEnd ||
        indexWriteOffset + indexBytes > indexFrameEnd ||
        vsConstantsWriteOffset + kWorldViewConstantStride > vsConstantsFrameEnd) {
        return;
    }

    std::memcpy(worldVertexMappedData_ + vertexWriteOffset, vertices, vertexBytes);
    auto* outIndices = reinterpret_cast<std::uint32_t*>(worldIndexMappedData_ + indexWriteOffset);
    std::memcpy(outIndices, indices, indexBytes);

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    const auto shadowTile = worldShadowActive_ ? worldShadowOffset() : std::array<std::uint32_t,2>{};
    vp.TopLeftX = float(shadowTile[0]); vp.TopLeftY = float(shadowTile[1]);
    D3D12_RECT scissor{LONG(shadowTile[0]), LONG(shadowTile[1]), LONG(shadowTile[0]+surfaceWidth), LONG(shadowTile[1]+surfaceHeight)};
    if(!worldShadowActive_ && worldPlanarReflection_[4][3]<0) {
        const auto bounds=worldHazeActive_
            ? engine::render::worldPlanarHazeScissor(worldPlanarScissor_,worldPlanarReflectionView_,surfaceWidth,surfaceHeight)
            : worldPlanarScissor_;
        scissor={bounds[0],bounds[1],bounds[2],bounds[3]};
    }

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
    if (!worldShadowActive_) { worldLastViewport_ = vp; worldLastScissor_ = scissor; }

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    if(worldSurfaceActive_)bindWorldSurfaceMotion();
    bindWorldDiffuseProbes(textureData && (textureData->materialMode == 246 || textureData->materialMode == 248), textureData && textureData->environment.enabled && textureData->directionalShadowReceiver && textureData->alphaMode<2, worldMaterialDebugView()==0 && worldSurfaceCompositionCapture_ && engine::render::worldSurfaceCompositionMaterial(textureData,worldCompositionLighting,textureData && textureData->materialMode<256 && worldMaterialProfile_.modes[textureData->materialMode].foliageComposition), textureData && (textureData->blendMode==1 || textureData->blendMode==3));
    const float* modelMatrix = textureData ? textureData->modelMatrix.data() : nullptr;
    bool gpuSkinningEnabled =
        textureData &&
        textureData->gpuSkinning != 0u &&
        textureData->skinMatrices != nullptr &&
        textureData->skinMatrixCount > 0u &&
        textureData->skinMatrixCount <= kMaxGpuSkinMatrices;
    const std::uint32_t gpuSkinningMode =
        gpuSkinningEnabled ? std::min<std::uint32_t>(textureData->gpuSkinningMode, 1u) : 0u;
    std::uint32_t gpuSkinMatrixCount = gpuSkinningEnabled ? textureData->skinMatrixCount : 0u;
    D3D12_GPU_VIRTUAL_ADDRESS skinMatrixGpuAddress =
        worldSkinMatrixBufferGpuAddress_ +
        static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_);
    if (gpuSkinningEnabled && !uploadWorldSkinPalette(
            textureData->skinMatrices,
            skinMatrixFloatCount(gpuSkinMatrixCount, gpuSkinningMode),
            skinMatrixGpuAddress,textureData->deformation)) {
        gpuSkinningEnabled = false;
        gpuSkinMatrixCount = 0u;
    }

    float vsConstants[kWorldViewConstantFloats] = {};
    packWorldVsConstants(
        viewProjectionMatrix4x4,
        modelMatrix,
        gpuSkinningEnabled,
        gpuSkinMatrixCount,
        gpuSkinningMode,
        !worldShadowActive_ && textureData ? textureData->clipSpaceDepthBias : 0.0f,
        vsConstants);
    const auto skinFrameAddress=worldSkinMatrixBufferGpuAddress_+
        static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_);
    vsConstants[35]=gpuSkinningEnabled?float((skinMatrixGpuAddress-skinFrameAddress)/16u):0.f;
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    commandList_->SetGraphicsRootConstantBufferView(24, preparedView.address);
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset));
    commandList_->SetGraphicsRootShaderResourceView(2, skinFrameAddress);
    commandList_->SetGraphicsRootShaderResourceView(
        4,
        worldInstanceBufferGpuAddress_ +
            static_cast<std::uint64_t>(worldInstanceFrameBaseOffset_));
    WorldPsConstants worldPs = makeWorldPsConstants(
        textureData, useTexture, (worldSceneColorPassActive_ ? (worldSceneRadianceExposure_ > 0 ? 2.f : 1.f) : 0.f), &worldMaterialProfile_);
    if (textureData && textureData->materialMode >= 2u &&
        worldMaterialDebugView() > 0) {
        worldPs.materialFlipbook1Fps =
            -100.0f - static_cast<float>(worldMaterialDebugView());
    } else if (textureData &&
               textureData->materialMode >= 2u &&
               worldMaterialProfile_.modes[textureData->materialMode].d3d12DebugParameter) {
        // A project may reserve this packed scalar for its own material data.
        worldPs.materialFlipbook1Fps = static_cast<float>(pbrDebugViewMode());
    }
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    materialHandle.ptr += static_cast<SIZE_T>(materialDescriptorBlockIndex) *
                          static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(3, materialHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE displacementHandle = materialHandle;
    displacementHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(5, displacementHandle);
    frameIndexedD3d12DescriptorTableSets_ += 1u;
    ID3D12PipelineState* pso = selectWorldPipelineState(textureData, worldPs.materialMode, preparedView.packet);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(vertexWriteOffset);
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = worldIndexBufferGpuAddress_ + static_cast<std::uint64_t>(indexWriteOffset);
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = static_cast<UINT>(indexBytes);
    commandList_->IASetIndexBuffer(&ibv);

    const bool drawCharacterOutline =
        textureData &&
        !worldShadowActive_ && !worldSurfaceActive_ && textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u &&
        safeVertexCount > 0u;
    // The textured surface must follow the inverted hull because blended
    // character materials do not guarantee a depth write.
    if (drawCharacterOutline) {
        WorldPsConstants outlinePs = makeWorldPsConstants(
            textureData, 0.0f, (worldSceneColorPassActive_ ? (worldSceneRadianceExposure_ > 0 ? 2.f : 1.f) : 0.f), &worldMaterialProfile_);
        outlinePs.materialMode = 3.0f;
        bindWorldMaterialConstants(&outlinePs, vsConstantsWriteOffset, true);
        commandList_->SetPipelineState(
            worldSceneColorPassActive_
                ? worldScenePipelines_[textureData->depthTestEnabled != 0u ? 1 : 4].Get()
                : textureData->depthTestEnabled != 0u
                    ? worldBlendPipelineState_.Get()
                    : worldNoDepthBlendPipelineState_.Get());
        ++frameIndexedD3d12PsoSets_;
        commandList_->DrawIndexedInstanced(
            static_cast<UINT>(safeIndexCount), 1, 0, 0, 0);
        ++frameDrawCalls_;
        frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);
    }
    bindWorldMaterialConstants(&worldPs, vsConstantsWriteOffset);
    commandList_->SetPipelineState(pso);
    ++frameIndexedD3d12PsoSets_;
    commandList_->DrawIndexedInstanced(
        static_cast<UINT>(safeIndexCount), 1, 0, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);

    worldVertexFrameOffset_ = static_cast<UINT>(vertexWriteOffset + vertexBytes);
    worldIndexFrameOffset_ = static_cast<UINT>(indexWriteOffset + indexBytes);
    worldVsConstantFrameOffset_ = std::max(worldVsConstantFrameOffset_,static_cast<UINT>(vsConstantsWriteOffset + kWorldViewConstantStride));
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)materialDescriptorBlockIndex;
    (void)textureData;
    (void)useTexture;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

#if defined(_WIN32)
void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedInternal(
    const CachedWorldMesh& mesh,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    std::uint32_t materialDescriptorBlockIndex,
    const WorldTextureData* textureData,
    float useTexture,
    const float* viewProjectionMatrix4x4,
    D3D12_GPU_VIRTUAL_ADDRESS instanceDataGpuAddress,
    std::uint32_t instanceCount,
    int surfaceWidth,
    int surfaceHeight) {
    if(worldSurfaceComposed_ && worldMaterialDebugView()==0 && worldSurfaceCompositionCapture_ && engine::render::worldSurfaceCompositionMaterial(textureData,worldCompositionLighting,textureData && textureData->materialMode<256 && worldMaterialProfile_.modes[textureData->materialMode].foliageComposition))return;
    if (worldPlanarReflection_[4][3]<0 && !worldShadowActive_ && !worldSurfaceComposing_ && !engine::render::isWorldLocalHazePass(textureData)) {
        viewProjectionMatrix4x4=worldPlanarReflectionView_.viewProjection.data();
        surfaceWidth=worldPlanarReflectionView_.width; surfaceHeight=worldPlanarReflectionView_.heightPixels;
    }
    if (worldShadowActive_) {
        if (!engine::render::worldDirectionalShadowCaster(textureData)) return;
        viewProjectionMatrix4x4 = worldShadowView_.viewProjection.data();
        surfaceWidth = surfaceHeight = static_cast<int>(worldShadowView_.resolution);
    }
    if (!recording_ || !viewProjectionMatrix4x4) return;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    if (!mesh.valid || !mesh.vertexBuffer || !mesh.indexBuffer || mesh.indexCount < 3u) return;
    if (instanceCount == 0u || instanceDataGpuAddress == 0u) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ ||
        !worldRootSignature_ ||
        !worldVsConstantBuffer_ ||
        !worldSkinMatrixBuffer_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
    }
    if (!worldVsConstantMappedData_ || !worldSkinMatrixMappedData_) return;

    engine::render::d3d12_internal::WorldIndexedDrawPacket packet;
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    const auto shadowTile = worldShadowActive_ ? worldShadowOffset() : std::array<std::uint32_t,2>{};
    vp.TopLeftX = float(shadowTile[0]); vp.TopLeftY = float(shadowTile[1]);
    D3D12_RECT scissor{LONG(shadowTile[0]), LONG(shadowTile[1]), LONG(shadowTile[0]+surfaceWidth), LONG(shadowTile[1]+surfaceHeight)};
    if(!worldShadowActive_ && worldPlanarReflection_[4][3]<0) {
        const auto bounds=worldHazeActive_
            ? engine::render::worldPlanarHazeScissor(worldPlanarScissor_,worldPlanarReflectionView_,surfaceWidth,surfaceHeight)
            : worldPlanarScissor_;
        scissor={bounds[0],bounds[1],bounds[2],bounds[3]};
    }

    packet.viewport = vp;
    packet.scissor = scissor;
    if (!worldShadowActive_) { worldLastViewport_ = vp; worldLastScissor_ = scissor; }

    packet.heap = srvHeap_.Get();
    packet.root = worldRootSignature_.Get();
    if(worldSurfaceActive_) packet.motion = prepareWorldSurfaceMotion();
    packet.diffuse = prepareWorldDiffuseBindings(textureData && (textureData->materialMode == 246 || textureData->materialMode == 248), textureData && textureData->environment.enabled && textureData->directionalShadowReceiver && textureData->alphaMode<2, worldMaterialDebugView()==0 && worldSurfaceCompositionCapture_ && engine::render::worldSurfaceCompositionMaterial(textureData,worldCompositionLighting,textureData && textureData->materialMode<256 && worldMaterialProfile_.modes[textureData->materialMode].foliageComposition), textureData && (textureData->blendMode==1 || textureData->blendMode==3));
    const std::size_t vsConstantsFrameEnd =
        static_cast<std::size_t>(worldVsConstantFrameBaseOffset_) +
        static_cast<std::size_t>(worldVsConstantBufferBytesPerFrame_);

    const float* modelMatrix = textureData ? textureData->modelMatrix.data() : nullptr;
    bool gpuSkinningEnabled =
        textureData &&
        textureData->gpuSkinning != 0u &&
        textureData->skinMatrices != nullptr &&
        textureData->skinMatrixCount > 0u &&
        textureData->skinMatrixCount <= kMaxGpuSkinMatrices;
    const std::uint32_t gpuSkinningMode =
        gpuSkinningEnabled ? std::min<std::uint32_t>(textureData->gpuSkinningMode, 1u) : 0u;
    std::uint32_t gpuSkinMatrixCount = gpuSkinningEnabled ? textureData->skinMatrixCount : 0u;
    D3D12_GPU_VIRTUAL_ADDRESS skinMatrixGpuAddress =
        worldSkinMatrixBufferGpuAddress_ +
        static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_);
    if (gpuSkinningEnabled && !uploadWorldSkinPalette(
            textureData->skinMatrices,
            skinMatrixFloatCount(gpuSkinMatrixCount, gpuSkinningMode),
            skinMatrixGpuAddress,textureData->deformation)) {
        gpuSkinningEnabled = false;
        gpuSkinMatrixCount = 0u;
    }

    const auto& preparedView = resolvePreparedWorldView(textureData);
    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (vsConstantsWriteOffset + kWorldViewConstantStride > vsConstantsFrameEnd) return;

    float vsConstants[kWorldViewConstantFloats] = {};
    packWorldVsConstants(
        viewProjectionMatrix4x4,
        modelMatrix,
        gpuSkinningEnabled,
        gpuSkinMatrixCount,
        gpuSkinningMode,
        !worldShadowActive_ && textureData ? textureData->clipSpaceDepthBias : 0.0f,
        vsConstants);
    const auto skinFrameAddress=worldSkinMatrixBufferGpuAddress_+
        static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_);
    vsConstants[35]=gpuSkinningEnabled?float((skinMatrixGpuAddress-skinFrameAddress)/16u):0.f;
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    packet.view = preparedView.address;
    packet.object = worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset);
    packet.skin = skinFrameAddress;
    packet.instances = instanceDataGpuAddress;
    WorldPsConstants worldPs = makeWorldPsConstants(
        textureData, useTexture, (worldSceneColorPassActive_ ? (worldSceneRadianceExposure_ > 0 ? 2.f : 1.f) : 0.f), &worldMaterialProfile_);
    if (textureData && textureData->materialMode >= 2u &&
        worldMaterialDebugView() > 0) {
        worldPs.materialFlipbook1Fps =
            -100.0f - static_cast<float>(worldMaterialDebugView());
    } else if (textureData &&
               textureData->materialMode >= 2u &&
               worldMaterialProfile_.modes[textureData->materialMode].d3d12DebugParameter) {
        // Respect project-owned packed parameters; see the
        // uncached path above.
        worldPs.materialFlipbook1Fps = static_cast<float>(pbrDebugViewMode());
    }
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    materialHandle.ptr += static_cast<SIZE_T>(materialDescriptorBlockIndex) *
                          static_cast<SIZE_T>(srvDescriptorSize_);
    packet.material = materialHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE displacementHandle = materialHandle;
    displacementHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_);
    packet.displacement = displacementHandle;
    frameIndexedD3d12DescriptorTableSets_ += 1u;

    ID3D12PipelineState* pso = selectWorldPipelineState(textureData, worldPs.materialMode, preparedView.packet);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = mesh.vertexGpuAddress;
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(mesh.vertexBytes);
    packet.vertices = vbv;

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = mesh.indexGpuAddress;
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = static_cast<UINT>(mesh.indexBytes);
    packet.indices = ibv;

    const bool drawCharacterOutline =
        textureData &&
        !worldShadowActive_ && !worldSurfaceActive_ && textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u;
    // Keep the cached path in the same outline-then-surface order.
    if (drawCharacterOutline) {
        WorldPsConstants outlinePs = makeWorldPsConstants(
            textureData, 0.0f, (worldSceneColorPassActive_ ? (worldSceneRadianceExposure_ > 0 ? 2.f : 1.f) : 0.f), &worldMaterialProfile_);
        outlinePs.materialMode = 3.0f;
        packet.outlineConstants = prepareWorldMaterialConstants(&outlinePs, vsConstantsWriteOffset, true);
        packet.outlinePipeline =
            worldSceneColorPassActive_
                ? worldScenePipelines_[textureData->depthTestEnabled != 0u ? 1 : 4].Get()
                : textureData->depthTestEnabled != 0u
                    ? worldBlendPipelineState_.Get()
                    : worldNoDepthBlendPipelineState_.Get();
        ++frameIndexedD3d12PsoSets_;
        ++frameDrawCalls_;
        frameTriangles_ += static_cast<std::uint64_t>(mesh.indexCount / 3u) *
                           static_cast<std::uint64_t>(instanceCount);
    }
    packet.surfaceConstants = prepareWorldMaterialConstants(&worldPs, vsConstantsWriteOffset);
    packet.surfacePipeline = pso;
    ++frameIndexedD3d12PsoSets_;
    packet.indexCount = static_cast<UINT>(mesh.indexCount);
    packet.instanceCount = instanceCount;
    submitWorldDrawPacket(packet);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(mesh.indexCount / 3u) *
                       static_cast<std::uint64_t>(instanceCount);

    worldVsConstantFrameOffset_ = std::max(worldVsConstantFrameOffset_,static_cast<UINT>(vsConstantsWriteOffset + kWorldViewConstantStride));
}
#endif

#if defined(_WIN32)
D3D12_GPU_VIRTUAL_ADDRESS D3D12RenderBackend::prepareWorldSurfaceMotion() {
    if(!worldMotionGpu_) {
        worldMotionGpu_=frameSkinPalettes_.uploadTransient(std::as_bytes(std::span(worldMotionWords_)));
    }
    return worldMotionGpu_;
}
#endif
void D3D12RenderBackend::bindWorldSurfaceMotion() {
#if defined(_WIN32)
    commandList_->SetGraphicsRootShaderResourceView(16,prepareWorldSurfaceMotion());
#endif
}

engine::render::backend::WorldPreparedViewStats D3D12RenderBackend::worldPreparedViewStats() const {
#if defined(_WIN32)
    return preparedWorldViews_.stats;
#else
    return {};
#endif
}

void D3D12RenderBackend::invalidatePreparedWorldViews() {
#if defined(_WIN32)
    flushWorldDrawPackets();
    preparedWorldViews_.invalidate();
#endif
}

#if defined(_WIN32)
void D3D12RenderBackend::submitWorldDrawPacket(const WorldIndexedDrawPacket& packet) {
    if(!preparedWorldViews_.inBatch()) {
        flushWorldDrawPackets();
        recordWorldIndexedDraw(*commandList_.Get(),packet);
        ++worldRecordingStats_.serialPackets;
        return;
    }
    if(!worldDrawPackets_.empty()) {
        const auto& first=worldDrawPackets_.front();
        if(first.heap!=packet.heap || first.root!=packet.root ||
           std::memcmp(&first.viewport,&packet.viewport,sizeof(packet.viewport)) ||
           std::memcmp(&first.scissor,&packet.scissor,sizeof(packet.scissor)))
            flushWorldDrawPackets();
    }
    worldDrawPackets_.push_back(packet);
    if(worldDrawPackets_.size()==engine::render::worldRecordingMaxPackets)flushWorldDrawPackets();
}
void D3D12RenderBackend::resetWorldRecordingFrame() {
    if(!worldDrawPackets_.empty())throw std::logic_error("Unsubmitted world recording batch");
    worldRecordingStats_={};
    for(auto& lane:worldRecordingFrames_[frameIndex_]) {
        if(lane.allocator && FAILED(lane.allocator->Reset()))
            throw std::runtime_error("World bundle allocator reset failed");
        lane.used=0;
    }
}
void D3D12RenderBackend::flushWorldDrawPackets() {
    using namespace engine::render;
    if(worldDrawPackets_.empty())return;
    const auto setupStart=WorldRecordingWorkers::Clock::now();
    auto& lanes=worldRecordingFrames_[frameIndex_];
    if(worldDrawPackets_.size()<worldRecordingMinPackets || lanes[0].used==worldRecordingListsPerLane) {
        WorldCommandRecorder<ID3D12GraphicsCommandList> recorder(*commandList_.Get());
        for(const auto& packet:worldDrawPackets_)recordWorldIndexedDraw(recorder,packet);
        worldRecordingStats_.bindingWrites+=recorder.writes;
        worldRecordingStats_.bindingReuses+=recorder.reuses;
        worldRecordingStats_.serialPackets+=worldDrawPackets_.size();
        ++worldRecordingStats_.serialBatches;
        worldRecordingStats_.serialMs+=std::chrono::duration<double,std::milli>(WorldRecordingWorkers::Clock::now()-setupStart).count();
        worldDrawPackets_.clear();
        return;
    }
    if(!worldRecordingWorkers_)worldRecordingWorkers_=std::make_unique<WorldRecordingWorkers>();
    std::array<ID3D12GraphicsCommandList*,2> lists{};
    for(unsigned i=0;i<2;++i) {
        auto& lane=lanes[i];
        if(!lane.allocator && FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE,
            IID_PPV_ARGS(lane.allocator.GetAddressOf()))))throw std::runtime_error("Create world bundle allocator failed");
        if(lane.used==lane.lists.size()) {
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
            if(FAILED(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_BUNDLE,lane.allocator.Get(),nullptr,
                IID_PPV_ARGS(list.GetAddressOf()))))throw std::runtime_error("Create world bundle failed");
            lane.lists.push_back(std::move(list));
        } else if(FAILED(lane.lists[lane.used]->Reset(lane.allocator.Get(),nullptr)))
            throw std::runtime_error("Reset world bundle failed");
        lists[i]=lane.lists[lane.used++].Get();
    }
    const auto runStart=WorldRecordingWorkers::Clock::now();
    WorldRecordingWorkers::RunStats run;
    std::array<std::array<std::uint64_t,2>,2> bindings{};
    const auto times=worldRecordingWorkers_->run([&](unsigned lane) {
        const auto begin=worldDrawPackets_.size()*lane/2, end=worldDrawPackets_.size()*(lane+1)/2;
        WorldCommandRecorder<ID3D12GraphicsCommandList> recorder(*lists[lane]);
        for(auto i=begin;i<end;++i)recordWorldIndexedDraw(recorder,worldDrawPackets_[i],true);
        bindings[lane]={recorder.writes,recorder.reuses};
        if(FAILED(lists[lane]->Close()))throw std::runtime_error("Close world bundle failed");
    },&run);
    const auto executeStart=WorldRecordingWorkers::Clock::now();
    const auto& first=worldDrawPackets_.front();
    commandList_->RSSetViewports(1,&first.viewport);
    commandList_->RSSetScissorRects(1,&first.scissor);
    auto* heap=first.heap;commandList_->SetDescriptorHeaps(1,&heap);
    // Completion order never changes authored order, including transparent draws.
    for(auto* list:lists)commandList_->ExecuteBundle(list);
    for(const auto& count:bindings) {
        worldRecordingStats_.bindingWrites+=count[0];
        worldRecordingStats_.bindingReuses+=count[1];
    }
    worldRecordingStats_.add(worldDrawPackets_.size(),times);
    worldRecordingStats_.addRun(run,
        std::chrono::duration<double,std::milli>(runStart-setupStart).count(),
        std::chrono::duration<double,std::milli>(WorldRecordingWorkers::Clock::now()-executeStart).count());
    worldDrawPackets_.clear();
}
#endif

engine::render::WorldDeformationPreparationStats D3D12RenderBackend::worldDeformationPreparationStats() const {
#if defined(_WIN32)
    const auto& s=frameSkinPalettes_.stats();
    return {s.preparedUploads,s.preparedBytes,s.preparationJoins,s.uploads,s.uploadBytes};
#else
    return {};
#endif
}
