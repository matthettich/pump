// pump GRUMBLE: interface. Top: the MORPH pad (drag the puck between the four layers, one at each corner), the
// creature (a line drawing that is the sound: its size follows BEAST's pitch, horns and teeth come with GROWL, a
// second head with SPLIT, antennae with SWARM; it opens its mouth when it makes a sound and radiates when it's loud),
// and the BODY (gate, drive, tone, cave, mix, output) with the output meter. Below: the four layers, each with its
// switch and its own controls.
#include "ui/PumpUI.hpp"
#include "PluginGrumble.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::grumble;

class UIGrumble : public PumpUI {
public:
    UIGrumble() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::grumble(), "GRUMBLE", 1000, 620, kBypass)
    {
        static const char* const onTip[kNumLayers] = {
            "BEAST: your voice moved in pitch and formant, with a creaky octave below (key 1)",
            "SPLIT: a second voice with its own pitch and formant and a wobble (key 2)",
            "GROWL: a rough throat that speaks with your voice through a vocoder (key 3)",
            "SWARM: a cloud of grains of your voice (key 4)",
        };
        for (int k = 0; k < kNumLayers; ++k)
        {
            onBtn[k] = addToggle(kLayerOn[k], kLayerNames[k]);
            onBtn[k]->tip = onTip[k];
        }
        auto d = [&](int layer, int p, const char* label, const char* tip, bool bip = false) {
            Dial* x = addDial(p, label, Dial::Small, bip);
            x->tip = tip; x->tabGroup = 10 + layer;
            dials[layer].push_back(x);
        };
        d(kBeast, kBeastPitch, "PITCH", "Beast pitch in semitones (down = bigger, up = smaller)", true);
        d(kBeast, kBeastFormant, "FORMANT", "Beast formant: the size of the throat (down = deep monster, up = tiny), apart from the pitch", true);
        d(kBeast, kBeastFry, "FRY", "Fry: a creaky octave below, coming in hardest on the voice's peaks");
        d(kBeast, kBeastLevel, "LEVEL", "Beast level");
        d(kBeast, kBeastPan, "PAN", "Beast pan", true);

        d(kSplit, kSplitPitch, "PITCH", "Split pitch in semitones", true);
        d(kSplit, kSplitFormant, "FORMANT", "Split formant", true);
        d(kSplit, kSplitWobble, "WOBBLE", "Wobble: an uneven vibrato of pitch and formant together (faster and deeper as it goes up)");
        d(kSplit, kSplitLevel, "LEVEL", "Split level");
        d(kSplit, kSplitPan, "PAN", "Split pan", true);

        d(kGrowl, kGrowlPitch, "PITCH", "Growl pitch: its throat follows your voice, this many semitones away", true);
        d(kGrowl, kGrowlRough, "ROUGH", "Rough: each period's length and strength jittered, some dropped (the crackle of a growl)");
        d(kGrowl, kGrowlBreath, "BREATH", "Breath: noise in the throat (a hiss, a ghostly whisper at full)");
        d(kGrowl, kGrowlSize, "SIZE", "Size: the throat's formants moved (down = a bigger animal)", true);
        d(kGrowl, kGrowlLevel, "LEVEL", "Growl level");
        d(kGrowl, kGrowlPan, "PAN", "Growl pan", true);

        d(kSwarm, kSwarmPitch, "PITCH", "Swarm pitch: the grains' centre, in semitones", true);
        d(kSwarm, kSwarmSpread, "SPREAD", "Spread: each grain lands at a random pitch within this many semitones of the centre");
        d(kSwarm, kSwarmDensity, "DENSITY", "Density: grains a second (a few to a buzzing cloud)");
        d(kSwarm, kSwarmGrain, "GRAIN", "Grain length (short = chittering, long = a crowd)");
        d(kSwarm, kSwarmLevel, "LEVEL", "Swarm level");
        d(kSwarm, kSwarmWidth, "WIDTH", "Width: how far the grains spread left and right");

        gateDial = addDial(kGate, "GATE", Dial::Medium);
        gateDial->tip = "Gate: below this the creature falls silent, so room noise and breaths don't growl (the far left = off)";
        driveDial = addDial(kDrive, "DRIVE", Dial::Medium);
        driveDial->tip = "Drive: an asymmetric tube on the creature, level kept";
        toneDial = addDial(kTone, "TONE", Dial::Medium, true);
        toneDial->tip = "Tone: a tilt, dark to bright";
        caveDial = addDial(kCave, "CAVE", Dial::Medium);
        caveDial->tip = "Cave: a short, dark stone room around the creature";
        mixDial = addDial(kMix, "MIX", Dial::Medium);
        mixDial->tip = "Mix: creature vs your voice (latency-aligned)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        gateDial->tabGroup = driveDial->tabGroup = toneDial->tabGroup = caveDial->tabGroup = mixDial->tabGroup = outDial->tabGroup = 1;

        if (auto* p = static_cast<PluginGrumble*>(pluginPtr())) plug = p;
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kBeastPitch: case kBeastFormant: case kSplitPitch: case kSplitFormant:
        case kGrowlPitch: case kGrowlSize: case kSwarmPitch:
            if (std::fabs(v) < 0.05f) return "0 ST";
            std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.05f ? "%+.0f ST" : "%+.1f ST", v); return b;
        case kSwarmSpread:
            std::snprintf(b, sizeof b, "±%.1f ST", v); return b;
        case kBeastPan: case kSplitPan: case kGrowlPan:
            if (std::fabs(v) < 0.5f) return "C";
            std::snprintf(b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v)); return b;
        case kGate: if (!gateActive(v)) return "OFF"; break;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        padR = Rect(14, 58, 322, 322);
        faceR = Rect(350, 58, 330, 322);
        bodyPanel = Rect(694, 58, 262, 322);
        meterL = Rect(966, 58, 9, 304);
        meterR = Rect(977, 58, 9, 304);
        const float bx = 704, bw = 82;
        const float rows[3] = { 84, 182, 280 };
        gateDial->r = Rect(bx, rows[0], 72, Dial::heightFor(Dial::Medium));
        driveDial->r = Rect(bx + bw, rows[0], 72, Dial::heightFor(Dial::Medium));
        toneDial->r = Rect(bx + 2 * bw, rows[0], 72, Dial::heightFor(Dial::Medium));
        caveDial->r = Rect(bx, rows[1], 72, Dial::heightFor(Dial::Medium));
        mixDial->r = Rect(bx + bw, rows[1], 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(bx + 2 * bw, rows[1], 72, Dial::heightFor(Dial::Medium));
        gateLampR = Rect(bx + 4, rows[2] + 6, 12, 12);

        const float y0 = 396, pw = 236, gap = 10;
        for (int k = 0; k < kNumLayers; ++k)
        {
            const float x = 14 + k * (pw + gap);
            layerPanel[k] = Rect(x, y0, pw, 192);
            onBtn[k]->r = Rect(x + 10, y0 + 8, 86, 22);
            layerMeter[k] = Rect(x + 108, y0 + 15, pw - 120, 8);
            auto& ds = dials[k];
            for (size_t j = 0; j < ds.size(); ++j)
            {
                const int row = (int)j / 3, col = (int)j % 3;
                ds[j]->r = Rect(x + 12 + col * 74, y0 + 40 + row * 76, 64, Dial::heightFor(Dial::Small));
            }
        }
    }

    void tick(float dt) override
    {
        time += dt;
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        const float k = std::min(1.0f, dt * 18.0f);
        const float out = plug->engine.outLevel.load(std::memory_order_relaxed);
        const float open = pump::clampv((out + 42.0f) / 36.0f, 0.0f, 1.0f);
        mouth += (open - mouth) * (open > mouth ? k : k * 0.5f);
        for (int i = 0; i < kNumLayers; ++i)
        {
            const float l = plug->engine.layerLevel[i].load(std::memory_order_relaxed);
            const float v = pump::clampv((l + 48.0f) / 48.0f, 0.0f, 1.0f);
            layerShow[i] = v > layerShow[i] ? v : layerShow[i] + (v - layerShow[i]) * k * 0.4f;
            gainShow[i] += (plug->engine.morphGain[i].load(std::memory_order_relaxed) - gainShow[i]) * k;
        }
        gateShow += (plug->engine.gateOpen.load(std::memory_order_relaxed) - gateShow) * k;
        const float m = plug->engine.inMidi.load(std::memory_order_relaxed);
        if (m > 0.0f) { inMidi = inMidi < 0.0f ? m : inMidi + (m - inMidi) * k; lastMidiAge = 0.0f; }
        else if ((lastMidiAge += dt) > 0.3f) inMidi = -1.0f;
    }

    bool layerOn(int k) const { return value(kLayerOn[k]) >= 0.5f; }

    void drawBody() override
    {
        drawPad();
        drawCreature();
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, padR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        strokeRect(bodyPanel, theme.dim, 1.0f);
        caption(bodyPanel.x + 12, bodyPanel.y + 16, "BODY");
        // the gate lamp and the voice's pitch
        const Rect& g = gateLampR;
        fillRect(g, gateShow > 0.05f ? pump::mixColor(theme.panel2, theme.neon, gateShow) : theme.panel2);
        strokeRect(g, theme.ink, 1.0f);
        textAt(g.r() + 8, g.cy(), gateActive(value(kGate)) ? "GATE OPEN" : "GATE OFF", kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        char nb[8], t[48];
        if (inMidi > 0.0f) std::snprintf(t, sizeof t, "VOICE  %s", pump::pitch::noteName((int)std::lround(inMidi), nb, sizeof nb));
        else std::snprintf(t, sizeof t, "VOICE  --");
        textAt(g.x, g.b() + 22, t, kMono, 11.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
        if (inMidi > 0.0f)
        {
            std::string s;
            const float shift[3] = { value(kBeastPitch), value(kSplitPitch), value(kGrowlPitch) };
            const char* nm[3] = { "B", "S", "G" };
            for (int k = 0; k < 3; ++k)
                if (layerOn(k == 2 ? kGrowl : k))
                {
                    if (!s.empty()) s += "  ";
                    s += std::string(nm[k]) + " " + pump::pitch::noteName((int)std::lround(inMidi + shift[k]), nb, sizeof nb);
                }
            textAt(g.x, g.b() + 40, s, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }

        for (int k = 0; k < kNumLayers; ++k)
        {
            const Rect& p = layerPanel[k];
            const bool on = layerOn(k);
            strokeRect(p, on ? withAlpha2(theme.ink, 0.55f) : theme.dim, on ? 1.5f : 1.0f);
            // the layer's level, scaled by where the pad puts it
            const Rect& m = layerMeter[k];
            fillRect(m, theme.panel2);
            if (on) fillRect(Rect(m.x, m.y, m.w * layerShow[k], m.h), cornerColor(k));
            strokeRect(m, withAlpha2(theme.ink, 0.6f), 1.0f);
            char b[16]; std::snprintf(b, sizeof b, "PAD %.0f%%", on ? gainShow[k] * 100.0f : 0.0f);
            textAt(m.r(), m.b() + 9, b, kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
    }

    // each corner's colour: the accent, its second, and two mixes, so the pad and the panels match
    Color cornerColor(int k) const
    {
        switch (k)
        {
        case kBeast: return theme.neon;
        case kSplit: return pump::mixColor(theme.neon, theme.second, 0.35f);
        case kGrowl: return pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.25f : 0.45f);
        default: return theme.second;
        }
    }

    // ---- the morph pad ----
    Rect padPlot() const { return Rect(padR.x + 24, padR.y + 30, padR.w - 48, padR.h - 60); }
    void drawPad()
    {
        const Rect& r = padR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "MORPH");
        const Rect p = padPlot();
        for (int i = 1; i < 4; ++i)
        {
            line(p.x + p.w * i / 4, p.y, p.x + p.w * i / 4, p.b(), theme.grid, 1.0f);
            line(p.x, p.y + p.h * i / 4, p.r(), p.y + p.h * i / 4, theme.grid, 1.0f);
        }
        strokeRect(p, withAlpha2(theme.ink, 0.35f), 1.0f);
        const float px = p.x + value(kMorphX) * 0.01f * p.w, py = p.b() - value(kMorphY) * 0.01f * p.h;
        // the corners: a disc that swells with the layer's gain and pulses with its sound
        const float cx[kNumLayers] = { p.x, p.r(), p.x, p.r() }, cy[kNumLayers] = { p.y, p.y, p.b(), p.b() };
        for (int k = 0; k < kNumLayers; ++k)
        {
            const bool on = layerOn(k);
            const Color c = cornerColor(k);
            if (on)
            {
                // a line from the puck to the corner, as strong as the corner's share
                line(px, py, cx[k], cy[k], withAlpha2(c, 0.15f + 0.6f * gainShow[k]), 1.0f + 2.5f * gainShow[k]);
                beginPath();
                circle(cx[k], cy[k], 6.0f + 14.0f * gainShow[k] * (0.6f + 0.4f * layerShow[k]));
                fillColor(withAlpha2(c, theme.dark ? 0.55f : 0.75f)); fill();
                strokeColor(theme.ink); strokeWidth(1.5f); stroke();
            }
            else
            {
                beginPath(); circle(cx[k], cy[k], 6.0f);
                strokeColor(theme.dim); strokeWidth(1.0f); stroke();
            }
            const bool left = k == kBeast || k == kGrowl, top = k == kBeast || k == kSplit;
            textAt(left ? r.x + 8 : r.r() - 8, top ? r.y + 30 : r.b() - 12, kLayerNames[k], kLabel, 10.5f,
                   on ? theme.ink : theme.ink2, (left ? ALIGN_LEFT : ALIGN_RIGHT) | ALIGN_MIDDLE, 1.0f);
        }
        keyBox(Rect(px - 8, py - 8, 16, 16), false, dragging, &theme.neon, 3.0f);
        char t[32]; std::snprintf(t, sizeof t, "%.0f / %.0f", value(kMorphX), value(kMorphY));
        textAt(r.r() - 10, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ---- the creature ----
    void drawCreature()
    {
        const Rect& r = faceR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "CREATURE");
        int nOn = 0; for (int k = 0; k < kNumLayers; ++k) nOn += layerOn(k);
        textAt(r.r() - 10, r.y + 14, nOn ? "" : "ALL LAYERS OFF", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        save();
        scissor(r.x + 2, r.y + 24, r.w - 4, r.h - 26);
        const Color ink = theme.ink, fillC = withAlpha2(theme.neon, theme.dark ? 0.22f : 0.40f);
        const bool beast = layerOn(kBeast), split = layerOn(kSplit), growl = layerOn(kGrowl), swarm = layerOn(kSwarm);
        // size: the beast's pitch and formant (down = bigger), else the growl's size
        float big = 0.0f;
        if (beast) big = -(value(kBeastPitch) + value(kBeastFormant)) / 36.0f;
        else if (growl) big = -value(kGrowlSize) / 24.0f;
        const float s = pump::clampv(70.0f * (1.0f + 0.45f * big), 44.0f, 108.0f);
        const float bob = 2.0f * std::sin(time * 2.1f) + 3.0f * mouth;
        const float cx = r.cx() - (split ? 26.0f : 0.0f), cy = r.cy() + 18.0f + bob;
        const float lw = 3.0f;

        // the second head (SPLIT), behind, wobbling with its wobble
        if (split)
        {
            const float wob = value(kSplitWobble) * 0.01f;
            const float sx = cx + s * 1.05f + 4.0f * wob * std::sin(time * (5.0f + 6.0f * wob)), sy = cy - s * 0.35f;
            const float ss = s * pump::clampv(0.62f - value(kSplitFormant) / 60.0f, 0.4f, 0.85f);
            line(cx + s * 0.6f, cy - s * 0.1f, sx - ss * 0.5f, sy + ss * 0.4f, ink, lw);   // the neck
            beginPath(); ellipse(sx, sy, ss * 0.9f, ss * 0.8f);
            fillColor(withAlpha2(cornerColor(kSplit), theme.dark ? 0.25f : 0.45f)); fill();
            strokeColor(ink); strokeWidth(lw); stroke();
            beginPath(); circle(sx + ss * 0.2f, sy - ss * 0.15f, ss * 0.2f); fillColor(theme.screen); fill(); strokeColor(ink); strokeWidth(2.0f); stroke();
            beginPath(); circle(sx + ss * 0.24f, sy - ss * 0.12f, ss * 0.08f); fillColor(ink); fill();
            beginPath(); ellipse(sx + ss * 0.1f, sy + ss * 0.35f, ss * 0.3f, 2.0f + ss * 0.22f * mouth * layerShow[kSplit]);
            fillColor(ink); fill();
        }
        // antennae (SWARM): curled lines with tips that buzz
        if (swarm)
        {
            const float buzz = layerShow[kSwarm];
            for (int side = -1; side <= 1; side += 2)
            {
                const float bx = cx + side * s * 0.35f, by = cy - s * 0.85f;
                const float tx = bx + side * s * 0.45f + side * 3.0f * buzz * std::sin(time * 40.0f + side), ty = by - s * 0.55f;
                beginPath(); moveTo(bx, by); quadTo(bx + side * s * 0.05f, ty, tx, ty);
                strokeColor(ink); strokeWidth(2.5f); stroke();
                beginPath(); circle(tx, ty, 4.0f + 3.0f * buzz); fillColor(cornerColor(kSwarm)); fill(); strokeColor(ink); strokeWidth(2.0f); stroke();
            }
        }
        // horns (GROWL): longer with ROUGH
        if (growl)
        {
            const float h = s * (0.25f + 0.45f * value(kGrowlRough) * 0.01f);
            for (int side = -1; side <= 1; side += 2)
            {
                const float b0 = cx + side * s * 0.55f, b1 = cx + side * s * 0.85f, by = cy - s * 0.62f + side * 0.0f;
                beginPath(); moveTo(b0, by - s * 0.18f); quadTo(b1 + side * h * 0.3f, by - h * 0.5f, b1 + side * h * 0.5f, by - s * 0.2f - h);
                lineTo(b1, by + s * 0.05f); closePath();
                fillColor(withAlpha2(cornerColor(kGrowl), 0.85f)); fill();
                strokeColor(ink); strokeWidth(2.5f); stroke();
            }
        }
        // radiating lines when it's loud
        if (mouth > 0.15f)
        {
            const int nRay = 12;
            for (int i = 0; i < nRay; ++i)
            {
                const float a = (float)i / nRay * 6.2832f + 0.13f;
                const float r0 = s * 1.18f, r1 = r0 + 6.0f + 18.0f * mouth;
                line(cx + std::cos(a) * r0 * 1.1f, cy + std::sin(a) * r0, cx + std::cos(a) * r1 * 1.1f, cy + std::sin(a) * r1, withAlpha2(ink, 0.25f + 0.6f * mouth), 2.0f);
            }
        }
        // the head
        beginPath(); ellipse(cx, cy, s * 1.08f, s);
        fillColor(fillC); fill();
        strokeColor(ink); strokeWidth(lw); stroke();
        // eyes: their pupils follow the voice's pitch
        const float look = inMidi > 0.0f ? pump::clampv((inMidi - 52.0f) / 24.0f, -1.0f, 1.0f) : 0.0f;
        const float eyeR = s * (beast && value(kBeastFormant) < -4.0f ? 0.16f : 0.2f);
        for (int side = -1; side <= 1; side += 2)
        {
            const float ex = cx + side * s * 0.36f, ey = cy - s * 0.3f;
            beginPath(); circle(ex, ey, eyeR); fillColor(theme.screen); fill(); strokeColor(ink); strokeWidth(2.5f); stroke();
            beginPath(); circle(ex + side * eyeR * 0.15f, ey - look * eyeR * 0.45f, eyeR * 0.45f); fillColor(ink); fill();
            // a heavy brow when growling
            if (growl) line(ex - eyeR * 1.2f, ey - eyeR * (side < 0 ? 1.6f : 1.1f), ex + eyeR * 1.2f, ey - eyeR * (side < 0 ? 1.1f : 1.6f), ink, 3.0f);
        }
        // the mouth, open with the sound; teeth with GROWL
        const float mw = s * 0.62f, mh = 3.0f + s * 0.42f * mouth, my = cy + s * 0.38f;
        beginPath(); ellipse(cx, my, mw, mh);
        fillColor(ink); fill();
        if (growl && mh > 6.0f)
        {
            const int nT = 4 + (int)(value(kGrowlRough) * 0.06f);
            const float tw = 2.0f * mw * 0.8f / nT;
            beginPath();
            for (int i = 0; i < nT; ++i)
            {
                const float x0 = cx - mw * 0.8f + i * tw;
                const float yTop = my - mh * std::sqrt(std::max(0.0f, 1.0f - std::pow((x0 + tw * 0.5f - cx) / mw, 2.0f)));
                moveTo(x0, yTop + 1.0f); lineTo(x0 + tw * 0.5f, yTop + std::min(mh, 6.0f + s * 0.08f)); lineTo(x0 + tw, yTop + 1.0f); closePath();
            }
            fillColor(theme.screen); fill();
        }
        if (beast && value(kBeastFry) > 30.0f && mh > 5.0f)   // fry: a jagged lip
            line(cx - mw * 0.6f, my + mh * 0.5f, cx + mw * 0.6f, my + mh * 0.5f, withAlpha2(theme.neon, 0.8f), 2.0f);
        restore();
    }

    // ---- the pad's puck is a control ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (padR.contains(x, y) && button == kMouseButtonLeft)
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kMorphX, spec(kMorphX).def); setParamGesture(kMorphY, spec(kMorphY).def); endGroup(); return false; }
            dragging = true;
            beginEdit(kMorphX); beginEdit(kMorphY);
            dragX = x; dragY = y; dragNX = norm(kMorphX); dragNY = norm(kMorphY);
            if (!(mod & kModifierShift)) movePuck(x, y);
            return true;
        }
        return false;
    }
    void movePuck(float x, float y)
    {
        const Rect p = padPlot();
        setParamNorm(kMorphX, (x - p.x) / p.w);
        setParamNorm(kMorphY, (p.b() - y) / p.h);
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (!dragging) return;
        if (mod & kModifierShift)
        {
            const Rect p = padPlot();
            setParamNorm(kMorphX, dragNX + 0.15f * (x - dragX) / p.w);
            setParamNorm(kMorphY, dragNY + 0.15f * (dragY - y) / p.h);
        }
        else movePuck(x, y);
        setStatus(paramStatus(kMorphX) + "    " + paramStatus(kMorphY));
    }
    void bodyMouseUp(float, float) override
    {
        if (!dragging) return;
        dragging = false;
        endEdit(kMorphY); endEdit(kMorphX);
    }
    void bodyHover(float x, float y) override
    {
        if (padR.contains(x, y)) setStatus("Morph: drag the puck toward a corner to bring its layer forward (Shift = fine, double-click = centre)");
        else if (faceR.contains(x, y)) setStatus("The creature: its size, horns, heads and antennae follow the layers; it opens its mouth when it sounds");
        else if (gateLampR.contains(x, y)) setStatus("The gate: lit while the voice is above GATE");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '4')
        {
            const int p = kLayerOn[key - '1'];
            setParamGesture(p, value(p) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the morph puck", "toward a corner to bring its layer forward (double-click = centre)" },
            { "1 - 4", "BEAST, SPLIT, GROWL, SWARM on / off" },
            { "Gate", "set it just above the room's noise so only your voice wakes the creature" },
        };
    }
    std::string footerInfo() override
    {
        std::string s;
        for (int k = 0; k < kNumLayers; ++k) if (layerOn(k)) { if (!s.empty()) s += " + "; s += kLayerNames[k]; }
        if (s.empty()) s = "NO LAYERS";
        char b[32]; std::snprintf(b, sizeof b, "   LATENCY %d", plug ? plug->latencyNow() : 0);
        return s + b;
    }

private:
    PluginGrumble* plug = nullptr;
    Rect padR, faceR, bodyPanel, meterL, meterR, gateLampR;
    Rect layerPanel[kNumLayers], layerMeter[kNumLayers];
    OutMeter outMeter;
    Button* onBtn[kNumLayers] = {};
    std::vector<Dial*> dials[kNumLayers];
    Dial *gateDial, *driveDial, *toneDial, *caveDial, *mixDial, *outDial;
    float time = 0.0f, mouth = 0.0f, gateShow = 0.0f, inMidi = -1.0f, lastMidiAge = 0.0f;
    float layerShow[kNumLayers] = {}, gainShow[kNumLayers] = {};
    bool dragging = false;
    float dragX = 0, dragY = 0, dragNX = 0, dragNY = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIGrumble)
};

UI* createUI() { return new UIGrumble(); }

END_NAMESPACE_DISTRHO
