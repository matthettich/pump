// pump PRISM: the analysis finds a tone's pitch and its harmonics' levels; the resynthesis plays it back at the
// note's pitch with the same harmonic levels, an octave up when asked; KEEP FORMANTS keeps the levels where they
// were in frequency; MORPH moves from one sound's spectrum to another's; the noise layer gives back white noise at
// its level; FREEZE holds one moment; every built-in source builds and is found at its pitch; random settings stay
// finite. CPU.
#include "../plugins/prism/PrismEngine.hpp"
#include "../plugins/prism/PrismSources.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>

using namespace pump;
using namespace pump::prism;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static const double kSr = 48000.0;

static std::vector<float> tone(double f0, int K, bool oddOnly, double secs)
{
    std::vector<float> y((size_t)(kSr * secs));
    for (size_t i = 0; i < y.size(); ++i)
    {
        double s = 0.0;
        for (int k = 1; k <= K; ++k) { if (oddOnly && !(k & 1)) continue; if (k * f0 > kSr * 0.45) break; s += std::sin(kTwoPi * f0 * k * i / kSr) / k; }
        y[i] = (float)(0.3 * s);
    }
    return y;
}
static std::vector<float> play(Engine& e, int note, int n)
{
    std::vector<float> L((size_t)n + 256), R((size_t)n + 256);
    synth::Midi on { 0, 0x90, (uint8_t)note, 127 };
    for (int off = 0; off < n; off += 256) e.process(L.data() + off, R.data() + off, 256, off == 0 ? &on : nullptr, off == 0 ? 1 : 0);
    L.resize((size_t)n);
    for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (L[i] + R[i]);
    return L;
}
static double harm(const std::vector<float>& x, size_t a, size_t n, double f)
{
    double re = 0, im = 0;
    for (size_t i = a; i < a + n; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i - a) / n); re += x[i] * w * std::cos(kTwoPi * f * i / kSr); im += x[i] * w * std::sin(kTwoPi * f * i / kSr); }
    return 2.0 * std::sqrt(re * re + im * im) / (0.5 * n);
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (b - a)); }
static Settings plain() { Settings s; s.noise = 0.0; s.attack = 0.002; s.release = 0.05; s.velocity = 0.0; s.width = 0.0; s.own = 1.0; return s; }

int main()
{
    printf("== the analysis\n");
    const auto saw = analyse(tone(220.0, 40, false, 2.0), kSr, "saw");
    {
        const int f = saw->frames / 2;
        const double a1 = saw->amp[(size_t)f * kK], a5 = saw->amp[(size_t)f * kK + 4];
        CHECK(std::fabs(saw->f0med - 220.0) < 1.5 && std::fabs(a1 - 0.3) < 0.01 && std::fabs(a5 / a1 - 0.2) < 0.01, "a 220 Hz saw: pitch %.2f Hz, harmonic 1 %.3f (0.300), harmonic 5 / 1 = %.3f (0.200)", saw->f0med, a1, a5 / a1);
        const auto sq = analyse(tone(110.0, 40, true, 2.0), kSr, "square");
        CHECK(std::fabs(sq->f0med - 110.0) < 1.0, "a 110 Hz square (odd harmonics only): pitch %.2f Hz (not an octave off)", sq->f0med);
    }
    printf("== resynthesis\n");
    {
        Engine e; Settings s = plain(); e.setSettings(s); e.prepare(kSr, 256); e.setSources(saw.get(), nullptr);
        const auto y = play(e, 57, 48000);
        const double h1 = harm(y, 9600, 19200, 220.0), h3 = harm(y, 9600, 19200, 660.0), h8 = harm(y, 9600, 19200, 1760.0);
        CHECK(std::fabs(20 * std::log10(h1 / 0.3)) < 1.0 && std::fabs(20 * std::log10((h3 / h1) / (1.0 / 3))) < 1.0 && std::fabs(20 * std::log10((h8 / h1) / 0.125)) < 1.0,
              "A3 plays it back: harmonic 1 at %.1f dB of the source, 3 / 1 and 8 / 1 within %.2f / %.2f dB", 20 * std::log10(h1 / 0.3), 20 * std::log10((h3 / h1) * 3), 20 * std::log10((h8 / h1) * 8));
        Engine o; o.setSettings(s); o.prepare(kSr, 256); o.setSources(saw.get(), nullptr);
        const auto up = play(o, 69, 48000);
        CHECK(harm(up, 9600, 19200, 440.0) > 10.0 * harm(up, 9600, 19200, 220.0), "A4 plays it an octave up (440 Hz %.3f, 220 Hz %.4f)", harm(up, 9600, 19200, 440.0), harm(up, 9600, 19200, 220.0));
        // KEEP FORMANTS: up an octave, the first partial takes the level the sound had at 440 Hz (its 2nd harmonic)
        Settings f = s; f.formant = 1.0;
        Engine g; g.setSettings(f); g.prepare(kSr, 256); g.setSources(saw.get(), nullptr);
        const auto fy = play(g, 69, 48000);
        const double k1 = harm(fy, 9600, 19200, 440.0);
        CHECK(std::fabs(20 * std::log10(k1 / 0.15)) < 1.0, "KEEP FORMANTS, an octave up: the 440 Hz partial at %.3f (the source's 2nd harmonic: 0.150)", k1);
    }
    printf("== morph, noise, freeze\n");
    {
        const auto sq = analyse(tone(220.0, 40, true, 2.0), kSr, "square");
        auto second = [&](double morph) {
            Engine e; Settings s = plain(); s.morph = morph; e.setSettings(s); e.prepare(kSr, 256); e.setSources(saw.get(), sq.get());
            const auto y = play(e, 57, 48000);
            return 20 * std::log10(harm(y, 9600, 19200, 440.0) / harm(y, 9600, 19200, 220.0));
        };
        const double m0 = second(0.0), m5 = second(0.5), m1 = second(1.0);
        CHECK(std::fabs(m0 + 6.0) < 1.0 && m5 < m0 - 4.0 && m1 < -40.0, "MORPH saw -> square: 2nd harmonic %.1f, %.1f, %.1f dB at 0 / 50 / 100 %%", m0, m5, m1);
        // white noise of a known level comes back at about that level
        fx::Rng rng(3u);
        std::vector<float> wn((size_t)kSr * 2);
        for (auto& x : wn) x = (float)(0.2 * rng.bi());
        const auto na = analyse(wn, kSr, "noise");
        Engine e; Settings s = plain(); s.noise = 1.0; s.partials = 1; e.setSettings(s); e.prepare(kSr, 256); e.setSources(na.get(), nullptr);
        const auto y = play(e, 57, 48000);
        // compare the band 100 Hz .. 16 kHz of both
        const double rIn = 0.2 / std::sqrt(3.0) * std::sqrt((16000.0 - 80.0) / 24000.0), rOut = rms(y, 9600, 40000);
        CHECK(std::fabs(20 * std::log10(rOut / rIn)) < 3.0, "white noise resynthesised: %.1f dB of the original's level (80 Hz - 16 kHz)", 20 * std::log10(rOut / rIn));
        // FREEZE: the level stays put while the sound itself fades
        std::vector<float> fade = tone(220.0, 20, false, 2.0);
        for (size_t i = 0; i < fade.size(); ++i) fade[i] *= (float)(1.0 - (double)i / fade.size());
        const auto fa = analyse(fade, kSr, "fade");
        Settings z = plain(); z.mode = kFreeze; z.scan = 0.2;
        Engine fz; fz.setSettings(z); fz.prepare(kSr, 256); fz.setSources(fa.get(), nullptr);
        const auto zy = play(fz, 57, 96000);
        const double r1 = rms(zy, 9600, 19200), r2 = rms(zy, 76800, 86400);
        CHECK(std::fabs(20 * std::log10(r2 / r1)) < 0.5 && std::fabs(20 * std::log10(r1 / (0.8 * 0.3 / std::sqrt(2.0) * 1.15))) < 4.0, "FREEZE at 20 %%: steady (%.2f dB from 0.2 s to 1.6 s)", 20 * std::log10(r2 / r1));
        // LOOP at 100 % through the fading sound: louder at its start than near its end
        Settings lp = plain();
        Engine le; le.setSettings(lp); le.prepare(kSr, 256); le.setSources(fa.get(), nullptr);
        const auto ly = play(le, 57, 96000);
        CHECK(rms(ly, 4800, 9600) > 2.0 * rms(ly, 81600, 86400), "LOOP at 100 %%: it plays the fade through (%.1f dB down at 1.7 s)", 20 * std::log10(rms(ly, 81600, 86400) / rms(ly, 4800, 9600)));
    }
    printf("== the built-in sources\n");
    {
        Builtins::get().start();
        const auto t0 = std::chrono::steady_clock::now();
        while (!Builtins::get().source(kBuiltins) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        static const double want[kNumSources] = { 0, 220.0, 130.81, 261.63, 110.0, 110.0, 440.0, 98.0 };
        bool all = true;
        for (int id = 1; id <= kBuiltins; ++id)
        {
            const Analysis* A = Builtins::get().source(id);
            const bool inharmonic = id == 3 || id == 7;   // bells and the gong have no one pitch
            const bool ok = A && A->frames > 100 && (inharmonic || std::fabs(1200 * std::log2(A->f0med / want[id])) < 60.0);
            printf("    %-14s %s  %.2f s  pitch %.1f Hz (%.1f)\n", kSourceNames[id], ok ? "ok" : "--", A ? A->seconds : 0.0, A ? A->f0med : 0.0, want[id]);
            all = all && ok;
        }
        CHECK(all, "all %d built-in sources ready in %.1f s, each at its pitch", kBuiltins, secs);
    }
    printf("== random settings\n");
    {
        std::mt19937 rng(9); std::uniform_real_distribution<double> u(0, 1);
        bool fin = true; float pk = 0;
        for (int t = 0; t < 30; ++t)
        {
            Settings s; s.mode = (int)(u(rng) * kNumModes); s.speed = u(rng) * 4.0; s.start = u(rng); s.end = u(rng); s.scan = u(rng); s.follow = u(rng);
            s.partials = 4 + (int)(u(rng) * 124); s.tilt = u(rng) * 2 - 1; s.oddEven = u(rng) * 2 - 1; s.inharm = u(rng); s.own = u(rng); s.formant = u(rng);
            s.blur = u(rng); s.noise = u(rng) * 2; s.width = u(rng); s.morph = u(rng); s.voiceMode = (int)(u(rng) * 3); s.glide = u(rng) * 200; s.octave = (int)(u(rng) * 7) - 3;
            Engine e; e.setSettings(s); e.prepare(t % 2 ? 44100.0 : 96000.0, 256);
            e.setSources(Builtins::get().source(1 + t % kBuiltins), t % 3 ? Builtins::get().source(1 + (t * 5) % kBuiltins) : nullptr);
            std::vector<float> L(48000 + 256), R(48000 + 256);
            for (int off = 0; off < 48000; off += 256)
            {
                std::vector<synth::Midi> ev;
                if (off % 6144 == 0) ev.push_back({ 0, 0x90, (uint8_t)(24 + (int)(u(rng) * 80)), (uint8_t)(1 + (int)(u(rng) * 126)) });
                if (off % 9216 == 0) ev.push_back({ 0, 0x80, (uint8_t)(24 + (int)(u(rng) * 80)), 0 });
                e.process(L.data() + off, R.data() + off, 256, ev.data(), (int)ev.size());
            }
            for (int i = 0; i < 48000; ++i) { fin = fin && std::isfinite(L[(size_t)i]); pk = std::max(pk, std::fabs(L[(size_t)i])); }
        }
        CHECK(fin && pk < 8.0f, "random settings, sources and MIDI: finite and bounded (peak %.2f)", pk);
    }
    printf("== cpu\n");
    {
        Engine e; Settings s; e.setSettings(s); e.prepare(kSr, 256); e.setSources(Builtins::get().source(1), Builtins::get().source(3));
        std::vector<float> L(48000 * 2 + 256), R(L.size());
        std::vector<synth::Midi> ev; for (int k = 0; k < 8; ++k) ev.push_back({ 0, 0x90, (uint8_t)(48 + k * 3), 100 });
        const auto t0 = std::chrono::steady_clock::now();
        for (int off = 0; off < 96000; off += 256) e.process(L.data() + off, R.data() + off, 256, off == 0 ? ev.data() : nullptr, off == 0 ? 8 : 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  cpu: 8 voices x 64 partials + noise, morphing: %.1f %% of one core\n", 100.0 * secs / 2.0);
    }
    std::printf(fails ? "test_prism: %d FAILED\n" : "test_prism: all passed\n", fails);
    return fails ? 1 : 0;
}
