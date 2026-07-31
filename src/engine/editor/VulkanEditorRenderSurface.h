#pragma once

#include "engine/editor/EditorRenderSurface.h"
#include "engine/render/VulkanRenderBackend.h"

#include <array>
#include <cstdint>

#include <vulkan/vulkan.h>

namespace engine::editor {

class VulkanEditorRenderSurface final
    : public EditorRenderSurface {
public:
    explicit VulkanEditorRenderSurface(
        VulkanRenderBackend& renderer);
    ~VulkanEditorRenderSurface() override;

    VulkanEditorRenderSurface(
        const VulkanEditorRenderSurface&) = delete;
    VulkanEditorRenderSurface& operator=(
        const VulkanEditorRenderSurface&) = delete;

    bool begin(int width, int height) override;
    void end() override;
    void shutdown() override;
    std::uint64_t textureId() const noexcept override;

private:
    bool ensure(int width, int height);
    bool registerTextures();
    void unregisterTextures();

    static constexpr std::uint32_t kFrameCount = 2u;
    VulkanRenderBackend* renderer_ = nullptr;
    VulkanRenderBackend::EditorSurfaceHandle surface_ =
        VulkanRenderBackend::kInvalidEditorSurface;
    std::array<VkDescriptorSet, kFrameCount>
        textureDescriptors_{};
    int width_ = 0;
    int height_ = 0;
    std::uint32_t activeFrame_ = 0u;
    bool active_ = false;
};

} // namespace engine::editor
