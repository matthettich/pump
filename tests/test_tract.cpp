// pump TRACT: a singer sings at the note; each vowel's first two formants (read from the sung harmonics' envelope)
// land near the textbook values; a bigger tract lowers them; more singers spread the pitch; the voice frees itself;
// random settings stay finite. CPU.
#include "../plugins/tract/TractEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::tract;
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
static Settings plain() { Settings s; s.motion = 0.0; s.singers = 1; s.vibrato = 0.0; s.jitter = 0.0; s.breath = 0.0; s.attack = 0.01; s.release = 0.05; s.velocity = 0.0; s.bright = 1.0; s.spread = 0.0; return s; }
// the formants: peaks of the harmonics' levels (whitened, smoothed over neighbours), the first in 200 Hz - 1 kHz, the next above it
static void formants(const std::vector<float>& y, double f0, double& F1, double& F2)
{
    std::vector<double> h;
    for (int k = 1; k * f0 < 4000.0; ++k) h.push_back(harm(y, (size_t)(0.3 * kSr), 32768, k * f0) * k);   // x k: the glottal pulse's own fall, roughly taken out
    std::vector<double> sm(h.size());
    for (size_t k = 0; k < h.size(); ++k) { double a = 0, n = 0; for (int j = -1; j <= 1; ++j) { const long q = (long)k + j; if (q >= 0 && q < (long)h.size()) { a += h[(size_t)q] * (j == 0 ? 2.0 : 1.0); n += j == 0 ? 2.0 : 1.0; } } sm[k] = a / n; }
    F1 = F2 = 0.0; double b1 = 0, b2 = 0;
    for (size_t k = 0; k < sm.size(); ++k)
    {
        const double f = (k + 1) * f0;
        if (f > 200.0 && f < 1000.0 && sm[k] > b1) { b1 = sm[k]; F1 = f; }
    }
    // F2: the first clear peak above F1 (at least a quarter of the strongest up there)
    double top = 0.0; for (size_t k = 0; k < sm.size(); ++k) if ((k + 1) * f0 > F1 * 1.35 && (k + 1) * f0 < 3200.0) top = std::max(top, sm[k]);
    for (size_t k = 1; k + 1 < sm.size(); ++k)
    {
        const double f = (k + 1) * f0;
        if (f > F1 * 1.35 && f < 3200.0 && sm[k] >= sm[k - 1] && sm[k] >= sm[k + 1] && sm[k] >= 0.25 * top) { F2 = f; break; }
    }
    (void)b2;
}

int main()
{
    printf("== pitch\n");
    {
        Engine e; e.prepare(kSr, 256); e.setSettings(plain());
        const auto y = play(e, 57, (int)(1.2 * kSr));
        double best = 0, bf = 0; for (double f = 210; f < 230; f += 0.25) { const double h = harm(y, (size_t)(0.3 * kSr), 32768, f); if (h > best) { best = h; bf = f; } }
        CHECK(std::fabs(bf - 220.0) < 0.6, "a singer sings at %.2f Hz (220)", bf);
    }
    printf("== vowels\n");
    {
        struct T { const char* n; double v, F1, F2; };
        const T ts[] = { { "A", 0.0, 730, 1090 }, { "E", 0.25, 530, 1840 }, { "I", 0.5, 270, 2290 }, { "O", 0.75, 570, 840 }, { "U", 1.0, 300, 870 } };
        for (const T& t : ts)
        {
            Settings s = plain(); s.vowel = t.v;
            Engine e; e.prepare(kSr, 256); e.setSettings(s);
            const auto y = play(e, 36, (int)(1.2 * kSr));   // 65 Hz: harmonics close enough to trace the formants
            double F1, F2; formants(y, synth::midiToHz(36), F1, F2);
            CHECK(std::fabs(std::log(F1 / t.F1)) < 0.22 && std::fabs(std::log(F2 / t.F2)) < 0.22, "%s: formants near %.0f and %.0f Hz (%.0f, %.0f)", t.n, F1, F2, t.F1, t.F2);
        }
        double f2[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings s = plain(); s.vowel = 0.5; s.size = k ? 1.4 : 1.0;
            Engine e; e.prepare(kSr, 256); e.setSettings(s);
            const auto y = play(e, 36, (int)(1.2 * kSr));
            double F1; formants(y, synth::midiToHz(36), F1, f2[k]);
        }
        CHECK(f2[1] < 0.85 * f2[0], "a 40 %% bigger tract lowers I's second formant: %.0f Hz to %.0f Hz", f2[0], f2[1]);
    }
    printf("== singers, release\n");
    {
        Settings s = plain(); s.singers = 4; s.detune = 30; s.spread = 1.0;
        Engine e; e.prepare(kSr, 256); e.setSettings(s);
        std::vector<float> L((size_t)kSr), R((size_t)kSr);
        synth::Midi on { 0, 0x90, 57, 100 };
        for (int o = 0; o < (int)kSr - 256; o += 256) e.process(L.data() + o, R.data() + o, 256, o == 0 ? &on : nullptr, o == 0);
        double sab = 0, saa = 0, sbb = 0; for (size_t i = (size_t)(0.3 * kSr); i < (size_t)(0.95 * kSr); ++i) { sab += L[i] * R[i]; saa += L[i] * L[i]; sbb += R[i] * R[i]; }
        CHECK(sab / std::sqrt(saa * sbb) < 0.8, "four detuned singers spread across the stereo field (correlation %.2f)", sab / std::sqrt(saa * sbb));
        Engine e2; e2.prepare(kSr, 256); e2.setSettings(plain());
        play(e2, 57, (int)(0.4 * kSr), (int)(0.2 * kSr));
        std::vector<float> l(256), r(256); for (int i = 0; i < 60; ++i) e2.process(l.data(), r.data(), 256, nullptr, 0);
        CHECK(e2.voicesNow() == 0, "the voice frees itself (%d on)", e2.voicesNow());
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(17);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true; double worst = 0.0;
        for (int t = 0; t < 80; ++t)
        {
            Settings s;
            s.vowel = u(rng); s.motion = u(rng); s.motionRate = 0.02 + u(rng) * 8; s.size = 0.6 + u(rng) * 0.9; s.tense = u(rng); s.breath = u(rng);
            s.vibrato = u(rng); s.vibRate = 2 + u(rng) * 7; s.jitter = u(rng); s.singers = 1 + (int)(u(rng) * 4) % 4; s.detune = u(rng) * 50;
            s.sizeSpread = u(rng); s.vowelSpread = u(rng); s.spread = u(rng); s.bright = u(rng);
            s.voiceMode = (int)(u(rng) * 3) % 3; s.glide = u(rng) * 300; s.octave = (int)(u(rng) * 7) - 3;
            Engine e; e.prepare(kSr, 256); e.setSettings(s);
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 80; ++b)
            {
                synth::Midi ev[2] = { { 0, 0x90, (uint8_t)(24 + (b * 13) % 84), 110 }, { 90, 0x80, (uint8_t)(24 + ((b + 5) * 13) % 84), 0 } };
                e.process(L.data(), R.data(), 256, ev, (b % 7) == 0 ? 2 : 0);
                for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); worst = std::max(worst, (double)std::fabs(L[i])); }
            }
        }
        CHECK(finite && worst < 10.0, "80 random patches stay finite (loudest %.2f before the limiter)", worst);
    }
    printf("== CPU\n");
    {
        Settings s; s.singers = 2;
        Engine e; e.prepare(kSr, 256); e.setSettings(s);
        std::vector<float> L(256), R(256);
        for (int n = 0; n < 6; ++n) { synth::Midi on { 0, 0x90, (uint8_t)(50 + n * 3), 100 }; e.process(L.data(), R.data(), 256, &on, 1); }
        const int blocks = (int)(4.0 * kSr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) e.process(L.data(), R.data(), 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs / 4.0 < 0.3, "6 notes of 2 singers: %.1f%% of one core", 100.0 * secs / 4.0);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
