// pump SING engine: a singing synthesizer. Framework-free. The voice is a port of Thunder Plus's VoxCore (a formant
// voice written from the published Klatt-style design), extended.
//
// Each note sings the next syllable of the lyrics: its onset consonants on the attack, its vowel held until note-off,
// then its closing consonants. The voice: a glottal pulse (or a buzzy saw: ROBOT) tilted by BRIGHT, with breath noise,
// through five formant resonators (F1-F3 from the phoneme, F4 and F5 fixed, all shifted by SIZE); frication noise
// through its own resonator in parallel for the consonants; nasals through a low hum; CLARITY lifts the formants that
// tell vowels apart. ROUGH adds period-to-period jitter, shimmer and a subharmonic (growl, rasp). VOICE fades the
// voicing out into a whisper.
//
// Play: MONO sings a line. A note that overlaps the last one slides there (GLIDE), closing the last syllable and
// opening the next. POLY sings chords: notes that start while others are held share their syllable. CHOIR: every note
// is sung by up to eight voices, each slightly off in pitch, size, timing and vibrato, spread in stereo. The host's
// transport starting from a stop goes back to the first syllable.
#pragma once

#include "dsp/Synth.hpp"
#include "SingText.hpp"
#include "SingParams.hpp"
#include <array>
#include <atomic>
#include <mutex>

namespace pump { namespace sing {

struct Settings {
    int octave = 0, mode = kModeMono, choir = 1;
    double size = 0.0, robot = 0.2, voice = 1.0, breath = 0.15, bright = 0.55, soft = 0.4, clarity = 0.7, rough = 0.0;
    double vib = 0.3, vibRate = 5.5, vibDelay = 280.0, glide = 50.0, wheel = 0.5;
    double hiss = 0.5, diction = 1.0, attack = 12.0, release = 160.0;
    double spread = 0.4, width = 0.5, velocity = 0.5, drive = 0.0, volumeDb = -6.0;
};

class Voice {
public:
    static constexpr int kBlk = 16, kMaxSegs = 32, kMaxPh = 10;
    enum Kind : int { Hold, Glide, Son, Clo, Burst, Fric, Asp, End };
    struct Seg { int ph; Kind k; int d; };

    void prepare(double sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        rnd = seed * 2654435761u + 12345u;
        reset();
    }
    void reset()
    {
        nseg = 0; si = 0; st = 0; hold = false; coda = false; sounding = false;
        env = 0.0; envT = 0.0; ph = 0.0; lf = 0.0; go = true; vt = 0.0; vph = 0.0; delay = 0;
        for (double& x : r) x = 0.0;
        lp = dc = dcx = an1 = yp = nl = nl2 = 0.0;
        k = Ctl(); tg = Ctl(); curNu = -1; ncoda = 0;
    }
    bool active() const { return sounding; }
    bool closing() const { return coda; }
    int note = -1;
    uint64_t age = 0;
    bool held = false;
    double detune = 0.0, sizeOff = 0.0, vibMul = 1.0, gl = 1.0, gr = 1.0, gain = 1.0;

    // a note starts: syllable s; `legato` closes the syllable being sung first (its closing consonants), `same` keeps
    // singing the vowel (a melisma); `startDelay` samples of silence first (a choir's loose attack)
    void on(const Syllable* s, double noteHz, bool legato, bool same, int startDelay)
    {
        target = noteHz;
        go = env < 0.01;
        if (go) delay = startDelay;
        if (same && curNu >= 0 && hold) { sounding = true; envT = 1.0; return; }
        if (!s || s->rest) { if (sounding) off(); return; }
        nseg = 0; si = 0; st = 0; hold = false; coda = false;
        if (legato && curNu >= 0)
        {
            if (kPhones[curNu].g[0] > 0.0f) push({ curNu, Glide, ms(50) });
            plan(coda_, ncoda, true);
        }
        int on[kMaxPh]; const int non = copyPh(s->on, on);
        curNu = s->nu; ncoda = copyPh(s->co, coda_);
        plan(on, non, false);
        push({ curNu, Hold, -1 });
        envT = 1.0; vt = 0.0; sounding = true;
        enter();
    }
    void off()
    {
        if (curNu < 0) { envT = 0.0; return; }
        nseg = 0; si = 0; st = 0; hold = false; coda = true;
        if (kPhones[curNu].g[0] > 0.0f) push({ curNu, Glide, ms(70) });
        plan(coda_, ncoda, true);
        push({ -1, End, 0 });
        enter();
    }
    void setPitch(double hz) { target = hz; }
    // the formants now (for the window)
    double f1() const { return k.f1; }
    double f2() const { return k.f2; }
    double level() const { return env; }
    int nucleus() const { return curNu; }

    // add n samples into L / R
    void render(float* L, float* R, int n, const Settings& p, double wheel, double bend)
    {
        int i = 0;
        while (i < n && delay > 0) { ++i; --delay; }
        while (i < n) { const int m = std::min(kBlk, n - i); block(L + i, R + i, m, p, wheel, bend); i += m; }
    }

private:
    struct Ctl { double f1 = 500, f2 = 1500, f3 = 2500, av = 0, an = 0, ah = 0, nf = 4000, nb = 3000, nz = 0; };

    static int copyPh(const std::vector<int>& v, int* out) { int n = 0; for (int x : v) if (n < kMaxPh) out[n++] = x; return n; }
    void push(const Seg& g) { if (nseg < kMaxSegs) segs[nseg++] = g; }
    int ms(double x) const { return std::max(1, (int)std::lround(x / 1000.0 * sr / std::max(0.3, diction))); }
    void plan(const int* list, int n, bool isCoda)
    {
        for (int idx = 0; idx < n; ++idx)
        {
            const int x = list[idx];
            if (x < 0) continue;
            const Phone& P = kPhones[x];
            const double d = P.d > 0 ? P.d : 60.0;
            if (P.t == 'P' || P.t == 'B' || P.t == 'C' || P.t == 'J')
            {
                const bool first = !isCoda && idx == 0;
                push({ x, Clo, ms(first ? 25 : (P.t == 'P' || P.t == 'C' ? 55 : 45)) });
                push({ x, Burst, ms(P.t == 'P' ? 12 : 8) });
                if (P.t == 'C' || P.t == 'J') push({ x, Fric, ms(P.t == 'C' ? 95 : 75) });
                else if (P.t == 'P') push({ x, Asp, ms(isCoda ? 20 : 35) });
            }
            else push({ x, P.t == 'F' || P.t == 'Z' ? Fric : P.t == 'H' ? Asp : Son, ms(isCoda && P.t == 'N' ? d * 1.35 : d) });
        }
    }
    // the targets for the segment being entered
    void enter()
    {
        if (si >= nseg) return;
        const Seg& g = segs[si];
        const Phone& P = kPhones[g.ph >= 0 ? g.ph : idAX()];
        const Phone* vow = curNu >= 0 ? &kPhones[curNu] : &kPhones[idAX()];
        if (vow->t != 'V') vow = &kPhones[idAX()];
        const float* f = (g.k == Asp || P.t == 'H') ? (g.k == Asp && coda ? kPhones[idAX()].f : vow->f) : g.k == Glide ? P.g : P.f;
        tg.f1 = f[0]; tg.f2 = f[1]; tg.f3 = f[2];
        tg.av = 0; tg.an = 0; tg.ah = 0; tg.nf = P.nf > 0 ? P.nf : 3000; tg.nb = P.nb > 0 ? P.nb : 3000; tg.nz = g.k == Son && P.t == 'N' ? 1 : 0;
        switch (g.k)
        {
        case Hold: case Glide: case Son: tg.av = P.av < 0 ? 1.0 : P.av; break;
        case Clo: tg.av = P.t == 'B' || P.t == 'J' ? 0.12 : 0.0; break;
        case Burst:
            tg.an = P.ba > 0 ? P.ba : (P.t == 'P' || P.t == 'C' ? 0.7 : 0.45);
            tg.av = P.t == 'B' || P.t == 'J' ? 0.25 : 0.0; tg.nf = P.bf > 0 ? P.bf : 2000; tg.nb = P.bb > 0 ? P.bb : 2500; break;
        case Fric:
        {
            const Phone& F = P.t == 'C' ? kPhones[idSH()] : P.t == 'J' ? kPhones[idZH()] : P;
            tg.an = F.fa > 0 ? F.fa : 0.4; tg.av = F.av > 0 ? F.av : 0.0; tg.nf = F.nf > 0 ? F.nf : 4000; tg.nb = F.nb > 0 ? F.nb : 3000; break;
        }
        case Asp: tg.ah = 0.26; break;
        case End: envT = 0.0; break;
        }
        if (go) { k.f1 = tg.f1; k.f2 = tg.f2; k.f3 = tg.f3; k.nf = tg.nf; k.nb = tg.nb; go = false; }
    }
    // a two-pole resonator (Klatt): coefficients per block, state per resonator
    struct Res { double a = 1, b = 0, c = 0; };
    static Res reso(double F, double B, double sr)
    {
        const double T = 1.0 / sr;
        Res o;
        o.c = -std::exp(-2.0 * kPi * B * T);
        o.b = 2.0 * std::exp(-kPi * B * T) * std::cos(2.0 * kPi * std::min(F, sr * 0.45) * T);
        o.a = 1.0 - o.b - o.c;
        return o;
    }
    inline double run(int j, double x, const Res& q) { const double y = q.a * x + q.b * r[j * 2] + q.c * r[j * 2 + 1]; r[j * 2 + 1] = r[j * 2]; r[j * 2] = y; return y; }
    inline double noise() { rnd = rnd * 1664525u + 1013904223u; return rnd / 2147483648.0 - 1.0; }

    void block(float* L, float* R, int n, const Settings& p, double wheel, double bend)
    {
        diction = p.diction;
        const double dt = n / sr;
        // move through the plan
        while (si < nseg && segs[si].d >= 0 && st >= segs[si].d && si < nseg - 1) { ++si; st = 0; enter(); }
        const Seg* g = si < nseg ? &segs[si] : nullptr;
        if (g && g->k == Hold) hold = true;
        st += n;
        if (coda && g && g->k == End && env < 1e-4) { sounding = false; curNu = -1; coda = false; return; }
        // controls glide toward their targets: levels in ~4 ms; formants ~10 ms into a consonant, ~16 ms through L R W
        // Y and the nasals, ~24 ms into a vowel (scaled by diction)
        const double tfm = g && (g->k == Hold || g->k == Glide) ? 24.0 : g && g->k == Son ? 16.0 : 10.0;
        const double kf = 1.0 - std::exp(-dt * 1000.0 / (tfm / std::max(0.3, p.diction))), ka = 1.0 - std::exp(-dt * 1000.0 / 4.0);
        k.f1 += (tg.f1 - k.f1) * kf; k.f2 += (tg.f2 - k.f2) * kf; k.f3 += (tg.f3 - k.f3) * kf;
        k.av += (tg.av - k.av) * ka; k.an += (tg.an - k.an) * ka; k.ah += (tg.ah - k.ah) * ka; k.nf += (tg.nf - k.nf) * ka; k.nb += (tg.nb - k.nb) * ka; k.nz += (tg.nz - k.nz) * ka;
        // the amplitude envelope
        const double er = envT > env ? 1.0 - std::exp(-dt * 1000.0 / std::max(0.5, p.attack)) : 1.0 - std::exp(-dt * 1000.0 * 4.6 / std::max(1.0, p.release));
        env += (envT - env) * er;
        if (envT == 0.0 && env < 1e-4) { env = 0.0; if (!coda || !g || g->k == End) { sounding = false; curNu = -1; coda = false; return; } }
        // pitch, with glide and vibrato (which fades in after its delay)
        const double lt = std::log2(std::max(20.0, target * std::exp2((p.octave * 1200.0 + detune + bend * 100.0) / 1200.0)));
        if (go || lf <= 0.0) lf = lt;
        if (p.glide > 0.5) lf += (lt - lf) * (1.0 - std::exp(-dt * 3000.0 / p.glide)); else lf = lt;   // ~95 % of the way in the glide time
        vt += dt;
        const double vd = clampv((vt * 1000.0 - p.vibDelay) / 300.0, 0.0, 1.0);
        const double vib = clampv(p.vib + wheel * p.wheel, 0.0, 1.5);
        const double hs = std::max(0.0, p.hiss) * 2.0, cl = p.clarity;
        const double vc = clampv(p.voice, 0.0, 1.0), fs = std::exp2((p.size + sizeOff) * 0.35), bwm = (0.6 + p.soft * 0.9) * (1.3 - cl * 0.6), rob = p.robot, br = p.breath;
        const double kb = 1.0 - std::exp(-kTwoPi * (1200.0 * std::pow(10.0, p.bright)) / sr);
        const double F1 = k.f1 * fs, F2 = k.f2 * fs, F3 = k.f3 * fs;
        const Res q0 = reso(F1, 60 * bwm, sr), q1 = reso(F2, 90 * bwm, sr), q2 = reso(F3, 150 * bwm, sr), q3 = reso(3350 * fs, 250, sr), q4 = reso(3850 * fs, 300, sr);
        const Res q5 = reso(k.nf * std::sqrt(fs), k.nb, sr);
        const double nas = k.nz, knl = 1.0 - std::exp(-kTwoPi * 650.0 * std::sqrt(fs) / sr);
        const double rough = p.rough;
        const double* G = glot().data();
        const double vr = p.vibRate * vibMul;
        for (int i = 0; i < n; ++i)
        {
            vph += vr / sr; if (vph >= 1.0) vph -= 1.0;
            const double f0 = std::exp2(lf + vd * vib * 0.06 * std::sin(kTwoPi * vph)) * jit;
            const double inc = f0 / sr;
            ph += inc;
            if (ph >= 1.0)
            {
                ph -= 1.0;
                // a new period: ROUGH picks its jitter, shimmer and (every other period) the subharmonic dip
                odd = !odd;
                jit = rough > 0.0 ? 1.0 + rough * 0.025 * noise() : 1.0;
                shim = rough > 0.0 ? (1.0 + rough * 0.25 * noise()) * (odd ? 1.0 - rough * 0.55 : 1.0) : 1.0;
            }
            // the source: a smooth glottal pulse <-> a buzzy saw (ROBOT), tilted by BRIGHT
            const double x = ph * 2048.0; const int xi = (int)x;
            const double glv = G[xi] + (G[xi + 1] - G[xi]) * (x - xi);
            double saw = 2.0 * ph - 1.0;
            { double t = ph; if (t < inc) { t /= inc; saw -= t + t - t * t - 1.0; } else if (t > 1.0 - inc) { t = (t - 1.0) / inc; saw -= t * t + t + t + 1.0; } }
            double src = (glv * (1.0 - rob) + saw * rob * 0.8) * shim;
            lp += (src - lp) * kb; src = lp;
            const double nz = noise();
            const double open = ph < 0.6 ? 1.0 : 0.35;   // breath is louder while the glottis is open
            an1 += (nz - an1) * 0.45; const double anz = an1 * 1.3;   // softened aspiration (H, the puff after P T K)
            const double voiced = src * k.av * vc + (anz * k.ah * hs + nz * br * k.av * (0.5 + (1.0 - vc)) * open) * 0.6;   // VOICE 0 = a whisper
            double y = run(0, voiced, q0); y = run(1, y, q1); y = run(2, y, q2); y = run(3, y, q3); y = run(4, y, q4);
            // M, N, NG: the sound comes out of the nose, a low hum with little above ~650 Hz
            nl += (y - nl) * knl; nl2 += (nl - nl2) * knl; if (nas > 0.001) y += (nl2 * 1.6 - y) * nas;
            const double fr = run(5, nz * k.an * hs, q5) * 1.6;   // frication, in parallel
            const double pres = y - yp; yp = y;                    // CLARITY: a presence lift
            double out = y * 0.5 + pres * cl * 0.9 + fr;
            const double o = out - dcx + 0.995 * dc; dcx = out; dc = o;
            out = o * env * gain * 0.32;
            L[i] += (float)(out * gl); R[i] += (float)(out * gr);
        }
    }
    static const std::array<double, 2049>& glot()
    {
        // the glottal pulse's derivative over one period (open phase 60 %), zero mean, peak 1
        static const std::array<double, 2049> T = [] {
            std::array<double, 2049> t {};
            const double oq = 0.6;
            for (int i = 0; i <= 2048; ++i) { const double x = i / 2048.0; t[(size_t)i] = x < oq ? (2 * x / oq - 3 * (x / oq) * (x / oq)) : 0.0; }
            double m = 0; for (double v : t) m += v; m /= 2049.0;
            double pk = 0; for (double& v : t) { v -= m; pk = std::max(pk, std::fabs(v)); }
            for (double& v : t) v /= pk;
            return t;
        }();
        return T;
    }

    double sr = 48000.0, diction = 1.0, target = 261.6;
    Seg segs[kMaxSegs] {};
    int nseg = 0, si = 0, st = 0, curNu = -1, ncoda = 0, delay = 0;
    int coda_[kMaxPh] {};
    bool hold = false, coda = false, sounding = false, go = true, odd = false;
    double env = 0, envT = 0, ph = 0, lf = 0, vt = 0, vph = 0, jit = 1.0, shim = 1.0;
    Ctl k, tg;
    double r[12] {};
    double lp = 0, dc = 0, dcx = 0, an1 = 0, yp = 0, nl = 0, nl2 = 0;
    uint32_t rnd = 12345u;
};

class Engine {
public:
    static constexpr int kMaxVoices = 16;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (int i = 0; i < kMaxVoices; ++i) voices[(size_t)i].prepare(sr, (uint32_t)(i + 1));
        vm.setup(1, synth::kMono); vm.reset();
        heldN = 0; pos = 0;
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
    }
    void setSettings(const Settings& s) { set = s; }
    // any thread but the audio one: new lyrics (they take effect at the next block, from the first syllable)
    void setLyrics(const std::string& text)
    {
        auto parsed = Lyrics::parse(text);
        std::lock_guard<std::mutex> g(lyricLock);
        pending = std::move(parsed);
        hasPending = true;
    }
    void restart() { restartFlag.store(true); }
    void setTransport(bool playing) { if (playing && !wasPlaying) restartFlag.store(true); wasPlaying = playing; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int syllableNow() const { return sylFeed.load(std::memory_order_relaxed); }   // the syllable sung last (-1 none)
    int nextSyllable() const { return nextFeed.load(std::memory_order_relaxed); }
    float f1Now() const { return f1Feed.load(std::memory_order_relaxed); }
    float f2Now() const { return f2Feed.load(std::memory_order_relaxed); }
    float levelNow() const { return levelFeed.load(std::memory_order_relaxed); }
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        if (hasPending.load() && lyricLock.try_lock())
        {
            if (hasPending) { std::swap(syl, pending); hasPending = false; pos = 0; }
            lyricLock.unlock();
        }
        if (restartFlag.exchange(false)) pos = 0;
        volS.setTarget(dbToGain(set.volumeDb));
        std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f);
        int done = 0, e = 0;
        while (done < n)
        {
            while (e < nev && (int)ev[e].offset <= done) handle(ev[e++]);
            const int until = e < nev ? std::min(n, (int)ev[e].offset) : n;
            const int m = until - done;
            if (m > 0)
            {
                for (auto& v : voices) if (v.active()) v.render(L + done, R + done, m, set, vm.wheel(), vm.pitchBend() * 2.0);
                done = until;
            }
        }
        while (e < nev) handle(ev[e++]);
        // drive, volume
        const double dr = set.drive > 0.001 ? 1.0 + set.drive * set.drive * 8.0 : 0.0, dn = dr > 0.0 ? 1.0 / std::tanh(dr) : 1.0;
        for (int i = 0; i < n; ++i)
        {
            const double vol = volS.next();
            double l = L[i], r = R[i];
            if (dr > 0.0) { l = std::tanh(l * dr) * dn; r = std::tanh(r * dr) * dn; }
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) v.reset(); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        // feeds
        int act = 0; float lv = 0; uint64_t lit[2] = { 0, 0 };
        const Voice* lead = nullptr;
        for (const auto& v : voices)
        {
            if (!v.active()) continue;
            ++act; lv = std::max(lv, (float)v.level());
            if (v.held && v.note >= 0) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63);
            if (!lead || v.age > lead->age) lead = &v;
        }
        if (lead) { f1Feed.store((float)lead->f1()); f2Feed.store((float)lead->f2()); }
        levelFeed.store(lv); activeFeed.store(act);
        litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        nextFeed.store(syl.empty() ? -1 : (int)(pos % syl.size()));
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t note, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), note, vel }; handle(m); }
    int syllableCount() const { return (int)syl.size(); }

private:
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (st == 0x90 && m.d2 > 0) noteOn(m.d1, m.d2 / 127.0);
        else if (st == 0x80 || (st == 0x90 && m.d2 == 0)) noteOff(m.d1);
        else vm.midi(m.status, m.d1, m.d2, [](const synth::VoiceCmd&) {});   // bend, wheel
        if (st == 0xB0 && (m.d1 == 120 || m.d1 == 123)) { for (auto& v : voices) { v.held = false; if (m.d1 == 120) v.reset(); else if (v.active()) v.off(); } heldN = 0; }
    }
    // the next syllable to sing (and move on), or the one being sung (a chord)
    const Syllable* take(bool advance, bool& extend)
    {
        extend = false;
        if (syl.empty()) return nullptr;
        if (!advance) return lastSyl >= 0 && lastSyl < (int)syl.size() ? &syl[(size_t)lastSyl] : nullptr;
        const int i = (int)(pos % syl.size());
        ++pos;
        const Syllable* s = &syl[(size_t)i];
        if (s->extend) { extend = true; return lastSyl >= 0 && lastSyl < (int)syl.size() ? &syl[(size_t)lastSyl] : nullptr; }
        lastSyl = i;
        sylFeed.store(i);
        return s;
    }
    void unisonShape(Voice& v, int k, int count, double vel)
    {
        const double sp = count > 1 ? set.spread : 0.0;
        const double t = count > 1 ? (k / (double)(count - 1)) * 2.0 - 1.0 : 0.0;   // -1 .. 1 across the choir
        v.detune = sp * 18.0 * t + sp * 6.0 * rng.bi();
        v.sizeOff = sp * 0.12 * rng.bi();
        v.vibMul = 1.0 + sp * 0.12 * rng.bi();
        const double pan = count > 1 ? clampv(t * set.width, -1.0, 1.0) : 0.0;
        v.gl = std::cos((pan + 1.0) * kPi * 0.25) * 1.41421356; v.gr = std::sin((pan + 1.0) * kPi * 0.25) * 1.41421356;
        v.gain = (1.0 - set.velocity * (1.0 - vel)) / std::sqrt((double)count);
    }
    double hzOf(int note) const { return synth::midiToHz(note); }

    void noteOn(int note, double vel)
    {
        pushHeld(note);
        const int count = clampv(set.choir, 1, 8);
        if (set.mode == kModeMono)
        {
            bool legato = false;
            for (int i = 0; i < count; ++i) if (voices[(size_t)i].active() && voices[(size_t)i].held && !voices[(size_t)i].closing()) legato = true;
            bool extend = false;
            const Syllable* s = take(true, extend);
            for (int i = 0; i < kMaxVoices; ++i)
            {
                Voice& v = voices[(size_t)i];
                if (i >= count) { if (v.active()) v.off(); v.held = false; continue; }
                if (!legato || !v.active()) unisonShape(v, i, count, vel);
                v.note = note; v.held = true; v.age = ++counter;
                v.on(s, hzOf(note), legato, extend, legato ? 0 : (int)(set.spread * rng.uni() * 0.025 * sr * (count > 1 ? 1 : 0)));
            }
            return;
        }
        // poly: a chord (other notes held) shares the syllable being sung
        bool extend = false;
        const Syllable* s = take(heldN <= 1, extend);
        for (int k = 0; k < count; ++k)
        {
            Voice& v = pick();
            unisonShape(v, k, count, vel);
            v.note = note; v.held = true; v.age = ++counter;
            v.on(s, hzOf(note), false, extend, (int)(set.spread * rng.uni() * 0.025 * sr * (count > 1 ? 1 : 0)));
        }
    }
    void noteOff(int note)
    {
        popHeld(note);
        if (set.mode == kModeMono)
        {
            bool any = false;
            for (auto& v : voices) if (v.held && v.note == note) any = true;
            if (!any) return;
            if (heldN > 0)
            {   // back to the last note still held: the pitch slides there, the syllable stays
                const int back = held[heldN - 1];
                for (auto& v : voices) if (v.held) { v.note = back; v.setPitch(hzOf(back)); }
                return;
            }
            for (auto& v : voices) if (v.held) { v.held = false; v.off(); }
            return;
        }
        for (auto& v : voices) if (v.held && v.note == note) { v.held = false; v.off(); }
    }
    Voice& pick()
    {
        for (auto& v : voices) if (!v.active()) return v;
        Voice* best = nullptr;
        for (auto& v : voices) if (!v.held && (!best || v.age < best->age)) best = &v;
        if (!best) for (auto& v : voices) if (!best || v.age < best->age) best = &v;
        best->reset();
        return *best;
    }
    void pushHeld(int note) { popHeld(note); if (heldN < 128) held[heldN++] = note; }
    void popHeld(int note) { int k = 0; for (int i = 0; i < heldN; ++i) if (held[i] != note) held[k++] = held[i]; heldN = k; }

    Settings set;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;   // bend, wheel
    fx::Rng rng { 0x5106u };
    std::vector<Syllable> syl, pending;
    std::mutex lyricLock;
    std::atomic<bool> hasPending { false }, restartFlag { false };
    bool wasPlaying = false;
    size_t pos = 0;
    int lastSyl = -1, held[128] = {}, heldN = 0;
    uint64_t counter = 0;
    double sr = 48000.0;
    LinearSmoother volS;
    std::atomic<int> sylFeed { -1 }, nextFeed { 0 }, activeFeed { 0 };
    std::atomic<float> f1Feed { 500.0f }, f2Feed { 1500.0f }, levelFeed { 0.0f };
    std::atomic<uint64_t> litFeed[2] {};
};

}} // namespace pump::sing
