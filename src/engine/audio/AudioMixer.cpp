#include "AudioMixer.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace engine::audio {
namespace {

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

void succeed(std::string* error) {
    if (error) error->clear();
}

bool gainValid(float gain) {
    return std::isfinite(gain) && gain >= 0.0f && gain <= 1.0f;
}

bool busValid(AudioBus bus) {
    return static_cast<std::size_t>(bus) < static_cast<std::size_t>(AudioBus::Count);
}

bool positionValid(const glm::vec3& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) &&
           std::isfinite(position.z) &&
           std::abs(position.x) <= 10000.0f &&
           std::abs(position.y) <= 10000.0f &&
           std::abs(position.z) <= 10000.0f;
}

bool distanceValid(float distance) {
    return std::isfinite(distance) && distance >= 0.1f && distance <= 1000.0f;
}

bool fadeFramesValid(std::uint32_t frames) {
    return frames <= sampleRate * 30u;
}

template <typename FadeType>
void advanceFade(float& gain, FadeType& fade) {
    if (fade.remaining == 0u) return;
    if (fade.remaining == 1u) {
        gain = fade.target;
        fade.remaining = 0u;
        return;
    }
    gain += (fade.target - gain) / static_cast<float>(fade.remaining);
    --fade.remaining;
}

} // namespace

AudioClip::AudioClip(std::uint8_t channels, std::vector<float> samples)
    : channels_(channels), samples_(std::move(samples)) {}

bool AudioClip::create(std::uint32_t sourceSampleRate, std::uint8_t channels,
                       std::span<const float> samples,
                       std::shared_ptr<const AudioClip>& out,
                       std::string* error) {
    if (sourceSampleRate != sampleRate || (channels != 1u && channels != 2u) ||
        samples.empty() || samples.size() > 16u * 1024u * 1024u ||
        samples.size() % channels != 0u)
        return fail(error, "invalid audio clip rate, channel count or size");
    for (const float sample : samples) {
        if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f)
            return fail(error, "invalid audio clip sample");
    }
    auto candidate = std::shared_ptr<const AudioClip>(new AudioClip(
        channels, std::vector<float>(samples.begin(), samples.end())));
    out = std::move(candidate);
    succeed(error);
    return true;
}

AudioMixer::AudioMixer() : owner_(std::this_thread::get_id()) {}

AudioMixer::~AudioMixer() {
    if (owner_ != std::this_thread::get_id()) std::terminate();
}

void AudioMixer::checkOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("AudioMixer used outside its owner thread");
}

bool AudioMixer::play(std::shared_ptr<const AudioClip> clip,
                      const AudioPlayOptions& options, AudioVoiceId& out,
                      std::string* error) {
    checkOwner();
    if (!clip || !busValid(options.bus) || !gainValid(options.gain) ||
        !positionValid(options.emitterPosition) ||
        !distanceValid(options.maxDistance) || !fadeFramesValid(options.spatialFadeFrames) ||
        (options.spatial && clip->channels() != 1u))
        return fail(error, "invalid audio play request");
    if (voices_.size() >= 64u ||
        nextVoiceId_ == (std::numeric_limits<AudioVoiceId>::max)())
        return fail(error, "audio voice capacity exhausted");
    Voice voice;
    voice.id = nextVoiceId_;
    voice.clip = std::move(clip);
    voice.options = options;
    voice.gain = options.gain;
    updateSpatial(voice,true);
    voices_.push_back(std::move(voice));
    out = nextVoiceId_++;
    succeed(error);
    return true;
}

bool AudioMixer::stop(AudioVoiceId id, std::string* error) {
    checkOwner();
    const auto it = std::find_if(voices_.begin(), voices_.end(),
        [id](const Voice& voice) { return voice.id == id; });
    if (it == voices_.end()) return fail(error, "audio voice is unavailable");
    voices_.erase(it);
    succeed(error);
    return true;
}

void AudioMixer::stopAll() {
    checkOwner();
    voices_.clear();
}

void AudioMixer::reset() {
    checkOwner();
    voices_.clear();
    masterGain_ = 1.0f;
    busGains_.fill(1.0f);
    busFades_.fill({});
    muted_ = false;
    listenerPosition_ = glm::vec3(0.0f);
    listenerRight_ = glm::vec3(1.0f, 0.0f, 0.0f);
}

bool AudioMixer::fadeVoice(AudioVoiceId id, float targetGain,
                           std::uint32_t durationFrames,
                           std::string* error) {
    checkOwner();
    if (!gainValid(targetGain) || !fadeFramesValid(durationFrames))
        return fail(error, "invalid audio voice fade");
    const auto it = std::find_if(voices_.begin(), voices_.end(),
        [id](const Voice& voice) { return voice.id == id; });
    if (it == voices_.end()) return fail(error, "audio voice is unavailable");
    if (durationFrames == 0u) {
        if (targetGain == 0.0f && !it->options.retainWhenSilent) voices_.erase(it);
        else {
            it->gain = targetGain;
            it->fade = {};
        }
    } else {
        it->fade = {targetGain, durationFrames};
    }
    succeed(error);
    return true;
}

bool AudioMixer::setMasterGain(float gain, std::string* error) {
    checkOwner();
    if (!gainValid(gain)) return fail(error, "invalid master gain");
    masterGain_ = gain;
    succeed(error);
    return true;
}

bool AudioMixer::setBusGain(AudioBus bus, float gain, std::string* error) {
    checkOwner();
    if (!busValid(bus) || !gainValid(gain))
        return fail(error, "invalid audio bus or gain");
    const std::size_t index = static_cast<std::size_t>(bus);
    busGains_[index] = gain;
    busFades_[index] = {};
    succeed(error);
    return true;
}

bool AudioMixer::fadeBus(AudioBus bus, float targetGain,
                         std::uint32_t durationFrames,
                         std::string* error) {
    checkOwner();
    if (!busValid(bus) || !gainValid(targetGain) ||
        !fadeFramesValid(durationFrames))
        return fail(error, "invalid audio bus fade");
    const std::size_t index = static_cast<std::size_t>(bus);
    if (durationFrames == 0u) {
        busGains_[index] = targetGain;
        busFades_[index] = {};
    } else {
        busFades_[index] = {targetGain, durationFrames};
    }
    succeed(error);
    return true;
}

void AudioMixer::setMuted(bool muted) {
    checkOwner();
    muted_ = muted;
}

bool AudioMixer::setListener(const glm::vec3& position,
                             const glm::vec3& right, std::string* error) {
    checkOwner();
    if (!positionValid(position) || !positionValid(right) ||
        glm::length(right) < 0.1f)
        return fail(error, "invalid audio listener");
    listenerPosition_ = position;
    listenerRight_ = glm::normalize(right);
    for (auto& voice : voices_) updateSpatial(voice);
    succeed(error);
    return true;
}

bool AudioMixer::setEmitter(AudioVoiceId id, const glm::vec3& position,
                            float maxDistance, std::string* error) {
    checkOwner();
    if (!positionValid(position) || !distanceValid(maxDistance))
        return fail(error, "invalid audio emitter");
    const auto it = std::find_if(voices_.begin(), voices_.end(),
        [id](const Voice& voice) { return voice.id == id; });
    if (it == voices_.end() || !it->options.spatial)
        return fail(error, "spatial audio voice is unavailable");
    it->options.emitterPosition = position;
    it->options.maxDistance = maxDistance;
    updateSpatial(*it);
    succeed(error);
    return true;
}

void AudioMixer::updateSpatial(Voice& voice, bool immediate) {
    if (!voice.options.spatial) return;
    const auto offset=voice.options.emitterPosition-listenerPosition_;
    const float distance=glm::length(offset);
    const float attenuation=(std::max)(0.f,1.f-distance/voice.options.maxDistance);
    const float pan=distance>1.e-6f?std::clamp(glm::dot(offset/distance,listenerRight_),-1.f,1.f):0;
    const std::array gains{attenuation*std::sqrt((1-pan)*.5f),attenuation*std::sqrt((1+pan)*.5f)};
    for (std::size_t i=0;i<2;++i) {
        if (immediate || voice.options.spatialFadeFrames==0) {
            voice.spatialGains[i]=gains[i];voice.spatialFades[i]={gains[i],0};
        } else if (voice.spatialFades[i].target!=gains[i])
            voice.spatialFades[i]={gains[i],voice.options.spatialFadeFrames};
    }
}

bool AudioMixer::render(std::size_t frames,
                        std::span<float> stereoInterleaved,
                        std::string* error) {
    checkOwner();
    if (frames == 0u || frames > 4096u ||
        stereoInterleaved.size() != frames * 2u)
        return fail(error, "invalid audio render buffer");
    std::vector<float> mixed(frames * 2u, 0.0f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t bus = 0; bus < busGains_.size(); ++bus)
            advanceFade(busGains_[bus], busFades_[bus]);
        for (Voice& voice : voices_) {
            if (voice.finished) continue;
            advanceFade(voice.gain, voice.fade);
            if (voice.gain == 0.0f && voice.fade.remaining == 0u &&
                voice.fade.target == 0.0f && !voice.options.retainWhenSilent) {
                voice.finished = true;
                continue;
            }
            const auto samples = voice.clip->samples();
            const std::size_t source = voice.frame * voice.clip->channels();
            float left = samples[source];
            float right = voice.clip->channels() == 2u ? samples[source + 1u] : left;
            if (voice.options.spatial) {
                advanceFade(voice.spatialGains[0],voice.spatialFades[0]);
                advanceFade(voice.spatialGains[1],voice.spatialFades[1]);
                left *= voice.spatialGains[0];
                right *= voice.spatialGains[1];
            }
            const float gain = muted_ ? 0.0f :
                masterGain_ * busGains_[static_cast<std::size_t>(voice.options.bus)] *
                voice.gain;
            mixed[frame * 2u] += left * gain;
            mixed[frame * 2u + 1u] += right * gain;
            ++voice.frame;
            if (voice.frame == voice.clip->frameCount()) {
                if (voice.options.loop) voice.frame = 0u;
                else voice.finished = true;
            }
        }
    }
    std::erase_if(voices_, [](const Voice& voice) { return voice.finished; });
    for (std::size_t i = 0; i < mixed.size(); ++i)
        stereoInterleaved[i] = std::clamp(mixed[i], -1.0f, 1.0f);
    succeed(error);
    return true;
}

std::size_t AudioMixer::activeVoices() const {
    checkOwner();
    return voices_.size();
}

} // namespace engine::audio
