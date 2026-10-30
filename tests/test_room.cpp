// pump ROOM measurement: sweeps through a simulated room (a speaker with a room mode, a dip, roll-offs, reflections,
// latency, noise and a microphone with its own tilt) must come back as the response that was put in, with the
// arrival times and polarities right; and calibration files in the formats people actually have must read.
#include "../common/dsp/Room.hpp"
#include "../common/dsp/RoomCorrect.hpp"
#include "RoomEngine.hpp"   // plugins/room (the plugin's engine)
#include <complex>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::room;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

using cd = std::complex<double>;

// RBJ biquads, run in double, with their exact frequency response (independent of pump's own filter code)
struct Bq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Bq peak(double fs, double f, double q, double db)
    {
        const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
        return norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    static Bq highpass(double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
        return norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static Bq lowpass(double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
        return norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static Bq highshelf(double fs, double f, double db)
    {
        const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * f / fs, c = std::cos(w);
        const double al = std::sin(w) / 2 * std::sqrt(2.0), sA = 2 * std::sqrt(A) * al;
        return norm(A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                    (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    static Bq allpass(double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
        return norm(1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }
    static Bq norm(double b0, double b1, double b2, double a0, double a1, double a2)
    {
        Bq q; q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0; return q;
    }
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    cd at(double fs, double f) const
    {
        const cd z1i = std::exp(cd(0, -2 * kPi * f / fs)), z2i = z1i * z1i;
        return (b0 + b1 * z1i + b2 * z2i) / (1.0 + a1 * z1i + a2 * z2i);
    }
};

// A path from the output to the microphone: filters, a direct sound and reflections, all after `latency` samples.
struct Path {
    std::vector<Bq> filters;
    struct Tap { int delay; double gain; };
    std::vector<Tap> taps { { 0, 1.0 } };   // relative to the latency
    int latency = 0;
    double polarity = 1.0;

    std::vector<float> run(const std::vector<float>& x, int n)
    {
        std::vector<double> y((size_t)n, 0.0);
        std::vector<Bq> f = filters;
        std::vector<double> filtered((size_t)n, 0.0);
        for (int i = 0; i < n; ++i)
        {
            double v = i < (int)x.size() ? x[(size_t)i] : 0.0;
            for (Bq& b : f) v = b.run(v);
            filtered[(size_t)i] = v;
        }
        for (const Tap& t : taps)
            for (int i = 0; i + latency + t.delay < n; ++i)
                y[(size_t)(i + latency + t.delay)] += polarity * t.gain * filtered[(size_t)i];
        std::vector<float> out((size_t)n);
        for (int i = 0; i < n; ++i) out[(size_t)i] = (float)y[(size_t)i];
        return out;
    }
    double dbAt(double fs, double f) const
    {
        cd h = 1.0;
        for (const Bq& b : filters) h *= b.at(fs, f);
        cd s = 0.0;
        for (const Tap& t : taps) s += t.gain * std::exp(cd(0, -2 * kPi * f * t.delay / fs));
        return 20 * std::log10(std::abs(h * s));
    }
};

static void addNoise(std::vector<float>& x, double db, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, std::pow(10.0, db / 20.0));
    for (float& v : x) v += (float)g(rng);
}

static Measurement measure(Path& p, const SweepSpec& s, double tailSec, double noiseDb, unsigned seed)
{
    const std::vector<float> sweep = makeSweep(s);
    const int n = (int)sweep.size() + (int)(tailSec * s.sampleRate);
    std::vector<float> rec = p.run(sweep, n);
    if (noiseDb > -200) addNoise(rec, noiseDb, seed);
    Measurement m;
    m.analyse(rec.data(), n, sweep, s, (int)(1.0 * s.sampleRate));
    return m;
}

static void testSweep()
{
    printf("sweep\n");
    SweepSpec s; s.sampleRate = 48000; s.seconds = 3; s.levelDb = -12;
    const std::vector<float> x = makeSweep(s);
    float pk = 0; for (float v : x) pk = std::max(pk, std::fabs(v));
    CHECK((int)x.size() == 144000, "length %d samples for 3 s", (int)x.size());
    CHECK(std::fabs(20 * std::log10(pk) + 12.0) < 0.05, "peak %.2f dBFS (asked -12)", 20 * std::log10(pk));
    CHECK(std::fabs(x.front()) < 1e-6 && std::fabs(x.back()) < 1e-3, "starts and ends at zero (%.1e, %.1e)", std::fabs(x.front()), std::fabs(x.back()));
    CHECK(std::fabs(s.end() - 21600) < 1, "ends at %.0f Hz (0.45 fs)", s.end());
    SweepSpec s2 = s; s2.sampleRate = 44100;
    CHECK(std::fabs(s2.validHigh() - 0.45 * 44100 / 1.06) < 1 && s2.validLow() == 20.0, "valid band at 44.1 kHz: %.0f..%.0f Hz", s2.validLow(), s2.validHigh());
}

// A straight wire (output = input): the response is flat at 0 dB and the arrival is at the latency. (The bottom
// octaves read a few hundredths of a dB low: the band edge under the sweep's start rings for longer than the 2 ms
// before the direct sound that the window keeps.)
static void testLoopback()
{
    printf("loopback\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        SweepSpec s; s.sampleRate = fs; s.seconds = 2;
        Path p; p.latency = 777;
        Measurement m = measure(p, s, 1.5, -300, 1);
        m.ensureSpectrum(500);
        const double top = std::min(20000.0, s.validHigh());
        const std::vector<double> g = logGrid(20, top, 24);
        std::vector<float> r; m.response(g, 0.0, nullptr, r);
        double worst = 0; for (float v : r) worst = std::max(worst, std::fabs((double)v));
        CHECK(worst < 0.1, "%.1f kHz: flat 20 Hz..%.1f kHz within %.3f dB", fs / 1000, top / 1000, worst);
        CHECK(std::fabs(m.arrival.index - 777) < 0.02 && m.arrival.polarity == 1, "%.1f kHz: arrival %.3f (777), polarity %+d", fs / 1000, m.arrival.index, m.arrival.polarity);
    }
}

// A speaker in a room: high-pass, a room mode, a dip, a top-end roll-off, two reflections; the measurement must
// match the response put in (unsmoothed, every 1/24 octave, 20 Hz..20 kHz) with noise 70 dB under the sweep.
static void testRoom()
{
    printf("room\n");
    const double fs = 48000;
    SweepSpec s; s.sampleRate = fs; s.seconds = 3; s.levelDb = -12;
    Path p;
    p.filters = { Bq::highpass(fs, 45, 0.8), Bq::peak(fs, 120, 4, 8), Bq::peak(fs, 2500, 2, -6), Bq::lowpass(fs, 19000, 0.6) };
    p.taps = { { 0, 0.5 }, { (int)(0.0031 * fs), 0.25 }, { (int)(0.0073 * fs), 0.15 } };
    p.latency = 2400;
    Measurement m = measure(p, s, 1.5, -12 - 3 - 70, 2);
    m.ensureSpectrum(500);
    const std::vector<double> g = logGrid(20, 20000, 24);
    std::vector<float> r; m.response(g, 0.0, nullptr, r);
    double worst = 0, at = 0;
    for (size_t i = 0; i < g.size(); ++i)
    {
        const double ref = p.dbAt(fs, g[i]);
        if (ref < -40) continue;   // inside a deep comb notch: not meaningful
        const double e = std::fabs(r[i] - ref);
        if (e > worst) { worst = e; at = g[i]; }
    }
    CHECK(worst < 0.1, "matches the room within %.2f dB (worst at %.0f Hz)", worst, at);
    CHECK(std::fabs(m.arrival.index - 2400) < 0.6, "arrival %.2f samples (2400)", m.arrival.index);
    // smoothing: 1/3 octave follows the room's broad shape (the mode is still there, the comb is gone)
    std::vector<float> sm; m.response(g, 1.0 / 3.0, nullptr, sm);
    double rough = 0, roughS = 0;
    for (size_t i = 1; i + 1 < g.size(); ++i)
    {
        rough += std::fabs(r[i + 1] - 2 * r[i] + r[i - 1]);
        roughS += std::fabs(sm[i + 1] - 2 * sm[i] + sm[i - 1]);
    }
    CHECK(roughS < 0.3 * rough, "1/3-octave smoothing calms the curve (%.0f -> %.0f)", rough, roughS);
    const size_t i120 = (size_t)std::lround(std::log2(120.0 / 20.0) * 24), i500 = (size_t)std::lround(std::log2(500.0 / 20.0) * 24);
    CHECK(sm[i120] - sm[i500] > 4.0, "the 120 Hz mode stands %.1f dB above 500 Hz after smoothing", sm[i120] - sm[i500]);
}

// A second channel 140 samples further away (about 1 m), inverted: the difference and the polarity must come out.
static void testTwoChannels()
{
    printf("two channels\n");
    const double fs = 48000;
    SweepSpec s; s.sampleRate = fs; s.seconds = 2;
    Path l, r;
    l.filters = r.filters = { Bq::highpass(fs, 50, 0.7), Bq::peak(fs, 80, 3, 6) };
    l.taps = r.taps = { { 0, 0.4 }, { 150, 0.2 } };
    l.latency = 1000; r.latency = 1140; r.polarity = -1;
    Measurement ml = measure(l, s, 1.5, -90, 3), mr = measure(r, s, 1.5, -90, 4);
    const double d = mr.arrival.index - ml.arrival.index;
    CHECK(std::fabs(d - 140) < 0.3, "right arrives %.2f samples later (140) = %.1f cm", d, d / fs * kSpeedOfSound * 100);
    CHECK(ml.arrival.polarity == 1 && mr.arrival.polarity == -1, "polarity L %+d, R %+d", ml.arrival.polarity, mr.arrival.polarity);
}

// Between samples: a pure delay of 1000.37 samples (windowed-sinc fractional delay) must read 1000.37.
static void testFractional()
{
    printf("fractional arrival\n");
    const double fs = 48000;
    SweepSpec s; s.sampleRate = fs; s.seconds = 1;
    const std::vector<float> sweep = makeSweep(s);
    for (double frac : { 0.0, 0.25, 0.37, 0.5, 0.81 })
    {
        const double D = 1000 + frac;
        const int n = (int)sweep.size() + 24000, half = 64;
        std::vector<float> rec((size_t)n, 0.0f);
        for (int i = 0; i < (int)sweep.size(); ++i)
            for (int k = -half; k <= half; ++k)
            {
                const int j = (int)std::floor(D) + i + k;
                if (j < 0 || j >= n) continue;
                const double t = j - i - D;   // distance from the true position
                const double w = 0.5 + 0.5 * std::cos(kPi * t / (half + 1));
                const double sinc = std::fabs(t) < 1e-12 ? 1.0 : std::sin(kPi * t * 0.92) / (kPi * t);   // band-limited to 0.92 x Nyquist
                rec[(size_t)j] += (float)(sweep[(size_t)i] * sinc * w);
            }
        Measurement m;
        m.analyse(rec.data(), n, sweep, s, 24000);
        CHECK(std::fabs(m.arrival.index - D) < 0.02, "delay %.2f read as %.3f", D, m.arrival.index);
    }
}

// A reflection louder than the direct sound (a speaker aimed away): the direct sound is still taken as long as it is
// within 6 dB of the loudest peak; the loudest peak is reported too.
static void testStrongReflection()
{
    printf("strong reflection\n");
    const double fs = 48000;
    SweepSpec s; s.sampleRate = fs; s.seconds = 1;
    Path p; p.latency = 500; p.taps = { { 0, 0.6 }, { 96, 1.0 } };
    Measurement m = measure(p, s, 1.0, -300, 5);
    CHECK(std::fabs(m.arrival.index - 500) < 0.5 && std::fabs(m.arrival.peakIndex - 596) < 1, "direct at %.1f (500), loudest at %.0f (596)", m.arrival.index, m.arrival.peakIndex);
}

// The microphone tilts the top end up 4 dB; its calibration file (as a mic maker would ship it) takes that back out.
static void testCalibration()
{
    printf("calibration\n");
    const double fs = 48000;
    SweepSpec s; s.sampleRate = fs; s.seconds = 3;
    const Bq mic = Bq::highshelf(fs, 8000, 4);
    Path p; p.latency = 1500;
    p.filters = { Bq::highpass(fs, 40, 0.7), Bq::peak(fs, 300, 1.5, -4), mic };
    Path truth = p; truth.filters.pop_back();
    // the file: tab separated, a sens line, a header, every 1/12 octave from 10 Hz to 24 kHz
    std::string file = "\"Sens Factor =-1.378dB, SERNO: 7012345\"\r\n\"Freq(Hz)\"\t\"dB\"\t\"Phase\"\r\n";
    for (double f = 10; f < 24000; f *= std::pow(2.0, 1.0 / 12))
    {
        char line[64];
        std::snprintf(line, sizeof line, "%.3f\t%.4f\t0\r\n", f, 20 * std::log10(std::abs(mic.at(fs, f))));
        file += line;
    }
    CalCurve cal;
    const int pts = parseCal(file.data(), file.size(), cal);
    CHECK(pts > 100 && cal.hasSensitivity && std::fabs(cal.sensitivityDb + 1.378) < 1e-9, "file read: %d points, sens %.3f dB", pts, cal.sensitivityDb);
    Measurement m = measure(p, s, 1.5, -95, 6);
    m.ensureSpectrum(500);
    const std::vector<double> g = logGrid(20, 20000, 24);
    std::vector<float> raw, fixed;
    m.response(g, 0.0, nullptr, raw);
    m.response(g, 0.0, &cal, fixed);
    double worstRaw = 0, worstFixed = 0;
    for (size_t i = 0; i < g.size(); ++i)
    {
        const double ref = truth.dbAt(fs, g[i]);
        worstRaw = std::max(worstRaw, std::fabs(raw[i] - ref));
        worstFixed = std::max(worstFixed, std::fabs(fixed[i] - ref));
    }
    CHECK(worstRaw > 3.5, "without the file the mic's tilt shows (%.2f dB off)", worstRaw);
    CHECK(worstFixed < 0.2, "with the file: within %.3f dB of the speaker alone", worstFixed);
}

static int parse(const char* text, CalCurve& c) { return parseCal(text, std::strlen(text), c); }

static void testCalFormats()
{
    printf("calibration file formats\n");
    CalCurve c;
    CHECK(parse("* comment\n* another\n20 0.5\n1000 0\n20000 -1.5\n", c) == 3 && c.at(1000) == 0.0, "space separated with * comments");
    CHECK(parse("Frequency,dB,Phase\n20,0.5,1\n1000,0,0\n20000,-1.5,3\n", c) == 3 && std::fabs(c.at(20) - 0.5) < 1e-12, "CSV with a header and phase");
    CHECK(parse("20;0,5\n1000;0\n20000;-1,5\n", c) == 3 && std::fabs(c.at(20000) + 1.5) < 1e-12, "semicolons with decimal commas");
    CHECK(parse("\xEF\xBB\xBF" "20\t0.5\r\n1000\t0\r\n20000\t-1.5\r\n", c) == 3, "BOM and CRLF");
    CHECK(parse("1000 0\n20 0.5\n20000 -1.5\n1000 1\n", c) == 3 && std::fabs(c.at(1000) - 0.5) < 1e-12, "unsorted, duplicate frequency averaged");
    CHECK(parse("2.0E+01 5.0E-01\n1.0e3 0\n2e4 -1.5\n", c) == 3 && std::fabs(c.at(20) - 0.5) < 1e-12, "exponent notation");
    CHECK(parse("20 0.5\n1000 0\n20000 -1.5\n", c) == 3 && std::fabs(c.at(10) - 0.5) < 1e-12 && std::fabs(c.at(30000) + 1.5) < 1e-12, "held flat past both ends");
    CHECK(std::fabs(c.at(std::sqrt(20.0 * 1000.0)) - 0.25) < 1e-9, "interpolated in log frequency (midway: %.3f)", c.at(std::sqrt(20.0 * 1000.0)));
    CHECK(parse("20 0.5\n1000 0\n", c) == kCalTooFew, "two points: too few");
    CHECK(parse("hello\nworld\n", c) == kCalEmpty, "no numbers: empty");
    CHECK(parse("", c) == kCalEmpty, "nothing at all");
    CHECK(parse("20 0.5\n1000 abc\n-5 1\n2000 99\n5000 0\n9000 1\n", c) == 3 && c.skippedLines == 3, "bad lines skipped (%d)", c.skippedLines);
    CHECK(parse("Sens Factor =-0.9dB, AGain=18dB, SERNO: 7004321\n20 0.5\n1000 0\n20000 -1.5\n", c) == 3 && std::fabs(c.sensitivityDb + 0.9) < 1e-12, "unquoted sens line");
    // locale-proof: a decimal comma never sneaks into comma-separated files
    CHECK(parse("20,0.5\n1000,0\n20000,-1.5\n", c) == 3 && std::fabs(c.at(20) - 0.5) < 1e-12, "commas as separators are not decimals");
}

// The response read back from saved samples matches the measured one.
static void testReload()
{
    printf("save and reload\n");
    const double fs = 48000;
    SweepSpec s; s.sampleRate = fs; s.seconds = 2;
    Path p; p.latency = 3000; p.filters = { Bq::peak(fs, 200, 2, 5) };
    Measurement m = measure(p, s, 1.5, -90, 7);
    m.ensureSpectrum(300);
    const int from = std::max(0, (int)m.arrival.index - 2400);
    Measurement b;
    b.load(m.ir.data() + from, (int)m.ir.size() - from, fs, m.arrival.index - from);
    b.ensureSpectrum(300);
    const std::vector<double> g = logGrid(20, 20000, 12);
    std::vector<float> ra, rb; m.response(g, 1.0 / 6, nullptr, ra); b.response(g, 1.0 / 6, nullptr, rb);
    double worst = 0; for (size_t i = 0; i < g.size(); ++i) worst = std::max(worst, (double)std::fabs(ra[i] - rb[i]));
    CHECK(worst < 1e-3 && std::fabs(b.arrival.index + from - m.arrival.index) < 1e-9, "same response (%.1e dB) and arrival", worst);
}

// Levels: the checks the app shows (signal, peak, clipping) read the recording right.
static void testLevels()
{
    printf("levels\n");
    std::vector<float> x(48000);
    for (int i = 0; i < 48000; ++i) x[(size_t)i] = 0.5f * (float)std::sin(2 * kPi * 1000 * i / 48000.0);
    CHECK(std::fabs(rmsDb(x.data(), 48000) + 9.03) < 0.02 && std::fabs(peakDb(x.data(), 48000) + 6.02) < 0.02, "sine at -6 dBFS peak: RMS %.2f dB", rmsDb(x.data(), 48000));
    CHECK(rmsDb(nullptr, 0) < -200, "empty: silence");
}

// ---------------------------------------------------------------------------------------------------------------------
// correction design

// a measured response on the correction grid from a path's exact response (plus an offset)
static std::vector<double> curveOf(const Path& p, double fs, double offset)
{
    const std::vector<double> g = correctionGrid();
    std::vector<double> v(g.size());
    for (size_t i = 0; i < g.size(); ++i) v[i] = p.dbAt(fs, g[i]) + offset;
    return v;
}

static double at(const std::vector<double>& f, const std::vector<double>& v, double hz) { return curveAt(f, v, hz); }

static void testCorrectionFlat()
{
    printf("correction: a flat speaker needs none\n");
    const std::vector<double> g = correctionGrid();
    std::vector<double> m(g.size(), -30.0);
    const ChannelCorrection c = designCorrection(g, m, targetLevel(g, { &m }), CorrectionSpec());
    double worst = 0; for (double v : c.correction) worst = std::max(worst, std::fabs(v));
    CHECK(worst < 0.01, "largest correction %.3f dB", worst);
    CHECK(preampFor({ &c.correction }) > -0.01, "no preamp cut");
}

static void testCorrectionRoom()
{
    printf("correction: a room mode, a broad dip and a narrow null\n");
    const double fs = 48000;
    Path p;
    p.filters = { Bq::peak(fs, 120, 4, 8), Bq::peak(fs, 2500, 1, -5), Bq::peak(fs, 400, 12, -20) };
    const std::vector<double> g = correctionGrid();
    const std::vector<double> m = curveOf(p, fs, -40);
    const ChannelCorrection c = designCorrection(g, m, targetLevel(g, { &m }), CorrectionSpec());
    CHECK(at(g, c.correction, 120) < -6.0, "the 120 Hz mode cut by %.1f dB", at(g, c.correction, 120));
    CHECK(at(g, c.correction, 2500) > 2.0 && std::fabs(at(g, c.result, 2500) - at(g, c.target, 2500)) < 1.0, "the 2.5 kHz dip lifted by %.1f dB, onto the target", at(g, c.correction, 2500));
    CHECK(at(g, c.correction, 400) < 5.0 && at(g, c.target, 400) - (at(g, m, 400) + at(g, c.correction, 400)) > 10.0, "the 400 Hz null (-20 dB, narrow) only lifted %.1f dB, not filled", at(g, c.correction, 400));
    double worst = 0, wf = 0;
    for (size_t i = 0; i < g.size(); ++i)
        if (g[i] > 30 && g[i] < 15000 && std::fabs(g[i] - 400) > 120)
        {
            const double e = std::fabs(c.result[i] - c.target[i]);
            if (e > worst) { worst = e; wf = g[i]; }
        }
    CHECK(worst < 1.5, "after correction within %.2f dB of the target outside the null (worst at %.0f Hz)", worst, wf);
}

static void testCorrectionLowEnd()
{
    printf("correction: no boost below the speaker's roll-off\n");
    const double fs = 48000;
    Path p;
    p.filters = { Bq::highpass(fs, 55, 0.7), Bq::highpass(fs, 55, 0.7) };
    const std::vector<double> g = correctionGrid();
    const std::vector<double> m = curveOf(p, fs, -30);
    const ChannelCorrection c = designCorrection(g, m, targetLevel(g, { &m }), CorrectionSpec());
    CHECK(c.lowGuardHz > 35 && c.lowGuardHz < 70, "roll-off found at %.0f Hz", c.lowGuardHz);
    double mx = -99; for (size_t i = 0; i < g.size(); ++i) if (g[i] <= c.lowGuardHz) mx = std::max(mx, c.correction[i]);
    CHECK(mx < 0.3, "largest boost below it %.2f dB", mx);
    CorrectionSpec off; off.guardLowEnd = false;
    const ChannelCorrection d = designCorrection(g, m, targetLevel(g, { &m }), off);
    CHECK(at(g, d.correction, 40) > 5.5, "with the guard off the bass is boosted (%.1f dB at 40 Hz, limit 6)", at(g, d.correction, 40));
}

static void testCorrectionLimits()
{
    printf("correction: limits, band, amount, target\n");
    const double fs = 48000;
    Path p;
    p.filters = { Bq::peak(fs, 200, 1, 14), Bq::peak(fs, 3000, 0.7, -12) };
    const std::vector<double> g = correctionGrid();
    const std::vector<double> m = curveOf(p, fs, -20);
    CorrectionSpec s; s.maxBoostDb = 4; s.maxCutDb = 9;
    const ChannelCorrection c = designCorrection(g, m, targetLevel(g, { &m }), s);
    double mx = -99, mn = 99; for (double v : c.correction) { mx = std::max(mx, v); mn = std::min(mn, v); }
    CHECK(mx <= 4.001 && mn >= -9.001, "within +4 / -9 dB (%.2f .. %.2f)", mn, mx);
    CHECK(std::fabs(preampFor({ &c.correction }) + mx) < 1e-9, "preamp %.2f dB covers the largest boost", preampFor({ &c.correction }));
    CorrectionSpec b; b.fHi = 500;
    const ChannelCorrection cb = designCorrection(g, m, targetLevel(g, { &m }), b);
    CHECK(std::fabs(at(g, cb.correction, 2000)) < 0.05 && at(g, cb.correction, 200) < -5, "band to 500 Hz: none at 2 kHz (%.2f), still at 200 Hz", at(g, cb.correction, 2000));
    CorrectionSpec h; h.amount = 0.5;
    const ChannelCorrection ch = designCorrection(g, m, targetLevel(g, { &m }), h), cf = designCorrection(g, m, targetLevel(g, { &m }), CorrectionSpec());
    CHECK(std::fabs(at(g, ch.correction, 200) - 0.5 * at(g, cf.correction, 200)) < 1e-9, "half the amount, half the correction");
    // a tilted target on a flat speaker: the result follows the tilt
    std::vector<double> flat(g.size(), -25.0);
    CorrectionSpec t; t.target.tiltDbPerOct = -0.8;
    const ChannelCorrection ct = designCorrection(g, flat, targetLevel(g, { &flat }), t);
    const double slope = (at(g, ct.result, 8000) - at(g, ct.result, 125)) / 6.0;
    CHECK(std::fabs(slope + 0.8) < 0.05, "tilt -0.8 dB/oct: result slopes %.2f dB/oct", slope);
}

// the FIR's own response at the grid frequencies
static std::vector<double> firDb(const std::vector<float>& h, double fs, const std::vector<double>& g)
{
    std::vector<double> out(g.size());
    for (size_t i = 0; i < g.size(); ++i)
    {
        cd s = 0.0;
        const cd step = std::exp(cd(0, -2 * kPi * g[i] / fs));
        cd z = 1.0;
        for (float v : h) { s += (double)v * z; z *= step; }
        out[i] = 20 * std::log10(std::abs(s) + 1e-30);
    }
    return out;
}

static void testFir()
{
    printf("correction filters\n");
    const double fs = 48000;
    Path p;
    p.filters = { Bq::peak(fs, 70, 5, 9), Bq::peak(fs, 900, 2, -4), Bq::peak(fs, 5000, 1, -6) };
    const std::vector<double> g = correctionGrid();
    const std::vector<double> m = curveOf(p, fs, -30);
    const ChannelCorrection c = designCorrection(g, m, targetLevel(g, { &m }), CorrectionSpec());
    const int taps = correctionTaps(fs);
    CHECK(taps == 16384 && correctionTaps(96000) == 32768, "%d taps at 48 kHz, %d at 96 kHz", taps, correctionTaps(96000));
    std::vector<double> sub; for (size_t i = 0; i < g.size(); i += 4) sub.push_back(g[i]);
    for (int lin = 0; lin < 2; ++lin)
    {
        const double delay = 37.3;
        const std::vector<float> h = designFir(g, c.correction, fs, taps, lin == 1, delay, -3.0);
        const std::vector<double> r = firDb(h, fs, sub);
        double worst = 0, wf = 0;
        for (size_t i = 0; i < sub.size(); ++i)
        {
            if (sub[i] < 25 || sub[i] > 19000) continue;
            const double e = std::fabs(r[i] - (curveAt(g, c.correction, sub[i]) - 3.0));
            if (e > worst) { worst = e; wf = sub[i]; }
        }
        CHECK(worst < 0.3, "%s phase: matches the correction within %.3f dB 25 Hz..19 kHz (worst at %.0f Hz)", lin ? "linear" : "minimum", worst, wf);
        // where the energy is: minimum phase at once (after the delay), linear phase around taps/2 + delay
        double e = 0, tot = 0; int at90 = 0;
        for (float v : h) tot += (double)v * v;
        for (size_t i = 0; i < h.size(); ++i) { e += (double)h[i] * h[i]; if (e < 0.9 * tot) at90 = (int)i; }
        int pk = 0; float pv = 0; for (size_t i = 0; i < h.size(); ++i) if (std::fabs(h[i]) > pv) { pv = std::fabs(h[i]); pk = (int)i; }
        if (lin) CHECK(std::abs(pk - (taps / 2 + 37)) <= 1, "linear phase peaks at %d (taps/2 + delay = %d)", pk, taps / 2 + 37);
        else CHECK(pk >= 36 && pk <= 39 && at90 < taps / 8, "minimum phase peaks at %d (delay 37.3), 90%% of the energy by sample %d", pk, at90);
    }
}

static void testProfile()
{
    printf("profiles\n");
    Profile p;
    p.name = "Studio, desk";
    p.preampDb = -4.25;
    p.channels = 2;
    const std::vector<double> g = correctionGrid();
    for (int c = 0; c < 2; ++c)
    {
        p.ch[c].side = c ? 'R' : 'L';
        p.ch[c].delayMs = c ? 0.0 : 0.583;
        p.ch[c].f = g;
        for (size_t i = 0; i < g.size(); ++i)
        {
            p.ch[c].correction.push_back(std::sin(i * 0.1) * 3);
            p.ch[c].measured.push_back(-30 + std::cos(i * 0.07) * 5);
            p.ch[c].target.push_back(-30);
        }
    }
    const std::string text = writeProfile(p);
    Profile q; std::string why;
    const bool ok = parseProfile(text.data(), text.size(), q, why);
    double worst = 0;
    for (int c = 0; c < 2 && ok; ++c)
        for (size_t i = 0; i < g.size(); ++i) worst = std::max(worst, std::fabs(q.ch[c].correction[i] - p.ch[c].correction[i]));
    CHECK(ok && q.channels == 2 && q.name == p.name && std::fabs(q.preampDb + 4.25) < 1e-9 && std::fabs(q.side('L')->delayMs - 0.583) < 1e-9 && worst < 0.0006,
          "written and read back (%zu bytes, %s)", text.size(), why.c_str());
    const char* bad[] = { "", "hello\n", "pump-room-profile 1\nchannel X 0\n", "pump-room-profile 1\nchannel L 0\n20 1\n10 2\n30 0\nend\n",
                          "pump-room-profile 1\nchannel L 0\n20 1\nend\n", "pump-room-profile 1\nname nothing\n" };
    int refused = 0;
    for (const char* b : bad) { Profile r; std::string w; if (!parseProfile(b, std::strlen(b), r, w)) ++refused; }
    CHECK(refused == 6, "%d of 6 broken files refused", refused);
    const char* one = "pump-room-profile 1\n# a comment\nname Left only\nchannel L 1.5\n20 0\n1000 -2\n20000 0\nend\n";
    Profile r; std::string w;
    CHECK(parseProfile(one, std::strlen(one), r, w) && r.channels == 1 && r.side('R') == nullptr && std::fabs(curveAt(r.ch[0].f, r.ch[0].correction, 1000) + 2) < 1e-9, "one channel, comments");
    CHECK(!q.headphones && text.find("kind") == std::string::npos, "a room profile says no kind");
    Profile hp = p; hp.headphones = true;
    const std::string ht = writeProfile(hp);
    Profile hq; std::string hw;
    CHECK(parseProfile(ht.data(), ht.size(), hq, hw) && hq.headphones && hq.channels == 2, "a headphone profile reads back as one");
}

// The plugin's engine: an impulse through it comes out as the designed filters, after one block; AMOUNT and the
// phase mode redesign on the worker; no profile = a straight wire.
static void testEngine()
{
    printf("plugin engine\n");
    using namespace pump::roomfx;
    const double fs = 48000;
    Path p;
    p.filters = { Bq::peak(fs, 90, 4, 8), Bq::peak(fs, 3000, 1, -4) };
    const std::vector<double> g = correctionGrid();
    const std::vector<double> m = curveOf(p, fs, -30);
    const ChannelCorrection c = designCorrection(g, m, targetLevel(g, { &m }), CorrectionSpec());
    Profile prof;
    prof.name = "test";
    prof.channels = 2;
    for (int k = 0; k < 2; ++k)
    {
        prof.ch[k].side = k ? 'R' : 'L';
        prof.ch[k].delayMs = k ? 0.0 : 0.5;
        prof.ch[k].f = g; prof.ch[k].correction = c.correction; prof.ch[k].measured = m; prof.ch[k].target = c.target;
    }
    auto impulse = [&](Engine& e, int n) {
        std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
        L[0] = R[0] = 1.0f;
        for (int i = 0; i < n; i += 512) e.process(L.data() + i, R.data() + i, std::min(512, n - i));
        return std::make_pair(L, R);
    };
    {
        Engine e;
        e.prepare(fs);
        const auto io = impulse(e, 4096);
        CHECK(std::fabs(io.first[Engine::kBlock] - 1.0f) < 1e-5 && e.latency() == Engine::kBlock, "no profile: a wire, %d samples late", e.latency());
    }
    Engine e;
    e.setProfile(&prof);
    e.prepare(fs);
    const int n = 32768;
    const auto io = impulse(e, n);
    DesignInput in; in.profile = prof; in.hasProfile = true; in.fs = fs;
    const Design d = designFilters(in, Engine::kBlock);
    double err = 0, ref = 0;
    for (size_t i = 0; i < d.fir[0].size() && i + Engine::kBlock < (size_t)n; ++i)
    {
        err = std::max(err, (double)std::fabs(io.first[i + Engine::kBlock] - d.fir[0][i]));
        ref = std::max(ref, (double)std::fabs(d.fir[0][i]));
    }
    CHECK(err < 1e-4 * ref && std::fabs(d.delayMs[0] - 0.5) < 1e-9 && d.preampDb < 0, "impulse = the left filter after one block (error %.1e), left delayed 0.5 ms, preamp %.2f dB", err / ref, d.preampDb);
    // the left channel's direct sound 24 samples (0.5 ms) after the right's
    int pl = 0, pr = 0; float vl = 0, vr = 0;
    for (int i = 0; i < 2000; ++i) { if (std::fabs(io.first[(size_t)i]) > vl) { vl = std::fabs(io.first[(size_t)i]); pl = i; } if (std::fabs(io.second[(size_t)i]) > vr) { vr = std::fabs(io.second[(size_t)i]); pr = i; } }
    CHECK(pl - pr == 24, "time alignment: left peaks %d samples after right", pl - pr);
    // half the amount: the worker redesigns, the preamp halves
    Settings s; s.amount = 0.5;
    e.setSettings(s);
    e.settle();
    CHECK(std::fabs(e.info().preampDb - 0.5 * d.preampDb) < 1e-6, "amount 50 %%: preamp %.2f dB (was %.2f)", e.info().preampDb, d.preampDb);
    s.linear = true;
    e.setSettings(s);
    e.settle();
    CHECK(e.latency() == Engine::kBlock + correctionTaps(fs) / 2, "linear phase: latency %d samples", e.latency());
    e.setProfile(nullptr);
    e.settle();
    CHECK(e.info().preampDb == 0.0, "profile cleared");
}

// Phase: a speaker whose bass arrives late (a 4th-order roll-off at 50 Hz) and with a crossover's phase turn (an
// allpass at 2 kHz). The measured group delay follows the real one, and the linear-phase correction lines it up.
static double pathGdMs(const Path& p, double fs, double f)
{
    auto ph = [&](double x) { cd h = 1.0; for (const Bq& b : p.filters) h *= b.at(fs, x); return std::arg(h); };
    const double d = f * 0.002;
    double a = ph(f + d) - ph(f - d);
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return -a / (2 * kPi * 2 * d) * 1000.0;
}

static void testPhase()
{
    printf("phase correction\n");
    const double fs = 48000;
    Path p;
    p.filters = { Bq::highpass(fs, 50, 0.54), Bq::highpass(fs, 50, 1.31), Bq::allpass(fs, 2000, 0.7) };
    p.latency = 1200;
    SweepSpec s; s.sampleRate = fs; s.seconds = 3;
    Measurement m = measure(p, s, 1.5, -95, 11);
    const std::vector<double> g = correctionGrid();
    const std::vector<double> gd = groupDelayMs(m.ir.data(), (int)m.ir.size(), m.arrival.index, fs, g);
    // the true excess group delay, relative to 3..8 kHz like the design
    double ref = 0; int k = 0;
    for (double f : g) if (f >= 3000 && f <= 8000) { ref += pathGdMs(p, fs, f); ++k; }
    ref /= k;
    double mref = 0; k = 0;
    for (size_t i = 0; i < g.size(); ++i) if (g[i] >= 3000 && g[i] <= 8000) { mref += gd[i]; ++k; }
    mref /= k;
    double worst = 0, wf = 0;
    for (size_t i = 0; i < g.size(); ++i)
    {
        if (g[i] < 80 || g[i] > 5000) continue;
        const double e = std::fabs((gd[i] - mref) - (pathGdMs(p, fs, g[i]) - ref));
        if (e > worst) { worst = e; wf = g[i]; }
    }
    CHECK(worst < 0.8, "measured group delay follows the speaker's within %.3f ms, 80 Hz..5 kHz (worst at %.0f Hz; %.1f ms at 80 Hz)", worst, wf, pathGdMs(p, fs, 80) - ref);

    PhaseSpec ps; ps.on = true;
    const std::vector<double> corr = designPhaseCorrection(g, gd, ps);
    const int taps = correctionTaps(fs);
    std::vector<double> zero(g.size(), 0.0);
    const std::vector<float> h = designFir(g, zero, fs, taps, true, 0.0, 0.0, &corr);
    // speaker + filter: group delay flat (= the filter's latency) across the band
    auto firPh = [&](double f) {
        cd sum = 0.0; const cd step = std::exp(cd(0, -2 * kPi * f / fs)); cd z = 1.0;
        for (float v : h) { sum += (double)v * z; z *= step; }
        return sum;
    };
    auto totalGd = [&](double f) {
        const double d = 0.1;   // Hz: the filter's own latency turns the phase fast
        auto ph = [&](double x) { cd hh = firPh(x); for (const Bq& b : p.filters) hh *= b.at(fs, x); return std::arg(hh); };
        double a = ph(f + d) - ph(f - d);
        while (a > kPi) a -= 2 * kPi;
        while (a < -kPi) a += 2 * kPi;
        return -a / (2 * kPi * 2 * d) * 1000.0;
    };
    const double centre = taps / 2.0 / fs * 1000.0;
    double before = 0, after = 0;
    for (double f : { 100.0, 150.0, 250.0, 500.0, 1000.0, 1500.0, 2000.0, 2500.0 })
    {
        before = std::max(before, std::fabs(pathGdMs(p, fs, f) - ref));
        after = std::max(after, std::fabs(totalGd(f) - centre - ref));
    }
    CHECK(after < 0.5 && before > 2.0, "spread of arrival times 100 Hz..2.5 kHz: %.2f ms before, %.2f ms after", before, after);
    const std::vector<double> magDb = firDb(h, fs, std::vector<double>{ 100, 1000, 5000 });
    CHECK(std::fabs(magDb[0]) < 0.1 && std::fabs(magDb[1]) < 0.1 && std::fabs(magDb[2]) < 0.1, "the phase filter leaves the level alone (%.3f / %.3f / %.3f dB)", magDb[0], magDb[1], magDb[2]);
    PhaseSpec off;
    const std::vector<double> none = designPhaseCorrection(g, gd, off);
    double mx = 0; for (double v : none) mx = std::max(mx, std::fabs(v));
    CHECK(mx == 0.0, "off: no phase correction");
    // in a room (reflections): the measurement still finds the speaker's group delay in the mids
    Path r = p;
    r.taps = { { 0, 0.5 }, { (int)(0.0042 * fs), 0.3 }, { (int)(0.0091 * fs), 0.2 } };
    Measurement mr = measure(r, s, 1.5, -95, 12);
    const std::vector<double> gr = groupDelayMs(mr.ir.data(), (int)mr.ir.size(), mr.arrival.index, fs, g);
    double rref = 0; k = 0;
    for (size_t i = 0; i < g.size(); ++i) if (g[i] >= 3000 && g[i] <= 8000) { rref += gr[i]; ++k; }
    rref /= k;
    double rw = 0;
    for (size_t i = 0; i < g.size(); ++i) if (g[i] >= 300 && g[i] <= 3000) rw = std::max(rw, std::fabs((gr[i] - rref) - (pathGdMs(p, fs, g[i]) - ref)));
    CHECK(rw < 0.5, "with reflections 4.4 dB down: within %.2f ms of the speaker 300 Hz..3 kHz", rw);
}

static void testCorrectionPositions()
{
    printf("correction: weighted positions, spread, spread-aware, a base target curve\n");
    const double fs = 48000;
    const std::vector<double> g = correctionGrid();
    // weights: a -6 dB curve at weight 3 and a 0 dB one at weight 1 average (in power) to 10 log10((3/4 + 1/4) ... )
    std::vector<double> a(g.size(), -6.0), b(g.size(), 0.0);
    const std::vector<double> avg = powerAverage({ a, b }, { 3.0, 1.0 });
    const double want = 10.0 * std::log10(0.75 * std::pow(10.0, -0.6) + 0.25);
    CHECK(std::fabs(avg[100] - want) < 1e-9, "weighted power average %.3f dB (want %.3f)", avg[100], want);
    const std::vector<double> eq = powerAverage({ a, b });
    CHECK(std::fabs(eq[100] - 10.0 * std::log10(0.5 * std::pow(10.0, -0.6) + 0.5)) < 1e-9, "no weights: plain average");
    // spread: a mode at 80 Hz that moves from +12 to -6 dB between positions, the rest the same everywhere
    std::vector<std::vector<double>> pos;
    const double modes[6] = { 12, 6, 0, -6, 9, 3 };
    for (double m : modes)
    {
        Path p; p.filters = { Bq::peak(fs, 80, 4, m), Bq::peak(fs, 3000, 1, -6) };
        pos.push_back(curveOf(p, fs, -20));
    }
    const std::vector<double> sp = positionSpread(g, pos);
    CHECK(at(g, sp, 80) > 4.0 && at(g, sp, 3000) < 0.05, "spread %.1f dB at the moving mode, %.2f dB at the steady dip", at(g, sp, 80), at(g, sp, 3000));
    const std::vector<double> mean = powerAverage(pos);
    CorrectionSpec s;
    const ChannelCorrection full = designCorrection(g, mean, targetLevel(g, { &mean }), s);
    s.trust = spreadTrust(g, sp);
    const ChannelCorrection aware = designCorrection(g, mean, targetLevel(g, { &mean }), s);
    CHECK(std::fabs(at(g, aware.correction, 80)) < 0.75 * std::fabs(at(g, full.correction, 80)),
          "spread-aware: the moving mode corrected less (%.1f vs %.1f dB)", at(g, aware.correction, 80), at(g, full.correction, 80));
    CHECK(std::fabs(at(g, aware.correction, 3000) - at(g, full.correction, 3000)) < 0.1,
          "spread-aware: the steady dip corrected the same (%.1f vs %.1f dB)", at(g, aware.correction, 3000), at(g, full.correction, 3000));
    // a base target curve: a flat response is corrected onto it
    CorrectionSpec t;
    t.target.curveF = { 20, 1000, 3000, 20000 };
    t.target.curveDb = { 4, 0, 8, 0 };
    std::vector<double> flat(g.size(), -25.0);
    const ChannelCorrection ct = designCorrection(g, flat, targetLevel(g, { &flat }), t);
    const double rise = at(g, ct.result, 3000) - at(g, ct.result, 1000);
    double mx = -99; for (double v : ct.correction) mx = std::max(mx, v);
    CHECK(rise > 5.0 && mx <= 6.001, "the result follows the base curve: +%.1f dB from 1 to 3 kHz, boosts within 6 dB (%.2f)", rise, mx);
    t.maxBoostDb = 12;
    const ChannelCorrection ct2 = designCorrection(g, flat, targetLevel(g, { &flat }), t);
    const double rise2 = at(g, ct2.result, 3000) - at(g, ct2.result, 1000);
    CHECK(std::fabs(rise2 - 8.0) < 0.6, "with room to boost it reaches the curve: +%.1f dB", rise2);
}

int main()
{
    testSweep();
    testLoopback();
    testRoom();
    testTwoChannels();
    testFractional();
    testStrongReflection();
    testCalibration();
    testCalFormats();
    testReload();
    testLevels();
    testCorrectionFlat();
    testCorrectionRoom();
    testCorrectionLowEnd();
    testCorrectionLimits();
    testCorrectionPositions();
    testFir();
    testProfile();
    testEngine();
    testPhase();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
