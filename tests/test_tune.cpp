// pump TUNE: with nothing to correct the output is the input delayed by the latency; hard tune puts a sharp note on
// pitch; RETUNE sets how fast; HUMANIZE keeps vibrato; FLEX leaves near notes alone; TRANSPOSE shifts by an octave
// with the formants kept (and moving with FORMANTS FOLLOW); FORMANT moves the formants without the pitch; Scale
// harmonies land on the scale's third and fifth; MIDI lead and harmony notes; Graph notes move their stretch of the
// voice and nothing else; CAPTURE sends frames with timeline positions; the latency is reported; random settings and
// block sizes stay finite and bounded. 44.1, 48 and 96 kHz. Prints CPU.
#include "../plugins/tune/TuneEngine.hpp"
#include "../plugins/tune/TuneNotes.hpp"
#include "dsp/FFT.hpp"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::tune;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

// a sung vowel: glottal pulses at f0(t) (Hz per sample) through three formants, about -16 dBFS
static std::vector<float> sing(double sr, const std::vector<double>& f0, int vowel = 0)
{
    static const double F[3][3] = { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 530, 1840, 2480 } };
    struct Res { double b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; void set(double f, double bw, double s) { double R = std::exp(-kPi * bw / s), th = kTwoPi * f / s; a1 = -2 * R * std::cos(th); a2 = R * R; b0 = 1 - R; }
                 double run(double x) { double y = b0 * x - a1 * z1 - a2 * z2; z2 = z1; z1 = y; return y; } } rs[3];
    for (int k = 0; k < 3; ++k) rs[k].set(F[vowel][k], 80 + 40 * k, sr);
    std::vector<float> x(f0.size());
    double ph = 0, prev = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        ph += f0[i] / sr;
        double p = 0; if (ph >= 1) { ph -= 1; p = 1; }
        // a glottal-ish pulse: a short smoothed pulse rather than a single sample
        const double g = 0.6 * p + 0.4 * prev; prev = p;
        double v = 0; for (int k = 0; k < 3; ++k) v += rs[k].run(g) * (k == 0 ? 1.0 : 0.5);
        x[i] = (float)v;
    }
    double e = 0; for (float s : x) e += (double)s * s;
    const double r = std::sqrt(e / x.size());
    for (float& s : x) s = (float)(s * 0.15 / r);
    return x;
}
static std::vector<double> steady(double hz, size_t n) { return std::vector<double>(n, hz); }
static double midiOf(double hz) { return pitch::hzToMidi(hz); }

static void run(Engine& e, const std::vector<float>& x, std::vector<float>& L, std::vector<float>& R, int block = 256, const MidiMsg* ev = nullptr, int nev = 0)
{
    L.assign(x.size(), 0); R.assign(x.size(), 0);
    for (size_t off = 0; off < x.size(); off += (size_t)block)
    {
        const int k = (int)std::min<size_t>((size_t)block, x.size() - off);
        e.process(x.data() + off, x.data() + off, L.data() + off, R.data() + off, k, off == 0 ? ev : nullptr, off == 0 ? nev : 0);
    }
}
// the median pitch (MIDI) the tracker finds in y[from, to)
static double pitchOf(const std::vector<float>& y, double sr, size_t from, size_t to)
{
    pitch::Tracker t; t.prepare(sr, 50, 1500, 24000);
    std::vector<double> v;
    for (size_t i = from; i < to && i < y.size(); ++i) if (t.push(y[i]) && i > from + (size_t)(0.05 * sr) && t.result().voiced) v.push_back(midiOf(t.result().hz));
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
// where the upper formants sit: energy at 4-7 kHz against 1.9-3.5 kHz (dB). The test vowel /i/ has F2 / F3 at
// 2290 / 3010 Hz: they move into the upper band when the formants go up an octave and out of both when they go down.
static double centroid(const std::vector<float>& x, size_t from, double sr, int N = 8192)
{
    RealFFT f(N); AlignedBuffer a((size_t)N), s((size_t)N);
    double hi = 0, lo = 0;
    for (size_t off = from; off + (size_t)N <= x.size(); off += (size_t)N)
    {
        for (int i = 0; i < N; ++i) a[(size_t)i] = x[off + (size_t)i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        f.forward(a.data(), s.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const double fr = k * sr / N;
            const double p = (double)s[(size_t)(2 * k)] * s[(size_t)(2 * k)] + (double)s[(size_t)(2 * k + 1)] * s[(size_t)(2 * k + 1)];
            if (fr >= 4000 && fr <= 7000) hi += p;
            else if (fr >= 1900 && fr <= 3500) lo += p;
        }
    }
    return 10 * std::log10((hi + 1e-30) / (lo + 1e-30));
}
static double rmsDb(const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0; to = std::min(to, x.size()); for (size_t i = from; i < to; ++i) e += (double)x[i] * x[i];
    return 10 * std::log10(e / std::max<size_t>(1, to - from) + 1e-30);
}
static Settings base() { Settings s; s.scale = pitch::kChromatic; s.harmMode = kHarmOff; return s; }

static void testIdentity(double sr)
{
    printf("Identity (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 2);
    // a voice exactly on A3: nothing to correct
    const auto x = sing(sr, steady(220.0, n));
    Settings s = base();
    Engine e; e.setSettings(s); e.prepare(sr, 256);
    std::vector<float> L, R; run(e, x, L, R);
    const int lat = e.latency();
    CHECK(lat == Engine::latencyFor(sr, kRangeMid), "latency %d samples (%.1f ms)", lat, 1000.0 * lat / sr);
    double err = 0, ref = 0;
    for (size_t i = (size_t)(0.5 * sr); i + (size_t)lat < n; ++i) { const double d = L[i + (size_t)lat] - x[i]; err += d * d; ref += (double)x[i] * x[i]; }
    const double snr = -10 * std::log10(err / ref);
    (void)snr;
    // PSOLA re-lays the same cycles (a constant shift in time, not sample-exact): the best correlation within two periods
    double best = 0;
    for (int d = -440; d <= 440; ++d)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = (size_t)(0.5 * sr); i + (size_t)(lat + 500) < n; i += 3) { const double a = x[i], b = L[(size_t)((int64_t)i + lat + d)]; xy += a * b; xx += a * a; yy += b * b; }
        best = std::max(best, xy / std::sqrt(xx * yy + 1e-30));
    }
    CHECK(best > 0.97, "on-pitch voice comes through unchanged: correlation %.3f", best);
    CHECK(std::fabs(rmsDb(L, (size_t)sr, n) - rmsDb(x, (size_t)sr, n)) < 0.5, "level %.2f dB vs %.2f dB", rmsDb(L, (size_t)sr, n), rmsDb(x, (size_t)sr, n));
}

static void testCorrection(double sr)
{
    printf("Correction (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 2);
    // 35 cents sharp of A3
    const double hz = 220.0 * std::exp2(0.35 / 12.0);
    const auto x = sing(sr, steady(hz, n));
    {
        Settings s = base(); s.retuneMs = 0;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, x, L, R);
        const double p = pitchOf(L, sr, (size_t)(0.6 * sr), n);
        CHECK(std::fabs(p - 57.0) < 0.06, "hard tune: %.3f (A3 = 57; sung at %.3f)", p, midiOf(hz));
    }
    {
        Settings s = base(); s.retuneMs = 0; s.amount = 50;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, x, L, R);
        const double p = pitchOf(L, sr, (size_t)(0.6 * sr), n);
        CHECK(std::fabs(p - (57.0 + 0.175)) < 0.06, "amount 50 %%: %.3f (half way: 57.175)", p);
    }
    {
        Settings s = base(); s.flexCents = 50;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, x, L, R);
        const double p = pitchOf(L, sr, (size_t)(0.6 * sr), n);
        CHECK(std::fabs(p - midiOf(hz)) < 0.06, "flex 50 c leaves a 35 c error alone: %.3f", p);
    }
    // C major: a voice on F#3 + 10 c goes to F3 or G3 (54.1 -> nearest in scale is G3: 55 is 0.9 away, F3 1.1)
    {
        const auto y = sing(sr, steady(pitch::midiToHz(54.1), n));
        Settings s = base(); s.scale = pitch::kMajor; s.key = 0; s.retuneMs = 0;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, y, L, R);
        const double p = pitchOf(L, sr, (size_t)(0.6 * sr), n);
        CHECK(std::fabs(p - 55.0) < 0.06, "C major: 54.1 -> %.3f (G3 = 55)", p);
    }
    // retune: with 200 ms the correction is still under way after 60 ms of a note, done after a second
    {
        Settings s = base(); s.retuneMs = 200;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, x, L, R);
        const double early = pitchOf(L, sr, (size_t)(0.05 * sr), (size_t)(0.18 * sr));
        const double late = pitchOf(L, sr, (size_t)(1.2 * sr), n);
        CHECK(early > 57.1 && std::fabs(late - 57.0) < 0.06, "retune 200 ms: %.3f early, %.3f after a second", early, late);
    }
    // humanize: a 6 Hz vibrato of +-40 c around A3 + 30 c keeps its swing, the centre goes to A3
    {
        std::vector<double> f(n);
        for (size_t i = 0; i < n; ++i) f[i] = 220.0 * std::exp2((0.30 + 0.40 * std::sin(kTwoPi * 6.0 * i / sr)) / 12.0);
        const auto y = sing(sr, f);
        for (int hum = 0; hum <= 1; ++hum)
        {
            Settings s = base(); s.retuneMs = 0; s.humanize = hum ? 100 : 0;
            Engine e; e.setSettings(s); e.prepare(sr, 256);
            std::vector<float> L, R; run(e, y, L, R);
            pitch::Tracker t; t.prepare(sr, 50, 1500, 24000);
            double lo = 1e9, hi = -1e9, sum = 0; int cnt = 0;
            for (size_t i = 0; i < n; ++i) if (t.push(L[i]) && i > (size_t)(0.8 * sr) && t.result().voiced) { const double m = midiOf(t.result().hz); lo = std::min(lo, m); hi = std::max(hi, m); sum += m; ++cnt; }
            const double swing = (hi - lo) * 100.0, centre = sum / std::max(1, cnt);
            if (hum) CHECK(swing > 50.0 && std::fabs(centre - 57.0) < 0.12, "humanize 100 %%: vibrato swing %.0f c kept, centre %.3f", swing, centre);
            else CHECK(swing < 35.0, "humanize 0, retune 0: vibrato flattened to %.0f c", swing);
        }
    }
}

static void testShiftAndFormant(double sr)
{
    printf("Transpose and formant (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 2);
    const auto x = sing(sr, steady(196.0, n), 1);   // G3, /i/
    const double c0 = centroid(x, (size_t)(0.5 * sr), sr);
    auto runS = [&](Settings s, double& p, double& c) {
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, x, L, R);
        p = pitchOf(L, sr, (size_t)(0.5 * sr), n);
        c = centroid(L, (size_t)(0.5 * sr), sr);
    };
    double p, c;
    Settings s = base(); s.transpose = 12; s.retuneMs = 0;
    runS(s, p, c);
    CHECK(std::fabs(p - (midiOf(196.0) + 12)) < 0.08 && std::fabs(c - c0) < 4.0, "+12: pitch %.2f (want %.2f), upper formants %+.1f dB vs %+.1f dB (kept)", p, midiOf(196.0) + 12, c, c0);
    s.formantFollow = true;
    runS(s, p, c);
    CHECK(c > c0 + 10.0, "+12 with formants following: upper formants %+.1f dB (from %+.1f)", c, c0);
    s = base(); s.formant = 5;
    runS(s, p, c);
    CHECK(std::fabs(p - midiOf(196.0)) < 0.08 && c > c0 + 6.0, "formant +5: pitch %.2f kept, upper formants %+.1f dB (from %+.1f)", p, c, c0);
    s.formant = -5;
    runS(s, p, c);
    CHECK(std::fabs(p - midiOf(196.0)) < 0.08 && c < c0 - 3.0, "formant -5: pitch %.2f kept, upper formants %+.1f dB (from %+.1f)", p, c, c0);
}

static void testHarmonyAndMidi(double sr)
{
    printf("Harmony and MIDI (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 1.5);
    const auto x = sing(sr, steady(pitch::midiToHz(52.0), n));   // E3
    auto voiceOnly = [&](Settings s, const MidiMsg* ev, int nev) {
        s.lead = 0;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, x, L, R, 256, ev, nev);
        std::vector<float> m(L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (L[i] + R[i]);
        return pitchOf(m, sr, (size_t)(0.5 * sr), n);
    };
    Settings s = base(); s.scale = pitch::kMajor; s.key = 0; s.retuneMs = 0; s.harmMode = kHarmScale;
    s.interval[0] = 2; s.level[0] = 100; s.level[1] = 0; s.level[2] = 0; s.level[3] = 0; s.harmHumanize = 0;
    double p = voiceOnly(s, nullptr, 0);
    CHECK(std::fabs(p - 55.0) < 0.1, "C major, a third above E3: %.2f (G3 = 55)", p);
    s.interval[0] = 4; p = voiceOnly(s, nullptr, 0);
    CHECK(std::fabs(p - 59.0) < 0.1, "a fifth above E3: %.2f (B3 = 59)", p);
    s.harmMode = kHarmFixed; s.interval[0] = -12; p = voiceOnly(s, nullptr, 0);
    CHECK(std::fabs(p - 40.0) < 0.1, "fixed -12: %.2f (E2 = 40)", p);
    // MIDI lead: D3 held, the voice on E3 sings D3
    {
        Settings t = base(); t.midi = kMidiLead; t.retuneMs = 0;
        const MidiMsg ev[1] = { { 0, 0x90, 50, 100 } };
        Engine e; e.setSettings(t); e.prepare(sr, 256);
        std::vector<float> L, R; run(e, x, L, R, 256, ev, 1);
        p = pitchOf(L, sr, (size_t)(0.5 * sr), n);
        CHECK(std::fabs(p - 50.0) < 0.08, "MIDI lead D3: %.2f", p);
    }
    // MIDI harmony: A3 held -> harmony voice 1 on A3
    {
        Settings t = base(); t.midi = kMidiHarmony; t.level[0] = 100; t.level[1] = t.level[2] = t.level[3] = 0; t.harmHumanize = 0;
        const MidiMsg ev[1] = { { 0, 0x90, 57, 100 } };
        p = voiceOnly(t, ev, 1);
        CHECK(std::fabs(p - 57.0) < 0.1, "MIDI harmony A3: %.2f", p);
    }
}

static void testGraph(double sr)
{
    printf("Graph (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 3);
    const auto x = sing(sr, steady(220.0, n));
    auto list = std::make_shared<NoteList>();
    GraphNote g; g.t0 = 1.0; g.t1 = 2.0; g.src = 57.0f; g.dst = 59.0f;
    list->notes.push_back(g);
    Settings s = base(); s.mode = kGraph;
    Engine e; e.setSettings(s); e.prepare(sr, 256);
    e.setNotes(list);
    e.setTimeline(true, 0.0);
    e.setCapture(true);
    std::vector<float> L(n), R(n);
    for (size_t off = 0; off < n; off += 256) e.process(x.data() + off, x.data() + off, L.data() + off, R.data() + off, (int)std::min<size_t>(256, n - off));
    const int lat = e.latency();
    const double before = pitchOf(L, sr, (size_t)(0.3 * sr) + lat, (size_t)(0.9 * sr) + lat);
    const double during = pitchOf(L, sr, (size_t)(1.15 * sr) + lat, (size_t)(1.9 * sr) + lat);
    const double after = pitchOf(L, sr, (size_t)(2.2 * sr) + lat, n);
    CHECK(std::fabs(before - 57.0) < 0.06 && std::fabs(during - 59.0) < 0.06 && std::fabs(after - 57.0) < 0.06,
          "a note moved A3 -> B3 for 1..2 s: %.3f / %.3f / %.3f", before, during, after);
    // capture: frames arrived, with times that advance with the timeline and the voice's pitch
    const uint32_t w = e.capW.load(), r0 = e.capR.load();
    int good = 0, total = 0; double lastT = -1; bool mono = true;
    for (uint32_t i = r0; i < w; ++i)
    {
        const CaptureFrame& f = e.capRing[i & (Engine::kCapRing - 1)];
        if (f.t < lastT) mono = false;
        lastT = f.t; ++total;
        if (f.midi > 0 && std::fabs(f.midi - 57.0f) < 0.1f) ++good;
    }
    CHECK(total > 500 && mono && good > total * 0.9 && lastT > 2.8, "capture: %d frames, %d on A3, last at %.2f s", total, good, lastT);
}

// the notes / curve texts round-trip; the note finder splits a melody into its notes (through vibrato, a slide, a
// consonant gap and a one-frame slip) and finds them in a captured performance
static void testNotes(double sr)
{
    printf("Notes (%.0f Hz)\n", sr);
    std::vector<GraphNote> ns;
    for (int i = 0; i < 5; ++i) { GraphNote g; g.t0 = 1.0 + i; g.t1 = 1.7 + i; g.src = 55.25f + i; g.dst = 57.0f + i; g.straighten = 0.1f * i; g.vibrato = 0.5f + 0.2f * i; g.formant = -2.0f + i; g.gainDb = -3.0f + i; ns.push_back(g); }
    const auto back = notesFromText(notesToText(ns).c_str());
    bool same = back.size() == ns.size();
    for (size_t i = 0; same && i < ns.size(); ++i)
        same = std::fabs(back[i].t0 - ns[i].t0) < 1e-3 && std::fabs(back[i].t1 - ns[i].t1) < 1e-3 && std::fabs(back[i].src - ns[i].src) < 1e-3f && std::fabs(back[i].dst - ns[i].dst) < 1e-3f
            && std::fabs(back[i].straighten - ns[i].straighten) < 1e-3f && std::fabs(back[i].vibrato - ns[i].vibrato) < 1e-3f && std::fabs(back[i].formant - ns[i].formant) < 0.01f && std::fabs(back[i].gainDb - ns[i].gainDb) < 0.01f;
    CHECK(same, "notes text round trip (%zu notes)", back.size());
    // a curve: A3 with vibrato, a slide to C4, a gap, E4, a one-frame octave slip inside it
    std::vector<CurveFrame> c;
    const double hop = 0.0053;
    double t = 2.0;
    for (int i = 0; i < 100; ++i, t += hop) c.push_back({ t, (float)(57.0 + 0.3 * std::sin(kTwoPi * 6.0 * i * hop)) });
    for (int i = 0; i < 8; ++i, t += hop) c.push_back({ t, (float)(57.0 + 3.0 * i / 8.0) });
    for (int i = 0; i < 80; ++i, t += hop) c.push_back({ t, 60.05f });
    for (int i = 0; i < 12; ++i, t += hop) c.push_back({ t, -1.0f });
    for (int i = 0; i < 90; ++i, t += hop) c.push_back({ t, i == 40 ? 76.0f : 64.0f });
    const auto cb = curveFromText(curveToText(c).c_str());
    double maxErr = 0; bool timesOk = cb.size() == c.size();
    for (size_t i = 0; timesOk && i < c.size(); ++i) { timesOk = std::fabs(cb[i].t - c[i].t) < 1e-4; if (c[i].midi > 0) maxErr = std::max(maxErr, (double)std::fabs(cb[i].midi - c[i].midi)); else timesOk = cb[i].midi < 0; }
    CHECK(timesOk && maxErr < 0.02, "curve text round trip: %zu frames, worst pitch error %.3f", cb.size(), maxErr);
    const auto found = findNotes(c, 0, c.size());
    CHECK(found.size() == 3 && std::fabs(found[0].src - 57.0f) < 0.15f && std::fabs(found[1].src - 60.05f) < 0.05f && std::fabs(found[2].src - 64.0f) < 0.05f,
          "the finder: %zu notes (A3, C4, E4): %.2f %.2f %.2f", found.size(), found.size() > 0 ? found[0].src : 0.f, found.size() > 1 ? found[1].src : 0.f, found.size() > 2 ? found[2].src : 0.f);
    // a captured performance: A3 C4 E4 D4, 0.4 s each with 0.1 s gaps
    std::vector<double> f0;
    const double mel[4] = { 57, 60, 64, 62 };
    for (int k = 0; k < 4; ++k) { for (int i = 0; i < (int)(0.4 * sr); ++i) f0.push_back(pitch::midiToHz(mel[k])); for (int i = 0; i < (int)(0.1 * sr); ++i) f0.push_back(0.0); }
    auto x = sing(sr, f0);
    for (size_t i = 0; i < x.size(); ++i) if (f0[i] == 0.0) x[i] = 0.0f;
    Settings st = base();
    Engine e; e.setSettings(st); e.prepare(sr, 256); e.setTimeline(true, 30.0); e.setCapture(true);
    std::vector<float> L(x.size()), R(x.size());
    std::vector<CurveFrame> cap;
    for (size_t off = 0; off < x.size(); off += 256)
    {
        const int k = (int)std::min<size_t>(256, x.size() - off);
        e.process(x.data() + off, x.data() + off, L.data() + off, R.data() + off, k);
        uint32_t w = e.capW.load(), r = e.capR.load();
        for (; r != w; ++r) { const CaptureFrame& fr = e.capRing[r & (Engine::kCapRing - 1)]; cap.push_back({ fr.t, fr.midi }); }
        e.capR.store(r);
    }
    const auto got = findNotes(cap, 0, cap.size());
    bool ok = got.size() == 4;
    for (size_t k = 0; ok && k < 4; ++k) ok = std::fabs(got[k].src - mel[k]) < 0.1 && std::fabs(got[k].t0 - (30.0 + 0.5 * k)) < 0.05 && std::fabs(got[k].t1 - (30.4 + 0.5 * k)) < 0.05;
    CHECK(ok, "captured melody: %zu notes found%s", got.size(), ok ? ", pitches and times within 0.1 st / 50 ms" : "");
}

static void testFuzz(double sr)
{
    printf("Random settings (%.0f Hz)\n", sr);
    std::mt19937 rng(7);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto I = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    const size_t n = (size_t)(sr * 0.8);
    std::vector<double> f(n);
    for (size_t i = 0; i < n; ++i) f[i] = 150.0 * std::exp2(std::sin(i / sr * 3.0));
    auto x = sing(sr, f);
    fx::Rng nz(5);
    for (size_t i = (size_t)(0.5 * sr); i < (size_t)(0.6 * sr); ++i) x[i] = (float)(0.1 * nz.bi());   // a consonant
    bool ok = true; double peak = 0;
    for (int t = 0; t < 30; ++t)
    {
        Settings s;
        s.mode = I(0, 1); s.key = I(0, 11); s.scale = I(0, pitch::kNumScales - 1); s.midi = I(0, 2); s.range = I(0, 2);
        s.retuneMs = U(0, 400); s.amount = U(0, 100); s.humanize = U(0, 100); s.flexCents = U(0, 100); s.vibDepth = U(0, 100); s.vibRate = U(2, 9);
        s.transpose = I(-24, 24); s.formant = U(-12, 12); s.formantFollow = I(0, 1); s.harmMode = I(0, 2);
        for (int h = 0; h < kVoices; ++h) { s.interval[h] = I(-12, 12); s.level[h] = U(0, 100); s.pan[h] = U(-100, 100); }
        s.harmFormant = U(-12, 12); s.harmHumanize = U(0, 100); s.lead = U(0, 100); s.harmLevelDb = U(-24, 12); s.outDb = U(-24, 6);
        Engine e; e.setSettings(s); e.prepare(sr, 512);
        auto list = std::make_shared<NoteList>();
        for (int k = 0; k < 5; ++k) { GraphNote g; g.t0 = 0.15 * k; g.t1 = g.t0 + 0.1; g.src = 50; g.dst = (float)I(40, 70); g.straighten = (float)U(0, 1); g.vibrato = (float)U(0, 2); g.formant = (float)U(-6, 6); g.gainDb = (float)U(-12, 6); list->notes.push_back(g); }
        e.setNotes(list);
        e.setTimeline(true, 0.0);
        std::vector<float> L(n), R(n);
        const MidiMsg ev[2] = { { 0, 0x90, (uint8_t)I(40, 70), 100 }, { 5, 0x90, (uint8_t)I(40, 70), 100 } };
        size_t off = 0; bool first = true;
        while (off < n)
        {
            const int k = (int)std::min<size_t>((size_t)I(1, 900), n - off);
            e.process(x.data() + off, x.data() + off, L.data() + off, R.data() + off, k, first ? ev : nullptr, first ? 2 : 0);
            first = false; off += (size_t)k;
        }
        for (size_t i = 0; i < n; ++i) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) ok = false; peak = std::max(peak, (double)std::max(std::fabs(L[i]), std::fabs(R[i]))); }
    }
    CHECK(ok && peak < 4.0, "30 random settings / notes / MIDI / block sizes: finite, peak %.2f", peak);
}

static void cpu()
{
    printf("CPU (48 kHz)\n");
    const double sr = 48000; const size_t n = (size_t)sr * 4;
    const auto x = sing(sr, steady(200.0, n));
    for (int heavy = 0; heavy < 2; ++heavy)
    {
        Settings s = base();
        if (heavy) { s.harmMode = kHarmScale; s.scale = pitch::kMajor; for (int h = 0; h < kVoices; ++h) s.level[h] = 70; s.range = kRangeLow; s.formant = 3; }
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R;
        const auto t0 = std::chrono::steady_clock::now();
        run(e, x, L, R);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  %s: %.2f%% of a core\n", heavy ? "lead + 4 harmonies, low range, formant" : "lead only", 100.0 * secs / 4.0);
    }
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        testIdentity(sr);
        testCorrection(sr);
        testShiftAndFormant(sr);
        testHarmonyAndMidi(sr);
        testGraph(sr);
        testNotes(sr);
        testFuzz(sr);
    }
    cpu();
    if (fails) { printf("%d FAILURE(S)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
