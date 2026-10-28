// pump's drawn modulation shapes: a curve of points (each line between two points bent by its own amount) that an
// LFO can play instead of its regular shapes, once through and then round a loop between two of its points. Saved as
// text with the session; handed to the audio thread without locks. Framework-free.
#pragma once
#include "Util.hpp"
#include "../PumpParams.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace pump { namespace mod {

struct DrawnShape {
    static constexpr int kMaxPoints = 32;
    // points in time order: u 0..1 (the first at 0, the last at 1), v -1..1, c -1..1 = the bend of the line INTO
    // this point (0 straight; the sign picks which way it bows)
    int n = 0;
    float u[kMaxPoints] = {}, v[kMaxPoints] = {}, c[kMaxPoints] = {};
    // the loop: from point loopA to point loopB (loopA < loopB); off: once through, then it holds the last point
    int loopA = 1, loopB = 4;
    bool loop = true;

    // a line's bend: t 0..1 along it -> 0..1 of the way from its start value to its end value
    static double bend(double t, double cv)
    {
        t = clampv(t, 0.0, 1.0);
        return cv >= 0.0 ? std::pow(t, 1.0 + 4.0 * cv) : 1.0 - std::pow(1.0 - t, 1.0 - 4.0 * cv);
    }
    // the curve at u (0..1)
    double at(double x) const
    {
        if (n <= 0) return 0.0;
        if (n == 1 || x <= u[0]) return v[0];
        if (x >= u[n - 1]) return v[n - 1];
        int k = 1;
        while (k < n - 1 && x > u[k]) ++k;
        const double span = (double)u[k] - u[k - 1];
        const double t = span > 1e-6 ? (x - u[k - 1]) / span : 1.0;
        return v[k - 1] + ((double)v[k] - v[k - 1]) * bend(t, c[k]);
    }
    double loopStart() const { return loop && valid() ? u[loopA] : 0.0; }
    double loopEnd() const { return loop && valid() ? u[loopB] : 1.0; }
    bool valid() const { return n >= 2 && loopA >= 0 && loopB < n && loopA < loopB && u[loopB] - u[loopA] > 1e-4f; }
    // a position that ran on by `p` from the start: past the loop's end it goes round the loop; with no loop, it
    // stops at the end
    double wrap(double p) const
    {
        if (p < 0.0) return 0.0;
        if (!loop || !valid()) return std::min(p, 1.0);
        const double a = u[loopA], b = u[loopB];
        if (p < b) return p;
        return a + std::fmod(p - a, b - a);
    }

    // keep it sane: 2..32 points, u sorted from 0 to 1, values in range, the loop on two points
    void fix()
    {
        n = clampv(n, 0, kMaxPoints);
        if (n < 2) { *this = defaultShape(0); return; }
        u[0] = 0.0f; u[n - 1] = 1.0f;
        for (int k = 0; k < n; ++k)
        {
            if (!std::isfinite(u[k]) || !std::isfinite(v[k]) || !std::isfinite(c[k])) { u[k] = k == 0 ? 0.0f : u[k - 1]; v[k] = 0.0f; c[k] = 0.0f; }
            u[k] = clampv(u[k], k ? u[k - 1] : 0.0f, 1.0f);
            v[k] = clampv(v[k], -1.0f, 1.0f);
            c[k] = clampv(c[k], -1.0f, 1.0f);
        }
        c[0] = 0.0f;
        loopA = clampv(loopA, 0, n - 2);
        loopB = clampv(loopB, loopA + 1, n - 1);
    }

    // the shape a new instance starts with: a rise, then a looping wobble, then a fall (one per LFO, a little different)
    static DrawnShape defaultShape(int which)
    {
        DrawnShape s;
        static const float U[6] = { 0.0f, 0.09f, 0.30f, 0.47f, 0.68f, 1.0f };
        static const float V[6] = { -0.85f, 0.92f, 0.05f, 0.62f, -0.55f, -0.85f };
        static const float C[6] = { 0.0f, -0.65f, 0.55f, -0.4f, 0.7f, 0.0f };
        s.n = 6;
        for (int k = 0; k < 6; ++k) { s.u[k] = U[k]; s.v[k] = which == 1 ? -V[k] : V[k]; s.c[k] = which == 1 ? -C[k] : C[k]; }
        s.loopA = 1; s.loopB = 4; s.loop = true;
        return s;
    }

    // as text: "u:v:c u:v:c ...|loopA:loopB:loop" (':' between numbers: the parser takes ',' as a decimal point)
    std::string toText() const
    {
        std::string t;
        char b[64];
        for (int k = 0; k < n; ++k) { std::snprintf(b, sizeof b, "%s%.4g:%.4g:%.4g", k ? " " : "", u[k], v[k], c[k]); t += b; }
        std::snprintf(b, sizeof b, "|%d:%d:%d", loopA, loopB, loop ? 1 : 0);
        return t + b;
    }
    static DrawnShape fromText(const char* s, int which)
    {
        DrawnShape d;
        if (!s || !*s) return defaultShape(which);
        const char* p = s;
        d.n = 0;
        while (*p && *p != '|' && d.n < kMaxPoints)
        {
            const char* e = nullptr;
            const float a = (float)parseNumber(p, &e); if (e == p) break; p = e; if (*p == ':') ++p;
            const float b = (float)parseNumber(p, &e); if (e == p) break; p = e; if (*p == ':') ++p;
            const float c = (float)parseNumber(p, &e); if (e == p) break; p = e;
            d.u[d.n] = a; d.v[d.n] = b; d.c[d.n] = c; ++d.n;
            while (*p == ' ') ++p;
        }
        if (*p == '|')
        {
            int a = 1, b = 4, l = 1;
            if (std::sscanf(p + 1, "%d:%d:%d", &a, &b, &l) >= 2) { d.loopA = a; d.loopB = b; d.loop = l != 0; }
        }
        if (d.n < 2) return defaultShape(which);
        d.fix();
        return d;
    }
};

// Three shapes, one per LFO, written by any one thread (the host's or the window's) and read on the audio thread
// without locks: the writer bumps a sequence number around its copy; the reader copies again if it changed meanwhile.
template <int N>
class DrawnShapes {
public:
    DrawnShapes() { for (int i = 0; i < N; ++i) store(i, DrawnShape::defaultShape(i)); }
    void store(int i, const DrawnShape& s)
    {
        Slot& d = slot[clampv(i, 0, N - 1)];
        d.seq.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_release);
        d.n.store(s.n, std::memory_order_relaxed);
        for (int k = 0; k < DrawnShape::kMaxPoints; ++k)
        {
            d.u[k].store(s.u[k], std::memory_order_relaxed); d.v[k].store(s.v[k], std::memory_order_relaxed); d.c[k].store(s.c[k], std::memory_order_relaxed);
        }
        d.loopA.store(s.loopA, std::memory_order_relaxed); d.loopB.store(s.loopB, std::memory_order_relaxed); d.loop.store(s.loop, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        d.seq.fetch_add(1, std::memory_order_acq_rel);
    }
    // the audio thread: true (and `out` updated) when shape i changed since the last call
    bool takeIfChanged(int i, DrawnShape& out)
    {
        Slot& d = slot[clampv(i, 0, N - 1)];
        const uint32_t s0 = d.seq.load(std::memory_order_acquire);
        if (s0 == seen[i] || (s0 & 1u)) return false;
        DrawnShape t;
        t.n = d.n.load(std::memory_order_relaxed);
        for (int k = 0; k < DrawnShape::kMaxPoints; ++k) { t.u[k] = d.u[k].load(std::memory_order_relaxed); t.v[k] = d.v[k].load(std::memory_order_relaxed); t.c[k] = d.c[k].load(std::memory_order_relaxed); }
        t.loopA = d.loopA.load(std::memory_order_relaxed); t.loopB = d.loopB.load(std::memory_order_relaxed); t.loop = d.loop.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (d.seq.load(std::memory_order_acquire) != s0) return false;   // written meanwhile: next time
        seen[i] = s0;
        t.fix();
        out = t;
        return true;
    }

private:
    struct Slot {
        std::atomic<uint32_t> seq { 0 };
        std::atomic<int> n { 0 }, loopA { 1 }, loopB { 4 };
        std::atomic<bool> loop { true };
        std::atomic<float> u[DrawnShape::kMaxPoints], v[DrawnShape::kMaxPoints], c[DrawnShape::kMaxPoints];
    };
    Slot slot[N];
    uint32_t seen[N] = {};
};

// all three shapes as one text (the session's "shapes" state): the shapes separated by ';'
inline std::string shapesToText(const DrawnShape* s, int n)
{
    std::string t;
    for (int i = 0; i < n; ++i) { if (i) t += ';'; t += s[i].toText(); }
    return t;
}
inline void shapesFromText(const char* text, DrawnShape* s, int n)
{
    std::string all = text ? text : "";
    size_t p = 0;
    for (int i = 0; i < n; ++i)
    {
        const size_t q = all.find(';', p);
        const std::string part = p <= all.size() ? all.substr(p, q == std::string::npos ? std::string::npos : q - p) : std::string();
        s[i] = DrawnShape::fromText(part.c_str(), i);
        p = q == std::string::npos ? all.size() + 1 : q + 1;
    }
}

}} // namespace pump::mod
