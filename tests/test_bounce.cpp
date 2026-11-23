// pump BOUNCE: the parameter table is stable (count, symbols, ranges); the hit times are the geometric series
// T (1 - g^k) / (1 - g) (and its inverse with RISE), stop when the gaps fall under 2 ms or the 8 s cap is reached;
// Taps puts an impulse's taps exactly on that series at 44.1 / 48 / 96 kHz with DAMP dB per bounce, spread across the
// field, and FEEDBACK re-drops the settled ball; Drop fires from a transient, the button, MIDI and the host tempo
// (free-running and locked to the beat), re-fires the slice at the bounce times and goes completely silent once the ball
// has settled, climbs or falls by PITCH per bounce, and fades the oldest of more than four drops; FEEDBACK brings the whole
// bounce back; mix 0 is the input exactly; the output is the same for every block size; random settings, tempo changes
// and absurd input stay finite, bounded and free of denormals. Also prints CPU.
#include "TestMix.hpp"
#include "../plugins/bounce/BounceEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::bounce;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    const int n = (int)L.size();
    for (int off = 0; off < n; off += block) e.process(L.data() + off, R.data() + off, std::min(block, n - off));
}
static double rms(const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min(to, x.size());
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)x[i] * x[i];
    return std::sqrt(e / std::max<size_t>(1, to - from));
}
static double sumWin(const std::vector<float>& x, long c, long r)   // the sum around c: a fractional-delay impulse keeps its area
{
    double s = 0;
    for (long i = std::max(0L, c - r); i <= std::min((long)x.size() - 1, c + r); ++i) s += x[(size_t)i];
    return s;
}
static double db(double g) { return 20.0 * std::log10(g + 1e-30); }
static double zcFreq(const std::vector<float>& x, size_t from, size_t to, double sr)
{
    double first = -1, last = -1; int count = 0;
    for (size_t i = from + 1; i < std::min(to, x.size()); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (double)(i - 1) + (double)x[i - 1] / ((double)x[i - 1] - x[i]);
            if (first < 0) first = t; else ++count;
            last = t;
        }
    return count > 0 ? sr * count / (last - first) : 0.0;
}
// fully wet, no feedback, centred
static Settings wet(int mode)
{
    Settings s; s.mode = mode; s.mix = 100; s.spread = 0; s.dampDb = 3; s.feedback = 0; s.jitter = 0; s.tone = 0; s.pitch = 0;
    return s;
}
static bool bad(const std::vector<float>& x, bool& denormal)
{
    bool b = false;
    for (float v : x) { if (!std::isfinite(v)) b = true; if (std::fpclassify(v) == FP_SUBNORMAL) denormal = true; }
    return b;
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    // ---- the parameter table: count, symbols (append only), defaults, presets
    {
        const char* const sym[] = { "mode", "time", "sync", "note", "gravity", "rise", "bounces", "damp", "pitch", "tone", "spread", "jitter",
                                    "slice", "fit", "trigger", "threshold", "every", "drop", "feedback", "mix", "out_gain", "bypass" };
        const auto& p = params();
        bool same = (int)p.size() == kNumParams && kNumParams == (int)(sizeof sym / sizeof sym[0]);
        for (size_t i = 0; same && i < p.size(); ++i) same = std::strcmp(p[i].symbol, sym[i]) == 0 && p[i].def >= p[i].min && p[i].def <= p[i].max;
        CHECK(same, "%zu parameters, symbols and indices as released", p.size());
        CHECK(p[kDrop].flags & kPNoUndo, "DROP is a momentary parameter (not in undo, A/B or presets)");
        CHECK(p[kGravity].min == 0.5f && p[kGravity].max == 0.98f && p[kBounces].min == 2 && p[kBounces].max == 64 && p[kPitch].min == -12 && p[kPitch].max == 12 &&
              p[kSlice].min == 5 && p[kSlice].max == 500 && p[kTime].min == 10 && p[kTime].max == 4000 && p[kFeedback].max == 90 && p[kDamp].max == 12,
              "ranges: gravity 0.50-0.98, bounces 2-64, pitch +-12, slice 5-500 ms, time 10-4000 ms, feedback 0-90 %%, damp 0-12 dB");
        CHECK(presets().size() >= 12 && std::strcmp(presets()[0].name, "Default") == 0, "%zu factory presets, Default first", presets().size());
    }

    // ---- the hit times
    {
        bool ok = true; double worst = 0;
        for (double T : { 0.01, 0.2, 0.75 })
            for (double g : { 0.5, 0.7, 0.9, 0.98 })
            {
                double t[kMaxBounces + 2];
                const int m = bounceTimes(T, g, 12, 8.0, t);
                for (int k = 1; k <= m; ++k)
                {
                    const double want = T * (1.0 - std::pow(g, k)) / (1.0 - g);
                    worst = std::max(worst, std::fabs(t[k] - want));
                    if (t[k] <= t[k - 1]) ok = false;
                }
            }
        CHECK(ok && worst < 1e-12, "hit times are T (1 - g^k) / (1 - g) (worst error %.2g s)", worst);
        double t[kMaxBounces + 2];
        const int m = bounceTimes(0.1, 0.5, 64, 8.0, t);
        CHECK(m == 6 && std::fabs(t[6] - 0.196875) < 1e-12, "g 0.5 from 100 ms: the ball settles after %d hits at %.4f s (the next gap is under 2 ms)", m, t[m]);
        const int c = bounceTimes(4.0, 0.98, 64, 8.0, t);
        CHECK(c == 2 && t[c] < 8.0, "T 4 s, g 0.98: %d hits, the third would be past 8 s", c);
        const double rho = gapRatio(0.8, true);
        const int r = bounceTimes(0.05, rho, 10, 8.0, t);
        bool grow = r == 10;
        for (int k = 2; k <= r; ++k) grow = grow && std::fabs((t[k] - t[k - 1]) / (t[k - 1] - t[k - 2]) - 1.25) < 1e-9;
        CHECK(grow, "RISE: the gaps grow by 1 / gravity (x1.25 for 0.80)");
    }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        // ---- Taps: an impulse's taps land on the series, DAMP dB apart
        {
            Settings s = wet(kModeTaps); s.timeMs = 120.0; s.gravity = 0.75; s.bounces = 10; s.dampDb = 3.0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            double t[kMaxBounces + 2];
            const int m = bounceTimes(0.12, 0.75, 10, 8.0, t);
            std::vector<float> L((size_t)(sr * 2.0), 0.0f), R = L;
            L[0] = R[0] = 1.0f;
            run(e, L, R, 100);
            bool pos = true, lvl = true; double worstLvl = 0; long worstPos = 0;
            for (int k = 1; k <= m; ++k)
            {
                const long c = std::lround(t[k] * sr);
                size_t pk = (size_t)c; float best = 0;
                for (long i = c - 4; i <= c + 4; ++i) if (std::fabs(L[(size_t)i]) > best) { best = std::fabs(L[(size_t)i]); pk = (size_t)i; }
                worstPos = std::max(worstPos, std::labs((long)pk - c));
                if (std::labs((long)pk - c) > 1) pos = false;
                const double want = std::pow(10.0, -3.0 * k / 20.0), got = sumWin(L, c, 4);
                worstLvl = std::max(worstLvl, std::fabs(db(got) - db(want)));
                if (std::fabs(db(got) - db(want)) > 0.3) lvl = false;
            }
            // and nothing between the taps beyond the last
            const double after = rms(L, (size_t)(t[m] * sr) + 16, L.size());
            CHECK(pos && lvl && after < 1e-6, "taps: %d taps on the series (worst %ld samples, %.2f dB off), silence after the last", m, worstPos, worstLvl);
        }
        // ---- Taps: the spread puts the bounces on alternating sides
        {
            Settings s = wet(kModeTaps); s.timeMs = 100.0; s.gravity = 0.5; s.bounces = 6; s.dampDb = 0.0; s.spread = 100.0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(sr * 0.5), 0.0f), R = L;
            L[0] = R[0] = 1.0f;
            run(e, L, R, 128);
            double t[kMaxBounces + 2];
            bounceTimes(0.1, 0.5, 6, 8.0, t);
            bool alt = true;
            for (int k = 1; k <= 6; ++k)
            {
                const long c = std::lround(t[k] * sr);
                const double l = sumWin(L, c, 3), r = sumWin(R, c, 3);
                if ((k & 1) ? !(l > r) : !(r > l)) alt = false;
            }
            CHECK(alt, "spread 100: odd bounces lean left, even ones right");
        }
        // ---- Taps: FEEDBACK re-drops the settled ball (a softer copy of the whole bounce after the last tap)
        {
            Settings s = wet(kModeTaps); s.timeMs = 100.0; s.gravity = 0.5; s.bounces = 6; s.dampDb = 6.0; s.feedback = 50.0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            double t[kMaxBounces + 2];
            const int m = bounceTimes(0.1, 0.5, 6, 8.0, t);
            std::vector<float> L((size_t)(sr * 1.2), 0.0f), R = L;
            L[0] = R[0] = 1.0f;
            run(e, L, R, 256);
            const long c = std::lround((t[m] + t[1]) * sr);
            const double got = sumWin(L, c, 4), want = 0.5 * std::pow(10.0, -6.0 / 20.0);
            Settings s0 = s; s0.feedback = 0.0;
            Engine e0; e0.setSettings(s0); e0.prepare(sr, 512);
            std::vector<float> L0((size_t)(sr * 1.2), 0.0f), R0 = L0;
            L0[0] = R0[0] = 1.0f;
            run(e0, L0, R0, 256);
            CHECK(std::fabs(db(got) - db(want)) < 0.5 && rms(L0, (size_t)(0.3 * sr), L0.size()) < 1e-6,
                  "feedback 50%%: the first bounce comes back after the last tap at %.1f dB (want %.1f); 0%% leaves silence", db(got), db(want));
        }
        // ---- mix 0: the input exactly (no latency)
        {
            Settings s = wet(kModeDrop); s.mix = 0; s.feedback = 60; s.pitch = 3;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(sr * 2.0);
            std::vector<float> L((size_t)n), R((size_t)n);
            for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            const std::vector<float> L0 = L, R0 = R;
            e.fire();
            run(e, L, R, 333);
            double err = 0;
            for (int i = 0; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]) + std::fabs(R[(size_t)i] - R0[(size_t)i]));
            CHECK(err == 0.0 && e.latency() == 0, "mix 0 = the input exactly (max error %.2g, latency %d)", err, e.latency());
        }
    }

    const double sr = 48000.0;
    // a 40 ms burst of a 440 Hz sine, starting at 100 ms
    auto burst = [&](double freq, double ms, double at, double len) {
        std::vector<float> L((size_t)(sr * len), 0.0f);
        for (int i = 0; i < (int)(ms * 0.001 * sr); ++i) L[(size_t)(at * sr) + (size_t)i] = (float)(0.8 * std::sin(kTwoPi * freq * i / sr));
        return L;
    };
    // ---- Drop: a transient fires it, the slice comes back on the series and the ball settles into silence
    {
        Settings s = wet(kModeDrop); s.timeMs = 100.0; s.gravity = 0.7; s.bounces = 64; s.dampDb = 1.0; s.sliceMs = 30.0; s.trigger = kTrigTransient; s.thresholdDb = -20.0;
        Engine e; e.setSettings(s); e.prepare(sr, 512);
        std::vector<float> L = burst(440.0, 30.0, 0.1, 3.0), R = L;
        const uint32_t ev0 = e.events();
        run(e, L, R, 200);
        double t[kMaxBounces + 2];
        const int m = bounceTimes(0.1, 0.7, 64, 8.0, t);
        bool hits = true; int heard = 0;
        for (int k = 1; k <= m; ++k)
        {
            const size_t c = (size_t)((0.1 + t[k]) * sr);
            const double dur = std::min(0.03, 0.92 * 0.1 * std::pow(0.7, k)) * sr;   // FIT: the hit lasts as long as its gap allows (at most the slice)
            double pk = 0; for (size_t i = c; i < c + (size_t)dur; ++i) pk = std::max(pk, (double)std::fabs(L[i]));
            const double want = std::pow(10.0, -1.0 * k / 20.0) * 0.8;
            if (pk > 0.2 * want && pk < 1.05 * want) ++heard; else { hits = false; printf("    hit %d: peak %.3f, want up to %.3f\n", k, pk, want); }
            if (k == 1)
            {
                const double inside = rms(L, c + (size_t)(0.3 * dur), c + (size_t)(0.7 * dur)), before = rms(L, c - 80, c - 20);
                if (before > 1e-6 || std::fabs(db(inside) - db(want * 0.7071)) > 1.5) hits = false;
            }
        }
        // the whole bounce: first hit at 0.1 + T, settles by 0.1 + t_m + slice
        const size_t end = (size_t)((0.1 + t[m] + 0.04) * sr);
        double tail = 0; for (size_t i = end; i < L.size(); ++i) tail = std::max(tail, (double)std::fabs(L[i]));
        CHECK(e.events() - ev0 == 1 && hits && heard == m, "drop: one drop, all %d hits on the series (the ball settles after %d, last at %.0f ms)", heard, m, 1000.0 * t[m]);
        CHECK(tail == 0.0 && rms(L, 0, (size_t)(0.1 * sr)) == 0.0 && e.liveDrops() == 0, "drop: silent (exactly 0) before the onset and %.0f ms after the settling", 1000.0 * (L.size() / sr - (0.1 + t[m] + 0.04)));
    }
    // ---- Drop: no signal, no drop; the button always drops (any trigger); MIDI only when TRIGGER is MIDI
    {
        Settings s = wet(kModeDrop); s.timeMs = 100.0; s.gravity = 0.6; s.bounces = 8; s.sliceMs = 40.0; s.trigger = kTrigButton; s.thresholdDb = -20.0;
        std::vector<float> L = burst(300.0, 80.0, 0.0, 1.5);
        {
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> a = L, b = L;
            const uint32_t ev0 = e.events();
            e.process(a.data(), b.data(), 1);   // (an onset at once: not a trigger with TRIGGER = Button)
            std::vector<float> x(L.begin() + 1, L.end()), y = x;
            run(e, x, y, 256);
            CHECK(e.events() == ev0 && rms(x, (size_t)(0.15 * sr), x.size()) == 0.0, "button trigger: a transient does not fire it");
        }
        {
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> a = burst(300.0, 80.0, 0.0, 0.2), b = a;
            e.fire();
            run(e, a, b, 64);
            std::vector<float> x(sr * 1.3, 0.0f), y = x;
            run(e, x, y, 64);
            std::vector<float> all = a; all.insert(all.end(), x.begin(), x.end());
            double t[kMaxBounces + 2]; bounceTimes(0.1, 0.6, 8, 8.0, t);
            bool ok = rms(all, (size_t)((t[1] + 0.001) * sr), (size_t)((t[1] + 0.03) * sr)) > 0.05 && e.events() == 1;
            CHECK(ok, "button: fire() drops the slice at T (%.1f dB at the first bounce)", db(rms(all, (size_t)((t[1] + 0.001) * sr), (size_t)((t[1] + 0.03) * sr))));
        }
        {
            Settings m = s; m.trigger = kTrigMidi;
            Engine e; e.setSettings(m); e.prepare(sr, 512);
            std::vector<float> a(256, 0.0f), b = a;
            e.noteOn(0.9f); e.process(a.data(), b.data(), 256);
            const uint32_t n1 = e.events();
            Engine e2; e2.setSettings(s); e2.prepare(sr, 512);
            e2.noteOn(0.9f); e2.process(a.data(), b.data(), 256);
            CHECK(n1 == 1 && e2.events() == 0, "MIDI: a note-on drops with TRIGGER = MIDI and is ignored otherwise");
        }
        {   // the engine is in Taps mode: the button only moves the ball
            Settings t = wet(kModeTaps); t.trigger = kTrigButton;
            Engine e; e.setSettings(t); e.prepare(sr, 512);
            std::vector<float> a(256, 0.0f), b = a;
            e.fire(); e.process(a.data(), b.data(), 256);
            CHECK(e.events() == 1 && e.ballAge() < 0.01f && rms(a, 0, 256) == 0.0, "taps: the button only moves the UI's ball");
        }
    }
    // ---- Drop: TRIGGER = Sync fires every note division (free-running, and locked to the beat)
    {
        Settings s = wet(kModeDrop); s.trigger = kTrigSync; s.every = kNote4; s.bounces = 4; s.timeMs = 50; s.sliceMs = 20;
        {
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.bpm = 120.0; e.setTransport(t);
            std::vector<float> L((size_t)(sr * 3.2), 0.0f), R = L;
            run(e, L, R, 500);
            CHECK(e.events() == 7, "sync, no transport: 1/4 at 120 BPM drops every 0.5 s (%u drops in 3.2 s: at 0, 0.5 ... 3.0)", e.events());
        }
        {
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.bpm = 120.0; t.ppq = 0.3; t.valid = true; t.playing = true; e.setTransport(t);
            std::vector<float> L(1, 0.0f), R = L;
            long first = -1;
            for (long i = 0; i < (long)sr && first < 0; ++i) { e.process(L.data(), R.data(), 1); if (e.events() > 0) first = i; }
            // the beat is 0.7 quarter = 0.35 s away
            CHECK(std::labs(first - std::lround(0.35 * sr)) <= 1, "sync, playing at 0.3 quarters: the first drop lands on the beat (sample %ld, want %ld)", first, std::lround(0.35 * sr));
        }
    }
    // ---- Drop: PITCH accumulates per bounce (a resampled slice): +12 st = x2 then x4 (the ratio stops at two octaves)
    {
        Settings s = wet(kModeDrop); s.timeMs = 120.0; s.gravity = 0.9; s.bounces = 4; s.dampDb = 0.0; s.sliceMs = 60.0; s.pitch = 12.0; s.trigger = kTrigButton; s.fit = false;
        Engine e; e.setSettings(s); e.prepare(sr, 512);
        std::vector<float> L((size_t)(sr * 1.5), 0.0f);
        for (size_t i = 0; i < L.size(); ++i) L[i] = (float)(0.5 * std::sin(kTwoPi * 440.0 * (double)i / sr));
        std::vector<float> R = L;
        e.fire();
        run(e, L, R, 128);
        double t[kMaxBounces + 2]; bounceTimes(0.12, 0.9, 4, 8.0, t);
        const double f1 = zcFreq(L, (size_t)(t[1] * sr) + 400, (size_t)(t[1] * sr) + 1400, sr);
        const double f2 = zcFreq(L, (size_t)(t[2] * sr) + 200, (size_t)(t[2] * sr) + 600, sr);
        Settings d = s; d.pitch = -12.0; d.sliceMs = 120.0;
        Engine e2; e2.setSettings(d); e2.prepare(sr, 512);
        std::vector<float> M((size_t)(sr * 1.5), 0.0f);
        for (size_t i = 0; i < M.size(); ++i) M[i] = (float)(0.5 * std::sin(kTwoPi * 440.0 * (double)i / sr));
        std::vector<float> M2 = M;
        e2.fire();
        run(e2, M, M2, 128);
        const double g1 = zcFreq(M, (size_t)(t[1] * sr) + 600, (size_t)(t[1] * sr) + 3600, sr);
        CHECK(std::fabs(f1 / 880.0 - 1.0) < 0.02 && std::fabs(f2 / 1760.0 - 1.0) < 0.03 && std::fabs(g1 / 220.0 - 1.0) < 0.02,
              "pitch +12 per bounce: %.0f Hz then %.0f Hz from 440 (the ratio stops at x4); -12: %.0f Hz", f1, f2, g1);
    }
    // ---- Drop: TONE tilts every bounce further; FIT shortens the hits to their gaps
    {
        Settings s = wet(kModeDrop); s.timeMs = 100.0; s.gravity = 0.8; s.bounces = 6; s.dampDb = 0.0; s.sliceMs = 40.0; s.tone = -100.0; s.trigger = kTrigButton;
        std::vector<float> src((size_t)(sr * 1.5), 0.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(-0.5f, 0.5f);
        float lp = 0.0f;
        for (float& v : src) { lp += 0.15f * (u(rng) - lp); v = lp * 3.0f; }   // dark noise: room to brighten
        auto level = [&](double tone, int k) {
            Settings q = s; q.tone = tone;
            Engine e; e.setSettings(q); e.prepare(sr, 512);
            std::vector<float> a = src, b = src;
            e.fire();
            run(e, a, b, 128);
            double t[kMaxBounces + 2]; bounceTimes(0.1, 0.8, 6, 8.0, t);
            // high band energy: first difference
            double hf = 0, all = 0;
            const size_t c = (size_t)(t[k] * sr);
            for (size_t i = c + 100; i < c + 1500; ++i) { const double d = a[i] - a[i - 1]; hf += d * d; all += (double)a[i] * a[i]; }
            return hf / (all + 1e-30);
        };
        const double d1 = level(-100.0, 1), d5 = level(-100.0, 5), b5 = level(100.0, 5), n5 = level(0.0, 5);
        CHECK(d5 < d1 * 0.7 && d5 < n5 * 0.4 && b5 > n5 * 1.5, "tone: darker each bounce at -100 (hf ratio %.2f -> %.2f), brighter at +100 (%.2f vs %.2f flat)", d1, d5, b5, n5);
    }
    // ---- Drop: FEEDBACK brings the whole bounce back, softer; more than four drops fade the oldest
    {
        Settings s = wet(kModeDrop); s.timeMs = 100.0; s.gravity = 0.5; s.bounces = 6; s.dampDb = 0.0; s.sliceMs = 20.0; s.trigger = kTrigButton; s.feedback = 50.0;
        Engine e; e.setSettings(s); e.prepare(sr, 512);
        std::vector<float> L((size_t)(sr * 3.0), 0.0f), R = L;
        for (int i = 0; i < 960; ++i) L[(size_t)i] = R[(size_t)i] = 0.7f;
        e.fire();
        run(e, L, R, 256);
        double t[kMaxBounces + 2]; bounceTimes(0.1, 0.5, 6, 8.0, t);
        const double second = t[6] + 0.1;   // the re-drop: the first hit again, one T after the last
        const double a1 = rms(L, (size_t)((t[1] + 0.003) * sr), (size_t)((t[1] + 0.015) * sr));
        const double a2 = rms(L, (size_t)((second + 0.003) * sr), (size_t)((second + 0.015) * sr));
        const double a3 = rms(L, (size_t)((second + t[1] + 0.003) * sr), (size_t)((second + t[1] + 0.015) * sr));
        CHECK(a2 > 0.0 && std::fabs(db(a2 / a1) + 6.0) < 1.0 && std::fabs(db(a3 / a1) + 6.0) < 1.0, "drop feedback 50%%: the bounce comes back %.1f dB down (want -6), again from the first hit", db(a2 / a1));
        Engine p; p.setSettings(wet(kModeDrop)); Settings q = wet(kModeDrop); q.trigger = kTrigButton; q.timeMs = 300; q.sliceMs = 200; p.setSettings(q); p.prepare(sr, 512);
        std::vector<float> a((size_t)(sr * 0.4), 0.0f), b = a;
        for (size_t i = 0; i < a.size(); ++i) a[i] = b[i] = 0.3f * std::sin((float)i * 0.05f);
        int maxLive = 0; bool finite = true, den = false;
        for (int k = 0; k < 7; ++k)
        {
            p.fire();
            std::vector<float> x(a.begin() + k * 1000, a.begin() + k * 1000 + 1000), y = x;
            p.process(x.data(), y.data(), 1000);
            maxLive = std::max(maxLive, p.liveDrops());
            finite = finite && !bad(x, den);
        }
        CHECK(maxLive <= 4 && finite, "polyphony: seven drops in a row, at most %d sounding at once", maxLive);
    }
    // ---- block-size independence: the same output for every block size
    {
        Settings s; s.mode = kModeDrop; s.trigger = kTrigTransient; s.thresholdDb = -18; s.timeMs = 150; s.gravity = 0.8; s.bounces = 30; s.dampDb = 1.0; s.pitch = 1.0; s.tone = 30; s.spread = 80; s.jitter = 40; s.sliceMs = 70; s.feedback = 30; s.mix = 70;
        Settings t = s; t.mode = kModeTaps; t.feedback = 40; t.jitter = 50;
        for (int pass = 0; pass < 2; ++pass)
        {
            const int n = (int)(sr * 6.0);
            std::vector<float> ref;
            double worst = 0;
            for (int block : { 1, 7, 64, 333, 4096 })
            {
                Engine e; e.setSettings(pass ? t : s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R((size_t)n);
                for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i) % mix.size()]; R[(size_t)i] = mix[(size_t)(2 * i + 1) % mix.size()]; }
                run(e, L, R, block);
                if (ref.empty()) ref = L;
                else for (int i = 0; i < n; ++i) worst = std::max(worst, (double)std::fabs(L[(size_t)i] - ref[(size_t)i]));
            }
            CHECK(worst == 0.0, "%s: block sizes 1, 7, 64, 333 and 4096 give the same output (max difference %.2g)", pass ? "taps" : "drop", worst);
        }
    }
    // ---- random settings, tempo and mode changes, absurd input: finite, bounded, no denormals
    {
        Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
        std::mt19937 rng(42);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        bool finite = true, den = false; double peak = 0; size_t pos = 0;
        std::vector<float> L(1300), R(1300);
        for (int it = 0; it < 2500; ++it)
        {
            if (it % 5 == 0)
            {
                s.mode = (int)(u(rng) * 2); s.timeMs = 10.0 * std::pow(400.0, u(rng)); s.sync = u(rng) < 0.3; s.note = (int)(u(rng) * kNumNotes);
                s.gravity = 0.5 + 0.48 * u(rng); s.rise = u(rng) < 0.3; s.bounces = 2 + (int)(u(rng) * 63); s.dampDb = 12.0 * u(rng);
                s.pitch = -12.0 + 24.0 * u(rng); s.tone = -100.0 + 200.0 * u(rng); s.spread = 100.0 * u(rng); s.jitter = 100.0 * u(rng);
                s.sliceMs = 5.0 * std::pow(100.0, u(rng)); s.fit = u(rng) < 0.7; s.trigger = (int)(u(rng) * 4); s.thresholdDb = -60.0 * u(rng);
                s.every = (int)(u(rng) * kNumNotes); s.feedback = 90.0 * u(rng); s.mix = 100.0 * u(rng); s.outDb = -24.0 + 24.0 * u(rng);
                e.setSettings(s);
            }
            if (it % 11 == 3) { Transport t; t.bpm = 40.0 + u(rng) * 200.0; t.ppq = u(rng) * 64.0; t.valid = u(rng) < 0.6; t.playing = u(rng) < 0.7; e.setTransport(t); }
            if (it % 7 == 2) e.fire((float)u(rng));
            if (it % 9 == 4) e.noteOn((float)u(rng));
            const int bs = 1 + (int)(u(rng) * 1290);
            for (int i = 0; i < bs; ++i)
            {
                L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                if (it == 1200 && i == 0) L[0] = 1000.0f;   // absurd (but finite) input
            }
            e.process(L.data(), R.data(), bs);
            std::vector<float> a(L.begin(), L.begin() + bs), b(R.begin(), R.begin() + bs);
            finite = finite && !bad(a, den) && !bad(b, den);
            if (it < 1200) for (int i = 0; i < bs; ++i) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
        }
        CHECK(finite && !den && peak < 1000.0, "random settings, tempo and triggers: finite and bounded, no denormals (peak %.1f; then a +60 dB input spike, still finite)", peak);
        // a long decay: the loop at its highest feedback runs out to silence without denormals
        Settings q = wet(kModeTaps); q.feedback = 90.0; q.timeMs = 10.0; q.gravity = 0.5; q.bounces = 64; q.dampDb = 12.0;
        Engine f; f.setSettings(q); f.prepare(sr, 512);
        std::vector<float> x((size_t)(sr * 60.0), 0.0f), y = x;
        x[0] = y[0] = 1.0f;
        bool d2 = false; bool nan2 = false;
        for (size_t off = 0; off < x.size(); off += 4800)
        {
            f.process(x.data() + off, y.data() + off, 4800);
            std::vector<float> c(x.begin() + (long)off, x.begin() + (long)off + 4800);
            nan2 = nan2 || bad(c, d2);
        }
        CHECK(!nan2 && !d2, "feedback 90%% for 60 s: finite, no denormals (last second at %.0f dBFS)", db(rms(x, x.size() - (size_t)sr, x.size())));
    }
    // ---- CPU
    {
        const int n = (int)(sr * 4.0);
        for (int mode = 0; mode < kNumModes; ++mode)
        {
            Settings s; s.mode = mode; s.bounces = 64; s.gravity = 0.95; s.timeMs = 60; s.feedback = 40; s.trigger = kTrigSync; s.every = kNote16; s.sliceMs = 300; s.fit = false; s.tone = 40; s.pitch = 0.5;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n), R((size_t)n);
            for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            const auto t0 = std::chrono::steady_clock::now();
            run(e, L, R, 512);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: %-5s %.1f %% of one core (worst-case settings)\n", kModeNames[mode], 100.0 * secs / 4.0);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
