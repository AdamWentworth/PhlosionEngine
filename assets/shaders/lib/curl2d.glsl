// assets/shaders/lib/curl2d.glsl
#include "assets/shaders/lib/fbm2d.glsl"
// (NO #version here)

// cheap curl field (buoyant swirl)
vec2 fbmGrad(vec2 p) {
    float e = 0.03;
    float nx = fbm2D(p + vec2(e, 0.0)) - fbm2D(p - vec2(e, 0.0));
    float ny = fbm2D(p + vec2(0.0, e)) - fbm2D(p - vec2(0.0, e));
    return vec2(nx, ny) / (2.0 * e);
}

vec2 curl2D(vec2 p) {
    vec2 g = fbmGrad(p);
    return vec2(g.y, -g.x);
}

// domain advection: makes detail "curl" instead of just scrolling upward
vec2 advect(vec2 p, float flowY, float amount) {
    vec2 c1 = curl2D(p * 1.30 + vec2(0.0, -flowY * 0.10));
    vec2 c2 = curl2D(p * 2.70 + vec2(3.1, -flowY * 0.18));
    return p + (c1 * 0.65 + c2 * 0.35) * amount;
}
