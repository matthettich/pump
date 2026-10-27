// pump CHOP: with no pad down the output is the input exactly; a pad plays its slice of the last bar, sample for
// sample, and on through the bar; the region stays put while a pad is down; reverse plays the slice backwards; pitch +12
// plays it at double speed; two pads loop from the lower slice to the end of the higher; quantise lands a press on
// the grid; releasing every pad returns to the live input; random settings and presses stay finite. 44.1 / 48 / 96 kHz.
#include "../plugins/chop/ChopEngine.hpp"
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::chop;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

// a test signal whose every sample says where it was recorded (a slow ramp, so reading it back says where from)
static double rampAt(long long i) { return (double)(i % 1048576) / 1048576.0 - 0.5; }

struct Run {
    Engine e;
    double sr;
    long long t = 0;
    std::vector<float> L, R;
    explicit Run(double s, const Settings& st) : sr(s) { e.setSettings(st); e.prepare(sr, 512); }
    // n samples with the pads `mask` held, the song playing at 120 BPM from position 0
    void go(int n, uint32_t mask, int block = 64)
    {
        for (int i = 0; i < n; i += block)
        {
            const int b = std::min(block, n - i);
            std::vector<float> l((size_t)b), r((size_t)b);
            for (int k = 0; k < b; ++k) l[(size_t)k] = r[(size_t)k] = (float)rampAt(t + k);
            Transport tp; tp.bpm = 120; tp.ppq = t * 2.0 / sr; tp.playing = true; tp.valid = true; tp.barQuarters = 4.0;
            e.setTransport(tp);
            e.setHeld(mask);
            e.process(l.data(), r.data(), b);
            for (int k = 0; k < b; ++k) { L.push_back(l[(size_t)k]); R.push_back(r[(size_t)k]); }
            t += b;
        }
    }
};

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const double bar = 2.0 * sr;   // 120 BPM, 4/4
        Settings st; st.quant = kQOff; st.slices = 8;
        for (int p = 0; p < kPads; ++p) st.pad[(size_t)p].slice = p + 1;   // as the plugin's defaults: pad n plays slice n
        // nothing held: the input exactly
        {
            Run r(sr, st); r.go((int)(1.5 * bar), 0);
            double err = 0; for (size_t i = 0; i < r.L.size(); ++i) err = std::max(err, std::fabs(r.L[i] - rampAt((long long)i)));
            CHECK(err < 1e-6, "no pad: output = input (max error %.2g)", err);
        }
        // pad 3 (slice 3) during the second bar: the third eighth of the first bar, and on from there
        {
            Run r(sr, st); r.go((int)bar, 0);
            const long long t0 = r.t;
            r.go((int)(0.3 * bar), 1u << 2);
            const double slice = bar / 8;
            double err = 0;
            for (int k = (int)(0.004 * sr); k < (int)(0.3 * bar); ++k)
            {
                const double want = rampAt((long long)(2 * slice) + k);
                err = std::max(err, std::fabs(r.L[(size_t)(t0 + k)] - want));
            }
            CHECK(err < 1e-4, "pad 3: slice 3 of the last bar, then on through it (max error %.2g)", err);
            // the region stays put while the pad is down (well into the next bar)
            r.go((int)(0.9 * bar), 1u << 2);
            const long long k1 = (long long)(1.1 * bar);
            const double want = rampAt((long long)std::fmod(2 * slice + k1, bar));
            CHECK(std::fabs(r.L[(size_t)(t0 + k1)] - want) < 1e-4, "held past the bar line, it still plays the captured bar (wrapping)");
            r.go((int)(0.2 * bar), 0);
            const long long k2 = r.t - 100;
            CHECK(std::fabs(r.L[(size_t)k2] - rampAt(k2)) < 1e-6, "every pad up: back to the live input");
        }
        // reverse, pitch
        {
            Settings s2 = st; s2.pad[0].reverse = true;
            Run r(sr, s2); r.go((int)bar, 0);
            const long long t0 = r.t; r.go((int)(0.1 * bar), 1u);
            const double slice = bar / 8;
            const int k = (int)(0.05 * bar);
            CHECK(std::fabs(r.L[(size_t)(t0 + k)] - rampAt((long long)(slice - 1 - k))) < 1e-4, "reverse: slice 1 backwards from its end");
            Settings s3 = st; s3.pad[0].pitch = 12;
            Run q(sr, s3); q.go((int)bar, 0);
            const long long u0 = q.t; q.go((int)(0.1 * bar), 1u);
            CHECK(std::fabs(q.L[(size_t)(u0 + k)] - rampAt((long long)(2 * k))) < 1e-4, "pitch +12: twice as fast through the slice");
        }
        // two pads: loop from slice 2 to the end of slice 3
        {
            Run r(sr, st); r.go((int)bar, 0);
            const long long t0 = r.t; r.go((int)(0.6 * bar), (1u << 1) | (1u << 2));
            const double slice = bar / 8;
            // the newest is pad 3 (bit 2 pressed in the same sample: the higher bit last) -> starts at slice 3, loops 2..3
            bool inLoop = true;
            for (int k = (int)(0.01 * sr); k < (int)(0.6 * bar); k += 97)
            {
                const double v = r.L[(size_t)(t0 + k)] + 0.5;   // back to a recorded position
                const double at = v * 1048576.0;
                if (at < slice - 2 || at > 3 * slice + 2) inLoop = false;
            }
            CHECK(inLoop, "two pads held: it loops from slice 2 to the end of slice 3");
        }
        // quantise: a press 10 ms after a 16th line starts on the next one
        {
            Settings s4 = st; s4.quant = kQ16;
            Run r(sr, s4); r.go((int)bar, 0);
            const double sixteenth = bar / 16;
            r.go((int)(0.01 * sr), 0);
            const long long t0 = r.t; r.go((int)(0.2 * bar), 1u);
            const long long startAt = (long long)(bar + sixteenth);
            const double before = r.L[(size_t)(startAt - 300)], after = r.L[(size_t)(startAt + 400)];
            CHECK(std::fabs(before - rampAt(startAt - 300)) < 1e-6 && std::fabs(after - rampAt(400 - (long long)(0.003 * sr) * 0)) < 0.02, "QUANTISE 1/16: a late press waits for the next 16th (live until then: %s)", t0 < startAt ? "yes" : "no");
        }
        // random settings and presses
        {
            std::mt19937 rng(7);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            bool finite = true; double peak = 0.0;
            for (int t = 0; t < 30; ++t)
            {
                Settings s;
                for (auto& p : s.pad) { p.slice = 1 + (int)(u(rng) * 16); p.pitch = (int)(u(rng) * 49) - 24; p.reverse = u(rng) < 0.3; p.fx = (int)(u(rng) * kNumFx) % kNumFx; p.amount = u(rng); p.levelDb = -24 + u(rng) * 30; }
                s.slices = u(rng) < 0.5 ? 8 : 16; s.length = (int)(u(rng) * kNumLengths) % kNumLengths; s.quant = (int)(u(rng) * kNumQuant) % kNumQuant;
                s.hold = u(rng) < 0.5; s.loopTwo = u(rng) < 0.5; s.mix = u(rng);
                Run r(sr, s);
                for (int b = 0; b < 60; ++b) r.go(1000 + (int)(u(rng) * 3000), (uint32_t)(u(rng) * 256), 37 + (int)(u(rng) * 300));
                for (float v : r.L) { finite = finite && std::isfinite(v); peak = std::max(peak, (double)std::fabs(v)); }
            }
            CHECK(finite && peak < 6.0, "random settings and presses: finite (peak %.2f)", peak);
        }
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
