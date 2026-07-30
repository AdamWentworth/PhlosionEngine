// default.vert

#version 330 core

layout(location = 0) in vec3 aPos;

uniform mat4 u_MVP;

out vec4 fakeOut; // prevent optimization

void main() {
    vec4 pos = u_MVP * vec4(aPos, 1.0);
    fakeOut = pos;
    gl_Position = pos;
}
