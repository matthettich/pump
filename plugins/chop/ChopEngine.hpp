// pump CHOP engine. Framework-free.
//
// The input goes into a 32 s ring all the time. The REGION is the last LENGTH of the song's grid (the last whole bar,
// say, ending at the most recent bar line: everything in it is already recorded), cut into SLICES equal slices. It
// follows the song until a pad goes down, and stays put while any pad is down (and after, with HOLD).
// The pads held (MIDI, the window, the keyboard: one mask) decide what plays: the last one pressed starts a read head
// at its slice (from its end, backwards, for REVERSE) at its pitch (a rate); it reads on through the region and wraps;
// with two or more held (and LOOP TWO) it loops from the lowest of their slices to the end of the highest. A new head
// crossfades from the old one in 3 ms; the chop crossfades over the live input in 3 ms (MIX: how much it replaces).
// QUANTISE holds a press back to the next grid line (a release is never held back). The pad's effect, its amount and
// its level shape what its head reads.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "ChopParams.hpp"
#include <array>
#include <atomic>

namespace pump { namespace chop {

struct PadSettings { int slice = 1, pitch = 0, fx = kFxNone, note = 48, key = '1'; bool reverse = false; double amount = 0.5, levelDb = 0.0; };
struct Settings {
    std::array<PadSettings, kPads> pad {};
    int slices = 8, length = kOneBar, quant = kQ16;
    bool hold = false, loopTwo = true;
    double bpm = 120.0, mix = 1.0, outDb = 0.0;
};

inline double lengthBars(int l) { static const double b[kNumLengths] = { 0.5, 1.0, 2.0, 4.0 }; return b[clampv(l, 0, kNumLengths - 1)]; }
inline double quantQuarters(int q) { static const double v[kNumQuant] = { 0.0, 0.125, 0.25, 0.5, 1.0 }; return v[clampv(q, 0, kNumQuant - 1)]; }

class Engine {
public:
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        int size = 1;
        while (size < (int)(32.0 * sr)) size <<= 1;
        for (auto& b : buf) b.assign((size_t)size, 0.0f);
        mask = size - 1;
        written = 0; pos = 0.0;
        held = pending = 0; order.clear(); order.reserve(16);
        head[0] = head[1] = Head();
        env = 0.0; xf = 1.0;
        fadeInc = 1.0 / std::max(1.0, 0.003 * sr);
        mixS.setRamp(std::max(1, (int)(0.02 * sr))); mixS.reset(set.mix);
        outS.setRamp(std::max(1, (int)(0.02 * sr))); outS.reset(dbToGain(set.outDb));
        regionStart = 0.0; regionLen = 1.0; locked = false; captured = false;
    }
    void setSettings(const Settings& s) { set = s; }
    void setTransport(const Transport& t)
    {
        tr = t;
        sync = t.playing && t.valid && std::isfinite(t.ppq) && std::fabs(t.ppq) < 1e9;
        if (sync) pos = t.ppq;
    }
    // the pads held now (bit p = pad p); takes effect at the next sample processed
    void setHeld(uint32_t m) { want = m & ((1u << kPads) - 1); }

    // ---- feeds (UI) ----
    double regionStartNow() const { return rsFeed.load(std::memory_order_relaxed); }
    double regionLenNow() const { return rlFeed.load(std::memory_order_relaxed); }
    double playheadNow() const { return phFeed.load(std::memory_order_relaxed); }   // absolute sample, or -1
    long long writtenNow() const { return wFeed.load(std::memory_order_relaxed); }
    int padNow() const { return padFeed.load(std::memory_order_relaxed); }           // the pad playing, or -1
    uint32_t heldNow() const { return heldFeed.load(std::memory_order_relaxed); }
    uint32_t pendingNow() const { return pendFeed.load(std::memory_order_relaxed); }
    // the region's waveform, n peaks (the UI thread reads the ring while it is written: a glimpse, not exact)
    void overview(float* dst, int n) const
    {
        const double rs = rsFeed.load(), rl = rlFeed.load();
        for (int i = 0; i < n; ++i)
        {
            const long long a = (long long)(rs + rl * i / n), b = (long long)(rs + rl * (i + 1) / n);
            float pk = 0.0f;
            for (long long k = a; k < b; k += std::max(1LL, (b - a) / 32)) if (k >= 0) pk = std::max(pk, std::max(std::fabs(buf[0][(size_t)(k & mask)]), std::fabs(buf[1][(size_t)(k & mask)])));
            dst[i] = pk;
        }
    }

    void process(float* L, float* R, int n)
    {
        const double bpm = sync ? clampv(tr.bpm, 20.0, 999.0) : clampv(set.bpm, 20.0, 999.0);
        const double qps = bpm / 60.0 / sr;
        const double barQ = tr.valid && tr.barQuarters > 0.0 ? tr.barQuarters : 4.0;
        const double lenQ = barQ * lengthBars(set.length);
        const int slices = set.slices >= 16 ? 16 : 8;
        const double qq = quantQuarters(set.quant);
        mixS.setTarget(set.mix); outS.setTarget(dbToGain(set.outDb));
        for (int i = 0; i < n; ++i)
        {
            buf[0][(size_t)(written & mask)] = L[i];
            buf[1][(size_t)(written & mask)] = R[i];
            ++written;
            // the region: follow the song's grid while nothing holds it
            const bool holding = held != 0 || pending != 0 || (set.hold && captured);
            if (!holding)
            {
                const double since = (pos - lenQ * std::floor(pos / lenQ)) / qps;   // samples since the last grid line
                regionLen = std::max(64.0, lenQ / qps);
                regionStart = (double)written - since - regionLen;
            }
            // presses and releases
            const uint32_t pressed = want & ~held & ~pending, released = (held | pending) & ~want;
            if (released) { held &= ~released; pending &= ~released; for (int p = 0; p < kPads; ++p) if (released >> p & 1) dropOrder(p); changed = true; }
            if (pressed)
            {
                if (qq <= 0.0) { held |= pressed; addOrder(pressed); changed = true; }
                else pending |= pressed;
            }
            if (pending && qq > 0.0)
            {
                // on the next grid line
                const double g = pos / qq;
                if (std::floor(g + qps / qq) > std::floor(g) || g - std::floor(g) < 1e-9) { held |= pending; addOrder(pending); pending = 0; changed = true; }
            }
            if (changed) { changed = false; retarget(slices); }
            // the chop
            double cl = 0.0, cr = 0.0;
            if (env > 0.0 || held)
            {
                double al = 0.0, ar = 0.0, bl = 0.0, br = 0.0;
                if (head[0].active) readHead(head[0], al, ar);
                if (xf < 1.0 && head[1].active) readHead(head[1], bl, br);
                cl = al * xf + bl * (1.0 - xf); cr = ar * xf + br * (1.0 - xf);
                xf = std::min(1.0, xf + fadeInc);
            }
            const double target = held ? 1.0 : 0.0;
            env += clampv(target - env, -fadeInc, fadeInc);
            if (env <= 0.0 && !held) head[0].active = head[1].active = false;
            const double a = env * mixS.next(), og = outS.next();
            L[i] = (float)((L[i] + (cl - L[i]) * a) * og);
            R[i] = (float)((R[i] + (cr - R[i]) * a) * og);
            pos += qps;
        }
        if (!std::isfinite(env + head[0].f.ic1 + head[1].f.ic1)) { head[0] = head[1] = Head(); env = 0.0; held = pending = 0; order.clear(); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        rsFeed.store(regionStart); rlFeed.store(regionLen); wFeed.store(written);
        phFeed.store(head[0].active && env > 0.0 ? head[0].pos : -1.0);
        padFeed.store(head[0].active && held ? head[0].pad : -1);
        heldFeed.store(held); pendFeed.store(pending);
    }

private:
    struct Head {
        bool active = false;
        int pad = 0;
        double pos = 0.0, rate = 1.0, start = 0.0, sliceLen = 1.0, elapsed = 0.0, loopA = 0.0, loopB = 1.0, hold = 0.0;
        double crushL = 0.0, crushR = 0.0, crushT = 0.0;
        fx::Svf f, f2;
    };

    void addOrder(uint32_t m) { for (int p = 0; p < kPads; ++p) if (m >> p & 1) { dropOrder(p); order.push_back(p); } }
    void dropOrder(int p) { for (size_t i = 0; i < order.size(); ++i) if (order[i] == p) { order.erase(order.begin() + (long)i); break; } }

    // what plays after a change: the newest held pad (a new head if it changed), and the loop of the pads held
    void retarget(int slices)
    {
        if (!held || order.empty()) return;
        captured = true;
        const int p = order.back();
        const PadSettings& s = set.pad[(size_t)p];
        const double sliceLen = regionLen / slices;
        auto sliceOf = [&](int q) { return clampv(set.pad[(size_t)q].slice, 1, slices) - 1; };
        // the loop: one pad, the whole region; two or more (LOOP TWO), from the lowest slice held to the end of the highest
        double la = regionStart, lb = regionStart + regionLen;
        int nh = 0; for (int q = 0; q < kPads; ++q) nh += held >> q & 1;
        if (set.loopTwo && nh >= 2)
        {
            int lo = slices, hi = -1;
            for (int q = 0; q < kPads; ++q) if (held >> q & 1) { lo = std::min(lo, sliceOf(q)); hi = std::max(hi, sliceOf(q)); }
            la = regionStart + lo * sliceLen; lb = regionStart + (hi + 1) * sliceLen;
        }
        if (!head[0].active || head[0].pad != p || !held)
        {
            head[1] = head[0];
            Head h;
            h.active = true; h.pad = p; h.sliceLen = sliceLen;
            h.start = regionStart + sliceOf(p) * sliceLen;
            h.rate = std::exp2(clampv(s.pitch, -24, 24) / 12.0) * (s.reverse ? -1.0 : 1.0);
            h.pos = s.reverse ? h.start + sliceLen - 1.0 : h.start;
            head[0] = h;
            xf = env > 0.0 ? 0.0 : 1.0;
        }
        head[0].loopA = la; head[0].loopB = lb;
    }

    // the recorded input at an absolute (fractional) position: cubic, never later than the newest sample
    inline void readAt(double p, double& l, double& r) const
    {
        p = std::min(p, (double)written - 2.0);
        p = std::max(p, (double)(written - mask + 4));
        const long long ip = (long long)std::floor(p);
        const double f = p - (double)ip;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& b = buf[ch];
            const double ym1 = b[(size_t)((ip - 1) & mask)], y0 = b[(size_t)(ip & mask)], y1 = b[(size_t)((ip + 1) & mask)], y2 = b[(size_t)((ip + 2) & mask)];
            const double c1 = 0.5 * (y1 - ym1), c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
            (ch ? r : l) = ((c3 * f + c2) * f + c1) * f + y0;
        }
    }
    inline void readHead(Head& h, double& l, double& r)
    {
        const PadSettings& s = set.pad[(size_t)h.pad];
        const double amt = clampv(s.amount, 0.0, 1.0);
        double rate = h.rate;
        // tape stop: the head slows to a stop over (0.3 to 2) slices
        if (s.fx == kFxTape) { const double T = h.sliceLen * (0.3 + 1.7 * amt); rate *= std::pow(std::max(0.0, 1.0 - h.elapsed / T), 1.5); }
        double p = h.pos;
        // ratchet: the head keeps going back to the start of its slice (every half to a sixteenth of it)
        if (s.fx == kFxRatchet)
        {
            const double r0 = h.sliceLen / std::exp2(1.0 + std::round(amt * 3.0));
            const double off = std::fmod(h.elapsed * std::fabs(h.rate), r0);
            p = h.rate < 0.0 ? h.start + h.sliceLen - 1.0 - off : h.start + off;
        }
        readAt(p, l, r);
        // the short fades where the ratchet jumps back
        if (s.fx == kFxRatchet)
        {
            const double r0 = h.sliceLen / std::exp2(1.0 + std::round(amt * 3.0)), off = std::fmod(h.elapsed * std::fabs(h.rate), r0), e = std::min(0.001 * sr, 0.25 * r0);
            const double g = std::min(1.0, std::min(off / e, (r0 - off) / e));
            l *= g; r *= g;
        }
        switch (s.fx)
        {
        case kFxLowpass:
            if ((ctl++ & 15) == 0) { const double fc = 20000.0 * std::exp2(-8.0 * amt); h.f.set(std::min(fc, 0.45 * sr), 1.0, sr); h.f2.set(std::min(fc, 0.45 * sr), 1.0, sr); }
            l = h.f.lowpass(l); r = h.f2.lowpass(r); break;
        case kFxHighpass:
            if ((ctl++ & 15) == 0) { const double fc = 20.0 * std::exp2(9.0 * amt); h.f.set(std::min(fc, 0.45 * sr), 1.0, sr); h.f2.set(std::min(fc, 0.45 * sr), 1.0, sr); }
            l = h.f.highpass(l); r = h.f2.highpass(r); break;
        case kFxCrush:
        {
            const double hold = 1.0 + 15.0 * amt * amt, q = std::ldexp(1.0, (int)std::lround(15.0 - 12.0 * amt));
            if ((h.crushT += 1.0) >= hold) { h.crushT -= hold; h.crushL = std::round(l * q) / q; h.crushR = std::round(r * q) / q; }
            l = h.crushL; r = h.crushR; break;
        }
        case kFxGate:
        {
            const double qps = (sync ? clampv(tr.bpm, 20.0, 999.0) : clampv(set.bpm, 20.0, 999.0)) / 60.0 / sr;
            const double step = 0.125 / qps, open = std::max(0.1, 1.0 - 0.9 * amt) * step, j = std::fmod(h.elapsed, step), e = 0.001 * sr;
            const double g = j < open ? std::min(1.0, std::min(j / e, (open - j) / e)) : 0.0;
            l *= g; r *= g; break;
        }
        case kFxDrive: { const double d = 1.0 + 9.0 * amt, c = 1.0 / std::tanh(d); l = std::tanh(l * d) * c * 0.8; r = std::tanh(r * d) * c * 0.8; break; }
        default: break;
        }
        const double g = dbToGain(s.levelDb);
        l *= g; r *= g;
        // on: wrap within the loop
        h.pos += rate;
        h.elapsed += 1.0;
        const double span = std::max(1.0, h.loopB - h.loopA);
        if (h.pos >= h.loopB) h.pos -= span * std::ceil((h.pos - h.loopB + 1e-9) / span);
        if (h.pos < h.loopA) h.pos += span * std::ceil((h.loopA - h.pos) / span);
    }

    double sr = 48000.0, pos = 0.0;
    Settings set;
    Transport tr;
    bool sync = false, locked = false, captured = false, changed = false;
    std::vector<float> buf[2];
    int mask = 0;
    long long written = 0;
    uint32_t want = 0, held = 0, pending = 0;
    std::vector<int> order;
    Head head[2];
    double env = 0.0, xf = 1.0, fadeInc = 0.007, regionStart = 0.0, regionLen = 1.0;
    unsigned ctl = 0;
    LinearSmoother mixS, outS;
    std::atomic<double> rsFeed { 0.0 }, rlFeed { 1.0 }, phFeed { -1.0 };
    std::atomic<long long> wFeed { 0 };
    std::atomic<int> padFeed { -1 };
    std::atomic<uint32_t> heldFeed { 0 }, pendFeed { 0 };
};

}} // namespace pump::chop
