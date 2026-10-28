// pump ROOM measurement: sweeps through a simulated room (a speaker with a room mode, a dip, roll-offs, reflections,
// latency, noise and a microphone with its own tilt) must come back as the response that was put in, with the
// arrival times and polarities right; and calibration files in the formats people actually have must read.
#include "../common/dsp/Room.hpp"
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
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
