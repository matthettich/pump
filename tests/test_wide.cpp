// pump WIDE: width 100 / stereoize 0 is the crossover's allpass path (linear: the delayed input), width 0 gives
// L == R, stereoize leaves L + R untouched, per-band width accuracy, mono bass (LR4 / LR8) side attenuation and mid
// integrity, correlation readings (mono, anti-phase, uncorrelated noise), solo / listen, at 44.1, 48 and 96 kHz.
#include "TestMix.hpp"
#include "../plugins/wide/WideEngine.hpp"
#include <cstdio>
#include <random>
#include <thread>

using namespace pump;
using namespace pump::wide;
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
static void allBands(Engine& e, float width, float st)
{
    for (int b = 0; b < kNumBands; ++b) { BandSettings s; s.width = width; s.stereoize = st; e.setBand(b, s); }
}
// the reference: the same splitter's bands summed (analog: the crossover allpass path)
static void reference(double sr, int mode, std::vector<float>& L, std::vector<float>& R)
{
    mb::Splitter s; s.setBands(4); s.setCrossovers(120, 1000, 6000); s.setMode(mode); s.prepare(sr, 256);
    const int n = (int)L.size();
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        s.process(L.data() + off, R.data() + off, m);
        for (int i = 0; i < m; ++i)
        {
            double a = 0, b = 0;
            for (int k = 0; k < 4; ++k) { a += s.band(k, 0)[i]; b += s.band(k, 1)[i]; }
            L[(size_t)(off + i)] = (float)a; R[(size_t)(off + i)] = (float)b;
        }
    }
}
// amplitude of a tone at f (Hann-windowed DFT over [from, end): accurate for any window length)
static double toneAmp(const std::vector<double>& x, double f, double sr, size_t from)
{
    double re = 0, im = 0, ws = 0;
    const size_t n = x.size() - from;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i + 0.5) / n);
        re += w * x[from + i] * std::cos(kTwoPi * f * (from + i) / sr); im += w * x[from + i] * std::sin(kTwoPi * f * (from + i) / sr); ws += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / ws;
}
static double nullDb(const std::vector<float>& a, const std::vector<float>& b, int from = 0)
{
    double e = 0, r = 0;
    for (size_t i = (size_t)from; i < a.size(); ++i) { e += std::pow((double)a[i] - b[i], 2); r += (double)b[i] * b[i]; }
    return 10 * std::log10(e / std::max(r, 1e-30) + 1e-30);
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int T = std::min((int)mix.size() / 2, (int)(sr * 4));
        auto load = [&](std::vector<float>& L, std::vector<float>& R) {
            L.resize((size_t)T); R.resize((size_t)T);
            for (int i = 0; i < T; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
        };
        for (int mode = 0; mode <= 1; ++mode)
        {
            const char* mn = mode ? "linear" : "analog";
            // 1. width 100 / stereoize 0: the crossover path itself
            {
                std::vector<float> L, R, rL, rR; load(L, R); load(rL, rR);
                Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); e.setXoverMode(mode); allBands(e, 100, 0);
                e.prepare(sr, 512);
                run(e, L, R, 300);
                if (mode) { const int lat = e.latency(); for (int i = T - 1; i >= 0; --i) { rL[(size_t)i] = i >= lat ? rL[(size_t)(i - lat)] : 0.0f; rR[(size_t)i] = i >= lat ? rR[(size_t)(i - lat)] : 0.0f; } }
                else reference(sr, 0, rL, rR);
                const double dl = nullDb(L, rL), dr = nullDb(R, rR);
                CHECK(std::max(dl, dr) < -100, "%s: width 100%% / stereoize 0%% nulls against %s: L %.1f dB, R %.1f dB", mn, mode ? "the input delayed by the latency" : "the crossover allpass path", dl, dr);
            }
            // 2. width 0 on every band: L == R exactly
            {
                std::vector<float> L, R; load(L, R);
                Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); e.setXoverMode(mode); allBands(e, 0, 0);
                e.prepare(sr, 512);
                run(e, L, R);
                double d = 0, pk = 0; for (int i = 0; i < T; ++i) { d = std::max(d, (double)std::fabs(L[(size_t)i] - R[(size_t)i])); pk = std::max(pk, (double)std::fabs(L[(size_t)i])); }
                CHECK(d == 0.0 && pk > 0.1, "%s: width 0%%: max |L - R| = %.1e (output peak %.2f)", mn, d, pk);
            }
            // 3. stereoize keeps L + R unchanged (and adds side energy)
            {
                std::vector<float> L, R, rL, rR; load(L, R); load(rL, rR);
                Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); e.setXoverMode(mode); allBands(e, 100, 100);
                e.prepare(sr, 512);
                run(e, L, R);
                Engine f; f.setBands(4); f.setCrossovers(120, 1000, 6000); f.setXoverMode(mode); allBands(f, 100, 0);
                f.prepare(sr, 512);
                run(f, rL, rR);
                std::vector<float> s(T), rs(T), side(T), rside(T);
                for (int i = 0; i < T; ++i) { s[(size_t)i] = L[(size_t)i] + R[(size_t)i]; rs[(size_t)i] = rL[(size_t)i] + rR[(size_t)i]; side[(size_t)i] = L[(size_t)i] - R[(size_t)i]; rside[(size_t)i] = rL[(size_t)i] - rR[(size_t)i]; }
                double es = 0, er = 0; for (int i = 0; i < T; ++i) { es += side[(size_t)i] * side[(size_t)i]; er += rside[(size_t)i] * rside[(size_t)i]; }
                CHECK(nullDb(s, rs) < -120, "%s: stereoize 100%% on all bands: L+R null against no stereoize %.1f dB; side energy %+.1f dB", mn, nullDb(s, rs), 10 * std::log10(es / er));
            }
        }
        // 4. per-band width: a side-only sine in band 2 at width 50% loses exactly 6.02 dB; band 1's side is untouched
        {
            double ex[3];
            Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); allBands(e, 100, 0);
            BandSettings s; s.width = 50; e.setBand(1, s);
            e.prepare(sr, 512); e.effectiveCrossovers(ex);
            const double f2 = std::sqrt(ex[0] * ex[1]), f1 = 40.0;
            const int N = (int)sr;
            double worst = 0;
            for (double f : { f2, f1 })
            {
                std::vector<float> L(N), R(N);
                for (int i = 0; i < N; ++i) { const float v = (float)(0.25 * std::sin(kTwoPi * f * i / sr)); L[(size_t)i] = v; R[(size_t)i] = -v; }
                Engine g; g.setBands(4); g.setCrossovers(120, 1000, 6000); allBands(g, 100, 0); g.setBand(1, s); g.prepare(sr, 512);
                run(g, L, R);
                double re = 0, im = 0; const int i0 = N / 2;
                for (int i = i0; i < N; ++i) { const double sd = 0.5 * (L[(size_t)i] - R[(size_t)i]); re += sd * std::cos(kTwoPi * f * i / sr); im += sd * std::sin(kTwoPi * f * i / sr); }
                const double amp = 2 * std::sqrt(re * re + im * im) / (N - i0);
                const double m = mb::bandMag(4, ex, 1, f, sr);
                const double want = 0.25 * (1.0 - 0.5 * m);
                worst = std::max(worst, std::fabs(gainToDb(amp) - gainToDb(want)));
            }
            CHECK(worst < 0.01, "band 2 width 50%%: side-only sines (band 2 centre, 40 Hz) match the band model within %.4f dB", worst);
        }
        // 5. mono bass: side below the split goes away (LR4 / LR8), the mid stays at full level
        for (int slope = 0; slope <= 1; ++slope)
        {
            const double fc = 120.0;
            char buf[256]; int pos = 0;
            double worstMid = 0; double at[4];
            const double fr[4] = { fc / 2, fc / 3, fc / 4, fc / 8 };
            for (int k = 0; k < 4; ++k)
            {
                const double f = fr[k];
                const int N = (int)(sr * 1.5);
                for (int pass = 0; pass < 2; ++pass)   // 0: side-only sine, 1: mid-only sine
                {
                    std::vector<float> L(N), R(N);
                    for (int i = 0; i < N; ++i) { const float v = (float)(0.25 * std::sin(kTwoPi * f * i / sr)); L[(size_t)i] = v; R[(size_t)i] = pass ? v : -v; }
                    Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); allBands(e, 100, 0); e.setMonoBass(true, (float)fc, slope);
                    e.prepare(sr, 512);
                    run(e, L, R);
                    std::vector<double> sd((size_t)N), md((size_t)N);
                    for (int i = 0; i < N; ++i) { sd[(size_t)i] = 0.5 * (L[(size_t)i] - R[(size_t)i]); md[(size_t)i] = 0.5 * (L[(size_t)i] + R[(size_t)i]); }
                    if (pass == 0) at[k] = gainToDb(toneAmp(sd, f, sr, (size_t)(N / 2)) / 0.25);
                    else worstMid = std::max(worstMid, std::fabs(gainToDb(toneAmp(md, f, sr, (size_t)(N / 2)) / 0.25)));
                }
                pos += std::snprintf(buf + pos, sizeof buf - (size_t)pos, "%s f/%d %.1f dB", k ? "," : "", (int)std::lround(fc / f), at[k]);
            }
            // the LR4 response is r/(1+r) with r = (f/fc)^4 (24 dB/oct); LR8 is twice as steep
            const bool ok = slope == 0 ? (at[3] < -60 && at[0] < -24 && at[0] > -26) : (at[1] < -60 && at[0] < -47);
            CHECK(ok && worstMid < 0.01, "mono bass at %.0f Hz, %s: side %s; mid sines unchanged within %.4f dB", fc, slope ? "LR8 48 dB/oct" : "LR4 24 dB/oct", buf, worstMid);
        }
        // 6. correlation: mono = +1, anti-phase = -1, independent noise ~ 0 (overall and per band)
        {
            std::mt19937 rng(9); std::normal_distribution<float> nd(0.0f, 0.2f);
            const int N = (int)(sr * 2);
            double got[3][2];
            for (int c = 0; c < 3; ++c)
            {
                std::vector<float> L(N), R(N);
                for (int i = 0; i < N; ++i) { const float a = nd(rng), b = nd(rng); L[(size_t)i] = a; R[(size_t)i] = c == 0 ? a : (c == 1 ? -a : b); }
                Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); allBands(e, 100, 0); e.prepare(sr, 512);
                run(e, L, R);
                got[c][0] = e.correlationAll();
                double worstBand = c == 2 ? 0.0 : 1.0;
                for (int b = 0; b < 4; ++b) worstBand = c == 2 ? std::max(worstBand, std::fabs((double)e.correlation(b))) : std::min(worstBand, std::fabs((double)e.correlation(b)));
                got[c][1] = worstBand;
            }
            CHECK(got[0][0] > 0.9999 && got[0][1] > 0.9999 && got[1][0] < -0.9999 && got[1][1] > 0.9999 && std::fabs(got[2][0]) < 0.08 && got[2][1] < 0.15,
                  "correlation: mono %+.4f (bands >= %.4f), anti-phase %+.4f (bands |r| >= %.4f), independent noise %+.3f (bands |r| <= %.3f)",
                  got[0][0], got[0][1], got[1][0], got[1][1], got[2][0], got[2][1]);
        }
    }
    // 7. stereoize decorrelates: a mono signal with stereoize 100% has correlation well below 1 but still positive
    {
        const double sr = 48000;
        const int T = std::min((int)mix.size() / 2, 48000 * 4);
        std::vector<float> L(T), R(T);
        for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = 0.5f * (mix[(size_t)(2 * i)] + mix[(size_t)(2 * i + 1)]);
        Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); allBands(e, 100, 100); e.prepare(sr, 512);
        run(e, L, R);
        CHECK(e.correlationAll() > 0.1 && e.correlationAll() < 0.5, "mono mix + stereoize 100%%: correlation %+.3f (wider, still mono-safe)", e.correlationAll());
    }
    // 8. solo / listen / mute equal band 3 alone (analog)
    {
        const double sr = 48000;
        const int T = 48000 * 2;
        for (int test = 0; test < 3; ++test)
        {
            std::vector<float> L(T), R(T), rL(T), rR(T);
            for (int i = 0; i < T; ++i) { L[(size_t)i] = rL[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = rR[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000);
            for (int b = 0; b < 4; ++b) { BandSettings s; if (test == 0) s.solo = b == 2; if (test == 1) s.mute = b != 2; e.setBand(b, s); }
            if (test == 2) e.setListen(2);
            e.prepare(sr, 512);
            run(e, L, R);
            mb::Splitter s; s.setBands(4); s.setCrossovers(120, 1000, 6000); s.prepare(sr, 256);
            double err = 0, ref = 0;
            for (int off = 0; off < T; off += 256)
            {
                s.process(rL.data() + off, rR.data() + off, 256);
                for (int i = 0; i < 256; ++i) { const double a = s.band(2, 0)[i]; err += std::pow(L[(size_t)(off + i)] - a, 2); ref += a * a; }
            }
            static const char* names[] = { "solo band 3", "mute bands 1, 2, 4", "Alt-listen band 3" };
            CHECK(10 * std::log10(err / ref + 1e-30) < -100, "%s: output equals band 3 alone (null %.1f dB)", names[test], 10 * std::log10(err / ref + 1e-30));
        }
    }
    // 9. balance: +50% turns the left down 6 dB and leaves the right alone
    {
        Engine e; e.setBands(1); allBands(e, 100, 0); e.setBalance(50); e.prepare(48000, 512);
        std::vector<float> L(48000, 0.5f), R(48000, 0.5f);
        run(e, L, R);
        CHECK(std::fabs(gainToDb(L.back() / 0.5) + 6.0206) < 0.01 && std::fabs(R.back() - 0.5f) < 1e-6, "balance +50%%: left %.2f dB, right %.2f dB", gainToDb(L.back() / 0.5), gainToDb(R.back() / 0.5));
    }
    // 10. no clicks while widths, stereoize, mono bass and crossovers move; random stress stays finite
    {
        const double sr = 48000;
        Engine e; e.setBands(4); e.setCrossovers(120, 1000, 6000); allBands(e, 100, 0); e.prepare(sr, 512);
        const int T = 48000 * 3;
        std::vector<float> L(T), R(T);
        for (int i = 0; i < T; ++i) { L[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 440.0 * i / sr)); R[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 440.0 * i / sr + 0.7)); }
        double maxStep = 0; float pl = L[0];
        for (int off = 0, blk = 0; off < T; off += 128, ++blk)
        {
            const double t = (double)off / T;
            BandSettings s; s.width = (float)(100 + 100 * std::sin(6.0 * t)); s.stereoize = (float)(50 + 50 * std::sin(9.0 * t));
            for (int b = 0; b < 4; ++b) e.setBand(b, s);
            e.setCrossovers(120, (float)(200 * std::pow(10.0, 1.3 * t)), 9000);
            e.setMonoBass(((blk / 100) & 1) != 0, (float)(60 + 300 * t), (blk / 150) & 1);
            e.process(L.data() + off, R.data() + off, 128);
            for (int i = off; i < off + 128; ++i) { maxStep = std::max(maxStep, (double)std::fabs(L[(size_t)i] - pl)); pl = L[(size_t)i]; }
        }
        CHECK(maxStep < 0.06, "moving width / stereoize / mono bass / crossover on a 440 Hz tone: largest sample step %.4f (tone alone 0.017, at 2x width 0.035)", maxStep);
        std::mt19937 rng(4);
        std::vector<float> a(512), b2(512);
        bool ok = true;
        for (int blk = 0; blk < 800; ++blk)
        {
            BandSettings s; s.width = (float)(rng() % 201); s.stereoize = (float)(rng() % 101); s.solo = rng() % 9 == 0; s.mute = rng() % 11 == 0; s.bypass = rng() % 9 == 0;
            e.setBand((int)(rng() % 4), s);
            if (blk % 40 == 0) e.setXoverMode((int)(rng() % 2));
            if (blk % 23 == 0) e.setBands(1 + (int)(rng() % 4));
            if (blk % 13 == 0) e.setMonoBass(rng() % 2, (float)(20 + rng() % 480), (int)(rng() % 2));
            if (blk % 7 == 0) e.setBalance((float)((int)(rng() % 201) - 100));
            e.setCrossovers((float)(20 + rng() % 20000), (float)(20 + rng() % 20000), (float)(20 + rng() % 20000));
            for (int i = 0; i < 512; ++i) { a[(size_t)i] = 0.3f * (float)std::sin(0.031 * (blk * 512 + i)); b2[(size_t)i] = 0.3f * (float)std::sin(0.017 * (blk * 512 + i)); }
            e.process(a.data(), b2.data(), 512);
            for (int i = 0; i < 512; ++i) if (!std::isfinite(a[(size_t)i]) || std::fabs(a[(size_t)i]) > 10.0f || !std::isfinite(b2[(size_t)i])) ok = false;
        }
        CHECK(ok, "800 blocks of random settings: finite and bounded");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
