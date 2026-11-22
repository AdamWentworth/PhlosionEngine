#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_control_flow_attributes : require
layout(set=1,binding=0) uniform ReflectionView {
    layout(offset=12688) vec4 screen[11];
} viewState;
layout(set=3,binding=0) uniform sampler2D sourceDepth;
layout(set=4,binding=0) uniform sampler2D sourceColor;
layout(set=6,binding=0) uniform sampler2D surfaceNormal;
layout(set=6,binding=1) uniform sampler2D surfaceDepth;
layout(set=6,binding=2) uniform sampler2D surfaceMaterial;
layout(location=0) out vec4 outColor;
#define WORLD_SCENE_RADIANCE true
#define SCREEN_REFLECTION(index) viewState.screen[index]
#define SCREEN_REFLECTION_LOOP [[dont_unroll]]
#define SCREEN_REFLECTION_DEPTH(p) texelFetch(sourceDepth,ivec2(p),0).r
#define SCREEN_REFLECTION_SAMPLE(uv) textureLod(sourceColor,uv,0.0)
#define SCREEN_SURFACE_NORMAL(p) texelFetch(surfaceNormal,ivec2(p),0)
#define SCREEN_SURFACE_DEPTH(p) texelFetch(surfaceDepth,ivec2(p),0).r
#define SCREEN_SURFACE_MATERIAL(p) texelFetch(surfaceMaterial,ivec2(p),0)
#include "world_screen_reflection.glsl"
void main() {outColor=resolveWorldSurfaceReflection(gl_FragCoord.xy);}
