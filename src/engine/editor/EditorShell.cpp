#include "engine/editor/EditorShell.h"

#include <algorithm>
#include <cfloat>
#include <iterator>
#include <memory>
#include <string>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

namespace engine::editor {

namespace {

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

} // namespace

struct EditorShell::Impl {
    SDL_Window* window = nullptr;
    bool ready = false;
    bool firstLayout = true;
    int selectedHierarchyItem = 0;
    std::string settingsIniPath;
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
    std::string* outError,
    const char* settingsIniPath) {
    if (impl_->ready) {
        return true;
    }
    if (!window) {
        if (outError) {
            *outError = "Editor shell requires a valid SDL window.";
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

    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        ImGui::DestroyContext();
        if (outError) {
            *outError = "Dear ImGui OpenGL backend initialization failed.";
        }
        return false;
    }

    impl_->window = window;
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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    impl_->window = nullptr;
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
    SDL_GL_GetDrawableSize(
        impl_->window,
        &drawableWidth,
        &drawableHeight);

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

    ImGui_ImplOpenGL3_NewFrame();
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
        ImGui::GetID("PhlosionEditorDockspace");
    if (impl_->firstLayout &&
        ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        impl_->firstLayout = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(
            dockspaceId,
            static_cast<ImGuiDockNodeFlags>(
                static_cast<int>(
                    ImGuiDockNodeFlags_DockSpace) |
                static_cast<int>(
                    ImGuiDockNodeFlags_PassthruCentralNode)));
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
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(
        dockspaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);

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

    ImGui::Begin("Scene Hierarchy");
    ImGui::TextDisabled("%s", text(workspace.sceneAssetId).c_str());
    ImGui::Separator();
    constexpr const char* hierarchyItems[] = {
        "Route 1",
        "Canonical environment",
        "Encounter grass",
        "Placed vegetation",
        "Projected lighting and shadows"};
    for (int index = 0;
         index < static_cast<int>(std::size(hierarchyItems));
         ++index) {
        if (ImGui::Selectable(
                hierarchyItems[index],
                impl_->selectedHierarchyItem == index)) {
            impl_->selectedHierarchyItem = index;
        }
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    ImGui::Text("%s", hierarchyItems[impl_->selectedHierarchyItem]);
    ImGui::Separator();
    ImGui::TextDisabled("Read-only source-backed view");
    ImGui::Spacing();
    ImGui::Text("Scenes");
    ImGui::SameLine(140.0f);
    ImGui::Text("%u", workspace.sceneCount);
    ImGui::Text("Materials");
    ImGui::SameLine(140.0f);
    ImGui::Text("%u", workspace.materialCount);
    ImGui::Text("Draw classes");
    ImGui::SameLine(140.0f);
    ImGui::Text("%u", workspace.drawClassCount);
    ImGui::Text("Encounter grass");
    ImGui::SameLine(140.0f);
    ImGui::Text("%u", workspace.encounterGrassInstanceCount);
    ImGui::Text("Vegetation");
    ImGui::SameLine(140.0f);
    ImGui::Text("%u", workspace.vegetationInstanceCount);
    ImGui::Text("Visible triangles");
    ImGui::SameLine(140.0f);
    ImGui::Text("%llu",
                static_cast<unsigned long long>(
                    workspace.visibleTriangleCount));
    ImGui::Text("Shadow triangles");
    ImGui::SameLine(140.0f);
    ImGui::Text("%llu",
                static_cast<unsigned long long>(
                    workspace.shadowTriangleCount));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled(
        "Backend: %s",
        text(workspace.backendName).c_str());
    ImGui::End();

    ImGui::Begin("Assets");
    if (ImGui::BeginTable(
            "AssetTable",
            3,
            ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Asset");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(text(workspace.sceneAssetId).c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(".phscene");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(
            ImVec4(0.35f, 0.90f, 0.58f, 1.0f),
            "Mounted (%zu files)",
            workspace.archiveFileCount);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(text(workspace.projectId).c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Project");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("Loaded");
        ImGui::EndTable();
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
    ImGui::End();

    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        actions.openProject = true;
    }
    return actions;
}

void EditorShell::render() {
    if (!impl_->ready) {
        return;
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
