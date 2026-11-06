// Draws the pump desktop apps' icons in Thunder Plus's IDM skin (warm grey paper, near-black ink, hazard orange and
// cobalt accents, JetBrains Mono caps): node tools/make_app_icons.js
//   convert/icon.png  pump CONVERT: a dog turning into a cat through a slash (smooth dog in, pixel cat out)
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
// a dog turning into a cat through the slash, like a file going through the app: the dog going in is drawn smooth in
// cobalt (floppy ears, round eyes, muzzle and nose), the cat coming out is sampled and quantised into orange pixels
// (pointed ears, eyes, nose, mouth and whiskers), each at the same height and size on its side of the slash.
const CAT = [   // 20 x 16 cells: # orange, o ink
  '...#............#...',
  '...##..........##...',
  '...#.#........#.#...',
  '...#..########..#...',
  '...#............#...',
  '..#..............#..',
  '..#..............#..',
  '..#...oo....oo...#..',
  '..#...oo....oo...#..',
  '..#..............#..',
  '###......##......###',
  '..#......##......#..',
  '###.....#..#.....###',
  '...#............#...',
  '....#..........#....',
  '.....##########.....',
];
function convert() {
  const sx = 512, top = 420, ang = 74 * Math.PI / 180, bar = 32, up = 420, down = 600;   // the slash, as before
  const run = (dy) => dy / Math.tan(ang);
  // the dog, centred on (316, 584)
  const ear = (m) => { const X = (x) => f(m ? 632 - x : x); return `M${X(264)},496 C${X(212)},494 ${X(194)},560 ${X(204)},632 C${X(212)},684 ${X(252)},680 ${X(264)},642 C${X(272)},602 ${X(272)},540 ${X(264)},496 Z`; };
  const dog = `
  <ellipse cx="316" cy="584" rx="86" ry="96" fill="none" stroke="${C.blue}" stroke-width="22"/>
  <path d="${ear(0)}" fill="${C.blue}"/><path d="${ear(1)}" fill="${C.blue}"/>
  <circle cx="288" cy="566" r="14" fill="${C.ink}"/><circle cx="292" cy="562" r="4" fill="${C.paper}"/>
  <circle cx="344" cy="566" r="14" fill="${C.ink}"/><circle cx="348" cy="562" r="4" fill="${C.paper}"/>
  <path d="M296,606 Q316,598 336,606 Q338,626 316,634 Q294,626 296,606 Z" fill="${C.ink}"/>
  <path d="M316,634 V650 M316,650 Q302,664 288,654 M316,650 Q330,664 344,654" fill="none" stroke="${C.blue}" stroke-width="10" stroke-linecap="round"/>`;
  // the cat: 16 px pixels from x 520, y 456
  const g = 16, cx0 = 520, cy0 = 456;
  let cat = '';
  CAT.forEach((row, r) => [...row].forEach((ch, c) => {
    if (ch !== '.') cat += `<rect x="${cx0 + c * g}" y="${cy0 + r * g}" width="${g}" height="${g}" fill="${ch === 'o' ? C.ink : C.orange}"/>`;
  }));
  const sl = `x1="${f(sx - run(down))}" y1="${f(top + down)}" x2="${f(sx + run(up))}" y2="${f(top - up)}"`;
  const body = `${dog}${cat}
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

// --out DIR writes the PNGs there instead of into convert/ and system/ (for previewing)
(async () => {
  const root = path.join(__dirname, '..');
  const oi = process.argv.indexOf('--out'), dir = oi > 0 ? path.resolve(process.argv[oi + 1]) : null;
  const where = (sub) => dir ? path.join(dir, sub + '-icon.png') : path.join(root, sub, 'icon.png');
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: S, height: S } });
  const out = [[where('convert'), convert()], [where('system'), systemEq()]];
  for (const [file, svg] of out) {
    await p.setContent(`<html><body style="margin:0;background:transparent">${svg}</body></html>`);
    await p.evaluate(() => document.fonts.ready);
    await p.screenshot({ path: file, omitBackground: true });
    console.log('wrote', file);
  }
  await b.close();
})();
