#version 450
#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_control_flow_attributes : require
#define PHLOSION_DIFFUSE_PROBE_LOOP [[dont_unroll]]
#extension GL_GOOGLE_include_directive : require
layout(constant_id = 3) const int worldFixedMaterialMode = -1;
#define WORLD_MATERIAL_MODE (worldFixedMaterialMode >= 0 ? float(worldFixedMaterialMode) : pushData.materialParams.w)
#include "portable_material.glsl"
layout(constant_id=1) const bool worldClusteredComposition=false;
#ifdef PHLOSION_WORLD_SURFACE_COMPOSITION
#define CLUSTER_FORWARD_FALLBACK false
#else
#define CLUSTER_FORWARD_FALLBACK (!worldClusteredComposition)
#endif

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
    uvec4 layered;
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
#if defined(PHLOSION_WORLD_SURFACE_CAPTURE)
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSurfaceNormal;
layout(location = 2) out vec4 outSurfaceMaterial;
layout(location = 3) out vec4 outSurfaceEmission;
layout(location=4) out vec4 outSurfaceMotion;
layout(location=5) out uvec2 outSurfaceIdentity;
layout(location=8) in vec4 motionCurrentClip;
layout(location=9) in vec4 motionPreviousClip;
layout(location=10) flat in uint motionObjectId;
layout(location=11) flat in uint motionHistoryValid;
#elif defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
vec4 outColor;
#elif defined(PHLOSION_VULKAN_DUAL_SOURCE_BLEND)
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outBlendAlpha;
#else
layout(location = 0) out vec4 outColor;
#endif

#ifdef PHLOSION_PROBE_RELIGHT
layout(set=6,binding=3) uniform usampler2D clusteredDiffuse;
#else
layout(set=6,binding=3) uniform sampler2D clusteredDiffuse;
#endif
layout(set=6,binding=4) uniform sampler2D clusteredSpecular;
layout(set=6,binding=5) uniform sampler2D clusteredDepth;
#define CLUSTER_VIEW(i) worldView.clustered[i]
#define CLUSTER_DIFFUSE(p) vec4(texelFetch(clusteredDiffuse,p,0))
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
#define WORLD_SCENE_RADIANCE (worldView.cameraPosition.w > 0.5)
#define ENV_ATLAS_RGBA(uv) textureLod(baseColorTexture,uv,0.0)
#define ENV_SKY_MODEL(uv) textureLod(lightProjectionTexture,uv,0.0).rgb
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
#define WORLD_PROBE_CAPTURE (worldView.screenReflection[10].w == -8.0)
#define ENV_LUT(uv) textureLod(baseColorTexture, uv, 0.0).rgb
layout(set=6,binding=13) uniform sampler2D worldFogNear;
layout(set=6,binding=14) uniform sampler2D worldFogFar;
layout(set=6,binding=24) uniform sampler2D worldFogSky;
#define WORLD_FOG_SKY_ATLAS(uv) textureLod(worldFogSky,uv,0.0).rgb
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
#define SKY_CLOUD(i) worldView.skyCloud[i]
#define SKY_CLOUD_SHADOW(uv) textureLod(worldCloudShadow,uv,0.0)
#define SKY_CLOUD_PRIVATE
#define SKY_CLOUD_LOOP [[dont_unroll]]
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
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
// Shadow descriptor uploads already clear reflection/contact state.
#define PLANAR(index) vec4(0.0)
#else
#define PLANAR(index) worldView.planar[index]
#endif
#define PLANAR_SAMPLE(uv) textureLod(worldPlanarTexture,uv,0.0)
#include "world_planar.glsl"
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define ENV_POINT(index) vec4(0.0)
#else
#define ENV_POINT(index) worldView.pointLights[index]
#endif
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define ENV_RECT(index) vec4(0.0)
#else
#define ENV_RECT(index) worldView.rectLights[index]
#endif
#define ENV_RECT_LUT(uv) textureLod(rectLightLut, uv, 0.0)
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define ENV_BOUNCE(index) vec4(0.0)
#else
#define ENV_BOUNCE(index) worldView.diffuseProbes[index]
#endif

#define HAZE_DIMENSIONS worldSpecializedMaterial.rect0
#define HAZE_PRECISE precise
#define HAZE_BRANCH [[dont_flatten]]
#define HAZE_COORD(uv) vec2((uv).x,1.0-(uv).y)
#define HAZE_DEPTH(p) texelFetch(lightProjectionTexture,ivec2(p),0).r
#define HAZE_FETCH(p) texelFetch(worldPlanarTexture,ivec2(p),0)
#define HAZE_UNPROJECT(p) (worldSpecializedMaterial.projectedShadowMatrix * (p))
layout(set=3,binding=0) uniform sampler2D worldContactDepth;
layout(set=5,binding=0) uniform sampler2D worldLocalReflectionTexture;
#if !defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define LOCAL_REFLECTION(index) worldView.localReflection[index]
#endif
#define LOCAL_REFLECTION_LOOP [[dont_unroll]]
#define LOCAL_REFLECTION_SAMPLE(uv) textureLod(worldLocalReflectionTexture,uv,0.0)
layout(set=4,binding=0) uniform sampler2D worldScreenReflectionTexture;
#if !defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define SCREEN_REFLECTION(index) worldView.screenReflection[index]
#endif
#define SCREEN_REFLECTION_LOOP [[dont_unroll]]
#define SCREEN_REFLECTION_DEPTH(p) texelFetch(worldContactDepth,ivec2(p),0).r
#define SCREEN_REFLECTION_SAMPLE(uv) textureLod(worldScreenReflectionTexture,uv,0.0)
#if !defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define CONTACT(index) worldView.contact[index]
#endif
#define CONTACT_LOOP [[dont_unroll]]
#define CONTACT_DEPTH(p) texelFetch(worldContactDepth,ivec2(p),0).r
layout(set=6,binding=0) uniform sampler2D worldContactBuffer;
layout(set=6,binding=1) uniform sampler2D worldContactHistory;
layout(set=6,binding=2) uniform sampler2D worldContactAux;
#define CONTACT_BUFFER_FETCH(p) texelFetch(worldContactBuffer,ivec2(p),0)
#define CONTACT_HISTORY_SAMPLE(uv) textureLod(worldContactHistory,uv,0.0)
#define SCREEN_REFLECTION_PREPARED(p) texelFetch(worldContactHistory,ivec2(p),0)
#define SCREEN_REFLECTION_CONFIDENCE(p) texelFetch(worldContactAux,ivec2(p),0).r
#define SCREEN_REFLECTION_PREPARED_ELIGIBLE (pushData.materialParams.x<1.5)
#define CONTACT_AUX_FETCH(p) texelFetch(worldContactAux,ivec2(p),0)
layout(std430,set=6,binding=10) readonly buffer WorldIndirectTransport { vec4 worldIndirectTransport[]; };
layout(std430,set=6,binding=11) readonly buffer WorldIndirectRadiance { vec4 worldIndirectRadiance[]; };
layout(set=6,binding=12) uniform sampler2D worldIndirectReceiver;
#define WORLD_GI_RECEIVER(p) texelFetch(worldIndirectReceiver,p,0)
#ifdef PHLOSION_WORLD_SURFACE_COMPOSITION
#define WORLD_GI_PREPARED texelFetch(worldIndirectReceiver,ivec2(gl_FragCoord.xy),0)
#define WORLD_GI_PREPARED_REVERSE texelFetch(worldIndirectReceiver,ivec2(gl_FragCoord.xy)+ivec2(0,int(CLUSTER_VIEW(4).y)),0)
#define WORLD_GI_PREPARED_NORMAL (STORED_NORMAL(ivec2(gl_FragCoord.xy)).xyz*2.0-1.0)
#define WORLD_GI_PREPARED_TWO_SIDED (STORED_ALBEDO(ivec2(gl_FragCoord.xy)).a>.84)
#endif
#define WORLD_GI_LOOP [[dont_unroll]]
#define WORLD_GI_FORWARD (pushData.materialParams.x>1.5)
#define WORLD_GI_DATA(i) worldIndirectTransport[i]
#define WORLD_GI_RESULT(i) worldIndirectRadiance[i]
#include "world_lighting.glsl"
#define WORLD_LAYERED_MATERIAL_INDEX worldSpecializedMaterial.layered.x
#include "world_layered_bindings.glsl"
layout(set=6,binding=23) uniform sampler2D worldSunNoise;
#define WORLD_SUN_NOISE(p) texelFetch(worldSunNoise,ivec2(p),0)
#define WORLD_SUN_PRIVATE
layout(set=6,binding=22) uniform sampler2DShadow worldSunDepth;
#define WORLD_SUN(i) worldView.sun[i]
#define WORLD_SUN_COMPARE(uv,z) textureLod(worldSunDepth,vec3(uv,z),0.0)
#if defined(PHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS)
#define WORLD_SHADOW_PARAMS vec4(0.0)
#else
#define WORLD_SHADOW_PARAMS worldView.directionalShadowParams
#endif
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

#define VOLUME_SAMPLE(uv) texture(baseColorTexture, uv).a
#include "world_volume.glsl"
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_declarations.glsl"
#endif
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
layout(depth_greater) out float gl_FragDepth;
#endif
void writeWorldColor(vec4 color) {
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
    gl_FragDepth=worldFoliageRasterDepth(gl_FragCoord.z);
#endif
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
    if (!worldSurfaceValid && motionObjectId==0u) discard;
    outColor = worldSurfaceValid?worldSurfaceOutput.albedo:vec4(0);
    outSurfaceNormal = worldSurfaceValid?worldSurfaceOutput.normalClass:
        vec4(vertexNormal*inversesqrt(max(dot(vertexNormal,vertexNormal),1e-12))*.5+.5,1.0);
    if(worldSurfaceValid)outSurfaceNormal.a=pushData.materialParams.x>.5?2.0/3.0:1.0/3.0;
    outSurfaceMaterial = worldSurfaceValid?worldSurfaceOutput.material:vec4(0);
    outSurfaceEmission = worldSurfaceValid?worldSurfaceOutput.emission:vec4(0);
    uint contactFlags=(uint(WORLD_MATERIAL_MODE+.5)==238u?1u:0u)|
        ((uint(worldSpecializedMaterial.timingFlagsAtlas.y)&4096u)!=0u?2u:0u);
    outSurfaceIdentity=uvec2(motionObjectId,(uint(worldView.pointLights[1].w)>>16u)|((motionHistoryValid>>1u)<<8u)|(contactFlags<<16u)|(uint(round(clamp(worldWetReactive,0.0,1.0)*255.0))<<24u));
    outSurfaceMotion=vec4(0);
    vec4 c=motionCurrentClip,p=motionPreviousClip;
    if((motionHistoryValid&1u)!=0u && c.w>1e-6 && p.w>1e-6 &&
       abs(c.z)<=c.w && abs(p.z)<=p.w) {
        vec3 a=c.xyz/c.w,b=p.xyz/p.w;
        vec4 velocity=vec4((b.xy-a.xy)*vec2(.5,-.5),worldFoliageResolveDepth((b.z+1)*.5),1);
        if(!any(isnan(velocity))&&!any(isinf(velocity)))outSurfaceMotion=velocity;
    }
    if(WORLD_PROBE_CAPTURE) {
        if(!worldSurfaceValid)discard;
        vec3 right=normalize(vec3(worldView.screenReflection[4].x,worldView.screenReflection[5].x,worldView.screenReflection[6].x));
        vec3 up=normalize(vec3(worldView.screenReflection[4].y,worldView.screenReflection[5].y,worldView.screenReflection[6].y));
        outSurfaceIdentity=packWorldProbeMaterial(worldSurfaceOutput,dot(worldView.screenReflection[3],vec4(worldPosition,1.0)),right,up);
        outSurfaceMotion=vec4(worldView.weatherMaterial.xy,worldSpecializedMaterial.timingFlagsAtlas.x,float(uint(worldView.pointLights[1].w)>>16u));
    }
    return;
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

vec3 encodeWorldSurfaceColor(vec3 linearColor) {
    if (WORLD_SCENE_RADIANCE) return max(linearColor, vec3(0.0));
    return pushData.shadingParams.w > 0.5
        ? linearColor
        : encodeClampedLinearColor(linearColor);
}



layout(constant_id = 0) const bool worldVolumeOnly = false;
// Effect-specific pipelines remove unrelated material work at pipeline creation.
layout(constant_id = 2) const uint worldHazeMode = 0u;

#ifdef PHLOSION_PROBE_RELIGHT
#define PROBE_DATA(p) texelFetch(clusteredDiffuse,p,0).rg
#define PROBE_DEPTH(p) texelFetch(worldContactHistory,p,0).r
#define PROBE_MATERIAL(p) texelFetch(worldContactAux,p,0)
#define PROBE_ALBEDO(p) texelFetch(clusteredDiffuse,p,0)
#define PROBE_EMISSION(p) texelFetch(clusteredSpecular,p,0)
#define PROBE_WEATHER(p) texelFetch(clusteredDepth,p,0)
#include "world_probe_relight.glsl"
#endif
void main() {
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
    gl_FragDepth=gl_FragCoord.z;
#endif
    worldWetScreen=gl_FragCoord.xy;worldWetDepth=gl_FragCoord.z;
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
    worldWetCategory=motionHistoryValid>>1u;
#endif
    localLightConsumer=pushData.materialParams.x>1.5?128u:4u;
#ifdef PHLOSION_WORLD_SURFACE_COMPOSITION
    vec3 emission,position;vec2 foliage;
    WorldSurfaceMaterial surface=loadWorldCompositionSurface(gl_FragCoord.xy,emission,position,foliage);
    updateEnvironmentDirectionalVisibility(position,surface.normal,2.0);
    vec3 color;
    [[branch]] if(foliage.x>=0.0){environmentWetMaterialApplied=1.0;color=worldLightFoliageSurface(surface.albedo,surface.normal,
        normalize(worldView.cameraPosition.xyz-position),foliage.x,surface.roughness,position,worldView.cameraPosition.xyz,foliage.y);}
    else color=lightWorldSurface(surface,emission,position,surface.normal,
        worldView.cameraPosition.xyz,worldView.cameraForward.xyz,worldView.cameraTarget.xyz,
        environmentTexture,-1.0,1.0);
    outColor=vec4(environmentHaze(color,position,worldView.cameraPosition.xyz),0.0);return;
#endif
#ifdef PHLOSION_PROBE_RELIGHT
    outColor=resolveWorldProbeSurface(gl_FragCoord.xy,worldView.cameraPosition.xyz);return;
#endif
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
    worldReflectionClip(worldPosition,WORLD_MATERIAL_MODE);
    if (pushData.materialParams.x > 1.5 ||
        (WORLD_MATERIAL_MODE > 246.5 && WORLD_MATERIAL_MODE != 254.0) ||
        (ENV_SUN.w > 0.5 && ENV_RADIANCE.w > 0.5)) discard;
#else
    if(worldScreenReflectionBuildingTiles()){writeWorldColor(worldScreenReflectionTileBounds(gl_FragCoord.xy));return;}
    if(worldLocalReflectionFiltering()){writeWorldColor(vec4(worldLocalReflectionDownsample(gl_FragCoord.xy),1.0));return;}
    if((worldContactPrepass() || worldScreenReflectionCapture() || worldLocalReflectionCapture()) && (pushData.materialParams.x>1.5 || (WORLD_MATERIAL_MODE>246.5 && WORLD_MATERIAL_MODE!=254.0)))discard;
    if (!worldVolumeOnly && (worldHazeMode != 0u || WORLD_MATERIAL_MODE == 246.0 || WORLD_MATERIAL_MODE == 247.0 || WORLD_MATERIAL_MODE == 248.0)) {
        float hazeMode = worldHazeMode != 0u ? float(worldHazeMode) : WORLD_MATERIAL_MODE;
        writeWorldColor(worldLocalHazePass(vertexUv,worldView.cameraPosition.xyz,hazeMode));return;
    }
    if(WORLD_MATERIAL_MODE==239.0){writeWorldColor(worldFogComposition(vertexUv,worldView.cameraPosition.xyz));return;}
    worldReflectionClip(worldPosition,WORLD_MATERIAL_MODE);
    if (worldVolumeOnly || WORLD_MATERIAL_MODE == 249.0) {
        writeWorldColor(worldFogTransparent(worldVolumeColor(vec3(vertexSourceUv1,vertexSourceUv2.x),vertexSourceUv2.y,
            worldSpecializedMaterial.timingFlagsAtlas.x,vertexColor,vertexUv.x),worldPosition));
        return;
    }
    updateEnvironmentForwardDirectionalVisibility(worldPosition, vertexNormal, WORLD_MATERIAL_MODE);
#endif
    if (ENV_SUN.w > 0.5 && ENV_RADIANCE.w > 0.5) {
        vec3 mapped = tonemapACESFilmic(environmentSky(worldPosition-worldView.cameraPosition.xyz,worldView.cameraPosition.xyz), ENV_AMBIENT.w);
        vec3 skyColor = pushData.shadingParams.w > 0.5 ? mapped : linearToSrgb(mapped);
        writeWorldColor(vec4(environmentDitherSky(skyColor, worldPosition-worldView.cameraPosition.xyz), 1.0));
        return;
    }
    if (WORLD_MATERIAL_MODE >= 249.5 && WORLD_MATERIAL_MODE != 254.0) {
        vec4 color = portableMaterial(WORLD_MATERIAL_MODE,
            texture(baseColorTexture, vertexUv), vertexColor,
            vec3(vertexSourceUv1, vertexSourceUv2.x));
        writeWorldColor(worldFogTransparent(color,worldPosition));
        return;
    }
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_evaluation.glsl"
#else

    float alphaMode = pushData.materialParams.x;
    float alphaCutoff = pushData.materialParams.y;
    float materialMode = WORLD_MATERIAL_MODE;

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (worldReflectionFrontFace(gl_FrontFacing)) discard;
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
