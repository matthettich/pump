// pump CONVERT: the window. The state (sources, versions, metadata) lives here and is saved as you go; planning and
// converting happen in the app (window.convert, preload.js). Opened without the app (a plain browser), it shows
// demo data so the page can be looked at and screenshotted.
'use strict';
(() => {
const API = window.convert || demoApi();
const $ = (s, r = document) => r.querySelector(s);
const el = (tag, props = {}, ...kids) => { const e = Object.assign(document.createElement(tag), props); for (const k of kids) if (k != null) e.append(k); return e; };

const FORMATS = { wav: 'WAV', aiff: 'AIFF', flac: 'FLAC', mp3: 'MP3', aac: 'AAC' };
const PCM = { wav: [16, 24, 32], aiff: [16, 24, 32], flac: [16, 24] };
const RATES = [0, 44100, 48000, 88200, 96000, 176400, 192000];
const MP3 = { cbr320: '320 kbps CBR', v0: 'V0 VBR', cbr256: '256 kbps CBR', cbr192: '192 kbps CBR' };
const AAC = [320, 256, 192];
const TOKENS = ['{track:02}', '{title}', '{artist}', '{album}', '{year}', '{isrc}', '{format}', '{bits}', '{khz}', '{bitdepth}', '{spec}', '{quality}', '{kbps}', '{source}', '{version}', '{tracks}'];

const v = (o) => Object.assign({ id: Math.random().toString(36).slice(2), enabled: true, format: 'wav', rate: 0, bits: 24, dither: 'tpdf', mp3: 'cbr320', aac: 256, folder: '', subfolder: '{format} {bits}-{khz}', name_tpl: '{track:02} {artist} - {title}' }, o);
const LOSSY_SUB = '{format} {kbps}';
const SETS = {
  'Master set (HD, streaming, CD, MP3, AAC)': [v({ rate: 0, bits: 24 }), v({ rate: 48000, bits: 24 }), v({ rate: 44100, bits: 16 }), v({ format: 'mp3', subfolder: LOSSY_SUB }), v({ format: 'aac', subfolder: LOSSY_SUB })],
  'Streaming (24/48 + 16/44.1)': [v({ rate: 48000, bits: 24 }), v({ rate: 44100, bits: 16 })],
  'CD (16/44.1, noise-shaped)': [v({ rate: 44100, bits: 16, dither: 'shaped' })],
  'Hi-res (24-bit at the source rate)': [v({ rate: 0, bits: 24 }), v({ format: 'flac', rate: 0, bits: 24, subfolder: '{format} {bits}-{khz}' })],
  'Promo (MP3 320 + AAC 256)': [v({ format: 'mp3', subfolder: LOSSY_SUB }), v({ format: 'aac', subfolder: LOSSY_SUB })],
};

let S = { sources: [], versions: SETS['Master set (HD, streaming, CD, MP3, AAC)'].map((x) => ({ ...x })), meta: { custom: [], writeTotal: true }, root: '' };
let userSets = {};
let caps = null, running = false, selected = -1, lastField = null, jobs = [];

// ------------------------------------------------------------------------------------------------- the rainbow
const hue = (col, row) => ((col + row) % 12) * 30 + 10;
function paint() {
  // keys and fields: hue by place in their row of controls, rows stepping down the page
  document.querySelectorAll('.tools, .artkeys, .run, .top').forEach((row, r) => row.querySelectorAll('.key, select').forEach((k, c) => k.style.setProperty('--h', hue(c, r))));
  document.querySelectorAll('.metagrid label').forEach((l, i) => l.style.setProperty('--h', hue(i % 2, Math.floor(i / 2) + 2)));
  document.querySelectorAll('#sourceTable tbody tr').forEach((tr, i) => tr.style.setProperty('--rh', hue(0, i)));
  document.querySelectorAll('#versionTable tbody tr').forEach((tr, i) => {
    tr.style.setProperty('--rh', hue(0, i + 3));
    tr.querySelectorAll('td').forEach((td, c) => td.style.setProperty('--h', hue(c, i + 3)));
  });
  document.querySelectorAll('.tok').forEach((t, i) => t.style.setProperty('--h', hue(i, 0)));
  document.querySelectorAll('.tagrow').forEach((t, i) => t.style.setProperty('--h', hue(1, i + 7)));
  $('#art').style.setProperty('--h', hue(0, 8));
}

// ------------------------------------------------------------------------------------------------- saving
let saveTimer = 0;
function save() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => API.save('last.json', { versions: S.versions, meta: S.meta, root: S.root, sources: S.sources }), 400);
  preview();
}

// ------------------------------------------------------------------------------------------------- sources
const fmtLen = (s) => { s = Math.round(s || 0); return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`; };
const fmtSrc = (p) => !p.ok ? 'unreadable' : `${p.float ? '32f' : (p.bits ? p.bits + '-bit' : p.codec)} ${(p.rate / 1000).toFixed(p.rate % 1000 ? 1 : 0)} kHz${p.channels === 1 ? ' mono' : p.channels > 2 ? ' ' + p.channels + 'ch' : ''}`;
async function addPaths(paths) {
  if (!paths.length) return;
  setBar(0, 'READING FILES…');
  const got = await API.probe(paths);
  for (const p of got)
  {
    if (S.sources.some((s) => s.path === p.path)) continue;
    p.title = p.tags && p.tags.title ? p.tags.title : p.name.replace(/\.[^.]+$/, '').replace(/^\d{1,3}[\s._-]+/, '');
    p.isrc = (p.tags && (p.tags.isrc || p.tags.tsrc)) || '';
    S.sources.push(p);
  }
  renderSources(); save(); setBar(0, idleText());
}
function renderSources() {
  const tb = $('#sourceTable tbody');
  tb.textContent = '';
  S.sources.forEach((s, i) => {
    const title = el('input', { type: 'text', value: s.title || '' });
    title.oninput = () => { s.title = title.value; save(); };
    const isrc = el('input', { type: 'text', value: s.isrc || '', placeholder: 'CC-XXX-YY-NNNNN' });
    isrc.oninput = () => { s.isrc = isrc.value.toUpperCase().replace(/[^A-Z0-9]/g, ''); save(); };
    isrc.onblur = () => { isrc.value = s.isrc; };
    const x = el('button', { className: 'x', title: 'Remove', textContent: '×' });
    x.onclick = () => { S.sources.splice(i, 1); renderSources(); save(); };
    const tr = el('tr', { className: 'row' + (i === selected ? ' sel' : ''), draggable: true },
      el('td', { className: 'n', textContent: String(i + 1).padStart(2, '0'), title: 'Drag to reorder' }),
      el('td', {}, title), el('td', {}, isrc),
      el('td', { className: 'file', textContent: s.name, title: s.path }),
      el('td', { className: 'fmt' + (s.ok ? '' : ' err'), textContent: fmtSrc(s) }),
      el('td', { className: 'len', textContent: fmtLen(s.duration) }),
      el('td', {}, x));
    tr.onclick = (e) => { if (e.target.tagName !== 'INPUT') { selected = i; renderSources(); } };
    tr.ondragstart = (e) => { if (e.target.tagName === 'INPUT') return; e.dataTransfer.setData('text/x-row', String(i)); e.dataTransfer.effectAllowed = 'move'; };
    tr.ondragover = (e) => { if ([...e.dataTransfer.types].includes('text/x-row')) { e.preventDefault(); tr.classList.add('drag-over'); } };
    tr.ondragleave = () => tr.classList.remove('drag-over');
    tr.ondrop = (e) => {
      const from = +e.dataTransfer.getData('text/x-row');
      if (isNaN(from)) return;
      e.preventDefault(); e.stopPropagation();
      const [m] = S.sources.splice(from, 1);
      S.sources.splice(i > from ? i - 1 + 1 : i, 0, m);
      selected = S.sources.indexOf(m); renderSources(); save();
    };
    tb.append(tr);
  });
  $('#dropHint').hidden = S.sources.length > 0;
  paint();
}

// ------------------------------------------------------------------------------------------------- versions
function opt(value, label, cur) { return el('option', { value, textContent: label, selected: String(value) === String(cur) }); }
function renderVersions() {
  const tb = $('#versionTable tbody');
  tb.textContent = '';
  S.versions.forEach((ver, i) => {
    const on = el('input', { type: 'checkbox', checked: ver.enabled !== false, title: 'Make this version' });
    on.onchange = () => { ver.enabled = on.checked; save(); };
    const seg = el('div', { className: 'seg' });
    for (const [k, name] of Object.entries(FORMATS))
    {
      const b = el('button', { className: ver.format === k ? 'on' : '', textContent: name });
      b.onclick = () => {
        const wasLossy = !PCM[ver.format];
        ver.format = k;
        if (PCM[k] && !PCM[k].includes(+ver.bits)) ver.bits = 24;
        if (!PCM[k] && ver.subfolder === '{format} {bits}-{khz}') ver.subfolder = LOSSY_SUB;
        if (PCM[k] && wasLossy && ver.subfolder === LOSSY_SUB) ver.subfolder = '{format} {bits}-{khz}';
        renderVersions(); save();
      };
      seg.append(b);
    }
    const rate = el('select', {}, ...RATES.map((r) => opt(r, r ? `${(r / 1000).toFixed(r % 1000 ? 1 : 0)} kHz` : 'SOURCE', ver.rate)));
    rate.onchange = () => { ver.rate = +rate.value; save(); };
    if (!PCM[ver.format]) { rate.textContent = ''; rate.append(opt(0, 'SOURCE ≤48k', ver.rate), opt(44100, '44.1 kHz', ver.rate), opt(48000, '48 kHz', ver.rate)); }
    let word;
    if (PCM[ver.format]) { word = el('select', {}, ...PCM[ver.format].map((b) => opt(b, b === 32 ? '32-bit float' : `${b}-bit`, ver.bits))); word.onchange = () => { ver.bits = +word.value; renderVersions(); save(); }; }
    else if (ver.format === 'mp3') { word = el('select', {}, ...Object.entries(MP3).map(([k, n]) => opt(k, n, ver.mp3))); word.onchange = () => { ver.mp3 = word.value; save(); }; }
    else { word = el('select', {}, ...AAC.map((k) => opt(k, `${k} kbps`, ver.aac))); word.onchange = () => { ver.aac = +word.value; save(); }; }
    const sixteen = PCM[ver.format] && +ver.bits === 16;
    const dither = el('select', { disabled: !sixteen, title: sixteen ? 'Dither for the 16-bit word length' : 'Only 16-bit results are dithered' },
      opt('tpdf', 'TPDF', ver.dither), opt('shaped', 'NOISE-SHAPED', ver.dither), opt('off', 'OFF', ver.dither));
    if (!sixteen) { dither.textContent = ''; dither.append(opt('', PCM[ver.format] ? 'NONE' : 'ENCODER', '')); }
    dither.onchange = () => { ver.dither = dither.value; save(); };
    const folder = el('button', { className: 'key small path folder', textContent: ver.folder ? shortPath(ver.folder) : 'OUTPUT FOLDER', title: ver.folder || 'The output folder above (click to give this version its own)' });
    folder.onclick = async () => { const p = await API.pickFolder('Folder for ' + FORMATS[ver.format]); if (p) { ver.folder = p; renderVersions(); save(); } };
    folder.oncontextmenu = (e) => { e.preventDefault(); ver.folder = ''; renderVersions(); save(); };
    const sub = el('input', { type: 'text', value: ver.subfolder || '', placeholder: '(none)' });
    sub.oninput = () => { ver.subfolder = sub.value; save(); };
    const name = el('input', { type: 'text', value: ver.name_tpl || '' });
    name.oninput = () => { ver.name_tpl = name.value; save(); };
    for (const f of [sub, name]) f.onfocus = () => { lastField = f; };
    const dup = el('button', { className: 'x', title: 'Duplicate', textContent: '⧉' });
    dup.onclick = () => { S.versions.splice(i + 1, 0, { ...ver, id: Math.random().toString(36).slice(2) }); renderVersions(); save(); };
    const x = el('button', { className: 'x', title: 'Remove', textContent: '×' });
    x.onclick = () => { S.versions.splice(i, 1); renderVersions(); save(); };
    tb.append(el('tr', { className: 'row' }, el('td', {}, on), el('td', {}, seg), el('td', {}, rate), el('td', {}, word), el('td', {}, dither),
      el('td', {}, folder), el('td', {}, sub), el('td', {}, name), el('td', {}, el('div', { style: 'display:flex' }, dup, x))));
  });
  paint();
}
const shortPath = (p) => { const parts = String(p).split(/[\\/]/).filter(Boolean); return (parts.length > 2 ? '…/' : '/') + parts.slice(-2).join('/'); };

// ------------------------------------------------------------------------------------------------- metadata
function renderMeta() {
  document.querySelectorAll('[data-meta]').forEach((i) => {
    i.value = S.meta[i.dataset.meta] || '';
    i.oninput = () => { S.meta[i.dataset.meta] = i.value; save(); };
    i.onfocus = () => { lastField = i; };
  });
  document.querySelectorAll('[data-meta-bool]').forEach((i) => { i.checked = S.meta[i.dataset.metaBool] !== false; i.onchange = () => { S.meta[i.dataset.metaBool] = i.checked; save(); }; });
  const box = $('#customTags');
  box.textContent = '';
  (S.meta.custom || []).forEach((c, i) => {
    const k = el('input', { type: 'text', value: c.key || '', placeholder: 'NAME' });
    const val = el('input', { type: 'text', value: c.value || '', placeholder: 'value, tokens welcome' });
    k.oninput = () => { c.key = k.value.toUpperCase(); save(); }; k.onblur = () => { k.value = c.key; };
    val.oninput = () => { c.value = val.value; save(); };
    k.onfocus = () => { lastField = k; }; val.onfocus = () => { lastField = val; };
    const x = el('button', { className: 'x', textContent: '×' });
    x.onclick = () => { S.meta.custom.splice(i, 1); renderMeta(); save(); };
    box.append(el('div', { className: 'tagrow' }, k, val, x));
  });
  const art = $('#art');
  art.classList.toggle('has', !!S.meta.artwork);
  art.style.backgroundImage = S.meta.artwork ? `url("${fileUrl(S.meta.artwork)}")` : '';
  art.title = S.meta.artwork || 'Artwork (JPEG or PNG)';
  paint();
}
const fileUrl = (p) => 'file://' + (p.startsWith('/') ? '' : '/') + p.replace(/\\/g, '/').split('/').map(encodeURIComponent).join('/');

function renderTokens() {
  const t = $('#tokens');
  t.textContent = '';
  for (const k of TOKENS)
  {
    const b = el('button', { className: 'tok', textContent: k, title: 'Insert into the last field you typed in' });
    b.onmousedown = (e) => e.preventDefault();
    b.onclick = () => {
      if (!lastField) return;
      const f = lastField, a = f.selectionStart ?? f.value.length, z = f.selectionEnd ?? f.value.length;
      f.value = f.value.slice(0, a) + k + f.value.slice(z);
      f.focus(); f.setSelectionRange(a + k.length, a + k.length);
      f.dispatchEvent(new Event('input'));
    };
    t.append(b);
  }
}

// ------------------------------------------------------------------------------------------------- planning, preview
let previewTimer = 0;
function preview() {
  clearTimeout(previewTimer);
  previewTimer = setTimeout(async () => {
    const ok = S.sources.filter((s) => s.ok);
    const vs = S.versions.filter((x) => x.enabled !== false);
    if (!ok.length || !vs.length) { $('#preview').textContent = ''; setBar(0, idleText()); return; }
    jobs = await API.plan(setup());
    const first = jobs[0];
    $('#preview').textContent = `${jobs.length} FILES  ·  e.g. ${first ? first.file : ''}`;
    if (!running) setBar(0, idleText());
  }, 150);
}
function setup() { return { sources: S.sources, versions: S.versions.map((x) => ({ ...x, folder: x.folder || S.root })), meta: { ...S.meta, outputRoot: S.root } }; }
function idleText() {
  const n = S.sources.filter((s) => s.ok).length, m = S.versions.filter((x) => x.enabled !== false).length;
  if (!n) return 'ADD SOURCES';
  if (!m) return 'ADD AN OUTPUT VERSION';
  if (!S.root && S.versions.some((x) => x.enabled !== false && !x.folder)) return 'CHOOSE AN OUTPUT FOLDER';
  return `READY  ·  ${n} SOURCE${n > 1 ? 'S' : ''} × ${m} VERSION${m > 1 ? 'S' : ''} = ${n * m} FILES`;
}
function setBar(frac, text) { $('#barFill').style.width = `${Math.round(frac * 1000) / 10}%`; $('#barText').textContent = text; }

// ------------------------------------------------------------------------------------------------- running
const progress = {};
async function go() {
  if (running) { await API.cancel(); return; }
  if (!S.root && S.versions.some((x) => x.enabled !== false && !x.folder)) { const p = await API.pickFolder('Output folder'); if (!p) return; S.root = p; renderRoot(); save(); }
  jobs = await API.plan(setup());
  if (!jobs.length) return;
  running = true;
  $('#go').textContent = 'STOP'; $('#go').classList.add('on');
  for (const k of Object.keys(progress)) delete progress[k];
  const lb = $('#logBody'); lb.textContent = '';
  for (const j of jobs)
  {
    progress[j.id] = { pct: 0, state: 'wait' };
    lb.append(el('tr', { id: 'job-' + j.id }, el('td', { className: 'state', textContent: 'WAITING' }), el('td', { className: 'num', textContent: j.file }), el('td', { className: 'num note' })));
  }
  $('#log').hidden = false;
  const r = await API.run(setup());
  running = false;
  $('#go').textContent = 'CONVERT'; $('#go').classList.remove('on');
  if (!r.ok && r.error) setBar(0, 'ERROR: ' + r.error);
  else setBar(r.cancelled ? sumPct() : 1, r.cancelled ? `STOPPED  ·  ${r.done} OF ${r.total} DONE` :
    `DONE  ·  ${r.done} FILES${r.warned ? `  ·  ${r.warned} REACH FULL SCALE (SEE LOG)` : ''}${r.failed ? `  ·  ${r.failed} FAILED (SEE LOG)` : '  ·  ALL CHECKED'}`);
}
function sumPct() { const v = Object.values(progress); return v.length ? v.reduce((a, b) => a + b.pct, 0) / v.length : 0; }
API.onProgress((m) => {
  const p = progress[m.id]; if (!p) return;
  p.pct = m.pct; p.state = m.state;
  const row = document.getElementById('job-' + m.id);
  if (row)
  {
    const st = row.cells[0];
    st.className = 'state ' + m.state;
    st.textContent = m.state === 'run' ? `${Math.round(m.pct * 100)} %` : m.state === 'done' ? 'DONE' : m.state === 'warn' ? 'CHECK' : 'FAILED';
    if (m.error || m.warning) row.cells[2].textContent = m.error || m.warning;
    else if (m.peak != null) row.cells[2].textContent = `peak ${m.peak.toFixed(1)} dBFS`;
    if (m.state !== 'run') row.ondblclick = () => API.reveal(m.file);
  }
  const done = Object.values(progress).filter((x) => x.state !== 'wait' && x.state !== 'run').length;
  setBar(sumPct(), `CONVERTING  ·  ${done} OF ${Object.keys(progress).length}`);
});

// ------------------------------------------------------------------------------------------------- the rest
function renderRoot() { $('#rootKey').textContent = S.root ? shortPath(S.root) : 'CHOOSE…'; $('#rootKey').title = S.root || 'Where the versions go (each row can have its own)'; }
function renderSets() {
  const m = $('#setMenu');
  m.textContent = '';
  m.append(el('option', { value: '', textContent: 'DELIVERABLE SETS…' }));
  for (const k of Object.keys(SETS)) m.append(el('option', { value: 'b:' + k, textContent: k }));
  for (const k of Object.keys(userSets)) m.append(el('option', { value: 'u:' + k, textContent: '★ ' + k }));
  m.onchange = () => {
    const [kind, ...rest] = m.value.split(':'); const k = rest.join(':');
    const set = kind === 'b' ? SETS[k] : userSets[k];
    if (set) { S.versions = set.map((x) => ({ ...x, id: Math.random().toString(36).slice(2) })); renderVersions(); save(); }
    m.value = '';
  };
}

async function init() {
  renderTokens();
  const last = await API.load('last.json');
  if (last) { S = Object.assign(S, last); S.meta.custom = S.meta.custom || []; }
  userSets = (await API.load('sets.json')) || {};
  renderSources(); renderVersions(); renderMeta(); renderRoot(); renderSets();
  caps = await API.caps();
  const eng = $('#engine');
  if (!caps.ok) { eng.textContent = 'FFMPEG MISSING: ' + caps.error; eng.classList.add('bad'); $('#go').disabled = true; }
  else
  {
    eng.textContent = `RESAMPLER ${caps.soxr ? 'SoX VHQ' : 'KAISER 256'}  ·  LAME ${caps.lame ? '✓' : '✗'}  ·  AAC ${caps.appleAac ? 'APPLE' : 'BUILT-IN'}`;
    if (!caps.lame) eng.classList.add('bad');
  }
  preview();
}

$('#addFiles').onclick = async () => addPaths(await API.pickFiles());
$('#addFolder').onclick = async () => { const p = await API.pickFolder('Add a folder of masters'); if (p) addPaths([p]); };
$('#clearSources').onclick = () => { S.sources = []; selected = -1; renderSources(); save(); };
$('#titlesFrom').onclick = () => { for (const s of S.sources) s.title = s.tags && s.tags.title ? s.tags.title : s.name.replace(/\.[^.]+$/, '').replace(/^\d{1,3}[\s._-]+/, ''); renderSources(); save(); };
$('#addVersion').onclick = () => { S.versions.push(v({})); renderVersions(); save(); };
$('#rootKey').onclick = async () => { const p = await API.pickFolder('Output folder'); if (p) { S.root = p; renderRoot(); save(); } };
$('#saveSet').onclick = () => {
  const name = prompt('Name for this set of versions:');
  if (!name) return;
  userSets[name] = S.versions.map((x) => ({ ...x })); API.save('sets.json', userSets); renderSets();
};
$('#pickArt').onclick = async () => { const p = await API.pickImage(); if (p) { S.meta.artwork = p; renderMeta(); save(); } };
$('#art').onclick = () => $('#pickArt').click();
$('#clearArt').onclick = () => { S.meta.artwork = ''; renderMeta(); save(); };
$('#addTag').onclick = () => { S.meta.custom.push({ key: '', value: '' }); renderMeta(); save(); };
$('#go').onclick = go;
$('#openOut').onclick = () => { const f = jobs[0] ? jobs[0].file : S.root; if (f) API.reveal(f); };
$('#logKey').onclick = () => { $('#log').hidden = !$('#log').hidden; };
$('#helpKey').onclick = () => { $('#help').hidden = false; };
$('#helpClose').onclick = () => { $('#help').hidden = true; };
document.addEventListener('keydown', (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); go(); }
  if (e.key === 'Delete' && selected >= 0 && document.activeElement.tagName !== 'INPUT') { S.sources.splice(selected, 1); selected = -1; renderSources(); save(); }
  if (e.key === 'Escape') $('#help').hidden = true;
});
// files dropped anywhere on the window
document.addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('Files')) { e.preventDefault(); document.body.classList.add('dragging'); } });
document.addEventListener('dragleave', (e) => { if (!e.relatedTarget) document.body.classList.remove('dragging'); });
document.addEventListener('drop', (e) => {
  document.body.classList.remove('dragging');
  if (!e.dataTransfer.files.length) return;
  e.preventDefault();
  addPaths([...e.dataTransfer.files].map((f) => API.pathFor(f)).filter(Boolean));
});

// ------------------------------------------------------------------------------------------------- demo (no app)
function demoApi() {
  const src = (n, t, rate, bits, d) => ({ ok: true, path: '/Masters/' + n, name: n, title: t, rate, bits, float: false, channels: 2, duration: d, codec: 'pcm_s24le', isrc: '', tags: {} });
  return {
    caps: async () => ({ ok: true, version: 'demo', soxr: true, lame: true, appleAac: true }),
    pickFiles: async () => [], pickFolder: async () => '', pickImage: async () => '',
    probe: async () => [], cancel: async () => {}, reveal: async () => {}, exists: async () => false,
    plan: async (s) => s.sources.flatMap((x, i) => s.versions.filter((q) => q.enabled !== false).map((q, k) => ({ id: `${k}-${i}`, file: `${s.meta.outputRoot || '/Out'}/${FORMATS[q.format]} ${q.bits || ''}/${String(i + 1).padStart(2, '0')} ${s.meta.artist || ''} - ${x.title}.${q.format === 'aac' ? 'm4a' : q.format}` }))),
    run: async () => ({ ok: true, done: 0, total: 0 }),
    load: async (n) => n === 'last.json' ? {
      root: '/Users/wake/Masters/Deliverables',
      sources: [src('01 Static Bloom.wav', 'Static Bloom', 96000, 24, 254), src('02 Night Pump.wav', 'Night Pump', 96000, 24, 301), src('03 Seeds.wav', 'Seeds', 48000, 24, 222), src('04 Ledger.aif', 'Ledger', 44100, 24, 189)]
        .map((s, i) => ({ ...s, isrc: 'USX9P26000' + (11 + i) })),
      meta: { artist: 'wake', album: 'Pump Season', year: '2026', genre: 'Electronic', label: 'pump records', copyright: '(c) 2026 wake', comment: 'Mastered · {spec}',
        custom: [{ key: 'SPEC', value: '{bitdepth} / {khz} kHz' }, { key: 'MASTERING ENGINEER', value: 'Hettich' }], writeTotal: true },
    } : null,
    save: async () => true, onProgress: () => {}, pathFor: () => '',
  };
}

init();
})();
