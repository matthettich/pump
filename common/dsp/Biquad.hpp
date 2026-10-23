// pump DSP: EQ filter design.
//
// Every band is defined by its ANALOG prototype (the response an ideal analog EQ would have), and the digital
// filters are designed to match that prototype's magnitude, so curves don't "cramp" near Nyquist the way plain
// bilinear-transform (RBJ cookbook) filters do. Method (after M. Vicanek, "Matched Second Order Digital Filters",
// 2016): poles by impulse invariance, then the numerator is solved so the digital magnitude equals the analog one
// at DC, at Nyquist and at a third frequency near the band's centre. Linear-phase mode uses the analog magnitude
// directly (see LinearPhaseEQ).
#pragma once

#include "Util.hpp"
#include <complex>

namespace pump {

struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    // Complex response at w (radians per sample).
    std::complex<double> response(double w) const
    {
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }
    double magSq(double w) const { return std::norm(response(w)); }
    static BiquadCoeffs lerp(const BiquadCoeffs& a, const BiquadCoeffs& b, double t)
    {
        BiquadCoeffs c;
        c.b0 = a.b0 + (b.b0 - a.b0) * t; c.b1 = a.b1 + (b.b1 - a.b1) * t; c.b2 = a.b2 + (b.b2 - a.b2) * t;
        c.a1 = a.a1 + (b.a1 - a.a1) * t; c.a2 = a.a2 + (b.a2 - a.a2) * t;
        return c;
    }
};

// Transposed direct form II, double precision state (accurate at low frequencies and for steep cascades).
struct BiquadState {
    double s1 = 0.0, s2 = 0.0;
    inline double process(const BiquadCoeffs& c, double x)
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { s1 = s2 = 0.0; }
    // keep the state finite (a NaN/inf from a host would otherwise latch forever)
    void sanitize() { if (!std::isfinite(s1) || !std::isfinite(s2)) reset(); }
};

// Analog second-order section H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0); a first-order section has
// n2 = d2 = 0. Frequencies are in radians per sample (so s = j*w with w in [0, pi]).
struct AnalogSection {
    double n0 = 1, n1 = 0, n2 = 0, d0 = 1, d1 = 0, d2 = 0;
    bool firstOrder() const { return d2 == 0.0 && n2 == 0.0; }
    std::complex<double> h(double w) const
    {
        const std::complex<double> s(0.0, w);
        return (n2 * s * s + n1 * s + n0) / (d2 * s * s + d1 * s + d0);
    }
    double magSq(double w) const
    {
        const double w2 = w * w;
        const double nr = n0 - n2 * w2, ni = n1 * w, dr = d0 - d2 * w2, di = d1 * w;
        return (nr * nr + ni * ni) / std::max(dr * dr + di * di, 1e-300);
    }
};

// Magnitude-matched digital design of one analog section. matchW = the third matching frequency.
inline BiquadCoeffs matchedDesign(const AnalogSection& a, double matchW)
{
    BiquadCoeffs c;
    if (a.firstOrder())
    {
        // pole p = exp(-wp); numerator matched at DC and Nyquist.
        const double wp = a.d0 / a.d1;
        const double p = std::exp(-std::min(wp, 30.0));
        const double h0 = std::sqrt(a.magSq(0.0)), hn = std::sqrt(a.magSq(kPi));
        const double sum = h0 * (1.0 - p), dif = hn * (1.0 + p);
        c.b0 = 0.5 * (sum + dif);
        c.b1 = 0.5 * (sum - dif);
        c.b2 = 0.0;
        c.a1 = -p;
        c.a2 = 0.0;
        return c;
    }

    // poles by impulse invariance
    const double wp = std::sqrt(a.d0 / a.d2);
    const double zeta = 0.5 * a.d1 / std::sqrt(a.d0 * a.d2);
    const double r = std::exp(-zeta * wp);
    if (zeta < 1.0)
        c.a1 = -2.0 * r * std::cos(wp * std::sqrt(1.0 - zeta * zeta));
    else
        c.a1 = -2.0 * r * std::cosh(wp * std::sqrt(zeta * zeta - 1.0));
    c.a2 = r * r;

    const double A0 = (1.0 + c.a1 + c.a2) * (1.0 + c.a1 + c.a2);
    const double A1 = (1.0 - c.a1 + c.a2) * (1.0 - c.a1 + c.a2);
    const double A2 = -4.0 * c.a2;

    const double phi0 = std::cos(0.5 * matchW) * std::cos(0.5 * matchW);
    const double phi1 = 1.0 - phi0;
    const double phi2 = 4.0 * phi0 * phi1;

    const double B0 = a.magSq(0.0) * A0;
    const double B1 = a.magSq(kPi) * A1;
    const double Dm = A0 * phi0 + A1 * phi1 + A2 * phi2;
    const double B2 = (a.magSq(matchW) * Dm - B0 * phi0 - B1 * phi1) / std::max(phi2, 1e-12);

    const double sq0 = std::sqrt(std::max(B0, 0.0)), sq1 = std::sqrt(std::max(B1, 0.0));
    const double W = 0.5 * (sq0 + sq1);
    const double disc = std::max(W * W + B2, 0.0);
    c.b0 = 0.5 * (W + std::sqrt(disc));
    c.b1 = 0.5 * (sq0 - sq1);
    c.b2 = c.b0 > 1e-300 ? -B2 / (4.0 * c.b0) : 0.0;
    return c;
}

enum class Shape : int {
    Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, Count
};

inline const char* shapeName(Shape s)
{
    switch (s)
    {
    case Shape::Bell: return "Bell";
    case Shape::LowShelf: return "Low Shelf";
    case Shape::HighShelf: return "High Shelf";
    case Shape::LowCut: return "Low Cut";
    case Shape::HighCut: return "High Cut";
    case Shape::Notch: return "Notch";
    case Shape::BandPass: return "Band Pass";
    case Shape::TiltShelf: return "Tilt Shelf";
    default: return "?";
    }
}

inline bool shapeHasGain(Shape s)
{
    return s == Shape::Bell || s == Shape::LowShelf || s == Shape::HighShelf || s == Shape::TiltShelf;
}
inline bool shapeHasSlope(Shape s)
{
    return s == Shape::LowCut || s == Shape::HighCut || s == Shape::LowShelf || s == Shape::HighShelf
        || s == Shape::TiltShelf || s == Shape::BandPass || s == Shape::Notch;
}

// Slopes available per shape, in dB/oct.
static constexpr int kCutSlopes[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
static constexpr int kNumCutSlopes = 9;
static constexpr int kShelfSlopes[] = { 6, 12, 24 };
static constexpr int kNumShelfSlopes = 3;

struct BandSpec {
    Shape shape = Shape::Bell;
    double freq = 1000.0;   // Hz
    double gainDb = 0.0;
    double q = 1.0;
    int slope = 12;          // dB/oct (cuts, shelves, band pass/notch stacking)
    bool operator==(const BandSpec& o) const
    {
        return shape == o.shape && freq == o.freq && gainDb == o.gainDb && q == o.q && slope == o.slope;
    }
    bool operator!=(const BandSpec& o) const { return !(*this == o); }
};

static constexpr int kMaxSections = 8;

// The analog prototype of one band, as up to kMaxSections cascaded sections, plus an overall gain.
struct AnalogBand {
    AnalogSection sec[kMaxSections];
    int n = 0;
    double gain = 1.0;

    void add(const AnalogSection& s) { if (n < kMaxSections) sec[n++] = s; }

    std::complex<double> h(double w) const
    {
        std::complex<double> v(gain, 0.0);
        for (int i = 0; i < n; ++i) v *= sec[i].h(w);
        return v;
    }
    double magSq(double w) const
    {
        double v = gain * gain;
        for (int i = 0; i < n; ++i) v *= sec[i].magSq(w);
        return v;
    }
};

// Butterworth section Q values for order n (n/2 second-order sections, highest Q last).
inline void butterworthQs(int order, double* qs, int& nSecond, bool& hasFirst)
{
    nSecond = order / 2;
    hasFirst = (order & 1) != 0;
    for (int k = 0; k < nSecond; ++k)
    {
        // angle of the k-th conjugate pole pair from the negative real axis
        const double alpha = hasFirst ? kPi * (k + 1) / order : kPi * (2.0 * k + 1.0) / (2.0 * order);
        qs[k] = 1.0 / (2.0 * std::cos(alpha));
    }
    // order lowest Q first, highest Q last (insertion sort: at most 8 entries, and std::sort trips a false
    // -Warray-bounds on the fixed-size caller array)
    for (int i = 1; i < nSecond; ++i)
        for (int j = i; j > 0 && qs[j - 1] > qs[j]; --j) std::swap(qs[j - 1], qs[j]);
}

// Builds the analog prototype for a band at sample rate sr. Frequencies in rad/sample.
inline AnalogBand analogPrototype(const BandSpec& b, double sr)
{
    AnalogBand out;
    const double w0 = kTwoPi * clampv(b.freq, 1.0, 0.5 * sr * 1.5) / sr;   // may exceed Nyquist: analog shapes still valid
    const double q = clampv(b.q, 0.025, 40.0);
    const double G = dbToGain(b.gainDb);

    switch (b.shape)
    {
    case Shape::Bell: {
        const double A = std::sqrt(G);
        AnalogSection s;
        s.n2 = 1.0; s.n1 = A * w0 / q; s.n0 = w0 * w0;
        s.d2 = 1.0; s.d1 = w0 / (A * q); s.d0 = w0 * w0;
        out.add(s);
        break;
    }
    case Shape::LowShelf:
    case Shape::HighShelf:
    case Shape::TiltShelf: {
        // Order-M Butterworth-type shelf (Holters & Zoelzer): zeros and poles on circles of radius
        // w0 * G^(+-1/(2M)) at Butterworth angles, so the gain at w0 is exactly half the shelf (in dB) and the
        // transition gets steeper with M. M = 2 is the classic RBJ shelf. Q adds resonance to the sharpest pair.
        const bool high = b.shape != Shape::LowShelf;
        const int order = b.slope <= 6 ? 1 : (b.slope <= 12 ? 2 : 4);
        const double gk = std::pow(G, 1.0 / (2.0 * order));
        const double rz = high ? w0 / gk : w0 * gk;   // zero radius
        const double rp = high ? w0 * gk : w0 / gk;   // pole radius
        double qs[8]; int nSecond = 0; bool hasFirst = false;
        butterworthQs(order, qs, nSecond, hasFirst);
        if (nSecond > 0) qs[nSecond - 1] *= q / 0.70710678118654752;
        if (hasFirst)
        {
            AnalogSection s;
            s.n1 = 1.0; s.n0 = rz; s.d1 = 1.0; s.d0 = rp;
            out.add(s);
        }
        for (int k = 0; k < nSecond; ++k)
        {
            AnalogSection s;
            s.n2 = 1.0; s.n1 = rz / qs[k]; s.n0 = rz * rz;
            s.d2 = 1.0; s.d1 = rp / qs[k]; s.d0 = rp * rp;
            out.add(s);
        }
        out.gain = high ? G : 1.0;
        if (b.shape == Shape::TiltShelf)
            out.gain = std::sqrt(G);   // high shelf by G, then down by G/2: -g/2 below, +g/2 above
        break;
    }
    case Shape::LowCut:
    case Shape::HighCut: {
        const bool hp = b.shape == Shape::LowCut;
        const int order = clampv(b.slope / 6, 1, 16);
        double qs[8]; int nSecond = 0; bool hasFirst = false;
        butterworthQs(order, qs, nSecond, hasFirst);
        // user Q adds resonance to the sharpest section (0.707 = plain Butterworth)
        if (nSecond > 0) qs[nSecond - 1] *= q / 0.70710678118654752;
        if (hasFirst)
        {
            AnalogSection s;
            if (hp) { s.n1 = 1.0; s.n0 = 0.0; } else { s.n1 = 0.0; s.n0 = w0; }
            s.d1 = 1.0; s.d0 = w0;
            out.add(s);
        }
        for (int k = 0; k < nSecond; ++k)
        {
            AnalogSection s;
            if (hp) { s.n2 = 1.0; s.n1 = 0.0; s.n0 = 0.0; } else { s.n2 = 0.0; s.n1 = 0.0; s.n0 = w0 * w0; }
            s.d2 = 1.0; s.d1 = w0 / qs[k]; s.d0 = w0 * w0;
            out.add(s);
        }
        break;
    }
    case Shape::Notch: {
        const int stages = b.slope >= 24 ? 2 : 1;
        for (int st = 0; st < stages; ++st)
        {
            AnalogSection s;
            s.n2 = 1.0; s.n1 = 0.0; s.n0 = w0 * w0;
            s.d2 = 1.0; s.d1 = w0 / q; s.d0 = w0 * w0;
            out.add(s);
        }
        break;
    }
    case Shape::BandPass: {
        const int stages = clampv(b.slope / 12, 1, 4);
        for (int st = 0; st < stages; ++st)
        {
            AnalogSection s;
            s.n2 = 0.0; s.n1 = w0 / q; s.n0 = 0.0;
            s.d2 = 1.0; s.d1 = w0 / q; s.d0 = w0 * w0;
            out.add(s);
        }
        break;
    }
    default:
        break;
    }
    return out;
}

// Worst dB error of a digital section against its analog prototype, over the audible range, counting only
// where the analog response is above -30 dB (deep stopband accuracy is inaudible and numerically meaningless).
inline double sectionError(const AnalogSection& s, const BiquadCoeffs& c, double sr)
{
    const double fTop = std::min(20000.0, 0.485 * sr);
    const double wTop = kTwoPi * fTop / sr;
    double worst = 0.0;
    auto at = [&](double w) {
        const double ra = s.magSq(w);
        if (ra < 1e-3) return;
        worst = std::max(worst, std::fabs(10.0 * std::log10(std::max(c.magSq(w), 1e-30) / ra)));
    };
    for (int i = 0; i <= 40; ++i)
        at(kTwoPi * 20.0 * std::pow(fTop / 20.0, i / 40.0) / sr);
    // densely around the centres of the poles and zeros (narrow bells and notches live there)
    if (!s.firstOrder())
    {
        const double wp = std::sqrt(s.d0 / s.d2);
        const double qp = std::sqrt(s.d0 * s.d2) / std::max(s.d1, 1e-12);
        for (int k = -6; k <= 6; ++k)
        {
            const double w = wp * std::pow(2.0, k / (6.0 * std::max(qp, 1.0)));
            if (w < wTop) at(w);
        }
        if (s.n2 > 0.0 && s.n0 > 0.0)
        {
            const double wz = std::sqrt(s.n0 / s.n2);
            const double qz = std::sqrt(s.n0 * s.n2) / std::max(s.n1, 1e-12);
            for (int k = -6; k <= 6; ++k)
            {
                const double w = wz * std::pow(2.0, k / (6.0 * std::max(qz, 1.0)));
                if (w < wTop) at(w);
            }
        }
    }
    return worst;
}

// Pole-zero mapping (matched-Z) for sections with complex zeros: places narrow dips exactly where the analog
// ones are. Scaled to the analog DC gain.
inline bool mztDesign(const AnalogSection& a, BiquadCoeffs& c)
{
    if (a.firstOrder() || a.n2 <= 0.0 || a.n0 <= 0.0) return false;
    auto map = [](double w, double zeta, double& c1, double& c2) {
        const double r = std::exp(-zeta * w);
        c1 = zeta < 1.0 ? -2.0 * r * std::cos(w * std::sqrt(1.0 - zeta * zeta))
                        : -2.0 * r * std::cosh(w * std::sqrt(zeta * zeta - 1.0));
        c2 = r * r;
    };
    double p1, p2, z1, z2;
    map(std::sqrt(a.d0 / a.d2), 0.5 * a.d1 / std::sqrt(a.d0 * a.d2), p1, p2);
    map(std::sqrt(a.n0 / a.n2), 0.5 * a.n1 / std::sqrt(a.n0 * a.n2), z1, z2);
    const double nd = 1.0 + z1 + z2;
    if (std::fabs(nd) < 1e-12) return false;
    const double k = (a.n0 / a.d0) * (1.0 + p1 + p2) / nd;
    c.b0 = k; c.b1 = k * z1; c.b2 = k * z2; c.a1 = p1; c.a2 = p2;
    return true;
}

// Best digital fit for one section: the matched design at a few candidate matching points, plus the
// pole-zero-mapped design where it applies; keeps whichever has the smallest worst-case error.
// Returns the matching point used (0 when the pole-zero design won).
inline double bestDesign(const AnalogSection& s, double sr, BiquadCoeffs& out)
{
    if (s.firstOrder()) { out = matchedDesign(s, 0.5 * kPi); return 0.5 * kPi; }
    const double wc = std::sqrt(s.d0 / s.d2);
    static constexpr double mults[] = { 1.0, 0.85, 0.7, 0.55, 0.4, 1.2 };
    double bestW = clampv(wc, 0.01 * kPi, 0.92 * kPi), bestE = 1e9;
    BiquadCoeffs best = matchedDesign(s, bestW);
    auto tryW = [&](double w) {
        w = clampv(w, 0.01 * kPi, 0.92 * kPi);
        const BiquadCoeffs c = matchedDesign(s, w);
        const double e = sectionError(s, c, sr);
        if (e < bestE - 1e-9) { bestE = e; bestW = w; best = c; }
    };
    for (double m : mults) tryW(wc * m);
    tryW(0.5 * (std::min(wc, kPi) + 0.6 * kPi));   // for centres close to Nyquist
    BiquadCoeffs mz;
    if (mztDesign(s, mz))
    {
        const double e = sectionError(s, mz, sr);
        if (e < bestE - 1e-9) { bestE = e; bestW = 0.0; best = mz; }
    }
    out = best;
    return bestW;
}

// A designed band: its digital sections (matched to the analog prototype).
struct DigitalBand {
    BiquadCoeffs c[kMaxSections];
    double matchW[kMaxSections] = {};
    int n = 0;
    double gain = 1.0;

    // Full design: searches the best matching point per section (call on parameter changes, not per sample).
    void design(const AnalogBand& a, double sr)
    {
        n = a.n;
        gain = a.gain;
        for (int i = 0; i < n; ++i)
            matchW[i] = bestDesign(a.sec[i], sr, c[i]);
        foldGain();
    }
    // Fast redesign reusing the choices of the last full design (for gain modulation: dynamic EQ).
    void redesignFast(const AnalogBand& a)
    {
        n = a.n;
        gain = a.gain;
        for (int i = 0; i < n; ++i)
        {
            if (matchW[i] > 0.0 || !mztDesign(a.sec[i], c[i]))
                c[i] = matchedDesign(a.sec[i], matchW[i] > 0.0 ? matchW[i] : 0.5 * kPi);
        }
        foldGain();
    }
    void foldGain()
    {
        // fold the overall gain into the first section's numerator
        if (n > 0 && gain != 1.0)
        {
            c[0].b0 *= gain; c[0].b1 *= gain; c[0].b2 *= gain;
            gain = 1.0;
        }
    }
    double magSq(double w) const
    {
        double v = gain * gain;
        for (int i = 0; i < n; ++i) v *= c[i].magSq(w);
        return v;
    }
    std::complex<double> response(double w) const
    {
        std::complex<double> v(gain, 0.0);
        for (int i = 0; i < n; ++i) v *= c[i].response(w);
        return v;
    }
};

// A running band: designed sections with per-channel state.
template <int Channels>
struct BandFilter {
    DigitalBand d;
    BiquadState st[Channels][kMaxSections];

    void reset() { for (auto& ch : st) for (auto& s : ch) s.reset(); }
    inline double process(int ch, double x)
    {
        for (int i = 0; i < d.n; ++i) x = st[ch][i].process(d.c[i], x);
        return x;
    }
};

} // namespace pump
