#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "engine/editor/EditorShell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

#include <imgui.h>
#if defined(_WIN32)
#include <imgui_impl_dx12.h>
#endif
#include <imgui_impl_opengl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"

namespace engine::editor {

namespace {

#if defined(_WIN32)
struct D3D12DescriptorAllocator {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptorSize = 0u;
    std::uint32_t capacity = 0u;
    std::uint32_t next = 0u;
    std::vector<std::uint32_t> freeIndices;

    bool allocate(
        D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE& gpu) {
        if (!heap || descriptorSize == 0u) {
            return false;
        }
        std::uint32_t index = 0u;
        if (!freeIndices.empty()) {
            index = freeIndices.back();
            freeIndices.pop_back();
        } else {
            if (next >= capacity) {
                return false;
            }
            index = next++;
        }
        cpu = heap->GetCPUDescriptorHandleForHeapStart();
        gpu = heap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr +=
            static_cast<SIZE_T>(index) *
            static_cast<SIZE_T>(descriptorSize);
        gpu.ptr +=
            static_cast<UINT64>(index) *
            static_cast<UINT64>(descriptorSize);
        return true;
    }

    void release(D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
        if (!heap || descriptorSize == 0u ||
            cpu.ptr == 0u) {
            return;
        }
        const SIZE_T start =
            heap->GetCPUDescriptorHandleForHeapStart().ptr;
        if (cpu.ptr < start) {
            return;
        }
        const SIZE_T offset = cpu.ptr - start;
        if (offset % descriptorSize != 0u) {
            return;
        }
        const auto index = static_cast<std::uint32_t>(
            offset / descriptorSize);
        if (index < next) {
            freeIndices.push_back(index);
        }
    }
};

void allocateImGuiD3D12Descriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
    D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
    if (!info || !outCpu || !outGpu ||
        !info->UserData) {
        return;
    }
    auto* allocator =
        static_cast<D3D12DescriptorAllocator*>(
            info->UserData);
    (void)allocator->allocate(*outCpu, *outGpu);
}

void freeImGuiD3D12Descriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE) {
    if (!info || !info->UserData) {
        return;
    }
    static_cast<D3D12DescriptorAllocator*>(
        info->UserData)->release(cpu);
}
#endif

enum class InspectorSelectionDomain {
    Hierarchy,
    Asset,
    Scene,
};

bool containsInsensitive(
    std::string_view value,
    std::string_view query) {
    if (query.empty()) {
        return true;
    }
    std::string foldedValue(value);
    std::string foldedQuery(query);
    const auto fold = [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    };
    std::transform(
        foldedValue.begin(),
        foldedValue.end(),
        foldedValue.begin(),
        fold);
    std::transform(
        foldedQuery.begin(),
        foldedQuery.end(),
        foldedQuery.begin(),
        fold);
    return foldedValue.find(foldedQuery) !=
           std::string::npos;
}

void drawInspectorProperties(
    const std::vector<WorkspaceProperty>& properties) {
    if (properties.empty()) {
        ImGui::TextDisabled(
            "No properties are available for this item.");
        return;
    }
    if (!ImGui::BeginTable(
            "InspectorProperties",
            2,
            ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn(
        "Property",
        ImGuiTableColumnFlags_WidthFixed,
        125.0f);
    ImGui::TableSetupColumn("Value");
    for (const auto& property : properties) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled(
            "%s",
            property.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "%s",
            property.value.c_str());
    }
    ImGui::EndTable();
}

ImGuiKey imguiKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_TAB: return ImGuiKey_Tab;
        case SDLK_LEFT: return ImGuiKey_LeftArrow;
        case SDLK_RIGHT: return ImGuiKey_RightArrow;
        case SDLK_UP: return ImGuiKey_UpArrow;
        case SDLK_DOWN: return ImGuiKey_DownArrow;
        case SDLK_PAGEUP: return ImGuiKey_PageUp;
        case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
        case SDLK_HOME: return ImGuiKey_Home;
        case SDLK_END: return ImGuiKey_End;
        case SDLK_INSERT: return ImGuiKey_Insert;
        case SDLK_DELETE: return ImGuiKey_Delete;
        case SDLK_BACKSPACE: return ImGuiKey_Backspace;
        case SDLK_SPACE: return ImGuiKey_Space;
        case SDLK_RETURN: return ImGuiKey_Enter;
        case SDLK_ESCAPE: return ImGuiKey_Escape;
        case SDLK_QUOTE: return ImGuiKey_Apostrophe;
        case SDLK_COMMA: return ImGuiKey_Comma;
        case SDLK_MINUS: return ImGuiKey_Minus;
        case SDLK_PERIOD: return ImGuiKey_Period;
        case SDLK_SLASH: return ImGuiKey_Slash;
        case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
        case SDLK_EQUALS: return ImGuiKey_Equal;
        case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
        case SDLK_BACKSLASH: return ImGuiKey_Backslash;
        case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
        case SDLK_BACKQUOTE: return ImGuiKey_GraveAccent;
        case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
        case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
        case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
        case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
        case SDLK_PAUSE: return ImGuiKey_Pause;
        case SDLK_KP_0: return ImGuiKey_Keypad0;
        case SDLK_KP_1: return ImGuiKey_Keypad1;
        case SDLK_KP_2: return ImGuiKey_Keypad2;
        case SDLK_KP_3: return ImGuiKey_Keypad3;
        case SDLK_KP_4: return ImGuiKey_Keypad4;
        case SDLK_KP_5: return ImGuiKey_Keypad5;
        case SDLK_KP_6: return ImGuiKey_Keypad6;
        case SDLK_KP_7: return ImGuiKey_Keypad7;
        case SDLK_KP_8: return ImGuiKey_Keypad8;
        case SDLK_KP_9: return ImGuiKey_Keypad9;
        case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
        case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
        case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
        case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
        case SDLK_KP_EQUALS: return ImGuiKey_KeypadEqual;
        case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
        case SDLK_LSHIFT: return ImGuiKey_LeftShift;
        case SDLK_LALT: return ImGuiKey_LeftAlt;
        case SDLK_LGUI: return ImGuiKey_LeftSuper;
        case SDLK_RCTRL: return ImGuiKey_RightCtrl;
        case SDLK_RSHIFT: return ImGuiKey_RightShift;
        case SDLK_RALT: return ImGuiKey_RightAlt;
        case SDLK_RGUI: return ImGuiKey_RightSuper;
        case SDLK_0: return ImGuiKey_0;
        case SDLK_1: return ImGuiKey_1;
        case SDLK_2: return ImGuiKey_2;
        case SDLK_3: return ImGuiKey_3;
        case SDLK_4: return ImGuiKey_4;
        case SDLK_5: return ImGuiKey_5;
        case SDLK_6: return ImGuiKey_6;
        case SDLK_7: return ImGuiKey_7;
        case SDLK_8: return ImGuiKey_8;
        case SDLK_9: return ImGuiKey_9;
        case SDLK_a: return ImGuiKey_A;
        case SDLK_b: return ImGuiKey_B;
        case SDLK_c: return ImGuiKey_C;
        case SDLK_d: return ImGuiKey_D;
        case SDLK_e: return ImGuiKey_E;
        case SDLK_f: return ImGuiKey_F;
        case SDLK_g: return ImGuiKey_G;
        case SDLK_h: return ImGuiKey_H;
        case SDLK_i: return ImGuiKey_I;
        case SDLK_j: return ImGuiKey_J;
        case SDLK_k: return ImGuiKey_K;
        case SDLK_l: return ImGuiKey_L;
        case SDLK_m: return ImGuiKey_M;
        case SDLK_n: return ImGuiKey_N;
        case SDLK_o: return ImGuiKey_O;
        case SDLK_p: return ImGuiKey_P;
        case SDLK_q: return ImGuiKey_Q;
        case SDLK_r: return ImGuiKey_R;
        case SDLK_s: return ImGuiKey_S;
        case SDLK_t: return ImGuiKey_T;
        case SDLK_u: return ImGuiKey_U;
        case SDLK_v: return ImGuiKey_V;
        case SDLK_w: return ImGuiKey_W;
        case SDLK_x: return ImGuiKey_X;
        case SDLK_y: return ImGuiKey_Y;
        case SDLK_z: return ImGuiKey_Z;
        case SDLK_F1: return ImGuiKey_F1;
        case SDLK_F2: return ImGuiKey_F2;
        case SDLK_F3: return ImGuiKey_F3;
        case SDLK_F4: return ImGuiKey_F4;
        case SDLK_F5: return ImGuiKey_F5;
        case SDLK_F6: return ImGuiKey_F6;
        case SDLK_F7: return ImGuiKey_F7;
        case SDLK_F8: return ImGuiKey_F8;
        case SDLK_F9: return ImGuiKey_F9;
        case SDLK_F10: return ImGuiKey_F10;
        case SDLK_F11: return ImGuiKey_F11;
        case SDLK_F12: return ImGuiKey_F12;
        default: return ImGuiKey_None;
    }
}

void updateModifiers(ImGuiIO& io, SDL_Keymod modifiers) {
    io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifiers & KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (modifiers & KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (modifiers & KMOD_GUI) != 0);
}

std::string text(std::string_view value) {
    return std::string(value.begin(), value.end());
}

void applyPhlosionStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 5.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.052f, 0.061f, 0.97f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.063f, 0.073f, 0.94f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.050f, 0.058f, 0.067f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.16f, 0.20f, 0.22f, 0.75f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.11f, 0.12f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.17f, 0.16f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.13f, 0.22f, 0.19f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.035f, 0.041f, 0.047f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.055f, 0.085f, 0.075f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.035f, 0.041f, 0.047f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.11f, 0.28f, 0.22f, 0.85f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.38f, 0.29f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.46f, 0.34f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.11f, 0.30f, 0.23f, 0.90f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.14f, 0.40f, 0.30f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.50f, 0.37f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.07f, 0.10f, 0.10f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.13f, 0.34f, 0.26f, 1.0f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.10f, 0.25f, 0.20f, 1.0f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.18f, 0.64f, 0.43f, 0.70f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.88f, 0.57f, 1.0f);
}

const char* rendererPreferenceLabel(
    EditorRendererPreference preference) {
    switch (preference) {
        case EditorRendererPreference::Auto:
            return "Auto (recommended)";
        case EditorRendererPreference::D3D12:
            return "Direct3D 12";
        case EditorRendererPreference::Vulkan:
            return "Vulkan";
        case EditorRendererPreference::OpenGL:
            return "OpenGL (compatibility)";
    }
    return "Auto (recommended)";
}

void drawPreferencesWindow(
    bool& open,
    EditorRendererPreference& pending,
    EditorRendererPreference current,
    EditorShellActions& actions) {
    if (!open) {
        return;
    }
    ImGui::SetNextWindowSize(
        ImVec2(520.0f, 300.0f),
        ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Preferences", &open)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Editor");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Rendering");
    ImGui::TextDisabled(
        "Select the graphics API used by the editor shell and all embedded render surfaces.");
    ImGui::Spacing();

    if (ImGui::BeginCombo(
            "Rendering API",
            rendererPreferenceLabel(pending))) {
        const auto option =
            [&](EditorRendererPreference preference) {
                const bool selected = pending == preference;
                if (ImGui::Selectable(
                        rendererPreferenceLabel(preference),
                        selected)) {
                    pending = preference;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            };
        option(EditorRendererPreference::Auto);
#if defined(_WIN32)
        option(EditorRendererPreference::D3D12);
#endif
        option(EditorRendererPreference::Vulkan);
        option(EditorRendererPreference::OpenGL);
        ImGui::EndCombo();
    }
    ImGui::TextDisabled(
        "The preference is stored locally for this editor installation.");
    ImGui::TextColored(
        ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
        "Applying a different API restarts the editor and restores the open project.");

    ImGui::Spacing();
    ImGui::Separator();
    const bool changed = pending != current;
    if (!changed) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(
            "Apply and Restart",
            ImVec2(150.0f, 32.0f))) {
        actions.rendererPreferenceChanged = true;
        actions.rendererPreference = pending;
        open = false;
    }
    if (!changed) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(
            "Close",
            ImVec2(90.0f, 32.0f))) {
        pending = current;
        open = false;
    }
    ImGui::End();
}

float screenDistanceSquared(
    const ImVec2& a,
    const ImVec2& b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

float screenDistanceToSegmentSquared(
    const ImVec2& point,
    const ImVec2& start,
    const ImVec2& end) {
    const float segmentX = end.x - start.x;
    const float segmentY = end.y - start.y;
    const float lengthSquared =
        segmentX * segmentX +
        segmentY * segmentY;
    if (lengthSquared <= 0.0001f) {
        return screenDistanceSquared(point, start);
    }
    const float projection = std::clamp(
        ((point.x - start.x) * segmentX +
         (point.y - start.y) * segmentY) /
            lengthSquared,
        0.0f,
        1.0f);
    return screenDistanceSquared(
        point,
        ImVec2(
            start.x + segmentX * projection,
            start.y + segmentY * projection));
}

float screenEdge(
    const ImVec2& first,
    const ImVec2& second,
    const ImVec2& point) {
    return (point.x - first.x) * (second.y - first.y) -
        (point.y - first.y) * (second.x - first.x);
}

bool screenPointInTriangle(
    const ImVec2& point,
    const ImVec2& first,
    const ImVec2& second,
    const ImVec2& third) {
    const float edge0 = screenEdge(first, second, point);
    const float edge1 = screenEdge(second, third, point);
    const float edge2 = screenEdge(third, first, point);
    const bool negative =
        edge0 < 0.0f || edge1 < 0.0f || edge2 < 0.0f;
    const bool positive =
        edge0 > 0.0f || edge1 > 0.0f || edge2 > 0.0f;
    return !(negative && positive);
}

bool screenPointInQuad(
    const ImVec2& point,
    const std::array<ImVec2, 4>& corners) {
    return screenPointInTriangle(
               point,
               corners[0],
               corners[1],
               corners[2]) ||
        screenPointInTriangle(
               point,
               corners[0],
               corners[2],
               corners[3]);
}

ImVec2 normalizedScreenDirection(
    float x,
    float y) {
    const float length =
        std::sqrt(x * x + y * y);
    if (length <= 0.0001f) {
        return ImVec2(1.0f, 0.0f);
    }
    return ImVec2(x / length, y / length);
}

ImU32 gizmoAxisColor(int axis, bool highlighted) {
    constexpr std::array<ImVec4, 3> colors{{
        ImVec4(0.96f, 0.22f, 0.20f, 1.0f),
        ImVec4(0.30f, 0.88f, 0.30f, 1.0f),
        ImVec4(0.24f, 0.52f, 1.0f, 1.0f),
    }};
    ImVec4 color = colors[
        static_cast<std::size_t>(
            std::clamp(axis, 0, 2))];
    if (highlighted) {
        color.x = std::min(1.0f, color.x + 0.28f);
        color.y = std::min(1.0f, color.y + 0.28f);
        color.z = std::min(1.0f, color.z + 0.28f);
    }
    return ImGui::ColorConvertFloat4ToU32(color);
}

} // namespace

struct EditorShell::Impl {
    enum class RendererBackend {
        OpenGL,
        D3D12,
        Vulkan,
    };

    SDL_Window* window = nullptr;
    IRenderBackend* renderer = nullptr;
    RendererBackend rendererBackend =
        RendererBackend::OpenGL;
    bool ready = false;
    bool firstLayout = true;
    int selectedHierarchyItem = 0;
    std::unordered_map<std::string, bool>
        hierarchyFolderExpanded;
    int selectedAsset = 0;
    int selectedScene = 0;
    int selectedGamePreview = 0;
    bool preferencesOpen = false;
    EditorRendererPreference pendingRendererPreference =
        EditorRendererPreference::Auto;
    std::string observedActiveGamePreviewId;
    InspectorSelectionDomain inspectorSelection =
        InspectorSelectionDomain::Hierarchy;
    std::array<char, 256> assetFilter{};
    EditorViewportKind selectedViewport =
        EditorViewportKind::Scene;
    int assetPreviewAnimationIndex = -1;
    float assetPreviewPlaybackSpeed = 1.0f;
    bool assetPreviewAnimationPlaying = true;
    bool assetPreviewShowMesh = true;
    bool assetPreviewShowMaterials = true;
    bool assetPreviewShowTextures = true;
    bool assetPreviewShowWireframe = false;
    bool assetPreviewShowSkeleton = false;
    std::string activeAssetPreviewId;
    std::string activeLayoutObjectId;
    std::array<char, 256> layoutObjectName{};
    std::array<char, 512> layoutObjectCategoryPath{};
    std::array<float, 3> layoutTranslation{};
    std::array<float, 3> layoutRotationDegrees{};
    std::array<float, 3> layoutScale{1.0f, 1.0f, 1.0f};
    bool layoutSuppressed = false;
    int selectedLayoutObject = -1;
    std::vector<std::string> selectedLayoutObjectIds;
    LayoutGizmoOperation layoutGizmoOperation =
        LayoutGizmoOperation::Translate;
    bool layoutGizmoDragging = false;
    int layoutGizmoAxis = -1;
    ImVec2 layoutGizmoDragStart{};
    ImVec2 layoutGizmoDragDirection{1.0f, 0.0f};
    float layoutGizmoSourceUnitsPerPixel = 1.0f;
    std::array<float, 3> layoutGizmoStartTranslation{};
    std::array<float, 3> layoutGizmoStartRotation{};
    std::array<float, 3> layoutGizmoStartScale{
        1.0f, 1.0f, 1.0f};
    bool layoutBoxSelecting = false;
    ImVec2 layoutBoxSelectStart{};
    float boardClearancePaddingCells = 0.35f;
    bool boardClearanceClearTerrain = true;
    bool boardClearanceClearVegetation = true;
    bool boardClearanceClearObjects = true;
    bool boardClearanceRetainRamps = true;
    bool boardClearanceAddGroundInfill = true;
    bool terrainTileEditing = false;
    bool terrainTileDragging = false;
    EditorProjectTerrainTileCoordinate
        terrainTileDragStart{};
    std::vector<EditorProjectTerrainTileCoordinate>
        selectedTerrainTiles;
    int terrainSurfaceIndex = 0;
    int terrainShapeIndex = 0;
    std::string settingsIniPath;
#if defined(_WIN32)
    std::unique_ptr<D3D12DescriptorAllocator>
        d3d12Descriptors;
#endif
};

EditorShell::EditorShell()
    : impl_(new Impl()) {}

EditorShell::~EditorShell() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool EditorShell::initialize(
    SDL_Window* window,
    IRenderBackend* renderer,
    std::string* outError,
    const char* settingsIniPath) {
    if (impl_->ready) {
        return true;
    }
    if (!window || !renderer) {
        if (outError) {
            *outError =
                "Editor shell requires a valid SDL window and renderer.";
        }
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "phlosion_sdl2";
    impl_->settingsIniPath =
        settingsIniPath ? settingsIniPath : "phlosion_editor.ini";
    io.IniFilename = impl_->settingsIniPath.c_str();
    applyPhlosionStyle();

    const std::string_view backendId =
        renderer->backendId()
            ? renderer->backendId()
            : "";
    if (backendId == "opengl") {
        if (!ImGui_ImplOpenGL3_Init("#version 330")) {
            ImGui::DestroyContext();
            if (outError) {
                *outError =
                    "Dear ImGui OpenGL backend initialization failed.";
            }
            return false;
        }
        impl_->rendererBackend =
            Impl::RendererBackend::OpenGL;
#if defined(_WIN32)
    } else if (backendId == "d3d12") {
        auto* d3d12 =
            dynamic_cast<D3D12RenderBackend*>(renderer);
        if (!d3d12 || !d3d12->nativeDevice() ||
            !d3d12->nativeCommandQueue()) {
            ImGui::DestroyContext();
            if (outError) {
                *outError =
                    "The D3D12 editor renderer did not expose a valid native device.";
            }
            return false;
        }
        auto descriptors =
            std::make_unique<
                D3D12DescriptorAllocator>();
        constexpr std::uint32_t kDescriptorCount = 64u;
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type =
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = kDescriptorCount;
        heapDesc.Flags =
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(
                d3d12->nativeDevice()->
                    CreateDescriptorHeap(
                        &heapDesc,
                        IID_PPV_ARGS(
                            descriptors->heap
                                .ReleaseAndGetAddressOf()))) ||
            !descriptors->heap) {
            ImGui::DestroyContext();
            if (outError) {
                *outError =
                    "Could not allocate the D3D12 editor texture heap.";
            }
            return false;
        }
        descriptors->descriptorSize =
            d3d12->nativeDevice()->
                GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        descriptors->capacity = kDescriptorCount;
        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = d3d12->nativeDevice();
        initInfo.CommandQueue =
            d3d12->nativeCommandQueue();
        initInfo.NumFramesInFlight = 2;
        initInfo.RTVFormat =
            DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        initInfo.UserData = descriptors.get();
        initInfo.SrvDescriptorHeap =
            descriptors->heap.Get();
        initInfo.SrvDescriptorAllocFn =
            &allocateImGuiD3D12Descriptor;
        initInfo.SrvDescriptorFreeFn =
            &freeImGuiD3D12Descriptor;
        if (!ImGui_ImplDX12_Init(&initInfo)) {
            ImGui::DestroyContext();
            if (outError) {
                *outError =
                    "Dear ImGui D3D12 backend initialization failed.";
            }
            return false;
        }
        impl_->d3d12Descriptors =
            std::move(descriptors);
        impl_->rendererBackend =
            Impl::RendererBackend::D3D12;
#endif
    } else if (backendId == "vulkan") {
        auto* vulkan =
            dynamic_cast<VulkanRenderBackend*>(renderer);
        VulkanRenderBackend::EditorUiContext context;
        if (!vulkan ||
            !vulkan->getEditorUiContext(context)) {
            ImGui::DestroyContext();
            if (outError) {
                *outError =
                    "The Vulkan editor renderer did not expose a valid native context.";
            }
            return false;
        }
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_0;
        initInfo.Instance = context.instance;
        initInfo.PhysicalDevice =
            context.physicalDevice;
        initInfo.Device = context.device;
        initInfo.QueueFamily =
            context.graphicsQueueFamily;
        initInfo.Queue = context.graphicsQueue;
        initInfo.DescriptorPoolSize = 64u;
        initInfo.RenderPass = context.renderPass;
        initInfo.MinImageCount =
            context.minimumImageCount;
        initInfo.ImageCount = context.imageCount;
        initInfo.MSAASamples =
            VK_SAMPLE_COUNT_1_BIT;
        if (!ImGui_ImplVulkan_Init(&initInfo) ||
            !ImGui_ImplVulkan_CreateFontsTexture()) {
            ImGui_ImplVulkan_Shutdown();
            ImGui::DestroyContext();
            if (outError) {
                *outError =
                    "Dear ImGui Vulkan backend initialization failed.";
            }
            return false;
        }
        impl_->rendererBackend =
            Impl::RendererBackend::Vulkan;
    } else {
        ImGui::DestroyContext();
        if (outError) {
            *outError =
                "The selected renderer does not yet provide an editor UI integration.";
        }
        return false;
    }

    impl_->window = window;
    impl_->renderer = renderer;
    impl_->ready = true;
    SDL_StartTextInput();
    if (outError) {
        outError->clear();
    }
    return true;
}

void EditorShell::shutdown() {
    if (!impl_ || !impl_->ready) {
        return;
    }
    SDL_StopTextInput();
    if (impl_->rendererBackend ==
        Impl::RendererBackend::OpenGL) {
        ImGui_ImplOpenGL3_Shutdown();
#if defined(_WIN32)
    } else if (
        impl_->rendererBackend ==
        Impl::RendererBackend::D3D12) {
        ImGui_ImplDX12_Shutdown();
        impl_->d3d12Descriptors.reset();
#endif
    } else if (
        impl_->rendererBackend ==
        Impl::RendererBackend::Vulkan) {
        ImGui_ImplVulkan_Shutdown();
    }
    ImGui::DestroyContext();
    impl_->window = nullptr;
    impl_->renderer = nullptr;
    impl_->ready = false;
}

void EditorShell::processEvent(const SDL_Event& event) {
    if (!impl_->ready) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_MOUSEMOTION:
            io.AddMousePosEvent(
                static_cast<float>(event.motion.x),
                static_cast<float>(event.motion.y));
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int button = -1;
            if (event.button.button == SDL_BUTTON_LEFT) button = 0;
            if (event.button.button == SDL_BUTTON_RIGHT) button = 1;
            if (event.button.button == SDL_BUTTON_MIDDLE) button = 2;
            if (event.button.button == SDL_BUTTON_X1) button = 3;
            if (event.button.button == SDL_BUTTON_X2) button = 4;
            if (button >= 0) {
                io.AddMouseButtonEvent(
                    button,
                    event.type == SDL_MOUSEBUTTONDOWN);
            }
            break;
        }
        case SDL_MOUSEWHEEL:
            io.AddMouseWheelEvent(
                static_cast<float>(event.wheel.x),
                static_cast<float>(event.wheel.y));
            break;
        case SDL_TEXTINPUT:
            io.AddInputCharactersUTF8(event.text.text);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool down = event.type == SDL_KEYDOWN;
            updateModifiers(
                io,
                static_cast<SDL_Keymod>(event.key.keysym.mod));
            const ImGuiKey key = imguiKey(event.key.keysym.sym);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, down);
                io.SetKeyEventNativeData(
                    key,
                    static_cast<int>(event.key.keysym.sym),
                    static_cast<int>(event.key.keysym.scancode));
            }
            break;
        }
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                io.AddFocusEvent(true);
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                io.AddFocusEvent(false);
            }
            break;
        default:
            break;
    }
}

void EditorShell::beginFrame(float deltaSeconds) {
    if (!impl_->ready) {
        return;
    }
    int windowWidth = 0;
    int windowHeight = 0;
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GetWindowSize(impl_->window, &windowWidth, &windowHeight);
    if (impl_->rendererBackend ==
        Impl::RendererBackend::OpenGL) {
        SDL_GL_GetDrawableSize(
            impl_->window,
            &drawableWidth,
            &drawableHeight);
    } else {
        drawableWidth = windowWidth;
        drawableHeight = windowHeight;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(windowWidth),
        static_cast<float>(windowHeight));
    if (windowWidth > 0 && windowHeight > 0) {
        io.DisplayFramebufferScale = ImVec2(
            static_cast<float>(drawableWidth) /
                static_cast<float>(windowWidth),
            static_cast<float>(drawableHeight) /
                static_cast<float>(windowHeight));
    }
    io.DeltaTime = std::max(deltaSeconds, 1.0f / 1000.0f);

    if (impl_->rendererBackend ==
        Impl::RendererBackend::OpenGL) {
        ImGui_ImplOpenGL3_NewFrame();
#if defined(_WIN32)
    } else if (
        impl_->rendererBackend ==
        Impl::RendererBackend::D3D12) {
        ImGui_ImplDX12_NewFrame();
#endif
    } else if (
        impl_->rendererBackend ==
        Impl::RendererBackend::Vulkan) {
        ImGui_ImplVulkan_NewFrame();
    }
    ImGui::NewFrame();
}

EditorShellActions EditorShell::drawProjectBrowser(
    const ProjectBrowserView& browser) {
    EditorShellActions actions;
    if (!impl_->ready) {
        return actions;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("PhlosionProjectBrowserHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) {
                actions.openProject = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                actions.exit = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Preferences...")) {
                impl_->pendingRendererPreference =
                    browser.rendererPreference;
                impl_->preferencesOpen = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextUnformatted("Phlosion Editor");
            ImGui::TextDisabled(
                "Open a phlosion.project.json to begin.");
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 cardSize(
        std::min(760.0f, std::max(420.0f, available.x - 80.0f)),
        std::min(560.0f, std::max(320.0f, available.y - 80.0f)));
    ImGui::SetCursorPos(ImVec2(
        std::max(20.0f, (available.x - cardSize.x) * 0.5f),
        std::max(20.0f, (available.y - cardSize.y) * 0.5f)));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ChildRounding,
        10.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(28.0f, 24.0f));
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(0.075f, 0.086f, 0.098f, 1.0f));
    if (ImGui::BeginChild(
            "PhlosionProjectBrowserCard",
            cardSize,
            true,
            ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextColored(
            ImVec4(0.35f, 0.90f, 0.58f, 1.0f),
            "PHLOSION");
        ImGui::SameLine();
        ImGui::TextDisabled("EDITOR");
        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.45f);
        ImGui::TextUnformatted("Create worlds. Open projects.");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled(
            "The Engine owns this editor; each game remains a separate project.");
        ImGui::TextDisabled(
            "Renderer: %s",
            text(browser.backendName).c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        if (ImGui::Button(
                "Open Project...",
                ImVec2(180.0f, 42.0f))) {
            actions.openProject = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Choose a tracked phlosion.project.json");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Recent projects");
        ImGui::Spacing();

        const auto* recent = browser.recentProjects;
        if (!recent || recent->empty()) {
            ImGui::TextDisabled(
                "No recent projects yet. You can also drop a project descriptor onto this window.");
        } else {
            for (std::size_t index = 0u;
                 index < recent->size();
                 ++index) {
                const std::string& path = (*recent)[index];
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(
                        path.c_str(),
                        false,
                        0,
                        ImVec2(0.0f, 34.0f))) {
                    actions.recentProjectIndex =
                        static_cast<int>(index);
                }
                ImGui::PopID();
            }
        }

        if (!browser.error.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(
                ImVec4(1.0f, 0.38f, 0.32f, 1.0f),
                "%s",
                text(browser.error).c_str());
        } else if (!browser.status.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled(
                "%s",
                text(browser.status).c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::End();

    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        actions.openProject = true;
    }
    drawPreferencesWindow(
        impl_->preferencesOpen,
        impl_->pendingRendererPreference,
        browser.rendererPreference,
        actions);
    return actions;
}

EditorShellActions EditorShell::drawWorkspace(
    const WorkspaceView& workspace) {
    EditorShellActions actions;
    if (!impl_->ready) {
        return actions;
    }
    actions.assetPreviewAnimationIndex =
        impl_->assetPreviewAnimationIndex;
    actions.assetPreviewPlaybackSpeed =
        impl_->assetPreviewPlaybackSpeed;
    actions.assetPreviewAnimationPlaying =
        impl_->assetPreviewAnimationPlaying;
    actions.assetPreviewShowMesh =
        impl_->assetPreviewShowMesh;
    actions.assetPreviewShowMaterials =
        impl_->assetPreviewShowMaterials;
    actions.assetPreviewShowTextures =
        impl_->assetPreviewShowTextures;
    actions.assetPreviewShowWireframe =
        impl_->assetPreviewShowWireframe;
    actions.assetPreviewShowSkeleton =
        impl_->assetPreviewShowSkeleton;

    const auto layoutObjectIdAt =
        [&](int index) -> const std::string* {
            if (!workspace.layoutObjects || index < 0 ||
                static_cast<std::size_t>(index) >=
                    workspace.layoutObjects->size()) {
                return nullptr;
            }
            return &(*workspace.layoutObjects)[
                static_cast<std::size_t>(index)]
                .stableId;
        };
    const auto layoutIndexSelected =
        [&](int index) {
            const auto* id = layoutObjectIdAt(index);
            return id && std::find(
                impl_->selectedLayoutObjectIds.begin(),
                impl_->selectedLayoutObjectIds.end(),
                *id) !=
                impl_->selectedLayoutObjectIds.end();
        };
    const auto selectOnlyLayoutIndex =
        [&](int index) {
            impl_->selectedLayoutObjectIds.clear();
            if (const auto* id = layoutObjectIdAt(index)) {
                impl_->selectedLayoutObjectIds.push_back(*id);
                impl_->selectedLayoutObject = index;
            } else {
                impl_->selectedLayoutObject = -1;
            }
        };
    const auto toggleLayoutIndex =
        [&](int index) {
            const auto* id = layoutObjectIdAt(index);
            if (!id) {
                return;
            }
            const auto found = std::find(
                impl_->selectedLayoutObjectIds.begin(),
                impl_->selectedLayoutObjectIds.end(),
                *id);
            if (found ==
                impl_->selectedLayoutObjectIds.end()) {
                impl_->selectedLayoutObjectIds.push_back(*id);
                impl_->selectedLayoutObject = index;
            } else {
                impl_->selectedLayoutObjectIds.erase(found);
                if (impl_->selectedLayoutObject == index) {
                    impl_->selectedLayoutObject = -1;
                    if (!impl_->selectedLayoutObjectIds.empty() &&
                        workspace.layoutObjects) {
                        const auto next = std::find_if(
                            workspace.layoutObjects->begin(),
                            workspace.layoutObjects->end(),
                            [&](const WorkspaceLayoutObject& object) {
                                return object.stableId ==
                                    impl_->selectedLayoutObjectIds.back();
                            });
                        if (next !=
                            workspace.layoutObjects->end()) {
                            impl_->selectedLayoutObject =
                                static_cast<int>(std::distance(
                                    workspace.layoutObjects->begin(),
                                    next));
                        }
                    }
                }
            }
        };
    const auto writeSelectedLayoutIndices = [&]() {
        actions.editLayoutObjectIndices.clear();
        if (!workspace.layoutObjects) {
            return;
        }
        for (std::size_t index = 0u;
             index < workspace.layoutObjects->size();
             ++index) {
            if (layoutIndexSelected(
                    static_cast<int>(index))) {
                actions.editLayoutObjectIndices.push_back(
                    static_cast<int>(index));
            }
        }
    };
    if (workspace.layoutObjects) {
        std::erase_if(
            impl_->selectedLayoutObjectIds,
            [&](const std::string& id) {
                return std::none_of(
                    workspace.layoutObjects->begin(),
                    workspace.layoutObjects->end(),
                    [&](const WorkspaceLayoutObject& object) {
                        return object.stableId == id;
                    });
            });
        if (impl_->selectedLayoutObject >= 0 &&
            !layoutIndexSelected(
                impl_->selectedLayoutObject)) {
            const auto* primary = layoutObjectIdAt(
                impl_->selectedLayoutObject);
            if (!primary ||
                std::find(
                    impl_->selectedLayoutObjectIds.begin(),
                    impl_->selectedLayoutObjectIds.end(),
                    *primary) ==
                    impl_->selectedLayoutObjectIds.end()) {
                impl_->selectedLayoutObject = -1;
            }
        }
    } else {
        impl_->selectedLayoutObjectIds.clear();
        impl_->selectedLayoutObject = -1;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::Begin("PhlosionDockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceId =
        ImGui::GetID("PhlosionEditorDockspaceV3");
    if (impl_->firstLayout &&
        ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        impl_->firstLayout = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(
            dockspaceId,
            ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(
            dockspaceId,
            viewport->WorkSize);

        ImGuiID center = dockspaceId;
        ImGuiID left = 0u;
        ImGuiID right = 0u;
        ImGuiID bottom = 0u;
        left = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Left, 0.20f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Right, 0.25f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Down, 0.25f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
        ImGui::DockBuilderDockWindow("Game Preview", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Scenes", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(
        dockspaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_None);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) {
                actions.openProject = true;
            }
            if (ImGui::MenuItem("Close Project")) {
                actions.closeProject = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                actions.exit = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem(
                    "Undo Scene Edit",
                    "Ctrl+Z",
                    false,
                    workspace.canUndoSceneEdit)) {
                actions.undoSceneEditRequested = true;
            }
            if (ImGui::MenuItem(
                    "Redo Scene Edit",
                    "Ctrl+Y",
                    false,
                    workspace.canRedoSceneEdit)) {
                actions.redoSceneEditRequested = true;
            }
            ImGui::Separator();
            const bool hasLayoutSelection =
                !impl_->selectedLayoutObjectIds.empty() &&
                impl_->selectedLayoutObject >= 0 &&
                workspace.layoutObjects &&
                static_cast<std::size_t>(
                    impl_->selectedLayoutObject) <
                    workspace.layoutObjects->size();
            if (ImGui::MenuItem(
                    "Duplicate Selected Object",
                    "Ctrl+D",
                    false,
                    hasLayoutSelection)) {
                actions.editLayoutObjectIndex =
                    impl_->selectedLayoutObject;
                actions.layoutObjectDuplicateRequested = true;
            }
            if (ImGui::MenuItem(
                    impl_->selectedLayoutObjectIds.size() > 1u
                        ? "Delete Selected Objects"
                        : "Delete Selected Object",
                    "Delete",
                    false,
                    hasLayoutSelection)) {
                actions.editLayoutObjectIndex =
                    impl_->selectedLayoutObject;
                writeSelectedLayoutIndices();
                actions.layoutObjectDeleteRequested = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) {
                impl_->pendingRendererPreference =
                    workspace.rendererPreference;
                impl_->preferencesOpen = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Play")) {
            if (workspace.playState ==
                EditorPlayState::Editing) {
                if (ImGui::MenuItem(
                        "Play",
                        "Ctrl+P")) {
                    actions.togglePlay = true;
                }
            } else {
                if (ImGui::MenuItem(
                        "Stop",
                        "Ctrl+P")) {
                    actions.togglePlay = true;
                }
            }
            if (ImGui::MenuItem(
                    workspace.playState ==
                            EditorPlayState::Paused
                        ? "Resume"
                        : "Pause",
                    "Ctrl+Shift+P",
                    false,
                    workspace.playState !=
                        EditorPlayState::Editing)) {
                actions.togglePause = true;
            }
            if (ImGui::MenuItem(
                    "Step",
                    "Ctrl+Alt+P",
                    false,
                    workspace.playState ==
                        EditorPlayState::Paused)) {
                actions.step = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout")) {
                ImGui::DockBuilderRemoveNode(dockspaceId);
                impl_->firstLayout = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextUnformatted("Phlosion Editor milestone 0");
            ImGui::EndMenu();
        }
        const std::string title =
            text(workspace.projectName) + "  /  " +
            text(workspace.sceneId);
        const float titleWidth = ImGui::CalcTextSize(title.c_str()).x;
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > titleWidth + 16.0f) {
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (available - titleWidth) * 0.5f);
        }
        ImGui::TextDisabled("%s", title.c_str());
        ImGui::EndMenuBar();
    }
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool hasLayoutSelection =
            !impl_->selectedLayoutObjectIds.empty() &&
            impl_->selectedLayoutObject >= 0 &&
            workspace.layoutObjects &&
            static_cast<std::size_t>(
                impl_->selectedLayoutObject) <
                workspace.layoutObjects->size();
        if (!io.WantTextInput && io.KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                actions.redoSceneEditRequested =
                    workspace.canRedoSceneEdit;
            } else {
                actions.undoSceneEditRequested =
                    workspace.canUndoSceneEdit;
            }
        }
        if (!io.WantTextInput && io.KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            actions.redoSceneEditRequested =
                workspace.canRedoSceneEdit;
        }
        if (!io.WantTextInput && io.KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_D, false) &&
            hasLayoutSelection) {
            actions.editLayoutObjectIndex =
                impl_->selectedLayoutObject;
            actions.layoutObjectDuplicateRequested = true;
        }
        if (!io.WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
            hasLayoutSelection) {
            actions.editLayoutObjectIndex =
                impl_->selectedLayoutObject;
            writeSelectedLayoutIndices();
            actions.layoutObjectDeleteRequested = true;
        }
    }
    ImGui::End();

    const bool viewportVisible = ImGui::Begin(
        "Viewport",
        nullptr,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);
    if (viewportVisible) {
        if (workspace.focusActiveViewport) {
            impl_->selectedViewport =
                workspace.activeViewport;
        }
        if (ImGui::Selectable(
                "Scene",
                impl_->selectedViewport ==
                    EditorViewportKind::Scene,
                0,
                ImVec2(72.0f, 22.0f))) {
            impl_->selectedViewport =
                EditorViewportKind::Scene;
        }
        ImGui::SameLine();
        if (ImGui::Selectable(
                "Game",
                impl_->selectedViewport ==
                    EditorViewportKind::Game,
                0,
                ImVec2(72.0f, 22.0f))) {
            impl_->selectedViewport =
                EditorViewportKind::Game;
        }
        if (impl_->selectedViewport ==
                EditorViewportKind::Scene &&
            workspace.terrainTileEditingSupported) {
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(12.0f, 0.0f));
            ImGui::SameLine();
            if (impl_->terrainTileEditing) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(0.52f, 0.31f, 0.08f, 1.0f));
            }
            if (ImGui::Button(
                    impl_->terrainTileEditing
                        ? "Tiles: ON"
                        : "Tiles: OFF",
                    ImVec2(88.0f, 22.0f))) {
                impl_->terrainTileEditing =
                    !impl_->terrainTileEditing;
                impl_->terrainTileDragging = false;
                impl_->layoutBoxSelecting = false;
                impl_->layoutGizmoDragging = false;
            }
            if (impl_->terrainTileEditing) {
                ImGui::PopStyleColor();
            }
        }
        if (impl_->selectedViewport ==
                EditorViewportKind::Scene &&
            !impl_->terrainTileEditing &&
            workspace.layoutObjects &&
            !workspace.layoutObjects->empty()) {
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(12.0f, 0.0f));
            ImGui::SameLine();
            const auto gizmoButton =
                [&](const char* label,
                    LayoutGizmoOperation operation) {
                    const bool selected =
                        impl_->layoutGizmoOperation ==
                        operation;
                    if (selected) {
                        ImGui::PushStyleColor(
                            ImGuiCol_Button,
                            ImVec4(
                                0.12f, 0.42f, 0.30f, 1.0f));
                    }
                    if (ImGui::Button(
                            label,
                            ImVec2(70.0f, 22.0f))) {
                        impl_->layoutGizmoOperation =
                            operation;
                    }
                    if (selected) {
                        ImGui::PopStyleColor();
                    }
                };
            gizmoButton(
                "W  Move",
                LayoutGizmoOperation::Translate);
            ImGui::SameLine();
            gizmoButton(
                "E  Rotate",
                LayoutGizmoOperation::Rotate);
            ImGui::SameLine();
            gizmoButton(
                "R  Scale",
                LayoutGizmoOperation::Scale);
            const ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsWindowFocused(
                    ImGuiFocusedFlags_RootAndChildWindows) &&
                !io.WantTextInput) {
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                    impl_->layoutGizmoOperation =
                        LayoutGizmoOperation::Translate;
                } else if (
                    ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                    impl_->layoutGizmoOperation =
                        LayoutGizmoOperation::Rotate;
                } else if (
                    ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                    impl_->layoutGizmoOperation =
                        LayoutGizmoOperation::Scale;
                }
            }
        }
        ImGui::Separator();

        const EditorViewportKind kind =
            impl_->selectedViewport;
        const std::uint64_t textureId =
            kind == EditorViewportKind::Game
                ? workspace.gameTextureId
                : workspace.sceneTextureId;
        const ImVec2 available =
            ImGui::GetContentRegionAvail();
        const ImVec2 origin =
            ImGui::GetCursorScreenPos();
        const int viewportWidth = std::max(
            1,
            static_cast<int>(available.x));
        const int viewportHeight = std::max(
            1,
            static_cast<int>(available.y));
        if (textureId != 0u) {
            const ImVec2 uv0 =
                workspace.flipRenderSurfaceTexturesVertically
                    ? ImVec2(0.0f, 1.0f)
                    : ImVec2(0.0f, 0.0f);
            const ImVec2 uv1 =
                workspace.flipRenderSurfaceTexturesVertically
                    ? ImVec2(1.0f, 0.0f)
                    : ImVec2(1.0f, 1.0f);
            ImGui::Image(
                static_cast<ImTextureID>(textureId),
                ImVec2(
                    static_cast<float>(viewportWidth),
                    static_cast<float>(viewportHeight)),
                uv0,
                uv1);
        } else {
            ImGui::Dummy(ImVec2(
                static_cast<float>(viewportWidth),
                static_cast<float>(viewportHeight)));
        }
        const bool imageHovered = ImGui::IsItemHovered();
        if (kind == EditorViewportKind::Scene &&
            workspace.playState == EditorPlayState::Editing &&
            impl_->terrainTileEditing &&
            workspace.terrainTiles &&
            !workspace.terrainTiles->empty()) {
            const auto& tiles = *workspace.terrainTiles;
            const auto sameCoordinate =
                [](const EditorProjectTerrainTileCoordinate& left,
                   const EditorProjectTerrainTileCoordinate& right) {
                    return left.gridX == right.gridX &&
                        left.gridZ == right.gridZ;
                };
            std::erase_if(
                impl_->selectedTerrainTiles,
                [&](const auto& selected) {
                    return std::none_of(
                        tiles.begin(),
                        tiles.end(),
                        [&](const WorkspaceTerrainTile& tile) {
                            return sameCoordinate(
                                tile.coordinate,
                                selected);
                        });
                });
            const auto selectedCoordinate =
                [&](const auto& coordinate) {
                    return std::any_of(
                        impl_->selectedTerrainTiles.begin(),
                        impl_->selectedTerrainTiles.end(),
                        [&](const auto& selected) {
                            return sameCoordinate(
                                selected,
                                coordinate);
                        });
                };
            ImDrawList* drawList =
                ImGui::GetWindowDrawList();
            drawList->PushClipRect(
                origin,
                ImVec2(
                    origin.x + static_cast<float>(viewportWidth),
                    origin.y + static_cast<float>(viewportHeight)),
                true);
            const ImVec2 mouse = ImGui::GetMousePos();
            int hoveredTile = -1;
            for (std::size_t index = 0u;
                 index < tiles.size();
                 ++index) {
                const auto& tile = tiles[index];
                if (!tile.viewportVisible) {
                    continue;
                }
                std::array<ImVec2, 4> corners{};
                for (std::size_t corner = 0u;
                     corner < corners.size();
                     ++corner) {
                    corners[corner] = ImVec2(
                        origin.x +
                            tile.viewportCorners[corner * 2u],
                        origin.y +
                            tile.viewportCorners[corner * 2u + 1u]);
                }
                if (screenPointInQuad(mouse, corners)) {
                    hoveredTile = static_cast<int>(index);
                }
                const bool selected =
                    selectedCoordinate(tile.coordinate);
                const ImU32 outline = selected
                    ? IM_COL32(255, 220, 72, 245)
                    : tile.authored
                    ? IM_COL32(255, 154, 48, 220)
                    : tile.sourceOccupied
                    ? IM_COL32(75, 218, 162, 125)
                    : IM_COL32(135, 148, 158, 70);
                if (selected || tile.authored) {
                    drawList->AddQuadFilled(
                        corners[0],
                        corners[1],
                        corners[2],
                        corners[3],
                        selected
                            ? IM_COL32(255, 205, 45, 38)
                            : IM_COL32(255, 130, 35, 22));
                }
                drawList->AddQuad(
                    corners[0],
                    corners[1],
                    corners[2],
                    corners[3],
                    outline,
                    selected ? 2.2f : 1.0f);
            }
            if (hoveredTile >= 0) {
                const auto& tile = tiles[
                    static_cast<std::size_t>(hoveredTile)];
                std::array<ImVec2, 4> corners{};
                for (std::size_t corner = 0u;
                     corner < corners.size();
                     ++corner) {
                    corners[corner] = ImVec2(
                        origin.x +
                            tile.viewportCorners[corner * 2u],
                        origin.y +
                            tile.viewportCorners[corner * 2u + 1u]);
                }
                drawList->AddQuad(
                    corners[0],
                    corners[1],
                    corners[2],
                    corners[3],
                    IM_COL32(255, 255, 255, 235),
                    2.0f);
            }
            const bool canInteract =
                imageHovered && !ImGui::GetIO().WantTextInput;
            if (canInteract && hoveredTile >= 0 &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                impl_->terrainTileDragging = true;
                impl_->terrainTileDragStart =
                    tiles[static_cast<std::size_t>(hoveredTile)]
                        .coordinate;
            }
            if (impl_->terrainTileDragging &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                const auto end = hoveredTile >= 0
                    ? tiles[static_cast<std::size_t>(hoveredTile)]
                          .coordinate
                    : impl_->terrainTileDragStart;
                const std::int32_t minimumX = std::min(
                    impl_->terrainTileDragStart.gridX,
                    end.gridX);
                const std::int32_t maximumX = std::max(
                    impl_->terrainTileDragStart.gridX,
                    end.gridX);
                const std::int32_t minimumZ = std::min(
                    impl_->terrainTileDragStart.gridZ,
                    end.gridZ);
                const std::int32_t maximumZ = std::max(
                    impl_->terrainTileDragStart.gridZ,
                    end.gridZ);
                if (!ImGui::GetIO().KeyCtrl &&
                    !ImGui::GetIO().KeyShift) {
                    impl_->selectedTerrainTiles.clear();
                }
                for (const auto& tile : tiles) {
                    if (!tile.viewportVisible ||
                        tile.coordinate.gridX < minimumX ||
                        tile.coordinate.gridX > maximumX ||
                        tile.coordinate.gridZ < minimumZ ||
                        tile.coordinate.gridZ > maximumZ) {
                        continue;
                    }
                    const auto found = std::find_if(
                        impl_->selectedTerrainTiles.begin(),
                        impl_->selectedTerrainTiles.end(),
                        [&](const auto& selected) {
                            return sameCoordinate(
                                selected,
                                tile.coordinate);
                        });
                    if (ImGui::GetIO().KeyCtrl &&
                        minimumX == maximumX &&
                        minimumZ == maximumZ &&
                        found !=
                            impl_->selectedTerrainTiles.end()) {
                        impl_->selectedTerrainTiles.erase(found);
                    } else if (
                        found ==
                        impl_->selectedTerrainTiles.end()) {
                        impl_->selectedTerrainTiles.push_back(
                            tile.coordinate);
                    }
                }
                impl_->terrainTileDragging = false;
            }
            drawList->AddText(
                ImVec2(origin.x + 12.0f, origin.y + 12.0f),
                IM_COL32(255, 240, 205, 235),
                impl_->terrainTileDragging
                    ? "TERRAIN TILES - drag across cells to select a rectangle"
                    : "TERRAIN TILES - click or drag cells; Ctrl/Shift adds to selection");
            drawList->PopClipRect();
        } else if (impl_->terrainTileDragging) {
            impl_->terrainTileDragging = false;
        }
        if (kind == EditorViewportKind::Scene &&
            workspace.playState == EditorPlayState::Editing &&
            !impl_->terrainTileEditing &&
            workspace.layoutObjects &&
            !workspace.layoutObjects->empty()) {
            const auto& objects = *workspace.layoutObjects;
            impl_->selectedLayoutObject = std::clamp(
                impl_->selectedLayoutObject,
                -1,
                static_cast<int>(objects.size() - 1u));
            ImDrawList* drawList =
                ImGui::GetWindowDrawList();
            const ImVec2 mouse = ImGui::GetMousePos();
            int hoveredObject = -1;
            float hoveredObjectDistance =
                std::numeric_limits<float>::max();
            for (std::size_t index = 0u;
                 index < objects.size();
                 ++index) {
                const auto& object = objects[index];
                if (!object.viewportVisible) {
                    continue;
                }
                const ImVec2 position(
                    origin.x + object.viewportPosition[0],
                    origin.y + object.viewportPosition[1]);
                const float distance =
                    screenDistanceSquared(mouse, position);
                if (distance < 16.0f * 16.0f &&
                    distance < hoveredObjectDistance) {
                    hoveredObject =
                        static_cast<int>(index);
                    hoveredObjectDistance = distance;
                }
                const bool selected =
                    layoutIndexSelected(
                        static_cast<int>(index));
                const bool hovered =
                    hoveredObject ==
                    static_cast<int>(index);
                const ImU32 markerColor =
                    object.suppressed
                    ? IM_COL32(255, 90, 70, 230)
                    : selected
                    ? IM_COL32(255, 225, 72, 245)
                    : hovered
                    ? IM_COL32(255, 255, 255, 235)
                    : IM_COL32(70, 220, 155, 180);
                drawList->AddCircle(
                    position,
                    selected ? 8.0f : 5.0f,
                    markerColor,
                    16,
                    selected ? 2.5f : 1.5f);
                if (object.suppressed) {
                    drawList->AddLine(
                        ImVec2(
                            position.x - 4.0f,
                            position.y - 4.0f),
                        ImVec2(
                            position.x + 4.0f,
                            position.y + 4.0f),
                        markerColor,
                        1.5f);
                    drawList->AddLine(
                        ImVec2(
                            position.x + 4.0f,
                            position.y - 4.0f),
                        ImVec2(
                            position.x - 4.0f,
                            position.y + 4.0f),
                        markerColor,
                        1.5f);
                }
            }

            int hoveredAxis = -1;
            const WorkspaceLayoutObject* selected = nullptr;
            ImVec2 gizmoCenter{};
            std::array<ImVec2, 3> axisDirections{};
            if (impl_->selectedLayoutObject >= 0 &&
                static_cast<std::size_t>(
                    impl_->selectedLayoutObject) <
                    objects.size()) {
                selected =
                    &objects[static_cast<std::size_t>(
                        impl_->selectedLayoutObject)];
                if (selected->viewportVisible) {
                    gizmoCenter = ImVec2(
                        origin.x +
                            selected->viewportPosition[0],
                        origin.y +
                            selected->viewportPosition[1]);
                    for (int axis = 0; axis < 3; ++axis) {
                        axisDirections[
                            static_cast<std::size_t>(axis)] =
                            normalizedScreenDirection(
                                selected->
                                    viewportAxisDirections[
                                        static_cast<
                                            std::size_t>(
                                            axis * 2)],
                                selected->
                                    viewportAxisDirections[
                                        static_cast<
                                            std::size_t>(
                                            axis * 2 + 1)]);
                    }
                    constexpr float axisLength = 62.0f;
                    if (impl_->layoutGizmoOperation ==
                        LayoutGizmoOperation::Rotate) {
                        constexpr float ringRadius = 50.0f;
                        constexpr int segments = 40;
                        float closest =
                            std::numeric_limits<float>::max();
                        for (int axis = 0;
                             axis < 3;
                             ++axis) {
                            const int first = (axis + 1) % 3;
                            const int second = (axis + 2) % 3;
                            ImVec2 previous{};
                            for (int segment = 0;
                                 segment <= segments;
                                 ++segment) {
                                const float angle =
                                    static_cast<float>(segment) /
                                    static_cast<float>(segments) *
                                    6.283185307f;
                                const ImVec2 current(
                                    gizmoCenter.x +
                                        (axisDirections[
                                             static_cast<
                                                 std::size_t>(
                                                 first)]
                                                 .x *
                                             std::cos(angle) +
                                         axisDirections[
                                             static_cast<
                                                 std::size_t>(
                                                 second)]
                                                 .x *
                                             std::sin(angle)) *
                                            ringRadius,
                                    gizmoCenter.y +
                                        (axisDirections[
                                             static_cast<
                                                 std::size_t>(
                                                 first)]
                                                 .y *
                                             std::cos(angle) +
                                         axisDirections[
                                             static_cast<
                                                 std::size_t>(
                                                 second)]
                                                 .y *
                                             std::sin(angle)) *
                                            ringRadius);
                                if (segment > 0) {
                                    const float distance =
                                        screenDistanceToSegmentSquared(
                                            mouse,
                                            previous,
                                            current);
                                    if (distance < closest) {
                                        closest = distance;
                                        hoveredAxis = axis;
                                    }
                                    drawList->AddLine(
                                        previous,
                                        current,
                                        gizmoAxisColor(
                                            axis,
                                            false),
                                        2.0f);
                                }
                                previous = current;
                            }
                        }
                        if (closest > 9.0f * 9.0f) {
                            hoveredAxis = -1;
                        }
                    } else {
                        float closest =
                            std::numeric_limits<float>::max();
                        for (int axis = 0;
                             axis < 3;
                             ++axis) {
                            const ImVec2 direction =
                                axisDirections[
                                    static_cast<std::size_t>(
                                        axis)];
                            const ImVec2 endpoint(
                                gizmoCenter.x +
                                    direction.x * axisLength,
                                gizmoCenter.y +
                                    direction.y * axisLength);
                            const float distance =
                                screenDistanceToSegmentSquared(
                                    mouse,
                                    gizmoCenter,
                                    endpoint);
                            if (distance < closest) {
                                closest = distance;
                                hoveredAxis = axis;
                            }
                            drawList->AddLine(
                                gizmoCenter,
                                endpoint,
                                gizmoAxisColor(axis, false),
                                3.0f);
                            if (impl_->layoutGizmoOperation ==
                                LayoutGizmoOperation::Scale) {
                                drawList->AddRectFilled(
                                    ImVec2(
                                        endpoint.x - 5.0f,
                                        endpoint.y - 5.0f),
                                    ImVec2(
                                        endpoint.x + 5.0f,
                                        endpoint.y + 5.0f),
                                    gizmoAxisColor(
                                        axis,
                                        hoveredAxis == axis));
                            } else {
                                drawList->AddCircleFilled(
                                    endpoint,
                                    5.0f,
                                    gizmoAxisColor(
                                        axis,
                                        hoveredAxis == axis),
                                    12);
                            }
                        }
                        if (closest > 10.0f * 10.0f) {
                            hoveredAxis = -1;
                        }
                    }
                    drawList->AddCircleFilled(
                        gizmoCenter,
                        4.0f,
                        IM_COL32(245, 245, 245, 240),
                        12);
                }
            }

            const bool canInteract =
                imageHovered &&
                !ImGui::GetIO().WantTextInput;
            if (!impl_->layoutGizmoDragging &&
                canInteract &&
                ImGui::IsMouseClicked(
                    ImGuiMouseButton_Left)) {
                if (selected && hoveredAxis >= 0) {
                    impl_->layoutGizmoDragging = true;
                    impl_->layoutGizmoAxis =
                        hoveredAxis;
                    impl_->layoutGizmoDragStart = mouse;
                    impl_->layoutGizmoDragDirection =
                        axisDirections[
                            static_cast<std::size_t>(
                                hoveredAxis)];
                    impl_->
                        layoutGizmoSourceUnitsPerPixel =
                        std::max(
                            0.0001f,
                            selected->
                                viewportSourceUnitsPerPixel[
                                    static_cast<std::size_t>(
                                        hoveredAxis)]);
                    impl_->layoutGizmoStartTranslation =
                        selected->translation;
                    impl_->layoutGizmoStartRotation =
                        selected->rotationDegrees;
                    impl_->layoutGizmoStartScale =
                        selected->scale;
                } else if (hoveredObject >= 0) {
                    const ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyCtrl || io.KeyShift) {
                        toggleLayoutIndex(hoveredObject);
                    } else {
                        selectOnlyLayoutIndex(hoveredObject);
                    }
                    impl_->inspectorSelection =
                        InspectorSelectionDomain::Hierarchy;
                    impl_->activeLayoutObjectId.clear();
                    if (layoutIndexSelected(hoveredObject)) {
                        actions.selectLayoutObjectIndex =
                            hoveredObject;
                    }
                    if (workspace.hierarchyItems) {
                        for (std::size_t hierarchyIndex = 0u;
                             hierarchyIndex <
                                 workspace.hierarchyItems->size();
                             ++hierarchyIndex) {
                            if ((*workspace.hierarchyItems)[
                                    hierarchyIndex]
                                    .layoutObjectIndex ==
                                hoveredObject) {
                                impl_->selectedHierarchyItem =
                                    static_cast<int>(
                                        hierarchyIndex);
                                break;
                            }
                        }
                    }
                } else {
                    impl_->layoutBoxSelecting = true;
                    impl_->layoutBoxSelectStart = mouse;
                    if (!ImGui::GetIO().KeyCtrl &&
                        !ImGui::GetIO().KeyShift) {
                        impl_->selectedLayoutObjectIds.clear();
                        impl_->selectedLayoutObject = -1;
                    }
                }
            }

            if (impl_->layoutBoxSelecting) {
                const ImVec2 minimum(
                    std::min(
                        impl_->layoutBoxSelectStart.x,
                        mouse.x),
                    std::min(
                        impl_->layoutBoxSelectStart.y,
                        mouse.y));
                const ImVec2 maximum(
                    std::max(
                        impl_->layoutBoxSelectStart.x,
                        mouse.x),
                    std::max(
                        impl_->layoutBoxSelectStart.y,
                        mouse.y));
                drawList->AddRectFilled(
                    minimum,
                    maximum,
                    IM_COL32(50, 170, 120, 36));
                drawList->AddRect(
                    minimum,
                    maximum,
                    IM_COL32(90, 235, 170, 230),
                    0.0f,
                    0,
                    1.5f);
                if (ImGui::IsMouseReleased(
                        ImGuiMouseButton_Left)) {
                    for (std::size_t index = 0u;
                         index < objects.size();
                         ++index) {
                        const auto& object = objects[index];
                        if (!object.viewportVisible) {
                            continue;
                        }
                        const ImVec2 position(
                            origin.x +
                                object.viewportPosition[0],
                            origin.y +
                                object.viewportPosition[1]);
                        if (position.x < minimum.x ||
                            position.x > maximum.x ||
                            position.y < minimum.y ||
                            position.y > maximum.y) {
                            continue;
                        }
                        if (!layoutIndexSelected(
                                static_cast<int>(index))) {
                            toggleLayoutIndex(
                                static_cast<int>(index));
                        }
                    }
                    impl_->layoutBoxSelecting = false;
                    impl_->activeLayoutObjectId.clear();
                    if (impl_->selectedLayoutObject >= 0) {
                        actions.selectLayoutObjectIndex =
                            impl_->selectedLayoutObject;
                    }
                }
            }

            if (impl_->layoutGizmoDragging &&
                selected) {
                actions.editLayoutObjectIndex =
                    impl_->selectedLayoutObject;
                if (ImGui::IsKeyPressed(
                        ImGuiKey_Escape,
                        false)) {
                    actions.layoutObjectCancelRequested = true;
                    impl_->layoutGizmoDragging = false;
                    impl_->activeLayoutObjectId.clear();
                } else {
                    const ImVec2 delta(
                        mouse.x -
                            impl_->layoutGizmoDragStart.x,
                        mouse.y -
                            impl_->layoutGizmoDragStart.y);
                    const float projectedPixels =
                        delta.x *
                            impl_->layoutGizmoDragDirection.x +
                        delta.y *
                            impl_->layoutGizmoDragDirection.y;
                    actions.layoutTranslation =
                        impl_->
                            layoutGizmoStartTranslation;
                    actions.layoutRotationDegrees =
                        impl_->layoutGizmoStartRotation;
                    actions.layoutScale =
                        impl_->layoutGizmoStartScale;
                    actions.layoutSuppressed =
                        selected->suppressed;
                    const std::size_t axis =
                        static_cast<std::size_t>(
                            impl_->layoutGizmoAxis);
                    if (impl_->layoutGizmoOperation ==
                        LayoutGizmoOperation::Translate) {
                        float sourceDelta =
                            projectedPixels *
                            impl_->
                                layoutGizmoSourceUnitsPerPixel;
                        if (ImGui::GetIO().KeyCtrl) {
                            sourceDelta =
                                std::round(sourceDelta / 5.0f) *
                                5.0f;
                        }
                        actions.layoutTranslation[axis] +=
                            sourceDelta;
                        if (selected->targetKind ==
                                "gameplay_board") {
                            const float snapStep =
                                axis == 1u
                                ? 50.0f
                                : 100.0f;
                            actions.layoutTranslation[axis] =
                                std::round(
                                    actions.layoutTranslation[axis] /
                                    snapStep) *
                                snapStep;
                        }
                    } else if (
                        impl_->layoutGizmoOperation ==
                        LayoutGizmoOperation::Rotate) {
                        float degrees =
                            projectedPixels * 0.6f;
                        if (ImGui::GetIO().KeyCtrl) {
                            degrees =
                                std::round(degrees / 15.0f) *
                                15.0f;
                        }
                        actions.layoutRotationDegrees[axis] +=
                            degrees;
                    } else {
                        float scaleDelta =
                            projectedPixels * 0.0125f;
                        if (ImGui::GetIO().KeyCtrl) {
                            scaleDelta =
                                std::round(
                                    scaleDelta / 0.1f) *
                                0.1f;
                        }
                        if (selected->targetKind ==
                                "gameplay_board") {
                            const float tileSize = std::clamp(
                                std::round(
                                    (actions.layoutScale[axis] +
                                     scaleDelta) /
                                    0.05f) *
                                    0.05f,
                                0.25f,
                                4.0f);
                            actions.layoutScale = {
                                tileSize,
                                tileSize,
                                tileSize};
                        } else {
                            actions.layoutScale[axis] =
                                std::max(
                                    0.01f,
                                    actions.layoutScale[axis] +
                                        scaleDelta);
                        }
                    }
                    impl_->layoutTranslation =
                        actions.layoutTranslation;
                    impl_->layoutRotationDegrees =
                        actions.layoutRotationDegrees;
                    impl_->layoutScale =
                        actions.layoutScale;
                    impl_->layoutSuppressed =
                        actions.layoutSuppressed;
                    actions.layoutObjectPreviewRequested =
                        true;
                    if (ImGui::IsMouseReleased(
                            ImGuiMouseButton_Left)) {
                        actions.layoutObjectCommitRequested =
                            true;
                        impl_->layoutGizmoDragging = false;
                        impl_->activeLayoutObjectId.clear();
                    }
                }
            }

            drawList->AddText(
                ImVec2(origin.x + 12.0f, origin.y + 12.0f),
                IM_COL32(235, 245, 240, 220),
                impl_->layoutGizmoDragging
                    ? "LIVE EDIT - release to autosave, Esc to cancel"
                    : impl_->layoutBoxSelecting
                    ? "BOX SELECT - release to select enclosed objects"
                    : "EDIT MODE - click, Ctrl/Shift-click, or drag empty space; W/E/R edits primary");
        } else if (
            impl_->layoutGizmoDragging ||
            impl_->layoutBoxSelecting) {
            impl_->layoutGizmoDragging = false;
            impl_->layoutBoxSelecting = false;
        }
        actions.activeViewport = kind;
        actions.viewportWidth = viewportWidth;
        actions.viewportHeight = viewportHeight;
        actions.viewportScreenX = origin.x;
        actions.viewportScreenY = origin.y;
        actions.viewportHovered =
            ImGui::IsWindowHovered(
                ImGuiHoveredFlags_RootAndChildWindows);
        actions.viewportFocused =
            ImGui::IsWindowFocused(
                ImGuiFocusedFlags_RootAndChildWindows);
    }
    ImGui::End();

    const ImVec2 toolbarSize(294.0f, 42.0f);
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x +
                (viewport->WorkSize.x - toolbarSize.x) * 0.5f,
            viewport->WorkPos.y + 28.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(toolbarSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    constexpr ImGuiWindowFlags toolbarFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("##PhlosionPlayToolbar", nullptr, toolbarFlags);
    const bool editing =
        workspace.playState == EditorPlayState::Editing;
    if (ImGui::Button(
            editing ? "Play" : "Stop",
            ImVec2(70.0f, 25.0f))) {
        actions.togglePlay = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editing);
    if (ImGui::Button(
            workspace.playState == EditorPlayState::Paused
                ? "Resume"
                : "Pause",
            ImVec2(70.0f, 25.0f))) {
        actions.togglePause = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(
        workspace.playState != EditorPlayState::Paused);
    if (ImGui::Button("Step", ImVec2(52.0f, 25.0f))) {
        actions.step = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const char* stateLabel = "EDIT";
    ImVec4 stateColor(0.68f, 0.72f, 0.76f, 1.0f);
    if (workspace.playState == EditorPlayState::Playing) {
        stateLabel = "PLAY";
        stateColor = ImVec4(0.35f, 0.90f, 0.58f, 1.0f);
    } else if (
        workspace.playState == EditorPlayState::Paused) {
        stateLabel = "PAUSED";
        stateColor = ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
    }
    ImGui::TextColored(stateColor, "%s", stateLabel);
    ImGui::End();

    ImGui::Begin("Scene Hierarchy");
    ImGui::TextDisabled("%s", text(workspace.sceneId).c_str());
    ImGui::Separator();
    if (!workspace.hierarchyItems ||
        workspace.hierarchyItems->empty()) {
        ImGui::TextWrapped(
            "The scene adapter has not exposed hierarchy items.");
    } else {
        impl_->selectedHierarchyItem = std::clamp(
            impl_->selectedHierarchyItem,
            0,
            static_cast<int>(
                workspace.hierarchyItems->size() - 1u));
        int collapsedDepth = -1;
        for (std::size_t index = 0u;
             index < workspace.hierarchyItems->size();
             ++index) {
            const auto& item =
                (*workspace.hierarchyItems)[index];
            if (collapsedDepth >= 0) {
                if (item.depth > collapsedDepth) {
                    continue;
                }
                collapsedDepth = -1;
            }
            ImGui::PushID(static_cast<int>(index));
            if (item.depth > 0) {
                ImGui::Indent(
                    static_cast<float>(item.depth) *
                    16.0f);
            }
            bool folderExpanded = true;
            if (item.folder) {
                const auto [state, inserted] =
                    impl_->hierarchyFolderExpanded.emplace(
                        item.id,
                        item.expandedByDefault);
                (void)inserted;
                folderExpanded = state->second;
                if (ImGui::ArrowButton(
                        "##folder",
                        folderExpanded
                            ? ImGuiDir_Down
                            : ImGuiDir_Right)) {
                    state->second = !state->second;
                    folderExpanded = state->second;
                }
                ImGui::SameLine();
            } else {
                ImGui::Dummy(ImVec2(12.0f, 1.0f));
                ImGui::SameLine();
            }
            const bool rowSelected =
                item.layoutObjectIndex >= 0
                ? layoutIndexSelected(
                      item.layoutObjectIndex)
                : impl_->inspectorSelection ==
                          InspectorSelectionDomain::Hierarchy &&
                      impl_->selectedHierarchyItem ==
                          static_cast<int>(index);
            if (ImGui::Selectable(
                    item.displayName.c_str(),
                    rowSelected)) {
                impl_->selectedHierarchyItem =
                    static_cast<int>(index);
                impl_->inspectorSelection =
                    InspectorSelectionDomain::Hierarchy;
                if (item.layoutObjectIndex >= 0) {
                    const ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyCtrl || io.KeyShift) {
                        toggleLayoutIndex(
                            item.layoutObjectIndex);
                    } else {
                        selectOnlyLayoutIndex(
                            item.layoutObjectIndex);
                    }
                    impl_->activeLayoutObjectId.clear();
                    if (layoutIndexSelected(
                            item.layoutObjectIndex)) {
                        actions.selectLayoutObjectIndex =
                            item.layoutObjectIndex;
                    }
                } else {
                    impl_->selectedLayoutObjectIds.clear();
                    impl_->selectedLayoutObject = -1;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n%s",
                    item.typeName.c_str(),
                    item.id.c_str());
            }
            if (item.folder &&
                !folderExpanded) {
                collapsedDepth = item.depth;
            }
            if (item.depth > 0) {
                ImGui::Unindent(
                    static_cast<float>(item.depth) *
                    16.0f);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();

    ImGui::Begin("Game Preview");
    ImGui::TextDisabled(
        "Warm in-editor runtime states");
    ImGui::Separator();
    const auto* gamePreviews = workspace.gamePreviews;
    if (!gamePreviews || gamePreviews->empty()) {
        ImGui::TextWrapped(
            "This project does not provide embedded game previews.");
    } else {
        const std::string activePreviewId =
            text(workspace.activeGamePreviewId);
        if (impl_->observedActiveGamePreviewId !=
            activePreviewId) {
            const auto active = std::find_if(
                gamePreviews->begin(),
                gamePreviews->end(),
                [&](const WorkspaceGamePreview& preview) {
                    return preview.id == activePreviewId;
                });
            if (active != gamePreviews->end()) {
                impl_->selectedGamePreview =
                    static_cast<int>(
                        std::distance(
                            gamePreviews->begin(),
                            active));
            }
            impl_->observedActiveGamePreviewId =
                activePreviewId;
        }
        impl_->selectedGamePreview = std::clamp(
            impl_->selectedGamePreview,
            0,
            static_cast<int>(
                gamePreviews->size() - 1u));

        std::vector<std::size_t> scenePreviewIndices;
        std::vector<std::size_t> applicationPreviewIndices;
        for (std::size_t index = 0u;
             index < gamePreviews->size();
             ++index) {
            const auto& preview = (*gamePreviews)[index];
            if (!workspace.activeSceneId.empty() &&
                preview.sceneId ==
                    workspace.activeSceneId) {
                scenePreviewIndices.push_back(index);
            } else if (preview.sceneId.empty()) {
                applicationPreviewIndices.push_back(index);
            }
        }
        std::vector<std::size_t> visibleIndices =
            scenePreviewIndices;
        visibleIndices.insert(
            visibleIndices.end(),
            applicationPreviewIndices.begin(),
            applicationPreviewIndices.end());
        if (visibleIndices.empty()) {
            for (std::size_t index = 0u;
                 index < gamePreviews->size();
                 ++index) {
                visibleIndices.push_back(index);
            }
        }
        if (std::find(
                visibleIndices.begin(),
                visibleIndices.end(),
                static_cast<std::size_t>(
                    impl_->selectedGamePreview)) ==
            visibleIndices.end()) {
            impl_->selectedGamePreview =
                static_cast<int>(visibleIndices.front());
        }

        const auto drawPreview =
            [&](std::size_t index) {
                const auto& preview =
                    (*gamePreviews)[index];
                ImGui::PushID(static_cast<int>(index));
                const bool selected =
                    impl_->selectedGamePreview ==
                    static_cast<int>(index);
                const bool active =
                    preview.id == activePreviewId;
                const std::string label =
                    preview.displayName +
                    (active ? "  [active]" : "");
                if (ImGui::Selectable(
                        label.c_str(),
                        selected)) {
                    impl_->selectedGamePreview =
                        static_cast<int>(index);
                    if (ImGui::IsMouseDoubleClicked(
                            ImGuiMouseButton_Left)) {
                        actions.selectGamePreviewIndex =
                            static_cast<int>(index);
                    }
                }
                ImGui::PopID();
            };

        if (!scenePreviewIndices.empty()) {
            std::string sceneName =
                text(workspace.activeSceneId);
            if (workspace.scenes) {
                const auto scene = std::find_if(
                    workspace.scenes->begin(),
                    workspace.scenes->end(),
                    [&](const WorkspaceScene& candidate) {
                        return candidate.id ==
                               workspace.activeSceneId;
                    });
                if (scene != workspace.scenes->end()) {
                    sceneName = scene->displayName;
                }
            }
            ImGui::TextDisabled(
                "Current Scene - %s",
                sceneName.c_str());
            for (const std::size_t index :
                 scenePreviewIndices) {
                drawPreview(index);
            }
        } else {
            ImGui::TextDisabled("Current Scene");
            ImGui::TextWrapped(
                "No runtime previews are associated with this scene.");
        }

        if (!applicationPreviewIndices.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled(
                "Application and runtime-only previews");
            std::string previousGroup;
            for (const std::size_t index :
                 applicationPreviewIndices) {
                const auto& preview =
                    (*gamePreviews)[index];
                if (preview.group != previousGroup) {
                    ImGui::TextDisabled(
                        "  %s",
                        preview.group.empty()
                            ? "Game"
                            : preview.group.c_str());
                    previousGroup = preview.group;
                }
                drawPreview(index);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        const auto& selected = (*gamePreviews)[
            static_cast<std::size_t>(
                impl_->selectedGamePreview)];
        ImGui::TextWrapped(
            "%s",
            selected.description.c_str());
        if (ImGui::Button(
                "Open In Game",
                ImVec2(-1.0f, 32.0f))) {
            actions.selectGamePreviewIndex =
                impl_->selectedGamePreview;
        }
        ImGui::TextDisabled(
            "Switches the already initialized game session.");
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    const char* inspectedName = "Nothing selected";
    const char* inspectedType = "";
    const std::vector<WorkspaceProperty>*
        inspectedProperties = nullptr;
    const WorkspaceAsset* inspectedAsset = nullptr;
    const WorkspaceLayoutObject* inspectedLayout = nullptr;
    int inspectedLayoutIndex = -1;
    if (impl_->inspectorSelection ==
            InspectorSelectionDomain::Hierarchy &&
        workspace.hierarchyItems &&
        !workspace.hierarchyItems->empty()) {
        impl_->selectedHierarchyItem = std::clamp(
            impl_->selectedHierarchyItem,
            0,
            static_cast<int>(
                workspace.hierarchyItems->size() - 1u));
        const auto& selected =
            (*workspace.hierarchyItems)[
                static_cast<std::size_t>(
                    impl_->selectedHierarchyItem)];
        inspectedName = selected.displayName.c_str();
        inspectedType = selected.typeName.c_str();
        inspectedProperties = &selected.properties;
        if (selected.layoutObjectIndex >= 0 &&
            workspace.layoutObjects &&
            static_cast<std::size_t>(
                selected.layoutObjectIndex) <
                workspace.layoutObjects->size()) {
            inspectedLayoutIndex =
                selected.layoutObjectIndex;
            inspectedLayout =
                &(*workspace.layoutObjects)[
                    static_cast<std::size_t>(
                        selected.layoutObjectIndex)];
        }
    } else if (
        impl_->inspectorSelection ==
            InspectorSelectionDomain::Asset &&
        workspace.assets &&
        !workspace.assets->empty()) {
        impl_->selectedAsset = std::clamp(
            impl_->selectedAsset,
            0,
            static_cast<int>(
                workspace.assets->size() - 1u));
        const auto& selected =
            (*workspace.assets)[
                static_cast<std::size_t>(
                    impl_->selectedAsset)];
        inspectedName = selected.displayName.c_str();
        inspectedType = selected.typeName.c_str();
        inspectedProperties = &selected.properties;
        inspectedAsset = &selected;
    } else if (
        impl_->inspectorSelection ==
            InspectorSelectionDomain::Scene &&
        workspace.scenes &&
        !workspace.scenes->empty()) {
        impl_->selectedScene = std::clamp(
            impl_->selectedScene,
            0,
            static_cast<int>(
                workspace.scenes->size() - 1u));
        const auto& selected =
            (*workspace.scenes)[
                static_cast<std::size_t>(
                    impl_->selectedScene)];
        inspectedName = selected.displayName.c_str();
        inspectedType = "Game Scene";
        inspectedProperties = &selected.properties;
    }
    ImGui::TextWrapped("%s", inspectedName);
    if (inspectedType[0] != '\0') {
        ImGui::TextDisabled("%s", inspectedType);
    }
    ImGui::Separator();
    ImGui::Spacing();
    if (workspace.layoutObjects &&
        !workspace.layoutObjects->empty()) {
        bool overlayVisible =
            workspace.layoutOverlayVisible;
        if (ImGui::Checkbox(
                "Show board footprint",
                &overlayVisible)) {
            actions.layoutOverlayVisibilityChanged = true;
            actions.layoutOverlayVisible = overlayVisible;
        }
        ImGui::TextDisabled(
            "Canonical source stays locked; edits are saved as project-owned overrides.");
        ImGui::Spacing();
        if (workspace.terrainTileEditingSupported &&
            ImGui::CollapsingHeader(
                "Terrain Tile Editor",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox(
                "Enable tile selection in Scene view",
                &impl_->terrainTileEditing);
            ImGui::TextWrapped(
                "Each cell is one source metre. Elevation changes use the source 50 cm level step; ledge faces are derived from neighboring cells.");
            ImGui::Text(
                "%zu tile(s) selected",
                impl_->selectedTerrainTiles.size());
            const bool hasTileSelection =
                !impl_->selectedTerrainTiles.empty();
            const auto queueTileEdit =
                [&](const char* operation,
                    const char* surface,
                    const char* shape) {
                    actions.terrainTileEditRequested = true;
                    actions.terrainTileCoordinates =
                        impl_->selectedTerrainTiles;
                    actions.terrainTileOperation = operation;
                    actions.terrainTileSurface =
                        surface ? surface : "";
                    actions.terrainTileShape =
                        shape ? shape : "";
                };
            ImGui::BeginDisabled(!hasTileSelection);
            if (ImGui::Button(
                    "Create / Fill Selected",
                    ImVec2(-1.0f, 28.0f))) {
                queueTileEdit("create", "", "flat");
            }
            if (ImGui::Button(
                    "Lower 50 cm",
                    ImVec2(0.0f, 28.0f))) {
                queueTileEdit("lower", "", "");
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    "Raise 50 cm",
                    ImVec2(-1.0f, 28.0f))) {
                queueTileEdit("raise", "", "");
            }

            if (workspace.terrainSurfaces &&
                !workspace.terrainSurfaces->empty()) {
                impl_->terrainSurfaceIndex = std::clamp(
                    impl_->terrainSurfaceIndex,
                    0,
                    static_cast<int>(
                        workspace.terrainSurfaces->size() - 1u));
                const auto& selectedSurface =
                    (*workspace.terrainSurfaces)[
                        static_cast<std::size_t>(
                            impl_->terrainSurfaceIndex)];
                if (ImGui::BeginCombo(
                        "Surface",
                        selectedSurface.displayName.c_str())) {
                    for (std::size_t index = 0u;
                         index < workspace.terrainSurfaces->size();
                         ++index) {
                        const auto& surface =
                            (*workspace.terrainSurfaces)[index];
                        const bool selected =
                            static_cast<int>(index) ==
                            impl_->terrainSurfaceIndex;
                        if (ImGui::Selectable(
                                surface.displayName.c_str(),
                                selected)) {
                            impl_->terrainSurfaceIndex =
                                static_cast<int>(index);
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button(
                        "Paint Surface",
                        ImVec2(-1.0f, 28.0f))) {
                    const auto& surface =
                        (*workspace.terrainSurfaces)[
                            static_cast<std::size_t>(
                                impl_->terrainSurfaceIndex)];
                    queueTileEdit(
                        "paint_surface",
                        surface.id.c_str(),
                        "");
                }
            }

            constexpr std::array<const char*, 5> kShapeIds{{
                "flat",
                "ramp_north",
                "ramp_east",
                "ramp_south",
                "ramp_west",
            }};
            constexpr std::array<const char*, 5> kShapeNames{{
                "Flat",
                "Ramp North (+1 level)",
                "Ramp East (+1 level)",
                "Ramp South (+1 level)",
                "Ramp West (+1 level)",
            }};
            impl_->terrainShapeIndex = std::clamp(
                impl_->terrainShapeIndex,
                0,
                static_cast<int>(kShapeIds.size() - 1u));
            if (ImGui::BeginCombo(
                    "Tile shape",
                    kShapeNames[static_cast<std::size_t>(
                        impl_->terrainShapeIndex)])) {
                for (std::size_t index = 0u;
                     index < kShapeIds.size();
                     ++index) {
                    const bool selected =
                        static_cast<int>(index) ==
                        impl_->terrainShapeIndex;
                    if (ImGui::Selectable(
                            kShapeNames[index],
                            selected)) {
                        impl_->terrainShapeIndex =
                            static_cast<int>(index);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button(
                    "Apply Tile Shape",
                    ImVec2(-1.0f, 28.0f))) {
                queueTileEdit(
                    "set_shape",
                    "",
                    kShapeIds[static_cast<std::size_t>(
                        impl_->terrainShapeIndex)]);
            }
            if (ImGui::Button(
                    "Restore Selected From Source",
                    ImVec2(-1.0f, 28.0f))) {
                queueTileEdit(
                    "restore_source", "", "");
            }
            ImGui::EndDisabled();
            if (ImGui::Button(
                    "Clear Tile Selection",
                    ImVec2(-1.0f, 24.0f))) {
                impl_->selectedTerrainTiles.clear();
            }
            ImGui::TextDisabled(
                "Click or drag cells in Scene view. Ctrl/Shift adds cells; all edits autosave and use Ctrl+Z history.");
            ImGui::Separator();
            ImGui::Spacing();
        }
        if (workspace.boardClearanceSupported &&
            ImGui::CollapsingHeader(
                "Autochess Board Clearing",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Suppress exact source objects intersecting the orange clearance boundary and place a matched flat ground patch beneath the board.");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat(
                "Clearance padding (cells)",
                &impl_->boardClearancePaddingCells,
                0.05f,
                0.0f,
                3.0f,
                "%.2f");
            ImGui::Checkbox(
                "Clear ledges and raised terrain",
                &impl_->boardClearanceClearTerrain);
            ImGui::Checkbox(
                "Clear vegetation",
                &impl_->boardClearanceClearVegetation);
            ImGui::Checkbox(
                "Clear props and other obstructions",
                &impl_->boardClearanceClearObjects);
            ImGui::Checkbox(
                "Retain ramps as entrances",
                &impl_->boardClearanceRetainRamps);
            ImGui::Checkbox(
                "Create ground infill",
                &impl_->boardClearanceAddGroundInfill);
            const bool canClear =
                impl_->boardClearanceClearTerrain ||
                impl_->boardClearanceClearVegetation ||
                impl_->boardClearanceClearObjects ||
                impl_->boardClearanceAddGroundInfill;
            ImGui::BeginDisabled(!canClear);
            if (ImGui::Button(
                    "Clear Board Footprint",
                    ImVec2(-1.0f, 30.0f))) {
                actions.applyBoardClearanceRequested = true;
                actions.boardClearanceRequest = {
                    .paddingCells =
                        impl_->boardClearancePaddingCells,
                    .clearTerrain =
                        impl_->boardClearanceClearTerrain,
                    .clearVegetation =
                        impl_->boardClearanceClearVegetation,
                    .clearObjects =
                        impl_->boardClearanceClearObjects,
                    .retainRamps =
                        impl_->boardClearanceRetainRamps,
                    .addGroundInfill =
                        impl_->boardClearanceAddGroundInfill};
            }
            ImGui::EndDisabled();
            if (ImGui::Button(
                    "Reset Entire Scene To Imported Source",
                    ImVec2(-1.0f, 28.0f))) {
                actions.resetSceneToSourceRequested = true;
                impl_->selectedLayoutObjectIds.clear();
                impl_->selectedLayoutObject = -1;
                impl_->activeLayoutObjectId.clear();
            }
            ImGui::TextDisabled(
                "Both operations are autosaved and undoable with Ctrl+Z.");
            ImGui::Separator();
            ImGui::Spacing();
        }
        if (impl_->selectedLayoutObjectIds.size() > 1u) {
            ImGui::Text(
                "%zu layout objects selected",
                impl_->selectedLayoutObjectIds.size());
            if (ImGui::Button(
                    "Suppress/Delete Selected Objects",
                    ImVec2(-1.0f, 28.0f))) {
                actions.editLayoutObjectIndex =
                    impl_->selectedLayoutObject;
                writeSelectedLayoutIndices();
                actions.layoutObjectDeleteRequested = true;
            }
            ImGui::TextDisabled(
                "Ctrl/Shift-click or drag an empty viewport area to change the selection.");
            ImGui::Separator();
            ImGui::Spacing();
        }
    }
    const bool showAssetPreview =
        inspectedAsset &&
        inspectedAsset->previewable3d;
    if (inspectedAsset &&
        inspectedAsset->sceneInstantiable) {
        if (ImGui::Button(
                "Add Prefab To Scene",
                ImVec2(-1.0f, 30.0f))) {
            actions.instantiateAssetIndex =
                impl_->selectedAsset;
        }
        ImGui::TextDisabled(
            "Creates a project-owned instance while sharing the cooked PHLO geometry.");
        ImGui::Spacing();
    }
    if (inspectedLayout) {
        if (impl_->activeLayoutObjectId !=
                inspectedLayout->stableId ||
            (!impl_->layoutGizmoDragging &&
             !ImGui::IsAnyItemActive())) {
            const bool changedSelection =
                impl_->activeLayoutObjectId !=
                    inspectedLayout->stableId;
            impl_->activeLayoutObjectId =
                inspectedLayout->stableId;
            impl_->layoutTranslation =
                inspectedLayout->translation;
            impl_->layoutRotationDegrees =
                inspectedLayout->rotationDegrees;
            impl_->layoutScale =
                inspectedLayout->scale;
            impl_->layoutSuppressed =
                inspectedLayout->suppressed;
            if (changedSelection) {
                std::snprintf(
                    impl_->layoutObjectName.data(),
                    impl_->layoutObjectName.size(),
                    "%s",
                    inspectedLayout->displayName.c_str());
                std::snprintf(
                    impl_->layoutObjectCategoryPath.data(),
                    impl_->layoutObjectCategoryPath.size(),
                    "%s",
                    inspectedLayout->categoryPath.c_str());
            }
        }
        const bool gameplayBoard =
            inspectedLayout->targetKind ==
            "gameplay_board";
        ImGui::TextUnformatted(
            gameplayBoard
                ? "Gameplay Board Layout"
                : "Layout Override");
        ImGui::TextDisabled(
            "%s",
            inspectedLayout->coordinateSystem.c_str());
        ImGui::TextDisabled(
            "%s",
            gameplayBoard
                ? "8x8 board with north and south bench rows"
                : inspectedLayout->prefabAssetId.empty()
                    ? "Editable source mesh group"
                    : ("Prefab: " +
                       inspectedLayout->prefabAssetId)
                          .c_str());
        ImGui::TextWrapped(
            "Values update live. Releasing a field or viewport gizmo autosaves the project override.");
        ImGui::BeginDisabled(gameplayBoard);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText(
            "Name",
            impl_->layoutObjectName.data(),
            impl_->layoutObjectName.size());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            actions.editLayoutObjectIndex =
                inspectedLayoutIndex;
            actions.layoutObjectRenameRequested = true;
            actions.layoutObjectText =
                impl_->layoutObjectName.data();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText(
            "Hierarchy folder",
            impl_->layoutObjectCategoryPath.data(),
            impl_->layoutObjectCategoryPath.size());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            actions.editLayoutObjectIndex =
                inspectedLayoutIndex;
            actions.layoutObjectReparentRequested = true;
            actions.layoutObjectText =
                impl_->layoutObjectCategoryPath.data();
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::BeginDisabled(gameplayBoard);
        if (ImGui::Button(
                "Duplicate",
                ImVec2(
                    ImGui::GetContentRegionAvail().x * 0.5f - 4.0f,
                    28.0f))) {
            actions.editLayoutObjectIndex =
                inspectedLayoutIndex;
            actions.layoutObjectDuplicateRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(
                impl_->selectedLayoutObjectIds.size() > 1u
                    ? "Delete Selected"
                    : "Delete",
                ImVec2(-1.0f, 28.0f))) {
            actions.editLayoutObjectIndex =
                inspectedLayoutIndex;
            writeSelectedLayoutIndices();
            actions.layoutObjectDeleteRequested = true;
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        bool liveEditChanged = false;
        bool liveEditFinished = false;
        ImGui::SetNextItemWidth(-1.0f);
        const bool translationChanged = ImGui::DragFloat3(
            gameplayBoard
                ? "Board center (source cm)"
                : "Translation",
            impl_->layoutTranslation.data(),
            1.0f,
            -100000.0f,
            100000.0f,
            "%.2f");
        if (translationChanged && gameplayBoard) {
            impl_->layoutTranslation[0] =
                std::round(
                    impl_->layoutTranslation[0] /
                    100.0f) *
                100.0f;
            impl_->layoutTranslation[1] =
                std::round(
                    impl_->layoutTranslation[1] /
                    50.0f) *
                50.0f;
            impl_->layoutTranslation[2] =
                std::round(
                    impl_->layoutTranslation[2] /
                    100.0f) *
                100.0f;
        }
        liveEditChanged |= translationChanged;
        liveEditFinished |=
            ImGui::IsItemDeactivatedAfterEdit();
        ImGui::BeginDisabled(gameplayBoard);
        ImGui::SetNextItemWidth(-1.0f);
        liveEditChanged |= ImGui::DragFloat3(
            "Rotation",
            impl_->layoutRotationDegrees.data(),
            0.25f,
            -360.0f,
            360.0f,
            "%.2f deg");
        liveEditFinished |=
            ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(-1.0f);
        if (gameplayBoard) {
            float tileSize = impl_->layoutScale[0];
            if (ImGui::DragFloat(
                    "Board tile size (m)",
                    &tileSize,
                    0.01f,
                    0.25f,
                    4.0f,
                    "%.2f m")) {
                tileSize = std::round(tileSize / 0.05f) *
                    0.05f;
                impl_->layoutScale = {
                    tileSize, tileSize, tileSize};
                liveEditChanged = true;
            }
            liveEditFinished |=
                ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Button(
                    "Match Route 1 Tiles (1.00 m)",
                    ImVec2(-1.0f, 28.0f))) {
                impl_->layoutScale =
                    {1.0f, 1.0f, 1.0f};
                liveEditChanged = true;
                liveEditFinished = true;
            }
            ImGui::TextDisabled(
                "Auto snap: X/Z = 100 cm terrain cells; Y = 50 cm elevation levels.");
            ImGui::TextWrapped(
                "Move with the viewport gizmo or source-centimetre fields. Tile size scales uniformly in 0.05 m steps; Match Route 1 Tiles selects exactly 1.00 m.");
        } else {
            liveEditChanged |= ImGui::DragFloat3(
                "Scale",
                impl_->layoutScale.data(),
                0.01f,
                0.01f,
                100.0f,
                "%.3f");
            liveEditFinished |=
                ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::BeginDisabled(gameplayBoard);
        if (ImGui::Checkbox(
                "Suppress in gameplay layout",
                &impl_->layoutSuppressed)) {
            liveEditChanged = true;
            liveEditFinished = true;
        }
        ImGui::EndDisabled();
        if (liveEditChanged || liveEditFinished) {
            actions.editLayoutObjectIndex =
                inspectedLayoutIndex;
            actions.layoutObjectPreviewRequested =
                liveEditChanged;
            actions.layoutObjectCommitRequested =
                liveEditFinished;
            actions.layoutTranslation =
                impl_->layoutTranslation;
            actions.layoutRotationDegrees =
                impl_->layoutRotationDegrees;
            actions.layoutScale =
                impl_->layoutScale;
            actions.layoutSuppressed =
                impl_->layoutSuppressed;
        }
        ImGui::Spacing();
        ImGui::TextDisabled(
            gameplayBoard
                ? "Viewport: select the board marker and use Move [W]; Scale [R] changes tile size."
                : "Viewport: click the green marker, then use Move [W], Rotate [E], or Scale [R].");
        ImGui::Spacing();
        if (liveEditFinished) {
            impl_->activeLayoutObjectId.clear();
        }
        ImGui::BeginDisabled(
            !inspectedLayout->hasOverride);
        if (ImGui::Button(
                gameplayBoard
                    ? "Reset Board Registration"
                    : "Reset To Canonical Source",
                ImVec2(-1.0f, 28.0f))) {
            actions.editLayoutObjectIndex =
                inspectedLayoutIndex;
            actions.layoutObjectResetRequested = true;
            impl_->activeLayoutObjectId.clear();
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled("Stable source target");
        ImGui::TextWrapped(
            "%s",
            inspectedLayout->stableId.c_str());
        ImGui::TextDisabled("Original translation");
        ImGui::Text(
            "%.2f, %.2f, %.2f",
            inspectedLayout->sourceTranslation[0],
            inspectedLayout->sourceTranslation[1],
            inspectedLayout->sourceTranslation[2]);
        ImGui::TextDisabled("Original rotation");
        ImGui::Text(
            "%.2f, %.2f, %.2f",
            inspectedLayout->sourceRotationDegrees[0],
            inspectedLayout->sourceRotationDegrees[1],
            inspectedLayout->sourceRotationDegrees[2]);
        ImGui::TextDisabled("Original scale");
        ImGui::Text(
            "%.3f, %.3f, %.3f",
            inspectedLayout->sourceScale[0],
            inspectedLayout->sourceScale[1],
            inspectedLayout->sourceScale[2]);
        ImGui::TextDisabled("Override status");
        ImGui::TextWrapped(
            "%s",
            inspectedLayout->hasOverride
                ? (inspectedLayout->reason.empty()
                       ? "Declared override"
                       : inspectedLayout->reason.c_str())
                : "Canonical");
    } else if (showAssetPreview) {
        const bool previewMatches =
            workspace.assetPreview &&
            workspace.assetPreview->assetId ==
                inspectedAsset->id;
        if (!previewMatches ||
            !workspace.assetPreview->ready) {
            ImGui::TextWrapped(
                "%s",
                previewMatches &&
                        !workspace.assetPreview->status.empty()
                    ? workspace.assetPreview->status.c_str()
                    : "Loading asset preview...");
        } else {
            const auto& preview =
                *workspace.assetPreview;
            if (impl_->activeAssetPreviewId !=
                preview.assetId) {
                impl_->activeAssetPreviewId =
                    preview.assetId;
                impl_->assetPreviewAnimationIndex =
                    preview.animationIndex;
                impl_->assetPreviewPlaybackSpeed = 1.0f;
                impl_->assetPreviewAnimationPlaying = true;
                impl_->assetPreviewShowMesh = true;
                impl_->assetPreviewShowMaterials = true;
                impl_->assetPreviewShowTextures = true;
                impl_->assetPreviewShowWireframe = false;
                impl_->assetPreviewShowSkeleton = false;
            }
            bool optionsChanged = false;
            if (ImGui::Button(
                    impl_->assetPreviewAnimationPlaying
                        ? "Pause"
                        : "Play")) {
                impl_->assetPreviewAnimationPlaying =
                    !impl_->assetPreviewAnimationPlaying;
                optionsChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    preview.kind ==
                            WorkspaceAssetPreviewKind::
                                VisualEffect
                        ? "Replay"
                        : "Restart")) {
                actions.assetPreviewSeekRequested = true;
                actions.assetPreviewSeekTimeSeconds = 0.0f;
                optionsChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset View")) {
                actions.resetAssetPreviewCamera = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "RMB orbit  MMB pan  Wheel zoom");

            const float previewWidth = std::max(
                160.0f,
                ImGui::GetContentRegionAvail().x);
            const float previewHeight = std::clamp(
                previewWidth * 0.72f,
                180.0f,
                390.0f);
            actions.assetPreviewWidth =
                std::max(1, static_cast<int>(previewWidth));
            actions.assetPreviewHeight =
                std::max(1, static_cast<int>(previewHeight));
            if (workspace.assetPreviewTextureId != 0u) {
                const ImVec2 uv0 =
                    workspace.flipRenderSurfaceTexturesVertically
                        ? ImVec2(0.0f, 1.0f)
                        : ImVec2(0.0f, 0.0f);
                const ImVec2 uv1 =
                    workspace.flipRenderSurfaceTexturesVertically
                        ? ImVec2(1.0f, 0.0f)
                        : ImVec2(1.0f, 1.0f);
                ImGui::Image(
                    static_cast<ImTextureID>(
                        workspace.assetPreviewTextureId),
                    ImVec2(previewWidth, previewHeight),
                    uv0,
                    uv1);
            } else {
                ImGui::Dummy(
                    ImVec2(previewWidth, previewHeight));
            }
            if (ImGui::IsItemHovered()) {
                ImGuiIO& io = ImGui::GetIO();
                if (ImGui::IsMouseDragging(
                        ImGuiMouseButton_Right)) {
                    actions.assetPreviewOrbitYaw =
                        -io.MouseDelta.x * 0.008f;
                    actions.assetPreviewOrbitPitch =
                        -io.MouseDelta.y * 0.008f;
                }
                if (ImGui::IsMouseDragging(
                        ImGuiMouseButton_Middle)) {
                    actions.assetPreviewPanX =
                        io.MouseDelta.x;
                    actions.assetPreviewPanY =
                        io.MouseDelta.y;
                }
                actions.assetPreviewZoom =
                    io.MouseWheel;
            }

            ImGui::Spacing();
            const auto* animations = preview.animations;
            if (preview.kind ==
                WorkspaceAssetPreviewKind::Model) {
                const char* animationLabel = "Bind pose";
                if (animations &&
                    impl_->assetPreviewAnimationIndex >= 0 &&
                    static_cast<std::size_t>(
                        impl_->assetPreviewAnimationIndex) <
                        animations->size()) {
                    animationLabel =
                        (*animations)[static_cast<std::size_t>(
                            impl_->assetPreviewAnimationIndex)]
                            .name.c_str();
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo(
                        "##PrefabAnimation",
                        animationLabel)) {
                    if (ImGui::Selectable(
                            "Bind pose",
                            impl_->assetPreviewAnimationIndex < 0)) {
                        impl_->assetPreviewAnimationIndex = -1;
                        optionsChanged = true;
                    }
                    if (animations) {
                        for (std::size_t index = 0u;
                             index < animations->size();
                             ++index) {
                            const auto& animation =
                                (*animations)[index];
                            const std::string label =
                                animation.name +
                                "  (" +
                                std::to_string(
                                    animation.durationSeconds) +
                                "s)";
                            if (ImGui::Selectable(
                                    label.c_str(),
                                    impl_->
                                            assetPreviewAnimationIndex ==
                                        static_cast<int>(index))) {
                                impl_->assetPreviewAnimationIndex =
                                    static_cast<int>(index);
                                optionsChanged = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat(
                    "Playback speed",
                    &impl_->assetPreviewPlaybackSpeed,
                    0.0f,
                    2.0f,
                    "%.2fx")) {
                optionsChanged = true;
            }
            if (preview.animationDurationSeconds >
                0.0001f) {
                float playhead = std::clamp(
                    preview.animationTimeSeconds,
                    0.0f,
                    preview.animationDurationSeconds);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat(
                        "Timeline",
                        &playhead,
                        0.0f,
                        preview.animationDurationSeconds,
                        "%.3fs")) {
                    actions.assetPreviewSeekRequested = true;
                    actions.assetPreviewSeekTimeSeconds =
                        playhead;
                    optionsChanged = true;
                }
                ImGui::TextDisabled(
                    "%.3fs / %.3fs",
                    preview.animationTimeSeconds,
                    preview.animationDurationSeconds);
            }

            if (preview.kind ==
                WorkspaceAssetPreviewKind::Model) {
                if (ImGui::Checkbox(
                        "Mesh",
                        &impl_->assetPreviewShowMesh)) {
                    optionsChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show or hide the model geometry.");
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(
                        "Materials",
                        &impl_->assetPreviewShowMaterials)) {
                    optionsChanged = true;
                    if (!impl_->assetPreviewShowMaterials) {
                        impl_->assetPreviewShowTextures = false;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Use the cooked material shaders and factors. "
                        "Off shows neutral geometry.");
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(
                    !impl_->assetPreviewShowMaterials);
                if (ImGui::Checkbox(
                        "Textures",
                        &impl_->assetPreviewShowTextures)) {
                    optionsChanged = true;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(
                        ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(
                        "Use the image maps referenced by the cooked "
                        "materials. Textures require Materials.");
                }
                ImGui::TextDisabled(
                    "Materials: shaders/factors   Textures: image maps");
                if (ImGui::Checkbox(
                        "Wireframe",
                        &impl_->assetPreviewShowWireframe)) {
                    optionsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(
                        "Skeleton",
                        &impl_->assetPreviewShowSkeleton)) {
                    optionsChanged = true;
                }
            }

            ImGui::Separator();
            if (preview.kind ==
                WorkspaceAssetPreviewKind::VisualEffect) {
                ImGui::TextDisabled(
                    "%u active visual element%s",
                    preview.activeElementCount,
                    preview.activeElementCount == 1u
                        ? ""
                        : "s");
            } else {
                ImGui::TextDisabled(
                    "%u vertices  %u triangles",
                    preview.vertexCount,
                    preview.triangleCount);
                ImGui::TextDisabled(
                    "%u materials  %u textures  %u bones  %zu clips",
                    preview.materialCount,
                    preview.textureCount,
                    preview.boneCount,
                    animations ? animations->size() : 0u);
            }
            if (!preview.status.empty()) {
                ImGui::TextWrapped(
                    "%s",
                    preview.status.c_str());
            }
            if (optionsChanged) {
                actions.assetPreviewOptionsChanged = true;
                actions.assetPreviewAnimationIndex =
                    impl_->assetPreviewAnimationIndex;
                actions.assetPreviewPlaybackSpeed =
                    impl_->assetPreviewPlaybackSpeed;
                actions.assetPreviewAnimationPlaying =
                    impl_->assetPreviewAnimationPlaying;
                actions.assetPreviewShowMesh =
                    impl_->assetPreviewShowMesh;
                actions.assetPreviewShowMaterials =
                    impl_->assetPreviewShowMaterials;
                actions.assetPreviewShowTextures =
                    impl_->assetPreviewShowTextures;
                actions.assetPreviewShowWireframe =
                    impl_->assetPreviewShowWireframe;
                actions.assetPreviewShowSkeleton =
                    impl_->assetPreviewShowSkeleton;
            }
        }
    } else if (inspectedProperties) {
        drawInspectorProperties(
            *inspectedProperties);
    } else {
        ImGui::TextDisabled(
            "Select a hierarchy object, asset, or scene.");
    }
    ImGui::End();

    ImGui::Begin("Assets");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint(
        "##AssetFilter",
        "Filter assets...",
        impl_->assetFilter.data(),
        impl_->assetFilter.size());
    ImGui::Separator();
    if (!workspace.assets || workspace.assets->empty()) {
        ImGui::TextWrapped(
            "No assets were discovered for this project.");
    } else if (ImGui::BeginTable(
                   "AssetTable",
                   2,
                   ImGuiTableFlags_RowBg |
                       ImGuiTableFlags_BordersInnerV |
                       ImGuiTableFlags_Resizable |
                       ImGuiTableFlags_ScrollY,
                   ImVec2(0.0f, -22.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(
            "Asset",
            ImGuiTableColumnFlags_WidthStretch,
            0.7f);
        ImGui::TableSetupColumn(
            "Type",
            ImGuiTableColumnFlags_WidthStretch,
            0.3f);
        ImGui::TableHeadersRow();

        const std::string_view filter(
            impl_->assetFilter.data());
        std::string previousCategory;
        for (std::size_t index = 0u;
             index < workspace.assets->size();
             ++index) {
            const auto& asset =
                (*workspace.assets)[index];
            if (!containsInsensitive(
                    asset.displayName,
                    filter) &&
                !containsInsensitive(
                    asset.typeName,
                    filter) &&
                !containsInsensitive(
                    asset.category,
                    filter) &&
                !containsInsensitive(
                    asset.path,
                    filter)) {
                continue;
            }
            if (asset.category != previousCategory) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(
                    "%s",
                    asset.category.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Category");
                previousCategory = asset.category;
            }
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(
                    asset.displayName.c_str(),
                    impl_->inspectorSelection ==
                            InspectorSelectionDomain::Asset &&
                        impl_->selectedAsset ==
                            static_cast<int>(index),
                    ImGuiSelectableFlags_SpanAllColumns)) {
                impl_->selectedAsset =
                    static_cast<int>(index);
                impl_->inspectorSelection =
                    InspectorSelectionDomain::Asset;
                actions.selectAssetIndex =
                    static_cast<int>(index);
                impl_->assetPreviewAnimationIndex = 0;
                impl_->assetPreviewPlaybackSpeed = 1.0f;
                impl_->assetPreviewAnimationPlaying = true;
                impl_->assetPreviewShowMesh = true;
                impl_->assetPreviewShowMaterials = true;
                impl_->assetPreviewShowTextures = true;
                impl_->assetPreviewShowWireframe = false;
                impl_->assetPreviewShowSkeleton = false;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s",
                    asset.path.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(
                asset.typeName.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "%zu top-level cooked assets",
        workspace.assets
            ? workspace.assets->size()
            : 0u);
    ImGui::End();

    ImGui::Begin("Scenes");
    ImGui::TextDisabled(
        "Game scene catalog");
    ImGui::Separator();
    if (!workspace.scenes || workspace.scenes->empty()) {
        ImGui::TextWrapped(
            "This project has not declared game scenes.");
    } else {
        impl_->selectedScene = std::clamp(
            impl_->selectedScene,
            0,
            static_cast<int>(
                workspace.scenes->size() - 1u));
        std::string previousCategory;
        for (std::size_t index = 0u;
             index < workspace.scenes->size();
             ++index) {
            const auto& scene =
                (*workspace.scenes)[index];
            if (scene.category != previousCategory) {
                ImGui::TextDisabled(
                    "%s",
                    scene.category.empty()
                        ? "Scenes"
                        : scene.category.c_str());
                previousCategory = scene.category;
            }
            ImGui::PushID(static_cast<int>(index));
            const bool active =
                scene.id ==
                workspace.activeSceneId;
            const std::string sceneLabel =
                scene.displayName +
                (active
                     ? "  [open]"
                     : scene.startup
                         ? "  [startup]"
                         : "");
            if (ImGui::Selectable(
                    sceneLabel.c_str(),
                    impl_->inspectorSelection ==
                            InspectorSelectionDomain::Scene &&
                        impl_->selectedScene ==
                            static_cast<int>(index))) {
                impl_->selectedScene =
                    static_cast<int>(index);
                impl_->inspectorSelection =
                    InspectorSelectionDomain::Scene;
                if (ImGui::IsMouseDoubleClicked(
                        ImGuiMouseButton_Left)) {
                    actions.openSceneIndex =
                        static_cast<int>(index);
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s / %s",
                scene.status.c_str(),
                scene.environmentDisplayName.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\nEnvironment: %s (%s)\n%s%s%s",
                    scene.id.c_str(),
                    scene.environmentAssetId.c_str(),
                    scene.environmentKind.c_str(),
                    scene.path.empty()
                        ? "Scene view: runtime-generated backdrop"
                        : scene.path.c_str(),
                    scene.runtimePath.empty()
                        ? ""
                        : "\nRuntime: ",
                    scene.runtimePath.empty()
                        ? ""
                        : scene.runtimePath.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        const auto& selected =
            (*workspace.scenes)[
                static_cast<std::size_t>(
                    impl_->selectedScene)];
        ImGui::TextWrapped(
            "A game scene composes a reusable environment backdrop with its runtime state. Opening it updates the Scene view, hierarchy, Inspector, and associated game previews.");
        if (ImGui::Button(
                selected.id ==
                        workspace.activeSceneId
                    ? "Focus Open Scene"
                    : "Open Scene",
                ImVec2(-1.0f, 30.0f))) {
            actions.openSceneIndex =
                impl_->selectedScene;
        }
    }
    ImGui::End();

    ImGui::Begin("Console");
    ImGui::TextColored(
        ImVec4(0.35f, 0.90f, 0.58f, 1.0f),
        "[Phlosion Editor]");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text(workspace.status).c_str());
    ImGui::TextDisabled(
        "Project: %s",
        text(workspace.projectRoot).c_str());
    ImGui::TextDisabled(
        "Scene: %s",
        text(workspace.scenePath).c_str());
    ImGui::TextDisabled(
        "Renderer: %s",
        text(workspace.backendName).c_str());
    ImGui::TextDisabled(
        "Play mode: %s  %.2fs",
        workspace.playState == EditorPlayState::Editing
            ? "frozen"
            : workspace.playState == EditorPlayState::Paused
                ? "paused"
                : "running",
        workspace.simulationSeconds);
    ImGui::End();

    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        actions.openProject = true;
    }
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
        ImGui::IsKeyPressed(ImGuiKey_P, false)) {
        actions.togglePlay = true;
    }
    if (io.KeyCtrl && io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_P, false) &&
        workspace.playState != EditorPlayState::Editing) {
        actions.togglePause = true;
    }
    if (io.KeyCtrl && io.KeyAlt &&
        ImGui::IsKeyPressed(ImGuiKey_P, false) &&
        workspace.playState == EditorPlayState::Paused) {
        actions.step = true;
    }
    drawPreferencesWindow(
        impl_->preferencesOpen,
        impl_->pendingRendererPreference,
        workspace.rendererPreference,
        actions);
    return actions;
}

void EditorShell::render() {
    if (!impl_->ready) {
        return;
    }
    ImGui::Render();
    if (impl_->rendererBackend ==
        Impl::RendererBackend::OpenGL) {
        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData());
#if defined(_WIN32)
    } else if (
        impl_->rendererBackend ==
        Impl::RendererBackend::D3D12) {
        auto* d3d12 =
            dynamic_cast<D3D12RenderBackend*>(
                impl_->renderer);
        if (d3d12 &&
            d3d12->nativeCommandList() &&
            impl_->d3d12Descriptors &&
            impl_->d3d12Descriptors->heap) {
            d3d12->bindBackbufferForEditorUi();
            ID3D12DescriptorHeap* descriptorHeaps[] = {
                impl_->d3d12Descriptors->heap.Get()};
            d3d12->nativeCommandList()->
                SetDescriptorHeaps(
                    1u,
                    descriptorHeaps);
            ImGui_ImplDX12_RenderDrawData(
                ImGui::GetDrawData(),
                d3d12->nativeCommandList());
        }
#endif
    } else if (
        impl_->rendererBackend ==
        Impl::RendererBackend::Vulkan) {
        auto* vulkan =
            dynamic_cast<VulkanRenderBackend*>(
                impl_->renderer);
        if (vulkan) {
            const VkCommandBuffer commandBuffer =
                vulkan->currentEditorCommandBuffer();
            if (commandBuffer != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RenderDrawData(
                    ImGui::GetDrawData(),
                    commandBuffer);
            }
        }
    }
}

bool EditorShell::allocateTextureDescriptor(
    EditorTextureDescriptor& outDescriptor) {
    outDescriptor = {};
#if defined(_WIN32)
    if (!impl_ || !impl_->ready ||
        impl_->rendererBackend !=
            Impl::RendererBackend::D3D12 ||
        !impl_->d3d12Descriptors) {
        return false;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!impl_->d3d12Descriptors->allocate(
            cpu,
            gpu)) {
        return false;
    }
    outDescriptor.cpuHandle =
        static_cast<std::uint64_t>(cpu.ptr);
    outDescriptor.gpuHandle =
        static_cast<std::uint64_t>(gpu.ptr);
    return true;
#else
    return false;
#endif
}

void EditorShell::selectAsset(int assetIndex) {
    if (!impl_ || assetIndex < 0) {
        return;
    }
    impl_->selectedAsset = assetIndex;
    impl_->inspectorSelection =
        InspectorSelectionDomain::Asset;
    impl_->activeAssetPreviewId.clear();
    impl_->assetPreviewAnimationIndex = 0;
    impl_->assetPreviewPlaybackSpeed = 1.0f;
    impl_->assetPreviewAnimationPlaying = true;
    impl_->assetPreviewShowMesh = true;
    impl_->assetPreviewShowMaterials = true;
    impl_->assetPreviewShowTextures = true;
    impl_->assetPreviewShowWireframe = false;
    impl_->assetPreviewShowSkeleton = false;
}

bool EditorShell::wantsMouseCapture() const {
    return impl_ && impl_->ready && ImGui::GetIO().WantCaptureMouse;
}

bool EditorShell::wantsKeyboardCapture() const {
    return impl_ && impl_->ready && ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorShell::initialized() const noexcept {
    return impl_ && impl_->ready;
}

} // namespace engine::editor
