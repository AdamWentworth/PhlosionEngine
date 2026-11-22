#pragma once

#include "WorldDisplay.h"
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
    virtual void setWorldDisplaySettings(const WorldDisplaySettings& settings) {
        validateWorldDisplay(settings);worldDisplaySettings_=settings;
    }
    virtual void setWorldHeatHazeNoise(const WorldHeatHazeNoise& noise) {
        noise.validate();
        if(worldHeatHazeNoise_.key!=noise.key)worldHeatHazeNoise_=noise;
    }
    const WorldDisplaySettings& worldDisplaySettings() const noexcept { return worldDisplaySettings_; }
protected:
    WorldDisplaySettings worldDisplaySettings_{};
    WorldHeatHazeNoise worldHeatHazeNoise_;

    float worldSceneRadianceExposure_ = 0;
};
}
