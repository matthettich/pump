// pump ACID engine: a monophonic bass synth with five voicings and a sixteen-step sequencer. Framework-free.
//
// The voice runs at twice the host rate and is brought down by a steep half-band filter (MONO's), since its filters
// and drive are nonlinear.
//
// The models (see AcidParams.hpp):
//   Acid     a band-limited saw, or a square shaped from it; a four-pole diode ladder: four one-pole stages that load
//            each other (each stage's capacitor sees its neighbours, so the poles split apart and the slope is soft
//            near the cutoff), solved stage by stage in order, with a tanh in the resonance loop and a high-pass in
//            the loop (the bass doesn't feed the resonance, so it survives a little and goes thin). The stage
//            coefficient for a cutoff comes from a table built once from the ladder's own frequency response: the
//            cutoff is where the loop turns half a cycle, and the resonance is a fraction of the gain that would make
//            it oscillate there (it gets close and squeals, never quite oscillates). Envelopes: the filter envelope
//            jumps up and decays (DECAY; accented notes always fast), the amp envelope decays slowly while the gate is
//            held; ACCENT opens the filter through a capacitor that charges on each accented note and drains slowly,
//            so consecutive accents climb, and lifts the level
//   Solo     saw + pulse (PW, PWM) crossfaded by WAVE, a square sub an octave down, noise; an OTA ladder (24 or 12)
//   Pocket   three oscillators: the second and third DETUNE either side at OSC 2's level; a two-pole state-variable
//            low-pass with soft integrators
//   Station  two oscillators (the second detuned, OSC 2), a sub; overdrive before a ladder (24 or 12) driven hot
//   Scream   a two-pole low-pass whose resonance loop clips (a diode pair across it), after a gentle high-pass
// Slides glide the pitch exponentially in SLIDE TIME without restarting the envelopes.
//
// The sequencer: SONG locks to the host's song position while it plays; FREE always plays (at the host tempo, locked
// while it plays); KEYS plays from the first step while a key is held, transposed by it. A step starts a note
// (envelopes restart), holds the note before (TIE), or rests. A note with SLIDE holds its gate into the next step,
// which glides to its pitch without restarting the envelopes. The gate otherwise closes after GATE LENGTH of the step.
// SWING delays every second step by up to three quarters of a step (33 % = a triplet shuffle).
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Oversampler.hpp"
#include "dsp/Tempo.hpp"
#include "AcidParams.hpp"
#include <array>
#include <climits>
#include <complex>

namespace pump { namespace acid {

struct Settings {
    int model = kAcid;
    double tune = 0.0, wave = 0.0, cutoff = 420.0, res = 0.72, envMod = 0.55, decay = 0.38, accent = 0.65, slide = 0.06;
    double drive = 0.0, volumeDb = 0.0, attack = 0.001, sustain = 0.0, release = 0.04, pw = 0.5, sub = 0.0, noise = 0.0;
    double detune = 8.0, osc2 = 0.0, keyTrack = 0.0;
    int accentVel = 100, lfoShape = kLfoTri, slope = 0;
    double lfoRate = 4.0, lfoPitch = 0.0, lfoCutoff = 0.0, lfoPw = 0.0, bendRange = 2.0;
    int seqMode = kSeqSong, rate = 2, transpose = 0;
    double swing = 0.0, gateLen = 0.55;
    Pattern pattern = parsePattern(kDefaultPattern);
};

// ---- the diode ladder ----------------------------------------------------------------------------------------------
// Four stages updated in order (each sees the previous one's new value):
//   y1 += 2b (x - y1 + y2);  y2 += b (y1 - 2 y2 + y3);  y3 += b (y2 - 2 y3 + y4);  y4 += b (y3 - 2 y4)
// The table maps a normalised cutoff (radians per sample) to b, and gives the loop gain there.
class DiodeTable {
public:
    static const DiodeTable& get() { static const DiodeTable t; return t; }
    // stage coefficient for a resonance at w (radians/sample), and the loop gain needed to oscillate there
    void lookup(double w, double& b, double& kCrit) const
    {
        w = clampv(w, wTab[0], wTab[kN - 1]);
        const double lw = std::log(w);
        int lo = 0, hi = kN - 1;
        while (hi - lo > 1) { const int m = (lo + hi) / 2; if (std::log(wTab[m]) <= lw) lo = m; else hi = m; }
        const double t = (lw - std::log(wTab[lo])) / std::max(1e-12, std::log(wTab[hi]) - std::log(wTab[lo]));
        b = std::exp(std::log(bTab[lo]) + t * (std::log(bTab[hi]) - std::log(bTab[lo])));
        kCrit = kTab[lo] + t * (kTab[hi] - kTab[lo]);
    }
    // the open-loop response (input to the fourth stage) of a ladder with coefficient b at w
    static std::complex<double> openLoop(double b, double w)
    {
        // state update s' = A s + B x, rows built in update order
        double A[4][4] = {}, Bv[4];
        double r1[4] = { 1 - 2 * b, 2 * b, 0, 0 };
        double r2[4], r3[4], r4[4];
        for (int j = 0; j < 4; ++j) r2[j] = b * r1[j] + (j == 1 ? 1 - 2 * b : 0) + (j == 2 ? b : 0);
        for (int j = 0; j < 4; ++j) r3[j] = b * r2[j] + (j == 2 ? 1 - 2 * b : 0) + (j == 3 ? b : 0);
        for (int j = 0; j < 4; ++j) r4[j] = b * r3[j] + (j == 3 ? 1 - 2 * b : 0);
        Bv[0] = 2 * b; Bv[1] = b * Bv[0]; Bv[2] = b * Bv[1]; Bv[3] = b * Bv[2];
        for (int j = 0; j < 4; ++j) { A[0][j] = r1[j]; A[1][j] = r2[j]; A[2][j] = r3[j]; A[3][j] = r4[j]; }
        // solve (zI - A) S = B
        const std::complex<double> z = std::polar(1.0, w);
        std::complex<double> M[4][5];
        for (int i = 0; i < 4; ++i) { for (int j = 0; j < 4; ++j) M[i][j] = (i == j ? z : 0.0) - A[i][j]; M[i][4] = Bv[i]; }
        for (int c = 0; c < 4; ++c)
        {
            int p = c; for (int i = c + 1; i < 4; ++i) if (std::abs(M[i][c]) > std::abs(M[p][c])) p = i;
            for (int j = 0; j < 5; ++j) std::swap(M[c][j], M[p][j]);
            for (int i = 0; i < 4; ++i)
            {
                if (i == c) continue;
                const std::complex<double> f = M[i][c] / M[c][c];
                for (int j = c; j < 5; ++j) M[i][j] -= f * M[c][j];
            }
        }
        return M[3][4] / M[3][3];
    }
private:
    static constexpr int kN = 160;
    double wTab[kN], bTab[kN], kTab[kN];
    DiodeTable()
    {
        // b from 1e-4 to 0.3 (the phase reaches half a cycle below Nyquist up to about 0.33)
        for (int i = 0; i < kN; ++i)
        {
            const double b = 1e-4 * std::pow(0.3 / 1e-4, (double)i / (kN - 1));
            // the frequency where the open-loop phase crosses -pi: bisect on the unwrapped phase (it falls monotonically)
            double lo = 1e-6, hi = 3.1;
            auto phaseAt = [&](double w) {
                // unwrap by walking up from DC in small steps is costly; the phase of four lags stays above -2 pi, so
                // atan2 with a branch cut at -pi..pi needs one correction: a positive imaginary part past -pi
                const std::complex<double> h = openLoop(b, w);
                double ph = std::arg(h);
                if (ph > 0.0 && w > 1e-3) ph -= kTwoPi;
                return ph;
            };
            for (int it = 0; it < 60; ++it) { const double m = 0.5 * (lo + hi); if (phaseAt(m) > -kPi) lo = m; else hi = m; }
            const double w = 0.5 * (lo + hi);
            bTab[i] = b; wTab[i] = w; kTab[i] = 1.0 / std::max(1e-6, std::abs(openLoop(b, w)));
        }
        // keep the table strictly increasing in w (it is; guard rounding)
        for (int i = 1; i < kN; ++i) if (wTab[i] <= wTab[i - 1]) wTab[i] = wTab[i - 1] * 1.000001;
    }
};

class DiodeLadder {
public:
    void reset() { y1 = y2 = y3 = y4 = 0.0; hp.reset(); }
    // fc (Hz), resonance 0..1, at the rate sr
    void set(double fc, double res, double sr)
    {
        double kc;
        DiodeTable::get().lookup(kTwoPi * clampv(fc, 10.0, 0.18 * sr) / sr, b, kc);
        // the resonance knob's taper (a log pot): most of the squelch in the upper half
        const double r = (1.0 - std::exp(-3.0 * clampv(res, 0.0, 1.0))) / (1.0 - std::exp(-3.0));
        k = 0.985 * r * kc;
        // make up a part of the level the loop takes from the passband (as the original's output stage does)
        out = 1.0 + 0.09 * k;
        hp.set(110.0, sr);
    }
    inline double process(double x)
    {
        const double fb = hp.hp(std::tanh(y4 * 1.6) / 1.6);
        const double y0 = std::tanh(x - k * fb);
        y1 += 2.0 * b * (y0 - y1 + y2);
        y2 += b * (y1 - 2.0 * y2 + y3);
        y3 += b * (y2 - 2.0 * y3 + y4);
        y4 += b * (y3 - 2.0 * y4);
        return y4 * out;
    }
private:
    double y1 = 0, y2 = 0, y3 = 0, y4 = 0, b = 0.01, k = 0.0, out = 1.0;
    fx::OnePole hp;
};

// ---- the other models' filters -------------------------------------------------------------------------------------
// two-pole low-pass, the integrators saturating softly (Pocket)
class SoftSvf {
public:
    void reset() { s1 = s2 = 0.0; }
    void set(double fc, double res, double sr)
    {
        g = std::tan(kPi * clampv(fc, 10.0, 0.45 * sr) / sr);
        kd = 2.0 - 1.9 * clampv(res, 0.0, 1.0);
    }
    inline double process(double x)
    {
        const double a1 = 1.0 / (1.0 + g * (g + kd)), a2 = g * a1, a3 = g * a2;
        const double v3 = x - s2;
        const double v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = std::tanh(0.8 * (2.0 * v1 - s1)) / 0.8;
        s2 = 2.0 * v2 - s2;
        return v2;
    }
private:
    double g = 0.1, kd = 1.4, s1 = 0.0, s2 = 0.0;
};
// the screaming two-pole: a Simper SVF with its band state clipped hard (a diode pair), resonance past oscillation
class ScreamLp {
public:
    void reset() { s1 = s2 = 0.0; }
    void set(double fc, double res, double sr)
    {
        g = std::tan(kPi * clampv(fc, 10.0, 0.45 * sr) / sr);
        const double r = clampv(res, 0.0, 1.0);
        kd = 2.0 * std::pow(1.0 - r, 1.5) - 0.15 * r;   // negative near the top: it oscillates, the diodes hold it
    }
    inline double process(double x)
    {
        const double a1 = 1.0 / (1.0 + g * (g + kd)), a2 = g * a1, a3 = g * a2;
        const double v3 = x - s2;
        const double v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        const double n1 = 2.0 * v1 - s1;
        s1 = 1.2 * std::tanh(n1 / 1.2);   // the diodes
        s2 = 2.0 * v2 - s2;
        return v2;
    }
private:
    double g = 0.1, kd = 1.4, s1 = 0.0, s2 = 0.0;
};

class Engine {
public:
    static constexpr int kCtl = 8;   // base-rate samples between control updates

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate; osr = 2.0 * sr;
        hb.setup(tables::kHB1, (int)(sizeof(tables::kHB1) / sizeof(double)));
        (void)DiodeTable::get();
        (void)synth::BandWaves::get();
        aEnv.setSampleRate(osr); fEnv.setSampleRate(osr);
        resetVoice();
        dc.set(12.0, sr); dc.reset();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        seqReset();
        ctl = 0;
    }
    void setSettings(const Settings& s) { set = s; }
    void setTransport(const Transport& t) { tr = t; }
    void setPreview(bool on) { preview = on; }
    int latency() const { return (hb.delay() + 1) / 2; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    float cutoffNow() const { return cutFeed.load(std::memory_order_relaxed); }
    float pitchNow() const { return pitchFeed.load(std::memory_order_relaxed); }
    bool gateNow() const { return gateFeed.load(std::memory_order_relaxed); }
    float ampNow() const { return ampFeed.load(std::memory_order_relaxed); }
    float filterEnvNow() const { return fenvFeed.load(std::memory_order_relaxed); }
    float accentNow() const { return accFeed.load(std::memory_order_relaxed); }
    int stepNow() const { return stepFeed.load(std::memory_order_relaxed); }   // the pattern step playing, -1 = stopped
    bool noteLit(int n) const { return gateNow() && n == litNote.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        const int mode = preview && set.seqMode != kSeqKeys ? kSeqFree : set.seqMode;
        if (mode != lastMode) { if (seqOwns) releaseVoice(); seqReset(); lastMode = mode; }
        const bool locked = tr.playing && tr.valid && std::isfinite(tr.ppq) && std::fabs(tr.ppq) < 1e9;
        const double spq = stepsPerQuarter(set.rate);
        const double stepsPerSample = clampv(tr.bpm, 20.0, 999.0) / 60.0 / sr * spq;
        // SONG stops with the host
        if (mode == kSeqSong && !locked && running) { if (seqOwns) releaseVoice(); seqReset(); }
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            // the step clock (in steps)
            bool clocked = false;
            double x = 0.0;
            if (mode == kSeqSong && locked) { x = tr.ppq * spq + i * stepsPerSample; clocked = true; }
            else if (mode == kSeqFree)
            {
                if (locked) x = tr.ppq * spq + i * stepsPerSample;
                else { x = freePos; freePos += stepsPerSample; }
                clocked = true;
            }
            else if (mode == kSeqKeys && nKeys > 0) { x = freePos; freePos += stepsPerSample; clocked = true; }
            if (clocked) clock(x);
            if ((ctl++ % kCtl) == 0) control();
            float y[2];
            for (int k = 0; k < 2; ++k) y[k] = (float)voiceSample();
            double out = hb.down(y[0], y[1]);
            out = dc.process(out) * volS.next();
            L[i] = R[i] = (float)out;
            outRing.push((float)out);
        }
        while (e < nev) handle(ev[e++]);
        if (!std::isfinite(L[0] + L[n - 1]) || !std::isfinite(voiceCheck()))
        {
            resetVoice(); hb.reset(); dc.reset(); sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        cutFeed.store((float)cutNow, std::memory_order_relaxed);
        pitchFeed.store((float)pitch, std::memory_order_relaxed);
        gateFeed.store(gate, std::memory_order_relaxed);
        litNote.store((int)std::lround(target), std::memory_order_relaxed);
        ampFeed.store((float)ampShown, std::memory_order_relaxed);
        fenvFeed.store((float)fenvShown, std::memory_order_relaxed);
        accFeed.store((float)accCap, std::memory_order_relaxed);
        stepFeed.store(running ? curStep : -1, std::memory_order_relaxed);
    }
    void uiNote(uint8_t n, uint8_t v) { synth::Midi m { 0, (uint8_t)(v ? 0x90 : 0x80), n, v }; handle(m); }

private:
    // ---- notes (MIDI and the window's keyboard) ----
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        const bool keys = set.seqMode == kSeqKeys && !preview;
        if (st == 0x90 && m.d2 > 0)
        {
            const bool held = nKeys > 0;
            removeKey(m.d1);
            if (nKeys < 32) keyStack[nKeys++] = m.d1;
            if (keys)
            {
                keyTranspose = m.d1 - kKeyRoot;
                if (!held) { seqReset(); freePos = 0.0; }
                return;
            }
            seqOwns = false;
            const bool acc = m.d2 >= set.accentVel;
            noteOn(m.d1, acc, held && gate, m.d2 / 127.0);
        }
        else if (st == 0x80 || (st == 0x90 && m.d2 == 0))
        {
            removeKey(m.d1);
            if (keys)
            {
                if (nKeys > 0) keyTranspose = keyStack[nKeys - 1] - kKeyRoot;
                else { if (seqOwns) releaseVoice(); seqReset(); }
                return;
            }
            if (seqOwns) return;
            if ((int)std::lround(midiNote) != m.d1) return;   // not the note sounding
            if (nKeys > 0) noteOn(keyStack[nKeys - 1], accented, true, vel);   // back to the last key held
            else if (gate) releaseVoice();
        }
        else if (st == 0xE0) bend = ((m.d2 << 7 | m.d1) - 8192) / 8192.0;
        else if (st == 0xB0)
        {
            if (m.d1 == 1) wheel = m.d2 / 127.0;
            else if (m.d1 == 120 || m.d1 == 123)
            {
                nKeys = 0; releaseVoice(); seqReset();
                if (m.d1 == 120) { aEnv.kill(); fEnv.kill(); veg = 0.0; }
            }
        }
    }
    void removeKey(int n) { int k = 0; for (int i = 0; i < nKeys; ++i) if (keyStack[i] != n) keyStack[k++] = keyStack[i]; nKeys = k; }

    // start a note (slide: glide to it and keep the envelopes running)
    void noteOn(int note, bool acc, bool slide, double velocity)
    {
        midiNote = note;
        target = note;
        vel = velocity;
        accented = acc;   // a slid-into accent lifts the level (the envelopes run on)
        if (!(slide && gate))
        {
            pitch = note;
            meg = 1.0; megAtk = 0.0;
            megK = coef(acc ? std::min(set.decay, 0.2) : set.decay);
            fEnv.gate(true); aEnv.gate(true);
            vegStage = 1;
            accCharge = acc;
        }
        gate = true;
    }
    void releaseVoice() { gate = false; fEnv.gate(false); aEnv.gate(false); if (vegStage > 0) vegStage = 3; accCharge = false; }

    // ---- the sequencer ----
    void seqReset() { running = false; curStep = -1; lastIdx = LLONG_MIN; holdThrough = false; freePos = 0.0; seqGate = false; }
    void clock(double x)
    {
        const Pattern& p = set.pattern;
        const int L = clampv(p.length, 1, kSteps);
        const double odd = 1.0 + clampv(set.swing, 0.0, 0.75);   // SWING: the second step of a pair starts that much later (33 % = triplets)
        const double pair = std::floor(x * 0.5 + 1e-9);
        const double w = x - 2.0 * pair;
        const bool second = w >= odd - 1e-9;
        const long long idx = (long long)pair * 2 + (second ? 1 : 0);
        const double start = second ? odd : 0.0, dur = second ? 2.0 - odd : odd;
        const double frac = (w - start) / std::max(1e-6, dur);
        if (idx != lastIdx)
        {
            const bool contiguous = running && idx == lastIdx + 1;
            lastIdx = idx;
            running = true;
            onStep((int)(((idx % L) + L) % L), contiguous);
        }
        // the gate closes after GATE LENGTH unless the note is held into the next step
        if (seqGate && !holdThrough && frac >= set.gateLen) { seqGate = false; if (seqOwns) releaseVoice(); }
    }
    void onStep(int s, bool contiguous)
    {
        const Pattern& p = set.pattern;
        const int L = clampv(p.length, 1, kSteps);
        const Step& st = p.s[s];
        const Step& nx = p.s[(s + 1) % L];
        curStep = s;
        const bool slideIn = contiguous && holdThrough && prevSlide && seqGate;
        const int note = kBaseNote + st.note + set.transpose + (set.seqMode == kSeqKeys && !preview ? keyTranspose : 0);
        if (st.gate == kRest)
        {
            if (seqGate && seqOwns) releaseVoice();
            seqGate = false;
            holdThrough = false; prevSlide = false;
            return;
        }
        if (st.gate == kTie)
        {
            // holds the note sounding (if any); its own SLIDE glides into the next note
            if (!seqGate) { holdThrough = false; prevSlide = false; return; }
        }
        else
        {
            seqOwns = true;
            noteOn(note, st.accent, slideIn, 0.8);
            seqGate = true;
        }
        prevSlide = st.slide;
        holdThrough = (st.slide && nx.gate != kRest) || nx.gate == kTie;
    }

    // ---- control rate ----
    // a per-control-step factor that falls by 60 dB in `seconds`
    double coef(double seconds) const { return std::exp(std::log(1e-3) / std::max(1.0, seconds * sr / kCtl)); }
    void control()
    {
        const Settings& s = set;
        // glide: 95 % of the way in SLIDE TIME
        const double gk = 1.0 - std::exp(-3.0 * kCtl / std::max(1.0, s.slide * sr));
        pitch += (target - pitch) * gk;
        // LFO
        lfoPh += s.lfoRate * kCtl / sr;
        if (lfoPh >= 1.0) { lfoPh -= std::floor(lfoPh); lfoHeld = rng.bi(); }
        double lfo;
        switch (s.lfoShape)
        {
        case kLfoSquare: lfo = lfoPh < 0.5 ? 1.0 : -1.0; break;
        case kLfoSaw: lfo = 1.0 - 2.0 * lfoPh; break;
        case kLfoSh: lfo = lfoHeld; break;
        default: lfo = 1.0 - 4.0 * std::fabs(lfoPh - 0.5); break;
        }
        const double semis = pitch + s.tune + bend * s.bendRange + lfo * (s.lfoPitch * s.lfoPitch * 12.0 + wheel * 0.5);
        const double hz = synth::midiToHz(clampv(semis, -24.0, 140.0));
        inc = hz / osr;
        const double cents = s.detune;
        inc2 = inc * std::exp2(cents / 1200.0); inc3 = inc * std::exp2(-cents / 1200.0);
        pwNow = clampv(s.pw * (1.0 - s.lfoPw * 0.9 * (0.5 + 0.5 * lfo)), 0.04, 0.5);
        mip = synth::BandWaves::mipFor(inc); mip2 = synth::BandWaves::mipFor(std::max(inc, inc2));

        double cutOct = std::log2(s.cutoff) + s.keyTrack * (pitch - 48.0) / 12.0 + s.lfoCutoff * 2.0 * lfo;
        if (s.model == kAcid)
        {
            // the filter envelope: a 2 ms rise, then the decay
            megAtk = std::min(1.0, megAtk + kCtl / (0.002 * sr));
            const double env = meg * megAtk;
            meg *= megK;
            // the accent capacitor: charges from the envelope on accented notes, drains slowly
            const double src = accented && accCharge ? env : 0.0;
            const double kc = 1.0 - std::exp(-(double)kCtl / ((0.006 + 0.03 * (1.0 - s.accent)) * sr));
            const double kd = 1.0 - std::exp(-(double)kCtl / (0.32 * sr));
            accCap += (src - accCap) * (src > accCap ? kc : kd);
            // envelope mod: up to about four octaves, and the base sinks a little as it rises (the knobs interact)
            cutOct += s.envMod * (4.2 * env - 0.9) + s.accent * 2.6 * accCap;
            fenvShown = env;
            // the amp: 3 ms rise, a slow decay while held, 8 ms down after the gate
            switch (vegStage)
            {
            case 1: veg += kCtl / (0.003 * sr); if (veg >= 1.0) { veg = 1.0; vegStage = 2; } break;
            case 2: veg *= std::exp(-(double)kCtl / (2.6 * sr)); break;
            case 3: veg *= std::exp(-(double)kCtl / (0.006 * sr)); if (veg < 1e-5) { veg = 0.0; vegStage = 0; } break;
            default: veg = 0.0; break;
            }
            ampK = veg * (1.0 + s.accent * (accented ? 0.9 * (0.4 + 0.6 * accCap) : 0.0));
            ampShown = veg;
        }
        else
        {
            aEnv.set(s.attack, s.decay, s.sustain, s.release);
            fEnv.set(s.attack, s.decay, s.sustain, s.release);
            const double fe = fEnv.level();
            const double accBoost = accented ? 1.0 + 0.8 * s.accent : 1.0;
            cutOct += s.envMod * 6.0 * fe * accBoost;
            fenvShown = fe;
            ampShown = aEnv.level();
            ampAcc = accented ? 1.0 + 0.7 * s.accent : 1.0;
        }
        cutNow = clampv(std::exp2(cutOct), 20.0, 0.45 * osr);
        switch (s.model)
        {
        case kAcid: diode.set(cutNow, s.res, osr); break;
        case kSolo: ladder.set(cutNow, s.res * 0.98, osr); break;
        case kPocket: svf.set(cutNow, s.res, osr); break;
        case kStation: ladder.set(cutNow, s.res * 0.95, osr); break;
        default: hpS.set(18.0 + cutNow * 0.004, osr); scream.set(cutNow, s.res, osr); break;
        }
        driveG = 1.0 + s.drive * s.drive * 24.0;
    }

    // ---- one sample at the oversampled rate ----
    inline double voiceSample()
    {
        const Settings& s = set;
        const bool acidM = s.model == kAcid;
        const bool active = acidM ? vegStage != 0 : aEnv.active();
        if (!active) { if (!acidM) { aEnv.next(); fEnv.next(); } return 0.0; }
        const synth::BandWaves& bw = synth::BandWaves::get();
        const double w = s.wave;
        double x;
        const double saw = -bw.saw(ph, mip);   // falling, as the original's
        if (acidM)
        {
            // the square a transistor makes of the saw: a pulse a little under half, its top leaning with the ramp,
            // its edges softened
            const double sq = 0.85 * bw.pulse(ph, 0.46, mip) + 0.15 * saw;
            sqLp += (sq - sqLp) * 0.55;
            x = saw * (1.0 - w) + sqLp * w;
        }
        else
        {
            const double pulse = bw.pulse(ph, s.model == kSolo ? pwNow : 0.5 + (pwNow - 0.5) * 0.5, mip);
            x = saw * (1.0 - w) + pulse * w;
            if (s.model == kPocket || s.model == kStation)
            {
                const double a = bw.saw(ph2, mip2), b = bw.saw(ph3, mip2);
                const double pa = bw.pulse(ph2, 0.5, mip2), pb = bw.pulse(ph3, 0.5, mip2);
                const double o2 = -a * (1.0 - w) + pa * w, o3 = -b * (1.0 - w) + pb * w;
                x += s.osc2 * (s.model == kPocket ? 0.8 * (o2 + o3) : 0.9 * o2);
            }
            if (s.sub > 0.0) x += s.sub * 0.9 * bw.pulse(phSub, 0.5, std::max(0, mip - 1));   // a square an octave down
            if (s.noise > 0.0) x += s.noise * 0.8 * rng.bi();
        }
        ph += inc; if (ph >= 1.0) ph -= 1.0;
        phSub += 0.5 * inc; if (phSub >= 1.0) phSub -= 1.0;
        ph2 += inc2; if (ph2 >= 1.0) ph2 -= 1.0;
        ph3 += inc3; if (ph3 >= 1.0) ph3 -= 1.0;
        x += 1e-4 * rng.bi();   // a whisper of noise, as the analog circuits have (it starts a resonating filter)
        double y;
        switch (s.model)
        {
        // (the output gains put every model's default line near the same loudness)
        case kAcid: y = diode.process(0.55 * x) * 1.6; break;
        case kSolo: y = ladder.process(0.3 * x, s.slope, 1.0) * 2.5; break;
        case kPocket: y = svf.process(std::tanh(0.6 * x)) * 0.87; break;
        case kStation: y = ladder.process(0.3 * std::tanh(1.8 * x), s.slope, 1.6) * 1.6; break;
        default: { const double h = hpS.hp(x); y = scream.process(std::tanh(0.7 * h)) * 0.85; break; }
        }
        double a;
        if (acidM) a = ampK;
        else { a = aEnv.next() * ampAcc; fEnv.next(); }
        y *= a;
        if (driveG > 1.0001) y = (std::tanh(driveG * y + 0.15) - std::tanh(0.15)) / std::sqrt(driveG) * 1.2;
        return y;
    }
    double voiceCheck() const { return pitch + cutNow; }
    void resetVoice()
    {
        diode.reset(); ladder.reset(); svf.reset(); scream.reset(); hpS.reset();
        aEnv.kill(); fEnv.kill();
        gate = false; vegStage = 0; veg = 0.0; meg = 0.0; accCap = 0.0; sqLp = 0.0;
        ph = ph2 = ph3 = phSub = 0.0;
        pitch = target = midiNote = 36.0;
        nKeys = 0; seqOwns = false;
    }

    double sr = 48000.0, osr = 96000.0;
    Settings set;
    Transport tr;
    HalfbandStage hb;
    DiodeLadder diode;
    synth::Ladder ladder;
    SoftSvf svf;
    ScreamLp scream;
    fx::OnePole hpS;
    fx::DcBlocker dc;
    synth::Adsr aEnv, fEnv;
    fx::Rng rng { 0xAC1Du };
    LinearSmoother volS;
    double ph = 0, ph2 = 0, ph3 = 0, phSub = 0, inc = 0.001, inc2 = 0.001, inc3 = 0.001, pwNow = 0.5, sqLp = 0.0;
    int mip = 0, mip2 = 0;
    double pitch = 36.0, target = 36.0, midiNote = 36.0, vel = 0.8, bend = 0.0, wheel = 0.0;
    bool gate = false, accented = false, accCharge = false;
    double meg = 0.0, megK = 0.999, megAtk = 1.0, accCap = 0.0, veg = 0.0, ampK = 0.0, ampAcc = 1.0;
    int vegStage = 0;
    double cutNow = 420.0, driveG = 1.0, fenvShown = 0.0, ampShown = 0.0;
    double lfoPh = 0.0, lfoHeld = 0.0;
    int keyStack[32] = {}, nKeys = 0, keyTranspose = 0;
    // sequencer
    bool preview = false, running = false, seqGate = false, holdThrough = false, prevSlide = false, seqOwns = false;
    int curStep = -1, lastMode = -1;
    long long lastIdx = 0;
    double freePos = 0.0;
    unsigned ctl = 0;
    std::atomic<float> cutFeed { 420.0f }, pitchFeed { 36.0f }, ampFeed { 0.0f }, fenvFeed { 0.0f }, accFeed { 0.0f };
    std::atomic<bool> gateFeed { false };
    std::atomic<int> litNote { -1 }, stepFeed { -1 };
};

}} // namespace pump::acid
