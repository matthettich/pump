// Calibration tool for pump MASTER (not built with the plugins). For each reference pair it takes the unmastered
// song (raw float32 stereo, interleaved) and the measured tone change of the reference master (31 third-octave
// values in dB, mid channel), learns the song with the real engine, then searches the Balanced style's tone
// parameters for the smallest shape error over all songs (each song's constant offset removed: level is the
// loudness stage's job). Prints the best row and the per-song errors, plus leave-one-out errors.
//   c++ -O2 -std=c++17 -Icommon -Iplugins/master tools/master_calibrate.cpp build/tests/pffft.o -o build/master_calibrate
//   build/master_calibrate 44100 song1.f32 song1_H.txt song2.f32 song2_H.txt ...
#include "../plugins/master/MasterEngine.hpp"
#include <cstdio>
#include <vector>
#include <string>
#include <cstdlib>

using namespace pump; using namespace pump::master;
static double kSmooth = std::getenv("SMOOTH") ? std::atof(std::getenv("SMOOTH")) : 0.05;

struct Song { std::string name; Analysis a; double H[kBands]; };

static double curveAt(const Engine& e, double f)
{
    const double x = std::log(f / 20.0) / std::log(1000.0) * (kCurvePts - 1);
    const int i = clampv((int)x, 0, kCurvePts - 2); const double t = clampv(x - i, 0.0, 1.0);
    return e.uiCurve[i].load() * (1 - t) + e.uiCurve[i + 1].load() * t;
}
static double bandW(double f) { return f < 30.0 || f > 17000.0 ? 0.0 : (f < 45.0 || f > 13000.0 ? 0.5 : 1.0); }

// rms shape error of one song (dB), best constant offset removed
static double songErr(Engine& e, const Song& s, const StyleDef& st)
{
    e.styleOverride = &st; e.setAnalysis(s.a);
    double p[kBands], sw = 0, so = 0;
    for (int b = 0; b < kBands; ++b) { p[b] = curveAt(e, bandFreq(b)); const double w = bandW(bandFreq(b)); sw += w; so += w * (s.H[b] - p[b]); }
    const double off = so / sw; double se = 0;
    for (int b = 0; b < kBands; ++b) { const double d = s.H[b] - p[b] - off; se += bandW(bandFreq(b)) * d * d; }
    return std::sqrt(se / sw);
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: master_calibrate rate song.f32 song_H.txt ...\n"); return 1; }
    const double sr = std::atof(argv[1]);
    std::vector<Song> songs;
    Engine e; Settings set; set.loudOn = false; set.dynOn = false; set.stereoOn = false;
    e.prepare(sr); e.set(set);
    for (int i = 2; i + 1 < argc; i += 2)
    {
        FILE* f = std::fopen(argv[i], "rb"); if (!f) { std::printf("can't read %s\n", argv[i]); return 1; }
        std::fseek(f, 0, SEEK_END); const size_t n = (size_t)std::ftell(f) / 8; std::fseek(f, 0, SEEK_SET);
        std::vector<float> v(2 * n); if (std::fread(v.data(), 8, n, f) != n) return 1; std::fclose(f);
        std::vector<float> L(n), R(n); for (size_t k = 0; k < n; ++k) { L[k] = v[2 * k]; R[k] = v[2 * k + 1]; }
        Song s; s.name = argv[i];
        e.learnBuffer(L.data(), R.data(), (int)n); s.a = e.analysis();
        FILE* h = std::fopen(argv[i + 1], "r"); for (int b = 0; b < kBands; ++b) if (std::fscanf(h, "%lf", &s.H[b]) != 1) return 1; std::fclose(h);
        songs.push_back(s);
        std::printf("learned %s: %.1f LUFS\n", argv[i], s.a.lufs);
    }
    const int N = (int)songs.size();
    StyleDef cur = styleDef(kBalanced);
    auto report = [&](const char* label, const StyleDef& st) {
        double tot = 0; std::printf("%s:", label);
        for (auto& s : songs) { const double er = songErr(e, s, st); tot += er * er; std::printf(" %.2f", er); }
        std::printf("  | rms %.3f\n", std::sqrt(tot / N));
    };
    report("current Balanced", cur);
    // the floor: the measured curves fitted straight onto the nine tone filters (what any target could reach at best)
    {
        double A[kBands][kEq];
        for (int i = 0; i < kEq; ++i)
        {
            DigitalBand d; d.design(analogPrototype(eqSpec(i, 1.0), sr), sr);
            for (int b = 0; b < kBands; ++b) A[b][i] = 10.0 * std::log10(std::max(1e-30, d.magSq(kTwoPi * std::min(bandFreq(b), 0.49 * sr) / sr)));
        }
        double tot = 0; std::printf("9-filter floor:");
        for (auto& s : songs)
        {
            double w[kBands], c[kBands], g[kEq], sw = 0, so = 0;
            for (int b = 0; b < kBands; ++b) { w[b] = bandW(bandFreq(b)); c[b] = s.H[b]; }
            leastSquares(A, w, c, 1e-4, g);
            double p[kBands]; for (int b = 0; b < kBands; ++b) { p[b] = 0; for (int i = 0; i < kEq; ++i) p[b] += A[b][i] * g[i]; sw += w[b]; so += w[b] * (c[b] - p[b]); }
            double se = 0; for (int b = 0; b < kBands; ++b) { const double dd = c[b] - p[b] - so / sw; se += w[b] * dd * dd; }
            const double r = std::sqrt(se / sw); tot += r * r; std::printf(" %.2f", r);
        }
        std::printf("  | rms %.3f\n", std::sqrt(tot / N));
    }
    if (std::getenv("DUMP"))
    {
        for (auto& s : songs)
        {
            e.styleOverride = &cur; e.setAnalysis(s.a);
            std::printf("%s\n  f      meas   pred   spec\n", s.name.c_str());
            for (int b = 3; b < kBands - 1; ++b) std::printf("  %6.0f %6.2f %6.2f %6.1f\n", bandFreq(b), s.H[b], curveAt(e, bandFreq(b)), e.plan().measured[b]);
        }
    }
    // coordinate search (repeated sweeps over each parameter): the base target table (smoothness-penalised) and the
    // style's strength, largest cut and low-end share; tilt/body/air/sub stay as the style has them
    struct Fit { StyleDef st; float t[kBands]; };
    auto cost = [&](const Fit& d, int skip) {
        e.targetOverride = d.t;
        double c = 0; int m = 0;
        for (int k = 0; k < N; ++k) if (k != skip) { const double r = songErr(e, songs[k], d.st); c += r * r; ++m; }
        double sm = 0;
        for (int b = 1; b + 1 < kBands; ++b) { const double dd = d.t[b - 1] - 2 * d.t[b] + d.t[b + 1]; sm += dd * dd; }
        e.targetOverride = nullptr;
        return c / m + kSmooth * sm;
    };
    auto errOf = [&](const Fit& d, int k) { e.targetOverride = d.t; const double r = songErr(e, songs[k], d.st); e.targetOverride = nullptr; return r; };
    auto search = [&](int skip, Fit f) {
        std::vector<float*> P; std::vector<float> lo, hi, stepv;
        auto add = [&](float* p, float l, float h, float s) { P.push_back(p); lo.push_back(l); hi.push_back(h); stepv.push_back(s); };
        add(&f.st.strength, 0.1f, 1.0f, 0.05f); add(&f.st.toneMax, 1.0f, 10.0f, 0.5f); add(&f.st.lowStr, 0.0f, 1.0f, 0.1f);
        for (int b = 0; b < kBands; ++b) add(&f.t[b], -30.0f, 30.0f, 1.0f);
        double best = cost(f, skip);
        for (int round = 0; round < 6; ++round)
        {
            for (int it = 0; it < 20; ++it)
            {
                bool moved = false;
                for (size_t k = 0; k < P.size(); ++k)
                    for (int dir = -1; dir <= 1; dir += 2)
                    {
                        const float old = *P[k]; *P[k] = clampv(old + dir * stepv[k], lo[k], hi[k]);
                        const double c = cost(f, skip);
                        if (c < best - 1e-9) { best = c; moved = true; } else *P[k] = old;
                    }
                if (!moved) break;
            }
            for (auto& s : stepv) s *= 0.5f;
        }
        return f;
    };
    Fit start; start.st = cur; start.st.tilt = start.st.body = start.st.air = start.st.sub = 0.0f;
    for (int b = 0; b < kBands; ++b) start.t[b] = baseTargetTable()[b];
    // the current model: the table holds the old Balanced curve with its tilt/body/air/sub folded in
    { double tot = 0; std::printf("start:"); for (int k = 0; k < N; ++k) { const double r = errOf(start, k); tot += r * r; std::printf(" %.2f", r); } std::printf("  | rms %.3f\n", std::sqrt(tot / N)); }
    const Fit fb = search(-1, start);
    { double tot = 0; std::printf("fitted:"); for (int k = 0; k < N; ++k) { const double r = errOf(fb, k); tot += r * r; std::printf(" %.2f", r); } std::printf("  | rms %.3f\n", std::sqrt(tot / N)); }
    std::printf("strength %.3f toneMax %.2f lowStr %.2f\ntable:", fb.st.strength, fb.st.toneMax, fb.st.lowStr);
    for (int b = 0; b < kBands; ++b) std::printf(" %.2ff,", fb.t[b]);
    std::printf("\nleave-one-out (error on the song left out; start model's error):\n");
    for (int k = 0; k < N; ++k)
    {
        const Fit d = search(k, start);
        std::printf("  %-40s %.2f (start %.2f)\n", songs[k].name.c_str(), errOf(d, k), errOf(start, k));
    }
    return 0;
}
