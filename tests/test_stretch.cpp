// pump STRETCH: the phase-randomised time stretch (a port of Thunder Plus's Extreme stretch). Mix 0 and bypass null
// with the input exactly; the level of the wash on noise and on a tone is the input's (within 1.5 dB) at every window
// and rate; a tone stays at its pitch in every window; a short burst is smeared by about the stretch factor; Freeze
// holds a moment after the input has changed and un-freezing returns to the live sound; the same input renders the same
// output whatever the block size; the read head falling about 10 s behind jumps to the newest sound without a click;
// after silence a new sound starts the wash promptly; a song start requests a jump (and an absent transport is fine);
// Tone tilts the way its formula says; random settings, hot and NaN input stay finite; and the work per block is
// bounded (every frame's units spread over its hop: at most one transform per 64-sample block with the largest
// window). 44.1, 48 and 96 kHz where it matters (192 kHz briefly). The numeric comparison with the web build's code
// is a scratch script (see docs/HANDOVER.md), not part of this test.
#include <cstdlib>
#include "../plugins/stretch/StretchEngine.hpp"
#include "dsp/Bypass.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::stretch;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

struct Sig { std::vector<float> L, R; };

static Settings mk(double amt, int win, double mix = 1.0, double tone = 0.0, bool freeze = false, double outDb = 0.0)
{
    Settings s; s.stretch = (float)amt; s.window = win; s.mix = (float)mix; s.tone = (float)tone; s.freeze = freeze; s.outDb = (float)outDb;
    return s;
}
static void run(Engine& e, Sig& x, int block = 256)
{
    const int n = (int)x.L.size();
    for (int off = 0; off < n; off += block) { const int m = std::min(block, n - off); e.process(x.L.data() + off, x.R.data() + off, m); }
}
static Sig noise(double sr, double seconds, float amp, unsigned seed = 1)
{
    std::mt19937 g(seed); std::normal_distribution<float> nd(0.0f, 1.0f);
    Sig s; const int n = (int)(sr * seconds); s.L.resize((size_t)n); s.R.resize((size_t)n);
    for (int i = 0; i < n; ++i) { s.L[(size_t)i] = amp * nd(g); s.R[(size_t)i] = amp * nd(g); }
    return s;
}
static Sig tone(double sr, double seconds, double hz, float amp)
{
    Sig s; const int n = (int)(sr * seconds); s.L.resize((size_t)n); s.R.resize((size_t)n);
    for (int i = 0; i < n; ++i) { const float v = amp * (float)std::sin(kTwoPi * hz * i / sr); s.L[(size_t)i] = v; s.R[(size_t)i] = 0.8f * v; }
    return s;
}
static void appendSig(Sig& a, const Sig& b) { a.L.insert(a.L.end(), b.L.begin(), b.L.end()); a.R.insert(a.R.end(), b.R.begin(), b.R.end()); }
static Sig silence(double sr, double seconds) { Sig s; s.L.assign((size_t)(sr * seconds), 0.0f); s.R = s.L; return s; }
static double rms(const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0.0; b = std::min(b, x.size());
    for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / (double)std::max<size_t>(1, b - a));
}
static double db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }
// the strongest frequency in x[a, a + n) (Hann window, n a power of two)
static double peakHz(const std::vector<float>& x, size_t a, int n, double sr, double* peakDb = nullptr)
{
    RealFFT fft; fft.setup(n);
    AlignedBuffer in((size_t)n), sp((size_t)n);
    for (int i = 0; i < n; ++i) in[(size_t)i] = x[a + (size_t)i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / n));
    fft.forward(in.data(), sp.data());
    int best = 1; float bv = 0.0f;
    for (int k = 1; k < n / 2; ++k) { const float p = sp[(size_t)(2 * k)] * sp[(size_t)(2 * k)] + sp[(size_t)(2 * k + 1)] * sp[(size_t)(2 * k + 1)]; if (p > bv) { bv = p; best = k; } }
    if (peakDb) *peakDb = 10.0 * std::log10(bv + 1e-30);
    return best * sr / n;
}
// the power (linear) of x[a, a + n) between lo and hi Hz
static double bandPower(const std::vector<float>& x, size_t a, int n, double sr, double lo, double hi)
{
    RealFFT fft; fft.setup(n);
    AlignedBuffer in((size_t)n), sp((size_t)n);
    for (int i = 0; i < n; ++i) in[(size_t)i] = x[a + (size_t)i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / n));
    fft.forward(in.data(), sp.data());
    double p = 0.0;
    for (int k = 1; k < n / 2; ++k) { const double f = k * sr / n; if (f >= lo && f < hi) p += (double)sp[(size_t)(2 * k)] * sp[(size_t)(2 * k)] + (double)sp[(size_t)(2 * k + 1)] * sp[(size_t)(2 * k + 1)]; }
    return p;
}

static const double kRates[3] = { 44100.0, 48000.0, 96000.0 };

static void testNullAndBypass()
{
    printf("Mix 0 and bypass null with the input exactly, latency 0\n");
    for (double sr : kRates)
    {
        Engine e; e.prepare(sr, 512); e.setSettings(mk(0.5, 2, 0.0));
        CHECK(e.latency() == 0, "%.0f Hz: latency %d", sr, e.latency());
        Sig x = noise(sr, 6.0, 0.3f), y = x;
        run(e, y, 300);
        double worst = 0.0;
        for (size_t i = 0; i < x.L.size(); ++i) worst = std::max(worst, (double)std::max(std::fabs(x.L[i] - y.L[i]), std::fabs(x.R[i] - y.R[i])));
        CHECK(worst == 0.0, "%.0f Hz: mix 0 is the input exactly (max difference %g)", sr, worst);
        // Mix glides 100 % -> 0 and then is exact
        Engine e2; e2.prepare(sr, 512); e2.setSettings(mk(0.5, 2, 1.0));
        Sig a = noise(sr, 8.0, 0.3f, 3), b = a;
        const int half = (int)(sr * 3.0);
        Sig first; first.L.assign(b.L.begin(), b.L.begin() + half); first.R.assign(b.R.begin(), b.R.begin() + half);
        run(e2, first, 256);
        e2.setSettings(mk(0.5, 2, 0.0));
        Sig second; second.L.assign(b.L.begin() + half, b.L.end()); second.R.assign(b.R.begin() + half, b.R.end());
        run(e2, second, 256);
        double w2 = 0.0; const size_t from = (size_t)(sr * 1.0);   // after the glide (about 10 ms)
        for (size_t i = from; i < second.L.size(); ++i) w2 = std::max(w2, (double)std::fabs(a.L[(size_t)half + i] - second.L[i]));
        CHECK(w2 == 0.0, "%.0f Hz: after the glide down to Mix 0 the input is exact (max difference %g)", sr, w2);
        // bypass, as the plugin does it
        Engine e3; e3.prepare(sr, 512); e3.setSettings(mk(0.7, 3, 1.0));
        BypassMixer bp; bp.prepare(sr, 1 << 15, 8192); bp.setLatency(0);
        Sig c = noise(sr, 4.0, 0.3f, 5), d = c;
        double wb = 0.0;
        for (int off = 0; off < (int)c.L.size(); off += 256)
        {
            const int m = std::min(256, (int)c.L.size() - off);
            bp.capture(d.L.data() + off, d.R.data() + off, m);
            e3.process(d.L.data() + off, d.R.data() + off, m);
            bp.apply(d.L.data() + off, d.R.data() + off, m, off >= (int)sr);
            if (off >= (int)(sr * 1.5)) for (int i = 0; i < m; ++i) wb = std::max(wb, (double)std::fabs(c.L[(size_t)(off + i)] - d.L[(size_t)(off + i)]));
        }
        CHECK(wb == 0.0, "%.0f Hz: bypassed output is the input exactly (max difference %g)", sr, wb);
    }
}

// The wash of steady noise is as loud as the noise (x4/3 and the Hann windows: the same level as the web build), for
// every window, and a tone's wash too.
static void testLevel()
{
    printf("Level of the wash at Mix 100 %%\n");
    for (double sr : kRates)
        for (int win = 0; win < kNumWindows; ++win)
        {
            if (sr > 50000.0 && win != 0 && win != 4) continue;
            Engine e; e.prepare(sr, 512); e.setSettings(mk(0.5, win));
            Sig x = noise(sr, 22.0, 0.1f, 7), y = x;
            run(e, y, 480);
            const size_t a = (size_t)(sr * 14.0), b = (size_t)(sr * 22.0);
            const double d = db(rms(y.L, a, b) / rms(x.L, a, b));
            CHECK(std::fabs(d) < 1.5, "%.0f Hz, window %d, noise: wash %+.2f dB re the input", sr, win, d);
        }
    for (double sr : { 48000.0 })
        for (int win : { 0, 2, 4 })
        {
            Engine e; e.prepare(sr, 512); e.setSettings(mk(0.5, win));
            Sig x = tone(sr, 22.0, 440.0, 0.3f), y = x;
            run(e, y, 480);
            const size_t a = (size_t)(sr * 14.0), b = (size_t)(sr * 22.0);
            const double d = db(rms(y.L, a, b) / rms(x.L, a, b));
            CHECK(std::fabs(d) < 1.5, "%.0f Hz, window %d, tone: wash %+.2f dB re the input", sr, win, d);
        }
    // output gain is a plain gain
    Engine e; e.prepare(48000.0, 512); e.setSettings(mk(0.5, 2, 1.0, 0.0, false, 6.0));
    Sig x = noise(48000.0, 16.0, 0.1f, 9), y = x;
    run(e, y, 480);
    Engine e0; e0.prepare(48000.0, 512); e0.setSettings(mk(0.5, 2, 1.0));
    Sig z = x; run(e0, z, 480);
    const double g = db(rms(y.L, 600000, 700000) / rms(z.L, 600000, 700000));
    CHECK(std::fabs(g - 6.0206) < 0.05, "output gain +6 dB gives %+.3f dB", g);
}

static void testPitch()
{
    printf("A tone stays at its pitch in every window\n");
    for (double sr : kRates)
        for (int win = 0; win < kNumWindows; ++win)
        {
            if (sr > 50000.0 && win != 0 && win != 2 && win != 4) continue;
            const double hz = 1234.5;
            Engine e; e.prepare(sr, 512); e.setSettings(mk(0.6, win));
            Sig x = tone(sr, 20.0, hz, 0.3f), y = x;
            run(e, y, 512);
            const int N = (int)std::lround(sr * 0.34) | 0;
            int n = 1; while (n < N) n <<= 1;
            const double f = peakHz(y.L, (size_t)(sr * 15.0), n, sr);
            const double winHz = sr / (e.windowSamples(win));   // the stretch window's own bin width
            CHECK(std::fabs(f - hz) <= std::max(1.5 * winHz, 2.0 * sr / n), "%.0f Hz, window %d (%d samples): peak at %.1f Hz (tone %.1f)", sr, win, e.windowSamples(win), f, hz);
        }
}

// the duration (s) over which the 20 ms energy envelope of x is within 12 dB of its peak
static double smearSeconds(const std::vector<float>& x, size_t from, double sr)
{
    const int blk = (int)(sr * 0.02);
    std::vector<double> env;
    for (size_t i = from; i + (size_t)blk <= x.size(); i += (size_t)blk) env.push_back(rms(x, i, i + (size_t)blk));
    double pk = 0.0; for (double v : env) pk = std::max(pk, v);
    int first = -1, last = -1;
    for (size_t i = 0; i < env.size(); ++i) if (env[i] > pk * 0.25) { if (first < 0) first = (int)i; last = (int)i; }
    return first < 0 ? 0.0 : (last - first + 1) * 0.02;
}
static void testSmear()
{
    printf("A burst is smeared by about the stretch factor\n");
    for (double sr : kRates)
    {
        double dur[2] = {}, fac[2] = {};
        const double amts[2] = { 0.3, 0.6 };
        for (int k = 0; k < 2; ++k)
        {
            Engine e; e.prepare(sr, 512); e.setSettings(mk(amts[k], 1));
            Sig x = silence(sr, 14.0);
            Sig b = noise(sr, 0.4, 0.3f, 11);
            appendSig(x, b);
            appendSig(x, silence(sr, 14.0));
            Sig y = x;
            run(e, y, 256);
            fac[k] = stretchFactor(amts[k]);
            dur[k] = smearSeconds(y.L, (size_t)(sr * 13.5), sr);
            CHECK(dur[k] > 0.5 * fac[k] * 0.4 && dur[k] < 1.5 * fac[k] * 0.4 + 0.3, "%.0f Hz: a 0.4 s burst at x%.1f lasts %.2f s (stretch x burst = %.2f s)", sr, fac[k], dur[k], fac[k] * 0.4);
        }
        const double ratio = dur[1] / dur[0], want = fac[1] / fac[0];
        CHECK(ratio > 0.7 * want && ratio < 1.3 * want, "%.0f Hz: x%.1f against x%.1f: durations %.2f, wanted %.2f", sr, fac[1], fac[0], ratio, want);
    }
}

static void testFreeze()
{
    printf("Freeze holds a moment, un-freezing returns to the live sound\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        Engine e; e.prepare(sr, 512);
        Sig x = tone(sr, 6.0, 500.0, 0.3f); appendSig(x, tone(sr, 26.0, 2000.0, 0.3f));
        auto seg = [&](double a, double b, const Settings& s) {
            Sig p; p.L.assign(x.L.begin() + (long)(sr * a), x.L.begin() + (long)(sr * b)); p.R.assign(x.R.begin() + (long)(sr * a), x.R.begin() + (long)(sr * b));
            e.setSettings(s); run(e, p, 200); return p;
        };
        Sig s1 = seg(0, 5.0, mk(0.5, 3));               // live
        Sig s2 = seg(5.0, 8.0, mk(0.5, 3, 1.0, 0.0, true));   // freeze on during the 500 Hz tone
        Sig s3 = seg(8.0, 18.0, mk(0.5, 3, 1.0, 0.0, true));  // the input is 2 kHz now: the frozen sound goes on
        const double f1 = peakHz(s3.L, (size_t)(sr * 8.0), 32768, sr);
        const double lvlFrozen = rms(s3.L, (size_t)(sr * 8.0), s3.L.size()), lvlLive = rms(s1.L, (size_t)(sr * 3.0), s1.L.size());
        CHECK(std::fabs(f1 - 500.0) < 8.0, "%.0f Hz: frozen for 10 s while the input is 2 kHz: the output is at %.1f Hz", sr, f1);
        CHECK(std::fabs(db(lvlFrozen / lvlLive)) < 2.0, "%.0f Hz: the frozen drone is %+.2f dB re the live wash", sr, db(lvlFrozen / lvlLive));
        CHECK(e.isFrozen(), "%.0f Hz: the engine reports frozen", sr);
        Sig s4 = seg(18.0, 28.0, mk(0.5, 3, 1.0, 0.0, false));   // un-freeze (the head is > 10 s behind: it jumps)
        const double f2 = peakHz(s4.L, (size_t)(sr * 6.0), 32768, sr);
        CHECK(std::fabs(f2 - 2000.0) < 8.0 && !e.isFrozen(), "%.0f Hz: after un-freezing the output follows the live 2 kHz tone (%.1f Hz)", sr, f2);
        // and the wash of the 500 Hz tone is gone for good
        double p5 = 0.0, p20 = 0.0;
        const size_t a = (size_t)(sr * 6.0);
        p5 = bandPower(s4.L, a, 32768, sr, 450.0, 550.0); p20 = bandPower(s4.L, a, 32768, sr, 1950.0, 2050.0);
        CHECK(p20 > 1000.0 * p5, "%.0f Hz: 500 Hz is %.0f dB under 2 kHz after un-freezing", sr, 10 * std::log10(p20 / std::max(p5, 1e-30)));
    }
    // frozen: the sound goes on after the input stops; not frozen it dies away
    {
        const double sr = 48000.0;
        Sig x = noise(sr, 8.0, 0.1f, 13); appendSig(x, silence(sr, 14.0));
        double lvl[2] = {};
        for (int frozen = 0; frozen < 2; ++frozen)
        {
            Engine e; e.prepare(sr, 512);
            Sig y = x;
            const int cut = (int)(sr * 6.0);
            for (int off = 0; off < (int)y.L.size(); off += 256)
            {
                if (off == 0) e.setSettings(mk(0.5, 3, 1.0));
                if (off >= cut && off < cut + 256) e.setSettings(mk(0.5, 3, 1.0, 0.0, frozen == 1));
                e.process(y.L.data() + off, y.R.data() + off, std::min(256, (int)y.L.size() - off));
            }
            lvl[frozen] = rms(y.L, (size_t)(sr * 18.0), (size_t)(sr * 22.0));
        }
        CHECK(db(lvl[1]) > -35.0 && db(lvl[1]) < -15.0, "noise stopped at 8 s: frozen it still sounds at %.1f dBFS 10 s later", db(lvl[1]));
        CHECK(db(lvl[0]) < db(lvl[1]) - 20.0, "not frozen it has died away (%.1f dBFS against %.1f)", db(lvl[0]), db(lvl[1]));
    }
}

static void testDeterministic()
{
    printf("The same input renders the same output, whatever the block size\n");
    const double sr = 48000.0;
    Sig x = noise(sr, 10.0, 0.2f, 17); appendSig(x, silence(sr, 2.0)); appendSig(x, tone(sr, 8.0, 300.0, 0.3f));
    Sig outs[4];
    const int blocks[4] = { 64, 64, 999, 4096 };
    for (int k = 0; k < 4; ++k)
    {
        Engine e; e.prepare(sr, 512);
        outs[k] = x;
        const int n = (int)x.L.size();
        for (int off = 0, i = 0; off < n; ++i)
        {
            const int m = std::min(blocks[k], n - off);
            if (i == 0) e.setSettings(mk(0.7, 2, 0.8, 0.3));
            if (off >= (int)(sr * 6.0) && off - blocks[k] < (int)(sr * 6.0)) e.setSettings(mk(0.7, 2, 0.8, 0.3, true));
            if (off >= (int)(sr * 12.0) && off - blocks[k] < (int)(sr * 12.0)) e.setSettings(mk(0.4, 3, 0.8, -0.3, false));
            e.process(outs[k].L.data() + off, outs[k].R.data() + off, m);
            off += m;
        }
    }
    CHECK(outs[0].L == outs[1].L && outs[0].R == outs[1].R, "two runs at 64: identical");
    double w = 0.0;
    for (int k = 2; k < 4; ++k) for (size_t i = 0; i < x.L.size(); ++i) w = std::max(w, (double)std::fabs(outs[0].L[i] - outs[k].L[i]));
    // (the settings change lands on a block boundary: with other block sizes it lands a few samples apart, so only the
    // first 6 s are compared bit for bit)
    double w6 = 0.0;
    for (int k = 2; k < 4; ++k) for (size_t i = 0; i < (size_t)(sr * 6.0); ++i) w6 = std::max(w6, (double)std::fabs(outs[0].L[i] - outs[k].L[i]));
    CHECK(w6 == 0.0, "blocks of 64, 999 and 4096: the first 6 s are identical (max difference %g)", w6);
    printf("  (whole run, settings landing on different samples: max difference %g)\n", w);
}

static void testJump()
{
    printf("Falling behind jumps to the newest sound, without a click\n");
    for (double sr : kRates)
    {
        Engine e; e.prepare(sr, 512); e.setSettings(mk(1.0, 2));   // x50: the head falls behind fast
        Sig x = tone(sr, 70.0, 220.0, 0.3f);
        // a slow swell so the output has no reason to be steady
        for (size_t i = 0; i < x.L.size(); ++i) { const float g = 0.6f + 0.4f * (float)std::sin(kTwoPi * 0.1 * i / sr); x.L[i] *= g; x.R[i] *= g; }
        Sig y = x;
        std::vector<uint32_t> jumpsAt;
        uint32_t last = 0;
        const int B = 256;
        for (int off = 0; off + B <= (int)y.L.size(); off += B)
        {
            e.process(y.L.data() + off, y.R.data() + off, B);
            if (e.jumps() != last) { last = e.jumps(); jumpsAt.push_back((uint32_t)off); }
        }
        CHECK(jumpsAt.size() >= 2, "%.0f Hz: %zu jumps in 70 s at x50 (about one per 10 s of falling behind)", sr, jumpsAt.size());
        bool finite = true; double dmax = 0.0;
        for (size_t i = (size_t)(sr * 2.0); i + 1 < y.L.size(); ++i) { if (!std::isfinite(y.L[i])) finite = false; dmax = std::max(dmax, (double)std::fabs(y.L[i + 1] - y.L[i])); }
        CHECK(finite, "%.0f Hz: every sample finite", sr);
        // the step between samples is never much more than a 220 Hz wash at this level can make
        double dnorm = 0.0;
        for (size_t i = (size_t)(sr * 2.0); i + 1 < y.L.size(); ++i) dnorm = std::max(dnorm, (double)std::fabs(y.L[i + 1] - y.L[i]));
        double dnear = 0.0;
        for (uint32_t j : jumpsAt) for (size_t i = j > (uint32_t)(sr * 1.5) ? j - (size_t)(sr * 1.5) : 0; i < j + (size_t)(sr * 2.5) && i + 1 < y.L.size(); ++i) dnear = std::max(dnear, (double)std::fabs(y.L[i + 1] - y.L[i]));
        const double expect = 2.0 * kPi * 220.0 / sr * 0.4 * 1.6;   // a generous bound: a 220 Hz sine of 0.4 peak
        CHECK(dnear < 1.5 * expect + 1e-4 && dnear <= 1.3 * dnorm + 1e-9, "%.0f Hz: largest sample step around the jumps %.4f (bound %.4f)", sr, dnear, 1.5 * expect);
        (void)dmax;
    }
    // the JUMP request and a song start
    {
        const double sr = 48000.0;
        Engine e; e.prepare(sr, 512); e.setSettings(mk(0.5, 2));
        Sig x = tone(sr, 8.0, 330.0, 0.3f);
        run(e, x, 256);
        const uint32_t j0 = e.jumps();
        e.requestJump();
        Sig z = tone(sr, 1.0, 330.0, 0.3f); run(e, z, 256);
        CHECK(e.jumps() == j0 + 1, "JUMP: one jump (%u -> %u)", j0, e.jumps());
        // a song start (stopped -> playing) and a jump back in the song
        Transport t; t.playing = false; t.valid = true; t.ppq = 40.0;
        e.setTransport(t); Sig z2 = tone(sr, 1.0, 330.0, 0.3f); run(e, z2, 256);
        const uint32_t j1 = e.jumps();
        t.playing = true; e.setTransport(t); run(e, z2, 256);
        CHECK(e.jumps() == j1 + 1, "song start: a jump (%u -> %u)", j1, e.jumps());
        t.ppq = 41.0; e.setTransport(t); run(e, z2, 256);
        CHECK(e.jumps() == j1 + 1, "playing on: no jump");
        t.ppq = 8.0; e.setTransport(t); run(e, z2, 256);
        CHECK(e.jumps() == j1 + 2, "a jump back in the song (loop): a jump (%u)", e.jumps());
        // no host transport at all
        Transport none; e.setTransport(none); run(e, z2, 256); e.setTransport(none); run(e, z2, 256);
        CHECK(e.jumps() == j1 + 2, "no transport: no jumps, no trouble");
    }
}

static void testSilenceThenSound()
{
    printf("After silence a new sound starts the wash promptly\n");
    for (double sr : kRates)
        for (int win : { 0, 2, 4 })
        {
            Engine e; e.prepare(sr, 512); e.setSettings(mk(0.7, win));
            Sig x = silence(sr, 20.0); Sig t = tone(sr, 6.0, 1000.0, 0.3f); appendSig(x, t);
            Sig y = x;
            run(e, y, 128);
            const size_t on = (size_t)(sr * 20.0);
            size_t first = 0;
            const int blk = (int)(sr * 0.01);
            for (size_t i = on; i + (size_t)blk < y.L.size(); i += (size_t)blk) if (rms(y.L, i, i + (size_t)blk) > 0.3 * 0.707 * 0.5) { first = i - on; break; }
            const double limit = 1.6 * e.windowSamples(win) / sr + 0.1;   // about half a window to take the sound in, a hop, and the window's fade-in
            CHECK(first > 0 && first / sr < limit, "%.0f Hz, window %d: the wash is half the tone's level %.2f s after it starts (limit %.2f s)", sr, win, first / sr, limit);
        }
}

static void testTone()
{
    printf("Tone tilts about 1 kHz\n");
    const double sr = 48000.0;
    double lvl[3] = {};
    const double tones[3] = { -1.0, 0.0, 1.0 };
    for (int k = 0; k < 3; ++k)
    {
        Engine e; e.prepare(sr, 512); e.setSettings(mk(0.5, 2, 1.0, tones[k]));
        Sig x = noise(sr, 14.0, 0.1f, 19), y = x;
        run(e, y, 480);
        const double lo = bandPower(y.L, (size_t)(sr * 10.0), 32768, sr, 200.0, 300.0), hi = bandPower(y.L, (size_t)(sr * 10.0), 32768, sr, 3500.0, 4500.0);
        lvl[k] = 10.0 * std::log10(hi / lo);
    }
    // tone +1: (f / 1 kHz)^0.75 in amplitude: 4 kHz against 250 Hz is 16^0.75 = 8 (18 dB) over flat; the noise's own scatter is about 1 dB
    CHECK(std::fabs((lvl[2] - lvl[1]) - 18.06) < 1.5, "Tone +100 %%: 4 kHz against 250 Hz %+.1f dB over flat (formula 18.1)", lvl[2] - lvl[1]);
    CHECK(std::fabs((lvl[0] - lvl[1]) + 18.06) < 1.5, "Tone -100 %%: %+.1f dB under flat (formula -18.1)", lvl[0] - lvl[1]);
}

static void testSampleRates()
{
    printf("Window times do not change with the sample rate\n");
    const double rates[5] = { 44100.0, 48000.0, 96000.0, 176400.0, 192000.0 };
    for (int w = 0; w < kNumWindows; ++w)
    {
        double t[5];
        for (int r = 0; r < 5; ++r) { Engine e; e.prepare(rates[r], 512); t[r] = e.windowSamples(w) / rates[r]; }
        CHECK(std::fabs(t[2] - t[1]) < 0.00001 && std::fabs(t[3] / t[0] - 1.0) < 0.001 && std::fabs(t[4] - t[1]) < 0.00001 && std::fabs(t[0] / t[1] - 48.0 / 44.1) < 0.001,
              "window %d: %.1f ms at 48 kHz (%d samples), 96 kHz %d, 192 kHz %d, 44.1 kHz %d", w, t[1] * 1000.0, kWindowSamples[w], (int)std::lround(t[2] * 96000.0), (int)std::lround(t[4] * 192000.0), (int)std::lround(t[0] * 44100.0));
    }
    for (int r = 0; r < 5; ++r)
    {
        Engine e; e.prepare(rates[r], 512);
        CHECK(std::fabs(e.ringSamples() / rates[r] - 12.0) < 1e-4 && std::fabs(e.jumpBehindSeconds(e.windowSamples(2)) - 10.0) < 0.5, "%.0f Hz: ring %.3f s, jumps at %.2f s behind", rates[r], e.ringSamples() / rates[r], e.jumpBehindSeconds(e.windowSamples(2)));
    }
    // a short run at 192 kHz: the wash is there and finite
    Engine e; e.prepare(192000.0, 512); e.setSettings(mk(0.5, 2));
    Sig x = noise(192000.0, 8.0, 0.1f, 23), y = x;
    run(e, y, 512);
    const double d = db(rms(y.L, (size_t)(192000.0 * 5.0), y.L.size()) / rms(x.L, (size_t)(192000.0 * 5.0), x.L.size()));
    CHECK(std::fabs(d) < 1.5, "192 kHz: noise wash %+.2f dB re the input", d);
}

// Random settings every few blocks, odd block sizes, hot and NaN input: always finite and bounded.
static void testStress()
{
    printf("Random settings, odd blocks, hot and NaN input stay finite\n");
    for (double sr : kRates)
    {
        Engine e; e.prepare(sr, 512);
        std::mt19937 g((unsigned)sr);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        std::normal_distribution<float> nd(0.0f, 1.0f);
        bool ok = true; double peak = 0.0;
        const long total = (long)(sr * 80.0);
        long done = 0; int step = 0;
        std::vector<float> L(8192), R(8192);
        while (done < total)
        {
            if (step++ % 7 == 0)
            {
                Settings s = mk(u(g), (int)(u(g) * 4.999f), u(g), u(g) * 2.0f - 1.0f, u(g) < 0.3f, (u(g) - 0.5f) * 48.0f);
                e.setSettings(s);
            }
            if (u(g) < 0.03f) e.requestJump();
            const int B = 1 + (int)(u(g) * u(g) * 4095.0f);
            const float amp = u(g) < 0.1f ? 100.0f : (u(g) < 0.6f ? 0.3f : 0.0f);
            const int kind = (int)(u(g) * 4.0f);
            for (int i = 0; i < B; ++i)
            {
                float v = amp * (kind == 0 ? nd(g) : (kind == 1 ? (float)std::sin(0.05 * (done + i)) : (kind == 2 ? (u(g) < 0.5f ? 1.0f : -1.0f) : 0.0f)));
                if (u(g) < 0.0002f) v = std::numeric_limits<float>::quiet_NaN();
                else if (u(g) < 0.0002f) v = std::numeric_limits<float>::infinity();
                L[(size_t)i] = v; R[(size_t)i] = -v;
            }
            e.process(L.data(), R.data(), B);
            for (int i = 0; i < B; ++i) { if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) ok = false; peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i]))); }
            done += B;
        }
        // 8 (the largest input that passes) x +24 dB = 127; the wash is under 4 x 16
        CHECK(ok && peak < 140.0, "%.0f Hz: 80 s of random settings: finite %s, peak %.2f", sr, ok ? "yes" : "NO", peak);
        // and it still works after that
        e.setSettings(mk(0.5, 2, 1.0));
        Sig x = noise(sr, 20.0, 0.1f, 29), y = x;
        run(e, y, 480);
        const double d = db(rms(y.L, (size_t)(sr * 15.0), y.L.size()) / rms(x.L, (size_t)(sr * 15.0), x.L.size()));
        CHECK(std::fabs(d) < 1.5, "%.0f Hz: afterwards the wash is %+.2f dB re the input", sr, d);
    }
    // an absurdly hot, sustained input (a +40 dB wall of noise) is dropped sample by sample and the output stays quiet
    {
        Engine e; e.prepare(48000.0, 512); e.setSettings(mk(0.5, 2));
        Sig x = noise(48000.0, 6.0, 100.0f, 31);
        run(e, x, 256);
        double pk = 0.0; for (float v : x.L) pk = std::max(pk, (double)std::fabs(v));
        CHECK(pk < 8.0 * 1.0 + 4.0, "hot noise (sigma 100): the output peaks at %.2f", pk);
    }
}

// The work of a block: at most one unit per 64-sample block with the largest window; and the time it takes.
static void testWork()
{
    printf("Per-block work is bounded (a 64-sample buffer)\n");
    std::vector<float> mix;
    if (FILE* f = std::fopen("tests/data/mix.f32", "rb")) { std::fseek(f, 0, SEEK_END); mix.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(mix.data(), 4, mix.size(), f) != mix.size()) mix.clear(); std::fclose(f); }
    for (double sr : kRates)
        for (int win : { 0, 2, 4 })
        {
            Engine e; e.prepare(sr, 64); e.setSettings(mk(0.6, win));
            Sig x = noise(sr, 30.0, 0.1f, 37);
            if (sr == 48000.0 && mix.size() > 100000) for (size_t i = 0; i < std::min(x.L.size(), mix.size() / 2); ++i) { x.L[i] = mix[2 * i]; x.R[i] = mix[2 * i + 1]; }
            const int B = 64;
            // the time of every block, over three passes of the same audio: the shared machine's pre-emptions land on
            // different blocks each time, so the smallest time a block took in any pass is its own cost
            const size_t nb = x.L.size() / (size_t)B;
            std::vector<double> best(nb, 1e30);
            double tMax = 0.0, tSum = 0.0; long blocks = 0;
            for (int pass = 0; pass < 3; ++pass)
            {
                if (pass > 0) e.prepare(sr, 64);
                Sig w = x;
                for (size_t bi = 0; bi < nb; ++bi)
                {
                    const int off = (int)bi * B;
                    // changes of window and Tone, Freeze toggles: the worst frames
                    if (off % (int)(sr * 3.0) < B) { Settings s = mk(0.6, win, 1.0, ((off / (int)(sr * 3.0)) % 2) ? 0.5 : -0.5, (off / (int)(sr * 6.0)) % 2 == 1); e.setSettings(s); }
                    else if (off == 0) e.setSettings(mk(0.6, win));
                    const auto t0 = std::chrono::steady_clock::now();
                    e.process(w.L.data() + off, w.R.data() + off, B);
                    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                    best[bi] = std::min(best[bi], us);
                    if (pass == 0) { tMax = std::max(tMax, us); tSum += us; ++blocks; }
                }
            }
            double tFilt = 0.0; for (double v : best) tFilt = std::max(tFilt, v);
            const int N = e.windowSamples(win);
            const double unitMax = (double)N * std::log2((double)N) + 2.0 * N;   // one transform with its power sums
            CHECK(e.statMaxUnits <= 2 && e.statForced == 0 && e.statMaxWork <= 2.0 * unitMax,
                  "%.0f Hz, window %d (N %d): at most %d units in a block (work %.2f of a transform), %d frames finished late; block time: worst %.0f us (%.0f us once pre-emptions are filtered out), mean %.1f us; the buffer is %.0f us",
                  sr, win, N, e.statMaxUnits, e.statMaxWork / unitMax, e.statForced, tMax, tFilt, tSum / blocks, 1e6 * B / sr);
            // wall-clock time depends on the machine (sanitizers, busy CI runners): only asked for with PUMP_TIMING_CHECKS=1
            if (sr <= 48000.0 && std::getenv("PUMP_TIMING_CHECKS")) CHECK(tFilt < 1e6 * B / sr, "%.0f Hz, window %d: the worst block (%.0f us) fits in the 64-sample buffer (%.0f us)", sr, win, tFilt, 1e6 * B / sr);
        }
    // the same with the other buffer sizes: still nothing finished late
    for (int B : { 32, 128, 512, 2048 })
    {
        Engine e; e.prepare(48000.0, B); e.setSettings(mk(0.6, 4));
        Sig x = noise(48000.0, 12.0, 0.1f, 41);
        for (int off = 0; off + B <= (int)x.L.size(); off += B) e.process(x.L.data() + off, x.R.data() + off, B);
        CHECK(e.statForced == 0, "buffer %d, window 4: %d frames finished late, at most %d units in a block", B, e.statForced, e.statMaxUnits);
    }
    // CPU per second of audio
    {
        Engine e; e.prepare(48000.0, 512); e.setSettings(mk(0.6, 0));
        Sig x = noise(48000.0, 20.0, 0.1f, 43);
        const auto t0 = std::chrono::steady_clock::now();
        run(e, x, 512);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  (CPU: %.1f ms per second of audio at 48 kHz, window 2048, the costliest)\n", 1000.0 * s / 20.0);
    }
}

int main()
{
    testNullAndBypass();
    testLevel();
    testPitch();
    testSmear();
    testFreeze();
    testDeterministic();
    testJump();
    testSilenceThenSound();
    testTone();
    testSampleRates();
    testStress();
    testWork();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
