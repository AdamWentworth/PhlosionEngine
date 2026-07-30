// assets/shaders/model/model.vert
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTex;
layout(location = 2) in uvec4 aJoints;
layout(location = 3) in vec4  aWeights;
layout(location = 4) in vec4  aColor;

uniform mat4 u_MVP;

// skinning control
uniform int u_UseSkin;

#ifndef MAX_JOINTS
#define MAX_JOINTS 128
#endif

uniform mat4 u_Joints[MAX_JOINTS];

out vec2 TexCoord;
out vec4 VertColor;

void main()
{
    TexCoord = aTex;
    VertColor = aColor;

    vec4 localPos = vec4(aPos, 1.0);

    if (u_UseSkin == 1) {
        mat4 skinMat =
              aWeights.x * u_Joints[int(aJoints.x)]
            + aWeights.y * u_Joints[int(aJoints.y)]
            + aWeights.z * u_Joints[int(aJoints.z)]
            + aWeights.w * u_Joints[int(aJoints.w)];

        localPos = skinMat * localPos;
    }

    gl_Position = u_MVP * localPos;
}
