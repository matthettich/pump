// Render a song through pump MASTER offline at its defaults (LEARN on the whole song, then play it): raw float32
// stereo interleaved in and out, the chain's latency removed. For checking against reference masters.
//   c++ -O2 -std=c++17 -Icommon -Iplugins/master tools/master_render.cpp build/tests/pffft.o -o build/master_render
//   build/master_render 44100 in.f32 out.f32 [intensity]
#include "../plugins/master/MasterEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace pump; using namespace pump::master;
int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: master_render rate in.f32 out.f32 [intensity]\n"); return 1; }
    const double sr = std::atof(argv[1]);
    FILE* f = std::fopen(argv[2], "rb"); if (!f) return 1;
    std::fseek(f, 0, SEEK_END); const size_t n = (size_t)std::ftell(f) / 8; std::fseek(f, 0, SEEK_SET);
    std::vector<float> v(2 * n); if (std::fread(v.data(), 8, n, f) != n) return 1; std::fclose(f);
    std::vector<float> L(n), R(n); for (size_t k = 0; k < n; ++k) { L[k] = v[2 * k]; R[k] = v[2 * k + 1]; }
    Engine e; e.prepare(sr); Settings s; if (argc > 4) s.intensity = (float)std::atof(argv[4]); e.set(s);
    e.learnBuffer(L.data(), R.data(), (int)n);
    const int lat = e.latency();
    std::vector<float> oL(L), oR(R);
    oL.resize(n + lat, 0.0f); oR.resize(n + lat, 0.0f);
    for (size_t off = 0; off < oL.size(); off += 512) e.process(oL.data() + off, oR.data() + off, (int)std::min<size_t>(512, oL.size() - off));
    const Plan& p = e.plan();
    std::fprintf(stderr, "plan: makeup %+.2f, predicted %.2f LUFS, tone:", p.makeupDb, p.outLufs);
    for (double g : p.eqGain) std::fprintf(stderr, " %+.1f", g);
    std::fprintf(stderr, "\n");
    FILE* o = std::fopen(argv[3], "wb");
    for (size_t k = 0; k < n; ++k) { const float x[2] = { oL[k + lat], oR[k + lat] }; std::fwrite(x, 4, 2, o); }
    std::fclose(o);
    return 0;
}
