// pump CONVERT: the conversion engine (Node, no Electron): probing sources, planning the jobs (one per source and
// output version), building each job's ffmpeg command, writing tags, running and checking the results.
// Used by main.js (the app) and test.mjs (real conversions with the local ffmpeg).
//
// Quality, the reason the settings are what they are:
//   - sample-rate conversion with the SoX resampler at its very-high-quality setting (28-bit precision, a
//     Chebyshev passband flat to the top of the audio band, linear phase), processing in 64-bit float; where an
//     ffmpeg build lacks it, ffmpeg's own resampler with a 256-tap Kaiser filter (beta 16), which measures the
//     same on test.mjs (residue under -150 dBFS, flat to 20 kHz at 44.1 kHz);
//   - word length: TPDF (high-passed triangular) dither when the result is 16-bit, optionally noise-shaped at 44.1
//     and 48 kHz; 24-bit and float outputs are not dithered (nothing to gain); the lossy encoders get the full-
//     resolution float signal, never a pre-dithered one;
//   - MP3: LAME at its highest encoder quality (320 kbps CBR or V0); AAC: Apple's encoder where it exists (macOS),
//     otherwise the built-in encoder in its best mode;
//   - every output is decoded again afterwards and checked (rate, word length, channels, length, peak), and any
//     sample at or over full scale is reported, since level changes from resampling can clip.
'use strict';
const { spawn, execFile } = require('child_process');
const fs = require('fs');
const path = require('path');

const AUDIO_EXT = ['.wav', '.wave', '.aif', '.aiff', '.aifc', '.flac', '.mp3', '.m4a', '.aac', '.ogg', '.opus', '.caf', '.w64', '.rf64', '.wv', '.ape', '.alac', '.mp4'];
const FORMATS = {
  wav: { ext: 'wav', label: 'WAV', pcm: true, bits: [16, 24, 32] },
  aiff: { ext: 'aif', label: 'AIFF', pcm: true, bits: [16, 24, 32] },
  flac: { ext: 'flac', label: 'FLAC', pcm: true, bits: [16, 24] },
  mp3: { ext: 'mp3', label: 'MP3', pcm: false, maxRate: 48000 },
  aac: { ext: 'm4a', label: 'AAC', pcm: false, maxRate: 48000 },
};
const RATES = [44100, 48000, 88200, 96000, 176400, 192000];
const MP3_MODES = { cbr320: { label: '320 kbps CBR', args: ['-b:a', '320k'] }, v0: { label: 'V0 VBR', args: ['-q:a', '0'] },
  cbr256: { label: '256 kbps CBR', args: ['-b:a', '256k'] }, cbr192: { label: '192 kbps CBR', args: ['-b:a', '192k'] } };
const AAC_RATES = [320, 256, 192];

// ------------------------------------------------------------------------------------------------- ffmpeg
let ffmpegPath = 'ffmpeg';
function setFfmpeg(p) { ffmpegPath = p || 'ffmpeg'; }
function getFfmpeg() { return ffmpegPath; }

function run(args, opts = {}) {
  return new Promise((resolve) => {
    const p = spawn(ffmpegPath, args, { windowsHide: true });
    let out = '', err = '';
    p.stdout.on('data', (d) => { out += d; if (opts.onStdout) opts.onStdout(String(d)); });
    p.stderr.on('data', (d) => { err += d; if (err.length > 2e6) err = err.slice(-1e6); });
    p.on('error', (e) => resolve({ code: -1, out, err: String(e.message || e) }));
    p.on('close', (code) => resolve({ code, out, err }));
    if (opts.onSpawn) opts.onSpawn(p);
  });
}

// What this ffmpeg can do: its version, the SoX resampler, Apple's AAC encoder, LAME.
async function capabilities() {
  const v = await run(['-hide_banner', '-version']);
  if (v.code !== 0) return { ok: false, error: 'ffmpeg was not found (' + v.err.trim().split('\n').pop() + ')' };
  const enc = await run(['-hide_banner', '-encoders']);
  const conf = v.out + v.err;
  return {
    ok: true,
    version: (conf.match(/ffmpeg version (\S+)/) || [])[1] || '?',
    soxr: /--enable-libsoxr/.test(conf),
    lame: /libmp3lame/.test(enc.out),
    appleAac: /\baac_at\b/.test(enc.out),
  };
}

// ------------------------------------------------------------------------------------------------- probing
// Reads ffmpeg's description of a file: codec, rate, channels, word length, duration and its tags.
function parseProbe(text) {
  text = String(text).replace(/\r/g, '');   // Windows line ends
  const r = { codec: '', rate: 0, channels: 0, bits: 0, float: false, duration: 0, tags: {} };
  const d = text.match(/Duration: (\d+):(\d+):(\d+(?:\.\d+)?)/);
  if (d) r.duration = (+d[1]) * 3600 + (+d[2]) * 60 + parseFloat(d[3]);
  const s = text.match(/Stream #\d+:\d+(?:\[[^\]]*\])?(?:\([^)]*\))?: Audio: ([^\n]+)/);
  if (s)
  {
    const line = s[1];
    r.codec = line.split(/[ ,(]/)[0];
    const hz = line.match(/(\d+) Hz/); if (hz) r.rate = +hz[1];
    if (/\bmono\b/.test(line)) r.channels = 1;
    else if (/\bstereo\b/.test(line)) r.channels = 2;
    else { const ch = line.match(/(\d+) channels/); r.channels = ch ? +ch[1] : (/5\.1/.test(line) ? 6 : 2); }
    const bits = line.match(/\((\d+) bit\)/);
    const fmt = (line.match(/Hz, [^,]+, (\w+)/) || [])[1] || '';
    r.float = /^flt|^dbl/.test(fmt) && !/^(mp3|aac|vorbis|opus)/.test(r.codec) || /pcm_f/.test(r.codec);
    if (bits) r.bits = +bits[1];
    else if (/pcm_[su]8/.test(r.codec)) r.bits = 8;
    else if (/pcm_[su]16|^s16/.test(r.codec + ' ' + fmt)) r.bits = 16;
    else if (/pcm_[su]24/.test(r.codec)) r.bits = 24;
    else if (/pcm_[su]32/.test(r.codec)) r.bits = 32;
    else if (/pcm_f32/.test(r.codec)) r.bits = 32;
    else if (/pcm_f64/.test(r.codec)) r.bits = 64;
    else if (/^s16/.test(fmt)) r.bits = 16;
    else if (/^s32/.test(fmt) && /flac|alac/.test(r.codec)) r.bits = 24;
  }
  // the input's tags: the "Metadata:" block before the first stream
  const head = text.split(/Stream #/)[0];
  const m = head.split(/\n\s*Metadata:\s*\n/)[1];
  if (m)
    for (const l of m.split('\n'))
    {
      const kv = l.match(/^\s{4,}([^:]+?)\s*: (.*)$/);
      if (!kv) break;
      r.tags[kv[1].trim().toLowerCase()] = kv[2];
    }
  return r;
}

async function probe(file) {
  const r = await run(['-hide_banner', '-nostdin', '-i', file]);
  const p = parseProbe(r.err);
  p.path = file; p.name = path.basename(file);
  p.ok = p.rate > 0;
  if (!p.ok) p.error = 'not an audio file ffmpeg can read';
  return p;
}

// folders are searched (two levels) for audio files; sorted by name
function expandPaths(paths) {
  const out = [];
  const walk = (p, depth) => {
    let st; try { st = fs.statSync(p); } catch (e) { return; }
    if (st.isDirectory())
    {
      if (depth > 2) return;
      let names = []; try { names = fs.readdirSync(p); } catch (e) { return; }
      names.sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
      for (const n of names) if (!n.startsWith('.')) walk(path.join(p, n), depth + 1);
    }
    else if (AUDIO_EXT.includes(path.extname(p).toLowerCase())) out.push(p);
  };
  for (const p of paths) walk(p, 0);
  return out;
}

// ------------------------------------------------------------------------------------------------- planning
// What a version makes of a source: the rate, the word length and whether it is float.
function resolveOutput(src, v) {
  const f = FORMATS[v.format];
  let rate = +v.rate || src.rate;
  if (f.maxRate && rate > f.maxRate) rate = (rate % 44100 === 0) ? 44100 : 48000;   // lossy: never above 48 kHz
  let bits = 0, float = false;
  if (f.pcm)
  {
    bits = +v.bits || (src.float ? 32 : (src.bits || 24));
    if (!f.bits.includes(bits)) bits = bits > 24 ? (f.bits.includes(32) ? 32 : 24) : (bits <= 16 ? 16 : 24);
    float = bits === 32;
  }
  return { rate, bits, float };
}

const khz = (r) => (r % 1000 === 0 ? String(r / 1000) : (r / 1000).toFixed(1));

// {token} or {token:02} replaced from `vals`; unknown tokens are left as they are
function fill(tpl, vals) {
  return String(tpl || '').replace(/\{(\w+)(?::(\d+))?\}/g, (all, k, pad) => {
    if (!(k in vals)) return all;
    let s = String(vals[k] ?? '');
    if (pad && /^\d+$/.test(s)) s = s.padStart(+pad, '0');
    return s;
  });
}

// a file or folder name that is valid on Windows and macOS
function safeName(s) {
  let t = String(s).replace(/[<>:"/\\|?*\u0000-\u001f]/g, '_').replace(/\s+/g, ' ').trim();
  t = t.replace(/[. ]+$/, '');
  if (/^(con|prn|aux|nul|com\d|lpt\d)$/i.test(t)) t = '_' + t;
  return t.slice(0, 180) || '_';
}

// Every value a template can use for one source and version.
function tokens(src, idx, total, v, out, meta) {
  const f = FORMATS[v.format];
  const qual = v.format === 'mp3' ? (MP3_MODES[v.mp3] || MP3_MODES.cbr320).label : v.format === 'aac' ? (v.aac || 256) + ' kbps' : '';
  return {
    title: src.title || path.parse(src.name || src.path).name,
    artist: src.artist || meta.artist || '', album: meta.album || '', albumartist: meta.albumArtist || meta.artist || '',
    year: meta.year || '', date: meta.year || '', genre: meta.genre || '', label: meta.label || '',
    track: src.track || idx + 1, tracks: total, isrc: src.isrc || '', disc: meta.disc || '',
    format: f.label, ext: f.ext, rate: out.rate, khz: khz(out.rate),
    bits: f.pcm ? (out.float ? '32f' : out.bits) : '', bitdepth: f.pcm ? (out.float ? '32-bit float' : out.bits + '-bit') : qual,
    quality: qual, kbps: v.format === 'mp3' ? ((MP3_MODES[v.mp3] || {}).args || [])[1]?.replace('k', '') || 'VBR' : v.format === 'aac' ? (v.aac || 256) : '',
    spec: f.pcm ? `${out.float ? '32-bit float' : out.bits + '-bit'} / ${khz(out.rate)} kHz` : `${qual} / ${khz(out.rate)} kHz`,
    source: path.parse(src.name || src.path).name, version: v.name || '',
  };
}

// The jobs: one per enabled version and source, with the output path and the tags to write.
function plan(sources, versions, meta) {
  const jobs = [];
  const total = sources.length;
  const usedPaths = new Set();
  versions.filter((v) => v.enabled !== false).forEach((v, vi) => {
    sources.forEach((src, idx) => {
      if (!src.ok) return;
      const out = resolveOutput(src, v);
      const tok = tokens(src, idx, total, v, out, meta);
      const root = v.folder || meta.outputRoot || path.dirname(src.path);
      const sub = (v.subfolder || '').split(/[\\/]/).map((p) => fill(p, tok)).filter((p) => p.trim()).map(safeName);
      const base = safeName(fill(v.name_tpl || '{track:02} {title}', tok)) + '.' + FORMATS[v.format].ext;
      let file = path.join(root, ...sub, base);
      // two sources that would land on the same name get a number
      for (let k = 2; usedPaths.has(file.toLowerCase()); ++k) file = path.join(root, ...sub, safeName(fill(v.name_tpl || '{track:02} {title}', tok)) + ` (${k}).` + FORMATS[v.format].ext);
      usedPaths.add(file.toLowerCase());
      const tags = buildTags(tok, src, meta);
      jobs.push({ id: `${vi}-${idx}`, version: v, versionIndex: vi, source: src, out, file, tags, tok });
    });
  });
  return jobs;
}

// The tags as generic names (mapped per format when written). Custom tags: { key, value } with tokens in either.
function buildTags(tok, src, meta) {
  const t = {};
  const put = (k, v) => { if (v !== undefined && v !== null && String(v).trim() !== '') t[k] = String(v); };
  put('title', fill(meta.titleTpl || '{title}', tok));
  put('artist', tok.artist);
  put('album', tok.album);
  put('album_artist', meta.albumArtist || '');
  put('date', meta.year);
  put('genre', meta.genre);
  put('track', meta.writeTotal === false ? String(tok.track) : `${tok.track}/${tok.tracks}`);
  if (meta.disc) put('disc', meta.disc);
  put('composer', src.composer || meta.composer);
  put('publisher', meta.label);
  put('copyright', fill(meta.copyright || '', tok));
  put('comment', fill(meta.comment || '', tok));
  put('isrc', src.isrc);
  for (const c of meta.custom || [])
  {
    const k = fill(c.key || '', tok).trim().toUpperCase().replace(/[^A-Z0-9_ -]/g, '');
    if (k) put('custom:' + k, fill(c.value || '', tok));
  }
  return t;
}

// ------------------------------------------------------------------------------------------------- commands
function resampleFilter(job, caps) {
  const { rate, bits, float } = job.out;
  const src = job.source;
  const f = FORMATS[job.version.format];
  const toWord = f.pcm && !float && (bits < (src.float ? 33 : src.bits || 32) || src.float);   // a word-length reduction
  const sixteen = f.pcm && !float && bits === 16;
  const parts = [];
  if (caps.soxr) parts.push('resampler=soxr', 'precision=28', 'cheby=1');
  // without it: ffmpeg's own resampler with a long Kaiser filter, measured as good on the same tests (residue under -150 dBFS, flat to 20 kHz at 44.1)
  else parts.push('resampler=swr', 'filter_size=256', 'phase_shift=14', 'cutoff=0.97', 'filter_type=kaiser', 'kaiser_beta=16', 'linear_interp=1');
  parts.push('internal_sample_fmt=dblp', `osr=${rate}`);
  if (sixteen && toWord && job.version.dither !== 'off')
  {
    const shaped = job.version.dither === 'shaped' && (rate === 44100 || rate === 48000);
    parts.push('osf=s16', `dither_method=${shaped ? 'shibata' : 'triangular_hp'}`);
  }
  else if (f.pcm && !float && bits === 24) parts.push('osf=s32');
  else parts.push('osf=fltp');
  return 'aresample=' + parts.join(':');
}

function codecArgs(job, caps, artwork) {
  const { bits, float } = job.out;
  switch (job.version.format)
  {
  case 'wav': return ['-c:a', float ? 'pcm_f32le' : bits === 16 ? 'pcm_s16le' : 'pcm_s24le', '-rf64', 'auto', '-f', 'wav'];
  case 'aiff': return ['-c:a', float ? 'pcm_f32be' : bits === 16 ? 'pcm_s16be' : 'pcm_s24be', '-f', 'aiff'];
  case 'flac': return ['-c:a', 'flac', '-compression_level', '8', '-sample_fmt', bits === 16 ? 's16' : 's32', ...(bits === 24 ? ['-bits_per_raw_sample', '24'] : []), '-f', 'flac'];
  case 'mp3': return ['-c:a', 'libmp3lame', ...(MP3_MODES[job.version.mp3] || MP3_MODES.cbr320).args, '-compression_level', '0', '-id3v2_version', '3', '-write_id3v1', '1', '-f', 'mp3'];
  case 'aac':
  {
    // the iTunes tags always; freeform tags (ISRC, your own) only without artwork: ffmpeg's MP4 writer drops the
    // cover when it writes them (known limitation; the cover wins)
    const mov = ['-f', 'ipod', '-movflags', artwork ? '+faststart' : '+faststart+use_metadata_tags'];
    return caps.appleAac
      ? ['-c:a', 'aac_at', '-b:a', `${job.version.aac || 256}k`, '-aac_at_mode', 'cbr', '-aac_at_quality', '0', ...mov]
      : ['-c:a', 'aac', '-b:a', `${job.version.aac || 256}k`, '-aac_coder', 'twoloop', ...mov];
  }
  }
  return [];
}

// generic tag names -> what each format's writer understands
function metadataArgs(job) {
  const fmt = job.version.format;
  const a = [];
  if (fmt === 'wav' || fmt === 'aiff')
  {
    // WAV: the RIFF INFO list (ffmpeg); both: an ID3 chunk written afterwards (writeId3Chunk), which carries everything
    if (fmt === 'wav')
    {
      const info = { title: 'title', artist: 'artist', album: 'album', date: 'date', genre: 'genre', comment: 'comment', copyright: 'copyright', track: 'track' };
      for (const [k, v] of Object.entries(job.tags)) if (info[k]) a.push('-metadata', `${info[k]}=${v}`);
    }
    return a;
  }
  for (const [k, v] of Object.entries(job.tags))
  {
    let key = k;
    if (k.startsWith('custom:')) key = k.slice(7);
    else if (k === 'isrc') key = fmt === 'mp3' ? 'TSRC' : 'ISRC';
    else if (k === 'publisher' && fmt === 'flac') key = 'LABEL';
    else if (k === 'track' && fmt === 'flac') { const [n, t] = v.split('/'); a.push('-metadata', `TRACKNUMBER=${n}`); if (t) a.push('-metadata', `TRACKTOTAL=${t}`); continue; }
    else if (k === 'disc' && fmt === 'flac') key = 'DISCNUMBER';
    a.push('-metadata', `${key}=${v}`);
  }
  return a;
}

function buildArgs(job, caps, artwork) {
  const fmt = job.version.format;
  const art = artwork && (fmt === 'mp3' || fmt === 'flac' || fmt === 'aac');
  const a = ['-hide_banner', '-nostdin', '-y', '-i', job.source.path];
  if (art) a.push('-i', artwork);
  a.push('-map', '0:a:0');
  if (art) a.push('-map', '1:v:0', '-c:v', 'copy', '-disposition:v:0', 'attached_pic', '-metadata:s:v', 'title=Album cover', '-metadata:s:v', 'comment=Cover (front)');
  a.push('-map_metadata', '-1', '-map_chapters', '-1');
  a.push('-af', resampleFilter(job, caps));
  a.push(...codecArgs(job, caps, art));
  a.push(...metadataArgs(job));
  a.push('-progress', 'pipe:1', '-nostats', job.file + '.part');
  return a;
}

// ------------------------------------------------------------------------------------------------- ID3 for WAV / AIFF
function id3Frame(id, body) {
  const h = Buffer.alloc(10);
  h.write(id, 0, 'latin1'); h.writeUInt32BE(body.length, 4);
  return Buffer.concat([h, body]);
}
function id3Text(s) {
  // ISO-8859-1 when it fits, else UTF-16 with a BOM (ID3v2.3 has no UTF-8)
  if (/^[\u0000-ÿ]*$/.test(s)) return Buffer.concat([Buffer.from([0]), Buffer.from(s, 'latin1')]);
  return Buffer.concat([Buffer.from([1, 0xff, 0xfe]), Buffer.from(s, 'utf16le')]);
}
function id3Terminated(s, enc) { return enc === 1 ? Buffer.concat([Buffer.from(s, 'utf16le'), Buffer.from([0, 0])]) : Buffer.concat([Buffer.from(s, 'latin1'), Buffer.from([0])]); }
function buildId3(tags, artwork) {
  const map = { title: 'TIT2', artist: 'TPE1', album: 'TALB', album_artist: 'TPE2', date: 'TYER', track: 'TRCK', disc: 'TPOS', genre: 'TCON',
    composer: 'TCOM', publisher: 'TPUB', copyright: 'TCOP', isrc: 'TSRC' };
  const frames = [];
  for (const [k, v] of Object.entries(tags))
  {
    if (map[k]) frames.push(id3Frame(map[k], id3Text(k === 'date' ? String(v).slice(0, 4) : v)));
    else if (k === 'comment')
    {
      const wide = !/^[\u0000-ÿ]*$/.test(v), enc = wide ? 1 : 0;
      const body = Buffer.concat([Buffer.from([enc]), Buffer.from('eng', 'latin1'), wide ? Buffer.from([0xff, 0xfe]) : Buffer.alloc(0), id3Terminated('', enc),
        wide ? Buffer.from([0xff, 0xfe]) : Buffer.alloc(0), wide ? Buffer.from(v, 'utf16le') : Buffer.from(v, 'latin1')]);
      frames.push(id3Frame('COMM', body));
    }
    else if (k.startsWith('custom:'))
    {
      const key = k.slice(7), wide = !/^[\u0000-ÿ]*$/.test(key + v), enc = wide ? 1 : 0;
      const bom = wide ? Buffer.from([0xff, 0xfe]) : Buffer.alloc(0);
      frames.push(id3Frame('TXXX', Buffer.concat([Buffer.from([enc]), bom, id3Terminated(key, enc), bom, wide ? Buffer.from(v, 'utf16le') : Buffer.from(v, 'latin1')])));
    }
  }
  if (artwork)
  {
    const img = fs.readFileSync(artwork);
    const mime = img[0] === 0x89 ? 'image/png' : 'image/jpeg';
    frames.push(id3Frame('APIC', Buffer.concat([Buffer.from([0]), Buffer.from(mime + '\0', 'latin1'), Buffer.from([3]), Buffer.from('Album cover\0', 'latin1'), img])));
  }
  const body = Buffer.concat(frames);
  const size = body.length;
  const h = Buffer.from([0x49, 0x44, 0x33, 3, 0, 0, (size >> 21) & 0x7f, (size >> 14) & 0x7f, (size >> 7) & 0x7f, size & 0x7f]);
  return Buffer.concat([h, body]);
}

// Appends an ID3 chunk to a finished WAV (RIFF or RF64) or AIFF file and fixes the container's size. The file is
// patched in place (no full read: masters can be gigabytes).
function writeId3Chunk(file, tags, artwork) {
  const tag = buildId3(tags, artwork);
  const fd = fs.openSync(file, 'r+');
  try
  {
    const head = Buffer.alloc(48);
    fs.readSync(fd, head, 0, 48, 0);
    const kind = head.toString('latin1', 0, 4);
    const big = kind === 'FORM';
    const id = big ? 'ID3 ' : 'id3 ';
    const ch = Buffer.alloc(8);
    ch.write(id, 0, 'latin1');
    if (big) ch.writeUInt32BE(tag.length, 4); else ch.writeUInt32LE(tag.length, 4);
    const pad = tag.length & 1 ? Buffer.from([0]) : Buffer.alloc(0);
    let len = fs.fstatSync(fd).size;
    if (len & 1) { fs.writeSync(fd, Buffer.from([0]), 0, 1, len); len += 1; }   // chunks start on even bytes
    fs.writeSync(fd, Buffer.concat([ch, tag, pad]), 0, 8 + tag.length + pad.length, len);
    const total = len + 8 + tag.length + pad.length;
    if (kind === 'RIFF') { const b = Buffer.alloc(4); b.writeUInt32LE(total - 8, 0); fs.writeSync(fd, b, 0, 4, 4); }
    else if (kind === 'RF64')
    {
      // the true size lives in the ds64 chunk right after the header
      if (head.toString('latin1', 12, 16) === 'ds64') { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(total - 8), 0); fs.writeSync(fd, b, 0, 8, 20); }
    }
    else if (big) { const b = Buffer.alloc(4); b.writeUInt32BE(total - 8, 0); fs.writeSync(fd, b, 0, 4, 4); }
    else throw new Error('not a WAV or AIFF file');
  }
  finally { fs.closeSync(fd); }
}

// Reads the ID3 chunk of a WAV/AIFF file back as { FRAME: text } (tests and the after-check).
function readId3Chunk(file) {
  const buf = fs.readFileSync(file);
  const big = buf.toString('latin1', 0, 4) === 'FORM';
  let p = 12;
  const out = {};
  while (p + 8 <= buf.length)
  {
    const id = buf.toString('latin1', p, p + 4);
    const n = big ? buf.readUInt32BE(p + 4) : buf.readUInt32LE(p + 4);
    if (id.toLowerCase() === 'id3 ')
    {
      const t = buf.subarray(p + 8, p + 8 + n);
      let q = 10;
      const end = 10 + ((t[6] << 21) | (t[7] << 14) | (t[8] << 7) | t[9]);
      while (q + 10 <= end)
      {
        const fid = t.toString('latin1', q, q + 4), fn = t.readUInt32BE(q + 4);
        if (!/^[A-Z0-9]{4}$/.test(fid) || fn === 0) break;
        const b = t.subarray(q + 10, q + 10 + fn);
        const dec = (x, enc) => (enc === 1 ? x.toString('utf16le').replace(/^﻿/, '') : x.toString('latin1')).replace(/\0+$/, '');
        if (fid[0] === 'T' && fid !== 'TXXX') out[fid] = dec(b.subarray(1), b[0]);
        else if (fid === 'TXXX')
        {
          const enc = b[0], rest = b.subarray(1);
          const z = enc === 1 ? (() => { for (let i = 0; i + 1 < rest.length; i += 2) if (rest[i] === 0 && rest[i + 1] === 0) return i; return -1; })() : rest.indexOf(0);
          out['TXXX:' + dec(rest.subarray(0, z), enc)] = dec(rest.subarray(z + (enc === 1 ? 2 : 1)), enc);
        }
        else if (fid === 'COMM') out.COMM = dec(b.subarray(b[0] === 1 ? 10 : 5), b[0]);
        else if (fid === 'APIC') out.APIC = fn;
        q += 10 + fn;
      }
      return out;
    }
    p += 8 + n + (n & 1);
  }
  return out;
}

// ------------------------------------------------------------------------------------------------- running
// One job: convert into "<file>.part", write the WAV/AIFF tags, check the result, then rename into place.
async function runJob(job, caps, opts = {}) {
  fs.mkdirSync(path.dirname(job.file), { recursive: true });
  const part = job.file + '.part';
  const dur = job.source.duration || 0;
  const args = buildArgs(job, caps, opts.artwork);
  let buf = '';
  const r = await run(args, {
    onSpawn: opts.onSpawn,
    onStdout: (s) => {
      buf += s;
      const m = buf.match(/out_time_us=(\d+)[^]*$/);
      if (m && dur > 0 && opts.onProgress) opts.onProgress(Math.min(0.99, +m[1] / 1e6 / dur));
      if (buf.length > 4096) buf = buf.slice(-1024);
    },
  });
  if (r.code !== 0)
  {
    try { fs.unlinkSync(part); } catch (e) {}
    const last = r.err.trim().split('\n').filter((l) => !/^\s*$/.test(l)).slice(-2).join(' / ');
    return { ok: false, error: opts.cancelled && opts.cancelled() ? 'cancelled' : (last || 'ffmpeg failed') };
  }
  try
  {
    if (job.version.format === 'wav' || job.version.format === 'aiff') writeId3Chunk(part, job.tags, opts.artwork);
  }
  catch (e) { try { fs.unlinkSync(part); } catch (e2) {} return { ok: false, error: 'tags: ' + e.message }; }
  const check = await verify(part, job);
  try { fs.renameSync(part, job.file); }
  catch (e) { try { fs.copyFileSync(part, job.file); fs.unlinkSync(part); } catch (e2) { return { ok: false, error: 'could not write ' + job.file + ': ' + e2.message }; } }
  return { ok: check.ok, error: check.ok ? undefined : check.error, warning: check.warning, file: job.file, check };
}

// The result decoded again: format, rate, word length, channels, length; peak (an over is a warning).
async function verify(file, job) {
  const p = await probe(file);
  const want = job.out;
  const f = FORMATS[job.version.format];
  const problems = [];
  if (!p.ok) return { ok: false, error: 'the result could not be read back' };
  if (p.rate !== want.rate) problems.push(`rate ${p.rate} (wanted ${want.rate})`);
  if (f.pcm && !want.float && p.bits && p.bits !== want.bits) problems.push(`${p.bits}-bit (wanted ${want.bits})`);
  if (p.channels !== job.source.channels) problems.push(`${p.channels} channels (source ${job.source.channels})`);
  if (job.source.duration > 0 && Math.abs(p.duration - job.source.duration) > 0.06 + (f.pcm ? 0 : 0.1)) problems.push(`length ${p.duration.toFixed(2)} s (source ${job.source.duration.toFixed(2)} s)`);
  // peak of the decoded result
  const vd = await run(['-hide_banner', '-nostdin', '-i', file, '-map', '0:a:0', '-af', 'volumedetect', '-f', 'null', '-']);
  const peak = parseFloat((vd.err.match(/max_volume: (-?[\d.]+) dB/) || [])[1]);
  const res = { ok: problems.length === 0, peakDb: isFinite(peak) ? peak : null, probe: p };
  if (!res.ok) res.error = problems.join(', ');
  if (isFinite(peak) && peak >= -0.0 && !want.float) res.warning = `reaches full scale (${peak.toFixed(1)} dBFS): may clip`;
  return res;
}

module.exports = { AUDIO_EXT, FORMATS, RATES, MP3_MODES, AAC_RATES, setFfmpeg, getFfmpeg, capabilities, probe, parseProbe, expandPaths,
  resolveOutput, plan, fill, safeName, tokens, buildTags, buildArgs, resampleFilter, buildId3, writeId3Chunk, readId3Chunk, runJob, verify, khz };
