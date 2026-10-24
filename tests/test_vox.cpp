// pump VOX: CLASSIC is the first VOX sample for sample (a reference copy of its DSP below, without its output soft
// clip) with Split L/R routing and the new controls at zero; silence in the voice closes every mode; the four modes
// land within a few dB of each other on the same voice and carrier; FORMANT moves the output's spectral centroid the
// right way in every mode; Spectral / Convolve report the FFT size as latency and the dry voice lines up with it;
// the talkbox puts the voice's formants on the carrier; the pitch tracker finds sung pitches within 10 cents;
// Follow plays the voice's note snapped to the scale; MIDI plays the synth; FREEZE keeps singing; mode switches are
// click-free; the sidechain routes swap voice and carrier; unvoiced noise lets consonants through between notes;
// random settings, routes and block sizes stay finite and bounded. 44.1, 48 and 96 kHz. Prints CPU per mode.
#include "../plugins/vox/VoxEngine.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

using namespace pump;
using namespace pump::vox;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

// ---- the first VOX's DSP, as it shipped (minus the soft clip at its output) ---------------------------------------
namespace orig {
enum { P_BANDS, P_LOW, P_HIGH, P_WIDTH, P_ATTACK, P_RELEASE, P_FORMANT, P_SIBIL, P_SPREAD, P_DRY, P_GAIN, P_COUNT };
static const int band_table[5] = { 8, 12, 16, 24, 32 };
struct coef { float b0, b1, b2, a1, a2; };
struct state { float z1, z2; };
struct vocoder {
    float sr; float p[P_COUNT]; int nbands;
    coef mc[32], cc[32]; state ms[32][2], cs[32][2]; float env[32], gl[32], gr[32];
    coef hp; state hps[2]; float att, rel, makeup, sib, dry, gain;
};
static float value(const vocoder* v, int i)
{
    const float x = v->p[i];
    switch (i)
    {
    case P_BANDS: return (float)band_table[(int)(x * 4.999f)];
    case P_LOW: return 50.f * powf(10.f, x);
    case P_HIGH: return 2000.f * powf(6.f, x);
    case P_WIDTH: return 0.5f * powf(4.f, x);
    case P_ATTACK: return 0.5f * powf(100.f, x);
    case P_RELEASE: return 10.f * powf(50.f, x);
    case P_FORMANT: return 0.5f * powf(4.f, x);
    case P_GAIN: return -24.f + 48.f * x;
    default: return x;
    }
}
static void bpf(coef* c, float f, float q, float sr)
{
    float nyq = 0.45f * sr; if (f > nyq) f = nyq; if (f < 20.f) f = 20.f;
    float w = 2.f * (float)M_PI * f / sr, cw = cosf(w), sw = sinf(w), alpha = sw / (2.f * q), a0 = 1.f + alpha;
    c->b0 = alpha / a0; c->b1 = 0.f; c->b2 = -alpha / a0; c->a1 = -2.f * cw / a0; c->a2 = (1.f - alpha) / a0;
}
static void hpf(coef* c, float f, float q, float sr)
{
    float w = 2.f * (float)M_PI * f / sr, cw = cosf(w), sw = sinf(w), alpha = sw / (2.f * q), a0 = 1.f + alpha;
    c->b0 = (1.f + cw) * 0.5f / a0; c->b1 = -(1.f + cw) / a0; c->b2 = (1.f + cw) * 0.5f / a0; c->a1 = -2.f * cw / a0; c->a2 = (1.f - alpha) / a0;
}
static inline float bq(const coef* c, state* s, float x) { float y = c->b0 * x + s->z1; s->z1 = c->b1 * x - c->a1 * y + s->z2; s->z2 = c->b2 * x - c->a2 * y; return y; }
static void init(vocoder* v, float sr, const float* p)
{
    memset(v, 0, sizeof *v); v->sr = sr; memcpy(v->p, p, sizeof v->p);
    int n = (int)value(v, P_BANDS); float lo = value(v, P_LOW), hi = value(v, P_HIGH); if (hi < lo * 2.f) hi = lo * 2.f;
    float ratio = powf(hi / lo, 1.f / (float)(n - 1));
    float q = (sqrtf(ratio) / (ratio - 1.f)) * 0.644f * value(v, P_WIDTH);
    float shift = value(v, P_FORMANT), spread = value(v, P_SPREAD);
    for (int b = 0; b < n; b++)
    {
        float f = lo * powf(ratio, (float)b);
        bpf(&v->mc[b], f, q, sr); bpf(&v->cc[b], f * shift, q, sr);
        float pan = (b & 1 ? 1.f : -1.f) * spread; v->gl[b] = sqrtf(1.f - pan); v->gr[b] = sqrtf(1.f + pan);
    }
    v->nbands = n;
    v->att = expf(-1.f / (value(v, P_ATTACK) * 0.001f * sr));
    v->rel = expf(-1.f / (value(v, P_RELEASE) * 0.001f * sr));
    v->makeup = 18.0f * sqrtf(16.f / (float)n);
    v->sib = value(v, P_SIBIL); v->dry = value(v, P_DRY); v->gain = powf(10.f, value(v, P_GAIN) / 20.f);
    hpf(&v->hp, fminf(5000.f, 0.4f * sr), 0.707f, sr);
}
static void process(vocoder* v, const float* car, const float* mod, float* L, float* R, int frames)
{
    for (int s = 0; s < frames; s++)
    {
        const float c = car[s], m = mod[s]; float l = 0.f, r = 0.f;
        for (int b = 0; b < v->nbands; b++)
        {
            float mb = bq(&v->mc[b], &v->ms[b][0], m); mb = bq(&v->mc[b], &v->ms[b][1], mb);
            float a = fabsf(mb), e = v->env[b]; e = a + (a > e ? v->att : v->rel) * (e - a); v->env[b] = e;
            float cb = bq(&v->cc[b], &v->cs[b][0], c); cb = bq(&v->cc[b], &v->cs[b][1], cb);
            float y = cb * e; l += y * v->gl[b]; r += y * v->gr[b];
        }
        float h = bq(&v->hp, &v->hps[0], m); h = bq(&v->hp, &v->hps[1], h) * v->sib;
        L[s] = (l * v->makeup + h + c * v->dry) * v->gain;
        R[s] = (r * v->makeup + h + c * v->dry) * v->gain;
    }
}
} // namespace orig

// ---- test signals ---------------------------------------------------------------------------------------------------------
// carrier: a saw chord (A2 / E3 / A3), about -11 dBFS RMS
static std::vector<float> sawChord(double sr, size_t n)
{
    std::vector<float> c(n);
    const double fr[3] = { 110.0, 164.81, 220.0 };
    double ph[3] = {};
    for (size_t i = 0; i < n; ++i)
    {
        double s = 0;
        for (int k = 0; k < 3; ++k) { ph[k] += fr[k] / sr; if (ph[k] >= 1) ph[k] -= 1; s += 2 * ph[k] - 1; }
        c[i] = (float)(s * 0.3);
    }
    return c;
}
// a voice-like signal: a glottal pulse train at f0 through three vowel formants (changing every 250 ms), a fricative
// burst and a gap of silence every second; about -16 dBFS while voiced
struct Formants { double f[3]; };
static const Formants kVowels[5] = { { { 730, 1090, 2440 } }, { { 530, 1840, 2480 } }, { { 270, 2290, 3010 } }, { { 570, 840, 2410 } }, { { 300, 870, 2240 } } };
static std::vector<float> speech(double sr, size_t n, double f0 = 120.0, int vowel = -1, bool gaps = true)
{
    struct Res { double b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; void set(double f, double bw, double sr_) { double R = std::exp(-kPi * bw / sr_), th = kTwoPi * f / sr_; a1 = -2 * R * std::cos(th); a2 = R * R; b0 = 1 - R; }
                 double run(double x) { double y = b0 * x - a1 * z1 - a2 * z2; z2 = z1; z1 = y; return y; } } rs[3];
    std::vector<float> m(n);
    double gph = 0;
    fx::Rng rng(7);
    const size_t seg = (size_t)(sr / 4);
    for (size_t i = 0; i < n; ++i)
    {
        const int sgi = (int)(i / seg);
        const double t = std::fmod((double)i / sr, 1.0);
        if (i % seg == 0) { const int vw = vowel >= 0 ? vowel : sgi % 5; for (int k = 0; k < 3; ++k) rs[k].set(kVowels[vw].f[k], 80 + 40 * k, sr); }
        gph += f0 / sr;
        double pulse = 0; if (gph >= 1) { gph -= 1; pulse = 1; }
        double v = 0; for (int k = 0; k < 3; ++k) v += rs[k].run(pulse) * (k == 0 ? 1.0 : 0.5);
        const double voiced = !gaps || t < 0.7 ? 1.0 : 0.0;
        const double fric = gaps && t > 0.7 && t < 0.8 ? 0.15 : 0.0;
        m[i] = (float)(v * 4.0 * voiced + rng.bi() * fric);
    }
    double e = 0; for (float x : m) e += (double)x * x;
    const double rms = std::sqrt(e / n);
    for (float& x : m) x = (float)(x * 0.15 / rms);
    return m;
}
static double rmsDb(const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min(to, x.size());
    double e = 0; for (size_t i = from; i < to; ++i) e += (double)x[i] * x[i];
    return 10 * std::log10(e / std::max<size_t>(1, to - from) + 1e-30);
}
// spectral centroid (Hz) of x[from, from + N)
static double centroid(const std::vector<float>& x, size_t from, double sr, int N = 8192)
{
    RealFFT f(N); AlignedBuffer a((size_t)N), s((size_t)N);
    double num = 0, den = 0;
    for (size_t off = from; off + (size_t)N <= x.size(); off += (size_t)N)
    {
        for (int i = 0; i < N; ++i) a[(size_t)i] = x[off + (size_t)i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        f.forward(a.data(), s.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const double p = (double)s[(size_t)(2 * k)] * s[(size_t)(2 * k)] + (double)s[(size_t)(2 * k + 1)] * s[(size_t)(2 * k + 1)];
            const double fr = k * sr / N;
            if (fr < 80 || fr > 12000) continue;
            num += fr * p; den += p;
        }
    }
    return num / std::max(den, 1e-30);
}
// energy in a band (relative, dB) of x[from, from + N)
static double bandDb(const std::vector<float>& x, size_t from, double sr, double lo, double hi, int N = 16384)
{
    RealFFT f(N); AlignedBuffer a((size_t)N), s((size_t)N);
    double e = 0;
    for (size_t off = from; off + (size_t)N <= x.size(); off += (size_t)N)
    {
        for (int i = 0; i < N; ++i) a[(size_t)i] = x[off + (size_t)i] * (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        f.forward(a.data(), s.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const double fr = k * sr / N;
            if (fr < lo || fr > hi) continue;
            e += (double)s[(size_t)(2 * k)] * s[(size_t)(2 * k)] + (double)s[(size_t)(2 * k + 1)] * s[(size_t)(2 * k + 1)];
        }
    }
    return 10 * std::log10(e + 1e-30);
}

// run with Split L/R (carrier left, voice right)
static void runSplit(Engine& e, const std::vector<float>& car, const std::vector<float>& mod, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    const size_t n = car.size();
    L.assign(n, 0); R.assign(n, 0);
    for (size_t off = 0; off < n; off += (size_t)block)
    {
        const int k = (int)std::min<size_t>((size_t)block, n - off);
        e.process(car.data() + off, mod.data() + off, nullptr, nullptr, L.data() + off, R.data() + off, k);
    }
}
static Settings plainSplit()
{
    Settings s;
    s.route = kRouteSplit; s.flatten = 0; s.emphasis = 0; s.noise = 0; s.gateDb = kGateOff; s.spread = 0; s.sibilance = 30;
    return s;
}

static void testClassicIsOriginal(double sr)
{
    printf("Classic = the first VOX (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 3);
    const auto car = sawChord(sr, n), mod = speech(sr, n);
    struct Case { float p[orig::P_COUNT]; } cases[] = {
        { { 0.5f, 0.301f, 0.774f, 0.5f, 0.5f, 0.411f, 0.5f, 0.3f, 0.0f, 0.0f, 0.5f } },     // its defaults: 16 bands, 100 Hz..8 kHz
        { { 1.0f, 0.1f, 0.5f, 0.8f, 0.2f, 0.7f, 0.75f, 0.0f, 0.6f, 0.25f, 0.4f } },       // 32 bands, formant up, spread, dry
        { { 0.0f, 0.6f, 1.0f, 0.2f, 0.8f, 0.1f, 0.25f, 1.0f, 1.0f, 0.0f, 0.45f } },        // 8 bands, formant down, full sibilance
    };
    for (const Case& c : cases)
    {
        orig::vocoder* ov = new orig::vocoder;
        orig::init(ov, (float)sr, c.p);
        std::vector<float> rl(n), rr(n);
        orig::process(ov, car.data(), mod.data(), rl.data(), rr.data(), (int)n);
        Settings s = plainSplit();
        const int nb = (int)orig::value(ov, orig::P_BANDS);
        for (int i = 0; i < kNumBandChoices; ++i) if (kBandCounts[i] == nb) s.bandsIndex = i;
        s.lowHz = orig::value(ov, orig::P_LOW); s.highHz = orig::value(ov, orig::P_HIGH); s.width = orig::value(ov, orig::P_WIDTH);
        s.attackMs = orig::value(ov, orig::P_ATTACK); s.releaseMs = orig::value(ov, orig::P_RELEASE);
        s.formant = 12.0 * std::log2(orig::value(ov, orig::P_FORMANT));
        s.sibilance = 100.0 * orig::value(ov, orig::P_SIBIL); s.spread = 100.0 * orig::value(ov, orig::P_SPREAD);
        s.carrierDry = 100.0 * orig::value(ov, orig::P_DRY); s.outDb = orig::value(ov, orig::P_GAIN);
        delete ov;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R;
        runSplit(e, car, mod, L, R, 97);
        double err = 0, ref = 0;
        for (size_t i = 0; i < n; ++i) { err += (double)(L[i] - rl[i]) * (L[i] - rl[i]) + (double)(R[i] - rr[i]) * (R[i] - rr[i]); ref += (double)rl[i] * rl[i] + (double)rr[i] * rr[i]; }
        const double db = 10 * std::log10(err / ref + 1e-30);
        CHECK(db < -70.0, "%d bands: difference to the original %.1f dB", nb, db);
    }
}

static Settings modeSettings(int mode)
{
    Settings s = plainSplit();
    s.mode = mode; s.sibilance = 0; s.bandsIndex = 2; s.releaseMs = 30; s.fftIndex = 1; s.detail = 50; s.tube = 0; s.driveDb = 0;
    return s;
}

static void testModes(double sr)
{
    printf("Modes (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 4);
    const auto car = sawChord(sr, n), mod = speech(sr, n);
    double lv[kNumModes] = {};
    for (int m = 0; m < kNumModes; ++m)
    {
        Engine e; e.setSettings(modeSettings(m)); e.prepare(sr, 256);
        std::vector<float> L, R;
        runSplit(e, car, mod, L, R);
        bool finite = true; double peak = 0;
        for (size_t i = 0; i < n; ++i) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) finite = false; peak = std::max(peak, (double)std::fabs(L[i])); }
        const int lat = e.latency();
        // talking (in each second: 0.05..0.65) vs the silent gap (0.93..0.995 into the second, after the release)
        double talk = 0, gap = 0;
        for (int s = 1; s < 4; ++s)
        {
            talk += rmsDb(L, (size_t)((s + 0.05) * sr) + lat, (size_t)((s + 0.65) * sr) + lat) / 3;
            gap += rmsDb(L, (size_t)((s + 0.93) * sr) + lat, (size_t)((s + 0.995) * sr) + lat) / 3;
        }
        lv[m] = talk;
        CHECK(finite && peak < 3.0, "%s: finite, peak %.2f", kModeNames[m], peak);
        CHECK(talk - gap > 30.0, "%s: silence closes the output (talk %.1f dB, gap %.1f dB)", kModeNames[m], talk, gap);
        const int wantLat = (m == kSpectral || m == kConvolve) ? (sr > 66000 ? 2048 : 1024) : 0;
        CHECK(lat == wantLat, "%s: latency %d (want %d)", kModeNames[m], lat, wantLat);
    }
    for (int m = 1; m < kNumModes; ++m)
        CHECK(std::fabs(lv[m] - lv[0]) < 3.0, "%s level %.1f dB vs Classic %.1f dB", kModeNames[m], lv[m], lv[0]);
}

static void testFormant(double sr)
{
    printf("Formant shift (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 3);
    // a bright carrier (saw chord) and a steady vowel /a/ without gaps
    const auto car = sawChord(sr, n), mod = speech(sr, n, 120.0, 0, false);
    for (int m = 0; m < kNumModes; ++m)
    {
        double c[3];
        const double sh[3] = { -7.0, 0.0, 7.0 };
        for (int k = 0; k < 3; ++k)
        {
            Settings s = modeSettings(m); s.formant = sh[k]; s.flatten = 60;
            Engine e; e.setSettings(s); e.prepare(sr, 256);
            std::vector<float> L, R; runSplit(e, car, mod, L, R);
            c[k] = centroid(L, (size_t)sr, sr);
        }
        CHECK(c[0] < c[1] * 0.93 && c[2] > c[1] * 1.07, "%s: centroid %.0f / %.0f / %.0f Hz at -7 / 0 / +7 st", kModeNames[m], c[0], c[1], c[2]);
    }
}

static void testTalkboxFormants(double sr)
{
    printf("Talkbox formants (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 3);
    const auto car = sawChord(sr, n);
    // /i/ (270, 2290) against /a/ (730, 1090): the energy near 2.3 kHz vs near 1.1 kHz flips between them
    double diff[2];
    for (int v = 0; v < 2; ++v)
    {
        const auto mod = speech(sr, n, 120.0, v == 0 ? 2 : 0, false);
        Settings s = modeSettings(kTalkbox); s.flatten = 80;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; runSplit(e, car, mod, L, R);
        diff[v] = bandDb(L, (size_t)sr, sr, 2000, 2600) - bandDb(L, (size_t)sr, sr, 950, 1250);
    }
    CHECK(diff[0] - diff[1] > 12.0, "2.3 kHz vs 1.1 kHz: /i/ %+.1f dB, /a/ %+.1f dB", diff[0], diff[1]);
}

static void testPitchTracker(double sr)
{
    printf("Pitch tracker (%.0f Hz)\n", sr);
    double worst = 0;
    int unvoicedMiss = 0, total = 0;
    for (double f0 : { 82.4, 110.0, 146.8, 196.0, 261.6, 392.0, 523.3, 784.0 })
    {
        const size_t n = (size_t)(sr * 0.6);
        const auto x = speech(sr, n, f0, 1, false);
        pitch::Tracker t; t.prepare(sr);
        for (size_t i = 0; i < n; ++i)
            if (t.push(x[i]) && i > (size_t)(0.15 * sr))
            {
                ++total;
                if (!t.result().voiced) { ++unvoicedMiss; continue; }
                worst = std::max(worst, std::fabs(1200.0 * std::log2(t.result().hz / f0)));
            }
    }
    CHECK(worst < 10.0 && unvoicedMiss <= total / 50, "82..784 Hz: worst error %.2f cents, %d of %d frames missed", worst, unvoicedMiss, total);
    // noise is unvoiced
    pitch::Tracker t; t.prepare(sr);
    fx::Rng r(3); int voiced = 0, frames = 0;
    for (int i = 0; i < (int)sr; ++i) if (t.push((float)(0.1 * r.bi()))) { ++frames; voiced += t.result().voiced; }
    CHECK(voiced <= frames / 20, "white noise: %d of %d frames voiced", voiced, frames);
}

static void testFollowAndMidi(double sr)
{
    printf("Synth carrier (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 1.5);
    auto run = [&](Settings s, const std::vector<float>& voice, std::vector<float>& L, const MidiMsg* ev = nullptr, int nev = 0) {
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> R(n); L.assign(n, 0);
        for (size_t off = 0; off < n; off += 256)
        {
            const int k = (int)std::min<size_t>(256, n - off);
            e.process(voice.data() + off, voice.data() + off, nullptr, nullptr, L.data() + off, R.data() + off, k, off == 0 ? ev : nullptr, off == 0 ? nev : 0);
        }
        return e.followMidi();
    };
    std::vector<float> L;
    // a voice on F#3 (185 Hz): Exact follows it; C major snaps to F3 or G3; chromatic to F#3
    const auto voice = speech(sr, n, 185.0, 1, false);
    Settings s; s.route = kRouteSynth; s.source = kSourceFollow;
    s.scale = 0; double m = run(s, voice, L);
    CHECK(std::fabs(m - 54.0) < 0.1, "Exact: follows the voice at %.2f (F#3 = 54)", m);
    s.scale = 1; m = run(s, voice, L);
    CHECK(m == 54.0, "Chromatic: %.2f", m);
    s.scale = 2; s.key = 0; m = run(s, voice, L);
    CHECK(m == 53.0 || m == 55.0, "C major: %.2f (F3 or G3)", m);
    // the dry carrier's pitch: Follow on a voice at 220 Hz with octave -1 plays 110 Hz
    {
        Settings f; f.route = kRouteSynth; f.source = kSourceFollow; f.scale = 1; f.octave = -1; f.wave = kWaveSaw; f.detune = 0; f.carrierDry = 100; f.mode = kClassic;
        f.sibilance = 0; f.outDb = 0;
        const auto v220 = speech(sr, n, 220.0, 1, false);
        Engine e; Settings g = f; e.setSettings(g); e.prepare(sr, 256);
        std::vector<float> l(n), r(n);
        e.process(v220.data(), v220.data(), nullptr, nullptr, l.data(), r.data(), (int)n);
        CHECK(std::fabs(e.followMidi() - 57.0) < 0.01, "voice A3 -> follow note %.2f", e.followMidi());
    }
    // MIDI: note 60 held, Auto plays it (not the voice); its fundamental from the dry carrier
    {
        Settings f; f.route = kRouteSynth; f.source = kSourceAuto; f.wave = kWaveSaw; f.detune = 0; f.carrierDry = 100; f.sibilance = 0;
        f.outDb = 0;
        const MidiMsg ev[1] = { { 0, 0x90, 60, 100 } };
        Engine e; e.setSettings(f); e.prepare(sr, 256);
        std::vector<float> silent(n, 0.0f), l(n), r(n);
        e.process(silent.data(), silent.data(), nullptr, nullptr, l.data(), r.data(), (int)n, ev, 1);
        // zero crossings (rising) over the last second: 261.6 per second
        int zc = 0; for (size_t i = (size_t)(0.5 * sr) + 1; i < n; ++i) if (l[i - 1] < 0 && l[i] >= 0) ++zc;
        CHECK(std::fabs(zc - 261.6) < 3.0, "MIDI C4: %d rising zero crossings per second", zc);
        const MidiMsg off[1] = { { 0, 0x80, 60, 0 } };
        e.process(silent.data(), silent.data(), nullptr, nullptr, l.data(), r.data(), (int)n, off, 1);
        CHECK(rmsDb(l, (size_t)(0.9 * sr), n) < -90.0, "note off: the carrier stops (%.1f dB)", rmsDb(l, (size_t)(0.9 * sr), n));
    }
}

// Follow ignores MIDI; a talkbox with FLATTEN doesn't spike when its carrier starts from silence
static void testReviewCases(double sr)
{
    printf("Follow vs MIDI, talkbox onsets (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 1.0);
    {
        Settings f; f.route = kRouteSynth; f.source = kSourceFollow; f.carrierDry = 100; f.sibilance = 0;
        const MidiMsg ev[1] = { { 0, 0x90, 60, 100 } };
        Engine e; e.setSettings(f); e.prepare(sr, 256);
        std::vector<float> silent(n, 0.0f), l(n), r(n);
        e.process(silent.data(), silent.data(), nullptr, nullptr, l.data(), r.data(), (int)n, ev, 1);
        CHECK(rmsDb(l, 0, n) < -100.0, "Follow with a MIDI note held and no voice: %.1f dB", rmsDb(l, 0, n));
    }
    const size_t m = (size_t)(sr * 2);
    auto car = sawChord(sr, m);
    for (size_t i = 0; i < m / 2; ++i) car[i] = 0.0f;   // the carrier starts at 1 s
    const auto mod = speech(sr, m, 120.0, 0, false);
    for (double fl : { 40.0, 100.0 })
    {
        Settings s = modeSettings(kTalkbox); s.flatten = fl;
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L, R; runSplit(e, car, mod, L, R);
        double on = 0, steady = 0;
        for (size_t i = m / 2; i < m / 2 + (size_t)(0.05 * sr); ++i) on = std::max(on, (double)std::fabs(L[i]));
        for (size_t i = m / 2 + (size_t)(0.3 * sr); i < m; ++i) steady = std::max(steady, (double)std::fabs(L[i]));
        CHECK(20 * std::log10(on / steady) < 6.0, "flatten %.0f: onset peak %+.1f dB against the steady peak", fl, 20 * std::log10(on / steady));
    }
}

static void testFreezeAndRoutes(double sr)
{
    printf("Freeze, routes, noise (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 3);
    const auto car = sawChord(sr, n);
    auto mod = speech(sr, n, 120.0, 0, false);
    for (size_t i = (size_t)(1.5 * sr); i < n; ++i) mod[i] = 0.0f;   // the voice stops at 1.5 s
    for (int m = 0; m < kNumModes; ++m)
    {
        Settings s = modeSettings(m);
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L(n), R(n);
        e.process(car.data(), mod.data(), nullptr, nullptr, L.data(), R.data(), (int)(1.2 * sr));
        s.freeze = true; e.setSettings(s);
        e.process(car.data() + (size_t)(1.2 * sr), mod.data() + (size_t)(1.2 * sr), nullptr, nullptr, L.data() + (size_t)(1.2 * sr), R.data() + (size_t)(1.2 * sr), (int)(n - (size_t)(1.2 * sr)));
        const double before = rmsDb(L, (size_t)(0.6 * sr), (size_t)(1.1 * sr)), after = rmsDb(L, (size_t)(2.3 * sr), (size_t)(2.9 * sr));
        CHECK(std::fabs(after - before) < 4.0, "%s: frozen, still singing a second after the voice stopped (%.1f vs %.1f dB)", kModeNames[m], after, before);
    }
    // routes: the same voice and carrier through each route give the same output
    std::vector<float> ref;
    const char* names[3] = { "split", "sidechain carrier", "sidechain voice" };
    const auto voice = speech(sr, n);
    for (int r = 0; r < 3; ++r)
    {
        Settings s = modeSettings(kClassic); s.route = r == 0 ? kRouteSplit : (r == 1 ? kRouteSidechain : kRouteSidechainVoice);
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L(n), R(n);
        if (r == 0) e.process(car.data(), voice.data(), nullptr, nullptr, L.data(), R.data(), (int)n);
        else if (r == 1) e.process(voice.data(), voice.data(), car.data(), car.data(), L.data(), R.data(), (int)n);
        else e.process(car.data(), car.data(), voice.data(), voice.data(), L.data(), R.data(), (int)n);
        if (r == 0) { ref = L; continue; }
        double err = 0, e0 = 0; for (size_t i = 0; i < n; ++i) { err += (double)(L[i] - ref[i]) * (L[i] - ref[i]); e0 += (double)ref[i] * ref[i]; }
        CHECK(10 * std::log10(err / e0 + 1e-30) < -60.0, "%s = split (%.1f dB)", names[r], 10 * std::log10(err / e0 + 1e-30));
    }
    // unvoiced noise: a fricative with no carrier at all still comes through
    {
        std::vector<float> zero(n, 0.0f), fric(n);
        fx::Rng rng(11); fx::Svf hp; hp.set(4000, 0.7, sr);
        for (size_t i = 0; i < n; ++i) fric[i] = (float)(0.2 * hp.highpass(rng.bi()));
        double lvl[2];
        for (int k = 0; k < 2; ++k)
        {
            Settings s = modeSettings(kClassic); s.noise = k ? 100 : 0;
            Engine e; e.setSettings(s); e.prepare(sr, 256);
            std::vector<float> L(n), R(n);
            e.process(zero.data(), fric.data(), nullptr, nullptr, L.data(), R.data(), (int)n);
            lvl[k] = rmsDb(L, (size_t)(0.5 * sr), n);
        }
        CHECK(lvl[0] < -100.0 && lvl[1] > -40.0, "a fricative with no carrier: %.1f dB without NOISE, %.1f dB with", lvl[0], lvl[1]);
    }
}

static void testSwitching(double sr)
{
    printf("Switching (%.0f Hz)\n", sr);
    const size_t n = (size_t)(sr * 4);
    const auto car = sawChord(sr, n), mod = speech(sr, n, 120.0, 0, false);
    Settings s = modeSettings(kClassic);
    Engine e; e.setSettings(s); e.prepare(sr, 256);
    std::vector<float> L(n), R(n);
    double worst = 0;
    int lastLat = 0;
    bool latOk = true;
    for (size_t off = 0, blk = 0; off < n; off += 256, ++blk)
    {
        if (blk % 150 == 75) { s.mode = (s.mode + 1) % kNumModes; s.fftIndex = (s.fftIndex + 1) % kNumFft; e.setSettings(s); }
        const int k = (int)std::min<size_t>(256, n - off);
        e.process(car.data() + off, mod.data() + off, nullptr, nullptr, L.data() + off, R.data() + off, k);
        if (!e.switching()) { const int lat = e.latency(); const int want = (e.activeSettings().mode == kSpectral || e.activeSettings().mode == kConvolve) ? e.fftSizeNow() : 0; if (lat != want) latOk = false; lastLat = lat; }
    }
    (void)lastLat;
    // the largest step against the signal's own largest step (the saw's resets)
    double ownStep = 0; for (size_t i = 1; i < n; ++i) ownStep = std::max(ownStep, (double)std::fabs(car[i] - car[i - 1]));
    for (size_t i = 1; i < n; ++i) worst = std::max(worst, (double)std::fabs(L[i] - L[i - 1]));
    CHECK(worst < ownStep * 1.5, "mode and FFT switches: largest step %.3f (the carrier's own %.3f)", worst, ownStep);
    CHECK(latOk, "latency follows the mode after each switch");
}

static void testFuzz(double sr)
{
    printf("Random settings (%.0f Hz)\n", sr);
    std::mt19937 rng(42);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto I = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    const size_t n = (size_t)(sr * 0.6);
    const auto car = sawChord(sr, n), mod = speech(sr, n);
    bool ok = true; double peak = 0;
    for (int t = 0; t < 40; ++t)
    {
        Settings s;
        s.mode = I(0, 3); s.route = I(0, 3); s.bandsIndex = I(0, 6); s.lowHz = U(40, 1000); s.highHz = U(1000, 16000); s.width = U(0.5, 2);
        s.detail = U(0, 100); s.fftIndex = I(0, 3); s.tube = U(0, 100); s.driveDb = U(0, 24); s.smear = U(0, 100); s.formant = U(-12, 12);
        s.attackMs = U(0.5, 100); s.releaseMs = U(5, 1000); s.flatten = U(0, 100); s.emphasis = U(0, 100); s.sibilance = U(0, 100);
        s.noise = U(0, 100); s.gateDb = U(-90, -20); s.freeze = I(0, 1); s.source = I(0, 2); s.wave = I(0, 4); s.octave = I(-2, 2);
        s.chord = I(0, 6); s.key = I(0, 11); s.scale = I(0, 14); s.glideMs = U(0, 500); s.detune = U(0, 100); s.spread = U(0, 100);
        s.ensemble = U(0, 100); s.voiceDry = U(0, 100); s.carrierDry = U(0, 100); s.outDb = U(-24, 24);
        Engine e; e.setSettings(s); e.prepare(sr, 512);
        std::vector<float> L(n), R(n);
        const MidiMsg ev[3] = { { 3, 0x90, (uint8_t)I(30, 90), 90 }, { 100, 0xE0, 0, (uint8_t)I(0, 127) }, { 200, 0xB0, 1, 127 } };
        size_t off = 0; bool first = true;
        while (off < n)
        {
            const int k = (int)std::min<size_t>((size_t)I(1, 700), n - off);
            const float* sc = (s.route == kRouteSidechain || s.route == kRouteSidechainVoice) ? (s.route == kRouteSidechain ? car.data() : mod.data()) : nullptr;
            const float* in = s.route == kRouteSidechain || s.route == kRouteSynth ? mod.data() : car.data();
            e.process(in + off, s.route == kRouteSplit ? mod.data() + off : in + off, sc ? sc + off : nullptr, sc ? sc + off : nullptr, L.data() + off, R.data() + off, k,
                      first ? ev : nullptr, first ? 3 : 0);
            first = false;
            off += (size_t)k;
        }
        for (size_t i = 0; i < n; ++i) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) ok = false; peak = std::max(peak, (double)std::max(std::fabs(L[i]), std::fabs(R[i]))); }
    }
    CHECK(ok && peak <= 4.0, "40 random settings / routes / block sizes / MIDI: finite, peak %.2f", peak);
}

// every factory preset on a sung voice (its own routing: the synth on its own, a saw chord on the sidechain or the
// left channel): a usable level, no clipping
static void testPresets()
{
    printf("Factory presets (48 kHz)\n");
    const double sr = 48000; const size_t n = (size_t)(sr * 3);
    const auto car = sawChord(sr, n), mod = speech(sr, n, 160.0);
    const auto& ps = params();
    for (const Preset& pr : presets())
    {
        float v[kNumParams];
        for (int i = 0; i < kNumParams; ++i) v[i] = ps[(size_t)i].def;
        for (const auto& pv : pr.values) for (int i = 0; i < kNumParams; ++i) if (std::strcmp(ps[(size_t)i].symbol, pv.symbol) == 0) v[i] = pv.value;
        Settings s;
        s.mode = (int)v[kMode]; s.route = (int)v[kRoute]; s.bandsIndex = (int)v[kBands]; s.lowHz = v[kLow]; s.highHz = v[kHigh]; s.width = v[kWidth];
        s.detail = v[kDetail]; s.fftIndex = (int)v[kFftSize]; s.tube = v[kTube]; s.driveDb = v[kDrive]; s.smear = v[kSmear]; s.formant = v[kFormant];
        s.attackMs = v[kAttack]; s.releaseMs = v[kRelease]; s.flatten = v[kFlatten]; s.emphasis = v[kEmphasis]; s.sibilance = v[kSibilance];
        s.noise = v[kNoise]; s.gateDb = v[kGate]; s.freeze = v[kFreeze] >= 0.5f; s.source = (int)v[kSource]; s.wave = (int)v[kWave];
        s.octave = (int)v[kOctave]; s.chord = (int)v[kChord]; s.key = (int)v[kKey]; s.scale = (int)v[kScale]; s.glideMs = v[kGlide];
        s.detune = v[kDetune]; s.spread = v[kSpread]; s.ensemble = v[kEnsemble]; s.voiceDry = v[kVoiceDry]; s.carrierDry = v[kCarrierDry]; s.outDb = v[kOutGain];
        Engine e; e.setSettings(s); e.prepare(sr, 256);
        std::vector<float> L(n), R(n);
        if (s.route == kRouteSplit) e.process(car.data(), mod.data(), nullptr, nullptr, L.data(), R.data(), (int)n);
        else if (s.route == kRouteSidechain) e.process(mod.data(), mod.data(), car.data(), car.data(), L.data(), R.data(), (int)n);
        else if (s.route == kRouteSidechainVoice) e.process(car.data(), car.data(), mod.data(), mod.data(), L.data(), R.data(), (int)n);
        else e.process(mod.data(), mod.data(), nullptr, nullptr, L.data(), R.data(), (int)n);
        double pk = 0; for (size_t i = 0; i < n; ++i) pk = std::max(pk, (double)std::max(std::fabs(L[i]), std::fabs(R[i])));
        const double lv = 0.5 * (rmsDb(L, (size_t)(0.5 * sr), n) + rmsDb(R, (size_t)(0.5 * sr), n));
        CHECK(lv > -30.0 && lv < -8.0 && pk < 1.0, "%-30s %6.1f dB RMS, peak %5.1f dBFS", pr.name, lv, 20 * std::log10(pk + 1e-12));
    }
}

static void cpu()
{
    printf("CPU (48 kHz, per second of audio)\n");
    const double sr = 48000; const size_t n = (size_t)sr * 4;
    const auto car = sawChord(sr, n), mod = speech(sr, n);
    for (int m = 0; m < kNumModes; ++m)
        for (int heavy = 0; heavy < 2; ++heavy)
        {
            Settings s = modeSettings(m);
            if (heavy) { s.bandsIndex = 6; s.fftIndex = 0; s.detail = 100; s.route = kRouteSynth; s.chord = kChordStack; s.wave = kWaveSuper; s.ensemble = 50; s.noise = 50; s.flatten = 60; }
            Engine e; e.setSettings(s); e.prepare(sr, 256);
            std::vector<float> L(n), R(n);
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t off = 0; off < n; off += 256) e.process(car.data() + off, mod.data() + off, nullptr, nullptr, L.data() + off, R.data() + off, 256);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  %-9s %s: %.2f%% of a core\n", kModeNames[m], heavy ? "heavy (40 bands / FFT 512 / synth stack + super + ensemble)" : "default", 100.0 * secs / 4.0);
        }
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        testClassicIsOriginal(sr);
        testModes(sr);
        testFormant(sr);
        testTalkboxFormants(sr);
        testPitchTracker(sr);
        testFollowAndMidi(sr);
        testReviewCases(sr);
        testFreezeAndRoutes(sr);
        testSwitching(sr);
        testFuzz(sr);
    }
    testPresets();
    cpu();
    if (fails) { printf("%d FAILURE(S)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
