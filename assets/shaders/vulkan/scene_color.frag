#version 450
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;

layout(location = 0) in vec2 textureUv;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform ScenePost { float bloomStrength; } post;
#define SCENE_SIZE vec2(textureSize(sceneColorTexture,0))
#define SCENE_SAMPLE(uv) textureLod(sceneColorTexture,uv,0.0)
#include "scene_bloom.glsl"

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
    sceneColor.rgb=sceneBloom(textureUv,sceneColor.rgb,post.bloomStrength);
    // Material alpha was already composited over the view's background.
    // The editor must display the completed image at full opacity.
    outColor = vec4(encodeNativeSrgb(sceneColor.rgb), 1.0);
}
