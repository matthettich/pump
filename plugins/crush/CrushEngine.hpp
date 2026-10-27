// pump CRUSH engine: lo-fi degrader. Framework-free.
//
// Signal path (stereo). Two stages, in either order (MEDIA after or before the sampler), then the output filters:
//
//   SAMPLER   drive (into the converter's own soft warmth, then its hard clip at full scale)
//             -> input filter (0 / 2 / 4 poles at 0.45 x the rate: none lets everything above the new Nyquist fold
//                back down as aliasing, which is most of the classic gritty sound)
//             -> sample and hold at RATE (a fractional clock shared by both channels; JITTER makes the clock
//                irregular, sample to sample and slowly)
//             -> quantise to BITS (fractional word lengths sweep smoothly): linear, companded (mu-law: quiet parts
//                keep their detail, loud parts get grainy) or linear through a DAC whose bit weights are slightly
//                off (the uneven, "broken staircase" nonlinearity of cheap converters)
//             -> output filter (0 / 2 / 4 poles; Dark 12's is low and resonant)
//             The voicings (Clean, Grit 12, Hi-fi 12, Dark 12, Companded 8, Toy, Phone) set the quantiser, the
//             filters and the warmth; RATE and BITS stay where you put them.
//   MEDIA     wow and flutter (one transport for both channels: a modulated delay around a fixed 6 ms, which is
//             the plugin's latency) -> age (high and low loss, saturation, slow level drift) -> dropouts (random
//             dips with high-frequency loss, slightly different per channel) -> hiss and crackle (surface noise,
//             clicks and the odd pop) added on top.
//   OUTPUT    low cut, high cut -> mix with the dry signal (delayed by the same 6 ms, so parallel crushing lines up)
//             -> output gain.
// Voicing and order changes fade the output out, switch, and fade back in (6 ms each way). Noise and crackle are
// deterministic (seeded): renders repeat exactly.
#pragma once

#include "dsp/Fx.hpp"
#include "CrushParams.hpp"

namespace pump { namespace crush {

struct Settings {
    int mode = kGrit12;
    double drive = 0.0, bits = 12.0, rate = 26040.0, jitter = 0.0;
    int order = kMediaAfter;
    double wow = 0.0, flutter = 0.0, hiss = 0.0, crackle = 0.0, dropout = 0.0, age = 0.0;
    double lowCut = kLowCutOff, highCut = kHighCutOff, mix = 100.0, outDb = 0.0;
};

// ---- the quantiser -----------------------------------------------------------------------------------------------
constexpr double kMu = 255.0;

// fixed per-bit weight errors (bit 0 = LSB), in -1..1; the MSBs get the largest (as in real ladders, where the
// biggest resistors dominate the mismatch)
inline const double* dacErrors()
{
    static double e[24];
    static bool init = false;
    if (!init)
    {
        fx::Rng r(0xC0FFEEu);
        for (int k = 0; k < 24; ++k) e[k] = r.bi();
        init = true;
    }
    return e;
}

// x in -1..1 (already clipped); bits >= 1 (fractional = smooth sweep); quant 0 linear, 1 mu-law, 2 DAC errors
inline double quantise(double x, double bits, int quant, double dacError)
{
    if (quant == 1)
    {
        const double a = std::fabs(x), sg = x < 0.0 ? -1.0 : 1.0;
        const double c = std::log1p(kMu * a) / std::log1p(kMu);
        const double q = std::exp2(bits - 1.0);
        const double cq = std::min(1.0, std::round(c * q) / q);
        return sg * (std::pow(1.0 + kMu, cq) - 1.0) / kMu;
    }
    if (quant == 2 && dacError > 0.0 && bits <= 16.5)
    {
        const int B = clampv((int)std::lround(bits), 1, 16);
        const double half = std::ldexp(1.0, B - 1);
        const long code = clampv((long)std::lround(x * half), -(long)half, (long)half - 1);
        const unsigned long u = (unsigned long)(code + (long)half);   // offset binary
        const double* e = dacErrors();
        double val = 0.0, mid = 0.0;
        for (int k = 0; k < B; ++k)
        {
            const double w = std::ldexp(1.0, k) * (1.0 + dacError * e[k] * (0.3 + 0.7 * (k + 1) / B));
            if (u & (1ul << k)) val += w;
            if (k == B - 1) mid = w;   // code 0 = only the top bit set
        }
        return (val - mid) / half;
    }
    const double q = std::exp2(bits - 1.0);
    return std::round(x * q) / q;
}

// Butterworth cascades of TPT SVFs (2, 4 or 8 poles), low or high pass; q scales the resonance of the last section
struct Cascade {
    fx::Svf s[4];
    int n = 0;   // sections
    void set(int poles, double f, double q, double sr)
    {
        static const double q2[] = { 0.70710678 }, q4[] = { 0.5412, 1.3066 }, q8[] = { 0.5098, 0.6013, 0.9000, 2.5629 };
        const double* t = poles == 8 ? q8 : (poles == 4 ? q4 : q2);
        n = poles == 8 ? 4 : (poles == 4 ? 2 : (poles == 2 ? 1 : 0));
        for (int k = 0; k < n; ++k) s[k].set(f, t[k] * (k == n - 1 ? q : 1.0), sr);
    }
    void reset() { for (auto& x : s) x.reset(); }
    inline double lp(double x) { for (int k = 0; k < n; ++k) x = s[k].lowpass(x); return x; }
    inline double hp(double x) { for (int k = 0; k < n; ++k) x = s[k].highpass(x); return x; }
};

class Engine {
public:
    static constexpr int kChunk = 256;
    static constexpr int kCtl = 32;   // filter coefficients follow the gliding rate every 32 samples

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        D = std::max(8, (int)std::lround(0.006 * sr));
        for (int ch = 0; ch < 2; ++ch)
        {
            wowLine[ch].setMax(2 * D + 16);
            dry[ch].setMaxDelay(D + 2);
            dry[ch].setDelay(D);
        }
        const int ramp = std::max(1, (int)(0.020 * sr));
        driveS.setRamp(ramp); driveS.reset(set.drive);
        bitsS.setRamp(ramp); bitsS.reset(set.bits);
        rateS.setRamp(ramp); rateS.reset(effRate(set.rate));
        jitS.setRamp(ramp); jitS.reset(set.jitter * 0.01);
        wowS.setRamp(ramp); wowS.reset(set.wow * 0.01);
        flS.setRamp(ramp); flS.reset(set.flutter * 0.01);
        hissS.setRamp(ramp); hissS.reset(set.hiss * 0.01);
        crS.setRamp(ramp); crS.reset(set.crackle * 0.01);
        dropS.setRamp(ramp); dropS.reset(set.dropout * 0.01);
        ageS.setRamp(ramp); ageS.reset(set.age * 0.01);
        mixS.setRamp(ramp); mixS.reset(set.mix * 0.01);
        outS.setRamp(ramp); outS.reset(dbToGain(set.outDb));
        rateOn.setRamp(ramp); rateOn.reset(rateActive(set.rate) ? 1.0 : 0.0);
        bitsOn.setRamp(ramp); bitsOn.reset(bitsActive(set.bits) ? 1.0 : 0.0);
        lcOn.setRamp(ramp); lcOn.reset(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        hcOn.setRamp(ramp); hcOn.reset(highCutActive(set.highCut) ? 1.0 : 0.0);
        trans.setRamp(std::max(1, (int)(0.006 * sr))); trans.reset(1.0);
        curMode = wantMode = set.mode; curOrder = wantOrder = set.order; pending = false;
        wowWander.setup(sr, 0x51u); wowWander.setRate(0.8);
        flWander.setup(sr, 0x77u); flWander.setRate(14.0);
        jitWander.setup(sr, 0x93u); jitWander.setRate(2.0);
        lvlWander.setup(sr, 0xA5u); lvlWander.setRate(0.3);
        wowLfo.setRate(0.55, sr); flLfo.setRate(7.3, sr);
        clearState();
    }

    void setSettings(const Settings& s)
    {
        set = s;
        set.mode = clampv(s.mode, 0, kNumModes - 1);
        set.order = clampv(s.order, 0, kNumOrders - 1);
        wantMode = set.mode; wantOrder = set.order;
        pending = wantMode != curMode || wantOrder != curOrder;
    }

    int latency() const { return D; }

    // ---- feeds (UI) ----
    float rateNow() const { return rateFeed.load(std::memory_order_relaxed); }        // the hold rate in Hz (0 = off)
    float dropNow() const { return dropFeed.load(std::memory_order_relaxed); }        // dropout gain now (1 = none)
    float wowNow() const { return wowFeed.load(std::memory_order_relaxed); }          // transport speed deviation, %
    float clicksNow() { return clickFeed.exchange(0.0f, std::memory_order_relaxed); } // crackle events since last read

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk) processChunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    double effRate(double r) const { return rateActive(r) ? std::min(r, sr) : sr; }

    void clearState()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            aa[ch].reset(); recon[ch].reset(); phoneHp[ch].reset();
            held[ch] = 0.0;
            wowLine[ch].clear(); dry[ch].clear();
            ageLp1[ch].reset(); ageLp2[ch].reset(); ageHp[ch].reset(); dropLp[ch].reset();
            hissLp[ch].reset(); hissHp[ch].reset(); clickHp[ch].reset(); popLp[ch].reset(); surfLp[ch].reset();
            click[ch] = 0.0; pop[ch] = 0.0;
            lc[ch].reset(); hc[ch].reset();
            dropEnv[ch] = 1.0;
        }
        phase = 1.0; dropLeft = 0; dropTarget = 1.0; dropSkew = 1.0;
        rng.seed(0x2545F491u);
        designFilters();
    }
    void applyStructural()
    {
        curMode = wantMode; curOrder = wantOrder;
        for (int ch = 0; ch < 2; ++ch) { aa[ch].reset(); recon[ch].reset(); phoneHp[ch].reset(); held[ch] = 0.0; }
        designFilters();
        pending = false;
    }
    void designFilters()
    {
        const Voicing& v = voicing(curMode);
        const double r = rateS.current();
        const double fa = std::min((v.aaPoles == 8 ? 0.40 : 0.45) * r, 0.49 * sr), fr = std::min(v.reconAt * r, 0.49 * sr);
        for (int ch = 0; ch < 2; ++ch)
        {
            aa[ch].set(v.aaPoles, fa, 1.0, sr);
            recon[ch].set(v.reconPoles, fr, v.reconQ, sr);
            phoneHp[ch].set(2, 300.0, 1.0, sr);
        }
    }

    // ---- the sampler: one channel, one sample (the hold decision is made once per sample for both channels)
    inline double samplerIn(int ch, double x, double drive, double warmth, double rOn)
    {
        double v = x * drive;
        if (warmth > 0.0) v += warmth * ((double)fastTanh((float)(1.3 * v)) / 1.3 - v);
        v = clampv(v, -1.0, 1.0);   // the converter's full scale
        const double f = aa[ch].lp(v);
        return v + (f - v) * rOn;
    }
    inline double samplerOut(int ch, double h, double rOn)
    {
        double y = h;
        const double f = recon[ch].lp(y);
        y += (f - y) * rOn;
        if (curMode == kPhone) y = phoneHp[ch].hp(y);
        return y;
    }

    void processChunk(float* L, float* R, int n)
    {
        if (pending)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) { applyStructural(); trans.setTarget(1.0); }
        }
        else trans.setTarget(1.0);

        driveS.setTarget(set.drive);
        bitsS.setTarget(std::max(1.0, set.bits));
        rateS.setTarget(effRate(set.rate));
        jitS.setTarget(set.jitter * 0.01);
        wowS.setTarget(set.wow * 0.01);
        flS.setTarget(set.flutter * 0.01);
        hissS.setTarget(set.hiss * 0.01);
        crS.setTarget(set.crackle * 0.01);
        dropS.setTarget(set.dropout * 0.01);
        ageS.setTarget(set.age * 0.01);
        mixS.setTarget(set.mix * 0.01);
        outS.setTarget(dbToGain(set.outDb));
        rateOn.setTarget(rateActive(set.rate) ? 1.0 : 0.0);
        bitsOn.setTarget(bitsActive(set.bits) ? 1.0 : 0.0);
        lcOn.setTarget(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        hcOn.setTarget(highCutActive(set.highCut) ? 1.0 : 0.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            lc[ch].set(std::max(10.0, set.lowCut), 0.70710678, sr);
            hc[ch].set(std::min(set.highCut, 0.45 * sr), 0.70710678, sr);
        }

        const Voicing& vc = voicing(curMode);
        const bool mediaFirst = curOrder == kMediaBefore;
        const double wowMax = 0.004 * sr, flMax = 0.00011 * sr;
        for (int i = 0; i < n; ++i)
        {
            if ((ctl++ & (kCtl - 1)) == 0) { designFilters(); updateMediaFilters(); }
            const double drive = dbToGain(driveS.next()), bits = bitsS.next(), rate = rateS.next(), jit = jitS.next();
            const double wow = wowS.next(), fl = flS.next(), hiss = hissS.next(), cr = crS.next(), drop = dropS.next(), age = ageS.next();
            const double mx = mixS.next(), og = outS.next(), tr = trans.next();
            const double rOn = rateOn.next(), bOn = bitsOn.next(), lcm = lcOn.next(), hcm = hcOn.next();

            // the shared clock: does the converter take a new sample now?
            double ratio = rate / sr;
            if (jit > 0.0) ratio *= 1.0 + jit * (0.6 * rng.bi() + 0.03 * jitWander.next());
            phase += clampv(ratio, 0.0005, 1.0);
            const bool take = phase >= 1.0;
            if (take) phase -= std::floor(phase);

            // the transport: one delay for both channels
            const double wv = 0.6 * wowLfo.sine() + 0.4 * wowWander.next();
            const double fv = 0.7 * flLfo.sine() + 0.3 * flWander.next();
            wowLfo.advance(); flLfo.advance();
            const double dl = D + wow * wowMax * wv + fl * flMax * fv;

            // dropouts: one event generator, each channel dips a little differently
            if (dropLeft > 0) { if (--dropLeft == 0) dropTarget = 1.0; }
            else if (drop > 0.0 && rng.uni() < drop * drop * 1.5 / sr)
            {
                dropLeft = (int)((0.02 + 0.18 * rng.uni()) * sr);
                dropTarget = dbToGain(-(3.0 + drop * 24.0 * rng.uni()));
                dropSkew = 0.8 + 0.4 * rng.uni();
            }
            // crackle events (shared timing, most clicks hit both channels at different strengths)
            double clickIn[2] = { 0.0, 0.0 }, popIn[2] = { 0.0, 0.0 };
            if (cr > 0.0)
            {
                if (rng.uni() < cr * cr * 60.0 / sr)
                {
                    const double a = std::min(1.0, cr * 0.35 * std::pow(-std::log(std::max(1e-9, rng.uni())), 1.5));
                    const double sg = rng.uni() < 0.5 ? -1.0 : 1.0;
                    const int which = rng.uni() < 0.7 ? 2 : (rng.uni() < 0.5 ? 0 : 1);
                    for (int ch = 0; ch < 2; ++ch)
                        if (which == 2 || which == ch) clickIn[ch] = sg * a * (which == 2 ? (0.6 + 0.4 * rng.uni()) : 1.0);
                    clickDecay = std::exp(-1.0 / ((0.00015 + 0.0005 * rng.uni()) * sr));
                    clickCount += 1.0f;
                }
                if (rng.uni() < cr * cr * 1.2 / sr)
                {
                    const double a = cr * (0.15 + 0.3 * rng.uni()) * (rng.uni() < 0.5 ? -1.0 : 1.0);
                    popIn[0] = a; popIn[1] = a * (0.7 + 0.3 * rng.uni());
                    clickCount += 1.0f;
                }
            }
            const double lvl = 1.0 + age * 0.08 * lvlWander.next();
            const double hissG = hiss > 0.0 ? dbToGain(-75.0 + 45.0 * hiss) * 1.7 : 0.0;
            const double surfG = cr > 0.0 ? dbToGain(-72.0 + 22.0 * cr) * 1.7 : 0.0;

            double x[2] = { L[i], R[i] }, d[2];
            for (int ch = 0; ch < 2; ++ch) d[ch] = dry[ch].process((float)x[ch]);

            // stage 1 of the sampler (before the hold) for both channels first: the hold is shared
            auto sampler = [&](double* v) {
                double pre[2];
                for (int ch = 0; ch < 2; ++ch) pre[ch] = samplerIn(ch, v[ch], drive, vc.warmth, rOn);
                if (take)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const double q = quantise(pre[ch], bits, vc.quant, vc.dacError);
                        held[ch] = pre[ch] + (q - pre[ch]) * bOn;
                    }
                for (int ch = 0; ch < 2; ++ch)
                {
                    v[ch] = samplerOut(ch, held[ch], rOn) / std::sqrt(drive);   // (rate off: every sample is taken)
                }
            };
            auto media = [&](double* v) {
                for (int ch = 0; ch < 2; ++ch)
                {
                    wowLine[ch].push((float)v[ch]);
                    double y = wowLine[ch].read(dl);
                    if (age > 0.0)
                    {
                        const double t = std::min(1.0, age * 6.0);
                        double a = ageHp[ch].hp(ageLp2[ch].lp(ageLp1[ch].lp(y)));
                        a += age * 0.5 * ((double)fastTanh((float)(1.6 * a)) / 1.6 - a);
                        y = (y + (a - y) * t) * lvl;
                    }
                    // dropouts: gain and a darker sound while dipped
                    const double tgt = dropTarget >= 1.0 ? 1.0 : std::pow(dropTarget, ch ? dropSkew : 1.0);
                    dropEnv[ch] += (tgt - dropEnv[ch]) * (tgt < dropEnv[ch] ? dropAtt : dropRel);
                    if (dropEnv[ch] < 0.9999)
                    {
                        const double dk = dropLp[ch].lp(y);
                        y = (y + (dk - y) * (1.0 - dropEnv[ch])) * dropEnv[ch];
                    }
                    else dropLp[ch].reset(y);
                    // noise on top
                    if (hissG > 0.0) y += hissG * hissHp[ch].hp(hissLp[ch].lp(rng.bi()));
                    if (surfG > 0.0) y += surfG * surfLp[ch].lp(rng.bi());
                    click[ch] = click[ch] * clickDecay + clickIn[ch];
                    pop[ch] = pop[ch] * popDecay + popIn[ch];
                    if (cr > 0.0) y += clickHp[ch].hp(click[ch]) + popLp[ch].lp(pop[ch]);
                    v[ch] = y;
                }
            };
            if (mediaFirst) { media(x); sampler(x); }
            else { sampler(x); media(x); }

            for (int ch = 0; ch < 2; ++ch)
            {
                double v = x[ch];
                const double h = lc[ch].highpass(v);
                v = lcm >= 1.0 ? h : (lcm <= 0.0 ? v : v + (h - v) * lcm);
                const double l = hc[ch].lowpass(v);
                v = hcm >= 1.0 ? l : (hcm <= 0.0 ? v : v + (l - v) * hcm);
                const double o = d[ch] + mx * (v - d[ch]);
                (ch ? R : L)[i] = (float)(o * og * tr);
            }
        }

        double chk = held[0] + held[1] + click[0] + click[1] + pop[0] + pop[1] + dropEnv[0] + dropEnv[1];
        for (int ch = 0; ch < 2; ++ch) chk += aa[ch].s[0].ic1 + aa[ch].s[3].ic1 + recon[ch].s[0].ic1 + recon[ch].s[1].ic1 + lc[ch].ic1 + hc[ch].ic1;
        for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            clearState();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        rateFeed.store(rateActive(set.rate) ? (float)std::min(set.rate, sr) : 0.0f, std::memory_order_relaxed);
        dropFeed.store((float)std::min(dropEnv[0], dropEnv[1]), std::memory_order_relaxed);
        // transport speed deviation (percent): the derivative of the delay, from the last two LFO states
        wowFeed.store((float)(100.0 * (set.wow * 0.01 * wowMax * 0.6 * kTwoPi * 0.55 * std::cos(kTwoPi * wowLfo.phase)
                                       + set.flutter * 0.01 * flMax * 0.7 * kTwoPi * 7.3 * std::cos(kTwoPi * flLfo.phase)) / sr),
                      std::memory_order_relaxed);
        if (clickCount > 0.0f) { clickFeed.store(clickFeed.load(std::memory_order_relaxed) + clickCount, std::memory_order_relaxed); clickCount = 0.0f; }
    }

    void updateMediaFilters()
    {
        const double age = ageS.current();
        const double lpF = std::min(0.45 * sr, 20000.0 * std::pow(2800.0 / 20000.0, age));
        const double hpF = 12.0 * std::pow(10.0, age);
        for (int ch = 0; ch < 2; ++ch)
        {
            ageLp1[ch].set(lpF, sr); ageLp2[ch].set(lpF * 1.3, sr); ageHp[ch].set(hpF, sr);
            dropLp[ch].set(1800.0, sr);
            hissLp[ch].set(7000.0, sr); hissHp[ch].set(450.0, sr);
            clickHp[ch].set(500.0, sr); popLp[ch].set(900.0, sr); surfLp[ch].set(2500.0, sr);
        }
        dropAtt = 1.0 - tauCoef(0.004, sr);
        dropRel = 1.0 - tauCoef(0.025, sr);
        popDecay = tauCoef(0.002, sr);
    }

    double sr = 48000.0;
    int D = 288;
    Settings set;
    int curMode = kGrit12, wantMode = kGrit12, curOrder = kMediaAfter, wantOrder = kMediaAfter;
    bool pending = false;
    unsigned ctl = 0;

    // sampler
    Cascade aa[2], recon[2], phoneHp[2];
    double held[2] = { 0, 0 }, phase = 1.0;
    // media
    fx::FracDelay wowLine[2];
    fx::Lfo wowLfo, flLfo;
    fx::Wander wowWander, flWander, jitWander, lvlWander;
    fx::OnePole ageLp1[2], ageLp2[2], ageHp[2], dropLp[2], hissLp[2], hissHp[2], clickHp[2], popLp[2], surfLp[2];
    double dropEnv[2] = { 1, 1 }, dropTarget = 1.0, dropSkew = 1.0, dropAtt = 0.01, dropRel = 0.001;
    int dropLeft = 0;
    double click[2] = { 0, 0 }, pop[2] = { 0, 0 }, clickDecay = 0.9, popDecay = 0.99;
    float clickCount = 0.0f;
    fx::Rng rng { 0x2545F491u };
    // output
    fx::Svf lc[2], hc[2];
    DelayLine dry[2];

    LinearSmoother driveS, bitsS, rateS, jitS, wowS, flS, hissS, crS, dropS, ageS, mixS, outS, rateOn, bitsOn, lcOn, hcOn, trans;
    std::atomic<float> rateFeed { 0.0f }, dropFeed { 1.0f }, wowFeed { 0.0f }, clickFeed { 0.0f };
};

}} // namespace pump::crush
