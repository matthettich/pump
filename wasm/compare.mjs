// Runs pump.wasm the way Thunder Plus's track-effect worklet does (128-sample blocks, values staged by symbol) on the
// same input as wasm/compare.cpp (the native build of the same wrapper), and compares the two outputs.
//   node wasm/compare.mjs build/wasm [tests/data/mix.f32] [seconds]    (needs build/pump_compare, see compare.cpp)
// Prints, per effect and preset: latency, peak and RMS of the output, the largest difference and the difference's RMS
// relative to the output (dB), plus how long the wasm took per second of audio.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { performance } from 'node:perf_hooks';
const dir = process.argv[2] || 'build/wasm', input = process.argv[3] || 'tests/data/mix.f32', secs = +(process.argv[4] || 10);
(0, eval)(fs.readFileSync(path.join(dir, 'pump-meta.js'), 'utf8'));
const META = globalThis.PUMP_META;
const z = () => 0;
const { instance } = await WebAssembly.instantiate(fs.readFileSync(path.join(dir, 'pump.wasm')), { wasi_snapshot_preview1: { fd_close: z, fd_seek: z, fd_write: z, proc_exit: z, environ_get: z, environ_sizes_get: z } });
const x = instance.exports; x._initialize();
const inBuf = new Float32Array(fs.readFileSync(input).buffer.slice(0));
const sr = 48000, frames = Math.min(inBuf.length / 2, secs * sr) | 0;
const F = (p, n) => new Float32Array(x.memory.buffer, p, n);
function runWasm(type, preset){
  const fx = META.fx[META.types[type]], ps = fx.params;
  const st = F(x.pump_stage(), 512);
  ps.forEach((p, i) => { st[i] = p.def; });
  if (preset >= 0) for (const [s, v] of Object.entries(fx.presets[preset].v)) { const i = ps.findIndex(p => p.s === s); if (i >= 0) st[i] = v; }
  const h = x.pump_new(type, sr);
  const change = ps.findIndex(p => (p.flags & 1) && !(p.flags & (2 | 4 | 16)) && !p.ch);
  const out = new Float32Array(frames * 2);
  const t0 = performance.now();
  for (let off = 0; off < frames; off += 128){
    const n = Math.min(128, frames - off);
    if (change >= 0 && off <= frames / 2 && off + 128 > frames / 2){ const p = ps[change];
      const v = p.scale === 'log' ? p.min * Math.pow(p.max / p.min, 0.3) : p.scale === 'pow' ? p.min + (p.max - p.min) * Math.pow(0.3, p.skew) : p.min + (p.max - p.min) * 0.3;
      F(x.pump_values(h), ps.length)[change] = v; x.pump_commit(h); }
    const L = F(x.pump_io(0), n), R = F(x.pump_io(1), n);
    for (let i = 0; i < n; i++){ L[i] = inBuf[(off + i) * 2]; R[i] = inBuf[(off + i) * 2 + 1]; }
    x.pump_process(h, n, 0);
    const L2 = F(x.pump_io(0), n), R2 = F(x.pump_io(1), n);   // memory may have grown
    for (let i = 0; i < n; i++){ out[(off + i) * 2] = L2[i]; out[(off + i) * 2 + 1] = R2[i]; }
  }
  const ms = performance.now() - t0, lat = x.pump_latency(h);
  x.pump_free(h);
  return { out, ms, lat };
}
const tmpIn = path.join(dir, 'cmp_in.f32'), tmpOut = path.join(dir, 'cmp_native.f32');
fs.writeFileSync(tmpIn, Buffer.from(inBuf.buffer, 0, frames * 8));
const db = v => (20 * Math.log10(Math.max(v, 1e-12))).toFixed(1);
const which = process.env.PRESETS ? process.env.PRESETS.split(',') : null;
let worst = -999;
for (let type = 0; type < META.types.length; type++){
  const fx = META.fx[META.types[type]];
  const list = [-1, ...fx.presets.map((_, i) => i).filter(i => i > 0 && (which ? which.includes(fx.presets[i].n) : (i % 7 === 1)))];
  for (const preset of list){
    const w = runWasm(type, preset);
    const nat = execFileSync('build/pump_compare', [String(type), String(preset), tmpIn, tmpOut, String(secs)]).toString().trim().split(' ');
    const o = new Float32Array(fs.readFileSync(tmpOut).buffer.slice(0));
    let mx = 0, se = 0, so = 0, pk = 0;
    for (let i = 0; i < o.length; i++){ const d = Math.abs(o[i] - w.out[i]); if (d > mx) mx = d; se += d * d; so += o[i] * o[i]; if (Math.abs(o[i]) > pk) pk = Math.abs(o[i]); }
    const rel = 10 * Math.log10(se / Math.max(so, 1e-30) + 1e-30);
    worst = Math.max(worst, rel);
    console.log(`${fx.name.padEnd(5)} ${(preset < 0 ? 'defaults' : fx.presets[preset].n).slice(0, 34).padEnd(34)} lat ${String(w.lat).padStart(5)} (native ${nat[0].padStart(5)})  peak ${db(pk)} dB  max diff ${mx.toExponential(2)}  diff/out ${rel.toFixed(1)} dB  wasm ${(w.ms / secs).toFixed(1)} ms per s`);
  }
}
console.log('worst difference relative to the output: ' + worst.toFixed(1) + ' dB');
fs.unlinkSync(tmpIn); fs.unlinkSync(tmpOut);
