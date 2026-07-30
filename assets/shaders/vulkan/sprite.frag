#version 450

layout(set = 0, binding = 0) uniform sampler2D spriteTexture;

layout(location = 0) in vec2 vertexUv;
layout(location = 1) in vec4 vertexColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(spriteTexture, vertexUv) * vertexColor;
}
