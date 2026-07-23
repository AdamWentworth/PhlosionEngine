#pragma once

#include <fastgltf/tools.hpp>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "engine/render/Model.h"

namespace pac::model_fastgltf {

struct MaterialRenderInfo {
    glm::vec3 emissiveFactor{0.0f};
    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    int alphaMode = 0;  // 0=OPAQUE, 1=MASK, 2=BLEND
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

MaterialRenderInfo resolveMaterialRenderInfo(const fastgltf::Asset& asset,
                                             int materialIndex,
                                             const Model::CPUTexture& baseCPU,
                                             bool dbgThisModel);

GLuint uploadTexture2D(const Model::CPUTexture& cpuTexture,
                       bool dbgThisModel,
                       const char* debugLabel);

}  // namespace pac::model_fastgltf
