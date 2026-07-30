#version 450
#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in vec4 inTangent;
layout(location = 7) in vec2 inSourceUv1;
layout(location = 8) in vec2 inSourceUv2;

layout(std430, set = 1, binding = 3) readonly buffer WorldSkinMatrices {
    mat4 matrices[];
} worldSkin;
layout(std430, set = 1, binding = 4) readonly buffer WorldInstanceWords {
    vec4 words[];
} worldInstances;

#include "world_indirect_state.glsl"

layout(push_constant) uniform WorldIndirectPushConstants {
    mat4 viewProjection;
    uvec4 batchParams;
} pushData;

layout(location = 0) out vec2 vertexUv;
layout(location = 1) out vec4 vertexColor;
layout(location = 2) out vec3 vertexNormal;
layout(location = 3) out vec4 vertexTangent;
layout(location = 4) out vec3 worldPosition;
layout(location = 5) out vec3 vertexGenerated;
layout(location = 6) out vec2 vertexSourceUv1;
layout(location = 7) out vec2 vertexSourceUv2;
layout(location = 8) flat out uint drawStateIndex;

mat4 loadSkinMatrix(
    int jointIndex,
    int matrixCount,
    int baseMatrixIndex,
    float skinningMode) {
    mat4 primary = worldSkin.matrices[baseMatrixIndex + jointIndex];
    if (skinningMode > 0.5) {
        mat4 secondary = worldSkin.matrices[
            baseMatrixIndex + matrixCount + jointIndex];
        return primary * secondary;
    }
    return primary;
}

vec3 applySkinning(vec3 localValue, float homogeneousW, vec4 skinningParams) {
    int matrixCount = int(skinningParams.z + 0.5);
    int baseMatrixIndex = int(skinningParams.w + 0.5);
    vec4 blended = vec4(0.0);
    float totalWeight = 0.0;
    for (int influence = 0; influence < 4; ++influence) {
        float weight = inWeights[influence];
        int jointIndex = int(inJoints[influence] + 0.5);
        if (weight <= 0.00001 || jointIndex < 0 || jointIndex >= matrixCount) continue;
        blended += loadSkinMatrix(
                       jointIndex,
                       matrixCount,
                       baseMatrixIndex,
                       skinningParams.y) *
                   vec4(localValue, homogeneousW) * weight;
        totalWeight += weight;
    }
    if (totalWeight <= 0.00001) return localValue;
    if (totalWeight < 0.999) {
        blended += vec4(localValue, homogeneousW) * (1.0 - totalWeight);
    }
    return blended.xyz;
}

void main() {
    uint stateIndex = pushData.batchParams.x + uint(gl_DrawIDARB);
    WorldIndirectDrawState drawState = worldIndirectDraws.states[stateIndex];
    int firstWord = int(drawState.drawParams.y) + int(gl_InstanceIndex) * 6;
    mat4 instanceModel = mat4(
        worldInstances.words[firstWord + 0],
        worldInstances.words[firstWord + 1],
        worldInstances.words[firstWord + 2],
        worldInstances.words[firstWord + 3]);
    vec4 instanceColor = worldInstances.words[firstWord + 4];
    vec4 skinningParams = worldInstances.words[firstWord + 5];

    vec3 localPosition = inPosition;
    vec3 localNormal = inNormal;
    vec3 localTangent = inTangent.xyz;
    float outlineExtrude = max(drawState.shadingParams.z, 0.0);
    float normalLengthSquared = dot(localNormal, localNormal);
    if (outlineExtrude > 0.0 && normalLengthSquared > 1e-10) {
        localPosition += localNormal * inversesqrt(normalLengthSquared) * outlineExtrude;
    }
    if (skinningParams.x > 0.5) {
        localPosition = applySkinning(localPosition, 1.0, skinningParams);
        localNormal = applySkinning(localNormal, 0.0, skinningParams);
        localTangent = applySkinning(localTangent, 0.0, skinningParams);
    }

    vec4 transformedPosition = instanceModel * vec4(localPosition, 1.0);
    vec4 clip = pushData.viewProjection * transformedPosition;
    clip.y = -clip.y;
    clip.z = (clip.z + clip.w) * 0.5;
    clip.z -= max(drawState.materialParams.z, 0.0) * clip.w;
    gl_Position = clip;

    vertexUv = inUv;
    vertexSourceUv1 = inSourceUv1;
    vertexSourceUv2 = inSourceUv2;
    vertexColor = inColor * instanceColor;
    mat3 normalMatrix = mat3(instanceModel);
    vertexNormal = normalize(normalMatrix * localNormal);
    vec3 transformedTangent = normalMatrix * localTangent;
    float tangentLengthSquared = dot(transformedTangent, transformedTangent);
    if (tangentLengthSquared > 1e-10) {
        transformedTangent *= inversesqrt(tangentLengthSquared);
    }
    vertexTangent = vec4(transformedTangent, inTangent.w);
    worldPosition = transformedPosition.xyz;
    vec3 generatedSpan = max(
        drawState.specializedRect1.xyz - drawState.specializedRect0.xyz,
        vec3(1e-5));
    vertexGenerated = clamp(
        (inPosition - drawState.specializedRect0.xyz) / generatedSpan,
        vec3(0.0),
        vec3(1.0));
    drawStateIndex = stateIndex;
}
