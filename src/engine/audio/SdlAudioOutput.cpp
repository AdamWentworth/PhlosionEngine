#include "SdlAudioOutput.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace engine::audio {
namespace {

bool fail(std::string* diagnostic, const std::string& message) {
    if (diagnostic) *diagnostic = message;
    return false;
}

void succeed(std::string* diagnostic) {
    if (diagnostic) diagnostic->clear();
}

constexpr std::size_t maxQueuedFrames = sampleRate / 10u;

} // namespace

SdlAudioOutput::SdlAudioOutput(AudioMixer& mixer)
    : mixer_(mixer), owner_(std::this_thread::get_id()) {}

SdlAudioOutput::~SdlAudioOutput() {
    if (owner_ != std::this_thread::get_id()) std::terminate();
    shutdown();
}

void SdlAudioOutput::checkOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("SdlAudioOutput used outside its owner thread");
}

void SdlAudioOutput::closeDevice() {
    if (device_ == 0) return;
    SDL_ClearQueuedAudio(device_);
    SDL_CloseAudioDevice(device_);
    device_ = 0;
}

bool SdlAudioOutput::start(std::string* diagnostic) {
    checkOwner();
    if (device_ != 0) {
        succeed(diagnostic);
        return true;
    }
    if (!ownsAudioSubsystem_) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
            return fail(diagnostic, std::string("SDL audio initialization failed: ") + SDL_GetError());
        ownsAudioSubsystem_ = true;
    }
    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(sampleRate);
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 1024;
    SDL_AudioSpec obtained{};
    const SDL_AudioDeviceID opened = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (opened == 0)
        return fail(diagnostic, std::string("SDL audio device open failed: ") + SDL_GetError());
    if (obtained.freq != desired.freq || obtained.format != desired.format ||
        obtained.channels != desired.channels) {
        SDL_CloseAudioDevice(opened);
        return fail(diagnostic, "SDL audio device did not match the mixer format");
    }
    device_ = opened;
    SDL_PauseAudioDevice(device_, 0);
    succeed(diagnostic);
    return true;
}

bool SdlAudioOutput::pump(std::size_t elapsedFrames, std::string* diagnostic) {
    checkOwner();
    if (elapsedFrames == 0u || elapsedFrames > 4096u)
        return fail(diagnostic, "invalid SDL audio pump frame count");
    const bool hadVoices = mixer_.activeVoices() != 0u;
    buffer_.resize(elapsedFrames * 2u);
    if (!mixer_.render(elapsedFrames, buffer_, diagnostic)) return false;
    lastPeak_ = 0.0f;
    for (const float sample : buffer_)
        lastPeak_ = std::max(lastPeak_, std::abs(sample));
    if (device_ == 0 || !hadVoices) {
        succeed(diagnostic);
        return true;
    }
    const std::size_t queued = queuedFrames();
    if (queued >= maxQueuedFrames || elapsedFrames > maxQueuedFrames - queued) {
        succeed(diagnostic);
        return true;
    }
    const auto byteCount = static_cast<Uint32>(buffer_.size() * sizeof(float));
    if (SDL_QueueAudio(device_, buffer_.data(), byteCount) != 0) {
        const std::string message = std::string("SDL audio queue failed: ") + SDL_GetError();
        closeDevice();
        return fail(diagnostic, message);
    }
    succeed(diagnostic);
    return true;
}

void SdlAudioOutput::stopAll() {
    checkOwner();
    mixer_.stopAll();
    if (device_ != 0) SDL_ClearQueuedAudio(device_);
    lastPeak_ = 0.0f;
}

void SdlAudioOutput::shutdown() {
    checkOwner();
    stopAll();
    closeDevice();
    if (ownsAudioSubsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        ownsAudioSubsystem_ = false;
    }
    buffer_.clear();
}

bool SdlAudioOutput::deviceReady() const {
    checkOwner();
    return device_ != 0;
}

std::size_t SdlAudioOutput::queuedFrames() const {
    checkOwner();
    return device_ == 0 ? 0u : SDL_GetQueuedAudioSize(device_) / (2u * sizeof(float));
}

float SdlAudioOutput::lastPeak() const {
    checkOwner();
    return lastPeak_;
}

} // namespace engine::audio
