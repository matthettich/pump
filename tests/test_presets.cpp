// Checks every factory preset of every pump plugin: each value names a real parameter and lies in its range, no
// preset sets a momentary/output parameter, names are unique within a plugin, "Default" comes first, and the
// CHAINS presets carry the same names in every mastering plugin that has them (so a chain can be loaded plugin by
// plugin; the creative effects have none).
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
#include "../plugins/grain/GrainParams.hpp"
#include "../plugins/gate/GateParams.hpp"
#include "../plugins/echo/EchoParams.hpp"
#include "../plugins/verb/VerbParams.hpp"
#include "../plugins/spring/SpringParams.hpp"
#include "../plugins/vox/VoxParams.hpp"
#include "../plugins/tune/TuneParams.hpp"
#include "../plugins/crush/CrushParams.hpp"
#include "../plugins/swirl/SwirlParams.hpp"
#include "../plugins/stutter/StutterParams.hpp"
#include "../plugins/amp/AmpParams.hpp"
#include "../plugins/table/TableParams.hpp"
#include "../plugins/mono/MonoParams.hpp"
#include "../plugins/poly/PolyParams.hpp"
#include "../plugins/wob/WobParams.hpp"
#include "../plugins/kit/KitParams.hpp"
#include "../plugins/machine/MachineParams.hpp"
#include "../plugins/sing/SingParams.hpp"
#include "../plugins/coil/CoilParams.hpp"
#include "../plugins/twang/TwangParams.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace pump;

static int failures = 0;
#define FAIL(...) do { std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } while (0)

struct Plugin {
    const char* name;
    const std::vector<ParamSpec>& specs;
    const std::vector<Preset>& presets;
};

static std::vector<std::string> checkPlugin(const Plugin& p, std::map<std::string, int>& sectionCounts)
{
    std::vector<std::string> chains;
    std::set<std::string> names;
    if (p.presets.empty() || std::strcmp(p.presets[0].name, "Default") != 0) FAIL("%s: the first preset must be \"Default\"", p.name);
    std::vector<std::string> sectionOrder;
    for (const Preset& pr : p.presets)
    {
        const std::string sec = pr.section ? pr.section : "FACTORY";
        sectionCounts[sec]++;
        if (sectionOrder.empty() || sectionOrder.back() != sec)
        {
            for (const auto& s : sectionOrder)
                if (s == sec) FAIL("%s: section %s is split (its presets must be added together)", p.name, sec.c_str());
            sectionOrder.push_back(sec);
        }
        if (!names.insert(pr.name).second) FAIL("%s: preset name \"%s\" is used twice", p.name, pr.name);
        if (sec == "CHAINS") chains.push_back(pr.name);
        std::set<std::string> seen;
        for (const PresetValue& v : pr.values)
        {
            const ParamSpec* s = nullptr;
            for (const ParamSpec& ps : p.specs) if (std::strcmp(ps.symbol, v.symbol) == 0) { s = &ps; break; }
            if (!s) { FAIL("%s / \"%s\": unknown parameter \"%s\"", p.name, pr.name, v.symbol); continue; }
            if (!seen.insert(v.symbol).second) FAIL("%s / \"%s\": \"%s\" is set twice", p.name, pr.name, v.symbol);
            if (s->flags & (kPOutput | kPBypass | kPHidden | kPNoUndo))
                FAIL("%s / \"%s\": sets \"%s\", which presets never touch (output, bypass or momentary)", p.name, pr.name, v.symbol);
            if (v.value < s->min - 1e-4f || v.value > s->max + 1e-4f)
                FAIL("%s / \"%s\": %s = %g is outside %g..%g", p.name, pr.name, v.symbol, v.value, s->min, s->max);
            if ((s->flags & (kPInteger | kPBoolean)) || s->choices)
                if (std::fabs(v.value - std::round(v.value)) > 1e-4f)
                    FAIL("%s / \"%s\": %s = %g should be a whole number", p.name, pr.name, v.symbol, v.value);
        }
    }
    std::printf("  %-6s %3zu presets:", p.name, p.presets.size());
    for (const auto& s : sectionOrder)
    {
        int c = 0;
        for (const Preset& pr : p.presets) if ((pr.section ? pr.section : "FACTORY") == s) ++c;
        std::printf("  %s %d", s.c_str(), c);
    }
    std::printf("\n");
    return chains;
}

int main()
{
    std::printf("== preset tables\n");
    const Plugin plugins[] = {
        { "EQ", eq::params(), eq::presets() },
        { "COMP", comp::params(), comp::presets() },
        { "LIMIT", limit::params(), limit::presets() },
        { "CALM", calm::params(), calm::presets() },
        { "TONE", tone::params(), tone::presets() },
        { "BAND", band::params(), band::presets() },
        { "WIDE", wide::params(), wide::presets() },
        { "HEAT", heat::params(), heat::presets() },
        { "METER", meter::params(), meter::presets() },
        { "TUBE", tube::params(), tube::presets() },
        { "SNAP", snap::params(), snap::presets() },
        { "GRAIN", grain::params(), grain::presets() },
        { "GATE", gate::params(), gate::presets() },
        { "ECHO", echo::params(), echo::presets() },
        { "VERB", verb::params(), verb::presets() },
        { "SPRING", spring::params(), spring::presets() },
        { "VOX", vox::params(), vox::presets() },
        { "TUNE", tune::params(), tune::presets() },
        { "CRUSH", crush::params(), crush::presets() },
        { "SWIRL", swirl::params(), swirl::presets() },
        { "STUTTER", stutter::params(), stutter::presets() },
        { "AMP", amp::params(), amp::presets() },
        { "TWANG", twang::params(), twang::presets() },
        { "TABLE", table::params(), table::presets() },
        { "MONO", mono::params(), mono::presets() },
        { "POLY", poly::params(), poly::presets() },
        { "WOB", wob::params(), wob::presets() },
        { "KIT", kit::params(), kit::presets() },
        { "MACHINE", machine::params(), machine::presets() },
        { "SING", sing::params(), sing::presets() },
        { "COIL", coil::params(), coil::presets() },
    };
    std::map<std::string, int> sections;
    std::vector<std::vector<std::string>> chains;
    for (const Plugin& p : plugins) chains.push_back(checkPlugin(p, sections));

    // every plugin that has chains has the same ones, in the same order
    std::vector<std::string> ref;
    for (const auto& c : chains) if (!c.empty()) { ref = c; break; }
    if (!ref.empty())
    {
        for (size_t i = 0; i < chains.size(); ++i)
            if (!chains[i].empty() && chains[i] != ref)   // (the creative effects have no chains)
            {
                std::string have, want;
                for (const auto& s : chains[i]) have += "\"" + s + "\" ";
                for (const auto& s : ref) want += "\"" + s + "\" ";
                FAIL("%s: chains differ from the others (has %s; expected %s)", plugins[i].name, have.c_str(), want.c_str());
            }
        std::printf("  %zu chains in every mastering plugin\n", ref.size());
    }

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all passed\n");
    return 0;
}
