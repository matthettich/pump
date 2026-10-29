// pump GLOSS: interface. BLOB: a glossy drop that wobbles with the sound (its outline is the output, wrapped around
// it), lit by a highlight, swelling with the level. SCOPE: the output, with the pitch envelope drawn under it.
// MACHINE: the seven machines, each with a line on what it is. Then the machine's four controls (named for it),
// the pitch (tune, envelope, time, glide, voice mode), the filter, the amp, the effects (drive and its type, crush,
// width), the LFO, the output; a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginGloss.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::gloss;

static const char* const kMachineAbout[kNumMachines] = {
    "two-operator FM: latex, boings, squeaks",
    "a sound chip: bitwise waves, sync, ring",
    "a formant voice: vowels, size, breath",
    "seven detuned saws, stereo",
    "sixteen digital waves, bits, fold, warp",
    "a plucked string, string to metal",
    "water drops that chirp and fizz",
};

class UIGloss : public PumpUI {
public:
    UIGloss() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::gloss(), "GLOSS", 1000, 600, -1)
    {
        for (int k = 0; k < kNumMachines; ++k)
        {
            Button* b = addButton(kMachineKeys[k], [this, k]() { setParamGesture(kMachine, (float)k); }, [this, k]() { return (int)value(kMachine) == k; });
            b->small = true; b->tip = kMachineAbout[k];
            machineBtn[k] = b;
        }
        for (int k = 0; k < 4; ++k) { ctl[k] = addDial(kA + k, "A", Dial::Large); }
        const struct { int p; const char* l; bool bip; const char* tip; } pitch[] = {
            { kTune, "TUNE", true, "Tune in semitones" },
            { kPitchEnv, "P.ENV", true, "Pitch envelope: semitones the note starts above (or below) and falls back from" },
            { kPitchTime, "P.TIME", false, "How long the pitch envelope takes to fall back" },
            { kGlide, "GLIDE", false, "Glide between notes (mono and legato; poly: none)" },
        };
        for (const auto& m : pitch) { Dial* d = addDial(m.p, m.l, Dial::Small, m.bip); d->tip = m.tip; pitchD.push_back(d); }
        modeSeg = addSegmented(kMode, { "POLY", "MONO", "LEGATO" });
        modeSeg->tip = "Poly (8 voices), mono (every note restarts), legato (overlapping notes glide)";
        filtSeg = addSegmented(kFilterType, { "LP", "BP", "HP", "OFF" });
        filtSeg->tip = "Filter: low-pass, band-pass, high-pass, or off";
        const struct { int p; const char* l; bool bip; const char* tip; } filt[] = {
            { kCutoff, "CUTOFF", false, "Cutoff" },
            { kRes, "RESO", false, "Resonance" },
            { kFilterEnv, "F.ENV", true, "Filter envelope: octaves the cutoff starts above (or below) and falls back from" },
            { kFilterDecay, "F.DEC", false, "Filter envelope decay" },
        };
        for (const auto& m : filt) { Dial* d = addDial(m.p, m.l, Dial::Small, m.bip); d->tip = m.tip; filtD.push_back(d); }
        const struct { int p; const char* l; const char* tip; } amp[] = {
            { kAttack, "A", "Attack" }, { kDecay, "D", "Decay" }, { kSustain, "S", "Sustain" }, { kRelease, "R", "Release" }, { kVelAmp, "VEL", "Velocity to level" },
        };
        for (const auto& m : amp) { Dial* d = addDial(m.p, m.l, Dial::Small); d->tip = m.tip; ampD.push_back(d); }
        driveSeg = addSegmented(kDriveType, { "SOFT", "HARD", "FOLD" });
        driveSeg->tip = "Drive: soft saturation, hard clipping, or wavefolding";
        const struct { int p; const char* l; const char* tip; } fx[] = {
            { kDrive, "DRIVE", "Drive (at the high rate, on the summed voices)" }, { kCrushBits, "BITS", "Crush: bits (16 = off)" },
            { kCrushRate, "RATE", "Crush: sample rate (the top = off)" }, { kWidth, "WIDTH", "Stereo width" },
        };
        for (const auto& m : fx) { Dial* d = addDial(m.p, m.l, Dial::Small); d->tip = m.tip; fxD.push_back(d); }
        lfoSeg = addSegmented(kLfoShape, { "SIN", "TRI", "SQR", "RMP", "S&H" });
        lfoSeg->tip = "LFO shape";
        destMenu = addMenu(kLfoDest);
        destMenu->tip = "Where the LFO goes";
        lfoRate = addDial(kLfoRate, "RATE", Dial::Small); lfoRate->tip = "LFO rate";
        lfoDepth = addDial(kLfoDepth, "DEPTH", Dial::Small); lfoDepth->tip = "LFO depth";
        const struct { int p; const char* l; bool bip; const char* tip; } out[] = {
            { kKeyTrack, "KEY", false, "Key tracking of the cutoff" }, { kBendRange, "BEND", false, "Pitch bend range" }, { kVolume, "VOLUME", true, "Volume" },
        };
        for (const auto& m : out) { Dial* d = addDial(m.p, m.l, Dial::Small, m.bip); d->tip = m.tip; outD.push_back(d); }

        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginGloss*>(pluginPtr())) plug = p;
    }
    ~UIGloss() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        const int m = (int)value(kMachine);
        if (i == kA && m == kChip) return kChipWaves[pump::clampv((int)(v * 0.01f * 7.999f), 0, 7)];
        if (i == kA && m == kRubber) { double r = 0.5 * std::exp2(v * 0.01 * 4.0); const double rr = std::round(r * 2.0) * 0.5; if (std::fabs(r - rr) < 0.08 * rr) r = rr; std::snprintf(b, sizeof b, "x%.2f", r); return b; }
        if (i == kA && m == kDigi) { std::snprintf(b, sizeof b, "WAVE %.1f", 1.0 + v * 0.15); return b; }
        if (i == kA && m == kVoice) { static const char* const vw = "AEIOU"; const float pos = v * 0.04f; const int k = pump::clampv((int)std::lround(pos), 0, 4); std::snprintf(b, sizeof b, "%c", vw[k]); return b; }
        if (i == kB && m == kDigi) { std::snprintf(b, sizeof b, "%.0f BITS", 2.0 + v * 0.14); return b; }
        if (i == kB && m == kVoice) { std::snprintf(b, sizeof b, "x%.2f", std::exp2((v * 0.01 - 0.35) * 1.6)); return b; }
        if (i == kC && m == kChip) { if (v < 0.5f) return "OFF"; std::snprintf(b, sizeof b, "x%.1f", 1.0 + 7.0 * v * 0.01); return b; }
        if (i == kTune || i == kPitchEnv) { if (std::fabs(v) < 0.005f) return "0 st"; std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.005f ? "%+.0f st" : "%+.1f st", v); return b; }
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kCrushBits && v >= 15.5f) return "OFF";
        if (i == kCrushRate && v >= 47999.0f) return "OFF";
        if (i == kBendRange) { std::snprintf(b, sizeof b, "%.0f st", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 56, h1 = 176;
        blobR = Rect(14, top, 250, h1);
        scopeR = Rect(276, top, 420, h1);
        machR = Rect(708, top, 278, h1);
        for (int k = 0; k < kNumMachines; ++k) machineBtn[k]->r = Rect(716, top + 24 + k * 21.0f, 78, 18);
        // row two: the machine's controls, pitch, filter
        const float y2 = 244;
        machP = Rect(14, y2, 420, 118);
        for (int k = 0; k < 4; ++k) ctl[k]->r = Rect(24 + k * 102.0f, y2 + 20, 96, Dial::heightFor(Dial::Large));
        pitchP = Rect(446, y2, 270, 118);
        modeSeg->r = Rect(526, y2 + 4, 180, 18);
        for (size_t k = 0; k < pitchD.size(); ++k) pitchD[k]->r = Rect(452 + k * 65.0f, y2 + 34, 62, Dial::heightFor(Dial::Small));
        filtP = Rect(728, y2, 258, 118);
        filtSeg->r = Rect(790, y2 + 4, 186, 18);
        for (size_t k = 0; k < filtD.size(); ++k) filtD[k]->r = Rect(734 + k * 62.0f, y2 + 34, 60, Dial::heightFor(Dial::Small));
        // row three: amp, effects, LFO, output
        const float y3 = 372;
        ampP = Rect(14, y3, 290, 104);
        for (size_t k = 0; k < ampD.size(); ++k) ampD[k]->r = Rect(20 + k * 56.0f, y3 + 28, 54, Dial::heightFor(Dial::Small));
        fxP = Rect(316, y3, 256, 104);
        driveSeg->r = Rect(436, y3 + 4, 126, 18);
        for (size_t k = 0; k < fxD.size(); ++k) fxD[k]->r = Rect(322 + k * 62.0f, y3 + 28, 60, Dial::heightFor(Dial::Small));
        lfoP = Rect(584, y3, 220, 104);
        lfoSeg->r = Rect(624, y3 + 4, 170, 18);
        lfoRate->r = Rect(590, y3 + 28, 56, Dial::heightFor(Dial::Small));
        lfoDepth->r = Rect(648, y3 + 28, 56, Dial::heightFor(Dial::Small));
        destMenu->r = Rect(710, y3 + 46, 86, 20);
        outP = Rect(816, y3, 170, 104);
        for (size_t k = 0; k < outD.size(); ++k) outD[k]->r = Rect(820 + k * 55.0f, y3 + 28, 54, Dial::heightFor(Dial::Small));
        keyboard.r = Rect(14, 488, 972, 76);
        keyboard.octaves = 6;
    }

    void tick(float dt) override
    {
        const int m = pump::clampv((int)value(kMachine), 0, kNumMachines - 1);
        for (int k = 0; k < 4; ++k) { ctl[k]->label = kMachineControls[m][k]; ctl[k]->tip = std::string(kMachineNames[m]) + ": " + controlTip(m, k); }
        const bool poly = (int)value(kMode) == kPoly;
        pitchD[3]->enabled = !poly;
        for (Dial* d : filtD) d->enabled = (int)value(kFilterType) != kFilterOff;
        lfoRate->enabled = lfoDepth->enabled = true;
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        wave.capture(plug->engine.outRing, dt);
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kBlobN; ++i) blobBuf[i] = plug->engine.outRing.at(end - (uint32_t)kBlobN + (uint32_t)i);
        float pk = 0.0f; for (float x : blobBuf) pk = std::max(pk, std::fabs(x));
        swell += (std::min(1.0f, pk * 3.0f) - swell) * std::min(1.0f, dt * (pk * 3.0f > swell ? 18.0f : 3.0f));
        penv = plug->engine.pitchEnvNow();
        t += dt;
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
    }
    static std::string controlTip(int m, int k)
    {
        static const char* const tips[kNumMachines][4] = {
            { "the modulator's ratio (snaps to whole and half ratios)", "the FM index (brightness)", "feedback on the modulator (rougher, noisier)", "bounce: the index falls with the pitch envelope" },
            { "the wave: triangle, saw, pulse, noise, and the bitwise mixes", "pulse width", "sync: a slave oscillator up to 8x the note, restarted each period", "ring modulation by a triangle a fifth up" },
            { "the vowel: a e i o u", "size: the formants shift (child to giant)", "breath: noise in the voice", "move: the vowel bends with the pitch envelope" },
            { "detune of the seven saws", "mix: the centre saw or the six around it", "shape: saw to square", "an octave up on top" },
            { "the wave: morphs through sixteen digital waves", "bits: the wave's resolution (all the way up = clean)", "fold: a wavefolder", "warp: phase distortion" },
            { "damp: how fast the string dies and how dark it gets", "bright: the pluck (felt to pick)", "stretch: stiffness (string to metal)", "body: a second string a few cents apart" },
            { "chirp: how far each drop's pitch rises as it dies", "time: how long a drop lasts", "wet: resonant noise at the drop's pitch", "fizz: more drops at random while the key is held" },
        };
        return tips[pump::clampv(m, 0, kNumMachines - 1)][pump::clampv(k, 0, 3)];
    }

    void drawBody() override
    {
        drawBlob();
        drawScope();
        // the machines' list
        screenBox(machR);
        caption(machR.x + 10, machR.y + 12, "MACHINE");
        const int m = pump::clampv((int)value(kMachine), 0, kNumMachines - 1);
        for (int k = 0; k < kNumMachines; ++k)
            textAt(machineBtn[k]->r.r() + 8, machineBtn[k]->r.cy(), kMachineAbout[k], kLabel, 9.0f, k == m ? theme.ink : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.3f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 13, c); };
        panel(machP, (std::string(kMachineKeys[m]) + " CONTROLS").c_str());
        panel(pitchP, "PITCH"); panel(filtP, "FILTER"); panel(ampP, "AMP"); panel(fxP, "DRIVE / CRUSH"); panel(lfoP, "LFO"); panel(outP, "OUT");
        caption(destMenu->r.x, destMenu->r.y - 9, "TO");
        keyboard.draw(*this);
        drawOutMeter(outMeter, Rect(990, 56, 4, 176), Rect(995, 56, 4, 176), -48.0f, 6.0f);
    }

    // the glossy drop: a circle whose radius follows the output around it, filled with a gradient, a highlight on top
    void drawBlob()
    {
        const Rect& r = blobR;
        screenBox(r);
        caption(r.x + 10, r.y + 12, "BLOB");
        const float cx = r.cx(), cy = r.cy() + 6, base = std::min(r.w, r.h) * (0.26f + 0.06f * swell);
        float scale = 1e-3f; for (float x : blobBuf) scale = std::max(scale, std::fabs(x));
        const int N = 160;
        // the outline: the newest samples wrapped around the circle, smoothed (a box filter, three times round)
        float ring[N];
        for (int i = 0; i < N; ++i) { const int si = kBlobN - 1 - (int)((float)i / N * (kBlobN - 1) * 0.5f); ring[i] = blobBuf[pump::clampv(si, 0, kBlobN - 1)] / scale; }
        for (int pass = 0; pass < 3; ++pass)
        {
            float tmp[N];
            for (int i = 0; i < N; ++i) { float a = 0.0f; for (int k = -3; k <= 3; ++k) a += ring[(i + k + N) % N]; tmp[i] = a / 7.0f; }
            std::copy(tmp, tmp + N, ring);
        }
        save(); scissor(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
        // a soft shadow
        beginPath(); ellipse(cx + 6, cy + base + 14, base * 0.9f, 7);
        fillColor(withAlpha2(theme.ink, theme.dark ? 0.35f : 0.12f)); fill();
        auto path = [&](float grow) {
            beginPath();
            for (int i = 0; i <= N; ++i)
            {
                const float a = (float)(pump::kTwoPi * i / N);
                // the sound around the circle, and a slow wobble of its own on top
                const float w = ring[i % N] * swell;
                const float wob = 0.04f * std::sin(a * 3.0f + t * 1.7f) + 0.03f * std::sin(a * 5.0f - t * 2.3f);
                const float rr = base * (1.0f + 0.28f * w + wob) + grow;
                const float x = cx + std::cos(a) * rr, y = cy + std::sin(a) * rr * 0.92f;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            closePath();
        };
        const Color hi = pump::mixColor(theme.neon, pump::hexColor(0xFFFFFF), 0.55f), lo = pump::mixColor(theme.neon, pump::hexColor(0x5A0838), theme.dark ? 0.35f : 0.15f);
        path(0.0f);
        fillPaint(radialGradient(cx - base * 0.35f, cy - base * 0.4f, base * 0.1f, base * 1.4f, hi, lo)); fill();
        strokeColor(theme.ink); strokeWidth(1.5f); stroke();
        // the highlight
        beginPath(); ellipse(cx - base * 0.32f, cy - base * 0.42f, base * 0.28f, base * 0.14f);
        fillColor(withAlpha2(pump::hexColor(0xFFFFFF), 0.75f)); fill();
        beginPath(); circle(cx + base * 0.30f, cy - base * 0.18f, base * 0.06f);
        fillColor(withAlpha2(pump::hexColor(0xFFFFFF), 0.55f)); fill();
        restore();
    }

    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 12, "SCOPE");
        const Rect p(r.x + 10, r.y + 22, r.w - 20, r.h - 30);
        // the pitch envelope's shape, under the wave
        const float pe = (float)value(kPitchEnv) / 48.0f;
        if (std::fabs(pe) > 0.001f)
        {
            const float T = (float)value(kPitchTime) / 1000.0f;
            beginPath();
            for (int i = 0; i <= 60; ++i)
            {
                const float tt = i / 60.0f * std::max(0.05f, T * 1.5f), v = std::exp(-3.0f * tt / std::max(0.001f, T));
                const float x = p.x + p.w * i / 60.0f, y = p.cy() - pe * v * p.h * 0.45f;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(withAlpha2(theme.ink, 0.25f)); strokeWidth(1.2f); stroke();
            // where the envelope is now
            const float now = penv;
            fillRect(Rect(p.r() - 6, p.cy() - std::max(0.0f, pe * now) * p.h * 0.45f, 4, std::fabs(pe * now) * p.h * 0.45f + 1), theme.second);
        }
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.2f), 1.0f);
        // the wave, thick, in the plugin's colour
        if (wave.show > 0.01f)
        {
            const int win = 1024;
            int start = WaveOverlay::kN - win - 1;
            for (int i = start; i > start - win && i > 1; --i) if (wave.buf[i - 1] < 0.0f && wave.buf[i] >= 0.0f) { start = i; break; }
            float mx = 1e-4f; for (int i = 0; i < win; ++i) mx = std::max(mx, std::fabs(wave.buf[start + i]));
            const float want = 1.0f / mx;
            wave.scale = want < wave.scale ? want : wave.scale + (want - wave.scale) * 0.05f;
            save(); scissor(p.x, p.y, p.w, p.h);
            for (int pass = 0; pass < 2; ++pass)
            {
                beginPath();
                for (int i = 0; i < win; i += 2)
                {
                    const float x = p.x + p.w * i / (win - 1), y = p.cy() - pump::clampv(wave.buf[start + i] * wave.scale, -1.2f, 1.2f) * p.h * 0.42f;
                    if (i == 0) moveTo(x, y); else lineTo(x, y);
                }
                lineJoin(ROUND);
                if (pass == 0) { strokeColor(withAlpha2(theme.neon, 0.35f * wave.show)); strokeWidth(6.0f); }
                else { strokeColor(withAlpha2(theme.dark ? theme.neon : theme.ink, wave.show)); strokeWidth(1.8f); }
                stroke();
            }
            restore();
        }
        char b[32]; std::snprintf(b, sizeof b, "VOICES %d", plug ? plug->engine.voicesNow() : 0);
        textAt(r.r() - 10, r.y + 12, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (blobR.contains(x, y) && plug) { blobNote = 60; plug->keys.push(60, 110); return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override { if (kbDown) keyboard.mouseDrag(x, y); }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; }
        if (blobNote >= 0 && plug) { plug->keys.push((uint8_t)blobNote, 0); blobNote = -1; }
    }
    void releaseAllHeld() override { keyboard.mouseUp(); kbDown = false; if (blobNote >= 0 && plug) plug->keys.push((uint8_t)blobNote, 0); blobNote = -1; }
    void bodyHover(float x, float y) override
    {
        if (keyboard.r.contains(x, y)) setStatus("Play: velocity follows where you strike the key; drag across keys");
        else if (blobR.contains(x, y)) setStatus("Click the blob: it plays C4");
        else if (scopeR.contains(x, y)) setStatus("The output, with the pitch envelope's shape under it");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Keyboard", "click to play, drag across" }, { "The blob", "click it to play C4" } };
    }
    std::string footerInfo() override
    {
        std::string s = std::string(kMachineKeys[pump::clampv((int)value(kMachine), 0, kNumMachines - 1)]) + "   " + kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)];
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kBlobN = 1024;
    PluginGloss* plug = nullptr;
    Rect blobR, scopeR, machR, machP, pitchP, filtP, ampP, fxP, lfoP, outP;
    Button* machineBtn[kNumMachines];
    Dial* ctl[4];
    std::vector<Dial*> pitchD, filtD, ampD, fxD, outD;
    Dial *lfoRate, *lfoDepth;
    Segmented *modeSeg, *filtSeg, *driveSeg, *lfoSeg;
    MenuButton* destMenu;
    KeyboardUI keyboard;
    OutMeter outMeter;
    WaveOverlay wave;
    float blobBuf[kBlobN] = {}, swell = 0.0f, penv = 0.0f, t = 0.0f;
    bool kbDown = false;
    int blobNote = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIGloss)
};

UI* createUI() { return new UIGloss(); }

END_NAMESPACE_DISTRHO
