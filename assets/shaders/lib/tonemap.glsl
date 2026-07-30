// assets/shaders/lib/tonemap.glsl
// (NO #version here)

vec3 tonemapSoft(vec3 c) {
    return c / (vec3(1.0) + c);
}
