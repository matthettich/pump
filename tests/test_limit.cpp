// pump LIMIT engine: the ceiling holds (sample peak always; true peak within +0.1 dB) on the test mix driven
// +18 dB in every style and oversampling setting at 44.1/48/96 kHz, plus synthetic worst cases; exact latency
// (impulse position, bit-exact null without oversampling); styles really differ; smooth parameter changes;
// dither on the grid; unity-gain listen.
#include "TestMix.hpp"
#include "../plugins/limit/LimitEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>

using namespace pump;
using namespace pump::limit;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> gMixL, gMixR;
static void loadMix()
{
    FILE* f = std::fopen(pump_test_mix(), "rb");
    if (!f) { printf("no test mix\n"); return; }
    std::fseek(f, 0, SEEK_END); const long n = std::ftell(f) / 8; std::fseek(f, 0, SEEK_SET);
    std::vector<float> v((size_t)n * 2);
    if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear();
    std::fclose(f);
    gMixL.resize((size_t)n); gMixR.resize((size_t)n);
    for (long i = 0; i < n; ++i) { gMixL[(size_t)i] = v[(size_t)(2 * i)]; gMixR[(size_t)i] = v[(size_t)(2 * i + 1)]; }
}

// Synthetic worst cases appended after the mix: an fs/4 sine at 45 degrees (true peak 3 dB above the sample
// peaks), a full-scale square wave, single-sample spikes, white noise bursts, and opposite-polarity clicks.
static void appendWorstCases(std::vector<float>& L, std::vector<float>& R, double sr)
{
    const int seg = (int)(sr * 0.5);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int i = 0; i < seg; ++i) { const float s = 0.5f * (float)std::sin(kPi * 0.5 * i + kPi / 4); L.push_back(s); R.push_back(-s); }
    for (int i = 0; i < seg; ++i) { const float s = ((i / 37) & 1) ? 0.5f : -0.5f; L.push_back(s); R.push_back(s); }
    for (int i = 0; i < seg; ++i) { const float s = (i % 997 == 0) ? 0.9f : 0.0f; L.push_back(s); R.push_back((i % 991 == 0) ? -0.9f : 0.0f); }
    for (int i = 0; i < seg; ++i) { const float g = ((i / 2000) & 1) ? 0.6f : 0.05f; L.push_back(g * u(rng)); R.push_back(g * u(rng)); }
    for (int i = 0; i < seg; ++i) { const float s = 0.4f * (float)std::sin(kTwoPi * 0.4 * i); L.push_back(s); R.push_back((i % 500 < 3) ? 0.95f : s); }
}

struct RunResult { double samplePeakDb, truePeakDb, lufs, avgGrDb, maxGrDb; double secs; };

static RunResult run(double sr, int style, int osIdx, bool tp, float gainDb, float ceilDb, const std::vector<float>& inL,
                     const std::vector<float>& inR, int block = 512, float release = 100.0f, float lookMs = 5.0f, float attack = 50.0f)
{
    Engine e; e.prepare(sr, block);
    e.setGain(gainDb); e.setCeiling(ceilDb); e.setStyle(style); e.setOversampling(osIdx); e.setTruePeak(tp);
    e.setLookahead(lookMs); e.setRelease(release); e.setAttack(attack); e.setLinks(75, 100);
    std::vector<float> L = inL, R = inR;
    const auto t0 = std::chrono::steady_clock::now();
    e.process(L.data(), R.data(), 0);   // settle smoothers at the new targets
    // process in irregular blocks (hosts do that)
    size_t off = 0; int k = 0;
    while (off < L.size())
    {
        const int n = (int)std::min<size_t>(L.size() - off, (size_t)(k++ % 3 == 0 ? block : (block / 3 + 17)));
        e.process(L.data() + off, R.data() + off, n);
        off += (size_t)n;
    }
    RunResult r;
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // skip the first 0.1 s (smoothers gliding from defaults)
    const size_t skip = (size_t)(sr * 0.1);
    float sp = 0.0f, tpk = 0.0f;
    TruePeakDetector d[2];
    for (size_t i = 0; i < L.size(); ++i)
    {
        const float a = d[0].process(L[i]), b = d[1].process(R[i]);
        if (i < skip) continue;
        sp = std::max(sp, std::max(std::fabs(L[i]), std::fabs(R[i])));
        tpk = std::max(tpk, std::max(a, b));
    }
    r.samplePeakDb = gainToDb(sp);
    r.truePeakDb = gainToDb(std::max(tpk, sp));
    LoudnessMeter m; m.prepare(sr); m.process(L.data(), R.data(), (int)L.size());
    r.lufs = m.getIntegrated();
    // GR estimate: driven input level vs output, short-term
    r.avgGrDb = 0; r.maxGrDb = 0;
    return r;
}

static void testCeiling()
{
    printf("Ceiling: test mix +18 dB (plus worst cases), ceiling -1 dB\n");
    const float ceil = -1.0f;
    double worstSP = -100, worstTP = -100, worstSPoff = -100, totalSecs = 0, audioSecs = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> L = gMixL, R = gMixR;
        if (sr != 48000.0) { L.resize((size_t)(sr * 9)); R.resize((size_t)(sr * 9)); }   // the mix at another rate (pitch-shifted, fine for peaks)
        appendWorstCases(L, R, sr);
        for (int os = 0; os <= 3; ++os)
        {
            for (int st = 0; st < kNumStyles; ++st)
            {
                const RunResult r = run(sr, st, os, true, 18.0f, ceil, L, R);
                totalSecs += r.secs; audioSecs += L.size() / sr;
                worstSP = std::max(worstSP, r.samplePeakDb);
                worstTP = std::max(worstTP, r.truePeakDb);
                if (r.truePeakDb > ceil + 0.1 || r.samplePeakDb > ceil + 1e-5)
                    printf("    over: %.0f Hz %s %s: sample %.4f dBFS, true peak %.4f dBTP\n", sr, kOSNames[os], kStyleNames[st], r.samplePeakDb, r.truePeakDb);
                if (st == kAllround || st == kAggressive)
                    printf("    %.0f Hz %-3s %-11s TP on : sample peak %+.4f  true peak %+.4f  %.2f LUFS\n", sr, kOSNames[os], kStyleNames[st], r.samplePeakDb, r.truePeakDb, r.lufs);
                // true peak off: the sample peak must still hold exactly
                if (st == kAllround || st == kTransparent || st == kAggressive)
                {
                    const RunResult q = run(sr, st, os, false, 18.0f, ceil, L, R);
                    worstSPoff = std::max(worstSPoff, q.samplePeakDb);
                    if (st == kAllround) printf("    %.0f Hz %-3s %-11s TP off: sample peak %+.4f  true peak %+.4f\n", sr, kOSNames[os], kStyleNames[st], q.samplePeakDb, q.truePeakDb);
                }
            }
        }
    }
    CHECK(worstSP <= ceil + 1e-5, "true peak on: worst sample peak %+.5f dBFS (ceiling %.1f), 84 runs", worstSP, ceil);
    CHECK(worstTP <= ceil + 0.1, "true peak on: worst 4x true peak %+.4f dBTP (limit %+.1f)", worstTP, ceil + 0.1);
    CHECK(worstSPoff <= ceil + 1e-5, "true peak off: worst sample peak %+.5f dBFS", worstSPoff);
    printf("    speed: %.1fx real time on average over all runs\n", audioSecs / std::max(1e-9, totalSecs));
}

static void testCeilingSweep()
{
    printf("Ceiling and lookahead extremes (48 kHz, Modern, 4x)\n");
    std::vector<float> L(gMixL.begin(), gMixL.begin() + 48000 * 6), R(gMixR.begin(), gMixR.begin() + 48000 * 6);
    appendWorstCases(L, R, 48000);
    double worst = -100;
    for (float c : { -12.0f, -6.0f, -0.1f, 0.0f })
        for (float la : { 0.1f, 1.0f, 10.0f })
            for (float atk : { 0.0f, 100.0f })
            {
                const RunResult r = run(48000, kModern, 2, true, 24.0f, c, L, R, 512, 30.0f, la, atk);
                worst = std::max(worst, r.truePeakDb - c);
                if (r.samplePeakDb > c + 1e-5) printf("    sample over: ceiling %.1f la %.1f atk %.0f: %.4f\n", c, la, atk, r.samplePeakDb);
            }
    CHECK(worst <= 0.1, "ceilings -12..0, lookahead 0.1..10 ms, attack 0/100%%, +24 dB: worst true peak %+.4f dB over the ceiling", worst);
}

static int osLatency(int lg) { Oversampler o; o.prepare(lg, 64); return o.latency(); }

static void testLatency()
{
    printf("Latency\n");
    int configs = 0, good = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int os = 0; os <= 3; ++os)
            for (int tp = 0; tp <= 1; ++tp)
                for (float la : { 0.1f, 1.5f, 10.0f })
                {
                    Engine e; e.prepare(sr, 512);
                    e.setGain(0); e.setCeiling(0); e.setOversampling(os); e.setTruePeak(tp != 0); e.setLookahead(la);
                    const int lat = e.latency();
                    const int T = lat + 4096;
                    std::vector<float> L((size_t)T, 0.0f), R((size_t)T, 0.0f);
                    L[100] = 0.25f; R[100] = -0.25f;
                    for (int off = 0; off < T; off += 256) e.process(L.data() + off, R.data() + off, std::min(256, T - off));
                    int pk = 0; float pv = 0;
                    for (int i = 0; i < T; ++i) if (std::fabs(L[(size_t)i]) > pv) { pv = std::fabs(L[(size_t)i]); pk = i; }
                    const bool okPos = pk == 100 + lat && std::fabs(pv - 0.25f) < 0.01f && R[(size_t)pk] < -0.24f;
                    ++configs; good += okPos;
                    if (!okPos || (os == 2 && tp && la == 1.5f && sr == 48000.0))
                        CHECK(okPos, "%.0f Hz %s TP %s lookahead %.1f ms: impulse at +%d, reported latency %d (peak %.4f)", sr, kOSNames[os], tp ? "on" : "off", la, pk - 100, lat, pv);
                }
    CHECK(good == configs, "impulse lands exactly at the reported latency in %d of %d configurations (3 rates x 4 OS x TP on/off x 3 lookaheads)", good, configs);
    // null against the delayed input (no limiting), 48 kHz
    std::vector<float> inL(gMixL.begin(), gMixL.begin() + 48000 * 8), inR(gMixR.begin(), gMixR.begin() + 48000 * 8);
    for (auto& v : inL) v *= 0.25f;
    for (auto& v : inR) v *= 0.25f;
    for (int os = 0; os <= 3; ++os)
        for (int tp = 0; tp <= 1; ++tp)
        {
            Engine e; e.prepare(48000, 512);
            e.setGain(0); e.setCeiling(0); e.setOversampling(os); e.setTruePeak(tp != 0); e.setLookahead(5);
            const int lat = e.latency();
            std::vector<float> L = inL, R = inR;
            for (size_t off = 0; off < L.size(); off += 480) e.process(L.data() + off, R.data() + off, (int)std::min<size_t>(480, L.size() - off));
            // reference: the same oversampler round trip on its own, then the rest of the latency as a plain delay
            std::vector<float> rL = inL, rR = inR;
            if (os > 0)
            {
                Oversampler ref; ref.prepare(os, 480);
                for (size_t off = 0; off < rL.size(); off += 480)
                {
                    const int n = (int)std::min<size_t>(480, rL.size() - off);
                    ref.upsample(rL.data() + off, rR.data() + off, n);
                    ref.downsample(rL.data() + off, rR.data() + off, n);
                }
            }
            const size_t d = (size_t)(lat - (os > 0 ? osLatency(os) : 0));
            double err = 0, ref = 0, maxErr = 0;
            for (size_t i = (size_t)lat + 4800; i < L.size(); ++i)
            {
                const double d0 = L[i] - rL[i - d], d1 = R[i] - rR[i - d];
                err += d0 * d0 + d1 * d1; ref += (double)rL[i - d] * rL[i - d] + (double)rR[i - d] * rR[i - d];
                maxErr = std::max(maxErr, std::max(std::fabs(d0), std::fabs(d1)));
            }
            const double nullDb = 10 * std::log10(err / ref + 1e-30);
            if (os == 0) CHECK(maxErr == 0.0, "no limiting, OS off, TP %s: bit-exact null vs input delayed by %d (max error %.1e)", tp ? "on" : "off", lat, maxErr);
            else CHECK(maxErr == 0.0, "no limiting, %s, TP %s: bit-exact null vs the %s round trip delayed to %d (max error %.1e, %.1f dB)", kOSNames[os], tp ? "on" : "off", kOSNames[os], lat, maxErr, nullDb);
        }
}

static void testStyles()
{
    printf("Styles (48 kHz, mix +12 dB, ceiling -1, 2x)\n");
    std::vector<float> L(gMixL.begin(), gMixL.begin() + 48000 * 12), R(gMixR.begin(), gMixR.begin() + 48000 * 12);
    std::vector<std::vector<float>> outs;
    for (int st = 0; st < kNumStyles; ++st)
    {
        Engine e; e.prepare(48000, 512);
        e.setGain(12); e.setCeiling(-1); e.setStyle(st); e.setOversampling(1);
        std::vector<float> a = L, b = R;
        double grSum = 0; int grN = 0; float grMax = 0;
        for (size_t off = 0; off < a.size(); off += 512)
        {
            e.process(a.data() + off, b.data() + off, (int)std::min<size_t>(512, a.size() - off));
            const float g = e.grMax.exchange(0.0f);
            grSum += g; ++grN; grMax = std::max(grMax, g);
        }
        LoudnessMeter m; m.prepare(48000); m.process(a.data(), b.data(), (int)a.size());
        double rms = 0; for (size_t i = 0; i < a.size(); ++i) rms += a[i] * a[i];
        rms = std::sqrt(rms / a.size());
        printf("    %-11s  %.2f LUFS  PLR %.2f dB  GR avg %.2f / max %.2f dB\n", kStyleNames[st], m.getIntegrated(), -1.0 - m.getIntegrated(), grSum / grN, grMax);
        outs.push_back(a);
    }
    // how different the styles sound: the residual between two styles' outputs, relative to the output
    double minDist = -1e9; int pa = 0, pb = 0;   // largest attenuation of the residual = most similar pair
    for (int a = 0; a < kNumStyles; ++a)
        for (int b = a + 1; b < kNumStyles; ++b)
        {
            double d = 0, ref = 0;
            for (size_t i = 4800; i < outs[(size_t)a].size(); ++i) { const double x = outs[(size_t)a][i] - outs[(size_t)b][i]; d += x * x; ref += outs[(size_t)a][i] * outs[(size_t)a][i]; }
            const double db = 10 * std::log10(d / ref + 1e-30);
            if (-db > minDist) { minDist = -db; pa = a; pb = b; }
        }
    CHECK(minDist < 40.0, "every pair of styles differs audibly: closest pair %s / %s, residual %.1f dB below the signal", kStyleNames[pa], kStyleNames[pb], -minDist);
}

static void testSmooth()
{
    printf("Parameter changes\n");
    // a 100 Hz sine at -6 dBFS, gain/ceiling/style/attack/release/links changed every block: no step larger than
    // the signal's own slope plus a little
    Engine e; e.prepare(48000, 256);
    e.setOversampling(0); e.setTruePeak(false);
    std::mt19937 rng(5);
    const int T = 48000 * 4;
    std::vector<float> L(T), R(T);
    for (int i = 0; i < T; ++i) L[(size_t)i] = R[(size_t)i] = 0.5f * (float)std::sin(kTwoPi * 100.0 * i / 48000.0);
    float prev = 0, maxStep = 0;
    bool finite = true;
    for (int off = 0; off < T; off += 256)
    {
        e.setGain((float)(rng() % 25)); e.setCeiling(-(float)(rng() % 7)); e.setStyle((int)(rng() % kNumStyles));
        e.setAttack((float)(rng() % 101)); e.setRelease(1.0f + (float)(rng() % 500)); e.setLinks((float)(rng() % 101), (float)(rng() % 101));
        e.process(L.data() + off, R.data() + off, 256);
        for (int i = off; i < off + 256; ++i)
        {
            if (!std::isfinite(L[(size_t)i])) finite = false;
            if (off > 48000) maxStep = std::max(maxStep, std::fabs(L[(size_t)i] - prev));
            prev = L[(size_t)i];
        }
    }
    // the sine's own largest step at 100 Hz, 0.5 amplitude, +24 dB drive limited to 0 dBFS: ~ 2 pi 100/48000
    CHECK(finite && maxStep < 0.03f, "random gain/ceiling/style/attack/release/link changes every block: largest sample step %.4f", maxStep);

    // reconfiguring (lookahead / oversampling / true peak) mid-stream: finite, ceiling holds, fades back in
    Engine f; f.prepare(48000, 512);
    f.setGain(12); f.setCeiling(-1);
    std::vector<float> a(gMixL.begin(), gMixL.begin() + 48000 * 6), b(gMixR.begin(), gMixR.begin() + 48000 * 6);
    float pk = 0; bool fin = true;
    for (size_t off = 0, k = 0; off < a.size(); off += 512, ++k)
    {
        if (k % 40 == 20) { f.setLookahead(0.1f + (float)(k % 7)); f.setOversampling((int)(k % 4)); f.setTruePeak(k % 3 != 0); }
        f.process(a.data() + off, b.data() + off, (int)std::min<size_t>(512, a.size() - off));
    }
    for (size_t i = 4800; i < a.size(); ++i) { pk = std::max(pk, std::max(std::fabs(a[i]), std::fabs(b[i]))); if (!std::isfinite(a[i])) fin = false; }
    CHECK(fin && pk <= (float)dbToGain(-1.0) + 1e-6f, "lookahead/oversampling/true-peak switches while playing: finite, sample peak %.4f dBFS", gainToDb(pk));
}

static void testDitherUnity()
{
    printf("Dither and unity listen\n");
    std::vector<float> L(gMixL.begin(), gMixL.begin() + 48000 * 4), R(gMixR.begin(), gMixR.begin() + 48000 * 4);
    for (int mode = 1; mode <= 2; ++mode)
        for (int sh = 0; sh <= 1; ++sh)
        {
            Engine e; e.prepare(48000, 512);
            e.setGain(10); e.setCeiling(-0.3f); e.setDither(mode, sh);
            std::vector<float> a = L, b = R;
            for (size_t off = 0; off < a.size(); off += 512) e.process(a.data() + off, b.data() + off, (int)std::min<size_t>(512, a.size() - off));
            const double q = std::ldexp(1.0, -((mode == 1 ? 16 : 24) - 1));
            double offGrid = 0; float pk = 0;
            for (size_t i = 4800; i < a.size(); ++i)
            {
                offGrid = std::max(offGrid, std::fabs(a[i] / q - std::round(a[i] / q)));
                pk = std::max(pk, std::max(std::fabs(a[i]), std::fabs(b[i])));
            }
            CHECK(offGrid < 1e-3 && pk <= (float)dbToGain(-0.3), "%s %s: samples on the grid (max off %.1e LSB), peak %.4f dBFS", kDitherNames[mode], kShapingNames[sh], offGrid, gainToDb(pk));
        }
    // unity: output loudness with unity on = loudness with it off minus the gain
    double lu[2];
    for (int u = 0; u <= 1; ++u)
    {
        Engine e; e.prepare(48000, 512);
        e.setGain(9); e.setCeiling(-1); e.setUnity(u != 0);
        std::vector<float> a = L, b = R;
        for (size_t off = 0; off < a.size(); off += 512) e.process(a.data() + off, b.data() + off, (int)std::min<size_t>(512, a.size() - off));
        LoudnessMeter m; m.prepare(48000); m.process(a.data(), b.data(), (int)a.size());
        lu[u] = m.getIntegrated();
    }
    CHECK(std::fabs(lu[0] - lu[1] - 9.0) < 0.05, "unity listen: %.2f LUFS -> %.2f LUFS (%.2f dB, gain 9 dB)", lu[0], lu[1], lu[0] - lu[1]);
    // release: longer release = more average gain reduction (less loud)
    double rl[2];
    for (int k = 0; k < 2; ++k)
    {
        const RunResult r = run(48000, kAllround, 1, true, 12, -1, L, R, 512, k ? 800.0f : 10.0f);
        rl[k] = r.lufs;
    }
    CHECK(rl[0] > rl[1] + 0.5, "release 10 ms -> %.2f LUFS, 800 ms -> %.2f LUFS", rl[0], rl[1]);
}

static void testMeters()
{
    printf("Meters\n");
    // engine output loudness readout matches an offline meter on the same output
    Engine e; e.prepare(48000, 512);
    e.setGain(6); e.setCeiling(-1);
    std::vector<float> a(gMixL.begin(), gMixL.begin() + 48000 * 10), b(gMixR.begin(), gMixR.begin() + 48000 * 10);
    for (size_t off = 0; off < a.size(); off += 512) e.process(a.data() + off, b.data() + off, (int)std::min<size_t>(512, a.size() - off));
    LoudnessMeter m; m.prepare(48000); m.process(a.data(), b.data(), (int)a.size());
    const float ri = e.outLoud.integrated.load(), rt = e.outLoud.truePeak.load();
    CHECK(std::fabs(ri - m.getIntegrated()) < 0.05 && rt <= -1.0f + 0.1f, "output readout I %.2f LUFS (offline %.2f), TP max %.2f dBTP", ri, m.getIntegrated(), rt);
    e.resetLoudness.store(true);
    std::vector<float> z(512, 0.0f), z2(512, 0.0f);
    e.process(z.data(), z2.data(), 512);
    CHECK(!std::isfinite(e.outLoud.integrated.load()), "reset: integrated back to -inf");
    int n = 0; float last = 0;
    std::vector<float> h(64);
    e.histGR.readLatest(h.data(), 64);
    for (float v : h) { if (v > 0) ++n; last = v; }
    (void)last;
    CHECK(e.histGR.position() > 1900, "history: %u GR points for 10 s (200/s)", e.histGR.position());
}

int main(int argc, char** argv)
{
    loadMix();
    if (gMixL.empty()) return 1;
    const std::string only = argc > 1 ? argv[1] : "";
    auto want = [&](const char* name) { return only.empty() || only == name; };
    if (want("latency")) testLatency();
    if (want("smooth")) testSmooth();
    if (want("dither")) testDitherUnity();
    if (want("meters")) testMeters();
    if (want("styles")) testStyles();
    if (want("sweep")) testCeilingSweep();
    if (want("ceiling")) testCeiling();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
