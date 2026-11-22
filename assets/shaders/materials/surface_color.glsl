vec3 rrtAndOdtFit(vec3 value) {
    vec3 a = value * (value + 0.0245786) - 0.000090537;
    vec3 b = value * (0.983729 * value + 0.4329510) + 0.238081;
    return a / b;
}

vec3 tonemapACESFilmic(vec3 color, float exposure) {
    if (WORLD_SCENE_RADIANCE) return color;
    const mat3 inputTransform = mat3(
        vec3(0.59719, 0.07600, 0.02840),
        vec3(0.35458, 0.90834, 0.13383),
        vec3(0.04823, 0.01566, 0.83777));
    const mat3 outputTransform = mat3(
        vec3( 1.60475, -0.10208, -0.00327),
        vec3(-0.53108,  1.10813, -0.07276),
        vec3(-0.07367, -0.00605,  1.07602));
    color *= exposure / 0.6;
    color = inputTransform * color;
    color = rrtAndOdtFit(color);
    return clamp(outputTransform * color, 0.0, 1.0);
}

vec3 linearToSrgb(vec3 color) {
    color = max(color, vec3(0.0));
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

vec3 encodeClampedLinearColor(vec3 linearColor) {
    // Clamp to the normalized output range before the standard sRGB transfer.
    return linearToSrgb(clamp(linearColor, 0.0, 1.0));
}

