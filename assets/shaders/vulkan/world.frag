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
#if defined(PAC_VULKAN_DUAL_SOURCE_BLEND)
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outBlendAlpha;
#else
layout(location = 0) out vec4 outColor;
#endif

#include "world_material.glsl"
#include "world_tail_fire.glsl"

vec4 sampleLgpeGroundTexture(sampler2D textureSampler, vec2 uv) {
    return texture(textureSampler, uv, -2.0);
}

vec2 lgpeRoute1CloudTextureUv(vec3 position);
float evaluateLgpeRoute1ProjectedCloud();
float evaluateLgpeRoute1ProjectedShadow();
float evaluateLgpeRoute1ProjectedLighting(float toon);

vec3 applyLgpeGroundCliffSharedLighting(vec3 surface) {
    const vec3 shadowColor = vec3(0.235, 0.361, 0.391);
    float light = evaluateLgpeRoute1ProjectedLighting(1.0);
    return mix(shadowColor, vec3(1.0), light) * surface;
}

vec3 evaluateLgpeFieldGroundSurface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 blendUv = vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec2 uv2 = vec2(vertexSourceUv2.x, 1.0 - vertexSourceUv2.y);
    vec4 ground01 = sampleLgpeGroundTexture(baseColorTexture, uv0);
    vec4 ground02 = sampleLgpeGroundTexture(normalTexture, uv0);
    vec4 grass02 =
        sampleLgpeGroundTexture(metallicRoughnessTexture, uv0);
    vec4 grass01 = sampleLgpeGroundTexture(occlusionTexture, uv0);
    float blend = clamp(
        sampleLgpeGroundTexture(environmentTexture, blendUv).r,
        0.0,
        1.0);
    vec4 grassMask =
        sampleLgpeGroundTexture(emissiveTexture, uv2);
    vec3 ground = mix(ground01.rgb, ground02.rgb, blend);
    vec3 grass = mix(grass02.rgb, grass01.rgb, blend);
    vec3 surface = mix(ground, grass, clamp(grassMask.a, 0.0, 1.0));
    vec3 sourceSurface =
        grassMask.rgb * vertexColor.rgb * surface +
        max(pushData.emissiveAndCamera.rgb, vec3(0.0)) *
            (1.0 - clamp(vertexColor.a, 0.0, 1.0));
    return applyLgpeGroundCliffSharedLighting(sourceSurface);
}

vec3 evaluateLgpeFieldCliffSurface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 uv2 = vec2(vertexSourceUv2.x, 1.0 - vertexSourceUv2.y);
    vec4 cliffTex = sampleLgpeGroundTexture(baseColorTexture, uv1);
    vec4 ground02 = sampleLgpeGroundTexture(normalTexture, uv0);
    vec4 ground01 =
        sampleLgpeGroundTexture(metallicRoughnessTexture, uv0);
    float blend = clamp(
        sampleLgpeGroundTexture(occlusionTexture, blendUv).r,
        0.0,
        1.0);
    vec4 borderTex = sampleLgpeGroundTexture(emissiveTexture, uv2);
    vec3 normal = normalize(vertexNormal);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = pushData.pbrFactors.x;
    float rimMax = pushData.pbrFactors.y;
    float rimStrength = pushData.pbrFactors.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) / rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 cliff =
        cliffTex.rgb +
        max(pushData.emissiveAndCamera.rgb, vec3(0.0)) *
            rim * cliffTex.a;
    vec3 grass = mix(ground02.rgb, ground01.rgb, blend);
    vec3 surface =
        mix(cliff, grass, clamp(borderTex.a, 0.0, 1.0));
    vec3 sourceSurface = borderTex.rgb * vertexColor.rgb * surface;
    return applyLgpeGroundCliffSharedLighting(sourceSurface);
}

vec3 lgpeFoliageRgbToHsv(vec3 color) {
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

vec3 lgpeFoliageHsvToRgb(vec3 hsv) {
    vec3 p = abs(
        fract(hsv.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) *
            6.0 -
        3.0);
    return hsv.z *
        mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), hsv.y);
}

vec3 lgpeFoliageColorBalance(
    vec3 color,
    float hueShift,
    float saturation,
    float value) {
    vec3 hsv = lgpeFoliageRgbToHsv(max(color, vec3(0.0)));
    hsv.x = fract(hsv.x + hueShift);
    hsv.y = clamp(hsv.y * saturation, 0.0, 1.0);
    hsv.z *= value;
    return lgpeFoliageHsvToRgb(hsv);
}

float lgpeFoliageAcceptedLightCoordinate(vec3 normal) {
    const vec3 acceptedLightDirection =
        vec3(0.32, -0.42, 0.85);
    float sourceDot =
        dot(normalize(normal), acceptedLightDirection);
    return mix(
        0.12,
        0.96,
        clamp((sourceDot + 0.15) / 1.0, 0.0, 1.0));
}

vec3 lgpeFoliageProjectionCompensation(vec3 shadowColor) {
    float cloud = evaluateLgpeRoute1ProjectedLighting(1.0);
    vec3 projectedLighting =
        mix(max(shadowColor, vec3(0.0)), vec3(1.0), cloud);
    return mix(vec3(1.0), projectedLighting, 0.25);
}

vec3 lgpeFoliageColorize(
    vec3 baseColor,
    vec3 paletteColor,
    float factor) {
    const vec3 luminanceWeights =
        vec3(0.2126, 0.7152, 0.0722);
    float baseLuminance =
        dot(max(baseColor, vec3(0.0)), luminanceWeights);
    float paletteLuminance = max(
        dot(max(paletteColor, vec3(0.0)), luminanceWeights),
        0.0001);
    vec3 paletteAtBaseLuminance =
        paletteColor * (baseLuminance / paletteLuminance);
    return mix(
        baseColor,
        paletteAtBaseLuminance,
        clamp(factor, 0.0, 1.0));
}

vec3 lgpeFoliageAcceptedDisplayTransform(vec3 color) {
    vec3 exposed = max(color, vec3(0.0)) * 0.72;
    vec3 mapped = clamp(
        (exposed * (2.51 * exposed + 0.03)) /
            (exposed * (2.43 * exposed + 0.59) + 0.14),
        0.0,
        1.0);
    float luminance =
        dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    float highlight = smoothstep(0.35, 0.85, luminance);
    float saturationRetention = mix(0.9, 0.55, highlight);
    return mix(vec3(luminance), mapped, saturationRetention);
}

vec4 evaluateLgpeReviewedFieldTree05Surface(
    float hueShift,
    float saturation,
    float value) {
    vec2 uv0 = vertexUv;
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    float lightCoordinate =
        lgpeFoliageAcceptedLightCoordinate(vertexNormal);
    vec3 texture02 = texture(
        normalTexture,
        vec2(0.5, 1.0 - lightCoordinate),
        0.0).rgb;
    float texture03 = texture(
        metallicRoughnessTexture,
        uv0,
        0.0).r;
    vec3 acceptedLocalSurface = clamp(
        texture01.rgb + texture02 * texture03,
        0.0,
        1.0);
    vec3 acceptedColor = lgpeFoliageColorBalance(
        acceptedLocalSurface,
        hueShift,
        saturation,
        value);
    vec3 finalColor =
        acceptedColor *
        lgpeFoliageProjectionCompensation(pushData.pbrFactors.xyz);
    return vec4(
        lgpeFoliageAcceptedDisplayTransform(finalColor),
        texture01.a);
}

vec4 evaluateLgpeReviewedFieldTree02Surface(
    float hueShift,
    float saturation,
    float value,
    bool darkConifer) {
    vec2 uv0 = vertexUv;
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec3 texture02 = texture(normalTexture, uv0, 0.0).rgb;
    float lightCoordinate =
        lgpeFoliageAcceptedLightCoordinate(vertexNormal);
    float lightToon = texture(
        emissiveTexture,
        vec2(lightCoordinate, 0.5),
        0.0).r;
    vec3 directionalLightColor =
        vec3(
            worldSpecializedMaterial.rect0.z,
            worldSpecializedMaterial.rect0.w,
            worldSpecializedMaterial.rect1.x);
    vec3 acceptedLocalSurface = clamp(
        texture01.rgb +
            texture02 * directionalLightColor *
                clamp(lightToon, 0.0, 1.0),
        0.0,
        1.0);
    if (darkConifer) {
        vec3 greenColor = pushData.pbrFactors.xyz;
        float inverseShade =
            1.0 -
            dot(
                clamp(texture02, 0.0, 1.0),
                vec3(0.2126, 0.7152, 0.0722));
        acceptedLocalSurface = lgpeFoliageColorize(
            acceptedLocalSurface,
            greenColor,
            inverseShade);
    }
    vec3 acceptedColor = lgpeFoliageColorBalance(
        acceptedLocalSurface,
        hueShift,
        saturation,
        value);
    vec3 finalColor =
        acceptedColor *
        lgpeFoliageProjectionCompensation(
            pushData.emissiveAndCamera.rgb);
    vec3 displayedColor =
        lgpeFoliageAcceptedDisplayTransform(finalColor);
    return vec4(
        displayedColor,
        texture01.a);
}

vec4 evaluateLgpeFieldTree05Surface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec3 texture02 = texture(normalTexture, uv1, 0.0).rgb;
    float texture03 = texture(metallicRoughnessTexture, uv0, 0.0).r;
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            0.0).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimMax = worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimStrength = worldSpecializedMaterial.timingFlagsAtlas.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) / rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    float lightGate =
        1.0 -
        clamp(
            (1.0 - normalDotLight) * 12.7408008575,
            0.0,
            1.0);
    vec3 secondaryDirection = mix(
        viewDirection,
        -sourceSunRay,
        worldSpecializedMaterial.flipbook0.w);
    float secondaryMin = worldSpecializedMaterial.flipbook1.z;
    float secondaryMax = worldSpecializedMaterial.flipbook1.w;
    float secondarySpan =
        max(secondaryMax, secondaryMin) - secondaryMin;
    float secondaryCoordinate =
        clamp(1.0 - dot(normal, secondaryDirection), 0.0, 1.0);
    float secondary = secondarySpan > 0.0
        ? clamp(
              (secondaryCoordinate - secondaryMin) / secondarySpan,
              0.0,
              1.0)
        : 0.0;
    vec3 shadowColor = max(pushData.pbrFactors.xyz, vec3(0.0));
    vec3 rimColor = max(
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y),
        vec3(0.0));
    vec3 rimColor02 = max(
        vec3(
            worldSpecializedMaterial.rect0.z,
            worldSpecializedMaterial.rect0.w,
            worldSpecializedMaterial.rect1.x),
        vec3(0.0));
    vec3 surface =
        texture01.rgb +
        texture02 * rim * rimColor +
        max(pushData.emissiveAndCamera.rgb, vec3(0.0)) *
            (1.0 - secondary) +
        texture03 * lightGate * rimColor02;
    return vec4(mix(shadowColor, vec3(1.0), toon) * surface,
                texture01.a);
}

vec4 evaluateLgpeFieldTree02Surface(
    bool useProjectedCloud,
    bool useCanonicalTextureUv) {
    // Exact modes retain the decoded 1-V operation. Reviewed BuildModel
    // grass02 uses canonical presentation UVs, matching its exact Blender
    // checkpoint's direct model-texcoord links.
    vec2 uv0 = useCanonicalTextureUv
        ? vertexUv
        : vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec3 texture02 = texture(normalTexture, uv0, 0.0).rgb;
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    vec2 toonUv = vec2(toonCoordinate, 1.0 - toonCoordinate);
    float toon = clamp(
        texture(occlusionTexture, toonUv, 0.0).r,
        0.0,
        1.0);
    float lightToon = clamp(
        texture(emissiveTexture, toonUv, 0.0).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float edge = clamp(1.0 - dot(normal, viewDirection), 0.0, 1.0);
    float rimMin = worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimMax = worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimStrength = worldSpecializedMaterial.timingFlagsAtlas.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) * rimStrength
        : 0.0;
    float directional = clamp(edge * (5.0 / 3.0), 0.0, 1.0);
    vec3 greenColor = pushData.pbrFactors.xyz;
    vec3 shadowColor = pushData.emissiveAndCamera.rgb;
    vec3 rimColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y);
    vec3 directionalLightColor =
        vec3(
            worldSpecializedMaterial.rect0.z,
            worldSpecializedMaterial.rect0.w,
            worldSpecializedMaterial.rect1.x);
    vec3 rimColor02 = worldSpecializedMaterial.rect1.yzw;
    vec3 secondary =
        rim * rimColor +
        (1.0 - directional) * rimColor02 +
        lightToon * directionalLightColor;
    vec3 surface = texture01.rgb + texture02 * secondary;
    vec3 authored = surface * vertexColor.rgb;
    vec3 tinted =
        mix(greenColor, authored, clamp(vertexColor.a, 0.0, 1.0));
    // Source shared ten-tap projected-depth PCF remains held at one. Only
    // pasted__pasted__tree15 samples the recovered stationary LightProjMap.
    float light = useProjectedCloud
        ? evaluateLgpeRoute1ProjectedLighting(toon)
        : toon * evaluateLgpeRoute1ProjectedShadow();
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        light);
    return vec4(lighting * tinted, texture01.a);
}

vec4 evaluateLgpeFieldGrassSurface(bool withRim) {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    bool floorFoliageCard =
        !withRim && vertexSourceUv2.x < -2048.0;
    vec3 textureMap01 =
        texture(
            baseColorTexture,
            uv0,
            sourceMipBias).rgb;
    vec3 textureMap02 =
        texture(normalTexture, uv0, sourceMipBias).rgb;
    vec4 greenHikari =
        texture(
            metallicRoughnessTexture,
            floorFoliageCard ? vertexSourceUv1 : uv1,
            sourceMipBias);
    if (floorFoliageCard) {
        if (greenHikari.r >=
            clamp(pushData.materialParams.y, 0.0, 1.0)) {
            discard;
        }
    } else if (greenHikari.a <=
               clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    float greenBlend = clamp(
        texture(
            occlusionTexture,
            blendUv,
            sourceMipBias).r,
        0.0,
        1.0);
    float highlight = clamp(
        texture(
            emissiveTexture,
            uv1,
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            environmentTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 sourceColor = pushData.pbrFactors.xyz;
    vec3 decoration =
        mix(textureMap02, textureMap01, greenBlend) +
        sourceColor * (1.0 - highlight);
    vec3 authoredColor = vertexColor.rgb;
    if (!withRim && sourceMipBias > -1.0 &&
        normalize(vertexNormal).y > 0.9) {
        authoredColor =
            vec3(0.180392161, 0.482352942, 0.431372553);
    }
    vec3 surface =
        decoration * greenHikari.rgb * authoredColor;
    if (withRim) {
        vec3 viewDirection =
            normalize(worldView.cameraPosition.xyz - worldPosition);
        float edge =
            clamp(1.0 - dot(normal, viewDirection), 0.0, 1.0);
        float rimMin =
            worldSpecializedMaterial.timingFlagsAtlas.x;
        float rimMax =
            worldSpecializedMaterial.timingFlagsAtlas.y;
        float rimStrength =
            worldSpecializedMaterial.timingFlagsAtlas.z;
        float rimSpan = max(rimMax, rimMin) - rimMin;
        float rim = rimSpan > 0.0
            ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) *
                  rimStrength
            : 0.0;
        vec3 rimColor =
            vec3(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                worldSpecializedMaterial.rect0.x,
                worldSpecializedMaterial.rect0.y);
        surface += rimColor * rim;
    }
    vec3 shadowColor = pushData.emissiveAndCamera.rgb;
    vec3 result =
        mix(
            shadowColor,
            vec3(1.0),
            evaluateLgpeRoute1ProjectedLighting(toon)) *
        surface;
    float alpha = greenHikari.a;
    if (!withRim) {
        vec3 onGameColor =
            worldSpecializedMaterial.timingFlagsAtlas.xyz;
        float onGameValue = clamp(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            0.0,
            1.0);
        result *= mix(vec3(1.0), onGameColor, onGameValue);
        alpha *= clamp(
            worldSpecializedMaterial.rect0.x,
            0.0,
            1.0);
    }
    return vec4(result, alpha);
}

vec2 lgpeRoute1CloudTextureUv(vec3 position) {
    vec4 world = vec4(position, 1.0);
    float sourceU =
        dot(world, worldSpecializedMaterial.lightProjectionUvRowU);
    float sourceV =
        dot(world, worldSpecializedMaterial.lightProjectionUvRowV);
    return vec2(sourceU, 1.0 - sourceV);
}

float evaluateLgpeRoute1ProjectedCloud() {
    return clamp(
        texture(
            lightProjectionTexture,
            lgpeRoute1CloudTextureUv(worldPosition),
            0.0).r,
        0.0,
        1.0);
}

float evaluateLgpeRoute1ProjectedShadow() {
    vec4 shadowParams =
        worldSpecializedMaterial.projectedShadowParams;
    if (shadowParams.x < 0.5) return 1.0;
    vec4 shadowClip =
        worldSpecializedMaterial.projectedShadowMatrix *
        vec4(worldPosition, 1.0);
    if (abs(shadowClip.w) <= 1e-8) return 1.0;
    vec3 shadowNdc = shadowClip.xyz / shadowClip.w;
    vec2 shadowUv = shadowNdc.xy * 0.5 + 0.5;
    if (any(lessThan(shadowUv, vec2(0.0))) ||
        any(greaterThan(shadowUv, vec2(1.0)))) {
        return 1.0;
    }
    float reference =
        shadowNdc.z * 0.5 + 0.5 - shadowParams.z / shadowClip.w;
    const vec2 poisson[10] = vec2[10](
        vec2(-0.8405, -0.0740), vec2(-0.3262, -0.4058),
        vec2(-0.2034,  0.4573), vec2(-0.6985,  0.6206),
        vec2( 0.9635, -0.1944), vec2( 0.4734, -0.4800),
        vec2( 0.5195,  0.7670), vec2( 0.1855, -0.8945),
        vec2( 0.5074,  0.0650), vec2(-0.3219,  0.5954));
    ivec2 extent = max(textureSize(projectedShadowTexture, 0), ivec2(1));
    float projectedShadow = 0.0;
    for (int tap = 0; tap < 10; ++tap) {
        vec2 tapUv = shadowUv + poisson[tap] * (0.0004 * shadowParams.y);
        vec2 texelPosition = tapUv * vec2(extent) - vec2(0.5);
        ivec2 baseTexel = ivec2(floor(texelPosition));
        vec2 blend = fract(texelPosition);
        float comparisons[4];
        for (int corner = 0; corner < 4; ++corner) {
            ivec2 texel =
                baseTexel + ivec2(corner & 1, (corner >> 1) & 1);
            float storedDepth = 1.0;
            if (all(greaterThanEqual(texel, ivec2(0))) &&
                all(lessThan(texel, extent))) {
                vec3 packedDepth =
                    texelFetch(projectedShadowTexture, texel, 0).rgb * 255.0;
                storedDepth =
                    dot(packedDepth, vec3(65536.0, 256.0, 1.0)) /
                    16777215.0;
            }
            comparisons[corner] =
                reference <= storedDepth ? 1.0 : 0.0;
        }
        float row0 = mix(comparisons[0], comparisons[1], blend.x);
        float row1 = mix(comparisons[2], comparisons[3], blend.x);
        projectedShadow += mix(row0, row1, blend.y) * 0.1;
    }
    return projectedShadow;
}

float evaluateLgpeRoute1ProjectedLighting(float toon) {
    return min(
        clamp(toon, 0.0, 1.0) *
            evaluateLgpeRoute1ProjectedShadow(),
        evaluateLgpeRoute1ProjectedCloud());
}

vec4 evaluateLgpeFieldGrassShader04Surface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    vec4 texture02 = texture(normalTexture, uv1, 0.0);
    float texture03 = clamp(
        texture(metallicRoughnessTexture, uv1, 0.0).r,
        0.0,
        1.0);
    vec4 base = mix(texture01, texture02, texture03);
    float alpha =
        base.a * vertexColor.a *
        clamp(worldSpecializedMaterial.timingFlagsAtlas.x, 0.0, 1.0) *
        clamp(worldSpecializedMaterial.timingFlagsAtlas.z, 0.0, 1.0);
    if (alpha <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            0.0).r,
        0.0,
        1.0);
    float projectedLight =
        evaluateLgpeRoute1ProjectedLighting(toon);
    vec3 onGameColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y);
    vec3 surface =
        base.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.y,
                0.0,
                1.0));
    vec3 lighting =
        mix(
            pushData.pbrFactors.xyz,
            vec3(1.0),
        projectedLight);
    return vec4(lighting * surface, alpha);
}

vec4 evaluateLgpeFieldGrassShader05Surface() {
    float scrollU = worldSpecializedMaterial.rect0.y;
    float scrollV = worldSpecializedMaterial.rect0.z;
    vec2 maskUv =
        vec2(vertexUv.x + scrollU, 1.0 - (vertexUv.y + scrollV));
    vec2 uv1Primary =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 uv1Secondary =
        vec2(vertexSourceUv1.x + 1.0, 0.5 - vertexSourceUv1.y);
    float lightLine =
        clamp(texture(normalTexture, maskUv, 0.0).r, 0.0, 1.0);
    vec4 alpha01Primary =
        texture(baseColorTexture, uv1Primary, 0.0);
    vec4 alpha01Secondary =
        texture(baseColorTexture, uv1Secondary, 0.0);
    vec4 base =
        mix(alpha01Primary, alpha01Secondary, lightLine);
    float alpha =
        base.a * vertexColor.a *
        clamp(worldSpecializedMaterial.timingFlagsAtlas.y, 0.0, 1.0);
    if (alpha <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    float greenBlend =
        clamp(texture(emissiveTexture, blendUv, 0.0).r, 0.0, 1.0);
    vec3 textureMap01 =
        texture(metallicRoughnessTexture, uv0, 0.0).rgb;
    vec3 textureMap02 = texture(occlusionTexture, uv0, 0.0).rgb;
    vec3 decoration = mix(textureMap02, textureMap01, greenBlend);
    float projectedLight =
        evaluateLgpeRoute1ProjectedLighting(1.0);
    vec3 onGameColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.z,
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x);
    vec3 surface =
        (base.rgb + decoration) * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.x,
                0.0,
                1.0));
    vec3 lighting =
        mix(pushData.pbrFactors.xyz, vec3(1.0), projectedLight);
    return vec4(lighting * surface, alpha);
}

vec4 evaluateLgpeRoadstoneOverlaySurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 onGameColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    float alpha =
        texture01.a * vertexColor.a *
        clamp(worldSpecializedMaterial.rect0.y, 0.0, 1.0) *
        clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0);
    vec3 surface =
        texture01.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                0.0,
                1.0));
    vec3 lighting = mix(
        pushData.emissiveAndCamera.rgb,
        vec3(1.0),
        evaluateLgpeRoute1ProjectedLighting(toon));
    return vec4(lighting * surface * alpha, alpha);
}

vec4 evaluateLgpeRockMaskOverlaySurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec3 textureMap01 =
        texture(baseColorTexture, uv0, sourceMipBias).rgb;
    vec3 textureMap02 =
        texture(normalTexture, uv0, sourceMipBias).rgb;
    vec4 greenHikari =
        texture(
            metallicRoughnessTexture,
            uv1,
            sourceMipBias);
    float greenBlend = clamp(
        texture(
            occlusionTexture,
            blendUv,
            sourceMipBias).r,
        0.0,
        1.0);
    float highlight = clamp(
        texture(
            emissiveTexture,
            uv1,
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            environmentTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 sourceColor = pushData.pbrFactors.xyz;
    vec3 decoration =
        mix(textureMap02, textureMap01, greenBlend) +
        sourceColor * (1.0 - highlight);
    vec3 onGameColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    float alpha =
        greenHikari.a *
        clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0);
    vec3 surface =
        decoration * greenHikari.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                0.0,
                1.0));
    vec3 lighting = mix(
        pushData.emissiveAndCamera.rgb,
        vec3(1.0),
        evaluateLgpeRoute1ProjectedLighting(toon));
    return vec4(lighting * surface * alpha, alpha);
}

float evaluateLgpeFieldRockLightToon(float toonCoordinate) {
    const float sourceValues[54] = float[54](
        1.0, 3.0, 5.0, 7.0, 10.0, 13.0, 16.0, 19.0,
        22.0, 26.0, 30.0, 34.0, 38.0, 43.0, 47.0, 53.0,
        58.0, 63.0, 68.0, 73.0, 80.0, 85.0, 91.0, 97.0,
        103.0, 110.0, 116.0, 122.0, 129.0, 135.0, 142.0,
        148.0, 155.0, 161.0, 168.0, 174.0, 181.0, 188.0,
        193.0, 200.0, 207.0, 211.0, 218.0, 223.0, 227.0,
        232.0, 236.0, 239.0, 243.0, 247.0, 249.0, 253.0,
        255.0, 255.0);
    float sourceTexel =
        clamp(toonCoordinate, 0.0, 1.0) * 512.0 - 0.5;
    int lower = int(floor(sourceTexel));
    int upper = lower + 1;
    float lowerValue = lower < 458
        ? 0.0
        : (lower >= 512 ? 255.0 : sourceValues[lower - 458]);
    float upperValue = upper < 458
        ? 0.0
        : (upper >= 512 ? 255.0 : sourceValues[upper - 458]);
    return mix(lowerValue, upperValue, fract(sourceTexel)) / 255.0;
}

float lgpeFlowerCoverage(float alpha) {
    float coverageT =
        clamp((alpha - 0.55) / (0.85 - 0.55), 0.0, 1.0);
    return coverageT * coverageT * (3.0 - 2.0 * coverageT);
}

float lgpeFlowerDitherThreshold(vec2 fragmentPosition) {
    ivec2 pixel = ivec2(mod(floor(fragmentPosition), 4.0));
    const float bayer[16] = float[16](
         0.5,  8.5,  2.5, 10.5,
        12.5,  4.5, 14.5,  6.5,
         3.5, 11.5,  1.5,  9.5,
        15.5,  7.5, 13.5,  5.5);
    return bayer[pixel.x + pixel.y * 4] / 16.0;
}

vec3 lgpeFlowerFieldHighlight(vec3 sourceColor) {
    float maximum = max(max(sourceColor.r, sourceColor.g), sourceColor.b);
    float minimum = min(min(sourceColor.r, sourceColor.g), sourceColor.b);
    float chroma = maximum - minimum;
    float sourceSaturation =
        maximum > 0.000001 ? chroma / maximum : 0.0;
    float saturation = clamp(sourceSaturation * 1.14, 0.0, 1.0);
    float value = maximum * 1.12;
    float adjustedMinimum = value * (1.0 - saturation);
    float adjustedChroma = value * saturation;
    vec3 hueComponent =
        chroma > 0.000001
            ? (sourceColor - vec3(minimum)) / chroma
            : vec3(0.0);
    return vec3(adjustedMinimum) + hueComponent * adjustedChroma;
}

vec4 evaluateLgpeFieldFlowerSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    bool buildmodelReview =
        pushData.materialParams.w > 19.5 &&
        pushData.materialParams.w < 20.5;
    vec2 uv0 =
        buildmodelReview
            ? vertexUv
            : vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    float sourceAlpha =
        texture01.a * vertexColor.a *
        clamp(worldSpecializedMaterial.rect0.y, 0.0, 1.0) *
        clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0);
    float alpha = sourceAlpha;
    if (buildmodelReview) {
        alpha = lgpeFlowerCoverage(sourceAlpha);
        if (alpha <= lgpeFlowerDitherThreshold(gl_FragCoord.xy)) {
            discard;
        }
    } else if (alpha <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }

    if (buildmodelReview) {
        float projectedLight =
            evaluateLgpeRoute1ProjectedLighting(1.0);
        vec3 normal = normalize(vertexNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                occlusionTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 projectedLighting = mix(
            pushData.emissiveAndCamera.rgb,
            vec3(1.0),
        projectedLight);
        vec3 projectionCompensation = mix(
            vec3(1.0),
            projectedLighting,
            0.25);
        vec3 accepted = lgpeFlowerFieldHighlight(texture01.rgb);
        vec3 exact =
            texture01.rgb * vertexColor.rgb *
            projectionCompensation;
        vec3 restrained = mix(accepted, exact, 0.35);
        vec3 fieldLighting = mix(
            pushData.emissiveAndCamera.rgb,
            vec3(1.0),
            evaluateLgpeRoute1ProjectedLighting(toon));
        return vec4(
            restrained * (fieldLighting + vec3(0.12)),
            alpha);
    }

    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 onGameColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    vec3 surface =
        texture01.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                0.0,
                1.0));
    vec3 lighting = mix(
        pushData.emissiveAndCamera.rgb,
        vec3(1.0),
        evaluateLgpeRoute1ProjectedLighting(toon));
    return vec4(lighting * surface, alpha);
}

vec4 evaluateLgpeFieldRockSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 uv2 =
        vec2(vertexSourceUv2.x, 1.0 - vertexSourceUv2.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec4 rockTexture =
        texture(baseColorTexture, uv1, sourceMipBias);
    vec3 groundTexture02 =
        texture(normalTexture, uv0, sourceMipBias).rgb;
    vec3 groundTexture01 =
        texture(
            metallicRoughnessTexture,
            uv0,
            sourceMipBias).rgb;
    float blend = clamp(
        texture(
            occlusionTexture,
            blendUv,
            sourceMipBias).r,
        0.0,
        1.0);
    vec4 borderTexture =
        texture(emissiveTexture, uv2, sourceMipBias);

    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float shadowToon = clamp(
        texture(
            environmentTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    float lightToon =
        evaluateLgpeFieldRockLightToon(toonCoordinate);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = pushData.pbrFactors.w;
    float rimMax =
        worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimStrength =
        worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) /
                  rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 rock =
        rockTexture.rgb +
        pushData.emissiveAndCamera.rgb * lightToon +
        pushData.pbrFactors.xyz * rim * rockTexture.a;
    vec3 ground =
        mix(groundTexture02, groundTexture01, blend);
    vec3 surface =
        mix(rock, ground, clamp(borderTexture.a, 0.0, 1.0));
    vec3 shadowColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.z,
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x);
    vec3 onGameColor =
        worldSpecializedMaterial.rect0.yzw;
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        evaluateLgpeRoute1ProjectedLighting(shadowToon));
    return vec4(
        lighting * borderTexture.rgb * vertexColor.rgb * surface *
            mix(
                vec3(1.0),
                onGameColor,
                clamp(worldSpecializedMaterial.rect1.x, 0.0, 1.0)),
        clamp(worldSpecializedMaterial.rect1.y, 0.0, 1.0));
}

vec4 evaluateLgpeFieldSignSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    // Canonical BNTX RGBA rows are top-down. The source program's 1-V
    // convention is therefore already represented by the decode.
    vec2 uv0 = vertexUv;
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    vec2 toonUv =
        vec2(toonCoordinate, 1.0 - toonCoordinate);
    float shadowToon = clamp(
        texture(
            occlusionTexture,
            toonUv,
            sourceMipBias).r,
        0.0,
        1.0);
    float lightToon =
        evaluateLgpeFieldRockLightToon(toonCoordinate);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin =
        worldSpecializedMaterial.timingFlagsAtlas.w;
    float rimMax = worldSpecializedMaterial.rect0.x;
    float rimStrength = worldSpecializedMaterial.rect0.y;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) /
                  rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 rimColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    // Source ShadowColor and OnGameColor are exactly white, so the recovered
    // AutoShadow and OnGame mixes are neutral for this Route 1 sign material.
    vec3 surface =
        texture01.rgb +
        pushData.emissiveAndCamera.rgb * lightToon +
        rimColor * rim;
    vec3 shadowColor = pushData.pbrFactors.xyz;
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        evaluateLgpeRoute1ProjectedLighting(shadowToon));
    return vec4(
        lighting * surface * vertexColor.rgb,
        texture01.a * vertexColor.a);
}

vec4 evaluateLgpeFieldEncounterGrassSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vertexUv;
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    float rimMask = clamp(
        texture(normalTexture, uv0, sourceMipBias).r,
        0.0,
        1.0);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float toonCoordinate =
        dot(normal, -sourceSunRay) * 0.5 + 0.5;
    float shadowToon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin =
        worldSpecializedMaterial.timingFlagsAtlas.w;
    float rimMax = worldSpecializedMaterial.rect0.x;
    float rimStrength = worldSpecializedMaterial.rect0.y;
    float rim = smoothstep(
                    rimMin,
                    max(rimMax, rimMin + 1.0e-5),
                    clamp(
                        1.0 - abs(dot(normal, viewDirection)),
                        0.0,
                        1.0)) *
                rimStrength * rimMask;
    vec3 rimColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    vec3 shadowColor = pushData.pbrFactors.xyz;
    vec3 base = texture01.rgb * vertexColor.rgb;
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        evaluateLgpeRoute1ProjectedLighting(shadowToon));
    return vec4(
        (base + rimColor * rim) * lighting,
        texture01.a * vertexColor.a);
}

vec4 evaluateLgpeFieldObjectTreeMikiSurface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    float highlightAlpha = texture(normalTexture, uv1, 0.0).a;
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            0.0).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimMax = worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimStrength = worldSpecializedMaterial.timingFlagsAtlas.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              (clamp(
                   1.0 - dot(normal, viewDirection),
                   0.0,
                   1.0) -
               rimMin) /
                  rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 shadowColor = max(pushData.pbrFactors.xyz, vec3(0.0));
    vec3 rimColor = max(
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y),
        vec3(0.0));
    vec3 lighting = mix(shadowColor, vec3(1.0), toon);
    vec3 surface = texture01.rgb + rimColor * rim * highlightAlpha;
    return vec4(
        lighting * surface * vertexColor.rgb,
        texture01.a * vertexColor.a);
}

void writeWorldColor(vec4 color) {
#if defined(PAC_VULKAN_DUAL_SOURCE_BLEND)
    float blendAlpha = clamp(color.a, 0.0, 1.0);
    float quantizedAlpha = floor(blendAlpha * 63.0 + 0.5) / 63.0;
    outColor = vec4(color.rgb, quantizedAlpha);
    outBlendAlpha = vec4(0.0, 0.0, 0.0, blendAlpha);
#else
    outColor = color;
#endif
}

vec3 encodeLgpeFinalColor(vec3 linearColor) {
    return pushData.shadingParams.w > 0.5
        ? linearColor
        : encodeLgpeFinalColorNative(linearColor);
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
    if (materialMode > 3.5 && materialMode < 4.5) {
        vec3 groundLinear = evaluateLgpeFieldGroundSurface();
        writeWorldColor(vec4(encodeLgpeFinalColor(groundLinear), 1.0));
        return;
    }
    if (materialMode > 4.5 && materialMode < 5.5) {
        vec3 cliffLinear = evaluateLgpeFieldCliffSurface();
        writeWorldColor(vec4(encodeLgpeFinalColor(cliffLinear), 1.0));
        return;
    }
    if (materialMode > 5.5 && materialMode < 6.5) {
        vec4 treeSurface = evaluateLgpeFieldTree05Surface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(treeSurface.rgb), treeSurface.a));
        return;
    }
    if (materialMode > 6.5 && materialMode < 7.5) {
        vec4 trunkSurface = evaluateLgpeFieldObjectTreeMikiSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(trunkSurface.rgb), trunkSurface.a));
        return;
    }
    if (materialMode > 7.5 && materialMode < 8.5) {
        vec4 treeSurface =
            evaluateLgpeFieldTree02Surface(false, false);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(treeSurface.rgb), treeSurface.a));
        return;
    }
    if (materialMode > 8.5 && materialMode < 9.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassSurface(false);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 9.5 && materialMode < 10.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassSurface(true);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 10.5 && materialMode < 11.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassShader04Surface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 11.5 && materialMode < 12.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassShader05Surface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 12.5 && materialMode < 13.5) {
        vec4 overlaySurface = evaluateLgpeRoadstoneOverlaySurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(overlaySurface.rgb), overlaySurface.a));
        return;
    }
    if (materialMode > 13.5 && materialMode < 14.5) {
        vec4 overlaySurface = evaluateLgpeRockMaskOverlaySurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(overlaySurface.rgb), overlaySurface.a));
        return;
    }
    if (materialMode > 14.5 && materialMode < 15.5) {
        vec4 flowerSurface = evaluateLgpeFieldFlowerSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(flowerSurface.rgb), flowerSurface.a));
        return;
    }
    if (materialMode > 15.5 && materialMode < 16.5) {
        vec4 rockSurface = evaluateLgpeFieldRockSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(rockSurface.rgb), rockSurface.a));
        return;
    }
    if (materialMode > 16.5 && materialMode < 17.5) {
        vec4 signSurface = evaluateLgpeFieldSignSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(signSurface.rgb), signSurface.a));
        return;
    }
    if (materialMode > 17.5 && materialMode < 18.5) {
        vec4 grassSurface =
            evaluateLgpeFieldEncounterGrassSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 18.5 && materialMode < 19.5) {
        vec4 shrubSurface =
            evaluateLgpeFieldTree02Surface(true, false);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(shrubSurface.rgb), shrubSurface.a));
        return;
    }
    if (materialMode > 19.5 && materialMode < 20.5) {
        vec4 flowerSurface = evaluateLgpeFieldFlowerSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(flowerSurface.rgb), flowerSurface.a));
        return;
    }
    if (materialMode > 20.5 && materialMode < 21.5) {
        vec4 foliageSurface =
            evaluateLgpeReviewedFieldTree05Surface(
                0.02072325, 1.08, 0.8807060431);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(foliageSurface.rgb), foliageSurface.a));
        return;
    }
    if (materialMode > 21.5 && materialMode < 22.5) {
        vec4 foliageSurface =
            evaluateLgpeReviewedFieldTree05Surface(
                0.00049965, 1.0371891204, 0.9015603440);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(foliageSurface.rgb), foliageSurface.a));
        return;
    }
    if (materialMode > 22.5 && materialMode < 23.5) {
        vec4 foliageSurface =
            evaluateLgpeReviewedFieldTree02Surface(
                0.02271645, 1.0476480571, 0.82, false);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(foliageSurface.rgb), foliageSurface.a));
        return;
    }
    if (materialMode > 23.5 && materialMode < 24.5) {
        vec4 foliageSurface =
            evaluateLgpeReviewedFieldTree02Surface(
                0.00425595, 1.0114461323, 1.0689001800, true);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(foliageSurface.rgb), foliageSurface.a));
        return;
    }
    if (materialMode > 24.5 && materialMode < 25.5) {
        vec4 foliageSurface =
            evaluateLgpeReviewedFieldTree05Surface(
                0.02981715, 0.9248157036, 0.9181899276);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(foliageSurface.rgb), foliageSurface.a));
        return;
    }
    if (materialMode > 25.5 && materialMode < 26.5) {
        vec4 sourceSurface =
            evaluateLgpeFieldTree02Surface(true, true);
        vec4 foliageSurface = vec4(
            lgpeFoliageAcceptedDisplayTransform(sourceSurface.rgb),
            sourceSurface.a);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(foliageSurface.rgb), foliageSurface.a));
        return;
    }

    vec4 sampled = texture(baseColorTexture, vertexUv);
    vec3 linearColor = clamp(sampled.rgb, 0.0, 1.0) * clamp(vertexColor.rgb, 0.0, 1.0);
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

    if (materialMode >= 1.5 && materialMode < 2.5) {
        linearColor = evaluateWorldMaterial(
            linearColor,
            vertexUv,
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
            pushData.pbrFactors,
            pushData.emissiveAndCamera.rgb);
    }

    const float toneMappingExposure = 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    vec3 resolvedColor = pushData.shadingParams.w > 0.5
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));
}
