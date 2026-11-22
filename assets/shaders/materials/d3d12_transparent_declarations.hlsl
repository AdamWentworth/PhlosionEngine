#include "phlosion/d3d12_surface_color.hlsl"
float3 safeNormalize(float3 value, float3 fallback) {
    float len2 = dot(value, value);
    if (len2 < 1e-8f) return fallback;
    return value * rsqrt(len2);
}
__PHLOSION_SHARED_WORLD_PBR_SECTION__
