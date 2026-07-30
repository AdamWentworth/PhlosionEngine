// assets/shaders/model/model.frag
#version 330 core

in vec2 TexCoord;
in vec4 VertColor;
out vec4 FragColor;

uniform sampler2D u_BaseColorTex;
uniform sampler2D u_EmissiveTex;
uniform vec3  u_EmissiveFactor;
uniform vec3  u_TintColor;
uniform float u_TintStrength;

// 0=OPAQUE, 1=MASK, 2=BLEND (glTF)
uniform int   u_AlphaMode;
uniform float u_AlphaCutoff;

// ----- TUNING -----
// Lower = less washed out body
const float kSceneExposure = 0.85;

// Accurate-ish sRGB <-> linear
vec3 srgbToLinear(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// ACES Filmic tonemap (common realtime fit)
vec3 tonemapACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec4 baseSrgb = texture(u_BaseColorTex, TexCoord);
    vec3 emiSrgb  = texture(u_EmissiveTex, TexCoord).rgb;

    // glTF baseColor/emissive textures are authored in sRGB
    vec3 baseLin = srgbToLinear(baseSrgb.rgb) * VertColor.rgb;
    vec3 emiLin  = srgbToLinear(emiSrgb) * u_EmissiveFactor;

    // Combine in linear HDR-ish space
    vec3 colorLin = (baseLin + emiLin) * kSceneExposure;

    // Tonemap -> display range
    vec3 mapped = tonemapACES(colorLin);

    // Output in sRGB (since you're not using GL_FRAMEBUFFER_SRGB)
    vec3 outSrgb = linearToSrgb(mapped);
    outSrgb = mix(outSrgb, u_TintColor, clamp(u_TintStrength, 0.0, 1.0));

    float outA = baseSrgb.a * VertColor.a;

    if (u_AlphaMode == 0) {
        outA = 1.0;
    } else if (u_AlphaMode == 1) {
        if (outA < u_AlphaCutoff) discard;
        outA = 1.0;
    }

    FragColor = vec4(outSrgb, outA);
}
