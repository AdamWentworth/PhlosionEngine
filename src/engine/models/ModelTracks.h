#pragma once
#include "engine/models/ModelData.h"
namespace engine::models {
float sampleMaterialCurve(const MaterialAnimationCurve& curve, float timeSec,
    float fallback, bool periodicOffset, float sourceFrameRate, bool holdSourceFrame);
bool sampleContinuousMaterialAnimation(const MeshData& mesh, std::size_t submeshIndex,
    MaterialAnimationParameter parameter, float materialTimeSec, glm::vec4& outValue);
bool sampleClipBoundMaterialAnimation(const MeshData& mesh, int animationIndex,
    std::size_t submeshIndex, MaterialAnimationParameter parameter,
    float animationTimeSec, glm::vec4& outValue);
float sampleVisibilityTrack(const MeshVisibilityTrack& track, float timeSec);
}
