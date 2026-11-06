// pump GRUMBLE: mix 0 is the input delayed by the latency, exactly; BEAST an octave down puts the voice an octave
// down; the gate silences room noise and silence stays silent; GROWL and SWARM sound while the voice does and stop
// after it; the morph pad's gains are constant power and one layer on its own is always at full; every factory
// preset stays finite and near the input's level; random settings at odd block sizes stay finite and bounded.
// 44.1, 48 and 96 kHz. Also prints CPU per second of audio.
#include "../plugins/grumble/GrumbleEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::grumble;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

// a sung "aah" at f0 (a glottal pulse through three formants, a little vibrato), starting at t0 and lasting dur
static void voice(std::vector<float>& L, std::vector<float>& R, double sr, double f0, double t0, double dur, double amp = 0.3)
{
    fx::Svf f1, f2, f3;
    f1.set(700, 6, sr); f2.set(1200, 8, sr); f3.set(2600, 10, sr);
    double ph = 0.0;
    const int a = (int)(t0 * sr), b = std::min((int)L.size(), (int)((t0 + dur) * sr));
    for (int i = a; i < b; ++i)
    {
        const double t = (i - a) / sr;
        const double f = f0 * (1.0 + 0.004 * std::sin(kTwoPi * 5.0 * t));
        ph += f / sr;
        double g = 0.0;
        if (ph >= 1.0) { ph -= 1.0; g = 1.0; }
        const double s = f1.bandpass(g) + 0.6 * f2.bandpass(g) + 0.3 * f3.bandpass(g);
        const double env = std::min(1.0, std::min(t / 0.02, (dur - t) / 0.02));
        const float v = (float)(s * amp * env * 3.0);
        L[(size_t)i] += v; R[(size_t)i] += v;
    }
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    const int n = (int)L.size();
    for (int off = 0; off < n; off += block) e.process(L.data() + off, R.data() + off, std::min(block, n - off));
}
static Settings clean()
{
    Settings s;
    for (auto& o : s.on) o = false;
    s.gateDb = kGateOff; s.drive = 0; s.tone = 0; s.cave = 0; s.mix = 100; s.outDb = 0;
    return s;
}
static double rms(const std::vector<float>& v, int a, int b)
{
    double e = 0.0; b = std::min(b, (int)v.size());
    for (int i = a; i < b; ++i) e += (double)v[(size_t)i] * v[(size_t)i];
    return std::sqrt(e / std::max(1, b - a));
}
static double peakOf(const std::vector<float>& v) { double p = 0.0; for (float x : v) p = std::max(p, (double)std::fabs(x)); return p; }
static bool finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }

// the pitch (MIDI) the tracker hears in [a, b)
static double pitchOf(const std::vector<float>& v, double sr, int a, int b)
{
    pitch::Tracker t;
    t.prepare(sr, 40.0, 1000.0, 24000.0, 0.0053);
    std::vector<double> ms;
    for (int i = a; i < b; ++i)
        if (t.push(v[(size_t)i]) && t.result().voiced && t.result().confidence > 0.5f) ms.push_back(pitch::hzToMidi(t.result().hz));
    if (ms.empty()) return -1.0;
    std::sort(ms.begin(), ms.end());
    return ms[ms.size() / 2];
}

static void testRate(double sr)
{
    printf("== %.0f Hz\n", sr);
    const int n = (int)(2.0 * sr);
    // mix 0: the input, delayed by the latency, exactly
    {
        Engine e; e.prepare(sr);
        Settings s; s.mix = 0;
        e.set(s);
        std::vector<float> L((size_t)n, 0.0f), R;
        voice(L, L, sr, 150.0, 0.1, 1.5);
        std::vector<float> inL = L; R = L;
        run(e, L, R, 100);
        const int lat = e.latency();
        double err = 0.0;
        for (int i = lat; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - inL[(size_t)(i - lat)]));
        CHECK(err < 1e-6, "mix 0 = input delayed by %d samples (max error %.2g)", lat, err);
    }
    // BEAST an octave down
    {
        Engine e; e.prepare(sr);
        Settings s = clean(); s.on[kBeast] = true; s.beastPitch = -12; s.beastFormant = 0; s.beastFry = 0; s.level[kBeast] = 100;
        e.set(s);
        std::vector<float> L((size_t)n, 0.0f);
        voice(L, L, sr, 200.0, 0.1, 1.6);
        std::vector<float> R = L;
        const double in = pitchOf(L, sr, (int)(0.4 * sr), (int)(1.4 * sr));
        run(e, L, R);
        const double out = pitchOf(L, sr, (int)(0.4 * sr), (int)(1.4 * sr));
        CHECK(std::fabs(out - (in - 12.0)) < 0.5, "beast -12: %.2f in -> %.2f out", in, out);
        const double g = 20.0 * std::log10(rms(L, (int)(0.4 * sr), (int)(1.4 * sr)) / std::max(1e-9, rms(R, 0, 1) + 1e-9));
        (void)g;
    }
    // the gate: quiet noise under the threshold makes nothing; silence stays silent
    {
        Engine e; e.prepare(sr);
        Settings s; s.gateDb = -50; s.cave = 0; s.on[kSwarm] = true;
        e.set(s);
        std::vector<float> L((size_t)n), R((size_t)n);
        fx::Rng r(7);
        for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)(0.0003 * r.bi());   // -70 dB-ish
        run(e, L, R);
        const double o = rms(L, (int)(0.5 * sr), n);
        CHECK(o < 1e-5, "gate at -50 dB keeps -70 dB noise silent (rms %.2g)", o);
        std::vector<float> Z((size_t)n, 0.0f), Z2((size_t)n, 0.0f);
        Engine e2; Settings s2; s2.on[kSplit] = s2.on[kSwarm] = true; e2.prepare(sr); e2.set(s2);
        run(e2, Z, Z2);
        CHECK(peakOf(Z) < 1e-6, "silence in, silence out (every layer on, peak %.2g)", peakOf(Z));
    }
    // GROWL and SWARM sound with the voice and stop after it
    for (int layer : { (int)kGrowl, (int)kSwarm })
    {
        Engine e; e.prepare(sr);
        Settings s = clean(); s.on[layer] = true; s.level[layer] = 100; s.gateDb = -60;
        e.set(s);
        std::vector<float> L((size_t)n, 0.0f);
        voice(L, L, sr, 140.0, 0.1, 0.9);
        std::vector<float> R = L;
        const double in = rms(L, (int)(0.3 * sr), (int)(0.9 * sr));
        run(e, L, R);
        const double during = rms(L, (int)(0.3 * sr), (int)(0.9 * sr)), after = rms(L, (int)(1.5 * sr), n);
        const double rel = 20.0 * std::log10(during / in);
        CHECK(rel > -18.0 && rel < 12.0, "%s sounds with the voice (%.1f dB from the input)", kLayerNames[layer], rel);
        CHECK(after < during * 0.003, "%s stops after it (%.1f dB down)", kLayerNames[layer], 20.0 * std::log10(after / during + 1e-12));
        CHECK(finite(L) && finite(R), "%s finite", kLayerNames[layer]);
    }
}

static void testMorph()
{
    printf("== morph pad\n");
    bool all[kNumLayers] = { true, true, true, true };
    double worst = 0.0;
    for (double x = 0.0; x <= 1.0; x += 0.1)
        for (double y = 0.0; y <= 1.0; y += 0.1)
        {
            double g[kNumLayers]; morphGains(x, y, all, g);
            double p = 0.0; for (double v : g) p += v * v;
            worst = std::max(worst, std::fabs(p - 1.0));
        }
    CHECK(worst < 1e-9, "constant power everywhere on the pad (worst %.2g)", worst);
    double g[kNumLayers];
    morphGains(0.0, 1.0, all, g);
    CHECK(g[kBeast] > 0.99 && g[kGrowl] < 0.03, "top left is BEAST (%.3f, growl %.3f)", g[kBeast], g[kGrowl]);
    for (int k = 0; k < kNumLayers; ++k)
    {
        bool one[kNumLayers] = {}; one[k] = true;
        double lo = 1.0;
        for (double x = 0.0; x <= 1.0; x += 0.25) for (double y = 0.0; y <= 1.0; y += 0.25) { morphGains(x, y, one, g); lo = std::min(lo, g[k]); }
        CHECK(lo > 0.999, "%s alone is at full anywhere on the pad (%.3f)", kLayerNames[k], lo);
    }
}

static Settings fromPreset(const Preset& p)
{
    // apply the preset's values over the parameter defaults, as the plugin does
    const auto& sp = params();
    std::vector<float> v(sp.size());
    for (size_t i = 0; i < sp.size(); ++i) v[i] = sp[i].def;
    for (const auto& pv : p.values)
        for (size_t i = 0; i < sp.size(); ++i) if (!std::strcmp(sp[i].symbol, pv.symbol)) v[i] = pv.value;
    Settings s;
    for (int k = 0; k < kNumLayers; ++k) { s.on[k] = v[(size_t)kLayerOn[k]] >= 0.5f; s.level[k] = v[(size_t)kLayerLevel[k]]; }
    s.pan[kBeast] = v[kBeastPan]; s.pan[kSplit] = v[kSplitPan]; s.pan[kGrowl] = v[kGrowlPan]; s.pan[kSwarm] = v[kSwarmWidth];
    s.beastPitch = v[kBeastPitch]; s.beastFormant = v[kBeastFormant]; s.beastFry = v[kBeastFry];
    s.splitPitch = v[kSplitPitch]; s.splitFormant = v[kSplitFormant]; s.splitWobble = v[kSplitWobble];
    s.growlPitch = v[kGrowlPitch]; s.growlRough = v[kGrowlRough]; s.growlBreath = v[kGrowlBreath]; s.growlSize = v[kGrowlSize];
    s.swarmPitch = v[kSwarmPitch]; s.swarmSpread = v[kSwarmSpread]; s.swarmDensity = v[kSwarmDensity]; s.swarmGrainMs = v[kSwarmGrain];
    s.morphX = v[kMorphX]; s.morphY = v[kMorphY];
    s.gateDb = v[kGate]; s.drive = v[kDrive]; s.tone = v[kTone]; s.cave = v[kCave]; s.mix = v[kMix]; s.outDb = v[kOutGain];
    return s;
}

static void testPresets()
{
    printf("== presets (48 kHz, a sung phrase)\n");
    const double sr = 48000.0;
    const int n = (int)(3.0 * sr);
    double lo = 1e9, hi = -1e9;
    for (const Preset& p : presets())
    {
        Engine e; e.prepare(sr); e.set(fromPreset(p));
        std::vector<float> L((size_t)n, 0.0f);
        voice(L, L, sr, 130.0, 0.1, 0.8); voice(L, L, sr, 175.0, 1.0, 0.6); voice(L, L, sr, 110.0, 1.7, 0.9);
        std::vector<float> R = L;
        const double in = rms(L, 0, n);
        run(e, L, R, 128);
        const double out = std::sqrt(0.5 * (rms(L, 0, n) * rms(L, 0, n) + rms(R, 0, n) * rms(R, 0, n)));
        const double d = 20.0 * std::log10(out / in);
        lo = std::min(lo, d); hi = std::max(hi, d);
        const bool ok = finite(L) && finite(R) && peakOf(L) < 4.0 && peakOf(R) < 4.0 && d > -18.0 && d < 12.0;
        if (!ok) CHECK(false, "preset \"%s\": %.1f dB, peak %.2f", p.name, d, std::max(peakOf(L), peakOf(R)));
    }
    CHECK(true, "%zu presets finite and bounded, %.1f .. %.1f dB from the input", presets().size(), lo, hi);
}

static void testRandom()
{
    printf("== random settings\n");
    std::mt19937 rnd(12345);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const auto& sp = params();
    for (double sr : { 44100.0, 96000.0 })
    {
        Engine e; e.prepare(sr);
        const int n = (int)(4.0 * sr);
        std::vector<float> L((size_t)n, 0.0f);
        voice(L, L, sr, 90.0, 0.0, 1.0, 0.9); voice(L, L, sr, 600.0, 1.2, 1.0, 0.9); voice(L, L, sr, 220.0, 2.4, 1.4, 2.0);
        fx::Rng r(3);
        for (int i = (int)(3.5 * sr); i < (int)(3.6 * sr); ++i) L[(size_t)i] += (float)(5.0 * r.bi());   // an absurd burst
        std::vector<float> R = L;
        int off = 0;
        double pk = 0.0;
        while (off < n)
        {
            Preset p { "rand", {} };
            for (const auto& s : sp) p.values.push_back({ s.symbol, s.min + (s.max - s.min) * u(rnd) });
            Settings s = fromPreset(p); s.outDb = 0; s.mix = 100;
            e.set(s);
            const int len = std::min(n - off, 1 + (int)(u(rnd) * 3000));
            e.process(L.data() + off, R.data() + off, len);
            off += len;
        }
        pk = std::max(peakOf(L), peakOf(R));
        CHECK(finite(L) && finite(R) && pk < 40.0, "%.0f Hz: finite, peak %.2f", sr, pk);
    }
}

static void testCpu()
{
    const double sr = 48000.0;
    const int n = (int)(10.0 * sr);
    Engine e; e.prepare(sr);
    Settings s; for (auto& o : s.on) o = true; s.cave = 30;
    e.set(s);
    std::vector<float> L((size_t)n, 0.0f);
    for (double t = 0.0; t < 9.5; t += 1.0) voice(L, L, sr, 120.0 + 20.0 * t, t, 0.8);
    std::vector<float> R = L;
    const auto t0 = std::chrono::steady_clock::now();
    run(e, L, R, 256);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("  cpu: every layer on, %.1f ms per second of audio (%.2f %% of one core)\n", ms / 10.0, ms / 100.0);
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 }) testRate(sr);
    testMorph();
    testPresets();
    testRandom();
    testCpu();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
