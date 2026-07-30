#pragma once

#include <glm/glm.hpp>

class Camera3D;
class TextRenderer;

namespace engine::tools::vfx_preview {

struct PreviewSceneState {
    glm::vec3 emitter{0.0f, 0.42f, 0.0f};
    glm::vec3 target{0.0f, 0.35f, 4.2f};
    glm::vec3 impactPoint{0.0f, 0.35f, 4.2f};
    bool useCustomImpactPoint = false;
    bool showEmitterMarker = true;
    bool showTargetMarker = true;
    bool showOrientationGuide = true;
    bool paused = false;
    bool loopPlayback = true;
    float timeScale = 1.0f;
};

struct PreviewEffectFocusFrame {
    bool enabled = false;
    glm::vec3 center{0.0f};
    float radius = 1.0f;
    float yawDeg = 20.0f;
    float pitchDeg = 18.0f;
    float distanceMul = 1.15f;
};

struct PreviewFrameContext {
    const Camera3D& camera;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    TextRenderer* overlayText = nullptr;
};

} // namespace engine::tools::vfx_preview
