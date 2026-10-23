// pump HEAT: band mix 0 / global mix 0 null against the crossover allpass path (linear: the delayed input) at every
// oversampling factor; drive 0 near-identity; aliasing of a 7 kHz sine at high drive (Clip, Warm) per factor;
// auto gain keeps the test mix's RMS steady across drive 0..18 dB; latency and wet/dry alignment per factor;
// solo / listen; random-settings stress. 44.1, 48 and 96 kHz.
#include "../plugins/heat/HeatEngine.hpp"
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::heat;
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
static void bands(Engine& e, int mode, float drive, float mix, float level = 0.0f)
{
    for (int b = 0; b < kNumBands; ++b) { BandSettings s; s.mode = mode; s.drive = drive; s.mix = mix; s.level = level; e.setBand(b, s); }
}
// the crossover allpass path (analog) or the input (linear), delayed by `lat`
static void reference(double sr, int mode, int lat, std::vector<float>& L, std::vector<float>& R)
{
    const int n = (int)L.size();
    if (mode == 0)
    {
        mb::Splitter s; s.setBands(3); s.setCrossovers(150, 3000, 9000); s.prepare(sr, 256);
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
    const int d = mode == 0 ? lat : lat;
    for (int i = n - 1; i >= 0; --i) { L[(size_t)i] = i >= d ? L[(size_t)(i - d)] : 0.0f; R[(size_t)i] = i >= d ? R[(size_t)(i - d)] : 0.0f; }
}
// a quiet multitone (40 tones, 30 Hz .. 16 kHz, random phases): inside every factor's passband
static void multitone(std::vector<float>& L, std::vector<float>& R, double sr, int n, float amp)
{
    L.assign((size_t)n, 0.0f); R.assign((size_t)n, 0.0f);
    std::mt19937 rng(3); std::uniform_real_distribution<double> ph(0.0, kTwoPi);
    for (int k = 0; k < 40; ++k)
    {
        const double f = 30.0 * std::pow(16000.0 / 30.0, k / 39.0), p1 = ph(rng), p2 = ph(rng);
        for (int i = 0; i < n; ++i) { L[(size_t)i] += (float)(amp / 40.0 * std::sin(kTwoPi * f * i / sr + p1)); R[(size_t)i] += (float)(amp / 40.0 * std::sin(kTwoPi * f * i / sr + p2)); }
    }
}
static double nullDb(const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double e = 0, r = 0;
    for (size_t i = from; i < a.size(); ++i) { e += std::pow((double)a[i] - b[i], 2); r += (double)b[i] * b[i]; }
    return 10 * std::log10(e / std::max(r, 1e-30) + 1e-30);
}
static double rmsDb(const std::vector<float>& L, const std::vector<float>& R, size_t from)
{
    double e = 0; for (size_t i = from; i < L.size(); ++i) e += (double)L[i] * L[i] + (double)R[i] * R[i];
    return 10 * std::log10(e / (2.0 * (L.size() - from)) + 1e-30);
}

int main()
{
    const std::vector<float> mix = loadMix("/home/claude/testkit/mix.f32");
    if (mix.empty()) { printf("no test mix\n"); return 1; }
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int T = std::min((int)mix.size() / 2, (int)(sr * 3));
        auto load = [&](std::vector<float>& L, std::vector<float>& R, float gain = 1.0f) {
            L.resize((size_t)T); R.resize((size_t)T);
            for (int i = 0; i < T; ++i) { L[(size_t)i] = gain * mix[(size_t)(2 * i)]; R[(size_t)i] = gain * mix[(size_t)(2 * i + 1)]; }
        };
        // 1 + 2: band mix 0 and global mix 0 are the crossover path, delayed by exactly the reported latency
        for (int xm = 0; xm <= 1; ++xm)
        {
            char buf[256]; int pos = 0; double worst = -400;
            for (int os = 0; os <= 3; ++os)
                for (int which = 0; which < 2; ++which)
                {
                    std::vector<float> L, R, rL, rR; load(L, R); load(rL, rR);
                    Engine e; e.setBands(3); e.setCrossovers(150, 3000, 9000); e.setXoverMode(xm); e.setOversampling(os);
                    bands(e, kClip, 18.0f, which == 0 ? 0.0f : 100.0f);
                    if (which == 1) e.setMix(0.0f);
                    e.prepare(sr, 512);
                    run(e, L, R, 300);
                    reference(sr, xm, e.latency(), rL, rR);
                    const double d = std::max(nullDb(L, rL, 0), nullDb(R, rR, 0));
                    worst = std::max(worst, d);
                    if (which == 0) pos += std::snprintf(buf + pos, sizeof buf - (size_t)pos, "%s%dx lat %d", os ? ", " : "", 1 << os, e.latency());
                }
            CHECK(worst < -100, "%s: band mix 0%% and global mix 0%% (Clip, 18 dB drive) null against %s: worst %.1f dB [%s]",
                  xm ? "linear" : "analog", xm ? "the delayed input" : "the delayed crossover path", worst, buf);
        }
        // 3: drive 0 dB on a quiet signal (the curves are linear there): the wet path is transparent inside the
        // oversampler's passband (the test mix itself also has content above 0.4535 fs, which oversampling removes)
        {
            char buf[320]; int pos = 0; double worst = -400;
            for (int os = 0; os <= 3; ++os)
            {
                std::vector<float> L, R, rL, rR;
                multitone(L, R, sr, T, 0.001f); rL = L; rR = R;
                std::vector<float> mL, mR, mrL, mrR; load(mL, mR, 0.001f); mrL = mL; mrR = mR;
                double d = 0, dm = 0;
                for (int pass = 0; pass < 2; ++pass)
                {
                    Engine e; e.setBands(3); e.setCrossovers(150, 3000, 9000); e.setOversampling(os); e.setAutoGain(false);
                    bands(e, kWarm, 0.0f, 100.0f);
                    e.prepare(sr, 512);
                    if (pass == 0) { run(e, L, R, 300); reference(sr, 0, e.latency(), rL, rR); d = std::max(nullDb(L, rL, 8192), nullDb(R, rR, 8192)); }
                    else { run(e, mL, mR, 300); reference(sr, 0, e.latency(), mrL, mrR); dm = std::max(nullDb(mL, mrL, 8192), nullDb(mR, mrR, 8192)); }
                }
                worst = std::max(worst, d);
                pos += std::snprintf(buf + pos, sizeof buf - (size_t)pos, "%s%dx %.1f dB (full-band mix %.1f)", os ? ", " : "", 1 << os, d, dm);
            }
            CHECK(worst < -80, "drive 0 dB, Warm, mix 100%%, -60 dBFS multitone 30 Hz-16 kHz: wet path vs dry path %s", buf);
        }
        // 6: latency and wet/dry alignment: an impulse comes out at the reported latency, and global mix 50% of a
        // linear (quiet) signal equals mix 100% (no comb from misaligned dry)
        {
            char buf[400]; int pos = 0; bool ok = true;
            for (int xm = 0; xm <= 1; ++xm)
                for (int os = 0; os <= 3; ++os)
                {
                    Engine e; e.setBands(3); e.setCrossovers(150, 3000, 9000); e.setXoverMode(xm); e.setOversampling(os); e.setAutoGain(false);
                    bands(e, kWarm, 0.0f, 100.0f);
                    e.prepare(sr, 512);
                    const int N = 32768;
                    std::vector<float> L((size_t)N, 0.0f), R((size_t)N, 0.0f);
                    L[64] = R[64] = 1e-3f;
                    run(e, L, R);
                    int pk = 0; float pv = 0; for (int i = 0; i < N; ++i) if (std::fabs(L[(size_t)i]) > pv) { pv = std::fabs(L[(size_t)i]); pk = i; }
                    // analog: the crossover's allpass smears the impulse; its peak still sits near the latency
                    const bool hit = xm ? pk - 64 == e.latency() : std::abs(pk - 64 - e.latency()) <= 2;
                    ok = ok && hit;
                    std::vector<float> a, b, c, d; multitone(a, b, sr, T, 0.001f); c = a; d = b;
                    Engine m1; m1.setBands(3); m1.setCrossovers(150, 3000, 9000); m1.setXoverMode(xm); m1.setOversampling(os); m1.setAutoGain(false); bands(m1, kWarm, 0.0f, 100.0f); m1.prepare(sr, 512);
                    Engine m2; m2.setBands(3); m2.setCrossovers(150, 3000, 9000); m2.setXoverMode(xm); m2.setOversampling(os); m2.setAutoGain(false); bands(m2, kWarm, 0.0f, 100.0f); m2.setMix(50.0f); m2.prepare(sr, 512);
                    run(m1, a, b); run(m2, c, d);
                    const double al = nullDb(c, a, 8192);
                    ok = ok && al < -80;
                    if (!hit) pos += std::snprintf(buf + pos, sizeof buf - (size_t)pos, " [%s %dx impulse at %d!]", xm ? "linear" : "analog", 1 << os, pk - 64);
                    if (xm == 0) pos += std::snprintf(buf + pos, sizeof buf - (size_t)pos, "%s%dx %d (mix 50 vs 100 %.0f dB)", os ? ", " : "", 1 << os, e.latency(), al);
                    else if (os == 3) pos += std::snprintf(buf + pos, sizeof buf - (size_t)pos, "; linear 8x %d", e.latency());
                }
            CHECK(ok, "latency per factor (impulse peak at the reported latency; global mix 50%% = 100%% for a linear signal, i.e. dry aligned to wet): %s", buf);
        }
    }
    // 4: aliasing of a 7 kHz sine (-6 dBFS) at high drive: everything below 18 kHz except the fundamental is alias
    // (Clip and Warm are symmetric, so they make only odd harmonics: 21 kHz and up)
    {
        const double sr = 48000, f = 7000;
        const int N = 65536;
        printf("== aliasing, 7 kHz sine at -6 dBFS, worst product below 18 kHz relative to the fundamental (48 kHz)\n");
        double at4x18[2] = { 0, 0 };
        for (int mode : { kClip, kWarm })
        {
            for (int os = 0; os <= 3; ++os)
            {
                printf("    %-5s %dx:", kModeNames[mode], 1 << os);
                for (float drive : { 12.0f, 18.0f, 24.0f })
                {
                    Engine e; e.setBands(1); e.setOversampling(os); e.setAutoGain(false); bands(e, mode, drive, 100.0f); e.prepare(sr, 512);
                    std::vector<float> L((size_t)(N + 16384)), R((size_t)(N + 16384));
                    for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = (float)(0.5 * std::sin(kTwoPi * f * i / sr));
                    run(e, L, R, 512);
                    RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
                    for (int i = 0; i < N; ++i)
                    {
                        const double t = kTwoPi * i / (N - 1);
                        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
                        in[(size_t)i] = (float)(L[L.size() - N + (size_t)i] * w);
                    }
                    fft.forward(in.data(), out.data());
                    auto p = [&](int k) { const double re = out[(size_t)(2 * k)], im = out[(size_t)(2 * k + 1)]; return re * re + im * im; };
                    const int kf = (int)std::lround(f * N / sr);
                    double pf = 0; for (int k = kf - 6; k <= kf + 6; ++k) pf = std::max(pf, p(k));
                    double worst = 0; const int k18 = (int)(18000.0 * N / sr);
                    for (int k = 4; k < k18; ++k) { if (std::abs(k - kf) <= 12) continue; worst = std::max(worst, p(k)); }
                    const double db = 10 * std::log10(worst / pf + 1e-30);
                    printf("  %2.0f dB drive %6.1f", drive, db);
                    if (os == 2 && drive == 18.0f) at4x18[mode == kClip ? 0 : 1] = db;
                }
                printf("\n");
            }
        }
        CHECK(at4x18[0] < -80 && at4x18[1] < -80, "4x, 18 dB drive: Clip %.1f dB, Warm %.1f dB (both under -80 dB)", at4x18[0], at4x18[1]);
    }
    // 5: auto gain keeps the RMS of the test mix within 1 dB of the input across drive 0..18 dB, every curve (4x)
    {
        const double sr = 48000;
        const int T = (int)mix.size() / 2;
        std::vector<float> L0(T), R0(T);
        for (int i = 0; i < T; ++i) { L0[(size_t)i] = mix[(size_t)(2 * i)]; R0[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
        const size_t from = (size_t)(sr * 1.0);
        const double ref = rmsDb(L0, R0, from);
        double worstAll = 0;
        for (int mode = 0; mode < kNumTypes; ++mode)
        {
            double lo = 1e9, hi = -1e9, offAt18 = 0;
            for (float drive = 0.0f; drive <= 18.0f; drive += 3.0f)
            {
                std::vector<float> L = L0, R = R0;
                Engine e; e.setBands(3); e.setCrossovers(150, 3000, 9000); e.setOversampling(2); bands(e, mode, drive, 100.0f); e.prepare(sr, 512);
                run(e, L, R, 512);
                const double d = rmsDb(L, R, from) - ref;
                lo = std::min(lo, d); hi = std::max(hi, d);
                if (drive == 18.0f)
                {
                    std::vector<float> A = L0, B = R0;
                    Engine f; f.setBands(3); f.setCrossovers(150, 3000, 9000); f.setOversampling(2); f.setAutoGain(false); bands(f, mode, drive, 100.0f); f.prepare(sr, 512);
                    run(f, A, B, 512);
                    offAt18 = rmsDb(A, B, from) - ref;
                }
            }
            worstAll = std::max(worstAll, std::max(std::fabs(lo), std::fabs(hi)));
            printf("    %-6s auto gain: RMS %+.2f .. %+.2f dB re input over drive 0..18 dB (auto gain off at 18 dB: %+.1f dB)\n", kModeNames[mode], lo, hi, offAt18);
        }
        CHECK(worstAll < 1.0, "auto gain: every curve stays within %.2f dB of the input RMS across drive 0..18 dB", worstAll);
    }
    // 7: solo / listen equal band 2 alone (mix 0, so the band is its dry split signal)
    {
        const double sr = 48000;
        const int T = 48000 * 2;
        for (int test = 0; test < 3; ++test)
        {
            std::vector<float> L(T), R(T), rL(T), rR(T);
            for (int i = 0; i < T; ++i) { L[(size_t)i] = rL[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = rR[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            Engine e; e.setBands(3); e.setCrossovers(150, 3000, 9000); e.setOversampling(0);
            for (int b = 0; b < 4; ++b) { BandSettings s; s.mix = 0; if (test == 0) s.solo = b == 1; if (test == 1) s.mute = b != 1; e.setBand(b, s); }
            if (test == 2) e.setListen(1);
            e.prepare(sr, 512);
            run(e, L, R);
            mb::Splitter s; s.setBands(3); s.setCrossovers(150, 3000, 9000); s.prepare(sr, 256);
            double err = 0, ref = 0;
            for (int off = 0; off < T; off += 256)
            {
                s.process(rL.data() + off, rR.data() + off, 256);
                for (int i = 0; i < 256; ++i) { const double a = s.band(1, 0)[i]; err += std::pow(L[(size_t)(off + i)] - a, 2); ref += a * a; }
            }
            static const char* names[] = { "solo band 2", "mute bands 1, 3", "Alt-listen band 2" };
            CHECK(10 * std::log10(err / ref + 1e-30) < -100, "%s: output equals band 2 alone (null %.1f dB)", names[test], 10 * std::log10(err / ref + 1e-30));
        }
    }
    // 8: mode switches and drive sweeps don't click; random settings stay finite and bounded
    {
        const double sr = 48000;
        Engine e; e.setBands(3); e.setCrossovers(150, 3000, 9000); bands(e, kWarm, 6.0f, 100.0f); e.prepare(sr, 512);
        const int T = 48000 * 3;
        std::vector<float> L(T), R(T);
        for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.25 * std::sin(kTwoPi * 440.0 * i / sr));
        double maxStep = 0; float pl = 0;
        for (int off = 0, blk = 0; off < T; off += 128, ++blk)
        {
            const double t = (double)off / T;
            BandSettings s; s.mode = (blk / 60) % kNumTypes; s.drive = (float)(12.0 + 12.0 * std::sin(8.0 * t)); s.mix = 100;
            for (int b = 0; b < 4; ++b) e.setBand(b, s);
            e.process(L.data() + off, R.data() + off, 128);
            for (int i = off; i < off + 128; ++i) { if (off > 4096) maxStep = std::max(maxStep, (double)std::fabs(L[(size_t)i] - pl)); pl = L[(size_t)i]; }
        }
        CHECK(maxStep < 0.08, "curve changes every 160 ms and a 0..24 dB drive sweep on a 440 Hz tone: largest sample step %.4f", maxStep);
        std::mt19937 rng(11);
        std::vector<float> a(512), b2(512);
        bool ok = true;
        for (int blk = 0; blk < 800; ++blk)
        {
            BandSettings s; s.mode = (int)(rng() % kNumTypes); s.drive = (float)(rng() % 25); s.mix = (float)(rng() % 101); s.level = (float)((int)(rng() % 25) - 12);
            s.solo = rng() % 9 == 0; s.mute = rng() % 11 == 0; s.bypass = rng() % 9 == 0;
            e.setBand((int)(rng() % 4), s);
            if (blk % 40 == 0) e.setXoverMode((int)(rng() % 2));
            if (blk % 29 == 0) e.setOversampling((int)(rng() % 4));
            if (blk % 23 == 0) e.setBands(1 + (int)(rng() % 4));
            if (blk % 11 == 0) e.setAutoGain(rng() % 2);
            if (blk % 7 == 0) e.setMix((float)(rng() % 101));
            e.setCrossovers((float)(20 + rng() % 20000), (float)(20 + rng() % 20000), (float)(20 + rng() % 20000));
            for (int i = 0; i < 512; ++i) { a[(size_t)i] = 0.5f * (float)std::sin(0.031 * (blk * 512 + i)); b2[(size_t)i] = 0.4f * (float)std::sin(0.007 * (blk * 512 + i)); }
            e.process(a.data(), b2.data(), 512);
            for (int i = 0; i < 512; ++i) if (!std::isfinite(a[(size_t)i]) || std::fabs(a[(size_t)i]) > 40.0f || !std::isfinite(b2[(size_t)i])) ok = false;
        }
        CHECK(ok, "800 blocks of random settings (curves, drive, mixes, oversampling, crossover type, band count): finite and bounded");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
