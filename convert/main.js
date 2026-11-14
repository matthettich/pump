// pump CONVERT: the desktop app (Electron). The window is app/index.html; everything that touches files or runs
// ffmpeg is here, reached through preload.js. The conversion itself is engine.js.
//
// ffmpeg: the packaged app carries its own (resources/ffmpeg/<platform>-<arch>/ffmpeg[.exe], put there by
// build.sh); in development the one on PATH is used.
'use strict';
const { app, BrowserWindow, Menu, dialog, ipcMain, shell, nativeTheme } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const E = require('./engine.js');

const isMac = process.platform === 'darwin';
app.setName('pump CONVERT');
nativeTheme.themeSource = 'light';   // the title bar, file dialogs and menus stay light too, even in system dark mode
if (!app.requestSingleInstanceLock()) app.quit();

let win = null;
let caps = null;

function findFfmpeg() {
  const exe = process.platform === 'win32' ? 'ffmpeg.exe' : 'ffmpeg';
  const dirs = [
    path.join(process.resourcesPath || '', 'ffmpeg', `${process.platform}-${process.arch}`),
    path.join(__dirname, 'ffmpeg', `${process.platform}-${process.arch}`),
  ];
  for (const d of dirs) { const p = path.join(d, exe); if (fs.existsSync(p)) return p; }
  return 'ffmpeg';
}

function createWindow() {
  win = new BrowserWindow({
    width: 1360, height: 900, minWidth: 980, minHeight: 640, title: 'pump CONVERT', show: false, backgroundColor: '#f5f5f8',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true, spellcheck: false },
  });
  win.once('ready-to-show', () => win.show());
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https:\/\//.test(url)) shell.openExternal(url); return { action: 'deny' }; });
  win.webContents.on('will-navigate', (e) => e.preventDefault());   // a dropped file must not replace the page
  win.on('closed', () => { win = null; });
  win.loadFile(path.join(__dirname, 'app', 'index.html'));
}

function menu() {
  if (!isMac) { Menu.setApplicationMenu(null); return; }
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    { role: 'appMenu' }, { role: 'fileMenu' }, { role: 'editMenu' },
    { label: 'View', submenu: [{ role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }, { type: 'separator' }, { role: 'togglefullscreen' }] },
    { role: 'windowMenu' },
  ]));
}

// ------------------------------------------------------------------------------------------------- IPC
const AUDIO_FILTER = [{ name: 'Audio', extensions: E.AUDIO_EXT.map((e) => e.slice(1)) }];

ipcMain.handle('caps', async () => {
  if (!caps) { E.setFfmpeg(findFfmpeg()); caps = await E.capabilities(); caps.cpus = os.cpus().length; }
  return caps;
});
ipcMain.handle('pick-files', async () => {
  const r = await dialog.showOpenDialog(win, { title: 'Add audio files', properties: ['openFile', 'multiSelections'], filters: AUDIO_FILTER });
  return r.canceled ? [] : r.filePaths;
});
ipcMain.handle('pick-folder', async (e, title) => {
  const r = await dialog.showOpenDialog(win, { title: title || 'Choose a folder', properties: ['openDirectory', 'createDirectory'] });
  return r.canceled ? '' : r.filePaths[0];
});
ipcMain.handle('pick-image', async () => {
  const r = await dialog.showOpenDialog(win, { title: 'Artwork', properties: ['openFile'], filters: [{ name: 'Images', extensions: ['jpg', 'jpeg', 'png'] }] });
  return r.canceled ? '' : r.filePaths[0];
});
// files and folders -> probed sources, in order
ipcMain.handle('probe', async (e, paths) => {
  const files = E.expandPaths(paths);
  const out = new Array(files.length);
  let next = 0;
  const worker = async () => { while (next < files.length) { const i = next++; out[i] = await E.probe(files[i]); } };
  await Promise.all(Array.from({ length: Math.min(4, files.length) }, worker));
  return out;
});
ipcMain.handle('plan', (e, { sources, versions, meta }) => E.plan(sources, versions, meta).map((j) => ({ id: j.id, file: j.file, out: j.out, versionIndex: j.versionIndex, title: j.tok.title })));
ipcMain.handle('reveal', (e, p) => { if (p && fs.existsSync(p)) shell.showItemInFolder(p); else if (p) shell.openPath(path.dirname(p)); });
ipcMain.handle('exists', (e, p) => !!p && fs.existsSync(p));

// presets (the user's deliverable sets) and the last setup, in the app's data folder
const dataFile = (n) => path.join(app.getPath('userData'), n);
ipcMain.handle('load', (e, name) => { try { return JSON.parse(fs.readFileSync(dataFile(name), 'utf8')); } catch (err) { return null; } });
ipcMain.handle('save', (e, name, data) => {
  try { fs.mkdirSync(app.getPath('userData'), { recursive: true }); fs.writeFileSync(dataFile(name) + '.tmp', JSON.stringify(data, null, 1)); fs.renameSync(dataFile(name) + '.tmp', dataFile(name)); return true; }
  catch (err) { return false; }
});

// the batch: every job, a few at a time; progress goes to the window as it happens
let running = null;
ipcMain.handle('run', async (e, { sources, versions, meta, threads }) => {
  if (running) return { ok: false, error: 'already running' };
  if (!caps) { E.setFfmpeg(findFfmpeg()); caps = await E.capabilities(); }
  if (!caps.ok) return { ok: false, error: caps.error };
  const jobs = E.plan(sources, versions, meta);
  const artwork = meta.artwork && fs.existsSync(meta.artwork) ? meta.artwork : '';
  const state = { cancelled: false, procs: new Set() };
  running = state;
  const send = (m) => { if (win) win.webContents.send('progress', m); };
  let next = 0, ok = 0, failed = 0, warned = 0;
  const n = Math.max(1, Math.min(threads || Math.max(1, os.cpus().length - 1), 8));
  const worker = async () => {
    while (next < jobs.length && !state.cancelled)
    {
      const j = jobs[next++];
      send({ id: j.id, state: 'run', pct: 0 });
      let last = 0;
      const r = await E.runJob(j, caps, {
        artwork,
        cancelled: () => state.cancelled,
        onSpawn: (p) => { state.procs.add(p); p.on('close', () => state.procs.delete(p)); },
        onProgress: (pct) => { const t = Date.now(); if (t - last > 120) { last = t; send({ id: j.id, state: 'run', pct }); } },
      });
      if (r.ok) { ok++; if (r.warning) warned++; } else failed++;
      send({ id: j.id, state: r.ok ? (r.warning ? 'warn' : 'done') : 'fail', pct: 1, error: r.error, warning: r.warning, file: j.file, peak: r.check && r.check.peakDb });
    }
  };
  await Promise.all(Array.from({ length: Math.min(n, jobs.length) }, worker));
  running = null;
  return { ok: !state.cancelled && failed === 0, done: ok, failed, warned, cancelled: state.cancelled, total: jobs.length };
});
ipcMain.handle('cancel', () => {
  if (!running) return;
  running.cancelled = true;
  for (const p of running.procs) { try { p.kill(); } catch (e) {} }
});

// ------------------------------------------------------------------------------------------------- PLAY page (player-engine.js)
// Everything below is the player's; the converter above does not use any of it.
const PE = require('./player-engine.js');
const { session } = require('electron');
let playerFfmpegSet = false;
const playerReady = () => { if (!playerFfmpegSet) { E.setFfmpeg(findFfmpeg()); playerFfmpegSet = true; } };
const M3U_FILTER = [{ name: 'Playlist', extensions: ['m3u8', 'm3u'] }];

// Output device names (enumerateDevices) need the media permission check to pass; speaker-selection is the output choice.
// Only those two, and only for audio and for this app's own pages: every other permission is refused.
const ownPage = (url) => typeof url === 'string' && url.startsWith('file://');
const audioOnly = (details) => !details || !details.mediaType || details.mediaType === 'audio' || (Array.isArray(details.mediaTypes) && details.mediaTypes.every((t) => t === 'audio'));
app.whenReady().then(() => {
  const ses = session.defaultSession;
  ses.setPermissionCheckHandler((wc, permission, origin, details) => {
    const url = (details && (details.requestingUrl || details.embeddingOrigin)) || origin || (wc && wc.getURL());
    return ownPage(url) && (permission === 'speaker-selection' || (permission === 'media' && audioOnly(details)));
  });
  ses.setPermissionRequestHandler((wc, permission, cb, details) => {
    cb(ownPage(details && details.requestingUrl) && (permission === 'speaker-selection' || (permission === 'media' && audioOnly(details))));
  });
});
// the decoder processes die with the page that asked for them
app.on('browser-window-created', (e, w) => {
  w.webContents.on('did-start-navigation', (ev, url, inPlace, isMain) => { if (isMain && !inPlace) PE.closeAll(); });
  w.on('closed', () => PE.closeAll());
});
app.on('before-quit', () => PE.closeAll());

ipcMain.handle('player-pick-files', async (e, playlist) => {
  const r = await dialog.showOpenDialog(win, { title: playlist ? 'Load a playlist' : 'Add audio files', properties: ['openFile', 'multiSelections'], filters: playlist ? M3U_FILTER : AUDIO_FILTER });
  return r.canceled ? [] : r.filePaths;
});
ipcMain.handle('player-pick-folder', async () => {
  const r = await dialog.showOpenDialog(win, { title: 'Add a folder', properties: ['openDirectory'] });
  return r.canceled ? '' : r.filePaths[0];
});
ipcMain.handle('player-pick-image', async () => {
  const r = await dialog.showOpenDialog(win, { title: 'Artwork', properties: ['openFile'], filters: [{ name: 'Images', extensions: ['jpg', 'jpeg', 'png'] }] });
  return r.canceled ? '' : r.filePaths[0];
});
// files, folders and playlists -> tracks (tags, length, format), in order
ipcMain.handle('player-probe', async (e, paths) => {
  playerReady();
  const files = [];
  for (const p of paths || [])
  {
    if (/\.m3u8?$/i.test(p)) { try { for (const t of PE.readM3u(p)) files.push(...E.expandPaths([t.path])); } catch (err) {} }
    else files.push(...E.expandPaths([p]));
  }
  const out = new Array(files.length);
  let next = 0;
  const worker = async () => { while (next < files.length) { const i = next++; out[i] = await PE.probeTrack(files[i]); } };
  await Promise.all(Array.from({ length: Math.min(4, files.length) }, worker));
  return out.map((t) => { if (t && t.raw) delete t.raw; return t; });
});
ipcMain.handle('player-tags', async (e, file) => { playerReady(); const t = await PE.readTags(file); if (t.raw) delete t.raw; return t; });
ipcMain.handle('player-write-tags', async (e, file, changes, cover) => {
  playerReady();
  const r = await PE.writeTags(file, changes, cover);
  if (r.track && r.track.raw) delete r.track.raw;
  return r;
});
ipcMain.handle('player-open', async (e, file, startSec, channels) => {
  playerReady();
  try { return { ok: true, ...(await PE.openStream(file, Math.max(0, +startSec || 0), channels)) }; }
  catch (err) { return { ok: false, error: String(err.message || err) }; }
});
ipcMain.handle('player-read', (e, id, bytes, channels) => PE.readStream(id, bytes, channels));
ipcMain.handle('player-close', (e, id) => PE.closeStream(id));
ipcMain.handle('player-m3u-save', async (e, tracks) => {
  const r = await dialog.showSaveDialog(win, { title: 'Save playlist', defaultPath: 'playlist.m3u8', filters: M3U_FILTER });
  if (r.canceled || !r.filePath) return { ok: false, canceled: true };
  try { PE.writeM3u(/\.m3u8?$/i.test(r.filePath) ? r.filePath : r.filePath + '.m3u8', tracks); return { ok: true, file: r.filePath }; }
  catch (err) { return { ok: false, error: String(err.message || err) }; }
});
ipcMain.handle('player-reveal', (e, p) => { if (p && fs.existsSync(p)) shell.showItemInFolder(p); });
ipcMain.handle('player-exists', (e, paths) => (paths || []).map((p) => !!p && fs.existsSync(p)));
ipcMain.handle('player-load', () => { try { return JSON.parse(fs.readFileSync(dataFile('player.json'), 'utf8')); } catch (err) { return null; } });
ipcMain.handle('player-save', (e, data) => {
  try { fs.mkdirSync(app.getPath('userData'), { recursive: true }); fs.writeFileSync(dataFile('player.json') + '.tmp', JSON.stringify(data)); fs.renameSync(dataFile('player.json') + '.tmp', dataFile('player.json')); return true; }
  catch (err) { return false; }
});

app.whenReady().then(() => {
  menu();
  createWindow();
  app.on('activate', () => { if (!win) createWindow(); });
});
app.on('second-instance', () => { if (win) { if (win.isMinimized()) win.restore(); win.focus(); } });
app.on('window-all-closed', () => app.quit());
