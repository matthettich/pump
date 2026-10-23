// Core DSP checks: oversampler latency + passband/aliasing, STFT and convolver reconstruction, loudness and
// true-peak against EBU/ITU reference cases, lookahead limiter gain.
#include "../common/dsp/Util.hpp"
#include "../common/dsp/FFT.hpp"
#include "../common/dsp/Oversampler.hpp"
#include "../common/dsp/Loudness.hpp"
#include "../common/dsp/Dynamics.hpp"
#include "../common/dsp/Crossover.hpp"
#include <cstdio>
#include <random>

using namespace pump;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void testOversampler()
{
    printf("Oversampler\n");
    const double sr = 48000.0;
    for (int lg = 1; lg <= 3; ++lg)
    {
        Oversampler os; os.prepare(lg, 512);
        const int lat = os.latency();
        // impulse: the output peak must land exactly at the reported latency
        std::vector<float> L(4096, 0.0f), R(4096, 0.0f);
        L[100] = 1.0f; R[100] = 1.0f;
        for (int off = 0; off < 4096; off += 512)
        {
            float* const* b = os.upsample(L.data() + off, R.data() + off, 512);
            (void)b;
            os.downsample(L.data() + off, R.data() + off, 512);
        }
        int pk = 0; float pv = 0;
        for (int i = 0; i < 4096; ++i) if (std::fabs(L[(size_t)i]) > pv) { pv = std::fabs(L[(size_t)i]); pk = i; }
        CHECK(pk == 100 + lat && pv > 0.9f, "%dx: impulse peak at %d (expected %d), value %.4f", 1 << lg, pk - 100, lat, pv);
        // passband: a 15 kHz sine through up/down keeps its level
        os.reset();
        std::vector<float> s(48000), r(48000);
        for (int i = 0; i < 48000; ++i) s[(size_t)i] = r[(size_t)i] = 0.5f * (float)std::sin(kTwoPi * 15000.0 * i / sr);
        for (int off = 0; off + 480 <= 48000; off += 480) { os.upsample(s.data() + off, r.data() + off, 480); os.downsample(s.data() + off, r.data() + off, 480); }
        double e = 0; for (int i = 24000; i < 48000; ++i) e += s[(size_t)i] * s[(size_t)i];
        const double db = 10 * std::log10(e / 24000.0 / 0.125);
        CHECK(std::fabs(db) < 0.01, "%dx: 15 kHz level change %.4f dB", 1 << lg, db);
    }
    // aliasing: a 7 kHz sine driven hard into tanh at 4x, energy that folds below 18 kHz should be far down
    {
        Oversampler os; os.prepare(2, 480);
        std::vector<float> s(96000), r(96000);
        for (int i = 0; i < 96000; ++i) s[(size_t)i] = r[(size_t)i] = 0.9f * (float)std::sin(kTwoPi * 7000.0 * i / sr);
        for (int off = 0; off + 480 <= 96000; off += 480)
        {
            float* const* b = os.upsample(s.data() + off, r.data() + off, 480);
            for (int i = 0; i < 480 * 4; ++i) { b[0][i] = (float)std::tanh(b[0][i] * 3.0f); b[1][i] = b[0][i]; }
            os.downsample(s.data() + off, r.data() + off, 480);
        }
        // DFT at a few alias frequencies (7k clipped -> harmonics 21k, 35k->13k alias, 49k->1k alias...)
        auto mag = [&](double f) { double re = 0, im = 0; for (int i = 48000; i < 96000; ++i) { re += s[(size_t)i] * std::cos(kTwoPi * f * i / sr); im += s[(size_t)i] * std::sin(kTwoPi * f * i / sr); } return 20 * std::log10(2 * std::sqrt(re * re + im * im) / 48000.0 + 1e-12); };
        const double fund = mag(7000), a13 = mag(13000), a1 = mag(1000);
        CHECK(a13 - fund < -90 && a1 - fund < -90, "4x tanh x3: aliases at 13 kHz %.1f dB, 1 kHz %.1f dB below the fundamental", a13 - fund, a1 - fund);
    }
}

static void testSTFT()
{
    printf("STFT\n");
    for (int N : { 1024, 2048, 4096 })
    {
        STFT st; st.prepare(N, 2);
        std::mt19937 rng(1);
        std::normal_distribution<float> nd(0.0f, 0.3f);
        const int T = N * 8;
        std::vector<float> in(T), L(T), R(T);
        for (int i = 0; i < T; ++i) in[(size_t)i] = L[(size_t)i] = R[(size_t)i] = nd(rng);
        float* io[2] = { L.data(), R.data() };
        for (int off = 0; off < T; off += 256)
        {
            float* p[2] = { L.data() + off, R.data() + off };
            st.process(p, 256, [](void*, float* const*, int, int) {}, nullptr);
        }
        (void)io;
        double err = 0, ref = 0;
        for (int i = N * 2; i < T; ++i) { const double d = L[(size_t)i] - in[(size_t)(i - st.latency())]; err += d * d; ref += in[(size_t)(i - st.latency())] * in[(size_t)(i - st.latency())]; }
        CHECK(10 * std::log10(err / ref) < -100, "N=%d identity null %.1f dB (latency %d)", N, 10 * std::log10(err / ref + 1e-30), st.latency());
    }
}

static void testConvolver()
{
    printf("Convolver\n");
    StereoConvolver c; c.prepare(512, 8192);
    // kernel: a delay of 1000 samples (LL), and 0.5 * delay 10 for LR (cross)
    std::vector<float> ll(4096, 0.0f), lr(4096, 0.0f), rr(4096, 0.0f);
    ll[1000] = 1.0f; lr[10] = 0.5f; rr[3] = 1.0f;
    const float* irs[4] = { ll.data(), lr.data(), nullptr, rr.data() };
    c.setKernelNow(c.makeKernel(irs, 4096));
    const int T = 16384;
    std::vector<float> L(T), R(T), inL(T), inR(T);
    std::mt19937 rng(2); std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    for (int i = 0; i < T; ++i) { inL[(size_t)i] = L[(size_t)i] = u(rng); inR[(size_t)i] = R[(size_t)i] = u(rng); }
    for (int off = 0; off < T; off += 300) c.process(L.data() + off, R.data() + off, std::min(300, T - off));
    const int lat = c.latency();
    double eL = 0, eR = 0;
    for (int i = 4000; i < T; ++i)
    {
        const double expL = inL[(size_t)(i - lat - 1000)] + 0.5 * inR[(size_t)(i - lat - 10)];
        const double expR = inR[(size_t)(i - lat - 3)];
        eL = std::max(eL, std::fabs(L[(size_t)i] - expL)); eR = std::max(eR, std::fabs(R[(size_t)i] - expR));
    }
    CHECK(eL < 1e-5 && eR < 1e-5, "2x2 partitioned convolution max error L %.2e R %.2e", eL, eR);
}

static void testLoudness()
{
    printf("Loudness\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // EBU Tech 3341 case: 1 kHz sine, -23 dBFS (peak) on both channels -> -23.0 LUFS
        LoudnessMeter m; m.prepare(sr);
        const int T = (int)(sr * 20);
        std::vector<float> L(T);
        const float a = (float)dbToGain(-23.0);
        for (int i = 0; i < T; ++i) L[(size_t)i] = a * (float)std::sin(kTwoPi * 1000.0 * i / sr);
        m.process(L.data(), L.data(), T);
        CHECK(std::fabs(m.getIntegrated() + 23.0) < 0.1 && std::fabs(m.getShortTerm() + 23.0) < 0.1 && std::fabs(m.getMomentary() + 23.0) < 0.1,
              "%.0f Hz: 1 kHz @ -23 dBFS -> I %.2f  S %.2f  M %.2f LUFS", sr, m.getIntegrated(), m.getShortTerm(), m.getMomentary());
        // gating: 20 s of that tone + 20 s at -60 dBFS (relative gate ignores the quiet part... it's > -70 abs)
        LoudnessMeter g; g.prepare(sr);
        std::vector<float> Q(T);
        const float b = (float)dbToGain(-43.0);
        for (int i = 0; i < T; ++i) Q[(size_t)i] = b * (float)std::sin(kTwoPi * 1000.0 * i / sr);
        g.process(L.data(), L.data(), T);
        g.process(Q.data(), Q.data(), T);
        CHECK(std::fabs(g.getIntegrated() + 23.0) < 0.15, "%.0f Hz: relative gate (20 s at -23, 20 s at -43) -> %.2f LUFS", sr, g.getIntegrated());
        // LRA: EBU 3342 case 1: -20 then -30 LUFS, 20 s each -> LRA 10
        LoudnessMeter r; r.prepare(sr);
        const float c1 = (float)dbToGain(-20.0), c2 = (float)dbToGain(-30.0);
        std::vector<float> A(T), B(T);
        for (int i = 0; i < T; ++i) { A[(size_t)i] = c1 * (float)std::sin(kTwoPi * 1000.0 * i / sr); B[(size_t)i] = c2 * (float)std::sin(kTwoPi * 1000.0 * i / sr); }
        r.process(A.data(), A.data(), T); r.process(B.data(), B.data(), T);
        CHECK(std::fabs(r.getRange() - 10.0) < 0.2, "%.0f Hz: LRA case 1 -> %.2f LU (expect 10)", sr, r.getRange());
    }
    // true peak: a sine at fs/4 with 45 degree phase has sample peaks of 0.707 but true peak 1.0
    {
        LoudnessMeter m; m.prepare(48000.0);
        std::vector<float> L(48000);
        for (int i = 0; i < 48000; ++i) L[(size_t)i] = (float)std::sin(kTwoPi * 12000.0 * i / 48000.0 + kPi / 4);
        m.process(L.data(), L.data(), 48000);
        float sp = 0; for (float v : L) sp = std::max(sp, std::fabs(v));
        CHECK(std::fabs(m.getTruePeakDb()) < 0.1, "true peak of fs/4 sine at 45 deg: %.2f dBTP (sample peak %.2f dBFS)", m.getTruePeakDb(), gainToDb(sp));
    }
}

static void testLookahead()
{
    printf("Lookahead gain\n");
    LookaheadGain lg; lg.setup(200); lg.setLength(144);
    const int L = lg.length();
    // a single sample needing gain 0.25 at t0 = 500: delayed audio at t0 + L must see gain <= 0.25, and no step
    std::vector<float> g(2000);
    for (int t = 0; t < 2000; ++t) g[(size_t)t] = lg.process(t == 500 ? 0.25f : 1.0f);
    float maxStep = 0; for (int t = 1; t < 2000; ++t) maxStep = std::max(maxStep, std::fabs(g[(size_t)t] - g[(size_t)(t - 1)]));
    CHECK(g[(size_t)(500 + L)] <= 0.2500001f && g[(size_t)(500 + L - 1)] <= 0.2500001f, "gain at the delayed peak %.4f (window %d)", g[(size_t)(500 + L)], L);
    CHECK(maxStep < 0.02f, "largest gain step %.4f (smooth)", maxStep);
}

static void testCrossover()
{
    printf("Crossover\n");
    Crossover4 x; x.prepare(48000.0); x.setFrequencies(150, 1200, 7000);
    // sum of bands is an allpass: magnitude flat for sines
    for (double f : { 50.0, 150.0, 600.0, 1200.0, 3000.0, 7000.0, 15000.0 })
    {
        x.reset();
        double e = 0, ei = 0;
        for (int i = 0; i < 96000; ++i)
        {
            const double s = std::sin(kTwoPi * f * i / 48000.0);
            double b[4]; x.process(0, s, b);
            const double y = b[0] + b[1] + b[2] + b[3];
            if (i >= 48000) { e += y * y; ei += s * s; }
        }
        CHECK(std::fabs(10 * std::log10(e / ei)) < 0.01, "band sum at %5.0f Hz: %+.4f dB", f, 10 * std::log10(e / ei));
    }
}

int main()
{
    testOversampler();
    testSTFT();
    testConvolver();
    testLoudness();
    testLookahead();
    testCrossover();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
