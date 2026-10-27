// pump ORBIT: the chaotic systems, and their calibration. Framework-free.
//
// Every system is three first-order equations (Duffing: position, velocity and the forcing's phase) with one parameter
// that CHAOS (0..1) sweeps, from a periodic orbit into chaos:
//   Lorenz         x' = 10 (y - x), y' = x (rho - z) - y, z' = x y - 8/3 z            rho 160 -> 28 (passes periodic windows)
//   Rossler        x' = -y - z, y' = x + 0.2 y, z' = 0.2 + z (x - c)                 c 2.5 (period 1) -> 3.5, 4 (doublings) -> 9
//   Double scroll  x' = a (y - x - h(x)), y' = x - y + z, z' = -28 y                  a 10.2 (one loop) -> 15.6 (both scrolls)
//                  h(x) = m1 x + (m0 - m1)(|x + 1| - |x - 1|) / 2, m0 = -8/7, m1 = -5/7
//   Thomas         x' = sin y - b x (and round)                                       b 0.30 -> 0.16
//   Halvorsen      x' = -a x - 4 y - 4 z - y^2 (and round)                            a 1.95 -> 1.27
//   Duffing        x' = v, v' = -0.25 v + x - x^3 + g cos(p), p' = 1                  g 0.20 (period 1) -> 0.50 (chaos)
// The calibration runs each system at 17 settings of CHAOS and keeps, for each, the
// mean and spread of x, y, z (to centre and scale what you hear), the mean time of one turn of the orbit (so a voice
// can step through one turn per period of its note) and a point on the attractor (where a voice starts). It takes
// about half a second, so it is done once, ahead (tools: build with ORBIT_CALIBRATE and run printCalTable()) and kept
// in OrbitCal.inc; tests/test_orbit.cpp checks that table against a fresh calibration.
#pragma once

#include "dsp/Util.hpp"
#include "OrbitParams.hpp"
#include <array>
#include <cmath>
#include <cstdio>

namespace pump { namespace orbit {

struct State { double x = 0.0, y = 0.0, z = 0.0; };

inline double sysParam(int sys, double chaos)
{
    const double c = clampv(chaos, 0.0, 1.0);
    switch (sys)
    {
    case 0: return 160.0 - 132.0 * c;
    case 1: return 2.5 + 6.5 * c;
    case 2: return 10.2 + 5.4 * c;
    case 3: return 0.30 - 0.14 * c;
    case 4: return 1.95 - 0.68 * c;
    default: return 0.20 + 0.30 * c;
    }
}

inline State deriv(int sys, const State& s, double p)
{
    State d;
    switch (sys)
    {
    case 0: d.x = 10.0 * (s.y - s.x); d.y = s.x * (p - s.z) - s.y; d.z = s.x * s.y - (8.0 / 3.0) * s.z; break;
    case 1: d.x = -s.y - s.z; d.y = s.x + 0.2 * s.y; d.z = 0.2 + s.z * (s.x - p); break;
    case 2:
    {
        const double m0 = -8.0 / 7.0, m1 = -5.0 / 7.0;
        const double h = m1 * s.x + 0.5 * (m0 - m1) * (std::fabs(s.x + 1.0) - std::fabs(s.x - 1.0));
        d.x = p * (s.y - s.x - h); d.y = s.x - s.y + s.z; d.z = -28.0 * s.y;
        break;
    }
    case 3: d.x = std::sin(s.y) - p * s.x; d.y = std::sin(s.z) - p * s.y; d.z = std::sin(s.x) - p * s.z; break;
    case 4:
        d.x = -p * s.x - 4.0 * s.y - 4.0 * s.z - s.y * s.y;
        d.y = -p * s.y - 4.0 * s.z - 4.0 * s.x - s.z * s.z;
        d.z = -p * s.z - 4.0 * s.x - 4.0 * s.y - s.x * s.x;
        break;
    default: d.x = s.y; d.y = -0.25 * s.y + s.x - s.x * s.x * s.x + p * std::cos(s.z); d.z = 1.0; break;
    }
    return d;
}

inline void rk4(int sys, State& s, double p, double h)
{
    const State k1 = deriv(sys, s, p);
    const State a { s.x + 0.5 * h * k1.x, s.y + 0.5 * h * k1.y, s.z + 0.5 * h * k1.z };
    const State k2 = deriv(sys, a, p);
    const State b { s.x + 0.5 * h * k2.x, s.y + 0.5 * h * k2.y, s.z + 0.5 * h * k2.z };
    const State k3 = deriv(sys, b, p);
    const State c { s.x + h * k3.x, s.y + h * k3.y, s.z + h * k3.z };
    const State k4 = deriv(sys, c, p);
    s.x += h / 6.0 * (k1.x + 2.0 * k2.x + 2.0 * k3.x + k4.x);
    s.y += h / 6.0 * (k1.y + 2.0 * k2.y + 2.0 * k3.y + k4.y);
    s.z += h / 6.0 * (k1.z + 2.0 * k2.z + 2.0 * k3.z + k4.z);
    if (sys == 5 && s.z > kTwoPi) s.z -= kTwoPi;   // Duffing: keep the forcing's phase small
}

inline State startOf(int sys)
{
    switch (sys)
    {
    case 0: return { 1.0, 1.0, 20.0 };
    case 1: return { 1.0, 0.0, 0.0 };
    case 2: return { 0.7, 0.0, 0.0 };
    case 3: return { 0.1, 0.0, -0.1 };
    case 4: return { -1.48, -1.51, 2.04 };
    default: return { 1.0, 0.0, 0.0 };
    }
}
// which coordinate turns once per loop (its upward crossings of its mean count the turns), and a rough turn time
inline int turnAxis(int sys) { return sys == 0 ? 2 : (sys == 2 ? 1 : 0); }
inline double turnGuess(int sys) { static const double g[6] = { 0.6, 6.0, 2.0, 25.0, 2.5, kTwoPi }; return g[clampv(sys, 0, 5)]; }
inline double& comp(State& s, int i) { return i == 0 ? s.x : (i == 1 ? s.y : s.z); }
inline double comp(const State& s, int i) { return i == 0 ? s.x : (i == 1 ? s.y : s.z); }

struct Cal { double mean[3], sd[3], turn; State on; };
constexpr int kCalPoints = 17;

inline Cal calibrate(int sys, double chaos)
{
    Cal c {};
    const double p = sysParam(sys, chaos), T = turnGuess(sys), h = T / 160.0;
    State s = startOf(sys);
    auto ok = [](const State& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::fabs(q.x) + std::fabs(q.y) + std::fabs(q.z) < 1e4; };
    for (int i = 0; i < 160 * 60; ++i) { rk4(sys, s, p, h); if (!ok(s)) { s = startOf(sys); } }
    c.on = s;
    double sum[3] = { 0, 0, 0 }, sq[3] = { 0, 0, 0 };
    const int N = 160 * 150;
    State q = s;
    for (int i = 0; i < N; ++i)
    {
        rk4(sys, q, p, h);
        for (int k = 0; k < 3; ++k) { const double v = comp(q, k); sum[k] += v; sq[k] += v * v; }
    }
    for (int k = 0; k < 3; ++k) { c.mean[k] = sum[k] / N; c.sd[k] = std::sqrt(std::max(1e-12, sq[k] / N - c.mean[k] * c.mean[k])); }
    if (sys == 5) { c.mean[2] = kPi; c.sd[2] = kPi / std::sqrt(3.0); }
    // the turns: upward crossings of the turn axis's mean, with a little hysteresis
    const int ax = turnAxis(sys);
    const double m = c.mean[ax], hy = 0.15 * c.sd[ax];
    bool low = false; int turns = 0; double first = -1.0, last = 0.0;
    q = s;
    for (int i = 0; i < N; ++i)
    {
        rk4(sys, q, p, h);
        const double v = comp(q, ax);
        if (v < m - hy) low = true;
        else if (low && v > m + hy) { low = false; ++turns; const double t = i * h; if (first < 0.0) first = t; last = t; }
    }
    c.turn = turns > 3 ? (last - first) / (turns - 1) : T;
    if (sys == 5) c.turn = kTwoPi;   // Duffing: one period of its forcing (subharmonics are the sound)
    return c;
}

using CalTable = std::array<std::array<Cal, kCalPoints>, kNumSystems>;
inline CalTable freshCalTable()
{
    CalTable t {};
    for (int s = 0; s < kNumSystems; ++s) for (int i = 0; i < kCalPoints; ++i) t[(size_t)s][(size_t)i] = calibrate(s, i / (double)(kCalPoints - 1));
    return t;
}
inline const CalTable& calTable()
{
#ifdef ORBIT_CALIBRATE
    static const CalTable T = freshCalTable();
#else
    static const CalTable T = {{
#include "OrbitCal.inc"
    }};
#endif
    return T;
}
#ifdef ORBIT_CALIBRATE
inline void printCalTable(FILE* f)
{
    const CalTable& t = calTable();
    for (int s = 0; s < kNumSystems; ++s)
    {
        std::fprintf(f, "    {{   // %s\n", kSystemNames[s]);
        for (int i = 0; i < kCalPoints; ++i)
        {
            const Cal& c = t[(size_t)s][(size_t)i];
            std::fprintf(f, "        { { %.9g, %.9g, %.9g }, { %.9g, %.9g, %.9g }, %.9g, { %.9g, %.9g, %.9g } },\n", c.mean[0], c.mean[1], c.mean[2], c.sd[0], c.sd[1], c.sd[2], c.turn, c.on.x, c.on.y, c.on.z);
        }
        std::fprintf(f, "    }},\n");
    }
}
#endif
// the calibration at any CHAOS (interpolated; the start point is the nearer one's)
inline Cal calAt(int sys, double chaos)
{
    const auto& t = calTable()[(size_t)clampv(sys, 0, kNumSystems - 1)];
    const double u = clampv(chaos, 0.0, 1.0) * (kCalPoints - 1);
    const int i = std::min(kCalPoints - 2, (int)u);
    const double f = u - i;
    const Cal& a = t[(size_t)i]; const Cal& b = t[(size_t)i + 1];
    Cal c;
    for (int k = 0; k < 3; ++k) { c.mean[k] = a.mean[k] + (b.mean[k] - a.mean[k]) * f; c.sd[k] = a.sd[k] + (b.sd[k] - a.sd[k]) * f; }
    c.turn = a.turn + (b.turn - a.turn) * f;
    c.on = f < 0.5 ? a.on : b.on;
    return c;
}

}} // namespace pump::orbit
