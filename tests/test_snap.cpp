// pump SNAP: on a synthetic drum hit, ATTACK +100 / -100 raises / lowers the peak-to-tail ratio and SUSTAIN +100 /
// -100 raises / lowers the tail; the gain trajectory is the same at -30 and -6 dBFS (level independence); steady tones
// settle to 0 dB of gain change; mix 0 and zero shaping null against the latency-delayed input; the lookahead latency
// is reported, the mix stays aligned and an attack boost is in place on the hit's first sample; the clipper never
// passes its ceiling; the band split with zero shaping is allpass-flat and shaping one band leaves the other alone;
// the detector focus lets the kick drive the shaping; automation and toggles don't click; random-settings stress
// (block sizes, toggles undone mid-fade, absurd input) stays finite and bounded. 44.1, 48 and 96 kHz. Also prints
// CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/snap/SnapEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::snap;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, int block = 256, std::vector<float>* g = nullptr)
{
    const int n = (int)L.size();
    if (g) g->assign((size_t)n, 0.0f);
    for (int off = 0; off < n; off += block)
        e.process(L.data() + off, R.data() + off, std::min(block, n - off), g ? g->data() + off : nullptr);
}
static Settings base(double attack = 0.0, double sustain = 0.0)
{
    Settings s; s.attack = attack; s.sustain = sustain;
    return s;
}
// A drum hit every `period` seconds: a 0.5 ms rise to a pitched body (150 -> 55 Hz) with a noisy click, decaying with
// a 60 ms time constant (the click with 4 ms). Peaks at `peak`.
static void drumHits(std::vector<float>& L, std::vector<float>& R, double sr, int n, double peak, double period = 0.5)
{
    L.assign((size_t)n, 0.0f);
    fx::Rng rng(7);
    const int P = (int)(period * sr);
    std::vector<double> x((size_t)n, 0.0);
    double mx = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double)(i % P) / sr;
        const double rise = std::min(1.0, t / 0.0005);
        const double ph = kTwoPi * (55.0 * t + (150.0 - 55.0) * 0.02 * (1.0 - std::exp(-t / 0.02)));
        const double v = rise * (std::sin(ph) * std::exp(-t / 0.060) + 0.5 * rng.bi() * std::exp(-t / 0.004));
        x[(size_t)i] = v;
        mx = std::max(mx, std::fabs(v));
    }
    for (int i = 0; i < n; ++i) L[(size_t)i] = (float)(x[(size_t)i] * peak / mx);
    R = L;
}
// peak (first 8 ms of each hit) and tail RMS (80..250 ms after each onset), in dB, averaged over the hits after the first
// (the first one comes out of silence); `lat` = the engine's latency
struct HitStats { double peak, tail; };
static HitStats hitStats(const std::vector<float>& y, double sr, int lat, double period = 0.5)
{
    const int P = (int)(period * sr), n = (int)y.size();
    double pk = 0, tl = 0; int hits = 0;
    for (int on = P; on + P <= n - lat; on += P, ++hits)
    {
        double p = 0, e = 0; int c = 0;
        for (int i = 0; i < (int)(0.008 * sr); ++i) p = std::max(p, (double)std::fabs(y[(size_t)(on + lat + i)]));
        for (int i = (int)(0.080 * sr); i < (int)(0.250 * sr); ++i, ++c) e += (double)y[(size_t)(on + lat + i)] * y[(size_t)(on + lat + i)];
        pk += gainToDb(p); tl += 10 * std::log10(e / c + 1e-30);
    }
    return { pk / hits, tl / hits };
}
static void sine(std::vector<float>& L, std::vector<float>& R, double sr, int n, double f, double amp)
{
    L.resize((size_t)n); R.resize((size_t)n);
    for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(amp * std::sin(kTwoPi * f * i / sr));
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)(sr * 2.5);
        // ---- attack and sustain on a drum hit
        {
            auto stats = [&](Settings s) {
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; drumHits(L, R, sr, n, 0.5);
                run(e, L, R);
                return hitStats(L, sr, e.latency());
            };
            const HitStats h0 = stats(base()), ap = stats(base(100)), am = stats(base(-100)), sp = stats(base(0, 100)), sm = stats(base(0, -100));
            const double r0 = h0.peak - h0.tail;
            CHECK(ap.peak - ap.tail > r0 + 6.0, "attack +100: peak-to-tail %.1f dB (unprocessed %.1f), peak %+.1f dB", ap.peak - ap.tail, r0, ap.peak - h0.peak);
            CHECK(am.peak - am.tail < r0 - 6.0, "attack -100: peak-to-tail %.1f dB (unprocessed %.1f), peak %+.1f dB", am.peak - am.tail, r0, am.peak - h0.peak);
            CHECK(sp.tail > h0.tail + 6.0 && std::fabs(sp.peak - h0.peak) < 1.0, "sustain +100: tail %+.1f dB, peak %+.2f dB", sp.tail - h0.tail, sp.peak - h0.peak);
            CHECK(sm.tail < h0.tail - 6.0 && std::fabs(sm.peak - h0.peak) < 1.0, "sustain -100: tail %+.1f dB, peak %+.2f dB", sm.tail - h0.tail, sm.peak - h0.peak);
            // moderate settings stay moderate (a professional range, not an on/off effect)
            const HitStats a3 = stats(base(30)), s3 = stats(base(0, 30));
            CHECK(a3.peak - h0.peak > 1.0 && a3.peak - h0.peak < 6.0 && std::fabs(s3.tail - h0.tail) > 1.0 && std::fabs(s3.tail - h0.tail) < 8.0,
                  "attack +30: peak %+.1f dB; sustain +30: tail %+.1f dB", a3.peak - h0.peak, s3.tail - h0.tail);
        }
        // ---- level independence: the same gain trajectory at -30 and -6 dBFS
        {
            double worst = 0;
            for (auto as : { std::make_pair(100.0, 0.0), std::make_pair(-100.0, 0.0), std::make_pair(0.0, 100.0), std::make_pair(0.0, -100.0), std::make_pair(60.0, -60.0) })
            {
                std::vector<float> g[2];
                int k = 0;
                for (double pk : { dbToGain(-30.0), dbToGain(-6.0) })
                {
                    Settings s = base(as.first, as.second);
                    Engine e; e.setSettings(s); e.prepare(sr, 512);
                    std::vector<float> L, R; drumHits(L, R, sr, n, pk);
                    run(e, L, R, 256, &g[k++]);
                }
                for (int i = 0; i < n; ++i) worst = std::max(worst, (double)std::fabs(g[0][(size_t)i] - g[1][(size_t)i]));
            }
            CHECK(worst < 0.5, "gain trajectories at -30 and -6 dBFS (attack/sustain +-100, 60/-60) differ by at most %.3f dB", worst);
        }
        // ---- steady tones settle to 0 dB of gain change
        {
            double worst = 0;
            for (double f : { 40.0, 100.0, 1000.0, 6000.0 })
                for (auto as : { std::make_pair(100.0, 100.0), std::make_pair(-100.0, -100.0) })
                    for (int focus = 0; focus < kNumFocus; ++focus)
                    {
                        Settings s = base(as.first, as.second); s.focus = focus; s.attackMs = 1; s.releaseMs = 20;
                        Engine e; e.setSettings(s); e.prepare(sr, 512);
                        std::vector<float> L, R, g; sine(L, R, sr, (int)sr * 2, f, 0.3);
                        run(e, L, R, 256, &g);
                        for (int i = (int)sr; i < (int)sr * 2; ++i) worst = std::max(worst, (double)std::fabs(g[(size_t)i]));
                    }
            CHECK(worst < 0.25, "steady sines (40 Hz..6 kHz, every focus, fastest times, +-100): gain change settles within %.3f dB", worst);
        }
        // ---- mix 0, and zero shaping: the delayed input, exactly (every lookahead)
        for (double la : { 0.0, 1.3, 5.0 })
        {
            double err[2] = { 0, 0 };
            int lat = 0;
            for (int k = 0; k < 2; ++k)
            {
                Settings s = k == 0 ? base(100, -100) : base(0, 0);
                if (k == 0) s.mix = 0;
                s.lookaheadMs = la;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R((size_t)n);
                for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i) % mix.size()]; R[(size_t)i] = mix[(size_t)(2 * i + 1) % mix.size()]; }
                const std::vector<float> L0 = L, R0 = R;
                run(e, L, R, 333);
                lat = e.latency();
                for (int i = lat; i < n; ++i) err[k] = std::max(err[k], (double)std::fabs(L[(size_t)i] - L0[(size_t)(i - lat)]) + std::fabs(R[(size_t)i] - R0[(size_t)(i - lat)]));
            }
            const int want = (int)std::lround(la * 0.001 * sr);
            CHECK(lat == want && err[0] < 1e-6 && err[1] < 1e-6, "lookahead %.1f ms: latency %d (want %d); mix 0 and zero shaping = input delayed (max error %.2g, %.2g)", la, lat, want, err[0], err[1]);
        }
        // ---- lookahead: the boost is in place on the hit's first sample; the shaped signal stays aligned
        {
            auto firstGain = [&](double la) {
                Settings s = base(100); s.lookaheadMs = la;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R, g; drumHits(L, R, sr, n, 0.5);
                run(e, L, R, 256, &g);
                const int on = (int)(0.5 * sr) + e.latency();   // the second hit's first sample at the output
                return (double)g[(size_t)on];
            };
            const double g0 = firstGain(0.0), g2 = firstGain(2.0);
            CHECK(g2 > 9.0 && g2 > g0 + 5.0, "attack boost on the hit's first sample: %.1f dB with 2 ms lookahead (%.1f dB without)", g2, g0);
            // aligned: with a large boost the output's onset is where the input's is (cross-correlation peak at the latency)
            Settings s = base(100); s.lookaheadMs = 3.0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L, R; drumHits(L, R, sr, n, 0.5);
            const std::vector<float> L0 = L;
            run(e, L, R);
            int best = -1; double bv = -1e30;
            for (int d = 0; d < (int)(0.010 * sr); ++d)
            {
                double c = 0;
                for (int i = (int)(0.5 * sr); i < (int)(0.6 * sr); ++i) c += (double)L[(size_t)(i + d)] * L0[(size_t)i];
                if (c > bv) { bv = c; best = d; }
            }
            CHECK(best == e.latency(), "3 ms lookahead: shaped output lines up with the input at %d samples (latency %d)", best, e.latency());
        }
        // ---- the clipper never passes its ceiling
        {
            double worst = -1e9, worstIn = 0;
            for (double ceil : { -12.0, -6.0, -1.0, 0.0 })
            {
                Settings s = base(100, 50); s.clip = true; s.ceilingDb = ceil; s.outDb = 18; s.lookaheadMs = 0.5;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R((size_t)n);
                for (int i = 0; i < n; ++i) { L[(size_t)i] = 2.0f * mix[(size_t)(2 * i) % mix.size()]; R[(size_t)i] = 2.0f * mix[(size_t)(2 * i + 1) % mix.size()]; }
                run(e, L, R, 111);
                double pk = 0;
                for (int i = 0; i < n; ++i) pk = std::max(pk, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                worst = std::max(worst, gainToDb(pk) - ceil);
                worstIn = std::max(worstIn, gainToDb(pk));
            }
            CHECK(worst <= 1e-5, "clipper on the hot test mix (+18 dB, attack +100): peaks at most %.6f dB from the ceiling (-12, -6, -1, 0 dBFS)", worst);
            // below the knee it is transparent
            Settings s = base(); s.clip = true; s.ceilingDb = 0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L, R; sine(L, R, sr, n, 1000.0, 0.5);
            const std::vector<float> L0 = L;
            run(e, L, R);
            double err = 0;
            for (int i = 0; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]));
            CHECK(err < 1e-6, "clipper is linear below its knee (-3 dB under the ceiling): max error %.2g", err);
        }
        // ---- band split with zero shaping is allpass-flat; shaping one band leaves the other alone
        {
            double worst = 0;
            const int N = 1 << 15;
            for (int band : { kBandLow, kBandHigh })
                for (double xo : { 40.0, 250.0, 8000.0 })
                {
                    Settings s = base(); s.band = band; s.crossover = xo; s.mix = 60;
                    Engine e; e.setSettings(s); e.prepare(sr, 512);
                    std::vector<float> L((size_t)N, 0.0f), R((size_t)N, 0.0f);
                    L[0] = R[0] = 1.0f;
                    run(e, L, R);
                    RealFFT fft; fft.setup(N);
                    AlignedBuffer in((size_t)N), out((size_t)N);
                    for (int i = 0; i < N; ++i) in[(size_t)i] = L[(size_t)i];
                    fft.forward(in.data(), out.data());
                    for (int k = 1; k < N / 2; ++k)
                    {
                        const double re = out[(size_t)(2 * k)], im = out[(size_t)(2 * k + 1)];
                        worst = std::max(worst, std::fabs(10 * std::log10(re * re + im * im)));
                    }
                }
            CHECK(worst < 0.01, "band split (low/high, 40 Hz / 250 Hz / 8 kHz, mix 60%%), zero shaping: magnitude flat within %.4f dB", worst);
            // sustain -100 on the low band (crossover 1 kHz): a 100 Hz tail drops, a 6 kHz tail stays
            auto tail = [&](int band, double f, double sus) {
                Settings s = base(0, sus); s.band = band; s.crossover = 1000;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R;
                for (int i = 0; i < n; ++i) { const double t = std::fmod((double)i / sr, 0.5); L[(size_t)i] = (float)(0.5 * std::sin(kTwoPi * f * i / sr) * std::exp(-t / 0.06) * std::min(1.0, t / 0.0005)); }
                R = L;
                run(e, L, R);
                return hitStats(L, sr, e.latency()).tail;
            };
            const double lo = tail(kBandLow, 100, -100) - tail(kBandLow, 100, 0), hi = tail(kBandLow, 6000, -100) - tail(kBandLow, 6000, 0);
            CHECK(lo < -10.0 && std::fabs(hi) < 0.5, "band Low, sustain -100: a 100 Hz tail %+.1f dB, a 6 kHz tail %+.2f dB", lo, hi);
        }
        if (sr != 48000.0) continue;
        // ---- detector focus: a clean kick (a 2 ms rise to a 60 Hz body, no click) under steady hi-hat-like noise; with
        // Low focus the kicks drive the boost (mean gain over the 20 ms after each onset), with High focus they don't
        // (it follows the noise, whose own small fluctuations average out the same with and without the kicks)
        {
            auto boost = [&](int focus) {
                Settings s = base(100); s.focus = focus;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R, g;
                fx::Rng rng(3); fx::Svf hp; hp.set(6000.0, 0.7, sr);
                for (int i = 0; i < n; ++i)
                {
                    const double t = std::fmod((double)i / sr, 0.5);
                    const double kick = 0.4 * std::sin(kTwoPi * 60.0 * t) * std::exp(-t / 0.08) * (0.5 - 0.5 * std::cos(kPi * std::min(1.0, t / 0.002)));
                    L[(size_t)i] = (float)(kick + 0.2 * hp.highpass(rng.bi()));
                }
                R = L;
                run(e, L, R, 256, &g);
                const int P = (int)(0.5 * sr), W = (int)(0.020 * sr);
                double onset = 0, between = 0;
                for (int on = P; on + P <= n; on += P)
                    for (int i = 0; i < W; ++i) { onset += g[(size_t)(on + i)]; between += g[(size_t)(on + P / 2 + i)]; }
                return onset - between > 0 ? (onset - between) / (W * (n / P - 1)) : 0.0;
            };
            const double bl = boost(kFocusLow), bh = boost(kFocusHigh);
            CHECK(bl > 5.0 && bh < 0.5, "focus: clean kicks under steady noise lift the gain by %.1f dB (mean, 20 ms) with Low focus, %.2f dB with High", bl, bh);
        }
        // ---- stereo link: a hit on the left only shapes the right with link 100 %, not with 0 %
        {
            auto rightGain = [&](double link) {
                Settings s = base(100); s.link = link;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; drumHits(L, R, sr, n, 0.5);
                for (int i = 0; i < n; ++i) R[(size_t)i] = (float)(0.1 * std::sin(kTwoPi * 440.0 * i / sr));
                const std::vector<float> R0 = R;
                run(e, L, R);
                double mx = 0; for (int i = n / 2; i < n; ++i) mx = std::max(mx, std::fabs((double)R[(size_t)i]) / 0.1);
                return gainToDb(mx);
            };
            const double r100 = rightGain(100), r0 = rightGain(0);
            CHECK(r100 > 6.0 && r0 < 0.5, "link: a left-only hit lifts the right channel by %.1f dB at 100%%, %.2f dB at 0%%", r100, r0);
        }
        // ---- delta: the mix minus the delta is the delayed input
        {
            Settings s = base(80, -60); s.lookaheadMs = 1.0;
            std::vector<float> outs[2];
            int lat = 0;
            for (int k = 0; k < 2; ++k)
            {
                s.delta = k == 1;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; drumHits(L, R, sr, n, 0.5);
                run(e, L, R);
                outs[k] = L; lat = e.latency();
            }
            std::vector<float> L0, R0; drumHits(L0, R0, sr, n, 0.5);
            double err = 0, dpk = 0;
            for (int i = lat; i < n; ++i) { err = std::max(err, (double)std::fabs(outs[0][(size_t)i] - outs[1][(size_t)i] - L0[(size_t)(i - lat)])); dpk = std::max(dpk, (double)std::fabs(outs[1][(size_t)i])); }
            CHECK(err < 1e-5 && dpk > 0.05, "delta = shaped - dry (residual %.2g, delta peak %.2f)", err, dpk);
        }
        // ---- automation and toggles don't click: a steady 100 Hz tone through attack/sustain sweeps, focus, band,
        // clip, delta and lookahead changes
        {
            Settings s = base();
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L, R; sine(L, R, sr, n, 100.0, 0.5);
            const double maxStep = 0.5 * kTwoPi * 100.0 / sr;   // the tone's own largest step
            double worst = 0;
            int k = 0;
            for (int off = 0; off < n; off += 128, ++k)
            {
                if (k % 25 == 5) { s.attack = (k / 25) % 2 ? 100 : -100; s.sustain = -s.attack; }
                if (k % 60 == 30) s.focus = (s.focus + 1) % kNumFocus;
                if (k % 70 == 35) s.band = (s.band + 1) % kNumBands;
                if (k % 90 == 45) s.lookaheadMs = s.lookaheadMs > 0 ? 0.0 : 3.0;
                if (k % 50 == 20) s.clip = !s.clip;
                s.ceilingDb = -6;
                e.setSettings(s);
                e.process(L.data() + off, R.data() + off, std::min(128, n - off));
            }
            for (int i = 4800; i < n; ++i) worst = std::max(worst, (double)std::fabs(L[(size_t)i] - L[(size_t)(i - 1)]));
            CHECK(worst < 2.0 * maxStep, "settings changes on a 100 Hz tone: largest step %.4f (the tone's own %.4f)", worst, maxStep);
        }
        // ---- the shape display: the same law (boost on the peak, longer tail)
        {
            constexpr int M = 400;
            float b[M], a[M];
            Settings s = base(100, 100);
            shapeDemo(s, 2000.0, 0.0, M, b, a);
            double pk = 0, pkB = 0; for (int i = 0; i < 20; ++i) { pk = std::max(pk, (double)a[i]); pkB = std::max(pkB, (double)b[i]); }
            CHECK(pk > 2.0 * pkB && a[300] > 2.0 * b[300], "shape display: peak %+.1f dB, tail at 150 ms %+.1f dB at +100/+100", gainToDb(pk / pkB), gainToDb(a[300] / b[300]));
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
                    s.attack = u(rng) * 200 - 100; s.sustain = u(rng) * 200 - 100; s.attackMs = std::pow(100.0, u(rng));
                    s.releaseMs = 20 * std::pow(100.0, u(rng)); s.link = u(rng) * 100; s.focus = (int)(u(rng) * 4); s.band = (int)(u(rng) * 3);
                    s.crossover = 40 * std::pow(200.0, u(rng)); s.lookaheadMs = u(rng) * 5; s.clip = u(rng) < 0.5; s.ceilingDb = -12 * u(rng);
                    s.delta = u(rng) < 0.2; s.mix = u(rng) * 100; s.outDb = 0;
                    e.setSettings(s);
                }
                if (it % 13 == 5) { Settings t = s; t.lookaheadMs = 5 - s.lookaheadMs; t.focus = (s.focus + 1) % 4; t.band = (s.band + 1) % 3; e.setSettings(t); e.setSettings(s); }
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
            CHECK(finite && peak < 10.0, "random settings, sizes and mid-fade toggles: finite, peak %.2f after recovering from a +60 dB spike", peak);
        }
        // ---- CPU
        {
            struct Cfg { const char* name; Settings s; };
            Settings d = base(40, -20);
            Settings full = base(60, -40); full.focus = kFocusLow; full.band = kBandLow; full.lookaheadMs = 2; full.clip = true; full.link = 50;
            for (const Cfg& c : { Cfg{ "default-ish (attack +40, sustain -20)", d }, Cfg{ "everything on (focus, band, lookahead, clip)", full } })
            {
                Engine e; e.setSettings(c.s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R((size_t)n);
                for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i) % mix.size()]; R[(size_t)i] = mix[(size_t)(2 * i + 1) % mix.size()]; }
                const auto t0 = std::chrono::steady_clock::now();
                run(e, L, R, 512);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("  cpu: %s: %.2f %% of one core\n", c.name, 100.0 * secs / ((double)n / sr));
            }
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
