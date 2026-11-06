// Draws the pump desktop apps' icons in Thunder Plus's IDM skin (warm grey paper, near-black ink, hazard orange and
// cobalt accents, JetBrains Mono caps): node tools/make_app_icons.js
//   convert/icon.png  pump CONVERT: one head, a dog turning into a cat through the slash (smooth dog half, pixel cat half)
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
// a dog turning into a cat through the slash: one head, the dog half drawn smooth in cobalt (floppy ear, round eye,
// muzzle), the cat half sampled and quantised into orange pixels (pointed ear, slit eye, nose, whiskers), the way the
// app turns one file into another. The cat is drawn as smooth lines first, then rasterised onto a grid in the browser
// (see catPixels), and only the cells right of the slash are kept.
const SLASH = { sx: 512, top: 420, ang: 74 * Math.PI / 180, bar: 32, up: 420, down: 600 };
const slashX = (y) => SLASH.sx + (SLASH.top - y) / Math.tan(SLASH.ang);   // x of the slash's centre line at height y
const HEAD = { cx: 500, cy: 560, rx: 205, ry: 192 };
const GRID = 20;   // cat pixel size

// the dog: drawn whole, then clipped to the left of the slash
const dogSvg = () => `
  <ellipse cx="${HEAD.cx}" cy="${HEAD.cy}" rx="${HEAD.rx}" ry="${HEAD.ry}" fill="none" stroke="${C.blue}" stroke-width="26"/>
  <path d="M372,404 C300,380 236,430 236,520 C236,610 262,700 316,712 C362,722 372,640 360,570 C350,510 352,450 372,404 Z" fill="${C.blue}"/>
  <circle cx="420" cy="528" r="26" fill="${C.ink}"/>
  <circle cx="429" cy="519" r="7" fill="${C.paper}"/>
  <path d="M500,612 C420,612 382,648 382,686 C382,724 430,746 500,746" fill="none" stroke="${C.blue}" stroke-width="22" stroke-linecap="round"/>`;

// the cat as smooth source shapes, rasterised later; strokes and fills are all one colour (coverage is what counts)
const catSrc = () => `
  <ellipse cx="${HEAD.cx}" cy="${HEAD.cy}" rx="${HEAD.rx}" ry="${HEAD.ry}" fill="none" stroke="#000" stroke-width="26"/>
  <path d="M578,392 L664,262 L690,446" fill="none" stroke="#000" stroke-width="26" stroke-linejoin="miter"/>
  <path d="M620,330 L654,300 L664,380 Z" fill="#000"/>
  <path d="M556,524 Q600,478 650,520 Q600,562 556,524 Z" fill="#000"/>
  <path d="M492,600 L552,600 L522,634 Z" fill="#000"/>
  <path d="M522,630 L522,662 M522,662 Q500,688 476,668 M522,662 Q544,688 568,668" fill="none" stroke="#000" stroke-width="16"/>
  <path d="M600,612 L770,584 M606,640 L780,640 M600,668 L770,698" fill="none" stroke="#000" stroke-width="14"/>`;

function convert(catCells) {
  const { sx, top, ang, bar, up, down } = SLASH;
  const run = (dy) => dy / Math.tan(ang);
  // clip region left of the slash (extended well past the tile)
  const L = `M0,0 L${f(slashX(0))},0 L${f(slashX(1024))},1024 L0,1024 Z`;
  // cat pixels: orange cells, with the pupil left as a dark slit inside the eye
  let px = '';
  for (const [x, y, k] of catCells) px += `<rect x="${x}" y="${y}" width="${GRID}" height="${GRID}" fill="${k ? C.ink : C.orange}"/>`;
  const sl = `x1="${f(sx - run(down))}" y1="${f(top + down)}" x2="${f(sx + run(up))}" y2="${f(top - up)}"`;
  const body = `
  <clipPath id="dogside"><path d="${L}"/></clipPath>
  <g clip-path="url(#dogside)">${dogSvg()}</g>
  ${px}
  <line ${sl} stroke="url(#paper)" stroke-width="${bar + 16}"/>
  <line ${sl} stroke="${C.ink}" stroke-width="${bar}"/>`;
  return tile('CONVERT', body);
}

// rasterise the cat in the page: draw its source shapes to a canvas, then keep each grid cell that is mostly covered
// and lies wholly right of the slash. The eye's slit pupil is a second pass, marked dark.
async function catPixels(p) {
  const svg = (inner) => `<svg xmlns="http://www.w3.org/2000/svg" width="${S}" height="${S}">${inner}</svg>`;
  const pupil = `<rect x="596" y="490" width="12" height="68" fill="#000"/>`;
  return p.evaluate(async ({ shapes, pupil, S, G, sl }) => {
    const raster = async (src) => {
      const img = new Image();
      img.src = 'data:image/svg+xml;base64,' + btoa(src);
      await img.decode();
      const c = document.createElement('canvas'); c.width = c.height = S;
      const g = c.getContext('2d'); g.drawImage(img, 0, 0);
      return g.getImageData(0, 0, S, S).data;
    };
    const a = await raster(shapes), b = await raster(pupil);
    const cover = (d, x0, y0) => { let n = 0; for (let y = y0; y < y0 + G; y++) for (let x = x0; x < x0 + G; x++) if (d[(y * S + x) * 4 + 3] > 127) n++; return n / (G * G); };
    const sx = (y) => sl.sx + (sl.top - y) / Math.tan(sl.ang);
    const out = [];
    for (let y = 0; y + G <= S; y += G) for (let x = 0; x + G <= S; x += G) {
      if (x < sx(y) + sl.bar / 2 + 8) continue;   // keep only cells clear of the slash
      const dark = cover(b, x, y) > 0.3, on = cover(a, x, y) > 0.4;
      if (dark && on) out.push([x, y, 1]); else if (on) out.push([x, y, 0]);
    }
    return out;
  }, { shapes: svg(catSrc()), pupil: svg(pupil), S, G: GRID, sl: SLASH });
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
  const out = [[where('convert'), convert(await catPixels(p))], [where('system'), systemEq()]];
  for (const [file, svg] of out) {
    await p.setContent(`<html><body style="margin:0;background:transparent">${svg}</body></html>`);
    await p.evaluate(() => document.fonts.ready);
    await p.screenshot({ path: file, omitBackground: true });
    console.log('wrote', file);
  }
  await b.close();
})();
