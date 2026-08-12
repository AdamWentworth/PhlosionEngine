vec3 safeNormalize(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-8 ? value * inversesqrt(lengthSquared) : fallback;
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
    vec3 texel = sampleWorldMaterialTexture(
        map, uv, textureDetailLodBias).xyz;
    vec2 mappedXY = (texel.xy * 2.0 - 1.0) * max(normalScale, 0.0) * 1.25;
    float authoredZ = texel.z * 2.0 - 1.0;
    float reconstructedZ = sqrt(max(1.0 - clamp(dot(mappedXY, mappedXY), 0.0, 1.0), 0.0));
    float mappedZ = mix(authoredZ, reconstructedZ, texel.z <= (1.5 / 255.0) ? 1.0 : 0.0);
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
                           float specularIblScale) {
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
    vec4 orm = sampleWorldMaterialTexture(
        metallicRoughnessMap, uv, textureDetailLodBias);
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
                               float textureDetailLodBias,
                               vec4 factors,
                               vec3 rimParameters) {
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
    float occlusion = mix(
        1.0,
        sampleWorldMaterialTexture(
            occlusionMap,
            uv,
            textureDetailLodBias).r,
        clamp(factors.w, 0.0, 1.0));
    float halfLambert = clamp(
        dot(normal, lightDirection) * 0.5 +
            (0.5 + clamp(factors.y, 0.0, 1.0)),
        0.0,
        1.0);
    float shadowAmount =
        (1.0 - halfLambert) * clamp(factors.z, 0.0, 1.0);
    vec3 sourceAlbedo = clamp(albedo, 0.0, 1.0);
    vec3 shadowTint = mix(vec3(1.0), shadowSpec.rgb, shadowAmount);
    vec3 shaded = sourceAlbedo * shadowTint * occlusion;
    vec3 halfVector = safeNormalize(
        viewDirection + lightDirection,
        normal);
    float specular =
        pow(max(dot(normal, halfVector), 0.0), 32.0) * shadowSpec.a;
    vec2 rimResponse = rimParameters.b > 0.5
        ? sampleWorldMaterialTexture(
              rimResponseMap,
              uv,
              textureDetailLodBias).rg
        : vec2(0.0);
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
    return max(
        shaded + vec3(specular) + sourceAlbedo * (rim + backRim),
        vec3(0.0));
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
                                float clearCoatRoughness,
                                float clearCoatCoverage) {
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
    float roughness = clamp(clearCoatRoughness, 0.04, 1.0);
    float distribution = distributionGGX(normalDotHalf, roughness);
    float geometry = geometrySchlickGGX(normalDotView, roughness) *
                     geometrySchlickGGX(normalDotLight, roughness);
    vec3 fresnel = fresnelSchlick(viewDotHalf, vec3(0.04));
    vec3 direct = distribution * geometry * fresnel /
                  max(4.0 * normalDotView * normalDotLight, 1e-4) *
                  (0.72 * 3.14159265) * normalDotLight;
    vec3 reflection = reflect(-view, normal);
    vec3 environment = sampleNeutralEnvironment(
        environmentMap, reflection, roughness) *
        fresnelSchlickRoughness(normalDotView, vec3(0.04), roughness) * 0.44;
    clearCoatCoverage = clamp(clearCoatCoverage, 0.0, 1.0);
    return max(
        linearColor *
            (vec3(1.0) - fresnel * (0.18 * clearCoatCoverage)) +
            (direct + environment) * clearCoatCoverage,
        vec3(0.0));
}
