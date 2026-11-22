#pragma once
#include "WorldLayeredMaterial.h"
#include <glm/glm.hpp>
namespace engine::render {
struct WorldLayerSurface {
    glm::vec3 color{},normal{0,1,0};
    float roughness=1,metalness=0,specular=.04f;
    std::uint32_t dominantLayer=0;
    float dominantWeight=0,selectedWeight=0;
};
// Prepared static transport/ground attributes use the same immutable layer
// inputs as native rendering. Call only on a validated set; index is one-based.
// This evaluates the dry material, without view-dependent normal fading.
WorldLayerSurface sampleWorldLayerMaterial(const WorldLayerMaterialSet&,std::uint32_t index,
    glm::vec2 uv,glm::vec3 position,glm::vec3 normal,glm::vec4 tangent,float mip=0);
}
