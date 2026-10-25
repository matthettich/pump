// pump WOB: a note at its pitch (octave and tune); one partial is a pure sine; the source shapes give saw, square
// and triangle spectra; nothing is left above Nyquist at a high note; the synced wobble moves the cutoff at the
// division's rate and follows the song when it doesn't retrigger; the wobble delay holds it back; glide slides;
// legato doesn't retrigger the envelope; the sub sits an octave down; random settings and MIDI stay finite. CPU.
#include "../plugins/wob/WobEngine.hpp"
#include "dsp/FFT.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::wob;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings plain()
{
    Settings s; s.octave = 0; s.cutoff = 16000; s.res = 0; s.filterEnv = 0; s.keyTrack = 0; s.sub = 0; s.width = 0; s.attack = 0.001; s.release = 0.02;
    return s;
}
static void render(Engine& e, std::vector<float>& L, std::vector<synth::Midi> ev, std::vector<float>* Rout = nullptr, int blk = 256)
{
    std::vector<float> R(L.size());
    const int n = (int)L.size();
    for (int off = 0; off < n; off += blk)
    {
        const int m = std::min(blk, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
    if (Rout) *Rout = R;
}
static double freqOf(const std::vector<float>& x, size_t a, size_t b, double sr)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * sr / (last - first) : 0.0;
}
// harmonic levels (dB re the fundamental) of a steady note, from a windowed FFT
static std::vector<double> harmonics(const std::vector<float>& x, size_t at, double f0, double sr, int H)
{
    const int N = 16384;
    RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
    for (int i = 0; i < N; ++i) { const double t = kTwoPi * i / N; in[(size_t)i] = (float)(x[at + (size_t)i] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t))); }
    fft.forward(in.data(), out.data());
    auto pw = [&](int k) { return (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)]; };
    std::vector<double> h;
    for (int j = 1; j <= H; ++j)
    {
        const int c = (int)std::lround(j * f0 * N / sr);
        double p = 0; for (int k = c - 4; k <= c + 4; ++k) p += pw(k);
        h.push_back(10 * std::log10(p + 1e-30));
    }
    for (int j = H - 1; j >= 0; --j) h[(size_t)j] -= h[0];
    return h;
}

int main()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)sr;
        {
            Engine e; Settings s = plain(); s.partials = 1; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 57, 100 } });
            const double f = freqOf(L, (size_t)n / 4, (size_t)n, sr);
            CHECK(std::fabs(f - 220.0) < 0.05, "A3, one partial: %.3f Hz", f);
            const auto h = harmonics(L, (size_t)n / 4, 220.0, sr, 4);
            CHECK(h[1] < -70 && h[2] < -70, "one partial is a sine: 2nd %.0f dB, 3rd %.0f dB", h[1], h[2]);
        }
        {
            Engine e; Settings s = plain(); s.octave = -1; s.tune = 7; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 57, 100 } });
            const double f = 110.0 * std::exp2(7.0 / 12.0), m = freqOf(L, (size_t)n / 4, (size_t)n, sr);
            CHECK(std::fabs(m - f) < 0.1, "octave -1, tune +7: %.2f Hz (want %.2f)", m, f);
        }
        {
            Engine e; Settings s = plain(); s.shape = 0.0; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(n + 20000));
            render(e, L, { { 0, 0x90, 45, 100 } });
            const auto h = harmonics(L, 8192, 110.0, sr, 5);
            CHECK(std::fabs(h[1] + 6.02) < 0.6 && std::fabs(h[2] + 9.54) < 0.6, "saw: 2nd %.1f dB, 3rd %.1f dB (1/k)", h[1], h[2]);
            Engine e2; s.shape = 0.5; e2.setSettings(s); e2.prepare(sr, 512);
            render(e2, L, { { 0, 0x90, 45, 100 } });
            const auto q = harmonics(L, 8192, 110.0, sr, 5);
            CHECK(q[1] < -60 && std::fabs(q[2] + 9.54) < 0.6, "square: 2nd %.0f dB, 3rd %.1f dB", q[1], q[2]);
            Engine e3; s.shape = 1.0; e3.setSettings(s); e3.prepare(sr, 512);
            render(e3, L, { { 0, 0x90, 45, 100 } });
            const auto t = harmonics(L, 8192, 110.0, sr, 5);
            CHECK(t[1] < -60 && std::fabs(t[2] + 19.08) < 0.6, "triangle: 2nd %.0f dB, 3rd %.1f dB (1/k^2)", t[1], t[2]);
            Engine e4; s.shape = 0.0; s.oddEven = 1.0; e4.setSettings(s); e4.prepare(sr, 512);
            render(e4, L, { { 0, 0x90, 45, 100 } });
            const auto o = harmonics(L, 8192, 110.0, sr, 5);
            CHECK(o[1] < -60 && o[3] < -60, "odd / even +100: the even partials gone (%.0f, %.0f dB)", o[1], o[3]);
        }
        {
            // a high note: partials past Nyquist are never made
            const int N = 16384;
            Engine e; Settings s = plain(); s.partials = 64; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(N + 8192));
            render(e, L, { { 0, 0x90, 96, 100 } });
            RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
            for (int i = 0; i < N; ++i) { const double t = kTwoPi * i / N; in[(size_t)i] = (float)(L[(size_t)(8192 + i)] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t))); }
            fft.forward(in.data(), out.data());
            const double f0 = synth::midiToHz(96);
            double harm = 0, other = 0;
            for (int k = 4; k < N / 2; ++k)
            {
                const double p = (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
                const double f = k * sr / N, h = f / f0;
                if (std::fabs(h - std::round(h)) * f0 < 6.0 * sr / N && std::round(h) >= 1) harm += p; else other += p;
            }
            const double r = 10 * std::log10(other / harm);
            CHECK(r < -80.0, "64 partials at C7: non-harmonic content %.1f dB", r);
        }
        {
            // synced wobble, 1/4 at 120 BPM = 2 Hz: the cutoff swings twice a second
            Engine e; Settings s = plain(); s.cutoff = 200; s.wobCut = 4; s.wobDiv = kNote4; s.wobSmooth = 0; e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.bpm = 120; e.setTransport(t);
            std::vector<float> L(256), R(256);
            std::vector<double> cut;
            synth::Midi on { 0, 0x90, 45, 100 };
            e.process(L.data(), R.data(), 256, &on, 1);
            for (int b = 0; b < (int)(2.0 * sr / 256); ++b) { e.process(L.data(), R.data(), 256, nullptr, 0); cut.push_back(e.cutoffNow()); }
            int rises = 0; for (size_t i = 2; i < cut.size(); ++i) if (cut[i - 1] < 1000 && cut[i] >= 1000) ++rises;
            const double mx = *std::max_element(cut.begin(), cut.end()), mn = *std::min_element(cut.begin(), cut.end());
            CHECK(rises == 4 && mn < 210 && mx > 3000, "1/4 wobble at 120 BPM over 2 s: %d sweeps, cutoff %.0f..%.0f Hz", rises, mn, mx);
            // without retrigger, it follows the song: at ppq 0.5 of a 1/4 sine it's at its top
            Engine f; s.wobRetrig = false; f.setSettings(s); f.prepare(sr, 512);
            f.process(L.data(), R.data(), 256, &on, 1);
            t.playing = true; t.valid = true;
            for (int b = 0; b < 200; ++b) { t.ppq = 0.5; f.setTransport(t); f.process(L.data(), R.data(), 256, nullptr, 0); }
            CHECK(f.wobbleNow() > 0.95, "song-locked wobble at the half beat: %.2f (top)", f.wobbleNow());
        }
        {
            // the wobble delay holds it at zero for its time
            Engine e; Settings s = plain(); s.wobCut = 4; s.wobDelay = 300; s.wobFade = 0; s.wobSync = false; s.wobRate = 8; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(256), R(256);
            synth::Midi on { 0, 0x90, 45, 100 };
            e.process(L.data(), R.data(), 256, &on, 1);
            double early = 0, late = 0;
            for (int b = 0; b < (int)(0.6 * sr / 256); ++b)
            {
                e.process(L.data(), R.data(), 256, nullptr, 0);
                const double ms = (b + 1) * 256 * 1000.0 / sr;
                if (ms < 280) early = std::max(early, (double)e.wobbleNow()); else if (ms > 320) late = std::max(late, (double)e.wobbleNow());
            }
            CHECK(early == 0.0 && late > 0.9, "wobble delay 300 ms: %.2f before, %.2f after", early, late);
        }
        {
            // glide: 200 ms from A2 to A3, halfway after about a time constant's worth
            Engine e; Settings s = plain(); s.partials = 1; s.glide = 100; s.legato = true; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 45, 100 }, { (uint32_t)(n / 4), 0x90, 57, 100 } });
            const double mid = freqOf(L, (size_t)(n / 4 + 0.02 * sr), (size_t)(n / 4 + 0.06 * sr), sr), end = freqOf(L, (size_t)(n * 3 / 4), (size_t)n, sr);
            CHECK(mid > 120 && mid < 210 && std::fabs(end - 220) < 0.1, "glide A2 -> A3: %.0f Hz on the way, %.2f Hz at the end", mid, end);
            // legato: the second (overlapping) note doesn't restart the attack
            Engine g; s.glide = 0; s.attack = 0.3; g.setSettings(s); g.prepare(sr, 512);
            std::vector<float> M((size_t)n);
            render(g, M, { { 0, 0x90, 45, 100 }, { (uint32_t)(n / 2), 0x90, 57, 100 } });
            double before = 0, after = 0;
            for (size_t i = (size_t)(n / 2) - 400; i < (size_t)(n / 2); ++i) before = std::max(before, (double)std::fabs(M[i]));
            for (size_t i = (size_t)(n / 2) + 50; i < (size_t)(n / 2) + 450; ++i) after = std::max(after, (double)std::fabs(M[i]));
            CHECK(after > 0.9 * before, "legato: level %.3f before the new note, %.3f after (no restart)", before, after);
        }
        {
            Engine e; Settings s = plain(); s.partials = 1; s.sub = 1.0; s.subOct = 0; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(n + 20000));
            render(e, L, { { 0, 0x90, 57, 100 } });
            const auto h = harmonics(L, 8192, 110.0, sr, 2);
            CHECK(std::fabs(h[1]) < 12.0, "sub: an octave under the note (110 Hz) with the note at %+.1f dB to it", h[1]);
        }
        if (sr != 48000.0) continue;
        {
            std::mt19937 rng(48); std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.bpm = 140; t.playing = true; t.valid = true;
            bool finite = true; double peak = 0;
            std::vector<float> L(512), R(512);
            for (int it = 0; it < 3000; ++it)
            {
                if (it % 11 == 0)
                {
                    s.octave = (int)(u(rng) * 6) - 3; s.tune = u(rng) * 24 - 12; s.shape = u(rng); s.partials = 1 + (int)(u(rng) * 64); s.tilt = u(rng) * 2 - 1;
                    s.oddEven = u(rng) * 2 - 1; s.stretch = u(rng); s.filterMode = (int)(u(rng) * 3); s.cutoff = 20 * std::pow(800.0, u(rng)); s.res = u(rng);
                    s.filterEnv = u(rng) * 12 - 4; s.filterDecay = 0.005 + 5 * u(rng); s.keyTrack = u(rng); s.vowel = u(rng); s.formant = u(rng); s.combFreq = u(rng); s.comb = u(rng);
                    s.detune = u(rng) * 50; s.width = u(rng); s.sub = u(rng); s.subOct = u(rng) < 0.5;
                    s.wobSync = u(rng) < 0.5; s.wobDiv = (int)(u(rng) * kNumNotes); s.wobRate = 0.05 + 30 * u(rng); s.wobShape = (int)(u(rng) * kNumShapes); s.wobRetrig = u(rng) < 0.5;
                    s.wobPhase = u(rng); s.wobSmooth = u(rng); s.wobDelay = 4000 * u(rng) * u(rng); s.wobFade = 4000 * u(rng) * u(rng); s.wobDepth = u(rng); s.wheel = u(rng);
                    s.wobCut = u(rng) * 12 - 6; s.wobVowel = u(rng) * 2 - 1; s.wobComb = u(rng) * 2 - 1; s.wobAmp = u(rng); s.wobPitch = u(rng) * 24 - 12;
                    s.glide = u(rng) < 0.5 ? 0 : 500 * u(rng); s.legato = u(rng) < 0.5; s.drive = u(rng); s.attack = 0.0005 + u(rng) * 0.2;
                    e.setSettings(s);
                }
                t.ppq += 512 / sr * t.bpm / 60; e.setTransport(t);
                std::vector<synth::Midi> ev;
                if (u(rng) < 0.3) ev.push_back({ (uint32_t)(u(rng) * 511), (uint8_t)(u(rng) < 0.6 ? 0x90 : 0x80), (uint8_t)(12 + u(rng) * 100), (uint8_t)(1 + u(rng) * 126) });
                if (u(rng) < 0.05) ev.push_back({ 511, 0xB0, 1, (uint8_t)(u(rng) * 127) });
                if (u(rng) < 0.05) ev.push_back({ 300, 0xE0, (uint8_t)(u(rng) * 127), (uint8_t)(u(rng) * 127) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                e.process(L.data(), R.data(), 512, ev.data(), (int)ev.size());
                for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            CHECK(finite && peak < 4.0, "random settings and MIDI: finite (peak %.2f)", peak);
        }
        {
            Engine e; Settings s; s.partials = 64; s.detune = 20; s.formant = 0.5; s.comb = 0.4; s.wobCut = 3; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            const auto t0 = std::chrono::steady_clock::now();
            render(e, L, { { 0, 0x90, 40, 100 } });
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: 64 partials x 2 (reese), formant, comb, wobble: %.1f %% of one core\n", 100.0 * secs);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
