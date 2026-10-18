#version 450
#extension GL_GOOGLE_include_directive : require
layout(set=0,binding=0) uniform sampler2D momentInput;
layout(push_constant) uniform MomentPush { vec4 momentProjection; vec4 momentOffsets; vec4 momentPass; };
layout(location=0) out vec2 result;
#define MOMENT_LOAD(p) texelFetch(momentInput,ivec2(p),0)
#include "shadow_moment_filter.glsl"
void main() { result=filterShadowMoment(gl_FragCoord.xy); }
