// pump AMP: interface. AMP (the model: name, rate, loudness; LOAD, previous / next file in its folder, CLEAR),
// SPECTRUM (the output, with the tone stack's curve and the cabinet's response over it) and CAB (the impulse
// response: name, length, its response; LOAD, previous / next, CLEAR). Below: input (input gain, gate, normalize,
// mono / stereo, output), tone (bass, mid, treble, presence) and cab (on, low cut, high cut).
#include "ui/PumpUI.hpp"
#include "PluginAmp.hpp"
#include <algorithm>
#include <complex>

START_NAMESPACE_DISTRHO

using namespace pump::amp;

class UIAmp : public PumpUI {
public:
    UIAmp() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::amp(), "AMP", 1000, 620, kBypass)
    {
        inDial = addDial(kInput, "INPUT", Dial::Large, true);
        inDial->tip = "Input gain into the model (models respond to level like the real amp did)";
        gateDial = addDial(kGate, "GATE");
        gateDial->tip = "Noise gate threshold before the amp (the far left = off)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        normBtn = addToggle(kNormalize, "NORMALIZE");
        normBtn->small = true;
        normBtn->tip = "Normalize: bring every model to the same loudness (when its file says how loud it is)";
        chSeg = addSegmented(kChannels, { "MONO", "STEREO" });
        chSeg->tip = "Mono: one model on L + R. Stereo: one model per side (twice the CPU)";
        inDial->tabGroup = gateDial->tabGroup = outDial->tabGroup = 1;

        bassDial = addDial(kBass, "BASS", Dial::Medium, true);
        midDial = addDial(kMid, "MID", Dial::Medium, true);
        trebDial = addDial(kTreble, "TREBLE", Dial::Medium, true);
        presDial = addDial(kPresence, "PRESENCE", Dial::Medium, true);
        bassDial->tip = "Bass: shelf at 110 Hz, after the amp";
        midDial->tip = "Mid: bell at 700 Hz";
        trebDial->tip = "Treble: shelf at 2.8 kHz";
        presDial->tip = "Presence: shelf at 5.5 kHz";
        bassDial->tabGroup = midDial->tabGroup = trebDial->tabGroup = presDial->tabGroup = 2;

        cabBtn = addToggle(kCab, "CABINET");
        cabBtn->small = true;
        cabBtn->tip = "Cabinet: the impulse response on or off (the timing stays the same)";
        lowDial = addDial(kCabLowCut, "LOW CUT", Dial::Small);
        lowDial->tip = "Low cut after the cabinet";
        highDial = addDial(kCabHighCut, "HIGH CUT", Dial::Small);
        highDial->tip = "High cut after the cabinet";

        modelLoad = addButton("LOAD", [this] { browse(0); });
        modelPrev = addButton("<", [this] { step(0, -1); });
        modelNext = addButton(">", [this] { step(0, +1); });
        modelClear = addButton("CLEAR", [this] { setState("model", ""); });
        irLoad = addButton("LOAD", [this] { browse(1); });
        irPrev = addButton("<", [this] { step(1, -1); });
        irNext = addButton(">", [this] { step(1, +1); });
        irClear = addButton("CLEAR", [this] { setState("ir", ""); });
        for (Button* b : { modelLoad, modelPrev, modelNext, modelClear, irLoad, irPrev, irNext, irClear }) b->small = true;
        modelLoad->tip = "Load a .nam model";
        modelPrev->tip = modelNext->tip = "Previous / next model in the same folder";
        irLoad->tip = "Load a cabinet impulse response (.wav)";
        irPrev->tip = irNext->tip = "Previous / next impulse response in the same folder";

        if (auto* p = static_cast<PluginAmp*>(pluginPtr())) plug = p;
        const double sr = plug && plug->getSampleRate() > 0.0 ? plug->getSampleRate() : sampleRate();
        onSampleRateChanged(sr);
    }

protected:
    void onSampleRateChanged(double sr) override { post.setup(sr); post.setRange(kLo, kHi); }

    // ---- files ----
    void browse(int which)
    {
        pendingBrowse = which;
        FileBrowserOptions o;
        o.title = which == 0 ? "Load an amp model (.nam)" : "Load a cabinet impulse response (.wav)";
        const std::string cur = which == 0 ? model.path : ir.path;
        std::string dir;
        if (!cur.empty()) dir = std::filesystem::path(cur).parent_path().string();
        if (!dir.empty()) o.startDir = dir.c_str();
        openFileBrowser(o);
    }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename || !*filename) return;
        setState(pendingBrowse == 0 ? "model" : "ir", filename);
    }
    // previous / next file with the same kind of extension in the folder of the current one
    void step(int which, int dir)
    {
        const std::string cur = which == 0 ? model.path : ir.path;
        if (cur.empty()) { browse(which); return; }
        std::error_code ec;
        const std::filesystem::path p(cur);
        std::vector<std::string> files;
        for (const auto& e : std::filesystem::directory_iterator(p.parent_path(), ec))
        {
            if (!e.is_regular_file(ec)) continue;
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if ((which == 0 && ext == ".nam") || (which == 1 && ext == ".wav")) files.push_back(e.path().string());
        }
        if (files.empty()) return;
        std::sort(files.begin(), files.end());
        auto it = std::find(files.begin(), files.end(), p.string());
        int i = it == files.end() ? 0 : (int)(it - files.begin());
        i = (i + dir + (int)files.size()) % (int)files.size();
        setState(which == 0 ? "model" : "ir", files[(size_t)i].c_str());
    }

    std::string formatParam(int i, float v) const override
    {
        switch (i)
        {
        case kGate: if (!gateActive(v)) return "OFF"; break;
        case kCabLowCut: if (!lowCutActive(v)) return "OFF"; break;
        case kCabHighCut: if (!highCutActive(v)) return "OFF"; break;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        ampR = Rect(14, 58, 300, 322);
        specR = Rect(330, 58, 392, 322);
        cabR = Rect(738, 58, 210, 322);
        meterL = Rect(962, 58, 9, 304);
        meterR = Rect(973, 58, 9, 304);
        // file buttons along the bottom of the two file screens
        auto row = [](const Rect& r, Button* load, Button* prev, Button* next, Button* clear) {
            const float y = r.b() - 34, x0 = r.x + 10;
            prev->r = Rect(x0, y, 26, 22);
            next->r = Rect(x0 + 30, y, 26, 22);
            const float bw = r.w < 250 ? 52 : 64;
            load->r = Rect(x0 + 62, y, bw, 22);
            clear->r = Rect(r.r() - 10 - bw, y, bw, 22);
        };
        row(ampR, modelLoad, modelPrev, modelNext, modelClear);
        row(cabR, irLoad, irPrev, irNext, irClear);

        const float y0 = 396;
        inPanel = Rect(14, y0, 440, 192);
        inDial->r = Rect(26, y0 + 72, 96, Dial::heightFor(Dial::Large));
        gateDial->r = Rect(142, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(236, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        normBtn->r = Rect(330, y0 + 40, 112, 24);
        chSeg->r = Rect(330, y0 + 98, 112, 24);

        tonePanel = Rect(468, y0, 308, 192);
        bassDial->r = Rect(476, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        midDial->r = Rect(552, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        trebDial->r = Rect(628, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        presDial->r = Rect(704, y0 + 80, 72, Dial::heightFor(Dial::Medium));

        cabPanel = Rect(790, y0, 196, 192);
        cabBtn->r = Rect(802, y0 + 40, 172, 24);
        lowDial->r = Rect(806, y0 + 92, 64, Dial::heightFor(Dial::Small));
        highDial->r = Rect(906, y0 + 92, 64, Dial::heightFor(Dial::Small));
    }

    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        const float ip = pump::gainToDbF(plug->engine.inPeak.exchange(0.0f));
        inLvl = std::max(ip, inLvl - 30.0f * dt);
        const float g = plug->engine.gateFeed.exchange(1.0f);
        gateShow += (g - gateShow) * std::min(1.0f, dt * 15.0f);
        post.update(&plug->engine.outRing, nullptr, dt);
        if (plug->infoSerial() != seenSerial) { seenSerial = plug->infoSerial(); model = plug->modelInfo(); ir = plug->irInfo(); }
    }

    void drawBody() override
    {
        drawAmp();
        drawSpectrumScreen();
        drawCab();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, ampR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        strokeRect(inPanel, theme.dim, 1.0f);
        strokeRect(tonePanel, theme.dim, 1.0f);
        strokeRect(cabPanel, theme.dim, 1.0f);
        caption(inPanel.x + 12, inPanel.y + 16, "INPUT");
        caption(chSeg->r.x, chSeg->r.y - 10, "CHANNELS");
        caption(tonePanel.x + 12, tonePanel.y + 16, "TONE");
        caption(cabPanel.x + 12, cabPanel.y + 16, "CAB");
        // input level and the gate, under the gate dial
        const Rect m(142, inPanel.y + 40, 166, 8);
        caption(m.x, m.y - 9, "IN");
        fillRect(m, theme.panel2);
        const float t = pump::clampv((inLvl + 60.0f) / 60.0f, 0.0f, 1.0f);
        fillRect(Rect(m.x, m.y, m.w * t, m.h), inLvl > -0.5f ? theme.red : theme.neon);
        if (gateActive(value(kGate)))
        {
            const float gx = m.x + m.w * pump::clampv((value(kGate) + 60.0f) / 60.0f, 0.0f, 1.0f);
            line(gx, m.y - 3, gx, m.b() + 3, theme.ink, 2.0f);
        }
        strokeRect(m, theme.ink, 1.0f);
        if (gateActive(value(kGate)))
            textAt(m.r(), m.y - 9, gateShow < 0.5f ? "GATE CLOSED" : "GATE OPEN", kLabel, 9.0f, gateShow < 0.5f ? theme.red : theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
    }

    void drawFileHead(const Rect& r, const char* cap, const std::string& name, const std::string& err, bool loaded, const char* none)
    {
        screenBox(r);
        caption(r.x + 10, r.y + 14, cap);
        const Rect nb(r.x + 10, r.y + 28, r.w - 20, 40);
        fillRect(nb, loaded ? theme.neon : theme.panel);
        strokeRect(nb, theme.ink, 1.5f);
        std::string n = loaded ? name : (err.empty() ? none : "COULDN'T LOAD");
        for (char& c : n) c = (char)std::toupper((unsigned char)c);
        // shorten to fit
        const float maxW = nb.w - 16;
        float size = 14.0f;
        while (size > 9.0f && textWidth(n, kBold, size, 0.6f) > maxW) size -= 0.5f;
        while (n.size() > 4 && textWidth(n, kBold, size, 0.6f) > maxW) n = n.substr(0, n.size() - 4) + "...";
        textAt(nb.x + 8, nb.cy(), n, kBold, size, loaded ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        if (!err.empty()) textAt(r.x + 10, nb.b() + 14, err, kLabel, 9.0f, theme.red, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
    }

    void drawAmp()
    {
        const Rect& r = ampR;
        drawFileHead(r, "AMP MODEL", model.name, model.error, model.loaded && plug && plug->engine.hasModel(), "NO MODEL: LOAD A .NAM FILE");
        float y = r.y + 104;
        auto row = [&](const char* k, const std::string& v) {
            caption(r.x + 10, y, k);
            textAt(r.r() - 10, y, v, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
            line(r.x + 10, y + 10, r.r() - 10, y + 10, theme.grid, 1.0f);
            y += 26;
        };
        char b[64];
        if (model.loaded)
        {
            std::snprintf(b, sizeof b, "%.1f kHz", model.rate / 1000.0); row("TRAINED AT", b);
            row("RUNS AT", model.resampled ? std::string("RESAMPLED") : std::string("HOST RATE"));
            if (model.hasLoudness) { std::snprintf(b, sizeof b, "%.1f dB", model.loudness); row("LOUDNESS", b); }
            else row("LOUDNESS", "NOT IN FILE");
            std::snprintf(b, sizeof b, "%+.1f dB", value(kNormalize) >= 0.5f && model.hasLoudness ? kTargetLoudness - model.loudness : 0.0);
            row("NORMALIZE", b);
        }
        std::snprintf(b, sizeof b, "%d SAMPLES", plug ? plug->latencyNow() : 0);
        row("LATENCY", b);
        row("MODE", value(kChannels) >= 0.5f ? "STEREO (2 MODELS)" : "MONO");
    }

    // the tone stack's response (dB) at f
    double toneDb(double f) const
    {
        const double sr = plug ? plug->getSampleRate() : 48000.0;
        const double g[4] = { value(kBass), value(kMid), value(kTreble), value(kPresence) };
        std::complex<double> h = 1.0;
        const std::complex<double> z1 = std::polar(1.0, -pump::kTwoPi * f / sr), z2 = z1 * z1;
        for (int k = 0; k < 4; ++k)
        {
            Biquad2 b; const ToneBand& t = toneBand(k); b.set(t.type, t.f, t.q, g[k], sr);
            h *= (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
        }
        return 20.0 * std::log10(std::max(1e-9, std::abs(h)));
    }
    void drawSpectrumScreen()
    {
        const Rect& r = specR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SPECTRUM");
        textAt(r.r() - 10, r.y + 14, "OUT  ·  TONE  ·  CAB", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        save();
        scissor(p.x, p.y, p.w, p.h);
        drawFreqGrid(p, kLo, kHi);
        const Rect sp(p.x, p.y, p.w, p.h - 16);
        drawSpectrum(post, sp, -6.0f, -96.0f, withAlpha2(theme.neon, theme.dark ? 0.20f : 0.30f), withAlpha2(pump::mixColor(theme.neon, theme.ink, 0.5f), 0.8f), kLo, kHi, 1.1f);
        // curves: the tone stack (ink) and the cabinet (second colour), on a +-18 dB scale around the middle
        auto yOf = [&](double db) { return (float)(sp.cy() - db / 18.0 * sp.h * 0.45); };
        line(sp.x, sp.cy(), sp.r(), sp.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        if (ir.loaded && value(kCab) >= 0.5f && !ir.response.empty())
        {
            // centred on its level from 200 Hz to 5 kHz (a cabinet's lows and highs fall away steeply)
            double mean = 0; int cnt = 0;
            for (int i = 0; i < kIrPoints; ++i)
            {
                const double f = 20.0 * std::pow(1000.0, (i + 0.5) / kIrPoints);
                if (f >= 200.0 && f <= 5000.0) { mean += ir.response[(size_t)i]; ++cnt; }
            }
            mean /= std::max(1, cnt);
            beginPath();
            for (int i = 0; i < kIrPoints; ++i)
            {
                const double f = 20.0 * std::pow(1000.0, (i + 0.5) / kIrPoints);
                const float x = freqToX(sp, f, kLo, kHi), y = yOf(pump::clampv(ir.response[(size_t)i] - mean, -30.0, 30.0));
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(withAlpha2(theme.second, 0.9f)); strokeWidth(1.6f); stroke();
        }
        beginPath();
        for (int i = 0; i <= 200; ++i)
        {
            const double f = kLo * std::pow(kHi / kLo, i / 200.0);
            const float x = freqToX(sp, f, kLo, kHi), y = yOf(toneDb(f));
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(2.0f); stroke();
        restore();
    }

    void drawCab()
    {
        const Rect& r = cabR;
        drawFileHead(r, "CABINET IR", ir.name, ir.error, ir.loaded, "NO IR: LOAD A .WAV");
        float y = r.y + 104;
        char b[64];
        auto row = [&](const char* k, const std::string& v) {
            caption(r.x + 10, y, k);
            textAt(r.r() - 10, y, v, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
            line(r.x + 10, y + 10, r.r() - 10, y + 10, theme.grid, 1.0f);
            y += 26;
        };
        if (ir.loaded)
        {
            std::snprintf(b, sizeof b, "%.0f ms", ir.seconds * 1000.0); row("LENGTH", b);
            std::snprintf(b, sizeof b, "%.1f kHz", ir.rate / 1000.0); row("RATE", b);
            row("CHANNELS", ir.channels > 1 ? "STEREO" : "MONO");
        }
        row("STATE", value(kCab) >= 0.5f ? (ir.loaded ? "ON" : "ON (NO IR)") : "OFF");
    }

    void bodyHover(float x, float y) override
    {
        if (ampR.contains(x, y)) setStatus(model.loaded ? model.path : "Load a .nam amp model (LOAD, or < > to step through a folder)");
        else if (cabR.contains(x, y)) setStatus(ir.loaded ? ir.path : "Load a cabinet impulse response (.wav)");
        else if (specR.contains(x, y)) setStatus("Output spectrum, with the tone stack's curve and the cabinet's response (+-18 dB)");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "LOAD", "a .nam model or a .wav impulse response" },
            { "< >", "the previous / next file in the same folder" },
            { "Models", "run at the rate they were trained at (resampled when the host differs)" },
        };
    }
    std::string footerInfo() override
    {
        std::string s = model.loaded ? model.name : std::string("NO MODEL");
        s += "   " + (ir.loaded ? ir.name : std::string("NO IR"));
        char b[32]; std::snprintf(b, sizeof b, "   LATENCY %d", plug ? plug->latencyNow() : 0);
        s += b;
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr double kLo = 20.0, kHi = 20000.0;
    PluginAmp* plug = nullptr;
    SpectrumView post;
    ModelInfo model;
    IrInfo ir;
    unsigned seenSerial = ~0u;
    int pendingBrowse = 0;
    Rect ampR, specR, cabR, meterL, meterR, inPanel, tonePanel, cabPanel;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float inLvl = -120.0f, gateShow = 1.0f;

    Dial *inDial, *gateDial, *outDial, *bassDial, *midDial, *trebDial, *presDial, *lowDial, *highDial;
    Button *normBtn, *cabBtn, *modelLoad, *modelPrev, *modelNext, *modelClear, *irLoad, *irPrev, *irNext, *irClear;
    Segmented* chSeg;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIAmp)
};

UI* createUI() { return new UIAmp(); }

END_NAMESPACE_DISTRHO
