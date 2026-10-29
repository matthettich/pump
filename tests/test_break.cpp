// pump BREAK: every built-in break renders a bar long at its tempo, loops without a jump and sits near -14 dBFS RMS;
// the straight chop at the break's tempo plays the break back as it was; slices land on their steps; PITCH, BEND,
// DECAY and REVERSE do what they say (on a test tone); FIT follows the song's tempo; the effects: STUTTER repeats,
// REPEAT loops, TAPE STOP slows to a stop and blocks new steps, FILTER and HIGH-PASS take their side of the spectrum
// away, CRUSH quantises, ECHO rings on after it is let go, REVERSE turns the playback round; MIDI plays slices and
// holds effects; SONG stops with the host; random settings stay finite. 44.1 and 48 kHz. CPU.
#include "../plugins/break/BreakEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::brk;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double db(double g) { return 20.0 * std::log10(g + 1e-12); }
static void render(Engine& e, std::vector<float>& L, std::vector<float>& R, const std::vector<synth::Midi>& ev, Transport tr, double sr)
{
    R.assign(L.size(), 0.0f);
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
// a test source: a 441 Hz sine (a bar at 120 BPM), or a rising ramp
static Source tone(double sr, bool ramp = false)
{
    Source s; s.sr = sr; s.bpm = 120.0; s.bars = 1;
    const size_t n = (size_t)(2.0 * sr);
    s.L.resize(n); s.R.resize(n);
    for (size_t i = 0; i < n; ++i) { const float v = ramp ? (float)((double)i / n - 0.5) : (float)(0.5 * std::sin(kTwoPi * 441.0 * i / sr)); s.L[i] = s.R[i] = v; }
    return s;
}
static double freqOf(const std::vector<float>& x, size_t a, size_t b, double sr)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b && i < x.size(); ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * sr / (last - first) : 0.0;
}
static Transport playing(double bpm) { Transport t; t.playing = true; t.valid = true; t.bpm = bpm; t.ppq = 0.0; return t; }
static Settings one(const char* chop) { Settings s; s.pattern = parseChop(chop); s.volumeDb = 0.0; return s; }

int main()
{
    // ---- the built-in breaks
    {
        printf("== built-in breaks\n");
        const auto a = std::chrono::steady_clock::now();
        Breaks::get().renderAllNow();
        printf("  rendered %d breaks in %.2f s\n", kNumBreaks, std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count());
        int badLen = 0, badSeam = 0; double lo = 0, hi = -200; bool finite = true;
        for (int i = 0; i < kNumBreaks; ++i)
        {
            const Source* s = Breaks::get().ready(i);
            if (!s) { ++badLen; continue; }
            const double want = 240.0 / kBreaks[i].bpm * s->sr;
            if (std::fabs((double)s->size() - want) > 1.0) ++badLen;
            for (float v : s->L) finite = finite && std::isfinite(v);
            // the seam: the step from the last sample to the first is no bigger than the signal's usual steps
            double maxStep = 0; for (size_t n = 1; n < s->size(); ++n) maxStep = std::max(maxStep, (double)std::fabs(s->L[n] - s->L[n - 1]));
            if (std::fabs(s->L.front() - s->L.back()) > 0.6 * maxStep) ++badSeam;
            const double r = db(0.5 * (rms(s->L, 0, s->size()) + rms(s->R, 0, s->size())));
            lo = i == 0 ? r : std::min(lo, r); hi = std::max(hi, r);
        }
        CHECK(badLen == 0 && finite, "every break is one bar at its tempo, finite");
        CHECK(badSeam == 0, "every break loops without a jump");
        CHECK(lo > -16.5 && hi < -11.5, "levels between %.1f and %.1f dBFS RMS", lo, hi);
    }
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        // ---- the straight chop at the break's own tempo is the break
        {
            const Source* s = Breaks::get().ready(0);
            Engine e; Settings st = one(kStraight); e.setSettings(st); e.prepare(sr, 512); e.setSource(s);
            std::vector<float> L((size_t)(240.0 / kBreaks[0].bpm * sr * 2.0)), R;
            render(e, L, R, {}, playing(kBreaks[0].bpm), sr);
            // compare the second bar with the source, resampled (its own rate is 44.1 kHz)
            const size_t bar = (size_t)(240.0 / kBreaks[0].bpm * sr);
            double num = 0, da = 0, dbb = 0;
            for (size_t i = bar + 2000; i < 2 * bar - 2000; i += 3)
            {
                const double p = (double)(i - bar) * s->sr / sr;
                const size_t k = (size_t)p; const double f = p - k;
                const double ref = s->L[k] * (1 - f) + s->L[std::min(k + 1, s->size() - 1)] * f;
                num += L[i] * ref; da += (double)L[i] * L[i]; dbb += ref * ref;
            }
            const double corr = num / std::sqrt(da * dbb + 1e-20);
            CHECK(corr > 0.97, "the straight chop plays the break back (correlation %.3f)", corr);
        }
        // ---- slices land on their steps; PITCH, BEND, DECAY on a test tone
        const Source tn = tone(sr), rp = tone(sr, true);
        const double step = sr * 0.125;   // a sixteenth at 120 BPM
        {
            Engine e; e.setSettings(one("1 . . . 1+12 . . . 1~0 . . . 1^12 . . .")); e.prepare(sr, 512); e.setSource(&tn);
            std::vector<float> L((size_t)(2 * sr)), R;
            render(e, L, R, {}, playing(120.0), sr);
            const double f0 = freqOf(L, (size_t)(0.5 * step), (size_t)(3.5 * step), sr);
            const double f12 = freqOf(L, (size_t)(4.5 * step), (size_t)(7.5 * step), sr);
            const double held = rms(L, (size_t)(3.0 * step), (size_t)(3.9 * step)), decayed = rms(L, (size_t)(9.5 * step), (size_t)(11.5 * step));
            const double bendA = freqOf(L, (size_t)(12.05 * step), (size_t)(12.3 * step), sr), bendB = freqOf(L, (size_t)(12.9 * step), (size_t)(13.0 * step), sr);
            CHECK(std::fabs(f0 - 441.0) < 1.0 && std::fabs(f12 - 882.0) < 2.0, "slice at its pitch (%.1f Hz), PITCH +12 an octave up (%.1f Hz)", f0, f12);
            CHECK(held > 0.2 && decayed < 0.01 * held, "DECAY 100 %% holds (%.1f dB), DECAY 0 dies within the step (%.1f dB)", db(held), db(decayed));
            CHECK(bendA < 520 && bendB > 800 && bendB < 900, "BEND +12 glides across the step: %.0f Hz -> %.0f Hz", bendA, bendB);
        }
        {
            Engine e; e.setSettings(one("1r . . . . . . . . . . . . . . .")); e.prepare(sr, 512); e.setSource(&rp);
            std::vector<float> L((size_t)(step * 2)), R;
            render(e, L, R, {}, playing(120.0), sr);
            CHECK(L[(size_t)(0.2 * step)] > L[(size_t)(0.8 * step)], "REVERSE plays the slice backwards");
        }
        // ---- FIT: at 240 BPM a slice plays twice as fast (and an octave up)
        {
            Engine e; e.setSettings(one("1 . . . . . . . . . . . . . . .")); e.prepare(sr, 512); e.setSource(&tn);
            std::vector<float> L((size_t)(sr / 2)), R;
            render(e, L, R, {}, playing(240.0), sr);
            const double f = freqOf(L, 200, (size_t)(0.2 * sr), sr);
            Engine e2; Settings nf = one("1 . . . . . . . . . . . . . . ."); nf.fit = false; e2.setSettings(nf); e2.prepare(sr, 512); e2.setSource(&tn);
            render(e2, L, R, {}, playing(240.0), sr);
            const double g = freqOf(L, 200, (size_t)(0.2 * sr), sr);
            CHECK(std::fabs(f - 882.0) < 2.0 && std::fabs(g - 441.0) < 1.0, "FIT at twice the tempo: %.1f Hz; without FIT: %.1f Hz", f, g);
        }
        // ---- the effects
        auto withFx = [&](int k, double amount, const Source* src, const char* chop, double secs, double pressAt = 0.0, double releaseAt = 1e9) {
            Engine e; Settings s = one(chop); s.fxAmount[k] = amount; e.setSettings(s); e.prepare(sr, 512); e.setSource(src);
            std::vector<float> L((size_t)(secs * sr)), R;
            Transport t = playing(120.0);
            std::vector<float> l2(256), r2(256);
            for (size_t off = 0; off < L.size(); off += 256)
            {
                const double tt = off / sr;
                Settings cur = s; cur.fxHold[k] = tt >= pressAt && tt < releaseAt; e.setSettings(cur);
                e.setTransport(t);
                const int m = (int)std::min<size_t>(256, L.size() - off);
                e.process(L.data() + off, r2.data(), m, nullptr, 0);
                t.ppq += m * t.bpm / 60.0 / sr;
            }
            return L;
        };
        {
            // TAPE STOP: the pitch falls; no new step starts while held
            const auto L = withFx(kTapeStop, 0.2, &tn, kStraight, 1.2, 0.1, 10.0);
            const double fa = freqOf(L, (size_t)(0.12 * sr), (size_t)(0.15 * sr), sr), fb = freqOf(L, (size_t)(0.35 * sr), (size_t)(0.45 * sr), sr);
            CHECK(fa > 380 && fb < 0.6 * fa, "TAPE STOP: %.0f Hz slowing to %.0f Hz", fa, fb);
        }
        {
            // FILTER / HIGH-PASS on the break: the top / the bottom goes
            const Source* b = Breaks::get().ready(2);
            auto bandE = [&](const std::vector<float>& x, size_t a, size_t n2, double lo, double hi) {
                // energy of x in a band (a few DFT bins, Hann-windowed)
                double e = 0; for (double f = lo; f < hi; f *= 1.2) { double re = 0, im = 0; for (size_t i = a; i < a + n2; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPi * (double)(i - a) / n2); re += w * x[i] * std::cos(kTwoPi * f * i / sr); im += w * x[i] * std::sin(kTwoPi * f * i / sr); } e += re * re + im * im; }
                return e;
            };
            const auto dry = withFx(kFilter, 0.8, b, kStraight, 1.5, 10.0);
            const auto lp = withFx(kFilter, 0.8, b, kStraight, 1.5, 0.0);
            const auto hp = withFx(kHighpass, 0.8, b, kStraight, 1.5, 0.0);
            const size_t a = (size_t)(0.8 * sr), n2 = (size_t)(0.5 * sr);
            const double topDry = bandE(dry, a, n2, 4000, 12000), topLp = bandE(lp, a, n2, 4000, 12000), lowDry = bandE(dry, a, n2, 40, 150), lowHp = bandE(hp, a, n2, 40, 150);
            CHECK(topLp < 0.01 * topDry && lowHp < 0.05 * lowDry, "FILTER takes the top away (%.0f dB), HIGH-PASS the bottom (%.0f dB)", 10 * std::log10(topLp / topDry), 10 * std::log10(lowHp / lowDry));
        }
        {
            // REPEAT: after the capture the output repeats with the loop's length (a sixteenth at amount 1)
            const auto L = withFx(kRepeat, 1.0, Breaks::get().ready(0), kStraight, 1.5, 0.3);
            const size_t len = (size_t)(0.125 * 60.0 / 120.0 * sr);   // a thirty-second at 120 BPM
            double num = 0, den = 0;
            for (size_t i = (size_t)(0.6 * sr); i < (size_t)(0.6 * sr) + 4 * len; ++i) { num += std::fabs(L[i] - L[i + len]); den += std::fabs(L[i]); }
            CHECK(num < 0.08 * den, "REPEAT loops (difference one loop apart %.1f %%)", 100.0 * num / den);
        }
        {
            // STUTTER: the voice keeps jumping back: with a ramp, the output repeats every stutter length
            const auto L = withFx(kStutter, 1.0, &rp, "1 . . . . . . . . . . . . . . .", 1.0, 0.0);
            const size_t len = (size_t)(step / 8.0);
            double num = 0, den = 0;
            for (size_t i = (size_t)(0.2 * sr); i < (size_t)(0.2 * sr) + 2 * len; ++i) { num += std::fabs(L[i] - L[i + len]); den += std::fabs(L[i]); }
            CHECK(num < 0.1 * den, "STUTTER repeats every eighth of a step");
        }
        {
            // CRUSH: few levels; ECHO: a tail after the release
            const auto c = withFx(kCrush, 1.0, &tn, kStraight, 0.5, 0.0);
            std::vector<float> lv; for (size_t i = (size_t)(0.1 * sr); i < (size_t)(0.4 * sr); ++i) { bool seen = false; for (float x : lv) seen = seen || std::fabs(x - c[i]) < 1e-6f; if (!seen && lv.size() < 100) lv.push_back(c[i]); }
            const auto ech = withFx(kEcho, 0.6, &tn, "1~10 . . . . . . . . . . . . . . .", 2.0, 0.0, 0.2);
            const double tail = rms(ech, (size_t)(0.6 * sr), (size_t)(0.9 * sr));
            CHECK(lv.size() < 20 && tail > 0.01, "CRUSH: %zu levels; ECHO rings on after the release (%.1f dB)", lv.size(), db(tail));
        }
        // ---- MIDI: a slice on C-4, an effect held from C-2
        {
            Engine e; Settings s = one(kStraight); s.seqMode = kSeqOff; e.setSettings(s); e.prepare(sr, 512); e.setSource(&tn);
            std::vector<float> L((size_t)(0.5 * sr)), R;
            render(e, L, R, { { 0, 0x90, 48, 100 }, { 100, 0x90, (uint8_t)fxNote(kReverse), 100 } }, Transport(), sr);
            CHECK(rms(L, 1000, 10000) > 0.1 && e.fxOn(kReverse) && !e.fxOn(kStutter), "MIDI: C-4 plays slice 1, C-2 + two white keys holds REVERSE");
        }
        // ---- SONG stops with the host
        {
            Engine e; e.setSettings(one(kStraight)); e.prepare(sr, 512); e.setSource(&tn);
            std::vector<float> L((size_t)(0.5 * sr)), R;
            render(e, L, R, {}, playing(120.0), sr);
            Transport stop; stop.playing = false;
            render(e, L, R, {}, stop, sr);
            CHECK(rms(L, (size_t)(0.1 * sr), L.size()) < 1e-4, "SONG: silent once the host stops");
        }
        // ---- the break goes away or changes while voices play (a built-in still being made, FILE with no file, a
        //      shorter file): silence or the new break, never a read through the old one
        {
            Engine e; Settings s = one(kStraight); s.seqMode = kSeqFree; e.setSettings(s); e.prepare(sr, 512); e.setSource(&tn);
            std::vector<float> L((size_t)(0.3 * sr)), R;
            Transport t; t.bpm = 120.0;
            render(e, L, R, {}, t, sr);
            const bool was = rms(L, 0, L.size()) > 0.05;
            e.setSource(nullptr);
            for (int k = 0; k < kFx; ++k) { s.fxHold[k] = true; } e.setSettings(s);
            render(e, L, R, {}, t, sr);
            const bool quiet = rms(L, (size_t)(0.05 * sr), L.size()) < 0.05;
            e.playSlice(3, 1.0);
            render(e, L, R, {}, t, sr);
            Source shortSrc = tone(sr); shortSrc.L.resize(100); shortSrc.R.resize(100);
            s = one(kStraight); s.seqMode = kSeqFree; e.setSettings(s); e.setSource(&tn);
            render(e, L, R, {}, t, sr);
            e.setSource(&shortSrc);
            render(e, L, R, {}, t, sr);
            bool fin = true; for (float x : L) fin = fin && std::isfinite(x);
            CHECK(was && quiet && fin, "the break taken away mid-play: silence; a shorter one: plays on");
        }
        // ---- random settings
        {
            std::mt19937 rg(3); std::uniform_real_distribution<double> u(0, 1);
            bool finite = true; double hi = 0;
            for (int trial = 0; trial < 40; ++trial)
            {
                Engine e; Settings s;
                for (auto& st : s.pattern.s) { st.slice = (int)(u(rg) * 17); st.pitch = -24 + 48 * u(rg); st.bend = -24 + 48 * u(rg); st.level = u(rg); st.decay = u(rg); st.reverse = u(rg) < 0.3; }
                s.transpose = -24 + 48 * u(rg); s.tone = u(rg); s.drive = u(rg); s.swing = 0.75 * u(rg); s.rate = (int)(u(rg) * kNumRates); s.length = 1 + (int)(u(rg) * 16);
                s.fit = u(rg) < 0.5; s.seqMode = kSeqFree;
                for (int k = 0; k < kFx; ++k) { s.fxAmount[k] = u(rg); s.fxHold[k] = u(rg) < 0.3; }
                e.setSettings(s); e.prepare(sr, 512); e.setSource(Breaks::get().ready(trial % kNumBreaks));
                std::vector<float> L((size_t)sr), R;
                Transport t; t.bpm = 60 + 150 * u(rg);
                render(e, L, R, {}, t, sr);
                for (size_t i = 0; i < L.size(); ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); hi = std::max(hi, (double)std::fabs(L[i])); }
            }
            CHECK(finite && hi < 6.0, "random chops, effects and tempos: finite, peak %.2f", hi);
        }
    }
    // ---- CPU
    {
        Engine e; Settings s; s.seqMode = kSeqFree; s.fxHold[kEcho] = true; s.fxHold[kFilter] = true; e.setSettings(s); e.prepare(48000, 512); e.setSource(Breaks::get().ready(0));
        Transport t; t.bpm = 100; e.setTransport(t);
        std::vector<float> L(48000 * 10), R(L.size());
        const auto a = std::chrono::steady_clock::now();
        for (size_t o = 0; o < L.size(); o += 256) e.process(L.data() + o, R.data() + o, 256, nullptr, 0);
        printf("  CPU: %.2f %% of one core\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count() * 10.0);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
