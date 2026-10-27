// pump STUTTER engine: tempo-locked repeats, rolls, reverses, tape stops and glitches. Framework-free.
//
// The input is recorded all the time (10 s ring). A MOVE plays from that ring for a while and is crossfaded over the
// live signal (3 ms in and out; MIX sets how much it replaces). Moves start three ways:
//   auto     at every INTERVAL boundary of the song (the host's position when it plays, a free clock at its tempo
//            when it doesn't send one), with CHANCE, picking one of the moves switched on for auto; it lasts LENGTH
//   held     MIDI notes (C, C#, D ... G of any octave = the eight moves) and the pads in the window: the move
//            starts at once and lasts while held (the last one pressed wins); held moves override auto
//   pattern  (PATTERN on, instead of auto) the pattern's steps on their own grid: a step with a move starts it for
//            its length in steps, with its own slice and transposition
// Every move starts where it is triggered (its slice is recorded as it plays), so nothing needs latency. GRID is
// the slice. Each repeat of a slice gets a 1 ms fade at its ends (no clicks at the loop points), its own pitch
// (PITCH semitones per repeat), level (DECAY per repeat) and filter (FILTER per repeat: down = a low-pass closing,
// up = a high-pass opening); GATE shortens every slice. VARIATION randomises each auto move: the grid (half or
// double), the pitch direction, reversed repeats, and how wild the glitches get. Randomness is seeded: renders repeat.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "StutterParams.hpp"

namespace pump { namespace stutter {

struct Settings {
    int trigger = kAuto, interval = kNote1, length = kNote4, grid = kNote16;
    double chance = 35.0, gate = 100.0, pitch = 0.0, decay = 0.0, filter = 0.0, variation = 0.0, tape = 100.0;
    double mix = 100.0, outDb = 0.0;
    bool use[kNumMoves] = { true, true, true, true, false, false, false, false };
    bool patternOn = false; int patternSel = 1, patStep = kNote16;
};

class Engine {
public:
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        int size = 1;
        while (size < (int)(10.0 * sr)) size <<= 1;
        for (auto& b : buf) b.assign((size_t)size, 0.0f);
        mask = size - 1;
        written = 0;
        fade = std::max(1, (int)(0.003 * sr));
        edge = std::max(1, (int)(0.001 * sr));
        mixS.setRamp(std::max(1, (int)(0.02 * sr))); mixS.reset(set.mix * 0.01);
        outS.setRamp(std::max(1, (int)(0.02 * sr))); outS.reset(dbToGain(set.outDb));
        ev = Event(); env = 0.0;
        offL = offR = lastL = lastR = 0.0; declick = false;
        offK = std::exp(-1.0 / (0.0007 * sr));
        pos = 0.0; lastStep = -1; lastPatStep = -1; heldMove = -1;
        rng.seed(0x5EEDu);
        for (auto& f : filt) f.reset();
        decim = 0; peakAcc = 0.0f;
    }
    void setSettings(const Settings& s) { set = s; }
    // the USER pattern (the factory ones are built in)
    void setUserPattern(const Pattern& p) { user = p; }
    const Pattern& pattern() const { return set.patternSel >= kUserPattern ? user : factoryPattern(set.patternSel); }
    int patternStepNow() const { return patFeed.load(std::memory_order_relaxed); }   // -1: not running
    void setTransport(const Transport& t)
    {
        tr = t;
        locked = t.playing && t.valid && std::isfinite(t.ppq) && std::fabs(t.ppq) < 1e9;
        if (locked) pos = t.ppq;
    }
    // the move held by MIDI or a pad (-1 = none); takes effect at the next sample processed
    void setHeld(int move) { heldMove = move >= 0 && move < kNumMoves ? move : -1; }

    int latency() const { return 0; }

    // ---- feeds (UI) ----
    int moveNow() const { return moveFeed.load(std::memory_order_relaxed); }           // -1 = none
    bool heldNow() const { return heldFeed.load(std::memory_order_relaxed); }
    int repeatNow() const { return repFeed.load(std::memory_order_relaxed); }
    float progressNow() const { return progFeed.load(std::memory_order_relaxed); }      // 0..1 (held: grows slowly)
    float barPhase() const { return barFeed.load(std::memory_order_relaxed); }         // 0..1 within the bar
    // a strip of the recent past for the timeline: one value per 64 samples: output peak, and the move (or -1)
    ScopeRing levelRing { 1 << 13 }, moveRing { 1 << 13 };

    void process(float* L, float* R, int n)
    {
        const double qps = clampv(tr.bpm, 20.0, 999.0) / 60.0 / sr;   // quarter notes per sample
        const double intervalQ = noteQuarters(set.interval), barQ = tr.barQuarters > 0.0 ? tr.barQuarters : 4.0;
        // auto runs while the host plays (locked to its position), or on a free clock when the host sends no position
        const bool patOn = set.patternOn && (locked || !tr.valid);
        const bool autoOn = !patOn && set.trigger == kAuto && (locked || !tr.valid);
        const Pattern& pat = pattern();
        const double patQ = noteQuarters(set.patStep);
        int patNow = -1;
        bool anyUse = false; for (bool u : set.use) anyUse = anyUse || u;
        mixS.setTarget(set.mix * 0.01);
        outS.setTarget(dbToGain(set.outDb));
        for (int i = 0; i < n; ++i)
        {
            // ---- record
            buf[0][(size_t)(written & mask)] = L[i];
            buf[1][(size_t)(written & mask)] = R[i];

            // ---- decide what plays
            // a boundary only where the position lands on one (starting, locating or looping to the middle of an
            // interval waits for the next grid line)
            const double steps = pos / intervalQ + 1e-9;
            const long long step = (long long)std::floor(steps);
            const bool boundary = step != lastStep && steps - (double)step < 2.0 * qps / intervalQ;
            lastStep = step;
            // the pattern's step: a move starts where a step has one (and not while a held move plays)
            if (patOn)
            {
                const double ps = pos / patQ + 1e-9;
                const long long pk = (long long)std::floor(ps);
                const bool pb = pk != lastPatStep && ps - (double)pk < 2.0 * qps / patQ;
                lastPatStep = pk;
                const int idx = (int)(((pk % pat.length) + pat.length) % pat.length);
                patNow = idx;
                if (pb && heldMove < 0)
                {
                    const Step& st = pat.s[idx];
                    if (st.move >= 0) start(st.move, false, st.len * patQ / qps, st.grid - 1, st.transpose);
                }
            }
            if (heldMove >= 0)
            {
                if (!ev.active || !ev.held || ev.ending || ev.move != heldMove) start(heldMove, true, 0.0);
            }
            else if (ev.active && ev.held) stop();
            if (boundary && autoOn && anyUse && !(ev.active && ev.held))
            {
                if (rng.uni() * 100.0 < set.chance)
                {
                    int pick = (int)(rng.uni() * kNumMoves), tries = 0;
                    while (!set.use[pick] && tries++ < 16) pick = (pick + 1) % kNumMoves;
                    if (set.use[pick]) start(pick, false, noteQuarters(set.length) / qps);
                }
            }

            // ---- play
            double wl = L[i], wr = R[i];
            if (ev.active)
            {
                render(wl, wr, L[i], R[i]);
                if (declick) { offL = lastL - wl; offR = lastR - wr; declick = false; }
                ++ev.elapsed;
                if (!ev.held && ev.lengthS > 0.0 && ev.elapsed >= ev.lengthS) ev.ending = true;
            }
            const double target = ev.active && !ev.ending ? 1.0 : 0.0;
            env += clampv(target - env, -1.0 / fade, 1.0 / fade);
            if (ev.active && ev.ending && env <= 0.0) ev.active = false;
            wl += offL; wr += offR;
            offL *= offK; offR *= offK;
            lastL = wl; lastR = wr;
            const double mx = mixS.next(), og = outS.next(), a = env * mx;
            const float ol = (float)((L[i] + (wl - L[i]) * a) * og), orr = (float)((R[i] + (wr - R[i]) * a) * og);
            L[i] = ol; R[i] = orr;

            // ---- feeds
            peakAcc = std::max(peakAcc, std::max(std::fabs(ol), std::fabs(orr)));
            if (++decim >= 64)
            {
                levelRing.push(peakAcc);
                moveRing.push(ev.active && env > 0.5 ? (float)ev.move : -1.0f);
                decim = 0; peakAcc = 0.0f;
            }
            ++written;
            pos += qps;
        }
        if (!std::isfinite(env + filt[0].ic1 + filt[1].ic1)) { ev.active = false; env = 0.0; for (auto& f : filt) f.reset(); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        moveFeed.store(ev.active ? ev.move : -1, std::memory_order_relaxed);
        heldFeed.store(ev.active && ev.held, std::memory_order_relaxed);
        repFeed.store(ev.active ? ev.k : 0, std::memory_order_relaxed);
        progFeed.store(ev.active ? (float)(ev.lengthS > 0.0 ? std::min(1.0, ev.elapsed / ev.lengthS) : std::min(1.0, ev.elapsed / (4.0 * sr))) : 0.0f, std::memory_order_relaxed);
        barFeed.store((float)(pos / barQ - std::floor(pos / barQ)), std::memory_order_relaxed);
        patFeed.store(patNow, std::memory_order_relaxed);
    }

private:
    struct Event {
        bool active = false, held = false, ending = false;
        int move = 0, k = 0;
        long long t0 = 0;           // absolute sample where the move started
        double elapsed = 0.0, lengthS = 0.0, sliceS = 1.0;
        double pitchStep = 0.0;     // semitones per repeat
        bool reverse = false;       // reversed repeats (variation)
        double readPos = 0.0;       // tape / half: absolute read position
        double segStart = 0.0, segLen = 1.0;   // roll: the current repeat; shuffle / glitch: the source
        double glitchRatio = 1.0;
        bool glitchRev = false;
        double nextChange = 0.0, wild = 0.0;
        double trRatio = 1.0;       // a pattern step's transposition, as a rate
    };

    void start(int move, bool held, double lengthS, int gridOverride = -1, int transpose = 0)
    {
        const double qps = clampv(tr.bpm, 20.0, 999.0) / 60.0 / sr;
        const double var = held ? 0.0 : set.variation * 0.01;
        Event e;
        e.active = true; e.held = held; e.move = move;
        e.t0 = written; e.elapsed = 0.0; e.lengthS = lengthS;
        e.trRatio = std::exp2(clampv(transpose, -24, 24) / 12.0);
        double slice = noteQuarters(gridOverride >= 0 ? gridOverride : set.grid) / qps;
        if (var > 0.0 && rng.uni() < var) slice *= rng.uni() < 0.5 ? 0.5 : 2.0;
        e.sliceS = clampv(slice, 0.004 * sr, 4.0 * sr);
        e.pitchStep = set.pitch;
        if (var > 0.0 && rng.uni() < var * 0.5) e.pitchStep = (rng.uni() < 0.5 ? -1.0 : 1.0) * (set.pitch != 0.0 ? std::fabs(set.pitch) : std::round(1.0 + 4.0 * rng.uni()));
        e.reverse = var > 0.0 && rng.uni() < var * 0.35;
        e.readPos = (double)written;
        e.segStart = 0.0; e.segLen = e.sliceS;   // roll: the current repeat (relative); shuffle / glitch set their own
        e.wild = var;
        e.nextChange = 0.0;
        // a move replacing one that is sounding: the jump in the output is taken out over ~2 ms
        if (ev.active && env > 0.0) declick = true;
        ev = e;
        for (auto& f : filt) f.reset();
    }
    void stop() { ev.ending = true; }

    // the recorded input at an absolute (fractional) position; never later than the newest sample
    inline void readAt(double p, double& l, double& r) const
    {
        p = std::min(p, (double)written);
        p = std::max(p, (double)(written - mask + 4));
        const long long ip = (long long)std::floor(p);
        const double f = p - (double)ip;
        auto at = [&](int ch, long long k) { return (double)buf[ch][(size_t)((k > written ? written : k) & mask)]; };
        for (int ch = 0; ch < 2; ++ch)
        {
            const double ym1 = at(ch, ip - 1), y0 = at(ch, ip), y1 = at(ch, ip + 1), y2 = at(ch, ip + 2);
            const double c1 = 0.5 * (y1 - ym1), c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
            (ch ? r : l) = f < 1e-9 ? y0 : ((c3 * f + c2) * f + c1) * f + y0;
        }
    }
    // the 1 ms fades at the ends of a segment of length len, at offset j
    inline double edges(double j, double len) const
    {
        const double e = std::min((double)edge, 0.25 * len);
        return std::min(1.0, std::min(j / e, (len - j) / e));
    }
    // repeat-count filter: down = low-pass closing 1.5 octaves per repeat, up = high-pass opening
    inline void filterRepeat(double& l, double& r, double k)
    {
        const double amt = set.filter * 0.01;
        if (std::fabs(amt) < 0.005 || k <= 0.0) return;
        if ((ctl++ & 15) == 0)
        {
            const double oct = std::min(9.0, k * std::fabs(amt) * 1.5);
            const double fc = amt < 0.0 ? 20000.0 * std::exp2(-oct) : 20.0 * std::exp2(oct);
            for (auto& f : filt) f.set(std::min(fc, 0.45 * sr), 0.9, sr);
        }
        if (amt < 0.0) { l = filt[0].lowpass(l); r = filt[1].lowpass(r); }
        else { l = filt[0].highpass(l); r = filt[1].highpass(r); }
    }

    void render(double& wl, double& wr, double inL, double inR)
    {
        Event& e = ev;
        const double gateF = clampv(set.gate * 0.01, 0.05, 1.0);
        const double decayG = 1.0 - clampv(set.decay * 0.01, 0.0, 1.0);
        switch (e.move)
        {
        case kRepeat:
        case kRoll:
        {
            // the current repeat: k, offset j inside it, and its length
            if (e.move == kRoll)
            {
                // every repeat half as long as the last (down to 6 ms), always from the start of the first slice
                if (e.elapsed >= e.segStart + e.segLen)
                {
                    e.segStart += e.segLen;
                    e.segLen = std::max(0.006 * sr, 0.5 * e.segLen);
                    ++e.k;
                }
            }
            else e.k = (int)std::floor(e.elapsed / e.sliceS);
            const double len = e.move == kRoll ? e.segLen : e.sliceS;
            const double j = e.move == kRoll ? e.elapsed - e.segStart : e.elapsed - e.k * e.sliceS;
            const double ratio = std::exp2(e.k * e.pitchStep / 12.0) * (e.k >= 1 ? e.trRatio : 1.0);
            double src = std::fmod(j * ratio, len);
            if (e.reverse && e.k >= 1) src = len - 1.0 - src;
            double l, r;
            readAt((double)e.t0 + src, l, r);
            double g = std::pow(decayG, (double)e.k);
            if (e.k >= 1) g *= edges(j, len);
            if (gateF < 1.0) g *= j < gateF * len ? std::min(1.0, (gateF * len - j) / edge) : 0.0;
            l *= g; r *= g;
            filterRepeat(l, r, (double)e.k);
            wl = l; wr = r;
            break;
        }
        case kReverse:
        {
            // what came before the move, backwards
            double l, r;
            readAt((double)e.t0 - 1.0 - e.elapsed * e.trRatio, l, r);
            wl = l; wr = r;
            break;
        }
        case kTapeStop:
        {
            const double T = std::max(1.0, (e.lengthS > 0.0 ? e.lengthS : 2.0 * sr) * set.tape * 0.01);
            const double x = std::min(1.0, e.elapsed / T);
            const double rate = std::pow(1.0 - x, 1.6);
            e.readPos += rate;
            double l, r;
            readAt(e.readPos, l, r);
            const double g = x >= 1.0 ? 0.0 : std::min(1.0, (1.0 - x) * 12.0);   // the last moments before the stop fade
            wl = l * g; wr = r * g;
            break;
        }
        case kHalf:
        {
            e.readPos += 0.5 * e.trRatio;
            double l, r;
            readAt(e.readPos, l, r);
            wl = l; wr = r;
            break;
        }
        case kShuffle:
        {
            // every slice: a random earlier slice of the last bar (or, on the first one, the live signal)
            const double qps = clampv(tr.bpm, 20.0, 999.0) / 60.0 / sr;
            const int k = (int)std::floor(e.elapsed / e.sliceS);
            const double j = e.elapsed - k * e.sliceS;
            if (k != e.k || e.elapsed == 0.0)
            {
                e.k = k;
                const int slices = std::max(1, (int)std::lround((tr.barQuarters > 0 ? tr.barQuarters : 4.0) / qps / e.sliceS));
                const int back = 1 + (int)(rng.uni() * slices);
                e.segStart = (double)e.t0 + k * e.sliceS - back * e.sliceS;
                e.glitchRev = e.reverse && rng.uni() < 0.5;
            }
            double l, r;
            const double jj = std::fmod(j * e.trRatio, e.sliceS);
            readAt(e.segStart + (e.glitchRev ? e.sliceS - 1.0 - jj : jj), l, r);
            const double g = edges(j, e.sliceS) * (gateF < 1.0 ? (j < gateF * e.sliceS ? std::min(1.0, (gateF * e.sliceS - j) / edge) : 0.0) : 1.0);
            wl = l * g; wr = r * g;
            break;
        }
        case kGateMove:
        {
            const double j = std::fmod(e.elapsed, e.sliceS), open = std::max(gateF, 0.05) * e.sliceS;
            const double g = j < open ? std::min(1.0, std::min(j / edge, (open - j) / edge)) : 0.0;
            wl = inL * g; wr = inR * g;
            break;
        }
        case kGlitch:
        default:
        {
            if (e.elapsed >= e.nextChange)
            {
                const double wild = 0.3 + 0.7 * e.wild;
                e.segLen = std::max(16.0, (0.003 + 0.057 * rng.uni() * wild + 0.01 * (1.0 - wild)) * sr);
                e.segStart = (double)written - e.segLen - rng.uni() * e.sliceS;
                static const double ratios[] = { 1.0, 1.0, 2.0, 0.5, 1.5, 0.75 };
                e.glitchRatio = ratios[(int)(rng.uni() * (wild > 0.5 ? 6 : 3)) % 6];
                e.glitchRev = rng.uni() < 0.3 * wild;
                e.nextChange = e.elapsed + e.sliceS * (rng.uni() < 0.5 ? 0.5 : 0.25);
                e.k++;
                e.readPos = 0.0;
            }
            const double j = std::fmod(e.readPos, e.segLen);
            e.readPos += e.glitchRatio * e.trRatio;
            double l, r;
            readAt(e.segStart + (e.glitchRev ? e.segLen - 1.0 - j : j), l, r);
            const double g = edges(j, e.segLen);
            wl = l * g; wr = r * g;
            break;
        }
        }
    }

    double sr = 48000.0;
    Settings set;
    Transport tr;
    bool locked = false;
    double pos = 0.0;
    long long lastStep = -1, written = 0, lastPatStep = -1;
    Pattern user;
    int mask = 0, fade = 144, edge = 48, heldMove = -1;
    unsigned ctl = 0;
    std::vector<float> buf[2];
    Event ev;
    double env = 0.0, offL = 0.0, offR = 0.0, offK = 0.97, lastL = 0.0, lastR = 0.0;
    bool declick = false;
    fx::Svf filt[2];
    fx::Rng rng { 0x5EEDu };
    LinearSmoother mixS, outS;
    int decim = 0;
    float peakAcc = 0.0f;
    std::atomic<int> moveFeed { -1 }, repFeed { 0 }, patFeed { -1 };
    std::atomic<bool> heldFeed { false };
    std::atomic<float> progFeed { 0.0f }, barFeed { 0.0f };
};

}} // namespace pump::stutter
