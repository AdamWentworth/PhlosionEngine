#pragma once
#include "engine/render/WorldSunShadows.h"
#include "engine/render/WorldWetSurface.h"

#include <array>
#include "engine/render/WorldEnvironmentPacketCache.h"
#include "engine/render/WorldClusteredLighting.h"
#include <cstddef>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"
#include "engine/render/IWorldLocalShadow.h"
#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/IWorldPlanarReflection.h"
#include "engine/render/IWorldContactVisibility.h"
#include "engine/render/IWorldScreenReflection.h"
#include "engine/render/IWorldLocalReflection.h"
#include "engine/render/IWorldPrecipitationLighting.h"

namespace engine::render::vulkan_backend {

struct alignas(16) WorldViewState {
    std::array<float, 4> cameraPosition{0.0f, 7.0f, 9.0f, 0.0f};
    std::array<float, 4> cameraForward{0.0f, -0.6139406f, -0.7893522f, 0.0f};
    std::array<float, 4> cameraTarget{0.0f, -1.0f, 0.0f, 0.0f};
    engine::render::WorldEnvironmentUniforms environment{};
    engine::render::WorldPointLightUniforms pointLights{};
    engine::render::WorldDirectionalShadowState directionalShadow{};
    engine::render::WorldRectLightUniforms rectLights{};
    std::array<float,4> shadowAtlasUv{0,0,1,1};
    engine::render::WorldLocalShadowUniforms localShadows{};
    engine::render::WorldDiffuseProbeUniforms diffuseProbes{};
    engine::render::WorldPlanarReflectionUniforms planar{};
    engine::render::WorldContactUniforms contact{};
    engine::render::WorldScreenReflectionUniforms screenReflection{};
    engine::render::WorldLocalReflectionUniforms localReflection{};
    engine::render::WorldReflectionResponseUniforms reflectionResponse{};
    engine::render::WorldClusteredConsumerUniforms clustered{};
    engine::render::WorldPuddleUniforms puddles{};
    engine::render::WorldFogUniforms fog{};
    engine::render::WorldWetUniforms wet{};
    engine::render::WorldSkyUniforms sky{};
    engine::render::WorldCloudUniforms clouds{};
    engine::render::WorldSunShadowUniforms sun{};
    engine::render::WorldCloudRows skyCloud{};
};

static_assert(std::is_standard_layout_v<WorldViewState>);
static_assert(sizeof(WorldViewState) == 4576u + sizeof(engine::render::WorldDiffuseProbeUniforms) + sizeof(engine::render::WorldContactUniforms) + sizeof(engine::render::WorldScreenReflectionUniforms) + sizeof(engine::render::WorldLocalReflectionUniforms) + sizeof(engine::render::WorldReflectionResponseUniforms) + sizeof(engine::render::WorldClusteredConsumerUniforms) + sizeof(engine::render::WorldPuddleUniforms)+sizeof(engine::render::WorldFogUniforms)+sizeof(engine::render::WorldWetUniforms)+sizeof(engine::render::WorldSkyUniforms)+sizeof(engine::render::WorldCloudUniforms)+sizeof(engine::render::WorldSunShadowUniforms)+sizeof(engine::render::WorldCloudRows));
static_assert(offsetof(WorldViewState, cameraPosition) == 0u);
static_assert(offsetof(WorldViewState, cameraForward) == 16u);
static_assert(offsetof(WorldViewState, cameraTarget) == 32u);
static_assert(offsetof(WorldViewState, environment) == 48u);
static_assert(offsetof(WorldViewState, pointLights) == 352u);
static_assert(offsetof(WorldViewState, directionalShadow) == 672u);
static_assert(offsetof(WorldViewState, rectLights) == 752u);
static_assert(offsetof(WorldViewState, contact) == 12304u); // contact_filter.frag
static_assert(offsetof(WorldViewState, screenReflection) == 12688u); // surface_reflection.frag

inline WorldViewState makeWorldViewState(
    const backend::WorldTextureData* texture, WorldEnvironmentPacketCache* packets = nullptr) {
    WorldViewState out;
    if (!texture) return out;

    out.cameraPosition = {
        texture->cameraPosX,
        texture->cameraPosY,
        texture->cameraPosZ,
        0.0f};
    out.cameraForward = {
        texture->cameraForwardX,
        texture->cameraForwardY,
        texture->cameraForwardZ,
        0.0f};
    out.cameraTarget = {
        texture->cameraTargetX,
        texture->cameraTargetY,
        texture->cameraTargetZ,
        0.0f};
    const auto fresh = packets ? WorldEnvironmentPacket{} : packWorldEnvironmentPacket(texture->environment);
    const auto& packet = packets ? packets->resolve(texture->environment) : fresh;
    out.environment = packet.environment;
    out.reflectionResponse = packet.reflectionResponse;
    out.pointLights = packet.pointLights;
    out.rectLights = packet.rectLights;
    out.puddles = packet.puddles;
    out.sky = packet.sky;
    return out;
}

} // namespace engine::render::vulkan_backend
