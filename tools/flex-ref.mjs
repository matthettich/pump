#!/usr/bin/env node
// Reference renders for pump FLEX's parity test (tests/test_flex.cpp).
//
//   node tools/flex-ref.mjs <path to thunder-plus index.html> [out dir]      (out dir defaults to $TMPDIR/flex-ref)
//
// It cuts `class FlexCore` out of the page, as the page's own tests do, and runs it over a fixed deterministic input
// (a 120 BPM drum-like stereo signal: kick, snare, hats and a sine, 48 kHz, four bars, the beat clock starting at 0) for
// every pattern, FLX 01-FF (flx 1-255) and FLY 01-FF (flx 257-511). Two more cases follow: the pattern changing from block to
// block (128 BPM, the clock running through the 16-beat wrap), and the transport stopping and restarting. The output is
// float32 interleaved stereo, written to the out dir (about 1.5 GB for all patterns: it is a temp folder, never committed).
//
// The host (not the page) gives the beat clock here: one value per block, as a plugin host does, with the tempo exact;
// the page's own clock estimation (clkSpb) is replaced by that tempo. Everything else is the page's code, untouched.
//
// Files: input.f32, ref_<flx, 3 digits>.f32, case_change.f32, case_stop.f32, names.txt (flx <tab> name; FLX names 1-16 as
// the page lists them, then FLX 17-255 and FLY 257-511), cases.txt (what the two cases were).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const src = process.argv[2];
if (!src) { console.error('usage: node tools/flex-ref.mjs <thunder-plus index.html> [out dir]'); process.exit(2); }
const out = process.argv[3] || path.join(process.env.TMPDIR || os.tmpdir(), 'flex-ref');
fs.mkdirSync(out, { recursive: true });

const html = fs.readFileSync(src, 'utf8');
const m = html.match(/class FlexCore \{[\s\S]*?\n\}\n/);
if (!m) { console.error('class FlexCore not found in ' + src); process.exit(1); }
// clkSpb stands in for the page's clock estimator: the host's tempo, exact (set per render)
const mkCore = host => { const FC = new Function('clkSpb', m[0] + '\nreturn FlexCore;')((o) => host.spb); return FC; };

const SR = 48000, BLOCK = 512, BARS = 4;
const NS = SR * 2 * BARS;                  // 4 bars at 120 BPM = 8 s = 384000 samples = 750 blocks of 512

// ---- the input: kick on the beat, snare on 2 and 4, hats on the 8ths, a sine that moves; noise from a fixed LCG ----
function makeInput(ns) {
  const L = new Float32Array(ns), R = new Float32Array(ns);
  let seed = 12345; const rnd = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296 * 2 - 1; };
  const beatS = SR / 2, eighthS = SR / 4;
  for (let i = 0; i < ns; i++) {
    const t = i / SR, inBeat = i % beatS, inEighth = i % eighthS, beat = Math.floor(i / beatS) % 4;
    let l = 0.25 * Math.sin(2 * Math.PI * 220 * t) * (0.6 + 0.4 * Math.sin(2 * Math.PI * 0.5 * t)), r = 0.2 * Math.sin(2 * Math.PI * 330 * t + 1);
    const kt = inBeat / SR; if (kt < 0.12) { const e = Math.exp(-kt * 30); const k = 0.7 * e * Math.sin(2 * Math.PI * (50 + 120 * Math.exp(-kt * 40)) * kt); l += k; r += k; }
    if ((beat & 1) && inBeat < SR * 0.15) { const e = Math.exp(-inBeat / SR * 28) * 0.35; const n1 = rnd() * e, n2 = rnd() * e; l += n1; r += n2; }
    if (inEighth < SR * 0.03) { const e = Math.exp(-inEighth / SR * 160) * 0.25; l += rnd() * e; r += rnd() * e; }
    if (i % (beatS * 4) === 0) { l += 0.3; r -= 0.3; }   // a click on every bar line
    L[i] = l; R[i] = r;
  }
  return [L, R];
}
const [IL, IR] = makeInput(NS);
const writeF32 = (name, L, R) => { const b = new Float32Array(L.length * 2); for (let i = 0; i < L.length; i++) { b[2 * i] = L[i]; b[2 * i + 1] = R[i]; } fs.writeFileSync(path.join(out, name), Buffer.from(b.buffer)); };
writeF32('input.f32', IL, IR);

// ppq at a sample: the same expression as the C++ test's
const ppqAt = (start, bpm, s) => start + s * bpm / 60 / SR;

// render: flxAt(block) -> flx value; playAt(block) -> bool (host playing); bpm; start beat
function render(flxAt, playAt, bpm, start, ns = NS) {
  const host = { spb: SR * 60 / bpm };
  const core = new (mkCore(host))(SR);
  const L = IL.slice(0, ns), R = IR.slice(0, ns), dp = { mix: 1, xf: 5, wait: 0, pat: 0 };
  for (let b = 0, o = 0; o < ns; b++, o += BLOCK) {
    const n = Math.min(BLOCK, ns - o), bp = new Float64Array(n);
    bp[0] = playAt(b) ? ppqAt(start, bpm, o) % 16 : -1; bp[1] = bp[0];   // (the page reads the first value; two values mean a clock is there)
    core.process(L.subarray(o, o + n), R.subarray(o, o + n), n, dp, { flx: flxAt(b), bp });
  }
  return [L, R];
}

const names = [];
const T0 = Date.now();
const FC = mkCore({ spb: SR / 2 });
const NAMES = FC.NAMES, NAMES2 = FC.NAMES2;
for (let k = 1; k <= 511; k++) {
  if (k === 256) continue;
  names.push(k + '\t' + (k <= 255 ? NAMES[k] : NAMES2[k - 256]));
  const [L, R] = render(() => k, () => true, 120, 0);
  writeF32('ref_' + String(k).padStart(3, '0') + '.f32', L, R);
  if (k % 50 === 0) console.log('flx ' + k + ' (' + ((Date.now() - T0) / 1000).toFixed(0) + ' s)');
}
fs.writeFileSync(path.join(out, 'names.txt'), names.join('\n') + '\n');

// ---- case: the pattern changes while it plays (128 BPM, the clock starts at beat 13.3 and wraps at 16) ----
// block b of 512 samples; the schedule is repeated in tests/test_flex.cpp (kChange)
const CHANGE = [[0, 0], [60, 0x1A], [140, 0x7A], [220, 0xB1], [300, 256 + 0x23], [380, 256 + 0x5C], [460, 5], [540, 0xF3], [620, 256 + 0xE2], [700, 0]];
const flxChange = b => { let f = 0; for (const [s, v] of CHANGE) if (b >= s) f = v; return f; };
{ const [L, R] = render(flxChange, () => true, 128, 13.3); writeF32('case_change.f32', L, R); }
// ---- case: the transport stops (blocks 100-179) and restarts somewhere else (the host jumped) ----
// FLX 0x2B (a stutter) playing throughout; while stopped the signal passes straight through; it restarts at beat 9.1
{
  const host = { spb: SR * 60 / 120 };
  const core = new (mkCore(host))(SR);
  const L = IL.slice(), R = IR.slice(), dp = { mix: 1, xf: 5, wait: 0, pat: 0 };
  for (let b = 0, o = 0; o < NS; b++, o += BLOCK) {
    const n = Math.min(BLOCK, NS - o), bp = new Float64Array(n);
    const playing = b < 100 || b >= 180;
    bp[0] = !playing ? -1 : b < 100 ? ppqAt(0, 120, o) % 16 : ppqAt(9.1, 120, o - 180 * BLOCK) % 16; bp[1] = bp[0];
    core.process(L.subarray(o, o + n), R.subarray(o, o + n), n, dp, { flx: 0x2B, bp });
  }
  writeF32('case_stop.f32', L, R);
}
fs.writeFileSync(path.join(out, 'cases.txt'),
  'change: 128 BPM, start beat 13.3, flx by block (from block, flx): ' + JSON.stringify(CHANGE) + '\n' +
  'stop: 120 BPM, FLX 2B, playing from beat 0; stopped in blocks 100-179; restarts at beat 9.1 (block 180)\n');
console.log('wrote ' + (names.length) + ' patterns and 2 cases to ' + out + ' in ' + ((Date.now() - T0) / 1000).toFixed(0) + ' s');
