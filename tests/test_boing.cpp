// pump BOING: every pad of the default kit sounds and dies away; a clean spring pad (HARMONIC 100, no bend) rings at its
// PITCH, an analog sine pad too; BEND starts a hit higher and falls back; HITS 4 makes four onsets; the closed hat
// chokes the open one; the GM notes play their pads; the tank send rings on after the pads; random settings stay
// finite and bounded. CPU.
#include "../plugins/boing/BoingEngine.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::boing;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static const double kSr = 48000.0;

static Settings defaults()
{
    Settings s;
    for (int p = 0; p < kPads; ++p)
    {
        const PadInfo& d = kPadInfo[p];
        PadSettings& o = s.pad[p];
        o.tank = (int)d.tank; o.wave = (int)d.wave; o.blend = d.blend * 0.01; o.pitch = d.pitch; o.decay = d.decay * 0.001; o.harmonic = d.harm * 0.01;
        o.spread = d.spread * 0.01; o.drip = d.drip; o.bend = d.bend; o.bendTime = d.bendT * 0.001; o.noise = d.noise * 0.01; o.noiseTone = d.ntone * 0.01;
        o.click = d.click * 0.01; o.hits = (int)d.hits; o.level = d.level; o.pan = d.pan * 0.01; o.send = d.send * 0.01;
    }
    return s;
}
static std::vector<float> run(Engine& e, int n, std::vector<synth::Midi> ev, std::vector<float>* Rout = nullptr)
{
    std::vector<float> L((size_t)n), R((size_t)n);
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
    if (Rout) *Rout = R;
    return L;
}
static double freqOf(const std::vector<float>& x, size_t a, size_t b)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * kSr / (last - first) : 0.0;
}
// the strongest frequency between lo and hi Hz (a plain DFT scan, 1 Hz steps)
static double peakHz(const std::vector<float>& x, size_t a, size_t b, double lo, double hi)
{
    double best = 0, bf = 0;
    for (double f = lo; f < hi; f += 1.0)
    {
        double re = 0, im = 0; const double w = kTwoPi * f / kSr;
        for (size_t i = a; i < b; ++i) { re += x[i] * std::cos(w * i); im += x[i] * std::sin(w * i); }
        if (re * re + im * im > best) { best = re * re + im * im; bf = f; }
    }
    return bf;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b && i < x.size(); ++i) e += (double)x[i] * x[i]; return std::sqrt(e / std::max<size_t>(1, b - a)); }

int main()
{
    printf("== every pad sounds and decays\n");
    for (int p = 0; p < kPads; ++p)
    {
        Settings s = defaults(); s.tankLevel = 0.0;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        const auto L = run(e, (int)(kSr * 6), { { 0, 0x90, (uint8_t)kPadInfo[p].note, 110 } });
        const double a = rms(L, 0, 4800), z = rms(L, (size_t)(kSr * 5.5), (size_t)(kSr * 6));
        float pk = 0; bool fin = true; for (float x : L) { pk = std::max(pk, std::fabs(x)); fin = fin && std::isfinite(x); }
        CHECK(fin && a > 1e-3 && z < a * 1e-3 && pk < 2.0f, "%-10s first 100 ms %.1f dB, after 5.5 s %.1f dB, peak %.2f", kPadInfo[p].name, 20 * std::log10(a), 20 * std::log10(z + 1e-12), pk);
    }
    printf("== pitch\n");
    for (double blend : { 1.0, 0.0 })
    {
        Settings s = defaults(); s.tankLevel = 0.0;
        PadSettings& t = s.pad[3]; t.blend = blend; t.harmonic = 1.0; t.bend = 0; t.noise = 0; t.click = 0; t.spread = 0; t.pitch = 57; t.decay = 2.0; t.wave = kSine;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        const auto L = run(e, (int)kSr, { { 0, 0x90, 43, 100 } });
        const double f = peakHz(L, 9600, 30000, 150, 330), c = 1200 * std::log2(f / 220.0);
        CHECK(std::fabs(c) < 8.0, "%s pad at A3: %.2f Hz (%+.1f cents)", blend > 0.5 ? "spring" : "analog", f, c);
    }
    {
        Settings s = defaults(); s.tankLevel = 0.0;
        PadSettings& t = s.pad[3]; t.blend = 0.0; t.bend = 24; t.bendTime = 0.03; t.noise = 0; t.click = 0; t.spread = 0; t.pitch = 45; t.decay = 2.0; t.wave = kSine;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        const auto L = run(e, (int)kSr, { { 0, 0x90, 43, 100 } });
        const double early = freqOf(L, 0, 480), late = freqOf(L, 24000, 48000);
        CHECK(early > 2.5 * late && std::fabs(late - 110) < 1.5, "BEND 24 over 30 ms: %.0f Hz at the start, %.1f Hz later", early, late);
        // the spring layer keeps its pitch through a bend (retuning a ringing spring smears it)
        s.pad[3].blend = 1.0; s.pad[3].harmonic = 1.0;
        Engine g; g.setSettings(s); g.prepare(kSr, 256);
        const auto M = run(g, (int)kSr, { { 0, 0x90, 43, 100 } });
        const double sl = peakHz(M, 4800, 30000, 60, 200);
        CHECK(std::fabs(1200 * std::log2(sl / 110.0)) < 10.0, "a bent pad's spring rings at its pitch: %.1f Hz", sl);
    }
    printf("== hits, choke, notes, tank\n");
    {
        Settings s = defaults(); s.tankLevel = 0.0;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        const auto L = run(e, (int)(kSr * 0.2), { { 0, 0x90, 39, 120 } });
        // an onset: a 1 ms window clearly louder than the 3 ms before it
        int onsets = 0; size_t last = 0;
        for (size_t i = 144; i < (size_t)(0.045 * kSr); i += 48)
            if (rms(L, i, i + 48) > 1.6 * rms(L, i - 144, i) && rms(L, i, i + 48) > 1e-3 && (onsets == 0 || i - last > 240)) { ++onsets; last = i; }
        if (rms(L, 0, 48) > 1e-3) ++onsets;
        CHECK(onsets >= 3, "the clap (HITS 4): %d onsets in its first 44 ms", onsets);
        Engine o; o.setSettings(s); o.prepare(kSr, 256);
        const auto open = run(o, (int)(kSr * 0.6), { { 0, 0x90, 46, 110 } });
        Engine c; c.setSettings(s); c.prepare(kSr, 256);
        const auto choked = run(c, (int)(kSr * 0.6), { { 0, 0x90, 46, 110 }, { 4800, 0x90, 42, 1 } });
        const double ro = rms(open, 14400, 28800), rc = rms(choked, 14400, 28800);
        CHECK(rc < ro * 0.2, "the closed hat chokes the open one: %.1f dB under it", 20 * std::log10(rc / ro));
        bool all = true;
        for (int note : { 35, 36, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 57 })
        {
            Engine x; x.setSettings(s); x.prepare(kSr, 256);
            const auto y = run(x, 9600, { { 0, 0x90, (uint8_t)note, 100 } });
            if (rms(y, 0, 4800) < 1e-4) { all = false; printf("    note %d silent\n", note); }
        }
        CHECK(all, "the GM drum notes 35-51 and 57 each play a pad");
        Settings t = s; t.tankLevel = 0.8; t.pad[1].send = 1.0;
        Engine w; w.setSettings(t); w.prepare(kSr, 256);
        Engine d; d.setSettings(s); d.prepare(kSr, 256);
        const auto wet = run(w, (int)(kSr * 2), { { 0, 0x90, 38, 110 } }), dry = run(d, (int)(kSr * 2), { { 0, 0x90, 38, 110 } });
        CHECK(rms(wet, 48000, 72000) > 4.0 * rms(dry, 48000, 72000), "the tank rings on after the snare (%.1f dB more at 1 s)", 20 * std::log10(rms(wet, 48000, 72000) / (rms(dry, 48000, 72000) + 1e-12)));
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(5); std::uniform_real_distribution<double> u(0, 1);
        bool fin = true; float pk = 0;
        for (int t = 0; t < 30; ++t)
        {
            Settings s;
            for (auto& o : s.pad) { o.tank = (int)(u(rng) * 6); o.wave = (int)(u(rng) * kNumWaves); o.blend = u(rng); o.pitch = 12 + u(rng) * 96; o.decay = 0.01 + u(rng) * 3;
                o.harmonic = u(rng); o.spread = u(rng); o.drip = u(rng) * 100; o.bend = u(rng) * 48; o.bendTime = 0.002 + u(rng) * 0.5; o.noise = u(rng); o.noiseTone = u(rng);
                o.click = u(rng); o.hits = 1 + (int)(u(rng) * 4); o.level = u(rng) * 12 - 12; o.pan = u(rng) * 2 - 1; o.send = u(rng); }
            s.tankModel = (int)(u(rng) * 6); s.tankDecay = 0.3 + u(rng) * 7; s.tankLevel = u(rng); s.velocity = u(rng); s.drive = u(rng); s.choke = u(rng) < 0.5;
            Engine e; e.setSettings(s); e.prepare(t % 2 ? 44100.0 : 96000.0, 256);
            std::vector<synth::Midi> ev;
            for (int k = 0; k < 24; ++k) ev.push_back({ (uint32_t)(k * 2000), 0x90, (uint8_t)(35 + (int)(u(rng) * 23)), (uint8_t)(1 + (int)(u(rng) * 126)) });
            const auto L = run(e, 60000, ev);
            for (float x : L) { fin = fin && std::isfinite(x); pk = std::max(pk, std::fabs(x)); }
        }
        CHECK(fin && pk < 8.0f, "random kits and hits at 44.1 / 96 kHz: finite and bounded (peak %.2f)", pk);
    }
    printf("== cpu\n");
    {
        Settings s = defaults();
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        std::vector<synth::Midi> ev;
        for (int k = 0; k < 64; ++k) ev.push_back({ (uint32_t)(k * 3000), 0x90, (uint8_t)kPadInfo[k % kPads].note, 110 });
        const auto t0 = std::chrono::steady_clock::now();
        run(e, (int)(kSr * 4), ev);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  cpu: all eight pads hit every 62 ms, with the tank: %.1f %% of one core\n", 100.0 * secs / 4.0);
    }
    printf("== notes\n");
    {
        // the pads in order on the white keys up from C4 (60); the General MIDI drum notes still play the same pads
        bool order = true;
        for (int p = 0; p < kPads; ++p) order = order && kPadInfo[p].note == whiteKeyNote(p);
        CHECK(order, "pads on C4 D4 E4 F4 G4 A4 B4 C5 (the white keys from 60)");
        static const int gm[kPads] = { 36, 38, 39, 43, 48, 42, 46, 49 };
        bool allNew = true, allGm = true;
        for (int p = 0; p < kPads; ++p)
            for (int pass = 0; pass < 2; ++pass)
            {
                Settings s = defaults();
                Engine e; e.setSettings(s); e.prepare(kSr, 256);
                run(e, 512, { { 0, 0x90, (uint8_t)(pass ? gm[p] : kPadInfo[p].note), 110 } });
                bool only = e.padActive(p);
                for (int q = 0; q < kPads; ++q) if (q != p && e.padActive(q)) only = false;
                (pass ? allGm : allNew) &= only;
            }
        CHECK(allNew, "each new note plays its pad, and only that pad");
        CHECK(allGm, "each General MIDI drum note (older patterns) still plays the same pad");
    }
    std::printf(fails ? "test_boing: %d FAILED\n" : "test_boing: all passed\n", fails);
    return fails ? 1 : 0;
}
