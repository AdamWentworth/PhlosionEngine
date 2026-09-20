#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "world_indirect_state.glsl"

layout(set = 0, binding = 0) uniform sampler2D baseColorTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 1) uniform sampler2D normalTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 3) uniform sampler2D occlusionTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 4) uniform sampler2D emissiveTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 5) uniform sampler2D environmentTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 6) uniform sampler2D lightProjectionTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 7) uniform sampler2D projectedShadowTextures[PHLOSION_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 1, binding = 0) uniform WorldViewState {
    vec4 cameraPosition;
    vec4 cameraForward;
    vec4 cameraTarget;
} worldView;

layout(location = 0) in vec2 vertexUv;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexTangent;
layout(location = 4) in vec3 worldPosition;
layout(location = 5) in vec3 vertexGenerated;
layout(location = 6) in vec2 vertexSourceUv1;
layout(location = 7) in vec2 vertexSourceUv2;
layout(location = 8) flat in uint drawStateIndex;
#if defined(PHLOSION_VULKAN_DUAL_SOURCE_BLEND)
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outBlendAlpha;
#else
layout(location = 0) out vec4 outColor;
#endif

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

#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_indirect_declarations.glsl"
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

bool sceneColorPostEnabled = false;

vec3 encodeWorldSurfaceColor(vec3 linearColor) {
    return sceneColorPostEnabled
        ? linearColor
        : encodeClampedLinearColor(linearColor);
}



void main() {
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_indirect_evaluation.glsl"
#else

    WorldIndirectDrawState drawState = worldIndirectDraws.states[drawStateIndex];
    sceneColorPostEnabled = drawState.shadingParams.w > 0.5;
    uint materialIndex = drawState.drawParams.x;
    float alphaMode = drawState.materialParams.x;
    float alphaCutoff = drawState.materialParams.y;
    float materialMode = drawState.materialParams.w;

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    vec2 materialUv = vertexUv;
    float textureDetailLodBias = materialMode >= 1.5 ? drawState.specializedFlipbook1.z : 0.0;
    vec4 sampled = sampleWorldMaterialTexture(
        baseColorTextures[nonuniformEXT(materialIndex)],
        materialUv,
        textureDetailLodBias);
    vec3 linearColor = clamp(sampled.rgb, 0.0, 1.0) * clamp(vertexColor.rgb, 0.0, 1.0);
    vec3 reviewAlbedo = linearColor;
    float alpha = clamp(vertexColor.a * sampled.a, 0.0, 1.0);

    float alphaWindowMin = clamp(drawState.shadingParams.x, 0.0, 1.0);
    float alphaWindowMax = clamp(drawState.shadingParams.y, 0.0, 1.0);
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
        drawState.specializedFlipbook1.w < -100.5;
    float pbrDebugView = explicitMaterialDebug
        ? -drawState.specializedFlipbook1.w - 100.0
        : 0.0;
    int materialDebugFlags = int(
        drawState.specializedTimingFlagsAtlas.y + 0.5);
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
                      normalTextures[nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.5, 0.5, 1.0);
        } else if (pbrDebugView < 4.5) {
            // 4: Roughness channel.
            float roughness = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTextures[
                          nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).g
                : 1.0;
            debugColor = vec3(roughness);
        } else if (pbrDebugView < 5.5) {
            // 5: Metallic channel.
            float metallic = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTextures[
                          nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).b
                : 0.0;
            debugColor = vec3(metallic);
        } else if (pbrDebugView < 6.5) {
            // 6: AO channel.
            float occlusion = (materialDebugFlags & (1 << 2)) != 0
                ? sampleWorldMaterialTexture(
                      occlusionTextures[nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).r
                : 1.0;
            debugColor = vec3(occlusion);
        } else if (pbrDebugView < 7.5) {
            // 7: Emissive sample.
            debugColor = (materialDebugFlags & (1 << 3)) != 0
                ? sampleWorldMaterialTexture(
                      emissiveTextures[nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.0);
        }
        vec3 resolvedDebugColor = drawState.shadingParams.w > 0.5
            ? debugColor
            : linearToSrgb(debugColor);
        writeWorldColor(vec4(resolvedDebugColor, 1.0));
        return;
    }

    vec3 reviewCameraForward = worldView.cameraForward.xyz;
    if (materialMode >= 1.5) {
        linearColor = evaluateWorldMaterial(linearColor, materialUv, worldPosition, vertexNormal, vertexTangent,
            worldView.cameraPosition.xyz, worldView.cameraForward.xyz, worldView.cameraTarget.xyz,
            normalTextures[nonuniformEXT(drawState.drawParams.x)], metallicRoughnessTextures[nonuniformEXT(drawState.drawParams.x)], occlusionTextures[nonuniformEXT(drawState.drawParams.x)], emissiveTextures[nonuniformEXT(drawState.drawParams.x)], environmentTextures[nonuniformEXT(drawState.drawParams.x)], textureDetailLodBias,
            drawState.pbrFactors, drawState.emissiveAndCamera.rgb, -1.0, 1.0, true);
    }
    if (materialMode >= 1.5) {
        linearColor = applyReviewLightingProfile(
            linearColor,
            reviewAlbedo,
            vertexNormal,
            reviewCameraForward);
    }
    const float toneMappingExposure = 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    vec3 resolvedColor = sceneColorPostEnabled
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));

#endif
}
