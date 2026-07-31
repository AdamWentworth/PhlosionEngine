#define SDL_MAIN_HANDLED

#include "engine/editor/EditorProjectPlugin.h"
#include "engine/editor/EditorShell.h"
#include "engine/editor/OpenGLEditorRenderSurface.h"
#include "engine/editor/ProjectDescriptor.h"
#include "engine/input/SdlKeyMap.h"
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
#include <cctype>
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
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef PHLOSION_EDITOR_BUILD_CONFIGURATION
#define PHLOSION_EDITOR_BUILD_CONFIGURATION "Unknown"
#endif

namespace {

struct Arguments {
    std::filesystem::path project;
    std::string gamePreview;
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
        constexpr std::string_view gamePreviewPrefix =
            "--game-preview=";
        constexpr std::string_view framesPrefix = "--frames=";
        if (argument.rfind(projectPrefix, 0u) == 0u) {
            result.project = argument.substr(projectPrefix.size());
        } else if (
            argument.rfind(gamePreviewPrefix, 0u) == 0u) {
            result.gamePreview =
                argument.substr(gamePreviewPrefix.size());
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

struct CookedAssetType {
    const char* typeName;
    const char* category;
    int sortOrder;
};

std::string friendlyAssetToken(std::string value) {
    std::replace(
        value.begin(),
        value.end(),
        '_',
        ' ');
    return value;
}

std::string cookedObjectOwner(
    const std::filesystem::path& path) {
    std::filesystem::path ownerPath =
        path.parent_path();
    if (ownerPath.filename() == "textures") {
        ownerPath = ownerPath.parent_path();
    }
    std::string owner =
        ownerPath.filename().string();
    const std::size_t hashSeparator =
        owner.rfind('-');
    if (hashSeparator != std::string::npos) {
        const std::string_view suffix(
            owner.data() + hashSeparator + 1u,
            owner.size() - hashSeparator - 1u);
        const bool looksLikeHash =
            suffix.size() >= 8u &&
            std::all_of(
                suffix.begin(),
                suffix.end(),
                [](unsigned char character) {
                    return std::isxdigit(character) != 0;
                });
        if (looksLikeHash) {
            owner.erase(hashSeparator);
        }
    }
    return friendlyAssetToken(std::move(owner));
}

std::string cookedAssetDisplayName(
    const std::filesystem::path& path,
    const CookedAssetType& type) {
    const std::string extension =
        path.extension().string();
    if (extension == ".phlo") {
        return friendlyAssetToken(
            path.stem().string());
    }
    const std::string owner =
        cookedObjectOwner(path);
    if ((extension == ".phmesh" ||
         extension == ".phmat" ||
         extension == ".phanim" ||
         extension == ".phskel") &&
        !owner.empty()) {
        return owner + " / " + type.typeName;
    }
    if (extension == ".ktx2" && !owner.empty()) {
        return owner + " / " +
               path.filename().string();
    }
    return path.filename().string();
}

std::optional<CookedAssetType> cookedAssetType(
    std::string extension) {
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character));
        });
    if (extension == ".phscene") {
        return CookedAssetType{
            "World Scene", "Scenes", 0};
    }
    if (extension == ".phlo") {
        return CookedAssetType{
            "Prefab", "Prefabs", 1};
    }
    if (extension == ".phmesh") {
        return CookedAssetType{
            "Mesh", "Meshes", 2};
    }
    if (extension == ".phmat") {
        return CookedAssetType{
            "Material", "Materials", 3};
    }
    if (extension == ".phanim") {
        return CookedAssetType{
            "Animation Set", "Animations", 4};
    }
    if (extension == ".phskel") {
        return CookedAssetType{
            "Skeleton", "Skeletons", 5};
    }
    if (extension == ".ktx2") {
        return CookedAssetType{
            "Texture", "Textures", 6};
    }
    if (extension == ".json") {
        return CookedAssetType{
            "Metadata", "Metadata", 7};
    }
    return std::nullopt;
}

std::string byteCountLabel(std::uintmax_t bytes) {
    constexpr std::uintmax_t kib = 1024u;
    constexpr std::uintmax_t mib = kib * 1024u;
    if (bytes >= mib) {
        const std::uintmax_t tenths =
            (bytes * 10u) / mib;
        return std::to_string(tenths / 10u) +
               "." +
               std::to_string(tenths % 10u) +
               " MiB";
    }
    if (bytes >= kib) {
        const std::uintmax_t tenths =
            (bytes * 10u) / kib;
        return std::to_string(tenths / 10u) +
               "." +
               std::to_string(tenths % 10u) +
               " KiB";
    }
    return std::to_string(bytes) + " bytes";
}

std::vector<engine::editor::WorkspaceAsset>
discoverCookedAssets(
    const std::filesystem::path& projectRoot,
    const engine::editor::ProjectDescriptor& descriptor) {
    struct PendingAsset {
        engine::editor::WorkspaceAsset view;
        int sortOrder = 0;
    };
    std::vector<PendingAsset> pending;
    for (const auto& mount : descriptor.contentMounts) {
        const std::filesystem::path mountRoot =
            (projectRoot / mount.root)
                .lexically_normal();
        std::error_code iteratorError;
        std::filesystem::recursive_directory_iterator
            iterator(
                mountRoot,
                std::filesystem::
                    directory_options::skip_permission_denied,
                iteratorError);
        const std::filesystem::
            recursive_directory_iterator end;
        while (!iteratorError && iterator != end) {
            const auto entry = *iterator;
            iterator.increment(iteratorError);
            std::error_code typeError;
            if (!entry.is_regular_file(typeError) ||
                typeError) {
                continue;
            }
            const auto type =
                cookedAssetType(
                    entry.path().extension().string());
            if (!type) {
                continue;
            }
            std::error_code relativeError;
            const auto relativeToProject =
                std::filesystem::relative(
                    entry.path(),
                    projectRoot,
                    relativeError);
            if (relativeError) {
                continue;
            }
            std::error_code sizeError;
            const std::uintmax_t bytes =
                entry.file_size(sizeError);
            const std::string projectPath =
                relativeToProject.generic_string();
            pending.push_back(PendingAsset{
                .view =
                    engine::editor::WorkspaceAsset{
                        .id = projectPath,
                        .displayName =
                            cookedAssetDisplayName(
                                entry.path(),
                                *type),
                        .typeName = type->typeName,
                        .category = type->category,
                        .path = projectPath,
                        .properties = {
                            {"Asset type", type->typeName},
                            {"Format",
                             entry.path()
                                 .extension()
                                 .string()},
                            {"Content mount", mount.id},
                            {"Project path", projectPath},
                            {"Cooked size",
                             sizeError
                                 ? "Unavailable"
                                 : byteCountLabel(bytes)},
                        }},
                .sortOrder = type->sortOrder});
        }
    }
    std::sort(
        pending.begin(),
        pending.end(),
        [](const PendingAsset& left,
           const PendingAsset& right) {
            if (left.sortOrder != right.sortOrder) {
                return left.sortOrder < right.sortOrder;
            }
            if (left.view.displayName !=
                right.view.displayName) {
                return left.view.displayName <
                       right.view.displayName;
            }
            return left.view.path < right.view.path;
        });
    std::vector<engine::editor::WorkspaceAsset>
        assets;
    assets.reserve(pending.size());
    for (auto& asset : pending) {
        assets.push_back(std::move(asset.view));
    }
    return assets;
}

std::vector<engine::editor::WorkspaceHierarchyItem>
buildReadOnlyHierarchy(
    const engine::editor::ProjectDescriptor& descriptor,
    const engine::editor::EditorProjectStats& stats,
    std::string_view backendName) {
    using Item =
        engine::editor::WorkspaceHierarchyItem;
    using Property =
        engine::editor::WorkspaceProperty;
    const auto number = [](auto value) {
        return std::to_string(value);
    };
    return {
        Item{
            .id = descriptor.startupScene.assetId,
            .displayName =
                descriptor.scenes.empty()
                    ? descriptor.startupScene.assetId
                    : descriptor.scenes.front()
                          .displayName,
            .typeName = "Scene Root",
            .depth = 0,
            .properties = {
                Property{
                    "Asset id",
                    descriptor.startupScene.assetId},
                Property{
                    "Backing",
                    "Cooked .phscene"},
                Property{
                    "Scene nodes",
                    number(stats.sceneCount)},
                Property{
                    "Renderer",
                    std::string(backendName)},
            }},
        Item{
            .id = "environment/composition",
            .displayName = "Canonical environment",
            .typeName = "Scene Composition",
            .depth = 1,
            .properties = {
                Property{
                    "Materials",
                    number(stats.materialCount)},
                Property{
                    "Draw classes",
                    number(stats.drawClassCount)},
                Property{
                    "Visible triangles",
                    number(stats.visibleTriangleCount)},
            }},
        Item{
            .id = "environment/encounter-grass",
            .displayName = "Encounter grass",
            .typeName = "Instanced Vegetation",
            .depth = 1,
            .properties = {
                Property{
                    "Instances",
                    number(
                        stats.encounterGrassInstanceCount)},
                Property{
                    "Simulation",
                    "Wind-animated"},
                Property{
                    "Source",
                    "Cooked scene composition"},
            }},
        Item{
            .id = "environment/placed-vegetation",
            .displayName = "Placed vegetation",
            .typeName = "Instanced Vegetation",
            .depth = 1,
            .properties = {
                Property{
                    "Instances",
                    number(
                        stats.vegetationInstanceCount)},
                Property{
                    "Includes",
                    "Trees, shrubs, flowers, and ground vegetation"},
                Property{
                    "Source",
                    "Cooked scene composition"},
            }},
        Item{
            .id = "environment/projected-lighting",
            .displayName =
                "Projected lighting and shadows",
            .typeName = "Lighting Group",
            .depth = 1,
            .properties = {
                Property{
                    "Shadow triangles",
                    number(stats.shadowTriangleCount)},
                Property{
                    "Evaluation",
                    "Shared world renderer"},
            }},
    };
}

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
    struct ResolvedPlayConfiguration {
        const engine::editor::PlayConfiguration* configuration =
            nullptr;
        std::filesystem::path executable;
        std::filesystem::path workingDirectory;
    };
    std::vector<ResolvedPlayConfiguration> playConfigurations;
    std::vector<engine::editor::WorkspacePlayConfiguration>
        playConfigurationViews;
    std::vector<engine::editor::WorkspaceHierarchyItem>
        hierarchyViews;
    std::vector<engine::editor::WorkspaceAsset>
        assetViews;
    std::vector<engine::editor::WorkspaceScene> sceneViews;
    std::vector<engine::editor::WorkspaceGamePreview>
        gamePreviewViews;
    std::string activeGamePreviewId = "main-menu";
};

std::unique_ptr<LoadedProject> loadProject(
    const std::filesystem::path& requestedDescriptorPath,
    IRenderBackend& renderer,
    const Camera3D& camera,
    Camera3D& gameCamera,
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
    loaded->assetViews =
        discoverCookedAssets(
            loaded->root,
            loaded->descriptor);
    loaded->sceneViews.reserve(
        loaded->descriptor.scenes.size());
    for (const auto& scene : loaded->descriptor.scenes) {
        std::filesystem::path resolvedScenePath;
        if (!engine::editor::resolveScenePath(
                loaded->descriptorPath,
                loaded->descriptor,
                scene,
                resolvedScenePath,
                &outError)) {
            return nullptr;
        }
        loaded->sceneViews.push_back(
            engine::editor::WorkspaceScene{
                .assetId = scene.assetId,
                .displayName = scene.displayName,
                .category = scene.category,
                .kind = scene.kind,
                .path = resolvedScenePath.generic_string(),
                .previewId = scene.previewId,
                .startup =
                    scene.assetId ==
                    loaded->descriptor.startupScene.assetId,
                .properties = {
                    {
                        "Asset id",
                        scene.assetId,
                    },
                    {
                        "Scene kind",
                        scene.kind == "runtime_stage"
                            ? "Runtime stage"
                            : "Cooked world scene",
                    },
                    {
                        "Backing path",
                        resolvedScenePath.generic_string(),
                    },
                    {
                        "Game preview",
                        scene.previewId.empty()
                            ? "None"
                            : scene.previewId,
                    },
                    {
                        "Startup scene",
                        scene.assetId ==
                                loaded->descriptor
                                    .startupScene.assetId
                            ? "Yes"
                            : "No",
                    },
                }});
    }
    loaded->playConfigurations.reserve(
        loaded->descriptor.playConfigurations.size());
    loaded->playConfigurationViews.reserve(
        loaded->descriptor.playConfigurations.size());
    for (const auto& configuration :
         loaded->descriptor.playConfigurations) {
        std::filesystem::path executable;
        std::filesystem::path workingDirectory;
        if (!engine::editor::resolvePlayExecutablePath(
                loaded->descriptorPath,
                configuration,
                PHLOSION_EDITOR_BUILD_CONFIGURATION,
                executable,
                &outError) ||
            !engine::editor::resolvePlayWorkingDirectory(
                loaded->descriptorPath,
                configuration,
                PHLOSION_EDITOR_BUILD_CONFIGURATION,
                workingDirectory,
                &outError)) {
            return nullptr;
        }
        std::error_code executableError;
        std::error_code directoryError;
        const bool available =
            std::filesystem::is_regular_file(
                executable,
                executableError) &&
            !executableError &&
            std::filesystem::is_directory(
                workingDirectory,
                directoryError) &&
            !directoryError;
        loaded->playConfigurations.push_back(
            LoadedProject::ResolvedPlayConfiguration{
                .configuration = &configuration,
                .executable = executable,
                .workingDirectory = workingDirectory});
        loaded->playConfigurationViews.push_back(
            engine::editor::WorkspacePlayConfiguration{
                .id = configuration.id,
                .displayName = configuration.displayName,
                .group = configuration.group,
                .description = configuration.description,
                .executablePath =
                    executable.generic_string(),
                .available = available});
    }
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
    loaded->hierarchyViews =
        buildReadOnlyHierarchy(
            loaded->descriptor,
            loaded->runtime->stats(),
            "OpenGL 3.3 / project renderer plugin");
    const std::size_t gamePreviewCount =
        loaded->runtime->gamePreviewCount();
    loaded->gamePreviewViews.reserve(gamePreviewCount);
    for (std::size_t index = 0u;
         index < gamePreviewCount;
         ++index) {
        const auto preview =
            loaded->runtime->gamePreview(index);
        if (!preview.id || !preview.displayName) {
            continue;
        }
        loaded->gamePreviewViews.push_back(
            engine::editor::WorkspaceGamePreview{
                .id = preview.id,
                .displayName = preview.displayName,
                .group = preview.group ? preview.group : "",
                .description =
                    preview.description
                        ? preview.description
                        : ""});
    }
    if (!loaded->gamePreviewViews.empty()) {
        const engine::editor::EditorProjectGamePreviewContext
            gamePreviewContext{
                .renderer = &renderer,
                .camera = &gameCamera,
                .surfaceWidth = 1280,
                .surfaceHeight = 720};
        if (!loaded->runtime->initializeGamePreview(
                gamePreviewContext,
                &outError)) {
            return nullptr;
        }
    }
    loaded->status =
        loaded->runtime->status()
            ? loaded->runtime->status()
            : "Project loaded.";
    outError.clear();
    return loaded;
}

#if defined(_WIN32)
std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") ==
        std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0u;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2u + 1u, L'\\');
            quoted.push_back(L'"');
            backslashes = 0u;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0u;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2u, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring widenUtf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size);
    return result;
}
#endif

bool launchPlayConfiguration(
    const LoadedProject::ResolvedPlayConfiguration& resolved,
    std::string& outError) {
    if (!resolved.configuration) {
        outError = "Play configuration is missing.";
        return false;
    }
    if (!std::filesystem::is_regular_file(resolved.executable)) {
        outError =
            "Build the game executable first: " +
            resolved.executable.generic_string();
        return false;
    }
    if (!std::filesystem::is_directory(
            resolved.workingDirectory)) {
        outError =
            "Play configuration working directory is missing: " +
            resolved.workingDirectory.generic_string();
        return false;
    }

#if defined(_WIN32)
    struct SavedEnvironment {
        std::wstring name;
        std::wstring value;
        bool existed = false;
    };
    std::vector<SavedEnvironment> savedEnvironment;
    savedEnvironment.reserve(
        resolved.configuration->environment.size());
    for (const auto& variable :
         resolved.configuration->environment) {
        const std::wstring name = widenUtf8(variable.name);
        SetLastError(ERROR_SUCCESS);
        const DWORD required =
            GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        const bool existed =
            required != 0u ||
            GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        std::wstring previous;
        if (required > 0u) {
            previous.resize(required);
            const DWORD written = GetEnvironmentVariableW(
                name.c_str(),
                previous.data(),
                required);
            previous.resize(written);
        }
        savedEnvironment.push_back(
            SavedEnvironment{
                .name = name,
                .value = std::move(previous),
                .existed = existed});
        const std::wstring value = widenUtf8(variable.value);
        SetEnvironmentVariableW(name.c_str(), value.c_str());
    }

    const std::wstring executable =
        resolved.executable.wstring();
    std::wstring commandLine =
        quoteWindowsArgument(executable);
    for (const std::string& argument :
         resolved.configuration->arguments) {
        commandLine.push_back(L' ');
        commandLine += quoteWindowsArgument(
            widenUtf8(argument));
    }
    std::vector<wchar_t> writableCommandLine(
        commandLine.begin(),
        commandLine.end());
    writableCommandLine.push_back(L'\0');
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const std::wstring workingDirectory =
        resolved.workingDirectory.wstring();
    const BOOL created = CreateProcessW(
        executable.c_str(),
        writableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr,
        workingDirectory.c_str(),
        &startupInfo,
        &processInfo);
    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();

    for (auto it = savedEnvironment.rbegin();
         it != savedEnvironment.rend();
         ++it) {
        SetEnvironmentVariableW(
            it->name.c_str(),
            it->existed ? it->value.c_str() : nullptr);
    }
    if (!created) {
        outError =
            "Could not launch game view (Windows error " +
            std::to_string(createError) + "): " +
            resolved.executable.generic_string();
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
#else
    const pid_t processId = fork();
    if (processId < 0) {
        outError = "Could not fork the game view process.";
        return false;
    }
    if (processId == 0) {
        for (const auto& variable :
             resolved.configuration->environment) {
            setenv(
                variable.name.c_str(),
                variable.value.c_str(),
                1);
        }
        if (chdir(
                resolved.workingDirectory.string().c_str()) != 0) {
            _exit(126);
        }
        std::vector<std::string> argumentStorage;
        argumentStorage.reserve(
            resolved.configuration->arguments.size() + 1u);
        argumentStorage.push_back(
            resolved.executable.string());
        argumentStorage.insert(
            argumentStorage.end(),
            resolved.configuration->arguments.begin(),
            resolved.configuration->arguments.end());
        std::vector<char*> arguments;
        arguments.reserve(argumentStorage.size() + 1u);
        for (std::string& argument : argumentStorage) {
            arguments.push_back(argument.data());
        }
        arguments.push_back(nullptr);
        execv(
            resolved.executable.string().c_str(),
            arguments.data());
        _exit(127);
    }
#endif
    outError.clear();
    return true;
}

InputEvent::MouseButton mapEditorMouseButton(
    std::uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return InputEvent::MouseButton::Left;
        case SDL_BUTTON_MIDDLE:
            return InputEvent::MouseButton::Middle;
        case SDL_BUTTON_RIGHT:
            return InputEvent::MouseButton::Right;
        case SDL_BUTTON_X1:
            return InputEvent::MouseButton::X1;
        case SDL_BUTTON_X2:
            return InputEvent::MouseButton::X2;
        default:
            return InputEvent::MouseButton::Unknown;
    }
}

bool translateGamePreviewInput(
    const SDL_Event& event,
    float viewportScreenX,
    float viewportScreenY,
    int viewportWidth,
    int viewportHeight,
    bool viewportHovered,
    bool viewportFocused,
    InputEvent& out) {
    const auto localPosition =
        [&](int screenX, int screenY) {
            return std::pair<int, int>{
                std::clamp(
                    static_cast<int>(
                        static_cast<float>(screenX) -
                        viewportScreenX),
                    0,
                    std::max(0, viewportWidth - 1)),
                std::clamp(
                    static_cast<int>(
                        static_cast<float>(screenY) -
                        viewportScreenY),
                    0,
                    std::max(0, viewportHeight - 1))};
        };

    switch (event.type) {
        case SDL_KEYDOWN:
            if (!viewportFocused) {
                return false;
            }
            out = InputEvent::KeyDownEvent(
                engine::input::mapSdlKeyToEngineKey(
                    static_cast<int>(
                        event.key.keysym.sym)),
                event.key.repeat != 0);
            return true;
        case SDL_KEYUP:
            if (!viewportFocused) {
                return false;
            }
            out = InputEvent::KeyUpEvent(
                engine::input::mapSdlKeyToEngineKey(
                    static_cast<int>(
                        event.key.keysym.sym)));
            return true;
        case SDL_MOUSEMOTION: {
            if (!viewportHovered) {
                return false;
            }
            const auto [x, y] = localPosition(
                event.motion.x,
                event.motion.y);
            out = InputEvent::MouseMoveEvent(x, y);
            return true;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (!viewportHovered) {
                return false;
            }
            const auto [x, y] = localPosition(
                event.button.x,
                event.button.y);
            const auto button = mapEditorMouseButton(
                event.button.button);
            out =
                event.type == SDL_MOUSEBUTTONDOWN
                    ? InputEvent::MouseDownEvent(x, y, button)
                    : InputEvent::MouseUpEvent(x, y, button);
            return true;
        }
        case SDL_MOUSEWHEEL:
            if (!viewportHovered) {
                return false;
            }
            out = InputEvent::MouseWheelEvent(
                event.wheel.x,
                event.wheel.y);
            return true;
        default:
            return false;
    }
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
        Camera3D gameCamera(
            45.0f,
            16.0f / 9.0f,
            0.1f,
            100.0f);
        engine::editor::OpenGLEditorRenderSurface
            sceneSurface;
        engine::editor::OpenGLEditorRenderSurface
            gameSurface;

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
        engine::editor::EditorPlayState playState =
            engine::editor::EditorPlayState::Editing;
        engine::editor::EditorViewportKind activeViewport =
            engine::editor::EditorViewportKind::Scene;
        int editorViewportWidth = 960;
        int editorViewportHeight = 540;
        float editorViewportScreenX = 0.0f;
        float editorViewportScreenY = 0.0f;
        bool editorViewportHovered = false;
        bool editorViewportFocused = false;
        bool focusActiveViewport = true;
        float gameFixedAccumulator = 0.0f;
        bool commandLinePreviewApplied = false;
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();

        while (running) {
            const auto now = Clock::now();
            const float deltaSeconds = std::min(
                0.05f,
                std::chrono::duration<float>(
                    now - previous).count());
            previous = now;
            if (playState ==
                engine::editor::EditorPlayState::Playing) {
                simulationSeconds += deltaSeconds;
            }

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
                    activeViewport ==
                        engine::editor::EditorViewportKind::Game &&
                    playState !=
                        engine::editor::EditorPlayState::Editing) {
                    InputEvent gameInput;
                    if (translateGamePreviewInput(
                            event,
                            editorViewportScreenX,
                            editorViewportScreenY,
                            editorViewportWidth,
                            editorViewportHeight,
                            editorViewportHovered,
                            editorViewportFocused,
                            gameInput)) {
                        project->runtime->
                            handleGamePreviewInput(gameInput);
                    }
                }
                if (
                    project &&
                    activeViewport ==
                        engine::editor::EditorViewportKind::Scene &&
                    event.type == SDL_MOUSEWHEEL &&
                    editorViewportHovered) {
                    wheelDelta +=
                        static_cast<float>(event.wheel.y);
                } else if (
                    project &&
                    activeViewport ==
                        engine::editor::EditorViewportKind::Scene &&
                    event.type == SDL_MOUSEMOTION &&
                    editorViewportHovered) {
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
                        gameCamera,
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
                    playState =
                        engine::editor::EditorPlayState::Editing;
                    activeViewport =
                        engine::editor::EditorViewportKind::Scene;
                    focusActiveViewport = true;
                    gameFixedAccumulator = 0.0f;
                    if (!commandLinePreviewApplied &&
                        !arguments.gamePreview.empty()) {
                        commandLinePreviewApplied = true;
                        std::string previewError;
                        if (project->runtime->selectGamePreview(
                                arguments.gamePreview.c_str(),
                                &previewError)) {
                            project->activeGamePreviewId =
                                arguments.gamePreview;
                            activeViewport =
                                engine::editor::
                                    EditorViewportKind::Game;
                            focusActiveViewport = true;
                            project->status =
                                project->runtime->status()
                                    ? project->runtime->status()
                                    : "Embedded game preview selected.";
                        } else {
                            project->status =
                                "Command-line game preview selection failed: " +
                                previewError;
                            std::cerr
                                << "[Phlosion Editor] "
                                << project->status << '\n';
                        }
                    }
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
            if (project &&
                activeViewport ==
                    engine::editor::EditorViewportKind::Scene &&
                editorViewportFocused &&
                !editor.wantsKeyboardCapture()) {
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

            if (project &&
                project->runtime->gamePreviewReady() &&
                playState ==
                    engine::editor::EditorPlayState::Playing) {
                constexpr float fixedDelta = 1.0f / 60.0f;
                gameFixedAccumulator += deltaSeconds;
                int fixedTicks = 0;
                while (gameFixedAccumulator >= fixedDelta &&
                       fixedTicks < 4) {
                    project->runtime->
                        fixedUpdateGamePreview(fixedDelta);
                    gameFixedAccumulator -= fixedDelta;
                    ++fixedTicks;
                }
                if (fixedTicks == 4 &&
                    gameFixedAccumulator >= fixedDelta) {
                    gameFixedAccumulator = 0.0f;
                }
            }

            renderer.beginFrame(
                0.025f,
                0.031f,
                0.037f,
                1.0f);
            engine::editor::EditorShellActions actions;
            if (project) {
                for (std::size_t index = 0u;
                     index < project->playConfigurations.size();
                     ++index) {
                    std::error_code executableError;
                    std::error_code directoryError;
                    project->playConfigurationViews[index].available =
                        std::filesystem::is_regular_file(
                            project->playConfigurations[index]
                                .executable,
                            executableError) &&
                        !executableError &&
                        std::filesystem::is_directory(
                            project->playConfigurations[index]
                                .workingDirectory,
                            directoryError) &&
                        !directoryError;
                }
                project->runtime->update(simulationSeconds);
                const int surfaceWidth =
                    std::max(1, editorViewportWidth);
                const int surfaceHeight =
                    std::max(1, editorViewportHeight);
                if (activeViewport ==
                    engine::editor::EditorViewportKind::Scene) {
                    camera.setAspectRatio(
                        static_cast<float>(surfaceWidth) /
                        static_cast<float>(surfaceHeight));
                }
                const glm::mat4 viewProjection =
                    camera.getProjectionMatrix() *
                    camera.getViewMatrix();
                const glm::vec3 cameraPosition =
                    camera.getPosition();
                const glm::vec3 cameraForward =
                    camera.getDirection();
                const glm::vec3 cameraTarget =
                    camera.getTarget();
                const engine::editor::EditorProjectRenderContext
                    renderContext{
                        .renderer = &renderer,
                        .viewProjectionMatrix4x4 =
                            glm::value_ptr(viewProjection),
                        .surfaceWidth = surfaceWidth,
                        .surfaceHeight = surfaceHeight,
                        .cameraWorldPosition3 =
                            glm::value_ptr(cameraPosition),
                        .cameraForward3 =
                            glm::value_ptr(cameraForward),
                        .cameraTarget3 =
                            glm::value_ptr(cameraTarget)};
                if (activeViewport ==
                    engine::editor::EditorViewportKind::Scene) {
                    if (sceneSurface.begin(
                            surfaceWidth,
                            surfaceHeight)) {
                        renderer.beginWorldSceneColorPass(
                            surfaceWidth,
                            surfaceHeight);
                        project->runtime->render(renderContext);
                        renderer.endWorldSceneColorPass();
                        sceneSurface.end();
                    }
                } else if (gameSurface.begin(
                               surfaceWidth,
                               surfaceHeight)) {
                    gameCamera.setAspectRatio(
                        static_cast<float>(surfaceWidth) /
                        static_cast<float>(surfaceHeight));
                    project->runtime->renderGamePreview(
                        renderContext);
                    gameSurface.end();
                }

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
                    .playState = playState,
                    .simulationSeconds = simulationSeconds,
                    .playConfigurations =
                        &project->playConfigurationViews,
                    .hierarchyItems =
                        &project->hierarchyViews,
                    .assets = &project->assetViews,
                    .scenes = &project->sceneViews,
                    .gamePreviews =
                        &project->gamePreviewViews,
                    .activeGamePreviewId =
                        project->activeGamePreviewId,
                    .activeViewport = activeViewport,
                    .focusActiveViewport =
                        focusActiveViewport,
                    .sceneTextureId =
                        sceneSurface.textureId(),
                    .gameTextureId =
                        gameSurface.textureId(),
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
                if (!focusActiveViewport ||
                    actions.activeViewport ==
                        activeViewport) {
                    focusActiveViewport = false;
                    activeViewport = actions.activeViewport;
                }
                editorViewportWidth =
                    std::max(1, actions.viewportWidth);
                editorViewportHeight =
                    std::max(1, actions.viewportHeight);
                editorViewportScreenX =
                    actions.viewportScreenX;
                editorViewportScreenY =
                    actions.viewportScreenY;
                editorViewportHovered =
                    actions.viewportHovered;
                editorViewportFocused =
                    actions.viewportFocused;
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
                simulationSeconds = 0.0f;
                gameFixedAccumulator = 0.0f;
                playState =
                    engine::editor::EditorPlayState::Editing;
                window.setTitle("Phlosion Editor");
            }
            if (project && actions.togglePlay) {
                if (playState ==
                    engine::editor::EditorPlayState::Editing) {
                    simulationSeconds = 0.0f;
                    project->runtime->update(simulationSeconds);
                    playState =
                        engine::editor::EditorPlayState::Playing;
                    project->status =
                        activeViewport ==
                                engine::editor::EditorViewportKind::Game
                            ? "Embedded game preview started."
                            : "Scene simulation started.";
                } else {
                    playState =
                        engine::editor::EditorPlayState::Editing;
                    simulationSeconds = 0.0f;
                    gameFixedAccumulator = 0.0f;
                    project->runtime->update(simulationSeconds);
                    project->runtime->resetGamePreview();
                    project->status =
                        "Play mode stopped; scene and game preview restored.";
                }
            }
            if (project && actions.togglePause &&
                playState !=
                    engine::editor::EditorPlayState::Editing) {
                playState =
                    playState ==
                            engine::editor::EditorPlayState::Paused
                        ? engine::editor::EditorPlayState::Playing
                        : engine::editor::EditorPlayState::Paused;
                project->status =
                    playState ==
                            engine::editor::EditorPlayState::Paused
                        ? "Play mode paused."
                        : "Play mode resumed.";
            }
            if (project && actions.step &&
                playState ==
                    engine::editor::EditorPlayState::Paused) {
                simulationSeconds += 1.0f / 60.0f;
                project->runtime->update(simulationSeconds);
                if (activeViewport ==
                        engine::editor::EditorViewportKind::Game &&
                    project->runtime->gamePreviewReady()) {
                    project->runtime->fixedUpdateGamePreview(
                        1.0f / 60.0f);
                }
                project->status =
                    "Play mode advanced by one 60 Hz frame.";
            }
            if (project &&
                actions.selectGamePreviewIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.selectGamePreviewIndex) <
                    project->gamePreviewViews.size()) {
                const auto& preview =
                    project->gamePreviewViews[
                        static_cast<std::size_t>(
                            actions.selectGamePreviewIndex)];
                std::string previewError;
                if (project->runtime->selectGamePreview(
                        preview.id.c_str(),
                        &previewError)) {
                    project->activeGamePreviewId = preview.id;
                    activeViewport =
                        engine::editor::EditorViewportKind::Game;
                    focusActiveViewport = true;
                    gameFixedAccumulator = 0.0f;
                    project->status =
                        project->runtime->status()
                            ? project->runtime->status()
                            : "Embedded game preview selected.";
                } else {
                    project->status =
                        "Game preview selection failed: " +
                        previewError;
                    std::cerr
                        << "[Phlosion Editor] "
                        << project->status << '\n';
                }
            }
            if (project &&
                actions.openSceneIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.openSceneIndex) <
                    project->sceneViews.size()) {
                const auto& scene =
                    project->sceneViews[
                        static_cast<std::size_t>(
                            actions.openSceneIndex)];
                if (scene.kind != "runtime_stage") {
                    activeViewport =
                        engine::editor::
                            EditorViewportKind::Scene;
                    focusActiveViewport = true;
                    project->status =
                        "Opened cooked world scene: " +
                        scene.displayName + ".";
                } else {
                    const auto preview =
                        std::find_if(
                            project->gamePreviewViews.begin(),
                            project->gamePreviewViews.end(),
                            [&](const auto& candidate) {
                                return candidate.id ==
                                       scene.previewId;
                            });
                    if (preview ==
                        project->gamePreviewViews.end()) {
                        project->status =
                            "Runtime stage has no matching game preview: " +
                            scene.previewId;
                    } else {
                        std::string previewError;
                        if (project->runtime->
                                selectGamePreview(
                                    preview->id.c_str(),
                                    &previewError)) {
                            project->
                                activeGamePreviewId =
                                    preview->id;
                            activeViewport =
                                engine::editor::
                                    EditorViewportKind::Game;
                            focusActiveViewport = true;
                            gameFixedAccumulator = 0.0f;
                            project->status =
                                "Opened runtime stage: " +
                                scene.displayName +
                                " (warm game state).";
                        } else {
                            project->status =
                                "Runtime stage open failed: " +
                                previewError;
                        }
                    }
                }
            }
            if (project &&
                actions.launchPlayConfigurationIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.launchPlayConfigurationIndex) <
                    project->playConfigurations.size()) {
                const std::size_t configurationIndex =
                    static_cast<std::size_t>(
                        actions.launchPlayConfigurationIndex);
                std::string launchError;
                if (launchPlayConfiguration(
                        project->playConfigurations[
                            configurationIndex],
                        launchError)) {
                    project->status =
                        "Launched game view: " +
                        project->playConfigurationViews[
                            configurationIndex].displayName;
                } else {
                    project->status =
                        "Game view launch failed: " +
                        launchError;
                    std::cerr
                        << "[Phlosion Editor] "
                        << project->status << '\n';
                }
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
        sceneSurface.shutdown();
        gameSurface.shutdown();
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
