// pump STUTTER: nothing playing = the input exactly; a held repeat plays its first slice again, sample for sample;
// a held reverse plays the past backwards; gate silences the closed part of each step; half speed halves the pitch;
// a tape stop runs down to silence; auto moves start exactly on the interval grid of the song position, at about
// the chance asked for; renders repeat; random settings, held moves and block sizes stay finite and bounded.
// 44.1, 48 and 96 kHz. Also prints CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/stutter/StutterEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::stutter;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}
static Settings manual() { Settings s; s.trigger = kManual; s.chance = 0; return s; }
// runs with a move held from sample `from` to `to` (blocks split there), transport stopped (free clock)
static void runHeld(Engine& e, std::vector<float>& L, std::vector<float>& R, int move, int from, int to, int block = 256)
{
    const int n = (int)L.size();
    int i = 0;
    while (i < n)
    {
        int end = std::min(n, i + block);
        if (i < from && end > from) end = from;
        if (i < to && end > to) end = to;
        e.setHeld(i >= from && i < to ? move : -1);
        e.process(L.data() + i, R.data() + i, end - i);
        i = end;
    }
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)(sr * 3.0);
        std::vector<float> L0((size_t)n), R0((size_t)n);
        for (int i = 0; i < n; ++i) { L0[(size_t)i] = mix[(size_t)(2 * i)]; R0[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
        const int fade = (int)(0.003 * sr), edge = (int)(0.001 * sr) + 2;
        // ---- nothing playing: exactly the input
        {
            Engine e; e.setSettings(manual()); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            runHeld(e, L, R, -1, 0, 0, 333);
            double err = 0; for (int i = 0; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]) + std::fabs(R[(size_t)i] - R0[(size_t)i]));
            CHECK(err == 0.0, "no move: output = input exactly (max error %.2g)", err);
        }
        const double qps = 120.0 / 60.0 / sr;
        const int slice = (int)std::lround(noteQuarters(kNote8) / qps);
        const int t0 = (int)(sr * 1.0);
        // ---- held repeat: every repeat is the first slice again
        {
            Settings s = manual(); s.grid = kNote8;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            runHeld(e, L, R, kRepeat, t0, t0 + 4 * slice);
            double err = 0;
            for (int k = 1; k < 4; ++k)
                for (int j = edge; j < slice - edge; ++j)
                    err = std::max(err, (double)std::fabs(L[(size_t)(t0 + k * slice + j)] - L0[(size_t)(t0 + j)]));
            double err0 = 0; for (int j = fade; j < slice; ++j) err0 = std::max(err0, (double)std::fabs(L[(size_t)(t0 + j)] - L0[(size_t)(t0 + j)]));
            CHECK(err < 1e-6 && err0 < 1e-6, "held repeat (1/8): repeats 2-4 = slice 1 (max error %.2g), slice 1 = live (%.2g)", err, err0);
            // after release (and its fade) the live signal is back
            double errAfter = 0; for (int i = t0 + 4 * slice + fade + 64; i < n; ++i) errAfter = std::max(errAfter, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]));
            CHECK(errAfter < 1e-6, "after release: live again (max error %.2g)", errAfter);
        }
        // ---- held reverse: the past, backwards
        {
            Engine e; e.setSettings(manual()); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            runHeld(e, L, R, kReverse, t0, t0 + slice);
            double err = 0; for (int j = fade; j < slice; ++j) err = std::max(err, (double)std::fabs(L[(size_t)(t0 + j)] - L0[(size_t)(t0 - 1 - j)]));
            CHECK(err < 1e-6, "held reverse: plays what came before backwards (max error %.2g)", err);
        }
        // ---- gate: silent in the closed part of every step
        {
            Settings s = manual(); s.grid = kNote16; s.gate = 50;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            const double step = noteQuarters(kNote16) / qps;   // (fractional at 44.1 kHz)
            runHeld(e, L, R, kGateMove, t0, t0 + (int)(8 * step));
            double closed = 0;
            for (int k = 0; k < 8; ++k)
                for (int i = t0 + (int)std::ceil(k * step + 0.5 * step) + edge; i < t0 + (int)std::floor((k + 1) * step) - 1; ++i)
                    closed = std::max(closed, (double)std::fabs(L[(size_t)i]));
            CHECK(closed < 1e-6, "gate 50%%: the closed half of each 1/16 is silent (peak %.2g)", closed);
        }
        if (sr != 48000.0) continue;
        // ---- half speed: a sine an octave down
        {
            Engine e; e.setSettings(manual()); e.prepare(sr, 512);
            std::vector<float> L((size_t)n), R((size_t)n);
            for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.5 * std::sin(kTwoPi * 880.0 * i / sr));
            runHeld(e, L, R, kHalf, t0, n);
            int cross = 0; for (int i = t0 + 4800; i < t0 + 4800 + 24000; ++i) if (L[(size_t)(i - 1)] < 0 && L[(size_t)i] >= 0) ++cross;
            CHECK(std::abs(cross - 220) <= 2, "half speed: 880 Hz plays at %d Hz", cross * 2);
        }
        // ---- tape stop: runs down to silence by the end of its time
        {
            Settings s = manual(); s.tape = 50;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            runHeld(e, L, R, kTapeStop, t0, n);   // held: 2 s of tape time x 50 % = 1 s to stop
            double late = 0; for (int i = t0 + (int)sr + 10; i < t0 + (int)(1.5 * sr); ++i) late = std::max(late, (double)std::fabs(L[(size_t)i]));
            CHECK(late < 1e-6, "tape stop (50%% of 2 s): silent once stopped (peak %.2g)", late);
        }
        // ---- auto: moves start on the song's quarter-note grid
        {
            Settings s; s.trigger = kAuto; s.interval = kNote4; s.length = kNote16; s.chance = 100;
            for (bool& u : s.use) u = false;
            s.use[kRepeat] = true;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int q = (int)(sr * 0.5);   // a quarter at 120 BPM
            int starts = 0, offGrid = 0, last = -1;
            float x[1], y[1];
            for (int i = 0; i < 4 * q; ++i)
            {
                Transport t; t.bpm = 120; t.ppq = (i + 0.25 * q) / (double)q; t.playing = true; t.valid = true;   // the song starts mid-quarter
                e.setTransport(t);
                x[0] = y[0] = 0.1f;
                e.process(x, y, 1);
                const int m = e.moveNow();
                if (m >= 0 && last < 0) { ++starts; if ((i + q / 4) % q != 0) ++offGrid; }
                last = m;
            }
            CHECK(starts >= 3 && offGrid == 0, "auto, chance 100%% every 1/4: %d moves, all on the quarter grid (%d off)", starts, offGrid);
            s.chance = 50; s.interval = kNote16;
            Engine e2; e2.setSettings(s); e2.prepare(sr, 512);
            std::vector<float> L((size_t)(sr * 20), 0.1f), R = L;
            int moves = 0, prev = -1;
            for (size_t off = 0; off < L.size(); off += 64)
            {
                e2.process(L.data() + off, R.data() + off, 64);
                const int m = e2.moveNow();
                if (m >= 0 && prev < 0) ++moves;
                prev = m;
            }
            // 20 s at 120 BPM = 160 sixteenths; a move lasts one 1/16, so a new one can start at the next boundary
            // only after the last one has faded: about every other boundary at most
            CHECK(moves > 25 && moves < 110, "auto, chance 50%% every 1/16 over 20 s: %d moves", moves);
        }
        // ---- determinism and stress
        {
            Settings s; s.chance = 80; s.interval = kNote8; s.variation = 100; for (bool& u : s.use) u = true;
            std::vector<float> a = L0, ar = R0, b = L0, br = R0;
            { Engine e; e.setSettings(s); e.prepare(sr, 512); runHeld(e, a, ar, -1, 0, 0, 256); }
            { Engine e; e.setSettings(s); e.prepare(sr, 512); runHeld(e, b, br, -1, 0, 0, 256); }
            double diff = 0; for (int i = 0; i < n; ++i) diff = std::max(diff, (double)std::fabs(a[(size_t)i] - b[(size_t)i]));
            CHECK(diff == 0.0, "auto with full variation renders identically twice (max diff %.2g)", diff);

            std::mt19937 rng(29);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            for (int it = 0; it < 4000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.trigger = u(rng) < 0.8 ? kAuto : kManual; s.interval = (int)(u(rng) * kNumNotes); s.length = (int)(u(rng) * kNumNotes);
                    s.grid = (int)(u(rng) * kNumNotes); s.chance = u(rng) * 100; s.gate = 5 + u(rng) * 95; s.pitch = u(rng) * 24 - 12;
                    s.decay = u(rng) * 100; s.filter = u(rng) * 200 - 100; s.variation = u(rng) * 100; s.tape = 10 + u(rng) * 90; s.mix = u(rng) * 100;
                    for (bool& x : s.use) x = u(rng) < 0.5;
                    e.setSettings(s);
                }
                Transport t; t.bpm = 40 + u(rng) * 200; t.ppq = pos / sr * 2.0; t.playing = u(rng) < 0.7; t.valid = u(rng) < 0.9;
                e.setTransport(t);
                e.setHeld(u(rng) < 0.2 ? (int)(u(rng) * kNumMoves) : -1);
                const int bs = 1 + (int)(u(rng) * 1200);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 2000 && i == 0) L[0] = 1000.0f;
                }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                    if (it > 3000) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 4.0, "random settings, held moves, tempos and block sizes: finite, peak %.2f", peak);
        }
        // ---- CPU (glitch with full variation, the busiest)
        {
            Settings s; s.chance = 100; s.interval = kNote8; s.length = kNote8; s.variation = 100; for (bool& x : s.use) x = false; s.use[kGlitch] = true;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            const auto t0c = std::chrono::steady_clock::now();
            runHeld(e, L, R, -1, 0, 0, 512);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0c).count();
            printf("  cpu: glitching all the time: %.1f %% of one core\n", 100.0 * secs / 3.0);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
