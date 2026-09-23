#pragma once

#include "engine/audio/AudioMixer.h"

#include <SDL_audio.h>

#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace engine::audio {

// Call once per host audio tick with the number of elapsed sample frames.
// The mixer continues through a null sink when SDL has no usable device.
// Destroy before the mixer and before the host's final SDL_Quit.
class SdlAudioOutput {
public:
    explicit SdlAudioOutput(AudioMixer& mixer);
    ~SdlAudioOutput();
    SdlAudioOutput(const SdlAudioOutput&) = delete;
    SdlAudioOutput& operator=(const SdlAudioOutput&) = delete;

    bool start(std::string* diagnostic = nullptr);
    bool pump(std::size_t elapsedFrames, std::string* diagnostic = nullptr);
    void stopAll();
    void shutdown();
    bool deviceReady() const;
    std::size_t queuedFrames() const;
    float lastPeak() const;

private:
    void checkOwner() const;
    void closeDevice();
    AudioMixer& mixer_;
    const std::thread::id owner_;
    SDL_AudioDeviceID device_ = 0;
    bool ownsAudioSubsystem_ = false;
    std::vector<float> buffer_;
    float lastPeak_ = 0.0f;
};

} // namespace engine::audio
