// pump BAND: crossover reconstruction (analog allpass sum, linear-phase exact null), latency, per-band gain
// accuracy for compression / expansion / upward compression with sines, detection modes, solo/mute/listen,
// band-count and crossover changes without clicks, at 44.1, 48 and 96 kHz.
#include "../plugins/band/BandEngine.hpp"
#include <cstdio>
#include <random>
#include <thread>

using namespace pump;
using namespace pump::band;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}

static double magDb(const std::vector<float>& h, double f, double sr, size_t from = 0)
{
    double re = 0, im = 0;
    for (size_t n = from; n < h.size(); ++n) { re += h[n] * std::cos(kTwoPi * f * n / sr); im -= h[n] * std::sin(kTwoPi * f * n / sr); }
    return 10 * std::log10(re * re + im * im + 1e-30);
}

// neutral bands: ratio 1 (no gain change at any level)
static void neutral(Engine& e)
{
    for (int b = 0; b < kNumBands; ++b) { BandSettings s; s.ratio = 1.0f; s.thresh = -30.0f; e.setBand(b, s); }
}
static void waitKernels(Engine& e, float* L, float* R)
{
    // process silence until a requested linear-phase kernel set has been designed and faded in
    for (int k = 0; k < 400 && !e.kernelsCurrent(); ++k)
    {
        std::fill(L, L + 512, 0.0f); std::fill(R, R + 512, 0.0f);
        e.process(L, R, 512);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    const int n = (int)L.size();
    for (int off = 0; off < n; off += block) e.process(L.data() + off, R.data() + off, std::min(block, n - off));
}

static const float kX[3] = { 120.0f, 1000.0f, 6000.0f };

int main()
{
    const std::vector<float> mix = loadMix("/home/claude/testkit/mix.f32");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        // ---- 1. crossover sums: analog = allpass (flat magnitude), linear = the delayed input exactly
        for (int mode = 0; mode <= 1; ++mode)
            for (int nb = 1; nb <= 4; ++nb)
            {
                Engine e; e.setBands(nb); e.setCrossovers(kX[0], kX[1], kX[2]); e.setXoverMode(mode); neutral(e);
                e.prepare(sr, 512);
                std::vector<float> z(512), z2(512);
                waitKernels(e, z.data(), z2.data());
                const int lat = e.latency();
                const int len = mode ? 65536 : 32768;
                std::vector<float> L((size_t)len, 0.0f), R((size_t)len, 0.0f);
                L[0] = 1.0f; R[0] = 1.0f;
                run(e, L, R);
                if (mode == 0)
                {
                    double worst = 0, wf = 0;
                    for (int i = 0; i <= 200; ++i)
                    {
                        const double f = 10.0 * std::pow(0.49 * sr / 10.0, i / 200.0);
                        const double m = magDb(L, f, sr);
                        if (std::fabs(m) > worst) { worst = std::fabs(m); wf = f; }
                    }
                    CHECK(worst < 0.001, "analog, %d band%s: band sum ripple %.5f dB max (at %.0f Hz), latency %d", nb, nb > 1 ? "s" : "", worst, wf, lat);
                }
                else
                {
                    double err = 0, pk = 0; int pkAt = 0;
                    for (int i = 0; i < len; ++i)
                    {
                        const double want = i == lat ? 1.0 : 0.0;
                        err = std::max(err, std::fabs(L[(size_t)i] - want));
                        if (std::fabs(L[(size_t)i]) > pk) { pk = std::fabs(L[(size_t)i]); pkAt = i; }
                    }
                    CHECK(pkAt == lat && err < 1e-5, "linear, %d band%s: impulse at %d = latency %d, reconstruction error %.1e (%.0f dB)", nb, nb > 1 ? "s" : "", pkAt, lat, err, 20 * std::log10(err + 1e-30));
                }
            }
        // ---- 2. the band shapes match the display model (both modes): band 1 of 4 at a few frequencies
        for (int mode = 0; mode <= 1; ++mode)
        {
            Engine e; e.setBands(4); e.setCrossovers(kX[0], kX[1], kX[2]); e.setXoverMode(mode); neutral(e);
            e.prepare(sr, 512);
            std::vector<float> z(512), z2(512);
            waitKernels(e, z.data(), z2.data());
            for (int b = 0; b < kNumBands; ++b) { BandSettings s; s.ratio = 1.0f; s.solo = b == 1; e.setBand(b, s); }
            for (int k = 0; k < 40; ++k) { std::fill(z.begin(), z.end(), 0.0f); std::fill(z2.begin(), z2.end(), 0.0f); e.process(z.data(), z2.data(), 512); }
            const int len = mode ? 65536 : 32768;
            std::vector<float> L((size_t)len, 0.0f), R((size_t)len, 0.0f);
            L[0] = 1.0f; R[0] = 1.0f;
            run(e, L, R);
            double ex[3]; e.effectiveCrossovers(ex);
            double worst = 0;
            for (double f : { 60.0, 120.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0 })
            {
                const double want = 20 * std::log10(mb::bandMag(4, ex, 1, f, sr));
                worst = std::max(worst, std::fabs(magDb(L, f, sr) - want));
            }
            CHECK(worst < (mode ? 0.05 : 0.01), "%s: soloed band 2 of 4 matches the LR4 display model within %.4f dB", mode ? "linear" : "analog", worst);
        }
        // ---- 3. engine with neutral bands: mix output nulls against the reference (analog: allpass path)
        if (!mix.empty())
            for (int mode = 0; mode <= 1; ++mode)
            {
                const int T = std::min((int)mix.size() / 2, (int)(sr * 4));
                std::vector<float> L(T), R(T), rL(T), rR(T);
                for (int i = 0; i < T; ++i) { L[(size_t)i] = rL[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = rR[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
                Engine e; e.setBands(4); e.setCrossovers(kX[0], kX[1], kX[2]); e.setXoverMode(mode); neutral(e);
                e.prepare(sr, 512);
                std::vector<float> z(512), z2(512);
                waitKernels(e, z.data(), z2.data());
                e.prepare(sr, 512);   // clean state, kernels are designed synchronously by prepare()
                run(e, L, R, 300);
                double err = 0, ref = 0;
                const int lat = e.latency();
                if (mode == 1)
                {
                    for (int i = lat; i < T; ++i) { err += std::pow(L[(size_t)i] - rL[(size_t)(i - lat)], 2) + std::pow(R[(size_t)i] - rR[(size_t)(i - lat)], 2); ref += rL[(size_t)(i - lat)] * rL[(size_t)(i - lat)] + rR[(size_t)(i - lat)] * rR[(size_t)(i - lat)]; }
                }
                else
                {
                    mb::Splitter s; s.setBands(4); s.setCrossovers(kX[0], kX[1], kX[2]); s.prepare(sr, 300);
                    for (int off = 0; off < T; off += 300)
                    {
                        const int n = std::min(300, T - off);
                        s.process(rL.data() + off, rR.data() + off, n);
                        for (int i = 0; i < n; ++i)
                        {
                            double a = 0, b = 0;
                            for (int k = 0; k < 4; ++k) { a += s.band(k, 0)[i]; b += s.band(k, 1)[i]; }
                            err += std::pow(L[(size_t)(off + i)] - a, 2) + std::pow(R[(size_t)(off + i)] - b, 2);
                            ref += a * a + b * b;
                        }
                    }
                }
                const double db = 10 * std::log10(err / std::max(ref, 1e-30) + 1e-30);
                CHECK(db < -100, "%s: neutral bands on the test mix null against %s: %.1f dB", mode ? "linear" : "analog", mode ? "the input delayed by the latency" : "the crossover allpass path", db);
            }
        // ---- 4. per-band gain with sines (peak detection, knee 0, mix 100)
        struct Case { int mode; float thr, ratio, levelDb, range; };
        const Case cases[] = { { kCompress, -20, 4, -6, 60 }, { kExpand, -20, 2, -32, 60 }, { kUpward, -20, 2, -40, 60 }, { kCompress, -30, 20, -10, 12 } };
        for (int mode = 0; mode <= 1; ++mode)
        {
            double worst = 0; char where[96] = "";
            double ex[3];
            for (const Case& c : cases)
                for (int b = 0; b < 4; ++b)
                {
                    Engine e; e.setBands(4); e.setCrossovers(kX[0], kX[1], kX[2]); e.setXoverMode(mode); neutral(e);
                    BandSettings s; s.mode = c.mode; s.thresh = c.thr; s.ratio = c.ratio; s.knee = 0; s.range = c.range; s.attack = 2; s.release = 50;
                    e.setBand(b, s);
                    e.prepare(sr, 512);
                    e.effectiveCrossovers(ex);
                    const double lo = b == 0 ? 30.0 : ex[b - 1], hi = b == 3 ? std::min(16000.0, 0.4 * sr) : ex[b];
                    const double f = std::sqrt(lo * hi);
                    const int T = (int)(sr * 1.5);
                    std::vector<float> L(T), R(T);
                    const double A = dbToGain(c.levelDb);
                    for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = (float)(A * std::sin(kTwoPi * f * i / sr));
                    run(e, L, R);
                    // measured output amplitude over the last 0.5 s
                    double re = 0, im = 0; const int i0 = T - (int)(sr * 0.5);
                    for (int i = i0; i < T; ++i) { re += L[(size_t)i] * std::cos(kTwoPi * f * i / sr); im += L[(size_t)i] * std::sin(kTwoPi * f * i / sr); }
                    const double outA = 2.0 * std::sqrt(re * re + im * im) / (T - i0);
                    // expected: the band sees A*m, gets the static gain; the other bands pass their share unchanged
                    const double m = mb::bandMag(4, ex, b, f, sr);
                    const double g = dbToGain(staticGain(c.mode, gainToDb(A * m), c.thr, c.ratio, 0.0, c.range));
                    const double want = A * (m * g + (1.0 - m));
                    const double d = std::fabs(gainToDb(outA) - gainToDb(want));
                    if (d > worst) { worst = d; std::snprintf(where, sizeof where, "%s band %d at %.0f Hz, expected %+.2f dB", kModeNames[c.mode], b + 1, f, gainToDb(want / A)); }
                }
            CHECK(worst < 0.1, "%s: per-band gain with sines (16 cases: compress 4:1, expand 2:1, upward 2:1, 20:1 range 12) worst error %.3f dB (%s)", mode ? "linear" : "analog", worst, where);
        }
    }

    // ---- 5. RMS detection: a 1 kHz sine reads 3 dB below its peak
    {
        const double sr = 48000;
        Engine e; e.setBands(1); neutral(e);
        BandSettings s; s.thresh = -20; s.ratio = 4; s.knee = 0; s.range = 60; s.attack = 5; s.release = 100; e.setBand(0, s);
        e.setDetection(1);
        e.prepare(sr, 512);
        const int T = 48000; std::vector<float> L(T), R(T);
        for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = (float)(dbToGain(-6.0) * std::sin(kTwoPi * 1000.0 * i / sr));
        run(e, L, R);
        double pk = 0; for (int i = T / 2; i < T; ++i) pk = std::max(pk, (double)std::fabs(L[(size_t)i]));
        const double want = -6.0 + staticGain(kCompress, -6.0 - 3.0103, -20, 4, 0, 60);
        CHECK(std::fabs(gainToDb(pk) - want) < 0.1, "RMS detection, -6 dBFS 1 kHz sine, -20 dB 4:1: output %.2f dBFS (expect %.2f)", gainToDb(pk), want);
    }
    // ---- 6. stereo link: a loud left and silent right channel; 100% link turns the right down too, 0% doesn't
    for (float link : { 100.0f, 0.0f })
    {
        const double sr = 48000;
        Engine e; e.setBands(1); neutral(e);
        BandSettings s; s.thresh = -20; s.ratio = 4; s.knee = 0; s.range = 60; s.attack = 2; s.release = 50; e.setBand(0, s);
        e.setLink(link);
        e.prepare(sr, 512);
        const int T = 48000; std::vector<float> L(T), R(T);
        for (int i = 0; i < T; ++i) { L[(size_t)i] = (float)(0.5 * std::sin(kTwoPi * 1000.0 * i / sr)); R[(size_t)i] = (float)(0.01 * std::sin(kTwoPi * 1000.0 * i / sr)); }
        run(e, L, R);
        double pr = 0; for (int i = T / 2; i < T; ++i) pr = std::max(pr, (double)std::fabs(R[(size_t)i]));
        const double gr = gainToDb(pr / 0.01);
        if (link > 50) CHECK(std::fabs(gr - staticGain(kCompress, gainToDb(0.5), -20, 4, 0, 60)) < 0.15, "stereo link 100%%: quiet channel follows the loud one: %.2f dB", gr);
        else CHECK(std::fabs(gr) < 0.05, "stereo link 0%%: quiet channel untouched: %.2f dB", gr);
    }
    // ---- 7. solo / mute / listen equal the reference band (analog, 48k, 4 bands)
    if (!mix.empty())
    {
        const double sr = 48000;
        const int T = 48000 * 2;
        for (int test = 0; test < 3; ++test)
        {
            std::vector<float> L(T), R(T), rL(T), rR(T);
            for (int i = 0; i < T; ++i) { L[(size_t)i] = rL[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = rR[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            Engine e; e.setBands(4); e.setCrossovers(kX[0], kX[1], kX[2]);
            for (int b = 0; b < 4; ++b)
            {
                BandSettings s; s.ratio = 1.0f;
                if (test == 0) s.solo = b == 2;                  // solo band 3
                if (test == 1) s.mute = b != 2;                  // mute everything else
                e.setBand(b, s);
            }
            if (test == 2) e.setListen(2);                       // Alt-listen band 3
            e.prepare(sr, 512);
            run(e, L, R, 256);
            mb::Splitter s; s.setBands(4); s.setCrossovers(kX[0], kX[1], kX[2]); s.prepare(sr, 256);
            double err = 0, ref = 0;
            for (int off = 0; off < T; off += 256)
            {
                s.process(rL.data() + off, rR.data() + off, 256);
                for (int i = 0; i < 256; ++i)
                {
                    const double a = s.band(2, 0)[i];
                    err += std::pow(L[(size_t)(off + i)] - a, 2); ref += a * a;
                }
            }
            static const char* names[] = { "solo band 3", "mute bands 1, 2, 4", "Alt-listen band 3" };
            CHECK(10 * std::log10(err / ref + 1e-30) < -100, "%s: output equals band 3 alone (null %.1f dB)", names[test], 10 * std::log10(err / ref + 1e-30));
        }
    }
    // ---- 8. lookahead adds exactly its latency, and catches a transient: a 5 ms lookahead fast-attack compressor
    // cuts a sudden burst before it starts
    {
        const double sr = 48000;
        for (int mode = 0; mode <= 1; ++mode)
        {
            Engine e; e.setBands(2); e.setCrossovers(1000, 4000, 8000); e.setXoverMode(mode); neutral(e); e.setLookahead(5.0f);
            e.prepare(sr, 512);
            const int lat = e.latency();
            const int len = 32768;
            std::vector<float> L((size_t)len, 0.0f), R((size_t)len, 0.0f);
            L[100] = 1.0f; R[100] = 1.0f;
            run(e, L, R);
            int pk = 0; float pv = 0; for (int i = 0; i < len; ++i) if (std::fabs(L[(size_t)i]) > pv) { pv = std::fabs(L[(size_t)i]); pk = i; }
            const int want = (mode ? (int)e.latency() : 240);
            if (mode == 1) CHECK(pk - 100 == lat, "linear + 5 ms lookahead: impulse at %d, reported latency %d", pk - 100, lat);
            else CHECK(pk - 100 == lat && lat == want, "analog + 5 ms lookahead: impulse at %d, reported latency %d (240 expected)", pk - 100, lat);
        }
        Engine e; e.setBands(1); neutral(e);
        BandSettings s; s.thresh = -30; s.ratio = 20; s.knee = 0; s.range = 60; s.attack = 0.5f; s.release = 100; e.setBand(0, s);
        e.setLookahead(5.0f);
        e.prepare(sr, 512);
        const int T = 24000; std::vector<float> L(T, 0.0f), R(T, 0.0f);
        for (int i = 12000; i < T; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.9 * std::sin(kTwoPi * 2000.0 * i / sr));
        run(e, L, R);
        const int lat = e.latency();
        double first = 0; for (int i = 12000 + lat; i < 12000 + lat + 48; ++i) first = std::max(first, (double)std::fabs(L[(size_t)i]));
        CHECK(gainToDb(first) < -12, "5 ms lookahead, 0.5 ms attack: the first millisecond of a 0.9 burst is already down to %.1f dBFS", gainToDb(first));
    }
    // ---- 9. no clicks: sweeping crossovers, switching band counts and modes while a sine plays
    {
        const double sr = 48000;
        for (int mode = 0; mode <= 1; ++mode)
        {
            Engine e; e.setBands(4); e.setCrossovers(kX[0], kX[1], kX[2]); e.setXoverMode(mode); neutral(e);
            e.prepare(sr, 512);
            const int T = 48000 * 3;
            std::vector<float> L(T), R(T);
            const double f = 440.0;
            for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * f * i / sr));
            double maxStep = 0; float prev = 0;
            for (int off = 0, blk = 0; off < T; off += 128, ++blk)
            {
                // crossover 2 sweeps through the sine's frequency; the band count changes every 0.5 s
                const double t = (double)off / T;
                e.setCrossovers(120, (float)(200.0 * std::pow(10.0, 1.3 * t)), 9000);
                e.setBands(4 - (blk / 190) % 3);
                e.process(L.data() + off, R.data() + off, 128);
                for (int i = off; i < off + 128; ++i) { maxStep = std::max(maxStep, (double)std::fabs(L[(size_t)i] - prev)); prev = L[(size_t)i]; }
                if (mode == 1) std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            // a 0.3 sine at 440 Hz moves at most 2*pi*440/48000*0.3 = 0.0173 per sample (+ a little allpass gain at changes)
            const int lat = e.latency();
            (void)lat;
            CHECK(maxStep < 0.03, "%s: crossover sweep through a 440 Hz sine plus band-count changes: largest sample step %.4f (sine alone 0.0173)", mode ? "linear" : "analog", maxStep);
        }
    }
    // ---- 10. random settings stress: finite and bounded
    {
        Engine e; e.prepare(48000, 512);
        std::mt19937 rng(5);
        std::vector<float> L(512), R(512);
        bool ok = true;
        for (int blk = 0; blk < 800; ++blk)
        {
            BandSettings s; s.mode = (int)(rng() % 3); s.thresh = -(float)(rng() % 60); s.ratio = 1.0f + (rng() % 190) / 10.0f; s.attack = 0.1f + (rng() % 100); s.release = 5.0f + (rng() % 1000);
            s.knee = (float)(rng() % 30); s.range = (float)(rng() % 60); s.makeup = (float)((int)(rng() % 25) - 12); s.solo = rng() % 7 == 0; s.mute = rng() % 9 == 0; s.bypass = rng() % 9 == 0;
            e.setBand((int)(rng() % 4), s);
            if (blk % 50 == 0) e.setXoverMode((int)(rng() % 2));
            if (blk % 23 == 0) e.setBands(1 + (int)(rng() % 4));
            if (blk % 31 == 0) e.setLookahead((float)(rng() % 11));
            if (blk % 17 == 0) e.setDetection((int)(rng() % 2));
            e.setCrossovers((float)(20 + rng() % 20000), (float)(20 + rng() % 20000), (float)(20 + rng() % 20000));
            for (int i = 0; i < 512; ++i) L[(size_t)i] = R[(size_t)i] = 0.3f * (float)std::sin(0.031 * (blk * 512 + i)) + 0.1f * (float)std::sin(0.0021 * (blk * 512 + i));
            e.process(L.data(), R.data(), 512);
            for (float v : L) if (!std::isfinite(v) || std::fabs(v) > 20.0f) ok = false;
        }
        CHECK(ok, "800 blocks of random settings (modes, band counts, crossovers, lookahead, detection): finite and bounded");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
