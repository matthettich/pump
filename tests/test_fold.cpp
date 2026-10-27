// pump FOLD: the folding cell stays inside +-1 and is smooth at EDGE 0; with no folding the principal is a clean sine
// at the note; FOLD adds harmonics step by step and SYMMETRY brings in the even ones; folded hard at a high note
// almost nothing lands between the harmonics (the oversampling works); FM adds sidebands at the modulator's spacing;
// a struck gate rings and darkens as it dies, longer with a slower RESPONSE; CYCLE repeats at rise + fall; the voice
// frees itself; random settings stay finite. CPU.
#include "../plugins/fold/FoldEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::fold;
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
// a held, open tone: function sustaining, gate wide open
static Settings open(double fold)
{
    Settings s; s.fold = fold; s.funcMode = kFuncSustain; s.rise = 0.001; s.fall = 0.05; s.funcGate = 1.0; s.gateMode = kGateVca; s.spread = 0.0; s.velocity = 0.0;
    s.edge = 0.0; s.symmetry = 0.0;
    return s;
}
static Engine* fresh(const Settings& s) { auto* e = new Engine(); e->prepare(kSr, 256); e->setSettings(s); return e; }
// the share of the power that is in the harmonics of f (the rest: aliases and noise)
static double harmonicShare(const std::vector<float>& x, size_t a, size_t n, double f)
{
    double hp = 0.0;
    for (int k = 1; k * f < kSr * 0.5; ++k) { const double h = harm(x, a, n, k * f); hp += 0.5 * h * h; }
    // the Hann window: harm() is amplitude-correct for a tone; the plain power of the windowed signal needs the same scale
    double tp = 0.0;
    for (size_t i = a; i < a + n; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i - a) / n); tp += x[i] * x[i] * w * w; }
    tp /= (0.375 * n);
    return hp / std::max(1e-20, tp);
}

int main()
{
    printf("== the folding cell\n");
    {
        bool inside = true; double maxJump = 0.0, prev = foldCell(-20.0, 0.0, 6);
        for (double u = -20.0; u <= 20.0; u += 0.001)
        {
            for (double e : { 0.0, 0.5, 1.0 }) { const double y = foldCell(u, e, 6); inside = inside && std::fabs(y) <= 1.0 + 1e-9; }
            const double y = foldCell(u, 0.0, 6); maxJump = std::max(maxJump, std::fabs(y - prev)); prev = y;
        }
        CHECK(inside && maxJump < 0.01, "stays inside +-1 for any input and edge; round folds are smooth (largest step %.4f)", maxJump);
    }
    printf("== folding\n");
    {
        const double f = synth::midiToHz(57);
        double share[3];
        const double amt[3] = { 0.0, 0.3, 0.7 };
        for (int k = 0; k < 3; ++k)
        {
            Engine* e = fresh(open(amt[k]));
            const auto y = play(*e, 57, (int)(0.8 * kSr));
            const double h1 = harm(y, 9600, 16384, f);
            double up = 0.0, num = 0.0, den = h1 * h1; for (int j = 2; j <= 40; ++j) { const double h = harm(y, 9600, 16384, j * f); up += h * h; num += j * h * h; den += h * h; }
            share[k] = k == 0 ? std::sqrt(up) / h1 : (num + h1 * h1) / den;   // 0: the overtones; else the centroid (in harmonics)
            if (k == 0) CHECK(share[0] < 0.01 , "no folding: a clean sine at %.1f Hz (overtones %.4f of it)", f, share[0]);
            delete e;
        }
        CHECK(share[1] > 1.5 && share[2] > share[1] * 1.3, "FOLD brightens it: the centre of the spectrum at harmonic %.1f at 30 %%, %.1f at 70 %%", share[1], share[2]);
        double even[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings s = open(0.4); s.symmetry = k ? 0.5 : 0.0;
            Engine* e = fresh(s);
            const auto y = play(*e, 57, (int)(0.8 * kSr));
            even[k] = (harm(y, 9600, 16384, 2 * f) + harm(y, 9600, 16384, 4 * f)) / harm(y, 9600, 16384, f);
            delete e;
        }
        CHECK(even[0] < 0.01 && even[1] > 0.1, "SYMMETRY brings in the even harmonics: %.4f at 0, %.2f at 50 %%", even[0], even[1]);
        Settings s = open(1.0); s.edge = 1.0; s.stages = 6;
        Engine* e = fresh(s);
        const auto y = play(*e, 96, (int)(0.8 * kSr));
        const double sh = harmonicShare(y, 9600, 16384, synth::midiToHz(96));
        CHECK(sh > 0.99, "folded hard and sharp at C7: %.2f %% of the power on the harmonics (aliases %.1f dB down)", 100.0 * sh, 10.0 * std::log10(std::max(1e-12, 1.0 - sh)));
        delete e;
    }
    printf("== FM\n");
    {
        Settings s = open(0.0); s.fm = 0.3; s.modRatio = 1.5;
        Engine* e = fresh(s);
        const auto y = play(*e, 57, (int)(0.8 * kSr));
        const double f = synth::midiToHz(57);
        const double c = harm(y, 9600, 16384, f), sb = harm(y, 9600, 16384, f * 2.5) + harm(y, 9600, 16384, f * 0.5);
        CHECK(sb > 0.2 * c, "FM at ratio 1.5: sidebands at 0.5 f and 2.5 f, %.2f of the carrier", sb / c);
        delete e;
    }
    printf("== the low-pass gate\n");
    {
        const double f = synth::midiToHz(57);
        double tail[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings s; s.fold = 0.5; s.funcMode = kFuncTrig; s.rise = 0.0003; s.fall = 0.01; s.vactrol = k ? 0.9 : 0.2; s.spread = 0.0; s.velocity = 0.0;
            Engine* e = fresh(s);
            const auto y = play(*e, 57, (int)(1.2 * kSr));
            const double early = rms(y, (size_t)(0.01 * kSr), (size_t)(0.04 * kSr));
            tail[k] = rms(y, (size_t)(0.25 * kSr), (size_t)(0.3 * kSr)) / early;
            if (k == 0)
            {
                const double b0 = harm(y, 240, 1536, 3 * f) / harm(y, 240, 1536, f), b1 = harm(y, (size_t)(0.08 * kSr), 1536, 3 * f) / std::max(1e-9, harm(y, (size_t)(0.08 * kSr), 1536, f));
                CHECK(b1 < 0.6 * b0, "a strike rings and darkens: harmonic 3 / 1 goes from %.2f to %.2f", b0, b1);
            }
            delete e;
        }
        CHECK(tail[1] > 3.0 * tail[0], "a slower RESPONSE rings longer: %.1f dB vs %.1f dB at a quarter second", 20 * std::log10(tail[0] + 1e-9), 20 * std::log10(tail[1] + 1e-9));
        Settings s; s.funcMode = kFuncCycle; s.rise = 0.05; s.fall = 0.15; s.vactrol = 0.0; s.spread = 0.0; s.velocity = 0.0;
        Engine* e = fresh(s);
        const auto y = play(*e, 69, (int)(2.0 * kSr));
        // the envelope of the output, at 1 ms steps; its strongest period
        std::vector<double> env; for (size_t i = 0; i + 48 < y.size(); i += 48) env.push_back(rms(y, i, i + 48));
        double best = 0, bp = 0;
        for (int lag = 100; lag < 300; ++lag) { double c = 0; for (size_t i = 500; i + lag < env.size(); ++i) c += (env[i] - 0.05) * (env[i + lag] - 0.05); if (c > best) { best = c; bp = lag; } }
        CHECK(std::fabs(bp - 200.0) < 6.0, "CYCLE repeats every %.0f ms (rise 50 + fall 150)", bp);
        delete e;
    }
    printf("== release\n");
    {
        Settings s; s.funcMode = kFuncSustain; s.vactrol = 0.5;
        Engine* e = fresh(s);
        play(*e, 57, (int)(0.4 * kSr), (int)(0.1 * kSr));
        std::vector<float> L(256), R(256);
        for (int i = 0; i < 400; ++i) e->process(L.data(), R.data(), 256, nullptr, 0);
        CHECK(e->voicesNow() == 0, "the voice frees itself (%d on)", e->voicesNow());
        delete e;
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(5);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true; double worst = 0.0;
        for (int t = 0; t < 150; ++t)
        {
            Settings s;
            s.shape = u(rng); s.modWave = (int)(u(rng) * 4) % 4; s.modRatio = 0.125 * std::pow(128.0, u(rng)); s.modDetune = u(rng) * 40 - 20;
            s.fm = u(rng); s.am = u(rng); s.modFold = u(rng); s.fold = u(rng); s.symmetry = u(rng) * 2 - 1; s.edge = u(rng); s.stages = 1 + (int)(u(rng) * 6) % 6;
            s.gateMode = (int)(u(rng) * 3) % 3; s.gateOpen = u(rng); s.vactrol = u(rng); s.gateReso = u(rng);
            s.funcMode = (int)(u(rng) * 3) % 3; s.rise = 0.0002 * std::pow(25000.0, u(rng)); s.fall = 0.001 * std::pow(10000.0, u(rng));
            s.funcGate = u(rng); s.funcFold = u(rng) * 2 - 1; s.funcFm = u(rng) * 2 - 1; s.funcPitch = u(rng) * 48 - 24;
            s.randRate = 0.05 * std::pow(800.0, u(rng)); s.randSmooth = u(rng); s.randFold = u(rng); s.randFm = u(rng); s.randPitch = u(rng) * 12;
            s.voiceMode = (int)(u(rng) * 3) % 3; s.glide = u(rng) * 300; s.octave = (int)(u(rng) * 7) - 3; s.fine = u(rng) * 200 - 100;
            Engine* e = fresh(s);
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 120; ++b)
            {
                synth::Midi ev[2] = { { 0, 0x90, (uint8_t)(20 + (b * 13) % 100), 110 }, { 90, 0x80, (uint8_t)(20 + ((b + 5) * 13) % 100), 0 } };
                e->process(L.data(), R.data(), 256, ev, (b % 6) == 0 ? 2 : 0);
                for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); worst = std::max(worst, (double)std::fabs(L[i])); }
            }
            delete e;
        }
        CHECK(finite && worst < 10.0, "150 random patches stay finite (loudest %.2f before the limiter)", worst);
    }
    printf("== CPU\n");
    {
        Settings s = open(0.6); s.fm = 0.3; s.edge = 0.5;
        Engine* e = fresh(s);
        std::vector<float> L(256), R(256);
        for (int n = 0; n < 8; ++n) { synth::Midi on { 0, 0x90, (uint8_t)(50 + n * 3), 100 }; e->process(L.data(), R.data(), 256, &on, 1); }
        const int blocks = (int)(4.0 * kSr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) e->process(L.data(), R.data(), 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs / 4.0 < 0.25, "8 voices at 4x: %.1f%% of one core", 100.0 * secs / 4.0);
        delete e;
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
