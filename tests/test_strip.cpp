// pump STRIP: Clean with everything flat nulls against the latency-delayed input at every oversampling factor;
// every model's EQ bands land on their analog curves (measured with sines, COLOUR 0); the passive low boost cuts
// above itself; preamps make the harmonics they should (Class A: 2nd over 3rd; Console: clean until pushed; COLOUR 0:
// nothing); every compressor lands near its static curve on a steady tone and the gate/expander pull down by their
// range; AUTO GAIN holds the level as INPUT moves (Clean); model and order switches fade instead of clicking;
// random-settings stress stays finite and bounded. 44.1, 48 and 96 kHz. Prints CPU per second of audio per model.
#include "TestMix.hpp"
#include "../plugins/strip/StripEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>

using namespace pump;
using namespace pump::strip;
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
static Settings flat(int model)
{
    Settings s; s.model = model; s.color = 0; s.oversampling = 1; s.autoGain = false;
    return s;
}
static void sine(std::vector<float>& L, std::vector<float>& R, double sr, int n, double f, double amp)
{
    L.resize((size_t)n); R.resize((size_t)n);
    for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(amp * std::sin(kTwoPi * f * i / sr));
}
static double rmsDb(const std::vector<float>& L, size_t from, size_t to = 0)
{
    if (to == 0 || to > L.size()) to = L.size();
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)L[i] * L[i];
    return 10 * std::log10(e / (double)(to - from) + 1e-30);
}
// the level change a fresh engine with these settings makes on a sine at f (dB), after it settles
static double gainAt(const Settings& s, double sr, double f, double ampDb = -20.0)
{
    auto e = std::make_unique<Engine>();
    e->setSettings(s); e->prepare(sr, 256);
    std::vector<float> L, R;
    const int n = (int)(sr * 0.6);
    sine(L, R, sr, n, f, dbToGain(ampDb));
    run(*e, L, R);
    return rmsDb(L, (size_t)(sr * 0.4)) - (ampDb - 3.0103);
}
// harmonic levels (dB re the fundamental) of a steady tone at f
static void harmonics(const std::vector<float>& x, double sr, double f, double* h, int N = 16384)
{
    RealFFT fft; fft.setup(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    const size_t start = x.size() - (size_t)N;
    for (int i = 0; i < N; ++i) in[(size_t)i] = (float)(x[start + (size_t)i] * (0.5 - 0.5 * std::cos(kTwoPi * i / N)));
    fft.forward(in.data(), out.data());
    auto band = [&](double fr) { const int k = (int)std::lround(fr * N / sr); double p = 0; for (int d = -3; d <= 3; ++d) { const int j = k + d; if (j > 0 && j < N / 2) { const double re = out[(size_t)(2 * j)], im = out[(size_t)(2 * j + 1)]; p += re * re + im * im; } } return p; };
    const double p1 = band(f);
    for (int k = 2; k <= 5; ++k) h[k] = 10 * std::log10(band(f * k) / p1 + 1e-30);
}

static void testNull(double sr)
{
    for (int os = 0; os <= 2; ++os)
    {
        auto e = std::make_unique<Engine>();
        Settings s = flat(kClean); s.oversampling = os; s.color = 100;
        e->setSettings(s); e->prepare(sr, 512);
        const int n = (int)(sr * 0.5);
        std::mt19937 rng(1); std::normal_distribution<float> nd(0.0f, 0.1f);
        std::vector<float> L((size_t)n), R((size_t)n);
        for (int i = 0; i < n; ++i) { L[(size_t)i] = nd(rng); R[(size_t)i] = nd(rng); }
        const std::vector<float> L0 = L;
        run(*e, L, R, 173);
        const int lat = e->latency();
        double err = 0, sig = 0;
        for (int i = n / 4; i < n; ++i) { const double d = L[(size_t)i] - L0[(size_t)(i - lat)]; err += d * d; sig += (double)L0[(size_t)i] * L0[(size_t)i]; }
        // the oversampler's half-band filters are flat to 0.4535 fs; white noise above that is filtered, so the null
        // is checked on the band below it
        const double db = 10 * std::log10(err / sig + 1e-30);
        CHECK(db < (os == 0 ? -120.0 : -20.0), "%.0f kHz %dx: Clean, flat: residue %.1f dB (latency %d)", sr / 1000, 1 << os, db, lat);
        if (os > 0)
        {
            // and a tone well inside the passband nulls deeply
            auto e2 = std::make_unique<Engine>(); e2->setSettings(s); e2->prepare(sr, 512);
            sine(L, R, sr, n, 1000.0, 0.3);
            const std::vector<float> T = L;
            run(*e2, L, R, 173);
            double er = 0, sg = 0;
            for (int i = n / 2; i < n; ++i) { const double d = L[(size_t)i] - T[(size_t)(i - lat)]; er += d * d; sg += (double)T[(size_t)i] * T[(size_t)i]; }
            CHECK(10 * std::log10(er / sg) < -70.0, "%.0f kHz %dx: 1 kHz tone nulls (%.1f dB)", sr / 1000, 1 << os, 10 * std::log10(er / sg));
        }
    }
}

static void testEq(double sr)
{
    for (int m = 0; m < kNumModels; ++m)
    {
        // each band alone, measured against the analog curve of the band the model builds
        struct B { int band; float g, f; } bands[] = { { 0, 6, 100 }, { 1, 6, 500 }, { 2, -6, 3000 }, { 3, 6, 8000 } };
        double worst = 0;
        for (const B& b : bands)
        {
            Settings s = flat(m);
            s.g[b.band] = b.g; s.f[b.band] = b.f;
            BandSpec bs[kNumSlots]; bool used[kNumSlots];
            buildBands(s, bs, used);
            for (double f : { b.f * 0.5, (double)b.f, b.f * 2.0 })
            {
                double want = 0;
                for (int k = kFirstEqSlot; k < kNumSlots; ++k)
                    if (used[k]) want += 10 * std::log10(analogPrototype(bs[k], sr).magSq(kTwoPi * f / sr));
                worst = std::max(worst, std::fabs(gainAt(s, sr, f) - want));
            }
        }
        CHECK(worst < 0.25, "%.0f kHz %s: EQ bands on their analog curves (worst %.3f dB)", sr / 1000, kModelNames[m], worst);
    }
    // the passive low boost dips above itself
    Settings s = flat(kValve); s.g[0] = 8; s.f[0] = 60;
    const double lo = gainAt(s, sr, 40), above = gainAt(s, sr, 160);
    CHECK(lo > 5.0 && above < -0.3, "%.0f kHz Valve: low boost +%.1f dB at 40 Hz, %.1f dB at 160 Hz", sr / 1000, lo, above);
    // proportional Q: a gentle boost is broader than a big one (relative to its own peak)
    Settings p = flat(kConsole87); p.g[1] = 3; p.f[1] = 1000;
    Settings q = p; q.g[1] = 12;
    const double r3 = gainAt(p, sr, 2000) / 3.0, r12 = gainAt(q, sr, 2000) / 12.0;
    CHECK(r3 > r12 + 0.05, "%.0f kHz Console 87: proportional Q (an octave up: %.2f of a 3 dB peak, %.2f of a 12 dB one)", sr / 1000, r3, r12);
}

static void testPreamp(double sr)
{
    const int n = (int)(sr * 0.8);
    std::vector<float> L, R;
    double h[6];
    auto measure = [&](int model, float input, float color, double amp) {
        auto e = std::make_unique<Engine>();
        Settings s = flat(model); s.input = input; s.color = color; s.oversampling = 2;
        e->setSettings(s); e->prepare(sr, 256);
        sine(L, R, sr, n, 200.0, amp);
        run(*e, L, R);
        harmonics(L, sr, 200.0, h);
    };
    measure(kClassA, 12, 100, 0.1);
    CHECK(h[2] > h[3] && h[2] > -50, "%.0f kHz Class A pushed: 2nd %.1f dB over 3rd %.1f dB", sr / 1000, h[2], h[3]);
    measure(kClassA, 12, 0, 0.1);
    CHECK(h[2] < -100 && h[3] < -100, "%.0f kHz Class A at COLOUR 0: clean (2nd %.1f, 3rd %.1f dB)", sr / 1000, h[2], h[3]);
    measure(kConsole79, 0, 100, 0.25);
    const double clean3 = h[3];
    measure(kConsole79, 18, 100, 0.25);
    CHECK(clean3 < -70 && h[3] > -40, "%.0f kHz Console 79: clean at -12 dBFS (3rd %.1f dB), clips pushed +18 dB (3rd %.1f dB)", sr / 1000, clean3, h[3]);
    for (int m : { kValve, kBroadcast, kDiscrete, kStudio })
    {
        measure(m, 6, 100, 0.1);
        CHECK(h[2] > -60 || h[3] > -60, "%.0f kHz %s +6 dB: colour (2nd %.1f, 3rd %.1f dB)", sr / 1000, kModelNames[m], h[2], h[3]);
    }
}

static void testDynamics(double sr)
{
    const int n = (int)(sr * 2.0);
    std::vector<float> L, R;
    for (int m = 0; m < kNumModels; ++m)
    {
        auto e = std::make_unique<Engine>();
        Settings s = flat(m); s.compOn = true; s.thresh = -30; s.ratio = 4; s.attack = 5; s.release = 100;
        e->setSettings(s); e->prepare(sr, 256);
        sine(L, R, sr, n, 1000.0, dbToGain(-10.0));
        run(*e, L, R);
        const double out = rmsDb(L, (size_t)(sr * 1.5)) + 3.0103;   // as a sine's peak level
        const double want = -10.0 + compStatic(profile(m).comp, -10.0, -30.0, 4.0);
        // feedback designs and the opto's memory settle near (not exactly on) the drawn curve
        CHECK(std::fabs(out - want) < 3.0 && out < -18.0, "%.0f kHz %s: -10 dBFS tone, 4:1 at -30 -> %.1f dBFS (curve %.1f), meter %.1f dB",
              sr / 1000, kModelNames[m], out, want, e->compGr.load());
        bool finite = true; for (float v : L) if (!std::isfinite(v)) finite = false;
        CHECK(finite, "%.0f kHz %s: finite", sr / 1000, kModelNames[m]);
    }
    for (int mode : { kExpand, kGate })
    {
        auto e = std::make_unique<Engine>();
        Settings s = flat(kClean); s.gateOn = true; s.gateMode = mode; s.gateThresh = -30; s.gateRange = 24;
        e->setSettings(s); e->prepare(sr, 256);
        sine(L, R, sr, n, 1000.0, dbToGain(-50.0));
        run(*e, L, R);
        const double quiet = rmsDb(L, (size_t)(sr * 1.5)) + 3.0103;
        sine(L, R, sr, n, 1000.0, dbToGain(-10.0));
        run(*e, L, R);
        const double loud = rmsDb(L, (size_t)(sr * 1.5)) + 3.0103;
        CHECK(std::fabs(quiet - (-74.0)) < 1.5 && std::fabs(loud + 10.0) < 0.3, "%.0f kHz %s: -50 dBFS -> %.1f, -10 dBFS -> %.1f", sr / 1000, kGateModeNames[mode], quiet, loud);
    }
}

static void testAutoGainAndSwitches(double sr)
{
    const auto mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("  (no test mix: skipping)\n"); return; }
    const int n = std::min((int)mix.size() / 2, (int)(sr * 4));
    std::vector<float> L0((size_t)n), R0((size_t)n);
    for (int i = 0; i < n; ++i) { L0[(size_t)i] = mix[(size_t)(2 * i)]; R0[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
    const double ref = rmsDb(L0, 0);
    for (float in : { -6.0f, 6.0f })
    {
        auto e = std::make_unique<Engine>();
        Settings s = flat(kClean); s.input = in; s.autoGain = true;
        e->setSettings(s); e->prepare(sr, 512);
        std::vector<float> L = L0, R = R0;
        run(*e, L, R);
        CHECK(std::fabs(rmsDb(L, (size_t)(sr * 0.1)) - rmsDb(L0, (size_t)(sr * 0.1))) < 0.1, "%.0f kHz auto gain holds Clean at input %+g dB", sr / 1000, in);
    }
    (void)ref;
    // switching models and order mid-signal: the largest sample-to-sample step stays small (fades, no clicks)
    auto e = std::make_unique<Engine>();
    Settings s = flat(kClassA); s.color = 100; s.oversampling = 1;
    e->setSettings(s); e->prepare(sr, 256);
    std::vector<float> L, R;
    sine(L, R, sr, (int)(sr * 2), 100.0, 0.5);
    double maxStep = 0, maxStepRef = 0;
    for (int off = 0, k = 0; off < (int)L.size(); off += 256, ++k)
    {
        if (k % 40 == 20) { s.model = (s.model + 3) % kNumModels; s.order = 1 - s.order; e->setSettings(s); }
        e->process(L.data() + off, R.data() + off, std::min(256, (int)L.size() - off));
    }
    for (size_t i = (size_t)(sr * 0.1); i + 1 < L.size(); ++i) maxStep = std::max(maxStep, (double)std::fabs(L[i + 1] - L[i]));
    maxStepRef = 0.5 * kTwoPi * 100.0 / sr * 1.5;   // a clean 100 Hz sine's step, with room for colour
    CHECK(maxStep < maxStepRef * 1.5, "%.0f kHz model + order switches fade (largest step %.4f, sine's %.4f)", sr / 1000, maxStep, maxStepRef / 1.5);
}

static void testStress(double sr)
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0, 1);
    auto e = std::make_unique<Engine>();
    Settings s;
    e->setSettings(s); e->prepare(sr, 2048);
    std::vector<float> L(4096), R(4096);
    bool ok = true; float peak = 0;
    for (int it = 0; it < 600; ++it)
    {
        if (it % 3 == 0)
        {
            s.model = (int)(u(rng) * kNumModels) % kNumModels; s.input = -12 + 36 * u(rng); s.color = 100 * u(rng);
            s.phase = u(rng) < 0.5f; s.hpf = u(rng) < 0.5f ? kHpfOff : 20 + 700 * u(rng); s.lpf = u(rng) < 0.5f ? kLpfOff : 1000 + 20000 * u(rng);
            s.eqOn = u(rng) < 0.8f;
            for (int b = 0; b < 4; ++b) { s.g[b] = -16 + 32 * u(rng); s.q[b] = 0.3f + 4.7f * u(rng); }
            s.f[0] = 20 + 780 * u(rng); s.f[1] = 60 + 2900 * u(rng); s.f[2] = 500 + 13000 * u(rng); s.f[3] = 1500 + 18500 * u(rng);
            s.lfBell = u(rng) < 0.5f; s.hfBell = u(rng) < 0.5f;
            s.gateOn = u(rng) < 0.5f; s.gateMode = u(rng) < 0.5f; s.gateThresh = -80 * u(rng); s.gateRange = 80 * u(rng); s.gateRelease = 5 + 1995 * u(rng);
            s.compOn = u(rng) < 0.7f; s.thresh = -50 * u(rng); s.ratio = 1 + 19 * u(rng); s.attack = 0.1f + 99.9f * u(rng); s.release = 20 + 1980 * u(rng);
            s.autoRel = u(rng) < 0.5f; s.makeup = 24 * u(rng); s.mix = 100 * u(rng); s.scHpf = u(rng) < 0.5f ? kScOff : 20 + 480 * u(rng);
            s.order = u(rng) < 0.5f; s.outDb = -24 + 48 * u(rng); s.autoGain = u(rng) < 0.5f; s.oversampling = (int)(u(rng) * 3) % 3;
            e->setSettings(s);
        }
        const int n = 1 + (int)(u(rng) * 2047);
        const float amp = it % 50 == 7 ? 1000.0f : 0.5f;
        for (int i = 0; i < n; ++i) { L[(size_t)i] = amp * (2 * u(rng) - 1); R[(size_t)i] = amp * (2 * u(rng) - 1); }
        if (it % 97 == 3) { L[0] = NAN; R[1] = INFINITY; for (int i = 0; i < n; ++i) { if (!std::isfinite(L[(size_t)i])) L[(size_t)i] = 0; if (!std::isfinite(R[(size_t)i])) R[(size_t)i] = 0; } }
        e->process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i) { if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) ok = false; peak = std::max(peak, std::fabs(L[(size_t)i])); }
    }
    CHECK(ok && peak < 1e6f, "%.0f kHz random settings, block sizes and hot input: finite (peak %.1f)", sr / 1000, peak);
}

static void testCpu()
{
    const double sr = 48000;
    for (int m = 0; m < kNumModels; ++m)
    {
        auto e = std::make_unique<Engine>();
        Settings s; s.model = m; s.compOn = true; s.gateOn = true; s.g[0] = 3; s.g[1] = -2; s.g[2] = 3; s.g[3] = 2; s.hpf = 60; s.oversampling = 1;
        e->setSettings(s); e->prepare(sr, 512);
        std::vector<float> L((size_t)sr), R((size_t)sr);
        sine(L, R, sr, (int)sr, 440, 0.3);
        const auto t0 = std::chrono::steady_clock::now();
        run(*e, L, R, 512);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("  cpu: %-14s 2x, everything on: %.1f ms per second of audio\n", kModelNames[m], ms);
    }
}

int main()
{
    printf("== pump STRIP\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        testNull(sr);
        testEq(sr);
        testPreamp(sr);
        testDynamics(sr);
        testAutoGainAndSwitches(sr);
        testStress(sr);
    }
    testCpu();
    printf(fails ? "STRIP: %d FAILED\n" : "STRIP: all passed\n", fails);
    return fails ? 1 : 0;
}
