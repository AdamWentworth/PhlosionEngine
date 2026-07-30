// assets/shaders/lib/fbm2d.glsl
#include "assets/shaders/lib/noise2d.glsl"
// (NO #version here)

float fbm2D(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 5; ++k) {
        v += a * valueNoise2D(p);
        p *= 2.02;
        a *= 0.5;
    }
    return v;
}

float ridged01(float n01) {
    float r = 1.0 - abs(n01 * 2.0 - 1.0);
    return r * r;
}

float ridgedFbm2D(vec2 p) {
    float v = 0.0;
    float a = 0.55;
    for (int k = 0; k < 4; ++k) {
        v += a * ridged01(valueNoise2D(p));
        p *= 2.10;
        a *= 0.5;
    }
    return v;
}
