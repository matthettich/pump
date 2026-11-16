// pump FLEX: the pattern table. 511 patterns in two banks, each a time-remap of the bar (see FlexEngine.hpp):
//   FLX 01-FF (flx value 1-255): 01-10 are hand-written (FlexEngine's pat()), 11-FF are families by the first hex digit,
//        the second one the variant, gentler to wilder:
//        1 ROLL · 2 STUTTER · 3 BUILD · 4 REVERSE · 5 DOWN · 6 UP · 7 TAPE · 8 SCRATCH · 9 GATE · A FILTER · B CRUSH ·
//        C FADE REPEAT · D JUGGLE · E GROOVE · F CHAOS
//   FLY 01-FF (flx value 256 + v): an IDM bank, 01-0F one of each family, 10-FF the families by the first digit:
//        1 PITCH ROLL · 2 EUCLID · 3 PROB · 4 SLICE PITCH · 5 FREEZE / SMEAR · 6 CD SKIP · 7 BOUNCE · 8 MUTATE ·
//        9 STUTTER EDIT · A POLYMETER · B WARP · C REVERSE ROLL / REWIND · D GRAINS · E TEXTURE · F CHAOS 2
// The table (and every name) is built once, on first use; the engine's prepare() and the plugin's constructor touch it, so
// the audio thread never builds it. Framework-free (it is part of the engine, which also builds to WebAssembly).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pump { namespace flex {

enum class Fam : uint8_t {
    None = 0,
    Roll, Stut, Build, Rev, Down, Up, Stop, Start, Wow, Scr, Gate, Pump, Swell, Fade, Mask, Filt, Crush, Echo, Jug, Swing, Tup, Drunk, Lag, Chaos,
    PRoll, Euc, Prob, SPitch, Frz, Skip, Bnc, Mut, Sted, Poly, Warp, RRoll, Grain, Tex, Chaos2
};

// Modes of the families that pick between a few variants (what the pattern list calls m / op / only / d / set)
enum : int {
    kRevAll = 0, kRevAlt, kRevLast, kRevHalf, kRevRnd,                 // Rev
    kUpAlways = 0, kUpLast, kUpAlt,                                    // Up
    kProbMute = 0, kProbRep, kProbRat, kProbAny,                       // Prob
    kSkipAny = 0, kSkipStuck, kSkipDrop, kSkipJump,                    // Skip (only)
    kWarpUp = 0, kWarpDown, kWarpZigzag, kWarpRandom,                  // Warp
    kRRollRev = 0, kRRollPP, kRRollRew,                                // RRoll
    kTexAll = 0, kTexCrush, kTexFilt, kTexChop, kTexCF                 // Tex
};

// One pattern's parameters. Every family reads its own few fields; the rest stay at their defaults.
struct Spec {
    Fam f = Fam::None;
    std::string n;                  // the name, e.g. "ROLL 1/16 FROM BEAT 4"
    double s = 0, L = 0, C = 0, a = 0, b = 0, d = 0, k = 0, T = 0, T2 = 0, d2 = 0, g = 0, h = 0, w = 0, rho = 0, pr = 1, sw = 0;
    double q = 0, dk = 1, L0 = 0, M = 0, G = 0, c = 0, rv = 0, x = 0, fd = 0, W = 0, gap = 0, m = 0;
    int mode = 0, mask = 0, seed = 0, nb = 0, gt = 0, pp = 0, cyc = 0, slow = 0, Ne = 0, rot = 0, euh = 0;
    double sg[5][2] = {}; int nsg = 0;                         // build: (start, length) pairs
    double S[10] = {}; int nS = 0;                             // spitch: the speeds
    // filt
    bool hasLp = false, hasHp = false, hasLpw = false, hasHpw = false; double lp[2] = {}, hp[2] = {}, lpw[3] = {}, hpw[3] = {}; int bw = 0, lpb = 0;
    // crush
    double ch = 0, cb = 0, hs[2] = {}, alt = 0, clp = 0; bool hasHs = false, hasBeats = false; int beatsMask = 0;
};

// ---- name helpers (the same text the pattern names have always had) ----
inline int jsRound(double x) { return (int)std::floor(x + 0.5); }   // JavaScript's Math.round: ties go up
inline std::string jsNum(double v)
{
    char b[48];
    if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%.0f", v == 0 ? 0.0 : v);
    else std::snprintf(b, sizeof b, "%.15g", v);
    return b;
}
inline std::string frName(double x)
{
    static const int ds[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 64 };
    for (int d : ds)
    {
        const int n = jsRound(x * d);
        if (std::fabs((double)n / d - x) < 1e-6) return d == 1 ? jsNum(n) : jsNum(n) + "/" + jsNum(d);
    }
    char b[32]; std::snprintf(b, sizeof b, "%.2f", x); return b;
}
// a length in beats as a note value: "1/16", "1/8T", "3/16", "BAR"
inline std::string nv(double beats)
{
    const double x = beats / 4;
    if (std::fabs(x - 1) < 1e-9) return "BAR";
    static const int ds[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
    for (int d : ds)
    {
        const int n = jsRound(x * d);
        if (n && std::fabs((double)n / d - x) < 1e-9) return std::to_string(n) + "/" + std::to_string(d);
        const double t = x * d * 1.5;
        if (std::fabs(t - 1) < 1e-9) return "1/" + std::to_string(d) + "T";
    }
    return frName(beats) + " BEAT";
}
inline std::string stName(double s) { const int v = jsRound(12 * std::log2(s)); return (v > 0 ? "+" : "") + std::to_string(v); }
inline std::string st2(double v) { return (v > 0 ? "+" : "") + jsNum(v); }
inline std::string pct(double v) { return std::to_string(jsRound(v * 100)) + "%"; }
inline std::string fromBeat(double a) { return " FROM BEAT " + jsNum(a + 1); }

namespace detail {

inline std::vector<Spec> build()
{
    std::vector<Spec> T(512);
    auto put = [&](int X, int y, Spec o) { if (X * 16 + y > 16) T[(size_t)(X * 16 + y)] = std::move(o); };
    auto put2 = [&](int X, int y, Spec o) { T[(size_t)(256 + X * 16 + y)] = std::move(o); };
    auto mk = [](Fam f, std::string n) { Spec o; o.f = f; o.n = std::move(n); return o; };

    // 1 ROLL: [start beat, length]
    {
        const double v[15][2] = { { 3, 1. / 2 }, { 3, 1. / 3 }, { 3, 1. / 4 }, { 3, 1. / 6 }, { 3, 1. / 8 }, { 3, 1. / 12 }, { 3, 1. / 16 }, { 3, 1. / 32 }, { 2, 1. / 4 }, { 2, 1. / 8 }, { 2, 1. / 16 },
                                  { 3.5, 1. / 16 }, { 3.5, 1. / 32 }, { 3.5, 1. / 64 }, { 0, 1. / 16 } };
        for (int i = 0; i < 15; ++i) { Spec o = mk(Fam::Roll, "ROLL " + nv(v[i][1]) + fromBeat(v[i][0])); o.s = v[i][0]; o.L = v[i][1]; put(1, i + 1, o); }
    }
    // 2 STUTTER: [cycle, length]
    {
        const double v[16][2] = { { 1, 1. / 3 }, { 1, 1. / 4 }, { 1, 1. / 8 }, { 1, 1. / 16 }, { 1. / 2, 1. / 4 }, { 1. / 2, 1. / 6 }, { 1. / 2, 1. / 8 }, { 1. / 2, 1. / 16 }, { 2, 1 }, { 2, 1. / 2 }, { 2, 1. / 4 },
                                  { 4, 1. / 2 }, { 4, 1. / 3 }, { 4, 3. / 4 }, { 4, 1. / 8 }, { 1. / 4, 1. / 16 } };
        for (int i = 0; i < 16; ++i) { Spec o = mk(Fam::Stut, "STUTTER " + nv(v[i][1]) + " EVERY " + nv(v[i][0])); o.C = v[i][0]; o.L = v[i][1]; put(2, i, o); }
    }
    // 3 BUILD: lists of (start, length)
    {
        struct B { int n; double q[5][2]; };
        const B v[16] = {
            { 3, { { 2, 1. / 2 }, { 3, 1. / 4 }, { 3.5, 1. / 8 } } }, { 3, { { 2, 1. / 4 }, { 3, 1. / 8 }, { 3.5, 1. / 16 } } }, { 3, { { 2, 1. / 8 }, { 3, 1. / 16 }, { 3.5, 1. / 32 } } },
            { 4, { { 0, 1 }, { 1, 1. / 2 }, { 2, 1. / 4 }, { 3, 1. / 8 } } }, { 4, { { 0, 1. / 2 }, { 1, 1. / 4 }, { 2, 1. / 8 }, { 3, 1. / 16 } } }, { 4, { { 0, 1. / 4 }, { 1, 1. / 8 }, { 2, 1. / 16 }, { 3, 1. / 32 } } },
            { 3, { { 3, 1. / 4 }, { 3.5, 1. / 8 }, { 3.75, 1. / 16 } } }, { 3, { { 3, 1. / 8 }, { 3.5, 1. / 16 }, { 3.75, 1. / 32 } } }, { 3, { { 2, 1. / 3 }, { 3, 1. / 6 }, { 3.5, 1. / 12 } } },
            { 4, { { 0, 1. / 3 }, { 1, 1. / 6 }, { 2, 1. / 12 }, { 3, 1. / 24 } } }, { 4, { { 0, 1. / 16 }, { 1, 1. / 8 }, { 2, 1. / 4 }, { 3, 1. / 2 } } }, { 2, { { 2, 1. / 16 }, { 3, 1. / 8 } } },
            { 4, { { 2, 1. / 4 }, { 3, 1. / 8 }, { 3.5, 1. / 16 }, { 3.75, 1. / 32 } } }, { 5, { { 1, 1. / 2 }, { 2, 1. / 4 }, { 2.5, 1. / 8 }, { 3, 1. / 16 }, { 3.5, 1. / 32 } } },
            { 4, { { 0, 2 }, { 2, 1 }, { 3, 1. / 2 }, { 3.5, 1. / 4 } } }, { 2, { { 3.5, 1. / 32 }, { 3.75, 1. / 64 } } } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o; o.f = Fam::Build; o.nsg = v[i].n;
            std::string n = (i == 10 || i == 11) ? "SLOW DOWN " : "BUILD ";
            for (int j = 0; j < v[i].n; ++j) { o.sg[j][0] = v[i].q[j][0]; o.sg[j][1] = v[i].q[j][1]; n += (j ? " " : "") + nv(o.sg[j][1]); }
            if (o.sg[0][0] != 0) n += fromBeat(o.sg[0][0]);
            o.n = n; put(3, i, o);
        }
    }
    // 4 REVERSE
    {
        struct R { double C; int m; };
        const R v[16] = { { 4, kRevAll }, { 2, kRevAll }, { 1. / 4, kRevAll }, { 1. / 8, kRevAll }, { 1. / 3, kRevAll }, { 1. / 6, kRevAll }, { 1, kRevAlt }, { 1. / 2, kRevAlt }, { 1. / 4, kRevAlt }, { 2, kRevAlt },
                         { 1, kRevLast }, { 1. / 2, kRevLast }, { 1. / 4, kRevLast }, { 1, kRevHalf }, { 2, kRevHalf }, { 1. / 2, kRevRnd } };
        for (int i = 0; i < 16; ++i)
        {
            const std::string c = nv(v[i].C); std::string n;
            switch (v[i].m)
            {
            case kRevAll: n = "REVERSE " + c + "S"; break;
            case kRevAlt: n = "REVERSE EVERY OTHER " + c; break;
            case kRevLast: n = "REVERSE " + c + "S IN BEAT 4"; break;
            case kRevHalf: n = "REVERSE HALF SPEED " + c; break;
            default: n = "REVERSE RANDOM " + c + "S"; break;
            }
            Spec o = mk(Fam::Rev, n); o.C = v[i].C; o.mode = v[i].m; put(4, i, o);
        }
    }
    // 5 DOWN: [cycle, speed, from beat]
    {
        const double v[16][3] = { { 1, 1. / 2, 0 }, { 2, 1. / 2, 0 }, { 1. / 2, 1. / 2, 0 }, { 4, 3. / 4, 0 }, { 1, 3. / 4, 0 }, { 4, 2. / 3, 0 }, { 1, 2. / 3, 0 }, { 4, 1. / 4, 0 }, { 1, 1. / 4, 0 }, { 2, 1. / 2, 2 },
                                  { 1, 1. / 2, 3 }, { 1. / 4, 1. / 2, 0 }, { 2, 3. / 4, 2 }, { 4, 0.89, 0 }, { 4, 1. / 2, 2 }, { 1. / 2, 1. / 4, 0 } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Down, "DOWN " + stName(v[i][1]) + " EVERY " + nv(v[i][0]) + (v[i][2] != 0 ? fromBeat(v[i][2]) : ""));
            o.C = v[i][0]; o.s = v[i][1]; o.a = v[i][2]; put(5, i, o);
        }
    }
    // 6 UP: [cycle, speed, mode]
    {
        struct U { double C, s; int m; };
        const U v[16] = { { 1. / 2, 2, kUpAlways }, { 1. / 4, 2, kUpAlways }, { 2, 2, kUpAlways }, { 4, 2, kUpAlways }, { 1, 1.5, kUpAlways }, { 1. / 2, 1.5, kUpAlways }, { 1, 4. / 3, kUpAlways }, { 1. / 2, 4. / 3, kUpAlways },
                         { 1. / 4, 3, kUpAlways }, { 1. / 4, 4, kUpAlways }, { 1. / 8, 4, kUpAlways }, { 1, 2, kUpLast }, { 1. / 2, 2, kUpAlt }, { 1, 1.5, kUpAlt }, { 2, 1.5, kUpAlways }, { 1, 1.1225, kUpAlways } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Up, "UP " + stName(v[i].s) + " EVERY " + nv(v[i].C) + (v[i].m == kUpLast ? " IN BEAT 4" : v[i].m == kUpAlt ? " (EVERY OTHER)" : ""));
            o.C = v[i].C; o.s = v[i].s; o.mode = v[i].m; put(6, i, o);
        }
    }
    // 7 TAPE
    {
        auto stop = [&](double a, double b, const char* n) { Spec o = mk(Fam::Stop, n); o.a = a; o.b = b; return o; };
        auto stopC = [&](double C, const char* n) { Spec o = mk(Fam::Stop, n); o.C = C; return o; };
        auto start = [&](double a, double b, const char* n) { Spec o = mk(Fam::Start, n); o.a = a; o.b = b; return o; };
        auto startC = [&](double C, const char* n) { Spec o = mk(Fam::Start, n); o.C = C; return o; };
        auto wow = [&](double T, double d, const char* n, double T2 = 0, double d2 = 0) { Spec o = mk(Fam::Wow, n); o.T = T; o.d = d; o.T2 = T2; o.d2 = d2; return o; };
        Spec v[16] = { stop(3, 4, "TAPE STOP BEAT 4"), stop(2, 4, "TAPE STOP HALF BAR"), stop(3.5, 4, "TAPE STOP 1/2 BEAT"), stopC(1, "TAPE STOP EVERY BEAT"), stopC(2, "TAPE STOP EVERY 2 BEATS"),
                       stopC(1. / 2, "TAPE STOP EVERY 1/8"), start(0, 1, "TAPE START BEAT 1"), start(0, 2, "TAPE START HALF BAR"), start(0, 4, "TAPE START BAR"), startC(1, "TAPE START EVERY BEAT"),
                       startC(2, "TAPE START EVERY 2 BEATS"), wow(1, 0.012, "WOW"), wow(1. / 2, 0.02, "WOW DEEP"), wow(1. / 16, 0.0012, "FLUTTER"), wow(1. / 3, 0.01, "WARBLE", 1. / 24, 0.0008),
                       stop(3.75, 4, "TAPE STOP 1/4 BEAT") };
        for (int i = 0; i < 16; ++i) put(7, i, v[i]);
    }
    // 8 SCRATCH: [cycle, depth, count, gate type]
    {
        const double v[16][4] = { { 1, 0.25, 1, 0 }, { 1, 0.5, 2, 0 }, { 1. / 2, 0.5, 1, 0 }, { 1. / 4, 0.5, 1, 0 }, { 2, 0.25, 4, 0 }, { 1, 0.25, 4, 0 }, { 1. / 2, 0.5, 2, 1 }, { 1, 0.5, 4, 1 }, { 1. / 4, 1, 1, 1 },
                                  { 4, 0.125, 8, 0 }, { 1, 0.125, 8, 0 }, { 1. / 2, 1, 2, 0 }, { 2, 0.5, 2, 0 }, { 1, 0.25, 2, 2 }, { 1. / 2, 0.25, 4, 1 }, { 4, 0.25, 2, 0 } };
        for (int i = 0; i < 16; ++i)
        {
            const int gt = (int)v[i][3];
            Spec o = mk(Fam::Scr, std::string(gt == 2 ? "TRANSFORM" : gt ? "CHIRP" : "SCRATCH") + " " + nv(v[i][0]) + " \xC3\x97" + jsNum(v[i][2]));
            o.C = v[i][0]; o.d = v[i][1]; o.k = v[i][2]; o.gt = gt; put(8, i, o);
        }
    }
    // 9 GATE
    {
        auto M = [](const char* s) { int m = 0; for (int j = 0; j < 16; ++j) if (s[j] == 'x') m |= 1 << (15 - j); return m; };
        std::vector<Spec> v;
        { Spec o = mk(Fam::Gate, "GATE 1/32"); o.g = 1. / 16; v.push_back(o); }
        { Spec o = mk(Fam::Gate, "GATE 1/8T"); o.g = 1. / 6; v.push_back(o); }
        { Spec o = mk(Fam::Gate, "GATE 1/16T"); o.g = 1. / 12; v.push_back(o); }
        { Spec o = mk(Fam::Pump, "PUMP 1/4"); o.C = 1; v.push_back(o); }
        { Spec o = mk(Fam::Pump, "PUMP 1/8"); o.C = 1. / 2; v.push_back(o); }
        { Spec o = mk(Fam::Swell, "SWELL EVERY BEAT"); o.C = 1; v.push_back(o); }
        { Spec o = mk(Fam::Swell, "SWELL BAR"); o.C = 4; v.push_back(o); }
        v.push_back(mk(Fam::Fade, "FADE OUT BAR"));
        const char* masks[8] = { "xx.x.xx.x.xx.x.x", "x...x.x.x...x.xx", "xxx.x.x.xxx.x.x.", "x.x.xx.x.x.xx.x.", ".xxx.xxx.xxx.xxx", "x..xx..xx..xx..x", "xxxxxx..xxxx..x.", "x.x.x.x.xxxxxxxx" };
        for (const char* s : masks)
        {
            std::string n = "GATE ";
            for (int j = 0; j < 16; ++j) n += s[j] == 'x' ? "\xE2\x96\xA0" : "\xC2\xB7";   // ■ and ·
            Spec o = mk(Fam::Mask, n); o.mask = M(s); v.push_back(o);
        }
        for (int i = 0; i < 16; ++i) put(9, i, v[(size_t)i]);
    }
    // A FILTER
    {
        std::vector<Spec> v;
        auto F = [&](const char* n) { v.push_back(mk(Fam::Filt, n)); return &v.back(); };
        auto LP = [](Spec* o, double a, double b) { o->hasLp = true; o->lp[0] = a; o->lp[1] = b; };
        auto HP = [](Spec* o, double a, double b) { o->hasHp = true; o->hp[0] = a; o->hp[1] = b; };
        Spec* o;
        o = F("LOW-PASS DOWN BAR"); LP(o, 1, 0.1);
        o = F("LOW-PASS UP BAR"); LP(o, 0.1, 1);
        o = F("HIGH-PASS RISER BAR"); HP(o, 0, 0.85);
        o = F("HIGH-PASS DOWN BAR"); HP(o, 0.85, 0);
        o = F("LOW-PASS DOWN BEAT 4"); LP(o, 1, 0.1); o->a = 3;
        o = F("HIGH-PASS UP BEAT 4"); HP(o, 0, 0.85); o->a = 3;
        o = F("LOW-PASS WOBBLE 1/4"); o->hasLpw = true; o->lpw[0] = 1; o->lpw[1] = 0.55; o->lpw[2] = 0.4;
        o = F("LOW-PASS WOBBLE 1/8"); o->hasLpw = true; o->lpw[0] = 2; o->lpw[1] = 0.55; o->lpw[2] = 0.4;
        o = F("HIGH-PASS WOBBLE 1/4"); o->hasHpw = true; o->hpw[0] = 1; o->hpw[1] = 0.4; o->hpw[2] = 0.35;
        o = F("LOW-PASS CLOSED"); LP(o, 0.35, 0.35);
        o = F("HIGH-PASS THIN"); HP(o, 0.6, 0.6);
        o = F("BAND-PASS"); LP(o, 0.62, 0.62); HP(o, 0.42, 0.42);
        o = F("BAND SWEEP UP BAR"); o->bw = 1;
        o = F("LOW-PASS WOBBLE 1/4T"); o->hasLpw = true; o->lpw[0] = 1.5; o->lpw[1] = 0.5; o->lpw[2] = 0.42;
        o = F("LOW-PASS DOWN EVERY BEAT"); o->lpb = 1;
        o = F("TELEPHONE"); LP(o, 0.55, 0.55); HP(o, 0.5, 0.5);
        for (int i = 0; i < 16; ++i) put(10, i, v[(size_t)i]);
    }
    // B CRUSH
    {
        std::vector<Spec> v;
        auto C = [&](const char* n) { v.push_back(mk(Fam::Crush, n)); return &v.back(); };
        Spec* o;
        o = C("CRUSH 8 BIT"); o->cb = 8;
        o = C("CRUSH 6 BIT"); o->cb = 6;
        o = C("CRUSH 4 BIT"); o->cb = 4;
        o = C("DOWNSAMPLE \xC3\xB7" "4"); o->ch = 4;
        o = C("DOWNSAMPLE \xC3\xB7" "8"); o->ch = 8;
        o = C("DOWNSAMPLE \xC3\xB7" "16"); o->ch = 16;
        o = C("CRUSH \xC3\xB7" "8 6 BIT"); o->ch = 8; o->cb = 6;
        o = C("CRUSH SWEEP UP BAR"); o->hasHs = true; o->hs[0] = 1; o->hs[1] = 24;
        o = C("CRUSH SWEEP DOWN BAR"); o->hasHs = true; o->hs[0] = 24; o->hs[1] = 1;
        o = C("CRUSH BEATS 2 AND 4"); o->ch = 12; o->cb = 5; o->hasBeats = true; o->beatsMask = (1 << 1) | (1 << 3);
        o = C("CRUSH EVERY OTHER 1/8"); o->ch = 12; o->cb = 5; o->alt = 1. / 2;
        o = C("CRUSH 3 BIT"); o->cb = 3;
        o = C("DOWNSAMPLE \xC3\xB7" "32"); o->ch = 32;
        o = C("CRUSH \xC3\xB7" "4 4 BIT"); o->ch = 4; o->cb = 4;
        o = C("CRUSH BEAT 4"); o->ch = 16; o->cb = 5; o->a = 3;
        o = C("CRUSH DARK"); o->ch = 12; o->clp = 0.55;
        for (int i = 0; i < 16; ++i) put(11, i, v[(size_t)i]);
    }
    // C FADE REPEAT: [cycle, length, decay]
    {
        const double v[16][3] = { { 1, 1. / 4, 0.6 }, { 1, 1. / 8, 0.75 }, { 1, 1. / 3, 0.6 }, { 2, 1. / 2, 0.6 }, { 2, 3. / 4, 0.6 }, { 4, 1, 0.6 }, { 4, 3. / 4, 0.65 }, { 4, 1. / 2, 0.7 }, { 1. / 2, 1. / 8, 0.6 },
                                  { 1. / 2, 1. / 16, 0.75 }, { 1, 1. / 6, 0.7 }, { 4, 1. / 4, 0.85 }, { 2, 1. / 3, 0.7 }, { 1, 1. / 16, 0.85 }, { 4, 3. / 2, 0.6 }, { 4, 2, 0.5 } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Echo, "FADE REPEAT " + nv(v[i][1]) + " EVERY " + nv(v[i][0])); o.C = v[i][0]; o.L = v[i][1]; o.k = v[i][2]; put(12, i, o);
        }
    }
    // D JUGGLE
    {
        std::vector<Spec> v;
        auto J = [&](std::string n, double G, double c, double rv, int seed) { Spec o = mk(Fam::Jug, std::move(n)); o.G = G; o.c = c; o.rv = rv; o.seed = seed; v.push_back(o); };
        for (int i = 0; i < 8; ++i) J("JUGGLE 1/16 #" + std::to_string(i + 1), 1. / 4, 0.5, i > 5 ? 0.25 : 0, i);
        for (int i = 0; i < 4; ++i) J("JUGGLE 1/8 #" + std::to_string(i + 1), 1. / 2, 0.6, i > 2 ? 0.3 : 0, 8 + i);
        J("JUGGLE 1/8T", 1. / 3, 0.55, 0, 12);
        J("JUGGLE 1/16T", 1. / 6, 0.55, 0.2, 13);
        J("JUGGLE ALL 1/16", 1. / 4, 1, 0.3, 14);
        J("JUGGLE 1/32", 1. / 8, 0.5, 0.2, 15);
        for (int i = 0; i < 16; ++i) put(13, i, v[(size_t)i]);
    }
    // E GROOVE
    {
        auto sw = [&](double C, double a, const char* n) { Spec o = mk(Fam::Swing, n); o.C = C; o.a = a; return o; };
        auto tup = [&](double C, double q, const char* n) { Spec o = mk(Fam::Tup, n); o.C = C; o.q = q; return o; };
        auto wow = [&](double T, double d, const char* n) { Spec o = mk(Fam::Wow, n); o.T = T; o.d = d; return o; };
        Spec v[16] = { sw(1. / 2, 0.08, "SWING 1/8 LIGHT"), sw(1. / 2, 0.12, "SWING 1/8"), sw(1. / 2, 0.17, "SWING 1/8 HARD"), sw(1. / 4, 0.08, "SWING 1/16 LIGHT"), sw(1. / 4, 0.12, "SWING 1/16"),
                       sw(1. / 4, 0.17, "SWING 1/16 HARD"), tup(1, 3, "6/8 FEEL"), tup(1. / 2, 3, "6/8 FEEL 1/8"), tup(1, 5, "5-TUPLET FEEL"), wow(1, 0.004, "VIBRATO SLOW"), wow(1. / 8, 0.0008, "VIBRATO FAST"),
                       wow(1. / 2, 0.009, "VIBRATO DEEP"), mk(Fam::Drunk, "DRUNK"), mk(Fam::Lag, "LAID-BACK OFFBEATS"), sw(1. / 2, 0.22, "SWING 1/8 EXTREME"), sw(1. / 4, 0.22, "SWING 1/16 EXTREME") };
        for (int i = 0; i < 16; ++i) put(14, i, v[i]);
    }
    // F CHAOS
    for (int y = 0; y < 16; ++y)
    {
        const double G = y < 4 ? 1 : y < 10 ? 1. / 2 : 1. / 4;
        const double c = 0.25 + 0.5 * ((double)(y < 4 ? y : y < 10 ? y - 4 : y - 10) / (double)(y < 4 ? 3 : 5));
        const int nb = y & 1;
        Spec o = mk(Fam::Chaos, "CHAOS " + frName(G / 4) + " " + std::to_string(jsRound(c * 100)) + "%" + (nb ? " NEW EVERY BAR" : ""));
        o.G = G; o.c = c; o.nb = nb; o.seed = y; put(15, y, o);
    }

    // ===== FLY =====
    // 1 PITCH ROLL: [from beat, length, semitones per repeat]
    {
        const double v[16][3] = { { 3, 1. / 4, 1 }, { 3, 1. / 4, 2 }, { 3, 1. / 4, -1 }, { 3, 1. / 4, -2 }, { 3, 1. / 8, 1 }, { 3, 1. / 8, -1 }, { 2, 1. / 4, 1 }, { 2, 1. / 4, -1 }, { 3.5, 1. / 8, 2 }, { 3.5, 1. / 16, 1 },
                                  { 2, 1. / 2, 3 }, { 3, 1. / 6, 1 }, { 3, 1. / 6, -2 }, { 0, 1. / 2, 1 }, { 0, 1. / 4, -0.5 }, { 2, 1. / 3, 2 } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::PRoll, "PITCH ROLL " + nv(v[i][1]) + " " + st2(v[i][2]) + (v[i][0] != 0 ? fromBeat(v[i][0]) : " BAR"));
            o.a = v[i][0]; o.L = v[i][1]; o.s = v[i][2]; put2(1, i, o);
        }
    }
    // 2 EUCLID: [hits, steps, hold, rotation]
    {
        const int v[16][4] = { { 3, 8, 0, 0 }, { 5, 8, 0, 0 }, { 5, 16, 0, 0 }, { 7, 16, 0, 0 }, { 9, 16, 0, 0 }, { 11, 16, 0, 0 }, { 5, 12, 0, 0 }, { 7, 12, 0, 0 }, { 4, 9, 0, 0 }, { 5, 11, 0, 0 }, { 7, 13, 0, 0 },
                               { 3, 16, 0, 2 }, { 3, 8, 1, 0 }, { 5, 16, 1, 0 }, { 7, 16, 1, 0 }, { 5, 12, 1, 0 } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Euc, std::string("EUCLID ") + (v[i][2] ? "REPEAT " : "") + std::to_string(v[i][0]) + "/" + std::to_string(v[i][1]) + (v[i][3] ? " +" + std::to_string(v[i][3]) : ""));
            o.k = v[i][0]; o.Ne = v[i][1]; o.euh = v[i][2]; o.rot = v[i][3]; put2(2, i, o);
        }
    }
    // 3 PROB: [op, grid, chance, ratchet count]
    {
        struct P { int op; double G, c; int x; };
        const P v[16] = { { kProbMute, 1. / 4, .25, 0 }, { kProbMute, 1. / 4, .5, 0 }, { kProbMute, 1. / 8, .33, 0 }, { kProbRep, 1. / 4, .25, 0 }, { kProbRep, 1. / 4, .5, 0 }, { kProbRep, 1. / 4, .75, 0 },
                          { kProbRep, 1. / 2, .5, 0 }, { kProbRat, 1. / 4, .33, 2 }, { kProbRat, 1. / 4, .33, 3 }, { kProbRat, 1. / 4, .25, 4 }, { kProbRat, 1. / 2, .5, 3 }, { kProbAny, 1. / 4, .25, 0 },
                          { kProbAny, 1. / 4, .5, 0 }, { kProbAny, 1. / 4, .75, 0 }, { kProbAny, 1. / 8, .5, 0 }, { kProbAny, 1. / 3, .5, 0 } };
        static const char* const nm[] = { "MUTE", "REPEAT", "RATCHET", "ANY" };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Prob, std::string("PROB ") + nm[v[i].op] + " " + nv(v[i].G) + (v[i].x ? " \xC3\x97" + std::to_string(v[i].x) : "") + " " + pct(v[i].c));
            o.mode = v[i].op; o.G = v[i].G; o.c = v[i].c; o.x = v[i].x; put2(3, i, o);
        }
    }
    // 4 SLICE PITCH: [set, grid, chance, fixed]
    {
        struct PS { const char* name; std::vector<double> v; };
        const PS sets[] = { { "OCTAVES", { -12, 12 } }, { "OCT+5TH", { -12, -5, 7, 12 } }, { "DOWN", { -12, -7, -5 } }, { "UP", { 5, 7, 12 } }, { "CHROMATIC", { -5, -4, -3, -2, -1, 1, 2, 3, 4, 5 } },
                            { "WIDE", { -24, -19, -12, -7, 7, 12, 19 } }, { "DETUNE", { -0.6, -0.3, 0.3, 0.6 } } };
        struct E { int set; double G, c; int fx; };
        const E v[16] = { { 0, 1. / 4, .5, 0 }, { 1, 1. / 4, .5, 0 }, { 2, 1. / 4, .5, 0 }, { 3, 1. / 4, .5, 0 }, { 4, 1. / 4, .6, 0 }, { 5, 1. / 4, .5, 0 }, { 6, 1. / 4, 1, 0 }, { 0, 1. / 2, .5, 0 },
                          { 1, 1. / 2, .6, 0 }, { 4, 1. / 2, .75, 0 }, { 5, 1. / 8, .5, 0 }, { 0, 1. / 8, .4, 0 }, { 1, 1. / 4, 1, 0 }, { 2, 1. / 2, .7, 0 }, { 4, 1. / 4, .6, 1 }, { 5, 1. / 3, .6, 0 } };
        for (int i = 0; i < 16; ++i)
        {
            const PS& s = sets[v[i].set];
            Spec o = mk(Fam::SPitch, std::string("SLICE PITCH ") + s.name + " " + nv(v[i].G) + (v[i].c < 1 ? " " + pct(v[i].c) : "") + (v[i].fx ? " FIXED" : ""));
            o.G = v[i].G; o.c = v[i].c; o.nb = v[i].fx ? 0 : 1; o.nS = (int)s.v.size();
            for (int j = 0; j < o.nS; ++j) o.S[j] = std::pow(2.0, s.v[(size_t)j] / 12);
            put2(4, i, o);
        }
    }
    // 5 FREEZE / SMEAR
    {
        std::vector<Spec> v;
        auto Z = [&](const char* n) { Spec o = mk(Fam::Frz, n); o.rho = 0; o.pr = 1; o.sw = 0; v.push_back(o); return &v.back(); };
        Spec* o;
        o = Z("FREEZE BEAT 4"); o->a = 3; o->b = 4; o->w = 1. / 16;
        o = Z("FREEZE LAST 1/8"); o->a = 3.5; o->b = 4; o->w = 1. / 32;
        o = Z("FREEZE HALF BAR"); o->a = 2; o->b = 4; o->w = 1. / 8;
        o = Z("FREEZE EVERY 2ND 1/8"); o->C = 1; o->h = .5; o->w = 1. / 32;
        o = Z("FREEZE EVERY 2ND 1/16"); o->C = 1. / 2; o->h = .5; o->w = 1. / 64;
        o = Z("BUZZ BEAT 4"); o->a = 3; o->b = 4; o->w = 1. / 128;
        o = Z("BUZZ END OF EVERY BEAT"); o->C = 1; o->h = .75; o->w = 1. / 128;
        o = Z("SMEAR BEAT 4"); o->a = 3; o->b = 4; o->w = 1. / 8; o->rho = .25;
        o = Z("SMEAR HALF BAR"); o->a = 2; o->b = 4; o->w = 1. / 8; o->rho = .5;
        o = Z("SMEAR BAR"); o->a = 0; o->b = 4; o->w = 1. / 4; o->rho = .5;
        o = Z("SMEAR EVERY BEAT"); o->C = 1; o->h = 0; o->w = 1. / 16; o->rho = .5;
        o = Z("STRETCH \xC3\x97" "2 EVERY 2 BEATS"); o->C = 2; o->h = 0; o->w = 1. / 8; o->rho = .5;
        o = Z("FREEZE RANDOM 1/8S"); o->C = 1. / 2; o->h = .25; o->w = 1. / 32; o->pr = .4;
        o = Z("FREEZE RANDOM 1/16S"); o->C = 1. / 4; o->h = 0; o->w = 1. / 64; o->pr = .35;
        o = Z("FREEZE RISING BEAT 4"); o->a = 3; o->b = 4; o->w = 1. / 16; o->sw = .85;
        o = Z("FREEZE BAR"); o->a = 0; o->b = 4; o->w = 1. / 16;
        for (int i = 0; i < 16; ++i) put2(5, i, v[(size_t)i]);
    }
    // 6 CD SKIP: [grid, density, only, fixed]
    {
        struct K { double G, d; int only, fx; };
        const K v[16] = { { 1. / 4, .2, 0, 0 }, { 1. / 4, .35, 0, 0 }, { 1. / 4, .5, 0, 0 }, { 1. / 4, .7, 0, 0 }, { 1. / 2, .35, 0, 0 }, { 1. / 2, .6, 0, 0 }, { 1. / 8, .3, 0, 0 }, { 1. / 8, .5, 0, 0 }, { 1. / 4, .9, 0, 0 },
                          { 1. / 4, .5, kSkipStuck, 0 }, { 1. / 8, .5, kSkipStuck, 0 }, { 1. / 4, .4, kSkipDrop, 0 }, { 1. / 8, .5, kSkipDrop, 0 }, { 1. / 4, .5, kSkipJump, 0 }, { 1. / 2, .5, kSkipJump, 0 }, { 1. / 4, .5, 0, 1 } };
        static const char* const nm[] = { "CD SKIP", "CD STUCK", "DROPOUTS", "SKIP BACK" };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Skip, std::string(nm[v[i].only]) + " " + nv(v[i].G) + " " + pct(v[i].d) + (v[i].fx ? " FIXED" : ""));
            o.G = v[i].G; o.d = v[i].d; o.mode = v[i].only; o.nb = v[i].fx ? 0 : 1; put2(6, i, o);
        }
    }
    // 7 BOUNCE / ACCEL / DECEL
    {
        std::vector<Spec> v;
        auto B = [&](const char* n) { Spec o = mk(Fam::Bnc, n); o.dk = 1; o.sw = 0; v.push_back(o); return &v.back(); };
        Spec* o;
        o = B("BOUNCE BEAT 4"); o->a = 3; o->L0 = 1. / 2; o->q = .75; o->dk = .8;
        o = B("BOUNCE HALF BAR"); o->a = 2; o->L0 = 1. / 2; o->q = .8; o->dk = .85;
        o = B("BOUNCE BAR"); o->a = 0; o->L0 = 1; o->q = .8; o->dk = .88;
        o = B("BOUNCE BEAT 4 FAST"); o->a = 3; o->L0 = 1. / 4; o->q = .7; o->dk = .8;
        o = B("ACCEL ROLL BEAT 4"); o->a = 3; o->L0 = 1. / 4; o->q = .85;
        o = B("ACCEL ROLL HALF BAR"); o->a = 2; o->L0 = 1. / 2; o->q = .85;
        o = B("ACCEL ROLL BAR"); o->a = 0; o->L0 = 1. / 2; o->q = .9;
        o = B("ACCEL TO BUZZ BEAT 4"); o->a = 3; o->L0 = 1. / 4; o->q = .7;
        o = B("DECEL ROLL HALF BAR"); o->a = 2; o->L0 = 1. / 16; o->q = 1.3;
        o = B("DECEL ROLL BAR"); o->a = 0; o->L0 = 1. / 16; o->q = 1.25;
        o = B("DECEL ROLL BEAT 4"); o->a = 3; o->L0 = 1. / 32; o->q = 1.4;
        o = B("BOUNCE EVERY 2 BEATS"); o->C = 2; o->L0 = 1. / 2; o->q = .75; o->dk = .85;
        o = B("BOUNCE EVERY BEAT"); o->C = 1; o->L0 = 1. / 4; o->q = .7; o->dk = .8;
        o = B("ACCEL EVERY BEAT"); o->C = 1; o->L0 = 1. / 4; o->q = .8;
        o = B("DECEL EVERY BEAT"); o->C = 1; o->L0 = 1. / 16; o->q = 1.35;
        o = B("SWELL ROLL BEAT 4"); o->a = 3; o->L0 = 1. / 4; o->q = .85; o->sw = 1;
        for (int i = 0; i < 16; ++i) put2(7, i, v[(size_t)i]);
    }
    // 8 MUTATE: [grid, chance, change, reverse, cycle]
    {
        const double v[16][5] = { { 1. / 4, .5, .15, 0, 0 }, { 1. / 4, .5, .3, 0, 0 }, { 1. / 4, .5, .5, 0, 0 }, { 1. / 2, .6, .2, 0, 0 }, { 1. / 2, .6, .4, 0, 0 }, { 1. / 3, .55, .3, 0, 0 }, { 1. / 8, .5, .3, 0, 0 },
                                  { 1. / 4, .5, .3, .25, 0 }, { 1. / 2, .6, .3, .3, 0 }, { 1. / 4, .5, .3, 0, 2 }, { 1. / 4, .5, .3, 0, 4 }, { 1. / 2, .6, .4, 0, 4 }, { 1. / 4, .35, .1, 0, 0 }, { 1. / 4, .8, .3, 0, 0 },
                                  { 1. / 4, 1, .25, .2, 0 }, { 1. / 6, .55, .35, 0, 0 } };
        for (int i = 0; i < 16; ++i)
        {
            const double G = v[i][0], c = v[i][1], m = v[i][2], rv = v[i][3]; const int cyc = (int)v[i][4];
            Spec o = mk(Fam::Mut, "MUTATE " + nv(G) + " " + pct(m) + (c >= 1 ? " ALL" : "") + (rv != 0 ? " REVERSE" : "") + (cyc ? " CYCLE " + std::to_string(cyc) : ""));
            o.G = G; o.c = c; o.m = m; o.rv = rv; o.cyc = cyc; put2(8, i, o);
        }
    }
    // 9 STUTTER EDIT: [grid, density, semitones, fade, gated, fixed]
    {
        const double v[16][6] = { { 1. / 4, .25, 0, 0, 0, 0 }, { 1. / 4, .4, 0, 0, 0, 0 }, { 1. / 4, .6, 0, 0, 0, 0 }, { 1. / 2, .3, 0, 0, 0, 0 }, { 1. / 2, .5, 0, 0, 0, 0 }, { 1, .4, 0, 0, 0, 0 },
                                  { 1. / 4, .4, 2, 0, 0, 0 }, { 1. / 4, .4, -2, 0, 0, 0 }, { 1. / 2, .4, 1, 0, 0, 0 }, { 1. / 2, .4, -3, 0, 0, 0 }, { 1. / 4, .4, 0, .7, 0, 0 }, { 1. / 2, .5, 0, .75, 0, 0 },
                                  { 1. / 8, .5, 0, 0, 0, 0 }, { 1. / 3, .45, 0, 0, 0, 0 }, { 1. / 4, .4, 0, 0, 1, 0 }, { 1. / 4, .4, 0, 0, 0, 1 } };
        // (the two last ones are written [.., 0, 0, 1] and [.., 0, 0, 0, 1] in the pattern list: gated, then fixed)
        for (int i = 0; i < 16; ++i)
        {
            const double G = v[i][0], d = v[i][1], s = v[i][2], fd = v[i][3]; const int gt = (int)v[i][4], fx = (int)v[i][5];
            Spec o = mk(Fam::Sted, "STUTTER EDIT " + nv(G) + " " + pct(d) + (s != 0 ? " " + st2(s) : "") + (fd != 0 ? " FADE" : "") + (gt ? " GATED" : "") + (fx ? " FIXED" : ""));
            o.G = G; o.d = d; o.s = s; o.fd = fd; o.gt = gt; o.nb = fx ? 0 : 1; put2(9, i, o);
        }
    }
    // A POLYMETER: [length, from beat, ping-pong]
    {
        const double v[16][3] = { { 3. / 4, 0, 0 }, { 5. / 4, 0, 0 }, { 3. / 2, 0, 0 }, { 7. / 4, 0, 0 }, { 9. / 4, 0, 0 }, { 5. / 2, 0, 0 }, { 11. / 4, 0, 0 }, { 13. / 4, 0, 0 }, { 3. / 4, 2, 0 }, { 5. / 4, 2, 0 },
                                  { 3. / 4, 3, 0 }, { 2. / 3, 0, 0 }, { 3. / 4, 0, 1 }, { 5. / 4, 0, 1 }, { 7. / 4, 0, 1 }, { 5. / 6, 0, 0 } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Poly, "POLYMETER " + nv(v[i][0]) + (v[i][1] != 0 ? fromBeat(v[i][1]) : "") + (v[i][2] != 0 ? " PING-PONG" : ""));
            o.M = v[i][0]; o.a = v[i][1]; o.pp = (int)v[i][2]; put2(10, i, o);
        }
    }
    // B WARP: [grid, bend, direction]
    {
        struct W { double G, k; int d; };
        const W v[16] = { { 1. / 4, .5, kWarpUp }, { 1. / 4, .5, kWarpDown }, { 1. / 4, .5, kWarpZigzag }, { 1. / 4, .5, kWarpRandom }, { 1. / 2, .5, kWarpUp }, { 1. / 2, .5, kWarpDown }, { 1. / 2, .5, kWarpZigzag },
                          { 1. / 2, .5, kWarpRandom }, { 1, .4, kWarpUp }, { 1, .4, kWarpDown }, { 1, .4, kWarpRandom }, { 2, .3, kWarpUp }, { 2, .3, kWarpDown }, { 1. / 3, .5, kWarpRandom }, { 1. / 8, .6, kWarpRandom },
                          { 4, .25, kWarpDown } };
        static const char* const nm[] = { "UP", "DOWN", "ZIGZAG", "RANDOM" };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Warp, std::string("WARP ") + nm[v[i].d] + " " + nv(v[i].G)); o.G = v[i].G; o.k = v[i].k; o.mode = v[i].d; put2(11, i, o);
        }
    }
    // C REVERSE ROLL / REWIND
    {
        std::vector<Spec> v;
        auto roll = [&](double a, double L, int m, double s, int sw) {
            Spec o; o.f = Fam::RRoll; o.mode = m; o.a = a; o.L = L; o.s = s; o.sw = sw;
            o.n = std::string(m == kRRollPP ? "PING-PONG ROLL" : sw ? "REVERSE SWELL" : "REVERSE ROLL") + " " + nv(L) + (s != 0 ? " " + st2(s) : "") + (a != 0 ? fromBeat(a) : " BAR");
            v.push_back(o);
        };
        auto rew = [&](double a, double C, double h, double k, const char* n) { Spec o = mk(Fam::RRoll, n); o.mode = kRRollRew; o.a = a; o.C = C; o.h = h; o.k = k; v.push_back(o); };
        roll(3, 1. / 4, kRRollRev, 0, 0); roll(3, 1. / 2, kRRollRev, 0, 0); roll(3, 1. / 8, kRRollRev, 0, 0); roll(2, 1. / 4, kRRollRev, 0, 0);
        roll(3, 1. / 4, kRRollPP, 0, 0); roll(2, 1. / 2, kRRollPP, 0, 0); roll(3, 1. / 8, kRRollPP, 0, 0);
        roll(3, 1. / 4, kRRollRev, -1, 0); roll(2, 1. / 2, kRRollRev, -2, 0);
        rew(3, 0, 0, 2, "REWIND BEAT 4"); rew(2, 0, 0, 1.5, "REWIND HALF BAR"); rew(3.5, 0, 0, 3, "REWIND LAST 1/8"); rew(0, 1, .5, 2, "REWIND EVERY 2ND 1/8");
        roll(2, 1. / 4, kRRollRev, 0, 1); roll(3, 1. / 3, kRRollRev, 0, 0); roll(0, 1. / 2, kRRollPP, 0, 0);
        for (int i = 0; i < 16; ++i) put2(12, i, v[(size_t)i]);
    }
    // D GRAINS: [grain, reach, reverse, gaps, text, from beat]
    {
        struct G { double w, W, rv, gap; const char* t; double a; };
        const G v[16] = { { 1. / 16, 1, 0, 0, "FROM 1 BEAT", 0 }, { 1. / 8, 1, 0, 0, "FROM 1 BEAT", 0 }, { 1. / 4, 4, 0, 0, "FROM 1 BAR", 0 }, { 1. / 16, 4, 0, 0, "FROM 1 BAR", 0 }, { 1. / 32, 1. / 2, 0, 0, "FROM 1/8", 0 },
                          { 1. / 8, 1, .5, 0, "REVERSE 50%", 0 }, { 1. / 16, 1, .5, 0, "REVERSE 50%", 0 }, { 1. / 8, 1, 0, .4, "GAPS 40%", 0 }, { 1. / 16, 1, 0, .6, "SPARSE", 0 }, { 1. / 4, 8, .3, 0, "CLOUD FROM 2 BARS", 0 },
                          { 1. / 16, 1, 0, 0, "BEAT 4", 3 }, { 1. / 8, 1, 0, 0, "HALF BAR", 2 }, { 1. / 32, 1, 0, 0, "DENSE", 0 }, { 1. / 8, 1, 1, 0, "ALL REVERSE", 0 }, { 1. / 32, 1. / 4, .5, 0, "SHIMMER", 0 },
                          { 1. / 2, 4, 0, .3, "GAPS 30%", 0 } };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Grain, "GRAINS " + nv(v[i].w) + " " + v[i].t); o.w = v[i].w; o.W = v[i].W; o.rv = v[i].rv; o.gap = v[i].gap; o.a = v[i].a; put2(13, i, o);
        }
    }
    // E TEXTURE: [grid, density, set, fixed]
    {
        struct X { double G, d; int set, fx; };
        const X v[16] = { { 1. / 4, .3, kTexAll, 0 }, { 1. / 4, .5, kTexAll, 0 }, { 1. / 4, .75, kTexAll, 0 }, { 1. / 2, .5, kTexAll, 0 }, { 1. / 8, .4, kTexAll, 0 }, { 1. / 4, .5, kTexCrush, 0 }, { 1. / 2, .6, kTexCrush, 0 },
                          { 1. / 4, .5, kTexFilt, 0 }, { 1. / 2, .6, kTexFilt, 0 }, { 1. / 4, .5, kTexChop, 0 }, { 1. / 4, .6, kTexCF, 0 }, { 1. / 3, .5, kTexAll, 0 }, { 1. / 4, 1, kTexAll, 0 }, { 1. / 4, .5, kTexAll, 1 },
                          { 1. / 2, .25, kTexAll, 0 }, { 1. / 8, .75, kTexAll, 0 } };
        static const char* const nm[] = { "TEXTURE", "CRUSH SLICES", "FILTER SLICES", "CHOP SLICES", "CRUSH+FILTER SLICES" };
        for (int i = 0; i < 16; ++i)
        {
            Spec o = mk(Fam::Tex, std::string(nm[v[i].set]) + " " + nv(v[i].G) + " " + pct(v[i].d) + (v[i].fx ? " FIXED" : ""));
            o.G = v[i].G; o.d = v[i].d; o.mode = v[i].set; o.nb = v[i].fx ? 0 : 1; put2(14, i, o);
        }
    }
    // F CHAOS 2: [grid, density, fixed, slow]
    {
        const double v[16][4] = { { 1. / 4, .25, 0, 0 }, { 1. / 4, .4, 0, 0 }, { 1. / 4, .55, 0, 0 }, { 1. / 4, .7, 0, 0 }, { 1. / 4, .85, 0, 0 }, { 1. / 4, 1, 0, 0 }, { 1. / 2, .4, 0, 0 }, { 1. / 2, .6, 0, 0 },
                                  { 1. / 2, .8, 0, 0 }, { 1. / 8, .4, 0, 0 }, { 1. / 8, .6, 0, 0 }, { 1. / 3, .5, 0, 0 }, { 1. / 4, .5, 1, 0 }, { 1. / 2, .5, 1, 0 }, { 1, .5, 0, 0 }, { 1. / 4, .6, 0, 1 } };
        for (int i = 0; i < 16; ++i)
        {
            const int fx = (int)v[i][2], slow = (int)v[i][3];
            Spec o = mk(Fam::Chaos2, "CHAOS 2 " + nv(v[i][0]) + " " + pct(v[i][1]) + (fx ? " FIXED" : slow ? " SLOW" : ""));
            o.G = v[i][0]; o.d = v[i][1]; o.nb = fx ? 0 : 1; o.slow = slow; put2(15, i, o);
        }
    }
    // 01-0F: one of each family
    static const int pick[15] = { 0x10, 0x22, 0x34, 0x41, 0x50, 0x61, 0x70, 0x81, 0x91, 0xA0, 0xB3, 0xC0, 0xD1, 0xE1, 0xF2 };
    for (int i = 0; i < 15; ++i) T[(size_t)(256 + 1 + i)] = T[(size_t)(256 + pick[i])];
    return T;
}

} // namespace detail

// the 512 patterns (index = the flx value: 17-255 FLX 11-FF, 257-511 FLY 01-FF; the rest has f == None)
inline const std::vector<Spec>& table() { static const std::vector<Spec> t = detail::build(); return t; }

// The names of the first sixteen FLX patterns (written by hand in the engine's pat())
inline const char* flxHandName(int k)
{
    static const char* const n[17] = { "OFF", "REPEAT 1/4", "REPEAT 1/8", "FILL 1/16", "STUTTER BUILD", "HALF SPEED", "TAPE STOP", "REVERSE BEATS", "REVERSE 1/8", "BACKSPIN", "SCRATCH",
                                       "GATE 1/16", "TRANCE CHOP", "SHUFFLE", "BEAT JUGGLE", "OCTAVE UP", "RANDOM SLICES" };
    return n[k < 0 ? 0 : k > 16 ? 0 : k];
}
// the name of a pattern by its flx value (1-255 FLX, 256 + v FLY v); "OFF" for 0 and for the unused
inline std::string patternName(int flx)
{
    if (flx <= 0 || flx > 511) return "OFF";
    if (flx <= 16) return flxHandName(flx);
    if (flx == 256) return "OFF";
    const Spec& o = table()[(size_t)flx];
    return o.f == Fam::None ? "OFF" : o.n;
}

// A slot value (the plugin's slot1..slot8): 0 empty, 1-255 FLX 01-FF, 256-510 FLY 01-FF. Returns the flx value the engine plays.
inline int slotToFlx(int slot) { return slot <= 0 ? 0 : slot <= 255 ? slot : (slot >= 510 ? 511 : slot + 1); }
inline int flxToSlot(int flx) { return flx <= 0 ? 0 : flx <= 255 ? flx : (flx >= 257 ? flx - 1 : 0); }
// "FLX 1A ROLL 1/16 FROM BEAT 4" / "FLY 23 ..." / "EMPTY"
inline std::string slotLabel(int slot)
{
    if (slot <= 0) return "EMPTY";
    const int flx = slotToFlx(slot);
    char b[16];
    std::snprintf(b, sizeof b, "%s %02X ", flx <= 255 ? "FLX" : "FLY", flx <= 255 ? flx : flx - 256);
    return b + patternName(flx);
}

}} // namespace pump::flex
