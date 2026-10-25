// pump DSP: building blocks for the instruments (MIDI in, stereo out). Header-only, framework-free.
//
//   Midi           one MIDI message with its sample offset in the block
//   NoteQueue      lock-free single-producer queue: the window's on-screen keyboard -> the audio thread
//   VoiceManager   note allocation: poly (with stealing: released voices first, then the oldest), mono and legato
//                  (last-note priority with a note stack), sustain pedal, pitch bend, mod wheel, aftertouch
//   Adsr           exponential envelope (linear attack), retrigger from the current level, velocity amount
//   blep           PolyBLEP / PolyBLAMP corrections for band-limited saw, pulse and triangle
//   BandWaves      band-limited saw / triangle tables per octave (pulse from two saws): cleaner than PolyBLEP
//   Ladder         four-pole transistor-ladder low-pass (TPT one-poles, tanh at the input and in the loop),
//                  12 or 24 dB, resonance to self-oscillation, bass compensation
//   midiToHz, ratioFromSemis
#pragma once

#include "Fx.hpp"

namespace pump { namespace synth {

struct Midi { uint32_t offset; uint8_t status, d1, d2; };

inline double midiToHz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }
inline double ratioFromSemis(double s) { return std::exp2(s / 12.0); }

// The window's keyboard: note on (velocity > 0) / off (velocity 0). Single producer (UI), single consumer (audio).
class NoteQueue {
public:
    bool push(uint8_t note, uint8_t vel)
    {
        const uint32_t w = wr.load(std::memory_order_relaxed);
        if (w - rd.load(std::memory_order_acquire) >= kSize) return false;
        buf[w & (kSize - 1)] = (uint16_t)(note | (vel << 8));
        wr.store(w + 1, std::memory_order_release);
        return true;
    }
    template <class F> void drain(F&& f)
    {
        uint32_t r = rd.load(std::memory_order_relaxed);
        const uint32_t w = wr.load(std::memory_order_acquire);
        while (r != w) { const uint16_t v = buf[r & (kSize - 1)]; f((uint8_t)(v & 0x7F), (uint8_t)(v >> 8)); ++r; }
        rd.store(r, std::memory_order_release);
    }
private:
    static constexpr uint32_t kSize = 256;
    uint16_t buf[kSize] = {};
    std::atomic<uint32_t> wr { 0 }, rd { 0 };
};

// ---- voice allocation -----------------------------------------------------------------------------------------
enum VoiceMode : int { kPoly = 0, kMono, kLegato };

// What the manager asks the instrument to do with a voice.
struct VoiceCmd {
    enum Kind { Start, Glide, Release, Kill } kind;
    int voice;
    int note;
    float velocity;
};

class VoiceManager {
public:
    static constexpr int kMaxVoices = 32;

    void setup(int voices, int mode) { numVoices = clampv(voices, 1, kMaxVoices); voiceMode = mode; }
    void reset()
    {
        for (auto& v : vs) v = VState();
        stackN = 0; sustain = false; bend = 0.0; modWheel = 0.0; pressure = 0.0; counter = 0;
    }
    int voices() const { return voiceMode == kPoly ? numVoices : 1; }
    int mode() const { return voiceMode; }

    // Feed one MIDI message; commands for the instrument go to `out` (up to a few per message)
    template <class F> void midi(uint8_t status, uint8_t d1, uint8_t d2, F&& out)
    {
        const uint8_t st = status & 0xF0;
        if (st == 0x90 && d2 > 0) noteOn(d1, d2 / 127.0f, out);
        else if (st == 0x80 || (st == 0x90 && d2 == 0)) noteOff(d1, out);
        else if (st == 0xE0) bend = ((d2 << 7 | d1) - 8192) / 8192.0;
        else if (st == 0xD0) pressure = d1 / 127.0;
        else if (st == 0xB0)
        {
            if (d1 == 1) modWheel = d2 / 127.0;
            else if (d1 == 64)
            {
                const bool on = d2 >= 64;
                if (sustain && !on)
                    for (int i = 0; i < voices(); ++i)
                        if (vs[i].active && vs[i].sustained) { vs[i].sustained = false; vs[i].held = false; out(VoiceCmd { VoiceCmd::Release, i, vs[i].note, 0.0f }); }
                sustain = on;
            }
            else if (d1 == 120 || d1 == 123)   // all sound / notes off
            {
                for (int i = 0; i < voices(); ++i)
                    if (vs[i].active) out(VoiceCmd { d1 == 120 ? VoiceCmd::Kill : VoiceCmd::Release, i, vs[i].note, 0.0f });
                for (auto& v : vs) { v.held = false; v.sustained = false; if (d1 == 120) v.active = false; }
                stackN = 0;
            }
        }
    }
    // the instrument tells the manager when a voice has gone silent
    void voiceFinished(int i) { if (i >= 0 && i < kMaxVoices) vs[i].active = false; }

    double pitchBend() const { return bend; }       // -1..1
    double wheel() const { return modWheel; }        // 0..1
    double aftertouch() const { return pressure; }   // 0..1
    bool isActive(int i) const { return vs[i].active; }
    int noteOf(int i) const { return vs[i].note; }

private:
    struct VState { bool active = false, held = false, sustained = false; int note = -1; uint64_t age = 0; };

    template <class F> void noteOn(int note, float vel, F&& out)
    {
        if (voiceMode != kPoly)
        {
            pushStack(note);
            VState& v = vs[0];
            const bool legato = v.active && v.held && voiceMode == kLegato;
            v.active = true; v.held = true; v.sustained = false; v.note = note; v.age = ++counter;
            out(VoiceCmd { legato ? VoiceCmd::Glide : VoiceCmd::Start, 0, note, vel });
            return;
        }
        // the same note again: restart its voice
        int pick = -1;
        for (int i = 0; i < numVoices; ++i) if (vs[i].active && vs[i].note == note) { pick = i; break; }
        // a free voice, else the oldest released one, else the oldest
        if (pick < 0) for (int i = 0; i < numVoices; ++i) if (!vs[i].active) { pick = i; break; }
        if (pick < 0)
        {
            uint64_t best = ~0ull;
            for (int i = 0; i < numVoices; ++i) if (!vs[i].held && !vs[i].sustained && vs[i].age < best) { best = vs[i].age; pick = i; }
        }
        if (pick < 0)
        {
            uint64_t best = ~0ull;
            for (int i = 0; i < numVoices; ++i) if (vs[i].age < best) { best = vs[i].age; pick = i; }
        }
        VState& v = vs[pick];
        v.active = true; v.held = true; v.sustained = false; v.note = note; v.age = ++counter;
        out(VoiceCmd { VoiceCmd::Start, pick, note, vel });
    }
    template <class F> void noteOff(int note, F&& out)
    {
        if (voiceMode != kPoly)
        {
            popStack(note);
            VState& v = vs[0];
            if (!v.active || v.note != note) return;
            if (stackN > 0)
            {
                // back to the last note still held
                const int back = stack[stackN - 1];
                v.note = back;
                out(VoiceCmd { voiceMode == kLegato ? VoiceCmd::Glide : VoiceCmd::Start, 0, back, -1.0f });
                return;
            }
            if (sustain) { v.sustained = true; v.held = false; return; }
            v.held = false;
            out(VoiceCmd { VoiceCmd::Release, 0, note, 0.0f });
            return;
        }
        for (int i = 0; i < numVoices; ++i)
            if (vs[i].active && vs[i].held && vs[i].note == note)
            {
                vs[i].held = false;
                if (sustain) vs[i].sustained = true;
                else out(VoiceCmd { VoiceCmd::Release, i, note, 0.0f });
            }
    }
    void pushStack(int note) { popStack(note); if (stackN < 128) stack[stackN++] = note; }
    void popStack(int note) { int k = 0; for (int i = 0; i < stackN; ++i) if (stack[i] != note) stack[k++] = stack[i]; stackN = k; }

    VState vs[kMaxVoices];
    int numVoices = 8, voiceMode = kPoly;
    int stack[128] = {}, stackN = 0;
    bool sustain = false;
    double bend = 0.0, modWheel = 0.0, pressure = 0.0;
    uint64_t counter = 0;
};

// ---- envelope -------------------------------------------------------------------------------------------------
// Attack is linear (to 1); decay and release are exponential toward their targets, shaped so that they reach the
// target (within -80 dB) in the time set. A retrigger starts from the current level.
class Adsr {
public:
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    void setSampleRate(double s) { sr = s; }
    void set(double attackS, double decayS, double sustain, double releaseS)
    {
        atkInc = 1.0 / std::max(1.0, attackS * sr);
        decK = coefFor(decayS);
        relK = coefFor(releaseS);
        sus = clampv(sustain, 0.0, 1.0);
    }
    void gate(bool on) { if (on) stage = Attack; else if (stage != Idle) stage = Release; }
    void kill() { stage = Idle; v = 0.0; }
    bool active() const { return stage != Idle; }
    Stage state() const { return stage; }
    double level() const { return v; }
    inline double next()
    {
        switch (stage)
        {
        case Attack: v += atkInc; if (v >= 1.0) { v = 1.0; stage = Decay; } break;
        case Decay: v = sus + (v - sus) * decK; if (v - sus < 1e-4) { v = sus; stage = Sustain; } break;
        case Sustain: v = sus; break;
        case Release: v *= relK; if (v < 1e-5) { v = 0.0; stage = Idle; } break;
        default: break;
        }
        return v;
    }
private:
    double coefFor(double seconds) const { return std::exp(std::log(1e-4) / std::max(1.0, seconds * sr)); }
    double sr = 48000.0, v = 0.0, atkInc = 0.01, decK = 0.999, relK = 0.999, sus = 1.0;
    Stage stage = Idle;
};

// ---- band-limited oscillators (PolyBLEP) ------------------------------------------------------------------------
namespace blep {
    // correction for a step of height 1 at phase 0 (t in [0,1), dt = phase increment)
    inline double step(double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    // correction for a slope change (triangle corners)
    inline double ramp(double t, double dt)
    {
        if (t < dt) { t = t / dt - 1.0; return -t * t * t / 3.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt + 1.0; return t * t * t / 3.0; }
        return 0.0;
    }
}
// saw (-1..1 rising), at phase p with increment dt
inline double blepSaw(double p, double dt) { return 2.0 * p - 1.0 - blep::step(p, dt); }
// pulse with width w (0..1)
inline double blepPulse(double p, double dt, double w)
{
    double y = p < w ? 1.0 : -1.0;
    y += blep::step(p, dt);
    double q = p - w; if (q < 0.0) q += 1.0;
    y -= blep::step(q, dt);
    return y;
}
// triangle (-1..1)
inline double blepTri(double p, double dt)
{
    double y = p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
    y += 4.0 * dt * blep::ramp(p, dt);
    double q = p - 0.5; if (q < 0.0) q += 1.0;
    y -= 4.0 * dt * blep::ramp(q, dt);
    return y;
}

// ---- band-limited saw and triangle tables ---------------------------------------------------------------------------
// One table per octave of pitch ("mip"), each holding only the harmonics that stay below Nyquist for the pitches it
// serves, built additively once per process and shared. Read with Hermite interpolation (at least 8 samples per
// period of the top harmonic): far cleaner than PolyBLEP at high notes. Pulse = the difference of two saws, so pulse
// width modulation stays band-limited too. Same shapes as blepSaw / blepTri (saw rises -1 -> 1; triangle -1 at 0).
class BandWaves {
public:
    static constexpr int kMips = 11;
    static const BandWaves& get() { static const BandWaves b; return b; }
    static int mipFor(double inc)
    {
        const double maxH = 0.5 / std::max(1e-9, inc);
        int m = 0;
        while (m < kMips - 1 && harmonics(m) > maxH) ++m;
        return m;
    }
    inline double saw(double p, int m) const { return read(sawT[m], p); }
    inline double tri(double p, int m) const { return read(triT[m], p); }
    inline double pulse(double p, double w, int m) const
    {
        double q = p - w; if (q < 0.0) q += 1.0;
        return (saw(q, m) - saw(p, m)) + (2.0 * w - 1.0);
    }
private:
    static int harmonics(int m) { return std::max(1, 1024 >> m); }
    static int size(int m) { return std::min(2048, std::max(256, 8 * harmonics(m))); }
    BandWaves()
    {
        for (int m = 0; m < kMips; ++m)
        {
            const int n = size(m), H = harmonics(m);
            std::vector<double> s((size_t)n, 0.0), t((size_t)n, 0.0);
            for (int h = 1; h <= H; ++h)
            {
                const double a = -2.0 / (kPi * h);                                  // 2p - 1 = -(2/pi) sum sin(2 pi h p) / h
                const double b = (h % 2) ? -8.0 / (kPi * kPi * h * h) : 0.0;       // triangle: -(8/pi^2) sum_odd cos / h^2
                for (int i = 0; i < n; ++i)
                {
                    const double x = kTwoPi * h * i / n;
                    s[(size_t)i] += a * std::sin(x);
                    if (b != 0.0) t[(size_t)i] += b * std::cos(x);
                }
            }
            auto store = [&](std::vector<float>& d, const std::vector<double>& v) {
                d.resize((size_t)n + 3);
                for (int i = 0; i < n; ++i) d[(size_t)i + 1] = (float)v[(size_t)i];
                d[0] = d[(size_t)n]; d[(size_t)n + 1] = d[1]; d[(size_t)n + 2] = d[2];
            };
            store(sawT[m], s); store(triT[m], t);
        }
    }
    static inline double read(const std::vector<float>& d, double p)
    {
        const int n = (int)d.size() - 3;
        const double x = p * n;
        const int i = (int)x;
        const float fr = (float)(x - i);
        const float* y = d.data() + i;   // y[0..3] = x(i-1) .. x(i+2)
        const float c1 = 0.5f * (y[2] - y[0]), c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3], c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
        return ((c3 * fr + c2) * fr + c1) * fr + y[1];
    }
    std::vector<float> sawT[kMips], triT[kMips];
};

// ---- ladder filter -----------------------------------------------------------------------------------------------
// Four TPT one-poles in series with feedback k (0..5.2; self-oscillation from 4), solved with one tanh-linearised step.
// mode 0 = 24 dB/oct (4th pole), 1 = 12 dB/oct (2nd pole). Bass loss at high resonance is partly made up.
class Ladder {
public:
    void reset() { for (auto& x : s) x = 0.0; }
    void set(double fc, double res, double sampleRate)
    {
        const double g = std::tan(kPi * clampv(fc, 10.0, 0.47 * sampleRate) / sampleRate);
        G = g / (1.0 + g);
        k = 5.2 * clampv(res, 0.0, 1.0);   // oscillation starts near 77 %, strong and steady at 100 %
    }
    inline double process(double x, int mode = 0, double drive = 1.0)
    {
        // estimate the output from the states (zero-delay feedback, linearised)
        const double G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const double S = G3 * (1.0 - G) * s[0] + G2 * (1.0 - G) * s[1] + G * (1.0 - G) * s[2] + (1.0 - G) * s[3];
        const double comp = 1.0 + 0.5 * k;   // bass compensation
        const double u = (std::tanh(drive * x) * comp - k * S) / (1.0 + k * G4);
        double y = std::tanh(u);
        for (int i = 0; i < 4; ++i)
        {
            const double v = (y - s[i]) * G;
            const double out = v + s[i];
            s[i] = out + v;
            y = out;
            if (i == 1) y2 = out;
        }
        return (mode == 1 ? y2 : y) / std::max(1.0, drive * 0.7);
    }
private:
    double s[4] = { 0, 0, 0, 0 }, G = 0.1, k = 0.0, y2 = 0.0;
};

}} // namespace pump::synth
