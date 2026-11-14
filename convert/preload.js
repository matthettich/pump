// pump CONVERT: what the window may ask of the app (main.js). Nothing else of Node or Electron reaches the page.
'use strict';
const { contextBridge, ipcRenderer, webUtils } = require('electron');

contextBridge.exposeInMainWorld('convert', {
  caps: () => ipcRenderer.invoke('caps'),
  pickFiles: () => ipcRenderer.invoke('pick-files'),
  pickFolder: (title) => ipcRenderer.invoke('pick-folder', title),
  pickImage: () => ipcRenderer.invoke('pick-image'),
  probe: (paths) => ipcRenderer.invoke('probe', paths),
  plan: (setup) => ipcRenderer.invoke('plan', setup),
  run: (setup) => ipcRenderer.invoke('run', setup),
  cancel: () => ipcRenderer.invoke('cancel'),
  reveal: (p) => ipcRenderer.invoke('reveal', p),
  exists: (p) => ipcRenderer.invoke('exists', p),
  load: (name) => ipcRenderer.invoke('load', name),
  save: (name, data) => ipcRenderer.invoke('save', name, data),
  onProgress: (fn) => ipcRenderer.on('progress', (e, m) => fn(m)),
  // a dropped file's path (drag and drop from the desktop)
  pathFor: (file) => { try { return webUtils.getPathForFile(file); } catch (e) { return ''; } },
});

// the PLAY page (player-engine.js, main.js 'player-*')
contextBridge.exposeInMainWorld('player', {
  pickFiles: (playlist) => ipcRenderer.invoke('player-pick-files', !!playlist),
  pickFolder: () => ipcRenderer.invoke('player-pick-folder'),
  pickImage: () => ipcRenderer.invoke('player-pick-image'),
  probe: (paths) => ipcRenderer.invoke('player-probe', paths),
  tags: (file) => ipcRenderer.invoke('player-tags', file),
  writeTags: (file, changes, cover) => ipcRenderer.invoke('player-write-tags', file, changes, cover),
  open: (file, startSec, channels) => ipcRenderer.invoke('player-open', file, startSec, channels),
  read: (id, bytes, channels) => ipcRenderer.invoke('player-read', id, bytes, channels),
  close: (id) => ipcRenderer.invoke('player-close', id),
  saveM3u: (tracks) => ipcRenderer.invoke('player-m3u-save', tracks),
  reveal: (p) => ipcRenderer.invoke('player-reveal', p),
  exists: (paths) => ipcRenderer.invoke('player-exists', paths),
  load: () => ipcRenderer.invoke('player-load'),
  save: (data) => ipcRenderer.invoke('player-save', data),
  pathFor: (file) => { try { return webUtils.getPathForFile(file); } catch (e) { return ''; } },
});
