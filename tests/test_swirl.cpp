// pump SWIRL: mix 0 is the input delayed by the latency (0, or 3 ms in through-zero) for every type and variant;
// a 4-stage phaser at rest notches where four all-pass stages reach -180 degrees; a flanger at rest has its first
// comb notch at 1 / (2 x delay); the negative flanger notches at the bottom instead; through-zero with the swept
// copy on the fixed one is flat; a vibrato moves the pitch; a synced LFO follows the song position; random settings
// (type / variant switches mid-fade, high feedback, block sizes, an absurd input) stay finite and bounded.
// 44.1, 48 and 96 kHz. Also prints CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/swirl/SwirlEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::swirl;
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
static Settings still(int mode, int var)
{
    Settings s; s.mode = mode; s.variant = var; s.depth = 0; s.feedback = 0; s.warmth = 0; s.mix = 50; s.manual = 50; s.rate = 0.5;
    return s;
}
// steady-state gain (dB) of a sine through the engine
static double gainAt(const Settings& s, double sr, double f)
{
    Engine e; e.setSettings(s); e.prepare(sr, 512);
    const int n = (int)(sr * 0.5);
    std::vector<float> L((size_t)n), R((size_t)n);
    for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.25 * std::sin(kTwoPi * f * i / sr));
    run(e, L, R);
    double e1 = 0; for (int i = n / 2; i < n; ++i) e1 += (double)L[(size_t)i] * L[(size_t)i];
    return 10 * std::log10(e1 / (n / 2) / (0.25 * 0.25 / 2.0) + 1e-30);
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)(sr * 1.0);
        std::vector<float> L0((size_t)n), R0((size_t)n);
        for (int i = 0; i < n; ++i) { L0[(size_t)i] = mix[(size_t)(2 * i)]; R0[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
        // ---- mix 0: the input, delayed by the latency, for every type and variant
        {
            double worst = 0; int lat3 = -1;
            for (int m = 0; m < kNumModes; ++m)
                for (int v = 0; v < 4; ++v)
                {
                    Settings s; s.mode = m; s.variant = v; s.mix = 0; s.feedback = 70; s.depth = 100;
                    Engine e; e.setSettings(s); e.prepare(sr, 512);
                    std::vector<float> L = L0, R = R0;
                    run(e, L, R, 333);
                    const int lat = e.latency();
                    if (isThroughZero(m, v)) lat3 = lat;
                    else if (lat != 0) worst = 1.0;
                    for (int i = lat; i < n; ++i) worst = std::max(worst, (double)std::fabs(L[(size_t)i] - L0[(size_t)(i - lat)]) + std::fabs(R[(size_t)i] - R0[(size_t)(i - lat)]));
                }
            CHECK(worst < 1e-6 && lat3 == (int)std::lround(0.003 * sr), "mix 0, every type and variant: the input delayed by the latency (max error %.2g; through-zero latency %d)", worst, lat3);
        }
        // ---- phaser at rest: 4 stages at 761 Hz notch where each stage reaches -45 / -135 degrees
        {
            const Settings s = still(kPhaser, 0);
            const double fc = phaserHz(0.5, 0.0, 0.0), t = std::tan(kPi * fc / sr);
            const double n1 = sr / kPi * std::atan(t * std::tan(kPi / 8.0)), n2 = sr / kPi * std::atan(t * std::tan(3.0 * kPi / 8.0));
            const double g1 = gainAt(s, sr, n1), g2 = gainAt(s, sr, n2), gc = gainAt(s, sr, fc);
            CHECK(g1 < -30.0 && g2 < -30.0 && std::fabs(gc) < 0.5, "phaser 4 at rest: notches at %.0f Hz (%.1f dB) and %.0f Hz (%.1f dB), %.0f Hz passes (%.2f dB)", n1, g1, n2, g2, fc, gc);
        }
        // ---- flanger at rest: the first comb notch at 1 / (2 delay); negative notches at the bottom
        {
            const double d = centreMs(kFlanger, 0.5) * 0.001, fn = 1.0 / (2.0 * d);
            const double g = gainAt(still(kFlanger, 0), sr, fn), gp = gainAt(still(kFlanger, 0), sr, 2.0 * fn);
            CHECK(g < -25.0 && std::fabs(gp - 6.02) < 0.5, "flanger at rest (%.2f ms): notch at %.0f Hz (%.1f dB), peak at %.0f Hz (%+.1f dB)", d * 1000, fn, g, 2 * fn, gp);
            const double gn = gainAt(still(kFlanger, 2), sr, 20.0), gnp = gainAt(still(kFlanger, 2), sr, fn);
            CHECK(gn < -10.0 && std::fabs(gnp - 6.02) < 0.5, "negative flanger: 20 Hz down %.1f dB, %.0f Hz up %+.1f dB", gn, fn, gnp);
            Settings fb = still(kFlanger, 2); fb.feedback = -60;
            const double gnf = gainAt(fb, sr, 20.0);
            CHECK(gnf < -6.0, "negative flanger with -60%% feedback still hollow at 20 Hz (%.1f dB)", gnf);
            // through-zero with the swept copy on the fixed one: flat
            double lo = 1e9, hi = -1e9;
            for (double f : { 100.0, 440.0, 1000.0, 3000.0, 9000.0 }) { const double gz = gainAt(still(kFlanger, 1), sr, f); lo = std::min(lo, gz); hi = std::max(hi, gz); }
            CHECK(hi - lo < 0.5 && std::fabs(hi - 6.02) < 0.5, "through-zero at the zero point: flat +6 dB (%.2f..%.2f dB)", lo, hi);
        }
        if (sr != 48000.0) continue;
        // ---- vibrato moves the pitch; depth 0 doesn't
        {
            auto spread = [&](double depth) {
                Settings s = still(kAnalog, 3); s.depth = depth; s.mix = 100; s.rate = 5.0;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int m = (int)(sr * 2);
                std::vector<float> L((size_t)m), R((size_t)m);
                for (int i = 0; i < m; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.5 * std::sin(kTwoPi * 1000.0 * i / sr));
                run(e, L, R);
                std::vector<double> per; double last = -1;
                for (size_t i = (size_t)(sr / 2); i < L.size(); ++i)
                    if (L[i - 1] < 0.0f && L[i] >= 0.0f) { const double t = (double)(i - 1) + L[i - 1] / (double)(L[i - 1] - L[i]); if (last >= 0) per.push_back(t - last); last = t; }
                double lo = 1e9, hi = 0; for (double p : per) { lo = std::min(lo, p); hi = std::max(hi, p); }
                return 100.0 * (hi - lo) / (sr / 1000.0);
            };
            const double s0 = spread(0), s1 = spread(100);
            CHECK(s0 < 0.01 && s1 > 2.0, "pedal vibrato: period spread %.2f %% at full depth (%.4f %% at 0)", s1, s0);
        }
        // ---- sync: the LFO follows the song position
        {
            Settings s = still(kPhaser, 0); s.sync = true; s.division = kNote4;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(512, 0.0f), R(512, 0.0f);
            double ppq = 7.25, worst = 0;
            for (int b = 0; b < 400; ++b)
            {
                Transport t; t.bpm = 120; t.ppq = ppq; t.playing = true; t.valid = true;
                if (b == 200) { t.ppq = ppq = 33.6; }   // a locate
                e.setTransport(t);
                e.process(L.data(), R.data(), 512);
                ppq += 512.0 / sr * 2.0;   // 120 BPM = 2 quarters per second
                if (b > 2 && b != 200)
                {
                    double d = (ppq - std::floor(ppq)) - e.lfoPhase(); d -= std::round(d);
                    worst = std::max(worst, std::fabs(d));
                }
            }
            CHECK(worst < 0.01, "synced to 1/4 at 120 BPM: LFO phase within %.4f cycles of the song position, through a locate", worst);
        }
        // ---- stress
        {
            std::mt19937 rng(23);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.mode = (int)(u(rng) * kNumModes); s.variant = (int)(u(rng) * 4); s.rate = 0.02 * std::pow(600.0, u(rng));
                    s.sync = u(rng) < 0.3; s.division = (int)(u(rng) * kNumNotes); s.depth = u(rng) * 100; s.manual = u(rng) * 100;
                    s.feedback = u(rng) * 190 - 95; s.spread = u(rng) * 180; s.tone = 1000 * std::pow(20.0, u(rng)); s.warmth = u(rng) * 100;
                    s.mix = u(rng) * 100; s.outDb = 0;
                    e.setSettings(s);
                }
                if (it % 13 == 5) { Settings t = s; t.variant = (s.variant + 1) % 4; e.setSettings(t); e.setSettings(s); }
                Transport t; t.bpm = 60 + u(rng) * 120; t.ppq = pos / sr * 2.0; t.playing = u(rng) < 0.8; t.valid = true;
                e.setTransport(t);
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
                    if (it > 1700) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 30.0, "random settings, sizes, mid-fade switches, 95%% feedback: finite, peak %.2f after a +60 dB spike", peak);
        }
        // ---- CPU (the heaviest: 6-voice rack, 12-stage phaser)
        for (auto mv : { std::make_pair((int)kRack, 3), std::make_pair((int)kPhaser, 3), std::make_pair((int)kEnsemble, 0) })
        {
            Settings s; s.mode = mv.first; s.variant = mv.second; s.depth = 70; s.feedback = 40;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            const auto t0 = std::chrono::steady_clock::now();
            run(e, L, R, 512);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: %s %s: %.1f %% of one core\n", kModeNames[mv.first], variantKey(mv.first, mv.second), 100.0 * secs / 1.0);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
