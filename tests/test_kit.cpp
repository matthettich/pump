// pump KIT: the kick's pitch drops to its tuned note; every model on every pad sounds and ends near its decay; the
// closed hat chokes the open hat; a fast re-hit fades the old voice (no clicks, never more than two per pad);
// velocity sensitivity sets the level; the hat sits up high; the GM notes reach their pads; random settings and
// MIDI stay finite. CPU.
#include "../plugins/kit/KitEngine.hpp"
#include "dsp/FFT.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::kit;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings defaults()
{
    Settings s;
    for (int p = 0; p < kPads; ++p)
    {
        const PadInfo& d = kPadInfo[p];
        PadSettings& q = s.pad[p];
        q.model = d.model; q.tune = d.tune; q.decay = d.decay * 0.001; q.pitch = d.pitch; q.pdecay = d.pdecay * 0.001; q.tone = d.tone * 0.01; q.noise = d.noise * 0.01;
        q.click = d.click * 0.01; q.chr = d.chr * 0.01; q.drive = d.drive * 0.01; q.levelDb = d.level; q.pan = d.pan * 0.01;
    }
    s.velocity = 0.6; s.humanize = 0.0;
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
static double freqOf(const std::vector<float>& x, size_t a, size_t b, double sr)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * sr / (last - first) : 0.0;
}
static double peakIn(const std::vector<float>& x, size_t a, size_t b) { double m = 0; for (size_t i = a; i < std::min(b, x.size()); ++i) m = std::max(m, (double)std::fabs(x[i])); return m; }
static double rmsIn(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }

int main()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)sr;
        {
            Engine e; Settings s = defaults(); s.pad[0].click = 0; s.pad[0].chr = 0; s.pad[0].noise = 0; s.pad[0].tone = 1.0; s.pad[0].decay = 1.5; s.pad[0].drive = 0; e.setSettings(s); e.prepare(sr, 512);
            const auto L = run(e, n, { { 0, 0x99, 36, 127 } });
            const double early = freqOf(L, 10, (size_t)(0.012 * sr), sr), late = freqOf(L, (size_t)(0.4 * sr), (size_t)(0.8 * sr), sr);
            CHECK(early > 150 && std::fabs(late - 50.0) < 0.5, "kick: %.0f Hz at the start, %.2f Hz once the pitch has dropped (50)", early, late);
        }
        {
            // every model on the snare pad: it sounds, and is silent (and its voice free) a little after its decay
            int ok = 0; char why[256] = "";
            for (int m = 0; m < kNumModels; ++m)
            {
                Engine e; Settings s = defaults(); s.pad[1].model = m; s.pad[1].decay = 0.3; e.setSettings(s); e.prepare(sr, 512);
                const auto L = run(e, (int)(1.5 * sr), { { 0, 0x99, 38, 127 } });
                const double pk = peakIn(L, 0, (size_t)(0.1 * sr)), tail = peakIn(L, (size_t)(1.2 * sr), (size_t)(1.5 * sr));
                if (pk > 0.05 && tail < 1e-4 && e.voicesNow() == 0) ++ok; else std::snprintf(why, sizeof why, " (%s: peak %.3f, tail %.5f, voices %d)", kModelNames[m], pk, tail, e.voicesNow());
            }
            CHECK(ok == kNumModels, "all %d models sound and end%s", ok, why);
        }
        {
            Engine e; Settings s = defaults(); e.setSettings(s); e.prepare(sr, 512);
            const int at = (int)(0.1 * sr);
            const auto L = run(e, (int)(0.4 * sr), { { 0, 0x99, 46, 127 }, { (uint32_t)at, 0x99, 42, 1 } });
            Engine f; f.setSettings(s); f.prepare(sr, 512);
            const auto M = run(f, (int)(0.4 * sr), { { 0, 0x99, 46, 127 } });
            const double open = rmsIn(M, (size_t)(at + 0.15 * sr), (size_t)(at + 0.25 * sr)), choked = rmsIn(L, (size_t)(at + 0.15 * sr), (size_t)(at + 0.25 * sr));
            CHECK(choked < open * 0.05, "the closed hat chokes the open hat: %.1f dB", 20 * std::log10(choked / open + 1e-12));
        }
        {
            // a re-hit every 5 ms on a long kick: at most two voices, and no sample jumps larger than the waveform's own
            Engine e; Settings s = defaults(); s.pad[0].click = 0; s.pad[0].decay = 2.0; e.setSettings(s); e.prepare(sr, 512);
            std::vector<synth::Midi> ev; for (int k = 0; k < 40; ++k) ev.push_back({ (uint32_t)(k * 0.005 * sr), 0x99, 36, 127 });
            std::vector<float> L((size_t)(0.25 * sr)), R(L.size());
            int most = 0; double jump = 0;
            for (int off = 0; off < (int)L.size(); off += 64)
            {
                const int m = std::min(64, (int)L.size() - off);
                std::vector<synth::Midi> here; for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
                e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
                most = std::max(most, e.voicesNow());
            }
            for (size_t i = 1; i < L.size(); ++i) jump = std::max(jump, (double)std::fabs(L[i] - L[i - 1]));
            CHECK(most <= 2 && jump < 0.2, "re-hits every 5 ms: at most %d voices, largest step %.3f", most, jump);
        }
        {
            Engine e; Settings s = defaults(); s.velocity = 1.0; s.pad[0].drive = 0; e.setSettings(s); e.prepare(sr, 512);
            const auto a = run(e, 4096, { { 0, 0x99, 36, 127 } });
            Engine f; f.setSettings(s); f.prepare(sr, 512);
            const auto b = run(f, 4096, { { 0, 0x99, 36, 64 } });
            const double r = 20 * std::log10(peakIn(b, 0, 4096) / peakIn(a, 0, 4096));
            Engine g; s.velocity = 0.0; g.setSettings(s); g.prepare(sr, 512);
            const auto c = run(g, 4096, { { 0, 0x99, 36, 20 } });
            const double r0 = 20 * std::log10(peakIn(c, 0, 4096) / peakIn(a, 0, 4096));
            CHECK(r < -4.0 && r > -14.0 && std::fabs(r0) < 3.0, "velocity 64 at full sensitivity %.1f dB; velocity 20 with none %.1f dB", r, r0);
        }
        {
            // the hat: most of its energy above 4 kHz
            const int N = 8192;
            Engine e; Settings s = defaults(); s.pad[3].decay = 0.5; s.pad[3].click = 0; e.setSettings(s); e.prepare(sr, 512);
            const auto L = run(e, N, { { 0, 0x99, 42, 127 } });
            RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
            for (int i = 0; i < N; ++i) in[(size_t)i] = L[(size_t)i];
            fft.forward(in.data(), out.data());
            double hi = 0, all = 0;
            for (int k = 1; k < N / 2; ++k) { const double p = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)]; all += p; if (k * sr / N > 4000) hi += p; }
            CHECK(hi / all > 0.85, "hat: %.0f %% of its energy above 4 kHz", 100 * hi / all);
        }
        {
            int ok = 0;
            for (int p = 0; p < kPads; ++p) { double off; if (padForNote(noteForPad(p), off) == p && off == 0.0) ++ok; }
            double o1, o2; const int t1 = padForNote(41, o1), t2 = padForNote(50, o2);
            CHECK(ok == kPads && t1 == 5 && t2 == 5 && o1 < 0 && o2 > 0, "every pad's own note reaches it; low and high toms play the tom pad lower and higher");
        }
        if (sr != 48000.0) continue;
        {
            std::mt19937 rng(49); std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s = defaults(); e.setSettings(s); e.prepare(sr, 512);
            bool finite = true; double peak = 0;
            std::vector<float> L(512), R(512);
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 13 == 0)
                {
                    for (auto& q : s.pad)
                    {
                        q.model = (int)(u(rng) * kNumModels); q.tune = u(rng) * 48 - 24; q.decay = 0.01 + 4 * u(rng) * u(rng); q.pitch = 48 * u(rng); q.pdecay = 0.002 + u(rng);
                        q.tone = u(rng); q.noise = u(rng); q.click = u(rng); q.chr = u(rng); q.drive = u(rng); q.levelDb = u(rng) * 54 - 48; q.pan = u(rng) * 2 - 1;
                    }
                    s.kitDrive = u(rng); s.velocity = u(rng); s.humanize = u(rng);
                    e.setSettings(s);
                }
                std::vector<synth::Midi> ev;
                for (int k = 0; k < 3; ++k) if (u(rng) < 0.4) ev.push_back({ (uint32_t)(u(rng) * 511), 0x99, (uint8_t)(35 + u(rng) * 25), (uint8_t)(1 + u(rng) * 126) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                e.process(L.data(), R.data(), 512, ev.data(), (int)ev.size());
                for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            CHECK(finite && peak < 8.0, "random settings and hits: finite (peak %.2f)", peak);
        }
        {
            Engine e; Settings s = defaults(); e.setSettings(s); e.prepare(sr, 512);
            std::vector<synth::Midi> ev;
            for (int k = 0; k < 32; ++k) for (int p = 0; p < kPads; ++p) ev.push_back({ (uint32_t)(k * sr / 32), 0x99, (uint8_t)noteForPad(p), 110 });
            std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
            const auto t0 = std::chrono::steady_clock::now();
            run(e, n, ev);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: all eight pads hit 32 times a second: %.1f %% of one core\n", 100.0 * secs);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
