// pump ROOM: the audio side. One AudioWorklet both plays the test signal and records the microphone, in the same
// render calls, so the recording and the playback stay locked together sample for sample: the delay between them
// (the system's latency) is the same for every speaker in a run, and the difference in arrival times is real.
//
// Messages in:  {type:'run', out:[Float32Array L, Float32Array R], length}   play + record `length` frames
//               {type:'stop'}                                                  abort a run (sends 'aborted')
//               {type:'noise', on, left, right, level, flipRight}              pink noise for setting the level: `level` is
//                                                                              its RMS (dBFS), flipRight inverts the right
//                                                                              channel (the polarity listening test)
//               {type:'sim', on}                                               simulated room instead of the mic (tests)
//               {type:'simMic', x, y}                                          move the simulated mic (metres; speakers at
//                                                                              x = -1.2 / +1.2, y = 0; the listener at y > 0)
// Messages out: {type:'meter', peak, rms, outPeak}   every ~50 ms (dBFS of the mic)
//               {type:'progress', done, length}
//               {type:'done', rec:Float32Array, dropped}   dropped = render calls that had no mic input
//               {type:'aborted'}

class RoomIO extends AudioWorkletProcessor {
    constructor() {
        super();
        this.run = null;
        this.noise = { on: false, left: true, right: true, flipRight: false, gain: 0 };
        this.pink = [0, 0, 0, 0, 0, 0, 0];
        this.meterPeak = 0; this.meterSum = 0; this.meterN = 0; this.outPeak = 0;
        this.meterEvery = Math.round(sampleRate * 0.05);
        this.sim = null;
        this.port.onmessage = (e) => this.message(e.data);
    }

    message(m) {
        if (m.type === 'run') {
            this.run = { out: m.out, length: m.length, pos: 0, rec: new Float32Array(m.length), dropped: 0, lastProgress: 0 };
        } else if (m.type === 'stop') {
            if (this.run) { this.run = null; this.port.postMessage({ type: 'aborted' }); }
        } else if (m.type === 'noise') {
            // pinkSample() is about -14.2 dBFS RMS: scale it to `level`
            this.noise = { on: !!m.on, left: !!m.left, right: !!m.right, flipRight: !!m.flipRight,
                           gain: Math.pow(10, ((m.level || -20) + 14.2) / 20) };
        } else if (m.type === 'sim') {
            this.sim = m.on ? this.makeSim() : null;
        } else if (m.type === 'simMic' && this.sim) {
            this.placeSimMic(m.x, m.y);
        }
    }

    // A test room: the left speaker 2.9 m away, the right 3.1 m (about 28 samples later at 48 kHz), a room mode
    // at 110 Hz, a dip at 3 kHz, two reflections each, 12 ms of system latency and a little noise.
    makeSim() {
        const fs = sampleRate;
        const peak = (f, q, db) => {
            const A = Math.pow(10, db / 40), w = 2 * Math.PI * f / fs, al = Math.sin(w) / (2 * q), c = Math.cos(w);
            const a0 = 1 + al / A;
            return { b0: (1 + al * A) / a0, b1: -2 * c / a0, b2: (1 - al * A) / a0, a1: -2 * c / a0, a2: (1 - al / A) / a0, z1: 0, z2: 0 };
        };
        const hp = (f, q) => {
            const w = 2 * Math.PI * f / fs, al = Math.sin(w) / (2 * q), c = Math.cos(w), a0 = 1 + al;
            return { b0: (1 + c) / 2 / a0, b1: -(1 + c) / a0, b2: (1 + c) / 2 / a0, a1: -2 * c / a0, a2: (1 - al) / a0, z1: 0, z2: 0 };
        };
        const lat = Math.round(0.012 * fs), size = 1 << 17;
        const chan = (dist, refl) => ({
            filters: [hp(40, 0.7), peak(110, 5, 9), peak(3000, 1.5, -5)],
            taps: [{ d: lat + Math.round(dist / 343 * fs), g: 0.5 }].concat(refl.map(([d, g]) => ({ d: lat + Math.round(d / 343 * fs), g }))),
            extra: refl.map(([d]) => d - dist),   // the reflections' extra path (they move with the mic)
        });
        return {
            size, buf: [new Float32Array(size), new Float32Array(size)], w: 0,
            ch: [chan(2.9, [[4.1, 0.22], [6.3, 0.12]]), chan(3.1, [[3.8, 0.2], [7.0, 0.1]])],
            seed: 12345, lat, speakers: [[-1.2, 0], [1.2, 0]],
        };
    }

    placeSimMic(x, y) {
        const s = this.sim, fs = sampleRate;
        s.ch.forEach((c, i) => {
            const [sx, sy] = s.speakers[i];
            const dist = Math.max(0.01, Math.hypot(x - sx, y - sy));
            c.taps[0].d = s.lat + Math.round(dist / 343 * fs);
            c.extra.forEach((e, k) => { c.taps[k + 1].d = s.lat + Math.round((dist + e) / 343 * fs); });
        });
    }

    simSample(outL, outR) {
        const s = this.sim;
        const run = (x, f) => { for (const b of f) { const y = b.b0 * x + b.z1; b.z1 = b.b1 * x - b.a1 * y + b.z2; b.z2 = b.b2 * x - b.a2 * y; x = y; } return x; };
        s.buf[0][s.w] = run(outL, s.ch[0].filters);
        s.buf[1][s.w] = run(outR, s.ch[1].filters);
        let y = 0;
        for (let c = 0; c < 2; ++c)
            for (const t of s.ch[c].taps) y += t.g * s.buf[c][(s.w - t.d + s.size) & (s.size - 1)];
        s.w = (s.w + 1) & (s.size - 1);
        s.seed = (s.seed * 1664525 + 1013904223) >>> 0;
        return y + (s.seed / 4294967296 - 0.5) * 2e-4;
    }

    pinkSample() {
        // Paul Kellet's pink filter on white noise
        const w = Math.random() * 2 - 1, b = this.pink;
        b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759;
        b[2] = 0.96900 * b[2] + w * 0.1538520; b[3] = 0.86650 * b[3] + w * 0.3104856;
        b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
        const p = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362;
        b[6] = w * 0.115926;
        return p * 0.11;
    }

    process(inputs, outputs) {
        const out = outputs[0];
        const outL = out[0], outR = out.length > 1 ? out[1] : null;
        const n = outL ? outL.length : 128;
        const input = inputs[0] && inputs[0].length > 0 ? inputs[0][0] : null;
        const r = this.run;

        for (let i = 0; i < n; ++i) {
            let l = 0, rr = 0;
            if (r) {
                const p = r.pos + i;
                if (p < r.length) {
                    l = p < r.out[0].length ? r.out[0][p] : 0;
                    rr = p < r.out[1].length ? r.out[1][p] : 0;
                }
            } else if (this.noise.on) {
                let v = this.pinkSample() * this.noise.gain;
                // pink noise peaks ~13 dB over its RMS: at high levels, round the peaks off below full scale
                const av = Math.abs(v);
                if (av > 0.7) v = Math.sign(v) * (0.7 + 0.25 * Math.tanh((av - 0.7) / 0.25));
                l = this.noise.left ? v : 0;
                rr = this.noise.right ? (this.noise.flipRight ? -v : v) : 0;
            }
            const x = this.sim ? this.simSample(l, rr) : (input ? input[i] : 0);
            if (outL) outL[i] = this.sim ? 0 : l;
            if (outR) outR[i] = this.sim ? 0 : rr;
            if (r && r.pos + i < r.length) r.rec[r.pos + i] = x;
            const ax = Math.abs(x);
            if (ax > this.meterPeak) this.meterPeak = ax;
            this.meterSum += x * x;
            this.outPeak = Math.max(this.outPeak, Math.abs(l), Math.abs(rr));
            if (++this.meterN >= this.meterEvery) {
                this.port.postMessage({ type: 'meter', peak: this.meterPeak, rms: Math.sqrt(this.meterSum / this.meterN), outPeak: this.outPeak });
                this.meterPeak = 0; this.meterSum = 0; this.meterN = 0; this.outPeak = 0;
            }
        }
        if (r) {
            if (!input && !this.sim) r.dropped++;
            r.pos += n;
            if (r.pos >= r.length) {
                this.port.postMessage({ type: 'done', rec: r.rec, dropped: r.dropped }, [r.rec.buffer]);
                this.run = null;
            } else if (r.pos - r.lastProgress >= sampleRate * 0.1) {
                r.lastProgress = r.pos;
                this.port.postMessage({ type: 'progress', done: r.pos, length: r.length });
            }
        }
        return true;
    }
}

registerProcessor('room-io', RoomIO);
