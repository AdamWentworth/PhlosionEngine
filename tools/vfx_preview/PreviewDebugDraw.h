#pragma once

#include <glm/glm.hpp>

namespace engine::tools::vfx_preview {

class IPreviewDebugDraw {
public:
    virtual ~IPreviewDebugDraw() = default;

    virtual void addLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) = 0;
    virtual void addCross(const glm::vec3& center, float radius, const glm::vec3& color) = 0;
    virtual void addCircleXZ(const glm::vec3& center,
                             float radius,
                             const glm::vec3& color,
                             int segments = 32) = 0;
    virtual void addArrow(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color) = 0;
};

} // namespace engine::tools::vfx_preview
