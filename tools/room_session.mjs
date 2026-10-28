// Looks at a pump ROOM session file (SAVE in the app: pump-room-*.json) with the app's own code: opens it in the built
// page (bin/pump-room.html, `make room-page`) in headless Chromium and writes, into an output folder:
//   summary.txt          every run and speaker: arrival, SNR, peak, polarity; L/R timing and level; the correction
//                        (preamp, delays, low-end limit, largest cut / boost) with the session's own settings
//   responses.csv        every speaker's response, 1/12-octave smoothed, 20 Hz..20 kHz (mic calibration applied)
//   correction.csv       the averaged measurement, target, correction and predicted result per speaker
//   response.png, impulse.png, correction.png   the app's three views
// Needs Playwright:  node tools/room_session.mjs <session.json> [out folder] [path/to/pump-room.html]
import { chromium } from 'playwright';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const session = process.argv[2];
if (!session) { console.log('usage: node tools/room_session.mjs <session.json> [out folder] [pump-room.html]'); process.exit(1); }
const out = resolve(process.argv[3] || 'build/room-session');
const pageFile = resolve(process.argv[4] || 'bin/pump-room.html');
mkdirSync(out, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } });
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto('file://' + pageFile);
await page.setInputFiles('#openFile', resolve(session));
await page.waitForFunction(() => window.pumpRoom.S.runs.length > 0 || /Could not open/.test(document.getElementById('status').textContent), null, { timeout: 30000 });
const status = await page.textContent('#status');
if (/Could not open/.test(status)) { console.log(status); await browser.close(); process.exit(1); }

await page.click('#smooth button[data-v="12"]');
const data = await page.evaluate(() => {
  const P = window.pumpRoom, S = P.S, W = P.W;
  const runs = S.runs.map((r) => ({
    name: r.name, time: r.time, fs: r.fs, seconds: r.seconds, level: r.level, noiseDb: r.noiseDb, use: r.use,
    dtMs: r.dtMs, levelDiff: r.levelDiff, notes: r.notes,
    ch: r.ch.map((c) => ({ side: c.side, arrivalMs: c.arrival / c.fs * 1000, snr: c.snr, peakDbFS: c.peakDbFS, signalDb: c.signalDb, polarity: c.polarity, id: c.id })),
  }));
  // responses on the app's grid (1/96 octave, read every 8th = 1/12), with the current smoothing and calibration
  const resp = [];
  for (const r of S.runs) for (const c of r.ch) {
    const p = W.x.room_response(c.id, S.windowMs, 20, 20000, 96, 1 / 12, S.cal ? 1 : 0);
    const k = W.x.room_grid_size();
    resp.push({ label: `${r.name} ${c.side}`, db: Array.from(W.f32(p, k)) });
  }
  const d = P.designCorrection();
  return {
    runs, resp, cal: S.cal ? { name: S.cal.name, points: S.cal.points, lo: S.cal.lo, hi: S.cal.hi } : null,
    corr: S.corr, windowMs: S.windowMs,
    design: d && { preamp: d.preamp, positions: d.positions, sides: d.sides.map((s) => s && { guard: s.guard, delay: s.delay,
      measured: Array.from(s.measured), target: Array.from(s.target), correction: Array.from(s.correction), result: Array.from(s.result) }) },
  };
});

const fmt = (v, d = 2) => (v === undefined || v === null || !isFinite(v) ? '-' : v.toFixed(d));
const lines = [`pump ROOM session ${session}`, `calibration: ${data.cal ? `${data.cal.name} (${data.cal.points} points, ${fmt(data.cal.lo, 0)}..${fmt(data.cal.hi, 0)} Hz)` : 'none'}`,
  `window ${data.windowMs} ms; correction settings ${JSON.stringify(data.corr)}`, ''];
for (const r of data.runs) {
  lines.push(`${r.name}  (${r.time}, ${r.fs} Hz, ${r.seconds} s sweeps at ${r.level} dBFS, noise ${fmt(r.noiseDb, 1)} dBFS${r.use ? '' : ', not in the correction'})`);
  for (const c of r.ch)
    lines.push(`  ${c.side}: arrival ${fmt(c.arrivalMs, 3)} ms, SNR ${fmt(c.snr, 1)} dB, sweep at the mic ${fmt(c.signalDb, 1)} dBFS, peak ${fmt(c.peakDbFS, 1)} dBFS, polarity ${c.polarity > 0 ? '+' : '-'}`);
  if (r.ch.length === 2) lines.push(`  right - left: ${fmt(r.dtMs, 3)} ms (${fmt(r.dtMs / 1000 * 34300, 1)} cm), level ${fmt(r.levelDiff, 2)} dB`);
  for (const n of r.notes || []) lines.push(`  note: ${n}`);
}
if (data.design) {
  lines.push('', `correction from ${data.design.positions} position(s): preamp ${fmt(data.design.preamp)} dB`);
  data.design.sides.forEach((s, i) => {
    if (!s) return;
    lines.push(`  ${i ? 'R' : 'L'}: delay ${fmt(s.delay, 3)} ms, no boost below ${s.guard > 0 ? fmt(s.guard, 0) + ' Hz' : '-'}, cut ${fmt(Math.min(...s.correction), 1)} / boost ${fmt(Math.max(...s.correction), 1)} dB`);
  });
}
if (errors.length) lines.push('', 'page errors: ' + errors.join('; '));
writeFileSync(join(out, 'summary.txt'), lines.join('\n') + '\n');

const f96 = (i) => 20 * Math.pow(2, i / 96);
let csv = 'hz,' + data.resp.map((r) => JSON.stringify(r.label)).join(',') + '\n';
for (let i = 0; i < data.resp[0].db.length; i += 8) csv += `${f96(i).toFixed(2)},` + data.resp.map((r) => r.db[i].toFixed(2)).join(',') + '\n';
writeFileSync(join(out, 'responses.csv'), csv);
if (data.design) {
  const f48 = (i) => Math.min(20000, 20 * Math.pow(2, i / 48));
  const cols = [];
  data.design.sides.forEach((s, i) => { if (s) for (const k of ['measured', 'target', 'correction', 'result']) cols.push({ h: `${i ? 'R' : 'L'}_${k}`, v: s[k] }); });
  let c2 = 'hz,' + cols.map((c) => c.h).join(',') + '\n';
  for (let i = 0; i < cols[0].v.length; ++i) c2 += `${f48(i).toFixed(2)},` + cols.map((c) => c.v[i].toFixed(2)).join(',') + '\n';
  writeFileSync(join(out, 'correction.csv'), c2);
}
for (const v of ['response', 'impulse', 'correction']) {
  await page.click(`#view button[data-v="${v}"]`);
  await page.waitForTimeout(250);
  await page.screenshot({ path: join(out, `${v}.png`), fullPage: true });
}
await browser.close();
console.log(lines.join('\n'));
console.log(`\nwritten to ${out}`);
