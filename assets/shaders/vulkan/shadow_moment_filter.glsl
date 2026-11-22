// The adapters use integer addressing, avoiding normalized-coordinate roundoff.
float shadowMomentDepth(vec2 pixel) { return MOMENT_LOAD(pixel + momentOffsets.xy).x; }
#ifndef MOMENT_CACHE_LOAD
#define MOMENT_CACHE_LOAD(p) 1.0
#endif
float shadowMomentCachedDepth(vec2 pixel) {
    return mod(momentPass.w,2.0) > 0.5 ? MOMENT_CACHE_LOAD(pixel + momentOffsets.xy - vec2(2048.0,0.0)) : 1.0;
}
vec2 shadowMomentIntermediate(vec2 pixel) { return MOMENT_LOAD(pixel).xy; }
#include "world_shadow_moments.glsl"
vec2 filterShadowMoment(vec2 fragment) {
    vec2 pixel = floor(fragment) - momentOffsets.zw;
    float resolution = momentPass.y;
    float scale = 2.0 * momentProjection.z / resolution;
    float origin = (1.0 / resolution - 1.0) * momentProjection.z;
    if (momentPass.x > 1.5) {
        float radial=shadowMomentDistance(shadowMomentDepth(pixel),pixel,
            momentProjection.xy,vec4(scale,scale,origin,origin));
        return vec2(radial,radial*radial);
    }
    if (momentPass.x < 0.5)
        return shadowMomentHorizontal(pixel, resolution - 1.0, int(momentPass.z),
            momentProjection.xy, vec4(scale, scale, origin, origin),momentPass.w>=2.0);
    return shadowMomentVertical(pixel, resolution - 1.0, int(momentPass.z));
}
