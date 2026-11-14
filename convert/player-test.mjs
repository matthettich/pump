// pump CONVERT, PLAY page: node convert/player-test.mjs (needs ffmpeg on PATH, or PUMP_FFMPEG).
// Checks player-engine.js for real: .m3u8 read / write, the ffmetadata parser, the tag-writing command, tag writes into
// generated WAV / AIFF / FLAC / MP3 / M4A files (audio packets identical afterwards, nothing left behind, cover art),
// refusals, and the decoder (native rate, float32, no gain, seeking, backpressure).
import { createRequire } from 'module';
import fs from 'fs';
import os from 'os';
import path from 'path';
import { execFileSync } from 'child_process';
const require = createRequire(import.meta.url);
const E = require('./engine.js');
const P = require('./player-engine.js');
const FF = process.env.PUMP_FFMPEG || 'ffmpeg';
E.setFfmpeg(FF);

let fails = 0;
const check = (ok, msg) => { if (!ok) fails++; console.log(`  ${ok ? 'ok' : 'FAIL'}: ${msg}`); };
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'pump-play-'));
const ff = (args) => execFileSync(FF, ['-hide_banner', '-nostdin', '-loglevel', 'error', '-y', ...args]);

console.log('m3u8');
{
  const dir = path.join(tmp, 'pl'); fs.mkdirSync(path.join(dir, 'sub'), { recursive: true });
  const tracks = [
    { path: path.join(dir, 'sub', 'a.wav'), title: 'Ä Title', artist: 'Wake', duration: 12.4 },
    { path: path.join(dir, 'b.flac'), title: 'B', artist: '', duration: 0 },
    { path: path.join(tmp, 'other', 'c.mp3'), title: 'C', artist: 'X', duration: 200 },
  ];
  const f = path.join(dir, 'list.m3u8');
  P.writeM3u(f, tracks);
  const txt = fs.readFileSync(f, 'utf8');
  check(txt.startsWith('#EXTM3U\n') && txt.includes('#EXTINF:12,Wake - Ä Title') && txt.includes('#EXTINF:-1,B'), 'header, EXTINF lines, UTF-8');
  check(txt.includes('\nsub/a.wav\n') && txt.includes('\n../other/c.mp3\n'), 'relative paths with forward slashes');
  const back = P.readM3u(f);
  check(back.length === 3 && back.every((b, i) => b.path === tracks[i].path), 'paths read back absolute and equal');
  check(back[0].title === 'Wake - Ä Title' && back[0].duration === 12, 'EXTINF read back');
  fs.writeFileSync(f, '﻿#EXTM3U\r\n#EXTINF:5,T\r\nhttp://x/y.mp3\r\nfile://' + encodeURI(path.join(dir, 'b.flac')) + '\r\nsub/a.wav\r\n');
  const odd = P.readM3u(f);
  check(odd.length === 2 && odd[0].path === path.join(dir, 'b.flac') && odd[1].path === path.join(dir, 'sub', 'a.wav'), 'BOM, CRLF, file:// URL; network streams skipped');
}

console.log('ffmetadata parser');
{
  const m = P.parseFfmetadata(';FFMETADATA1\ntitle=T\\=1\;x\nComment=line1\\\nline2\nALBUM_ARTIST=Z\n[STREAM]\ntitle=no\n');
  check(m.title === 'T=1;x' && m.comment === 'line1\nline2' && m.album_artist === 'Z' && Object.keys(m).length === 3, 'escapes, continuation, lower-case keys, stops at [STREAM]');
}

console.log('tag command');
{
  const a = P.buildTagArgs({ file: 'in.flac', out: 'o.flac', fmt: 'flac', raw: { album_artist: 'Old', track: '1' }, hasCover: true, changes: { albumArtist: 'New', track: '2', title: 'Hi' }, cover: { action: 'keep' } });
  const s = a.join(' ');
  check(s.includes('-map 0:a -map 0:v?') && s.includes('-c copy') && s.includes('-map_metadata 0'), 'stream copy, audio + existing picture, tags carried');
  check(s.includes('-metadata album_artist=') && s.includes('-metadata ALBUMARTIST=New') && s.includes('-metadata TRACKNUMBER=2') && s.includes('-metadata TITLE=Hi'), 'FLAC: Vorbis names, old alias removed');
  check(!/-c:a|-ar |-af /.test(s), 'no re-encoding or resampling options');
  const m = P.buildTagArgs({ file: 'in.mp3', out: 'o.mp3', fmt: 'mp3', raw: {}, hasCover: false, changes: { year: '2020' }, cover: { action: 'set', path: 'c.jpg' } }).join(' ');
  check(m.includes('-id3v2_version 3') && m.includes('-i c.jpg') && m.includes('-disposition:v:0 attached_pic') && m.includes('-metadata date=2020'), 'MP3: ID3v2.3, cover attached');
}

// test material
const sine = (f, args) => ff(['-f', 'lavfi', '-i', 'sine=f=440:d=3:r=' + (f.rate || 44100), '-af', 'pan=stereo|c0=c0|c1=0.5*c0', ...args]);
const files = {};
for (const [ext, extra] of [['wav', ['-c:a', 'pcm_s24le']], ['aiff', ['-c:a', 'pcm_s16be', '-write_id3v2', '1', '-id3v2_version', '3']], ['flac', []], ['mp3', ['-b:a', '192k']], ['m4a', ['-c:a', 'aac', '-b:a', '128k']]])
{
  const f = path.join(tmp, 'src.' + ext);
  sine({ rate: 44100 }, [...extra, '-metadata', 'title=Old', '-metadata', 'artist=Old Artist', '-metadata', 'album=Alb', '-metadata', 'genre=IDM', f]);
  files[ext] = f;
}
ff(['-f', 'lavfi', '-i', 'color=c=red:s=64x64:d=1', '-frames:v', '1', path.join(tmp, 'c.png')]);
ff(['-f', 'lavfi', '-i', 'color=c=blue:s=64x64:d=1', '-frames:v', '1', path.join(tmp, 'c.jpg')]);
const md5 = (f) => execFileSync(FF, ['-hide_banner', '-nostdin', '-loglevel', 'error', '-i', f, '-map', '0:a', '-c', 'copy', '-f', 'md5', '-']).toString().trim();

console.log('tag writes (stream copy, verified, atomic)');
for (const ext of Object.keys(files))
{
  const f = files[ext];
  const h0 = md5(f);
  const r = await P.writeTags(f, { title: 'Nöw Title', artist: 'New Artist', albumArtist: 'The Band', track: '3/9', year: '2024', genre: 'Ambient', comment: 'a comment' }, { action: 'set', path: path.join(tmp, ext === 'mp3' ? 'c.jpg' : 'c.png') });
  check(r.ok, `${ext}: write ok${r.ok ? '' : ' (' + r.error + ')'}`);
  if (!r.ok) continue;
  const t = await P.readTags(f);
  check(t.title === 'Nöw Title' && t.artist === 'New Artist' && t.album === 'Alb' && t.genre === 'Ambient', `${ext}: edited and untouched fields read back`);
  check(t.albumArtist === 'The Band' && t.track.startsWith('3') && t.year.startsWith('2024') && t.comment === 'a comment', `${ext}: album artist, track, year, comment`);
  check(!!t.cover && t.cover.startsWith('data:image/'), `${ext}: cover art present`);
  check(md5(f) === h0, `${ext}: audio packets identical`);
  check(t.rate === 44100 && t.channels === 2, `${ext}: rate and channels unchanged`);
  // edit one field, keep the cover, clear another
  const r2 = await P.writeTags(f, { title: 'Second', genre: '' }, { action: 'keep' });
  const t2 = await P.readTags(f);
  check(r2.ok && t2.title === 'Second' && t2.genre === '' && t2.artist === 'New Artist' && t2.albumArtist === 'The Band' && !!t2.cover, `${ext}: second edit keeps the rest and the cover${r2.ok ? '' : ' (' + r2.error + ')'}`);
  const r3 = await P.writeTags(f, {}, { action: 'remove' });
  const t3 = await P.readTags(f);
  check(r3.ok && !t3.cover && t3.title === 'Second' && md5(f) === h0, `${ext}: cover removed${r3.ok ? '' : ' (' + r3.error + ')'}`);
}
check(fs.readdirSync(tmp).every((n) => !/pump-(tags|art)/.test(n)), 'no temp files left behind');

console.log('refusals');
{
  const ogg = path.join(tmp, 'x.ogg'); ff(['-f', 'lavfi', '-i', 'sine=d=1', ogg]);
  const r = await P.writeTags(ogg, { title: 'x' });
  check(!r.ok && /not supported/.test(r.error), 'unsupported format: an error, not a crash');
  const bad = path.join(tmp, 'nofile.wav');
  const r2 = await P.writeTags(bad, { title: 'x' });
  check(!r2.ok, 'missing file: an error');
  // a WAV with a cue chunk: refused (copying would drop it)
  const w = fs.readFileSync(files.wav);
  const cue = Buffer.alloc(8 + 4); cue.write('cue ', 0, 'latin1'); cue.writeUInt32LE(4, 4);
  const w2 = Buffer.concat([w, cue]); w2.writeUInt32LE(w2.length - 8, 4);
  const cf = path.join(tmp, 'cue.wav'); fs.writeFileSync(cf, w2);
  const r3 = await P.writeTags(cf, { title: 'x' });
  check(!r3.ok && /cue/.test(r3.error) && fs.readFileSync(cf).equals(w2), 'WAV with a cue chunk: refused, file untouched');
  const r4 = await P.writeTags(files.flac, { title: 'x' }, { action: 'set', path: path.join(tmp, 'src.flac') });
  check(!r4.ok, 'artwork that is not an image: refused');
}

console.log('decoder');
{
  const hi = path.join(tmp, 'hi.flac');
  ff(['-f', 'lavfi', '-i', 'sine=f=1000:d=2:r=96000', '-af', 'pan=stereo|c0=c0|c1=c0', '-c:a', 'flac', '-sample_fmt', 's32', hi]);
  const s = await P.openStream(hi, 0, 2);
  check(s.rate === 96000 && s.channels === 2, 'native rate (96000) and stereo reported');
  let total = 0, first = null;
  for (;;) { const b = await P.readStream(s.id, 65536 * 8, 2); if (!b) break; if (!first) first = b; total += b.length; }
  check(total === 2 * 96000 * 2 * 4, `float32 stereo, all ${total / 8} frames, no resampling`);
  const f32 = new Float32Array(first.buffer, first.byteOffset, first.byteLength / 4);
  let pk = 0; for (let i = 0; i < f32.length; i += 2) pk = Math.max(pk, Math.abs(f32[i]));
  check(Math.abs(pk - 0.125) < 0.001, 'level is unchanged (sine peak ' + pk.toFixed(4) + ')');
  check(first.buffer.byteLength === first.byteLength, 'chunk owns exactly its bytes (cheap to send)');
  const s2 = await P.openStream(hi, 1.5, 2);
  let t2 = 0; for (;;) { const b = await P.readStream(s2.id, 1 << 18, 2); if (!b) break; t2 += b.length; }
  check(Math.abs(t2 / 8 - 0.5 * 96000) < 96000 * 0.01, 'seek to 1.5 s leaves 0.5 s (' + (t2 / 8 / 96000).toFixed(3) + ' s)');
  const mono = path.join(tmp, 'mono.wav'); ff(['-f', 'lavfi', '-i', 'sine=d=1:r=48000', '-ac', '1', mono]);
  const s3 = await P.openStream(mono, 0, 1);
  check(s3.channels === 1 && s3.rate === 48000, 'mono stays mono');
  const s4 = await P.openStream(mono, 0, 2);
  check(s4.channels === 1, 'a stale stereo hint on a mono file is corrected');
  // backpressure: a long file is not read ahead without limit
  const long = path.join(tmp, 'long.wav'); ff(['-f', 'lavfi', '-i', 'anoisesrc=d=120:r=48000', '-c:a', 'pcm_s16le', long]);
  const s5 = await P.openStream(long, 0, 2);
  await new Promise((r) => setTimeout(r, 1500));
  const reader = [...[s5.id]].map((id) => id);
  const b = await P.readStream(s5.id, 1 << 16, 2);
  check(b && b.length === 1 << 16, 'long file streams on demand');
  P.closeAll();
  let threw = false; try { await P.openStream(path.join(tmp, 'nofile.wav'), 0, 2); } catch (e) { threw = true; }
  check(threw, 'opening a missing file rejects');
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log(fails ? `\n${fails} FAILED` : '\nall passed');
process.exit(fails ? 1 : 0);
