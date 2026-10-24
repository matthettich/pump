// Native twin of wasm/compare.mjs: runs one pump effect through wasm/pump_wasm.cpp (the same file pump.wasm is built
// from) on a stereo float32 file, in 128-sample blocks as Thunder Plus's worklet does, and writes the output.
//   c++ -O2 -std=c++17 -DPUMP_SYNC_KERNELS=1 -Icommon wasm/compare.cpp common/pump_pffft.c -o build/pump_compare
//   build/pump_compare <type 0-8> <preset index, -1 = defaults> <in.f32 interleaved stereo 48 kHz> <out.f32> [seconds]
// Halfway through, it moves the first automatable continuous parameter to 30% of its range (a parameter change mid-run).
#include "pump_wasm.cpp"
#include <cstdio>

static const std::vector<pump::Preset>& presetsOf(int type)
{
    switch (type)
    {
    case kEQ: return pump::eq::presets();
    case kComp: return pump::comp::presets();
    case kLimit: return pump::limit::presets();
    case kCalm: return pump::calm::presets();
    case kTone: return pump::tone::presets();
    case kBand: return pump::band::presets();
    case kWide: return pump::wide::presets();
    case kHeat: return pump::heat::presets();
    default: return pump::meter::presets();
    }
}

int main(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "usage: pump_compare type preset in.f32 out.f32 [seconds]\n"); return 1; }
    const int type = std::atoi(argv[1]), preset = std::atoi(argv[2]);
    const double secs = argc > 5 ? std::atof(argv[5]) : 10.0;
    std::FILE* f = std::fopen(argv[3], "rb");
    if (!f) return 1;
    std::vector<float> in;
    float buf[4096];
    size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, f)) > 0) in.insert(in.end(), buf, buf + got);
    std::fclose(f);
    const double sr = 48000.0;
    const int frames = std::min((int)(in.size() / 2), (int)(secs * sr));
    const auto& specs = specsOf(type);
    const int np = (int)specs.size();
    // the values a preset gives (as the UI applies it: unlisted parameters take their defaults)
    float* st = pump_stage();
    for (int i = 0; i < np; ++i) st[i] = specs[(size_t)i].def;
    if (preset >= 0 && preset < (int)presetsOf(type).size())
        for (const auto& pv : presetsOf(type)[(size_t)preset].values)
            for (int i = 0; i < np; ++i) if (std::strcmp(specs[(size_t)i].symbol, pv.symbol) == 0) st[i] = pv.value;
    const int h = pump_new(type, sr);
    int change = -1;
    for (int i = 0; i < np; ++i)
    {
        const auto& s = specs[(size_t)i];
        if ((s.flags & pump::kPAutomatable) && !(s.flags & (pump::kPInteger | pump::kPBoolean | pump::kPBypass)) && !s.choices) { change = i; break; }
    }
    std::vector<float> out((size_t)frames * 2);
    float* L = pump_io(0);
    float* R = pump_io(1);
    for (int off = 0; off < frames; off += 128)
    {
        const int n = std::min(128, frames - off);
        if (change >= 0 && off <= frames / 2 && off + 128 > frames / 2)
        {
            pump_values(h)[change] = specs[(size_t)change].fromNorm(0.3f);
            pump_commit(h);
        }
        for (int i = 0; i < n; ++i) { L[i] = in[(size_t)(off + i) * 2]; R[i] = in[(size_t)(off + i) * 2 + 1]; }
        pump_process(h, n, 0);
        for (int i = 0; i < n; ++i) { out[(size_t)(off + i) * 2] = L[i]; out[(size_t)(off + i) * 2 + 1] = R[i]; }
    }
    f = std::fopen(argv[4], "wb");
    std::fwrite(out.data(), sizeof(float), out.size(), f);
    std::fclose(f);
    std::printf("%d %d %d\n", pump_latency(h), frames, change);
    return 0;
}
