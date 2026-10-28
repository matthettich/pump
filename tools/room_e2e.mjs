// End-to-end check of the built pump ROOM page (bin/pump-room.html, `make room`) in headless Chromium, with the
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

const dir = mkdtempSync(join(tmpdir(), 'pump-room-'));
const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#save')]);
const saved = join(dir, 'session.json');
await dl.saveAs(saved);
await page.setInputFiles('#openFile', saved);
await page.waitForFunction(() => window.pumpRoom.S.runs.length === 2, null, { timeout: 10000 });
const again = await page.evaluate(() => window.pumpRoom.S.runs.map((r) => ({ dt: r.dtMs, ld: r.levelDiff, a: r.ch.map((c) => c.arrival) })));
check(Math.abs(again[1].dt - again[0].dt) < 1e-6 && Math.abs(again[1].ld - again[0].ld) < 0.01, 'reopened session matches');
check(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.join('; ') : ''));
await browser.close();
console.log(fails ? `\n${fails} FAILED` : '\nall passed');
process.exit(fails ? 1 : 0);
