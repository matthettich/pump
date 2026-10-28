// pump STRIKE: every engine sounds at its pitch (the string's loop, the circuit's resonator, the modal membrane's first
// mode, the FM carrier, the west-coast triangle) and dies in about its DECAY; the engines are about as loud as each
// other; a bend falls to the pitch; snare wires rattle with the body; wood dies faster than metal; the closed hat
// chokes the open one; a re-hit doesn't click; the pads answer C4 up in whole steps and the General MIDI notes; random settings stay finite.
// CPU.
#include "../plugins/strike/StrikeEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::strike;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static const double kSr = 48000.0;

static std::vector<float> run(Engine& e, int n, std::vector<std::pair<int, int>> hits)   // (sample, pad)
{
    std::vector<float> L((size_t)n + 256), R((size_t)n + 256);
    for (int o = 0; o < n; o += 256)
    {
        for (auto& h : hits) if (h.first >= o && h.first < o + 256) e.hit(h.second, 1.0);
        e.process(L.data() + o, R.data() + o, 256, nullptr, 0);
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
static double peakHz(const std::vector<float>& x, size_t a, size_t n, double lo, double hi, double step = 0.5)
{
    double best = 0, bf = lo;
    for (double f = lo; f <= hi; f += step) { const double h = harm(x, a, n, f); if (h > best) { best = h; bf = f; } }
    return bf;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (double)(b - a)); }
static Settings one(int engine, double pitch, double decay)
{
    Settings s;
    for (auto& p : s.pad) { p.level = -100.0; }
    PadSettings& p = s.pad[0];
    p.engine = engine; p.pitch = pitch; p.decay = decay; p.tone = 0.5; p.a = 0.2; p.b = 0.3; p.bend = 0.0; p.noise = 0.0; p.level = 0.0; p.pan = 0.0;
    s.velocity = 0.0; s.drive = 0.0;
    return s;
}
static Engine* fresh(const Settings& s) { auto* e = new Engine(); e->prepare(kSr, 256); e->setSettings(s); return e; }

int main()
{
    printf("== pitch and decay\n");
    {
        struct T { int eng; double a, b; const char* what; };
        const T ts[] = { { kModal, 0.0, 0.8, "modal membrane (first mode)" }, { kString, 0.0, 0.5, "string" }, { kCircuit, 0.0, 0.0, "circuit" },
                         { kFm, 0.2, 0.0, "FM (carrier)" }, { kWest, 0.0, 0.0, "west" } };
        for (const T& t : ts)
        {
            Settings s = one(t.eng, 57, 0.8); s.pad[0].a = t.a; s.pad[0].b = t.b; s.pad[0].tone = 0.6;
            Engine* e = fresh(s);
            const auto y = run(*e, (int)(1.2 * kSr), { { 0, 0 } });
            const double f = synth::midiToHz(57);
            const double p = peakHz(y, 1200, 16384, f * 0.9, f * 1.1, 0.25);
            const double a0 = harm(y, 1200, 4096, p), a1 = harm(y, (size_t)(0.4 * kSr), 4096, p);
            const double db = 20.0 * std::log10(a1 / std::max(1e-12, a0)), want = -60.0 * (0.4 * kSr - 1200) / kSr / 0.8;
            CHECK(std::fabs(p - f) < (t.eng == kWest ? 2.5 : 1.5) && (t.eng == kWest || std::fabs(db - want) < 9.0), "%s: rings at %.2f Hz (%.2f), down %.1f dB at 0.4 s (DECAY 0.8 s: about %.1f)", t.what, p, f, db, want);
            delete e;
        }
        // additive: its first partial at the pitch (bell ratios)
        Settings s = one(kAdditive, 69, 1.5); s.pad[0].a = 0.0;
        Engine* e = fresh(s);
        const auto y = run(*e, (int)(0.8 * kSr), { { 0, 0 } });
        const double p = peakHz(y, 1200, 16384, 430, 450, 0.25);
        CHECK(std::fabs(p - 440.0) < 1.0, "additive (bell): its first partial at %.2f Hz", p);
        delete e;
    }
    printf("== levels\n");
    {
        double lv[kNumEngines], lo = 1e9, hi = -1e9;
        for (int en = 0; en < kNumEngines; ++en)
        {
            Settings s = one(en, en == kAdditive ? 84 : 50, 0.4); s.pad[0].a = 0.5; s.pad[0].b = 0.4;
            Engine* e = fresh(s);
            const auto y = run(*e, (int)(0.5 * kSr), { { 0, 0 } });
            lv[en] = 20.0 * std::log10(rms(y, 0, (size_t)(0.25 * kSr)) + 1e-12);
            lo = std::min(lo, lv[en]); hi = std::max(hi, lv[en]);
            printf("    %-9s %.1f dB\n", kEngineNames[en], lv[en]);
            delete e;
        }
        CHECK(hi - lo < 8.0, "the engines are within %.1f dB of each other (a 0.4 s hit, its first quarter second)", hi - lo);
    }
    printf("== bend, wires, material\n");
    {
        Settings s = one(kCircuit, 40, 0.8); s.pad[0].bend = 12.0; s.pad[0].bendTime = 0.03;
        Engine* e = fresh(s);
        const auto y = run(*e, (int)(1.0 * kSr), { { 0, 0 } });
        const double f = synth::midiToHz(40);
        const double early = peakHz(y, 0, 1024, f, f * 2.2, 1.0), late = peakHz(y, (size_t)(0.25 * kSr), 16384, f * 0.9, f * 1.1, 0.25);
        CHECK(early > f * 1.3 && std::fabs(late - f) < 1.0, "a 12-semitone bend: %.0f Hz at the start, %.1f Hz (%.1f) once it settles", early, late, f);
        delete e;
        double hiE[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings t = one(kModal, 55, 0.3); t.pad[0].noise = k ? 0.8 : 0.0;
            e = fresh(t);
            const auto z = run(*e, (int)(0.4 * kSr), { { 0, 0 } });
            hiE[k] = harm(z, (size_t)(0.05 * kSr), 2048, 5000.0) + harm(z, (size_t)(0.05 * kSr), 2048, 6000.0);
            delete e;
        }
        CHECK(hiE[1] > 20.0 * hiE[0] + 1e-6, "snare wires add a rattle near 5 kHz after the hit (%.2g vs %.2g)", hiE[1], hiE[0]);
        double tail[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings t = one(kModal, 57, 1.5); t.pad[0].a = 0.6; t.pad[0].b = k ? 1.0 : 0.0; t.pad[0].tone = 1.0;
            e = fresh(t);
            const auto z = run(*e, (int)(1.0 * kSr), { { 0, 0 } });
            const double f2 = synth::midiToHz(57) * 2.756;
            tail[k] = harm(z, (size_t)(0.5 * kSr), 4096, f2) / std::max(1e-12, harm(z, 600, 4096, f2));
            delete e;
        }
        CHECK(tail[0] < 0.02 && tail[1] > 0.07, "a bar's second mode after half a second: wood %.3f, metal %.2f of its start", tail[0], tail[1]);
    }
    printf("== choke, re-hits, notes\n");
    {
        Settings s; s.choke = true; s.velocity = 0.0;
        for (auto& p : s.pad) p.level = -100.0;
        s.pad[6].level = 0.0; s.pad[6].decay = 1.0;
        Engine* e = fresh(s);
        const auto y = run(*e, (int)(0.6 * kSr), { { 0, 6 }, { (int)(0.2 * kSr), 5 } });
        CHECK(rms(y, (size_t)(0.25 * kSr), (size_t)(0.3 * kSr)) < 0.02 * rms(y, (size_t)(0.1 * kSr), (size_t)(0.15 * kSr)), "the closed hat chokes the open one");
        delete e;
        Settings t = one(kModal, 45, 1.0);
        e = fresh(t);
        const auto z = run(*e, (int)(0.5 * kSr), { { 0, 0 }, { (int)(0.1 * kSr), 0 } });
        double jump = 0.0; for (size_t i = (size_t)(0.1 * kSr) - 256; i < (size_t)(0.1 * kSr) + 512; ++i) jump = std::max(jump, (double)std::fabs(z[i] - z[i - 1]));
        double typ = 0.0; for (size_t i = 1; i < 2000; ++i) typ = std::max(typ, (double)std::fabs(z[i] - z[i - 1]));
        CHECK(jump < 1.5 * typ, "a re-hit fades the old voice out (largest step %.3f; the first hit's own attack %.3f)", jump, typ);
        delete e;
        Settings u; u.velocity = 0.0;
        e = fresh(u);
        bool all = true;
        for (int p = 0; p < kPads; ++p)
        {
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 40; ++b) e->process(L.data(), R.data(), 256, nullptr, 0);
            synth::Midi m { 0, 0x90, (uint8_t)kPadInfo[p].note, 100 };
            e->process(L.data(), R.data(), 256, &m, 1);
            all = all && e->padLevel(p) > 0.0f;
        }
        CHECK(all, "every pad answers its note");
        delete e;
    }
    printf("== notes\n");
    {
        // the pads in order a whole step apart from C4 (60); the General MIDI drum notes still play the same pads, and the
        // odd notes left of the old run up from 60 (61, 63, 65, 67) still play theirs
        bool order = true;
        for (int p = 0; p < kPads; ++p) order = order && kPadInfo[p].note == 60 + 2 * p;
        CHECK(order, "pads on C4 D4 E4 F#4 G#4 A#4 C5 D5 (60-74 in whole steps)");
        auto plays = [](int note) {   // the one pad a note plays, -1 for none, -2 for more than one
            Settings s; Engine* e = fresh(s);
            const uint32_t before = e->lastHit();
            std::vector<float> L(256), R(256);
            synth::Midi m { 0, 0x90, (uint8_t)note, 110 };
            e->process(L.data(), R.data(), 256, &m, 1);
            const uint32_t after = e->lastHit();
            delete e;
            const uint32_t n = (after >> 8) - (before >> 8);
            return n == 0 ? -1 : n == 1 ? (int)(after & 0xFF) : -2;
        };
        static const int gm[kPads] = { 36, 38, 37, 43, 48, 42, 46, 49 };
        bool allNew = true, allGm = true;
        for (int p = 0; p < kPads; ++p) { allNew = allNew && plays(kPadInfo[p].note) == p; allGm = allGm && plays(gm[p]) == p; }
        CHECK(allNew, "each new note plays its pad, and only that pad");
        CHECK(allGm, "each General MIDI drum note (older patterns) still plays the same pad");
        CHECK(plays(61) == 1 && plays(63) == 3 && plays(65) == 5 && plays(67) == 7, "61, 63, 65, 67 (left of the old run up from 60) still play their pads");
        CHECK(plays(59) == -1 && plays(75) == -1 && plays(69) == -1, "other notes play nothing");
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(3);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true; double worst = 0.0;
        for (int t = 0; t < 100; ++t)
        {
            Settings s;
            for (auto& p : s.pad) { p.engine = (int)(u(rng) * kNumEngines) % kNumEngines; p.pitch = 12 + u(rng) * 96; p.decay = 0.01 + u(rng) * 6; p.tone = u(rng); p.a = u(rng); p.b = u(rng); p.bend = u(rng) * 48; p.bendTime = 0.002 + u(rng) * 0.5; p.noise = u(rng); p.level = -36 + u(rng) * 42; p.pan = u(rng) * 2 - 1; }
            s.drive = u(rng); s.velocity = u(rng);
            Engine* e = fresh(s);
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 100; ++b)
            {
                if (b % 3 == 0) e->hit((b / 3) % kPads, u(rng));
                e->process(L.data(), R.data(), 256, nullptr, 0);
                for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); worst = std::max(worst, (double)std::fabs(L[i])); }
            }
            delete e;
        }
        CHECK(finite && worst < 20.0, "100 random kits stay finite (loudest %.2f before the limiter)", worst);
    }
    printf("== CPU\n");
    {
        Settings s;
        for (auto& p : s.pad) { p.engine = kAdditive; p.a = 1.0; p.decay = 4.0; p.pitch = 70; }
        Engine* e = fresh(s);
        std::vector<float> L(256), R(256);
        for (int p = 0; p < kPads; ++p) { e->hit(p, 1.0); e->hit(p, 1.0); }
        const int blocks = (int)(2.0 * kSr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) { if (b % 20 == 0) for (int p = 0; p < kPads; ++p) e->hit(p, 1.0); e->process(L.data(), R.data(), 256, nullptr, 0); }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs / 2.0 < 0.25, "16 dense additive cymbals at once: %.1f%% of one core", 100.0 * secs / 2.0);
        delete e;
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
