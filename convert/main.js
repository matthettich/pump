// pump CONVERT: the desktop app (Electron). The window is app/index.html; everything that touches files or runs
// ffmpeg is here, reached through preload.js. The conversion itself is engine.js.
//
// ffmpeg: the packaged app carries its own (resources/ffmpeg/<platform>-<arch>/ffmpeg[.exe], put there by
// build.sh); in development the one on PATH is used.
'use strict';
const { app, BrowserWindow, Menu, dialog, ipcMain, shell } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const E = require('./engine.js');

const isMac = process.platform === 'darwin';
app.setName('pump CONVERT');
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

app.whenReady().then(() => {
  menu();
  createWindow();
  app.on('activate', () => { if (!win) createWindow(); });
});
app.on('second-instance', () => { if (win) { if (win.isMinimized()) win.restore(); win.focus(); } });
app.on('window-all-closed', () => app.quit());
