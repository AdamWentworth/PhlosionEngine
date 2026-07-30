// assets/shaders/lib/noise2d.glsl
// (NO #version here)

float hash11(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

float hash21(vec2 p) {
    float n = dot(p, vec2(127.1, 311.7));
    return fract(sin(n) * 43758.5453);
}

float valueNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float smoothFlicker(float t, float seed) {
    float x = t * 9.0 + seed * 97.0;
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash11(i), hash11(i + 1.0), f);
}
