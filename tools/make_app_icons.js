// Draws the pump desktop apps' icons in Thunder Plus's IDM skin (warm grey paper, near-black ink, hazard orange and
// cobalt accents, JetBrains Mono caps): node tools/make_app_icons.js
//   convert/icon.png  pump CONVERT: a smooth wave, a slash, then the same wave sampled and quantised (a staircase)
//   system/icon.png   System EQ: a segmented spectrum with peak-hold caps
// 1024 px with transparency, on the macOS icon grid (an 824 px rounded tile with its shadow); electron-builder makes
// CONVERT's .icns / .ico from its PNG, system/build.sh makes System EQ's .icns.
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const S = 1024;
const C = { ink: '#141414', ink2: '#505050', dim: '#bdbcb6', paper: '#f2f1ec', paper2: '#e9e8e3', orange: '#ff4d00', blue: '#2a6cff' };
const font = fs.readFileSync(path.join(__dirname, '..', 'common', 'fonts', 'JetBrainsMono-Bold.ttf')).toString('base64');
const f = (v) => v.toFixed(1);

// the tile: macOS grid (100 px margin, 824 px body), a soft shadow, paper with a faint fall-off, a hairline inner edge,
// the app's name small in the top-left corner after an orange block, and a ruler along the bottom
function tile(name, body) {
  let ticks = '';
  for (let i = 0; i <= 32; i++) {
    const x = 196 + i * (632 / 32), h = i % 8 === 0 ? 26 : i % 4 === 0 ? 17 : 10;
    ticks += `<rect x="${f(x - 2)}" y="${f(838 - h)}" width="4" height="${h}" fill="${i % 8 === 0 ? C.ink : C.ink2}"/>`;
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${S}" height="${S}" viewBox="0 0 ${S} ${S}">
<defs>
  <style>@font-face{font-family:JBM;font-weight:700;src:url(data:font/ttf;base64,${font})}</style>
  <linearGradient id="paper" gradientUnits="userSpaceOnUse" x1="0" y1="100" x2="0" y2="924"><stop offset="0" stop-color="${C.paper}"/><stop offset="1" stop-color="${C.paper2}"/></linearGradient>
  <filter id="sh" x="-20%" y="-20%" width="140%" height="140%"><feDropShadow dx="0" dy="12" stdDeviation="14" flood-color="#000" flood-opacity=".30"/></filter>
  <clipPath id="tile"><rect x="100" y="100" width="824" height="824" rx="186"/></clipPath>
</defs>
<rect x="100" y="100" width="824" height="824" rx="186" fill="url(#paper)" filter="url(#sh)"/>
<g clip-path="url(#tile)">
  <rect x="100" y="100" width="824" height="824" rx="186" fill="none" stroke="${C.dim}" stroke-width="4"/>
  <rect x="196" y="196" width="30" height="30" fill="${C.orange}"/>
  <text x="246" y="224" font-family="JBM" font-weight="700" font-size="38" letter-spacing="6" fill="${C.ink}">${name}</text>
  ${ticks}
  <rect x="196" y="836" width="632" height="4" fill="${C.ink}"/>
  ${body}
</g>
</svg>`;
}

// --- pump CONVERT
// one wave running through the slash, as if converted live: smooth going in, sampled and quantised coming out. The
// slash hits the wave at the top of a crest, where it is flat, so the two halves split straight down the middle under
// the slash and meet at the same height.
function convert() {
  const cy = 570, A = 150, x0 = 196, x1 = 828, sx = 512, W = x1 - x0;
  const wave = (x) => cy - A * Math.cos(((x - sx) / W) * 1.5 * 2 * Math.PI);   // 1.5 cycles: zero, trough, crest at the slash, trough, zero
  const top = cy - A, ang = 74 * Math.PI / 180, bar = 32, up = 205, down = 34;   // the slash comes down onto the crest
  const run = (dy) => dy / Math.tan(ang);
  let d = '';
  for (let x = x0; x <= sx; x += 2) d += (d ? 'L' : 'M') + f(x) + ',' + f(wave(x));
  // sample and hold, 9 levels, 13 whole steps from the slash to the edge; each holds its centre's value
  const q = (2 * A) / 8, n = 13, sw = (x1 - sx) / n, quant = (x) => cy + Math.round((wave(x) - cy) / q) * q;
  let st = '';
  for (let i = 0; i < n; i++) {
    const x = sx + i * sw, y = quant(x + sw / 2);
    st += (i ? `V${f(y)}` : `M${f(x)},${f(y)}`) + `H${f(x + sw)}`;
  }
  const sl = `x1="${f(sx - run(down))}" y1="${f(top + down)}" x2="${f(sx + run(up))}" y2="${f(top - up)}"`;
  const body = `
  <line x1="196" y1="${cy}" x2="828" y2="${cy}" stroke="${C.dim}" stroke-width="4"/>
  <path d="${d}" fill="none" stroke="${C.blue}" stroke-width="26" stroke-linecap="butt" stroke-linejoin="round"/>
  <path d="${st}" fill="none" stroke="${C.orange}" stroke-width="22" stroke-linecap="butt" stroke-linejoin="miter"/>
  <line ${sl} stroke="url(#paper)" stroke-width="${bar + 16}"/>
  <line ${sl} stroke="${C.ink}" stroke-width="${bar}"/>`;
  return tile('CONVERT', body);
}

// --- System EQ
function systemEq() {
  // 14 bands (blocks lit per band), a music-like spectrum: strong lows, a dip, a mid hump, presence, then the top rolling off
  const lv = [7, 10, 11, 9, 7, 7, 8, 8, 7, 6, 6, 5, 4, 2];
  const pk = lv.map((v) => v + 2);   // peak hold: one dark block above each bar
  const n = lv.length, xL = 196, xR = 828, gap = 12, bw = ((xR - xL) - gap * (n - 1)) / n;
  const rows = 14, base = 792, segGap = 10, segH = (base - 280 - segGap * (rows - 1)) / rows;
  const y = (r) => base - (r + 1) * segH - r * segGap;
  let out = '';
  for (let i = 0; i < n; i++) {
    const x = xL + i * (bw + gap);
    for (let r = 0; r < rows; r++) {
      const c = r < lv[i] ? `fill="${C.ink}"` : r === pk[i] - 1 ? `fill="${C.orange}"` : `fill="${C.ink}" opacity=".055"`;
      out += `<rect x="${f(x)}" y="${f(y(r))}" width="${f(bw)}" height="${f(segH)}" ${c}/>`;
    }
  }
  return tile('SYSTEM EQ', out);
}

(async () => {
  const root = path.join(__dirname, '..');
  const out = [[path.join(root, 'convert', 'icon.png'), convert()], [path.join(root, 'system', 'icon.png'), systemEq()]];
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: S, height: S } });
  for (const [file, svg] of out) {
    await p.setContent(`<html><body style="margin:0;background:transparent">${svg}</body></html>`);
    await p.evaluate(() => document.fonts.ready);
    await p.screenshot({ path: file, omitBackground: true });
    console.log('wrote', path.relative(root, file));
  }
  await b.close();
})();
