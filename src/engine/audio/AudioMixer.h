#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace engine::audio {

inline constexpr std::uint32_t sampleRate = 48000u;

enum class AudioBus : std::uint8_t { Music, Sfx, Ambient, Voice, Count };

class AudioClip {
public:
    static bool create(std::uint32_t sourceSampleRate, std::uint8_t channels,
                       std::span<const float> samples,
                       std::shared_ptr<const AudioClip>& out,
                       std::string* error = nullptr);

    std::uint8_t channels() const { return channels_; }
    std::size_t frameCount() const { return samples_.size() / channels_; }
    std::span<const float> samples() const { return samples_; }

private:
    AudioClip(std::uint8_t channels, std::vector<float> samples);
    std::uint8_t channels_ = 0;
    std::vector<float> samples_;
};

using AudioVoiceId = std::uint64_t;

struct AudioPlayOptions {
    AudioBus bus = AudioBus::Sfx;
    float gain = 1.0f;
    bool loop = false;
    bool spatial = false;
    glm::vec3 emitterPosition{0.0f};
    float maxDistance = 50.0f;
    // Ambient layers may reach silence while retaining their loop phase.
    bool retainWhenSilent = false;
};

// Main-thread deterministic mixer. Render fills stereo float PCM; a platform
// adapter may queue it to a device. It owns clips while voices use them.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;
    AudioMixer(AudioMixer&&) = delete;
    AudioMixer& operator=(AudioMixer&&) = delete;

    bool play(std::shared_ptr<const AudioClip> clip,
              const AudioPlayOptions& options, AudioVoiceId& out,
              std::string* error = nullptr);
    bool stop(AudioVoiceId id, std::string* error = nullptr);
    void stopAll();
    void reset();
    bool fadeVoice(AudioVoiceId id, float targetGain,
                   std::uint32_t durationFrames, std::string* error = nullptr);
    bool setMasterGain(float gain, std::string* error = nullptr);
    bool setBusGain(AudioBus bus, float gain, std::string* error = nullptr);
    bool fadeBus(AudioBus bus, float targetGain,
                 std::uint32_t durationFrames, std::string* error = nullptr);
    void setMuted(bool muted);
    bool setListener(const glm::vec3& position, const glm::vec3& right,
                     std::string* error = nullptr);
    bool setEmitter(AudioVoiceId id, const glm::vec3& position,
                    float maxDistance, std::string* error = nullptr);
    bool render(std::size_t frames, std::span<float> stereoInterleaved,
                std::string* error = nullptr);
    std::size_t activeVoices() const;

private:
    struct Fade {
        float target = 1.0f;
        std::uint32_t remaining = 0u;
    };
    struct Voice {
        AudioVoiceId id = 0;
        std::shared_ptr<const AudioClip> clip;
        AudioPlayOptions options;
        std::size_t frame = 0;
        float gain = 1.0f;
        Fade fade;
        bool finished = false;
    };
    void checkOwner() const;
    const std::thread::id owner_;
    std::vector<Voice> voices_;
    AudioVoiceId nextVoiceId_ = 1;
    float masterGain_ = 1.0f;
    std::array<float, static_cast<std::size_t>(AudioBus::Count)> busGains_{1, 1, 1, 1};
    std::array<Fade, static_cast<std::size_t>(AudioBus::Count)> busFades_{};
    bool muted_ = false;
    glm::vec3 listenerPosition_{0.0f};
    glm::vec3 listenerRight_{1.0f, 0.0f, 0.0f};
};

} // namespace engine::audio
