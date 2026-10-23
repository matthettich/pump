// pump METER engine: EBU Tech 3341 / 3342 cases (momentary, short-term, integrated with gating, LRA, true peak),
// PLR / PSR, correlation / balance / RMS, history (and the window the UI shows), reset behaviour, NaN / Inf input,
// and bit-exact pass-through.
#include "../plugins/meter/MeterEngine.hpp"
#include <cstdio>
#include <random>
#include <string>

using namespace pump;
using namespace pump::meter;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

// Feed a 1 kHz stereo sine (same on both channels) at `dbfs` for `secs`, in host-sized blocks.
struct Feeder {
    Engine& e; double sr; double phase = 0.0;
    std::vector<float> L, R;
    Feeder(Engine& en) : e(en), sr(en.sampleRate()) {}
    void sine(double dbfs, double secs, double f = 1000.0, double phaseR = 0.0, bool invertR = false)
    {
        const int n = (int)std::lround(secs * sr);
        L.resize((size_t)n); R.resize((size_t)n);
        const double a = dbfs <= -200 ? 0.0 : dbToGain(dbfs);
        for (int i = 0; i < n; ++i)
        {
            L[(size_t)i] = (float)(a * std::sin(phase));
            R[(size_t)i] = (float)((invertR ? -a : a) * std::sin(phase + phaseR));
            phase += kTwoPi * f / sr;
        }
        feed();
    }
    void feed()
    {
        for (size_t off = 0; off < L.size(); off += 512)
            e.process(L.data() + off, R.data() + off, (int)std::min<size_t>(512, L.size() - off));
    }
};
static float I(Engine& e) { return e.out.integrated.load(); }
static float S(Engine& e) { return e.out.shortTerm.load(); }
static float M(Engine& e) { return e.out.momentary.load(); }

static void tech3341()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("EBU Tech 3341 at %.0f Hz\n", sr);
        { Engine e; e.prepare(sr); Feeder f(e); f.sine(-23, 20);
          CHECK(std::fabs(M(e) + 23) < 0.1 && std::fabs(S(e) + 23) < 0.1 && std::fabs(I(e) + 23) < 0.1, "case 1: 1 kHz -23 dBFS 20 s -> M %.2f  S %.2f  I %.2f LUFS", M(e), S(e), I(e)); }
        { Engine e; e.prepare(sr); Feeder f(e); f.sine(-33, 20);
          CHECK(std::fabs(M(e) + 33) < 0.1 && std::fabs(S(e) + 33) < 0.1 && std::fabs(I(e) + 33) < 0.1, "case 2: -33 dBFS -> M %.2f  S %.2f  I %.2f LUFS", M(e), S(e), I(e)); }
        { Engine e; e.prepare(sr); Feeder f(e); f.sine(-36, 10); f.sine(-23, 60); f.sine(-36, 10);
          CHECK(std::fabs(I(e) + 23) < 0.1, "case 3: -36/-23/-36 (10/60/10 s) -> I %.2f LUFS (relative gate)", I(e)); }
        { Engine e; e.prepare(sr); Feeder f(e); f.sine(-72, 10); f.sine(-36, 10); f.sine(-23, 60); f.sine(-36, 10); f.sine(-72, 10);
          CHECK(std::fabs(I(e) + 23) < 0.1, "case 4: -72/-36/-23/-36/-72 -> I %.2f LUFS (absolute + relative gate)", I(e)); }
        { Engine e; e.prepare(sr); Feeder f(e); f.sine(-26, 20.1); f.sine(-20, 20.1); f.sine(-26, 20.1);
          CHECK(std::fabs(I(e) + 23) < 0.1, "case 5: -26/-20/-26 (20.1 s each) -> I %.2f LUFS", I(e)); }
        // case 8 (momentary constancy): alternating 0.18 s at -20 and 0.22 s at -30 -> M = -23 after 1 s
        { Engine e; e.prepare(sr); Feeder f(e);
          float lo = 100, hi = -100;
          for (int k = 0; k < 25; ++k) { f.sine(-20, 0.18); f.sine(-30, 0.22); if (k >= 3) { lo = std::min(lo, M(e)); hi = std::max(hi, M(e)); } }
          CHECK(lo > -23.1f && hi < -22.9f, "case 8: momentary on 0.18/0.22 s alternation stays %.2f .. %.2f LUFS (expect -23.0 +-0.1)", lo, hi); }
        // case 9 (short-term constancy): alternating 1.34 s at -20 and 1.66 s at -30 -> S = -23 after 3 s
        { Engine e; e.prepare(sr); Feeder f(e);
          float lo = 100, hi = -100;
          for (int k = 0; k < 8; ++k)
          {
              for (int j = 0; j < 134; ++j) { f.sine(-20, 0.01); if (k >= 1) { lo = std::min(lo, S(e)); hi = std::max(hi, S(e)); } }
              for (int j = 0; j < 166; ++j) { f.sine(-30, 0.01); if (k >= 1) { lo = std::min(lo, S(e)); hi = std::max(hi, S(e)); } }
          }
          CHECK(lo > -23.1f && hi < -22.9f, "case 9: short-term on 1.34/1.66 s alternation stays %.2f .. %.2f LUFS (expect -23.0 +-0.1)", lo, hi); }
    }
    // true peak (48 kHz): Tech 3341 cases 15-19 style signals, expected +0.2 / -0.4 dB
    printf("True peak (48 kHz, expected within +0.2 / -0.4 dB)\n");
    struct TPCase { const char* name; double f, db, ph; };
    const TPCase cases[] = { { "576 Hz -6 dBFS", 576, -6, 0 }, { "576 Hz -6 dBFS, 45 deg", 576, -6, kPi / 4 },
                             { "1882 Hz -6 dBFS", 1882, -6, 0 }, { "9.6 kHz -6 dBFS", 9600, -6, kPi / 7 },
                             { "12 kHz at 45 deg (sample peak -3 dB lower)", 12000, 0, kPi / 4 }, { "19 kHz -3 dBFS", 19000, -3, 0.3 } };
    for (const auto& c : cases)
    {
        // 20 ms raised-cosine fade-in: an abrupt start overshoots in any band-limited reconstruction
        Engine e; e.prepare(48000);
        std::vector<float> L(144000), R(144000);
        const double a = dbToGain(c.db);
        for (int i = 0; i < 144000; ++i)
        {
            const double w = i < 960 ? 0.5 - 0.5 * std::cos(kPi * i / 960.0) : 1.0;
            L[(size_t)i] = R[(size_t)i] = (float)(w * a * std::sin(kTwoPi * c.f * i / 48000.0 + c.ph));
        }
        for (int off = 0; off < 144000; off += 512) e.process(L.data() + off, R.data() + off, std::min(512, 144000 - off));
        float sp = 0; for (float v : L) sp = std::max(sp, std::fabs(v));
        const float tp = e.out.truePeak.load();
        CHECK(tp - c.db < 0.2 && tp - c.db > -0.4, "%s: %.2f dBTP (true %.1f, sample peak %.2f dBFS)", c.name, tp, c.db, gainToDb(sp));
    }
}

static void tech3342()
{
    printf("EBU Tech 3342 (LRA)\n");
    for (double sr : { 44100.0, 48000.0 })
    {
        struct C { const char* name; std::vector<double> lv; double expect; };
        const C cs[] = { { "case 1: -20/-30 dBFS", { -20, -30 }, 10.0 }, { "case 2: -20/-15", { -20, -15 }, 5.0 },
                         { "case 3: -40/-20", { -40, -20 }, 20.0 }, { "case 4: -50/-35/-20/-35/-50", { -50, -35, -20, -35, -50 }, 15.0 } };
        for (const auto& c : cs)
        {
            Engine e; e.prepare(sr); Feeder f(e);
            for (double l : c.lv) f.sine(l, 20.0);
            const float lra = e.out.range.load();
            CHECK(std::fabs(lra - c.expect) <= 1.0, "%.0f Hz %s -> LRA %.2f LU (expect %.0f +-1)", sr, c.name, lra, c.expect);
        }
    }
}

static void plrPsr()
{
    printf("PLR / PSR\n");
    Engine e; e.prepare(48000); Feeder f(e);
    f.sine(-23, 10);
    const float plr0 = e.out.plr.load(), tp0 = e.out.truePeak.load(), i0 = I(e);
    CHECK(std::fabs(plr0 - (tp0 - i0)) < 1e-4 && std::fabs(plr0) < 0.2, "sine: PLR %.2f = TP max %.2f - I %.2f (a sine's peak-to-loudness is ~0)", plr0, tp0, i0);
    // one sample spike at 0.5 (-6.02 dBFS) 10 s in, then 1 more second
    f.sine(-23, 0.5);
    { std::vector<float> l(1, 0.5f), r(1, 0.5f); e.process(l.data(), r.data(), 1); }
    f.sine(-23, 0.5);
    const float tp1 = e.out.truePeak.load(), plr1 = e.out.plr.load(), psr1 = e.out.psr.load(), stp1 = e.out.shortTP.load(), s1 = S(e);
    CHECK(tp1 >= -6.03f && std::fabs(plr1 - (tp1 - I(e))) < 1e-4 && std::fabs(plr1 - 17.0f) < 0.6f, "after a -6 dBFS spike: TP max %.2f dBTP, PLR %.2f LU (= TP - I)", tp1, plr1);
    CHECK(std::fabs(psr1 - (stp1 - s1)) < 1e-4 && psr1 > 16.0f, "PSR %.2f = short-term TP %.2f - short-term %.2f", psr1, stp1, s1);
    f.sine(-23, 3.5);
    const float psr2 = e.out.psr.load(), plr2 = e.out.plr.load();
    CHECK(std::fabs(psr2) < 0.2f && plr2 > 16.0f, "3.5 s later: PSR back to %.2f (spike left the 3 s window), PLR stays %.2f (max)", psr2, plr2);
    CHECK(e.out.maxMomentary.load() >= M(e) - 1e-3f && e.out.maxShortTerm.load() >= S(e) - 1e-3f, "max momentary %.2f, max short-term %.2f", e.out.maxMomentary.load(), e.out.maxShortTerm.load());
}

static void stereo()
{
    printf("Correlation, balance, RMS, peaks\n");
    { Engine e; e.prepare(48000); Feeder f(e); f.sine(-20, 2);
      CHECK(e.out.correlation.load() > 0.999f && std::fabs(e.out.balance.load()) < 0.01f, "mono: correlation %+.3f, balance %+.2f dB", e.out.correlation.load(), e.out.balance.load());
      CHECK(std::fabs(e.out.rmsDb[0].load() + 23.01f) < 0.1f, "sine -20 dBFS: RMS %.2f dBFS (peak - 3.01)", e.out.rmsDb[0].load());
      const float pk = gainToDbF(e.peak[0].exchange(0.0f));
      CHECK(std::fabs(pk + 20.0f) < 0.01f, "sample peak %.3f dBFS", pk); }
    { Engine e; e.prepare(48000); Feeder f(e); f.sine(-20, 2, 1000, 0, true);
      CHECK(e.out.correlation.load() < -0.999f, "polarity-inverted: correlation %+.3f", e.out.correlation.load()); }
    { Engine e; e.prepare(48000); Feeder f(e); f.sine(-20, 2, 1000, kPi / 2);
      CHECK(std::fabs(e.out.correlation.load()) < 0.02f, "90 degrees apart: correlation %+.3f", e.out.correlation.load()); }
    { Engine e; e.prepare(48000);
      std::mt19937 rng(3); std::normal_distribution<float> nd(0, 0.1f);
      std::vector<float> L(96000), R(96000);
      for (int i = 0; i < 96000; ++i) { L[(size_t)i] = nd(rng); R[(size_t)i] = 0.5f * nd(rng); }
      e.process(L.data(), R.data(), 96000);
      CHECK(std::fabs(e.out.correlation.load()) < 0.1f && std::fabs(e.out.balance.load() - 6.02f) < 0.6f, "independent noise, R 6 dB lower: correlation %+.3f, balance %+.2f dB", e.out.correlation.load(), e.out.balance.load()); }
    { Engine e; e.prepare(48000); std::vector<float> z(48000, 0.0f);
      e.process(z.data(), z.data(), 48000);
      CHECK(e.out.silent.load() && e.out.correlation.load() == 0.0f && !std::isfinite(I(e)) && !std::isfinite(e.out.truePeak.load()), "silence: no correlation, I and TP -inf"); }
}

static void history()
{
    printf("History\n");
    Engine e; e.prepare(48000); Feeder f(e);
    const uint32_t p0 = e.histS.position();
    f.sine(-18, 12.0);
    const uint32_t n = e.histS.position() - p0;
    CHECK(n == 120, "12 s -> %u history points (10 per second)", n);
    const float last = e.histS.at(e.histS.position() - 1), lastI = e.histI.at(e.histI.position() - 1);
    CHECK(std::fabs(last + 18.0f) < 0.1f && std::fabs(lastI + 18.0f) < 0.1f, "last points: short-term %.2f, integrated %.2f LUFS", last, lastI);

    // the window the UI draws: always the newest points, each where the time axis says it is, however long it runs
    // (the ring holds 54 min; views are 1..30 min)
    bool allOk = true;
    for (double minutes : { 0.0, 53.0, 54.0, 105.0, 120.0, 1800.0 })
    {
        ScopeRing r(1 << 15);
        for (int i = 0; i < 777; ++i) r.push(-1.0f);   // points before the reset
        const uint32_t start = r.position();
        const uint32_t pts = (uint32_t)std::lround(minutes * 60.0 * Engine::kHistRate);
        for (uint32_t i = 0; i < pts; ++i) r.push((float)i);   // value = index since the reset
        for (int view : { 600, 1800, 6000, 18000 })
        {
            const HistoryWindow w = historyWindow(r.position(), start, r.capacity(), view);
            const uint32_t want = std::min<uint32_t>(pts, (uint32_t)view);
            bool ok = w.count == (int)want && w.first == pts - want;
            for (int i = 0; ok && i < w.count; ++i) ok = r.at(w.base + (uint32_t)i) == (float)(w.first + (uint32_t)i);
            if (!ok) { allOk = false; printf("  window wrong after %.0f min, view %d: count %d first %u\n", minutes, view, w.count, w.first); }
        }
    }
    CHECK(allOk, "history window: newest points, right time axis after 0 min, 53 min, 54 min, 105 min, 2 h and 30 h (all four views)");
}

static void resets()
{
    printf("Reset\n");
    Engine e; e.prepare(48000); Feeder f(e);
    f.sine(-20, 10);
    const uint32_t c0 = e.resetCount.load(), h0 = e.histStart.load();
    e.setResetCounter(1);
    CHECK(e.resetCount.load() == c0 + 1 && !std::isfinite(I(e)) && !std::isfinite(e.out.range.load()) && !std::isfinite(e.out.truePeak.load())
          && !std::isfinite(e.out.maxShortTerm.load()) && !std::isfinite(M(e)), "counter 0 -> 1: everything reset (I, LRA, TP, maxima, momentary)");
    CHECK(e.histStart.load() == e.histS.position() && e.histStart.load() > h0, "history restarts at the reset (position %u)", e.histStart.load());
    f.sine(-30, 5);
    const float i1 = I(e);
    CHECK(std::fabs(i1 + 30.0f) < 0.1f, "after the reset only new audio counts: I %.2f LUFS", i1);
    e.setResetCounter(1);   // same value again (host echo, state reload): no reset
    CHECK(std::fabs(I(e) - i1) < 1e-6, "same counter again: no reset (I %.2f)", I(e));
    e.setResetCounter(0);   // undo / A/B / preset bring an older value back: no reset
    CHECK(std::fabs(I(e) - i1) < 1e-6 && e.resetHighWater() == 1.0f, "lower counter (undo): no reset, high water %.0f", e.resetHighWater());
    e.setResetCounter(e.resetHighWater() + 1.0f);
    CHECK(!std::isfinite(I(e)) && e.resetCount.load() == c0 + 2, "high water + 1: reset");
    // RMS, correlation and balance start over too (on a reset and on prepare / re-activation)
    for (int how = 0; how < 2; ++how)
    {
        f.sine(-20, 2.0);
        if (how == 0) e.setResetCounter(e.resetHighWater() + 1.0f); else e.prepare(48000);
        const float z = 0.0f;
        e.process(&z, &z, 1);
        CHECK(e.out.rmsDb[0].load() < -100.0f && e.out.silent.load() && e.out.correlation.load() == 0.0f,
              "%s: RMS %.0f dBFS, correlation %.2f right after (no -20 dBFS sine left in the averages)", how == 0 ? "reset" : "prepare", e.out.rmsDb[0].load(), e.out.correlation.load());
    }
}

static void badInput()
{
    printf("NaN / Inf input\n");
    // a NaN or Inf sample is measured as silence: nothing may stick at NaN, and integrated loudness keeps moving
    Engine e; e.prepare(48000); Feeder f(e);
    f.sine(-20, 4.0);
    {
        std::vector<float> l(512, 0.0f), r(512, 0.0f);
        l[5] = NAN; r[9] = INFINITY; l[100] = -INFINITY; r[200] = NAN;
        e.process(l.data(), r.data(), 512);
    }
    f.sine(-20, 2.0);
    const float rms = e.out.rmsDb[0].load(), corr = e.out.correlation.load(), bal = e.out.balance.load(), tp = e.out.truePeak.load(), i1 = I(e);
    CHECK(std::fabs(rms + 23.01f) < 0.1f && corr > 0.999f && std::fabs(bal) < 0.01f && std::fabs(tp + 20.0f) < 0.3f && std::fabs(M(e) + 20.0f) < 0.1f && std::fabs(i1 + 20.0f) < 0.2f,
          "2 s after NaN / Inf samples: RMS %.2f dBFS, correlation %+.3f, balance %+.2f dB, TP %.2f dBTP, M %.2f, I %.2f LUFS", rms, corr, bal, tp, M(e), i1);
    f.sine(-26, 20.0);
    const float i2 = I(e);
    CHECK(i2 < i1 - 2.0f && std::isfinite(i2) && std::fabs(M(e) + 26.0f) < 0.1f, "integrated keeps moving: %.2f -> %.2f LUFS after 20 s at -26 dBFS", i1, i2);
    const float pk = gainToDbF(e.peak[0].exchange(0.0f)), tpk = gainToDbF(e.tpPeak[1].exchange(0.0f));
    CHECK(std::isfinite(pk) && std::isfinite(tpk), "peak feeds finite (%.2f dBFS, TP %.2f dBTP)", pk, tpk);
}

static void passThrough()
{
    printf("Pass-through\n");
    // the plugin copies input to output and the engine only reads: compare against the input, bit for bit
    std::mt19937 rng(9); std::uniform_real_distribution<float> u(-1.5f, 1.5f);
    std::vector<float> inL(100000), inR(100000);
    for (size_t i = 0; i < inL.size(); ++i) { inL[i] = u(rng); inR[i] = (i % 7 == 0) ? 0.0f : u(rng); }
    inL[5] = INFINITY * 0.0f; inL[6] = 1e-40f; inR[7] = -0.0f;   // NaN, denormal and -0 survive untouched too
    std::vector<float> outL(inL.size()), outR(inR.size());
    Engine e; e.prepare(48000);
    for (size_t off = 0; off < inL.size(); off += 333)
    {
        const int n = (int)std::min<size_t>(333, inL.size() - off);
        std::memcpy(outL.data() + off, inL.data() + off, sizeof(float) * (size_t)n);
        std::memcpy(outR.data() + off, inR.data() + off, sizeof(float) * (size_t)n);
        e.process(outL.data() + off, outR.data() + off, n);
    }
    CHECK(std::memcmp(outL.data(), inL.data(), sizeof(float) * inL.size()) == 0 && std::memcmp(outR.data(), inR.data(), sizeof(float) * inR.size()) == 0,
          "100000 samples (incl. NaN, denormal, -0, > 0 dBFS) come out bit-identical");
}

int main()
{
    tech3341();
    tech3342();
    plrPsr();
    stereo();
    history();
    resets();
    badInput();
    passThrough();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
