// pump COIL / TWANG engine: every spring model plays in tune (the lowest partial at the note, within a few cents, at
// HARMONIC 0 and 100), low and high notes alike; HARMONIC 0 stretches the partials (a spring) and 100 lines them up (a
// string); a released note dies faster; eight voices play at once; TANK adds SPRING's own tail; the resonator rings at
// its chord's notes from noise; random settings and MIDI stay finite. 44.1 and 48 kHz. CPU.
#include "../plugins/coil/CoilEngine.hpp"
#include "dsp/FFT.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::coil;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> run(Engine& e, int n, std::vector<synth::Midi> ev, const std::vector<float>* in = nullptr)
{
    std::vector<float> L((size_t)n), R((size_t)n);
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        if (in) e.process(in->data() + off, in->data() + off, L.data() + off, R.data() + off, m, here.data(), (int)here.size());
        else e.process(nullptr, nullptr, L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
    for (int i = 0; i < n; ++i) L[(size_t)i] = 0.5f * (L[(size_t)i] + R[(size_t)i]);
    return L;
}
struct Spectrum { std::vector<double> p; double binHz; };
static Spectrum spectrum(const std::vector<float>& x, size_t at, int N, double sr)
{
    RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
    for (int i = 0; i < N; ++i) { const double t = kTwoPi * i / N; in[(size_t)i] = (float)(x[at + (size_t)i] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t))); }
    fft.forward(in.data(), out.data());
    Spectrum s; s.binHz = sr / N; s.p.resize((size_t)N / 2);
    for (int k = 1; k < N / 2; ++k) s.p[(size_t)k] = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
    return s;
}
// the strongest peak between lo and hi Hz (parabolic interpolation), and its power
static double peakHz(const Spectrum& s, double lo, double hi, double* power = nullptr)
{
    int best = -1;
    for (int k = std::max(2, (int)(lo / s.binHz)); k < std::min((int)s.p.size() - 2, (int)(hi / s.binHz) + 1); ++k) if (best < 0 || s.p[(size_t)k] > s.p[(size_t)best]) best = k;
    if (best < 0) return 0.0;
    const double a = std::log(s.p[(size_t)best - 1] + 1e-30), b = std::log(s.p[(size_t)best] + 1e-30), c = std::log(s.p[(size_t)best + 1] + 1e-30);
    const double d = 0.5 * (a - c) / (a - 2 * b + c);
    if (power) *power = s.p[(size_t)best];
    return (best + d) * s.binHz;
}
static double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }

int main()
{
    for (double sr : { 44100.0, 48000.0, 192000.0 })   // 192 kHz: the dispersion history reaches past 16 samples
    {
        printf("== %.0f Hz\n", sr);
        const int N = 32768;
        // KICK with nothing ringing (no note yet, TANK 0): it strikes C3, then the last note played
        {
            Engine e; Settings s; s.wobble = 0.0; s.spread = 0.0; e.setSettings(s); e.prepare(sr, 512);
            run(e, 4096, {});
            e.kick(1.0);
            const auto L = run(e, N + 8192, {});
            double pk = 0.0; for (float v : L) pk = std::max(pk, (double)std::fabs(v));
            const auto sp = spectrum(L, 4096, N, sr);
            const double f0 = synth::midiToHz(48), f = peakHz(sp, f0 / 1.12, f0 * 1.12);
            CHECK(pk > 0.01 && std::fabs(cents(f, f0)) < 12.0, "KICK with nothing ringing strikes C3 (peak %.3f, %.1f Hz)", pk, f);
            run(e, 2048, { { 0, 0x90, 57, 100 }, { 1000, 0x80, 57, 0 } });
            run(e, (int)(sr * 12), {});   // rung out
            e.kick(1.0);
            const auto M = run(e, N + 8192, {});
            const auto sq = spectrum(M, 4096, N, sr);
            const double g0 = synth::midiToHz(57), g = peakHz(sq, g0 / 1.12, g0 * 1.12);
            CHECK(std::fabs(cents(g, g0)) < 12.0, "then the last note played (A3: %.1f Hz)", g);
        }
        // tuning: every model, A2 A3 A4 A5 C7, HARMONIC 0 and 100
        for (double h : { 0.0, 1.0 })
        {
            double worst = 0.0; char where[96] = "";
            for (int m = 0; m < 6; ++m)
                for (int note : { 45, 57, 69, 81, 96 })
                {
                    Engine e; Settings s; s.model = m; s.harmonic = h; s.wobble = 0.0; s.decay = 4.0; s.spread = 0.0; e.setSettings(s); e.prepare(sr, 512);
                    const auto L = run(e, N + 8192, { { 0, 0x90, (uint8_t)note, 110 } });
                    const auto sp = spectrum(L, 8192, N, sr);
                    const double f0 = synth::midiToHz(note), f = peakHz(sp, f0 / 1.12, f0 * 1.12);
                    const double c = std::fabs(cents(f, f0));
                    if (c > worst) { worst = c; std::snprintf(where, sizeof where, "%s note %d: %.2f Hz for %.2f", kModelNames[m], note, f, f0); }
                }
            CHECK(worst < (h > 0.5 ? 6.0 : 12.0), "HARMONIC %.0f: every model in tune from A2 to C7, worst %.1f cents (%s)", h * 100, worst, where);
        }
        {
            // HARMONIC 0: the upper partials (toward the transition) bend away from the harmonic series; 100: on it
            double dev[2] = {};
            for (int k = 0; k < 2; ++k)
            {
                Engine e; Settings s; s.model = 0; s.harmonic = k ? 1.0 : 0.0; s.wobble = 0.0; s.decay = 4.0; s.spread = 0.0; e.setSettings(s); e.prepare(sr, 512);
                const auto L = run(e, N + 8192, { { 0, 0x90, 45, 110 } });
                const auto sp = spectrum(L, 8192, N, sr);
                const double f0 = peakHz(sp, 100, 120);
                double sum = 0.0; int n = 0;
                for (int p = 8; p <= 20; ++p) { sum += std::fabs(cents(peakHz(sp, p * f0 * 0.985, p * f0 * 1.015), p * f0)); ++n; }
                dev[k] = sum / n;
            }
            CHECK(dev[0] > 10.0 && dev[1] < 6.0, "A2's partials 8-20 (0.9-2.2 kHz): %.0f cents off the harmonic series on average at HARMONIC 0 (a spring), %.1f at 100 (a string)", dev[0], dev[1]);
        }
        {
            Engine e; Settings s; s.decay = 4.0; s.release = 0.2; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(1.5 * sr);
            const auto held = run(e, n, { { 0, 0x90, 57, 110 } });
            Engine f; f.setSettings(s); f.prepare(sr, 512);
            const auto rel = run(f, n, { { 0, 0x90, 57, 110 }, { (uint32_t)(0.3 * sr), 0x80, 57, 0 } });
            const double a = rms(held, (size_t)(1.0 * sr), (size_t)n), b = rms(rel, (size_t)(1.0 * sr), (size_t)n);
            CHECK(b < a * 0.01, "note off: release 0.2 s takes it %.0f dB under the held note within a second", 20 * std::log10(b / a + 1e-12));
        }
        {
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
            std::vector<synth::Midi> ev; for (int k = 0; k < 8; ++k) ev.push_back({ 0, 0x90, (uint8_t)(48 + 3 * k), 100 });
            run(e, 4096, ev);
            CHECK(e.voicesNow() == 8, "an 8-note chord: %d voices", e.voicesNow());
        }
        {
            // TANK: SPRING's own tail rings on after the tuned springs (short decay) are gone
            Engine e; Settings s; s.decay = 0.3; s.release = 0.3; e.setSettings(s); e.prepare(sr, 512);
            const int n = (int)(2.0 * sr);
            const auto dry = run(e, n, { { 0, 0x90, 57, 110 }, { 2000, 0x80, 57, 0 } });
            Engine f; s.tank = 0.8; f.setSettings(s); f.prepare(sr, 512);
            const auto wet = run(f, n, { { 0, 0x90, 57, 110 }, { 2000, 0x80, 57, 0 } });
            const double a = rms(dry, (size_t)(0.8 * sr), (size_t)(1.2 * sr)), b = rms(wet, (size_t)(0.8 * sr), (size_t)(1.2 * sr));
            CHECK(b > 10.0 * a + 1e-5, "TANK 80 %%: the tank's tail at 1 s is %.0f dB over the tuned springs alone", 20 * std::log10((b + 1e-12) / (a + 1e-12)));
        }
        {
            // TWANG: noise in, an A fifth (A3 + E4): the output's peaks at 220 and 330 Hz
            Engine e; Settings s; s.resonator = true; s.tuning = kTuneChord; s.root = 9; s.chord = kChordFifth; s.octave = 0; s.mix = 1.0; s.harmonic = 0.6; s.decay = 3.0; s.wobble = 0.0; s.spread = 0.0;
            e.setSettings(s); e.prepare(sr, 512);
            std::mt19937 rng(3); std::uniform_real_distribution<float> u(-0.3f, 0.3f);
            const int Nt = N * std::max(1, (int)(sr / 48000.0));   // the same frequency resolution at every rate
            std::vector<float> in((size_t)(Nt + 16384)); for (auto& x : in) x = u(rng);
            const auto L = run(e, (int)in.size(), {}, &in);
            const auto sp = spectrum(L, 16384, Nt, sr);
            const double fa = peakHz(sp, 200, 240), fe = peakHz(sp, 310, 350);
            double pa = 0, pn = 0; peakHz(sp, 215, 225, &pa); peakHz(sp, 260, 280, &pn);
            CHECK(std::fabs(cents(fa, 220.0)) < 12 && std::fabs(cents(fe, synth::midiToHz(64))) < 12 && pa > 30 * pn, "TWANG on noise, A fifth: peaks at %.1f and %.1f Hz, %.0f dB over the gap between", fa, fe, 10 * std::log10(pa / (pn + 1e-30)));
            CHECK(e.voicesNow() == 2, "the fifth rings on %d voices", e.voicesNow());
        }
        if (sr != 48000.0) continue;
        {
            std::mt19937 rng(51); std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
            bool finite = true; double peak = 0;
            std::vector<float> in(512), L(512), R(512);
            for (int it = 0; it < 2000; ++it)
            {
                if (it % 13 == 0)
                {
                    s.model = (int)(u(rng) * 6); s.tension = u(rng) * 200 - 100; s.drip = 100 * u(rng); s.decay = 0.1 + 15 * u(rng) * u(rng); s.release = 0.02 + 4 * u(rng);
                    s.harmonic = u(rng); s.spread = u(rng); s.wobble = u(rng); s.tone = u(rng) * 2 - 1; s.excite = u(rng); s.tank = u(rng) < 0.3 ? u(rng) : 0.0; s.grit = 100 * u(rng);
                    s.width = u(rng); s.octave = (int)(u(rng) * 5) - 2; s.resonator = u(rng) < 0.3; s.tuning = (int)(u(rng) * 3); s.root = (int)(u(rng) * 12); s.chord = (int)(u(rng) * kNumChords); s.mix = u(rng);
                    e.setSettings(s);
                }
                for (auto& x : in) x = (float)(0.4 * (u(rng) * 2 - 1));
                std::vector<synth::Midi> ev;
                if (u(rng) < 0.3) ev.push_back({ (uint32_t)(u(rng) * 511), (uint8_t)(u(rng) < 0.6 ? 0x90 : 0x80), (uint8_t)(24 + u(rng) * 84), (uint8_t)(1 + u(rng) * 126) });
                if (u(rng) < 0.05) ev.push_back({ 300, 0xE0, (uint8_t)(u(rng) * 127), (uint8_t)(u(rng) * 127) });
                if (u(rng) < 0.03) e.kick(u(rng) * 1.5);
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                e.process(s.resonator ? in.data() : nullptr, s.resonator ? in.data() : nullptr, L.data(), R.data(), 512, ev.data(), (int)ev.size());
                for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            CHECK(finite && peak < 16.0, "random settings, input and MIDI: finite and bounded (peak %.2f)", peak);
        }
        {
            Engine e; Settings s; s.model = 1; e.setSettings(s); e.prepare(sr, 512);
            std::vector<synth::Midi> ev; for (int k = 0; k < 8; ++k) ev.push_back({ 0, 0x90, (uint8_t)(40 + 4 * k), 100 });
            const auto t0 = std::chrono::steady_clock::now();
            run(e, (int)sr, ev);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: 8 three-spring voices: %.1f %% of one core\n", 100.0 * secs);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
