// Writes every pump plugin's parameter table and factory presets as JSON, from the same Params.hpp tables the plugins
// use (so Thunder Plus's preset lists and parameter ranges stay in parity with the plugins). Built and run by
// wasm/build.sh on the host compiler:  c++ -std=c++17 -Icommon wasm/pump_meta.cpp -o pump_meta && ./pump_meta > meta.json
// `./pump_meta synth` writes the instruments in pump-synth.wasm instead (wasm/pump_synth.cpp), in the same shape with
// "synth" in place of "fx".
//
// Output: {"version":1,"types":["eq",...],"fx":{"eq":{"name":"EQ","long":"...","params":[...],"presets":[...]},...}}
//   param:  {"s":symbol,"n":name,"sn":short name,"min","max","def","unit":"dB"|"Hz"|...|"","scale":"lin"|"log"|"pow",
//            "skew","flags":{automatable,integer,boolean,output,bypass,hidden,noUndo} as an int bit mask (PumpParams.hpp),
//            "ch":[choice names] (choice params only)}
//   preset: {"n":name,"sec":"FACTORY"|"SOURCES"|"CHAINS","v":{symbol:value,...}}  (unlisted symbols take their default)
#include "../plugins/eq/EQParams.hpp"
#include "../plugins/comp/CompParams.hpp"
#include "../plugins/limit/LimitParams.hpp"
#include "../plugins/calm/CalmParams.hpp"
#include "../plugins/tone/ToneParams.hpp"
#include "../plugins/band/BandParams.hpp"
#include "../plugins/wide/WideParams.hpp"
#include "../plugins/heat/HeatParams.hpp"
#include "../plugins/meter/MeterParams.hpp"
#include "../plugins/tube/TubeParams.hpp"
#include "../plugins/snap/SnapParams.hpp"
#include "../plugins/gate/GateParams.hpp"
#include "../plugins/echo/EchoParams.hpp"
#include "../plugins/spring/SpringParams.hpp"
#include "../plugins/verb/VerbParams.hpp"
#include "../plugins/grain/GrainParams.hpp"
#include "../plugins/vox/VoxParams.hpp"
#include "../plugins/tune/TuneParams.hpp"
#include "../plugins/crush/CrushParams.hpp"
#include "../plugins/swirl/SwirlParams.hpp"
#include "../plugins/stutter/StutterParams.hpp"
#include "../plugins/chop/ChopParams.hpp"
#include "../plugins/twang/TwangParams.hpp"
#include "../plugins/master/MasterParams.hpp"
#include "../plugins/mono/MonoParams.hpp"
#include "../plugins/poly/PolyParams.hpp"
#include "../plugins/coil/CoilParams.hpp"
#include <cstdio>
#include <cstring>
#include <string>

using namespace pump;

static std::string esc(const char* s)
{
    std::string o = "\"";
    for (const char* p = s; *p; ++p)
    {
        const unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}
static std::string num(double v)
{
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}
static const char* unitName(PUnit u)
{
    switch (u)
    {
    case PUnit::dB: return "dB";
    case PUnit::Hz: return "Hz";
    case PUnit::ms: return "ms";
    case PUnit::Percent: return "%";
    case PUnit::Ratio: return "ratio";
    case PUnit::Octave: return "oct";
    case PUnit::Times: return "x";
    case PUnit::LU: return "LU";
    case PUnit::Seconds: return "s";
    default: return "";
    }
}

static void dump(const char* key, const char* name, const char* longName, const std::vector<ParamSpec>& ps, const std::vector<Preset>& pr, bool last)
{
    std::printf("%s:{\"name\":%s,\"long\":%s,\"params\":[\n", esc(key).c_str(), esc(name).c_str(), esc(longName).c_str());
    for (size_t i = 0; i < ps.size(); ++i)
    {
        const ParamSpec& s = ps[i];
        std::string o = "{\"s\":" + esc(s.symbol) + ",\"n\":" + esc(s.name) + ",\"sn\":" + esc(s.shortName) + ",\"min\":" + num(s.min)
            + ",\"max\":" + num(s.max) + ",\"def\":" + num(s.def) + ",\"unit\":" + esc(unitName(s.unit)) + ",\"scale\":"
            + (s.scale == PScale::Log ? "\"log\"" : s.scale == PScale::Power ? "\"pow\"" : "\"lin\"") + ",\"skew\":" + num(s.skew)
            + ",\"flags\":" + std::to_string(s.flags);
        if (s.choices)
        {
            o += ",\"ch\":[";
            for (int k = 0; k < s.numChoices(); ++k) o += (k ? "," : "") + esc(s.choices[k]);
            o += "]";
        }
        o += "}";
        std::printf("%s%s\n", o.c_str(), i + 1 < ps.size() ? "," : "");
    }
    std::printf("],\"presets\":[\n");
    for (size_t i = 0; i < pr.size(); ++i)
    {
        const Preset& p = pr[i];
        std::string o = "{\"n\":" + esc(p.name) + ",\"sec\":" + esc(p.section ? p.section : "FACTORY") + ",\"v\":{";
        for (size_t k = 0; k < p.values.size(); ++k) o += (k ? "," : "") + esc(p.values[k].symbol) + ":" + num(p.values[k].value);
        o += "}}";
        std::printf("%s%s\n", o.c_str(), i + 1 < pr.size() ? "," : "");
    }
    std::printf("]}%s\n", last ? "" : ",");
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "synth") == 0)
    {
        std::printf("{\"version\":1,\"types\":[\"mono\",\"poly\",\"coil\"],\"synth\":{\n");
        dump("mono", "MONO", "Analog Mono Synth", mono::params(), mono::presets(), false);
        dump("poly", "POLY", "Vintage Poly Synth", poly::params(), poly::presets(), false);
        dump("coil", "COIL", "Spring Synth", coil::params(), coil::presets(), true);
        std::printf("}}\n");
        return 0;
    }
    std::printf("{\"version\":1,\"types\":[\"eq\",\"comp\",\"limit\",\"calm\",\"tone\",\"band\",\"wide\",\"heat\",\"meter\",\"tube\",\"snap\",\"gate\",\"echo\",\"spring\",\"verb\",\"grain\",\"vox\",\"tune\",\"crush\",\"swirl\",\"stutter\",\"chop\",\"twang\",\"master\"],\"fx\":{\n");
    dump("eq", "EQ", "Equalizer", eq::params(), eq::presets(), false);
    dump("comp", "COMP", "Compressor", comp::params(), comp::presets(), false);
    dump("limit", "LIMIT", "Limiter", limit::params(), limit::presets(), false);
    dump("calm", "CALM", "Resonance Suppressor", calm::params(), calm::presets(), false);
    dump("tone", "TONE", "Tonal Balance", tone::params(), tone::presets(), false);
    dump("band", "BAND", "Multiband Dynamics", band::params(), band::presets(), false);
    dump("wide", "WIDE", "Stereo Imager", wide::params(), wide::presets(), false);
    dump("heat", "HEAT", "Saturator", heat::params(), heat::presets(), false);
    dump("meter", "METER", "Loudness Meter", meter::params(), meter::presets(), false);
    dump("tube", "TUBE", "Tube Distortion", tube::params(), tube::presets(), false);
    dump("snap", "SNAP", "Transient Shaper", snap::params(), snap::presets(), false);
    dump("gate", "GATE", "Sequenced Gate", gate::params(), gate::presets(), false);
    dump("echo", "ECHO", "Delay", echo::params(), echo::presets(), false);
    dump("spring", "SPRING", "Spring Reverb", spring::params(), spring::presets(), false);
    dump("verb", "VERB", "Reverb", verb::params(), verb::presets(), false);
    dump("grain", "GRAIN", "Granular", grain::params(), grain::presets(), false);
    dump("vox", "VOX", "Vocoder", vox::params(), vox::presets(), false);
    dump("tune", "TUNE", "Pitch Correction", tune::params(), tune::presets(), false);
    dump("crush", "CRUSH", "Lo-Fi Degrader", crush::params(), crush::presets(), false);
    dump("swirl", "SWIRL", "Modulation", swirl::params(), swirl::presets(), false);
    dump("stutter", "STUTTER", "Beat Repeat", stutter::params(), stutter::presets(), false);
    dump("chop", "CHOP", "Live Chopper", chop::params(), chop::presets(), false);
    dump("twang", "TWANG", "Spring Resonator", twang::params(), twang::presets(), false);
    dump("master", "MASTER", "Auto Mastering", master::params(), master::presets(), true);
    std::printf("}}\n");
    return 0;
}
