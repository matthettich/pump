// pump multiband UI: the display behaviour BAND, WIDE and HEAT share (an identical copy lives in each plugin folder
// so the plugins stay independent). Band regions between draggable crossover handles, a strip of per-band S / M / B
// keys across the top of the display, and the multiband / standard interaction set:
//   drag a crossover handle (Shift: fine), double-click it to type (Tab: next crossover), wheel on it: 1/12 octave
//   Alt + click-and-hold a band: hear only that band while held (slide across to switch bands)
//   click a band: select it; drag up/down in a band: its main amount; Ctrl/Cmd + drag up/down: its second amount
//   wheel in a band: main amount (Shift: fine); double-click a band: type its main amount (Tab: second amount)
//   right-click a band: band menu; keys 1-4 select, Left/Right step, S / M / B toggle the selected band, Esc unsolos
// S / M / B keys: click toggles, click-and-hold acts only while held, Ctrl/Cmd-click makes it exclusive.
#pragma once

#include "ui/PumpUI.hpp"
#include "dsp/Multiband.hpp"

START_NAMESPACE_DISTRHO

class MultibandUI : public PumpUI {
public:
    MultibandUI(const ParamSpec* specs, uint32_t numParams, const std::vector<Preset>* presets, const Accent& accent,
                const char* tag, float w, float h, int bypassParam)
        : PumpUI(specs, numParams, presets, accent, tag, w, h, bypassParam) {}

protected:
    static constexpr double kLo = 15.0, kHi = 22000.0;
    static constexpr float kStripH = 26.0f;
    static constexpr int kBands = pump::mb::kMaxBands;

    // ---- what a plugin provides ---------------------------------------------------------------------------------
    virtual int pBands() const = 0;
    virtual int pXover(int k) const = 0;
    virtual int pListen() const = 0;               // hidden parameter: 0 = off, 1..4 = hear only that band
    virtual int pSolo(int b) const = 0;
    virtual int pMute(int) const { return -1; }
    virtual int pBandBypass(int) const { return -1; }
    virtual int pMain(int b) const = 0;            // wheel / plain drag
    virtual int pSecond(int b) const = 0;          // Ctrl/Cmd + drag
    virtual int bandFirstParam(int b) const = 0;   // the band's parameters are contiguous
    virtual int bandParamCount() const = 0;
    virtual std::string mainName() const = 0;      // "threshold", "width", "drive"
    virtual std::string secondName() const = 0;    // "ratio", "stereoize", "band mix"
    virtual std::string stripValue(int) { return ""; }
    virtual void onSelect(int) {}
    // a plugin-specific grab inside a band (return true to own the drag)
    virtual bool regionGrab(int, float, float, uint) { return false; }
    virtual void regionGrabDrag(float, float, uint) {}
    virtual void regionGrabEnd() {}
    // main / second amount from the value at the press and the vertical travel (pixels, up > 0; Shift already applied)
    virtual float mainFromDrag(int b, float start, float up) { const ParamSpec& s = spec(pMain(b)); return s.fromNorm(s.toNorm(start) + up * 0.004f); }
    virtual float secondFromDrag(int b, float start, float up) { const ParamSpec& s = spec(pSecond(b)); return s.fromNorm(s.toNorm(start) + up * 0.004f); }
    virtual float mainWheel(int b, float v, float dir, bool fine) { const ParamSpec& s = spec(pMain(b)); return s.fromNorm(s.toNorm(v) + dir * (fine ? 0.004f : 0.025f)); }
    virtual std::vector<std::pair<std::string, std::string>> pluginHelp() { return {}; }

    Rect disp;     // the display (set in layout())
    int sel = 0;   // selected band

    // ---- geometry ------------------------------------------------------------------------------------------------
    int numBands() const { return pump::clampv((int)value(pBands()), 1, kBands); }
    double rate() const { const double s = sampleRate(); return s > 0 ? s : 48000.0; }
    void effXo(double e[3]) const
    {
        const float x[3] = { value(pXover(0)), value(pXover(1)), value(pXover(2)) };
        pump::mb::effectiveCrossovers(numBands(), x, rate(), e);
    }
    float xOf(double f) const { return freqToX(disp, f, kLo, kHi); }
    double fOf(float x) const { return xToFreq(disp, x, kLo, kHi); }
    void regionX(int b, float& x0, float& x1) const
    {
        double e[3]; effXo(e);
        const int nb = numBands();
        x0 = b == 0 ? disp.x : std::max(disp.x, xOf(e[b - 1]));
        x1 = b >= nb - 1 ? disp.r() : std::min(disp.r(), xOf(e[b]));
    }
    int regionAt(float x) const
    {
        double e[3]; effXo(e);
        const int nb = numBands();
        int b = 0;
        for (int k = 0; k < nb - 1; ++k) if (x >= xOf(e[k])) b = k + 1;
        return b;
    }
    Rect xoverTab(int k) const
    {
        double e[3]; effXo(e);
        const float w = 62.0f, x = xOf(e[k]);
        float y = disp.b() - 40.0f;
        // stagger tabs that would overlap their left neighbour
        if (k > 0 && x - xOf(e[k - 1]) < w + 4.0f && (k % 2) == 1) y -= 22.0f;
        return Rect(std::max(disp.x + 2.0f, std::min(disp.r() - w - 2.0f, x - w * 0.5f)), y, w, 18.0f);
    }
    int xoverAt(float x, float y) const
    {
        if (y < disp.y + kStripH) return -1;
        double e[3]; effXo(e);
        const int nb = numBands();
        int best = -1; float bd = 6.0f;
        for (int k = 0; k < nb - 1; ++k)
        {
            if (xoverTab(k).contains(x, y)) return k;
            const float d = std::fabs(x - xOf(e[k]));
            if (d < bd) { bd = d; best = k; }
        }
        return best;
    }
    static const char* bandName(int b, int nb)
    {
        static const char* n1[] = { "FULL" };
        static const char* n2[] = { "LOW", "HIGH" };
        static const char* n3[] = { "LOW", "MID", "HIGH" };
        static const char* n4[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
        switch (nb) { case 1: return n1[0]; case 2: return n2[b & 1]; case 3: return n3[std::min(b, 2)]; default: return n4[std::min(b, 3)]; }
    }
    std::string bandRange(int b) const
    {
        double e[3]; effXo(e);
        const int nb = numBands();
        auto f = [](double v) { char t[24]; if (v >= 1000.0) std::snprintf(t, sizeof t, "%.3gk", v / 1000.0); else std::snprintf(t, sizeof t, "%.0f", v); return std::string(t); };
        if (nb == 1) return "full range";
        if (b == 0) return "below " + f(e[0]) + " Hz";
        if (b == nb - 1) return "above " + f(e[nb - 2]) + " Hz";
        return f(e[b - 1]) + " - " + f(e[b]) + " Hz";
    }

    // ---- selection, solo / mute / bypass -------------------------------------------------------------------------
    void select(int b)
    {
        b = pump::clampv(b, 0, numBands() - 1);
        if (b == sel) return;
        sel = b;
        onSelect(b);
        repaint();
    }
    bool bandFlag(int b, int t) const { const int p = flagParam(b, t); return p >= 0 && value(p) >= 0.5f; }
    int flagParam(int b, int t) const { return t == 0 ? pSolo(b) : (t == 1 ? pMute(b) : pBandBypass(b)); }
    bool anySolo() const { for (int b = 0; b < numBands(); ++b) if (bandFlag(b, 0)) return true; return false; }
    int listening() const { return (int)value(pListen()) - 1; }
    // Ctrl/Cmd-click on S / M / B: this band only (again: clear them all)
    void exclusive(int b, int t)
    {
        const int nb = numBands();
        bool onlyThis = bandFlag(b, t);
        for (int k = 0; k < nb; ++k) if (k != b && bandFlag(k, t)) onlyThis = false;
        beginGroup();
        for (int k = 0; k < kBands; ++k)
        {
            const int p = flagParam(k, t);
            if (p >= 0) setParamGesture(p, onlyThis ? 0.0f : (k == b ? 1.0f : 0.0f));
        }
        endGroup();
        static const char* what[] = { "Solo", "Mute", "Bypass" };
        setStatus(onlyThis ? std::string(what[t]) + " cleared on all bands" : std::string(what[t]) + ": band " + std::to_string(b + 1) + " only");
    }
    // A change that is not an undo step (momentary listening). Still a host gesture.
    void setQuiet(int p, float v)
    {
        const bool h = fApplyingHistory;
        fApplyingHistory = true;
        setParamGesture(p, v);
        fApplyingHistory = h;
    }
    void stopListening() { if (value(pListen()) != 0.0f) { setQuiet(pListen(), 0.0f); setStatus(""); } }

    // Call from the plugin UI's constructor (after its own controls): the S / M / B keys in the strip.
    void buildStrip()
    {
        static const char* lab[] = { "S", "M", "B" };
        static const char* tips[] = {
            "Solo this band (click: on/off · hold: only while held · Ctrl/Cmd+click: solo only this band)",
            "Mute this band (click: on/off · hold: only while held · Ctrl/Cmd+click: mute only this band)",
            "Bypass this band's processing (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)" };
        for (int b = 0; b < kBands; ++b)
            for (int t = 0; t < 3; ++t)
            {
                stripBtn[b][t] = nullptr;
                const int p = flagParam(b, t);
                if (p < 0) continue;
                Button* bt = addToggle(p, lab[t]);
                bt->small = true;
                bt->holdMomentary = true;
                bt->tip = tips[t];
                // (a Ctrl/Cmd press must not be undone by the momentary release: push its press time out of reach)
                bt->onExclusive = [this, b, t, bt]() { exclusive(b, t); bt->pressTime = 1e300; };
                stripBtn[b][t] = bt;
            }
    }
    void layoutStrip()
    {
        const int nb = numBands();
        for (int b = 0; b < kBands; ++b)
        {
            float x0 = 0, x1 = 0;
            if (b < nb) regionX(b, x0, x1);
            int n = 0;
            for (int t = 0; t < 3; ++t) n += stripBtn[b][t] ? 1 : 0;
            const float bw = 19.0f, gap = 4.0f, need = n * bw + (n - 1) * gap + 34.0f;
            const bool show = b < nb && x1 - x0 >= need;
            float x = x1 - 7.0f - n * bw - (n - 1) * gap;
            for (int t = 0; t < 3; ++t)
            {
                Button* bt = stripBtn[b][t];
                if (!bt) continue;
                bt->visible = show;
                bt->r = Rect(x, disp.y + 4.0f, bw, 17.0f);
                x += bw + gap;
            }
        }
    }

    // ---- drawing -------------------------------------------------------------------------------------------------
    // Region tints and the strip (after screenBox, before the plugin's content).
    void drawRegionsUnder()
    {
        layoutStrip();
        const int nb = numBands();
        for (int b = 0; b < nb; ++b)
        {
            float x0, x1; regionX(b, x0, x1);
            if (b == sel) fillRect(Rect(x0, disp.y + kStripH, x1 - x0, disp.h - kStripH), withAlpha2(theme.neon, theme.dark ? 0.07f : 0.11f));
            else if (b == hoverBand && dmode == dmNone) fillRect(Rect(x0, disp.y + kStripH, x1 - x0, disp.h - kStripH), withAlpha2(theme.ink, 0.025f));
            fillRect(Rect(x0, disp.y, x1 - x0, kStripH), b == sel ? withAlpha2(theme.neon, theme.dark ? 0.30f : 0.40f) : withAlpha2(theme.panel2, 0.8f));
        }
        line(disp.x, disp.y + kStripH, disp.r(), disp.y + kStripH, theme.dim, 1.0f);
    }
    // Crossover handles, strip labels and the solo / listen dimming (after the plugin's content).
    void drawRegionsOver()
    {
        const int nb = numBands();
        double e[3]; effXo(e);
        // dim bands that aren't heard
        const int ls = dmode == dmListen ? listenBand : listening();
        const bool solo = anySolo();
        for (int b = 0; b < nb; ++b)
        {
            float x0, x1; regionX(b, x0, x1);
            const bool heard = ls >= 0 ? b == ls : (solo ? bandFlag(b, 0) : !bandFlag(b, 1));
            if (!heard) fillRect(Rect(x0, disp.y + kStripH, x1 - x0, disp.h - kStripH), withAlpha2(theme.screen, ls >= 0 ? 0.72f : 0.55f));
            if (bandFlag(b, 2) && x1 - x0 > 60)
                textAt(0.5f * (x0 + x1), disp.y + kStripH + 14.0f, "BYPASSED", kLabel, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.5f);
        }
        // crossover lines and tabs
        for (int k = 0; k < nb - 1; ++k)
        {
            const float x = xOf(e[k]);
            const bool hot = k == hoverXo || (dmode == dmXover && dragXo == k);
            line(x, disp.y, x, disp.b(), hot ? theme.ink : withAlpha2(theme.ink, theme.dark ? 0.55f : 0.45f), hot ? 2.0f : 1.4f);
            const Rect t = xoverTab(k);
            fillRect(Rect(t.x + 2, t.y + 2, t.w, t.h), theme.dim);
            fillRect(t, hot ? theme.ink : theme.bg);
            strokeRect(t, theme.ink, 1.2f);
            textAt(t.cx(), t.cy() + 0.5f, spec(pXover(k)).format((float)e[k]), kMono, 10.0f, hot ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // strip labels: number key, name, value
        for (int b = 0; b < nb; ++b)
        {
            float x0, x1; regionX(b, x0, x1);
            const float w = x1 - x0;
            if (w < 22.0f) continue;
            const Rect key(x0 + 5.0f, disp.y + 4.0f, 17.0f, 17.0f);
            const bool isSel = b == sel;
            fillRect(key, isSel ? theme.ink : theme.bg);
            strokeRect(key, theme.ink, 1.2f);
            textAt(key.cx(), key.cy() + 0.5f, std::to_string(b + 1), kBold, 10.5f, isSel ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
            int nbtn = 0; for (int t = 0; t < 3; ++t) nbtn += (stripBtn[b][t] && stripBtn[b][t]->visible) ? 1 : 0;
            const float right = x1 - 7.0f - (nbtn > 0 ? nbtn * 19.0f + (nbtn - 1) * 4.0f + 6.0f : 0.0f);
            const std::string name = bandName(b, nb), val = stripValue(b);
            const float avail = right - (key.r() + 6.0f);
            const float nw = textWidth(name, kLabel, 10.5f, 1.0f), vw = textWidth(val, kMono, 10.5f);
            if (avail > nw + vw + 10.0f)
            {
                textAt(key.r() + 6.0f, key.cy() + 0.5f, name, kLabel, 10.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
                textAt(right, key.cy() + 0.5f, val, kMono, 10.5f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
            }
            else if (avail > vw + 2.0f) textAt(right, key.cy() + 0.5f, val, kMono, 10.5f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        // listening banner
        if (ls >= 0 && ls < nb)
        {
            float x0, x1; regionX(ls, x0, x1);
            const std::string s = "SOLO BAND " + std::to_string(ls + 1) + (dmode == dmListen ? "  (ALT)" : "");
            const float tw = textWidth(s, kLabel, 11.5f, 1.0f) + 16.0f;
            const Rect lb(std::max(disp.x + 6, std::min(disp.r() - tw - 6, 0.5f * (x0 + x1) - tw * 0.5f)), disp.y + kStripH + 8.0f, tw, 21.0f);
            fillRect(lb, theme.second);
            textAt(lb.cx(), lb.cy() + 0.5f, s, kLabel, 11.5f, theme.dark ? theme.on : theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
    }

    // ---- mouse ---------------------------------------------------------------------------------------------------
    enum DragMode { dmNone = 0, dmXover, dmListen, dmMainDrag, dmSecondDrag, dmGrab };

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!disp.contains(x, y)) return false;
        const bool alt = (mod & kModifierAlt) != 0, cmd = (mod & (kModifierControl | kModifierSuper)) != 0;
        const int b = regionAt(x);
        if (button == kMouseButtonRight) { select(b); bandMenu(b, x, y); return false; }
        if (button != kMouseButtonLeft) return false;
        if (alt)
        {
            dmode = dmListen; listenBand = b;
            select(b);
            setQuiet(pListen(), (float)(b + 1));
            setStatus("Listening to band " + std::to_string(b + 1) + " (" + bandRange(b) + ") while held");
            return true;
        }
        const int xo = xoverAt(x, y);
        if (xo >= 0)
        {
            if (dbl) { openXoverEntry(xo); return false; }
            double e[3]; effXo(e);
            dmode = dmXover; dragXo = xo; startV = (float)e[xo]; lastX = x; eff = 0.0f;
            beginEdit(pXover(xo));
            return true;
        }
        if (y < disp.y + kStripH) { select(b); return false; }
        select(b);
        if (dbl && !cmd)
        {
            const Rect r(std::max(disp.x + 4, std::min(disp.r() - 104, x - 50)), std::max(disp.y + kStripH + 4, y - 30), 100, 22);
            openTextEntry(pMain(b), r, { { pMain(b), r }, { pSecond(b), r } });
            setStatus("Type the band's " + mainName() + "; Tab: " + secondName() + "; Enter: done");
            return false;
        }
        dragBand = b; lastY = y; eff = 0.0f;
        if (cmd) { dmode = dmSecondDrag; startV = value(pSecond(b)); beginEdit(pSecond(b)); return true; }
        if (regionGrab(b, x, y, mod)) { dmode = dmGrab; return true; }
        dmode = dmMainDrag; startV = value(pMain(b)); beginEdit(pMain(b));
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        const float fine = (mod & kModifierShift) ? 0.15f : 1.0f;
        switch (dmode)
        {
        case dmListen: {
            const int b = regionAt(std::max(disp.x, std::min(disp.r() - 1, x)));
            if (b != listenBand) { listenBand = b; setQuiet(pListen(), (float)(b + 1)); select(b); setStatus("Listening to band " + std::to_string(b + 1) + " (" + bandRange(b) + ")"); }
            return;
        }
        case dmXover: {
            eff += (x - lastX) * fine; lastX = x;
            double e[3]; effXo(e);
            const int nb = numBands();
            double f = startV * std::pow(kHi / kLo, (double)(eff / disp.w));
            const double lo = dragXo > 0 ? e[dragXo - 1] * pump::mb::kMinRatio : pump::mb::kMinXover;
            const double hi = dragXo < nb - 2 ? e[dragXo + 1] / pump::mb::kMinRatio : std::min(pump::mb::kMaxXover, 0.45 * rate());
            f = pump::clampv(f, lo, hi);
            setParam(pXover(dragXo), (float)f);
            setStatus("Crossover " + std::to_string(dragXo + 1) + "  " + spec(pXover(dragXo)).format((float)f));
            return;
        }
        case dmMainDrag:
            eff += (lastY - y) * fine; lastY = y;
            setParam(pMain(dragBand), mainFromDrag(dragBand, startV, eff));
            setStatus(paramStatus(pMain(dragBand)));
            return;
        case dmSecondDrag:
            eff += (lastY - y) * fine; lastY = y;
            setParam(pSecond(dragBand), secondFromDrag(dragBand, startV, eff));
            setStatus(paramStatus(pSecond(dragBand)));
            return;
        case dmGrab: regionGrabDrag(x, y, mod); return;
        default: return;
        }
    }
    void bodyMouseUp(float, float) override
    {
        switch (dmode)
        {
        case dmListen: stopListening(); break;
        case dmXover: endEdit(pXover(dragXo)); break;
        case dmMainDrag: endEdit(pMain(dragBand)); break;
        case dmSecondDrag: endEdit(pSecond(dragBand)); break;
        case dmGrab: regionGrabEnd(); break;
        default: break;
        }
        dmode = dmNone;
    }
    bool keyRelease(uint key, uint) override
    {
        if ((key == kKeyAltL || key == kKeyAltR) && dmode == dmListen) { stopListening(); listenBand = -1; dmode = dmNone; fBodyActive = false; repaint(); return true; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        if (!disp.contains(x, y)) { hoverXo = -1; hoverBand = -1; return; }
        hoverXo = xoverAt(x, y);
        hoverBand = regionAt(x);
        if (hoverXo >= 0)
            setStatus(std::string(spec(pXover(hoverXo)).name) + "  " + spec(pXover(hoverXo)).format(value(pXover(hoverXo))) + "    drag (Shift: fine) · double-click: type · wheel: 1/12 octave");
        else if (y >= disp.y + kStripH)
            setStatus("Band " + std::to_string(hoverBand + 1) + " (" + bandRange(hoverBand) + ")    drag/wheel: " + mainName() + " · Ctrl/Cmd+drag: " + secondName() + " · Alt-hold: solo · right-click: menu");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (!disp.contains(x, y)) return false;
        const float dir = dy > 0 ? 1.0f : -1.0f;
        const bool fine = (mod & kModifierShift) != 0;
        const int xo = xoverAt(x, y);
        if (xo >= 0)
        {
            double e[3]; effXo(e);
            const int nb = numBands();
            const double lo = xo > 0 ? e[xo - 1] * pump::mb::kMinRatio : pump::mb::kMinXover;
            const double hi = xo < nb - 2 ? e[xo + 1] / pump::mb::kMinRatio : std::min(pump::mb::kMaxXover, 0.45 * rate());
            const double f = pump::clampv(e[xo] * std::pow(2.0, dir * (fine ? 1.0 / 48.0 : 1.0 / 12.0)), lo, hi);
            setParamGesture(pXover(xo), (float)f);
            setStatus("Crossover " + std::to_string(xo + 1) + "  " + spec(pXover(xo)).format((float)f));
            return true;
        }
        if (y < disp.y + kStripH) return false;
        const int b = regionAt(x);
        select(b);
        setParamGesture(pMain(b), mainWheel(b, value(pMain(b)), dir, fine));
        setStatus(paramStatus(pMain(b)));
        return true;
    }

    void openXoverEntry(int k)
    {
        std::vector<std::pair<int, Rect>> ring;
        for (int j = 0; j < numBands() - 1; ++j) ring.push_back({ pXover(j), xoverTab(j) });
        openTextEntry(pXover(k), xoverTab(k), ring);
        setStatus("Type a frequency (120, 2k, A4); Tab: next crossover; Enter: done");
    }

    // ---- keys and menus ------------------------------------------------------------------------------------------
    bool keyPress(uint key, uint mod) override
    {
        const bool cmd = (mod & (kModifierControl | kModifierSuper)) != 0;
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        const int nb = numBands();
        if (!cmd && key >= '1' && key <= '4') { const int b = (int)(key - '1'); if (b < nb) { select(b); setStatus("Band " + std::to_string(b + 1) + " (" + bandRange(b) + ")"); } return true; }
        if (key == kKeyLeft || key == kKeyRight) { select((sel + (key == kKeyLeft ? nb - 1 : 1)) % nb); return true; }
        if (!cmd && (k == 's' || k == 'm' || k == 'b'))
        {
            const int t = k == 's' ? 0 : (k == 'm' ? 1 : 2);
            const int p = flagParam(sel, t);
            if (p >= 0) setParamGesture(p, value(p) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (key == kKeyEscape)
        {
            beginGroup();
            for (int b = 0; b < kBands; ++b) if (pSolo(b) >= 0) setParamGesture(pSolo(b), 0.0f);
            endGroup();
            stopListening();
            setStatus("Solo cleared");
            return true;
        }
        return false;
    }

    static std::vector<float>& clipboard() { static std::vector<float> c; return c; }
    bool isFlag(int p) const
    {
        for (int b = 0; b < kBands; ++b) for (int t = 0; t < 3; ++t) if (flagParam(b, t) == p) return true;
        return false;
    }
    void bandMenu(int b, float x, float y)
    {
        std::vector<std::string> it = {
            std::string(bandFlag(b, 0) ? "Unsolo" : "Solo") + " band " + std::to_string(b + 1),
            "Solo only this band",
            pMute(b) >= 0 ? std::string(bandFlag(b, 1) ? "Unmute" : "Mute") + " band " + std::to_string(b + 1) : "-",
            pBandBypass(b) >= 0 ? std::string(bandFlag(b, 2) ? "Enable" : "Bypass") + " band " + std::to_string(b + 1) : "-",
            "-", "Copy band settings", clipboard().empty() ? "Paste (nothing copied)" : "Paste band settings", "Reset band",
        };
        openMenu(Rect(x, y, 10, 10), it, -1, [this, b](int i) {
            const int first = bandFirstParam(b), cnt = bandParamCount();
            switch (i)
            {
            case 0: setParamGesture(pSolo(b), bandFlag(b, 0) ? 0.0f : 1.0f); break;
            case 1: exclusive(b, 0); break;
            case 2: if (pMute(b) >= 0) setParamGesture(pMute(b), bandFlag(b, 1) ? 0.0f : 1.0f); break;
            case 3: if (pBandBypass(b) >= 0) setParamGesture(pBandBypass(b), bandFlag(b, 2) ? 0.0f : 1.0f); break;
            case 5:
                clipboard().clear();
                for (int j = 0; j < cnt; ++j) clipboard().push_back(value(first + j));
                setStatus("Copied band " + std::to_string(b + 1));
                break;
            case 6:
                if ((int)clipboard().size() != cnt) break;
                beginGroup();
                for (int j = 0; j < cnt; ++j) if (!isFlag(first + j)) setParamGesture(first + j, clipboard()[(size_t)j]);
                endGroup();
                setStatus("Pasted into band " + std::to_string(b + 1));
                break;
            case 7:
                beginGroup();
                for (int j = 0; j < cnt; ++j) setParamGesture(first + j, spec(first + j).def);
                endGroup();
                setStatus("Band " + std::to_string(b + 1) + " reset");
                break;
            default: break;
            }
            onSelect(sel);
        });
    }

    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        std::vector<std::pair<std::string, std::string>> v = {
            { "Alt + hold a band", "solo it while held (slide across to switch bands)" },
            { "Click a band · 1-4 · Left/Right", "select it" },
            { "Drag / wheel in a band", mainName() + " (Shift: fine)" },
            { "Ctrl/Cmd + drag up/down in a band", secondName() },
            { "Double-click a band", "type its " + mainName() + " (Tab: " + secondName() + ")" },
            { "Drag a crossover", "move it (Shift: fine) · wheel: 1/12 octave" },
            { "Double-click a crossover", "type it: 120, 2k, A4 (Tab: next crossover)" },
            { "S · M · B keys in the display", "solo · mute · bypass (click: on/off · hold: while held)" },
            { "Ctrl/Cmd + click S / M / B", "only this band (again: clear them all)" },
            { "S / M / B on the keyboard · Esc", "toggle them on the selected band · clear every solo" },
            { "Right-click a band", "solo, mute, bypass, copy / paste settings, reset" },
        };
        for (const auto& l : pluginHelp()) v.push_back(l);
        return v;
    }

    // multiband state for subclasses
    DragMode dmode = dmNone;
    int dragXo = -1, dragBand = -1, hoverXo = -1, hoverBand = -1, listenBand = -1;
    float startV = 0.0f, lastX = 0.0f, lastY = 0.0f, eff = 0.0f;
    Button* stripBtn[kBands][3] = {};
};

END_NAMESPACE_DISTRHO
