#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// Ordered per-platform enrollment; the first available entry is the default.
struct RendererPolicy {
    std::map<std::string, std::vector<std::string>> platforms;
};

const char* rendererHostPlatform() noexcept;
bool rendererCompiled(std::string_view backend) noexcept;
RendererPolicy defaultRendererPolicy();
RendererPolicy parseRendererPolicy(std::string_view projectJson);
RendererPolicy loadRendererPolicy(const std::filesystem::path& descriptor);
std::vector<std::string> availableRenderers(
    const RendererPolicy& policy, std::string_view platform = rendererHostPlatform());
// Explicit requests never fall back. Auto returns the enrolled fallback order.
std::vector<std::string> rendererCandidates(
    const RendererPolicy& policy, std::string_view requested = "auto",
    std::string_view platform = rendererHostPlatform());

} // namespace engine::render
