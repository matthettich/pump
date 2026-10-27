// pump METER: interface. Left: the loudness screen (integrated, short-term, momentary, maxima, LRA, true peak,
// PLR, PSR). Middle: the loudness history, the spectrum and the stereo (correlation, balance) meters, plus the
// target / history / scale settings. Right: L/R peak + RMS meters.
#include "ui/PumpUI.hpp"
#include "PluginMeter.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::meter;

// A small value field (drag vertically, wheel, right-click to type, double-click to reset).
struct ValueField : Control {
    float dragStart = 0, dragY = 0;
    std::string unit;
    void draw(PumpUI& ui) override
    {
        const pump::Theme& t = ui.theme;
        ui.keyBox(r, false, false, nullptr, 3.0f);
        char b[32]; std::snprintf(b, sizeof b, "%.1f", ui.value(param));
        ui.textAt(r.x + 8.0f, r.cy() + 0.5f, label, PumpUI::kLabel, 10.0f, enabled ? t.ink2 : t.dim, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 0.8f);
        ui.textAt(r.r() - 8.0f, r.cy() + 0.5f, std::string(b) + unit, PumpUI::kMono, 11.5f, enabled ? t.ink : t.dim, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
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
        ui.setParam(param, std::round((dragStart + (dragY - y) * ((mod & kModifierShift) ? 0.01f : 0.1f)) * 10.0f) / 10.0f);
        if (mod & kModifierShift) { dragStart = ui.value(param); dragY = y; }
    }
    void mouseUp(PumpUI& ui, float, float) override { ui.endEdit(param); }
    bool scroll(PumpUI& ui, float dy, uint mod) override
    {
        ui.setParamGesture(param, ui.value(param) + (dy > 0 ? 1.0f : -1.0f) * ((mod & kModifierShift) ? 0.1f : 0.5f));
        return true;
    }
};

class UIMeter : public PumpUI {
public:
    UIMeter() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::meter(), "METER", 1000, 620, -1)
    {
        resetBtn = addButton("RESET", [this]() { resetAll(); });
        resetBtn->small = true;
        resetBtn->tip = "Reset every measurement: loudness, true peak, history, holds (R)";

        targetMenu = addMenu(kTarget, "");
        targetMenu->tip = "Loudness target: colours the readouts and draws the target band";
        customLufs = add(new ValueField());
        customLufs->param = kCustomLufs; customLufs->label = "LUFS";
        customLufs->tip = "Custom target loudness (drag, wheel; right-click to type, double-click to reset)";
        customTP = add(new ValueField());
        customTP->param = kCustomTP; customTP->label = "TP";
        customTP->tip = "Custom true-peak limit in dBTP (drag, wheel; right-click to type, double-click to reset)";
        customLufs->tabGroup = customTP->tabGroup = 1;
        histSeg = addSegmented(kHistory, { "1M", "3M", "10M", "30M" });
        histSeg->tip = "History length: 1, 3, 10 or 30 minutes";
        scaleMenu = addMenu(kScale, "SCALE");
        scaleMenu->tip = "Meter scale: EBU +9 / +18 (relative to the target) or absolute LUFS";

        plug = static_cast<PluginMeter*>(pluginPtr());
        setupAnalyzer(plug && plug->getSampleRate() > 0 ? plug->getSampleRate() : (sampleRate() > 0 ? sampleRate() : 48000.0));
        rebind();
    }

protected:
    // ---------------------------------------------------------------- layout
    void layout() override
    {
        resetBtn->r = Rect(headerExtra.r() - 76.0f, headerExtra.y, 76.0f, headerExtra.h);
        loud = Rect(14, 58, 300, 526);
        hist = Rect(326, 58, 548, 306);
        specR = Rect(326, 378, 316, 124);
        stereoR = Rect(326, 516, 316, 68);
        panel = Rect(654, 378, 220, 206);
        meters = Rect(886, 58, 100, 526);
        const float px = panel.x + 12.0f, pw = panel.w - 24.0f;
        targetMenu->r = Rect(px, panel.y + 26.0f, pw, 26.0f);
        customLufs->r = Rect(px, panel.y + 62.0f, (pw - 8.0f) * 0.5f, 24.0f);
        customTP->r = Rect(px + (pw + 8.0f) * 0.5f, panel.y + 62.0f, (pw - 8.0f) * 0.5f, 24.0f);
        histSeg->r = Rect(px, panel.y + 122.0f, pw, 24.0f);
        scaleMenu->r = Rect(px, panel.y + 166.0f, pw, 26.0f);
        // meter bars
        const float mt = meters.y + 52.0f, mb = meters.b() - 56.0f;
        barL = Rect(meters.x + 14.0f, mt, 22.0f, mb - mt);
        barR = Rect(meters.x + 40.0f, mt, 22.0f, mb - mt);
    }
    void rebind()
    {
        const bool custom = (int)value(kTarget) == kCustom;
        customLufs->visible = customTP->visible = custom;
    }
    void onParamChanged(int, float) override { rebind(); }
    void onPresetApplied() override { rebind(); }
    // the spectrum's FFT size and bin frequencies follow the sample rate (ring holds 1 << 15)
    void setupAnalyzer(double sr)
    {
        if (sr <= 0.0 || sr == anaRate) return;
        anaRate = sr;
        spec.setup(sr);
        spec.setRange(20.0, 24000.0);
    }
    void onSampleRateChanged(double sr) override { setupAnalyzer(sr); }

    // ---------------------------------------------------------------- helpers
    float tgt() const { return targetLufs((int)value(kTarget), value(kCustomLufs)); }
    float tpLimit() const { return targetTP((int)value(kTarget), value(kCustomTP)); }
    float tol() const { return targetTol((int)value(kTarget)); }
    static bool ok(float v) { return std::isfinite(v) && v > -70.0f; }
    static std::string fmt(float v, const char* f = "%.1f")
    {
        if (!std::isfinite(v)) return v > 0 ? "+inf" : "-inf";
        char b[24]; std::snprintf(b, sizeof b, f, v); return b;
    }
    static std::string fmtTime(double secs)
    {
        const long s = (long)std::floor(std::max(0.0, secs));
        char b[24];
        if (s >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
        else std::snprintf(b, sizeof b, "%ld:%02ld", s / 60, s % 60);
        return b;
    }
    // the colour a loudness value gets relative to the target
    Color loudColor(float v) const
    {
        if (!ok(v)) return theme.ink2;
        const float d = v - tgt();
        if (d > tol()) return theme.red;
        if (d < -tol()) return theme.second;
        return theme.ink;
    }

    void resetAll()
    {
        if (!plug) return;
        // the next value must be above anything the DSP has seen (undo / A/B can bring lower values back)
        const float v = std::max(value(kReset), plug->engine.resetHighWater()) + 1.0f;
        openGesture(kReset);   // a plain gesture, no undo step: a reset isn't a setting
        setParam(kReset, v);
        closeGesture(kReset);
        clearHolds();
        setStatus("All measurements reset");
    }
    void clearHolds()
    {
        for (int c = 0; c < 2; ++c) { hold[c] = -120.0f; holdT[c] = 0.0f; tpHold[c] = -INFINITY; }
        corrMin = 1.0f;
    }

    // ---------------------------------------------------------------- per frame
    void tick(float dt) override
    {
        if (!plug) return;
        setupAnalyzer(plug->getSampleRate());   // also for hosts that don't tell the editor about a new rate
        Engine& e = plug->engine;
        const uint32_t rc = e.resetCount.load();
        if (rc != lastReset) { lastReset = rc; clearHolds(); }
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(e.peak[c].exchange(0.0f));
            const float tp = pump::gainToDbF(e.tpPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 20.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 2.0f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 12.0f * dt);
            if (tp > -119.0f) tpHold[c] = std::isfinite(tpHold[c]) ? std::max(tpHold[c], tp) : tp;
            const float rms = e.out.rmsDb[c].load();
            rmsS[c] = rms > rmsS[c] ? rms : std::max(rms, rmsS[c] - 15.0f * dt);
        }
        const float corr = e.out.correlation.load();
        corrS += (corr - corrS) * std::min(1.0f, dt * 8.0f);
        if (!e.out.silent.load()) corrMin = std::min(corrMin, corr);
        balS += (e.out.balance.load() - balS) * std::min(1.0f, dt * 8.0f);
        spec.update(&e.spectrum, nullptr, dt);
    }

    // ---------------------------------------------------------------- the loudness screen
    void drawLoudness()
    {
        screenBox(loud);
        if (!plug) return;
        const Readout& o = plug->engine.out;
        const float I = o.integrated.load(), S = o.shortTerm.load(), Mo = o.momentary.load();
        const float x = loud.x + 16.0f, rx = loud.r() - 16.0f;
        float y = loud.y + 22.0f;
        caption(x, y, "INTEGRATED");
        textAt(rx, y, fmtTime(o.elapsed.load()), kMono, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        // the big number, coloured relative to the target
        y += 42.0f;
        const std::string iv = ok(I) ? fmt(I) : "--.-";
        textAt(x - 2.0f, y, iv, kBold, 54.0f, loudColor(I), ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(x + textWidth(iv, kBold, 54.0f) + 6.0f, y + 13.0f, "LUFS", kLabel, 12.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        // delta to the target
        y += 44.0f;
        {
            std::string s; Color f = theme.panel2, tc = theme.ink;
            if (!ok(I)) s = "WAITING FOR AUDIO";
            else
            {
                const float d = I - tgt();
                char b[48];
                if (std::fabs(d) <= tol()) { std::snprintf(b, sizeof b, "ON TARGET  %+.1f LU", d); f = theme.neon; tc = theme.neonInk; }
                else if (d > 0) { std::snprintf(b, sizeof b, "TOO LOUD  %+.1f LU", d); f = theme.red; tc = theme.bg; }
                else { std::snprintf(b, sizeof b, "BELOW TARGET  %+.1f LU", d); f = theme.panel2; tc = theme.ink; }
                s = b;
            }
            const float tw = textWidth(s, kLabel, 11.5f, 1.0f) + 18.0f;
            fillRect(Rect(x, y - 11.0f, tw, 22.0f), f);
            textAt(x + 9.0f, y + 0.5f, s, kLabel, 11.5f, tc, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        }
        // loudness bar: short-term fill, momentary tick, integrated marker, target band
        y += 26.0f;
        {
            float lo, hi; scaleRange((int)value(kScale), tgt(), lo, hi);
            const Rect bar(x, y, rx - x, 14.0f);
            auto bx = [&](float v) { return bar.x + (std::max(lo, std::min(hi, v)) - lo) / (hi - lo) * bar.w; };
            fillRect(bar, theme.panel2);
            if (ok(S)) fillRect(Rect(bar.x, bar.y, bx(S) - bar.x, bar.h), theme.neon);
            fillRect(Rect(bx(tgt() - tol()), bar.y - 3.0f, bx(tgt() + tol()) - bx(tgt() - tol()), bar.h + 6.0f), withAlpha2(theme.second, 0.28f));
            line(bx(tgt()), bar.y - 4.0f, bx(tgt()), bar.b() + 4.0f, theme.second, 2.0f);
            if (ok(Mo)) line(bx(Mo), bar.y + 2.0f, bx(Mo), bar.b() - 2.0f, withAlpha2(theme.ink, 0.5f), 1.5f);
            if (ok(I)) line(bx(I), bar.y - 2.0f, bx(I), bar.b() + 2.0f, theme.ink, 3.0f);
            strokeRect(bar, theme.ink, 1.0f);
            const int stepv = (hi - lo) > 40.0f ? 10 : 6;
            for (int v = (int)std::ceil(lo / stepv) * stepv; v <= (int)hi; v += stepv)
                textAt(bx((float)v), bar.b() + 10.0f, std::to_string(v), kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // short-term and momentary
        y += 52.0f;
        const float cw = (rx - x) * 0.5f;
        caption(x, y, "SHORT-TERM");
        caption(x + cw, y, "MOMENTARY");
        y += 26.0f;
        auto mid = [&](float cx, float v) {
            const std::string s = ok(v) ? fmt(v) : "--.-";
            textAt(cx, y, s, kBold, 28.0f, loudColor(v), ALIGN_LEFT | ALIGN_MIDDLE);
            textAt(cx + textWidth(s, kBold, 28.0f) + 4.0f, y + 7.0f, "LUFS", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        };
        mid(x, S); mid(x + cw, Mo);
        bigArea = Rect(loud.x, loud.y + 4.0f, loud.w, y + 20.0f - loud.y);
        y += 28.0f;
        line(x, y, rx, y, theme.dim, 1.0f);
        // the grid of statistics
        const float TP = o.truePeak.load(), lra = o.range.load(), plr = o.plr.load(), psr = o.psr.load();
        const float mM = o.maxMomentary.load(), mS = o.maxShortTerm.load(), stp = o.shortTP.load();
        auto cell = [&](int col, int row, const char* cap, const std::string& v, const char* unit, const Color& c) {
            const float cx = x + col * cw, cy = y + 22.0f + row * 47.0f;
            caption(cx, cy, cap);
            textAt(cx, cy + 22.0f, v, kMono, 19.0f, c, ALIGN_LEFT | ALIGN_MIDDLE);
            textAt(cx + textWidth(v, kMono, 19.0f) + 5.0f, cy + 25.0f, unit, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        };
        cell(0, 0, "MAX MOMENTARY", ok(mM) ? fmt(mM) : "--.-", "LUFS", loudColor(mM));
        cell(1, 0, "MAX SHORT-TERM", ok(mS) ? fmt(mS) : "--.-", "LUFS", loudColor(mS));
        cell(0, 1, "LOUDNESS RANGE", ok(I) && std::isfinite(lra) ? fmt(lra) : "--.-", "LU", theme.ink);
        cell(1, 1, "TRUE PEAK MAX", std::isfinite(TP) ? fmt(TP) : "-inf", "dBTP", std::isfinite(TP) && TP > tpLimit() ? theme.red : theme.ink);
        cell(0, 2, "PLR", std::isfinite(plr) && ok(I) ? fmt(plr) : "--.-", "LU", theme.ink);
        cell(1, 2, "PSR", std::isfinite(psr) ? fmt(psr) : "--.-", "LU", theme.ink);
        // what it would take: gain to reach the target, and the true-peak headroom left at the limit
        const float g2t = ok(I) ? tgt() - I : NAN, hr = std::isfinite(TP) ? tpLimit() - TP : NAN;
        cell(0, 3, "GAIN TO TARGET", std::isfinite(g2t) ? fmt(g2t, "%+.1f") : "--.-", "dB", theme.ink);
        cell(1, 3, "TP HEADROOM", std::isfinite(hr) ? fmt(hr, "%+.1f") : "--.-", "dB", std::isfinite(hr) && hr < 0.0f ? theme.red : theme.ink);
        statArea = Rect(loud.x, y + 4.0f, loud.w, 196.0f);
        // short-term true peak (the PSR's peak), small
        const float fy = loud.b() - 22.0f;
        line(x, fy - 18.0f, rx, fy - 18.0f, theme.dim, 1.0f);
        textAt(x, fy, "SHORT-TERM TRUE PEAK", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(rx, fy, (std::isfinite(stp) ? fmt(stp) : std::string("-inf")) + " dBTP", kMono, 11.5f, std::isfinite(stp) && stp > tpLimit() ? theme.red : theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ---------------------------------------------------------------- history
    void drawHistory()
    {
        screenBox(hist);
        caption(hist.x + 12.0f, hist.y + 16.0f, "LOUDNESS HISTORY");
        float lo, hi; scaleRange((int)value(kScale), tgt(), lo, hi);
        const Rect g(hist.x + 40.0f, hist.y + 42.0f, hist.w - 80.0f, hist.h - 66.0f);
        auto yOf = [&](float v) { return g.y + (hi - std::max(lo, std::min(hi, v))) / (hi - lo) * g.h; };
        // legend (top right)
        {
            float lx = hist.r() - 12.0f;
            auto item = [&](const char* t, const Color& c, bool band) {
                const float tw = textWidth(t, kLabel, 9.5f, 0.8f);
                textAt(lx, hist.y + 16.0f, t, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
                lx -= tw + 18.0f;
                if (band) fillRect(Rect(lx, hist.y + 12.0f, 12.0f, 8.0f), c); else line(lx, hist.y + 16.0f, lx + 12.0f, hist.y + 16.0f, c, 2.0f);
                lx -= 14.0f;
            };
            item("TARGET", withAlpha2(theme.second, 0.5f), true);
            item("INTEGRATED", theme.second, false);
            item("SHORT-TERM", theme.ink, false);
            item("MOMENTARY", withAlpha2(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.25f), 0.7f), true);
        }
        // grid: LUFS lines every 3 or 6 LU, labelled left (LUFS) and right (LU relative to the target)
        const float step = (hi - lo) > 40.0f ? 6.0f : 3.0f;
        const bool rel = (int)value(kScale) <= 1;
        for (float v = std::ceil(lo / step) * step; v <= hi + 0.01f; v += step)
        {
            const float yy = yOf(v);
            line(g.x, yy, g.r(), yy, theme.grid, 1.0f);
            textAt(g.x - 6.0f, yy, std::to_string((int)v), kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        if (rel)
            for (float d = -36.0f; d <= 18.0f; d += 9.0f)
            {
                const float v = tgt() + d;
                if (v < lo - 0.01f || v > hi + 0.01f) continue;
                char b[12]; std::snprintf(b, sizeof b, d == 0 ? "0" : "%+.0f", d);
                textAt(g.r() + 6.0f, yOf(v), b, kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            }
        textAt(g.x - 6.0f, g.y - 13.0f, "LUFS", kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        if (rel) textAt(g.r() + 6.0f, g.y - 13.0f, "LU", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        // target band
        fillRect(Rect(g.x, yOf(tgt() + tol()), g.w, yOf(tgt() - tol()) - yOf(tgt() + tol())), withAlpha2(theme.second, theme.dark ? 0.16f : 0.12f));
        for (float xx = g.x; xx < g.r(); xx += 9.0f) line(xx, yOf(tgt()), std::min(g.r(), xx + 5.0f), yOf(tgt()), withAlpha2(theme.second, 0.9f), 1.2f);
        // time axis
        const int N = (int)(kHistoryMinutes[pump::clampv((int)value(kHistory), 0, 3)] * 60.0f * Engine::kHistRate);
        // the newest N points since the reset (the integrated ring is written last, so all three hold them; the
        // reset position is read first, so a reset in between can't put it after the end)
        pump::meter::HistoryWindow hw;
        if (plug)
        {
            const uint32_t start = plug->engine.histStart.load();
            const uint32_t end = plug->engine.histI.smoothPosition();
            hw = pump::meter::historyWindow(end, start, plug->engine.histI.capacity(), N);
        }
        const int have = hw.count;                 // points shown, from ring index hw.base
        const uint32_t first = hw.first;           // index (since reset) of the leftmost point shown
        const float secW = g.w / (N / (float)Engine::kHistRate);
        {
            const float span = N / (float)Engine::kHistRate;
            const float tstep = span <= 60 ? 10.0f : span <= 180 ? 30.0f : span <= 600 ? 60.0f : 300.0f;
            const float t0 = first / (float)Engine::kHistRate;
            for (float t = std::ceil(t0 / tstep) * tstep; t <= t0 + span + 0.01f; t += tstep)
            {
                const float xx = g.x + (t - t0) * secW;
                line(xx, g.y, xx, g.b(), theme.grid, 1.0f);
                textAt(xx, g.b() + 13.0f, fmtTime(t), kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
        strokeRect(g, theme.dim, 1.0f);
        if (!plug || have <= 0) { textAt(g.cx(), g.cy(), "WAITING FOR AUDIO", kLabel, 13.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 2.0f); return; }
        // per-pixel reduction: momentary min/max band, short-term and integrated at the pixel's last point
        save();
        scissor(g.x, g.y, g.w, g.h);
        const pump::meter::Engine& e = plug->engine;
        const int cols = (int)g.w;
        const float ptsPerCol = N / (float)cols;
        auto pointAt = [&](const pump::ScopeRing& r, int i) { return r.at(hw.base + (uint32_t)i); };
        // momentary band
        beginPath();
        bool open = false;
        std::vector<float>& top = colTop; std::vector<float>& bot = colBot; std::vector<float>& sv = colS; std::vector<float>& iv = colI;
        top.assign((size_t)cols, NAN); bot.assign((size_t)cols, NAN); sv.assign((size_t)cols, NAN); iv.assign((size_t)cols, NAN);
        for (int c = 0; c < cols; ++c)
        {
            const int a = (int)std::floor(c * ptsPerCol), b = std::max((int)std::floor((c + 1) * ptsPerCol), a + 1);
            if (a >= have) break;
            float mn = 1e9f, mx = -1e9f;
            for (int i = a; i < std::min(b, have); ++i)
            {
                const float m = pointAt(e.histM, i);
                if (std::isfinite(m) && m > -90.0f) { mn = std::min(mn, m); mx = std::max(mx, m); }
            }
            if (mx > -1e8f) { top[(size_t)c] = yOf(mx); bot[(size_t)c] = yOf(mn); }
            const int li = std::min(b, have) - 1;
            sv[(size_t)c] = pointAt(e.histS, li);
            iv[(size_t)c] = pointAt(e.histI, li);
        }
        (void)open;
        const Color mc = withAlpha2(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.25f), theme.dark ? 0.45f : 0.55f);
        for (int c = 0; c < cols; ++c)
            if (std::isfinite(top[(size_t)c])) fillRect(Rect(g.x + c, top[(size_t)c], 1.0f, std::max(1.0f, bot[(size_t)c] - top[(size_t)c])), mc);
        auto polyline = [&](const std::vector<float>& v, const Color& col, float w) {
            beginPath();
            bool pen = false;
            for (int c = 0; c < cols; ++c)
            {
                const float val = v[(size_t)c];
                if (!std::isfinite(val) || val < -90.0f) { pen = false; continue; }
                const float yy = yOf(val);
                if (!pen) { moveTo(g.x + c, yy); pen = true; } else lineTo(g.x + c + 0.5f, yy);
            }
            strokeColor(col); strokeWidth(w); lineJoin(ROUND); stroke();
        };
        polyline(iv, theme.second, 1.6f);
        polyline(sv, theme.ink, 1.8f);
        restore();
    }

    // ---------------------------------------------------------------- spectrum and stereo
    void drawSpectrumPanel()
    {
        screenBox(specR);
        const Rect g(specR.x + 1.0f, specR.y + 1.0f, specR.w - 2.0f, specR.h - 2.0f);
        save();
        scissor(g.x, g.y, g.w, g.h);
        for (int d = -12; d >= -84; d -= 12)
        {
            const float yy = g.y + (6.0f - d) / 96.0f * g.h;
            line(g.x, yy, g.r(), yy, theme.grid, 1.0f);
        }
        drawFreqGrid(g, 20.0, 24000.0, true);
        drawSpectrum(spec, g, 6.0f, -90.0f, withAlpha2(theme.neon, theme.dark ? 0.35f : 0.55f), pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.1f : 0.55f), 20.0, 24000.0, 1.2f);
        restore();
        caption(specR.x + 10.0f, specR.y + 14.0f, "SPECTRUM");
    }

    void drawStereo()
    {
        screenBox(stereoR);
        const float x = stereoR.x + 96.0f, w = stereoR.w - 112.0f;
        auto bar = [&](float y, const char* cap, float v, float vmin, float lo, float hi, const char* l0, const char* lm, const char* l1, bool neg) {
            caption(stereoR.x + 10.0f, y, cap);
            const Rect b(x, y - 6.0f, w, 12.0f);
            fillRect(b, theme.panel2);
            const float cx = b.x + (0.0f - lo) / (hi - lo) * b.w;
            const float vx = b.x + (std::max(lo, std::min(hi, v)) - lo) / (hi - lo) * b.w;
            fillRect(Rect(std::min(cx, vx), b.y, std::fabs(vx - cx), b.h), neg && v < 0.0f ? theme.red : theme.neon);
            if (vmin < hi) { const float mx = b.x + (std::max(lo, std::min(hi, vmin)) - lo) / (hi - lo) * b.w; line(mx, b.y - 2.0f, mx, b.b() + 2.0f, theme.red, 1.5f); }
            line(cx, b.y - 3.0f, cx, b.b() + 3.0f, theme.ink, 1.2f);
            line(vx, b.y - 3.0f, vx, b.b() + 3.0f, theme.ink, 2.0f);
            strokeRect(b, theme.ink, 1.0f);
            textAt(b.x, b.b() + 8.0f, l0, kMono, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            textAt(cx, b.b() + 8.0f, lm, kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            textAt(b.r(), b.b() + 8.0f, l1, kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        };
        // correlation: -1 .. +1 with the lowest value since the reset; balance: L .. R (dB, R louder to the right)
        bar(stereoR.y + 18.0f, "CORRELATION", corrS, corrMin, -1.0f, 1.0f, "-1", "0", "+1", true);
        bar(stereoR.y + 46.0f, "BALANCE", -balS, 99.0f, -6.0f, 6.0f, "L 6", "C", "6 R", false);
        char b[16]; std::snprintf(b, sizeof b, "%+.2f", corrS);
        corrArea = stereoR;
    }

    // ---------------------------------------------------------------- level meters
    static float meterT(float db)
    {
        if (db >= 0.0f) return 0.0f;
        if (db >= -12.0f) return 0.4f * (-db / 12.0f);
        if (db >= -30.0f) return 0.4f + 0.3f * ((-db - 12.0f) / 18.0f);
        if (db >= -60.0f) return 0.7f + 0.3f * ((-db - 30.0f) / 30.0f);
        return 1.0f;
    }
    void drawMeters()
    {
        screenBox(meters);
        const float tpl = tpLimit();
        // true-peak holds on top (click to reset)
        for (int c = 0; c < 2; ++c)
        {
            const Rect& b = c ? barR : barL;
            const Rect h(b.cx() - 13.0f, meters.y + 10.0f, 26.0f, 30.0f);
            const bool over = std::isfinite(tpHold[c]) && tpHold[c] > tpl;
            fillRect(h, over ? theme.red : theme.panel2);
            textAt(h.cx(), h.y + 9.0f, c ? "R" : "L", kLabel, 9.0f, over ? theme.bg : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
            textAt(h.cx(), h.y + 21.0f, std::isfinite(tpHold[c]) ? fmt(tpHold[c]) : std::string("-inf"), kMono, 9.5f, over ? theme.bg : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        for (int c = 0; c < 2; ++c)
        {
            const Rect& b = c ? barR : barL;
            fillRect(b, theme.panel2);
            const float tp = meterT(lvl[c]), tr = meterT(rmsS[c]);
            if (tp < 0.999f) fillRect(Rect(b.x, b.y + tp * b.h, b.w, b.h - tp * b.h), theme.neon);
            if (tr < 0.999f) fillRect(Rect(b.x + 5.0f, b.y + tr * b.h, b.w - 10.0f, b.h - tr * b.h), theme.dark ? withAlpha2(theme.bg, 0.6f) : withAlpha2(theme.ink, 0.45f));
            const float th = meterT(hold[c]);
            if (th < 0.999f) line(b.x, b.y + th * b.h, b.r(), b.y + th * b.h, hold[c] > tpl ? theme.red : theme.ink, 1.5f);
            strokeRect(b, theme.ink, 1.0f);
        }
        // the true-peak limit mark
        const float yl = barL.y + meterT(tpl) * barL.h;
        line(barL.x - 3.0f, yl, barR.r() + 3.0f, yl, theme.second, 1.5f);
        // scale
        for (int m : { 0, 3, 6, 9, 12, 18, 24, 30, 40, 50, 60 })
        {
            const float yy = barL.y + meterT((float)-m) * barL.h;
            line(barR.r() + 3.0f, yy, barR.r() + 6.0f, yy, theme.dim, 1.0f);
            textAt(barR.r() + 20.0f, yy, std::to_string(m), kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // RMS readouts at the bottom
        const float by = barL.b() + 16.0f;
        textAt(meters.cx(), by, "RMS", kLabel, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        for (int c = 0; c < 2; ++c)
        {
            const Rect& b = c ? barR : barL;
            textAt(b.cx(), by + 18.0f, rmsS[c] > -99.0f ? fmt(rmsS[c]) : std::string("-inf"), kMono, 9.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawBody() override
    {
        drawLoudness();
        drawHistory();
        drawSpectrumPanel();
        drawStereo();
        drawMeters();
        strokeRect(panel, theme.dim, 1.0f);
        caption(panel.x + 12.0f, panel.y + 13.0f, "TARGET");
        if ((int)value(kTarget) != kCustom)
        {
            // show what the preset target means where the custom fields would be
            char b[64]; std::snprintf(b, sizeof b, "%.0f LUFS  ·  %.1f dBTP  ·  ±%.1f LU", tgt(), tpLimit(), tol());
            textAt(customLufs->r.x, customLufs->r.cy(), b, kMono, 10.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        caption(panel.x + 12.0f, panel.y + 109.0f, "HISTORY");
    }
    void drawOverlay() override {}

    std::string footerInfo() override
    {
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        char b[96]; std::snprintf(b, sizeof b, "BS.1770-4 / EBU R128   %.1f kHz   PASS-THROUGH (BIT-EXACT)", sr / 1000.0);
        return b;
    }

    // ---------------------------------------------------------------- mouse and keys
    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button == kMouseButtonRight && (loud.contains(x, y) || hist.contains(x, y)))
        {
            openMenu(Rect(x, y, 10, 10), { "Reset all measurements", "Reset peak holds", "Reset correlation hold" }, -1, [this](int i) {
                if (i == 0) resetAll();
                else if (i == 1) { for (int c = 0; c < 2; ++c) { hold[c] = -120.0f; tpHold[c] = -INFINITY; } setStatus("Peak holds reset"); }
                else { corrMin = 1.0f; setStatus("Correlation hold reset"); }
            });
            return false;
        }
        if (button != kMouseButtonLeft) return false;
        if (loud.contains(x, y)) { resetAll(); return false; }
        if (meters.contains(x, y)) { for (int c = 0; c < 2; ++c) { hold[c] = -120.0f; tpHold[c] = -INFINITY; } setStatus("Peak and true-peak holds reset"); return false; }
        if (stereoR.contains(x, y)) { corrMin = 1.0f; setStatus("Correlation hold reset"); return false; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        if (loud.contains(x, y)) setStatus("Loudness (EBU R128): click any readout to reset all measurements (R)");
        else if (meters.contains(x, y)) setStatus("Sample peak (bar), RMS (inner bar), true-peak hold (top): click to reset the holds");
        else if (stereoR.contains(x, y)) setStatus("Correlation (red mark: lowest since reset) and balance: click to reset the correlation hold");
        else if (hist.contains(x, y)) setStatus("Short-term (ink), momentary (band) and integrated (line) loudness since the reset; right-click: reset menu");
        else if (specR.contains(x, y)) setStatus("Spectrum (mid, +4.5 dB/oct tilt: pink noise reads flat)");
    }
    bool keyPress(uint key, uint mod) override
    {
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        if (mod & (kModifierControl | kModifierSuper)) return false;
        if (k == 'r') { resetAll(); return true; }
        if (k == 'p') { for (int c = 0; c < 2; ++c) { hold[c] = -120.0f; tpHold[c] = -INFINITY; } corrMin = 1.0f; setStatus("Holds reset"); return true; }
        if (key == kKeyLeft || key == kKeyRight)
        {
            const int t = (int)value(kTarget);
            setParamGesture(kTarget, (float)((t + (key == kKeyRight ? 1 : 7)) % 8));
            setStatus(paramStatus(kTarget));
            return true;
        }
        if (k >= '1' && k <= '4') { setParamGesture(kHistory, (float)(k - '1')); setStatus(paramStatus(kHistory)); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "R  ·  RESET", "reset every measurement (loudness, LRA, true peak, history, holds)" },
            { "Click the loudness readouts", "reset every measurement (same as R)" },
            { "Click the level meters", "reset the peak and true-peak holds (P: all holds)" },
            { "Click the stereo meters", "reset the lowest-correlation hold" },
            { "Right-click readouts / history", "reset menu" },
            { "Left / Right", "previous / next loudness target" },
            { "1  2  3  4", "history length 1 / 3 / 10 / 30 minutes" },
        };
    }

private:
    PluginMeter* plug = nullptr;
    SpectrumView spec;
    Rect loud, hist, specR, stereoR, panel, meters, barL, barR, bigArea, statArea, corrArea;
    float lvl[2] = { -120, -120 }, hold[2] = { -120, -120 }, holdT[2] = {}, tpHold[2] = { -INFINITY, -INFINITY }, rmsS[2] = { -120, -120 };
    float corrS = 0.0f, corrMin = 1.0f, balS = 0.0f;
    uint32_t lastReset = 0;
    double anaRate = 0.0;   // sample rate the spectrum is set up for
    std::vector<float> colTop, colBot, colS, colI;

    Button* resetBtn;
    MenuButton *targetMenu, *scaleMenu;
    ValueField *customLufs, *customTP;
    Segmented* histSeg;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIMeter)
};

UI* createUI() { return new UIMeter(); }

END_NAMESPACE_DISTRHO
