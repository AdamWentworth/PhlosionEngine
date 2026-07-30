#define SDL_MAIN_HANDLED

#include "engine/editor/EditorProjectPlugin.h"
#include "engine/editor/EditorShell.h"
#include "engine/editor/ProjectDescriptor.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/OpenGLRenderBackend.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#else
#include <dlfcn.h>
#endif

#ifndef PHLOSION_EDITOR_BUILD_CONFIGURATION
#define PHLOSION_EDITOR_BUILD_CONFIGURATION "Unknown"
#endif

namespace {

struct Arguments {
    std::filesystem::path project;
    int frameLimit = 0;
};

struct WindowPlacement {
    int x = 0;
    int y = 0;
    int width = 1440;
    int height = 900;
    bool maximized = false;
    bool valid = false;
};

Arguments parseArguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) {
            continue;
        }
        const std::string argument(argv[index]);
        constexpr std::string_view projectPrefix = "--project=";
        constexpr std::string_view framesPrefix = "--frames=";
        if (argument.rfind(projectPrefix, 0u) == 0u) {
            result.project = argument.substr(projectPrefix.size());
        } else if (argument.rfind(framesPrefix, 0u) == 0u) {
            result.frameLimit = std::max(
                1,
                std::stoi(argument.substr(framesPrefix.size())));
        } else if (!argument.empty() && argument.front() != '-') {
            result.project = argument;
        }
    }
    return result;
}

std::filesystem::path editorStateDirectory() {
    char* rawPath = SDL_GetPrefPath("Phlosion", "Editor");
    if (!rawPath) {
        return std::filesystem::current_path() /
               ".phlosion-editor";
    }
    const std::filesystem::path result(rawPath);
    SDL_free(rawPath);
    return result;
}

std::filesystem::path normalizeDescriptorPath(
    std::filesystem::path path) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error) && !error) {
        path /= "phlosion.project.json";
    }
    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) {
        path = absolute;
    }
    const auto canonical =
        std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

std::vector<std::string> loadRecentProjects(
    const std::filesystem::path& stateDirectory) {
    std::vector<std::string> projects;
    std::ifstream input(stateDirectory / "recent_projects.txt");
    std::string line;
    while (std::getline(input, line) && projects.size() < 10u) {
        if (line.empty()) {
            continue;
        }
        std::error_code error;
        if (std::filesystem::is_regular_file(line, error) && !error) {
            projects.push_back(std::move(line));
        }
    }
    return projects;
}

void saveRecentProjects(
    const std::filesystem::path& stateDirectory,
    const std::vector<std::string>& projects) {
    std::error_code error;
    std::filesystem::create_directories(stateDirectory, error);
    std::ofstream output(
        stateDirectory / "recent_projects.txt",
        std::ios::trunc);
    for (const std::string& project : projects) {
        output << project << '\n';
    }
}

void promoteRecentProject(
    std::vector<std::string>& projects,
    const std::filesystem::path& descriptorPath) {
    const std::string path = descriptorPath.generic_string();
    projects.erase(
        std::remove(projects.begin(), projects.end(), path),
        projects.end());
    projects.insert(projects.begin(), path);
    if (projects.size() > 10u) {
        projects.resize(10u);
    }
}

WindowPlacement loadWindowPlacement(
    const std::filesystem::path& stateDirectory) {
    WindowPlacement placement;
    int maximized = 0;
    std::ifstream input(stateDirectory / "window_state.txt");
    if (input >> placement.x >> placement.y >>
            placement.width >> placement.height >> maximized) {
        placement.maximized = maximized != 0;
        placement.valid =
            placement.width >= 640 && placement.height >= 480;
    }
    return placement;
}

bool intersectsDisplay(const WindowPlacement& placement) {
    const int displayCount = SDL_GetNumVideoDisplays();
    for (int display = 0; display < displayCount; ++display) {
        SDL_Rect bounds{};
        if (SDL_GetDisplayUsableBounds(display, &bounds) != 0) {
            continue;
        }
        const int overlapWidth = std::max(
            0,
            std::min(placement.x + placement.width, bounds.x + bounds.w) -
                std::max(placement.x, bounds.x));
        const int overlapHeight = std::max(
            0,
            std::min(placement.y + placement.height, bounds.y + bounds.h) -
                std::max(placement.y, bounds.y));
        if (overlapWidth >= 160 && overlapHeight >= 120) {
            return true;
        }
    }
    return false;
}

void applyWindowPlacement(
    SDL_Window* window,
    const WindowPlacement& placement) {
    if (!window || !placement.valid ||
        !intersectsDisplay(placement)) {
        return;
    }
    SDL_SetWindowPosition(window, placement.x, placement.y);
    SDL_SetWindowSize(window, placement.width, placement.height);
    if (placement.maximized) {
        SDL_MaximizeWindow(window);
    }
}

void updateWindowPlacement(
    SDL_Window* window,
    WindowPlacement& placement) {
    if (!window) {
        return;
    }
    const Uint32 flags = SDL_GetWindowFlags(window);
    placement.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0u;
    if (!placement.maximized && (flags & SDL_WINDOW_MINIMIZED) == 0u) {
        SDL_GetWindowPosition(window, &placement.x, &placement.y);
        SDL_GetWindowSize(
            window,
            &placement.width,
            &placement.height);
        placement.valid = true;
    }
}

void saveWindowPlacement(
    const std::filesystem::path& stateDirectory,
    const WindowPlacement& placement) {
    if (!placement.valid) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(stateDirectory, error);
    std::ofstream output(
        stateDirectory / "window_state.txt",
        std::ios::trunc);
    output << placement.x << ' ' << placement.y << ' '
           << placement.width << ' ' << placement.height << ' '
           << (placement.maximized ? 1 : 0) << '\n';
}

std::optional<std::filesystem::path> showProjectOpenDialog(
    SDL_Window* owner,
    const std::vector<std::string>& recentProjects,
    std::string& outError) {
#if defined(_WIN32)
    wchar_t selected[32768]{};
    const wchar_t filter[] =
        L"Phlosion Project\0phlosion.project.json\0"
        L"JSON Files\0*.json\0"
        L"All Files\0*.*\0\0";
    std::wstring initialDirectory;
    if (!recentProjects.empty()) {
        initialDirectory =
            std::filesystem::path(recentProjects.front())
                .parent_path()
                .wstring();
    }

    HWND ownerHandle = nullptr;
    SDL_SysWMinfo windowInfo{};
    SDL_VERSION(&windowInfo.version);
    if (owner && SDL_GetWindowWMInfo(owner, &windowInfo) == SDL_TRUE) {
        ownerHandle = windowInfo.info.win.window;
    }

    OPENFILENAMEW request{};
    request.lStructSize = sizeof(request);
    request.hwndOwner = ownerHandle;
    request.lpstrFile = selected;
    request.nMaxFile =
        static_cast<DWORD>(std::size(selected));
    request.lpstrFilter = filter;
    request.nFilterIndex = 1u;
    request.lpstrInitialDir =
        initialDirectory.empty()
            ? nullptr
            : initialDirectory.c_str();
    request.lpstrTitle = L"Open Phlosion Project";
    request.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR |
        OFN_EXPLORER;
    if (GetOpenFileNameW(&request) != FALSE) {
        outError.clear();
        return std::filesystem::path(selected);
    }
    const DWORD dialogError = CommDlgExtendedError();
    if (dialogError != 0u) {
        outError =
            "Windows project picker failed with error " +
            std::to_string(dialogError) + ".";
    }
    return std::nullopt;
#else
    (void)owner;
    (void)recentProjects;
    outError =
        "The native project picker is not implemented on this platform. "
        "Pass a project path on the command line or drop it on the window.";
    return std::nullopt;
#endif
}

class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary() {
        close();
    }
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    bool open(
        const std::filesystem::path& path,
        std::string& outError) {
        close();
#if defined(_WIN32)
        handle_ = LoadLibraryExW(
            path.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle_) {
            outError =
                "Could not load project editor plugin (Windows error " +
                std::to_string(GetLastError()) + "): " +
                path.generic_string();
            return false;
        }
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_) {
            const char* message = dlerror();
            outError =
                "Could not load project editor plugin: " +
                std::string(message ? message : "unknown error") +
                " (" + path.generic_string() + ")";
            return false;
        }
#endif
        outError.clear();
        return true;
    }

    template <typename Function>
    Function function(const char* name) const {
#if defined(_WIN32)
        return reinterpret_cast<Function>(
            GetProcAddress(handle_, name));
#else
        return reinterpret_cast<Function>(
            dlsym(handle_, name));
#endif
    }

private:
    void close() {
        if (!handle_) {
            return;
        }
#if defined(_WIN32)
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }

#if defined(_WIN32)
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

struct LoadedProject {
    ~LoadedProject() {
        if (runtime && destroyRuntime) {
            destroyRuntime(runtime);
            runtime = nullptr;
        }
    }

    engine::editor::ProjectDescriptor descriptor;
    std::filesystem::path descriptorPath;
    std::filesystem::path root;
    std::filesystem::path scenePath;
    std::filesystem::path pluginPath;
    std::string descriptorPathText;
    std::string rootText;
    std::string scenePathText;
    std::string status;
    DynamicLibrary library;
    engine::editor::IEditorProjectRuntime* runtime = nullptr;
    engine::editor::DestroyEditorProjectRuntimeFn destroyRuntime =
        nullptr;
};

std::unique_ptr<LoadedProject> loadProject(
    const std::filesystem::path& requestedDescriptorPath,
    IRenderBackend& renderer,
    const Camera3D& camera,
    std::string& outError) {
    auto loaded = std::make_unique<LoadedProject>();
    loaded->descriptorPath =
        normalizeDescriptorPath(requestedDescriptorPath);
    if (!std::filesystem::is_regular_file(
            loaded->descriptorPath)) {
        outError =
            "Project descriptor does not exist: " +
            loaded->descriptorPath.generic_string();
        return nullptr;
    }
    if (!engine::editor::loadProjectDescriptor(
            loaded->descriptorPath,
            loaded->descriptor,
            &outError)) {
        return nullptr;
    }
    if (!engine::editor::resolveStartupScenePath(
            loaded->descriptorPath,
            loaded->descriptor,
            loaded->scenePath,
            &outError)) {
        return nullptr;
    }
    if (!engine::editor::resolveEditorPluginPath(
            loaded->descriptorPath,
            loaded->descriptor,
            PHLOSION_EDITOR_BUILD_CONFIGURATION,
            loaded->pluginPath,
            &outError)) {
        return nullptr;
    }
    if (!std::filesystem::is_regular_file(
            loaded->pluginPath)) {
        outError =
            "Project editor plugin is not built for " +
            std::string(PHLOSION_EDITOR_BUILD_CONFIGURATION) +
            ": " + loaded->pluginPath.generic_string() +
            "\nBuild the " +
            loaded->descriptor.editorPlugin.library +
            " target in the game repository, then open the project again.";
        return nullptr;
    }
    if (!loaded->library.open(loaded->pluginPath, outError)) {
        return nullptr;
    }

    const auto abiVersion =
        loaded->library.function<
            engine::editor::EditorProjectPluginAbiVersionFn>(
            engine::editor::kEditorProjectPluginAbiSymbol);
    const auto createRuntime =
        loaded->library.function<
            engine::editor::CreateEditorProjectRuntimeFn>(
            engine::editor::kCreateEditorProjectRuntimeSymbol);
    loaded->destroyRuntime =
        loaded->library.function<
            engine::editor::DestroyEditorProjectRuntimeFn>(
            engine::editor::kDestroyEditorProjectRuntimeSymbol);
    if (!abiVersion || !createRuntime ||
        !loaded->destroyRuntime) {
        outError =
            "Project editor plugin is missing required Phlosion symbols: " +
            loaded->pluginPath.generic_string();
        return nullptr;
    }
    if (abiVersion() !=
        engine::editor::kEditorProjectPluginAbiVersion) {
        outError =
            "Project editor plugin ABI does not match this Phlosion Editor.";
        return nullptr;
    }
    loaded->runtime = createRuntime();
    if (!loaded->runtime) {
        outError =
            "Project editor plugin could not create its runtime.";
        return nullptr;
    }

    loaded->root =
        loaded->descriptorPath.parent_path();
    loaded->descriptorPathText =
        loaded->descriptorPath.generic_string();
    loaded->rootText = loaded->root.generic_string();
    loaded->scenePathText =
        loaded->scenePath.generic_string();
    const engine::editor::EditorProjectOpenContext openContext{
        .descriptor = &loaded->descriptor,
        .descriptorPath = loaded->descriptorPathText.c_str(),
        .projectRoot = loaded->rootText.c_str(),
        .startupScenePath = loaded->scenePathText.c_str()};
    if (!loaded->runtime->open(openContext, &outError)) {
        return nullptr;
    }

    const glm::vec3 cameraPosition = camera.getPosition();
    const glm::vec3 cameraForward = camera.getDirection();
    const glm::vec3 cameraTarget = camera.getTarget();
    const engine::editor::EditorProjectCameraContext cameraContext{
        .cameraWorldPosition3 = glm::value_ptr(cameraPosition),
        .cameraForward3 = glm::value_ptr(cameraForward),
        .cameraTarget3 = glm::value_ptr(cameraTarget)};
    loaded->runtime->prewarm(renderer, cameraContext);
    loaded->status =
        loaded->runtime->status()
            ? loaded->runtime->status()
            : "Project loaded.";
    outError.clear();
    return loaded;
}

glm::vec3 horizontalDirection(glm::vec3 direction) {
    direction.y = 0.0f;
    const float length = glm::length(direction);
    if (length <= 0.0001f) {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return direction / length;
}

} // namespace

int main(int argc, char** argv) {
    const Arguments arguments = parseArguments(argc, argv);
    int result = 0;
    try {
        Window window(
            "Phlosion Editor",
            1440,
            900,
            Window::GraphicsApi::OpenGL,
            true);
        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(
                    SDL_GL_GetProcAddress))) {
            throw std::runtime_error("Failed to initialize GLAD.");
        }

        const std::filesystem::path stateDirectory =
            editorStateDirectory();
        WindowPlacement windowPlacement =
            loadWindowPlacement(stateDirectory);
        applyWindowPlacement(
            window.getSDLWindow(),
            windowPlacement);
        updateWindowPlacement(
            window.getSDLWindow(),
            windowPlacement);

        int width = 0;
        int height = 0;
        window.getDrawableSize(width, height);
        width = std::max(width, 1);
        height = std::max(height, 1);
        glViewport(0, 0, width, height);

        OpenGLRenderBackend renderer;
        renderer.onResize(width, height);
        renderer.prewarmWorldRenderAssets();

        Camera3D camera(
            35.0f,
            static_cast<float>(width) /
                static_cast<float>(height),
            0.1f,
            200.0f);

        std::string error;
        const std::string layoutPath =
            (stateDirectory / "layout.ini").string();
        engine::editor::EditorShell editor;
        if (!editor.initialize(
                window.getSDLWindow(),
                &error,
                layoutPath.c_str())) {
            throw std::runtime_error(error);
        }

        std::vector<std::string> recentProjects =
            loadRecentProjects(stateDirectory);
        std::unique_ptr<LoadedProject> project;
        std::optional<std::filesystem::path> pendingProject;
        if (!arguments.project.empty()) {
            pendingProject = arguments.project;
        }
        std::string browserStatus =
            "Open a project or drop phlosion.project.json onto this window.";
        std::string browserError;

        bool running = true;
        int frameCount = 0;
        float simulationSeconds = 0.0f;
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();

        while (running) {
            const auto now = Clock::now();
            const float deltaSeconds = std::min(
                0.05f,
                std::chrono::duration<float>(
                    now - previous).count());
            previous = now;
            simulationSeconds += deltaSeconds;

            float wheelDelta = 0.0f;
            glm::vec2 panPixels(0.0f);
            glm::vec2 orbitRadians(0.0f);
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                editor.processEvent(event);
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_DROPFILE) {
                    if (event.drop.file) {
                        pendingProject =
                            std::filesystem::path(event.drop.file);
                        SDL_free(event.drop.file);
                    }
                } else if (
                    event.type == SDL_WINDOWEVENT &&
                    (event.window.event ==
                         SDL_WINDOWEVENT_SIZE_CHANGED ||
                     event.window.event ==
                         SDL_WINDOWEVENT_RESIZED)) {
                    window.getDrawableSize(width, height);
                    width = std::max(width, 1);
                    height = std::max(height, 1);
                    glViewport(0, 0, width, height);
                    renderer.onResize(width, height);
                    camera.setAspectRatio(
                        static_cast<float>(width) /
                        static_cast<float>(height));
                    updateWindowPlacement(
                        window.getSDLWindow(),
                        windowPlacement);
                } else if (
                    event.type == SDL_WINDOWEVENT &&
                    (event.window.event ==
                         SDL_WINDOWEVENT_MOVED ||
                     event.window.event ==
                         SDL_WINDOWEVENT_MAXIMIZED ||
                     event.window.event ==
                         SDL_WINDOWEVENT_RESTORED)) {
                    updateWindowPlacement(
                        window.getSDLWindow(),
                        windowPlacement);
                } else if (
                    project &&
                    event.type == SDL_MOUSEWHEEL &&
                    !editor.wantsMouseCapture()) {
                    wheelDelta +=
                        static_cast<float>(event.wheel.y);
                } else if (
                    project &&
                    event.type == SDL_MOUSEMOTION &&
                    !editor.wantsMouseCapture()) {
                    const bool shift =
                        (SDL_GetModState() & KMOD_SHIFT) != 0;
                    const bool left =
                        (event.motion.state &
                         SDL_BUTTON_LMASK) != 0u;
                    const bool middle =
                        (event.motion.state &
                         SDL_BUTTON_MMASK) != 0u;
                    const bool right =
                        (event.motion.state &
                         SDL_BUTTON_RMASK) != 0u;
                    if (left || (middle && shift)) {
                        panPixels += glm::vec2(
                            static_cast<float>(
                                event.motion.xrel),
                            static_cast<float>(
                                event.motion.yrel));
                    } else if (right || middle) {
                        orbitRadians +=
                            glm::vec2(
                                -static_cast<float>(
                                    event.motion.xrel),
                                -static_cast<float>(
                                    event.motion.yrel)) *
                            0.006f;
                    }
                }
            }
            if (!running) {
                break;
            }

            if (pendingProject) {
                std::unique_ptr<LoadedProject> candidate =
                    loadProject(
                        *pendingProject,
                        renderer,
                        camera,
                        browserError);
                pendingProject.reset();
                if (candidate) {
                    promoteRecentProject(
                        recentProjects,
                        candidate->descriptorPath);
                    saveRecentProjects(
                        stateDirectory,
                        recentProjects);
                    project = std::move(candidate);
                    browserError.clear();
                    browserStatus = "Project loaded.";
                    simulationSeconds = 0.0f;
                    window.setTitle(
                        project->descriptor.displayName +
                        " - Phlosion Editor");
                } else {
                    std::cerr
                        << "[Phlosion Editor] "
                        << browserError << '\n';
                    if (project) {
                        project->status =
                            "Project open failed: " + browserError;
                    }
                }
            }

            editor.beginFrame(deltaSeconds);
            if (project && !editor.wantsKeyboardCapture()) {
                const Uint8* keys =
                    SDL_GetKeyboardState(nullptr);
                const glm::vec3 forward =
                    horizontalDirection(camera.getDirection());
                const glm::vec3 right =
                    glm::normalize(glm::cross(
                        forward,
                        glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::vec3 translation(0.0f);
                if (keys[SDL_SCANCODE_W]) translation += forward;
                if (keys[SDL_SCANCODE_S]) translation -= forward;
                if (keys[SDL_SCANCODE_D]) translation += right;
                if (keys[SDL_SCANCODE_A]) translation -= right;
                if (keys[SDL_SCANCODE_E]) translation.y += 1.0f;
                if (keys[SDL_SCANCODE_Q]) translation.y -= 1.0f;
                if (glm::length(translation) > 0.001f) {
                    const float speed =
                        (keys[SDL_SCANCODE_LSHIFT] ||
                         keys[SDL_SCANCODE_RSHIFT])
                            ? 12.0f
                            : 5.0f;
                    camera.move(
                        glm::normalize(translation) *
                        speed * deltaSeconds);
                }
            }
            if (glm::length(panPixels) > 0.001f) {
                camera.panPlanar(
                    panPixels.x,
                    panPixels.y,
                    0.012f);
            }
            if (glm::length(orbitRadians) > 0.0001f) {
                camera.orbit(
                    orbitRadians.x,
                    orbitRadians.y);
            }
            if (wheelDelta != 0.0f) {
                camera.zoom(wheelDelta * 1.25f);
            }

            renderer.beginFrame(
                0.025f,
                0.031f,
                0.037f,
                1.0f);
            engine::editor::EditorShellActions actions;
            if (project) {
                project->runtime->update(simulationSeconds);
                const glm::mat4 viewProjection =
                    camera.getProjectionMatrix() *
                    camera.getViewMatrix();
                const glm::vec3 cameraPosition =
                    camera.getPosition();
                const glm::vec3 cameraForward =
                    camera.getDirection();
                const glm::vec3 cameraTarget =
                    camera.getTarget();
                renderer.beginWorldSceneColorPass(width, height);
                const engine::editor::EditorProjectRenderContext
                    renderContext{
                        .renderer = &renderer,
                        .viewProjectionMatrix4x4 =
                            glm::value_ptr(viewProjection),
                        .surfaceWidth = width,
                        .surfaceHeight = height,
                        .cameraWorldPosition3 =
                            glm::value_ptr(cameraPosition),
                        .cameraForward3 =
                            glm::value_ptr(cameraForward),
                        .cameraTarget3 =
                            glm::value_ptr(cameraTarget)};
                project->runtime->render(renderContext);
                renderer.endWorldSceneColorPass();

                const auto stats =
                    project->runtime->stats();
                const engine::editor::WorkspaceView workspace{
                    .projectName =
                        project->descriptor.displayName,
                    .projectId =
                        project->descriptor.projectId,
                    .projectRoot = project->rootText,
                    .sceneAssetId =
                        project->descriptor.startupScene.assetId,
                    .scenePath = project->scenePathText,
                    .backendName =
                        "OpenGL 3.3 / project renderer plugin",
                    .status = project->status,
                    .sceneCount = stats.sceneCount,
                    .materialCount = stats.materialCount,
                    .drawClassCount = stats.drawClassCount,
                    .encounterGrassInstanceCount =
                        stats.encounterGrassInstanceCount,
                    .vegetationInstanceCount =
                        stats.vegetationInstanceCount,
                    .visibleTriangleCount =
                        stats.visibleTriangleCount,
                    .shadowTriangleCount =
                        stats.shadowTriangleCount,
                    .archiveFileCount =
                        stats.archiveFileCount};
                actions = editor.drawWorkspace(workspace);
            } else {
                const engine::editor::ProjectBrowserView browser{
                    .status = browserStatus,
                    .error = browserError,
                    .recentProjects = &recentProjects};
                actions = editor.drawProjectBrowser(browser);
            }
            editor.render();
            renderer.endFrame();
            window.swapBuffers();

            if (actions.exit) {
                running = false;
            }
            if (actions.closeProject) {
                project.reset();
                window.setTitle("Phlosion Editor");
            }
            if (actions.recentProjectIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.recentProjectIndex) <
                    recentProjects.size()) {
                pendingProject =
                    recentProjects[static_cast<std::size_t>(
                        actions.recentProjectIndex)];
            }
            if (actions.openProject) {
                std::string dialogError;
                const auto selected = showProjectOpenDialog(
                    window.getSDLWindow(),
                    recentProjects,
                    dialogError);
                if (selected) {
                    pendingProject = *selected;
                } else if (!dialogError.empty()) {
                    browserError = std::move(dialogError);
                }
            }

            ++frameCount;
            if (arguments.frameLimit > 0 &&
                frameCount >= arguments.frameLimit) {
                running = false;
            }
        }

        updateWindowPlacement(
            window.getSDLWindow(),
            windowPlacement);
        saveWindowPlacement(
            stateDirectory,
            windowPlacement);
        project.reset();
        editor.shutdown();
        renderer.shutdown();
    } catch (const std::exception& exception) {
        std::cerr
            << "[Phlosion Editor] "
            << exception.what() << '\n';
        result = 1;
    }
    if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0) {
        SDL_Quit();
    }
    return result;
}
