#pragma once

#include "engine/render/RenderBackendTypes.h"

#include <string>

class IRenderBackendFrame {
public:
    using BackendFrameTimings = engine::render::backend::BackendFrameTimings;
    using BackendFrameStats = engine::render::backend::BackendFrameStats;

    virtual ~IRenderBackendFrame() = default;

    virtual const char* backendId() const = 0;
    virtual void beginFrame(float r, float g, float b, float a) = 0;
    virtual void endFrame() = 0;
    virtual void onResize(int width, int height) = 0;
    virtual bool requiresOpenGLContext() const = 0;
    virtual bool handlesPresentation() const = 0;
    virtual bool getLastFrameTimings(BackendFrameTimings& outTimings) const {
        outTimings = BackendFrameTimings{};
        return false;
    }
    virtual bool getLastFrameStats(BackendFrameStats& outStats) const {
        outStats = BackendFrameStats{};
        return false;
    }
    virtual std::string activeGpuName() const { return {}; }
    virtual bool activeGpuIsDiscrete() const { return false; }
    virtual void setVSyncEnabled(bool enabled) {
        (void)enabled;
    }
    virtual void shutdown() = 0;
};
