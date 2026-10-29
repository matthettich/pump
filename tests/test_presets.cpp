// Checks every factory preset of every pump plugin: each value names a real parameter and lies in its range, no
// preset sets a momentary/output parameter, names are unique within a plugin, "Default" comes first, and the
// CHAINS presets carry the same names in every mastering plugin that has them (so a chain can be loaded plugin by
// plugin; the creative effects have none).
//
// It also guards saved sessions. Hosts store automation and parameter values by parameter INDEX (VST3 and CLAP ids
// are the index), so an existing index must never change meaning: tests/data/param_ids.txt records every plugin's
// index -> symbol, and a parameter may only be added at the end. After adding parameters (at the end), run
//   PUMP_WRITE_PARAM_IDS=1 ./build/tests/test_presets
// to append them to the file, and commit it.
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
#include "../plugins/chop/ChopParams.hpp"
#include "../plugins/amp/AmpParams.hpp"
#include "../plugins/table/TableParams.hpp"
#include "../plugins/room/RoomParams.hpp"
#include "../plugins/mono/MonoParams.hpp"
#include "../plugins/poly/PolyParams.hpp"
#include "../plugins/wob/WobParams.hpp"
#include "../plugins/kit/KitParams.hpp"
#include "../plugins/machine/MachineParams.hpp"
#include "../plugins/sing/SingParams.hpp"
#include "../plugins/coil/CoilParams.hpp"
#include "../plugins/twang/TwangParams.hpp"
#include "../plugins/phase/PhaseParams.hpp"
#include "../plugins/life/LifeParams.hpp"
#include "../plugins/boing/BoingParams.hpp"
#include "../plugins/prism/PrismParams.hpp"
#include "../plugins/modal/ModalParams.hpp"
#include "../plugins/orbit/OrbitParams.hpp"
#include "../plugins/fold/FoldParams.hpp"
#include "../plugins/vector/VectorParams.hpp"
#include "../plugins/strike/StrikeParams.hpp"
#include "../plugins/freeze/FreezeParams.hpp"
#include "../plugins/tract/TractParams.hpp"
#include "../plugins/acid/AcidParams.hpp"
#include "../plugins/x0x/X0xParams.hpp"
#include "../plugins/gloss/GlossParams.hpp"
#include "../plugins/break/BreakParams.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
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
    int numParams;   // the plugin's kNumParams (the size its code indexes with)
};

// the table matches the enum the code indexes with, symbols are unique, choice lists are sane
static void checkTable(const Plugin& p)
{
    if ((int)p.specs.size() != p.numParams) FAIL("%s: %zu parameters in the table, kNumParams is %d", p.name, p.specs.size(), p.numParams);
    std::set<std::string> syms;
    for (size_t i = 0; i < p.specs.size(); ++i)
    {
        const ParamSpec& s = p.specs[i];
        if (!syms.insert(s.symbol).second) FAIL("%s: symbol \"%s\" is used twice", p.name, s.symbol);
        if (!(s.min <= s.def && s.def <= s.max)) FAIL("%s: %s default %g is outside %g..%g", p.name, s.symbol, s.def, s.min, s.max);
        if (s.scale == PScale::Log && !(s.min > 0.0f)) FAIL("%s: %s has a log scale from %g (must start above 0)", p.name, s.symbol, s.min);
        if (s.choices && (s.numChoices() < 1 || s.numChoices() > 255 || s.min != std::floor(s.min)))
            FAIL("%s: %s has %d choices from %g", p.name, s.symbol, s.numChoices(), s.min);
        if (s.choices) for (int c = 0; c < s.numChoices(); ++c) if (!s.choices[c]) FAIL("%s: %s choice %d has no name", p.name, s.symbol, c);
    }
}

// every index recorded in tests/data/param_ids.txt still has the symbol it had; new parameters only at the end
static void checkIds(const Plugin* plugins, size_t count)
{
    const char* path = "tests/data/param_ids.txt";
    std::map<std::string, std::vector<std::string>> recorded;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string plug, sym; size_t idx = 0;
            if (!(ls >> plug >> idx >> sym)) { FAIL("%s: bad line \"%s\"", path, line.c_str()); continue; }
            auto& v = recorded[plug];
            if (idx != v.size()) { FAIL("%s: %s index %zu out of order", path, plug.c_str(), idx); continue; }
            v.push_back(sym);
        }
    }
    const bool write = std::getenv("PUMP_WRITE_PARAM_IDS") != nullptr, fresh = recorded.empty();
    std::string appended;
    int added = 0;
    for (size_t k = 0; k < count; ++k)
    {
        const Plugin& p = plugins[k];
        const auto& rec = recorded[p.name];
        for (size_t i = 0; i < rec.size(); ++i)
        {
            if (i >= p.specs.size()) { FAIL("%s: parameter %zu (\"%s\") was removed: hosts' saved sessions use its index", p.name, i, rec[i].c_str()); break; }
            if (rec[i] != p.specs[i].symbol)
                FAIL("%s: parameter %zu is now \"%s\" but sessions saved it as \"%s\" (add new parameters at the end only)", p.name, i, p.specs[i].symbol, rec[i].c_str());
        }
        for (size_t i = rec.size(); i < p.specs.size(); ++i)
        {
            appended += std::string(p.name) + " " + std::to_string(i) + " " + p.specs[i].symbol + "\n";
            ++added;
        }
    }
    if (added == 0) { std::printf("  parameter ids: every plugin matches %s\n", path); return; }
    if (!write) { FAIL("%d parameters are not in %s yet (run with PUMP_WRITE_PARAM_IDS=1 to record them)", added, path); return; }
    std::ofstream out(path, std::ios::app);
    if (fresh) out << "# plugin index symbol. Hosts save parameters by index: never change or remove a line, only append (tests/test_presets.cpp)\n";
    out << appended;
    std::printf("  parameter ids: recorded %d new parameters in %s\n", added, path);
}

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
        { "EQ", eq::params(), eq::presets(), eq::kNumParams },
        { "COMP", comp::params(), comp::presets(), comp::kNumParams },
        { "LIMIT", limit::params(), limit::presets(), limit::kNumParams },
        { "CALM", calm::params(), calm::presets(), calm::kNumParams },
        { "TONE", tone::params(), tone::presets(), tone::kNumParams },
        { "BAND", band::params(), band::presets(), band::kNumParams },
        { "WIDE", wide::params(), wide::presets(), wide::kNumParams },
        { "HEAT", heat::params(), heat::presets(), heat::kNumParams },
        { "METER", meter::params(), meter::presets(), meter::kNumParams },
        { "TUBE", tube::params(), tube::presets(), tube::kNumParams },
        { "SNAP", snap::params(), snap::presets(), snap::kNumParams },
        { "GRAIN", grain::params(), grain::presets(), grain::kNumParams },
        { "GATE", gate::params(), gate::presets(), gate::kNumParams },
        { "ECHO", echo::params(), echo::presets(), echo::kNumParams },
        { "VERB", verb::params(), verb::presets(), verb::kNumParams },
        { "SPRING", spring::params(), spring::presets(), spring::kNumParams },
        { "VOX", vox::params(), vox::presets(), vox::kNumParams },
        { "TUNE", tune::params(), tune::presets(), tune::kNumParams },
        { "CRUSH", crush::params(), crush::presets(), crush::kNumParams },
        { "SWIRL", swirl::params(), swirl::presets(), swirl::kNumParams },
        { "STUTTER", stutter::params(), stutter::presets(), stutter::kNumParams },
        { "CHOP", chop::params(), chop::presets(), chop::kNumParams },
        { "AMP", amp::params(), amp::presets(), amp::kNumParams },
        { "TWANG", twang::params(), twang::presets(), twang::kNumParams },
        { "TABLE", table::params(), table::presets(), table::kNumParams },
        { "MONO", mono::params(), mono::presets(), mono::kNumParams },
        { "POLY", poly::params(), poly::presets(), poly::kNumParams },
        { "WOB", wob::params(), wob::presets(), wob::kNumParams },
        { "KIT", kit::params(), kit::presets(), kit::kNumParams },
        { "MACHINE", machine::params(), machine::presets(), machine::kNumParams },
        { "SING", sing::params(), sing::presets(), sing::kNumParams },
        { "COIL", coil::params(), coil::presets(), coil::kNumParams },
        { "PHASE", phase::params(), phase::presets(), phase::kNumParams },
        { "LIFE", life::params(), life::presets(), life::kNumParams },
        { "BOING", boing::params(), boing::presets(), boing::kNumParams },
        { "PRISM", prism::params(), prism::presets(), prism::kNumParams },
        { "MODAL", modal::params(), modal::presets(), modal::kNumParams },
        { "ORBIT", orbit::params(), orbit::presets(), orbit::kNumParams },
        { "FOLD", fold::params(), fold::presets(), fold::kNumParams },
        { "VECTOR", vector::params(), vector::presets(), vector::kNumParams },
        { "STRIKE", strike::params(), strike::presets(), strike::kNumParams },
        { "FREEZE", freeze::params(), freeze::presets(), freeze::kNumParams },
        { "TRACT", tract::params(), tract::presets(), tract::kNumParams },
        { "ROOM", roomfx::params(), roomfx::presets(), roomfx::kNumParams },
        { "ACID", acid::params(), acid::presets(), acid::kNumParams },
        { "X0X", x0x::params(), x0x::presets(), x0x::kNumParams },
        { "GLOSS", gloss::params(), gloss::presets(), gloss::kNumParams },
        { "BREAK", brk::params(), brk::presets(), brk::kNumParams },
    };
    std::map<std::string, int> sections;
    std::vector<std::vector<std::string>> chains;
    for (const Plugin& p : plugins) { checkTable(p); chains.push_back(checkPlugin(p, sections)); }
    checkIds(plugins, sizeof plugins / sizeof plugins[0]);

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
