// pump DSP: room correction (pump ROOM, stage 2 and 3).
//
// From measured responses to a correction, and from a correction to the filter that applies it:
//  - positions are power-averaged per speaker;
//  - a target curve (flat, tilted, with bass and treble shelves) is lined up with the measured level;
//  - the correction is target minus measured, with the usual restraint: cuts follow the detail, boosts only fill a
//    broad (1/3-octave) deficit so narrow dips (cancellations no EQ can fill) aren't chased; no boost below the
//    speaker's own low-end roll-off; limits on boost and cut; faded out outside the chosen band; smoothed more toward
//    the top, where a single position means less;
//  - per-speaker delays from the arrival times, so both arrive together; one preamp gain for headroom;
//  - an FIR per speaker, minimum phase (no latency) or linear phase (half its length of latency), with the delay in it;
//  - a profile text file (".pumproom") that carries the correction between the measurement app, the plugin and
//    System EQ, and that each of them designs its own filter from at its own sample rate.
//
// Framework-free, header-only, no exceptions (compiles to WebAssembly: wasm/room_wasm.cpp).
#pragma once

#include "Room.hpp"
#include <cstdio>

namespace pump {
namespace room {

// ---------------------------------------------------------------------------------------------------------------------
// curves on a log-frequency grid

// The grid every curve here lives on: kGridPerOct points per octave from kGridLo to kGridHi.
constexpr double kGridLo = 20.0, kGridHi = 20000.0;
constexpr int kGridPerOct = 48;

inline std::vector<double> correctionGrid() { return logGrid(kGridLo, kGridHi, kGridPerOct); }

// A curve's value at x: linear in log-frequency between points, held flat past the ends.
inline double curveAt(const std::vector<double>& f, const std::vector<double>& v, double x)
{
    if (f.empty()) return 0.0;
    if (f.size() == 1 || x <= f.front()) return v.front();
    if (x >= f.back()) return v.back();
    const size_t i = (size_t)(std::upper_bound(f.begin(), f.end(), x) - f.begin());
    const double t = std::log(x / f[i - 1]) / std::log(f[i] / f[i - 1]);
    return v[i - 1] + t * (v[i] - v[i - 1]);
}

// Power average of several responses in dB (same grid).
inline std::vector<double> powerAverage(const std::vector<std::vector<double>>& curves)
{
    if (curves.empty()) return {};
    const size_t n = curves[0].size();
    std::vector<double> out(n, -300.0);
    for (size_t i = 0; i < n; ++i)
    {
        double s = 0.0;
        int k = 0;
        for (const auto& c : curves)
            if (i < c.size()) { s += std::pow(10.0, c[i] / 10.0); ++k; }
        if (k > 0) out[i] = 10.0 * std::log10(std::max(s / k, 1e-30));
    }
    return out;
}

// Fractional-octave smoothing that widens with frequency: `lowOct` up to fA, `highOct` from fB, in between
// log-linear. `power` averages powers (for responses); otherwise dB values (for corrections).
inline std::vector<double> smoothVariable(const std::vector<double>& f, const std::vector<double>& v, double lowOct,
                                          double highOct, double fA = 200.0, double fB = 5000.0, bool power = true)
{
    const size_t n = f.size();
    std::vector<double> out(n);
    if (n < 2) return v;
    const double perOct = (n - 1) / std::log2(f.back() / f.front());   // grid density
    for (size_t i = 0; i < n; ++i)
    {
        double w;
        if (f[i] <= fA) w = lowOct;
        else if (f[i] >= fB) w = highOct;
        else w = lowOct + (highOct - lowOct) * std::log(f[i] / fA) / std::log(fB / fA);
        const int half = (int)std::lround(0.5 * w * perOct);
        double s = 0.0;
        int k = 0;
        for (int j = (int)i - half; j <= (int)i + half; ++j)
        {
            if (j < 0 || j >= (int)n) continue;
            s += power ? std::pow(10.0, v[(size_t)j] / 10.0) : v[(size_t)j];
            ++k;
        }
        out[i] = power ? 10.0 * std::log10(std::max(s / k, 1e-30)) : s / k;
    }
    return out;
}

// average of a dB curve between f1 and f2 (power average)
inline double bandAverage(const std::vector<double>& f, const std::vector<double>& v, double f1, double f2)
{
    double s = 0.0;
    int k = 0;
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i] >= f1 && f[i] <= f2) { s += std::pow(10.0, v[i] / 10.0); ++k; }
    return k ? 10.0 * std::log10(s / k) : 0.0;
}

// ---------------------------------------------------------------------------------------------------------------------
// target and correction settings

struct TargetSpec {
    double tiltDbPerOct = 0.0;   // slope across the range, pivoting at 1 kHz (e.g. -0.5: a little darker)
    double bassDb = 0.0;         // a low shelf centred at bassHz
    double bassHz = 120.0;
    double trebleDb = 0.0;       // a high shelf centred at trebleHz
    double trebleHz = 6000.0;

    // the target's shape (0 dB at 1 kHz with no shelves); second-order-like shelves
    double at(double f) const
    {
        const double r = f / bassHz, q = trebleHz / f;
        return tiltDbPerOct * std::log2(f / 1000.0) + bassDb / (1.0 + r * r) + trebleDb / (1.0 + q * q);
    }
};

struct CorrectionSpec {
    TargetSpec target;
    double fLo = 20.0, fHi = 20000.0;   // the band corrected (faded out over a third of an octave outside it)
    double maxBoostDb = 6.0;
    double maxCutDb = 15.0;
    bool protectNulls = true;           // boosts only fill the 1/3-octave deficit
    bool guardLowEnd = true;            // no boost below the speaker's roll-off
    double amount = 1.0;                // 0..1 of the correction (the plugin's AMOUNT)
};

struct ChannelCorrection {
    std::vector<double> f;         // the grid
    std::vector<double> measured;  // power average of the positions (lightly smoothed)
    std::vector<double> target;    // where it should be, at the measured level
    std::vector<double> correction;
    std::vector<double> result;    // measured + correction (what it should measure like afterwards)
    double lowGuardHz = 0.0;       // below this, no boost (0 = none)
    double delayMs = 0.0;
};

// The level the target sits at: the measured responses' average from 200 Hz to 2 kHz.
inline double targetLevel(const std::vector<double>& f, const std::vector<const std::vector<double>*>& measured)
{
    double s = 0.0;
    int k = 0;
    for (const auto* m : measured)
        if (m != nullptr && !m->empty()) { s += bandAverage(f, *m, 200.0, 2000.0); ++k; }
    return k ? s / k : 0.0;
}

// One speaker's correction. `measured`: its power-averaged response on `f` (unsmoothed or lightly smoothed); `level`:
// targetLevel() over all speakers, so every speaker is corrected to the same level.
inline ChannelCorrection designCorrection(const std::vector<double>& f, const std::vector<double>& measured, double level,
                                          const CorrectionSpec& spec)
{
    ChannelCorrection c;
    const size_t n = f.size();
    c.f = f;
    if (n < 2 || measured.size() != n) return c;
    // the detail cuts follow (1/12 octave in the bass widening to 1/3 above 5 kHz) and the broad picture boosts follow
    const std::vector<double> detail = smoothVariable(f, measured, 1.0 / 12.0, 1.0 / 3.0);
    const std::vector<double> broad = smoothVariable(f, measured, 1.0 / 3.0, 1.0);
    // the target's shape, lined up so its 200 Hz..2 kHz average is the measured level
    std::vector<double> shape(n);
    for (size_t i = 0; i < n; ++i) shape[i] = spec.target.at(f[i]);
    const double shapeMid = bandAverage(f, shape, 200.0, 2000.0);
    c.target.resize(n);
    for (size_t i = 0; i < n; ++i) c.target[i] = level + shape[i] - shapeMid;
    c.measured = detail;

    // the speaker's low-end roll-off: walking down from 150 Hz, the first point where the broad response falls more
    // than 6 dB under the target and stays under it from there down
    c.lowGuardHz = 0.0;
    if (spec.guardLowEnd)
    {
        int start = -1;
        for (size_t i = 0; i < n; ++i) if (f[i] <= 150.0) start = (int)i;
        for (int i = start; i >= 0; --i)
        {
            bool below = true;
            for (int j = i; j >= 0 && below; --j) below = c.target[(size_t)j] - broad[(size_t)j] > 6.0;
            if (below) { c.lowGuardHz = f[(size_t)i]; break; }
        }
    }

    std::vector<double> corr(n);
    for (size_t i = 0; i < n; ++i)
    {
        double v = c.target[i] - detail[i];
        if (v > 0.0)
        {
            if (spec.protectNulls) v = std::min(v, std::max(0.0, c.target[i] - broad[i]));
            if (c.lowGuardHz > 0.0)
            {
                // no boost at or below the roll-off, faded back in over half an octave above it
                const double oct = std::log2(f[i] / c.lowGuardHz);
                const double w = oct <= 0.0 ? 0.0 : (oct >= 0.5 ? 1.0 : 0.5 - 0.5 * std::cos(kPi * oct / 0.5));
                v *= w;
            }
        }
        v = std::clamp(v, -spec.maxCutDb, spec.maxBoostDb);
        // the band, faded over a third of an octave outside it
        double w = 1.0;
        if (f[i] < spec.fLo) w = std::max(0.0, 1.0 - std::log2(spec.fLo / f[i]) * 3.0);
        else if (f[i] > spec.fHi) w = std::max(0.0, 1.0 - std::log2(f[i] / spec.fHi) * 3.0);
        w = 0.5 - 0.5 * std::cos(kPi * w);
        corr[i] = v * w;
    }
    // the correction itself smoothed (no sharp steps where a limit or the null rule switched)
    c.correction = smoothVariable(f, corr, 1.0 / 24.0, 1.0 / 3.0, 200.0, 5000.0, false);
    for (double& v : c.correction) v *= std::clamp(spec.amount, 0.0, 1.0);
    c.result.resize(n);
    for (size_t i = 0; i < n; ++i) c.result[i] = detail[i] + c.correction[i];
    return c;
}

// ---------------------------------------------------------------------------------------------------------------------
// the filter

// FIR length for a sample rate: about a third of a second (16384 taps at 44.1 / 48 kHz), enough for the low end.
inline int correctionTaps(double fs)
{
    int t = 1024;
    while (t < 0.34 * fs && t < (1 << 18)) t <<= 1;
    return t;
}

// The FIR that applies `corrDb` (on `f`) at `fs`, with `gainDb` added and a delay of `delaySamples` (fractional
// allowed) built in. Minimum phase: the response starts at once (plus the delay); length taps + delay. Linear phase:
// symmetric about taps / 2 (its latency), plus the delay; length taps + delay.
inline std::vector<float> designFir(const std::vector<double>& f, const std::vector<double>& corrDb, double fs, int taps,
                                    bool linearPhase, double delaySamples = 0.0, double gainDb = 0.0)
{
    std::vector<float> out;
    if (!(fs > 0.0) || taps < 64 || f.empty() || corrDb.size() != f.size()) return out;
    delaySamples = std::clamp(delaySamples, 0.0, 0.25 * taps);
    const int N = 4 * taps;
    const int K = N / 2;
    RealFFT fft(N);
    AlignedBuffer a((size_t)N), b((size_t)N);
    // the magnitude on the FFT bins
    std::vector<double> mag((size_t)K + 1);
    for (int k = 0; k <= K; ++k)
    {
        const double hz = std::max(k * fs / N, 1e-3);
        mag[(size_t)k] = std::pow(10.0, (curveAt(f, corrDb, hz) + gainDb) / 20.0);
    }
    const int len = taps + (int)std::ceil(delaySamples);
    out.assign((size_t)len, 0.0f);

    // complex spectrum (ordered layout) -> real impulse
    auto toTime = [&](std::vector<double>& re, std::vector<double>& im) {
        a[0] = (float)re[0];
        a[1] = (float)re[(size_t)K];
        for (int k = 1; k < K; ++k) { a[2 * k] = (float)re[(size_t)k]; a[2 * k + 1] = (float)im[(size_t)k]; }
        fft.inverse(a.data(), b.data());
        for (int i = 0; i < N; ++i) b[(size_t)i] /= (float)N;
    };

    std::vector<double> re((size_t)K + 1), im((size_t)K + 1, 0.0);
    if (linearPhase)
    {
        const double centre = 0.5 * taps + delaySamples;
        for (int k = 0; k <= K; ++k)
        {
            const double ph = -2.0 * kPi * k * centre / N;
            re[(size_t)k] = mag[(size_t)k] * std::cos(ph);
            im[(size_t)k] = mag[(size_t)k] * std::sin(ph);
        }
        re[(size_t)K] = mag[(size_t)K] * std::cos(-kPi * centre);   // the Nyquist bin stays real
        toTime(re, im);
        // a Tukey window (flat in the middle half) around the centre
        for (int i = 0; i < len; ++i)
        {
            const double d = std::fabs(i - centre) / (0.5 * taps);   // 0 at the centre, 1 at the ends
            double w = 0.0;
            if (d <= 0.5) w = 1.0;
            else if (d < 1.0) w = 0.5 + 0.5 * std::cos(kPi * (d - 0.5) / 0.5);
            out[(size_t)i] = (float)(b[(size_t)i] * w);
        }
        return out;
    }

    // minimum phase from the real cepstrum (homomorphic): fold the cepstrum of log|H| onto positive time
    for (int k = 0; k <= K; ++k) re[(size_t)k] = std::log(std::max(mag[(size_t)k], 1e-12));
    std::fill(im.begin(), im.end(), 0.0);
    toTime(re, im);                                  // b = real cepstrum (even)
    a[0] = b[0];
    for (int i = 1; i < K; ++i) a[(size_t)i] = 2.0f * b[(size_t)i];
    a[(size_t)K] = b[(size_t)K];
    for (int i = K + 1; i < N; ++i) a[(size_t)i] = 0.0f;
    AlignedBuffer Z((size_t)N);
    fft.forward(a.data(), Z.data());                // complex log spectrum of the minimum-phase filter
    auto put = [&](int k, double lr, double li) {
        const double m = std::exp(lr), ph = li - 2.0 * kPi * k * delaySamples / N;
        re[(size_t)k] = m * std::cos(ph);
        im[(size_t)k] = m * std::sin(ph);
    };
    put(0, Z[0], 0.0);
    for (int k = 1; k < K; ++k) put(k, Z[2 * k], Z[2 * k + 1]);
    put(K, Z[1], 0.0);
    re[(size_t)K] = std::exp((double)Z[1]) * std::cos(-kPi * delaySamples);
    im[(size_t)K] = 0.0;
    toTime(re, im);
    // fade out the last quarter
    const int fade = taps / 4;
    for (int i = 0; i < len; ++i)
    {
        double w = 1.0;
        const int fromEnd = len - 1 - i;
        if (fromEnd < fade) w = 0.5 - 0.5 * std::cos(kPi * fromEnd / fade);
        out[(size_t)i] = (float)(b[(size_t)i] * w);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// profiles (".pumproom" text files)
//
//   pump-room-profile 1
//   name <text to the end of the line>
//   preamp <dB>
//   channel L <delay ms>        (then R)
//   <Hz> <correction dB> <measured dB> <target dB>     one line per grid point
//   end
//
// Lines starting with '#' are comments. Numbers use '.' and are read without the C locale.

struct ProfileChannel {
    char side = 'L';
    double delayMs = 0.0;
    std::vector<double> f, correction, measured, target;
};

struct Profile {
    std::string name;
    double preampDb = 0.0;
    ProfileChannel ch[2];
    int channels = 0;
    bool valid() const { return channels > 0; }
    const ProfileChannel* side(char s) const
    {
        for (int i = 0; i < channels; ++i) if (ch[i].side == s) return &ch[i];
        return nullptr;
    }
};

// Reads a profile; returns false (and a reason) when it isn't one.
inline bool parseProfile(const char* text, size_t len, Profile& p, std::string& why)
{
    p = Profile();
    why.clear();
    if (text == nullptr || len == 0) { why = "empty"; return false; }
    if (len > (size_t)4 << 20) { why = "too large"; return false; }
    const char* s = text;
    const char* end = text + len;
    bool header = false;
    ProfileChannel* cur = nullptr;
    while (s < end)
    {
        const char* eol = s;
        while (eol < end && *eol != '\n' && *eol != '\r') ++eol;
        std::string line(s, eol);
        s = eol;
        while (s < end && (*s == '\n' || *s == '\r')) ++s;
        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t' || (unsigned char)line[a] >= 0x80)) ++a;
        line = line.substr(a);
        if (line.empty() || line[0] == '#') continue;
        if (!header)
        {
            if (line.rfind("pump-room-profile", 0) != 0) { why = "not a pump ROOM profile"; return false; }
            header = true;
            continue;
        }
        if (line.rfind("name ", 0) == 0) { p.name = line.substr(5, 200); continue; }
        const char* q = line.c_str();
        const char* qe = q + line.size();
        if (line.rfind("preamp ", 0) == 0)
        {
            q += 7;
            double v;
            if (readNumber(q, qe, v) && std::fabs(v) < 60.0) p.preampDb = v;
            continue;
        }
        if (line.rfind("channel ", 0) == 0)
        {
            if (p.channels >= 2 || line.size() < 9 || (line[8] != 'L' && line[8] != 'R')) { why = "bad channel line"; return false; }
            cur = &p.ch[p.channels++];
            cur->side = line[8];
            q += 9;
            while (q < qe && *q == ' ') ++q;
            double v;
            if (readNumber(q, qe, v) && v >= 0.0 && v < 1000.0) cur->delayMs = v;
            continue;
        }
        if (line == "end") { cur = nullptr; continue; }
        if (cur == nullptr) continue;   // something newer than this reader: skipped
        double v[4] = { 0, 0, 0, 0 };
        int got = 0;
        while (got < 4 && q < qe)
        {
            while (q < qe && (*q == ' ' || *q == '\t')) ++q;
            if (q >= qe || !readNumber(q, qe, v[got])) break;
            ++got;
        }
        if (got < 2 || !(v[0] > 0.0) || std::fabs(v[1]) > 60.0) { why = "bad data line"; return false; }
        if (!cur->f.empty() && v[0] <= cur->f.back()) { why = "frequencies out of order"; return false; }
        if (cur->f.size() > 20000) { why = "too many points"; return false; }
        cur->f.push_back(v[0]);
        cur->correction.push_back(v[1]);
        cur->measured.push_back(got > 2 ? v[2] : 0.0);
        cur->target.push_back(got > 3 ? v[3] : 0.0);
    }
    if (!header) { why = "not a pump ROOM profile"; return false; }
    for (int i = 0; i < p.channels; ++i)
        if (p.ch[i].f.size() < 3) { why = "a channel has no curve"; p.channels = 0; return false; }
    if (p.channels == 0) { why = "no channels"; return false; }
    return true;
}

// snprintf follows the C locale: a host may have set a decimal comma. Numbers here are space-separated, so any ','
// can only be a decimal point.
inline std::string dotDecimals(std::string s) { for (char& c : s) if (c == ',') c = '.'; return s; }

inline std::string writeProfile(const Profile& p)
{
    std::string s = "pump-room-profile 1\n# made by pump ROOM\n";
    std::string name = p.name;
    for (char& c : name) if (c == '\n' || c == '\r') c = ' ';
    s += "name " + name + "\n";
    char buf[160];
    std::snprintf(buf, sizeof buf, "preamp %.3f\n", p.preampDb);
    s += dotDecimals(buf);
    for (int i = 0; i < p.channels; ++i)
    {
        const ProfileChannel& c = p.ch[i];
        std::snprintf(buf, sizeof buf, "channel %c %.4f\n", c.side, c.delayMs);
        s += dotDecimals(buf);
        for (size_t k = 0; k < c.f.size(); ++k)
        {
            std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f %.3f\n", c.f[k], c.correction[k],
                          k < c.measured.size() ? c.measured[k] : 0.0, k < c.target.size() ? c.target[k] : 0.0);
            s += dotDecimals(buf);
        }
        s += "end\n";
    }
    return s;
}

// The preamp a set of corrections needs: the largest boost, as a cut (never a boost itself).
inline double preampFor(const std::vector<const std::vector<double>*>& corrections)
{
    double mx = 0.0;
    for (const auto* c : corrections)
        if (c != nullptr) for (double v : *c) mx = std::max(mx, v);
    return -mx;
}

} // namespace room
} // namespace pump
