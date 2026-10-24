// pump VOX: interface. Two screens: BANDS (the voice's level per band, or its spectral envelope / mouth filter, in
// neon, with the carrier's in the second colour; in Classic the LOW and HIGH handles drag the band range, anywhere
// else drag up/down moves the formants; hold Alt over a band to hear only that band) and VOICE (the pitch the voice
// is singing over the last four seconds, the note the synth carrier plays, and a keyboard with the notes sounding
// and the scale marked). Meters: voice, carrier, output. Below: MODE (the four modes and their own controls, FREEZE),
// VOICE (formant, envelope times, flatten, emphasis, sibilance, noise, gate), CARRIER (routing and the built-in synth)
// and OUTPUT (spread, ensemble, dry voice / carrier, output).
#include "ui/PumpUI.hpp"
#include "PluginVox.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::vox;

class UIVox : public PumpUI {
public:
    UIVox() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::vox(), "VOX", 1040, 700, kBypass)
    {
        // ---- MODE ----
        modeSeg = addSegmented(kMode, { "CLASSIC", "SPECTRAL", "TALKBOX", "CONVOLVE" });
        modeSeg->tip = "Mode: Classic (band vocoder) · Spectral (FFT envelope) · Talkbox (the carrier through a model of your mouth) · Convolve (the two spectra multiplied)   (keys 1-4)";
        bandSeg = addSegmented(kBands, { "8", "12", "16", "20", "24", "32", "40" });
        bandSeg->tip = "Bands: how many band pairs (more = clearer words, fewer = more robotic)";
        fftSeg = addSegmented(kFftSize, { "512", "1024", "2048", "4096" });
        fftSeg->tip = "FFT size: longer = finer detail but slower to react; it is also the latency (at 48 kHz; doubled at 96 kHz)";
        lowDial = addDial(kLow, "LOW");
        lowDial->tip = "Lowest band (or drag the LOW handle on the screen)";
        highDial = addDial(kHigh, "HIGH");
        highDial->tip = "Highest band (or drag the HIGH handle on the screen)";
        widthDial = addDial(kWidth, "WIDTH");
        widthDial->tip = "Band width: narrower bands ring and sound more synthetic, wider ones smoother and blurrier";
        detailDial = addDial(kDetail, "DETAIL");
        tubeDial = addDial(kTube, "TUBE");
        tubeDial->tip = "Tube: the resonances of the plastic tube between the driver and your mouth";
        driveDial = addDial(kDrive, "DRIVE");
        driveDial->tip = "Driver drive: pushes the horn driver into soft clipping (quiet parts come up, peaks round off)";
        smearDial = addDial(kSmear, "SMEAR");
        smearDial->tip = "Smear: 0 = only the voice's magnitudes (crisp, metallic); 100 % = full complex multiplication (smeared, reverberant)";
        lowDial->tabGroup = highDial->tabGroup = widthDial->tabGroup = 1;
        freezeBtn = addToggle(kFreeze, "FREEZE");
        freezeBtn->holdMomentary = true;
        freezeBtn->tip = "Freeze: hold the voice's last sound while the carrier keeps playing (click toggles, click and hold = only while held; key F)";

        // ---- VOICE ----
        formantDial = addDial(kFormant, "FORMANT", Dial::Medium, true);
        formantDial->tip = "Formant: moves the voice's formants down (bigger, darker) or up (smaller, brighter), +-12 semitones (or drag up/down on the screen)";
        attackDial = addDial(kAttack, "ATTACK");
        attackDial->tip = "Attack: how fast the bands open when the voice gets louder";
        releaseDial = addDial(kRelease, "RELEASE");
        releaseDial->tip = "Release: how fast they close again (long = smooth, pad-like)";
        flattenDial = addDial(kFlatten, "FLATTEN");
        flattenDial->tip = "Flatten: evens out the carrier's own tone so every band has something to say (dull pads still speak in the top bands)";
        emphDial = addDial(kEmphasis, "EMPHASIS");
        emphDial->tip = "Emphasis: lifts the voice's top end before analysis (up to +12 dB above 2 kHz): crisper consonants";
        sibDial = addDial(kSibilance, "SIBILANCE");
        sibDial->tip = "Sibilance: the voice's own highs (above 5 kHz) mixed in, so s and t stay clear";
        noiseDial = addDial(kNoise, "NOISE");
        noiseDial->tip = "Unvoiced noise: noise added to the carrier while you say s, f, t, sh (detected), so consonants come through even between notes";
        gateDial = addDial(kGate, "GATE");
        gateDial->tip = "Voice gate: keeps bleed and room noise below this level out of the vocoder (OFF at the bottom)";
        formantDial->tabGroup = attackDial->tabGroup = releaseDial->tabGroup = flattenDial->tabGroup = 2;
        emphDial->tabGroup = sibDial->tabGroup = noiseDial->tabGroup = gateDial->tabGroup = 3;

        // ---- CARRIER ----
        routeMenu = addMenu(kRoute, "ROUTE");
        routeMenu->tip = "Routing: Synth carrier (VOX on your vocal, the built-in synth) · Sidechain carrier (a synth track on the sidechain) · Sidechain voice (VOX on the synth, voice on the sidechain) · Split L/R (left = carrier, right = voice)";
        sourceSeg = addSegmented(kSource, { "AUTO", "MIDI", "FOLLOW" });
        sourceSeg->tip = "Synth source: MIDI plays it from notes you send to VOX; Follow sings along with your voice (snapped to the key and scale); Auto = MIDI while notes are held, otherwise Follow";
        waveSeg = addSegmented(kWave, { "SAW", "SQR", "SUPER", "BUZZ", "NOISE" });
        waveSeg->tip = "Synth wave: saw (classic), square (hollow), super (seven detuned saws), buzz (narrow pulse, nasal), noise (whisper)";
        octaveDial = addDial(kOctave, "OCTAVE", Dial::Small, true);
        octaveDial->tip = "Synth octave";
        glideDial = addDial(kGlide, "GLIDE", Dial::Small);
        glideDial->tip = "Follow glide: how quickly the synth moves to the voice's new note (0 = hard steps, the robotic sound)";
        detuneDial = addDial(kDetune, "DETUNE", Dial::Small);
        detuneDial->tip = "Detune: unison voices spread in pitch and across the stereo field";
        keyMenu = addMenu(kKey);
        keyMenu->tip = "Key (Follow snaps to this key's scale)";
        keyMenu->textFn = [this]() { return std::string("KEY ") + kKeyNamesV[pump::clampv((int)value(kKey), 0, 11)]; };
        scaleMenu = addMenu(kScale);
        scaleMenu->tip = "Scale: Exact follows the voice as sung; the others snap to the scale (Chromatic = nearest semitone)";
        chordMenu = addMenu(kChord);
        chordMenu->tip = "Follow chord: notes added above or below the followed note, built from the scale";
        chordMenu->textFn = [this]() { const int c = pump::clampv((int)value(kChord), 0, kNumChords - 1); return std::string("CHORD  ") + (c == 0 ? std::string("OFF") : std::string(kChordNames[c])); };

        // ---- OUTPUT ----
        spreadDial = addDial(kSpread, "SPREAD", Dial::Small);
        spreadDial->tip = "Stereo spread: bands (or spectral regions) alternate left and right";
        ensDial = addDial(kEnsemble, "ENSEMBLE", Dial::Small);
        ensDial->tip = "Ensemble: three slowly moving delays per side, the shimmer of a string machine";
        voiceDial = addDial(kVoiceDry, "VOICE", Dial::Small);
        voiceDial->tip = "Dry voice mixed in (aligned with the latency)";
        carrierDial = addDial(kCarrierDry, "CARRIER", Dial::Small);
        carrierDial->tip = "Dry carrier mixed in (aligned with the latency)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Small, true);
        outDial->tip = "Output gain";

        if (auto* p = static_cast<PluginVox*>(pluginPtr())) plug = p;
        for (auto& h : hist) h = -1.0f;
        for (auto& h : histF) h = -1.0f;
    }

    // never leave the plugin soloed behind a closed window
    ~UIVox() override { if (plug) plug->engine.setSolo(0.0, 0.0); }

protected:
    // the window lost focus: a held Alt's release never arrives, so let go of the solo now
    void uiFocus(bool focus, DGL_NAMESPACE::CrossingMode mode) override
    {
        PumpUI::uiFocus(focus, mode);
        if (!focus) { altKey = altMouse = false; stopSolo(); }
    }
    // the FFT size and prediction order actually used at this sample rate
    int rateMul() const { int m = 1; while (sampleRate() / m > 66000.0 && m < 4) m *= 2; return m; }
    int talkOrder(float detail) const { return pump::clampv((int)std::lround((12 + (int)std::lround(pump::clampv(detail, 0.0f, 100.0f) * 0.44f)) * std::max(1.0, sampleRate() / 48000.0)), 8, 112); }

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kFormant: if (std::fabs(v) < 0.05f) return "0.0 st"; std::snprintf(b, sizeof b, "%+.1f st", v); return b;
        case kWidth: std::snprintf(b, sizeof b, "x%.2f", v); return b;
        case kGate: if (v <= kGateOff + 0.05f) return "OFF"; break;
        case kOctave: if (std::lround(v) == 0) return "0"; std::snprintf(b, sizeof b, "%+d", (int)std::lround(v)); return b;
        case kGlide: if (v < 0.5f) return "OFF"; break;
        case kDetail:
            if ((int)value(kMode) == kTalkbox)
            {
                std::snprintf(b, sizeof b, "%.0f%% (%d)", v, talkOrder(v)); return b;
            }
            break;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        screenR = Rect(14, 58, 676, 300);
        voiceR = Rect(706, 58, 270, 300);
        for (int m = 0; m < 4; ++m) meterR[m] = Rect(990 + m * 10, 70, 8, 266);
        const float y0 = 374, ph = 290;
        modePanel = Rect(14, y0, 296, ph);
        modeSeg->r = Rect(26, y0 + 26, 272, 28);
        bandSeg->r = Rect(26, y0 + 76, 272, 24);
        fftSeg->r = Rect(26, y0 + 76, 272, 24);
        const float dy = y0 + 110, dh = Dial::heightFor(Dial::Medium);
        lowDial->r = Rect(30, dy, 80, dh);
        highDial->r = Rect(122, dy, 80, dh);
        widthDial->r = Rect(214, dy, 80, dh);
        detailDial->r = Rect(30, dy, 80, dh);
        tubeDial->r = Rect(122, dy, 80, dh);
        driveDial->r = Rect(214, dy, 80, dh);
        smearDial->r = Rect(122, dy, 80, dh);
        freezeBtn->r = Rect(26, y0 + 240, 272, 32);

        voicePanel = Rect(322, y0, 296, ph);
        const float vx = 330, vw = 70;
        formantDial->r = Rect(vx, y0 + 24, vw, dh);
        attackDial->r = Rect(vx + 72, y0 + 24, vw, dh);
        releaseDial->r = Rect(vx + 144, y0 + 24, vw, dh);
        flattenDial->r = Rect(vx + 216, y0 + 24, vw, dh);
        emphDial->r = Rect(vx, y0 + 122, vw, dh);
        sibDial->r = Rect(vx + 72, y0 + 122, vw, dh);
        noiseDial->r = Rect(vx + 144, y0 + 122, vw, dh);
        gateDial->r = Rect(vx + 216, y0 + 122, vw, dh);
        uvBar = Rect(334, y0 + 250, 272, 8);

        carrierPanel = Rect(630, y0, 250, ph);
        routeMenu->r = Rect(642, y0 + 24, 226, 26);
        sourceSeg->r = Rect(642, y0 + 74, 226, 24);
        waveSeg->r = Rect(642, y0 + 104, 226, 24);
        const float sh = Dial::heightFor(Dial::Small);
        octaveDial->r = Rect(646, y0 + 134, 66, sh);
        glideDial->r = Rect(722, y0 + 134, 66, sh);
        detuneDial->r = Rect(798, y0 + 134, 66, sh);
        keyMenu->r = Rect(642, y0 + 212, 80, 26);
        scaleMenu->r = Rect(728, y0 + 212, 140, 26);
        chordMenu->r = Rect(642, y0 + 246, 226, 26);

        outPanel = Rect(892, y0, 134, ph);
        spreadDial->r = Rect(896, y0 + 24, 62, sh);
        ensDial->r = Rect(960, y0 + 24, 62, sh);
        voiceDial->r = Rect(896, y0 + 112, 62, sh);
        carrierDial->r = Rect(960, y0 + 112, 62, sh);
        outDial->r = Rect(928, y0 + 200, 62, sh);
        updateVisibility();
    }

    void updateVisibility()
    {
        const int m = pump::clampv((int)value(kMode), 0, kNumModes - 1);
        const bool classic = m == kClassic, spectral = m == kSpectral, talk = m == kTalkbox, conv = m == kConvolve;
        bandSeg->visible = lowDial->visible = highDial->visible = widthDial->visible = classic;
        fftSeg->visible = spectral || conv;
        detailDial->visible = !classic;
        tubeDial->visible = driveDial->visible = talk;
        smearDial->visible = conv;
        detailDial->tip = talk ? "Detail: how finely the mouth is modelled (the prediction order in brackets): low = vague and buzzy, high = sharp, every formant"
                        : conv ? "Detail: how much of the voice's own harmonics multiply the carrier (100 % = all of them, metallic; lower blurs them toward an envelope)"
                               : "Detail: how finely the spectral envelope follows the voice (low = broad and smooth, high = sharp; the voice's own harmonics start to show)";
        const bool synth = (int)value(kRoute) == kRouteSynth;
        for (Control* c : std::initializer_list<Control*>{ sourceSeg, waveSeg, octaveDial, glideDial, detuneDial, keyMenu, scaleMenu, chordMenu }) c->enabled = synth;
    }
    void onParamChanged(int i, float) override { if (i == kMode || i == kRoute) updateVisibility(); }
    void onPresetApplied() override { updateVisibility(); }

    void tick(float dt) override
    {
        if (!plug) return;
        Engine& e = plug->engine;
        auto meter = [&](pump::MeterValue& mv, int k) {
            const float pk = pump::gainToDbF(mv.exchange(0.0f));
            lvl[k] = std::max(pk, lvl[k] - 26.0f * dt);
            if (pk >= hold[k]) { hold[k] = pk; holdT[k] = 1.5f; }
            else if ((holdT[k] -= dt) <= 0.0f) hold[k] = std::max(pk, hold[k] - 18.0f * dt);
        };
        meter(e.voicePeak, 0); meter(e.carrierPeak, 1); meter(e.outPeak[0], 2); meter(e.outPeak[1], 3);
        // band display, smoothed a little
        const int n = pump::clampv(e.dispCount.load(std::memory_order_relaxed), 0, kDispArr);
        if (n != dispN) { dispN = n; for (int b = 0; b < kDispArr; ++b) { showV[b] = -120.0f; showC[b] = -120.0f; } }
        const float k = std::min(1.0f, dt * 18.0f), kd = std::min(1.0f, dt * 7.0f);
        for (int b = 0; b < n; ++b)
        {
            dispHzS[b] = std::max(20.0f, e.dispHz[b].load(std::memory_order_relaxed));
            const float v = e.dispVoice[b].load(std::memory_order_relaxed), c = e.dispCarrier[b].load(std::memory_order_relaxed);
            showV[b] += (v - showV[b]) * (v > showV[b] ? k : kd);
            showC[b] += (c - showC[b]) * (c > showC[b] ? k : kd);
        }
        // pitch history: one point per tick, about 4 s
        histT += dt;
        if (histT >= 1.0f / 60.0f)
        {
            histT = 0.0f;
            const float hz = e.pitchHz.load(std::memory_order_relaxed);
            hist[histPos] = hz > 0.0f ? (float)pump::pitch::hzToMidi(hz) : -1.0f;
            histF[histPos] = e.followNote.load(std::memory_order_relaxed);
            histPos = (histPos + 1) % kHist;
        }
        uvShow += (e.unvoicedNow.load(std::memory_order_relaxed) - uvShow) * std::min(1.0f, dt * 20.0f);
        for (int i = 0; i < 4; ++i) noteBits[i] = e.notes[i].load(std::memory_order_relaxed);
        missing = e.carrierMissing.load(std::memory_order_relaxed);
        // the voice's note, for the readout and the keyboard's range
        const float hz = e.pitchHz.load(std::memory_order_relaxed);
        if (hz > 0.0f) { lastMidi = (float)pump::pitch::hzToMidi(hz); lastMidiT = 0.0f; } else lastMidiT += dt;
    }

    // ---- drawing ----
    float yOfDb(const Rect& p, float db) const { return p.y + (kDbTop - pump::clampv(db, kDbBottom, kDbTop)) / (kDbTop - kDbBottom) * p.h; }
    Rect plotRect() const { return Rect(screenR.x + 34, screenR.y + 30, screenR.w - 46, screenR.h - 52); }

    void drawBody() override
    {
        drawBands();
        drawVoice();
        for (int m = 0; m < 4; ++m) drawLevelMeter(meterR[m], lvl[m], hold[m], -60.0f, 6.0f);
        textAt(meterR[0].cx(), meterR[0].y - 6, "V", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(meterR[1].cx(), meterR[1].y - 6, "C", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(meterR[2].r() + 1, meterR[2].y - 6, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);

        for (const Rect* r : { &modePanel, &voicePanel, &carrierPanel, &outPanel }) strokeRect(*r, theme.dim, 1.0f);
        caption(modePanel.x + 12, modePanel.y + 14, "MODE");
        const int m = pump::clampv((int)value(kMode), 0, kNumModes - 1);
        if (m == kClassic) caption(modePanel.x + 12, modePanel.y + 68, "BANDS");
        else if (m == kTalkbox) caption(modePanel.x + 12, modePanel.y + 68, "THE MOUTH");
        else caption(modePanel.x + 12, modePanel.y + 68, "FFT SIZE");
        if (m == kTalkbox)
            textAt(modePanel.x + 12, modePanel.y + 90, "driver > tube > throat > mouth", kLabel, 10.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        caption(voicePanel.x + 12, voicePanel.y + 14, "VOICE");
        // the unvoiced detector, under the voice dials
        caption(uvBar.x, uvBar.y - 8, "CONSONANTS");
        fillRect(uvBar, theme.panel2);
        fillRect(Rect(uvBar.x, uvBar.y, uvBar.w * pump::clampv(uvShow, 0.0f, 1.0f), uvBar.h), theme.second);
        strokeRect(uvBar, theme.ink, 1.0f);
        caption(carrierPanel.x + 12, carrierPanel.y + 14, "CARRIER");
        caption(carrierPanel.x + 12, carrierPanel.y + 64, "SYNTH");
        caption(outPanel.x + 12, outPanel.y + 14, "OUTPUT");
    }

    void drawBands()
    {
        const Rect& r = screenR;
        screenBox(r);
        const int m = pump::clampv((int)value(kMode), 0, kNumModes - 1);
        static const char* titles[kNumModes] = { "BANDS", "SPECTRAL ENVELOPE", "THE MOUTH", "VOICE SPECTRUM" };
        caption(r.x + 10, r.y + 14, titles[m]);
        char info[96];
        if (m == kClassic)
        {
            const int nb = kBandCounts[pump::clampv((int)value(kBands), 0, kNumBandChoices - 1)];
            std::snprintf(info, sizeof info, "%d BANDS   %s - %s", nb, formatParam(kLow, value(kLow)).c_str(), formatParam(kHigh, value(kHigh)).c_str());
        }
        else if (m == kTalkbox) std::snprintf(info, sizeof info, "ORDER %d   TUBE %.0f%%", talkOrder(value(kDetail)), value(kTube));
        else std::snprintf(info, sizeof info, "FFT %d   LATENCY %.1f ms", (512 << pump::clampv((int)value(kFftSize), 0, kNumFft - 1)) * rateMul(),
                           plug ? 1000.0 * plug->latencyNow() / std::max(1.0, sampleRate()) : 0.0);
        textAt(r.r() - 10, r.y + 14, info, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect p = plotRect();
        drawFreqGrid(p, kFLo, kFHi, true);
        for (int d = 0; d >= -84; d -= 12)
        {
            const float y = yOfDb(p, (float)d);
            line(p.x, y, p.r(), y, theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%d", d);
            textAt(p.x - 5, y, b, kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        save();
        scissor(p.x, p.y, p.w, p.h);
        const float shift = std::exp2(value(kFormant) / 12.0f);
        if (m == kClassic && dispN > 0)
        {
            // one bar per band: the voice's level (neon), the carrier band's level (a tick in the second colour, at the
            // carrier bank's own frequency: FORMANT moves it)
            for (int b = 0; b < dispN; ++b)
            {
                const double f = dispHzS[b];
                const double fl = b > 0 ? std::sqrt(f * dispHzS[b - 1]) : f / std::sqrt(dispN > 1 ? dispHzS[1] / dispHzS[0] : 1.2);
                const double fh = b + 1 < dispN ? std::sqrt(f * dispHzS[b + 1]) : f * std::sqrt(dispN > 1 ? dispHzS[1] / dispHzS[0] : 1.2);
                const float x0 = freqToX(p, fl, kFLo, kFHi) + 1.5f, x1 = freqToX(p, fh, kFLo, kFHi) - 1.5f;
                const float y = yOfDb(p, showV[b]);
                const bool solo = soloBand == b;
                Color fc = solo ? theme.second : theme.neon;
                if (soloBand >= 0 && !solo) fc = withAlpha2(theme.neon, 0.35f);
                if (y < p.b() - 0.5f)
                {
                    fillRect(Rect(x0, y, std::max(1.0f, x1 - x0), p.b() - y), fc);
                    strokeRect(Rect(x0, y, std::max(1.0f, x1 - x0), p.b() - y), theme.ink, 1.0f);
                }
                const float cx0 = freqToX(p, fl * shift, kFLo, kFHi), cx1 = freqToX(p, fh * shift, kFLo, kFHi);
                const float cy = yOfDb(p, showC[b]);
                if (cy < p.b() - 0.5f) line(cx0 + 2, cy, cx1 - 2, cy, theme.second, 2.2f);
            }
        }
        else if (dispN > 0)
        {
            // a filled curve (the voice) and a line (the carrier)
            beginPath();
            moveTo(p.x, p.b());
            for (int b = 0; b < dispN; ++b) lineTo(freqToX(p, dispHzS[b], kFLo, kFHi), yOfDb(p, showV[b]));
            lineTo(freqToX(p, dispHzS[dispN - 1], kFLo, kFHi), p.b());
            closePath();
            fillColor(withAlpha2(theme.neon, theme.dark ? 0.55f : 0.75f));
            fill();
            beginPath();
            for (int b = 0; b < dispN; ++b) { const float x = freqToX(p, dispHzS[b], kFLo, kFHi), y = yOfDb(p, showV[b]); if (b == 0) moveTo(x, y); else lineTo(x, y); }
            strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke();
            beginPath();
            for (int b = 0; b < dispN; ++b) { const float x = freqToX(p, dispHzS[b], kFLo, kFHi), y = yOfDb(p, showC[b]); if (b == 0) moveTo(x, y); else lineTo(x, y); }
            strokeColor(theme.second); strokeWidth(1.6f); stroke();
            if (soloLoHz > 0.0)
            {
                const float x0 = freqToX(p, soloLoHz, kFLo, kFHi), x1 = freqToX(p, soloHiHz, kFLo, kFHi);
                fillRect(Rect(p.x, p.y, x0 - p.x, p.h), withAlpha2(theme.bg, 0.6f));
                fillRect(Rect(x1, p.y, p.r() - x1, p.h), withAlpha2(theme.bg, 0.6f));
            }
        }
        // Classic: the LOW and HIGH handles
        if (m == kClassic)
            for (int h = 0; h < 2; ++h)
            {
                const float x = freqToX(p, value(h ? kHigh : kLow), kFLo, kFHi);
                const bool hot = dragHandle == h + 1 || hoverHandle == h + 1;
                line(x, p.y, x, p.b(), hot ? theme.ink : withAlpha2(theme.ink, 0.45f), hot ? 2.0f : 1.2f);
                const Rect tag(x - 17, p.y + 2, 34, 14);
                Color c = hot ? theme.ink : theme.bg;
                fillRect(tag, c);
                strokeRect(tag, theme.ink, 1.2f);
                textAt(tag.cx(), tag.cy() + 0.5f, h ? "HIGH" : "LOW", kLabel, 9.0f, hot ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
            }
        restore();
        // legend and state
        textAt(p.x + 4, r.b() - 10, "VOICE", kLabel, 9.5f, pump::mixColor(theme.neon, theme.ink, 0.45f), ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(p.x + 48, r.b() - 10, "CARRIER", kLabel, 9.5f, theme.second, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        if (std::fabs(value(kFormant)) >= 0.05f)
        {
            char b[32]; std::snprintf(b, sizeof b, "FORMANT %+.1f st", value(kFormant));
            textAt(p.x + 110, r.b() - 10, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        if (value(kFreeze) >= 0.5f)
        {
            const Rect fz(r.r() - 82, r.y + 26, 70, 20);
            fillRect(fz, theme.ink);
            textAt(fz.cx(), fz.cy() + 0.5f, "FROZEN", kLabel, 10.5f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 1.2f);
        }
        if (missing)
        {
            const int rt = (int)value(kRoute);
            const char* msg = rt == kRouteSidechainVoice ? "NO CARRIER ON THE TRACK" : (rt == kRouteSplit ? "NO CARRIER ON THE LEFT CHANNEL" : "NO CARRIER: ROUTE A SYNTH TO VOX'S SIDECHAIN");
            const float tw = textWidth(msg, kLabel, 12.0f, 1.0f) + 24.0f;
            const Rect box(p.cx() - tw * 0.5f, p.y + 26, tw, 26);
            fillRect(Rect(box.x + 3, box.y + 3, box.w, box.h), theme.dim);
            fillRect(box, theme.bg);
            strokeRect(box, theme.red, 1.5f);
            textAt(box.cx(), box.cy() + 0.5f, msg, kLabel, 12.0f, theme.red, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
    }

    void drawVoice()
    {
        const Rect& r = voiceR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "VOICE");
        char t[48];
        if (lastMidiT < 0.25f)
        {
            const int nn = (int)std::lround(lastMidi);
            char nb[8];
            std::snprintf(t, sizeof t, "%s %+d c", pump::pitch::noteName(nn, nb, sizeof nb), (int)std::lround(100.0f * (lastMidi - nn)));
            textAt(r.r() - 10, r.y + 14, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        else textAt(r.r() - 10, r.y + 14, "-", kMono, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        // the range: an octave and a half around the recent pitches
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < kHist; ++i)
        {
            if (hist[i] > 0.0f) { lo = std::min(lo, hist[i]); hi = std::max(hi, hist[i]); }
            if (histF[i] > 0.0f) { lo = std::min(lo, histF[i]); hi = std::max(hi, histF[i]); }
        }
        if (lo > hi) { lo = 48.0f; hi = 60.0f; }
        const float mid = 0.5f * (lo + hi), span = std::max(18.0f, hi - lo + 6.0f);
        rangeLo += ((mid - span * 0.5f) - rangeLo) * 0.08f;
        rangeHi += ((mid + span * 0.5f) - rangeHi) * 0.08f;
        const Rect p(r.x + 10, r.y + 28, r.w - 20, r.h - 104);
        auto yOf = [&](float midi) { return p.b() - (midi - rangeLo) / std::max(1.0f, rangeHi - rangeLo) * p.h; };
        // note grid: in-scale notes as faint lines, C as stronger ones
        const int key = pump::clampv((int)value(kKey), 0, 11), sc = pump::clampv((int)value(kScale), 0, kNumScaleChoices - 1);
        const uint16_t mask = sc == 0 ? 0xFFF : pump::pitch::kScaleMasks[sc - 1];
        save();
        scissor(p.x, p.y, p.w, p.h);
        for (int n = (int)std::floor(rangeLo); n <= (int)std::ceil(rangeHi); ++n)
        {
            const float y = yOf((float)n);
            const bool in = pump::pitch::inScale(n, key, mask);
            if (pump::pitch::pitchClass(n) == 0) { line(p.x, y, p.r(), y, withAlpha2(theme.ink, 0.25f), 1.0f); char nb[8]; textAt(p.x + 2, y - 5, pump::pitch::noteName(n, nb, 8), kMono, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE); }
            else if (in) line(p.x, y, p.r(), y, theme.grid, 1.0f);
        }
        // the synth's note (steps, second colour), then the voice (neon over ink)
        auto trace = [&](const float* h, const Color& c, float w) {
            bool pen = false;
            beginPath();
            for (int i = 0; i < kHist; ++i)
            {
                const float v = h[(histPos + i) % kHist];
                const float x = p.x + p.w * i / (kHist - 1);
                if (v <= 0.0f) { pen = false; continue; }
                if (!pen) { moveTo(x, yOf(v)); pen = true; } else lineTo(x, yOf(v));
            }
            strokeColor(c); strokeWidth(w); lineJoin(ROUND); stroke();
        };
        trace(histF, theme.second, 3.0f);
        trace(hist, theme.ink, 2.6f);
        trace(hist, theme.neon, 1.2f);
        restore();
        // keyboard: two octaves around the range's centre; sounding notes lit, scale notes dotted
        const Rect kb(r.x + 10, r.b() - 70, r.w - 20, 46);
        const int first = 12 * (int)std::floor((0.5f * (rangeLo + rangeHi) - 12.0f) / 12.0f);
        const int whites = 14;
        const float ww = kb.w / whites;
        auto lit = [&](int n) { return n >= 0 && n < 128 && (noteBits[n >> 5] >> (n & 31)) & 1u; };
        static const int whiteOff[7] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int w = 0; w < whites; ++w)
        {
            const int n = first + 12 * (w / 7) + whiteOff[w % 7];
            const Rect k(kb.x + w * ww, kb.y, ww, kb.h);
            fillRect(k, lit(n) ? theme.neon : theme.bg);
            strokeRect(k, theme.ink, 1.0f);
            if (sc >= 2 && pump::pitch::inScale(n, key, mask)) { beginPath(); circle(k.cx(), k.b() - 15, 2.0f); fillColor(theme.ink2); fill(); }
            if (n % 12 == 0) { char nb[8]; textAt(k.cx(), k.b() - 6, pump::pitch::noteName(n, nb, 8), kMono, 8.0f, lit(n) ? theme.neonInk : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE); }
        }
        static const int blackAfter[5] = { 0, 1, 3, 4, 5 }, blackOff[5] = { 1, 3, 6, 8, 10 };
        for (int o = 0; o < 2; ++o)
            for (int b = 0; b < 5; ++b)
            {
                const int n = first + 12 * o + blackOff[b];
                const float x = kb.x + (o * 7 + blackAfter[b] + 1) * ww - ww * 0.3f;
                const Rect k(x, kb.y, ww * 0.6f, kb.h * 0.6f);
                fillRect(k, lit(n) ? theme.neon : theme.ink);
                strokeRect(k, theme.ink, 1.0f);
                if (sc >= 2 && pump::pitch::inScale(n, key, mask)) { beginPath(); circle(k.cx(), k.b() - 5, 1.8f); fillColor(lit(n) ? theme.ink : theme.on); fill(); }
            }
        // what plays the carrier
        const int route = pump::clampv((int)value(kRoute), 0, kNumRoutes - 1);
        std::string what;
        if (route != kRouteSynth) what = route == kRouteSplit ? "CARRIER: LEFT INPUT" : (route == kRouteSidechain ? "CARRIER: SIDECHAIN" : "CARRIER: THE TRACK");
        else
        {
            const int src = pump::clampv((int)value(kSource), 0, kNumSources - 1);
            what = std::string("SYNTH ") + (src == kSourceMidi ? "MIDI" : (src == kSourceFollow ? "FOLLOWS" : "AUTO"));
            if (src != kSourceMidi) what += std::string("   ") + kKeyNamesV[key] + " " + kScaleNamesV[sc];
            for (char& ch : what) ch = (char)std::toupper((unsigned char)ch);
        }
        textAt(r.x + 10, r.b() - 12, what, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
    }

    // ---- screen interaction ----
    // which classic band (or display point region) is under x
    int bandAt(float x) const
    {
        if (dispN <= 0) return -1;
        const Rect p = plotRect();
        const double f = xToFreq(p, x, kFLo, kFHi);
        int best = 0;
        for (int b = 1; b < dispN; ++b) if (std::fabs(std::log(dispHzS[b] / f)) < std::fabs(std::log(dispHzS[best] / f))) best = b;
        return best;
    }
    void startSolo(float x)
    {
        const int m = (int)value(kMode);
        if (!plug || m == kTalkbox) return;
        const Rect p = plotRect();
        if (m == kClassic)
        {
            if (dispN != kBandCounts[pump::clampv((int)value(kBands), 0, kNumBandChoices - 1)]) return;   // still the old mode's points
            soloBand = bandAt(x);
            if (soloBand < 0) return;
            const double f = dispHzS[soloBand];
            plug->engine.setSolo(f * 0.98, f * 1.02);
            setStatus("Listening to one band (" + std::to_string((int)std::lround(f)) + " Hz) while Alt is held");
        }
        else
        {
            // a third of an octave around the mouse
            const double f = xToFreq(p, x, kFLo, kFHi);
            soloLoHz = f * std::exp2(-1.0 / 6.0); soloHiHz = f * std::exp2(1.0 / 6.0);
            plug->engine.setSolo(soloLoHz, soloHiHz);
            setStatus("Listening to a third of an octave around " + std::to_string((int)std::lround(f)) + " Hz while Alt is held");
        }
    }
    void stopSolo()
    {
        if (soloBand < 0 && soloLoHz <= 0.0) return;
        soloBand = -1; soloLoHz = soloHiHz = 0.0;
        if (plug) plug->engine.setSolo(0.0, 0.0);
    }
    int handleAt(float x, float y) const
    {
        if ((int)value(kMode) != kClassic) return 0;
        const Rect p = plotRect();
        if (!p.contains(x, y)) return 0;
        for (int h = 0; h < 2; ++h)
            if (std::fabs(x - freqToX(p, value(h ? kHigh : kLow), kFLo, kFHi)) < 7.0f) return h + 1;
        return 0;
    }
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!screenR.contains(x, y) || button != kMouseButtonLeft) return false;
        if (mod & kModifierAlt) { altMouse = true; startSolo(x); return true; }
        const int h = handleAt(x, y);
        if (h)
        {
            const int prm = h == 2 ? kHigh : kLow;
            if (dbl || cmdKey(mod)) { setParamGesture(prm, spec(prm).def); return false; }
            dragHandle = h; beginEdit(prm);
            return true;
        }
        if (dbl || cmdKey(mod)) { setParamGesture(kFormant, 0.0f); return false; }
        dragFormant = true; dragY = y; dragStart = value(kFormant);
        beginEdit(kFormant);
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (altMouse) { stopSolo(); startSolo(x); return; }
        const Rect p = plotRect();
        if (dragHandle)
        {
            const int prm = dragHandle == 2 ? kHigh : kLow;
            setParam(prm, (float)xToFreq(p, pump::clampv(x, p.x, p.r()), kFLo, kFHi));
            setStatus(paramStatus(prm));
            return;
        }
        if (dragFormant)
        {
            const float f = (mod & kModifierShift) ? 0.01f : 0.06f;
            setParam(kFormant, dragStart + (dragY - y) * f);
            if (mod & kModifierShift) { dragStart = value(kFormant); dragY = y; }
            setStatus(paramStatus(kFormant));
        }
    }
    void bodyMouseUp(float, float) override
    {
        if (altMouse) { altMouse = false; if (!altKey) stopSolo(); }
        if (dragHandle) { endEdit(dragHandle == 2 ? kHigh : kLow); dragHandle = 0; }
        if (dragFormant) { dragFormant = false; endEdit(kFormant); }
    }
    void bodyHover(float x, float y) override
    {
        hoverX = x; hoverY = y;
        hoverHandle = handleAt(x, y);
        if (screenR.contains(x, y))
        {
            if (altKey) { stopSolo(); startSolo(x); return; }
            if (hoverHandle) setStatus(hoverHandle == 2 ? "Drag: the highest band (double-click resets)" : "Drag: the lowest band (double-click resets)");
            else setStatus("Drag up/down: formant (double-click resets)   Hold Alt: listen to one band");
        }
        else if (voiceR.contains(x, y)) setStatus("The pitch you sing (neon) and the note the synth carrier plays (second colour); keys light up as they sound, dots mark the scale");
        else if (altKey) stopSolo();
    }
    bool keyPress(uint key, uint mod) override
    {
        if ((key == kKeyAltL || key == kKeyAltR)) { altKey = true; if (screenR.contains(hoverX, hoverY)) startSolo(hoverX); return true; }
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '4') { setParamGesture(kMode, (float)(key - '1')); return true; }
        if (key == 'f' || key == 'F') { setParamGesture(kFreeze, value(kFreeze) >= 0.5f ? 0.0f : 1.0f); return true; }
        return false;
    }
    bool keyRelease(uint key, uint) override
    {
        if ((key == kKeyAltL || key == kKeyAltR)) { altKey = false; if (!altMouse) stopSolo(); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "1 - 4", "mode: classic, spectral, talkbox, convolve" },
            { "F", "freeze on / off (or hold the FREEZE key)" },
            { "Hold Alt on the screen", "listen to the band under the mouse (move to sweep)" },
            { "Drag on the screen", "up/down = formant; in Classic drag LOW / HIGH to set the band range" },
            { "MIDI into VOX", "plays the synth carrier (Auto / MIDI); send a track's MIDI to VOX in your host" },
        };
    }
    std::string footerInfo() override
    {
        char buf[160];
        const int m = pump::clampv((int)value(kMode), 0, kNumModes - 1);
        const int route = pump::clampv((int)value(kRoute), 0, kNumRoutes - 1);
        std::snprintf(buf, sizeof buf, "%s   %s   LATENCY %d", kModeNames[m], kRouteNames[route], plug ? plug->latencyNow() : 0);
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    static constexpr int kHist = 240;
    static constexpr float kDbTop = 0.0f, kDbBottom = -84.0f;
    static constexpr double kFLo = 30.0, kFHi = 20000.0;
    PluginVox* plug = nullptr;
    Rect screenR, voiceR, meterR[4], modePanel, voicePanel, carrierPanel, outPanel, uvBar;
    float lvl[4] = { -120, -120, -120, -120 }, hold[4] = { -120, -120, -120, -120 }, holdT[4] = {};
    int dispN = 0;
    double dispHzS[kDispArr] = {};
    float showV[kDispArr] = {}, showC[kDispArr] = {};
    float hist[kHist], histF[kHist], histT = 0.0f;
    int histPos = 0;
    float rangeLo = 45.0f, rangeHi = 69.0f, lastMidi = 60.0f, lastMidiT = 10.0f, uvShow = 0.0f;
    uint32_t noteBits[4] = {};
    bool missing = false;
    int soloBand = -1;
    double soloLoHz = 0.0, soloHiHz = 0.0;
    bool altKey = false, altMouse = false, dragFormant = false;
    int dragHandle = 0, hoverHandle = 0;
    float dragY = 0, dragStart = 0, hoverX = -1, hoverY = -1;

    Segmented *modeSeg, *bandSeg, *fftSeg, *sourceSeg, *waveSeg;
    Dial *lowDial, *highDial, *widthDial, *detailDial, *tubeDial, *driveDial, *smearDial;
    Dial *formantDial, *attackDial, *releaseDial, *flattenDial, *emphDial, *sibDial, *noiseDial, *gateDial;
    Dial *octaveDial, *glideDial, *detuneDial, *spreadDial, *ensDial, *voiceDial, *carrierDial, *outDial;
    MenuButton *routeMenu, *keyMenu, *scaleMenu, *chordMenu;
    Button* freezeBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIVox)
};

UI* createUI() { return new UIVox(); }

END_NAMESPACE_DISTRHO
