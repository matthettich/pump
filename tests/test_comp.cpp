// pump COMP engine: static curve accuracy, attack/release time constants, lookahead and oversampling latency
// alignment, parallel-mix nulls, mid/side, external sidechain, silence/denormals, every style, 44.1/48/96 kHz.
#include "../plugins/comp/CompEngine.hpp"
#include "../common/dsp/Bypass.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::comp;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings base()
{
    Settings s;
    s.style = 0; s.threshold = -30; s.ratio = 4; s.knee = 6; s.range = 60; s.attack = 1; s.release = 60;
    return s;
}
static void make(Engine& e, double sr, const Settings& s)
{
    e.setSettings(s);
    e.prepare(sr, 512);
    e.commitTopology();
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, const float* scL = nullptr, const float* scR = nullptr, int block = 256)
{
    const int n = (int)L.size();
    for (int off = 0; off < n; off += block)
    {
        const int m = std::min(block, n - off);
        e.process(L.data() + off, R.data() + off, scL ? scL + off : nullptr, scR ? scR + off : nullptr, L.data() + off, R.data() + off, m);
    }
}
static std::vector<float> sine(double sr, double f, double amp, int n, double ph = 0.0)
{
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i) v[(size_t)i] = (float)(amp * std::sin(kTwoPi * f * i / sr + ph));
    return v;
}
static double rmsDb(const std::vector<float>& v, int a, int b)
{
    double s = 0.0;
    for (int i = a; i < b; ++i) s += (double)v[(size_t)i] * v[(size_t)i];
    return 10.0 * std::log10(s / std::max(1, b - a) + 1e-30);
}


// characterisation of the styles and the remaining controls (48 kHz)
static void extraTests()
{
    const double sr = 48000.0;
    printf("== behaviour (48000 Hz)\n");
    // Punch lets the first milliseconds of a transient through (compared with Clean at the same settings)
    {
        double crest[2] = { 0, 0 };
        const int styles[2] = { 0, 3 };
        for (int k = 0; k < 2; ++k)
        {
            Engine e; Settings s = base(); s.style = styles[k]; s.threshold = -30; s.ratio = 8; s.knee = 0; s.attack = 5; s.release = 120; make(e, sr, s);
            const int n = (int)(2.0 * sr);
            std::vector<float> L((size_t)n, 0.0f), R;
            // drum-like hits: 4 per second, a 3 ms click on a decaying 80 Hz body
            for (int i = 0; i < n; ++i)
            {
                const int t = i % (int)(sr / 4);
                const double env = std::exp(-t / (0.060 * sr)), click = t < (int)(0.003 * sr) ? 0.6 : 0.0;
                L[(size_t)i] = (float)(0.7 * env * std::sin(kTwoPi * 80.0 * t / sr) + click * std::sin(kTwoPi * 3000.0 * t / sr));
            }
            R = L;
            run(e, L, R);
            // level of the first 4 ms of each hit relative to 20-60 ms (the body), last 4 hits
            double a = 0, b = 0;
            for (int h = 4; h < 8; ++h)
            {
                const int o = h * (int)(sr / 4);
                for (int i = 0; i < (int)(0.004 * sr); ++i) a = std::max(a, (double)std::fabs(L[(size_t)(o + i)]));
                for (int i = (int)(0.020 * sr); i < (int)(0.060 * sr); ++i) b = std::max(b, (double)std::fabs(L[(size_t)(o + i)]));
            }
            crest[k] = toDb(a) - toDb(b);
        }
        CHECK(crest[1] > crest[0] + 1.5, "transient vs body after compression: Clean %.2f dB, Punch %.2f dB (Punch lets the hit through)", crest[0], crest[1]);
    }
    // Vari-Mu colour: gentle, rises with gain reduction; Clean adds none
    {
        auto thd = [&](int style, double thr) {
            Engine e; Settings s = base(); s.style = style; s.threshold = thr; s.ratio = 2; s.knee = 6; s.attack = 20; s.release = 300; s.oversampling = 1; make(e, sr, s);
            const int n = (int)(1.0 * sr);
            std::vector<float> L = sine(sr, 1000.0, dbToGain(-6), n), R = L;
            run(e, L, R);
            // harmonics 2..5 against the fundamental (DFT over the last 0.5 s)
            auto mag = [&](double f) { double re = 0, im = 0; for (int i = n / 2; i < n; ++i) { re += L[(size_t)i] * std::cos(kTwoPi * f * i / sr); im += L[(size_t)i] * std::sin(kTwoPi * f * i / sr); } return std::sqrt(re * re + im * im); };
            const double f1 = mag(1000.0);
            double h = 0; for (int k = 2; k <= 5; ++k) h += std::pow(mag(1000.0 * k), 2);
            return 100.0 * std::sqrt(h) / f1;
        };
        const double vClean = thd(0, -30), v0 = thd(4, 0), v1 = thd(4, -30);
        CHECK(vClean < 0.01 && v0 > 0.02 && v0 < 0.3 && v1 > 2.0 * v0 && v1 < 1.5, "THD at -6 dBFS, 1 kHz: Clean %.4f%%, Vari-Mu uncompressed %.3f%%, Vari-Mu compressing %.3f%%", vClean, v0, v1);
    }
    // bypass through the plugin's BypassMixer is sample-exact against the latency (lookahead 5 ms, 4x)
    {
        Engine e; Settings s = base(); s.lookahead = 5; s.oversampling = 2; make(e, sr, s);
        BypassMixer by; by.prepare(sr, e.maxLatencySamples() + 8, 256); by.setLatency(e.latency());
        const int n = (int)sr;
        std::vector<float> in = sine(sr, 700, 0.8, n), L = in, R = in;
        for (int off = 0; off < n; off += 256)
        {
            const int m = std::min(256, n - off);
            by.capture(L.data() + off, R.data() + off, m);
            e.process(L.data() + off, R.data() + off, nullptr, nullptr, L.data() + off, R.data() + off, m);
            by.apply(L.data() + off, R.data() + off, m, true);
        }
        double d = 0; const int lat = e.latency();
        for (int i = n / 2; i < n; ++i) d = std::max(d, (double)std::fabs(L[(size_t)i] - in[(size_t)(i - lat)]));
        CHECK(d < 1e-6, "bypassed (lookahead 5 ms + 4x, latency %d): output equals the input delayed by the latency, max error %.1g", lat, d);
    }
    // hold: the release starts `hold` later
    {
        double t63[2];
        for (int k = 0; k < 2; ++k)
        {
            Engine e; Settings s = base(); s.knee = 0; s.attack = 1; s.release = 50; s.hold = k ? 80.0 : 0.0; make(e, sr, s);
            const int n0 = (int)(0.3 * sr), n1 = (int)(0.3 * sr), n2 = (int)(0.5 * sr);
            std::vector<float> L((size_t)(n0 + n1 + n2)), R;
            for (int i = 0; i < (int)L.size(); ++i) L[(size_t)i] = (float)(((i >= n0 && i < n0 + n1) ? dbToGain(-10) : dbToGain(-40)) * std::sin(kTwoPi * 1000.0 * i / sr));
            R = L;
            std::vector<double> gr(L.size());
            for (size_t i = 0; i < L.size(); ++i) { e.process(&L[i], &R[i], nullptr, nullptr, &L[i], &R[i], 1); gr[i] = e.grDb(0); }
            const double full = gr[(size_t)(n0 + n1 - 1)];
            int tr = -1;
            for (int i = n0 + n1; i < (int)gr.size(); ++i) if (gr[(size_t)i] >= full * std::exp(-1.0)) { tr = i - n0 - n1; break; }
            t63[k] = tr * 1000.0 / sr;
        }
        CHECK(std::fabs(t63[1] - t63[0] - 80.0) < 2.0, "hold 80 ms: release reaches 63%% after %.1f ms (without hold %.1f ms)", t63[1], t63[0]);
    }
    // auto release: short peaks recover faster than sustained compression
    {
        auto recover = [&](double burstMs, bool autoRel) {
            Engine e; Settings s = base(); s.knee = 0; s.attack = 1; s.release = 200; s.autoRelease = autoRel; make(e, sr, s);
            const int n0 = (int)(0.3 * sr), n1 = (int)(burstMs * 0.001 * sr), n2 = (int)(2.5 * sr);
            std::vector<float> L((size_t)(n0 + n1 + n2)), R;
            for (int i = 0; i < (int)L.size(); ++i) L[(size_t)i] = (float)(((i >= n0 && i < n0 + n1) ? dbToGain(-10) : dbToGain(-40)) * std::sin(kTwoPi * 1000.0 * i / sr));
            R = L;
            std::vector<double> gr(L.size());
            for (size_t i = 0; i < L.size(); i += 16) { const int m = (int)std::min<size_t>(16, L.size() - i); e.process(&L[i], &R[i], nullptr, nullptr, &L[i], &R[i], m); gr[i] = e.grDb(0); }
            double full = 0; for (int i = n0; i < n0 + n1; i += 16) full = std::min(full, gr[(size_t)(i - i % 16)]);
            for (int i = (n0 + n1) / 16 * 16 + 16; i < (int)gr.size(); i += 16) if (gr[(size_t)i] >= full * 0.1) return (i - n0 - n1) * 1000.0 / sr;   // 90% recovered
            return -1.0;
        };
        const double fShort = recover(30, false), fLong = recover(1500, false), aShort = recover(30, true), aLong = recover(1500, true);
        CHECK(aLong > 2.0 * aShort && std::fabs(fLong - fShort) < 0.25 * fShort,
              "release 200 ms, 90%% recovery: fixed %.0f ms (30 ms burst) / %.0f ms (1.5 s burst); auto %.0f ms / %.0f ms", fShort, fLong, aShort, aLong);
    }
    // stereo link: 0% leaves the quiet channel alone, 100% reduces it like the loud one
    {
        double r[2];
        for (int k = 0; k < 2; ++k)
        {
            Engine e; Settings s = base(); s.link = k ? 100 : 0; s.release = 50; make(e, sr, s);
            const int n = (int)sr;
            std::vector<float> L = sine(sr, 400, dbToGain(-6), n), R = sine(sr, 400, dbToGain(-45), n), inR = R;
            run(e, L, R);
            r[k] = rmsDb(R, n / 2, n) - rmsDb(inR, n / 2, n);
        }
        Curve c; c.set(0, -30, 4, 6, 60);
        CHECK(std::fabs(r[0]) < 0.01 && std::fabs(r[1] - c.gain(-6)) < 0.1, "stereo link: quiet channel %.2f dB at 0%%, %.2f dB at 100%% (loud channel's curve %.2f)", r[0], r[1], c.gain(-6));
    }
    // CPU: real-time factor of the worst case (4x at 96 kHz, Glue + auto release, M/S)
    {
        Engine e; Settings s = base(); s.style = 1; s.autoRelease = true; s.oversampling = 2; s.channelMode = 1; make(e, 96000.0, s);
        const int n = 96000 * 10;
        std::vector<float> L((size_t)n), R((size_t)n);
        std::mt19937 rng(9); std::uniform_real_distribution<float> u(-0.5f, 0.5f);
        for (int i = 0; i < n; ++i) { L[(size_t)i] = u(rng); R[(size_t)i] = u(rng); }
        const auto t0 = std::chrono::steady_clock::now();
        run(e, L, R);
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  info: 96 kHz, 4x, Glue + auto release, M/S: %.1fx real time (%.1f%% of one core)\n", 10.0 / t, 100.0 * t / 10.0);
        Engine f; Settings s1 = base(); make(f, 48000.0, s1);
        std::vector<float> A(L.begin(), L.begin() + 480000), B(R.begin(), R.begin() + 480000);
        const auto t1 = std::chrono::steady_clock::now();
        run(f, A, B);
        const double tt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        printf("  info: 48 kHz, 1x, Clean: %.0fx real time (%.2f%% of one core)\n", 10.0 / tt, 100.0 * tt / 10.0);
    }
}

int main()
{
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    for (double sr : rates)
    {
        printf("== %.0f Hz\n", sr);
        // 1) static curve accuracy, Clean, steady sines at several levels and frequencies (fast release: the
        //    analytic-envelope detector has no ripple, so the measured gain matches the curve)
        {
            double worst = 0.0, wl = 0, wf = 0;
            for (double f : { 60.0, 1000.0, 6000.0 })
                for (double lvl : { -48.0, -36.0, -33.0, -30.0, -27.0, -20.0, -10.0, -3.0, 0.0 })
                {
                    Engine e; Settings s = base(); s.release = 30; make(e, sr, s);
                    const int n = (int)(sr * 1.0);
                    std::vector<float> L = sine(sr, f, dbToGain(lvl), n), R = L, in = L;
                    run(e, L, R);
                    const int a = n / 2;
                    const double meas = rmsDb(L, a, n) - rmsDb(in, a, n);
                    Curve c; c.set(0, s.threshold, s.ratio, s.knee, s.range);
                    const double err = std::fabs(meas - c.gain(lvl));
                    if (err > worst) { worst = err; wl = lvl; wf = f; }
                }
            CHECK(worst < 0.2, "Clean static curve (thr -30, 4:1, knee 6, release 30 ms): worst error %.3f dB (%.0f dBFS at %.0f Hz)", worst, wl, wf);
            // infinite ratio and range
            Engine e; Settings s = base(); s.ratio = 100; s.range = 9; s.release = 30; make(e, sr, s);
            const int n = (int)sr;
            std::vector<float> L = sine(sr, 440, dbToGain(-6), n), R = L, in = L;
            run(e, L, R);
            const double g = rmsDb(L, n / 2, n) - rmsDb(in, n / 2, n);
            CHECK(std::fabs(g + 9.0) < 0.2, "range 9 dB at INF:1 with a -6 dBFS tone: %.2f dB", g);
        }
        // 2) attack / release time constants from a step (-40 -> -10 dBFS, thr -30, 4:1 hard knee: 15 dB target)
        for (double atk : { 1.0, 10.0, 50.0 })
            for (double rel : { 50.0, 300.0 })
            {
                Engine e; Settings s = base(); s.knee = 0; s.attack = atk; s.release = rel; make(e, sr, s);
                const int n0 = (int)(0.3 * sr), n1 = (int)(1.0 * sr), n2 = (int)(2.0 * sr);
                std::vector<float> L((size_t)(n0 + n1 + n2)), R;
                for (int i = 0; i < (int)L.size(); ++i)
                {
                    const double a = (i >= n0 && i < n0 + n1) ? dbToGain(-10) : dbToGain(-40);
                    L[(size_t)i] = (float)(a * std::sin(kTwoPi * 1000.0 * i / sr));
                }
                R = L;
                std::vector<double> gr(L.size());
                for (size_t i = 0; i < L.size(); ++i) { e.process(&L[i], &R[i], nullptr, nullptr, &L[i], &R[i], 1); gr[i] = e.grDb(0); }
                const double full = gr[(size_t)(n0 + n1 - 1)];
                int ta = -1, tr = -1;
                for (int i = n0; i < n0 + n1; ++i) if (gr[(size_t)i] <= full * (1.0 - std::exp(-1.0))) { ta = i - n0; break; }
                for (int i = n0 + n1; i < (int)gr.size(); ++i) if (gr[(size_t)i] >= full * std::exp(-1.0)) { tr = i - n0 - n1; break; }
                const double tam = ta * 1000.0 / sr, trm = tr * 1000.0 / sr;
                CHECK(std::fabs(full + 15.0) < 0.15 && ta > 0 && std::fabs(tam - atk) < 0.1 * atk + 0.1 && tr > 0 && std::fabs(trm - rel) < 0.06 * rel,
                      "step: GR %.2f dB; attack %.1f ms -> 63%% in %.2f ms; release %.0f ms -> 63%% in %.1f ms", full, atk, tam, rel, trm);
            }
        // 3) lookahead: reported latency = impulse position; and the gain is down before the transient arrives
        for (double la : { 0.0, 2.0, 5.0, 20.0 })
        {
            Engine e; Settings s = base(); s.threshold = 0; s.lookahead = la; make(e, sr, s);
            const int n = (int)(0.2 * sr);
            std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
            L[100] = 0.5f; R[100] = -0.5f;
            run(e, L, R);
            int pk = 0; for (int i = 0; i < n; ++i) if (std::fabs(L[(size_t)i]) > std::fabs(L[(size_t)pk])) pk = i;
            const int expect = (int)std::lround(la * 0.001 * sr);
            CHECK(pk - 100 == e.latency() && e.latency() == expect && std::fabs(L[(size_t)pk] - 0.5f) < 1e-6f && std::fabs(R[(size_t)pk] + 0.5f) < 1e-6f,
                  "lookahead %.0f ms: impulse at +%d, reported latency %d (expected %d), value %.6f", la, pk - 100, e.latency(), expect, L[(size_t)pk]);
        }
        {
            // a burst from silence: with 5 ms lookahead and 1 ms attack the overshoot above the static target is gone
            double over[2];
            for (int k = 0; k < 2; ++k)
            {
                Engine e; Settings s = base(); s.knee = 0; s.ratio = 100; s.threshold = -20; s.attack = 1; s.release = 200; s.lookahead = k ? 5.0 : 0.0; make(e, sr, s);
                const int n = (int)(0.5 * sr), on = (int)(0.25 * sr);
                std::vector<float> L((size_t)n, 0.0f), R;
                for (int i = on; i < n; ++i) L[(size_t)i] = (float)(0.9 * std::sin(kTwoPi * 2000.0 * (i - on) / sr));
                R = L;
                run(e, L, R);
                float pk = 0.0f;
                for (int i = on; i < n; ++i) pk = std::max(pk, std::fabs(L[(size_t)i]));
                over[k] = toDb(pk) - (-20.0);
            }
            CHECK(over[1] < 1.0 && over[1] < over[0] - 3.0, "INF:1 at -20 dB, 1 ms attack, burst from silence: peak overshoot %.2f dB without lookahead, %.2f dB with 5 ms", over[0], over[1]);
        }
        // 4) parallel mix 50% with no compression nulls against the delayed input (lookahead 3 ms); bypass-ready
        for (int os = 0; os <= 2; ++os)
        {
            Engine e; Settings s = base(); s.threshold = 0; s.knee = 0; s.mix = 50; s.lookahead = 3; s.oversampling = os; make(e, sr, s);
            const int n = (int)(0.5 * sr);
            std::mt19937 rng(5);
            std::normal_distribution<float> nd(0.0f, 0.05f);
            std::vector<float> L((size_t)n), R((size_t)n);
            // band-limited test signal (below the oversampler's passband edge) so the 2x/4x round trip is transparent
            for (int i = 0; i < n; ++i) { L[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 997.0 * i / sr) + 0.2 * std::sin(kTwoPi * 8123.0 * i / sr) + 0.1 * std::sin(kTwoPi * 63.0 * i / sr)); R[(size_t)i] = -L[(size_t)i] * 0.7f; }
            std::vector<float> inL = L, inR = R;
            if (os == 0) for (int i = 0; i < n; ++i) { const float x = nd(rng); L[(size_t)i] += x; R[(size_t)i] += x * 0.5f; inL[(size_t)i] = L[(size_t)i]; inR[(size_t)i] = R[(size_t)i]; }
            run(e, L, R);
            const int lat = e.latency();
            double err = 0.0, ref = 0.0;
            for (int i = n / 4; i < n; ++i)
            {
                const double d = L[(size_t)i] - inL[(size_t)(i - lat)], d2 = R[(size_t)i] - inR[(size_t)(i - lat)];
                err += d * d + d2 * d2; ref += (double)inL[(size_t)(i - lat)] * inL[(size_t)(i - lat)] + (double)inR[(size_t)(i - lat)] * inR[(size_t)(i - lat)];
            }
            const double nul = 10.0 * std::log10(err / ref + 1e-30);
            CHECK(nul < (os == 0 ? -140.0 : -90.0), "%dx, mix 50%%, lookahead 3 ms: latency %d, null vs delayed input %.1f dB", 1 << os, lat, nul);
        }
        // 5) oversampling: impulse lands at the reported latency at 2x/4x (+ lookahead)
        for (int os = 1; os <= 2; ++os)
            for (double la : { 0.0, 1.0 })
            {
                Engine e; Settings s = base(); s.threshold = 0; s.oversampling = os; s.lookahead = la; make(e, sr, s);
                const int n = (int)(0.1 * sr);
                std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
                L[200] = 0.5f; R[200] = 0.5f;
                run(e, L, R);
                int pk = 0; for (int i = 0; i < n; ++i) if (std::fabs(L[(size_t)i]) > std::fabs(L[(size_t)pk])) pk = i;
                // symmetric around the peak (linear phase)
                double asym = 0.0;
                for (int k = 1; k < 40; ++k) asym = std::max(asym, (double)std::fabs(L[(size_t)(pk + k)] - L[(size_t)(pk - k)]));
                CHECK(pk - 200 == e.latency() && asym < 1e-5, "%dx + lookahead %.0f ms: impulse peak at +%d, reported latency %d, asymmetry %.1e", 1 << os, la, pk - 200, e.latency(), asym);
            }
        // 6) mid/side: a mono signal only touches mid (side stays exactly zero); "side only" leaves mono untouched
        {
            Engine e; Settings s = base(); s.channelMode = 1; s.link = 0; make(e, sr, s);
            const int n = (int)sr;
            std::vector<float> L = sine(sr, 500, dbToGain(-6), n), R = L, in = L;
            run(e, L, R);
            double side = 0.0; for (int i = 0; i < n; ++i) side = std::max(side, (double)std::fabs(L[(size_t)i] - R[(size_t)i]));
            const double g = rmsDb(L, n / 2, n) - rmsDb(in, n / 2, n);
            CHECK(side == 0.0 && g < -10.0, "Mid/Side, mono input: side peak %.2g (exactly 0), mid compressed by %.2f dB", side, g);
            Engine e2; s.channelMode = 3; make(e2, sr, s);
            std::vector<float> L2 = in, R2 = in;
            run(e2, L2, R2);
            double d = 0.0; for (int i = 0; i < n; ++i) d = std::max(d, (double)std::fabs(L2[(size_t)i] - in[(size_t)i]) + std::fabs(R2[(size_t)i] - in[(size_t)i]));
            CHECK(d < 1e-6, "Side only, mono input: max deviation %.2g (untouched)", d);
            // side-only compresses a side signal
            Engine e3; make(e3, sr, s);
            std::vector<float> L3 = in, R3 = in; for (auto& v : R3) v = -v;
            run(e3, L3, R3);
            const double g3 = rmsDb(L3, n / 2, n) - rmsDb(in, n / 2, n);
            CHECK(g3 < -10.0, "Side only, side signal: compressed by %.2f dB", g3);
        }
        // 7) external sidechain: a loud key triggers reduction while the main input is quiet; silence/null key does nothing
        {
            Engine e; Settings s = base(); s.scExternal = true; s.knee = 0; s.release = 30; make(e, sr, s);
            const int n = (int)sr;
            std::vector<float> L = sine(sr, 300, dbToGain(-50), n), R = L, in = L;
            std::vector<float> key = sine(sr, 60, dbToGain(-6), n);
            run(e, L, R, key.data(), key.data());
            const double g = rmsDb(L, n / 2, n) - rmsDb(in, n / 2, n);
            CHECK(std::fabs(g + 18.0) < 0.3, "external key at -6 dBFS (60 Hz), main at -50 dBFS: main reduced by %.2f dB (curve: -18.00)", g);
            Engine e2; make(e2, sr, s);
            std::vector<float> L2 = in, R2 = in, zero((size_t)n, 0.0f);
            run(e2, L2, R2, zero.data(), zero.data());
            double d = 0.0; for (int i = 0; i < n; ++i) d = std::max(d, (double)std::fabs(L2[(size_t)i] - in[(size_t)i]));
            Engine e3; make(e3, sr, s);
            std::vector<float> L3 = in, R3 = in;
            run(e3, L3, R3, nullptr, nullptr);
            double d3 = 0.0; for (int i = 0; i < n; ++i) d3 = std::max(d3, (double)std::fabs(L3[(size_t)i] - in[(size_t)i]));
            CHECK(d < 1e-7 && d3 < 1e-7, "external key silent / missing (host passes silence or no bus): main untouched (%.1g, %.1g)", d, d3);
            // key filters: a 60 Hz key with the SC high-pass at 400 Hz barely triggers
            Engine e4; s.scHpf = 400; make(e4, sr, s);
            std::vector<float> L4 = in, R4 = in;
            run(e4, L4, R4, key.data(), key.data());
            const double g4 = rmsDb(L4, n / 2, n) - rmsDb(in, n / 2, n);
            CHECK(g4 > -2.0, "SC high-pass 400 Hz on a 60 Hz key: reduction %.2f dB (was %.2f)", g4, g);
        }
        // 8) silence: no NaN, no denormal slowdown, output exactly zero after a loud passage
        {
            Engine e; Settings s = base(); s.style = 2; s.oversampling = 2; s.autoRelease = true; make(e, sr, s);
            const int n = (int)sr;
            std::vector<float> L = sine(sr, 100, 0.9, n), R = L;
            run(e, L, R);
            const int ns = (int)(sr * 20);
            std::vector<float> Z((size_t)ns, 0.0f), Z2((size_t)ns, 0.0f);
            const auto t0 = std::chrono::steady_clock::now();
            run(e, Z, Z2);
            const double tSil = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            bool finite = true; float last = 0.0f;
            for (int i = 0; i < ns; ++i) { if (!std::isfinite(Z[(size_t)i])) finite = false; }
            last = std::max(std::fabs(Z[(size_t)(ns - 1)]), std::fabs(Z2[(size_t)(ns - 1)]));
            std::vector<float> N1((size_t)ns), N2((size_t)ns);
            std::mt19937 rng(1); std::uniform_real_distribution<float> u(-0.5f, 0.5f);
            for (int i = 0; i < ns; ++i) { N1[(size_t)i] = u(rng); N2[(size_t)i] = u(rng); }
            const auto t1 = std::chrono::steady_clock::now();
            run(e, N1, N2);
            const double tSig = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            CHECK(finite && last == 0.0f && tSil < tSig * 1.5, "20 s of silence after a loud passage (Opto, 4x): finite, ends at %.1g, %.2fx real time vs %.2fx with noise (no denormal stall)",
                  last, 20.0 / tSil, 20.0 / tSig);
        }
        // 9) every style: compresses a loud tone toward its own static curve, stays finite with extreme settings
        for (int st = 0; st < 6; ++st)
        {
            Engine e; Settings s = base(); s.style = st; s.release = 80; s.attack = 5; make(e, sr, s);
            const int n = (int)(1.5 * sr);
            std::vector<float> L = sine(sr, 220, dbToGain(-10), n), R = L, in = L;
            run(e, L, R);
            const double g = rmsDb(L, n * 2 / 3, n) - rmsDb(in, n * 2 / 3, n);
            Curve c; c.set(st, s.threshold, s.ratio, s.knee, s.range);
            // extreme: INF ratio, fastest attack, hold, auto release, 4x, M/S, noise bursts
            Engine x; Settings s2 = s; s2.ratio = 100; s2.attack = 0.005; s2.release = 5; s2.hold = 20; s2.autoRelease = true; s2.oversampling = 2; s2.channelMode = 1; s2.knee = 0; make(x, sr, s2);
            std::vector<float> A((size_t)n), B((size_t)n);
            std::mt19937 rng(st + 11); std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            for (int i = 0; i < n; ++i) { const float env = ((i / 4000) % 3 == 0) ? 1.0f : 0.05f; A[(size_t)i] = u(rng) * env; B[(size_t)i] = u(rng) * env; }
            run(x, A, B);
            bool ok = true; float pk = 0.0f;
            for (int i = 0; i < n; ++i) { if (!std::isfinite(A[(size_t)i]) || !std::isfinite(B[(size_t)i])) ok = false; pk = std::max(pk, std::max(std::fabs(A[(size_t)i]), std::fabs(B[(size_t)i]))); }
            // steady-state ripple of the gain on a steady tone (distortion indicator)
            Engine y; make(y, sr, s);
            std::vector<float> P = sine(sr, 220, dbToGain(-10), n), Q = P;
            double gmin = 0, gmax = -100;
            for (int off = 0; off < n; off += 32)
            {
                y.process(P.data() + off, Q.data() + off, nullptr, nullptr, P.data() + off, Q.data() + off, std::min(32, n - off));
                if (off > n * 2 / 3) { gmin = std::min(gmin, y.grDb(0)); gmax = std::max(gmax, y.grDb(0)); }
            }
            CHECK(ok && pk < 2.5f && std::fabs(g - c.gain(-10.0)) < 1.5, "%-7s -10 dBFS tone: %.2f dB (its curve %.2f), gain ripple %.3f dB; INF/0.005 ms/4x/M/S stress: finite, peak %.2f",
                  kStyleNames[st], g, c.gain(-10.0), gmax - gmin, pk);
        }
        // 10) smoothness: threshold and ratio swept every block do not click
        {
            Engine e; Settings s = base(); s.attack = 2; s.release = 50; make(e, sr, s);
            const int n = (int)sr;
            std::vector<float> L = sine(sr, 200, 0.5, n), R = L;
            double maxStep = 0.0; float prev = 0.0f;
            for (int off = 0; off < n; off += 64)
            {
                s.threshold = -40.0 + 35.0 * (0.5 + 0.5 * std::sin(off * 40.0 / sr));
                s.ratio = (off / 64) % 2 ? 20.0 : 1.5;   // jumps every block
                e.setSettings(s);
                const int m = std::min(64, n - off);
                e.process(L.data() + off, R.data() + off, nullptr, nullptr, L.data() + off, R.data() + off, m);
                for (int i = off; i < off + m; ++i) { if (i > 0) maxStep = std::max(maxStep, (double)std::fabs(L[(size_t)i] - prev)); prev = L[(size_t)i]; }
            }
            // a 200 Hz sine at 0.5 moves at most 2*pi*200/sr*0.5 per sample
            const double bound = kTwoPi * 200.0 / sr * 0.5 * 1.15;
            CHECK(maxStep < bound, "threshold sweep + ratio jumping 1.5 <-> 20 every 64 samples: largest step %.4f (signal's own max %.4f)", maxStep, kTwoPi * 200.0 / sr * 0.5);
        }
        // 11) switching oversampling / lookahead while running: declicked (no jump bigger than the signal's own) and the latency follows
        {
            Engine e; Settings s = base(); s.threshold = -20; make(e, sr, s);
            const int n = (int)sr;
            std::vector<float> L = sine(sr, 150, 0.5, n), R = L;
            double maxStep = 0.0; float prev = 0.0f;
            int lat2 = -1;
            for (int off = 0; off < n; off += 128)
            {
                if (off == (int)(0.3 * sr) / 128 * 128) { s.oversampling = 2; s.lookahead = 4; e.setSettings(s); }
                const int m = std::min(128, n - off);
                e.process(L.data() + off, R.data() + off, nullptr, nullptr, L.data() + off, R.data() + off, m);
                for (int i = off; i < off + m; ++i) { if (i > 0) maxStep = std::max(maxStep, (double)std::fabs(L[(size_t)i] - prev)); prev = L[(size_t)i]; }
            }
            lat2 = e.latency();
            Oversampler o; o.prepare(2, 64);
            const int expect = o.latency() + (int)std::lround(0.004 * sr);
            CHECK(maxStep < 0.06 && lat2 == expect, "switch to 4x + 4 ms lookahead mid-stream: largest step %.4f, latency now %d (expected %d)", maxStep, lat2, expect);
        }
    }
    // 12) the analytic envelope detector: ripple on steady tones (20 Hz .. 18 kHz) at the oversampled rates too
    {
        double worst = 0.0, wf = 0.0, wfs = 0.0;
        for (double fs : { 44100.0, 96000.0, 192000.0, 384000.0 })
            for (double f : { 20.0, 40.0, 100.0, 1000.0, 5000.0, 12000.0, 18000.0 })
            {
                HilbertEnvelope h; h.setup(fs);
                const int n = (int)(fs * 0.5);
                double lo = 1e9, hi = -1e9;
                for (int i = 0; i < n; ++i)
                {
                    const double e = h.process(0.5 * std::sin(kTwoPi * f * i / fs));
                    if (i > n / 2) { lo = std::min(lo, e); hi = std::max(hi, e); }
                }
                const double r = 20.0 * std::log10(hi / lo);
                if (r > worst) { worst = r; wf = f; wfs = fs; }
            }
        CHECK(worst < 0.1, "analytic-envelope detector: worst ripple %.4f dB (%.0f Hz at %.0f Hz sample rate)", worst, wf, wfs);
    }
    extraTests();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
