#define SDL_MAIN_HANDLED

#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "engine/editor/EditorProjectPlugin.h"
#include "engine/editor/D3D12EditorRenderSurface.h"
#include "engine/editor/EditorRenderSurface.h"
#include "engine/editor/EditorRendererPreference.h"
#include "engine/editor/EditorShell.h"
#include "engine/editor/OpenGLEditorRenderSurface.h"
#include "engine/editor/VulkanEditorRenderSurface.h"
#include "engine/editor/ProjectDescriptor.h"
#include "engine/input/SdlKeyMap.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

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
    std::string assetPreview;
    std::optional<int> assetPreviewAnimation;
    std::optional<
        engine::editor::EditorRendererPreference>
        rendererPreference;
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
        constexpr std::string_view assetPreviewPrefix =
            "--asset-preview=";
        constexpr std::string_view
            assetPreviewAnimationPrefix =
                "--asset-preview-animation=";
        constexpr std::string_view framesPrefix = "--frames=";
        constexpr std::string_view rendererPrefix =
            "--renderer=";
        if (argument.rfind(projectPrefix, 0u) == 0u) {
            result.project = argument.substr(projectPrefix.size());
        } else if (
            argument.rfind(gamePreviewPrefix, 0u) == 0u) {
            result.gamePreview =
                argument.substr(gamePreviewPrefix.size());
        } else if (
            argument.rfind(assetPreviewPrefix, 0u) == 0u) {
            result.assetPreview =
                argument.substr(assetPreviewPrefix.size());
        } else if (
            argument.rfind(
                assetPreviewAnimationPrefix,
                0u) == 0u) {
            const std::string value =
                argument.substr(
                    assetPreviewAnimationPrefix.size());
            result.assetPreviewAnimation =
                value == "bind" || value == "Bind"
                    ? -1
                    : std::stoi(value);
        } else if (argument.rfind(framesPrefix, 0u) == 0u) {
            result.frameLimit = std::max(
                1,
                std::stoi(argument.substr(framesPrefix.size())));
        } else if (
            argument.rfind(rendererPrefix, 0u) == 0u) {
            result.rendererPreference =
                engine::editor::
                    parseEditorRendererPreference(
                        argument.substr(
                            rendererPrefix.size()));
        } else if (!argument.empty() && argument.front() != '-') {
            result.project = argument;
        }
    }
    return result;
}

bool containsInsensitive(
    std::string_view text,
    std::string_view token) {
    if (token.empty()) {
        return true;
    }
    return std::search(
               text.begin(),
               text.end(),
               token.begin(),
               token.end(),
               [](char left, char right) {
                   return std::tolower(
                              static_cast<unsigned char>(left)) ==
                          std::tolower(
                              static_cast<unsigned char>(right));
               }) != text.end();
}

std::string humanizeIdentifier(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    if (!value.empty()) {
        value.front() = static_cast<char>(
            std::toupper(
                static_cast<unsigned char>(value.front())));
    }
    return value;
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

engine::editor::EditorRendererPreference
loadRendererPreference(
    const std::filesystem::path& stateDirectory) {
    std::ifstream input(
        stateDirectory / "renderer.txt");
    std::string value;
    if (input >> value) {
        return engine::editor::
            parseEditorRendererPreference(value);
    }
    return engine::editor::
        EditorRendererPreference::Auto;
}

void saveRendererPreference(
    const std::filesystem::path& stateDirectory,
    engine::editor::EditorRendererPreference
        preference) {
    std::error_code error;
    std::filesystem::create_directories(
        stateDirectory,
        error);
    std::ofstream output(
        stateDirectory / "renderer.txt",
        std::ios::trunc);
    output
        << engine::editor::
               editorRendererPreferenceName(preference)
        << '\n';
}

struct EditorGraphics {
    std::unique_ptr<Window> window;
    std::unique_ptr<IRenderBackend> renderer;
    engine::editor::EditorRendererPreference
        activePreference =
            engine::editor::
                EditorRendererPreference::OpenGL;
    std::string fallbackReason;
};

EditorGraphics createEditorGraphics(
    engine::editor::EditorRendererPreference requested) {
    using engine::editor::EditorRendererPreference;
    const auto create =
        [](EditorRendererPreference preference) {
            EditorGraphics graphics;
            if (preference ==
                EditorRendererPreference::D3D12) {
#if defined(_WIN32)
                graphics.window =
                    std::make_unique<Window>(
                        "Phlosion Editor",
                        1440,
                        900,
                        Window::GraphicsApi::Native,
                        true);
                int width = 0;
                int height = 0;
                graphics.window->getDrawableSize(
                    width,
                    height);
                graphics.renderer =
                    std::make_unique<
                        D3D12RenderBackend>(
                        graphics.window->
                            getSDLWindow(),
                        std::max(1, width),
                        std::max(1, height),
                        true);
                graphics.activePreference =
                    EditorRendererPreference::D3D12;
                return graphics;
#else
                throw std::runtime_error(
                    "D3D12 is only available on Windows.");
#endif
            }
            if (preference ==
                EditorRendererPreference::Vulkan) {
                graphics.window =
                    std::make_unique<Window>(
                        "Phlosion Editor",
                        1440,
                        900,
                        Window::GraphicsApi::Vulkan,
                        true);
                int width = 0;
                int height = 0;
                graphics.window->getDrawableSize(
                    width,
                    height);
                graphics.renderer =
                    std::make_unique<
                        VulkanRenderBackend>(
                        graphics.window->
                            getSDLWindow(),
                        std::max(1, width),
                        std::max(1, height),
                        true);
                graphics.activePreference =
                    EditorRendererPreference::Vulkan;
                return graphics;
            }
            graphics.window =
                std::make_unique<Window>(
                    "Phlosion Editor",
                    1440,
                    900,
                    Window::GraphicsApi::OpenGL,
                    true);
            if (!gladLoadGLLoader(
                    reinterpret_cast<GLADloadproc>(
                        SDL_GL_GetProcAddress))) {
                throw std::runtime_error(
                    "Failed to initialize GLAD.");
            }
            graphics.renderer =
                std::make_unique<
                    OpenGLRenderBackend>();
            graphics.activePreference =
                EditorRendererPreference::OpenGL;
            return graphics;
        };

    try {
        return create(requested);
    } catch (const std::exception& exception) {
        if (requested ==
            EditorRendererPreference::OpenGL) {
            throw;
        }
        EditorGraphics fallback =
            create(EditorRendererPreference::OpenGL);
        fallback.fallbackReason =
            std::string(
                "Requested editor renderer failed (") +
            exception.what() +
            "); using OpenGL compatibility mode.";
        return fallback;
    }
}

std::unique_ptr<engine::editor::EditorRenderSurface>
createEditorRenderSurface(
    IRenderBackend& renderer,
    engine::editor::EditorShell& editor) {
    if (std::string_view(
            renderer.backendId()
                ? renderer.backendId()
                : "") == "d3d12") {
#if defined(_WIN32)
        auto* d3d12 =
            dynamic_cast<D3D12RenderBackend*>(
                &renderer);
        engine::editor::EditorTextureDescriptor
            descriptor;
        if (!d3d12 ||
            !editor.allocateTextureDescriptor(
                descriptor)) {
            throw std::runtime_error(
                "Could not allocate a D3D12 editor surface descriptor.");
        }
        return std::make_unique<
            engine::editor::
                D3D12EditorRenderSurface>(
                    *d3d12,
                    descriptor);
#else
        throw std::runtime_error(
            "D3D12 editor surfaces are only available on Windows.");
#endif
    }
    if (std::string_view(
            renderer.backendId()
                ? renderer.backendId()
                : "") == "vulkan") {
        auto* vulkan =
            dynamic_cast<VulkanRenderBackend*>(
                &renderer);
        if (!vulkan) {
            throw std::runtime_error(
                "Could not resolve the Vulkan editor renderer.");
        }
        return std::make_unique<
            engine::editor::
                VulkanEditorRenderSurface>(*vulkan);
    }
    return std::make_unique<
        engine::editor::OpenGLEditorRenderSurface>();
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

std::string prefabDependencySummary(
    const std::filesystem::path& prefabPath) {
    std::size_t meshes = 0u;
    std::size_t materials = 0u;
    std::size_t animations = 0u;
    std::size_t skeletons = 0u;
    std::size_t textures = 0u;
    std::error_code iteratorError;
    std::filesystem::recursive_directory_iterator iterator(
        prefabPath.parent_path(),
        std::filesystem::directory_options::
            skip_permission_denied,
        iteratorError);
    const std::filesystem::recursive_directory_iterator end;
    while (!iteratorError && iterator != end) {
        const auto entry = *iterator;
        iterator.increment(iteratorError);
        std::error_code typeError;
        if (!entry.is_regular_file(typeError) || typeError) {
            continue;
        }
        std::string extension =
            entry.path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(
                    std::tolower(character));
            });
        meshes += extension == ".phmesh" ? 1u : 0u;
        materials += extension == ".phmat" ? 1u : 0u;
        animations += extension == ".phanim" ? 1u : 0u;
        skeletons += extension == ".phskel" ? 1u : 0u;
        textures += extension == ".ktx2" ? 1u : 0u;
    }
    return std::to_string(meshes) + " mesh, " +
           std::to_string(materials) + " material, " +
           std::to_string(animations) + " animation set, " +
           std::to_string(skeletons) + " skeleton, " +
           std::to_string(textures) + " textures";
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

std::optional<std::string> cookedPrefabKind(
    const std::filesystem::path& path) {
    engine::assets::phrc::ManifestInspection inspection;
    if (!engine::assets::phrc::inspectFileManifest(
            path,
            inspection,
            nullptr) ||
        inspection.magic != engine::assets::phrc::magic("PHLO")) {
        return std::nullopt;
    }
    try {
        const nlohmann::json manifest =
            nlohmann::json::parse(inspection.manifestJson);
        if (manifest.value("root_type", std::string{}) !=
            "Prefab") {
            return std::nullopt;
        }
        const std::string kind =
            manifest.value("prefab_kind", std::string{});
        return kind.empty()
            ? std::nullopt
            : std::optional<std::string>(kind);
    } catch (...) {
        return std::nullopt;
    }
}

CookedAssetType cookedPrefabAssetType(
    std::string_view kind) {
    if (kind == "Character" ||
        kind == "CharacterPrefab" ||
        kind == "Pokemon") {
        return CookedAssetType{
            "Character Prefab", "Character Prefabs", 1};
    }
    if (kind == "Object" ||
        kind == "ObjectPrefab" ||
        kind == "RuntimeObject") {
        return CookedAssetType{
            "Object Prefab", "Object Prefabs", 2};
    }
    if (kind == "Environment" ||
        kind == "EnvironmentPrefab" ||
        kind == "LgpeEnvironment") {
        return CookedAssetType{
            "Environment Prefab", "Environment Prefabs", 3};
    }
    return CookedAssetType{
        "Prefab", "Prefabs", 4};
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

bool wildcardPathMatch(
    std::string_view pattern,
    std::string_view path) {
    std::size_t patternIndex = 0u;
    std::size_t pathIndex = 0u;
    std::size_t starIndex =
        std::string_view::npos;
    std::size_t starPathIndex = 0u;
    while (pathIndex < path.size()) {
        if (patternIndex < pattern.size() &&
            (pattern[patternIndex] == '?' ||
             pattern[patternIndex] ==
                 path[pathIndex])) {
            ++patternIndex;
            ++pathIndex;
            continue;
        }
        if (patternIndex < pattern.size() &&
            pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            starPathIndex = pathIndex;
            continue;
        }
        if (starIndex != std::string_view::npos) {
            patternIndex = starIndex + 1u;
            pathIndex = ++starPathIndex;
            continue;
        }
        return false;
    }
    while (patternIndex < pattern.size() &&
           pattern[patternIndex] == '*') {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

bool hiddenFromAssetBrowser(
    const engine::editor::ContentMount& mount,
    std::string_view mountRelativePath) {
    return std::any_of(
        mount.assetBrowserExcludePatterns.begin(),
        mount.assetBrowserExcludePatterns.end(),
        [&](const std::string& pattern) {
            return wildcardPathMatch(
                pattern,
                mountRelativePath);
        });
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
            const auto discoveredType =
                cookedAssetType(
                    entry.path().extension().string());
            if (!discoveredType) {
                continue;
            }
            if (discoveredType->typeName !=
                    std::string_view("Prefab") &&
                discoveredType->typeName !=
                    std::string_view("World Scene")) {
                continue;
            }
            const auto prefabKind =
                entry.path().extension() == ".phlo"
                ? cookedPrefabKind(entry.path())
                : std::nullopt;
            const CookedAssetType type =
                prefabKind
                ? cookedPrefabAssetType(*prefabKind)
                : *discoveredType;
            std::error_code mountRelativeError;
            const std::string mountRelativePath =
                std::filesystem::relative(
                    entry.path(),
                    mountRoot,
                    mountRelativeError)
                    .generic_string();
            if (mountRelativeError ||
                hiddenFromAssetBrowser(
                    mount,
                    mountRelativePath)) {
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
                                type),
                        .typeName = type.typeName,
                        .category = type.category,
                        .path = projectPath,
                        .previewable3d =
                            entry.path().extension() ==
                            ".phlo",
                        .properties = {
                            {"Asset type", type.typeName},
                            {"Prefab kind",
                             prefabKind
                                 ? *prefabKind
                                 : "Not applicable"},
                            {"Contains",
                             entry.path().extension() ==
                                     ".phlo"
                                 ? prefabDependencySummary(
                                       entry.path())
                                 : "Cooked world hierarchy"},
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
                .sortOrder = type.sortOrder});
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

void appendProjectAssets(
    engine::editor::IEditorProjectRuntime& runtime,
    std::vector<engine::editor::WorkspaceAsset>& assets) {
    const std::size_t projectAssetCount =
        runtime.assetCount();
    assets.reserve(assets.size() + projectAssetCount);
    for (std::size_t index = 0u;
         index < projectAssetCount;
         ++index) {
        const auto asset = runtime.asset(index);
        if (!asset.id || !asset.displayName ||
            !asset.typeName || !asset.path) {
            continue;
        }
        const std::string description =
            asset.description ? asset.description : "";
        assets.push_back(
            engine::editor::WorkspaceAsset{
                .id = asset.id,
                .displayName = asset.displayName,
                .typeName = asset.typeName,
                .category =
                    asset.category
                        ? asset.category
                        : "Project Assets",
                .path = asset.path,
                .previewable3d = asset.previewable,
                .sceneInstantiable =
                    asset.sceneInstantiable,
                .properties = {
                    {"Asset type", asset.typeName},
                    {"Contains",
                     description.empty()
                         ? "Project-defined runtime asset"
                         : description},
                    {"Format", "Project runtime"},
                    {"Project path", asset.path},
                }});
    }
    std::stable_sort(
        assets.begin(),
        assets.end(),
        [](const engine::editor::WorkspaceAsset& left,
           const engine::editor::WorkspaceAsset& right) {
            if (left.category != right.category) {
                return left.category < right.category;
            }
            if (left.displayName != right.displayName) {
                return left.displayName < right.displayName;
            }
            return left.path < right.path;
        });
}

std::vector<engine::editor::WorkspaceHierarchyItem>
buildReadOnlyHierarchy(
    const engine::editor::WorkspaceScene& scene,
    const engine::editor::EditorProjectStats& stats,
    std::string_view backendName) {
    using Item =
        engine::editor::WorkspaceHierarchyItem;
    using Property =
        engine::editor::WorkspaceProperty;
    const auto number = [](auto value) {
        return std::to_string(value);
    };
    std::vector<Item> hierarchy{
        Item{
            .id = scene.id,
            .displayName = scene.displayName,
            .typeName = "Game Scene",
            .depth = 0,
            .folder = true,
            .expandedByDefault = true,
            .properties = {
                Property{
                    "Scene id",
                    scene.id},
                Property{
                    "Status",
                    scene.status},
                Property{
                    "Runtime source",
                    scene.runtimePath.empty()
                        ? "Engine/runtime snapshots"
                        : scene.runtimePath},
                Property{
                    "Environment",
                    scene.environmentDisplayName},
                Property{
                    "Environment kind",
                    scene.environmentKind},
                Property{
                    "Authored scene",
                    scene.authoredScenePath.empty()
                        ? "No project-owned overlay"
                        : scene.authoredScenePath},
                Property{
                    "Renderer",
                    std::string(backendName)},
            }}};
    hierarchy.push_back(
        Item{
            .id = scene.environmentAssetId,
            .displayName = scene.environmentDisplayName,
            .typeName = "Environment Backdrop",
            .depth = 1,
            .folder = true,
            .expandedByDefault = true,
            .properties = {
                Property{
                    "Asset id",
                    scene.environmentAssetId},
                Property{
                    "Kind",
                    scene.environmentKind},
                Property{
                    "Backing",
                    scene.path.empty()
                        ? "Generated by the game runtime"
                        : scene.path},
                Property{
                    "Scene nodes",
                    number(stats.sceneCount)},
            }});
    if (scene.path.empty()) {
        hierarchy.push_back(
            Item{
                .id = scene.id + "/runtime-preview",
                .displayName =
                    "Runtime backdrop preview",
                .typeName = "Game View Adapter",
                .depth = 2,
                .properties = {
                    Property{
                        "Scene view",
                        "No cooked environment adapter yet"},
                    Property{
                        "Game view",
                        "Available through associated previews"},
                }});
        return hierarchy;
    }
    hierarchy.push_back(
        Item{
            .id = "environment/composition",
            .displayName = "Canonical environment",
            .typeName = "Scene Composition",
            .depth = 2,
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
            }});
    hierarchy.push_back(
        Item{
            .id = "environment/encounter-grass",
            .displayName = "Encounter grass",
            .typeName = "Instanced Vegetation",
            .depth = 2,
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
            }});
    hierarchy.push_back(
        Item{
            .id = "environment/placed-vegetation",
            .displayName = "Placed vegetation",
            .typeName = "Instanced Vegetation",
            .depth = 2,
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
            }});
    hierarchy.push_back(
        Item{
            .id = "environment/projected-lighting",
            .displayName =
                "Projected lighting and shadows",
            .typeName = "Lighting Group",
            .depth = 2,
            .properties = {
                Property{
                    "Shadow triangles",
                    number(stats.shadowTriangleCount)},
                Property{
                    "Evaluation",
                    "Shared world renderer"},
            }});
    return hierarchy;
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
    std::vector<engine::editor::WorkspaceLayoutObject>
        layoutObjectViews;
    std::vector<engine::editor::WorkspaceAsset>
        assetViews;
    std::size_t cookedAssetViewCount = 0u;
    std::vector<engine::editor::WorkspaceScene> sceneViews;
    std::vector<engine::editor::WorkspaceGamePreview>
        gamePreviewViews;
    std::size_t activeSceneIndex = 0u;
    std::vector<engine::editor::WorkspaceAssetAnimation>
        assetPreviewAnimations;
    engine::editor::WorkspaceAssetPreview
        assetPreviewView;
    std::string activeGamePreviewId = "main-menu";
};

void refreshLayoutObjectViews(LoadedProject& project) {
    project.layoutObjectViews.clear();
    const std::size_t count =
        project.runtime->layoutObjectCount();
    project.layoutObjectViews.reserve(count);
    for (std::size_t index = 0u;
         index < count;
         ++index) {
        const auto object =
            project.runtime->layoutObject(index);
        if (!object.stableId ||
            !object.displayName) {
            continue;
        }
        project.layoutObjectViews.push_back(
            engine::editor::WorkspaceLayoutObject{
                .stableId = object.stableId,
                .displayName = object.displayName,
                .typeName =
                    object.typeName
                        ? object.typeName
                        : "Layout Object",
                .coordinateSystem =
                    object.coordinateSystem
                        ? object.coordinateSystem
                        : "Project units",
                .reason =
                    object.reason
                        ? object.reason
                        : "",
                .targetKind =
                    object.targetKind
                        ? object.targetKind
                        : "",
                .categoryPath =
                    object.categoryPath
                        ? object.categoryPath
                        : "Environment/Objects",
                .prefabAssetId =
                    object.prefabAssetId
                        ? object.prefabAssetId
                        : "",
                .sourceTranslation =
                    object.sourceTranslation,
                .sourceRotationDegrees =
                    object.sourceRotationDegrees,
                .sourceScale =
                    object.sourceScale,
                .translation =
                    object.translation,
                .rotationDegrees =
                    object.rotationDegrees,
                .scale = object.scale,
                .viewportPosition =
                    object.viewportPosition,
                .viewportAxisDirections =
                    object.viewportAxisDirections,
                .viewportSourceUnitsPerPixel =
                    object.viewportSourceUnitsPerPixel,
                .viewportVisible =
                    object.viewportVisible,
                .suppressed =
                    object.suppressed,
                .hasOverride =
                    object.hasOverride});
    }
}

bool naturalNameLess(
    std::string_view left,
    std::string_view right) {
    std::size_t leftIndex = 0u;
    std::size_t rightIndex = 0u;
    while (leftIndex < left.size() &&
           rightIndex < right.size()) {
        const unsigned char leftCharacter =
            static_cast<unsigned char>(left[leftIndex]);
        const unsigned char rightCharacter =
            static_cast<unsigned char>(right[rightIndex]);
        if (std::isdigit(leftCharacter) &&
            std::isdigit(rightCharacter)) {
            std::size_t leftEnd = leftIndex;
            std::size_t rightEnd = rightIndex;
            while (leftEnd < left.size() &&
                   std::isdigit(
                       static_cast<unsigned char>(
                           left[leftEnd]))) {
                ++leftEnd;
            }
            while (rightEnd < right.size() &&
                   std::isdigit(
                       static_cast<unsigned char>(
                           right[rightEnd]))) {
                ++rightEnd;
            }
            std::size_t leftDigits = leftIndex;
            std::size_t rightDigits = rightIndex;
            while (leftDigits < leftEnd &&
                   left[leftDigits] == '0') {
                ++leftDigits;
            }
            while (rightDigits < rightEnd &&
                   right[rightDigits] == '0') {
                ++rightDigits;
            }
            const std::size_t leftLength =
                leftEnd - leftDigits;
            const std::size_t rightLength =
                rightEnd - rightDigits;
            if (leftLength != rightLength) {
                return leftLength < rightLength;
            }
            const int numericComparison =
                left.substr(leftDigits, leftLength).compare(
                    right.substr(rightDigits, rightLength));
            if (numericComparison != 0) {
                return numericComparison < 0;
            }
            const std::size_t leftRunLength =
                leftEnd - leftIndex;
            const std::size_t rightRunLength =
                rightEnd - rightIndex;
            if (leftRunLength != rightRunLength) {
                return leftRunLength < rightRunLength;
            }
            leftIndex = leftEnd;
            rightIndex = rightEnd;
            continue;
        }
        const unsigned char leftFolded =
            static_cast<unsigned char>(
                std::tolower(leftCharacter));
        const unsigned char rightFolded =
            static_cast<unsigned char>(
                std::tolower(rightCharacter));
        if (leftFolded != rightFolded) {
            return leftFolded < rightFolded;
        }
        ++leftIndex;
        ++rightIndex;
    }
    if (leftIndex != left.size() ||
        rightIndex != right.size()) {
        return leftIndex == left.size();
    }
    return left < right;
}

void rebuildProjectHierarchy(
    LoadedProject& project,
    const engine::editor::WorkspaceScene& scene,
    const engine::editor::EditorProjectStats& stats,
    std::string_view backendName) {
    project.hierarchyViews =
        buildReadOnlyHierarchy(
            scene,
            stats,
            backendName);
    if (project.layoutObjectViews.empty()) {
        return;
    }
    // Replace the old diagnostic pseudo-groups with a real project object
    // tree. The scene and environment roots from buildReadOnlyHierarchy()
    // remain, while project adapters provide semantic folder paths.
    if (project.hierarchyViews.size() > 2u) {
        project.hierarchyViews.resize(2u);
    }

    struct FolderNode {
        std::string name;
        std::string path;
        std::vector<std::unique_ptr<FolderNode>> children;
        std::vector<std::size_t> objects;
    };
    FolderNode root{
        .name = "Environment",
        .path = "Environment"};
    const auto splitPath =
        [](std::string_view path) {
            std::vector<std::string> segments;
            std::size_t start = 0u;
            while (start < path.size()) {
                const std::size_t slash =
                    path.find('/', start);
                const std::size_t end =
                    slash == std::string_view::npos
                    ? path.size()
                    : slash;
                if (end > start) {
                    segments.emplace_back(
                        path.substr(start, end - start));
                }
                if (slash == std::string_view::npos) {
                    break;
                }
                start = slash + 1u;
            }
            return segments;
        };
    for (std::size_t index = 0u;
         index < project.layoutObjectViews.size();
         ++index) {
        const auto& object =
            project.layoutObjectViews[index];
        auto segments =
            splitPath(object.categoryPath);
        FolderNode* folder = &root;
        std::size_t first =
            !segments.empty() &&
                    segments.front() == "Environment"
            ? 1u
            : 0u;
        for (std::size_t segmentIndex = first;
             segmentIndex < segments.size();
             ++segmentIndex) {
            const auto found = std::find_if(
                folder->children.begin(),
                folder->children.end(),
                [&](const std::unique_ptr<FolderNode>& child) {
                    return child->name ==
                        segments[segmentIndex];
                });
            if (found != folder->children.end()) {
                folder = found->get();
                continue;
            }
            auto child =
                std::make_unique<FolderNode>();
            child->name = segments[segmentIndex];
            child->path =
                folder->path + "/" + child->name;
            folder->children.push_back(
                std::move(child));
            folder = folder->children.back().get();
        }
        folder->objects.push_back(index);
    }

    const auto folderRank =
        [](std::string_view name) {
            if (name == "Terrain") return 0;
            if (name == "Vegetation") return 1;
            if (name == "Props") return 2;
            if (name == "Lighting") return 3;
            return 10;
        };
    const auto objectCount =
        [](const auto& self,
           const FolderNode& folder) -> std::size_t {
            std::size_t count = folder.objects.size();
            for (const auto& child : folder.children) {
                count += self(self, *child);
            }
            return count;
        };
    const auto appendFolder =
        [&](const auto& self,
            FolderNode& folder,
            int depth) -> void {
            std::sort(
                folder.children.begin(),
                folder.children.end(),
                [&](const auto& left,
                    const auto& right) {
                    const int leftRank =
                        folderRank(left->name);
                    const int rightRank =
                        folderRank(right->name);
                    return leftRank != rightRank
                        ? leftRank < rightRank
                        : naturalNameLess(
                              left->name,
                              right->name);
                });
            for (auto& child : folder.children) {
                project.hierarchyViews.push_back(
                    engine::editor::WorkspaceHierarchyItem{
                        .id = "folder/" + child->path,
                        .displayName = child->name,
                        .typeName = "Scene Folder",
                        .depth = depth,
                        .folder = true,
                        .expandedByDefault =
                            depth <= 3,
                        .properties = {
                            {"Path", child->path},
                            {"Objects",
                             std::to_string(
                                 objectCount(
                                     objectCount,
                                     *child))},
                        }});
                self(self, *child, depth + 1);
            }
            std::sort(
                folder.objects.begin(),
                folder.objects.end(),
                [&](std::size_t left,
                    std::size_t right) {
                    return naturalNameLess(
                        project.layoutObjectViews[left]
                            .displayName,
                        project.layoutObjectViews[right]
                            .displayName);
                });
            for (const std::size_t index :
                 folder.objects) {
                const auto& object =
                    project.layoutObjectViews[index];
                project.hierarchyViews.push_back(
                    engine::editor::WorkspaceHierarchyItem{
                        .id = object.stableId,
                        .displayName =
                            object.displayName +
                            (object.hasOverride
                                 ? "  [override]"
                                 : ""),
                        .typeName = object.typeName,
                        .depth = depth,
                        .layoutObjectIndex =
                            static_cast<int>(index),
                        .properties = {
                            {"Stable source target",
                             object.stableId},
                            {"Prefab asset",
                             object.prefabAssetId.empty()
                                 ? "Source mesh group"
                                 : object.prefabAssetId},
                            {"State",
                             object.hasOverride
                                 ? (object.suppressed
                                        ? "Suppressed by layout"
                                        : "Transformed by layout")
                                 : "Canonical"},
                            {"Reason",
                             object.reason.empty()
                                 ? "None"
                                 : object.reason},
                        }});
            }
        };
    appendFolder(appendFolder, root, 2);
    project.hierarchyViews.push_back(
        engine::editor::WorkspaceHierarchyItem{
            .id = "environment/lighting",
            .displayName = "Lighting and Atmosphere",
            .typeName = "Scene Folder",
            .depth = 2,
            .folder = true,
            .expandedByDefault = false,
            .properties = {
                {"Projected shadow triangles",
                 std::to_string(
                     stats.shadowTriangleCount)},
                {"Editing",
                 "Scene-level lighting controls are a later component adapter"},
            }});
}

void refreshAssetPreviewView(LoadedProject& project) {
    const auto info =
        project.runtime->assetPreviewInfo();
    project.assetPreviewAnimations.clear();
    project.assetPreviewAnimations.reserve(
        info.animationCount);
    for (std::size_t index = 0u;
         index < info.animationCount;
         ++index) {
        const auto animation =
            project.runtime->assetPreviewAnimation(index);
        project.assetPreviewAnimations.push_back(
            engine::editor::WorkspaceAssetAnimation{
                .name =
                    animation.name
                        ? animation.name
                        : "Unnamed clip",
                .durationSeconds =
                    animation.durationSeconds});
    }
    project.assetPreviewView =
        engine::editor::WorkspaceAssetPreview{
            .kind =
                info.kind ==
                        engine::editor::
                            EditorProjectAssetPreviewKind::
                                VisualEffect
                    ? engine::editor::
                          WorkspaceAssetPreviewKind::
                              VisualEffect
                    : engine::editor::
                          WorkspaceAssetPreviewKind::Model,
            .assetId =
                info.assetId ? info.assetId : "",
            .status =
                info.status ? info.status : "",
            .vertexCount = info.vertexCount,
            .triangleCount = info.triangleCount,
            .materialCount = info.materialCount,
            .textureCount = info.textureCount,
            .boneCount = info.boneCount,
            .activeElementCount =
                info.activeElementCount,
            .animationIndex = info.animationIndex,
            .animationTimeSeconds =
                info.animationTimeSeconds,
            .animationDurationSeconds =
                info.animationDurationSeconds,
            .boundsRadius = info.boundsRadius,
            .boundsCenterY = info.boundsCenterY,
            .ready = info.ready,
            .animations =
                &project.assetPreviewAnimations};
}

void resetAssetPreviewCamera(
    Camera3D& camera,
    const engine::editor::WorkspaceAssetPreview&
        preview) {
    const float radius =
        std::clamp(preview.boundsRadius, 0.45f, 6.0f);
    const glm::vec3 target(
        0.0f,
        preview.boundsCenterY,
        0.0f);
    camera.lookAt(target);
    camera.setPosition(
        target +
        glm::vec3(
            radius * 1.35f,
            radius * 0.65f,
            radius * 2.65f));
}

bool selectAssetPreview(
    LoadedProject& project,
    int assetIndex,
    Camera3D& camera,
    int& selectedAssetPreviewIndex,
    std::string& outError) {
    if (assetIndex < 0 ||
        static_cast<std::size_t>(assetIndex) >=
            project.assetViews.size()) {
        outError = "Asset index is out of range.";
        return false;
    }
    const auto& asset =
        project.assetViews[static_cast<std::size_t>(
            assetIndex)];
    if (!asset.previewable3d) {
        outError =
            "Asset does not provide an inspector preview: " +
            asset.displayName;
        return false;
    }
    const bool virtualPath =
        asset.path.find("://") != std::string::npos;
    const std::string resolvedAssetPath =
        virtualPath
            ? asset.path
            : (project.root / asset.path)
                  .lexically_normal()
                  .string();
    if (!project.runtime->selectAssetPreview(
            asset.id.c_str(),
            resolvedAssetPath.c_str(),
            &outError)) {
        return false;
    }
    selectedAssetPreviewIndex = assetIndex;
    refreshAssetPreviewView(project);
    resetAssetPreviewCamera(camera, project.assetPreviewView);
    project.status =
        "Previewing " + asset.typeName + ": " +
        asset.displayName + ".";
    return true;
}

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
    loaded->cookedAssetViewCount =
        loaded->assetViews.size();
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
        std::filesystem::path resolvedAuthoredScenePath;
        if (!engine::editor::resolveAuthoredScenePath(
                loaded->descriptorPath,
                scene,
                resolvedAuthoredScenePath,
                &outError)) {
            return nullptr;
        }
        const auto environment = std::find_if(
            loaded->descriptor.environments.begin(),
            loaded->descriptor.environments.end(),
            [&](const engine::editor::ProjectEnvironment&
                    candidate) {
                return candidate.assetId ==
                       scene.environmentAssetId;
            });
        if (environment ==
            loaded->descriptor.environments.end()) {
            outError =
                "Scene references an unknown environment: " +
                scene.environmentAssetId;
            return nullptr;
        }
        loaded->sceneViews.push_back(
            engine::editor::WorkspaceScene{
                .id = scene.sceneId,
                .displayName = scene.displayName,
                .category = scene.category,
                .environmentAssetId =
                    environment->assetId,
                .environmentDisplayName =
                    environment->displayName,
                .environmentKind =
                    environment->kind,
                .path = resolvedScenePath.generic_string(),
                .authoredScenePath =
                    resolvedAuthoredScenePath.generic_string(),
                .runtimePath =
                    scene.runtimePath.generic_string(),
                .status =
                    humanizeIdentifier(scene.status),
                .startup =
                    scene.sceneId ==
                    loaded->descriptor.startupSceneId,
                .properties = {
                    {
                        "Scene id",
                        scene.sceneId,
                    },
                    {
                        "Status",
                        humanizeIdentifier(scene.status),
                    },
                    {
                        "Environment",
                        environment->displayName,
                    },
                    {
                        "Environment asset",
                        environment->assetId,
                    },
                    {
                        "Environment kind",
                        environment->kind,
                    },
                    {
                        "Environment backing",
                        resolvedScenePath.empty()
                            ? "Generated by the game runtime"
                            : resolvedScenePath.generic_string(),
                    },
                    {
                        "Authored scene",
                        resolvedAuthoredScenePath.empty()
                            ? "No project-owned overlay"
                            : resolvedAuthoredScenePath.generic_string(),
                    },
                    {
                        "Runtime source",
                        scene.runtimePath.empty()
                            ? "Engine/runtime snapshots"
                            : scene.runtimePath.generic_string(),
                    },
                    {
                        "Startup scene",
                        scene.sceneId ==
                                loaded->descriptor
                                    .startupSceneId
                            ? "Yes"
                            : "No",
                    },
                }});
    }
    const auto startupScene = std::find_if(
        loaded->sceneViews.begin(),
        loaded->sceneViews.end(),
        [&](const engine::editor::WorkspaceScene& scene) {
            return scene.id ==
                   loaded->descriptor.startupSceneId;
        });
    if (startupScene == loaded->sceneViews.end()) {
        outError =
            "The startup scene is not present in the game scene catalog.";
        return nullptr;
    }
    loaded->activeSceneIndex =
        static_cast<std::size_t>(
            std::distance(
                loaded->sceneViews.begin(),
                startupScene));
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
    appendProjectAssets(
        *loaded->runtime,
        loaded->assetViews);

    const glm::vec3 cameraPosition = camera.getPosition();
    const glm::vec3 cameraForward = camera.getDirection();
    const glm::vec3 cameraTarget = camera.getTarget();
    const engine::editor::EditorProjectCameraContext cameraContext{
        .cameraWorldPosition3 = glm::value_ptr(cameraPosition),
        .cameraForward3 = glm::value_ptr(cameraForward),
        .cameraTarget3 = glm::value_ptr(cameraTarget)};
    loaded->runtime->prewarm(renderer, cameraContext);
    refreshLayoutObjectViews(*loaded);
    rebuildProjectHierarchy(
        *loaded,
        loaded->sceneViews[
            loaded->activeSceneIndex],
        loaded->runtime->stats(),
        renderer.backendId()
            ? renderer.backendId()
            : "unknown");
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
                        : "",
                .sceneId =
                    preview.sceneId
                        ? preview.sceneId
                        : ""});
    }
    for (auto& scene : loaded->sceneViews) {
        const auto previewCount = std::count_if(
            loaded->gamePreviewViews.begin(),
            loaded->gamePreviewViews.end(),
            [&](const engine::editor::
                    WorkspaceGamePreview& preview) {
                return preview.sceneId ==
                       scene.id;
            });
        scene.properties.push_back(
            engine::editor::WorkspaceProperty{
                .name = "Associated game previews",
                .value =
                    std::to_string(previewCount)});
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

bool relaunchEditor(
    const std::filesystem::path& projectPath) {
#if defined(_WIN32)
    std::wstring executable(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        executable.data(),
        static_cast<DWORD>(executable.size()));
    if (length == 0u ||
        length >= executable.size()) {
        return false;
    }
    executable.resize(length);
    std::wstring commandLine =
        L"\"" + executable + L"\"";
    if (!projectPath.empty()) {
        commandLine +=
            L" --project=\"" +
            projectPath.wstring() +
            L"\"";
    }
    std::vector<wchar_t> writable(
        commandLine.begin(),
        commandLine.end());
    writable.push_back(L'\0');
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(
        executable.c_str(),
        writable.data(),
        nullptr,
        nullptr,
        FALSE,
        0u,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    if (!created) {
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
#else
    (void)projectPath;
    return false;
#endif
}

} // namespace

int main(int argc, char** argv) {
    const Arguments arguments = parseArguments(argc, argv);
    int result = 0;
    try {
        if (SDL_Init(0) != 0) {
            throw std::runtime_error(
                std::string(
                    "SDL bootstrap failed: ") +
                SDL_GetError());
        }
        const std::filesystem::path stateDirectory =
            editorStateDirectory();
        const auto rendererPreference =
            arguments.rendererPreference.value_or(
                loadRendererPreference(
                    stateDirectory));
        const auto resolvedRendererPreference =
            engine::editor::
                resolveEditorRendererPreference(
                    rendererPreference,
                    engine::editor::
                        currentEditorHostPlatform());
        EditorGraphics graphics =
            createEditorGraphics(
                resolvedRendererPreference);
        Window& window = *graphics.window;
        IRenderBackend& renderer =
            *graphics.renderer;
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
        if (renderer.requiresOpenGLContext()) {
            glViewport(0, 0, width, height);
        }
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
        Camera3D assetPreviewCamera(
            36.0f,
            4.0f / 3.0f,
            0.01f,
            100.0f);
        assetPreviewCamera.setPosition(
            glm::vec3(2.6f, 1.6f, 4.8f));
        assetPreviewCamera.lookAt(
            glm::vec3(0.0f, 0.8f, 0.0f));
        std::string error;
        const std::string layoutPath =
            (stateDirectory / "layout.ini").string();
        engine::editor::EditorShell editor;
        if (!editor.initialize(
                window.getSDLWindow(),
                &renderer,
                &error,
                layoutPath.c_str())) {
            throw std::runtime_error(error);
        }
        auto sceneSurface =
            createEditorRenderSurface(
                renderer,
                editor);
        auto gameSurface =
            createEditorRenderSurface(
                renderer,
                editor);
        auto assetPreviewSurface =
            createEditorRenderSurface(
                renderer,
                editor);

        std::vector<std::string> recentProjects =
            loadRecentProjects(stateDirectory);
        std::unique_ptr<LoadedProject> project;
        std::optional<std::filesystem::path> pendingProject;
        if (!arguments.project.empty()) {
            pendingProject = arguments.project;
        }
        std::string browserStatus =
            graphics.fallbackReason.empty()
                ? "Open a project or drop phlosion.project.json onto this window."
                : graphics.fallbackReason;
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
        int assetPreviewWidth = 400;
        int assetPreviewHeight = 300;
        int selectedAssetPreviewIndex = -1;
        float gameFixedAccumulator = 0.0f;
        bool commandLinePreviewApplied = false;
        bool commandLineAssetPreviewApplied = false;
        bool restartRequested = false;
        std::filesystem::path restartProjectPath;
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
                    if (renderer.requiresOpenGLContext()) {
                        glViewport(0, 0, width, height);
                    }
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
                    const bool middle =
                        (event.motion.state &
                         SDL_BUTTON_MMASK) != 0u;
                    const bool right =
                        (event.motion.state &
                         SDL_BUTTON_RMASK) != 0u;
                    if (middle && shift) {
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
                    selectedAssetPreviewIndex = -1;
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
                    if (!commandLineAssetPreviewApplied &&
                        !arguments.assetPreview.empty()) {
                        commandLineAssetPreviewApplied = true;
                        const auto match = std::find_if(
                            project->assetViews.begin(),
                            project->assetViews.end(),
                            [&](const auto& asset) {
                                return asset.previewable3d &&
                                    (containsInsensitive(
                                         asset.id,
                                         arguments.assetPreview) ||
                                     containsInsensitive(
                                         asset.displayName,
                                         arguments.assetPreview) ||
                                     containsInsensitive(
                                         asset.path,
                                         arguments.assetPreview));
                            });
                        if (match !=
                            project->assetViews.end()) {
                            const int assetIndex =
                                static_cast<int>(
                                    std::distance(
                                        project->assetViews.begin(),
                                        match));
                            std::string previewError;
                            if (selectAssetPreview(
                                    *project,
                                    assetIndex,
                                    assetPreviewCamera,
                                    selectedAssetPreviewIndex,
                                    previewError)) {
                                if (arguments
                                        .assetPreviewAnimation) {
                                    project->runtime->
                                        setAssetPreviewOptions(
                                            engine::editor::
                                                EditorProjectAssetPreviewOptions{
                                                    .animationIndex =
                                                        *arguments
                                                             .assetPreviewAnimation,
                                                    .animationPlaying =
                                                        false});
                                    refreshAssetPreviewView(
                                        *project);
                                }
                                editor.selectAsset(assetIndex);
                            } else {
                                project->status =
                                    "Command-line prefab preview selection failed: " +
                                    previewError;
                            }
                        } else {
                            project->status =
                                "Command-line prefab preview was not found: " +
                                arguments.assetPreview;
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
                (SDL_GetMouseState(nullptr, nullptr) &
                 (SDL_BUTTON_RMASK |
                  SDL_BUTTON_MMASK)) != 0u &&
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
                    if (sceneSurface->begin(
                            surfaceWidth,
                            surfaceHeight)) {
                        renderer.beginWorldSceneColorPass(
                            surfaceWidth,
                            surfaceHeight);
                        project->runtime->render(renderContext);
                        renderer.endWorldSceneColorPass();
                        sceneSurface->end();
                        // render() updates project-owned viewport
                        // projections used by picking and transform gizmos.
                        refreshLayoutObjectViews(*project);
                    }
                } else if (gameSurface->begin(
                               surfaceWidth,
                               surfaceHeight)) {
                    gameCamera.setAspectRatio(
                        static_cast<float>(surfaceWidth) /
                        static_cast<float>(surfaceHeight));
                    project->runtime->renderGamePreview(
                        renderContext);
                    gameSurface->end();
                }

                if (selectedAssetPreviewIndex >= 0) {
                    project->runtime->updateAssetPreview(
                        deltaSeconds);
                    refreshAssetPreviewView(*project);
                    if (project->assetPreviewView.ready) {
                        const int previewWidth =
                            std::max(1, assetPreviewWidth);
                        const int previewHeight =
                            std::max(1, assetPreviewHeight);
                        assetPreviewCamera.setAspectRatio(
                            static_cast<float>(previewWidth) /
                            static_cast<float>(previewHeight));
                        const glm::mat4 previewViewProjection =
                            assetPreviewCamera
                                .getProjectionMatrix() *
                            assetPreviewCamera
                                .getViewMatrix();
                        const glm::vec3 previewPosition =
                            assetPreviewCamera.getPosition();
                        const glm::vec3 previewForward =
                            assetPreviewCamera.getDirection();
                        const glm::vec3 previewTarget =
                            assetPreviewCamera.getTarget();
                        const engine::editor::
                            EditorProjectRenderContext
                                assetRenderContext{
                                    .renderer = &renderer,
                                    .viewProjectionMatrix4x4 =
                                        glm::value_ptr(
                                            previewViewProjection),
                                    .surfaceWidth =
                                        previewWidth,
                                    .surfaceHeight =
                                        previewHeight,
                                    .cameraWorldPosition3 =
                                        glm::value_ptr(
                                            previewPosition),
                                    .cameraForward3 =
                                        glm::value_ptr(
                                            previewForward),
                                    .cameraTarget3 =
                                        glm::value_ptr(
                                            previewTarget)};
                        if (assetPreviewSurface->begin(
                                previewWidth,
                                previewHeight)) {
                            project->runtime->
                                renderAssetPreview(
                                    assetRenderContext);
                            assetPreviewSurface->end();
                        }
                    }
                }

                const auto stats =
                    project->runtime->stats();
                const auto& activeScene =
                    project->sceneViews[
                        project->activeSceneIndex];
                const engine::editor::WorkspaceView workspace{
                    .projectName =
                        project->descriptor.displayName,
                    .projectId =
                        project->descriptor.projectId,
                    .projectRoot = project->rootText,
                    .sceneId =
                        activeScene.id,
                    .scenePath = activeScene.path,
                    .backendName =
                        renderer.backendId()
                            ? renderer.backendId()
                            : "unknown",
                    .rendererPreference =
                        rendererPreference,
                    .status = project->status,
                    .playState = playState,
                    .simulationSeconds = simulationSeconds,
                    .playConfigurations =
                        &project->playConfigurationViews,
                    .hierarchyItems =
                        &project->hierarchyViews,
                    .layoutObjects =
                        &project->layoutObjectViews,
                    .layoutOverlayVisible =
                        project->runtime->
                            layoutOverlayVisible(),
                    .canUndoSceneEdit =
                        project->runtime->
                            canUndoSceneEdit(),
                    .canRedoSceneEdit =
                        project->runtime->
                            canRedoSceneEdit(),
                    .assets = &project->assetViews,
                    .assetPreview =
                        selectedAssetPreviewIndex >= 0
                            ? &project->assetPreviewView
                            : nullptr,
                    .scenes = &project->sceneViews,
                    .activeSceneId =
                        activeScene.id,
                    .gamePreviews =
                        &project->gamePreviewViews,
                    .activeGamePreviewId =
                        project->activeGamePreviewId,
                    .activeViewport = activeViewport,
                    .focusActiveViewport =
                        focusActiveViewport,
                    .sceneTextureId =
                        sceneSurface->textureId(),
                    .gameTextureId =
                        gameSurface->textureId(),
                    .assetPreviewTextureId =
                        assetPreviewSurface->textureId(),
                    .flipRenderSurfaceTexturesVertically =
                        renderer.requiresOpenGLContext(),
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
                    .recentProjects = &recentProjects,
                    .backendName =
                        renderer.backendId()
                            ? renderer.backendId()
                            : "unknown",
                    .rendererPreference =
                        rendererPreference};
                actions = editor.drawProjectBrowser(browser);
            }
            editor.render();
            renderer.endFrame();
            window.swapBuffers();

            assetPreviewWidth =
                std::max(1, actions.assetPreviewWidth);
            assetPreviewHeight =
                std::max(1, actions.assetPreviewHeight);
            if (project &&
                actions.layoutOverlayVisibilityChanged) {
                project->runtime->setLayoutOverlayVisible(
                    actions.layoutOverlayVisible);
            }
            const auto refreshSceneAuthoringViews =
                [&]() {
                    if (!project) {
                        return;
                    }
                    refreshLayoutObjectViews(*project);
                    const auto& activeScene =
                        project->sceneViews[
                            project->activeSceneIndex];
                    rebuildProjectHierarchy(
                        *project,
                        activeScene,
                        project->runtime->stats(),
                            renderer.backendId()
                            ? renderer.backendId()
                            : "unknown");
                    project->assetViews.resize(
                        project->cookedAssetViewCount);
                    appendProjectAssets(
                        *project->runtime,
                        project->assetViews);
                };
            if (project &&
                (actions.undoSceneEditRequested ||
                 actions.redoSceneEditRequested)) {
                std::string sceneEditError;
                const bool applied =
                    actions.undoSceneEditRequested
                    ? project->runtime->undoSceneEdit(
                          &sceneEditError)
                    : project->runtime->redoSceneEdit(
                          &sceneEditError);
                if (applied) {
                    refreshSceneAuthoringViews();
                    project->status =
                        actions.undoSceneEditRequested
                        ? "Scene edit undone and autosaved."
                        : "Scene edit redone and autosaved.";
                } else {
                    project->status =
                        "Scene history command failed: " +
                        sceneEditError;
                }
            }
            if (project &&
                actions.selectLayoutObjectIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.selectLayoutObjectIndex) <
                    project->layoutObjectViews.size()) {
                const auto& object =
                    project->layoutObjectViews[
                        static_cast<std::size_t>(
                            actions.selectLayoutObjectIndex)];
                project->runtime->selectLayoutObject(
                    object.stableId.c_str());
            }
            if (project &&
                actions.editLayoutObjectIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.editLayoutObjectIndex) <
                    project->layoutObjectViews.size() &&
                (actions.layoutObjectDuplicateRequested ||
                 actions.layoutObjectDeleteRequested ||
                 actions.layoutObjectRenameRequested ||
                 actions.layoutObjectReparentRequested)) {
                const auto object =
                    project->layoutObjectViews[
                        static_cast<std::size_t>(
                            actions.editLayoutObjectIndex)];
                std::string commandError;
                std::string createdStableId;
                bool applied = false;
                if (actions.layoutObjectDuplicateRequested) {
                    applied = project->runtime->
                        duplicateLayoutObject(
                            object.stableId.c_str(),
                            &createdStableId,
                            &commandError);
                } else if (
                    actions.layoutObjectDeleteRequested) {
                    applied = project->runtime->
                        deleteLayoutObject(
                            object.stableId.c_str(),
                            &commandError);
                } else if (
                    actions.layoutObjectRenameRequested) {
                    applied = project->runtime->
                        renameLayoutObject(
                            engine::editor::
                                EditorProjectLayoutObjectCommand{
                                    .stableId =
                                        object.stableId.c_str(),
                                    .value =
                                        actions.layoutObjectText.c_str()},
                            &commandError);
                } else {
                    applied = project->runtime->
                        reparentLayoutObject(
                            engine::editor::
                                EditorProjectLayoutObjectCommand{
                                    .stableId =
                                        object.stableId.c_str(),
                                    .value =
                                        actions.layoutObjectText.c_str()},
                            &commandError);
                }
                if (applied) {
                    refreshSceneAuthoringViews();
                    if (!createdStableId.empty()) {
                        project->runtime->selectLayoutObject(
                            createdStableId.c_str());
                    }
                    project->status =
                        actions.layoutObjectDuplicateRequested
                        ? "Prefab instance duplicated and autosaved."
                        : actions.layoutObjectDeleteRequested
                        ? "Scene object deleted and autosaved."
                        : actions.layoutObjectRenameRequested
                        ? "Scene object renamed and autosaved."
                        : "Scene object moved to a hierarchy folder and autosaved.";
                } else {
                    project->status =
                        "Scene object command failed: " +
                        commandError;
                }
            }
            if (project &&
                actions.instantiateAssetIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.instantiateAssetIndex) <
                    project->assetViews.size()) {
                const auto asset =
                    project->assetViews[
                        static_cast<std::size_t>(
                            actions.instantiateAssetIndex)];
                std::string createdStableId;
                std::string instantiateError;
                if (asset.sceneInstantiable &&
                    project->runtime->instantiateAsset(
                        asset.id.c_str(),
                        &createdStableId,
                        &instantiateError)) {
                    refreshSceneAuthoringViews();
                    if (!createdStableId.empty()) {
                        project->runtime->selectLayoutObject(
                            createdStableId.c_str());
                    }
                    project->status =
                        "Prefab instantiated and autosaved: " +
                        asset.displayName + ".";
                } else {
                    project->status =
                        "Prefab instantiation failed: " +
                        instantiateError;
                }
            }
            if (project &&
                actions.editLayoutObjectIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.editLayoutObjectIndex) <
                    project->layoutObjectViews.size() &&
                (actions.layoutObjectEditRequested ||
                 actions.layoutObjectPreviewRequested ||
                 actions.layoutObjectCommitRequested ||
                 actions.layoutObjectCancelRequested ||
                 actions.layoutObjectResetRequested)) {
                const auto& object =
                    project->layoutObjectViews[
                        static_cast<std::size_t>(
                            actions.editLayoutObjectIndex)];
                std::string layoutError;
                bool applied = true;
                if (actions.layoutObjectCancelRequested) {
                    project->runtime->
                        cancelLayoutObjectOverride(
                            object.stableId.c_str());
                } else if (
                    actions.layoutObjectResetRequested) {
                    applied = project->runtime->
                        resetLayoutObjectOverride(
                            object.stableId.c_str(),
                            &layoutError);
                } else if (
                    actions.layoutObjectPreviewRequested) {
                    applied = project->runtime->
                        previewLayoutObjectOverride(
                            engine::editor::
                                EditorProjectLayoutEdit{
                                    .stableId =
                                        object.stableId.c_str(),
                                    .translation =
                                        actions.layoutTranslation,
                                    .rotationDegrees =
                                        actions
                                            .layoutRotationDegrees,
                                    .scale =
                                        actions.layoutScale,
                                    .suppressed =
                                        actions.layoutSuppressed,
                                    .reason =
                                        "autochess_board_clearance"},
                            &layoutError);
                    if (applied &&
                        actions.layoutObjectCommitRequested) {
                        applied = project->runtime->
                            commitLayoutObjectOverride(
                                object.stableId.c_str(),
                                &layoutError);
                    }
                } else if (
                    actions.layoutObjectCommitRequested) {
                    applied = project->runtime->
                        commitLayoutObjectOverride(
                            object.stableId.c_str(),
                            &layoutError);
                } else {
                    applied = project->runtime->
                        setLayoutObjectOverride(
                            engine::editor::
                                EditorProjectLayoutEdit{
                                    .stableId =
                                        object.stableId.c_str(),
                                    .translation =
                                        actions.layoutTranslation,
                                    .rotationDegrees =
                                        actions
                                            .layoutRotationDegrees,
                                    .scale =
                                        actions.layoutScale,
                                    .suppressed =
                                        actions.layoutSuppressed,
                                    .reason =
                                        "autochess_board_clearance"},
                            &layoutError);
                }
                if (applied) {
                    refreshLayoutObjectViews(*project);
                    const auto& activeScene =
                        project->sceneViews[
                            project->activeSceneIndex];
                    rebuildProjectHierarchy(
                        *project,
                        activeScene,
                        project->runtime->stats(),
                        renderer.backendId()
                            ? renderer.backendId()
                            : "unknown");
                    project->status =
                        actions.layoutObjectResetRequested
                        ? "Layout override reset to canonical source."
                        : actions.layoutObjectCancelRequested
                        ? "Live layout edit cancelled."
                        : actions.layoutObjectCommitRequested
                        ? "Layout override autosaved."
                        : actions.layoutObjectPreviewRequested
                        ? "Live layout edit preview."
                        : "Layout override applied, saved, and hot-reloaded.";
                } else {
                    project->status =
                        "Layout override failed: " +
                        layoutError;
                }
            }
            if (project &&
                actions.selectAssetIndex >= 0 &&
                static_cast<std::size_t>(
                    actions.selectAssetIndex) <
                    project->assetViews.size()) {
                const auto& asset =
                    project->assetViews[
                        static_cast<std::size_t>(
                            actions.selectAssetIndex)];
                if (asset.previewable3d) {
                    std::string previewError;
                    if (!selectAssetPreview(
                            *project,
                            actions.selectAssetIndex,
                            assetPreviewCamera,
                            selectedAssetPreviewIndex,
                            previewError)) {
                        selectedAssetPreviewIndex = -1;
                        project->status =
                            "Cooked prefab preview failed: " +
                            previewError;
                    }
                } else {
                    selectedAssetPreviewIndex = -1;
                }
            }
            if (project &&
                selectedAssetPreviewIndex >= 0 &&
                actions.assetPreviewOptionsChanged) {
                project->runtime->setAssetPreviewOptions(
                    engine::editor::
                        EditorProjectAssetPreviewOptions{
                            .animationIndex =
                                actions
                                    .assetPreviewAnimationIndex,
                            .playbackSpeed =
                                actions
                                    .assetPreviewPlaybackSpeed,
                            .seekTimeSeconds =
                                actions
                                    .assetPreviewSeekTimeSeconds,
                            .seekRequested =
                                actions
                                    .assetPreviewSeekRequested,
                            .animationPlaying =
                                actions
                                    .assetPreviewAnimationPlaying,
                            .showMesh =
                                actions.assetPreviewShowMesh,
                            .showMaterials =
                                actions
                                    .assetPreviewShowMaterials,
                            .showTextures =
                                actions
                                    .assetPreviewShowTextures,
                            .showWireframe =
                                actions
                                    .assetPreviewShowWireframe,
                            .showSkeleton =
                                actions
                                    .assetPreviewShowSkeleton});
            }
            if (project &&
                selectedAssetPreviewIndex >= 0) {
                if (actions.resetAssetPreviewCamera) {
                    resetAssetPreviewCamera(
                        assetPreviewCamera,
                        project->assetPreviewView);
                }
                if (actions.assetPreviewOrbitYaw != 0.0f ||
                    actions.assetPreviewOrbitPitch != 0.0f) {
                    assetPreviewCamera.orbit(
                        actions.assetPreviewOrbitYaw,
                        actions.assetPreviewOrbitPitch);
                }
                if (actions.assetPreviewPanX != 0.0f ||
                    actions.assetPreviewPanY != 0.0f) {
                    const float panScale =
                        std::max(
                            0.001f,
                            project->assetPreviewView
                                    .boundsRadius *
                                0.0025f);
                    assetPreviewCamera.panPlanar(
                        actions.assetPreviewPanX,
                        actions.assetPreviewPanY,
                        panScale);
                }
                if (actions.assetPreviewZoom != 0.0f) {
                    assetPreviewCamera.zoom(
                        actions.assetPreviewZoom *
                        std::max(
                            0.12f,
                            project->assetPreviewView
                                    .boundsRadius *
                                0.28f));
                }
            }

            if (actions.exit) {
                running = false;
            }
            if (actions.rendererPreferenceChanged) {
                saveRendererPreference(
                    stateDirectory,
                    actions.rendererPreference);
                restartRequested = true;
                if (project) {
                    restartProjectPath =
                        project->descriptorPath;
                }
                running = false;
            }
            if (actions.closeProject) {
                project.reset();
                selectedAssetPreviewIndex = -1;
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
                const std::size_t requestedSceneIndex =
                    static_cast<std::size_t>(
                        actions.openSceneIndex);
                if (requestedSceneIndex ==
                    project->activeSceneIndex) {
                    activeViewport =
                        engine::editor::
                            EditorViewportKind::Scene;
                    focusActiveViewport = true;
                    project->status =
                        "Focused open scene: " +
                        scene.displayName + ".";
                } else {
                    std::string sceneError;
                    const engine::editor::
                        EditorProjectSceneContext sceneContext{
                            .sceneId =
                                scene.id.c_str(),
                            .displayName =
                                scene.displayName.c_str(),
                            .environmentAssetId =
                                scene.environmentAssetId.c_str(),
                            .environmentKind =
                                scene.environmentKind.c_str(),
                            .environmentPath =
                                scene.path.c_str(),
                            .authoredScenePath =
                                scene.authoredScenePath.c_str(),
                            .runtimePath =
                                scene.runtimePath.c_str(),
                            .status =
                                scene.status.c_str()};
                    if (!project->runtime->openScene(
                            sceneContext,
                            &sceneError)) {
                        project->status =
                            "Scene open failed: " +
                            sceneError;
                    } else {
                        project->activeSceneIndex =
                            requestedSceneIndex;
                        project->scenePath =
                            scene.path;
                        project->scenePathText =
                            scene.path;
                        simulationSeconds = 0.0f;
                        playState =
                            engine::editor::
                                EditorPlayState::Editing;
                        project->runtime->update(0.0f);
                        const glm::vec3 cameraPosition =
                            camera.getPosition();
                        const glm::vec3 cameraForward =
                            camera.getDirection();
                        const glm::vec3 cameraTarget =
                            camera.getTarget();
                        project->runtime->prewarm(
                            renderer,
                            engine::editor::
                                EditorProjectCameraContext{
                                    .cameraWorldPosition3 =
                                        glm::value_ptr(
                                            cameraPosition),
                                    .cameraForward3 =
                                        glm::value_ptr(
                                            cameraForward),
                                    .cameraTarget3 =
                                        glm::value_ptr(
                                            cameraTarget)});
                        refreshLayoutObjectViews(
                            *project);
                        rebuildProjectHierarchy(
                            *project,
                            scene,
                            project->runtime->stats(),
                            renderer.backendId()
                                ? renderer.backendId()
                                : "unknown");
                        activeViewport =
                            engine::editor::
                                EditorViewportKind::Scene;
                        focusActiveViewport = true;
                        project->status =
                            "Opened game scene: " +
                            scene.displayName + ".";
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
        sceneSurface->shutdown();
        gameSurface->shutdown();
        assetPreviewSurface->shutdown();
        editor.shutdown();
        renderer.shutdown();
        if (restartRequested &&
            !relaunchEditor(restartProjectPath)) {
            std::cerr
                << "[Phlosion Editor] Could not restart after changing the renderer. "
                << "Please open the editor again manually.\n";
        }
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
