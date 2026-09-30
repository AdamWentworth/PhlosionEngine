// Numeric unlit material contract shared by direct and indirect world draws.
vec4 portableMaterial(float mode, vec4 tex, vec4 high, vec3 lowRgb) {
    if (mode > 254.5) return clamp(high * tex, 0.0, 1.0);
    vec4 low = vec4(lowRgb, 0.0);
    bool separate = mode == 251.0 || mode == 253.0;
    float intensity = separate ? tex.r : tex.a;
    vec4 factor = vec4(vec3(intensity), tex.a);
    vec4 color = clamp(mix(low, high, factor), 0.0, 1.0);
    if (mode >= 251.5) {
        factor = floor(factor * 255.0 + 0.5);
        low = floor(low * 255.0 + 0.5);
        high = floor(high * 255.0 + 0.5);
        vec4 bytes = clamp(floor((low * 256.0 + (high - low) *
            (factor + floor(factor / 128.0)) + 128.0) / 256.0), 0.0, 255.0);
        color = bytes / 255.0;
        if (bytes.a == 1.0) color.a = 0.0;
    }
    return color;
}
