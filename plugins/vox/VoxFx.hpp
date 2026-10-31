// pump VOX, VOICE FX engine: creative voice processing without a vocoder or pitch correction. Framework-free.
//
// The voice (mono) runs through four sections, each with its own on/off:
//   PITCH      a pitch-synchronous resynthesis (TUNE's PSOLA, from pitch::Tracker's periods) moves the pitch by
//              PITCH semitones (-24..+24) and, separately, the formants by FORMANT (-12..+12: deep monster to
//              chipmunk, or a gender bend when it goes against the pitch); SUB blends a voice an octave below.
//   GRIT       the gravel: FRY adds the octave-down voice under the main one with a rough, creaky blend (the
//              subharmonic of vocal fry), RASP adds noise that pulses with the voice's own waveform (pitch-locked
//              breath, a throaty rasp rather than hiss), RATTLE flutters the level at RATE (20..80 Hz, smoothed
//              random: a growl), DRIVE pushes an asymmetric tube curve (0..36 dB, level kept), TONE tilts it dark or
//              bright.
//   ROBOT      MONO flattens the melody toward one note (100 % = a monotone robot): the note held on MIDI (the
//              last one), or NOTE without MIDI, reached in GLIDE ms; RING multiplies the voice with a sine at RING HZ
//              (the classic sci-fi robot); COMB rings a comb tuned to the voice's pitch (metallic, a tin can);
//              CRUSH lowers the sample rate and the bits together.
//   CHARACTER  FILTER: Radio, Phone, Megaphone or Walkie (band limits, a resonance, some drive); STACK adds
//              voices an octave up, down or both (from the same resynthesis, mixed by STACK MIX); DOUBLE two
//              modulated copies (12 and 19 ms) left and right; SLAP a slap-back echo of SLAP MS.
// MIX blends the result with the dry voice (latency-aligned). The latency is the resynthesis's, always (about
// 24 ms at 48 kHz), so switching sections never moves anything in time: what a section doesn't change passes
// through delayed by the same amount.
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Fx.hpp"
#include "dsp/Pitch.hpp"
#include "dsp/Saturation.hpp"
#include "../tune/TuneEngine.hpp"   // its PSOLA (pump::tune::Psola)
#include <atomic>

namespace pump { namespace vox {

using namespace pump::fx;

enum FxFilter : int { kFiltOff = 0, kFiltRadio, kFiltPhone, kFiltMegaphone, kFiltWalkie, kNumFxFilters };
static const char* const kFxFilterNames[] = { "Off", "Radio", "Phone", "Megaphone", "Walkie" };
enum FxStack : int { kStackOff = 0, kStackUp, kStackDown, kStackBoth, kNumStacks };
static const char* const kFxStackNames[] = { "Off", "Octave up", "Octave down", "Up + down" };

struct FxSettings {
    bool pitchOn = true, gritOn = false, robotOn = false, charOn = false;
    float pitch = 0.0f, formant = 0.0f, sub = 0.0f;                              // st, st, %
    float fry = 0.0f, rasp = 0.0f, rattle = 0.0f, rattleHz = 40.0f, drive = 0.0f, tone = 0.0f;   // %, %, %, Hz, dB, -100..100
    float mono = 0.0f, ring = 0.0f, ringHz = 120.0f, comb = 0.0f, crush = 0.0f, glideMs = 30.0f; int note = 48;
    int filter = kFiltOff, stack = kStackOff; float stackMix = 50.0f, doubler = 0.0f, slap = 0.0f, slapMs = 110.0f;
    float mix = 100.0f, outDb = 0.0f;
};

class VoiceFx {
public:
    static constexpr double kMinHz = 55.0, kMaxHz = 1000.0;

    // feeds for the window
    std::atomic<float> inMidi { -1.0f }, outMidi { -1.0f }, robotNote { -1.0f }, level { -120.0f };

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        tracker.prepare(sr, kMinHz, kMaxHz, 24000.0, 0.0053);
        psola.prepare(sr, sr / kMinHz);
        lat = psola.latencySamples();
        dry.setMax(lat + 8);
        for (auto& d : dbl) d.setMax((int)(0.03 * sr) + 8);
        slapL.setMax((int)(0.3 * sr) + 8); slapR.setMax((int)(0.3 * sr) + 8);
        combLine.setMax((int)(sr / 20.0) + 8);
        dblLfo[0].setRate(0.31, sr); dblLfo[1].setRate(0.23, sr); dblLfo[1].reset(0.37);
        rattleW.setup(sr, 0x5eedu);
        raspHp.set(800.0, 0.7, sr); raspBp.set(3200.0, 0.8, sr);
        driveDc.set(10.0, sr);
        glide.setTime(cur.glideMs * 0.001, sr);
        setFilter(cur.filter);
        reset();
    }
    void reset()
    {
        tracker.reset(); psola.reset();
        dry.clear(); for (auto& d : dbl) d.clear(); slapL.clear(); slapR.clear(); combLine.clear();
        period = 0.0; midiNow = -1.0; ctlW = 0;
        for (auto& c : ctl) c = Ctl();
        envF = 0.0; ringPh = 0.0; crusher.reset(); toneLp.reset(); raspHp.reset(); raspBp.reset();
        for (auto& f : filt) f.reset();
        glide.reset(cur.note);
        for (auto& s : sm) s = 0.0;
        held = 0; lastHeld = -1;
        for (auto& h : down) h = 0;
    }
    void set(const FxSettings& s)
    {
        if (s.filter != cur.filter) setFilter(s.filter);
        if (s.glideMs != cur.glideMs) glide.setTime(std::max(0.5f, s.glideMs) * 0.001, sr);
        cur = s;
    }
    int latency() const { return lat; }
    static int latencyFor(double sampleRate) { return (int)std::ceil(1.25 * sampleRate / kMinHz) + 64; }

    // MIDI: the robot's note (the last key down; let go = the one before, or NOTE when none is held)
    void midi(uint8_t st, uint8_t d1, uint8_t d2)
    {
        const int type = st & 0xF0;
        if (type == 0x90 && d2 > 0) { if (!down[d1 & 127]) { down[d1 & 127] = ++held, lastHeld = d1 & 127; } }
        else if (type == 0x80 || (type == 0x90 && d2 == 0))
        {
            const int k = d1 & 127;
            if (down[k]) { down[k] = 0; int best = -1, bo = 0; for (int i = 0; i < 128; ++i) if (down[i] > bo) { bo = down[i]; best = i; } lastHeld = best; if (best < 0) held = 0; }
        }
        else if (type == 0xB0 && (d1 == 123 || d1 == 120)) { for (auto& h : down) h = 0; lastHeld = -1; held = 0; }
    }

    // mono voice in, stereo out (in may alias outL)
    void process(const float* in, float* outL, float* outR, int n)
    {
        const double og = dbToGain(cur.outDb), mix = clampv(cur.mix * 0.01, 0.0, 1.0);
        // section switches glide (20 ms) so nothing clicks
        const double a = 1.0 - std::exp(-1.0 / (0.02 * sr));
        const double tgt[kSm] = { cur.pitchOn ? 1.0 : 0.0, cur.gritOn ? 1.0 : 0.0, cur.robotOn ? 1.0 : 0.0, cur.charOn ? 1.0 : 0.0, mix };
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            for (int k = 0; k < kSm; ++k) sm[k] += (tgt[k] - sm[k]) * a;
            const double sPitch = sm[0], sGrit = sm[1], sRobot = sm[2], sChar = sm[3], sMix = sm[4];
            float x = in[i];
            if (!(std::fabs(x) < 1e6f)) x = 0.0f;
            peak = std::max(peak, (double)std::fabs(x));
            // pitch tracking, stamped with the input time each estimate describes
            if (tracker.push(x))
            {
                const pitch::Tracker::Result& r = tracker.result();
                const bool v = r.voiced && r.confidence > 0.5f && r.hz >= kMinHz * 0.9 && r.hz <= kMaxHz * 1.1;
                period = v ? sr / r.hz : 0.0;
                Ctl& c = ctl[ctlW & (kCtl - 1)];
                c.time = (double)(psola.inputCount() - tracker.latencySamples());
                c.midi = v ? pitch::hzToMidi(r.hz) : -1.0;
                ++ctlW;
            }
            psola.push(x, period);
            dry.push(x);
            const float xd = dry.at(lat);

            // the control for the sample coming out now
            const Ctl& c = ctlAt((double)(psola.inputCount() - lat));
            const double inM = c.midi;
            const double robotTarget = glide.next(lastHeld >= 0 ? (double)lastHeld : (double)cur.note);
            const double pitchSt = cur.pitch * sPitch;
            const double monoAmt = clampv(cur.mono * 0.01, 0.0, 1.0) * sRobot;
            const double shift = pitchSt + (inM > 0.0 ? monoAmt * (robotTarget - inM) : 0.0);
            const double fmt = std::exp2(cur.formant * sPitch / 12.0);
            const double subLv = clampv(cur.sub * 0.01, 0.0, 1.0) * sPitch + clampv(cur.fry * 0.01, 0.0, 1.0) * 0.8 * sGrit;
            const double st = clampv(cur.stackMix * 0.01, 0.0, 1.0) * sChar;
            const double upLv = (cur.stack == kStackUp || cur.stack == kStackBoth) ? st * 0.7 : 0.0;
            const double dnLv = (cur.stack == kStackDown || cur.stack == kStackBoth) ? st * 0.7 : 0.0;

            tune::Psola::VoiceParams vp[3];
            vp[0].on = true; vp[0].shiftSt = shift; vp[0].formant = fmt;
            vp[1].on = subLv > 1e-4 || dnLv > 1e-4; vp[1].shiftSt = shift - 12.0; vp[1].formant = fmt;   // sub, fry and stack down
            vp[2].on = upLv > 1e-4; vp[2].shiftSt = shift + 12.0; vp[2].formant = fmt;
            float y4[tune::Psola::kMaxVoices] = {};
            psola.render(vp, 3, y4);
            // the resynthesis only where PITCH or ROBOT's MONO asks for it: otherwise the delayed input, untouched
            const double useRes = std::max(sPitch, monoAmt > 0.0 ? sRobot : 0.0);
            double y = xd + (y4[0] - xd) * useRes;
            const double subV = y4[1];
            y += subV * subLv * fryShape(subV);
            const double voiceOut = inM > 0.0 ? inM + shift : -1.0;

            // GRIT
            if (sGrit > 1e-4)
            {
                const double g0 = y;
                double g = y;
                // rasp: noise pulsing with the voice's own (high-passed, rectified) waveform
                const double hp = raspHp.highpass(g);
                envF += (std::fabs(g) - envF) * (std::fabs(g) > envF ? 0.02 : 0.0015);
                const double pulse = std::fabs(hp) / (envF + 1e-4);
                g += raspBp.bandpass(rng.bi()) * envF * clampv(pulse, 0.0, 3.0) * 1.2 * clampv(cur.rasp * 0.01, 0.0, 1.0);
                // rattle: a smoothed random flutter of the level
                rattleW.setRate(clampv(cur.rattleHz, 5.0f, 120.0f));
                const double rw = rattleW.next();
                g *= 1.0 - 0.7 * clampv(cur.rattle * 0.01, 0.0, 1.0) * (0.5 + 0.5 * rw);
                // drive: asymmetric tube, level kept
                const double dr = clampv(cur.drive, 0.0f, 36.0f);
                if (dr > 0.01)
                {
                    const double pre = dbToGain(dr);
                    g = satCurve(SatMode::Tube, (float)(g * pre + 0.08 * dr / 36.0)) / std::pow(pre, 0.85);
                    g = driveDc.process(g);
                }
                // tone: a tilt around 1 kHz (one pole)
                const double t = clampv(cur.tone * 0.01, -1.0, 1.0);
                const double lo = toneLp.lp(g);
                g = t >= 0.0 ? g + t * (g - lo) * 1.2 : g + t * (g - lo) * 0.9;
                y = g0 + (g - g0) * sGrit;
            }

            // ROBOT
            if (sRobot > 1e-4)
            {
                const double r0 = y;
                double r = y;
                const double ring = clampv(cur.ring * 0.01, 0.0, 1.0);
                if (ring > 0.0)
                {
                    ringPh += clampv(cur.ringHz, 5.0f, 5000.0f) / sr; ringPh -= std::floor(ringPh);
                    r = r * (1.0 - ring) + r * std::sin(kTwoPi * ringPh) * ring * 1.41;
                }
                const double comb = clampv(cur.comb * 0.01, 0.0, 1.0);
                const double combHz = voiceOut > 0.0 ? pitch::midiToHz(voiceOut) : pitch::midiToHz(robotTarget);
                const double cd = clampv(sr / clampv(combHz, 40.0, 2000.0), 2.0, (double)(sr / 20.0));
                const double fb = combLine.read(cd - 1.0);
                const double cy = r + fb * (0.55 + 0.4 * comb);
                combLine.push((float)clampv(cy, -4.0, 4.0));
                r = r + (cy * 0.6 - r) * comb;
                const double crush = clampv(cur.crush * 0.01, 0.0, 1.0);
                if (crush > 0.0)
                {
                    crusher.set(std::pow(0.08, crush), 16.0 - 12.0 * crush);
                    r = r + (crusher.process(r) - r) * std::min(1.0, crush * 3.0);
                }
                y = r0 + (r - r0) * sRobot;
            }

            // CHARACTER
            double L = y, R = y;
            if (sChar > 1e-4)
            {
                double f = y;
                if (cur.filter != kFiltOff)
                {
                    f = filt[0].highpass(f); f = filt[1].highpass(f);
                    f = filt[2].lowpass(f); f = filt[3].lowpass(f);
                    f = filt[4].peak(f, filtPeak);
                    f = satCurve(SatMode::Clip, (float)(f * filtDrive)) / filtDrive * filtMakeup;
                }
                f += (y4[2] * upLv + y4[1] * dnLv);
                double l = f, rr = f;
                const double dmix = clampv(cur.doubler * 0.01, 0.0, 1.0);
                dbl[0].push((float)f); dbl[1].push((float)f);
                if (dmix > 0.0)
                {
                    dblLfo[0].advance(); dblLfo[1].advance();
                    const double d0 = (0.012 + 0.0015 * dblLfo[0].sine()) * sr, d1 = (0.019 + 0.0018 * dblLfo[1].sine()) * sr;
                    l += dbl[0].read(d0) * dmix * 0.8; rr += dbl[1].read(d1) * dmix * 0.8;
                    l -= f * dmix * 0.25; rr -= f * dmix * 0.25;
                }
                const double sl = clampv(cur.slap * 0.01, 0.0, 1.0);
                const double sd = clampv(cur.slapMs, 20.0f, 280.0f) * 0.001 * sr;
                const double el = slapL.read(sd), er = slapR.read(sd * 1.07);
                slapL.push((float)(l + el * 0.25)); slapR.push((float)(rr + er * 0.25));
                l += el * sl * 0.6; rr += er * sl * 0.6;
                L = y + (l - y) * sChar; R = y + (rr - y) * sChar;
            }
            outL[i] = (float)((xd + (L - xd) * sMix) * og);
            outR[i] = (float)((xd + (R - xd) * sMix) * og);
            lastOut = voiceOut;
            lastIn = inM;
            lastRobot = robotTarget;
        }
        sanitizeBlock(outL, n); sanitizeBlock(outR, n);
        inMidi.store((float)lastIn, std::memory_order_relaxed);
        outMidi.store((float)lastOut, std::memory_order_relaxed);
        robotNote.store(cur.robotOn && cur.mono > 0.0f ? (float)lastRobot : -1.0f, std::memory_order_relaxed);
        level.store((float)gainToDb(std::max(peak, 1e-6)), std::memory_order_relaxed);
    }

private:
    struct Ctl { double time = -1e18; double midi = -1.0; };
    static constexpr int kCtl = 256, kSm = 5;
    const Ctl& ctlAt(double t) const
    {
        // newest estimate describing a time at or before t
        for (int k = 1; k <= kCtl && k <= ctlW; ++k)
        {
            const Ctl& c = ctl[(ctlW - k) & (kCtl - 1)];
            if (c.time <= t) return c;
        }
        return ctl[(ctlW - 1) & (kCtl - 1)];
    }
    // the fry blend: the sub voice comes in harder on its peaks (a creaky, uneven subharmonic, not a smooth octave)
    static double fryShape(double s) { return 0.75 + 0.6 * std::fabs(std::tanh(3.0 * s)); }

    void setFilter(int f)
    {
        struct P { double hp, lp, pk, pkQ, pkDb, drive, makeDb; };
        static const P presets[kNumFxFilters] = {
            { 20, 20000, 1000, 1, 0, 1, 0 },
            { 280, 4200, 1600, 1.2, 4, 2.0, 2 },    // radio: AM band, a mid lift, a little drive
            { 420, 3100, 1900, 1.6, 6, 3.0, 4 },    // phone: narrower, more drive
            { 650, 2600, 1300, 3.0, 10, 5.0, 3 },   // megaphone: a horn's honk and a hard driver
            { 520, 2400, 1700, 2.0, 5, 6.0, 4 },    // walkie: tight, crunchy
        };
        const P& p = presets[clampv(f, 0, kNumFxFilters - 1)];
        filt[0].set(p.hp, 0.7071, sr); filt[1].set(p.hp, 0.7071, sr);
        filt[2].set(std::min(p.lp, 0.45 * sr), 0.7071, sr); filt[3].set(std::min(p.lp, 0.45 * sr), 0.7071, sr);
        filt[4].set(p.pk, p.pkQ, sr);
        filtPeak = dbToGain(p.pkDb); filtDrive = p.drive; filtMakeup = dbToGain(p.makeDb);
        toneLp.set(1000.0, sr);
    }

    double sr = 48000.0;
    FxSettings cur;
    pitch::Tracker tracker;
    tune::Psola psola;
    int lat = 0;
    double period = 0.0, midiNow = -1.0;
    Ctl ctl[kCtl];
    int ctlW = 0;
    FracDelay dry, dbl[2], slapL, slapR, combLine;
    Lfo dblLfo[2];
    Wander rattleW;
    Rng rng;
    Svf raspHp, raspBp, filt[5];
    OnePole toneLp;
    DcBlocker driveDc;
    Crusher crusher;
    Glide glide;
    double envF = 0.0, ringPh = 0.0, filtPeak = 1.0, filtDrive = 1.0, filtMakeup = 1.0;
    double sm[kSm] = {};
    double lastOut = -1.0, lastIn = -1.0, lastRobot = -1.0;
    int down[128] = {}, held = 0, lastHeld = -1;
};

}} // namespace pump::vox
