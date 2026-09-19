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

#include "world_material.glsl"
#include "world_tail_fire.glsl"

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

vec3 applyNativeGastlySmokeLighting(
    vec3 color,
    vec4 authoredResponse,
    bool useAuthoredShadowColor) {
    vec3 normal = normalize(vertexNormal);
    vec3 cameraForward = safeNormalize(
        worldView.cameraForward.xyz,
        vec3(0.0, -0.6139406, -0.7893522));
    vec3 cameraRight = safeNormalize(
        cross(cameraForward, vec3(0.0, 1.0, 0.0)),
        vec3(1.0, 0.0, 0.0));
    vec3 viewDirection = safeNormalize(
        worldView.cameraPosition.xyz - worldPosition,
        -cameraForward);
    vec3 lightPosition = worldView.cameraPosition.xyz +
        cameraRight * 0.5 - cameraForward * 0.8660254;
    vec3 lightDirection = safeNormalize(
        lightPosition - worldView.cameraTarget.xyz,
        vec3(0.45, 0.86, 0.24));
    float lightFacing = clamp(dot(normal, lightDirection), -1.0, 1.0);
    float viewFacing = clamp(dot(normal, viewDirection), -1.0, 1.0);
    // Z-A IkCharacter: HalfLambertBias=.1, ShadowStrength=.7,
    // RimLightOffset=.2, RimLightContrast=2, RimLightIntensity=.8,
    // BackRimLightIntensity=.01.
    float edge = clamp(1.0 - max(viewFacing, 0.0), 0.0, 1.0);
    float rimDomain = clamp((edge - 0.2) / 0.8, 0.0, 1.0);
    float rim;
    float backRim;
    vec3 diffuseColor;
    if (useAuthoredShadowColor) {
        float wrappedLambert = clamp(lightFacing * 0.5 + 0.5, 0.0, 1.0);
        float biasedLambert = wrappedLambert * wrappedLambert;
        const float shadowBandLow = 0.3465;
        const float shadowBandHigh = 0.3535;
        float shadowAmount = clamp(
            1.0 - (biasedLambert - shadowBandLow) /
                (shadowBandHigh - shadowBandLow),
            0.0,
            1.0);
        diffuseColor = color * mix(
            vec3(1.0),
            authoredResponse.rgb,
            shadowAmount);
        float rimSmooth = rimDomain * rimDomain * (3.0 - 2.0 * rimDomain);
        float rimShape = clamp(rimSmooth * 5.0 - 2.0, 0.0, 1.0);
        const float zaIkRimPresentationScale = 0.25;
        rim = rimShape * authoredResponse.a * zaIkRimPresentationScale;
        float rimMask = clamp(authoredResponse.a / 0.8, 0.0, 1.0);
        backRim = clamp(-viewFacing, 0.0, 1.0) * 0.01 * rimMask *
            zaIkRimPresentationScale;
    } else {
        float halfLambert = clamp(lightFacing * 0.5 + 0.6, 0.0, 1.0);
        float diffuse = mix(1.0, halfLambert, 0.7);
        diffuseColor = color * diffuse;
        rim = rimDomain * rimDomain * 0.8;
        backRim = clamp(-viewFacing, 0.0, 1.0) * 0.01;
    }
    return max(diffuseColor + color * (rim + backRim), vec3(0.0));
}

void main() {
    WorldIndirectDrawState drawState = worldIndirectDraws.states[drawStateIndex];
    sceneColorPostEnabled = drawState.shadingParams.w > 0.5;
    uint materialIndex = drawState.drawParams.x;
    float alphaMode = drawState.materialParams.x;
    float alphaCutoff = drawState.materialParams.y;
    float materialMode = drawState.materialParams.w;
    TailFireMaterialState tailFireMaterial = TailFireMaterialState(
        drawState.specializedTimingFlagsAtlas,
        drawState.specializedRect0,
        drawState.specializedRect1,
        drawState.specializedFlipbook0,
        drawState.specializedFlipbook1);

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    if (materialMode > 0.5 && materialMode < 1.5) {
        writeWorldColor(evaluateTailFire(
            baseColorTextures[nonuniformEXT(materialIndex)],
            tailFireMaterial));
        return;
    }
    if (materialMode > 26.5 && materialMode < 27.5) {
        vec4 surface = evaluateNativeLayeredUnlitDisplaced(
            baseColorTextures[nonuniformEXT(materialIndex)],
            metallicRoughnessTextures[nonuniformEXT(materialIndex)],
            tailFireMaterial);
        if (drawState.specializedTimingFlagsAtlas.y > 2.5 &&
            drawState.specializedTimingFlagsAtlas.y < 3.5) {
            bool useAuthoredShadowColor =
                drawState.specializedTimingFlagsAtlas.y > 3.3;
            vec4 authoredResponse = useAuthoredShadowColor
                ? texture(
                    emissiveTextures[nonuniformEXT(materialIndex)],
                    nativeLayeredMaterialUv(tailFireMaterial))
                : vec4(surface.rgb, 0.0);
            surface.rgb = applyNativeGastlySmokeLighting(
                surface.rgb,
                authoredResponse,
                useAuthoredShadowColor);
        }
        const float nativeToneMappingExposure = 1.15;
        vec3 nativeMapped = clamp(
            max(surface.rgb, vec3(0.0)) * nativeToneMappingExposure,
            vec3(0.0),
            vec3(1.0));
        vec3 nativeResolved = sceneColorPostEnabled
            ? nativeMapped
            : linearToSrgb(nativeMapped);
        writeWorldColor(vec4(nativeResolved, surface.a));
        return;
    }
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_indirect_evaluation.glsl"
#endif

    bool animatedEyeMaterial =
        materialMode > 28.5 && materialMode < 30.5;
    vec2 materialUv = animatedEyeMaterial
        ? vec2(
              vertexUv.x *
                      drawState.specializedLightProjectionUvRowU.x +
                  drawState.specializedLightProjectionUvRowU.z,
              vertexUv.y *
                      drawState.specializedLightProjectionUvRowU.y +
                  drawState.specializedLightProjectionUvRowU.w)
        : vertexUv;
    float textureDetailLodBias =
        ((materialMode >= 1.5 && materialMode < 2.5) ||
         (materialMode > 27.5 && materialMode < 30.5) ||
         (materialMode > 31.5 && materialMode < 35.5))
            ? drawState.specializedFlipbook1.z
            : 0.0;
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
            debugColor = (materialMode > 33.5 && materialMode < 34.5)
                ? nativeFresnelEffectBase(
                      linearColor,
                      drawState.specializedRect0,
                      drawState.specializedFlipbook1)
                : clamp(linearColor, 0.0, 1.0);
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

    // See the direct world path: the exact 384x128 probe carrier is a
    // stable, profile-private Vulkan marker when editor view-state coalescing
    // removes the redundant camera-vector length lane.
    ivec2 zaUiDiffuseProbeSize = textureSize(
        lightProjectionTextures[nonuniformEXT(materialIndex)], 0);
    bool zaSourceStageCarrier =
        zaUiDiffuseProbeSize.x == 384 && zaUiDiffuseProbeSize.y == 128;
    vec3 reviewCameraForward = zaSourceStageCarrier
        ? safeNormalize(
              worldView.cameraForward.xyz,
              vec3(0.0, -0.6139406, -0.7893522)) * 5.0
        : worldView.cameraForward.xyz;
    if ((materialMode >= 1.5 && materialMode < 2.5) ||
        (materialMode > 27.5 && materialMode < 35.5)) {
        bool nativeEyeClearCoat =
            (materialMode > 27.5 && materialMode < 28.5) ||
            (materialMode > 29.5 && materialMode < 30.5);
        bool nativePlainEye =
            nativeEyeClearCoat && drawState.specializedRect1.w < -0.5;
        bool nativeSeparateEyeCoat =
            nativeEyeClearCoat && !nativePlainEye;
        bool nativeGastlyFace =
            materialMode > 30.5 &&
            drawState.specializedTimingFlagsAtlas.y > 3.5 &&
            drawState.specializedTimingFlagsAtlas.y < 4.5;
        bool nativeIkCharacter =
            materialMode > 31.5 && materialMode < 32.5;
        bool nativeSss =
            materialMode > 32.5 && materialMode < 33.5;
        bool nativeFresnelEffect =
            materialMode > 33.5 && materialMode < 34.5;
        bool nativeIkCharacterEye =
            materialMode > 34.5 && materialMode < 35.5;
        if (nativeFresnelEffect) {
            reviewAlbedo = nativeFresnelEffectBase(
                linearColor,
                drawState.specializedRect0,
                drawState.specializedFlipbook1);
        }
        if (nativeSss) {
            linearColor = evaluateNativeSssSurface(
                linearColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                reviewCameraForward,
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.specializedTimingFlagsAtlas.y,
                drawState.pbrFactors,
                drawState.emissiveAndCamera.rgb);
        } else if (nativeIkCharacter || nativeIkCharacterEye) {
            linearColor = evaluateNativeIkCharacter(
                linearColor,
                vertexColor.rgb,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                reviewCameraForward,
                worldView.cameraTarget.xyz,
                baseColorTextures[nonuniformEXT(materialIndex)],
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                lightProjectionTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors,
                drawState.emissiveAndCamera.rgb,
                drawState.specializedRect0,
                drawState.specializedRect1,
                drawState.specializedFlipbook0,
                drawState.specializedFlipbook1,
                drawState.specializedLightProjectionUvRowV.w,
                nativeIkCharacterEye);
        } else if (nativeGastlyFace) {
            linearColor = evaluateNativeGastlyFace(
                linearColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors.x,
                drawState.pbrFactors.w,
                drawState.specializedRect0.w > 0.5);
        } else if (nativeFresnelEffect) {
            vec3 primaryColor = nativeFresnelEffectBase(
                linearColor,
                drawState.specializedRect0,
                drawState.specializedFlipbook1);
            linearColor = evaluateWorldMaterial(
                primaryColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors,
                vec3(0.0),
                -1.0,
                1.0,
                false);
            linearColor = evaluateNativeFresnelEffectLayer(
                linearColor,
                primaryColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors,
                drawState.specializedRect1,
                drawState.specializedFlipbook0,
                drawState.specializedFlipbook1);
        } else {
            linearColor = evaluateWorldMaterial(
                  linearColor,
                  materialUv,
                  worldPosition,
                  vertexNormal,
                  vertexTangent,
                  worldView.cameraPosition.xyz,
                  worldView.cameraForward.xyz,
                  worldView.cameraTarget.xyz,
                  normalTextures[nonuniformEXT(materialIndex)],
                  metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                  occlusionTextures[nonuniformEXT(materialIndex)],
                  emissiveTextures[nonuniformEXT(materialIndex)],
                  environmentTextures[nonuniformEXT(materialIndex)],
                  textureDetailLodBias,
                  nativeSeparateEyeCoat
                      ? vec4(0.0, drawState.pbrFactors.yzw)
                      : nativePlainEye
                          ? vec4(
                                drawState.pbrFactors.x * 0.8,
                                drawState.pbrFactors.yzw)
                          : drawState.pbrFactors,
                  drawState.emissiveAndCamera.rgb,
                  nativeSeparateEyeCoat
                      ? 0.0
                      : nativePlainEye
                          ? -2.0
                      : (materialMode > 1.5 && materialMode < 2.5 &&
                         drawState.specializedTimingFlagsAtlas.y > 4.5 &&
                         drawState.specializedTimingFlagsAtlas.y < 5.5)
                            ? clamp(drawState.specializedRect0.x, 0.0, 1.0)
                            : -1.0,
                  nativeSeparateEyeCoat ? 0.0 : 1.0,
                  true);
        }
        if (nativeEyeClearCoat) {
            linearColor = evaluateNativeEyeClearCoat(
                linearColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                0.0,
                tailFireMaterial.rect0,
                tailFireMaterial.rect1,
                tailFireMaterial.flipbook0.xyz);
        }
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
}
