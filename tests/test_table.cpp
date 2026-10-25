// pump TABLE: a note sounds at the right pitch and stops after its release; a high saw has no aliasing (the
// mips); the analog table at position 0 is a sine; voices are stolen without exceeding the polyphony; legato
// glides; a user wavetable loads from a WAV; random settings and MIDI stay finite. 44.1 and 48 kHz. CPU.
#include "../plugins/table/TableEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::table;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings plain()
{
    Settings s; s.osc[0].table = kAnalog; s.osc[0].pos = 0.5; s.osc[0].voices = 1; s.osc[0].level = 1.0; s.osc[1].level = 0.0;
    s.cutoff = 20000; s.res = 0; s.filterEnv = 0; s.keyTrack = 0; s.filterType = kLp24; s.ampA = 0.001; s.ampS = 1.0; s.ampR = 0.05; s.velAmp = 0;
    for (auto& m : s.mod) m.amt = 0;
    return s;
}
static void render(Engine& e, std::vector<float>& L, std::vector<float>& R, std::vector<synth::Midi> ev = {}, int at = 0)
{
    const int n = (int)L.size();
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset + at >= off && (int)x.offset + at < off + m) { x.offset = x.offset + at - off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (double)(b - a)); }
static double freqOf(const std::vector<float>& x, size_t a, size_t b, double sr)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * sr / (last - first) : 0.0;
}

int main()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)sr;
        // ---- a sine (analog table at position 0): pitch and release
        {
            Settings s = plain(); s.osc[0].pos = 0.0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n), R((size_t)n);
            render(e, L, R, { { 0, 0x90, 69, 100 }, { (uint32_t)(n / 2), 0x80, 69, 0 } });
            const double f = freqOf(L, (size_t)n / 8, (size_t)n / 2 - 100, sr);
            CHECK(std::fabs(f - 440.0) < 0.2, "A4 plays at %.2f Hz", f);
            const double tail = rms(L, (size_t)(n / 2 + 0.2 * sr), (size_t)n);
            CHECK(tail < 1e-6 && e.voicesNow() == 0, "after the release: silent (%.2g), no voices left (%d)", tail, e.voicesNow());
        }
        // ---- no aliasing: a saw at C8
        {
            Settings s = plain(); s.osc[0].pos = 0.5;   // the saw frame of the analog table
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int N = 16384;
            std::vector<float> L((size_t)(N + 8192)), R = L;
            render(e, L, R, { { 0, 0x90, 108, 100 } });
            RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
            // (a 4-term Blackman-Harris window: its side lobes are below -92 dB, so leakage doesn't read as aliasing)
            for (int i = 0; i < N; ++i)
            {
                const double t = kTwoPi * i / N;
                in[(size_t)i] = (float)(L[(size_t)(8192 + i)] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t)));
            }
            fft.forward(in.data(), out.data());
            const double f0 = synth::midiToHz(108);
            double harm = 0, other = 0;
            for (int k = 4; k < N / 2; ++k)
            {
                const double p = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
                const double f = k * sr / N, h = f / f0;
                if (std::fabs(h - std::round(h)) * f0 < 6.0 * sr / N && std::round(h) >= 1) harm += p; else other += p;
            }
            const double ratio = 10 * std::log10(other / harm);
            CHECK(ratio < -60.0, "saw at C8 (%.0f Hz): everything that isn't a harmonic is %.1f dB down (no aliasing)", f0, ratio);
        }
        if (sr != 48000.0) continue;
        // ---- polyphony and stealing
        {
            Settings s = plain(); s.polyphony = 8; s.ampR = 2.0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<synth::Midi> ev;
            for (int k = 0; k < 20; ++k) ev.push_back({ (uint32_t)(k * 600), 0x90, (uint8_t)(48 + k), 100 });
            std::vector<float> L((size_t)n), R((size_t)n);
            render(e, L, R, ev);
            bool finite = true; for (float x : L) finite = finite && std::isfinite(x);
            CHECK(finite && e.voicesNow() == 8, "20 held notes on 8 voices: %d sounding", e.voicesNow());
        }
        // ---- legato with glide: the pitch moves from one note to the next
        {
            Settings s = plain(); s.osc[0].pos = 0.0; s.voiceMode = synth::kLegato; s.glide = 200;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n), R((size_t)n);
            render(e, L, R, { { 0, 0x90, 57, 100 }, { (uint32_t)(n / 4), 0x90, 69, 100 } });
            const double f1 = freqOf(L, (size_t)n / 8, (size_t)n / 4, sr), fmid = freqOf(L, (size_t)(n / 4 + 0.02 * sr), (size_t)(n / 4 + 0.06 * sr), sr), f2 = freqOf(L, (size_t)(3 * n / 4), (size_t)n, sr);
            CHECK(std::fabs(f1 - 220) < 0.5 && fmid > 230 && fmid < 430 && std::fabs(f2 - 440) < 0.5, "legato glide: %.1f Hz -> %.1f (on the way) -> %.1f Hz", f1, fmid, f2);
        }
        // ---- a user table from a WAV (two frames: sine, square)
        {
            std::vector<float> x(2 * kFrameSize);
            for (int i = 0; i < kFrameSize; ++i) { x[(size_t)i] = (float)std::sin(kTwoPi * i / kFrameSize); x[(size_t)(kFrameSize + i)] = i < kFrameSize / 2 ? 0.8f : -0.8f; }
            FILE* f = std::fopen("build/tests/wt_test.wav", "wb");
            auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
            std::fwrite("RIFF", 1, 4, f); w32(36 + 4 * (uint32_t)x.size()); std::fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(3); w16(1); w32(48000); w32(192000); w16(4); w16(32);
            std::fwrite("data", 1, 4, f); w32(4 * (uint32_t)x.size()); std::fwrite(x.data(), 4, x.size(), f); std::fclose(f);
            std::string err;
            auto t = loadUserTable("build/tests/wt_test.wav", err);
            CHECK(t && t->frames == 2 && std::fabs(t->read(0.25, 0.0, 0) - 1.0f) < 0.01f && t->read(0.25, 1.0, 0) > 0.7f && t->read(0.1, 1.0, 0) > 0.7f, "user wavetable: 2 frames, sine then square (%s)", err.c_str());
            Settings s = plain(); s.osc[0].table = kUserTable; s.osc[0].pos = 1.0;
            Engine e; e.setSettings(s); e.prepare(sr, 512); e.setUserTable(0, t.get());
            std::vector<float> L(8192), R(8192);
            render(e, L, R, { { 0, 0x90, 60, 100 } });
            CHECK(rms(L, 4096, 8192) > 0.05, "plays the user table (RMS %.3f)", rms(L, 4096, 8192));
        }
        // ---- stress: random settings and MIDI
        {
            std::mt19937 rng(41); std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
            bool finite = true; double peak = 0;
            std::vector<float> L(512), R(512);
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 11 == 0)
                {
                    for (auto& o : s.osc) { o.table = (int)(u(rng) * kNumFactory); o.pos = u(rng); o.voices = 1 + (int)(u(rng) * 7); o.detune = u(rng); o.spread = u(rng); o.oct = (int)(u(rng) * 7) - 3; o.level = u(rng); o.pan = u(rng) * 2 - 1; }
                    s.sub = u(rng); s.noise = u(rng) * 0.3; s.filterType = (int)(u(rng) * 4); s.cutoff = 20 * std::pow(1000.0, u(rng)); s.res = u(rng); s.driveDb = u(rng) * 24;
                    s.filterEnv = u(rng) * 2 - 1; s.keyTrack = u(rng); s.lfo1Shape = (int)(u(rng) * kNumShapes); s.lfo2Shape = (int)(u(rng) * kNumShapes); s.lfo1Rate = 0.1 + u(rng) * 30;
                    for (auto& m : s.mod) { m.src = (int)(u(rng) * kNumSources); m.dst = (int)(u(rng) * kNumDests); m.amt = u(rng) * 2 - 1; }
                    s.voiceMode = (int)(u(rng) * 3); s.polyphony = 1 + (int)(u(rng) * 16); s.glide = u(rng) < 0.5 ? 0 : u(rng) * 500;
                    e.setSettings(s);
                }
                std::vector<synth::Midi> ev;
                if (u(rng) < 0.3) ev.push_back({ (uint32_t)(u(rng) * 511), (uint8_t)(u(rng) < 0.6 ? 0x90 : 0x80), (uint8_t)(24 + u(rng) * 84), (uint8_t)(1 + u(rng) * 126) });
                if (u(rng) < 0.05) ev.push_back({ 511, 0xE0, 0, (uint8_t)(u(rng) * 127) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                e.process(L.data(), R.data(), 512, ev.data(), (int)ev.size());
                for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            CHECK(finite && peak < 20.0, "random settings and MIDI: finite (peak %.2f)", peak);
        }
        // ---- CPU: 8 voices, 7 + 7 unison, ladder 24
        {
            Settings s = plain(); s.osc[0].voices = 7; s.osc[1].voices = 7; s.osc[1].level = 0.8; s.polyphony = 8; s.cutoff = 3000; s.res = 0.5;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<synth::Midi> ev; for (int k = 0; k < 8; ++k) ev.push_back({ 0, 0x90, (uint8_t)(48 + 3 * k), 100 });
            std::vector<float> L((size_t)n), R((size_t)n);
            const auto t0 = std::chrono::steady_clock::now();
            render(e, L, R, ev);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: 8 notes x 14 unison oscillators: %.1f %% of one core\n", 100.0 * secs);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
