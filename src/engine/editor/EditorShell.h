#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <SDL2/SDL.h>

struct SDL_Window;

namespace engine::editor {

struct WorkspaceView {
    std::string_view projectName;
    std::string_view projectId;
    std::string_view projectRoot;
    std::string_view sceneAssetId;
    std::string_view scenePath;
    std::string_view backendName;
    std::string_view status;
    std::uint32_t sceneCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t drawClassCount = 0u;
    std::uint32_t encounterGrassInstanceCount = 0u;
    std::uint32_t vegetationInstanceCount = 0u;
    std::uint64_t visibleTriangleCount = 0u;
    std::uint64_t shadowTriangleCount = 0u;
    std::size_t archiveFileCount = 0u;
};

class EditorShell {
public:
    EditorShell();
    ~EditorShell();
    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;

    bool initialize(
        SDL_Window* window,
        std::string* outError = nullptr);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame(float deltaSeconds);
    void drawWorkspace(const WorkspaceView& workspace);
    void render();

    bool wantsMouseCapture() const;
    bool wantsKeyboardCapture() const;
    bool initialized() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace engine::editor
