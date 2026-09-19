#version 450
#extension GL_GOOGLE_include_directive : require

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
} worldView;
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
        // Selected Z-A IkCharacter 514/594 uses the literal bias and narrow
        // HalfLambertBias/ShadowStrength band before multiplying its authored
        // shadow color into albedo. Shadow color is not a replacement color.
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
    float alphaMode = pushData.materialParams.x;
    float alphaCutoff = pushData.materialParams.y;
    float materialMode = pushData.materialParams.w;

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    if (materialMode > 0.5 && materialMode < 1.5) {
        TailFireMaterialState tailFireMaterial = TailFireMaterialState(
            worldSpecializedMaterial.timingFlagsAtlas,
            worldSpecializedMaterial.rect0,
            worldSpecializedMaterial.rect1,
            worldSpecializedMaterial.flipbook0,
            worldSpecializedMaterial.flipbook1);
        writeWorldColor(evaluateTailFire(baseColorTexture, tailFireMaterial));
        return;
    }
    if (materialMode > 26.5 && materialMode < 27.5) {
        TailFireMaterialState nativeUnlitMaterial = TailFireMaterialState(
            worldSpecializedMaterial.timingFlagsAtlas,
            worldSpecializedMaterial.rect0,
            worldSpecializedMaterial.rect1,
            worldSpecializedMaterial.flipbook0,
            worldSpecializedMaterial.flipbook1);
        vec4 surface = evaluateNativeLayeredUnlitDisplaced(
            baseColorTexture,
            metallicRoughnessTexture,
            nativeUnlitMaterial);
        if (worldSpecializedMaterial.timingFlagsAtlas.y > 2.5 &&
            worldSpecializedMaterial.timingFlagsAtlas.y < 3.5) {
            bool useAuthoredShadowColor =
                worldSpecializedMaterial.timingFlagsAtlas.y > 3.3;
            vec4 authoredResponse = useAuthoredShadowColor
                ? texture(
                    emissiveTexture,
                    nativeLayeredMaterialUv(nativeUnlitMaterial))
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
        vec3 nativeResolved = pushData.shadingParams.w > 0.5
            ? nativeMapped
            : linearToSrgb(nativeMapped);
        writeWorldColor(vec4(nativeResolved, surface.a));
        return;
    }
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_evaluation.glsl"
#endif

    bool animatedEyeMaterial =
        materialMode > 28.5 && materialMode < 30.5;
    vec2 materialUv = animatedEyeMaterial
        ? vec2(
              vertexUv.x *
                      worldSpecializedMaterial.lightProjectionUvRowU.x +
                  worldSpecializedMaterial.lightProjectionUvRowU.z,
              vertexUv.y *
                      worldSpecializedMaterial.lightProjectionUvRowU.y +
                  worldSpecializedMaterial.lightProjectionUvRowU.w)
        : vertexUv;
    float textureDetailLodBias =
        ((materialMode >= 1.5 && materialMode < 2.5) ||
         (materialMode > 27.5 && materialMode < 30.5) ||
         (materialMode > 31.5 && materialMode < 35.5))
            ? worldSpecializedMaterial.flipbook1.z
            : 0.0;
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
            debugColor = (materialMode > 33.5 && materialMode < 34.5)
                ? nativeFresnelEffectBase(
                      linearColor,
                      worldSpecializedMaterial.rect0,
                      worldSpecializedMaterial.flipbook1)
                : clamp(linearColor, 0.0, 1.0);
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

    // The transient preview profile is normally transported in the
    // camera-forward length. Vulkan can coalesce view state across the
    // editor's scene and inspector passes, so also recognize the exact
    // recovered Z-A diffuse carrier. It is attached only by the Inspector's
    // Z-A Source Stage profile.
    ivec2 zaUiDiffuseProbeSize = textureSize(lightProjectionTexture, 0);
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
            nativeEyeClearCoat &&
            worldSpecializedMaterial.rect1.w < -0.5;
        bool nativeSeparateEyeCoat =
            nativeEyeClearCoat && !nativePlainEye;
        bool nativeGastlyFace =
            materialMode > 30.5 &&
            worldSpecializedMaterial.timingFlagsAtlas.y > 3.5 &&
            worldSpecializedMaterial.timingFlagsAtlas.y < 4.5;
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
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.flipbook1);
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
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                textureDetailLodBias,
                worldSpecializedMaterial.timingFlagsAtlas.y,
                pushData.pbrFactors,
                pushData.emissiveAndCamera.rgb);
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
                baseColorTexture,
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                lightProjectionTexture,
                textureDetailLodBias,
                pushData.pbrFactors,
                pushData.emissiveAndCamera.rgb,
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.rect1,
                worldSpecializedMaterial.flipbook0,
                worldSpecializedMaterial.flipbook1,
                worldSpecializedMaterial.lightProjectionUvRowV.w,
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
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                textureDetailLodBias,
                pushData.pbrFactors.x,
                pushData.pbrFactors.w,
                worldSpecializedMaterial.rect0.w > 0.5);
        } else if (nativeFresnelEffect) {
            vec3 primaryColor = nativeFresnelEffectBase(
                linearColor,
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.flipbook1);
            linearColor = evaluateWorldMaterial(
                primaryColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                textureDetailLodBias,
                pushData.pbrFactors,
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
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                textureDetailLodBias,
                pushData.pbrFactors,
                worldSpecializedMaterial.rect1,
                worldSpecializedMaterial.flipbook0,
                worldSpecializedMaterial.flipbook1);
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
                  normalTexture,
                  metallicRoughnessTexture,
                  occlusionTexture,
                  emissiveTexture,
                  environmentTexture,
                  textureDetailLodBias,
                  nativeSeparateEyeCoat
                      ? vec4(0.0, pushData.pbrFactors.yzw)
                      : nativePlainEye
                          ? vec4(
                                pushData.pbrFactors.x * 0.8,
                                pushData.pbrFactors.yzw)
                          : pushData.pbrFactors,
                  pushData.emissiveAndCamera.rgb,
                  nativeSeparateEyeCoat
                      ? 0.0
                      : nativePlainEye
                          ? -2.0
                      : (materialMode > 1.5 && materialMode < 2.5 &&
                         worldSpecializedMaterial.timingFlagsAtlas.y > 4.5 &&
                         worldSpecializedMaterial.timingFlagsAtlas.y < 5.5)
                            ? clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0)
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
                normalTexture,
                environmentTexture,
                0.0,
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.rect1,
                worldSpecializedMaterial.flipbook0.xyz);
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
    vec3 resolvedColor = pushData.shadingParams.w > 0.5
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));
}
