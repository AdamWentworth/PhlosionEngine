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
    if (max(abs(clip.x), max(abs(clip.y), abs(clip.z))) >= 1.0) return 1.0;
    vec2 uv = clip.xy * vec2(0.5, WORLD_SHADOW_Y * 0.5) + vec2(0.5, 0.5);
    float referenceDepth = clip.z * 0.5 + 0.5 - WORLD_SHADOW_PARAMS.y;
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 tap = uv + vec2(float(x), float(y)) * WORLD_SHADOW_PARAMS.w;
            float depth = min(tap.x, tap.y) < 0.0 || max(tap.x, tap.y) > 1.0
                ? 1.0 : WORLD_SHADOW_SAMPLE(tap);
            visibility += step(referenceDepth, depth);
        }
    }
    return mix(1.0, visibility / 9.0, WORLD_SHADOW_PARAMS.x);
}

void updateEnvironmentDirectionalVisibility(vec3 position, vec3 normal, float mode) {
    environmentDirectionalVisibility = 1.0;
    if (mode >= 1.5 && (mode < 249.5 || mode == 254.0) && !(mode > 2.5 && mode < 3.5))
        environmentDirectionalVisibility = environmentShadowVisibility(position, normal);
}
