// assets/shaders/vfx/particle.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

// Flipbook
uniform sampler2D u_Flipbook;
uniform vec2  u_FlipbookGrid;   // (cols, rows)
uniform float u_FrameCount;
uniform float u_Fps;

float hash1(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

float smoothFlicker(float t, float seed) {
    float a0 = hash1(floor(t * 7.0 + seed * 91.0));
    float a1 = hash1(floor(t * 7.0 + seed * 91.0) + 1.0);
    float af = fract(t * 7.0 + seed * 91.0);
    float a = mix(a0, a1, af);

    float b0 = hash1(floor(t * 13.0 + seed * 37.0));
    float b1 = hash1(floor(t * 13.0 + seed * 37.0) + 1.0);
    float bf = fract(t * 13.0 + seed * 37.0);
    float b = mix(b0, b1, bf);

    return mix(a, b, 0.5);
}

vec3 adjustSaturation(vec3 c, float sat) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(l), c, sat);
}

// Orange/red ramp (no white-hot)
vec3 fireRampOrangeRed(float x) {
    vec3 hot  = vec3(1.05, 0.42, 0.18);
    vec3 mid  = vec3(0.90, 0.30, 0.14);
    vec3 cool = vec3(0.58, 0.16, 0.10);

    vec3 c = mix(hot, mid, smoothstep(0.0, 0.65, x));
    c = mix(c, cool, smoothstep(0.55, 1.0, x));

    c *= 0.70;               // brightness trim
    c = adjustSaturation(c, 0.95);
    return c;
}

// Simple highlight compression so additive overdraw doesn't go white
vec3 tonemapSoft(vec3 c) {
    // c / (1 + c) keeps color and prevents blowout
    return c / (vec3(1.0) + c);
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 local = gl_PointCoord;
    local.y = 1.0 - local.y;

    // Thick-ish edges without huge halos
    vec2 p = local - vec2(0.5);
    float r = length(p) * 2.0;
    float soft = smoothstep(1.20, 0.28, r);

    float t = u_Time;
    float fl = smoothFlicker(t, vSeed);

    // Small wobble only
    vec2 wobble = vec2(
        smoothFlicker(t * 0.9, vSeed + 0.17),
        smoothFlicker(t * 1.1, vSeed + 0.73)
    ) - 0.5;
    local += wobble * (0.010 * (1.0 - age));

    // Flipbook frame selection
    float speed = mix(0.85, 1.08, hash1(vSeed * 19.31));
    float f = floor(u_Time * u_Fps * speed + vSeed * u_FrameCount);
    float frame = mod(f, u_FrameCount);

    float cols = u_FlipbookGrid.x;
    float rows = u_FlipbookGrid.y;

    float col = mod(frame, cols);
    float rowFromTop = floor(frame / cols);
    float row = (rows - 1.0) - rowFromTop;

    vec2 cellUV = (vec2(col, row) + local) / vec2(cols, rows);
    vec4 tex = texture(u_Flipbook, cellUV);

    // Keep density through lifetime (but not infinite)
    float fade = (1.0 - age);
    fade = mix(fade, 1.0, 0.25);
    fade = pow(fade, 0.75);

    float intensity = soft * fade * mix(0.95, 1.06, fl);

    // Thickness knob (reduced so it won't blow out)
    float alpha = tex.a * intensity;
    alpha *= 1.25;

    if (alpha < 0.0015) discard;

    vec3 rgb = tex.rgb * fireRampOrangeRed(age);

    // prevent the atlas bright pixels from going white after overdraw
    rgb = tonemapSoft(rgb);

    FragColor = vec4(rgb * alpha, alpha);
}
