#include "WavFile.h"

#include <cstdint>
#include <cstring>
#include <fstream>

namespace dreamdsp::dsp {

namespace {

uint32_t rd32(const unsigned char *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint16_t rd16(const unsigned char *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }

void wr32(std::ostream &o, uint32_t v) { char b[4] = { char(v), char(v >> 8), char(v >> 16), char(v >> 24) }; o.write(b, 4); }
void wr16(std::ostream &o, uint16_t v) { char b[2] = { char(v), char(v >> 8) }; o.write(b, 2); }

bool fail(std::string *error, const char *msg) { if (error) *error = msg; return false; }

} // namespace

bool readWav(const std::string &path, WavData *out, std::string *error)
{
    if (!out)
        return fail(error, "null output");

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return fail(error, "cannot open file");

    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (buf.size() < 44)
        return fail(error, "file too small");
    if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
        return fail(error, "not a RIFF/WAVE file");

    int format = 0, channels = 0, bits = 0;
    size_t dataOffset = 0, dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const unsigned char *id = buf.data() + pos;
        const uint32_t size = rd32(buf.data() + pos + 4);

        if (std::memcmp(id, "fmt ", 4) == 0 && pos + 8 + 16 <= buf.size()) {
            const unsigned char *fmt = buf.data() + pos + 8;
            format = rd16(fmt);
            channels = rd16(fmt + 2);
            out->sampleRate = int(rd32(fmt + 4));
            bits = rd16(fmt + 14);
            if (format == 0xFFFE && size >= 40)   // WAVE_FORMAT_EXTENSIBLE
                format = rd16(fmt + 24);          // first two bytes of SubFormat
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataOffset = pos + 8;
            dataSize = size;
            break;
        }

        pos += 8 + size + (size & 1);
        if (size == 0)
            break;
    }

    if (channels <= 0 || bits <= 0 || dataOffset == 0)
        return fail(error, "missing fmt or data chunk");
    if (dataOffset + dataSize > buf.size())
        dataSize = buf.size() - dataOffset;

    const int bytes = bits / 8;
    const size_t frames = dataSize / size_t(bytes * channels);

    out->channels.assign(size_t(channels), std::vector<float>(frames, 0.0f));

    const unsigned char *p = buf.data() + dataOffset;
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            const unsigned char *s = p + (i * size_t(channels) + size_t(c)) * size_t(bytes);
            float v = 0.0f;
            if (format == 3 && bits == 32) {
                std::memcpy(&v, s, 4);
            } else if (bits == 16) {
                v = float(int16_t(rd16(s))) / 32768.0f;
            } else if (bits == 24) {
                const int32_t raw = (int32_t(s[2]) << 24 | int32_t(s[1]) << 16 | int32_t(s[0]) << 8) >> 8;
                v = float(raw) / 8388608.0f;
            } else if (bits == 32) {
                v = float(int32_t(rd32(s))) / 2147483648.0f;
            } else {
                return fail(error, "unsupported bit depth");
            }
            out->channels[size_t(c)][i] = v;
        }
    }
    return true;
}

bool writeWav(const std::string &path, const WavData &data, std::string *error)
{
    const int channels = data.channelCount();
    const int frames = data.frames();
    if (channels <= 0 || frames <= 0)
        return fail(error, "nothing to write");

    std::ofstream f(path, std::ios::binary);
    if (!f)
        return fail(error, "cannot create file");

    const uint32_t dataBytes = uint32_t(frames) * uint32_t(channels) * 4u;

    f.write("RIFF", 4);  wr32(f, 36u + dataBytes);  f.write("WAVE", 4);
    f.write("fmt ", 4);  wr32(f, 16u);
    wr16(f, 3);                                        // IEEE float
    wr16(f, uint16_t(channels));
    wr32(f, uint32_t(data.sampleRate));
    wr32(f, uint32_t(data.sampleRate) * uint32_t(channels) * 4u);
    wr16(f, uint16_t(channels * 4));
    wr16(f, 32);
    f.write("data", 4);  wr32(f, dataBytes);

    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            const float v = data.channels[size_t(c)][size_t(i)];
            f.write(reinterpret_cast<const char *>(&v), 4);
        }
    }
    return bool(f);
}

} // namespace dreamdsp::dsp
