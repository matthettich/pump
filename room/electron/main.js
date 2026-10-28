// pump ROOM as a desktop app (Mac and Windows): a window around the same page as bin/pump-room.html (room/build.sh),
// which is copied to app/pump-room.html before packaging (room/electron/build.sh). Only the microphone and choosing
// the output are allowed; nothing is loaded from the network.
//
// PUMP_ROOM_SMOKE=1: run the page's simulated room (#sim), measure both speakers, print the result and quit with 0
// if the timing comes out right (CI runs the packaged app this way).
'use strict';
const { app, BrowserWindow, Menu, session, shell, systemPreferences } = require('electron');
const path = require('path');

const SMOKE = process.env.PUMP_ROOM_SMOKE === '1';
const isMac = process.platform === 'darwin';

app.setName('pump ROOM');
if (!SMOKE && !app.requestSingleInstanceLock()) app.quit();

let win = null;

function createWindow() {
  win = new BrowserWindow({
    width: 1280, height: 900, minWidth: 420, minHeight: 560, title: 'pump ROOM', show: false,
    backgroundColor: '#fff3df',
    webPreferences: { contextIsolation: true, nodeIntegration: false, sandbox: true, backgroundThrottling: false, spellcheck: false },
  });
  win.once('ready-to-show', () => win.show());
  // the page is the whole app: no navigation away from it, links open in the browser
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https:\/\//.test(url)) shell.openExternal(url);
    return { action: 'deny' };
  });
  win.webContents.on('will-navigate', (e, url) => { if (url.split('#')[0] !== win.webContents.getURL().split('#')[0]) e.preventDefault(); });
  win.on('closed', () => { win = null; });
  win.loadFile(path.join(__dirname, 'app', 'pump-room.html'), SMOKE ? { hash: 'sim' } : undefined);
  return win;
}

function menu() {
  if (!isMac) { Menu.setApplicationMenu(null); return; }
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    { role: 'appMenu' },
    { role: 'fileMenu' },
    { role: 'editMenu' },
    { label: 'View', submenu: [{ role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }, { type: 'separator' }, { role: 'togglefullscreen' }] },
    { role: 'windowMenu' },
  ]));
}

async function smoke(w) {
  const finish = (code, msg) => {
    console.log(msg);
    // a Windows GUI app's console output goes nowhere: PUMP_ROOM_SMOKE_OUT names a file for it
    if (process.env.PUMP_ROOM_SMOKE_OUT) { try { require('fs').writeFileSync(process.env.PUMP_ROOM_SMOKE_OUT, msg + '\n'); } catch (e) {} }
    app.exit(code);
  };
  const timer = setTimeout(() => finish(1, 'SMOKE FAIL: timed out'), 120000);
  try {
    await new Promise((resolve) => (w.webContents.isLoading() ? w.webContents.once('did-finish-load', resolve) : resolve()));
    const r = await w.webContents.executeJavaScript(`(async () => {
      const P = window.pumpRoom;
      await P.startAudio();
      if (!P.S.node) return { err: document.getElementById('status').textContent };
      P.S.seconds = 1;
      await P.measure();
      const run = P.S.runs[0];
      if (!run) return { err: document.getElementById('status').textContent };
      return { fs: run.fs, dt: run.dtMs, arrivals: run.ch.map((c) => c.arrival), n: run.ch.length };
    })()`, true);
    clearTimeout(timer);
    if (r.err) return finish(1, 'SMOKE FAIL: ' + r.err);
    const at = (m) => Math.round(0.012 * r.fs) + Math.round(m / 343 * r.fs);   // the simulated room's geometry
    const want = (at(3.1) - at(2.9)) / r.fs * 1000;
    const ok = r.n === 2 && Math.abs(r.dt - want) < 0.02 && Math.abs(r.arrivals[0] - at(2.9)) < 1;
    finish(ok ? 0 : 1, `SMOKE ${ok ? 'ok' : 'FAIL'}: ${JSON.stringify(r)} (right should be ${want.toFixed(3)} ms later)`);
  } catch (e) {
    finish(1, 'SMOKE FAIL: ' + e.message);
  }
}

app.whenReady().then(async () => {
  const allowed = new Set(['media', 'speaker-selection', 'fullscreen', 'clipboard-sanitized-write']);
  session.defaultSession.setPermissionRequestHandler((wc, permission, callback, details) => {
    const audioOnly = !details || !details.mediaTypes || details.mediaTypes.every((t) => t === 'audio');
    callback(allowed.has(permission) && (permission !== 'media' || audioOnly));
  });
  session.defaultSession.setPermissionCheckHandler((wc, permission) => allowed.has(permission));
  menu();
  // macOS asks once for the microphone (the reason is in Info.plist: NSMicrophoneUsageDescription)
  if (isMac && !SMOKE) {
    try { await systemPreferences.askForMediaAccess('microphone'); } catch (e) { /* the page reports a refusal */ }
  }
  const w = createWindow();
  if (SMOKE) smoke(w);
});

app.on('second-instance', () => { if (win) { if (win.isMinimized()) win.restore(); win.focus(); } });
app.on('activate', () => { if (!win) createWindow(); });
app.on('window-all-closed', () => app.quit());
