#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 textureUv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D sceneImage;
layout(set=0,binding=1) uniform sampler2D historyImage;
layout(set=0,binding=2) uniform sampler2D maskImage;
layout(set=0,binding=3,std140) uniform Feedback { vec4 control; vec4 passes[12]; vec4 scenePost; };
vec4 fetchHistory(ivec2 p) { return texelFetch(historyImage,p,0); }
vec4 sampleScene(vec2 uv) { ivec2 size=textureSize(sceneImage,0);return texelFetch(sceneImage,clamp(ivec2(floor(uv*vec2(size))),ivec2(0),size-1),0); }
float sampleMask(vec2 uv) { return texelFetch(maskImage,clamp(ivec2(floor(uv*vec2(textureSize(maskImage,0)))),ivec2(0),textureSize(maskImage,0)-1),0).a; }
#define SCENE_SIZE vec2(textureSize(sceneImage,0))
#define SCENE_SAMPLE(uv) sampleScene(uv)
#define SCENE_EXPOSURE scenePost.y
#include "scene_bloom.glsl"
#include "screen_feedback_common.glsl"
void main() { outColor=feedbackOutput(textureUv); }
