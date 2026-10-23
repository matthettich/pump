// pump EQ engine: measured responses (impulse -> FFT) against the analog target, in zero-latency and
// linear-phase modes; latency; mid/side placement; dynamic band behaviour; parameter-change smoothness.
#include "../plugins/eq/EQEngine.hpp"
#include <cstdio>
#include <chrono>
#include <limits>
#include <random>
#include <thread>

using namespace pump;
using namespace pump::eq;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static void setupBands(Engine& e, double sr, int mode)
{
    e.prepare(sr, 512);
    BandSpec a; a.shape = Shape::Bell; a.freq = 1000; a.gainDb = 6; a.q = 1.0; a.slope = 12;
    BandSpec b; b.shape = Shape::HighShelf; b.freq = 8000; b.gainDb = -4; b.q = 0.707; b.slope = 12;
    BandSpec c; c.shape = Shape::LowCut; c.freq = 40; c.q = 0.707; c.slope = 24;
    BandSpec d; d.shape = Shape::Bell; d.freq = 15000; d.gainDb = 3; d.q = 0.7; d.slope = 12;
    e.setBand(0, true, false, a, kStereo, false, 0, 0);
    e.setBand(1, true, false, b, kStereo, false, 0, 0);
    e.setBand(2, true, false, c, kStereo, false, 0, 0);
    e.setBand(3, true, false, d, kStereo, false, 0, 0);
    e.setPhaseMode(mode, 2);
    e.finishPending();   // the switch is asynchronous in a running plugin
}

// impulse response of the engine (left channel), then magnitude at f
struct IR { std::vector<float> L, R; };
static IR impulse(Engine& e, int len, float inL = 1.0f, float inR = 1.0f)
{
    IR ir; ir.L.assign((size_t)len, 0.0f); ir.R.assign((size_t)len, 0.0f);
    ir.L[0] = inL; ir.R[0] = inR;
    for (int off = 0; off < len; off += 256) e.process(ir.L.data() + off, ir.R.data() + off, std::min(256, len - off));
    return ir;
}
static double magDb(const std::vector<float>& h, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t n = 0; n < h.size(); ++n) { re += h[n] * std::cos(kTwoPi * f * n / sr); im -= h[n] * std::sin(kTwoPi * f * n / sr); }
    return 10 * std::log10(re * re + im * im + 1e-30);
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        for (int mode = 0; mode <= 1; ++mode)
        {
            Engine e; setupBands(e, sr, mode);
            // let the coefficient ramps settle, then measure
            { std::vector<float> z(4096, 0.0f), z2(4096, 0.0f); e.process(z.data(), z2.data(), 4096); }
            const int len = mode ? 131072 : 65536;
            IR ir = impulse(e, len);
            double worst = 0, wf = 0;
            for (int i = 0; i <= 60; ++i)
            {
                const double f = 20.0 * std::pow(std::min(20000.0, 0.45 * sr) / 20.0, i / 60.0);
                const double target = e.responseDb(f, 0);
                if (target < -24) continue;
                const double m = magDb(ir.L, f, sr);
                if (std::fabs(m - target) > worst) { worst = std::fabs(m - target); wf = f; }
            }
            CHECK(worst < (mode ? 0.05 : 0.6), "%s: worst deviation from the analog target %.3f dB (at %.0f Hz)", mode ? "linear" : "zero-latency", worst, wf);
            if (mode == 1)
            {
                int pk = 0; float pv = 0;
                for (int i = 0; i < len; ++i) if (std::fabs(ir.L[(size_t)i]) > pv) { pv = std::fabs(ir.L[(size_t)i]); pk = i; }
                // symmetric around the latency
                double asym = 0, en = 0;
                const int lat = e.latency();
                for (int k = 1; k < 2000; ++k) { asym += std::pow(ir.L[(size_t)(lat + k)] - ir.L[(size_t)(lat - k)], 2); en += std::pow(ir.L[(size_t)(lat + k)], 2); }
                CHECK(pk == lat && asym / en < 1e-6, "linear: peak at %d, reported latency %d, asymmetry %.1e (linear phase)", pk, lat, asym / std::max(en, 1e-30));
            }
        }
    }

    // mid/side: a side-only cut must leave a mono signal untouched and change a side-only signal
    for (int mode = 0; mode <= 1; ++mode)
    {
        Engine e; e.prepare(48000, 512);
        BandSpec s; s.shape = Shape::Bell; s.freq = 2000; s.gainDb = -12; s.q = 1.0; s.slope = 12;
        e.setBand(0, true, false, s, kSide, false, 0, 0);
        e.setPhaseMode(mode, 1);
        e.finishPending();
        { std::vector<float> z(4096, 0.0f), z2(4096, 0.0f); e.process(z.data(), z2.data(), 4096); }
        const int len = mode ? 65536 : 32768;
        IR mono = impulse(e, len, 1.0f, 1.0f);
        Engine e2; e2.prepare(48000, 512); e2.setBand(0, true, false, s, kSide, false, 0, 0); e2.setPhaseMode(mode, 1); e2.finishPending();
        { std::vector<float> z(4096, 0.0f), z2(4096, 0.0f); e2.process(z.data(), z2.data(), 4096); }
        IR side = impulse(e2, len, 1.0f, -1.0f);
        const double m2k = magDb(mono.L, 2000, 48000), s2k = magDb(side.L, 2000, 48000), s200 = magDb(side.L, 200, 48000);
        CHECK(std::fabs(m2k) < 0.05 && std::fabs(s2k + 12) < 0.3 && std::fabs(s200) < 0.5,
              "%s mid/side: mono at 2k %.2f dB, side at 2k %.2f dB, side at 200 Hz %.2f dB", mode ? "linear" : "zero-latency", m2k, s2k, s200);
    }

    // dynamic band: a loud 3 kHz tone above threshold gets cut by about the range; a quiet one doesn't
    {
        for (float level : { -6.0f, -50.0f })
        {
            Engine e; e.prepare(48000, 512);
            BandSpec s; s.shape = Shape::Bell; s.freq = 3000; s.gainDb = 0; s.q = 1.5; s.slope = 12;
            e.setBand(0, true, false, s, kStereo, true, -6.0f, -30.0f);
            e.setDynTimes(5, 80);
            const int T = 48000;
            std::vector<float> L(T), R(T);
            const float a = (float)dbToGain(level);
            for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = a * (float)std::sin(kTwoPi * 3000.0 * i / 48000.0);
            for (int off = 0; off < T; off += 256) e.process(L.data() + off, R.data() + off, std::min(256, T - off));
            double ein = 0, eout = 0;
            for (int i = T / 2; i < T; ++i) { eout += L[(size_t)i] * L[(size_t)i]; ein += std::pow(a * std::sin(kTwoPi * 3000.0 * i / 48000.0), 2); }
            const double gdb = 10 * std::log10(eout / ein);
            if (level > -20) CHECK(gdb < -5.0 && gdb > -6.6, "dynamic band, loud tone: %.2f dB (range -6)", gdb);
            else CHECK(std::fabs(gdb) < 0.1, "dynamic band, quiet tone: %.2f dB (untouched)", gdb);
        }
    }

    // smoothness: sweeping a band's frequency every block must not click (no sample-to-sample jump > the signal's own)
    {
        Engine e; e.prepare(48000, 512);
        BandSpec s; s.shape = Shape::Bell; s.gainDb = 12; s.q = 2.0; s.slope = 12;
        const int T = 48000;
        std::vector<float> L(T), R(T);
        for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = 0.25f * (float)std::sin(kTwoPi * 440.0 * i / 48000.0);
        double maxJump = 0;
        float prev = 0;
        for (int off = 0; off < T; off += 128)
        {
            s.freq = 200.0 * std::pow(10.0, 2.0 * off / T);
            e.setBand(0, true, false, s, kStereo, false, 0, 0);
            const int n = std::min(128, T - off);
            e.process(L.data() + off, R.data() + off, n);
            for (int i = off; i < off + n; ++i) { if (off > 0 || i > 0) maxJump = std::max(maxJump, (double)std::fabs(L[(size_t)i] - prev)); prev = L[(size_t)i]; }
        }
        // the 440 Hz sine with up to +12 dB moves at most ~ 2*pi*440/48000 * 1.0 per sample
        CHECK(maxJump < 0.08, "sweeping a +12 dB bell 200 Hz -> 20 kHz: largest sample step %.4f (no clicks)", maxJump);
    }
    // stress: switch phase mode and quality while bands keep changing (worker thread rebuilding kernels)
    {
        Engine e; e.prepare(48000, 512);
        std::mt19937 rng(3);
        std::vector<float> L(512), R(512);
        bool finite = true;
        for (int blk = 0; blk < 600; ++blk)
        {
            BandSpec s; s.shape = (Shape)(rng() % 8); s.freq = 30.0 * std::pow(600.0, (rng() % 1000) / 1000.0); s.gainDb = (int)(rng() % 25) - 12; s.q = 0.3 + (rng() % 100) / 20.0; s.slope = 12;
            e.setBand((int)(rng() % 6), true, false, s, (int)(rng() % 5), (rng() % 3) == 0, -6, -30);
            if (blk % 37 == 0) e.setPhaseMode((int)(rng() % 2), (int)(rng() % 3));
            for (int i = 0; i < 512; ++i) L[(size_t)i] = R[(size_t)i] = 0.2f * (float)std::sin(0.05 * (blk * 512 + i));
            e.process(L.data(), R.data(), 512);
            for (float v : L) if (!std::isfinite(v) || std::fabs(v) > 100.0f) finite = false;
        }
        CHECK(finite, "600 blocks of random band changes with phase/quality switches: output finite and bounded");
    }

    // --- regressions from the 2026-10 audit ---

    // a band created at 0 dB and then cut must sound like a band designed at that gain (matching points re-chosen)
    {
        for (double sr : { 48000.0, 96000.0 })
        {
            BandSpec s; s.shape = Shape::Bell; s.freq = 30; s.gainDb = 0; s.q = 1.5; s.slope = 12;
            Engine e; e.prepare(sr, 512);
            e.setBand(0, true, false, s, kStereo, false, 0, 0);
            { std::vector<float> z(2048, 0.0f), z2(2048, 0.0f); e.process(z.data(), z2.data(), 2048); }
            s.gainDb = -12; e.setBand(0, true, false, s, kStereo, false, 0, 0);
            for (int k = 0; k < 8; ++k) { std::vector<float> z(512, 0.0f), z2(512, 0.0f); e.process(z.data(), z2.data(), 512); }   // settle
            IR ir = impulse(e, 1 << 17);
            const double m = magDb(ir.L, 30.0, sr);
            CHECK(std::fabs(m + 12.0) < 0.3, "bell 30 Hz made at 0 dB then set to -12 dB @ %.0f: %.2f dB at 30 Hz", sr, m);
        }
    }
    // one NaN input sample: no non-finite output, and the dynamic band keeps working (both modes)
    {
        for (int mode = 0; mode <= 1; ++mode)
        {
            Engine e; e.prepare(48000, 512);
            BandSpec s; s.shape = Shape::Bell; s.freq = 3000; s.gainDb = 0; s.q = 1.5; s.slope = 12;
            e.setBand(0, true, false, s, kStereo, true, -6.0f, -30.0f);
            e.setPhaseMode(mode, 1); e.finishPending();
            const int T = 48000 * 2;
            std::vector<float> L(T), R(T);
            for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = 0.5f * (float)std::sin(kTwoPi * 3000.0 * i / 48000.0);
            L[(size_t)T / 4] = std::numeric_limits<float>::quiet_NaN();
            int bad = 0;
            for (int off = 0; off < T; off += 256) e.process(L.data() + off, R.data() + off, std::min(256, T - off));
            for (float v : L) if (!std::isfinite(v)) ++bad;
            double ein = 0, eout = 0;
            for (int i = T - 24000; i < T; ++i) { eout += L[(size_t)i] * L[(size_t)i]; ein += std::pow(0.5 * std::sin(kTwoPi * 3000.0 * i / 48000.0), 2); }
            const double g = 10 * std::log10(eout / ein);
            CHECK(bad == 0 && g < -5.0 && g > -6.6, "%s: one NaN input sample -> %d non-finite out, dynamic band still %.2f dB", mode ? "linear" : "zero latency", bad, g);
        }
    }
    // switching a band's bypass while audio plays fades (no click); switching it back on after silence doesn't burst
    {
        Engine e; e.prepare(48000, 512);
        BandSpec s; s.shape = Shape::Bell; s.freq = 80; s.gainDb = 12; s.q = 2.0; s.slope = 12;
        e.setBand(0, true, false, s, kStereo, false, 0, 0);
        const int T = 48000;
        std::vector<float> L(T), R(T);
        for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = 0.1f * (float)std::sin(kTwoPi * 80.0 * i / 48000.0);
        double maxStep = 0; float prev = 0;
        for (int off = 0; off < T; off += 256)
        {
            if (off == 24064) e.setBand(0, true, true, s, kStereo, false, 0, 0);
            e.process(L.data() + off, R.data() + off, std::min(256, T - off));
            for (int i = off; i < std::min(T, off + 256); ++i) { if (i > 4800) maxStep = std::max(maxStep, (double)std::fabs(L[(size_t)i] - prev)); prev = L[(size_t)i]; }
        }
        CHECK(maxStep < 0.006, "band bypass switched mid-signal: largest sample step %.4f (the tone's own is ~0.0042)", maxStep);
        { std::vector<float> z(48000, 0.0f), z2(48000, 0.0f); e.process(z.data(), z2.data(), 48000); }
        e.setBand(0, true, false, s, kStereo, false, 0, 0);
        std::vector<float> z(4800, 0.0f), z2(4800, 0.0f); e.process(z.data(), z2.data(), 4800);
        float pk = 0; for (float v : z) pk = std::max(pk, std::fabs(v));
        CHECK(pk == 0.0f, "band switched back on after silence: output with silent input %.2e (no stale-state burst)", pk);
    }
    // the switch to linear phase happens off the audio thread, a few blocks later, with the right latency
    {
        Engine e; e.prepare(48000, 512);
        e.setPhaseMode(1, 1);
        std::vector<float> z(512, 0.0f), z2(512, 0.0f);
        int blocks = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (e.latency() == 0 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) { e.process(z.data(), z2.data(), 512); ++blocks; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK(e.latency() == 8192 / 2 - 1 + 512, "async switch to linear phase: latency %d after %d blocks", e.latency(), blocks);
        e.setPhaseMode(1, 1);   // same again: nothing to do
        e.setPhaseMode(0, 1); e.process(z.data(), z2.data(), 512);
        CHECK(e.latency() == 0, "back to zero latency at once: latency %d", e.latency());
    }
    // typed values: no NaN/inf from text, either decimal separator, NaN never survives clamp
    {
        const ParamSpec& g = params()[(size_t)bandParam(0, kGain)];
        float v = 0.0f;
        CHECK(!g.parse("nan", v) && !g.parse("-nan", v), "typing 'nan' is rejected");
        CHECK(g.parse("1,5", v) && std::fabs(v - 1.5f) < 1e-6f && g.parse("-2.25", v) && std::fabs(v + 2.25f) < 1e-6f, "'1,5' and '-2.25' parse (locale-independent)");
        CHECK(g.clamp(std::numeric_limits<float>::quiet_NaN()) == g.def, "clamp(NaN) gives the default");
        const ParamSpec& f = params()[(size_t)bandParam(0, kFreq)];
        CHECK(f.parse("C#3+nan", v) && std::isfinite(v), "'C#3+nan' doesn't produce NaN (%.1f Hz)", v);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
