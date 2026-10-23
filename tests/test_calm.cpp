// pump CALM engine: resonance reduction on the test mix (1.2 kHz and 2.9 kHz resonances vs broadband), depth 0 null,
// delta + output = delayed input, latency per quality (impulse), stereo modes, sample rates, parameter-change safety.
#include "TestMix.hpp"
#include "../plugins/calm/CalmEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>

using namespace pump;
using namespace pump::calm;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb"))
    {
        std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET);
        if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear();
        std::fclose(f);
    }
    return v;
}

struct Stereo { std::vector<float> L, R; };
static Stereo split(const std::vector<float>& il)
{
    Stereo s; s.L.resize(il.size() / 2); s.R.resize(il.size() / 2);
    for (size_t i = 0; i < s.L.size(); ++i) { s.L[i] = il[2 * i]; s.R[i] = il[2 * i + 1]; }
    return s;
}
template <class P> static Stereo run(P& e, const Stereo& in, int block = 512)
{
    Stereo o = in;
    const int n = (int)o.L.size();
    for (int off = 0; off < n; off += block) e.process(o.L.data() + off, o.R.data() + off, std::min(block, n - off));
    return o;
}
// simple linear resampler for the 44.1/96 kHz runs
static Stereo resample(const Stereo& in, double ratio)
{
    Stereo o;
    const size_t n = (size_t)(in.L.size() * ratio);
    o.L.resize(n); o.R.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double x = i / ratio; const size_t k = (size_t)x; const double t = x - k;
        const size_t k1 = std::min(k + 1, in.L.size() - 1);
        o.L[i] = (float)(in.L[k] * (1 - t) + in.L[k1] * t); o.R[i] = (float)(in.R[k] * (1 - t) + in.R[k1] * t);
    }
    return o;
}

// Average spectrum (power per bin, Hann, N = 8192) of a signal from sample `from`.
// (the last 20000 samples are left out: outputs are compared at a latency offset, so their tails are padding)
static std::vector<double> avgSpectrum(const std::vector<float>& x, size_t from, double sr, int N = 8192)
{
    const size_t end = x.size() > 40000 ? x.size() - 20000 : x.size();
    RealFFT fft(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    std::vector<double> p((size_t)N / 2 + 1, 0.0);
    int frames = 0;
    for (size_t s = from; s + N <= end; s += N / 2)
    {
        for (int i = 0; i < N; ++i) in[(size_t)i] = x[s + i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        fft.forward(in.data(), out.data());
        for (int k = 1; k < N / 2; ++k) p[(size_t)k] += (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)];
        ++frames;
    }
    for (auto& v : p) v /= std::max(1, frames);
    (void)sr;
    return p;
}
// energy-ratio (dB, positive = reduced) between two spectra over [f0, f1]
static double bandReduction(const std::vector<double>& a, const std::vector<double>& b, double f0, double f1, double sr, int N = 8192)
{
    double ea = 0, eb = 0;
    for (int k = std::max(1, (int)(f0 * N / sr)); k <= (int)(f1 * N / sr) && k < (int)a.size(); ++k) { ea += a[(size_t)k]; eb += b[(size_t)k]; }
    return 10.0 * std::log10((ea + 1e-30) / (eb + 1e-30));
}


// Thunder Plus's Suppressor (master-worklet.js), ported 1:1 as the reference to beat: N 2048, its default settings.
struct ThunderRef {
    int N = 2048, H = 512, K = 1025; double sr = 48000;
    RealFFT fft { 2048 };
    std::vector<float> win, inL, inR, pw, env, red, redS, tmp, fw, db; std::vector<double> pre;
    AlignedBuffer fr { 2048 }, sp { 2048 }, frR { 2048 }, spR { 2048 };
    std::vector<float> olaL, olaR; int w = 0, cnt = 0;
    float depth = 0.5f, sharp = 0.5f, sel = 3, atk = 8, rel = 90, lo = 150, hi = 12000;
    ThunderRef()
    {
        win.resize(N); for (int i = 0; i < N; ++i) win[i] = (float)std::sqrt(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        inL.assign(N, 0); inR.assign(N, 0); pw.assign(K, 0); env.assign(K, 0); red.assign(K, 0); redS.assign(K, 0); tmp.assign(K, 0); fw.assign(K, 0); db.assign(K, 0); pre.assign(K + 1, 0);
        olaL.assign(N, 0); olaR.assign(N, 0);
        for (int k = 0; k < K; ++k) { const double f = std::max(1.0, k * sr / N), a = std::log2(f / lo), b = std::log2(hi / f); fw[k] = (float)(clampv(a * 2 + 1, 0.0, 1.0) * clampv(b * 2 + 1, 0.0, 1.0)); }
    }
    void process(float* L, float* R, int n)   // latency N (the JS adds one hop for its scheduling; not needed here)
    {
        for (int i = 0; i < n; ++i)
        {
            inL[w] = L[i]; inR[w] = R[i]; w = (w + 1) % N;
            L[i] = olaL[cnt]; R[i] = olaR[cnt];
            if (++cnt == H) { cnt = 0; frame(); }
        }
    }
    void frame()
    {
        for (int i = 0; i < N; ++i) { fr[i] = inL[(w + i) % N] * win[i]; frR[i] = inR[(w + i) % N] * win[i]; }
        fft.forward(fr.data(), sp.data()); fft.forward(frR.data(), spR.data());
        auto pwr = [&](AlignedBuffer& s, int k) { return k == 0 ? s[0] * s[0] : (k == K - 1 ? s[1] * s[1] : s[2 * k] * s[2 * k] + s[2 * k + 1] * s[2 * k + 1]); };
        const double av = 1 - std::exp(-(H / sr) / 0.06);
        for (int k = 0; k < K; ++k) { const double m = (pwr(sp, k) + pwr(spR, k)) * 0.5; pw[k] += (float)((m - pw[k]) * av); db[k] = (float)(10 * std::log10(pw[k] + 1e-20)); }
        const double oct = 0.25 + (1 - sharp) * 1.25, half = std::pow(2.0, oct / 2);
        pre[0] = 0; for (int k = 0; k < K; ++k) pre[k + 1] = pre[k] + db[k];
        for (int k = 1; k < K; ++k) { const int a = std::max(1, (int)std::floor(k / half)), b = std::min(K - 1, (int)std::ceil(k * half)); env[k] = (float)((pre[b + 1] - pre[a]) / (b - a + 1)); }
        env[0] = env[1];
        const float maxCut = 18 * depth + 2, sc = 0.35f + 1.15f * depth;
        for (int k = 0; k < K; ++k) { const float ex = db[k] - env[k] - sel; tmp[k] = ex > 0 ? std::min(maxCut, ex * sc) * fw[k] : 0; }
        const int rad = (int)std::lround(1 + (1 - sharp) * 5);
        for (int k = 0; k < K; ++k) { float m = 0; for (int j = std::max(0, k - rad); j <= std::min(K - 1, k + rad); ++j) { const float v = tmp[j] * (1 - std::fabs((float)(j - k)) / (rad + 1)); if (v > m) m = v; } red[k] = m; }
        const double frs = H / sr, ca = std::exp(-frs / std::max(0.001, atk / 1000.0)), cr = std::exp(-frs / std::max(0.005, rel / 1000.0));
        for (int k = 0; k < K; ++k) { const float t = red[k]; const double c = t > redS[k] ? ca : cr; redS[k] = (float)(t + (redS[k] - t) * c); }
        for (int k = 0; k < K; ++k)
        {
            const float g = (float)std::pow(10.0, -redS[k] / 20.0);
            if (k == 0) { sp[0] *= g; spR[0] *= g; } else if (k == K - 1) { sp[1] *= g; spR[1] *= g; }
            else { sp[2 * k] *= g; sp[2 * k + 1] *= g; spR[2 * k] *= g; spR[2 * k + 1] *= g; }
        }
        fft.inverse(sp.data(), fr.data()); fft.inverse(spR.data(), frR.data());
        // shift the overlap-add buffer by one hop, then add this frame
        std::memmove(olaL.data(), olaL.data() + H, sizeof(float) * (N - H)); std::memset(olaL.data() + N - H, 0, sizeof(float) * H);
        std::memmove(olaR.data(), olaR.data() + H, sizeof(float) * (N - H)); std::memset(olaR.data() + N - H, 0, sizeof(float) * H);
        for (int i = 0; i < N; ++i) { olaL[i] += fr[i] * win[i] * 0.5f / N; olaR[i] += frR[i] * win[i] * 0.5f / N; }
    }
};
// pink noise (Paul Kellet's filter)
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
// mean of per-third-octave reductions (dB) from 100 Hz to 16 kHz, leaving out the test mix's two resonances
static double elsewhere(const std::vector<double>& a, const std::vector<double>& b, double sr)
{
    double s = 0; int n = 0;
    for (double f = 100; f < 16000; f *= 1.2599)
    {
        const double f1 = f * 1.2599;
        if ((f1 > 1050 && f < 1350) || (f1 > 2650 && f < 3150)) continue;
        s += bandReduction(a, b, f, f1, sr); ++n;
    }
    return s / n;
}

static double nullDb(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t lagB)
{
    double e = 0, r = 0;
    for (size_t i = from; i < a.size(); ++i)
    {
        const double ref = i >= lagB ? b[i - lagB] : 0.0;
        e += (a[i] - ref) * (a[i] - ref); r += ref * ref;
    }
    return 10.0 * std::log10((e + 1e-30) / (r + 1e-30));
}

int main()
{
    const std::vector<float> il = loadMix(pump_test_mix());
    if (il.empty()) { printf("cannot read %s\n", pump_test_mix()); return 1; }
    const Stereo mix48 = split(il);
    printf("test mix: %zu samples (%.1f s)\n", mix48.L.size(), mix48.L.size() / 48000.0);

    // ---- 1. resonances on the test mix ----
    printf("== resonance reduction on the test mix (48 kHz)\n");
    struct Case { const char* name; Settings s; };
    std::vector<Case> cases;
    { Case c { "default (depth 50)", Settings() }; cases.push_back(c); }
    { Case c { "depth 80 / soft", Settings() }; c.s.depth = 80; cases.push_back(c); }
    { Case c { "depth 80 / hard", Settings() }; c.s.depth = 80; c.s.mode = kHard; cases.push_back(c); }
    { Case c { "depth 30 / 8192", Settings() }; c.s.depth = 30; c.s.quality = 3; cases.push_back(c); }
    for (auto& c : cases)
    {
        Engine e; e.set(c.s); e.prepare(48000);
        const Stereo out = run(e, mix48);
        const int lat = e.latency();
        // align: compare out[t] with in[t - lat]
        std::vector<float> inM(mix48.L.size()), outM(mix48.L.size());
        for (size_t i = 0; i < inM.size(); ++i) { inM[i] = 0.5f * (mix48.L[i] + mix48.R[i]); outM[i] = i + lat < out.L.size() ? 0.5f * (out.L[i + lat] + out.R[i + lat]) : 0.0f; }
        const size_t from = 48000;   // skip the first second (detectors settle)
        const auto sIn = avgSpectrum(inM, from, 48000), sOut = avgSpectrum(outM, from, 48000);
        const double r12 = bandReduction(sIn, sOut, 1150, 1250, 48000), r29 = bandReduction(sIn, sOut, 2780, 3020, 48000);
        const double rAll = bandReduction(sIn, sOut, 20, 20000, 48000);
        // broadband excluding the two resonances' octaves (what "everything else" loses)
        const double rLow = bandReduction(sIn, sOut, 60, 800, 48000), rMid = bandReduction(sIn, sOut, 1500, 2400, 48000), rHi = bandReduction(sIn, sOut, 4000, 16000, 48000);
        printf("  %-20s 1.2k: %5.2f dB   2.9k: %5.2f dB   broadband: %5.2f dB  (60-800: %.2f, 1.5-2.4k: %.2f, 4-16k: %.2f)\n",
               c.name, r12, r29, rAll, rLow, rMid, rHi);
        const double minAbs = c.s.depth >= 50 ? 2.5 : 1.5;   // light settings take less, but still selectively
        CHECK(r12 > rAll + 1.5 && r29 > rAll + 1.5 && r12 > minAbs && r29 > minAbs && std::min(r12, r29) > 3.0 * rAll,
              "%s: resonances reduced clearly more than broadband (1.2k %.2f, 2.9k %.2f vs %.2f dB: %.1fx)", c.name, r12, r29, rAll, std::min(r12, r29) / std::max(rAll, 1e-3));
    }

    // ---- 1b. against Thunder Plus's suppressor (the starting point), on the mix and on pink noise ----
    printf("== versus Thunder Plus's Suppressor (selectivity: resonance cut / cut elsewhere)\n");
    {
        auto measure = [&](auto& proc, int lat, const Stereo& x, double& r12, double& r29, double& rel) {
            const Stereo o = run(proc, x);
            std::vector<float> a(x.L.size()), b(x.L.size());
            for (size_t i = 0; i < a.size(); ++i) { a[i] = 0.5f * (x.L[i] + x.R[i]); const size_t j = std::min(o.L.size() - 1, i + lat); b[i] = 0.5f * (o.L[j] + o.R[j]); }
            const auto A = avgSpectrum(a, 48000, 48000), B = avgSpectrum(b, 48000, 48000);
            r12 = bandReduction(A, B, 1150, 1250, 48000); r29 = bandReduction(A, B, 2780, 3020, 48000); rel = elsewhere(A, B, 48000);
        };
        Stereo pink; pink.L = pinkNoise(mix48.L.size(), 3, 0.05f); pink.R = pinkNoise(mix48.L.size(), 4, 0.05f);
        double t12, t29, tel, tpk, a, b2;
        { ThunderRef t; measure(t, 2048, mix48, t12, t29, tel); }
        { ThunderRef t; measure(t, 2048, pink, a, b2, tpk); }
        printf("  Thunder (defaults)      1.2k %5.2f  2.9k %5.2f  elsewhere %5.2f  pink noise %5.2f dB   selectivity %.1fx\n", t12, t29, tel, tpk, 0.5 * (t12 + t29) / tel);
        for (float depth : { 30.0f, 50.0f, 80.0f })
        {
            Settings s; s.depth = depth;
            double c12, c29, cel, cpk;
            { Engine e; e.set(s); e.prepare(48000); measure(e, e.latency(), mix48, c12, c29, cel); }
            { Engine e; e.set(s); e.prepare(48000); measure(e, e.latency(), pink, a, b2, cpk); }
            printf("  CALM depth %3.0f          1.2k %5.2f  2.9k %5.2f  elsewhere %5.2f  pink noise %5.2f dB   selectivity %.1fx\n", depth, c12, c29, cel, cpk, 0.5 * (c12 + c29) / cel);
            if (depth == 50.0f)
                CHECK(0.5 * (c12 + c29) / cel > 1.8 * 0.5 * (t12 + t29) / tel && cpk < 0.25,
                      "CALM (defaults) is far more selective than Thunder's suppressor: %.1fx vs %.1fx; pink noise (no resonances) %.2f dB vs %.2f dB",
                      0.5 * (c12 + c29) / cel, 0.5 * (t12 + t29) / tel, cpk, tpk);
        }
    }

    // ---- 1c. smoothness: how much the reduction flutters from frame to frame (warble), CALM vs Thunder ----
    printf("== smoothness on the test mix (frame-to-frame change of the reduction, 100 Hz - 16 kHz)\n");
    {
        // relative flutter = mean |change per hop| / mean reduction, on the same 220 log-spaced display points for
        // both (max over the bins in each point, as each plugin draws it), 100 Hz - 16 kHz, where reduction is active
        const int P = Engine::kUiPoints;
        int p0 = 0, p1 = P - 1;
        while (Engine::uiPointFreq(p0) < 100.0) ++p0;
        while (Engine::uiPointFreq(p1) > 16000.0) --p1;
        auto flutterOf = [&](auto getRed, auto& proc, double& meanRed, double& rel) {
            Stereo x = mix48;
            std::vector<float> prev((size_t)P, 0.0f);
            double sumD = 0, sumR = 0; long cnt = 0;
            for (size_t off = 0; off + 512 <= x.L.size(); off += 512)
            {
                proc.process(x.L.data() + off, x.R.data() + off, 512);
                for (int p = p0; p <= p1; ++p)
                {
                    const float r = getRed(p);
                    if (off >= 48000 && (r > 0.25f || prev[(size_t)p] > 0.25f)) { sumD += std::fabs(r - prev[(size_t)p]); sumR += r; ++cnt; }
                    prev[(size_t)p] = r;
                }
            }
            meanRed = sumR / std::max(1L, cnt); rel = sumD / std::max(1e-9, sumR);
        };
        double tr, trel, cr, crel;
        {
            ThunderRef t;
            auto tp = [&](int p) {
                const double binHz = 48000.0 / 2048;
                const double f0 = Engine::kUiLo * std::pow(Engine::kUiHi / Engine::kUiLo, (double)p / P), f1 = Engine::kUiLo * std::pow(Engine::kUiHi / Engine::kUiLo, (double)(p + 1) / P);
                const int k0 = (int)std::ceil(f0 / binHz), k1 = (int)std::floor(f1 / binHz);
                if (k1 >= k0) { float m = 0; for (int k = k0; k <= k1; ++k) m = std::max(m, t.redS[(size_t)k]); return m; }
                const double fc = std::sqrt(f0 * f1) / binHz; const int k = (int)fc; const float fr = (float)(fc - k);
                return t.redS[(size_t)k] + (t.redS[(size_t)k + 1] - t.redS[(size_t)k]) * fr;
            };
            flutterOf(tp, t, tr, trel);
        }
        { Engine e; e.prepare(48000); flutterOf([&](int p) { return e.uiReduction(0, p); }, e, cr, crel); }
        printf("  Thunder: mean reduction where active %.2f dB, moves %.3f dB per hop (%.1f%% of it)\n", tr, tr * trel, 100 * trel);
        printf("  CALM:    mean reduction where active %.2f dB, moves %.3f dB per hop (%.1f%% of it)\n", cr, cr * crel, 100 * crel);
        CHECK(crel < trel, "CALM's reduction moves more smoothly than Thunder's (%.1f%% vs %.1f%% of the reduction per hop)", 100 * crel, 100 * trel);
    }

    // ---- 2. depth 0 nulls against the delayed input ----
    printf("== depth 0 / identity\n");
    for (int q = 0; q < 4; ++q)
    {
        Settings s; s.depth = 0; s.quality = q;
        Engine e; e.set(s); e.prepare(48000);
        const Stereo out = run(e, mix48, 333);
        const double nl = nullDb(out.L, mix48.L, (size_t)e.latency() + 100, (size_t)e.latency());
        const double nr = nullDb(out.R, mix48.R, (size_t)e.latency() + 100, (size_t)e.latency());
        CHECK(nl < -100 && nr < -100, "quality %d (N %d): depth 0 null vs delayed input L %.1f dB, R %.1f dB", q, e.fftSize(), nl, nr);
    }

    // ---- 3. delta + output == delayed input ----
    printf("== delta\n");
    for (int st = 0; st < 3; ++st)
    {
        Settings s; s.depth = 70; s.stereo = st;
        Engine a, b; a.set(s); a.prepare(48000);
        s.delta = true; b.set(s); b.prepare(48000);
        const Stereo oa = run(a, mix48), ob = run(b, mix48);
        std::vector<float> sumL(oa.L.size()), sumR(oa.L.size());
        for (size_t i = 0; i < sumL.size(); ++i) { sumL[i] = oa.L[i] + ob.L[i]; sumR[i] = oa.R[i] + ob.R[i]; }
        const int lat = a.latency();
        const double nl = nullDb(sumL, mix48.L, (size_t)lat + 100, (size_t)lat), nr = nullDb(sumR, mix48.R, (size_t)lat + 100, (size_t)lat);
        double eD = 0, eI = 0; for (size_t i = lat; i < ob.L.size(); ++i) { eD += ob.L[i] * ob.L[i]; eI += mix48.L[i - lat] * mix48.L[i - lat]; }
        CHECK(nl < -100 && nr < -100 && eD > 1e-6 * eI, "%s: delta + output vs delayed input %.1f / %.1f dB (delta itself %.1f dB below the input)",
              kStereoNames[st], nl, nr, -10 * std::log10(eD / eI));
    }

    // ---- 4. latency per quality (impulse), at 44.1 / 48 / 96 kHz ----
    printf("== latency\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int q = 0; q < 4; ++q)
        {
            Settings s; s.depth = 0; s.quality = q;
            Engine e; e.set(s); e.prepare(sr);
            const int len = 3 * e.latency() + 1000;
            Stereo x; x.L.assign((size_t)len, 0.0f); x.R.assign((size_t)len, 0.0f);
            x.L[500] = 1.0f; x.R[500] = -0.5f;
            const Stereo o = run(e, x, 256);
            int pk = 0; float pv = 0; for (int i = 0; i < len; ++i) if (std::fabs(o.L[(size_t)i]) > pv) { pv = std::fabs(o.L[(size_t)i]); pk = i; }
            CHECK(pk - 500 == e.latency() && std::fabs(pv - 1.0f) < 1e-4f && std::fabs(o.R[(size_t)pk] + 0.5f) < 1e-4f,
                  "%.0f Hz quality %s: impulse at +%d (reported %d), height %.6f", sr, kQualityNames[q], pk - 500, e.latency(), pv);
        }
    // also with the mix aligned: depth > 0 at mix 0 nulls (mix is latency-aligned)
    {
        Settings s; s.depth = 80; s.mix = 0;
        Engine e; e.set(s); e.prepare(48000);
        const Stereo o = run(e, mix48);
        const double nl = nullDb(o.L, mix48.L, (size_t)e.latency() + 100, (size_t)e.latency());
        CHECK(nl < -120, "mix 0%% at depth 80: null vs delayed input %.1f dB (dry path sample-aligned)", nl);
    }

    // ---- 5. stereo modes ----
    printf("== stereo modes\n");
    {
        // resonance only in the side signal: M/S with balance +100 must cut it; mono content (mid) must stay
        Stereo x = mix48;
        // add a ringing 4.2 kHz resonance to the side only: L += r, R -= r
        BiquadCoeffs bp; { AnalogSection a; const double w0 = kTwoPi * 4200 / 48000.0, q = 12; a.n2 = 0; a.n1 = w0 / q; a.n0 = 0; a.d2 = 1; a.d1 = w0 / q; a.d0 = w0 * w0; bestDesign(a, 48000, bp); }
        BiquadState st;
        std::mt19937 rng(5); std::normal_distribution<float> nd(0.0f, 1.0f);
        for (size_t i = 0; i < x.L.size(); ++i) { const float r = (float)st.process(bp, 0.6 * nd(rng)); x.L[i] += r; x.R[i] -= r; }
        for (int mode = 0; mode < 3; ++mode)
        {
            Settings s; s.depth = 70; s.stereo = mode; if (mode == kMidSide) s.balance = 100;
            Engine e; e.set(s); e.prepare(48000);
            const Stereo o = run(e, x);
            const int lat = e.latency();
            std::vector<float> mi(x.L.size()), si(x.L.size()), mo(x.L.size()), so(x.L.size());
            for (size_t i = 0; i < mi.size(); ++i)
            {
                mi[i] = 0.5f * (x.L[i] + x.R[i]); si[i] = 0.5f * (x.L[i] - x.R[i]);
                const size_t j = std::min(o.L.size() - 1, i + lat);
                mo[i] = 0.5f * (o.L[j] + o.R[j]); so[i] = 0.5f * (o.L[j] - o.R[j]);
            }
            const auto sMi = avgSpectrum(mi, 48000, 48000), sMo = avgSpectrum(mo, 48000, 48000), sSi = avgSpectrum(si, 48000, 48000), sSo = avgSpectrum(so, 48000, 48000);
            const double side42 = bandReduction(sSi, sSo, 4050, 4350, 48000), midAll = bandReduction(sMi, sMo, 20, 20000, 48000), mid12 = bandReduction(sMi, sMo, 1150, 1250, 48000);
            printf("  %-11s side 4.2k: %5.2f dB   mid broadband: %5.2f dB   mid 1.2k: %5.2f dB\n", kStereoNames[mode], side42, midAll, mid12);
            if (mode == kMidSide) CHECK(side42 > 3.0 && std::fabs(midAll) < 0.01 && std::fabs(mid12) < 0.01, "Mid/Side, balance +100 (side only): side resonance -%.2f dB, mid untouched (%.3f dB)", side42, midAll);
            else CHECK(side42 > 2.0, "%s: side resonance reduced %.2f dB", kStereoNames[mode], side42);
        }
        // Stereo L/R: a resonance only on the left must not touch the right channel
        {
            Stereo y = mix48;
            BiquadState s2;
            for (size_t i = 0; i < y.L.size(); ++i) y.L[i] += (float)s2.process(bp, 0.6 * nd(rng));
            Settings s; s.depth = 70; s.stereo = kStereoLR;
            Engine e; e.set(s); e.prepare(48000);
            const Stereo o = run(e, y);
            const int lat = e.latency();
            std::vector<float> lo(y.L.size()), ro(y.L.size());
            for (size_t i = 0; i < lo.size(); ++i) { const size_t j = std::min(o.L.size() - 1, i + lat); lo[i] = o.L[j]; ro[i] = o.R[j]; }
            const auto sLi = avgSpectrum(y.L, 48000, 48000), sLo = avgSpectrum(lo, 48000, 48000), sRi = avgSpectrum(y.R, 48000, 48000), sRo = avgSpectrum(ro, 48000, 48000);
            const double l42 = bandReduction(sLi, sLo, 4050, 4350, 48000), r42 = bandReduction(sRi, sRo, 4050, 4350, 48000);
            Settings s2l = s; s2l.stereo = kLinked;
            Engine e2; e2.set(s2l); e2.prepare(48000);
            const Stereo o2 = run(e2, y);
            for (size_t i = 0; i < ro.size(); ++i) { const size_t j = std::min(o2.L.size() - 1, i + lat); ro[i] = o2.R[j]; }
            const auto sRo2 = avgSpectrum(ro, 48000, 48000);
            const double r42l = bandReduction(sRi, sRo2, 4050, 4350, 48000);
            CHECK(l42 > 3.0 && r42 < 0.5 * l42 && r42l > r42, "Stereo L/R: left-only resonance -%.2f dB on L, right at 4.2k -%.2f dB (Linked would take -%.2f dB from R)", l42, r42, r42l);
        }
    }

    // ---- 6. sample rates: the same behaviour at 44.1 and 96 kHz ----
    printf("== sample rates\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const Stereo x = sr == 48000.0 ? mix48 : resample(mix48, sr / 48000.0);
        Engine e; e.prepare(sr);
        const Stereo o = run(e, x);
        const int lat = e.latency();
        std::vector<float> im(x.L.size()), om(x.L.size());
        for (size_t i = 0; i < im.size(); ++i) { im[i] = 0.5f * (x.L[i] + x.R[i]); const size_t j = std::min(o.L.size() - 1, i + lat); om[i] = 0.5f * (o.L[j] + o.R[j]); }
        const int N = sr > 60000 ? 16384 : 8192;
        const auto a = avgSpectrum(im, (size_t)sr, sr, N), b = avgSpectrum(om, (size_t)sr, sr, N);
        const double r12 = bandReduction(a, b, 1150, 1250, sr, N), r29 = bandReduction(a, b, 2780, 3020, sr, N), rAll = bandReduction(a, b, 20, 20000, sr, N);
        bool finite = true; for (float v : o.L) if (!std::isfinite(v)) finite = false;
        CHECK(finite && r12 > rAll + 2.0 && r29 > rAll + 2.0, "%.0f Hz (N %d, latency %d): 1.2k %.2f dB, 2.9k %.2f dB, broadband %.2f dB", sr, e.fftSize(), lat, r12, r29, rAll);
    }

    // ---- 7. random parameter changes (including quality switches) stay finite, bounded and click-safe ----
    printf("== parameter changes\n");
    {
        Engine e; e.prepare(48000);
        std::mt19937 rng(11);
        auto U = [&](float a, float b) { return a + (b - a) * (float)((rng() % 10000) / 9999.0); };
        Stereo x = mix48;
        bool finite = true; float peak = 0;
        const int B = 256;
        for (int off = 0, blk = 0; off + B <= (int)x.L.size(); off += B, ++blk)
        {
            if (blk % 7 == 0)
            {
                Settings s;
                s.depth = U(0, 100); s.sharpness = U(0, 100); s.selectivity = U(0, 100); s.attackMs = U(1, 200); s.releaseMs = U(10, 1000);
                s.mode = (int)(rng() % 2); s.focusLo = U(20, 2000); s.focusHi = U(500, 20000); s.stereo = (int)(rng() % 3); s.balance = U(-100, 100);
                s.quality = blk % 91 == 0 ? (int)(rng() % 4) : e.settings().quality; s.delta = (rng() % 5) == 0; s.mix = U(0, 100); s.trimDb = U(-12, 12);
                for (int k = 0; k < kNumNodes; ++k) { s.nodes[k].freq = U(20, 20000); s.nodes[k].gain = U(-12, 12); s.nodes[k].q = U(0.1f, 10); }
                e.set(s);
            }
            e.process(x.L.data() + off, x.R.data() + off, B);
            for (int i = off; i < off + B; ++i) { if (!std::isfinite(x.L[(size_t)i]) || !std::isfinite(x.R[(size_t)i])) finite = false; peak = std::max(peak, std::fabs(x.L[(size_t)i])); }
        }
        CHECK(finite && peak < 4.0f * 0.5f * 4.0f, "random automation incl. FFT size switches: finite, peak %.3f (input peak 0.5, trim up to +12 dB)", peak);
    }

    // ---- 8. CPU: real-time factor per quality ----
    printf("== cpu\n");
    for (int q = 0; q < 4; ++q)
    {
        Settings s; s.quality = q; s.stereo = kStereoLR;
        Engine e; e.set(s); e.prepare(48000);
        Stereo x = mix48;
        const auto t0 = std::chrono::steady_clock::now();
        run(e, x, 512);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  quality %s, two detectors: %.1f%% of one core (%.0fx real time)\n", kQualityNames[q], 100.0 * sec / (x.L.size() / 48000.0), (x.L.size() / 48000.0) / sec);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
