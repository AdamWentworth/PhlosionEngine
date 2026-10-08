#define SDL_MAIN_HANDLED
#include "engine/audio/SdlAudioOutput.h"

#include <SDL.h>

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace engine::audio;

namespace {

#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #c + " SDL: " + SDL_GetError()); } while (false)

std::shared_ptr<const AudioClip> clip() {
    const std::array samples{1.0f, -1.0f, 0.5f, -0.5f};
    std::shared_ptr<const AudioClip> result;
    CHECK(AudioClip::create(sampleRate, 2, samples, result));
    return result;
}

std::shared_ptr<const AudioClip> longerClip() {
    const std::vector<float> samples(10000u * 2u, 0.25f);
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
    CHECK(output.lastPeak() > 0.0f);
    CHECK(!output.pump(0, &diagnostic));
    CHECK(!diagnostic.empty());

    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    CHECK(output.start(&diagnostic));
    CHECK(diagnostic.empty());
    CHECK(output.deviceReady());
    CHECK(output.start());
    CHECK(output.pump(800));
    CHECK(output.queuedFrames() == 0);
    CHECK(output.lastPeak() == 0.0f);
    AudioPlayOptions looping;
    looping.loop = true;
    CHECK(mixer.play(clip(), looping, id));
    CHECK(output.pump(800));
    CHECK(output.queuedFrames() > 5000u);
    CHECK(output.lastPeak() > 0.0f);
    for (int i = 0; i < 8; ++i) {
        CHECK(output.pump(4096));
        CHECK(output.queuedFrames() <= sampleRate / 6u);
    }
    CHECK(mixer.activeVoices() == 1);
    output.stopAll();
    CHECK(mixer.activeVoices() == 0);
    CHECK(output.queuedFrames() == 0);
    CHECK(output.lastPeak() == 0.0f);
    CHECK(mixer.play(longerClip(), {}, id));
    for (int i = 0; i < 8; ++i) CHECK(output.pump(4096));
    CHECK(mixer.activeVoices() == 1);
    output.stopAll();
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

void generatedBlocksDoNotReadAhead() {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    AudioMixer mixer;
    SdlAudioOutput output(mixer, AudioQueueMode::Generated);
    CHECK(output.start());
    AudioVoiceId id = 0;
    CHECK(mixer.play(longerClip(), {}, id));
    CHECK(output.pump(800));
    CHECK(output.queuedFrames() == 800u);
    SDL_Delay(50); // Startup must retain real blocks until the priming threshold.
    CHECK(output.queuedFrames() == 800u);
    CHECK(mixer.activeVoices() == 1u);
    for (int i = 0; i < 11; ++i) CHECK(output.pump(800));
    CHECK(mixer.activeVoices() == 1u); // Exactly 9600 of 10000 samples consumed.
    CHECK(output.pump(400));
    CHECK(mixer.activeVoices() == 0u);
    CHECK(output.queuedFrames() <= sampleRate / 6u);
    CHECK(!output.pump(4097));
    output.stopAll();
    CHECK(output.queuedFrames() == 0u);
    CHECK(output.pump(800)); // Silent blocks preserve the generated timeline.
    CHECK(output.lastPeak() == 0.0f);
    output.shutdown();
    CHECK(mixer.play(clip(), {}, id));
    CHECK(output.pump(2));
    CHECK(mixer.activeVoices() == 0u);
    CHECK(output.lastPeak() > 0.0f);
    CHECK(output.queuedFrames() == 0u);
}

void generatedOverflowReprimes() {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    AudioMixer mixer;
    SdlAudioOutput output(mixer, AudioQueueMode::Generated);
    CHECK(output.start());
    bool discarded = false;
    for (int i = 0; i < 2000 && !discarded; ++i) {
        const auto before = output.queuedFrames();
        CHECK(output.pump(128));
        discarded = before > sampleRate / 6u - 128u && output.queuedFrames() == 128u;
    }
    CHECK(discarded);
    SDL_Delay(60); // A tiny replacement block must not play straight into an underrun.
    CHECK(output.queuedFrames() == 128u);
    output.stopAll();
    CHECK(output.pump(800));
    SDL_Delay(50);
    CHECK(output.queuedFrames() == 800u);
}

} // namespace

int main() {
    try {
        failureSinkRetryAndCleanup();
        subsystemAndThreadOwnership();
        generatedBlocksDoNotReadAhead();
        generatedOverflowReprimes();
        std::cout << "SDL audio output contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
