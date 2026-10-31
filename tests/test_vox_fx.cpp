// pump VOX, VOICE FX: with every section off it is the input delayed by its latency; PITCH moves a voice-like tone
// by the semitones asked (and FORMANT moves its spectral weight, not its pitch); ROBOT's MONO holds a gliding voice
// on NOTE, and on a held MIDI note instead; GRIT's FRY puts energy an octave under the voice; RING makes sidebands;
// random settings stay finite and the latency never moves.
#include "../plugins/vox/VoxFx.hpp"
#include "dsp/FFT.hpp"
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::vox;
using namespace pump::fx;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double sr = 48000.0;

// a voice-like tone: a pulse train through a formant filter (vowel "ah": 700, 1200, 2600 Hz), f0 from fn(t)
template <class F> static std::vector<float> voice(double seconds, F f0, double amp = 0.3)
{
    const int n = (int)(seconds * sr);
    std::vector<float> v((size_t)n);
    double ph = 0.0;
    Svf fm[3]; const double ff[3] = { 700, 1200, 2600 };
    for (int k = 0; k < 3; ++k) fm[k].set(ff[k], 6.0, sr);
    for (int i = 0; i < n; ++i)
    {
        ph += f0(i / sr) / sr;
        double s = 0.0;
        if (ph >= 1.0) { ph -= 1.0; s = 1.0; }
        double y = 0.0;
        for (int k = 0; k < 3; ++k) y += fm[k].bandpass(s) * (k == 0 ? 1.0 : 0.6);
        v[(size_t)i] = (float)(y * amp * 4.0);
    }
    return v;
}
static void run(VoiceFx& fx, const std::vector<float>& in, std::vector<float>& L, std::vector<float>& R)
{
    L.assign(in.size(), 0.0f); R.assign(in.size(), 0.0f);
    for (size_t off = 0; off < in.size(); off += 256)
    {
        const int n = (int)std::min<size_t>(256, in.size() - off);
        fx.process(in.data() + off, L.data() + off, R.data() + off, n);
    }
}
// median pitch (MIDI) of x over [a, b) seconds
static double medianMidi(const std::vector<float>& x, double a, double b)
{
    pitch::Tracker t; t.prepare(sr, 40.0, 1000.0, 24000.0, 0.0053);
    std::vector<double> m;
    for (size_t i = 0; i < x.size(); ++i)
        if (t.push(x[i]) && i > a * sr && i < b * sr && t.result().voiced) m.push_back(pitch::hzToMidi(t.result().hz));
    if (m.empty()) return -1.0;
    std::sort(m.begin(), m.end());
    return m[m.size() / 2];
}
// power (dB) of x in [f0, f1] Hz over the middle of the signal
static double bandDb(const std::vector<float>& x, double f0, double f1)
{
    const int N = 1 << 15;
    RealFFT fft(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    double acc = 0.0; int frames = 0;
    for (size_t s = x.size() / 4; s + N < x.size() * 3 / 4 + N && s + N <= x.size(); s += N / 2, ++frames)
    {
        for (int i = 0; i < N; ++i) in[(size_t)i] = x[s + (size_t)i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        fft.forward(in.data(), out.data());
        for (int k = (int)(f0 * N / sr); k <= (int)(f1 * N / sr); ++k) acc += (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
    }
    return 10.0 * std::log10(acc / std::max(1, frames) + 1e-30);
}
static double centroid(const std::vector<float>& x)
{
    double num = 0.0, den = 0.0;
    for (double f = 200.0; f < 6000.0; f += 100.0) { const double p = std::pow(10.0, bandDb(x, f, f + 100.0) / 10.0); num += p * (f + 50.0); den += p; }
    return num / den;
}

int main()
{
    printf("== pump VOX VOICE FX\n");
    const auto steady = voice(3.0, [](double) { return 150.0; });

    // ---- every section off: the input, delayed
    {
        VoiceFx fx; FxSettings s; s.pitchOn = false; fx.set(s); fx.prepare(sr);
        std::vector<float> L, R; run(fx, steady, L, R);
        double mx = 0.0;
        for (size_t i = (size_t)fx.latency() + 2000; i < steady.size(); ++i) mx = std::max(mx, (double)std::fabs(L[i] - steady[i - (size_t)fx.latency()]) + std::fabs(R[i] - L[i]));
        CHECK(mx < 1e-6 && fx.latency() == VoiceFx::latencyFor(sr), "sections off: the input delayed by %d samples (%.1f ms), diff %.2g", fx.latency(), 1000.0 * fx.latency() / sr, mx);
    }
    // ---- PITCH
    {
        const double m0 = pitch::hzToMidi(150.0);
        for (float st : { -5.0f, -12.0f, 7.0f })
        {
            VoiceFx fx; FxSettings s; s.pitch = st; fx.set(s); fx.prepare(sr);
            std::vector<float> L, R; run(fx, steady, L, R);
            const double m = medianMidi(L, 1.0, 2.8);
            CHECK(std::fabs(m - (m0 + st)) < 0.25, "PITCH %+.0f: %.2f (wanted %.2f)", st, m, m0 + st);
        }
        VoiceFx a, b; FxSettings s; a.set(s); a.prepare(sr); s.formant = -6.0f; b.set(s); b.prepare(sr);
        std::vector<float> La, Ra, Lb, Rb; run(a, steady, La, Ra); run(b, steady, Lb, Rb);
        const double ca = centroid(La), cb = centroid(Lb), ma = medianMidi(La, 1, 2.8), mb = medianMidi(Lb, 1, 2.8);
        CHECK(cb < ca * 0.85 && std::fabs(ma - mb) < 0.25, "FORMANT -6: spectral centroid %.0f -> %.0f Hz, pitch kept (%.2f / %.2f)", ca, cb, ma, mb);
    }
    // ---- ROBOT: monotone on NOTE, then on a MIDI note
    {
        const auto glideV = voice(3.0, [](double t) { return 140.0 * std::pow(2.0, t / 3.0 * 7.0 / 12.0); });   // a fifth up over 3 s
        VoiceFx fx; FxSettings s; s.robotOn = true; s.mono = 100.0f; s.note = 45; s.glideMs = 5.0f; fx.set(s); fx.prepare(sr);
        std::vector<float> L, R; run(fx, glideV, L, R);
        const double m1 = medianMidi(L, 0.6, 1.4), m2 = medianMidi(L, 2.0, 2.8);
        CHECK(std::fabs(m1 - 45.0) < 0.35 && std::fabs(m2 - 45.0) < 0.35, "MONO 100 %%: a gliding voice held on note 45: %.2f then %.2f", m1, m2);
        VoiceFx fm; fm.set(s); fm.prepare(sr);
        fm.midi(0x90, 52, 100);
        run(fm, glideV, L, R);
        const double mm = medianMidi(L, 1.0, 2.8);
        CHECK(std::fabs(mm - 52.0) < 0.35, "MONO on a held MIDI note 52: %.2f", mm);
    }
    // ---- GRIT: fry under the voice
    {
        VoiceFx a, b; FxSettings s; s.pitchOn = false; a.set(s); a.prepare(sr);
        s.gritOn = true; s.fry = 80.0f; s.rasp = 0.0f; s.rattle = 0.0f; s.drive = 0.0f; b.set(s); b.prepare(sr);
        std::vector<float> La, Ra, Lb, Rb; run(a, steady, La, Ra); run(b, steady, Lb, Rb);
        const double da = bandDb(La, 70, 80) - bandDb(La, 145, 155), db = bandDb(Lb, 70, 80) - bandDb(Lb, 145, 155);
        CHECK(db > da + 15.0, "FRY: energy at f0/2 (75 Hz) against f0: %.1f -> %.1f dB", da, db);
    }
    // ---- RING
    {
        VoiceFx a; FxSettings s; s.pitchOn = false; s.robotOn = true; s.mono = 0.0f; s.ring = 100.0f; s.ringHz = 40.0f; a.set(s); a.prepare(sr);
        std::vector<float> L, R; run(a, steady, L, R);
        const double side = bandDb(L, 105, 115) - bandDb(L, 145, 155);   // 150 - 40 = 110 Hz
        CHECK(side > -10.0, "RING 40 Hz: a sideband at 110 Hz %.1f dB against 150 Hz", side);
    }
    // ---- random settings: finite, latency fixed
    {
        VoiceFx fx; fx.prepare(sr);
        std::mt19937 rng(7); std::uniform_real_distribution<float> u(0, 1);
        const auto v = voice(4.0, [](double t) { return 120.0 + 80.0 * std::sin(t * 3.0); });
        std::vector<float> L(256), R(256);
        bool finite = true; float pk = 0.0f;
        for (size_t off = 0; off + 256 <= v.size(); off += 256)
        {
            if ((off / 256) % 30 == 0)
            {
                FxSettings s;
                s.pitchOn = u(rng) < 0.7f; s.pitch = -24 + 48 * u(rng); s.formant = -12 + 24 * u(rng); s.sub = 100 * u(rng);
                s.gritOn = u(rng) < 0.5f; s.fry = 100 * u(rng); s.rasp = 100 * u(rng); s.rattle = 100 * u(rng); s.drive = 36 * u(rng); s.tone = -100 + 200 * u(rng);
                s.robotOn = u(rng) < 0.5f; s.mono = 100 * u(rng); s.ring = 100 * u(rng); s.ringHz = 10 + 1990 * u(rng); s.comb = 100 * u(rng); s.crush = 100 * u(rng);
                s.charOn = u(rng) < 0.5f; s.filter = (int)(u(rng) * 5); s.stack = (int)(u(rng) * 4); s.doubler = 100 * u(rng); s.slap = 100 * u(rng);
                fx.set(s);
                if (u(rng) < 0.2f) fx.midi(0x90, (uint8_t)(36 + 30 * u(rng)), 100);
            }
            fx.process(v.data() + off, L.data(), R.data(), 256);
            for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i]))); }
        }
        CHECK(finite && pk < 8.0f && fx.latency() == VoiceFx::latencyFor(sr), "random settings every 160 ms: finite, peak %.2f, latency fixed", pk);
    }
    printf(fails ? "VOX FX: %d FAILED\n" : "VOX FX: all passed\n", fails);
    return fails ? 1 : 0;
}
