// pump TONE: interface. Input (dim) and output (neon) spectra, the live gain curve around 0 dB (boosts neon, cuts in
// theme.second), the target curve faint, and the five balance controls as big dials.
#include "ui/PumpUI.hpp"
#include "PluginTone.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::tone;

class UITone : public PumpUI {
public:
    UITone() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::tone(), "TONE", 1000, 620, kBypass)
    {
        recDial = addDial(kRecover, "RECOVER", Dial::Large);
        recDial->tip = "Recover: brings up frequencies that are masked or under-represented (never lifts noise)";
        tameDial = addDial(kTame, "TAME", Dial::Large);
        tameDial->tip = "Tame: pulls down frequencies that dominate the balance";
        biasDial = addDial(kBias, "BIAS", Dial::Large, true);
        biasDial->tip = "Bias: + favours taming (cuts), - favours recovering (boosts)";
        brightDial = addDial(kBrighten, "BRIGHTEN", Dial::Large, true);
        brightDial->tip = "Brighten: tilts the target and the curve around 1 kHz (+ brighter, - darker)";
        bodyDial = addDial(kBody, "BODY", Dial::Large, true);
        bodyDial->tip = "Body: low-end shelf on the target, below about 150 Hz (+ fuller, - leaner)";
        recDial->tabGroup = tameDial->tabGroup = biasDial->tabGroup = 1;
        brightDial->tabGroup = bodyDial->tabGroup = 2;
        speedDial = addDial(kSpeed, "SPEED", Dial::Medium);
        speedDial->tip = "Speed: how quickly the balance follows the music (relaxed = smooth and steady, fast = lively)";
        mixDial = addDial(kMix, "MIX", Dial::Medium);
        mixDial->tip = "Mix: processed vs. dry (latency-aligned, no phasing)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        speedDial->tabGroup = mixDial->tabGroup = outDial->tabGroup = 3;
        autoBtn = addToggle(kAutoLevel, "AUTO LEVEL");
        autoBtn->tip = "Auto level: keeps the loudness unchanged, so you hear the balance, not the level (A key)";
        deltaBtn = addToggle(kDelta, "DELTA");
        deltaBtn->holdMomentary = true;
        deltaBtn->tip = "Delta: hear only what TONE changes (click = latch, click-and-hold = while held; D key)";
        qualSeg = addSegmented(kQuality, { "2048", "4096" });
        qualSeg->tip = "Quality: FFT size (scaled with the sample rate). 4096 = finer low end, more latency";

        rangeBtn = addButton("±6", [this]() {
            static const int r[] = { 3, 6, 12 };
            std::vector<std::string> it; int cur = 1;
            for (int i = 0; i < 3; ++i) { it.push_back("Curve scale ±" + std::to_string(r[i]) + " dB"); if ((int)dbRange == r[i]) cur = i; }
            openMenu(rangeBtn->r, it, cur, [this](int i) { static const int rr[] = { 3, 6, 12 }; setRange((float)rr[i]); });
        });
        rangeBtn->small = true;
        rangeBtn->tip = "Display scale of the gain curve";

        if (auto* p = static_cast<PluginTone*>(pluginPtr())) plug = p;
        for (int b = 0; b < Engine::kBands; ++b) target[b] = -120.0f;   // the engine's "nothing there yet"
        // the analyzers run at the rate the plugin feeds them at
        setupAnalyzers(plug && plug->getSampleRate() > 0 ? plug->getSampleRate() : (sampleRate() > 0 ? sampleRate() : 48000.0));
    }
    ~UITone() override
    {
        // closing the editor mid-hold must not leave the master band-passed
        if (plug) plug->auditionOn.store(false);
    }

protected:
    static constexpr double kLo = 20.0, kHi = 20000.0;

    void setupAnalyzers(double sr)
    {
        anaRate = sr;
        pre.setup(sr); post.setup(sr);
        pre.setRange(kLo, kHi); post.setRange(kLo, kHi);
    }
    void onSampleRateChanged(double sr) override { if (sr > 0.0 && sr != anaRate) setupAnalyzers(sr); }

    // ---------------------------------------------------------------- layout
    void layout() override
    {
        display = Rect(14, 58, W - 28 - 34, 350);
        meterL = Rect(display.r() + 12, display.y, 9, display.h - 18);
        meterR = Rect(meterL.r() + 2, display.y, 9, display.h - 18);
        rangeBtn->r = Rect(display.r() - 52, display.y + 8, 44, 22);
        const float y0 = 424, ph = 160;
        panelA = Rect(14, y0, 330, ph);
        panelB = Rect(panelA.r() + 12, y0, 226, ph);
        panelC = Rect(panelB.r() + 12, y0, W - 14 - (panelB.r() + 12), ph);
        const float lh = Dial::heightFor(Dial::Large), mh = Dial::heightFor(Dial::Medium);
        recDial->r = Rect(panelA.x + 12, y0 + 40, 96, lh);
        tameDial->r = Rect(panelA.x + 117, y0 + 40, 96, lh);
        biasDial->r = Rect(panelA.x + 222, y0 + 40, 96, lh);
        brightDial->r = Rect(panelB.x + 13, y0 + 40, 96, lh);
        bodyDial->r = Rect(panelB.x + 117, y0 + 40, 96, lh);
        speedDial->r = Rect(panelC.x + 8, y0 + 34, 76, mh);
        mixDial->r = Rect(panelC.x + 84, y0 + 34, 76, mh);
        outDial->r = Rect(panelC.x + 160, y0 + 34, 76, mh);
        const float bx = panelC.x + 250, bw = panelC.r() - 14 - bx;
        autoBtn->r = Rect(bx, y0 + 30, bw, 26);
        deltaBtn->r = Rect(bx, y0 + 66, bw, 26);
        qualSeg->r = Rect(bx, y0 + 122, bw, 24);
        makeupRect = Rect(panelC.x + 14, y0 + 128, 220, 22);
    }

    // ---------------------------------------------------------------- helpers
    float xOfF(double f) const { return freqToX(display, f, kLo, kHi); }
    double fOfX(float x) const { return xToFreq(display, x, kLo, kHi); }
    float yOfDb(float db) const { return display.cy() - db / dbRange * (display.h * 0.5f - 14.0f); }
    float yOfSpec(float db) const { return display.y + (kSpecTop - db) / (kSpecTop - kSpecBottom) * display.h; }
    static constexpr float kSpecTop = 6.0f, kSpecBottom = -90.0f;
    void setRange(float r) { dbRange = r; rangeBtn->label = "±" + std::to_string((int)r); saveUi(); }
    // Alt-hold listen goes straight to the plugin's atomics (never through host parameters: nothing to save or echo)
    void pushAudition(bool on)
    {
        if (!plug) return;
        plug->auditionFreq.store(listenF); plug->auditionQ.store(listenQ);
        plug->auditionOn.store(on);
    }
    std::string freqText(double f) const { return spec(kAuditionFreq).format((float)f); }
    float curveAt(double f) const { return Engine::interp(gain, erb, f); }

    // ---------------------------------------------------------------- drawing
    void tick(float dt) override
    {
        if (!plug) return;
        // the host may change the rate without telling the UI (or before it opened): follow the plugin's rate
        const double srNow = plug->getSampleRate();
        if (srNow > 0.0 && srNow != anaRate) setupAnalyzers(srNow);
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        pre.update(&plug->preRing, nullptr, dt);
        post.update(&plug->postRing, nullptr, dt);
        const float a = 1.0f - std::exp(-dt / 0.08f);
        for (int b = 0; b < Engine::kBands; ++b)
        {
            erb[b] = plug->engine.bandErb(b);
            // the balance curve, without the auto level makeup (that is shown on its own)
            gain[b] += (plug->engine.uiGain(b) - plug->engine.uiMakeup() - gain[b]) * a;
            target[b] += (plug->engine.uiTarget(b) - target[b]) * (1.0f - std::exp(-dt / 0.3f));
        }
        makeup += (plug->engine.uiMakeup() - makeup) * a;
    }

    void drawBody() override
    {
        screenBox(display);
        save();
        scissor(display.x + 1, display.y + 1, display.w - 2, display.h - 2);
        // grid
        for (int k = -2; k <= 2; ++k)
        {
            const float y = yOfDb(dbRange * k / 2.0f);
            line(display.x, y, display.r(), y, k == 0 ? withAlpha2(theme.ink, 0.28f) : theme.grid, 1.0f);
        }
        drawFreqGrid(display, kLo, kHi);
        // spectra
        drawSpectrum(pre, display, kSpecTop, kSpecBottom, withAlpha2(theme.ink, theme.dark ? 0.08f : 0.05f), withAlpha2(theme.ink, 0.30f), kLo, kHi, 1.0f);
        drawSpectrum(post, display, kSpecTop, kSpecBottom, withAlpha2(theme.neon, theme.dark ? 0.22f : 0.30f), withAlpha2(pump::mixColor(theme.neon, theme.ink, 0.45f), 0.75f), kLo, kHi, 1.1f);
        // target (faint, on the spectrum scale)
        if (showTarget && plug)
        {
            beginPath();
            bool first = true;
            for (int b = 0; b < Engine::kBands; ++b)
            {
                const float x = xOfF(plug->engine.bandFreq(b)), y = std::max(display.y, std::min(display.b(), yOfSpec(target[b])));
                if (first) { moveTo(x, y); first = false; } else lineTo(x, y);
            }
            strokeColor(withAlpha2(theme.ink, 0.45f)); strokeWidth(1.4f); lineJoin(ROUND);
            stroke();
            // dashes would be nicer, nanovg has none: draw small ticks along it instead
            for (int b = 2; b < Engine::kBands; b += 4)
            {
                const float x = xOfF(plug->engine.bandFreq(b)), y = yOfSpec(target[b]);
                if (y > display.y + 4 && y < display.b() - 4) { beginPath(); circle(x, y, 2.0f); fillColor(withAlpha2(theme.ink, 0.45f)); fill(); }
            }
        }
        // the gain curve: boosts neon, cuts in the second colour
        const int NP = 240;
        float xs[NP], ys[NP], ds[NP];
        const float y0 = yOfDb(0.0f);
        for (int p = 0; p < NP; ++p)
        {
            xs[p] = display.x + display.w * p / (NP - 1);
            ds[p] = curveAt(fOfX(xs[p]));
            ys[p] = std::max(display.y + 2.0f, std::min(display.b() - 2.0f, yOfDb(ds[p])));
        }
        for (int sign = 0; sign < 2; ++sign)
        {
            beginPath();
            moveTo(xs[0], y0);
            for (int p = 0; p < NP; ++p) lineTo(xs[p], sign == 0 ? std::min(y0, ys[p]) : std::max(y0, ys[p]));
            lineTo(xs[NP - 1], y0);
            closePath();
            fillColor(sign == 0 ? withAlpha2(theme.neon, theme.dark ? 0.45f : 0.55f) : withAlpha2(theme.second, theme.dark ? 0.40f : 0.42f));
            fill();
        }
        if (theme.dark)
        {
            beginPath();
            for (int p = 0; p < NP; ++p) { if (p == 0) moveTo(xs[p], ys[p]); else lineTo(xs[p], ys[p]); }
            strokeColor(theme.glow); strokeWidth(6.0f); stroke();
        }
        beginPath();
        for (int p = 0; p < NP; ++p) { if (p == 0) moveTo(xs[p], ys[p]); else lineTo(xs[p], ys[p]); }
        strokeColor(theme.ink); strokeWidth(2.2f); lineJoin(ROUND); stroke();
        // the auto level makeup: a faint line at its level
        if (value(kAutoLevel) >= 0.5f && std::fabs(makeup) > 0.05f)
        {
            const float ym = yOfDb(makeup);
            for (float x = display.x + 4; x < display.r() - 40; x += 10) line(x, ym, x + 5, ym, withAlpha2(theme.ink, 0.45f), 1.0f);
            char b[32]; std::snprintf(b, sizeof b, "AUTO LEVEL %+.1f dB", makeup);
            const float ly = ym + (makeup >= 0 ? -9.0f : 9.0f), lw = textWidth(b, kMono, 9.5f) + 8.0f;
            fillRect(Rect(display.x + 6, ly - 7.0f, lw, 14.0f), withAlpha2(theme.screen, 0.85f));
            textAt(display.x + 10, ly, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        // dB labels (curve scale)
        for (int k = -2; k <= 2; ++k)
        {
            if (k == 2) continue;
            const float db = dbRange * k / 2.0f;
            char b[16]; std::snprintf(b, sizeof b, k == 0 ? "0" : "%+.0f", db);
            textAt(display.r() - 8, yOfDb(db) - 7, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        // legend
        {
            float lx = display.x + 12;
            auto key = [&](const Color& c, const char* t, bool lineKey) {
                if (lineKey) line(lx, display.y + 14, lx + 14, display.y + 14, c, 1.6f);
                else { fillRect(Rect(lx, display.y + 9, 12, 10), c); strokeRect(Rect(lx, display.y + 9, 12, 10), theme.ink, 1.0f); }
                textAt(lx + 18, display.y + 14.5f, t, kLabel, 10.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
                lx += 18 + textWidth(t, kLabel, 10.5f, 1.0f) + 16;
            };
            key(withAlpha2(theme.neon, 0.8f), "BOOST", false);
            key(withAlpha2(theme.second, 0.7f), "CUT", false);
            if (showTarget) key(withAlpha2(theme.ink, 0.5f), "TARGET", true);
        }
        // hover readout
        if (hoverX >= display.x && dmode == dmNone && display.contains(hoverX, hoverY))
        {
            const double f = fOfX(hoverX);
            const float g = curveAt(f);
            line(hoverX, display.y, hoverX, display.b(), withAlpha2(theme.ink, 0.35f), 1.0f);
            char b[64]; std::snprintf(b, sizeof b, "%s  %+.1f dB", freqText(f).c_str(), g);   // (balance, without auto level)
            const float tw = textWidth(b, kMono, 11.0f) + 14.0f;
            const float yy = std::max(display.y + 30.0f, std::min(display.b() - 30.0f, yOfDb(g) - 26.0f));
            const Rect tb(std::max(display.x + 6, std::min(display.r() - tw - 6, hoverX + 10)), yy, tw, 22);
            fillRect(tb, theme.ink);
            textAt(tb.cx(), tb.cy(), b, kMono, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // delta banner
        if (value(kDelta) >= 0.5f)
        {
            const std::string s = "DELTA  ·  HEARING ONLY WHAT TONE CHANGES";
            const float tw = textWidth(s, kLabel, 12.0f, 1.0f) + 20.0f;
            const Rect lb(display.cx() - tw * 0.5f, display.b() - 36, tw, 22);
            fillRect(lb, theme.second);
            textAt(lb.cx(), lb.cy(), s, kLabel, 12.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        // Alt-listen
        if (dmode == dmAudition)
        {
            const double f = listenF, q = listenQ;
            const double half = std::asinh(1.0 / (2.0 * q)) / std::log(2.0);
            const float x0 = xOfF(f * std::pow(2.0, -half)), x1 = xOfF(f * std::pow(2.0, half));
            const Color sh = withAlpha2(theme.bg, 0.62f);
            fillRect(Rect(display.x, display.y, std::max(0.0f, x0 - display.x), display.h), sh);
            fillRect(Rect(x1, display.y, std::max(0.0f, display.r() - x1), display.h), sh);
            line(x0, display.y, x0, display.b(), theme.second, 1.5f);
            line(x1, display.y, x1, display.b(), theme.second, 1.5f);
            char qb[24]; std::snprintf(qb, sizeof qb, "  Q %.1f", q);
            const std::string label = "LISTEN  " + freqText(f) + qb;
            const float tw = textWidth(label, kLabel, 12.0f, 1.0f) + 16.0f;
            const Rect lb(std::max(display.x + 6, std::min(display.r() - tw - 6, xOfF(f) - tw * 0.5f)), display.y + 36, tw, 22);
            fillRect(lb, theme.second);
            textAt(lb.cx(), lb.cy(), label, kLabel, 12.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        else if (altHeld && display.contains(hoverX, hoverY))
        {
            const std::string s = "ALT: CLICK AND HOLD TO LISTEN TO THIS REGION";
            const float tw = textWidth(s, kLabel, 11.0f, 1.0f) + 18.0f;
            const Rect lb(display.cx() - tw * 0.5f, display.y + 36, tw, 22);
            fillRect(lb, theme.ink);
            textAt(lb.cx(), lb.cy(), s, kLabel, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        restore();

        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, display.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        for (const Rect* p : { &panelA, &panelB, &panelC }) strokeRect(*p, theme.dim, 1.0f);
        caption(panelA.x + 14, panelA.y + 15, "BALANCE");
        caption(panelB.x + 14, panelB.y + 15, "TARGET");
        caption(panelC.x + 14, panelC.y + 15, "RESPONSE & OUTPUT");
        caption(qualSeg->r.x, qualSeg->r.y - 10, "RESOLUTION");
        // auto level readout
        {
            char b[48];
            if (value(kAutoLevel) >= 0.5f) std::snprintf(b, sizeof b, "AUTO LEVEL %+.1f dB", makeup);
            else std::snprintf(b, sizeof b, "AUTO LEVEL OFF");
            textAt(makeupRect.x, makeupRect.cy() + 4, b, kMono, 11.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
        }
    }

    std::string footerInfo() override
    {
        const int lat = plug ? plug->latencyNow() : 0;
        const double sr = anaRate > 0 ? anaRate : 48000.0;   // the plugin's rate (the latency is in its samples)
        char buf[96];
        std::snprintf(buf, sizeof buf, "48 ERB BANDS  ·  FFT %d  ·  LATENCY %.0f ms", lat, 1000.0 * lat / sr);
        return buf;
    }

    // ---------------------------------------------------------------- mouse on the display (standard Alt-solo)
    enum ToneDragMode { dmNone = 0, dmAudition };

    bool bodyMouseDown(float x, float y, int button, uint mod, bool) override
    {
        if (!display.contains(x, y)) return false;
        if (button == kMouseButtonRight) { displayMenu(x, y); return false; }
        if (button != kMouseButtonLeft) return false;
        if (mod & kModifierAlt)
        {
            dmode = dmAudition;
            listenF = (float)fOfX(x);
            pushAudition(true);
            setStatus("Listening around " + freqText(listenF) + "  (drag: sweep, wheel: width)");
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        hoverX = x; hoverY = y;
        if (dmode == dmAudition)
        {
            listenF = (float)fOfX(std::max(display.x, std::min(display.r(), x)));
            pushAudition(true);
            setStatus("Listening around " + freqText(listenF) + "  (wheel: width)");
        }
    }
    void bodyMouseUp(float, float) override
    {
        if (dmode == dmAudition) { pushAudition(false); setStatus(""); }
        dmode = dmNone;
    }
    void bodyHover(float x, float y) override
    {
        hoverX = x; hoverY = y;
        if (display.contains(x, y))
        {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s  %+.1f dB    Alt + hold: listen here · right-click: display options",
                          freqText(fOfX(x)).c_str(), curveAt(fOfX(x)));
            setStatus(buf);
        }
        else hoverX = -1;
    }
    bool bodyScroll(float, float, float dy, uint mod) override
    {
        if (dmode != dmAudition) return false;
        const float dir = dy > 0 ? 1.0f : -1.0f;
        listenQ = pump::clampv(listenQ * std::pow(2.0f, dir * ((mod & kModifierShift) ? 0.05f : 0.25f)), 0.3f, 12.0f);
        pushAudition(true);
        char b[64]; std::snprintf(b, sizeof b, "Listen width: Q %.2f  (wheel up: narrower)", listenQ); setStatus(b);
        saveUi();
        return true;
    }
    void displayMenu(float x, float y)
    {
        std::vector<std::string> it = { "Curve scale ±3 dB", "Curve scale ±6 dB", "Curve scale ±12 dB", "-", showTarget ? "Hide target curve" : "Show target curve" };
        openMenu(Rect(x, y, 10, 10), it, -1, [this](int i) {
            static const int rr[] = { 3, 6, 12 };
            if (i <= 2) setRange((float)rr[i]);
            else if (i == 4) { showTarget = !showTarget; saveUi(); }
        });
    }

    bool keyPress(uint key, uint mod) override
    {
        if (key == kKeyAltL || key == kKeyAltR) { altHeld = true; repaint(); return false; }
        const bool cmd = (mod & (kModifierControl | kModifierSuper)) != 0;
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        if (!cmd && k == 'd') { setParamGesture(kDelta, value(kDelta) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (!cmd && k == 'a') { setParamGesture(kAutoLevel, value(kAutoLevel) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (!cmd && k == 't') { showTarget = !showTarget; saveUi(); repaint(); return true; }
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
            { "Recover · Tame", "lift what is masked · pull down what dominates (both follow the music)" },
            { "Bias · Brighten · Body", "favour cuts (+) or boosts (-) · tilt the target · low-end shelf on the target" },
            { "D  ·  A  ·  T", "delta on/off · auto level on/off · show/hide the target curve" },
            { "DELTA key", "click = latch, click-and-hold = only while held" },
            { "Right-click the display", "curve scale, target curve" },
        };
    }

    void saveUi()
    {
        char buf[96]; std::snprintf(buf, sizeof buf, "range=%d;target=%d;lq=%.3f;", (int)dbRange, showTarget ? 1 : 0, listenQ);
        saveUiState(buf);
    }
    void loadUiState(const std::string& s) override
    {
        const int r = std::atoi(uiStateGet(s, "range", "6").c_str());
        dbRange = (float)(r == 3 || r == 12 ? r : 6);
        rangeBtn->label = "±" + std::to_string((int)dbRange);
        showTarget = uiStateGet(s, "target", "1") != "0";
        listenQ = pump::clampv((float)std::atof(uiStateGet(s, "lq", "2").c_str()), 0.3f, 12.0f);
    }

private:
    PluginTone* plug = nullptr;
    SpectrumView pre, post;
    Rect display, meterL, meterR, panelA, panelB, panelC, makeupRect;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float gain[Engine::kBands] = {}, target[Engine::kBands] = {};
    double erb[Engine::kBands] = {};
    float makeup = 0.0f, dbRange = 6.0f, hoverX = -1, hoverY = -1;
    float listenF = 1000.0f, listenQ = 2.0f;   // Alt-hold listen (sent to the plugin's atomics; the width is remembered)
    double anaRate = 0.0;                      // sample rate the analyzers are set up for
    bool showTarget = true, altHeld = false;
    ToneDragMode dmode = dmNone;

    Dial *recDial, *tameDial, *biasDial, *brightDial, *bodyDial, *speedDial, *mixDial, *outDial;
    Button *autoBtn, *deltaBtn, *rangeBtn;
    Segmented* qualSeg;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UITone)
};

UI* createUI() { return new UITone(); }

END_NAMESPACE_DISTRHO
