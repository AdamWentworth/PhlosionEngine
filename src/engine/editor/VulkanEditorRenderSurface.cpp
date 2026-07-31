#include "engine/editor/VulkanEditorRenderSurface.h"

#include <algorithm>
#include <cstring>

#include <imgui_impl_vulkan.h>

namespace engine::editor {

VulkanEditorRenderSurface::VulkanEditorRenderSurface(
    VulkanRenderBackend& renderer)
    : renderer_(&renderer) {}

VulkanEditorRenderSurface::~VulkanEditorRenderSurface() {
    shutdown();
}

bool VulkanEditorRenderSurface::registerTextures() {
    if (!renderer_ ||
        surface_ ==
            VulkanRenderBackend::kInvalidEditorSurface) {
        return false;
    }
    for (std::uint32_t frameIndex = 0u;
         frameIndex < kFrameCount;
         ++frameIndex) {
        VkSampler sampler = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        if (!renderer_->getEditorSurfaceTexture(
                surface_,
                frameIndex,
                sampler,
                imageView)) {
            unregisterTextures();
            return false;
        }
        textureDescriptors_[frameIndex] =
            ImGui_ImplVulkan_AddTexture(
                sampler,
                imageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (textureDescriptors_[frameIndex] ==
            VK_NULL_HANDLE) {
            unregisterTextures();
            return false;
        }
    }
    return true;
}

void VulkanEditorRenderSurface::unregisterTextures() {
    for (VkDescriptorSet& descriptor :
         textureDescriptors_) {
        if (descriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descriptor);
            descriptor = VK_NULL_HANDLE;
        }
    }
}

bool VulkanEditorRenderSurface::ensure(
    int width,
    int height) {
    if (!renderer_) {
        return false;
    }
    width = std::max(1, width);
    height = std::max(1, height);
    const bool texturesRegistered =
        std::all_of(
            textureDescriptors_.begin(),
            textureDescriptors_.end(),
            [](VkDescriptorSet descriptor) {
                return descriptor != VK_NULL_HANDLE;
            });
    if (surface_ !=
            VulkanRenderBackend::kInvalidEditorSurface &&
        width_ == width &&
        height_ == height &&
        texturesRegistered) {
        return true;
    }
    if (!texturesRegistered) {
        unregisterTextures();
    }

    if (surface_ ==
        VulkanRenderBackend::kInvalidEditorSurface) {
        surface_ =
            renderer_->createEditorSurface(width, height);
        if (surface_ ==
            VulkanRenderBackend::kInvalidEditorSurface) {
            return false;
        }
    } else if (width_ != width || height_ != height) {
        unregisterTextures();
        if (!renderer_->resizeEditorSurface(
                surface_,
                width,
                height)) {
            return false;
        }
    }
    width_ = width;
    height_ = height;
    return registerTextures();
}

bool VulkanEditorRenderSurface::begin(
    int width,
    int height) {
    if (active_ || !ensure(width, height)) {
        return false;
    }
    activeFrame_ =
        renderer_->currentEditorFrameIndex() %
        kFrameCount;
    active_ = renderer_->beginEditorSurface(surface_);
    return active_;
}

void VulkanEditorRenderSurface::end() {
    if (!active_ || !renderer_) {
        return;
    }
    renderer_->endEditorSurface();
    active_ = false;
}

void VulkanEditorRenderSurface::shutdown() {
    if (active_) {
        end();
    }
    unregisterTextures();
    if (renderer_ &&
        surface_ !=
            VulkanRenderBackend::kInvalidEditorSurface) {
        renderer_->destroyEditorSurface(surface_);
    }
    surface_ =
        VulkanRenderBackend::kInvalidEditorSurface;
    width_ = 0;
    height_ = 0;
    activeFrame_ = 0u;
}

std::uint64_t
VulkanEditorRenderSurface::textureId() const noexcept {
    const VkDescriptorSet descriptor =
        textureDescriptors_[activeFrame_];
    std::uint64_t value = 0u;
    static_assert(
        sizeof(descriptor) <= sizeof(value));
    std::memcpy(
        &value,
        &descriptor,
        sizeof(descriptor));
    return value;
}

} // namespace engine::editor
