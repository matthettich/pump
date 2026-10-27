// pump VECTOR: the bank's waves keep to their band limits; a saw at C8 has nothing between its harmonics; each corner
// of the square plays only its source; the vector envelope walks the path in time; a wave sequence steps at its rate
// (and transposes); a scanned string plays at the note, changes its timbre as it moves, dies with damping and doesn't
// alias at a high note; random settings stay finite. CPU.
#include "../plugins/vector/VectorEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::vector;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static const double kSr = 48000.0;

static std::vector<float> play(Engine& e, int note, int n, int offAt = -1)
{
    std::vector<float> L((size_t)n + 256), R((size_t)n + 256);
    synth::Midi on { 0, 0x90, (uint8_t)note, 127 }, off { 0, 0x80, (uint8_t)note, 0 };
    for (int o = 0; o < n; o += 256)
    {
        const synth::Midi* ev = o == 0 ? &on : (offAt >= 0 && o == (offAt / 256) * 256 ? &off : nullptr);
        e.process(L.data() + o, R.data() + o, 256, ev, ev ? 1 : 0);
    }
    L.resize((size_t)n);
    for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (L[i] + R[i]);
    return L;
}
static double harm(const std::vector<float>& x, size_t a, size_t n, double f)
{
    double re = 0, im = 0;
    for (size_t i = a; i < a + n; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i - a) / n); re += x[i] * w * std::cos(kTwoPi * f * i / kSr); im += x[i] * w * std::sin(kTwoPi * f * i / kSr); }
    return 2.0 * std::sqrt(re * re + im * im) / (0.5 * n);
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (double)(b - a)); }
static double harmonicShare(const std::vector<float>& x, size_t a, size_t n, double f)
{
    double hp = 0.0;
    for (int k = 1; k * f < kSr * 0.5; ++k) { const double h = harm(x, a, n, k * f); hp += 0.5 * h * h; }
    double tp = 0.0;
    for (size_t i = a; i < a + n; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i - a) / n); tp += x[i] * x[i] * w * w; }
    tp /= (0.375 * n);
    return hp / std::max(1e-20, tp);
}
// which of these frequencies is loudest in a window
static int loudest(const std::vector<float>& x, size_t a, size_t n, const std::vector<double>& fs)
{
    int b = 0; double bv = -1;
    for (size_t k = 0; k < fs.size(); ++k) { const double h = harm(x, a, n, fs[k]); if (h > bv) { bv = h; b = (int)k; } }
    return b;
}
// the share of the power within +-band Hz of the harmonics of f (a scanned string's motion puts sidebands a few Hz
// either side of each harmonic; aliases land anywhere)
static double nearHarmonics(const std::vector<float>& x, size_t a, double f, double band)
{
    const int N = 16384;
    RealFFT fft(N);
    std::vector<float> in(N), out(N);
    for (int i = 0; i < N; ++i) in[(size_t)i] = x[a + (size_t)i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
    fft.forward(in.data(), out.data());
    double near = 0.0, all = 0.0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double p = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
        const double hz = k * kSr / N, h = std::round(hz / f);
        all += p;
        if (h >= 1.0 && std::fabs(hz - h * f) <= band) near += p;
    }
    return near / std::max(1e-30, all);
}
static Settings plain()
{
    Settings s; s.cutoff = 18000.0; s.reso = 0.0; s.keytrack = 0.0; s.spread = 0.0; s.velocity = 0.0; s.attack = 0.001; s.sustain = 1.0; s.release = 0.05;
    for (auto& q : s.src) { q.type = kWave; q.wave = 0; }
    return s;
}
static Engine* fresh(const Settings& s) { auto* e = new Engine(); e->prepare(kSr, 256); e->setSettings(s); return e; }

int main()
{
    printf("== the wave bank\n");
    {
        const WaveBank& b = WaveBank::get();
        // the saw at mip 5 (32 harmonics): nothing from 33 up
        double in = 0, out = 0;
        for (int h = 1; h <= 96; ++h)
        {
            double re = 0, im = 0;
            for (int i = 0; i < 2048; ++i) { const double v = b.read(2, 5, i / 2048.0); re += v * std::cos(kTwoPi * h * i / 2048.0); im += v * std::sin(kTwoPi * h * i / 2048.0); }
            (h <= 32 ? in : out) += re * re + im * im;
        }
        CHECK(out < 1e-8 * in, "the saw's 32-harmonic table has nothing above harmonic 32 (%.1g of its power)", out / in);
        bool sane = true;
        for (int w = 0; w < kNumWaves; ++w) for (int m = 0; m < WaveBank::kMips; ++m) for (int i = 0; i < 2048; i += 7) sane = sane && std::isfinite(b.read(w, m, i / 2048.0)) && std::fabs(b.read(w, m, i / 2048.0)) < 2.0;
        CHECK(sane, "all 32 waves at all 11 band limits are finite and within +-2");
        Settings s = plain(); s.x = 0.0; s.y = 0.0; s.src[0].wave = 2;
        Engine* e = fresh(s);
        const auto y = play(*e, 108, (int)(0.5 * kSr));
        const double sh = harmonicShare(y, 4800, 16384, synth::midiToHz(108));
        CHECK(sh > 0.999, "a saw at C8: %.3f %% on its harmonics", 100.0 * sh);
        delete e;
    }
    printf("== the square\n");
    {
        // A sine at the note, B a fifth up, C an octave up, D an octave and a fifth up
        const double f = synth::midiToHz(57);
        const std::vector<double> fs = { f, f * std::pow(2.0, 7 / 12.0), 2 * f, f * std::pow(2.0, 19 / 12.0) };
        Settings s = plain(); s.src[1].tune = 7; s.src[2].tune = 12; s.src[3].tune = 19;
        bool corners = true; double worst = 1e-9;
        const double cx[4] = { 0, 1, 0, 1 }, cy[4] = { 0, 0, 1, 1 };
        for (int c = 0; c < 4; ++c)
        {
            s.x = cx[c]; s.y = cy[c];
            Engine* e = fresh(s);
            const auto y = play(*e, 57, (int)(0.5 * kSr));
            const double own = harm(y, 4800, 16384, fs[(size_t)c]);
            for (int o = 0; o < 4; ++o) if (o != c) { const double r = harm(y, 4800, 16384, fs[(size_t)o]) / own; worst = std::max(worst, r); corners = corners && r < 0.003; if (r >= 0.003) printf("    corner %c: %c at %.4f\n", 'A' + c, 'A' + o, r); }
            delete e;
        }
        CHECK(corners, "each corner plays only its source (the others at least %.0f dB down)", -20.0 * std::log10(worst));
        s.vecAmt = 1.0; s.vecTime = 0.3; s.vecLoop = kVecOnce;
        Engine* e = fresh(s);
        const auto y = play(*e, 57, (int)(1.4 * kSr));
        // P1 (0,0) A -> P2 (1,0) B -> P3 (1,1) D -> P4 (0,1) C
        const int a = loudest(y, 0, 2048, fs), b = loudest(y, (size_t)(0.29 * kSr), 2048, fs), d = loudest(y, (size_t)(0.59 * kSr), 2048, fs), c = loudest(y, (size_t)(1.1 * kSr), 4096, fs);
        CHECK(a == 0 && b == 1 && d == 3 && c == 2, "the vector envelope walks A, B, D, then holds at C (heard %c %c %c %c)", 'A' + a, 'A' + b, 'A' + d, 'A' + c);
        delete e;
    }
    printf("== wave sequences\n");
    {
        const double f = synth::midiToHz(57);
        Settings s = plain(); s.x = 0.0; s.y = 0.0; s.src[0].type = kSequence; s.src[0].wave = 0;
        s.seqSteps = 2; s.seqStride = 0; s.seqTranspose = 12; s.seqRate = 0.2; s.seqXfade = 0.0;
        Engine* e = fresh(s);
        const auto y = play(*e, 57, (int)(1.0 * kSr));
        const std::vector<double> fs = { f, 2 * f };
        int pattern[4]; for (int k = 0; k < 4; ++k) pattern[k] = loudest(y, (size_t)((0.05 + 0.2 * k) * kSr), 4096, fs);
        CHECK(pattern[0] == 0 && pattern[1] == 1 && pattern[2] == 0 && pattern[3] == 1, "a two-step sequence with TRANSPOSE 12 alternates every 200 ms: %d %d %d %d", pattern[0], pattern[1], pattern[2], pattern[3]);
        delete e;
        // stepping through the bank: sine, then (stride 2) the saw
        s.seqSteps = 2; s.seqStride = 2; s.seqTranspose = 0;
        e = fresh(s);
        const auto z = play(*e, 57, (int)(0.5 * kSr));
        const double h2a = harm(z, 1000, 4096, 2 * f) / harm(z, 1000, 4096, f), h2b = harm(z, (size_t)(0.25 * kSr), 4096, 2 * f) / harm(z, (size_t)(0.25 * kSr), 4096, f);
        CHECK(h2a < 0.01 && h2b > 0.3, "stride 2 steps from the sine (harmonic 2 at %.3f) to the saw (%.2f)", h2a, h2b);
        delete e;
    }
    printf("== scanned strings\n");
    {
        const double f = synth::midiToHz(57);
        Settings s = plain(); s.x = 0.0; s.y = 0.0; s.src[0].type = kScanned; s.src[0].wave = 2; s.scanTension = 0.5; s.scanDamp = 0.6; s.scanSpeed = 0.5;
        Engine* e = fresh(s);
        const auto y = play(*e, 57, (int)(2.0 * kSr));
        double peak = 0, pf = 0; for (double q = f * 0.8; q < f * 1.2; q += 0.5) { const double h = harm(y, 2400, 8192, q); if (h > peak) { peak = h; pf = q; } }
        auto centroid = [&](size_t a) { double n = 0, d = 0; for (int k = 1; k <= 20; ++k) { const double h = harm(y, a, 4096, k * f); n += k * h * h; d += h * h; } return n / std::max(1e-20, d); };
        double c0 = 1e9, c1 = -1e9;   // the spectrum's centre, over a second and a half (lightly damped)
        {
            Settings t = s; t.scanDamp = 0.15;
            Engine* e2 = fresh(t);
            const auto w = play(*e2, 57, (int)(2.0 * kSr));
            for (int k = 0; k < 16; ++k)
            {
                const size_t a = 2400 + (size_t)(k * 0.1 * kSr);
                double n = 0, d = 0; for (int j = 1; j <= 20; ++j) { const double h = harm(w, a, 4096, j * f); n += j * h * h; d += h * h; }
                const double c = n / std::max(1e-20, d); c0 = std::min(c0, c); c1 = std::max(c1, c);
            }
            delete e2;
        }
        const double r0 = rms(y, 2400, 7200), r1 = rms(y, (size_t)(1.8 * kSr), (size_t)(1.9 * kSr));
        CHECK(std::fabs(pf - f) < 1.5, "plays at the note: %.1f Hz (%.1f)", pf, f);
        CHECK(std::fabs(c1 - c0) > 0.3, "its timbre moves: the spectrum's centre wanders between harmonic %.2f and %.2f", c0, c1);
        CHECK(r1 < 0.5 * r0, "damping lets it die: %.1f dB after 1.8 s", 20.0 * std::log10(r1 / r0));
        delete e;
        s.scanDamp = 0.0; s.scanTension = 1.0; s.scanSpeed = 1.0; s.src[0].wave = 28;
        e = fresh(s);
        const auto z = play(*e, 96, (int)(0.6 * kSr));
        const double sh = nearHarmonics(z, 4800, synth::midiToHz(96), 80.0);
        CHECK(sh > 0.995, "a hard, fast string at C7: %.2f %% within 80 Hz of its harmonics (no aliases)", 100.0 * sh);
        delete e;
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(9);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true; double worst = 0.0;
        for (int t = 0; t < 120; ++t)
        {
            Settings s;
            for (auto& q : s.src) { q.type = (int)(u(rng) * 3) % 3; q.wave = (int)(u(rng) * kNumWaves) % kNumWaves; q.tune = (int)(u(rng) * 48) - 24; q.fine = u(rng) * 100 - 50; q.level = u(rng); }
            s.x = u(rng); s.y = u(rng); s.vecAmt = u(rng); s.vecTime = 0.01 + u(rng) * 3; s.vecLoop = (int)(u(rng) * 3) % 3;
            for (int k = 0; k < 4; ++k) { s.px[k] = u(rng); s.py[k] = u(rng); }
            s.seqRate = 0.01 + u(rng); s.seqSteps = 1 + (int)(u(rng) * 16) % 16; s.seqStride = (int)(u(rng) * 17) - 8; s.seqXfade = u(rng); s.seqLoop = (int)(u(rng) * 3) % 3; s.seqTranspose = (int)(u(rng) * 25) - 12;
            s.scanTension = u(rng); s.scanDamp = u(rng); s.scanCentre = u(rng); s.scanHammer = u(rng); s.scanSpeed = u(rng);
            s.cutoff = 30.0 * std::pow(600.0, u(rng)); s.reso = u(rng); s.envCut = u(rng) * 2 - 1; s.lfoPitch = u(rng); s.lfoX = u(rng); s.lfoY = u(rng); s.lfoCut = u(rng);
            s.voiceMode = (int)(u(rng) * 3) % 3; s.glide = u(rng) * 300; s.octave = (int)(u(rng) * 7) - 3;
            Engine* e = fresh(s);
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 100; ++b)
            {
                synth::Midi ev[2] = { { 0, 0x90, (uint8_t)(20 + (b * 13) % 100), 110 }, { 90, 0x80, (uint8_t)(20 + ((b + 5) * 13) % 100), 0 } };
                e->process(L.data(), R.data(), 256, ev, (b % 5) == 0 ? 2 : 0);
                for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); worst = std::max(worst, (double)std::fabs(L[i])); }
            }
            delete e;
        }
        CHECK(finite && worst < 10.0, "120 random patches stay finite (loudest %.2f before the limiter)", worst);
    }
    printf("== CPU\n");
    {
        Settings s; for (auto& q : s.src) q.type = kScanned; s.x = 0.5; s.y = 0.5;
        Engine* e = fresh(s);
        std::vector<float> L(256), R(256);
        for (int n = 0; n < 8; ++n) { synth::Midi on { 0, 0x90, (uint8_t)(50 + n * 3), 100 }; e->process(L.data(), R.data(), 256, &on, 1); }
        const int blocks = (int)(4.0 * kSr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) e->process(L.data(), R.data(), 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs / 4.0 < 0.3, "8 voices, all four sources scanned strings: %.1f%% of one core", 100.0 * secs / 4.0);
        delete e;
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
