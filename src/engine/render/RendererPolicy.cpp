#include "engine/render/RendererPolicy.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace engine::render {

const char* rendererHostPlatform() noexcept {
#if defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "other";
#endif
}

bool rendererCompiled(std::string_view backend) noexcept {
    if (backend == "vulkan") return true;
#if defined(_WIN32)
    if (backend == "d3d12") return true;
#endif
#if PHLOSION_ENABLE_OPENGL
    if (backend == "opengl") return true;
#endif
    return false;
}

RendererPolicy defaultRendererPolicy() {
    RendererPolicy policy{{{"windows", {"d3d12", "vulkan"}}, {"linux", {"vulkan"}}}};
#if PHLOSION_ENABLE_OPENGL
    policy.platforms["windows"].push_back("opengl");
    policy.platforms["linux"].push_back("opengl");
    policy.platforms["macos"] = {"opengl"};
    policy.platforms["other"] = {"opengl"};
#endif
    return policy;
}

RendererPolicy parseRendererPolicy(std::string_view projectJson) {
    const auto project = nlohmann::json::parse(projectJson);
    if (!project.is_object()) throw std::runtime_error("Project must be a JSON object");
    if (!project.contains("rendering")) return defaultRendererPolicy();
    const auto& rendering = project.at("rendering");
    if (!rendering.is_object() || rendering.empty())
        throw std::runtime_error("rendering must declare nonempty platform renderer lists");
    RendererPolicy policy;
    for (const auto& [platform, entries] : rendering.items()) {
        if (platform != "windows" && platform != "linux" && platform != "macos" && platform != "other")
            throw std::runtime_error("Unknown rendering platform: " + platform);
        if (!entries.is_array() || entries.empty())
            throw std::runtime_error("Empty or invalid rendering list for " + platform);
        auto& names = policy.platforms[platform];
        for (const auto& entry : entries) {
            if (!entry.is_string()) throw std::runtime_error("Renderer names must be strings");
            const auto name = entry.get<std::string>();
            if (name != "d3d12" && name != "vulkan" && name != "opengl")
                throw std::runtime_error("Unknown renderer: " + name);
            if (name == "d3d12" && platform != "windows")
                throw std::runtime_error("d3d12 requires windows enrollment");
            if (std::find(names.begin(), names.end(), name) != names.end())
                throw std::runtime_error("Duplicate renderer: " + name);
            names.push_back(name);
        }
    }
    return policy;
}

RendererPolicy loadRendererPolicy(const std::filesystem::path& descriptor) {
    std::ifstream file(descriptor);
    if (!file) throw std::runtime_error("Cannot read renderer policy: " + descriptor.string());
    return parseRendererPolicy(std::string(std::istreambuf_iterator<char>(file), {}));
}

std::vector<std::string> availableRenderers(const RendererPolicy& policy, std::string_view platform) {
    std::vector<std::string> result;
    const auto found = policy.platforms.find(std::string(platform));
    if (found != policy.platforms.end()) {
        for (const auto& name : found->second) {
            if (rendererCompiled(name) && (name != "d3d12" || platform == "windows")) result.push_back(name);
        }
    }
    return result;
}

std::vector<std::string> rendererCandidates(const RendererPolicy& policy, std::string_view requested,
                                          std::string_view platform) {
    auto available = availableRenderers(policy, platform);
    if (available.empty()) throw std::runtime_error("No enrolled renderer is available for " + std::string(platform));
    if (requested == "auto") return available;
    if (std::find(available.begin(), available.end(), requested) == available.end())
        throw std::runtime_error("Renderer '" + std::string(requested) + "' is not enrolled for this project/platform or is disabled in this engine build");
    return {std::string(requested)};
}

} // namespace engine::render
