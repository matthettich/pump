// pump ACID: interface. Gray, with colour where it plays: the VIEWER (the output in the colour of the note sounding,
// the filter envelope, the accent charge and the amp as coloured bars), the FILTER (the cutoff where it is now; drag:
// left / right = cutoff, up / down = resonance) and the STEP (the note playing, big, in its colour). Then the voice:
// the model and its controls. Then the SEQUENCER: a piano roll of the sixteen steps (every note in the colour of its
// pitch, accents bright and tall, slides drawn into the next note, ties drawn on), the accent and slide rows, the step
// numbers (the playhead; click one to end the pattern there); beside it the sequencer's mode, PLAY, rate, length,
// swing, gate and transpose, and the RANDOM generator (which lanes it rolls, the scale and root, how dense, how many
// accents, slides and octave jumps). The strip of keys at the left of the roll plays the voice.
#include "ui/PumpUI.hpp"
#include "PluginAcid.hpp"
#include <chrono>
#include <complex>

START_NAMESPACE_DISTRHO

using namespace pump::acid;

class UIAcid : public PumpUI {
public:
    UIAcid() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::acid(), "ACID", 1000, 700, -1)
    {
        model = addSegmented(kModel, { "ACID", "SOLO", "POCKET", "STATION", "SCREAM" });
        model->tip = "Acid: diode ladder, accent and slide · Solo: saw / pulse / sub, OTA filter · Pocket: three detuned oscillators, two-pole · Station: two oscillators, hot ladder · Scream: clipping two-pole";
        const struct { int p; const char* l; bool bip; const char* tip; } main[] = {
            { kTune, "TUNE", true, "Tune in semitones (fine in between)" },
            { kWave, "WAVE", false, "Waveform: saw (0) to square (100), crossfaded" },
            { kCutoff, "CUTOFF", false, "Cutoff frequency" },
            { kRes, "RESO", false, "Resonance" },
            { kEnvMod, "ENV MOD", false, "How far the filter envelope opens the filter" },
            { kDecay, "DECAY", false, "Filter envelope decay (Acid: accented notes are always fast; the others: the ADSR's decay)" },
            { kAccent, "ACCENT", false, "Accent: how much accented steps open the filter (it climbs over consecutive accents) and lift the level" },
            { kSlide, "SLIDE", false, "Slide time: how long a slide takes to reach the next note" },
            { kDrive, "DRIVE", false, "Drive: distortion after the filter" },
            { kVolume, "VOLUME", true, "Volume" },
        };
        for (const auto& m : main) { Dial* d = addDial(m.p, m.l, Dial::Small, m.bip); d->tip = m.tip; mainD.push_back(d); }
        const struct { int p; const char* l; const char* tip; } more[] = {
            { kAttack, "ATTACK", "Envelope attack (not Acid)" },
            { kSustain, "SUSTAIN", "Envelope sustain (not Acid)" },
            { kRelease, "RELEASE", "Envelope release (not Acid)" },
            { kPw, "PW", "Pulse width of the square (Solo; a little on the others)" },
            { kSub, "SUB", "Sub oscillator: a square an octave down (not Acid)" },
            { kNoise, "NOISE", "Noise into the filter (not Acid)" },
            { kOsc2, "OSC 2", "The second oscillators' level (Pocket: two, detuned either side; Station: one)" },
            { kDetune, "DETUNE", "Detune of the second oscillators, in cents" },
            { kKeyTrack, "KEY", "Key tracking: the cutoff follows the note" },
            { kLfoRate, "LFO", "LFO rate" },
            { kLfoPitch, "PITCH", "LFO to pitch (vibrato)" },
            { kLfoCutoff, "L>CUT", "LFO to cutoff" },
            { kLfoPw, "PWM", "LFO to pulse width" },
            { kAccentVel, "ACC VEL", "MIDI notes at or above this velocity are accented" },
            { kBendRange, "BEND", "Pitch bend range in semitones" },
        };
        for (const auto& m : more) { Dial* d = addDial(m.p, m.l, Dial::Small); d->tip = m.tip; moreD.push_back(d); }
        slopeSeg = addSegmented(kSlope, { "24", "12" });
        slopeSeg->tip = "Filter slope in dB per octave (Solo and Station)";
        lfoSeg = addSegmented(kLfoShape, { "TRI", "SQR", "SAW", "S&H" });
        lfoSeg->tip = "LFO shape";

        // the sequencer
        modeSeg = addSegmented(kSeqMode, { "OFF", "SONG", "KEYS", "FREE" });
        modeSeg->tip = "Off: MIDI plays the voice · Song: the pattern plays with the host · Keys: while a key is held, transposed by it (C-4 = as written) · Free: always, at the host tempo";
        playBtn = addButton("PLAY", [this]() { if (plug) plug->preview.store(!plug->preview.load()); }, [this]() { return plug && plug->preview.load(); });
        playBtn->small = true;
        playBtn->tip = "Play the pattern now, at the host tempo (Space)";
        rateMenu = addMenu(kSeqRate, "RATE");
        rateMenu->arrows = true;
        rateMenu->tip = "Step length (T = triplets)";
        const struct { int p; const char* l; const char* tip; } rows[] = {
            { kSeqLength, "LENGTH", "Pattern length in steps (or click a step number)" },
            { kSwing, "SWING", "Swing: every second step starts later (33 % = a triplet shuffle)" },
            { kGateLen, "GATE", "How long a note's gate stays open within its step (ties and slides hold it)" },
            { kTranspose, "TRANSP", "Transpose the pattern in semitones" },
        };
        for (const auto& m : rows) { SliderRow* r = addRow(m.p, m.l, m.p == kTranspose); r->labelW = 52; r->valueW = 40; r->tip = m.tip; seqRows.push_back(r); }
        static const char* const laneNames[5] = { "NOTE", "OCT", "ACC", "SLIDE", "TIME" };
        static const char* const laneTips[5] = { "Random rolls the notes (pitch within the octave)", "Random rolls the octave jumps", "Random rolls the accents",
                                                 "Random rolls the slides", "Random rolls the rhythm: which steps play, rest or tie" };
        for (int k = 0; k < 5; ++k)
        {
            Button* b = addButton(laneNames[k], [this, k]() { lanes ^= 1u << k; if (!lanes) lanes = 1u << k; saveUiState(uiString()); }, [this, k]() { return (lanes >> k) & 1u; });
            b->small = true; b->tip = laneTips[k];
            laneBtn[k] = b;
        }
        scaleMenu = addMenu(kRndScale);
        scaleMenu->tip = "The scale random notes come from";
        rootMenu = addMenu(kRndRoot);
        rootMenu->tip = "The root of the scale (the first step always plays it)";
        const struct { int p; const char* l; const char* tip; } rnd[] = {
            { kRndDensity, "DENSITY", "How many steps play (the rest are rests and ties)" },
            { kRndAccent, "ACCENTS", "How many notes are accented" },
            { kRndSlide, "SLIDES", "How many notes slide into the next" },
            { kRndOctave, "OCTAVES", "How many notes jump an octave (mostly up, likelier on accents)" },
        };
        for (const auto& m : rnd) { SliderRow* r = addRow(m.p, m.l); r->labelW = 60; r->valueW = 40; r->tip = m.tip; rndRows.push_back(r); }
        randomBtn = addButton("RANDOM", [this]() { patternOp(kOpRandom); });
        randomBtn->tip = "A new pattern: rolls the lanes lit below (R)";
        mutateBtn = addButton("MUTATE", [this]() { patternOp(kOpMutate); });
        mutateBtn->tip = "Rolls the lit lanes again on a few steps only (M)";
        clearBtn = addButton("CLEAR", [this]() { patternOp(kOpClear); });
        clearBtn->tip = "Every step back to the root, no accents or slides";
        leftBtn = addButton("<", [this]() { patternOp(kOpLeft); });
        leftBtn->tip = "Shift the pattern one step earlier (Left)";
        rightBtn = addButton(">", [this]() { patternOp(kOpRight); });
        rightBtn->tip = "Shift the pattern one step later (Right)";
        downBtn = addButton("-1", [this]() { patternOp(kOpDown); });
        downBtn->tip = "Every note a semitone down (Down)";
        upBtn = addButton("+1", [this]() { patternOp(kOpUp); });
        upBtn->tip = "Every note a semitone up (Up)";
        for (Button* b : { randomBtn, mutateBtn, clearBtn, leftBtn, rightBtn, downBtn, upBtn }) b->small = true;

        if (auto* p = static_cast<PluginAcid*>(pluginPtr())) plug = p;
    }
    ~UIAcid() override { releaseAllHeld(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kTune) { std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.005f ? "%+.0f st" : "%+.2f st", v); return std::fabs(v) < 0.005f ? std::string("0 st") : std::string(b); }
        if (i == kTranspose) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kDetune) { std::snprintf(b, sizeof b, "%.0f ct", v); return b; }
        if (i == kBendRange) { std::snprintf(b, sizeof b, "%.0f st", v); return b; }
        if (i >= kStep1 && (i - kStep1) % 4 == kStepNote) return noteText((int)std::lround(v));
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 56, vh = 140;
        viewR = Rect(14, top, 560, vh);
        filtR = Rect(586, top, 250, vh);
        stepR = Rect(848, top, 118, vh);
        meterR = Rect(974, top, 12, vh);
        // the voice
        voiceP = Rect(14, 208, 972, 162);
        model->r = Rect(120, 214, 420, 20);
        const float dh = Dial::heightFor(Dial::Small);
        for (size_t k = 0; k < mainD.size(); ++k) mainD[k]->r = Rect(22 + k * 66.0f, 240, 62, dh);
        // the second row: envelope, oscillators, filter, LFO
        float x = 22;
        for (size_t k = 0; k < moreD.size(); ++k)
        {
            if (k == 3 || k == 9 || k == 13) x += 12;   // gaps between the groups
            moreD[k]->r = Rect(x, 302, 60, dh);
            x += 60;
        }
        slopeSeg->r = Rect(666, 216, 70, 18);
        lfoSeg->r = Rect(812, 216, 162, 18);
        // the sequencer
        seqP = Rect(14, 380, 972, 290);
        const float bx = 120;
        randomBtn->r = Rect(bx, 385, 66, 18); mutateBtn->r = Rect(bx + 70, 385, 66, 18); clearBtn->r = Rect(bx + 140, 385, 60, 18);
        leftBtn->r = Rect(bx + 210, 385, 26, 18); rightBtn->r = Rect(bx + 240, 385, 26, 18);
        downBtn->r = Rect(bx + 276, 385, 30, 18); upBtn->r = Rect(bx + 310, 385, 30, 18);
        keysR = Rect(24, 412, 22, kRows * kRowH);
        rollR = Rect(50, 412, 704, kRows * kRowH);
        namesR = Rect(rollR.x, rollR.b() + 4, rollR.w, 15);
        accR = Rect(rollR.x, namesR.b() + 3, rollR.w, 15);
        slideR = Rect(rollR.x, accR.b() + 3, rollR.w, 15);
        numR = Rect(rollR.x, slideR.b() + 3, rollR.w, 15);
        // the right column
        const float cx = 770, cw = 206;
        modeSeg->r = Rect(cx, 410, cw, 20);
        playBtn->r = Rect(cx, 436, 60, 20);
        rateMenu->r = Rect(cx + 66, 436, cw - 66, 20);
        for (size_t k = 0; k < seqRows.size(); ++k) seqRows[k]->r = Rect(cx, 462 + k * 19.0f, cw, 16);
        for (int k = 0; k < 5; ++k) laneBtn[k]->r = Rect(cx + k * 41.6f, 556, 39, 18);
        scaleMenu->r = Rect(cx, 580, 140, 18);
        rootMenu->r = Rect(cx + 146, 580, cw - 146, 18);
        for (size_t k = 0; k < rndRows.size(); ++k) rndRows[k]->r = Rect(cx, 604 + k * 16.0f, cw, 14);
    }

    void tick(float dt) override
    {
        const int m = (int)value(kModel);
        const bool acidM = m == kAcid;
        for (int k : { 0, 1, 2 }) moreD[(size_t)k]->enabled = !acidM;   // attack, sustain, release
        moreD[3]->enabled = m == kSolo || (int)value(kWave) > 0;         // PW
        moreD[4]->enabled = moreD[5]->enabled = !acidM;                  // sub, noise
        moreD[6]->enabled = moreD[7]->enabled = m == kPocket || m == kStation;
        moreD[12]->enabled = m == kSolo;                                  // PWM
        slopeSeg->enabled = m == kSolo || m == kStation;
        playBtn->label = (plug && plug->preview.load()) ? "STOP" : "PLAY";
        if (!plug) return;
        const float pk = pump::gainToDbF(plug->outPeak.exchange(0.0f));
        lvl = std::max(pk, lvl - 26.0f * dt);
        if (pk >= hold) { hold = pk; holdT = 1.5f; } else if ((holdT -= dt) <= 0.0f) hold = std::max(pk, hold - 18.0f * dt);
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
        cutShow += (std::log2(std::max(15.0f, plug->engine.cutoffNow())) - cutShow) * std::min(1.0f, dt * 25.0f);
        pitchShow = plug->engine.pitchNow();
        gate = plug->engine.gateNow();
        envShow = plug->engine.filterEnvNow(); accShow = plug->engine.accentNow(); ampShow = plug->engine.ampNow();
        const int st = plug->engine.stepNow();
        if (st != playStep) { playStep = st; if (st >= 0) flash[st] = 1.0f; }
        for (float& f : flash) f = std::max(0.0f, f - dt * 3.0f);
    }

    // ---- drawing ----------------------------------------------------------------------------------------------------
    // the colour of a pitch class (C = red, round the wheel)
    Color noteColour(int note, bool forText = false) const
    {
        static const int idx[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 13 };
        const int pc = ((note % 12) + 12) % 12;
        return forText ? pump::wheelInk(idx[pc], theme.dark) : pump::wheelColour(idx[pc], false);
    }
    static std::string noteText(int rel)
    {
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int pc = ((rel % 12) + 12) % 12, oc = (int)std::floor(rel / 12.0);
        std::string s = nn[pc];
        if (oc > 0) s += std::string((size_t)oc, '\'');
        if (oc < 0) s += ",";
        return s;
    }

    void drawBody() override
    {
        drawViewer();
        drawFilter();
        drawStep();
        drawLevelMeter(meterR, lvl, hold, -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(voiceP, "VOICE");
        caption(model->r.x - 52, model->r.cy(), "MODEL");
        caption(slopeSeg->r.x - 40, slopeSeg->r.cy(), "SLOPE");
        caption(lfoSeg->r.x - 30, lfoSeg->r.cy(), "LFO");
        // the second row's groups: envelope | oscillators and filter | LFO
        line(moreD[3]->r.x - 6, 306, moreD[3]->r.x - 6, 364, theme.dim, 1.0f);
        line(moreD[9]->r.x - 6, 306, moreD[9]->r.x - 6, 364, theme.dim, 1.0f);
        line(moreD[13]->r.x - 6, 306, moreD[13]->r.x - 6, 364, theme.dim, 1.0f);
        panel(seqP, "SEQUENCER");
        drawRoll();
        caption(770, 548, "RANDOM");
    }

    void drawViewer()
    {
        const Rect& r = viewR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "VIEWER");
        const int t = pump::clampv((int)value(kModel), 0, kNumModels - 1);
        textAt(r.r() - 10, r.y + 14, kModelNames[t], kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        // the bars: filter envelope, accent charge, amp
        const Rect bars(r.r() - 74, r.y + 26, 64, r.h - 36);
        const struct { float v; int wheel; const char* l; } bv[3] = { { envShow, 1, "ENV" }, { accShow, 0, "ACC" }, { ampShow, 3, "AMP" } };
        for (int k = 0; k < 3; ++k)
        {
            const Rect b(bars.x + k * 22.0f, bars.y, 16, bars.h - 14);
            fillRect(b, theme.panel2);
            const float h = b.h * pump::clampv(bv[k].v, 0.0f, 1.0f);
            fillRect(Rect(b.x, b.b() - h, b.w, h), pump::wheelColour(bv[k].wheel, false));
            strokeRect(b, theme.ink, 1.0f);
            textAt(b.cx(), bars.b() - 4, bv[k].l, kLabel, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        }
        const Rect p(r.x + 10, r.y + 24, r.w - 96, r.h - 32);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        if (!plug) return;
        // two cycles of the note, triggered on a rising zero crossing, in the note's colour
        const double sr = plug->getSampleRate();
        const int cyc = pump::clampv((int)(sr / pump::synth::midiToHz(pump::clampv((double)pitchShow, 12.0, 120.0))), 8, kScopeN / 4);
        const int win = std::min(kScopeN / 2, 2 * cyc);
        int start = kScopeN - win - 1;
        for (int i = start; i > start - 2 * cyc && i > 1; --i) if (scope[i - 1] < 0.0f && scope[i] >= 0.0f) { start = i; break; }
        float mx = 1e-3f; for (int i = 0; i < win; ++i) mx = std::max(mx, std::fabs(scope[start + i]));
        const float want = 0.9f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        const Color c = noteColour((int)std::lround(pitchShow));
        save(); scissor(p.x, p.y, p.w, p.h);
        for (int pass = 0; pass < 2; ++pass)
        {
            beginPath();
            for (int i = 0; i < win; ++i)
            {
                const float x = p.x + p.w * i / (win - 1), y = p.cy() - scope[start + i] * scale * p.h * 0.48f;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            lineJoin(ROUND);
            if (pass == 0) { strokeColor(theme.dark ? withAlpha2(c, 0.25f) : withAlpha2(theme.ink, 0.18f)); strokeWidth(6.0f); }
            else { strokeColor(theme.dark ? c : pump::mixColor(c, theme.ink, 0.35f)); strokeWidth(2.2f); }
            stroke();
        }
        restore();
    }

    void drawFilter()
    {
        const Rect& r = filtR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "FILTER");
        const Rect p(r.x + 8, r.y + 24, r.w - 16, r.h - 34);
        drawFreqGrid(p, 20.0, 20000.0, false);
        const double fc = std::exp2((double)cutShow), res = value(kRes) * 0.01;
        const int t = (int)value(kModel);
        auto yOf = [&](double db) { return (float)(p.y + (24.0 - pump::clampv(db, -48.0, 24.0)) / 72.0 * p.h); };
        line(p.x, yOf(0), p.r(), yOf(0), withAlpha2(theme.ink, 0.25f), 1.0f);
        beginPath();
        for (int i = 0; i <= 120; ++i)
        {
            const double f = 20.0 * std::pow(1000.0, i / 120.0);
            const std::complex<double> s(0.0, f / fc);
            std::complex<double> H;
            if (t == kAcid)
            {
                // the diode ladder's own response (one octave of oversampling assumed: the shape, not the exact rate)
                double b, kc; const double osr = 96000.0;
                DiodeTable::get().lookup(pump::kTwoPi * pump::clampv(fc, 20.0, 17000.0) / osr, b, kc);
                const double rr = (1.0 - std::exp(-3.0 * res)) / (1.0 - std::exp(-3.0));
                const double k = 0.985 * rr * kc;
                const std::complex<double> ol = DiodeTable::openLoop(b, pump::kTwoPi * f / osr);
                const std::complex<double> sh = s * (fc / 110.0);
                const std::complex<double> hp = sh / (1.0 + sh);
                H = ol * (1.0 + 0.09 * k) / (1.0 + k * hp * ol);
            }
            else if (t == kSolo || t == kStation)
            {
                const auto g = 1.0 / (1.0 + s); const double k = 5.2 * res * (t == kSolo ? 0.98 : 0.95);
                H = (int)value(kSlope) == 1 ? g * g * (1.0 + 0.5 * k) / (1.0 + k * g * g * g * g) : g * g * g * g * (1.0 + 0.5 * k) / (1.0 + k * g * g * g * g);
            }
            else
            {
                const double kd = t == kPocket ? 2.0 - 1.9 * res : std::max(0.05, 2.0 * std::pow(1.0 - res, 1.5) - 0.15 * res);
                H = 1.0 / (s * s + s * kd + 1.0);
            }
            const float x = p.x + p.w * i / 120.0f, y = yOf(20.0 * std::log10(std::max(1e-9, std::abs(H))));
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.second); strokeWidth(2.2f); lineJoin(ROUND); stroke();
        const float cx = freqToX(p, pump::clampv(fc, 20.0, 20000.0));
        line(cx, p.y, cx, p.b(), withAlpha2(theme.ink, 0.6f), 1.2f);
        char b[32];
        if (fc >= 1000) std::snprintf(b, sizeof b, "%.2f kHz", fc / 1000); else std::snprintf(b, sizeof b, "%.0f Hz", fc);
        textAt(r.x + 10, r.b() - 8, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, "RES %.0f%%", value(kRes));
        textAt(r.r() - 10, r.b() - 8, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawStep()
    {
        const Rect& r = stepR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, playStep >= 0 ? "STEP" : "NOTE");
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = (int)std::lround(pitchShow);
        char b[24]; std::snprintf(b, sizeof b, "%s%d", nn[((n % 12) + 12) % 12], n / 12 - 1);
        const Rect big(r.x + 10, r.y + 26, r.w - 20, 50);
        const Color c = noteColour(n);
        fillRect(big, gate ? c : theme.panel);
        strokeRect(big, theme.ink, 1.5f);
        textAt(big.cx(), big.cy(), b, kBold, 22.0f, gate ? pump::hexColor(0x101014) : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        if (playStep >= 0) std::snprintf(b, sizeof b, "%d / %d", playStep + 1, (int)value(kSeqLength));
        else std::snprintf(b, sizeof b, "%s", kSeqModeNames[pump::clampv((int)value(kSeqMode), 0, kNumSeqModes - 1)]);
        std::string s = b; for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        textAt(r.cx(), big.b() + 16, s, kMono, 12.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        if (playStep >= 0)
        {
            const bool acc = value(stepParam(playStep, kStepAccent)) >= 0.5f, sl = value(stepParam(playStep, kStepSlide)) >= 0.5f;
            const Rect a(r.x + 10, big.b() + 30, (r.w - 26) / 2, 16), sR(a.r() + 6, a.y, a.w, 16);
            fillRect(a, acc ? kAccCol : theme.panel); strokeRect(a, theme.ink, 1.0f);
            textAt(a.cx(), a.cy(), "ACC", kLabel, 9.0f, acc ? pump::hexColor(0x101014) : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
            fillRect(sR, sl ? kSlideCol : theme.panel); strokeRect(sR, theme.ink, 1.0f);
            textAt(sR.cx(), sR.cy(), "SLIDE", kLabel, 9.0f, sl ? pump::hexColor(0x101014) : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        }
        std::snprintf(b, sizeof b, "LATENCY %d", plug ? plug->engine.latency() : 0);
        textAt(r.cx(), r.b() - 9, b, kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // ---- the piano roll ---------------------------------------------------------------------------------------------
    int stepCount() const { return pump::clampv((int)value(kSeqLength), 1, kSteps); }
    float colW() const { return rollR.w / kSteps; }
    float rowY(int note) const { return rollR.y + (kNoteHi - note) * kRowH; }   // top of a note's row
    int stepAtX(float x) const { return pump::clampv((int)((x - rollR.x) / colW()), 0, kSteps - 1); }
    int noteAtY(float y) const { return pump::clampv(kNoteHi - (int)((y - rollR.y) / kRowH), kNoteLo, kNoteHi); }
    int stepNote(int k) const { return (int)std::lround(value(stepParam(k, kStepNote))); }
    int stepGate(int k) const { return (int)std::lround(value(stepParam(k, kStepGate))); }
    // the note a step sounds (a tie: the note it holds), or the step's own note for a rest
    int soundingNote(int k) const
    {
        for (int j = 0, i = k; j < kSteps; ++j, i = (i + kSteps - 1) % kSteps) if (stepGate(i) == kNote) return stepNote(i);
        return stepNote(k);
    }

    void drawRoll()
    {
        const int L = stepCount();
        const float cw = colW();
        // the keys at the left (they play the voice)
        for (int n = kNoteLo; n <= kNoteHi; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const Rect kr(keysR.x, rowY(n), keysR.w, kRowH);
            fillRect(kr, n == auditioning ? noteColour(n) : (black ? theme.ink2 : theme.screen));
        }
        strokeRect(keysR, theme.ink, 1.0f);
        // the grid: C rows marked, the steps past the length dimmed, beats every four steps
        fillRect(rollR, theme.screen);
        for (int n = kNoteLo; n <= kNoteHi; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) fillRect(Rect(rollR.x, rowY(n), rollR.w, kRowH), withAlpha2(theme.ink, theme.dark ? 0.05f : 0.035f));
            if (pc == 0) line(rollR.x, rowY(n) + kRowH, rollR.r(), rowY(n) + kRowH, withAlpha2(theme.ink, n == 0 ? 0.35f : 0.18f), 1.0f);
        }
        for (int k = 0; k <= kSteps; ++k)
            line(rollR.x + k * cw, rollR.y, rollR.x + k * cw, rollR.b(), k % 4 == 0 ? withAlpha2(theme.ink, 0.25f) : theme.grid, 1.0f);
        if (L < kSteps) fillRect(Rect(rollR.x + L * cw, rollR.y, (kSteps - L) * cw, rollR.h), withAlpha2(theme.bg, 0.6f));
        // the playhead
        if (playStep >= 0)
        {
            fillRect(Rect(rollR.x + playStep * cw, rollR.y, cw, rollR.h), withAlpha2(noteColour(soundingNote(playStep)), 0.10f + 0.12f * flash[playStep]));
        }
        // the notes: a bar per step, through its ties; accents taller and outlined hard; slides drawn to the next note
        const float bh = kRowH + 4;
        for (int k = 0; k < kSteps; ++k)
        {
            const int g = stepGate(k);
            if (g == kRest) continue;
            const int n = g == kNote ? stepNote(k) : soundingNote(k);
            if (g == kTie && stepGate((k + kSteps - 1) % kSteps) == kRest) continue;   // a tie with nothing to hold
            const bool acc = g == kNote && value(stepParam(k, kStepAccent)) >= 0.5f;
            const bool dim = k >= L;
            const Color c = noteColour(n);
            const float x0 = rollR.x + k * cw + (g == kTie ? 0.0f : 2.0f), x1 = rollR.x + (k + 1) * cw - 2.0f;
            const float yc = rowY(n) + kRowH * 0.5f, h = acc ? bh + 4 : bh;
            const Rect nr(x0, yc - h * 0.5f, x1 - x0, h);
            fillRect(nr, withAlpha2(c, dim ? 0.35f : (g == kTie ? 0.65f : 1.0f)));
            strokeRect(nr, dim ? theme.dim : theme.ink, acc ? 2.0f : 1.0f);
            if (k < L && playStep == k) strokeRect(nr.inset(-2), theme.ink, 1.0f);
            // the slide: a line from this note into the next step's note
            if (value(stepParam(k, kStepSlide)) >= 0.5f && stepGate((k + 1) % kSteps) == kNote && k + 1 < kSteps)
            {
                const int n2 = stepNote(k + 1);
                const float y2 = rowY(n2) + kRowH * 0.5f;
                beginPath(); moveTo(x1 - 6, yc); bezierTo(x1 + 4, yc, x1 + 2, y2, x1 + 10, y2);
                strokeColor(dim ? theme.dim : kSlideColInk()); strokeWidth(2.5f); lineCap(ROUND); stroke();
            }
        }
        strokeRect(rollR, theme.ink, 1.0f);
        // the rows under the roll: names, accent, slide, numbers
        for (int k = 0; k < kSteps; ++k)
        {
            const float x = rollR.x + k * cw;
            const bool dim = k >= L;
            const int g = stepGate(k);
            const std::string nm = g == kRest ? "." : (g == kTie ? "-" : noteText(stepNote(k)));
            textAt(x + cw * 0.5f, namesR.cy(), nm, kBold, 11.0f, dim ? theme.dim : (g == kNote ? noteColour(stepNote(k), true) : theme.ink2), ALIGN_CENTER | ALIGN_MIDDLE);
            const bool acc = value(stepParam(k, kStepAccent)) >= 0.5f, sl = value(stepParam(k, kStepSlide)) >= 0.5f;
            const Rect a(x + 3, accR.y, cw - 6, accR.h), s(x + 3, slideR.y, cw - 6, slideR.h);
            fillRect(a, acc ? (dim ? withAlpha2(kAccCol, 0.4f) : kAccCol) : theme.panel); strokeRect(a, dim ? theme.dim : theme.ink, 1.0f);
            fillRect(s, sl ? (dim ? withAlpha2(kSlideCol, 0.4f) : kSlideCol) : theme.panel); strokeRect(s, dim ? theme.dim : theme.ink, 1.0f);
            const Rect nb(x + 3, numR.y, cw - 6, numR.h);
            const bool on = playStep == k;
            fillRect(nb, on ? theme.ink : (k < L ? theme.panel2 : theme.bg));
            char b[4]; std::snprintf(b, sizeof b, "%d", k + 1);
            textAt(nb.cx(), nb.cy(), b, kMono, 9.5f, on ? theme.on : (dim ? theme.dim : theme.ink), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        textAt(accR.x - 4, accR.cy(), "ACC", kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
        textAt(slideR.x - 4, slideR.cy(), "SLD", kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
        textAt(numR.x - 4, numR.cy(), "LEN", kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
        // the hovered cell
        if (hoverStep >= 0 && hoverNote > -100)
        {
            const Rect hr(rollR.x + hoverStep * cw, rowY(hoverNote), cw, kRowH);
            strokeRect(hr.inset(-1), withAlpha2(theme.ink, 0.6f), 1.0f);
        }
    }
    Color kSlideColInk() const { return theme.dark ? kSlideCol : pump::hexColor(0x00838F); }

    // ---- editing ------------------------------------------------------------------------------------------------------
    void setStep(int k, int field, float v) { setParamGesture(stepParam(k, field), v); }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft && button != kMouseButtonRight) return false;
        if (keysR.contains(x, y) && button == kMouseButtonLeft)
        {
            auditioning = kBaseNote + noteAtY(y);
            if (plug) plug->keys.push((uint8_t)auditioning, 100);
            auditioning -= kBaseNote;
            return true;
        }
        if (filtR.contains(x, y) && button == kMouseButtonLeft)
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kCutoff, spec(kCutoff).def); setParamGesture(kRes, spec(kRes).def); endGroup(); return false; }
            dragFilter = true; dx = x; dy = y; dCut = norm(kCutoff); dRes = norm(kRes);
            beginEdit(kCutoff); beginEdit(kRes);
            return true;
        }
        if (rollR.contains(x, y))
        {
            const int k = stepAtX(x), n = noteAtY(y);
            const int g = stepGate(k);
            beginGroup();
            if (button == kMouseButtonRight || (mod & kModifierShift))
            {
                // a tie (or back to a rest)
                setStep(k, kStepGate, g == kTie ? (float)kRest : (float)kTie);
                endGroup();
                setStatus(stepStatus(k));
                return true;
            }
            if (mod & kModifierAlt) { setStep(k, kStepSlide, value(stepParam(k, kStepSlide)) >= 0.5f ? 0.0f : 1.0f); endGroup(); setStatus(stepStatus(k)); return true; }
            if (g == kNote && stepNote(k) == n)
            {
                // on the note: drag it up and down, or a click without moving makes it a rest
                dragRoll = true; dragStep = k; dragMoved = false; dragY0 = y; dragNote0 = n;
                return true;
            }
            setStep(k, kStepNote, (float)n);
            setStep(k, kStepGate, (float)kNote);
            dragRoll = true; dragStep = k; dragMoved = true; dragY0 = y; dragNote0 = n;
            if (plug && value(kSeqMode) != kSeqKeys && !plug->preview.load() && playStep < 0) { plug->keys.push((uint8_t)(kBaseNote + n), 100); auditioning = n; }
            setStatus(stepStatus(k));
            return true;
        }
        auto toggleRow = [&](const Rect& row, int field) {
            if (!row.contains(x, y)) return false;
            const int k = stepAtX(x);
            beginGroup(); setStep(k, field, value(stepParam(k, field)) >= 0.5f ? 0.0f : 1.0f); endGroup();
            paintField = field; paintValue = value(stepParam(k, field)); paintLast = k;
            setStatus(stepStatus(k));
            return true;
        };
        if (toggleRow(accR, kStepAccent) || toggleRow(slideR, kStepSlide)) return true;
        if (numR.contains(x, y)) { setParamGesture(kSeqLength, (float)(stepAtX(x) + 1)); setStatus(paramStatus(kSeqLength)); return true; }
        if (namesR.contains(x, y))
        {
            // the name row: cycle a step note -> tie -> rest
            const int k = stepAtX(x), g = stepGate(k);
            setStep(k, kStepGate, (float)(g == kNote ? kTie : (g == kTie ? kRest : kNote)));
            setStatus(stepStatus(k));
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (dragFilter)
        {
            const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
            setParamNorm(kCutoff, dCut + (x - dx) / filtR.w * f);
            setParamNorm(kRes, dRes + (dy - y) / filtR.h * f);
            setStatus(paramStatus(kCutoff) + "    " + paramStatus(kRes));
            return;
        }
        if (dragRoll)
        {
            const int n = pump::clampv(dragNote0 + (int)std::lround((dragY0 - y) / kRowH), kNoteLo, kNoteHi);
            if (n != stepNote(dragStep) || std::fabs(y - dragY0) > 3) dragMoved = true;
            if (n != stepNote(dragStep))
            {
                setStep(dragStep, kStepNote, (float)n);
                if (auditioning != -1000 && plug) { plug->keys.push((uint8_t)(kBaseNote + auditioning), 0); plug->keys.push((uint8_t)(kBaseNote + n), 100); auditioning = n; }
            }
            setStatus(stepStatus(dragStep));
            return;
        }
        if (paintField >= 0)
        {
            const Rect& row = paintField == kStepAccent ? accR : slideR;
            if (x < row.x || x >= row.r()) return;
            const int k = stepAtX(x);
            if (k != paintLast) { setStep(k, paintField, paintValue); paintLast = k; }
        }
    }
    void bodyMouseUp(float, float) override
    {
        if (auditioning != -1000 && plug) { plug->keys.push((uint8_t)(kBaseNote + auditioning), 0); }
        auditioning = -1000;
        if (dragFilter) { dragFilter = false; endEdit(kRes); endEdit(kCutoff); }
        if (dragRoll)
        {
            if (!dragMoved) setStep(dragStep, kStepGate, (float)kRest);   // a click on a note: a rest
            dragRoll = false;
            endGroup();
        }
        if (paintField >= 0) { paintField = -1; endGroup(); }
    }
    bool bodyScroll(float x, float y, float dyw, uint mod) override
    {
        if (!rollR.contains(x, y)) return false;
        const int k = stepAtX(x);
        setStep(k, kStepNote, (float)pump::clampv(stepNote(k) + ((dyw > 0) ? ((mod & kModifierShift) ? 12 : 1) : ((mod & kModifierShift) ? -12 : -1)), kNoteLo, kNoteHi));
        setStatus(stepStatus(k));
        return true;
    }
    void bodyHover(float x, float y) override
    {
        hoverStep = -1; hoverNote = -1000;
        if (rollR.contains(x, y))
        {
            hoverStep = stepAtX(x); hoverNote = noteAtY(y);
            setStatus("Step " + std::to_string(hoverStep + 1) + "  " + noteText(hoverNote) + "    click: note · click a note: rest · drag: pitch · Shift / right-click: tie · Alt: slide · wheel: pitch");
        }
        else if (keysR.contains(x, y)) setStatus("Play " + noteText(noteAtY(y)));
        else if (filtR.contains(x, y)) setStatus("Drag: left / right = cutoff, up / down = resonance (double-click = reset)");
        else if (accR.contains(x, y)) setStatus("Accent: click (drag along to paint)");
        else if (slideR.contains(x, y)) setStatus("Slide into the next note: click (drag along to paint)");
        else if (numR.contains(x, y)) setStatus("Click: the pattern ends at this step");
        else if (namesR.contains(x, y)) setStatus("Click: note -> tie -> rest");
        else if (viewR.contains(x, y)) setStatus("The output in the colour of the note playing; ENV = filter envelope, ACC = the accent charge, AMP = the amp envelope");
    }
    std::string stepStatus(int k) const
    {
        const int g = stepGate(k);
        std::string s = "Step " + std::to_string(k + 1) + "  " + (g == kRest ? std::string("rest") : (g == kTie ? std::string("tie") : noteText(stepNote(k))));
        if (value(stepParam(k, kStepAccent)) >= 0.5f) s += "  ACCENT";
        if (value(stepParam(k, kStepSlide)) >= 0.5f) s += "  SLIDE";
        return s;
    }

    // pattern operations, each one undo step
    enum Op { kOpRandom, kOpMutate, kOpClear, kOpLeft, kOpRight, kOpUp, kOpDown };
    void patternOp(Op op)
    {
        Pattern p = patternFrom([this](int i) { return value(i); });
        const int L = p.length;
        RandomSettings rs;
        rs.scale = (int)value(kRndScale); rs.root = (int)value(kRndRoot); rs.density = value(kRndDensity) * 0.01;
        rs.accent = value(kRndAccent) * 0.01; rs.slide = value(kRndSlide) * 0.01; rs.octave = value(kRndOctave) * 0.01;
        const char* what = "";
        switch (op)
        {
        case kOpRandom: randomizeSteps(p, lanes, rs, rng); what = "Random pattern"; break;
        case kOpMutate: mutateSteps(p, lanes, rs, rng); what = "Mutated"; break;
        case kOpClear: for (auto& s : p.s) s = Step(); for (auto& s : p.s) s.note = rs.root; what = "Cleared"; break;
        case kOpLeft: case kOpRight:
        {
            Pattern q = p;
            for (int k = 0; k < L; ++k) q.s[k] = p.s[op == kOpLeft ? (k + 1) % L : (k + L - 1) % L];
            p = q; what = op == kOpLeft ? "Shifted left" : "Shifted right";
            break;
        }
        case kOpUp: case kOpDown:
        {
            const int d = op == kOpUp ? 1 : -1;
            bool fits = true;
            for (auto& s : p.s) fits = fits && s.note + d >= kNoteLo && s.note + d <= kNoteHi;
            if (fits) for (auto& s : p.s) s.note += d;
            what = fits ? (d > 0 ? "Up a semitone" : "Down a semitone") : "Out of range";
            break;
        }
        }
        beginGroup();
        for (int k = 0; k < kSteps; ++k)
        {
            setStep(k, kStepNote, (float)p.s[k].note);
            setStep(k, kStepGate, (float)p.s[k].gate);
            setStep(k, kStepAccent, p.s[k].accent ? 1.0f : 0.0f);
            setStep(k, kStepSlide, p.s[k].slide ? 1.0f : 0.0f);
        }
        endGroup();
        setStatus(what);
    }

    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key == 'r' || key == 'R') { patternOp(kOpRandom); return true; }
        if (key == 'm' || key == 'M') { patternOp(kOpMutate); return true; }
        if (key == ' ') { if (plug) plug->preview.store(!plug->preview.load()); return true; }
        if (key == kKeyLeft) { patternOp(kOpLeft); return true; }
        if (key == kKeyRight) { patternOp(kOpRight); return true; }
        if (key == kKeyUp) { patternOp(kOpUp); return true; }
        if (key == kKeyDown) { patternOp(kOpDown); return true; }
        return false;
    }
    void releaseAllHeld() override
    {
        if (auditioning != -1000 && plug) plug->keys.push((uint8_t)(kBaseNote + auditioning), 0);
        auditioning = -1000;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Click the roll", "a note there (click a note: a rest; drag it: its pitch)" },
            { "Shift-click, right-click", "a tie (holds the note before)" },
            { "Alt-click", "slide into the next note" },
            { "Wheel on a step", "its pitch (Shift: an octave)" },
            { "ACC / SLD rows", "click, drag to paint; LEN: click where the pattern ends" },
            { "R / M", "random pattern / mutate (the lit lanes)" },
            { "Left / Right, Up / Down", "shift the pattern, transpose it" },
            { "Space", "play the pattern now" },
            { "Drag the filter screen", "left / right = cutoff, up / down = resonance" },
        };
    }
    std::string footerInfo() override
    {
        char b[128];
        std::snprintf(b, sizeof b, "%s   SEQ %s   %d x %s", kModelNames[pump::clampv((int)value(kModel), 0, kNumModels - 1)],
                      kSeqModeNames[pump::clampv((int)value(kSeqMode), 0, kNumSeqModes - 1)], stepCount(), kRateNames[pump::clampv((int)value(kSeqRate), 0, kNumRates - 1)]);
        std::string s = b;
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }
    void loadUiState(const std::string& s) override
    {
        const int v = std::atoi(uiStateGet(s, "lanes", "31").c_str());
        lanes = (v > 0 && v < 32) ? (unsigned)v : kAllLanes;
    }
    std::string uiString() const { return "lanes=" + std::to_string(lanes); }

private:
    static constexpr int kScopeN = 4096, kRows = kNoteHi - kNoteLo + 1;
    static constexpr float kRowH = 4.0f;
    const Color kAccCol = pump::hexColor(0xFF4F5E), kSlideCol = pump::hexColor(0x3DE3F5);
    PluginAcid* plug = nullptr;
    Rect viewR, filtR, stepR, meterR, voiceP, seqP, keysR, rollR, namesR, accR, slideR, numR;
    Segmented *model, *slopeSeg, *lfoSeg, *modeSeg;
    std::vector<Dial*> mainD, moreD;
    std::vector<SliderRow*> seqRows, rndRows;
    Button *playBtn, *randomBtn, *mutateBtn, *clearBtn, *leftBtn, *rightBtn, *upBtn, *downBtn, *laneBtn[5];
    MenuButton *rateMenu, *scaleMenu, *rootMenu;
    unsigned lanes = kAllLanes;
    pump::fx::Rng rng { (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() | 1u };
    bool dragFilter = false, dragRoll = false, dragMoved = false, gate = false;
    int dragStep = 0, dragNote0 = 0, paintField = -1, paintLast = -1, hoverStep = -1, hoverNote = -1000, playStep = -1, auditioning = -1000;
    float paintValue = 0, dragY0 = 0, dx = 0, dy = 0, dCut = 0, dRes = 0;
    float scope[kScopeN] = {}, scale = 1.0f, cutShow = 8.7f, pitchShow = 36.0f, envShow = 0, accShow = 0, ampShow = 0;
    float flash[kSteps] = {};
    float lvl = -120.0f, hold = -120.0f, holdT = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIAcid)
};

UI* createUI() { return new UIAcid(); }

END_NAMESPACE_DISTRHO
