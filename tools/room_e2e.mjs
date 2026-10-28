// End-to-end check of the built pump ROOM page (bin/pump-room.html, `make room-page`) in headless Chromium, with the
// page's simulated room (#sim) standing in for the speakers and mic: a measurement of both speakers, the timing and
// level between them, a calibration file, and a session saved and opened again.
// Needs Playwright:  node tools/room_e2e.mjs [path/to/pump-room.html] [screenshot folder]
import { chromium } from 'playwright';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

const page_file = resolve(process.argv[2] || 'bin/pump-room.html');
const shots = process.argv[3] || null;
let fails = 0;
const check = (ok, what) => { console.log(`  ${ok ? 'ok' : 'FAIL'}: ${what}`); if (!ok) ++fails; };

const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto('file://' + page_file + '#sim');
await page.click('#start');
await page.waitForFunction(() => !document.querySelector('#measure').disabled, null, { timeout: 15000 });
await page.click('#len button[data-v="2"]');
await page.click('#measure');
await page.waitForFunction(() => window.pumpRoom.S.runs.length === 1 && !window.pumpRoom.S.busy, null, { timeout: 60000 });
const run = await page.evaluate(() => {
  const r = window.pumpRoom.S.runs[0];
  return { fs: r.fs, dt: r.dtMs, ld: r.levelDiff, ch: r.ch.map((c) => ({ side: c.side, arrival: c.arrival, snr: c.snr, pol: c.polarity })) };
});
// the simulation: 12 ms of latency, the left speaker 2.9 m away, the right 3.1 m
const expect = (m) => Math.round(0.012 * run.fs) + Math.round(m / 343 * run.fs);
check(run.ch.length === 2, 'both speakers measured');
check(Math.abs(run.ch[0].arrival - expect(2.9)) < 0.6, `left arrives at ${run.ch[0].arrival.toFixed(2)} samples (${expect(2.9)})`);
check(Math.abs(run.ch[1].arrival - expect(3.1)) < 0.6, `right arrives at ${run.ch[1].arrival.toFixed(2)} samples (${expect(3.1)})`);
check(Math.abs(run.dt - (expect(3.1) - expect(2.9)) / run.fs * 1000) < 0.02, `right ${run.dt.toFixed(3)} ms later`);
check(run.ch.every((c) => c.snr > 40 && c.pol === 1), `SNR ${run.ch.map((c) => c.snr.toFixed(0)).join(' / ')} dB, polarity +`);
check(Math.abs(run.ld) < 1.5, `level difference ${run.ld.toFixed(2)} dB`);
check(/later/.test(await page.textContent('.verdict')), 'timing shown');
if (shots) await page.screenshot({ path: join(shots, 'room-response.png') });

await page.setInputFiles('#calFile', { name: 'mic.txt', mimeType: 'text/plain',
  buffer: Buffer.from('"Sens Factor =-1.378dB, SERNO: 7012345"\n10 -2\n100 0\n1000 0\n10000 2\n20000 4\n') });
await page.waitForTimeout(200);
check(/5 points/.test(await page.textContent('#calInfo')), 'calibration file loaded');
const withCal = await page.evaluate(() => window.pumpRoom.S.cal && window.pumpRoom.W.x.room_cal_at(10000));
check(Math.abs(withCal - 2) < 1e-9, 'calibration curve read');

await page.click('#view button[data-v="impulse"]');
if (shots) await page.screenshot({ path: join(shots, 'room-impulse.png') });

// the correction: designed from the run, a profile, the Windows export
await page.click('#view button[data-v="correction"]');
await page.waitForTimeout(200);
const d = await page.evaluate(() => {
  const x = window.pumpRoom.S.design;
  if (!x) return null;
  const s = x.sides.map((sd) => sd && { mode: Math.min(...sd.correction), delay: sd.delay });
  return { preamp: x.preamp, s };
});
check(d && d.s[0] && d.s[1], 'a correction for both speakers');
check(d && d.s[0].mode < -4 && d.s[1].mode < -4, `the 110 Hz room mode is cut (${d && d.s.map((x) => x.mode.toFixed(1)).join(' / ')} dB)`);
check(d && d.preamp <= 0 && d.preamp > -7, `preamp ${d && d.preamp.toFixed(2)} dB`);
check(d && Math.abs(d.s[0].delay - Math.abs(run.dt)) < 0.01 && d.s[1].delay === 0, `the nearer (left) speaker delayed ${d && d.s[0].delay.toFixed(3)} ms`);
if (shots) await page.screenshot({ path: join(shots, 'room-correction.png') });
// phase: ALIGN PHASE with linear-phase filters lines up the sim speakers' late bass (their 40 Hz roll-off)
await page.click('#cPhase');
await page.click('#cLat button[data-v="linear"]');
await page.waitForTimeout(200);
const ph = await page.evaluate(() => {
  const x = window.pumpRoom.S.design, F = Array.from({ length: x.sides[0].gd.length }, (_, i) => 20 * Math.pow(2, i / 48));
  const band = (a) => { let lo = 1e9, hi = -1e9; for (let i = 0; i < F.length; ++i) if (F[i] >= 100 && F[i] <= 3000) { lo = Math.min(lo, a[i]); hi = Math.max(hi, a[i]); } return hi - lo; };
  return x.sides.map((sd) => ({ before: band(sd.gd), after: band(sd.gd.map((v, i) => v + sd.gdCorr[i])) }));
});
check(ph.every((p) => p.after < 0.6 * p.before), `phase: arrival spread 100 Hz..3 kHz ${ph.map((p) => p.before.toFixed(2) + ' -> ' + p.after.toFixed(2)).join(' / ')} ms`);
if (shots) await page.screenshot({ path: join(shots, 'room-phase.png') });
const dir = mkdtempSync(join(tmpdir(), 'pump-room-'));
{
  const [p] = await Promise.all([page.waitForEvent('download'), page.click('#cSave')]);
  const f = join(dir, 'profile.pumproom');
  await p.saveAs(f);
  const text = (await import('node:fs')).readFileSync(f, 'utf8');
  check(text.startsWith('pump-room-profile 1') && /channel L /.test(text) && /channel R /.test(text), `profile saved (${p.suggestedFilename()}, ${text.length} bytes)`);
  const gdCol = text.split('\n').filter((l) => /^[0-9]/.test(l)).map((l) => Math.abs(+l.split(' ')[4] || 0));
  check(Math.max(...gdCol) > 0.2, `profile carries the phase correction (largest ${Math.max(...gdCol).toFixed(2)} ms)`);
  const [z] = await Promise.all([page.waitForEvent('download'), page.click('#cWin')]);
  const zf = join(dir, 'win.zip');
  await z.saveAs(zf);
  const zb = (await import('node:fs')).readFileSync(zf);
  const names = [];
  for (let i = 0; i + 30 < zb.length;) {
    if (zb.readUInt32LE(i) !== 0x04034b50) break;
    const size = zb.readUInt32LE(i + 18), nl = zb.readUInt16LE(i + 26);
    names.push(zb.toString('utf8', i + 30, i + 30 + nl));
    i += 30 + nl + size;
  }
  check(names.length === 7 && names.includes('pump-room.txt') && names.includes('pump-room-48000.wav'), `Windows export: ${names.join(', ')}`);
  const [pz] = await Promise.all([page.waitForEvent('download'), page.click('#cPi')]);
  const pf = join(dir, 'pi.zip');
  await pz.saveAs(pf);
  const pb = (await import('node:fs')).readFileSync(pf);
  const pn = [];
  let yml = '';
  for (let i = 0; i + 30 < pb.length;) {
    if (pb.readUInt32LE(i) !== 0x04034b50) break;
    const size = pb.readUInt32LE(i + 18), nl = pb.readUInt16LE(i + 26);
    const nm = pb.toString('utf8', i + 30, i + 30 + nl);
    pn.push(nm);
    if (nm === 'pump-room.yml') yml = pb.toString('utf8', i + 30 + nl, i + 30 + nl + size);
    i += 30 + nl + size;
  }
  check(pn.length === 13 && pn.includes('coeffs/pump-room-R-48000.wav') && pn.includes('install.sh'), `Pi export: ${pn.length} files`);
  check(/filename: \/etc\/pump-room\/coeffs\/pump-room-L-\$samplerate\$\.wav/.test(yml) && /channels: \[1\]/.test(yml), 'CamillaDSP config names the filters per rate');
  // the shipped install script parses as shell
  const sh = join(dir, 'install.sh');
  for (let i = 0; i + 30 < pb.length;) {
    const size = pb.readUInt32LE(i + 18), nl = pb.readUInt16LE(i + 26);
    if (pb.toString('utf8', i + 30, i + 30 + nl) === 'install.sh') (await import('node:fs')).writeFileSync(sh, pb.subarray(i + 30 + nl, i + 30 + nl + size));
    i += 30 + nl + size;
  }
  const { execFileSync } = await import('node:child_process');
  let shOk = true;
  try { execFileSync('sh', ['-n', sh]); } catch (e) { shOk = false; }
  check(shOk, 'install.sh is valid shell');
}

const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#save')]);
const saved = join(dir, 'session.json');
await dl.saveAs(saved);
await page.setInputFiles('#openFile', saved);
await page.waitForFunction(() => window.pumpRoom.S.runs.length === 2, null, { timeout: 10000 });
const again = await page.evaluate(() => window.pumpRoom.S.runs.map((r) => ({ dt: r.dtMs, ld: r.levelDiff, a: r.ch.map((c) => c.arrival) })));
check(Math.abs(again[1].dt - again[0].dt) < 1e-6 && Math.abs(again[1].ld - again[0].ld) < 0.01, 'reopened session matches');
// handheld: the simulated mic against the left speaker, then at the listening spot, then carried to every dot
await page.click('#view button[data-v="handheld"]');
await page.click('#hPoints button[data-v="5"]');
const before = await page.evaluate(() => window.pumpRoom.S.runs.length);
const simMic = (x, y) => page.evaluate(([a, b]) => window.pumpRoom.S.node.port.postMessage({ type: 'simMic', x: a, y: b }), [x, y]);
await simMic(-1.2, 0.03);
await page.click('#hCal');
await page.waitForFunction(() => !window.pumpRoom.S.busy, null, { timeout: 20000 });
const cal = await page.evaluate(() => ({ spacing: window.pumpRoom.H.spacing, lat: window.pumpRoom.H.lat / window.pumpRoom.H.fs * 1000, info: window.pumpRoom.H.info }));
check(cal.spacing && Math.abs(cal.spacing - 2.4) < 0.06 && Math.abs(cal.lat - 12) < 0.3, `handheld: speakers ${cal.spacing && cal.spacing.toFixed(3)} m apart (2.4), latency ${cal.lat.toFixed(2)} ms (12) ${cal.spacing ? '' : cal.info}`);
const seat = { x: 0.1, y: 2.6 };
await simMic(seat.x, seat.y);
await page.click('#hCenter');
await page.waitForFunction(() => !window.pumpRoom.S.busy, null, { timeout: 20000 });
const ctr = await page.evaluate(() => window.pumpRoom.H.center);
check(ctr && Math.hypot(ctr.x - seat.x, ctr.y - seat.y) < 0.05, `handheld: center found at (${ctr && ctr.x.toFixed(3)}, ${ctr && ctr.y.toFixed(3)}) m, mic at (${seat.x}, ${seat.y})`);
if (shots) await page.screenshot({ path: join(shots, 'room-hand-start.png') });
await page.click('#hStart');
// carry the mic: first a little off the dot (it must say where to go), then onto it
let guided = false;
for (let step = 0; step < 400; ++step) {
  const st = await page.evaluate(() => { const H = window.pumpRoom.H; return { cur: H.cur, t: H.targets[H.cur], running: H.running, info: H.info, done: H.done.filter(Boolean).length }; });
  if (!st.running || st.cur < 0) break;
  if (/Move /.test(st.info)) guided = true;
  const offDot = step % 12 < 2;
  await simMic(st.t.x + (offDot ? 0.2 : 0.01), st.t.y + (offDot ? -0.1 : 0.01));
  await page.waitForTimeout(250);
}
await page.waitForFunction(() => !window.pumpRoom.S.busy, null, { timeout: 30000 });
const hand = await page.evaluate(() => ({ done: window.pumpRoom.H.done.filter(Boolean).length, n: window.pumpRoom.H.targets.length, info: window.pumpRoom.H.info,
  runs: window.pumpRoom.S.runs.filter((r) => r.pos).map((r) => r.pos) }));
check(hand.done === 5 && hand.runs.length === 5, `handheld: ${hand.done} of ${hand.n} points measured (${hand.info})`);
check(guided, 'handheld: it said which way to move');
const truth = [[0, 0], [0.3, 0], [0, 0.3], [-0.3, 0], [0, -0.3]];
check(hand.runs.every((p) => truth.some(([x, y]) => Math.hypot(p.x - x, p.y - y) < 0.06)), `handheld: positions recorded within 6 cm of their dots`);
check(await page.evaluate((b) => window.pumpRoom.S.runs.length === b + 5, before), 'handheld: the points joined the measurements');
if (shots) await page.screenshot({ path: join(shots, 'room-hand-done.png') });
await page.click('#view button[data-v="correction"]');
await page.waitForTimeout(200);
const pos = await page.evaluate(() => window.pumpRoom.S.design && window.pumpRoom.S.design.positions);
check(pos >= 6, `the correction averages ${pos} positions (the handheld ones included)`);
check(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.join('; ') : ''));
await browser.close();
console.log(fails ? `\n${fails} FAILED` : '\nall passed');
process.exit(fails ? 1 : 0);
