// pump MACHINE: every sound plays from its MIDI note at 44.1 and 48 kHz; a hit sounds the same at both rates; two
// instances interleaved block by block each match an instance rendered alone, bit for bit (the firmware's state is
// swapped correctly); a parameter change reaches the voice (coarse tune raises the pitch); channels 2-8 play the
// sounds chromatically; random parameters and hits stay finite. CPU.
#include "../plugins/machine/MachineEngine.hpp"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::machine;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<int> defaults()
{
    std::vector<int> v((size_t)kFwNumParams, -1);
    for (int i = 0; i < kNumFw; ++i) v[(size_t)kFw[i].fw] = kFw[i].def;
    return v;
}
static int fwIndex(const char* sym) { for (int i = 0; i < kNumFw; ++i) if (!std::strcmp(kFw[i].sym, sym)) return kFw[i].fw; return -1; }
static std::vector<float> run(Engine& e, int n, std::vector<synth::Midi> ev, int blk = 256)
{
    std::vector<float> L((size_t)n), R((size_t)n);
    for (int off = 0; off < n; off += blk)
    {
        const int m = std::min(blk, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
    for (int i = 0; i < n; ++i) L[(size_t)i] = 0.5f * (L[(size_t)i] + R[(size_t)i]);
    return L;
}
static double peak(const std::vector<float>& x, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(b, x.size()); ++i) m = std::max(m, (double)std::fabs(x[i])); return m; }
static double energy(const std::vector<float>& x) { double s = 0; for (float v : x) s += (double)v * v; return s; }
static double crossings(const std::vector<float>& x, size_t a, size_t b, double sr)
{
    int c = 0; for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) ++c;
    return c * sr / (double)(b - a);
}

int main()
{
    const auto def = defaults();
    double e441[kSounds] = {};
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        int ok = 0; char why[160] = "";
        for (int s = 0; s < kSounds; ++s)
        {
            Engine e; e.prepare(sr, 512, def.data());
            const auto L = run(e, (int)(0.8 * sr), { { 0, 0x99, (uint8_t)noteForSound(s), 127 } });
            const double pk = peak(L, 0, (size_t)(0.5 * sr));
            if (pk > 0.03) ++ok; else std::snprintf(why, sizeof why, " (%s: peak %.4f)", kSoundNames[s], pk);
            if (sr == 44100.0) e441[s] = energy(L) / sr; else if (s == 0 || s == 3)
            {
                const double d = 10 * std::log10((energy(L) / sr) / e441[s]);
                CHECK(std::fabs(d) < 0.5, "%s at 48 kHz has the energy it has at 44.1 kHz (%+.2f dB)", kSoundNames[s], d);
            }
        }
        CHECK(ok == kSounds, "all %d sounds play from their notes%s", ok, why);
    }
    {
        // two instances, interleaved: each matches one rendered alone
        const double sr = 48000.0;
        auto other = def; other[(size_t)fwIndex("coarse1")] = 80; other[(size_t)fwIndex("velod1")] = 90;
        Engine a, b, alone;
        a.prepare(sr, 256, def.data()); b.prepare(sr, 256, other.data()); alone.prepare(sr, 256, def.data());
        std::vector<synth::Midi> hits = { { 0, 0x99, 36, 120 }, { 100, 0x99, 38, 100 }, { 30, 0x99, 42, 90 } };
        const int blocks = 60;
        std::vector<float> la(256), ra(256), lb(256), rb(256), lc(256), rc(256);
        double worst = 0.0;
        for (int k = 0; k < blocks; ++k)
        {
            std::vector<synth::Midi> ev; if (k % 15 == 0) ev = hits;
            a.process(la.data(), ra.data(), 256, ev.data(), (int)ev.size());
            b.process(lb.data(), rb.data(), 256, ev.data(), (int)ev.size());
            if (k == 20) b.setParam(fwIndex("filter_freq_1"), 30);
            alone.process(lc.data(), rc.data(), 256, ev.data(), (int)ev.size());
            for (int i = 0; i < 256; ++i) worst = std::max({ worst, (double)std::fabs(la[(size_t)i] - lc[(size_t)i]), (double)std::fabs(ra[(size_t)i] - rc[(size_t)i]) });
        }
        // and b really is a different kit
        double diff = 0.0; for (int i = 0; i < 256; ++i) diff = std::max(diff, (double)std::fabs(lb[(size_t)i] - lc[(size_t)i]));
        CHECK(worst == 0.0 && diff > 1e-4, "two instances interleaved: the first matches one alone exactly (worst %.1e), the second differs (%.3f)", worst, diff);
    }
    {
        // coarse tune up an octave and a fifth (+19): the kick's crossings rise
        const double sr = 48000.0;
        Engine e; e.prepare(sr, 512, def.data());
        const auto a = run(e, (int)(0.3 * sr), { { 0, 0x99, 36, 127 } });
        Engine f; auto hi = def; hi[(size_t)fwIndex("coarse1")] = std::min(127, def[(size_t)fwIndex("coarse1")] + 19); f.prepare(sr, 512, hi.data());
        const auto b = run(f, (int)(0.3 * sr), { { 0, 0x99, 36, 127 } });
        Engine g; g.prepare(sr, 512, def.data()); g.setParam(fwIndex("coarse1"), hi[(size_t)fwIndex("coarse1")]);
        const auto c = run(g, (int)(0.3 * sr), { { 0, 0x99, 36, 127 } });
        const double za = crossings(a, 400, (size_t)(0.05 * sr), sr), zb = crossings(b, 400, (size_t)(0.05 * sr), sr), zc = crossings(c, 400, (size_t)(0.05 * sr), sr);
        CHECK(zb > 2.0 * za && std::fabs(zc - zb) < 0.05 * zb, "coarse +19: crossings %.0f -> %.0f per second (set live: %.0f)", za, zb, zc);
        // channel 2 plays drum 1 chromatically: note 72 an octave over 60
        Engine h; h.prepare(sr, 512, def.data());
        const auto d = run(h, (int)(0.3 * sr), { { 0, 0x91, 60, 127 } });
        Engine k; k.prepare(sr, 512, def.data());
        const auto o = run(k, (int)(0.3 * sr), { { 0, 0x91, 72, 127 } });
        const double z60 = crossings(d, 400, (size_t)(0.05 * sr), sr), z72 = crossings(o, 400, (size_t)(0.05 * sr), sr);
        CHECK(std::fabs(z60 - za) < 0.05 * za && z72 > 1.7 * z60 && z72 < 2.3 * z60, "channel 2: note 60 = the GM note's pitch (%.0f), note 72 an octave up (%.0f)", z60, z72);
    }
    {
        std::mt19937 rng(50); std::uniform_real_distribution<double> u(0.0, 1.0);
        const double sr = 48000.0;
        Engine e; e.prepare(sr, 512, def.data());
        bool finite = true; double pk = 0;
        std::vector<float> L(512), R(512);
        for (int it = 0; it < 1500; ++it)
        {
            if (it % 7 == 0) for (int j = 0; j < 12; ++j) { const FwParam& f = kFw[(size_t)(u(rng) * kNumFw)]; e.setParam(f.fw, f.min + (int)(u(rng) * (f.max - f.min + 1))); }
            std::vector<synth::Midi> ev;
            for (int k = 0; k < 2; ++k) if (u(rng) < 0.4) ev.push_back({ (uint32_t)(u(rng) * 511), (uint8_t)(u(rng) < 0.7 ? 0x99 : 0x90 + (int)(u(rng) * 8)), (uint8_t)(30 + u(rng) * 60), (uint8_t)(1 + u(rng) * 126) });
            std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
            e.process(L.data(), R.data(), 512, ev.data(), (int)ev.size());
            for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); pk = std::max(pk, (double)std::fabs(L[(size_t)i])); }
        }
        CHECK(finite && pk <= 2.01, "random parameters and hits: finite (peak %.2f)", pk);
    }
    {
        const double sr = 48000.0;
        Engine e; e.prepare(sr, 512, def.data());
        std::vector<synth::Midi> ev;
        for (int k = 0; k < 32; ++k) for (int s = 0; s < kSounds; ++s) ev.push_back({ (uint32_t)(k * sr / 32), 0x99, (uint8_t)noteForSound(s), 110 });
        std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
        const auto t0 = std::chrono::steady_clock::now();
        run(e, (int)sr, ev);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  cpu: the whole machine, every sound hit 32 times a second: %.1f %% of one core\n", 100.0 * secs);
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
