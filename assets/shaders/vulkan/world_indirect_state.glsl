#ifndef PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS
#define PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS 256
#endif

struct WorldIndirectDrawState {
    vec4 specializedTimingFlagsAtlas;
    vec4 specializedRect0;
    vec4 specializedRect1;
    vec4 specializedFlipbook0;
    vec4 specializedFlipbook1;
    mat4 specializedProjectedShadowMatrix;
    vec4 specializedProjectedShadowParams;
    vec4 specializedLightProjectionUvRowU;
    vec4 specializedLightProjectionUvRowV;
    vec4 materialParams;
    vec4 shadingParams;
    vec4 pbrFactors;
    vec4 emissiveAndCamera;
    uvec4 drawParams;
};

layout(std430, set = 1, binding = 5) readonly buffer WorldIndirectDrawStates {
    WorldIndirectDrawState states[];
} worldIndirectDraws;
