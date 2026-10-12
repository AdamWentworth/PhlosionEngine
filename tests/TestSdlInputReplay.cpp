#include "engine/input/SdlInputReplay.h"
#include <fstream>
#include <string>
#include <stdexcept>

bool test_sdl_replay_controller_isolation(std::string& outFail) {
    const auto path = std::filesystem::temp_directory_path() / "phlosion-controller-replay-test.json";
    try {
        std::ofstream(path) << R"({"schema":"phlosion-sdl-replay-v1","events":[{"frame":1,"type":"attach"},{"frame":2,"type":"detach"},{"frame":2,"type":"key","key":"Delete","down":true,"mod":64}]})";
        engine::input::SdlInputReplay replay(path);
        const auto check = [](bool value) { if (!value) throw std::runtime_error("Replay controller isolation failed"); };
        SDL_Event button{};
        button.type = SDL_CONTROLLERBUTTONDOWN;
        button.cbutton.which = -42;
        check(!replay.acceptsEvent(button));
        SDL_Event key{};
        key.type = SDL_KEYDOWN;
        check(replay.acceptsEvent(key));
        engine::editor::EditorSdlGameInputContext context{};
        replay.frame(context);
        SDL_JoystickID owned = -1;
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_JoystickIsVirtual(i) && std::string(SDL_JoystickNameForIndex(i)) == "Phlosion owned replay controller")
                owned = SDL_JoystickGetDeviceInstanceID(i);
        check(owned >= 0 && !replay.acceptsEvent(button));
        button.cbutton.which = owned;
        check(replay.acceptsEvent(button));
        button.type = SDL_CONTROLLERBUTTONUP;
        check(replay.acceptsEvent(button));
        SDL_Event axis{};
        axis.type = SDL_CONTROLLERAXISMOTION;
        axis.caxis.which = owned;
        check(replay.acceptsEvent(axis));
        axis.caxis.which = -42;
        check(!replay.acceptsEvent(axis));
        replay.frame(context);
        check(!replay.acceptsEvent(button));
        bool shortcut=false;SDL_Event event{};
        while(SDL_PollEvent(&event)) if(event.type==SDL_KEYDOWN&&event.key.keysym.sym==SDLK_DELETE) {
            check(event.key.keysym.mod==KMOD_LCTRL&&event.key.windowID==context.windowId);shortcut=true;
        }
        check(shortcut);
        for (const auto mod : {"-1","65536","true"}) {
            std::ofstream(path)<<"{\"schema\":\"phlosion-sdl-replay-v1\",\"events\":[{\"frame\":1,\"type\":\"key\",\"key\":\"Delete\",\"down\":true,\"mod\":"<<mod<<"}]}";
            bool rejected=false;
            try { engine::input::SdlInputReplay invalid(path); } catch(const std::runtime_error&) { rejected=true; }
            check(rejected);
        }
        std::filesystem::remove(path);
        return true;
    } catch (const std::exception& error) {
        outFail = error.what();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return false;
    }
}
