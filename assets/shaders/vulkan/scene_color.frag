#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;

layout(location = 0) in vec2 textureUv;
layout(location = 0) out vec4 outColor;

vec3 encodeNativeSrgb(vec3 linearColor) {
    vec3 low = linearColor * 12.9200001;
    vec3 high =
        1.05499995 *
            pow(abs(linearColor), vec3(0.416666657)) -
        vec3(0.0549999997);
    return mix(
        high,
        low,
        lessThanEqual(
            linearColor,
            vec3(0.00313080009)));
}

void main() {
    vec4 sceneColor = texture(sceneColorTexture, textureUv);
    outColor = vec4(encodeNativeSrgb(sceneColor.rgb), sceneColor.a);
}
