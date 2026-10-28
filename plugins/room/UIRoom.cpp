// pump ROOM: interface. PROFILE (the loaded .pumproom: name, speakers, preamp, delays, latency; LOAD, CLEAR),
// CORRECTION (what the room measured like, faint; how it should measure with this correction, bold; the target,
// dotted; underneath, the correction itself, all scaled by AMOUNT), and below: amount, phase, time alignment, output.
#include "ui/PumpUI.hpp"
#include "PluginRoom.hpp"
#include "FilePath.hpp"
#include <algorithm>

START_NAMESPACE_DISTRHO

using namespace pump::roomfx;
namespace room = pump::room;

class UIRoom : public PumpUI {
public:
    UIRoom() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::room(), "ROOM", 940, 540, kBypass)
    {
        amountDial = addDial(kAmount, "AMOUNT", Dial::Large);
        amountDial->tip = "How much of the correction to apply (the preamp follows)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        phaseSeg = addSegmented(kPhase, { "MINIMUM", "LINEAR" });
        phaseSeg->tip = "Minimum phase: no extra latency (tracking, listening). Linear phase: about 0.17 s of latency (mixing), and the profile's phase correction if it has one";
        alignBtn = addToggle(kTimeAlign, "TIME ALIGN");
        alignBtn->small = true;
        alignBtn->tip = "Delay the nearer speaker so both arrive together (from the measurement)";
        amountDial->tabGroup = outDial->tabGroup = 1;

        loadBtn = addButton("LOAD", [this] { browse(); });
        clearBtn = addButton("CLEAR", [this] { setState("profile", ""); setState("profile_file", ""); refresh(true); });
        loadBtn->small = clearBtn->small = true;
        loadBtn->tip = "Load a room profile (.pumproom) made with the pump ROOM app";
        clearBtn->tip = "Remove the profile (the sound passes unchanged)";

        if (auto* p = static_cast<PluginRoom*>(pluginPtr())) plug = p;
    }

protected:
    void browse()
    {
        FileBrowserOptions o;
        o.title = "Load a room profile (.pumproom)";
        std::string dir;
        if (!file.empty()) dir = pump::pathUtf8(pump::utf8Path(file).parent_path());
        if (!dir.empty()) o.startDir = dir.c_str();
        openFileBrowser(o);
    }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename || !*filename) return;
        std::FILE* f = pump::fopenUtf8(filename, "rb");
        if (!f) { setStatus("Couldn't open that file"); return; }
        std::string s;
        char buf[8192];
        size_t n;
        bool tooBig = false;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        {
            s.append(buf, n);
            if (s.size() > ((size_t)4 << 20)) { tooBig = true; break; }
        }
        std::fclose(f);
        room::Profile p;
        std::string why;
        if (tooBig || !room::parseProfile(s.data(), s.size(), p, why)) { setStatus(std::string("Not a room profile: ") + (tooBig ? "too large" : why)); return; }
        setState("profile", s.c_str());
        setState("profile_file", filename);
        setStatus("Loaded " + (p.name.empty() ? std::string("the profile") : p.name));
        refresh(true);
    }

    std::string formatParam(int i, float v) const override
    {
        if (i == kAmount) { char b[16]; std::snprintf(b, sizeof b, "%.0f %%", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void refresh(bool force)
    {
        if (!plug) return;
        if (!force && plug->profileSerial() == seenProfile && plug->engine.infoSerial() == seenDesign) return;
        seenProfile = plug->profileSerial();
        seenDesign = plug->engine.infoSerial();
        prof = plug->profile(&error, &file);
        design = plug->engine.info();
    }

    void layout() override
    {
        profR = Rect(14, 58, 290, 292);
        graphR = Rect(318, 58, 568, 292);
        meterL = Rect(898, 58, 9, 276);
        meterR = Rect(909, 58, 9, 276);
        loadBtn->r = Rect(profR.x + 10, profR.b() - 34, 64, 22);
        clearBtn->r = Rect(profR.r() - 74, profR.b() - 34, 64, 22);

        const float y0 = 364;
        ctlPanel = Rect(14, y0, 904, 140);
        amountDial->r = Rect(40, y0 + 26, 96, Dial::heightFor(Dial::Large));
        phaseSeg->r = Rect(190, y0 + 46, 210, 26);
        alignBtn->r = Rect(430, y0 + 46, 150, 26);
        outDial->r = Rect(812, y0 + 34, 72, Dial::heightFor(Dial::Medium));
    }

    void tick(float dt) override
    {
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        refresh(false);
    }

    void drawBody() override
    {
        drawProfile();
        drawGraph();
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, graphR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        strokeRect(ctlPanel, theme.dim, 1.0f);
        caption(ctlPanel.x + 12, ctlPanel.y + 16, "CORRECTION");
        caption(phaseSeg->r.x, phaseSeg->r.y - 10, "PHASE");
        caption(alignBtn->r.x, alignBtn->r.y - 10, "SPEAKERS");
        // what the controls mean right now
        char b[160];
        const bool lin = (int)value(kPhase) == kLinear;
        const bool hasPhase = prof.valid() && ((prof.side('L') && prof.side('L')->hasPhase()) || (prof.side('R') && prof.side('R')->hasPhase()));
        std::snprintf(b, sizeof b, lin ? (hasPhase ? "LINEAR PHASE + PHASE CORRECTION: %d SAMPLES OF LATENCY" : "LINEAR PHASE: %d SAMPLES OF LATENCY")
                                       : (hasPhase ? "MINIMUM PHASE: %d SAMPLES (PHASE CORRECTION NEEDS LINEAR)" : "MINIMUM PHASE: %d SAMPLES OF LATENCY"),
                      plug ? plug->engine.latency() : 0);
        textAt(phaseSeg->r.x, phaseSeg->r.b() + 18, b, kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        if (prof.valid())
        {
            std::snprintf(b, sizeof b, "PREAMP %.1f dB (HEADROOM FOR THE BOOSTS)", design.preampDb);
            textAt(phaseSeg->r.x, phaseSeg->r.b() + 36, b, kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            const double dl = design.delayMs[0], dr = design.delayMs[1];
            if (value(kTimeAlign) >= 0.5f && (dl > 0.0 || dr > 0.0))
                std::snprintf(b, sizeof b, "%s DELAYED %.2f ms", dl > 0.0 ? "LEFT" : "RIGHT", dl > 0.0 ? dl : dr);
            else
                std::snprintf(b, sizeof b, value(kTimeAlign) >= 0.5f ? "NO DELAY NEEDED" : "NOT ALIGNED");
            textAt(alignBtn->r.x, alignBtn->r.b() + 18, b, kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        }
        if (plug && plug->engine.busy())
            textAt(ctlPanel.r() - 120, ctlPanel.y + 16, "UPDATING…", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
    }

    void drawProfile()
    {
        const Rect& r = profR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "PROFILE");
        const Rect nb(r.x + 10, r.y + 28, r.w - 20, 40);
        const bool loaded = prof.valid();
        fillRect(nb, loaded ? theme.neon : theme.panel);
        strokeRect(nb, theme.ink, 1.5f);
        std::string n = loaded ? (prof.name.empty() ? std::string("UNNAMED") : prof.name) : std::string("NO PROFILE: LOAD A .PUMPROOM");
        for (char& c : n) c = (char)std::toupper((unsigned char)c);
        const float maxW = nb.w - 16;
        float size = 14.0f;
        while (size > 9.0f && textWidth(n, kBold, size, 0.6f) > maxW) size -= 0.5f;
        while (n.size() > 4 && textWidth(n, kBold, size, 0.6f) > maxW) n = n.substr(0, n.size() - 4) + "...";
        textAt(nb.x + 8, nb.cy(), n, kBold, size, loaded ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        if (!error.empty()) textAt(r.x + 10, nb.b() + 14, "LAST FILE: " + error, kLabel, 9.0f, theme.red, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);

        float y = r.y + 100;
        auto row = [&](const char* k, const std::string& v) {
            caption(r.x + 10, y, k);
            textAt(r.r() - 10, y, v, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
            line(r.x + 10, y + 10, r.r() - 10, y + 10, theme.grid, 1.0f);
            y += 25;
        };
        char b[64];
        if (loaded)
        {
            const bool l = prof.side('L') != nullptr, rr = prof.side('R') != nullptr;
            row("SPEAKERS", l && rr ? "LEFT + RIGHT" : (l ? "LEFT (ON BOTH)" : "RIGHT (ON BOTH)"));
            std::snprintf(b, sizeof b, "%.1f dB", design.preampDb); row("PREAMP", b);
            std::snprintf(b, sizeof b, "%.2f / %.2f ms", design.delayMs[0], design.delayMs[1]); row("DELAY L / R", b);
            double lo = 0, hi = 0;
            for (int c = 0; c < prof.channels; ++c)
                for (double v : prof.ch[c].correction) { lo = std::min(lo, v); hi = std::max(hi, v); }
            const double a = value(kAmount) / 100.0;
            std::snprintf(b, sizeof b, "%.1f / +%.1f dB", lo * a, hi * a); row("CUT / BOOST", b);
        }
        std::snprintf(b, sizeof b, "%d SAMPLES", plug ? plug->engine.latency() : 0);
        row("LATENCY", b);
    }

    void drawGraph()
    {
        const Rect& r = graphR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "CORRECTION");
        const Rect top(r.x + 34, r.y + 26, r.w - 44, (r.h - 60) * 0.64f);
        const Rect bot(top.x, top.b() + 10, top.w, r.b() - 22 - (top.b() + 10));
        auto lx = [&](double f) { return (float)(top.x + top.w * std::log(f / 20.0) / std::log(1000.0)); };
        // grid
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = lx(f);
            line(x, top.y, x, top.b(), theme.grid, 1.0f);
            line(x, bot.y, x, bot.b(), theme.grid, 1.0f);
            char b[16];
            if (f >= 1000.0) std::snprintf(b, sizeof b, "%gk", f / 1000.0); else std::snprintf(b, sizeof b, "%g", f);
            textAt(x, bot.b() + 9, b, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        strokeRect(top, theme.ink, 1.0f);
        strokeRect(bot, theme.ink, 1.0f);
        if (!prof.valid())
        {
            textAt(top.cx(), top.cy(), "LOAD A PROFILE MADE WITH THE PUMP ROOM APP", kLabel, 11.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
            return;
        }
        const double a = value(kAmount) / 100.0;
        // levels: the target's 200 Hz..2 kHz average at 0 dB
        const room::ProfileChannel& ref = prof.ch[0];
        double off = 0.0; int k = 0;
        for (size_t i = 0; i < ref.f.size(); ++i) if (ref.f[i] >= 200 && ref.f[i] <= 2000) { off += ref.target[i]; ++k; }
        off = k ? -off / k : 0.0;
        const float yTop = 15.0f, yBot = -25.0f, cMax = 15.0f;
        auto ly = [&](double db) { return (float)(top.y + top.h * (yTop - pump::clampv(db, (double)yBot, (double)yTop)) / (yTop - yBot)); };
        auto cy = [&](double db) { return (float)(bot.y + bot.h * (cMax - pump::clampv(db, (double)-cMax, (double)cMax)) / (2 * cMax)); };
        for (float db : { 10.0f, 0.0f, -10.0f, -20.0f })
        {
            line(top.x, ly(db), top.r(), ly(db), theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%+.0f", db);
            textAt(top.x - 4, ly(db), db == 0.0f ? "0" : b, kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        line(bot.x, cy(0), bot.r(), cy(0), theme.dim, 1.0f);
        textAt(bot.x - 4, cy(10), "+10", kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        textAt(bot.x - 4, cy(-10), "-10", kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);

        auto curve = [&](const Rect& clip, const std::vector<double>& f, auto valueAt, auto yOf, const Color& c, float w) {
            save();
            scissor(clip.x, clip.y, clip.w, clip.h);
            beginPath();
            for (size_t i = 0; i < f.size(); ++i)
            {
                const float x = lx(f[i]), y = yOf(valueAt(i));
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(c);
            strokeWidth(w);
            lineJoin(ROUND);
            stroke();
            restore();
        };
        const Color cols[2] = { theme.ink, theme.second };
        for (int c = 0; c < prof.channels; ++c)
        {
            const room::ProfileChannel& p = prof.ch[c];
            const Color col = cols[p.side == 'R' ? 1 : 0];
            curve(top, p.f, [&](size_t i) { return p.measured[i] + off; }, ly, pump::withAlpha(col, 0.35f), 1.25f);
            curve(top, p.f, [&](size_t i) { return p.measured[i] + a * p.correction[i] + off; }, ly, col, 2.0f);
            curve(bot, p.f, [&](size_t i) { return a * p.correction[i]; }, cy, col, 1.75f);
        }
        // the target, dotted
        for (size_t i = 0; i + 1 < ref.f.size(); i += 3)
        {
            const float x0 = lx(ref.f[i]), x1 = lx(ref.f[std::min(i + 1, ref.f.size() - 1)]);
            line(x0, ly(ref.target[i] + off), x1, ly(ref.target[i + 1] + off), theme.ink2, 1.5f);
        }
        textAt(top.x + 8, top.y + 10, "MEASURED (FAINT) · CORRECTED (BOLD) · TARGET (DOTTED)", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        float lxk = top.r() - 8;
        for (int c = prof.channels - 1; c >= 0; --c)
        {
            const char* nm = prof.ch[c].side == 'R' ? "RIGHT" : "LEFT";
            const float w = textWidth(nm, kLabel, 9.0f, 0.6f);
            textAt(lxk, top.y + 10, nm, kLabel, 9.0f, cols[prof.ch[c].side == 'R' ? 1 : 0], ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
            lxk -= w + 12;
        }
    }

private:
    PluginRoom* plug = nullptr;
    Dial* amountDial = nullptr;
    Dial* outDial = nullptr;
    Segmented* phaseSeg = nullptr;
    Button* alignBtn = nullptr;
    Button* loadBtn = nullptr;
    Button* clearBtn = nullptr;
    Rect profR, graphR, meterL, meterR, ctlPanel;
    OutMeter outMeter;
    room::Profile prof;
    Design design;
    std::string error, file;
    unsigned seenProfile = ~0u, seenDesign = ~0u;
};

UI* createUI() { return new UIRoom(); }

END_NAMESPACE_DISTRHO
