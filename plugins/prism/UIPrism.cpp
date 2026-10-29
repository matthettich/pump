// pump PRISM: interface. SOUND: the analysed sound as a picture of its partials over time (partial 1 at the bottom,
// brighter = louder), its waveform under it, the START / END region and where every voice is now; PARTIALS NOW: the
// levels the newest voice plays. Below: the sources (A, B, LOAD, MORPH), how it plays through them (mode, speed,
// start, end, scan, follow), the spectrum (partials, tilt, odd / even, inharmonic, own detuning, formants, blur,
// noise, width), the voice, and a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginPrism.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::prism;

class UIPrism : public PumpUI {
public:
    UIPrism() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::prism(), "PRISM", 1000, 728, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Medium, bi); if (tip) d->tip = tip; return d; };
        srcA = addMenu(kSourceA, "A"); srcA->arrows = true;
        srcB = addMenu(kSourceB, "B"); srcB->arrows = true;
        loadA = addButton("LOAD A", [this] { which = 0; FileBrowserOptions op; op.title = "Load a sound to resynthesise (.wav, up to 20 s)"; openFileBrowser(op); });
        loadB = addButton("LOAD B", [this] { which = 1; FileBrowserOptions op; op.title = "Load a second sound to morph to (.wav, up to 20 s)"; openFileBrowser(op); });
        loadA->tip = "Load a .wav as source A (then SOURCE A: File A)";
        loadB->tip = "Load a .wav as source B (then SOURCE B: File B)";
        morphD = dial(kMorph, "MORPH", false, "From sound A (0) to sound B (100), partial by partial"); morphD->size = Dial::Large;
        modeSeg = addSegmented(kMode, { "LOOP", "ONE-SHOT", "BOUNCE", "FREEZE" });
        modeSeg->tip = "Loop START-END · play once and hold the last moment · back and forth · stand still at SCAN";
        speedD = dial(kSpeed, "SPEED", false, "How fast it moves through the sound (100 % = as recorded, 0 = still)");
        startD = dial(kStart, "START"); endD = dial(kEnd, "END");
        scanD = dial(kScan, "SCAN", false, "FREEZE: the moment it stands at");
        followD = dial(kFollow, "FOLLOW", false, "Keep the sound's own pitch movement (its melody, its vibrato) on top of the note");
        partsD = dial(kPartials, "PARTIALS");
        tiltD = dial(kTilt, "TILT", true, "Brighter (+) or darker (-), keeping the level");
        oddD = dial(kOddEven, "ODD/EVEN", true, "+ takes out the even partials, - the odd ones");
        inharmD = dial(kInharm, "INHARM", false, "Bends the partials sharp by a power law: bells, glass");
        ownD = dial(kTrue, "OWN", false, "The sound's own detuning of each partial (0: a perfect harmonic series)");
        formD = dial(kFormant, "FORMANT", false, "100: its formants stay where they were (a voice stays a voice when played higher)");
        blurD = dial(kBlur, "BLUR", false, "Smooths every partial over time: a wash");
        noiseD = dial(kNoise, "NOISE", false, "The sound's noise (breath, bow, hiss) between the partials");
        widthD = dial(kWidth, "WIDTH");
        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) envD[k] = dial(kAttack + k, adsr[k]);
        velD = dial(kVelocity, "VELOCITY"); voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE");
        octD = dial(kOctave, "OCTAVE", true); volD = dial(kVolume, "VOLUME", true);
        vmodeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginPrism*>(pluginPtr())) plug = p;
    }
    ~UIPrism() override { keyboard.mouseUp(); }

protected:
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename) return;
        setState(which == 0 ? "file_a" : "file_b", filename);
        if (which == 0) setParamGesture(kSourceA, 0.0f); else setParamGesture(kSourceB, 1.0f);
    }
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kSpeed && v < 0.5f) return "STILL";
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kSourceA || i == kSourceB)
        {
            std::string s = (i == kSourceA ? kSourceNames : kSourceBNames)[pump::clampv((int)v, 0, i == kSourceA ? kNumSources - 1 : kNumSources)];
            for (char& c : s) c = (char)std::toupper((unsigned char)c);
            return s;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, sh = 168, hm = Dial::heightFor(Dial::Medium), hl = Dial::heightFor(Dial::Large);
        soundR = Rect(14, top, 744, sh);
        partsR = Rect(770, top, 190, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);

        // SOURCES (A, B, LOAD, MORPH) beside PLAY (mode + five dials)
        const float y1 = top + sh + 12, ph = 140;
        srcPanel = Rect(14, y1, 476, ph);
        srcA->r = Rect(26, y1 + 44, 260, 24); loadA->r = Rect(294, y1 + 44, 90, 24);
        srcB->r = Rect(26, y1 + 84, 260, 24); loadB->r = Rect(294, y1 + 84, 90, 24);
        morphD->r = Rect(392, y1 + 30, 90, hl);
        playPanel = Rect(502, y1, 484, ph);
        modeSeg->r = Rect(514, y1 + 28, 460, 24);
        Dial* pd[5] = { speedD, startD, endD, scanD, followD };
        for (int k = 0; k < 5; ++k) pd[k]->r = Rect(512 + k * 92.8f, y1 + 56, 92.8f, hm);

        // SPECTRUM: nine dials spread across the strip
        const float y2 = y1 + ph + 12, ph2 = 112;
        specPanel = Rect(14, y2, 972, ph2);
        Dial* sd[9] = { partsD, tiltD, oddD, inharmD, ownD, formD, blurD, noiseD, widthD };
        for (int k = 0; k < 9; ++k) sd[k]->r = Rect(24 + k * 105.8f, y2 + 28, 105.8f, hm);

        // VOICE: envelope, play dials, voice mode
        const float y3 = y2 + ph2 + 12;
        voicePanel = Rect(14, y3, 972, ph2);
        for (int k = 0; k < 4; ++k) envD[k]->r = Rect(22 + k * 76, y3 + 28, 76, hm);
        Dial* vd[5] = { velD, voicesD, glideD, octD, volD };
        for (int k = 0; k < 5; ++k) vd[k]->r = Rect(330 + k * 88, y3 + 28, 88, hm);
        vmodeSeg->r = Rect(780, y3 + 56, 194, 24);
        keyboard.r = Rect(14, y3 + ph2 + 12, 966, 56);
    }

    void tick(float dt) override
    {
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        scanD->enabled = (int)value(kMode) == kFreeze;
        speedD->enabled = (int)value(kMode) != kFreeze;
        morphD->enabled = (int)value(kSourceB) != 0;
        if (!plug) return;
        overlay.capture(plug->engine.outRing, dt);   // the output over the sound in violet
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        for (int k = 0; k < 64; ++k) now[k] = std::max(plug->engine.partialLevel(k + 1), now[k] * std::exp(-dt * 8.0f));
        pos = plug->engine.positionNow();
    }

    // the picture of a sound's partials: columns = time, rows = partials 1..64 (bottom up), log level
    void buildImage(const Analysis* A)
    {
        imgOf = A;
        pix.assign((size_t)kImgW * kImgH * 4, 0);
        if (!A) return;
        const Color hi = theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.65f), lo = theme.panel2;
        const double ref = std::max(1e-6f, A->maxAmp) * 0.6;
        for (int x = 0; x < kImgW; ++x)
        {
            const int f = std::min(A->frames - 1, (int)((int64_t)x * A->frames / kImgW));
            for (int y = 0; y < kImgH; ++y)
            {
                const int k = kImgH - 1 - y;   // partial k + 1
                const double a = A->amp[(size_t)f * kK + k];
                const double db = 20.0 * std::log10(a / ref + 1e-9);
                const float v = pump::clampv((float)((db + 60.0) / 60.0), 0.0f, 1.0f);
                const Color c = pump::mixColor(lo, hi, theme.dark ? v * v : std::sqrt(v) * v);
                uint8_t* p = &pix[((size_t)y * kImgW + (size_t)x) * 4];
                p[0] = (uint8_t)(c.red * 255); p[1] = (uint8_t)(c.green * 255); p[2] = (uint8_t)(c.blue * 255); p[3] = 255;
            }
        }
        imgDirty = true;
        imgDark = theme.dark;
    }
    void drawSound()
    {
        const Rect& r = soundR;
        screenBox(r);
        const Analysis* A = plug ? plug->soundA() : nullptr;
        if (A != imgOf || imgDark != theme.dark) buildImage(A);
        char b[128];
        if (A) std::snprintf(b, sizeof b, "%s  ·  %.1f s  ·  %.1f Hz", A->name.c_str(), A->seconds, A->f0med);
        else if ((int)value(kSourceA) == 0) std::snprintf(b, sizeof b, "%s", plug && !plug->fileInfo(0).empty() ? plug->fileInfo(0).c_str() : "NO FILE: LOAD A");
        else std::snprintf(b, sizeof b, "ANALYSING...");
        caption(r.x + 10, r.y + 14, "SOUND A");
        std::string up = b; for (char& c : up) c = (char)std::toupper((unsigned char)c);
        textAt(r.r() - 10, r.y + 14, up, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const Rect p(r.x + 10, r.y + 24, r.w - 20, r.h - 64);
        const Rect wv(r.x + 10, p.b() + 6, r.w - 20, 28);
        if (A && !pix.empty())
        {
            if (!img.isValid()) img = createImageFromRGBA((uint)kImgW, (uint)kImgH, pix.data(), 0);
            else if (imgDirty) img.update(pix.data());
            imgDirty = false;
            if (img.isValid()) { const Paint pt = imagePattern(p.x, p.y, p.w, p.h, 0.0f, img, 1.0f); beginPath(); rect(p.x, p.y, p.w, p.h); fillPaint(pt); fill(); }
            // the waveform
            beginPath();
            for (int i = 0; i < 1024; ++i)
            {
                const float x = wv.x + wv.w * i / 1023.0f, h = std::min(1.0f, A->overview[(size_t)i] * 1.3f) * wv.h * 0.5f;
                if (i == 0) moveTo(x, wv.cy() - h); else lineTo(x, wv.cy() - h);
            }
            for (int i = 1023; i >= 0; --i) { const float x = wv.x + wv.w * i / 1023.0f, h = std::min(1.0f, A->overview[(size_t)i] * 1.3f) * wv.h * 0.5f; lineTo(x, wv.cy() + h); }
            closePath(); fillColor(withAlpha2(theme.ink, 0.45f)); fill();
        }
        strokeRect(p, theme.dim, 1.0f);
        // START / END (shading outside), SCAN in freeze, the playhead
        const int mode = (int)value(kMode);
        const float s0 = std::min(value(kStart), value(kEnd)) * 0.01f, s1 = std::max(value(kStart), value(kEnd)) * 0.01f;
        if (mode != kFreeze)
        {
            fillRect(Rect(p.x, p.y, p.w * s0, wv.b() - p.y), withAlpha2(theme.bg, 0.6f));
            fillRect(Rect(p.x + p.w * s1, p.y, p.w * (1.0f - s1), wv.b() - p.y), withAlpha2(theme.bg, 0.6f));
            line(p.x + p.w * s0, p.y, p.x + p.w * s0, wv.b(), theme.ink, 1.5f);
            line(p.x + p.w * s1, p.y, p.x + p.w * s1, wv.b(), theme.ink, 1.5f);
        }
        else { const float x = p.x + p.w * value(kScan) * 0.01f; line(x, p.y, x, wv.b(), theme.ink, 2.0f); }
        if (pos >= 0.0f) { const float x = p.x + p.w * pos; line(x, p.y, x, wv.b(), pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.5f), 2.0f); }
        textAt(p.x + 4, p.y + 8, "64", kMono, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(p.x + 4, p.b() - 8, "1", kMono, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    void drawNow()
    {
        const Rect& r = partsR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "PARTIALS NOW");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 34);
        float mx = 1e-5f; for (float v : now) mx = std::max(mx, v);
        const float bw = p.w / 64.0f;
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.35f);
        for (int k = 0; k < 64; ++k)
        {
            const float db = 20.0f * std::log10(now[k] / mx + 1e-9f), h = pump::clampv((db + 60.0f) / 60.0f, 0.0f, 1.0f) * p.h;
            if (h > 0.5f) fillRect(Rect(p.x + k * bw, p.b() - h, std::max(1.0f, bw - 1.0f), h), c);
        }
    }

    void drawBody() override
    {
        drawSound();
        drawWaveOverlay(overlay, Rect(soundR.x + 10, soundR.y + 24, soundR.w - 20, soundR.h - 64));
        drawNow();
        drawOutMeter(outMeter, meterL, meterR);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(srcPanel, "SOURCES"); panel(playPanel, "PLAY"); panel(specPanel, "SPECTRUM"); panel(voicePanel, "VOICE");
        if (plug)
        {
            const std::string fa = plug->fileInfo(0), fb = plug->fileInfo(1);
            std::string t = fa.empty() ? "" : "A: " + fa; if (!fb.empty()) t += (t.empty() ? "" : "   ") + std::string("B: ") + fb;
            for (char& c : t) c = (char)std::toupper((unsigned char)c);
            textAt(srcPanel.r() - 12, srcPanel.y + 16, t, kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.4f);
        }
        keyboard.draw(*this);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        // the sound screen: drag to set SCAN (freeze) or the nearer of START / END
        const Rect p(soundR.x + 10, soundR.y + 24, soundR.w - 20, soundR.h - 30);
        if (p.contains(x, y))
        {
            const float u = pump::clampv((x - p.x) / p.w, 0.0f, 1.0f) * 100.0f;
            if ((int)value(kMode) == kFreeze) dragParam = kScan;
            else dragParam = std::fabs(u - value(kStart)) <= std::fabs(u - value(kEnd)) ? kStart : kEnd;
            beginEdit(dragParam); setParam(dragParam, u);
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (dragParam >= 0) { const Rect p(soundR.x + 10, soundR.y + 24, soundR.w - 20, soundR.h - 30); setParam(dragParam, pump::clampv((x - p.x) / p.w, 0.0f, 1.0f) * 100.0f); }
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; }
        if (dragParam >= 0) { endEdit(dragParam); dragParam = -1; }
    }
    void bodyHover(float x, float y) override
    {
        if (soundR.contains(x, y)) setStatus("The sound's partials over time (1 at the bottom). Drag: START / END (FREEZE: SCAN)");
        else if (partsR.contains(x, y)) setStatus("The partials the newest voice plays");
        else if (keyboard.r.contains(x, y)) setStatus("Play PRISM");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "LOAD A / B", "a .wav of your own (up to 20 s), analysed into partials" }, { "SOUND", "drag: START / END, or SCAN when frozen" },
                 { "FORMANT", "100: the formants stay put when you play higher or lower" }, { "MORPH", "partial by partial, A to B" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d VOICES", kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)], plug ? plug->engine.voicesNow() : 0);
        std::string s = b; for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kImgW = 512, kImgH = 64;
    PluginPrism* plug = nullptr;
    WaveOverlay overlay;   // the output over the sound, a thin line in the second colour
    Rect soundR, partsR, meterL, meterR, srcPanel, playPanel, specPanel, voicePanel;
    MenuButton *srcA, *srcB;
    Button *loadA, *loadB;
    Segmented *modeSeg, *vmodeSeg;
    Dial *morphD, *speedD, *startD, *endD, *scanD, *followD, *partsD, *tiltD, *oddD, *inharmD, *ownD, *formD, *blurD, *noiseD, *widthD;
    Dial *envD[4], *velD, *voicesD, *glideD, *octD, *volD;
    KeyboardUI keyboard;
    bool kbDown = false, imgDirty = false, imgDark = false;
    int which = 0, dragParam = -1;
    const Analysis* imgOf = nullptr;
    std::vector<uint8_t> pix;
    NanoImage img;
    float now[64] = {}, pos = -1.0f;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIPrism)
};

UI* createUI() { return new UIPrism(); }

END_NAMESPACE_DISTRHO
