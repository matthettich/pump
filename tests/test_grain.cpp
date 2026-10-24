// pump GRAIN: with spray 0 and pitch 0 the grains read the input delayed by POSITION (cross-correlation peak at that
// lag); PITCH +12 / -12 moves a sine's dominant frequency an octave up / down; quantizing keeps every grain's pitch on
// its scale (spawn log); REVERSE 100 % turns a rising ramp into falling grain segments; FREEZE keeps sounding after
// the input stops (and stops when released); DENSITY gives that many grains per second; SYNC starts grains on the
// note grid of a playing transport (odd block sizes, a loop jump) and free-runs at the host tempo when stopped; the
// output level holds across density and size; the macros push size, density and scatter the right way; feedback
// 100 % stays bounded for 30 s; envelope edges, freeze and parameter moves are click-free; mix 0 is the input
// exactly; random settings / macros / block sizes / transports stay finite and bounded. 44.1, 48 and 96 kHz. Also
// prints CPU per second of audio (default setting, and 64 grains with every FX on).
#include "TestMix.hpp"
#include "../plugins/grain/GrainEngine.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::grain;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, int block = 256, size_t from = 0, size_t to = 0)
{
    if (to == 0) to = L.size();
    for (size_t off = from; off < to; off += (size_t)block) e.process(L.data() + off, R.data() + off, (int)std::min<size_t>((size_t)block, to - off));
}
// a dry, still setting: no space, no motion, no stereo spread, all wet
static Settings plain()
{
    Settings s; s.space = 0; s.motionDepth = 0; s.stereo = 0; s.spray = 0; s.mix = 100; s.feedback = 0; s.filter = kFilterOff;
    return s;
}
static void noise(std::vector<float>& L, std::vector<float>& R, size_t n, double amp, uint32_t seed)
{
    fx::Rng r(seed);
    L.resize(n); R.resize(n);
    for (size_t i = 0; i < n; ++i) { L[i] = (float)(amp * r.bi()); R[i] = (float)(amp * r.bi()); }
}
static double rmsDb(const std::vector<float>& L, const std::vector<float>& R, size_t from, size_t to = 0)
{
    if (to == 0 || to > L.size()) to = L.size();
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)L[i] * L[i] + (double)R[i] * R[i];
    return 10 * std::log10(e / (2.0 * (to - from)) + 1e-30);
}
static double maxJump(const std::vector<float>& x, size_t from, size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    double m = 0; for (size_t i = from + 1; i < to; ++i) m = std::max(m, (double)std::fabs(x[i] - x[i - 1]));
    return m;
}
// the lag (0..maxLag) where y correlates best with x (circular, by FFT; N a power of two >= the signals)
static int bestLag(const std::vector<float>& x, const std::vector<float>& y, size_t from, int N, int maxLag)
{
    RealFFT fft; fft.setup(N);
    AlignedBuffer a((size_t)N), b((size_t)N), A((size_t)N), B((size_t)N), C((size_t)N), c((size_t)N);
    for (int i = 0; i < N; ++i)
    {
        const size_t k = from + (size_t)i;
        a[(size_t)i] = k < x.size() ? x[k] : 0.0f;
        b[(size_t)i] = k < y.size() ? y[k] : 0.0f;
    }
    fft.forward(a.data(), A.data());
    fft.forward(b.data(), B.data());
    C[0] = A[0] * B[0]; C[1] = A[1] * B[1];
    for (int k = 1; k < N / 2; ++k)
    {
        const float ar = A[(size_t)(2 * k)], ai = A[(size_t)(2 * k + 1)], br = B[(size_t)(2 * k)], bi = B[(size_t)(2 * k + 1)];
        C[(size_t)(2 * k)] = ar * br + ai * bi;   // conj(A) B
        C[(size_t)(2 * k + 1)] = ar * bi - ai * br;
    }
    fft.inverse(C.data(), c.data());
    int best = 0;
    for (int l = 1; l <= maxLag; ++l) if (c[(size_t)l] > c[(size_t)best]) best = l;
    return best;
}
// the dominant frequency from `from` to the end: the power-weighted centre of the strongest peak (+-4 %; Hann windows,
// half overlap, powers averaged so the random interference between grains averages out)
static double peakHz(const std::vector<float>& x, size_t from, int N, double sr)
{
    RealFFT fft; fft.setup(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    std::vector<double> pw((size_t)N / 2, 0.0);
    for (size_t st = from; st + (size_t)N <= x.size(); st += (size_t)N / 2)
    {
        for (int i = 0; i < N; ++i) in[(size_t)i] = (float)(x[st + (size_t)i] * (0.5 - 0.5 * std::cos(kTwoPi * i / N)));
        fft.forward(in.data(), out.data());
        for (int k = 1; k < N / 2; ++k) { const double re = out[(size_t)(2 * k)], im = out[(size_t)(2 * k + 1)]; pw[(size_t)k] += re * re + im * im; }
    }
    int b = 2;
    for (int k = 2; k < N / 2 - 1; ++k) if (pw[(size_t)k] > pw[(size_t)b]) b = k;
    double sw = 0.0, sk = 0.0;
    for (int k = std::max(1, (int)(b * 0.96)); k <= std::min(N / 2 - 1, (int)(b * 1.04) + 1); ++k) { sw += pw[(size_t)k]; sk += pw[(size_t)k] * k; }
    return sk / sw * sr / N;
}

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        // ---- spray 0, pitch 0: the grains read the input delayed by POSITION (every grain reads the same sample, so
        // the sum is the delayed input); the cross-correlation of noise in vs out peaks at that lag
        {
            const int N = sr > 50000.0 ? (1 << 19) : (1 << 18);
            for (double posMs : { 0.0, 37.0, 300.0, 1500.0 })
            {
                Settings s = plain(); s.positionMs = posMs; s.sizeMs = 100; s.density = 20;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L, R; noise(L, R, (size_t)N + (size_t)sr, 0.25, 7);
                const std::vector<float> L0 = L;
                run(e, L, R, 512);
                const int want = std::max(Engine::kMargin, (int)std::lround(posMs * 0.001 * sr));
                const int lag = bestLag(L0, L, (size_t)sr / 2, N, (int)(2.2 * sr));
                // the rest after taking out the delayed input (fractional delays: cubic interpolation error)
                double er = 0, rr = 0;
                const double d = std::max((double)Engine::kMargin, posMs * 0.001 * sr), fr = d - std::floor(d);
                for (size_t i = (size_t)(sr + d) + 2; i < L.size() - 2; ++i)
                {
                    // the input d samples back (a fractional d: the same cubic read the grains use)
                    const size_t k = i - (size_t)std::floor(d);
                    const float t = (float)(1.0 - fr);
                    const float ym1 = L0[k - 2], y0 = L0[k - 1], y1 = L0[k], y2 = L0[k + 1];
                    const double ref = fr == 0.0 ? (double)L0[k]
                        : (double)((((0.5f * (y2 - ym1) + 1.5f * (y0 - y1)) * t + (ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2)) * t + 0.5f * (y1 - ym1)) * t + y0);
                    er += (L[i] - ref) * (L[i] - ref); rr += ref * ref;
                }
                CHECK(std::abs(lag - want) <= 1 && 10 * std::log10(er / rr) < -60.0,
                      "position %.0f ms: correlation peaks at %d samples (want %d), residual %.1f dB", posMs, lag, want, 10 * std::log10(er / rr));
            }
        }
        // ---- pitch +12 / -12 on a sine: the dominant frequency moves an octave (at the default spray: grains spaced
        // exactly evenly add a comb at the grain rate to any shifted tone)
        {
            const double f0 = 440.0;
            for (double st : { 12.0, -12.0, 7.0, 24.0 })
            {
                Settings s = plain(); s.pitch = st; s.sizeMs = 80; s.density = 30; s.positionMs = 50; s.spray = 10;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const size_t n = (size_t)(12.0 * sr);
                std::vector<float> L(n), R(n);
                for (size_t i = 0; i < n; ++i) L[i] = R[i] = (float)(0.3 * std::sin(kTwoPi * f0 * (double)i / sr));
                run(e, L, R, 512);
                const int N = sr > 50000.0 ? 32768 : 16384;
                const double got = peakHz(L, (size_t)sr, N, sr), want = f0 * std::exp2(st / 12.0);
                CHECK(std::fabs(got / want - 1.0) < 0.004, "pitch %+.0f st on a %.0f Hz sine: dominant %.1f Hz (want %.1f)", st, f0, got, want);
            }
        }
        // ---- density = grains per second (free running, with and without spray jitter)
        {
            double worst[2] = { 0, 0 };
            for (double dens : { 1.0, 5.0, 20.0, 60.0, 100.0 })
                for (int k = 0; k < 2; ++k)
                {
                    Settings s = plain(); s.density = dens; s.spray = k ? 60 : 0; s.sizeMs = 20;
                    Engine e; e.setSettings(s); e.prepare(sr, 512);
                    std::vector<float> L((size_t)(30.0 * sr), 0.0f), R = L;
                    run(e, L, R, 397);
                    const uint64_t n = e.spawnCount(), first = n > (uint64_t)Engine::kLogN ? n - Engine::kLogN : 0;
                    const double rate = (double)(n - 1 - first) * sr / (double)(e.spawnLog(n - 1).time - e.spawnLog(first).time);
                    worst[k] = std::max(worst[k], std::fabs(rate / dens - 1.0));
                }
            CHECK(worst[0] < 0.001 && worst[1] < 0.05, "density 1..100 /s: grains per second within %.2f %% (spray 0), %.1f %% (spray 60, jittered timing)",
                  100.0 * worst[0], 100.0 * worst[1]);
            Settings s = plain(); s.density = 100; s.sizeMs = 1000;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(4.0 * sr), 0.0f), R = L;
            int most = 0;
            for (size_t off = 0; off < L.size(); off += 512) { e.process(L.data() + off, R.data() + off, (int)std::min<size_t>(512, L.size() - off)); most = std::max(most, e.activeGrains()); }
            CHECK(most == kMaxGrains && e.droppedCount() > 0, "100 /s x 1 s grains: %d voices at most (%llu over the limit not started)", most, (unsigned long long)e.droppedCount());
        }
        // ---- sync: grains start on the note grid of a playing transport, also across a loop jump; free-run at the
        // host tempo when stopped
        {
            for (int div : { (int)kNote16, (int)kNote8T, (int)kNote4D })
            {
                Settings s = plain(); s.sync = true; s.division = div; s.sizeMs = 30;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const double bpm = 128.0, startQ = 3.1;
                const double spq = 60.0 / bpm * sr;   // samples per quarter
                std::vector<float> L(4096, 0.0f), R = L;
                int64_t pos = 0;
                double maxErr = 0;
                uint64_t seen = 0;
                int jumpAt = (int)(6.0 * sr);
                double ppqBase = startQ;
                int64_t posBase = 0;
                std::mt19937 rng(3);
                bool gridOk = true;
                while (pos < (int64_t)(10.0 * sr))
                {
                    if (pos >= jumpAt && jumpAt > 0) { ppqBase = 1.0; posBase = pos; jumpAt = 0; }   // loop back to quarter 1
                    Transport t; t.bpm = bpm; t.playing = true; t.valid = true; t.ppq = ppqBase + (double)(pos - posBase) / spq;
                    e.setTransport(t);
                    const int bs = 37 + (int)(rng() % 900);
                    e.process(L.data(), R.data(), bs);
                    pos += bs;
                    for (; seen < e.spawnCount(); ++seen)
                    {
                        const Engine::SpawnInfo& si = e.spawnLog(seen);
                        const int64_t ts = si.time;
                        const double q = (ts < posBase && posBase > 0 ? startQ + (double)ts / spq : ppqBase + (double)(ts - posBase) / spq) / noteQuarters(div);
                        const double err = std::fabs(q - std::round(q)) * noteQuarters(div) * spq;
                        maxErr = std::max(maxErr, err);
                        if (err > 1.01) gridOk = false;
                    }
                }
                const double expect = 10.0 * sr / (noteQuarters(div) * spq);
                CHECK(gridOk && std::fabs((double)seen - expect) <= 3.0, "sync %s at %.0f BPM, playing (odd blocks, a loop jump): %llu grains, all within %.2f samples of the grid (expected ~%.0f)",
                      kNoteNames[div], bpm, (unsigned long long)seen, maxErr, expect);
            }
            // stopped: free-run at the host tempo (and 120 BPM when the host sends none)
            for (double bpm : { 90.0, 120.0 })
            {
                Settings s = plain(); s.sync = true; s.division = kNote8; s.sizeMs = 30;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                Transport t; t.bpm = bpm; t.playing = false; t.valid = false;
                e.setTransport(t);
                std::vector<float> L((size_t)(8.0 * sr), 0.0f), R = L;
                run(e, L, R, 333);
                double worst = 0;
                for (uint64_t i = 1; i < e.spawnCount(); ++i)
                    worst = std::max(worst, std::fabs((double)(e.spawnLog(i).time - e.spawnLog(i - 1).time) - noteSeconds(kNote8, bpm) * sr));
                CHECK(worst <= 1.0 && e.spawnCount() > 10, "sync 1/8, stopped at %.0f BPM: grains every %.0f samples (+-%.0f)", bpm, noteSeconds(kNote8, bpm) * sr, worst);
            }
        }
        if (sr != 48000.0) continue;
        // ---- quantize: every grain's pitch is on the scale (pitch spread 24, motion on)
        {
            bool allOk = true;
            for (int sc = kScaleChromatic; sc < kNumScales; ++sc)
            {
                Settings s = plain(); s.quantize = sc; s.pitchSpread = 24; s.pitch = 3; s.motionDepth = 100; s.motionRate = 5; s.density = 80; s.sizeMs = 20;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)(5.0 * sr), 0.0f), R = L;
                run(e, L, R, 256);
                int off = 0, distinct = 0;
                bool used[49] = {};
                const uint64_t n = std::min<uint64_t>(e.spawnCount(), Engine::kLogN);
                for (uint64_t i = e.spawnCount() - n; i < e.spawnCount(); ++i)
                {
                    const double st = 12.0 * std::log2(e.spawnLog(i).ratio);
                    const int c = (int)std::lround(st);
                    const int pc = ((c % 12) + 12) % 12;
                    if (std::fabs(st - c) > 1e-3 || !(kScaleMask[sc] & (1u << pc))) ++off;
                    if (c >= -24 && c <= 24 && !used[c + 24]) { used[c + 24] = true; ++distinct; }
                }
                if (off) allOk = false;
                if (sc == kScaleOctaves || sc == kScaleFifths || sc == kScalePentatonic)
                    CHECK(off == 0 && distinct >= (sc == kScaleOctaves ? 4 : 6), "quantize %s: %llu grains, %d off the scale, %d distinct pitches",
                          kScaleNames[sc], (unsigned long long)n, off, distinct);
            }
            CHECK(allOk, "quantize chromatic / major / minor: every grain on its scale");
        }
        // ---- reverse 100 %: a rising ramp comes out as falling segments (and rising with reverse 0)
        {
            for (double rev : { 100.0, 0.0 })
            {
                Settings s = plain(); s.reverse = rev; s.envelope = kEnvSquare; s.sizeMs = 100; s.density = 4; s.positionMs = 400;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                const size_t n = (size_t)(6.0 * sr);
                std::vector<float> L(n), R(n);
                for (size_t i = 0; i < n; ++i) L[i] = R[i] = (float)(0.8 * (double)i / n);
                run(e, L, R, 512);
                size_t up = 0, down = 0;
                for (size_t i = (size_t)sr; i < n; ++i)
                {
                    // only inside grain bodies (away from their 1.5 ms edges): both neighbours sounding at full level
                    if (std::fabs(L[i]) < 1e-3f || std::fabs(L[i - 1]) < 1e-3f) continue;
                    const float d = L[i] - L[i - 1];
                    if (std::fabs(d) > 2e-4f) continue;   // an edge
                    (d < 0.0f ? down : up)++;
                }
                uint64_t revN = 0;
                for (uint64_t i = 0; i < e.spawnCount(); ++i) revN += e.spawnLog(i).reverse ? 1 : 0;
                const double share = (double)(rev > 50 ? down : up) / std::max<size_t>(1, up + down);
                CHECK(share > 0.99 && revN == (rev > 50 ? e.spawnCount() : 0), "reverse %.0f%% on a rising ramp: %.1f %% of grain samples %s (%llu of %llu grains reversed)",
                      rev, 100.0 * share, rev > 50 ? "fall" : "rise", (unsigned long long)revN, (unsigned long long)e.spawnCount());
            }
        }
        // ---- freeze: keeps sounding after the input stops; stops once released; no click on the switch
        {
            Settings s = plain(); s.sizeMs = 200; s.density = 15; s.spray = 40; s.positionMs = 300; s.pitch = 7; s.reverse = 30;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            const size_t n = (size_t)(12.0 * sr);
            std::vector<float> L, R; noise(L, R, n, 0.25, 9);
            for (size_t i = (size_t)(3.0 * sr); i < n; ++i) L[i] = R[i] = 0.0f;   // the input stops at 3 s
            run(e, L, R, 512, 0, (size_t)(2.5 * sr));
            const double before = rmsDb(L, R, (size_t)(1.5 * sr), (size_t)(2.5 * sr));
            s.freeze = true; e.setSettings(s);
            run(e, L, R, 512, (size_t)(2.5 * sr), (size_t)(8.0 * sr));
            const double frozen = rmsDb(L, R, (size_t)(4.0 * sr), (size_t)(8.0 * sr));
            s.freeze = false; e.setSettings(s);
            run(e, L, R, 512, (size_t)(8.0 * sr), n);
            const double after = rmsDb(L, R, (size_t)(10.5 * sr), n);
            CHECK(frozen > before - 6.0 && after < -100.0, "freeze: %.1f dB while playing, %.1f dB frozen with no input (1-5 s after it stopped), %.1f dB after release",
                  before, frozen, after);
            // the switch itself: freeze on/off over a steady 200 Hz tone (grains of a sine; jumps stay as small as
            // the tone's own steps)
            Settings t = plain(); t.sizeMs = 150; t.density = 20; t.positionMs = 100;
            Engine f; f.setSettings(t); f.prepare(sr, 512);
            std::vector<float> A((size_t)(6.0 * sr)), B;
            for (size_t i = 0; i < A.size(); ++i) A[i] = (float)(0.5 * std::sin(kTwoPi * 200.0 * (double)i / sr));
            B = A;
            const double toneStep = maxJump(A, 0);
            for (size_t off = 0, k = 0; off < A.size(); off += 256, ++k)
            {
                if (k % 60 == 30) { t.freeze = !t.freeze; f.setSettings(t); }
                f.process(A.data() + off, B.data() + off, (int)std::min<size_t>(256, A.size() - off));
            }
            const double j = maxJump(A, (size_t)sr);
            CHECK(j < 2.5 * toneStep, "freeze toggled every 0.3 s on a 200 Hz tone: largest step %.4f (the tone's own %.4f)", j, toneStep);
        }
        // ---- level holds across density and size (constant-power normalisation)
        {
            std::vector<float> L0((size_t)(6.0 * sr)), R0(L0.size());
            for (size_t i = 0; i < L0.size(); ++i) { L0[i] = mix[(2 * i) % mix.size()]; R0[i] = mix[(2 * i + 1) % mix.size()]; }
            const double ref = rmsDb(L0, R0, (size_t)(2.0 * sr));
            // (overlapping clouds: 2 grains at a time or more; sparser settings have gaps of silence by design)
            for (auto sp : { std::make_pair(0.0, 0.0), std::make_pair(50.0, 0.0), std::make_pair(50.0, 12.0) })
            {
                const double spray = sp.first;
                double lo = 1e9, hi = -1e9;
                for (double dens : { 5.0, 8.0, 30.0, 90.0 })
                    for (double size : { 40.0, 150.0, 500.0 })
                    {
                        const double overlap = dens * size * 0.001;
                        if (overlap < 2.0 || overlap > 50.0) continue;
                        Settings s = plain(); s.density = dens; s.sizeMs = size; s.spray = spray; s.pitch = sp.second;
                        Engine e; e.setSettings(s); e.prepare(sr, 512);
                        std::vector<float> L = L0, R = R0;
                        run(e, L, R, 512);
                        const double d = rmsDb(L, R, (size_t)(2.0 * sr)) - ref;
                        lo = std::min(lo, d); hi = std::max(hi, d);
                    }
                CHECK(hi - lo < 2.0 && std::fabs(lo) < 2.0 && std::fabs(hi) < 2.0, "spray %.0f%%, pitch %+.0f: test mix level over density 5..90 /s and size 40..500 ms (2..45 at a time): %+.1f .. %+.1f dB",
                      spray, sp.second, lo, hi);
            }
        }
        // ---- the macros: glitchy = shorter, denser, more scattered grains; steady = no spray, reverse or spread
        {
            auto stats = [&](double mx, double my, double& rate, double& meanSize, double& delaySd, double& revShare, double& pitchSd) {
                Settings s = plain(); s.morphX = mx; s.morphY = my; s.spray = 20; s.reverse = 20; s.pitchSpread = 2; s.sizeMs = 120; s.density = 12;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)(10.0 * sr), 0.0f), R = L;
                run(e, L, R, 512);
                const uint64_t n = std::min<uint64_t>(e.spawnCount(), Engine::kLogN);
                double sz = 0, d1 = 0, d2 = 0, rv = 0, p2 = 0;
                for (uint64_t i = e.spawnCount() - n; i < e.spawnCount(); ++i)
                {
                    const auto& si = e.spawnLog(i);
                    sz += si.size; d1 += si.delay; d2 += (double)si.delay * si.delay; rv += si.reverse ? 1 : 0;
                    const double st = 12.0 * std::log2(si.ratio); p2 += st * st;
                }
                rate = e.spawnCount() / 10.0; meanSize = sz / n / sr * 1000.0;
                delaySd = std::sqrt(std::max(0.0, d2 / n - (d1 / n) * (d1 / n))) / sr * 1000.0;
                revShare = rv / n; pitchSd = std::sqrt(p2 / n);
            };
            double r0, s0, d0, v0, p0, r1, s1, d1, v1, p1, r2, s2, d2, v2, p2;
            stats(0, 0, r0, s0, d0, v0, p0);
            stats(100, 0, r1, s1, d1, v1, p1);
            stats(0, -100, r2, s2, d2, v2, p2);
            CHECK(r1 > 4.0 * r0 && s1 < s0 / 5.0 && d1 > d0 * 1.5, "morph X +100: %.0f -> %.0f grains/s, %.0f -> %.0f ms, spread of start %.0f -> %.0f ms", r0, r1, s0, s1, d0, d1);
            CHECK(d2 < 0.5 && v2 == 0.0 && p2 < 1e-3, "morph Y -100 (steady): start spread %.2f ms, %.0f %% reversed, pitch spread %.3f st", d2, 100.0 * v2, p2);
            double r3, s3, d3, v3, p3;
            stats(0, 100, r3, s3, d3, v3, p3);
            CHECK(d3 > 2.0 * d0 && v3 > 2.0 * v0 && p3 > 3.0 * p0, "morph Y +100 (wild): start spread %.0f -> %.0f ms, reversed %.0f -> %.0f %%, pitch spread %.1f -> %.1f st",
                  d0, d3, 100.0 * v0, 100.0 * v3, p0, p3);
        }
        // ---- feedback 100 % for 30 s stays bounded (unpitched and shimmering, dense)
        {
            for (double st : { 0.0, 12.0 })
            {
                Settings s; s.feedback = 100; s.pitch = st; s.density = 60; s.sizeMs = 200; s.spray = 5; s.space = 100; s.mix = 100;
                Engine e; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L(4096), R(4096);
                double peak = 0, lateRms = 0; size_t lateN = 0;
                bool finite = true;
                const size_t total = (size_t)(30.0 * sr);
                for (size_t pos = 0; pos < total; pos += 512)
                {
                    for (int i = 0; i < 512; ++i) { L[(size_t)i] = mix[(2 * (pos + i)) % mix.size()]; R[(size_t)i] = mix[(2 * (pos + i) + 1) % mix.size()]; }
                    e.process(L.data(), R.data(), 512);
                    for (int i = 0; i < 512; ++i)
                    {
                        if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                        peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                        if (pos > total - (size_t)(5 * sr)) { lateRms += (double)L[(size_t)i] * L[(size_t)i]; ++lateN; }
                    }
                }
                CHECK(finite && peak < 1.6, "feedback 100%%, pitch %+.0f, space 100%%, 30 s of the test mix (peaks 0.5): peak %.2f, last 5 s %.1f dBFS RMS",
                      st, peak, 10 * std::log10(lateRms / std::max<size_t>(1, lateN) + 1e-30));
            }
        }
        // ---- envelope edges are click-free: sparse grains of DC (one at a time), every shape, also reversed and
        // pitched, and with grains far shorter than the edges allow
        {
            const double bound = 0.5 * (kPi / 2.0) / (0.0015 * sr) * 1.3;   // DC 0.5 x the steepest 1.5 ms sin^2 edge
            double worst = 0;
            for (int env = 0; env < kNumEnvelopes; ++env)
                for (double size : { 5.0, 40.0, 300.0 })
                    for (double rev : { 0.0, 100.0 })
                    {
                        Settings s = plain(); s.envelope = env; s.sizeMs = size; s.density = std::min(3.0, 400.0 / size); s.reverse = rev; s.pitch = rev > 0 ? -5 : 7;
                        Engine e; e.setSettings(s); e.prepare(sr, 512);
                        std::vector<float> L((size_t)(4.0 * sr), 0.5f), R = L;
                        run(e, L, R, 512);
                        worst = std::max(worst, maxJump(L, (size_t)(0.6 * sr)));
                    }
            CHECK(worst < bound, "envelopes on DC 0.5 (all shapes, 5..300 ms, forward/reverse): largest step %.4f (bound %.4f; a hard edge would be 0.5)", worst, bound);
        }
        // ---- parameter moves don't click: a 100 Hz tone with every continuous control jumping every 50 ms
        {
            Settings s = plain(); s.sizeMs = 120; s.density = 25; s.positionMs = 80; s.filter = kFilterLP; s.space = 30;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(4.0 * sr)), R;
            for (size_t i = 0; i < L.size(); ++i) L[i] = (float)(0.4 * std::sin(kTwoPi * 100.0 * (double)i / sr));
            R = L;
            std::mt19937 rng(21); std::uniform_real_distribution<double> u(0.0, 1.0);
            for (size_t off = 0, k = 0; off < L.size(); off += 240, ++k)
            {
                if (k % 10 == 5)
                {
                    s.mix = u(rng) * 100; s.outDb = u(rng) * 12 - 6; s.cutoff = 200 * std::pow(50.0, u(rng)); s.resonance = u(rng) * 100;
                    s.space = u(rng) * 100; s.filter = (int)(u(rng) * kNumFilterModes); s.feedback = u(rng) * 60;
                    e.setSettings(s);
                }
                e.process(L.data() + off, R.data() + off, (int)std::min<size_t>(240, L.size() - off));
            }
            const double j = maxJump(L, (size_t)(0.5 * sr));
            CHECK(j < 0.08, "mix, output, filter mode/cutoff/resonance, space and feedback jumping every 50 ms on a 100 Hz tone: largest step %.3f", j);
        }
        // ---- mix 0: exactly the input (no latency)
        {
            Settings s; s.mix = 0; s.feedback = 80; s.pitch = 12; s.space = 100; s.filter = kFilterBP; s.density = 80;
            Engine e; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)(4.0 * sr)), R(L.size());
            for (size_t i = 0; i < L.size(); ++i) { L[i] = mix[(2 * i) % mix.size()]; R[i] = mix[(2 * i + 1) % mix.size()]; }
            const std::vector<float> L0 = L, R0 = R;
            run(e, L, R, 333);
            double err = 0;
            for (size_t i = 0; i < L.size(); ++i) err = std::max(err, (double)std::fabs(L[i] - L0[i]) + std::fabs(R[i] - R0[i]));
            CHECK(err == 0.0 && e.latency() == 0, "mix 0 = the input, sample for sample, latency %d (max error %.2g)", e.latency(), err);
        }
        // ---- stress: random settings, macros, block sizes, freeze and sync toggles, transports, absurd input
        {
            std::mt19937 rng(11);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 4096);
            bool finite = true; double peak = 0;
            std::vector<float> L(4096), R(4096);
            size_t pos = 0;
            double ppq = 0.0;
            for (int it = 0; it < 4000; ++it)
            {
                if (it % 7 == 0)
                {
                    s.positionMs = 4000 * std::pow(u(rng), 2.5); s.sizeMs = 5 * std::pow(200.0, u(rng)); s.density = std::pow(100.0, u(rng));
                    s.spray = u(rng) * 100; s.sync = u(rng) < 0.3; s.division = (int)(u(rng) * kNumNotes); s.envelope = (int)(u(rng) * kNumEnvelopes);
                    s.reverse = u(rng) * 100; s.stereo = u(rng) * 100; s.pitch = std::round(u(rng) * 48 - 24); s.pitchSpread = u(rng) * u(rng) * 24;
                    s.quantize = (int)(u(rng) * kNumScales); s.feedback = u(rng) * 100; s.filter = (int)(u(rng) * kNumFilterModes);
                    s.cutoff = 20 * std::pow(1000.0, u(rng)); s.resonance = u(rng) * 100; s.space = u(rng) * 100;
                    s.motionRate = 0.02 * std::pow(500.0, u(rng)); s.motionDepth = u(rng) * 100;
                    s.morphX = u(rng) * 200 - 100; s.morphY = u(rng) * 200 - 100; s.mix = u(rng) * 100; s.outDb = 0;
                    e.setSettings(s);
                }
                if (it % 11 == 3) { s.freeze = !s.freeze; e.setSettings(s); }
                const int bs = 1 + (int)(u(rng) * 1500);
                Transport t; t.bpm = 40 + u(rng) * 200; t.playing = u(rng) < 0.7; t.valid = u(rng) < 0.9;
                if (u(rng) < 0.02) ppq = u(rng) * 64.0;   // jumps
                t.ppq = ppq; ppq += bs / sr * t.bpm / 60.0;
                e.setTransport(t);
                for (int i = 0; i < bs; ++i)
                {
                    L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos;
                    if (it == 2000 && i == 0) L[0] = 1000.0f;   // absurd (but finite) input: must recover
                }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false;
                    if (it < 1990 || it > 2400) peak = std::max(peak, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            CHECK(finite && peak < 8.0, "random settings, macros, sizes, freeze/sync toggles and transports: finite, peak %.2f (outside a +60 dB spike's 10 s)", peak);
        }
        // ---- CPU
        {
            const double secs = 10.0;
            std::vector<float> L0((size_t)(secs * sr)), R0(L0.size());
            for (size_t i = 0; i < L0.size(); ++i) { L0[i] = mix[(2 * i) % mix.size()]; R0[i] = mix[(2 * i + 1) % mix.size()]; }
            struct Cfg { const char* name; Settings s; };
            Settings def;
            Settings max; max.density = 100; max.sizeMs = 1000; max.pitchSpread = 24; max.spray = 60; max.stereo = 100; max.reverse = 50;
            max.feedback = 50; max.filter = kFilterBP; max.resonance = 60; max.space = 100; max.motionDepth = 60;
            for (const Cfg& c : { Cfg { "default", def }, Cfg { "64 grains, every FX", max } })
            {
                Engine e; e.setSettings(c.s); e.prepare(sr, 512);
                std::vector<float> L = L0, R = R0;
                int most = 0;
                const auto t0 = std::chrono::steady_clock::now();
                for (size_t off = 0; off < L.size(); off += 512) { e.process(L.data() + off, R.data() + off, (int)std::min<size_t>(512, L.size() - off)); most = std::max(most, e.activeGrains()); }
                const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("  cpu: %s (up to %d grains): %.1f %% of one core\n", c.name, most, 100.0 * el / secs);
                if (c.s.density >= 100) CHECK(100.0 * el / secs < 10.0, "64 grains at 48 kHz under 10 %% of one core (%.1f %%)", 100.0 * el / secs);
            }
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
