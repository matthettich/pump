// pump UI: the shared plugin window in Thunder Plus's "Simple" style. Flat, square, 1.5 px ink outlines with an
// offset block behind keys and screens; Share Tech Mono labels; one light-neon accent per plugin, light or dark.
//
// A plugin UI derives from PumpUI, adds controls in its constructor (dials, slider rows, toggles, segmented
// buttons, menus), lays them out in layout(), and draws its own screens in drawBody(). The base class handles the
// header (brand, presets, A/B, theme, bypass), typed value entry, the status line, scaling and resizing.
//
// Interaction: drag to change (Shift = fine), double-click or right-click to type a
// value, Ctrl/Cmd/Alt-click to reset to default, mouse wheel steps.
#pragma once

#include "DistrhoUI.hpp"
#include "../PumpParams.hpp"
#include "../dsp/Util.hpp"
#include "../dsp/FFT.hpp"
#include "FontData.hpp"
#include "Theme.hpp"
#include "WinFrameClock.hpp"
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

// What the plugin is ("Compressor") and a one-line description, from the plugin's DistrhoPluginInfo.h: shown next to
// the tag in the header and when hovering it.
#ifndef PUMP_PLUGIN_KIND
#define PUMP_PLUGIN_KIND ""
#endif
#ifndef PUMP_PLUGIN_ABOUT
#define PUMP_PLUGIN_ABOUT ""
#endif
#include <functional>
#include <memory>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace DGL_NAMESPACE;
using DGL_NAMESPACE::Color;
using pump::ParamSpec;
using pump::Preset;
using pump::Theme;
using pump::Accent;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + 0.5f * w; }
    float cy() const { return y + 0.5f * h; }
    Rect inset(float d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }
};

class PumpUI;

// The "command" modifier of the shortcuts: Cmd on macOS (where Ctrl-click means right-click), Ctrl
// elsewhere (the Windows/Super key also counts there).
inline bool cmdKey(uint mod)
{
   #ifdef __APPLE__
    return (mod & kModifierSuper) != 0;
   #else
    return (mod & (kModifierControl | kModifierSuper)) != 0;
   #endif
}

// ---------------------------------------------------------------------------------------------------------------
// Controls

struct Control {
    Rect r;
    int param = -1;
    bool visible = true, enabled = true;
    std::string label;
    std::string tip;           // shown in the status line on hover
    int tabGroup = 0;          // controls sharing a group > 0 are a Tab ring when typing values
    virtual ~Control() {}
    virtual void draw(PumpUI& ui) = 0;
    virtual bool mouseDown(PumpUI&, float, float, int /*button*/, uint /*mod*/) { return false; }
    virtual void mouseDrag(PumpUI&, float, float, uint) {}
    virtual void mouseUp(PumpUI&, float, float) {}
    virtual bool scroll(PumpUI&, float /*dy*/, uint /*mod*/) { return false; }
    virtual bool wantsTextEntry() const { return param >= 0; }
    virtual Rect valueRect() const { return r; }
};

// Round dial with label above and value below (drawn flat).
struct Dial : Control {
    enum Size { Small, Medium, Large } size = Medium;
    bool bipolar = false;     // arc from the centre (gains)
    float dragStartNorm = 0, dragStartY = 0, dragStartX = 0;
    int dragParam = -1;       // the parameter a drag started on (the control may be re-bound mid-drag)
    void draw(PumpUI& ui) override;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override;
    void mouseDrag(PumpUI& ui, float x, float y, uint mod) override;
    void mouseUp(PumpUI& ui, float x, float y) override;
    bool scroll(PumpUI& ui, float dy, uint mod) override;
    float radius() const { return size == Small ? 15.0f : (size == Large ? 27.0f : 20.0f); }
    Rect valueRect() const override { const float cy = r.y + 18.0f + radius(); return Rect(r.x - 8, cy + radius() + 3.0f, r.w + 16, 18); }
    // the height a dial of this size needs
    static float heightFor(Size s) { const float rad = s == Small ? 15.0f : (s == Large ? 27.0f : 20.0f); return 18.0f + 2.0f * rad + 22.0f; }
};

// Simple-skin settings row: label | bar | value.
struct SliderRow : Control {
    float labelW = 92, valueW = 74;
    bool bipolar = false;
    float dragStartNorm = 0, dragStartX = 0;
    int dragParam = -1;
    void draw(PumpUI& ui) override;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override;
    void mouseDrag(PumpUI& ui, float x, float y, uint mod) override;
    void mouseUp(PumpUI& ui, float x, float y) override;
    bool scroll(PumpUI& ui, float dy, uint mod) override;
    Rect valueRect() const override { return Rect(r.r() - valueW, r.y, valueW, r.h); }
};

// Key-style button. With a boolean parameter it toggles it; otherwise calls onClick. isOn() decides the fill.
struct Button : Control {
    std::function<void()> onClick;
    std::function<bool()> isOnFn;
    bool small = false;
    bool pressed = false;
    bool holdMomentary = false;              // click toggles; click-and-hold (>300 ms) acts only while held
    std::function<void()> onExclusive;       // Ctrl/Cmd-click (solo/mute this one exclusively)
    double pressTime = 0.0;
    bool exclusivePress = false;
    bool isOn(PumpUI& ui) const;
    void draw(PumpUI& ui) override;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override;
    void mouseUp(PumpUI& ui, float x, float y) override;
    bool wantsTextEntry() const override { return false; }
};

// Row of adjacent keys for a choice parameter (or a custom selection).
struct Segmented : Control {
    std::vector<std::string> options;      // empty = the parameter's choice names
    std::function<int()> getFn;            // custom selection (when param < 0)
    std::function<void(int)> setFn;
    bool vertical = false;
    int count(PumpUI& ui) const;
    int selected(PumpUI& ui) const;
    std::string optionText(PumpUI& ui, int i) const;
    void draw(PumpUI& ui) override;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override;
    bool wantsTextEntry() const override { return false; }
};

// Dropdown for a choice parameter or a custom list.
struct MenuButton : Control {
    std::function<std::vector<std::string>()> itemsFn;   // custom items (when param < 0)
    std::function<int()> getFn;
    std::function<void(int)> setFn;
    std::function<std::string()> textFn;                 // custom face text
    bool arrows = false;                                  // ◀ ▶ steppers at the sides
    void draw(PumpUI& ui) override;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override;
    bool scroll(PumpUI& ui, float dy, uint mod) override;
    bool wantsTextEntry() const override { return false; }
    std::vector<std::string> items(PumpUI& ui) const;
    int current(PumpUI& ui) const;
    void choose(PumpUI& ui, int i);
    static int stepRow(const std::vector<std::string>& it, int from, int dir, bool wrap);
};

// ---------------------------------------------------------------------------------------------------------------
// Spectrum analyzer (UI thread): reads the newest samples from a ScopeRing, FFT, log-frequency display points
// with peak-ish ballistics and a +4.5 dB/oct display tilt (pink noise reads flat).

class SpectrumView {
public:
    static constexpr int kPoints = 320;
    // fftSize 0 = 8192 at 44.1/48 kHz, scaled up with the sample rate (to 32768 at 176.4/192 kHz) so resolution in
    // Hz and the noise floor read the same at every rate. Rings must hold at least N samples (plugins use 1 << 15).
    void setup(double sampleRate, int fftSize = 0)
    {
        sr = sampleRate;
        if (fftSize <= 0) fftSize = pump::analyzerSize(sampleRate);
        N = fftSize;
        fft.setup(N);
        in.resize((size_t)N); out.resize((size_t)N);
        win.assign((size_t)N, 0.0f);
        double wsum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double t = pump::kTwoPi * i / (N - 1);
            const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
            win[(size_t)i] = (float)w; wsum += w;
        }
        norm = (float)(2.0 / wsum);
        for (int p = 0; p < kPoints; ++p) { db[p] = -120.0f; }
        tmp.resize((size_t)N);
    }
    // Reads one or two rings (summed to mono).
    void update(const pump::ScopeRing* a, const pump::ScopeRing* b, float dt, float speed = 1.0f)
    {
        if (!a) return;
        a->readSmooth(tmp.data(), N);
        for (int i = 0; i < N; ++i) in[(size_t)i] = tmp[(size_t)i];
        if (b)
        {
            b->readSmooth(tmp.data(), N);
            for (int i = 0; i < N; ++i) in[(size_t)i] = 0.5f * (in[(size_t)i] + tmp[(size_t)i]);
        }
        for (int i = 0; i < N; ++i) in[(size_t)i] *= win[(size_t)i];
        fft.forward(in.data(), out.data());
        const float binHz = (float)(sr / N);
        const float decay = 30.0f * dt * speed;   // dB per frame fall
        const float rise = std::min(1.0f, 0.55f * speed * dt * 60.0f);
        const double nyq = 0.5 * sr;
        for (int p = 0; p < kPoints; ++p)
        {
            const double f0 = fLo * std::pow(fHi / fLo, (double)p / kPoints);
            const double f1 = fLo * std::pow(fHi / fLo, (double)(p + 1) / kPoints);
            // nothing exists above Nyquist (e.g. a 20 kHz-24 kHz range at 44.1 kHz): fall to the floor, never
            // extrapolate the top bins
            if (f0 >= nyq) { db[p] = std::max(-140.0f, db[p] - decay); continue; }
            int k0 = (int)std::floor(f0 / binHz), k1 = (int)std::ceil(f1 / binHz);
            k0 = std::max(1, k0); k1 = std::min(N / 2 - 1, std::max(k1, k0 + 1));
            float m = 0.0f;
            if (k1 - k0 <= 2)
            {
                // few bins: interpolate the power at the point's centre
                const double fc = std::sqrt(f0 * f1) / binHz;
                const int k = std::min(N / 2 - 2, std::max(1, (int)fc));
                const float t = (float)(fc - k);
                m = power(k) * (1 - t) + power(k + 1) * t;
            }
            else
            {
                double sum = 0.0;
                for (int k = k0; k < k1; ++k) sum += power(k);
                m = (float)(sum / (k1 - k0)) * 2.0f;   // average power, ~+3 dB so tones read near their level
            }
            const double fc = std::sqrt(f0 * f1);
            float v = 10.0f * std::log10(m + 1e-24f) + (float)(4.5 * std::log2(fc / 1000.0));
            v = std::max(v, -140.0f);
            if (v > db[p]) db[p] += (v - db[p]) * rise;
            else db[p] = std::max(v, db[p] - decay);
        }
    }
    float pointDb(int p) const
    {
        const int a = p > 0 ? p - 1 : p, b = p < kPoints - 1 ? p + 1 : p;
        return 0.25f * db[a] + 0.5f * db[p] + 0.25f * db[b];
    }
    double pointFreq(int p) const { return fLo * std::pow(fHi / fLo, (p + 0.5) / kPoints); }
    void setRange(double lo, double hi) { fLo = lo; fHi = hi; }
private:
    float power(int k) const
    {
        const float re = out[(size_t)(2 * k)] * norm, im = out[(size_t)(2 * k + 1)] * norm;
        return re * re + im * im;
    }
    double sr = 48000.0, fLo = 18.0, fHi = 22000.0;
    int N = 8192;
    pump::RealFFT fft;
    pump::AlignedBuffer in, out;
    std::vector<float> win, tmp;
    float norm = 1.0f;
    float db[kPoints] = {};
};

// ---------------------------------------------------------------------------------------------------------------
// The window

class PumpUI : public UI {
public:
    // logical layout size; the window scales uniformly from it
    static constexpr float kHeaderH = 46.0f, kFooterH = 24.0f;

    PumpUI(const ParamSpec* specs, uint32_t numParams, const std::vector<Preset>* presets, const Accent& accent,
           const char* tag, float logicalW, float logicalH, int bypassParam = -1)
        : UI((uint)logicalW, (uint)logicalH),
          W(logicalW), H(logicalH),
          fSpecs(specs), fNum(numParams), fPresets(presets), fAccent(accent), fTag(tag), fBypass(bypassParam)
    {
        fValues.resize(numParams);
        fLocalSet.assign(numParams, -1.0);
        for (uint32_t i = 0; i < numParams; ++i) fValues[i] = specs[i].def;
        const double sf = getScaleFactor();
        if (sf != 1.0) setSize((uint)(W * sf), (uint)(H * sf));
        setGeometryConstraints((uint)(W * 0.6 * sf), (uint)(H * 0.6 * sf), true, false);
        fFontLabel = createFontFromMemory("label", pump::fonts::kFontLabel, pump::fonts::kFontLabelSize, false);
        fFontMono = createFontFromMemory("mono", pump::fonts::kFontMono, pump::fonts::kFontMonoSize, false);
        fFontBold = createFontFromMemory("bold", pump::fonts::kFontMonoBold, pump::fonts::kFontMonoBoldSize, false);
        fDark = readGlobalTheme();
        theme.build(fAccent, fDark);
        buildHeader();
    }

    ~PumpUI() override { pump::winFrameClockStop(fFrameClock); fFrameClock = nullptr; }

    // ---- for subclasses ------------------------------------------------------------------------------------------
    Theme theme;
    float W, H;

    float value(int i) const { return (i >= 0 && i < (int)fNum) ? fValues[(size_t)i] : 0.0f; }
    const ParamSpec& spec(int i) const { return fSpecs[i]; }
    float norm(int i) const { return fSpecs[i].toNorm(fValues[(size_t)i]); }

    // Change a parameter from the UI (sends it to the host).
    void setParam(int i, float v)
    {
        if (i < 0 || i >= (int)fNum) return;
        v = fSpecs[i].clamp(v);
        if (v == fValues[(size_t)i]) return;
        fValues[(size_t)i] = v;
        fLocalSet[(size_t)i] = nowSeconds();
        setParameterValue((uint32_t)i, v);
        onParamChanged(i, v);
        repaint();
    }
    void setParamNorm(int i, float n) { setParam(i, fSpecs[i].fromNorm(n)); }
    // Gestures: every begin/end pair (and group) becomes one undo step (automation doesn't).
    void beginEdit(int i) { beginGroup(); if (i >= 0) editParameter((uint32_t)i, true); }
    void endEdit(int i) { if (i >= 0) editParameter((uint32_t)i, false); endGroup(); }
    void setParamGesture(int i, float v)
    {
        if (i < 0 || i >= (int)fNum || fSpecs[i].clamp(v) == fValues[(size_t)i]) return;
        beginEdit(i); setParam(i, v); endEdit(i);
    }
    void beginGroup() { if (fGestureDepth++ == 0 && !fApplyingHistory) fGestureBefore = serialize(); }
    void endGroup()
    {
        if (fGestureDepth <= 0) return;
        if (--fGestureDepth == 0 && !fApplyingHistory)
        {
            const std::string now = serialize();
            if (now != fGestureBefore)
            {
                undoStack().push_back(fGestureBefore);
                if (undoStack().size() > 200) undoStack().erase(undoStack().begin());
                redoStack().clear();
            }
        }
    }
    // each A/B slot has its own history, so undo never carries one slot's settings into the other
    std::vector<std::string>& undoStack() { return fUndo[fABSel]; }
    std::vector<std::string>& redoStack() { return fRedo[fABSel]; }
    bool canUndo() const { return !fUndo[fABSel].empty(); }
    bool canRedo() const { return !fRedo[fABSel].empty(); }
    void undo()
    {
        if (undoStack().empty()) return;
        redoStack().push_back(serialize());
        const std::string s = undoStack().back(); undoStack().pop_back();
        fApplyingHistory = true; deserialize(s); fApplyingHistory = false;
        onPresetApplied(); setStatus("Undo"); repaint();
    }
    void redo()
    {
        if (redoStack().empty()) return;
        undoStack().push_back(serialize());
        const std::string s = redoStack().back(); redoStack().pop_back();
        fApplyingHistory = true; deserialize(s); fApplyingHistory = false;
        onPresetApplied(); setStatus("Redo"); repaint();
    }

    template <class T> T* add(T* c) { fControls.emplace_back(c); return c; }
    Dial* addDial(int param, const char* label, Dial::Size sz = Dial::Medium, bool bipolar = false)
    {
        Dial* d = add(new Dial());
        d->param = param; d->label = label; d->size = sz; d->bipolar = bipolar;
        return d;
    }
    SliderRow* addRow(int param, const char* label, bool bipolar = false)
    {
        SliderRow* s = add(new SliderRow());
        s->param = param; s->label = label; s->bipolar = bipolar;
        return s;
    }
    Button* addToggle(int param, const char* label) { Button* b = add(new Button()); b->param = param; b->label = label; return b; }
    Button* addButton(const char* label, std::function<void()> fn, std::function<bool()> on = nullptr)
    {
        Button* b = add(new Button()); b->label = label; b->onClick = std::move(fn); b->isOnFn = std::move(on); return b;
    }
    Segmented* addSegmented(int param, std::vector<std::string> opts = {})
    {
        Segmented* s = add(new Segmented()); s->param = param; s->options = std::move(opts); return s;
    }
    MenuButton* addMenu(int param, const char* label = "") { MenuButton* m = add(new MenuButton()); m->param = param; m->label = label; return m; }

    // status line text (shown at the bottom left)
    void setStatus(const std::string& s) { fStatus = s; }
    std::string paramStatus(int i) const
    {
        if (i < 0) return "";
        return std::string(fSpecs[i].name) + "  " + formatParam(i, fValues[(size_t)i]);
    }
    // Value text of a parameter on controls and in the status line. Override for plugin-specific text ("OFF" at a
    // filter's end stop, note names); the default is the ParamSpec's own.
    virtual std::string formatParam(int i, float v) const { return fSpecs[i].format(v); }

    // ---- drawing helpers (logical coordinates) --------------------------------------------------------------------
    enum Font { kLabel, kMono, kBold };
    void font(Font f, float size)
    {
        fontFaceId(f == kLabel ? fFontLabel : (f == kMono ? fFontMono : fFontBold));
        fontSize(size);
    }
    void textAt(float x, float y, const std::string& s, Font f, float size, const Color& c, int align = ALIGN_LEFT | ALIGN_MIDDLE, float spacing = 0.0f)
    {
        if (s.empty()) return;
        font(f, size);
        textLetterSpacing(spacing);
        fillColor(c);
        textAlign(align);
        text(x, y, s.c_str(), nullptr);
        textLetterSpacing(0.0f);
    }
    float textWidth(const std::string& s, Font f, float size, float spacing = 0.0f)
    {
        if (s.empty()) return 0.0f;
        font(f, size);
        textLetterSpacing(spacing);
        Rectangle<float> b;
        const float w = textBounds(0, 0, s.c_str(), nullptr, b);
        textLetterSpacing(0.0f);
        return w;
    }
    void fillRect(const Rect& r, const Color& c) { beginPath(); rect(r.x, r.y, r.w, r.h); fillColor(c); fill(); }
    void strokeRect(const Rect& r, const Color& c, float w = 1.5f)
    {
        beginPath(); rect(r.x + 0.5f * w, r.y + 0.5f * w, r.w - w, r.h - w); strokeColor(c); strokeWidth(w); stroke();
    }
    void line(float x0, float y0, float x1, float y1, const Color& c, float w = 1.0f)
    {
        beginPath(); moveTo(x0, y0); lineTo(x1, y1); strokeColor(c); strokeWidth(w); stroke();
    }
    // A Simple-skin key/screen: fill, 1.5 px ink outline and an offset outlined block behind it.
    void keyBox(const Rect& r, bool filled, bool pressed = false, const Color* fillOverride = nullptr, float offset = 4.0f)
    {
        const float o = pressed ? 1.0f : offset;
        if (o > 0.0f)
        {
            fillRect(Rect(r.x + o, r.y + o, r.w, r.h), theme.bg);
            strokeRect(Rect(r.x + o, r.y + o, r.w, r.h), theme.dim, 1.5f);
        }
        fillRect(r, fillOverride ? *fillOverride : (filled ? theme.ink : theme.bg));
        strokeRect(r, theme.ink, 1.5f);
    }
    // A screen area (graphs): panel fill, ink outline, offset block.
    void screenBox(const Rect& r)
    {
        fillRect(Rect(r.x + 5, r.y + 5, r.w, r.h), theme.bg);
        strokeRect(Rect(r.x + 5, r.y + 5, r.w, r.h), theme.dim, 1.5f);
        fillRect(r, theme.screen);
        strokeRect(r, theme.ink, 1.5f);
    }
    // Section caption in small caps
    void caption(float x, float y, const std::string& s, const Color* c = nullptr)
    {
        textAt(x, y, s, kLabel, 11.0f, c ? *c : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.2f);
    }

    // Frequency <-> x on a log axis
    static float freqToX(const Rect& r, double f, double lo = 20.0, double hi = 20000.0)
    {
        return r.x + (float)(std::log(f / lo) / std::log(hi / lo)) * r.w;
    }
    static double xToFreq(const Rect& r, float x, double lo = 20.0, double hi = 20000.0)
    {
        return lo * std::pow(hi / lo, (double)((x - r.x) / r.w));
    }
    void drawFreqGrid(const Rect& r, double lo = 20.0, double hi = 20000.0, bool labels = true)
    {
        static const double fs[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                     1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
        for (double f : fs)
        {
            if (f < lo || f > hi) continue;
            const float x = freqToX(r, f, lo, hi);
            const bool major = f == 100 || f == 1000 || f == 10000;
            line(x, r.y, x, r.b(), major ? withAlpha2(theme.ink, theme.dark ? 0.22f : 0.20f) : theme.grid, 1.0f);
        }
        if (!labels) return;
        static const struct { double f; const char* t; } lb[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
            { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (const auto& l : lb)
        {
            if (l.f < lo || l.f > hi) continue;
            textAt(freqToX(r, l.f, lo, hi), r.b() - 8, l.t, kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }
    static Color withAlpha2(const Color& c, float a) { Color o = c; o.alpha = a; return o; }

    // Spectrum fill + line, dB range mapped to the rect.
    void drawSpectrum(const SpectrumView& sv, const Rect& r, float dbTop, float dbBottom, const Color& fillC, const Color& lineC,
                      double lo = 20.0, double hi = 20000.0, float lineW = 1.2f)
    {
        auto yOf = [&](float d) { return r.y + (dbTop - d) / (dbTop - dbBottom) * r.h; };
        beginPath();
        moveTo(r.x, r.b());
        for (int p = 0; p < SpectrumView::kPoints; ++p)
        {
            const double f = sv.pointFreq(p);
            if (f < lo * 0.9 || f > hi * 1.1) continue;
            lineTo(freqToX(r, f, lo, hi), std::min(r.b(), std::max(r.y, yOf(sv.pointDb(p)))));
        }
        lineTo(r.r(), r.b());
        closePath();
        fillColor(fillC);
        fill();
        beginPath();
        bool first = true;
        for (int p = 0; p < SpectrumView::kPoints; ++p)
        {
            const double f = sv.pointFreq(p);
            if (f < lo * 0.9 || f > hi * 1.1) continue;
            const float x = freqToX(r, f, lo, hi), y = std::min(r.b(), std::max(r.y, yOf(sv.pointDb(p))));
            if (first) { moveTo(x, y); first = false; } else lineTo(x, y);
        }
        strokeColor(lineC);
        strokeWidth(lineW);
        stroke();
    }

    // Vertical level meter (dBFS), with a held peak line and a scale on one side.
    void drawLevelMeter(const Rect& r, float levelDb, float peakDb, float dbMin = -60.0f, float dbMax = 6.0f)
    {
        fillRect(r, theme.panel2);
        auto yOf = [&](float d) { return r.b() - (std::max(dbMin, std::min(dbMax, d)) - dbMin) / (dbMax - dbMin) * r.h; };
        const float y = yOf(levelDb);
        const float y0 = yOf(0.0f);
        if (levelDb > dbMin)
        {
            fillRect(Rect(r.x, std::max(y, y0), r.w, r.b() - std::max(y, y0)), theme.neon);
            if (y < y0) fillRect(Rect(r.x, y, r.w, y0 - y), theme.red);
        }
        if (peakDb > dbMin) line(r.x, yOf(peakDb), r.r(), yOf(peakDb), peakDb > 0.0f ? theme.red : theme.ink, 1.5f);
        strokeRect(r, theme.ink, 1.0f);
    }
    // Gain-reduction meter: grows downward from the top.
    void drawGRMeter(const Rect& r, float grDb, float rangeDb = 24.0f)
    {
        fillRect(r, theme.panel2);
        const float h = std::min(1.0f, std::max(0.0f, -grDb / rangeDb)) * r.h;
        if (h > 0.5f) fillRect(Rect(r.x, r.y, r.w, h), theme.second);
        strokeRect(r, theme.ink, 1.0f);
    }

    // Override points
    virtual void onSampleRateChanged(double) {}    // re-setup analyzers etc. (the host changed the rate)
    virtual void layout() {}                       // place controls (logical coords)
    virtual void drawBody() {}                     // screens and graphs (before controls)
    virtual void drawOverlay() {}                  // after controls
    virtual void onParamChanged(int, float) {}
    virtual bool bodyMouseDown(float, float, int, uint, bool /*dbl*/) { return false; }
    virtual void bodyMouseDrag(float, float, uint) {}
    virtual void bodyMouseUp(float, float) {}
    virtual void bodyHover(float, float) {}
    virtual bool bodyScroll(float, float, float, uint) { return false; }
    virtual std::string footerInfo() { return ""; }
    virtual bool keyPress(uint /*key*/, uint /*mod*/) { return false; }
    virtual bool keyRelease(uint /*key*/, uint /*mod*/) { return false; }
    // Plugin-specific lines for the "?" key overlay: { keys, what they do }
    virtual std::vector<std::pair<std::string, std::string>> helpLines() { return {}; }
    // UI-only settings saved with the session ("key=value;..." in the "ui" state)
    virtual void loadUiState(const std::string&) {}
    void saveUiState(const std::string& s) { fUiState = s; setState("ui", s.c_str()); }
    static std::string uiStateGet(const std::string& s, const std::string& key, const std::string& def = "")
    {
        const std::string k = key + "=";
        size_t p = 0;
        while ((p = s.find(k, p)) != std::string::npos)
        {
            if (p == 0 || s[p - 1] == ';') { const size_t e = s.find(';', p); return s.substr(p + k.size(), e == std::string::npos ? std::string::npos : e - p - k.size()); }
            ++p;
        }
        return def;
    }
    virtual void tick(float /*dt*/) {}           // ~30-60 Hz, before repaint
    virtual bool animating() { return true; }      // keep repainting (meters/analyzers)

    // Header area for plugin extras (to the left of the theme/bypass keys)
    Rect headerExtra;

    // Popup menu (also usable by subclasses: right-click menus on graphs)
    void openMenu(const Rect& anchor, const std::vector<std::string>& items, int current, std::function<void(int)> onPick)
    {
        fMenu.open = true; fMenu.items = items; fMenu.current = current; fMenu.onPick = std::move(onPick);
        fMenu.hover = -1;
        const float itemH = 22.0f;
        float w = std::max(anchor.w, 140.0f);
        for (const auto& s : items) w = std::max(w, textWidth(s, kLabel, 12.5f, 0.6f) + 28.0f);
        const float h = itemH * (float)items.size() + 8.0f;
        float y = anchor.b() + 2.0f;
        if (y + h > H - 4.0f) y = std::max(4.0f, anchor.y - h - 2.0f);
        float x = std::min(anchor.x, W - w - 6.0f);
        fMenu.r = Rect(x, y, w, std::min(h, H - 8.0f));
        fMenu.itemH = itemH;
        fMenu.scroll = 0;
        if (h > H - 8.0f && current >= 0) fMenu.scroll = std::max(0.0f, std::min(h - (H - 8.0f), current * itemH - (H - 8.0f) * 0.5f));
        repaint();
    }
    // Typed value entry over a control. `ring` lists the fields Tab steps through (e.g. Freq -> Gain -> Q).
    void openTextEntry(int param, const Rect& r, const std::vector<std::pair<int, Rect>>& ring = {})
    {
        fEdit.ring = ring;
        fEdit.onText = nullptr;
        fEdit.active = true; fEdit.param = param; fEdit.r = r;
        fEdit.text = fSpecs[param].format(fValues[(size_t)param]);
        // strip units for editing convenience
        const size_t sp = fEdit.text.find(' ');
        if (sp != std::string::npos && !fSpecs[param].isChoice()) fEdit.text = fEdit.text.substr(0, sp);
        if (fSpecs[param].unit == pump::PUnit::Hz && fValues[(size_t)param] >= 1000.0f)
        {
            char b[32]; std::snprintf(b, sizeof b, "%.2fk", fValues[(size_t)param] / 1000.0f); fEdit.text = b;
        }
        fEdit.original = fEdit.text;
        fEdit.selectAll = true;
        takeKeyboardFocus();
        repaint();
    }

    // Typed text that isn't a parameter value (a preset name): Enter calls onText, Esc or a click elsewhere cancels.
    void openNameEntry(const Rect& r, const std::string& initial, std::function<void(const std::string&)> onText)
    {
        fEdit.ring.clear();
        fEdit.active = true; fEdit.param = -1; fEdit.r = r;
        fEdit.text = initial; fEdit.original = "\x01";
        fEdit.selectAll = true;
        fEdit.onText = std::move(onText);
        takeKeyboardFocus();
        repaint();
    }
    // Typing goes to whichever view has the keyboard: a host (or System EQ) may have given it to the view around
    // ours, so take it before asking for text.
    void takeKeyboardFocus() { getWindow().focus(); }

    void* pluginPtr() const { return getPluginInstancePointer(); }
    double sampleRate() const { return getSampleRate(); }
    bool isDark() const { return fDark; }
    void setDark(bool d)
    {
        fDark = d;
        theme.build(fAccent, d);
        setState("theme", d ? "dark" : "light");
        writeGlobalTheme(d);
        repaint();
    }

protected:
    // ---- DPF callbacks ------------------------------------------------------------------------------------------
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= fNum) return;
        value = fSpecs[index].clamp(value);   // hosts can send anything (CLAP passes raw values; NaN)
        // Hosts (Ableton Live especially) echo our own edits back late, sometimes with older values. Live (VST3,
        // plugins with more than 64 parameters) goes further: when a gesture begins on a parameter it pushes the
        // value *it* believes to the DSP, which can land after our new value and revert it there while this UI
        // still shows the edit. So for a parameter this UI changed itself in the last 300 ms, a differing echo is
        // not accepted but answered: re-send our value so host, DSP and UI agree again.
        if (value != fValues[index] && nowSeconds() - fLocalSet[index] < 0.3)
        {
            editParameter(index, true);   // VST3 wants every perform-edit inside begin/end
            setParameterValue(index, fValues[index]);
            editParameter(index, false);
            return;
        }
        fValues[index] = value;
        onParamChanged((int)index, value);
        repaint();
    }
   #if DISTRHO_PLUGIN_WANT_PROGRAMS
    void programLoaded(uint32_t index) override
    {
        if (fPresets && index < fPresets->size()) { fPresetName = (*fPresets)[index].name; fPresetIsUser = false; }
        repaint();
    }
   #endif
    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "theme") == 0 && value && *value)
        {
            fDark = std::strcmp(value, "dark") == 0;
            theme.build(fAccent, fDark);
        }
        else if (std::strcmp(key, "abA") == 0) fAB[0] = value ? value : "";
        else if (std::strcmp(key, "abB") == 0) fAB[1] = value ? value : "";
        else if (std::strcmp(key, "abSel") == 0) fABSel = (value && value[0] == 'B') ? 1 : 0;
        else if (std::strcmp(key, "preset") == 0)
        {
            const std::string v = value ? value : "";
            fPresetIsUser = v.rfind("user:", 0) == 0;
            fPresetName = fPresetIsUser ? v.substr(5) : v;
        }
        else if (std::strcmp(key, "ui") == 0) { fUiState = value ? value : ""; loadUiState(fUiState); }
        else onExtraState(key, value ? value : "");
        repaint();
    }
    void sampleRateChanged(double newRate) override { onSampleRateChanged(newRate); repaint(); }
    void uiIdle() override
    {
        // the frame clock drives the window on Windows; the host's idle only stands in when it falls silent
        if (fFrameClock != nullptr && nowSeconds() - fLastTick < 0.05) return;
        frame();
    }
    static void frameFromClock(void* self) { static_cast<PumpUI*>(self)->frame(); }
    void frame()
    {
        const double now = nowSeconds();
        float dt = (float)(now - fLastTick);
        if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 30.0f;
        fLastTick = now;
        tick(dt);
        if (animating()) repaint();
    }

    void onNanoDisplay() override
    {
        fScale = (float)getWidth() / W;
        save();
        scale(fScale, fScale);
        // background
        fillRect(Rect(0, 0, W, H), theme.bg);
        if (!fLaidOut)
        {
            layoutHeader(); layout(); fLaidOut = true;
            // test hooks for headless screenshots (unused in normal runs)
            if (const char* tp = std::getenv("PUMP_TEST_PRESET")) applyPreset(std::atoi(tp));
            if (const char* td = std::getenv("PUMP_TEST_DARK")) if (td[0] == '1') { fDark = true; theme.build(fAccent, true); }
            if (const char* tw = std::getenv("PUMP_TEST_WIDTH")) { const int w = std::atoi(tw); if (w > 200) setSize((uint)w, (uint)(w * H / W)); }
            fFrameClock = pump::winFrameClockStart(getWindow().getNativeWindowHandle(), &PumpUI::frameFromClock, this);
        }
        drawHeader();
        drawBody();
        for (auto& c : fControls)
            if (c->visible) c->draw(*this);
        drawOverlay();
        drawFooter();
        drawResizeGrip();
        if (fEdit.active) drawTextEntry();
        if (fHelpOpen) drawHelp();
        if (fMenu.open) drawMenu();
        restore();
    }

    void onResize(const ResizeEvent& ev) override
    {
        UI::onResize(ev);
        repaint();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float x = (float)ev.pos.getX() / fScale, y = (float)ev.pos.getY() / fScale;
        uint button = ev.button, mod = ev.mod;
       #ifdef __APPLE__
        if (button == kMouseButtonLeft && (mod & kModifierControl)) { button = kMouseButtonRight; mod &= ~(uint)kModifierControl; }
       #endif
        if (ev.press)
        {
            // a release can get lost (window lost focus/capture mid-drag): end that drag before starting anything
            finishPointer(x, y);
            const bool dbl = (ev.time - fLastClickTime) < 350 && std::fabs(x - fLastClickX) < 4 && std::fabs(y - fLastClickY) < 4 && button == fLastClickButton;
            fLastClickTime = dbl ? 0 : ev.time; fLastClickX = x; fLastClickY = y; fLastClickButton = button;
            // menus first
            if (fMenu.open)
            {
                if (fMenu.r.contains(x, y))
                {
                    const int i = menuIndexAt(y);
                    if (i >= 0 && i < (int)fMenu.items.size() && menuRowPickable(fMenu.items[(size_t)i]))
                    {
                        auto fn = fMenu.onPick;
                        fMenu.open = false;
                        if (fn) fn(i);
                    }
                }
                else fMenu.open = false;
                repaint();
                return true;
            }
            if (fHelpOpen) { fHelpOpen = false; repaint(); return true; }
            if (fEdit.active)
            {
                if (fEdit.r.contains(x, y)) return true;
                if (fEdit.param < 0) { fEdit.active = false; fEdit.onText = nullptr; setStatus("Not saved"); }   // name entry: cancel
                else commitTextEntry();
            }
            // resize grip
            if (gripRect().contains(x, y)) { fResizing = true; fResizeStartX = (float)ev.pos.getX(); fResizeStartW = (float)getWidth(); return true; }
            for (int i = (int)fControls.size() - 1; i >= 0; --i)
            {
                Control* c = fControls[(size_t)i].get();
                if (!c->visible || !c->enabled || !c->r.contains(x, y)) continue;
                const bool reset = cmdKey(mod) || (mod & kModifierAlt) != 0;
                if (c->param >= 0 && c->wantsTextEntry())
                {
                    if (button == kMouseButtonRight || (dbl && button == kMouseButtonLeft))
                    {
                        if (fActive) { fActive->mouseUp(*this, x, y); fActive = nullptr; }
                        std::vector<std::pair<int, Rect>> ring;
                        if (c->tabGroup > 0)
                            for (auto& o : fControls)
                                if (o->tabGroup == c->tabGroup && o->visible && o->enabled && o->param >= 0) ring.push_back({ o->param, o->valueRect() });
                        openTextEntry(c->param, c->valueRect(), ring);
                        return true;
                    }
                    if (reset && button == kMouseButtonLeft)
                    {
                        setParamGesture(c->param, fSpecs[c->param].def);
                        return true;
                    }
                }
                if (c->mouseDown(*this, x, y, (int)button, mod)) { fActive = c; return true; }
                return true;
            }
            if (y > kHeaderH && y < H - kFooterH)
            {
                if (bodyMouseDown(x, y, (int)button, mod, dbl)) { fBodyActive = true; return true; }
            }
            return false;
        }
        // release
        if (fResizing) { fResizing = false; return true; }
        if (fActive) { fActive->mouseUp(*this, x, y); fActive = nullptr; repaint(); return true; }
        if (fBodyActive) { fBodyActive = false; bodyMouseUp(x, y); repaint(); return true; }
        return false;
    }

    // End whatever the mouse was doing (drag, hold, audition) as if the button had been released here.
    void finishPointer(float x, float y)
    {
        if (fActive) { Control* c = fActive; fActive = nullptr; c->mouseUp(*this, x, y); }
        if (fBodyActive) { fBodyActive = false; bodyMouseUp(x, y); }
        fResizing = false;
    }
    void uiFocus(bool focus, DGL_NAMESPACE::CrossingMode mode) override
    {
        UI::uiFocus(focus, mode);
        if (!focus) { finishPointer(fLastClickX, fLastClickY); repaint(); }
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float x = (float)ev.pos.getX() / fScale, y = (float)ev.pos.getY() / fScale;
        if (fResizing)
        {
            const float nw = std::max(W * 0.6f * (float)getScaleFactor(), fResizeStartW + ((float)ev.pos.getX() - fResizeStartX));
            setSize((uint)nw, (uint)(nw * H / W));
            return true;
        }
        if (fMenu.open)
        {
            const int i = fMenu.r.contains(x, y) ? menuIndexAt(y) : -1;
            fMenu.hover = (i >= 0 && i < (int)fMenu.items.size() && menuRowPickable(fMenu.items[(size_t)i])) ? i : -1;
            repaint();
            return true;
        }
        if (fActive) { fActive->mouseDrag(*this, x, y, ev.mod); if (fActive->param >= 0) fStatus = paramStatus(fActive->param); repaint(); return true; }
        if (fBodyActive) { bodyMouseDrag(x, y, ev.mod); repaint(); return true; }
        // hover status
        std::string st;
        for (int i = (int)fControls.size() - 1; i >= 0; --i)
        {
            Control* c = fControls[(size_t)i].get();
            if (c->visible && c->r.contains(x, y))
            {
                st = !c->tip.empty() ? c->tip : (c->param >= 0 ? paramStatus(c->param) : c->label);
                break;
            }
        }
        // hovering the brand / tag / kind: what this plugin does
        if (st.empty() && y < kHeaderH && x < brandRight()) st = PUMP_PLUGIN_ABOUT;
        fStatus = st;
        if (y > kHeaderH && y < H - kFooterH) bodyHover(x, y);
        repaint();
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = (float)ev.pos.getX() / fScale, y = (float)ev.pos.getY() / fScale;
        const float dy = (float)ev.delta.getY();
        if (dy == 0.0f) return false;   // horizontal wheel / trackpad end-of-gesture: not "down"
        if (fMenu.open)
        {
            const float total = fMenu.itemH * (float)fMenu.items.size() + 8.0f;
            fMenu.scroll = std::max(0.0f, std::min(std::max(0.0f, total - fMenu.r.h), fMenu.scroll - dy * 22.0f));
            repaint();
            return true;
        }
        for (int i = (int)fControls.size() - 1; i >= 0; --i)
        {
            Control* c = fControls[(size_t)i].get();
            if (c->visible && c->enabled && c->r.contains(x, y))
            {
                if (c->scroll(*this, dy, ev.mod)) { if (c->param >= 0) fStatus = paramStatus(c->param); repaint(); return true; }
                return false;
            }
        }
        if (y > kHeaderH && y < H - kFooterH && bodyScroll(x, y, dy, ev.mod)) { repaint(); return true; }
        return false;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!fEdit.active)
        {
            const uint k = ev.key >= 'A' && ev.key <= 'Z' ? ev.key + 32 : ev.key;
            if (fMenu.open || fHelpOpen)   // a popup is up: keys don't reach the plugin (Esc / ? close it)
            {
                if (ev.press && (ev.key == kKeyEscape || (fHelpOpen && (ev.key == '?' || k == '/' || ev.key == kKeyF1))))
                { fHelpOpen = false; fMenu.open = false; repaint(); }
                return true;
            }
            if (!ev.press) return keyRelease(ev.key, ev.mod);
            const bool cmd = cmdKey(ev.mod);
            if (cmd && k == 'z') { if (ev.mod & kModifierShift) redo(); else undo(); return true; }
            if (cmd && k == 'y') { redo(); return true; }
            if (ev.key == '?' || k == '/' || ev.key == kKeyF1) { fHelpOpen = !fHelpOpen; repaint(); return true; }
            return keyPress(ev.key, ev.mod);
        }
        if (!ev.press) return false;
        if (ev.key == kKeyTab) { if (fEdit.param >= 0) tabTextEntry((ev.mod & kModifierShift) != 0); return true; }
        if (ev.key == kKeyEnter) { commitTextEntry(); return true; }
        if (ev.key == kKeyEscape) { if (fEdit.param < 0) setStatus("Not saved"); fEdit.active = false; fEdit.onText = nullptr; repaint(); return true; }
        if (ev.key == kKeyBackspace)
        {
            if (fEdit.selectAll) fEdit.text.clear();
            else if (!fEdit.text.empty()) fEdit.text.pop_back();
            fEdit.selectAll = false;
            repaint();
            return true;
        }
        return true;
    }
    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!fEdit.active) return false;
        const uint c = ev.character;
        if (c >= 32 && c < 127)
        {
            if (fEdit.selectAll) { fEdit.text.clear(); fEdit.selectAll = false; }
            if (fEdit.text.size() < (fEdit.param < 0 ? 40u : 16u)) fEdit.text.push_back((char)c);
            repaint();
        }
        return true;
    }

    // ---- header --------------------------------------------------------------------------------------------------
    void buildHeader()
    {
        fPresetMenu = add(new MenuButton());
        fPresetMenu->arrows = true;
        fPresetMenu->tip = "Presets (the side arrows step through them; Save preset… at the end of the menu keeps your own)";
        fPresetMenu->itemsFn = [this]() { return buildPresetMenu(true); };
        fPresetMenu->getFn = [this]() {
            for (size_t i = 0; i < fPresetRows.size(); ++i)
            {
                const PresetRow& r = fPresetRows[i];
                if (r.kind == PresetRow::Factory && !fPresetIsUser && fPresets && fPresetName == (*fPresets)[(size_t)r.index].name) return (int)i;
                if (r.kind == PresetRow::User && fPresetIsUser && fPresetName == fUserPresets[(size_t)r.index]) return (int)i;
            }
            return -1;
        };
        fPresetMenu->setFn = [this](int i) { pickPresetRow(i); };
        buildPresetMenu(true);
        fPresetMenu->textFn = [this]() { return fPresetName.empty() ? std::string("Default") : fPresetName; };

        fABButton = add(new Segmented());
        fABButton->options = { "A", "B" };
        fABButton->getFn = [this]() { return fABSel; };
        fABButton->setFn = [this](int i) { selectAB(i); };
        fABButton->tip = "Compare two settings: A and B";
        fCopyAB = addButton("COPY", [this]() { copyAB(); });
        fCopyAB->small = true;
        fCopyAB->tip = "Copy this setting to the other slot";

        fUndoBtn = addButton("UNDO", [this]() { undo(); });
        fUndoBtn->small = true;
        fUndoBtn->tip = "Undo (Ctrl/Cmd+Z)";
        fRedoBtn = addButton("REDO", [this]() { redo(); });
        fRedoBtn->small = true;
        fRedoBtn->tip = "Redo (Shift+Ctrl/Cmd+Z or Ctrl+Y)";
        fHelpBtn = addButton("?", [this]() { fHelpOpen = !fHelpOpen; }, [this]() { return fHelpOpen; });
        fHelpBtn->small = true;
        fHelpBtn->tip = "Mouse and keyboard shortcuts (?)";
        fThemeBtn = addButton("DARK", [this]() { setDark(!fDark); }, [this]() { return fDark; });
        fThemeBtn->small = true;
        fThemeBtn->tip = "Light or dark (remembered for new instances)";
        if (fBypass >= 0)
        {
            fBypassBtn = addToggle(fBypass, "BYPASS");
            fBypassBtn->small = true;
        }
    }
    void layoutHeader()
    {
        const float y = 9.0f, h = 28.0f;
        float x = brandRight() + 18.0f;
        const float presetW = std::min(280.0f, W * 0.25f);
        fPresetMenu->r = Rect(x, y, presetW, h);
        x += presetW + 14.0f;
        fUndoBtn->r = Rect(x, y, 50.0f, h);
        x += 50.0f + 6.0f;
        fRedoBtn->r = Rect(x, y, 50.0f, h);
        x += 50.0f + 14.0f;
        fABButton->r = Rect(x, y, 56.0f, h);
        x += 56.0f + 8.0f;
        fCopyAB->r = Rect(x, y, 52.0f, h);
        x += 52.0f + 14.0f;
        float rx = W - 12.0f;
        if (fBypassBtn) { fBypassBtn->r = Rect(rx - 74.0f, y, 74.0f, h); rx -= 74.0f + 8.0f; }
        fThemeBtn->r = Rect(rx - 56.0f, y, 56.0f, h);
        rx -= 56.0f + 8.0f;
        fHelpBtn->r = Rect(rx - 30.0f, y, 30.0f, h);
        rx -= 30.0f + 12.0f;
        headerExtra = Rect(x, y, std::max(0.0f, rx - x), h);
    }
    // the kind label ("RESONANCE / SUPPRESSOR"): one word per line, at most two lines
    std::vector<std::string> kindLines() const
    {
        std::string k = PUMP_PLUGIN_KIND;
        for (char& ch : k) ch = (char)std::toupper((unsigned char)ch);
        std::vector<std::string> out;
        if (k.empty()) return out;
        const size_t sp = k.find(' ');
        if (sp == std::string::npos) out.push_back(k);
        else { out.push_back(k.substr(0, sp)); out.push_back(k.substr(sp + 1)); }
        return out;
    }
    static constexpr float kKindSize = 10.0f, kKindSpacing = 1.2f;
    float kindWidth()
    {
        float w = 0.0f;
        for (const auto& l : kindLines()) w = std::max(w, textWidth(l, kLabel, kKindSize, kKindSpacing));
        return w;
    }
    float tagRight() { return 14.0f + textWidth("…pump", kBold, 21.0f) + 10.0f + textWidth(fTag, kBold, 15.0f) + 16.0f; }
    float brandRight() { const float kw = kindWidth(); return tagRight() + (kw > 0.0f ? 9.0f + kw : 0.0f); }
    void drawHeader()
    {
        fillRect(Rect(0, 0, W, kHeaderH), theme.panel2);
        line(0, kHeaderH - 0.75f, W, kHeaderH - 0.75f, theme.ink, 1.5f);
        // brand: …pump in ink, then the plugin's tag as a lit neon key
        const float bx = 14.0f;
        textAt(bx + 1.5f, kHeaderH * 0.5f + 2.0f, "…pump", kBold, 21.0f, theme.dim);
        textAt(bx, kHeaderH * 0.5f, "…pump", kBold, 21.0f, theme.ink);
        const float tx = bx + textWidth("…pump", kBold, 21.0f) + 10.0f;
        const float tw = textWidth(fTag, kBold, 15.0f) + 16.0f;
        Rect tag(tx, 10.0f, tw, 26.0f);
        Color n = theme.neon;
        keyBox(tag, false, false, &n, 3.0f);
        textAt(tag.cx(), tag.cy() + 0.5f, fTag, kBold, 15.0f, theme.neonInk, ALIGN_CENTER | ALIGN_MIDDLE);
        // what it is, in small caps beside the tag
        const auto kl = kindLines();
        const float kx = tag.r() + 9.0f;
        if (kl.size() == 1) textAt(kx, tag.cy() + 0.5f, kl[0], kLabel, kKindSize, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, kKindSpacing);
        else if (kl.size() == 2)
        {
            textAt(kx, tag.cy() - 5.5f, kl[0], kLabel, kKindSize, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, kKindSpacing);
            textAt(kx, tag.cy() + 6.0f, kl[1], kLabel, kKindSize, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, kKindSpacing);
        }
    }
    void drawFooter()
    {
        const Rect f(0, H - kFooterH, W, kFooterH);
        fillRect(f, theme.panel2);
        line(0, f.y + 0.75f, W, f.y + 0.75f, theme.ink, 1.5f);
        const std::string info = footerInfo();
        // the status line never runs into the info on the right
        const float infoW = info.empty() ? 0.0f : textWidth(info, kLabel, 11.5f, 0.6f) + 18.0f;
        save();
        scissor(0.0f, f.y, std::max(0.0f, W - 30.0f - infoW), f.h);
        textAt(12.0f, f.cy(), fStatus, kLabel, 12.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        restore();
        textAt(W - 30.0f, f.cy(), info, kLabel, 11.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
    }
    Rect gripRect() const { return Rect(W - 18.0f, H - 18.0f, 18.0f, 18.0f); }
    void drawResizeGrip()
    {
        const Rect g = gripRect();
        for (int i = 1; i <= 3; ++i)
            line(g.r() - 3.0f - i * 4.0f, g.b() - 3.0f, g.r() - 3.0f, g.b() - 3.0f - i * 4.0f, theme.ink2, 1.2f);
    }

    // ---- menus and text entry -------------------------------------------------------------------------------------
    int menuIndexAt(float y) const { return (int)std::floor((y - fMenu.r.y - 4.0f + fMenu.scroll) / fMenu.itemH); }
public:
    // Menu rows: "-" separator, "#TITLE" heading (not pickable), ">Action" (pickable, but ◀ ▶ and the wheel skip it)
    static bool menuRowPickable(const std::string& s) { return !s.empty() && s != "-" && s[0] != '#'; }
    static bool menuRowSteppable(const std::string& s) { return menuRowPickable(s) && s[0] != '>'; }
protected:
    void drawMenu()
    {
        const Rect& r = fMenu.r;
        fillRect(Rect(r.x + 5, r.y + 5, r.w, r.h), withAlpha2(theme.ink, 0.25f));
        fillRect(r, theme.bg);
        strokeRect(r, theme.ink, 1.5f);
        save();
        scissor(r.x, r.y, r.w, r.h);
        for (size_t i = 0; i < fMenu.items.size(); ++i)
        {
            const float iy = r.y + 4.0f + (float)i * fMenu.itemH - fMenu.scroll;
            if (iy + fMenu.itemH < r.y || iy > r.b()) continue;
            const std::string& s = fMenu.items[i];
            if (s == "-") { line(r.x + 8, iy + fMenu.itemH * 0.5f, r.r() - 8, iy + fMenu.itemH * 0.5f, theme.dim, 1.0f); continue; }
            const bool cur = (int)i == fMenu.current, hov = (int)i == fMenu.hover;
            if (hov) fillRect(Rect(r.x + 3, iy, r.w - 6, fMenu.itemH), theme.ink);
            else if (cur) fillRect(Rect(r.x + 3, iy, r.w - 6, fMenu.itemH), theme.panel2);
            if (s.size() > 1 && s[0] == '#')   // group heading
                textAt(r.x + 12, iy + fMenu.itemH * 0.5f, s.substr(1), kLabel, 10.5f, hov ? theme.on : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.2f);
            else if (s.size() > 1 && s[0] == '>')   // action (Save preset…): pickable, skipped by ◀ ▶
                textAt(r.x + 14, iy + fMenu.itemH * 0.5f, s.substr(1), kLabel, 12.5f, hov ? theme.on : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            else
                textAt(r.x + 14, iy + fMenu.itemH * 0.5f, s, kLabel, 12.5f, hov ? theme.on : theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        }
        restore();
    }
    void drawTextEntry()
    {
        const Rect r = fEdit.r;
        fillRect(r, theme.bg);
        strokeRect(r, theme.ink, 1.5f);
        if (fEdit.selectAll && !fEdit.text.empty())
        {
            const float tw = textWidth(fEdit.text, kMono, 12.0f);
            fillRect(Rect(r.cx() - tw * 0.5f - 2, r.y + 3, tw + 4, r.h - 6), theme.neon);
        }
        textAt(r.cx(), r.cy(), fEdit.text, kMono, 12.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        if (!fEdit.selectAll)
        {
            const float tw = textWidth(fEdit.text, kMono, 12.0f);
            line(r.cx() + tw * 0.5f + 1, r.y + 4, r.cx() + tw * 0.5f + 1, r.b() - 4, theme.ink, 1.2f);
        }
    }
    void commitTextEntry()
    {
        if (!fEdit.active) return;
        fEdit.active = false;
        if (fEdit.param < 0)
        {
            auto fn = std::move(fEdit.onText);
            fEdit.onText = nullptr;
            if (fn) fn(fEdit.text);
            repaint();
            return;
        }
        float v;
        if (fEdit.text != fEdit.original && fSpecs[fEdit.param].parse(fEdit.text.c_str(), v)) setParamGesture(fEdit.param, v);
        repaint();
    }
    void tabTextEntry(bool back)
    {
        const std::vector<std::pair<int, Rect>> ring = fEdit.ring;
        const int cur = fEdit.param;
        commitTextEntry();
        if (ring.size() < 2) return;
        int k = 0;
        for (size_t i = 0; i < ring.size(); ++i) if (ring[i].first == cur) k = (int)i;
        k = (k + (back ? (int)ring.size() - 1 : 1)) % (int)ring.size();
        openTextEntry(ring[(size_t)k].first, ring[(size_t)k].second, ring);
    }

    // ---- presets and A/B ----------------------------------------------------------------------------------------------
    std::string serialize() const
    {
        std::string s;
        char buf[96];
        for (uint32_t i = 0; i < fNum; ++i)
        {
            // momentary parameters (Alt-listen, band listen, meter reset) are never part of undo or A/B
            if (fSpecs[i].flags & (pump::kPOutput | pump::kPBypass | pump::kPNoUndo | pump::kPHidden)) continue;
            std::snprintf(buf, sizeof buf, "%s=%.9g;", fSpecs[i].symbol, fValues[i]);
            s += buf;
        }
        const std::string extra = serializeExtra();
        if (!extra.empty()) { s += '\x1E'; s += extra; }
        return s;
    }
    void deserialize(const std::string& all)
    {
        const size_t sep = all.find('\x1E');
        const std::string s = sep == std::string::npos ? all : all.substr(0, sep);
        beginGroup();
        size_t p = 0;
        while (p < s.size())
        {
            const size_t eq = s.find('=', p), sc = s.find(';', p);
            if (eq == std::string::npos || sc == std::string::npos || eq > sc) break;
            const std::string key = s.substr(p, eq - p);
            const std::string num = s.substr(eq + 1, sc - eq - 1);
            const char* end = nullptr;
            const float v = (float)pump::parseNumber(num.c_str(), &end);   // '.' or ',' whatever the locale
            if (end != num.c_str())
                for (uint32_t i = 0; i < fNum; ++i)
                    if (key == fSpecs[i].symbol) { if (!(fSpecs[i].flags & (pump::kPNoUndo | pump::kPHidden | pump::kPOutput))) setParamGesture((int)i, v); break; }
            p = sc + 1;
        }
        if (sep != std::string::npos) deserializeExtra(all.substr(sep + 1));
        endGroup();
    }
    void applyPreset(int idx)
    {
        if (!fPresets || idx < 0 || idx >= (int)fPresets->size()) return;
        const Preset& pr = (*fPresets)[(size_t)idx];
        beginGroup();
        for (uint32_t i = 0; i < fNum; ++i)
        {
            if (fSpecs[i].flags & (pump::kPOutput | pump::kPBypass | pump::kPNoUndo | pump::kPHidden)) continue;
            float v = fSpecs[i].def;
            for (const auto& pv : pr.values) if (std::strcmp(pv.symbol, fSpecs[i].symbol) == 0) { v = pv.value; break; }
            setParamGesture((int)i, v);
        }
        endGroup();
        fPresetName = pr.name;
        fPresetIsUser = false;
        setState("preset", fPresetName.c_str());
        onPresetApplied();
        repaint();
    }
    virtual void onPresetApplied() {}
    // A plugin's own states (beyond the parameters) that take part in undo and A/B: serialize() appends
    // serializeExtra() after a \x1E separator (nothing when it is empty, so other plugins are unchanged), and
    // deserialize() hands that part back to deserializeExtra().
    virtual std::string serializeExtra() const { return ""; }
    virtual void deserializeExtra(const std::string&) {}
    // states of the plugin's own that the host restores (TUNE's notes)
    virtual void onExtraState(const char* /*key*/, const char* /*value*/) {}

    // ---- the preset menu: factory presets (grouped by section), your own presets, Save / Delete ----------------------
    struct PresetRow { enum Kind { Other, Factory, User, Save, Delete } kind = Other; int index = -1; };
    std::vector<std::string> buildPresetMenu(bool rescan)
    {
        if (rescan) scanUserPresets();
        std::vector<std::string> items;
        fPresetRows.clear();
        auto add = [&](const std::string& s, PresetRow::Kind k, int idx) { items.push_back(s); fPresetRows.push_back({ k, idx }); };
        if (fPresets)
        {
            std::string last = "\x01";
            for (size_t i = 0; i < fPresets->size(); ++i)
            {
                const Preset& p = (*fPresets)[i];
                const std::string sec = p.section ? p.section : "FACTORY";
                if (sec != last) { add("#" + sec, PresetRow::Other, -1); last = sec; }
                add(p.name, PresetRow::Factory, (int)i);
            }
        }
        if (!fUserPresets.empty())
        {
            add("#YOUR PRESETS", PresetRow::Other, -1);
            for (size_t i = 0; i < fUserPresets.size(); ++i) add(fUserPresets[i], PresetRow::User, (int)i);
        }
        add("-", PresetRow::Other, -1);
        add(">Save preset…", PresetRow::Save, -1);
        if (fPresetIsUser && !fPresetName.empty()) add(">Delete \"" + fPresetName + "\"", PresetRow::Delete, -1);
        return items;
    }
    void pickPresetRow(int i)
    {
        if (i < 0 || i >= (int)fPresetRows.size()) return;
        const PresetRow r = fPresetRows[(size_t)i];
        switch (r.kind)
        {
        case PresetRow::Factory: applyPreset(r.index); break;
        case PresetRow::User: if (r.index < (int)fUserPresets.size()) applyUserPreset(fUserPresets[(size_t)r.index]); break;
        case PresetRow::Save:
            openNameEntry(fPresetMenu->r, fPresetIsUser ? fPresetName : std::string("My preset"),
                          [this](const std::string& name) { saveUserPreset(name); });
            setStatus("Name the preset, then Enter to save (Esc cancels)");
            break;
        case PresetRow::Delete: deleteUserPreset(fPresetName); break;
        default: break;
        }
        repaint();
    }

    // Your presets live as small text files, one folder per plugin:
    //   macOS   ~/Documents/wake/pump/Presets/<TAG>/      Windows  Documents\wake\pump\Presets\<TAG>
    //   Linux   ~/.local/share/wake/pump/Presets/<TAG>/
    // Each file holds "symbol=value" lines (parameter symbols never change, so presets survive updates).
    std::filesystem::path userPresetDir() const
    {
        namespace fs = std::filesystem;
       #if defined(_WIN32)
        const char* home = std::getenv("USERPROFILE");
        if (!home) return {};
        return fs::u8path(home) / "Documents" / "wake" / "pump" / "Presets" / fs::u8path(fTag);
       #elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (!home) return {};
        return fs::u8path(home) / "Documents" / "wake" / "pump" / "Presets" / fs::u8path(fTag);
       #else
        const char* xdg = std::getenv("XDG_DATA_HOME");
        const char* home = std::getenv("HOME");
        if (xdg && *xdg) return fs::u8path(xdg) / "wake" / "pump" / "Presets" / fs::u8path(fTag);
        if (!home) return {};
        return fs::u8path(home) / ".local" / "share" / "wake" / "pump" / "Presets" / fs::u8path(fTag);
       #endif
    }
    static std::string presetFileName(const std::string& name)
    {
        std::string f;
        for (char ch : name) f += (std::strchr("/\\:*?\"<>|", ch) || (unsigned char)ch < 32) ? '-' : ch;
        while (!f.empty() && (f.back() == ' ' || f.back() == '.')) f.pop_back();
        return f.empty() ? std::string("preset") : f;
    }
    // Reads a preset file: its name (the "name=" line, else the file name) and its values.
    // A real preset is a few hundred bytes (one short line per parameter): a file far past that is not one, and the
    // folder is scanned (every file opened) whenever the menu opens, so bound what one file can cost.
    static constexpr std::uintmax_t kMaxPresetBytes = 256 * 1024;
    static constexpr size_t kMaxPresetLine = 512;
    static constexpr size_t kMaxPresetValues = 4096;
    static bool readPresetFile(const std::filesystem::path& p, std::string& name, std::vector<std::pair<std::string, float>>& values)
    {
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(p, ec);
        if (ec || size > kMaxPresetBytes) return false;
        std::ifstream in(p);
        if (!in) return false;
        name = p.stem().u8string();
        values.clear();
        std::string line;
        while (std::getline(in, line))
        {
            if (line.size() > kMaxPresetLine || values.size() >= kMaxPresetValues) continue;   // not a line of ours: skip it
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "name") { if (!v.empty()) name = v; continue; }
            if (k == "plugin") continue;
            const char* end = nullptr;
            const float f = (float)pump::parseNumber(v.c_str(), &end);   // '.' or ',' whatever the locale
            if (end != v.c_str()) values.push_back({ k, f });
        }
        return true;
    }
    void scanUserPresets()
    {
        namespace fs = std::filesystem;
        fUserPresets.clear(); fUserPresetFiles.clear();
        const fs::path dir = userPresetDir();
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) return;
        std::vector<std::pair<std::string, fs::path>> found;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".pumppreset") continue;
            std::string name; std::vector<std::pair<std::string, float>> vals;
            if (readPresetFile(it->path(), name, vals)) found.push_back({ name, it->path() });
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
            std::string x = a.first, y = b.first;
            for (char& c : x) c = (char)std::tolower((unsigned char)c);
            for (char& c : y) c = (char)std::tolower((unsigned char)c);
            return x < y;
        });
        for (const auto& f : found) { fUserPresets.push_back(f.first); fUserPresetFiles.push_back(f.second); }
    }
    void applyUserPreset(const std::string& name)
    {
        scanUserPresets();
        for (size_t k = 0; k < fUserPresets.size(); ++k)
        {
            if (fUserPresets[k] != name) continue;
            std::string nm; std::vector<std::pair<std::string, float>> vals;
            if (!readPresetFile(fUserPresetFiles[k], nm, vals)) break;
            beginGroup();
            for (uint32_t i = 0; i < fNum; ++i)
            {
                if (fSpecs[i].flags & (pump::kPOutput | pump::kPBypass | pump::kPNoUndo | pump::kPHidden)) continue;
                float v = fSpecs[i].def;   // anything the file doesn't mention (a newer parameter) starts at its default
                for (const auto& pv : vals) if (pv.first == fSpecs[i].symbol) { v = pv.second; break; }
                setParamGesture((int)i, fSpecs[i].clamp(v));
            }
            endGroup();
            fPresetName = name;
            fPresetIsUser = true;
            setState("preset", ("user:" + name).c_str());
            onPresetApplied();
            setStatus("Loaded your preset \"" + name + "\"");
            repaint();
            return;
        }
        setStatus("Preset \"" + name + "\" not found (moved or deleted?)");
    }
    void saveUserPreset(std::string name)
    {
        namespace fs = std::filesystem;
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name.empty()) { setStatus("Not saved: the name is empty"); return; }
        const fs::path dir = userPresetDir();
        std::error_code ec;
        if (dir.empty() || (!fs::create_directories(dir, ec) && ec)) { setStatus("Could not create the presets folder"); return; }
        // the same name (case-insensitively) overwrites; otherwise a new file
        scanUserPresets();
        fs::path file = dir / fs::u8path(presetFileName(name) + ".pumppreset");
        for (size_t k = 0; k < fUserPresets.size(); ++k)
        {
            std::string a = fUserPresets[k], b = name;
            for (char& c : a) c = (char)std::tolower((unsigned char)c);
            for (char& c : b) c = (char)std::tolower((unsigned char)c);
            if (a == b) { file = fUserPresetFiles[k]; break; }
        }
        // Write to a temporary file next to it, then rename over the real one: a crash or a full disk part-way
        // leaves the old preset intact instead of a truncated one (rename replaces atomically on one volume).
        fs::path tmp = file; tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) { setStatus("Could not write the preset file"); return; }
            out << "# pump preset\nplugin=" << fTag << "\nname=" << name << "\n";
            char buf[64];
            for (uint32_t i = 0; i < fNum; ++i)
            {
                if (fSpecs[i].flags & (pump::kPOutput | pump::kPBypass | pump::kPNoUndo | pump::kPHidden)) continue;
                std::snprintf(buf, sizeof buf, "%.9g", fValues[i]);
                for (char* c = buf; *c; ++c) if (*c == ',') *c = '.';   // locale-proof
                out << fSpecs[i].symbol << "=" << buf << "\n";
            }
            out.flush();
            if (!out) { out.close(); fs::remove(tmp, ec); setStatus("Could not write the preset file (disk full?)"); return; }
        }
        fs::rename(tmp, file, ec);
        if (ec) { std::error_code ec2; fs::remove(tmp, ec2); setStatus("Could not write the preset file"); return; }
        fPresetName = name;
        fPresetIsUser = true;
        setState("preset", ("user:" + name).c_str());
        scanUserPresets();
        setStatus("Saved \"" + name + "\" (" + dir.u8string() + ")");
        repaint();
    }
    void deleteUserPreset(const std::string& name)
    {
        namespace fs = std::filesystem;
        scanUserPresets();
        for (size_t k = 0; k < fUserPresets.size(); ++k)
            if (fUserPresets[k] == name)
            {
                std::error_code ec;
                fs::remove(fUserPresetFiles[k], ec);
                if (ec) { setStatus("Could not delete \"" + name + "\""); return; }
                fPresetIsUser = false;
                setState("preset", name.c_str());
                scanUserPresets();
                setStatus("Deleted your preset \"" + name + "\" (the current settings stay)");
                repaint();
                return;
            }
    }
    void selectAB(int i)
    {
        if (i == fABSel) return;
        fAB[fABSel] = serialize();
        setState(fABSel ? "abB" : "abA", fAB[fABSel].c_str());
        fABSel = i;
        setState("abSel", i ? "B" : "A");
        if (!fAB[i].empty())
        {
            fApplyingHistory = true;   // switching slots is not an undo step
            deserialize(fAB[i]);
            fApplyingHistory = false;
        }
        else { fAB[i] = serialize(); setState(i ? "abB" : "abA", fAB[i].c_str()); }
        onPresetApplied();
        repaint();
    }
    void copyAB()
    {
        const int other = 1 - fABSel;
        fAB[other] = serialize();
        fUndo[other].clear(); fRedo[other].clear();   // its old history no longer applies
        setState(other ? "abB" : "abA", fAB[other].c_str());
        fStatus = std::string("Copied to ") + (other ? "B" : "A");
        repaint();
    }

    // ---- the "?" overlay ------------------------------------------------------------------------------------------------
    void drawHelp()
    {
        std::vector<std::pair<std::string, std::string>> lines = {
            { "Drag", "change a control (up/right = more)" },
            { "Shift + drag / wheel", "fine adjustment" },
            { "Double-click / right-click", "type a value: 1k, A4, C#3+13, 2x (= +6 dB), 50% (halfway)" },
            { "Tab (while typing)", "next field" },
            { "Ctrl/Cmd + click", "reset to default (Alt + click works too)" },
            { "Mouse wheel", "step a control" },
            { "Ctrl/Cmd + Z", "undo     Shift + Ctrl/Cmd + Z, Ctrl + Y: redo" },
            { "A / B, COPY", "compare two settings, copy this one to the other slot" },
        };
        const auto extra = helpLines();
        if (!extra.empty()) { lines.push_back({ "", "" }); for (const auto& e : extra) lines.push_back(e); }
        const float lh = 21.0f, w = std::min(W - 80.0f, 820.0f), h = 74.0f + lh * (float)lines.size();
        const Rect r((W - w) * 0.5f, std::max(kHeaderH + 10.0f, (H - h) * 0.5f), w, std::min(h, H - kHeaderH - 30.0f));
        fillRect(Rect(0, kHeaderH, W, H - kHeaderH), withAlpha2(theme.bg, 0.55f));
        fillRect(Rect(r.x + 6, r.y + 6, r.w, r.h), theme.dim);
        fillRect(r, theme.bg);
        strokeRect(r, theme.ink, 1.5f);
        textAt(r.x + 20, r.y + 24, "MOUSE AND KEYS", kLabel, 13.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 2.0f);
        textAt(r.r() - 20, r.y + 24, "click or Esc to close", kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        line(r.x + 14, r.y + 42, r.r() - 14, r.y + 42, theme.dim, 1.0f);
        float yy = r.y + 60.0f;
        const float kx = r.x + 20, dx = r.x + 20 + std::min(300.0f, r.w * 0.38f);
        for (const auto& l : lines)
        {
            if (yy > r.b() - 10) break;
            if (!l.first.empty())
            {
                textAt(kx, yy, l.first, kMono, 11.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
                textAt(dx, yy, l.second, kLabel, 12.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
            }
            yy += lh;
        }
    }

    // ---- remembered theme (for new instances) -------------------------------------------------------------------------
    static std::string configPath()
    {
       #if defined(_WIN32)
        const char* base = std::getenv("APPDATA");
        return base ? std::string(base) + "\\wake-pump.cfg" : std::string();
       #elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        return home ? std::string(home) + "/Library/Preferences/wake-pump.cfg" : std::string();
       #else
        const char* home = std::getenv("HOME");
        return home ? std::string(home) + "/.config/wake-pump.cfg" : std::string();
       #endif
    }
    static bool readGlobalTheme()
    {
        const std::string p = configPath();
        if (p.empty()) return false;
        FILE* f = std::fopen(p.c_str(), "rb");
        if (!f) return false;
        char buf[16] = {};
        const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        std::fclose(f);
        return n >= 4 && std::strncmp(buf, "dark", 4) == 0;
    }
    static void writeGlobalTheme(bool dark)
    {
        const std::string p = configPath();
        if (p.empty()) return;
        if (FILE* f = std::fopen(p.c_str(), "wb")) { std::fputs(dark ? "dark" : "light", f); std::fclose(f); }
    }
    static double nowSeconds()
    {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    // ---- data ------------------------------------------------------------------------------------------------------
    const ParamSpec* fSpecs;
    uint32_t fNum;
    const std::vector<Preset>* fPresets;
    Accent fAccent;
    std::string fTag;
    int fBypass;
    std::vector<float> fValues;
    std::vector<double> fLocalSet;   // when this UI last changed each parameter (seconds)
    std::vector<std::unique_ptr<Control>> fControls;
    Control* fActive = nullptr;
    bool fBodyActive = false;
    bool fLaidOut = false;
    bool fDark = false;
    float fScale = 1.0f;
    std::string fStatus, fPresetName, fUiState;
    std::string fAB[2];
    int fABSel = 0;
    FontId fFontLabel = -1, fFontMono = -1, fFontBold = -1;
    uint fLastClickTime = 0;
    float fLastClickX = 0, fLastClickY = 0;
    uint fLastClickButton = 0;
    bool fResizing = false;
    float fResizeStartX = 0, fResizeStartW = 0;
    double fLastTick = 0.0;
    void* fFrameClock = nullptr;   // Windows: the steady frame clock (WinFrameClock.hpp)
    MenuButton* fPresetMenu = nullptr;
    std::vector<PresetRow> fPresetRows;              // what each row of the preset menu is
    std::vector<std::string> fUserPresets;           // your presets (names, sorted), and their files
    std::vector<std::filesystem::path> fUserPresetFiles;
    bool fPresetIsUser = false;                       // fPresetName is one of yours (state "preset" = "user:<name>")
    Segmented* fABButton = nullptr;
    Button* fCopyAB = nullptr;
    Button* fThemeBtn = nullptr;
    Button* fBypassBtn = nullptr;
    struct { bool open = false; Rect r; std::vector<std::string> items; int current = -1, hover = -1; float itemH = 22, scroll = 0; std::function<void(int)> onPick; } fMenu;
    struct { bool active = false; int param = -1; Rect r; std::string text, original; bool selectAll = true; std::vector<std::pair<int, Rect>> ring;
             std::function<void(const std::string&)> onText; } fEdit;
    std::vector<std::string> fUndo[2], fRedo[2];   // per A/B slot
    std::string fGestureBefore;
    int fGestureDepth = 0;
    bool fApplyingHistory = false;
    bool fHelpOpen = false;
    Button* fUndoBtn = nullptr;
    Button* fRedoBtn = nullptr;
    Button* fHelpBtn = nullptr;

    friend struct Dial; friend struct SliderRow; friend struct Button; friend struct Segmented; friend struct MenuButton;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PumpUI)
};

// ---------------------------------------------------------------------------------------------------------------
// Control implementations

inline void Dial::draw(PumpUI& ui)
{
    const Theme& t = ui.theme;
    const bool dis = !enabled;
    const float rad = size == Small ? 15.0f : (size == Large ? 27.0f : 20.0f);
    const float cx = r.cx(), cy = r.y + 16.0f + rad + 2.0f;
    // label
    ui.textAt(cx, r.y + 7.0f, label, PumpUI::kLabel, size == Small ? 10.5f : 11.5f, dis ? t.dim : t.ink2, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 1.0f);
    const float a0 = (float)(pump::kPi * 0.75), a1 = (float)(pump::kPi * 2.25);
    const float n = ui.norm(param);
    // track
    ui.beginPath(); ui.arc(cx, cy, rad, a0, a1, NanoVG::CW); ui.strokeColor(t.dim); ui.strokeWidth(size == Small ? 4.0f : 5.0f); ui.stroke();
    // value arc
    const float from = bipolar ? (float)(pump::kPi * 1.5) : a0;
    const float to = a0 + (a1 - a0) * n;
    if (std::fabs(to - from) > 0.01f)
    {
        ui.beginPath();
        ui.arc(cx, cy, rad, std::min(from, to), std::max(from, to), NanoVG::CW);
        ui.strokeColor(dis ? t.dim : t.neon); ui.strokeWidth(size == Small ? 4.0f : 5.0f); ui.stroke();
    }
    // body: flat disc with ink outline
    ui.beginPath(); ui.circle(cx, cy, rad - 5.0f); ui.fillColor(t.bg); ui.fill();
    ui.strokeColor(dis ? t.dim : t.ink); ui.strokeWidth(1.5f); ui.stroke();
    // pointer
    const float pa = to;
    ui.line(cx + std::cos(pa) * (rad - 14.0f) * 0.25f, cy + std::sin(pa) * (rad - 14.0f) * 0.25f,
            cx + std::cos(pa) * (rad - 6.5f), cy + std::sin(pa) * (rad - 6.5f), dis ? t.dim : t.ink, 2.2f);
    // value
    ui.textAt(cx, cy + rad + 12.0f, ui.formatParam(param, ui.value(param)), PumpUI::kMono, size == Small ? 10.0f : 11.0f, dis ? t.dim : t.ink, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
}
inline bool Dial::mouseDown(PumpUI& ui, float x, float y, int button, uint)
{
    if (button != kMouseButtonLeft) return false;
    dragParam = param;
    dragStartNorm = ui.norm(dragParam); dragStartX = x; dragStartY = y;
    ui.beginEdit(dragParam);
    return true;
}
inline void Dial::mouseDrag(PumpUI& ui, float x, float y, uint mod)
{
    if (dragParam < 0) return;
    const float sens = (mod & kModifierShift) ? 0.0008f : 0.005f;
    const float d = ((x - dragStartX) - (y - dragStartY)) * sens;
    ui.setParamNorm(dragParam, dragStartNorm + d);
    if (mod & kModifierShift) { dragStartNorm = ui.norm(dragParam); dragStartX = x; dragStartY = y; }
}
inline void Dial::mouseUp(PumpUI& ui, float, float) { if (dragParam >= 0) ui.endEdit(dragParam); dragParam = -1; }
inline bool Dial::scroll(PumpUI& ui, float dy, uint mod)
{
    const ParamSpec& s = ui.spec(param);
    const float step = (s.flags & (pump::kPInteger | pump::kPBoolean)) ? 0.0f : ((mod & kModifierShift) ? 0.002f : 0.02f);
    if (step == 0.0f) ui.setParamGesture(param, ui.value(param) + (dy > 0 ? 1.0f : -1.0f));
    else { ui.beginEdit(param); ui.setParamNorm(param, ui.norm(param) + (dy > 0 ? step : -step)); ui.endEdit(param); }
    return true;
}

inline void SliderRow::draw(PumpUI& ui)
{
    const Theme& t = ui.theme;
    const bool dis = !enabled;
    ui.textAt(r.x, r.cy(), label, PumpUI::kLabel, 12.0f, dis ? t.dim : t.ink2, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 0.8f);
    const Rect bar(r.x + labelW, r.cy() - 6.0f, r.w - labelW - valueW - 8.0f, 12.0f);
    ui.fillRect(bar, t.panel2);
    const float n = ui.norm(param);
    const float x0 = bipolar ? bar.x + bar.w * 0.5f : bar.x, x1 = bar.x + bar.w * n;
    ui.fillRect(Rect(std::min(x0, x1), bar.y, std::fabs(x1 - x0), bar.h), dis ? t.dim : t.neon);
    ui.strokeRect(bar, dis ? t.dim : t.ink, 1.2f);
    ui.fillRect(Rect(x1 - 1.5f, bar.y - 3.0f, 3.0f, bar.h + 6.0f), dis ? t.dim : t.ink);
    ui.textAt(r.r(), r.cy(), ui.formatParam(param, ui.value(param)), PumpUI::kMono, 11.5f, dis ? t.dim : t.ink, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
}
inline bool SliderRow::mouseDown(PumpUI& ui, float x, float, int button, uint)
{
    if (button != kMouseButtonLeft) return false;
    dragParam = param;
    dragStartNorm = ui.norm(dragParam); dragStartX = x;
    ui.beginEdit(dragParam);
    return true;
}
inline void SliderRow::mouseDrag(PumpUI& ui, float x, float, uint mod)
{
    if (dragParam < 0) return;
    const float barW = std::max(40.0f, r.w - labelW - valueW - 8.0f);
    const float d = (x - dragStartX) / barW * ((mod & kModifierShift) ? 0.15f : 1.0f);
    ui.setParamNorm(dragParam, dragStartNorm + d);
    if (mod & kModifierShift) { dragStartNorm = ui.norm(dragParam); dragStartX = x; }
}
inline void SliderRow::mouseUp(PumpUI& ui, float, float) { if (dragParam >= 0) ui.endEdit(dragParam); dragParam = -1; }
inline bool SliderRow::scroll(PumpUI& ui, float dy, uint mod)
{
    const ParamSpec& s = ui.spec(param);
    if (s.flags & (pump::kPInteger | pump::kPBoolean)) { ui.setParamGesture(param, ui.value(param) + (dy > 0 ? 1.0f : -1.0f)); return true; }
    const float step = (mod & kModifierShift) ? 0.002f : 0.02f;
    ui.beginEdit(param); ui.setParamNorm(param, ui.norm(param) + (dy > 0 ? step : -step)); ui.endEdit(param);
    return true;
}

inline bool Button::isOn(PumpUI& ui) const
{
    if (isOnFn) return isOnFn();
    if (param >= 0) return ui.value(param) >= 0.5f;
    return false;
}
inline void Button::draw(PumpUI& ui)
{
    const Theme& t = ui.theme;
    const bool on = isOn(ui);
    ui.keyBox(r, on, pressed || on, nullptr, small ? 3.0f : 4.0f);
    ui.textAt(r.cx(), r.cy() + 0.5f, label, PumpUI::kLabel, small ? 11.5f : 12.5f, on ? t.on : (enabled ? t.ink : t.dim), NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 1.0f);
}
inline bool Button::mouseDown(PumpUI& ui, float, float, int button, uint mod)
{
    if (button != kMouseButtonLeft) return false;
    pressed = true;
    pressTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    exclusivePress = false;
    if (onExclusive && cmdKey(mod)) { exclusivePress = true; onExclusive(); return true; }
    if (param >= 0) ui.setParamGesture(param, ui.value(param) >= 0.5f ? ui.spec(param).min : ui.spec(param).max);
    else if (onClick) onClick();
    return true;
}
inline void Button::mouseUp(PumpUI& ui, float, float)
{
    pressed = false;
    if (!holdMomentary || exclusivePress) return;   // a Ctrl/Cmd-click never toggles, however long it is held
    const double held = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - pressTime;
    if (held < 0.3) return;
    // released after a hold: undo the toggle (momentary)
    if (param >= 0) ui.setParamGesture(param, ui.value(param) >= 0.5f ? ui.spec(param).min : ui.spec(param).max);
    else if (onClick) onClick();
}

inline int Segmented::count(PumpUI& ui) const
{
    if (!options.empty()) return (int)options.size();
    return param >= 0 ? ui.spec(param).numChoices() : 0;
}
inline int Segmented::selected(PumpUI& ui) const
{
    if (getFn) return getFn();
    return param >= 0 ? (int)std::lround(ui.value(param) - ui.spec(param).min) : -1;
}
inline std::string Segmented::optionText(PumpUI& ui, int i) const
{
    if (!options.empty()) return options[(size_t)i];
    return ui.spec(param).choices ? ui.spec(param).choices[i] : std::to_string(i);
}
inline void Segmented::draw(PumpUI& ui)
{
    const Theme& t = ui.theme;
    const int n = count(ui);
    if (n <= 0) return;
    const int sel = selected(ui);
    // offset block behind the whole row
    ui.fillRect(Rect(r.x + 3, r.y + 3, r.w, r.h), t.bg);
    ui.strokeRect(Rect(r.x + 3, r.y + 3, r.w, r.h), t.dim, 1.5f);
    for (int i = 0; i < n; ++i)
    {
        Rect c = vertical ? Rect(r.x, r.y + r.h * i / n, r.w, r.h / n) : Rect(r.x + r.w * i / n, r.y, r.w / n, r.h);
        const bool on = i == sel;
        ui.fillRect(c, on ? t.ink : t.bg);
        ui.strokeRect(c, t.ink, 1.5f);
        ui.textAt(c.cx(), c.cy() + 0.5f, optionText(ui, i), PumpUI::kLabel, 11.5f, on ? t.on : (enabled ? t.ink : t.dim), NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 0.8f);
    }
}
inline bool Segmented::mouseDown(PumpUI& ui, float x, float y, int button, uint)
{
    if (button != kMouseButtonLeft) return false;
    const int n = count(ui);
    if (n <= 0) return true;
    const int i = std::min(n - 1, std::max(0, vertical ? (int)((y - r.y) / r.h * n) : (int)((x - r.x) / r.w * n)));
    if (setFn) setFn(i);
    else if (param >= 0) ui.setParamGesture(param, ui.spec(param).min + (float)i);
    return true;
}

inline std::vector<std::string> MenuButton::items(PumpUI& ui) const
{
    if (itemsFn) return itemsFn();
    std::vector<std::string> v;
    if (param >= 0 && ui.spec(param).choices)
        for (int i = 0; i < ui.spec(param).numChoices(); ++i) v.push_back(ui.spec(param).choices[i]);
    return v;
}
inline int MenuButton::current(PumpUI& ui) const
{
    if (getFn) return getFn();
    return param >= 0 ? (int)std::lround(ui.value(param) - ui.spec(param).min) : -1;
}
// The next row from `from` in direction `dir` that ◀ ▶ / the wheel may land on (skips headings, separators and
// actions); wraps around when `wrap`. Returns -1 if there is none, `from` if it can't move without wrapping.
inline int MenuButton::stepRow(const std::vector<std::string>& it, int from, int dir, bool wrap)
{
    const int n = (int)it.size();
    if (n == 0) return -1;
    int i = from < 0 ? (dir > 0 ? -1 : n) : from;
    for (int k = 0; k < n; ++k)
    {
        i += dir;
        if (i < 0 || i >= n)
        {
            if (!wrap) return from;
            i = i < 0 ? n - 1 : 0;
        }
        if (PumpUI::menuRowSteppable(it[(size_t)i])) return i;
    }
    return -1;
}
inline void MenuButton::choose(PumpUI& ui, int i)
{
    if (setFn) setFn(i);
    else if (param >= 0) ui.setParamGesture(param, ui.spec(param).min + (float)i);
}
inline void MenuButton::draw(PumpUI& ui)
{
    const Theme& t = ui.theme;
    ui.keyBox(r, false, false, nullptr, 3.0f);
    std::string s = textFn ? textFn() : (param >= 0 ? ui.formatParam(param, ui.value(param)) : label);
    const float pad = arrows ? 26.0f : 10.0f;
    if (!label.empty() && param >= 0)
        ui.textAt(r.x + pad, r.cy(), label, PumpUI::kLabel, 10.5f, t.ink2, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 1.0f);
    const float lw = (!label.empty() && param >= 0) ? ui.textWidth(label, PumpUI::kLabel, 10.5f, 1.0f) + 8.0f : 0.0f;
    ui.save();
    ui.scissor(r.x + pad + lw, r.y, r.w - 2 * pad - lw - 12.0f, r.h);
    ui.textAt(r.x + pad + lw, r.cy() + 0.5f, s, PumpUI::kLabel, 12.5f, enabled ? t.ink : t.dim, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 0.6f);
    ui.restore();
    ui.textAt(r.r() - pad, r.cy(), "▾", PumpUI::kMono, 12.0f, t.ink, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
    if (arrows)
    {
        ui.line(r.x + 20.0f, r.y + 4, r.x + 20.0f, r.b() - 4, t.dim, 1.0f);
        ui.line(r.r() - 20.0f, r.y + 4, r.r() - 20.0f, r.b() - 4, t.dim, 1.0f);
        ui.textAt(r.x + 10.0f, r.cy(), "◀", PumpUI::kMono, 10.0f, t.ink, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
        ui.textAt(r.r() - 10.0f, r.cy(), "▶", PumpUI::kMono, 10.0f, t.ink, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
    }
}
inline bool MenuButton::mouseDown(PumpUI& ui, float x, float, int button, uint)
{
    if (button != kMouseButtonLeft && button != kMouseButtonRight) return false;
    const std::vector<std::string> it = items(ui);
    if (it.empty()) return true;
    if (arrows && x < r.x + 20.0f) { const int t = stepRow(it, current(ui), -1, true); if (t >= 0) choose(ui, t); return true; }
    if (arrows && x > r.r() - 20.0f) { const int t = stepRow(it, current(ui), +1, true); if (t >= 0) choose(ui, t); return true; }
    ui.openMenu(r, it, current(ui), [this, &ui](int i) { choose(ui, i); });
    return true;
}
inline bool MenuButton::scroll(PumpUI& ui, float dy, uint)
{
    const std::vector<std::string> it = items(ui);
    if (it.empty()) return true;
    const int c = current(ui), t = stepRow(it, c, dy > 0 ? -1 : 1, false);
    if (t >= 0 && t != c) choose(ui, t);
    return true;
}

END_NAMESPACE_DISTRHO
