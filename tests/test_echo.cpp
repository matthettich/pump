// pump ECHO: an impulse's first echo lands exactly on TIME with the right level and each repeat falls by FEEDBACK
// (44.1 / 48 / 96 kHz); ping-pong alternates sides; SYNC follows the tempo (1/4 at 120 BPM = 24000 samples at 48 kHz);
// mix 0 is the input exactly; Pitch +12 climbing doubles the frequency per repeat (once: every repeat an octave up);
// Reverse plays a rising ramp falling; bucket-brigade long times are darker than short ones; tape wow and flutter
// move the pitch where Digital holds it; Digital time changes and mode switches don't click; 120 % feedback stays
// bounded for 30 s in every mode (and keeps sounding); FREEZE holds the loop's level for 5 s; DUCK dips the echoes
// under the input and lets them back; random-settings stress (block sizes, tempo changes, absurd input) stays finite
// and bounded. Also prints CPU per mode.
#include "TestMix.hpp"
#include "../plugins/echo/EchoEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::echo;
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
// fully wet, no sync, clean: the echo alone
static Settings wet(int mode, double ms, double fb)
{
    Settings s; s.mode = mode; s.sync = false; s.timeL = s.timeR = ms; s.link = true; s.feedback = fb; s.mix = 100;
    s.modDepth = 0; s.age = 0; s.drive = 0; s.diffuse = 0; s.duck = 0;
    return s;
}
static double rms(const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min(to, x.size());
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)x[i] * x[i];
    return std::sqrt(e / std::max<size_t>(1, to - from));
}
static double db(double g) { return 20.0 * std::log10(g + 1e-30); }
static size_t argmax(const std::vector<float>& x, size_t from, size_t to)
{
    size_t k = from; float m = -1;
    for (size_t i = from; i < std::min(to, x.size()); ++i) if (std::fabs(x[i]) > m) { m = std::fabs(x[i]); k = i; }
    return k;
}
// frequency from rising zero crossings (with sub-sample interpolation) over [from, to)
static double zcFreq(const std::vector<float>& x, size_t from, size_t to, double sr)
{
    double first = -1, last = -1; int count = 0;
    for (size_t i = from + 1; i < std::min(to, x.size()); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (double)(i - 1) + (double)x[i - 1] / ((double)x[i - 1] - x[i]);
            if (first < 0) first = t; else ++count;
            last = t;
        }
    return count > 0 ? sr * count / (last - first) : 0.0;
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        // ---- Digital: the first echo lands on TIME with the input's level; repeats fall by FEEDBACK
        for (double ms : { 1.0, 37.0, 375.0, 1000.0 })
        {
            Engine e; e.setSettings(wet(kModeDigital, ms, 50)); e.prepare(sr, 512);
            const int T = (int)std::lround(ms * 0.001 * sr);
            std::vector<float> L((size_t)(T * 4 + 64), 0.0f), R = L;
            L[0] = R[0] = 0.5f;
            run(e, L, R, 128);
            const size_t k1 = argmax(L, 1, (size_t)T + T / 2 + 2);
            const size_t k2 = argmax(L, k1 + 1, k1 + T + T / 2 + 2);
            const double a1 = L[k1], a2 = L[k2];
            CHECK(std::abs((long)k1 - T) <= 1 && std::fabs(a1 - 0.5) < 0.01 && std::abs((long)(k2 - k1) - T) <= 1 && std::fabs(a2 / a1 - 0.5) < 0.02,
                  "digital %.0f ms: first echo at %zu (want %d) level %.3f, second %zu later at %.3f of it", ms, k1, T, a1, k2 - k1, a2 / a1);
        }
        // ---- mix 0: the input exactly (no latency)
        {
            Settings s = wet(kModeTape, 250, 80); s.mix = 0; s.age = 60; s.drive = 12;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(sr * 2.0);
            std::vector<float> L((size_t)n), R((size_t)n);
            for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
            const std::vector<float> L0 = L, R0 = R;
            run(e, L, R, 333);
            double err = 0;
            for (int i = 0; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]) + std::fabs(R[(size_t)i] - R0[(size_t)i]));
            CHECK(err == 0.0 && e.latency() == 0, "mix 0 = the input exactly (max error %.2g, latency %d)", err, e.latency());
        }
        // ---- the test mix through every mode at 120 % feedback for 30 s: bounded, and still sounding
        if (sr == 48000.0)
        {
            for (int mode = 0; mode < kNumModes; ++mode)
            {
                Settings s = wet(mode, 330, 120); s.pitchMode = kShiftClimb; s.age = 50; s.diffuse = 40; s.mix = 50;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int n = (int)(sr * 30.0), feed = (int)(sr * 4.0);
                std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
                for (int i = 0; i < feed; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
                run(e, L, R, 512);
                double peak = 0; bool finite = true;
                for (int i = 0; i < n; ++i) { if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false; peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i]))); }
                const double tail = db(rms(L, (size_t)n - (size_t)sr, (size_t)n));
                CHECK(finite && peak < 2.5 && tail > -40.0, "%s at 120%% feedback for 30 s: peak %.2f, still %.1f dBFS 26 s after the input stops",
                      kModeNames[mode], peak, tail);
            }
        }
        if (sr != 48000.0) continue;

        // ---- ping-pong: the mono input goes left first, then the repeats alternate sides
        {
            Settings s = wet(kModeDigital, 250, 60); s.route = kRoutePingPong;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int T = 12000;
            std::vector<float> L((size_t)(T * 5 + 100), 0.0f), R = L;
            L[0] = R[0] = 0.5f;
            run(e, L, R);
            bool alt = true;
            char trace[128] = {}; int tp = 0;
            for (int k = 1; k <= 4; ++k)
            {
                const double l = std::fabs(L[(size_t)(k * T)]), r = std::fabs(R[(size_t)(k * T)]);
                const bool left = k % 2 == 1;
                if (left ? !(l > 0.01 && r < 1e-4) : !(r > 0.01 && l < 1e-4)) alt = false;
                tp += std::snprintf(trace + tp, sizeof trace - (size_t)tp, " %s %.3f", l > r ? "L" : "R", std::max(l, r));
            }
            CHECK(alt, "ping-pong: repeats alternate L R L R (%s)", trace);
        }
        // ---- wide: mono in, the right repeats trail by the offset, so the echoes spread
        {
            Settings s = wet(kModeDigital, 250, 0); s.route = kRouteWide;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(24000, 0.0f), R = L;
            L[0] = R[0] = 0.5f;
            run(e, L, R);
            const size_t kl = argmax(L, 1, L.size()), kr = argmax(R, 1, R.size());
            CHECK(kl == 12000 && kr == 12000 + (size_t)std::lround(kWideOffsetSeconds * sr), "wide: left echo at %zu, right at %zu (+%.0f ms)", kl, kr, 1000.0 * (double)(kr - kl) / sr);
        }
        // ---- sync: note divisions at the host tempo
        {
            struct { int note; double bpm; int want; } cases[] = { { kNote4, 120.0, 24000 }, { kNote8D, 90.0, 24000 }, { kNote16T, 150.0, 3200 }, { kNote2Bars, 60.0, 384000 } };
            for (const auto& c : cases)
            {
                Settings s = wet(kModeDigital, 100, 0); s.sync = true; s.noteL = s.noteR = c.note;
                Engine e; e.setSettings(s); e.setTempo(c.bpm); e.prepare(sr, 512);
                std::vector<float> L((size_t)c.want + 4000, 0.0f), R = L;
                L[0] = R[0] = 0.5f;
                run(e, L, R, 512);
                const size_t k = argmax(L, 1, L.size());
                CHECK(std::abs((long)k - c.want) <= 1, "sync %s at %.0f BPM: echo at %zu samples (want %d)", kNoteNames[c.note], c.bpm, k, c.want);
            }
            // the tempo moves while running: the echo follows
            Settings s = wet(kModeDigital, 100, 0); s.sync = true; s.noteL = s.noteR = kNote4;
            Engine e; e.setSettings(s); e.setTempo(120.0); e.prepare(sr, 512);
            std::vector<float> L(48000, 0.0f), R = L;
            run(e, L, R);
            e.setTempo(100.0);
            std::vector<float> L2(48000, 0.0f), R2 = L2;
            L2[0] = R2[0] = 0.5f;
            run(e, L2, R2);
            const size_t k = argmax(L2, 1, L2.size());
            CHECK(std::abs((long)k - 28800) <= 1, "tempo 120 -> 100 BPM: a 1/4 echo moves to %zu samples (want 28800)", k);
        }
        // ---- pitch: +12 climbing doubles the frequency every repeat; once: every repeat an octave up
        {
            for (int pm : { kShiftClimb, kShiftOnce })
            {
                Settings s = wet(kModePitch, 400, 70); s.pitch = 12; s.pitchMode = pm;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int T = 19200, burst = 9600;
                std::vector<float> L((size_t)(T * 4 + 4800), 0.0f), R;
                for (int i = 0; i < burst; ++i)
                {
                    const double w = std::min(1.0, std::min(i, burst - i) / 480.0);
                    L[(size_t)i] = (float)(0.3 * w * std::sin(kTwoPi * 220.0 * i / sr));
                }
                R = L;
                run(e, L, R);
                double f[3];
                for (int k = 1; k <= 3; ++k) f[k - 1] = zcFreq(L, (size_t)(k * T + 2400), (size_t)(k * T + burst - 2400), sr);
                if (pm == kShiftClimb)
                    CHECK(std::fabs(f[0] / 440.0 - 1) < 0.02 && std::fabs(f[1] / 880.0 - 1) < 0.02 && std::fabs(f[2] / 1760.0 - 1) < 0.02,
                          "pitch +12 climb: a 220 Hz burst repeats at %.1f, %.1f, %.1f Hz", f[0], f[1], f[2]);
                else
                    CHECK(std::fabs(f[0] / 440.0 - 1) < 0.02 && std::fabs(f[1] / 440.0 - 1) < 0.02 && std::fabs(f[2] / 440.0 - 1) < 0.02,
                          "pitch +12 once: every repeat at 440 Hz (%.1f, %.1f, %.1f Hz)", f[0], f[1], f[2]);
            }
            Settings s = wet(kModePitch, 400, 0); s.pitch = -7; s.fine = -30;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(48000, 0.0f), R;
            for (int i = 0; i < 9600; ++i) L[(size_t)i] = (float)(0.3 * std::min(1.0, std::min(i, 9600 - i) / 480.0) * std::sin(kTwoPi * 440.0 * i / sr));
            R = L;
            run(e, L, R);
            const double f = zcFreq(L, 19200 + 2400, 19200 + 7200, sr), want = 440.0 * std::pow(2.0, -7.3 / 12.0);
            CHECK(std::fabs(f / want - 1) < 0.01, "pitch -7 st -30 cents: 440 Hz echoes at %.1f Hz (want %.1f)", f, want);
        }
        // ---- reverse: the echo of a rising ramp falls
        {
            auto slope = [&](int mode) {
                Settings s = wet(mode, 300, 0);
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L(48000, 0.0f), R;
                const int len = 4800;
                for (int i = 0; i < len; ++i) L[(size_t)i] = (float)(0.5 * i / len);
                R = L;
                run(e, L, R);
                // the echo: the stretch where it is loudest, least squares slope over its middle
                size_t a = 0, b = 0;
                for (size_t i = 1; i < L.size(); ++i) if (std::fabs(L[i]) > 0.05f) { if (!a) a = i; b = i; }
                const size_t m0 = a + (b - a) / 5, m1 = b - (b - a) / 5;
                double sx = 0, sy = 0, sxx = 0, sxy = 0; const double cnt = (double)(m1 - m0);
                for (size_t i = m0; i < m1; ++i) { const double x = (double)i, y = L[i]; sx += x; sy += y; sxx += x * x; sxy += x * y; }
                const double sl = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
                printf("    %s: echo from %.1f to %.1f ms\n", kModeNames[mode], 1000.0 * a / sr, 1000.0 * b / sr);
                return sl * len / 0.5;   // relative to the input ramp's slope
            };
            const double sd = slope(kModeDigital), sv = slope(kModeReverse);
            CHECK(sd > 0.95 && sv < -0.95, "a rising ramp echoes rising in Digital (slope %+.2f x the input) and falling in Reverse (%+.2f)", sd, sv);
        }
        // ---- bucket-brigade: long times are darker (energy above 3 kHz, relative to the whole echo)
        {
            auto hfShare = [&](double ms) {
                Settings s = wet(kModeBbd, ms, 0);
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int T = (int)(ms * 0.001 * sr), N = 8192;
                std::vector<float> L((size_t)(T + N + 9600), 0.0f), R;
                fx::Rng rng(7);
                for (int i = 0; i < N + 9600; ++i) L[(size_t)i] = (float)(0.2 * rng.bi());
                R = L;
                run(e, L, R);
                RealFFT fft; fft.setup(N);
                AlignedBuffer in((size_t)N), out((size_t)N);
                for (int i = 0; i < N; ++i) in[(size_t)i] = L[(size_t)(T + 4800 + i)] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
                fft.forward(in.data(), out.data());
                double hi = 0, all = 0;
                for (int k = 1; k < N / 2; ++k)
                {
                    const double p = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
                    all += p; if (k * sr / N > 3000.0) hi += p;
                }
                return 10 * std::log10(hi / all + 1e-30);
            };
            const double s50 = hfShare(50), s300 = hfShare(300), s1500 = hfShare(1500);
            CHECK(s300 < s50 - 6.0 && s1500 < s300 - 6.0, "bucket-brigade: energy above 3 kHz %.1f dB (50 ms), %.1f dB (300 ms), %.1f dB (1.5 s)", s50, s300, s1500);
        }
        // ---- tape wow and flutter: the echo's pitch wanders (Digital: steady)
        {
            auto spread = [&](int mode, double age) {
                Settings s = wet(mode, 300, 0); s.age = age;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int n = (int)(sr * 6.0);
                std::vector<float> L((size_t)n), R;
                for (int i = 0; i < n; ++i) L[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 1000.0 * i / sr));
                R = L;
                run(e, L, R);
                // frequency in 20 ms frames over 4 s; the spread in cents
                std::vector<double> c;
                for (int f0 = (int)sr; f0 + 960 < n; f0 += 960) c.push_back(1200.0 * std::log2(zcFreq(L, (size_t)f0, (size_t)(f0 + 960), sr) / 1000.0));
                double m = 0; for (double v : c) m += v; m /= (double)c.size();
                double v2 = 0; for (double v : c) v2 += (v - m) * (v - m);
                return std::sqrt(v2 / (double)c.size());
            };
            const double d = spread(kModeDigital, 0), t0 = spread(kModeTape, 0), t1 = spread(kModeTape, 60);
            CHECK(d < 0.05 && t0 > 0.5 && t1 > 2.0 * t0, "tape wow + flutter: pitch spread %.2f cents (age 0), %.2f cents (age 60); digital %.3f cents", t0, t1, d);
        }
        // ---- Digital time changes and mode switches on a steady tone: no clicks
        {
            Settings s = wet(kModeDigital, 200, 40);
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const int n = 96000;
            std::vector<float> L((size_t)n), R;
            for (int i = 0; i < n; ++i) L[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 300.0 * i / sr));
            R = L;
            double maxJump = 0;
            const double bound = 2.0 * kTwoPi * 300.0 / sr * 0.3 / (1.0 - 0.4);   // the steepest the echo sum can be
            for (int off = 0, k = 0; off < n; off += 128, ++k)
            {
                if (k % 9 == 4) { s.timeL = s.timeR = 30.0 + std::fmod(k * 137.0, 900.0); e.setSettings(s); }
                e.process(L.data() + off, R.data() + off, 128);
            }
            for (int i = 4800; i < n; ++i) maxJump = std::max(maxJump, (double)std::fabs(L[(size_t)i] - L[(size_t)(i - 1)]));
            CHECK(maxJump < bound, "digital time changes every 24 ms on a 300 Hz tone: largest step %.4f (smooth tone bound %.4f)", maxJump, bound);

            Settings m = wet(kModeDigital, 200, 40);
            Engine e2; e2.setSettings(m); e2.prepare(sr, 512);
            for (int i = 0; i < n; ++i) L[(size_t)i] = (float)(0.3 * std::sin(kTwoPi * 300.0 * i / sr));
            R = L;
            for (int off = 0, k = 0; off < n; off += 128, ++k)
            {
                if (k % 30 == 15) { m.mode = (m.mode + 1) % kNumModes; e2.setSettings(m); }
                e2.process(L.data() + off, R.data() + off, 128);
            }
            maxJump = 0;
            for (int i = 4800; i < n; ++i) maxJump = std::max(maxJump, (double)std::fabs(L[(size_t)i] - L[(size_t)(i - 1)]));
            CHECK(maxJump < 0.08, "mode switches every 80 ms on a 300 Hz tone: largest step %.4f", maxJump);
        }
        // ---- freeze: the loop's level holds for 5 s (and the input stays out of it)
        {
            for (int mode : { kModeDigital, kModeTape, kModeBbd })
                for (int route : { kRouteStereo, kRoutePingPong })
                {
                    Settings s = wet(mode, 450, 50); s.route = route; s.age = 40;
                    Engine e; e.setSettings(s); e.prepare(sr, 512);
                    const int n = (int)(sr * 8.0), f0 = (int)(sr * 2.0);
                    std::vector<float> L((size_t)n), R((size_t)n);
                    for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
                    for (int off = 0; off < n; off += 256)
                    {
                        if (off == f0) { s.freeze = true; e.setSettings(s); }
                        e.process(L.data() + off, R.data() + off, std::min(256, n - off));
                    }
                    double lo = 1e9, hi = -1e9;
                    for (int k = 0; k < 10; ++k)
                    {
                        const size_t a = (size_t)(f0 + sr * 0.5 + k * sr * 0.5);
                        const double l = db(std::sqrt(0.5 * (rms(L, a, a + (size_t)(sr * 0.45)) * rms(L, a, a + (size_t)(sr * 0.45)) + rms(R, a, a + (size_t)(sr * 0.45)) * rms(R, a, a + (size_t)(sr * 0.45)))));
                        lo = std::min(lo, l); hi = std::max(hi, l);
                    }
                    CHECK(hi - lo < 3.0 && lo > -40.0, "freeze (%s, %s): the held loop stays within %.2f dB for 5 s (%.1f..%.1f dBFS)", kModeNames[mode], kRouteNames[route], hi - lo, lo, hi);
                }
        }
        // ---- duck: the echoes dip while the input plays and come back after it stops
        {
            auto level = [&](double duck, double& during, double& after) {
                Settings s = wet(kModeDigital, 300, 70); s.duck = duck;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const int n = (int)(sr * 3.0), on = (int)(sr * 1.5);
                std::vector<float> L((size_t)n, 0.0f), R;
                for (int i = 0; i < on; ++i) L[(size_t)i] = (float)(0.25 * std::sin(kTwoPi * 500.0 * i / sr));
                R = L;
                run(e, L, R);
                // mix 100 = the echoes alone: their level while the input plays, and after it stops
                during = db(rms(L, (size_t)(sr * 0.8), (size_t)(sr * 1.4)));
                after = db(rms(L, (size_t)(sr * 2.3), (size_t)(sr * 2.9)));
            };
            double d0, a0, d1, a1;
            level(0, d0, a0);
            level(100, d1, a1);
            CHECK(d1 < d0 - 20.0 && std::fabs(a1 - a0) < 1.5, "duck 100%%: the echoes are %.1f dB lower under the input, %.1f dB of the unducked level 0.8 s after it stops",
                  d0 - d1, a1 - a0);
        }
        // ---- stress: random settings, block sizes, sync and tempo changes, absurd input
        {
            std::mt19937 rng(23);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            for (int it = 0; it < 4000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.mode = (int)(u(rng) * kNumModes); s.timeL = 1.0 * std::pow(4000.0, u(rng)); s.timeR = 1.0 * std::pow(4000.0, u(rng));
                    s.sync = u(rng) < 0.5; s.noteL = (int)(u(rng) * kNumNotes); s.noteR = (int)(u(rng) * kNumNotes); s.link = u(rng) < 0.3;
                    s.feedback = u(rng) * 120; s.route = (int)(u(rng) * kNumRoutes); s.width = u(rng) * 100;
                    s.modDepth = u(rng) * 100; s.modRate = 0.05 * std::pow(200.0, u(rng)); s.age = u(rng) * 100; s.drive = u(rng) * 24;
                    s.lowCut = 10 * std::pow(200.0, u(rng)); s.highCut = 1000 * std::pow(22.0, u(rng)); s.diffuse = u(rng) * 100;
                    s.duck = u(rng) * 100; s.freeze = u(rng) < 0.15; s.pitch = std::round(u(rng) * 48 - 24); s.fine = u(rng) * 200 - 100;
                    s.pitchMode = (int)(u(rng) * 2); s.mix = u(rng) * 100; s.outDb = 0;
                    e.setSettings(s);
                }
                if (it % 11 == 3) e.setTempo(40.0 + u(rng) * 200.0);
                if (it % 13 == 5) { Settings t = s; t.mode = (s.mode + 1) % kNumModes; e.setSettings(t); e.setSettings(s); }
                const int bs = 1 + (int)(u(rng) * 1200);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 2000 && i == 0) L[0] = 1000.0f;   // absurd (but finite) input: must stay bounded
                }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                    if (it > 2100 || it < 2000) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 4.0, "random settings, sizes, tempo and sync changes: finite, peak %.2f (outside a +60 dB spike)", peak);
        }
        // ---- CPU
        {
            const int n = (int)(sr * 4.0);
            for (int mode = 0; mode < kNumModes; ++mode)
            {
                Settings s; s.mode = mode; s.modDepth = 30; s.diffuse = 40; s.age = 30;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R((size_t)n);
                for (int i = 0; i < n; ++i) { L[(size_t)i] = mix[(size_t)(2 * i)]; R[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
                const auto t0 = std::chrono::steady_clock::now();
                run(e, L, R, 512);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("  cpu: %-8s %.1f %% of one core\n", kModeNames[mode], 100.0 * secs / 4.0);
            }
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
