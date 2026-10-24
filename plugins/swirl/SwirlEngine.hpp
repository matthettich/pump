// pump SWIRL engine: chorus, flanger and phaser. Framework-free.
//
// One LFO (free at RATE, or locked to the host's song position at a note DIVISION), read at two phases SPREAD apart
// for the left and right sides, drives one of six types:
//   Ensemble  two bucket-brigade lines (3..9 ms), one per side, swept in opposite directions by a slow rounded
//             triangle; each line also feeds the other side, inverted (more of it in the higher variants): one side
//             rises while the other falls, so the image widens and moves instead of the whole sound wobbling
//   Analog    a bucket-brigade chorus: one line per side (2..12 ms), triangle sweep (sine in VIBRATO); the variants
//             are slow, fast, a fast shallow shimmer and a pitch vibrato
//   Rack      clean digital chorus: 2 / 3 / 4 / 6 voices per side (5..25 ms), sine sweeps at evenly spread phases and
//             slightly different rates, summed
//   Flanger   one short line per side (0.25..8 ms); CLASSIC, NEGATIVE (the swept copy inverted: hollow), JET (made
//             for high feedback) and THRU-0, where the dry signal is itself delayed by a fixed 3 ms and the swept copy
//             passes through it, so the comb collapses to nothing and back (3 ms latency in that variant only)
//   Phaser    4 / 6 / 8 / 12 first-order all-pass stages at one swept frequency, with feedback around them
//   Lamp      four all-pass stages spread over ~4 octaves; the LFO lights a lamp that a photocell follows (fast to
//             brighten, slow to dim), so the sweep lingers low and snaps up
// MANUAL sets the centre (delay or frequency), DEPTH the sweep, FEEDBACK feeds the wet output back into the line
// (or the all-pass chain). The bucket-brigade types band-limit and soften the wet path and add a little clock hiss
// with WARMTH; the others take a gentle saturation from it. TONE is a high cut on the wet signal. Type and variant
// changes fade the output out, switch, and fade back in (6 ms each way).
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "SwirlParams.hpp"

namespace pump { namespace swirl {

struct Settings {
    int mode = kAnalog, variant = 0;
    double rate = 0.5;
    bool sync = false;
    int division = kNote1;
    double depth = 60.0, manual = 45.0, feedback = 0.0, spread = 90.0, tone = kToneOff, warmth = 30.0;
    double mix = 50.0, outDb = 0.0;
};

constexpr double kTzMs = 3.0;    // through-zero: the fixed delay of the dry copy
constexpr int kMaxVoices = 6;

inline bool isThroughZero(int mode, int variant) { return mode == kFlanger && variant == 1; }
inline int rackVoices(int variant) { static const int n[4] = { 2, 3, 4, 6 }; return n[clampv(variant, 0, 3)]; }
inline int phaserStages(int variant) { static const int n[4] = { 4, 6, 8, 12 }; return n[clampv(variant, 0, 3)]; }
inline double lampRatio(int k) { static const double r[4] = { 1.0, 2.6, 6.3, 15.0 }; return r[clampv(k, 0, 3)]; }

// LFO shapes, phase 0..1 -> -1..1
inline double triShape(double p) { p -= std::floor(p); return 1.0 - 4.0 * std::fabs(p - 0.5); }
inline double sineShape(double p) { return std::sin(kTwoPi * p); }
// a triangle with its corners rounded (the bucket-brigade choruses and the flanger): no click in the pitch at the turns
inline double roundTri(double p) { const double t = triShape(p); return t * (1.5 - 0.5 * t * t); }

// ---- the swept quantity: delay in ms (delay types) or frequency in Hz (all-pass types), for an LFO value -1..1 ----
// m = manual 0..1, d = depth 0..1
inline double centreMs(int mode, double m)
{
    switch (mode)
    {
    case kEnsemble: return 3.0 + 6.0 * m;
    case kAnalog: return 2.0 + 10.0 * m;
    case kRack: return 5.0 + 20.0 * m;
    case kFlanger: return 0.25 + 7.75 * m * m;
    default: return 0.0;
    }
}
inline double sweepMs(int mode, int variant, double m, double d)
{
    const double c = centreMs(mode, m);
    switch (mode)
    {
    case kEnsemble: return 0.1 + 1.9 * d;
    case kAnalog: return d * std::min(0.45 * c, variant == 3 ? 5.0 : 3.0);
    case kRack: return d * 0.25 * c;
    case kFlanger: return isThroughZero(mode, variant) ? d * kTzMs : d * 0.95 * c;
    default: return 0.0;
    }
}
inline double delayMs(int mode, int variant, double m, double d, double lfo)
{
    if (isThroughZero(mode, variant)) return std::max(0.03, kTzMs + (m - 0.5) * kTzMs + sweepMs(mode, variant, m, d) * lfo);
    return std::max(0.03, centreMs(mode, m) + sweepMs(mode, variant, m, d) * lfo);
}
inline double phaserHz(double m, double d, double lfo) { return 80.0 * std::exp2(m * 6.5 + d * 2.5 * lfo); }
inline double lampHz(double m, double d, double light) { return 50.0 * std::exp2(m * 3.0) * std::exp2(d * 4.5 * (light - 0.3)); }
// first-order all-pass coefficient: H(z) = (a + z^-1) / (1 + a z^-1)
inline double apCoef(double f, double sr) { const double t = std::tan(kPi * clampv(f, 10.0, 0.45 * sr) / sr); return (t - 1.0) / (t + 1.0); }

class Engine {
public:
    static constexpr int kChunk = 256;
    static constexpr int kCtl = 8;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int v = 0; v < 2; ++v) line[ch][v].setMax((int)(0.060 * sr) + 16);
            dry[ch].setMaxDelay((int)(0.001 * kTzMs * sr) + 8);
        }
        const int ramp = std::max(1, (int)(0.030 * sr));
        depthS.setRamp(ramp); depthS.reset(set.depth * 0.01);
        manS.setRamp(ramp); manS.reset(set.manual * 0.01);
        fbS.setRamp(ramp); fbS.reset(set.feedback * 0.01);
        spreadS.setRamp(ramp); spreadS.reset(set.spread / 360.0);
        warmS.setRamp(ramp); warmS.reset(set.warmth * 0.01);
        mixS.setRamp(ramp); mixS.reset(set.mix * 0.01);
        outS.setRamp(ramp); outS.reset(dbToGain(set.outDb));
        toneOn.setRamp(ramp); toneOn.reset(toneActive(set.tone) ? 1.0 : 0.0);
        trans.setRamp(std::max(1, (int)(0.006 * sr))); trans.reset(1.0);
        curMode = wantMode = set.mode; curVar = wantVar = set.variant; pending = false;
        phase = 0.0;
        for (auto& v : vph) v = 0.0;
        clearState();
    }

    void setSettings(const Settings& s)
    {
        set = s;
        set.mode = clampv(s.mode, 0, kNumModes - 1);
        set.variant = clampv(s.variant, 0, 3);
        set.division = clampv(s.division, 0, kNumNotes - 1);
        wantMode = set.mode; wantVar = set.variant;
        pending = wantMode != curMode || wantVar != curVar;
    }
    void setTransport(const Transport& t) { tr = t; }

    int latency() const { return isThroughZero(curMode, curVar) ? (int)std::lround(0.001 * kTzMs * sr) : 0; }

    // ---- feeds (UI) ----
    float lfoPhase() const { return phaseFeed.load(std::memory_order_relaxed); }    // 0..1, left side
    float lightNow(int ch) const { return lightFeed[ch & 1].load(std::memory_order_relaxed); }   // Lamp: the photocell 0..1

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk) processChunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    void clearState()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& l : line[ch]) l.clear();
            dry[ch].clear();
            for (auto& a : ap[ch]) a = 0.0;
            fbState[ch] = 0.0;
            bbdIn[ch].reset(); bbdOut[ch].reset(); toneF[ch].reset();
            light[ch] = 0.0;
            dc[ch].reset();
        }
        designFixed();
    }
    void applyStructural()
    {
        curMode = wantMode; curVar = wantVar;
        clearState();
        const int lat = latency();
        for (int ch = 0; ch < 2; ++ch) dry[ch].setDelay(lat);
        pending = false;
    }
    void designFixed()
    {
        for (int ch = 0; ch < 2; ++ch) { dc[ch].set(8.0, sr); dry[ch].setDelay(latency()); }
        lampUp = 1.0 - std::exp(-1.0 / ((curVar == 3 ? 0.040 : 0.012) * sr));
        lampDown = 1.0 - std::exp(-1.0 / ((curVar == 3 ? 0.280 : 0.080) * sr));
    }
    bool isBbd() const { return curMode == kEnsemble || curMode == kAnalog; }

    // the LFO's phase increment per sample, and (when locked) the phase at the block start
    void clock(int n)
    {
        const double bpm = clampv(tr.bpm, 20.0, 999.0);
        if (set.sync)
        {
            const double q = noteQuarters(set.division);
            inc = bpm / 60.0 / q / sr;
            if (tr.playing && tr.valid && std::isfinite(tr.ppq) && std::fabs(tr.ppq) < 1e9)
            {
                const double want = tr.ppq / q - std::floor(tr.ppq / q);
                // follow the song position; a jump (loop, locate) snaps, small drift glides
                double d = want - phase; d -= std::round(d);
                phase += std::fabs(d) > 0.05 ? d : 0.1 * d;
            }
        }
        else inc = clampv(set.rate, 0.001, 50.0) / sr;
        (void)n;
    }

    void processChunk(float* L, float* R, int n)
    {
        if (pending)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) { applyStructural(); trans.setTarget(1.0); }
        }
        else trans.setTarget(1.0);
        depthS.setTarget(set.depth * 0.01);
        manS.setTarget(set.manual * 0.01);
        fbS.setTarget(clampv(set.feedback, -95.0, 95.0) * 0.01);
        spreadS.setTarget(set.spread / 360.0);
        warmS.setTarget(set.warmth * 0.01);
        mixS.setTarget(set.mix * 0.01);
        outS.setTarget(dbToGain(set.outDb));
        toneOn.setTarget(toneActive(set.tone) ? 1.0 : 0.0);
        clock(n);

        const int mode = curMode, var = curVar;
        const double bw = isBbd() ? (mode == kEnsemble ? 11000.0 : (var == 2 ? 9500.0 : 7500.0)) : 0.0;
        const double msToS = 0.001 * sr;
        const double cross = mode == kEnsemble ? -(0.20 + 0.10 * var) : 0.0;
        const int nv = rackVoices(var), stages = phaserStages(var);
        for (int i = 0; i < n; ++i)
        {
            const double depth = depthS.next(), man = manS.next(), fb = fbS.next(), spread = spreadS.next(), warm = warmS.next();
            const double mx = mixS.next(), og = outS.next(), tn = toneOn.next(), trv = trans.next();
            if ((ctl++ & (kCtl - 1)) == 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    toneF[ch].set(std::min(set.tone, 0.45 * sr), 0.70710678, sr);
                    if (isBbd()) { bbdIn[ch].set(bw * (1.0 - 0.45 * warm), 0.6, sr); bbdOut[ch].set(bw * (1.0 - 0.45 * warm), 0.6, sr); }
                }
            }
            const double ph[2] = { phase, phase + (mode == kEnsemble ? 0.5 : spread) };
            double x[2] = { L[i], R[i] }, d[2], w[2] = { 0.0, 0.0 };
            for (int ch = 0; ch < 2; ++ch) d[ch] = dry[ch].process((float)x[ch]);

            switch (mode)
            {
            case kEnsemble:
            case kAnalog:
            {
                double a[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double lfo = (mode == kAnalog && var == 3) ? sineShape(ph[ch]) : roundTri(ph[ch]);
                    double in = x[ch] + fb * fbState[ch];
                    if (warm > 0.0) in += warm * 0.6 * ((double)fastTanh((float)(1.5 * in)) / 1.5 - in);
                    in = bbdIn[ch].lowpass(in) + warm * 0.0006 * rng.bi();   // clock hiss
                    line[ch][0].push((float)in);
                    a[ch] = bbdOut[ch].lowpass(line[ch][0].read(delayMs(mode, var, man, depth, lfo) * msToS));
                    fbState[ch] = a[ch];
                }
                if (mode == kEnsemble) { w[0] = a[0] + cross * a[1]; w[1] = a[1] + cross * a[0]; }
                else { w[0] = a[0]; w[1] = a[1]; }
                break;
            }
            case kRack:
            {
                const double g = 1.0 / std::sqrt((double)nv);
                for (int ch = 0; ch < 2; ++ch)
                {
                    line[ch][0].push((float)(x[ch] + fb * fbState[ch]));
                    double s = 0.0;
                    for (int v = 0; v < nv; ++v)
                    {
                        // each voice: its own phase, evenly spaced, drifting at a slightly different rate
                        const double pv = vph[v] + (double)v / nv + (ch ? spread : 0.0);
                        s += line[ch][0].read(delayMs(mode, var, man, depth, sineShape(pv)) * msToS);
                    }
                    w[ch] = s * g;
                    fbState[ch] = s / nv;   // the average: the loop gain stays at FEEDBACK whatever the voice count
                }
                break;
            }
            case kFlanger:
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double lfo = roundTri(ph[ch]);
                    line[ch][0].push((float)(x[ch] + fb * fbState[ch]));
                    const double y = line[ch][0].read(delayMs(mode, var, man, depth, lfo) * msToS);
                    fbState[ch] = y;                  // the loop keeps its own polarity:
                    w[ch] = var == 2 ? -y : y;        // NEGATIVE inverts only what is heard
                }
                break;
            }
            case kPhaser:
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    if ((i & (kCtl - 1)) == 0) apA[ch][0] = apCoef(phaserHz(man, depth, sineShape(ph[ch])), sr);
                    const double a = apA[ch][0];
                    double u = x[ch] + fb * fbState[ch];
                    for (int k = 0; k < stages; ++k) { const double y = a * u + ap[ch][k]; ap[ch][k] = u - a * y; u = y; }
                    fbState[ch] = u;
                    w[ch] = u;
                }
                break;
            }
            case kLamp:
            default:
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    // the lamp: brightness follows the LFO squared (a lamp is dark for longer than it is lit); the
                    // photocell brightens fast and dims slowly
                    const double on = 0.5 + 0.5 * sineShape(ph[ch]);
                    const double target = on * on;
                    light[ch] += (target - light[ch]) * (target > light[ch] ? lampUp : lampDown);
                    if ((i & (kCtl - 1)) == 0)
                    {
                        const double f = lampHz(man, var == 2 ? std::min(1.0, depth * 1.25) : depth, light[ch]);
                        for (int k = 0; k < 4; ++k) apA[ch][k] = apCoef(f * lampRatio(k), sr);
                    }
                    double u = x[ch] + fb * fbState[ch];
                    for (int k = 0; k < 4; ++k) { const double y = apA[ch][k] * u + ap[ch][k]; ap[ch][k] = u - apA[ch][k] * y; u = y; }
                    fbState[ch] = u;
                    w[ch] = u;
                }
                break;
            }
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                double v = w[ch];
                if (!isBbd() && warm > 0.0) v += warm * 0.4 * ((double)fastTanh((float)(1.4 * v)) / 1.4 - v);
                const double t = toneF[ch].lowpass(v);
                v = tn >= 1.0 ? t : (tn <= 0.0 ? v : v + (t - v) * tn);
                if (isBbd()) v = dc[ch].process(v);
                // the all-pass types notch when wet and dry meet equally: their mix is a plain crossfade; the delay
                // types add the wet copy to the dry signal up to 50 %, then fade the dry out (so 50 % = both at full
                // level, the way chorus pedals mix, and 100 % = wet only, for vibrato)
                double o;
                if (mode == kPhaser || mode == kLamp) o = d[ch] + mx * (v - d[ch]);
                else o = std::min(1.0, 2.0 * (1.0 - mx)) * d[ch] + std::min(1.0, 2.0 * mx) * v;
                (ch ? R : L)[i] = (float)(o * og * trv);
            }
            phase += inc;
            phase -= std::floor(phase);
            for (int v = 0; v < kMaxVoices; ++v) { vph[v] += inc * (1.0 + 0.071 * v); vph[v] -= std::floor(vph[v]); }
        }
        // a blown-up state (NaN from upstream or runaway feedback) resets
        double chk = fbState[0] + fbState[1] + light[0] + light[1];
        for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { clearState(); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        phaseFeed.store((float)phase, std::memory_order_relaxed);
        lightFeed[0].store((float)light[0], std::memory_order_relaxed);
        lightFeed[1].store((float)light[1], std::memory_order_relaxed);
    }

    double sr = 48000.0;
    Settings set;
    Transport tr;
    int curMode = kAnalog, wantMode = kAnalog, curVar = 0, wantVar = 0;
    bool pending = false;
    unsigned ctl = 0;
    double phase = 0.0, inc = 0.0, vph[kMaxVoices] = {};

    fx::FracDelay line[2][2];
    DelayLine dry[2];
    double ap[2][12] = {}, apA[2][4] = {}, fbState[2] = { 0, 0 }, light[2] = { 0, 0 };
    double lampUp = 0.01, lampDown = 0.001;
    fx::Svf bbdIn[2], bbdOut[2], toneF[2];
    fx::DcBlocker dc[2];
    fx::Rng rng { 0x1234567u };

    LinearSmoother depthS, manS, fbS, spreadS, warmS, mixS, outS, toneOn, trans;
    std::atomic<float> phaseFeed { 0.0f }, lightFeed[2] { { 0.0f }, { 0.0f } };
};

}} // namespace pump::swirl
