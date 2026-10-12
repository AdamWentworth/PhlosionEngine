#include "engine/audio/AudioMixer.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace engine::audio;

namespace {

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)

bool near(float a, float b, float tolerance = 1.0e-5f) {
    return std::abs(a - b) <= tolerance;
}

std::shared_ptr<const AudioClip> clip(std::span<const float> samples,
                                      std::uint8_t channels = 1u) {
    std::shared_ptr<const AudioClip> result;
    CHECK(AudioClip::create(sampleRate, channels, samples, result));
    return result;
}

void checkMixAndClipLifetime() {
    std::array<float, 2> source{0.25f, 0.5f};
    auto sample = clip(source);
    source[0] = 0.0f; // The clip owns a copy of its source samples.
    AudioMixer mixer;
    AudioVoiceId id = 0;
    CHECK(mixer.play(sample, {}, id));
    CHECK(id != 0 && mixer.activeVoices() == 1u);
    sample.reset();
    std::array<float, 4> output{};
    CHECK(mixer.render(2u, output));
    CHECK(near(output[0], 0.25f) && near(output[1], 0.25f));
    CHECK(near(output[2], 0.5f) && near(output[3], 0.5f));
    CHECK(mixer.activeVoices() == 0u);
    const std::array<float, 2> stereo{0.2f, -0.4f};
    CHECK(mixer.play(clip(stereo, 2u), {}, id));
    std::array<float, 2> stereoOutput{};
    CHECK(mixer.render(1u, stereoOutput));
    CHECK(near(stereoOutput[0], 0.2f));
    CHECK(near(stereoOutput[1], -0.4f));
    const std::array<float, 1> loud{1.0f};
    CHECK(mixer.play(clip(loud), {}, id));
    CHECK(mixer.play(clip(loud), {}, id));
    CHECK(mixer.render(1u, stereoOutput));
    CHECK(near(stereoOutput[0], 1.0f) && near(stereoOutput[1], 1.0f));
}

void checkBusesMuteLoopAndReset() {
    AudioMixer mixer;
    const std::array<float, 2> source{1.0f, 0.5f};
    AudioVoiceId id = 0;
    AudioPlayOptions options;
    options.bus = AudioBus::Music;
    CHECK(mixer.setMasterGain(0.5f));
    CHECK(mixer.setBusGain(AudioBus::Music, 0.5f));
    CHECK(mixer.play(clip(source), options, id));
    std::array<float, 2> output{};
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], 0.25f));
    mixer.setMuted(true);
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], 0.0f) && mixer.activeVoices() == 0u);
    mixer.setMuted(false);
    options.loop = true;
    CHECK(mixer.play(clip(source), options, id));
    std::array<float, 10> looped{};
    CHECK(mixer.render(5u, looped));
    CHECK(near(looped[0], 0.25f) && near(looped[2], 0.125f));
    CHECK(near(looped[4], 0.25f) && near(looped[8], 0.25f));
    CHECK(mixer.activeVoices() == 1u);
    mixer.stopAll();
    CHECK(mixer.activeVoices() == 0u);
    CHECK(!mixer.stop(id));
    mixer.reset();
    CHECK(mixer.play(clip(source), options, id));
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], 1.0f));
}

void checkFades() {
    const std::array<float, 1> source{1.0f};
    AudioMixer mixer;
    AudioVoiceId id = 0;
    AudioPlayOptions options;
    options.loop = true;
    CHECK(mixer.play(clip(source), options, id));
    CHECK(mixer.fadeVoice(id, 0.0f, 4u));
    std::array<float, 8> output{};
    CHECK(mixer.render(4u, output));
    CHECK(near(output[0], 0.75f) && near(output[2], 0.5f));
    CHECK(near(output[4], 0.25f) && near(output[6], 0.0f));
    CHECK(mixer.activeVoices() == 0u);
    CHECK(mixer.play(clip(source), options, id));
    CHECK(mixer.fadeBus(AudioBus::Sfx, 0.0f, 4u));
    CHECK(mixer.render(4u, output));
    CHECK(near(output[0], 0.75f) && near(output[2], 0.5f));
    CHECK(near(output[4], 0.25f) && near(output[6], 0.0f));
    CHECK(mixer.activeVoices() == 1u);
    CHECK(mixer.fadeBus(AudioBus::Sfx, 1.0f, 0u));
    CHECK(mixer.fadeVoice(id, 0.0f, 0u));
    CHECK(mixer.activeVoices() == 0u);
}

void checkSpatialPanAndMotion() {
    AudioMixer mixer;
    const std::array<float, 1> source{1.0f};
    AudioPlayOptions options;
    options.spatial = true;
    options.loop = true;
    options.emitterPosition = {10.0f, 0.0f, 0.0f};
    options.maxDistance = 20.0f;
    AudioVoiceId id = 0;
    CHECK(mixer.play(clip(source), options, id));
    std::array<float, 2> output{};
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], 0.0f) && near(output[1], 0.5f));
    CHECK(mixer.setEmitter(id, {-10.0f, 0.0f, 0.0f}, 20.0f));
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], 0.5f) && near(output[1], 0.0f));
    CHECK(mixer.setEmitter(id, {0.0f, 0.0f, 0.0f}, 20.0f));
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], std::sqrt(0.5f)));
    CHECK(near(output[1], std::sqrt(0.5f)));
    CHECK(mixer.setEmitter(id, {20.0f, 0.0f, 0.0f}, 20.0f));
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], 0.0f) && near(output[1], 0.0f));
    CHECK(mixer.setListener({0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}));
    CHECK(mixer.setEmitter(id, {10.0f, 0.0f, 0.0f}, 20.0f));
    CHECK(mixer.render(1u, output));
    CHECK(near(output[0], 0.5f) && near(output[1], 0.0f));
}

void checkInvalidInputsAndOwnership() {
    std::shared_ptr<const AudioClip> accepted;
    const std::array<float, 2> source{0.2f, 0.4f};
    CHECK(AudioClip::create(sampleRate, 1u, source, accepted));
    const auto prior = accepted;
    std::string error;
    CHECK(!AudioClip::create(44100u, 1u, source, accepted, &error));
    CHECK(accepted == prior && !error.empty());
    const std::array<float, 1> invalid{(std::numeric_limits<float>::quiet_NaN)()};
    CHECK(!AudioClip::create(sampleRate, 1u, invalid, accepted, &error));
    CHECK(accepted == prior);
    AudioMixer mixer;
    AudioVoiceId id = 42u;
    AudioPlayOptions options;
    options.gain = 2.0f;
    CHECK(!mixer.play(prior, options, id, &error));
    CHECK(id == 42u && mixer.activeVoices() == 0u);
    options = {};
    options.maxDistance = (std::numeric_limits<float>::quiet_NaN)();
    CHECK(!mixer.play(prior, options, id, &error));
    CHECK(id == 42u && mixer.activeVoices() == 0u);
    options = {};
    CHECK(mixer.play(prior, options, id, &error));
    std::array<float, 2> output{9.0f, 9.0f};
    CHECK(!mixer.render(2u, output, &error));
    CHECK(output[0] == 9.0f && mixer.activeVoices() == 1u);
    CHECK(!mixer.fadeVoice(id, -1.0f, 1u, &error));
    CHECK(!mixer.setBusGain(static_cast<AudioBus>(255), 0.5f, &error));
    CHECK(!mixer.setListener({}, {}, &error));
    CHECK(!mixer.setEmitter(id, {}, 10.0f, &error));
    CHECK(mixer.render(1u, output, &error));
    CHECK(near(output[0], 0.2f));
    bool wrongThreadRejected = false;
    std::thread worker([&] {
        try {
            (void)mixer.activeVoices();
        } catch (const std::logic_error&) {
            wrongThreadRejected = true;
        }
    });
    worker.join();
    CHECK(wrongThreadRejected);
}

} // namespace

void checkSilentAmbientPhase() {
    AudioMixer mixer;
    const std::array<float, 6> samples{.1f,.1f,.2f,.2f,.3f,.3f};
    std::shared_ptr<const AudioClip> source;
    CHECK(AudioClip::create(sampleRate,2,samples,source));
    AudioPlayOptions options;options.loop=true;options.gain=0;options.retainWhenSilent=true;
    AudioVoiceId id=0;CHECK(mixer.play(source,options,id));
    std::array<float,2> output{};
    CHECK(mixer.render(1,output) && output[0]==0 && mixer.activeVoices()==1);
    CHECK(mixer.fadeVoice(id,1,0));
    CHECK(mixer.render(1,output) && near(output[0],.2f));
    CHECK(mixer.fadeVoice(id,0,1));
    CHECK(mixer.render(1,output) && output[0]==0 && mixer.activeVoices()==1);
    CHECK(mixer.fadeVoice(id,0,0) && mixer.activeVoices()==1);
    CHECK(mixer.fadeVoice(id,1,0));
    CHECK(mixer.render(1,output) && near(output[0],.1f));
    mixer.stopAll();CHECK(mixer.activeVoices()==0);
}

void checkSpatialRamps() {
    AudioMixer mixer;AudioVoiceId id=0;
    const std::array<float,1> source{1};
    AudioPlayOptions options;options.spatial=true;options.loop=true;
    options.emitterPosition={10,0,0};options.maxDistance=20;options.spatialFadeFrames=4;
    CHECK(mixer.play(clip(source),options,id));
    std::array<float,8> output{};
    CHECK(mixer.setListener({}, {-1,0,0}));
    CHECK(mixer.render(4,output));
    CHECK(near(output[0],.125f) && near(output[1],.375f));
    CHECK(near(output[6],.5f) && near(output[7],0));
    CHECK(mixer.setEmitter(id,{30,0,0},20));
    CHECK(mixer.render(4,output));
    CHECK(near(output[0],.375f) && near(output[6],0));
    CHECK(mixer.setEmitter(id,{-10,0,0},20));
    std::array<float,2> single{};
    CHECK(mixer.render(1,single) && near(single[1],.125f));
    // Identical pose updates must not restart a partially completed ramp.
    CHECK(mixer.setListener({}, {-1,0,0}));
    CHECK(mixer.render(1,single) && near(single[1],.25f));
    CHECK(mixer.render(1,single) && near(single[1],.375f));
    CHECK(mixer.render(1,single) && near(single[1],.5f));
    options.spatialFadeFrames=sampleRate*31;
    CHECK(!mixer.play(clip(source),options,id));
}

int main() {
    try {
        checkMixAndClipLifetime();
        checkBusesMuteLoopAndReset();
        checkFades();
        checkSilentAmbientPhase();
        checkSpatialPanAndMotion();
        checkSpatialRamps();
        checkInvalidInputsAndOwnership();
        std::cout << "[PASS] audio mixer contracts\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] audio mixer contracts: " << exception.what() << '\n';
        return 1;
    }
}
