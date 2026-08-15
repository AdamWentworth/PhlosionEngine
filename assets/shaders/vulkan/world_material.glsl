vec3 safeNormalize(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-8 ? value * inversesqrt(lengthSquared) : fallback;
}

int decodeReviewLightingProfile(vec3 cameraForwardPacked) {
    // The camera direction is normalized everywhere it is consumed. Model
    // previews use its redundant length as a transient cross-backend profile
    // lane: 1=source bridge, 2=studio, 3=albedo-biased, 4=grazing.
    return clamp(
        int(floor(length(cameraForwardPacked) + 0.5)) - 1,
        0,
        3);
}

vec3 applyReviewLightingProfile(vec3 composite,
                                vec3 resolvedAlbedo,
                                vec3 sourceNormal,
                                vec3 cameraForwardPacked) {
    int profile = decodeReviewLightingProfile(cameraForwardPacked);
    if (profile == 0) return max(composite, vec3(0.0));

    vec3 albedo = max(resolvedAlbedo, vec3(0.0));
    if (profile == 1) {
        // Neutral studio: retain highlights while lifting only hard shadowed
        // regions. This is the default import-review rig.
        vec3 shadowFloor = albedo * 0.50;
        return max(
            mix(composite, max(composite, shadowFloor), 0.56),
            vec3(0.0));
    }
    if (profile == 2) {
        // Albedo-biased: primarily authored color, with a restrained amount
        // of Composite response left for gloss, translucency, and emission.
        vec3 highlight = max(composite - albedo, vec3(0.0));
        return max(
            mix(composite, albedo, 0.82) + highlight * 0.16,
            vec3(0.0));
    }

    vec3 cameraForward = safeNormalize(
        cameraForwardPacked,
        vec3(0.0, -0.6139406, -0.7893522));
    vec3 cameraRight = safeNormalize(
        cross(cameraForward, vec3(0.0, 1.0, 0.0)),
        vec3(1.0, 0.0, 0.0));
    vec3 normal = safeNormalize(sourceNormal, vec3(0.0, 1.0, 0.0));
    float grazing = pow(abs(dot(normal, cameraRight)), 0.70);
    vec3 grazingSurface = albedo * (0.36 + 0.78 * grazing);
    vec3 highlight = max(composite - albedo, vec3(0.0));
    return max(
        mix(max(composite, albedo * 0.34), grazingSurface, 0.62) +
            highlight * 0.20,
        vec3(0.0));
}

vec3 rrtAndOdtFit(vec3 value) {
    vec3 a = value * (value + 0.0245786) - 0.000090537;
    vec3 b = value * (0.983729 * value + 0.4329510) + 0.238081;
    return a / b;
}

vec3 tonemapACESFilmic(vec3 color, float exposure) {
    const mat3 inputTransform = mat3(
        vec3(0.59719, 0.07600, 0.02840),
        vec3(0.35458, 0.90834, 0.13383),
        vec3(0.04823, 0.01566, 0.83777));
    const mat3 outputTransform = mat3(
        vec3( 1.60475, -0.10208, -0.00327),
        vec3(-0.53108,  1.10813, -0.07276),
        vec3(-0.07367, -0.00605,  1.07602));
    color *= exposure / 0.6;
    color = inputTransform * color;
    color = rrtAndOdtFit(color);
    return clamp(outputTransform * color, 0.0, 1.0);
}

vec3 linearToSrgb(vec3 color) {
    color = max(color, vec3(0.0));
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

vec3 encodeLgpeFinalColorNative(vec3 linearColor) {
    // The source writes linear color to UNORM before its dedicated
    // gamma_correction shader applies the standard sRGB transfer.
    return linearToSrgb(clamp(linearColor, 0.0, 1.0));
}

float distributionGGX(float normalDotHalf, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(3.14159265 * denominator * denominator, 1e-5);
}

float geometrySchlickGGX(float normalDotView, float roughness) {
    float radius = roughness + 1.0;
    float k = radius * radius * 0.125;
    return normalDotView / max(normalDotView * (1.0 - k) + k, 1e-5);
}

vec3 fresnelSchlick(float cosineTheta, vec3 reflectanceAtNormal) {
    float factor = pow(clamp(1.0 - cosineTheta, 0.0, 1.0), 5.0);
    return reflectanceAtNormal + (vec3(1.0) - reflectanceAtNormal) * factor;
}

#include "world_environment.glsl"

vec3 evaluateDirectPbr(vec3 normal,
                       vec3 view,
                       vec3 light,
                       vec3 albedo,
                       vec3 reflectanceAtNormal,
                       float roughness,
                       float metallic) {
    float normalDotLight = max(dot(normal, light), 0.0);
    float normalDotView = max(dot(normal, view), 0.0);
    if (normalDotLight <= 0.0 || normalDotView <= 0.0) return vec3(0.0);
    vec3 halfVector = safeNormalize(view + light, normal);
    float normalDotHalf = max(dot(normal, halfVector), 0.0);
    float viewDotHalf = max(dot(view, halfVector), 0.0);
    float distribution = distributionGGX(normalDotHalf, roughness);
    float geometry = geometrySchlickGGX(normalDotView, roughness) *
                     geometrySchlickGGX(normalDotLight, roughness);
    vec3 fresnel = fresnelSchlick(viewDotHalf, reflectanceAtNormal);
    vec3 specular = distribution * geometry * fresnel /
                    max(4.0 * normalDotView * normalDotLight, 1e-4);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    const float directIntensity = 0.72 * 3.14159265;
    return (diffuseWeight * albedo / 3.14159265 + specular) *
           directIntensity * normalDotLight;
}

vec4 sampleWorldMaterialTexture(sampler2D map,
                                vec2 uv,
                                float textureDetailLodBias) {
    float lodScale = exp2(textureDetailLodBias);
    return textureGrad(
        map,
        uv,
        dFdx(uv) * lodScale,
        dFdy(uv) * lodScale);
}

vec3 mappedWorldNormal(vec2 uv,
                       vec3 position,
                       vec3 sourceNormal,
                       vec4 sourceTangent,
                       sampler2D map,
                       float textureDetailLodBias,
                       float normalScale) {
    float faceDirection = gl_FrontFacing ? 1.0 : -1.0;
    vec3 normal = safeNormalize(sourceNormal, vec3(0.0, 1.0, 0.0)) * faceDirection;
    if (normalScale <= 0.0) return normal;
    vec3 texel = sampleWorldMaterialTexture(
        map, uv, textureDetailLodBias).xyz;
    vec2 mappedXY = (texel.xy * 2.0 - 1.0) * max(normalScale, 0.0) * 1.25;
    float authoredZ = texel.z * 2.0 - 1.0;
    float reconstructedZ = sqrt(max(1.0 - clamp(dot(mappedXY, mappedXY), 0.0, 1.0), 0.0));
    // Two-channel normal maps are commonly decoded into RGBA with a constant
    // blue channel of either 0 or 255; neither value is authored Z.
    // Reconstruct Z in both sentinel cases.
    float reconstructPackedZ =
        texel.z <= (1.5 / 255.0) || texel.z >= (253.5 / 255.0)
            ? 1.0
            : 0.0;
    float mappedZ = mix(authoredZ, reconstructedZ, reconstructPackedZ);
    vec3 tangentNormal = safeNormalize(vec3(mappedXY, mappedZ), vec3(0.0, 0.0, 1.0));

    vec3 tangent = sourceTangent.xyz;
    if (dot(tangent, tangent) > 1e-6 && abs(sourceTangent.w) > 0.5) {
        tangent = safeNormalize(tangent - normal * dot(normal, tangent), vec3(1.0, 0.0, 0.0));
        vec3 bitangent = safeNormalize(cross(normal, tangent), vec3(0.0, 0.0, 1.0)) *
                         (sourceTangent.w < 0.0 ? -1.0 : 1.0);
        if (!gl_FrontFacing) {
            tangent = -tangent;
            bitangent = -bitangent;
        }
        return safeNormalize(
            tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z,
            normal);
    }

    vec3 positionDx = dFdx(position);
    vec3 positionDy = dFdy(position);
    vec2 uvDx = dFdx(uv);
    vec2 uvDy = dFdy(uv);
    vec3 tangentPerp = cross(positionDy, normal);
    vec3 bitangentPerp = cross(normal, positionDx);
    vec3 tangentFromDerivatives = tangentPerp * uvDx.x + bitangentPerp * uvDy.x;
    vec3 bitangentFromDerivatives = tangentPerp * uvDx.y + bitangentPerp * uvDy.y;
    float determinant = max(
        dot(tangentFromDerivatives, tangentFromDerivatives),
        dot(bitangentFromDerivatives, bitangentFromDerivatives));
    float scale = determinant > 1e-10 ? faceDirection * inversesqrt(determinant) : 0.0;
    return safeNormalize(
        tangentFromDerivatives * (tangentNormal.x * scale) +
        bitangentFromDerivatives * (tangentNormal.y * scale) +
        normal * tangentNormal.z,
        normal);
}

vec3 evaluateWorldMaterial(vec3 albedo,
                           vec2 uv,
                           vec3 position,
                           vec3 sourceNormal,
                           vec4 sourceTangent,
                           vec3 cameraPosition,
                           vec3 cameraForwardPacked,
                           vec3 cameraTarget,
                           sampler2D normalMap,
                           sampler2D metallicRoughnessMap,
                           sampler2D occlusionMap,
                           sampler2D emissiveMap,
                           sampler2D environmentMap,
                           float textureDetailLodBias,
                           vec4 factors,
                           vec3 emissiveFactor,
                           float dielectricSpecularIntensity,
                           float specularIblScale,
                           bool useMetallicRoughnessMap) {
    vec3 normal = mappedWorldNormal(
        uv,
        position,
        sourceNormal,
        sourceTangent,
        normalMap,
        textureDetailLodBias,
        factors.x);
    vec3 cameraForward = safeNormalize(
        cameraForwardPacked,
        normalize(vec3(0.0, -0.6139406, -0.7893522)));
    vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
    if (dot(cameraRight, cameraRight) < 1e-6) {
        cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
    }
    cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
    vec3 view = safeNormalize(cameraPosition - position, -cameraForward);
    vec3 lightPosition =
        cameraPosition + cameraRight * 0.5 - cameraForward * 0.8660254;
    vec3 light = safeNormalize(
        lightPosition - cameraTarget, vec3(0.45, 0.86, 0.24));
    vec4 orm = useMetallicRoughnessMap
        ? sampleWorldMaterialTexture(
              metallicRoughnessMap, uv, textureDetailLodBias)
        : vec4(1.0);
    float metallic = clamp(orm.b * factors.y, 0.0, 1.0);
    float roughness = clamp(orm.g * factors.z, 0.16, 1.0);
    float occlusion = mix(
        1.0,
        sampleWorldMaterialTexture(
            occlusionMap, uv, textureDetailLodBias).r,
        factors.w);
    float dielectricSpecular = dielectricSpecularIntensity >= 0.0
        ? clamp(dielectricSpecularIntensity, 0.0, 1.0) *
              clamp(orm.a, 0.0, 1.0)
        : 0.04;
    vec3 reflectanceAtNormal = mix(
        vec3(dielectricSpecular),
        albedo,
        metallic);

    vec3 direct = evaluateDirectPbr(
        normal, view, light, albedo, reflectanceAtNormal, roughness, metallic);

    float normalDotView = max(dot(normal, view), 0.0);
    vec3 fresnel = fresnelSchlickRoughness(
        normalDotView, reflectanceAtNormal, roughness);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    vec3 reflection = reflect(-view, normal);
    vec3 environmentIrradiance =
        3.14159265 * sampleNeutralEnvironment(environmentMap, normal, 1.0);
    vec3 environmentRadiance =
        sampleNeutralEnvironment(environmentMap, reflection, roughness);
    vec3 singleScattering;
    vec3 multiScattering;
    computeMultiscattering(
        normal,
        view,
        reflectanceAtNormal,
        roughness,
        singleScattering,
        multiScattering);
    vec3 cosineWeightedIrradiance = environmentIrradiance / 3.14159265;
    vec3 totalScattering = singleScattering + multiScattering;
    float remainingEnergy = 1.0 - max(
        max(totalScattering.r, totalScattering.g), totalScattering.b);
    vec3 diffuseIbl = albedo * (1.0 - metallic) * max(remainingEnergy, 0.0) *
                      cosineWeightedIrradiance * 1.26 * occlusion;
    vec3 specularIbl =
        (environmentRadiance * singleScattering +
         multiScattering * cosineWeightedIrradiance) * 0.44;
    specularIbl *= clamp(specularIblScale, 0.0, 1.0);
    specularIbl *= computeSpecularOcclusion(normalDotView, occlusion, roughness);
    vec3 ambient = diffuseWeight * albedo * 0.56;
    vec3 shaded = direct + diffuseIbl + specularIbl + ambient;
    vec3 emissive = clamp(
        sampleWorldMaterialTexture(
            emissiveMap, uv, textureDetailLodBias).rgb,
        0.0,
        1.0) *
                    max(emissiveFactor, vec3(0.0));
    if (specularIblScale < 0.5) {
        // PLA may encode only a sparse layer-5 catchlight in this map. Gate
        // the plain-Eye diffuse fill per pixel so authored emissive regions
        // stay exact without darkening the rest of Geodude's eye.
        float emissiveCoverage = clamp(
            max(emissive.r, max(emissive.g, emissive.b)),
            0.0,
            1.0);
        shaded = mix(
            shaded,
            albedo,
            0.25 * (1.0 - emissiveCoverage));
    }
    return max(shaded + emissive, vec3(0.0));
}

vec3 nativeIkCharacterRgbToHsv(vec3 color) {
    vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(
        vec4(color.bg, k.wz),
        vec4(color.gb, k.xy),
        step(color.b, color.g));
    vec4 q = mix(
        vec4(p.xyw, color.r),
        vec4(color.r, p.yzx),
        step(p.x, color.r));
    float d = q.x - min(q.w, q.y);
    float e = 1e-10;
    return vec3(
        abs(q.z + (q.w - q.y) / (6.0 * d + e)),
        d / (q.x + e),
        q.x);
}

vec3 nativeIkCharacterHsvToRgb(vec3 hsv) {
    vec3 p = abs(
        fract(hsv.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) *
            6.0 -
        3.0);
    return hsv.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), hsv.y);
}

vec3 nativeIkCharacterRotateHue(vec3 color, float hueOffset) {
    vec3 hsv = nativeIkCharacterRgbToHsv(max(color, vec3(0.0)));
    hsv.x = fract(hsv.x + hueOffset);
    return nativeIkCharacterHsvToRgb(hsv);
}

vec3 sampleZaLocalReflectionProbe(sampler2D environmentMap,
                                  vec3 direction,
                                  float sourceLod,
                                  float fallbackRoughness);

vec3 evaluateNativeIkCharacter(vec3 albedo,
                               vec2 uv,
                               vec3 position,
                               vec3 sourceNormal,
                               vec4 sourceTangent,
                               vec3 cameraPosition,
                               vec3 cameraForwardPacked,
                               vec3 cameraTarget,
                               sampler2D normalMap,
                               sampler2D shadowSpecMap,
                               sampler2D occlusionMap,
                               sampler2D rimResponseMap,
                               sampler2D environmentMap,
                               float textureDetailLodBias,
                               vec4 factors,
                               vec3 rimParameters,
                               vec4 surfaceParameters,
                               vec4 shadowProcessParameters,
                               vec4 midProcessParameters,
                               vec4 darkProcessParameters) {
    float qualityDetail = clamp(
        (0.90 - textureDetailLodBias) / 1.30,
        0.0,
        1.0);
    vec3 normal = mappedWorldNormal(
        uv,
        position,
        sourceNormal,
        sourceTangent,
        normalMap,
        textureDetailLodBias,
        // The shared PBR normal helper applies a 1.25 presentation boost.
        // Z-A IkCharacter's NormalHeight is already an authored shader
        // amplitude, so cancel that boost and preserve the source value.
        factors.x * 0.8);
    float faceDirection = gl_FrontFacing ? 1.0 : -1.0;
    vec3 geometricNormal = safeNormalize(
        sourceNormal,
        vec3(0.0, 1.0, 0.0)) * faceDirection;
    vec3 cameraForward = safeNormalize(
        cameraForwardPacked,
        normalize(vec3(0.0, -0.6139406, -0.7893522)));
    vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
    if (dot(cameraRight, cameraRight) < 1e-6) {
        cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
    }
    cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
    vec3 viewDirection = safeNormalize(
        cameraPosition - position,
        -cameraForward);
    vec3 lightPosition =
        cameraPosition + cameraRight * 0.5 - cameraForward * 0.8660254;
    vec3 lightDirection = safeNormalize(
        lightPosition - cameraTarget,
        vec3(0.45, 0.86, 0.24));
    vec4 shadowSpec = sampleWorldMaterialTexture(
        shadowSpecMap,
        uv,
        textureDetailLodBias);
    vec4 surfaceControl = sampleWorldMaterialTexture(
        occlusionMap,
        uv,
        textureDetailLodBias);
    // Z-A resolves this AO sample as a weight between scene ambient and base
    // color before authored shadow color. It is not a final albedo multiplier.
    float aoBaseWeight = clamp(
        surfaceControl.r * max(factors.w, 0.0),
        0.0,
        1.0);
    float metallic = clamp(surfaceControl.g, 0.0, 1.0);
    float specularOffset = surfaceControl.b * 1.5 - 0.5;
    float specularContrast = surfaceControl.a * 5.0;
    float reflectionBlur = max(surfaceParameters.x, 0.0);
    float diffusionLevels = clamp(surfaceParameters.y, 0.0, 1.0);
    float shadowingGiGain = clamp(surfaceParameters.w, 0.0, 1.0);
    float normalDotLightSigned = dot(normal, lightDirection);
    float lambert = max(normalDotLightSigned, 0.0);
    float wrappedLambert = clamp(
        normalDotLightSigned * 0.5 + 0.5,
        0.0,
        1.0);
    // HalfLambertBias is a response selector, not an additive light bias.
    // Adding a source value of 1 saturated every normal and erased stone,
    // feather, and skin relief. Blend ordinary and wrapped Lambert instead.
    float halfLambert = mix(
        lambert,
        wrappedLambert,
        clamp(factors.y, 0.0, 1.0));
    halfLambert = mix(
        halfLambert,
        sqrt(max(halfLambert, 0.0)),
        diffusionLevels);
    float geometricLambert = max(dot(geometricNormal, lightDirection), 0.0);
    float geometricWrappedLambert = clamp(
        dot(geometricNormal, lightDirection) * 0.5 + 0.5,
        0.0,
        1.0);
    float geometricHalfLambert = mix(
        geometricLambert,
        geometricWrappedLambert,
        clamp(factors.y, 0.0, 1.0));
    geometricHalfLambert = mix(
        geometricHalfLambert,
        sqrt(max(geometricHalfLambert, 0.0)),
        diffusionLevels);
    // IkCharacter carries a second lighting-domain transform after its
    // half-Lambert response. The source default is Shift=-0.5 and
    // Contrast=0, so center shift around -0.5 rather than interpreting the
    // raw signed value as an additive darkness term. The bounded scale keeps
    // authored outliers from becoming the body-wide black bands produced by
    // the old AO approximation.
    // Existing cooked assets predate the source color-process payload and
    // therefore carry an all-zero block. HueShiftBias is authored well above
    // zero in IkCharacter, so it also serves as a backwards-compatible
    // presence marker until those assets are recooked.
    bool hasAuthoredColorProcess = shadowProcessParameters.w > 0.05;
    float authoredShadowBias = hasAuthoredColorProcess
        ? shadowProcessParameters.x
        : 1.0;
    float authoredShadowShift = hasAuthoredColorProcess
        ? shadowProcessParameters.y
        : -0.5;
    float authoredShadowContrast = hasAuthoredColorProcess
        ? shadowProcessParameters.z
        : 0.0;
    float authoredShadowDomain = clamp(
        (1.0 - halfLambert) +
            (authoredShadowShift + 0.5) * 0.24,
        0.0,
        1.0);
    authoredShadowDomain = clamp(
        (authoredShadowDomain - 0.5) *
                (1.0 + max(authoredShadowContrast, 0.0) * 1.5) +
            0.5,
        0.0,
        1.0);
    authoredShadowDomain = pow(
        authoredShadowDomain,
        clamp(authoredShadowBias, 0.25, 2.0));
    float shadowAmount =
        authoredShadowDomain * clamp(factors.z, 0.0, 1.0);
    vec3 sourceAlbedo = clamp(albedo, 0.0, 1.0);
    float aoShadowAmount = (1.0 - aoBaseWeight) * shadowingGiGain;
    float combinedShadowAmount = 1.0 -
        (1.0 - shadowAmount) * (1.0 - aoShadowAmount);
    vec3 shadowTint = mix(
        vec3(1.0),
        shadowSpec.rgb,
        combinedShadowAmount);
    vec3 shaded = sourceAlbedo * shadowTint;
    // The decompiled material program performs separate middle- and
    // dark-area hue processing. Preserve that separation from AO: these
    // authored controls tint the existing shadow response and never create a
    // new dark edge around an eye or layer boundary.
    float midArea = 4.0 * combinedShadowAmount *
        (1.0 - combinedShadowAmount);
    midArea = clamp(
        (midArea - 0.5) * (1.0 + max(midProcessParameters.y, 0.0)) +
            0.5 + midProcessParameters.x,
        0.0,
        1.0);
    float darkArea = clamp(
        (combinedShadowAmount - 0.5) *
                (1.0 + max(darkProcessParameters.x, 0.0)) +
            0.5 + midProcessParameters.w,
        0.0,
        1.0);
    float hueStrength = hasAuthoredColorProcess
        ? clamp(shadowProcessParameters.w, 0.0, 1.0)
        : 0.0;
    vec3 midHueColor = nativeIkCharacterRotateHue(
        shaded,
        midProcessParameters.z);
    vec3 darkHueColor = nativeIkCharacterRotateHue(
        shaded,
        darkProcessParameters.y);
    shaded = mix(shaded, midHueColor, midArea * hueStrength * 0.20);
    shaded = mix(shaded, darkHueColor, darkArea * hueStrength * 0.32);
    // The source normal map also perturbs IkCharacter's diffuse term. Its
    // contribution was previously visible only where the authored shadow
    // tint differed strongly from albedo, leaving skin, fur, and pale stone
    // flat even though their full-resolution normals were present. Apply only
    // the bounded difference from the geometric-normal response: broad light
    // and shadow stay unchanged, while High/Ultra recover local relief.
    float normalDetailDelta = clamp(
        halfLambert - geometricHalfLambert,
        -0.22,
        0.22);
    shaded *= 1.0 + normalDetailDelta * qualityDetail * 0.62;
    bool fibreSurface = abs(surfaceParameters.z - 1.0) < 0.25 &&
        rimParameters.b > 0.5;
    bool featherSurface = abs(surfaceParameters.z - 2.0) < 0.25 &&
        factors.x > 0.001;
    float specularStrength = clamp(shadowSpec.a, 0.0, 1.0);
    vec4 rimResponse = rimParameters.b > 0.5
        ? sampleWorldMaterialTexture(
              rimResponseMap,
              uv,
              textureDetailLodBias)
        : vec4(0.0, 0.0, 0.0, 1.0);
    float facing = dot(normal, viewDirection);
    float edge = clamp(1.0 - max(facing, 0.0), 0.0, 1.0);
    float rimOffset = clamp(rimParameters.r, 0.0, 0.99);
    float rimDomain = clamp(
        (edge - rimOffset) / max(1.0 - rimOffset, 1e-4),
        0.0,
        1.0);
    float rim = pow(
        rimDomain,
        max(rimParameters.g, 1.0)) * rimResponse.r;
    float backRim = clamp(-facing, 0.0, 1.0) * rimResponse.g;
    // Surface carriers need a slightly sharper sample than color at this
    // thumbnail scale; otherwise their 1024px strokes prefilter to flat gray.
    float fineFibre = fibreSurface
        ? sampleWorldMaterialTexture(
              rimResponseMap,
              uv,
              textureDetailLodBias - 1.25).a
        : 1.0;
    float coarseFibre = fibreSurface
        ? sampleWorldMaterialTexture(
              rimResponseMap,
              uv,
              textureDetailLodBias + 1.25).a
        : 1.0;
    float fibreRelief = clamp(
        abs(coarseFibre - fineFibre) * 10.0,
        0.0,
        1.0);
    float fibreSignal = clamp(1.0 - fineFibre, 0.0, 1.0);
    float velvet = pow(edge, 2.5);
    float surfaceDetailLight = 0.35 + 0.65 * halfLambert;
    // The compatible SV roughness atlas is a directional strand field. Use
    // its filtered fine/coarse separation as positive-only coat lift: this
    // survives the Inspector's small preview without turning the base color
    // into grime or drawing dark seams around the eyes. Missing optional data
    // remains neutral at lower quality tiers.
    float fibreSheen = qualityDetail * surfaceDetailLight *
        (1.0 - metallic) *
        (fibreSignal * (0.90 + 0.20 * velvet) +
         fibreRelief * (0.30 + 0.15 * velvet));

    vec2 fineFeatherNormal = featherSurface
        ? sampleWorldMaterialTexture(
              normalMap,
              uv,
              textureDetailLodBias - 1.0).xy * 2.0 - 1.0
        : vec2(0.0);
    vec2 coarseFeatherNormal = featherSurface
        ? sampleWorldMaterialTexture(
              normalMap,
              uv,
              textureDetailLodBias + 1.25).xy * 2.0 - 1.0
        : fineFeatherNormal;
    float featherRelief = clamp(max(
        length(fineFeatherNormal - coarseFeatherNormal) * 10.0,
        length(fineFeatherNormal) * 0.50),
        0.0,
        1.0);
    // Relief is already near zero on the atlas' hard, flat beak/claw regions.
    // A second specular threshold incorrectly rejected the feathers as well.
    // The additive-only response cannot draw dark seams around the eyes.
    float featherSheen = featherSurface
        ? qualityDetail * surfaceDetailLight * (1.0 - metallic) *
            featherRelief * (0.32 + pow(edge, 2.0) * 0.05)
        : 0.0;
    vec3 featherTint = mix(sourceAlbedo, vec3(1.0), 0.22);
    vec3 nativeBase = shaded +
        sourceAlbedo * (rim + backRim + fibreSheen) +
        featherTint * featherSheen;

    // IkCharacter's decompiled Z-A body variant has no roughness input or
    // generic PBR outer coat. It shapes direct specular from the authored
    // mask plus per-layer offset/contrast, and samples the material's local
    // reflection cube at the literal ReflectionsBlur LOD.
    vec3 halfDirection = safeNormalize(
        lightDirection + viewDirection,
        normal);
    float normalDotHalf = max(dot(normal, halfDirection), 0.0);
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    float normalDotLight = lambert;
    float specularDomain = clamp(
        normalDotHalf + specularOffset,
        0.0,
        1.0);
    float specularExponent = mix(
        8.0,
        64.0,
        clamp(specularContrast / 5.0, 0.0, 1.0));
    float specularLobe = pow(specularDomain, specularExponent);
    float dielectricSpecular = specularStrength * specularStrength;
    float surfaceSpecular = max(dielectricSpecular, metallic);
    vec3 specularColor = mix(vec3(1.0), sourceAlbedo, metallic);
    vec3 directSpecular = specularColor * surfaceSpecular * specularLobe *
        normalDotLight * 0.72;
    vec3 reflection = reflect(-viewDirection, normal);
    float reflectionRoughness = clamp(
        reflectionBlur * 0.16,
        0.04,
        0.92);
    vec3 environmentRadiance = sampleZaLocalReflectionProbe(
        environmentMap,
        reflection,
        reflectionBlur + max(textureDetailLodBias, 0.0),
        reflectionRoughness);
    float grazingResponse = mix(
        1.0,
        1.35,
        pow(1.0 - normalDotView, 5.0));
    vec3 environmentSpecular = environmentRadiance *
        specularColor * surfaceSpecular * grazingResponse *
        computeSpecularOcclusion(
            normalDotView,
            aoBaseWeight,
            reflectionRoughness) * 0.44;
    vec3 diffuse = nativeBase * (1.0 - metallic * 0.85);
    return max(
        diffuse + directSpecular + environmentSpecular,
        vec3(0.0));
}

vec3 evaluateNativeSssSurface(vec3 albedo,
                              vec2 uv,
                              vec3 position,
                              vec3 sourceNormal,
                              vec4 sourceTangent,
                              vec3 cameraPosition,
                              vec3 cameraForwardPacked,
                              sampler2D normalMap,
                              sampler2D roughnessMap,
                              sampler2D occlusionMap,
                              sampler2D sssMaskMap,
                              sampler2D environmentMap,
                              float textureDetailLodBias,
                              float surfaceProfile,
                              vec4 factors,
                              vec3 subsurfaceColor) {
    vec3 normal = mappedWorldNormal(
        uv,
        position,
        sourceNormal,
        sourceTangent,
        normalMap,
        textureDetailLodBias,
        factors.x);
    vec3 cameraForward = safeNormalize(
        cameraForwardPacked,
        normalize(vec3(0.0, -0.6139406, -0.7893522)));
    vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
    if (dot(cameraRight, cameraRight) < 1e-6) {
        cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
    }
    cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
    vec3 cameraUp = safeNormalize(
        cross(cameraRight, cameraForward),
        vec3(0.0, 1.0, 0.0));
    vec3 viewDirection = safeNormalize(
        cameraPosition - position,
        -cameraForward);
    vec3 lightDirection = safeNormalize(
        cameraRight * 0.45 + cameraUp * 0.86 - cameraForward * 0.24,
        vec3(0.45, 0.86, 0.24));
    vec3 halfDirection = safeNormalize(
        lightDirection + viewDirection,
        normal);
    float roughness = clamp(
        sampleWorldMaterialTexture(
            roughnessMap,
            uv,
            textureDetailLodBias).g * clamp(factors.z, 0.0, 1.0),
        0.04,
        1.0);
    bool fibreSurface = abs(surfaceProfile - 1.0) < 0.25;
    float coarseRoughness = fibreSurface
        ? clamp(
              sampleWorldMaterialTexture(
                  roughnessMap,
                  uv,
                  textureDetailLodBias + 2.0).g *
                  clamp(factors.z, 0.0, 1.0),
              0.04,
              1.0)
        : roughness;
    float ao = mix(
        1.0,
        sampleWorldMaterialTexture(
            occlusionMap,
            uv,
            textureDetailLodBias).r,
        clamp(factors.w, 0.0, 1.0));
    float sssMask = sampleWorldMaterialTexture(
        sssMaskMap,
        uv,
        textureDetailLodBias).r;
    vec3 sourceAlbedo = clamp(albedo, 0.0, 1.0);
    vec3 subsurfaceTint = mix(
        sourceAlbedo,
        max(subsurfaceColor, vec3(0.0)),
        0.35);
    float wrappedNdotL = clamp(
        (dot(normal, lightDirection) + 0.5) / 1.5,
        0.0,
        1.0);
    float subsurfaceFill = clamp(sssMask, 0.0, 1.0) *
        (1.0 - max(dot(normal, lightDirection), 0.0)) * 0.10;
    float specularPower = mix(16.0, 96.0, 1.0 - roughness);
    float sourceSpecular = pow(
        max(dot(normal, halfDirection), 0.0),
        specularPower) * 0.04 * 0.45;
    float nDotV = clamp(dot(normal, viewDirection), 0.0, 1.0);
    vec3 environmentFresnel = fresnelSchlickRoughness(
        nDotV,
        vec3(0.04),
        roughness);
    // Exact SV SSS variation 56 samples diffuse irradiance at the mapped
    // normal (tcb_34, LOD 0) and specular radiance at the reflected view
    // vector (tcb_36, roughness-selected LOD). Source scene cubes are runtime
    // state, so bridge their proven roles through the shared neutral room.
    vec3 environmentDiffuse = sampleNeutralEnvironment(
        environmentMap,
        normal,
        1.0) * sourceAlbedo * (vec3(1.0) - environmentFresnel) * ao *
        (1.26 * 1.18);
    vec3 reflection = reflect(-viewDirection, normal);
    vec3 environmentSpecular = sampleNeutralEnvironment(
        environmentMap,
        reflection,
        roughness) * environmentFresnel *
        computeSpecularOcclusion(nDotV, ao, roughness) * 0.44;
    vec3 directDiffuse = sourceAlbedo * 0.90 * wrappedNdotL * ao;
    // SV supplies two scene-owned environment cubes whose exact captured
    // contents are unavailable offline. Keep the neutral replacement usable
    // as a model-review and gameplay light rig without flattening AO.
    vec3 neutralFill = sourceAlbedo *
        mix(0.08, 0.12, clamp(sssMask, 0.0, 1.0)) *
        mix(1.0, ao, 0.5);
    float qualityDetail = clamp(
        (0.90 - textureDetailLodBias) / 1.30,
        0.0,
        1.0);
    float fibreRelief = clamp(
        (coarseRoughness - roughness) * 3.25,
        0.0,
        1.0);
    float velvet = pow(1.0 - nDotV, 2.5);
    float fibreSheen = fibreSurface
        ? qualityDetail * wrappedNdotL *
              (fibreRelief * (0.18 + 0.14 * velvet) + velvet * 0.08)
        : 0.0;
    return max(
        environmentDiffuse + directDiffuse + neutralFill +
            environmentSpecular +
            subsurfaceTint * subsurfaceFill +
            vec3(sourceSpecular) + sourceAlbedo * fibreSheen,
        vec3(0.0));
}

float decodeSvLocalProbeHalf(uint bits) {
    uint exponentBits = (bits >> 10u) & 31u;
    uint mantissaBits = bits & 1023u;
    float signValue = (bits & 32768u) != 0u ? -1.0 : 1.0;
    if (exponentBits == 0u) {
        return signValue * float(mantissaBits) * exp2(-24.0);
    }
    if (exponentBits == 31u) return 0.0;
    return signValue *
        (1.0 + float(mantissaBits) * (1.0 / 1024.0)) *
        exp2(float(exponentBits) - 15.0);
}

vec3 decodeSvLocalProbeTexel(sampler2D environmentMap, ivec2 texel) {
    vec4 packedRg = texelFetch(environmentMap, texel, 0);
    vec4 packedBa = texelFetch(environmentMap, texel + ivec2(1, 0), 0);
    uvec4 rg = uvec4(round(clamp(packedRg, 0.0, 1.0) * 255.0));
    uvec4 ba = uvec4(round(clamp(packedBa, 0.0, 1.0) * 255.0));
    return vec3(
        decodeSvLocalProbeHalf(rg.r | (rg.g << 8u)),
        decodeSvLocalProbeHalf(rg.b | (rg.a << 8u)),
        decodeSvLocalProbeHalf(ba.r | (ba.g << 8u)));
}

vec3 sampleSvLocalSpecularProbe(sampler2D environmentMap,
                                vec3 direction,
                                float roughness) {
    ivec2 atlasSize = textureSize(environmentMap, 0);
    if (atlasSize.x != atlasSize.y * 3 ||
        atlasSize.y < 2 || (atlasSize.y & 1) != 0) {
        return sampleNeutralEnvironment(environmentMap, direction, roughness);
    }
    int faceSize = atlasSize.y / 2;
    vec3 d = safeNormalize(direction, vec3(0.0, 0.0, 1.0));
    vec3 a = abs(d);
    int face;
    vec2 faceUv;
    if (a.x >= a.y && a.x >= a.z) {
        if (d.x >= 0.0) {
            face = 0;
            faceUv = vec2(-d.z, -d.y) / a.x;
        } else {
            face = 1;
            faceUv = vec2(d.z, -d.y) / a.x;
        }
    } else if (a.y >= a.z) {
        if (d.y >= 0.0) {
            face = 2;
            faceUv = vec2(d.x, d.z) / a.y;
        } else {
            face = 3;
            faceUv = vec2(d.x, -d.z) / a.y;
        }
    } else if (d.z >= 0.0) {
        face = 4;
        faceUv = vec2(d.x, -d.y) / a.z;
    } else {
        face = 5;
        faceUv = vec2(-d.x, -d.y) / a.z;
    }
    vec2 p = clamp(faceUv * 0.5 + 0.5, 0.0, 1.0) *
        float(faceSize) - 0.5;
    ivec2 lo = clamp(
        ivec2(floor(p)), ivec2(0), ivec2(faceSize - 1));
    ivec2 hi = min(lo + ivec2(1), ivec2(faceSize - 1));
    vec2 blend = fract(p);
    ivec2 origin = ivec2(
        (face % 3) * faceSize * 2,
        (face / 3) * faceSize);
    vec3 c00 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(lo.x * 2, lo.y));
    vec3 c10 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(hi.x * 2, lo.y));
    vec3 c01 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(lo.x * 2, hi.y));
    vec3 c11 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(hi.x * 2, hi.y));
    return mix(
        mix(c00, c10, blend.x),
        mix(c01, c11, blend.x),
        blend.y);
}

vec3 sampleZaLocalReflectionProbeMip(sampler2D environmentMap,
                                     vec3 direction,
                                     int baseFaceSize,
                                     int mipLevel) {
    int mipSize = max(baseFaceSize >> mipLevel, 1);
    vec3 d = safeNormalize(direction, vec3(0.0, 0.0, 1.0));
    vec3 a = abs(d);
    int face;
    vec2 faceUv;
    if (a.x >= a.y && a.x >= a.z) {
        if (d.x >= 0.0) {
            face = 0;
            faceUv = vec2(-d.z, -d.y) / a.x;
        } else {
            face = 1;
            faceUv = vec2(d.z, -d.y) / a.x;
        }
    } else if (a.y >= a.z) {
        if (d.y >= 0.0) {
            face = 2;
            faceUv = vec2(d.x, d.z) / a.y;
        } else {
            face = 3;
            faceUv = vec2(d.x, -d.z) / a.y;
        }
    } else if (d.z >= 0.0) {
        face = 4;
        faceUv = vec2(d.x, -d.y) / a.z;
    } else {
        face = 5;
        faceUv = vec2(-d.x, -d.y) / a.z;
    }
    vec2 p = clamp(faceUv * 0.5 + 0.5, 0.0, 1.0) *
        float(mipSize) - 0.5;
    ivec2 lo = clamp(
        ivec2(floor(p)), ivec2(0), ivec2(mipSize - 1));
    ivec2 hi = min(lo + ivec2(1), ivec2(mipSize - 1));
    vec2 blend = fract(p);
    int mipStripY = baseFaceSize * 4 - mipSize * 4;
    ivec2 origin = ivec2(
        (face % 3) * mipSize * 2,
        mipStripY + (face / 3) * mipSize);
    vec3 c00 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(lo.x * 2, lo.y));
    vec3 c10 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(hi.x * 2, lo.y));
    vec3 c01 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(lo.x * 2, hi.y));
    vec3 c11 = decodeSvLocalProbeTexel(
        environmentMap, origin + ivec2(hi.x * 2, hi.y));
    return mix(
        mix(c00, c10, blend.x),
        mix(c01, c11, blend.x),
        blend.y);
}

vec3 sampleZaLocalReflectionProbe(sampler2D environmentMap,
                                  vec3 direction,
                                  float sourceLod,
                                  float fallbackRoughness) {
    ivec2 atlasSize = textureSize(environmentMap, 0);
    if (atlasSize.x < 6 || atlasSize.x % 6 != 0) {
        return sampleNeutralEnvironment(
            environmentMap, direction, fallbackRoughness);
    }
    int faceSize = atlasSize.x / 6;
    if (atlasSize.y != faceSize * 4 - 2 ||
        (faceSize & (faceSize - 1)) != 0) {
        return sampleNeutralEnvironment(
            environmentMap, direction, fallbackRoughness);
    }
    int maxMip = int(round(log2(float(faceSize))));
    float lod = clamp(sourceLod, 0.0, float(maxMip));
    int lo = int(floor(lod));
    int hi = min(lo + 1, maxMip);
    return mix(
        sampleZaLocalReflectionProbeMip(
            environmentMap, direction, faceSize, lo),
        sampleZaLocalReflectionProbeMip(
            environmentMap, direction, faceSize, hi),
        fract(lod));
}

vec3 nativeFresnelEffectBase(vec3 baseMap,
                             vec4 baseColor,
                             vec4 surfaceControls) {
    vec3 tinted = max(baseMap, vec3(0.0)) * max(baseColor.rgb, vec3(0.0));
    float luminance = dot(tinted, vec3(0.299, 0.587, 0.114));
    return max(
        mix(vec3(luminance), tinted, max(surfaceControls.x, 0.0)),
        vec3(0.0));
}

vec3 evaluateNativeFresnelEffectLayer(
    vec3 litBase,
    vec3 primaryColor,
    vec2 uv,
    vec3 position,
    vec3 sourceNormal,
    vec4 sourceTangent,
    vec3 cameraPosition,
    vec3 cameraForwardPacked,
    sampler2D layerNormalMap,
    sampler2D occlusionMap,
    sampler2D layerMap,
    sampler2D environmentMap,
    float textureDetailLodBias,
    vec4 factors,
    vec4 layerColor,
    vec4 fresnelControls,
    vec4 surfaceControls) {
    vec3 normal = mappedWorldNormal(
        uv,
        position,
        sourceNormal,
        sourceTangent,
        layerNormalMap,
        textureDetailLodBias,
        max(surfaceControls.w, 0.0));
    vec3 cameraForward = safeNormalize(
        cameraForwardPacked,
        vec3(0.0, 0.0, -1.0));
    vec3 viewDirection = safeNormalize(
        cameraPosition - position,
        -cameraForward);
    float nDotV = clamp(dot(normal, viewDirection), 0.0, 1.0);
    float angleTerm = 1.0 - max(
        nDotV - clamp(fresnelControls.w, 0.0, 1.0),
        0.0);
    float fresnelAlpha = mix(
        clamp(fresnelControls.y, 0.0, 1.0),
        clamp(fresnelControls.z, 0.0, 1.0),
        pow(clamp(angleTerm, 0.0, 1.0), 5.0));
    float ao = mix(
        1.0,
        sampleWorldMaterialTexture(
            occlusionMap,
            uv,
            textureDetailLodBias).r,
        clamp(factors.w, 0.0, 1.0));
    vec3 additiveLayer = sampleWorldMaterialTexture(
            layerMap,
            uv,
            textureDetailLodBias).rgb *
        max(layerColor.rgb, vec3(0.0)) *
        ao * max(surfaceControls.y, 0.0) *
        (1.0 - fresnelAlpha);
    vec3 environmentRadiance = sampleSvLocalSpecularProbe(
        environmentMap,
        reflect(-viewDirection, normal),
        clamp(factors.z, 0.04, 1.0));
    vec3 f0 = mix(
        vec3(0.04),
        clamp(primaryColor, 0.0, 1.0),
        clamp(factors.y, 0.0, 1.0));
    vec3 localProbe = environmentRadiance *
        fresnelSchlick(nDotV, f0) *
        max(fresnelControls.x, 0.0) * ao * 0.44;
    return max(litBase + additiveLayer + localProbe, vec3(0.0));
}

vec3 evaluateNativeGastlyFace(vec3 albedo,
                              vec2 uv,
                              vec3 position,
                              vec3 sourceNormal,
                              vec4 sourceTangent,
                              vec3 cameraPosition,
                              vec3 cameraForwardPacked,
                              vec3 cameraTarget,
                              sampler2D normalMap,
                              sampler2D shadowSpecMap,
                              sampler2D occlusionMap,
                              sampler2D rimMaskMap,
                              float textureDetailLodBias,
                              float normalScale,
                              float occlusionStrength,
                              bool concealTongue) {
    vec3 normal = mappedWorldNormal(
        uv,
        position,
        sourceNormal,
        sourceTangent,
        normalMap,
        textureDetailLodBias,
        normalScale);
    vec3 cameraForward = safeNormalize(
        cameraForwardPacked,
        normalize(vec3(0.0, -0.6139406, -0.7893522)));
    vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
    if (dot(cameraRight, cameraRight) < 1e-6) {
        cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
    }
    cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
    vec3 cameraUp = safeNormalize(
        cross(cameraRight, cameraForward),
        vec3(0.0, 1.0, 0.0));
    vec3 view = safeNormalize(cameraPosition - position, -cameraForward);
    vec3 light = safeNormalize(
        cameraRight * 0.45 + cameraUp * 0.86 - cameraForward * 0.24,
        vec3(0.45, 0.86, 0.24));
    vec4 shadowSpec = sampleWorldMaterialTexture(
        shadowSpecMap,
        uv,
        textureDetailLodBias);
    float tongueMask = smoothstep(0.48, 0.52, shadowSpec.a);
    if (concealTongue && tongueMask > 0.5) {
        discard;
    }
    float occlusion = mix(
        1.0,
        sampleWorldMaterialTexture(
            occlusionMap,
            uv,
            textureDetailLodBias).r,
        clamp(occlusionStrength, 0.0, 1.0));
    float halfLambert = clamp(dot(normal, light) * 0.5 + 0.6, 0.0, 1.0);
    float shadowAmount = (1.0 - halfLambert) * 0.7;
    vec3 shaded = mix(albedo, shadowSpec.rgb, shadowAmount) * occlusion;

    vec3 halfVector = safeNormalize(view + light, normal);
    float sourceSpecularMask = max(shadowSpec.a - 0.5 * tongueMask, 0.0);
    float specular = pow(max(dot(normal, halfVector), 0.0), 32.0) *
        sourceSpecularMask;
    // Z-A gives the tongue a broad, soft highlight and much gentler
    // self-shadowing than the surrounding spectral body. Applying the body
    // shadow ramp to it flattened the central groove into hard-looking slabs.
    float tongueDiffuse = mix(
        0.82,
        1.06,
        smoothstep(0.0, 1.0, halfLambert));
    vec3 tongueShaded = albedo * tongueDiffuse * mix(1.0, occlusion, 0.25);
    float ndh = max(dot(normal, halfVector), 0.0);
    float tongueSpecular =
        pow(ndh, 12.0) * 0.105 + pow(ndh, 48.0) * 0.04;
    shaded = mix(shaded, tongueShaded, tongueMask);
    specular = mix(specular, tongueSpecular, tongueMask);
    float edge = clamp(1.0 - max(dot(normal, view), 0.0), 0.0, 1.0);
    float rimDomain = clamp((edge - 0.4) / 0.6, 0.0, 1.0);
    float rimMask = sampleWorldMaterialTexture(
        rimMaskMap,
        uv,
        textureDetailLodBias).r;
    float rim = pow(rimDomain, 5.0) * 0.8 * rimMask;
    float backRim = clamp(-dot(normal, view), 0.0, 1.0) *
        0.08 * rimMask;
    rim *= mix(1.0, 0.18, tongueMask);
    backRim *= mix(1.0, 0.18, tongueMask);
    return max(
        shaded + vec3(specular) + albedo * (rim + backRim),
        vec3(0.0));
}

vec3 evaluateNativeEyeClearCoat(vec3 linearColor,
                                vec2 uv,
                                vec3 position,
                                vec3 sourceNormal,
                                vec4 sourceTangent,
                                vec3 cameraPosition,
                                vec3 cameraForwardPacked,
                                vec3 cameraTarget,
                                sampler2D normalMap,
                                sampler2D environmentMap,
                                float normalScale,
                                vec4 clearCoatParameters,
                                vec4 clearCoatBaseAndMetallic,
                                vec3 highlightEmission) {
    vec3 normal = mappedWorldNormal(
        uv,
        position,
        sourceNormal,
        sourceTangent,
        normalMap,
        0.0,
        normalScale);
    vec3 cameraForward = safeNormalize(
        cameraForwardPacked,
        normalize(vec3(0.0, -0.6139406, -0.7893522)));
    vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
    if (dot(cameraRight, cameraRight) < 1e-6) {
        cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
    }
    cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
    vec3 view = safeNormalize(cameraPosition - position, -cameraForward);
    vec3 lightPosition =
        cameraPosition + cameraRight * 0.5 - cameraForward * 0.8660254;
    vec3 light = safeNormalize(
        lightPosition - cameraTarget, vec3(0.45, 0.86, 0.24));
    vec3 halfVector = safeNormalize(view + light, normal);
    float normalDotView = max(dot(normal, view), 0.0);
    float normalDotLight = max(dot(normal, light), 0.0);
    float normalDotHalf = max(dot(normal, halfVector), 0.0);
    float viewDotHalf = max(dot(view, halfVector), 0.0);
    float clearCoatMetallic = clearCoatBaseAndMetallic.w;
    if (clearCoatMetallic < -0.5) return linearColor;
    float roughness = clamp(clearCoatParameters.x, 0.04, 1.0);
    vec3 clearCoatBaseColor = max(
        clearCoatBaseAndMetallic.xyz,
        vec3(0.0));
    vec3 clearCoatF0 = mix(
        vec3(0.04),
        clearCoatBaseColor,
        clamp(clearCoatMetallic, 0.0, 1.0));
    float distribution = distributionGGX(normalDotHalf, roughness);
    float geometry = geometrySchlickGGX(normalDotView, roughness) *
                     geometrySchlickGGX(normalDotLight, roughness);
    vec3 fresnel = fresnelSchlick(viewDotHalf, clearCoatF0);
    vec3 direct = distribution * geometry * fresnel /
                  max(4.0 * normalDotView * normalDotLight, 1e-4) *
                  (0.72 * 3.14159265) * normalDotLight;
    vec3 reflection = reflect(-view, normal);
    vec3 environment = sampleNeutralEnvironment(
        environmentMap, reflection, roughness) *
        fresnelSchlickRoughness(normalDotView, clearCoatF0, roughness) * 0.44;
    vec3 coatLighting = direct + environment;
    float coatPeak = max(
        coatLighting.x,
        max(coatLighting.y, coatLighting.z));
    vec3 boundedCoat = coatLighting / (1.0 + coatPeak);
    const float sceneCoatBridge = 0.20;
    vec3 result = linearColor *
        (vec3(1.0) - fresnel * 0.18) +
        boundedCoat * sceneCoatBridge;

    // fp_c8[96] is an optional source point-light position/enable field. Its
    // bound source value and light energy remain unavailable; preserve all
    // proven material inputs while isolating that unknown state in the same
    // bounded viewer-light bridge used by the other backends.
    highlightEmission = max(highlightEmission, vec3(0.0));
    float highlightEnergy = max(
        highlightEmission.x,
        max(highlightEmission.y, highlightEmission.z));
    float highlightEnabled = clamp(clearCoatParameters.w, 0.0, 1.0);
    if (highlightEnabled > 0.0 && highlightEnergy > 1e-5) {
        float highlightRoughness = clamp(
            clearCoatParameters.y,
            0.04,
            1.0);
        float highlightMetallic = clamp(
            clearCoatParameters.z,
            0.0,
            1.0);
        vec3 highlightTint = highlightEmission / highlightEnergy;
        vec3 highlightF0 = mix(
            vec3(0.04),
            highlightTint,
            highlightMetallic);
        float highlightDistribution = distributionGGX(
            normalDotHalf,
            highlightRoughness);
        float highlightGeometry =
            geometrySchlickGGX(normalDotView, highlightRoughness) *
            geometrySchlickGGX(normalDotLight, highlightRoughness);
        vec3 highlightFresnel = fresnelSchlick(viewDotHalf, highlightF0);
        vec3 highlightDirect =
            highlightDistribution * highlightGeometry * highlightFresnel /
            max(4.0 * normalDotView * normalDotLight, 1e-4) *
            (0.72 * 3.14159265) * normalDotLight;
        float highlightPeak = max(
            highlightDirect.x,
            max(highlightDirect.y, highlightDirect.z));
        vec3 boundedHighlight = highlightDirect / (1.0 + highlightPeak);
        const float sceneHighlightBridge = 0.12;
        result += boundedHighlight *
            (sceneHighlightBridge * highlightEnabled *
             (1.0 - exp(-highlightEnergy)));
    }
    return max(result, vec3(0.0));
}
