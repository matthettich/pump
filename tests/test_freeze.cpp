// pump FREEZE: a sine, cut into spectra and frozen, comes back at its own level and pitch, and at a note's pitch an
// octave up; key tracking 0 keeps it where it was; a frozen voice is steady (its level doesn't wander) and a moving one
// follows the sound; LOW and HIGH cut; WIDTH decorrelates the channels; the voice frees itself; every built-in sound
// is made and plays; random settings stay finite. CPU.
#include "../plugins/freeze/FreezeSources.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::freeze;
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
    for (double f = lo; f <= hi; f += 0.5) { const double h = harm(x, a, n, f); if (h > best) { best = h; bf = f; } }
    return bf;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (double)(b - a)); }
static Settings plain()
{
    Settings s; s.spray = 0.0; s.grains = 1; s.blur = 0.0; s.shimmer = 0.0; s.width = 0.0; s.phase = 1.0; s.attack = 0.005; s.release = 0.05; s.velocity = 0.0; s.low = 20.0; s.high = 20000.0;
    return s;
}
static std::vector<float> sine(double f, double a, double secs) { std::vector<float> y((size_t)(kSr * secs)); for (size_t i = 0; i < y.size(); ++i) y[i] = (float)(a * std::sin(kTwoPi * f * i / kSr)); return y; }

int main()
{
    printf("== a frozen sine\n");
    const auto S = makeSpectra(sine(440.0, 0.5, 3.0), kSr, "sine", 69);
    {
        Settings s = plain(); s.position = 0.5;
        Engine e; e.prepare(kSr, 256); e.setSettings(s); e.setSource(S.get());
        std::vector<float> L, R;
        play(e, 69, (int)(1.5 * kSr), L, R);
        const double p = peakHz(L, (size_t)(0.3 * kSr), 32768, 420, 460), lv = rms(L, (size_t)(0.3 * kSr), (size_t)(1.4 * kSr));
        CHECK(std::fabs(p - 440.0) < 1.0 && std::fabs(20.0 * std::log10(lv / (0.5 / std::sqrt(2.0)))) < 1.5, "plays back at %.1f Hz and %.1f dB of the original's level", p, 20.0 * std::log10(lv / (0.5 / std::sqrt(2.0))));
        {
            Settings r = s; r.phase = 0.0;
            Engine er; er.prepare(kSr, 256); er.setSettings(r); er.setSource(S.get());
            std::vector<float> A, B;
            play(er, 69, (int)(1.5 * kSr), A, B);
            const double lr = rms(A, (size_t)(0.3 * kSr), (size_t)(1.4 * kSr));
            CHECK(std::fabs(20.0 * std::log10(lr / (0.5 / std::sqrt(2.0)))) < 4.0, "with random phases (PHASE 0) its level is %.1f dB of the original's", 20.0 * std::log10(lr / (0.5 / std::sqrt(2.0))));
        }
        double lo = 1e9, hi = 0; for (int k = 0; k < 20; ++k) { const double r = rms(L, (size_t)((0.3 + 0.05 * k) * kSr), (size_t)((0.35 + 0.05 * k) * kSr)); lo = std::min(lo, r); hi = std::max(hi, r); }
        CHECK(20.0 * std::log10(hi / lo) < 3.0, "frozen, its level holds steady (within %.1f dB over a second)", 20.0 * std::log10(hi / lo));
    }
    {
        Settings s = plain(); s.position = 0.5;
        Engine e; e.prepare(kSr, 256); e.setSettings(s); e.setSource(S.get());
        std::vector<float> L, R;
        play(e, 81, (int)(1.2 * kSr), L, R);
        const double p = peakHz(L, (size_t)(0.3 * kSr), 32768, 860, 900);
        CHECK(std::fabs(p - 880.0) < 1.5, "a note an octave up plays %.1f Hz", p);
        s.track = 0.0; e.setSettings(s);
        Engine e2; e2.prepare(kSr, 256); e2.setSettings(s); e2.setSource(S.get());
        play(e2, 81, (int)(1.2 * kSr), L, R);
        const double q = peakHz(L, (size_t)(0.3 * kSr), 32768, 420, 460);
        CHECK(std::fabs(q - 440.0) < 1.0, "with no key tracking it stays at %.1f Hz", q);
        Settings t = plain(); t.position = 0.5; t.low = 2000.0;
        Engine e3; e3.prepare(kSr, 256); e3.setSettings(t); e3.setSource(S.get());
        play(e3, 69, (int)(1.0 * kSr), L, R);
        CHECK(rms(L, (size_t)(0.3 * kSr), (size_t)(0.9 * kSr)) < 0.02 * 0.354, "LOW at 2 kHz takes a 440 Hz sine out (%.1f dB)", 20.0 * std::log10(rms(L, (size_t)(0.3 * kSr), (size_t)(0.9 * kSr)) / 0.354 + 1e-9));
    }
    printf("== moving, width, release\n");
    {
        // a sound that is a 300 Hz sine for its first half and 600 Hz for its second; SPEED 100 plays through it
        std::vector<float> y((size_t)(2.0 * kSr));
        for (size_t i = 0; i < y.size(); ++i) y[i] = (float)(0.5 * std::sin(kTwoPi * (i < y.size() / 2 ? 300.0 : 600.0) * i / kSr));
        const auto T = makeSpectra(y, kSr, "two", 60);
        Settings s = plain(); s.position = 0.1; s.speed = 1.0; s.track = 0.0;
        Engine e; e.prepare(kSr, 256); e.setSettings(s); e.setSource(T.get());
        std::vector<float> L, R;
        play(e, 60, (int)(1.6 * kSr), L, R);
        const bool first = harm(L, (size_t)(0.3 * kSr), 8192, 300.0) > 3.0 * harm(L, (size_t)(0.3 * kSr), 8192, 600.0);
        const bool second = harm(L, (size_t)(1.3 * kSr), 8192, 600.0) > 3.0 * harm(L, (size_t)(1.3 * kSr), 8192, 300.0);
        CHECK(first && second, "SPEED 100 plays through the sound: 300 Hz first, then 600 Hz");
        Settings w = plain(); w.position = 0.5; w.phase = 0.0; w.width = 1.0; w.grains = 4; w.spray = 0.2;
        const auto N = makeSpectra([] { std::vector<float> z((size_t)(2.0 * kSr)); fx::Rng r(5u); for (auto& v : z) v = (float)(0.3 * r.bi()); return z; }(), kSr, "noise", 60);
        Engine e2; e2.prepare(kSr, 256); e2.setSettings(w); e2.setSource(N.get());
        play(e2, 60, (int)(1.0 * kSr), L, R);
        double sab = 0, saa = 0, sbb = 0;
        for (size_t i = (size_t)(0.3 * kSr); i < L.size(); ++i) { sab += L[i] * R[i]; saa += L[i] * L[i]; sbb += R[i] * R[i]; }
        CHECK(std::fabs(sab / std::sqrt(saa * sbb)) < 0.3, "WIDTH 100: the channels are decorrelated (%.2f)", sab / std::sqrt(saa * sbb));
        Engine e3; e3.prepare(kSr, 256); e3.setSettings(plain()); e3.setSource(S.get());
        play(e3, 69, (int)(0.5 * kSr), L, R, (int)(0.2 * kSr));
        std::vector<float> l(256), r(256);
        for (int i = 0; i < 100; ++i) e3.process(l.data(), r.data(), 256, nullptr, 0);
        CHECK(e3.voicesNow() == 0, "the voice frees itself (%d on)", e3.voicesNow());
    }
    printf("== the built-in sounds\n");
    {
        const auto t0 = std::chrono::steady_clock::now();
        Builtins::get().start(); Builtins::get().waitReady();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool all = true;
        for (int id = 1; id < kNumSources; ++id)
        {
            const Spectra* B = Builtins::get().source(id);
            Engine e; e.prepare(kSr, 256); Settings s; e.setSettings(s); e.setSource(B);
            std::vector<float> L, R;
            play(e, 60, (int)(1.0 * kSr), L, R);
            const double lv = rms(L, (size_t)(0.5 * kSr), L.size());
            all = all && B && B->frames > 50 && lv > 0.003 && std::isfinite(lv);
            printf("    %-14s %3d frames, %.1f dB\n", kSourceNames[id], B ? B->frames : 0, 20.0 * std::log10(lv + 1e-12));
        }
        CHECK(all, "all seven are made (in %.1f s) and play", secs);
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(21);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true; double worst = 0.0;
        for (int t = 0; t < 60; ++t)
        {
            Settings s;
            s.position = u(rng); s.speed = u(rng) * 2 - 1; s.spray = u(rng); s.grains = 1 + (int)(u(rng) * 8) % 8; s.blur = u(rng); s.track = u(rng); s.stretch = u(rng) * 2 - 1;
            s.tilt = u(rng) * 2 - 1; s.shimmer = u(rng); s.low = 20 * std::pow(100.0, u(rng)); s.high = 500 * std::pow(40.0, u(rng)); s.phase = u(rng); s.width = u(rng);
            s.voiceMode = (int)(u(rng) * 3) % 3; s.glide = u(rng) * 300; s.octave = (int)(u(rng) * 7) - 3;
            Engine e; e.prepare(kSr, 256); e.setSettings(s); e.setSource(Builtins::get().source(1 + t % 7));
            std::vector<float> L(256), R(256);
            for (int b = 0; b < 100; ++b)
            {
                synth::Midi ev[2] = { { 0, 0x90, (uint8_t)(24 + (b * 13) % 84), 110 }, { 90, 0x80, (uint8_t)(24 + ((b + 5) * 13) % 84), 0 } };
                e.process(L.data(), R.data(), 256, ev, (b % 7) == 0 ? 2 : 0);
                for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); worst = std::max(worst, (double)std::fabs(L[i])); }
            }
        }
        CHECK(finite && worst < 10.0, "60 random patches stay finite (loudest %.2f before the limiter)", worst);
    }
    printf("== CPU\n");
    {
        Settings s; s.grains = 8;
        Engine e; e.prepare(kSr, 256); e.setSettings(s); e.setSource(Builtins::get().source(1));
        std::vector<float> L(256), R(256);
        for (int n = 0; n < 8; ++n) { synth::Midi on { 0, 0x90, (uint8_t)(50 + n * 3), 100 }; e.process(L.data(), R.data(), 256, &on, 1); }
        const int blocks = (int)(4.0 * kSr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) e.process(L.data(), R.data(), 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs / 4.0 < 0.3, "8 voices of 8 grains: %.1f%% of one core", 100.0 * secs / 4.0);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
