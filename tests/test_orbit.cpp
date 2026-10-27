// pump ORBIT: the stored calibration matches a fresh one; every system is alive (moving, not stuck on a fixed point)
// at every CHAOS step; a periodic orbit plays at the note's pitch (Rossler, Lorenz, Duffing); Rossler doubles its
// period as CHAOS rises (a strong subharmonic appears); SYNC 100 makes every period the same and free chaos doesn't;
// WIDTH makes the channels differ; the voice frees itself after the release; random settings stay finite. CPU.
#include "../plugins/orbit/OrbitEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::orbit;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static const double kSr = 48000.0;

static void play(Engine& e, int note, int n, std::vector<float>& L, std::vector<float>& R, int offAt = -1)
{
    L.assign((size_t)n + 256, 0.0f); R.assign((size_t)n + 256, 0.0f);
    synth::Midi on { 0, 0x90, (uint8_t)note, 127 }, off { 0, 0x80, (uint8_t)note, 0 };
    for (int o = 0; o < n; o += 256)
    {
        const synth::Midi* ev = o == 0 ? &on : (offAt >= 0 && o == (offAt / 256) * 256 ? &off : nullptr);
        e.process(L.data() + o, R.data() + o, 256, ev, ev ? 1 : 0);
    }
    L.resize((size_t)n); R.resize((size_t)n);
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
// how alike one period is to the next (correlation, averaged over many periods)
static double periodLikeness(const std::vector<float>& x, size_t a, double f, int periods)
{
    const double P = kSr / f;
    double acc = 0.0;
    for (int k = 0; k < periods; ++k)
    {
        double sab = 0, saa = 0, sbb = 0;
        const size_t s0 = a + (size_t)(k * P), s1 = a + (size_t)((k + 1) * P), m = (size_t)P;
        for (size_t i = 0; i < m; ++i) { const double u = x[s0 + i], w = x[s1 + i]; sab += u * w; saa += u * u; sbb += w * w; }
        acc += sab / std::sqrt(std::max(1e-20, saa * sbb));
    }
    return acc / periods;
}
static Settings plain(int sys, double chaos, double sync)
{
    Settings s; s.system = sys; s.chaos = chaos; s.sync = sync; s.width = 0.0; s.drive = 0.0; s.cutoff = 18000.0; s.reso = 0.0; s.keytrack = 0.0;
    s.attack = 0.001; s.sustain = 1.0; s.release = 0.05; s.velocity = 0.0;
    return s;
}
static Engine* fresh(const Settings& s) { auto* e = new Engine(); e->prepare(kSr, 256); e->setSettings(s); return e; }

int main()
{
    printf("== calibration\n");
    {
        const CalTable got = freshCalTable();
        const CalTable& stored = calTable();
        double worst = 0.0;
        for (int s = 0; s < kNumSystems; ++s) for (int i = 0; i < kCalPoints; ++i)
        {
            const Cal& a = got[(size_t)s][(size_t)i]; const Cal& b = stored[(size_t)s][(size_t)i];
            worst = std::max(worst, std::fabs(a.turn - b.turn) / b.turn);
            for (int k = 0; k < 3; ++k) worst = std::max(worst, std::fabs(a.sd[k] - b.sd[k]) / std::max(1e-9, b.sd[k]));
        }
        CHECK(worst < 1e-3, "OrbitCal.inc matches a fresh calibration (worst difference %.2g)", worst);
        bool alive = true; double least = 1e9;
        for (int s = 0; s < kNumSystems; ++s) for (int i = 0; i < kCalPoints; ++i)
        {
            const Cal& c = stored[(size_t)s][(size_t)i];
            const double spread = std::max(c.sd[0], std::max(c.sd[1], s == kDuffing ? 0.0 : c.sd[2]));
            const double scale = std::fabs(c.mean[0]) + std::fabs(c.mean[1]) + std::fabs(c.mean[2]) + 1.0;
            least = std::min(least, spread / scale);
            alive = alive && spread / scale > 0.02 && std::isfinite(c.turn) && c.turn > 0.0;
        }
        CHECK(alive, "every system moves at every CHAOS step (the least spread is %.3f of its size)", least);
    }
    printf("== pitch\n");
    for (int sys : { kRossler, kLorenz, kDuffing, kScroll })
    {
        Settings s = plain(sys, 0.0, 1.0);
        Engine* e = fresh(s);
        std::vector<float> L, R;
        play(*e, 57, (int)(1.2 * kSr), L, R);
        const double f = synth::midiToHz(57);
        const double p = peakHz(L, (size_t)(0.3 * kSr), 32768, f * 0.9, f * 1.1);
        const double h1 = harm(L, (size_t)(0.3 * kSr), 32768, f), sub = harm(L, (size_t)(0.3 * kSr), 32768, f * 0.5);
        CHECK(std::fabs(p - f) < 1.0 && sub < 0.3 * h1, "%s at CHAOS 0, SYNC 100: plays %.2f Hz (%.2f), the sub-octave at %.2f of it", kSystemNames[sys], p, f, sub / h1);
        delete e;
    }
    printf("== period doubling and sync\n");
    {
        const double f = synth::midiToHz(57);
        double ratio[2];
        const double chaos[2] = { 0.0, (3.6 - 2.5) / 6.5 };   // c 2.5 (period 1) and 3.6 (period 2)
        for (int k = 0; k < 2; ++k)
        {
            Engine* e = fresh(plain(kRossler, chaos[k], 0.0));
            std::vector<float> L, R;
            play(*e, 57, (int)(1.5 * kSr), L, R);
            // period 2: a component at half the turn rate
            const double base = peakHz(L, (size_t)(0.4 * kSr), 32768, f * 0.85, f * 1.15);
            ratio[k] = harm(L, (size_t)(0.4 * kSr), 32768, base * 0.5) / harm(L, (size_t)(0.4 * kSr), 32768, base);
            delete e;
        }
        CHECK(ratio[0] < 0.05 && ratio[1] > 0.1, "Rossler: the sub-octave of the turn is %.3f of it at c 2.5, %.2f at c 3.6 (period doubled)", ratio[0], ratio[1]);
        double like[2];
        for (int k = 0; k < 2; ++k)
        {
            Engine* e = fresh(plain(kLorenz, 1.0, k ? 1.0 : 0.0));
            std::vector<float> L, R;
            play(*e, 57, (int)(1.5 * kSr), L, R);
            like[k] = periodLikeness(L, (size_t)(0.5 * kSr), f, 100);
            delete e;
        }
        CHECK(like[1] > 0.97 && like[0] < 0.8, "Lorenz in chaos: one period is like the next %.2f with SYNC 0, %.3f with SYNC 100", like[0], like[1]);
        Settings s = plain(kRossler, 0.5, 0.5); s.width = 1.0;
        Engine* e = fresh(s);
        std::vector<float> L, R;
        play(*e, 57, (int)(0.8 * kSr), L, R);
        double sab = 0, saa = 0, sbb = 0;
        for (size_t i = (size_t)(0.3 * kSr); i < L.size(); ++i) { sab += L[i] * R[i]; saa += L[i] * L[i]; sbb += R[i] * R[i]; }
        CHECK(std::fabs(sab / std::sqrt(saa * sbb)) < 0.7, "WIDTH 100: the channels hear different directions (correlation %.2f)", sab / std::sqrt(saa * sbb));
        delete e;
    }
    printf("== release\n");
    {
        Engine* e = fresh(plain(kRossler, 0.3, 0.5));
        std::vector<float> L, R;
        play(*e, 57, (int)(0.5 * kSr), L, R, (int)(0.2 * kSr));
        std::vector<float> l(256), r(256);
        for (int i = 0; i < 100; ++i) e->process(l.data(), r.data(), 256, nullptr, 0);
        CHECK(e->voicesNow() == 0, "the voice frees itself after the release (%d on)", e->voicesNow());
        delete e;
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true; double worst = 0.0;
        for (int t = 0; t < 150; ++t)
        {
            Settings s;
            s.system = (int)(u(rng) * kNumSystems) % kNumSystems; s.chaos = u(rng); s.sync = u(rng); s.axis = u(rng); s.width = u(rng); s.drive = u(rng);
            s.cutoff = 30.0 * std::pow(600.0, u(rng)); s.reso = u(rng); s.keytrack = u(rng); s.envCut = u(rng) * 2 - 1; s.envChaos = u(rng) * 2 - 1;
            s.driftRate = 0.02 + u(rng) * 10; s.driftPitch = u(rng); s.driftCut = u(rng); s.driftChaos = u(rng);
            s.voiceMode = (int)(u(rng) * 3) % 3; s.glide = u(rng) * 300; s.octave = (int)(u(rng) * 7) - 3; s.fine = u(rng) * 200 - 100;
            Engine* e = fresh(s);
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 150; ++b)
            {
                synth::Midi ev[2] = { { 0, 0x90, (uint8_t)(20 + (b * 11) % 100), 110 }, { 80, 0x80, (uint8_t)(20 + ((b + 4) * 11) % 100), 0 } };
                e->process(L.data(), R.data(), 256, ev, (b % 7) == 0 ? 2 : 0);
                for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); worst = std::max(worst, (double)std::fabs(L[i])); }
            }
            delete e;
        }
        CHECK(finite && worst < 10.0, "150 random patches stay finite (loudest %.2f before the limiter)", worst);
    }
    printf("== CPU\n");
    {
        Settings s; s.system = kLorenz; s.chaos = 0.8;
        Engine* e = fresh(s);
        std::vector<float> L(256), R(256);
        for (int n = 0; n < 8; ++n) { synth::Midi on { 0, 0x90, (uint8_t)(60 + n * 4), 100 }; e->process(L.data(), R.data(), 256, &on, 1); }
        const int blocks = (int)(4.0 * kSr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) e->process(L.data(), R.data(), 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs / 4.0 < 0.2, "8 voices: %.1f%% of one core", 100.0 * secs / 4.0);
        delete e;
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
