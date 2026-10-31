// Draws pump CONVERT's icon (convert/icon.png, 1024 px; electron-builder makes the .icns / .ico from it): node tools/make_convert_icon.js
// pump CONVERT icon: one waveform cut by a diagonal slash, pink on the left of the slash, green on the right
const { chromium } = require('playwright');
const S = 1024, cx = 512, cy = 512;
// bars: an envelope that swells and falls like a song, with some detail
const n = 19, x0 = 158, x1 = 866, step = (x1 - x0) / (n - 1), bw = step * 0.62;
let bars = '';
for (let i = 0; i < n; i++) {
  const t = i / (n - 1);
  const env = Math.pow(Math.sin(Math.PI * (0.08 + 0.84 * t)), 0.8);
  const det = 0.55 + 0.45 * Math.abs(Math.sin(i * 1.9) * Math.cos(i * 0.7 + 0.4));
  const h = Math.max(28, 600 * env * det);
  const x = x0 + i * step - bw / 2;
  bars += `<rect x="${x.toFixed(1)}" y="${(cy - h / 2).toFixed(1)}" width="${bw.toFixed(1)}" height="${h.toFixed(1)}" rx="${(bw / 2).toFixed(1)}"/>`;
}
// the slash: through the centre, rising to the right
const a = 64 * Math.PI / 180, L = 470, dx = Math.cos(a) * L, dy = Math.sin(a) * L;
const sx0 = cx - dx, sy0 = cy + dy, sx1 = cx + dx, sy1 = cy - dy;
// the halves: everything left of the slash line, everything right of it (big polygons along the line)
const far = 3000, ux = dx / L, uy = -dy / L, nx = -uy, ny = ux;   // u along the slash (upward), n to its left
const P = (x, y) => `${x.toFixed(1)},${y.toFixed(1)}`;
const left = [P(cx - ux * far, cy - uy * far), P(cx + ux * far, cy + uy * far), P(cx + ux * far - nx * -far, cy + uy * far - ny * -far), P(cx - ux * far - nx * -far, cy - uy * far - ny * -far)].join(' ');
const right = [P(cx - ux * far, cy - uy * far), P(cx + ux * far, cy + uy * far), P(cx + ux * far - nx * far, cy + uy * far - ny * far), P(cx - ux * far - nx * far, cy - uy * far - ny * far)].join(' ');
const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${S}" height="${S}" viewBox="0 0 ${S} ${S}">
<defs><clipPath id="L"><polygon points="${left}"/></clipPath><clipPath id="R"><polygon points="${right}"/></clipPath>
<g id="w">${bars}</g></defs>
<rect width="${S}" height="${S}" fill="#f4f4f6"/>
<rect x="52" y="52" width="920" height="920" fill="none" stroke="#3b3b44" stroke-width="24"/>
<g clip-path="url(#R)" fill="#f0609e"><use href="#w"/></g>
<g clip-path="url(#L)" fill="#3fd66a"><use href="#w"/></g>
<line x1="${sx0.toFixed(1)}" y1="${sy0.toFixed(1)}" x2="${sx1.toFixed(1)}" y2="${sy1.toFixed(1)}" stroke="#f4f4f6" stroke-width="76" stroke-linecap="butt"/>
<line x1="${sx0.toFixed(1)}" y1="${sy0.toFixed(1)}" x2="${sx1.toFixed(1)}" y2="${sy1.toFixed(1)}" stroke="#3b3b44" stroke-width="38" stroke-linecap="round"/>
</svg>`;
(async () => {
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: S, height: S } });
  await p.setContent(`<html><body style="margin:0">${svg}</body></html>`);
  await p.screenshot({ path: process.argv[2] || "convert/icon.png", omitBackground: false });
  await b.close();
})();
