#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/RendererParityContract.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#include <stb_image_write.h>
#include "engine/core/Environment.h"
#include "engine/render/Renderer.h"
#include "engine/render/DebugGeometry.h"

namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsCi(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

} // namespace

OpenGLRenderBackend::OpenGLRenderBackend()
    : renderer_(std::make_unique<Renderer>()) {
    glEnable(GL_DEPTH_TEST);
    bool fbSrgbEnabled = false;
    // Shader path already tone-maps + encodes to sRGB explicitly.
    // Keep fixed-function framebuffer sRGB conversion disabled to avoid double-encoding.
#ifdef GL_FRAMEBUFFER_SRGB
    if (engine::render::parity_contract::kFramebufferSrgbEnabled) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    } else {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    fbSrgbEnabled = (glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE);
    std::cout << "[OpenGL] GL_FRAMEBUFFER_SRGB=" << (fbSrgbEnabled ? "ON" : "OFF") << "\n";
#endif
    engine::render::parity_contract::RuntimeConfig parityCfg =
        engine::render::parity_contract::makeBaselineConfig();
    parityCfg.framebufferSrgbEnabled = fbSrgbEnabled;
    engine::render::parity_contract::logValidation("OpenGL", parityCfg);

    gpuTimingSupported_ = GLAD_GL_VERSION_3_3;
    if (gpuTimingSupported_) {
        glGenQueries(static_cast<GLsizei>(gpuTimerQueries_.size()), gpuTimerQueries_.data());
        if (gpuTimerQueries_[0] == 0u || gpuTimerQueries_[1] == 0u) {
            gpuTimingSupported_ = false;
            gpuTimerQueries_[0] = 0u;
            gpuTimerQueries_[1] = 0u;
        }
    }

    configureScreenshotCapture();
}

OpenGLRenderBackend::~OpenGLRenderBackend() {
    shutdown();
}

void OpenGLRenderBackend::beginFrame(float r, float g, float b, float a) {
    ++frameCounter_;
    frameDrawCalls_ = 0u;
    frameTriangles_ = 0u;
    frameIndexedOpaqueDraws_ = 0u;
    frameIndexedBlendDraws_ = 0u;
    frameIndexedCachedDraws_ = 0u;
    frameIndexedDynamicDraws_ = 0u;
    frameIndexedInstancedDraws_ = 0u;
    frameIndexedOutlineBatches_ = 0u;
    frameIndexedGeometrySwitches_ = 0u;
    frameIndexedMaterialSwitches_ = 0u;
    frameIndexedTextureSwitches_ = 0u;
    frameIndexedGlTextureBindCalls_ = 0u;
    frameClearColor_ = {r, g, b, a};
#ifdef GL_FRAMEBUFFER_SRGB
    if (engine::render::parity_contract::kFramebufferSrgbEnabled) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    } else {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
#endif
    if (gpuTimingSupported_) {
        const std::size_t idx = static_cast<std::size_t>(gpuTimerWriteIndex_ & 1u);
        if (gpuTimerQueries_[idx] != 0u) {
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries_[idx]);
            gpuTimerIssued_[idx] = true;
        }
    }
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderBackend::endFrame() {
    if (worldSceneColorPassActive_) {
        endWorldSceneColorPass();
    }
    if (gpuTimingSupported_) {
        const std::size_t writeIdx = static_cast<std::size_t>(gpuTimerWriteIndex_ & 1u);
        if (gpuTimerQueries_[writeIdx] != 0u && gpuTimerIssued_[writeIdx]) {
            glEndQuery(GL_TIME_ELAPSED);
        }

        const std::size_t readIdx = static_cast<std::size_t>((gpuTimerWriteIndex_ ^ 1u) & 1u);
        if (gpuTimerQueries_[readIdx] != 0u && gpuTimerIssued_[readIdx]) {
            GLuint available = 0u;
            glGetQueryObjectuiv(
                gpuTimerQueries_[readIdx],
                GL_QUERY_RESULT_AVAILABLE,
                &available);
            if (available != 0u) {
                GLuint64 elapsedNs = 0u;
                glGetQueryObjectui64v(
                    gpuTimerQueries_[readIdx],
                    GL_QUERY_RESULT,
                    &elapsedNs);
                lastGpuFrameMs_ = static_cast<float>(
                    static_cast<double>(elapsedNs) * (1.0 / 1000000.0));
                lastGpuFrameValid_ = true;
                gpuTimerIssued_[readIdx] = false;
            } else {
                lastGpuFrameValid_ = false;
            }
        } else {
            lastGpuFrameValid_ = false;
        }

        gpuTimerWriteIndex_ ^= 1u;
    } else {
        lastGpuFrameValid_ = false;
    }

    captureScreenshotIfRequested();
    lastFrameDrawCalls_ = frameDrawCalls_;
    lastFrameTriangles_ = frameTriangles_;
    lastFrameIndexedOpaqueDraws_ = frameIndexedOpaqueDraws_;
    lastFrameIndexedBlendDraws_ = frameIndexedBlendDraws_;
    lastFrameIndexedCachedDraws_ = frameIndexedCachedDraws_;
    lastFrameIndexedDynamicDraws_ = frameIndexedDynamicDraws_;
    lastFrameIndexedInstancedDraws_ = frameIndexedInstancedDraws_;
    lastFrameIndexedOutlineBatches_ = frameIndexedOutlineBatches_;
    lastFrameIndexedGeometrySwitches_ = frameIndexedGeometrySwitches_;
    lastFrameIndexedMaterialSwitches_ = frameIndexedMaterialSwitches_;
    lastFrameIndexedTextureSwitches_ = frameIndexedTextureSwitches_;
    lastFrameIndexedGlTextureBindCalls_ = frameIndexedGlTextureBindCalls_;
}

void OpenGLRenderBackend::onResize(int width, int height) {
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

std::string OpenGLRenderBackend::activeGpuName() const {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    return renderer ? reinterpret_cast<const char*>(renderer) : std::string{};
}

bool OpenGLRenderBackend::activeGpuIsDiscrete() const {
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const std::string vendorStr = vendor ? reinterpret_cast<const char*>(vendor) : "";
    const std::string rendererStr = renderer ? reinterpret_cast<const char*>(renderer) : "";
    return !containsCi(vendorStr, "intel") && !containsCi(rendererStr, "intel");
}

bool OpenGLRenderBackend::getLastFrameStats(BackendFrameStats& outStats) const {
    outStats = BackendFrameStats{};
    outStats.drawCalls = lastFrameDrawCalls_;
    outStats.triangles = lastFrameTriangles_;
    outStats.indexedOpaqueDraws = lastFrameIndexedOpaqueDraws_;
    outStats.indexedBlendDraws = lastFrameIndexedBlendDraws_;
    outStats.indexedCachedDraws = lastFrameIndexedCachedDraws_;
    outStats.indexedDynamicDraws = lastFrameIndexedDynamicDraws_;
    outStats.indexedInstancedDraws = lastFrameIndexedInstancedDraws_;
    outStats.indexedOutlineBatches = lastFrameIndexedOutlineBatches_;
    outStats.indexedGeometrySwitches = lastFrameIndexedGeometrySwitches_;
    outStats.indexedMaterialSwitches = lastFrameIndexedMaterialSwitches_;
    outStats.indexedTextureSwitches = lastFrameIndexedTextureSwitches_;
    outStats.indexedGlTextureBindCalls = lastFrameIndexedGlTextureBindCalls_;
    return true;
}

void OpenGLRenderBackend::recordWorldIndexedSubmissionStats(
    const WorldIndexedSubmissionStats& stats) {
    frameIndexedOpaqueDraws_ += stats.opaqueDraws;
    frameIndexedBlendDraws_ += stats.blendDraws;
    frameIndexedCachedDraws_ += stats.cachedDraws;
    frameIndexedDynamicDraws_ += stats.dynamicDraws;
    frameIndexedInstancedDraws_ += stats.instancedDraws;
    frameIndexedOutlineBatches_ += stats.outlineBatches;
    frameIndexedGeometrySwitches_ += stats.geometrySwitches;
    frameIndexedMaterialSwitches_ += stats.materialSwitches;
    frameIndexedTextureSwitches_ += stats.textureSwitches;
}

bool OpenGLRenderBackend::getLastFrameTimings(BackendFrameTimings& outTimings) const {
    outTimings.presentWaitMs = 0.0f;
    outTimings.gpuFrameMs = lastGpuFrameMs_;
    outTimings.gpuFrameValid = lastGpuFrameValid_;
    return true;
}

void OpenGLRenderBackend::shutdown() {
    if (gpuTimingSupported_) {
        glDeleteQueries(static_cast<GLsizei>(gpuTimerQueries_.size()), gpuTimerQueries_.data());
    }
    gpuTimingSupported_ = false;
    gpuTimerQueries_[0] = 0u;
    gpuTimerQueries_[1] = 0u;
    gpuTimerIssued_[0] = false;
    gpuTimerIssued_[1] = false;
    gpuTimerWriteIndex_ = 0u;
    lastGpuFrameMs_ = 0.0f;
    lastGpuFrameValid_ = false;

    destroyWorldSceneColorResources();
    destroyDebugPipeline();
    destroyWorldPipeline();
    destroySpritePipeline();
    clearTextureCaches();
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
    frameDrawCalls_ = 0u;
    frameTriangles_ = 0u;
    lastFrameDrawCalls_ = 0u;
    lastFrameTriangles_ = 0u;
    frameIndexedOpaqueDraws_ = 0u;
    frameIndexedBlendDraws_ = 0u;
    frameIndexedCachedDraws_ = 0u;
    frameIndexedDynamicDraws_ = 0u;
    frameIndexedInstancedDraws_ = 0u;
    frameIndexedOutlineBatches_ = 0u;
    frameIndexedGeometrySwitches_ = 0u;
    frameIndexedMaterialSwitches_ = 0u;
    frameIndexedTextureSwitches_ = 0u;
    frameIndexedGlTextureBindCalls_ = 0u;
    lastFrameIndexedOpaqueDraws_ = 0u;
    lastFrameIndexedBlendDraws_ = 0u;
    lastFrameIndexedCachedDraws_ = 0u;
    lastFrameIndexedDynamicDraws_ = 0u;
    lastFrameIndexedInstancedDraws_ = 0u;
    lastFrameIndexedOutlineBatches_ = 0u;
    lastFrameIndexedGeometrySwitches_ = 0u;
    lastFrameIndexedMaterialSwitches_ = 0u;
    lastFrameIndexedTextureSwitches_ = 0u;
    lastFrameIndexedGlTextureBindCalls_ = 0u;
}

void OpenGLRenderBackend::configureScreenshotCapture() {
    const auto path = engine::env::get("PAC_BACKEND_SCREENSHOT_PATH");
    if (!path.has_value() || path->empty()) return;

    screenshotPath_ = *path;
    screenshotCaptureConfigured_ = true;
    screenshotCaptured_ = false;
    frameCounter_ = 0u;
    screenshotFrameTarget_ = 0u;

    if (const auto frame = engine::env::get("PAC_BACKEND_SCREENSHOT_FRAME")) {
        try {
            screenshotFrameTarget_ = static_cast<std::uint64_t>(std::stoull(*frame));
        } catch (...) {
            screenshotFrameTarget_ = 0u;
        }
    }
}

void OpenGLRenderBackend::captureScreenshotIfRequested() {
    if (!screenshotCaptureConfigured_ || screenshotCaptured_) return;
    if (frameCounter_ < screenshotFrameTarget_) return;

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = std::max(1, viewport[2]);
    const int height = std::max(1, viewport[3]);
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelCount == 0u) {
        screenshotCaptured_ = true;
        return;
    }

    std::vector<unsigned char> rgba(pixelCount * 4u, 0u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    std::vector<unsigned char> flipped(rgba.size(), 0u);
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    for (int y = 0; y < height; ++y) {
        const std::size_t srcOffset = static_cast<std::size_t>(y) * rowBytes;
        const std::size_t dstOffset = static_cast<std::size_t>(height - 1 - y) * rowBytes;
        std::memcpy(flipped.data() + dstOffset, rgba.data() + srcOffset, rowBytes);
    }

    try {
        const std::filesystem::path outPath(screenshotPath_);
        if (!outPath.parent_path().empty()) {
            std::filesystem::create_directories(outPath.parent_path());
        }
        const int wrote = stbi_write_png(
            outPath.string().c_str(),
            width,
            height,
            4,
            flipped.data(),
            width * 4);
        std::cout << "[Screenshot][OpenGL] "
                  << (wrote != 0 ? "WROTE " : "FAILED ")
                  << outPath.string()
                  << " size=" << width << "x" << height
                  << " frame=" << frameCounter_ << "\n";
    } catch (const std::exception& ex) {
        std::cout << "[Screenshot][OpenGL] FAILED exception=" << ex.what() << "\n";
    }

    screenshotCaptured_ = true;
}

