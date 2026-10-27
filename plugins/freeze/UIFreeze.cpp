// pump FREEZE: interface. SOUND: the sound's spectra over time (low at the bottom, log frequency; brighter = louder),
// its waveform under it, POSITION and SPRAY's span, and where every voice is now (drag to set POSITION). NOW: the
// newest voice's spectrum. Below: the source (which sound, LOAD, position, speed, spray, grains, blur), the spectrum
// (root, tracking, stretch, tilt, shimmer, low and high cuts, phase, width), the voice, and a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginFreeze.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::freeze;

class UIFreeze : public PumpUI {
public:
    UIFreeze() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::freeze(), "FREEZE", 1000, 716, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Medium, bi); if (tip) d->tip = tip; return d; };
        srcM = addMenu(kSource, "SOURCE"); srcM->arrows = true;
        loadB = addButton("LOAD", [this] { FileBrowserOptions op; op.title = "Load a sound to freeze (.wav, up to 20 s)"; openFileBrowser(op); });
        loadB->tip = "Load a .wav of your own (then SOURCE: File)";
        posD = dial(kPosition, "POSITION", false, "Where in the sound the voices start (drag on the picture)");
        speedD = dial(kSpeed, "SPEED", true, "How fast the voices move on through it (0: frozen; 100: as recorded; minus: backwards)");
        sprayD = dial(kSpray, "SPRAY", false, "How far around the position the grains are picked");
        grainsD = dial(kGrains, "GRAINS", false, "How many spectra each hop averages: more is smoother");
        blurD = dial(kBlur, "BLUR", false, "Smears each hop into the last: a slower, softer cloud");
        rootD = dial(kRoot, "ROOT", false, "Your own file's note (the built-in sounds know theirs)");
        trackD = dial(kTrack, "TRACK", false, "How much the notes you play move its pitch (0: it stays as it is)");
        stretchD = dial(kStretch, "STRETCH", true, "Spreads (+) or squeezes (-) its partials about the root: inharmonic");
        tiltD = dial(kTilt, "TILT", true); shimD = dial(kShimmer, "SHIMMER", false, "Each bin's level jitters hop to hop: sparkle");
        lowD = dial(kLow, "LOW"); highD = dial(kHigh, "HIGH");
        phaseD = dial(kPhase, "PHASE", false, "0: fresh random phases every hop (the frozen wash) · 100: a phase vocoder's steady, tonal phases");
        widthD = dial(kWidth, "WIDTH");
        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) envD[k] = dial(kAttack + k, adsr[k]);
        velD = dial(kVelocity, "VELOCITY"); voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE");
        octD = dial(kOctave, "OCTAVE", true); volD = dial(kVolume, "VOLUME", true);
        vmodeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginFreeze*>(pluginPtr())) plug = p;
    }
    ~UIFreeze() override { keyboard.mouseUp(); }

protected:
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename) return;
        setState("file", filename);
        setParamGesture(kSource, 0.0f);
    }
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kSpeed && std::fabs(v) < 0.5f) return "FROZEN";
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kRoot)
        {
            static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int n = (int)std::lround(v); std::snprintf(b, sizeof b, "%s%d", nn[((n % 12) + 12) % 12], n / 12 - 1); return b;
        }
        if (i == kSource) { std::string s = kSourceNames[pump::clampv((int)v, 0, kNumSources - 1)]; for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        // top: sound, now, meters
        const float top = 58, sh = 190, hm = Dial::heightFor(Dial::Medium), ph = 112;
        soundR = Rect(14, top, 700, sh);
        nowR = Rect(726, top, 234, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);
        // spread n dials across w px from x, 28 below the panel's top
        auto rowOf = [&](float x, float y, float w, Dial** d, int n) {
            const float cw = w / n;
            for (int k = 0; k < n; ++k) d[k]->r = Rect(x + k * cw, y + 28, cw, hm);
        };
        // SOURCE · GRAINS (source, load, five dials) | ENVELOPE
        const float y1 = top + sh + 12;
        srcPanel = Rect(14, y1, 652, ph);
        srcM->r = Rect(26, y1 + 32, 220, 24); loadB->r = Rect(26, y1 + 66, 220, 24);
        Dial* sd[5] = { posD, speedD, sprayD, grainsD, blurD }; rowOf(256, y1, 400, sd, 5);
        envPanel = Rect(678, y1, 308, ph);
        rowOf(688, y1, 288, envD, 4);
        // SPECTRUM: nine dials
        const float y2 = y1 + ph + 12;
        specPanel = Rect(14, y2, 972, ph);
        Dial* pd[9] = { rootD, trackD, stretchD, tiltD, shimD, lowD, highD, phaseD, widthD }; rowOf(24, y2, 952, pd, 9);
        // VOICE: five dials, the mode beside them
        const float y3 = y2 + ph + 12;
        voicePanel = Rect(14, y3, 972, ph);
        Dial* vd[5] = { velD, voicesD, glideD, octD, volD }; rowOf(24, y3, 640, vd, 5);
        vmodeSeg->r = Rect(690, y3 + ph * 0.5f - 12, 284, 24);
        keyboard.r = Rect(14, y3 + ph + 12, 966, 50);
    }

    void tick(float dt) override
    {
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        rootD->enabled = (int)value(kSource) == 0;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        for (int k = 0; k < 256; ++k) spec[k] = plug->engine.spectrumAt(k);
        for (int v = 0; v < Engine::kMaxVoices; ++v) vpos[v] = plug->engine.positionOf(v);
        playing = plug->engine.voicesNow() > 0;
    }

    // the picture of a sound's spectra: columns = time, rows = log frequency (20 Hz .. 20 kHz, bottom up)
    void buildImage(const Spectra* S)
    {
        imgOf = S;
        pix.assign((size_t)kImgW * kImgH * 4, 0);
        if (!S) return;
        const Color hi = theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.65f), lo = theme.panel2;
        const double ref = std::max(1e-9f, S->maxMag), binHz = S->sr / kN;
        for (int x = 0; x < kImgW; ++x)
        {
            const float* m = S->frame((int)((int64_t)x * S->frames / kImgW));
            for (int y = 0; y < kImgH; ++y)
            {
                const double f = 20.0 * std::pow(1000.0, (kImgH - 1 - y) / (kImgH - 1.0));
                const int k = pump::clampv((int)(f / binHz), 1, kBins - 2);
                const double db = 20.0 * std::log10(m[k] / ref + 1e-9);
                const float v = pump::clampv((float)((db + 70.0) / 70.0), 0.0f, 1.0f);
                const Color c = pump::mixColor(lo, hi, theme.dark ? v * v : std::sqrt(v) * v);
                uint8_t* p = &pix[((size_t)y * kImgW + (size_t)x) * 4];
                p[0] = (uint8_t)(c.red * 255); p[1] = (uint8_t)(c.green * 255); p[2] = (uint8_t)(c.blue * 255); p[3] = 255;
            }
        }
        imgDirty = true; imgDark = theme.dark;
    }
    Rect picOf() const { return Rect(soundR.x + 10, soundR.y + 24, soundR.w - 20, soundR.h - 64); }
    void drawSound()
    {
        const Rect& r = soundR;
        screenBox(r);
        const Spectra* S = plug ? plug->soundNow() : nullptr;
        if (S != imgOf || imgDark != theme.dark) buildImage(S);
        char b[128];
        if (S) std::snprintf(b, sizeof b, "%s  ·  %.1f s", S->name.c_str(), S->seconds);
        else if ((int)value(kSource) == 0) std::snprintf(b, sizeof b, "%s", plug && !plug->fileInfo().empty() ? plug->fileInfo().c_str() : "NO FILE: LOAD ONE");
        else std::snprintf(b, sizeof b, "MAKING THE SOUNDS...");
        caption(r.x + 10, r.y + 14, "SOUND");
        std::string up = b; for (char& c : up) c = (char)std::toupper((unsigned char)c);
        textAt(r.r() - 10, r.y + 14, up, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const Rect p = picOf();
        const Rect wv(r.x + 10, p.b() + 6, r.w - 20, 28);
        if (S && !pix.empty())
        {
            if (!img.isValid()) img = createImageFromRGBA((uint)kImgW, (uint)kImgH, pix.data(), 0);
            else if (imgDirty) img.update(pix.data());
            imgDirty = false;
            if (img.isValid()) { const Paint pt = imagePattern(p.x, p.y, p.w, p.h, 0.0f, img, 1.0f); beginPath(); rect(p.x, p.y, p.w, p.h); fillPaint(pt); fill(); }
            beginPath();
            for (int i = 0; i < 1024; ++i) { const float x = wv.x + wv.w * i / 1023.0f, h = std::min(1.0f, S->overview[(size_t)i] * 1.3f) * wv.h * 0.5f; if (!i) moveTo(x, wv.cy() - h); else lineTo(x, wv.cy() - h); }
            for (int i = 1023; i >= 0; --i) { const float x = wv.x + wv.w * i / 1023.0f, h = std::min(1.0f, S->overview[(size_t)i] * 1.3f) * wv.h * 0.5f; lineTo(x, wv.cy() + h); }
            closePath(); fillColor(withAlpha2(theme.ink, 0.45f)); fill();
        }
        strokeRect(p, theme.dim, 1.0f);
        // POSITION, and SPRAY's span around it
        const float px = p.x + p.w * value(kPosition) * 0.01f, sp = p.w * value(kSpray) * 0.01f * 0.25f;
        if (sp > 1.0f) fillRect(Rect(px - sp, p.y, 2 * sp, wv.b() - p.y), withAlpha2(theme.ink, 0.12f));
        line(px, p.y, px, wv.b(), theme.ink, 2.0f);
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.5f);
        for (int v = 0; v < Engine::kMaxVoices; ++v) if (vpos[v] >= 0.0f) { const float x = p.x + p.w * vpos[v]; line(x, p.y, x, p.b(), c, 1.5f); }
        textAt(p.x + 4, p.y + 8, "20k", kMono, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(p.x + 4, p.b() - 8, "20", kMono, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    void drawNow()
    {
        const Rect& r = nowR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "NOW");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 36);
        for (float f : { 100.0f, 1000.0f, 10000.0f }) { const float x = p.x + p.w * std::log(f / 20.0f) / std::log(1000.0f); line(x, p.y, x, p.b(), theme.grid, 1.0f); }
        if (!playing) { textAt(p.cx(), p.cy(), "PLAY A NOTE", kLabel, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f); return; }
        beginPath();
        moveTo(p.x, p.b());
        for (int k = 0; k < 256; ++k) lineTo(p.x + p.w * k / 255.0f, p.b() - spec[k] * p.h);
        lineTo(p.r(), p.b()); closePath();
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.4f);
        fillColor(withAlpha2(c, 0.5f)); fill();
        strokeColor(c); strokeWidth(1.4f); stroke();
    }

    void drawBody() override
    {
        drawSound(); drawNow();
        drawOutMeter(outMeter, meterL, meterR);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(srcPanel, "SOURCE · GRAINS"); panel(envPanel, "ENVELOPE"); panel(specPanel, "SPECTRUM"); panel(voicePanel, "VOICE");
        keyboard.draw(*this);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        const Rect p = picOf();
        if (Rect(p.x, p.y, p.w, soundR.b() - p.y).contains(x, y)) { dragging = true; beginEdit(kPosition); setParam(kPosition, pump::clampv((x - p.x) / p.w, 0.0f, 1.0f) * 100.0f); return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (dragging) { const Rect p = picOf(); setParam(kPosition, pump::clampv((x - p.x) / p.w, 0.0f, 1.0f) * 100.0f); }
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; }
        if (dragging) { endEdit(kPosition); dragging = false; }
    }
    void bodyHover(float x, float y) override
    {
        if (soundR.contains(x, y)) setStatus("The sound's spectra over time. Drag: POSITION");
        else if (nowR.contains(x, y)) setStatus("The newest voice's spectrum");
        else if (keyboard.r.contains(x, y)) setStatus("Play FREEZE");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "LOAD / SOURCE", "a .wav of your own, or one of seven built in" }, { "SOUND", "drag: where the voices freeze it" },
                 { "SPEED", "0 frozen; move through it, either way" }, { "PHASE", "0 the classic wash; 100 smooth and tonal" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d VOICES", std::fabs(value(kSpeed)) < 0.5f ? "FROZEN" : "MOVING", plug ? plug->engine.voicesNow() : 0);
        return b;
    }

private:
    static constexpr int kImgW = 512, kImgH = 128;
    PluginFreeze* plug = nullptr;
    Rect soundR, nowR, meterL, meterR, srcPanel, envPanel, specPanel, voicePanel;
    MenuButton* srcM;
    Button* loadB;
    Segmented* vmodeSeg;
    Dial *posD, *speedD, *sprayD, *grainsD, *blurD, *rootD, *trackD, *stretchD, *tiltD, *shimD, *lowD, *highD, *phaseD, *widthD;
    Dial *envD[4], *velD, *voicesD, *glideD, *octD, *volD;
    KeyboardUI keyboard;
    bool kbDown = false, dragging = false, playing = false, imgDirty = false, imgDark = false;
    const Spectra* imgOf = nullptr;
    std::vector<uint8_t> pix;
    NanoImage img;
    float spec[256] = {}, vpos[Engine::kMaxVoices] = {};
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIFreeze)
};

UI* createUI() { return new UIFreeze(); }

END_NAMESPACE_DISTRHO
