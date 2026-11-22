#version 450
#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_control_flow_attributes : require
#define PHLOSION_DIFFUSE_PROBE_LOOP [[dont_unroll]]
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "portable_material.glsl"
layout(constant_id=1) const bool worldClusteredComposition=false;
#define CLUSTER_FORWARD_FALLBACK (!worldClusteredComposition)

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
    vec4 pointLights[20];
    mat4 directionalShadowMatrix;
    vec4 directionalShadowParams;
    vec4 rectLights[112];
    vec4 shadowAtlasUv;
    vec4 localShadows[120];
    vec4 diffuseProbes[483];
    vec4 planar[6];
    vec4 contact[24];
    vec4 screenReflection[11];
    vec4 localReflection[68];
    vec4 reflectionResponse;
    vec4 reflectionResponseParams;
    vec4 clustered[5];
    vec4 puddles[32];
    vec4 fog[13];
    vec4 wet[11];
    vec4 sky[24];
    vec4 clouds[12];
    vec4 sun[72];
    vec4 skyCloud[72];
} worldView;
layout(set = 1, binding = 6) uniform sampler2D directionalShadowTexture;
layout(set = 1, binding = 7) uniform sampler2D rectLightLut;

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

#define WORLD_SCENE_RADIANCE (worldView.cameraPosition.w > 0.5)
#define ENV_SKY(i) worldView.sky[i]
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
#define ENV_PUDDLE(index) worldView.puddles[index]
#define ENV_REFLECTION_RESPONSE worldView.reflectionResponse
#define ENV_REFLECTION_RESPONSE_PARAMS worldView.reflectionResponseParams
#define ENV_LUT(uv) vec3(0.0)
layout(set=6,binding=13) uniform sampler2D worldFogNear;
layout(set=6,binding=14) uniform sampler2D worldFogFar;
bool worldFogAdditive=false;
#define WORLD_FOG_ADDITIVE worldFogAdditive
#define WORLD_FOG_VIEW(i) worldView.fog[i]
#define WORLD_FOG_SAMPLE(uv,far) ((far) ? textureLod(worldFogFar,uv,0.0) : textureLod(worldFogNear,uv,0.0))

layout(set=6,binding=15) uniform sampler2D worldWetDrops;
layout(set=6,binding=16) uniform sampler2D worldWetLeaks;
layout(set=6,binding=17) uniform sampler2D worldWetRipples;
layout(set=6,binding=18) uniform sampler2D worldWetFields;
layout(set=6,binding=19) uniform sampler2D worldWetRain;
#define WORLD_WET_LOOP [[dont_unroll]]
layout(set=6,binding=20) uniform sampler2D worldCloudShadow;
layout(set=6,binding=21) uniform sampler2D worldCloudResolved;
#define WORLD_CLOUD(i) worldView.clouds[i]
#define WORLD_CLOUD_SHADOW(uv) textureLod(worldCloudShadow,uv,0.0).r
#define WORLD_CLOUD_COLOR(uv) textureLod(worldCloudResolved,uv,0.0)
#define WORLD_WET_VIEW(i) worldView.wet[i]
#define WORLD_WET_DROPS(uv) textureGrad(worldWetDrops,uv,dFdx(uv),dFdy(uv))
#define WORLD_WET_LEAKS(uv) textureGrad(worldWetLeaks,uv,dFdx(uv),dFdy(uv))
#define WORLD_WET_RIPPLES(uv) textureGrad(worldWetRipples,uv,dFdx(uv),dFdy(uv))
#define WORLD_WET_FIELD(p) texelFetch(worldWetFields,p,0)
#define WORLD_WET_RAIN(p) texelFetch(worldWetRain,p,0).r

#include "world_atmosphere.glsl"
layout(set = 2, binding = 0) uniform sampler2D worldPlanarTexture;
#define PLANAR(index) worldView.planar[index]
#define PLANAR_SAMPLE(uv) textureLod(worldPlanarTexture,uv,0.0)
#include "world_planar.glsl"
#define ENV_POINT(index) worldView.pointLights[index]
#define ENV_RECT(index) worldView.rectLights[index]
#define ENV_RECT_LUT(uv) textureLod(rectLightLut, uv, 0.0)
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define ENV_BOUNCE(index) vec4(0.0)
#else
#define ENV_BOUNCE(index) worldView.diffuseProbes[index]
#endif
layout(set=3,binding=0) uniform sampler2D worldContactDepth;
layout(set=5,binding=0) uniform sampler2D worldLocalReflectionTexture;
#define LOCAL_REFLECTION(index) worldView.localReflection[index]
#define LOCAL_REFLECTION_LOOP [[dont_unroll]]
#define LOCAL_REFLECTION_SAMPLE(uv) textureLod(worldLocalReflectionTexture,uv,0.0)
layout(set=4,binding=0) uniform sampler2D worldScreenReflectionTexture;
#define SCREEN_REFLECTION(index) worldView.screenReflection[index]
#define SCREEN_REFLECTION_LOOP [[dont_unroll]]
#define SCREEN_REFLECTION_DEPTH(p) texelFetch(worldContactDepth,ivec2(p),0).r
#define SCREEN_REFLECTION_SAMPLE(uv) textureLod(worldScreenReflectionTexture,uv,0.0)
#define CONTACT(index) worldView.contact[index]
#define CONTACT_LOOP [[dont_unroll]]
#define CONTACT_DEPTH(p) texelFetch(worldContactDepth,ivec2(p),0).r
layout(set=6,binding=0) uniform sampler2D worldContactBuffer;
layout(set=6,binding=1) uniform sampler2D worldContactHistory;
layout(set=6,binding=2) uniform sampler2D worldContactAux;
layout(set=6,binding=3) uniform sampler2D clusteredDiffuse;
layout(set=6,binding=4) uniform sampler2D clusteredSpecular;
layout(set=6,binding=5) uniform sampler2D clusteredDepth;
#define CLUSTER_VIEW(i) ((i)==4 && worldIndirectDraws.states[drawStateIndex].materialParams.x>1.5 ? vec4(0.0) : worldView.clustered[i])
#define CLUSTER_DIFFUSE(p) texelFetch(clusteredDiffuse,p,0)
#define CLUSTER_SPECULAR(p) texelFetch(clusteredSpecular,p,0)
#define CLUSTER_DEPTH(p) texelFetch(clusteredDepth,p,0).r
layout(set=6,binding=6) uniform sampler2D storedAlbedo;
layout(set=6,binding=7) uniform sampler2D storedNormal;
layout(set=6,binding=8) uniform sampler2D storedMaterial;
layout(set=6,binding=9) uniform sampler2D storedEmission;
#define STORED_ALBEDO(p) texelFetch(storedAlbedo,p,0)
#define STORED_NORMAL(p) texelFetch(storedNormal,p,0)
#define STORED_MATERIAL(p) texelFetch(storedMaterial,p,0)
#define STORED_EMISSION(p) texelFetch(storedEmission,p,0)
#define CONTACT_BUFFER_FETCH(p) texelFetch(worldContactBuffer,ivec2(p),0)
#define CONTACT_HISTORY_SAMPLE(uv) textureLod(worldContactHistory,uv,0.0)
#define SCREEN_REFLECTION_PREPARED(p) texelFetch(worldContactHistory,ivec2(p),0)
#define SCREEN_REFLECTION_CONFIDENCE(p) texelFetch(worldContactAux,ivec2(p),0).r
#define SCREEN_REFLECTION_PREPARED_ELIGIBLE (worldIndirectDraws.states[drawStateIndex].materialParams.x<1.5)
#define CONTACT_AUX_FETCH(p) texelFetch(worldContactAux,ivec2(p),0)
layout(std430,set=6,binding=10) readonly buffer WorldGiTransport {vec4 worldGiTransport[];};
layout(std430,set=6,binding=11) readonly buffer WorldGiRadiance {vec4 worldGiRadiance[];};
layout(set=6,binding=12) uniform sampler2D worldIndirectReceiver;
#define WORLD_GI_RECEIVER(p) texelFetch(worldIndirectReceiver,p,0)
#define WORLD_GI_FORWARD (worldIndirectDraws.states[drawStateIndex].materialParams.x>1.5)
#define WORLD_GI_DATA(i) worldGiTransport[i]
#define WORLD_GI_RESULT(i) worldGiRadiance[i]
#define WORLD_GI_LOOP [[dont_unroll]]
#include "world_lighting.glsl"
uint worldLayeredMaterialIndex=0u;
#define WORLD_LAYERED_MATERIAL_INDEX worldLayeredMaterialIndex
#include "world_layered_bindings.glsl"
layout(set=6,binding=23) uniform sampler2D worldSunNoise;
#define WORLD_SUN_NOISE(p) texelFetch(worldSunNoise,ivec2(p),0)
#define WORLD_SUN_PRIVATE
layout(set=6,binding=22) uniform sampler2DShadow worldSunDepth;
#define WORLD_SUN(i) worldView.sun[i]
#define WORLD_SUN_COMPARE(uv,z) textureLod(worldSunDepth,vec3(uv,z),0.0)
#define WORLD_SHADOW_PARAMS worldView.directionalShadowParams
#define WORLD_SHADOW_CLIP(position) (worldView.directionalShadowMatrix * vec4(position, 1.0))
layout(set=1,binding=8) uniform sampler2D localShadowMoments;
#define WORLD_MOMENT_SAMPLE(uv) textureLod(localShadowMoments,uv,0.0).rg
#define WORLD_SHADOW_SAMPLE(uv) textureLod(directionalShadowTexture, uv, 0.0).r
#define WORLD_SHADOW_ATLAS worldView.shadowAtlasUv
#define WORLD_LOCAL_SHADOW(index) worldView.localShadows[index]
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

#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_indirect_declarations.glsl"
#endif
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
layout(depth_greater) out float gl_FragDepth;
#endif
void writeWorldColor(vec4 color) {
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
    gl_FragDepth=worldFoliageRasterDepth(gl_FragCoord.z);
#endif
    if(worldContactPrepass())color=vec4(vertexNormal*inversesqrt(max(dot(vertexNormal,vertexNormal),1e-12))*.5+.5,1.0);
    if(!(ENV_SUN.w>.5 && ENV_RADIANCE.w>.5))
        color=worldLocalReflectionOutput(color,worldPosition,cross(dFdx(worldPosition),dFdy(worldPosition)));
#if defined(PHLOSION_VULKAN_DUAL_SOURCE_BLEND)
    float blendAlpha = clamp(color.a, 0.0, 1.0);
    float quantizedAlpha = floor(blendAlpha * 63.0 + 0.5) / 63.0;
    outColor = vec4(color.rgb, worldLocalReflectionCapture()?color.a:quantizedAlpha);
    outBlendAlpha = vec4(0.0, 0.0, 0.0, blendAlpha);
#else
    outColor = color;
#endif
}

bool sceneColorPostEnabled = false;
uint volumeMaterialIndex = 0u;
#define VOLUME_SAMPLE(uv) texture(baseColorTextures[nonuniformEXT(volumeMaterialIndex)], uv).a
#include "world_volume.glsl"

vec3 encodeWorldSurfaceColor(vec3 linearColor) {
    if (WORLD_SCENE_RADIANCE) return max(linearColor, vec3(0.0));
    return sceneColorPostEnabled
        ? linearColor
        : encodeClampedLinearColor(linearColor);
}



void main() {
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
    gl_FragDepth=gl_FragCoord.z;
#endif
    worldWetScreen=gl_FragCoord.xy;worldWetDepth=gl_FragCoord.z;
    WorldIndirectDrawState portableDraw = worldIndirectDraws.states[drawStateIndex];
    worldLayeredMaterialIndex=portableDraw.specializedLayered.x;
    worldFogAdditive=portableDraw.drawParams.z!=0u;
    if((worldContactPrepass() || worldScreenReflectionCapture() || worldLocalReflectionCapture()) && (portableDraw.materialParams.x>1.5 || (portableDraw.materialParams.w>246.5 && portableDraw.materialParams.w!=254.0)))discard;
    worldReflectionClip(worldPosition,portableDraw.materialParams.w);
    updateEnvironmentDirectionalVisibility(worldPosition, vertexNormal, portableDraw.materialParams.w);
    if (portableDraw.materialParams.w == 249.0) {
        volumeMaterialIndex=portableDraw.drawParams.x;
        writeWorldColor(worldFogTransparent(worldVolumeColor(vec3(vertexSourceUv1,vertexSourceUv2.x),vertexSourceUv2.y,
            portableDraw.specializedTimingFlagsAtlas.x,vertexColor,vertexUv.x),worldPosition));
        return;
    }
    if (portableDraw.materialParams.w >= 249.5 && portableDraw.materialParams.w != 254.0) {
        uint materialIndex = portableDraw.drawParams.x;
        vec4 color = portableMaterial(portableDraw.materialParams.w,
            texture(baseColorTextures[nonuniformEXT(materialIndex)], vertexUv),
            vertexColor, vec3(vertexSourceUv1, vertexSourceUv2.x));
        writeWorldColor(worldFogTransparent(color,worldPosition));
        return;
    }
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
        if (worldReflectionFrontFace(gl_FrontFacing)) discard;
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
    linearColor = environmentHaze(linearColor, worldPosition, worldView.cameraPosition.xyz);
    float toneMappingExposure = ENV_SUN.w > 0.5 ? ENV_AMBIENT.w : 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    vec3 resolvedColor = sceneColorPostEnabled
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));

#endif
}
