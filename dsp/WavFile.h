#pragma once

#include <string>
#include <vector>

// Offline only -- this allocates and does file I/O, so it must never appear on
// an audio callback path. It exists so the DSP can be exercised against real
// material from a console before it goes anywhere near a live audio graph.

namespace dreamdsp::dsp {

struct WavData {
    std::vector<std::vector<float>> channels;   // deinterleaved
    int sampleRate = 0;

    int channelCount() const { return int(channels.size()); }
    int frames() const { return channels.empty() ? 0 : int(channels[0].size()); }
};

// Accepts PCM 16/24/32 and IEEE float 32. Returns false with `error` set.
bool readWav(const std::string &path, WavData *out, std::string *error = nullptr);

// Writes IEEE float 32, which round-trips the DSP output without a
// quantisation step confusing a measurement.
bool writeWav(const std::string &path, const WavData &data, std::string *error = nullptr);

} // namespace dreamdsp::dsp
