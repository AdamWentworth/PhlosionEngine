#pragma once

#include <cmath>
#include <stdexcept>

namespace engine::render {
// Optional scene-linear composition. Zero retains legacy per-material mapping;
// positive exposure defers mapping until after the whole view and bloom.
// Every world color pass resets this state before its client submits content.
class IWorldSceneRadiance {
public:
    virtual ~IWorldSceneRadiance() = default;
    float worldSceneRadianceExposure() const noexcept { return worldSceneRadianceExposure_; }
    virtual void setWorldSceneRadianceExposure(float exposure) {
        if (!std::isfinite(exposure) || exposure < 0 || exposure > 128)
            throw std::invalid_argument("Invalid scene radiance exposure.");
        worldSceneRadianceExposure_ = exposure;
    }
protected:
    float worldSceneRadianceExposure_ = 0;
};
}
