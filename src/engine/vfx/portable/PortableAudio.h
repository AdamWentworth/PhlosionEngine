#pragma once

#include "engine/audio/AudioMixer.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace vfx::portable {

struct AudioCue {
    int startMs = 0;
    std::shared_ptr<const engine::audio::AudioClip> clip;
};

// Decode package-contained PCM WAV cues into the engine mixer's 48 kHz format.
std::vector<AudioCue> loadAudio(const std::filesystem::path& manifest);
std::shared_ptr<const engine::audio::AudioClip> loadPcmWav(const std::filesystem::path& file);

} // namespace vfx::portable
