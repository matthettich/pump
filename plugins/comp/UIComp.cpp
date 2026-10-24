// pump COMP: interface (in the Simple look). A scrolling level/gain-reduction history with a draggable
// threshold line, a transfer-curve inset, In / Out / GR meters with peak hold, and the controls.
#include "ui/PumpUI.hpp"
#include "PluginComp.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::comp;

// Value text for this plugin's controls (more precise than the generic ParamSpec text, and "OFF" end stops).
static std::string compFormat(const ParamSpec& sp, int param, float v)
{
    char b[48];
    switch (param)
    {
    case kThreshold: std::snprintf(b, sizeof b, "%.1f dB", v); return b;
    case kKnee: std::snprintf(b, sizeof b, "%.1f dB", v); return b;
    case kRange: if (v >= 59.95f) return "60 dB"; std::snprintf(b, sizeof b, v < 9.95f ? "%.1f dB" : "%.0f dB", v); return b;
    case kAttack:
        if (v < 0.0995f) std::snprintf(b, sizeof b, "%.3f ms", v);
        else if (v < 9.95f) std::snprintf(b, sizeof b, "%.2f ms", v);
        else if (v < 99.5f) std::snprintf(b, sizeof b, "%.1f ms", v);
        else std::snprintf(b, sizeof b, "%.0f ms", v);
        return b;
    case kHold: if (v < 0.05f) return "0 ms"; std::snprintf(b, sizeof b, v < 9.95f ? "%.1f ms" : "%.0f ms", v); return b;
    case kLookahead: if (v < 0.005f) return "0 ms"; std::snprintf(b, sizeof b, "%.1f ms", v); return b;
    case kScHpf: if (!hpfActive(v)) return "OFF"; break;
    case kScLpf: if (!lpfActive(v)) return "OFF"; break;
    default: break;
    }
    return sp.format(v);
}

// Dial with this plugin's value text (same drawing as the shared Dial).
struct CompDial : Dial {
    void draw(PumpUI& ui) override
    {
        const Theme& t = ui.theme;
        const bool dis = !enabled;
        const float rad = radius();
        const float cx = r.cx(), cy = r.y + 16.0f + rad + 2.0f;
        ui.textAt(cx, r.y + 7.0f, label, PumpUI::kLabel, size == Small ? 10.5f : 11.5f, dis ? t.dim : t.ink2, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 1.0f);
        const float a0 = (float)(pump::kPi * 0.75), a1 = (float)(pump::kPi * 2.25);
        const float n = ui.norm(param);
        const float sw = size == Small ? 4.0f : (size == Large ? 6.0f : 5.0f);
        ui.beginPath(); ui.arc(cx, cy, rad, a0, a1, NanoVG::CW); ui.strokeColor(t.dim); ui.strokeWidth(sw); ui.stroke();
        const float from = bipolar ? (float)(pump::kPi * 1.5) : a0;
        const float to = a0 + (a1 - a0) * n;
        if (std::fabs(to - from) > 0.01f)
        {
            ui.beginPath();
            ui.arc(cx, cy, rad, std::min(from, to), std::max(from, to), NanoVG::CW);
            ui.strokeColor(dis ? t.dim : t.neon); ui.strokeWidth(sw); ui.stroke();
        }
        ui.beginPath(); ui.circle(cx, cy, rad - 5.0f); ui.fillColor(t.bg); ui.fill();
        ui.strokeColor(dis ? t.dim : t.ink); ui.strokeWidth(1.5f); ui.stroke();
        ui.line(cx + std::cos(to) * (rad - 14.0f) * 0.25f, cy + std::sin(to) * (rad - 14.0f) * 0.25f,
                cx + std::cos(to) * (rad - 6.5f), cy + std::sin(to) * (rad - 6.5f), dis ? t.dim : t.ink, 2.2f);
        ui.textAt(cx, cy + rad + 12.0f, compFormat(ui.spec(param), param, ui.value(param)), PumpUI::kMono, size == Small ? 10.0f : 11.0f,
                  dis ? t.dim : t.ink, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
    }
};

class UIComp : public PumpUI {
public:
    UIComp() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::comp(), "COMP", 1000, 620, kBypass)
    {
        styleSeg = addSegmented(kStyle, { "CLEAN", "GLUE", "OPTO", "PUNCH", "VARI-MU", "PUMP" });
        styleSeg->tip = "Compressor style";
        autoRelBtn = addToggle(kAutoRelease, "AUTO REL");
        autoRelBtn->small = true;
        autoRelBtn->tip = "Auto release: program-dependent (fast after short peaks, slow under sustained compression)";

        thrDial = dial(kThreshold, "THRESHOLD", 1, "Threshold: where compression starts (or drag the line in the graph)");
        ratioDial = dial(kRatio, "RATIO", 1, "Ratio (INF at the end): or Ctrl/Cmd + drag up/down on the transfer curve");
        kneeDial = dial(kKnee, "KNEE", 1, "Knee width: soft transition around the threshold (or mouse wheel on the transfer curve)");
        rangeDial = dial(kRange, "RANGE", 1, "Range: the most gain reduction allowed");
        atkDial = dial(kAttack, "ATTACK", 1, "Attack: how fast it reacts");
        relDial = dial(kRelease, "RELEASE", 1, "Release: how fast it recovers");
        holdDial = dial(kHold, "HOLD", 1, "Hold: keeps the reduction this long before releasing");
        laDial = dial(kLookahead, "LOOKAHEAD", 1, "Lookahead: reacts before the audio arrives (adds this much latency)");

        chanMenu = addMenu(kChannelMode, "CH");
        chanMenu->tip = "Channels: Stereo (L/R), Mid/Side (both, linked by Stereo Link), Mid only or Side only";
        osMenu = addMenu(kOversampling, "OS");
        osMenu->tip = "Oversampling: detector, gain and tube colour at 2x or 4x (adds a little latency)";
        autoGainBtn = addToggle(kAutoMakeup, "AUTO GAIN");
        autoGainBtn->small = true;
        autoGainBtn->tip = "Auto makeup gain: compensates for the curve (half the reduction at 0 dBFS)";
        inDial = dial(kInGain, "INPUT", 2, "Input gain (before the compressor and the dry signal)", true);
        outDial = dial(kOutGain, "OUTPUT", 2, "Output gain", true);
        mixDial = dial(kMix, "MIX", 2, "Mix: dry/wet for parallel compression (latency-aligned)");
        linkDial = dial(kLink, "LINK", 2, "Stereo link: 0% each channel on its own, 100% both follow the louder");

        scMenu = addMenu(kScSource, "");
        scMenu->tip = "Sidechain: the input itself, or the external sidechain bus (inputs 3-4)";
        scHpDial = dial(kScHpf, "HP", 3, "Sidechain high-pass: keeps lows from triggering compression (left end = off)", false, Dial::Small);
        scLpDial = dial(kScLpf, "LP", 3, "Sidechain low-pass (right end = off)", false, Dial::Small);
        scGainDial = dial(kScGain, "SC GAIN", 3, "Sidechain gain: drives the detector harder or softer", true, Dial::Small);
        // listening is monitoring, not an edit: it stays out of the undo history
        listenBtn = addButton("LISTEN", [this]() { setListen(value(kScListen) < 0.5f); }, [this]() { return value(kScListen) >= 0.5f; });
        listenBtn->holdMomentary = true;
        listenBtn->tip = "Hear the (filtered) sidechain: click toggles, click-and-hold listens while held";

        rangeBtn = addButton("48 dB", [this]() {
            static const int rr[] = { 24, 36, 48, 60, 72 };
            std::vector<std::string> it;
            int cur = 2;
            for (int i = 0; i < 5; ++i) { it.push_back(std::to_string(rr[i]) + " dB"); if ((int)dispRange == rr[i]) cur = i; }
            openMenu(rangeBtn->r, it, cur, [this](int i) { static const int r2[] = { 24, 36, 48, 60, 72 }; dispRange = (float)r2[i]; updateRangeLabel(); saveUi(); });
        });
        rangeBtn->small = true;
        rangeBtn->tip = "Display range of the history graph";

        if (auto* p = static_cast<PluginComp*>(pluginPtr())) plug = p;
        updateRangeLabel();
    }

protected:
    CompDial* dial(int param, const char* label, int group, const char* tip, bool bipolar = false, Dial::Size sz = Dial::Medium)
    {
        CompDial* d = add(new CompDial());
        d->param = param; d->label = label; d->size = sz; d->bipolar = bipolar; d->tabGroup = group; d->tip = tip;
        return d;
    }

    // ---------------------------------------------------------------- layout
    void layout() override
    {
        graph = Rect(14, 58, 640, 392);
        // meters: In L/R | scale | Out L/R | GR
        const float my = graph.y + 26.0f, mh = graph.h - 26.0f - 20.0f;
        inM[0] = Rect(664, my, 8, mh); inM[1] = Rect(673, my, 8, mh);
        outM[0] = Rect(706, my, 8, mh); outM[1] = Rect(715, my, 8, mh);
        grM = Rect(744, my, 12, mh);
        inHit = Rect(658, graph.y, 30, graph.h); outHit = Rect(700, graph.y, 30, graph.h); grHit = Rect(734, graph.y, 40, graph.h);
        rangeBtn->r = Rect(graph.r() - 56, graph.y + 8, 48, 22);
        // right column: transfer inset, sidechain
        const float rx = 786.0f, rw = 200.0f;
        inset = Rect(rx, graph.y, rw, 200);
        scCaptionY = inset.b() + 18.0f;
        scMenu->r = Rect(rx, inset.b() + 30.0f, 124, 26);
        listenBtn->r = Rect(rx + 132, inset.b() + 30.0f, rw - 132, 26);
        listenBtn->small = true;
        const float dy = scMenu->r.b() + 14.0f;
        scHpDial->r = Rect(rx + 2, dy, 62, 70);
        scLpDial->r = Rect(rx + 69, dy, 62, 70);
        scGainDial->r = Rect(rx + 136, dy, 62, 70);
        detBar = Rect(rx, graph.b() - 10.0f, rw, 8.0f);
        // bottom: style + dynamics (left), output (right)
        const float rowA = 468.0f, rowB = 510.0f;
        styleSeg->r = Rect(14, rowA, 528, 28);
        autoRelBtn->r = Rect(556, rowA, 98, 28);
        Dial* row[8] = { thrDial, ratioDial, kneeDial, rangeDial, atkDial, relDial, holdDial, laDial };
        for (int i = 0; i < 8; ++i) row[i]->r = Rect(14 + 80.0f * i + 8, rowB, 64, Dial::heightFor(Dial::Medium));
        const float bx = 664.0f, bw = W - 14.0f - bx;
        chanMenu->r = Rect(bx, rowA, 146, 28);
        osMenu->r = Rect(bx + 154, rowA, 72, 28);
        autoGainBtn->r = Rect(bx + 234, rowA, bw - 234, 28);
        Dial* row2[4] = { inDial, outDial, mixDial, linkDial };
        for (int i = 0; i < 4; ++i) row2[i]->r = Rect(bx + bw * i / 4.0f + (bw / 4.0f - 64) * 0.5f, rowB, 64, Dial::heightFor(Dial::Medium));
        rebind();
    }

    void setListen(bool on)
    {
        const bool was = fApplyingHistory;
        fApplyingHistory = true;   // not an undo step
        setParamGesture(kScListen, on ? 1.0f : 0.0f);
        fApplyingHistory = was;
        setStatus(on ? "Listening to the sidechain" : "Sidechain listen off");
    }
    // Typed-entry text with full precision (the shared entry rounds dB values >= 10 to whole numbers).
    std::string entryText(int p) const
    {
        const float v = value(p);
        char b[32];
        switch (p)
        {
        case kRatio: if (v >= 99.95f) return "inf"; std::snprintf(b, sizeof b, "%.2f", v); return b;
        case kScHpf: case kScLpf:
            if ((p == kScHpf && !hpfActive(v)) || (p == kScLpf && !lpfActive(v))) return "off";
            if (v >= 1000.0f) { std::snprintf(b, sizeof b, "%.2fk", v / 1000.0f); return b; }
            std::snprintf(b, sizeof b, "%.0f", v); return b;
        default: {
            std::string t = compFormat(fSpecs[p], p, v);
            const size_t sp = t.find(' ');
            if (sp != std::string::npos && !fSpecs[p].isChoice()) t = t.substr(0, sp);
            if (t.size() > 1 && t[0] == '+') t = t.substr(1);
            if (t == "OFF") t = "0";
            return t;
        }
        }
    }
    void fixEntry(bool wasActive, int wasParam)
    {
        if (fEdit.active && (!wasActive || fEdit.param != wasParam)) { fEdit.text = entryText(fEdit.param); fEdit.selectAll = true; }
    }
    bool onMouse(const MouseEvent& ev) override
    {
        const bool wa = fEdit.active; const int wp = fEdit.param;
        const bool r = PumpUI::onMouse(ev);
        fixEntry(wa, wp);
        return r;
    }
    static const char* styleTip(int i)
    {
        static const char* const t[] = {
            "Clean: feed-forward peak detector, exact curve, lowest distortion",
            "Glue: bus compression; release slows as it compresses deeper",
            "Opto: smooth RMS leveling, fast-then-slow release, softer at depth",
            "Punch: feedback design that lets the first milliseconds of transients through",
            "Vari-Mu: tube; the ratio rises with level, gentle colour",
            "Pump: aggressive ducking with a breathing release (try the external sidechain)",
        };
        return t[clampi(i, 0, 5)];
    }
    static int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

    void rebind()
    {
        const int mode = (int)std::lround(value(kChannelMode));
        linkDial->enabled = mode <= 1;
    }
    void onParamChanged(int, float) override { rebind(); }
    void onPresetApplied() override { rebind(); }

    // ---------------------------------------------------------------- scales
    static constexpr float kTopDb = 3.0f;
    static constexpr float kDecayPerCol = 1.1f;    // display release: 1.1 dB per 7.5 ms column (~150 dB/s)
    float pxPerDb() const { return graph.h / dispRange; }
    float yOfDb(float db) const { return graph.y + (kTopDb - db) * pxPerDb(); }
    float dbOfY(float y) const { return kTopDb - (y - graph.y) / pxPerDb(); }
    float yOfGr(float gr) const { return graph.y + std::min(0.0f, gr) * -pxPerDb(); }
    static constexpr float kInLo = -60.0f, kInHi = 0.0f;
    float insetX(float db) const { return inset.x + (db - kInLo) / (kInHi - kInLo) * inset.w; }
    float insetY(float db) const { return inset.b() - (db - kInLo) / (kInHi - kInLo) * inset.h; }
    float insetDbOfX(float x) const { return kInLo + (x - inset.x) / inset.w * (kInHi - kInLo); }
    static constexpr float kMeterTop = 3.0f, kMeterBot = -60.0f, kGrRange = 24.0f;

    Curve uiCurve() const
    {
        Curve c;
        c.set((int)std::lround(value(kStyle)), value(kThreshold), value(kRatio), value(kKnee), value(kRange));
        return c;
    }
    std::string fmt(int p) const { return compFormat(fSpecs[p], p, value(p)); }

    // ---------------------------------------------------------------- meters
    void tick(float dt) override
    {
        if (!plug) return;
        Engine& e = plug->engine;
        for (int c = 0; c < 2; ++c)
        {
            const float pi = pump::gainToDbF(e.inPeak[c].exchange(0.0f)), po = pump::gainToDbF(e.outPeak[c].exchange(0.0f));
            inLvl[c] = std::max(pi, inLvl[c] - 26.0f * dt);
            outLvl[c] = std::max(po, outLvl[c] - 26.0f * dt);
            const float pkI = std::max(pi, -120.0f), pkO = std::max(po, -120.0f);
            if (pkI >= inHold) { inHold = pkI; inHoldT = 2.0f; }
            if (pkO >= outHold) { outHold = pkO; outHoldT = 2.0f; }
        }
        if ((inHoldT -= dt) <= 0.0f) inHold = std::max(std::max(inLvl[0], inLvl[1]), inHold - 15.0f * dt);
        if ((outHoldT -= dt) <= 0.0f) outHold = std::max(std::max(outLvl[0], outLvl[1]), outHold - 15.0f * dt);
        const float g = std::min(e.grMin[0].exchange(0.0f), e.grMin[1].exchange(0.0f));
        grNow = g < grNow ? g : grNow + (g - grNow) * std::min(1.0f, dt * 14.0f);
        if (g <= grHold) { grHold = g; grHoldT = 2.0f; }
        else if ((grHoldT -= dt) <= 0.0f) grHold = std::min(grNow, grHold + 10.0f * dt);
        const float d = e.detLevel.exchange(-200.0f);
        detNow = d > detNow ? d : std::max(d, detNow - 30.0f * dt);
        const float sc = e.scPeak.exchange(0.0f);
        scQuiet = sc > 1e-5f ? 0.0f : scQuiet + dt;
    }

    // ---------------------------------------------------------------- drawing
    void drawBody() override
    {
        drawHistory();
        drawMeters();
        drawInset();
        caption(inset.x, scCaptionY, "SIDECHAIN");
        drawDetectorBar();
        if ((int)std::lround(value(kScSource)) == 1)
        {
            const bool quiet = scQuiet > 0.6f || (plug && !plug->sidechainConnected());
            const char* t = quiet ? "NO SIGNAL" : "ACTIVE";
            textAt(inset.r(), scCaptionY, t, kLabel, 10.0f, quiet ? theme.red : theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        }
        // bottom panel separators
        line(14, 458.0f, W - 14, 458.0f, theme.dim, 1.0f);
        line(659.0f, 468.0f, 659.0f, 586.0f, theme.dim, 1.0f);
    }

    void drawHistory()
    {
        screenBox(graph);
        save();
        scissor(graph.x + 1, graph.y + 1, graph.w - 2, graph.h - 2);
        const Curve cv = uiCurve();
        // knee band and grid
        const float thr = value(kThreshold);
        float kneeW = value(kKnee);
        if (cv.style == (int)Style::VariMu) kneeW += 12.0f;
        if (kneeW > 0.1f)
        {
            // the knee: a soft band fading out from the threshold
            const float yA = yOfDb(thr + 0.5f * kneeW), yT = yOfDb(thr), yB = yOfDb(thr - 0.5f * kneeW);
            const Color c0 = withAlpha2(theme.ink, 0.0f), c1 = withAlpha2(theme.ink, theme.dark ? 0.07f : 0.05f);
            beginPath(); rect(graph.x, yA, graph.w, yT - yA); fillPaint(linearGradient(0, yA, 0, yT, c0, c1)); fill();
            beginPath(); rect(graph.x, yT, graph.w, yB - yT); fillPaint(linearGradient(0, yT, 0, yB, c1, c0)); fill();
        }
        const float stepDb = dispRange <= 36.0f ? 6.0f : 12.0f;
        for (float db = 0.0f; db > kTopDb - dispRange; db -= stepDb)
        {
            const float y = yOfDb(db);
            line(graph.x, y, graph.r(), y, db == 0.0f ? withAlpha2(theme.ink, 0.22f) : theme.grid, 1.0f);
        }
        // history: columns of 3 points (400 points/s -> 4.8 s across)
        const int cols = (int)graph.w, step = 3;
        bool have = false;
        uint32_t endIdx = 0;
        if (plug)
        {
            Engine& e = plug->engine;
            endIdx = e.histDet.smoothPosition();
            endIdx -= endIdx % step;
            const uint32_t need = (uint32_t)(cols * step);
            for (int k = 0; k < cols; ++k)
            {
                float a = -200.0f, b = -200.0f, g = 0.0f;
                for (int j = 0; j < step; ++j)
                {
                    const int64_t idx = (int64_t)endIdx - (int64_t)need + (int64_t)k * step + j;
                    if (idx < 0) continue;
                    a = std::max(a, e.histIn.at((uint32_t)idx));
                    b = std::max(b, e.histOut.at((uint32_t)idx));
                    g = std::min(g, e.histGr.at((uint32_t)idx));
                }
                // display ballistics (meter-like release, in time order): a soft envelope instead of raw peaks
                if (k > 0) { a = std::max(a, hin[k - 1] - kDecayPerCol); b = std::max(b, hout[k - 1] - kDecayPerCol); }
                hin[k] = a; hout[k] = b; hgr[k] = g;
            }
            have = true;
            // one faint line per second, scrolling with the audio
            for (int k = 0; k < cols; ++k)
            {
                const int64_t i0 = (int64_t)endIdx - (int64_t)need + (int64_t)k * step;
                if (i0 >= 0 && (i0 % 400) < step) line(graph.x + k + 0.5f, graph.y, graph.x + k + 0.5f, graph.b(), theme.grid, 1.0f);
            }
        }
        if (have)
        {
            auto yc = [&](float db) { return std::min(graph.b() + 2.0f, std::max(graph.y, yOfDb(db))); };
            auto trace = [&](const float* v) {
                beginPath();
                for (int k = 0; k < cols; ++k) { const float y = yc(v[k]); if (k == 0) moveTo(graph.x + 0.5f, y); else lineTo(graph.x + k + 0.5f, y); }
            };
            auto area = [&](const float* v) {
                beginPath(); moveTo(graph.x, graph.b() + 2.0f);
                for (int k = 0; k < cols; ++k) lineTo(graph.x + k + 0.5f, yc(v[k]));
                lineTo(graph.r(), graph.b() + 2.0f); closePath();
            };
            // input: soft filled level, ink-tinted; output: neon on top; the input's edge drawn over both
            area(hin); fillColor(withAlpha2(theme.ink, theme.dark ? 0.13f : 0.09f)); fill();
            area(hout); fillColor(withAlpha2(theme.neon, theme.dark ? 0.30f : 0.42f)); fill();
            trace(hout); strokeColor(theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.40f)); strokeWidth(1.4f); lineJoin(ROUND); stroke();
            trace(hin); strokeColor(withAlpha2(theme.ink, theme.dark ? 0.45f : 0.38f)); strokeWidth(1.0f); stroke();
            // gain reduction: thick line from the top
            beginPath(); moveTo(graph.x, graph.y);
            for (int k = 0; k < cols; ++k) lineTo(graph.x + k + 0.5f, yOfGr(hgr[k]));
            lineTo(graph.r(), graph.y); closePath();
            fillColor(withAlpha2(theme.second, theme.dark ? 0.16f : 0.13f)); fill();
            beginPath();
            for (int k = 0; k < cols; ++k) { const float y = yOfGr(hgr[k]); if (k == 0) moveTo(graph.x + 0.5f, y); else lineTo(graph.x + k + 0.5f, y); }
            strokeColor(theme.second); strokeWidth(2.6f); lineJoin(ROUND); stroke();
        }
        // dB labels (left edge, on a small backing so they read over the waveforms)
        for (float db = 0.0f; db > kTopDb - dispRange + 2.0f; db -= stepDb)
        {
            char b[16]; std::snprintf(b, sizeof b, "%.0f", db);
            const std::string t = db == 0.0f ? "0 dB" : b;
            const float tw = textWidth(t, kMono, 9.5f);
            fillRect(Rect(graph.x + 4, yOfDb(db) - 14.0f, tw + 6.0f, 12.0f), withAlpha2(theme.screen, 0.85f));
            textAt(graph.x + 7, yOfDb(db) - 8.0f, t, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        // threshold line + tag
        const float ty = yOfDb(thr);
        const bool hot = dmode == dmThresh || hoverThresh;
        line(graph.x, ty, graph.r(), ty, hot ? theme.ink : withAlpha2(theme.ink, 0.75f), hot ? 2.0f : 1.5f);
        const Rect tag = threshTag();
        fillRect(tag, theme.ink);
        textAt(tag.cx(), tag.cy() + 0.5f, "THRESHOLD  " + fmt(kThreshold), kLabel, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        // external sidechain without signal
        if ((int)std::lround(value(kScSource)) == 1 && (scQuiet > 0.6f || (plug && !plug->sidechainConnected())))
        {
            const std::string s = "EXTERNAL SIDECHAIN: NO SIGNAL";
            const float tw = textWidth(s, kLabel, 11.0f, 1.2f) + 20.0f;
            const Rect b(graph.cx() - tw * 0.5f, graph.b() - 40.0f, tw, 24);
            fillRect(b, theme.bg); strokeRect(b, theme.red, 1.5f);
            textAt(b.cx(), b.cy() + 0.5f, s, kLabel, 11.0f, theme.red, ALIGN_CENTER | ALIGN_MIDDLE, 1.2f);
        }
        if (value(kScListen) >= 0.5f)
        {
            const std::string s = "LISTENING TO THE SIDECHAIN";
            const float tw = textWidth(s, kLabel, 11.0f, 1.2f) + 20.0f;
            const Rect b(graph.cx() - tw * 0.5f, graph.y + 10.0f, tw, 24);
            fillRect(b, theme.second);
            textAt(b.cx(), b.cy() + 0.5f, s, kLabel, 11.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.2f);
        }
        restore();
        // legend (bottom right, inside, on its own background)
        const float ly = graph.b() - 15.0f;
        {
            const float lw = textWidth("GAIN REDUCTION", kLabel, 10.0f, 1.0f) + textWidth("OUTPUT", kLabel, 10.0f, 1.0f) + textWidth("INPUT", kLabel, 10.0f, 1.0f) + 3 * 32.0f + 2.0f;
            const Rect lb(graph.r() - 8.0f - lw, ly - 10.0f, lw, 20.0f);
            fillRect(lb, withAlpha2(theme.screen, 0.92f));
            strokeRect(lb, theme.dim, 1.0f);
        }
        float lx = graph.r() - 16.0f;
        auto legend = [&](const char* s, const Color& c, bool lineStyle) {
            const float tw = textWidth(s, kLabel, 10.0f, 1.0f);
            textAt(lx, ly, s, kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
            lx -= tw + 6.0f;
            if (lineStyle) line(lx - 12.0f, ly, lx, ly, c, 2.6f);
            else fillRect(Rect(lx - 12.0f, ly - 4.0f, 12.0f, 8.0f), c);
            lx -= 12.0f + 14.0f;
        };
        legend("GAIN REDUCTION", theme.second, true);
        legend("OUTPUT", withAlpha2(theme.neon, theme.dark ? 0.7f : 0.8f), false);
        legend("INPUT", withAlpha2(theme.ink, theme.dark ? 0.3f : 0.22f), false);
    }
    Rect threshTag()
    {
        const std::string s = "THRESHOLD  " + fmt(kThreshold);
        const float tw = textWidth(s, kLabel, 11.0f, 0.8f) + 16.0f;
        const float ty = yOfDb(value(kThreshold));
        float y = ty - 20.0f;
        if (y < graph.y + 4.0f) y = ty + 4.0f;
        return Rect(graph.x + 64.0f, y, tw, 18.0f);
    }

    // The detector's level (what the compressor hears, after the sidechain filters) against the threshold.
    void drawDetectorBar()
    {
        const Rect b = detBar;
        textAt(b.x, b.y - 10.0f, "DETECTOR", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        char t[24];
        if (detNow < -99.0f) std::snprintf(t, sizeof t, "-INF"); else std::snprintf(t, sizeof t, "%.1f dB", detNow);
        textAt(b.r(), b.y - 10.0f, t, kMono, 10.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        fillRect(b, theme.panel2);
        auto xOf = [&](float d) { return b.x + (std::max(-60.0f, std::min(0.0f, d)) + 60.0f) / 60.0f * b.w; };
        const float thr = value(kThreshold);
        if (detNow > -60.0f)
        {
            const float x1 = xOf(detNow), xt = xOf(thr);
            fillRect(Rect(b.x, b.y, std::min(x1, xt) - b.x, b.h), theme.neon);
            if (x1 > xt) fillRect(Rect(xt, b.y, x1 - xt, b.h), theme.second);
        }
        strokeRect(b, theme.ink, 1.0f);
        const float xt = xOf(thr);
        line(xt, b.y - 3.0f, xt, b.b() + 3.0f, theme.ink, 2.0f);
    }

    void drawMeters()
    {
        auto meter = [&](const Rect& r, float lvl, float hold) { drawLevelMeter(r, lvl, hold, kMeterBot, kMeterTop); };
        meter(inM[0], inLvl[0], -200.0f); meter(inM[1], inLvl[1], -200.0f);
        meter(outM[0], outLvl[0], -200.0f); meter(outM[1], outLvl[1], -200.0f);
        // held peaks across both channels
        auto yLv = [&](const Rect& r, float d) { return r.b() - (std::max(kMeterBot, std::min(kMeterTop, d)) - kMeterBot) / (kMeterTop - kMeterBot) * r.h; };
        if (inHold > kMeterBot) line(inM[0].x, yLv(inM[0], inHold), inM[1].r(), yLv(inM[0], inHold), inHold > 0.0f ? theme.red : theme.ink, 1.5f);
        if (outHold > kMeterBot) line(outM[0].x, yLv(outM[0], outHold), outM[1].r(), yLv(outM[0], outHold), outHold > 0.0f ? theme.red : theme.ink, 1.5f);
        drawGRMeter(grM, grNow, kGrRange);
        if (grHold < -0.05f)
        {
            const float y = grM.y + std::min(1.0f, -grHold / kGrRange) * grM.h;
            line(grM.x, y, grM.r(), y, theme.ink, 1.5f);
        }
        // level scale between In and Out
        const float sx = 0.5f * (inM[1].r() + outM[0].x);
        for (float d : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
        {
            char b[8]; std::snprintf(b, sizeof b, "%.0f", std::fabs(d));
            textAt(sx, yLv(inM[0], d), b, kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // GR scale on its right
        for (float d : { 3.0f, 6.0f, 12.0f, 18.0f, 24.0f })
        {
            const float y = grM.y + d / kGrRange * grM.h;
            char b[8]; std::snprintf(b, sizeof b, "%.0f", d);
            line(grM.r(), y, grM.r() + 3.0f, y, theme.ink2, 1.0f);
            textAt(grM.r() + 5.0f, y, b, kMono, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        // readouts (click a meter to reset its peak hold) and labels
        auto num = [](float v, bool gr) {
            char b[16];
            if (gr) { if (v > -0.05f) return std::string("0.0"); std::snprintf(b, sizeof b, "%.1f", v); }
            else { if (v < -99.0f) return std::string("-INF"); std::snprintf(b, sizeof b, "%.1f", v); }
            return std::string(b);
        };
        const float ry = graph.y + 9.0f;
        const float cIn = 0.5f * (inM[0].x + inM[1].r()), cOut = 0.5f * (outM[0].x + outM[1].r());
        textAt(cIn, ry, num(inHold, false), kMono, 9.5f, inHold > 0.0f ? theme.red : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(cOut, ry, num(outHold, false), kMono, 9.5f, outHold > 0.0f ? theme.red : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(grM.cx() + 2.0f, ry, num(grHold, true), kMono, 9.5f, theme.second, ALIGN_CENTER | ALIGN_MIDDLE);
        const float by = graph.b() - 7.0f;
        textAt(cIn, by, "IN", kLabel, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        textAt(cOut, by, "OUT", kLabel, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        textAt(grM.cx() + 2.0f, by, "GR", kLabel, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
    }

    void drawInset()
    {
        screenBox(inset);
        save();
        scissor(inset.x + 1, inset.y + 1, inset.w - 2, inset.h - 2);
        for (float d = -48.0f; d < 0.0f; d += 12.0f)
        {
            line(insetX(d), inset.y, insetX(d), inset.b(), theme.grid, 1.0f);
            line(inset.x, insetY(d), inset.r(), insetY(d), theme.grid, 1.0f);
        }
        // 1:1
        line(insetX(kInLo), insetY(kInLo), insetX(kInHi), insetY(kInHi), theme.dim, 1.0f);
        const Curve cv = uiCurve();
        const float thr = value(kThreshold);
        line(insetX(thr), inset.y, insetX(thr), inset.b(), withAlpha2(theme.ink, 0.25f), 1.0f);
        // curve (fill below, then line)
        const int NP = 120;
        beginPath(); moveTo(inset.x, inset.b());
        for (int p = 0; p <= NP; ++p)
        {
            const float din = kInLo + (kInHi - kInLo) * p / NP;
            lineTo(insetX(din), insetY(din + (float)cv.gain(din)));
        }
        lineTo(inset.r(), inset.b()); closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.20f : 0.32f)); fill();
        if (theme.dark)
        {
            beginPath();
            for (int p = 0; p <= NP; ++p) { const float din = kInLo + (kInHi - kInLo) * p / NP; const float x = insetX(din), y = insetY(din + (float)cv.gain(din)); if (p == 0) moveTo(x, y); else lineTo(x, y); }
            strokeColor(theme.glow); strokeWidth(6.0f); stroke();
        }
        beginPath();
        for (int p = 0; p <= NP; ++p) { const float din = kInLo + (kInHi - kInLo) * p / NP; const float x = insetX(din), y = insetY(din + (float)cv.gain(din)); if (p == 0) moveTo(x, y); else lineTo(x, y); }
        strokeColor(theme.dark ? theme.neon : theme.ink); strokeWidth(2.2f); lineJoin(ROUND); stroke();
        // the current operating point: detector level and the reduction applied right now
        if (detNow > kInLo - 6.0f)
        {
            const float x = insetX(std::min(kInHi, detNow)), y = insetY(std::min(kInHi, detNow + grNow));
            beginPath(); circle(x, y, 5.0f); fillColor(theme.second); fill();
            strokeColor(theme.ink); strokeWidth(1.5f); stroke();
        }
        restore();
        // text: style and ratio (top left), knee (bottom right)
        std::string sn = kStyleNames[cv.style];
        for (auto& c : sn) c = (char)std::toupper((unsigned char)c);
        textAt(inset.x + 8, inset.y + 12, sn, kLabel, 10.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.2f);
        textAt(inset.x + 8, inset.y + 27, fmt(kRatio), kMono, 12.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(inset.r() - 8, inset.b() - 10, "KNEE " + fmt(kKnee), kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
    }

    std::string footerInfo() override
    {
        const int lat = plug ? plug->latencyNow() : 0;
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        char b[160];
        std::snprintf(b, sizeof b, "%s  %s  %s  LATENCY %.1f MS", kStyleNames[(int)std::lround(value(kStyle))],
                      kChannelNames[(int)std::lround(value(kChannelMode))], kOsNames[(int)std::lround(value(kOversampling))], lat * 1000.0 / sr);
        std::string s = b;
        for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

    // ---------------------------------------------------------------- mouse
    // Conventions:
    //   graph: drag the threshold line (Shift: fine), double-click it to type; wheel: threshold
    //   transfer inset: drag = threshold, Ctrl/Cmd + drag up/down = ratio, wheel = knee (Shift: fine), double-click: type
    //   meters: click to reset the peak hold
    enum DragMode { dmNone = 0, dmThresh, dmInsetThresh, dmInsetRatio };

    bool nearThresh(float x, float y)
    {
        if (!graph.contains(x, y)) return false;
        return std::fabs(y - yOfDb(value(kThreshold))) < 7.0f || threshTag().contains(x, y);
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        const bool cmd = (mod & (kModifierControl | kModifierSuper)) != 0, alt = (mod & kModifierAlt) != 0;
        if (button != kMouseButtonLeft && button != kMouseButtonRight) return false;
        pressX = lastX = x; pressY = lastY = y;
        if (inHit.contains(x, y)) { inHold = std::max(inLvl[0], inLvl[1]); setStatus("Input peak hold reset"); return false; }
        if (outHit.contains(x, y)) { outHold = std::max(outLvl[0], outLvl[1]); setStatus("Output peak hold reset"); return false; }
        if (grHit.contains(x, y)) { grHold = grNow; setStatus("Gain reduction peak hold reset"); return false; }
        if (nearThresh(x, y))
        {
            if (button == kMouseButtonRight || dbl) { openTextEntry(kThreshold, threshTag()); fEdit.text = entryText(kThreshold); return false; }
            if (cmd || alt) { setParamGesture(kThreshold, fSpecs[kThreshold].def); setStatus("Threshold reset to " + fmt(kThreshold)); return false; }
            beginEdit(kThreshold);
            dragStart = value(kThreshold);
            dmode = dmThresh;
            return true;
        }
        if (inset.contains(x, y))
        {
            if (button == kMouseButtonRight || dbl)
            {
                const Rect er(inset.x + 40, inset.cy() - 11, inset.w - 80, 22);
                openTextEntry(kRatio, er, { { kRatio, er }, { kKnee, er }, { kThreshold, er } });
                fEdit.text = entryText(kRatio);
                setStatus("Type the ratio (inf = INF:1); Tab: knee, threshold; Enter: done");
                return false;
            }
            if (cmd)
            {
                beginEdit(kRatio);
                dragStart = norm(kRatio);
                dmode = dmInsetRatio;
                return true;
            }
            beginEdit(kThreshold);
            dragStart = value(kThreshold);
            dmode = dmInsetThresh;
            return true;
        }
        return false;
    }

    void bodyMouseDrag(float x, float y, uint mod) override
    {
        const float fine = (mod & kModifierShift) ? 0.15f : 1.0f;
        const float dx = (x - lastX) * fine, dy = (y - lastY) * fine;
        lastX = x; lastY = y;
        switch (dmode)
        {
        case dmThresh:
            dragStart -= dy / pxPerDb();
            setParam(kThreshold, dragStart);
            setStatus("Threshold  " + fmt(kThreshold) + "    Shift: fine");
            break;
        case dmInsetThresh:
            dragStart += (dx - dy) * (kInHi - kInLo) / inset.w;
            setParam(kThreshold, dragStart);
            setStatus("Threshold  " + fmt(kThreshold) + "    Ctrl/Cmd + drag: ratio · wheel: knee");
            break;
        case dmInsetRatio:
            dragStart -= dy * 0.004f;
            setParamNorm(kRatio, dragStart);
            setStatus("Ratio  " + fmt(kRatio) + "    Shift: fine");
            break;
        default: break;
        }
    }

    void bodyMouseUp(float, float) override
    {
        if (dmode == dmThresh || dmode == dmInsetThresh) endEdit(kThreshold);
        else if (dmode == dmInsetRatio) endEdit(kRatio);
        dmode = dmNone;
    }

    void bodyHover(float x, float y) override
    {
        hoverThresh = nearThresh(x, y);
        if (hoverThresh) setStatus("Threshold  " + fmt(kThreshold) + "    drag (Shift: fine) · double-click: type · Ctrl/Cmd+click: reset");
        else if (graph.contains(x, y))
        {
            char b[160];
            std::snprintf(b, sizeof b, "Level %.1f dB · GR %.1f dB    drag the threshold line · wheel: threshold", dbOfY(y), -(y - graph.y) / pxPerDb());
            setStatus(b);
        }
        else if (inset.contains(x, y))
        {
            const float din = insetDbOfX(x);
            const Curve cv = uiCurve();
            char b[200];
            std::snprintf(b, sizeof b, "In %.1f -> out %.1f dB    drag: threshold · Ctrl/Cmd+drag: ratio · wheel: knee",
                          din, din + cv.gain(din));
            setStatus(b);
        }
        else if (inHit.contains(x, y) || outHit.contains(x, y) || grHit.contains(x, y)) setStatus("Click to reset the peak hold");
    }

    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        const bool shift = (mod & kModifierShift) != 0;
        const float dir = dy > 0 ? 1.0f : -1.0f;
        if (inset.contains(x, y))
        {
            setParamGesture(kKnee, value(kKnee) + dir * (shift ? 0.25f : 1.0f));
            setStatus("Knee  " + fmt(kKnee));
            return true;
        }
        if (graph.contains(x, y))
        {
            setParamGesture(kThreshold, value(kThreshold) + dir * (shift ? 0.1f : 0.5f));
            setStatus("Threshold  " + fmt(kThreshold));
            return true;
        }
        return false;
    }

    // status text with this plugin's formatting while dragging a control
    bool onMotion(const MotionEvent& ev) override
    {
        const bool r = PumpUI::onMotion(ev);
        if (fActive && fActive->param >= 0) fStatus = std::string(fSpecs[fActive->param].name) + "  " + fmt(fActive->param);
        else if (!fActive && !fMenu.open)
        {
            const float x = (float)ev.pos.getX() / fScale, y = (float)ev.pos.getY() / fScale;
            if (styleSeg->r.contains(x, y)) fStatus = styleTip((int)((x - styleSeg->r.x) / styleSeg->r.w * 6.0f));
        }
        return r;
    }
    // "off" typed into a sidechain filter
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (fEdit.active && ev.press && ev.key == kKeyEnter && (fEdit.param == kScHpf || fEdit.param == kScLpf))
        {
            std::string t = fEdit.text;
            for (auto& c : t) c = (char)std::tolower((unsigned char)c);
            if (t.rfind("of", 0) == 0) fEdit.text = fEdit.param == kScHpf ? "20" : "20000";
        }
        const bool wa = fEdit.active; const int wp = fEdit.param;
        const bool r = PumpUI::onKeyboard(ev);
        fixEntry(wa, wp);
        return r;
    }

    bool keyPress(uint key, uint mod) override
    {
        const bool shift = (mod & kModifierShift) != 0;
        if (key == kKeyUp || key == kKeyDown)
        {
            setParamGesture(kThreshold, value(kThreshold) + (key == kKeyUp ? 1.0f : -1.0f) * (shift ? 0.1f : 0.5f));
            setStatus("Threshold  " + fmt(kThreshold));
            return true;
        }
        if (key == kKeyLeft || key == kKeyRight)
        {
            const int s = (int)std::lround(value(kStyle));
            setParamGesture(kStyle, (float)((s + (key == kKeyRight ? 1 : 5)) % 6));
            setStatus(std::string("Style  ") + kStyleNames[(int)std::lround(value(kStyle))]);
            return true;
        }
        return false;
    }

    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the threshold line", "threshold (Shift: fine); double-click it or right-click: type a value" },
            { "Wheel over the graph", "threshold in 0.5 dB steps (Shift: 0.1 dB)" },
            { "Drag the transfer curve", "threshold;  Ctrl/Cmd + drag up/down: ratio" },
            { "Wheel over the transfer curve", "knee (Shift: fine);  double-click: type ratio, Tab to knee and threshold" },
            { "Click a meter", "reset its peak hold (In, Out, GR)" },
            { "SC LISTEN", "click: toggle;  click and hold: listen only while held" },
            { "Up / Down  ·  Left / Right", "threshold (Shift: fine)  ·  previous / next style" },
            { "Typing values", "ratio: 4, inf;  sidechain filters: off, 120, 2k" },
        };
    }

    void updateRangeLabel() { rangeBtn->label = std::to_string((int)dispRange) + " dB"; }
    void saveUi()
    {
        char buf[64]; std::snprintf(buf, sizeof buf, "range=%d;", (int)dispRange);
        saveUiState(buf);
    }
    void loadUiState(const std::string& s) override
    {
        const int r = std::atoi(uiStateGet(s, "range", "48").c_str());
        dispRange = (float)(r >= 24 && r <= 72 ? r : 48);
        updateRangeLabel();
    }

private:
    PluginComp* plug = nullptr;
    Rect graph, inset, inM[2], outM[2], grM, inHit, outHit, grHit, detBar;
    float scCaptionY = 0.0f;
    float dispRange = 48.0f;
    float inLvl[2] = { -120.0f, -120.0f }, outLvl[2] = { -120.0f, -120.0f };
    float inHold = -120.0f, outHold = -120.0f, inHoldT = 0.0f, outHoldT = 0.0f;
    float grNow = 0.0f, grHold = 0.0f, grHoldT = 0.0f, detNow = -200.0f, scQuiet = 0.0f;
    DragMode dmode = dmNone;
    bool hoverThresh = false;
    float pressX = 0, pressY = 0, lastX = 0, lastY = 0, dragStart = 0;
    float hin[1024] = {}, hout[1024] = {}, hgr[1024] = {};   // history columns (UI thread)

    Segmented* styleSeg;
    Button *autoRelBtn, *autoGainBtn, *listenBtn, *rangeBtn;
    MenuButton *chanMenu, *osMenu, *scMenu;
    CompDial *thrDial, *ratioDial, *kneeDial, *rangeDial, *atkDial, *relDial, *holdDial, *laDial;
    CompDial *inDial, *outDial, *mixDial, *linkDial, *scHpDial, *scLpDial, *scGainDial;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIComp)
};

UI* createUI() { return new UIComp(); }

END_NAMESPACE_DISTRHO
