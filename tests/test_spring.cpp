// pump SPRING: the tank disperses (in its impulse response the 2-3.4 kHz band arrives later than the 300-600 Hz band,
// by a spring-like 1-15 ms, and by more when the springs are looser); the broadband decay follows DECAY within
// +-35 % in every tank model; the digital models alias (a sine comes out with partners that are not its harmonics)
// and lose bandwidth as GRIT rises; KICK makes the tank ring with no input; the longest decay stays bounded on loud
// input; mix 0 is the input exactly (no latency); model switches and control sweeps don't click; random-settings
// stress (block sizes, model changes, kicks, absurd input) stays finite and bounded. 44.1, 48 and 96 kHz where it
// matters. Also prints CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/spring/SpringEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::spring;
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
// the wet signal alone, flat: mix 100 %, no low cut, tone open, no wobble
static Settings wet(int model, double decay = 2.5, double tension = 0.0)
{
    Settings s; s.model = model; s.decay = decay; s.tension = tension; s.mix = 100; s.lowCut = kLowCutOff; s.tone = 0; s.wobble = 0;
    s.width = 100; s.preDelay = 0; s.dwell = 50; s.drip = 50; s.grit = 35; s.outDb = 0;
    return s;
}
// the mono impulse response (a 1-sample impulse at a level the driver keeps linear)
static std::vector<float> impulse(const Settings& s, double sr, double seconds)
{
    Engine e; e.setSettings(s); e.prepare(sr, 512);
    const int n = (int)(sr * seconds);
    std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
    L[0] = R[0] = 0.05f;
    run(e, L, R);
    for (int i = 0; i < n; ++i) L[(size_t)i] = 0.5f * (L[(size_t)i] + R[(size_t)i]);
    return L;
}
// the part of x between lo and hi (brick-wall in the frequency domain, zero-padded so nothing wraps)
static std::vector<float> band(const std::vector<float>& x, double sr, double lo, double hi)
{
    int N = 32;
    while (N < 2 * (int)x.size()) N <<= 1;
    RealFFT fft; fft.setup(N);
    AlignedBuffer in((size_t)N), sp((size_t)N), out((size_t)N);
    for (size_t i = 0; i < x.size(); ++i) in[i] = x[i];
    fft.forward(in.data(), sp.data());
    sp[0] = 0.0f; sp[1] = 0.0f;   // DC and Nyquist
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * sr / N;
        if (f < lo || f > hi) { sp[(size_t)(2 * k)] = 0.0f; sp[(size_t)(2 * k + 1)] = 0.0f; }
    }
    fft.inverse(sp.data(), out.data());
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = out[i] / (float)N;
    return y;
}
// when a band's energy arrives: the moment half of its energy in the first `window` seconds has come out (the first
// echoes, before the third trip of the shortest spring)
static double arrival(const std::vector<float>& x, double sr, double window)
{
    const int n = std::min((int)x.size(), (int)(window * sr));
    double tot = 0.0;
    for (int i = 0; i < n; ++i) tot += (double)x[(size_t)i] * x[(size_t)i];
    double acc = 0.0;
    for (int i = 0; i < n; ++i) { acc += (double)x[(size_t)i] * x[(size_t)i]; if (acc >= 0.5 * tot) return i / sr; }
    return 0.0;
}
// T60 from the energy decay curve (Schroeder): the slope between -5 and -25 dB
static double t60(const std::vector<float>& x, double sr)
{
    std::vector<double> edc(x.size() + 1, 0.0);
    for (size_t i = x.size(); i-- > 0;) edc[i] = edc[i + 1] + (double)x[i] * x[i];
    auto when = [&](double db) {
        const double lim = edc[0] * std::pow(10.0, db / 10.0);
        for (size_t i = 0; i < x.size(); ++i) if (edc[i] < lim) return i / sr;
        return x.size() / sr;
    };
    return 3.0 * (when(-25.0) - when(-5.0));
}
static double rmsDb(const std::vector<float>& L, const std::vector<float>& R, size_t from, size_t to = 0)
{
    if (to == 0 || to > L.size()) to = L.size();
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)L[i] * L[i] + (double)R[i] * R[i];
    return 10 * std::log10(e / (2.0 * (to - from)) + 1e-30);
}
// spectrum of the last N samples of x (Hann), power per bin
static std::vector<double> spectrum(const std::vector<float>& x, int N)
{
    RealFFT fft; fft.setup(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    const size_t start = x.size() - (size_t)N;
    for (int i = 0; i < N; ++i) in[(size_t)i] = (float)(x[start + (size_t)i] * (0.5 - 0.5 * std::cos(kTwoPi * i / N)));
    fft.forward(in.data(), out.data());
    std::vector<double> p((size_t)(N / 2), 0.0);
    for (int k = 1; k < N / 2; ++k) p[(size_t)k] = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
    return p;
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        // ---- dispersion: the high band of the first echo arrives after the low band, more so when looser
        {
            double worstLo = 1e9, worstHi = -1e9;
            bool order = true;
            for (int m = 0; m < 4; ++m)
            {
                double dt[3];
                const double ten[3] = { -100.0, 0.0, 100.0 };
                for (int k = 0; k < 3; ++k)
                {
                    const std::vector<float> ir = impulse(wet(m, 2.5, ten[k]), sr, 0.25);
                    const Design d = design(m, ten[k], 50.0, sr);
                    const double win = 2.8 * d.travel[0];   // the first echoes, before the shortest spring's second
                    dt[k] = arrival(band(ir, sr, 2000.0, 3400.0), sr, win) - arrival(band(ir, sr, 300.0, 600.0), sr, win);
                    worstLo = std::min(worstLo, dt[k]); worstHi = std::max(worstHi, dt[k]);
                }
                order = order && dt[0] > dt[1] + 0.5e-3 && dt[1] > dt[2] + 0.2e-3;
                if (sr == 48000.0)
                    printf("  %s: 2-3.4 kHz arrives after 300-600 Hz by %.1f ms (loose) / %.1f ms / %.1f ms (tight)\n", kModelNames[m], 1e3 * dt[0], 1e3 * dt[1], 1e3 * dt[2]);
            }
            CHECK(worstLo > 1.0e-3 && worstHi < 15e-3, "dispersion: the high band arrives %.1f..%.1f ms after the low band (spring-like: 1..15 ms)", 1e3 * worstLo, 1e3 * worstHi);
            CHECK(order, "dispersion grows as TENSION loosens, in every tank model");
        }
        // ---- the broadband decay follows DECAY
        {
            double worst = 0.0;
            for (int m = 0; m < 4; ++m)
                for (double dec : { 0.3, 1.0, 2.5, 8.0 })
                {
                    const std::vector<float> ir = impulse(wet(m, dec), sr, std::max(1.5, 1.6 * dec));
                    const double t = t60(ir, sr);
                    worst = std::max(worst, std::fabs(t / dec - 1.0));
                    if (sr == 48000.0 && m == 0) printf("  two-spring: DECAY %.1f s -> T60 %.2f s\n", dec, t);
                }
            CHECK(worst < 0.35, "decay: the measured T60 is within %.0f %% of DECAY (4 tanks x 0.3, 1, 2.5, 8 s)", 100.0 * worst);
        }
        // ---- digital models: aliasing, and bandwidth that shrinks with GRIT
        {
            // a 7 kHz sine (bin-exact) through the wet path: what isn't 7 kHz or its harmonics is aliasing (images
            // around the converter clock, the hold's mirror images)
            const int N = 16384;
            const double f0 = std::round(7000.0 * N / sr) * sr / N;
            for (int m : { kDigital, kLofi })
            {
                double alias[2], top[2];
                const double grit[2] = { 0.0, 100.0 };
                for (int g = 0; g < 2; ++g)
                {
                    Settings s = wet(m, 1.0); s.grit = grit[g]; s.drip = 0;
                    Engine e; e.setSettings(s); e.prepare(sr, 512);
                    const int n = (int)(sr * 1.0);
                    std::vector<float> L((size_t)n), R((size_t)n);
                    for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.25 * std::sin(kTwoPi * f0 * i / sr));
                    run(e, L, R);
                    const std::vector<double> p = spectrum(L, N);
                    const int k0 = (int)std::lround(f0 * N / sr);
                    double tot = 0.0, other = 0.0, hiE = 0.0;
                    for (int k = 4; k < N / 2; ++k)
                    {
                        tot += p[(size_t)k];
                        const int h = (int)std::lround((double)k / k0);
                        if (!(h >= 1 && std::abs(k - h * k0) <= 3)) other += p[(size_t)k];
                    }
                    // bandwidth: white noise through the wet path, the energy above 8 kHz
                    Engine e2; e2.setSettings(s); e2.prepare(sr, 512);
                    fx::Rng rng(7);
                    for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = 0.1f * rng.bif();
                    run(e2, L, R);
                    const std::vector<double> q = spectrum(L, N);
                    double all = 0.0;
                    for (int k = 4; k < N / 2; ++k) { all += q[(size_t)k]; if (k * sr / N > 8000.0) hiE += q[(size_t)k]; }
                    alias[g] = 10.0 * std::log10(other / (tot + 1e-30) + 1e-30);
                    top[g] = 10.0 * std::log10(hiE / (all + 1e-30) + 1e-30);
                }
                CHECK(alias[0] > -45.0 && alias[1] > -6.0 && alias[1] > alias[0] - 0.5, "%s: a 7 kHz sine comes out with non-harmonic partners (%.1f dB of the output at GRIT 0, %.1f dB at 100)", kModelNames[m], alias[0], alias[1]);
                CHECK(top[1] < top[0] - 10.0, "%s: energy above 8 kHz falls from %.1f dB (GRIT 0) to %.1f dB (GRIT 100)", kModelNames[m], top[0], top[1]);
            }
            // a tank does not alias: a steady sine comes out clean (the driver's harmonics only; 3 s, so the onset's
            // own reverb has died away)
            Settings s = wet(kTwoSpring, 1.0); s.dwell = 0;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(sr * 3.0);
            std::vector<float> L((size_t)n), R((size_t)n);
            const double f1 = std::round(1000.0 * N / sr) * sr / N;
            for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.05 * std::sin(kTwoPi * f1 * i / sr));
            run(e, L, R);
            const std::vector<double> p = spectrum(L, N);
            const int k0 = (int)std::lround(f1 * N / sr);
            double tot = 0.0, other = 0.0;
            for (int k = 4; k < N / 2; ++k)
            {
                tot += p[(size_t)k];
                const int h = (int)std::lround((double)k / k0);
                if (!(h >= 1 && std::abs(k - h * k0) <= 6)) other += p[(size_t)k];
            }
            CHECK(10.0 * std::log10(other / tot + 1e-30) < -50.0, "two-spring tank: a 1 kHz sine stays clean (%.1f dB of non-harmonic content)", 10.0 * std::log10(other / tot + 1e-30));
        }
        // ---- KICK: the tank rings with no input, in every model
        {
            double weakest = 1e9;
            for (int m = 0; m < kNumModels; ++m)
            {
                Settings s = wet(m, 2.0);
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int n = (int)(sr * 1.0);
                std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
                run(e, L, R);   // silence first: nothing comes out
                double quiet = 0.0;
                for (int i = 0; i < n; ++i) quiet = std::max(quiet, (double)std::fabs(L[(size_t)i]));
                std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f);
                e.kick(1.0);
                run(e, L, R);
                const double after = rmsDb(L, R, (size_t)(0.3 * sr), (size_t)(0.6 * sr));
                weakest = std::min(weakest, rmsDb(L, R, 0, (size_t)(0.3 * sr)));
                if (quiet > 0.0 || after < -75.0)
                {
                    CHECK(false, "%s: kick with no input (silence before: peak %.3g, 0.3-0.6 s after the kick: %.1f dBFS)", kModelNames[m], quiet, after);
                    weakest = -200.0;
                }
            }
            CHECK(weakest > -40.0, "kick with zero input: every model rings (the quietest first 300 ms: %.1f dBFS RMS); silence stays silent", weakest);
        }
        // ---- mix 0: the input exactly, no latency
        {
            Settings s = wet(kThreeSpring, 4.0); s.mix = 0; s.dwell = 100; s.wobble = 60;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(sr * 2.0);
            std::vector<float> L((size_t)n), R((size_t)n);
            for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            const std::vector<float> L0 = L, R0 = R;
            run(e, L, R, 333);
            double err = 0;
            for (int i = 0; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]) + std::fabs(R[(size_t)i] - R0[(size_t)i]));
            CHECK(e.latency() == 0 && err < 1e-6, "mix 0 = the input, latency %d (max error %.2g)", e.latency(), err);
        }
        // ---- the longest decay on loud input stays bounded (every model, 20 s)
        {
            double worst = 0.0, tailWorst = -200.0;
            bool finite = true;
            for (int m = 0; m < kNumModels; ++m)
            {
                Settings s = wet(m, 8.0); s.dwell = 100; s.drip = 100; s.tension = -100; s.wobble = 100;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int n = (int)(sr * 20.0);
                std::vector<float> L((size_t)n), R((size_t)n);
                const size_t mn = mix.size() / 2;
                for (int i = 0; i < n; ++i) { L[(size_t)i] = 2.0f * mix[(2 * (size_t)i) % (2 * mn)]; R[(size_t)i] = 2.0f * mix[(2 * (size_t)i + 1) % (2 * mn)]; }
                for (int i = (int)(16.0 * sr); i < n; ++i) L[(size_t)i] = R[(size_t)i] = 0.0f;   // then 4 s of silence
                int pos = 0, k = 0;
                while (pos < n)
                {
                    const int bs = std::min(512, n - pos);
                    if (pos >= (int)(k * 2.0 * sr) && k < 8) { e.kick(1.5); ++k; }
                    e.process(L.data() + pos, R.data() + pos, bs);
                    pos += bs;
                }
                double pk = 0.0;
                for (int i = 0; i < n; ++i) { if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false; pk = std::max(pk, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i]))); }
                worst = std::max(worst, pk);
                tailWorst = std::max(tailWorst, rmsDb(L, R, (size_t)(19.5 * sr)));
            }
            CHECK(finite && worst < 4.0, "DECAY 8 s, dwell/drip/wobble 100 %%, +6 dB input and kicks for 16 s: finite, peak %.2f", worst);
            CHECK(tailWorst < -40.0, "...and 3.5 s after the input stops it has died down (%.1f dBFS)", tailWorst);
        }
        if (sr != 48000.0) continue;
        // ---- model switches and control sweeps on a steady tone don't click
        {
            Settings s = wet(kTwoSpring, 1.5); s.mix = 40;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(sr * 6.0);
            std::vector<float> L((size_t)n), R((size_t)n);
            for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 110.0 * i / sr));
            const std::vector<float> L0 = L;
            double maxJump = 0;
            for (int off = 0, k = 0; off < n; off += 128, ++k)
            {
                if (k % 37 == 20) { s.model = (s.model + 1) % kNumModels; e.setSettings(s); }
                if (k % 23 == 10) { s.tension = s.tension > 0 ? -80 : 80; s.decay = s.decay > 2 ? 0.5 : 6; s.tone = -s.tone + 30; s.preDelay = s.preDelay > 100 ? 0 : 200; s.grit = 100 - s.grit; e.setSettings(s); }
                e.process(L.data() + off, R.data() + off, std::min(128, n - off));
            }
            // the output minus the dry part (60 %): what the wet path adds, sample to sample
            for (int i = 4800; i < n; ++i) maxJump = std::max(maxJump, (double)std::fabs((L[(size_t)i] - L0[(size_t)i]) - (L[(size_t)i - 1] - L0[(size_t)i - 1])));
            CHECK(maxJump < 0.08, "model switches every 0.1 s and control jumps on a 110 Hz tone: largest step in the wet signal %.3f", maxJump);
        }
        // ---- stress: random settings, block sizes, model changes, kicks, absurd input
        {
            std::mt19937 rng(11);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.model = (int)(u(rng) * kNumModels); s.dwell = u(rng) * 100; s.decay = 0.3 * std::pow(8.0 / 0.3, u(rng));
                    s.tension = u(rng) * 200 - 100; s.drip = u(rng) * 100; s.tone = u(rng) * 200 - 100; s.lowCut = 20 * std::pow(50.0, u(rng));
                    s.preDelay = u(rng) * 250; s.wobble = u(rng) * 100; s.width = u(rng) * 100; s.grit = u(rng) * 100; s.mix = u(rng) * 100; s.outDb = 0;
                    e.setSettings(s);
                }
                if (it % 11 == 3) e.kick(0.2 + 1.3 * u(rng));
                const int bs = 1 + (int)(u(rng) * 1200);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 1500 && i == 0) L[0] = 1000.0f;   // absurd (but finite) input: must recover
                }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                    if (it > 1700) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 8.0, "random settings, sizes, models and kicks: finite, peak %.2f after recovering from a +60 dB spike", peak);
        }
        // ---- CPU (the test mix through each model, 512-sample blocks)
        {
            const int n = (int)(sr * 4.0);
            for (int m = 0; m < kNumModels; ++m)
            {
                Settings s; s.model = m;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R((size_t)n);
                for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
                const DenormalGuard dg;
                const auto t0 = std::chrono::steady_clock::now();
                run(e, L, R, 512);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("  cpu: %s: %.1f %% of one core\n", kModelNames[m], 100.0 * secs / 4.0);
            }
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
