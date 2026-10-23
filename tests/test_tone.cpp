// pump TONE engine: Tame pulls down a broad bump, Recover lifts a broad dip (pink noise with +8 dB at 3 kHz and
// -8 dB at 300 Hz); identity null at Recover = Tame = Brighten = Body = 0; auto level keeps the integrated loudness
// of the test mix within 1 LU; latency per quality (impulse) at 44.1/48/96 kHz; delta; noise is not lifted;
// parameter changes stay safe; a quality change taken back mid-switch ends on the requested size; NaN/Inf input
// never reaches the output and the analysis recovers.
#include "TestMix.hpp"
#include "../plugins/tone/ToneEngine.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Loudness.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::tone;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

struct Stereo { std::vector<float> L, R; };
static Stereo loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb"))
    {
        std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET);
        if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear();
        std::fclose(f);
    }
    Stereo s; s.L.resize(v.size() / 2); s.R.resize(v.size() / 2);
    for (size_t i = 0; i < s.L.size(); ++i) { s.L[i] = v[2 * i]; s.R[i] = v[2 * i + 1]; }
    return s;
}
static Stereo run(Engine& e, const Stereo& in, int block = 512)
{
    Stereo o = in;
    const int n = (int)o.L.size();
    for (int off = 0; off < n; off += block) e.process(o.L.data() + off, o.R.data() + off, std::min(block, n - off));
    return o;
}
static std::vector<float> pinkNoise(size_t n, unsigned seed, float gain)
{
    std::vector<float> v(n); std::mt19937 rng(seed); std::normal_distribution<float> nd(0, 1);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = nd(rng);
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
        v[i] = (float)(p * gain);
    }
    return v;
}
// broad bell (analog-matched) on a signal
static void bell(std::vector<float>& x, double sr, double f, double db, double q)
{
    BandSpec b; b.shape = Shape::Bell; b.freq = f; b.gainDb = db; b.q = q;
    const AnalogBand a = analogPrototype(b, sr);
    BiquadCoeffs c; bestDesign(a.sec[0], sr, c);
    BiquadState s;
    for (auto& v : x) v = (float)(s.process(c, v) * a.gain);
}
static std::vector<double> avgSpectrum(const std::vector<float>& x, size_t from, int N = 8192)
{
    RealFFT fft(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    std::vector<double> p((size_t)N / 2 + 1, 0.0);
    int frames = 0;
    for (size_t s = from; s + N <= x.size(); s += N / 2)
    {
        for (int i = 0; i < N; ++i) in[(size_t)i] = x[s + i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        fft.forward(in.data(), out.data());
        for (int k = 1; k < N / 2; ++k) p[(size_t)k] += (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
        ++frames;
    }
    for (auto& v : p) v /= std::max(1, frames);
    return p;
}
static double bandDb(const std::vector<double>& p, double f0, double f1, double sr, int N = 8192)
{
    double e = 0; int n = 0;
    for (int k = std::max(1, (int)(f0 * N / sr)); k <= (int)(f1 * N / sr) && k < (int)p.size(); ++k) { e += p[(size_t)k]; ++n; }
    return 10.0 * std::log10(e / std::max(1, n) + 1e-30);
}
static double nullDb(const std::vector<float>& a, const std::vector<float>& ref, size_t from, size_t lag)
{
    double e = 0, r = 0;
    for (size_t i = from; i < a.size(); ++i) { const double x = i >= lag ? ref[i - lag] : 0.0; e += (a[i] - x) * (a[i] - x); r += x * x; }
    return 10.0 * std::log10((e + 1e-30) / (r + 1e-30));
}
static double integrated(const Stereo& s, double sr, size_t from)
{
    LoudnessMeter m; m.prepare(sr);
    m.process(s.L.data() + from, s.R.data() + from, (int)(s.L.size() - from));
    return m.getIntegrated();
}

int main()
{
    const double sr = 48000.0;
    const size_t n = (size_t)(sr * 20);
    // ---- 1. bump and dip on pink noise ----
    printf("== pink noise with +8 dB at 3 kHz and -8 dB at 300 Hz (both about an octave wide)\n");
    Stereo shaped; shaped.L = pinkNoise(n, 1, 0.05f); shaped.R = pinkNoise(n, 2, 0.05f);
    for (auto* ch : { &shaped.L, &shaped.R }) { bell(*ch, sr, 3000, 8, 1.2); bell(*ch, sr, 300, -8, 1.2); }
    struct Case { const char* name; float rec, tame; };
    double bumpCut = 0, dipLift = 0;
    for (const Case& c : { Case { "Tame 100, Recover 0", 0, 100 }, Case { "Recover 100, Tame 0", 100, 0 }, Case { "defaults (30 / 30)", 30, 30 }, Case { "both 70", 70, 70 } })
    {
        Settings s; s.recover = c.rec; s.tame = c.tame; s.autoLevel = false;
        Engine e; e.set(s); e.prepare(sr);
        const Stereo o = run(e, shaped);
        const int lat = e.latency();
        // compare in[t] with out[t + latency] over the samples both have (no zero-padded tail: its step would leak)
        std::vector<float> in(n - lat), out(n - lat);
        for (size_t i = 0; i + lat < n; ++i) { in[i] = 0.5f * (shaped.L[i] + shaped.R[i]); out[i] = 0.5f * (o.L[i + lat] + o.R[i + lat]); }
        const size_t from = (size_t)(sr * 6);   // after the long-term spectrum has settled
        const auto a = avgSpectrum(in, from), b = avgSpectrum(out, from);
        const double bump = bandDb(b, 2500, 3600, sr) - bandDb(a, 2500, 3600, sr);
        const double dip = bandDb(b, 250, 360, sr) - bandDb(a, 250, 360, sr);
        const double ref = bandDb(b, 900, 1100, sr) - bandDb(a, 900, 1100, sr);
        const double hi = bandDb(b, 10000, 14000, sr) - bandDb(a, 10000, 14000, sr);
        // flatness: deviation of the output from a pink (flat) spectrum, 3k vs 1k and 300 vs 1k
        const double bumpBefore = (bandDb(a, 2500, 3600, sr) + 4.5 * std::log2(3000.0 / 1000)) - (bandDb(a, 900, 1100, sr));
        const double bumpAfter = (bandDb(b, 2500, 3600, sr) + 4.5 * std::log2(3000.0 / 1000)) - (bandDb(b, 900, 1100, sr));
        printf("  %-22s 3 kHz bump: %+5.2f dB   300 Hz dip: %+5.2f dB   1 kHz: %+5.2f dB   12 kHz: %+5.2f dB   (bump vs 1k: %.1f -> %.1f dB)\n",
               c.name, bump, dip, ref, hi, bumpBefore, bumpAfter);
        if (c.tame == 100 && c.rec == 0) { bumpCut = bump - ref; CHECK(bump - ref < -3.0 && dip - ref < 1.0, "Tame reduces the bump by %.2f dB relative to 1 kHz (dip left alone: %+.2f dB)", bump - ref, dip - ref); }
        if (c.rec == 100 && c.tame == 0) { dipLift = dip - ref; CHECK(dip - ref > 3.0 && bump - ref > -1.0, "Recover lifts the dip by %.2f dB relative to 1 kHz (bump left alone: %+.2f dB)", dip - ref, bump - ref); }
        if (c.rec == 30 && c.tame == 30) CHECK(bump - ref < -1.0 && dip - ref > 1.0, "defaults: bump %+.2f dB, dip %+.2f dB (relative to 1 kHz)", bump - ref, dip - ref);
    }
    (void)bumpCut; (void)dipLift;

    // ---- 1b. Brighten / Body: through the target (high Recover/Tame) and directly (both 0) ----
    printf("== Brighten / Body (pink noise, balance change vs. Brighten = Body = 0)\n");
    {
        Stereo pink; pink.L = pinkNoise(n, 11, 0.05f); pink.R = pinkNoise(n, 12, 0.05f);
        auto tiltOf = [&](float rec, float tame, float bright, float body, double& hiVsMid, double& loVsMid) {
            Settings s; s.recover = rec; s.tame = tame; s.brighten = bright; s.body = body; s.autoLevel = true;
            Engine e; e.set(s); e.prepare(sr);
            const Stereo o = run(e, pink);
            const int lat = e.latency();
            std::vector<float> in(n - lat), out(n - lat);
            for (size_t i = 0; i + lat < n; ++i) { in[i] = pink.L[i]; out[i] = o.L[i + lat]; }
            const auto a = avgSpectrum(in, (size_t)(sr * 6)), b = avgSpectrum(out, (size_t)(sr * 6));
            auto d = [&](double f0, double f1) { return bandDb(b, f0, f1, sr) - bandDb(a, f0, f1, sr); };
            hiVsMid = d(7000, 11000) - d(700, 1400); loVsMid = d(40, 90) - d(700, 1400);
        };
        for (float amt : { 0.0f, 70.0f })
        {
            double h0, l0, hb, lb, hd, ld;
            tiltOf(amt, amt, 0, 0, h0, l0);
            tiltOf(amt, amt, 100, 0, hb, lb);
            tiltOf(amt, amt, 0, 100, hd, ld);
            CHECK(hb - h0 > 3.0 && ld - l0 > 2.5, "Recover = Tame = %.0f: Brighten +100 lifts 7-11 kHz vs 1 kHz by %+.2f dB; Body +100 lifts 40-90 Hz by %+.2f dB",
                  amt, hb - h0, ld - l0);
        }
    }

    // ---- 2. identity ----
    printf("== identity\n");
    const Stereo mix = loadMix(pump_test_mix());
    if (mix.L.empty()) { printf("cannot read the test mix\n"); return 1; }
    for (int q = 0; q < 2; ++q)
        for (bool al : { false, true })
        {
            Settings s; s.recover = 0; s.tame = 0; s.brighten = 0; s.body = 0; s.quality = q; s.autoLevel = al;
            Engine e; e.set(s); e.prepare(sr);
            const Stereo o = run(e, mix, 300);
            const double nl = nullDb(o.L, mix.L, (size_t)e.latency() + 10, (size_t)e.latency()), nr = nullDb(o.R, mix.R, (size_t)e.latency() + 10, (size_t)e.latency());
            CHECK(nl < -100 && nr < -100, "Recover = Tame = Brighten = Body = 0 (quality %s, auto level %s): null vs delayed input %.1f / %.1f dB", kQualityNames[q], al ? "on" : "off", nl, nr);
        }

    // ---- 3. auto level keeps the loudness ----
    printf("== auto level (integrated loudness of the test mix)\n");
    {
        const double lin = integrated(mix, sr, 0);
        struct LC { const char* name; Settings s; };
        std::vector<LC> lc;
        { LC c { "defaults", Settings() }; lc.push_back(c); }
        { LC c { "Recover 100 / Tame 0", Settings() }; c.s.recover = 100; c.s.tame = 0; lc.push_back(c); }
        { LC c { "Tame 100 / Recover 0", Settings() }; c.s.recover = 0; c.s.tame = 100; lc.push_back(c); }
        { LC c { "Brighten +100", Settings() }; c.s.brighten = 100; lc.push_back(c); }
        { LC c { "Body +100, Brighten -50", Settings() }; c.s.body = 100; c.s.brighten = -50; lc.push_back(c); }
        { LC c { "everything 100, fast", Settings() }; c.s.recover = 100; c.s.tame = 100; c.s.brighten = 100; c.s.body = 100; c.s.speed = 100; lc.push_back(c); }
        for (auto& c : lc)
        {
            for (bool al : { true, false })
            {
                Settings s = c.s; s.autoLevel = al;
                Engine e; e.set(s); e.prepare(sr);
                const Stereo o = run(e, mix);
                Stereo al2; al2.L.assign(o.L.begin() + e.latency(), o.L.end()); al2.R.assign(o.R.begin() + e.latency(), o.R.end());
                const double lout = integrated(al2, sr, 0);
                if (al) CHECK(std::fabs(lout - lin) < 1.0, "%-26s auto level on:  %.2f LUFS -> %.2f LUFS (%+.2f LU)", c.name, lin, lout, lout - lin);
                else printf("        %-26s auto level off: %+.2f LU\n", c.name, lout - lin);
            }
        }
    }

    // ---- 4. latency ----
    printf("== latency\n");
    for (double r : { 44100.0, 48000.0, 96000.0 })
        for (int q = 0; q < 2; ++q)
        {
            Settings s; s.quality = q; s.recover = 0; s.tame = 0;
            Engine e; e.set(s); e.prepare(r);
            const int len = 3 * e.latency() + 1000;
            Stereo x; x.L.assign((size_t)len, 0.0f); x.R.assign((size_t)len, 0.0f);
            x.L[400] = 1.0f; x.R[400] = -0.5f;
            const Stereo o = run(e, x, 128);
            int pk = 0; float pv = 0; for (int i = 0; i < len; ++i) if (std::fabs(o.L[(size_t)i]) > pv) { pv = std::fabs(o.L[(size_t)i]); pk = i; }
            CHECK(pk - 400 == e.latency() && std::fabs(pv - 1.0f) < 1e-4f && std::fabs(o.R[(size_t)pk] + 0.5f) < 1e-4f, "%.0f Hz quality %s: impulse at +%d (reported %d)", r, kQualityNames[q], pk - 400, e.latency());
        }

    // ---- 5. delta and mix ----
    printf("== delta / mix\n");
    {
        Settings s; s.recover = 70; s.tame = 70;
        Engine a, b; a.set(s); a.prepare(sr); s.delta = true; b.set(s); b.prepare(sr);
        const Stereo oa = run(a, mix), ob = run(b, mix);
        std::vector<float> sum(oa.L.size());
        for (size_t i = 0; i < sum.size(); ++i) sum[i] = oa.L[i] + ob.L[i];
        const double nl = nullDb(sum, mix.L, (size_t)a.latency() + 10, (size_t)a.latency());
        CHECK(nl < -100, "delta + output == delayed input: %.1f dB", nl);
        Settings m; m.recover = 80; m.tame = 80; m.brighten = 50; m.mix = 0;
        Engine c; c.set(m); c.prepare(sr);
        const Stereo oc = run(c, mix);
        const double nm = nullDb(oc.L, mix.L, (size_t)c.latency() + 10, (size_t)c.latency());
        CHECK(nm < -100, "mix 0%%: null vs delayed input %.1f dB (latency-aligned dry)", nm);
    }

    // ---- 6. never lift noise: a quiet hiss under a band-limited signal stays where it is ----
    printf("== noise gate\n");
    {
        // a 200 Hz - 4 kHz band of pink noise at a normal level, plus white hiss 60 dB down
        Stereo x; x.L = pinkNoise(n, 5, 0.08f); x.R = x.L;
        BandSpec lc; lc.shape = Shape::LowCut; lc.freq = 200; lc.slope = 48; lc.q = 0.707;
        BandSpec hc; hc.shape = Shape::HighCut; hc.freq = 4000; hc.slope = 48; hc.q = 0.707;
        for (const BandSpec& b : { lc, hc })
        {
            const AnalogBand a = analogPrototype(b, sr);
            for (int k = 0; k < a.n; ++k) { BiquadCoeffs c; bestDesign(a.sec[k], sr, c); BiquadState s1; for (auto& v : x.L) v = (float)s1.process(c, v); }
        }
        x.R = x.L;
        std::mt19937 rng(9); std::normal_distribution<float> nd(0, 1);
        for (size_t i = 0; i < n; ++i) { const float h = 0.0002f * nd(rng); x.L[i] += h; x.R[i] += h; }
        for (int q = 0; q < 2; ++q)
        {
            Settings s; s.recover = 100; s.tame = 0; s.autoLevel = false; s.quality = q;
            Engine e; e.set(s); e.prepare(sr);
            Stereo o = x;
            float maxGated = 0.0f;   // the largest gain the engine applies to the bands that hold only hiss
            for (size_t off = 0; off + 512 <= n; off += 512)
            {
                e.process(o.L.data() + off, o.R.data() + off, 512);
                if (off > (size_t)(sr * 6))
                    for (int b = 0; b < Engine::kBands; ++b)
                        if (e.bandFreq(b) < 70.0 || e.bandFreq(b) > 7000.0) maxGated = std::max(maxGated, e.uiGain(b));
            }
            const int lat = e.latency();
            std::vector<float> in(n - lat), out(n - lat);
            for (size_t i = 0; i + lat < n; ++i) { in[i] = x.L[i]; out[i] = o.L[i + lat]; }
            const auto a = avgSpectrum(in, (size_t)(sr * 6)), b = avgSpectrum(out, (size_t)(sr * 6));
            const double hiss = bandDb(b, 10000, 16000, sr) - bandDb(a, 10000, 16000, sr);
            const double sub = bandDb(b, 30, 60, sr) - bandDb(a, 30, 60, sr);
            // the bands of hiss get no gain at all; what the output shows at 30-60 Hz is the STFT's leakage from
            // lifting the low cut's skirt (100-200 Hz), 60+ dB below the music, and it shrinks with the FFT size
            CHECK(maxGated < 0.1f && hiss < 0.5 && sub < (q == 0 ? 4.0 : 1.0),
                  "Recover 100, FFT %s: hiss 60 dB below the music gets %+.2f dB gain at most; measured %+.2f dB at 10-16 kHz, %+.2f dB at 30-60 Hz (leakage)",
                  kQualityNames[q], maxGated, hiss, sub);
        }
    }

    // ---- 7. musical, not swirly: frame-to-frame gain movement on steady material ----
    printf("== stability\n");
    {
        Stereo x; x.L = pinkNoise(n, 7, 0.05f); x.R = pinkNoise(n, 8, 0.05f);
        for (auto* ch : { &x.L, &x.R }) { bell(*ch, sr, 3000, 8, 1.2); bell(*ch, sr, 300, -8, 1.2); }
        for (float speed : { 0.0f, 40.0f, 100.0f })
        {
            Settings s; s.recover = 70; s.tame = 70; s.speed = speed;
            Engine e; e.set(s); e.prepare(sr);
            std::vector<float> prev(Engine::kBands, 0.0f);
            double mov = 0; int cnt = 0;
            for (size_t off = 0; off + 512 <= n; off += 512)
            {
                e.process(x.L.data() + off, x.R.data() + off, 512);
                if (off < (size_t)(sr * 6)) { for (int b = 0; b < Engine::kBands; ++b) prev[(size_t)b] = e.uiGain(b); continue; }
                for (int b = 0; b < Engine::kBands; ++b) { mov += std::fabs(e.uiGain(b) - prev[(size_t)b]); prev[(size_t)b] = e.uiGain(b); }
                ++cnt;
            }
            const double perSec = mov / std::max(1, cnt) / Engine::kBands * (sr / 512);
            CHECK(perSec < (speed >= 100 ? 6.0 : 2.5), "speed %3.0f%%: average gain movement on steady noise %.2f dB/s per band", speed, perSec);
        }
    }

    // ---- 8. random automation incl. quality switches ----
    printf("== parameter changes\n");
    {
        Engine e; e.prepare(sr);
        std::mt19937 rng(4);
        auto U = [&](float a, float b) { return a + (b - a) * (float)((rng() % 10000) / 9999.0); };
        Stereo x = mix;
        bool finite = true; float peak = 0;
        for (int off = 0, blk = 0; off + 256 <= (int)x.L.size(); off += 256, ++blk)
        {
            if (blk % 5 == 0)
            {
                Settings s; s.recover = U(0, 100); s.tame = U(0, 100); s.bias = U(-100, 100); s.brighten = U(-100, 100); s.body = U(-100, 100);
                s.speed = U(0, 100); s.autoLevel = rng() % 2; s.outDb = U(-12, 12); s.mix = U(0, 100); s.delta = rng() % 6 == 0;
                s.quality = blk % 77 == 0 ? (int)(rng() % 2) : e.settings().quality;
                e.set(s);
            }
            e.process(x.L.data() + off, x.R.data() + off, 256);
            for (int i = off; i < off + 256; ++i) { if (!std::isfinite(x.L[(size_t)i])) finite = false; peak = std::max(peak, std::fabs(x.L[(size_t)i])); }
        }
        CHECK(finite && peak < 8.0f, "random automation: finite, peak %.3f", peak);
    }

    // ---- 8b. quality switched back while a switch is under way: the engine ends on the size asked for ----
    printf("== quality toggle-back\n");
    {
        // identity settings, so after settling the output must null against the input delayed by the reported
        // latency (proves the engine really runs at that size, not just that it says so)
        const int fade = (int)(0.005 * sr);   // the 5 ms fade-out before a switch
        bool allOk = true;
        int worstGap = -1;
        for (int from = 0; from < 2; ++from)
            for (int gap : { 0, 1, 100, fade - 1, fade, fade + 1, 2 * fade, 1000, 2048, 2048 + fade, 2600, 4096, 4096 + fade, 4600, 6000 })
            {
                Settings s; s.recover = 0; s.tame = 0; s.quality = from;
                Engine e; e.set(s); e.prepare(sr);
                Stereo x = mix;
                size_t off = 0;
                auto proc = [&](int m) { m = std::min<int>(m, (int)(x.L.size() - off)); if (m > 0) e.process(x.L.data() + off, x.R.data() + off, m); off += (size_t)std::max(0, m); };
                proc(1024);
                s.quality = 1 - from; e.set(s);                  // click the other size ...
                for (int left = gap; left > 0; left -= 512) proc(std::min(512, left));
                s.quality = from; e.set(s);                      // ... and back, `gap` samples later
                const size_t until = off + (size_t)(1.5 * sr);
                while (off < until) proc(512);
                const int lat = e.latency();
                const size_t from0 = off - (size_t)(0.5 * sr);
                double err = 0, ref = 0;
                for (size_t i = from0; i < off; ++i) { const double d = x.L[i] - mix.L[i - (size_t)lat]; err += d * d; ref += (double)mix.L[i - (size_t)lat] * mix.L[i - (size_t)lat]; }
                const double nl = 10.0 * std::log10((err + 1e-30) / (ref + 1e-30));
                const bool ok = e.fftSize() == e.sizeFor(from) && lat == e.sizeFor(from) && nl < -100.0;
                if (!ok) { allOk = false; worstGap = gap; printf("    %s -> %s -> %s, back after %d samples: engine FFT %d, latency %d, null %.1f dB\n",
                                                                   kQualityNames[from], kQualityNames[1 - from], kQualityNames[from], gap, e.fftSize(), lat, nl); }
            }
        CHECK(allOk, "switching back 0..6000 samples after a quality change (incl. the block right after the fade-out): engine always ends on the requested size and nulls at its latency%s",
              worstGap >= 0 ? " (see above)" : "");
    }

    // ---- 8c. one NaN / Inf input sample: no non-finite output, and the analysis recovers (it used to freeze at 0 dB) ----
    printf("== NaN / Inf input\n");
    {
        const int secs = 12, blk = 512, poisonBlock = 100;   // poison at ~1.07 s
        auto runPoison = [&](int kind, std::vector<float>& gainOut, bool& finiteOut, bool& uiFinite) {
            Settings s; s.recover = 60; s.tame = 60;
            Engine e; e.set(s); e.prepare(sr);
            std::mt19937 rng(1); std::normal_distribution<float> nd(0.0f, 0.1f);
            std::vector<float> L((size_t)blk), R((size_t)blk);
            long t = 0; finiteOut = true;
            for (int k = 0; k < (int)(secs * sr / blk); ++k)
            {
                for (int i = 0; i < blk; ++i, ++t) { const float tone = 0.3f * (float)std::sin(kTwoPi * 3000.0 * t / sr); L[(size_t)i] = nd(rng) + tone; R[(size_t)i] = nd(rng) + tone; }
                if (k == poisonBlock)
                {
                    if (kind == 1) L[7] = NAN;
                    if (kind == 2) R[300] = -INFINITY;
                    if (kind == 3) for (int i = 0; i < blk; ++i) { L[(size_t)i] = NAN; R[(size_t)i] = 1e30f; }
                }
                e.process(L.data(), R.data(), blk);
                for (int i = 0; i < blk; ++i) if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finiteOut = false;
            }
            gainOut.assign(Engine::kBands, 0.0f); uiFinite = std::isfinite(e.uiMakeup());
            for (int b = 0; b < Engine::kBands; ++b)
            {
                gainOut[(size_t)b] = e.uiGain(b);
                if (!std::isfinite(e.uiGain(b)) || !std::isfinite(e.uiTarget(b)) || !std::isfinite(e.uiLevel(b))) uiFinite = false;
            }
        };
        std::vector<float> clean, poisoned; bool fin = true, uiFin = true;
        runPoison(0, clean, fin, uiFin);
        float cleanSpan = 0; for (float g : clean) cleanSpan = std::max(cleanSpan, std::fabs(g));
        for (int kind = 1; kind <= 3; ++kind)
        {
            runPoison(kind, poisoned, fin, uiFin);
            float diff = 0; for (int b = 0; b < Engine::kBands; ++b) diff = std::max(diff, std::fabs(poisoned[(size_t)b] - clean[(size_t)b]));
            static const char* const names[] = { "", "one NaN sample", "one -Inf sample", "a block of NaN / 1e30" };
            CHECK(fin && uiFin && diff < 0.5f && cleanSpan > 1.0f,
                  "%-22s at 1.07 s: output finite throughout, UI feeds finite, gain curve %.0f s later within %.3f dB of the clean run (which works up to %.1f dB)",
                  names[kind], secs - 1.07, diff, cleanSpan);
        }
    }

    // ---- 9. CPU ----
    printf("== cpu\n");
    for (int q = 0; q < 2; ++q)
    {
        Settings s; s.quality = q;
        Engine e; e.set(s); e.prepare(sr);
        const auto t0 = std::chrono::steady_clock::now();
        run(e, mix);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  quality %s: %.1f%% of one core\n", kQualityNames[q], 100.0 * sec / (mix.L.size() / sr));
    }
    // a quality switch only flips tables built in prepare(): its block costs about what a block with a frame does
    // (informational: timings on a shared machine are too noisy for a pass/fail bound)
    for (double r : { 48000.0, 192000.0 })
    {
        auto us = [](std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count(); };
        std::vector<double> sw, fr;
        for (int rep = 0; rep < 9; ++rep)
        {
            Settings s; s.recover = 50; s.tame = 50;
            Engine e; e.set(s); e.prepare(r);
            std::vector<float> L(64), R(64);
            std::mt19937 rng(3); std::normal_distribution<float> nd(0, 0.1f);
            auto fill = [&]() { for (int i = 0; i < 64; ++i) { L[(size_t)i] = nd(rng); R[(size_t)i] = nd(rng); } };
            for (int k = 0; k < (int)(r / 64); ++k) { fill(); e.process(L.data(), R.data(), 64); }
            double mxN = 0; for (int k = 0; k < 600; ++k) { fill(); const auto t0 = std::chrono::steady_clock::now(); e.process(L.data(), R.data(), 64); mxN = std::max(mxN, us(t0)); }
            s.quality = 1; e.set(s);
            double mxS = 0;
            for (int k = 0; k < 600; ++k)
            {
                fill();
                const int before = e.fftSize();
                const auto t0 = std::chrono::steady_clock::now();
                e.process(L.data(), R.data(), 64);
                const double t = us(t0);
                if (e.fftSize() != before) mxS = t;   // the block that switched
            }
            sw.push_back(mxS); fr.push_back(mxN);
        }
        std::sort(sw.begin(), sw.end()); std::sort(fr.begin(), fr.end());
        printf("  %.0f Hz, quality switch: the switching 64-sample block takes %.0f us (median of 9), the worst normal block %.0f us; block period %.0f us\n",
               r, sw[sw.size() / 2], fr[fr.size() / 2], 64e6 / r);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
