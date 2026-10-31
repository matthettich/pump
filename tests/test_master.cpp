// pump MASTER engine: nothing changes before LEARN or with every stage at zero (null against the delayed input);
// after learning the test mix the output lands on the loudness target under the true-peak ceiling; medium stays
// subtle (tone moves within a few dB, compression of a couple of dB); intensity scales the tone curve; the tone
// stage moves the spectrum toward the target and cuts a deliberate bump in pink noise; an already loud song is
// turned down; COMPARE is level matched; the analysis survives its text form and garbage is rejected; stage
// switches and setting jumps never click or blow up; 44.1 and 96 kHz work.
#include "TestMix.hpp"
#include "../plugins/master/MasterEngine.hpp"
#include "dsp/Loudness.hpp"
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::master;
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
static Stereo twice(const Stereo& s) { Stereo o = s; o.L.insert(o.L.end(), s.L.begin(), s.L.end()); o.R.insert(o.R.end(), s.R.begin(), s.R.end()); return o; }
static Stereo scaled(const Stereo& s, float g) { Stereo o = s; for (auto& x : o.L) x *= g; for (auto& x : o.R) x *= g; return o; }
static double nullDiff(const Stereo& in, const Stereo& out, int lat)
{
    double mx = 0.0;
    for (size_t i = (size_t)lat; i < out.L.size(); ++i)
        mx = std::max(mx, (double)std::max(std::fabs(out.L[i] - in.L[i - lat]), std::fabs(out.R[i] - in.R[i - lat])));
    return mx;
}
struct Loud { double I, tp; };
static Loud measure(const Stereo& s, double sr, size_t from = 0)
{
    LoudnessMeter m; m.prepare(sr);
    m.process(s.L.data() + from, s.R.data() + from, (int)(s.L.size() - from));
    return { m.getIntegrated(), m.getTruePeakDb() };
}
static Stereo pinkStereo(size_t n, unsigned seed, float gain)
{
    Stereo s; s.L.resize(n); s.R.resize(n);
    std::mt19937 rng(seed); std::normal_distribution<float> nd(0, 1);
    double st[2][7] = {};
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            double* b = st[c];
            const double w = nd(rng);
            b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759; b[2] = 0.96900 * b[2] + w * 0.1538520;
            b[3] = 0.86650 * b[3] + w * 0.3104856; b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
            const double p = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362; b[6] = w * 0.115926;
            (c == 0 ? s.L : s.R)[i] = (float)(p * gain);
        }
    return s;
}
// rms (dB) of the analysis's smoothed spectrum minus the aligned target, 60 Hz .. 10 kHz
static double deviation(const Plan& p)
{
    double s = 0.0; int n = 0;
    for (int b = 0; b < kBands; ++b)
    {
        const double f = bandFreq(b);
        if (f < 60.0 || f > 10000.0) continue;
        const double d = p.measured[b] - p.target[b];
        s += d * d; ++n;
    }
    return std::sqrt(s / std::max(1, n));
}
static double maxAbsCurve(const Engine& e)
{
    double m = 0.0;
    for (int k = 0; k < kCurvePts; ++k) m = std::max(m, (double)std::fabs(e.uiCurve[k].load()));
    return m;
}

int main()
{
    const double sr = 48000.0;
    const Stereo mix = loadMix(pump_test_mix());
    if (mix.L.empty()) { printf("no test mix\n"); return 1; }
    const Stereo mix2 = twice(mix);
    printf("== pump MASTER\n");

    // ---- before LEARN: the delayed input, exactly
    {
        Engine e; e.prepare(sr);
        const Stereo o = run(e, mix);
        CHECK(e.latency() > 0 && e.latency() < 2000, "latency %d samples (the limiter's)", e.latency());
        CHECK(nullDiff(mix, o, e.latency()) == 0.0, "no analysis: output is the input delayed (max diff %.3g)", nullDiff(mix, o, e.latency()));
    }

    // ---- learn the mix at the defaults
    Engine e; e.prepare(sr);
    e.learnBuffer(mix.L.data(), mix.R.data(), (int)mix.L.size());
    const Analysis a = e.analysis();
    const Loud in = measure(mix, sr);
    CHECK(a.valid && std::fabs(a.seconds - mix.L.size() / sr) < 0.01, "learned %.2f s", a.seconds);
    CHECK(std::fabs(a.lufs - in.I) < 0.05, "learned loudness %.2f LUFS (meter %.2f)", a.lufs, in.I);
    CHECK(a.q[0] < a.q[9] && a.q[9] < a.q[kQuantiles - 1], "level quantiles rise: 5%% %.1f, 50%% %.1f, 95%% %.1f dB", a.q[0], a.q[9], a.q[kQuantiles - 1]);
    CHECK(a.crest > 6.0f && a.crest < 25.0f, "crest %.1f dB", a.crest);
    {
        const Plan& p = e.plan();
        double mg = 0.0; for (double g : p.eqGain) mg = std::max(mg, std::fabs(g));
        CHECK(p.valid && maxAbsCurve(e) < 3.5, "medium: tone curve within %.2f dB (filters up to %.2f dB)", maxAbsCurve(e), mg);
        CHECK(p.ratio == 1.0 && p.lift > 0.5 && p.lift <= 1.0 && std::fabs(p.compDeltaDb) < 1.0,
              "Balanced at medium: no downward compression, quiet sections lifted up to %.2f dB (loudness %+.2f dB)", p.lift, p.compDeltaDb);
        Engine pu; pu.prepare(sr); Settings ps; ps.style = kPunch; pu.set(ps); pu.setAnalysis(a);
        CHECK(pu.plan().ratio > 1.1 && pu.plan().compDeltaDb < 0.5 && pu.plan().compDeltaDb > -3.0,
              "Punch at medium: glue %.2f:1 at %.1f dB, loudness %+.2f dB", pu.plan().ratio, pu.plan().thr, pu.plan().compDeltaDb);
        CHECK(p.sideHi > dbToGain(-1.6) && p.sideHi < dbToGain(1.6), "medium: width change %.2f dB", gainToDb(p.sideHi));
        printf("     plan: tone %+.2f dB on loudness, makeup %+.2f dB, predicted %.2f LUFS, peaks over the ceiling by %.1f dB\n",
               p.eqDeltaDb, p.makeupDb, p.outLufs, p.peakOverDb);
        printf("     tone filters:"); for (double g : p.eqGain) printf(" %+.2f", g); printf("\n");
    }
    {
        Engine r; r.prepare(sr); Settings ts; ts.loudMode = kToTarget; r.set(ts); r.setAnalysis(a);
        const Stereo o = run(r, mix2);
        const Loud out = measure(o, sr, mix.L.size());   // the second pass (the limiter and compressor settled)
        CHECK(std::fabs(out.I - (-12.0)) < 1.0, "output %.2f LUFS (target -12)", out.I);
        CHECK(out.tp <= -1.0 + 0.15, "true peak %.2f dBTP (ceiling -1)", out.tp);
        float gr = r.compGR.exchange(0.0f);
        CHECK(gr < 6.0f, "largest compressor reduction %.2f dB", gr);
        // COMPARE is level matched
        Settings s; s.compare = true; s.loudMode = kToTarget;
        Engine c; c.prepare(sr); c.setAnalysis(a); c.set(s);
        const Stereo oc = run(c, mix2);
        const Loud lc = measure(oc, sr, mix.L.size());
        CHECK(std::fabs(lc.I - out.I) < 1.0, "compare: %.2f LUFS against %.2f processed", lc.I, out.I);
        // and it is the input
        double mx = 0.0; const double g = dbToGain(c.plan().compareDb);
        for (size_t i = mix.L.size(); i < oc.L.size(); ++i) mx = std::max(mx, (double)std::fabs(oc.L[i] - g * mix2.L[i - c.latency()]));
        CHECK(mx < 1e-5, "compare plays the input, gain %+.2f dB (diff %.2g)", c.plan().compareDb, mx);
    }

    // ---- GENTLE: only as loud as the peaks allow (at most 2 dB of peak limiting at medium), never past the target
    {
        Engine g; g.prepare(sr); g.setAnalysis(a);
        const Stereo o = run(g, mix2);
        const Loud lo = measure(o, sr, mix.L.size());
        const Plan& p = g.plan();
        CHECK(p.peakOverDb <= 2.01 && p.limitLossDb > -0.6 && lo.tp <= -0.85 && lo.I < -12.0,
              "gentle: makeup %+.2f dB, peaks %.2f dB into the limiter, loudness cost %.2f dB, output %.2f LUFS at %.2f dBTP",
              p.makeupDb, p.peakOverDb, p.limitLossDb, lo.I, lo.tp);
        Settings s; s.intensity = 25.0f; g.set(s); g.replan();
        CHECK(g.plan().peakOverDb <= 1.01, "gentle at 25 %%: at most %.2f dB into the limiter", g.plan().peakOverDb);
    }

    // ---- intensity scales the tone; zero and loudness off: transparent again
    {
        Engine m; m.prepare(sr); m.setAnalysis(a);
        const double med = maxAbsCurve(m);
        Settings s; s.intensity = 100.0f; m.set(s); m.setAnalysis(a);
        const double hi = maxAbsCurve(m);
        s.intensity = 25.0f; m.set(s); m.setAnalysis(a);
        const double lo = maxAbsCurve(m);
        CHECK(hi > 1.6 * med && lo < 0.65 * med, "tone curve: %.2f dB at 25 %%, %.2f at 50 %%, %.2f at 100 %%", lo, med, hi);
        Engine z; z.prepare(sr); Settings zs; zs.intensity = 0.0f; zs.loudOn = false; z.set(zs); z.setAnalysis(a);
        const Stereo o = run(z, mix);
        CHECK(nullDiff(mix, o, z.latency()) < 1e-6, "intensity 0, loudness off: transparent (max diff %.3g)", nullDiff(mix, o, z.latency()));
        Engine off; off.prepare(sr); Settings os; os.toneOn = os.dynOn = os.stereoOn = os.loudOn = false; off.set(os); off.setAnalysis(a);
        const Stereo o2 = run(off, mix);
        CHECK(nullDiff(mix, o2, off.latency()) < 1e-6, "every stage off: transparent (max diff %.3g)", nullDiff(mix, o2, off.latency()));
    }

    // ---- the tone stage moves the balance toward the target; it cuts a bump in pink noise
    {
        Engine t; t.prepare(sr);
        Settings s; s.dynOn = s.stereoOn = s.loudOn = false; s.intensity = 100.0f; t.set(s);
        t.learnBuffer(mix.L.data(), mix.R.data(), (int)mix.L.size());
        const double before = deviation(t.plan());
        const Stereo o = run(t, mix);
        Engine t2; t2.prepare(sr);
        t2.learnBuffer(o.L.data(), o.R.data(), (int)o.L.size());
        const double after = deviation(t2.plan());
        CHECK(after < before, "tone at 100 %%: balance deviation from the target %.2f -> %.2f dB rms", before, after);

        Stereo pn = pinkStereo((size_t)(sr * 12), 7, 0.05f);
        // +7 dB bell at 3 kHz, one octave wide
        BandFilter<2> bump; bump.d.design(analogPrototype(BandSpec{ Shape::Bell, 3000.0, 7.0, 1.4, 12 }, sr), sr);
        for (size_t i = 0; i < pn.L.size(); ++i) { pn.L[i] = (float)bump.process(0, pn.L[i]); pn.R[i] = (float)bump.process(1, pn.R[i]); }
        Engine b; b.prepare(sr);
        b.learnBuffer(pn.L.data(), pn.R.data(), (int)pn.L.size());
        double at3k = 0.0;
        for (int k = 0; k < kCurvePts; ++k) if (std::fabs(std::log2(Engine::curveFreq(k) / 3000.0)) < 0.1) at3k = b.uiCurve[k].load();
        CHECK(at3k < -1.0, "a +7 dB bump at 3 kHz in pink noise: the tone curve cuts %.2f dB there", at3k);
    }

    // ---- an already loud master (about -10.5 LUFS at -1 dBTP: this engine's own output) is turned down to -14
    {
        Engine m; m.prepare(sr); m.setAnalysis(a);
        Settings ms; ms.target = -9.0f; ms.loudMode = kToTarget; m.set(ms);
        const Stereo o0 = run(m, mix2);
        Stereo loud; loud.L.assign(o0.L.begin() + (long)mix.L.size(), o0.L.end()); loud.R.assign(o0.R.begin() + (long)mix.L.size(), o0.R.end());
        Engine l; l.prepare(sr);
        Settings ls; ls.target = -14.0f; ls.loudMode = kToTarget; l.set(ls);
        l.learnBuffer(loud.L.data(), loud.R.data(), (int)loud.L.size());
        const Stereo o = run(l, twice(loud));
        const Loud lo = measure(o, sr, loud.L.size());
        CHECK(l.plan().makeupDb < -1.0 && std::fabs(lo.I + 14.0) < 1.0 && l.plan().limitLossDb > -0.5,
              "a loud master (%.1f LUFS): makeup %+.2f dB, limiting %.2f dB, output %.2f LUFS",
              l.analysis().lufs, l.plan().makeupDb, l.plan().limitLossDb, lo.I);
    }

    // ---- ADVANCED: offsets land on top of auto; the free bands are an EQ of their own
    {
        Engine e2; e2.prepare(sr); e2.setAnalysis(a);
        const Plan base = e2.plan();
        Settings s; s.toneTrim[3] = 2.0f; s.gainTrim = 1.5f; s.ratioTrim = 0.5f; s.liftTrim = -0.3f; s.widthTrim = 1.0f; s.lowMonoTrim = 40.0f;
        e2.set(s); e2.replan();
        const Plan& p = e2.plan();
        CHECK(std::fabs(p.eqGain[3] - (base.eqGain[3] + 2.0)) < 1e-9 && std::fabs(p.autoEq[3] - base.eqGain[3]) < 1e-9,
              "tone trim: %+.2f auto + 2 = %+.2f", p.autoEq[3], p.eqGain[3]);
        CHECK(std::fabs(p.makeupDb - (p.autoMakeup + 1.5)) < 1e-6 && std::fabs(p.ratio - 1.5) < 1e-6 && std::fabs(p.lift - (base.lift - 0.3)) < 1e-6
              && std::fabs(gainToDb(p.sideHi) - (gainToDb(p.autoSideHi) + 1.0)) < 1e-6 && std::fabs(p.sideLo - (p.autoSideLo - 0.4)) < 1e-6,
              "gain, glue ratio, lift, width and low-mono trims add to auto (makeup %+.2f = %+.2f + 1.5, ratio %.2f, lift %.2f, width %+.2f dB on %+.2f, side lo %.2f on %.2f)",
              p.makeupDb, p.autoMakeup, p.ratio, p.lift, gainToDb(p.sideHi), gainToDb(p.autoSideHi), p.sideLo, p.autoSideLo);
        // a +6 dB bell at 1 kHz, Q 1, with no analysis (bands work before LEARN): a 1 kHz tone comes out 6 dB up
        Engine b; b.prepare(sr);
        Settings bs; bs.band[2].on = true; bs.band[2].freq = 1000.0f; bs.band[2].gain = 6.0f; bs.band[2].q = 1.0f; b.set(bs);
        const int n = (int)sr;
        Stereo t; t.L.resize((size_t)n); for (int i = 0; i < n; ++i) t.L[i] = 0.1f * (float)std::sin(kTwoPi * 1000.0 * i / sr); t.R = t.L;
        const Stereo o = run(b, t);
        double pk = 0.0; for (int i = n / 2; i < n; ++i) pk = std::max(pk, (double)std::fabs(o.L[i]));
        CHECK(std::fabs(gainToDb(pk / 0.1) - 6.0) < 0.1, "free band: +6 dB bell at 1 kHz gives %+.2f dB", gainToDb(pk / 0.1));
        bs.band[2].on = false; b.set(bs);
        const Stereo o2 = run(b, t);
        double mx = 0.0; for (int i = n / 2; i < n; ++i) mx = std::max(mx, (double)std::fabs(o2.L[i] - t.L[i - b.latency()]));
        CHECK(mx < 1e-6, "free band off: transparent again (diff %.2g)", mx);
    }

    // ---- the analysis as text
    {
        const std::string t = serializeAnalysis(a);
        Analysis b;
        const bool ok = parseAnalysis(t.c_str(), b);
        double md = 0.0;
        for (int i = 0; i < kBands; ++i) md = std::max(md, (double)std::fabs(b.mid[i] - a.mid[i]) + std::fabs(b.side[i] - a.side[i]));
        CHECK(ok && b.valid && std::fabs(b.lufs - a.lufs) < 0.001 && md < 0.011 && std::fabs(b.q[5] - a.q[5]) < 0.006,
              "text round trip (%zu chars)", t.size());
        Analysis c;
        const bool bad1 = parseAnalysis("pumpmaster1;sec=3;lufs=-14", c);
        const bool bad2 = parseAnalysis("hello", c);
        std::string cut = t.substr(0, t.size() / 2);
        const bool bad3 = parseAnalysis(cut.c_str(), c);
        CHECK(!bad1 && !bad2 && !bad3 && !c.valid, "incomplete or foreign text is rejected");
        CHECK(serializeAnalysis(Analysis()).empty(), "no analysis: empty text");
        // the plugin's path: an analysis loaded through `incoming` reaches the plan at the next block
        Engine l; l.prepare(sr);
        l.incoming.store(b); l.incomingSerial.fetch_add(1);
        float x[64] = {}, y[64] = {};
        l.process(x, y, 64);
        Analysis pub;
        CHECK(l.plan().valid && l.published.load(pub) && pub.valid && std::fabs(pub.lufs - a.lufs) < 0.001, "loaded analysis in use and published");
    }

    // ---- LEARN through process(): start, play, finish
    {
        Engine l; l.prepare(sr);
        l.learnCmd.store(1);
        run(l, mix);
        l.learnCmd.store(2);
        float x[64] = {}, y[64] = {};
        l.process(x, y, 64);
        CHECK(l.hasAnalysis() && std::fabs(l.analysis().lufs - a.lufs) < 0.05 && !l.uiLearning.load(), "LEARN while playing: %.2f LUFS", l.analysis().lufs);
        // a learn on silence keeps the old analysis
        l.learnCmd.store(1);
        Stereo quiet; quiet.L.assign((size_t)sr * 3, 0.0f); quiet.R = quiet.L;
        run(l, quiet);
        l.learnCmd.store(2);
        l.process(x, y, 64);
        CHECK(l.hasAnalysis() && std::fabs(l.analysis().lufs - a.lufs) < 0.05, "learning silence keeps the previous analysis");
    }

    // ---- stage switches and jumps: no clicks, always finite
    {
        Engine j; j.prepare(sr); j.setAnalysis(a);
        Stereo o = mix;
        std::mt19937 rng(3); std::uniform_real_distribution<float> u(0, 1);
        const int blk = 480;
        double maxStep = 0.0, refStep = 0.0;
        float pl = 0, pr = 0;
        bool finite = true;
        for (size_t off = 0; off + blk <= o.L.size(); off += blk)
        {
            if ((off / blk) % 20 == 0)
            {
                Settings s;
                s.toneOn = u(rng) < 0.5f; s.dynOn = u(rng) < 0.5f; s.stereoOn = u(rng) < 0.5f; s.loudOn = u(rng) < 0.7f;
                s.intensity = 100 * u(rng); s.style = (int)(u(rng) * 4); s.toneAmt = 200 * u(rng);
                s.compare = u(rng) < 0.2f; s.target = -20 + 14 * u(rng); s.loudMode = u(rng) < 0.5f ? kGentle : kToTarget;
                j.set(s);
            }
            j.process(o.L.data() + off, o.R.data() + off, blk);
            for (int i = 0; i < blk; ++i)
            {
                const float l = o.L[off + i], r = o.R[off + i];
                finite = finite && std::isfinite(l) && std::isfinite(r);
                maxStep = std::max(maxStep, (double)std::max(std::fabs(l - pl), std::fabs(r - pr)));
                pl = l; pr = r;
            }
        }
        // the reference: the loudest these settings can make it, held steady (target -6, everything at 100 %)
        {
            Engine ref; ref.prepare(sr); ref.setAnalysis(a);
            Settings s; s.target = -6.0f; s.intensity = 100.0f; s.toneAmt = 200.0f; s.loudMode = kToTarget; ref.set(s);
            const Stereo o2 = run(ref, mix);
            float ql = 0, qr = 0;
            for (size_t i = 0; i < o2.L.size(); ++i) { refStep = std::max(refStep, (double)std::max(std::fabs(o2.L[i] - ql), std::fabs(o2.R[i] - qr))); ql = o2.L[i]; qr = o2.R[i]; }
        }
        CHECK(finite && maxStep < 1.3 * refStep, "random settings every 200 ms: finite, largest step %.3f (steady at the loudest settings %.3f)", maxStep, refStep);
    }

    // ---- other rates
    for (double rate : { 44100.0, 96000.0 })
    {
        Engine r; r.prepare(rate); Settings ts; ts.loudMode = kToTarget; r.set(ts);
        r.learnBuffer(mix.L.data(), mix.R.data(), (int)mix.L.size());   // (the mix played at another rate: a different song, fine)
        const Stereo o = run(r, mix2);
        const Loud lo = measure(o, rate, mix.L.size());
        CHECK(r.hasAnalysis() && std::fabs(lo.I + 12.0) < 1.0 && maxAbsCurve(r) < 3.5, "%.1f kHz: output %.2f LUFS, tone within %.2f dB",
              rate / 1000.0, lo.I, maxAbsCurve(r));
    }

    printf(fails ? "MASTER: %d FAILED\n" : "MASTER: all passed\n", fails);
    return fails ? 1 : 0;
}
