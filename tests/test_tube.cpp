// pump TUBE: mix 0 nulls against the latency-delayed input at every oversampling factor; a quiet signal through
// Glow at drive 0 nearly nulls (alignment, filters flat); auto gain holds the test mix's level across drive,
// stages and voltage; harmonic signatures (triode: 2nd, push-pull: odd only, cold push-pull: crossover, pentode:
// more odd); aliasing of a hard-driven 7 kHz sine falls with oversampling; sag makes a sustained loud note bloom
// down; starving raises distortion; circuit switches don't click; random-settings stress (block sizes, toggles
// undone mid-fade, absurd input) stays finite and bounded. 44.1, 48 and 96 kHz. Also prints CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/tube/TubeEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::tube;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    const int n = (int)L.size();
    for (int off = 0; off < n; off += block) e.process(L.data() + off, R.data() + off, std::min(block, n - off));
}
static Settings base(int mode, double drive)
{
    Settings s; s.mode = mode; s.drive = drive; s.sag = 0; s.bias = 0; s.voltage = 100; s.stages = 1; s.mix = 100; s.autoGain = false; s.oversampling = 2;
    return s;
}
static void sine(std::vector<float>& L, std::vector<float>& R, double sr, int n, double f, double amp)
{
    L.resize((size_t)n); R.resize((size_t)n);
    for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(amp * std::sin(kTwoPi * f * i / sr));
}
static double rmsDb(const std::vector<float>& L, const std::vector<float>& R, size_t from, size_t to = 0)
{
    if (to == 0 || to > L.size()) to = L.size();
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)L[i] * L[i] + (double)R[i] * R[i];
    return 10 * std::log10(e / (2.0 * (to - from)) + 1e-30);
}
// harmonic levels (dB relative to the fundamental) of a steady tone at f (bin-exact: f * N / sr is an integer)
struct Harm { double h[9]; double alias; };
static Harm harmonics(const std::vector<float>& x, double sr, double f, int N = 8192)
{
    RealFFT fft; fft.setup(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    const size_t start = x.size() - (size_t)N;
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(kTwoPi * i / N);
        in[(size_t)i] = (float)(x[start + (size_t)i] * w);
    }
    fft.forward(in.data(), out.data());
    auto pw = [&](int k) { const double re = out[(size_t)(2 * k)], im = out[(size_t)(2 * k + 1)]; return re * re + im * im; };
    auto band = [&](int k) { double p = 0; for (int d = -2; d <= 2; ++d) if (k + d > 0 && k + d < N / 2) p += pw(k + d); return p; };
    const int k1 = (int)std::lround(f * N / sr);
    Harm r{};
    const double p1 = band(k1);
    for (int h = 2; h <= 10; ++h)
    {
        const int k = k1 * h;
        r.h[h - 2] = k < N / 2 - 3 ? 10 * std::log10(band(k) / p1 + 1e-30) : -200.0;
    }
    // everything that isn't a harmonic (or DC) = aliasing + noise, relative to the fundamental
    double other = 0;
    for (int k = 8; k < N / 2; ++k)
    {
        const int near = (int)std::lround((double)k / k1);
        if (near >= 1 && std::abs(k - near * k1) <= 3) continue;
        other += pw(k);
    }
    r.alias = 10 * std::log10(other / p1 + 1e-30);
    return r;
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)(sr * 2.0);
        // ---- mix 0: the delayed input, exactly
        for (int lg = 0; lg <= 3; ++lg)
        {
            Settings s = base(kFuzz, 30); s.mix = 0; s.oversampling = lg;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(mix.begin(), mix.begin() + 2 * n), R;
            L.resize((size_t)n); R.resize((size_t)n);
            for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            const std::vector<float> L0 = L, R0 = R;
            run(e, L, R, 333);
            const int lat = e.latency();
            double err = 0;
            for (int i = lat; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)(i - lat)]) + std::fabs(R[(size_t)i] - R0[(size_t)(i - lat)]));
            CHECK(err < 1e-6, "mix 0 at %dx = input delayed by %d samples (max error %.2g)", 1 << lg, lat, err);
        }
        // ---- a quiet multitone through Glow at drive 0 is nearly the input (alignment and flat filters)
        for (int lg : { 0, 2 })
        {
            Settings s = base(kGlow, 0); s.oversampling = lg;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
            std::mt19937 rng(5); std::uniform_real_distribution<double> ph(0.0, kTwoPi);
            for (int k = 0; k < 24; ++k)
            {
                const double f = 120.0 * std::pow(9000.0 / 120.0, k / 23.0), p = ph(rng);
                for (int i = 0; i < n; ++i) L[(size_t)i] += (float)(0.003 * std::sin(kTwoPi * f * i / sr + p));
            }
            R = L;
            const std::vector<float> L0 = L;
            run(e, L, R);
            const int lat = e.latency();
            double er = 0, rr = 0;
            for (int i = n / 2; i < n; ++i) { const double d = L[(size_t)i] - L0[(size_t)(i - lat)]; er += d * d; rr += (double)L0[(size_t)(i - lat)] * L0[(size_t)(i - lat)]; }
            const double nul = 10 * std::log10(er / rr);
            CHECK(nul < -24.0, "glow, drive 0, quiet multitone at %dx nulls with the input to %.1f dB", 1 << lg, nul);
        }
        // ---- auto gain holds the test mix's level
        {
            std::vector<float> L0((size_t)n), R0((size_t)n);
            for (int i = 0; i < n; ++i) { L0[(size_t)i] = mix[(size_t)(2 * i)]; R0[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            const double ref = rmsDb(L0, R0, (size_t)n / 2);
            double worst = 0;
            for (int mode = 0; mode < kNumModes; ++mode)
                for (double drive : { 0.0, 12.0, 24.0, 36.0 })
                    for (int st : { 1, 3 })
                    {
                        Settings s = base(mode, drive); s.autoGain = true; s.stages = st; s.sag = 40;
                        Engine e; e.setSettings(s); e.prepare(sr, 512);
                        std::vector<float> L = L0, R = R0;
                        run(e, L, R);
                        worst = std::max(worst, std::fabs(rmsDb(L, R, (size_t)n / 2) - ref));
                    }
            CHECK(worst < 1.5, "auto gain: every circuit, drive 0..36 dB, 1 or 3 stages within %.2f dB of the input level", worst);
        }
        if (sr != 48000.0) continue;
        // ---- harmonic signatures (48 kHz, 1500 Hz = bin-exact for N 8192: 1500 * 8192 / 48000 = 256)
        auto measure = [&](Settings s, double amp, double f = 1500.0) {
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L, R; sine(L, R, sr, 48000, f, amp);
            run(e, L, R);
            return harmonics(L, sr, f);
        };
        {
            const Harm t = measure(base(kTriode, 12), 0.5);
            CHECK(t.h[0] > -35.0, "triode: strong 2nd harmonic (H2 %.1f dBc, H3 %.1f dBc)", t.h[0], t.h[1]);
            Settings pp = base(kPushPull, 12); pp.bias = 80;
            const Harm p = measure(pp, 0.5);
            CHECK(p.h[0] < -70.0 && p.h[1] > -45.0, "push-pull (hot): odd only (H2 %.1f dBc, H3 %.1f dBc)", p.h[0], p.h[1]);
            Settings ppc = base(kPushPull, 0); ppc.bias = -100;
            const Harm pc = measure(ppc, 0.05);
            Settings pph = ppc; pph.bias = 100;
            const Harm ph = measure(pph, 0.05);
            CHECK(pc.h[1] > ph.h[1] + 20.0, "push-pull cold bias: crossover distortion on a quiet note (H3 %.1f vs hot %.1f dBc)", pc.h[1], ph.h[1]);
            const Harm pe = measure(base(kPentode, 12), 0.5);
            CHECK(pe.h[1] > pe.h[0], "pentode: more odd than even (H2 %.1f, H3 %.1f dBc)", pe.h[0], pe.h[1]);
            Settings fz = base(kFuzz, 18); fz.bias = 100;
            const Harm fr = measure(fz, 0.5);
            fz.bias = 0;
            const Harm f0 = measure(fz, 0.5);
            CHECK(fr.h[0] > -3.0 && fr.h[0] > f0.h[0] + 6.0, "fuzz bias +100 rectifies (octave up): H2 %.1f dBc vs %.1f at bias 0", fr.h[0], f0.h[0]);
            Settings sv = base(kTriode, 18); sv.voltage = 100;
            const Harm v1 = measure(sv, 0.02);
            sv.voltage = 20;
            const Harm v2 = measure(sv, 0.02);
            auto thd = [](const Harm& h) { double p = 0; for (double d : h.h) p += std::pow(10.0, d / 10.0); return 10 * std::log10(p + 1e-30); };
            CHECK(thd(v2) > thd(v1) + 6.0, "voltage 20%% distorts more than 100%% (THD %.1f vs %.1f dB)", thd(v2), thd(v1));
        }
        // ---- aliasing falls with oversampling (hard-driven 7031.25 Hz: bin 1200 of 8192, its aliases fall between
        // harmonics)
        {
            double al[4];
            for (int lg = 0; lg <= 3; ++lg)
            {
                Settings s = base(kFuzz, 30); s.oversampling = lg;
                al[lg] = measure(s, 0.9, 7031.25).alias;
            }
            printf("  aliasing + noise (fuzz, drive 30, 7 kHz): 1x %.1f  2x %.1f  4x %.1f  8x %.1f dBc\n", al[0], al[1], al[2], al[3]);
            // (a 7 kHz sine driven 30 dB into the fuzz is a square wave: the worst case there is)
            CHECK(al[2] < al[0] - 30.0 && al[2] < -40.0, "4x oversampling + ADAA cuts aliasing by %.1f dB to %.1f dBc", al[0] - al[2], al[2]);
            CHECK(al[3] < -60.0, "8x: %.1f dBc", al[3]);
        }
        // ---- sag: a sustained loud note settles lower than its attack
        {
            auto bloom = [&](double sag) {
                Settings s = base(kTriode, 18); s.sag = sag;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; sine(L, R, sr, 24000, 220.0, 0.7);
                run(e, L, R, 64);
                return rmsDb(L, R, 48, 336) - rmsDb(L, R, 20000, 24000);   // the first ms vs after 400 ms
            };
            const double b0 = bloom(0), b1 = bloom(100);
            CHECK(b1 > b0 + 1.0, "sag 100%%: the note drops %.1f dB after its attack (sag 0: %.1f dB)", b1, b0);
        }
        // ---- circuit switches crossfade (no step on a steady tone)
        {
            Settings s = base(kGlow, 12);
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L, R; sine(L, R, sr, 48000, 100.0, 0.3);
            double maxJump = 0;
            for (int off = 0, k = 0; off < 48000; off += 128, ++k)
            {
                if (k % 20 == 10) { s.mode = (s.mode + 1) % kNumModes; e.setSettings(s); }
                e.process(L.data() + off, R.data() + off, 128);
            }
            for (int i = 4800; i < 48000; ++i) maxJump = std::max(maxJump, (double)std::fabs(L[(size_t)i] - L[(size_t)(i - 1)]));
            CHECK(maxJump < 0.06, "circuit switches on a 100 Hz tone: largest step %.3f", maxJump);
        }
        // ---- stress: random settings, block sizes, toggles undone mid-fade, absurd input
        {
            std::mt19937 rng(11);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.mode = (int)(u(rng) * kNumModes); s.drive = u(rng) * 36; s.bias = u(rng) * 200 - 100; s.sag = u(rng) * 100;
                    s.voltage = u(rng) * 100; s.stages = 1 + (int)(u(rng) * 3); s.focus = (int)(u(rng) * 4);
                    s.lowCut = 10 * std::pow(100.0, u(rng)); s.highCut = 1000 * std::pow(22.0, u(rng)); s.tone = u(rng) * 200 - 100;
                    s.mix = u(rng) * 100; s.autoGain = u(rng) < 0.5; s.oversampling = (int)(u(rng) * 4); s.outDb = 0;
                    e.setSettings(s);
                }
                if (it % 13 == 5) { Settings t = s; t.oversampling = (s.oversampling + 1) % 4; e.setSettings(t); e.setSettings(s); }
                const int bs = 1 + (int)(u(rng) * 1200);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 1500 && i == 0) L[0] = 1000.0f;   // absurd (but finite) input: must recover
                }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                    if (it > 1600) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 20.0, "random settings, sizes and mid-fade toggles: finite, peak %.2f after recovering from a +60 dB spike", peak);
        }
        // ---- CPU
        {
            for (auto cfg : { std::make_pair(2, 1), std::make_pair(3, 3) })
            {
                Settings s = base(kTriode, 18); s.oversampling = cfg.first; s.stages = cfg.second; s.sag = 50;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R((size_t)n);
                for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
                const auto t0 = std::chrono::steady_clock::now();
                run(e, L, R, 512);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("  cpu: %dx, %d stage(s): %.1f %% of one core\n", 1 << cfg.first, cfg.second, 100.0 * secs / 2.0);
            }
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
