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
