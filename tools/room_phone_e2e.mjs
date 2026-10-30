// End-to-end check of PHONE MIC (room/remote.js) in headless Chromium: two pages of the built pump ROOM page, the
// computer and the phone, paired through the local relay (relay/local.mjs) and connected directly, with the test
// mode's simulated room on the phone's side: a random start and a phone clock that runs fast or slow, so the run has
// to be found and the clocks matched from the recording itself. Also checks that MEASURE still works beside it.
// Needs Playwright and the ws package:  node tools/room_phone_e2e.mjs [path/to/pump-room.html] [screenshot folder]
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const pageFile = resolve(process.argv[2] || 'bin/pump-room.html');
const shots = process.argv[3] || null;
let fails = 0;
const check = (ok, what) => { console.log(`  ${ok ? 'ok' : 'FAIL'}: ${what}`); if (!ok) ++fails; };

const port = 18000 + Math.floor(Math.random() * 1000);
const relay = spawn(process.execPath, [join(here, '../relay/local.mjs'), String(port)], { stdio: ['ignore', 'pipe', 'inherit'] });
await new Promise((res, rej) => { relay.stdout.once('data', res); relay.once('exit', () => rej(new Error('relay did not start'))); });
const relayUrl = `ws://localhost:${port}`;

// the direct connection on one machine: plain local addresses instead of the hidden (mDNS) ones
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--disable-features=WebRtcHideLocalIpsWithMdns'] });
const errors = [];
const computer = await browser.newPage({ viewport: { width: 1280, height: 900 } });
computer.on('pageerror', (e) => errors.push('computer: ' + e.message));
await computer.goto(`file://${pageFile}#sim&relay=${encodeURIComponent(relayUrl)}`);
await computer.click('#len button[data-v="2"]');
await computer.click('#phoneMic');
await computer.waitForSelector('#phQr');
const code = await computer.evaluate(() => window.pumpRoomPhone.P.code);
check(/^[A-HJ-NP-Z2-9]{6}$/.test(code), `pairing code ${code}`);
const link = await computer.evaluate(() => document.querySelector('.ph-url a').href);
check(link.includes('#phone=' + code), 'the QR link opens the phone page with the code');
const qrDark = await computer.evaluate(() => {
  const c = document.querySelector('#phQr'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 4) if (d[i] < 50) ++n;
  return n / (d.length / 4);
});
check(qrDark > 0.2 && qrDark < 0.6, `QR code drawn (${(qrDark * 100).toFixed(0)}% dark)`);
if (shots) await computer.screenshot({ path: join(shots, 'phone-pair.png') });

const phone = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
phone.on('pageerror', (e) => errors.push('phone: ' + e.message));
await phone.goto(`file://${pageFile}#phone=${code}&sim&ppm=150&relay=${encodeURIComponent(relayUrl)}`);
await Promise.all([
  computer.waitForFunction(() => window.pumpRoomPhone.P.open, null, { timeout: 20000 }),
  phone.waitForFunction(() => window.pumpRoomPhone.P.open, null, { timeout: 20000 }),
]);
check(true, 'paired and connected directly');
await computer.waitForFunction(() => window.pumpRoomPhone.P.phoneMic, null, { timeout: 5000 });
await phone.waitForTimeout(2000);
check(await phone.evaluate(() => !window.pumpRoomPhone.P.ws), 'the phone left the relay');
check(/SWEEP 2 S/.test(await phone.textContent('#ppLink')), 'the phone shows the computer\'s sweep settings');
if (shots) await phone.screenshot({ path: join(shots, 'phone-page.png') });

const runs = () => computer.evaluate(() => window.pumpRoomPhone.P.busy ? -1 : window.pumpRoom.S.runs.length);
const expectDt = 0.2 / 343 * 1000;
async function measureFrom(where, n) {
  if (where === 'phone') await phone.click('#ppMeasure');
  else await computer.click('#phMeasure');
  await computer.waitForFunction((k) => !window.pumpRoomPhone.P.busy && window.pumpRoom.S.runs.length === k, n, { timeout: 90000 }).catch(() => {});
  return computer.evaluate(() => {
    const r = window.pumpRoom.S.runs.at(-1);
    return r && { name: r.name, phone: r.phone, dt: r.dtMs, pol: r.polarity, ch: r.ch.map((c) => ({ side: c.side, snr: c.snr, arrival: c.arrival })), notes: r.notes,
      said: document.querySelector('#phNote').textContent };
  });
}

// 1) both speakers, the phone's clock 150 ppm fast, started from the phone
let r = await measureFrom('phone', 1);
check(r && r.phone && r.ch.length === 2, `both speakers measured from the phone (${r && r.said})`);
check(r && Math.abs(r.dt - expectDt) < 0.02, `right ${r && r.dt.toFixed(3)} ms after left (${expectDt.toFixed(3)}; unmatched clocks would be ~0.6 ms off)`);
check(r && r.ch.every((c) => c.snr > 40) && r.pol === 1, `SNR ${r && r.ch.map((c) => c.snr.toFixed(0)).join(' / ')} dB, polarity +`);
check(r && /150 ppm/.test(r.said), 'the clock difference was found');
check(/Measured/.test(await phone.textContent('#ppSays')), 'the phone was told the result');

// 2) the phone's clock 400 ppm slow, started from the computer
await phone.evaluate(() => { window.pumpRoomPhone.P.simPpm = -400; });
r = await measureFrom('computer', 2);
check(r && r.ch.length === 2 && Math.abs(r.dt - expectDt) < 0.02, `slow clock: right ${r && r.dt.toFixed(3)} ms after left (${r && r.said})`);

// 3) one speaker (no clock ping needed)
await computer.click('#phClose');
await computer.click('#chans button[data-v="L"]');
await computer.click('#phoneMic');
r = await measureFrom('phone', 3);
check(r && r.ch.length === 1 && r.ch[0].side === 'L' && r.ch[0].snr > 40, 'left speaker alone');
await computer.click('#phClose');
await computer.click('#chans button[data-v="LR"]');

// a calibration file sent from the phone
await phone.setInputFiles('#ppCalFile', { name: 'mymic.txt', mimeType: 'text/plain', buffer: Buffer.from('10 -2\n100 0\n1000 0\n10000 2\n20000 4\n') });
await computer.waitForFunction(() => window.pumpRoom.S.cal && window.pumpRoom.S.cal.name === 'mymic.txt', null, { timeout: 5000 }).catch(() => {});
check(await computer.evaluate(() => !!window.pumpRoom.S.cal && window.pumpRoom.S.cal.name === 'mymic.txt'), 'calibration file sent from the phone');
await phone.waitForFunction(() => /mymic/.test(document.querySelector('#ppCalInfo').textContent), null, { timeout: 5000 }).catch(() => {});
check(/mymic/.test(await phone.textContent('#ppCalInfo')), 'the phone shows it');

// the computer's own MEASURE still works beside it
await computer.click('#start');
await computer.waitForFunction(() => !document.querySelector('#measure').disabled, null, { timeout: 15000 });
await computer.click('#measure');
await computer.waitForFunction(() => window.pumpRoom.S.runs.length === 4 && !window.pumpRoom.S.busy, null, { timeout: 60000 });
const own = await computer.evaluate(() => { const x = window.pumpRoom.S.runs[3]; return { phone: !!x.phone, dt: x.dtMs, n: x.ch.length }; });
check(!own.phone && own.n === 2 && Math.abs(own.dt - (Math.round(0.012 * 48000 + 3.1 / 343 * 48000) - Math.round(0.012 * 48000 + 2.9 / 343 * 48000)) / 48) < 0.03,
  `MEASURE on the computer unchanged (right ${own.dt.toFixed(3)} ms after left)`);

// the correction takes the phone's runs with the rest: time alignment from them all
await computer.click('#view button[data-v="correction"]');
await computer.waitForTimeout(300);
const d = await computer.evaluate(() => { const x = window.pumpRoom.S.design; return x && x.sides.map((s) => s && s.delay); });
check(d && d[0] > 0.5 && d[0] < 0.7, `correction: left delayed ${d && d[0] && d[0].toFixed(3)} ms`);
if (shots) {
  await computer.click('#phoneMic');
  await computer.screenshot({ path: join(shots, 'phone-connected.png') });
  await computer.click('#phClose');
}

// the phone goes away: the computer waits for it again
await phone.close();
await computer.waitForFunction(() => !window.pumpRoomPhone.P.open, null, { timeout: 20000 }).catch(() => {});
check(await computer.evaluate(() => !window.pumpRoomPhone.P.open && window.pumpRoomPhone.P.role === 'player'), 'phone gone: the computer waits for it again');

check(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
await browser.close();
relay.kill();
console.log(fails ? `\n${fails} FAILED` : '\nall passed');
process.exit(fails ? 1 : 0);
