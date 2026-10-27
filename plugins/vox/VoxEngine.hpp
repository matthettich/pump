// pump VOX engine: a vocoder with four ways of putting a voice onto a carrier. Framework-free.
//
// Routing (ROUTE): the voice (modulator) and the carrier come from the track's input and the sidechain:
//   Synth carrier      the input is the voice; the carrier is the built-in synth (below)
//   Sidechain carrier  the input is the voice; the carrier comes in on the sidechain (a synth on another track)
//   Sidechain voice    the input is the carrier; the voice comes in on the sidechain
//   Split L/R          one stereo input: left = carrier, right = voice (the convention of the first VOX, built for a
//                      hardware sampler that gives an effect one stereo pair)
// The voice is summed to mono (Split: the right channel); the carrier stays stereo.
//
// Voice conditioning, shared by every mode:
//   GATE       a level gate on the voice (bleed and room noise stay out of the vocoder); off at the bottom stop
//   EMPHASIS   a high shelf (up to +12 dB above 2 kHz) on the copy the analysis hears, so consonants open the top
//   NOISE      unvoiced sounds (s, f, t, sh) have no pitch for a carrier to carry: a detector (energy above 3.5 kHz
//              against the whole voice, and the pitch tracker finding no period) adds white noise to the carrier
//              while they last, so consonants come through even between notes
//   SIBILANCE  the voice's own top end (fourth-order high pass at 5 kHz) mixed into the output
//   FREEZE     holds the analysis (band envelopes, spectral envelope, mouth filter): the carrier keeps singing the
//              last vowel
//   FORMANT    moves the voice's formants up or down (+-12 semitones) in every mode
//
// The modes (MODE). Output level ~ carrier level x voice level, calibrated so the modes match within a few dB:
//   Classic    the original VOX channel vocoder, unchanged: BANDS (8..40) log-spaced band pairs from LOW to HIGH, each
//              two cascaded band passes with Q set so neighbours meet at -3 dB (x WIDTH); the voice band's rectified
//              level (ATTACK / RELEASE) scales the matching carrier band; FORMANT shifts the carrier bank against the
//              voice bank; SPREAD pans odd and even bands apart. With FLATTEN, NOISE, EMPHASIS and the gate at zero,
//              ROUTE Split L/R, it is the first VOX sample for sample (tests/test_vox.cpp checks this) minus its output
//              soft clip. FLATTEN evens the carrier's bands out (each band is scaled toward the mean band level,
//              boost limited to +30 dB), so a dull carrier still speaks in the top bands.
//   Spectral   an FFT vocoder (STFT, sqrt-Hann, 75 % overlap; FFT size 512..4096 at 48 kHz, scaled with the rate;
//              latency = the FFT size): the voice's spectral envelope (power averaged over a window DETAIL wide, from
//              half an octave down to a fiftieth), shifted by FORMANT and smoothed in time per bin (ATTACK /
//              RELEASE), multiplies the carrier's spectrum; FLATTEN divides out the carrier's own envelope (1/3
//              octave). Sharper than any filter bank: at high DETAIL the voice's own harmonics start to show.
//   Talkbox    the mouth as a filter: linear prediction (Levinson-Durbin on the autocorrelation of a 21 ms Hann
//              frame every 5 ms; order 12..56 from DETAIL, scaled with the rate; 40 Hz lag window) models the voice's
//              vocal tract, and the carrier plays through that all-pole filter (a lattice whose reflection
//              coefficients glide sample by sample, so it can't go unstable). FORMANT warps the frame's power spectrum
//              before the autocorrelation. Before the mouth the carrier goes through the driver (DRIVE, a soft
//              clipper) and the TUBE (a damped comb, a ~90 cm tube's resonances); FLATTEN whitens it with its own
//              order-16 prediction filter first, as the throat would. No latency.
//   Convolve   the two spectra multiplied, frame by frame: the carrier filtered by the voice's whole spectrum,
//              harmonics and all (metallic, ringing). DETAIL blurs the voice's spectrum (100 % = untouched); SMEAR
//              adds the voice's phase (0 = magnitudes only, 100 % = true complex multiplication, which smears each
//              frame in time). Same STFT and latency as Spectral.
//
// The built-in carrier (ROUTE Synth carrier): up to 8 voices of SAW, SQUARE, SUPER (seven detuned saws), BUZZ (a
// narrow 12.5 % pulse) or NOISE, band-limited (polyBLEP), DETUNE spreading unison voices across the stereo field,
// OCTAVE. SOURCE: MIDI plays it from notes (pitch bend +-2, mod wheel vibrato, sustain pedal); Follow tracks the
// voice's pitch (pitch::Tracker), snaps it to KEY / SCALE ("Exact" follows it as sung), glides there in GLIDE ms and
// adds a CHORD built from the scale (Octave, Fifth, Triad, Seventh, Sus, Stack); Auto plays MIDI while notes are
// held and follows the voice otherwise. Follow voices hold their note through consonants and let go after half a
// second of silence.
//
// After the mode: ENSEMBLE (three modulated delays per side, the shimmer of a string machine), SIBILANCE, the dry
// voice (VOICE) and dry carrier (CARRIER), all aligned with the mode's latency, OUTPUT. Mode, routing and FFT size
// changes fade out (10 ms), switch and fade back in.
//
// Feeds for the UI (atomics, written once per ~10 ms): the band display (per band or display point: centre
// frequency, voice level, carrier level), the voice's pitch and confidence, the follow note, the notes the synth is
// playing, input / carrier / output levels, the unvoiced detector, and whether the carrier is missing.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/FFT.hpp"
#include "dsp/Pitch.hpp"
#include "VoxParams.hpp"
#include <array>
#include <atomic>
#include <memory>

namespace pump { namespace vox {

struct Settings {
    int mode = kClassic, route = kRouteSynth, bandsIndex = 2;
    double lowHz = 100.0, highHz = 8000.0, width = 1.0, detail = 50.0;
    int fftIndex = 1;
    double tube = 35.0, driveDb = 6.0, smear = 50.0;
    double formant = 0.0, attackMs = 4.0, releaseMs = 60.0, flatten = 40.0, emphasis = 30.0;
    double sibilance = 30.0, noise = 30.0, gateDb = kGateOff;
    bool freeze = false;
    int source = kSourceAuto, wave = kWaveSaw, octave = 0, chord = kChordOff, key = 0, scale = 1;
    double glideMs = 40.0, detune = 25.0, spread = 30.0, ensemble = 0.0, voiceDry = 0.0, carrierDry = 0.0, outDb = 0.0;
};

struct MidiMsg { uint32_t frame; uint8_t status, d1, d2; };

constexpr int kDispMax = 48;   // display points (Classic shows its bands, the others 48 log-spaced points)
constexpr int kDispArr = kDispMax > kMaxBands ? kDispMax : kMaxBands;

// ---- small filters -----------------------------------------------------------------------------------------------------
// The original VOX's biquads: RBJ designs, transposed direct form II in single precision.
struct Bq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct BqState { float z1 = 0, z2 = 0; };
inline float bqRun(const Bq& c, BqState& s, float x)
{
    const float y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}
inline void designBandpass(Bq& c, float f, float q, float sr)   // constant 0 dB peak
{
    const float nyq = 0.45f * sr;
    if (f > nyq) f = nyq;
    if (f < 20.f) f = 20.f;
    const float w = 2.f * (float)kPi * f / sr, cw = std::cos(w), sw = std::sin(w), alpha = sw / (2.f * q), a0 = 1.f + alpha;
    c.b0 = alpha / a0; c.b1 = 0.f; c.b2 = -alpha / a0; c.a1 = -2.f * cw / a0; c.a2 = (1.f - alpha) / a0;
}
inline void designHighpass(Bq& c, float f, float q, float sr)
{
    const float w = 2.f * (float)kPi * f / sr, cw = std::cos(w), sw = std::sin(w), alpha = sw / (2.f * q), a0 = 1.f + alpha;
    c.b0 = (1.f + cw) * 0.5f / a0; c.b1 = -(1.f + cw) / a0; c.b2 = (1.f + cw) * 0.5f / a0; c.a1 = -2.f * cw / a0; c.a2 = (1.f - alpha) / a0;
}

// ---- Classic: the original channel vocoder ---------------------------------------------------------------------------
class Classic {
public:
    void prepare(double sampleRate) { sr = (float)sampleRate; n = 0; reset(); }
    void reset()
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            ms[b][0] = ms[b][1] = BqState();
            for (int c = 0; c < 2; ++c) cs[c][b][0] = cs[c][b][1] = BqState();
            env[b] = 0.0f; cenv[b] = 0.0f; cgain[b] = 1.0f;
        }
    }
    // Recompute the banks (call when bands, range, width, formant, envelope times or spread change).
    void design(int bands, double lo, double hi, double width, double formantSemis, double attackMs, double releaseMs, double spread)
    {
        const int nn = clampv(bands, 2, kMaxBands);
        float flo = (float)lo, fhi = (float)hi;
        if (fhi < flo * 2.f) fhi = flo * 2.f;
        // log-spaced centres; Q so neighbours meet at -3 dB, 0.644 undoing the narrowing of two cascaded biquads
        const float ratio = std::pow(fhi / flo, 1.f / (float)(nn - 1));
        const float q = (std::sqrt(ratio) / (ratio - 1.f)) * 0.644f * (float)width;
        const float shift = (float)std::exp2(formantSemis / 12.0);
        if (nn > n)   // bands switching on start from silence
            for (int b = n; b < nn; ++b)
            {
                ms[b][0] = ms[b][1] = BqState();
                for (int c = 0; c < 2; ++c) cs[c][b][0] = cs[c][b][1] = BqState();
                env[b] = 0.0f; cenv[b] = 0.0f; cgain[b] = 1.0f;
            }
        const float sp = (float)clampv(spread * 0.01, 0.0, 1.0);
        for (int b = 0; b < nn; ++b)
        {
            const float f = flo * std::pow(ratio, (float)b);
            centre[b] = f;
            designBandpass(mc[b], f, q, sr);
            designBandpass(cc[b], f * shift, q, sr);
            // equal-power pan, odd bands right, even left; unity when centred
            const float pan = (b & 1 ? 1.f : -1.f) * sp;
            gl[b] = std::sqrt(1.f - pan);
            gr[b] = std::sqrt(1.f + pan);
        }
        n = nn;
        att = std::exp(-1.f / ((float)attackMs * 0.001f * sr));
        rel = std::exp(-1.f / ((float)releaseMs * 0.001f * sr));
        // more bands = each band quieter; sqrt keeps the overall level similar (the original's calibration)
        makeup = 18.0f * std::sqrt(16.f / (float)n);
        catt = std::exp(-1.f / (0.002f * sr));
        crel = std::exp(-1.f / (0.030f * sr));
    }
    void setFlatten(double f) { flat = (float)clampv(f * 0.01, 0.0, 1.0); }
    void setSolo(double loHz, double hiHz)
    {
        for (int b = 0; b < kMaxBands; ++b) active[b] = loHz <= 0.0 || (centre[b] >= loHz && centre[b] <= hiHz);
    }
    // one sample: voice (analysis copy), carrier L/R -> out L/R
    // `stereo` is fixed by the routing (Split L/R has one carrier channel), never decided per sample
    inline void process(float m, float cl, float cr, bool freeze, bool stereo, float& outL, float& outR)
    {
        float l = 0.f, r = 0.f;
        for (int b = 0; b < n; ++b)
        {
            float mb = bqRun(mc[b], ms[b][0], m);
            mb = bqRun(mc[b], ms[b][1], mb);
            if (!freeze)
            {
                const float a = std::fabs(mb);
                float e = env[b];
                e = a + (a > e ? att : rel) * (e - a);
                env[b] = e;
            }
            float cbl = bqRun(cc[b], cs[0][b][0], cl);
            cbl = bqRun(cc[b], cs[0][b][1], cbl);
            float cbr = cbl;
            if (stereo)
            {
                cbr = bqRun(cc[b], cs[1][b][0], cr);
                cbr = bqRun(cc[b], cs[1][b][1], cbr);
            }
            {
                // the carrier band's level (FLATTEN and the display)
                const float a = 0.5f * (std::fabs(cbl) + std::fabs(cbr));
                float e = cenv[b];
                e = a + (a > e ? catt : crel) * (e - a);
                cenv[b] = e;
            }
            if (!active[b]) continue;
            const float g = env[b] * cgain[b];
            l += cbl * g * gl[b];
            r += cbr * g * gr[b];
        }
        outL = l * makeup;
        outR = r * makeup;
        if (flat > 0.0f && ++ctl >= 32) { ctl = 0; updateFlatten(); }
        else if (flat <= 0.0f && cgain[0] != 1.0f) for (int b = 0; b < kMaxBands; ++b) cgain[b] = 1.0f;
    }
    int bands() const { return n; }
    float centreHz(int b) const { return centre[b]; }
    float bandLevel(int b) const { return env[b]; }
    float carrierLevel(int b) const { return cenv[b]; }

private:
    void updateFlatten()
    {
        float mean = 0.0f;
        for (int b = 0; b < n; ++b) mean += cenv[b];
        mean /= (float)std::max(1, n);
        for (int b = 0; b < n; ++b)
        {
            const float ratio = clampv(mean / (cenv[b] + 1e-7f), 0.01f, 31.6f);
            const float t = std::pow(ratio, flat);
            cgain[b] += (t - cgain[b]) * 0.25f;
        }
    }
    float sr = 48000.f;
    int n = 0, ctl = 0;
    Bq mc[kMaxBands], cc[kMaxBands];
    BqState ms[kMaxBands][2], cs[2][kMaxBands][2];
    float env[kMaxBands] = {}, cenv[kMaxBands] = {}, cgain[kMaxBands] = {}, gl[kMaxBands] = {}, gr[kMaxBands] = {}, centre[kMaxBands] = {};
    bool active[kMaxBands] = { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
                               true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true };
    float att = 0.f, rel = 0.f, catt = 0.f, crel = 0.f, makeup = 18.f, flat = 0.f;
};

// ---- Spectral and Convolve: STFT with a voice analysis channel and a stereo carrier ------------------------------------
class SpectralProc {
public:
    // Allocate for every size up to maxN (all FFT setups made here, never on the audio thread).
    void prepare(double sampleRate, const int* sizes, int numSizes)
    {
        sr = sampleRate;
        maxN = 0;
        for (int i = 0; i < numSizes; ++i)
        {
            ffts[i] = std::make_unique<RealFFT>(sizes[i]);
            fftSizes[i] = sizes[i];
            maxN = std::max(maxN, sizes[i]);
        }
        numFfts = numSizes;
        for (auto* b : { &ringV, &ringL, &ringR, &win, &accL, &accR, &frame, &specV, &specL, &specR })
            b->resize((size_t)maxN);
        outQL.resize((size_t)maxN); outQR.resize((size_t)maxN);
        const size_t K = (size_t)maxN / 2 + 1;
        cumM.assign(K + 1, 0.0); cumC.assign(K + 1, 0.0);
        envM.assign(K, 0.0f); envC.assign(K, 0.0f); phase.assign(K, 0.0f); smooth.assign(K, 0.0f); shifted.assign(K, 0.0f); groupPan.assign(K, 0);
        configure(sizes[0]);
    }
    void configure(int N_)
    {
        N = N_;
        H = N / 4;
        K = N / 2;
        fft = nullptr;
        for (int i = 0; i < numFfts; ++i) if (fftSizes[i] == N) fft = ffts[i].get();
        sumw2 = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float w = (float)std::sqrt(0.5 - 0.5 * std::cos(kTwoPi * i / N));
            win[(size_t)i] = w;
            sumw2 += (double)w * w;
        }
        // stereo spread groups: third-octave groups from 50 Hz, alternating sides
        const double binHz = sr / N;
        for (int k = 0; k <= K; ++k)
        {
            const double f = std::max(binHz * k, 50.0);
            groupPan[(size_t)k] = (int)std::floor(3.0 * std::log2(f / 50.0)) & 1;
        }
        reset();
    }
    void reset()
    {
        for (auto* b : { &ringV, &ringL, &ringR, &accL, &accR, &outQL, &outQR }) b->clear();
        std::fill(smooth.begin(), smooth.end(), 0.0f);
        std::fill(phase.begin(), phase.end(), 0.0f);
        wpos = 0; cnt = 0;
    }
    int latency() const { return N; }
    int size() const { return N; }

    struct Params {
        bool convolve = false, freeze = false;
        double widthOct = 0.2, formantShift = 1.0, attack = 0.01, release = 0.06, flatten = 0.0, smear = 0.0, detail = 0.5;
        double spread = 0.0, soloLo = 0.0, soloHi = 0.0;
    };
    Params p;

    inline void process(float v, float cl, float cr, float& outL, float& outR)
    {
        ringV[(size_t)wpos] = v; ringL[(size_t)wpos] = cl; ringR[(size_t)wpos] = cr;
        outL = outQL[(size_t)cnt]; outR = outQR[(size_t)cnt];
        wpos = (wpos + 1) % N;
        if (++cnt == H) { cnt = 0; runFrame(); }
    }

    // display: voice level and carrier level (dB, band RMS) at frequency f with a band `bwOct` octaves wide
    float displayVoiceDb(double f, double bwOct) const { return bandDb(smooth, f, bwOct); }
    float displayCarrierDb(double f, double bwOct) const { return bandDb(envC, f, bwOct); }

private:
    float bandDb(const std::vector<float>& mag, double f, double bwOct) const
    {
        const double binHz = sr / N;
        int a = (int)std::floor(f * std::exp2(-0.5 * bwOct) / binHz), b = (int)std::ceil(f * std::exp2(0.5 * bwOct) / binHz);
        a = clampv(a, 1, K - 1); b = clampv(b, a, K - 1);
        double e = 0.0;
        for (int k = a; k <= b; ++k) e += (double)mag[(size_t)k] * mag[(size_t)k];
        // band RMS: (2/N) x the bins' power / the window's energy
        return (float)(10.0 * std::log10(2.0 * e / ((double)N * sumw2) + 1e-30));
    }

    void runFrame()
    {
        // windowed frames in time order, transformed
        auto analyse = [&](const AlignedBuffer& ring, AlignedBuffer& spec) {
            for (int i = 0; i < N; ++i) frame[(size_t)i] = ring[(size_t)((wpos + i) % N)] * win[(size_t)i];
            fft->forward(frame.data(), spec.data());
        };
        analyse(ringV, specV);
        analyse(ringL, specL);
        analyse(ringR, specR);
        auto pw = [](const AlignedBuffer& s, int k, int K_) -> double {
            if (k == 0) return (double)s[0] * s[0];
            if (k == K_) return (double)s[1] * s[1];
            return (double)s[(size_t)(2 * k)] * s[(size_t)(2 * k)] + (double)s[(size_t)(2 * k + 1)] * s[(size_t)(2 * k + 1)];
        };
        // prefix sums of the voice's and the carrier's power
        cumM[0] = cumC[0] = 0.0;
        for (int k = 0; k <= K; ++k)
        {
            cumM[(size_t)k + 1] = cumM[(size_t)k] + pw(specV, k, K);
            cumC[(size_t)k + 1] = cumC[(size_t)k] + 0.5 * (pw(specL, k, K) + pw(specR, k, K));
        }
        auto meanPow = [&](const std::vector<double>& cum, int k, double r) {
            int a = (int)std::floor(k / r), b = (int)std::ceil(k * r);
            a = clampv(a, 0, K); b = clampv(b, a, K);
            return (cum[(size_t)b + 1] - cum[(size_t)a]) / (double)(b - a + 1);
        };
        // voice envelope (or, for Convolve, the voice's magnitude blurred by 1 - DETAIL)
        const double r = std::exp2(0.5 * p.widthOct);
        const double rc = std::exp2(1.0 / 6.0);   // the carrier's envelope: 1/3 octave
        const double binHz = sr / N;
        int k80 = std::max(1, (int)(80.0 / binHz)), k16 = std::min(K, (int)(16000.0 / binHz));
        if (k16 <= k80) k16 = std::min(K, k80 + 1);
        double carMean = 0.0;
        for (int k = 0; k <= K; ++k)
        {
            envC[(size_t)k] = (float)std::sqrt(meanPow(cumC, k, rc));
            if (p.convolve)
            {
                const double raw = std::sqrt(pw(specV, k, K));
                const double env = std::sqrt(meanPow(cumM, k, r));
                envM[(size_t)k] = (float)(env + (raw - env) * p.detail);
            }
            else envM[(size_t)k] = (float)std::sqrt(meanPow(cumM, k, r));
        }
        carMean = std::sqrt((cumC[(size_t)k16 + 1] - cumC[(size_t)k80]) / (double)(k16 - k80 + 1));
        // formant shift: the envelope at k / shift
        const double inv = 1.0 / p.formantShift;
        for (int k = 0; k <= K; ++k)
        {
            const double src = k * inv;
            const int i = (int)src;
            const double t = src - i;
            float vsrc = 0.0f;
            if (i < K) vsrc = (float)(envM[(size_t)i] * (1.0 - t) + envM[(size_t)i + 1] * t);
            else if (i == K) vsrc = envM[(size_t)K];
            shifted[(size_t)k] = vsrc;
        }
        // time smoothing per bin (or hold)
        if (!p.freeze)
        {
            const double hopS = H / sr;
            const float aC = (float)std::exp(-hopS / std::max(1e-4, p.attack)), rC = (float)std::exp(-hopS / std::max(1e-4, p.release));
            for (int k = 0; k <= K; ++k)
            {
                const float x = shifted[(size_t)k], e = smooth[(size_t)k];
                smooth[(size_t)k] = x + (x > e ? aC : rC) * (e - x);
            }
        }
        // gains: voice level x carrier flatten, applied to both carrier channels
        // (x sqrt(48k / rate): at a higher rate the same harmonics are packed into a smaller share of the band)
        const double norm = std::sqrt(48000.0 / sr) / std::sqrt(sumw2);
        const double makeup = p.convolve ? kConvolveMakeup : kSpectralMakeup;
        const double ph = p.convolve ? p.smear : 0.0;
        const float spreadAmt = (float)clampv(p.spread, 0.0, 1.0);
        const float gSide[2][2] = { { std::sqrt(1.f + spreadAmt), std::sqrt(1.f - spreadAmt) },
                                    { std::sqrt(1.f - spreadAmt), std::sqrt(1.f + spreadAmt) } };
        for (int k = 0; k <= K; ++k)
        {
            double g = smooth[(size_t)k] * norm * makeup;
            if (p.flatten > 0.0)
            {
                const double ratio = clampv(carMean / ((double)envC[(size_t)k] + 1e-9), 0.01, 31.6);
                g *= std::pow(ratio, p.flatten);
            }
            if (p.soloLo > 0.0 && (k * binHz < p.soloLo || k * binHz > p.soloHi)) g = 0.0;
            const int side = groupPan[(size_t)k];
            const float gl = (float)g * gSide[side][0], gr = (float)g * gSide[side][1];
            if (k == 0) { specL[0] *= gl; specR[0] *= gr; continue; }
            if (k == K) { specL[1] *= gl; specR[1] *= gr; continue; }
            float* L = &specL[(size_t)(2 * k)];
            float* R = &specR[(size_t)(2 * k)];
            if (ph > 0.0)
            {
                // rotate by a share of the voice's phase (true convolution at 100 %)
                if (!p.freeze) phase[(size_t)k] = (float)std::atan2((double)specV[(size_t)(2 * k + 1)], (double)specV[(size_t)(2 * k)]);
                const double ang = ph * phase[(size_t)k];
                const float c = (float)std::cos(ang), s = (float)std::sin(ang);
                const float lr = L[0] * c - L[1] * s, li = L[0] * s + L[1] * c;
                const float rr = R[0] * c - R[1] * s, ri = R[0] * s + R[1] * c;
                L[0] = lr * gl; L[1] = li * gl; R[0] = rr * gr; R[1] = ri * gr;
            }
            else { L[0] *= gl; L[1] *= gl; R[0] *= gr; R[1] *= gr; }
        }
        // back, overlap-add (sqrt-Hann twice = Hann, which sums to 2 at 75 %; the inverse is unscaled, x N)
        const float scale = 0.5f / (float)N;
        auto synth = [&](AlignedBuffer& spec, AlignedBuffer& acc, AlignedBuffer& q) {
            fft->inverse(spec.data(), frame.data());
            for (int i = 0; i < N; ++i) acc[(size_t)i] += frame[(size_t)i] * win[(size_t)i] * scale;
            std::memcpy(q.data(), acc.data(), sizeof(float) * (size_t)H);
            std::memmove(acc.data(), acc.data() + H, sizeof(float) * (size_t)(N - H));
            std::memset(acc.data() + (N - H), 0, sizeof(float) * (size_t)H);
        };
        synth(specL, accL, outQL);
        synth(specR, accR, outQR);
    }

public:
    // level calibration against Classic (tests/test_vox.cpp)
    static constexpr double kSpectralMakeup = 1.15, kConvolveMakeup = 1.6;
private:
    double sr = 48000.0, sumw2 = 1.0;
    int N = 1024, H = 256, K = 512, maxN = 0, wpos = 0, cnt = 0, numFfts = 0;
    std::unique_ptr<RealFFT> ffts[kNumFft + 2];
    int fftSizes[kNumFft + 2] = {};
    RealFFT* fft = nullptr;
    AlignedBuffer ringV, ringL, ringR, win, accL, accR, outQL, outQR, frame, specV, specL, specR;
    std::vector<double> cumM, cumC;
    std::vector<float> envM, envC, smooth, shifted, phase;
    std::vector<int> groupPan;
};

// ---- Talkbox: driver, tube, throat whitening, LPC mouth --------------------------------------------------------------
class Talkbox {
public:
    static constexpr int kMaxOrder = 112, kCarOrder = 16;

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        Na = 1024;
        while (Na < 0.0205 * sr) Na *= 2;
        fft.setup(Na);
        hop = std::max(32, (int)std::lround(0.005 * sr));
        win.resize((size_t)Na); ringV.assign((size_t)Na, 0.0f); ringC.assign((size_t)Na, 0.0f);
        frame.resize((size_t)Na); spec.resize((size_t)Na); pw.assign((size_t)Na / 2 + 1, 0.0); carPw.assign((size_t)Na / 2 + 1, 0.0); acf.resize((size_t)Na);
        sumw2 = 0.0;
        for (int i = 0; i < Na; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i + 0.5) / Na); win[(size_t)i] = (float)w; sumw2 += w * w; }
        // lag window: a 40 Hz Gaussian (bandwidth expansion: no needle-sharp poles)
        for (int i = 0; i <= kMaxOrder; ++i) { const double a = kTwoPi * 40.0 * i / sr; lagWin[i] = std::exp(-0.5 * a * a); }
        tubeLen = std::max(8, (int)std::lround(sr / 190.0));
        for (auto& t : tube) t.setMax(tubeLen + 8);
        haas.setMax((int)(0.012 * sr) + 8);
        lips.set(12000.0, sr);
        rateNorm = std::sqrt(48000.0 / sr);
        smC = 1.0 - std::exp(-1.0 / (0.010 * sr));
        wsRise = 1.0 - std::exp(-1.0 / (0.030 * sr));
        carRel = std::exp(-1.0 / (0.020 * sr));
        lipsR = lips;
        reset();
    }
    void reset()
    {
        std::fill(ringV.begin(), ringV.end(), 0.0f);
        std::fill(ringC.begin(), ringC.end(), 0.0f);
        wpos = 0; cnt = 0;
        for (int c = 0; c < 2; ++c)
        {
            std::fill(std::begin(bs[c]), std::end(bs[c]), 0.0);
            std::fill(std::begin(cb[c]), std::end(cb[c]), 0.0);
            tube[c].clear(); tubeLp[c] = 0.0;
        }
        lips.reset(); lipsR.reset();
        driveCur = p.driveDb; tubeCur = clampv(p.tube, 0.0, 1.0); wsCur = 1.0; onsetRamp = 1.0; lastCarRms = 0.0; carEnv = 0.0;
        haas.clear();
        std::fill(std::begin(k), std::end(k), 0.0); std::fill(std::begin(kStep), std::end(kStep), 0.0); std::fill(std::begin(kTgt), std::end(kTgt), 0.0);
        std::fill(std::begin(kc), std::end(kc), 0.0); std::fill(std::begin(kcStep), std::end(kcStep), 0.0);
        g = gStep = gEnv = 0.0;
        cRms = cSig = 1e-4; cRmsStep = cSigStep = 0.0;
        std::fill(std::begin(aDisp), std::end(aDisp), 0.0);
        aDisp[0] = 1.0;
        dispG = 0.0;
    }
    struct Params { int order = 32; double formantShift = 1.0, tube = 0.35, driveDb = 6.0, flatten = 0.4, attack = 0.004, release = 0.06, spread = 0.0; bool freeze = false; };
    Params p;
    int latency() const { return 0; }

    inline void process(float v, float cl, float cr, float& outL, float& outR)
    {
        // driver and tube on the carrier
        // the driver: a soft clipper that keeps a -6 dBFS signal at -6 dBFS (quieter parts come up, peaks round off)
        driveCur += (p.driveDb - driveCur) * smC;
        tubeCur += (clampv(p.tube, 0.0, 1.0) - tubeCur) * smC;
        const double dg = dbToGain(driveCur), dgN = 0.5 / std::tanh(0.5 * dg);
        double x[2] = { cl, cr };
        const double fb = 0.62 * tubeCur;
        for (int c = 0; c < 2; ++c)
        {
            double y = dg > 1.0001 ? std::tanh(x[c] * dg) * dgN : x[c];
            // tube: a comb at the tube's length, damped by a one-pole inside the loop
            const double d = tube[c].at(tubeLen - 1);
            tubeLp[c] += 0.45 * (d - tubeLp[c]);
            const double t = y + fb * tubeLp[c];
            tube[c].push((float)t);
            x[c] = y + (t - y) * tubeCur * 0.8;
        }
        // while the carrier is silent, hold the onset fade at zero: it starts with the carrier's first sample
        {
            const double ax = 0.5 * (std::fabs(x[0]) + std::fabs(x[1]));
            carEnv = std::max(ax, carEnv * carRel);
            if (carEnv < 1e-4) onsetRamp = 0.0;
        }
        ringV[(size_t)wpos] = v;
        ringC[(size_t)wpos] = (float)(0.5 * (x[0] + x[1]));
        wpos = (wpos + 1) % Na;
        // glide the filters toward this hop's targets
        for (int i = 0; i < order; ++i) k[i] += kStep[i];
        for (int i = 0; i < kCarOrder; ++i) kc[i] += kcStep[i];
        g += gStep; cRms += cRmsStep; cSig += cSigStep;
        const double fl = clampv(p.flatten, 0.0, 1.0);
        // equal floors (silence reads 1, not 10) and at most +12 dB, so a carrier starting from silence can't spike
        // and it rises over ~30 ms (the analysis frame lags an onset), falls at once
        const double wT = std::min(4.0, std::max(cRms, 1e-6) / std::max(cSig, 1e-6));
        wsCur += (wT - wsCur) * (wT > wsCur ? wsRise : 0.05);
        const double wScale = wsCur;
        onsetRamp = std::min(1.0, onsetRamp + 1.0 / Na);
        for (int c = 0; c < 2; ++c)
        {
            // throat: whitening by the carrier's own prediction filter (FIR lattice), scaled back to its RMS
            // (cb[c][i] holds stage i's backward signal from the previous sample)
            double f = x[c];
            if (fl > 0.0)
            {
                double bNow = x[c];   // b_0(n)
                for (int i = 0; i < kCarOrder; ++i)
                {
                    const double bOld = cb[c][i];
                    const double fn = f + kc[i] * bOld;
                    const double bn = kc[i] * f + bOld;
                    cb[c][i] = bNow;
                    bNow = bn;
                    f = fn;
                }
            }
            // a carrier starting from silence fades in over one analysis frame while its whitening filter is found
            const double e = ((1.0 - fl) * x[c] + fl * f * wScale) * (1.0 - fl * (1.0 - onsetRamp));
            // mouth: all-pole lattice
            double s = e;
            for (int i = order - 1; i >= 0; --i)
            {
                s -= k[i] * bs[c][i];
                bs[c][i + 1] = bs[c][i] + k[i] * s;
            }
            bs[c][0] = s;
            if (!(std::fabs(s) < 1e6)) { reset(); s = 0.0; }
            x[c] = s * g * kTalkboxMakeup * rateNorm;
        }
        double yl = lips.lp(x[0]), yr = lipsR.lp(x[1]);
        if (p.spread > 0.0)
        {
            // pseudo-stereo: a 9 ms copy, in phase on one side and out on the other
            haas.push((float)(0.5 * (yl + yr)));
            const double h = haas.at((int)(0.009 * sr)) * 0.35 * p.spread;
            yl += h; yr -= h;
        }
        outL = (float)yl; outR = (float)yr;
        if (++cnt >= hop) { cnt = 0; analyse(); }
    }

    // display: the mouth's response (band RMS, dB) at f for a band bwOct wide, and the carrier's
    float displayVoiceDb(double f, double bwOct) const
    {
        const double w = kTwoPi * f / sr;
        double re = 0.0, im = 0.0;
        for (int i = 0; i <= dispOrder; ++i) { re += aDisp[i] * std::cos(w * i); im -= aDisp[i] * std::sin(w * i); }
        const double psd = dispG * dispG / std::max(1e-30, re * re + im * im);
        const double band = psd * 2.0 * f * (std::exp2(0.5 * bwOct) - std::exp2(-0.5 * bwOct)) / sr;
        return (float)(10.0 * std::log10(band + 1e-30));
    }
    float displayCarrierDb(double f, double bwOct) const
    {
        const double binHz = sr / Na;
        int a = clampv((int)std::floor(f * std::exp2(-0.5 * bwOct) / binHz), 1, Na / 2 - 1);
        int b = clampv((int)std::ceil(f * std::exp2(0.5 * bwOct) / binHz), a, Na / 2 - 1);
        double e = 0.0;
        for (int i = a; i <= b; ++i) e += carPw[(size_t)i];
        return (float)(10.0 * std::log10(2.0 * e / ((double)Na * sumw2) + 1e-30));
    }

private:
    // autocorrelation of the newest frame of `ring` (optionally formant-warped), into r[0..ord]
    void autocorr(const std::vector<float>& ring, double shift, int ord, double* r, bool keepPower)
    {
        for (int i = 0; i < Na; ++i) frame[(size_t)i] = ring[(size_t)((wpos + i) % Na)] * win[(size_t)i];
        fft.forward(frame.data(), spec.data());
        const int K = Na / 2;
        pw[0] = (double)spec[0] * spec[0];
        pw[(size_t)K] = (double)spec[1] * spec[1];
        for (int i = 1; i < K; ++i) pw[(size_t)i] = (double)spec[(size_t)(2 * i)] * spec[(size_t)(2 * i)] + (double)spec[(size_t)(2 * i + 1)] * spec[(size_t)(2 * i + 1)];
        if (keepPower) std::copy(pw.begin(), pw.end(), carPw.begin());
        // warp: the spectrum at i / shift (a formant at f moves to f x shift)
        const double inv = 1.0 / shift;
        auto at = [&](int i) -> float {
            if (std::fabs(shift - 1.0) < 1e-6) return (float)pw[(size_t)i];
            const double s = i * inv;
            const int j = (int)s;
            if (j >= K) return (float)pw[(size_t)K] * 0.0f;
            const double t = s - j;
            return (float)(pw[(size_t)j] * (1.0 - t) + pw[(size_t)j + 1] * t);
        };
        spec[0] = at(0);
        spec[1] = at(K);
        for (int i = 1; i < K; ++i) { spec[(size_t)(2 * i)] = at(i); spec[(size_t)(2 * i + 1)] = 0.0f; }
        fft.inverse(spec.data(), acf.data());
        for (int i = 0; i <= ord; ++i) r[i] = acf[(size_t)i] / (double)Na;
    }
    // Levinson-Durbin: reflection coefficients kOut[0..ord-1] and the prediction error; aOut = the direct-form
    // polynomial (aOut[0] = 1) when given
    static double levinson(const double* r, int ord, double* kOut, double* aOut)
    {
        double a[kMaxOrder + 1] = { 1.0 }, tmp[kMaxOrder + 1];
        double E = r[0];
        if (!(E > 1e-20)) { for (int i = 0; i < ord; ++i) kOut[i] = 0.0; if (aOut) { aOut[0] = 1.0; for (int i = 1; i <= ord; ++i) aOut[i] = 0.0; } return 0.0; }
        for (int i = 1; i <= ord; ++i)
        {
            double acc = r[i];
            for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
            double kk = -acc / E;
            kk = clampv(kk, -0.9995, 0.9995);
            for (int j = 1; j < i; ++j) tmp[j] = a[j] + kk * a[i - j];
            for (int j = 1; j < i; ++j) a[j] = tmp[j];
            a[i] = kk;
            kOut[i - 1] = kk;
            E *= (1.0 - kk * kk);
        }
        if (aOut) for (int i = 0; i <= ord; ++i) aOut[i] = a[i];
        return E;
    }
    void analyse()
    {
        const int newOrder = clampv(p.order, 4, kMaxOrder);
        if (newOrder != order)
        {
            // a new order starts the extra stages from zero (they glide in)
            for (int i = order; i < newOrder; ++i) { k[i] = 0.0; kTgt[i] = 0.0; for (int c = 0; c < 2; ++c) bs[c][i + 1] = 0.0; }
            order = newOrder;
        }
        double r[kMaxOrder + 1];
        // carrier: its own prediction filter (whitening) and level
        {
            double rc[kCarOrder + 1], kk[kCarOrder];
            autocorr(ringC, 1.0, kCarOrder, rc, true);
            const double rms = std::sqrt(std::max(0.0, rc[0]) / sumw2);
            for (int i = 0; i <= kCarOrder; ++i) rc[i] *= lagWin[i];
            rc[0] *= 1.0001;
            const double E = levinson(rc, kCarOrder, kk, nullptr);
            const double sig = std::sqrt(std::max(0.0, E) / sumw2);
            for (int i = 0; i < kCarOrder; ++i) kcStep[i] = (kk[i] - kc[i]) / hop;
            if (rms >= 1e-4 && lastCarRms < 1e-4) onsetRamp = 0.0;
            lastCarRms = rms;
            cRmsStep = (std::max(rms, 1e-6) - cRms) / hop;
            cSigStep = (std::max(sig, 1e-6) - cSig) / hop;
        }
        if (p.freeze)
        {
            for (int i = 0; i < order; ++i) kStep[i] = 0.0;
            gStep = 0.0;
            return;
        }
        autocorr(ringV, p.formantShift, order, r, false);
        for (int i = 0; i <= order; ++i) r[i] *= lagWin[i];
        r[0] *= 1.0001;
        double kk[kMaxOrder], a[kMaxOrder + 1];
        const double E = levinson(r, order, kk, a);
        const double gT = std::sqrt(std::max(0.0, E) / sumw2);
        // gain: attack / release at the hop rate; the filter glides at the attack time
        const double hs = hop / sr;
        const double ac = std::exp(-hs / std::max(1e-4, p.attack)), rcf = std::exp(-hs / std::max(1e-4, p.release));
        gEnv = gT + (gT > gEnv ? ac : rcf) * (gEnv - gT);
        const double kAlpha = 1.0 - std::exp(-hs / std::max(0.002, p.attack));
        for (int i = 0; i < order; ++i)
        {
            kTgt[i] += (kk[i] - kTgt[i]) * kAlpha;
            kStep[i] = (kTgt[i] - k[i]) / hop;
        }
        gStep = (gEnv - g) / hop;
        // display polynomial (every hop is cheap enough at this order)
        dispOrder = order;
        for (int i = 0; i <= order; ++i) aDisp[i] = a[i];
        dispG = gEnv;
    }

public:
    static constexpr double kTalkboxMakeup = 1.24;
private:
    double sr = 48000.0, sumw2 = 1.0;
    int Na = 1024, hop = 240, wpos = 0, cnt = 0, order = 32, tubeLen = 250, dispOrder = 0;
    RealFFT fft;
    AlignedBuffer frame, spec, acf;
    std::vector<float> win, ringV, ringC;
    std::vector<double> pw, carPw;
    double lagWin[kMaxOrder + 1] = {};
    double k[kMaxOrder] = {}, kStep[kMaxOrder] = {}, kTgt[kMaxOrder] = {};
    double kc[kCarOrder] = {}, kcStep[kCarOrder] = {};
    double bs[2][kMaxOrder + 1] = {}, cb[2][kCarOrder + 1] = {};
    double smC = 0.002, driveCur = 6.0, tubeCur = 0.35, wsCur = 1.0, wsRise = 0.001, onsetRamp = 1.0, lastCarRms = 0.0, carEnv = 0.0, carRel = 0.999;
    double rateNorm = 1.0, g = 0.0, gStep = 0.0, gEnv = 0.0, cRms = 1e-4, cSig = 1e-4, cRmsStep = 0.0, cSigStep = 0.0;
    double aDisp[kMaxOrder + 1] = {}, dispG = 0.0;
    fx::FracDelay tube[2], haas;
    double tubeLp[2] = {};
    fx::OnePole lips, lipsR;
};

// ---- the built-in carrier --------------------------------------------------------------------------------------------
class Synth {
public:
    static constexpr int kVoices = 8, kFollowSlots = 5, kMaxUnison = 7;

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        atk = 1.0 - std::exp(-1.0 / (0.004 * sr));
        relC = std::exp(-1.0 / (0.080 * sr));
        vib.setRate(5.5, sr);
        reset();
    }
    void reset()
    {
        for (auto& v : voices) v = Voice();
        for (auto& v : follow) v = Voice();
        for (auto& h : held) h = 0;
        for (auto& s : sustained) s = false;
        numDown = 0; ctl = 0;
        sustain = false; bend = 0.0; modWheel = 0.0;
        followActive = false; followNote = -1; followMidi = 60.0; silentFor = 0.0; candidate = -100.0;
        level = 1.0;
        ageCounter = 0;
    }
    struct Params { int source = kSourceAuto, wave = kWaveSaw, octave = 0, chord = kChordOff, key = 0, scale = 1; double glideMs = 40.0, detune = 0.25; };
    Params p;

    void midi(uint8_t st, uint8_t d1, uint8_t d2)
    {
        const int type = st & 0xF0;
        if (type == 0x90 && d2 > 0) { if (p.source != kSourceFollow) noteOn(d1 & 127, d2); }
        else if (type == 0x80 || (type == 0x90 && d2 == 0)) noteOff(d1 & 127);
        else if (type == 0xE0) bend = ((((int)d2 << 7) | d1) - 8192) / 8192.0 * 2.0;
        else if (type == 0xB0)
        {
            if (d1 == 1) modWheel = d2 / 127.0;
            else if (d1 == 64) { sustain = d2 >= 64; if (!sustain) for (int n = 0; n < 128; ++n) if (sustained[n] && !held[n]) { sustained[n] = false; releaseNote(n); } recount(); }
            else if (d1 == 120 || d1 == 123) allOff();
        }
    }
    void allOff()
    {
        for (int n = 0; n < 128; ++n) { held[n] = 0; sustained[n] = false; }
        numDown = 0;
        for (auto& v : voices) if (v.on) v.gate = false;
    }
    // the voice's pitch (one tracker estimate): steers the Follow voices
    void voicePitch(const pitch::Tracker::Result& r, double hopSeconds)
    {
        if (r.voiced && r.confidence > 0.5f && r.hz > 30.0)
        {
            silentFor = 0.0;
            const double m = pitch::hzToMidi(r.hz);
            // a jump of more than three semitones (or the first note) counts once two estimates in a row agree on
            // it: a note's onset can give one stray estimate an octave or more away
            const double ref = followActive ? followMidi : -100.0;
            if (std::fabs(m - ref) > 3.0)
            {
                const bool confirmed = std::fabs(m - candidate) < 0.7;
                candidate = m;
                if (!confirmed) return;
            }
            candidate = m;
            if (p.scale <= 0) { followMidi = m; followNote = (int)std::lround(m); }
            else
            {
                const uint16_t mask = pitch::kScaleMasks[clampv(p.scale - 1, 0, pitch::kNumScales - 1)];
                followNote = pitch::nearestWithHold(m, p.key, mask, followNote, 0.2);
                followMidi = followNote;
            }
            followActive = true;
        }
        else if (r.levelDb < -55.0f)
        {
            silentFor += hopSeconds;
            if (silentFor > 0.5) followActive = false;
        }
    }
    double followMidiNote() const { return followActive ? followMidi : -1.0; }
    bool anyHeld() const { return numDown > 0; }
    // notes sounding (for the UI): MIDI voices and follow slots
    void soundingNotes(uint32_t bits[4]) const
    {
        bits[0] = bits[1] = bits[2] = bits[3] = 0;
        auto set = [&](int n) { if (n >= 0 && n < 128) bits[n >> 5] |= 1u << (n & 31); };
        for (const auto& v : voices) if (v.on && v.gate) set(v.note);
        for (const auto& v : follow) if (v.on && v.gate) set((int)std::lround(v.midi));
    }

    inline void process(float& outL, float& outR)
    {
        const bool midiMode = p.source == kSourceMidi || (p.source == kSourceAuto && anyHeld());
        const bool followMode = p.source == kSourceFollow || (p.source == kSourceAuto && !midiMode);
        // follow slots: root + chord (targets every 32 samples; the voices glide every sample)
        if (ctl-- <= 0) { ctl = 31; updateFollowTargets(followMode); }
        vib.advance();
        const double vibSt = modWheel * 0.5 * vib.sine();
        double l = 0.0, r = 0.0;
        int active = 0;
        const double glideC = tauCoef(p.glideMs * 0.001, sr);
        if (p.source == kSourceFollow) for (auto& v : voices) v.gate = false;   // Follow: MIDI notes still sounding let go
        for (auto& v : voices) if (v.on) { renderVoice(v, l, r, bend + vibSt + 12.0 * p.octave, 0.0); ++active; }
        for (auto& v : follow) if (v.on) { renderVoice(v, l, r, 12.0 * p.octave, glideC); ++active; }
        // chords and stacks: hold the level near one voice's (1 / sqrt of the voices sounding, gliding)
        const double want = 1.0 / std::sqrt((double)std::max(1, active));
        level += (want - level) * 0.0005;
        outL = (float)(l * level);
        outR = (float)(r * level);
    }

private:
    struct Voice {
        bool on = false, gate = false;
        int note = 60, age = 0;
        double midi = 60.0, target = 60.0, env = 0.0, vel = 1.0;
        double phase[kMaxUnison] = {};
        uint32_t noise = 0x12345u;
    };
    void recount() { numDown = 0; for (int n = 0; n < 128; ++n) if (held[n] || sustained[n]) ++numDown; }
    void noteOn(int n, int vel)
    {
        held[n] = (uint8_t)vel;
        sustained[n] = false;
        recount();
        // retrigger the same note's voice, else a free one, else the oldest
        Voice* v = nullptr;
        for (auto& x : voices) if (x.on && x.note == n) { v = &x; break; }
        if (!v) for (auto& x : voices) if (!x.on) { v = &x; break; }
        if (!v)
        {
            v = &voices[0];
            for (auto& x : voices) if ((!x.gate && v->gate) || (x.gate == v->gate && x.age < v->age)) v = &x;
        }
        v->noise = 0x12345u + 7919u * (uint32_t)n + 104729u * (uint32_t)ageCounter;
        if (!v->on) for (int u = 0; u < kMaxUnison; ++u) v->phase[u] = (u * 0.137 + n * 0.01) - std::floor(u * 0.137 + n * 0.01);
        v->on = true; v->gate = true; v->note = n; v->midi = v->target = n;
        v->vel = 0.6 + 0.4 * vel / 127.0;
        v->age = ++ageCounter;
    }
    void noteOff(int n)
    {
        held[n] = 0;
        if (sustain) { sustained[n] = true; recount(); return; }
        recount();
        releaseNote(n);
    }
    void releaseNote(int n) { for (auto& v : voices) if (v.on && v.note == n) v.gate = false; }

    void updateFollowTargets(bool followMode)
    {
        int notes[kFollowSlots]; int count = 0;
        if (followMode && followActive)
        {
            const bool diatonic = p.scale >= 2;   // Exact and Chromatic build chords in semitones
            const uint16_t mask = diatonic ? pitch::kScaleMasks[clampv(p.scale - 1, 0, pitch::kNumScales - 1)] : 0xFFF;
            const int root = (int)std::lround(followMidi);
            const double frac = followMidi - root;   // Exact: chord tones follow the sung pitch's detune
            auto st = [&](int steps, int semis) { return diatonic ? pitch::scaleStep(root, steps, p.key, mask) : root + semis; };
            int iv[kFollowSlots]; int c = 0;
            iv[c++] = root;
            switch (p.chord)
            {
            case kChordOctave: iv[c++] = root - 12; break;
            case kChordFifth: iv[c++] = st(4, 7); break;
            case kChordTriad: iv[c++] = st(2, 4); iv[c++] = st(4, 7); break;
            case kChordSeventh: iv[c++] = st(2, 4); iv[c++] = st(4, 7); iv[c++] = st(6, 10); break;
            case kChordSus: iv[c++] = st(3, 5); iv[c++] = st(4, 7); break;
            case kChordStack: iv[c++] = root - 12; iv[c++] = st(4, 7); iv[c++] = root + 12; break;
            default: break;
            }
            for (int i = 0; i < c; ++i) notes[count++] = iv[i];
            for (int s = 0; s < kFollowSlots; ++s)
            {
                Voice& v = follow[s];
                if (s < count)
                {
                    const double t = notes[s] + (p.scale <= 0 ? frac : 0.0);
                    if (!v.on) { v = Voice(); v.noise = 0xACE1u + 7919u * (uint32_t)(s + 1); v.on = true; v.midi = t; for (int u = 0; u < kMaxUnison; ++u) v.phase[u] = u * 0.173 - std::floor(u * 0.173); }
                    v.gate = true; v.target = t; v.note = notes[s];
                }
                else v.gate = false;
            }
        }
        else for (auto& v : follow) v.gate = false;
    }

    // band-limited step correction (polyBLEP) at phase t with increment dt
    static inline double blep(double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    inline void renderVoice(Voice& v, double& l, double& r, double offsetSt, double glideC)
    {
        // envelope: fast attack, 80 ms release
        if (v.gate) v.env += (1.0 - v.env) * atk;
        else { v.env *= relC; if (v.env < 1e-4) { v.on = false; v.env = 0.0; return; } }
        if (glideC > 0.0) v.midi = v.target + (v.midi - v.target) * glideC;
        else v.midi = v.target;
        const double f0 = pitch::midiToHz(v.midi + offsetSt);
        const double amp = v.env * v.vel;
        if (p.wave == kWaveNoise)
        {
            // white noise (whisper carrier), one stream per side
            v.noise ^= v.noise << 13; v.noise ^= v.noise >> 17; v.noise ^= v.noise << 5;
            const double a = ((v.noise >> 8) * (1.0 / 8388608.0) - 1.0);
            v.noise ^= v.noise << 13; v.noise ^= v.noise >> 17; v.noise ^= v.noise << 5;
            const double b = ((v.noise >> 8) * (1.0 / 8388608.0) - 1.0);
            l += a * amp * 0.45; r += b * amp * 0.45;
            return;
        }
        const int U = p.wave == kWaveSuper ? 7 : (p.detune > 0.001 ? 3 : 1);
        static const double sup[7] = { 0.0, -1.0, 1.0, -0.62, 0.62, -0.27, 0.27 };
        static const double tri3[3] = { 0.0, -1.0, 1.0 };
        const double cents = p.wave == kWaveSuper ? 10.0 + 40.0 * p.detune : 25.0 * p.detune;
        const double g = 0.45 / std::sqrt((double)U);
        const double width = clampv(p.detune * 1.6, 0.0, 0.8);
        for (int u = 0; u < U; ++u)
        {
            const double off = U == 7 ? sup[u] : (U == 3 ? tri3[u] : 0.0);
            const double f = f0 * std::exp2(off * cents / 1200.0);
            const double dt = clampv(f / sr, 1e-6, 0.45);
            double& ph = v.phase[u];
            double y;
            switch (p.wave)
            {
            case kWaveSquare:
            {
                double t2 = ph + 0.5; t2 -= std::floor(t2);
                y = (ph < 0.5 ? 1.0 : -1.0) + blep(ph, dt) - blep(t2, dt);
                y *= 0.8;
                break;
            }
            case kWaveBuzz:
            {
                // narrow pulse (12.5 %): two band-limited saws a duty apart
                double t2 = ph + 0.875; t2 -= std::floor(t2);
                // (s2 - s1 is +1.75 for an eighth of the cycle and -0.25 otherwise: no DC)
                const double s1 = 2.0 * ph - 1.0 - blep(ph, dt);
                const double s2 = 2.0 * t2 - 1.0 - blep(t2, dt);
                y = (s2 - s1) * 0.75;
                break;
            }
            default: y = 2.0 * ph - 1.0 - blep(ph, dt); break;   // saw and super
            }
            ph += dt; if (ph >= 1.0) ph -= 1.0;
            const double pan = off * width;
            l += y * amp * g * std::sqrt(1.0 - pan);
            r += y * amp * g * std::sqrt(1.0 + pan);
        }
    }

    double sr = 48000.0, atk = 0.01, relC = 0.999, level = 1.0;
    Voice voices[kVoices], follow[kFollowSlots];
    uint8_t held[128] = {};
    bool sustained[128] = {}, sustain = false;
    double bend = 0.0, modWheel = 0.0;
    fx::Lfo vib;
    bool followActive = false;
    int followNote = -1, ageCounter = 0;
    double followMidi = 60.0, silentFor = 0.0, candidate = -100.0;
    int numDown = 0, ctl = 0;
};

// ---- ensemble ---------------------------------------------------------------------------------------------------------
class Ensemble {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        for (auto& d : line) { d.setMax((int)(0.03 * sr) + 8); d.clear(); }
        static const double rates[3] = { 0.55, 0.83, 1.17 };
        for (int c = 0; c < 2; ++c) for (int t = 0; t < 3; ++t) { lfo[c][t].setRate(rates[t], sr); lfo[c][t].reset(t / 3.0 + c * 0.5); }
    }
    void reset() { for (auto& d : line) d.clear(); }
    inline void process(float& l, float& r, double amount)
    {
        float* io[2] = { &l, &r };
        static const double base[3] = { 0.0062, 0.0095, 0.0131 }, depth[3] = { 0.0012, 0.0018, 0.0024 };
        for (int c = 0; c < 2; ++c)
        {
            line[c].push(*io[c]);
            double s = 0.0;
            for (int t = 0; t < 3; ++t)
            {
                lfo[c][t].advance();
                s += line[c].read((base[t] + depth[t] * lfo[c][t].sine()) * sr);
            }
            *io[c] = (float)(*io[c] * (1.0 - 0.35 * amount) + amount * 0.62 * s / 3.0 * 1.7);
        }
    }
private:
    double sr = 48000.0;
    fx::FracDelay line[2];
    fx::Lfo lfo[2][3];
};

// ---- the engine --------------------------------------------------------------------------------------------------------
class Engine {
public:
    Engine()
    {
        for (int i = 0; i < kDispArr; ++i) { dispHz[i].store(0.0f); dispVoice[i].store(-120.0f); dispCarrier[i].store(-120.0f); }
        for (auto& b : notes) b.store(0u);
    }
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        rateMul = 1;
        while (sr / rateMul > 66000.0 && rateMul < 4) rateMul *= 2;
        int sizes[kNumFft];
        for (int i = 0; i < kNumFft; ++i) sizes[i] = (512 << i) * rateMul;
        spec.prepare(sr, sizes, kNumFft);
        classic.prepare(sr);
        talk.prepare(sr);
        synth.prepare(sr);
        ens.prepare(sr);
        tracker.prepare(sr, 60.0, 1000.0, 12000.0);
        const int maxLat = sizes[kNumFft - 1] + 8;
        for (auto* d : { &dlyVoice, &dlyCarL, &dlyCarR, &dlySib }) { d->setMaxDelay(maxLat); d->clear(); }
        emph.reset(); sibHp[0] = sibHp[1] = BqState();
        designHighpass(sibCoef, std::min(5000.f, 0.4f * (float)sr), 0.707f, (float)sr);
        hfHp.set(3500.0, 0.707, sr);
        noise.seed(0xC0FFEEu); noise2.seed(0xBADCAFEu);
        gateAtk = tauCoef(0.001, sr); gateRel = tauCoef(0.080, sr);
        smC = 1.0 - tauCoef(0.010, sr);
        smFresh = true;
        gOpen = tauCoef(0.002, sr); gClose = tauCoef(0.060, sr);
        envC = tauCoef(0.005, sr);
        uAtk = tauCoef(0.003, sr); uRel = tauCoef(0.030, sr);
        active = pending;
        applyStructure();
        fade = 1.0; fadeDir = 0;
        dirty = true;
        reset();
    }
    void reset()
    {
        classic.reset(); spec.reset(); talk.reset(); synth.reset(); ens.reset(); tracker.reset();
        for (auto* d : { &dlyVoice, &dlyCarL, &dlyCarR, &dlySib }) d->clear();
        emph.reset(); hfHp.reset(); sibHp[0] = sibHp[1] = BqState();
        gateEnv = 0.0; gateGain = 1.0; hfEnv = fullEnv = 0.0; unvoiced = 0.0; uTarget = 0.0;
        carrierEnv = 0.0; noCarrierFor = 0.0;
        dispCounter = 0;
    }
    void setSettings(const Settings& s)
    {
        pending = s;
        const bool fftUsed = s.mode == kSpectral || s.mode == kConvolve;
        if (s.mode != active.mode || s.route != active.route || (fftUsed && s.fftIndex != active.fftIndex))
        {
            if (fadeDir == 0 || fadeDir == 1) fadeDir = -1;   // fade out, switch, fade in
        }
        dirty = true;
    }
    int latency() const { return curLatencyPub.load(std::memory_order_relaxed); }
    int maxLatencySamples() const { return (512 << (kNumFft - 1)) * rateMul; }
    int fftSizeNow() const { return spec.size(); }
    void setSolo(double loHz, double hiHz) { soloLo.store((float)loHz); soloHi.store((float)hiHz); }

    // inL/inR: the track; scL/scR: the sidechain (null = silence). Any of the buffers may alias.
    void process(const float* inL, const float* inR, const float* scL, const float* scR, float* outL, float* outR, int n,
                 const MidiMsg* events = nullptr, int numEvents = 0)
    {
        int ev = 0;
        for (int i = 0; i < n; ++i)
        {
            while (ev < numEvents && (int)events[ev].frame <= i) { synth.midi(events[ev].status, events[ev].d1, events[ev].d2); ++ev; }
            if (dirty) applySettings();
            float l, r;
            processSample(inL[i], inR[i], scL ? scL[i] : 0.0f, scR ? scR[i] : 0.0f, l, r);
            outL[i] = l; outR[i] = r;
        }
        while (ev < numEvents) { synth.midi(events[ev].status, events[ev].d1, events[ev].d2); ++ev; }
        // a non-finite value got into the state (a filter or lattice blew up): the rest of this block is silenced
        // above; clear every stage so it can't stay latched
        if (blown) { blown = false; reset(); }
    }

    // ---- feeds for the UI ----
    std::atomic<int> dispCount { 0 };
    std::atomic<float> dispHz[kDispArr], dispVoice[kDispArr], dispCarrier[kDispArr];
    std::atomic<float> pitchHz { 0.0f }, pitchConf { 0.0f }, followNote { -1.0f }, unvoicedNow { 0.0f };
    std::atomic<bool> voicedNow { false }, carrierMissing { false };
    std::atomic<uint32_t> notes[4];
    MeterValue voicePeak, carrierPeak, outPeak[2];

    // for the tests
    const pitch::Tracker& pitchTracker() const { return tracker; }
    double followMidi() const { return synth.followMidiNote(); }
    const Settings& activeSettings() const { return active; }
    bool switching() const { return fadeDir != 0; }

private:
    bool blown = false;   // a non-finite output this block

    void applyStructure()
    {
        // what a mode / route / FFT size change rebuilds (called while silent)
        spec.configure((512 << clampv(active.fftIndex, 0, kNumFft - 1)) * rateMul);
        curLatency = (active.mode == kSpectral || active.mode == kConvolve) ? spec.latency() : 0;
        curLatencyPub.store(curLatency, std::memory_order_relaxed);
        for (auto* d : { &dlyVoice, &dlyCarL, &dlyCarR, &dlySib }) { d->setDelay(curLatency); d->clear(); }
        classic.reset(); talk.reset(); spec.reset(); ens.reset();
    }
    void applySettings()
    {
        dirty = false;
        // the structural part waits for the fade; everything else applies now
        const Settings s = pending;
        const int mode = active.mode, route = active.route, fftIndex = active.fftIndex;
        active = s;
        active.mode = mode; active.route = route; active.fftIndex = fftIndex;
        const int nb = kBandCounts[clampv(active.bandsIndex, 0, kNumBandChoices - 1)];
        classic.design(nb, active.lowHz, active.highHz, active.width, active.formant, active.attackMs, active.releaseMs, active.spread);
        classic.setFlatten(active.flatten);
        const double shift = std::exp2(active.formant / 12.0);
        spec.p.convolve = mode == kConvolve;
        spec.p.freeze = active.freeze;
        spec.p.widthOct = 0.5 * std::pow(0.04, clampv(active.detail * 0.01, 0.0, 1.0));
        if (mode == kConvolve) spec.p.widthOct = 0.5;   // Convolve: DETAIL blends the raw magnitude with this envelope
        spec.p.detail = clampv(active.detail * 0.01, 0.0, 1.0);
        spec.p.formantShift = shift;
        spec.p.attack = active.attackMs * 0.001;
        spec.p.release = active.releaseMs * 0.001;
        spec.p.flatten = clampv(active.flatten * 0.01, 0.0, 1.0);
        spec.p.smear = clampv(active.smear * 0.01, 0.0, 1.0);
        spec.p.spread = clampv(active.spread * 0.01, 0.0, 1.0);
        const int baseOrder = 12 + (int)std::lround(clampv(active.detail, 0.0, 100.0) * 0.44);
        talk.p.order = clampv((int)std::lround(baseOrder * std::max(1.0, sr / 48000.0)), 8, Talkbox::kMaxOrder);
        talk.p.formantShift = shift;
        talk.p.tube = clampv(active.tube * 0.01, 0.0, 1.0);
        talk.p.driveDb = clampv(active.driveDb, 0.0, 24.0);
        talk.p.flatten = clampv(active.flatten * 0.01, 0.0, 1.0);
        talk.p.attack = active.attackMs * 0.001;
        talk.p.release = active.releaseMs * 0.001;
        talk.p.spread = clampv(active.spread * 0.01, 0.0, 1.0);
        talk.p.freeze = active.freeze;
        synth.p.source = active.source; synth.p.wave = active.wave; synth.p.octave = active.octave; synth.p.chord = active.chord;
        synth.p.key = clampv(active.key, 0, 11); synth.p.scale = clampv(active.scale, 0, kNumScaleChoices - 1);
        synth.p.glideMs = clampv(active.glideMs, 0.0, 2000.0); synth.p.detune = clampv(active.detune * 0.01, 0.0, 1.0);
        emph.set(2000.0, 0.6, sr);
        tgt[kSmEmph] = dbToGain(12.0 * clampv(active.emphasis * 0.01, 0.0, 1.0));
        gateThr = active.gateDb <= kGateOff + 0.05 ? -1.0 : dbToGain(active.gateDb);
        tgt[kSmSib] = clampv(active.sibilance * 0.01, 0.0, 1.0);
        noiseAmt = clampv(active.noise * 0.01, 0.0, 1.0);
        tgt[kSmEns] = clampv(active.ensemble * 0.01, 0.0, 1.0);
        tgt[kSmVoice] = clampv(active.voiceDry * 0.01, 0.0, 1.0);
        tgt[kSmCar] = clampv(active.carrierDry * 0.01, 0.0, 1.0);
        tgt[kSmOut] = dbToGain(clampv(active.outDb, -60.0, 24.0));
        // automation glides (~10 ms) instead of stepping at block edges; a fresh start takes the values at once
        if (smFresh) { for (int i = 0; i < kNumSm; ++i) sm[i] = tgt[i]; smFresh = false; }
    }

    inline void processSample(float inL, float inR, float scL, float scR, float& outL, float& outR)
    {
        // a switch: fade out, rebuild, fade back in (10 ms each way)
        if (fadeDir != 0)
        {
            fade += fadeDir * (1.0 / (0.010 * sr));
            if (fade <= 0.0)
            {
                fade = 0.0; fadeDir = 1;
                const Settings s = pending;
                active.mode = s.mode; active.route = s.route; active.fftIndex = s.fftIndex;
                applyStructure();
                dirty = true;
                applySettings();
            }
            else if (fade >= 1.0) { fade = 1.0; fadeDir = 0; }
        }
        // ---- routing ----
        if (!(std::fabs(inL) < 1e6f)) inL = 0.0f;
        if (!(std::fabs(inR) < 1e6f)) inR = 0.0f;
        if (!(std::fabs(scL) < 1e6f)) scL = 0.0f;
        if (!(std::fabs(scR) < 1e6f)) scR = 0.0f;
        float v, cl, cr;
        switch (active.route)
        {
        case kRouteSidechain: v = 0.5f * (inL + inR); cl = scL; cr = scR; break;
        case kRouteSidechainVoice: v = 0.5f * (scL + scR); cl = inL; cr = inR; break;
        case kRouteSplit: v = inR; cl = cr = inL; break;
        default: v = 0.5f * (inL + inR); synth.process(cl, cr); break;
        }
        // ---- the voice: gate, pitch, unvoiced detector, emphasis ----
        const double av = std::fabs((double)v);
        gateEnv = av > gateEnv ? av + gateAtk * (gateEnv - av) : av + gateRel * (gateEnv - av);
        double vg = v;
        if (gateThr > 0.0)
        {
            const double want = gateEnv > gateThr ? 1.0 : 0.0;
            gateGain = want + (want > gateGain ? gOpen : gClose) * (gateGain - want);
            vg = v * gateGain;
        }
        if (tracker.push((float)vg))
        {
            const pitch::Tracker::Result& pr = tracker.result();
            synth.voicePitch(pr, tracker.hopSamples() / sr);
            voicedConf = pr.voiced ? pr.confidence : 0.0f;
            levelDb = pr.levelDb;
        }
        const double hf = hfHp.highpass(vg);
        hfEnv = std::fabs(hf) + envC * (hfEnv - std::fabs(hf));
        fullEnv = std::fabs(vg) + envC * (fullEnv - std::fabs(vg));
        {
            const double ratio = hfEnv / (fullEnv + 1e-9);
            double u = clampv((ratio - 0.12) / 0.30, 0.0, 1.0) * (1.0 - 0.85 * voicedConf);
            if (fullEnv < 1e-3) u = 0.0;   // below about -60 dBFS: not a consonant, just the room
            uTarget = u;
            unvoiced = uTarget + (uTarget > unvoiced ? uAtk : uRel) * (unvoiced - uTarget);
        }
        for (int i = 0; i < kNumSm; ++i) sm[i] += (tgt[i] - sm[i]) * smC;
        const double emphGain = sm[kSmEmph], sibAmt = sm[kSmSib], ensAmt = sm[kSmEns], voiceDry = sm[kSmVoice], carDry = sm[kSmCar], outGain = sm[kSmOut];
        const double va = emphGain > 1.0001 ? emph.highShelf(vg, emphGain) : vg;
        // ---- the carrier: dry copy, levels, unvoiced noise ----
        const float dryCl = cl, dryCr = cr;
        const double ac = 0.5 * (std::fabs((double)cl) + std::fabs((double)cr));
        carrierEnv = ac + envC * (carrierEnv - ac);
        if (noiseAmt > 0.0 && unvoiced > 1e-4)
        {
            const double k = 0.5 * noiseAmt * unvoiced;
            cl += (float)(noise.bi() * k);
            cr += (float)(noise2.bi() * k);
        }
        // ---- the mode ----
        float wl = 0.0f, wr = 0.0f;
        switch (active.mode)
        {
        case kSpectral:
        case kConvolve: spec.process((float)va, cl, cr, wl, wr); break;
        case kTalkbox: talk.process((float)va, cl, cr, wl, wr); break;
        default: classic.process((float)va, cl, cr, active.freeze, active.route != kRouteSplit, wl, wr); break;
        }
        // ---- after: ensemble, sibilance, dry paths ----
        if (ensAmt > 1e-4) ens.process(wl, wr, ensAmt);
        float sib = bqRun(sibCoef, sibHp[0], (float)vg);
        sib = bqRun(sibCoef, sibHp[1], sib) * (float)sibAmt;
        sib = dlySib.process(sib);
        const float dv = dlyVoice.process(v) * (float)voiceDry;
        const float dcl = dlyCarL.process(dryCl) * (float)carDry, dcr = dlyCarR.process(dryCr) * (float)carDry;
        double l = (wl + sib + dv + dcl) * outGain * fade, r = (wr + sib + dv + dcr) * outGain * fade;
        // safety: a soft ceiling far above normal levels (bends at +9.5 dBFS, never past +12), so a runaway setting
        // can't explode
        // (NaN fails every comparison: it is caught here and becomes silence, and process() then resets the state)
        if (!(std::fabs(l) <= 3.0)) l = std::isfinite(l) ? (l > 0 ? 1.0 : -1.0) * (3.0 + fastTanh((float)(std::fabs(l) - 3.0))) : (blown = true, 0.0);
        if (!(std::fabs(r) <= 3.0)) r = std::isfinite(r) ? (r > 0 ? 1.0 : -1.0) * (3.0 + fastTanh((float)(std::fabs(r) - 3.0))) : (blown = true, 0.0);
        outL = (float)l; outR = (float)r;
        // ---- feeds ----
        voicePeak.setMax((float)std::fabs(v));
        carrierPeak.setMax((float)std::max(std::fabs(dryCl), std::fabs(dryCr)));
        outPeak[0].setMax((float)std::fabs(l)); outPeak[1].setMax((float)std::fabs(r));
        if (++dispCounter >= 512) { dispCounter = 0; publish(); }
    }

    void publish()
    {
        const float lo = soloLo.load(std::memory_order_relaxed), hi = soloHi.load(std::memory_order_relaxed);
        classic.setSolo(lo, hi);
        spec.p.soloLo = lo; spec.p.soloHi = hi;
        if (active.mode == kClassic)
        {
            const int nb = classic.bands();
            for (int b = 0; b < nb; ++b)
            {
                dispHz[b].store(classic.centreHz(b), std::memory_order_relaxed);
                // band RMS of a rectified-mean envelope: x 1.11
                dispVoice[b].store(gainToDbF(classic.bandLevel(b) * 1.11f + 1e-9f), std::memory_order_relaxed);
                dispCarrier[b].store(gainToDbF(classic.carrierLevel(b) * 1.11f + 1e-9f), std::memory_order_relaxed);
            }
            dispCount.store(nb, std::memory_order_relaxed);
        }
        else
        {
            const double fLo = 50.0, fHi = std::min(16000.0, 0.45 * sr);
            const double bw = std::log2(fHi / fLo) / kDispMax;
            for (int b = 0; b < kDispMax; ++b)
            {
                const double f = fLo * std::exp2((b + 0.5) * bw);
                dispHz[b].store((float)f, std::memory_order_relaxed);
                const bool tb = active.mode == kTalkbox;
                dispVoice[b].store(tb ? talk.displayVoiceDb(f, bw) : spec.displayVoiceDb(f, bw), std::memory_order_relaxed);
                dispCarrier[b].store(tb ? talk.displayCarrierDb(f, bw) : spec.displayCarrierDb(f, bw), std::memory_order_relaxed);
            }
            dispCount.store(kDispMax, std::memory_order_relaxed);
        }
        const pitch::Tracker::Result& pr = tracker.result();
        pitchHz.store(pr.voiced ? (float)pr.hz : 0.0f, std::memory_order_relaxed);
        pitchConf.store(pr.confidence, std::memory_order_relaxed);
        voicedNow.store(pr.voiced, std::memory_order_relaxed);
        followNote.store(active.route == kRouteSynth ? (float)synth.followMidiNote() : -1.0f, std::memory_order_relaxed);
        unvoicedNow.store((float)unvoiced, std::memory_order_relaxed);
        uint32_t bits[4] = { 0, 0, 0, 0 };
        if (active.route == kRouteSynth) synth.soundingNotes(bits);
        for (int i = 0; i < 4; ++i) notes[i].store(bits[i], std::memory_order_relaxed);
        // no carrier while the voice is there (a sidechain that isn't connected)
        const double dt = 512.0 / sr;
        if (active.route != kRouteSynth && carrierEnv < 3e-5 && fullEnv > 3e-3) noCarrierFor += dt;
        else if (carrierEnv >= 3e-5) noCarrierFor = 0.0;
        carrierMissing.store(noCarrierFor > 0.6, std::memory_order_relaxed);
    }

    double sr = 48000.0;
    int rateMul = 1, curLatency = 0, dispCounter = 0;
    std::atomic<int> curLatencyPub { 0 };
    Settings pending, active;
    bool dirty = true;
    double fade = 1.0; int fadeDir = 0;
    Classic classic;
    SpectralProc spec;
    Talkbox talk;
    Synth synth;
    Ensemble ens;
    pitch::Tracker tracker;
    DelayLine dlyVoice, dlyCarL, dlyCarR, dlySib;
    fx::Svf emph, hfHp;
    Bq sibCoef; BqState sibHp[2];
    fx::Rng noise, noise2;
    double gateThr = -1.0, noiseAmt = 0.3;
    enum { kSmEmph = 0, kSmSib, kSmEns, kSmVoice, kSmCar, kSmOut, kNumSm };
    double tgt[kNumSm] = { 1, 0.3, 0, 0, 0, 1 }, sm[kNumSm] = { 1, 0.3, 0, 0, 0, 1 }, smC = 0.002;
    bool smFresh = true;
    double gateEnv = 0.0, gateGain = 1.0, gateAtk = 0.0, gateRel = 0.0, gOpen = 0.0, gClose = 0.0;
    double hfEnv = 0.0, fullEnv = 0.0, envC = 0.0, unvoiced = 0.0, uTarget = 0.0, uAtk = 0.0, uRel = 0.0;
    double carrierEnv = 0.0, noCarrierFor = 0.0;
    float voicedConf = 0.0f, levelDb = -120.0f;
    std::atomic<float> soloLo { 0.0f }, soloHi { 0.0f };
};

}} // namespace pump::vox
