// pump POLY: a note at its pitch; a chord sounds on as many voices as notes; unison puts every voice on one note;
// poly-mod (filter envelope to A's frequency) sweeps the pitch; sync and the chorus stay finite; aliasing of a saw
// at C7 measured; random settings and MIDI stay finite. 44.1 and 48 kHz. CPU.
#include "../plugins/poly/PolyEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::poly;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings plain()
{
    Settings s; s.levelB = 0; s.cutoff = 20000; s.res = 0; s.filterEnv = 0; s.keyTrack = 0; s.slop = 0; s.aA = 0.001; s.aS = 1.0; s.aR = 0.05; s.velAmp = 0;
    return s;
}
static void render(Engine& e, std::vector<float>& L, std::vector<synth::Midi> ev)
{
    std::vector<float> R(L.size());
    const int n = (int)L.size();
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
}
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
        {
            Engine e; Settings s = plain(); s.aSaw = false; s.aPulse = true; s.aPw = 0.5; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 57, 100 } });
            const double f = freqOf(L, (size_t)n / 4, (size_t)n, sr);
            CHECK(std::fabs(f - 220.0) < 0.1, "A3 (square) plays at %.2f Hz", f);
        }
        {
            Engine e; Settings s = plain(); s.voices = 8; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(4096);
            render(e, L, { { 0, 0x90, 48, 100 }, { 0, 0x90, 52, 100 }, { 0, 0x90, 55, 100 }, { 0, 0x90, 60, 100 } });
            CHECK(e.voicesNow() == 4, "a 4-note chord: %d voices", e.voicesNow());
        }
        {
            Engine e; Settings s = plain(); s.voiceMode = kModeUnison; s.voices = 6; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(4096);
            render(e, L, { { 0, 0x90, 48, 100 } });
            CHECK(e.voicesNow() == 6, "unison 6 on one note: %d voices", e.voicesNow());
        }
        {
            // poly-mod: the filter envelope sweeps A's pitch up (decaying back)
            Engine e; Settings s = plain(); s.aSaw = false; s.aPulse = true; s.pmEnv = 0.4; s.pmFreqA = true; s.fA = 0.001; s.fD = 0.3; s.fS = 0.0; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 57, 100 } });
            const double early = freqOf(L, 200, (size_t)(0.02 * sr), sr), late = freqOf(L, (size_t)(0.8 * sr), (size_t)n, sr);
            CHECK(early > 350.0 && std::fabs(late - 220.0) < 1.0, "poly-mod env to A: %.0f Hz at the start, %.1f Hz once it has decayed", early, late);
        }
        {
            const int N = 16384;
            Engine e; Settings s = plain(); e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(N + 8192));
            render(e, L, { { 0, 0x90, 96, 100 } });
            RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
            for (int i = 0; i < N; ++i) { const double t = kTwoPi * i / N; in[(size_t)i] = (float)(L[(size_t)(8192 + i)] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t))); }
            fft.forward(in.data(), out.data());
            const double f0 = synth::midiToHz(96);
            double harm = 0, other = 0;
            for (int k = 4; k < N / 2; ++k)
            {
                const double p = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
                const double f = k * sr / N, h = f / f0;
                if (std::fabs(h - std::round(h)) * f0 < 6.0 * sr / N && std::round(h) >= 1) harm += p; else other += p;
            }
            const double r = 10 * std::log10(other / harm);
            CHECK(r < -60.0, "saw at C7 (band-limited tables, open filter): non-harmonic content %.1f dB", r);
        }
        if (sr != 48000.0) continue;
        {
            std::mt19937 rng(47); std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
            bool finite = true; double peak = 0;
            std::vector<float> L(512), R(512);
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 11 == 0)
                {
                    s.aSaw = u(rng) < 0.6; s.aPulse = u(rng) < 0.5; s.sync = u(rng) < 0.3; s.bSaw = u(rng) < 0.5; s.bTri = u(rng) < 0.5; s.bPulse = u(rng) < 0.5; s.bLow = u(rng) < 0.2;
                    s.aFreq = u(rng) * 48 - 24; s.bFreq = u(rng) * 48 - 24; s.aPw = 0.05 + 0.9 * u(rng); s.bPw = 0.05 + 0.9 * u(rng);
                    s.levelA = u(rng); s.levelB = u(rng); s.noise = u(rng); s.sub = u(rng);
                    s.filterType = (int)(u(rng) * 4); s.cutoff = 20 * std::pow(1000.0, u(rng)); s.res = u(rng); s.filterEnv = u(rng) * 2 - 1; s.hpf = 10 * std::pow(200.0, u(rng));
                    s.pmEnv = u(rng); s.pmOscB = u(rng); s.pmFreqA = u(rng) < 0.5; s.pmPwA = u(rng) < 0.5; s.pmFilter = u(rng) < 0.5;
                    s.lfoShape = (int)(u(rng) * 4); s.lfoRate = 0.05 + 30 * u(rng); s.lfoPitch = u(rng); s.lfoPw = u(rng); s.lfoFilter = u(rng); s.lfoDelay = 2000 * u(rng);
                    s.voiceMode = (int)(u(rng) * 3); s.voices = 1 + (int)(u(rng) * 16); s.unisonDetune = u(rng); s.glide = u(rng) < 0.5 ? 0 : 400 * u(rng); s.slop = u(rng); s.chorus = (int)(u(rng) * 4);
                    e.setSettings(s);
                }
                std::vector<synth::Midi> ev;
                if (u(rng) < 0.3) ev.push_back({ (uint32_t)(u(rng) * 511), (uint8_t)(u(rng) < 0.6 ? 0x90 : 0x80), (uint8_t)(24 + u(rng) * 84), (uint8_t)(1 + u(rng) * 126) });
                if (u(rng) < 0.05) ev.push_back({ 511, 0xB0, 1, (uint8_t)(u(rng) * 127) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                e.process(L.data(), R.data(), 512, ev.data(), (int)ev.size());
                for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            CHECK(finite && peak < 10.0, "random settings and MIDI: finite (peak %.2f)", peak);
        }
        {
            Engine e; Settings s = plain(); s.levelB = 0.7; s.voices = 16; s.res = 0.4; s.cutoff = 2000; s.chorus = kChorusI; s.pmOscB = 0.3; s.pmFilter = true; e.setSettings(s); e.prepare(sr, 512);
            std::vector<synth::Midi> ev; for (int k = 0; k < 16; ++k) ev.push_back({ 0, 0x90, (uint8_t)(36 + 3 * k), 100 });
            std::vector<float> L((size_t)n);
            const auto t0 = std::chrono::steady_clock::now();
            render(e, L, ev);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: 16 voices, 2 oscillators, audio-rate poly-mod to the filter, chorus: %.1f %% of one core\n", 100.0 * secs);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
