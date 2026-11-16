// pump FLEX engine: re-sequences the last bar or two in time with the song. Framework-free (also built to WebAssembly).
//
// A pattern is a time-remap: it maps the bar position p (beats 0-4, the song's position modulo a bar) to the position r
// it plays from (r <= p, so it only ever reads the past: no latency). Repeats, reverses, half speed, tape stops,
// scratches, gates, shuffles, pitch moves... all are a different r(p) (and sometimes a gain, a filter or a bit crusher).
// Jumps of the read head crossfade (XF ms). The input is recorded all the time, whether or not a pattern is playing, so
// switching one on mid-bar already has the bar to play from. The 511 patterns (FLX 01-FF, FLY 01-FF) are in FlexTable.hpp.
//
// Time. The beat clock is the host's position in quarter notes; the engine uses it modulo 16 (four 4/4 bars, which is
// also the cycle the "new every bar" patterns follow): the 4/4 grid is assumed, whatever the host's own bar length is.
// It keeps its own song position (`ph`, double, advanced a sample at a time and locked to the host's position at the start
// of each block: gently while close, at once when it jumps), and a glided samples-per-beat (`sp`), so the read head moves
// smoothly. With the host stopped (or sending no musical position) there is no clock and the signal passes straight through.
//
// A pattern change is latched at the start of a block (the block's flx value): the read head moves and crossfades.
// The maths is that of Thunder Plus's Flex effect, sample for sample; the only difference is that the host gives the
// block's start position and tempo instead of a clock value for every sample (tests/test_flex.cpp checks it against the
// original's output, tools/flex-ref.mjs).
#pragma once

#include "dsp/Tempo.hpp"
#include "dsp/Util.hpp"
#include "FlexTable.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pump { namespace flex {

constexpr double kPi = 3.14159265358979323846;

struct Settings {
    int flx = 0;               // 0 = off, 1-255 FLX, 256 + v FLY v (see slotToFlx)
    double mix = 100.0;        // %
    double outDb = 0.0;
    double xfadeMs = 5.0;      // the crossfade when the read head jumps
};

class Engine {
public:
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        N = (int)std::ceil(sr * 13.0);
        bL.assign((size_t)N, 0.0f); bR.assign((size_t)N, 0.0f);
        tab = &table();    // built here (never on the audio thread)
        w = 0; D = 0.0; xo = 0.0; xc = 0; xn = 1; g = 1.0; pg = 1.0;
        spb = sp = sr / 2; ph = -1.0; lockQ = -1.0; lockOn = false;
        slp = 1.0; shp = 0.0;
        for (double& f : fs) f = 0.0;
        for (int i = 0; i < 3; ++i) fa[i] = fb[i] = 0.0;
        fk = 0; hc = 0.0; hl = hr = 0.0;
        mixS.setRamp(std::max(1, (int)(0.02 * sr))); mixS.reset(set.mix * 0.01);
        outS.setRamp(std::max(1, (int)(0.02 * sr))); outS.reset(dbToGain(set.outDb));
        phaseFeed.store(-1.0f, std::memory_order_relaxed); kFeed.store(0, std::memory_order_relaxed);
    }
    void setSettings(const Settings& s)
    {
        set = s;
        mixS.setTarget(set.mix * 0.01);
        outS.setTarget(dbToGain(set.outDb));
    }
    // The host transport at the start of the block (call once per host block, before process()). Beats are quarter notes;
    // the clock is ppq modulo 16, and absent (pass-through) unless the host plays with a valid musical position.
    void setTransport(const Transport& t)
    {
        tr = t;
        lockOn = true;
        const bool live = t.playing && t.valid && std::isfinite(t.ppq) && std::fabs(t.ppq) < 1e9 && t.ppq >= 0.0;
        lockQ = live ? std::fmod(t.ppq, 16.0) : -1.0;
        const double bpm = clampv(t.bpm, 20.0, 999.0);
        spbHost = sr * 60.0 / bpm;
    }
    int latency() const { return 0; }

    // feeds for the window: the song position in beats within the 16-beat cycle (-1 = no clock), and the pattern playing
    float phaseNow() const { return phaseFeed.load(std::memory_order_relaxed); }
    int patternNow() const { return kFeed.load(std::memory_order_relaxed); }

    // In place. A host block may be split into several calls: the position carries on from where the last call ended.
    void process(float* L, float* R, int n)
    {
        if (n <= 0) return;
        const int k = clampv(set.flx, 0, 511);
        const double fsk = 1 - std::exp(-1 / (sr * 0.015)), nyq = sr * 0.45;
        const int XF = std::max(16, jsRound(sr * set.xfadeMs / 1000));
        const double gk = 1 - std::exp(-1 / (sr * 0.002));
        // Lock our position to the clock: gently while close, at once when it jumps.
        if (lockOn)
        {
            lockOn = false;
            const double q0 = lockQ;
            if (q0 < 0) ph = -1;
            else if (ph < 0) ph = q0;
            else { double e = q0 - ph; e -= 16 * jsRound(e / 16); if (std::fabs(e) > 0.004) ph = q0; else ph += e * 0.1; }
            spb = spbHost;
            if (std::fabs(spb / sp - 1) > 0.02) sp = spb;      // a real tempo change: follow at once (that jump crossfades)
        }
        const double sk = 1 - std::exp(-1 / (sr * 0.1));
        const Spec* T = tab->data();
        for (int i = 0; i < n; ++i)
        {
            const int wi = w; bL[(size_t)wi] = L[i]; bR[(size_t)wi] = R[i];
            sp += (spb - sp) * sk;
            const double q = ph; if (q >= 0) { ph += 1 / sp; if (ph >= 16) ph -= 16; }
            double Dn = 0, gg = 1, lpn = 1, hpn = 0, crn = 1, btn = 0;
            if (k > 0 && q >= 0)
            {
                const double p = std::fmod(q, 4.0); const int bar = (int)std::floor(q / 4);
                double r;
                if (k > 16) { r = ext(T, k, p, bar); lpn = plp; hpn = php; crn = pcr; btn = pbt; }
                else r = pat(k, p, bar);
                gg = pg;
                Dn = std::max(0.0, std::min((double)(N - 4), (p - r) * sp));
            }
            if (std::fabs(Dn - D) > 8) { xo = D; xc = XF; xn = XF; }
            D = Dn; g += (gg - g) * gk;
            double yl = rd(bL.data(), wi, D), yr = rd(bR.data(), wi, D);
            if (xc > 0) { const double f = (double)xc / xn; yl += (rd(bL.data(), wi, xo) - yl) * f; yr += (rd(bR.data(), wi, xo) - yr) * f; --xc; }
            // CRUSH (hold every cr samples, bt bits) and FILTER (12 dB low- and high-pass state-variable filters)
            if (crn > 1.01) { if (++hc >= crn) { hc -= crn; hl = yl; hr = yr; } yl = hl; yr = hr; } else { hc = 0; hl = yl; hr = yr; }
            if (btn > 0) { const double qz = std::pow(2.0, btn - 1); yl = std::floor(yl * qz + 0.5) / qz; yr = std::floor(yr * qz + 0.5) / qz; }
            slp += (lpn - slp) * fsk; shp += (hpn - shp) * fsk;
            if (slp < 0.995 || shp > 0.005)
            {
                if (!(fk++ & 7)) { svc(fa, 40 * std::pow(2.0, slp * 9), nyq); svc(fb, 20 * std::pow(2.0, shp * 9), nyq); }
                if (slp < 0.995) { yl = svf(0, yl, fa, false); yr = svf(2, yr, fa, false); }
                if (shp > 0.005) { yl = svf(4, yl, fb, true); yr = svf(6, yr, fb, true); }
            }
            else if (fs[0] != 0 || fs[1] != 0 || fs[4] != 0) for (double& f : fs) f = 0.0;
            const double mx = mixS.next(), og = outS.next();
            L[i] = (float)(L[i] + (yl * g - L[i]) * mx);
            R[i] = (float)(R[i] + (yr * g - R[i]) * mx);
            if (og != 1.0) { L[i] = (float)(L[i] * og); R[i] = (float)(R[i] * og); }
            w = wi + 1 >= N ? 0 : wi + 1;
        }
        phaseFeed.store(ph >= 0 ? (float)ph : -1.0f, std::memory_order_relaxed);
        kFeed.store(k, std::memory_order_relaxed);
    }

private:
    // ---- helpers (as in the original) ----
    static double cs(double p, double C) { return std::floor(p / C + 1e-9) * C; }
    static double lr(const double* v, double t) { return v[0] + (v[1] - v[0]) * t; }
    static bool eon(const Spec& o, int Nn, int j) { return ((((j + o.rot) % Nn) * (int)o.k) % Nn) < (int)o.k; }
    // a hash of three integers in [0, 1): the patterns' dice. The products are exact in double and wrap to 32 bits the way
    // the original's `^` does; the rest is 32-bit integer arithmetic.
    static double hh(int a, int b, int c)
    {
        auto wrap = [](double v) { return (uint32_t)(int64_t)v; };   // exact: |v| < 2^53, and the int64 -> uint32 conversion keeps the low 32 bits
        uint32_t x = wrap((double)a * 73856093.0) ^ wrap((double)b * 19349663.0) ^ wrap((double)c * 83492791.0);
        uint32_t h = x * 2654435761u;
        h ^= h >> 15; h *= 2246822519u; h = (h ^ (h >> 13));
        return (double)h / 4294967296.0;
    }
    static constexpr double kTup[5] = { 0, 0, 1, 2, 3 };
    static constexpr int kMrr[3] = { kProbMute, kProbRep, kProbRat };
    static constexpr int kSkp[4] = { kSkipStuck, kSkipJump, kSkipDrop, 100 };   // 100: "prev"
    static constexpr double kSte[7] = { 1. / 2, 1. / 3, 1. / 4, 1. / 6, 1. / 8, 1. / 12, 1. / 16 };
    static constexpr double kCp[4] = { 0.5, 0.75, 1.5, 2 };
    static constexpr int kP13[16] = { 0, 1, 2, 3, 4, 5, 2, 7, 8, 9, 10, 11, 8, 13, 6, 15 };

    // the filter's coefficients for cutoff f, into o; and one sample through a filter stage
    void svc(double* o, double f, double nyq) const
    {
        const double gg = std::tan(kPi * std::min(nyq, f) / sr), kk = 1.1, a1 = 1 / (1 + gg * (gg + kk));
        o[0] = a1; o[1] = gg * a1; o[2] = gg * gg * a1;
    }
    double svf(int j, double x, const double* c, bool hi)
    {
        const double v3 = x - fs[j + 1], v1 = c[0] * fs[j] + c[1] * v3, v2 = fs[j + 1] + c[1] * fs[j] + c[2] * v3;
        fs[j] = 2 * v1 - fs[j]; fs[j + 1] = 2 * v2 - fs[j + 1];
        return hi ? x - 1.1 * v1 - v2 : v2;
    }

    // 17-511: sets pg (gain), plp / php (filter, 0-1; 1 / 0 = open), pcr / pbt (hold, bits; 1 / 0 = clean); returns the read position
    double ext(const Spec* T, int k, double p, int bar)
    {
        const Spec& o = T[k];
        double r = p, g = 1, lp = 1, hp = 0, cr = 1, bt = 0;
        switch (o.f)
        {
        case Fam::None: break;
        case Fam::Roll: if (p >= o.s) r = o.s + std::fmod(p - o.s, o.L); break;
        case Fam::Stut: { const double c = cs(p, o.C); r = c + std::fmod(p - c, o.L); } break;
        case Fam::Build: {
            const double* s = nullptr;
            for (int j = 0; j < o.nsg; ++j) if (p >= o.sg[j][0]) s = o.sg[j];
            if (s) r = s[0] + std::fmod(p - s[0], s[1]);
        } break;
        case Fam::Rev: {
            const double C = o.C, c = cs(p, C); const int i = jsRound(c / C); const double u = p - c;
            const bool on = o.mode == kRevAll || o.mode == kRevHalf || (o.mode == kRevAlt && (i & 1)) || (o.mode == kRevLast && c >= 3 - 1e-9) || (o.mode == kRevRnd && hh(i, k, 5) < 0.5);
            if (on) r = c - (o.mode == kRevHalf ? u / 2 : u);
        } break;
        case Fam::Down: if (p >= o.a) { const double c = o.a + std::floor((p - o.a) / o.C + 1e-9) * o.C; r = c + (p - c) * o.s; } break;
        case Fam::Up: {
            const double C = o.C, c = cs(p, C); const int i = jsRound(c / C);
            if (o.mode == kUpAlways || (o.mode == kUpLast && c >= 3 - 1e-9) || (o.mode == kUpAlt && (i & 1))) r = c - C * (o.s - 1) + (p - c) * o.s;
        } break;
        case Fam::Stop: {
            const double a = o.C != 0 ? cs(p, o.C) : o.a, W = o.C != 0 ? o.C : o.b - o.a;
            if (p >= a) { const double u = std::min(p - a, W); r = a + u - u * u / (2 * W); if (u > W * 0.85) g = std::max(0.0, (W - u) / (W * 0.15)); }
        } break;
        case Fam::Start: {
            const double a = o.C != 0 ? cs(p, o.C) : o.a, W = o.C != 0 ? o.C : o.b - o.a;
            // the spin-up runs 4t - 3t^2 (0 -> peak 1.33x -> 1x): from its own downbeat, never reading ahead (r <= p), landing on the grid
            if (p >= a && p < a + W) { const double u = p - a, t = u / W; r = a + W * t * t * (2 - t); g = std::min(1.0, u / (W * 0.2)); }
        } break;
        case Fam::Wow: r = p - o.d * (1 - std::cos(2 * kPi * p / o.T)) / 2 - (o.d2 != 0 ? o.d2 * (1 - std::cos(2 * kPi * p / o.T2)) / 2 : 0); break;
        case Fam::Scr: {
            const double C = o.C, c = cs(p, C), u = (p - c) / C;
            r = c - C * o.d + C * o.d * (1 - std::cos(2 * kPi * o.k * u)) / 2;
            if (o.gt == 1) g = std::sin(2 * kPi * o.k * u) >= 0 ? 1 : 0; else if (o.gt == 2) g = std::fmod(p * 16, 1.0) < 0.5 ? 1 : 0;
        } break;
        case Fam::Gate: g = std::fmod(p / o.g, 2.0) < 1 ? 1 : 0; break;
        case Fam::Pump: { const double u = std::fmod(p, o.C) / o.C; g = std::min(1.0, u * 3); } break;
        case Fam::Swell: g = std::fmod(p, o.C) / o.C; break;
        case Fam::Fade: g = 1 - p / 4; break;
        case Fam::Mask: g = (o.mask >> (15 - std::min(15, (int)std::floor(p * 4)))) & 1; break;
        case Fam::Filt: {
            const double t = o.a != 0 ? std::max(0.0, (p - o.a) / (4 - o.a)) : p / 4;
            if (o.hasLp) lp = lr(o.lp, t);
            if (o.hasHp) hp = lr(o.hp, t);
            if (o.hasLpw) lp = o.lpw[1] + o.lpw[2] * std::cos(2 * kPi * p * o.lpw[0]);
            if (o.hasHpw) hp = std::max(0.0, o.hpw[1] - o.hpw[2] * std::cos(2 * kPi * p));
            if (o.bw) { hp = 0.05 + 0.7 * t; lp = hp + 0.25; }
            if (o.lpb) lp = 1 - 0.8 * std::fmod(p, 1.0);
        } break;
        case Fam::Crush: {
            bool on = true;
            if (o.hasBeats) on = (o.beatsMask >> (int)std::floor(p)) & 1;
            if (o.alt != 0) on = ((int)std::floor(p / o.alt) & 1) != 0;
            if (o.a != 0) on = p >= o.a;
            if (on) { cr = o.hasHs ? o.hs[0] + (o.hs[1] - o.hs[0]) * p / 4 : (o.ch != 0 ? o.ch : 1); bt = o.cb; if (o.clp != 0) lp = o.clp; }
        } break;
        case Fam::Echo: { const double c = cs(p, o.C); const double n = std::floor((p - c) / o.L + 1e-9); r = c + (p - c) - n * o.L; g = std::pow(o.k, n); } break;
        case Fam::Jug: {
            const double G = o.G; const int per = jsRound(4 / G); const int s = std::min(per - 1, (int)std::floor(p / G + 1e-9)); const double u = p - s * G;
            if (hh(o.seed, s, 11) < o.c)
            {
                const int src = s - 1 - (int)std::floor(hh(o.seed, s, 23) * (per - 1)); const bool rv = hh(o.seed, s, 31) < o.rv;
                r = src * G + (rv ? G - u : u);
            }
        } break;
        case Fam::Swing: {
            const double C = o.C, c = cs(p, C), u = (p - c) / C, h = 0.5 + o.a;
            r = c + C * (u < h ? u * 0.5 / h : 0.5 + (u - h) * 0.5 / (1 - h));
        } break;
        case Fam::Tup: {
            const double C = o.C, c = cs(p, C), u = (p - c) / C;
            const int j = std::min((int)o.q - 1, (int)std::floor(u * o.q + 1e-9));
            const double sj = (int)o.q == 5 ? kTup[j] : (double)j;
            r = c + C * (sj / 4 + (u - j / o.q));
        } break;
        case Fam::Drunk: { const int s = (int)std::floor(p * 4); r = p - hh(bar & 3, s, 41) * 0.06; } break;
        case Fam::Lag: r = std::fmod(p, 1.0) >= 0.5 ? p - 1. / 16 : p; break;
        case Fam::Chaos: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double c = s * G, u = p - c; const int key = o.nb ? (bar & 3) + 1 : 0;
            if (hh(o.seed, s, key * 7 + 3) < o.c)
            {
                const int op = (int)std::floor(hh(o.seed, s, key * 7 + 4) * 7);
                if (op == 0) r = p - G; else if (op == 1) r = c - u; else if (op == 2) r = c - G + u * 2; else if (op == 3) r = c + u / 2;
                else if (op == 4) r = c + std::fmod(u, G / 4); else if (op == 5) g = u < G / 2 ? 1 : 0; else r = c + u - u * u / (2 * G);
            }
        } break;
        // ===== FLY: each reads only what has played (r <= p)
        case Fam::PRoll: if (p >= o.a)
        {
            const double nn = std::floor((p - o.a) / o.L + 1e-9), u = p - o.a - nn * o.L;   // repeat n: speed <= n + 1 keeps it behind now
            if (nn > 0) r = o.a + u * std::min(nn + 1, std::max(0.25, std::pow(2.0, nn * o.s / 12)));
        } break;
        case Fam::Euc: {
            const int Nn = o.Ne; const double step = 4.0 / Nn; const int s = std::min(Nn - 1, (int)std::floor(p / step + 1e-9));
            if (!o.euh) g = eon(o, Nn, s) ? 1 : 0;
            else if (!eon(o, Nn, s)) { int j = s - 1; while (j >= 0 && !eon(o, Nn, j)) j--; if (j >= 0) { const double c0 = j * step; r = c0 + std::fmod(p - c0, step); } }
        } break;
        case Fam::Prob: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double c0 = s * G, u = p - c0; const int B = bar & 255;
            if (hh(k, s, B * 13 + 1) < o.c)
            {
                int op = o.mode; if (op == kProbAny) op = kMrr[(int)std::floor(hh(k, s, B * 13 + 2) * 3)];
                if (op == kProbMute) g = 0; else if (op == kProbRep) r = p - G;
                else r = c0 + std::fmod(u, G / (o.x != 0 ? o.x : 2 + std::floor(hh(k, s, B * 13 + 3) * 3)));
            }
        } break;
        case Fam::SPitch: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double c0 = s * G, u = p - c0; const int B = o.nb ? bar & 255 : 0;
            if (hh(k, s, B * 17 + 1) < o.c) { const double sp = o.S[(int)std::floor(hh(k, s, B * 17 + 2) * o.nS)]; r = sp > 1 ? c0 - G * (sp - 1) + u * sp : c0 + u * sp; }
        } break;
        case Fam::Frz: {
            double a, b;
            if (o.C != 0) { const double c = cs(p, o.C); a = c + o.h * o.C; b = c + o.C; } else { a = o.a; b = o.b; }
            if (p >= a && p < b && (o.pr >= 1 || hh(k, jsRound(a * 64), bar & 255) < o.pr))
            {
                const double u = p - a, w = o.sw != 0 ? o.w * (1 - o.sw * u / (b - a)) : o.w;
                r = a + std::floor(u / w + 1e-9) * w * o.rho + std::fmod(u, w);
            }
        } break;
        case Fam::Skip: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double c0 = s * G, u = p - c0; const int B = o.nb ? bar & 255 : 0;
            if (hh(k, s, B * 19 + 1) < o.d)
            {
                const int op = o.mode != 0 ? o.mode : kSkp[(int)std::floor(hh(k, s, B * 19 + 2) * 4)];
                if (op == kSkipStuck) r = c0 + std::fmod(u, G / 8);
                else if (op == kSkipJump) r = p - G * (1 + std::floor(hh(k, s, B * 19 + 3) * 3)) / 2;
                else if (op == kSkipDrop) g = u < G * 0.35 ? 0 : 1;
                else r = c0 - G / 4 + std::fmod(u, G / 4);
            }
        } break;
        case Fam::Bnc: {
            const double a = o.C != 0 ? cs(p, o.C) : o.a, b = o.C != 0 ? a + o.C : 4;
            if (p >= a && p < b)
            {
                const double x = p - a, q = o.q, Lm = 1.0 / 128; double n, c;   // repeat n lasts L0 * q^n (q < 1 faster, > 1 slower)
                if (q > 1) { n = std::floor(std::log(1 + x * (q - 1) / o.L0) / std::log(q) + 1e-9); c = o.L0 * (std::pow(q, n) - 1) / (q - 1); }
                else
                {
                    const double nm = std::max(0.0, std::ceil(std::log(Lm / o.L0) / std::log(q))), Sm = o.L0 * (1 - std::pow(q, nm)) / (1 - q);
                    if (x < Sm) { n = std::floor(std::log(1 - x * (1 - q) / o.L0) / std::log(q) + 1e-9); c = o.L0 * (1 - std::pow(q, n)) / (1 - q); }
                    else { const double m = std::floor((x - Sm) / Lm + 1e-9); n = nm + m; c = Sm + m * Lm; if (o.dk < 1) g = 0; }   // a bounce stops; a roll buzzes on
                }
                if (c > x) c = x;
                r = a + x - c; if (o.dk < 1 && g != 0) g = std::pow(o.dk, n); if (o.sw != 0) g = std::min(1.0, 0.25 * std::pow(1.2, n));
            }
        } break;
        case Fam::Mut: {
            const double G = o.G; const int per = jsRound(4 / G); const int s = std::min(per - 1, (int)std::floor(p / G + 1e-9)); const double u = p - s * G; int key = 0;
            if (o.cyc) { const int B = bar % o.cyc; if (B && hh(k, s, 97 + B) < o.m) key = 100 + B; }
            else for (int B = bar & 3; B > 0; B--) if (hh(k, s, 97 + B) < o.m) { key = 100 + B; break; }   // the changes add up over the four bars
            if (hh(k, s, key * 7 + 11) < o.c)
            {
                const int src = s - 1 - (int)std::floor(hh(k, s, key * 7 + 23) * (per - 1)); const bool rv = hh(k, s, key * 7 + 31) < o.rv;
                r = src * G + (rv ? G - u : u);
            }
        } break;
        case Fam::Sted: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double c0 = s * G, u = p - c0; const int B = o.nb ? bar & 255 : 0;
            if (hh(k, s, B * 23 + 1) < o.d)
            {
                const double Lr = G * kSte[(int)std::floor(hh(k, s, B * 23 + 2) * 7)]; const double nn = std::floor(u / Lr + 1e-9), v = u - nn * Lr;
                r = c0 + v * (o.s != 0 && nn > 0 ? std::min(nn + 1, std::max(0.25, std::pow(2.0, nn * o.s / 12))) : 1);
                if (o.fd != 0) g = std::pow(o.fd, nn);
                if (o.gt) g = v < Lr / 2 ? 1 : 0;
            }
        } break;
        case Fam::Poly: if (p >= o.a) { const double x = p - o.a, nn = std::floor(x / o.M + 1e-9), u = x - nn * o.M; r = o.a + (o.pp && ((int)nn & 1) ? o.M - u : u); } break;
        case Fam::Warp: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double c0 = s * G, u = p - c0, K = o.k;
            const bool up = o.mode == kWarpUp ? true : o.mode == kWarpDown ? false : o.mode == kWarpZigzag ? !(s & 1) : hh(k, s, (bar & 255) * 29 + 1) < 0.5;
            r = up ? c0 + (1 - K) * u + K * u * u / G : c0 - K * G / 4 + (1 + K) * u - K * u * u / G;   // up: slow -> fast, lands on the grid; down: fast -> slow
        } break;
        case Fam::RRoll:
            if (o.mode == kRRollRew)
            {
                const double a = o.C != 0 ? cs(p, o.C) + o.h * o.C : o.a, b = o.C != 0 ? cs(p, o.C) + o.C : 4;
                if (p >= a && p < b) { r = a - o.k * (p - a); g = std::min(1.0, (b - p) / ((b - a) * 0.1)); }
            }
            else if (p >= o.a)
            {
                const double L = o.L, nn = std::floor((p - o.a) / L + 1e-9), u = p - o.a - nn * L;
                if (nn > 0)
                {
                    const double sp = o.s != 0 ? std::max(0.25, std::pow(2.0, nn * o.s / 12)) : 1;
                    r = o.mode == kRRollRev || ((int)nn & 1) ? o.a + L - u * sp : o.a + u;
                    if (o.sw != 0) g = std::min(1.0, 0.2 + 0.8 * (p - o.a) / (4 - o.a));
                }
            }
            break;
        case Fam::Grain: if (p >= o.a)
        {
            const double w = o.w; const int j = (int)std::floor(p / w + 1e-9); const double u = p - j * w; const int B = bar & 255; const double off = w + hh(k, j, B * 31 + 1) * (o.W - w);
            r = hh(k, j, B * 31 + 2) < o.rv ? p - off + w - 2 * u : p - off;
            if (o.gap != 0 && hh(k, j, B * 31 + 3) < o.gap) g = 0;
        } break;
        case Fam::Tex: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double u = p - s * G; const int B = o.nb ? bar & 255 : 0;
            if (hh(k, s, B * 37 + 1) < o.d)
            {
                const double h2 = hh(k, s, B * 37 + 2), h3 = hh(k, s, B * 37 + 3);
                // the effect a slice gets: 0 crush, 1 low-pass, 2 high-pass, 3 chop
                static const int t4[4] = { 0, 1, 2, 3 }, t3[3] = { 0, 1, 2 };
                const int op = o.mode == kTexAll ? t4[(int)std::floor(h2 * 4)] : o.mode == kTexCF ? t3[(int)std::floor(h2 * 3)] : o.mode == kTexFilt ? (h2 < 0.5 ? 1 : 2) : o.mode == kTexCrush ? 0 : 3;
                if (op == 0) { cr = 3 + std::floor(h3 * 22); bt = h3 < 0.5 ? 4 + std::floor(h3 * 8) : 0; }
                else if (op == 1) lp = 0.2 + h3 * 0.35; else if (op == 2) hp = 0.5 + h3 * 0.3;
                else g = u < G * (0.25 + h3 * 0.4) ? 1 : 0;
            }
        } break;
        case Fam::Chaos2: {
            const double G = o.G; const int s = (int)std::floor(p / G + 1e-9); const double c0 = s * G, u = p - c0; const int B = o.nb ? (o.slow ? (bar >> 1) & 1 : bar & 255) : 0;
            if (hh(k, s, B * 41 + 1) < o.d)
            {
                const int op = (int)std::floor(hh(k, s, B * 41 + 2) * 8); const double h3 = hh(k, s, B * 41 + 3);
                if (op == 0) { const double sp = kCp[(int)std::floor(h3 * 4)]; r = sp > 1 ? c0 - G * (sp - 1) + u * sp : c0 + u * sp; }   // pitch
                else if (op == 1) r = c0 + std::fmod(u, G / 8);            // freeze
                else if (op == 2) r = p - G / 2;                           // skip back
                else if (op == 3) r = c0 - u;                              // the slice before, backwards
                else if (op == 4) r = c0 + 0.5 * u + 0.5 * u * u / G;      // warp up
                else if (op == 5) g = u < G / 2 ? 1 : 0;                   // gate
                else if (op == 6) { cr = 6 + std::floor(h3 * 16); bt = 5; }   // crush
                else r = c0 + std::fmod(u, G / 3);                         // ratchet
            }
        } break;
        }
        pg = g; plp = lp; php = hp; pcr = cr; pbt = bt;
        return r;
    }

    // 1-16: the hand-written patterns
    double pat(int k, double p, int bar)
    {
        double r = p, g = 1, u;
        switch (k)
        {
        case 1: r = std::fmod(p, 1.0); break;
        case 2: r = std::floor(p) + std::fmod(p, 0.5); break;
        case 3: if (p >= 3) r = 3 + std::fmod(p - 3, 0.25); break;
        case 4: if (p >= 3.5) r = 3.5 + std::fmod(p - 3.5, 0.125); else if (p >= 3) r = 3 + std::fmod(p - 3, 0.25); else if (p >= 2) r = 2 + std::fmod(p - 2, 0.5); break;
        case 5: r = p / 2; break;
        case 6: r = p - p * p / 8; if (p > 3.5) g = (4 - p) * 2; break;
        case 7: r = std::floor(p) - std::fmod(p, 1.0); break;
        case 8: r = std::floor(p * 2) / 2 - std::fmod(p, 0.5); break;
        case 9: if (p >= 3) { u = p - 3; r = 3 - 2.5 * u * u; g = 1 - u * u; } break;
        // depth 0.105 is the most that stays behind the playhead (0.1098): the push and pull are smooth
        case 10: { const double b = std::floor(p * 2) / 2; u = (p - b) * 2; r = b + 0.105 * (1 - std::cos(2 * kPi * u)); } break;
        case 11: g = std::fmod(p, 0.25) < 0.125 ? 1 : 0; break;
        case 12: g = (0xB6B6 >> (15 - (int)std::floor(p * 4))) & 1; break;
        case 13: { const int s = (int)std::floor(p * 4); r = kP13[s] * 0.25 + std::fmod(p, 0.25); } break;
        case 14: if (p >= 2) r = p - 2; break;
        case 15: r = std::floor(p) - 1 + 2 * std::fmod(p, 1.0); break;
        case 16: {
            const int s = (int)std::floor(p * 4); uint32_t hu = (uint32_t)(((bar & 3) * 31 + s * 17 + 7)) * 2654435761u;
            int32_t h = (int32_t)(hu ^ (hu >> 13)); h = h % 11;   // signed, as the original's
            r = (s - (h < 4 ? 0 : h - 3)) * 0.25 + std::fmod(p, 0.25);
        } break;
        }
        pg = g; return r;
    }

    // 4-point Hermite read (half speed, tape stops and scratches stay clean on cymbals); linear within 2 samples of now,
    // where the Hermite's far point would still be unwritten.
    double rd(const float* b, int wi, double Dd) const
    {
        double x = wi - Dd; if (x < 0) x += N;
        int i = (int)std::floor(x); double f = x - i;
        if (i >= N) { i = 0; f = 0; }   // (x rounded up to N)
        const int j = i + 1 >= N ? 0 : i + 1;
        if (Dd < 2) return b[i] + (b[j] - b[i]) * f;
        const double y0 = b[i ? i - 1 : N - 1], y1 = b[i], y2 = b[j], y3 = b[j + 1 >= N ? 0 : j + 1];
        return ((((y3 - y0) * 0.5 + (y1 - y2) * 1.5) * f + (y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3)) * f + (y2 - y0) * 0.5) * f + y1;
    }

    double sr = 48000.0;
    int N = 1, w = 0;
    std::vector<float> bL, bR;
    const std::vector<Spec>* tab = nullptr;
    Settings set;
    Transport tr;
    double D = 0, xo = 0, g = 1, pg = 1, plp = 1, php = 0, pcr = 1, pbt = 0;
    int xc = 0, xn = 1;
    double spb = 24000.0, sp = 24000.0, spbHost = 24000.0, ph = -1.0, lockQ = -1.0;
    bool lockOn = false;
    double slp = 1, shp = 0, fs[8] = {}, fa[3] = {}, fb[3] = {}, hc = 0, hl = 0, hr = 0;
    uint32_t fk = 0;
    LinearSmoother mixS, outS;
    std::atomic<float> phaseFeed { -1.0f };
    std::atomic<int> kFeed { 0 };
};

}} // namespace pump::flex
