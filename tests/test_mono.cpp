// pump MONO: a note sounds at its pitch; LOW / HIGH / LAST priority pick the right note from a held chord and fall
// back when notes are released; every filter model self-oscillates near its cutoff at full resonance (no
// oscillators); a saw at C7 through the open ladder stays clean (oversampled, band-limited); random settings and
// MIDI stay finite. 44.1 and 48 kHz. CPU.
#include "../plugins/mono/MonoEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::mono;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings one()   // one saw at 8', open filter, instant envelopes
{
    Settings s; s.oct[0] = 2; s.level[1] = 0.0; s.level[2] = 0.0; s.cutoff = 20000; s.res = 0; s.filterEnv = 0; s.keyTrack = 0; s.drift = 0;
    s.aA = 0.001; s.aS = 1.0; s.aR = 0.05; s.velAmp = 0; s.wheelVib = 0;
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
            Engine e; Settings s = one(); s.wave[0] = kTri; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 57, 100 } });
            const double f = freqOf(L, (size_t)n / 4, (size_t)n, sr);
            CHECK(std::fabs(f - 220.0) < 0.1, "A3 (triangle, 8') plays at %.2f Hz (latency %d)", f, e.latency());
        }
        // ---- note priority
        for (int pr = 0; pr < 3; ++pr)
        {
            Engine e; Settings s = one(); s.wave[0] = kTri; s.priority = pr; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 57, 100 }, { (uint32_t)(n / 4), 0x90, 69, 100 }, { (uint32_t)(n / 2), 0x90, 64, 100 }, { (uint32_t)(3 * n / 4), 0x80, 64, 0 } });
            const double a = freqOf(L, (size_t)(n / 4 + 2000), (size_t)(n / 2), sr), b = freqOf(L, (size_t)(n / 2 + 2000), (size_t)(3 * n / 4), sr), c = freqOf(L, (size_t)(3 * n / 4 + 2000), (size_t)n, sr);
            static const double want[3][3] = { { 440.0, 329.6, 440.0 }, { 220.0, 220.0, 220.0 }, { 440.0, 440.0, 440.0 } };
            static const char* nm[3] = { "last", "low", "high" };
            CHECK(std::fabs(a - want[pr][0]) < 1 && std::fabs(b - want[pr][1]) < 1 && std::fabs(c - want[pr][2]) < 1, "%s priority: %.1f, %.1f, %.1f Hz", nm[pr], a, b, c);
        }
        // ---- self-oscillation of every model but the SVF (which, like the filter it recalls, only rings)
        for (int t = 0; t < kNumFilters; ++t)
        {
            if (t == kSvf) continue;
            Engine e; Settings s = one(); s.level[0] = 0; s.res = 1.0; s.cutoff = 1000; s.filterType = t; s.noise = 0.002; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 60, 100 } });
            double pk = 0; for (int i = n / 2; i < n; ++i) pk = std::max(pk, (double)std::fabs(L[(size_t)i]));
            const double f = freqOf(L, (size_t)n / 2, (size_t)n, sr);
            bool finite = true; for (float x : L) finite = finite && std::isfinite(x);
            CHECK(finite && pk > 0.05 && pk < 4.0 && f > 500 && f < 2000, "%s at full resonance: rings at %.0f Hz (cutoff 1 kHz), peak %.2f", kFilterNames[t], f, pk);
        }
        // ---- a clean saw at C7
        {
            Engine e; Settings s = one(); s.overdriveDb = 0; e.setSettings(s); e.prepare(sr, 512);
            const int N = 16384;
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
            CHECK(r < -55.0, "saw at C7 through the open ladder: non-harmonic content %.1f dB", r);
        }
        if (sr != 48000.0) continue;
        // ---- stress
        {
            std::mt19937 rng(43); std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
            bool finite = true; double peak = 0;
            std::vector<float> L(512), R(512);
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 11 == 0)
                {
                    for (int o = 0; o < 3; ++o) { s.wave[o] = (int)(u(rng) * 4); s.oct[o] = (int)(u(rng) * 5); s.pitch[o] = u(rng) * 24 - 12; s.level[o] = u(rng); }
                    s.sync = u(rng) < 0.5; s.fm = u(rng); s.pw = 0.05 + 0.45 * u(rng); s.noise = u(rng); s.overdriveDb = u(rng) * 24;
                    s.filterType = (int)(u(rng) * 4); s.cutoff = 20 * std::pow(1000.0, u(rng)); s.res = u(rng); s.filterEnv = u(rng) * 2 - 1; s.keyTrack = u(rng); s.mode = u(rng);
                    s.lfoShape = (int)(u(rng) * 4); s.lfoRate = 0.05 + 40 * u(rng); s.lfoPitch = u(rng); s.lfoFilter = u(rng); s.lfoPw = u(rng);
                    s.glide = u(rng) < 0.5 ? 0 : 500 * u(rng); s.glideMode = u(rng) < 0.5; s.priority = (int)(u(rng) * 3); s.legato = u(rng) < 0.5; s.drift = u(rng);
                    e.setSettings(s);
                }
                std::vector<synth::Midi> ev;
                if (u(rng) < 0.3) ev.push_back({ (uint32_t)(u(rng) * 511), (uint8_t)(u(rng) < 0.6 ? 0x90 : 0x80), (uint8_t)(24 + u(rng) * 84), (uint8_t)(1 + u(rng) * 126) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                e.process(L.data(), R.data(), 512, ev.data(), (int)ev.size());
                for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            CHECK(finite && peak < 10.0, "random settings and MIDI: finite (peak %.2f)", peak);
        }
        {
            Engine e; Settings s = one(); s.level[1] = 0.7; s.level[2] = 0.6; s.res = 0.6; s.cutoff = 900; s.overdriveDb = 10; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            const auto t0 = std::chrono::steady_clock::now();
            render(e, L, { { 0, 0x90, 45, 100 } });
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: three oscillators, ladder, overdrive: %.1f %% of one core\n", 100.0 * secs);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
