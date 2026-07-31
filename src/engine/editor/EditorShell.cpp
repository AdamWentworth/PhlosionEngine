#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "engine/editor/EditorShell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <iterator>
#include <memory>
#include <string>

#include <imgui.h>
#if defined(_WIN32)
#include <imgui_impl_dx12.h>
#endif
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include "engine/render/D3D12RenderBackend.h"

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

bool drawRendererPreferenceMenu(
    EditorRendererPreference current,
    EditorShellActions& actions) {
    bool changed = false;
    const auto option =
        [&](const char* label,
            EditorRendererPreference preference,
            bool enabled = true) {
            if (ImGui::MenuItem(
                    label,
                    nullptr,
                    current == preference,
                    enabled)) {
                actions.rendererPreferenceChanged = true;
                actions.rendererPreference = preference;
                changed = true;
            }
        };
    option(
        "Auto (recommended)",
        EditorRendererPreference::Auto);
#if defined(_WIN32)
    option(
        "Direct3D 12",
        EditorRendererPreference::D3D12);
#endif
    option(
        "Vulkan (runtime available; editor integration pending)",
        EditorRendererPreference::Vulkan,
        false);
    option(
        "OpenGL (compatibility)",
        EditorRendererPreference::OpenGL);
    ImGui::Separator();
    ImGui::TextDisabled(
        "Changing API restarts the editor.");
    return changed;
}

} // namespace

struct EditorShell::Impl {
    enum class RendererBackend {
        OpenGL,
        D3D12,
    };

    SDL_Window* window = nullptr;
    IRenderBackend* renderer = nullptr;
    RendererBackend rendererBackend =
        RendererBackend::OpenGL;
    bool ready = false;
    bool firstLayout = true;
    int selectedHierarchyItem = 0;
    int selectedAsset = 0;
    int selectedScene = 0;
    int selectedPlayConfiguration = 0;
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
            if (ImGui::BeginMenu("Rendering API")) {
                drawRendererPreferenceMenu(
                    browser.rendererPreference,
                    actions);
                ImGui::EndMenu();
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
            ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
            ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
            ImGui::Separator();
            if (ImGui::BeginMenu("Rendering API")) {
                drawRendererPreferenceMenu(
                    workspace.rendererPreference,
                    actions);
                ImGui::EndMenu();
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
            text(workspace.sceneAssetId);
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
    ImGui::TextDisabled("%s", text(workspace.sceneAssetId).c_str());
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
        for (std::size_t index = 0u;
             index < workspace.hierarchyItems->size();
             ++index) {
            const auto& item =
                (*workspace.hierarchyItems)[index];
            ImGui::PushID(static_cast<int>(index));
            if (item.depth > 0) {
                ImGui::Indent(
                    static_cast<float>(item.depth) *
                    16.0f);
            }
            if (ImGui::Selectable(
                    item.displayName.c_str(),
                    impl_->inspectorSelection ==
                            InspectorSelectionDomain::
                                Hierarchy &&
                        impl_->selectedHierarchyItem ==
                            static_cast<int>(index))) {
                impl_->selectedHierarchyItem =
                    static_cast<int>(index);
                impl_->inspectorSelection =
                    InspectorSelectionDomain::Hierarchy;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n%s",
                    item.typeName.c_str(),
                    item.id.c_str());
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
        impl_->selectedPlayConfiguration = std::clamp(
            impl_->selectedPlayConfiguration,
            0,
            static_cast<int>(
                gamePreviews->size() - 1u));
        std::string previousGroup;
        for (std::size_t index = 0u;
             index < gamePreviews->size();
             ++index) {
            const auto& configuration =
                (*gamePreviews)[index];
            if (configuration.group != previousGroup) {
                if (index != 0u) {
                    ImGui::Spacing();
                }
                ImGui::TextDisabled(
                    "%s",
                    configuration.group.empty()
                        ? "Game"
                        : configuration.group.c_str());
                previousGroup = configuration.group;
            }
            ImGui::PushID(static_cast<int>(index));
            const bool selected =
                impl_->selectedPlayConfiguration ==
                static_cast<int>(index);
            if (ImGui::Selectable(
                    configuration.displayName.c_str(),
                    selected)) {
                impl_->selectedPlayConfiguration =
                    static_cast<int>(index);
                if (ImGui::IsMouseDoubleClicked(
                        ImGuiMouseButton_Left)) {
                    actions.selectGamePreviewIndex =
                        static_cast<int>(index);
                }
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        const auto& selected = (*gamePreviews)[
            static_cast<std::size_t>(
                impl_->selectedPlayConfiguration)];
        ImGui::TextWrapped(
            "%s",
            selected.description.c_str());
        if (ImGui::Button(
                "Open In Game",
                ImVec2(-1.0f, 32.0f))) {
            actions.selectGamePreviewIndex =
                impl_->selectedPlayConfiguration;
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
        inspectedType =
            selected.kind == "runtime_stage"
                ? "Runtime Stage"
                : "Cooked World Scene";
        inspectedProperties = &selected.properties;
    }
    ImGui::TextWrapped("%s", inspectedName);
    if (inspectedType[0] != '\0') {
        ImGui::TextDisabled("%s", inspectedType);
    }
    ImGui::Separator();
    ImGui::Spacing();
    const bool showAssetPreview =
        inspectedAsset &&
        inspectedAsset->previewable3d;
    if (showAssetPreview) {
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
                    : "Loading cooked prefab preview...");
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
            if (ImGui::Button("Restart")) {
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

            ImGui::Separator();
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
        "Filter cooked assets...",
        impl_->assetFilter.data(),
        impl_->assetFilter.size());
    ImGui::Separator();
    if (!workspace.assets || workspace.assets->empty()) {
        ImGui::TextWrapped(
            "No cooked assets were discovered in the project content mounts.");
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
        "Project scene asset catalog");
    ImGui::Separator();
    if (!workspace.scenes || workspace.scenes->empty()) {
        ImGui::TextWrapped(
            "This project has not declared scene assets.");
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
            const std::string sceneLabel =
                scene.displayName +
                (scene.startup ? "  [startup]" : "");
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
                "%s%s",
                scene.kind == "runtime_stage"
                    ? "runtime stage"
                    : "cooked world",
                scene.previewId.empty()
                    ? ""
                    : " / preview");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n%s\n%s",
                    scene.assetId.c_str(),
                    scene.kind.c_str(),
                    scene.path.c_str());
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
            "%s",
            selected.kind == "runtime_stage"
                ? "Runtime stage: opens the real game state. It is not yet a standalone cooked environment."
                : "Cooked world scene: opens the source-backed Scene view.");
        if (ImGui::Button(
                selected.kind == "runtime_stage"
                    ? "Open Runtime Stage"
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
            d3d12->nativeCommandList()) {
            d3d12->bindBackbufferForEditorUi();
            ImGui_ImplDX12_RenderDrawData(
                ImGui::GetDrawData(),
                d3d12->nativeCommandList());
        }
#endif
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
