// pump BOUNCE engine: a bouncing-ball delay. Framework-free, allocation-free on the audio thread.
//
// The bounce: the gaps between the hits shrink by GRAVITY each time, like a ball dropped on a floor (RISE inverts the
// ratio: the gaps grow, a ball thrown up). Hit k lands at t_k = T (1 + g + g^2 + ... + g^(k-1)) after the trigger, T
// being the first gap. The ball settles when the next gap would be under 2 ms, after BOUNCES hits, or at 8 s.
//
// Two engines (MODE):
//   Taps  a multitap delay on the continuous signal: a tap at every t_k, each DAMP dB quieter than the one before and
//         panned by SPREAD. JITTER moves each tap by a fixed random fraction of its gap. TIME, GRAVITY and the rest
//         glide (the taps slide like a tape head). FEEDBACK feeds the last tap back into the line, so the settled
//         ball re-drops (a softer copy of the whole bounce after t_N).
//   Drop  a trigger (a transient over THRESHOLD, a beat division of the host tempo, a MIDI note-on or the DROP
//         button) captures SLICE ms of the input and re-fires it as a bounce: the input is the first hit and every
//         hit after is the slice again, DAMP dB quieter, PITCH semitones higher or lower than the last (a resampled
//         playback, accumulating: a ratio of 0.25..4), tilted brighter or darker by TONE, panned by SPREAD. FIT keeps
//         each hit inside its gap. Up to four drops sound at once (a fifth fades out the oldest). A drop keeps the
//         settings it started with. FEEDBACK brings the whole bounce back after it settles, softer, until it is
//         about 50 dB down. Every hit has a raised-cosine window at both ends.
// The ring that records the input is also the pre-roll of a drop: a transient trigger includes the 2 ms before the
// onset. The dry signal is the input itself (no latency); the dry stays at unity up to MIX 50 %, the wet reaches
// unity at 50 %.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "BounceParams.hpp"
#include <atomic>
#include <cmath>
#include <vector>

namespace pump { namespace bounce {

struct Settings {
    int mode = kModeTaps;
    double timeMs = 200.0;
    bool sync = false;
    int note = kNote8;
    double gravity = 0.80;
    bool rise = false;
    int bounces = 16;
    double dampDb = 1.5, pitch = 0.0, tone = 0.0, spread = 40.0, jitter = 0.0;
    double sliceMs = 200.0;
    bool fit = true;
    int trigger = kTrigTransient;
    double thresholdDb = -24.0;
    int every = kNote4;
    double feedback = 0.0;   // %
    double mix = 50.0, outDb = 0.0;
};

// The first gap in seconds (SYNC = a note division at the tempo). Shared by the engine, the UI and the tests.
inline double firstGapSeconds(bool sync, int note, double ms, double bpm)
{
    return clampv(sync ? noteSeconds(note, bpm) : 0.001 * ms, kMinGapSeconds, kMaxSeconds);
}
// The ratio of each gap to the one before.
inline double gapRatio(double gravity, bool rise)
{
    const double g = clampv(gravity, (double)kGravityMin, (double)kGravityMax);
    return rise ? 1.0 / g : g;
}
// The hit times: out[0] = 0 (the input itself), out[k] = T (1 + rho + ... + rho^(k-1)). Stops after `bounces` hits, when
// the gap that would follow a hit is under 2 ms (before it is placed: the ball has settled) or past `cap` seconds.
// Returns the number of hits after the first (`out` holds that + 1 values, at most kMaxBounces + 1).
inline int bounceTimes(double T, double rho, int bounces, double cap, double* out)
{
    out[0] = 0.0;
    double gap = T, t = 0.0;
    int m = 0;
    const int n = clampv(bounces, 0, kMaxBounces);
    for (int k = 1; k <= n; ++k)
    {
        if (gap < kMinGapSeconds) break;
        t += gap;
        if (t > cap) break;
        out[++m] = t;
        gap *= rho;
    }
    return m;
}
// Per-bounce values (k = 1 is the first bounce). The pitch ratio accumulates and stays within two octaves.
inline double pitchRatio(double st, int k) { return clampv(std::exp2(st * k / 12.0), 0.25, 4.0); }
inline double tiltDb(double tone, int k) { return clampv(tone * k * 0.04, -18.0, 18.0); }
inline double dampGain(double dampDb, int k) { return std::pow(10.0, -dampDb * k / 20.0); }
// -1..+1 across the field: the bounces alternate sides and wander in depth. k = 0 (the first hit) is centred.
inline double panFor(int k, double spread)
{
    if (k <= 0) return 0.0;
    return clampv(spread * 0.01, 0.0, 1.0) * ((k & 1) ? -1.0 : 1.0) * (0.6 + 0.4 * std::cos(0.9 * k));
}
inline void panGains(double p, double& gl, double& gr)
{
    const double a = (clampv(p, -1.0, 1.0) + 1.0) * 0.25 * kPi;
    gl = 1.41421356237 * std::cos(a);
    gr = 1.41421356237 * std::sin(a);
}
// a repeatable pseudo-random value in [-1, 1) for the Taps jitter
inline double hashNoise(int k)
{
    uint32_t x = (uint32_t)k * 2654435761u + 0x9E3779B9u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    return (double)(x >> 8) * (2.0 / 16777216.0) - 1.0;
}

constexpr double kLineSeconds = 8.6;        // the Taps line: the 8 s of the longest bounce plus the jitter
constexpr double kPrerollSeconds = 0.002;   // a transient drop includes this much of the input before the onset
constexpr double kMaxSliceSeconds = 0.5;
constexpr double kTiltHz = 1500.0;

class Engine {
public:
    static constexpr int kChunk = 32;
    static constexpr int kVoices = 8;      // 4 sounding + the ones fading out
    static constexpr int kLive = 4;
    static constexpr int kHits = 4;        // hits sounding at once in a drop (they overlap when FIT is off)
    static constexpr int kMaxGen = 32;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c) ring[c].setMax((int)std::ceil(kLineSeconds * sr) + 8);
        const int len = (int)std::ceil((kMaxSliceSeconds + kPrerollSeconds) * sr) + 8;
        for (Voice& v : voices) for (int c = 0; c < 2; ++c) v.buf[c].assign((size_t)len, 0.0f);
        fall = std::exp(-1.0 / (0.015 * sr));
        glT.setTime(0.06, sr / kChunk); glRho.setTime(0.05, sr / kChunk); glDamp.setTime(0.04, sr / kChunk);
        glSpread.setTime(0.04, sr / kChunk); glJit.setTime(0.06, sr / kChunk);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) ring[c].clear();
        for (Voice& v : voices) { v.used = false; for (Hit& h : v.hits) h.on = false; }
        for (int k = 0; k <= kMaxBounces; ++k) { delPrev[k] = delCur[k] = 0.0; gPrev[0][k] = gPrev[1][k] = gCur[0][k] = gCur[1][k] = 0.0f; }
        tapsPrev = tapsCur = 0;
        phase = 0;
        fast = 0.0; armed = true; holdoff = 0;
        syncCount = 0.0; syncLocked = false; trNew = false;
        pending = 0; pendingGain = 1.0f;
        glT.reset(std::log(firstGapSeconds(S.sync, S.note, S.timeMs, tr.bpm)));
        glRho.reset(gapRatio(S.gravity, S.rise)); glDamp.reset(S.dampDb); glSpread.reset(S.spread); glJit.reset(S.jitter);
        dryG.reset(dryTarget()); wetG.reset(wetTarget()); outG.reset(dbToGain(S.outDb));
        tapsG.reset(S.mode == kModeTaps ? 1.0 : 0.0);
        dryG.setRamp((int)(0.02 * sr)); wetG.setRamp((int)(0.02 * sr)); outG.setRamp((int)(0.02 * sr)); tapsG.setRamp((int)(0.04 * sr));
        eventSamples = 1e9; eventAgeFeed.store(1e3f, std::memory_order_relaxed);
        liveFeed.store(0, std::memory_order_relaxed);
    }

    void setSettings(const Settings& s)
    {
        S = s;
        dryG.setTarget(dryTarget());
        wetG.setTarget(wetTarget());
        outG.setTarget(dbToGain(S.outDb));
        tapsG.setTarget(S.mode == kModeTaps ? 1.0 : 0.0);
    }
    // the host transport at the start of the block (tempo for the synced times and the SYNC trigger)
    void setTransport(const Transport& t) { tr = t; trNew = true; }
    void setTempo(double bpm) { Transport t = tr; t.bpm = bpm; setTransport(t); }
    // the DROP button: a drop at the next sample (Drop mode, any trigger; in Taps the UI's ball still moves)
    void fire(float gain = 1.0f) { pending = std::min(pending + 1, 4); pendingGain = gain; }
    // a MIDI note-on: a drop when TRIGGER is MIDI
    void noteOn(float velocity) { if (S.trigger == kTrigMidi) fire(clampv(velocity, 0.0f, 1.0f)); }

    int latency() const { return 0; }

    // feeds for the UI (any thread)
    float tempo() const { return tempoFeed.load(std::memory_order_relaxed); }
    // seconds since the ball last dropped (Drop: a drop started; Taps: an onset in the input), 1000 when never
    float ballAge() const { return eventAgeFeed.load(std::memory_order_relaxed); }
    uint32_t events() const { return eventCount.load(std::memory_order_relaxed); }
    int liveDrops() const { return liveFeed.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n)
    {
        if (trNew) applyTransport();
        int i = 0;
        while (i < n)
        {
            if (phase == 0) chunkUpdate();
            const int m = std::min(n - i, kChunk - phase);
            for (int j = 0; j < m; ++j) sample(L[i + j], R[i + j], phase + j);
            i += m;
            phase += m;
            if (phase >= kChunk) phase = 0;
        }
        tempoFeed.store((float)tr.bpm, std::memory_order_relaxed);
    }

private:
    // ---- drop voices ----
    struct Hit {
        bool on = false, killing = false;
        int j = 0, dur = 0, fin = 1, fout = 1, killLeft = 0;
        double pos = 0.0, step = 1.0;
        float gl = 1.0f, gr = 1.0f, gLo = 1.0f, gHi = 1.0f;
        bool tilt = false;
        fx::OnePole lp[2];
    };
    struct Voice {
        bool used = false, releasing = false, capturing = false;
        std::vector<float> buf[2];
        int captured = 0, total = 0, pre = 0, gen = 0, M = 0, k = 1, relLeft = 0;
        double tv = 0.0, age = 0.0, endT = 0.0;
        double tm[kMaxBounces + 2] = {};      // when each hit of the current sequence starts (voice samples)
        float level = 1.0f;
        Settings p;                           // what the drop started with
        double T = 0.2, rho = 0.8, sliceSamples = 0.0;
        fx::Rng rng;
        Hit hits[kHits];
    };

    double dryTarget() const { return S.mix <= 50.0 ? 1.0 : (100.0 - S.mix) * 0.02; }
    double wetTarget() const { return std::min(1.0, S.mix * 0.02); }

    void applyTransport()
    {
        trNew = false;
        const double period = noteSeconds(S.every, tr.bpm) * sr;
        if (tr.valid && tr.playing)
        {
            const double div = noteQuarters(S.every);
            const double next = std::ceil(tr.ppq / div - 1e-9) * div;
            syncCount = std::max(0.0, (next - tr.ppq) * 60.0 / clampv(tr.bpm, 20.0, 999.0) * sr);
            syncLocked = true;
        }
        else
        {
            if (syncLocked) syncCount = period;   // the transport stopped: free-run from here
            syncLocked = false;
            if (syncCount > period) syncCount = period;
        }
    }

    // once per 32 samples: smoothed values and, in Taps mode, the tap table
    void chunkUpdate()
    {
        const double bpm = tr.bpm;
        const double T = firstGapSeconds(S.sync, S.note, S.timeMs, bpm);
        Ts = std::exp(glT.next(std::log(T)));
        rhoS = glRho.next(gapRatio(S.gravity, S.rise));
        dampS = glDamp.next(S.dampDb);
        spreadS = glSpread.next(S.spread);
        jitS = glJit.next(S.jitter);
        fbTaps = S.feedback * 0.01;

        eventSamples += kChunk;
        eventAgeFeed.store((float)std::min(1000.0, eventSamples / sr), std::memory_order_relaxed);

        const bool want = S.mode == kModeTaps || tapsG.isSmoothing() || tapsG.current() > 0.0;
        tapsOn = want;
        tapsPrev = tapsCur;
        for (int k = 0; k <= kMaxBounces; ++k) { delPrev[k] = delCur[k]; gPrev[0][k] = gCur[0][k]; gPrev[1][k] = gCur[1][k]; }
        if (!want) { tapsCur = 0; for (int k = 0; k <= kMaxBounces; ++k) gCur[0][k] = gCur[1][k] = 0.0f; return; }

        double t[kMaxBounces + 2];
        const int M = bounceTimes(Ts, rhoS, S.bounces, kMaxSeconds, t);
        const double maxD = (double)ring[0].maxDelay() - 4.0;
        double g = 1.0;
        const double dm = std::pow(10.0, -dampS / 20.0);
        for (int k = 1; k <= M; ++k)
        {
            g *= dm;
            const double before = t[k] - t[k - 1];
            const double after = k < M ? t[k + 1] - t[k] : before * rhoS;
            const double off = jitS * 0.01 * 0.4 * std::min(before, after) * hashNoise(k);
            const double d = clampv((t[k] + off) * sr, 2.0, maxD);
            double gl, gr;
            panGains(panFor(k, spreadS), gl, gr);
            delCur[k] = d;
            gCur[0][k] = (float)(g * gl);
            gCur[1][k] = (float)(g * gr);
            if (k > tapsPrev || (gPrev[0][k] == 0.0f && gPrev[1][k] == 0.0f)) delPrev[k] = d;   // a tap that appears does not slide in
        }
        for (int k = M + 1; k <= kMaxBounces; ++k) gCur[0][k] = gCur[1][k] = 0.0f;
        tapsCur = M;
    }

    void sample(float& l, float& r, int pos)
    {
        const float inL = l, inR = r;

        // ---- onsets (always: Taps shows them, Drop can trigger on them) ----
        const double a = std::max(std::fabs((double)inL), std::fabs((double)inR));
        fast = std::max(a, fast * fall);
        const double thr = dbToGain(clampv(S.thresholdDb, (double)kThresholdMin, 0.0));
        if (holdoff > 0) --holdoff;
        bool onset = false;
        if (!armed && fast < 0.5 * thr) armed = true;
        if (armed && a >= thr && holdoff == 0) { armed = false; holdoff = (int)(0.03 * sr); onset = true; }

        // ---- triggers ----
        const bool drop = S.mode == kModeDrop;
        if (onset)
        {
            if (drop && S.trigger == kTrigTransient) startDrop(1.0f, kPrerollSeconds);
            else if (!drop) markEvent();
        }
        if (syncCount <= 0.0)
        {
            syncCount += std::max(8.0, noteSeconds(S.every, tr.bpm) * sr);
            if (drop && S.trigger == kTrigSync) startDrop(1.0f, 0.0);
        }
        syncCount -= 1.0;
        while (pending > 0)
        {
            --pending;
            if (drop) startDrop(pendingGain, 0.0); else markEvent();
        }

        // ---- the wet signal ----
        double wl = 0.0, wr = 0.0;
        const double tg = tapsG.next();
        if (tapsOn)
        {
            const float f = (float)(pos + 1) * (1.0f / kChunk);
            const int nt = std::max(tapsPrev, tapsCur);
            float fbL = 0.0f, fbR = 0.0f;
            for (int k = 1; k <= nt; ++k)
            {
                const double d = delPrev[k] + (delCur[k] - delPrev[k]) * f;
                const float gL = gPrev[0][k] + (gCur[0][k] - gPrev[0][k]) * f, gR = gPrev[1][k] + (gCur[1][k] - gPrev[1][k]) * f;
                // the line is written after the read, so a delay of d samples reads d - 1 back
                const float xl = ring[0].read(d - 1.0), xr = ring[1].read(d - 1.0);
                wl += xl * gL; wr += xr * gR;
                if (k == tapsCur) { fbL = xl; fbR = xr; }
            }
            wl *= tg; wr *= tg;
            const float fb = (float)(fbTaps * (S.mode == kModeTaps ? 1.0 : 0.0));
            float wL = inL + fb * fbL, wR = inR + fb * fbR;
            if (std::fabs(wL) < 1e-20f) wL = 0.0f;
            if (std::fabs(wR) < 1e-20f) wR = 0.0f;
            ring[0].push(wL); ring[1].push(wR);
        }
        else { ring[0].push(inL); ring[1].push(inR); }

        int live = 0;
        for (Voice& v : voices)
        {
            if (!v.used) continue;
            renderVoice(v, inL, inR, wl, wr);
            if (v.used && !v.releasing) ++live;
        }
        if (pos == 0) liveFeed.store(live, std::memory_order_relaxed);

        if (std::fabs(wl) < 1e-30) wl = 0.0;   // no denormals out of a long decay
        if (std::fabs(wr) < 1e-30) wr = 0.0;
        const double dg = dryG.next(), wg = wetG.next(), og = outG.next();
        l = (float)((inL * dg + wl * wg) * og);
        r = (float)((inR * dg + wr * wg) * og);
    }

    void markEvent()
    {
        eventSamples = 0.0;
        eventCount.fetch_add(1, std::memory_order_relaxed);
        eventAgeFeed.store(0.0f, std::memory_order_relaxed);
    }

    // ---- starting a drop ----
    void startDrop(float gain, double preroll)
    {
        markEvent();
        int live = 0;
        Voice* oldest = nullptr;
        for (Voice& v : voices)
            if (v.used && !v.releasing) { ++live; if (!oldest || v.age > oldest->age) oldest = &v; }
        if (live >= kLive && oldest) { oldest->releasing = true; oldest->relLeft = std::max(16, (int)(0.003 * sr)); }
        Voice* v = nullptr;
        for (Voice& c : voices) if (!c.used) { v = &c; break; }
        if (!v)   // every slot is busy fading out: take the one furthest along
        {
            int best = 1 << 30;
            for (Voice& c : voices) if (c.releasing && c.relLeft < best) { best = c.relLeft; v = &c; }
            if (!v) v = &voices[0];
        }
        Voice& d = *v;
        d.used = true; d.releasing = false; d.relLeft = 0;
        d.age = 0.0; d.tv = 0.0; d.gen = 0;
        d.p = S;
        d.T = firstGapSeconds(S.sync, S.note, S.timeMs, tr.bpm);
        d.rho = gapRatio(S.gravity, S.rise);
        d.level = gain;
        d.rng.seed((uint32_t)(0x9E3779B9u * (uint32_t)(eventCount.load(std::memory_order_relaxed) + 1u)));
        for (Hit& h : d.hits) h.on = false;
        const int pre = (int)std::lround(preroll * sr);
        const int sliceN = std::max(16, (int)std::lround(clampv(S.sliceMs, (double)kSliceMinMs, (double)kSliceMaxMs) * 0.001 * sr));
        d.total = pre + sliceN;
        d.sliceSamples = sliceN;
        d.captured = pre; d.pre = pre;
        d.capturing = true;
        for (int i = 0; i < pre; ++i)   // the input before this sample
            for (int c = 0; c < 2; ++c) d.buf[c][(size_t)i] = ring[c].at(pre - 1 - i);
        // (the current sample is the first one captured after the pre-roll: renderVoice does it, from this same sample)
        schedule(d, 0.0, 1);
    }

    // the hit times of one sequence starting at voice time `origin`, from hit index `first`
    void schedule(Voice& v, double origin, int first)
    {
        double t[kMaxBounces + 2];
        v.M = bounceTimes(v.T, v.rho, v.p.bounces, kMaxSeconds, t);
        for (int k = 0; k <= v.M; ++k)
        {
            const double before = k > 0 ? t[k] - t[k - 1] : v.T;
            const double after = k < v.M ? t[k + 1] - t[k] : before * v.rho;
            const double off = v.p.jitter * 0.01 * 0.4 * std::min(before, after) * v.rng.bi();
            // the hit starts early by the pre-roll (at its playback speed), so the onset itself lands on the time
            const double r = k == 0 ? 1.0 : pitchRatio(v.p.pitch, k);
            const double at = origin + (k == 0 ? 0.0 : (t[k] + off) * sr);
            v.tm[k] = at - v.pre / r;
            if (k == v.M) v.endT = at;
        }
        v.k = first;
    }

    // one hit: the slice, played back at the bounce's pitch ratio, for the length that fits its gap
    void startHit(Voice& v, int k)
    {
        Hit* h = nullptr;
        for (Hit& c : v.hits) if (!c.on) { h = &c; break; }
        if (!h)   // all of them are sounding (FIT off): the oldest fades out fast
        {
            Hit* o = &v.hits[0];
            for (Hit& c : v.hits) if (c.j > o->j) o = &c;
            o->killing = true; o->killLeft = std::max(16, (int)(0.002 * sr));
            return;   // this one is skipped; the oldest is gone in 2 ms
        }
        const double r = k == 0 ? 1.0 : pitchRatio(v.p.pitch, k);
        const double gapAfter = v.T * std::pow(v.rho, k);
        double src = v.sliceSamples;
        if (v.p.fit) src = std::min(src, 0.92 * gapAfter * sr * r);
        src += v.pre;   // the pre-roll plays first (silence up to a transient's onset), so the content keeps its length
        // a faster playback must not run past what has been captured so far
        if (v.capturing && r > 1.0001) src = std::min(src, ((double)v.captured - 4.0) * r / (r - 1.0));
        src = std::max(8.0 * r, std::min(src, (double)v.total - 4.0));
        h->on = true; h->killing = false; h->j = 0;
        h->pos = 0.0; h->step = r;
        h->dur = std::max(16, (int)std::lround(src / r));
        const int half = h->dur / 2;
        h->fin = clampv((int)(h->dur * 0.25), 4, std::max(4, (int)(0.002 * sr)));
        h->fout = clampv((int)(h->dur * 0.35), 8, std::max(8, (int)(0.008 * sr)));
        h->fin = std::min(h->fin, half); h->fout = std::min(h->fout, half);
        double gl, gr;
        panGains(panFor(k, v.p.spread), gl, gr);
        const double lv = v.level * (k == 0 ? 1.0 : dampGain(v.p.dampDb, k));
        h->gl = (float)(lv * gl); h->gr = (float)(lv * gr);
        const double tilt = k == 0 ? 0.0 : tiltDb(v.p.tone, k);
        h->tilt = std::fabs(tilt) > 0.05;
        h->gHi = (float)dbToGain(0.5 * tilt); h->gLo = (float)dbToGain(-0.5 * tilt);
        for (int c = 0; c < 2; ++c) { h->lp[c].set(kTiltHz, sr); h->lp[c].reset(); }
    }

    static float cubic(const float* b, int len, double s)
    {
        const int i = (int)s;
        const float f = (float)(s - i);
        auto at = [&](int k) { return b[clampv(k, 0, len - 1)]; };
        const float ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

    void renderVoice(Voice& v, float inL, float inR, double& wl, double& wr)
    {
        if (v.capturing)
        {
            v.buf[0][(size_t)v.captured] = inL; v.buf[1][(size_t)v.captured] = inR;
            if (++v.captured >= v.total) v.capturing = false;
        }
        // the hits that start on this sample
        while (v.k <= v.M && v.tv >= v.tm[v.k]) { startHit(v, v.k); ++v.k; }
        if (v.k > v.M)
        {
            // every hit of this sequence is out: queue the next one (FEEDBACK), the slice dropped again from its start
            const double fb = v.p.feedback * 0.01;
            const float next = (float)(v.level * fb);
            if (fb > 0.0 && v.gen < kMaxGen && next > 0.003f && v.M > 0)
            {
                v.level = next; ++v.gen;
                schedule(v, v.endT + v.T * sr, 0);
                v.k = 0;
            }
        }
        const int len = v.capturing ? v.captured : v.total;
        double ol = 0.0, orr = 0.0;
        bool any = false;
        for (Hit& h : v.hits)
        {
            if (!h.on) continue;
            any = true;
            float sl = cubic(v.buf[0].data(), len, h.pos), sr_ = cubic(v.buf[1].data(), len, h.pos);
            if (h.tilt)
            {
                const double lo0 = h.lp[0].lp((double)sl), lo1 = h.lp[1].lp((double)sr_);
                sl = (float)(lo0 * h.gLo + ((double)sl - lo0) * h.gHi);
                sr_ = (float)(lo1 * h.gLo + ((double)sr_ - lo1) * h.gHi);
            }
            // the window: a raised cosine into and out of the hit
            float w = 1.0f;
            if (h.j < h.fin) w = 0.5f - 0.5f * std::cos((float)kPi * (float)h.j / (float)h.fin);
            const int left = h.dur - h.j;
            if (left < h.fout) w *= 0.5f - 0.5f * std::cos((float)kPi * (float)left / (float)h.fout);
            if (h.killing) { w *= (float)h.killLeft / (float)std::max(16, (int)(0.002 * sr)); if (--h.killLeft <= 0) h.on = false; }
            ol += sl * w * h.gl; orr += sr_ * w * h.gr;
            h.pos += h.step;
            if (++h.j >= h.dur) h.on = false;
        }
        if (v.releasing)
        {
            const double g = (double)v.relLeft / std::max(16, (int)(0.003 * sr));
            ol *= g; orr *= g;
            if (--v.relLeft <= 0) { v.used = false; for (Hit& h : v.hits) h.on = false; return; }
        }
        wl += ol; wr += orr;
        v.tv += 1.0; v.age += 1.0;
        if (!any && v.k > v.M) v.used = false;   // settled: nothing sounding, nothing queued
    }

    double sr = 48000.0;
    Settings S;
    Transport tr;
    bool trNew = false, syncLocked = false;
    int phase = 0;
    fx::FracDelay ring[2];
    Voice voices[kVoices];

    // control-rate values
    fx::Glide glT, glRho, glDamp, glSpread, glJit;
    double Ts = 0.2, rhoS = 0.8, dampS = 1.5, spreadS = 40.0, jitS = 0.0, fbTaps = 0.0;
    LinearSmoother dryG, wetG, outG, tapsG;
    bool tapsOn = true;
    int tapsPrev = 0, tapsCur = 0;
    double delPrev[kMaxBounces + 2] = {}, delCur[kMaxBounces + 2] = {};
    float gPrev[2][kMaxBounces + 2] = {}, gCur[2][kMaxBounces + 2] = {};

    // detector and triggers
    double fast = 0.0, fall = 0.999;
    bool armed = true;
    int holdoff = 0, pending = 0;
    float pendingGain = 1.0f;
    double syncCount = 0.0;

    // feeds
    double eventSamples = 1e9;
    std::atomic<float> eventAgeFeed { 1000.0f }, tempoFeed { 120.0f };
    std::atomic<uint32_t> eventCount { 0 };
    std::atomic<int> liveFeed { 0 };
};

}} // namespace pump::bounce
