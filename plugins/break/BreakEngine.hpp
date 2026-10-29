// pump BREAK engine: a break cut into sixteen slices, played by a sixteen-step sequencer with a pitch and a volume
// envelope per step, and eight momentary effects. Framework-free.
//
// A step with a slice starts a voice at the slice's start (REVERSE: at its end, going back); the voice before fades
// out in 3 ms. A voice plays on through the break (wrapping round it) until the next one takes over, so a step with no
// slice lets the one before continue. Its rate: the break's own rate, times the song's tempo over the break's (FIT),
// times the pitch: PITCH + TRANSPOSE semitones, moving by BEND semitones across the step (the pitch envelope). Its level:
// LEVEL, falling with DECAY (the volume envelope: from about a twentieth of a step to holding; 100 % holds).
//
// The effects, while held (from the window, the computer keys 1-8, MIDI notes on the white keys up from C-2, or the
// host parameters FX n Hold):
//   STUTTER    the voice playing jumps back to where it started, every 1 to 1/8 step (AMOUNT: shorter)
//   REPEAT     the output from the moment it is pressed loops, a quarter note to a thirty-second long (AMOUNT: shorter)
//   REVERSE    every voice plays backwards
//   TAPE STOP  the playback slows to a stop over 0.15 to 2 s (AMOUNT: slower), no new steps; let go: it spins back up
//   FILTER     a resonant low-pass sweeps down (AMOUNT: how far), and back up after
//   HIGH-PASS  a resonant high-pass sweeps up, and back down after
//   CRUSH      fewer bits and a lower rate (AMOUNT: more)
//   ECHO       the output feeds a dotted-eighth echo (AMOUNT: feedback); the tail rings on after
// Then TONE (a low-pass), DRIVE, VOLUME.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Tempo.hpp"
#include "BreakParams.hpp"
#include "BreakSounds.hpp"
#include <array>
#include <climits>

namespace pump { namespace brk {

struct Settings {
    bool fit = true;
    int fileBars = 1, seqMode = kSeqSong, length = kSteps, rate = 2;
    double transpose = 0.0, tone = 1.0, drive = 0.0, volumeDb = 0.0, swing = 0.0;
    double fxAmount[kFx] = { 0.5, 0.5, 1.0, 0.5, 0.7, 0.6, 0.6, 0.5 };
    bool fxHold[kFx] = {};
    Pattern pattern = parseChop(kStraight);
};

class Engine {
public:
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice();
        rep.assign((size_t)(sr * 4.0) + 8, 0.0f); repR.assign(rep.size(), 0.0f);
        echoL.assign((size_t)(sr * 3.0) + 8, 0.0f); echoR.assign(echoL.size(), 0.0f); ew = 0;
        lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); toneL.reset(); toneR.reset(); crushL.reset(); crushR.reset();
        lpHz = 20000.0; hpHz = 20.0; tape = 1.0; repOn = false;
        for (bool& m : midiHeld) m = false;
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        seqReset();
    }
    void setSettings(const Settings& s) { set = s; }
    void setTransport(const Transport& t) { tr = t; }
    void setPreview(bool on) { preview = on; }
    void setSource(const Source* s) { src = s; }
    // held from outside (the window, the keys): combined with the parameters and the MIDI notes
    void setHeld(int k, bool on) { if (k >= 0 && k < kFx) uiHeld[k] = on; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int stepNow() const { return stepFeed.load(std::memory_order_relaxed); }
    float playPos() const { return posFeed.load(std::memory_order_relaxed); }   // 0..1 through the break, < 0 = silent
    int sliceNow() const { return sliceFeed.load(std::memory_order_relaxed); }
    bool fxOn(int k) const { return (fxFeed.load(std::memory_order_relaxed) >> k) & 1u; }

    // play a slice now (the window, MIDI), 0-based
    void playSlice(int slice, double level)
    {
        Step st; st.slice = slice + 1; st.level = level; st.decay = 1.0;
        trigger(st, stepSamples());
    }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        const int mode = preview ? kSeqFree : set.seqMode;
        if (mode != lastMode) { seqReset(); lastMode = mode; }
        const bool locked = tr.playing && tr.valid && std::isfinite(tr.ppq) && std::fabs(tr.ppq) < 1e9;
        const double spq = stepsPerQuarter(set.rate);
        const double stepsPerSample = clampv(tr.bpm, 20.0, 999.0) / 60.0 / sr * spq;
        if (mode == kSeqSong && !locked && running) { seqReset(); for (auto& v : voices) if (v.active) v.fadeOut = true; }
        // the effects held now
        unsigned held = 0;
        for (int k = 0; k < kFx; ++k) if (set.fxHold[k] || uiHeld[k] || midiHeld[k]) held |= 1u << k;
        const double fitR = fitRatio();
        // effect settings for this block
        const double a[kFx] = { set.fxAmount[0], set.fxAmount[1], set.fxAmount[2], set.fxAmount[3], set.fxAmount[4], set.fxAmount[5], set.fxAmount[6], set.fxAmount[7] };
        const double tapeStep = 1.0 / std::max(1.0, (0.15 + 1.85 * a[kTapeStop]) * sr), spinUp = 1.0 / (0.06 * sr);
        const double lpTarget = (held >> kFilter) & 1u ? 20000.0 * std::exp2(-a[kFilter] * 7.0) : 20000.0;
        const double hpTarget = (held >> kHighpass) & 1u ? 25.0 * std::exp2(a[kHighpass] * 7.0) : 20.0;
        const double lpK = 1.0 - std::exp(-1.0 / (((held >> kFilter) & 1u ? 0.35 : 0.12) * sr));
        const double hpK = 1.0 - std::exp(-1.0 / (((held >> kHighpass) & 1u ? 0.35 : 0.12) * sr));
        const bool crush = (held >> kCrush) & 1u;
        if (crush) { crushL.set(1.0 / (1.0 + a[kCrush] * 15.0), 12.0 - a[kCrush] * 9.0); crushR.set(1.0 / (1.0 + a[kCrush] * 15.0), 12.0 - a[kCrush] * 9.0); }
        const double echoD = clampv(0.75 * 60.0 / clampv(tr.bpm, 20.0, 999.0) * sr, 10.0, (double)echoL.size() - 4.0), echoFb = 0.3 + 0.55 * a[kEcho];
        const double toneHz = 300.0 * std::exp2(set.tone * 6.1);
        toneL.set(std::min(toneHz, 0.45 * sr), sr); toneR.set(std::min(toneHz, 0.45 * sr), sr);
        const double dg = 1.0 + set.drive * set.drive * 12.0, dn = 1.0 / std::tanh(dg);
        // REPEAT: starts capturing on the press, loops after
        const bool repHeld = (held >> kRepeat) & 1u;
        if (repHeld && !repOn)
        {
            static const double q[4] = { 1.0, 0.5, 0.25, 0.125 };
            repLen = std::max(64, (int)(q[clampv((int)(a[kRepeat] * 3.999), 0, 3)] * 60.0 / clampv(tr.bpm, 20.0, 999.0) * sr));
            repLen = std::min(repLen, (int)rep.size() - 4);
            repOn = true; repW = 0; repRead = 0; repFull = false;
        }
        if (!repHeld) repOn = false;
        // STUTTER: every 1 .. 1/8 step back to where the voice started
        const bool stut = (held >> kStutter) & 1u;
        static const double stutDiv[6] = { 1.0, 2.0, 3.0, 4.0, 6.0, 8.0 };
        const int stutLen = std::max(32, (int)(stepSamples() / stutDiv[clampv((int)(a[kStutter] * 5.999), 0, 5)]));
        if (!stut) stutCount = 0;
        const bool rev = (held >> kReverse) & 1u;
        const bool tapeHeld = (held >> kTapeStop) & 1u;
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            // the step clock
            tapeHeldNow = tapeHeld;
            if (mode == kSeqSong && locked) clock(tr.ppq * spq + i * stepsPerSample);
            else if (mode == kSeqFree) { if (locked) clock(tr.ppq * spq + i * stepsPerSample); else { clock(freePos); freePos += stepsPerSample; } }
            // tape stop / spin up
            if (tapeHeld) tape = std::max(0.0, tape - tapeStep); else tape = std::min(1.0, tape + spinUp);
            // stutter
            if (stut && ++stutCount >= stutLen) { stutCount = 0; restutter(); }
            // the voices
            double l = 0.0, r = 0.0;
            for (auto& v : voices) if (v.active) voiceSample(v, l, r, fitR, rev);
            // REPEAT: capture, then loop
            if (repOn)
            {
                if (!repFull) { rep[(size_t)repW] = (float)l; repR[(size_t)repW] = (float)r; if (++repW >= repLen) repFull = true; }
                else
                {
                    // a 2 ms fade at the loop's seam
                    const int fadeN = std::max(1, (int)(0.002 * sr));
                    double g = 1.0; if (repRead < fadeN) g = (double)repRead / fadeN; else if (repRead > repLen - fadeN) g = (double)(repLen - repRead) / fadeN;
                    l = rep[(size_t)repRead] * g; r = repR[(size_t)repRead] * g;
                    if (++repRead >= repLen) repRead = 0;
                }
            }
            // filter sweeps (state-variable, resonant); out of the way when open
            lpHz += (lpTarget - lpHz) * lpK; hpHz += (hpTarget - hpHz) * hpK;
            if (lpHz < 19000.0) { if ((ctl & 15) == 0) { lpL.set(lpHz, 2.2, sr); lpR.set(lpHz, 2.2, sr); } l = lpL.lowpass(l); r = lpR.lowpass(r); }
            if (hpHz > 22.0) { if ((ctl & 15) == 0) { hpL.set(hpHz, 2.2, sr); hpR.set(hpHz, 2.2, sr); } l = hpL.highpass(l); r = hpR.highpass(r); }
            ++ctl;
            if (crush) { l = crushL.process(l); r = crushR.process(r); }
            // echo: fed while held, always heard
            {
                const int d = (int)echoD, N = (int)echoL.size();
                int ri = ew - d; if (ri < 0) ri += N;
                const double el = echoL[(size_t)ri], er = echoR[(size_t)ri];
                const double send = ((held >> kEcho) & 1u) ? 1.0 : 0.0;
                echoL[(size_t)ew] = (float)(l * send + er * echoFb);   // ping-pong
                echoR[(size_t)ew] = (float)(r * send + el * echoFb);
                ew = (ew + 1) % N;
                l += el * 0.7; r += er * 0.7;
            }
            if (set.tone < 0.999) { l = toneL.lp(l); r = toneR.lp(r); }
            if (set.drive > 0.001) { l = std::tanh(l * dg) * dn; r = std::tanh(r * dg) * dn; }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            for (auto& v : voices) v.active = false;
            lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); std::fill(echoL.begin(), echoL.end(), 0.0f); std::fill(echoR.begin(), echoR.end(), 0.0f);
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
        stepFeed.store(running ? curStep : -1, std::memory_order_relaxed);
        const Voice* nv = newest();
        posFeed.store(nv && src && src->size() ? (float)(nv->pos / (double)src->size()) : -1.0f, std::memory_order_relaxed);
        sliceFeed.store(nv ? nv->slice : -1, std::memory_order_relaxed);
        fxFeed.store(held, std::memory_order_relaxed);
    }

private:
    struct Voice {
        bool active = false, fadeOut = false;
        int slice = 0;
        uint64_t order = 0;
        double pos = 0.0, start = 0.0, dir = 1.0, t = 0.0, stepLen = 1.0, amp = 1.0, ampK = 1.0, level = 1.0, fade = 1.0, atk = 0.0;
        double pitch = 0.0, bend = 0.0;
        bool reverse = false;
    };

    double srcBpm() const
    {
        if (!src || src->size() == 0) return 120.0;
        if (src->bars > 0) return src->bpm;
        // a file: its length is FILE LENGTH bars
        static const int bars[3] = { 1, 2, 4 };
        return 240.0 * bars[clampv(set.fileBars, 0, 2)] / (src->size() / src->sr);
    }
    double fitRatio() const { return set.fit ? clampv(clampv(tr.bpm, 20.0, 999.0) / srcBpm(), 0.25, 4.0) : 1.0; }
    double stepSamples() const { return 60.0 / clampv(tr.bpm, 20.0, 999.0) / stepsPerQuarter(set.rate) * sr; }
    const Voice* newest() const
    {
        const Voice* b = nullptr;
        for (const auto& v : voices) if (v.active && !v.fadeOut && (!b || v.order > b->order)) b = &v;
        return b;
    }

    // ---- MIDI: slices on the white keys up from C-4, the effects on the white keys up from C-2 ----
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        const bool on = st == 0x90 && m.d2 > 0, off = st == 0x80 || (st == 0x90 && m.d2 == 0);
        if (!on && !off)
        {
            if (st == 0xB0 && (m.d1 == 120 || m.d1 == 123)) { for (auto& v : voices) if (v.active) v.fadeOut = true; for (bool& h : midiHeld) h = false; }
            return;
        }
        for (int k = 0; k < kFx; ++k) if (fxNote(k) == m.d1) { midiHeld[k] = on; return; }
        if (!on) return;
        const int wi = whiteKeyIndex(m.d1);
        if (wi >= 0 && wi < kSlices) playSlice(wi, m.d2 / 127.0);
    }

    // ---- the sequencer ----
    void seqReset() { running = false; curStep = -1; lastIdx = LLONG_MIN; freePos = 0.0; }
    void clock(double x)
    {
        const int L = clampv(set.length, 1, kSteps);
        const double odd = 1.0 + clampv(set.swing, 0.0, 0.75);
        const double pair = std::floor(x * 0.5 + 1e-9);
        const double w = x - 2.0 * pair;
        const long long idx = (long long)pair * 2 + (w >= odd - 1e-9 ? 1 : 0);
        if (idx == lastIdx) return;
        lastIdx = idx;
        running = true;
        curStep = (int)(((idx % L) + L) % L);
        const Step& st = set.pattern.s[curStep];
        if (st.slice > 0 && !tapeHeldNow) trigger(st, stepSamples() * ((idx & 1) ? 2.0 - odd : odd));
    }

    void trigger(const Step& st, double stepLen)
    {
        if (!src || src->size() < 64) return;
        // the oldest (or a free) voice takes it; every other one fades
        Voice* slot = nullptr;
        for (auto& v : voices) { if (v.active) v.fadeOut = true; }
        for (auto& v : voices) if (!v.active) { slot = &v; break; }
        if (!slot) { slot = &voices[0]; for (auto& v : voices) if (v.order < slot->order) slot = &v; }
        Voice& v = *slot;
        v = Voice();
        v.active = true; v.order = ++orders; v.slice = clampv(st.slice - 1, 0, kSlices - 1);
        const double sl = (double)src->size() / kSlices;
        v.reverse = st.reverse;
        v.start = st.reverse ? (v.slice + 1) * sl - 1.0 : v.slice * sl;
        v.pos = v.start;
        v.stepLen = std::max(1.0, stepLen);
        v.level = clampv(st.level, 0.0, 1.5);
        v.pitch = st.pitch; v.bend = st.bend;
        v.ampK = st.decay >= 0.995 ? 1.0 : std::exp(-1.0 / (v.stepLen * 0.05 * std::pow(40.0, clampv(st.decay, 0.0, 1.0))));
        v.amp = 1.0; v.atk = 0.0;
    }
    void restutter()
    {
        Voice* v = const_cast<Voice*>(newest());
        if (!v) return;
        // a fresh voice at the same place (the old one fades), so the jump doesn't click
        Step st; st.slice = v->slice + 1; st.pitch = v->pitch; st.bend = v->bend; st.level = v->level; st.reverse = v->reverse; st.decay = 1.0;
        const double sl = v->stepLen;
        trigger(st, sl);
    }

    inline void voiceSample(Voice& v, double& l, double& r, double fitR, bool rev)
    {
        const double semis = set.transpose + v.pitch + v.bend * std::min(1.0, v.t / v.stepLen);
        const double rate = src->sr / sr * fitR * std::exp2(semis / 12.0) * tape;
        const double dir = (v.reverse != rev) ? -1.0 : 1.0;
        const double N = (double)src->size();
        // cubic read, wrapping round the break
        const double p = v.pos;
        const long i1 = (long)std::floor(p);
        const double f = p - i1;
        auto at = [&](const std::vector<float>& b, long k) { k %= (long)N; if (k < 0) k += (long)N; return (double)b[(size_t)k]; };
        auto cubic = [&](const std::vector<float>& b) {
            const double y0 = at(b, i1 - 1), y1 = at(b, i1), y2 = at(b, i1 + 1), y3 = at(b, i1 + 2);
            const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        };
        if (v.atk < 1.0) v.atk = std::min(1.0, v.atk + 1.0 / (0.0015 * sr));
        double g = v.level * v.amp * v.atk;
        if (v.fadeOut) { v.fade -= 1.0 / (0.003 * sr); if (v.fade <= 0.0) { v.active = false; return; } g *= v.fade; }
        l += cubic(src->L) * g; r += cubic(src->R) * g;
        v.pos += dir * rate;
        if (v.pos >= N) v.pos -= N; else if (v.pos < 0.0) v.pos += N;
        v.amp *= v.ampK;
        v.t += 1.0;
        if (v.amp < 1e-4) v.active = false;
    }

    double sr = 48000.0;
    Settings set;
    Transport tr;
    const Source* src = nullptr;
    std::array<Voice, 3> voices {};
    uint64_t orders = 0;
    bool preview = false, running = false, tapeHeldNow = false;
    bool uiHeld[kFx] = {}, midiHeld[kFx] = {};
    int curStep = -1, lastMode = -1;
    long long lastIdx = LLONG_MIN;
    double freePos = 0.0, tape = 1.0, lpHz = 20000.0, hpHz = 20.0;
    std::vector<float> rep, repR, echoL, echoR;
    int repLen = 1, repW = 0, repRead = 0, ew = 0, stutCount = 0;
    bool repOn = false, repFull = false;
    unsigned ctl = 0;
    fx::Svf lpL, lpR, hpL, hpR;
    fx::OnePole toneL, toneR;
    fx::Crusher crushL, crushR;
    LinearSmoother volS;
    std::atomic<int> stepFeed { -1 }, sliceFeed { -1 };
    std::atomic<float> posFeed { -1.0f };
    std::atomic<unsigned> fxFeed { 0u };
};

}} // namespace pump::brk
