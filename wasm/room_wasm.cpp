// pump ROOM: the measurement DSP (common/dsp/Room.hpp) behind a small C interface for the browser app (room/).
// Built by room/build.sh into pump-room.wasm. No threads, no exceptions; the app passes audio and text through
// buffers it allocates here (room_alloc / room_release_buf).
#include "dsp/RoomCorrect.hpp"
#include <cstdlib>

using namespace pump;
using namespace pump::room;

namespace {
constexpr int kSlots = 256;
Measurement* slots[kSlots] = {};
SweepSpec spec;
std::vector<float> sweep;
CalCurve cal;
std::vector<double> grid;
std::vector<float> out;

Measurement* slot(int id) { return (id >= 0 && id < kSlots) ? slots[id] : nullptr; }
int store(Measurement* m)
{
    for (int i = 0; i < kSlots; ++i)
        if (slots[i] == nullptr) { slots[i] = m; return i; }
    delete m;
    return -1;
}
}

extern "C" {

void* room_alloc(int bytes) { return bytes > 0 ? std::malloc((size_t)bytes) : nullptr; }
void room_release_buf(void* p) { std::free(p); }

// Make the sweep; returns its length in samples (read it with room_sweep_ptr).
int room_sweep(double fs, double f1, double f2, double seconds, double levelDb)
{
    spec = SweepSpec();
    spec.sampleRate = fs; spec.f1 = f1; spec.f2 = f2; spec.seconds = seconds; spec.levelDb = levelDb;
    sweep = makeSweep(spec);
    return (int)sweep.size();
}
const float* room_sweep_ptr() { return sweep.data(); }
double room_sweep_band(int high) { return high ? spec.validHigh() : spec.validLow(); }

// A recording from the moment the current sweep started, n samples: returns a measurement id, or -1 when no direct
// sound was found (silence), -2 when the input was too short.
int room_analyze(const float* rec, int n, int searchSamples)
{
    if (rec == nullptr || sweep.empty() || n <= (int)sweep.size()) return -2;
    Measurement* m = new Measurement();
    if (!m->analyse(rec, n, sweep, spec, searchSamples)) { delete m; return -1; }
    return store(m);
}

// A saved response read back: samples, rate, the direct sound's position in them.
int room_load(const float* ir, int n, double fs, double arrival)
{
    Measurement* m = new Measurement();
    if (!m->load(ir, n, fs, arrival)) { delete m; return -1; }
    return store(m);
}

void room_free(int id)
{
    if (Measurement* m = slot(id)) { delete m; slots[id] = nullptr; }
}

// 0 arrival (samples), 1 polarity, 2 largest peak (dB), 3 sweep RMS at the mic (dBFS), 4 recording peak (dBFS),
// 5 response length (samples), 6 sample rate, 7 direct sound level (dB), 8 largest peak's position (samples)
double room_info(int id, int what)
{
    Measurement* m = slot(id);
    if (m == nullptr) return 0.0;
    switch (what)
    {
    case 0: return m->arrival.index;
    case 1: return m->arrival.polarity;
    case 2: return gainToDb(m->arrival.peak);
    case 3: return m->signalDb;
    case 4: return m->peakDbFS;
    case 5: return (double)m->ir.size();
    case 6: return m->sampleRate;
    case 7: return gainToDb(m->arrival.level);
    case 8: return m->arrival.peakIndex;
    default: return 0.0;
    }
}
const float* room_ir_ptr(int id) { Measurement* m = slot(id); return m ? m->ir.data() : nullptr; }

// The response on the grid fLo * 2^(i / perOct) (up to fHi), in dB: written to the returned buffer, which holds
// room_grid_size() values until the next call.
const float* room_response(int id, double windowMs, double fLo, double fHi, int perOct, double smoothOct, int useCal)
{
    Measurement* m = slot(id);
    if (m == nullptr || !m->ensureSpectrum(windowMs)) return nullptr;
    grid = logGrid(fLo, fHi, perOct);
    m->response(grid, smoothOct, useCal ? &cal : nullptr, out);
    return out.data();
}
int room_grid_size() { return (int)grid.size(); }
const double* room_grid_ptr() { return grid.data(); }

// Calibration file text (UTF-8): returns the number of points, or a CalError (< 0); the old curve is kept on error.
int room_cal(const char* text, int len)
{
    CalCurve c;
    const int r = parseCal(text, (size_t)std::max(0, len), c);
    if (r > 0) cal = c;
    return r;
}
void room_cal_clear() { cal = CalCurve(); }
// 0 points, 1 lowest Hz, 2 highest Hz, 3 sensitivity dB, 4 has sensitivity, 5 lines skipped
double room_cal_info(int what)
{
    switch (what)
    {
    case 0: return (double)cal.f.size();
    case 1: return cal.lowest();
    case 2: return cal.highest();
    case 3: return cal.sensitivityDb;
    case 4: return cal.hasSensitivity ? 1.0 : 0.0;
    case 5: return cal.skippedLines;
    default: return 0.0;
    }
}
double room_cal_at(double f) { return cal.at(f); }

// --- correction (stage 2): add each speaker's measurements, design, read the curves, write a profile, make filters

namespace {
std::vector<std::vector<double>> corrIn[2];   // per side: responses on the correction grid
std::vector<std::vector<double>> gdIn[2];     // per side: group delays (ms) on the correction grid
std::vector<double> wIn[2];                   // per side: each position's weight
std::vector<double> spreadOut[2];             // per side: how much the positions disagree (dB)
std::vector<double> tgtF, tgtDb;              // a base target curve (headphones), or none
std::vector<double> gdAvg[2], gdCorr[2];      // averaged group delay, and the phase correction (ms)
ChannelCorrection corrOut[2];
bool corrHave[2] = { false, false };
double corrPreamp = 0.0, corrDelay[2] = { 0.0, 0.0 };
std::string text;
std::vector<float> fir;
}

void room_corr_begin()
{
    for (int s = 0; s < 2; ++s) { corrIn[s].clear(); gdIn[s].clear(); wIn[s].clear(); corrHave[s] = false; }
    tgtF.clear(); tgtDb.clear();
}

// one measurement's response (window in ms, the mic's calibration if useCal) into a side's average (0 L, 1 R), with
// its weight in the average
int room_corr_add(int side, int id, double windowMs, int useCal, double weight)
{
    Measurement* m = slot(id);
    if (m == nullptr || side < 0 || side > 1 || !m->ensureSpectrum(windowMs)) return 0;
    const std::vector<double> g = correctionGrid();
    std::vector<float> r;
    m->response(g, 1.0 / 48.0, useCal ? &cal : nullptr, r);
    corrIn[side].emplace_back(r.begin(), r.end());
    gdIn[side].push_back(groupDelayMs(m->ir.data(), (int)m->ir.size(), m->arrival.index, m->sampleRate, g));
    wIn[side].push_back(weight > 0.0 ? weight : 1.0);
    return (int)corrIn[side].size();
}

// a response given as a curve (n points: frequencies f, levels db), e.g. a headphone's, into a side's average; no
// timing (its group delay counts as flat)
int room_corr_add_curve(int side, const double* f, const double* db, int n, double weight)
{
    if (side < 0 || side > 1 || f == nullptr || db == nullptr || n < 2) return 0;
    const std::vector<double> ff(f, f + n), dd(db, db + n), g = correctionGrid();
    std::vector<double> r(g.size());
    for (size_t i = 0; i < g.size(); ++i) r[i] = curveAt(ff, dd, g[i]);
    corrIn[side].push_back(r);
    gdIn[side].push_back(std::vector<double>(g.size(), 0.0));
    wIn[side].push_back(weight > 0.0 ? weight : 1.0);
    return (int)corrIn[side].size();
}

// the base target curve the tilt and shelves sit on (n points; n = 0: flat). Call after room_corr_begin.
void room_corr_target(const double* f, const double* db, int n)
{
    tgtF.clear(); tgtDb.clear();
    if (f == nullptr || db == nullptr || n < 2) return;
    tgtF.assign(f, f + n); tgtDb.assign(db, db + n);
}

// p: tilt, bass dB, treble dB, fLo, fHi, max boost, max cut, protect nulls, guard low end, amount, delay L ms, delay R ms,
//    phase on, phase from Hz, phase to Hz, spread-aware (correct less where the positions disagree; 4 or more),
//    smoothing (0: the measured curve as it is, for an EQ file)
int room_corr_design(const double* p)
{
    CorrectionSpec spec;
    spec.target.tiltDbPerOct = p[0]; spec.target.bassDb = p[1]; spec.target.trebleDb = p[2];
    spec.target.curveF = tgtF; spec.target.curveDb = tgtDb;
    spec.smooth = p[16] != 0.0;
    spec.fLo = p[3]; spec.fHi = p[4]; spec.maxBoostDb = p[5]; spec.maxCutDb = p[6];
    spec.protectNulls = p[7] != 0.0; spec.guardLowEnd = p[8] != 0.0; spec.amount = p[9];
    const std::vector<double> g = correctionGrid();
    std::vector<double> avg[2];
    std::vector<const std::vector<double>*> all;
    for (int s = 0; s < 2; ++s)
    {
        corrHave[s] = !corrIn[s].empty();
        if (corrHave[s]) { avg[s] = powerAverage(corrIn[s], wIn[s]); all.push_back(&avg[s]); }
        spreadOut[s] = corrHave[s] ? positionSpread(g, corrIn[s], wIn[s]) : std::vector<double>(g.size(), 0.0);
    }
    const double level = targetLevel(g, all);
    std::vector<const std::vector<double>*> corrs;
    for (int s = 0; s < 2; ++s)
    {
        CorrectionSpec sp = spec;
        if (p[15] != 0.0 && corrIn[s].size() >= 4) sp.trust = spreadTrust(g, spreadOut[s]);
        corrOut[s] = corrHave[s] ? designCorrection(g, avg[s], level, sp) : ChannelCorrection();
        if (corrHave[s]) corrs.push_back(&corrOut[s].correction);
        corrDelay[s] = std::max(0.0, p[10 + s]);
        corrOut[s].delayMs = corrDelay[s];
        // phase: the positions' group delays averaged, then the correction that lines them up
        gdAvg[s].assign(g.size(), 0.0);
        gdCorr[s].assign(g.size(), 0.0);
        if (corrHave[s] && !gdIn[s].empty())
        {
            double wsum = 0.0;
            for (size_t k = 0; k < gdIn[s].size(); ++k) wsum += wIn[s][k];
            for (size_t k = 0; k < gdIn[s].size(); ++k)
                for (size_t i = 0; i < g.size(); ++i) gdAvg[s][i] += gdIn[s][k][i] * wIn[s][k] / wsum;
            PhaseSpec ps;
            ps.on = p[12] != 0.0;
            ps.fLo = p[13];
            ps.fHi = p[14];
            gdCorr[s] = designPhaseCorrection(g, gdAvg[s], ps);
            for (double& v : gdCorr[s]) v *= std::clamp(spec.amount, 0.0, 1.0);
        }
    }
    corrPreamp = preampFor(corrs);
    return (int)g.size();
}

// 0 measured, 1 target, 2 correction, 3 result (doubles on the correction grid; null if the side has none)
const double* room_corr_curve(int side, int which)
{
    if (side < 0 || side > 1 || !corrHave[side]) return nullptr;
    const ChannelCorrection& c = corrOut[side];
    switch (which)
    {
    case 0: return c.measured.data();
    case 1: return c.target.data();
    case 2: return c.correction.data();
    case 3: return c.result.data();
    case 4: return gdAvg[side].data();    // measured group delay (ms)
    case 5: return gdCorr[side].data();   // phase correction (ms)
    case 6: return spreadOut[side].data(); // how much the positions disagree (dB)
    default: return nullptr;
    }
}
int room_corr_grid_size() { return (int)correctionGrid().size(); }
double room_corr_info(int side, int what)
{
    if (what == 0) return corrPreamp;
    if (side < 0 || side > 1 || !corrHave[side]) return 0.0;
    if (what == 1) return corrOut[side].lowGuardHz;
    if (what == 2) return corrDelay[side];
    return 0.0;
}

// the profile text for the current design (read it with room_text_ptr / room_text_len)
int room_corr_profile(const char* name, int len, int headphones)
{
    Profile p;
    p.name.assign(name, (size_t)std::max(0, len));
    p.headphones = headphones != 0;
    p.preampDb = corrPreamp;
    for (int s = 0; s < 2; ++s)
    {
        if (!corrHave[s]) continue;
        ProfileChannel& c = p.ch[p.channels++];
        c.side = s ? 'R' : 'L';
        c.delayMs = corrDelay[s];
        c.f = corrOut[s].f;
        c.correction = corrOut[s].correction;
        c.measured = corrOut[s].measured;
        c.target = corrOut[s].target;
        c.groupDelayMs = gdCorr[s];
    }
    text = writeProfile(p);
    return (int)text.size();
}
const char* room_text_ptr() { return text.data(); }
int room_text_len() { return (int)text.size(); }

// a side's correction filter at fs, the preamp and its delay in it, and with linear phase the phase correction (read
// with room_fir_ptr); returns its length
int room_corr_fir(int side, double fs, int linear)
{
    fir.clear();
    if (side < 0 || side > 1 || !corrHave[side]) return 0;
    fir = designFir(corrOut[side].f, corrOut[side].correction, fs, correctionTaps(fs), linear != 0,
                    corrDelay[side] * 0.001 * fs, corrPreamp, linear != 0 ? &gdCorr[side] : nullptr);
    return (int)fir.size();
}
const float* room_fir_ptr() { return fir.data(); }

double room_rms_db(const float* x, int n) { return rmsDb(x, n); }
double room_peak_db(const float* x, int n) { return peakDb(x, n); }

}
