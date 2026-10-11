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

// Debug render frames can exceed one 60 Hz audio block. Keep enough ready
// PCM for a slow frame without advancing the mixer when SDL's queue is full.
constexpr std::size_t maxQueuedFrames = sampleRate / 6u;
constexpr std::size_t targetQueuedFrames = sampleRate / 8u;
constexpr std::size_t generatedStartFrames = 2048u;

} // namespace

SdlAudioOutput::SdlAudioOutput(AudioMixer& mixer)
    : SdlAudioOutput(mixer, AudioQueueMode::Buffered) {}

SdlAudioOutput::SdlAudioOutput(AudioMixer& mixer, AudioQueueMode mode)
    : mixer_(mixer), mode_(mode), owner_(std::this_thread::get_id()) {
    if (mode != AudioQueueMode::Buffered && mode != AudioQueueMode::Generated)
        throw std::invalid_argument("invalid audio queue mode");
}

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
    queueStarted_ = false;
    primingGenerated_ = false;
    mixedAheadFrames_ = 0;
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
    queueStarted_ = false;
    primingGenerated_ = mode_ == AudioQueueMode::Generated;
    suspended_ = mode_ == AudioQueueMode::Buffered;
    SDL_PauseAudioDevice(device_, 1);
    succeed(diagnostic);
    return true;
}

bool SdlAudioOutput::pump(std::size_t elapsedFrames, std::string* diagnostic) {
    checkOwner();
    if (elapsedFrames == 0u || elapsedFrames > 4096u)
        return fail(diagnostic, "invalid SDL audio pump frame count");
    if (mode_ == AudioQueueMode::Generated) {
        // A synthesizer can only supply samples already produced by its simulation.
        // Consume exactly this block, including silence, without predicting future input.
        buffer_.resize(elapsedFrames * 2u);
        if (!mixer_.render(elapsedFrames, buffer_, diagnostic)) return false;
        lastPeak_ = 0.0f;
        for (const float sample : buffer_)
            lastPeak_ = std::max(lastPeak_, std::abs(sample));
        if (device_ != 0) {
            if (queuedFrames() == 0 && !primingGenerated_) {
                if (queueStarted_) ++underruns_;
                queueStarted_ = false;
                SDL_PauseAudioDevice(device_, 1);
                primingGenerated_ = true;
            }
            // A stalled/minimized host must not accumulate stale audio indefinitely.
            if (queuedFrames() + elapsedFrames > maxQueuedFrames) {
                SDL_PauseAudioDevice(device_, 1);
                SDL_ClearQueuedAudio(device_);
                primingGenerated_ = true;
            }
            if (SDL_QueueAudio(device_, buffer_.data(), static_cast<Uint32>(buffer_.size() * sizeof(float))) != 0) {
                const std::string message = std::string("SDL audio queue failed: ") + SDL_GetError();
                closeDevice();
                return fail(diagnostic, message);
            }
            // Collect actual generated blocks before starting the device. Two device
            // periods absorb scheduling jitter without advancing the synthesizer.
            if (primingGenerated_ && queuedFrames() >= generatedStartFrames) {
                SDL_PauseAudioDevice(device_, 0);
                primingGenerated_ = false;
                suspended_ = false;
                queueStarted_ = true;
            }
        }
        succeed(diagnostic);
        return true;
    }
    const bool hadVoices = mixer_.activeVoices() != 0u;
    if (device_ == 0 || !hadVoices) {
        mixedAheadFrames_ = 0;
        buffer_.resize(elapsedFrames * 2u);
        if (!mixer_.render(elapsedFrames, buffer_, diagnostic)) return false;
        lastPeak_ = 0.0f;
        for (const float sample : buffer_)
            lastPeak_ = std::max(lastPeak_, std::abs(sample));
        succeed(diagnostic);
        return true;
    }
    mixedAheadFrames_ -= static_cast<std::int64_t>(elapsedFrames);
    const std::size_t queued = queuedFrames();
    if (queued == 0 && queueStarted_ && !suspended_) {
        ++underruns_;
        queueStarted_ = false;
    }
    const std::size_t room = queued < maxQueuedFrames ? maxQueuedFrames - queued : 0u;
    const std::size_t due = mixedAheadFrames_ < 0 ?
        static_cast<std::size_t>(-mixedAheadFrames_) : 0u;
    const std::size_t refill = queued < targetQueuedFrames ?
        targetQueuedFrames - queued : 0u;
    std::size_t framesToMix = std::min(room, std::max(due, refill));
    lastPeak_ = 0.0f;
    buffer_.clear();
    while (framesToMix > 0u) {
        const std::size_t chunk = std::min<std::size_t>(framesToMix, 4096u);
        buffer_.resize(chunk * 2u);
        if (!mixer_.render(chunk, buffer_, diagnostic)) return false;
        for (const float sample : buffer_)
            lastPeak_ = std::max(lastPeak_, std::abs(sample));
        const auto byteCount = static_cast<Uint32>(buffer_.size() * sizeof(float));
        if (SDL_QueueAudio(device_, buffer_.data(), byteCount) != 0) {
            const std::string message = std::string("SDL audio queue failed: ") + SDL_GetError();
            closeDevice();
            return fail(diagnostic, message);
        }
        mixedAheadFrames_ += static_cast<std::int64_t>(chunk);
        framesToMix -= chunk;
    }
    if (suspended_ && queuedFrames()>0) { SDL_PauseAudioDevice(device_,0);suspended_=false; }
    if (queuedFrames()>0) queueStarted_=true;
    succeed(diagnostic);
    return true;
}

void SdlAudioOutput::suspend() {
    checkOwner();
    if (device_!=0) {
        SDL_PauseAudioDevice(device_,1);
        SDL_ClearQueuedAudio(device_);
    }
    suspended_=true;primingGenerated_=mode_==AudioQueueMode::Generated;
    queueStarted_=false;
    mixedAheadFrames_=0;lastPeak_=0;buffer_.clear();
}

void SdlAudioOutput::stopAll() {
    checkOwner();
    suspended_=false;
    queueStarted_=false;underruns_=0;
    mixer_.stopAll();
    if (device_ != 0) {
        if (mode_ == AudioQueueMode::Generated) {
            SDL_PauseAudioDevice(device_, 1);
            primingGenerated_ = true;
        } else {
            SDL_PauseAudioDevice(device_, 0);
        }
        SDL_ClearQueuedAudio(device_);
    }
    lastPeak_ = 0.0f;
    mixedAheadFrames_ = 0;
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
