// Offline host for the instruments: loads a plugin through DPF's own exporter (no UI, no audio device), plays every
// factory preset with a fixed test phrase and prints each preset's sample peak and loudness. Used to set the
// instruments' output gain (the volume pass) and to check that no factory preset clips.
// Build (from the repo root), e.g. for MONO:
//   c++ -O2 -std=c++17 -DPUMP_TEST_HOOKS=1 -Iplugins/mono -Idpf/distrho -Idpf/distrho/src -Icommon -DPARAMS_HDR='"MonoParams.hpp"' -DNS=mono \
//       -DKIND=0 tools/render_presets.cpp plugins/mono/PluginMono.cpp plugins/mono/pffft.c -lpthread -o build/render_mono
// KIND: 0 a synth (a chord, then a melody), 1 drums (GM notes), 2 LIFE (it plays itself), 3 SING (one voice).
// Output: one line per preset, then "median peak" and "max peak" (dBFS). With PUMP_RENDER_DIR set, each preset's audio
// is also written there as <index>.f32 (stereo interleaved float, 48 kHz).
#include "DistrhoPluginInternal.hpp"
#include "src/DistrhoPlugin.cpp"
#define DISTRHO_IS_STANDALONE 0
#include "src/DistrhoUtils.cpp"
#include PARAMS_HDR
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

USE_NAMESPACE_DISTRHO

static bool writeMidi(void*, const MidiEvent&) { return true; }
static bool reqParam(void*, uint32_t, float) { return true; }
static bool updState(void*, const char*, const char*) { return true; }

int main()
{
    const double sr = 48000.0; const uint32_t bs = 256;
    d_nextBufferSize = bs; d_nextSampleRate = sr; d_nextCanRequestParameterValueChanges = false;
    const auto& ps = pump::NS::presets();
    std::vector<double> peaks;
    for (size_t p = 0; p < ps.size(); ++p)
    {
        PluginExporter ex(nullptr, writeMidi, reqParam, updState);
        ex.setSampleRate(sr); ex.setBufferSize(bs);
        const uint32_t np = ex.getParameterCount();
        for (const auto& v : ps[p].values)
            for (uint32_t i = 0; i < np; ++i)
                if (ex.getParameterSymbol(i) == v.symbol) ex.setParameterValue(i, v.value);
       #if KIND == 2
        for (uint32_t i = 0; i < np; ++i) if (ex.getParameterSymbol(i) == "mode") ex.setParameterValue(i, 2.0f);   // free running
       #endif
        ex.activate();
        // the phrase: (time s, note, velocity; 0 = off)
        std::vector<std::tuple<double, int, int>> ev;
       #if KIND == 0
        for (int n : { 48, 55, 60, 64 }) { ev.push_back({ 0.05, n, 100 }); ev.push_back({ 1.6, n, 0 }); }
        { const int mel[8] = { 60, 62, 64, 67, 69, 67, 64, 72 }; for (int k = 0; k < 8; ++k) { ev.push_back({ 2.2 + k * 0.3, mel[k], 110 }); ev.push_back({ 2.2 + k * 0.3 + 0.25, mel[k], 0 }); } }
        const double secs = 5.2;
       #elif KIND == 1
        for (int k = 0; k < 16; ++k) { const int n = k % 4 == 0 ? 36 : k % 4 == 2 ? 38 : 42; ev.push_back({ 0.05 + k * 0.125, n, k % 2 ? 90 : 120 }); ev.push_back({ 0.05 + k * 0.125 + 0.1, n, 0 }); }
        ev.push_back({ 2.1, 49, 120 }); ev.push_back({ 2.2, 49, 0 });
        const double secs = 3.5;
       #elif KIND == 3
        { const int mel[8] = { 60, 62, 64, 65, 67, 65, 64, 62 }; for (int k = 0; k < 8; ++k) { ev.push_back({ 0.05 + k * 0.45, mel[k], 100 }); ev.push_back({ 0.05 + k * 0.45 + 0.4, mel[k], 0 }); } }
        const double secs = 4.0;
       #else
        const double secs = 8.0;
       #endif
        std::sort(ev.begin(), ev.end());
        const uint32_t total = (uint32_t)(secs * sr);
        std::vector<float> L(bs), R(bs);
        float* outs[2] = { L.data(), R.data() };
       #if DISTRHO_PLUGIN_NUM_INPUTS > 0
        std::vector<float> zi(bs, 0.0f); const float* ins[2] = { zi.data(), zi.data() };
       #else
        const float** ins = nullptr;
       #endif
        size_t e = 0; double pk = 0.0, en = 0.0; uint64_t cnt = 0;
        std::vector<float> dump;
        const char* dir = std::getenv("PUMP_RENDER_DIR");
        for (uint32_t off = 0; off < total; off += bs)
        {
            std::vector<MidiEvent> me;
            while (e < ev.size() && std::get<0>(ev[e]) * sr < off + bs)
            {
                MidiEvent m {}; m.frame = (uint32_t)std::max(0.0, std::get<0>(ev[e]) * sr - off); m.size = 3;
                const int vel = std::get<2>(ev[e]);
                m.data[0] = vel ? 0x90 : 0x80; m.data[1] = (uint8_t)std::get<1>(ev[e]); m.data[2] = (uint8_t)vel;
                me.push_back(m); ++e;
            }
           #if DISTRHO_PLUGIN_WANT_MIDI_INPUT
            ex.run(ins, outs, bs, me.data(), (uint32_t)me.size());
           #else
            ex.run(ins, outs, bs);
           #endif
            for (uint32_t i = 0; i < bs; ++i)
            {
                const double a = std::max(std::fabs(L[i]), std::fabs(R[i]));
                pk = std::max(pk, a);
                if (a > 1e-4) { en += 0.5 * ((double)L[i] * L[i] + (double)R[i] * R[i]); ++cnt; }
                if (dir) { dump.push_back(L[i]); dump.push_back(R[i]); }
            }
        }
        if (dir) { const std::string fn = std::string(dir) + "/" + std::to_string(p) + ".f32"; if (FILE* f = std::fopen(fn.c_str(), "wb")) { std::fwrite(dump.data(), 4, dump.size(), f); std::fclose(f); } }
        const double pdb = 20 * std::log10(pk + 1e-12), rdb = cnt ? 10 * std::log10(en / cnt + 1e-24) : -200;
        std::printf("%3zu %-28s peak %6.1f dBFS  rms %6.1f dBFS\n", p, ps[p].name, pdb, rdb);
        peaks.push_back(pdb);
    }
    std::vector<double> s = peaks; std::sort(s.begin(), s.end());
    std::printf("median peak %.1f  max peak %.1f  min peak %.1f\n", s[s.size() / 2], s.back(), s.front());
    return 0;
}
