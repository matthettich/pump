// pump LIFE engine: eight tone voices, a multi-FX, and a Game of Life as the sequencer. Framework-free.
//
// Tones. A parabolic sine (two parabolas: a sine with a little odd-harmonic edge). KICK sweeps its pitch down from
// up to four octaves above in about 12 ms (a tone becomes a drum); FM moves its phase by the NEXT voice's oscillator,
// RING multiplies it by the PREVIOUS voice's (all eight oscillators run all the time, so the voices colour each other
// even when silent); DECAY is the time to -60 dB; LEVEL; mute. Global FM and DECAY scale every voice's; the LFO moves
// every pitch by up to PITCH RANGE semitones (and the FM a little). DRIVE saturates the mix.
// The FX. Each hit also opens its own short envelope into the FX (FX DECAY: only the start of every hit goes in, which
// keeps busy patterns tight). The FX is a delay (TIME) whose loop holds a two-grain pitch shifter (SHIFT, GRAIN) and a
// bipolar filter (left: low-pass closing, right: high-pass opening), with FEEDBACK through a soft clip; MIX.
// The sequencer. A toroidal grid (up to 16 x 16) under a life rule. Every step (1/4 to 1/32 of a beat) is one
// generation; the cells that are born (or that live, or die: TRIGGER) are counted by colour, and a voice plays when
// its colour's count reaches the threshold SENSITIVITY sets (velocity grows with the count). A cell's colour is
// (x * X-WRAP + y * Y-WRAP + COLOUR) mod 8. LOOP: the board is the pattern evolved OFFSET + (step mod LENGTH)
// generations, so it repeats exactly with the song; EVOLVE: it runs on from the pattern while the host plays (back
// to the pattern when the host starts, or when it dies or stalls, with RESEED); FREE: the same at the host tempo,
// whether or not the host plays. MIDI notes 36-43 play the voices directly.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "LifeParams.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace pump { namespace life {

struct ToneSettings { double pitch = 60.0, kick = 0.0, fm = 0.0, ring = 0.0, decay = 0.2, level = 0.7; bool mute = false; };
struct Settings {
    double volumeDb = -6.0, drive = 0.15, fmAll = 0.5, decayAll = 1.0, pitchRange = 2.0, lfoRate = 0.5, lfoDepth = 0.0, width = 0.6;
    int mode = kModeLoop, rate = 2, length = 16, offset = 0;
    double sens = 1.0;
    int xWrap = 1, yWrap = 3, colour = 0, cols = 16, rows = 16, rule = kLife, trigger = kTrigBirth, pattern = 2;
    bool reseed = true;
    double shift = 0.0, grain = 0.06, time = 0.18, feedback = 0.35, filter = 0.0, fxDecay = 0.12, mix = 0.25;
    ToneSettings tone[kTones];
};

using Board = std::array<uint16_t, kMaxGrid>;   // a row per entry, bit x = column x

// ---- the board ------------------------------------------------------------------------------------------------------
inline Board patternBoard(int id, int W, int H)
{
    Board b {}; b.fill(0);
    auto set = [&](int x, int y) { if (x >= 0 && y >= 0 && x < W && y < H) b[(size_t)y] |= (uint16_t)(1u << x); };
    auto put = [&](std::initializer_list<std::pair<int, int>> cells, int ox, int oy) { for (auto c : cells) set(ox + c.first, oy + c.second); };
    auto centered = [&](std::initializer_list<std::pair<int, int>> cells, int pw, int ph) { put(cells, (W - pw) / 2, (H - ph) / 2); };
    switch (id)
    {
    case 1: centered({ {1,0}, {2,1}, {0,2}, {1,2}, {2,2} }, 3, 3); break;
    case 2: put({ {1,0}, {2,1}, {0,2}, {1,2}, {2,2} }, 1, 1); put({ {1,0}, {0,1}, {2,2}, {1,2}, {0,2} }, W - 5, H / 2); break;
    case 3: put({ {0,0}, {1,0}, {2,0} }, 2, 2); put({ {0,0}, {0,1}, {0,2} }, W - 4, 2); put({ {0,0}, {1,0}, {2,0} }, W / 2 - 1, H - 4); put({ {0,0}, {0,1}, {0,2} }, 3, H / 2); break;
    case 4:
    {
        // the pulsar (period 3), 13 x 13
        const int ox = (W - 13) / 2, oy = (H - 13) / 2;
        for (int k : { 0, 5, 7, 12 }) for (int j : { 2, 3, 4, 8, 9, 10 }) { set(ox + j, oy + k); set(ox + k, oy + j); }
        break;
    }
    case 5: centered({ {1,0}, {2,0}, {0,1}, {1,1}, {1,2} }, 3, 3); break;
    case 6: centered({ {1,0}, {3,1}, {0,2}, {1,2}, {4,2}, {5,2}, {6,2} }, 7, 3); break;
    case 7: put({ {1,0}, {4,0}, {0,1}, {0,2}, {4,2}, {0,3}, {1,3}, {2,3}, {3,3} }, 1, (H - 4) / 2); break;
    case 8: put({ {1,0}, {2,0}, {3,0}, {0,1}, {1,1}, {2,1} }, 2, 3); put({ {0,0}, {1,0}, {0,1}, {3,2}, {2,3}, {3,3} }, W - 6, H - 7); break;
    case 9: centered({ {6,0}, {0,1}, {1,1}, {1,2}, {5,2}, {6,2}, {7,2} }, 8, 3); break;
    case 10:
    {
        // the galaxy (period 8), 9 x 9
        static const char* const g[9] = { "##.######", "##.######", "##.......", "##.....##", "##.....##", "##.....##", ".......##", "######.##", "######.##" };
        for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) if (g[y][x] == '#') set((W - 9) / 2 + x, (H - 9) / 2 + y);
        break;
    }
    case 11: for (int k = 0; k < 4; ++k) { const int cx = (k % 2) * (W / 2) + W / 4, cy = (k / 2) * (H / 2) + H / 4; put({ {0,-1}, {-1,0}, {0,0}, {1,0}, {0,1} }, cx, cy); } break;
    case 12: case 13: case 14:
    {
        const double density = id == 12 ? 0.33 : (id == 13 ? 0.16 : 0.55);
        uint32_t s = 0x5EED0000u + (uint32_t)id;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { s = s * 1664525u + 1013904223u; if ((s >> 8) * (1.0 / 16777216.0) < density) set(x, y); }
        break;
    }
    default: break;
    }
    return b;
}
inline Board stepBoard(const Board& b, int W, int H, int rule)
{
    Board n {}; n.fill(0);
    const unsigned birth = kBirth[clampv(rule, 0, kNumRules - 1)], survive = kSurvive[clampv(rule, 0, kNumRules - 1)];
    auto alive = [&](int x, int y) { x = (x + W) % W; y = (y + H) % H; return (b[(size_t)y] >> x) & 1; };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            int c = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) if (dx || dy) c += alive(x + dx, y + dy);
            const bool on = alive(x, y);
            if (on ? (survive >> c & 1) : (birth >> c & 1)) n[(size_t)y] |= (uint16_t)(1u << x);
        }
    return n;
}
inline int colourOf(int x, int y, int xw, int yw, int off) { return ((x * xw + y * yw + off) % kTones + kTones) % kTones; }
inline bool boardEmpty(const Board& b) { for (auto r : b) if (r) return false; return true; }

class Engine {
public:
    static constexpr int kCtl = 16;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& t : tones) t = Tone();
        for (int i = 0; i < kTones; ++i) tones[(size_t)i].ph = i * 0.137;
        dly.assign((size_t)(sr * 1.1) + 4, 0.0f); dlyPos = 0;
        shf.assign((size_t)(sr * 0.5) + 4, 0.0f); shfPos = 0; shfD = 0.0;
        fbState = 0.0; filt.reset(); dcL.reset(); dcR.reset(); dcL.set(10.0, sr); dcR.set(10.0, sr);
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        lfoPh = 0.0; freePhase = 0.0; lastStep = -1; wasPlaying = false;
        resetBoard();
        control();
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.pattern != set.pattern || s.cols != set.cols || s.rows != set.rows;
        set = s;
        if (re) resetBoard();
    }
    void setTransport(const Transport& t) { tr = t; }
    // the drawn board (pattern 0), from the UI or the saved state
    void setDrawn(const Board& b) { for (int y = 0; y < kMaxGrid; ++y) drawn[(size_t)y].store(b[(size_t)y], std::memory_order_relaxed); drawnVersion.fetch_add(1, std::memory_order_release); }
    void requestReset() { resetReq.store(true, std::memory_order_release); }
    void requestStep() { stepReq.fetch_add(1, std::memory_order_release); }
    void trigger(int t, double vel) { if (t >= 0 && t < kTones) hit(t, vel); }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    Board boardNow() const { Board b; for (int y = 0; y < kMaxGrid; ++y) b[(size_t)y] = boardFeed[(size_t)y].load(std::memory_order_relaxed); return b; }
    int generationNow() const { return genFeed.load(std::memory_order_relaxed); }
    float toneLevel(int t) const { return levelFeed[(size_t)clampv(t, 0, kTones - 1)].load(std::memory_order_relaxed); }
    // bit t: tone t was hit since the last read
    uint32_t takeHits() { return hitFeed.exchange(0u, std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        if (drawnVersion.load(std::memory_order_acquire) != drawnSeen && set.pattern == 0) resetBoard();
        if (resetReq.exchange(false, std::memory_order_acq_rel)) { resetBoard(); lastStep = -1; }
        for (int k = stepReq.exchange(0, std::memory_order_acq_rel); k > 0; --k) advance(1);
        const bool hostRun = tr.playing && set.mode != kModeFree;
        if (tr.playing && !wasPlaying && set.mode == kModeEvolve) { resetBoard(); lastStep = -1; }
        wasPlaying = tr.playing;
        const double spq = kStepsPerQuarter[clampv(set.rate, 0, 3)];
        const double qps = tr.bpm / 60.0 / sr;   // quarter notes per sample
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            // the step clock
            if (hostRun && tr.valid)
            {
                const long long s = (long long)std::floor((tr.ppq + i * qps) * spq + 1e-7);
                if (s != lastStep) { onStep(s); lastStep = s; }
            }
            else if (set.mode == kModeFree)
            {
                freePhase += qps * spq;
                if (freePhase >= 1.0) { freePhase -= std::floor(freePhase); advance(1); }
            }
            if ((ctl++ % kCtl) == 0) control();
            double l, r;
            renderSample(l, r);
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            for (auto& t : tones) { t.env = 0.0; t.raw = 0.0; t.fxEnv = 0.0; }
            std::fill(dly.begin(), dly.end(), 0.0f); std::fill(shf.begin(), shf.end(), 0.0f); fbState = 0.0; filt.reset();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        for (int t = 0; t < kTones; ++t) levelFeed[(size_t)t].store((float)tones[(size_t)t].env);
        for (int y = 0; y < kMaxGrid; ++y) boardFeed[(size_t)y].store(cur[(size_t)y], std::memory_order_relaxed);
        genFeed.store(gen);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }

private:
    struct Tone {
        double ph = 0.0, raw = 0.0, env = 0.0, att = 1.0, kickEnv = 0.0, fxEnv = 0.0, vel = 1.0;
        double inc = 0.0, decK = 0.999, fmAmt = 0.0, kickSemis = 0.0, level = 0.7, ring = 0.0, panL = 0.707, panR = 0.707;
    };

    int W() const { return clampv(set.cols, 4, kMaxGrid); }
    int H() const { return clampv(set.rows, 4, kMaxGrid); }
    Board pattern()
    {
        if (set.pattern == 0)
        {
            drawnSeen = drawnVersion.load(std::memory_order_acquire);
            Board b; for (int y = 0; y < kMaxGrid; ++y) b[(size_t)y] = drawn[(size_t)y].load(std::memory_order_relaxed);
            const uint16_t mask = (uint16_t)((1u << W()) - 1u);
            for (int y = 0; y < kMaxGrid; ++y) b[(size_t)y] = y < H() ? (uint16_t)(b[(size_t)y] & mask) : 0;
            return b;
        }
        return patternBoard(set.pattern, W(), H());
    }
    void resetBoard()
    {
        cur = pattern();
        for (int k = 0; k < set.offset; ++k) cur = stepBoard(cur, W(), H(), set.rule);
        prev = prev2 = cur;
        gen = set.offset;
    }
    // the host's step s: LOOP puts the board at its place in the cycle; EVOLVE runs one generation on
    void onStep(long long s)
    {
        if (set.mode == kModeLoop)
        {
            const int L = clampv(set.length, 1, 64);
            const int g = set.offset + (int)(((s % L) + L) % L);
            if (g != gen + 1)
            {
                // jump: rebuild from the pattern to the generation before, then step into g (its events)
                Board b = pattern();
                for (int k = 0; k < g - 1 && k < 200; ++k) b = stepBoard(b, W(), H(), set.rule);
                if (g == 0) { cur = b; gen = 0; fire(Board {}, cur, true); return; }
                cur = b; gen = g - 1;
            }
            advance(1, false);
            return;
        }
        advance(1);
    }
    void advance(int k, bool allowReseed = true)
    {
        for (int i = 0; i < k; ++i)
        {
            const Board before = cur;
            prev2 = prev; prev = cur;
            cur = stepBoard(cur, W(), H(), set.rule);
            ++gen;
            if (allowReseed && set.reseed && set.mode != kModeLoop && (boardEmpty(cur) || cur == prev || cur == prev2))
            {
                cur = pattern(); prev = prev2 = Board {}; gen = 0;
                fire(Board {}, cur, true);
                continue;
            }
            fire(before, cur, false);
        }
    }
    // count the events by colour and play the voices whose count reaches the threshold
    void fire(const Board& a, const Board& b, bool asBirths)
    {
        int count[kTones] = {};
        const int trig = asBirths ? kTrigBirth : set.trigger;
        for (int y = 0; y < H(); ++y)
            for (int x = 0; x < W(); ++x)
            {
                const bool was = (a[(size_t)y] >> x) & 1, now = (b[(size_t)y] >> x) & 1;
                const bool ev = trig == kTrigBirth ? (!was && now) : (trig == kTrigAlive ? now : (was && !now));
                if (ev) ++count[colourOf(x, y, set.xWrap, set.yWrap, set.colour)];
            }
        const int thr = 1 + (int)std::lround((1.0 - clampv(set.sens, 0.0, 1.0)) * 7.0);
        for (int t = 0; t < kTones; ++t)
            if (count[t] >= thr) hit(t, clampv(0.55 + 0.12 * (count[t] - thr), 0.55, 1.0));
    }
    void hit(int t, double vel)
    {
        if (set.tone[t].mute) return;
        Tone& o = tones[(size_t)t];
        o.vel = vel; o.att = 0.0; o.env = std::max(o.env, 1e-4); o.kickEnv = 1.0; o.fxEnv = 1.0;
        hitFeed.fetch_or(1u << t, std::memory_order_relaxed);
    }
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (st == 0x90 && m.d2 > 0 && m.d1 >= 36 && m.d1 < 36 + kTones) hit(m.d1 - 36, m.d2 / 127.0);
    }

    void control()
    {
        const Settings& s = set;
        const double dt = kCtl / sr;
        lfoPh += s.lfoRate * dt; lfoPh -= std::floor(lfoPh);
        const double lfo = std::sin(kTwoPi * lfoPh) * s.lfoDepth;
        for (int t = 0; t < kTones; ++t)
        {
            const ToneSettings& p = s.tone[t];
            Tone& o = tones[(size_t)t];
            o.inc = synth::midiToHz(p.pitch + lfo * s.pitchRange) / sr;
            o.fmAmt = p.fm * s.fmAll * 1.5 * (1.0 + 0.5 * lfo);
            o.kickSemis = p.kick * 48.0;
            o.decK = std::exp(std::log(1e-3) / std::max(1.0, p.decay * s.decayAll * sr));
            o.level = p.level; o.ring = p.ring;
            const double pan = s.width * (2.0 * t / (kTones - 1) - 1.0) * (t % 2 ? 1.0 : -1.0) * 0.8;
            o.panL = std::cos(0.25 * kPi * (pan + 1.0)); o.panR = std::sin(0.25 * kPi * (pan + 1.0));
        }
        kickK = std::exp(-1.0 / (0.012 * sr));
        attInc = 1.0 / (0.001 * sr);
        fxK = std::exp(std::log(1e-3) / std::max(1.0, s.fxDecay * sr));
        // the FX filter: left of centre a low-pass closing from 20 kHz to 150 Hz, right a high-pass opening to 6 kHz
        const double f = clampv(s.filter, -1.0, 1.0);
        filtMode = f < -0.02 ? -1 : (f > 0.02 ? 1 : 0);
        if (filtMode < 0) filt.set(20000.0 * std::pow(150.0 / 20000.0, -f), 0.9, sr);
        else if (filtMode > 0) filt.set(20.0 * std::pow(6000.0 / 20.0, f), 0.9, sr);
        shfRatio = std::exp2(s.shift / 12.0);
        shfN = clampv(s.grain * sr, 64.0, (double)shf.size() - 8.0);
        dlySamples = clampv(s.time * sr, 1.0, (double)dly.size() - 4.0);
        drvG = 1.0 + s.drive * 7.0;
    }

    static inline double parabolic(double p)
    {
        p -= std::floor(p);
        return p < 0.5 ? 16.0 * p * (0.5 - p) : -16.0 * (p - 0.5) * (1.0 - p);
    }
    inline void renderSample(double& outL, double& outR)
    {
        double raws[kTones];
        for (int t = 0; t < kTones; ++t) raws[t] = tones[(size_t)t].raw;
        double l = 0.0, r = 0.0, send = 0.0;
        for (int t = 0; t < kTones; ++t)
        {
            Tone& o = tones[(size_t)t];
            const double y0 = parabolic(o.ph + o.fmAmt * raws[(t + 1) % kTones]);
            o.raw = y0;
            const double inc = o.kickEnv > 1e-4 ? o.inc * std::exp2(o.kickSemis * o.kickEnv / 12.0) : o.inc;
            o.ph += std::min(inc, 0.45); if (o.ph >= 1.0) o.ph -= std::floor(o.ph);
            o.kickEnv *= kickK;
            if (o.env <= 1e-6 && o.att >= 1.0) { o.env = 0.0; continue; }
            if (o.att < 1.0) { o.att = std::min(1.0, o.att + attInc); o.env = std::max(o.env, o.att); }
            else o.env *= o.decK;
            const double y = y0 * (1.0 - o.ring + o.ring * raws[(t + kTones - 1) % kTones]) * o.env * o.level * o.vel;
            l += y * o.panL; r += y * o.panR;
            send += y * o.fxEnv;
            o.fxEnv *= fxK;
        }
        // the FX: delay -> pitch shifter -> filter, fed back
        const double x = send + set.feedback * 0.95 * fbState;
        const size_t DN = dly.size();
        dly[dlyPos] = (float)x;
        double rp = (double)dlyPos - dlySamples; if (rp < 0) rp += (double)DN;
        const size_t i0 = (size_t)rp, i1 = (i0 + 1) % DN; const double fr = rp - (double)i0;
        double y = dly[i0] + (dly[i1] - dly[i0]) * fr;
        dlyPos = (dlyPos + 1) % DN;
        // two grains reading behind the write head, half a grain apart, crossfaded (sin^2 windows)
        const size_t SN = shf.size();
        shf[shfPos] = (float)y;
        auto tap = [&](double d) { double p = (double)shfPos - d - 2.0; while (p < 0) p += (double)SN; const size_t a = (size_t)p % SN, b2 = (a + 1) % SN; const double f2 = p - std::floor(p); return shf[a] + (shf[b2] - shf[a]) * f2; };
        const double d1 = shfD, d2 = std::fmod(shfD + 0.5 * shfN, shfN);
        const double w1 = std::sin(kPi * d1 / shfN), w2 = std::sin(kPi * d2 / shfN);
        y = tap(d1) * w1 * w1 + tap(d2) * w2 * w2;
        shfD += 1.0 - shfRatio; if (shfD >= shfN) shfD -= shfN; if (shfD < 0) shfD += shfN;
        shfPos = (shfPos + 1) % SN;
        if (filtMode < 0) y = filt.lowpass(y); else if (filtMode > 0) y = filt.highpass(y);
        fbState = std::tanh(y * 1.2) / 1.2;
        const double wet = y * set.mix * 1.4, dry = 1.0 - 0.4 * set.mix;
        double ol = (l * dry + wet) * 0.5, orr = (r * dry + wet) * 0.5;
        ol = std::tanh(ol * drvG) / std::sqrt(drvG); orr = std::tanh(orr * drvG) / std::sqrt(drvG);
        outL = dcL.process(ol); outR = dcR.process(orr);
    }

    Settings set;
    Transport tr;
    double sr = 48000.0;
    std::array<Tone, kTones> tones {};
    Board cur {}, prev {}, prev2 {};
    int gen = 0;
    long long lastStep = -1;
    bool wasPlaying = false;
    double freePhase = 0.0, lfoPh = 0.0;
    uint64_t ctl = 0;
    double kickK = 0.99, attInc = 0.02, fxK = 0.99, shfRatio = 1.0, shfN = 2880.0, shfD = 0.0, dlySamples = 8000.0, drvG = 1.0, fbState = 0.0;
    int filtMode = 0;
    fx::Svf filt;
    fx::DcBlocker dcL, dcR;
    std::vector<float> dly, shf;
    size_t dlyPos = 0, shfPos = 0;
    LinearSmoother volS;
    std::array<std::atomic<uint16_t>, kMaxGrid> drawn {};
    std::atomic<uint32_t> drawnVersion { 0 };
    uint32_t drawnSeen = 0;
    std::atomic<bool> resetReq { false };
    std::atomic<int> stepReq { 0 };
    std::array<std::atomic<uint16_t>, kMaxGrid> boardFeed {};
    std::atomic<int> genFeed { 0 };
    std::array<std::atomic<float>, kTones> levelFeed {};
    std::atomic<uint32_t> hitFeed { 0 };
};

}} // namespace pump::life
