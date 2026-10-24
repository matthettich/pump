// pump TUNE engine: pitch correction, harmonies and formants for a solo voice. Framework-free.
//
// Pitch: pitch::Tracker (YIN) at ~24 kHz analysis, one estimate every ~5 ms, limits from RANGE (High 100-1200 Hz,
// Mid 70-900, Low 50-600).
//
// Shifting: pitch-synchronous overlap-add (PSOLA). Marks are placed one period apart on the input, each moved to the
// strongest peak of a low-passed copy within a quarter period of where it was expected, so every mark sits at the
// same point of its cycle. A voice (the lead and up to four harmonies) lays Hann grains of two input periods, each
// centred on the input mark nearest its own synthesis time, one *output* period apart: closer for a higher pitch,
// further for a lower one. Each grain keeps the shape of one glottal cycle, so the formants stay where they were
// whatever the pitch (FORMANTS FOLLOW off). FORMANT plays each grain faster or slower (shorter or longer at the
// output; its spectrum moves up or down) for a formant shift on its own; with FORMANTS FOLLOW the formants move with
// the pitch (the chipmunk / giant sound).
// Unvoiced sounds are passed through by the same grains at the input's own rate (a period of 5 ms, no shift).
// Latency: 1.25 x the longest period (17-27 ms by RANGE) plus 64 samples.
//
// Correction (MODE Live): the note to sing is the nearest one in KEY / SCALE (a voice wavering near the middle
// between two notes keeps the one it had), or with MIDI = Lead notes the held note nearest the voice. The
// correction moves toward it in RETUNE ms (0 = at once: flat, stepped notes). HUMANIZE corrects only the note's
// centre (the voice's pitch smoothed at ~5 Hz) and lets vibrato and small movements through; FLEX leaves notes within
// that many cents alone; AMOUNT scales the whole correction. VIBRATO adds a sine vibrato (DEPTH cents at RATE, faded
// in over 250 ms after each new note); TRANSPOSE moves everything by semitones.
// MODE Graph: the notes captured and edited in the window. Inside a note the voice moves by the note's edit (its new
// pitch minus where it was sung), STRAIGHTEN pulls its drift toward the centre, VIBRATO scales the voice's own
// vibrato, and the note's FORMANT and GAIN apply; transitions between notes take ~30 ms; outside notes it passes
// through. While CAPTURE is on and the host plays, the tracker's estimates go to the UI with the timeline position.
//
// Harmonies (HARMONY): up to four more voices, Scale (scale steps above or below the corrected note: 2 = a third,
// 4 = a fifth) or Fixed (semitones), each with LEVEL and PAN; or with MIDI = Harmony notes the held notes. They
// follow the lead's corrected pitch (its vibrato and slides), with their own formant shift and HUMANIZE (each voice
// drifts by a few cents and a few ms, like separate singers).
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Pitch.hpp"
#include "TuneParams.hpp"
#include <atomic>
#include <memory>

namespace pump { namespace tune {

struct Settings {
    int mode = kLive, key = 0, scale = pitch::kChromatic, midi = kMidiOff, range = kRangeMid;
    double retuneMs = 20.0, amount = 100.0, humanize = 0.0, flexCents = 0.0, vibDepth = 0.0, vibRate = 5.5;
    int transpose = 0;
    double formant = 0.0;
    bool formantFollow = false;
    int harmMode = kHarmOff;
    int interval[kVoices] = { 2, -3, 4, 7 };
    double level[kVoices] = { 70, 60, 0, 0 }, pan[kVoices] = { -45, 45, -80, 80 };
    double harmFormant = 0.0, harmHumanize = 30.0, lead = 100.0, harmLevelDb = 0.0, outDb = 0.0;
};

struct MidiMsg { uint32_t frame; uint8_t status, d1, d2; };

// A note on the graph (times in seconds on the host's timeline). src = where it was sung (its centre, MIDI),
// dst = where it should be. straighten 0..1, vibrato 0..2 (x the voice's own), formant semitones, gain dB.
struct GraphNote {
    double t0 = 0.0, t1 = 0.0;
    float src = 60.0f, dst = 60.0f, straighten = 0.0f, vibrato = 1.0f, formant = 0.0f, gainDb = 0.0f;
};
struct NoteList { std::vector<GraphNote> notes; };   // sorted by t0, not overlapping

// One analysis frame sent to the UI while capturing.
struct CaptureFrame { double t; float midi; float levelDb; };

// ---- PSOLA ------------------------------------------------------------------------------------------------------------
class Psola {
public:
    static constexpr int kMaxVoices = 1 + kVoices;

    void prepare(double sampleRate, double maxPeriodSamples)
    {
        sr = sampleRate;
        Pmax = maxPeriodSamples;
        Pu = std::max(16.0, sr / 200.0);
        latency = (int)std::ceil(1.25 * Pmax) + 64;
        int size = 1;
        while (size < (int)(4.0 * Pmax + latency + 4096)) size <<= 1;
        buf.assign((size_t)size, 0.0f); lp.assign((size_t)size, 0.0f);
        for (int v = 0; v < kMaxVoices; ++v) acc[v].assign((size_t)size, 0.0f);
        mask = size - 1;
        lpC = std::exp(-kTwoPi * 900.0 / sr);
        reset();
    }
    void reset()
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        std::fill(lp.begin(), lp.end(), 0.0f);
        for (auto& a : acc) std::fill(a.begin(), a.end(), 0.0f);
        n = 0; lpState = 0.0;
        marks = 0; lastMark = -(int64_t)Pu;
        for (auto& v : voice) v = VoiceState();
    }
    int latencySamples() const { return latency; }
    int64_t inputCount() const { return n; }

    // Write one input sample with the current period estimate (0 = unvoiced); places marks.
    inline void push(float x, double period)
    {
        const int64_t i = n++;
        buf[(size_t)(i & mask)] = x;
        lpState = x + lpC * (lpState - x);
        lp[(size_t)(i & mask)] = (float)lpState;
        const bool voiced = period > 0.0;
        const double P = voiced ? clampv(period, 8.0, Pmax) : Pu;
        // a mark one period after the last, moved to the low-passed peak within +-P/4 (+-P/2 when voicing begins)
        const double reach = voiced ? (lastVoiced ? 0.25 * P : 0.5 * P) : 0.0;
        while ((double)lastMark + P + reach < (double)(i - 1))
        {
            int64_t c = lastMark + (int64_t)std::lround(P);
            if (voiced && reach > 0.0)
            {
                const int64_t a = std::max(lastMark + (int64_t)(0.5 * P), c - (int64_t)reach), b = c + (int64_t)reach;
                int64_t best = c; float bv = -1e30f;
                for (int64_t k = a; k <= b; ++k) { const float v = lp[(size_t)(k & mask)]; if (v > bv) { bv = v; best = k; } }
                c = best;
            }
            const int s = (int)(marks % kMarkRing);
            markPos[s] = c; markPer[s] = P; markVoiced[s] = voiced;
            ++marks;
            lastMark = c;
            lastVoiced = voiced;
        }
        if (!voiced) lastVoiced = false;
    }

    // Per output sample: run every voice (shift in semitones, formant factor, gain, timing offset in samples),
    // returns each voice's output (one sample, at the latency).
    struct VoiceParams { bool on = false; double shiftSt = 0.0, formant = 1.0, gain = 1.0, offset = 0.0; bool follow = false; };
    inline void render(const VoiceParams* vp, int numVoices, float* out)
    {
        const int64_t tOut = n - latency;
        for (int v = 0; v < numVoices; ++v)
        {
            VoiceState& vs = voice[v];
            float y = 0.0f;
            if (vp[v].on && marks > 0)
            {
                if (!vs.active) { vs.active = true; vs.ts = (double)tOut + 1.0; }
                // lay grains until the next one starts beyond this sample
                int guard = 0;
                while (vs.ts - vs.half <= (double)tOut + 1.0 && guard++ < 8) addGrain(v, vp[v]);
                const size_t k = (size_t)(tOut & mask);
                y = acc[v][k];
                acc[v][k] = 0.0f;
            }
            else
            {
                if (vs.active) { vs.active = false; std::fill(acc[v].begin(), acc[v].end(), 0.0f); }
            }
            out[v] = y;
        }
    }

private:
    struct VoiceState { bool active = false; double ts = 0.0, half = 0.0; };

    // the mark nearest time t whose grain (source half-width `need`) is already in the buffer
    int nearestMark(double t, double fr) const
    {
        int best = -1; double bd = 1e300;
        const int64_t lo = std::max<int64_t>(0, marks - kMarkRing + 1);
        for (int64_t m = marks - 1; m >= lo && m >= marks - 96; --m)
        {
            const int s = (int)(m % kMarkRing);
            if ((double)markPos[s] + markPer[s] * std::max(1.0, fr) + 2.0 > (double)(n - 1)) continue;
            const double d = std::fabs((double)markPos[s] - t);
            if (d < bd) { bd = d; best = s; }
            else if ((double)markPos[s] < t) break;
        }
        return best;
    }
    void addGrain(int v, const VoiceParams& p)
    {
        VoiceState& vs = voice[v];
        const int s = nearestMark(vs.ts - p.offset, p.formant);
        if (s < 0) { vs.ts += Pu; vs.half = Pu; return; }
        const bool voiced = markVoiced[s];
        const double Pa = markPer[s];
        const double ratio = voiced ? std::exp2(p.shiftSt / 12.0) : 1.0;
        // formants: the grain read faster (up) or slower (down); following the pitch = read at the pitch ratio
        const double fr = clampv(p.follow && voiced ? ratio * p.formant : p.formant, 0.25, 4.0);
        // the grain always spans two input periods (one cycle's shape); read at fr it lasts 2 Pa / fr at the output
        const int H = std::max(4, (int)std::lround(Pa / fr));
        const double M = (double)markPos[s];
        // energy: a pitch shift lays ratio x as many grains per second, a formant shift up makes them shorter
        const float g = (float)(p.gain * std::sqrt(std::max(1.0, fr)) / std::sqrt(std::max(0.25, ratio)));
        const int64_t c = (int64_t)std::lround(vs.ts);
        const int64_t tOut = n - latency;
        for (int i = -H; i < H; ++i)
        {
            const int64_t t = c + i;
            if (t < tOut) continue;   // already played (the very first grain only)
            const double w = 0.5 + 0.5 * std::cos(kPi * i / H);
            const double src = M + i * fr;
            const int64_t si = (int64_t)std::floor(src);
            const float f = (float)(src - (double)si);
            const float a = buf[(size_t)(si & mask)], b = buf[(size_t)((si + 1) & mask)];
            acc[v][(size_t)(t & mask)] += (float)w * (a + (b - a) * f) * g;
        }
        vs.half = H;
        vs.ts += voiced ? Pa / ratio : Pa;
    }

    static constexpr int kMarkRing = 4096;
    double sr = 48000.0, Pmax = 960.0, Pu = 240.0, lpC = 0.9, lpState = 0.0;
    int latency = 1264, mask = 0;
    std::vector<float> buf, lp, acc[kMaxVoices];
    int64_t n = 0, marks = 0, lastMark = 0;
    bool lastVoiced = false;
    int64_t markPos[kMarkRing] = {};
    double markPer[kMarkRing] = {};
    bool markVoiced[kMarkRing] = {};
    VoiceState voice[kMaxVoices];
};

// ---- the engine ----------------------------------------------------------------------------------------------------------
class Engine {
public:
    // a control snapshot: the lead's shift, the graph note's formant and gain, the harmonies' offsets from the lead
    // (-1000 = silent), stamped with the input time they describe
    struct CtlSnap { double time = -1e18, lead = 0.0, formant = 0.0, gain = 1.0; double harm[kVoices] = { -1000.0, -1000.0, -1000.0, -1000.0 }; };

    Engine()
    {
        for (auto& b : heldBits) b.store(0u);
        for (auto& h : harmNoteNow) h.store(-1.0f);
    }
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        // allocate for the low range (the longest periods) first, so a later range change only reuses the buffers
        const int r = cur.range;
        cur.range = kRangeLow; configure();
        cur.range = r; configure();
        // humanize: advanced once per control hop (~5.3 ms)
        for (int v = 0; v < kVoices; ++v)
        {
            drift[v].setup(1.0 / 0.0053, 0x1000u + (uint32_t)v * 7919u); drift[v].setRate(0.7 + 0.23 * v);
            lag[v].setup(1.0 / 0.0053, 0x2000u + (uint32_t)v * 104729u); lag[v].setRate(0.4 + 0.17 * v);
        }
        reset();
    }
    void reset()
    {
        tracker.reset(); psola.reset();
        for (auto& h : held) h = 0;
        heldOrder = 0;
        jumpCand = -100.0; holdLast = false;
        period = 0.0; midiNow = -1.0; midiLp = -1.0; midiLp1 = -1.0; ctlW = 0; targetNote = -1; correction = 0.0; vibPhase = 0.0; noteAge = 0.0;
        graphShift = 0.0; graphGain = 1.0; graphFormant = 0.0;
        for (auto& s : sm) s = 0.0;
        voicedNow.store(false);
    }
    void setSettings(const Settings& s)
    {
        const bool reconf = s.range != cur.range;
        cur = s;
        if (reconf && sr > 0.0) { configure(); psola.reset(); tracker.reset(); }
    }
    int latency() const { return psola.latencySamples(); }
    static int latencyFor(double sampleRate, int range) { return (int)std::ceil(1.25 * sampleRate / rangeMinHz(range)) + 64; }

    // Graph notes from the UI (set from the main thread; swapped in at the next block)
    void setNotes(std::shared_ptr<const NoteList> l)
    {
       #if PUMP_SYNC_KERNELS
        retired.reset();
        incoming = std::move(l);
       #else
        std::atomic_store(&retired, std::shared_ptr<const NoteList>());   // the list the audio thread let go of: freed here
        std::atomic_store(&incoming, std::move(l));
       #endif
        notesReady.store(true, std::memory_order_release);
    }
    // the host's timeline at the start of the next block
    void setTimeline(bool playing, double seconds)
    {
        tlPlaying = playing; tlSeconds = seconds;
        playingNow.store(playing, std::memory_order_relaxed);
        playheadNow.store(seconds, std::memory_order_relaxed);
    }
    void setCapture(bool on) { capture.store(on); }

    void process(const float* inL, const float* inR, float* outL, float* outR, int nFrames, const MidiMsg* ev = nullptr, int numEv = 0)
    {
        if (notesReady.exchange(false, std::memory_order_acquire))
        {
           #if PUMP_SYNC_KERNELS
            retired = std::move(notes);
            notes = std::move(incoming);
           #else
            // never free a list on the audio thread: the old one waits in `retired` for the next setNotes()
            std::atomic_store(&retired, std::move(notes));
            notes = std::atomic_load(&incoming);
           #endif
            noteIdx = 0;
        }
        int e = 0;
        const double lv = clampv(cur.lead * 0.01, 0.0, 1.0), hl = dbToGain(cur.harmLevelDb), og = dbToGain(cur.outDb);
        for (int i = 0; i < nFrames; ++i)
        {
            while (e < numEv && (int)ev[e].frame <= i) { midiIn(ev[e]); ++e; }
            float x = 0.5f * (inL[i] + inR[i]);
            if (!(std::fabs(x) < 1e6f)) x = 0.0f;
            // pitch
            if (tracker.push(x))
            {
                const pitch::Tracker::Result& r = tracker.result();
                bool v = r.voiced && r.confidence > 0.55f && r.hz >= minHz * 0.9 && r.hz <= maxHz * 1.1;
                // a jump of more than three semitones counts once the next estimate agrees (a note's first estimate
                // can slip by an octave): until then the last pitch holds
                if (v && midiNow > 0.0)
                {
                    const double m = pitch::hzToMidi(r.hz);
                    if (std::fabs(m - midiNow) > 3.0 && std::fabs(m - jumpCand) > 0.7) { jumpCand = m; v = false; holdLast = true; }
                    else jumpCand = m;
                }
                const double frameT = tlSeconds + (double)(i - tracker.latencySamples()) / sr;
                if (holdLast) holdLast = false;   // keep the previous period and pitch for this one estimate
                else
                {
                    period = v ? sr / r.hz : 0.0;
                    control(v ? pitch::hzToMidi(r.hz) : -1.0, r.levelDb, frameT);
                }
                // stamped with the input time the estimate describes
                CtlSnap& c = ctlRing[ctlW & (kCtlRing - 1)];
                c.time = (double)(psola.inputCount() - tracker.latencySamples());
                c.lead = shiftT; c.formant = graphFormantT; c.gain = graphGainT;
                for (int h = 0; h < kVoices; ++h) c.harm[h] = harmTarget[h] > 0.0 ? harmTarget[h] - leadTargetNow : -1000.0;
                ++ctlW;
                if (capture.load(std::memory_order_relaxed) && tlPlaying) pushCapture({ frameT, v ? (float)pitch::hzToMidi(r.hz) : -1.0f, r.levelDb });
            }
            psola.push(x, period);
            // voices
            Psola::VoiceParams vp[Psola::kMaxVoices];
            const CtlSnap& now = snapAt((double)(psola.inputCount() - psola.latencySamples()));
            const double lead = leadShift(now);
            vp[0].on = true; vp[0].shiftSt = lead; vp[0].formant = std::exp2((cur.formant + graphFormant) / 12.0);
            vp[0].follow = cur.formantFollow; vp[0].gain = graphGain;
            int nv = 1;
            for (int h = 0; h < kVoices; ++h)
            {
                Psola::VoiceParams& p = vp[1 + h];
                const double off = now.harm[h];
                p.on = off > -999.0 && voiceLevel(h) > 0.0005;
                if (p.on)
                {
                    const double hum = clampv(cur.harmHumanize * 0.01, 0.0, 1.0);
                    p.shiftSt = lead + off + hum * 0.12 * driftNow[h];
                    p.formant = std::exp2((cur.harmFormant + cur.formant) / 12.0);
                    p.follow = cur.formantFollow;
                    p.offset = hum * 0.012 * sr * (0.5 + 0.5 * lagNow[h]);
                    p.gain = 1.0;
                }
                nv = 2 + h;
            }
            float y[Psola::kMaxVoices];
            psola.render(vp, nv, y);
            double l = y[0] * lv, r = y[0] * lv;
            for (int h = 0; h < kVoices; ++h)
            {
                if (!vp[1 + h].on) continue;
                const double g = voiceLevel(h) * hl;
                const double pn = clampv(cur.pan[h] * 0.01, -1.0, 1.0);
                l += y[1 + h] * g * std::sqrt(0.5 * (1.0 - pn));
                r += y[1 + h] * g * std::sqrt(0.5 * (1.0 + pn));
            }
            outL[i] = (float)(l * og);
            outR[i] = (float)(r * og);
        }
        while (e < numEv) { midiIn(ev[e]); ++e; }
        tlSeconds += nFrames / sr;
        publish();
    }

    // ---- feeds ----
    std::atomic<float> inMidi { -1.0f }, outMidi { -1.0f }, targetMidi { -1.0f };
    std::atomic<float> harmNoteNow[kVoices];
    std::atomic<bool> voicedNow { false }, playingNow { false };
    std::atomic<double> playheadNow { 0.0 };
    std::atomic<uint32_t> heldBits[4];
    // capture frames for the UI (single producer: the audio thread; single consumer: the UI)
    static constexpr int kCapRing = 1 << 14;
    CaptureFrame capRing[kCapRing];
    std::atomic<uint32_t> capW { 0 }, capR { 0 };

    // for the tests
    double currentShift() const { return leadShiftLast; }
    int currentTarget() const { return targetNote; }

private:
    void configure()
    {
        minHz = rangeMinHz(cur.range); maxHz = rangeMaxHz(cur.range);
        tracker.prepare(sr, minHz, maxHz, 24000.0, 0.0053);
        psola.prepare(sr, sr / minHz);
    }
    double voiceLevel(int h) const { return clampv(cur.level[h] * 0.01, 0.0, 1.0); }
    void pushCapture(const CaptureFrame& f)
    {
        const uint32_t w = capW.load(std::memory_order_relaxed);
        if (w - capR.load(std::memory_order_acquire) >= (uint32_t)kCapRing) return;   // the UI is behind: drop
        capRing[w & (kCapRing - 1)] = f;
        capW.store(w + 1, std::memory_order_release);
    }
    void midiIn(const MidiMsg& m)
    {
        const int t = m.status & 0xF0;
        if (t == 0x90 && m.d2 > 0) held[m.d1 & 127] = ++heldOrder;
        else if (t == 0x80 || (t == 0x90 && m.d2 == 0)) held[m.d1 & 127] = 0;
        else if (t == 0xB0 && (m.d1 == 120 || m.d1 == 123)) for (auto& h : held) h = 0;
    }
    uint16_t scaleMask() const { return pitch::kScaleMasks[clampv(cur.scale, 0, pitch::kNumScales - 1)]; }

    // once per tracker estimate (~5 ms): the lead's target and correction, the harmonies' notes
    void control(double m, float levelDb, double frameT)
    {
        const double dt = tracker.hopSamples() / sr;
        const bool voiced = m > 0.0;
        if (voiced)
        {
            // the note's centre: two one-poles at 2.5 Hz (a 6 Hz vibrato is down to a seventh); a new note jumps
            if (midiNow < 0.0 || std::fabs(m - midiLp) > 1.5) midiLp = midiLp1 = m;
            const double kc = 1.0 - std::exp(-dt * kTwoPi * 2.5);
            midiLp1 += (m - midiLp1) * kc;
            midiLp += (midiLp1 - midiLp) * kc;
            midiNow = m;
        }
        else { midiNow = -1.0; }
        // ---- the lead ----
        const uint16_t mask = scaleMask();
        int want = targetNote;
        double wantGraph = 0.0;
        bool inNote = false;
        if (cur.mode == kGraph)
        {
            const GraphNote* gn = noteAt(frameT);
            if (gn && voiced)
            {
                inNote = true;
                // move by the edit, straighten the drift, scale the vibrato
                const double centre = gn->src;
                const double out = gn->dst + (midiLp - centre) * (1.0 - gn->straighten) + (m - midiLp) * gn->vibrato;
                wantGraph = out - m;
                graphFormantT = gn->formant;
                graphGainT = dbToGain(gn->gainDb);
            }
            else { graphFormantT = 0.0; graphGainT = 1.0; }
            const double k = 1.0 - std::exp(-dt / 0.030);
            graphShift += ((inNote ? wantGraph : 0.0) - graphShift) * k;
            leadTargetNow = voiced ? m + graphShift : leadTargetNow;
        }
        else if (voiced)
        {
            int midiNote = -1;
            if (cur.midi == kMidiLead)
            {
                double bd = 1e9;
                for (int nn = 0; nn < 128; ++nn) if (held[nn] && std::fabs(nn - midiLp) < bd) { bd = std::fabs(nn - midiLp); midiNote = nn; }
            }
            if (midiNote >= 0) want = midiNote;
            else
            {
                const double base = cur.humanize > 0.0 ? midiLp : m;
                want = pitch::nearestWithHold(base, cur.key, mask, targetNote, 0.25);
            }
            if (want != targetNote) { targetNote = want; noteAge = 0.0; }
            noteAge += dt;
            // the error: of the note's centre (humanize) and of the voice right now
            const double hum = clampv(cur.humanize * 0.01, 0.0, 1.0);
            double err = (targetNote - midiLp) + (targetNote - m - (targetNote - midiLp)) * (1.0 - hum);
            // flex: notes within FLEX cents of the target are left alone (a soft edge)
            if (cur.flexCents > 0.0)
            {
                const double c = std::fabs(targetNote - midiLp) * 100.0;
                err *= clampv((c - cur.flexCents) / std::max(5.0, 0.3 * cur.flexCents), 0.0, 1.0);
            }
            err *= clampv(cur.amount * 0.01, 0.0, 1.0);
            const double k = cur.retuneMs < 0.5 ? 1.0 : 1.0 - std::exp(-dt / (cur.retuneMs * 0.001));
            correction += (err - correction) * k;
            leadTargetNow = targetNote;
        }
        // vibrato (added): faded in over 250 ms after a new note
        vibPhase += dt * cur.vibRate;
        vibPhase -= std::floor(vibPhase);
        const double vib = cur.vibDepth > 0.0 ? cur.vibDepth * 0.01 * std::sin(kTwoPi * vibPhase) * clampv((noteAge - 0.1) / 0.25, 0.0, 1.0) : 0.0;
        if (cur.mode == kGraph) shiftT = (voiced ? graphShift : 0.0) + vib + cur.transpose;
        else shiftT = (voiced ? correction : 0.0) + vib + cur.transpose;
        if (!voiced && cur.mode != kGraph) correction *= 0.9;   // no stale correction carried across a pause
        // ---- harmonies ----
        for (int h = 0; h < kVoices; ++h) harmTarget[h] = -1.0;
        if (voiced && cur.midi == kMidiHarmony)
        {
            int k = 0;
            for (int nn = 0; nn < 128 && k < kVoices; ++nn) if (held[nn]) harmTarget[k++] = nn;
        }
        else if (voiced && cur.harmMode != kHarmOff)
        {
            const int root = cur.mode == kGraph ? (int)std::lround(leadTargetNow) : targetNote;
            for (int h = 0; h < kVoices; ++h)
            {
                if (cur.interval[h] == 0) continue;
                harmTarget[h] = cur.harmMode == kHarmScale ? pitch::scaleStep(pitch::nearestInScale(root, cur.key, mask), cur.interval[h], cur.key, mask)
                                                           : root + cur.interval[h];
                if (cur.mode == kGraph) harmTarget[h] += leadTargetNow - root;
            }
        }
        if (cur.mode == kGraph && voiced) leadTargetNow = m + graphShift;
        // humanize: slow random drift (cents) and timing (ms) per harmony voice
        for (int h = 0; h < kVoices; ++h) { driftNow[h] = drift[h].advance(1); lagNow[h] = lag[h].advance(1); }
        (void)levelDb;
    }
    const GraphNote* noteAt(double t)
    {
        if (!notes || notes->notes.empty()) return nullptr;
        const auto& v = notes->notes;
        if (noteIdx >= (int)v.size()) noteIdx = (int)v.size() - 1;
        // usually the same note or the next: walk from the last one, else search
        if (t >= v[(size_t)noteIdx].t0 && t < v[(size_t)noteIdx].t1) return &v[(size_t)noteIdx];
        if (noteIdx + 1 < (int)v.size() && t >= v[(size_t)noteIdx + 1].t0 && t < v[(size_t)noteIdx + 1].t1) return &v[(size_t)++noteIdx];
        int lo = 0, hi = (int)v.size() - 1;
        while (lo <= hi)
        {
            const int mid = (lo + hi) / 2;
            if (t < v[(size_t)mid].t0) hi = mid - 1;
            else if (t >= v[(size_t)mid].t1) lo = mid + 1;
            else { noteIdx = mid; return &v[(size_t)mid]; }
        }
        return nullptr;
    }
    // the newest control snapshot describing input time t or earlier
    const CtlSnap& snapAt(double t) const
    {
        if (ctlW == 0) return ctlEmpty;
        const uint32_t lo = ctlW > (uint32_t)kCtlRing ? ctlW - (uint32_t)kCtlRing : 0u;
        for (uint32_t k = ctlW; k-- > lo;)
            if (ctlRing[k & (kCtlRing - 1)].time <= t) return ctlRing[k & (kCtlRing - 1)];
        return ctlRing[lo & (kCtlRing - 1)];
    }
    // per sample: the shift glides to the control value (no zipper), the graph's formant / gain too
    inline double leadShift(const CtlSnap& c)
    {
        const double k = 0.02;
        sm[0] += (c.lead - sm[0]) * k;
        sm[1] += (c.formant - sm[1]) * 0.002;
        sm[2] += (c.gain - sm[2]) * 0.002;
        graphFormant = sm[1]; graphGain = sm[2];
        leadShiftLast = sm[0];
        return sm[0];
    }
    void publish()
    {
        inMidi.store((float)midiNow, std::memory_order_relaxed);
        outMidi.store(midiNow > 0.0 ? (float)(midiNow + leadShiftLast) : -1.0f, std::memory_order_relaxed);
        targetMidi.store(midiNow > 0.0 ? (float)leadTargetNow : -1.0f, std::memory_order_relaxed);
        voicedNow.store(midiNow > 0.0, std::memory_order_relaxed);
        for (int h = 0; h < kVoices; ++h) harmNoteNow[h].store(voiceLevel(h) > 0.0005 ? (float)harmTarget[h] : -1.0f, std::memory_order_relaxed);
        uint32_t bits[4] = {};
        for (int nn = 0; nn < 128; ++nn) if (held[nn]) bits[nn >> 5] |= 1u << (nn & 31);
        for (int i = 0; i < 4; ++i) heldBits[i].store(bits[i], std::memory_order_relaxed);
    }

    double sr = 48000.0, minHz = 70.0, maxHz = 900.0;
    Settings cur;
    pitch::Tracker tracker;
    Psola psola;
    double jumpCand = -100.0;
    bool holdLast = false;
    double period = 0.0, midiNow = -1.0, midiLp = -1.0, midiLp1 = -1.0, correction = 0.0, shiftT = 0.0, vibPhase = 0.0, noteAge = 0.0, leadShiftLast = 0.0;
    int targetNote = -1;
    double leadTargetNow = 60.0;
    double harmTarget[kVoices] = { -1, -1, -1, -1 }, driftNow[kVoices] = {}, lagNow[kVoices] = {};
    fx::Wander drift[kVoices], lag[kVoices];
    uint32_t held[128] = {};
    uint32_t heldOrder = 0;
    double graphShift = 0.0, graphFormantT = 0.0, graphGainT = 1.0, graphFormant = 0.0, graphGain = 1.0;
    double sm[3] = { 0.0, 0.0, 1.0 };
    static constexpr int kCtlRing = 64;   // ~0.34 s of estimates: far more than the latency
    CtlSnap ctlRing[kCtlRing], ctlEmpty;
    uint32_t ctlW = 0;
    int noteIdx = 0;
    bool tlPlaying = false;
    double tlSeconds = 0.0;
    std::shared_ptr<const NoteList> notes, incoming, retired;
    std::atomic<bool> notesReady { false }, capture { false };
};

}} // namespace pump::tune
