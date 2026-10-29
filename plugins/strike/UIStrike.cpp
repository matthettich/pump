// pump STRIKE: interface. PADS across the top (click or keys 1-8; higher in the pad = harder; the pad you play is the
// one you edit), each drawing its engine as it rings: a membrane's modes, a string's standing waves, a circuit's
// ring-down, FM's index, folds, partials, an attractor. Below: the selected pad (its
// engine; pitch, decay, the engine's TONE and two characters, named for what they do in that engine; bend, noise,
// level, pan), then the kit (choke, velocity, drive, volume) and a scope.
#include "ui/PumpUI.hpp"
#include "PluginStrike.hpp"
#include <array>

START_NAMESPACE_DISTRHO

using namespace pump::strike;

class UIStrike : public PumpUI {
public:
    UIStrike() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::strike(), "STRIKE", 1000, 600, -1)
    {
        static const char* const lab[kPerPad] = { "ENGINE", "PITCH", "DECAY", "TONE", "CHAR A", "CHAR B", "BEND", "B-TIME", "NOISE", "LEVEL", "PAN" };
        for (int p = 0; p < kPads; ++p)
        {
            engSeg[p] = addSegmented(pp(p, kEngine), { kEngineKeys, kEngineKeys + kNumEngines });
            engSeg[p]->tip = "Modal objects · a waveguide string · an analog circuit · three-operator FM · west-coast fold and gate · additive partials · chaos";
            for (int f = kPitch; f < kPerPad; ++f) edit[p][f] = addDial(pp(p, f), lab[f], Dial::Medium, f == kPan || f == kLevel);
            edit[p][kBend]->tip = "The pitch starts this many semitones up and falls back in B-TIME";
            edit[p][kNoise]->tip = "Noise that bursts and then rattles with the body (snare wires, sizzle, snap)";
        }
        auto dial = [&](int p, const char* l, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Medium); if (tip) d->tip = tip; return d; };
        velD = dial(kVelocity, "VELOCITY"); driveD = dial(kDrive, "DRIVE"); volD = addDial(kVolume, "VOLUME", Dial::Medium, true);
        chokeB = addToggle(kChoke, "CHOKE");
        chokeB->tip = "The closed hat cuts the open hat";
        followB = addToggle(kFollow, "FOLLOW"); followB->tip = "Show the pad you play: every hit (MIDI, keys, a click) selects it for editing";
        if (auto* p = static_cast<PluginStrike*>(pluginPtr())) plug = p;
        select(0);
    }

protected:
    // what TONE, CHAR A and CHAR B do in each engine
    static constexpr const char* kToneName[kNumEngines] = { "HARDNESS", "BRIGHT", "PULSE", "INDEX", "RING", "TILT", "CENTRE" };
    static constexpr const char* kAName[kNumEngines] = { "OBJECT", "STIFFNESS", "SQUEEZE", "RATIO", "FOLD", "DENSITY", "CHAOS" };
    static constexpr const char* kBName[kNumEngines] = { "MATERIAL", "EXCITE", "SECOND", "FEEDBACK", "SYMMETRY", "SHIMMER", "RESONANCE" };
    static constexpr const char* kToneTip[kNumEngines] = { "The mallet: soft felt (0) to a hard stick (100)", "The loop's brightness: how fast the highs die", "The pulse that pings the circuit: dull to bright",
        "The FM depth (it falls faster than the level)", "How bright the gate opens", "The partials' tilt: dark to bright", "The band-pass's centre" };
    static constexpr const char* kATip[kNumEngines] = { "The object: membrane (0-24), plate (25-49), bar (50-74), bell (75-100)", "Dispersion in the loop: toward a bell or a steel drum",
        "The pitch rises with the level, as the circuit's does", "The modulator's ratio to the pitch", "How hard the triangle folds", "A few bell-like partials (0) to a dense cymbal (100)", "From a clean orbit to chaos" };
    static constexpr const char* kBTip[kNumEngines] = { "Wood (0: the upper modes die fast) to metal (100)", "A click (0) to a burst of noise (100)", "A second resonator a fifth up (cowbell, clave)",
        "The third operator's feedback: toward noise", "The fold's bias: even harmonics", "How differently the partials die", "The band-pass's resonance" };

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i < kPads * kPerPad)
        {
            const int f = i % kPerPad, p = i / kPerPad;
            if (f == kPitch)
            {
                static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                const int n = (int)std::lround(v); std::snprintf(b, sizeof b, "%s%d", nn[((n % 12) + 12) % 12], n / 12 - 1); return b;
            }
            if (f == kPan) { if (std::fabs(v) < 0.5f) return "C"; std::snprintf(b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v)); return b; }
            if (f == kBend) { if (v < 0.05f) return "OFF"; std::snprintf(b, sizeof b, "+%.0f st", v); return b; }
            if (f == kCharA && (int)value(pp(p, kEngine)) == kModal) { static const char* const o[4] = { "MEMBRANE", "PLATE", "BAR", "BELL" }; return o[std::min(3, (int)(v / 25.0f))]; }
            if (f == kCharA && (int)value(pp(p, kEngine)) == kFm) { std::snprintf(b, sizeof b, "x%.2f", 0.5f + 7.5f * (v * 0.01f) * (v * 0.01f)); return b; }
        }
        return PumpUI::formatParam(i, v);
    }
    void select(int p)
    {
        sel = pump::clampv(p, 0, kPads - 1);
        for (int q = 0; q < kPads; ++q)
        {
            engSeg[q]->visible = q == sel;
            for (int f = kPitch; f < kPerPad; ++f) edit[q][f]->visible = q == sel;
        }
    }

    void layout() override
    {
        // top: the pads, the whole width, each with its engine moving in it
        const float top = 58, sh = 216, hm = Dial::heightFor(Dial::Medium);
        padsR = Rect(14, top, 972, sh);
        const float pw = (padsR.w - 20 - 3 * 8) / 4, ph = (sh - 28 - 10 - 8) / 2;
        for (int p = 0; p < kPads; ++p) padR[p] = Rect(padsR.x + 10 + (p % 4) * (pw + 8), top + 28 + (p / 4) * (ph + 8), pw, ph);
        // the selected pad: engine, then its ten dials in one row
        const float ey = top + sh + 12, eh = 150;
        editR = Rect(14, ey, 972, eh);
        static const int order[10] = { kPitch, kDecay, kTone, kCharA, kCharB, kBend, kBendTime, kNoise, kLevel, kPan };
        for (int p = 0; p < kPads; ++p)
        {
            engSeg[p]->r = Rect(26, ey + 28, 948, 24);
            for (int k = 0; k < 10; ++k) edit[p][order[k]]->r = Rect(26 + k * 94.8f, ey + 62, 94.8f, hm);
        }
        followB->r = Rect(866, ey + 4, 110, 20);
        // KIT (velocity, drive, volume, choke) beside the scope
        const float ky = ey + eh + 12, kh = 116;
        kitR = Rect(14, ky, 560, kh);
        Dial* kd[3] = { velD, driveD, volD };
        for (int k = 0; k < 3; ++k) kd[k]->r = Rect(26 + k * 128, ky + 28, 128, hm);
        chokeB->r = Rect(422, ky + 52, 140, 26);
        scopeR = Rect(586, ky, 372, kh);
        meterL = Rect(964, ky, 8, kh - 12); meterR = Rect(975, ky, 8, kh - 12);
    }

    void tick(float dt) override
    {
        // TONE / CHAR A / CHAR B are named for the selected pad's engine
        const int eng = pump::clampv((int)value(pp(sel, kEngine)), 0, kNumEngines - 1);
        edit[sel][kTone]->label = kToneName[eng]; edit[sel][kTone]->tip = kToneTip[eng];
        edit[sel][kCharA]->label = kAName[eng]; edit[sel][kCharA]->tip = kATip[eng];
        edit[sel][kCharB]->label = kBName[eng]; edit[sel][kCharB]->tip = kBTip[eng];
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        // FOLLOW: a pad played (MIDI, keys or a click) becomes the one being edited (not while a control is dragged)
        if (plug)
        {
            const uint32_t lh = plug->engine.lastHit();
            if (lh != lastHitSeen) { lastHitSeen = lh; if (lh >> 8 && value(kFollow) >= 0.5f && !dragging()) select((int)(lh & 0xFF)); }
        }
        const uint32_t hits = plug->engine.takeHits();
        anim += dt;
        for (int p = 0; p < kPads; ++p)
        {
            if (hits >> p & 1) glow[p] = 1.0f; else glow[p] = std::max(0.0f, glow[p] - dt * 4.0f);
            // how hard the pad is ringing, 0..1 over 42 dB: what the motion in the pad follows
            const float db = pump::gainToDbF(plug->engine.padLevel(p));
            const float e = pump::clampv((db + 42.0f) / 42.0f, 0.0f, 1.0f);
            ring[p] = e > ring[p] ? e : ring[p] + (e - ring[p]) * std::min(1.0f, dt * 12.0f);
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
    }

    // The pad's engine as ORBIT draws its attractors: a path through the engine's own phase space, turning slowly in
    // 3-D, traced by a bright head with a comet trail (older parts thinner and fainter) while the pad rings; the whole
    // path stays faintly in view when it's quiet. e: 0 quiet .. 1 just hit. Each engine's path is what it does:
    //  MODAL   a rim flexing in its first modes (CHAR A: membrane round, plate square, bar long, bell squat and deep)
    //  STRING  the standing wave, its first three harmonics swinging out of the plane
    //  CIRCUIT the resonator's ring-down: a spiral falling into the centre, a tight coil when quiet
    //  FM      carrier against modulator, a knot that tangles as the index opens (CHAR A: RATIO sets its lobes)
    //  WEST    a sine round the folder, folding over on itself harder with FOLD (CHAR A) and the level
    //  ADDITIVE the inharmonic partials summed as a spirograph, more of them with DENSITY (CHAR A)
    //  CHAOS   the Lorenz attractor itself
    void drawMotion(int p, const Rect& a, float e)
    {
        const int eng = pump::clampv((int)value(pp(p, kEngine)), 0, kNumEngines - 1);
        const float ca = value(pp(p, kCharA)) * 0.01f, t = anim, seed = p * 1.3f;
        const float cx = a.cx(), cy = a.cy(), sc = std::min(a.w * 0.36f, a.h * 0.44f);
        const float pi = 3.14159265f, tau = 2.0f * pi;
        const int obj = std::min(3, (int)(ca * 4.0f));
        const float ratio = 1.0f + std::round(ca * 4.0f), ix = 0.6f + 3.4f * e, drive = 0.9f + (1.5f + 4.0f * ca) * e;
        const int nPart = 3 + (int)(ca * 5.0f);
        const auto& L = lorenz();
        auto wrap = [](float v, float m) { const float r = std::fmod(v, m); return r < 0.0f ? r + m : r; };
        // the point of the engine's path at s (one lap = tau; CHAOS: along the attractor)
        auto at = [&](float s, float& x, float& y, float& z) {
            switch (eng)
            {
            case kModal:
            {
                const float th = s, d = 0.12f + 0.88f * e;
                float r = 1.0f + d * (0.26f * std::cos(2 * th + seed) * std::cos(t * 11) + 0.14f * std::cos(3 * th) * std::cos(t * 17 + seed));
                if (obj == 1) r /= std::max(std::fabs(std::cos(th)), std::fabs(std::sin(th))) * 1.15f;
                const float sx = obj == 2 ? 1.5f : 1.0f, sy = obj == 2 ? 0.28f : obj == 3 ? 0.72f : 1.0f;
                x = r * std::cos(th) * sx; z = r * std::sin(th) * sy;
                y = d * 0.55f * std::sin(2 * th + seed) * std::cos(t * 11) + (obj == 3 ? 0.35f * (1.0f - std::fabs(std::cos(th))) : 0.0f);
                break;
            }
            case kString:
            {
                const float u = 0.5f + 0.5f * std::sin(s), d = 0.1f + 0.9f * e;   // back and forth along the string
                x = (u - 0.5f) * 2.6f;
                y = -d * (std::sin(pi * u) * std::cos(t * 9) + 0.45f * std::sin(2 * pi * u) * std::cos(t * 15 + 1) + 0.25f * std::sin(3 * pi * u) * std::cos(t * 21 + 2));
                z = d * 0.8f * (std::sin(pi * u) * std::sin(t * 9) - 0.4f * std::sin(2 * pi * u) * std::sin(t * 15));
                break;
            }
            case kCircuit:
            {
                const float k = wrap(s, tau) / tau;                    // one ring-down per lap: 7 turns, falling inwards
                const float r = (0.12f + 0.88f * e) * std::exp(-2.6f * k), th = k * 7.0f * tau - t * 4.0f;
                x = r * std::cos(th) * 1.3f; z = r * std::sin(th); y = 0.9f * (k - 0.5f) * (0.3f + 0.7f * e);
                break;
            }
            case kFm:
            {
                const float amp = 0.3f + 0.7f * e;
                x = amp * 1.2f * std::sin(s); y = amp * std::sin(ratio * s + ix * std::sin(3 * s + t * 2)); z = amp * std::cos(s + 0.6f * std::sin(ratio * s));
                break;
            }
            case kWest:
            {
                float v = std::sin(1.5f * s + t * 3) * drive;
                for (int k = 0; k < 8 && (v > 1.0f || v < -1.0f); ++k) v = v > 1.0f ? 2.0f - v : -2.0f - v;
                const float amp = 0.35f + 0.65f * e;
                x = 1.25f * std::cos(s); z = std::sin(s); y = -amp * 0.9f * v;
                break;
            }
            case kAdditive:
            {
                x = y = z = 0.0f; float tot = 0.0f;
                for (int i = 0; i < nPart; ++i)
                {
                    const float f = 1.0f + i * (1.41f + 0.37f * hash01(i + p * 7)), a0 = 1.0f / (1.0f + 0.8f * i);
                    const float ph = f * s + t * (0.5f + i * 0.3f);
                    x += a0 * std::cos(ph); z += a0 * std::sin(ph); y += a0 * std::sin(f * 0.5f * s - t) * 0.6f; tot += a0;
                }
                const float amp = (0.35f + 0.65f * e) * 1.15f / tot;
                x *= amp; y *= amp; z *= amp;
                break;
            }
            default:
            {
                const float n = (float)L.size(), u = wrap(s / tau * 0.25f, 1.0f) * (n - 1);
                const int i0 = std::min((int)u, (int)n - 1); const float fr = u - i0; const int i1 = std::min((int)n - 1, i0 + 1);
                const float amp = 0.45f + 0.55f * e;
                x = amp * 1.3f * (L[(size_t)i0][0] * (1 - fr) + L[(size_t)i1][0] * fr);
                y = amp * (L[(size_t)i0][1] * (1 - fr) + L[(size_t)i1][1] * fr);
                z = amp * 1.3f * (L[(size_t)i0][2] * (1 - fr) + L[(size_t)i1][2] * fr);
                break;
            }
            }
        };
        // turning slowly about the vertical, tipped toward the viewer, with a little perspective (as ORBIT)
        const float rot = t * 0.35f + seed, cr = std::cos(rot), sr = std::sin(rot), tilt = 0.42f, ct = std::cos(tilt), st = std::sin(tilt);
        auto proj = [&](float s, float& X, float& Y, float& D) {
            float x, y, z; at(s, x, y, z);
            const float x1 = x * cr + z * sr, z1 = -x * sr + z * cr;
            const float y2 = y * ct - z1 * st, z2 = y * st + z1 * ct;
            const float k = 1.0f / (1.0f + 0.18f * z2);
            X = cx + sc * x1 * k; Y = cy + sc * y2 * k; D = z2;
        };
        const bool chaos = eng == kChaos;
        const float lap = chaos ? 4.0f * tau : tau;                         // the whole path (CHAOS: the attractor)
        const float speed = chaos ? 2.4f : eng == kCircuit ? 0.9f : eng == kString ? 2.2f : 1.6f;
        const float head = t * speed * (0.6f + 0.8f * e) + seed;
        const Color hot = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        save(); scissor(a.x, a.y, a.w, a.h);
        lineJoin(ROUND); lineCap(ROUND);
        // the whole path, faint
        {
            const int n = chaos ? 260 : 140;
            beginPath();
            for (int i = 0; i <= n; ++i) { float X, Y, D; proj(head - lap * i / n, X, Y, D); if (!i) moveTo(X, Y); else lineTo(X, Y); }
            strokeColor(withAlpha2(hot, 0.16f + 0.14f * e)); strokeWidth(0.8f); stroke();
        }
        // the comet: in pieces, older fainter and thinner; longer while the pad rings
        const float live = std::max(e, 0.12f);
        const float span = lap * (chaos ? 0.12f + 0.3f * e : 0.3f + 0.55f * e);
        const int pieces = 24, per = 4;
        for (int k = 0; k < pieces; ++k)
        {
            const float age = (k + 1.0f) / pieces;   // 1 = the newest piece
            beginPath();
            for (int j = 0; j <= per; ++j)
            {
                const float s = head - span * (1.0f - (k * per + j) / (float)(pieces * per));
                float X, Y, D; proj(s, X, Y, D);
                if (!j) moveTo(X, Y); else lineTo(X, Y);
            }
            strokeColor(withAlpha2(hot, live * (0.06f + 0.94f * age * age))); strokeWidth(0.6f + 1.6f * age * (0.5f + 0.5f * e)); stroke();
        }
        float X, Y, D; proj(head, X, Y, D);
        beginPath(); circle(X, Y, 1.6f + 2.2f * e); fillColor(withAlpha2(theme.ink, 0.35f + 0.65f * live)); fill();
        restore();
    }
    static float hash01(int n) { const float x = std::sin(n * 12.9898f) * 43758.5453f; return x - std::floor(x); }
    static const std::vector<std::array<float, 3>>& lorenz()
    {
        static const std::vector<std::array<float, 3>> pts = [] {
            std::vector<std::array<float, 3>> v;
            double x = 0.1, y = 0.0, z = 0.0;
            for (int i = 0; i < 1600; ++i)
            {
                const double dx = 10 * (y - x), dy = x * (28 - z) - y, dz = x * y - 8.0 / 3.0 * z;
                x += dx * 0.006; y += dy * 0.006; z += dz * 0.006;
                if (i > 300 && i % 3 == 0) v.push_back({ (float)(x / 22.0), (float)((z - 25.0) / 22.0), (float)(y / 22.0) });
            }
            return v;
        }();
        return pts;
    }
    void drawPad(int p)
    {
        const Rect& r = padR[p];
        const float g = pump::clampv(glow[p], 0.0f, 1.0f);
        fillRect(r, pump::mixColor(theme.panel2, theme.neon, 0.15f + 0.85f * std::sqrt(g)));
        strokeRect(r, p == sel ? theme.ink : theme.dim, p == sel ? 2.0f : 1.0f);
        const Color ink = g > 0.45f ? theme.neonInk : theme.ink;
        char b[32];
        std::snprintf(b, sizeof b, "%d", p + 1);
        textAt(r.x + 8, r.y + 12, b, kMono, 10.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(r.x + 8, r.y + 32, kPadInfo[p].key, kLabel, 12.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(r.x + 8, r.y + 50, kEngineKeys[pump::clampv((int)value(pp(p, kEngine)), 0, kNumEngines - 1)], kLabel, 8.5f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        // the note that plays it: name and number
        const int note = kPadInfo[p].note;
        pump::padNoteName(note, b);   // as Renoise writes it (C-4 = 48)
        textAt(r.x + 8, r.b() - 10, b, kMono, 10.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, "%d", note);
        textAt(r.x + 38, r.b() - 10, b, kMono, 9.0f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        drawMotion(p, Rect(r.x + 96, r.y + 6, r.w - 104, r.h - 12), ring[p]);
    }
    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        float mx = 1e-3f; for (int i = 0; i < kScopeN; ++i) mx = std::max(mx, std::fabs(scope[i]));
        const float want = 0.9f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        save(); scissor(p.x, p.y, p.w, p.h);
        beginPath();
        for (int i = 0; i < kScopeN; i += 4)
        {
            const float x = p.x + p.w * i / (kScopeN - 1), y = p.cy() - scope[i] * scale * p.h * 0.48f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.4f); lineJoin(ROUND); stroke();
        restore();
    }

    void drawBody() override
    {
        screenBox(padsR);
        caption(padsR.x + 10, padsR.y + 14, "PADS");
        textAt(padsR.r() - 10, padsR.y + 14, "CLICK OR 1-8 · HIGHER = HARDER · EACH PAD MOVES AS IT RINGS", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        for (int p = 0; p < kPads; ++p) drawPad(p);
        strokeRect(editR, theme.dim, 1.0f);
        char b[64];
        std::snprintf(b, sizeof b, "PAD %d · %s", sel + 1, kPadInfo[sel].key);
        caption(editR.x + 12, editR.y + 16, b);
        strokeRect(kitR, theme.dim, 1.0f); caption(kitR.x + 12, kitR.y + 16, "KIT");
        drawScope();
        drawOutMeter(outMeter, meterL, meterR);
    }

    void play(int p, float vel)
    {
        select(p);
        if (plug) plug->keys.push((uint8_t)p, (uint8_t)pump::clampv((int)std::lround(vel * 127.0f), 1, 127));
    }
    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        for (int p = 0; p < kPads; ++p)
            if (padR[p].contains(x, y)) { play(p, pump::clampv(1.0f - (y - padR[p].y) / padR[p].h * 0.75f, 0.2f, 1.0f)); return true; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        for (int p = 0; p < kPads; ++p) if (padR[p].contains(x, y)) { setStatus(std::string("Play and edit ") + kPadInfo[p].name + " (higher in the pad = harder)"); return; }
    }
    bool keyPress(uint key, uint mod) override
    {
        if (mod & (kModifierControl | kModifierSuper | kModifierAlt)) return false;
        if (key >= '1' && key <= '8') { play((int)(key - '1'), 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Click a pad / keys 1-8", "play it and edit it (higher in the pad = harder)" }, { "MIDI", "C-4 (48) up the white keys: C-4 kick, D-4 snare, E-4 rim ... C-5 cymbal; the General MIDI drum notes below play them too" },
                 { "TONE / CHAR A / CHAR B", "named for what they do in the pad's engine" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "PAD %d %s   %s", sel + 1, kPadInfo[sel].key, kEngineKeys[pump::clampv((int)value(pp(sel, kEngine)), 0, kNumEngines - 1)]);
        return b;
    }

private:
    static constexpr int kScopeN = 4096;
    PluginStrike* plug = nullptr;
    Rect padsR, padR[kPads], editR, kitR, scopeR, meterL, meterR;
    Segmented* engSeg[kPads];
    Dial* edit[kPads][kPerPad] = {};
    Dial *velD, *driveD, *volD;
    Button *chokeB, *followB;
    uint32_t lastHitSeen = 0;
    int sel = 0;
    float glow[kPads] = {}, ring[kPads] = {}, scope[kScopeN] = {}, scale = 1.0f, anim = 0.0f;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIStrike)
};

UI* createUI() { return new UIStrike(); }

END_NAMESPACE_DISTRHO
