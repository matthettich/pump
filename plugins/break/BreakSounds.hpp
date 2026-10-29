// pump BREAK: the built-in breaks, rendered by an acoustic-style drum kit in a small room onto tape. Framework-free.
//
// The kit (one shot per hit, summed into a stereo bar):
//   kick    a sine falling from about twice its pitch to it in 30 ms (the head), a beater click (low-passed noise),
//           a little of the shell's thump an octave up; soft-clipped
//   snare   two drum-head modes and the wires: noise through a band and a high-pass with its own, longer decay;
//           hard hits brighter, ghost notes quiet and short
//   hats    six detuned square partials high up and noise through a high-pass; closed short, open long (the next
//           closed hat chokes it)
//   ride    the partials lower and longer, a bell on top
//   toms    a sine with a small pitch drop and some noise
// Each hit is placed on its sixteenth with the break's swing and a drummer's drift (less for tight drummers), its
// velocity varied a little; hats pushed right, the ride left, toms across. Then the room (a small Schroeder reverb:
// four combs and two allpasses per side), and tape: saturation, a high cut set by BRIGHT, a slow wobble, a hint of hiss.
// The bar is rendered twice and the second kept, so the first bar's tails run into its start and it loops seamlessly.
#pragma once

#include "BreakParams.hpp"
#include "dsp/Fx.hpp"
#include "dsp/Synth.hpp"
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace pump { namespace brk {

// a loop to chop: stereo, its rate, its tempo and length in bars
struct Source {
    std::vector<float> L, R;
    double sr = 44100.0, bpm = 100.0;
    int bars = 1;
    std::string name;
    size_t size() const { return L.size(); }
};

namespace kitsynth {

struct Hit { int kind; double time, vel; int variant; double pan; };
enum Kind : int { kKick = 0, kSnare, kHatClosed, kHatOpen, kRide, kTom };

// render one hit into (L, R) from sample `at`
inline void renderHit(std::vector<float>& L, std::vector<float>& R, double sr, const Hit& h, const BreakScore& b, fx::Rng& rng)
{
    const long at = (long)std::lround(h.time * sr);
    const double v = clampv(h.vel, 0.0, 1.3);
    const double gl = std::cos((h.pan + 1.0) * kPi * 0.25) * 1.41421356, gr = std::sin((h.pan + 1.0) * kPi * 0.25) * 1.41421356;
    auto put = [&](long i, double y) { if (i >= 0 && i < (long)L.size()) { L[(size_t)i] += (float)(y * gl); R[(size_t)i] += (float)(y * gr); } };
    switch (h.kind)
    {
    case kKick:
    {
        const double f0 = 55.0 + 8.0 * b.bright, dec = 0.075 + 0.05 * (1.0 - b.tight);
        double ph = 0.0, ph2 = 0.0; fx::OnePole click; click.set(3500.0 + 2500.0 * b.bright, sr);
        const long n = (long)(sr * 0.6);
        for (long i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double f = f0 * (1.0 + 1.1 * std::exp(-t / 0.012));
            ph += f / sr; ph2 += 2.1 * f / sr;
            const double body = std::sin(kTwoPi * ph) * std::exp(-t / dec);
            const double shell = 0.25 * std::sin(kTwoPi * ph2) * std::exp(-t / 0.04);
            const double cl = click.lp(rng.bi()) * std::exp(-t / 0.0025) * 1.6;
            put(at + i, std::tanh(1.6 * (body + shell + cl) * v) * 0.9);
        }
        break;
    }
    case kSnare:
    {
        const bool ghost = h.variant == 1, hard = h.variant == 2;
        const double f1 = 185.0 + 20.0 * b.bright, f2 = f1 * 1.62;
        const double bodyDec = ghost ? 0.03 : 0.06, wireDec = ghost ? 0.035 : (0.065 + 0.03 * b.room);
        fx::Svf band; band.set(2600.0 + 2200.0 * b.bright + (hard ? 1500.0 : 0.0), 0.8, sr);
        fx::OnePole hp; hp.set(900.0, sr);
        double p1 = 0.0, p2 = 0.0;
        const long n = (long)(sr * 0.6);
        for (long i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double pm = 1.0 + 0.25 * std::exp(-t / 0.008);
            p1 += f1 * pm / sr; p2 += f2 * pm / sr;
            const double head = (0.7 * std::sin(kTwoPi * p1) + 0.45 * std::sin(kTwoPi * p2)) * std::exp(-t / bodyDec);
            const double wn = rng.bi();
            const double wires = (band.bandpass(wn) * 0.9 + hp.hp(wn) * 0.6) * std::exp(-t / wireDec);
            const double crack = hard ? rng.bi() * std::exp(-t / 0.004) * 0.8 : 0.0;
            put(at + i, std::tanh((head * 0.8 + wires * 1.1 + crack) * v * 1.2) * 0.75);
        }
        break;
    }
    case kHatClosed: case kHatOpen: case kRide:
    {
        static const double ratio[6] = { 1.0, 1.341, 1.567, 1.89, 2.131, 2.71 };
        const bool ride = h.kind == kRide, open = h.kind == kHatOpen;
        const double base = ride ? 520.0 : 3100.0 + 600.0 * b.bright;
        const double dec = ride ? 0.9 : (open ? 0.28 : 0.035 + 0.02 * (1.0 - b.tight));
        double ph[6]; for (double& p : ph) p = rng.uni();
        fx::OnePole hp; hp.set(ride ? 1800.0 : 6500.0, sr);
        fx::Svf band; band.set(ride ? 5200.0 : 9000.0 + 2000.0 * b.bright, 0.9, sr);
        const long n = (long)(sr * (ride ? 1.6 : (open ? 0.6 : 0.15)));
        const long cut = h.variant > 0 ? (long)(h.variant) : n;   // an open hat choked by the next closed one (samples)
        for (long i = 0; i < n; ++i)
        {
            const double t = i / sr;
            double m = 0.0;
            for (int k = 0; k < 6; ++k) { ph[k] += base * ratio[k] / sr; ph[k] -= std::floor(ph[k]); m += ph[k] < 0.5 ? 1.0 : -1.0; }
            m /= 6.0;
            const double x = band.bandpass(m * 0.6 + rng.bi() * 0.7);
            double env = std::exp(-t / dec) * (1.0 - std::exp(-t / 0.0006));
            if (i > cut) env *= std::exp(-(double)(i - cut) / (0.006 * sr));
            double y = hp.hp(x) * env;
            if (ride) y += 0.35 * std::sin(kTwoPi * 2.6 * base * t) * std::exp(-t / 0.25);   // the bell
            put(at + i, y * v * (ride ? 0.6 : 0.85));
        }
        break;
    }
    case kTom:
    {
        static const double tf[4] = { 0.0, 190.0, 145.0, 105.0 };
        const double f0 = tf[clampv(h.variant, 1, 3)];
        double ph = 0.0;
        fx::OnePole lp; lp.set(1500.0, sr);
        const long n = (long)(sr * 0.8);
        for (long i = 0; i < n; ++i)
        {
            const double t = i / sr;
            ph += f0 * (1.0 + 0.35 * std::exp(-t / 0.03)) / sr;
            const double y = std::sin(kTwoPi * ph) * std::exp(-t / 0.28) + lp.lp(rng.bi()) * std::exp(-t / 0.05) * 0.5;
            put(at + i, std::tanh(y * v * 1.2) * 0.75);
        }
        break;
    }
    default: break;
    }
}

// the room: four combs and two allpasses per side (lengths in ms, the right side a little longer)
struct Room {
    std::vector<float> comb[2][4], ap[2][2];
    int ci[2][4] = {}, ai[2][2] = {};
    double fb = 0.7, damp = 0.3, lpS[2][4] = {};
    void setup(double sr, double size)
    {
        static const double cms[4] = { 29.7, 37.1, 41.1, 43.7 }, ams[2] = { 5.0, 1.7 };
        for (int s = 0; s < 2; ++s)
        {
            for (int k = 0; k < 4; ++k) comb[s][k].assign((size_t)(sr * cms[k] * (0.6 + 0.4 * size) * (s ? 1.03 : 1.0) / 1000.0), 0.0f);
            for (int k = 0; k < 2; ++k) ap[s][k].assign((size_t)(sr * ams[k] * (s ? 1.07 : 1.0) / 1000.0), 0.0f);
        }
        fb = 0.62 + 0.2 * size; damp = 0.35;
    }
    double process(int s, double x)
    {
        double y = 0.0;
        for (int k = 0; k < 4; ++k)
        {
            std::vector<float>& b = comb[s][k];
            const double o = b[(size_t)ci[s][k]];
            lpS[s][k] = o * (1.0 - damp) + lpS[s][k] * damp;
            b[(size_t)ci[s][k]] = (float)(x + lpS[s][k] * fb);
            ci[s][k] = (ci[s][k] + 1) % (int)b.size();
            y += o;
        }
        y *= 0.25;
        for (int k = 0; k < 2; ++k)
        {
            std::vector<float>& b = ap[s][k];
            const double o = b[(size_t)ai[s][k]];
            const double w = y + o * 0.5;
            b[(size_t)ai[s][k]] = (float)w;
            ai[s][k] = (ai[s][k] + 1) % (int)b.size();
            y = o - w * 0.5;
        }
        return y;
    }
};

} // namespace kitsynth

// Render built-in break `i` at `sr`: one bar, looping.
inline std::shared_ptr<Source> renderBreak(int i, double sr = 44100.0)
{
    using namespace kitsynth;
    const BreakScore& b = kBreaks[clampv(i, 0, kNumBreaks - 1)];
    auto src = std::make_shared<Source>();
    src->sr = sr; src->bpm = b.bpm; src->bars = 1; src->name = b.name;
    const double bar = 240.0 / b.bpm, six = bar / 16.0;
    const size_t barN = (size_t)std::lround(bar * sr);
    std::vector<float> L(barN * 3 + (size_t)(sr * 2), 0.0f), R(L.size(), 0.0f);
    fx::Rng rng((uint32_t)(0xB4EAu + 131u * (uint32_t)i));
    std::vector<Hit> hits;
    // the score, three times over (the second bar is kept), each bar played the same way: swing pushes the off
    // sixteenths, the drummer drifts
    for (int rep = 0; rep < 3; ++rep)
    {
        fx::Rng feel((uint32_t)(0x5EE1u + 977u * (uint32_t)i));
        for (int k = 0; k < 16; ++k)
        {
            const double base = rep * bar + k * six + ((k & 1) ? b.swing * six : 0.0);
            auto when = [&](double spread) { return base + feel.bi() * (1.0 - b.tight) * spread + 0.004; };
            const double beat = (k % 4 == 0) ? 1.0 : ((k % 2 == 0) ? 0.85 : 0.72);
            if (b.k[k] == 'x') hits.push_back({ kKick, when(0.006), 0.95 + 0.08 * feel.bi(), 0, 0.0 });
            if (b.s[k] == 'X' || b.s[k] == 'x' || b.s[k] == 'g')
            {
                const int var = b.s[k] == 'g' ? 1 : (b.s[k] == 'X' ? 2 : 0);
                const double vel = var == 1 ? 0.22 + 0.06 * feel.uni() : (var == 2 ? 1.05 : 0.8) + 0.05 * feel.bi();
                hits.push_back({ kSnare, when(var == 1 ? 0.010 : 0.005) + (var == 2 ? 0.003 : 0.0), vel, var, 0.05 });
            }
            if (b.h[k] == 'x' || b.h[k] == 'o')
                hits.push_back({ b.h[k] == 'o' ? kHatOpen : kHatClosed, when(0.008), (0.55 + 0.35 * beat) + 0.08 * feel.bi(), 0, 0.3 });
            if (b.r[k] == 'x') hits.push_back({ kRide, when(0.008), (0.6 + 0.3 * beat) + 0.06 * feel.bi(), 0, -0.35 });
            if (b.t[k] >= '1' && b.t[k] <= '3') hits.push_back({ kTom, when(0.006), 0.9 + 0.06 * feel.bi(), b.t[k] - '0', b.t[k] == '1' ? 0.35 : (b.t[k] == '2' ? 0.0 : -0.35) });
        }
    }
    // open hats are choked by the next closed hat
    for (size_t a = 0; a < hits.size(); ++a)
    {
        if (hits[a].kind != kHatOpen) continue;
        double next = 1e9;
        for (const Hit& o : hits) if (o.kind == kHatClosed && o.time > hits[a].time && o.time < next) next = o.time;
        if (next < 1e8) hits[a].variant = (int)((next - hits[a].time) * sr);
    }
    for (const Hit& h : hits) renderHit(L, R, sr, h, b, rng);
    // the room and the tape, over both bars
    Room room; room.setup(sr, b.room);
    fx::OnePole tapeL, tapeR, hpL, hpR; tapeL.set(7000.0 + 9000.0 * b.bright, sr); tapeR.set(7000.0 + 9000.0 * b.bright, sr);
    hpL.set(28.0, sr); hpR.set(28.0, sr);
    const double wet = 0.10 + 0.32 * b.room, drive = 1.0 + 2.5 * b.tape;
    double wob = 0.0;
    fx::OnePole roomHp; roomHp.set(250.0, sr);   // the room hears little of the bass
    std::vector<float> outL(L.size()), outR(L.size());
    fx::FracDelay dl, dr; dl.setMax(256); dr.setMax(256);
    for (size_t n = 0; n < L.size(); ++n)
    {
        const double inL = L[n], inR = R[n];
        const double send = roomHp.hp(0.5 * (inL + inR));
        double l = inL + wet * room.process(0, send);
        double r = inR + wet * room.process(1, send);
        l = std::tanh(l * drive) / drive * 1.25; r = std::tanh(r * drive) / drive * 1.25;
        l = tapeL.lp(hpL.hp(l)); r = tapeR.lp(hpR.hp(r));
        // the wobble: a slowly moving short delay (wow and flutter)
        wob = 30.0 + b.tape * (6.0 * std::sin(kTwoPi * 2.0 * n / barN) + 1.5 * std::sin(kTwoPi * 29.0 * n / barN));   // whole cycles per bar: the loop stays seamless
        dl.push((float)l); dr.push((float)r);
        l = dl.read(wob); r = dr.read(wob);
        l += b.tape * 0.0015 * rng.bi(); r += b.tape * 0.0015 * rng.bi();
        outL[n] = (float)l; outR[n] = (float)r;
    }
    // the second bar (its start carries the first bar's tails), aligned for the delay; its first few milliseconds
    // crossfade in from what follows its end (the third bar's start), so the loop point joins without a step
    const size_t off = barN + 30;
    src->L.assign(outL.begin() + (long)off, outL.begin() + (long)(off + barN));
    src->R.assign(outR.begin() + (long)off, outR.begin() + (long)(off + barN));
    const size_t xf = (size_t)(0.003 * sr);
    for (size_t j = 0; j < xf; ++j)
    {
        const double t = (double)j / xf;
        src->L[j] = (float)(outL[off + barN + j] * (1.0 - t) + src->L[j] * t);
        src->R[j] = (float)(outR[off + barN + j] * (1.0 - t) + src->R[j] * t);
    }
    // level: about -14 dBFS RMS
    double e = 0.0; for (size_t n = 0; n < barN; ++n) e += 0.5 * ((double)src->L[n] * src->L[n] + (double)src->R[n] * src->R[n]);
    const double g = 0.2 / std::sqrt(e / (double)barN + 1e-12);
    for (size_t n = 0; n < barN; ++n) { src->L[n] = (float)(src->L[n] * g); src->R[n] = (float)(src->R[n] * g); }
    return src;
}

// The built-in breaks, rendered once per process on a worker thread (a second or two for all of them), started when a
// BREAK is activated; the one asked for goes first. Plugins retain() and release() it: when the last one goes the thread
// is stopped and joined (an unfinished set carries on at the next start), so it never outlives the plugins (PRISM's
// built-ins work the same way). ready(i) is lock-free for the audio thread (nullptr until that break is done).
class Breaks {
public:
    static Breaks& get() { static Breaks b; return b; }
    void retain() { std::lock_guard<std::mutex> g(m); ++users; }
    void release() { std::lock_guard<std::mutex> g(m); if (--users <= 0) { users = 0; stopLocked(); } }
    void start(int first)
    {
        std::lock_guard<std::mutex> g(m);
        if (running.load() || allReady()) return;
        if (th.joinable()) th.join();
        quit.store(false); running.store(true);
        const int f = clampv(first, 0, kNumBreaks - 1);
        th = std::thread([this, f] {
            renderOne(f);
            for (int i = 0; i < kNumBreaks && !quit.load(); ++i) renderOne(i);
            running.store(false);
        });
    }
    // for the tests: everything now, on this thread
    void renderAllNow() { std::lock_guard<std::mutex> g(m); stopLocked(); for (int i = 0; i < kNumBreaks; ++i) renderOne(i); }
    const Source* ready(int i) const { return (i >= 0 && i < kNumBreaks) ? ptr[i].load(std::memory_order_acquire) : nullptr; }
    ~Breaks() { std::lock_guard<std::mutex> g(m); stopLocked(); }

private:
    Breaks() = default;
    bool allReady() const { for (int i = 0; i < kNumBreaks; ++i) if (!ptr[i].load()) return false; return true; }
    void renderOne(int i) { if (!ptr[i].load(std::memory_order_acquire)) { keep[i] = renderBreak(i); ptr[i].store(keep[i].get(), std::memory_order_release); } }
    void stopLocked() { quit.store(true); if (th.joinable()) th.join(); }
    std::mutex m;
    int users = 0;
    std::thread th;
    std::atomic<bool> quit { false }, running { false };
    std::shared_ptr<Source> keep[kNumBreaks];
    std::atomic<const Source*> ptr[kNumBreaks] {};
};

}} // namespace pump::brk
