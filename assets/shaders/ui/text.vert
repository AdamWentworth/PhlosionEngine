// text.vert

#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 u_Projection;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = u_Projection * vec4(aPos.xy, 0.0, 1.0);
}

