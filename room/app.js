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
  busy: false, noise: false, flip: false, unsaved: false, pending: null, bt: false, wake: null,
  corr: { tilt: 0, bass: 0, treble: 0, amount: 100, fLo: 20, fHi: 20000, boost: 6, cut: 15, nulls: true, low: true, align: true,
          phase: false, pLo: 60, pHi: 3000, linear: false, spread: true, fit: false, bands: 10 },
  design: null,
  hp: { file: null, target: 'over', tilt: 0, bass: 0, treble: 0, amount: 100, boost: 8, fHi: 12000, fit: false, bands: 10 }, hpDesign: null,
  sim: /(^|[#&?])sim\b/.test(location.hash + location.search),
};
const GRID = { lo: 20, hi: 20000, perOct: 96 };
const TAIL_S = 2.0, PRE_S = 0.5, SEARCH_S = 1.0, SPEED = 343;
// Bluetooth playback: its delay is longer (often 100-300 ms) and changes from one connection to the next, the receiver's
// clock drifts against this one, and its codec may stop at about 16 kHz. So: a longer search for the sweep, a longer
// lead-in (receivers wake up and fade in), a whisper of noise under the run (some mute on digital silence), and only
// the frequency response is used: no time alignment, no phase correction, no handheld tracking, EQ up to 16 kHz.
const BT = { preS: 1.5, tailS: 2.6, searchS: 2.4, fHi: 16000, keepAlive: 1e-4 };

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
    keepAwake(true);
    $('rate').textContent = (S.fs / 1000).toFixed(1) + ' kHz';
    await listDevices();
    $('start').textContent = 'RESTART AUDIO';
    $('start').classList.remove('hot');
    $('measure').disabled = false;
    $('noise').disabled = false;
    $('flip').disabled = S.chans !== 'LR';
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
  keepAwake(false);
  $('measure').disabled = true;
  $('noise').disabled = true;
  $('flip').disabled = true;
}

// Phones: keep the screen on while audio runs (a sleeping screen stops the audio mid-sweep)
async function keepAwake(on) {
  try {
    if (on && navigator.wakeLock && !S.wake) { S.wake = await navigator.wakeLock.request('screen'); S.wake.addEventListener('release', () => { S.wake = null; }); }
    else if (!on && S.wake) { await S.wake.release(); S.wake = null; }
  } catch (e) { S.wake = null; }
}

function showLink() {
  $('link').querySelectorAll('button').forEach((b) => b.classList.toggle('on', (b.dataset.v === 'bt') === S.bt));
  const canPick = !!(S.ctx && S.ctx.setSinkId);
  const notes = [];
  if (S.bt) notes.push('<b>Bluetooth:</b> the response is measured, timing isn\'t (its delay varies): the correction is EQ only, up to 16 kHz, and HANDHELD needs a wired link.');
  if (S.ctx && !canPick) notes.push('This browser plays through the device\'s own output: on a phone, its speaker or whatever is connected by Bluetooth or USB. Choose <b>BLUETOOTH</b> if the speakers are connected that way.');
  $('linkNote').innerHTML = notes.join('<br>');
  $('linkNote').hidden = notes.length === 0;
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
  $('out').title = canPick ? '' : 'This browser plays to the system output (Chrome and Edge on a computer can choose)';
  showLink();
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
    S.runProgress = m.done / m.length;
  } else if (m.type === 'done') {
    $('prog').style.width = '100%';
    if (S.pending) { S.pending.resolve(m); S.pending = null; }
  } else if (m.type === 'aborted') {
    if (S.pending) { S.pending.reject(new Error('Stopped')); S.pending = null; }
  }
}

// Pink noise at the sweeps' loudness (the same RMS: a sweep at LEVEL dB peak is LEVEL - 3 dB RMS), so setting the
// volume by ear with it sets the sweeps too. FLIP RIGHT inverts the right speaker's noise for the polarity test.
function setNoise(on, announce = true) {
  const was = S.noise;
  S.noise = on;
  $('noise').classList.toggle('active', on);
  $('noise').textContent = on ? 'STOP NOISE' : 'TEST LEVEL';
  const flip = S.flip && S.chans === 'LR';
  if (S.node) S.node.port.postMessage({ type: 'noise', on, left: S.chans !== 'R', right: S.chans !== 'L', level: S.level - 3, flipRight: flip });
  if (!announce || S.view === 'handheld') return;
  if (on && flip) status('Right speaker flipped. From your seat: the way that sounds solid and centred, with fuller bass, is right. Correctly wired speakers sound best with FLIP RIGHT off.');
  else if (on && !was) status('Test noise playing. Hold the mic where you\'ll measure: at your listening spot, ear height, not near a speaker. Set the volume so MIC LEVEL sits in the shaded zone; the sweeps play at this same loudness.');
}

function setFlip(on) {
  S.flip = on;
  $('flip').classList.toggle('on', on);
  if (S.noise) setNoise(true);
  else if (on) status('Press TEST LEVEL to hear the noise with the right speaker flipped.');
}

// ---------------------------------------------------------------------------------------------------------------------
// measuring

// Play sweeps out of `sides` one after the other and record the mic in the same callbacks; returns the recording's
// noise floor and, per side, a measurement id in the wasm (or a note why there is none). Used by MEASURE and the
// handheld mode (its short location pings too).
async function capture({ sides, seconds, level, preS = PRE_S, tailS = TAIL_S, searchS = SEARCH_S, minSnr = 10 }) {
  const fs = S.fs;
  if (S.bt) { preS = Math.max(preS, BT.preS); tailS = Math.max(tailS, BT.tailS); searchS = Math.max(searchS, BT.searchS); }
  const n = W.x.room_sweep(fs, 10, 0, seconds, level);
  const sweep = W.f32(W.x.room_sweep_ptr(), n).slice();
  const pre = Math.round(preS * fs), seg = n + Math.round(tailS * fs), total = pre + sides.length * seg;
  const out = [new Float32Array(total), new Float32Array(total)];
  sides.forEach((side, k) => out[side === 'L' ? 0 : 1].set(sweep, pre + k * seg));
  if (S.bt) for (const o of out) for (let i = 0; i < total; ++i) if (o[i] === 0) o[i] = (Math.random() * 2 - 1) * BT.keepAlive;
  const result = await new Promise((resolve, reject) => {
    S.pending = { resolve, reject };
    S.node.port.postMessage({ type: 'run', out, length: total }, [out[0].buffer, out[1].buffer]);
  });
  const rec = result.rec;
  // the noise floor: the second half of the lead-in (a Bluetooth receiver may still be waking up in the first)
  const noiseDb = pre > 0.1 * fs ? rmsDb(rec.subarray(Math.max(Math.round(0.05 * fs), pre >> 1), pre - Math.round(0.05 * fs))) : null;
  const chans = [];
  for (let k = 0; k < sides.length; ++k) {
    const side = sides[k], name = side === 'L' ? 'left' : 'right';
    const part = rec.subarray(pre + k * seg, pre + (k + 1) * seg);
    const p = W.putF32(part);
    const id = W.x.room_analyze(p, part.length, Math.round(Math.min(searchS, tailS) * fs));
    W.x.room_release_buf(p);
    if (id < 0) { chans.push({ side, id: null, note: `Nothing heard from the ${name} speaker.` }); continue; }
    if (noiseDb !== null && W.x.room_info(id, 3) - noiseDb < minSnr) {
      W.x.room_free(id);
      chans.push({ side, id: null, note: `The ${name} sweep was not heard over the noise: check the output, the volume and the mic.` });
      continue;
    }
    chans.push({ side, id, note: '' });
  }
  return { fs, noiseDb, dropped: result.dropped, chans };
}

async function measure() {
  if (!S.node || S.busy) return;
  setNoise(false);
  S.busy = true;
  $('measure').textContent = 'STOP';
  $('measure').classList.remove('hot');
  $('prog').style.width = '0%';
  const sides = S.chans === 'LR' ? ['L', 'R'] : [S.chans];
  try {
    status(`Measuring ${sides.map((s) => (s === 'L' ? 'left' : 'right')).join(' then ')}… keep quiet and still.`);
    const cap = await capture({ sides, seconds: S.seconds, level: S.level });
    const name = $('pos').value.trim() || 'Position ' + (S.runs.length + 1);
    const run = runFrom(cap, name, S.seconds, S.level);
    if (run.ch.length === 0) {
      status(run.notes.join(' ') || 'Nothing was measured.', true);
    } else {
      finishRun(run);
      S.runs.push(run);
      S.unsaved = true;
      if (U.latest) showUpdate();
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

function runFrom(cap, name, seconds, level) {
  const run = { name, time: new Date().toISOString(), fs: cap.fs, seconds, level, noiseDb: cap.noiseDb ?? -120, ch: [], notes: [], use: true, bt: S.bt };
  if (cap.dropped > 0) run.notes.push('The mic input dropped out during the run: measure again.');
  for (const c of cap.chans) {
    if (c.id === null) run.notes.push(c.note);
    else run.ch.push(channelFrom(c.id, c.side));
  }
  return run;
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
    run.polarity = polarityMatch(l, r);
  }
}

// Do the two speakers push the same way? +1 yes, -1 no (one is wired backwards), 0 can't tell. The sign of one
// peak each is easily fooled (a two-way speaker's tweeter and woofer give lobes of opposite sign and similar size,
// and a small mic offset or toe-in decides which one is picked), so this compares the direct sounds as a whole:
// both low-passed below the crossover with no phase shift, lined up on their biggest low-passed lobe, then
// correlated over a small range of lags. The lag range stays well inside half a period of the low-pass so it can't line up a
// lobe with its opposite.
function polarityMatch(l, r) {
  if (l.fs !== r.fs) return 0;
  const fs = l.fs;
  const prep = (c) => {
    const n = W.x.room_info(c.id, 5), ir = W.f32(W.x.room_ir_ptr(c.id), n);
    const at = c.arrival - c.irStart;
    const a = Math.max(0, Math.floor(at - 0.004 * fs)), b = Math.min(n, Math.ceil(at + 0.012 * fs));
    const x = zeroPhaseLowpass(ir.slice(a, b), 700, fs);
    // line up on the low-passed direct sound's biggest lobe (the arrival itself may be on either driver's lobe)
    let pk = Math.round(at - a), m = 0;
    for (let i = Math.max(0, Math.round(at - a - 0.001 * fs)); i < Math.min(x.length, Math.round(at - a + 0.003 * fs)); ++i)
      if (Math.abs(x[i]) > m) { m = Math.abs(x[i]); pk = i; }
    return { x, at: pk };
  };
  const L = prep(l), R = prep(r);
  const pre = 0.0015 * fs, post = 0.0025 * fs, maxLag = Math.round(0.0003 * fs);
  let best = 0;
  for (let lag = -maxLag; lag <= maxLag; ++lag) {
    let sxy = 0, sxx = 0, syy = 0;
    for (let k = -Math.floor(pre); k <= Math.ceil(post); ++k) {
      const w = 0.5 + 0.5 * Math.cos(Math.PI * (k < 0 ? k / pre : k / post));   // Hann, peaking at the arrival
      const xi = Math.round(L.at) + k, yi = Math.round(R.at) + k + lag;
      if (xi < 0 || yi < 0 || xi >= L.x.length || yi >= R.x.length) continue;
      const x = L.x[xi] * w, y = R.x[yi] * w;
      sxy += x * y; sxx += x * x; syy += y * y;
    }
    const rho = sxx > 0 && syy > 0 ? sxy / Math.sqrt(sxx * syy) : 0;
    if (Math.abs(rho) > Math.abs(best)) best = rho;
  }
  return best >= 0.4 ? 1 : (best <= -0.4 ? -1 : 0);
}

// 2nd-order Butterworth low-pass run forwards then backwards (no phase shift)
function zeroPhaseLowpass(x, fc, fs) {
  const w = 2 * Math.PI * fc / fs, al = Math.sin(w) / Math.SQRT2, c = Math.cos(w), a0 = 1 + al;
  const b0 = (1 - c) / 2 / a0, b1 = (1 - c) / a0, b2 = b0, a1 = -2 * c / a0, a2 = (1 - al) / a0;
  const pass = (v) => {
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < v.length; ++i) {
      const xi = v[i], y = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = xi; y2 = y1; y1 = y; v[i] = y;
    }
  };
  const y = Float64Array.from(x);
  pass(y); y.reverse(); pass(y); y.reverse();
  return y;
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
  else if (S.view === 'handheld') drawHand(w, h);
  else if (S.view === 'headphones') drawHeadphones(w, h);
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
  if (run.bt) parts.push('TIMING: <b>not measured over Bluetooth</b> (its delay varies)');
  else if (Math.abs(dt) < 0.02) parts.push('TIMING: <b>both arrive together</b>');
  else {
    const later = dt > 0 ? 'right' : 'left', earlier = dt > 0 ? 'left' : 'right';
    parts.push(`TIMING: <b>${later} ${Math.abs(dt).toFixed(2)} ms later (${cm.toFixed(1)} cm further)</b> → delay ${earlier} by ${Math.abs(dt).toFixed(2)} ms`);
  }
  const ld = run.levelDiff;
  parts.push(Math.abs(ld) < 0.25 ? 'LEVEL: <b>matched</b>' : `LEVEL: <b>right ${Math.abs(ld).toFixed(1)} dB ${ld > 0 ? 'louder' : 'quieter'}</b> (500 Hz–2 kHz)`);
  if (run.polarity === 1) parts.push('POLARITY: <b>matched</b>');
  else if (run.polarity === -1) parts.push('<span class="warn">POLARITY: the speakers push opposite ways: one may be wired backwards. Check by ear with FLIP RIGHT POLARITY on the test noise.</span>');
  else parts.push('POLARITY: <b>unclear</b> (check by ear with FLIP RIGHT POLARITY on the test noise)');
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
      name: run.name, time: run.time, fs: run.fs, seconds: run.seconds, level: run.level, noiseDb: run.noiseDb, notes: run.notes, use: run.use, bt: !!run.bt,
      pos: run.pos || null, weight: run.weight || 1, group: run.group || null, height: run.height || 0,
      ch: run.ch.map((c) => {
        const seg = irSegment(c);
        return { side: c.side, arrival: c.arrival, polarity: c.polarity, signalDb: c.signalDb, peakDbFS: c.peakDbFS,
                 irStart: seg.start, ir: b64(seg.data) };
      }),
    })),
  };
  const stamp = new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-');
  download(`pump-room-${stamp}.json`, JSON.stringify(doc), 'application/json');
  S.unsaved = false;
  if (U.latest) showUpdate();
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
                    notes: Array.isArray(r.notes) ? r.notes.map(String) : [], use: r.use !== false, bt: r.bt === true,
                    pos: r.pos && isFinite(+r.pos.x) && isFinite(+r.pos.y) ? { x: +r.pos.x, y: +r.pos.y } : undefined,
                    weight: +r.weight > 0 ? +r.weight : 1, group: r.group === 'mix' || r.group === 'couch' ? r.group : undefined,
                    height: +r.height === 1 || +r.height === -1 ? +r.height : 0 };
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

// any measurement in the correction made over Bluetooth: EQ only
function btRuns() { return S.runs.some((r) => r.use && r.bt); }
function showBtLocks() {
  const bt = btRuns();
  for (const id of ['cAlign', 'cPhase']) $(id).disabled = bt;
  $('cAlign').title = bt ? 'Off: measured over Bluetooth, whose delay varies' : 'Delay the nearer speaker so both arrive together';
  $('hCal').disabled = S.busy || !S.node || S.bt;
}

function designCorrection() {
  showBtLocks();
  const runs = S.runs.filter((r) => r.use);
  W.x.room_corr_begin();
  let added = 0;
  for (const run of runs) for (const c of run.ch) added += W.x.room_corr_add(c.side === 'L' ? 0 : 1, c.id, S.windowMs, S.useCal && S.cal ? 1 : 0, run.weight || 1) ? 1 : 0;
  if (!added) { S.design = null; return null; }
  const c = S.corr, bt = btRuns(), dt = c.align && !bt ? meanDt(runs) : 0;
  const params = [c.tilt, c.bass, c.treble, c.fLo, bt ? Math.min(c.fHi, BT.fHi) : c.fHi, c.boost, c.cut, c.nulls ? 1 : 0, c.low ? 1 : 0, c.amount / 100,
                  dt > 0 ? dt : 0, dt < 0 ? -dt : 0, c.phase && !bt ? 1 : 0, c.pLo, c.pHi, c.spread ? 1 : 0, 1];
  const p = W.x.room_alloc(params.length * 8);
  W.f64(p, params.length).set(params);
  const n = W.x.room_corr_design(p);
  W.x.room_release_buf(p);
  const d = { n, preamp: W.x.room_corr_info(0, 0), sides: [], positions: runs.length };
  for (let s = 0; s < 2; ++s) {
    const get = (k) => { const q = W.x.room_corr_curve(s, k); return q ? W.f64(q, n).slice() : null; };
    const m = get(0);
    d.sides.push(m ? { measured: m, target: get(1), correction: get(2), result: get(3), guard: W.x.room_corr_info(s, 1), delay: W.x.room_corr_info(s, 2),
                       gd: get(4), gdCorr: get(5), spread: get(6) } : null);
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
  const c = S.corr;
  drawDesign(w, h, d, { fLo: c.fLo, fHi: c.fHi, boost: c.boost, cut: c.cut, pLo: c.pLo, pHi: c.pHi, linear: c.linear,
    showPhase: c.phase && !btRuns(), sides: [0, 1], fits: c.fit ? d.sides.map((sd) => (sd ? fitCurve(sd.correction, c.bands, c.fHi) : null)) : null,
    title: `AVERAGE OF ${d.positions} POSITION${d.positions === 1 ? '' : 'S'} (FAINT${d.positions >= 2 ? ', SHADED: HOW MUCH THEY DIFFER' : ''}) · CORRECTED (BOLD) · TARGET (DASHED)` });
}

// the design's curves: measured, corrected and target on top, the correction under them, and (showPhase) the arrival
// by frequency at the bottom. cfg: the band, limits and phase settings it was designed with, which sides to draw,
// the title.
function drawDesign(w, h, d, cfg) {
  $('empty').style.display = 'none';
  const F = corrFreqs();
  const showPhase = cfg.showPhase;
  const split = Math.round(h * (showPhase ? 0.5 : 0.66)), split2 = showPhase ? Math.round(h * 0.73) : h;
  const top = { x0: 46, x1: w - 14, y0: 14, y1: split - 10 }, bot = { x0: 46, x1: w - 14, y0: split + 8, y1: showPhase ? split2 - 10 : h - 26 };
  const ph = { x0: 46, x1: w - 14, y0: split2 + 8, y1: h - 26 };
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
  const cMax = Math.max(6, Math.ceil(Math.max(cfg.boost, cfg.cut) / 3) * 3);
  const cy = (db) => bot.y0 + (bot.y1 - bot.y0) * (cMax - db) / (2 * cMax);
  const colours = [css(theme.ink), css(theme.second)];

  // grid
  g2.strokeStyle = theme.grid; g2.lineWidth = 1; g2.font = font(10, true); g2.fillStyle = css(theme.ink2);
  for (const hz of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
    const x = Math.round(lx(hz)) + 0.5;
    for (const fr of showPhase ? [top, bot, ph] : [top, bot]) { g2.beginPath(); g2.moveTo(x, fr.y0); g2.lineTo(x, fr.y1); g2.stroke(); }
    g2.textAlign = 'center'; g2.textBaseline = 'top'; g2.fillText(fmtHz(hz, true), x, (showPhase ? ph : bot).y1 + 6);
  }
  g2.textAlign = 'right'; g2.textBaseline = 'middle';
  for (let db = yBot; db <= yTop; db += 10) { const y = Math.round(ly(db)) + 0.5; g2.beginPath(); g2.moveTo(top.x0, y); g2.lineTo(top.x1, y); g2.stroke(); g2.fillText(db + (db === yTop ? ' dB' : ''), top.x0 - 6, y); }
  for (let db = -cMax; db <= cMax; db += cMax / 2) { const y = Math.round(cy(db)) + 0.5; g2.globalAlpha = db === 0 ? 1 : 0.6; g2.beginPath(); g2.moveTo(bot.x0, y); g2.lineTo(bot.x1, y); g2.stroke(); g2.globalAlpha = 1; g2.fillText((db > 0 ? '+' : '') + db, bot.x0 - 6, y); }
  // the band corrected
  g2.fillStyle = theme.grid;
  if (cfg.fLo > 20) for (const fr of [top, bot]) g2.fillRect(fr.x0, fr.y0, lx(cfg.fLo) - fr.x0, fr.y1 - fr.y0);
  if (cfg.fHi < 20000) for (const fr of [top, bot]) g2.fillRect(lx(cfg.fHi), fr.y0, fr.x1 - lx(cfg.fHi), fr.y1 - fr.y0);

  const line = (fr, arr, y, colour, width, dash, alpha) => {
    g2.save(); g2.beginPath(); g2.rect(fr.x0, fr.y0, fr.x1 - fr.x0, fr.y1 - fr.y0); g2.clip();
    g2.strokeStyle = colour; g2.lineWidth = width; g2.setLineDash(dash || []); g2.globalAlpha = alpha || 1; g2.lineJoin = 'round';
    g2.beginPath();
    for (let i = 0; i < F.length; ++i) { const X = lx(F[i]), Y = y(arr[i]); if (i) g2.lineTo(X, Y); else g2.moveTo(X, Y); }
    g2.stroke(); g2.restore();
  };
  // how much the positions disagree: a band of ± one standard deviation around each speaker's average
  if (d.positions >= 2) d.sides.forEach((sd, s) => {
    if (!sd || !sd.spread || !cfg.sides.includes(s)) return;
    g2.save(); g2.beginPath(); g2.rect(top.x0, top.y0, top.x1 - top.x0, top.y1 - top.y0); g2.clip();
    g2.beginPath();
    for (let i = 0; i < F.length; ++i) { const X = lx(F[i]), Y = ly(sd.measured[i] + off + sd.spread[i]); if (i) g2.lineTo(X, Y); else g2.moveTo(X, Y); }
    for (let i = F.length - 1; i >= 0; --i) g2.lineTo(lx(F[i]), ly(sd.measured[i] + off - sd.spread[i]));
    g2.closePath(); g2.fillStyle = css(s ? theme.second : theme.ink, 0.09); g2.fill(); g2.restore();
  });
  d.sides.forEach((sd, s) => {
    if (!sd || !cfg.sides.includes(s)) return;
    const shift = (a) => a.map((v) => v + off);
    line(top, shift(sd.measured), ly, colours[s], 1.25, null, 0.35);
    line(top, shift(sd.result), ly, colours[s], 2);
    line(bot, sd.correction, cy, colours[s], 1.75);
    if (sd.guard > 0) {
      const x = lx(sd.guard);
      g2.strokeStyle = colours[s]; g2.setLineDash([2, 3]); g2.beginPath(); g2.moveTo(x, bot.y0); g2.lineTo(x, bot.y1); g2.stroke(); g2.setLineDash([]);
    }
  });
  // the parametric EQ fit, dotted over the correction it approximates
  if (cfg.fits) cfg.fits.forEach((fit, s) => {
    if (!fit || !cfg.sides.includes(s)) return;
    line(bot, Array.from(F, (hz) => fit.curve(hz)), cy, colours[s], 2, [2, 3]);
  });
  const tgt = d.sides.find((x) => x);
  line(top, tgt.target.map((v) => v + off), ly, css(theme.ink2), 1.5, [6, 4]);
  if (showPhase) {
    // arrival by frequency: each speaker's group delay over its treble's (faint), and after the phase correction (bold)
    const rel = (a) => { let r = 0, k2 = 0; for (let i = 0; i < F.length; ++i) if (F[i] >= 3000 && F[i] <= 8000) { r += a[i]; ++k2; } r /= k2 || 1; return a.map((v) => v - r); };
    let gMax = 2;
    d.sides.forEach((sd) => { if (sd) for (let i = 0; i < F.length; ++i) if (F[i] >= 40 && F[i] <= 10000) gMax = Math.max(gMax, Math.abs(rel(sd.gd)[i])); });
    gMax = Math.min(20, Math.ceil(gMax));
    const gy = (ms) => ph.y0 + (ph.y1 - ph.y0) * (gMax - Math.max(-gMax, Math.min(gMax, ms))) / (2 * gMax);
    g2.strokeStyle = theme.grid; g2.fillStyle = css(theme.ink2); g2.font = font(10, true); g2.textAlign = 'right'; g2.textBaseline = 'middle';
    for (const ms of [-gMax, 0, gMax]) { const y = Math.round(gy(ms)) + 0.5; g2.beginPath(); g2.moveTo(ph.x0, y); g2.lineTo(ph.x1, y); g2.stroke(); g2.fillText((ms > 0 ? '+' : '') + ms + (ms === gMax ? ' ms' : ''), ph.x0 - 6, y); }
    g2.fillStyle = theme.grid;
    g2.fillRect(ph.x0, ph.y0, lx(cfg.pLo) - ph.x0, ph.y1 - ph.y0);
    g2.fillRect(lx(cfg.pHi), ph.y0, ph.x1 - lx(cfg.pHi), ph.y1 - ph.y0);
    d.sides.forEach((sd, s) => {
      if (!sd) return;
      const g0 = rel(sd.gd);
      line(ph, g0, gy, colours[s], 1.25, null, 0.35);
      line(ph, g0.map((v, i) => v + sd.gdCorr[i]), gy, colours[s], 2);
    });
  }
  g2.strokeStyle = css(theme.ink); g2.lineWidth = 1.5;
  for (const fr of showPhase ? [top, bot, ph] : [top, bot]) g2.strokeRect(fr.x0 + 0.75, fr.y0 + 0.75, fr.x1 - fr.x0 - 1.5, fr.y1 - fr.y0 - 1.5);
  g2.font = font(11); g2.fillStyle = css(theme.ink2); g2.textAlign = 'left'; g2.textBaseline = 'top';
  g2.fillText(cfg.title, top.x0 + 8, top.y0 + 6);
  g2.fillText(cfg.fits ? `CORRECTION · PARAMETRIC EQ FIT, ${cfg.fits.find(Boolean).filters.length} BANDS (DOTTED)` : 'CORRECTION', bot.x0 + 8, bot.y0 + 6);
  if (showPhase) g2.fillText(cfg.linear ? 'ARRIVAL BY FREQUENCY (MEASURED FAINT, CORRECTED BOLD)' : 'ARRIVAL BY FREQUENCY: CORRECTED ONLY WITH LINEAR-PHASE FILTERS', ph.x0 + 8, ph.y0 + 6);
}

function showCorrInfo(d) {
  const el = $('cInfo');
  $('cSave').disabled = $('cExport').disabled = !d;
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
  if (btRuns()) parts.push('<span class="warn">BLUETOOTH: EQ only, up to 16 kHz (no time alignment or phase correction: its delay varies).</span>');
  else if (S.corr.phase) {
    const spread = (a) => { let lo = 1e9, hi = -1e9; const F = corrFreqs(); for (let i = 0; i < F.length; ++i) if (F[i] >= Math.max(100, S.corr.pLo) && F[i] <= S.corr.pHi) { lo = Math.min(lo, a[i]); hi = Math.max(hi, a[i]); } return hi - lo; };
    d.sides.forEach((sd, s) => {
      if (!sd) return;
      const after = sd.gd.map((v, i) => v + sd.gdCorr[i]);
      parts.push(`${s ? 'RIGHT' : 'LEFT'} ARRIVAL SPREAD: <b>${spread(sd.gd).toFixed(1)} ms → ${spread(after).toFixed(1)} ms</b>`);
    });
    if (!S.corr.linear) parts.push('<span class="warn">Phase correction is only in LINEAR PHASE exports (and the plugin in linear phase).</span>');
  }
  if (d.positions >= 4) {
    // where the positions disagree most (below 500 Hz, where the room rules)
    const F = corrFreqs();
    let worst = 0, wf = 0;
    d.sides.forEach((sd) => { if (sd && sd.spread) for (let i = 0; i < F.length; ++i) if (F[i] >= 30 && F[i] <= 500 && sd.spread[i] > worst) { worst = sd.spread[i]; wf = F[i]; } });
    if (worst > 3) parts.push(`POSITIONS DIFFER MOST: <b>±${worst.toFixed(1)} dB at ${fmtHz(wf)}</b>` + (S.corr.spread ? ' (corrected less there)' : ' (turn on SPREAD-AWARE to correct less there)'));
  }
  if (S.corr.fit) d.sides.forEach((sd, s) => {
    if (!sd) return;
    const f = fitCurve(sd.correction, S.corr.bands, S.corr.fHi);
    parts.push(`${s ? 'RIGHT' : 'LEFT'} EQ FIT: <b>${f.filters.length} bands, within ${f.worst.toFixed(1)} dB</b> (typically ${f.rms.toFixed(1)})`);
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
  for (const [id, k] of [['cNulls', 'nulls'], ['cLow', 'low'], ['cAlign', 'align'], ['cPhase', 'phase'], ['cSpread', 'spread'], ['cFit', 'fit']]) $(id).classList.toggle('on', c[k]);
  $('cBandsRow').hidden = !c.fit;
  showBtLocks();
  $('cLat').querySelectorAll('button').forEach((b) => b.classList.toggle('on', (b.dataset.v === 'linear') === c.linear));
}

function initCorrection() {
  const slider = (id, key) => { $(id).oninput = () => { S.corr[key] = +$(id).value; showCorrControls(); draw(); }; };
  slider('cTilt', 'tilt'); slider('cBass', 'bass'); slider('cTreble', 'treble'); slider('cAmount', 'amount'); slider('cBoost', 'boost'); slider('cCut', 'cut');
  seg('cLo', (v) => { S.corr.fLo = +v; draw(); });
  seg('cHi', (v) => { S.corr.fHi = +v; draw(); });
  for (const [id, k] of [['cNulls', 'nulls'], ['cLow', 'low'], ['cAlign', 'align'], ['cPhase', 'phase'], ['cSpread', 'spread'], ['cFit', 'fit']]) $(id).onclick = () => { S.corr[k] = !S.corr[k]; showCorrControls(); draw(); };
  seg('cBands', (v) => { S.corr.bands = +v; draw(); });
  seg('cLat', (v) => { S.corr.linear = v === 'linear'; showCorrControls(); draw(); });
  $('cSave').onclick = saveProfile;
  $('cWin').onclick = () => exportWindows();
  $('cPi').onclick = exportPi;
  $('cWavelet').onclick = () => exportGraphicEq(designCorrection(), profileName(), 'the room correction');
  $('cPeq').onclick = () => exportPeq(designCorrection(), profileName(), S.corr.bands, btRuns() ? Math.min(S.corr.fHi, BT.fHi) : S.corr.fHi, false);
  $('cConv').onclick = () => exportConv(designCorrection(), profileName(), S.corr.linear, filterKind());
  $('cPiGuide').onclick = () => { $('guide').hidden = false; $('guideClose').focus(); };
  $('guideClose').onclick = () => { $('guide').hidden = true; };
  $('guide').onclick = (e) => { if (e.target === $('guide')) $('guide').hidden = true; };
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !$('guide').hidden) $('guide').hidden = true; });
  showCorrControls();
}

function filterKind() {
  return S.corr.linear ? `Linear phase (about 0.17 s of latency)${S.corr.phase ? ' with phase correction' : ''}` : 'Minimum phase (no latency)';
}

function profileName() { return ($('cName').value || 'My room').trim().slice(0, 60) || 'My room'; }

function saveProfile() {
  if (!designCorrection()) return;
  const [p, len] = W.putText(profileName());
  const n = W.x.room_corr_profile(p, len, 0);
  W.x.room_release_buf(p);
  const text = new TextDecoder().decode(new Uint8Array(W.x.memory.buffer, W.x.room_text_ptr(), n));
  download(`${fileName(profileName())}.pumproom`, text, 'text/plain');
  status('Profile saved. Double-click it: System EQ (Mac) or Room Profiles (Windows) adds it to Room Profiles in its menu and switches to it. Or load it in pump ROOM (the plugin).');
}

// filters at the usual rates (minimum phase: no latency; preamp and delays inside), a config file that picks the one
// for the device's rate, and a read-me, zipped
function exportWindows(d = designCorrection(), name = profileName(), linear = S.corr.linear, kind = filterKind()) {
  if (!d) return;
  const files = [], rates = [44100, 48000, 88200, 96000, 192000];
  for (const fs of rates) {
    const ch = [0, 1].map((s) => {
      const side = d.sides[s] ? s : (s ? 0 : 1);   // one speaker measured: its filter on both
      const n = W.x.room_corr_fir(side, fs, linear ? 1 : 0);
      return W.f32(W.x.room_fir_ptr(), n).slice();
    });
    files.push({ name: `pump-room-${fs}.wav`, data: wav(ch, fs) });
  }
  const conf = [`# pump ROOM correction: ${name} (${new Date().toISOString().slice(0, 10)})`,
    `# ${kind}. The preamp (${d.preamp.toFixed(2)} dB) and the time alignment are inside the filters.`,
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

// Wavelet (Android) and other apps that take a "GraphicEQ:" line: the correction, the preamp in it (nothing above
// 0 dB), at the 127 frequencies those files use (20 Hz x 1.0563^k). Both speakers' curves are averaged (the app is one
// EQ for both sides); a headphone correction has one curve anyway. Fine enough to carry the correction nearly whole.
function graphicEq(d) {
  const F = corrFreqs(), sides = d.sides.filter(Boolean);
  if (!sides.length) return null;
  const at = (arr, hz) => {
    if (hz <= F[0]) return arr[0];
    for (let i = 1; i < F.length; ++i) if (F[i] >= hz) { const t = Math.log(hz / F[i - 1]) / Math.log(F[i] / F[i - 1]); return arr[i - 1] + t * (arr[i] - arr[i - 1]); }
    return arr[F.length - 1];
  };
  const pts = Array.from({ length: 127 }, (_, k) => Math.floor(20 * Math.pow(1.0563, k))).map((hz) => {
    const db = sides.reduce((a, sd) => a + at(sd.correction, hz), 0) / sides.length;
    return [hz, db + d.preamp];
  });
  const top = Math.max(0, ...pts.map((p) => p[1]));
  return 'GraphicEQ: ' + pts.map(([hz, db]) => `${hz} ${(db - top).toFixed(1)}`).join('; ');
}

function exportGraphicEq(d, name, what) {
  const line = d && graphicEq(d);
  if (!line) return;
  download(`${fileName(name)} GraphicEQ.txt`, line + '\n', 'text/plain');
  status(`Saved ${what} for Wavelet: in Wavelet's graphic equalizer, import this file (or paste its line). It includes the volume cut, so turning Wavelet's EQ off plays louder.`);
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
      const n = W.x.room_corr_fir(side, fs, S.corr.linear ? 1 : 0);
      files.push({ name: `coeffs/pump-room-${s ? 'R' : 'L'}-${fs}.wav`, data: wav([W.f32(W.x.room_fir_ptr(), n).slice()], fs) });
    }
  const yml = [
    `# pump ROOM correction for CamillaDSP: ${name} (${new Date().toISOString().slice(0, 10)})`,
    `# ${filterKind()}. The preamp (${d.preamp.toFixed(2)} dB) and the time alignment are inside the filters.`,
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
    'Shopping list and a longer walkthrough: WHAT-TO-BUY.md (also PI: WHAT TO BUY in pump ROOM).', '',
    'Set up (once per box):',
    '1. Put Raspberry Pi OS Lite (64-bit) on a microSD card with Raspberry Pi Imager; in its settings turn on SSH',
    '   and Wi-Fi so you can reach the Pi from your computer.',
    '2. Plug the audio interface into the Pi\'s USB, boot, and copy this folder over (for example: scp -r).',
    '3. On the Pi, in this folder:  sudo sh install.sh',
    '4. If your interface isn\'t one that shows up as "CODEC", edit the devices part of /etc/pump-room/pump-room.yml:',
    '   the card name from "arecord -l" (device: "hw:NAME,0") and the format it takes (S16_LE, S24_3_LE, S32_LE).',
    '   Then: sudo systemctl restart pump-room', '',
    'New correction for the same room: copy the new export over and run install.sh again (your device settings stay).',
    'moOde or Volumio (they run CamillaDSP themselves): skip install.sh. Upload the files in coeffs/ on their CamillaDSP',
    'page and copy the filters: and pipeline: parts of pump-room.yml into a config there (keep their devices: part).',
    'Status and errors: systemctl status pump-room   /   journalctl -u pump-room', '',
    `The filters include the preamp (${d.preamp.toFixed(2)} dB, headroom for the boosts) and the time alignment.`,
    'They exist for 44.1, 48, 88.2, 96 and 192 kHz; the config runs at 48 kHz (change samplerate to use another).',
    S.corr.linear ? 'Linear-phase filters: about 0.17 s of latency on top (fine for music, too much for TV lip sync).' : 'Minimum-phase filters: no latency of their own.',
    'Latency: about the chunk size (1024 samples, 21 ms at 48 kHz) plus the interface\'s own. Lower chunksize for',
    'less (the Pi has to keep up: if you hear dropouts, raise it again).', ''].join('\n');
  files.push({ name: 'README.txt', data: new TextEncoder().encode(readme) });
  files.push({ name: 'WHAT-TO-BUY.md', data: new TextEncoder().encode(PI_GUIDE_MD) });
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
// handheld: the mic held in the hand, placed by sound alone
//
// Every arrival time is the system's latency plus the distance from that speaker. 1) With the mic against the left
// speaker, a ping from each speaker gives the latency (the left one is a few cm away) and the speakers' spacing (the
// right one is across the room). 2) At the listening spot, the distances to both speakers place the mic (speakers at
// x = -spacing / 2 and + spacing / 2, the listener in front, y > 0): that is the center. 3) A pattern of points around
// it; the mic's position is tracked with short pings (about one a second) and when it rests on a point a full
// measurement of both speakers is taken there, checked (still on the point, levels) and added to the measurements.
// Height isn't tracked (two speakers place the mic in a plane): the user keeps it at ear height.

const H = {
  lat: null, spacing: null, fs: 0, ctx: null, center: null, couch: null, targets: [], done: [], cur: -1,
  mic: null, trail: [], running: false, points: 9, phase: 'away', pop: null, anim: false, info: '', infoErr: false, tail: 1.0, stopReq: false,
};
const HAND = { tolerance: 0.08, still: 0.04, moved: 0.12, grille: 0.03, pingS: 0.25, measureS: 1.0 };

function handPos(tL, tR) {
  const c = 343, dL = Math.max(0.01, (tL - H.lat) / H.fs * c), dR = Math.max(0.01, (tR - H.lat) / H.fs * c), sp = H.spacing;
  const x = (dL * dL - dR * dR) / (2 * sp);
  const y = Math.sqrt(Math.max(0, dL * dL - (x + sp / 2) * (x + sp / 2)));
  return { x, y, dL, dR };
}

// one ping from each speaker: their arrivals (samples), or null with the reason
async function handPing(tailS) {
  const cap = await capture({ sides: ['L', 'R'], seconds: HAND.pingS, level: S.level, preS: 0.02, tailS, searchS: tailS, minSnr: 0 });
  const t = {};
  let why = '';
  for (const c of cap.chans) {
    if (c.id === null) { why = c.note; continue; }
    const peak = W.x.room_info(c.id, 4);
    t[c.side] = W.x.room_info(c.id, 0);
    W.x.room_free(c.id);
    if (peak > -1) why = 'The mic clipped: turn the volume or the sweep level down.';
  }
  if (cap.dropped > 0) why = 'The mic input dropped out.';
  return t.L !== undefined && t.R !== undefined && !why ? { tL: t.L, tR: t.R } : { why: why || 'A speaker was not heard.' };
}

// The dot being measured reacts: it lights up and breathes when the mic reaches it ('on'), a ring fills around it
// while it measures ('measuring'), and it bursts with a tick when the point is kept (or flashes red when it isn't).
// While any of that shows, the map is redrawn every frame.
// each dot its own colour, from the suite's colour wheel (common/ui/Theme.hpp: COIL's springs, SING's words), in the
// same order: the fill is the wheel's bright shade, the outline and tick its deep one (light theme) or a lighter tint
const WHEEL_NEON = [0xFF7466, 0xFFD04A, 0xC8FF4D, 0x5CFFB4, 0x4DF3FF, 0x73B4FF, 0xB9A0FF, 0xFF6FB5,
                    0xFFB24F, 0xDCEB5A, 0x8CFF5A, 0x3DF5D0, 0x5CCBFF, 0x949FFF, 0xDD77FF, 0xFF92C2];
const WHEEL_INK = [0x8C2318, 0x6B4700, 0x3B5C00, 0x006E44, 0x00636D, 0x1443AC, 0x4528B5, 0x8C1257,
                   0x934000, 0x4E5700, 0x235C00, 0x006652, 0x00527A, 0x262C99, 0x650C8A, 0x8A1450];
const dotColour = (i) => rgb(WHEEL_NEON.at(((i % 16) + 16) % 16));
const dotInk = (i) => theme.dark ? mix(dotColour(i), rgb(0xffffff), 0.18) : rgb(WHEEL_INK.at(((i % 16) + 16) % 16));

function handPhase(phase) {
  if (phase === 'measuring' && H.phase !== 'measuring') S.runProgress = 0;
  H.phase = phase;
  handAnimate();
}
function handPop(i, ok) { H.pop = { i, ok, t: performance.now() }; handAnimate(); }
function handAnimate() {
  if (H.anim) return;
  const live = () => S.view === 'handheld' && (H.phase !== 'away' || (H.pop && performance.now() - H.pop.t < 900));
  if (!live()) return;
  H.anim = true;
  const tick = () => {
    draw();
    if (live()) requestAnimationFrame(tick);
    else { H.anim = false; if (H.pop && performance.now() - H.pop.t >= 900) H.pop = null; draw(); }
  };
  requestAnimationFrame(tick);
}

function handInfo(text, err = false) { H.info = text; H.infoErr = err; showHand(); }

async function handRun(fn) {
  if (!S.node || S.busy) { if (!S.node) handInfo('START AUDIO first.', true); return; }
  setNoise(false);
  S.busy = true;
  showHand();
  try { await fn(); } catch (e) { handInfo(e.message === 'Stopped' || e.message === 'Audio stopped' ? 'Stopped.' : 'Failed: ' + e.message, true); }
  finally { S.busy = false; H.running = false; H.phase = 'away'; showHand(); renderRuns(); draw(); }
}

function handFind() {
  return handRun(async () => {
    H.fs = S.fs; H.ctx = S.ctx; H.lat = null; H.spacing = null; H.center = null; H.couch = null; H.targets = []; H.done = []; H.cur = -1; H.mic = null; H.trail = [];
    handInfo('Listening to both speakers…');
    const p = await handPing(1.0);
    if (p.why) { handInfo(p.why, true); return; }
    const lat = p.tL - HAND.grille / 343 * H.fs;
    const spacing = (p.tR - lat) / H.fs * 343;
    if (spacing < 0 && spacing > -8) {
      // the right speaker was heard first: the mic is on the right one, or left and right are swapped on the way out
      handInfo(`The RIGHT speaker was heard first (${(-spacing * 100).toFixed(0)} cm before the left). If the mic is on the left speaker, left and right are swapped somewhere: check the cables, the output's channel setup (and Equalizer APO's) and the speakers' own settings. TEST LEVEL with SPEAKERS on LEFT shows which one plays.`, true);
      return;
    }
    if (!(spacing > 0.3 && spacing < 8)) { handInfo(`That puts the speakers ${spacing.toFixed(2)} m apart: hold the mic against the LEFT speaker's grille, in front of the tweeter, and try again.`, true); return; }
    H.lat = lat; H.spacing = spacing;
    H.tail = Math.min(1.0, lat / H.fs + 0.06);   // pings only need the latency and a few metres
    handInfo(`Speakers ${(spacing * 100).toFixed(0)} cm apart, latency ${(lat / H.fs * 1000).toFixed(1)} ms. Now step 2.`);
  });
}

function handCenter() {
  return handRun(async () => {
    if (H.ctx !== S.ctx) { handInfo('Audio restarted since step 1: find the speakers again.', true); return; }
    const couch = H.points === 'couch' && H.center && !H.couch;
    handInfo('Finding the mic…');
    const p = await handPing(H.tail);
    if (p.why) { handInfo(p.why, true); return; }
    const pos = handPos(p.tL, p.tR);
    if (pos.y < 0.3) { handInfo(`The mic seems to be between the speakers or behind them: hold it at ${couch ? 'the couch' : 'your listening spot'} and try again.`, true); return; }
    H.mic = pos; H.trail = [pos];
    if (couch) {
      if (Math.hypot(pos.x - H.center.x, pos.y - H.center.y) < 0.9) { handInfo('That is close to the mix position: sit on the couch, mic where your head is, and press SET COUCH again.', true); return; }
      H.couch = { x: pos.x, y: pos.y };
      const add = handLayout('couchOnly').map((t) => ({ ...t, x: pos.x + t.dx, y: pos.y + t.dy }));
      H.targets = H.targets.concat(add);
      H.done = H.done.concat(add.map(() => false));
      H.cur = handNext();
      handInfo(`Couch ${pos.dL.toFixed(2)} m from the left speaker, ${pos.dR.toFixed(2)} m from the right. Press ${H.done.some(Boolean) ? 'CONTINUE' : 'START'}.`);
      return;
    }
    H.center = { x: pos.x, y: pos.y };
    H.couch = null;
    handCenterPattern(false);
    handInfo(`Center ${pos.dL.toFixed(2)} m from the left speaker, ${pos.dR.toFixed(2)} m from the right. Press START.`);
  });
}

// Points around the center (metres: x to the right, y away from the speakers), each with its height (h: +1 is 15 cm
// above ear height, -1 below), its weight in the average (w) and its group. 5, 9 or 13 points: one listener, all
// at ear height and weighted the same. MIX: a studio's mix position, wider (the chair moving, leaning in) and at
// three heights, the middle counting most. MIX + COUCH adds a couch or client seat (couchOnly), set on its own and
// weighted to a quarter of the whole.
const HAND_UP = 0.15;
function handLayout(n) {
  const pt = (dx, dy, h = 0, w = 1, group = 'mix') => ({ dx, dy, h, w, group });
  const ring = (count, rx, ry, start, h, w) => Array.from({ length: count }, (_, i) => {
    const a = (start + i * 360 / count) * Math.PI / 180;
    return pt(rx * Math.cos(a), ry * Math.sin(a), typeof h === 'function' ? h(i) : h, w);
  });
  const mix = () => [pt(0, 0), pt(0, 0, 1), pt(0, 0, -1),
    ...ring(6, 0.3, 0.3, 0, (i) => (i % 2 ? -1 : 1), 0.75),
    ...ring(8, 0.7, 0.5, 22.5, 0, 0.5)];
  if (n === 'couchOnly') {
    const wm = mix().reduce((a, t) => a + t.w, 0), w = wm / 3 / 5;   // the couch: a quarter of the total weight
    return [pt(0, 0, 0, w, 'couch'), pt(-0.45, 0, 0, w, 'couch'), pt(0.45, 0, 0, w, 'couch'), pt(0, -0.25, 0, w, 'couch'), pt(0, 0.2, 0, w, 'couch')];
  }
  if (n === 'mix' || n === 'couch') return mix();
  const one = (k, r, start) => ring(k, r, r, start, 0, 1);
  if (+n <= 5) return [pt(0, 0), ...one(4, 0.3, 0)];
  if (+n <= 9) return [pt(0, 0), ...one(8, 0.35, 0)];
  return [pt(0, 0), ...one(6, 0.25, 0), ...one(6, 0.5, 30)];
}

function handCenterPattern(redraw = true) {
  if (!H.center) return;
  H.couch = null;
  H.targets = handLayout(H.points).map((t) => ({ ...t, x: H.center.x + t.dx, y: H.center.y + t.dy }));
  H.done = H.targets.map(() => false);
  H.cur = 0;
  if (redraw) { showHand(); draw(); }
}

const heightWords = (h) => (h > 0 ? `${HAND_UP * 100} cm ABOVE ear height` : `${HAND_UP * 100} cm BELOW ear height`);

function handNext() {
  const at = H.mic || H.center;
  let best = -1, bd = 1e9;
  H.targets.forEach((t, i) => { if (!H.done[i]) { const d = Math.hypot(t.x - at.x, t.y - at.y); if (d < bd) { bd = d; best = i; } } });
  return best;
}

function handStart() {
  if (H.running) { H.stopReq = true; stopMeasure(); return; }
  return handRun(async () => {
    if (H.ctx !== S.ctx || !H.center) { handInfo('Find the speakers and set the center first.', true); return; }
    H.running = true; H.stopReq = false;
    showHand();
    if (H.cur < 0 || H.done[H.cur]) H.cur = handNext();
    let last = null;
    while (H.running && !H.stopReq && H.cur >= 0) {
      const p = await handPing(H.tail);
      if (p.why) { handInfo(p.why, true); last = null; continue; }
      const pos = handPos(p.tL, p.tR);
      H.mic = pos;
      H.trail.push(pos);
      if (H.trail.length > 40) H.trail.shift();
      const t = H.targets[H.cur];
      const off = Math.hypot(pos.x - t.x, pos.y - t.y);
      const still = last && Math.hypot(pos.x - last.x, pos.y - last.y) < HAND.still;
      last = pos;
      const hTip = t.h ? `Mic ${heightWords(t.h)}. ` : '';
      if (off > HAND.tolerance) { handPhase('away'); handInfo(hTip + handDirection(pos, t)); draw(); continue; }
      if (!still) { handPhase('on'); handInfo(`On the point: ${t.h ? 'mic ' + heightWords(t.h) + ', ' : ''}hold still…`); draw(); continue; }
      handPhase('measuring');
      handInfo(`Measuring here${t.h ? ' (' + heightWords(t.h).toLowerCase() + ')' : ''}: hold still…`);
      draw();
      const cap = await capture({ sides: ['L', 'R'], seconds: HAND.measureS, level: S.level, preS: 0.3, tailS: H.tail + 0.6, searchS: H.tail + 0.1 });
      const studio = H.points === 'mix' || H.points === 'couch';
      const nGroup = H.targets.filter((x, i) => H.done[i] && x.group === t.group).length + 1;
      const label = !studio ? `Hand ${H.done.filter(Boolean).length + 1}` : `${t.group === 'couch' ? 'Couch' : 'Mix'} ${nGroup}${t.h > 0 ? ' ▲' : (t.h < 0 ? ' ▼' : '')}`;
      const run = runFrom(cap, label, HAND.measureS, S.level);
      run.weight = t.w || 1;
      if (studio) { run.group = t.group; run.height = t.h || 0; }
      const ok = run.ch.length === 2;
      let why = run.notes.join(' ');
      if (ok) {
        finishRun(run);
        const at = handPos(run.ch[0].arrival, run.ch[1].arrival);
        if (Math.hypot(at.x - t.x, at.y - t.y) > HAND.moved) why = 'The mic moved during the measurement.';
        else {
          const warn = runWarnings(run);
          if (warn.length) why = warn.join(' ');
          run.pos = { x: at.x - H.center.x, y: at.y - H.center.y };
        }
      }
      if (why) {
        run.ch.forEach((c) => W.x.room_free(c.id));
        handPhase('away');
        handPop(H.cur, false);
        handInfo(`Not kept: ${why}`, true);
        last = null;
        continue;
      }
      S.runs.push(run);
      S.unsaved = true;
      if (U.latest) showUpdate();
      H.done[H.cur] = true;
      handPhase('away');
      handPop(H.cur, true);
      H.cur = handNext();
      renderRuns();
      $('save').disabled = false;
      const n = H.done.filter(Boolean).length;
      const couchNext = H.cur < 0 && H.points === 'couch' && !H.couch;
      handInfo(couchNext ? `The ${n} mix points are done. Sit on the couch, mic where your head is, and press SET COUCH.`
        : (H.cur < 0 ? `All ${n} points measured. Open CORRECTION.` : `Point ${n} of ${H.targets.length} kept. Next dot${H.targets[H.cur].h ? ': mic ' + heightWords(H.targets[H.cur].h) : ''}.`));
      draw();
    }
  });
}

function handDirection(pos, t) {
  const dx = t.x - pos.x, dy = t.y - pos.y, parts = [];
  if (Math.abs(dx) > 0.03) parts.push(`${Math.round(Math.abs(dx) * 100)} cm ${dx > 0 ? 'right' : 'left'}`);
  if (Math.abs(dy) > 0.03) parts.push(`${Math.round(Math.abs(dy) * 100)} cm ${dy > 0 ? 'back' : 'forward'}`);
  return 'Move ' + (parts.join(', ') || 'a little') + '.';
}

function handReset() { H.center = null; H.couch = null; H.targets = []; H.done = []; H.cur = -1; H.mic = null; H.trail = []; handInfo(H.lat !== null ? 'Set the center again (step 2).' : ''); draw(); }

function showHand() {
  const ready = H.lat !== null && H.ctx === S.ctx && S.ctx !== null;
  const finished = H.center && H.targets.length && H.done.every(Boolean);
  const step = !ready ? 1 : (!H.center ? 2 : (finished ? 4 : 3));
  for (const k of [1, 2, 3]) { $('hs' + k).classList.toggle('now', k === step); $('hs' + k).classList.toggle('done', k < step); }
  $('hCal').disabled = S.busy || !S.node || S.bt;
  const couchStep = H.points === 'couch' && !!H.center && !H.couch;
  $('hCenter').textContent = couchStep ? 'SET COUCH' : 'SET CENTER';
  $('hCenter').disabled = S.busy || !ready;
  $('hStart').disabled = (S.busy && !H.running) || !H.center || (H.cur < 0 && !H.running);
  const studio = H.points === 'mix' || H.points === 'couch';
  $('hs2t').innerHTML = studio
    ? 'Sit at the mix position. Hold the mic where your head is, at ear height, and press <b>SET CENTER</b>.'
    : 'Sit where you listen. Hold the mic where your head is, at ear height, and press <b>SET CENTER</b>.';
  $('hs3t').innerHTML = studio
    ? 'Press <b>START</b> and move the mic to each dot. ▲ dots: hold the mic 15 cm above ear height; ▼ 15 cm below. Hold still on a dot: it measures by itself.' +
      (H.points === 'couch' ? ' When the mix dots are done, sit on the couch and press <b>SET COUCH</b>, then <b>CONTINUE</b>.' : '')
    : 'Press <b>START</b> and move the mic to each dot. Hold still on a dot: it measures by itself, then shows the next.';
  $('hStart').textContent = H.running ? 'STOP' : (H.done.some(Boolean) ? 'CONTINUE' : 'START');
  $('hRestart').disabled = S.busy || !H.center;
  const n = H.done.filter(Boolean).length;
  const facts = [];
  if (S.bt) facts.push('<span class="warn">HANDHELD needs a wired link: it tracks the mic by timing, and Bluetooth\'s delay varies. Switch LINK to WIRED.</span>');
  if (ready) facts.push(`SPEAKERS <b>${(H.spacing * 100).toFixed(0)} cm</b> APART · LATENCY <b>${(H.lat / H.fs * 1000).toFixed(1)} ms</b>`);
  if (H.center) facts.push(`POINTS <b>${n} / ${H.targets.length}</b>`);
  $('hInfo').innerHTML = facts.join('<br>') + (H.info ? `<br><span class="${H.infoErr ? 'warn' : ''}">${esc(H.info)}</span>` : '');
  if (S.view === 'handheld') status(H.info || 'Handheld: follow the steps.', H.infoErr);
}

function drawHand(w, h) {
  $('empty').style.display = 'none';
  const sp = H.spacing || 2.4, cy0 = H.targets.length ? Math.max(...H.targets.map((t) => t.y)) - 0.3 : (H.center ? H.center.y : 2.5);
  // left: the whole room from above; right (once there's a center): the points, close up
  const split = H.center && w > 560 ? Math.round(w * 0.42) : w;
  const room = { x0: 0, x1: split, y0: 22, y1: h - 8 };
  const X = Math.max(sp / 2 + 0.6, 1.4, ...H.targets.map((t) => Math.abs(t.x) + 0.4)), y0 = -0.35, y1 = cy0 + 0.9;
  const scale = Math.min((room.x1 - room.x0 - 30) / (2 * X), (room.y1 - room.y0 - 20) / (y1 - y0));
  const ox = (room.x0 + room.x1) / 2, oy = room.y0 + 10 - y0 * scale;
  const px = (x) => ox + x * scale, py = (y) => oy + y * scale;
  g2.strokeStyle = theme.grid; g2.lineWidth = 1;
  for (let x = -Math.floor(X * 2) / 2; x <= X; x += 0.5) { g2.beginPath(); g2.moveTo(px(x), py(y0)); g2.lineTo(px(x), py(y1)); g2.stroke(); }
  for (let y = 0; y <= y1; y += 0.5) { g2.beginPath(); g2.moveTo(px(-X), py(y)); g2.lineTo(px(X), py(y)); g2.stroke(); }
  const known = H.spacing !== null;
  [[-sp / 2, 'L'], [sp / 2, 'R']].forEach(([x, name]) => {
    const s2 = Math.max(16, 0.16 * scale);
    g2.fillStyle = css(theme.dim); g2.fillRect(px(x) - s2 / 2 + 3, py(0) - s2 / 2 + 3, s2, s2);
    g2.fillStyle = known ? css(theme.ink) : css(theme.panel2); g2.fillRect(px(x) - s2 / 2, py(0) - s2 / 2, s2, s2);
    g2.strokeStyle = css(theme.ink); g2.lineWidth = 1.5; g2.strokeRect(px(x) - s2 / 2, py(0) - s2 / 2, s2, s2);
    g2.fillStyle = known ? css(theme.on) : css(theme.ink); g2.font = font(11); g2.textAlign = 'center'; g2.textBaseline = 'middle';
    g2.fillText(name, px(x), py(0));
  });
  if (H.center) {
    g2.strokeStyle = css(theme.ink2); g2.lineWidth = 1; g2.setLineDash([3, 3]);
    g2.beginPath(); g2.moveTo(px(-sp / 2), py(0)); g2.lineTo(px(H.center.x), py(H.center.y)); g2.lineTo(px(sp / 2), py(0)); g2.stroke(); g2.setLineDash([]);
  }
  const now = performance.now();
  const marks = (P, pxs, pys, r, bold) => {
    // the current dot's state, under its outline
    const cur = H.targets[H.cur];
    if (cur && H.phase !== 'away') {
      const cx = pxs(cur.x), cy = pys(cur.y), beat = 0.5 + 0.5 * Math.sin(now / 160), col = dotColour(H.cur);
      if (H.phase === 'on') {
        // reached: it lights up and breathes, a ring ripples outward
        g2.beginPath(); g2.arc(cx, cy, r, 0, Math.PI * 2); g2.fillStyle = css(col, 0.35 + 0.4 * beat); g2.fill();
        const ripple = (now % 900) / 900;
        g2.beginPath(); g2.arc(cx, cy, r * (1 + 0.8 * ripple), 0, Math.PI * 2);
        g2.strokeStyle = css(col, 1 - ripple); g2.lineWidth = bold ? 3 : 2; g2.stroke();
      } else if (H.phase === 'measuring') {
        // measuring: solid, with a ring filling clockwise as the sweeps play
        g2.beginPath(); g2.arc(cx, cy, r, 0, Math.PI * 2); g2.fillStyle = css(col, 0.75 + 0.2 * beat); g2.fill();
        const p = Math.max(0.02, Math.min(1, S.runProgress || 0));
        g2.beginPath(); g2.arc(cx, cy, r + (bold ? 7 : 4), -Math.PI / 2, -Math.PI / 2 + p * Math.PI * 2);
        g2.strokeStyle = css(theme.ink); g2.lineWidth = bold ? 4 : 2.5; g2.lineCap = 'round'; g2.stroke(); g2.lineCap = 'butt';
      }
    }
    // a point just kept bursts outward (or flashes red when it wasn't kept)
    if (H.pop && H.targets[H.pop.i]) {
      const a = Math.min(1, (now - H.pop.t) / 700), t0 = H.targets[H.pop.i], cx = pxs(t0.x), cy = pys(t0.y);
      if (a < 1) {
        const col = H.pop.ok ? dotColour(H.pop.i) : theme.red;
        for (const k of [0, 0.25]) {
          const b = Math.max(0, a - k) / (1 - k);
          if (b <= 0) continue;
          g2.beginPath(); g2.arc(cx, cy, r * (1 + 1.6 * b), 0, Math.PI * 2);
          g2.strokeStyle = css(col, 1 - b); g2.lineWidth = (bold ? 4 : 2.5) * (1 - b) + 0.5; g2.stroke();
        }
        if (H.pop.ok) for (let j = 0; j < 8; ++j) {
          const ang = j * Math.PI / 4, d = r * (1.2 + 1.4 * a);
          g2.beginPath(); g2.arc(cx + Math.cos(ang) * d, cy + Math.sin(ang) * d, (bold ? 3 : 2) * (1 - a), 0, Math.PI * 2);
          g2.fillStyle = css(col, 1 - a); g2.fill();
        }
      }
    }
    H.targets.forEach((t, i) => {
      const col = dotColour(i), ink = dotInk(i);
      g2.beginPath(); g2.arc(pxs(t.x), pys(t.y), r, 0, Math.PI * 2);
      if (H.done[i]) { g2.fillStyle = css(col); g2.fill(); }
      else if (i !== H.cur || H.phase === 'away') { g2.fillStyle = css(col, i === H.cur ? 0.45 : (theme.dark ? 0.16 : 0.28)); g2.fill(); }
      g2.strokeStyle = css(i === H.cur ? theme.ink : ink);
      g2.lineWidth = i === H.cur ? (bold ? 3.5 : 2.5) : (bold ? 2 : 1.25); g2.stroke();
      if (H.done[i] && r > 8) { g2.fillStyle = css(rgb(WHEEL_INK.at(i % 16))); g2.font = font(Math.min(16, r)); g2.textAlign = 'center'; g2.textBaseline = 'middle'; g2.fillText('✓', pxs(t.x), pys(t.y)); }
    });
    // heights: points at the same spot drawn as one dot, with ▲ / ▼ beside it for the raised and lowered ones
    if (r > 8) H.targets.forEach((t, i) => {
      if (!t.h) return;
      g2.fillStyle = css(H.done[i] ? theme.ink2 : (i === H.cur ? theme.ink : theme.ink2)); g2.font = font(Math.min(14, r));
      g2.textAlign = 'center'; g2.textBaseline = 'middle';
      g2.fillText(t.h > 0 ? '▲' : '▼', pxs(t.x) + (t.h > 0 ? r + 8 : -(r + 8)), pys(t.y) - r * 0.6);
    });
    if (H.trail.length > 1) {
      g2.strokeStyle = css(theme.second, 0.35); g2.lineWidth = 2; g2.beginPath();
      H.trail.forEach((p, i) => (i ? g2.lineTo(pxs(p.x), pys(p.y)) : g2.moveTo(pxs(p.x), pys(p.y)))); g2.stroke();
    }
    if (H.mic) {
      g2.beginPath(); g2.arc(pxs(H.mic.x), pys(H.mic.y), bold ? 9 : 6, 0, Math.PI * 2);
      g2.fillStyle = css(theme.second); g2.fill(); g2.strokeStyle = css(theme.ink); g2.lineWidth = 1.5; g2.stroke();
    }
  };
  marks(room, px, py, Math.max(3, HAND.tolerance * scale), false);
  if (!H.center) {
    g2.fillStyle = css(theme.ink2); g2.font = font(12); g2.textAlign = 'center'; g2.textBaseline = 'alphabetic';
    g2.fillText(known ? 'STEP 2: SIT WHERE YOU LISTEN, MIC AT YOUR HEAD, SET CENTER' : 'STEP 1: MIC AGAINST THE LEFT SPEAKER, FIND SPEAKERS', w / 2, h - 18);
  }
  g2.fillStyle = css(theme.ink2); g2.font = font(11); g2.textAlign = 'left'; g2.textBaseline = 'top';
  g2.fillText('TOP VIEW · GRID 50 CM', 12, 8);
  if (split >= w) return;

  // the close-up: 1.5 m square around the center, grid 10 cm
  const zone = { x0: split + 12, x1: w - 12, y0: 22, y1: h - 12 };
  g2.strokeStyle = css(theme.ink); g2.lineWidth = 1.5;
  g2.beginPath(); g2.moveTo(split + 0.5, 6); g2.lineTo(split + 0.5, h - 6); g2.stroke();
  // the close-up follows the group being measured (the mix position, or the couch)
  const curT = H.targets[H.cur];
  const zc = curT && curT.group === 'couch' && H.couch ? H.couch : H.center;
  const grp = H.targets.filter((t) => (zc === H.couch ? t.group === 'couch' : t.group !== 'couch'));
  const half = Math.max(0.75, Math.ceil((Math.max(0, ...grp.map((t) => Math.max(Math.abs(t.x - zc.x), Math.abs(t.y - zc.y)))) + 0.15) * 10) / 10);
  const zs = Math.min(zone.x1 - zone.x0, zone.y1 - zone.y0) / (2 * half);
  const zx0 = (zone.x0 + zone.x1) / 2, zy0 = (zone.y0 + zone.y1) / 2;
  const qx = (x) => zx0 + (x - zc.x) * zs, qy = (y) => zy0 + (y - zc.y) * zs;
  g2.strokeStyle = theme.grid; g2.lineWidth = 1;
  for (let d = -half; d <= half + 1e-9; d += 0.1) {
    g2.beginPath(); g2.moveTo(qx(zc.x + d), qy(zc.y - half)); g2.lineTo(qx(zc.x + d), qy(zc.y + half)); g2.stroke();
    g2.beginPath(); g2.moveTo(qx(zc.x - half), qy(zc.y + d)); g2.lineTo(qx(zc.x + half), qy(zc.y + d)); g2.stroke();
  }
  g2.save(); g2.beginPath(); g2.rect(zone.x0, zone.y0, zone.x1 - zone.x0, zone.y1 - zone.y0); g2.clip();
  marks(zone, qx, qy, HAND.tolerance * zs, true);
  // where to go: an arrow from the mic to the current dot
  const t = H.targets[H.cur];
  if (t && H.mic && Math.hypot(H.mic.x - t.x, H.mic.y - t.y) > HAND.tolerance) {
    const ax = qx(H.mic.x), ay = qy(H.mic.y), bx = qx(t.x), by = qy(t.y), ang = Math.atan2(by - ay, bx - ax);
    const ex = bx - Math.cos(ang) * (HAND.tolerance * zs + 4), ey = by - Math.sin(ang) * (HAND.tolerance * zs + 4);
    g2.strokeStyle = css(theme.ink); g2.lineWidth = 2.5; g2.beginPath(); g2.moveTo(ax, ay); g2.lineTo(ex, ey); g2.stroke();
    g2.beginPath(); g2.moveTo(ex, ey);
    g2.lineTo(ex - 12 * Math.cos(ang - 0.45), ey - 12 * Math.sin(ang - 0.45)); g2.lineTo(ex - 12 * Math.cos(ang + 0.45), ey - 12 * Math.sin(ang + 0.45));
    g2.closePath(); g2.fillStyle = css(theme.ink); g2.fill();
  }
  g2.restore();
  g2.fillStyle = css(theme.ink2); g2.font = font(11); g2.textAlign = 'left'; g2.textBaseline = 'top';
  g2.fillText(`CLOSE UP${zc === H.couch ? ': COUCH' : ''} · GRID 10 CM · SPEAKERS ↑`, split + 12, 8);
}


// ---------------------------------------------------------------------------------------------------------------------
// headphones: no measuring here (that needs an ear simulator): a published measurement of the model, or an EQ file made
// for it, corrected to a target with the same design and exports as the room

// The usual targets for a measurement on the common ear simulator, described as filters (kind, Hz, dB, Q, evaluated at
// 96 kHz) plus an offset: over-ear within 0.1 dB RMS of the published curve, in-ear within 0.05 dB.
const HP_TARGETS = {
  over: { offset: -24.862, filters: [['LS', 66.5, 25.0, 0.97], ['PK', 32.9, -3.28, 1.107], ['PK', 149.0, 2.46, 1.381], ['PK', 99.3, 4.94, 0.851],
    ['PK', 213.6, 16.0, 0.2], ['PK', 1406.3, 12.15, 0.2], ['PK', 2486.0, 5.13, 1.024], ['PK', 4066.0, 13.73, 0.563], ['PK', 8887.7, 12.89, 0.62], ['HS', 19041.5, -4.3, 2.911]] },
  in: { offset: -22.059, filters: [['LS', 63.9, 25.0, 0.578], ['PK', 222.0, -6.76, 0.67], ['PK', 30.2, -0.78, 1.078], ['PK', 209.2, 24.95, 0.2],
    ['PK', 2789.3, 18.18, 0.46], ['PK', 4999.1, 7.99, 0.59], ['PK', 8094.5, 10.17, 0.702], ['HS', 19260.9, -5.42, 2.01]] },
};

// one biquad's level (dB) at f: peaking (PK), low shelf (LS) or high shelf (HS), the usual cookbook forms
function biquadDb(kind, fc, gain, q, f, fs = 96000) {
  const w = 2 * Math.PI * fc / fs, A = Math.pow(10, gain / 40), al = Math.sin(w) / (2 * q), c = Math.cos(w);
  let b, a;
  if (kind === 'PK') { b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]; }
  else {
    const sA = 2 * Math.sqrt(A) * al;
    if (kind === 'LS') { b = [A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA)]; a = [(A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA]; }
    else { b = [A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA)]; a = [(A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA]; }
  }
  const ph = 2 * Math.PI * Math.min(f, fs * 0.49) / fs, cr = Math.cos(ph), ci = -Math.sin(ph), c2r = Math.cos(2 * ph), c2i = -Math.sin(2 * ph);
  const nr = b[0] + b[1] * cr + b[2] * c2r, ni = b[1] * ci + b[2] * c2i, dr = a[0] + a[1] * cr + a[2] * c2r, di = a[1] * ci + a[2] * c2i;
  return 10 * Math.log10((nr * nr + ni * ni) / Math.max(1e-30, dr * dr + di * di));
}
const filtersDb = (filters, f, fs = 96000) => filters.reduce((acc, [k, fc, g, q]) => acc + biquadDb(k, fc, g, q, f, fs), 0);

// What a loaded file is: a frequency response (kind 'fr', with the file's own target if it has one) or an EQ to apply
// (kind 'eq'). Parametric ("Filter 1: ON PK Fc 105 Hz Gain 3 dB Q 0.7"), graphic ("GraphicEQ: 20 -3; 21 -3.1; ...")
// or a table of frequency and dB (columns named raw / target are used when there is a header).
function parseHeadphoneFile(text) {
  text = String(text).replace(/^﻿/, '');
  const filters = [];
  const re = /Filter\s*\d*\s*:\s*ON\s+(PK|PEQ|LSC|LSQ|LS|HSC|HSQ|HS)\s+Fc\s+([\d.]+)\s*Hz(?:\s+Gain\s+([-+\d.]+)\s*dB)?(?:\s+Q\s+([\d.]+))?/gi;
  let m;
  while ((m = re.exec(text))) {
    const k = m[1].toUpperCase(), kind = k.startsWith('L') ? 'LS' : (k.startsWith('H') ? 'HS' : 'PK');
    const fc = +m[2], g = +(m[3] || 0), q = +(m[4] || (kind === 'PK' ? 1 : 0.707));
    if (fc > 0 && fc < 48000 && isFinite(g) && Math.abs(g) <= 30 && q > 0 && q < 50) filters.push([kind, fc, g, q]);
  }
  if (filters.length) {
    const f = Array.from(corrFreqs()), db = f.map((x) => filtersDb(filters, x, 48000));   // EQ files are made for 48 kHz
    return { kind: 'eq', f, db, note: `${filters.length} filter${filters.length === 1 ? '' : 's'}` };
  }
  const ge = /GraphicEQ\s*:\s*([^\n\r]*)/i.exec(text);
  if (ge) {
    const pts = ge[1].split(';').map((x) => x.trim().split(/\s+/).map(Number)).filter((x) => x.length >= 2 && x[0] > 0 && isFinite(x[1]));
    pts.sort((a, b) => a[0] - b[0]);
    if (pts.length >= 3) return { kind: 'eq', f: pts.map((x) => x[0]), db: pts.map((x) => x[1]), note: `graphic EQ, ${pts.length} points` };
  }
  // a table
  let fCol = 0, dbCol = 1, tCol = -1;
  const rows = [];
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line[0] === '#' || line[0] === '*') continue;
    const cells = line.split(/[,;\t]|\s+/).filter((x) => x !== '');
    const nums = cells.map((x) => Number(x));
    if (!isFinite(nums[0])) {
      if (!rows.length) {   // a header: find the columns by name
        const names = cells.map((x) => x.toLowerCase());
        const fi = names.findIndex((x) => x.startsWith('freq')), ri = names.indexOf('raw'), ti = names.indexOf('target');
        if (fi >= 0) fCol = fi;
        if (ri >= 0) dbCol = ri; else if (names.length > 1) dbCol = fCol === 0 ? 1 : 0;
        tCol = ti;
      }
      continue;
    }
    if (cells.length <= Math.max(fCol, dbCol)) continue;
    const f = nums[fCol], d = nums[dbCol];
    if (!(f > 0) || !isFinite(d)) continue;
    rows.push([f, d, tCol >= 0 && isFinite(nums[tCol]) ? nums[tCol] : null]);
  }
  rows.sort((a, b) => a[0] - b[0]);
  const clean = rows.filter((r, i) => i === 0 || r[0] > rows[i - 1][0]);
  if (clean.length < 10) return null;
  const out = { kind: 'fr', f: clean.map((r) => r[0]), db: clean.map((r) => r[1]), note: `${clean.length} points, ${fmtHz(clean[0][0])} to ${fmtHz(clean[clean.length - 1][0])}` };
  if (tCol >= 0 && clean.every((r) => r[2] !== null)) out.target = clean.map((r) => r[2]);
  return out;
}

async function loadHeadphones(file) {
  if (file.size > 8 * 1024 * 1024) { status('That file is too large to be a headphone measurement.', true); return; }
  const parsed = parseHeadphoneFile(await file.text());
  if (!parsed) { status(`${file.name}: no frequency response or EQ found in it.`, true); return; }
  const name = file.name.replace(/\.[^.]+$/, '').replace(/\s*(ParametricEQ|GraphicEQ|FixedBandEQ)\s*$/i, '').trim();
  S.hp.file = { name: file.name, ...parsed };
  if (parsed.kind === 'fr' && S.hp.target === 'file' && !parsed.target) S.hp.target = 'over';
  if (parsed.kind === 'fr' && parsed.target && S.hp.target !== 'flat') S.hp.target = 'file';
  $('hpName').value = name.slice(0, 60) || 'My headphones';
  status(parsed.kind === 'eq' ? `Loaded the EQ for ${name}: applied as it is, with your tilt, bass and treble on top.`
    : `Loaded ${name}'s response: corrected to the ${S.hp.target === 'file' ? 'target in the file' : S.hp.target === 'in' ? 'in-ear target' : S.hp.target === 'over' ? 'over-ear target' : 'flat target'}.`);
  showHp(); draw();
}

function designHeadphones() {
  const h = S.hp, file = h.file;
  if (!file) { S.hpDesign = null; return null; }
  const put = (arr) => { const p = W.x.room_alloc(arr.length * 8); W.f64(p, arr.length).set(arr); return p; };
  W.x.room_corr_begin();
  const pf = put(file.f), pd = put(file.kind === 'eq' ? file.db.map((v) => -v) : file.db);
  W.x.room_corr_add_curve(0, pf, pd, file.f.length, 1);
  W.x.room_corr_add_curve(1, pf, pd, file.f.length, 1);
  W.x.room_release_buf(pf); W.x.room_release_buf(pd);
  // the base target: none for an EQ file (it already is the correction)
  let tf = null, td = null;
  const tgt = file.kind === 'eq' ? 'flat' : h.target;
  if (tgt === 'over' || tgt === 'in') { tf = Array.from(corrFreqs()); const t = HP_TARGETS[tgt]; td = tf.map((x) => t.offset + filtersDb(t.filters, x)); }
  else if (tgt === 'file' && file.target) { tf = file.f; td = file.target; }
  if (tf) { const a = put(tf), b = put(td); W.x.room_corr_target(a, b, tf.length); W.x.room_release_buf(a); W.x.room_release_buf(b); }
  const eq = file.kind === 'eq';
  const params = [h.tilt, h.bass, h.treble, 20, eq ? 20000 : h.fHi, eq ? 20 : h.boost, eq ? 24 : 15, eq ? 0 : 1, 0, h.amount / 100, 0, 0, 0, 60, 3000, 0, eq ? 0 : 1];
  const p = W.x.room_alloc(params.length * 8);
  W.f64(p, params.length).set(params);
  const n = W.x.room_corr_design(p);
  W.x.room_release_buf(p);
  const d = { n, preamp: W.x.room_corr_info(0, 0), sides: [], positions: 1 };
  for (let s = 0; s < 2; ++s) {
    const get = (k) => { const q = W.x.room_corr_curve(s, k); return q ? W.f64(q, n).slice() : null; };
    d.sides.push({ measured: get(0), target: get(1), correction: get(2), result: get(3), guard: 0, delay: 0, gd: get(4), gdCorr: get(5), spread: null });
  }
  S.hpDesign = d;
  return d;
}

function drawHeadphones(w, h) {
  const d = designHeadphones();
  showHpOut(d);
  if (!d) { $('empty').style.display = 'flex'; $('empty').textContent = 'Load a headphone measurement or EQ file (LOAD FILE…).'; return; }
  const eq = S.hp.file.kind === 'eq';
  drawDesign(w, h, d, { fLo: 20, fHi: eq ? 20000 : S.hp.fHi, boost: eq ? 15 : S.hp.boost, cut: 15, pLo: 60, pHi: 3000, linear: false, showPhase: false, sides: [0],
    fits: S.hp.fit ? [fitCurve(d.sides[0].correction, S.hp.bands, eq ? 20000 : S.hp.fHi)] : null,
    title: eq ? 'THE EQ FILE AS A RESPONSE (FAINT) · CORRECTED (BOLD) · TARGET (DASHED)' : 'HEADPHONE RESPONSE (FAINT) · CORRECTED (BOLD) · TARGET (DASHED)' });
}

function showHpOut(d) {
  for (const id of ['hpSave', 'hpExport']) $(id).disabled = !d;
  if (!d) { $('hpOut').innerHTML = ''; return; }
  let lo = 1e9, hi = -1e9;
  for (const v of d.sides[0].correction) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
  let fitLine = '';
  if (S.hp.fit) { const f = fitCurve(d.sides[0].correction, S.hp.bands, S.hp.file.kind === 'eq' ? 20000 : S.hp.fHi); fitLine = `<br>EQ FIT <b>${f.filters.length} bands, within ${f.worst.toFixed(1)} dB</b>`; }
  $('hpOut').innerHTML = `PREAMP <b>${d.preamp.toFixed(1)} dB</b><br>CORRECTION <b>${lo.toFixed(1)} / +${Math.max(0, hi).toFixed(1)} dB</b>${fitLine}`;
}

function showHp() {
  const h = S.hp, f = h.file;
  $('hpInfo').innerHTML = f ? `<b>${esc(f.name)}</b><br>${f.kind === 'eq' ? 'EQ file' : 'Frequency response'}: ${esc(f.note)}${f.target ? ', with its target' : ''}` : 'Load your headphones\' frequency response, or an EQ file made for them.';
  $('hpClear').disabled = !f;
  const eq = f && f.kind === 'eq';
  $('hpTarget').querySelectorAll('button').forEach((b) => {
    b.classList.toggle('on', b.dataset.v === (eq ? 'flat' : h.target));
    b.disabled = eq || (b.dataset.v === 'file' && !(f && f.target));
  });
  $('hpTarget').title = eq ? 'An EQ file is already the correction to a target: it is applied as it is' : 'What the headphones should measure like';
  const set = (id, text) => { $(id + 'T').textContent = text; };
  $('hpTilt').value = h.tilt; set('hpTilt', (h.tilt > 0 ? '+' : '') + h.tilt.toFixed(1) + ' dB/oct');
  $('hpBass').value = h.bass; set('hpBass', (h.bass > 0 ? '+' : '') + h.bass.toFixed(1) + ' dB');
  $('hpTreble').value = h.treble; set('hpTreble', (h.treble > 0 ? '+' : '') + h.treble.toFixed(1) + ' dB');
  $('hpAmount').value = h.amount; set('hpAmount', h.amount + ' %');
  $('hpBoost').value = h.boost; set('hpBoost', '+' + h.boost.toFixed(1) + ' dB');
  $('hpBoost').disabled = !!eq;
  $('hpHi').querySelectorAll('button').forEach((b) => { b.classList.toggle('on', +b.dataset.v === h.fHi); b.disabled = !!eq; });
}

function hpName() { return ($('hpName').value || 'My headphones').trim().slice(0, 60) || 'My headphones'; }

function saveHpProfile() {
  if (!designHeadphones()) return;
  const [p, len] = W.putText(hpName());
  const n = W.x.room_corr_profile(p, len, 1);
  W.x.room_release_buf(p);
  const text = new TextDecoder().decode(new Uint8Array(W.x.memory.buffer, W.x.room_text_ptr(), n));
  download(`${fileName(hpName())}.pumproom`, text, 'text/plain');
  status('Headphone profile saved. Double-click it: System EQ (Mac) or Room Profiles (Windows) adds it to Headphones in its menu and switches to it. Or load it in pump ROOM (the plugin).');
}

function initHeadphones() {
  const slider = (id, key) => { $(id).oninput = () => { S.hp[key] = +$(id).value; showHp(); draw(); }; };
  slider('hpTilt', 'tilt'); slider('hpBass', 'bass'); slider('hpTreble', 'treble'); slider('hpAmount', 'amount'); slider('hpBoost', 'boost');
  seg('hpTarget', (v) => { S.hp.target = v; showHp(); draw(); });
  seg('hpHi', (v) => { S.hp.fHi = +v; showHp(); draw(); });
  $('hpLoad').onclick = () => $('hpFile').click();
  $('hpFile').onchange = () => { const f = $('hpFile').files[0]; if (f) loadHeadphones(f); $('hpFile').value = ''; };
  $('hpClear').onclick = () => { S.hp.file = null; showHp(); draw(); };
  $('hpSave').onclick = saveHpProfile;
  $('hpWavelet').onclick = () => exportGraphicEq(designHeadphones(), hpName(), 'the headphone correction');
  $('hpPeq').onclick = () => { const d = designHeadphones(); exportPeq(d, hpName(), S.hp.bands, S.hp.file && S.hp.file.kind === 'eq' ? 20000 : S.hp.fHi, true); };
  $('hpConv').onclick = () => exportConv(designHeadphones(), hpName(), false, 'Minimum phase (no latency): headphone correction');
  seg('hpBands', (v) => { S.hp.bands = +v; showHp(); draw(); });
  $('hpFit').onclick = () => { S.hp.fit = !S.hp.fit; $('hpFit').classList.toggle('on', S.hp.fit); showHp(); draw(); };
  $('hpWin').onclick = () => exportWindows(designHeadphones(), hpName(), false, 'Minimum phase (no latency): headphone correction');
  showHp();
}

// ---------------------------------------------------------------------------------------------------------------------
// parametric EQ: a few bell and shelf filters fitted to a correction (Levenberg-Marquardt), for devices and apps that
// take nothing else (WiiM, miniDSP, Poweramp, the Windows equalizer's parametric mode). Export only: the correction
// itself, and everything that plays it in full, is unchanged. Limits: gain -15..+12 dB, Q 0.3..8 (shelves 0.5..1).
const PEQ_LIMITS = { fLo: 20, fHi: 20000, gLo: -15, gHi: 12, qLo: 0.3, qHi: 8, shelfQLo: 0.5, shelfQHi: 1.0 };

// f, target: the correction (dB) on a log grid; fitHi: the band corrected (above it the correction is ~0); n bands
function fitPeq(f, target, n, fitHi = 20000) {
  const L = { ...PEQ_LIMITS };
  // boosts no bigger than the correction's own (plus a little), so the fit can't buy accuracy with headroom
  L.gHi = Math.min(PEQ_LIMITS.gHi, Math.max(1, ...target.map((v, i) => (f[i] <= fitHi ? v : 0))) + 1.5);
  // points: 1/12 octave from 20 Hz to fitHi (a little past it, so a band doesn't overshoot there)
  const top = Math.min(20000, fitHi * 1.3);
  const at = (hz) => {
    if (hz <= f[0]) return target[0];
    for (let i = 1; i < f.length; ++i) if (f[i] >= hz) { const t = Math.log(hz / f[i - 1]) / Math.log(f[i] / f[i - 1]); return target[i - 1] + t * (target[i] - target[i - 1]); }
    return target[target.length - 1];
  };
  const F = [], T = [], Wt = [];
  for (let hz = 20; hz <= top; hz *= Math.pow(2, 1 / 12)) {
    F.push(hz); T.push(at(hz));
    Wt.push(hz < 30 ? 0.4 : hz > 12000 ? 0.4 : 1);
  }
  const model = (fl) => F.map((hz) => fl.reduce((a, b) => a + biquadDb(b.kind, b.fc, b.gain, b.q, hz, 48000), 0));
  const clamp = (b) => {
    b.fc = Math.min(L.fHi, Math.max(L.fLo, b.fc));
    b.gain = Math.min(L.gHi, Math.max(L.gLo, b.gain));
    b.q = b.kind === 'PK' ? Math.min(L.qHi, Math.max(L.qLo, b.q)) : Math.min(L.shelfQHi, Math.max(L.shelfQLo, b.q));
    return b;
  };
  const toP = (fl) => fl.flatMap((b) => [Math.log(b.fc), b.gain, Math.log(b.q)]);
  const fromP = (fl, p) => fl.map((b, i) => clamp({ kind: b.kind, fc: Math.exp(p[3 * i]), gain: p[3 * i + 1], q: Math.exp(p[3 * i + 2]) }));
  // a small cost on every band's gain keeps bands from fighting each other (a big boost beside a big cut)
  const PEN = 0.02 * F.length / 100;
  const cost = (fl) => { const m = model(fl); let s = 0; for (let i = 0; i < F.length; ++i) s += Wt[i] * (m[i] - T[i]) ** 2; for (const b of fl) s += PEN * b.gain * b.gain; return s; };
  const lm = (fl, iters) => {
    let p = toP(fl), cur = fromP(fl, p), c0 = cost(cur), lam = 1e-2;
    for (let it = 0; it < iters; ++it) {
      const m0 = model(cur), r = F.map((_, i) => Math.sqrt(Wt[i]) * (m0[i] - T[i])).concat(cur.map((b) => Math.sqrt(PEN) * b.gain));
      const k = p.length, J = [];
      for (let j = 0; j < k; ++j) {
        const q = p.slice(); const h = j % 3 === 1 ? 0.05 : 0.01; q[j] += h;
        const q1 = fromP(fl, q), m1 = model(q1);
        J.push(F.map((_, i) => Math.sqrt(Wt[i]) * (m1[i] - m0[i]) / h).concat(q1.map((b, bi) => Math.sqrt(PEN) * (b.gain - cur[bi].gain) / h)));
      }
      const A = Array.from({ length: k }, (_, a) => Array.from({ length: k }, (_, b) => J[a].reduce((s, v, i) => s + v * J[b][i], 0)));
      const g = Array.from({ length: k }, (_, a) => J[a].reduce((s, v, i) => s + v * r[i], 0));
      let improved = false;
      for (let tries = 0; tries < 6 && !improved; ++tries) {
        const M = A.map((row, a) => row.map((v, b) => v + (a === b ? lam * (v + 1e-6) : 0)));
        const d = solve(M, g.map((v) => -v));
        if (!d) { lam *= 10; continue; }
        const np = p.map((v, i) => v + d[i]), nf = fromP(fl, np), c1 = cost(nf);
        if (c1 < c0) { p = toP(nf); cur = nf; c0 = c1; lam = Math.max(1e-6, lam / 3); improved = true; }
        else lam *= 8;
      }
      if (!improved) break;
    }
    return cur;
  };
  // start: shelves for the ends when they sit off zero, then peaks where the error is largest
  let fl = [];
  const meanOf = (lo, hi) => { let s = 0, k = 0; F.forEach((hz, i) => { if (hz >= lo && hz <= hi) { s += T[i]; ++k; } }); return k ? s / k : 0; };
  const lowEnd = meanOf(20, 60), highEnd = meanOf(8000, Math.min(16000, top));
  if (n >= 3 && Math.abs(lowEnd) > 1.5) fl.push({ kind: 'LS', fc: 90, gain: lowEnd, q: 0.7 });
  if (n >= 4 && Math.abs(highEnd) > 1.5 && fitHi > 9000) fl.push({ kind: 'HS', fc: 7000, gain: highEnd, q: 0.7 });
  fl = lm(fl, 15);
  while (fl.length < n) {
    const m = model(fl);
    // the largest error, smoothed over a third of an octave
    let best = -1, bv = 0;
    for (let i = 0; i < F.length; ++i) {
      let s = 0, k = 0;
      for (let j = Math.max(0, i - 2); j <= Math.min(F.length - 1, i + 2); ++j) { s += T[j] - m[j]; ++k; }
      const v = Math.abs(s / k) * Wt[i];
      if (v > bv) { bv = v; best = i; }
    }
    if (best < 0 || bv < 0.25) break;
    const e = T[best] - m[best];
    fl.push(clamp({ kind: 'PK', fc: F[best], gain: e, q: 2 }));
    fl = lm(fl, 12);
  }
  fl = lm(fl, 40);
  fl.sort((a, b) => a.fc - b.fc);
  for (let i = 1; i < fl.length; ++i)
    if (fl[i].kind === 'PK' && fl[i - 1].kind === 'PK' && Math.abs(Math.log2(fl[i].fc / fl[i - 1].fc)) < 1 / 6 && Math.sign(fl[i].gain) !== Math.sign(fl[i - 1].gain)) {
      fl.splice(i, 1); --i;   // a pair cancelling each other: drop one, let the rest absorb it
    }
  fl = lm(fl, 25);
  fl.sort((a, b) => a.fc - b.fc);
  const fitted = (hz) => fl.reduce((a, b) => a + biquadDb(b.kind, b.fc, b.gain, b.q, hz, 48000), 0);
  let worst = 0, sq = 0, k = 0;
  F.forEach((hz, i) => { if (hz >= 30 && hz <= Math.min(10000, fitHi)) { const d = Math.abs(fitted(hz) - T[i]); worst = Math.max(worst, d); sq += d * d; ++k; } });
  return { filters: fl, worst, rms: Math.sqrt(sq / Math.max(1, k)), curve: fitted };
}

// Gaussian elimination with partial pivoting
function solve(A, b) {
  const n = b.length, M = A.map((r, i) => [...r, b[i]]);
  for (let c = 0; c < n; ++c) {
    let piv = c;
    for (let r = c + 1; r < n; ++r) if (Math.abs(M[r][c]) > Math.abs(M[piv][c])) piv = r;
    if (Math.abs(M[piv][c]) < 1e-12) return null;
    [M[c], M[piv]] = [M[piv], M[c]];
    for (let r = c + 1; r < n; ++r) { const k = M[r][c] / M[c][c]; for (let j = c; j <= n; ++j) M[r][j] -= k * M[c][j]; }
  }
  const x = new Array(n).fill(0);
  for (let r = n - 1; r >= 0; --r) { let s = M[r][n]; for (let j = r + 1; j < n; ++j) s -= M[r][j] * x[j]; x[r] = s / M[r][r]; }
  return x;
}


const FIT_CACHE = new Map();
// the fit of one curve (dB on the correction grid), cached by its contents
function fitCurve(curve, n, fHi) {
  let h = 0;
  for (let i = 0; i < curve.length; i += 3) h = (h * 31 + Math.round(curve[i] * 100)) | 0;
  const key = `${n}|${fHi}|${h}`;
  if (FIT_CACHE.has(key)) return FIT_CACHE.get(key);
  const r = fitPeq(Array.from(corrFreqs()), Array.from(curve), n, fHi);
  if (FIT_CACHE.size > 40) FIT_CACHE.clear();
  FIT_CACHE.set(key, r);
  return r;
}
const avgCurve = (sides) => sides[0].correction.map((_, i) => sides.reduce((a, sd) => a + sd.correction[i], 0) / sides.length);

// the text the Windows equalizer, WiiM, Poweramp and REW-style importers (miniDSP) read; the preamp keeps every boost
// under 0 dB
function peqText(fit, title) {
  let top = 0;
  for (let hz = 20; hz <= 20000; hz *= Math.pow(2, 1 / 24)) top = Math.max(top, fit.curve(hz));
  const kind = (k) => (k === 'LS' ? 'LSC' : k === 'HS' ? 'HSC' : 'PK');
  const lines = [`Preamp: ${(-top).toFixed(1)} dB`];
  fit.filters.forEach((b, i) => lines.push(`Filter ${i + 1}: ON ${kind(b.kind)} Fc ${Math.round(b.fc)} Hz Gain ${b.gain.toFixed(1)} dB Q ${b.q.toFixed(2)}`));
  return `# ${title}\n# pump ROOM parametric EQ: ${fit.filters.length} filters, within ${fit.worst.toFixed(1)} dB of the full correction (30 Hz to 10 kHz)\n` + lines.join('\n') + '\n';
}

function exportPeq(d, name, n, fHi, headphones) {
  if (!d) return;
  const sides = d.sides.filter(Boolean);
  status('Fitting the parametric EQ…');
  setTimeout(() => {
    const same = headphones || sides.length < 2;
    if (same) {
      const fit = fitCurve(sides[0].correction, n, fHi);
      download(`${fileName(name)} ParametricEQ.txt`, peqText(fit, `${name}${headphones ? ' (headphones)' : ''}`), 'text/plain');
      status(`Saved a ${fit.filters.length}-band parametric EQ, within ${fit.worst.toFixed(1)} dB of the full correction. It includes the volume cut: turning the EQ off on the device plays louder.`);
      return;
    }
    const both = fitCurve(avgCurve(sides), n, fHi), L = fitCurve(d.sides[0].correction, n, fHi), R = fitCurve(d.sides[1].correction, n, fHi);
    const enc = (t) => new TextEncoder().encode(t);
    const readme = [`pump ROOM parametric EQ: ${name}`, '',
      `${name} ParametricEQ.txt      one EQ for both speakers (the two speakers' corrections averaged): WiiM in Stereo mode, Poweramp, the Windows equalizer`,
      `${name} ParametricEQ L.txt / R.txt  each speaker's own: WiiM in L/R mode, miniDSP (import each into its channel's PEQ)`, '',
      `Each file has ${n} filters at most and a Preamp line, so nothing boosts above 0 dB. They include the volume cut: turning`,
      'the EQ off on the device plays louder by that much. Time alignment and phase correction need the full filters',
      '(SAVE PROFILE, the Windows, Pi or convolution exports): a parametric EQ carries the levels only.', ''].join('\n');
    download(`${fileName(name)}-parametric-eq.zip`, zip([
      { name: `${name} ParametricEQ.txt`, data: enc(peqText(both, `${name} (both speakers)`)) },
      { name: `${name} ParametricEQ L.txt`, data: enc(peqText(L, `${name} (left speaker)`)) },
      { name: `${name} ParametricEQ R.txt`, data: enc(peqText(R, `${name} (right speaker)`)) },
      { name: 'README.txt', data: enc(readme) }]), 'application/zip');
    status(`Saved the parametric EQ (both speakers, and left / right): within ${Math.max(L.worst, R.worst).toFixed(1)} dB of the full correction. README.txt says which file goes where.`);
  }, 30);
}

// Players with a convolution engine (Roon, JRiver, foobar2000, HQPlayer): one stereo filter per common rate, the
// preamp and time alignment inside, in a folder, zipped (Roon loads the zip as it is)
function exportConv(d, name, linear, kind) {
  if (!d) return;
  const files = [], rates = [44100, 48000, 88200, 96000, 176400, 192000], dir = fileName(name);
  for (const fs of rates) {
    const ch = [0, 1].map((s) => {
      const side = d.sides[s] ? s : (s ? 0 : 1);
      const n = W.x.room_corr_fir(side, fs, linear ? 1 : 0);
      return W.f32(W.x.room_fir_ptr(), n).slice();
    });
    files.push({ name: `${dir}/${dir}-${fs}.wav`, data: wav(ch, fs) });
  }
  const readme = [`pump ROOM correction "${name}" for players with a convolution engine`, '',
    `${kind}. One stereo filter (32-bit float WAV) per sample rate: 44.1, 48, 88.2, 96, 176.4 and 192 kHz.`,
    `The preamp (${d.preamp.toFixed(2)} dB, headroom for the boosts) and the time alignment are inside the filters.`, '',
    'Roon: Settings of the zone > DSP Engine > Convolution > add this zip as it is. Roon picks the filter for each rate.',
    'JRiver Media Center: DSP Studio > Convolution > choose the WAV for your output rate (or use JRiver\'s resampling).',
    'foobar2000: add the Convolver DSP and load the WAV for your output rate.',
    'HQPlayer: Convolution > load the WAV for the rate it outputs.', '',
    'They include the volume cut: turning convolution off in the player plays louder by that much.', ''].join('\n');
  files.push({ name: `${dir}/README.txt`, data: new TextEncoder().encode(readme) });
  download(`${dir}-convolution.zip`, zip(files), 'application/zip');
  status('Saved the convolution filters: Roon takes the zip as it is; README.txt has the other players.');
}

// the EXPORT menus: open on click, close on a choice, a click elsewhere or Esc
function initMenus() {
  for (const id of ['cExportMenu', 'hpExportMenu']) {
    const m = $(id), list = m.querySelector('.menu-list'), btn = m.querySelector('button');
    btn.onclick = (e) => { e.stopPropagation(); list.hidden = !list.hidden; };
    list.addEventListener('click', () => { list.hidden = true; });
  }
  document.addEventListener('click', () => document.querySelectorAll('.menu-list').forEach((l) => { l.hidden = true; }));
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') document.querySelectorAll('.menu-list').forEach((l) => { l.hidden = true; }); });
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

// ---------------------------------------------------------------------------------------------------------------------
// updates: every build carries its commit and time (room/build.sh); CI publishes the newest one's as version.json
// next to the web app. A newer one shows a bar: on the web UPDATE reloads the page, in the desktop app (or the page
// opened from a file) it opens the download page.

const UPDATE_URL = 'https://matthettich.github.io/pump/version.json';
const DOWNLOAD_URL = 'https://github.com/matthettich/pump/releases/tag/latest';
const U = { checked: 0, latest: null, dismissed: '' };

async function checkUpdate(force = false) {
  if (S.sim || !ROOM_BUILD || !ROOM_BUILD.time || !window.fetch) return;
  const now = Date.now();
  if (!force && now - U.checked < 10 * 60 * 1000) return;
  U.checked = now;
  try {
    const r = await fetch(UPDATE_URL + '?t=' + now, { cache: 'no-store' });
    if (!r.ok) return;
    const v = await r.json();
    if (v && typeof v.id === 'string' && +v.time > ROOM_BUILD.time && v.id !== ROOM_BUILD.id) { U.latest = v; showUpdate(); }
  } catch (e) { /* offline: try again later */ }
}

function showUpdate() {
  const v = U.latest, web = location.protocol === 'https:';
  $('update').hidden = !v || U.dismissed === v.id;
  if (!v) return;
  $('updateText').textContent = web
    ? 'A new version of pump ROOM is ready.' + (S.unsaved ? ' SAVE your measurements first: updating reloads the page.' : '')
    : 'A new version of pump ROOM is out: download it and install it over this one.';
  $('updateGo').textContent = web ? 'UPDATE' : 'DOWNLOAD';
}

function doUpdate() {
  const v = U.latest;
  if (!v) return;
  if (location.protocol !== 'https:') { window.open(DOWNLOAD_URL, '_blank'); return; }
  if (S.busy) { status('Wait for the measurement to finish, then UPDATE.', true); return; }
  if (S.unsaved && !confirm('Measurements you haven\'t saved will be lost. Update now?')) return;
  setNoise(false, false);
  location.replace(location.pathname + '?v=' + encodeURIComponent(v.id) + location.hash);   // a new URL skips the cached page
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
  S.bt = recall('link') === 'bt';
  seg('link', (v) => { S.bt = v === 'bt'; store('link', v); showLink(); showCorrControls(); showBtLocks(); if (S.view === 'handheld') showHand(); draw(); });
  showLink();
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState !== 'visible') return;
    if (S.ctx) keepAwake(true);
    checkUpdate();
  });
  $('updateGo').onclick = doUpdate;
  $('updateLater').onclick = () => { if (U.latest) U.dismissed = U.latest.id; showUpdate(); };
  setTimeout(() => checkUpdate(true), 3000);
  setInterval(() => checkUpdate(), 30 * 60 * 1000);
  seg('chans', (v) => { S.chans = v; $('flip').disabled = !S.node || v !== 'LR'; if (S.noise) setNoise(true, false); });
  seg('len', (v) => { S.seconds = +v; });
  $('level').oninput = () => { S.level = +$('level').value; $('levelText').textContent = S.level + ' dB'; if (S.noise) setNoise(true, false); };
  $('noise').onclick = () => setNoise(!S.noise);
  $('flip').onclick = () => setFlip(!S.flip);
  $('measure').onclick = () => (S.busy ? stopMeasure() : measure());
  $('calLoad').onclick = () => $('calFile').click();
  $('calFile').onchange = () => { const f = $('calFile').files[0]; if (f) loadCal(f); $('calFile').value = ''; };
  $('calClear').onclick = clearCal;
  seg('view', (v) => {
    S.view = v;
    $('corrPanel').hidden = v !== 'correction';
    $('handPanel').hidden = v !== 'handheld';
    $('hpPanel').hidden = v !== 'headphones';
    $('smooth').style.visibility = v === 'correction' || v === 'handheld' || v === 'headphones' ? 'hidden' : '';
    if (v === 'headphones') showHp();
    if (v === 'handheld') showHand();
    draw();
  });
  seg('hPoints', (v) => { H.points = v === 'mix' || v === 'couch' ? v : +v; if (H.center && !H.running && !H.done.some(Boolean)) handCenterPattern(); else showHand(); });
  $('hCal').onclick = handFind;
  $('hCenter').onclick = handCenter;
  $('hStart').onclick = handStart;
  $('hRestart').onclick = () => { if (!S.busy) handReset(); showHand(); };
  initCorrection();
  initHeadphones();
  initMenus();
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
  window.pumpRoom = { S, W, H, fitPeq, fitCurve, handFind, handCenter, handStart, parseHeadphoneFile, designHeadphones, HP_TARGETS, filtersDb, measure, startAudio, applyCal, saveSession, openSession, draw, designCorrection, saveProfile, exportWindows, exportPi };   // for tests
}

init();
