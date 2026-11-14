// pump CONVERT, PLAY page: the audio thread. Chunks of 32-bit float PCM (interleaved, 1 or 2 channels) arrive from the
// window and are copied to the output one after the other, so two tracks pushed back to back play with no gap. The
// samples are written as they are: no gain, no filter. Reports where it is (and the peaks) about every 25 ms.
class PumpPlayer extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = [];            // chunks waiting: { id, data, ch, frames, off, start, last }
    this.cur = null;
    this.curId = -1;        // the stream the last rendered frame came from
    this.epoch = 0;
    this.consumed = 0;      // frames taken from the queue since the last flush
    this.pos = 0;           // the position inside the current stream, in frames
    this.since = 0; this.pkL = 0; this.pkR = 0; this.dry = false;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'push') this.q.push({ id: m.id, data: new Float32Array(m.data), ch: m.ch, frames: m.frames, off: 0, start: m.start, last: !!m.last });
      else if (m.type === 'flush') { this.q = []; this.cur = null; this.curId = -1; this.epoch = m.epoch; this.consumed = 0; this.pos = 0; this.dry = false; }
      else if (m.type === 'drop') this.q = this.q.filter((c) => c.id === m.keep);   // the queued next track is no longer wanted
    };
  }
  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;
    let i = 0, underrun = false;
    while (i < n) {
      if (!this.cur) {
        this.cur = this.q.shift();
        if (!this.cur) { underrun = true; break; }
        if (this.cur.id !== this.curId) { this.curId = this.cur.id; this.port.postMessage({ type: 'started', epoch: this.epoch, id: this.curId }); }
      }
      const c = this.cur, take = Math.min(c.frames - c.off, n - i), d = c.data;
      if (c.ch === 1) {
        for (let k = 0, o = c.off; k < take; k++, o++) { const s = d[o]; L[i + k] = s; R[i + k] = s; const a = s < 0 ? -s : s; if (a > this.pkL) this.pkL = a; }
        if (this.pkL > this.pkR) this.pkR = this.pkL;
      } else {
        for (let k = 0, o = c.off * 2; k < take; k++, o += 2) {
          const l = d[o], r = d[o + 1]; L[i + k] = l; R[i + k] = r;
          const al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
          if (al > this.pkL) this.pkL = al; if (ar > this.pkR) this.pkR = ar;
        }
      }
      c.off += take; i += take; this.consumed += take; this.pos = c.start + c.off;
      if (c.off >= c.frames) {
        this.cur = null;
        if (c.last) this.port.postMessage({ type: 'ended', epoch: this.epoch, id: c.id });
      }
    }
    for (; i < n; i++) { L[i] = 0; R[i] = 0; }
    this.since += n;
    if (this.since >= sampleRate * 0.025) {
      this.port.postMessage({ type: 'pos', epoch: this.epoch, id: this.curId, frame: this.pos, consumed: this.consumed, pkL: this.pkL, pkR: this.pkR, dry: underrun });
      this.since = 0; this.pkL = 0; this.pkR = 0;
    }
    return true;
  }
}
registerProcessor('pump-player', PumpPlayer);
