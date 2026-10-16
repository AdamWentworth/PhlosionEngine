// Manual bounded 3x3 PCF preserves the same comparison on all supported APIs.
float environmentShadowVisibility(vec3 position, vec3 normal) {
    if (ENV_SUN.w < 0.5 || WORLD_SHADOW_PARAMS.x <= 0.0) return 1.0;
    float normalLength = dot(normal, normal);
    if (normalLength > 0.00000001) {
        normal *= inversesqrt(normalLength);
        float orientation = dot(normal, environmentLightDirection()) < 0.0 ? -1.0 : 1.0;
        position += normal * (orientation * WORLD_SHADOW_PARAMS.z);
    }
    vec4 projected = WORLD_SHADOW_CLIP(position);
    if (projected.w <= 0.00000001) return 1.0;
    vec3 clip = projected.xyz / projected.w;
    vec2 uv = clip.xy * vec2(0.5, WORLD_SHADOW_Y * 0.5) + vec2(0.5, 0.5);
    float referenceDepth = clip.z * 0.5 + 0.5 - WORLD_SHADOW_PARAMS.y;
    // Compare each nearest-filtered texel against the receiver plane at that
    // texel's centre. One depth for the whole PCF footprint self-shadows slopes.
    vec3 dx = dFdx(vec3(uv, referenceDepth));
    vec3 dy = dFdy(vec3(uv, referenceDepth));
    float determinant = dx.x * dy.y - dx.y * dy.x;
    vec2 depthGradient = vec2(0.0, 0.0);
    if (abs(determinant) > 0.00000000000000000001)
        depthGradient = vec2(dy.y * dx.z - dx.y * dy.z,
                             dx.x * dy.z - dy.x * dx.z) / determinant;
    if (max(abs(clip.x), max(abs(clip.y), abs(clip.z))) >= 1.0) return 1.0;
    float texelSize = WORLD_SHADOW_PARAMS.w;
    vec2 centre = (floor(uv / texelSize) + vec2(0.5, 0.5)) * texelSize;
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 tap = centre + vec2(float(x), float(y)) * texelSize;
            float depth = min(tap.x, tap.y) < 0.0 || max(tap.x, tap.y) > 1.0
                ? 1.0 : WORLD_SHADOW_SAMPLE(tap);
            float planeDepth = referenceDepth + clamp(dot(depthGradient, tap - uv), -0.01, 0.01);
            visibility += step(planeDepth, depth);
        }
    }
    return mix(1.0, visibility / 9.0, WORLD_SHADOW_PARAMS.x);
}

void updateEnvironmentDirectionalVisibility(vec3 position, vec3 normal, float mode) {
    environmentDirectionalVisibility = 1.0;
    if (mode >= 1.5 && (mode < 249.5 || mode == 254.0) && !(mode > 2.5 && mode < 3.5))
        environmentDirectionalVisibility = environmentShadowVisibility(position, normal);
}
