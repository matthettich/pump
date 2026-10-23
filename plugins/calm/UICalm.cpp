// pump CALM: interface. A resonance suppressor-style display: input spectrum (dim), output spectrum (neon), the live reduction
// hanging from the top, the sensitivity curve with six draggable nodes and the focus range with draggable edges.
#include "ui/PumpUI.hpp"
#include "PluginCalm.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::calm;

class UICalm : public PumpUI {
public:
    UICalm() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::calm(), "CALM", 1000, 620, kBypass)
    {
        // ---- suppression ----
        depthDial = addDial(kDepth, "DEPTH", Dial::Large);
        depthDial->tip = "Depth: how much the resonances are pulled down (0% = nothing changes)";
        sharpDial = addDial(kSharpness, "SHARPNESS", Dial::Medium);
        sharpDial->tip = "Sharpness: narrow notches on exact frequencies (high) or broad, smooth dips (low)";
        selDial = addDial(kSelectivity, "SELECTIVITY", Dial::Medium);
        selDial->tip = "Selectivity: only peaks that stick out far and keep ringing (high), or anything above the spectral shape (low)";
        depthDial->tabGroup = sharpDial->tabGroup = selDial->tabGroup = 1;
        modeSeg = addSegmented(kMode, { "SOFT", "HARD" });
        modeSeg->vertical = true;
        modeSeg->tip = "Soft: gentle, leaves part of each peak (musical). Hard: pulls peaks right down onto the spectral shape";

        // ---- timing / resolution ----
        atkDial = addDial(kAttack, "ATTACK", Dial::Medium);
        atkDial->tip = "Attack: how fast the reduction grabs a resonance";
        relDial = addDial(kRelease, "RELEASE", Dial::Medium);
        relDial->tip = "Release: how fast it lets go (longer = smoother, fewer artifacts)";
        atkDial->tabGroup = relDial->tabGroup = 2;
        qualMenu = addMenu(kQuality, "FFT");
        qualMenu->itemsFn = []() { return std::vector<std::string> { "1024  fast, coarse", "2048  balanced", "4096  fine", "8192  finest, slow" }; };
        qualMenu->textFn = [this]() { return std::string(kQualityNames[pump::clampv((int)value(kQuality), 0, 3)]) + "  " + latencyText(); };
        qualMenu->tip = "Quality: FFT size (scaled with the sample rate). Bigger = finer frequency resolution, more latency";

        // ---- stereo / output ----
        stereoSeg = addSegmented(kStereo, { "LINKED", "L / R", "M / S" });
        stereoSeg->tip = "Stereo: Linked (one detector, same cut on both sides), Stereo L/R or Mid/Side (two detectors)";
        deltaBtn = addToggle(kDelta, "DELTA");
        deltaBtn->holdMomentary = true;
        deltaBtn->tip = "Delta: hear only what is being removed (click = latch, click-and-hold = while held; D key)";
        balDial = addDial(kBalance, "BALANCE", Dial::Medium, true);
        balDial->tip = "Balance (L/R, M/S): where the processing goes. -100% = left/mid only, +100% = right/side only";
        mixDial = addDial(kMix, "MIX", Dial::Medium);
        mixDial->tip = "Mix: processed vs. dry (latency-aligned, no phasing)";
        trimDial = addDial(kTrim, "TRIM", Dial::Medium, true);
        trimDial->tip = "Output trim";
        balDial->tabGroup = mixDial->tabGroup = trimDial->tabGroup = 3;

        grRangeBtn = addButton("GR 12", [this]() {
            static const int r[] = { 6, 12, 24 };
            std::vector<std::string> it; int cur = 1;
            for (int i = 0; i < 3; ++i) { it.push_back("Reduction scale " + std::to_string(r[i]) + " dB"); if ((int)grRange == r[i]) cur = i; }
            openMenu(grRangeBtn->r, it, cur, [this](int i) { static const int rr[] = { 6, 12, 24 }; setGrRange((float)rr[i]); });
        });
        grRangeBtn->small = true;
        grRangeBtn->tip = "Scale of the reduction curve hanging from the top";

        if (auto* p = static_cast<PluginCalm*>(pluginPtr())) plug = p;
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        pre.setup(sr); post.setup(sr);
        pre.setRange(kLo, kHi); post.setRange(kLo, kHi);
        updateEnabled();
    }

protected:
    static constexpr double kLo = 20.0, kHi = 20000.0;

    // ---------------------------------------------------------------- layout
    void layout() override
    {
        display = Rect(14, 58, W - 28 - 34, 350);
        meterL = Rect(display.r() + 12, display.y, 9, display.h - 18);
        meterR = Rect(meterL.r() + 2, display.y, 9, display.h - 18);
        grRangeBtn->r = Rect(display.r() - 62, display.y + 8, 54, 22);
        const float y0 = 424, ph = 160;
        panelA = Rect(14, y0, 392, ph);
        panelB = Rect(panelA.r() + 12, y0, 196, ph);
        panelC = Rect(panelB.r() + 12, y0, W - 14 - (panelB.r() + 12), ph);
        // suppression
        depthDial->r = Rect(panelA.x + 14, y0 + 34, 92, Dial::heightFor(Dial::Large));
        sharpDial->r = Rect(panelA.x + 116, y0 + 42, 82, Dial::heightFor(Dial::Medium));
        selDial->r = Rect(panelA.x + 204, y0 + 42, 82, Dial::heightFor(Dial::Medium));
        modeSeg->r = Rect(panelA.r() - 88, y0 + 44, 74, 64);
        // timing
        atkDial->r = Rect(panelB.x + 12, y0 + 26, 82, Dial::heightFor(Dial::Medium));
        relDial->r = Rect(panelB.x + 102, y0 + 26, 82, Dial::heightFor(Dial::Medium));
        qualMenu->r = Rect(panelB.x + 14, y0 + 124, panelB.w - 28, 24);
        // stereo / output
        stereoSeg->r = Rect(panelC.x + 14, y0 + 30, 222, 26);
        deltaBtn->r = Rect(panelC.x + 248, y0 + 30, panelC.w - 248 - 16, 26);
        balDial->r = Rect(panelC.x + 8, y0 + 72, 76, Dial::heightFor(Dial::Medium));
        mixDial->r = Rect(panelC.x + 86, y0 + 72, 76, Dial::heightFor(Dial::Medium));
        trimDial->r = Rect(panelC.x + 164, y0 + 72, 76, Dial::heightFor(Dial::Medium));
        grMeter = Rect(panelC.r() - 108, y0 + 88, 92, 12);
    }

    // ---------------------------------------------------------------- helpers
    float nodeFreq(int n) const { return value(nodeParam(n, kNFreq)); }
    float nodeGain(int n) const { return value(nodeParam(n, kNGain)); }
    float nodeQ(int n) const { return value(nodeParam(n, kNQ)); }

    float xOfF(double f) const { return freqToX(display, f, kLo, kHi); }
    double fOfX(float x) const { return xToFreq(display, x, kLo, kHi); }
    // sensitivity curve: +-12 dB around a line a little below the middle (the top belongs to the reduction)
    float sensMid() const { return display.y + display.h * 0.56f; }
    float sensScale() const { return display.h * 0.34f / 12.0f; }
    float yOfSens(float db) const { return sensMid() - db * sensScale(); }
    float sensOfY(float y) const { return (sensMid() - y) / sensScale(); }
    float yOfRed(float db) const { return display.y + db / grRange * (display.h * 0.42f); }

    void nodePos(int n, float& x, float& y) const
    {
        x = xOfF(nodeFreq(n));
        y = std::max(display.y + 9.0f, std::min(display.b() - 9.0f, yOfSens(nodeGain(n))));
    }
    int nodeAt(float x, float y) const
    {
        int best = -1; float bd = 12.0f * 12.0f;
        for (int n = 0; n < kNumNodes; ++n)
        {
            float nx, ny; nodePos(n, nx, ny);
            const float d = (nx - x) * (nx - x) + (ny - y) * (ny - y);
            if (d < bd) { bd = d; best = n; }
        }
        return best;
    }
    // 0 = low focus edge, 1 = high, -1 none
    int focusEdgeAt(float x, float y) const
    {
        if (!display.contains(x, y)) return -1;
        const float xl = xOfF(value(kFocusLow)), xh = xOfF(value(kFocusHigh));
        // the handles are drawn inside the border, so grab them there too
        const float hl = std::max(display.x + 7.0f, std::min(display.r() - 7.0f, xl)), hh = std::max(display.x + 7.0f, std::min(display.r() - 7.0f, xh));
        const bool inHandleRow = y > display.b() - 52.0f && y < display.b() - 26.0f;
        const float dl = std::min(std::fabs(x - xl), inHandleRow ? std::fabs(x - hl) : 1e9f), dh = std::min(std::fabs(x - xh), inHandleRow ? std::fabs(x - hh) : 1e9f);
        if (dl < 6.0f && dl <= dh) return 0;
        if (dh < 6.0f) return 1;
        return -1;
    }
    static const char* nodeName(int n)
    {
        static const char* names[] = { "Low shelf", "Bell 2", "Bell 3", "Bell 4", "Bell 5", "High shelf" };
        return names[n];
    }
    std::string nodeReadout(int n) const
    {
        char q[32]; std::snprintf(q, sizeof q, "  Q %.2f", nodeQ(n));
        return std::string("Sensitivity ") + std::to_string(n + 1) + " (" + nodeName(n) + ")  " + spec(nodeParam(n, kNFreq)).format(nodeFreq(n)) +
               "  " + spec(nodeParam(n, kNGain)).format(nodeGain(n)) + q;
    }
    std::string nodeShort(int n) const
    {
        char q[24]; std::snprintf(q, sizeof q, "  Q %.2f", nodeQ(n));
        return "S" + std::to_string(n + 1) + "  " + spec(nodeParam(n, kNFreq)).format(nodeFreq(n)) + "  " + spec(nodeParam(n, kNGain)).format(nodeGain(n)) + q;
    }
    std::string latencyText() const
    {
        const int lat = plug ? plug->latencyNow() : fftSizeFor((int)value(kQuality), sampleRate() > 0 ? sampleRate() : 48000.0);
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        char b[32]; std::snprintf(b, sizeof b, "%.0f ms", 1000.0 * lat / sr);
        return b;
    }
    // A hidden, non-automatable audition parameter: tell the host (edit begin/end) but keep it out of undo.
    void setHidden(int i, float v)
    {
        if (spec(i).clamp(v) == value(i)) return;
        editParameter((uint32_t)i, true);
        setParam(i, v);
        editParameter((uint32_t)i, false);
    }
    void setGrRange(float r)
    {
        grRange = r;
        grRangeBtn->label = "GR " + std::to_string((int)r);
        saveUi();
    }
    void updateEnabled()
    {
        balDial->enabled = (int)value(kStereo) != kLinked;
    }
    void onParamChanged(int i, float) override { if (i == kStereo) updateEnabled(); }
    void onPresetApplied() override { updateEnabled(); sel = -1; }

    // ---------------------------------------------------------------- drawing
    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        pre.update(&plug->preRing, nullptr, dt);
        post.update(&plug->postRing, nullptr, dt);
        // reduction curves: rise at once, fall smoothly (the display shouldn't flicker more than the sound)
        const float fall = 1.0f - std::exp(-dt / 0.12f);
        for (int c = 0; c < 2; ++c)
            for (int p = 0; p < Engine::kUiPoints; ++p)
            {
                const float v = plug->engine.uiReduction(c, p);
                red[c][p] = v > red[c][p] ? v : red[c][p] + (v - red[c][p]) * fall;
            }
        const float g = plug->engine.grMax.exchange(0.0f);
        grNow = g > grNow ? g : grNow + (g - grNow) * (1.0f - std::exp(-dt / 0.25f));
    }

    Color mixDeep() const { return pump::mixColor(theme.neon, theme.ink, 0.45f); }
    Color chan2Color() const { return pump::mixColor(theme.second, theme.red, theme.dark ? 0.6f : 0.7f); }   // salmon next to the orange

    void drawBody() override
    {
        screenBox(display);
        save();
        scissor(display.x + 1, display.y + 1, display.w - 2, display.h - 2);
        drawFreqGrid(display, kLo, kHi);
        // sensitivity dB lines
        for (int k = -2; k <= 2; ++k)
        {
            const float y = yOfSens(6.0f * k);
            line(display.x, y, display.r(), y, k == 0 ? withAlpha2(theme.ink, 0.22f) : theme.grid, 1.0f);
        }
        // spectra (0 dBFS-ish at the top, -90 at the bottom): input dim, output neon
        const float top = 18.0f, bottom = -84.0f;   // a little low: the top belongs to the reduction
        drawSpectrum(pre, display, top, bottom, withAlpha2(theme.ink, theme.dark ? 0.08f : 0.05f), withAlpha2(theme.ink, 0.30f), kLo, kHi, 1.0f);
        drawSpectrum(post, display, top, bottom, withAlpha2(theme.neon, theme.dark ? 0.26f : 0.36f), withAlpha2(mixDeep(), 0.75f), kLo, kHi, 1.1f);

        // focus range: shade what is outside, edges with handles
        const float xl = xOfF(value(kFocusLow)), xh = xOfF(value(kFocusHigh));
        const Color shade = withAlpha2(theme.bg, theme.dark ? 0.62f : 0.58f);
        if (xl > display.x) fillRect(Rect(display.x, display.y, xl - display.x, display.h), shade);
        if (xh < display.r()) fillRect(Rect(xh, display.y, display.r() - xh, display.h), shade);
        for (int e = 0; e < 2; ++e)
        {
            const float x = e == 0 ? xl : xh;
            const bool hot = hoverEdge == e || (dmode == dmFocus && dragEdge == e);
            line(x, display.y, x, display.b(), hot ? theme.ink : theme.ink2, hot ? 2.0f : 1.2f);
            // the handle stays inside the display (an edge at 20 Hz / 20 kHz sits on the border)
            const float hx = std::max(display.x + 7.0f, std::min(display.r() - 7.0f, x));
            const Rect hnd(hx - 5.0f, display.b() - 50.0f, 10.0f, 22.0f);
            Color hc = hot ? theme.neon : theme.bg;
            fillRect(hnd, hc);
            strokeRect(hnd, theme.ink, 1.2f);
            line(hx - 1.5f, hnd.y + 6, hx - 1.5f, hnd.b() - 6, theme.ink, 1.0f);
            line(hx + 1.5f, hnd.y + 6, hx + 1.5f, hnd.b() - 6, theme.ink, 1.0f);
        }

        // the live reduction, hanging from the top
        const bool two = (int)value(kStereo) != kLinked;
        for (int c = two ? 1 : 0; c >= 0; --c)
        {
            const Color col = c == 0 ? theme.second : chan2Color();
            beginPath();
            moveTo(display.x, display.y);
            for (int p = 0; p < Engine::kUiPoints; ++p)
                lineTo(xOfF(Engine::uiPointFreq(p)), std::min(display.b(), yOfRed(red[c][p])));
            lineTo(display.r(), display.y);
            closePath();
            fillColor(withAlpha2(col, c == 0 ? (theme.dark ? 0.30f : 0.26f) : 0.16f));
            fill();
            beginPath();
            for (int p = 0; p < Engine::kUiPoints; ++p)
            {
                const float x = xOfF(Engine::uiPointFreq(p)), y = std::min(display.b(), yOfRed(red[c][p]));
                if (p == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(col); strokeWidth(c == 0 ? 1.6f : 1.2f); lineJoin(ROUND); stroke();
        }
        // reduction scale (left edge)
        for (int k = 1; k <= 2; ++k)
        {
            const float db = grRange * k / 2.0f;
            char b[16]; std::snprintf(b, sizeof b, "-%.0f", db);
            textAt(display.x + 6, yOfRed(db), b, kMono, 9.5f, pump::mixColor(theme.second, theme.ink, theme.dark ? 0.15f : 0.45f), ALIGN_LEFT | ALIGN_MIDDLE);
        }
        if (two)
        {
            // legend: a swatch per detector
            const bool ms = (int)value(kStereo) == kMidSide;
            float lx = display.x + 40;
            for (int c = 0; c < 2; ++c)
            {
                const char* t = c == 0 ? (ms ? "MID" : "LEFT") : (ms ? "SIDE" : "RIGHT");
                fillRect(Rect(lx, display.y + 8, 10, 10), c == 0 ? theme.second : chan2Color());
                strokeRect(Rect(lx, display.y + 8, 10, 10), theme.ink, 1.0f);
                textAt(lx + 15, display.y + 13.5f, t, kLabel, 10.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
                lx += 15 + textWidth(t, kLabel, 10.5f, 1.0f) + 16;
            }
        }

        // sensitivity curve (and the selected node's own shape)
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        NodeSpec ns[kNumNodes];
        for (int n = 0; n < kNumNodes; ++n) { ns[n].freq = nodeFreq(n); ns[n].gain = nodeGain(n); ns[n].q = nodeQ(n); }
        SensitivityCurve sc; sc.set(ns, sr);
        const int NP = 200;
        float xs[NP], ys[NP];
        for (int p = 0; p < NP; ++p)
        {
            xs[p] = display.x + display.w * p / (NP - 1);
            ys[p] = std::max(display.y + 2.0f, std::min(display.b() - 2.0f, yOfSens((float)sc.db(fOfX(xs[p])))));
        }
        const int focusNode = dragNode >= 0 ? dragNode : (hover >= 0 ? hover : sel);
        if (focusNode >= 0 && std::fabs(nodeGain(focusNode)) > 0.05f)
        {
            beginPath();
            moveTo(xs[0], yOfSens(0));
            for (int p = 0; p < NP; ++p) lineTo(xs[p], std::max(display.y + 2.0f, std::min(display.b() - 2.0f, yOfSens((float)sc.nodeDb(focusNode, fOfX(xs[p]))))));
            lineTo(xs[NP - 1], yOfSens(0));
            closePath();
            fillColor(withAlpha2(theme.neon, theme.dark ? 0.22f : 0.30f));
            fill();
        }
        beginPath();
        moveTo(xs[0], yOfSens(0));
        for (int p = 0; p < NP; ++p) lineTo(xs[p], ys[p]);
        lineTo(xs[NP - 1], yOfSens(0));
        closePath();
        fillColor(withAlpha2(theme.ink, 0.05f));
        fill();
        if (theme.dark)
        {
            beginPath();
            for (int p = 0; p < NP; ++p) { if (p == 0) moveTo(xs[p], ys[p]); else lineTo(xs[p], ys[p]); }
            strokeColor(theme.glow); strokeWidth(6.0f); stroke();
        }
        beginPath();
        for (int p = 0; p < NP; ++p) { if (p == 0) moveTo(xs[p], ys[p]); else lineTo(xs[p], ys[p]); }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        // sensitivity scale (right edge)
        for (int k = -2; k <= 2; ++k)
        {
            char b[16]; std::snprintf(b, sizeof b, k == 0 ? "0" : "%+d", 6 * k);
            textAt(display.r() - 8, yOfSens(6.0f * k) - 7, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        textAt(display.r() - 8, yOfSens(12.0f) - 20, "SENSITIVITY", kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);

        // nodes
        for (int n = 0; n < kNumNodes; ++n)
        {
            float x, y; nodePos(n, x, y);
            const bool isSel = n == sel || n == dragNode, hov = n == hover, flat = std::fabs(nodeGain(n)) < 0.05f;
            const float rr = isSel ? 9.0f : 8.0f;
            beginPath(); circle(x + 2.0f, y + 2.0f, rr); fillColor(theme.dim); fill();
            beginPath(); circle(x, y, rr);
            fillColor(isSel ? theme.neon : (hov ? theme.panel2 : theme.bg));
            fill();
            strokeColor(flat && !isSel && !hov ? theme.ink2 : theme.ink); strokeWidth(isSel ? 2.0f : 1.5f); stroke();
            textAt(x, y + 0.5f, std::to_string(n + 1), kBold, 9.5f, isSel ? theme.neonInk : (flat ? theme.ink2 : theme.ink), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // readout near the node being dragged / hovered
        const int info = dragNode >= 0 ? dragNode : hover;
        if (info >= 0 && dmode != dmAudition && dmode != dmSoloNode)
        {
            float x, y; nodePos(info, x, y);
            char q[24]; std::snprintf(q, sizeof q, "  Q %.2f", nodeQ(info));
            const std::string s = std::to_string(info + 1) + "  " + spec(nodeParam(info, kNFreq)).format(nodeFreq(info)) + "  " + spec(nodeParam(info, kNGain)).format(nodeGain(info)) + q;
            const float tw = textWidth(s, kMono, 11.0f) + 14.0f;
            const Rect tb(std::max(display.x + 6, std::min(display.r() - tw - 6, x + 14)), y < display.y + 40 ? y + 14 : y - 34, tw, 22);
            fillRect(tb, theme.ink);
            textAt(tb.cx(), tb.cy(), s, kMono, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // focus readout while dragging / hovering an edge
        const int fe = dmode == dmFocus ? dragEdge : hoverEdge;
        if (fe >= 0 && info < 0)
        {
            const int pi = fe == 0 ? kFocusLow : kFocusHigh;
            const std::string s = std::string(fe == 0 ? "FOCUS FROM " : "FOCUS TO ") + spec(pi).format(value(pi));
            const float tw = textWidth(s, kLabel, 11.0f, 0.8f) + 16.0f, x = fe == 0 ? xl : xh;
            const Rect tb(std::max(display.x + 6, std::min(display.r() - tw - 6, fe == 0 ? x + 10 : x - tw - 10)), display.b() - 74, tw, 22);
            fillRect(tb, theme.ink);
            textAt(tb.cx(), tb.cy(), s, kLabel, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        }
        // delta banner
        if (value(kDelta) >= 0.5f)
        {
            const std::string s = "DELTA  ·  HEARING ONLY WHAT IS REMOVED";
            const float tw = textWidth(s, kLabel, 12.0f, 1.0f) + 20.0f;
            const Rect lb(display.cx() - tw * 0.5f, display.b() - 36, tw, 22);
            fillRect(lb, theme.second);
            textAt(lb.cx(), lb.cy(), s, kLabel, 12.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        // Alt-listen / node solo: shade everything but the auditioned region
        if (dmode == dmAudition || dmode == dmSoloNode)
        {
            const double f = value(kAuditionFreq), q = value(kAuditionQ);
            const int region = (int)value(kAuditionRegion);
            float x0 = display.x, x1 = display.r();
            if (region == pump::Audition::kBand)
            {
                const double half = std::asinh(1.0 / (2.0 * q)) / std::log(2.0);
                x0 = xOfF(f * std::pow(2.0, -half)); x1 = xOfF(f * std::pow(2.0, half));
            }
            else if (region == pump::Audition::kBelow) x1 = xOfF(f);
            else x0 = xOfF(f);
            const Color sh = withAlpha2(theme.bg, 0.62f);
            if (x0 > display.x) fillRect(Rect(display.x, display.y, x0 - display.x, display.h), sh);
            if (x1 < display.r()) fillRect(Rect(x1, display.y, display.r() - x1, display.h), sh);
            if (x0 > display.x) line(x0, display.y, x0, display.b(), theme.second, 1.5f);
            if (x1 < display.r()) line(x1, display.y, x1, display.b(), theme.second, 1.5f);
            std::string label = dmode == dmAudition ? "LISTEN  " + spec(kAuditionFreq).format((float)f)
                                                    : "SOLO SENSITIVITY " + std::to_string(soloNode + 1) + "  " + spec(kAuditionFreq).format((float)f);
            if (dmode == dmAudition) { char qb[24]; std::snprintf(qb, sizeof qb, "  Q %.1f", q); label += qb; }
            const float tw = textWidth(label, kLabel, 12.0f, 1.0f) + 16.0f;
            const float cx = region == pump::Audition::kBand ? xOfF(f) : 0.5f * (x0 + x1);
            const Rect lb(std::max(display.x + 6, std::min(display.r() - tw - 6, cx - tw * 0.5f)), display.y + 36, tw, 22);
            fillRect(lb, theme.second);
            textAt(lb.cx(), lb.cy(), label, kLabel, 12.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        else if (altHeld && display.contains(mouseX, mouseY))
        {
            const std::string s = "ALT: CLICK AND HOLD TO LISTEN  ·  ON A NODE: SOLO ITS BAND";
            const float tw = textWidth(s, kLabel, 11.0f, 1.0f) + 18.0f;
            const Rect lb(display.cx() - tw * 0.5f, display.y + 36, tw, 22);
            fillRect(lb, theme.ink);
            textAt(lb.cx(), lb.cy(), s, kLabel, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        restore();

        // output meters
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, display.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        // panels
        for (const Rect* p : { &panelA, &panelB, &panelC }) strokeRect(*p, theme.dim, 1.0f);
        caption(panelA.x + 14, panelA.y + 15, "SUPPRESSION");
        caption(modeSeg->r.x, panelA.y + 15, "MODE");
        caption(panelB.x + 14, panelB.y + 15, "TIMING");
        caption(panelB.x + 14, qualMenu->r.y - 10, "RESOLUTION");
        caption(panelC.x + 14, panelC.y + 15, "STEREO");
        caption(deltaBtn->r.x, panelC.y + 15, "LISTEN");
        // broadband reduction meter (horizontal, grows to the right)
        caption(grMeter.x, grMeter.y - 11, "REDUCTION");
        {
            fillRect(grMeter, theme.panel2);
            const float w = std::min(1.0f, std::max(0.0f, grNow / 6.0f)) * grMeter.w;
            if (w > 0.5f) fillRect(Rect(grMeter.x, grMeter.y, w, grMeter.h), theme.second);
            strokeRect(grMeter, theme.ink, 1.0f);
            for (int k = 1; k < 6; ++k) { const float x = grMeter.x + grMeter.w * k / 6.0f; line(x, grMeter.b() - 3, x, grMeter.b(), theme.ink2, 1.0f); }
            char b[24]; std::snprintf(b, sizeof b, grNow < 0.05f ? "0.0 dB" : "-%.1f dB", grNow);
            textAt(grMeter.x, grMeter.b() + 14, b, kMono, 11.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
            textAt(grMeter.r(), grMeter.b() + 14, "6 dB", kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
    }

    std::string footerInfo() override
    {
        char buf[128];
        const int lat = plug ? plug->latencyNow() : 0;
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        static const char* const modes[] = { "SOFT", "HARD" };
        static const char* const st[] = { "LINKED", "STEREO L/R", "MID/SIDE" };
        std::snprintf(buf, sizeof buf, "%s  ·  %s  ·  FFT %d  ·  LATENCY %.0f ms", modes[pump::clampv((int)value(kMode), 0, 1)],
                      st[pump::clampv((int)value(kStereo), 0, 2)], lat, 1000.0 * lat / sr);
        return buf;
    }

    // ---------------------------------------------------------------- mouse on the display
    // EQ conventions on the sensitivity nodes, the Alt-solo on the display:
    //   drag a node: frequency + sensitivity (Shift: fine); Ctrl/Cmd + vertical drag or the wheel: width (Q);
    //   Ctrl/Cmd + wheel: sensitivity; double-click: type frequency, Tab to sensitivity and Q; right-click: menu
    //   Alt + click-and-hold a node: hear only that node's band; Alt + hold / drag empty space: listen to the
    //   region under the mouse (drag to sweep, wheel = width); drag a focus edge to set the processing range
    enum CalmDragMode { dmNone = 0, dmNode, dmFocus, dmAudition, dmSoloNode };

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!display.contains(x, y)) return false;
        const bool alt = (mod & kModifierAlt) != 0, cmd = (mod & (kModifierControl | kModifierSuper)) != 0;
        const int n = nodeAt(x, y);
        const int edge = n < 0 ? focusEdgeAt(x, y) : -1;
        mouseX = x; mouseY = y;
        if (button == kMouseButtonRight)
        {
            if (n >= 0) { sel = n; nodeMenu(n, x, y); }
            else if (edge >= 0) focusMenu(edge, x, y);
            else backgroundMenu(x, y);
            return false;
        }
        if (button != kMouseButtonLeft) return false;
        if (alt)
        {
            if (n >= 0)
            {
                // solo this node's band while the button is held
                soloNode = n;
                const NodeType t = nodeType(n);
                setHidden(kAuditionRegion, (float)(t == kNodeLowShelf ? pump::Audition::kBelow : (t == kNodeHighShelf ? pump::Audition::kAbove : pump::Audition::kBand)));
                setHidden(kAuditionFreq, nodeFreq(n));
                setHidden(kAuditionQ, std::max(0.3f, nodeQ(n)));
                setHidden(kAudition, 1.0f);
                dmode = dmSoloNode;
                setStatus("Solo: only node " + std::to_string(n + 1) + "'s region (release to return)");
                return true;
            }
            dmode = dmAudition;
            setHidden(kAuditionRegion, (float)pump::Audition::kBand);
            setHidden(kAuditionQ, listenQ);
            setHidden(kAuditionFreq, (float)fOfX(x));
            setHidden(kAudition, 1.0f);
            setStatus("Listening around " + spec(kAuditionFreq).format(value(kAuditionFreq)) + "  (drag: sweep, wheel: width)");
            return true;
        }
        if (n >= 0)
        {
            sel = n;
            if (dbl) { openNodeEntry(n); return false; }
            startNodeDrag(n, x, y, cmd);
            return true;
        }
        if (edge >= 0)
        {
            if (dbl) { openFocusEntry(edge); return false; }
            dragEdge = edge;
            beginEdit(edge == 0 ? kFocusLow : kFocusHigh);
            dmode = dmFocus;
            return true;
        }
        sel = -1;
        return false;
    }

    void startNodeDrag(int n, float x, float y, bool)
    {
        dragNode = n;
        origF = nodeFreq(n); origG = nodeGain(n); origQ = nodeQ(n);
        beginGroup();
        beginEdit(nodeParam(n, kNFreq)); beginEdit(nodeParam(n, kNGain)); beginEdit(nodeParam(n, kNQ));
        dmode = dmNode;
        lastX = x; lastY = y; effX = effY = 0.0f;
    }

    void bodyMouseDrag(float x, float y, uint mod) override
    {
        mouseX = x; mouseY = y;
        const bool cmd = (mod & (kModifierControl | kModifierSuper)) != 0;
        const float fine = (mod & kModifierShift) ? 0.15f : 1.0f;
        switch (dmode)
        {
        case dmAudition:
            setHidden(kAuditionFreq, (float)fOfX(std::max(display.x, std::min(display.r(), x))));
            setStatus("Listening around " + spec(kAuditionFreq).format(value(kAuditionFreq)) + "  (wheel: width)");
            return;
        case dmFocus: {
            const int pi = dragEdge == 0 ? kFocusLow : kFocusHigh;
            float f = (float)fOfX(std::max(display.x, std::min(display.r(), x)));
            // keep at least a third of an octave between the edges
            if (dragEdge == 0) f = std::min(f, value(kFocusHigh) / 1.26f); else f = std::max(f, value(kFocusLow) * 1.26f);
            setParam(pi, f);
            setStatus(paramStatus(pi));
            return;
        }
        case dmNode: {
            effX += (x - lastX) * fine;
            effY += (y - lastY) * fine;
            lastX = x; lastY = y;
            const int n = dragNode;
            setParam(nodeParam(n, kNFreq), (float)(origF * std::pow(kHi / kLo, (double)(effX / display.w))));
            if (cmd)
            {
                // width: up = narrower (higher Q), relative to where the Ctrl drag started
                if (!qMode) { qMode = true; qBaseY = effY; origQ = nodeQ(n); origG = nodeGain(n); }
                setParam(nodeParam(n, kNQ), origQ * std::pow(2.0f, -(effY - qBaseY) / 50.0f));
            }
            else
            {
                if (qMode) { qMode = false; origG = nodeGain(n); effY = 0.0f; lastY = y; origF = nodeFreq(n); effX = 0.0f; lastX = x; }
                setParam(nodeParam(n, kNGain), origG - effY / sensScale());
            }
            setStatus(nodeReadout(n));
            return;
        }
        default: return;
        }
    }

    void bodyMouseUp(float, float) override
    {
        switch (dmode)
        {
        case dmNode:
            endEdit(nodeParam(dragNode, kNFreq)); endEdit(nodeParam(dragNode, kNGain)); endEdit(nodeParam(dragNode, kNQ));
            endGroup();
            break;
        case dmFocus: endEdit(dragEdge == 0 ? kFocusLow : kFocusHigh); break;
        case dmAudition: case dmSoloNode: setHidden(kAudition, 0.0f); setStatus(""); break;
        default: break;
        }
        dmode = dmNone;
        dragNode = -1; dragEdge = -1; qMode = false;
    }

    void bodyHover(float x, float y) override
    {
        mouseX = x; mouseY = y;
        hover = display.contains(x, y) ? nodeAt(x, y) : -1;
        hoverEdge = hover < 0 ? focusEdgeAt(x, y) : -1;
        if (hover >= 0) setStatus(nodeShort(hover) + "   drag · Ctrl: width · Alt: solo · right-click");
        else if (hoverEdge >= 0) setStatus(paramStatus(hoverEdge == 0 ? kFocusLow : kFocusHigh) + "   drag: processing range edge");
        else if (display.contains(x, y))
        {
            const double f = fOfX(x);
            int p = (int)std::floor(std::log(f / Engine::kUiLo) / std::log(Engine::kUiHi / Engine::kUiLo) * Engine::kUiPoints);
            p = pump::clampv(p, 0, Engine::kUiPoints - 1);
            char buf[200];
            std::snprintf(buf, sizeof buf, "%s  ·  reduction -%.1f dB    Alt + hold: listen here",
                          spec(kAuditionFreq).format((float)f).c_str(), red[0][p]);
            setStatus(buf);
        }
    }

    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        const float dir = dy > 0 ? 1.0f : -1.0f;
        const bool shift = (mod & kModifierShift) != 0, cmd = (mod & (kModifierControl | kModifierSuper)) != 0;
        if (dmode == dmAudition)
        {
            // wheel up = narrower
            listenQ = pump::clampv(value(kAuditionQ) * std::pow(2.0f, dir * (shift ? 0.05f : 0.25f)), 0.3f, 12.0f);
            setHidden(kAuditionQ, listenQ);
            char b[64]; std::snprintf(b, sizeof b, "Listen width: Q %.2f  (wheel up: narrower)", listenQ); setStatus(b);
            saveUi();
            return true;
        }
        int n = nodeAt(x, y);
        if (n < 0 && sel >= 0 && std::fabs(x - xOfF(nodeFreq(sel))) < 30.0f) n = sel;
        if (n < 0) return false;
        if (cmd) setParamGesture(nodeParam(n, kNGain), nodeGain(n) + dir * (shift ? 0.1f : 0.5f));
        else setParamGesture(nodeParam(n, kNQ), nodeQ(n) * std::pow(2.0f, dir * (shift ? 0.02f : 0.1f)));
        setStatus(nodeReadout(n));
        return true;
    }

    void openNodeEntry(int n)
    {
        float nx, ny; nodePos(n, nx, ny);
        const Rect r(std::max(display.x + 4, std::min(display.r() - 104, nx - 50)), ny < display.y + 50 ? ny + 16 : ny - 38, 100, 22);
        openTextEntry(nodeParam(n, kNFreq), r, { { nodeParam(n, kNFreq), r }, { nodeParam(n, kNGain), r }, { nodeParam(n, kNQ), r } });
        setStatus("Type (1k, A4, C#3+13) · Tab: frequency, sensitivity, Q · Enter");
    }
    void openFocusEntry(int edge)
    {
        const float x = xOfF(value(edge == 0 ? kFocusLow : kFocusHigh));
        const Rect r(std::max(display.x + 4, std::min(display.r() - 104, x - 50)), display.b() - 76, 100, 22);
        openTextEntry(edge == 0 ? kFocusLow : kFocusHigh, r, edge == 0 ? std::vector<std::pair<int, Rect>> { { kFocusLow, r }, { kFocusHigh, r } }
                                                                      : std::vector<std::pair<int, Rect>> { { kFocusHigh, r }, { kFocusLow, r } });
        setStatus("Type the edge (150, 1.2k, A4) · Tab: other edge · Enter");
    }

    void resetNode(int n)
    {
        beginGroup();
        setParamGesture(nodeParam(n, kNFreq), kNodeDefFreq[n]);
        setParamGesture(nodeParam(n, kNGain), 0.0f);
        setParamGesture(nodeParam(n, kNQ), kNodeDefQ[n]);
        endGroup();
    }
    void nodeMenu(int n, float x, float y)
    {
        std::vector<std::string> it = { std::string("#SENSITIVITY ") + std::to_string(n + 1) + " · " + nodeName(n), "Reset node", "Sensitivity 0 dB", "Reset width",
                                        "Type values…", "-", "Solo while held: Alt + click", "-", "Reset all nodes" };
        openMenu(Rect(x, y, 10, 10), it, -1, [this, n](int i) {
            if (i == 1) resetNode(n);
            else if (i == 2) setParamGesture(nodeParam(n, kNGain), 0.0f);
            else if (i == 3) setParamGesture(nodeParam(n, kNQ), kNodeDefQ[n]);
            else if (i == 4) openNodeEntry(n);
            else if (i == 8) { beginGroup(); for (int k = 0; k < kNumNodes; ++k) resetNode(k); endGroup(); }
        });
    }
    void focusMenu(int edge, float x, float y)
    {
        std::vector<std::string> it = { "#FOCUS RANGE", "Type value…", "Reset this edge", "Full range (20 Hz - 20 kHz)", "Reset focus range" };
        openMenu(Rect(x, y, 10, 10), it, -1, [this, edge](int i) {
            const int pi = edge == 0 ? kFocusLow : kFocusHigh;
            if (i == 1) openFocusEntry(edge);
            else if (i == 2) setParamGesture(pi, spec(pi).def);
            else if (i == 3) { beginGroup(); setParamGesture(kFocusLow, 20.0f); setParamGesture(kFocusHigh, 20000.0f); endGroup(); }
            else if (i == 4) { beginGroup(); setParamGesture(kFocusLow, spec(kFocusLow).def); setParamGesture(kFocusHigh, spec(kFocusHigh).def); endGroup(); }
        });
    }
    void backgroundMenu(float x, float y)
    {
        std::vector<std::string> it = { "Reset sensitivity curve", "Reset focus range", "-", "Reduction scale 6 dB", "Reduction scale 12 dB", "Reduction scale 24 dB" };
        openMenu(Rect(x, y, 10, 10), it, -1, [this](int i) {
            if (i == 0) { beginGroup(); for (int k = 0; k < kNumNodes; ++k) resetNode(k); endGroup(); }
            else if (i == 1) { beginGroup(); setParamGesture(kFocusLow, spec(kFocusLow).def); setParamGesture(kFocusHigh, spec(kFocusHigh).def); endGroup(); }
            else if (i >= 3) { static const int rr[] = { 6, 12, 24 }; setGrRange((float)rr[i - 3]); }
        });
    }

    bool keyPress(uint key, uint mod) override
    {
        const bool cmd = (mod & (kModifierControl | kModifierSuper)) != 0, shift = (mod & kModifierShift) != 0;
        if (key == kKeyAltL || key == kKeyAltR) { altHeld = true; repaint(); return false; }
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        if (!cmd && k == 'd') { setParamGesture(kDelta, value(kDelta) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (!cmd && k == 'h') { setParamGesture(kMode, value(kMode) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (key == kKeyLeft || key == kKeyRight)
        {
            sel = sel < 0 ? (key == kKeyLeft ? kNumNodes - 1 : 0) : (sel + (key == kKeyLeft ? kNumNodes - 1 : 1)) % kNumNodes;
            setStatus(nodeReadout(sel)); repaint();
            return true;
        }
        if ((key == kKeyUp || key == kKeyDown) && sel >= 0)
        {
            setParamGesture(nodeParam(sel, kNGain), nodeGain(sel) + (key == kKeyUp ? 1.0f : -1.0f) * (shift ? 0.1f : 0.5f));
            setStatus(nodeReadout(sel));
            return true;
        }
        if ((key == kKeyDelete || key == kKeyBackspace) && sel >= 0) { resetNode(sel); setStatus(nodeReadout(sel)); return true; }
        if (key == kKeyEscape) { sel = -1; repaint(); return true; }
        return false;
    }
    bool keyRelease(uint key, uint) override
    {
        if (key == kKeyAltL || key == kKeyAltR) { altHeld = false; repaint(); }
        return false;
    }

    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Alt + hold / drag the display", "listen: hear only the region under the mouse (drag to sweep, wheel: width)" },
            { "Alt + click-and-hold a node", "solo: hear only that sensitivity node's band while held" },
            { "Drag a node", "frequency and sensitivity (up = more reduction there); Shift: fine" },
            { "Ctrl/Cmd + drag up/down", "node width (Q); mouse wheel on a node: width, Ctrl/Cmd + wheel: sensitivity" },
            { "Double-click a node", "type frequency, Tab to sensitivity and Q (1k, A4, C#3+13)" },
            { "Right-click a node / display", "node menu (reset node, width) · display menu (reset curve, scale)" },
            { "Drag a focus edge", "set the processing range (soft-edged); double-click: type it" },
            { "◀ ▶  ·  ▲ ▼  ·  Delete", "select node · its sensitivity ±0.5 dB (Shift ±0.1) · reset it" },
            { "D  ·  H", "delta on/off (hear what is removed) · Soft/Hard mode" },
            { "DELTA key", "click = latch, click-and-hold = only while held" },
        };
    }

    void saveUi()
    {
        char buf[96]; std::snprintf(buf, sizeof buf, "gr=%d;lq=%.3f;", (int)grRange, listenQ);
        saveUiState(buf);
    }
    void loadUiState(const std::string& s) override
    {
        const int r = std::atoi(uiStateGet(s, "gr", "12").c_str());
        grRange = (float)(r == 6 || r == 24 ? r : 12);
        grRangeBtn->label = "GR " + std::to_string((int)grRange);
        listenQ = pump::clampv((float)std::atof(uiStateGet(s, "lq", "2").c_str()), 0.3f, 12.0f);
    }

private:
    PluginCalm* plug = nullptr;
    SpectrumView pre, post;
    Rect display, meterL, meterR, panelA, panelB, panelC, grMeter;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float red[2][Engine::kUiPoints] = {};
    float grNow = 0.0f, grRange = 12.0f, listenQ = 2.0f;
    int sel = -1, hover = -1, hoverEdge = -1, dragNode = -1, dragEdge = -1, soloNode = 0;
    CalmDragMode dmode = dmNone;
    float origF = 1000, origG = 0, origQ = 1, lastX = 0, lastY = 0, effX = 0, effY = 0, qBaseY = 0, mouseX = -1, mouseY = -1;
    bool qMode = false, altHeld = false;

    Dial *depthDial, *sharpDial, *selDial, *atkDial, *relDial, *balDial, *mixDial, *trimDial;
    Segmented *modeSeg, *stereoSeg;
    MenuButton* qualMenu;
    Button *deltaBtn, *grRangeBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UICalm)
};

UI* createUI() { return new UICalm(); }

END_NAMESPACE_DISTRHO
