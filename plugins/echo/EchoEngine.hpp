// pump ECHO engine: a multi-mode stereo delay. Framework-free.
//
// Two delay lines, up to 8 s each (free times go to 4 s; a synced 2/1 at 60 BPM needs 8). Per line:
//   write  the routed input + FEEDBACK x the loop signal -> LOW CUT -> HIGH CUT -> DIFFUSE (four allpasses, 4-14 ms;
//          the read heads move earlier by their delay so the echo stays on time) -> the mode's record stage
//          (saturation, wear) -> DC blocker -> line
//   read   the mode's read head(s) -> the mode's playback stage -> the echo, and the loop signal
//   out    echoes -> DUCK -> WIDTH (M/S) -> mixed with the dry input (the dry stays at unity up to MIX 50 %, the echo
//          reaches unity at 50 %) -> OUTPUT. No latency: the dry signal is the input itself.
// Routing (crossfaded, 40 ms): Stereo (L into L, R into R, each line feeds itself), Ping-Pong (the mono sum into L;
// the lines feed each other, so repeats alternate sides), Wide (the mono sum into both; each line feeds back 70 % into
// itself and 30 % into the other, so repeats at TIME L, TIME R and their sums bloom across the field).
// FEEDBACK 0..120 % is the loop gain for small signals. Every record stage has unity small-signal gain and a ceiling
// (DRIVE lowers it), so above 100 % the loop self-oscillates into a saturated but bounded state.
//
// Modes (switches crossfade, 40 ms; each mode's filters start fresh):
//   Digital  clean. A time change starts a 50 ms crossfade to a second read head at the new time (queued while one
//            runs), so times move without pitch. AGE = an early converter: sample rate down to 6 kHz, 24 -> 10 bits.
//   Tape     the head glides to a new time (250 ms, at most +-50 % speed): varispeed pitch bends. Wow (smooth random,
//            0.6-1.1 Hz) and flutter (7.1 + 9.4 Hz) move the head, the same on both channels as on one tape. Record:
//            a soft tape curve (gentle compression from low levels, a touch of 2nd harmonic) and an HF loss from
//            15 kHz down to 3.75 kHz with AGE. Playback: a head bump (95 Hz, +1.5..+3.5 dB), a 30 Hz roll-off and
//            hiss (-90..-64 dBFS). AGE also deepens the wow and flutter.
//   BBD      bucket-brigade: the clock follows the delay time (an 8192-stage line), so the anti-alias and
//            reconstruction low-passes (12 dB/oct each, both in the loop) sit at 1740 Hz x s / T: bright at short
//            times, dark at long ones (800 Hz floor). Companding: the signal is compressed (square-root law, peak
//            detector, 30 ms release) into the buckets, where noise is added and the bucket clips, and expanded out
//            by the same gain: the noise breathes with the music and silence stays quiet. The head glides (120 ms);
//            a slow triangle (0.45 Hz) adds a gentle chorus. AGE raises the noise, opens the anti-alias filter, adds
//            sample-and-hold aliasing at the clock rate (above 50 %) and clock whine (above 60 %).
//   Pitch    a two-head pitch shifter (50 ms sin^2 windows, anti-aliased) on the read: Once = every repeat shifted by
//            PITCH + FINE; Climb = the shifted echo is what feeds back, so each repeat steps further (+12 = shimmer
//            stairs, -12 = falling octaves). The heads read earlier by the shifter's delay. Times crossfade as in
//            Digital, and AGE is the same converter grit.
//   Reverse  the loop is a forward delay; the echo is read backwards in windows as long as the delay time by two
//            readers half a window apart, each faded by sin^2 (the two always sum to 1): every repeat plays
//            backwards, without clicks. Times crossfade, AGE as in Digital.
// MOD: a sine (a triangle in BBD) on the read heads, up to 4 ms deep and never more than +-3.5 % in pitch; the right
// side runs 90 degrees later. DUCK: the echoes dip by up to 30 dB while the input plays (the gain reduction recovers
// with a 250 ms time constant). FREEZE: the input fades out of the loop and the loop becomes a plain copy of what it
// reads (no filters, saturation or pitch), so it holds forever.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "EchoParams.hpp"

namespace pump { namespace echo {

struct Settings {
    int mode = kModeDigital;
    double timeL = 375.0, timeR = 500.0;    // ms (free running)
    bool sync = true;
    int noteL = kNote8D, noteR = kNote4;
    bool link = false;
    double feedback = 35.0;                 // %
    int route = kRouteStereo;
    double width = 100.0, modDepth = 0.0, modRate = 0.8, age = 0.0, drive = 0.0;
    double lowCut = kLowCutOff, highCut = kHighCutOff, diffuse = 0.0, duck = 0.0;
    bool freeze = false;
    double pitch = 12.0, fine = 0.0;
    int pitchMode = kShiftOnce;
    double mix = 30.0, outDb = 0.0;
};

// The delay of one side in seconds (SYNC = a note division at the tempo). Shared by the engine and the UI.
inline double delaySeconds(bool sync, int note, double ms, double bpm)
{
    return clampv(sync ? noteSeconds(note, bpm) : 0.001 * ms, 0.001, kMaxSeconds);
}
inline void delaySeconds(const Settings& s, double bpm, double out[2])
{
    out[0] = delaySeconds(s.sync, s.noteL, s.timeL, bpm);
    out[1] = s.link ? out[0] : delaySeconds(s.sync, s.noteR, s.timeR, bpm);
}

// The routing matrices: in[c] = sum G[c][k] x[k]; loop[c] = sum M[c][k] echo[k].
struct Routing { double G[2][2], M[2][2]; };
inline Routing routing(int route)
{
    switch (route)
    {
    case kRoutePingPong: return { { { 0.5, 0.5 }, { 0.0, 0.0 } }, { { 0.0, 1.0 }, { 1.0, 0.0 } } };
    case kRouteWide: return { { { 0.5, 0.5 }, { 0.5, 0.5 } }, { { 0.7, 0.3 }, { 0.3, 0.7 } } };
    default: return { { { 1.0, 0.0 }, { 0.0, 1.0 } }, { { 1.0, 0.0 }, { 0.0, 1.0 } } };
    }
}

// Record-stage curves, all with a small-signal gain of 1 (callers drive in by g and divide by g after).
inline float clipClean(float x)   // linear to 0.8, then a tanh knee up to 1.0
{
    const float a = std::fabs(x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * fastTanh((a - 0.8f) * 5.0f);
    return x < 0.0f ? -y : y;
}
inline float clipBucket(float x) { return 0.7f * fastTanh(x * (1.0f / 0.7f)); }

constexpr double kHeadXfSeconds = 0.050;     // Digital / Pitch / Reverse: read-head crossfade on a time change
constexpr double kModeXfSeconds = 0.040;
constexpr double kShiftWindow = 0.050;       // pitch shifter window
constexpr int kAp = 4;
constexpr double kApMs[2][kAp] = { { 4.1, 6.3, 9.7, 14.3 }, { 4.6, 6.9, 10.4, 13.1 } };
constexpr double kBbdRef = 0.25;             // compander reference level
constexpr double kBbdBandwidth = 1740.0;     // Hz x s: the bucket-brigade's bandwidth at a delay of T s is this / T
constexpr double kBbdClockPerBand = 1.0 / 0.425;   // clock = bandwidth / 0.425 (8192 stages, 2 phases)

// What a mode does on the write side (the record stage) and the read side (the head).
enum Kind : int { kClean = 0, kTapeKind, kBbdKind };
inline int modeKind(int mode) { return mode == kModeTape ? kTapeKind : (mode == kModeBbd ? kBbdKind : kClean); }

class Engine {
public:
    static constexpr int kChunk = 32;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        maxD = (int)std::ceil(kMaxSeconds * sr) + 64;
        for (int c = 0; c < 2; ++c)
        {
            Chan& ch = chans[c];
            ch.line.setMax(maxD + 8);
            apTotal[c] = 0;
            for (int k = 0; k < kAp; ++k)
            {
                ch.apLen[k] = std::max(2, (int)std::lround(kApMs[c][k] * 0.001 * sr));
                ch.ap[k].setMax(ch.apLen[k] + 4);
                apTotal[c] += ch.apLen[k];
            }
            ch.shifter.prepare(sr, kShiftWindow);
            ch.rng.seed(0x2545F491u + 0x9E3779B9u * (uint32_t)c);
            ch.dc.set(5.0, sr);
            ch.lfo.reset(c ? 0.25 : 0.0);
            ch.bbdLfo.reset(c ? 0.5 : 0.0);
            ch.bbdLfo.setRate(0.45, sr);
            ch.hissHp.set(1000.0, sr);
        }
        wow.setup(sr, 0x51A7u);
        wow.setRate(0.85);
        flutter[0].setRate(7.1, sr); flutter[1].setRate(9.4, sr);
        flutter[0].reset(0.0); flutter[1].reset(0.37);
        xfStep = 1.0 / (kHeadXfSeconds * sr);
        modeStep = 1.0 / (kModeXfSeconds * sr);
        routeStep = 1.0 / (0.040 * sr);
        envRel = 1.0 - std::exp(-1.0 / (0.030 * sr));
        levelRel = 1.0 - std::exp(-1.0 / (0.040 * sr));
        const int ramp = std::max(1, (int)(0.020 * sr));
        for (LinearSmoother* s : { &fbS, &mixS, &outS, &widthS, &driveS, &depthS })
            s->setRamp(ramp);
        for (LinearSmoother* s : { &freezeS, &climbS, &diffS, &lcOn, &hcOn })
            s->setRamp(std::max(1, (int)(0.030 * sr)));

        // start settled at the current settings
        curMode = fromMode = wantMode;
        modeT = 1.0;
        const Routing r = routing(wantRoute);
        rFrom = rTo = rNow = r; routeT = 1.0; curRoute = wantRoute;
        fbS.reset(fbTarget()); mixS.reset(clampv(set.mix, 0.0, 100.0) * 0.01); outS.reset(dbToGain(set.outDb));
        widthS.reset(clampv(set.width, 0.0, 100.0) * 0.01); driveS.reset(driveGain());
        depthS.reset(depthTarget());
        freezeS.reset(set.freeze ? 1.0 : 0.0); climbS.reset(set.pitchMode == kShiftClimb ? 1.0 : 0.0);
        diffS.reset(diffOn() ? 1.0 : 0.0);
        lcOn.reset(lowCutActive(set.lowCut) ? 1.0 : 0.0); hcOn.reset(highCutActive(set.highCut) ? 1.0 : 0.0);
        lcHz = clampv(set.lowCut, (double)kLowCutOff, 2000.0); hcHz = clampv(set.highCut, 1000.0, (double)kHighCutOff);
        ageNow = clampv(set.age, 0.0, 100.0) * 0.01;
        apG = 0.72 * clampv(set.diffuse, 0.0, 100.0) * 0.01;
        ratio = ratioTarget();
        grDb = 0.0; duckGain = 1.0; level = 0.0;
        clearState();
        computeTargets();
        for (auto& ch : chans) { ch.heads.a = ch.heads.b = (int)std::lround(ch.tgt); ch.heads.t = 1.0; ch.glide = ch.tgt; }
        updateControls();
    }

    // ---- settings and tempo (audio thread, block start) ----
    void setSettings(const Settings& s)
    {
        set = s;
        wantMode = clampv(s.mode, 0, kNumModes - 1);
        wantRoute = clampv(s.route, 0, kNumRoutes - 1);
    }
    // the host tempo (120 when the host sends none); synced times follow it, also when the transport is stopped
    void setTempo(double b) { if (std::isfinite(b) && b > 1.0) bpm = clampv(b, 20.0, 999.0); }

    int latency() const { return 0; }

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk) processChunk(L + off, R + off, std::min(kChunk, n - off));
    }

    // ---- feeds (UI) ----
    float timeMs(int c) const { return timeFeed[c].load(std::memory_order_relaxed); }      // where the echo is now
    float tempo() const { return tempoFeed.load(std::memory_order_relaxed); }
    float wobbleMs() const { return wobbleFeed.load(std::memory_order_relaxed); }          // tape wow + flutter now
    float duckDb() const { return duckFeed.load(std::memory_order_relaxed); }              // the echoes' dip now
    MeterValue inPeak[2], wetPeak[2];   // input and echo (after duck and width) peaks

private:
    struct Heads {
        int a = 2, b = 2;
        double t = 1.0;   // crossfade progress a -> b (1 = idle on a)
        void follow(int target) { if (t >= 1.0 && target != a) { b = target; t = 0.0; } }
    };
    struct Chan {
        fx::FracDelay line;
        Heads heads;
        double tgt = 2.0, glide = 2.0, revWin = 1024.0;
        fx::Svf lc, hc;
        fx::DcBlocker dc;
        fx::Allpass ap[kAp];
        int apLen[kAp] = {};
        fx::Crusher crush;
        fx::Svf tapeHf, tapeBump, tapeRoll;
        fx::OnePole hissHp;
        fx::Svf bbdAa, bbdRec;
        fx::Crusher bbdZoh;
        double bbdEnv = 0.0, bbdOutEnv = 0.0, whinePh = 0.0, whineInc = 0.0;
        fx::Lfo lfo, bbdLfo;
        fx::PitchShifter shifter;
        fx::Svf pitchAa;
        double revPh = 0.0, revPbPrev = 0.5, revTa = 1024.0, revTb = 1024.0;
        fx::Rng rng;
    };

    double fbTarget() const { return clampv(set.feedback, 0.0, 120.0) * 0.01; }
    bool diffOn() const { return set.diffuse > 0.05; }
    // DRIVE: the signal goes into the loop's curves this much hotter (and comes out divided by it): half the dB
    // value, so 24 dB of drive puts the loop's ceiling at -12 dBFS
    double driveGain() const { return dbToGain(0.5 * clampv(set.drive, 0.0, 24.0)); }
    double ratioTarget() const { return fx::semitonesToRatio(clampv(set.pitch, -24.0, 24.0) + 0.01 * clampv(set.fine, -100.0, 100.0)); }
    // MOD depth in samples: up to 4 ms, and never more than +-3.5 % pitch at the rate
    double depthTarget() const
    {
        const double f = clampv(set.modRate, 0.05, 10.0);
        return clampv(set.modDepth, 0.0, 100.0) * 0.01 * std::min(0.004, 0.035 / (kTwoPi * f)) * sr;
    }

    inline double tap(const Chan& ch, double d) const { return ch.line.read(clampv(d, 2.0, (double)maxD) - 1.0); }
    inline double headsRead(const Chan& ch, double mod) const
    {
        const Heads& h = ch.heads;
        if (h.t >= 1.0) return tap(ch, h.a + mod);
        const double s = std::sin(0.5 * kPi * h.t), gb = s * s;
        return tap(ch, h.a + mod) * (1.0 - gb) + tap(ch, h.b + mod) * gb;
    }
    // Reverse: two readers half a window apart; each plays its window backwards (the read delay grows at twice the
    // rate of time) under a sin^2 fade, and latches the window length when it starts a new one.
    inline double reverseRead(Chan& ch)
    {
        ch.revPh += 1.0 / ch.revWin;
        if (ch.revPh >= 1.0) { ch.revPh -= std::floor(ch.revPh); ch.revTa = ch.revWin; }
        double pb = ch.revPh + 0.5;
        if (pb >= 1.0) pb -= 1.0;
        if (pb < ch.revPbPrev) ch.revTb = ch.revWin;
        ch.revPbPrev = pb;
        const double s = std::sin(kPi * ch.revPh), ga = s * s;
        return ga * tap(ch, 2.0 + 2.0 * ch.revPh * ch.revTa) + (1.0 - ga) * tap(ch, 2.0 + 2.0 * pb * ch.revTb);
    }

    // One mode's read side: `raw` = the forward read (what freeze writes back), `out` = the echo heard, `loop` = what
    // feeds back.
    inline void readMode(int m, Chan& ch, double modSine, double modTri, double tapeMod, double bbdMod, double climb,
                         double& raw, double& out, double& loop)
    {
        switch (m)
        {
        case kModeTape:
        {
            const double y = tap(ch, ch.glide + modSine + tapeMod);
            raw = loop = y;
            out = ch.tapeBump.peak(ch.tapeRoll.highpass(y), bumpGain) + hissLevel * ch.hissHp.hp(ch.rng.bi());
            return;
        }
        case kModeBbd:
        {
            const double y = tap(ch, ch.glide + modTri + bbdMod);
            const double l = ch.bbdRec.lowpass(y);
            raw = y; loop = out = l;
            const double a = std::fabs(l);
            ch.bbdOutEnv += (a - ch.bbdOutEnv) * (a > ch.bbdOutEnv ? 1.0 : envRel);
            if (whineLevel > 0.0 && ch.whineInc > 0.0)
            {
                ch.whinePh += ch.whineInc;
                ch.whinePh -= std::floor(ch.whinePh);
                out += whineLevel * std::sqrt(ch.bbdOutEnv / kBbdRef) * std::sin(kTwoPi * ch.whinePh);
            }
            return;
        }
        case kModePitch:
        {
            // the heads read early by the shifter's delay; the unshifted repeat is read on time (T)
            const double sd = ch.shifter.averageDelay();
            const double y = headsRead(ch, modSine);
            const double yo = headsRead(ch, modSine + sd);
            const double sh = ch.shifter.process((float)ch.pitchAa.lowpass(y));
            raw = yo; out = sh;
            loop = yo + (sh - yo) * climb;
            return;
        }
        default:
        {
            const double y = headsRead(ch, modSine);
            raw = loop = y;
            out = m == kModeReverse ? reverseRead(ch) : y;
            return;
        }
        }
    }

    // One record stage (by kind): small-signal gain 1, ceiling ~1/g.
    inline double recordStage(int kind, Chan& ch, double v, double g)
    {
        switch (kind)
        {
        case kTapeKind:
            return ch.tapeHf.lowpass(satCurve(SatMode::Tape, (float)(v * g)) / g);
        case kBbdKind:
        {
            const double a = ch.bbdAa.lowpass(v);
            const double r = std::fabs(a);
            ch.bbdEnv = r > ch.bbdEnv ? r : ch.bbdEnv + (r - ch.bbdEnv) * envRel;
            const double cg = std::sqrt(kBbdRef / std::max(ch.bbdEnv, 1e-5));   // compress: level -> sqrt(level * ref)
            double c = a * cg + bbdNoise * ch.rng.bi();
            c = clipBucket((float)(c * g)) / g;
            if (zohMix > 0.0) c += (ch.bbdZoh.process(c) - c) * zohMix;
            return clipClean((float)(c / cg * g)) / g;                            // expand by the same gain (the
                                                                                  // expander has headroom too)
        }
        default:
        {
            const double s = clipClean((float)(v * g)) / g;
            return crushOn ? ch.crush.process(s) : s;
        }
        }
    }

    // a mode's read and record states start fresh when it fades in (unless the outgoing mode shares them)
    void activate(int m, int from)
    {
        for (auto& ch : chans)
        {
            switch (m)
            {
            case kModeTape: ch.tapeHf.reset(); ch.tapeBump.reset(); ch.tapeRoll.reset(); ch.hissHp.reset(); break;
            case kModeBbd: ch.bbdAa.reset(); ch.bbdRec.reset(); ch.bbdZoh.reset(); ch.bbdEnv = ch.bbdOutEnv = 0.0; break;
            case kModePitch: ch.shifter.reset(); ch.pitchAa.reset(); break;
            case kModeReverse: ch.revPh = 0.0; ch.revPbPrev = 0.5; ch.revTa = ch.revTb = ch.revWin; break;
            default: break;
            }
            if (modeKind(m) == kClean && modeKind(from) != kClean) ch.crush.reset();
        }
    }

    void clearState()
    {
        for (auto& ch : chans)
        {
            ch.line.clear();
            ch.lc.reset(); ch.hc.reset(); ch.dc.reset();
            for (auto& a : ch.ap) a.clear();
            ch.crush.reset();
            ch.tapeHf.reset(); ch.tapeBump.reset(); ch.tapeRoll.reset(); ch.hissHp.reset();
            ch.bbdAa.reset(); ch.bbdRec.reset(); ch.bbdZoh.reset(); ch.bbdEnv = ch.bbdOutEnv = 0.0;
            ch.shifter.reset(); ch.pitchAa.reset();
            ch.revPh = 0.0; ch.revPbPrev = 0.5;
        }
        level = 0.0; grDb = 0.0; duckGain = 1.0;
    }

    // read delays (samples) for the current times, moved earlier by the diffuser (when on) and the pitch shifter
    void computeTargets()
    {
        double secs[2];
        delaySeconds(set, bpm, secs);
        for (int c = 0; c < 2; ++c)
        {
            Chan& ch = chans[c];
            double comp = diffOn() ? (double)apTotal[c] : 0.0;
            if (curMode == kModePitch) comp += ch.shifter.averageDelay();
            ch.tgt = clampv(secs[c] * sr - comp, 2.0, (double)maxD);
            ch.revWin = clampv(ch.tgt, 16.0, 0.5 * (maxD - 8));
        }
    }

    // control rate (every chunk): targets, coefficients, smoothers
    void updateControls()
    {
        if (modeT >= 1.0 && wantMode != curMode)
        {
            fromMode = curMode; curMode = wantMode; modeT = 0.0;
            activate(curMode, fromMode);
        }
        if (wantRoute != curRoute)
        {
            rFrom = rNow; rTo = routing(wantRoute); routeT = 0.0; curRoute = wantRoute;
        }
        computeTargets();
        for (auto& ch : chans) ch.heads.follow((int)std::lround(ch.tgt));
        const double tau = curMode == kModeTape ? 0.25 : 0.12;
        glideK = 1.0 - std::exp(-1.0 / (tau * sr));

        fbS.setTarget(fbTarget());
        mixS.setTarget(clampv(set.mix, 0.0, 100.0) * 0.01);
        outS.setTarget(dbToGain(clampv(set.outDb, -24.0, 24.0)));
        widthS.setTarget(clampv(set.width, 0.0, 100.0) * 0.01);
        driveS.setTarget(driveGain());
        depthS.setTarget(depthTarget());
        freezeS.setTarget(set.freeze ? 1.0 : 0.0);
        climbS.setTarget(set.pitchMode == kShiftClimb ? 1.0 : 0.0);
        lcOn.setTarget(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        hcOn.setTarget(highCutActive(set.highCut) ? 1.0 : 0.0);
        if (diffOn() && !diffS.isSmoothing() && diffS.current() <= 0.0)
            for (auto& ch : chans) for (auto& a : ch.ap) a.clear();   // re-engaging from off: no stale smear
        diffS.setTarget(diffOn() ? 1.0 : 0.0);

        // control-rate glides (per chunk, ~30-50 ms)
        const double k = 1.0 - std::exp(-(double)kChunk / (0.04 * sr));
        lcHz *= std::pow(clampv(set.lowCut, (double)kLowCutOff, 2000.0) / lcHz, k);
        hcHz *= std::pow(clampv(set.highCut, 1000.0, (double)kHighCutOff) / hcHz, k);
        ageNow += (clampv(set.age, 0.0, 100.0) * 0.01 - ageNow) * k;
        apG += (0.72 * clampv(set.diffuse, 0.0, 100.0) * 0.01 - apG) * k;
        ratio *= std::pow(ratioTarget() / ratio, k);

        // coefficients of the modes that are sounding
        const double age = ageNow;
        auto on = [&](int m) { return curMode == m || fromMode == m; };
        const bool clean = modeKind(curMode) == kClean || modeKind(fromMode) == kClean;
        crushOn = age > 0.001;
        for (int c = 0; c < 2; ++c)
        {
            Chan& ch = chans[c];
            ch.lc.set(std::max((double)kLowCutOff, lcHz), 0.70710678, sr);
            ch.hc.set(std::min(hcHz, 0.45 * sr), 0.70710678, sr);
            ch.lfo.setRate(clampv(set.modRate, 0.05, 10.0), sr);
            // the clean modes: an early converter's grit
            if (clean && crushOn) ch.crush.set(std::exp(std::log(6000.0 / sr) * age), 24.0 - 14.0 * age);
            if (on(kModeTape))
            {
                ch.tapeHf.set(std::min(0.45 * sr, 15000.0 * std::pow(0.25, age)), 0.6, sr);
                ch.tapeBump.set(95.0, 1.0, sr);
                ch.tapeRoll.set(30.0, 0.70710678, sr);
            }
            if (on(kModeBbd))
            {
                // the clock follows where the head is now
                const double T = (ch.glide + (diffOn() ? apTotal[c] : 0)) / sr;
                const double band = clampv(kBbdBandwidth / std::max(T, 1e-3), 800.0, 14000.0);
                const double clock = band * kBbdClockPerBand;
                ch.bbdRec.set(std::min(band, 0.45 * sr), 0.70710678, sr);
                ch.bbdAa.set(std::min(band * (1.0 + 1.5 * age), 0.45 * sr), 0.70710678, sr);
                ch.bbdZoh.set(clock / sr, 0.0);
                ch.whineInc = clock < 0.42 * sr ? clock / sr : 0.0;
            }
            if (on(kModePitch))
            {
                // anti-alias ahead of an upward shift
                ch.shifter.setRatio(ratio);
                ch.pitchAa.set(std::min(0.45 * sr, 0.45 * sr / std::max(1.0, ratio)), 0.70710678, sr);
            }
        }
        bumpGain = dbToGain(1.5 + 2.0 * age);
        hissLevel = dbToGain(-90.0 + 26.0 * age);
        wowDepth = (0.10 + 1.3 * age) * 0.001 * sr;
        flutterDepth = (0.012 + 0.05 * age) * 0.001 * sr;
        bbdDepth = (0.8 + 0.6 * age) * 0.001 * sr;
        bbdNoise = 0.0003 + 0.004 * age;
        zohMix = 0.6 * clampv((age - 0.5) * 2.0, 0.0, 1.0);
        whineLevel = 0.004 * clampv((age - 0.6) / 0.4, 0.0, 1.0);

        // duck: the gain reduction for the input level (10 ms in, 250 ms back)
        const double lvDb = gainToDb(std::max(level, 1e-9));
        const double want = -clampv(set.duck, 0.0, 100.0) * 0.30 * clampv((lvDb + 50.0) / 30.0, 0.0, 1.0);
        const double tc = want < grDb ? 0.010 : 0.250;
        grDb += (want - grDb) * (1.0 - std::exp(-(double)kChunk / (tc * sr)));
    }

    void processChunk(float* L, float* R, int n)
    {
        updateControls();
        const double duckFrom = duckGain, duckTo = dbToGain(grDb);
        const bool tapeOn = curMode == kModeTape || fromMode == kModeTape;
        const bool bbdOn = curMode == kModeBbd || fromMode == kModeBbd;
        const bool diffRun = diffOn() || diffS.current() > 0.0 || diffS.isSmoothing();
        const int kCur = modeKind(curMode), kFrom = modeKind(fromMode);
        double chk = 0.0, pin[2] = { 0, 0 }, pw[2] = { 0, 0 }, wobble = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double fb = fbS.next(), fz = freezeS.next(), g = driveS.next(), climb = climbS.next();
            const double eng = diffS.next(), lcm = lcOn.next(), hcm = hcOn.next(), wd = widthS.next();
            const double mx = mixS.next(), og = outS.next(), depth = depthS.next();
            double mt = 1.0;
            if (modeT < 1.0)
            {
                modeT = std::min(1.0, modeT + modeStep);
                const double s = std::sin(0.5 * kPi * modeT);
                mt = s * s;
                if (modeT >= 1.0) fromMode = curMode;
            }
            if (routeT < 1.0)
            {
                routeT = std::min(1.0, routeT + routeStep);
                for (int a = 0; a < 2; ++a)
                    for (int b = 0; b < 2; ++b)
                    {
                        rNow.G[a][b] = rFrom.G[a][b] + (rTo.G[a][b] - rFrom.G[a][b]) * routeT;
                        rNow.M[a][b] = rFrom.M[a][b] + (rTo.M[a][b] - rFrom.M[a][b]) * routeT;
                    }
            }
            const double x[2] = { L[i], R[i] };
            const double ax = std::max(std::fabs(x[0]), std::fabs(x[1]));
            level = ax > level ? ax : level + (ax - level) * levelRel;

            // modulation
            double tapeMod = 0.0;
            if (tapeOn)
            {
                flutter[0].advance(); flutter[1].advance();
                tapeMod = wowDepth * wow.next() + flutterDepth * (0.7 * flutter[0].sine() + 0.3 * flutter[1].sine());
                wobble = tapeMod;
            }
            // ---- read
            double raw[2], out[2], loop[2];
            for (int c = 0; c < 2; ++c)
            {
                Chan& ch = chans[c];
                ch.lfo.advance();
                double ms = 0.0, mtr = 0.0, bm = 0.0;
                if (depth > 0.0) { ms = depth * ch.lfo.sine(); mtr = depth * ch.lfo.tri(); }
                if (bbdOn) { ch.bbdLfo.advance(); bm = bbdDepth * ch.bbdLfo.tri(); }
                ch.glide += clampv((ch.tgt - ch.glide) * glideK, -0.5, 0.5);
                readMode(curMode, ch, ms, mtr, tapeMod, bm, climb, raw[c], out[c], loop[c]);
                if (mt < 1.0)
                {
                    double r0, o0, l0;
                    readMode(fromMode, ch, ms, mtr, tapeMod, bm, climb, r0, o0, l0);
                    raw[c] = r0 + (raw[c] - r0) * mt;
                    out[c] = o0 + (out[c] - o0) * mt;
                    loop[c] = l0 + (loop[c] - l0) * mt;
                }
                if (ch.heads.t < 1.0)
                {
                    ch.heads.t += xfStep;
                    if (ch.heads.t >= 1.0) { ch.heads.a = ch.heads.b; ch.heads.t = 1.0; }
                }
            }
            // ---- write
            const double feed = 1.0 - fz;
            for (int c = 0; c < 2; ++c)
            {
                Chan& ch = chans[c];
                const double in = (rNow.G[c][0] * x[0] + rNow.G[c][1] * x[1]) * feed;
                double v = in + fb * (rNow.M[c][0] * loop[0] + rNow.M[c][1] * loop[1]);
                { const double h = ch.lc.highpass(v); v += (h - v) * lcm; }
                { const double l = ch.hc.lowpass(v); v += (l - v) * hcm; }
                if (diffRun)
                {
                    float d = (float)v;
                    for (int k = 0; k < kAp; ++k) d = ch.ap[k].processInt(d, ch.apLen[k], (float)apG);
                    v += ((double)d - v) * eng;
                }
                double w = recordStage(kCur, ch, v, g);
                if (mt < 1.0 && kFrom != kCur) { const double w0 = recordStage(kFrom, ch, v, g); w = w0 + (w - w0) * mt; }
                w = ch.dc.process(w);
                w += (raw[c] - w) * fz;
                ch.line.push((float)w);
                chk += w;
            }
            // ---- out
            const double dg = duckFrom + (duckTo - duckFrom) * (i + 1) / n;
            const double e0 = out[0] * dg, e1 = out[1] * dg;
            const double m = 0.5 * (e0 + e1), s = 0.5 * (e0 - e1) * wd;
            const double wet[2] = { m + s, m - s };
            const double dryG = std::min(1.0, 2.0 * (1.0 - mx)), wetG = std::min(1.0, 2.0 * mx);
            L[i] = (float)((x[0] * dryG + wet[0] * wetG) * og);
            R[i] = (float)((x[1] * dryG + wet[1] * wetG) * og);
            chk += wet[0] + wet[1];
            for (int c = 0; c < 2; ++c) { pin[c] = std::max(pin[c], std::fabs(x[c])); pw[c] = std::max(pw[c], std::fabs(wet[c])); }
        }
        duckGain = duckTo;
        chk += level + grDb;
        for (const auto& ch : chans) chk += ch.glide + ch.bbdEnv;
        if (!std::isfinite(chk))
        {
            clearState();
            for (auto& ch : chans) { ch.glide = ch.tgt; ch.heads.a = ch.heads.b = (int)std::lround(ch.tgt); ch.heads.t = 1.0; }
            sanitizeBlock(L, n); sanitizeBlock(R, n);
            return;
        }
        for (int c = 0; c < 2; ++c)
        {
            inPeak[c].setMax((float)pin[c]);
            wetPeak[c].setMax((float)pw[c]);
            const Chan& ch = chans[c];
            const bool glides = modeKind(curMode) != kClean;
            const double head = glides ? ch.glide : (ch.heads.t < 1.0 ? ch.heads.b : ch.heads.a);
            double comp = diffOn() ? (double)apTotal[c] : 0.0;
            if (curMode == kModePitch) comp += ch.shifter.averageDelay();
            timeFeed[c].store((float)(1000.0 * (head + comp) / sr), std::memory_order_relaxed);
        }
        tempoFeed.store((float)bpm, std::memory_order_relaxed);
        wobbleFeed.store((float)(1000.0 * wobble / sr), std::memory_order_relaxed);
        duckFeed.store((float)grDb, std::memory_order_relaxed);
    }

    double sr = 48000.0, bpm = 120.0;
    int maxD = 1 << 16;
    Settings set;
    Chan chans[2];
    int apTotal[2] = { 0, 0 };
    int wantMode = kModeDigital, curMode = kModeDigital, fromMode = kModeDigital;
    int wantRoute = kRouteStereo, curRoute = kRouteStereo;
    double modeT = 1.0, modeStep = 0.001, routeT = 1.0, routeStep = 0.001, xfStep = 0.001, glideK = 0.001;
    Routing rFrom = routing(kRouteStereo), rTo = routing(kRouteStereo), rNow = routing(kRouteStereo);
    fx::Wander wow;
    fx::Lfo flutter[2];
    double lcHz = kLowCutOff, hcHz = kHighCutOff, ageNow = 0.0, apG = 0.0, ratio = 2.0;
    bool crushOn = false;
    double bumpGain = 1.0, hissLevel = 0.0, wowDepth = 0.0, flutterDepth = 0.0, bbdDepth = 0.0;
    double bbdNoise = 0.0, zohMix = 0.0, whineLevel = 0.0;
    double envRel = 0.001, levelRel = 0.001;
    double level = 0.0, grDb = 0.0, duckGain = 1.0;
    LinearSmoother fbS, mixS, outS, widthS, driveS, depthS, freezeS, climbS, diffS, lcOn, hcOn;
    std::atomic<float> timeFeed[2] { { 375.0f }, { 500.0f } }, tempoFeed { 120.0f }, wobbleFeed { 0.0f }, duckFeed { 0.0f };
};

}} // namespace pump::echo
