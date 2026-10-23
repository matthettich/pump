// pump LIMIT: interface (Pro-L 2 style in the Simple look): scrolling level / gain-reduction history with the
// ceiling and loudness target, In / GR / Out meters, a loudness screen, and the controls below.
#include "ui/PumpUI.hpp"
#include "PluginLimit.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::limit;

// A small typed/dragged value field (the custom loudness target).
struct ValueBox : Control {
    float dragStart = 0, dragY = 0;
    std::function<std::string()> textFn;
    void draw(PumpUI& ui) override
    {
        ui.keyBox(r, false, false, nullptr, 3.0f);
        ui.textAt(r.cx(), r.cy() + 0.5f, textFn ? textFn() : ui.spec(param).format(ui.value(param)), PumpUI::kMono, 11.5f,
                  ui.theme.ink, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
    }
    bool mouseDown(PumpUI& ui, float, float y, int button, uint) override
    {
        if (button != kMouseButtonLeft) return false;
        dragStart = ui.value(param); dragY = y;
        ui.beginEdit(param);
        return true;
    }
    void mouseDrag(PumpUI& ui, float, float y, uint mod) override
    {
        const float sens = (mod & kModifierShift) ? 0.01f : 0.1f;
        ui.setParam(param, dragStart + (dragY - y) * sens);
        if (mod & kModifierShift) { dragStart = ui.value(param); dragY = y; }
    }
    void mouseUp(PumpUI& ui, float, float) override { ui.endEdit(param); }
    bool scroll(PumpUI& ui, float dy, uint mod) override
    {
        ui.setParamGesture(param, ui.value(param) + (dy > 0 ? 1.0f : -1.0f) * ((mod & kModifierShift) ? 0.1f : 0.5f));
        return true;
    }
};

class UILimit : public PumpUI {
public:
    UILimit() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::limit(), "LIMIT", 1000, 620, kBypass)
    {
        styleMenu = addMenu(kStyle, "");
        styleMenu->arrows = true;
        styleMenu->tip = "Limiting style (◀ ▶ or the arrow keys step through them)";

        gainDial = addDial(kGain, "GAIN", Dial::Large);
        gainDial->tip = "Gain: drive into the limiter (drag in the display too)";
        ceilDial = addDial(kCeiling, "CEILING", Dial::Large);
        ceilDial->tip = "Output ceiling: the output never goes above it (drag the ceiling line too)";
        gainDial->tabGroup = ceilDial->tabGroup = 1;
        lookDial = addDial(kLookahead, "LOOKAHEAD", Dial::Medium);
        lookDial->tip = "Lookahead: how far ahead the limiter sees peaks (longer = smoother, more latency)";
        atkDial = addDial(kAttack, "ATTACK", Dial::Medium);
        atkDial->tip = "Attack: shape of the gain change inside the lookahead (0% = late and steep, 100% = early and gentle)";
        relDial = addDial(kRelease, "RELEASE", Dial::Medium);
        relDial->tip = "Release: how fast the gain recovers (program dependent: the style adapts it)";
        lookDial->tabGroup = atkDial->tabGroup = relDial->tabGroup = 2;
        linkTDial = addDial(kLinkTrans, "TRANSIENTS", Dial::Medium);
        linkTDial->tip = "Channel link for transients: 100% = both channels duck together on peaks, 0% = independent";
        linkRDial = addDial(kLinkRel, "RELEASE", Dial::Medium);
        linkRDial->tip = "Channel link for the release and leveling stage: lower keeps the stereo image wider";
        linkTDial->tabGroup = linkRDial->tabGroup = 3;

        tpBtn = addToggle(kTruePeak, "TRUE PEAK");
        tpBtn->small = true;
        tpBtn->tip = "True peak limiting: also catch the peaks between samples (ITU-R BS.1770, 4x detector)";
        dcBtn = addToggle(kDC, "DC FILTER");
        dcBtn->small = true;
        dcBtn->tip = "DC offset filter (5 Hz high-pass) before the limiter";
        osMenu = addMenu(kOversampling, "OS");
        osMenu->tip = "Oversampling for the limiter stage: less aliasing and fewer inter-sample peaks; more latency and CPU";
        unityBtn = addToggle(kUnity, "UNITY");
        unityBtn->small = true;
        unityBtn->holdMomentary = true;
        unityBtn->tip = "Unity gain listen: hear the limiting at the loudness it came in (click = latch, click-and-hold = while held)";
        ditherMenu = add(new MenuButton());
        ditherMenu->itemsFn = []() { return std::vector<std::string> { "Off", "16-bit", "16-bit shaped", "24-bit", "24-bit shaped" }; };
        ditherMenu->getFn = [this]() { const int d = (int)value(kDither), s = (int)value(kShaping); return d == 0 ? 0 : (d == 1 ? 1 + s : 3 + s); };
        ditherMenu->setFn = [this](int i) {
            beginGroup();
            setParamGesture(kDither, i == 0 ? 0.0f : (i <= 2 ? 1.0f : 2.0f));
            if (i > 0) setParamGesture(kShaping, (i == 2 || i == 4) ? 1.0f : 0.0f);
            endGroup();
        };
        ditherMenu->textFn = [this]() {
            const int d = (int)value(kDither);
            if (d == 0) return std::string("Dither: off");
            return std::string("Dither: ") + (d == 1 ? "16-bit" : "24-bit") + (value(kShaping) >= 0.5f ? " shaped" : "");
        };
        ditherMenu->tip = "Dither for the final bit depth (TPDF; shaped = noise moved to where the ear is least sensitive)";

        resetBtn = addButton("RESET", [this]() { resetLoudness(); });
        resetBtn->small = true;
        resetBtn->tip = "Reset the loudness measurement (or click any readout; key R)";
        targetMenu = addMenu(kTarget, "");
        targetMenu->tip = "Loudness target: drawn in the display and compared with the integrated loudness";
        customBox = add(new ValueBox());
        customBox->param = kTargetCustom;
        customBox->textFn = [this]() { char b[24]; std::snprintf(b, sizeof b, "%.1f", value(kTargetCustom)); return std::string(b); };
        customBox->tip = "Custom target in LUFS (drag, wheel or double-click to type)";

        rangeBtn = addButton("24 dB", [this]() {
            static const int r[] = { 12, 24, 36, 48 };
            std::vector<std::string> it;
            int cur = 1;
            for (int i = 0; i < 4; ++i) { it.push_back(std::to_string(r[i]) + " dB range"); if ((int)range == r[i]) cur = i; }
            openMenu(rangeBtn->r, it, cur, [this](int i) { static const int rr[] = { 12, 24, 36, 48 }; setRange((float)rr[i]); });
        });
        rangeBtn->small = true;
        rangeBtn->tip = "Display range";

        plug = static_cast<PluginLimit*>(pluginPtr());
        rebind();
    }

protected:
    static constexpr int kHistPts = 1000;   // 5 s at 200 points per second

    // ---------------------------------------------------------------- layout
    void layout() override
    {
        display = Rect(14, 58, 578, 356);
        rangeBtn->r = Rect(display.r() - 56, display.y + 8, 48, 22);
        meterArea = Rect(602, 58, 140, 356);
        const float mt = 84.0f, mb = 394.0f;
        inBars = Rect(606, mt, 28, mb - mt);
        grBar = Rect(650, mt, 18, mb - mt);
        outBars = Rect(684, mt, 28, mb - mt);
        loud = Rect(752, 58, 234, 356);
        resetBtn->r = Rect(loud.r() - 64, loud.y + 9, 54, 22);
        targetMenu->r = Rect(loud.x + 12, loud.y + 182, 150, 26);
        customBox->r = Rect(loud.x + 170, loud.y + 182, 52, 26);

        const float y0 = 448.0f, h = 136.0f;
        pStyle = Rect(14, y0, 172, h);
        pOut = Rect(196, y0, 176, h);
        pTime = Rect(382, y0, 210, h);
        pLink = Rect(602, y0, 140, h);
        pOpt = Rect(752, y0, 234, h);
        styleMenu->r = Rect(pStyle.x + 12, y0 + 26, pStyle.w - 24, 28);
        const float dy = y0 + 22.0f;
        gainDial->r = Rect(pOut.x + 6, dy + 4, 80, Dial::heightFor(Dial::Large));
        ceilDial->r = Rect(pOut.x + 90, dy + 4, 80, Dial::heightFor(Dial::Large));
        const float my = dy + 12.0f;
        lookDial->r = Rect(pTime.x + 4, my, 66, Dial::heightFor(Dial::Medium));
        atkDial->r = Rect(pTime.x + 72, my, 66, Dial::heightFor(Dial::Medium));
        relDial->r = Rect(pTime.x + 140, my, 66, Dial::heightFor(Dial::Medium));
        linkTDial->r = Rect(pLink.x + 2, my, 66, Dial::heightFor(Dial::Medium));
        linkRDial->r = Rect(pLink.x + 72, my, 66, Dial::heightFor(Dial::Medium));
        const float ox = pOpt.x + 12, cw = (pOpt.w - 24 - 8) * 0.5f;
        tpBtn->r = Rect(ox, y0 + 26, cw, 26);
        dcBtn->r = Rect(ox + cw + 8, y0 + 26, cw, 26);
        osMenu->r = Rect(ox, y0 + 62, cw, 26);
        unityBtn->r = Rect(ox + cw + 8, y0 + 62, cw, 26);
        ditherMenu->r = Rect(ox, y0 + 98, pOpt.w - 24, 26);
    }

    void rebind()
    {
        customBox->visible = (int)value(kTarget) == 6;
        targetMenu->r.w = customBox->visible ? 150.0f : 210.0f;
    }
    void onParamChanged(int, float) override { rebind(); }
    void onPresetApplied() override { rebind(); }

    // ---------------------------------------------------------------- helpers
    float levelTop() const { return display.y + 10.0f; }
    float pxPerDb() const { return (display.h - 30.0f) / range; }
    float yOfDb(float db) const { return levelTop() - db * pxPerDb(); }
    float dbOfY(float y) const { return (levelTop() - y) / pxPerDb(); }
    float clampY(float y) const { return std::max(display.y + 1.0f, std::min(display.b() - 1.0f, y)); }
    float targetLu() const { return pump::limit::targetLufs((int)value(kTarget), value(kTargetCustom)); }
    float tpLimitDb() const
    {
        switch ((int)value(kTarget)) { case 0: return 0.0f; case 3: return -0.3f; case 4: return -0.3f; default: return -1.0f; }
    }
    void setRange(float r)
    {
        range = r;
        rangeBtn->label = std::to_string((int)r) + " dB";
        char buf[48]; std::snprintf(buf, sizeof buf, "range=%d;", (int)r);
        saveUiState(buf);
        repaint();
    }
    void loadUiState(const std::string& s) override
    {
        const int r = std::atoi(uiStateGet(s, "range", "24").c_str());
        range = (float)(r == 12 || r == 36 || r == 48 ? r : 24);
        rangeBtn->label = std::to_string((int)range) + " dB";
    }

    // Meter scale (Pro-L style): the top 12 dB take half the height.
    static float meterT(float db)
    {
        if (db >= 0.0f) return 0.0f;
        if (db >= -12.0f) return 0.5f * (-db / 12.0f);
        if (db >= -48.0f) return 0.5f + 0.5f * ((-db - 12.0f) / 36.0f);
        return 1.0f;
    }
    float meterY(const Rect& r, float db) const { return r.y + meterT(db) * r.h; }

    void resetLoudness()
    {
        if (plug) plug->engine.resetLoudness.store(true);
        setStatus("Loudness measurement reset");
    }
    void resetHolds(int which)   // 0 in, 1 gr, 2 out, -1 all
    {
        if (which < 0 || which == 0) { for (int c = 0; c < 2; ++c) inHoldNum[c] = -120.0f; }
        if (which < 0 || which == 1) grHoldNum = 0.0f;
        if (which < 0 || which == 2) { for (int c = 0; c < 2; ++c) outHoldNum[c] = -120.0f; }
        setStatus(which == 0 ? "Input peak hold reset" : which == 1 ? "Gain reduction hold reset" : which == 2 ? "Output peak hold reset" : "Peak holds reset");
    }

    // ---------------------------------------------------------------- meters
    void tick(float dt) override
    {
        if (!plug) return;
        pump::limit::Engine& e = plug->engine;
        for (int c = 0; c < 2; ++c)
        {
            const float pi = pump::gainToDbF(e.inPeak[c].exchange(0.0f)), po = pump::gainToDbF(e.outPeak[c].exchange(0.0f));
            ballistics(inLvl[c], inHold[c], inHoldT[c], pi, dt);
            ballistics(outLvl[c], outHold[c], outHoldT[c], po, dt);
            inHoldNum[c] = std::max(inHoldNum[c], pi);
            outHoldNum[c] = std::max(outHoldNum[c], po);
        }
        const float g = e.grMax.exchange(0.0f);
        // gain reduction: positive dB, attack immediately, fall back at 20 dB/s
        grLvl = std::max(g, grLvl - 20.0f * dt);
        if (g >= grHold) { grHold = g; grHoldT = 2.0f; }
        else if ((grHoldT -= dt) <= 0.0f) grHold = std::max(g, grHold - 12.0f * dt);
        grHoldNum = std::max(grHoldNum, g);
    }
    static void ballistics(float& lvl, float& hold, float& holdT, float pk, float dt)
    {
        lvl = std::max(pk, lvl - 24.0f * dt);
        if (pk >= hold) { hold = pk; holdT = 2.0f; }
        else if ((holdT -= dt) <= 0.0f) hold = std::max(pk, hold - 15.0f * dt);
    }

    void drawBar(const Rect& r, float t /*0 top .. 1 bottom*/, float holdT, bool fromTop, const Color& c)
    {
        fillRect(r, theme.panel2);
        t = std::max(0.0f, std::min(1.0f, t));
        if (fromTop) { if (t > 0.002f) fillRect(Rect(r.x, r.y, r.w, t * r.h), c); }
        else if (t < 0.998f) fillRect(Rect(r.x, r.y + t * r.h, r.w, r.h - t * r.h), c);
        if (holdT > 0.002f && holdT < 0.998f) line(r.x, r.y + holdT * r.h, r.r(), r.y + holdT * r.h, theme.ink, 1.5f);
        strokeRect(r, theme.ink, 1.0f);
    }
    static std::string fmtDb(float db, bool plus = false)
    {
        if (!std::isfinite(db) || db <= -119.0f) return "-inf";
        char b[24];
        std::snprintf(b, sizeof b, plus ? "%+.1f" : "%.1f", db);
        return b;
    }

    void drawMeters()
    {
        const float ceil = value(kCeiling);
        // peak hold numbers (click to reset)
        const float inMax = std::max(inHoldNum[0], inHoldNum[1]), outMax = std::max(outHoldNum[0], outHoldNum[1]);
        auto holdBox = [&](const Rect& bars, const std::string& s, bool over) {
            const Rect b(bars.cx() - 19.0f, meterArea.y + 2.0f, 38.0f, 18.0f);
            fillRect(b, over ? theme.red : theme.panel2);
            textAt(b.cx(), b.cy() + 0.5f, s, kMono, 10.0f, over ? theme.bg : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        };
        holdBox(inBars, fmtDb(inMax), inMax > 0.0f);
        holdBox(grBar, grHoldNum > 0.05f ? fmtDb(-grHoldNum) : "0.0", false);
        holdBox(outBars, fmtDb(outMax), outMax > ceil + 0.05f);
        const float bw = (inBars.w - 2.0f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            drawBar(Rect(inBars.x + c * (bw + 2.0f), inBars.y, bw, inBars.h), meterT(inLvl[c]), meterT(inHold[c]), false, theme.neon);
            drawBar(Rect(outBars.x + c * (bw + 2.0f), outBars.y, bw, outBars.h), meterT(outLvl[c]), meterT(outHold[c]), false, theme.neon);
        }
        drawBar(grBar, meterT(-grLvl), grHold > 0.05f ? meterT(-grHold) : 0.0f, true, theme.second);
        // ceiling tick on the output meter
        const float yc = meterY(outBars, ceil);
        line(outBars.x - 4, yc, outBars.r() + 4, yc, theme.ink, 1.5f);
        // scale (dB below full scale / dB of gain reduction)
        static const int marks[] = { 0, 3, 6, 9, 12, 18, 24, 36, 48 };
        for (int m : marks)
        {
            const float y = meterY(inBars, (float)-m);
            textAt(outBars.r() + 17.0f, y, std::to_string(m), kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            line(inBars.r() + 3, y, inBars.r() + 6, y, theme.dim, 1.0f);
            line(grBar.r() + 3, y, grBar.r() + 6, y, theme.dim, 1.0f);
        }
        const float ly = inBars.b() + 13.0f;
        textAt(inBars.cx(), ly, "IN", kLabel, 10.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        textAt(grBar.cx(), ly, "GR", kLabel, 10.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        textAt(outBars.cx(), ly, "OUT", kLabel, 10.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
    }

    // ---------------------------------------------------------------- the display
    void drawDisplay()
    {
        screenBox(display);
        save();
        scissor(display.x + 1, display.y + 1, display.w - 2, display.h - 2);
        // grid: dB lines
        const float step = range <= 12.0f ? 2.0f : (range <= 24.0f ? 3.0f : 6.0f);
        for (float db = 0.0f; db >= -range - 0.01f; db -= step)
        {
            const float y = yOfDb(db);
            line(display.x, y, display.r(), y, db == 0.0f ? withAlpha2(theme.ink, 0.22f) : theme.grid, 1.0f);
        }
        // seconds (fixed grid; the history scrolls under it)
        const float pxPerPt = (display.w - 2.0f) / (float)(kHistPts - 1);
        for (int s = 1; s < 5; ++s)
        {
            const float x = display.r() - 1.0f - s * 200.0f * pxPerPt;
            line(x, display.y, x, display.b(), theme.grid, 1.0f);
        }

        // history
        if (plug)
        {
            const pump::limit::Engine& e = plug->engine;
            const uint32_t end = e.histST.position();
            const int n = (int)std::min<uint32_t>((uint32_t)kHistPts, end);
            for (int k = 0; k < kHistPts; ++k)
            {
                if (k < kHistPts - n) { hIn[k] = hOut[k] = -200.0f; hGR[k] = 0.0f; hST[k] = -INFINITY; continue; }
                const uint32_t idx = end - (uint32_t)kHistPts + (uint32_t)k;
                hIn[k] = e.histIn.at(idx); hOut[k] = e.histOut.at(idx); hGR[k] = e.histGR.at(idx); hST[k] = e.histST.at(idx);
            }
        }
        const float x0 = display.x + 1.0f, bot = display.b();
        auto xOf = [&](int k) { return x0 + k * pxPerPt; };
        auto fillLevel = [&](const float* v, const Color& c) {
            beginPath();
            moveTo(xOf(0), bot);
            for (int k = 0; k < kHistPts; ++k) lineTo(xOf(k), clampY(yOfDb(v[k])));
            lineTo(xOf(kHistPts - 1), bot);
            closePath();
            fillColor(c); fill();
        };
        // input (driven) level: dim; output: neon with a darker edge
        fillLevel(hIn, withAlpha2(theme.ink, theme.dark ? 0.20f : 0.15f));
        fillLevel(hOut, withAlpha2(theme.neon, theme.dark ? 0.62f : 0.80f));
        beginPath();
        for (int k = 0; k < kHistPts; ++k) { const float y = clampY(yOfDb(hOut[k])); if (k == 0) moveTo(xOf(k), y); else lineTo(xOf(k), y); }
        strokeColor(withAlpha2(pump::mixColor(theme.neon, theme.ink, 0.55f), 0.9f)); strokeWidth(1.0f); stroke();
        // gain reduction from the top
        const float top = levelTop();
        beginPath();
        moveTo(xOf(0), display.y);
        for (int k = 0; k < kHistPts; ++k) lineTo(xOf(k), std::min(bot, top + std::max(0.0f, hGR[k]) * pxPerDb()));
        lineTo(xOf(kHistPts - 1), display.y);
        closePath();
        fillColor(withAlpha2(theme.second, theme.dark ? 0.30f : 0.24f)); fill();
        beginPath();
        for (int k = 0; k < kHistPts; ++k) { const float y = std::min(bot, top + std::max(0.0f, hGR[k]) * pxPerDb()); if (k == 0) moveTo(xOf(k), y); else lineTo(xOf(k), y); }
        strokeColor(theme.second); strokeWidth(1.5f); lineJoin(ROUND); stroke();
        // short-term loudness line (LUFS on the same scale)
        {
            bool pen = false;
            beginPath();
            for (int k = 0; k < kHistPts; ++k)
            {
                if (!std::isfinite(hST[k]) || hST[k] < -90.0f) { pen = false; continue; }
                const float y = clampY(yOfDb(hST[k]));
                if (!pen) { moveTo(xOf(k), y); pen = true; } else lineTo(xOf(k), y);
            }
            strokeColor(theme.ink); strokeWidth(1.6f); stroke();
        }
        // loudness target (dashed)
        const float tgt = targetLu();
        if ((int)value(kTarget) != 0)
        {
            const float y = yOfDb(tgt);
            for (float x = display.x; x < display.r(); x += 10.0f) line(x, y, std::min(display.r(), x + 5.0f), y, theme.ink, 1.2f);
            char b[40]; std::snprintf(b, sizeof b, "TARGET %.0f LUFS", tgt);
            const float tw = textWidth(b, kLabel, 10.5f, 0.8f) + 12.0f;
            const Rect tb(display.r() - tw - 8.0f, y - 19.0f, tw, 16.0f);
            fillRect(tb, theme.screen);
            textAt(tb.cx(), tb.cy() + 0.5f, b, kLabel, 10.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        }
        // ceiling line with its tag (drag it); the tag sits right of the gain tag so they never collide
        gainTag = Rect(display.x + 8.0f, display.y + 8.0f, textWidth("GAIN " + spec(kGain).format(value(kGain)), kLabel, 11.0f, 0.8f) + 16.0f, 22.0f);
        const float yc = yOfDb(value(kCeiling));
        const bool ceilHot = dmode == dmCeiling || hoverCeil;
        line(display.x, yc, display.r(), yc, ceilHot ? theme.red : theme.ink, ceilHot ? 2.0f : 1.5f);
        ceilTag = Rect(display.x + 8.0f + gainTag.w + 8.0f, yc + 4.0f, textWidth("CEILING -12.0 dB", kLabel, 10.5f, 0.8f) + 14.0f, 18.0f);
        fillRect(ceilTag, ceilHot ? theme.red : theme.ink);
        textAt(ceilTag.cx(), ceilTag.cy() + 0.5f, "CEILING " + spec(kCeiling).format(value(kCeiling)), kLabel, 10.5f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        // dB labels at the right edge
        for (float db = -step; db >= -range + 0.01f; db -= step)
        {
            if (std::fmod(-db, step * 2.0f) > 0.01f && range > 12.0f) continue;
            const std::string t = std::to_string((int)db);
            const float tw = textWidth(t, kMono, 9.5f) + 8.0f;
            const Rect c(display.r() - 4.0f - tw, yOfDb(db) - 14.0f, tw, 13.0f);
            fillRect(c, withAlpha2(theme.screen, 0.85f));
            textAt(c.cx(), c.cy() + 0.5f, t, kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // gain tag (top left) and unity listen badge
        {
            const std::string s = "GAIN " + spec(kGain).format(value(kGain));
            const Rect gb(display.x + 8.0f, display.y + 8.0f, textWidth(s, kLabel, 11.0f, 0.8f) + 16.0f, 22.0f);
            const bool hot = dmode == dmGain;
            fillRect(gb, hot ? theme.neon : theme.screen);
            strokeRect(gb, theme.ink, 1.2f);
            textAt(gb.cx(), gb.cy() + 0.5f, s, kLabel, 11.0f, hot ? theme.neonInk : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
            gainTag = gb;
            if (value(kUnity) >= 0.5f)
            {
                const std::string u = "UNITY LISTEN  " + spec(kGain).format(-value(kGain));
                const float tw = textWidth(u, kLabel, 11.0f, 0.8f) + 16.0f;
                const Rect ub(display.cx() - tw * 0.5f, gb.y, tw, 22.0f);
                fillRect(ub, theme.second);
                textAt(ub.cx(), ub.cy() + 0.5f, u, kLabel, 11.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
            }
            if (value(kBypass) >= 0.5f)
            {
                const Rect bb(display.cx() - 50.0f, display.cy() - 14.0f, 100.0f, 28.0f);
                fillRect(bb, theme.ink);
                textAt(bb.cx(), bb.cy() + 0.5f, "BYPASSED", kLabel, 13.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 2.0f);
            }
        }
        // seconds, on small chips so they stay readable over the waveform
        for (int s = 1; s < 5; ++s)
        {
            const float x = display.r() - 1.0f - s * 200.0f * pxPerPt;
            const std::string t = "-" + std::to_string(s) + " s";
            const float tw = textWidth(t, kMono, 9.0f) + 8.0f;
            const Rect c(x - tw * 0.5f, display.b() - 17.0f, tw, 14.0f);
            fillRect(c, withAlpha2(theme.screen, 0.85f));
            textAt(c.cx(), c.cy() + 0.5f, t, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        restore();
    }

    // ---------------------------------------------------------------- loudness screen
    void drawLoudness()
    {
        screenBox(loud);
        float st = -INFINITY, it = -INFINITY, mo = -INFINITY, lra = -INFINITY, tp = -INFINITY, inI = -INFINITY;
        if (plug)
        {
            const auto& o = plug->engine.outLoud;
            st = o.shortTerm.load(); it = o.integrated.load(); mo = o.momentary.load(); lra = o.range.load(); tp = o.truePeak.load();
            inI = plug->engine.inLoud.integrated.load();
        }
        auto ok = [](float v) { return std::isfinite(v) && v > -70.0f; };
        const float x = loud.x + 14.0f, rx = loud.r() - 14.0f;
        caption(x, loud.y + 20.0f, "LOUDNESS");
        // short-term
        caption(x, loud.y + 46.0f, "SHORT-TERM");
        auto big = [&](float y, float v, const Color& c) {
            const std::string s = ok(v) ? fmtDb(v) : "--.-";
            textAt(x - 1.0f, y, s, kBold, 31.0f, c, ALIGN_LEFT | ALIGN_MIDDLE);
            textAt(x + textWidth(s, kBold, 31.0f) + 5.0f, y + 6.0f, "LUFS", kLabel, 10.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        };
        big(loud.y + 70.0f, st, theme.ink);
        // integrated, with the distance to the target
        caption(x, loud.y + 98.0f, "INTEGRATED");
        const float tgt = targetLu();
        const bool hasT = (int)value(kTarget) != 0;
        Color ic = theme.ink;
        if (hasT && ok(it)) { const float d = it - tgt; ic = d > 1.0f ? theme.red : theme.ink; }
        big(loud.y + 122.0f, it, ic);
        if (hasT && ok(it))
        {
            const float d = it - tgt;
            char b[24]; std::snprintf(b, sizeof b, "%+.1f LU", d);
            const bool on = std::fabs(d) <= 1.0f;
            const float tw = textWidth(b, kMono, 11.5f) + 12.0f;
            const Rect db(rx - tw, loud.y + 113.0f, tw, 19.0f);
            Color f = on ? theme.neon : (d > 0.0f ? theme.red : theme.panel2);
            fillRect(db, f);
            textAt(db.cx(), db.cy() + 0.5f, b, kMono, 11.5f, on ? theme.neonInk : (d > 0.0f ? theme.bg : theme.ink), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // target bar: -30 .. -4 LUFS, target band +-1 LU, short-term block and integrated line
        {
            const Rect bar(x, loud.y + 148.0f, rx - x, 12.0f);
            const float lo = -30.0f, hi = -4.0f;
            auto bx = [&](float v) { return bar.x + (std::max(lo, std::min(hi, v)) - lo) / (hi - lo) * bar.w; };
            fillRect(bar, theme.panel2);
            if (ok(st)) fillRect(Rect(bar.x, bar.y, bx(st) - bar.x, bar.h), withAlpha2(theme.neon, 0.9f));
            if (hasT)
            {
                fillRect(Rect(bx(tgt - 1.0f), bar.y - 3.0f, bx(tgt + 1.0f) - bx(tgt - 1.0f), bar.h + 6.0f), withAlpha2(theme.second, 0.30f));
                line(bx(tgt), bar.y - 4.0f, bx(tgt), bar.b() + 4.0f, theme.second, 2.0f);
            }
            if (ok(it)) line(bx(it), bar.y - 2.0f, bx(it), bar.b() + 2.0f, theme.ink, 2.5f);
            strokeRect(bar, theme.ink, 1.0f);
            for (int v : { -30, -24, -18, -14, -9, -4 })
                textAt(bx((float)v), bar.b() + 9.0f, std::to_string(v), kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // the grid of smaller readouts
        const float cw = (rx - x) * 0.5f;
        auto cell = [&](int col, int row, const char* cap, const std::string& v, const Color& c) {
            const float cx = x + col * cw, cy = loud.y + 236.0f + row * 40.0f;
            caption(cx, cy, cap);
            textAt(cx, cy + 19.0f, v, kMono, 16.0f, c, ALIGN_LEFT | ALIGN_MIDDLE);
        };
        line(x, loud.y + 221.0f, rx, loud.y + 221.0f, theme.dim, 1.0f);
        cell(0, 0, "MOMENTARY", ok(mo) ? fmtDb(mo) : "--.-", theme.ink);
        cell(1, 0, "RANGE LRA", ok(it) && std::isfinite(lra) ? fmtDb(lra) + " LU" : "--.-", theme.ink);
        cell(0, 1, "TRUE PEAK", std::isfinite(tp) && tp > -120.0f ? fmtDb(tp) : "-inf", std::isfinite(tp) && tp > tpLimitDb() + 0.05f ? theme.red : theme.ink);
        cell(1, 1, "PLR", ok(it) && std::isfinite(tp) ? fmtDb(tp - it) + " LU" : "--.-", theme.ink);
        cell(0, 2, "INPUT", ok(inI) ? fmtDb(inI) : "--.-", theme.ink2);
        cell(1, 2, "LOUDER BY", ok(inI) && ok(it) ? fmtDb(it - inI, true) + " LU" : "--.-", theme.ink);
        loudReadouts = Rect(loud.x, loud.y + 34.0f, loud.w, 144.0f);
        loudGrid = Rect(loud.x, loud.y + 223.0f, loud.w, loud.h - 225.0f);
    }

    void drawLegend()
    {
        float lx = display.x + 2.0f;
        const float ly = display.b() + 19.0f;
        auto item = [&](const Color& c, const char* t, bool lineStyle) {
            if (lineStyle) line(lx, ly, lx + 14.0f, ly, c, 2.0f); else { fillRect(Rect(lx, ly - 4.5f, 14.0f, 9.0f), c); strokeRect(Rect(lx, ly - 4.5f, 14.0f, 9.0f), theme.dim, 1.0f); }
            textAt(lx + 19.0f, ly + 0.5f, t, kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.9f);
            lx += 19.0f + textWidth(t, kLabel, 10.0f, 0.9f) + 18.0f;
        };
        item(withAlpha2(theme.ink, theme.dark ? 0.26f : 0.20f), "INPUT", false);
        item(theme.neon, "OUTPUT", false);
        item(theme.second, "GAIN REDUCTION", true);
        item(theme.ink, "SHORT-TERM LUFS", true);
        textAt(display.r(), ly + 0.5f, "5 S HISTORY", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.9f);
    }

    void drawBody() override
    {
        drawDisplay();
        drawLegend();
        drawMeters();
        drawLoudness();
        // bottom panels
        for (const Rect* p : { &pStyle, &pOut, &pTime, &pLink, &pOpt }) strokeRect(*p, theme.dim, 1.0f);
        caption(pStyle.x + 12, pStyle.y + 13, "STYLE");
        caption(pOut.x + 12, pOut.y + 13, "OUTPUT");
        caption(pTime.x + 12, pTime.y + 13, "TIMING");
        caption(pLink.x + 12, pLink.y + 13, "CHANNEL LINK");
        caption(pOpt.x + 12, pOpt.y + 13, "OPTIONS");
        // style description
        static const char* const desc[kNumStyles][3] = {
            { "Slow leveling does most", "of the work: the cleanest,", "least coloured limiting." },
            { "Transients pass the", "leveling stage and the", "release snaps back." },
            { "Brickwall only, with a", "transient-aware release", "that keeps micro-dynamics." },
            { "Balanced leveling and", "brickwall. A safe first", "choice for any master." },
            { "Soft clipping ahead of a", "fast brickwall: loud and", "dense, with some grit." },
            { "RMS leveling, light", "clipping and a fast", "release: loud and clear." },
            { "Early attack and slow", "release: avoids audible", "distortion at all costs." },
        };
        const int s = pump::clampv((int)value(kStyle), 0, kNumStyles - 1);
        for (int k = 0; k < 3; ++k)
            textAt(pStyle.x + 12, pStyle.y + 76.0f + k * 16.0f, desc[s][k], kLabel, 11.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.3f);
    }

    std::string footerInfo() override
    {
        const int lat = plug ? plug->latencyNow() : 0;
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s   OS %s   LATENCY %.2f ms (%d)", value(kTruePeak) >= 0.5f ? "TRUE PEAK" : "SAMPLE PEAK",
                      kOSNames[pump::clampv((int)value(kOversampling), 0, 3)], 1000.0 * lat / sr, lat);
        return buf;
    }

    // ---------------------------------------------------------------- mouse
    enum DragMode { dmNone = 0, dmGain, dmCeiling };

    bool nearCeiling(float x, float y) const
    {
        if (!display.contains(x, y)) return false;
        return std::fabs(y - yOfDb(value(kCeiling))) < 6.0f || ceilTag.contains(x, y);
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        const bool cmd = (mod & (kModifierControl | kModifierSuper | kModifierAlt)) != 0;
        if (display.contains(x, y))
        {
            const bool onCeil = nearCeiling(x, y);
            const int p = onCeil ? kCeiling : kGain;
            if (button == kMouseButtonRight) { displayMenu(x, y); return false; }
            if (button != kMouseButtonLeft) return false;
            if (dbl)
            {
                const Rect r = onCeil ? Rect(ceilTag.x, ceilTag.y, std::max(ceilTag.w, 110.0f), 22.0f)
                                      : Rect(std::max(display.x + 4, std::min(display.r() - 104, x - 50)), y - 11, 100, 22);
                openTextEntry(p, r, { { kGain, onCeil ? Rect(gainTag.x, gainTag.y, 110, 22) : r }, { kCeiling, Rect(ceilTag.x, ceilTag.y, 110, 22) } });
                setStatus(onCeil ? "Type the ceiling (dB); Tab: gain" : "Type the gain (dB, or 2x); Tab: ceiling");
                return false;
            }
            if (cmd) { setParamGesture(p, spec(p).def); return false; }
            dmode = onCeil ? dmCeiling : dmGain;
            dragStart = value(p); dragY = y;
            beginEdit(p);
            return true;
        }
        const float bx0 = inBars.x - 6, bx1 = outBars.r() + 30;
        if (x >= bx0 && x < bx1 && y >= meterArea.y && y < meterArea.b())
        {
            if (x < grBar.x - 6) resetHolds(0);
            else if (x < outBars.x - 6) resetHolds(1);
            else resetHolds(2);
            return false;
        }
        if (loud.contains(x, y) && (loudReadouts.contains(x, y) || loudGrid.contains(x, y)) && button == kMouseButtonLeft)
        {
            resetLoudness();
            return false;
        }
        return false;
    }
    void bodyMouseDrag(float, float y, uint mod) override
    {
        if (dmode == dmNone) return;
        const int p = dmode == dmCeiling ? kCeiling : kGain;
        // the waveform / ceiling line follow the mouse (dB per pixel of the display); Shift = fine
        const float sens = 1.0f / pxPerDb() * ((mod & kModifierShift) ? 0.1f : 1.0f);
        setParam(p, dragStart + (dragY - y) * sens);
        if (mod & kModifierShift) { dragStart = value(p); dragY = y; }
        setStatus(paramStatus(p));
    }
    void bodyMouseUp(float, float) override
    {
        if (dmode == dmGain) endEdit(kGain);
        else if (dmode == dmCeiling) endEdit(kCeiling);
        dmode = dmNone;
    }
    void bodyHover(float x, float y) override
    {
        hoverCeil = nearCeiling(x, y);
        if (hoverCeil) setStatus("Ceiling " + spec(kCeiling).format(value(kCeiling)) + "    drag to change · Shift: fine · double-click: type · Ctrl-click: reset");
        else if (display.contains(x, y))
        {
            char b[160];
            std::snprintf(b, sizeof b, "%.1f dB    drag up/down: gain (%s) · wheel: gain · double-click: type · right-click: display menu", dbOfY(y), spec(kGain).format(value(kGain)).c_str());
            setStatus(b);
        }
        else if (x >= inBars.x - 6 && x < outBars.r() + 30 && y >= meterArea.y && y < meterArea.b())
        {
            if (x < grBar.x - 6) setStatus("Input level (L/R) and peak hold: click to reset the hold");
            else if (x < outBars.x - 6) setStatus("Gain reduction and its hold: click to reset the hold");
            else setStatus("Output level (L/R), ceiling mark and peak hold: click to reset the hold");
        }
        else if (loud.contains(x, y) && (loudReadouts.contains(x, y) || loudGrid.contains(x, y)))
            setStatus("Output loudness (EBU R128): click any readout to reset the measurement (key R)");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (!display.contains(x, y)) return false;
        const int p = nearCeiling(x, y) ? kCeiling : kGain;
        setParamGesture(p, value(p) + (dy > 0 ? 1.0f : -1.0f) * ((mod & kModifierShift) ? 0.1f : 0.5f));
        setStatus(paramStatus(p));
        return true;
    }
    void displayMenu(float x, float y)
    {
        std::vector<std::string> it = { "Reset loudness", "Reset peak holds", "-", "#DISPLAY RANGE", "12 dB", "24 dB", "36 dB", "48 dB" };
        openMenu(Rect(x, y, 10, 10), it, -1, [this](int i) {
            if (i == 0) resetLoudness();
            else if (i == 1) resetHolds(-1);
            else if (i >= 4) { static const int r[] = { 12, 24, 36, 48 }; setRange((float)r[i - 4]); }
        });
    }

    // ---------------------------------------------------------------- keys
    bool keyPress(uint key, uint mod) override
    {
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        const bool cmd = (mod & (kModifierControl | kModifierSuper)) != 0;
        if (cmd) return false;
        const float fine = (mod & kModifierShift) ? 0.1f : 0.5f;
        if (k == 'r') { resetLoudness(); return true; }
        if (k == 'p') { resetHolds(-1); return true; }
        if (k == 'u') { setParamGesture(kUnity, value(kUnity) >= 0.5f ? 0.0f : 1.0f); setStatus(paramStatus(kUnity)); return true; }
        if (key == kKeyUp || key == kKeyDown) { setParamGesture(kGain, value(kGain) + (key == kKeyUp ? fine : -fine)); setStatus(paramStatus(kGain)); return true; }
        if (key == kKeyPageUp || key == kKeyPageDown) { setParamGesture(kCeiling, value(kCeiling) + (key == kKeyPageUp ? 0.1f : -0.1f)); setStatus(paramStatus(kCeiling)); return true; }
        if (key == kKeyLeft || key == kKeyRight)
        {
            const int s = (int)value(kStyle);
            setParamGesture(kStyle, (float)((s + (key == kKeyRight ? 1 : kNumStyles - 1)) % kNumStyles));
            setStatus(paramStatus(kStyle));
            return true;
        }
        return false;
    }

    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag up/down in the display", "gain (the waveform follows the mouse); wheel: 0.5 dB steps" },
            { "Drag the ceiling line", "output ceiling; Shift: fine; double-click: type (Tab: gain)" },
            { "Double-click the display", "type the gain; Ctrl/Cmd-click: reset gain (on the line: ceiling)" },
            { "UNITY (click / hold)", "latch / hear while held: the limiting at the loudness it came in" },
            { "Click a meter", "reset that meter's peak hold (IN, GR or OUT); P: all holds" },
            { "Click a loudness readout", "reset the loudness measurement (also RESET or R)" },
            { "Up / Down  ·  PgUp / PgDn", "gain ±0.5 dB (Shift ±0.1)  ·  ceiling ±0.1 dB" },
            { "Left / Right  ·  U", "previous / next style  ·  unity listen on/off" },
            { "Right-click the display", "reset loudness or holds, display range" },
        };
    }

private:
    PluginLimit* plug = nullptr;
    Rect display, meterArea, inBars, grBar, outBars, loud, loudReadouts, loudGrid, ceilTag, gainTag;
    Rect pStyle, pOut, pTime, pLink, pOpt;
    float range = 24.0f;
    float hIn[kHistPts] = {}, hOut[kHistPts] = {}, hGR[kHistPts] = {}, hST[kHistPts] = {};
    float inLvl[2] = { -120, -120 }, inHold[2] = { -120, -120 }, inHoldT[2] = {}, outLvl[2] = { -120, -120 }, outHold[2] = { -120, -120 }, outHoldT[2] = {};
    float inHoldNum[2] = { -120, -120 }, outHoldNum[2] = { -120, -120 };
    float grLvl = 0, grHold = 0, grHoldT = 0, grHoldNum = 0;
    DragMode dmode = dmNone;
    bool hoverCeil = false;
    float dragStart = 0, dragY = 0;

    MenuButton *styleMenu, *osMenu, *ditherMenu, *targetMenu;
    Dial *gainDial, *ceilDial, *lookDial, *atkDial, *relDial, *linkTDial, *linkRDial;
    Button *tpBtn, *dcBtn, *unityBtn, *resetBtn, *rangeBtn;
    ValueBox* customBox;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UILimit)
};

UI* createUI() { return new UILimit(); }

END_NAMESPACE_DISTRHO
