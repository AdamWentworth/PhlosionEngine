#version 450
#extension GL_GOOGLE_include_directive : require
#include "portable_material.glsl"

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 4) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 5) uniform sampler2D environmentTexture;
layout(set = 0, binding = 6) uniform sampler2D lightProjectionTexture;
layout(set = 0, binding = 7) uniform sampler2D projectedShadowTexture;
layout(set = 1, binding = 0) uniform WorldViewState {
    vec4 cameraPosition;
    vec4 cameraForward;
    vec4 cameraTarget;
    vec4 environmentSun;
    vec4 environmentRadiance;
    vec4 environmentAmbient;
    vec4 environmentHaze;
    vec4 environmentHeight;
    vec4 environmentNight;
    vec4 environmentMoon;
    vec4 environmentMoonLight;
    vec4 environmentCelestial;
    vec4 environmentCloudParams;
    vec4 environmentCloudOffsets;
    vec4 environmentCloudTint;
    vec4 weatherParams;
    vec4 weatherShelters[4];
    vec4 weatherShelterHeights;
    vec4 weatherMaterial;
    vec4 pointLights[8];
    mat4 directionalShadowMatrix;
    vec4 directionalShadowParams;
} worldView;
layout(set = 1, binding = 6) uniform sampler2D directionalShadowTexture;
layout(set = 1, binding = 1) uniform WorldSpecializedMaterialState {
    vec4 timingFlagsAtlas;
    vec4 rect0;
    vec4 rect1;
    vec4 flipbook0;
    vec4 flipbook1;
    mat4 projectedShadowMatrix;
    vec4 projectedShadowParams;
    vec4 lightProjectionUvRowU;
    vec4 lightProjectionUvRowV;
} worldSpecializedMaterial;

layout(push_constant) uniform WorldPushConstants {
    mat4 viewProjection;
    vec4 materialParams;
    vec4 shadingParams;
    vec4 pbrFactors;
    vec4 emissiveAndCamera;
} pushData;

layout(location = 0) in vec2 vertexUv;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexTangent;
layout(location = 4) in vec3 worldPosition;
layout(location = 5) in vec3 vertexGenerated;
layout(location = 6) in vec2 vertexSourceUv1;
layout(location = 7) in vec2 vertexSourceUv2;
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
vec4 outColor;
#elif defined(PHLOSION_VULKAN_DUAL_SOURCE_BLEND)
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outBlendAlpha;
#else
layout(location = 0) out vec4 outColor;
#endif

#define ENV_SUN worldView.environmentSun
#define ENV_RADIANCE worldView.environmentRadiance
#define ENV_AMBIENT worldView.environmentAmbient
#define ENV_HAZE worldView.environmentHaze
#define ENV_HEIGHT worldView.environmentHeight
#define ENV_NIGHT worldView.environmentNight
#define ENV_MOON worldView.environmentMoon
#define ENV_MOON_LIGHT worldView.environmentMoonLight
#define ENV_CELESTIAL worldView.environmentCelestial
#define ENV_CLOUD_PARAMS worldView.environmentCloudParams
#define ENV_CLOUD_OFFSETS worldView.environmentCloudOffsets
#define ENV_CLOUD_TINT worldView.environmentCloudTint
#define ENV_WEATHER worldView.weatherParams
#define ENV_SHELTER(index) worldView.weatherShelters[index]
#define ENV_SHELTER_HEIGHT worldView.weatherShelterHeights
#define ENV_WEATHER_MATERIAL worldView.weatherMaterial
#define ENV_LUT(uv) textureLod(baseColorTexture, uv, 0.0).rgb
#include "world_atmosphere.glsl"
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define ENV_POINT(index) vec4(0.0)
#else
#define ENV_POINT(index) worldView.pointLights[index]
#endif
#include "world_lighting.glsl"
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define WORLD_SHADOW_PARAMS vec4(0.0)
#else
#define WORLD_SHADOW_PARAMS worldView.directionalShadowParams
#endif
#define WORLD_SHADOW_CLIP(position) (worldView.directionalShadowMatrix * vec4(position, 1.0))
#define WORLD_SHADOW_SAMPLE(uv) textureLod(directionalShadowTexture, uv, 0.0).r
#define WORLD_SHADOW_Y -1.0
#include "world_shadow.glsl"
#ifndef PHLOSION_PROJECT_MATERIAL
#include "world_material.glsl"
#endif

vec3 rgbToHsv(vec3 color) {
    vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(
        vec4(color.bg, k.wz),
        vec4(color.gb, k.xy),
        step(color.b, color.g));
    vec4 q = mix(
        vec4(p.xyw, color.r),
        vec4(color.r, p.yzx),
        step(p.x, color.r));
    float chroma = q.x - min(q.w, q.y);
    const float epsilon = 1.0e-10;
    return vec3(
        abs(q.z + (q.w - q.y) / (6.0 * chroma + epsilon)),
        chroma / (q.x + epsilon),
        q.x);
}

vec3 hsvToRgb(vec3 hsv) {
    vec3 p = abs(
        fract(hsv.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) *
            6.0 -
        3.0);
    return hsv.z *
        mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), hsv.y);
}

#define VOLUME_SAMPLE(uv) texture(baseColorTexture, uv).a
#include "world_volume.glsl"
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_declarations.glsl"
#endif
void writeWorldColor(vec4 color) {
#if defined(PHLOSION_VULKAN_DUAL_SOURCE_BLEND)
    float blendAlpha = clamp(color.a, 0.0, 1.0);
    float quantizedAlpha = floor(blendAlpha * 63.0 + 0.5) / 63.0;
    outColor = vec4(color.rgb, quantizedAlpha);
    outBlendAlpha = vec4(0.0, 0.0, 0.0, blendAlpha);
#else
    outColor = color;
#endif
}

vec3 encodeWorldSurfaceColor(vec3 linearColor) {
    return pushData.shadingParams.w > 0.5
        ? linearColor
        : encodeClampedLinearColor(linearColor);
}



layout(constant_id = 0) const bool worldVolumeOnly = false;
void main() {
    if (worldVolumeOnly || pushData.materialParams.w == 249.0) {
        writeWorldColor(worldVolumeColor(vec3(vertexSourceUv1,vertexSourceUv2.x),vertexSourceUv2.y,
            worldSpecializedMaterial.timingFlagsAtlas.x,vertexColor,vertexUv.x));
        return;
    }
    updateEnvironmentDirectionalVisibility(worldPosition, vertexNormal, pushData.materialParams.w);
    if (ENV_SUN.w > 0.5 && ENV_RADIANCE.w > 0.5) {
        vec3 mapped = tonemapACESFilmic(environmentSky(worldPosition-worldView.cameraPosition.xyz,worldView.cameraPosition.xyz), ENV_AMBIENT.w);
        vec3 skyColor = pushData.shadingParams.w > 0.5 ? mapped : linearToSrgb(mapped);
        writeWorldColor(vec4(environmentDitherSky(skyColor, worldPosition-worldView.cameraPosition.xyz), 1.0));
        return;
    }
    if (pushData.materialParams.w >= 249.5 && pushData.materialParams.w != 254.0) {
        vec4 color = portableMaterial(pushData.materialParams.w,
            texture(baseColorTexture, vertexUv), vertexColor,
            vec3(vertexSourceUv1, vertexSourceUv2.x));
        writeWorldColor(color);
        return;
    }
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_evaluation.glsl"
#else

    float alphaMode = pushData.materialParams.x;
    float alphaCutoff = pushData.materialParams.y;
    float materialMode = pushData.materialParams.w;

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    vec2 materialUv = vertexUv;
    float textureDetailLodBias = materialMode >= 1.5 ? worldSpecializedMaterial.flipbook1.z : 0.0;
    vec4 sampled = sampleWorldMaterialTexture(
        baseColorTexture, materialUv, textureDetailLodBias);
    vec3 linearColor = clamp(sampled.rgb, 0.0, 1.0) * clamp(vertexColor.rgb, 0.0, 1.0);
    vec3 reviewAlbedo = linearColor;
    float alpha = clamp(vertexColor.a * sampled.a, 0.0, 1.0);

    float alphaWindowMin = clamp(pushData.shadingParams.x, 0.0, 1.0);
    float alphaWindowMax = clamp(pushData.shadingParams.y, 0.0, 1.0);
    if ((alphaWindowMax < 1.0 || alphaWindowMin > 0.0) &&
        (alpha < alphaWindowMin || alpha >= alphaWindowMax)) {
        discard;
    }
    if (alphaMode < 0.5) {
        alpha = clamp(vertexColor.a, 0.0, 1.0);
    } else if (alphaMode < 1.5) {
        if (alpha < alphaCutoff) discard;
        alpha = clamp(vertexColor.a, 0.0, 1.0);
    }

    bool explicitMaterialDebug =
        worldSpecializedMaterial.flipbook1.w < -100.5;
    float pbrDebugView = explicitMaterialDebug
        ? -worldSpecializedMaterial.flipbook1.w - 100.0
        : 0.0;
    int materialDebugFlags = int(
        worldSpecializedMaterial.timingFlagsAtlas.y + 0.5);
    if (materialMode >= 1.5 && pbrDebugView > 0.5) {
        vec3 debugColor = vec3(0.0);
        if (pbrDebugView < 1.5) {
            // 1: Raw base-color texture sample.
            debugColor = clamp(sampled.rgb, 0.0, 1.0);
        } else if (pbrDebugView < 2.5) {
            // 2: Authored tint-resolved albedo without lighting.
            debugColor = clamp(linearColor, 0.0, 1.0);
        } else if (pbrDebugView < 3.5) {
            // 3: Normal map sample.
            debugColor = (materialDebugFlags & (1 << 0)) != 0
                ? sampleWorldMaterialTexture(
                      normalTexture,
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.5, 0.5, 1.0);
        } else if (pbrDebugView < 4.5) {
            // 4: Roughness channel.
            float roughness = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTexture,
                      materialUv,
                      textureDetailLodBias).g
                : 1.0;
            debugColor = vec3(roughness);
        } else if (pbrDebugView < 5.5) {
            // 5: Metallic channel.
            float metallic = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTexture,
                      materialUv,
                      textureDetailLodBias).b
                : 0.0;
            debugColor = vec3(metallic);
        } else if (pbrDebugView < 6.5) {
            // 6: AO channel.
            float occlusion = (materialDebugFlags & (1 << 2)) != 0
                ? sampleWorldMaterialTexture(
                      occlusionTexture,
                      materialUv,
                      textureDetailLodBias).r
                : 1.0;
            debugColor = vec3(occlusion);
        } else if (pbrDebugView < 7.5) {
            // 7: Emissive sample.
            debugColor = (materialDebugFlags & (1 << 3)) != 0
                ? sampleWorldMaterialTexture(
                      emissiveTexture,
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.0);
        }
        vec3 resolvedDebugColor = pushData.shadingParams.w > 0.5
            ? debugColor
            : linearToSrgb(debugColor);
        writeWorldColor(vec4(resolvedDebugColor, 1.0));
        return;
    }

    vec3 reviewCameraForward = worldView.cameraForward.xyz;
    if (materialMode >= 1.5) {
        linearColor = evaluateWorldMaterial(linearColor, materialUv, worldPosition, vertexNormal, vertexTangent,
            worldView.cameraPosition.xyz, worldView.cameraForward.xyz, worldView.cameraTarget.xyz,
            normalTexture, metallicRoughnessTexture, occlusionTexture, emissiveTexture, environmentTexture, textureDetailLodBias,
            pushData.pbrFactors, pushData.emissiveAndCamera.rgb, -1.0, 1.0, true);
    }
    if (materialMode >= 1.5) {
        linearColor = applyReviewLightingProfile(
            linearColor,
            reviewAlbedo,
            vertexNormal,
            reviewCameraForward);
    }
    linearColor = environmentHaze(linearColor, worldPosition, worldView.cameraPosition.xyz);
    float toneMappingExposure = ENV_SUN.w > 0.5 ? ENV_AMBIENT.w : 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    vec3 resolvedColor = pushData.shadingParams.w > 0.5
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));

#endif
}
