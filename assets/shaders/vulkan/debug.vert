#version 450

layout(location = 0) in vec2 inPositionPx;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform DebugPushConstants {
    vec4 surfaceSize;
} pushData;

layout(location = 0) out vec4 vertexColor;

void main() {
    vec2 safeSize = max(pushData.surfaceSize.xy, vec2(1.0));
    vec2 ndc = (inPositionPx / safeSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vertexColor = inColor;
}
