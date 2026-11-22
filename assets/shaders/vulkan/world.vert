#version 450
#extension GL_GOOGLE_include_directive : require
#include "world_wind_vertex.glsl"
#include "world_foliage_geometry.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in vec4 inTangent;
layout(location = 7) in vec2 inSourceUv1;
layout(location = 8) in vec2 inSourceUv2;

layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 5) uniform sampler2D environmentTexture;

layout(set = 1, binding = 1) uniform WorldSpecializedMaterialState {
    vec4 timingFlagsAtlas;
    vec4 rect0;
    vec4 rect1;
    vec4 flipbook0;
    vec4 flipbook1;
} worldSpecializedMaterial;
layout(set = 1, binding = 2) uniform WorldTransformState {
    mat4 modelMatrix;
    vec4 vertexColorMultiplier;
    vec4 skinningParams;
    vec4 instanceParams;
} worldTransform;
layout(std430, set = 1, binding = 3) readonly buffer WorldSkinMatrices {
    mat4 matrices[];
} worldSkin;
layout(std430, set = 1, binding = 4) readonly buffer WorldInstanceWords {
    vec4 words[];
} worldInstances;

layout(push_constant) uniform WorldPushConstants {
    mat4 viewProjection;
    vec4 materialParams;
    vec4 shadingParams;
    vec4 pbrFactors;
    vec4 emissiveAndCamera;
} pushData;

layout(location = 0) out vec2 vertexUv;
layout(location = 1) out vec4 vertexColor;
layout(location = 2) out vec3 vertexNormal;
layout(location = 3) out vec4 vertexTangent;
layout(location = 4) out vec3 worldPosition;
layout(location = 5) out vec3 vertexGenerated;
layout(location = 6) out vec2 vertexSourceUv1;
layout(location = 7) out vec2 vertexSourceUv2;


struct VertexTransform {mat4 modelMatrix;vec4 vertexColorMultiplier;vec4 skinningParams;vec4 instanceParams;};
struct VertexPush {mat4 viewProjection;vec4 materialParams;vec4 shadingParams;vec4 pbrFactors;vec4 emissiveAndCamera;};
struct VertexMaterial {vec4 timingFlagsAtlas;vec4 rect0;vec4 rect1;vec4 flipbook0;vec4 flipbook1;};
#ifdef PHLOSION_MOTION_VERTEX
layout(location=8) out vec4 motionCurrentClip;
layout(location=9) out vec4 motionPreviousClip;
layout(location=10) flat out uint motionObjectId;
layout(location=11) flat out uint motionHistoryValid;
int motionPacket=0;
int motionPositionPacket=0;
int motionPaletteMatrixBase=0;
bool motionExternalPalette=false;
bool motionPrevious=false;
mat4 motionMatrix(int offset) {
    int k=motionPacket+offset;
    return mat4(worldInstances.words[k],worldInstances.words[k+1],worldInstances.words[k+2],worldInstances.words[k+3]);
}
#endif
mat4 vertexSkinMatrix(int index) {
#ifdef PHLOSION_MOTION_VERTEX
    if(motionPrevious)return motionExternalPalette?worldSkin.matrices[motionPaletteMatrixBase+index]:motionMatrix(16+index*4);
#endif
    return worldSkin.matrices[index];
}

mat4 loadSkinMatrix(
    int jointIndex,
    int matrixCount,
    int baseMatrixIndex,
    float skinningMode) {
    mat4 primary = vertexSkinMatrix(baseMatrixIndex + jointIndex);
    if (skinningMode > 0.5) {
        mat4 secondary = vertexSkinMatrix(baseMatrixIndex + matrixCount + jointIndex);
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

void evaluateVertex(bool previous) {
    VertexTransform evaluatedTransform=VertexTransform(worldTransform.modelMatrix,worldTransform.vertexColorMultiplier,
        worldTransform.skinningParams,worldTransform.instanceParams);
    VertexPush evaluatedPush=VertexPush(pushData.viewProjection,pushData.materialParams,pushData.shadingParams,
        pushData.pbrFactors,pushData.emissiveAndCamera);
    VertexMaterial evaluatedMaterial=VertexMaterial(worldSpecializedMaterial.timingFlagsAtlas,
        worldSpecializedMaterial.rect0,worldSpecializedMaterial.rect1,worldSpecializedMaterial.flipbook0,worldSpecializedMaterial.flipbook1);
#ifdef PHLOSION_MOTION_VERTEX
    motionPrevious=previous;
    if(previous) {
        evaluatedTransform.modelMatrix=motionMatrix(0);
        evaluatedPush.viewProjection=motionMatrix(8);
        evaluatedMaterial.timingFlagsAtlas=worldInstances.words[motionPacket+12];
        evaluatedMaterial.rect0=worldInstances.words[motionPacket+13];
        evaluatedMaterial.rect1=worldInstances.words[motionPacket+14];
    }
#endif
#define worldTransform evaluatedTransform
#define pushData evaluatedPush
#define worldSpecializedMaterial evaluatedMaterial

    mat4 instanceModel = mat4(1.0);
    vec4 instanceColor = vec4(1.0);
    vec4 skinningParams = worldTransform.skinningParams;
    if (worldTransform.instanceParams.x > 0.5) {
        int firstWord = int(worldTransform.instanceParams.y + 0.5) +
                        int(gl_InstanceIndex) * 6;
        instanceModel = mat4(
            worldInstances.words[firstWord + 0],
            worldInstances.words[firstWord + 1],
            worldInstances.words[firstWord + 2],
            worldInstances.words[firstWord + 3]);
        instanceColor = worldInstances.words[firstWord + 4];
        vec4 instanceSkinningParams = worldInstances.words[firstWord + 5];
        if (instanceSkinningParams.x > 0.5) {
            skinningParams = instanceSkinningParams;
        }
    }

#ifdef PHLOSION_MOTION_VERTEX
    if(previous) {
        instanceModel=motionMatrix(4);
        vec4 meta=worldInstances.words[motionPacket+15];
        skinningParams=vec4(meta.x>0?1:0,meta.y,meta.x,0);
    }
#endif
    vec3 localPosition = inPosition;
#ifdef PHLOSION_MOTION_VERTEX
    if(previous && motionPositionPacket>0) {
        if(gl_VertexIndex<int(worldInstances.words[motionPositionPacket].x))
            localPosition=worldInstances.words[motionPositionPacket+1+gl_VertexIndex].xyz;
        else motionHistoryValid&=~1u;
    }
#endif
    vec3 localNormal = inNormal;
    vec3 localTangent = inTangent.xyz;
    float outlineExtrude = max(pushData.shadingParams.z, 0.0);
    float normalLengthSquared = dot(localNormal, localNormal);
    if (outlineExtrude > 0.0 && normalLengthSquared > 1e-10) {
        localPosition += localNormal * inversesqrt(normalLengthSquared) * outlineExtrude;
    }
#ifdef PHLOSION_PROJECT_MATERIAL
#include "project_world_vertex_evaluation.glsl"
#endif

    if (skinningParams.x > 0.5) {
        localPosition = applySkinning(localPosition, 1.0, skinningParams);
        localNormal = applySkinning(localNormal, 0.0, skinningParams);
        localTangent = applySkinning(localTangent, 0.0, skinningParams);
    }

    vec4 transformedPosition =
        worldTransform.modelMatrix * instanceModel * vec4(localPosition, 1.0);
    vec4 clip = pushData.viewProjection * transformedPosition;
    clip.y = -clip.y;
    clip.z = (clip.z + clip.w) * 0.5;
    clip.z -= max(pushData.materialParams.z, 0.0) * clip.w;
    gl_Position = clip;

    vertexUv = inUv;
    vertexSourceUv1 = inSourceUv1;
    vertexSourceUv2 = inSourceUv2;
    vertexColor = inColor * worldTransform.vertexColorMultiplier * instanceColor;
    mat3 normalMatrix = mat3(worldTransform.modelMatrix) * mat3(instanceModel);
    vertexNormal = normalize(normalMatrix * localNormal);
    vec3 transformedTangent = normalMatrix * localTangent;
    float tangentLengthSquared = dot(transformedTangent, transformedTangent);
    if (tangentLengthSquared > 1e-10) {
        transformedTangent *= inversesqrt(tangentLengthSquared);
    }
    vertexTangent = vec4(transformedTangent, inTangent.w);
    worldPosition = transformedPosition.xyz;
    vec3 generatedSpan = max(
        worldSpecializedMaterial.rect1.xyz - worldSpecializedMaterial.rect0.xyz,
        vec3(1e-5));
    vertexGenerated = clamp(
        (inPosition - worldSpecializedMaterial.rect0.xyz) / generatedSpan,
        vec3(0.0),
        vec3(1.0));
}

#undef worldTransform
#undef pushData
#undef worldSpecializedMaterial
void main() {
#ifdef PHLOSION_MOTION_VERTEX
    motionObjectId=0u;motionHistoryValid=0u;
    vec4 oldClip=vec4(0);
    bool enrolled=false;
    if(worldTransform.instanceParams.w>.5) {
        int base=int(worldTransform.instanceParams.z);
        enrolled=gl_InstanceIndex<int(worldInstances.words[base].x);
        if(enrolled) {
            vec4 packetHeader=worldInstances.words[base+1+gl_InstanceIndex];
            motionPacket=base+int(packetHeader.x);
            motionPositionPacket=packetHeader.w>0.?base+int(packetHeader.w):0;
            motionPaletteMatrixBase=int(packetHeader.y)/4;motionExternalPalette=packetHeader.z>.5;
            vec4 meta=worldInstances.words[motionPacket+15];
            uint high=uint(meta.w);
            motionHistoryValid=high>>16;
            motionObjectId=uint(meta.z)|((high&65535u)<<16);
            evaluateVertex(true);oldClip=gl_Position;
            if(worldFoliageDepthProxy)oldClip.z=worldFoliageOriginalClipZ;
        }
    }
    evaluateVertex(false);
    vec4 currentClip=gl_Position;
    if(worldFoliageDepthProxy)currentClip.z=worldFoliageOriginalClipZ;
    motionCurrentClip=vec4(currentClip.x,-currentClip.y,2*currentClip.z-currentClip.w,currentClip.w);
    motionPreviousClip=enrolled?vec4(oldClip.x,-oldClip.y,2*oldClip.z-oldClip.w,oldClip.w):motionCurrentClip;
#else
    evaluateVertex(false);
#endif
}
