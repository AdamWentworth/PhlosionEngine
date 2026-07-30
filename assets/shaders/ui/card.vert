// card.vert

#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 v_TexCoord;

uniform mat4 u_Projection;
uniform mat4 u_Model;
uniform vec2 u_UVMin;
uniform vec2 u_UVMax;

void main() {
    v_TexCoord = mix(u_UVMin, u_UVMax, aTexCoord);
    gl_Position = u_Projection * u_Model * vec4(aPos, 0.0, 1.0);
}
