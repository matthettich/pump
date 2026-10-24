// pump GATE: with a playing transport at 120 BPM from ppq 0 an alternating pattern at 1/16 alternates open/closed
// every 125 ms (>= 30 dB apart at depth 100 %); starting at ppq 3.25 lands on the right step and the next edge falls
// on its exact sample inside the block; swing delays the second step of each pair by swing x a step (sample-exact);
// gate length closes each step on time; a stopped transport (or none) free-runs at the host tempo (120 BPM when
// there is none); random host block sizes give exactly what one continuous run gives; a constant input never jumps
// by more than a small bound per sample (any attack/release, every mode, with settings moving); stereo offset
// delays the right channel by offset x a step; filter mode removes the highs in closed steps and keeps the lows;
// loops and jumps resync on the next block; mix 0 nulls; random settings and transports (including absurd ones)
// stay finite and bounded. 44.1, 48 and 96 kHz where timing matters. Also prints CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/gate/GateEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::gate;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}
// a clean, hard gate: full length, no attack/release/smooth, full depth, volume mode
static Settings hard(const char* pat)
{
    Settings s;
    s.length = 100; s.attack = 0; s.release = 0; s.smooth = 0; s.depth = 100; s.swing = 0; s.mode = kVolume;
    std::vector<float> lv;
    for (const char* c = pat; *c; ++c) lv.push_back(*c == 'x' ? 100.0f : 0.0f);
    s.steps = (int)lv.size();
    for (int k = 0; k < kMaxSteps; ++k) s.level[k] = lv[(size_t)k % lv.size()];
    return s;
}
// runs the engine like a host: every block gets the transport at its start (ppq from the block's first sample)
struct Host {
    double sr, bpm, ppq0;
    bool playing = true, valid = true;
};
static void run(Engine& e, const Host& h, std::vector<float>& L, std::vector<float>& R, int block = 512, size_t from = 0, double ppqAtFrom = -1e300)
{
    const size_t n = L.size();
    const double base = ppqAtFrom > -1e299 ? ppqAtFrom : h.ppq0;
    for (size_t off = from; off < n; off += (size_t)block)
    {
        const int b = (int)std::min<size_t>((size_t)block, n - off);
        Transport t;
        t.bpm = h.bpm; t.playing = h.playing; t.valid = h.valid;
        t.ppq = base + (double)(off - from) * h.bpm / (60.0 * h.sr);
        e.setTransport(t);
        e.process(L.data() + off, R.data() + off, b);
    }
}
static double rmsDb(const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)x[i] * x[i];
    return 10 * std::log10(e / (double)(to - from) + 1e-30);
}
// the first index >= from where |x| rises above thr
static long firstAbove(const std::vector<float>& x, size_t from, double thr)
{
    for (size_t i = from; i < x.size(); ++i) if (std::fabs(x[i]) > thr) return (long)i;
    return -1;
}
static long firstBelow(const std::vector<float>& x, size_t from, double thr)
{
    for (size_t i = from; i < x.size(); ++i) if (std::fabs(x[i]) < thr) return (long)i;
    return -1;
}
// amplitude of a sine at f over [from, to) (integer number of cycles: exact)
static double toneAmp(const std::vector<float>& x, size_t from, size_t to, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) { const double ph = kTwoPi * f * (double)i / sr; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); }
    return 2.0 * std::sqrt(re * re + im * im) / (double)(to - from);
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const double stepS = 0.125 * sr;   // a 1/16 at 120 BPM, in samples
        // ---- alternating pattern from ppq 0: open / closed per 125 ms step, on the test mix
        {
            Engine e; e.setSettings(hard("x.")); e.prepare(sr, 512);
            const size_t n = (size_t)(2.0 * sr);
            std::vector<float> L(n), R(n);
            for (size_t i = 0; i < n; ++i) { L[i] = mix[(2 * i) % mix.size()]; R[i] = mix[(2 * i + 1) % mix.size()]; }
            run(e, { sr, 120.0, 0.0 }, L, R, 512);
            double worst = 1e9, openMin = 1e9, closedMax = -1e9;
            for (int k = 0; k + 1 < 16; k += 2)
            {
                // each step from 2 ms after its edge (the minimum ramp) to its end
                const size_t a0 = (size_t)std::ceil(k * stepS + 0.002 * sr), a1 = (size_t)std::floor((k + 1) * stepS);
                const size_t b0 = (size_t)std::ceil((k + 1) * stepS + 0.002 * sr), b1 = (size_t)std::floor((k + 2) * stepS);
                const double o = std::min(rmsDb(L, a0, a1), rmsDb(R, a0, a1)), c = std::max(rmsDb(L, b0, b1), rmsDb(R, b0, b1));
                openMin = std::min(openMin, o); closedMax = std::max(closedMax, c);
                worst = std::min(worst, o - c);
            }
            CHECK(worst >= 30.0, "alternating 1/16 at 120 BPM: open steps >= %.1f dB, closed <= %.1f dB (at least %.0f dB apart)", openMin, closedMax, std::min(worst, 999.0));
        }
        // ---- starting at ppq 3.25: step 13 plays, and the edge into step 14 lands on its sample mid-block
        {
            Settings s = hard("x.");
            for (int k = 0; k < kMaxSteps; ++k) s.level[k] = 10.0f + 5.0f * (float)(k % 16);
            s.steps = 16;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const size_t n = (size_t)(0.25 * sr);
            std::vector<float> L(n, 1.0f), R(n, 1.0f);
            run(e, { sr, 120.0, 3.25 }, L, R, 512);
            const double edge = (3.5 - 3.25) * 0.5 * sr;   // samples from the start to step 14
            const long expect = (long)std::ceil(edge - 1e-6);
            const double lv13 = L[(size_t)expect - 1];
            long at = -1;
            for (size_t i = (size_t)(0.01 * sr); i < n; ++i) if (std::fabs(L[i] - lv13) > 1e-5) { at = (long)i; break; }
            CHECK(std::fabs(lv13 - 0.75) < 1e-6 && at == expect && expect % 512 != 0,
                  "start at ppq 3.25: step 13 (level %.4f), the edge into step 14 at sample %ld (expected %ld, %ld into its block)", lv13, at, expect, expect % 512);
        }
        // ---- swing: the second step of each pair starts swing x a step late; gate length closes each step on time
        {
            for (double sw : { 33.0, 50.0, 75.0 })
            {
                Settings s = hard(".x");
                s.swing = sw;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const size_t n = (size_t)(0.6 * sr);
                std::vector<float> L(n, 1.0f), R(n, 1.0f);
                run(e, { sr, 120.0, 0.0 }, L, R, 333);
                long errMax = 0;
                for (int pair = 0; pair < 2; ++pair)
                {
                    const double on = (2 * pair + 1 + sw * 0.01) * stepS;
                    const long expect = (long)std::ceil(on - 1e-6);
                    const long got = firstAbove(L, (size_t)((2 * pair + 0.5) * stepS), 1e-9);   // (the last pair's tail has died away)
                    errMax = std::max(errMax, std::labs(got - expect));
                }
                CHECK(errMax == 0, "swing %.0f%%: the second step of each pair starts %.1f samples late, on its sample (max error %ld)", sw, sw * 0.01 * stepS, errMax);
            }
            Settings s = hard("x");
            s.length = 40;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const size_t n = (size_t)(0.5 * sr);
            std::vector<float> L(n, 1.0f), R(n, 1.0f);
            run(e, { sr, 120.0, 0.0 }, L, R, 256);
            long errMax = 0;
            for (int k = 1; k < 3; ++k)
            {
                const long expect = (long)std::ceil((k + 0.4) * stepS - 1e-6);
                const long got = firstBelow(L, (size_t)(k * stepS + 0.01 * sr), 0.99999);
                errMax = std::max(errMax, std::labs(got - expect));
            }
            CHECK(errMax == 0, "gate length 40%%: each step starts closing exactly 40%% of a step after its edge (max error %ld)", errMax);
        }
        // ---- stopped transport free-runs at the host tempo; no tempo at all = 120 BPM
        {
            for (double bpm : { 100.0, 0.0 })
            {
                Engine e; e.setSettings(hard(".x")); e.prepare(sr, 512);
                const size_t n = (size_t)(1.0 * sr);
                std::vector<float> L(n, 1.0f), R(n, 1.0f);
                Host h { sr, bpm, 57.3 };
                h.playing = false; h.valid = bpm > 0.0;   // stopped with a (stale) position, or nothing at all
                for (size_t off = 0; off < n; off += 512)
                {
                    Transport t; t.bpm = bpm; t.playing = false; t.valid = h.valid; t.ppq = 57.3;   // the position doesn't move
                    e.setTransport(t);
                    e.process(L.data() + off, R.data() + off, (int)std::min<size_t>(512, n - off));
                }
                const double tempo = bpm > 0.0 ? bpm : 120.0;
                const double st = 0.25 * 60.0 / tempo * sr;
                long errMax = 0;
                for (int pair = 0; pair < 2; ++pair)
                {
                    const long expect = (long)std::ceil((2 * pair + 1) * st - 1e-6);
                    errMax = std::max(errMax, std::labs(firstAbove(L, (size_t)((2 * pair + 0.5) * st), 1e-9) - expect));
                }
                CHECK(errMax == 0 && !e.hostLocked(), "stopped transport, host tempo %s: free-runs at %.0f BPM (1/16 = %.0f samples, onsets exact to %ld)",
                      bpm > 0.0 ? "100" : "none", tempo, st, errMax);
            }
        }
        // ---- random host block sizes give exactly what one continuous free run gives
        {
            Settings s; s.swing = 41; s.length = 63; s.attack = 3; s.release = 40; s.smooth = 30; s.mode = kBoth; s.resonance = 70; s.offset = 3;
            s.steps = 13; s.rate = kNote16T;
            for (int k = 0; k < kMaxSteps; ++k) s.level[k] = (float)((k * 37) % 101);
            const size_t n = (size_t)(1.5 * sr);
            std::vector<float> L0(n), R0(n);
            for (size_t i = 0; i < n; ++i) { L0[i] = mix[(2 * i) % mix.size()]; R0[i] = mix[(2 * i + 1) % mix.size()]; }
            std::vector<float> L1 = L0, R1 = R0, L2 = L0, R2 = R0;
            Engine a; a.setSettings(s); a.prepare(sr, 512);
            a.setTransport({ 128.0, 0.0, 4.0, false, false });
            a.process(L1.data(), R1.data(), (int)n);
            Engine b; b.setSettings(s); b.prepare(sr, 512);
            std::mt19937 rng(3);
            for (size_t off = 0; off < n;)
            {
                const int bs = 1 + (int)(rng() % 1500);
                const int m = (int)std::min<size_t>((size_t)bs, n - off);
                Transport t; t.bpm = 128.0; t.playing = true; t.valid = true; t.ppq = (double)off * 128.0 / (60.0 * sr);
                b.setTransport(t);
                b.process(L2.data() + off, R2.data() + off, m);
                off += (size_t)m;
            }
            double err = 0;
            for (size_t i = 0; i < n; ++i) err = std::max(err, (double)std::max(std::fabs(L1[i] - L2[i]), std::fabs(R1[i] - R2[i])));
            CHECK(err < 1e-5, "playing transport in random block sizes = one continuous run (max difference %.2g)", err);
        }
        // ---- stereo offset: the right channel is the left one delayed by offset x a step (no swing)
        {
            Settings s; s.length = 70; s.attack = 2; s.release = 30; s.smooth = 20; s.depth = 100; s.offset = 3; s.steps = 16;
            for (int k = 0; k < kMaxSteps; ++k) s.level[k] = (float)((k * 53 + 11) % 101);
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const size_t n = (size_t)(3.0 * sr);
            std::vector<float> L(n, 1.0f), R(n, 1.0f);
            run(e, { sr, 120.0, 0.0 }, L, R, 512);
            const size_t d = (size_t)std::lround(3 * stepS);
            double err = 0;
            const size_t from = (size_t)(16 * stepS) + d;   // after a full pattern
            const bool exact = std::fabs(3 * stepS - (double)d) < 1e-9;
            for (size_t i = from; i < n; ++i) err = std::max(err, (double)std::fabs(R[i] - L[i - d]));
            if (exact) CHECK(err < 1e-6, "stereo offset 3: right = left delayed by 3 steps (%zu samples), max difference %.2g", d, err);
            else CHECK(err < 0.03, "stereo offset 3: right = left delayed by 3 steps (%.1f samples, rounded), max difference %.3f", 3 * stepS, err);
        }
        if (sr != 48000.0) continue;
        // ---- a constant input never jumps more than a small bound per sample
        {
            std::mt19937 rng(7);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            for (bool fastest : { false, true })
            {
                double worst = 0;
                for (int trial = 0; trial < 24; ++trial)
                {
                    Settings s;
                    auto randomize = [&]() {
                        s.steps = 1 + (int)(u(rng) * 32); s.rate = kNote32 + (int)(u(rng) * 10); s.swing = u(rng) * 75; s.length = 5 + u(rng) * 95;
                        s.attack = fastest ? 0.0 : 1.0 + u(rng) * 20; s.release = fastest ? 0.0 : 1.0 + u(rng) * 100; s.smooth = u(rng) < 0.5 ? 0 : u(rng) * 100;
                        s.depth = u(rng) * 100; s.mode = trial % 3; s.offset = (int)(u(rng) * 16); s.resonance = u(rng) * 100;
                        s.filterLow = 20 * std::pow(250.0, u(rng)); s.filterHigh = 200 * std::pow(100.0, u(rng));
                        for (int k = 0; k < kMaxSteps; ++k) s.level[k] = u(rng) < 0.5 ? 0.0f : (float)(u(rng) * 100);
                    };
                    randomize();
                    Engine e; e.setSettings(s); e.prepare(sr, 512);
                    const size_t n = (size_t)(1.0 * sr);
                    std::vector<float> L(n, 1.0f), R(n, 1.0f);
                    const double bpm = 60 + u(rng) * 140;
                    for (size_t off = 0, k = 0; off < n; off += 256, ++k)
                    {
                        if (k % 40 == 20) { randomize(); e.setSettings(s); }
                        Transport t; t.bpm = bpm; t.playing = true; t.valid = true; t.ppq = (double)off * bpm / (60.0 * sr);
                        e.setTransport(t);
                        e.process(L.data() + off, R.data() + off, (int)std::min<size_t>(256, n - off));
                    }
                    for (size_t i = (size_t)(0.05 * sr); i < n; ++i)
                        worst = std::max(worst, (double)std::max(std::fabs(L[i] - L[i - 1]), std::fabs(R[i] - R[i - 1])));
                }
                // (>= 1 ms: no faster than a 1 ms linear ramp, 0.021 per sample; 0 ms: the minimum ramp's 0.022 plus
                // the filter's passband level moving with it in Both mode)
                CHECK(worst < (fastest ? 0.03 : 0.021), "constant input, attack/release %s, every mode, settings moving: largest step %.4f",
                      fastest ? "0 (the minimum ramp)" : ">= 1 ms", worst);
            }
        }
        // ---- filter mode: closed steps lose the highs, keep the lows
        {
            Settings s = hard("x.");
            s.mode = kFilter; s.filterLow = 250; s.filterHigh = 20000; s.resonance = 0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const size_t n = (size_t)(1.0 * sr);
            std::vector<float> L(n), R(n);
            for (size_t i = 0; i < n; ++i) L[i] = R[i] = (float)(0.3 * std::sin(kTwoPi * 100.0 * i / sr) + 0.3 * std::sin(kTwoPi * 8000.0 * i / sr));
            run(e, { sr, 120.0, 0.0 }, L, R, 512);
            double hiDrop = 1e9, loDiff = 0;
            for (int k = 2; k < 6; k += 2)
            {
                const size_t a0 = (size_t)(k * stepS + 0.0125 * sr), b0 = (size_t)((k + 1) * stepS + 0.0125 * sr), w = (size_t)(0.1 * sr);
                const double hiO = toneAmp(L, a0, a0 + w, 8000, sr), hiC = toneAmp(L, b0, b0 + w, 8000, sr);
                const double loO = toneAmp(L, a0, a0 + w, 100, sr), loC = toneAmp(L, b0, b0 + w, 100, sr);
                hiDrop = std::min(hiDrop, gainToDb(hiO / std::max(1e-12, hiC)));
                loDiff = std::max(loDiff, std::fabs(gainToDb(loO / std::max(1e-12, loC))));
            }
            CHECK(hiDrop > 30.0 && loDiff < 3.0, "filter mode: 8 kHz drops %.1f dB in closed steps, 100 Hz changes %.2f dB", hiDrop, loDiff);
            s.resonance = 100;
            Engine r; r.setSettings(s); r.prepare(sr, 512);
            std::vector<float> L2(n), R2(n);
            for (size_t i = 0; i < n; ++i) L2[i] = R2[i] = (float)(0.3 * std::sin(kTwoPi * 250.0 * i / sr));
            run(r, { sr, 120.0, 0.0 }, L2, R2, 512);
            const size_t b0 = (size_t)(3 * stepS + 0.02 * sr), w = (size_t)(0.1 * sr);
            const double peak = gainToDb(toneAmp(L2, b0, b0 + w, 250, sr) / 0.3);
            CHECK(peak > 6.0 && peak < 20.0, "filter mode, resonance 100%%: a closed step rings at FILTER LOW (+%.1f dB at 250 Hz, kept in proportion)", peak);
        }
        // ---- loops and jumps resync on the next block (edges on their samples again right away)
        {
            Settings s = hard("x");
            for (int k = 0; k < kMaxSteps; ++k) s.level[k] = 10.0f + 5.0f * (float)(k % 16);
            s.steps = 16;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const size_t n = (size_t)(1.0 * sr), jump = 512 * 40;
            std::vector<float> L(n, 1.0f), R(n, 1.0f);
            std::vector<float> Lp(L.begin(), L.begin() + (long)jump), Rp(R.begin(), R.begin() + (long)jump);
            run(e, { sr, 120.0, 1.7 }, Lp, Rp, 512);
            std::vector<float> Lj(L.begin() + (long)jump, L.end()), Rj(R.begin() + (long)jump, R.end());
            run(e, { sr, 120.0, 10.1 }, Lj, Rj, 512);   // a jump forward into step 40 (pattern step 8), 40 % of the way in
            const long expect = (long)std::ceil(0.15 * 0.5 * sr - 1e-6);   // to ppq 10.25
            long at = -1;
            const double lv8 = Lj[(size_t)expect - 1];
            for (size_t i = (size_t)(0.015 * sr); i < Lj.size(); ++i) if (std::fabs(Lj[i] - lv8) > 1e-5) { at = (long)i; break; }
            CHECK(std::fabs(lv8 - 0.5) < 1e-6 && at == expect, "jump to ppq 10.1: step 8 (level %.4f) right away, next edge on its sample (%ld, expected %ld)", lv8, at, expect);
            // a loop back to the start mid-step: the step and its remaining length come from the new position
            Engine f; f.setSettings(s); f.prepare(sr, 512);
            std::vector<float> A(4096 * 4, 1.0f), B(4096 * 4, 1.0f);
            std::vector<float> A0(A.begin(), A.begin() + 4096), B0(B.begin(), B.begin() + 4096);
            run(f, { sr, 120.0, 7.6 }, A0, B0, 512);
            std::vector<float> A1(A.begin() + 4096, A.end()), B1(B.begin() + 4096, B.end());
            run(f, { sr, 120.0, 0.0 }, A1, B1, 512);
            const long ex2 = (long)std::ceil(0.25 * 0.5 * sr - 1e-6);
            long at2 = -1;
            const double lv0 = A1[(size_t)ex2 - 1];
            for (size_t i = (size_t)(0.015 * sr); i < A1.size(); ++i) if (std::fabs(A1[i] - lv0) > 1e-5) { at2 = (long)i; break; }
            CHECK(std::fabs(lv0 - 0.10) < 1e-6 && at2 == ex2 && f.currentStep(0) == 2, "loop back to ppq 0: step 0 (level %.4f), the next edge on its sample (%ld, expected %ld)", lv0, at2, ex2);
        }
        // ---- mix 0: the input, exactly (no latency), whatever the mode
        {
            double err = 0;
            for (int mode = 0; mode < kNumModes; ++mode)
            {
                Settings s = hard("x..x.x"); s.mix = 0; s.mode = mode; s.resonance = 90;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const size_t n = (size_t)(1.0 * sr);
                std::vector<float> L(n), R(n);
                for (size_t i = 0; i < n; ++i) { L[i] = mix[(2 * i) % mix.size()]; R[i] = mix[(2 * i + 1) % mix.size()]; }
                const std::vector<float> L0 = L, R0 = R;
                run(e, { sr, 120.0, 0.0 }, L, R, 333);
                for (size_t i = 0; i < n; ++i) err = std::max(err, (double)std::max(std::fabs(L[i] - L0[i]), std::fabs(R[i] - R0[i])));
            }
            CHECK(err == 0.0, "mix 0 = the input exactly in every mode (latency 0, max error %.2g)", err);
        }
        // ---- stress: random settings, block sizes, transports (stopped, playing, jumping, absurd), absurd input
        {
            std::mt19937 rng(11);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            double ppq = 0.0;
            for (int it = 0; it < 4000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.steps = 1 + (int)(u(rng) * 32); s.rate = (int)(u(rng) * kNumNotes); s.swing = u(rng) * 75; s.length = 5 + u(rng) * 95;
                    s.attack = u(rng) * 50; s.release = u(rng) * 500; s.smooth = u(rng) * 100; s.depth = u(rng) * 100;
                    s.mode = (int)(u(rng) * kNumModes); s.filterLow = 20 * std::pow(250.0, u(rng)); s.filterHigh = 200 * std::pow(100.0, u(rng));
                    s.resonance = u(rng) * 100; s.offset = (int)(u(rng) * 17); s.mix = u(rng) * 100; s.outDb = 0;
                    for (int k = 0; k < kMaxSteps; ++k) s.level[k] = (float)(u(rng) * 100);
                    e.setSettings(s);
                }
                Transport t;
                const double r = u(rng);
                t.bpm = r < 0.05 ? 0.0 : (r < 0.08 ? std::nan("") : (r < 0.1 ? 1e6 : 20 + u(rng) * 400));
                t.playing = u(rng) < 0.7; t.valid = u(rng) < 0.9;
                const double j = u(rng);
                if (j < 0.03) ppq = -u(rng) * 20;                     // pre-roll (negative positions)
                else if (j < 0.05) ppq = u(rng) * 1e7;                // far into a long session
                else if (j < 0.06) ppq = 1e300;                       // garbage
                else if (j < 0.07) ppq = std::nan("");
                else if (j < 0.12) ppq = u(rng) * 64;                 // loop / jump
                t.ppq = ppq;
                e.setTransport(t);
                const int bs = 1 + (int)(u(rng) * 1500);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 2000 && i == 0) L[0] = 1000.0f;   // absurd (but finite) input: must recover
                }
                e.process(L.data(), R.data(), bs);
                if (std::isfinite(ppq) && std::fabs(ppq) < 1e9) ppq += bs * 120.0 / (60.0 * sr);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                    if (it > 2100) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 8.0, "random settings, block sizes and transports (stopped, jumps, negative, NaN, absurd tempo): finite, peak %.2f after a +60 dB spike", peak);
        }
        // ---- CPU
        {
            for (int mode : { (int)kVolume, (int)kFilter, (int)kBoth })
            {
                Settings s; s.mode = mode; s.smooth = 20; s.resonance = 40;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const size_t n = (size_t)(4.0 * sr);
                std::vector<float> L(n), R(n);
                for (size_t i = 0; i < n; ++i) { L[i] = mix[(2 * i) % mix.size()]; R[i] = mix[(2 * i + 1) % mix.size()]; }
                const auto t0 = std::chrono::steady_clock::now();
                run(e, { sr, 120.0, 0.0 }, L, R, 512);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("  cpu: %s mode: %.2f %% of one core\n", kModeNames[mode], 100.0 * secs / 4.0);
            }
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
