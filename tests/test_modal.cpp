// pump MODAL: a struck string rings at the note's pitch with its harmonics; the marimba's second mode is two octaves
// up; a strike in the middle of a string has no even harmonics; wood dies faster than metal and a soft mallet is
// darker than a hard one; the modes decay in DECAY and in DAMP after key up, and the voice frees itself; a bowed
// string sustains while held and settles at a level that doesn't depend on DECAY; breath sounds at the note; every
// object has its modes in its ratios; random settings stay finite. CPU.
#include "../plugins/modal/ModalEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::modal;
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
static double peakHz(const std::vector<float>& x, size_t a, size_t n, double lo, double hi)
{
    double best = 0, bf = lo;
    for (double f = lo; f <= hi; f += 0.25) { const double h = harm(x, a, n, f); if (h > best) { best = h; bf = f; } }
    return bf;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (double)(b - a)); }
static double bright(const std::vector<float>& x, size_t a, size_t n, double f1)
{
    double lo = 0, hi = 0;
    for (int k = 1; k <= 16; ++k) { const double h = harm(x, a, n, f1 * k); (k <= 2 ? lo : hi) += h * h; }
    return std::sqrt(hi / std::max(1e-20, lo));
}
static Settings plain(int model) { Settings s; s.model = model; s.modes = 32; s.noise = 0.0; s.spread = 0.0; s.velocity = 0.0; s.bright = 0.0; s.stretch = 0.0; return s; }
static Engine* fresh(const Settings& s) { auto* e = new Engine(); e->prepare(kSr, 256); e->setSettings(s); return e; }

int main()
{
    const double f69 = 440.0;
    printf("== struck string\n");
    {
        Settings s = plain(kString); s.material = 0.9; s.decay = 4.0; s.hardness = 1.0; s.position = 0.22;
        Engine* e = fresh(s);
        const auto y = play(*e, 69, (int)(1.0 * kSr));
        const double p = peakHz(y, 4800, 16384, 430, 450);
        const double h1 = harm(y, 4800, 16384, f69), h2 = harm(y, 4800, 16384, 2 * f69), h3 = harm(y, 4800, 16384, 3 * f69);
        CHECK(std::fabs(p - f69) < 1.0 && h2 > 0.05 * h1 && h3 > 0.03 * h1, "A4 rings at %.2f Hz, harmonics 2, 3 at %.2f, %.2f of the first", p, h2 / h1, h3 / h1);
        delete e;
        s.position = 1.0;   // the middle (POSITION 100% = half way along)
        e = fresh(s);
        const auto m = play(*e, 69, (int)(1.0 * kSr));
        const double m1 = harm(m, 4800, 16384, f69), m2 = harm(m, 4800, 16384, 2 * f69), m3 = harm(m, 4800, 16384, 3 * f69);
        CHECK(m2 < 0.03 * m1 && m3 > 0.1 * m1, "struck in the middle: harmonic 2 at %.3f (gone), 3 at %.2f", m2 / m1, m3 / m1);
        delete e;
    }
    printf("== objects\n");
    {
        Settings s = plain(kMarimba); s.material = 0.9; s.decay = 3.0; s.hardness = 1.0; s.position = 0.1;
        Engine* e = fresh(s);
        const auto y = play(*e, 57, (int)(1.0 * kSr));
        const double f1 = synth::midiToHz(57);
        const double r = peakHz(y, 2400, 16384, f1 * 3.8, f1 * 4.2) / f1;
        CHECK(std::fabs(r - 3.99) < 0.02, "the marimba's second mode is two octaves up: %.3f (3.99)", r);
        delete e;
        for (int m = 0; m < kNumModels; ++m)
        {
            const ModelTable& T = modelTable(m);
            bool up = true; for (int k = 1; k < kMaxModes; ++k) up = up && T.ratio[k] >= T.ratio[k - 1] - 1e-9 && T.level[k] > 0.0;
            CHECK(up, "%s: the modes go up (%.3f, %.3f, %.3f, ...)", kModelNames[m], T.ratio[0], T.ratio[1], T.ratio[2]);
        }
        // the bell's hum is an octave under its prime, the tube has only odd harmonics
        s = plain(kTube); s.material = 0.9; s.decay = 3.0; s.hardness = 1.0; s.position = 0.3;
        e = fresh(s);
        const auto t = play(*e, 57, (int)(1.0 * kSr));
        const double t1 = harm(t, 2400, 16384, f1), t2 = harm(t, 2400, 16384, 2 * f1), t3 = harm(t, 2400, 16384, 3 * f1);
        CHECK(t2 < 0.01 * t1 && t3 > 0.05 * t1, "the tube: harmonic 2 at %.3f (none), 3 at %.2f", t2 / t1, t3 / t1);
        delete e;
    }
    printf("== material and hardness\n");
    {
        double tail[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings s = plain(kBar); s.material = k ? 1.0 : 0.0; s.decay = 2.0; s.hardness = 1.0;
            Engine* e = fresh(s);
            const auto y = play(*e, 69, (int)(1.5 * kSr));
            const double f2 = f69 * 2.756;
            tail[k] = harm(y, (size_t)(0.6 * kSr), 8192, f2) / std::max(1e-12, harm(y, 1000, 8192, f2));
            delete e;
        }
        CHECK(tail[0] < 0.05 && tail[1] > 0.09, "the second mode after 0.6 s: wood %.3f of its start, metal %.2f", tail[0], tail[1]);
        double br[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings s = plain(kString); s.material = 0.8; s.decay = 3.0; s.hardness = k ? 1.0 : 0.0;
            Engine* e = fresh(s);
            br[k] = bright(play(*e, 57, (int)(0.6 * kSr)), 2400, 16384, synth::midiToHz(57));
            delete e;
        }
        CHECK(br[0] < 0.6 * br[1], "a soft mallet is darker than a hard one: %.3f vs %.3f", br[0], br[1]);
    }
    printf("== decay, damp and release\n");
    {
        Settings s = plain(kString); s.material = 1.0; s.decay = 2.0; s.damp = 0.1; s.hardness = 0.6; s.modes = 8;
        Engine* e = fresh(s);
        auto y = play(*e, 57, (int)(2.2 * kSr));
        const double f1 = synth::midiToHz(57);
        const double a0 = harm(y, 1200, 8192, f1), a1 = harm(y, (size_t)(1.0 * kSr), 8192, f1);
        const double db = 20.0 * std::log10(a1 / a0), want = -60.0 * (1.0 * kSr - 1200) / kSr / 2.0;
        CHECK(std::fabs(db - want) < 2.0, "held: the first mode falls %.1f dB in about a second (%.1f for DECAY 2 s)", db, want);
        delete e;
        e = fresh(s);
        y = play(*e, 57, (int)(1.0 * kSr), (int)(0.2 * kSr));
        const double after = rms(y, (size_t)(0.45 * kSr), (size_t)(0.5 * kSr)), before = rms(y, (size_t)(0.15 * kSr), (size_t)(0.2 * kSr));
        CHECK(after < before * 0.01, "key up with DAMP 0.1 s: %.1f dB a quarter second later", 20.0 * std::log10(after / before));
        std::vector<float> L(256), R(256);
        for (int i = 0; i < 400; ++i) e->process(L.data(), R.data(), 256, nullptr, 0);
        CHECK(e->voicesNow() == 0, "the voice frees itself (%d still on)", e->voicesNow());
        delete e;
    }
    printf("== bow and breath\n");
    {
        double lvl[2];
        const double f1 = synth::midiToHz(57);
        for (int k = 0; k < 2; ++k)
        {
            Settings s = plain(kString); s.exciter = kBow; s.material = 0.6; s.decay = k ? 8.0 : 1.0; s.pressure = 0.6; s.damp = 0.2;
            Engine* e = fresh(s);
            const auto y = play(*e, 57, (int)(3.0 * kSr), (int)(2.5 * kSr));
            const double a = rms(y, (size_t)(1.5 * kSr), (size_t)(2.0 * kSr)), b = rms(y, (size_t)(2.0 * kSr), (size_t)(2.5 * kSr)), z = rms(y, (size_t)(2.9 * kSr), (size_t)(3.0 * kSr));
            lvl[k] = a;
            const double p = peakHz(y, (size_t)(1.5 * kSr), 32768, f1 - 5, f1 + 5);
            CHECK(std::fabs(a / b - 1.0) < 0.15 && z < 0.05 * a && std::fabs(p - f1) < 1.0, "bowed (DECAY %.0f s): sustains (%.2f), at %.2f Hz (%.2f), stops after key up (%.3f)", s.decay, a / b, p, f1, z / a);
            delete e;
        }
        CHECK(std::fabs(20.0 * std::log10(lvl[1] / lvl[0])) < 6.0, "the bowed level hardly depends on DECAY: %.1f dB between 1 s and 8 s", 20.0 * std::log10(lvl[1] / lvl[0]));
        Settings s = plain(kTube); s.exciter = kBlow; s.decay = 0.6; s.pressure = 0.7; s.noise = 0.2;
        Engine* e = fresh(s);
        const auto y = play(*e, 57, (int)(1.5 * kSr));
        const double p = peakHz(y, (size_t)(0.5 * kSr), 32768, f1 * 0.8, f1 * 1.2);
        CHECK(std::fabs(p - f1) < 2.0 && rms(y, (size_t)(0.5 * kSr), (size_t)(1.5 * kSr)) > 0.01, "breath through a tube sounds at %.2f Hz (%.2f), level %.3f", p, f1, rms(y, (size_t)(0.5 * kSr), (size_t)(1.5 * kSr)));
        delete e;
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true; double worst = 0.0;
        for (int t = 0; t < 120; ++t)
        {
            Settings s;
            s.model = (int)(u(rng) * kNumModels) % kNumModels; s.modes = 4 + (int)(u(rng) * 45); s.exciter = (int)(u(rng) * kNumExciters) % kNumExciters;
            s.material = u(rng); s.decay = 0.05 + u(rng) * 20; s.damp = 0.02 + u(rng) * 10; s.bright = u(rng) * 2 - 1; s.stretch = u(rng) * 2 - 1;
            s.position = u(rng); s.hardness = u(rng); s.noise = u(rng); s.pressure = u(rng); s.vibrato = u(rng); s.vibRate = 0.5 + u(rng) * 11;
            s.velocity = u(rng); s.spread = u(rng); s.voiceMode = (int)(u(rng) * 3) % 3; s.glide = u(rng) * 500; s.octave = (int)(u(rng) * 7) - 3;
            Engine* e = fresh(s);
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 200; ++b)
            {
                synth::Midi ev[2] = { { 0, 0x90, (uint8_t)(24 + (b * 7) % 84), 110 }, { 100, 0x80, (uint8_t)(24 + ((b + 3) * 7) % 84), 0 } };
                e->process(L.data(), R.data(), 256, ev, (b % 9) == 0 ? 2 : 0);
                for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); worst = std::max(worst, (double)std::fabs(L[i])); }
            }
            if (getenv("DBG") && worst > 20.0) { printf("trial %d model %d ex %d modes %d mat %.2f dec %.2f bright %.2f str %.2f pos %.2f hard %.2f mode %d oct %d glide %.0f\n", t, s.model, s.exciter, s.modes, s.material, s.decay, s.bright, s.stretch, s.position, s.hardness, s.voiceMode, s.octave, s.glide); worst = 0; }
            delete e;
        }
        CHECK(finite && worst < 20.0, "120 random patches stay finite (loudest %.2f before the limiter)", worst);
    }
    printf("== CPU\n");
    {
        Settings s; s.modes = 48; s.model = kString; s.exciter = kBow; s.decay = 4.0;
        Engine* e = fresh(s);
        std::vector<float> L(256), R(256);
        for (int n = 0; n < 8; ++n) { synth::Midi on { 0, 0x90, (uint8_t)(48 + n * 3), 100 }; e->process(L.data(), R.data(), 256, &on, 1); }
        const int blocks = (int)(4.0 * kSr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) e->process(L.data(), R.data(), 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs / 4.0 < 0.25, "8 bowed voices of 48 modes: %.1f%% of one core", 100.0 * secs / 4.0);
        delete e;
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
