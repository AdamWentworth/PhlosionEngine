#extension GL_EXT_nonuniform_qualifier : require
layout(std430,set=7,binding=1) readonly buffer WorldLayeredWords {uint worldLayeredWords[];};
layout(set=7,binding=2) uniform sampler2D worldLayeredTextures[512];
#define LAYER_CAMERA worldView.cameraPosition.xyz
#define LAYER_STORAGE
#define LAYER_WORD(i) worldLayeredWords[i]
#define LAYER_SAMPLE(i,uv,dx,dy) textureGrad(worldLayeredTextures[nonuniformEXT(i)],uv,dx,dy)
#define LAYER_LEVEL(i,uv) textureLod(worldLayeredTextures[nonuniformEXT(i)],uv,0.0)
#define LAYER_POPCOUNT(i) bitCount(i)
#include "world_layered_sampling.glsl"
