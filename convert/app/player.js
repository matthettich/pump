// pump CONVERT, PLAY page: a playlist player with tag editing and a choice of output device.
//   Decoding is ffmpeg's, in the main process (player-engine.js): 32-bit float PCM at the file's own sample rate, pulled
//   in chunks. Here the AudioContext is made at that rate (and made again when the rate changes between tracks) and an
//   AudioWorklet (player-worklet.js) plays the chunks one after the other, so same-rate tracks are gapless. At 100 %
//   volume the samples go from the worklet straight to the output: there is no gain stage in the path.
// Opened without the app (a plain browser) there is no window.player and the PLAY page does not appear.
'use strict';
(() => {
const API = window.player;
const $ = (s, r = document) => r.querySelector(s);
const el = (tag, props = {}, ...kids) => { const e = Object.assign(document.createElement(tag), props); for (const k of kids) if (k != null) e.append(k); return e; };
if (!API) { for (const id of ['pgConvert', 'pgPlay']) { const b = document.getElementById(id); if (b) b.parentElement.hidden = true; } return; }

const CHUNK_FRAMES = 32768;   // what one pull asks for
const TARGET_SEC = 4;         // how much is kept queued in the audio thread
const FIELDS = [['title', 'TITLE'], ['artist', 'ARTIST'], ['album', 'ALBUM'], ['albumArtist', 'ALBUM ARTIST'], ['track', 'TRACK', 'half'], ['year', 'YEAR', 'half'], ['genre', 'GENRE'], ['comment', 'COMMENT', 'area']];

// ------------------------------------------------------------------------------------------------- state
const S = { tracks: [], repeat: 'off', shuffle: false, volume: 100, device: '' };
let nextTid = 1;
let selTid = 0;
let shufOrder = [];
const byTid = (tid) => S.tracks.findIndex((t) => t.id === tid);
const keep = ['path', 'name', 'title', 'artist', 'album', 'albumArtist', 'track', 'year', 'genre', 'comment', 'duration', 'rate', 'bits', 'float', 'channels', 'codec', 'hasCover', 'writable'];
let saveTimer = 0;
function save() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => API.save({ v: 1, repeat: S.repeat, shuffle: S.shuffle, volume: S.volume, device: S.device,
    tracks: S.tracks.map((t) => Object.fromEntries(keep.map((k) => [k, t[k]]))) }), 400);
}

// ------------------------------------------------------------------------------------------------- the page
const page = el('main', { className: 'pmain', id: 'pagePlay', hidden: true });
page.innerHTML = `
  <section class="panel plist">
    <div class="phead">
      <div class="cap">PLAYLIST</div>
      <div class="tools">
        <button class="key" id="pAddFiles">ADD FILES</button>
        <button class="key" id="pAddFolder">ADD FOLDER</button>
        <button class="key" id="pLoadList" title="Add the tracks of an .m3u8 playlist">LOAD M3U8</button>
        <button class="key" id="pSaveList" title="Save the playlist as .m3u8 (UTF-8)">SAVE M3U8</button>
        <button class="key" id="pClear">CLEAR</button>
      </div>
    </div>
    <div class="table-wrap">
      <table class="list dense" id="pTable">
        <thead><tr><th class="c-n">#</th><th>TITLE</th><th>ARTIST</th><th>ALBUM</th><th class="c-t">TIME</th><th class="c-f">FORMAT</th><th class="c-x"></th></tr></thead>
        <tbody></tbody>
      </table>
      <div class="drop" id="pDrop">DROP AUDIO FILES, FOLDERS OR AN .M3U8 HERE<br><span>Double-click a track to play it. Drag rows to reorder. Click a track to edit its tags.</span></div>
    </div>
  </section>
  <section class="panel pmeta">
    <div class="cap">TRACK TAGS</div>
    <div class="pfile" id="pFile">SELECT A TRACK</div>
    <div class="metagrid" id="pFields"></div>
    <div class="artrow">
      <div class="art" id="pArt" title="Artwork (JPEG or PNG)"><span>ARTWORK</span></div>
      <div class="artkeys"><button class="key" id="pArtPick">CHOOSE ARTWORK</button><button class="key" id="pArtNone">REMOVE</button></div>
    </div>
    <div class="tools">
      <button class="key" id="pTagSave">SAVE TAGS</button>
      <button class="key" id="pTagRevert">REVERT</button>
      <button class="key" id="pReveal">SHOW FILE</button>
    </div>
    <div class="preview" id="pMsg"></div>
  </section>
  <section class="panel pdeck">
    <div class="deckrow">
      <div class="transport">
        <button class="key" id="pPrev">PREV</button>
        <button class="key big" id="pPlay">PLAY</button>
        <button class="key" id="pStop">STOP</button>
        <button class="key" id="pNext">NEXT</button>
        <button class="key" id="pRepeat">REPEAT OFF</button>
        <button class="key" id="pShuffle">SHUFFLE</button>
      </div>
      <div class="nowplaying num" id="pNow">NOTHING PLAYING</div>
    </div>
    <div class="seekrow"><span class="num" id="pElapsed">0:00</span><input type="range" id="pSeek" min="0" max="1000" value="0" step="1" disabled><span class="num" id="pRemain">-0:00</span></div>
    <div class="deckrow2">
      <span class="lbl">OUTPUT</span><select id="pDevice" title="Audio output device"></select>
      <span class="num" id="pRate"></span>
      <span class="lbl">VOLUME</span><input type="range" id="pVol" min="0" max="100" value="100" step="1"><span class="num" id="pVolText">100%</span>
      <div class="meter" title="Peak level, left and right"><div id="pMeterL"><i></i></div><div id="pMeterR"><i></i></div></div>
    </div>
    <div class="preview num" id="pStatus"></div>
  </section>`;
$('#pageConvert').after(page);
const fieldsBox = $('#pFields', page);
const inputs = {};
for (const [k, label, kind] of FIELDS) {
  const lab = el('label', { className: kind === 'half' ? 'half' : '' }, label);
  const inp = kind === 'area' ? el('textarea') : el('input', { type: 'text' });
  inp.dataset.pf = k; inp.spellcheck = false; inputs[k] = inp;
  lab.append(inp); fieldsBox.append(lab);
}

// ------------------------------------------------------------------------------------------------- helpers
const hue = (col, row) => ((col + row) % 12) * 30 + 10;
function paint() {
  page.querySelectorAll('.tools, .transport, .artkeys, .deckrow2').forEach((row, r) => row.querySelectorAll('.key, select').forEach((k, c) => k.style.setProperty('--h', hue(c, r))));
  page.querySelectorAll('.metagrid label').forEach((l, i) => l.style.setProperty('--h', hue(i % 2, Math.floor(i / 2) + 2)));
  $('#pArt', page).style.setProperty('--h', hue(0, 8));
  document.querySelectorAll('.pages .key').forEach((k, i) => k.style.setProperty('--h', hue(i + 4, 0)));
  $('#pSeek', page).style.setProperty('--h', hue(2, 5)); $('#pVol', page).style.setProperty('--h', hue(6, 5));
}
const mmss = (s) => { s = Math.max(0, Math.floor(s || 0)); const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), x = s % 60; return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(x).padStart(2, '0'); };
const khz = (r) => (r % 1000 === 0 ? String(r / 1000) : (r / 1000).toFixed(1));
function formatOf(t) {
  if (!t.rate) return '';
  const ext = (t.path.match(/\.([^.\\/]+)$/) || [, ''])[1].toUpperCase().replace('WAVE', 'WAV').replace('AIF', 'AIFF').replace('AIFC', 'AIFF');
  const lossy = /^(mp3|aac|vorbis|opus|wma)/.test(t.codec || '');
  const bits = t.float ? '32f' : t.bits || '';
  return lossy || !bits ? `${khz(t.rate)}k ${ext}` : `${khz(t.rate)}k/${bits} ${ext}`;
}
const fileUrl = (p) => 'file://' + (/^[A-Za-z]:/.test(p) ? '/' : '') + encodeURI(p.replace(/\\/g, '/')).replace(/#/g, '%23').replace(/\?/g, '%3F');
function status(msg, bad) { const s = $('#pStatus', page); s.textContent = msg || ''; s.classList.toggle('bad', !!bad); }
function metaMsg(msg, bad) { const s = $('#pMsg', page); s.textContent = msg || ''; s.classList.toggle('bad', !!bad); }

// ------------------------------------------------------------------------------------------------- the playlist
let rows = [];
function renderList() {
  const body = $('#pTable tbody', page);
  body.textContent = '';
  rows = S.tracks.map((t, i) => {
    const tr = el('tr', { className: 'row', draggable: true });
    tr.style.setProperty('--rh', hue(0, i));
    const n = el('td', { className: 'n', textContent: String(i + 1) });
    tr.append(n, el('td', { textContent: t.title || t.name, title: t.path }), el('td', { className: 'dim', textContent: t.artist || '' }), el('td', { className: 'dim', textContent: t.album || '' }),
      el('td', { className: 't', textContent: t.duration ? mmss(t.duration) : '' }), el('td', { className: 'fmt', textContent: formatOf(t) }));
    const x = el('button', { className: 'x', textContent: '×', title: 'Remove from the playlist' });
    x.onclick = (e) => { e.stopPropagation(); removeAt(byTid(t.id)); };
    tr.append(el('td', {}, x));
    tr.onclick = () => select(t.id);
    tr.ondblclick = () => { select(t.id); play(byTid(t.id)); };
    tr.ondragstart = (e) => { e.dataTransfer.setData('text/x-row', String(t.id)); e.dataTransfer.effectAllowed = 'move'; };
    tr.ondragover = (e) => { if ([...e.dataTransfer.types].includes('text/x-row')) { e.preventDefault(); tr.classList.add('drag-over'); } };
    tr.ondragleave = () => tr.classList.remove('drag-over');
    tr.ondrop = (e) => {
      tr.classList.remove('drag-over');
      const id = +e.dataTransfer.getData('text/x-row');
      if (!id) return;
      e.preventDefault(); e.stopPropagation();
      const from = byTid(id); let to = byTid(t.id);
      if (from < 0 || from === to) return;
      const [m] = S.tracks.splice(from, 1);
      to = byTid(t.id);
      S.tracks.splice(from < to ? to + 1 : to, 0, m);   // dropped on a row: after it when moving down, before it when moving up
      changed();
    };
    return tr;
  });
  body.append(...rows);
  $('#pDrop', page).hidden = S.tracks.length > 0;
  markRows();
}
function markRows() {
  const ct = cur ? cur.tid : 0;
  rows.forEach((tr, i) => {
    const t = S.tracks[i];
    tr.classList.toggle('sel', t.id === selTid);
    tr.classList.toggle('now', t.id === ct);
    tr.classList.toggle('missing', !!t.missing);
    tr.firstChild.textContent = t.id === ct && state !== 'stopped' ? '▶' : String(i + 1);
  });
}
// a change to the list (order, content): what is queued next may no longer be right
function changed() { fixShuffle(); renderList(); invalidateNext(); updateUi(); save(); }
function removeAt(i) {
  if (i < 0) return;
  const t = S.tracks[i];
  if (cur && cur.tid === t.id) stop();
  S.tracks.splice(i, 1);
  if (selTid === t.id) { selTid = 0; loadMeta(); }
  changed();
}
async function addPaths(paths) {
  paths = paths.filter(Boolean);
  if (!paths.length) return;
  status('READING FILES…');
  let got;
  try { got = await API.probe(paths); } catch (e) { status('Could not read those files: ' + (e.message || e), true); return; }
  const ok = got.filter((t) => t && t.ok);
  for (const t of ok) { t.id = nextTid++; S.tracks.push(t); }
  const bad = got.length - ok.length;
  status(ok.length ? `ADDED ${ok.length} TRACK${ok.length === 1 ? '' : 'S'}${bad ? ` · ${bad} SKIPPED (not readable as audio)` : ''}` : (bad ? `${bad} FILE${bad === 1 ? '' : 'S'} SKIPPED (not readable as audio)` : 'NO AUDIO FILES FOUND'), !ok.length);
  if (!selTid && ok.length) select(ok[0].id);
  changed();
}
function select(tid, done) { if (selTid === tid) return; selTid = tid; markRows(); loadMeta(done); }

$('#pAddFiles', page).onclick = async () => addPaths(await API.pickFiles(false));
$('#pAddFolder', page).onclick = async () => { const f = await API.pickFolder(); if (f) addPaths([f]); };
$('#pLoadList', page).onclick = async () => addPaths(await API.pickFiles(true));
$('#pSaveList', page).onclick = async () => {
  const r = await API.saveM3u(S.tracks.map((t) => ({ path: t.path, title: t.title, artist: t.artist, duration: t.duration })));
  if (r.ok) status('SAVED ' + r.file); else if (!r.canceled) status('Could not save the playlist: ' + r.error, true);
};
$('#pClear', page).onclick = () => { stop(); S.tracks = []; selTid = 0; loadMeta(); changed(); status(''); };

// files dropped on the window: while this page is showing they are the player's (the converter does not see them)
const playing = () => !page.hidden;
document.addEventListener('dragover', (e) => { if (playing() && [...e.dataTransfer.types].includes('Files')) { e.preventDefault(); e.stopPropagation(); document.body.classList.add('dragging'); } }, true);
document.addEventListener('drop', (e) => {
  if (!playing() || !e.dataTransfer.files.length) return;
  e.preventDefault(); e.stopPropagation();
  document.body.classList.remove('dragging');
  addPaths([...e.dataTransfer.files].map((f) => API.pathFor(f)));
}, true);

// ------------------------------------------------------------------------------------------------- the audio
let ctx = null, node = null, gain = null;
let state = 'stopped';          // stopped | playing | paused
let gen = 0;                    // changes every time playback is (re)started: older async work gives up
let epoch = 0;                  // changes every flush of the audio thread: older reports are ignored
let feedId = 1;
let cur = null, nxt = null;     // the streams: playing / queued behind it (gapless)
let nxtOpening = false, nxtDone = false, nxtToken = 0;
let pushed = 0, consumed = 0;   // frames sent to / taken by the audio thread since the last flush
let position = 0;               // seconds into the current track
let scrub = false, pendingSeek = null, seekTimer = 0, failures = 0;

const gainFor = (v) => (v / 100) * (v / 100);
function route() {
  if (!ctx || !node) return;
  node.disconnect(); gain.disconnect();
  if (S.volume >= 100) node.connect(ctx.destination);   // unity: the worklet's samples reach the output untouched
  else { gain.gain.value = gainFor(S.volume); node.connect(gain); gain.connect(ctx.destination); }
}
async function disposeCtx() {
  const c = ctx; ctx = null; node = null; gain = null;
  if (c) { try { await c.close(); } catch (e) {} }
}
async function ensureCtx(rate) {
  if (ctx && ctx.sampleRate === rate && ctx.state !== 'closed') return;
  await disposeCtx();
  const opts = { sampleRate: rate, latencyHint: 'playback' };
  let c;
  try { c = new AudioContext(S.device ? { ...opts, sinkId: S.device } : opts); }
  catch (e) { c = new AudioContext(opts); if (S.device) status('That output is not available: using the system default.', true); }
  if (S.device && c.setSinkId && c.sinkId !== S.device) { try { await c.setSinkId(S.device); } catch (e) {} }
  if (c.sampleRate !== rate) { try { await c.close(); } catch (e) {} throw new Error(`The audio engine cannot run at ${rate} Hz here.`); }
  await c.audioWorklet.addModule('player-worklet.js');
  ctx = c;
  node = new AudioWorkletNode(c, 'pump-player', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
  node.port.onmessage = onWorkletMsg;
  gain = c.createGain();
  route();
  showRate();
}
function flushWorklet() { epoch++; pushed = 0; consumed = 0; node.port.postMessage({ type: 'flush', epoch }); }
function showRate() {
  const sel = $('#pDevice', page);
  const name = sel.selectedOptions[0] ? sel.selectedOptions[0].textContent : '';
  $('#pRate', page).textContent = ctx ? `${ctx.sampleRate} Hz · FLOAT32 · ${name}` : 'THE ENGINE RUNS AT EACH TRACK\'S OWN RATE';
}

async function openFor(track, startSec) {
  const r = await API.open(track.path, startSec, track.channels);
  if (!r.ok) throw new Error(r.error);
  return { id: feedId++, tid: track.id, track, sid: r.id, rate: r.rate, ch: r.channels, eof: false, start: Math.round(startSec * r.rate), sent: 0, dead: false };
}
function closeStream(s) { if (s && !s.dead) { s.dead = true; API.close(s.sid); } }
function dropStreams() { nxtToken++; closeStream(cur); closeStream(nxt); cur = null; nxt = null; nxtOpening = false; nxtDone = false; }

async function startTrack(i, startSec = 0, opts = {}) {   // opts: { paused, chain: moving on by itself, so a bad file is skipped }
  const t = S.tracks[i];
  if (!t) return;
  const my = ++gen;
  clearTimeout(seekTimer); pendingSeek = null;
  dropStreams();
  if (node) flushWorklet();   // the old track stops now, not when the new one is ready
  let s;
  try { s = await openFor(t, startSec); } catch (e) { return failed(t, e, my, opts); }
  if (my !== gen) { closeStream(s); return; }
  try { await ensureCtx(s.rate); } catch (e) { closeStream(s); return failed(t, e, my, opts); }
  if (my !== gen) { closeStream(s); return; }
  flushWorklet();
  cur = s; position = startSec; failures = 0;
  if (opts.paused) { state = 'paused'; await ctx.suspend(); }
  else { state = 'playing'; if (ctx.state !== 'running') await ctx.resume(); }
  if (my !== gen) return;
  status('');
  markRows(); updateUi(); kick();
}
function failed(t, e, my, opts) {
  if (my !== gen) return;
  t.missing = true; markRows();
  status(`${t.title || t.name}: ${String(e.message || e).slice(0, 300)}`, true);
  failures++;
  // moving on by itself: skip the bad file, but never go round and round when every file is bad
  const n = nextIndex(false, byTid(t.id));
  if (opts.chain && failures < S.tracks.length && n != null && n !== byTid(t.id)) startTrack(n, 0, { chain: true });
  else stop(true);
}
function play(i) { if (i >= 0) startTrack(i); }
async function pause() {
  if (state !== 'playing' || !ctx) return;
  state = 'paused'; peakL = peakR = 0; drawMeter(); updateUi(); markRows();
  try { await ctx.suspend(); } catch (e) {}
}
async function resume() {
  if (state !== 'paused' || !ctx) return;
  state = 'playing'; updateUi(); markRows();
  try { await ctx.resume(); } catch (e) {}
  kick();
}
function toggle() {
  if (state === 'playing') pause();
  else if (state === 'paused') resume();
  else { const i = byTid(selTid); const n = S.tracks.length; if (n) startTrack(i >= 0 ? i : 0); }
}
function stop(keepStatus) {
  gen++; clearTimeout(seekTimer); pendingSeek = null;
  dropStreams();
  state = 'stopped'; position = 0;
  if (ctx && node) { flushWorklet(); ctx.suspend().catch(() => {}); }
  if (!keepStatus) status('');
  markRows(); updateUi();
}
const playingTid = () => (cur ? cur.tid : 0);

// where playback goes after the current track
function fixShuffle() {
  const ids = new Set(S.tracks.map((t) => t.id));
  shufOrder = shufOrder.filter((id) => ids.has(id));
  const have = new Set(shufOrder);
  const add = S.tracks.map((t) => t.id).filter((id) => !have.has(id));
  for (let i = add.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [add[i], add[j]] = [add[j], add[i]]; }
  shufOrder.push(...add);
}
function order() { return S.shuffle ? shufOrder.map(byTid).filter((i) => i >= 0) : S.tracks.map((_, i) => i); }
function nextIndex(auto, from) {
  const n = S.tracks.length;
  if (!n) return null;
  const ci = from !== undefined ? from : cur ? byTid(cur.tid) : byTid(selTid);
  if (ci < 0) return 0;
  if (auto && S.repeat === 'one') return ci;
  const ord = order(), p = ord.indexOf(ci);
  if (p + 1 < ord.length) return ord[p + 1];
  return S.repeat === 'all' ? ord[0] : null;
}
function prevIndex() {
  const ci = cur ? byTid(cur.tid) : byTid(selTid);
  const ord = order(), p = ord.indexOf(ci);
  if (p > 0) return ord[p - 1];
  return S.repeat === 'all' && ord.length ? ord[ord.length - 1] : ci;
}
function next() { const n = nextIndex(false); if (n == null) stop(); else startTrack(n); }
function prev() { if (state !== 'stopped' && position > 3) startTrack(byTid(playingTid())); else { const p = prevIndex(); if (p != null && p >= 0) startTrack(p); } }

// what is queued behind the current track is dropped when the list or the modes change
function invalidateNext() {
  if (!cur) return;
  nxtToken++;
  if (nxt) { pushed -= nxt.sent; closeStream(nxt); nxt = null; node.port.postMessage({ type: 'drop', keep: cur.id }); }
  nxtOpening = false; nxtDone = false;
  kick();
}

// ---- feeding the audio thread
let feeding = false, again = false;
function kick() {
  if (feeding) { again = true; return; }
  feeding = true;
  (async () => { try { do { again = false; await feedOnce(); } while (again); } catch (e) { status('Playback error: ' + (e.message || e), true); } finally { feeding = false; } })();
}
async function openNext(my) {
  const idx = nextIndex(true);
  const t = idx == null ? null : S.tracks[idx];
  if (!t || (t.rate && t.rate !== ctx.sampleRate)) { nxtDone = true; return; }   // the end, or another rate: a restart at the next track's rate
  const token = ++nxtToken;
  nxtOpening = true;
  let s = null;
  try { s = await openFor(t, 0); } catch (e) { s = null; }
  nxtOpening = false;
  if (my !== gen || token !== nxtToken) { closeStream(s); return; }
  if (!s || s.rate !== ctx.sampleRate) { closeStream(s); nxtDone = true; return; }
  nxt = s;
}
async function feedOnce() {
  const my = gen;
  for (;;) {
    if (my !== gen || !cur || !ctx || !node || state === 'stopped') return;
    if (pushed - consumed >= TARGET_SEC * ctx.sampleRate) return;
    let s = !cur.eof ? cur : (nxt && !nxt.eof ? nxt : null);
    if (!s) {
      if (cur.eof && !nxt && !nxtOpening && !nxtDone) {
        await openNext(my);
        if (my === gen && nxtDone && cur && cur.ended) { afterEnd(); return; }   // the queued track could not be opened and the current one is over
        continue;
      }
      return;
    }
    const buf = await API.read(s.sid, CHUNK_FRAMES * s.ch * 4, s.ch);
    if (my !== gen || s.dead) return;
    if (!buf || !buf.byteLength) {
      s.eof = true;
      node.port.postMessage({ type: 'push', id: s.id, data: new ArrayBuffer(0), ch: s.ch, frames: 0, start: s.start + s.sent, last: true });
      API.close(s.sid);   // ffmpeg is done (the queue keeps its own copy of the data)
      continue;
    }
    const ab = buf.byteOffset === 0 && buf.byteLength === buf.buffer.byteLength ? buf.buffer : buf.slice().buffer;
    const frames = ab.byteLength / (4 * s.ch);
    node.port.postMessage({ type: 'push', id: s.id, data: ab, ch: s.ch, frames, start: s.start + s.sent, last: false }, [ab]);
    s.sent += frames; pushed += frames;
  }
}

// ---- reports from the audio thread
let peakL = 0, peakR = 0, clipUntil = 0;
function onWorkletMsg(e) {
  const m = e.data;
  if (m.epoch !== epoch) return;
  if (m.type === 'pos') {
    consumed = m.consumed;
    if (cur && m.id === cur.id) position = m.frame / cur.rate;
    peakL = Math.max(m.pkL, peakL * 0.8); peakR = Math.max(m.pkR, peakR * 0.8);
    if (m.pkL >= 1 || m.pkR >= 1) clipUntil = performance.now() + 700;
    drawTime(); drawMeter();
    if (pushed - consumed < TARGET_SEC * 0.75 * ctx.sampleRate) kick();
  } else if (m.type === 'started') {
    if (cur && m.id === cur.id) return;
    if (nxt && m.id === nxt.id) {   // gapless: the queued track has begun
      cur = nxt; nxt = null; nxtDone = false; position = 0; failures = 0;
      markRows(); updateUi(); kick();
    }
  } else if (m.type === 'ended') {
    if (!cur || m.id !== cur.id) return;
    if (nxt || nxtOpening) { cur.ended = true; return; }   // gapless: the queued track follows by itself
    afterEnd();
  }
}
function afterEnd() {
  const n = nextIndex(true);
  if (n == null) stop(); else startTrack(n, 0, { chain: true });
}

// ---- seeking
function seekTo(sec) {
  if (!cur || state === 'stopped') return;
  const t = S.tracks[byTid(cur.tid)];
  const dur = t && t.duration ? t.duration : Infinity;
  sec = Math.min(Math.max(0, sec), Math.max(0, dur - 0.1));
  startTrack(byTid(cur.tid), sec, { paused: state === 'paused' });
}
function seekBy(delta) {   // key repeats collapse into one restart of the decoder
  if (!cur || state === 'stopped') return;
  pendingSeek = Math.max(0, (pendingSeek == null ? position : pendingSeek) + delta);
  position = pendingSeek; drawTime();
  clearTimeout(seekTimer);
  seekTimer = setTimeout(() => { const s = pendingSeek; pendingSeek = null; seekTo(s); }, 150);
}

// ------------------------------------------------------------------------------------------------- transport UI
function drawTime() {
  const t = cur ? S.tracks[byTid(cur.tid)] : null;
  const dur = t && t.duration ? t.duration : 0;
  const shown = scrub ? +$('#pSeek', page).value / 1000 * dur : position;
  $('#pElapsed', page).textContent = mmss(shown);
  $('#pRemain', page).textContent = dur ? '-' + mmss(dur - shown) : '';
  if (!scrub) $('#pSeek', page).value = dur ? Math.min(1000, Math.round(position / dur * 1000)) : 0;
}
const meterPct = (pk) => Math.max(0, Math.min(100, (20 * Math.log10(pk + 1e-9) + 60) / 60 * 100));
function drawMeter() {
  $('#pMeterL i', page).style.width = meterPct(peakL) + '%';
  $('#pMeterR i', page).style.width = meterPct(peakR) + '%';
  const clip = performance.now() < clipUntil;
  $('#pMeterL', page).classList.toggle('clip', clip); $('#pMeterR', page).classList.toggle('clip', clip);
}
function updateUi() {
  const t = cur ? S.tracks[byTid(cur.tid)] : null;
  const p = $('#pPlay', page);
  p.textContent = state === 'playing' ? 'PAUSE' : 'PLAY';
  p.classList.toggle('on', state === 'playing');
  $('#pSeek', page).disabled = state === 'stopped';
  $('#pNow', page).textContent = t && state !== 'stopped' ? `${[t.artist, t.title].filter(Boolean).join(' — ') || t.name}   ·   ${formatOf(t)}` : 'NOTHING PLAYING';
  $('#pRepeat', page).textContent = 'REPEAT ' + S.repeat.toUpperCase();
  $('#pRepeat', page).classList.toggle('on', S.repeat !== 'off');
  $('#pShuffle', page).classList.toggle('on', S.shuffle);
  if (state === 'stopped') { peakL = peakR = 0; drawMeter(); }
  drawTime(); showRate();
}
$('#pPlay', page).onclick = toggle;
$('#pStop', page).onclick = () => stop();
$('#pNext', page).onclick = next;
$('#pPrev', page).onclick = prev;
$('#pRepeat', page).onclick = () => { S.repeat = { off: 'all', all: 'one', one: 'off' }[S.repeat]; invalidateNext(); updateUi(); save(); };
$('#pShuffle', page).onclick = () => { S.shuffle = !S.shuffle; if (S.shuffle) { shufOrder = []; fixShuffle(); const c = cur ? cur.tid : 0; if (c) { shufOrder = [c, ...shufOrder.filter((x) => x !== c)]; } } invalidateNext(); updateUi(); save(); };
const seek = $('#pSeek', page);
seek.oninput = () => { scrub = true; drawTime(); };
seek.onchange = () => {
  const t = cur ? S.tracks[byTid(cur.tid)] : null;
  const to = t && t.duration ? +seek.value / 1000 * t.duration : 0;
  scrub = false; position = to; drawTime(); seekTo(to);
};
const vol = $('#pVol', page);
vol.oninput = () => {
  const was = S.volume >= 100;
  S.volume = +vol.value;
  $('#pVolText', page).textContent = S.volume + '%';
  if (ctx) { if (was !== (S.volume >= 100)) route(); else if (gain) gain.gain.value = gainFor(S.volume); }
  save();
};

// ------------------------------------------------------------------------------------------------- output devices
async function refreshDevices() {
  let list = [];
  try { list = (await navigator.mediaDevices.enumerateDevices()).filter((d) => d.kind === 'audiooutput'); } catch (e) {}
  const sel = $('#pDevice', page);
  sel.textContent = '';
  const def = list.find((d) => d.deviceId === 'default');
  sel.append(el('option', { value: '', textContent: def && def.label ? 'SYSTEM DEFAULT · ' + def.label.replace(/^Default - /, '') : 'SYSTEM DEFAULT' }));
  let n = 0;
  for (const d of list) {
    if (d.deviceId === 'default' || d.deviceId === 'communications') continue;
    n++;
    sel.append(el('option', { value: d.deviceId, textContent: d.label || 'OUTPUT ' + n }));
  }
  if (S.device && ![...sel.options].some((o) => o.value === S.device)) {   // the chosen device is gone (unplugged)
    S.device = '';
    if (ctx) { try { await ctx.setSinkId(''); } catch (e) {} }
    status('The chosen output is no longer there: back on the system default.', true);
    save();
  }
  sel.value = S.device;
  showRate();
}
$('#pDevice', page).onchange = async (e) => {
  S.device = e.target.value;
  if (ctx) { try { await ctx.setSinkId(S.device); status(''); } catch (err) { status('Could not use that output: ' + (err.message || err), true); } }
  showRate(); save();
};
if (navigator.mediaDevices) navigator.mediaDevices.addEventListener('devicechange', refreshDevices);

// ------------------------------------------------------------------------------------------------- the tag editor
let ed = null;   // { tid, file, orig: {field: value}, cover: 'keep' | 'remove' | 'set', coverPath, coverUrl, writable }
let edToken = 0;
function artShow(url) {
  const a = $('#pArt', page);
  a.style.backgroundImage = url ? `url("${url}")` : '';
  a.classList.toggle('has', !!url);
}
function edChanges() {
  const c = {};
  if (ed) for (const [k] of FIELDS) if (inputs[k].value !== ed.orig[k]) c[k] = inputs[k].value;
  return c;
}
function edDirty() { return !!ed && (Object.keys(edChanges()).length > 0 || ed.cover !== 'keep'); }
function edRefresh() {
  for (const [k] of FIELDS) inputs[k].classList.toggle('changed', !!ed && inputs[k].value !== ed.orig[k]);
  $('#pTagSave', page).disabled = !ed || !ed.writable || !edDirty();
  $('#pTagRevert', page).disabled = !edDirty();
  $('#pArtPick', page).disabled = !ed || !ed.writable; $('#pArtNone', page).disabled = !ed || !ed.writable;
  $('#pReveal', page).disabled = !ed;
}
async function loadMeta(done) {   // done: a message to show once the tags are there
  const token = ++edToken;
  const t = S.tracks[byTid(selTid)];
  ed = null; artShow('');
  for (const [k] of FIELDS) { inputs[k].value = ''; inputs[k].disabled = true; }
  metaMsg('');
  if (!t) { $('#pFile', page).textContent = 'SELECT A TRACK'; edRefresh(); return; }
  $('#pFile', page).textContent = t.path;
  edRefresh();
  let r;
  try { r = await API.tags(t.path); } catch (e) { r = { ok: false, error: String(e.message || e) }; }
  if (token !== edToken) return;
  if (!r.ok) { metaMsg('Could not read the tags: ' + (r.error || 'unknown error'), true); return; }
  ed = { tid: t.id, file: t.path, orig: {}, cover: 'keep', coverPath: '', writable: r.writable };
  for (const [k] of FIELDS) { inputs[k].value = r[k] || ''; inputs[k].disabled = !r.writable; ed.orig[k] = r[k] || ''; }
  artShow(r.cover || '');
  ed.coverUrl = r.cover || '';
  metaMsg(done || (r.writable ? '' : 'Tags of this kind of file cannot be written here (WAV, AIFF, FLAC, MP3 and M4A can).'));
  edRefresh();
}
for (const [k] of FIELDS) inputs[k].addEventListener('input', edRefresh);
$('#pTagRevert', page).onclick = () => {
  if (!ed) return;
  for (const [k] of FIELDS) inputs[k].value = ed.orig[k];
  ed.cover = 'keep'; ed.coverPath = ''; artShow(ed.coverUrl); metaMsg(''); edRefresh();
};
$('#pArtPick', page).onclick = $('#pArt', page).onclick = async () => {
  if (!ed || !ed.writable) return;
  const p = await API.pickImage();
  if (!p) return;
  ed.cover = 'set'; ed.coverPath = p; artShow(fileUrl(p)); edRefresh();
};
$('#pArtNone', page).onclick = () => { if (!ed) return; ed.cover = 'remove'; ed.coverPath = ''; artShow(''); edRefresh(); };
$('#pReveal', page).onclick = () => { if (ed) API.reveal(ed.file); };
$('#pTagSave', page).onclick = async () => {
  if (!ed || !edDirty()) return;
  const e = ed, i = byTid(e.tid);
  const changes = edChanges();
  const cover = e.cover === 'set' ? { action: 'set', path: e.coverPath } : { action: e.cover };
  // the file is being read by the decoder: stop it first (it would also block the replacement on Windows)
  const inUse = (cur && cur.tid === e.tid) || (nxt && nxt.tid === e.tid);
  if (inUse) { stop(); metaMsg('Playback stopped to write the file.'); }
  $('#pTagSave', page).disabled = true; metaMsg('WRITING…');
  let r;
  try { r = await API.writeTags(e.file, changes, cover); } catch (err) { r = { ok: false, error: String(err.message || err) }; }
  if (!r.ok) { metaMsg('Not saved: ' + r.error, true); edRefresh(); return; }
  if (i >= 0 && S.tracks[i]) Object.assign(S.tracks[i], Object.fromEntries(keep.filter((k) => r.track[k] !== undefined).map((k) => [k, r.track[k]])), { missing: false });
  invalidateNext(); renderList(); save();
  edToken++;
  if (selTid === e.tid) { selTid = 0; select(e.tid, 'SAVED: audio untouched (stream copy), checked and swapped in.'); }
};

// ------------------------------------------------------------------------------------------------- pages and keys
function setPage(p) {
  const play = p === 'play';
  $('#pageConvert').hidden = play; page.hidden = !play;
  $('#pgConvert').classList.toggle('cur', !play); $('#pgPlay').classList.toggle('cur', play);
  document.body.classList.remove('dragging');
  if (play) { paint(); refreshDevices(); }
}
$('#pgConvert').onclick = () => setPage('convert');
$('#pgPlay').onclick = () => setPage('play');
document.addEventListener('keydown', (e) => {
  if (!playing()) return;
  const tag = e.target.tagName;
  const typing = tag === 'TEXTAREA' || tag === 'SELECT' || (tag === 'INPUT' && !['range', 'checkbox', 'button'].includes(e.target.type));
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.stopImmediatePropagation(); return; }   // the converter's shortcut
  if (e.key === 'Delete' && !typing) { e.stopImmediatePropagation(); e.preventDefault(); const i = byTid(selTid); if (i >= 0) removeAt(i); return; }
  if (typing || e.ctrlKey || e.metaKey || e.altKey) return;
  if (e.key === ' ') { e.preventDefault(); e.stopImmediatePropagation(); toggle(); }
  else if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && e.target.id !== 'pVol') { e.preventDefault(); e.stopImmediatePropagation(); seekBy(e.key === 'ArrowRight' ? 5 : -5); }
  else if (e.key === 'Enter' && tag !== 'BUTTON') { const i = byTid(selTid); if (i >= 0) { e.preventDefault(); play(i); } }
}, true);

// ------------------------------------------------------------------------------------------------- start
(async () => {
  paint();
  let st = null;
  try { st = await API.load(); } catch (e) {}
  if (st && Array.isArray(st.tracks)) {
    S.tracks = st.tracks.filter((t) => t && t.path).map((t) => ({ ...t, id: nextTid++ }));
    S.repeat = ['off', 'all', 'one'].includes(st.repeat) ? st.repeat : 'off';
    S.shuffle = !!st.shuffle;
    S.volume = Number.isFinite(st.volume) ? Math.min(100, Math.max(0, st.volume)) : 100;
    S.device = typeof st.device === 'string' ? st.device : '';
    try { const ex = await API.exists(S.tracks.map((t) => t.path)); S.tracks.forEach((t, i) => { t.missing = !ex[i]; }); } catch (e) {}
  }
  vol.value = S.volume; $('#pVolText', page).textContent = S.volume + '%';
  fixShuffle(); renderList(); updateUi(); edRefresh();
  await refreshDevices();
})();
})();
