// pump ROOM: the measurement DSP (common/dsp/Room.hpp) behind a small C interface for the browser app (room/).
// Built by room/build.sh into pump-room.wasm. No threads, no exceptions; the app passes audio and text through
// buffers it allocates here (room_alloc / room_release_buf).
#include "dsp/Room.hpp"
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

double room_rms_db(const float* x, int n) { return rmsDb(x, n); }
double room_peak_db(const float* x, int n) { return peakDb(x, n); }

}
