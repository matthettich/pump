// pump DSP: a small WAV reader (impulse responses). PCM 8/16/24/32-bit, float 32/64, plain or extensible headers.
// Header-only, framework-free; allocates (never on the audio thread).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pump {

struct WavData {
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels;   // [channel][sample]
    std::string error;                          // empty when it loaded
    bool ok() const { return error.empty() && !channels.empty() && !channels[0].empty(); }
};

inline WavData readWav(const std::string& path, size_t maxFrames = (size_t)1 << 22)
{
    WavData w;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { w.error = "can't open the file"; return w; }
    std::vector<uint8_t> d;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len > 0 && len < (1L << 30)) { d.resize((size_t)len); if (std::fread(d.data(), 1, d.size(), f) != d.size()) d.clear(); }
    std::fclose(f);
    auto u16 = [&](size_t o) { return (uint32_t)d[o] | ((uint32_t)d[o + 1] << 8); };
    auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) { w.error = "not a WAV file"; return w; }
    uint32_t fmt = 0, ch = 0, bits = 0;
    size_t pos = 12, dataAt = 0, dataLen = 0;
    while (pos + 8 <= d.size())
    {
        const uint32_t sz = u32(pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(d.data() + pos, "fmt ", 4) == 0 && body + 16 <= d.size())
        {
            fmt = u16(body); ch = u16(body + 2); w.sampleRate = u32(body + 4); bits = u16(body + 14);
            if (fmt == 0xFFFE && body + 26 <= d.size()) fmt = u16(body + 24);   // extensible: the sub-format
        }
        else if (std::memcmp(d.data() + pos, "data", 4) == 0) { dataAt = body; dataLen = std::min<size_t>(sz, d.size() - body); }
        pos = body + sz + (sz & 1);
    }
    if (!dataAt || ch == 0 || w.sampleRate <= 0) { w.error = "no audio in the file"; return w; }
    if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && (bits == 32 || bits == 64))))
    { w.error = "unsupported WAV format"; return w; }
    const size_t bps = bits / 8, frames = std::min(maxFrames, dataLen / (bps * ch));
    w.channels.assign(ch, std::vector<float>(frames));
    for (size_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < ch; ++c)
        {
            const uint8_t* p = d.data() + dataAt + (i * ch + c) * bps;
            float v = 0.0f;
            if (fmt == 3 && bits == 32) { std::memcpy(&v, p, 4); }
            else if (fmt == 3) { double x; std::memcpy(&x, p, 8); v = (float)x; }
            else if (bits == 8) v = ((int)p[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t)(p[0] | (p[1] << 8)) / 32768.0f;
            else if (bits == 24) { int32_t x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24); v = (float)(x / 2147483648.0); }
            else { int32_t x; std::memcpy(&x, p, 4); v = (float)(x / 2147483648.0); }
            w.channels[c][i] = std::isfinite(v) ? v : 0.0f;
        }
    return w;
}

} // namespace pump
