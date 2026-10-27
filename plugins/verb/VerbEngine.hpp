// pump VERB engine: algorithmic reverb. Framework-free.
//
// Signal path (stereo):
//   dry ------------------------------------------------------------------------------------------------+-> mix -> out
//   in -> duck detector                                                                                   |
//   in -> pre-delay (crossfading heads) -> colour in -> early reflections ---------------+                |
//                                                   \-> input diffusion (4 allpasses) -> late tank -> colour out -> width
//                                                                                        -> low/high cut -> duck ---+
// Pre-delay: 0..500 ms, or a note value at the host tempo (up to 1 s). A new time crossfades to a new read head.
// Early reflections: first- and second-order images of a shoebox room per mode (source and two receivers, a little
//   scattering), 24 taps per side, times scaled by SIZE, energy-normalised, softened by a one-pole (wall absorption).
// Late tanks:
//   FDN    8 or 16 delay lines (lengths: primes spread geometrically between the mode's shortest and longest, scaled by
//          SIZE), mixed by an orthonormal Hadamard matrix (Householder for the rack: each line keeps more of itself,
//          so echoes stay distinct and ring a little). Each line: cubic-interpolated modulated read -> a short
//          density allpass (the studio and 80s voices) -> three-band absorption. The absorption (two first-order
//          shelves at CROSSOVER and DAMP FREQ) is designed per line from its own loop length so that every line
//          decays 60 dB in exactly RT60 low = DECAY x BASS MULT, mid = DECAY, high = DECAY x HIGH DAMP: the whole
//          network then decays at those rates. Output taps sit at different points inside the lines, with orthogonal
//          sign patterns for L and R (decorrelated); one tap per side reads the line input, so the late field starts
//          right at the pre-delay and builds up from there.
//   Plate  a figure-eight tank (two cross-coupled branches: modulated allpass -> delay -> absorption -> allpass ->
//          delay), seven output taps per side; the vintage and classic plates use it.
//   Burst  Gated and Reverse: 128 taps per side (stratified random times over the gate / reverse TIME, random signs)
//          read a lightly diffused copy of the input, then two short allpasses. Gated: dense right away, sloping by
//          DECAY, cut at TIME; Reverse: rising toward TIME (DECAY = how slowly it rises), then cut.
// Modulation: per line, smooth random wander (+ a cyclic "spin" for the 80s voice and the rack's chorus), computed at
//   control rate (32 samples) and interpolated. Depth per voice: barely there for the studio voices (it only breaks
//   up modal ringing), random chorus for the 70s, slow wide wander and spin for the 80s.
// Colour (scaled by AGE): band limiting (4-pole Butterworth in and out), quantisation and a low internal rate:
//   late-70s   ~11..7.5 kHz, 30..20 kHz hold, 14..9-bit gain-ranging converters (the grain and its dithered noise
//              float with the level), 14..8-bit mantissa truncation inside the loop
//   80s        ~18..12 kHz, 40..30 kHz hold, 18..12-bit gain-ranging, 18..11-bit mantissa in the loop
//   90s rack   ~15..10 kHz, 44..24 kHz hold, 16..11-bit fixed-point converters (no dither), a 20..15-bit
//              fixed-point loop truncated toward zero, so tails end in grit
//   Truncation toward zero only ever removes energy, so the quantised loops stay stable.
// Shimmer: two lines of the FDN pass through pitch shifters (correlation-aligned splices) inside the loop, crossfaded by
//   AMOUNT; each pass shifts again, so the tail climbs (or falls) in steps of PITCH. Shifted samples are convex
//   combinations of past samples, so the loop gain never exceeds the unshifted one.
// FREEZE: the input to the tanks fades out and the absorption fades to unity (an endless, lossless tail).
// DUCK: the wet signal dips (up to 30 dB) while the dry input is loud (2 ms attack, 250 ms release).
// Level: the late tank is normalised for SIZE and half-normalised for DECAY (energy grows with RT60; the gain
//   compensates half of it in dB), so long decays sound bigger without jumping in level.
// Changes: every control glides. SIZE glides (the tail bends in pitch slightly while it moves); a mode change or a big
//   SIZE jump (a preset) fades the wet signal out (12 ms), switches, clears the tank and fades back in. The dry path
//   has no latency, so the mix lines up sample-exactly and mix 0 is the input, exactly.
#pragma once

#include "VerbDsp.hpp"
#include "VerbParams.hpp"

namespace pump { namespace verb {

struct Settings {
    int mode = kHall;
    double predelayMs = 20.0;
    bool sync = false;
    int note = kNote32;
    double size = 50.0, decay = 2.2, diffusion = 80.0, balance = 0.0;
    double bassMult = 1.2, crossover = 600.0, highDamp = 0.55, dampFreq = 6000.0;
    double lowCut = kLowCutOff, highCut = kHighCutOff;
    double modDepth = 40.0, modRate = 0.7, width = 100.0, age = 35.0, envTimeMs = 320.0;
    int pitch = 12;
    double shimmer = 50.0, duck = 0.0;
    bool freeze = false;
    double mix = 30.0, outDb = 0.0;
};

// ---- the voices -----------------------------------------------------------------------------------------------------
enum Topo : int { kTopoFdn = 0, kTopoPlate, kTopoBurst };
enum Colour : int { kColourClean = 0, kColour70s, kColour80s, kColour90s };

struct Voice {
    int topo = kTopoFdn, lines = 16;
    bool householder = false;
    double lenMin = 38.0, lenMax = 135.0;   // FDN line lengths at SIZE scale 1 (ms)
    double apMs = 0.0, apG = 0.0;           // density allpass in each line (0 = none), coefficient at diffusion 100 %
    double diffMs = 11.0, diffG = 0.72;     // input diffusers: the longest of the four (ms), coefficient at 100 %
    double room[3] = { 36.0, 24.0, 14.0 };  // shoebox (m) at SIZE scale 1
    double refl = 0.75, erLevel = 0.5, erLp = 7000.0;
    int colour = kColourClean;
    double wanderMs = 0.2, wanderMul = 1.0, spinMs = 0.0, spinMul = 1.0;   // modulation at 100 % depth
    double trim = 1.0;                      // late level
};
inline Voice voice(int mode)
{
    Voice v;
    auto room = [&](double x, double y, double z, double refl, double level, double lp) { v.room[0] = x; v.room[1] = y; v.room[2] = z; v.refl = refl; v.erLevel = level; v.erLp = lp; };
    switch (mode)
    {
    case kRoom:
        v.lenMin = 14; v.lenMax = 52; v.apMs = 3.5; v.apG = 0.5; v.diffMs = 6; v.diffG = 0.7; v.wanderMs = 0.12;
        room(9.0, 7.0, 3.2, 0.82, 0.85, 9000); v.trim = 0.62; break;
    case kChamber:
        v.lenMin = 24; v.lenMax = 80; v.apMs = 5; v.apG = 0.6; v.diffMs = 7; v.diffG = 0.75; v.wanderMs = 0.15;
        room(7.0, 5.0, 4.0, 0.9, 0.8, 11000); v.trim = 0.8; break;
    case kPlate:
        v.lenMin = 12; v.lenMax = 48; v.apMs = 4; v.apG = 0.65; v.diffMs = 5; v.diffG = 0.78; v.wanderMs = 0.15;
        room(2.2, 1.4, 0.6, 0.92, 0.3, 14000); v.trim = 0.73; break;
    case kVintageHall:
        v.lines = 8; v.lenMin = 40; v.lenMax = 120; v.diffMs = 9; v.diffG = 0.68; v.colour = kColour70s;
        v.wanderMs = 1.1; v.wanderMul = 1.6; room(30.0, 20.0, 12.0, 0.7, 0.35, 6000); v.trim = 0.77; break;
    case kVintagePlate:
        v.topo = kTopoPlate; v.diffMs = 12.7; v.diffG = 0.75; v.colour = kColour70s; v.wanderMs = 0.8; v.wanderMul = 1.6;
        room(2.0, 1.3, 0.6, 0.9, 0.15, 9000); v.trim = 1.36; break;
    case kClassicHall:
        v.lenMin = 45; v.lenMax = 150; v.apMs = 8; v.apG = 0.55; v.diffMs = 12; v.diffG = 0.72; v.colour = kColour80s;
        v.wanderMs = 1.6; v.wanderMul = 0.45; v.spinMs = 0.5; v.spinMul = 1.0; room(40.0, 26.0, 15.0, 0.78, 0.4, 7000); v.trim = 1.15; break;
    case kClassicPlate:
        v.topo = kTopoPlate; v.diffMs = 12.7; v.diffG = 0.75; v.colour = kColour80s; v.wanderMs = 1.0; v.wanderMul = 0.5;
        v.spinMs = 0.4; v.spinMul = 1.0; room(2.0, 1.3, 0.6, 0.9, 0.15, 11000); v.trim = 1.35; break;
    case kRack:
        v.lines = 8; v.householder = true; v.lenMin = 28; v.lenMax = 70; v.diffMs = 4; v.diffG = 0.6; v.colour = kColour90s;
        v.wanderMs = 0.0; v.spinMs = 0.35; v.spinMul = 1.0; room(14.0, 10.0, 5.0, 0.7, 0.55, 9000); v.trim = 0.6; break;
    case kGated:
    case kReverse:
        v.topo = kTopoBurst; v.diffMs = 2.0; v.diffG = 0.5; v.wanderMs = 0.0; room(1, 1, 1, 0.5, 0.0, 10000); v.trim = 1.0; break;
    case kShimmerMode:
        v.lenMin = 40; v.lenMax = 140; v.apMs = 8; v.apG = 0.55; v.diffMs = 11; v.diffG = 0.72; v.wanderMs = 0.3;
        room(36.0, 24.0, 14.0, 0.75, 0.25, 7000); v.trim = 1.2; break;
    case kHall:
    default:
        v.apMs = 7; v.apG = 0.5; room(36.0, 24.0, 14.0, 0.75, 0.5, 7000); break;
    }
    return v;
}
inline uint32_t erSeed(int mode) { return 0x51ED270Bu + 7919u * (uint32_t)mode; }

// SIZE (0..100 %) -> the scale of every delay length: 0.3 .. 1.6 (geometric; 50 % = 0.69)
inline double sizeScale(double pct) { return 0.3 * std::pow(1.6 / 0.3, clampv(pct, 0.0, 100.0) * 0.01); }
// the pre-delay in use (ms): the note value at the tempo when synced
inline double predelayFor(bool sync, int note, double ms, double bpm)
{
    return sync ? std::min(kMaxSyncedMs, 1000.0 * noteSeconds(clampv(note, 0, kMaxSyncNote), bpm)) : clampv(ms, 0.0, kMaxPredelayMs);
}

// RT60 at frequency f for the decay controls (the absorption's exact response for a representative line; the
// design makes the curve all but independent of the line length). For the DECAY display and the tests.
inline double rt60At(double f, double decay, double bass, double xover, double highDamp, double dampFreq, double sr)
{
    const double L = 0.05 * sr;
    Absorber a;
    a.design(L, decay * bass, decay, decay * highDamp, sr);
    const double m = absorberMag(f, xover, dampFreq, a.gM, a.rL, a.rH, sr);
    return -6.907755278982137 * L / (sr * std::log(clampv(m, 1e-9, 0.9999999)));
}

// The colour of a voice at an AGE (0..1)
struct ColourParams {
    double bw = 0.0, rate = 0.0;        // band limit (Hz), internal rate (Hz)
    double convBits = 0.0;              // converter word (0 = none)
    int ranges = 0;                     // gain-ranging steps (6 dB each; 0 = fixed point)
    bool dither = false;
    int loopBits = 0;                   // loop word (0 = float)
    bool loopFixed = false;
};
inline ColourParams colourFor(int colour, double age)
{
    ColourParams c;
    const double a = clampv(age, 0.0, 1.0);
    auto lerp = [&](double x0, double x1) { return x0 + (x1 - x0) * a; };
    switch (colour)
    {
    case kColour70s:
        c.bw = lerp(11000, 7500); c.rate = lerp(30000, 20000); c.convBits = lerp(14, 9); c.ranges = 8; c.dither = true;
        c.loopBits = (int)std::lround(lerp(14, 8)); break;
    case kColour80s:
        c.bw = lerp(18000, 12000); c.rate = lerp(40000, 30000); c.convBits = lerp(18, 12); c.ranges = 4; c.dither = true;
        c.loopBits = (int)std::lround(lerp(18, 11)); break;
    case kColour90s:
        c.bw = lerp(15000, 10000); c.rate = lerp(44000, 24000); c.convBits = lerp(16, 11); c.ranges = 0; c.dither = false;
        c.loopBits = (int)std::lround(lerp(20, 15)); c.loopFixed = true; break;
    default: break;
    }
    return c;
}

// The plate tank's segments (ms at SIZE scale 1) and output taps (segment, branch, position 0..1, sign)
struct PlateGeom { double ap1, dA, ap2, dB; };
inline const PlateGeom& plateGeom(int b)
{
    static const PlateGeom g[2] = { { 22.58, 149.62, 60.48, 125.0 }, { 30.51, 141.70, 89.24, 106.28 } };
    return g[b & 1];
}
struct PlateTap { int seg, branch; float pos, sign; };   // seg 0 = delay A, 1 = second allpass, 2 = delay B
static const PlateTap kPlateTaps[2][8] = {
    { { 0, 0, 0.0f, 1.0f }, { 0, 1, 0.063f, 1.0f }, { 0, 1, 0.705f, 1.0f }, { 1, 1, 0.720f, -1.0f }, { 2, 1, 0.631f, 1.0f },
      { 0, 0, 0.447f, -1.0f }, { 1, 0, 0.104f, -1.0f }, { 2, 0, 0.287f, -1.0f } },
    { { 0, 1, 0.0f, 1.0f }, { 0, 0, 0.079f, 1.0f }, { 0, 0, 0.815f, 1.0f }, { 1, 0, 0.682f, -1.0f }, { 2, 0, 0.719f, 1.0f },
      { 0, 1, 0.501f, -1.0f }, { 1, 1, 0.126f, -1.0f }, { 2, 1, 0.038f, -1.0f } },
};

// Burst envelope (Gated / Reverse) at u = 0..1 of TIME T (s); DECAY d (s) sets the slope
inline double burstEnvelope(int mode, double u, double T, double d)
{
    const double t = u * T;
    double e;
    if (mode == kReverse) e = std::exp(-6.907755 * (1.0 - u) * T / std::max(0.05, d));
    else e = std::exp(-6.907755 * t / std::max(0.05, d));
    const double taper = std::min(0.006, 0.15 * T), left = T - t;   // a short raised-cosine cut
    if (left < taper) e *= 0.5 - 0.5 * std::cos(kPi * std::max(0.0, left) / taper);
    return e;
}

class Engine {
public:
    static constexpr int kChunk = 32;          // control rate: modulation, absorption, gains
    static constexpr int kMaxLines = 16;
    static constexpr int kBurstTaps = 128;
    static constexpr double kMaxModMs = 3.0;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        const double ms = sr * 0.001, sMax = sizeScale(100.0);
        double maxLine = 0.0, maxAp = 0.0, maxDiff = 0.0, maxEr = 0.0;
        for (int m = 0; m < kNumModes; ++m)
        {
            const Voice v = voice(m);
            maxLine = std::max(maxLine, v.lenMax * 1.05);
            maxAp = std::max(maxAp, v.apMs * 1.45);
            maxDiff = std::max(maxDiff, v.diffMs);
            ImagePattern p;
            p.build(v.room[0], v.room[1], v.room[2], v.refl, erSeed(m));
            maxEr = std::max(maxEr, (double)p.span);
        }
        for (int k = 0; k < kMaxLines; ++k)
        {
            line[k].setMax((int)((maxLine * sMax + kMaxModMs) * ms) + 8);
            dens[k].setMax((int)(maxAp * ms) + 8);
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& a : diff[ch]) a.setMax((int)(maxDiff * ms) + 8);
            erLine[ch].setMax((int)(maxEr * sMax * sr) + 8);
            burst[ch].setMax((int)(1.6 * sr) + 8);
            for (auto& a : post[ch]) a.setMax((int)(2.0 * ms) + 8);
            shifter[ch].prepare(sr);
            shimLp[ch].set(std::min(9000.0, 0.2 * sr), sr);
        }
        for (int b = 0; b < 2; ++b)
        {
            const PlateGeom& g = plateGeom(b);
            plate[b].ap1.setMax((int)((g.ap1 * sMax + kMaxModMs) * ms) + 8);
            plate[b].dA.setMax((int)(g.dA * sMax * ms) + 8);
            plate[b].ap2.setMax((int)(g.ap2 * sMax * ms) + 8);
            plate[b].dB.setMax((int)(g.dB * sMax * ms) + 8);
        }
        pre.setMax((int)(kMaxSyncedMs * ms) + 8, std::max(1, (int)(0.020 * sr)));
        pre.reset((int)std::lround(predelayFor(set.sync, set.note, set.predelayMs, bpm) * ms));

        // fixed per-line constants (deterministic)
        fx::Rng rng(0xC0FFEEu);
        for (int k = 0; k < kMaxLines; ++k)
        {
            inSign[k] = rng.uni() < 0.5 ? -1.0f : 1.0f;
            outSign[0][k] = rng.uni() < 0.5 ? -1.0f : 1.0f;
            outSign[1][k] = outSign[0][k] * ((k & 1) ? -1.0f : 1.0f);
            tapPos[0][k] = k == 0 ? 0.0f : (float)(0.12 + 0.8 * rng.uni());
            tapPos[1][k] = k == 1 ? 0.0f : (float)(0.12 + 0.8 * rng.uni());
            wander[k].setup(sr / kChunk, 0x1234567u + 977u * (uint32_t)k);
            spin[k].reset(k / (double)kMaxLines);
            jitter[k] = rng.bi();
        }
        fx::Rng br(0xB0057u);
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < kBurstTaps; ++k)
            {
                burstU[ch][k] = k == 0 ? 0.0f : (float)((k + br.uni()) / kBurstTaps);   // the first tap starts the burst
                burstAmp[ch][k] = (float)((br.uni() < 0.5 ? -1.0 : 1.0) * (0.75 + 0.5 * br.uni()));
            }
        for (int ch = 0; ch < 2; ++ch) conv[ch][0].setup(sr), conv[ch][1].setup(sr);

        const int ramp = std::max(1, (int)(0.020 * sr));
        dryS.setRamp(ramp); dryS.reset(std::cos(0.5 * kPi * clampv(set.mix, 0.0, 100.0) * 0.01));
        wetS.setRamp(ramp); wetS.reset(std::sin(0.5 * kPi * clampv(set.mix, 0.0, 100.0) * 0.01));
        outS.setRamp(ramp); outS.reset(dbToGain(set.outDb));
        earlyS.setRamp(ramp); lateS.setRamp(ramp);
        double eg, lg; balanceGains(set.balance, eg, lg); earlyS.reset(eg); lateS.reset(lg);
        widthS.setRamp(ramp); widthS.reset(set.width * 0.01);
        lcOn.setRamp(ramp); lcOn.reset(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        hcOn.setRamp(ramp); hcOn.reset(highCutActive(set.highCut) ? 1.0 : 0.0);
        inGain.setRamp(std::max(1, (int)(0.04 * sr))); inGain.reset(freezeOn() ? 0.0 : 1.0);
        trans.setRamp(std::max(1, (int)(0.012 * sr))); trans.reset(1.0);
        duckAtt = 1.0 - tauCoef(0.002, sr);
        duckRel = 1.0 - tauCoef(0.25, sr);
        duckGlide = 1.0 - tauCoef(0.002, sr);
        kc50 = 1.0 - std::exp(-kChunk / (0.05 * sr));
        kc30 = 1.0 - std::exp(-kChunk / (0.03 * sr));
        kc200 = 1.0 - std::exp(-kChunk / (0.2 * sr));
        // control state jumps to the settings
        lnScale = lnScaleTarget = std::log(sizeScale(set.size));
        rtS = std::max(0.1, set.decay); bassS = set.bassMult; hdS = set.highDamp;
        xoverS = set.crossover; dampS = set.dampFreq; ageS = set.age * 0.01; depthS = set.modDepth * 0.01;
        rateS = set.modRate; shimS = shimmerAmount(); diffS = set.diffusion * 0.01; lcS = set.lowCut; hcS = set.highCut; fz = freezeOn() ? 1.0 : 0.0; envTS = set.envTimeMs * 0.001;
        duckEnv = 0.0; duckG = duckTarget = 1.0; wetE = 0.0; wetN = 0; ctrlLeft = 0;
        curMode = set.mode;
        pendingMode = false;
        applyStructural(true);
        prepared = true;
    }

    // ---- settings (audio thread, block start) ----
    void setSettings(const Settings& s)
    {
        set = s;
        set.mode = clampv(s.mode, 0, kNumModes - 1);
        if (!prepared) return;
        if (set.mode != curMode) pendingMode = true;
        const double want = std::log(sizeScale(set.size));
        if (std::fabs(want - lnScaleTarget) > 0.35) pendingJump = true;   // a preset-sized jump: fade, switch, fade
        lnScaleTarget = want;
    }
    void setTempo(double b) { bpm = clampv(std::isfinite(b) ? b : 120.0, 20.0, 999.0); }

    int latency() const { return 0; }

    // ---- feeds (UI) ----
    float duckDb() const { return duckFeed.load(std::memory_order_relaxed); }           // the wet gain from ducking
    float predelayNowMs() const { return preFeed.load(std::memory_order_relaxed); }     // the pre-delay in use
    float freezeNow() const { return fzFeed.load(std::memory_order_relaxed); }          // 0..1
    // the wet signal's RMS (both channels) since the last call (audio thread: the plugin publishes it)
    float takeWetRms() { const float r = wetN > 0 ? (float)std::sqrt(wetE / (2.0 * wetN)) : 0.0f; wetE = 0.0; wetN = 0; return r; }

    // Control-rate work (glides, absorption, modulation targets) runs every kChunk samples on a clock of its own, so
    // the host's block size never changes how the reverb sounds.
    void process(float* L, float* R, int n)
    {
        int i = 0;
        while (i < n)
        {
            if (ctrlLeft == 0) { beginControl(); ctrlLeft = kChunk; }
            const int m = std::min(ctrlLeft, n - i);
            processRun(L + i, R + i, m);
            i += m;
            ctrlLeft -= m;
        }
    }

private:
    bool freezeOn() const { return set.freeze && voice(set.mode).topo != kTopoBurst; }
    double shimmerAmount() const { return set.mode == kShimmerMode ? 0.75 * clampv(set.shimmer, 0.0, 100.0) * 0.01 : 0.0; }
    static void balanceGains(double b, double& e, double& l)
    {
        b = clampv(b, -100.0, 100.0) * 0.01;
        e = b > 0.0 ? 1.0 - b : 1.0;
        l = b < 0.0 ? 1.0 + b : 1.0;
    }

    // the parts that change topology: applied while the wet path is faded out
    void applyStructural(bool clearAll)
    {
        const bool modeChanged = clearAll || pendingJump || curMode != set.mode;
        curMode = set.mode;
        vc = voice(curMode);
        lnScale = lnScaleTarget;
        const double ms = sr * 0.001;
        nLines = vc.topo == kTopoFdn ? vc.lines : 0;
        // FDN: primes spread geometrically, shuffled so neighbours (and the L / R inputs) mix short and long lines
        static const int order16[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
        static const int order8[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
        for (int k = 0; k < nLines; ++k)
        {
            const int slot = nLines == 16 ? order16[k] : order8[k];
            const double t = slot / (double)(nLines - 1);
            const double lenMs = vc.lenMin * std::pow(vc.lenMax / vc.lenMin, t) * (1.0 + 0.025 * jitter[k]);
            baseLen[k] = (double)nearestPrime((int)std::lround(lenMs * ms));
            apLen[k] = vc.apMs > 0.0 ? nearestPrime((int)std::lround(vc.apMs * (0.6 + 0.8 * (k / (double)(nLines - 1))) * ms)) : 0;
            if (apLen[k] > 0 && k % 2 == 1) apLen[k] = nearestPrime((int)std::lround(apLen[k] * 0.83));
        }
        // input diffusers (fixed per voice; L and R differ)
        static const double dr[2][4] = { { 0.376, 0.283, 1.0, 0.732 }, { 0.41, 0.31, 0.93, 0.69 } };
        for (int ch = 0; ch < 2; ++ch)
            for (int j = 0; j < 4; ++j)
                diffLen[ch][j] = std::max(1, nearestPrime((int)std::lround(vc.diffMs * dr[ch][j] * ms)));
        static const double pr[2][2] = { { 1.3, 1.9 }, { 1.5, 1.7 } };
        for (int ch = 0; ch < 2; ++ch)
            for (int j = 0; j < 2; ++j) postLen[ch][j] = std::max(1, nearestPrime((int)std::lround(pr[ch][j] * ms)));
        er.build(vc.room[0], vc.room[1], vc.room[2], vc.refl, erSeed(curMode));
        for (int ch = 0; ch < 2; ++ch) erLp[ch].set(vc.erLp, sr);
        if (modeChanged)
        {
            for (int k = 0; k < kMaxLines; ++k) { line[k].clear(); dens[k].clear(); ab[k].reset(); }
            for (auto& b : plate) { b.ap1.clear(); b.dA.clear(); b.ap2.clear(); b.dB.clear(); b.ab.reset(); }
            for (int ch = 0; ch < 2; ++ch)
            {
                for (auto& a : diff[ch]) a.clear();
                for (auto& a : post[ch]) a.clear();
                burst[ch].clear(); shifter[ch].reset(); shimLp[ch].reset();
                bwIn[ch][0].reset(); bwIn[ch][1].reset(); bwOut[ch][0].reset(); bwOut[ch][1].reset();
                crush[ch].reset(); conv[ch][0].reset(); conv[ch][1].reset(); erLp[ch].reset();
            }
            burstShapeT = -1.0;
        }
        pendingMode = pendingJump = false;
    }
    void clearState()
    {
        for (int k = 0; k < kMaxLines; ++k) { line[k].clear(); dens[k].clear(); ab[k].reset(); }
        for (auto& b : plate) { b.ap1.clear(); b.dA.clear(); b.ap2.clear(); b.dB.clear(); b.ab.reset(); }
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& a : diff[ch]) a.clear();
            for (auto& a : post[ch]) a.clear();
            burst[ch].clear(); erLine[ch].clear(); shifter[ch].reset(); shimLp[ch].reset();
            bwIn[ch][0].reset(); bwIn[ch][1].reset(); bwOut[ch][0].reset(); bwOut[ch][1].reset();
            crush[ch].reset(); conv[ch][0].reset(); conv[ch][1].reset(); erLp[ch].reset(); lc[ch].reset(); hc[ch].reset();
        }
        pre.clear();
        duckEnv = 0.0;
    }

    // ---- control rate ----
    void control()
    {
        // glides (log domain for times and frequencies)
        rtS = std::exp(std::log(rtS) + (std::log(clampv(set.decay, 0.1, 30.0)) - std::log(rtS)) * kc50);
        bassS = std::exp(std::log(bassS) + (std::log(clampv(set.bassMult, 0.5, 2.0)) - std::log(bassS)) * kc50);
        hdS += (clampv(set.highDamp, 0.1, 1.0) - hdS) * kc50;
        xoverS = std::exp(std::log(xoverS) + (std::log(clampv(set.crossover, 50.0, 2000.0)) - std::log(xoverS)) * kc50);
        dampS = std::exp(std::log(dampS) + (std::log(clampv(set.dampFreq, 1000.0, 20000.0)) - std::log(dampS)) * kc50);
        ageS += (clampv(set.age, 0.0, 100.0) * 0.01 - ageS) * kc50;
        depthS += (clampv(set.modDepth, 0.0, 100.0) * 0.01 - depthS) * kc50;
        rateS = std::exp(std::log(rateS) + (std::log(clampv(set.modRate, 0.05, 5.0)) - std::log(rateS)) * kc50);
        shimS += (shimmerAmount() - shimS) * kc50;
        fz += ((freezeOn() ? 1.0 : 0.0) - fz) * kc30;
        if (std::fabs(fz - (freezeOn() ? 1.0 : 0.0)) < 1e-4) fz = freezeOn() ? 1.0 : 0.0;
        envTS = std::exp(std::log(envTS) + (std::log(clampv(set.envTimeMs, 50.0, 1500.0) * 0.001) - std::log(envTS)) * kc50);
        diffS += (clampv(set.diffusion, 0.0, 100.0) * 0.01 - diffS) * kc50;
        lcS = std::exp(std::log(lcS) + (std::log(clampv(set.lowCut, 10.0, 1000.0)) - std::log(lcS)) * kc50);
        hcS = std::exp(std::log(hcS) + (std::log(clampv(set.highCut, 1000.0, 22000.0)) - std::log(hcS)) * kc50);
        // SIZE: a one-pole glide, rate-limited to 0.3 per second in log (the longest lines bend at most ~7 % in pitch
        // while it moves)
        {
            const double d = lnScaleTarget - lnScale;
            const double lim = 0.3 * kChunk / sr;
            lnScale += clampv(d * kc200, -lim, lim);
            if (std::fabs(d) < 1e-6) lnScale = lnScaleTarget;
        }
        const double s = std::exp(lnScale), ms = sr * 0.001;
        scaleNow = s;

        // late level: normalised for size, half-normalised for decay
        lateNorm = vc.trim * (vc.topo == kTopoBurst ? 1.0 : std::sqrt(s / 0.69) * std::pow(rtS / 2.0, -0.25));

        // absorption for every line / branch at its own loop length
        absC.set(xoverS, dampS, sr);
        const double rtL = rtS * bassS, rtH = rtS * hdS;
        auto freezeIt = [&](Absorber& a) {
            if (fz <= 0.0) return;
            const double gm = a.gM, gl = gm * a.rL, gh = gm * a.rH;
            const double nm = gm + (1.0 - gm) * fz, nl = gl + (1.0 - gl) * fz, nh = gh + (1.0 - gh) * fz;
            a.gM = (float)nm; a.rL = (float)(nl / nm); a.rH = (float)(nh / nm);
        };
        for (int k = 0; k < nLines; ++k)
        {
            ab[k].design(baseLen[k] * s + 1.0 + apLen[k], rtL, rtS, rtH, sr);
            freezeIt(ab[k]);
        }
        if (vc.topo == kTopoPlate)
            for (int b = 0; b < 2; ++b)
            {
                const PlateGeom& g = plateGeom(b);
                plate[b].ab.design((g.ap1 + g.dA + g.ap2 + g.dB) * s * ms + 4.0, rtL, rtS, rtH, sr);
                freezeIt(plate[b].ab);
            }

        // read lengths for this tick. Frozen, they glide to whole samples (a cubic read between samples is a gentle
        // low-pass: a frozen loop must be exactly lossless) and the modulation and shimmer fade out.
        const double thaw = 1.0 - fz;
        auto whole = [&](double len) { const double f = std::floor(len); return f + (len - f) * thaw; };
        for (int k = 0; k < nLines; ++k) readLen[k] = whole(baseLen[k] * s);
        if (vc.topo == kTopoPlate)
            for (int b = 0; b < 2; ++b)
            {
                const PlateGeom& g = plateGeom(b);
                plateLen[b][0] = whole(g.ap1 * s * ms); plateLen[b][1] = whole(g.dA * s * ms);
                plateLen[b][2] = whole(g.ap2 * s * ms); plateLen[b][3] = whole(g.dB * s * ms);
            }
        shimNow = shimS * thaw;

        // modulation targets for the end of this tick (per-sample linear steps toward them)
        const double wDepth = vc.wanderMs * depthS * ms * thaw, sDepth = vc.spinMs * depthS * ms * thaw;
        const int nMod = vc.topo == kTopoPlate ? 2 : nLines;
        for (int k = 0; k < nMod; ++k)
        {
            const double rk = rateS * (0.75 + 0.5 * (k / std::max(1.0, nMod - 1.0)));
            wander[k].setRate(rk * vc.wanderMul);
            spin[k].setRate(rk * vc.spinMul, sr / kChunk);
            spin[k].advance();
            const double target = wDepth * wander[k].next() + sDepth * spin[k].sine();
            modStep[k] = (float)((target - modNow[k]) / kChunk);
        }

        // colour
        col = colourFor(vc.colour, ageS);
        if (vc.colour != kColourClean)
        {
            static const double q4[2] = { 0.54119610, 1.30656296 };   // 4-pole Butterworth
            for (int ch = 0; ch < 2; ++ch)
                for (int j = 0; j < 2; ++j) { bwIn[ch][j].set(col.bw, q4[j], sr); bwOut[ch][j].set(col.bw, q4[j], sr); }
            for (int ch = 0; ch < 2; ++ch) crush[ch].set(col.rate / sr, 0.0);
            convQ = std::pow(2.0, col.convBits - 1.0);
            loopMask = mantissaMask(col.loopBits);
            loopQ = std::ldexp(1.0, col.loopBits - 1);
        }

        // diffusion
        const double d = diffS;
        diffG = (float)(vc.diffG * (0.45 + 0.55 * d));
        densG = (float)(vc.apG * d);
        plateG1 = (float)(0.7 * (0.4 + 0.6 * d));
        plateG2 = (float)(0.5 * (0.4 + 0.6 * d));
        postG = (float)(0.4 * (0.4 + 0.6 * d));

        // burst taps: re-shape when TIME or DECAY moved
        if (vc.topo == kTopoBurst && (std::fabs(envTS - burstShapeT) > 1e-4 * envTS || std::fabs(rtS - burstShapeD) > 1e-4 * rtS || burstShapeMode != curMode))
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                double e = 0.0;
                for (int k = 0; k < kBurstTaps; ++k)
                {
                    const double g = burstEnvelope(curMode, burstU[ch][k], envTS, rtS) * burstAmp[ch][k];
                    burstG[ch][k] = (float)g; e += g * g;
                }
                const float sc = (float)(1.0 / std::sqrt(std::max(1e-12, e)));
                for (int k = 0; k < kBurstTaps; ++k) burstG[ch][k] *= sc;
            }
            burstShapeT = envTS; burstShapeD = rtS; burstShapeMode = curMode;
        }

        // pre-delay and the output filters
        const double pd = predelayFor(set.sync, set.note, set.predelayMs, bpm);
        pre.setTarget((int)std::lround(pd * ms));
        preFeed.store((float)pd, std::memory_order_relaxed);
        for (int ch = 0; ch < 2; ++ch)
        {
            lc[ch].set(lcS, 0.70710678, sr);
            hc[ch].set(std::min(hcS, 0.45 * sr), 0.70710678, sr);
        }
        shifter[0].setRatio(fx::semitonesToRatio(set.pitch));
        shifter[1].setRatio(fx::semitonesToRatio(set.pitch));
    }

    inline float quantLoop(float x) const
    {
        if (col.loopFixed) return (float)(std::trunc(x * loopQ) / loopQ);
        return truncMantissa(x, loopMask);
    }

    // once per control tick: structural switches, glides, and the per-sample smoothers' targets
    void beginControl()
    {
        if (pendingMode || pendingJump)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) { applyStructural(false); trans.setTarget(1.0); }
        }
        else trans.setTarget(1.0);
        control();
        const double mx = clampv(set.mix, 0.0, 100.0) * 0.01;
        dryS.setTarget(std::cos(0.5 * kPi * mx));   // equal power; mix 0 = exactly the dry signal
        wetS.setTarget(std::sin(0.5 * kPi * mx));
        outS.setTarget(dbToGain(set.outDb));
        double eg, lg; balanceGains(set.balance, eg, lg);
        earlyS.setTarget(eg * vc.erLevel); lateS.setTarget(lg * lateNorm);
        widthS.setTarget(clampv(set.width, 0.0, 150.0) * 0.01);
        lcOn.setTarget(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        hcOn.setTarget(highCutActive(set.highCut) ? 1.0 : 0.0);
        inGain.setTarget(freezeOn() ? 0.0 : 1.0);
        // duck: up to -30 dB as the dry input's peak envelope goes from -45 to -10 dBFS
        const double duckAmt = clampv(set.duck, 0.0, 100.0) * 0.01;
        duckTarget = duckAmt > 0.0 ? dbToGain(-30.0 * duckAmt * clampv((gainToDb(duckEnv + 1e-9) + 45.0) / 35.0, 0.0, 1.0)) : 1.0;
        duckFeed.store((float)gainToDb(duckG), std::memory_order_relaxed);
        fzFeed.store((float)fz, std::memory_order_relaxed);
        // recover from non-finite state (clear the tank and keep going)
        double chk = duckG + duckEnv;
        for (int k = 0; k < nLines; ++k) chk += ab[k].s1 + ab[k].s2 + line[k].at(0);
        for (const auto& b : plate) chk += b.ab.s1 + b.dB.at(0);
        if (!std::isfinite(chk)) { clearState(); duckG = 1.0; }
    }

    void processRun(float* L, float* R, int n)
    {
        const double s = scaleNow;
        const bool colour = vc.colour != kColourClean;
        const bool holdOn = colour && col.rate < sr * 0.999;
        const float dg = diffG;
        double e = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float dl = L[i], dr = R[i];
            // duck detector (dry)
            const double a = std::max(std::fabs(dl), std::fabs(dr));
            duckEnv += (a - duckEnv) * (a > duckEnv ? duckAtt : duckRel);
            duckG += (duckTarget - duckG) * duckGlide;

            // pre-delay, colour in
            const float ig = (float)inGain.next();
            float pl, pr;
            pre.process(dl * ig, dr * ig, pl, pr);
            if (colour)
            {
                pl = (float)bwIn[0][1].lowpass(bwIn[0][0].lowpass(pl));
                pr = (float)bwIn[1][1].lowpass(bwIn[1][0].lowpass(pr));
                pl = convert(conv[0][0], pl, ig); pr = convert(conv[1][0], pr, ig);
            }

            // early reflections (alternate taps hear the opposite side: both side walls)
            float el = 0.0f, er_ = 0.0f;
            if (vc.erLevel > 0.0)
            {
                erLine[0].push(pl); erLine[1].push(pr);
                for (int k = 0; k < er.n[0]; ++k) el += er.tap[0][k].g * erLine[k & 1].readLinear(er.tap[0][k].t * s * sr);
                for (int k = 0; k < er.n[1]; ++k) er_ += er.tap[1][k].g * erLine[(k & 1) ^ 1].readLinear(er.tap[1][k].t * s * sr);
                el = (float)erLp[0].lp(el); er_ = (float)erLp[1].lp(er_);
            }

            // input diffusion
            float xl = pl, xr = pr;
            for (int j = 0; j < 4; ++j)
            {
                const float g = j < 2 ? dg : 0.83f * dg;
                xl = diff[0][j].processInt(xl, diffLen[0][j], g);
                xr = diff[1][j].processInt(xr, diffLen[1][j], g);
            }

            // late tank
            float ll = 0.0f, lr = 0.0f;
            if (vc.topo == kTopoFdn) fdn(xl, xr, ll, lr);
            else if (vc.topo == kTopoPlate) plateTank(0.5f * (xl + xr), ll, lr);
            else burstTank(xl, xr, ll, lr);

            const float eG = (float)earlyS.next(), lG = (float)lateS.next(), tr = (float)trans.next();
            float wl = (eG * el + lG * ll) * tr, wr = (eG * er_ + lG * lr) * tr;

            // colour out: the internal rate's hold, the output converter, reconstruction
            if (colour)
            {
                if (holdOn) { wl = (float)crush[0].process(wl); wr = (float)crush[1].process(wr); }
                wl = convert(conv[0][1], wl, 1.0f); wr = convert(conv[1][1], wr, 1.0f);
                wl = (float)bwOut[0][1].lowpass(bwOut[0][0].lowpass(wl));
                wr = (float)bwOut[1][1].lowpass(bwOut[1][0].lowpass(wr));
            }
            // width (mid / side)
            const float wd = (float)widthS.next();
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * wd;
            double ol = m + sd, orr = m - sd;
            // output filters (their end stops are off: crossfaded in and out)
            const double lcm = lcOn.next(), hcm = hcOn.next();
            if (lcm > 0.0)
            {
                const double hl = lc[0].highpass(ol), hr = lc[1].highpass(orr);
                ol += (hl - ol) * lcm; orr += (hr - orr) * lcm;
            }
            if (hcm > 0.0)
            {
                const double tl = hc[0].lowpass(ol), tr2 = hc[1].lowpass(orr);
                ol += (tl - ol) * hcm; orr += (tr2 - orr) * hcm;
            }
            ol *= duckG; orr *= duckG;
            e += ol * ol + orr * orr;

            const double dG = dryS.next(), wG = wetS.next(), og = outS.next();
            L[i] = (float)((dl * dG + ol * wG) * og);
            R[i] = (float)((dr * dG + orr * wG) * og);
        }
        if (!std::isfinite(e))
        {
            clearState();
            for (int i = 0; i < n; ++i) { if (!std::isfinite(L[i])) L[i] = 0.0f; if (!std::isfinite(R[i])) R[i] = 0.0f; }
            e = 0.0;
        }
        wetE += e;
        wetN += n;
    }

    // a converter of the voice's colour; `dith` scales its dither (0 while frozen: a lossless tank must not
    // integrate noise)
    inline float convert(Converter& c, float x, float dith)
    {
        float y = c.process(x, (float)convQ, col.ranges);
        if (col.dither && dith > 0.0f)
        {
            // TPDF dither at the current range's step: the noise floats with the level
            int e = 0;
            std::frexp(std::max(c.env, 1e-9f), &e);
            const float step = 1.0f / ((float)convQ * (float)(1 << clampv(-e, 0, col.ranges)));
            y += dith * step * (float)(noise.uni() - noise.uni());
        }
        return y;
    }

    // ---- FDN ----
    inline void fdn(float xl, float xr, float& outL, float& outR)
    {
        float y[kMaxLines];
        const int N = nLines;
        const bool quant = vc.colour != kColourClean;
        for (int k = 0; k < N; ++k)
        {
            modNow[k] += modStep[k];
            float v = line[k].read(readLen[k] + modNow[k]);
            if (apLen[k] > 0) v = dens[k].processInt(v, apLen[k], densG);
            v = ab[k].process(v, absC);
            y[k] = quant ? quantLoop(v) : v;
        }
        if (shimNow > 1e-4)
        {
            const float a = (float)shimNow;
            y[0] += a * (shifter[0].process((float)shimLp[0].lp(y[0])) - y[0]);
            y[1] += a * (shifter[1].process((float)shimLp[1].lp(y[1])) - y[1]);
        }
        if (vc.householder) householder(y, N); else hadamard(y, N);
        const float bi = N == 16 ? 0.5f : 0.70710678f;
        for (int k = 0; k < N; ++k) line[k].push(y[k] + inSign[k] * bi * ((k & 1) ? xr : xl));
        float ol = 0.0f, orr = 0.0f;
        for (int k = 0; k < N; ++k)
        {
            ol += outSign[0][k] * line[k].readLinear(tapPos[0][k] * readLen[k]);
            orr += outSign[1][k] * line[k].readLinear(tapPos[1][k] * readLen[k]);
        }
        const float og = N == 16 ? 0.25f : 0.35355339f;
        outL = ol * og; outR = orr * og;
    }

    // ---- figure-eight plate ----
    struct PlateBranch { fx::Allpass ap1, ap2; fx::FracDelay dA, dB; Absorber ab; };
    inline void plateTank(float x, float& outL, float& outR)
    {
        const bool quant = vc.colour != kColourClean;
        float end[2];
        for (int b = 0; b < 2; ++b) end[b] = plate[b].dB.read(plateLen[b][3]);
        for (int b = 0; b < 2; ++b)
        {
            PlateBranch& p = plate[b];
            modNow[b] += modStep[b];
            float v = p.ap1.process(x + end[b ^ 1], plateLen[b][0] + modNow[b], -plateG1);
            p.dA.push(v);
            v = p.dA.read(plateLen[b][1]);
            v = p.ab.process(v, absC);
            if (quant) v = quantLoop(v);
            v = p.ap2.process(v, plateLen[b][2], plateG2);
            p.dB.push(v);
        }
        float o[2] = { 0.0f, 0.0f };
        for (int side = 0; side < 2; ++side)
            for (const PlateTap& t : kPlateTaps[side])
            {
                const PlateBranch& p = plate[t.branch];
                const double* len = plateLen[t.branch];
                float v;
                if (t.seg == 0) v = p.dA.readLinear(t.pos * len[1]);
                else if (t.seg == 1) v = p.ap2.line().readLinear(t.pos * len[2]);
                else v = p.dB.readLinear(t.pos * len[3]);
                o[side] += t.sign * v;
            }
        outL = 0.42f * o[0]; outR = 0.42f * o[1];
    }

    // ---- Gated / Reverse ----
    inline void burstTank(float xl, float xr, float& outL, float& outR)
    {
        burst[0].push(xl); burst[1].push(xr);
        const double T = envTS * sr;
        float o[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < 2; ++ch)
        {
            float acc = 0.0f;
            const float* u = burstU[ch];
            const float* g = burstG[ch];
            for (int k = 0; k < kBurstTaps; ++k) acc += g[k] * burst[ch].readLinear(u[k] * T);
            for (int j = 0; j < 2; ++j) acc = post[ch][j].processInt(acc, postLen[ch][j], postG);
            o[ch] = acc;
        }
        outL = o[0]; outR = o[1];
    }

    double sr = 48000.0, bpm = 120.0;
    Settings set;
    Voice vc;
    bool prepared = false, pendingMode = false, pendingJump = false;
    int curMode = kHall, nLines = 16;

    // FDN
    fx::FracDelay line[kMaxLines];
    fx::Allpass dens[kMaxLines];
    Absorber ab[kMaxLines];
    AbsorberCoefs absC;
    double baseLen[kMaxLines] = {}, readLen[kMaxLines] = {}, plateLen[2][4] = {};
    int apLen[kMaxLines] = {};
    float inSign[kMaxLines] = {}, outSign[2][kMaxLines] = {}, tapPos[2][kMaxLines] = {};
    double jitter[kMaxLines] = {};
    fx::Wander wander[kMaxLines];
    fx::Lfo spin[kMaxLines];
    float modNow[kMaxLines] = {}, modStep[kMaxLines] = {};
    AlignedPitchShifter shifter[2];
    fx::OnePole shimLp[2];
    // plate
    PlateBranch plate[2];
    float plateG1 = 0.5f, plateG2 = 0.4f;
    // burst
    fx::FracDelay burst[2];
    fx::Allpass post[2][2];
    int postLen[2][2] = {};
    float burstU[2][kBurstTaps] = {}, burstAmp[2][kBurstTaps] = {}, burstG[2][kBurstTaps] = {};
    double burstShapeT = -1.0, burstShapeD = -1.0;
    int burstShapeMode = -1;
    float postG = 0.4f;
    // input side
    HeadDelay pre;
    fx::Allpass diff[2][4];
    int diffLen[2][4] = {};
    float diffG = 0.6f, densG = 0.4f;
    ImagePattern er;
    fx::FracDelay erLine[2];
    fx::OnePole erLp[2];
    // colour
    ColourParams col;
    fx::Svf bwIn[2][2], bwOut[2][2];
    fx::Crusher crush[2];
    Converter conv[2][2];
    fx::Rng noise { 0xD17E4u };
    double convQ = 2048.0, loopQ = 32768.0;
    uint32_t loopMask = 0xFFFFFFFFu;
    // output
    fx::Svf lc[2], hc[2];
    LinearSmoother dryS, wetS, outS, earlyS, lateS, widthS, lcOn, hcOn, inGain, trans;
    // control state
    double lnScale = 0.0, lnScaleTarget = 0.0, scaleNow = 1.0;
    double rtS = 2.2, bassS = 1.2, hdS = 0.55, xoverS = 600.0, dampS = 6000.0, ageS = 0.35, depthS = 0.4, rateS = 0.7;
    double shimS = 0.0, shimNow = 0.0, fz = 0.0, envTS = 0.32, lateNorm = 1.0, diffS = 0.8, lcS = 10.0, hcS = 22000.0;
    double kc50 = 0.03, kc30 = 0.05, kc200 = 0.01;
    double duckEnv = 0.0, duckG = 1.0, duckAtt = 0.01, duckRel = 1e-4;
    double duckTarget = 1.0, duckGlide = 0.02;
    double wetE = 0.0;
    int wetN = 0;
    int ctrlLeft = 0;
    std::atomic<float> duckFeed { 0.0f }, preFeed { 20.0f }, fzFeed { 0.0f };
};

}} // namespace pump::verb
