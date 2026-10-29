// pump ACID: every model plays a MIDI note at its pitch; overlapping MIDI notes slide; the sequencer's steps land on
// the song grid (SONG, swing), TIE holds a note, SLIDE glides into the next step without a new attack, rests are
// silent, KEYS transposes by the key and stops with it, SONG stops with the host; the diode ladder's table puts the
// resonance at the cutoff and it rings near it at full resonance; the random generator keeps its rules over
// thousands of patterns (the root first, notes in the scale and range, accents only on notes and never four in a
// row, slides only into notes) and MUTATE keeps most of a pattern; random settings and MIDI stay finite; every
// model's default line sits near the same loudness. 44.1 and 48 kHz. CPU.
#include "../plugins/acid/AcidEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::acid;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings plain(int model)   // open filter, no envelope, no resonance
{
    Settings s; s.model = model; s.seqMode = kSeqOff; s.cutoff = 8000; s.res = 0; s.envMod = 0; s.accent = 0; s.keyTrack = 0;
    s.attack = 0.001; s.decay = 3.0; s.sustain = 1.0; s.release = 0.05; s.sub = 0; s.osc2 = 0; s.noise = 0; s.drive = 0;
    return s;
}
static void render(Engine& e, std::vector<float>& L, const std::vector<synth::Midi>& ev, Transport tr = Transport(), double sr = 48000.0)
{
    std::vector<float> R(L.size());
    const int n = (int)L.size();
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.setTransport(tr);
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
        tr.ppq += m * tr.bpm / 60.0 / sr;
    }
}
static double freqOf(const std::vector<float>& x, size_t a, size_t b, double sr)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * sr / (last - first) : 0.0;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double db(double g) { return 20.0 * std::log10(g + 1e-12); }
// the first sample after `from` where the level rises above -50 dBFS
static long onsetAfter(const std::vector<float>& x, size_t from)
{
    for (size_t i = from; i < x.size(); ++i) if (std::fabs(x[i]) > 0.003f) return (long)i;
    return -1;
}

int main()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)sr;
        // ---- pitch, every model
        for (int m = 0; m < kNumModels; ++m)
        {
            Engine e; Settings s = plain(m); e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 45, 90 } }, Transport(), sr);
            const double f = freqOf(L, (size_t)n / 4, (size_t)n, sr);
            CHECK(std::fabs(f - 110.0) < 0.2, "%s: A2 plays at %.2f Hz", kModelNames[m], f);
        }
        // ---- an overlapping MIDI note slides (no new attack): the pitch moves, the level doesn't dip
        {
            Engine e; Settings s = plain(kAcid); s.slide = 0.06; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 45, 90 }, { (uint32_t)(n / 2), 0x90, 57, 90 }, { (uint32_t)(n / 2 + 10), 0x80, 45, 0 } }, Transport(), sr);
            const double f = freqOf(L, (size_t)(n / 2 + n / 8), (size_t)n, sr);
            const double before = rms(L, (size_t)(n / 2 - 2000), (size_t)(n / 2)), during = rms(L, (size_t)(n / 2), (size_t)(n / 2 + 1500));
            CHECK(std::fabs(f - 220.0) < 0.5 && during > 0.5 * before, "MIDI legato slides to A3 (%.1f Hz), level held (%.1f -> %.1f dB)", f, db(before), db(during));
        }
        // ---- the diode ladder: resonance at the cutoff
        {
            double worst = 0.0;
            for (double fc : { 200.0, 500.0, 1000.0, 2000.0, 5000.0 })
            {
                double b, kc; const double osr = 2 * sr;
                DiodeTable::get().lookup(kTwoPi * fc / osr, b, kc);
                const std::complex<double> h = DiodeTable::openLoop(b, kTwoPi * fc / osr);
                double ph = std::arg(h); if (ph > 0) ph -= kTwoPi;
                worst = std::max(worst, std::fabs(ph + kPi));
            }
            CHECK(worst < 0.01, "diode table: the loop turns half a cycle at the cutoff (worst %.4f rad)", worst);
            // full resonance, no input: it rings (from the analog-noise whisper) close to the cutoff
            Engine e; Settings s = plain(kAcid); s.res = 1.0; s.cutoff = 800; s.wave = 0; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { 0, 0x90, 24, 100 } }, Transport(), sr);
            // the resonance shows as a peak in the spectrum near 800 Hz: compare the band around it with the band an octave up
            auto band = [&](double f0) { double re = 0, im = 0; for (int i = n / 2; i < n; ++i) { const double w = kTwoPi * f0 * i / sr; re += L[(size_t)i] * std::cos(w); im += L[(size_t)i] * std::sin(w); } return std::hypot(re, im); };
            double best = 0, bestF = 0; for (double f = 300; f < 2500; f += 10) { const double a = band(f); if (a > best) { best = a; bestF = f; } }
            bool finite = true; for (float v : L) finite = finite && std::isfinite(v);
            CHECK(finite && bestF > 600 && bestF < 1100, "full resonance peaks near the cutoff: %.0f Hz (cutoff 800)", bestF);
        }
        // ---- the sequencer on the song grid
        const int lat = [&] { Engine e; e.prepare(sr, 512); return e.latency(); }();
        {
            Engine e; Settings s = plain(kAcid); s.seqMode = kSeqSong; s.pattern = parsePattern("C . . . C . . . C . . . C . . ."); s.gateLen = 0.5;
            e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.playing = true; t.valid = true; t.bpm = 120.0; t.ppq = 0.0;
            std::vector<float> L((size_t)(2 * n));
            render(e, L, {}, t, sr);
            const double step = sr * 0.125;   // a 16th at 120 BPM
            long worst = 0; bool silentBetween = true;
            for (int k = 0; k < 4; ++k)
            {
                const size_t at = (size_t)std::lround(k * 4 * step);
                const long o = onsetAfter(L, at > 200 ? at - 200 : 0);
                worst = std::max(worst, std::labs(o - (long)at - lat));
                silentBetween = silentBetween && rms(L, at + (size_t)(2.2 * step), at + (size_t)(3.8 * step)) < 1e-3;
            }
            CHECK(worst < 40 && silentBetween, "SONG: notes on the grid (worst %ld samples off), silent on the rests", worst);
            // the host stops: the voice is released
            Transport stop = t; stop.playing = false; stop.ppq = 1.0;
            Engine e2; Settings s2 = s; s2.pattern = parsePattern("C - - - - - - - - - - - - - - -"); e2.setSettings(s2); e2.prepare(sr, 512);
            std::vector<float> A((size_t)n / 2), B((size_t)n / 2);
            render(e2, A, {}, t, sr);
            render(e2, B, {}, stop, sr);
            CHECK(rms(A, A.size() / 2, A.size()) > 0.01 && rms(B, B.size() / 2, B.size()) < 1e-4, "SONG stops with the host (%.1f dB playing, %.1f dB after)", db(rms(A, A.size() / 2, A.size())), db(rms(B, B.size() / 2, B.size())));
        }
        // ---- swing delays the second step of each pair
        {
            Engine e; Settings s = plain(kAcid); s.seqMode = kSeqSong; s.pattern = parsePattern(". C . . . . . . . . . . . . . ."); s.swing = 1.0 / 3.0;
            e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.playing = true; t.valid = true; t.bpm = 120.0; t.ppq = 0.0;
            std::vector<float> L((size_t)n);
            render(e, L, {}, t, sr);
            const double step = sr * 0.125;
            const long o = onsetAfter(L, 0);
            const double want = step * (1.0 + 1.0 / 3.0) + lat;
            CHECK(std::fabs(o - want) < 40, "SWING 33 %%: step 2 starts %.2f steps in (want 1.33)", (o - lat) / step);
        }
        // ---- TIE holds, SLIDE glides without a new attack
        {
            Engine e; Settings s = plain(kAcid); s.seqMode = kSeqSong; s.gateLen = 0.5; s.pattern = parsePattern("C - . . . . . . . . . . . . . .");
            e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.playing = true; t.valid = true; t.bpm = 120.0;
            std::vector<float> L((size_t)n);
            render(e, L, {}, t, sr);
            const double step = sr * 0.125;
            const double mid = rms(L, (size_t)(1.2 * step), (size_t)(1.4 * step)), after = rms(L, (size_t)(1.7 * step), (size_t)(1.9 * step));
            CHECK(mid > 0.01 && after < 0.02 * mid, "TIE: the note holds into step 2 (%.1f dB) and stops at its gate (%.1f dB)", db(mid), db(after));
        }
        {
            Engine e; Settings s = plain(kAcid); s.seqMode = kSeqSong; s.slide = 0.02; s.pattern = parsePattern("A~ a' . . . . . . . . . . . . . .");
            e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.playing = true; t.valid = true; t.bpm = 120.0;
            std::vector<float> L((size_t)n);
            render(e, L, {}, t, sr);
            const double step = sr * 0.125;
            const double f1 = freqOf(L, (size_t)(0.3 * step), (size_t)(0.95 * step), sr), f2 = freqOf(L, (size_t)(1.35 * step), (size_t)(1.5 * step), sr);
            const double edge = rms(L, (size_t)(0.97 * step), (size_t)(1.03 * step)), before = rms(L, (size_t)(0.8 * step), (size_t)(0.95 * step));
            CHECK(std::fabs(f1 - 110.0) < 1 && std::fabs(f2 - 220.0) < 1 && edge > 0.5 * before, "SLIDE: %.1f Hz glides to %.1f Hz with no gap (%.1f / %.1f dB)", f1, f2, db(edge), db(before));
        }
        // ---- KEYS: transposed by the key, silent without one
        {
            Engine e; Settings s = plain(kAcid); s.seqMode = kSeqKeys; s.pattern = parsePattern("A - - - - - - - - - - - - - - -");
            e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n);
            render(e, L, { { (uint32_t)(n / 4), 0x90, kKeyRoot + 12, 100 }, { (uint32_t)(3 * n / 4), 0x80, kKeyRoot + 12, 0 } }, Transport(), sr);
            const double f = freqOf(L, (size_t)(n / 4 + 4000), (size_t)(3 * n / 4), sr);
            CHECK(rms(L, 0, (size_t)(n / 4)) < 1e-4 && std::fabs(f - 220.0) < 1 && rms(L, (size_t)(3 * n / 4 + 3000), (size_t)n) < 1e-4,
                  "KEYS: silent until a key, an octave up plays A3 (%.1f Hz), stops with the key", f);
        }
        // ---- loudness of every model's default line
        {
            double lo = 0, hi = -200;
            for (int m = 0; m < kNumModels; ++m)
            {
                Engine e; Settings s; s.model = m; s.seqMode = kSeqFree; e.setSettings(s); e.prepare(sr, 512);
                Transport t; t.bpm = 125;
                std::vector<float> L((size_t)(4 * n));
                render(e, L, {}, t, sr);
                const double r = db(rms(L, (size_t)n, L.size()));
                lo = m == 0 ? r : std::min(lo, r); hi = std::max(hi, r);
            }
            CHECK(lo > -23 && hi < -17, "default lines between %.1f and %.1f dBFS RMS", lo, hi);
        }
        // ---- random settings and MIDI stay finite
        {
            std::mt19937 rg(7);
            std::uniform_real_distribution<double> u(0, 1);
            bool finite = true; double peak = 0;
            for (int trial = 0; trial < 60; ++trial)
            {
                Engine e; Settings s; s.model = trial % kNumModels; s.cutoff = 40 * std::pow(300.0, u(rg)); s.res = u(rg); s.envMod = u(rg); s.decay = 0.03 + 3 * u(rg);
                s.accent = u(rg); s.drive = u(rg); s.wave = u(rg); s.sub = u(rg); s.noise = u(rg); s.osc2 = u(rg); s.detune = 50 * u(rg); s.slide = 0.01 + 0.6 * u(rg);
                s.sustain = u(rg); s.attack = 0.0005 + u(rg); s.lfoPitch = u(rg); s.lfoCutoff = u(rg); s.lfoPw = u(rg); s.keyTrack = u(rg); s.slope = trial & 1;
                s.seqMode = (int)(u(rg) * kNumSeqModes); s.swing = 0.75 * u(rg); s.rate = (int)(u(rg) * kNumRates);
                fx::Rng r((uint32_t)trial + 1); randomizeSteps(s.pattern, kAllLanes, RandomSettings(), r);
                e.setSettings(s); e.prepare(sr, 512);
                std::vector<synth::Midi> ev;
                for (int k = 0; k < 20; ++k) ev.push_back({ (uint32_t)(u(rg) * n / 2), (uint8_t)(u(rg) < 0.6 ? 0x90 : 0x80), (uint8_t)(24 + u(rg) * 60), (uint8_t)(1 + u(rg) * 126) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                std::vector<float> L((size_t)n / 2);
                Transport t; t.playing = u(rg) < 0.5; t.valid = true; t.bpm = 60 + 140 * u(rg);
                render(e, L, ev, t, sr);
                for (float v : L) { finite = finite && std::isfinite(v); peak = std::max(peak, (double)std::fabs(v)); }
            }
            CHECK(finite && peak < 8.0, "random settings, patterns and MIDI: finite, peak %.2f", peak);
        }
    }
    // ---- the random generator's rules
    {
        printf("== random patterns\n");
        int bad = 0, firstNotRoot = 0, outOfScale = 0, accOnRest = 0, longAcc = 0, badSlide = 0, total = 0;
        double accents = 0, slides = 0, notes = 0;
        for (int sc = 0; sc < kNumScales; ++sc)
            for (int root = 0; root < 12; root += 5)
                for (int t = 0; t < 120; ++t)
                {
                    Pattern p; RandomSettings rs; rs.scale = sc; rs.root = root;
                    fx::Rng rng((uint32_t)(1 + t * 31 + sc * 977 + root * 13));
                    rs.density = 0.3 + 0.7 * rng.uni(); rs.accent = rng.uni(); rs.slide = rng.uni(); rs.octave = rng.uni();
                    p.length = 1 + (int)(rng.next() % kSteps);
                    randomizeSteps(p, kAllLanes, rs, rng);
                    ++total;
                    const std::vector<int> steps = scaleSteps(sc);
                    if (p.s[0].gate != kNote || pitchClassOf(p.s[0].note - root) != 0) ++firstNotRoot;
                    int run = 0;
                    for (int i = 0; i < p.length; ++i)
                    {
                        const Step& s = p.s[i];
                        if (s.note < kNoteLo || s.note > kNoteHi) ++bad;
                        if (s.gate == kNote)
                        {
                            ++notes;
                            bool in = false; for (int d : steps) in = in || pitchClassOf(s.note - root) == d;
                            if (!in) ++outOfScale;
                        }
                        if (s.accent && s.gate != kNote) ++accOnRest;
                        run = s.accent ? run + 1 : 0; if (run > 3) ++longAcc;
                        if (s.slide && (s.gate == kRest || p.s[(i + 1) % p.length].gate != kNote)) ++badSlide;
                        accents += s.accent; slides += s.slide;
                    }
                }
        CHECK(firstNotRoot == 0, "%d patterns: every one starts on the root", total);
        CHECK(bad == 0 && outOfScale == 0, "notes in range and in the scale (%d out of range, %d out of scale)", bad, outOfScale);
        CHECK(accOnRest == 0 && longAcc == 0, "accents only on notes, never four in a row");
        CHECK(badSlide == 0, "slides only from a sounding step into a note");
        CHECK(accents > 0.1 * notes && slides > 0.05 * notes, "accents %.0f and slides %.0f over %.0f notes", accents, slides, notes);
        // MUTATE keeps most of a pattern; one lane rolled leaves the others alone
        int changed = 0, otherLanes = 0;
        for (int t = 0; t < 200; ++t)
        {
            Pattern p; RandomSettings rs; fx::Rng rng((uint32_t)(t + 5));
            randomizeSteps(p, kAllLanes, rs, rng);
            Pattern q = p;
            mutateSteps(q, kLaneNotes, rs, rng);
            for (int i = 0; i < kSteps; ++i)
            {
                changed += q.s[i].note != p.s[i].note;
                otherLanes += q.s[i].gate != p.s[i].gate || q.s[i].accent != p.s[i].accent;
            }
        }
        CHECK(changed > 0 && changed < 200 * kSteps / 3 && otherLanes == 0, "MUTATE (notes): %.1f steps of 16 changed on average, gates and accents untouched", changed / 200.0);
        // presets parse
        const Pattern d = parsePattern(kDefaultPattern);
        CHECK(d.length == 16 && d.s[0].accent && d.s[2].note == 12 && d.s[2].slide && d.s[5].gate == kRest && d.s[9].gate == kTie && d.s[8].note == 0 && d.s[7].note == -5,
              "the pattern text reads: C! C c'~ C D#! . C~ G, C! - ...");
    }
    // ---- CPU
    {
        Engine e; Settings s; s.seqMode = kSeqFree; s.res = 0.9; s.drive = 0.5; e.setSettings(s); e.prepare(48000, 512);
        std::vector<float> L(48000 * 10), R(L.size());
        Transport t; t.bpm = 140;
        e.setTransport(t);
        const auto a = std::chrono::steady_clock::now();
        for (size_t o = 0; o < L.size(); o += 256) e.process(L.data() + o, R.data() + o, 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        printf("  CPU: %.2f %% of one core (10 s of the acid model)\n", secs * 10.0);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
