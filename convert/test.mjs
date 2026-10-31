// pump CONVERT: real conversions with the local ffmpeg (node convert/test.mjs). Checks every format's result (rate,
// word length, tags read back), the resampler's quality (a 1 kHz tone keeps its level to 0.01 dB, 20 kHz to 0.1 dB
// at 44.1 kHz, a 30 kHz tone from a 96 kHz source doesn't alias into a 48 kHz result, the residue stays under
// -130 dBFS in float), the 16-bit dither (a TPDF noise floor where it belongs, no truncation distortion on a quiet
// tone), name templates and folders, the ID3 chunk on WAV and AIFF, and the probe parser.
import { createRequire } from 'module';
import fs from 'fs';
import os from 'os';
import path from 'path';
import { execFileSync, spawnSync } from 'child_process';
const require = createRequire(import.meta.url);
const E = require('./engine.js');
// PUMP_FFMPEG: the ffmpeg to test (CI tests the one the app ships)
const FF = process.env.PUMP_FFMPEG || 'ffmpeg';
E.setFfmpeg(FF);

let fails = 0;
const check = (ok, msg) => { if (!ok) fails++; console.log(`  ${ok ? 'ok' : 'FAIL'}: ${msg}`); };
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'pump-convert-'));
const ff = (args) => execFileSync(FF, ['-hide_banner', '-nostdin', '-loglevel', 'error', '-y', ...args]);

// decoded samples (left channel) as Float64Array
function decode(file, rate) {
  const raw = execFileSync(FF, ['-hide_banner', '-nostdin', '-loglevel', 'error', '-i', file, '-map', '0:a:0', '-f', 'f64le', '-ac', '2', '-ar', String(rate), '-'], { maxBuffer: 1 << 30 });
  const all = new Float64Array(raw.buffer, raw.byteOffset, raw.byteLength / 8);
  const L = new Float64Array(all.length / 2);
  for (let i = 0; i < L.length; ++i) L[i] = all[2 * i];
  return L;
}
// least-squares fit of sines at the given frequencies over the middle of x: amplitudes and the residual rms (dBFS)
function fit(x, rate, freqs) {
  const a = Math.floor(x.length * 0.2), b = Math.floor(x.length * 0.8);
  const n = b - a, k = freqs.length * 2;
  const basis = [];
  for (const f of freqs) { const c = new Float64Array(n), s = new Float64Array(n); for (let i = 0; i < n; ++i) { const w = 2 * Math.PI * f * (a + i) / rate; c[i] = Math.cos(w); s[i] = Math.sin(w); } basis.push(c, s); }
  const M = Array.from({ length: k }, () => new Float64Array(k + 1));
  for (let i = 0; i < k; ++i) { for (let j = 0; j < k; ++j) { let t = 0; for (let q = 0; q < n; ++q) t += basis[i][q] * basis[j][q]; M[i][j] = t; } let t = 0; for (let q = 0; q < n; ++q) t += basis[i][q] * x[a + q]; M[i][k] = t; }
  for (let c = 0; c < k; ++c) { let p = c; for (let r = c + 1; r < k; ++r) if (Math.abs(M[r][c]) > Math.abs(M[p][c])) p = r; [M[p], M[c]] = [M[c], M[p]]; for (let r = 0; r < k; ++r) if (r !== c) { const f = M[r][c] / M[c][c]; for (let q = c; q <= k; ++q) M[r][q] -= f * M[c][q]; } }
  const g = M.map((row, i) => row[k] / row[i]);
  let res = 0;
  for (let q = 0; q < n; ++q) { let y = x[a + q]; for (let i = 0; i < k; ++i) y -= g[i] * basis[i][q]; res += y * y; }
  const amps = freqs.map((f, i) => 20 * Math.log10(Math.hypot(g[2 * i], g[2 * i + 1]) + 1e-30));
  return { amps, residual: 10 * Math.log10(res / n + 1e-300) + 3.0103 };   // residual as the rms of a full-scale-sine-referenced dBFS
}

const caps = await E.capabilities();
console.log(`== pump CONVERT (ffmpeg ${caps.version}; SoX resampler ${caps.soxr ? 'yes' : 'NO'}; LAME ${caps.lame ? 'yes' : 'NO'}; Apple AAC ${caps.appleAac ? 'yes' : 'no'})`);
check(caps.ok && caps.soxr && caps.lame, 'ffmpeg with the SoX resampler and LAME');

// ---- sources: 24-bit / 96 kHz (1 kHz at -6 dBFS, 30 kHz at -20 dBFS), 32-bit float / 48 kHz (1 kHz and 20 kHz at -12)
const s96 = path.join(tmp, 'Song One.wav'), s48 = path.join(tmp, 'Song Two.wav'), quiet = path.join(tmp, 'Quiet.wav');
ff(['-f', 'lavfi', '-i', 'aevalsrc=0.5*sin(2*PI*1000*t)+0.1*sin(2*PI*30000*t)|0.5*sin(2*PI*1000*t)+0.1*sin(2*PI*30000*t):s=96000:d=4', '-c:a', 'pcm_s24le', s96]);
ff(['-f', 'lavfi', '-i', 'aevalsrc=0.25*sin(2*PI*1000*t)+0.25*sin(2*PI*20000*t)|0.25*sin(2*PI*1000*t)+0.25*sin(2*PI*20000*t):s=48000:d=4', '-c:a', 'pcm_f32le', s48]);
ff(['-f', 'lavfi', '-i', 'aevalsrc=0.00003*sin(2*PI*1000*t)|0.00003*sin(2*PI*1000*t):s=96000:d=4', '-c:a', 'pcm_f32le', quiet]);   // -90 dBFS

const srcs = [];
for (const f of [s96, s48]) srcs.push(await E.probe(f));
check(srcs[0].rate === 96000 && srcs[0].bits === 24 && !srcs[0].float && Math.abs(srcs[0].duration - 4) < 0.01, `probe: ${srcs[0].bits}-bit ${srcs[0].rate} Hz, ${srcs[0].duration} s`);
check(srcs[1].rate === 48000 && srcs[1].float && srcs[1].bits === 32, `probe: float ${srcs[1].rate} Hz`);
srcs[0].title = 'Song One'; srcs[0].isrc = 'USX9P2600001';
srcs[1].title = 'Sóng Twö'; srcs[1].isrc = 'USX9P2600002';

const out = path.join(tmp, 'out');
const art = path.join(tmp, 'cover.png');
ff(['-f', 'lavfi', '-i', 'color=c=0x5CFFB4:s=64x64:d=1', '-frames:v', '1', art]);
const versions = [
  { name: 'HD', format: 'wav', rate: 96000, bits: 24, folder: out, subfolder: 'WAV {bits}-{khz}', name_tpl: '{track:02} {artist} - {title}' },
  { name: 'CD', format: 'wav', rate: 44100, bits: 16, dither: 'tpdf', folder: out, subfolder: 'WAV {bits}-{khz}', name_tpl: '{track:02} {title}' },
  { name: 'Float', format: 'wav', rate: 48000, bits: 32, folder: out, subfolder: 'WAV float {khz}', name_tpl: '{track:02} {title}' },
  { name: 'AIFF', format: 'aiff', rate: 0, bits: 24, folder: out, subfolder: 'AIFF {bits}-{khz}', name_tpl: '{track:02} {title}' },
  { name: 'FLAC', format: 'flac', rate: 44100, bits: 16, dither: 'shaped', folder: out, subfolder: 'FLAC', name_tpl: '{track:02} {title}' },
  { name: 'MP3', format: 'mp3', mp3: 'cbr320', folder: out, subfolder: 'MP3', name_tpl: '{track:02} {title}' },
  { name: 'AAC', format: 'aac', aac: 256, folder: out, subfolder: 'AAC', name_tpl: '{track:02} {title}' },
  { name: 'off', format: 'wav', enabled: false, folder: out },
];
const meta = { artist: 'wake', album: 'Test Album', year: '2026', genre: 'Electronic', label: 'pump records', copyright: '(c) 2026 wake',
  comment: 'Mastered: {spec}', custom: [{ key: 'SPEC', value: '{bitdepth} / {khz} kHz' }, { key: 'MASTERING ENGINEER', value: 'Hettich' }] };
const jobs = E.plan(srcs, versions, meta);
check(jobs.length === 14, `${jobs.length} jobs (7 versions x 2 songs, the disabled one skipped)`);
check(jobs[0].file === path.join(out, 'WAV 24-96', '01 wake - Song One.wav'), `name and folder from the templates: ${path.relative(out, jobs[0].file)}`);
const mp3Job = jobs.find((j) => j.version.format === 'mp3' && j.source === srcs[0]);
check(mp3Job.out.rate === 48000, `MP3 from a 96 kHz source: ${mp3Job.out.rate} Hz (lossy stays at or under 48 kHz)`);
check(jobs.find((j) => j.version.name === 'AIFF' && j.source === srcs[1]).out.rate === 48000, 'AIFF "keep rate" keeps the source rate');

// run them all
let allOk = true;
for (const j of jobs)
{
  const r = await E.runJob(j, caps, { artwork: art });
  if (!r.ok) { allOk = false; console.log('     ', path.relative(out, j.file), r.error); }
}
check(allOk, 'every job converted and passed its check');
for (const j of jobs)
{
  const p = await E.probe(j.file);
  if (p.rate !== j.out.rate) { allOk = false; console.log('     rate', j.file, p.rate); }
}
check(allOk && !fs.readdirSync(path.join(out, 'MP3')).some((f) => f.endsWith('.part')), 'results have the planned rates; no .part files left');

// ---- tags
{
  const pm = await E.probe(path.join(out, 'MP3', '01 Song One.mp3'));
  check(pm.tags.title === 'Song One' && pm.tags.artist === 'wake' && pm.tags.album === 'Test Album' && (pm.tags.tsrc === 'USX9P2600001' || pm.tags.isrc === 'USX9P2600001')
        && /320 kbps CBR \/ 48 kHz/.test(pm.tags.comment || '') && /MP3/.test(pm.tags.spec || '') === false && (pm.tags.spec || '').length > 0,
        `MP3 tags: ${JSON.stringify(pm.tags)}`);
  const pf = await E.probe(path.join(out, 'FLAC', '02 Sóng Twö.flac'));
  check(pf.tags.title === 'Sóng Twö' && pf.tags.isrc === 'USX9P2600002' && pf.tags.spec === '16-bit / 44.1 kHz' && pf.tags['mastering engineer'] === 'Hettich' && pf.tags.label === 'pump records',
        `FLAC tags: ${JSON.stringify(pf.tags)}`);
  const pa = await E.probe(path.join(out, 'AAC', '01 Song One.m4a'));
  check(pa.tags.title === 'Song One' && pa.tags.artist === 'wake' && /256 kbps/.test(pa.tags.comment || ''), `AAC tags (with artwork: the iTunes ones): ${JSON.stringify(pa.tags)}`);
  const w = E.readId3Chunk(path.join(out, 'WAV 24-96', '01 wake - Song One.wav'));
  check(w.TIT2 === 'Song One' && w.TPE1 === 'wake' && w.TSRC === 'USX9P2600001' && w['TXXX:SPEC'] === '24-bit / 96 kHz' && w.COMM === 'Mastered: 24-bit / 96 kHz' && w.APIC > 0,
        `WAV ID3 chunk: ${JSON.stringify(w)}`);
  const wi = await E.probe(path.join(out, 'WAV 24-96', '01 wake - Song One.wav'));
  check(wi.tags.title === 'Song One' && wi.tags.artist === 'wake', `WAV INFO list read by ffmpeg: ${JSON.stringify(wi.tags)}`);
  const ai = E.readId3Chunk(path.join(out, 'AIFF 24-48', '02 Sóng Twö.aif'));
  check(ai.TIT2 === 'Sóng Twö' && ai['TXXX:MASTERING ENGINEER'] === 'Hettich', `AIFF ID3 chunk (UTF-16 title): ${JSON.stringify(ai)}`);
  const arts = ['FLAC/01 Song One.flac', 'MP3/01 Song One.mp3', 'AAC/01 Song One.m4a'].map((f) =>
    /attached pic/.test(spawnSync(FF, ['-hide_banner', '-nostdin', '-i', path.join(out, f)]).stderr.toString()));
  check(arts.every(Boolean), `artwork in FLAC, MP3 and AAC: ${arts.join(' ')}`);
}

// ---- quality: resampling
{
  const x = decode(path.join(out, 'WAV float 48', '01 Song One.wav'), 48000);   // 96 kHz 24-bit -> 48 kHz float
  const r = fit(x, 48000, [1000]);
  check(Math.abs(r.amps[0] - 20 * Math.log10(0.5)) < 0.01, `96 -> 48 kHz: 1 kHz at ${r.amps[0].toFixed(3)} dBFS (source ${(20 * Math.log10(0.5)).toFixed(3)})`);
  check(r.residual < -125, `96 -> 48 kHz: the 30 kHz tone gone, nothing aliased: residue ${r.residual.toFixed(1)} dBFS`);
  const y = decode(path.join(out, 'WAV float 48', '02 Sóng Twö.wav'), 48000);   // float 48 -> float 48: untouched
  const r2 = fit(y, 48000, [1000, 20000]);
  check(r2.residual < -140, `48 kHz float to 48 kHz float: residue ${r2.residual.toFixed(1)} dBFS`);
  const z = decode(path.join(out, 'AIFF 24-48', '02 Sóng Twö.aif'), 48000);
  const r3 = fit(z, 48000, [1000, 20000]);
  check(Math.abs(r3.amps[1] - r3.amps[0]) < 0.01 && r3.residual < -135, `float -> 24-bit: 20 kHz vs 1 kHz ${(r3.amps[1] - r3.amps[0]).toFixed(3)} dB, residue ${r3.residual.toFixed(1)} dBFS (24-bit floor)`);
}
// ---- quality: 48 -> 44.1 kHz passband and 16-bit dither
{
  const x = decode(path.join(out, 'WAV 16-44.1', '02 Sóng Twö.wav'), 44100);
  const r = fit(x, 44100, [1000, 20000]);
  check(Math.abs(r.amps[1] - r.amps[0]) < 0.1, `48 -> 44.1 kHz: 20 kHz vs 1 kHz ${(r.amps[1] - r.amps[0]).toFixed(3)} dB (flat passband)`);
  // TPDF on 16 bits: quantization (1/12 LSB^2) plus dither (1/6 LSB^2) = 0.5 LSB rms = -96.3 dBFS, -93.3 against a full-scale sine
  check(r.residual > -95 && r.residual < -91.5, `16-bit TPDF: noise floor ${r.residual.toFixed(1)} dB under a full-scale sine (theory -93.3)`);
}
{
  // a -90 dBFS tone to 16 bits: dithered, its level holds and the residue is noise (truncation would distort it)
  const v = { name: 'q', format: 'wav', rate: 44100, bits: 16, dither: 'tpdf', folder: out, subfolder: 'quiet', name_tpl: '{title}' };
  const vu = { ...v, dither: 'off', subfolder: 'quiet-undithered' };
  const qs = await E.probe(quiet); qs.title = 'q';
  for (const ver of [v, vu]) { const [j] = E.plan([qs], [ver], {}); const r = await E.runJob(j, caps); if (!r.ok) console.log('     ', r.error); }
  const d = fit(decode(path.join(out, 'quiet', 'q.wav'), 44100), 44100, [1000, 3000, 5000]);
  const u = fit(decode(path.join(out, 'quiet-undithered', 'q.wav'), 44100), 44100, [1000, 3000, 5000]);
  check(Math.abs(d.amps[0] - (-90.46)) < 0.5 && d.amps[1] < -125, `-90 dBFS tone to 16-bit with dither: ${d.amps[0].toFixed(2)} dBFS, 3rd harmonic ${d.amps[1].toFixed(1)} dBFS`);
  check(u.amps[1] > d.amps[1] + 10, `without dither the 3rd harmonic rises to ${u.amps[1].toFixed(1)} dBFS (why the dither is on)`);
}
// ---- templates and names
{
  check(E.safeName('a/b:c*?"<>|. ') === 'a_b_c______', `safe name: ${E.safeName('a/b:c*?"<>|. ')}`);
  check(E.fill('{track:02} {missing} {khz}', { track: 3, khz: '44.1' }) === '03 {missing} 44.1', 'template tokens and padding');
  const p = E.parseProbe(`Input #0, wav, from 'x.wav':\n  Metadata:\n    title           : Hello: World\n    artist          : wake\n  Duration: 00:03:21.50, bitrate: 4608 kb/s\n  Stream #0:0: Audio: pcm_s24le ([1][0][0][0] / 0x0001), 96000 Hz, stereo, s32 (24 bit), 4608 kb/s\n`);
  check(p.rate === 96000 && p.bits === 24 && p.channels === 2 && Math.abs(p.duration - 201.5) < 1e-9 && p.tags.title === 'Hello: World', 'probe parser');
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log(fails ? `CONVERT: ${fails} FAILED` : 'CONVERT: all passed');
process.exit(fails ? 1 : 0);
