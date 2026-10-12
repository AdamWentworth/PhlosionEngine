#include "SdlInputReplay.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <stdexcept>

namespace engine::input {
namespace {
using json = nlohmann::json;
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(std::string("SDL replay: ") + text); }
void fields(const json& j, std::initializer_list<const char*> allowed) {
    check(j.is_object(), "expected object");
    std::set<std::string> keys; for (const auto* key : allowed) keys.insert(key);
    for (auto i = j.begin(); i != j.end(); ++i) check(keys.contains(i.key()), "unknown field");
}
int integer(const json& j, const char* key, int low, int high) {
    check(j.contains(key) && j.at(key).is_number_integer(), "integer required");
    const auto& value = j.at(key);
    if (value.is_number_unsigned()) {
        const auto n = value.get<std::uint64_t>();
        check(high >= 0 && n <= static_cast<std::uint64_t>(high) &&
              (low <= 0 || n >= static_cast<std::uint64_t>(low)), "integer out of bounds");
        return static_cast<int>(n);
    }
    const auto n = value.get<std::int64_t>(); check(n >= low && n <= high, "integer out of bounds");
    return static_cast<int>(n);
}
void boolean(const json& j, const char* key) { check(j.contains(key) && j.at(key).is_boolean(), "boolean required"); }
void push(SDL_Event& event) { check(SDL_PushEvent(&event) == 1, "event push failed"); }
}
struct SdlInputReplay::Impl {
    json events;
    json gates = {{"focus", true}, {"active", true}, {"playing", true}, {"paused", false},
                  {"viewport_focus", true}, {"hover", true}, {"text", false}};
    int frame = 0;
    std::size_t next = 0;
    SDL_Joystick* pad = nullptr;
    SDL_JoystickID instance = -1;
    bool reference = false;
    void detach() {
        if (pad) { SDL_JoystickClose(pad); pad = nullptr; }
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_JoystickGetDeviceInstanceID(i) == instance && SDL_JoystickIsVirtual(i)) {
                check(SDL_JoystickDetachVirtual(i) == 0, "detach failed"); break;
            }
        instance = -1;
    }
    ~Impl() { detach(); if (reference) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER); }
    void attach() {
        check(!pad, "duplicate attach");
        SDL_VirtualJoystickDesc d{};
        d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER; d.naxes = 6; d.nbuttons = 15;
        d.axis_mask = 63; d.button_mask = 32767; d.name = "Phlosion owned replay controller";
        const int index = SDL_JoystickAttachVirtualEx(&d); check(index >= 0, "attach failed");
        instance = SDL_JoystickGetDeviceInstanceID(index);
        if (instance < 0) { SDL_JoystickDetachVirtual(index); throw std::runtime_error("SDL replay: virtual instance lookup failed"); }
        pad = SDL_JoystickOpen(index); check(pad != nullptr, "open virtual device failed");
        for (int a = 0; a < 6; ++a) check(SDL_JoystickSetVirtualAxis(pad, a, a >= 4 ? -32768 : 0) == 0, "neutral axis failed");
        SDL_JoystickUpdate();
    }
};
SdlInputReplay::SdlInputReplay(const std::filesystem::path& script) : impl_(std::make_unique<Impl>()) {
    check(std::filesystem::file_size(script) <= 1024 * 1024, "script exceeds 1 MiB");
    std::ifstream in(script);
    const json root=json::parse(in,[](int depth,json::parse_event_t,json&){check(depth<=12,"JSON nesting limit");return true;});
    fields(root, {"schema", "events"}); check(root.at("schema") == "phlosion-sdl-replay-v1", "schema mismatch");
    const auto& events = root.at("events"); check(events.is_array() && events.size() <= 4096, "event limit");
    int previous = 0; bool attached = false;
    for (const auto& e : events) {
        const int f = integer(e, "frame", 1, 10000); check(f >= previous, "out-of-order frame"); previous = f;
        const std::string type = e.at("type").get<std::string>();
        if (type == "state") {
            fields(e, {"frame", "type", "focus", "active", "playing", "paused", "viewport_focus", "hover", "text"});
            for (auto i = e.begin(); i != e.end(); ++i) if (i.key() != "frame" && i.key() != "type") boolean(e, i.key().c_str());
        } else if (type == "key") {
            fields(e, {"frame", "type", "key", "down", "repeat", "mod"}); boolean(e, "down");
            check(e.at("key").is_string() && e.at("key").get<std::string>().size() <= 32, "key required");
            check(SDL_GetKeyFromName(e.at("key").get<std::string>().c_str()) != SDLK_UNKNOWN, "unknown key");
            if (e.contains("repeat")) boolean(e, "repeat");
            if (e.contains("mod")) integer(e,"mod",0,65535);
        } else if (type == "attach" || type == "detach") {
            fields(e, {"frame", "type"}); check(type == "attach" ? !attached : attached, "invalid device lifetime"); attached = type == "attach";
        } else if (type == "axis" || type == "button") {
            fields(e, {"frame", "type", "index", "value"}); check(attached, "device event before attach");
            integer(e, "index", 0, type == "axis" ? 5 : 14); integer(e, "value", type == "axis" ? -32768 : 0, type == "axis" ? 32767 : 1);
        } else if (type == "mouse") {
            fields(e, {"frame", "type", "down"}); boolean(e, "down");
        } else if (type == "motion") {
            fields(e, {"frame", "type", "x", "y"}); integer(e, "x", -1000, 1000); integer(e, "y", -1000, 1000);
        } else if (type == "wheel") {
            fields(e, {"frame", "type", "y"}); integer(e, "y", -100, 100);
        } else throw std::runtime_error("SDL replay: unknown event type");
    }
    impl_->events = events;
    check(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0, "subsystem initialization failed"); impl_->reference = true;
}
SdlInputReplay::~SdlInputReplay() = default;
int SdlInputReplay::frameNumber() const { return impl_->frame; }
bool SdlInputReplay::acceptsEvent(const SDL_Event& event) const {
    if (event.type == SDL_CONTROLLERAXISMOTION)
        return impl_->instance >= 0 && event.caxis.which == impl_->instance;
    if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP)
        return impl_->instance >= 0 && event.cbutton.which == impl_->instance;
    return true;
}
void SdlInputReplay::frame(editor::EditorSdlGameInputContext& c) {
    auto& p = *impl_; check(p.frame < 10000,"replay host frame limit exceeded"); ++p.frame; c.synthetic = true;
    while (p.next < p.events.size() && p.events[p.next].at("frame").get<int>() == p.frame) {
        const auto& e = p.events[p.next++]; const std::string type = e.at("type"); SDL_Event event{};
        if (type == "state") {
            for (auto i = e.begin(); i != e.end(); ++i) if (i.key() != "type" && i.key() != "frame") p.gates[i.key()] = i.value();
            if (e.contains("focus")) {
                event.type = SDL_WINDOWEVENT; event.window.windowID = c.windowId;
                event.window.event = static_cast<Uint8>(e.at("focus").get<bool>() ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST); push(event);
            }
        } else if (type == "attach") p.attach();
        else if (type == "detach") p.detach();
        else if (type == "axis") check(SDL_JoystickSetVirtualAxis(p.pad, e.at("index"), static_cast<Sint16>(e.at("value").get<int>())) == 0, "axis failed");
        else if (type == "button") check(SDL_JoystickSetVirtualButton(p.pad, e.at("index"), static_cast<Uint8>(e.at("value").get<int>())) == 0, "button failed");
        else if (type == "key") {
            event.type = e.at("down").get<bool>() ? SDL_KEYDOWN : SDL_KEYUP; event.key.windowID = c.windowId;
            event.key.keysym.sym = SDL_GetKeyFromName(e.at("key").get<std::string>().c_str());
            event.key.keysym.mod=static_cast<Uint16>(e.value("mod",0));
            event.key.repeat = e.value("repeat", false) ? 1 : 0; push(event);
        } else if (type == "mouse") {
            event.type = e.at("down").get<bool>() ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; event.button.windowID = c.windowId;
            event.button.button = SDL_BUTTON_RIGHT; event.button.x = c.viewport.x + c.viewport.width / 2; event.button.y = c.viewport.y + c.viewport.height / 2; push(event);
        } else if (type == "motion") {
            event.type = SDL_MOUSEMOTION; event.motion.windowID = c.windowId; event.motion.state = SDL_BUTTON_RMASK;
            event.motion.x = c.viewport.x + c.viewport.width / 2; event.motion.y = c.viewport.y + c.viewport.height / 2;
            event.motion.xrel = e.at("x"); event.motion.yrel = e.at("y"); push(event);
        } else if (type == "wheel") {
            event.type = SDL_MOUSEWHEEL; event.wheel.windowID = c.windowId; event.wheel.y = e.at("y"); push(event);
        }
        SDL_GameControllerUpdate(); // Generate real virtual-device transitions before the host drains.
    }
    c.windowFocused = p.gates.at("focus"); c.activeGameViewport = p.gates.at("active");
    c.playing = p.gates.at("playing"); c.paused = p.gates.at("paused"); c.viewportFocused = p.gates.at("viewport_focus");
    c.viewportHovered = p.gates.at("hover"); c.textEditing = p.gates.at("text");
}
} // namespace engine::input
