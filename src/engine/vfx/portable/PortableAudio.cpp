#include "engine/vfx/portable/PortableAudio.h"

#include "vfxlibrary/Effect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace vfx::portable {
namespace {

std::uint16_t u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("Truncated PCM WAV");
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Truncated PCM WAV");
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8) |
        (std::uint32_t(data[offset + 2]) << 16) |
        (std::uint32_t(data[offset + 3]) << 24);
}

std::shared_ptr<const engine::audio::AudioClip> decodeWav(
    const vfxlibrary::AudioCue& cue) {
    std::ifstream file(cue.file, std::ios::binary);
    if (!file) throw std::runtime_error("Missing portable effect WAV: " + cue.file.string());
    const std::vector<std::uint8_t> bytes(
        std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    if (bytes.size() < 12 || u32(bytes, 0) != 0x46464952u ||
        u32(bytes, 8) != 0x45564157u)
        throw std::runtime_error("Invalid portable effect RIFF/WAVE header");
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    std::size_t pcmOffset = 0, pcmBytes = 0;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const std::uint32_t id = u32(bytes, offset);
        const std::size_t size = u32(bytes, offset + 4);
        const std::size_t payload = offset + 8;
        if (size > bytes.size() - payload)
            throw std::runtime_error("Truncated portable effect WAV chunk");
        if (id == 0x20746d66u) {
            if (size < 16) throw std::runtime_error("Invalid portable effect WAV format");
            format = u16(bytes, payload);
            channels = u16(bytes, payload + 2);
            rate = u32(bytes, payload + 4);
            bits = u16(bytes, payload + 14);
        } else if (id == 0x61746164u) {
            pcmOffset = payload;
            pcmBytes = size;
        }
        offset = payload + size + (size & 1u);
    }
    if (format != 1 || (channels != 1 && channels != 2) || bits != 16 ||
        rate < 8000 || rate > 192000 || pcmBytes == 0 ||
        pcmBytes % (channels * 2u) != 0)
        throw std::runtime_error("Unsupported portable effect PCM WAV format");
    const std::size_t sourceFrames = pcmBytes / (channels * 2u);
    const std::size_t sourceStart = std::min(sourceFrames,
        static_cast<std::size_t>(std::llround(double(cue.sourceStartMs) * rate / 1000.0)));
    const std::size_t sourceEnd = cue.sourceEndMs < 0 ? sourceFrames :
        std::min(sourceFrames,
            static_cast<std::size_t>(std::llround(double(cue.sourceEndMs) * rate / 1000.0)));
    if (sourceEnd <= sourceStart) throw std::runtime_error("Empty portable effect WAV range");
    const std::size_t outputFrames = static_cast<std::size_t>(std::llround(
        double(sourceEnd - sourceStart) * engine::audio::sampleRate / rate));
    if (outputFrames == 0 || outputFrames > 8u * 1024u * 1024u)
        throw std::runtime_error("Invalid portable effect WAV duration");
    const auto source = [&](std::size_t frame, std::size_t channel) {
        const std::size_t offset = pcmOffset + (frame * channels + channel) * 2u;
        return static_cast<float>(static_cast<std::int16_t>(u16(bytes, offset))) / 32768.0f;
    };
    std::vector<float> samples;
    samples.reserve(outputFrames * channels);
    for (std::size_t frame = 0; frame < outputFrames; ++frame) {
        const double position = double(frame) * rate / engine::audio::sampleRate;
        const std::size_t first = std::min(sourceStart +
            static_cast<std::size_t>(position), sourceEnd - 1);
        const std::size_t second = std::min(first + 1, sourceEnd - 1);
        const float fraction = static_cast<float>(position - std::floor(position));
        for (std::size_t channel = 0; channel < channels; ++channel)
            samples.push_back(source(first, channel) * (1.0f - fraction) +
                              source(second, channel) * fraction);
    }
    std::shared_ptr<const engine::audio::AudioClip> clip;
    std::string error;
    if (!engine::audio::AudioClip::create(engine::audio::sampleRate,
            static_cast<std::uint8_t>(channels), samples, clip, &error))
        throw std::runtime_error("Could not prepare portable effect audio: " + error);
    return clip;
}

} // namespace

std::vector<AudioCue> loadAudio(const std::filesystem::path& manifest) {
    const auto effect = vfxlibrary::load(manifest);
    std::vector<AudioCue> cues;
    cues.reserve(effect.audio.size());
    for (const auto& cue : effect.audio)
        cues.push_back({cue.startMs, decodeWav(cue)});
    return cues;
}

} // namespace vfx::portable
