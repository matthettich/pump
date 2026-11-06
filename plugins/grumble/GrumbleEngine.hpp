// pump GRUMBLE, creature voice engine. Framework-free.
//
// The voice (the mono sum of the input) feeds four layers, blended on the MORPH pad and each with its own switch,
// level and pan:
//   BEAST   a pitch-synchronous resynthesis (TUNE's PSOLA, from pitch::Tracker's periods) moves the voice by PITCH
//           and its formants by FORMANT (a bigger or smaller throat); FRY adds the octave below on a rough, creaky
//           blend (the subharmonic of vocal fry).
//   SPLIT   a second resynthesised voice with its own PITCH and FORMANT, and WOBBLE: an uneven vibrato of pitch and
//           formant together (two heads arguing, an alien warble, an insect's buzz at full).
//   GROWL   a throat that isn't human: a pulse train at the voice's own pitch moved by PITCH, made ROUGH (each
//           period's length and strength jittered, pulses dropped: the crackle of a growl), mixed with BREATH
//           (noise), then given the voice's words through a 20-band vocoder whose bands are moved by SIZE (formants
//           down = a bigger animal). Unvoiced sounds (s, t, k) come through as breath, so it still speaks.
//   SWARM   a granular cloud of the voice: grains of GRAIN ms at DENSITY, each at PITCH plus a random part of SPREAD
//           semitones, spread across the stereo field by WIDTH (insects, chittering, a crowd of small things).
// The pad's corners are the layers (BEAST top left, SPLIT top right, GROWL bottom left, SWARM bottom right); the
// layers that are on share its weights at constant power, so one layer on its own always sounds at its level.
// Then the body: GATE (below it the creature falls silent, so room noise doesn't growl), DRIVE (an asymmetric tube,
// level kept), TONE (a tilt), CAVE (a dark, short stone room). MIX blends with the voice, latency-aligned.
// The latency is the resynthesis's (about 24 ms at 48 kHz) whatever is on, so nothing moves in time.
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Fx.hpp"
#include "dsp/Pitch.hpp"
#include "dsp/Saturation.hpp"
#include "../tune/TuneEngine.hpp"   // its PSOLA (pump::tune::Psola)
#include "GrumbleParams.hpp"
#include <atomic>

namespace pump { namespace grumble {

using namespace pump::fx;

struct Settings {
    bool on[kNumLayers] = { true, false, true, false };
    float level[kNumLayers] = { 80, 60, 60, 50 };   // %
    float pan[kNumLayers] = { 0, 0, 0, 70 };        // -100..100 (SWARM: width 0..100)
    float beastPitch = -7, beastFormant = -5, beastFry = 30;
    float splitPitch = 5, splitFormant = 4, splitWobble = 25;
    float growlPitch = -12, growlRough = 55, growlBreath = 25, growlSize = -4;
    float swarmPitch = 0, swarmSpread = 7, swarmDensity = 50, swarmGrainMs = 30;
    float morphX = 50, morphY = 50;
    float gateDb = -60, drive = 4, tone = 0, cave = 12, mix = 100, outDb = 0;
};

// The morph pad's weight for each layer at (x, y) in 0..1 (y up), before the layers that are off are left out.
inline void cornerWeights(double x, double y, double w[kNumLayers])
{
    x = clampv(x, 0.0, 1.0); y = clampv(y, 0.0, 1.0);
    w[kBeast] = (1.0 - x) * y; w[kSplit] = x * y; w[kGrowl] = (1.0 - x) * (1.0 - y); w[kSwarm] = x * (1.0 - y);
}
// Each layer's gain from the pad: the weights of the layers that are on, at constant power. A small floor keeps a
// layer that is on from vanishing entirely when the puck sits on another corner's far side.
inline void morphGains(double x, double y, const bool on[kNumLayers], double g[kNumLayers])
{
    double w[kNumLayers];
    cornerWeights(x, y, w);
    double s = 0.0;
    for (int i = 0; i < kNumLayers; ++i) { w[i] = on[i] ? w[i] + 0.02 : 0.0; s += w[i] * w[i]; }
    const double k = s > 0.0 ? 1.0 / std::sqrt(s) : 0.0;
    for (int i = 0; i < kNumLayers; ++i) g[i] = w[i] * k;
}

class Engine {
public:
    static constexpr double kMinHz = 55.0, kMaxHz = 1000.0;
    static constexpr int kBands = 20, kMaxGrains = 40;

    // feeds for the window
    std::atomic<float> inMidi { -1.0f }, gateOpen { 0.0f }, outLevel { -120.0f }, inLevel { -120.0f };
    std::atomic<float> layerLevel[kNumLayers] { { -120.0f }, { -120.0f }, { -120.0f }, { -120.0f } };
    std::atomic<float> morphGain[kNumLayers] { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f } };

    void prepare(double sampleRate, int /*maxBlock*/ = 0)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        tracker.prepare(sr, kMinHz, kMaxHz, 24000.0, 0.0053);
        psola.prepare(sr, sr / kMinHz);
        lat = psola.latencySamples();
        dry.setMax(lat + 8);
        swarmLine.setMax((int)(1.2 * sr) + 8);
        for (int k = 0; k < 4; ++k) caveLine[k].setMax((int)(0.08 * sr) + 8);
        wobLfo.setRate(5.3, sr);
        wobW.setup(sr, 0xB0B0u);
        driveDc[0].set(10.0, sr); driveDc[1].set(10.0, sr);
        toneLp[0].set(900.0, sr); toneLp[1].set(900.0, sr);
        caveLp[0].set(3200.0, sr); caveLp[1].set(3200.0, sr); caveLp[2].set(3200.0, sr); caveLp[3].set(3200.0, sr);
        caveHp.set(120.0, sr);
        breathHp.set(400.0, 0.7, sr);
        setBands(cur.growlSize);
        aEnv = 1.0 - std::exp(-1.0 / (0.003 * sr));
        rEnv = 1.0 - std::exp(-1.0 / (0.030 * sr));
        reset();
    }
    void reset()
    {
        tracker.reset(); psola.reset();
        dry.clear(); swarmLine.clear();
        for (auto& c : caveLine) c.clear();
        for (auto& c : ctl) c = Ctl();
        ctlW = 0; period = 0.0;
        for (auto& b : anaBp) b.reset();
        for (auto& b : synBp) b.reset();
        for (auto& e : anaEnv) e = 0.0;
        for (auto& e : synEnv) e = 0.0;
        for (auto& g : grains) g.on = false;
        for (auto& s : sm) s = 0.0;
        for (auto& s : lvl) s = 0.0;
        for (auto& d : driveDc) d.reset();
        for (auto& t : toneLp) t.reset();
        for (auto& c : caveLp) c.reset();
        caveHp.reset(); breathHp.reset();
        pulsePh = 0.0; pulseAmp = 1.0; pulseHz = 110.0; pulseHold = 0.0; pulseNext = 0.0;
        grainClock = 0.0; gateEnv = 0.0; gateGain = 0.0; holdN = 0; inEnv = 0.0; outEnv = 0.0;
        lastVoicedMidi = 45.0;
        rng.seed(0x5EED1u); grainRng.seed(0xA11CEu);
    }
    void set(const Settings& s)
    {
        if (s.growlSize != cur.growlSize) setBands(s.growlSize);
        cur = s;
    }
    const Settings& settings() const { return cur; }
    int latency() const { return lat; }

    // stereo in place (the voice is the mono sum)
    void process(float* L, float* R, int n)
    {
        const double og = dbToGain(cur.outDb);
        const double a20 = 1.0 - std::exp(-1.0 / (0.02 * sr));   // switches and the pad glide over 20 ms
        double mg[kNumLayers];
        morphGains(cur.morphX * 0.01, cur.morphY * 0.01, cur.on, mg);
        double tgt[kSm];
        for (int k = 0; k < kNumLayers; ++k) tgt[k] = mg[k] * clampv(cur.level[k] * 0.01, 0.0, 1.0);
        tgt[4] = clampv(cur.mix * 0.01, 0.0, 1.0);
        tgt[5] = clampv(cur.cave * 0.01, 0.0, 1.0);
        double panG[3][2];
        for (int k = 0; k < 3; ++k) { double a, b; equalPower(0.5 + 0.5 * clampv(cur.pan[k] * 0.01, -1.0, 1.0), a, b); panG[k][0] = a * 1.4142; panG[k][1] = b * 1.4142; }
        const double gateThr = dbToGain(cur.gateDb);
        const bool gateOn = gateActive(cur.gateDb);
        const double gA = 1.0 - std::exp(-1.0 / (0.002 * sr)), gR = 1.0 - std::exp(-1.0 / (0.12 * sr));
        const int holdLen = (int)(0.05 * sr);
        const double fryAmt = clampv(cur.beastFry * 0.01, 0.0, 1.0);
        const double wob = clampv(cur.splitWobble * 0.01, 0.0, 1.0);
        wobLfo.setRate(4.0 + 5.0 * wob, sr);
        wobW.setRate(2.0 + 10.0 * wob);
        const double rough = clampv(cur.growlRough * 0.01, 0.0, 1.0), breath = clampv(cur.growlBreath * 0.01, 0.0, 1.0);
        const double grainLen = clampv(cur.swarmGrainMs, 5.0f, 150.0f) * 0.001 * sr;
        const double dens = clampv(cur.swarmDensity * 0.01, 0.0, 1.0);
        const double grainRate = 3.0 * std::pow(60.0, dens);   // 3..180 grains a second
        const double overlap = std::max(1.0, grainRate * grainLen / sr);
        const double grainGain = 1.6 / std::sqrt(overlap);
        const double width = clampv(cur.pan[kSwarm] * 0.01, 0.0, 1.0);
        const double drv = clampv(cur.drive, 0.0f, 24.0f), drvPre = dbToGain(drv), drvPost = 1.0 / std::pow(drvPre, 0.85);
        const double drvBias = 0.06 * drv / 24.0, satBias = satCurve(SatMode::Tube, (float)drvBias);
        const double tilt = clampv(cur.tone * 0.01, -1.0, 1.0);
        const double caveFb = 0.55 + 0.3 * clampv(cur.cave * 0.01, 0.0, 1.0);
        static const double caveMs[4] = { 29.7, 37.1, 41.1, 53.3 };
        double peakIn = 0.0, layerPk[kNumLayers] = {};
        double peakOut = 0.0;

        for (int i = 0; i < n; ++i)
        {
            for (int k = 0; k < kSm; ++k) sm[k] += (tgt[k] - sm[k]) * a20;
            float x = 0.5f * (L[i] + R[i]);
            if (!(std::fabs(x) < 1e6f)) x = 0.0f;
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
            const double xd = dry.at(lat);   // the voice, aligned with what comes out now
            peakIn = std::max(peakIn, std::fabs(xd));
            const Ctl& c = ctlAt((double)(psola.inputCount() - lat));
            const double inM = c.midi;
            if (inM > 0.0) lastVoicedMidi = inM;

            // the gate: an envelope of the aligned voice, held 50 ms, opening in 2 ms and closing over 120 ms
            const double ax = std::fabs(xd);
            gateEnv += (ax - gateEnv) * (ax > gateEnv ? aEnv : rEnv);
            if (!gateOn || gateEnv > gateThr) holdN = holdLen;
            else if (holdN > 0) --holdN;
            const double gTgt = holdN > 0 ? 1.0 : 0.0;
            gateGain += (gTgt - gateGain) * (gTgt > gateGain ? gA : gR);

            // ---- BEAST and SPLIT: the resynthesis ----
            tune::Psola::VoiceParams vp[3];
            const bool beastOn = sm[kBeast] > 1e-5, splitOn = sm[kSplit] > 1e-5;
            vp[0].on = beastOn; vp[0].shiftSt = cur.beastPitch; vp[0].formant = std::exp2(cur.beastFormant / 12.0);
            vp[1].on = beastOn && fryAmt > 1e-4; vp[1].shiftSt = cur.beastPitch - 12.0; vp[1].formant = vp[0].formant;
            wobLfo.advance();
            const double wv = wob > 0.0 ? (0.6 * wobLfo.sine() + 0.4 * wobW.next()) : 0.0;
            vp[2].on = splitOn; vp[2].shiftSt = cur.splitPitch + wv * wob * 2.5; vp[2].formant = std::exp2((cur.splitFormant + wv * wob * 3.0) / 12.0);
            float y3[tune::Psola::kMaxVoices] = {};
            psola.render(vp, 3, y3);
            const double beast = y3[0] + y3[1] * fryAmt * (0.75 + 0.6 * std::fabs(std::tanh(3.0 * y3[1])));
            const double split = y3[2];

            // ---- GROWL: a rough pulse train and breath through the voice's vocoder ----
            double growl = 0.0;
            if (sm[kGrowl] > 1e-5)
            {
                const double target = pitch::midiToHz(clampv(lastVoicedMidi + cur.growlPitch, 12.0, 100.0));
                pulseHz += (target - pulseHz) * 0.002;
                // the pulse lands at a fractional time (split across two samples) so the train stays in tune
                double pulse = pulseHold; pulseHold = 0.0;
                pulsePh += 1.0;
                if (pulsePh >= pulseNext)
                {
                    const double f = pulsePh - pulseNext;   // how far past the pulse time this sample is (0..1)
                    // rough: the next period's length and this pulse's strength jittered; some pulses dropped
                    const double jit = 1.0 + rough * 0.22 * rng.bi();
                    const bool drop = rng.uni() < rough * 0.18;
                    pulseAmp = drop ? 0.0 : 1.0 - rough * 0.7 * rng.uni();
                    const double amp = pulseAmp * std::sqrt(sr / pulseHz) * 0.5;
                    pulse += amp * (1.0 - f);
                    pulseHold = amp * f;
                    pulsePh = f;
                    pulseNext = std::max(2.0, sr / pulseHz * jit);
                }
                const double nz = breathHp.highpass(rng.bi()) * 1.6;
                const bool voiced = inM > 0.0;
                const double carrier = voiced ? pulse * (1.0 - breath) + nz * (0.15 + breath) : nz;
                double sum = 0.0;
                for (int b = 0; b < kBands; ++b)
                {
                    const double av = anaBp[b].bandpass(xd);
                    const double aa = std::fabs(av);
                    anaEnv[b] += (aa - anaEnv[b]) * (aa > anaEnv[b] ? aEnv : rEnv);
                    const double cv = synBp[b].bandpass(carrier);
                    const double ca = std::fabs(cv);
                    synEnv[b] += (ca - synEnv[b]) * (ca > synEnv[b] ? aEnv : rEnv);
                    // the carrier flattened, then given the voice's level in this band
                    const double g = std::min(40.0, anaEnv[b] / (synEnv[b] + 1e-4));
                    sum += cv * g;
                }
                growl = clampv(sum * 0.9, -4.0, 4.0);
            }

            // ---- SWARM: grains of the voice ----
            double swL = 0.0, swR = 0.0;
            swarmLine.push((float)xd);
            if (sm[kSwarm] > 1e-5)
            {
                grainClock += grainRate / sr * (0.5 + gateGain);
                if (grainClock >= 1.0 && gateGain > 0.2)
                {
                    grainClock -= std::floor(grainClock);
                    startGrain(grainLen, width);
                }
                else if (grainClock >= 1.0) grainClock -= std::floor(grainClock);
                for (auto& g : grains)
                {
                    if (!g.on) continue;
                    const double t = (double)g.pos / g.len;
                    const double w = 0.5 - 0.5 * std::cos(kTwoPi * t);
                    const double s = swarmLine.readLinear(g.d) * w * grainGain;
                    swL += s * g.gl; swR += s * g.gr;
                    g.d += 1.0 - g.ratio;
                    if (++g.pos >= g.len) g.on = false;
                }
            }

            // ---- the mix of the layers ----
            const double lb = beast * sm[kBeast], ls = split * sm[kSplit], lg = growl * sm[kGrowl];
            double wl = lb * panG[0][0] + ls * panG[1][0] + lg * panG[2][0] + swL * sm[kSwarm];
            double wr = lb * panG[0][1] + ls * panG[1][1] + lg * panG[2][1] + swR * sm[kSwarm];
            layerPk[kBeast] = std::max(layerPk[kBeast], std::fabs(lb));
            layerPk[kSplit] = std::max(layerPk[kSplit], std::fabs(ls));
            layerPk[kGrowl] = std::max(layerPk[kGrowl], std::fabs(lg));
            layerPk[kSwarm] = std::max(layerPk[kSwarm], 0.5 * (std::fabs(swL) + std::fabs(swR)) * sm[kSwarm]);
            wl *= gateGain; wr *= gateGain;

            // ---- body: drive, tone, cave ----
            double w2[2] = { wl, wr };
            for (int ch = 0; ch < 2; ++ch)
            {
                double v = w2[ch];
                if (drv > 0.01)
                {
                    v = (satCurve(SatMode::Tube, (float)(v * drvPre + drvBias)) - satBias) * drvPost;   // the bias's own DC taken out
                    v = driveDc[ch].process(v);
                }
                const double lo = toneLp[ch].lp(v);
                v = tilt >= 0.0 ? v + tilt * (v - lo) * 1.3 : v + tilt * (v - lo) * 0.9;
                w2[ch] = v;
            }
            const double caveAmt = sm[5];
            if (caveAmt > 1e-4 || caveOn)
            {
                caveOn = caveAmt > 1e-4;
                // four lines, a Householder mix, darkened in the loop
                double o[4];
                for (int k = 0; k < 4; ++k) o[k] = caveLine[k].readLinear(caveMs[k] * 0.001 * sr);
                const double h = 0.5 * (o[0] + o[1] + o[2] + o[3]);
                const double in = caveHp.hp(0.5 * (w2[0] + w2[1]));
                for (int k = 0; k < 4; ++k)
                {
                    const double fb = caveLp[k].lp((o[k] - h) * caveFb);
                    caveLine[k].push((float)clampv(fb + (k < 2 ? in : -in) * 0.5, -8.0, 8.0));
                }
                const double cl = (o[0] + o[2]) * 0.7, cr = (o[1] + o[3]) * 0.7;
                w2[0] += cl * caveAmt; w2[1] += cr * caveAmt;
            }

            const double mix = sm[4];
            const double oL = (xd + (w2[0] - xd) * mix) * og, oR = (xd + (w2[1] - xd) * mix) * og;
            L[i] = (float)oL; R[i] = (float)oR;
            peakOut = std::max(peakOut, std::max(std::fabs(oL), std::fabs(oR)));
        }
        sanitizeBlock(L, n); sanitizeBlock(R, n);
        inMidi.store((float)ctlAt((double)(psola.inputCount() - lat)).midi, std::memory_order_relaxed);
        gateOpen.store((float)gateGain, std::memory_order_relaxed);
        inLevel.store((float)gainToDb(std::max(peakIn, 1e-6)), std::memory_order_relaxed);
        outLevel.store((float)gainToDb(std::max(peakOut, 1e-6)), std::memory_order_relaxed);
        for (int k = 0; k < kNumLayers; ++k)
        {
            layerLevel[k].store((float)gainToDb(std::max(layerPk[k], 1e-6)), std::memory_order_relaxed);
            morphGain[k].store((float)mg[k], std::memory_order_relaxed);
        }
    }

private:
    struct Ctl { double time = -1e18; double midi = -1.0; };
    struct Grain { bool on = false; int pos = 0, len = 1; double d = 0.0, ratio = 1.0, gl = 1.0, gr = 1.0; };
    static constexpr int kCtl = 256, kSm = 6;

    const Ctl& ctlAt(double t) const
    {
        for (int k = 1; k <= kCtl && k <= ctlW; ++k)
        {
            const Ctl& c = ctl[(ctlW - k) & (kCtl - 1)];
            if (c.time <= t) return c;
        }
        return ctl[(ctlW - 1) & (kCtl - 1)];
    }

    void startGrain(double len, double width)
    {
        for (auto& g : grains)
        {
            if (g.on) continue;
            const double st = cur.swarmPitch + clampv(cur.swarmSpread, 0.0f, 24.0f) * grainRng.bi();
            g.ratio = std::exp2(clampv(st, -24.0, 36.0) / 12.0);
            g.len = std::max(8, (int)len);
            // start far enough back that a grain read faster than the line never passes the write head
            g.d = std::max(0.0, (g.ratio - 1.0) * g.len) + 16.0 + grainRng.uni() * 0.02 * sr;
            g.d = std::min(g.d, (double)swarmLine.maxDelay() - (std::max(0.0, 1.0 - g.ratio) * g.len) - 4.0);
            double a, b;
            equalPower(0.5 + 0.5 * width * grainRng.bi(), a, b);
            g.gl = a * 1.4142; g.gr = b * 1.4142;
            g.pos = 0; g.on = true;
            return;
        }
    }

    // the vocoder: analysis bands fixed on the voice (90 Hz .. 9 kHz), synthesis bands moved by SIZE semitones
    void setBands(double sizeSt)
    {
        const double lo = 90.0, hi = 9000.0, shift = std::exp2(clampv(sizeSt, -12.0, 12.0) / 12.0);
        for (int b = 0; b < kBands; ++b)
        {
            const double f = lo * std::pow(hi / lo, (double)b / (kBands - 1));
            anaBp[b].set(std::min(f, 0.45 * sr), 4.0, sr);
            synBp[b].set(clampv(f * shift, 30.0, 0.45 * sr), 4.0, sr);
        }
    }

    double sr = 48000.0;
    Settings cur;
    pitch::Tracker tracker;
    tune::Psola psola;
    int lat = 0;
    double period = 0.0;
    Ctl ctl[kCtl];
    int ctlW = 0;
    FracDelay dry, swarmLine, caveLine[4];
    Lfo wobLfo;
    Wander wobW;
    Rng rng, grainRng;
    Svf anaBp[kBands], synBp[kBands], breathHp;
    double anaEnv[kBands] = {}, synEnv[kBands] = {};
    double aEnv = 0.01, rEnv = 0.001;
    double pulsePh = 0.0, pulseNext = 0.0, pulseAmp = 1.0, pulseHz = 110.0, pulseHold = 0.0, lastVoicedMidi = 45.0;
    Grain grains[kMaxGrains];
    double grainClock = 0.0;
    double gateEnv = 0.0, gateGain = 0.0, inEnv = 0.0, outEnv = 0.0;
    int holdN = 0;
    DcBlocker driveDc[2];
    OnePole toneLp[2], caveLp[4], caveHp;
    bool caveOn = false;
    double sm[kSm] = {};
    double lvl[kNumLayers] = {};
};

}} // namespace pump::grumble
