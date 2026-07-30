#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in float aAge01;
layout(location = 2) in float aSizePx;
layout(location = 3) in float aSeed;

uniform mat4  u_ViewProj;
uniform float u_PointScale;

out float vAge01;
out float vSeed;

void main() {
    vec4 clip = u_ViewProj * vec4(aPos, 1.0);
    gl_Position = clip;

    float invW = 1.0 / max(0.0001, clip.w);

    // Convert "aSizePx" (world-ish scalar) into screen pixels.
    float px = aSizePx * u_PointScale * invW;

    // Fire needs larger sprites to look volumetric.
    gl_PointSize = clamp(px, 3.0, 160.0);

    vAge01 = clamp(aAge01, 0.0, 1.0);
    vSeed  = aSeed;
}
