// pump VERB: RT60 from the impulse response (Schroeder-integrated energy in the 1 kHz octave band, T20 fit) within
// +-25 % of DECAY for Hall / Room / Chamber / Plate (and the other reverberant modes within +-35 %); BASS MULT 2x
// makes the low band ring longer than the high band and HIGH DAMP shortens the high band, both as designed; with the
// early reflections off the first output arrives at PRE-DELAY (+-1 ms, also synced to a tempo); FREEZE holds the
// tail's energy within 1 dB over 2 s; the L / R tail correlation is low at WIDTH 100 % (and 1 at width 0); maximum
// DECAY / SIZE / MOD / AGE stays bounded for 30 s in every mode; Reverse builds up then cuts; Gated drops > 40 dB
// shortly after its gate time; Shimmer's tail holds energy an octave up; mix 0 is the input, exactly; every mode is
// finite and level-matched; random-settings stress (block sizes, mode switches, freeze, absurd input) stays finite
// and bounded. 44.1, 48 and 96 kHz where it matters. Also prints CPU per mode.
#include "TestMix.hpp"
#include "../plugins/verb/VerbEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::verb;
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
// a neutral starting point: wet only, flat damping, no pre-delay
static Settings base(int mode, double decay = 2.0)
{
    Settings s; s.mode = mode; s.decay = decay; s.mix = 100; s.predelayMs = 0; s.bassMult = 1.0; s.highDamp = 1.0; s.duck = 0;
    return s;
}
static void impulse(const Settings& s, double sr, double seconds, std::vector<float>& L, std::vector<float>& R)
{
    Engine e; e.setSettings(s); e.prepare(sr, 512);
    const int n = (int)(sr * seconds);
    L.assign((size_t)n, 0.0f); R.assign((size_t)n, 0.0f);
    L[0] = R[0] = 1.0f;
    run(e, L, R);
}
// RT60 in an octave band: two 2nd-order band-passes (Q 1.41), Schroeder backward integration, the -5..-25 dB slope
static double rt60(const std::vector<float>& x, double sr, double fc)
{
    fx::Svf a, b; a.set(fc, 1.41, sr); b.set(fc, 1.41, sr);
    std::vector<double> sch(x.size());
    for (size_t i = 0; i < x.size(); ++i) { const double v = b.bandpass(a.bandpass(x[i])); sch[i] = v * v; }
    double acc = 0.0;
    for (size_t i = x.size(); i-- > 0;) { acc += sch[i]; sch[i] = acc; }
    double t5 = -1.0, t25 = -1.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double d = 10.0 * std::log10(sch[i] / sch[0] + 1e-30);
        if (t5 < 0.0 && d <= -5.0) t5 = i / sr;
        if (t25 < 0.0 && d <= -25.0) { t25 = i / sr; break; }
    }
    return t25 > t5 && t5 >= 0.0 ? (t25 - t5) * 3.0 : 0.0;
}
static double windowDb(const std::vector<float>& L, const std::vector<float>& R, double sr, double t0, double t1)
{
    const size_t a = (size_t)(t0 * sr), b = std::min(L.size(), (size_t)(t1 * sr));
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += (double)L[i] * L[i] + (double)R[i] * R[i];
    return 10.0 * std::log10(e / (2.0 * std::max<size_t>(1, b - a)) + 1e-30);
}
static bool finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }
static double peakOf(const std::vector<float>& L, const std::vector<float>& R, size_t from = 0)
{
    double p = 0.0;
    for (size_t i = from; i < L.size(); ++i) p = std::max(p, (double)std::max(std::fabs(L[i]), std::fabs(R[i])));
    return p;
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }
    auto mixPart = [&](int n, std::vector<float>& L, std::vector<float>& R) {
        L.resize((size_t)n); R.resize((size_t)n);
        for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i) % mix.size()]; R[(size_t)i] = mix[(size_t)(2 * i + 1) % mix.size()]; }
    };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        // ---- RT60 at 1 kHz against DECAY (the studio voices within 25 %, the other reverberant ones within 35 %)
        {
            double worstStudio = 0.0, worstOther = 0.0;
            int wsMode = 0, woMode = 0;
            double wsDecay = 0, woDecay = 0, wsRt = 0, woRt = 0;
            for (int m = 0; m < kNumModes; ++m)
            {
                if (isBurst(m)) continue;
                for (double d : { 0.5, 2.0, 6.0 })
                {
                    Settings s = base(m, d); s.balance = 100;   // the late tank alone
                    std::vector<float> L, R;
                    impulse(s, sr, d * 1.2 + 0.6, L, R);
                    const double r = 0.5 * (rt60(L, sr, 1000.0) + rt60(R, sr, 1000.0));
                    const double err = std::fabs(r / d - 1.0);
                    if (m <= kPlate) { if (err > worstStudio) { worstStudio = err; wsMode = m; wsDecay = d; wsRt = r; } }
                    else if (err > worstOther) { worstOther = err; woMode = m; woDecay = d; woRt = r; }
                }
            }
            CHECK(worstStudio < 0.25, "hall / room / chamber / plate, decay 0.5, 2, 6 s: RT60 at 1 kHz within %.0f %% (worst %s %.1f s -> %.2f s)",
                  100 * worstStudio, kModeNames[wsMode], wsDecay, wsRt);
            CHECK(worstOther < 0.35, "the vintage, classic, rack and shimmer voices: within %.0f %% (worst %s %.1f s -> %.2f s)",
                  100 * worstOther, kModeNames[woMode], woDecay, woRt);
        }
        // ---- mix 0: the input, exactly (no latency)
        {
            Settings s; s.mode = kClassicHall; s.mix = 0; s.decay = 8; s.age = 100;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L, R; mixPart((int)sr, L, R);
            const std::vector<float> L0 = L, R0 = R;
            run(e, L, R, 333);
            double err = 0;
            for (size_t i = 0; i < L.size(); ++i) err = std::max(err, (double)std::fabs(L[i] - L0[i]) + std::fabs(R[i] - R0[i]));
            CHECK(e.latency() == 0 && err == 0.0, "mix 0 = the input, exactly (latency %d, max error %.2g)", e.latency(), err);
        }
        // ---- first arrival at the pre-delay with the early reflections off
        {
            double worst = 0.0;
            for (int m = 0; m < kNumModes; ++m)
                for (double pd : { 0.0, 37.0, 250.0 })
                {
                    Settings s = base(m); s.predelayMs = pd; s.balance = 100;
                    std::vector<float> L, R;
                    impulse(s, sr, pd * 0.001 + 0.25, L, R);
                    const double pk = peakOf(L, R);
                    size_t first = 0;
                    while (first < L.size() && std::fabs(L[first]) + std::fabs(R[first]) < 0.01 * pk) ++first;
                    worst = std::max(worst, std::fabs(first * 1000.0 / sr - pd));
                }
            CHECK(worst <= 1.0, "early reflections off: the first output arrives at the pre-delay (0, 37, 250 ms; every mode) within %.2f ms", worst);
            // synced: a 1/8 at 100 BPM = 300 ms
            Settings s = base(kHall); s.sync = true; s.note = kNote8; s.balance = 100;
            Engine e; e.setTempo(100.0); e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(sr * 0.5), 0.0f), R((size_t)(sr * 0.5), 0.0f);
            L[0] = R[0] = 1.0f;
            run(e, L, R);
            const double pk = peakOf(L, R);
            size_t first = 0;
            while (first < L.size() && std::fabs(L[first]) + std::fabs(R[first]) < 0.01 * pk) ++first;
            CHECK(std::fabs(first * 1000.0 / sr - 300.0) <= 1.0, "pre-delay synced to 1/8 at 100 BPM: first output at %.2f ms (300)", first * 1000.0 / sr);
        }
        if (sr != 48000.0) continue;

        // ---- bass multiplier and high damping shape the decay across frequency (as designed)
        {
            auto bands = [&](int m, double bass, double hd, double& lo, double& mid, double& hi) {
                Settings s = base(m, 2.0); s.bassMult = bass; s.highDamp = hd; s.crossover = 500; s.dampFreq = 4000; s.balance = 100;
                std::vector<float> L, R;
                impulse(s, sr, 8.0, L, R);
                lo = 0.5 * (rt60(L, sr, 125.0) + rt60(R, sr, 125.0));
                mid = 0.5 * (rt60(L, sr, 1000.0) + rt60(R, sr, 1000.0));
                hi = 0.5 * (rt60(L, sr, 8000.0) + rt60(R, sr, 8000.0));
            };
            for (int m : { kHall, kPlate, kClassicHall })
            {
                double l1, m1, h1, l2, m2, h2, l3, m3, h3;
                bands(m, 1.0, 1.0, l1, m1, h1);
                bands(m, 2.0, 1.0, l2, m2, h2);
                bands(m, 1.0, 0.3, l3, m3, h3);
                CHECK(l2 > 1.6 * h2 && l2 > 1.6 * l1, "%s bass mult 2x: RT 125 Hz %.2f s vs 8 kHz %.2f s (flat: %.2f s; designed %.2f s)", kModeNames[m], l2, h2, l1,
                      rt60At(125.0, 2.0, 2.0, 500, 1.0, 4000, sr));
                CHECK(h3 < 0.55 * h1 && h3 < 0.55 * m3, "%s high damp 0.3x: RT 8 kHz %.2f s (flat: %.2f s; designed %.2f s), 1 kHz %.2f s", kModeNames[m], h3, h1,
                      rt60At(8000.0, 2.0, 1.0, 500, 0.3, 4000, sr), m3);
            }
        }
        // ---- freeze holds the tail
        {
            double worst = 0.0; int wm = 0;
            for (int m = 0; m < kNumModes; ++m)
            {
                if (isBurst(m)) continue;
                Settings s = base(m, 3.0); s.age = 60;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; mixPart((int)(sr * 4.0), L, R);
                for (size_t i = (size_t)sr; i < L.size(); ++i) L[i] = R[i] = 0.0f;   // 1 s of the mix, then silence
                for (int off = 0, n = (int)L.size(); off < n; off += 256)
                {
                    if (off >= (int)(1.1 * sr) && !s.freeze) { s.freeze = true; e.setSettings(s); }
                    e.process(L.data() + off, R.data() + off, std::min(256, n - off));
                }
                const double a = windowDb(L, R, sr, 1.5, 1.7), b = windowDb(L, R, sr, 3.5, 3.7);
                if (std::fabs(b - a) > worst) { worst = std::fabs(b - a); wm = m; }
            }
            CHECK(worst < 1.0, "freeze: the tail's energy holds within %.2f dB over 2 s in every mode (worst %s)", worst, kModeNames[wm]);
        }
        // ---- stereo: decorrelated at width 100 %, mono at 0
        {
            double worst = 0.0; int wm = 0;
            for (int m = 0; m < kNumModes; ++m)
            {
                Settings s = base(m, 2.0); s.width = 100;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)(sr * 2.0), 0.0f), R((size_t)(sr * 2.0), 0.0f);
                fx::Rng rng(7);
                for (int i = 0; i < (int)(0.3 * sr); ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.3 * rng.bi());   // a mono source
                run(e, L, R);
                const size_t a = (size_t)(0.35 * sr), b = isBurst(m) ? (size_t)(0.6 * sr) : L.size();
                double lr = 0, ll = 0, rr = 0;
                for (size_t i = a; i < b; ++i) { lr += (double)L[i] * R[i]; ll += (double)L[i] * L[i]; rr += (double)R[i] * R[i]; }
                const double c = std::fabs(lr / std::sqrt(ll * rr + 1e-30));
                if (c > worst) { worst = c; wm = m; }
            }
            CHECK(worst < 0.5, "a mono source's tail at width 100 %%: L / R correlation at most %.2f (%s)", worst, kModeNames[wm]);
            Settings s = base(kHall, 2.0); s.width = 0;
            std::vector<float> L, R; impulse(s, sr, 1.0, L, R);
            double d = 0; for (size_t i = 0; i < L.size(); ++i) d = std::max(d, (double)std::fabs(L[i] - R[i]));
            CHECK(d < 1e-6, "width 0: mono (max L - R %.2g)", d);
        }
        // ---- Gated: dense, then cut
        {
            for (double T : { 150.0, 300.0, 700.0 })
            {
                Settings s = base(kGated, 4.0); s.envTimeMs = T;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)(sr * 1.5), 0.0f), R((size_t)(sr * 1.5), 0.0f);
                fx::Rng rng(2);
                for (int i = 0; i < (int)(0.004 * sr); ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.6 * rng.bi());   // a drum hit
                run(e, L, R);
                const double t = T * 0.001;
                const double body = windowDb(L, R, sr, 0.2 * t, 0.9 * t), after = windowDb(L, R, sr, t + 0.010, t + 0.200);
                CHECK(body - after > 40.0, "gated, time %.0f ms: %.1f dB in the gate, %.1f dB from 10 ms after it (drop %.0f dB)", T, body, after, body - after);
            }
        }
        // ---- Reverse: builds up, then cuts
        {
            Settings s = base(kReverse, 0.7); s.envTimeMs = 600;
            std::vector<float> L, R; impulse(s, sr, 1.2, L, R);
            double prev = -300.0; bool rising = true;
            for (int w = 0; w < 5; ++w)
            {
                const double d = windowDb(L, R, sr, 0.01 + w * 0.11, 0.01 + (w + 1) * 0.11);
                if (d < prev + 3.0) rising = false;
                prev = d;
            }
            const double late = windowDb(L, R, sr, 0.48, 0.59), after = windowDb(L, R, sr, 0.61, 0.9), early = windowDb(L, R, sr, 0.01, 0.1);
            CHECK(rising && late > early + 25.0, "reverse: rises in every 110 ms window (%.1f dB at the start, %.1f dB just before 600 ms)", early, late);
            CHECK(late - after > 40.0, "reverse: cuts at its time (%.0f dB down after it)", late - after);
        }
        // ---- Shimmer: the tail climbs an octave
        {
            auto octaveRatio = [&](int mode, int pitch) {
                Settings s = base(mode, 6.0); s.pitch = pitch; s.shimmer = 50;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int n = (int)(sr * 3.0);
                std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
                for (int i = 0; i < (int)(0.5 * sr); ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 440.0 * i / sr));
                run(e, L, R);
                const int N = 32768;
                RealFFT fft; fft.setup(N);
                AlignedBuffer in((size_t)N), out((size_t)N);
                for (int i = 0; i < N; ++i) in[(size_t)i] = L[(size_t)(n - N + i)] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
                fft.forward(in.data(), out.data());
                auto band = [&](double f) {
                    double p = 0;
                    for (int k = (int)(f * 0.94 * N / sr); k <= (int)(f * 1.06 * N / sr); ++k) p += out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
                    return 10.0 * std::log10(p + 1e-30);
                };
                return band(440.0 * std::pow(2.0, pitch / 12.0)) - band(440.0);
            };
            const double up = octaveRatio(kShimmerMode, 12), hall = octaveRatio(kHall, 12), down = octaveRatio(kShimmerMode, -12);
            CHECK(up > -12.0 && up > hall + 25.0, "shimmer +12: 880 Hz in the tail %.1f dB relative to 440 Hz (the plain hall: %.1f dB)", up, hall);
            CHECK(down > -20.0 && down > hall + 15.0, "shimmer -12: 220 Hz in the tail %.1f dB relative to 440 Hz", down);
        }
        // ---- every mode: finite, the wet level near the input's (the test mix), the tail present
        {
            std::vector<float> L0, R0; mixPart((int)(sr * 6.0), L0, R0);
            const double ref = windowDb(L0, R0, sr, 1.0, 6.0);
            bool ok = true; double lo = 1e9, hi = -1e9;
            for (int m = 0; m < kNumModes; ++m)
            {
                Settings s; s.mode = m; s.mix = 100;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L = L0, R = R0;
                run(e, L, R, 512);
                ok = ok && finite(L) && finite(R);
                const double d = windowDb(L, R, sr, 1.0, 6.0) - ref;
                lo = std::min(lo, d); hi = std::max(hi, d);
            }
            CHECK(ok && lo > -9.0 && hi < 6.0, "every mode at its defaults on the test mix: finite, wet level %.1f..%+.1f dB against the input", lo, hi);
        }
        // ---- the vintage colour: age narrows the band (less above 12 kHz) and adds grain
        {
            auto hfShare = [&](int mode, double age) {
                Settings s = base(mode, 2.0); s.age = age;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int n = (int)(sr * 1.5);
                std::vector<float> L((size_t)n), R((size_t)n);
                fx::Rng rng(5);
                for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = i < (int)sr ? (float)(0.2 * rng.bi()) : 0.0f;
                run(e, L, R);
                const int N = 16384;
                RealFFT fft; fft.setup(N);
                AlignedBuffer in((size_t)N), out((size_t)N);
                for (int i = 0; i < N; ++i) in[(size_t)i] = L[(size_t)(16000 + i)] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
                fft.forward(in.data(), out.data());
                double hiE = 0, all = 0;
                for (int k = 4; k < N / 2; ++k)
                {
                    const double p = out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
                    all += p; if (k * sr / N > 12000.0) hiE += p;
                }
                return 10.0 * std::log10(hiE / all + 1e-30);
            };
            const double hall = hfShare(kHall, 0), v0 = hfShare(kVintageHall, 0), v1 = hfShare(kVintageHall, 100), rack = hfShare(kRack, 50);
            CHECK(v0 < hall - 20.0 && v1 < v0 - 3.0, "late-70s band limit: energy above 12 kHz %.0f dB (age 0), %.0f dB (age 100), studio hall %.0f dB", v0, v1, hall);
            CHECK(rack < hall - 10.0, "90s rack band limit: energy above 12 kHz %.0f dB (studio hall %.0f dB)", rack, hall);
        }
        // ---- bounded at maximum decay, size, modulation, age and shimmer for 30 s
        {
            bool ok = true; double worstPk = 0.0; int wm = 0;
            for (int m = 0; m < kNumModes; ++m)
            {
                Settings s = base(m, 30.0); s.size = 100; s.modDepth = 100; s.modRate = 5; s.bassMult = 2; s.dampFreq = 20000; s.diffusion = 100;
                s.shimmer = 100; s.age = 100; s.envTimeMs = 1500;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; mixPart((int)(sr * 30.0), L, R);
                for (size_t i = (size_t)(10 * sr); i < L.size(); ++i) L[i] = R[i] = 0.0f;   // 10 s of the mix, 20 s of tail
                run(e, L, R, 512);
                ok = ok && finite(L) && finite(R);
                const double pk = peakOf(L, R, (size_t)(10 * sr));
                if (pk > worstPk) { worstPk = pk; wm = m; }
            }
            CHECK(ok && worstPk < 2.0, "max decay / size / mod / age / shimmer for 30 s, every mode: finite, tail peak %.2f (%s)", worstPk, kModeNames[wm]);
        }
        // ---- switches don't click: mode changes, size jumps and freeze toggles on a steady tone
        {
            Settings s; s.mode = kHall; s.mix = 50; s.decay = 1.5;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(sr * 4.0);
            std::vector<float> L((size_t)n), R((size_t)n);
            for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 110.0 * i / sr));
            double maxJump = 0;
            for (int off = 0, k = 0; off < n; off += 128, ++k)
            {
                if (k % 37 == 20) { s.mode = (s.mode + 5) % kNumModes; e.setSettings(s); }
                if (k % 53 == 30) { s.size = s.size > 50 ? 5 : 95; e.setSettings(s); }
                if (k % 71 == 40) { s.freeze = !s.freeze; e.setSettings(s); }
                e.process(L.data() + off, R.data() + off, 128);
            }
            for (int i = 4800; i < n; ++i) maxJump = std::max(maxJump, (double)std::fabs(L[(size_t)i] - L[(size_t)(i - 1)]));
            // a 110 Hz sine at 0.3 moves up to 0.0043 per sample; the reverb adds its own motion
            CHECK(maxJump < 0.05, "mode switches, size jumps and freeze toggles on a 110 Hz tone: largest step %.3f", maxJump);
        }
        // ---- the duck: the wet signal dips while the input plays
        {
            auto wetDuring = [&](double duck) {
                Settings s = base(kHall, 2.0); s.duck = duck;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; mixPart((int)(sr * 3.0), L, R);
                run(e, L, R);
                return windowDb(L, R, sr, 1.0, 3.0);
            };
            const double d0 = wetDuring(0), d1 = wetDuring(100);
            CHECK(d1 < d0 - 10.0, "duck 100 %%: the reverb under the playing mix drops %.1f dB", d0 - d1);
        }
        // ---- stress: random settings, block sizes, mode switches, freeze, absurd input
        {
            std::mt19937 rng(11);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool fin = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.mode = (int)(u(rng) * kNumModes); s.predelayMs = u(rng) * 500; s.sync = u(rng) < 0.2; s.note = (int)(u(rng) * (kMaxSyncNote + 1));
                    s.size = u(rng) * 100; s.decay = 0.1 * std::pow(300.0, u(rng)); s.diffusion = u(rng) * 100; s.balance = u(rng) * 200 - 100;
                    s.bassMult = 0.5 * std::pow(4.0, u(rng)); s.crossover = 50 * std::pow(40.0, u(rng)); s.highDamp = 0.1 + 0.9 * u(rng);
                    s.dampFreq = 1000 * std::pow(20.0, u(rng)); s.lowCut = 10 * std::pow(100.0, u(rng)); s.highCut = 1000 * std::pow(22.0, u(rng));
                    s.modDepth = u(rng) * 100; s.modRate = 0.05 * std::pow(100.0, u(rng)); s.width = u(rng) * 150; s.age = u(rng) * 100;
                    s.envTimeMs = 50 * std::pow(30.0, u(rng)); s.pitch = (int)(u(rng) * 49) - 24; s.shimmer = u(rng) * 100; s.duck = u(rng) * 100;
                    s.freeze = u(rng) < 0.15; s.mix = u(rng) * 100; s.outDb = 0;
                    e.setSettings(s);
                    e.setTempo(60 + 120 * u(rng));
                }
                if (it % 13 == 5) { Settings t = s; t.mode = (s.mode + 3) % kNumModes; e.setSettings(t); e.setSettings(s); }
                const int bs = 1 + (int)(u(rng) * 1200);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 1500 && i == 0) L[0] = 1000.0f;   // absurd (but finite) input: must recover
                }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) fin = false;
                    if (it > 1800) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(fin && peak < 8.0, "random settings, sizes, switches and freezes: finite, peak %.2f after a +60 dB spike", peak);
        }
        // ---- CPU per mode (the test mix, 512-sample blocks)
        {
            const int n = (int)(sr * 4.0);
            std::vector<float> L0, R0; mixPart(n, L0, R0);
            for (int m = 0; m < kNumModes; ++m)
            {
                Settings s; s.mode = m;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L = L0, R = R0;
                const auto t0 = std::chrono::steady_clock::now();
                run(e, L, R, 512);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("  cpu: %-14s %.1f %% of one core\n", kModeNames[m], 100.0 * secs / 4.0);
            }
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
