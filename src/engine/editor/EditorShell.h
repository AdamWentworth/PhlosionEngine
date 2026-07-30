#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <SDL2/SDL.h>

struct SDL_Window;

namespace engine::editor {

enum class EditorPlayState {
    Editing,
    Playing,
    Paused,
};

struct WorkspacePlayConfiguration {
    std::string id;
    std::string displayName;
    std::string group;
    std::string description;
    std::string executablePath;
    bool available = false;
};

struct WorkspaceView {
    std::string_view projectName;
    std::string_view projectId;
    std::string_view projectRoot;
    std::string_view sceneAssetId;
    std::string_view scenePath;
    std::string_view backendName;
    std::string_view status;
    EditorPlayState playState = EditorPlayState::Editing;
    float simulationSeconds = 0.0f;
    const std::vector<WorkspacePlayConfiguration>*
        playConfigurations = nullptr;
    std::uint32_t sceneCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t drawClassCount = 0u;
    std::uint32_t encounterGrassInstanceCount = 0u;
    std::uint32_t vegetationInstanceCount = 0u;
    std::uint64_t visibleTriangleCount = 0u;
    std::uint64_t shadowTriangleCount = 0u;
    std::size_t archiveFileCount = 0u;
};

struct ProjectBrowserView {
    std::string_view status;
    std::string_view error;
    const std::vector<std::string>* recentProjects = nullptr;
};

struct EditorShellActions {
    bool openProject = false;
    bool closeProject = false;
    bool exit = false;
    bool togglePlay = false;
    bool togglePause = false;
    bool step = false;
    int recentProjectIndex = -1;
    int launchPlayConfigurationIndex = -1;
};

class EditorShell {
public:
    EditorShell();
    ~EditorShell();
    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;

    bool initialize(
        SDL_Window* window,
        std::string* outError = nullptr,
        const char* settingsIniPath = nullptr);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame(float deltaSeconds);
    EditorShellActions drawProjectBrowser(
        const ProjectBrowserView& browser);
    EditorShellActions drawWorkspace(
        const WorkspaceView& workspace);
    void render();

    bool wantsMouseCapture() const;
    bool wantsKeyboardCapture() const;
    bool initialized() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace engine::editor
