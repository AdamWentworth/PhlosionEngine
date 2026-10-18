// The adapters use integer addressing, avoiding normalized-coordinate roundoff.
float shadowMomentDepth(vec2 pixel) { return MOMENT_LOAD(pixel + momentOffsets.xy).x; }
float shadowMomentCachedDepth(vec2 pixel) { return 1.0; }
vec2 shadowMomentIntermediate(vec2 pixel) { return MOMENT_LOAD(pixel).xy; }
#include "world_shadow_moments.glsl"
vec2 filterShadowMoment(vec2 fragment) {
    vec2 pixel = floor(fragment) - momentOffsets.zw;
    float resolution = momentPass.y;
    float scale = 2.0 * momentProjection.z / resolution;
    float origin = (1.0 / resolution - 1.0) * momentProjection.z;
    if (momentPass.x < 0.5)
        return shadowMomentHorizontal(pixel, resolution - 1.0, int(momentPass.z),
            momentProjection.xy, vec4(scale, scale, origin, origin));
    return shadowMomentVertical(pixel, resolution - 1.0, int(momentPass.z));
}
