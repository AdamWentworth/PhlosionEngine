#define SDL_MAIN_HANDLED
#include "engine/audio/SdlAudioOutput.h"

#include <SDL.h>

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace engine::audio;

namespace {

#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #c + " SDL: " + SDL_GetError()); } while (false)

std::shared_ptr<const AudioClip> clip() {
    const std::array samples{1.0f, -1.0f, 0.5f, -0.5f};
    std::shared_ptr<const AudioClip> result;
    CHECK(AudioClip::create(sampleRate, 2, samples, result));
    return result;
}

void failureSinkRetryAndCleanup() {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "phlosion-invalid-driver", 1) == 0);
    AudioMixer mixer;
    SdlAudioOutput output(mixer);
    std::string diagnostic;
    CHECK(!output.start(&diagnostic));
    CHECK(!diagnostic.empty());
    CHECK(!output.deviceReady());
    AudioVoiceId id = 0;
    CHECK(mixer.play(clip(), {}, id));
    CHECK(output.pump(2));
    CHECK(mixer.activeVoices() == 0);
    CHECK(output.queuedFrames() == 0);
    CHECK(!output.pump(0, &diagnostic));
    CHECK(!diagnostic.empty());

    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    CHECK(output.start(&diagnostic));
    CHECK(diagnostic.empty());
    CHECK(output.deviceReady());
    CHECK(output.start());
    AudioPlayOptions looping;
    looping.loop = true;
    CHECK(mixer.play(clip(), looping, id));
    for (int i = 0; i < 8; ++i) {
        CHECK(output.pump(4096));
        CHECK(output.queuedFrames() <= sampleRate / 10u);
    }
    CHECK(mixer.activeVoices() == 1);
    output.stopAll();
    CHECK(mixer.activeVoices() == 0);
    CHECK(output.queuedFrames() == 0);
    CHECK(mixer.play(clip(), looping, id));
    output.shutdown();
    CHECK(!output.deviceReady());
    CHECK(mixer.activeVoices() == 0);
    CHECK(output.pump(2));
    CHECK(output.start());
    CHECK(output.deviceReady());
    output.shutdown();
}

void subsystemAndThreadOwnership() {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    CHECK(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
    {
        AudioMixer mixer;
        SdlAudioOutput output(mixer);
        CHECK(output.start());
        bool rejected = false;
        std::thread other([&] {
            try { (void)output.deviceReady(); }
            catch (const std::logic_error&) { rejected = true; }
        });
        other.join();
        CHECK(rejected);
        output.shutdown();
        CHECK((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

} // namespace

int main() {
    try {
        failureSinkRetryAndCleanup();
        subsystemAndThreadOwnership();
        std::cout << "SDL audio output contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
