// pump TUNE: interface. One big screen with a keyboard down its side:
//  LIVE   the last six seconds: the pitch sung (thin ink), the pitch that comes out (neon), the target notes, the
//         harmony voices (second colour); scale rows tinted, held MIDI notes lit on the keyboard.
//  GRAPH  the host's timeline: CAPTURE records the voice's pitch while the song plays and finds its notes. Drag notes
//         up and down (snaps to semitones; Alt = free), double-click one to snap it to the scale, drag on empty space
//         to select several; the strip below sets the selected notes' STRAIGHTEN (pull the drift to the centre),
//         VIBRATO (x the voice's own), FORMANT and GAIN. The corrected pitch is drawn over the sung one. Notes keep
//         their edits through a new capture of the same passage.
// Below: CORRECTION (mode, key, scale, MIDI, range, retune, amount, humanize, flex), PITCH (vibrato, transpose,
// formant), HARMONY (four voices: interval, level, pan) and OUTPUT.
#include "ui/PumpUI.hpp"
#include "PluginTune.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::tune;

// A slider row that edits the selected notes rather than a parameter.
struct NoteSlider : Control {
    std::function<float()> get;            // NaN = no notes
    std::function<void(float)> set;
    std::function<void()> begin, end;
    std::function<std::string(float)> fmt;
    float mn = 0, mx = 1, def = 0;
    float dragStartV = 0, dragStartX = 0;
    bool dragging = false;
    double lastClick = 0;
    bool wantsTextEntry() const override { return false; }
    void draw(PumpUI& ui) override;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override;
    void mouseDrag(PumpUI& ui, float x, float y, uint mod) override;
    void mouseUp(PumpUI& ui, float x, float y) override;
    bool scroll(PumpUI& ui, float dy, uint mod) override;
};

class UITune : public PumpUI {
public:
    UITune() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::tune(), "TUNE", 1100, 760, kBypass)
    {
        // ---- CORRECTION ----
        modeSeg = addSegmented(kMode, { "LIVE", "GRAPH" });
        modeSeg->tip = "Live: corrects to the key and scale (or MIDI) as you sing · Graph: capture the voice as notes and edit them   (Tab switches)";
        keyMenu = addMenu(kKey);
        keyMenu->tip = "Key";
        keyMenu->textFn = [this]() { return std::string("KEY ") + pump::pitch::kKeyNames[pump::clampv((int)value(kKey), 0, 11)]; };
        scaleMenu = addMenu(kScale);
        scaleMenu->tip = "Scale: the notes the voice may land on (Chromatic = every semitone)";
        midiMenu = addMenu(kMidi, "MIDI");
        midiMenu->tip = "MIDI sent to TUNE: Lead notes = the voice goes to the held note nearest it; Harmony notes = each held note becomes a harmony voice";
        rangeSeg = addSegmented(kRange, { "HIGH", "MID", "LOW" });
        rangeSeg->tip = "Voice range: High 100-1200 Hz, Mid 70-900 Hz, Low 50-600 Hz (lower ranges need a little more latency)";
        retuneDial = addDial(kRetune, "RETUNE");
        retuneDial->tip = "Retune speed: how fast the voice is pulled onto the note (0 = at once: the hard, stepped sound)";
        amountDial = addDial(kAmount, "AMOUNT");
        amountDial->tip = "Amount of correction";
        humanDial = addDial(kHumanize, "HUMANIZE");
        humanDial->tip = "Humanize: correct the note's centre and leave vibrato and small movements alone";
        flexDial = addDial(kFlex, "FLEX");
        flexDial->tip = "Flex: notes already within this many cents of the target are left alone (only bigger misses are fixed)";
        retuneDial->tabGroup = amountDial->tabGroup = humanDial->tabGroup = flexDial->tabGroup = 1;

        // ---- PITCH / FORMANT ----
        vibDial = addDial(kVibDepth, "VIBRATO", Dial::Small);
        vibDial->tip = "Added vibrato depth (cents), faded in after each new note";
        rateDial = addDial(kVibRate, "RATE", Dial::Small);
        rateDial->tip = "Added vibrato rate";
        transDial = addDial(kTranspose, "TRANSPOSE", Dial::Small, true);
        transDial->tip = "Transpose (semitones)";
        formDial = addDial(kFormant, "FORMANT", Dial::Small, true);
        formDial->tip = "Formant: the voice's character up (smaller, brighter) or down (bigger, darker) without changing the pitch";
        followBtn = addToggle(kFormantFollow, "FORMANTS FOLLOW");
        followBtn->small = true;
        followBtn->tip = "Formants follow the pitch (the chipmunk / giant sound); off keeps them where they were";

        // ---- HARMONY ----
        harmSeg = addSegmented(kHarmMode, { "OFF", "SCALE", "FIXED" });
        harmSeg->tip = "Harmony: Scale = steps of the scale above or below the corrected note (2 = a third, 4 = a fifth); Fixed = semitones; with MIDI = Harmony notes the held notes";
        for (int h = 0; h < kVoices; ++h)
        {
            static const char* names[4] = { "VOICE 1", "VOICE 2", "VOICE 3", "VOICE 4" };
            intDial[h] = addDial(kH1 + h, names[h], Dial::Small, true);
            intDial[h]->tip = "Harmony interval (scale steps in Scale, semitones in Fixed)";
            lvlDial[h] = addDial(kH1Level + h, "LEVEL", Dial::Small);
            lvlDial[h]->tip = "Harmony level (0 = off)";
            panDial[h] = addDial(kH1Pan + h, "PAN", Dial::Small, true);
            panDial[h]->tip = "Harmony pan";
        }

        // ---- OUTPUT ----
        leadDial = addDial(kLead, "LEAD", Dial::Small);
        leadDial->tip = "The corrected lead's level";
        harmLvlDial = addDial(kHarmLevel, "HARMONY", Dial::Small, true);
        harmLvlDial->tip = "All harmonies' level";
        harmFormDial = addDial(kHarmFormant, "H FORMANT", Dial::Small, true);
        harmFormDial->tip = "Harmonies' formant shift (other singers: down for deeper voices, up for lighter ones)";
        harmHumDial = addDial(kHarmHumanize, "H HUMAN", Dial::Small);
        harmHumDial->tip = "Harmony humanize: each voice drifts by a few cents and a few ms, like separate singers";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Small, true);
        outDial->tip = "Output gain";

        // ---- the graph's tools ----
        captureBtn = addButton("CAPTURE", [this]() { setCapture(!capturing); }, [this]() { return capturing; });
        captureBtn->small = true;
        captureBtn->tip = "Capture: while on, playing the song records the voice's pitch here and finds its notes (play the part once, then edit)";
        followViewBtn = addButton("FOLLOW", [this]() { followView = !followView; saveUi(); }, [this]() { return followView; });
        followViewBtn->small = true;
        followViewBtn->tip = "Follow: the view scrolls with the playhead";
        snapBtn = addButton("SNAP", [this]() { snapNotes(); });
        snapBtn->small = true;
        snapBtn->tip = "Snap the selected notes (all when none are) to the key and scale (key S)";
        resetBtn = addButton("RESET", [this]() { resetNotes(); });
        resetBtn->small = true;
        resetBtn->tip = "Reset the selected notes (all when none are) to how they were sung (Delete)";
        clearBtn = addButton("CLEAR", [this]() { clearAll(); }, [this]() { return clearArmed > 0.0f; });
        clearBtn->small = true;
        clearBtn->tip = "Clear everything captured (click twice)";
        auto mkSlider = [this](const char* label, float mn, float mx, float def, float GraphNote::* field, std::function<std::string(float)> fmt, const char* tip) {
            NoteSlider* s = add(new NoteSlider());
            s->label = label; s->mn = mn; s->mx = mx; s->def = def; s->fmt = std::move(fmt); s->tip = tip;
            s->get = [this, field]() -> float {
                for (size_t i = 0; i < notes.size(); ++i) if (sel[i]) return notes[i].*field;
                return notes.empty() ? NAN : notes[0].*field;
            };
            s->set = [this, field, mn, mx](float v) {
                v = pump::clampv(v, mn, mx);
                const bool any = anySelected();
                for (size_t i = 0; i < notes.size(); ++i) if (!any || sel[i]) notes[i].*field = v;
                notesChanged(false);
            };
            s->begin = [this]() { beginGroup(); };
            s->end = [this]() { endGroup(); notesChanged(true); };
            return s;
        };
        straightSl = mkSlider("STRAIGHTEN", 0.0f, 1.0f, 0.0f, &GraphNote::straighten, [](float v) { char b[16]; std::snprintf(b, sizeof b, "%.0f%%", 100.0f * v); return std::string(b); },
                              "Straighten: pulls the selected notes' slow drift toward their centre (100 % = a flat line)");
        vibSl = mkSlider("VIBRATO", 0.0f, 2.0f, 1.0f, &GraphNote::vibrato, [](float v) { char b[16]; std::snprintf(b, sizeof b, "%.0f%%", 100.0f * v); return std::string(b); },
                         "Vibrato: the selected notes' own vibrato, removed (0) to doubled (200 %)");
        formSl = mkSlider("FORMANT", -12.0f, 12.0f, 0.0f, &GraphNote::formant, [](float v) { char b[16]; std::snprintf(b, sizeof b, "%+.1f st", v); return std::string(b); },
                          "Formant of the selected notes");
        gainSl = mkSlider("GAIN", -24.0f, 12.0f, 0.0f, &GraphNote::gainDb, [](float v) { char b[16]; std::snprintf(b, sizeof b, "%+.1f dB", v); return std::string(b); },
                          "Level of the selected notes");

        if (auto* p = static_cast<PluginTune*>(pluginPtr())) plug = p;
        for (int i = 0; i < kHist; ++i) { hIn[i] = hOut[i] = hTgt[i] = -1.0f; for (int h = 0; h < kVoices; ++h) hHarm[h][i] = -1.0f; }
    }

protected:
    // ---- params ----
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kFlex || i == kVibDepth) { if (v < 0.5f) return "OFF"; std::snprintf(b, sizeof b, "%.0f c", v); return b; }
        if (i == kFormant || i == kHarmFormant) { if (std::fabs(v) < 0.05f) return "0.0 st"; std::snprintf(b, sizeof b, "%+.1f st", v); return b; }
        if (i == kTranspose) { if (std::lround(v) == 0) return "0 st"; std::snprintf(b, sizeof b, "%+d st", (int)std::lround(v)); return b; }
        if (i >= kH1 && i <= kH4)
        {
            const int n = (int)std::lround(v);
            if (n == 0) return "OFF";
            if ((int)value(kHarmMode) == kHarmScale)
            {
                static const char* deg[8] = { "", "2nd", "3rd", "4th", "5th", "6th", "7th", "8ve" };
                const int a = std::abs(n);
                if (a <= 7) { std::snprintf(b, sizeof b, "%s%s", n > 0 ? "+" : "-", deg[a]); return b; }
            }
            std::snprintf(b, sizeof b, "%+d", n); return b;
        }
        if (i >= kH1Pan && i <= kH4Pan)
        {
            if (std::fabs(v) < 0.5f) return "C";
            std::snprintf(b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v)); return b;
        }
        if (i == kRetune && v < 0.5f) return "INSTANT";
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        screenR = Rect(14, 56, 1072, 356);
        const float bx = screenR.r() - 10, by = screenR.y + 7, bh = 22;
        float x = bx;
        auto place = [&](Button* b, float w) { x -= w; b->r = Rect(x, by, w, bh); x -= 6; };
        place(clearBtn, 62); place(resetBtn, 62); place(snapBtn, 56); place(followViewBtn, 70); place(captureBtn, 82);
        const float sy = 420, sw = 254, sg = 18.7f;
        straightSl->r = Rect(18, sy, sw, 26);
        vibSl->r = Rect(18 + (sw + sg), sy, sw, 26);
        formSl->r = Rect(18 + 2 * (sw + sg), sy, sw, 26);
        gainSl->r = Rect(18 + 3 * (sw + sg), sy, sw, 26);

        const float y0 = 458, ph = 268;
        corrPanel = Rect(14, y0, 410, ph);
        modeSeg->r = Rect(26, y0 + 24, 120, 26);
        keyMenu->r = Rect(156, y0 + 24, 82, 26);
        scaleMenu->r = Rect(244, y0 + 24, 168, 26);
        midiMenu->r = Rect(26, y0 + 60, 212, 26);
        rangeSeg->r = Rect(244, y0 + 60, 168, 26);
        const float dh = Dial::heightFor(Dial::Medium), sh = Dial::heightFor(Dial::Small);
        retuneDial->r = Rect(30, y0 + 110, 90, dh);
        amountDial->r = Rect(126, y0 + 110, 90, dh);
        humanDial->r = Rect(222, y0 + 110, 90, dh);
        flexDial->r = Rect(318, y0 + 110, 90, dh);

        pitchPanel = Rect(436, y0, 180, ph);
        vibDial->r = Rect(446, y0 + 22, 76, sh);
        rateDial->r = Rect(530, y0 + 22, 76, sh);
        transDial->r = Rect(446, y0 + 104, 76, sh);
        formDial->r = Rect(530, y0 + 104, 76, sh);
        followBtn->r = Rect(448, y0 + 196, 156, 26);

        harmPanel = Rect(628, y0, 290, ph);
        harmSeg->r = Rect(640, y0 + 24, 266, 24);
        for (int h = 0; h < kVoices; ++h)
        {
            const float hx = 636 + h * 69.0f;
            intDial[h]->r = Rect(hx, y0 + 56, 68, sh);
            lvlDial[h]->r = Rect(hx, y0 + 126, 68, sh);
            panDial[h]->r = Rect(hx, y0 + 196, 68, sh);
        }

        outPanel = Rect(930, y0, 156, ph);
        leadDial->r = Rect(936, y0 + 22, 72, sh);
        harmLvlDial->r = Rect(1008, y0 + 22, 72, sh);
        harmFormDial->r = Rect(936, y0 + 104, 72, sh);
        harmHumDial->r = Rect(1008, y0 + 104, 72, sh);
        outDial->r = Rect(972, y0 + 186, 72, sh);
        updateVisibility();
    }
    bool graphMode() const { return (int)value(kMode) == kGraph; }
    void updateVisibility()
    {
        const bool g = graphMode();
        for (Control* c : std::initializer_list<Control*>{ captureBtn, followViewBtn, snapBtn, resetBtn, clearBtn, straightSl, vibSl, formSl, gainSl }) c->visible = g;
        const bool harm = (int)value(kHarmMode) != kHarmOff || (int)value(kMidi) == kMidiHarmony;
        for (int h = 0; h < kVoices; ++h) { intDial[h]->enabled = harm && (int)value(kMidi) != kMidiHarmony; lvlDial[h]->enabled = panDial[h]->enabled = harm; }
        const bool live = !g;
        for (Control* c : std::initializer_list<Control*>{ retuneDial, amountDial, humanDial, flexDial }) c->enabled = live;
    }
    void onParamChanged(int i, float) override { if (i == kMode || i == kHarmMode || i == kMidi) updateVisibility(); }
    void onPresetApplied() override { updateVisibility(); }

    // ---- notes: state, undo, the DSP ----
    std::string serializeExtra() const override { return notesToText(notes); }
    void deserializeExtra(const std::string& s) override
    {
        notes = notesFromText(s.c_str());
        sel.assign(notes.size(), false);
        sendNotes();
    }
    void onExtraState(const char* key, const char* v) override
    {
        if (std::strcmp(key, "notes") == 0) { notes = notesFromText(v); sel.assign(notes.size(), false); fitWhenLoaded = true; }
        else if (std::strcmp(key, "curve") == 0) { curve = curveFromText(v); recomputeLp(); fitWhenLoaded = true; }
    }
    void sendNotes() { setState("notes", notesToText(notes).c_str()); lastSend = nowS(); notesDirty = false; }
    void sendCurve() { setState("curve", curveToText(curve).c_str()); curveDirty = false; lastCurveSend = nowS(); }
    // a change to the notes: sent now when `final`, else at most every 60 ms (drags)
    void notesChanged(bool final)
    {
        notesDirty = true;
        if (final || nowS() - lastSend > 0.06) sendNotes();
        repaint();
    }
    bool anySelected() const { for (bool b : sel) if (b) return true; return false; }
    static double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    void snapNotes()
    {
        beginGroup();
        const bool any = anySelected();
        const uint16_t mask = pump::pitch::kScaleMasks[pump::clampv((int)value(kScale), 0, pump::pitch::kNumScales - 1)];
        for (size_t i = 0; i < notes.size(); ++i) if (!any || sel[i]) notes[i].dst = (float)pump::pitch::nearestInScale(notes[i].dst, (int)value(kKey), mask);
        endGroup();
        notesChanged(true);
        setStatus(any ? "Snapped the selected notes to the scale" : "Snapped every note to the scale");
    }
    void resetNotes()
    {
        beginGroup();
        const bool any = anySelected();
        for (size_t i = 0; i < notes.size(); ++i)
            if (!any || sel[i]) { GraphNote& n = notes[i]; n.dst = n.src; n.straighten = 0; n.vibrato = 1; n.formant = 0; n.gainDb = 0; }
        endGroup();
        notesChanged(true);
        setStatus(any ? "Reset the selected notes" : "Reset every note");
    }
    void clearAll()
    {
        if (clearArmed <= 0.0f) { clearArmed = 3.0f; setStatus("Click CLEAR again to delete everything captured"); return; }
        clearArmed = 0.0f;
        beginGroup();
        notes.clear(); sel.clear();
        endGroup();
        curve.clear(); lp.clear();
        sendNotes(); sendCurve();
        setStatus("Cleared");
    }
    void setCapture(bool on)
    {
        capturing = on;
        if (plug) plug->engine.setCapture(on);
        if (!on && curveDirty) sendCurve();
        passOpen = false;
        if (on && !graphMode()) setParamGesture(kMode, (float)kGraph);
        setStatus(on ? "Capturing: play the song (the voice's pitch is recorded while the host plays)" : "Capture off");
    }

    // ---- capture: frames from the engine into the curve, notes found as they arrive ----
    void drainCapture()
    {
        if (!plug) return;
        Engine& e = plug->engine;
        const uint32_t w = e.capW.load(std::memory_order_acquire);
        uint32_t r = e.capR.load(std::memory_order_relaxed);
        if (w == r) return;
        std::vector<CurveFrame> batch;
        batch.reserve(w - r);
        for (; r != w; ++r) { const CaptureFrame& f = e.capRing[r & (Engine::kCapRing - 1)]; batch.push_back({ f.t, f.midi }); }
        e.capR.store(r, std::memory_order_release);
        // a pass continues while frames follow on from the last one; a jump starts a new pass
        if (!passOpen || batch.front().t < passEnd - 0.002 || batch.front().t > passEnd + 0.1) { passOpen = true; passStart = batch.front().t; passEnd = passStart - 1e-6; }
        const double a = passEnd + 1e-6, b = batch.back().t + 1e-4;
        // replace what was there
        auto lo = std::lower_bound(curve.begin(), curve.end(), a, [](const CurveFrame& f, double t) { return f.t < t; });
        auto hi = std::lower_bound(lo, curve.end(), b, [](const CurveFrame& f, double t) { return f.t < t; });
        const long at = lo - curve.begin();
        curve.erase(lo, hi);
        curve.insert(curve.begin() + at, batch.begin(), batch.end());
        passEnd = batch.back().t;
        // find the notes again from just before the touched stretch (the last note may still be growing)
        double from = std::max(passStart, a - 0.4);
        std::vector<GraphNote> old;
        for (size_t i = 0; i < notes.size();)
        {
            if (notes[i].t1 > from && notes[i].t0 < b + 0.05) { from = std::min(from, notes[i].t0); old.push_back(notes[i]); notes.erase(notes.begin() + (long)i); sel.erase(sel.begin() + (long)i); }
            else ++i;
        }
        from = std::max(from, passStart - 0.0001);
        const size_t i0 = (size_t)(std::lower_bound(curve.begin(), curve.end(), from, [](const CurveFrame& f, double t) { return f.t < t; }) - curve.begin());
        const size_t i1 = (size_t)(std::lower_bound(curve.begin(), curve.end(), b, [](const CurveFrame& f, double t) { return f.t < t; }) - curve.begin());
        std::vector<GraphNote> found = findNotes(curve, i0, i1);
        // a note sung again in the same place keeps its edits
        for (GraphNote& n : found)
            for (const GraphNote& o : old)
            {
                const double ov = std::min(n.t1, o.t1) - std::max(n.t0, o.t0);
                if (ov > 0.5 * std::min(n.t1 - n.t0, o.t1 - o.t0) && std::fabs(n.src - o.src) < 0.5f)
                { n.dst = n.src + (o.dst - o.src); n.straighten = o.straighten; n.vibrato = o.vibrato; n.formant = o.formant; n.gainDb = o.gainDb; break; }
            }
        for (const GraphNote& n : found)
        {
            auto it = std::lower_bound(notes.begin(), notes.end(), n.t0, [](const GraphNote& x, double t) { return x.t0 < t; });
            const long k = it - notes.begin();
            notes.insert(it, n);
            sel.insert(sel.begin() + k, false);
        }
        // no overlaps (the list is sorted)
        for (size_t i = 1; i < notes.size(); ++i) if (notes[i].t0 < notes[i - 1].t1) notes[i].t0 = notes[i - 1].t1;
        for (size_t i = 0; i < notes.size();) { if (!(notes[i].t1 > notes[i].t0 + 0.005)) { notes.erase(notes.begin() + (long)i); sel.erase(sel.begin() + (long)i); } else ++i; }
        recomputeLp();
        curveDirty = true;
        notesChanged(false);
        if (nowS() - lastCurveSend > 1.0) sendCurve();
    }
    // a lone frame more than 1.5 semitones from both neighbours (a tracker slip at a note's start)
    bool isSpike(size_t i) const
    {
        if (i == 0 || i + 1 >= curve.size()) return false;
        const float a = curve[i - 1].midi, b = curve[i].midi, c = curve[i + 1].midi;
        if (a <= 0.0f && c <= 0.0f) return false;
        const bool farA = a <= 0.0f || std::fabs(b - a) > 1.5f, farC = c <= 0.0f || std::fabs(b - c) > 1.5f;
        return farA && farC;
    }
    // the pitch's slow centre (two one-poles at 2.5 Hz, like the engine), for drawing the corrected curve
    void recomputeLp()
    {
        lp.resize(curve.size());
        double a = -1, b = -1, lastT = -1e9;
        for (size_t i = 0; i < curve.size(); ++i)
        {
            const double m = curve[i].midi;
            if (m <= 0 || isSpike(i)) { lp[i] = -1; continue; }
            const double dt = pump::clampv(curve[i].t - lastT, 0.001, 0.05);
            if (a < 0 || std::fabs(m - b) > 1.5 || curve[i].t - lastT > 0.1) a = b = m;
            const double k = 1.0 - std::exp(-dt * pump::kTwoPi * 2.5);
            a += (m - a) * k; b += (a - b) * k;
            lp[i] = (float)b;
            lastT = curve[i].t;
        }
    }

    void tick(float dt) override
    {
        // test hook for headless screenshots (unused in normal runs): a file with the "notes" state, a line
        // "----", then the "curve" state
        if (!testLoaded)
        {
            testLoaded = true;
            if (const char* tg = std::getenv("PUMP_TEST_GRAPH"))
                if (FILE* f = std::fopen(tg, "rb"))
                {
                    std::string all; char buf[4096]; size_t k;
                    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, k);
                    std::fclose(f);
                    const size_t sp = all.find("----\n");
                    if (sp != std::string::npos) { onExtraState("notes", all.substr(0, sp).c_str()); onExtraState("curve", all.substr(sp + 5).c_str()); }
                    for (size_t i = 0; i < sel.size(); ++i) sel[i] = i == 5 || i == 6;
                    setParam(kMode, (float)kGraph);
                }
        }
        if (clearArmed > 0.0f) clearArmed -= dt;
        drainCapture();
        if (notesDirty && nowS() - lastSend > 0.06 && !dragNotes) sendNotes();
        if (!plug) return;
        Engine& e = plug->engine;
        playhead = e.playheadNow.load(std::memory_order_relaxed);
        playing = e.playingNow.load(std::memory_order_relaxed);
        if (!playing && passOpen) { passOpen = false; if (curveDirty) sendCurve(); }
        // live history
        histT += dt;
        while (histT >= 1.0f / 60.0f)
        {
            histT -= 1.0f / 60.0f;
            hIn[hPos] = e.inMidi.load(std::memory_order_relaxed);
            hOut[hPos] = e.outMidi.load(std::memory_order_relaxed);
            hTgt[hPos] = e.targetMidi.load(std::memory_order_relaxed);
            for (int h = 0; h < kVoices; ++h)
            {
                const float hn = e.harmNoteNow[h].load(std::memory_order_relaxed);
                hHarm[h][hPos] = hn > 0.0f && hOut[hPos] > 0.0f ? hOut[hPos] + (hn - hTgt[hPos]) : -1.0f;
            }
            hPos = (hPos + 1) % kHist;
        }
        for (int i = 0; i < 4; ++i) held[i] = e.heldBits[i].load(std::memory_order_relaxed);
        // the view
        if (graphMode())
        {
            if (fitWhenLoaded) { fitWhenLoaded = false; fitView(); }
            if (followView && playing && !dragNotes && !marquee)
            {
                if (playhead < vt0 || playhead > vt0 + vSpan * 0.85) vt0 = playhead - vSpan * 0.15;
            }
        }
        else
        {
            float lo = 1e9f, hi = -1e9f;
            for (int i = 0; i < kHist; ++i) for (float v : { hIn[i], hOut[i] }) if (v > 0.0f) { lo = std::min(lo, v); hi = std::max(hi, v); }
            if (lo > hi) { lo = 52; hi = 64; }
            const float span = std::max(15.0f, hi - lo + 6.0f), mid = 0.5f * (lo + hi);
            liveLo += ((mid - span * 0.5f) - liveLo) * std::min(1.0f, dt * 3.0f);
            liveHi += ((mid + span * 0.5f) - liveHi) * std::min(1.0f, dt * 3.0f);
        }
    }
    void fitView()
    {
        if (notes.empty() && curve.empty()) return;
        float lo = 1e9f, hi = -1e9f;
        for (const auto& n : notes) { lo = std::min({ lo, n.src, n.dst }); hi = std::max({ hi, n.src, n.dst }); }
        if (lo > hi) for (const auto& f : curve) if (f.midi > 0) { lo = std::min(lo, f.midi); hi = std::max(hi, f.midi); }
        if (lo > hi) return;
        vSpanP = pump::clampv(hi - lo + 8.0f, 12.0f, 48.0f);
        vp0 = 0.5f * (lo + hi) - 0.5f * vSpanP;
        vt0 = !notes.empty() ? notes.front().t0 - 0.5 : curve.front().t - 0.5;
    }

    // ---- drawing ----
    Rect plotRect() const { return Rect(screenR.x + 44, screenR.y + 36, screenR.w - 54, screenR.h - 58); }
    float pLo() const { return graphMode() ? vp0 : liveLo; }
    float pHi() const { return graphMode() ? vp0 + vSpanP : liveHi; }
    float yOf(const Rect& p, double m) const { return p.b() - (float)((m - pLo()) / std::max(1e-3, (double)(pHi() - pLo()))) * p.h; }
    double mOfY(const Rect& p, float y) const { return pLo() + (double)(p.b() - y) / p.h * (pHi() - pLo()); }
    float xOfT(const Rect& p, double t) const { return p.x + (float)((t - vt0) / vSpan) * p.w; }
    double tOfX(const Rect& p, float x) const { return vt0 + (double)(x - p.x) / p.w * vSpan; }
    uint16_t mask() const { return pump::pitch::kScaleMasks[pump::clampv((int)value(kScale), 0, pump::pitch::kNumScales - 1)]; }

    void drawBody() override
    {
        drawScreen();
        for (const Rect* r : { &corrPanel, &pitchPanel, &harmPanel, &outPanel }) strokeRect(*r, theme.dim, 1.0f);
        caption(corrPanel.x + 12, corrPanel.y + 14, "CORRECTION");
        caption(corrPanel.x + 12, corrPanel.y + 100, graphMode() ? "LIVE ONLY (THE GRAPH SETS ITS OWN NOTES)" : "");
        caption(pitchPanel.x + 12, pitchPanel.y + 14, "PITCH");
        caption(harmPanel.x + 12, harmPanel.y + 14, "HARMONY");
        caption(outPanel.x + 12, outPanel.y + 14, "OUTPUT");
        if (!graphMode())
        {
            // live readout where the graph's strip would be
            char b[96], n1[8], n2[8];
            const float in = hIn[(hPos + kHist - 1) % kHist], out = hOut[(hPos + kHist - 1) % kHist];
            if (in > 0.0f && out > 0.0f)
            {
                const int a = (int)std::lround(in), c = (int)std::lround(out);
                std::snprintf(b, sizeof b, "SUNG %s %+d c      SOUNDS %s %+d c      CORRECTION %+d c", pump::pitch::noteName(a, n1, 8), (int)std::lround(100 * (in - a)),
                              pump::pitch::noteName(c, n2, 8), (int)std::lround(100 * (out - c)), (int)std::lround(100 * (out - in)));
            }
            else std::snprintf(b, sizeof b, "SING: THE PITCH SUNG AND THE PITCH THAT COMES OUT ARE DRAWN ABOVE");
            textAt(20, 433, b, kLabel, 11.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
            textAt(W - 20, 433, std::string("LATENCY ") + std::to_string(plug ? plug->latencyNow() : 0), kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        }
    }

    void drawScreen()
    {
        const Rect& r = screenR;
        screenBox(r);
        const bool g = graphMode();
        caption(r.x + 10, r.y + 18, g ? "GRAPH" : "LIVE");
        if (g)
        {
            char b[64]; std::snprintf(b, sizeof b, "%zu NOTES", notes.size());
            textAt(r.x + 70, r.y + 18, b, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            if (capturing)
            {
                const bool blink = playing ? std::fmod(nowS(), 1.0) < 0.6 : true;
                textAt(r.x + 160, r.y + 18, playing ? "CAPTURING" : "CAPTURE ARMED: PLAY THE SONG", kLabel, 11.0f, blink ? theme.red : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.2f);
            }
        }
        else
        {
            const int sc = pump::clampv((int)value(kScale), 0, pump::pitch::kNumScales - 1);
            std::string t = std::string(pump::pitch::kKeyNames[pump::clampv((int)value(kKey), 0, 11)]) + " " + pump::pitch::kScaleNames[sc];
            for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
            textAt(r.x + 60, r.y + 18, t, kLabel, 11.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        }
        const Rect p = plotRect();
        const int key = pump::clampv((int)value(kKey), 0, 11);
        const uint16_t m = mask();
        // rows: scale notes tinted
        save();
        scissor(p.x, p.y, p.w, p.h);
        for (int n = (int)std::floor(pLo()); n <= (int)std::ceil(pHi()); ++n)
        {
            const float y0 = yOf(p, n + 0.5), y1 = yOf(p, n - 0.5);
            if (pump::pitch::inScale(n, key, m)) fillRect(Rect(p.x, y0, p.w, y1 - y0), withAlpha2(theme.panel2, theme.dark ? 0.55f : 0.45f));
            if (pump::pitch::pitchClass(n) == 0) line(p.x, y1, p.r(), y1, withAlpha2(theme.ink, 0.22f), 1.0f);
        }
        if (g) drawGraph(p); else drawLive(p);
        restore();
        drawKeys(Rect(r.x + 8, p.y, 32, p.h), key, m);
        strokeRect(p, withAlpha2(theme.ink, 0.35f), 1.0f);
    }

    void drawKeys(const Rect& k, int key, uint16_t m)
    {
        save();
        scissor(k.x, k.y, k.w, k.h);
        const float tgt = hTgt[(hPos + kHist - 1) % kHist];
        for (int n = (int)std::floor(pLo()) - 1; n <= (int)std::ceil(pHi()) + 1; ++n)
        {
            const float y0 = yOf(plotRect(), n + 0.5), y1 = yOf(plotRect(), n - 0.5);
            const int pc = pump::pitch::pitchClass(n);
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const bool lit = (n >= 0 && n < 128 && ((held[n >> 5] >> (n & 31)) & 1u)) || (!graphMode() && tgt > 0 && std::lround(tgt) == n);
            Color c = lit ? theme.neon : (black ? theme.ink : theme.bg);
            fillRect(Rect(k.x, y0, black ? k.w * 0.62f : k.w, y1 - y0), c);
            line(k.x, y1, k.r(), y1, withAlpha2(theme.ink, 0.35f), 1.0f);
            if (pc == 0 && y1 - y0 > 6.0f) { char b[8]; textAt(k.r() - 2, 0.5f * (y0 + y1), pump::pitch::noteName(n, b, 8), kMono, std::min(9.0f, y1 - y0), theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE); }
            (void)key; (void)m;
        }
        restore();
        strokeRect(k, theme.ink, 1.0f);
    }

    void drawLive(const Rect& p)
    {
        // time grid: one line a second
        for (int s = 1; s < 6; ++s) line(p.x + p.w * s / 6.0f, p.y, p.x + p.w * s / 6.0f, p.b(), theme.grid, 1.0f);
        auto trace = [&](const float* h, const Color& c, float w) {
            bool pen = false;
            beginPath();
            for (int i = 0; i < kHist; ++i)
            {
                const float v = h[(hPos + i) % kHist];
                const float x = p.x + p.w * i / (kHist - 1);
                if (v <= 0.0f) { pen = false; continue; }
                if (!pen) { moveTo(x, yOf(p, v)); pen = true; } else lineTo(x, yOf(p, v));
            }
            strokeColor(c); strokeWidth(w); lineJoin(ROUND); stroke();
        };
        // the target notes as bars
        for (int i = 0; i < kHist;)
        {
            const float t = hTgt[(hPos + i) % kHist];
            int j = i + 1;
            while (j < kHist && hTgt[(hPos + j) % kHist] == t) ++j;
            if (t > 0.0f)
            {
                const float x0 = p.x + p.w * i / (kHist - 1), x1 = p.x + p.w * (j - 1) / (kHist - 1);
                const float y0 = yOf(p, t + 0.5), y1 = yOf(p, t - 0.5);
                fillRect(Rect(x0, y0, std::max(1.0f, x1 - x0), y1 - y0), withAlpha2(theme.neon, 0.35f));
            }
            i = j;
        }
        for (int h = 0; h < kVoices; ++h) trace(hHarm[h], withAlpha2(theme.second, 0.9f), 2.0f);
        trace(hIn, withAlpha2(theme.ink, 0.55f), 1.2f);
        trace(hOut, theme.ink, 3.2f);
        trace(hOut, theme.neon, 1.6f);
        textAt(p.x + 6, p.b() - 10, "SUNG", kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(p.x + 44, p.b() - 10, "OUT", kLabel, 9.5f, pump::mixColor(theme.neon, theme.ink, 0.5f), ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(p.x + 76, p.b() - 10, "HARMONY", kLabel, 9.5f, theme.second, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
    }

    void drawGraph(const Rect& p)
    {
        // time grid
        const double steps[] = { 0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60 };
        double st = 60;
        for (double s : steps) if (vSpan / s <= 14) { st = s; break; }
        for (double t = std::ceil(vt0 / st) * st; t < vt0 + vSpan; t += st)
        {
            const float x = xOfT(p, t);
            line(x, p.y, x, p.b(), theme.grid, 1.0f);
            char b[24];
            const int mm = (int)(t / 60.0);
            std::snprintf(b, sizeof b, st < 1 ? "%d:%05.2f" : "%d:%02.0f", mm, t - 60.0 * mm);
            textAt(x + 3, p.y + 8, b, kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        if (notes.empty() && curve.empty())
        {
            textAt(p.cx(), p.cy() - 10, "TURN CAPTURE ON AND PLAY THE SONG", kLabel, 13.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.5f);
            textAt(p.cx(), p.cy() + 12, "the voice is recorded as notes you can drag up and down", kLabel, 11.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        }
        const float rowH = p.h / std::max(1.0f, vSpanP);
        // notes: the sung place as an outline, the new place filled
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const GraphNote& n = notes[i];
            if (n.t1 < vt0 || n.t0 > vt0 + vSpan) continue;
            const float x0 = xOfT(p, n.t0), x1 = xOfT(p, n.t1);
            const float h = std::max(5.0f, rowH * 0.8f);
            if (std::fabs(n.dst - n.src) > 0.01f)
            {
                const float ys = yOf(p, n.src);
                strokeRect(Rect(x0, ys - h * 0.5f, std::max(2.0f, x1 - x0), h), withAlpha2(theme.ink, 0.35f), 1.0f);
                line(0.5f * (x0 + x1), ys, 0.5f * (x0 + x1), yOf(p, n.dst), withAlpha2(theme.ink, 0.35f), 1.0f);
            }
            const float y = yOf(p, n.dst);
            const Rect nr(x0, y - h * 0.5f, std::max(2.0f, x1 - x0), h);
            fillRect(nr, sel[i] ? theme.ink : withAlpha2(theme.neon, theme.dark ? 0.5f : 0.8f));
            strokeRect(nr, sel[i] ? theme.neon : theme.ink, sel[i] ? 2.0f : 1.2f);
            // marks for notes with their own straighten / vibrato / formant / gain
            if (nr.w > 18 && (n.straighten > 0.01f || std::fabs(n.vibrato - 1.0f) > 0.01f || std::fabs(n.formant) > 0.05f || std::fabs(n.gainDb) > 0.05f))
                textAt(nr.x + 3, nr.cy(), "*", kBold, 10.0f, sel[i] ? theme.on : theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        // the pitch: sung (thin) and corrected (thick)
        const size_t a = (size_t)(std::lower_bound(curve.begin(), curve.end(), vt0 - 0.1, [](const CurveFrame& f, double t) { return f.t < t; }) - curve.begin());
        const size_t b = (size_t)(std::lower_bound(curve.begin(), curve.end(), vt0 + vSpan + 0.1, [](const CurveFrame& f, double t) { return f.t < t; }) - curve.begin());
        const size_t stride = std::max<size_t>(1, (b - a) / (size_t)std::max(1.0f, p.w * 1.5f));
        auto curveTrace = [&](bool corrected, const Color& c, float w) {
            beginPath();
            bool pen = false; double lastT = -1e9; size_t ni = 0;
            for (size_t i = a; i < b; i += stride)
            {
                const CurveFrame& f = curve[i];
                if (f.midi <= 0.0f || f.t - lastT > 0.05) pen = false;
                if (f.midi <= 0.0f || isSpike(i)) continue;
                double mm = f.midi;
                if (corrected)
                {
                    while (ni < notes.size() && notes[ni].t1 <= f.t) ++ni;
                    if (ni < notes.size() && f.t >= notes[ni].t0)
                    {
                        const GraphNote& n = notes[ni];
                        const double l = i < lp.size() && lp[i] > 0 ? lp[i] : f.midi;
                        mm = n.dst + (l - n.src) * (1.0 - n.straighten) + (f.midi - l) * n.vibrato;
                    }
                }
                const float x = xOfT(p, f.t), y = yOf(p, mm);
                if (!pen) { moveTo(x, y); pen = true; } else lineTo(x, y);
                lastT = f.t;
            }
            strokeColor(c); strokeWidth(w); lineJoin(ROUND); stroke();
        };
        curveTrace(false, withAlpha2(theme.ink, 0.45f), 1.1f);
        curveTrace(true, theme.second, 1.8f);
        // playhead
        if (playhead >= vt0 && playhead <= vt0 + vSpan)
        {
            const float x = xOfT(p, playhead);
            line(x, p.y, x, p.b(), playing ? theme.red : withAlpha2(theme.ink, 0.5f), 1.5f);
        }
        // the marquee
        if (marquee)
        {
            const Rect mr(std::min(mx0, mx1), std::min(my0, my1), std::fabs(mx1 - mx0), std::fabs(my1 - my0));
            fillRect(mr, withAlpha2(theme.neon, 0.15f));
            strokeRect(mr, theme.ink, 1.0f);
        }
        textAt(p.x + 6, p.b() - 10, "SUNG", kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(p.x + 44, p.b() - 10, "CORRECTED", kLabel, 9.5f, theme.second, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
    }

    // ---- graph interaction ----
    int noteAt(float x, float y) const
    {
        const Rect p = plotRect();
        const float rowH = p.h / std::max(1.0f, vSpanP), h = std::max(7.0f, rowH * 0.8f);
        const double t = tOfX(p, x);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const GraphNote& n = notes[i];
            if (t < n.t0 || t > n.t1) continue;
            if (std::fabs(yOf(p, n.dst) - y) <= h * 0.5f + 1.0f) return (int)i;
        }
        return -1;
    }
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        const Rect p = plotRect();
        if (!graphMode() || !p.contains(x, y) || button != kMouseButtonLeft) return false;
        const int hit = noteAt(x, y);
        const bool shift = (mod & kModifierShift) != 0;
        if (hit >= 0)
        {
            if (dbl)
            {
                beginGroup();
                notes[(size_t)hit].dst = (float)pump::pitch::nearestInScale(notes[(size_t)hit].dst, (int)value(kKey), mask());
                endGroup(); notesChanged(true);
                return false;
            }
            if (shift) sel[(size_t)hit] = !sel[(size_t)hit];
            else if (!sel[(size_t)hit]) { std::fill(sel.begin(), sel.end(), false); sel[(size_t)hit] = true; }
            dragNotes = true; dragY = y;
            dragStart.clear();
            for (size_t i = 0; i < notes.size(); ++i) dragStart.push_back(notes[i].dst);
            beginGroup();
            return true;
        }
        if (!shift) std::fill(sel.begin(), sel.end(), false);
        marquee = true; mx0 = mx1 = x; my0 = my1 = y;
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        const Rect p = plotRect();
        if (dragNotes)
        {
            const float rowH = p.h / std::max(1.0f, vSpanP);
            double d = (dragY - y) / rowH;
            const bool free = (mod & kModifierAlt) != 0;
            if (mod & kModifierShift) d *= 0.2;
            for (size_t i = 0; i < notes.size(); ++i)
                if (sel[i]) notes[i].dst = (float)pump::clampv(free ? dragStart[i] + d : std::round(dragStart[i] + d), 0.0, 127.0);
            notesChanged(false);
            for (size_t i = 0; i < notes.size(); ++i) if (sel[i])
            {
                char b[64], nb[8]; const int nn = (int)std::lround(notes[i].dst);
                std::snprintf(b, sizeof b, "%s %+d c   (%+.2f semitones from where it was sung)", pump::pitch::noteName(nn, nb, 8), (int)std::lround(100 * (notes[i].dst - nn)), notes[i].dst - notes[i].src);
                setStatus(b); break;
            }
            return;
        }
        if (marquee)
        {
            mx1 = pump::clampv(x, p.x, p.r()); my1 = pump::clampv(y, p.y, p.b());
            const double t0 = tOfX(p, std::min(mx0, mx1)), t1 = tOfX(p, std::max(mx0, mx1));
            const double m0 = mOfY(p, std::max(my0, my1)), m1 = mOfY(p, std::min(my0, my1));
            for (size_t i = 0; i < notes.size(); ++i)
                if (notes[i].t1 > t0 && notes[i].t0 < t1 && notes[i].dst > m0 - 0.4 && notes[i].dst < m1 + 0.4) sel[i] = true;
        }
    }
    void bodyMouseUp(float, float) override
    {
        if (dragNotes) { dragNotes = false; endGroup(); notesChanged(true); }
        marquee = false;
    }
    void bodyHover(float x, float y) override
    {
        const Rect p = plotRect();
        if (!p.contains(x, y)) return;
        if (!graphMode()) { setStatus("The pitch sung (thin), what comes out (neon), the target notes and the harmonies (second colour)"); return; }
        const int hit = noteAt(x, y);
        if (hit >= 0)
        {
            const GraphNote& n = notes[(size_t)hit];
            char b[160], n1[8];
            const int nn = (int)std::lround(n.dst);
            std::snprintf(b, sizeof b, "%s %+d c  (sung %+.2f st)   %.2f s   drag: move (Alt free)   double-click: snap to the scale", pump::pitch::noteName(nn, n1, 8),
                          (int)std::lround(100 * (n.dst - nn)), n.dst - n.src, n.t1 - n.t0);
            setStatus(b);
        }
        else setStatus("Drag: select notes   Wheel: scroll   Cmd/Ctrl+wheel: zoom time   Shift+wheel: scroll pitch   Alt+wheel: zoom pitch");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        const Rect p = plotRect();
        if (!screenR.contains(x, y)) return false;
        if (!graphMode()) return true;
        if (cmdKey(mod))
        {
            const double t = tOfX(p, x), f = dy > 0 ? 0.8 : 1.25;
            vSpan = pump::clampv(vSpan * f, 0.25, 600.0);
            vt0 = t - (double)(x - p.x) / p.w * vSpan;
        }
        else if (mod & kModifierAlt)
        {
            const double m = mOfY(p, y), f = dy > 0 ? 0.85 : 1.18;
            vSpanP = (float)pump::clampv(vSpanP * f, 6.0, 72.0);
            vp0 = (float)(m - (double)(p.b() - y) / p.h * vSpanP);
        }
        else if (mod & kModifierShift) vp0 += dy > 0 ? 1.0f : -1.0f;
        else vt0 += (dy > 0 ? -0.1 : 0.1) * vSpan;
        vp0 = pump::clampv(vp0, 0.0f, 127.0f - vSpanP);
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (!graphMode())
        {
            if (key == kKeyTab) { setParamGesture(kMode, (float)kGraph); return true; }
            return false;
        }
        if (key == kKeyTab) { setParamGesture(kMode, (float)kLive); return true; }
        const bool cmd = cmdKey(mod);
        if (cmd && (key == 'a' || key == 'A')) { std::fill(sel.begin(), sel.end(), true); repaint(); return true; }
        if (cmd) return false;
        if (key == kKeyUp || key == kKeyDown)
        {
            const double d = (key == kKeyUp ? 1.0 : -1.0) * ((mod & kModifierAlt) ? 0.05 : ((mod & kModifierShift) ? 12.0 : 1.0));
            beginGroup();
            for (size_t i = 0; i < notes.size(); ++i) if (sel[i]) notes[i].dst = (float)pump::clampv(notes[i].dst + d, 0.0, 127.0);
            endGroup(); notesChanged(true);
            return true;
        }
        if (key == kKeyDelete || key == kKeyBackspace) { if (anySelected()) resetNotes(); return true; }
        if (key == kKeyEscape) { std::fill(sel.begin(), sel.end(), false); repaint(); return true; }
        if (key == 's' || key == 'S') { snapNotes(); return true; }
        if (key == 'c' || key == 'C') { setCapture(!capturing); return true; }
        if (key == 'f' || key == 'F') { followView = !followView; saveUi(); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Tab", "switch between Live and Graph" },
            { "C (graph)", "capture on / off, then play the song in your host" },
            { "Drag a note", "move it (snaps to semitones; Alt = free, Shift = fine)" },
            { "Double-click a note", "snap it to the key and scale" },
            { "Up / Down", "selected notes a semitone (Shift = octave, Alt = 5 cents)" },
            { "Cmd/Ctrl + A, Esc", "select every note, select none" },
            { "S, Delete", "snap the selection to the scale, reset it to how it was sung" },
            { "Wheel", "scroll time; Cmd/Ctrl = zoom time; Shift = scroll pitch; Alt = zoom pitch" },
            { "F", "the view follows the playhead" },
        };
    }
    std::string footerInfo() override
    {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s   RANGE %s   LATENCY %d", graphMode() ? "GRAPH" : "LIVE", kRangeNames[pump::clampv((int)value(kRange), 0, kNumRanges - 1)], plug ? plug->latencyNow() : 0);
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }
    void saveUi() { char b[96]; std::snprintf(b, sizeof b, "follow=%d;", followView ? 1 : 0); saveUiState(b); }
    void loadUiState(const std::string& s) override { followView = uiStateGet(s, "follow", "1") != "0"; }

private:
    static constexpr int kHist = 360;
    PluginTune* plug = nullptr;
    Rect screenR, corrPanel, pitchPanel, harmPanel, outPanel;
    // live history
    float hIn[kHist], hOut[kHist], hTgt[kHist], hHarm[kVoices][kHist];
    int hPos = 0;
    float histT = 0.0f, liveLo = 50.0f, liveHi = 66.0f;
    uint32_t held[4] = {};
    // the graph
    std::vector<CurveFrame> curve;
    std::vector<float> lp;
    std::vector<GraphNote> notes;
    std::vector<bool> sel;
    std::vector<double> dragStart;
    double vt0 = 0.0, vSpan = 8.0, playhead = 0.0, passStart = 0.0, passEnd = 0.0, lastSend = 0.0, lastCurveSend = 0.0;
    float vp0 = 48.0f, vSpanP = 24.0f, dragY = 0.0f, mx0 = 0, my0 = 0, mx1 = 0, my1 = 0, clearArmed = 0.0f;
    bool capturing = false, playing = false, followView = true, passOpen = false, dragNotes = false, marquee = false;
    bool notesDirty = false, curveDirty = false, fitWhenLoaded = false, testLoaded = false;

    Segmented *modeSeg, *rangeSeg, *harmSeg;
    MenuButton *keyMenu, *scaleMenu, *midiMenu;
    Dial *retuneDial, *amountDial, *humanDial, *flexDial, *vibDial, *rateDial, *transDial, *formDial;
    Dial *intDial[kVoices], *lvlDial[kVoices], *panDial[kVoices];
    Dial *leadDial, *harmLvlDial, *harmFormDial, *harmHumDial, *outDial;
    Button *followBtn, *captureBtn, *followViewBtn, *snapBtn, *resetBtn, *clearBtn;
    NoteSlider *straightSl, *vibSl, *formSl, *gainSl;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UITune)
};

// ---- NoteSlider ----
inline void NoteSlider::draw(PumpUI& ui)
{
    const Theme& t = ui.theme;
    const float v = get ? get() : NAN;
    const bool has = std::isfinite(v);
    ui.textAt(r.x, r.cy(), label, PumpUI::kLabel, 11.0f, has ? t.ink2 : t.dim, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 0.8f);
    const float lw = 86.0f, vw = 62.0f;
    const Rect bar(r.x + lw, r.cy() - 5.0f, r.w - lw - vw - 6.0f, 10.0f);
    ui.fillRect(bar, t.panel2);
    if (has)
    {
        const float n = (v - mn) / (mx - mn), z = (def - mn) / (mx - mn);
        const float x0 = bar.x + bar.w * std::min(n, z), x1 = bar.x + bar.w * std::max(n, z);
        ui.fillRect(Rect(x0, bar.y, std::max(1.0f, x1 - x0), bar.h), t.neon);
        ui.fillRect(Rect(bar.x + bar.w * n - 1.5f, bar.y - 3.0f, 3.0f, bar.h + 6.0f), t.ink);
    }
    ui.strokeRect(bar, has ? t.ink : t.dim, 1.2f);
    ui.textAt(r.r(), r.cy(), has && fmt ? fmt(v) : "-", PumpUI::kMono, 11.0f, has ? t.ink : t.dim, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
}
inline bool NoteSlider::mouseDown(PumpUI& ui, float x, float, int button, uint mod)
{
    if (button != kMouseButtonLeft || !get) return false;
    const float v = get();
    if (!std::isfinite(v)) return true;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const bool dbl = now - lastClick < 0.35;
    lastClick = now;
    if (dbl || cmdKey(mod) || (mod & kModifierAlt)) { if (begin) begin(); set(def); if (end) end(); return true; }
    dragging = true; dragStartV = v; dragStartX = x;
    if (begin) begin();
    return true;
}
inline void NoteSlider::mouseDrag(PumpUI&, float x, float, uint mod)
{
    if (!dragging) return;
    const float barW = std::max(40.0f, r.w - 86.0f - 62.0f - 6.0f);
    const float d = (x - dragStartX) / barW * (mx - mn) * ((mod & kModifierShift) ? 0.15f : 1.0f);
    set(dragStartV + d);
}
inline void NoteSlider::mouseUp(PumpUI&, float, float) { if (dragging) { dragging = false; if (end) end(); } }
inline bool NoteSlider::scroll(PumpUI&, float dy, uint mod)
{
    if (!get) return true;
    const float v = get();
    if (!std::isfinite(v)) return true;
    if (begin) begin();
    set(v + (dy > 0 ? 1.0f : -1.0f) * (mx - mn) * ((mod & kModifierShift) ? 0.005f : 0.05f));
    if (end) end();
    return true;
}

UI* createUI() { return new UITune(); }

END_NAMESPACE_DISTRHO
