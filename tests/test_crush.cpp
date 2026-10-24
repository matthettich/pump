// pump CRUSH: mix 0 and an all-off setting are the input delayed by the latency, exactly; bit reduction gives the
// textbook noise floor (6 dB per bit); companding keeps quiet signals cleaner than linear at the same bits; uneven
// DAC bits distort more; a low rate folds a 5 kHz tone down to 3 kHz with no input filter, while a filtered voicing
// removes it; wow moves the pitch, and an untouched transport doesn't; hiss sits at its level; dropouts dip; noise
// and crackle render identically twice; random settings (voicing / order switches mid-fade, block sizes, an
// absurd input) stay finite and bounded. 44.1, 48 and 96 kHz. Also prints CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/crush/CrushEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::crush;
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
// everything off: the clean voicing at full rate and 24 bits, no media
static Settings off()
{
    Settings s; s.mode = kClean; s.bits = kBitsOff; s.rate = kRateOff; s.drive = 0; s.jitter = 0;
    s.wow = s.flutter = s.hiss = s.crackle = s.dropout = s.age = 0; s.mix = 100;
    return s;
}
static void sine(std::vector<float>& L, std::vector<float>& R, double sr, int n, double f, double amp)
{
    L.resize((size_t)n); R.resize((size_t)n);
    for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(amp * std::sin(kTwoPi * f * i / sr));
}
static double rmsDb(const std::vector<float>& x, size_t from, size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)x[i] * x[i];
    return 10 * std::log10(e / (double)(to - from) + 1e-30);
}
// level (dBFS-ish, Hann-windowed) of the band around frequency f in the last N samples
struct Spec {
    std::vector<double> p; double sr; int N;
    Spec(const std::vector<float>& x, double sampleRate, int n = 16384) : sr(sampleRate), N(n)
    {
        RealFFT fft; fft.setup(N);
        AlignedBuffer in((size_t)N), out((size_t)N);
        const size_t start = x.size() - (size_t)N;
        for (int i = 0; i < N; ++i) in[(size_t)i] = (float)(x[start + (size_t)i] * (0.5 - 0.5 * std::cos(kTwoPi * i / N)));
        fft.forward(in.data(), out.data());
        p.resize((size_t)N / 2);
        for (int k = 1; k < N / 2; ++k) p[(size_t)k] = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
    }
    double at(double f) const
    {
        const int k = (int)std::lround(f * N / sr);
        double s = 0; for (int d = -3; d <= 3; ++d) if (k + d > 0 && k + d < N / 2) s += p[(size_t)(k + d)];
        return 10 * std::log10(s + 1e-30);
    }
};

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)(sr * 2.0);
        std::vector<float> L0((size_t)n), R0((size_t)n);
        for (int i = 0; i < n; ++i) { L0[(size_t)i] = mix[(size_t)(2 * i)]; R0[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
        // ---- mix 0, and everything off: the input delayed by the latency
        for (int pass = 0; pass < 2; ++pass)
        {
            Settings s = pass == 0 ? Settings() : off();
            if (pass == 0) { s.mix = 0; s.bits = 4; s.rate = 3000; s.wow = 80; s.hiss = 60; s.crackle = 60; s.dropout = 60; s.age = 60; }
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            run(e, L, R, 333);
            const int lat = e.latency();
            double err = 0;
            for (int i = lat; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)(i - lat)]) + std::fabs(R[(size_t)i] - R0[(size_t)(i - lat)]));
            CHECK(err < 1e-5, "%s = input delayed by %d samples (max error %.2g)", pass == 0 ? "mix 0 (everything else up)" : "all off", lat, err);
        }
        // ---- bit depth: 8-bit linear on a full-scale sine gives ~50 dB SNR
        {
            for (double b : { 6.0, 8.0, 12.0 })
            {
                Settings s = off(); s.bits = b;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; sine(L, R, sr, n, 997.0, 0.9);
                const std::vector<float> L1 = L;
                run(e, L, R);
                const int lat = e.latency();
                double es = 0, en = 0;
                for (int i = n / 2; i < n; ++i) { const double d = L[(size_t)i] - L1[(size_t)(i - lat)]; en += d * d; es += (double)L1[(size_t)(i - lat)] * L1[(size_t)(i - lat)]; }
                const double snr = 10 * std::log10(es / en), want = 6.02 * b + 1.76 - 0.9;
                CHECK(std::fabs(snr - want) < 3.0, "%.0f bits: SNR %.1f dB (textbook %.1f)", b, snr, want);
            }
        }
        if (sr != 48000.0) continue;
        // ---- companding keeps a quiet signal cleaner than linear at 8 bits; uneven bits distort more
        {
            auto snrOf = [&](int mode, double amp) {
                Settings s = off(); s.mode = mode; s.bits = 8;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; sine(L, R, sr, n, 997.0, amp);
                const std::vector<float> L1 = L;
                run(e, L, R);
                const int lat = e.latency();
                double es = 0, en = 0;
                for (int i = n / 2; i < n; ++i) { const double d = L[(size_t)i] - L1[(size_t)(i - lat)]; en += d * d; es += (double)L1[(size_t)(i - lat)] * L1[(size_t)(i - lat)]; }
                return 10 * std::log10(es / en);
            };
            const double lin = snrOf(kClean, 0.01), mu = snrOf(kCompand8, 0.01);
            CHECK(mu > lin + 15.0, "8-bit at -40 dBFS: companded SNR %.1f dB vs linear %.1f dB", mu, lin);
            Settings s = off();
            const double clean = snrOf(kClean, 0.5);
            // Toy: linear but with uneven bit weights (no filters, so the comparison is fair)
            const double toy = snrOf(kToy, 0.5);
            CHECK(toy < clean - 6.0, "8-bit with uneven bits: SNR %.1f dB vs even %.1f dB", toy, clean);
            (void)s;
        }
        // ---- aliasing: 5 kHz at a 8 kHz rate folds to 3 kHz unfiltered; a filtered voicing removes it
        {
            auto run5k = [&](int mode) {
                Settings s = off(); s.mode = mode; s.rate = 8000; s.bits = kBitsOff;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; sine(L, R, sr, 65536, 5000.0, 0.5);
                run(e, L, R);
                return Spec(L, sr);
            };
            const Spec raw = run5k(kClean), hifi = run5k(kHifi12);
            std::vector<float> L, R; sine(L, R, sr, 65536, 5000.0, 0.5);
            const double ref = Spec(L, sr).at(5000.0);
            CHECK(raw.at(3000.0) > ref - 12.0, "clean at 8 kHz: 5 kHz folds to 3 kHz (%.1f dB re the input tone)", raw.at(3000.0) - ref);
            CHECK(hifi.at(3000.0) < ref - 30.0 && hifi.at(5000.0) < ref - 30.0, "hi-fi 12 at 8 kHz: input filter removes it (3 kHz %.1f, 5 kHz %.1f dB)", hifi.at(3000.0) - ref, hifi.at(5000.0) - ref);
        }
        // ---- wow moves the pitch; an untouched transport doesn't
        {
            auto spread = [&](double wow) {
                Settings s = off(); s.wow = wow;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; sine(L, R, sr, (int)(sr * 4), 1000.0, 0.5);
                run(e, L, R);
                // periods from rising zero crossings (linear interpolation), after the first second
                std::vector<double> per; double last = -1;
                for (size_t i = (size_t)sr; i < L.size(); ++i)
                    if (L[i - 1] < 0.0f && L[i] >= 0.0f)
                    {
                        const double t = (double)(i - 1) + L[i - 1] / (double)(L[i - 1] - L[i]);
                        if (last >= 0) per.push_back(t - last);
                        last = t;
                    }
                double lo = 1e9, hi = 0; for (double p : per) { lo = std::min(lo, p); hi = std::max(hi, p); }
                return 100.0 * (hi - lo) / (sr / 1000.0);
            };
            const double w0 = spread(0), w1 = spread(100);
            CHECK(w0 < 0.01 && w1 > 1.0, "wow 100%%: period spread %.2f %% (wow 0: %.4f %%)", w1, w0);
        }
        // ---- hiss level on silence; deterministic renders; dropouts dip
        {
            Settings s = off(); s.hiss = 100; s.crackle = 70;
            std::vector<float> a((size_t)n, 0.0f), ar((size_t)n, 0.0f), b((size_t)n, 0.0f), br((size_t)n, 0.0f);
            { Engine e; e.setSettings(s); e.prepare(sr, 512); run(e, a, ar); }
            { Engine e; e.setSettings(s); e.prepare(sr, 512); run(e, b, br, 97); }
            double diff = 0; for (int i = 0; i < n; ++i) diff = std::max(diff, (double)std::fabs(a[(size_t)i] - b[(size_t)i]));
            CHECK(diff < 1e-6, "hiss + crackle render identically with different block sizes (max diff %.2g)", diff);
            s.crackle = 0;
            std::vector<float> h((size_t)n, 0.0f), hr((size_t)n, 0.0f);
            Engine e; e.setSettings(s); e.prepare(sr, 512); run(e, h, hr);
            const double lv = rmsDb(h, (size_t)n / 4);
            CHECK(lv > -42.0 && lv < -24.0, "hiss 100%% on silence: %.1f dBFS RMS", lv);
        }
        {
            Settings s = off(); s.dropout = 100;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L, R; sine(L, R, sr, (int)(sr * 20), 440.0, 0.5);
            run(e, L, R);
            double worst = 0;
            const int w = (int)(0.01 * sr);
            for (size_t i = (size_t)sr; i + (size_t)w < L.size(); i += (size_t)w) worst = std::min(worst, rmsDb(L, i, i + (size_t)w) - rmsDb(L, 0, (size_t)sr / 2));
            CHECK(worst < -3.0, "dropouts 100%% over 20 s: deepest dip %.1f dB", worst);
        }
        // ---- stress: random settings, block sizes, voicing and order switches mid-fade, absurd input
        {
            std::mt19937 rng(17);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.mode = (int)(u(rng) * kNumModes); s.drive = u(rng) * 24; s.bits = 1 + u(rng) * 23; s.rate = 200 * std::pow(240.0, u(rng));
                    s.jitter = u(rng) * 100; s.order = u(rng) < 0.5 ? 0 : 1; s.wow = u(rng) * 100; s.flutter = u(rng) * 100;
                    s.hiss = u(rng) * 100; s.crackle = u(rng) * 100; s.dropout = u(rng) * 100; s.age = u(rng) * 100;
                    s.lowCut = 10 * std::pow(100.0, u(rng)); s.highCut = 1000 * std::pow(22.0, u(rng)); s.mix = u(rng) * 100; s.outDb = 0;
                    e.setSettings(s);
                }
                if (it % 13 == 5) { Settings t = s; t.mode = (s.mode + 1) % kNumModes; t.order = 1 - s.order; e.setSettings(t); e.setSettings(s); }
                const int bs = 1 + (int)(u(rng) * 1200);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 1500 && i == 0) L[0] = 1000.0f;
                }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                    if (it > 1600) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 8.0, "random settings, sizes and mid-fade switches: finite, peak %.2f after a +60 dB spike", peak);
        }
        // ---- CPU
        {
            Settings s; s.mode = kDark12; s.wow = 50; s.flutter = 50; s.hiss = 50; s.crackle = 50; s.dropout = 50; s.age = 50; s.jitter = 20;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            const auto t0 = std::chrono::steady_clock::now();
            run(e, L, R, 512);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: everything on: %.1f %% of one core\n", 100.0 * secs / 2.0);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
