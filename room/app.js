// pump ROOM: room measurement in the browser. Plays log sweeps out of each speaker, records the measurement mic,
// and shows the response (with the mic's calibration file applied) and the arrival times. The DSP is pump's own
// (common/dsp/Room.hpp, compiled to pump-room.wasm); this file is the page around it.
'use strict';

// ---------------------------------------------------------------------------------------------------------------------
// the wasm module

const W = (() => {
  const bin = Uint8Array.from(atob(ROOM_WASM), (c) => c.charCodeAt(0));
  // a reactor built with wasi-libc: none of its system calls are used here, so they are stubs
  const wasi = new Proxy({}, { get: (_, name) => (name === 'proc_exit' ? () => { throw new Error('exit'); } : () => 52) });
  const inst = new WebAssembly.Instance(new WebAssembly.Module(bin), { wasi_snapshot_preview1: wasi });
  const x = inst.exports;
  if (x._initialize) x._initialize();
  const mem = () => x.memory.buffer;
  return {
    x,
    f32: (ptr, n) => new Float32Array(mem(), ptr, n),
    f64: (ptr, n) => new Float64Array(mem(), ptr, n),
    putF32(arr) {
      const p = x.room_alloc(arr.length * 4);
      new Float32Array(mem(), p, arr.length).set(arr);
      return p;
    },
    putText(text) {
      const b = new TextEncoder().encode(text);
      const p = x.room_alloc(Math.max(1, b.length));
      new Uint8Array(mem(), p, b.length).set(b);
      return [p, b.length];
    },
  };
})();

// ---------------------------------------------------------------------------------------------------------------------
// colours (common/ui/Theme.hpp's recipe, ROOM's accent)

const ACCENT = { neon: 0xffc86b, deep: 0x6e4300, second: 0x2f7bff };
const rgb = (h) => [(h >> 16) & 255, (h >> 8) & 255, h & 255];
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
const css = (c, a = 1) => `rgba(${c.map((v) => Math.round(v)).join(',')},${a})`;
let theme = {};

function buildTheme(dark) {
  const white = rgb(0xffffff), black = rgb(0x07070b), neon = rgb(ACCENT.neon), deep = rgb(ACCENT.deep), sec = rgb(ACCENT.second);
  const t = {};
  if (!dark) {
    t.bg = mix(white, neon, 0.16); t.panel = mix(white, neon, 0.08); t.panel2 = mix(white, neon, 0.3);
    t.ink = deep; t.ink2 = mix(deep, t.bg, 0.38); t.dim = mix(mix(neon, deep, 0.3), white, 0.42);
    t.neon = neon; t.neonInk = deep; t.on = mix(white, neon, 0.06); t.second = mix(sec, black, 0.08);
    t.red = rgb(0xd0302a); t.grid = css(deep, 0.11); t.screen = mix(white, neon, 0.05);
  } else {
    t.bg = mix(black, neon, 0.07); t.panel = mix(black, neon, 0.1); t.panel2 = mix(black, neon, 0.19);
    t.ink = mix(neon, white, 0.18); t.ink2 = mix(neon, t.bg, 0.38); t.dim = mix(neon, t.bg, 0.7);
    t.neon = neon; t.neonInk = mix(black, neon, 0.05); t.on = mix(black, neon, 0.05); t.second = mix(sec, white, 0.15);
    t.red = rgb(0xff5a4e); t.grid = css(neon, 0.1); t.screen = mix(black, neon, 0.04);
  }
  const root = document.documentElement.style;
  for (const [k, v] of Object.entries(t)) root.setProperty('--' + k, typeof v === 'string' ? v : css(v));
  t.dark = dark;
  // curve colours: left in ink, right in the second colour, further runs in their own hues
  t.curves = dark
    ? [css(t.ink), css(t.second), '#5ee0a0', '#ff7ab8', '#b49cff', '#ff9f5a', '#6fd3ff', '#e6e05a']
    : [css(t.ink), css(t.second), '#178a55', '#c2306f', '#6a4fd6', '#c45f12', '#1f87b8', '#7d7a10'];
  theme = t;
  $('theme').textContent = dark ? 'LIGHT' : 'DARK';
  renderRuns();
  draw();
}

// ---------------------------------------------------------------------------------------------------------------------
// state

const $ = (id) => document.getElementById(id);
const S = {
  ctx: null, node: null, stream: null, source: null, splitter: null,
  micId: '', outId: '', micCh: 0, fs: 0,
  cal: null,                 // { name, text, points, lo, hi, sens }
  runs: [], nextColour: 0,
  chans: 'LR', seconds: 4, level: -18,
  view: 'response', smooth: 12, windowMs: 500, norm: true, useCal: true,
  busy: false, noise: false, pending: null,
  corr: { tilt: 0, bass: 0, treble: 0, amount: 100, fLo: 20, fHi: 20000, boost: 6, cut: 15, nulls: true, low: true, align: true },
  design: null,
  sim: /(^|[#&?])sim\b/.test(location.hash + location.search),
};
const GRID = { lo: 20, hi: 20000, perOct: 96 };
const TAIL_S = 2.0, PRE_S = 0.5, SEARCH_S = 1.0, SPEED = 343;

function status(text, err = false) {
  const s = $('status');
  s.textContent = text;
  s.classList.toggle('err', !!err);
}

function store(key, value) { try { localStorage.setItem('pump-room-' + key, value); } catch (e) {} }
function recall(key) { try { return localStorage.getItem('pump-room-' + key); } catch (e) { return null; } }

// ---------------------------------------------------------------------------------------------------------------------
// audio

async function startAudio() {
  await stopAudio();
  try {
    if (!S.sim) {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia)
        throw new Error('This browser has no microphone access here. Open the page in Chrome or Edge.');
      const audio = { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: { ideal: 2 } };
      if (S.micId) audio.deviceId = { exact: S.micId };
      S.stream = await navigator.mediaDevices.getUserMedia({ audio });
    }
    S.ctx = new AudioContext({ latencyHint: 'interactive' });
    if (S.outId && S.ctx.setSinkId) {
      try { await S.ctx.setSinkId(S.outId); } catch (e) { S.outId = ''; }
    }
    // a data: URL, not a blob: one: opened as a file (origin null) a blob: module is refused as cross-origin
    await S.ctx.audioWorklet.addModule('data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(ROOM_WORKLET))));
    S.node = new AudioWorkletNode(S.ctx, 'room-io', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 1, channelCountMode: 'explicit',
    });
    S.node.port.onmessage = (e) => fromWorklet(e.data);
    if (S.stream) {
      S.source = S.ctx.createMediaStreamSource(S.stream);
      const n = Math.max(1, S.source.channelCount);
      const settings = S.stream.getAudioTracks()[0]?.getSettings?.() || {};
      const chans = Math.max(1, settings.channelCount || n);
      S.splitter = S.ctx.createChannelSplitter(Math.max(2, chans));
      S.source.connect(S.splitter);
      if (S.micCh >= chans) S.micCh = 0;
      S.splitter.connect(S.node, S.micCh, 0);
      fillMicChannels(chans);
    }
    S.node.connect(S.ctx.destination);
    if (S.sim) S.node.port.postMessage({ type: 'sim', on: true });
    await S.ctx.resume();
    S.fs = S.ctx.sampleRate;
    $('rate').textContent = (S.fs / 1000).toFixed(1) + ' kHz';
    await listDevices();
    $('start').textContent = 'RESTART AUDIO';
    $('start').classList.remove('hot');
    $('measure').disabled = false;
    $('noise').disabled = false;
    status(S.sim ? 'Simulated room (test mode): no speakers or mic are used.' : 'Audio running. Play TEST LEVEL and set your volume, then MEASURE.');
  } catch (e) {
    await stopAudio();
    status(e.name === 'NotAllowedError' ? 'Microphone access was refused. Allow it in the browser and try again.' : 'Could not start audio: ' + e.message, true);
  }
}

async function stopAudio() {
  if (S.pending) { S.pending.reject(new Error('Audio stopped')); S.pending = null; }
  if (S.node) { S.node.port.onmessage = null; S.node.disconnect(); S.node = null; }
  if (S.splitter) { S.splitter.disconnect(); S.splitter = null; }
  if (S.source) { S.source.disconnect(); S.source = null; }
  if (S.stream) { S.stream.getTracks().forEach((t) => t.stop()); S.stream = null; }
  if (S.ctx) { try { await S.ctx.close(); } catch (e) {} S.ctx = null; }
  setNoise(false);
  $('measure').disabled = true;
  $('noise').disabled = true;
}

async function listDevices() {
  if (S.sim) {
    $('mic').innerHTML = '<option>Simulated room</option>';
    $('out').innerHTML = '<option>Simulated room</option>';
    return;
  }
  const devs = await navigator.mediaDevices.enumerateDevices();
  const ins = devs.filter((d) => d.kind === 'audioinput' && d.deviceId !== 'communications');
  const outs = devs.filter((d) => d.kind === 'audiooutput' && d.deviceId !== 'communications');
  const track = S.stream && S.stream.getAudioTracks()[0];
  const current = track ? (track.getSettings().deviceId || '') : '';
  $('mic').innerHTML = ins.map((d, i) => `<option value="${esc(d.deviceId)}">${esc(d.label || 'Input ' + (i + 1))}</option>`).join('');
  $('mic').value = S.micId || current;
  $('mic').disabled = false;
  const canPick = !!(S.ctx && S.ctx.setSinkId);
  $('out').innerHTML = canPick
    ? outs.map((d, i) => `<option value="${esc(d.deviceId)}">${esc(d.label || 'Output ' + (i + 1))}</option>`).join('')
    : '<option>System output</option>';
  if (canPick) $('out').value = S.outId || 'default';
  $('out').disabled = !canPick;
  $('out').title = canPick ? '' : 'This browser plays to the system output (Chrome and Edge can choose)';
}

function fillMicChannels(n) {
  $('micChRow').hidden = n < 2;
  $('micCh').innerHTML = Array.from({ length: n }, (_, i) => `<option value="${i}">INPUT ${i + 1}</option>`).join('');
  $('micCh').value = String(S.micCh);
}

let holdDb = -120, holdUntil = 0;
function fromWorklet(m) {
  if (m.type === 'meter') {
    const db = 20 * Math.log10(Math.max(m.peak, 1e-9));
    const pct = Math.max(0, Math.min(100, (db + 60) / 60 * 100));
    $('meterBar').style.width = pct + '%';
    $('meterBar').classList.toggle('hot', db > -1);
    const now = performance.now();
    if (db > holdDb || now > holdUntil) { holdDb = db; holdUntil = now + 1500; }
    $('meterHold').style.left = `calc(${Math.max(0, Math.min(100, (holdDb + 60) / 60 * 100))}% - 1px)`;
    $('meterText').textContent = db < -100 ? '—' : db.toFixed(1) + ' dBFS';
  } else if (m.type === 'progress') {
    $('prog').style.width = (100 * m.done / m.length).toFixed(1) + '%';
  } else if (m.type === 'done') {
    $('prog').style.width = '100%';
    if (S.pending) { S.pending.resolve(m); S.pending = null; }
  } else if (m.type === 'aborted') {
    if (S.pending) { S.pending.reject(new Error('Stopped')); S.pending = null; }
  }
}

function setNoise(on) {
  S.noise = on;
  $('noise').classList.toggle('active', on);
  $('noise').textContent = on ? 'STOP NOISE' : 'TEST LEVEL';
  if (S.node) S.node.port.postMessage({ type: 'noise', on, left: S.chans !== 'R', right: S.chans !== 'L', level: S.level - 3 });
}

// ---------------------------------------------------------------------------------------------------------------------
// measuring

async function measure() {
  if (!S.node || S.busy) return;
  if (S.busy) return;
  setNoise(false);
  S.busy = true;
  $('measure').textContent = 'STOP';
  $('measure').classList.remove('hot');
  $('prog').style.width = '0%';
  const fs = S.fs, sides = S.chans === 'LR' ? ['L', 'R'] : [S.chans];
  try {
    const n = W.x.room_sweep(fs, 10, 0, S.seconds, S.level);
    const sweep = W.f32(W.x.room_sweep_ptr(), n).slice();
    const pre = Math.round(PRE_S * fs), seg = n + Math.round(TAIL_S * fs), total = pre + sides.length * seg;
    const out = [new Float32Array(total), new Float32Array(total)];
    sides.forEach((side, k) => out[side === 'L' ? 0 : 1].set(sweep, pre + k * seg));
    status(`Measuring ${sides.map((s) => (s === 'L' ? 'left' : 'right')).join(' then ')}… keep quiet and still.`);
    const result = await new Promise((resolve, reject) => {
      S.pending = { resolve, reject };
      S.node.port.postMessage({ type: 'run', out, length: total }, [out[0].buffer, out[1].buffer]);
    });
    const rec = result.rec;
    const noiseDb = rmsDb(rec.subarray(Math.round(0.05 * fs), pre - Math.round(0.05 * fs)));
    const name = $('pos').value.trim() || 'Position ' + (S.runs.length + 1);
    const run = { name, time: new Date().toISOString(), fs, seconds: S.seconds, level: S.level, noiseDb, ch: [], notes: [], use: true };
    if (result.dropped > 0) run.notes.push('The mic input dropped out during the run: measure again.');
    for (let k = 0; k < sides.length; ++k) {
      const part = rec.subarray(pre + k * seg, pre + (k + 1) * seg);
      const p = W.putF32(part);
      const id = W.x.room_analyze(p, part.length, Math.round(SEARCH_S * fs));
      W.x.room_release_buf(p);
      const side = sides[k];
      if (id < 0) { run.notes.push(`Nothing heard from the ${side === 'L' ? 'left' : 'right'} speaker.`); continue; }
      const c = channelFrom(id, side);
      if (c.signalDb - noiseDb < 10) {
        W.x.room_free(id);
        run.notes.push(`The ${side === 'L' ? 'left' : 'right'} sweep was not heard over the noise: check the output, the volume and the mic.`);
        continue;
      }
      run.ch.push(c);
    }
    if (run.ch.length === 0) {
      status(run.notes.join(' ') || 'Nothing was measured.', true);
    } else {
      finishRun(run);
      S.runs.push(run);
      const warn = runWarnings(run);
      status(warn.length ? warn.join(' ') : `Measured ${name}.`, warn.length > 0);
      $('pos').value = '';
    }
  } catch (e) {
    status(e.message === 'Stopped' ? 'Measurement stopped.' : 'Measurement failed: ' + e.message, e.message !== 'Stopped');
  } finally {
    S.busy = false;
    $('measure').textContent = 'MEASURE';
    $('measure').classList.add('hot');
    $('save').disabled = S.runs.length === 0;
    renderRuns();
    draw();
  }
}

function stopMeasure() {
  if (S.node) S.node.port.postMessage({ type: 'stop' });
}

function channelFrom(id, side) {
  const info = (k) => W.x.room_info(id, k);
  return {
    id, side, visible: true, colour: S.nextColour++,
    arrival: info(0), polarity: info(1), signalDb: info(3), peakDbFS: info(4), fs: info(6), cache: null,
    irStart: 0,   // where the response held in wasm starts in the run (a saved one starts a little before the direct sound)
  };
}

function finishRun(run) {
  run.ch.forEach((c) => { c.snr = c.signalDb - run.noiseDb; });
  if (run.ch.length === 2) {
    const [l, r] = run.ch;
    run.dtMs = (r.arrival - l.arrival) / run.fs * 1000;
    run.levelDiff = bandLevel(r, 500, 2000) - bandLevel(l, 500, 2000);
  }
}

function runWarnings(run) {
  const w = run.notes.slice();
  for (const c of run.ch) {
    const s = c.side === 'L' ? 'left' : 'right';
    if (c.peakDbFS > -1) w.push(`The ${s} sweep clipped at the mic: turn the volume or the sweep level down.`);
    else if (c.snr < 30) w.push(`The ${s} sweep is only ${c.snr.toFixed(0)} dB over the room noise: turn up or quieten the room.`);
  }
  return w;
}

const rmsDb = (x) => {
  let s = 0;
  for (let i = 0; i < x.length; ++i) s += x[i] * x[i];
  return 10 * Math.log10(Math.max(s / Math.max(1, x.length), 1e-30));
};

// ---------------------------------------------------------------------------------------------------------------------
// responses

const SMOOTH_OCT = (d) => (d > 0 ? 1 / d : 0);

function responseOf(c) {
  const key = `${S.windowMs}|${S.smooth}|${S.useCal && S.cal ? 1 : 0}|${S.cal ? S.cal.points + S.cal.name : ''}`;
  if (c.cache && c.cache.key === key) return c.cache.db;
  const p = W.x.room_response(c.id, S.windowMs, GRID.lo, GRID.hi, GRID.perOct, SMOOTH_OCT(S.smooth), S.useCal && S.cal ? 1 : 0);
  const db = p ? W.f32(p, W.x.room_grid_size()).slice() : new Float32Array(0);
  c.cache = { key, db };
  return db;
}

function gridFreqs() {
  const n = Math.ceil(Math.log2(GRID.hi / GRID.lo) * GRID.perOct);
  return Float64Array.from({ length: n + 1 }, (_, i) => Math.min(GRID.hi, GRID.lo * Math.pow(2, i / GRID.perOct)));
}
const FREQS = gridFreqs();

// average level between f1 and f2 (1/3-octave smoothed, unnormalised)
function bandLevel(c, f1, f2) {
  const p = W.x.room_response(c.id, S.windowMs, GRID.lo, GRID.hi, GRID.perOct, 1 / 3, S.useCal && S.cal ? 1 : 0);
  if (!p) return 0;
  const db = W.f32(p, W.x.room_grid_size());
  let s = 0, n = 0;
  for (let i = 0; i < FREQS.length; ++i) if (FREQS[i] >= f1 && FREQS[i] <= f2) { s += Math.pow(10, db[i] / 10); ++n; }
  return n ? 10 * Math.log10(s / n) : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// calibration files

async function loadCal(file) {
  if (file.size > 8 * 1024 * 1024) { status('That file is too large to be a calibration file.', true); return; }
  const text = await file.text();
  applyCal(file.name, text, true);
}

function applyCal(name, text, announce) {
  const [p, len] = W.putText(text);
  const r = W.x.room_cal(p, len);
  W.x.room_release_buf(p);
  if (r <= 0) {
    status(r === -2 ? `${name}: fewer than 3 usable points.` : `${name}: no frequency / dB pairs found. Is it a calibration file?`, true);
    return false;
  }
  const i = (k) => W.x.room_cal_info(k);
  S.cal = { name, text, points: i(0), lo: i(1), hi: i(2), sens: i(4) ? i(3) : null, skipped: i(5) };
  for (const run of S.runs) for (const c of run.ch) c.cache = null;
  S.runs.forEach(finishRun);
  showCal();
  if (announce) status(`Calibration loaded: ${S.cal.points} points from ${fmtHz(S.cal.lo)} to ${fmtHz(S.cal.hi)}.` +
    (S.cal.skipped ? ` ${S.cal.skipped} lines skipped.` : ''));
  renderRuns();
  draw();
  return true;
}

function clearCal() {
  W.x.room_cal_clear();
  S.cal = null;
  for (const run of S.runs) for (const c of run.ch) c.cache = null;
  S.runs.forEach(finishRun);
  showCal();
  renderRuns();
  draw();
  status('Calibration cleared: the mic is taken as flat.');
}

function showCal() {
  const c = S.cal;
  $('calClear').disabled = !c;
  if (!c) { $('calInfo').textContent = 'No file: the mic is taken as flat.'; return; }
  let range = 0;
  let lo = 1e9, hi = -1e9;
  for (let f = 20; f <= 20000; f *= 1.05) { const v = W.x.room_cal_at(f); lo = Math.min(lo, v); hi = Math.max(hi, v); }
  range = hi - lo;
  $('calInfo').innerHTML = `<b>${esc(c.name)}</b><br>${c.points} points, ${fmtHz(c.lo)}–${fmtHz(c.hi)}, ` +
    `corrects ${range.toFixed(1)} dB` + (c.sens != null ? `, sens ${c.sens.toFixed(2)} dB` : '');
}

// ---------------------------------------------------------------------------------------------------------------------
// drawing

const canvas = $('graph');
const g2 = canvas.getContext('2d');

function draw() {
  if (!theme.curves) return;
  const r = canvas.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  const w = Math.max(10, Math.round(r.width)), h = Math.max(10, Math.round(r.height));
  if (canvas.width !== w * dpr || canvas.height !== h * dpr) { canvas.width = w * dpr; canvas.height = h * dpr; }
  g2.setTransform(dpr, 0, 0, dpr, 0, 0);
  g2.clearRect(0, 0, w, h);
  const shown = [];
  for (const run of S.runs) for (const c of run.ch) if (c.visible) shown.push({ run, c });
  $('empty').style.display = S.runs.length ? 'none' : 'flex';
  $('empty').textContent = 'Start audio, set the level, then MEASURE.';
  if (S.view === 'response') drawResponse(w, h, shown);
  else if (S.view === 'impulse') drawImpulse(w, h, shown);
  else drawCorrection(w, h);
}

const font = (px, mono) => `${px}px ${mono ? '"JBM", monospace' : '"STM", monospace'}`;

function frame(w, h) {
  return { x0: 46, x1: w - 14, y0: 14, y1: h - 26 };
}

function drawResponse(w, h, shown) {
  const f = frame(w, h);
  const lx = (hz) => f.x0 + (f.x1 - f.x0) * Math.log(hz / GRID.lo) / Math.log(GRID.hi / GRID.lo);
  // curves and the dB range
  const curves = shown.map(({ run, c }) => ({ run, c, db: responseOf(c) }));
  let offset = 0;
  if (S.norm && curves.length) {
    // one offset for every curve (the first shown curve at 0 dB between 200 Hz and 2 kHz), so level differences stay
    const ref = curves[0].db;
    let s = 0, n = 0;
    for (let i = 0; i < FREQS.length; ++i) if (FREQS[i] >= 200 && FREQS[i] <= 2000) { s += Math.pow(10, ref[i] / 10); ++n; }
    offset = n ? -10 * Math.log10(s / n) : 0;
  }
  let top = -Infinity;
  for (const cv of curves) for (let i = 0; i < cv.db.length; ++i) if (FREQS[i] >= 25 && FREQS[i] <= 18000) top = Math.max(top, cv.db[i] + offset);
  if (!isFinite(top)) top = 10;
  const yTop = Math.ceil((top + 3) / 5) * 5, yBot = yTop - 50;
  const ly = (db) => f.y0 + (f.y1 - f.y0) * (yTop - db) / (yTop - yBot);

  // band the sweep can't measure
  g2.fillStyle = theme.grid;
  if (S.fs || S.runs.length) {
    const fs = S.runs.length ? S.runs[S.runs.length - 1].fs : S.fs;
    const hiValid = Math.min(0.45 * fs / 1.06, GRID.hi);
    if (hiValid < GRID.hi) g2.fillRect(lx(hiValid), f.y0, f.x1 - lx(hiValid), f.y1 - f.y0);
  }
  // grid
  g2.strokeStyle = theme.grid; g2.lineWidth = 1; g2.font = font(10, true); g2.fillStyle = css(theme.ink2);
  g2.textAlign = 'center'; g2.textBaseline = 'top';
  for (const hz of [20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 600, 800, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 20000]) {
    const x = Math.round(lx(hz)) + 0.5;
    g2.beginPath(); g2.moveTo(x, f.y0); g2.lineTo(x, f.y1); g2.stroke();
    if ([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000].includes(hz)) g2.fillText(fmtHz(hz, true), x, f.y1 + 6);
  }
  g2.textAlign = 'right'; g2.textBaseline = 'middle';
  for (let db = yBot; db <= yTop; db += 5) {
    const y = Math.round(ly(db)) + 0.5;
    g2.globalAlpha = db % 10 === 0 ? 1 : 0.5;
    g2.beginPath(); g2.moveTo(f.x0, y); g2.lineTo(f.x1, y); g2.stroke();
    g2.globalAlpha = 1;
    if (db % 10 === 0) g2.fillText(db + (db === yTop ? ' dB' : ''), f.x0 - 6, y);
  }
  // curves
  g2.save();
  g2.beginPath(); g2.rect(f.x0, f.y0, f.x1 - f.x0, f.y1 - f.y0); g2.clip();
  g2.lineJoin = 'round';
  for (const cv of curves) {
    g2.strokeStyle = colourOf(cv.c); g2.lineWidth = 1.75;
    g2.beginPath();
    let started = false;
    for (let i = 0; i < cv.db.length; ++i) {
      const x = lx(FREQS[i]), y = ly(cv.db[i] + offset);
      if (!started) { g2.moveTo(x, y); started = true; } else g2.lineTo(x, y);
    }
    g2.stroke();
  }
  g2.restore();
  g2.strokeStyle = css(theme.ink); g2.lineWidth = 1.5;
  g2.strokeRect(f.x0 + 0.75, f.y0 + 0.75, f.x1 - f.x0 - 1.5, f.y1 - f.y0 - 1.5);
  if (curves.length) {
    g2.font = font(11); g2.fillStyle = css(theme.ink2); g2.textAlign = 'left'; g2.textBaseline = 'top';
    const label = (S.smooth ? `1/${S.smooth} OCT` : 'NO SMOOTHING') + ` · ${S.windowMs} MS WINDOW` + (S.cal && S.useCal ? ' · MIC CAL' : '') + (S.norm ? ' · NORMALIZED' : '');
    g2.fillText(label, f.x0 + 8, f.y0 + 6);
  }
}

function drawImpulse(w, h, shown) {
  const f = frame(w, h);
  const tMin = -2, tMax = 60;   // ms after the earliest arrival in the run
  const lx = (ms) => f.x0 + (f.x1 - f.x0) * (ms - tMin) / (tMax - tMin);
  const ly = (db) => f.y0 + (f.y1 - f.y0) * (0 - db) / 60;
  g2.strokeStyle = theme.grid; g2.lineWidth = 1; g2.font = font(10, true); g2.fillStyle = css(theme.ink2);
  g2.textAlign = 'center'; g2.textBaseline = 'top';
  for (let ms = 0; ms <= tMax; ms += 5) {
    const x = Math.round(lx(ms)) + 0.5;
    g2.beginPath(); g2.moveTo(x, f.y0); g2.lineTo(x, f.y1); g2.stroke();
    if (ms % 10 === 0) g2.fillText(ms + (ms === tMax ? ' ms' : ''), x, f.y1 + 6);
  }
  g2.textAlign = 'right'; g2.textBaseline = 'middle';
  for (let db = 0; db >= -60; db -= 10) {
    const y = Math.round(ly(db)) + 0.5;
    g2.beginPath(); g2.moveTo(f.x0, y); g2.lineTo(f.x1, y); g2.stroke();
    g2.fillText(db + (db === 0 ? ' dB' : ''), f.x0 - 6, y);
  }
  g2.save();
  g2.beginPath(); g2.rect(f.x0, f.y0, f.x1 - f.x0, f.y1 - f.y0); g2.clip();
  for (const { run, c } of shown) {
    const n = W.x.room_info(c.id, 5), ir = W.f32(W.x.room_ir_ptr(c.id), n);
    const ref = Math.min(...run.ch.map((d) => d.arrival));   // positions in the run
    const peak = Math.max(1e-12, ...run.ch.map((d) => Math.pow(10, W.x.room_info(d.id, 2) / 20)));
    const fs = c.fs;
    const i0 = Math.floor(ref - c.irStart + tMin / 1000 * fs), i1 = Math.ceil(ref - c.irStart + tMax / 1000 * fs);
    // envelope per pixel column: the largest |sample| in it
    g2.strokeStyle = colourOf(c); g2.lineWidth = 1.25;
    g2.beginPath();
    const cols = Math.max(1, Math.round(f.x1 - f.x0));
    let started = false;
    for (let px = 0; px < cols; ++px) {
      const a = Math.floor(i0 + (i1 - i0) * px / cols), b = Math.max(a + 1, Math.floor(i0 + (i1 - i0) * (px + 1) / cols));
      let m = 0;
      for (let i = Math.max(0, a); i < b && i < n; ++i) m = Math.max(m, Math.abs(ir[i]));
      const db = Math.max(-80, 20 * Math.log10(m / peak + 1e-12));
      const x = f.x0 + px, y = ly(db);
      if (!started) { g2.moveTo(x, y); started = true; } else g2.lineTo(x, y);
    }
    g2.stroke();
    // the direct sound
    const ax = lx((c.arrival - ref) / fs * 1000);
    g2.setLineDash([3, 3]); g2.beginPath(); g2.moveTo(ax, f.y0); g2.lineTo(ax, f.y1); g2.stroke(); g2.setLineDash([]);
  }
  g2.restore();
  g2.strokeStyle = css(theme.ink); g2.lineWidth = 1.5;
  g2.strokeRect(f.x0 + 0.75, f.y0 + 0.75, f.x1 - f.x0 - 1.5, f.y1 - f.y0 - 1.5);
  if (shown.length) {
    g2.font = font(11); g2.fillStyle = css(theme.ink2); g2.textAlign = 'left'; g2.textBaseline = 'top';
    g2.fillText('ENERGY OVER TIME FROM THE FIRST ARRIVAL IN EACH RUN · DASHED: DIRECT SOUND', f.x0 + 8, f.y0 + 6);
  }
}

const colourOf = (c) => theme.curves[c.colour % theme.curves.length];

// ---------------------------------------------------------------------------------------------------------------------
// the list of measurements

function renderRuns() {
  const list = $('runList');
  if (!S.runs.length) { list.innerHTML = '<p class="none">None yet.</p>'; return; }
  list.innerHTML = '';
  S.runs.forEach((run, ri) => {
    const el = document.createElement('div');
    el.className = 'run';
    const when = new Date(run.time);
    el.innerHTML = `<div class="run-head"><span class="run-name">${esc(run.name)}</span>
      <span class="run-time">${when.toLocaleString([], { dateStyle: 'short', timeStyle: 'short' })} · ${(run.fs / 1000).toFixed(1)} kHz · ${run.seconds} s sweeps · noise ${run.noiseDb.toFixed(0)} dBFS</span>
      <span class="spacer"></span><button class="key use toggle ${run.use ? 'on' : ''}" data-act="use" title="Average this position into the correction">IN CORRECTION</button><button class="key" data-act="del">DELETE</button></div>`;
    for (const c of run.ch) {
      const row = document.createElement('div');
      row.className = 'chan';
      const clip = c.peakDbFS > -1, low = c.snr < 30;
      row.innerHTML = `<button class="sw ${c.visible ? '' : 'off'}" style="background:${colourOf(c)}" title="Show or hide"></button>
        <b>${c.side === 'L' ? 'LEFT' : 'RIGHT'}</b>
        <span><span class="lab">ARRIVAL </span><b class="num">${(c.arrival / c.fs * 1000).toFixed(2)} ms</b></span>
        <span class="${low ? 'warn' : ''}"><span class="lab">SNR </span><b class="num">${c.snr.toFixed(0)} dB</b></span>
        <span class="wide-only ${clip ? 'warn' : ''}"><span class="lab">PEAK </span><b class="num">${c.peakDbFS.toFixed(1)} dBFS</b></span>
        <span class="wide-only"><span class="lab">POLARITY </span><b class="num">${c.polarity > 0 ? '+' : '−'}</b></span>
        <span class="wide-only"></span>
        <span><button class="key" data-act="txt">TXT</button> <button class="key" data-act="wav">WAV</button></span>`;
      row.querySelector('.sw').onclick = () => { c.visible = !c.visible; renderRuns(); draw(); };
      row.querySelector('[data-act=txt]').onclick = () => exportTxt(run, c);
      row.querySelector('[data-act=wav]').onclick = () => exportWav(run, c);
      el.appendChild(row);
    }
    const v = verdict(run);
    if (v) { const d = document.createElement('div'); d.className = 'verdict'; d.innerHTML = v; el.appendChild(d); }
    el.querySelector('[data-act=use]').onclick = () => { run.use = !run.use; renderRuns(); draw(); };
    el.querySelector('[data-act=del]').onclick = () => {
      run.ch.forEach((c) => W.x.room_free(c.id));
      S.runs.splice(ri, 1);
      $('save').disabled = S.runs.length === 0;
      renderRuns(); draw();
    };
    list.appendChild(el);
  });
}

function verdict(run) {
  if (run.ch.length !== 2) return run.notes.length ? `<span class="warn">${esc(run.notes.join(' '))}</span>` : '';
  const parts = [];
  const dt = run.dtMs, cm = Math.abs(dt) / 1000 * SPEED * 100;
  if (Math.abs(dt) < 0.02) parts.push('TIMING: <b>both arrive together</b>');
  else {
    const later = dt > 0 ? 'right' : 'left', earlier = dt > 0 ? 'left' : 'right';
    parts.push(`TIMING: <b>${later} ${Math.abs(dt).toFixed(2)} ms later (${cm.toFixed(1)} cm further)</b> → delay ${earlier} by ${Math.abs(dt).toFixed(2)} ms`);
  }
  const ld = run.levelDiff;
  parts.push(Math.abs(ld) < 0.25 ? 'LEVEL: <b>matched</b>' : `LEVEL: <b>right ${Math.abs(ld).toFixed(1)} dB ${ld > 0 ? 'louder' : 'quieter'}</b> (500 Hz–2 kHz)`);
  if (run.ch[0].polarity !== run.ch[1].polarity) parts.push('<span class="warn">POLARITY: the speakers disagree (one may be wired backwards)</span>');
  if (run.notes.length) parts.push(`<span class="warn">${esc(run.notes.join(' '))}</span>`);
  return parts.map((p) => `<span>${p}</span>`).join('');
}

// ---------------------------------------------------------------------------------------------------------------------
// saving, opening, exporting

const IR_BEFORE_S = 0.02, IR_AFTER_S = 1.0;

function irSegment(c) {
  const n = W.x.room_info(c.id, 5), ir = W.f32(W.x.room_ir_ptr(c.id), n);
  const at = c.arrival - c.irStart;
  const start = Math.max(0, Math.floor(at - IR_BEFORE_S * c.fs));
  const end = Math.min(n, Math.ceil(at + IR_AFTER_S * c.fs));
  return { start: c.irStart + start, data: ir.slice(start, end) };
}

function b64(f32) {
  const u = new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength);
  let s = '';
  for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000));
  return btoa(s);
}
const unb64 = (s) => { const u = Uint8Array.from(atob(s), (ch) => ch.charCodeAt(0)); return new Float32Array(u.buffer); };

function saveSession() {
  const doc = {
    format: 'pump-room-session', version: 1, saved: new Date().toISOString(),
    cal: S.cal ? { name: S.cal.name, text: S.cal.text } : null,
    correction: S.corr,
    runs: S.runs.map((run) => ({
      name: run.name, time: run.time, fs: run.fs, seconds: run.seconds, level: run.level, noiseDb: run.noiseDb, notes: run.notes, use: run.use,
      ch: run.ch.map((c) => {
        const seg = irSegment(c);
        return { side: c.side, arrival: c.arrival, polarity: c.polarity, signalDb: c.signalDb, peakDbFS: c.peakDbFS,
                 irStart: seg.start, ir: b64(seg.data) };
      }),
    })),
  };
  const stamp = new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-');
  download(`pump-room-${stamp}.json`, JSON.stringify(doc), 'application/json');
  status(`Saved ${S.runs.length} measurement${S.runs.length === 1 ? '' : 's'}.`);
}

async function openSession(file) {
  try {
    if (file.size > 200 * 1024 * 1024) throw new Error('file too large');
    const doc = JSON.parse(await file.text());
    if (!doc || doc.format !== 'pump-room-session' || !Array.isArray(doc.runs)) throw new Error('not a pump ROOM session');
    if (doc.cal && typeof doc.cal.text === 'string') applyCal(String(doc.cal.name || 'calibration'), doc.cal.text, false);
    if (doc.correction && typeof doc.correction === 'object') {
      for (const k of Object.keys(S.corr)) if (typeof doc.correction[k] === typeof S.corr[k]) S.corr[k] = doc.correction[k];
      showCorrControls();
    }
    let added = 0;
    for (const r of doc.runs) {
      const fs = +r.fs;
      if (!(fs >= 8000 && fs <= 768000) || !Array.isArray(r.ch)) continue;
      const run = { name: String(r.name || 'Position'), time: String(r.time || new Date().toISOString()), fs,
                    seconds: +r.seconds || 0, level: +r.level || 0, noiseDb: +r.noiseDb || -120, ch: [],
                    notes: Array.isArray(r.notes) ? r.notes.map(String) : [], use: r.use !== false };
      for (const ch of r.ch) {
        if (typeof ch.ir !== 'string') continue;
        const data = unb64(ch.ir);
        const p = W.putF32(data);
        const id = W.x.room_load(p, data.length, fs, (+ch.arrival) - (+ch.irStart));
        W.x.room_release_buf(p);
        if (id < 0) continue;
        const c = channelFrom(id, ch.side === 'R' ? 'R' : 'L');
        // positions in the run (the IR was saved from irStart on)
        c.irStart = +ch.irStart || 0;
        c.arrival = (+ch.arrival);
        c.signalDb = +ch.signalDb; c.peakDbFS = +ch.peakDbFS; c.polarity = ch.polarity < 0 ? -1 : 1;
        run.ch.push(c);
      }
      if (run.ch.length) { finishRun(run); S.runs.push(run); ++added; }
    }
    $('save').disabled = S.runs.length === 0;
    renderRuns(); draw();
    status(`Opened ${file.name}: ${added} measurement${added === 1 ? '' : 's'}.`);
  } catch (e) {
    status(`Could not open ${file.name}: ${e.message}.`, true);
  }
}

function exportTxt(run, c) {
  const db = responseOf(c);
  const lines = [`* pump ROOM response: ${run.name}, ${c.side === 'L' ? 'left' : 'right'}`,
    `* ${new Date(run.time).toISOString()}, ${S.smooth ? '1/' + S.smooth + ' octave smoothing' : 'no smoothing'}, ${S.windowMs} ms window, ` +
    (S.cal && S.useCal ? `mic calibration ${S.cal.name}` : 'no mic calibration'),
    '* Freq(Hz) SPL(dB) Phase(degrees)'];
  for (let i = 0; i < FREQS.length; i += 2) lines.push(`${FREQS[i].toFixed(3)} ${db[i].toFixed(3)} 0`);
  download(`${fileName(run.name)}-${c.side}.txt`, lines.join('\n') + '\n', 'text/plain');
}

function exportWav(run, c) {
  const { data } = irSegment(c);
  const fs = Math.round(c.fs), n = data.length;
  const buf = new ArrayBuffer(44 + n * 4), v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; ++i) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); v.setUint32(4, 36 + n * 4, true); str(8, 'WAVE'); str(12, 'fmt ');
  v.setUint32(16, 16, true); v.setUint16(20, 3, true); v.setUint16(22, 1, true); v.setUint32(24, fs, true);
  v.setUint32(28, fs * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 32, true); str(36, 'data'); v.setUint32(40, n * 4, true);
  new Float32Array(buf, 44).set(data);
  download(`${fileName(run.name)}-${c.side}-impulse.wav`, buf, 'audio/wav');
}

function download(name, content, type) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const a = document.createElement('a');
  a.href = url; a.download = name;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

const fileName = (s) => s.replace(/[^\w\- ]+/g, '').trim().replace(/\s+/g, '-') || 'measurement';
const esc = (s) => String(s).replace(/[&<>"']/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
function fmtHz(hz, short) {
  if (hz >= 1000) { const k = hz / 1000; return (Number.isInteger(k) ? k : k.toFixed(1)) + (short ? 'k' : ' kHz'); }
  return Math.round(hz) + (short ? '' : ' Hz');
}


// ---------------------------------------------------------------------------------------------------------------------
// correction (stage 2): the measurements ticked IN CORRECTION, averaged per speaker, against a target

const CORR_GRID = { f: null };   // the correction grid's frequencies (filled once the wasm says how many points)

function corrFreqs() {
  if (!CORR_GRID.f) {
    const n = W.x.room_corr_grid_size(), lo = 20, per = 48;
    CORR_GRID.f = Float64Array.from({ length: n }, (_, i) => Math.min(20000, lo * Math.pow(2, i / per)));
  }
  return CORR_GRID.f;
}

// the arrival difference over the runs that have both speakers (ms, right minus left)
function meanDt(runs) {
  const both = runs.filter((r) => r.ch.length === 2 && isFinite(r.dtMs));
  return both.length ? both.reduce((a, r) => a + r.dtMs, 0) / both.length : 0;
}

function designCorrection() {
  const runs = S.runs.filter((r) => r.use);
  W.x.room_corr_begin();
  let added = 0;
  for (const run of runs) for (const c of run.ch) added += W.x.room_corr_add(c.side === 'L' ? 0 : 1, c.id, S.windowMs, S.useCal && S.cal ? 1 : 0) ? 1 : 0;
  if (!added) { S.design = null; return null; }
  const c = S.corr, dt = c.align ? meanDt(runs) : 0;
  const params = [c.tilt, c.bass, c.treble, c.fLo, c.fHi, c.boost, c.cut, c.nulls ? 1 : 0, c.low ? 1 : 0, c.amount / 100,
                  dt > 0 ? dt : 0, dt < 0 ? -dt : 0];
  const p = W.x.room_alloc(params.length * 8);
  W.f64(p, params.length).set(params);
  const n = W.x.room_corr_design(p);
  W.x.room_release_buf(p);
  const d = { n, preamp: W.x.room_corr_info(0, 0), sides: [], positions: runs.length };
  for (let s = 0; s < 2; ++s) {
    const get = (k) => { const q = W.x.room_corr_curve(s, k); return q ? W.f64(q, n).slice() : null; };
    const m = get(0);
    d.sides.push(m ? { measured: m, target: get(1), correction: get(2), result: get(3), guard: W.x.room_corr_info(s, 1), delay: W.x.room_corr_info(s, 2) } : null);
  }
  S.design = d;
  return d;
}

function drawCorrection(w, h) {
  const d = designCorrection();
  showCorrInfo(d);
  if (!d) {
    $('empty').style.display = 'flex';
    $('empty').textContent = S.runs.length ? 'Tick IN CORRECTION on at least one measurement.' : 'Measure first: the correction is built from your measurements.';
    return;
  }
  $('empty').style.display = 'none';
  const F = corrFreqs();
  const split = Math.round(h * 0.66);
  const top = { x0: 46, x1: w - 14, y0: 14, y1: split - 10 }, bot = { x0: 46, x1: w - 14, y0: split + 8, y1: h - 26 };
  const lx = (hz) => top.x0 + (top.x1 - top.x0) * Math.log(hz / 20) / Math.log(1000);
  // levels relative to the target's 200 Hz..2 kHz level
  const ref = d.sides.find((x) => x);
  let off = 0, k = 0;
  for (let i = 0; i < F.length; ++i) if (F[i] >= 200 && F[i] <= 2000) { off += ref.target[i]; ++k; }
  off = -off / k;
  let hiDb = -1e9;
  for (const sd of d.sides) if (sd) for (let i = 0; i < F.length; ++i) if (F[i] > 25 && F[i] < 18000) hiDb = Math.max(hiDb, sd.measured[i] + off, sd.target[i] + off);
  const yTop = Math.ceil((hiDb + 3) / 5) * 5, yBot = yTop - 40;
  const ly = (db) => top.y0 + (top.y1 - top.y0) * (yTop - db) / (yTop - yBot);
  const cMax = Math.max(6, Math.ceil(Math.max(S.corr.boost, S.corr.cut) / 3) * 3);
  const cy = (db) => bot.y0 + (bot.y1 - bot.y0) * (cMax - db) / (2 * cMax);
  const colours = [css(theme.ink), css(theme.second)];

  // grid
  g2.strokeStyle = theme.grid; g2.lineWidth = 1; g2.font = font(10, true); g2.fillStyle = css(theme.ink2);
  for (const hz of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
    const x = Math.round(lx(hz)) + 0.5;
    for (const fr of [top, bot]) { g2.beginPath(); g2.moveTo(x, fr.y0); g2.lineTo(x, fr.y1); g2.stroke(); }
    g2.textAlign = 'center'; g2.textBaseline = 'top'; g2.fillText(fmtHz(hz, true), x, bot.y1 + 6);
  }
  g2.textAlign = 'right'; g2.textBaseline = 'middle';
  for (let db = yBot; db <= yTop; db += 10) { const y = Math.round(ly(db)) + 0.5; g2.beginPath(); g2.moveTo(top.x0, y); g2.lineTo(top.x1, y); g2.stroke(); g2.fillText(db + (db === yTop ? ' dB' : ''), top.x0 - 6, y); }
  for (let db = -cMax; db <= cMax; db += cMax / 2) { const y = Math.round(cy(db)) + 0.5; g2.globalAlpha = db === 0 ? 1 : 0.6; g2.beginPath(); g2.moveTo(bot.x0, y); g2.lineTo(bot.x1, y); g2.stroke(); g2.globalAlpha = 1; g2.fillText((db > 0 ? '+' : '') + db, bot.x0 - 6, y); }
  // the band corrected
  g2.fillStyle = theme.grid;
  if (S.corr.fLo > 20) for (const fr of [top, bot]) g2.fillRect(fr.x0, fr.y0, lx(S.corr.fLo) - fr.x0, fr.y1 - fr.y0);
  if (S.corr.fHi < 20000) for (const fr of [top, bot]) g2.fillRect(lx(S.corr.fHi), fr.y0, fr.x1 - lx(S.corr.fHi), fr.y1 - fr.y0);

  const line = (fr, arr, y, colour, width, dash, alpha) => {
    g2.save(); g2.beginPath(); g2.rect(fr.x0, fr.y0, fr.x1 - fr.x0, fr.y1 - fr.y0); g2.clip();
    g2.strokeStyle = colour; g2.lineWidth = width; g2.setLineDash(dash || []); g2.globalAlpha = alpha || 1; g2.lineJoin = 'round';
    g2.beginPath();
    for (let i = 0; i < F.length; ++i) { const X = lx(F[i]), Y = y(arr[i]); if (i) g2.lineTo(X, Y); else g2.moveTo(X, Y); }
    g2.stroke(); g2.restore();
  };
  d.sides.forEach((sd, s) => {
    if (!sd) return;
    const shift = (a) => a.map((v) => v + off);
    line(top, shift(sd.measured), ly, colours[s], 1.25, null, 0.35);
    line(top, shift(sd.result), ly, colours[s], 2);
    line(bot, sd.correction, cy, colours[s], 1.75);
    if (sd.guard > 0) {
      const x = lx(sd.guard);
      g2.strokeStyle = colours[s]; g2.setLineDash([2, 3]); g2.beginPath(); g2.moveTo(x, bot.y0); g2.lineTo(x, bot.y1); g2.stroke(); g2.setLineDash([]);
    }
  });
  const tgt = d.sides.find((x) => x);
  line(top, tgt.target.map((v) => v + off), ly, css(theme.ink2), 1.5, [6, 4]);
  g2.strokeStyle = css(theme.ink); g2.lineWidth = 1.5;
  for (const fr of [top, bot]) g2.strokeRect(fr.x0 + 0.75, fr.y0 + 0.75, fr.x1 - fr.x0 - 1.5, fr.y1 - fr.y0 - 1.5);
  g2.font = font(11); g2.fillStyle = css(theme.ink2); g2.textAlign = 'left'; g2.textBaseline = 'top';
  g2.fillText(`AVERAGE OF ${d.positions} POSITION${d.positions === 1 ? '' : 'S'} (FAINT) · CORRECTED (BOLD) · TARGET (DASHED)`, top.x0 + 8, top.y0 + 6);
  g2.fillText('CORRECTION', bot.x0 + 8, bot.y0 + 6);
}

function showCorrInfo(d) {
  const el = $('cInfo');
  $('cSave').disabled = $('cWin').disabled = $('cPi').disabled = !d;
  if (!d) { el.innerHTML = ''; return; }
  const parts = [`PREAMP <b>${d.preamp.toFixed(1)} dB</b>`];
  d.sides.forEach((sd, s) => {
    if (!sd) return;
    const name = s ? 'RIGHT' : 'LEFT';
    let lo = 1e9, hi = -1e9;
    for (const v of sd.correction) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
    parts.push(`${name}: <b>${lo.toFixed(1)} / +${Math.max(0, hi).toFixed(1)} dB</b>` + (sd.delay > 0 ? `, delayed <b>${sd.delay.toFixed(2)} ms</b>` : '') +
      (sd.guard > 0 ? `, no boost under <b>${fmtHz(sd.guard)}</b>` : ''));
  });
  if (!d.sides[0] || !d.sides[1]) parts.push('<span class="warn">Only one speaker measured.</span>');
  el.innerHTML = parts.join('<br>');
}

function showCorrControls() {
  const c = S.corr;
  const set = (id, v, text) => { $(id).value = v; $(id + 'T').textContent = text; };
  set('cTilt', c.tilt, (c.tilt > 0 ? '+' : '') + c.tilt.toFixed(1) + ' dB/oct');
  set('cBass', c.bass, '+' + c.bass.toFixed(1) + ' dB');
  set('cTreble', c.treble, (c.treble > 0 ? '+' : '') + c.treble.toFixed(1) + ' dB');
  set('cAmount', c.amount, c.amount + ' %');
  set('cBoost', c.boost, '+' + c.boost.toFixed(1) + ' dB');
  set('cCut', c.cut, '-' + c.cut + ' dB');
  for (const [id, v] of [['cLo', c.fLo], ['cHi', c.fHi]]) $(id).querySelectorAll('button').forEach((b) => b.classList.toggle('on', +b.dataset.v === v));
  for (const [id, k] of [['cNulls', 'nulls'], ['cLow', 'low'], ['cAlign', 'align']]) $(id).classList.toggle('on', c[k]);
}

function initCorrection() {
  const slider = (id, key) => { $(id).oninput = () => { S.corr[key] = +$(id).value; showCorrControls(); draw(); }; };
  slider('cTilt', 'tilt'); slider('cBass', 'bass'); slider('cTreble', 'treble'); slider('cAmount', 'amount'); slider('cBoost', 'boost'); slider('cCut', 'cut');
  seg('cLo', (v) => { S.corr.fLo = +v; draw(); });
  seg('cHi', (v) => { S.corr.fHi = +v; draw(); });
  for (const [id, k] of [['cNulls', 'nulls'], ['cLow', 'low'], ['cAlign', 'align']]) $(id).onclick = () => { S.corr[k] = !S.corr[k]; showCorrControls(); draw(); };
  $('cSave').onclick = saveProfile;
  $('cWin').onclick = exportWindows;
  $('cPi').onclick = exportPi;
  showCorrControls();
}

function profileName() { return ($('cName').value || 'My room').trim().slice(0, 60) || 'My room'; }

function saveProfile() {
  if (!designCorrection()) return;
  const [p, len] = W.putText(profileName());
  const n = W.x.room_corr_profile(p, len);
  W.x.room_release_buf(p);
  const text = new TextDecoder().decode(new Uint8Array(W.x.memory.buffer, W.x.room_text_ptr(), n));
  download(`${fileName(profileName())}.pumproom`, text, 'text/plain');
  status('Profile saved: load it in pump ROOM (the plugin) or in System EQ.');
}

// filters at the usual rates (minimum phase: no latency; preamp and delays inside), a config file that picks the one
// for the device's rate, and a read-me, zipped
function exportWindows() {
  const d = designCorrection();
  if (!d) return;
  const files = [], rates = [44100, 48000, 88200, 96000, 192000];
  for (const fs of rates) {
    const ch = [0, 1].map((s) => {
      const side = d.sides[s] ? s : (s ? 0 : 1);   // one speaker measured: its filter on both
      const n = W.x.room_corr_fir(side, fs, 0);
      return W.f32(W.x.room_fir_ptr(), n).slice();
    });
    files.push({ name: `pump-room-${fs}.wav`, data: wav(ch, fs) });
  }
  const name = profileName();
  const conf = [`# pump ROOM correction: ${name} (${new Date().toISOString().slice(0, 10)})`,
    `# Minimum phase (no latency). The preamp (${d.preamp.toFixed(2)} dB) and the time alignment are inside the filters.`,
    'Channel: L R',
    ...rates.map((fs, i) => `${i ? 'ElseIf' : 'If'}: sampleRate == ${fs}\nConvolution: pump-room-${fs}.wav`),
    'EndIf:', ''].join('\r\n');
  files.push({ name: 'pump-room.txt', data: new TextEncoder().encode(conf) });
  const readme = [`pump ROOM correction "${name}" for Windows`, '',
    'For the free system-wide equalizer for Windows that reads its settings from config.txt in its config folder:', '',
    '1. Copy pump-room.txt and the pump-room-*.wav files into that config folder.',
    '2. Add this line to config.txt (or add pump-room.txt in its editor):',
    '     Include: pump-room.txt',
    '3. Play something. Remove the line again to turn the correction off.', '',
    'The filter that matches your output\'s sample rate is used (44.1, 48, 88.2, 96 or 192 kHz).',
    `Preamp ${d.preamp.toFixed(2)} dB (headroom for the boosts) and time alignment are already inside the filters.`, ''].join('\r\n');
  files.push({ name: 'README.txt', data: new TextEncoder().encode(readme) });
  download(`${fileName(name)}-windows.zip`, zip(files), 'application/zip');
  status('Exported for Windows: unzip, then follow README.txt.');
}

// A Raspberry Pi (or any Linux box) running CamillaDSP between the sources and the speakers: one mono filter per
// speaker and rate (minimum phase; preamp and time alignment inside), a CamillaDSP config that picks the file for
// its rate ($samplerate$ in the file name), an install script and a read-me, zipped.
function exportPi() {
  const d = designCorrection();
  if (!d) return;
  const name = profileName(), files = [], rates = [44100, 48000, 88200, 96000, 192000];
  for (const fs of rates)
    for (const s of [0, 1]) {
      const side = d.sides[s] ? s : (s ? 0 : 1);
      const n = W.x.room_corr_fir(side, fs, 0);
      files.push({ name: `coeffs/pump-room-${s ? 'R' : 'L'}-${fs}.wav`, data: wav([W.f32(W.x.room_fir_ptr(), n).slice()], fs) });
    }
  const yml = [
    `# pump ROOM correction for CamillaDSP: ${name} (${new Date().toISOString().slice(0, 10)})`,
    `# Minimum phase. The preamp (${d.preamp.toFixed(2)} dB) and the time alignment are inside the filters.`,
    '# devices: set these to your audio interface (README.txt). As written: a USB interface that shows up as',
    '# "CODEC" (the common 16-bit USB line in/out boxes), its line input in and its line output out, at 48 kHz.',
    'devices:',
    '  samplerate: 48000',
    '  chunksize: 1024',
    '  capture:',
    '    type: Alsa',
    '    channels: 2',
    '    device: "hw:CODEC,0"',
    '    format: S16_LE',
    '  playback:',
    '    type: Alsa',
    '    channels: 2',
    '    device: "hw:CODEC,0"',
    '    format: S16_LE',
    '',
    'filters:',
    ...['L', 'R'].flatMap((c) => [
      `  room_${c === 'L' ? 'left' : 'right'}:`,
      '    type: Conv',
      '    parameters:',
      '      type: Wav',
      `      filename: /etc/pump-room/coeffs/pump-room-${c}-$samplerate$.wav`,
    ]),
    '',
    'pipeline:',
    '  - type: Filter',
    '    channels: [0]',
    '    names: [room_left]',
    '  - type: Filter',
    '    channels: [1]',
    '    names: [room_right]',
    ''].join('\n');
  files.push({ name: 'pump-room.yml', data: new TextEncoder().encode(yml) });
  const install = [
    '#!/bin/sh',
    '# pump ROOM: puts this correction on a Raspberry Pi (Raspberry Pi OS Lite, 64-bit) with CamillaDSP.',
    '# Run from this folder:  sudo sh install.sh',
    '# Installs the filters and config in /etc/pump-room, CamillaDSP in /usr/local/bin (if it isn\'t there yet,',
    '# from its GitHub releases) and a service that runs it at boot. Running it again replaces the correction.',
    'set -e',
    '[ "$(id -u)" = 0 ] || { echo "run it with sudo"; exit 1; }',
    'mkdir -p /etc/pump-room/coeffs',
    'cp coeffs/*.wav /etc/pump-room/coeffs/',
    '# keep device settings already made in an earlier install',
    'if [ -f /etc/pump-room/pump-room.yml ] && grep -q "^devices:" /etc/pump-room/pump-room.yml; then',
    '  awk \'/^devices:/{on=1} /^filters:/{on=0} on\' /etc/pump-room/pump-room.yml > /tmp/pump-room-devices.yml',
    '  awk \'/^filters:/{on=1} on\' pump-room.yml > /tmp/pump-room-rest.yml',
    '  { grep "^#" pump-room.yml; cat /tmp/pump-room-devices.yml; cat /tmp/pump-room-rest.yml; } > /etc/pump-room/pump-room.yml',
    'else',
    '  cp pump-room.yml /etc/pump-room/pump-room.yml',
    'fi',
    'if ! command -v camilladsp >/dev/null 2>&1; then',
    '  case "$(uname -m)" in',
    '    aarch64) a=aarch64 ;;',
    '    armv7l|armv6l) a=armv7 ;;',
    '    x86_64) a=amd64 ;;',
    '    *) echo "unknown machine $(uname -m): install CamillaDSP by hand"; exit 1 ;;',
    '  esac',
    '  t=$(mktemp -d)',
    '  curl -fsSL -o "$t/c.tar.gz" "https://github.com/HEnquist/camilladsp/releases/latest/download/camilladsp-linux-$a.tar.gz"',
    '  tar -xzf "$t/c.tar.gz" -C "$t"',
    '  install -m 755 "$t/camilladsp" /usr/local/bin/camilladsp',
    '  rm -rf "$t"',
    'fi',
    'cat > /etc/systemd/system/pump-room.service <<EOF',
    '[Unit]',
    'Description=pump ROOM room correction (CamillaDSP)',
    'After=sound.target',
    '',
    '[Service]',
    'ExecStart=/usr/local/bin/camilladsp /etc/pump-room/pump-room.yml',
    'Restart=always',
    'RestartSec=3',
    '',
    '[Install]',
    'WantedBy=multi-user.target',
    'EOF',
    'systemctl daemon-reload',
    'systemctl enable pump-room.service',
    'systemctl restart pump-room.service',
    'echo "pump ROOM is running. Your audio devices:"',
    'arecord -l || true',
    'echo "If the devices in /etc/pump-room/pump-room.yml are not yours, edit them and: sudo systemctl restart pump-room"',
    ''].join('\n');
  files.push({ name: 'install.sh', data: new TextEncoder().encode(install) });
  const readme = [`pump ROOM correction "${name}" for a Raspberry Pi`, '',
    'The Pi sits between your sources and your speakers (or amp): sources -> audio interface line in -> Pi',
    '(CamillaDSP applies the correction) -> audio interface line out -> speakers.', '',
    'Set up (once per box):',
    '1. Put Raspberry Pi OS Lite (64-bit) on a microSD card with Raspberry Pi Imager; in its settings turn on SSH',
    '   and Wi-Fi so you can reach the Pi from your computer.',
    '2. Plug the audio interface into the Pi\'s USB, boot, and copy this folder over (for example: scp -r).',
    '3. On the Pi, in this folder:  sudo sh install.sh',
    '4. If your interface isn\'t one that shows up as "CODEC", edit the devices part of /etc/pump-room/pump-room.yml:',
    '   the card name from "arecord -l" (device: "hw:NAME,0") and the format it takes (S16_LE, S24_3_LE, S32_LE).',
    '   Then: sudo systemctl restart pump-room', '',
    'New correction for the same room: copy the new export over and run install.sh again (your device settings stay).',
    'Status and errors: systemctl status pump-room   /   journalctl -u pump-room', '',
    `The filters include the preamp (${d.preamp.toFixed(2)} dB, headroom for the boosts) and the time alignment.`,
    'They exist for 44.1, 48, 88.2, 96 and 192 kHz; the config runs at 48 kHz (change samplerate to use another).',
    'Latency: about the chunk size (1024 samples, 21 ms at 48 kHz) plus the interface\'s own. Lower chunksize for',
    'less (the Pi has to keep up: if you hear dropouts, raise it again).', ''].join('\n');
  files.push({ name: 'README.txt', data: new TextEncoder().encode(readme) });
  download(`${fileName(name)}-pi.zip`, zip(files), 'application/zip');
  status('Exported for a Pi: unzip, then follow README.txt.');
}

function wav(channels, fs) {
  const n = channels[0].length, nc = channels.length;
  const buf = new ArrayBuffer(44 + n * nc * 4), v = new DataView(buf);
  const str = (o, t) => { for (let i = 0; i < t.length; ++i) v.setUint8(o + i, t.charCodeAt(i)); };
  str(0, 'RIFF'); v.setUint32(4, 36 + n * nc * 4, true); str(8, 'WAVE'); str(12, 'fmt ');
  v.setUint32(16, 16, true); v.setUint16(20, 3, true); v.setUint16(22, nc, true); v.setUint32(24, fs, true);
  v.setUint32(28, fs * nc * 4, true); v.setUint16(32, nc * 4, true); v.setUint16(34, 32, true); str(36, 'data'); v.setUint32(40, n * nc * 4, true);
  for (let i = 0; i < n; ++i) for (let c = 0; c < nc; ++c) v.setFloat32(44 + (i * nc + c) * 4, channels[c][i] || 0, true);
  return new Uint8Array(buf);
}

// a zip archive with the files stored (no compression)
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; ++n) { let c = n; for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(u) { let c = 0xffffffff; for (let i = 0; i < u.length; ++i) c = CRC[(c ^ u[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function zip(files) {
  const parts = [], central = [];
  let offset = 0;
  const now = new Date();
  const dosTime = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
  const dosDate = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
  for (const f of files) {
    const name = new TextEncoder().encode(f.name), crc = crc32(f.data), size = f.data.length;
    const h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(8, 0, true);
    h.setUint16(10, dosTime, true); h.setUint16(12, dosDate, true); h.setUint32(14, crc, true);
    h.setUint32(18, size, true); h.setUint32(22, size, true); h.setUint16(26, name.length, true); h.setUint16(28, 0, true);
    parts.push(new Uint8Array(h.buffer), name, f.data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true);
    c.setUint16(10, 0, true); c.setUint16(12, dosTime, true); c.setUint16(14, dosDate, true); c.setUint32(16, crc, true);
    c.setUint32(20, size, true); c.setUint32(24, size, true); c.setUint16(28, name.length, true);
    c.setUint32(42, offset, true);
    central.push(new Uint8Array(c.buffer), name);
    offset += 30 + name.length + size;
  }
  const cSize = central.reduce((a, u) => a + u.length, 0);
  const e = new DataView(new ArrayBuffer(22));
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
  e.setUint32(12, cSize, true); e.setUint32(16, offset, true);
  return new Blob([...parts, ...central, new Uint8Array(e.buffer)]);
}

// ---------------------------------------------------------------------------------------------------------------------
// wiring

function seg(id, onPick) {
  const el = $(id);
  el.addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b || !el.contains(b)) return;
    el.querySelectorAll('button').forEach((x) => x.classList.toggle('on', x === b));
    onPick(b.dataset.v);
  });
}

function init() {
  const saved = recall('theme');
  const dark = saved ? saved === 'dark' : !!(window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches);
  buildTheme(dark);
  $('theme').onclick = () => { buildTheme(!theme.dark); store('theme', theme.dark ? 'dark' : 'light'); };
  $('start').onclick = startAudio;
  $('mic').onchange = () => { S.micId = $('mic').value; S.micCh = 0; store('mic', S.micId); startAudio(); };
  $('micCh').onchange = () => {
    S.micCh = +$('micCh').value;
    if (S.splitter && S.node) { S.splitter.disconnect(); S.splitter.connect(S.node, S.micCh, 0); }
  };
  $('out').onchange = async () => {
    S.outId = $('out').value; store('out', S.outId);
    if (S.ctx && S.ctx.setSinkId) { try { await S.ctx.setSinkId(S.outId); status('Output: ' + $('out').selectedOptions[0].textContent); } catch (e) { status('Could not switch output: ' + e.message, true); } }
  };
  S.micId = recall('mic') || '';
  S.outId = recall('out') || '';
  seg('chans', (v) => { S.chans = v; if (S.noise) setNoise(true); });
  seg('len', (v) => { S.seconds = +v; });
  $('level').oninput = () => { S.level = +$('level').value; $('levelText').textContent = S.level + ' dB'; if (S.noise) setNoise(true); };
  $('noise').onclick = () => setNoise(!S.noise);
  $('measure').onclick = () => (S.busy ? stopMeasure() : measure());
  $('calLoad').onclick = () => $('calFile').click();
  $('calFile').onchange = () => { const f = $('calFile').files[0]; if (f) loadCal(f); $('calFile').value = ''; };
  $('calClear').onclick = clearCal;
  seg('view', (v) => { S.view = v; $('corrPanel').hidden = v !== 'correction'; $('smooth').style.visibility = v === 'correction' ? 'hidden' : ''; draw(); });
  initCorrection();
  seg('smooth', (v) => { S.smooth = +v; draw(); });
  seg('win', (v) => {
    S.windowMs = +v;
    for (const run of S.runs) { for (const c of run.ch) c.cache = null; finishRun(run); }
    renderRuns(); draw();
  });
  $('norm').onclick = () => { S.norm = !S.norm; $('norm').classList.toggle('on', S.norm); draw(); };
  $('useCal').onclick = () => {
    S.useCal = !S.useCal; $('useCal').classList.toggle('on', S.useCal);
    for (const run of S.runs) finishRun(run);
    renderRuns(); draw();
  };
  $('save').onclick = saveSession;
  $('open').onclick = () => $('openFile').click();
  $('openFile').onchange = () => { const f = $('openFile').files[0]; if (f) openSession(f); $('openFile').value = ''; };
  new ResizeObserver(() => draw()).observe(canvas);
  if (document.fonts) document.fonts.ready.then(draw);
  if (S.sim) status('Test mode: a simulated room stands in for the speakers and mic. START AUDIO, then MEASURE.');
  window.pumpRoom = { S, W, measure, startAudio, applyCal, saveSession, openSession, draw, designCorrection, saveProfile, exportWindows, exportPi };   // for tests
}

init();
