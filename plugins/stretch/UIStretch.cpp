// pump STRETCH: interface. The recording: the last 12 s scrolling by (one peak per column), the write head at the right
// edge, the read head with the window it reads and how far behind the live sound it is, the frozen moment while Freeze
// is on, and where the read head would jump to the newest sound (drag the recording left / right = STRETCH). Below: the
// stretch (the dial, the window, FREEZE and JUMP), tone (the tilt curve over the wash's spectrum) and output (mix and
// output gain).
#include "ui/PumpUI.hpp"
#include "PluginStretch.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::stretch;

class UIStretch : public PumpUI {
public:
    UIStretch() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::stretch(), "STRETCH", 900, 470, kBypass)
    {
        stretchDial = addDial(kStretch, "STRETCH", Dial::Large);
        stretchDial->tip = "Stretch: how many times slower the read head runs, x1.5 .. x50 (or drag the recording)";
        windowSeg = addSegmented(kWindow, { "45 MS", "90 MS", "0.18 S", "0.35 S", "0.7 S" });
        windowSeg->tip = "Window: short = grit and rhythm, long = smooth, dreamy, starts later  (1-5)";
        freezeBtn = addToggle(kFreeze, "FREEZE");
        freezeBtn->tip = "Freeze: hold the moment at the read head as an endless drone  (F)";
        jumpBtn = addButton("JUMP", [this]() { jump(); });
        jumpBtn->tip = "Jump: the read head goes to the newest sound (1 s crossfade)  (J)";
        stretchDial->tabGroup = 1;

        toneDial = addDial(kTone, "TONE", Dial::Medium, true);
        toneDial->tip = "Tone: tilts the wash about 1 kHz, up to 4.5 dB an octave";
        toneDial->tabGroup = 2;

        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: dry to wash (it glides); try a pad or a vocal near 100 %";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        mixDial->tabGroup = outDial->tabGroup = 3;

        if (auto* p = static_cast<PluginStretch*>(pluginPtr())) plug = p;
        setupAnalyzer(rate());
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kStretch: std::snprintf(b, sizeof b, stretchFactor(v) < 9.95 ? "x%.1f" : "x%.0f", stretchFactor(v)); return b;
        case kTone:
            if (std::fabs(v) < 0.5f) return "FLAT";
            std::snprintf(b, sizeof b, "%s %.0f%%", v < 0 ? "DARK" : "BRIGHT", std::fabs(v)); return b;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }
    // typing "12" into STRETCH means x12
    float shownValue(int i, float v) const override { return i == kStretch ? (float)stretchFactor(v) : v; }
    float fromShown(int i, float s) const override
    {
        if (i != kStretch) return s;
        return (float)(std::log(std::max((double)s, kStretchMin) / kStretchMin) / std::log(kStretchMax / kStretchMin));
    }

    void layout() override
    {
        recR = Rect(14, 58, 842, 214);
        meterL = Rect(868, 58, 9, 196);
        meterR = Rect(879, 58, 9, 196);
        const float y0 = 286;
        stretchPanel = Rect(14, y0, 392, 150);
        stretchDial->r = Rect(24, y0 + 34, 92, Dial::heightFor(Dial::Large));
        windowSeg->r = Rect(132, y0 + 38, 262, 28);
        freezeBtn->r = Rect(132, y0 + 78, 126, 38);
        jumpBtn->r = Rect(268, y0 + 78, 126, 38);

        tonePanel = Rect(420, y0, 270, 150);
        toneDial->r = Rect(430, y0 + 42, 72, Dial::heightFor(Dial::Medium));
        specR = Rect(512, y0 + 30, 168, 108);

        outPanel = Rect(704, y0, 182, 150);
        mixDial->r = Rect(710, y0 + 42, 82, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(796, y0 + 42, 82, Dial::heightFor(Dial::Medium));
    }

    double rate() const { return plug && plug->getSampleRate() > 0.0 ? plug->getSampleRate() : 48000.0; }
    void setupAnalyzer(double sr) { anaRate = sr; sv.setup(sr); sv.setRange(kSpecLo, kSpecHi); }
    void onSampleRateChanged(double sr) override { if (sr > 0.0 && sr != anaRate) setupAnalyzer(sr); }
    int windowIndex() const { return pump::clampv((int)value(kWindow), 0, kNumWindows - 1); }

    void jump()
    {
        if (!plug) return;
        plug->requestJump();
        setStatus("Jump: the read head goes to the newest sound");
        jumpFlash = 1.0f;
    }

    void tick(float dt) override
    {
        if (!plug) return;
        if (rate() != anaRate) setupAnalyzer(rate());
        for (int c = 0; c < 2; ++c) outMeter.tickDb(c, pump::gainToDbF(plug->outPeak[c].exchange(0.0f)), dt);
        sv.update(&plug->outRing, nullptr, dt);
        const Engine& e = plug->engine;
        wpos = e.written(); cen = e.readCentre(); hc = e.holdCentre(); frozen = e.isFrozen(); waiting = e.isWaiting();
        winN = e.windowNow(); cap = e.ringSamples();
        const uint32_t j = e.jumps();
        if (j != jumpsSeen) { jumpsSeen = j; jumpFlash = 1.0f; }
        jumpFlash = std::max(0.0f, jumpFlash - dt * 1.5f);
        frozenShow += ((frozen ? 1.0f : 0.0f) - frozenShow) * std::min(1.0f, dt * 12.0f);
    }

    // the wave's height for a peak (log-compressed, so a quiet part is still seen)
    static float shown(float pk) { return std::min(1.0f, std::log10(1.0f + 60.0f * pk) / 1.7853f); }

    void drawBody() override
    {
        drawRecording();
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, recR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        strokeRect(stretchPanel, theme.dim, 1.0f);
        strokeRect(tonePanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(stretchPanel.x + 12, stretchPanel.y + 16, "STRETCH");
        caption(tonePanel.x + 12, tonePanel.y + 16, "TONE");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        caption(windowSeg->r.x, stretchPanel.y + 32, "WINDOW");
        const double sr = rate();
        const int N = plug ? plug->engine.windowSamples(windowIndex()) : kWindowSamples[windowIndex()];
        char t[96];
        std::snprintf(t, sizeof t, "WINDOW %.0f MS   HOP %.0f MS   1 S OF SOUND TAKES %.1f S", 1000.0 * N / sr, 250.0 * N / sr, stretchFactor(value(kStretch)));
        textAt(stretchPanel.x + 12, stretchPanel.b() - 12, t, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        drawTone();
    }

    // ---- the recording ----
    void drawRecording()
    {
        const Rect& r = recR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "RECORDING");
        textAt(r.x + 94, r.y + 14, "THE LAST 12 S", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        char t[128];
        const double sr = rate();
        const double stretch = stretchFactor(value(kStretch));
        std::snprintf(t, sizeof t, "x%.1f SLOWER", stretch);
        textAt(r.r() - 10, r.y + 14, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);

        const Rect p(r.x + 12, r.y + 30, r.w - 24, r.h - 30 - 34);
        if (!plug || cap <= 0) return;
        const double span = (double)cap;
        auto X = [&](double s) { return p.r() - (float)(((double)wpos - s) / span) * p.w; };
        const int64_t cols = plug->engine.columns();
        const int colLen = plug->engine.columnSamples();
        save();
        scissor(p.x, p.y, p.w, p.h);
        // time grid: every 2 s back from now
        for (int k = 1; k <= 5; ++k) line(p.r() - (float)(2.0 * k * sr / span) * p.w, p.y, p.r() - (float)(2.0 * k * sr / span) * p.w, p.b(), theme.grid, 1.0f);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.22f), 1.0f);
        // the part of the ring older than the read head may get (the head jumps before it gets there): dimmed
        const double limit = plug->engine.jumpBehindSeconds(winN) * sr;
        const float xl = p.r() - (float)(limit / span) * p.w;
        fillRect(Rect(p.x, p.y, std::max(0.0f, xl - p.x), p.h), withAlpha2(theme.dim, theme.dark ? 0.22f : 0.16f));
        // the recording, one bar per column, mirrored about the middle line
        const float half = p.h * 0.5f - 3.0f;
        beginPath();
        const int64_t first = std::max<int64_t>(0, cols - Engine::kHistCols + 2);
        bool any = false;
        std::vector<float>& top = scratchTop; top.clear();
        std::vector<float>& xs = scratchX; xs.clear();
        for (int64_t c = first; c <= cols; ++c)
        {
            const float pk = c == cols ? plug->engine.columnNow() : plug->engine.columnPeak(c);
            const float xx = X((double)c * colLen);
            if (xx < p.x - 2.0f) continue;
            xs.push_back(xx); top.push_back(shown(pk) * half);
            any = true;
        }
        if (any)
        {
            moveTo(xs.front(), p.cy() - top.front());
            for (size_t i = 1; i < xs.size(); ++i) lineTo(xs[i], p.cy() - top[i]);
            lineTo(p.r(), p.cy() - top.back());
            lineTo(p.r(), p.cy() + top.back());
            for (size_t i = xs.size(); i-- > 0;) lineTo(xs[i], p.cy() + top[i]);
            closePath();
            fillColor(withAlpha2(theme.neon, theme.dark ? 0.55f : 0.85f)); fill();
            strokeColor(theme.ink); strokeWidth(1.0f); lineJoin(ROUND); stroke();
        }
        // the window the read head is reading (the frozen moment's, while frozen)
        const double cx = frozen ? hc : cen;
        const double w0 = cx - winN * 0.5, w1 = cx + winN * 0.5;
        float xa = X(w0), xb = X(w1);
        const float xr = X(cx);
        const bool offLeft = xr < p.x;
        xa = std::max(xa, p.x); xb = std::max(xb, p.x + 3.0f);
        if (!offLeft) fillRect(Rect(xa, p.y, std::max(3.0f, xb - xa), p.h), withAlpha2(theme.second, theme.dark ? 0.26f : 0.20f));
        // the read head
        const float xrc = std::max(xr, p.x + 1.0f);
        line(xrc, p.y, xrc, p.b(), theme.second, 2.0f);
        // the write head
        line(p.r() - 1.0f, p.y, p.r() - 1.0f, p.b(), theme.ink, 2.5f);
        // the jump limit
        for (float yy = p.y; yy < p.b(); yy += 8.0f) line(xl, yy, xl, std::min(p.b(), yy + 4.0f), withAlpha2(theme.ink2, 0.8f), 1.2f);
        // how far behind: a double arrow between the read head and the write head
        const float ya = p.y + 11.0f;
        const double behind = std::max(0.0, ((double)wpos - cx) / sr);
        {
            const float xs0 = xrc + 3.0f, xs1 = p.r() - 6.0f;
            if (xs1 - xs0 > 14.0f)
            {
                line(xs0, ya, xs1, ya, theme.second, 1.5f);
                beginPath(); moveTo(xs0, ya); lineTo(xs0 + 6, ya - 4); lineTo(xs0 + 6, ya + 4); closePath(); fillColor(theme.second); fill();
                beginPath(); moveTo(xs1, ya); lineTo(xs1 - 6, ya - 4); lineTo(xs1 - 6, ya + 4); closePath(); fillColor(theme.second); fill();
            }
        }
        restore();
        // labels on top of the picture
        std::snprintf(t, sizeof t, "%.1f S BEHIND", behind);
        const float lw = textWidth(t, kMono, 10.5f, 0.5f) + 12.0f;
        const float lx = pump::clampv(0.5f * (xrc + p.r()) - lw * 0.5f, p.x + 2.0f, p.r() - lw - 2.0f);
        fillRect(Rect(lx, ya - 9.0f, lw, 18.0f), theme.screen);
        strokeRect(Rect(lx, ya - 9.0f, lw, 18.0f), theme.second, 1.0f);
        textAt(lx + lw * 0.5f, ya, t, kMono, 10.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.5f);
        // heads' names at the bottom of the picture
        const float yn = p.b() - 9.0f;
        textAt(p.r() - 6, yn, "WRITE", kLabel, 10.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE, 1.2f);
        if (!offLeft)
        {
            const float xt = pump::clampv(xrc + 5.0f, p.x + 2.0f, p.r() - 120.0f);
            textAt(xt, yn, frozen ? "FROZEN" : (waiting ? "READ (WAITING)" : "READ"), kLabel, 10.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.2f);
        }
        else textAt(p.x + 6, yn, "FROZEN, OLDER THAN 12 S", kLabel, 10.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.2f);
        textAt(xl - 4.0f, p.y + 38.0f, "JUMP", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        // time axis
        for (int k = 0; k <= 6; ++k)
        {
            const float xx = p.r() - (float)(2.0 * k * sr / span) * p.w;
            char b[16]; std::snprintf(b, sizeof b, k == 0 ? "NOW" : "-%d S", 2 * k);
            textAt(xx, r.b() - 22.0f, b, kMono, 9.0f, theme.ink2, (k == 0 ? ALIGN_RIGHT : (k == 6 ? ALIGN_LEFT : ALIGN_CENTER)) | ALIGN_MIDDLE);
        }
        // read-out line
        const double jb = plug->engine.jumpBehindSeconds(winN);
        std::snprintf(t, sizeof t, "READ HEAD %.1f S BEHIND   JUMPS AT %.1f S", behind, jb);
        textAt(r.x + 10, r.b() - 9.0f, t, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        const char* st = frozen ? "FROZEN: THE HELD MOMENT, DRIFTING" : (waiting ? "SILENCE: WAITING AT THE NEWEST SOUND" : "READING THE RECORDING");
        textAt(r.r() - 10, r.b() - 9.0f, jumpFlash > 0.05f && !frozen ? "JUMPED TO THE NEWEST SOUND" : st, kMono, 10.0f, jumpFlash > 0.05f ? theme.ink : theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ---- tone: the wash's spectrum, with the tilt curve over it ----
    void drawTone()
    {
        const Rect& r = specR;
        screenBox(r);
        const Rect p(r.x + 4, r.y + 4, r.w - 8, r.h - 8);
        save();
        scissor(p.x, p.y, p.w, p.h);
        drawFreqGrid(p, kSpecLo, kSpecHi, false);
        drawSpectrum(sv, p, -6.0f, -96.0f, withAlpha2(theme.neon, theme.dark ? 0.22f : 0.38f), withAlpha2(pump::mixColor(theme.neon, theme.ink, 0.5f), 0.9f), kSpecLo, kSpecHi, 1.1f);
        // the tilt: +-4.5 dB an octave about 1 kHz (+12 / -24 dB at most), on a +-18 dB scale around the middle
        const double tone = pump::clampv((double)value(kTone) * 0.01, -1.0, 1.0);
        auto yOf = [&](double dbv) { return (float)(p.cy() - dbv / 18.0 * p.h * 0.45); };
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        beginPath();
        constexpr int N = 60;
        for (int i = 0; i <= N; ++i)
        {
            const double f = kSpecLo * std::pow(kSpecHi / kSpecLo, (double)i / N);
            const double g = pump::clampv(std::pow(std::max(f, 30.0) * 0.001, tone * 0.75), 0.063, 4.0);
            const float x = p.x + p.w * i / N, y = yOf(20.0 * std::log10(g));
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.second); strokeWidth(1.25f); lineJoin(ROUND); stroke();
        restore();
        textAt(r.x + 7, r.y + 12, "WASH", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(r.r() - 7, r.b() - 11, "TILT", kLabel, 9.0f, theme.second, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        textAt(freqToX(p, 1000.0, kSpecLo, kSpecHi), r.b() - 11, "1K", kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // ---- mouse: drag the recording left / right = STRETCH ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft || !recR.contains(x, y)) return false;
        if (dbl || cmdKey(mod)) { setParamGesture(kStretch, spec(kStretch).def); return false; }
        dragging = true; dragX = x; dragA = norm(kStretch);
        beginEdit(kStretch);
        return true;
    }
    void bodyMouseDrag(float x, float, uint mod) override
    {
        if (!dragging) return;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        setParamNorm(kStretch, dragA + (x - dragX) * 0.0016f * f);
        if (mod & kModifierShift) { dragA = norm(kStretch); dragX = x; }
        setStatus(paramStatus(kStretch));
    }
    void bodyMouseUp(float, float) override
    {
        if (dragging) endEdit(kStretch);
        dragging = false;
    }
    void bodyHover(float x, float y) override
    {
        if (recR.contains(x, y)) setStatus("Drag left/right = stretch (Shift = fine, double-click = reset, wheel)");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (!recR.contains(x, y)) return false;
        const float step = (mod & kModifierShift) ? 0.004f : 0.03f;
        beginEdit(kStretch); setParamNorm(kStretch, norm(kStretch) + (dy > 0 ? step : -step)); endEdit(kStretch);
        setStatus(paramStatus(kStretch));
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '5') { setParamGesture(kWindow, (float)(key - '1')); return true; }
        if (key == 'f' || key == 'F') { setParamGesture(kFreeze, value(kFreeze) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (key == 'j' || key == 'J') { jump(); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the recording", "left/right = stretch (double-click resets)" },
            { "F", "freeze the moment at the read head" },
            { "J", "jump the read head to the newest sound" },
            { "1 - 5", "window: 45 ms .. 0.7 s" },
        };
    }
    std::string footerInfo() override
    {
        char buf[128];
        const double sr = rate();
        const int N = plug ? plug->engine.windowSamples(windowIndex()) : kWindowSamples[windowIndex()];
        std::snprintf(buf, sizeof buf, "X%.1f   WINDOW %.0f MS   %s   LATENCY 0", stretchFactor(value(kStretch)), 1000.0 * N / sr, value(kFreeze) >= 0.5f ? "FROZEN" : "LIVE");
        return buf;
    }

private:
    static constexpr double kSpecLo = 30.0, kSpecHi = 20000.0;
    PluginStretch* plug = nullptr;
    Rect recR, meterL, meterR, stretchPanel, tonePanel, outPanel, specR;
    OutMeter outMeter;
    SpectrumView sv;
    double anaRate = 0.0;
    int64_t wpos = 0; double cen = 0.0, hc = 0.0; int winN = 2048, cap = 0; bool frozen = false, waiting = false;
    uint32_t jumpsSeen = 0; float jumpFlash = 0.0f, frozenShow = 0.0f;
    std::vector<float> scratchTop, scratchX;
    bool dragging = false; float dragX = 0.0f, dragA = 0.0f;

    Dial *stretchDial, *toneDial, *mixDial, *outDial;
    Segmented* windowSeg;
    Button *freezeBtn, *jumpBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIStretch)
};

UI* createUI() { return new UIStretch(); }

END_NAMESPACE_DISTRHO
