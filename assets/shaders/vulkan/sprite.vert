#version 450

layout(location = 0) in vec4 inRectPx;
layout(location = 1) in vec4 inUvRect;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform SpritePushConstants {
    vec4 surfaceSize;
} pushData;

layout(location = 0) out vec2 vertexUv;
layout(location = 1) out vec4 vertexColor;

void main() {
    const vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0));
    vec2 corner = corners[gl_VertexIndex];
    vec2 positionPx = inRectPx.xy + corner * inRectPx.zw;
    vec2 safeSize = max(pushData.surfaceSize.xy, vec2(1.0));
    vec2 ndc = (positionPx / safeSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vertexUv = mix(inUvRect.xy, inUvRect.zw, corner);
    vertexColor = inColor;
}
