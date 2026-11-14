// pump CONVERT, PLAY page: the Node side of the player (used by main.js, never by the window directly).
//   - reading a file's tags, cover and technical description (one ffmpeg run, no decoding)
//   - streaming 32-bit float PCM at the file's own sample rate, in chunks that are pulled by the window (so a long
//     file is never held in memory: ffmpeg is paused whenever the window is not keeping up)
//   - writing tags with stream copy into a temp file next to the original, checking it, then renaming it into place
//   - .m3u8 playlists
// engine.js (the converter) is only read from: probe parser, AUDIO_EXT, the ID3 chunk writer for WAV / AIFF.
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const { fileURLToPath } = require('url');
const E = require('./engine.js');

// ------------------------------------------------------------------------------------------------- ffmpeg
function ff(args) {
  return new Promise((resolve) => {
    const p = spawn(E.getFfmpeg(), args, { windowsHide: true });
    const out = [];
    let err = '';
    p.stdout.on('data', (d) => out.push(d));
    p.stderr.on('data', (d) => { err += d; if (err.length > 2e6) err = err.slice(-1e6); });
    p.on('error', (e) => resolve({ code: -1, out: '', err: String(e.message || e) }));
    p.on('close', (code) => resolve({ code, out: Buffer.concat(out).toString('utf8'), err }));
  });
}
const lastLines = (s, n = 3) => String(s).replace(/\r/g, '').trim().split('\n').slice(-n).join(' | ');

// ------------------------------------------------------------------------------------------------- tags
// ffmpeg's "ffmetadata" text: key=value lines, with \ before = ; # \ and a line end inside a value.
function parseFfmetadata(text) {
  const raw = {};
  const lines = String(text).replace(/\r/g, '').split('\n');
  for (let i = 1; i < lines.length; i++)
  {
    let l = lines[i];
    if (l.startsWith('[')) break;   // [STREAM] / [CHAPTER]: only the file's own tags
    if (!l || l.startsWith(';') || l.startsWith('#')) continue;
    while (/(^|[^\\])(\\\\)*\\$/.test(l) && i + 1 < lines.length) l = l.slice(0, -1) + '\n' + lines[++i];   // continuation
    let k = -1;
    for (let j = 0; j < l.length; j++) { if (l[j] === '\\') j++; else if (l[j] === '=') { k = j; break; } }
    if (k < 1) continue;
    const un = (s) => s.replace(/\\(.)/gs, '$1');
    raw[un(l.slice(0, k)).toLowerCase()] = un(l.slice(k + 1));
  }
  return raw;
}

// the fields the editor shows: its name -> the names ffmpeg reads them under (the first is what is written)
const FIELDS = {
  title: ['title'], artist: ['artist'], album: ['album'], albumArtist: ['album_artist', 'albumartist'], track: ['track', 'tracknumber'],
  year: ['date', 'year'], genre: ['genre'], comment: ['comment'],
};
function fieldsFromRaw(raw) {
  const f = {};
  for (const [k, names] of Object.entries(FIELDS))
  {
    f[k] = '';
    for (const n of names) if (raw[n]) { f[k] = raw[n]; break; }
  }
  return f;
}

async function probeTrack(file) {
  const r = await ff(['-hide_banner', '-nostdin', '-i', file, '-f', 'ffmetadata', '-']);
  const p = E.parseProbe(r.err);
  const base = path.basename(file);
  if (!(p.rate > 0)) return { ok: false, path: file, name: base, error: 'not an audio file ffmpeg can read' };
  const raw = parseFfmetadata(r.out);
  const f = fieldsFromRaw(raw);
  return {
    ok: true, path: file, name: base, ...f,
    title: f.title || base.replace(/\.[^.]+$/, ''),
    duration: p.duration, rate: p.rate, bits: p.bits, float: p.float, channels: p.channels, codec: p.codec,
    hasCover: /Video: [^\n]*attached pic/.test(r.err),
    writable: !!writableFormat(file), raw,
  };
}

async function extractCover(file) {
  const r = await new Promise((resolve) => {
    const p = spawn(E.getFfmpeg(), ['-hide_banner', '-nostdin', '-loglevel', 'error', '-i', file, '-map', '0:v:0', '-c', 'copy', '-frames:v', '1', '-f', 'image2pipe', '-'], { windowsHide: true });
    const out = [];
    p.stdout.on('data', (d) => out.push(d));
    p.on('error', () => resolve(null));
    p.on('close', () => resolve(Buffer.concat(out)));
  });
  return r && r.length > 8 ? r : null;
}
const sniffMime = (b) => (b[0] === 0x89 && b[1] === 0x50 ? 'image/png' : 'image/jpeg');

// everything the editor needs for one file
async function readTags(file) {
  const t = await probeTrack(file);
  if (!t.ok) return t;
  if (t.hasCover)
  {
    const img = await extractCover(file);
    if (img) t.cover = `data:${sniffMime(img)};base64,${img.toString('base64')}`;
  }
  return t;
}

// ------------------------------------------------------------------------------------------------- writing tags
// WAV / AIFF: ffmpeg copies the audio (and writes the INFO list of a WAV), then the ID3 chunk (all the tags and the
// cover) is added the way CONVERT does it. FLAC / MP3 / M4A: ffmpeg writes everything, cover as an attached picture.
const EXT_FORMAT = { '.wav': 'wav', '.wave': 'wav', '.aif': 'aiff', '.aiff': 'aiff', '.flac': 'flac', '.mp3': 'mp3', '.m4a': 'm4a' };
const MUXER = { wav: 'wav', aiff: 'aiff', flac: 'flac', mp3: 'mp3', m4a: 'ipod' };
function writableFormat(file) { return EXT_FORMAT[path.extname(file).toLowerCase()] || ''; }

// the key a field is written under (FLAC wants the Vorbis names; ffmpeg would write "album_artist" and "track" as they are)
function writeKey(fmt, field) {
  if (fmt === 'flac') return { albumArtist: 'ALBUMARTIST', track: 'TRACKNUMBER', year: 'DATE' }[field] || FIELDS[field][0].toUpperCase();
  return FIELDS[field][0];
}

// ffmpeg arguments that copy the audio and set only the fields that changed ('' removes one). The tags of the file that
// are not edited here (replay gain, composer, ISRC ...) travel with -map_metadata 0.
function buildTagArgs({ file, out, fmt, raw, hasCover, changes, cover }) {
  const set = cover && cover.action === 'set';
  const a = ['-hide_banner', '-nostdin', '-y', '-i', file];
  if (set && (fmt === 'flac' || fmt === 'mp3' || fmt === 'm4a')) a.push('-i', cover.path);
  a.push('-map', '0:a');
  const picture = fmt === 'flac' || fmt === 'mp3' || fmt === 'm4a';
  if (picture)
  {
    if (set) a.push('-map', '1:v:0');
    else if (!cover || cover.action === 'keep') { if (hasCover) a.push('-map', '0:v?'); }
  }
  a.push('-c', 'copy', '-map_metadata', '0', '-map_chapters', '-1');
  if (picture && set) a.push('-disposition:v:0', 'attached_pic', '-metadata:s:v', 'title=Album cover', '-metadata:s:v', 'comment=Cover (front)');
  if (fmt === 'mp3') a.push('-id3v2_version', '3');
  if (fmt === 'wav' && ['description', 'originator', 'originator_reference', 'origination_date', 'origination_time', 'time_reference', 'umid', 'coding_history'].some((k) => raw[k] !== undefined)) a.push('-write_bext', '1');
  const dels = [], sets = [];
  for (const [field, value] of Object.entries(changes || {}))
  {
    if (!FIELDS[field]) continue;
    const key = writeKey(fmt, field);
    for (const alias of FIELDS[field]) if (raw[alias] !== undefined && alias.toLowerCase() !== key.toLowerCase()) dels.push(alias);
    sets.push([key, String(value)]);
  }
  for (const k of dels) a.push('-metadata', `${k}=`);
  for (const [k, v] of sets) a.push('-metadata', `${k}=${v}`);
  a.push('-f', MUXER[fmt], out);
  return a;
}

// the generic tag set the ID3 chunk of a WAV / AIFF is built from
const ID3_SKIP = new Set(['encoder', 'major_brand', 'minor_version', 'compatible_brands', 'creation_time', 'description', 'originator', 'originator_reference',
  'origination_date', 'origination_time', 'time_reference', 'umid', 'coding_history']);
function id3Tags(raw, changes) {
  const t = {};
  const known = { title: 'title', artist: 'artist', album: 'album', album_artist: 'album_artist', albumartist: 'album_artist', date: 'date', year: 'date', track: 'track',
    tracknumber: 'track', disc: 'disc', genre: 'genre', composer: 'composer', publisher: 'publisher', copyright: 'copyright', isrc: 'isrc', tsrc: 'isrc', comment: 'comment' };
  for (const [k, v] of Object.entries(raw))
  {
    if (!v || ID3_SKIP.has(k)) continue;
    t[known[k] || 'custom:' + k.toUpperCase().replace(/[^A-Z0-9_ -]/g, '')] = v;
  }
  const generic = { title: 'title', artist: 'artist', album: 'album', albumArtist: 'album_artist', track: 'track', year: 'date', genre: 'genre', comment: 'comment' };
  for (const [field, value] of Object.entries(changes || {}))
  {
    if (!generic[field]) continue;
    if (String(value) === '') delete t[generic[field]]; else t[generic[field]] = String(value);
  }
  return t;
}

// chunk ids of a WAV / AIFF that ffmpeg would not carry over (cue points, markers, loops, iXML ...)
function lostChunks(file, fmt) {
  const ok = fmt === 'wav' ? new Set(['fmt ', 'data', 'LIST', 'id3 ', 'ID3 ', 'bext', 'fact', 'JUNK', 'PAD ', 'ds64'])
    : new Set(['COMM', 'SSND', 'ID3 ', 'id3 ', 'NAME', 'AUTH', '(c) ', 'ANNO', 'FVER']);
  const be = fmt === 'aiff';
  const fd = fs.openSync(file, 'r');
  const bad = [];
  try
  {
    const size = fs.fstatSync(fd).size;
    const h = Buffer.alloc(12);
    fs.readSync(fd, h, 0, 12, 0);
    let pos = 12;
    const c = Buffer.alloc(8);
    for (let i = 0; i < 4000 && pos + 8 <= size; i++)
    {
      fs.readSync(fd, c, 0, 8, pos);
      const id = c.toString('latin1', 0, 4);
      const n = be ? c.readUInt32BE(4) : c.readUInt32LE(4);
      if (!ok.has(id) && !bad.includes(id)) bad.push(id);
      if (n === 0xffffffff) break;   // RF64: the real size lives in ds64; nothing after the audio is checked
      pos += 8 + n + (n & 1);
    }
  }
  finally { fs.closeSync(fd); }
  return bad.map((s) => s.trim());
}

const md5Of = async (file) => {
  const r = await ff(['-hide_banner', '-nostdin', '-loglevel', 'error', '-i', file, '-map', '0:a', '-c', 'copy', '-f', 'md5', '-']);
  return r.code === 0 ? r.out.trim() : '';
};
const norm = (s) => String(s || '').replace(/\r/g, '').trim();

// changes: { title, artist, album, albumArtist, track, year, genre, comment } (only what changed; '' clears);
// cover: { action: 'keep' | 'remove' | 'set', path }
async function writeTags(file, changes, cover) {
  const fmt = writableFormat(file);
  if (!fmt) return { ok: false, error: `Writing tags is not supported for ${path.extname(file) || 'this'} files (WAV, AIFF, FLAC, MP3 and M4A are).` };
  let st;
  try { st = fs.statSync(file); fs.accessSync(file, fs.constants.W_OK); fs.accessSync(path.dirname(file), fs.constants.W_OK); }
  catch (e) { return { ok: false, error: 'The file or its folder is not writable (' + (e.code || e.message) + ').' }; }
  cover = cover || { action: 'keep' };
  const before = await probeTrack(file);
  if (!before.ok) return { ok: false, error: before.error };
  if (fmt === 'wav' || fmt === 'aiff')
  {
    let lost = [];
    try { lost = lostChunks(file, fmt); } catch (e) { return { ok: false, error: 'Could not read the file: ' + e.message }; }
    if (lost.length) return { ok: false, error: `Not written: this ${fmt.toUpperCase()} holds chunks (${lost.join(', ')}) that copying would drop. Nothing was changed.` };
  }
  const dir = path.dirname(file), ext = path.extname(file);
  const tmp = path.join(dir, `.${path.basename(file, ext)}.pump-tags-${process.pid}.tmp${ext}`);
  const tmpFiles = [tmp];
  const cleanup = () => { for (const f of tmpFiles) { try { fs.unlinkSync(f); } catch (e) {} } };
  try
  {
    const srcHash = await md5Of(file);
    if (!srcHash) return { ok: false, error: 'Could not read the audio of the file.' };
    let artwork = '';   // the picture the ID3 chunk of a WAV / AIFF gets
    if (cover.action === 'set') artwork = cover.path;
    else if (cover.action === 'keep' && before.hasCover && (fmt === 'wav' || fmt === 'aiff'))
    {
      const img = await extractCover(file);
      if (img) { artwork = path.join(dir, `.${path.basename(file, ext)}.pump-art-${process.pid}.tmp`); fs.writeFileSync(artwork, img); tmpFiles.push(artwork); }
    }
    if (artwork && cover.action === 'set')
    {
      let img; try { img = fs.readFileSync(artwork); } catch (e) { return { ok: false, error: 'Could not read the artwork file.' }; }
      if (!((img[0] === 0xff && img[1] === 0xd8) || (img[0] === 0x89 && img[1] === 0x50))) return { ok: false, error: 'The artwork must be a JPEG or PNG.' };
    }
    const args = buildTagArgs({ file, out: tmp, fmt, raw: before.raw, hasCover: before.hasCover, changes, cover });
    const r = await ff(args);
    if (r.code !== 0) return { ok: false, error: 'ffmpeg could not write the tags: ' + lastLines(r.err) };
    if (fmt === 'wav' || fmt === 'aiff')
    {
      const tags = id3Tags(before.raw, changes);
      if (Object.keys(tags).length || artwork) E.writeId3Chunk(tmp, tags, artwork || undefined);
    }
    // the check: same audio packets, same description, the new tags read back
    const after = await probeTrack(tmp);
    if (!after.ok) return { ok: false, error: 'The result could not be read back; the original is untouched.' };
    if (after.rate !== before.rate || after.channels !== before.channels || after.codec !== before.codec) return { ok: false, error: 'The result differs from the original (format); the original is untouched.' };
    if (Math.abs(after.duration - before.duration) > Math.max(0.1, before.duration * 0.005)) return { ok: false, error: 'The result has a different length; the original is untouched.' };
    if ((await md5Of(tmp)) !== srcHash) return { ok: false, error: 'The audio of the result differs from the original; the original is untouched.' };
    const want = fieldsFromRaw(after.raw);
    for (const [field, value] of Object.entries(changes || {}))
      if (FIELDS[field] && norm(want[field]) !== norm(value)) return { ok: false, error: `The ${field} did not read back (${JSON.stringify(norm(want[field]))}); the original is untouched.` };
    if (cover.action === 'set' && !after.hasCover && fmt !== 'wav' && fmt !== 'aiff') return { ok: false, error: 'The artwork did not read back; the original is untouched.' };
    if ((fmt === 'wav' || fmt === 'aiff') && cover.action === 'set' && !E.readId3Chunk(tmp).APIC) return { ok: false, error: 'The artwork did not read back; the original is untouched.' };
    try { fs.chmodSync(tmp, st.mode & 0o7777); } catch (e) {}
    fs.renameSync(tmp, file);   // atomic: the old file is replaced in one step
    const track = await probeTrack(file);
    return { ok: true, track };
  }
  catch (e) { return { ok: false, error: String(e && e.message || e) }; }
  finally { cleanup(); }
}

// ------------------------------------------------------------------------------------------------- decoding
// A pull stream: the window asks for n bytes and gets them when ffmpeg has made them. Past HIGH bytes buffered ffmpeg is
// paused (its pipe fills and it blocks), so memory stays small however long the file is.
const HIGH = 8 << 20, LOW = 2 << 20;
class ChunkReader {
  constructor(proc) {
    this.proc = proc; this.bufs = []; this.size = 0; this.ended = false; this.waiter = null; this.paused = false;
    proc.stdout.on('data', (d) => {
      this.bufs.push(d); this.size += d.length;
      if (this.size > HIGH && !this.paused) { this.paused = true; proc.stdout.pause(); }
      this.wake();
    });
    proc.stdout.on('end', () => { this.ended = true; this.wake(); });
    proc.on('close', () => { this.ended = true; this.wake(); });
    proc.on('error', () => { this.ended = true; this.wake(); });
  }
  wake() { const w = this.waiter; if (w) { this.waiter = null; w(); } }
  // up to n bytes (a multiple of `align`); null at the end of the stream
  async pull(n, align) {
    while (this.size < n && !this.ended && !this.closed) await new Promise((res) => { this.waiter = res; });
    if (this.closed) return null;
    let take = Math.min(n, this.size);
    take -= take % align;
    if (take <= 0) return null;
    const out = new Uint8Array(take);   // its own ArrayBuffer: that is what crosses to the window
    let o = 0;
    while (o < take)
    {
      const b = this.bufs[0];
      const k = Math.min(b.length, take - o);
      out.set(b.subarray(0, k), o);
      o += k;
      if (k === b.length) this.bufs.shift(); else this.bufs[0] = b.subarray(k);
    }
    this.size -= take;
    if (this.paused && this.size < LOW) { this.paused = false; this.proc.stdout.resume(); }
    return out;
  }
  close() { this.closed = true; this.ended = true; try { this.proc.kill(); } catch (e) {} this.wake(); }
}

const streams = new Map();
let nextStream = 1;

// Starts decoding at startSec to float32 at the file's own rate (no resampling, no gain). chHint 1 keeps a mono file mono
// (the window plays it on both sides); anything else comes out as stereo (a surround file is folded down by ffmpeg).
async function openStream(file, startSec, chHint) {
  for (let attempt = 0; attempt < 2; attempt++)
  {
    const mono = chHint === 1;
    const args = ['-hide_banner', '-nostdin', '-nostats', ...(startSec > 0 ? ['-ss', String(startSec)] : []), '-i', file, '-map', '0:a:0', '-vn', '-sn', '-dn',
      ...(mono ? [] : ['-ac', '2']), '-f', 'f32le', '-acodec', 'pcm_f32le', 'pipe:1'];
    const proc = spawn(E.getFfmpeg(), args, { windowsHide: true });
    let err = '';
    const reader = new ChunkReader(proc);
    const head = await new Promise((resolve) => {
      const done = (v) => { clearTimeout(timer); resolve(v); };
      const timer = setTimeout(() => done(null), 15000);
      proc.stderr.on('data', (d) => { err += d; if (err.length > 1e6) err = err.slice(-5e5); if (/Output #0/.test(err)) done(E.parseProbe(err)); });
      proc.on('close', () => done(E.parseProbe(err)));
      proc.on('error', (e) => { err += String(e.message || e); done(null); });
    });
    if (!head || !(head.rate > 0)) { reader.close(); throw new Error('ffmpeg could not decode this file: ' + lastLines(err)); }
    const out = mono ? head.channels : 2;
    if (mono && head.channels !== 1 && head.channels !== 2) { reader.close(); chHint = 2; continue; }   // the hint was stale: fold down
    if (!mono && head.channels === 1) { reader.close(); chHint = 1; continue; }
    const id = nextStream++;
    streams.set(id, reader);
    return { id, rate: head.rate, channels: out, srcChannels: head.channels };
  }
  throw new Error('could not set up the decoder');
}
async function readStream(id, bytes, channels) {
  const r = streams.get(id);
  if (!r) return null;
  return r.pull(bytes, 4 * (channels || 2));
}
function closeStream(id) { const r = streams.get(id); if (r) { r.close(); streams.delete(id); } }
function closeAll() { for (const id of [...streams.keys()]) closeStream(id); }

// ------------------------------------------------------------------------------------------------- playlists (.m3u8)
function readM3u(file) {
  let text = fs.readFileSync(file, 'utf8');
  if (text.charCodeAt(0) === 0xfeff) text = text.slice(1);
  const dir = path.dirname(file);
  const out = [];
  let info = null;
  for (let line of text.replace(/\r/g, '').split('\n'))
  {
    line = line.trim();
    if (!line) continue;
    if (line.startsWith('#'))
    {
      const m = line.match(/^#EXTINF:\s*(-?\d+(?:\.\d+)?)\s*,?(.*)$/i);
      if (m) info = { duration: Math.max(0, parseFloat(m[1])), title: m[2].trim() };
      continue;
    }
    let p = line;
    if (/^file:\/\//i.test(p)) { try { p = fileURLToPath(p); } catch (e) { info = null; continue; } }
    else if (/^[a-z][a-z0-9+.-]*:\/\//i.test(p)) { info = null; continue; }   // streams are not files
    if (process.platform === 'win32') p = p.replace(/\//g, '\\');
    if (!path.isAbsolute(p)) p = path.resolve(dir, p);
    out.push({ path: p, duration: info ? info.duration : 0, title: info ? info.title : '' });
    info = null;
  }
  return out;
}
function m3uText(file, tracks) {
  const dir = path.dirname(file);
  const lines = ['#EXTM3U'];
  for (const t of tracks)
  {
    const label = [t.artist, t.title].filter(Boolean).join(' - ').replace(/[\r\n]+/g, ' ');
    lines.push(`#EXTINF:${t.duration > 0 ? Math.round(t.duration) : -1},${label}`);
    const rel = path.relative(dir, t.path);
    lines.push((rel && !path.isAbsolute(rel) ? rel : t.path).split(path.sep).join('/'));
  }
  return lines.join('\n') + '\n';
}
function writeM3u(file, tracks) {
  const tmp = file + '.tmp';
  fs.writeFileSync(tmp, m3uText(file, tracks), 'utf8');
  fs.renameSync(tmp, file);
}

module.exports = { parseFfmetadata, fieldsFromRaw, probeTrack, readTags, extractCover, writableFormat, buildTagArgs, id3Tags, lostChunks, writeTags,
  openStream, readStream, closeStream, closeAll, ChunkReader, readM3u, m3uText, writeM3u, FIELDS, EXT_FORMAT };
