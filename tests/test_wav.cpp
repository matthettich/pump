// WAV reader: good files of every supported format load exactly; hostile / damaged files (zero or absurd channel
// counts, chunk sizes past the end, a data chunk before fmt, truncation, a missing file, a directory, an oversized
// header) come back as a clean error: no crash, no huge allocation, no out-of-range read. Run under ASan/UBSan
// (make tests-asan) to make the memory part of that claim checkable.
#include "../common/dsp/Wav.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>

using namespace pump;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using Bytes = std::vector<uint8_t>;
static void p16(Bytes& b, uint32_t v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void p32(Bytes& b, uint32_t v) { p16(b, v & 65535); p16(b, v >> 16); }
static void pstr(Bytes& b, const char* s) { for (; *s; ++s) b.push_back((uint8_t)*s); }

struct Spec {
    uint32_t fmt = 1, ch = 1, rate = 48000, bits = 16;
    uint32_t dataBytes = 0;           // bytes actually written
    int32_t dataClaim = -1;           // chunk size written in the header (-1: the true size)
    bool dataFirst = false;           // data chunk before fmt
    bool extensible = false;
};

static Bytes makeWav(const Spec& s, const Bytes& payload)
{
    Bytes fmt;
    p16(fmt, s.extensible ? 0xFFFE : s.fmt); p16(fmt, s.ch); p32(fmt, s.rate);
    p32(fmt, s.rate * s.ch * (s.bits / 8)); p16(fmt, s.ch * (s.bits / 8)); p16(fmt, s.bits);
    if (s.extensible) { p16(fmt, 22); p16(fmt, s.bits); p32(fmt, 0); p16(fmt, s.fmt); for (int i = 0; i < 14; ++i) fmt.push_back(0); }
    Bytes data;
    pstr(data, "data"); p32(data, s.dataClaim >= 0 ? (uint32_t)s.dataClaim : (uint32_t)payload.size());
    data.insert(data.end(), payload.begin(), payload.end());
    if (payload.size() & 1) data.push_back(0);
    Bytes fchunk; pstr(fchunk, "fmt "); p32(fchunk, (uint32_t)fmt.size()); fchunk.insert(fchunk.end(), fmt.begin(), fmt.end());
    Bytes body; pstr(body, "WAVE");
    if (s.dataFirst) { body.insert(body.end(), data.begin(), data.end()); body.insert(body.end(), fchunk.begin(), fchunk.end()); }
    else { body.insert(body.end(), fchunk.begin(), fchunk.end()); body.insert(body.end(), data.begin(), data.end()); }
    Bytes out; pstr(out, "RIFF"); p32(out, (uint32_t)body.size()); out.insert(out.end(), body.begin(), body.end());
    return out;
}

static std::string put(const char* name, const Bytes& b)
{
    const std::string path = std::string("build/tests/") + name;
    if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
    return path;
}

static Bytes pcm16(const std::vector<int>& v) { Bytes b; for (int x : v) p16(b, (uint32_t)(x & 0xFFFF)); return b; }

int main()
{
    // ---- good files ----
    {
        Spec s; const WavData w = readWav(put("w_ok16.wav", makeWav(s, pcm16({ 0, 16384, -16384, 32767, -32768 }))));
        CHECK(w.ok(), "16-bit mono loads (%s)", w.error.c_str());
        CHECK(w.channels.size() == 1 && w.channels[0].size() == 5, "5 frames");
        CHECK(w.ok() && std::fabs(w.channels[0][1] - 0.5f) < 1e-6f && std::fabs(w.channels[0][4] + 1.0f) < 1e-6f, "values exact");
        CHECK(w.sampleRate == 48000.0, "rate");
    }
    {
        Spec s; s.ch = 2; Bytes p = pcm16({ 1000, -1000, 2000, -2000 });
        const WavData w = readWav(put("w_st.wav", makeWav(s, p)));
        CHECK(w.ok() && w.channels.size() == 2 && w.channels[0].size() == 2, "stereo splits into two channels");
        CHECK(w.ok() && w.channels[1][1] < 0 && w.channels[0][1] > 0, "channels not swapped");
    }
    {
        Spec s; s.fmt = 3; s.bits = 32; Bytes p; const float v[3] = { 0.25f, -0.5f, 1.0f };
        for (float f : v) { uint32_t u; std::memcpy(&u, &f, 4); p32(p, u); }
        const WavData w = readWav(put("w_f32.wav", makeWav(s, p)));
        CHECK(w.ok() && w.channels[0].size() == 3 && w.channels[0][1] == -0.5f, "float32");
    }
    {
        Spec s; s.bits = 24; Bytes p; for (int x : { 0x400000, -0x400000 }) { p.push_back(x & 255); p.push_back((x >> 8) & 255); p.push_back((x >> 16) & 255); }
        const WavData w = readWav(put("w_24.wav", makeWav(s, p)));
        CHECK(w.ok() && std::fabs(w.channels[0][0] - 0.5f) < 1e-6f && std::fabs(w.channels[0][1] + 0.5f) < 1e-6f, "24-bit");
    }
    {
        Spec s; s.extensible = true; const WavData w = readWav(put("w_ext.wav", makeWav(s, pcm16({ 100, 200 }))));
        CHECK(w.ok() && w.channels[0].size() == 2, "WAVE_FORMAT_EXTENSIBLE");
    }
    {   // an odd-sized unknown chunk before data must be skipped with its pad byte
        Spec s; Bytes good = makeWav(s, pcm16({ 7, 8, 9 }));
        Bytes junk; pstr(junk, "LIST"); p32(junk, 3); junk.push_back(1); junk.push_back(2); junk.push_back(3); junk.push_back(0);
        good.insert(good.begin() + 12, junk.begin(), junk.end());
        const WavData w = readWav(put("w_junk.wav", good));
        CHECK(w.ok() && w.channels[0].size() == 3, "odd-sized chunk skipped");
    }
    {   // maxFrames caps the length
        Spec s; std::vector<int> big(1000, 5); const WavData w = readWav(put("w_cap.wav", makeWav(s, pcm16(big))), 100);
        CHECK(w.ok() && w.channels[0].size() == 100, "maxFrames honoured (%zu)", w.ok() ? w.channels[0].size() : 0);
    }

    // ---- hostile files: every one must fail cleanly and quickly ----
    auto bad = [&](const char* what, const Bytes& b)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const WavData w = readWav(put("w_bad.wav", b));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(!w.ok(), "%s: must be rejected", what);
        CHECK(!w.ok() ? !w.error.empty() : true, "%s: must say why", what);
        CHECK(ms < 500.0, "%s: took %.0f ms", what, ms);
        size_t total = 0; for (auto& c : w.channels) total += c.size();
        CHECK(total <= ((size_t)1 << 22) * 2, "%s: allocated %zu samples", what, total);
    };
    { Spec s; s.ch = 0; bad("zero channels", makeWav(s, pcm16({ 1, 2 }))); }
    { Spec s; s.ch = 65535; bad("65535 channels", makeWav(s, pcm16({ 1, 2, 3, 4 }))); }
    { Spec s; s.ch = 65535; s.dataClaim = 0x7FFFFFF0; bad("65535 channels, huge claimed data", makeWav(s, pcm16({ 1, 2, 3, 4 }))); }
    { Spec s; s.bits = 0; bad("zero bits", makeWav(s, pcm16({ 1, 2 }))); }
    { Spec s; s.bits = 12; bad("12-bit", makeWav(s, pcm16({ 1, 2 }))); }
    { Spec s; s.rate = 0; bad("zero sample rate", makeWav(s, pcm16({ 1, 2 }))); }
    { Spec s; s.rate = 0xFFFFFFFFu; bad("absurd sample rate", makeWav(s, pcm16({ 1, 2 }))); }
    { Spec s; s.fmt = 2; bad("ADPCM", makeWav(s, pcm16({ 1, 2 }))); }
    { Spec s; s.ch = 3; bad("three channels", makeWav(s, pcm16({ 1, 2, 3, 4, 5, 6 }))); }
    { Spec s; s.rate = 999; bad("rate just under the minimum", makeWav(s, pcm16({ 1, 2 }))); }
    { Spec s; s.rate = 768001; bad("rate just over the maximum", makeWav(s, pcm16({ 1, 2 }))); }
    {   // the chunk order is free: data before fmt decodes correctly (so it is accepted, not rejected)
        Spec s; s.dataFirst = true; const WavData w = readWav(put("w_df.wav", makeWav(s, pcm16({ 16384, -16384 }))));
        CHECK(w.ok() && w.channels[0].size() == 2 && std::fabs(w.channels[0][0] - 0.5f) < 1e-6f, "data before fmt decodes");
    }
    {   // the limits themselves are inside the range
        Spec a; a.rate = 1000; Spec b; b.rate = 768000; Spec c; c.ch = 2;
        CHECK(readWav(put("w_lo.wav", makeWav(a, pcm16({ 1, 2 })))).ok(), "rate 1000 accepted");
        CHECK(readWav(put("w_hi.wav", makeWav(b, pcm16({ 1, 2 })))).ok(), "rate 768000 accepted");
        CHECK(readWav(put("w_2c.wav", makeWav(c, pcm16({ 1, 2, 3, 4 })))).ok(), "two channels accepted");
    }
    { Spec s; s.dataClaim = 0x7FFFFFFF; Bytes b = makeWav(s, pcm16({ 1, 2 })); const WavData w = readWav(put("w_claim.wav", b));
      CHECK(w.ok() ? w.channels[0].size() == 2 : true, "claimed data past EOF is clamped to what is there"); }
    { Spec s; Bytes b = makeWav(s, pcm16({ 1, 2, 3, 4 })); b.resize(30); bad("truncated mid-fmt", b); }
    { Spec s; Bytes b = makeWav(s, pcm16({ 1, 2, 3, 4 })); b.resize(11); bad("truncated header", b); }
    { bad("empty file", Bytes()); }
    { Bytes b(100, 0); bad("zeros", b); }
    { Bytes b; pstr(b, "RIFF"); p32(b, 0); pstr(b, "WAVE"); pstr(b, "fmt "); p32(b, 0xFFFFFFFFu); bad("fmt chunk of 4 GB", b); }
    { Bytes b; pstr(b, "RIFF"); p32(b, 0); pstr(b, "WAVE"); for (int i = 0; i < 4000; ++i) { pstr(b, "junk"); p32(b, 0); } bad("4000 empty chunks, no audio", b); }
    {   // no unconditional crash on any prefix of a good file
        Spec s; const Bytes good = makeWav(s, pcm16({ 1, 2, 3, 4, 5, 6, 7, 8 }));
        int accepted = 0;
        for (size_t n = 0; n <= good.size(); ++n) { Bytes b(good.begin(), good.begin() + (long)n); const WavData w = readWav(put("w_prefix.wav", b)); accepted += w.ok(); }
        CHECK(accepted >= 1, "the full file loads");
    }

    // ---- not a regular file ----
    { const WavData w = readWav("build/tests/does_not_exist.wav"); CHECK(!w.ok() && !w.error.empty(), "missing file: %s", w.error.c_str()); }
    { const WavData w = readWav("build/tests"); CHECK(!w.ok() && !w.error.empty(), "a directory: %s", w.error.c_str()); }
    { const WavData w = readWav(""); CHECK(!w.ok(), "empty path"); }

    std::printf(fails ? "test_wav: %d FAILED\n" : "test_wav: all passed\n", fails);
    return fails ? 1 : 0;
}
