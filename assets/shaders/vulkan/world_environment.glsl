vec3 fresnelSchlickRoughness(float cosineTheta,
                             vec3 reflectanceAtNormal,
                             float roughness) {
    float factor = pow(clamp(1.0 - cosineTheta, 0.0, 1.0), 5.0);
    vec3 reflectanceAtGrazing = max(vec3(1.0 - roughness), reflectanceAtNormal);
    return reflectanceAtNormal +
           (reflectanceAtGrazing - reflectanceAtNormal) * factor;
}

vec2 dfgApprox(vec3 normal, vec3 view, float roughness) {
    float normalDotView = clamp(dot(normal, view), 0.0, 1.0);
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * normalDotView)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

void computeMultiscattering(vec3 normal,
                            vec3 view,
                            vec3 reflectanceAtNormal,
                            float roughness,
                            out vec3 singleScattering,
                            out vec3 multiScattering) {
    vec2 fab = dfgApprox(normal, view, roughness);
    vec3 fresnelSingle = reflectanceAtNormal * fab.x + fab.y;
    float singleEnergy = fab.x + fab.y;
    float missingEnergy = 1.0 - singleEnergy;
    vec3 averageFresnel = reflectanceAtNormal +
                          (vec3(1.0) - reflectanceAtNormal) * 0.047619;
    vec3 fresnelMulti = fresnelSingle * averageFresnel /
                        max(vec3(1.0) - missingEnergy * averageFresnel, vec3(1e-5));
    singleScattering = fresnelSingle;
    multiScattering = fresnelMulti * missingEnergy;
}

float computeSpecularOcclusion(float normalDotView,
                               float ambientOcclusion,
                               float roughness) {
    return clamp(
        pow(normalDotView + ambientOcclusion, exp2(-16.0 * roughness - 1.0)) -
            1.0 + ambientOcclusion,
        0.0,
        1.0);
}

float roughnessToNeutralMip(float roughness) {
    const float r0 = 1.0;
    const float m0 = -2.0;
    const float r1 = 0.8;
    const float m1 = -1.0;
    const float r4 = 0.4;
    const float m4 = 2.0;
    const float r5 = 0.305;
    const float m5 = 3.0;
    const float r6 = 0.21;
    const float m6 = 4.0;
    float value = clamp(roughness, 0.0, 1.0);
    if (value >= r1) return (r0 - value) * (m1 - m0) / (r0 - r1) + m0;
    if (value >= r4) return (r1 - value) * (m4 - m1) / (r1 - r4) + m1;
    if (value >= r5) return (r4 - value) * (m5 - m4) / (r4 - r5) + m4;
    if (value >= r6) return (r5 - value) * (m6 - m5) / (r5 - r6) + m5;
    return -2.0 * log2(max(1.16 * value, 1e-4));
}

float cubeFace(vec3 direction) {
    vec3 absoluteDirection = abs(direction);
    if (absoluteDirection.x > absoluteDirection.z) {
        if (absoluteDirection.x > absoluteDirection.y) {
            return direction.x > 0.0 ? 0.0 : 3.0;
        }
        return direction.y > 0.0 ? 1.0 : 4.0;
    }
    if (absoluteDirection.z > absoluteDirection.y) {
        return direction.z > 0.0 ? 2.0 : 5.0;
    }
    return direction.y > 0.0 ? 1.0 : 4.0;
}

vec2 cubeFaceUv(vec3 direction, float face) {
    vec2 uv;
    if (face == 0.0) {
        uv = vec2(direction.z, direction.y) / abs(direction.x);
    } else if (face == 1.0) {
        uv = vec2(-direction.x, -direction.z) / abs(direction.y);
    } else if (face == 2.0) {
        uv = vec2(-direction.x, direction.y) / abs(direction.z);
    } else if (face == 3.0) {
        uv = vec2(-direction.z, direction.y) / abs(direction.x);
    } else if (face == 4.0) {
        uv = vec2(-direction.x, direction.z) / abs(direction.y);
    } else {
        uv = vec2(direction.x, direction.y) / abs(direction.z);
    }
    return 0.5 * (uv + 1.0);
}

vec3 sampleLinearEnvironment(sampler2D map, vec2 uv) {
    return textureLod(map, uv, 0.0).rgb;
}

vec3 bilinearCubeUv(sampler2D environmentMap,
                    vec3 direction,
                    float mipInteger) {
    const float minimumMipLevel = 4.0;
    const float minimumTileSize = 16.0;
    vec2 dimensions = max(vec2(textureSize(environmentMap, 0)), vec2(1.0));
    vec2 texelSize = 1.0 / dimensions;
    float maximumFaceSize = max(dimensions.x / 3.0, minimumTileSize);
    float maximumMip = max(log2(maximumFaceSize), minimumMipLevel);
    float face = cubeFace(direction);
    float filterInteger = max(minimumMipLevel - mipInteger, 0.0);
    mipInteger = max(mipInteger, minimumMipLevel);
    float faceSize = exp2(mipInteger);
    vec2 uv = cubeFaceUv(direction, face) * (faceSize - 2.0) + 1.0;
    if (face > 2.0) {
        uv.y += faceSize;
        face -= 3.0;
    }
    uv.x += face * faceSize;
    uv.x += filterInteger * 3.0 * minimumTileSize;
    uv.y += 4.0 * (exp2(maximumMip) - faceSize);
    return sampleLinearEnvironment(environmentMap, uv * texelSize);
}

vec3 sampleNeutralEnvironment(sampler2D environmentMap,
                              vec3 direction,
                              float roughness) {
    vec3 sampleDirection = safeNormalize(direction, vec3(0.0, 1.0, 0.0));
    vec2 dimensions = max(vec2(textureSize(environmentMap, 0)), vec2(1.0));
    float maximumFaceSize = max(dimensions.x / 3.0, 16.0);
    float maximumMip = max(log2(maximumFaceSize), 4.0);
    float mip = clamp(roughnessToNeutralMip(roughness), -2.0, maximumMip);
    float mipFraction = fract(mip);
    float mipInteger = floor(mip);
    vec3 color0 = bilinearCubeUv(environmentMap, sampleDirection, mipInteger);
    if (mipFraction == 0.0) return color0;
    vec3 color1 = bilinearCubeUv(environmentMap, sampleDirection, mipInteger + 1.0);
    return mix(color0, color1, mipFraction);
}
