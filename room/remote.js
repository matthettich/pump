// pump ROOM: PHONE MIC, a phone as the measurement mic. A mode of its own, beside the rest of the page: nothing here
// runs until it's used, and it doesn't change how MEASURE or HANDHELD work. It reads the SWEEP settings and hands its
// measurements to the same list (as ordinary runs), so averaging, the correction and the exports take them as they are.
//
// Two pages: the computer (the "player": it plays the sweeps through its output, wired to the speakers, and does all
// the analysis) and the phone (the mic, with a USB measurement mic plugged in). The computer shows a QR code; the phone
// opens the same page with #phone=CODE. A small relay (relay/) passes the few messages that open a direct connection
// between the two pages over the local network (WebRTC); after that the relay is closed and everything goes directly.
//
// Timing across two devices. On one device, playing and recording share one clock, so a run's arrivals are exact.
// Here the phone can't know when the computer starts playing (network delay), and the two clocks run at slightly
// different rates (up to ~100 ppm: 0.1 ms per second). So a run is located and measured from the recording itself:
//   - the phone records from before it says "ready" until well after the run must have ended;
//   - the first sweep is found anywhere in the first seconds (that fixes the start);
//   - the run starts and ends with the same short ping from the first speaker: the mic hasn't moved, so the time
//     between their arrivals gives the ratio of the two clocks (two identical pings: a clock difference stretches a
//     sweep's response a little, by its length, so comparing a long sweep with a short ping would be off);
//   - the recording is resampled onto the computer's clock (so no sweep is stretched), and each sweep is cut out
//     where it landed, between samples.
// So the arrival difference between the speakers (time alignment, polarity, phase) is as good as on one device, and
// each run carries its own clock check. The absolute arrival isn't the latency + distance any more (it starts a set
// lead before the first speaker's sound): HANDHELD isn't offered with the phone.
//
// Messages (JSON over the data channel; binary chunks follow a {t:'blob'} header, in order):
//   phone -> computer  {t:'mic', ready, label}  {t:'meter', db}  {t:'noise', on}  {t:'measure', name}  {t:'stop'}
//                      {t:'cal', name, text}    {t:'recording', id, ok, why}      blob 'rec' (the recording)
//   computer -> phone  {t:'state', ...}  {t:'record', id, length, warm}  {t:'progress', p}  {t:'says', text, err}
//                      {t:'abort', id}   blobs 'simL' / 'simR' (test mode: the run itself, for the simulated room)
'use strict';

// The relay's address (relay/README.md). Empty: PHONE MIC says it isn't set up. #relay=… overrides it.
const RELAY_URL = '';

const PHONE = {
  web: 'https://matthettich.github.io/pump/',
  fs: 48000,                 // both pages run at this rate, so only the clocks' drift differs
  preS: 0.5, tailS: 2.0,     // silence before the first sweep; room decay after each
  pingS: 0.25, pingTailS: 0.8,
  warmS: 0.5,                // the phone records this long before it says it's ready
  searchS: 3.0,              // how late the first sweep may land (network + output latency)
  leadS: 0.1,                // each sweep is cut out starting this long before its arrival
  maxPpm: 1000,              // a clock difference beyond this means the reference ping was missed
  chunk: 65536,
  meterEvery: 200,           // ms between the phone's level reports
};

const P = {
  role: null, code: '', relay: '', relayGiven: false, ws: null, pc: null, dc: null, ice: [], iceQueue: [], open: false,
  sim: /(^|[#&?])sim\b/.test(location.hash), simPpm: 150,
  waits: new Map(), early: new Map(), inBlob: null, busy: false, runId: 0, abort: false,
  // player
  ctx: null, src: null, noiseSrc: null, noise: false, phoneMic: null, phoneDb: null,
  // phone
  micCtx: null, micStream: null, micNode: null, micSrc: null, micId: '', micReady: false, recWait: null, lastMeter: 0,
  retry: null, wake: null,
};

(() => {
  const h = location.hash;
  const r = h.match(/[#&]relay=([^&]+)/);
  P.relay = r ? decodeURIComponent(r[1]) : RELAY_URL;
  P.relayGiven = !!r;
  const ppm = h.match(/[#&]ppm=(-?[\d.]+)/);
  if (ppm) P.simPpm = +ppm[1];
  const c = h.match(/[#&]phone=([A-Za-z0-9]{6})/);
  if (c) { P.role = 'phone'; P.code = c[1].toUpperCase(); }
  if (P.code && !/^[A-HJ-NP-Z2-9]{6}$/.test(P.code)) P.code = '';
})();

const pq = (sel) => document.querySelector(sel);
const sideName = (s) => (s === 'L' ? 'left' : 'right');

// ---------------------------------------------------------------------------------------------------------------------
// the link: the relay until the direct connection is open, then the data channel

function relaySend(o) { if (P.ws && P.ws.readyState === 1) P.ws.send(JSON.stringify(o)); }

function linkStart() {
  linkClose(false);
  const ws = new WebSocket(P.relay.replace(/\/+$/, '').replace(/^http/, 'ws') + '/pair/' + P.code + '?role=' + P.role);
  P.ws = ws;
  ws.onmessage = async (e) => {
    let m;
    try { m = JSON.parse(e.data); } catch (err) { return; }
    try {
      if (m.type === 'hello') P.ice = Array.isArray(m.ice) ? m.ice : [];
      else if (m.type === 'peer' && m.here && P.role === 'player' && !P.open) await makeOffer();
      else if (m.type === 'sdp') await gotSdp(m.sdp);
      else if (m.type === 'ice' && m.c) {
        if (P.pc && P.pc.remoteDescription) await P.pc.addIceCandidate(m.c).catch(() => {});
        else P.iceQueue.push(m.c);
      }
    } catch (err) { linkNote('Could not connect: ' + err.message, true); }
  };
  ws.onclose = () => {
    if (P.ws !== ws) return;
    P.ws = null;
    if (!P.open) {
      linkNote(P.role === 'phone' ? 'Can\'t reach the computer yet. Trying again…' : 'The pairing relay closed. Reconnecting…', true);
      scheduleRetry();
    }
  };
  ws.onerror = () => {};
}

function scheduleRetry() {
  clearTimeout(P.retry);
  P.retry = setTimeout(() => { if (!P.open && P.role) linkStart(); }, 2500);
}

function newPc() {
  if (P.pc) { try { P.pc.close(); } catch (e) {} }
  const pc = new RTCPeerConnection({ iceServers: P.ice });
  P.pc = pc;
  P.iceQueue = [];
  pc.onicecandidate = (e) => { if (e.candidate) relaySend({ type: 'ice', c: e.candidate.toJSON() }); };
  pc.onconnectionstatechange = () => {
    if (pc !== P.pc) return;
    if (pc.connectionState === 'failed') {
      linkNote('The direct connection failed: are both on the same Wi-Fi network?', true);
      linkLost();
    }
  };
  return pc;
}

async function makeOffer() {
  const pc = newPc();
  useChannel(pc.createDataChannel('room', { ordered: true }));
  await pc.setLocalDescription(await pc.createOffer());
  relaySend({ type: 'sdp', sdp: pc.localDescription });
}

async function gotSdp(sdp) {
  if (sdp.type === 'offer') {
    const pc = newPc();
    pc.ondatachannel = (e) => useChannel(e.channel);
    await pc.setRemoteDescription(sdp);
    await pc.setLocalDescription(await pc.createAnswer());
    relaySend({ type: 'sdp', sdp: pc.localDescription });
  } else if (P.pc) {
    await P.pc.setRemoteDescription(sdp);
  }
  for (const c of P.iceQueue.splice(0)) await P.pc.addIceCandidate(c).catch(() => {});
}

function useChannel(dc) {
  dc.binaryType = 'arraybuffer';
  dc.bufferedAmountLowThreshold = PHONE.chunk;
  dc.onopen = () => {
    P.dc = dc;
    P.open = true;
    clearTimeout(P.retry);
    // the relay's job is done (a moment's grace for the last candidates)
    setTimeout(() => { if (P.open && P.ws) { const ws = P.ws; P.ws = null; ws.close(); } }, 1500);
    linked();
  };
  dc.onclose = () => { if (P.dc === dc) linkLost(); };
  dc.onmessage = (e) => {
    if (typeof e.data === 'string') {
      let m;
      try { m = JSON.parse(e.data); } catch (err) { return; }
      if (m.t === 'blob') { P.inBlob = { kind: m.kind, id: m.id, data: new Float32Array(m.n), got: 0 }; if (m.n === 0) blobDone(); return; }
      gotMsg(m);
    } else if (P.inBlob) {
      const b = P.inBlob, bytes = new Uint8Array(b.data.buffer);
      const chunk = new Uint8Array(e.data);
      bytes.set(chunk.subarray(0, Math.max(0, bytes.length - b.got)), b.got);
      b.got += chunk.length;
      if (b.got >= bytes.length) blobDone();
    }
  };
}

function blobDone() {
  const b = P.inBlob;
  P.inBlob = null;
  resolveWait('blob:' + b.kind + ':' + b.id, b.data);
}

function say(o) { if (P.dc && P.dc.readyState === 'open') P.dc.send(JSON.stringify(o)); }

async function sendBlob(kind, id, f32) {
  const dc = P.dc;
  if (!dc || dc.readyState !== 'open') throw new Error('Not connected');
  dc.send(JSON.stringify({ t: 'blob', kind, id, n: f32.length }));
  const bytes = new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength);
  for (let i = 0; i < bytes.length; i += PHONE.chunk) {
    while (dc.bufferedAmount > 8 * PHONE.chunk) {
      if (dc.readyState !== 'open') throw new Error('Connection lost');
      await new Promise((res) => { const t = setTimeout(res, 50); dc.onbufferedamountlow = () => { clearTimeout(t); res(); }; });
    }
    dc.send(bytes.slice(i, Math.min(bytes.length, i + PHONE.chunk)));
  }
}

// waits for a message ('recording:3') or a blob ('blob:rec:3'), rejected when the link goes or after `ms`
function waitFor(key, ms) {
  if (P.early.has(key)) { const v = P.early.get(key); P.early.delete(key); return Promise.resolve(v); }
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => { P.waits.delete(key); reject(new Error('No answer from the ' + (P.role === 'player' ? 'phone' : 'computer'))); }, ms);
    P.waits.set(key, { resolve: (v) => { clearTimeout(t); resolve(v); }, reject: (e) => { clearTimeout(t); reject(e); } });
  });
}
// an answer that came before anyone asked is kept for the next waitFor (only the latest few)
function resolveWait(key, v) {
  const w = P.waits.get(key);
  if (w) { P.waits.delete(key); w.resolve(v); return; }
  P.early.set(key, v);
  while (P.early.size > 4) P.early.delete(P.early.keys().next().value);
}
function rejectWaits(why) { for (const w of P.waits.values()) w.reject(new Error(why)); P.waits.clear(); P.early.clear(); }

function linkClose(announce) {
  clearTimeout(P.retry);
  if (P.ws) { const ws = P.ws; P.ws = null; try { ws.close(); } catch (e) {} }
  if (P.dc) { const dc = P.dc; P.dc = null; try { dc.close(); } catch (e) {} }
  if (P.pc) { const pc = P.pc; P.pc = null; try { pc.close(); } catch (e) {} }
  P.inBlob = null;
  const was = P.open;
  P.open = false;
  rejectWaits('Disconnected');
  if (announce && was) linkNote('Disconnected.', false);
}

function linkLost() {
  const was = P.open;
  linkClose(false);
  if (P.role === 'player') { stopPlayerNoise(); if (was) playerNote('The phone disconnected. Scan the code again, or reopen the page on the phone.', true); }
  if (P.role) { showLink(); linkStart(); }
}

function linkNote(text, err) { if (P.role === 'phone') phoneSays(text, err); else playerNote(text, err); }
function showLink() { if (P.role === 'phone') showPhone(); else showPlayer(); }
function linked() { if (P.role === 'phone') phoneLinked(); else playerLinked(); }
function gotMsg(m) { if (P.role === 'phone') phoneGot(m); else playerGot(m); }

// ---------------------------------------------------------------------------------------------------------------------
// the computer: the PHONE MIC dialog, playback, and the analysis

function newCode() {
  const r = new Uint8Array(6);
  crypto.getRandomValues(r);
  return Array.from(r, (v) => 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'[v & 31]).join('');
}

function phoneUrl() {
  const base = location.protocol === 'https:' ? location.origin + location.pathname : PHONE.web;
  return base + '#phone=' + P.code + (P.relayGiven ? '&relay=' + encodeURIComponent(P.relay) : '') + (P.sim ? '&sim&ppm=' + P.simPpm : '');
}

function playerOpen() {
  pq('#phoneDlg').hidden = false;
  if (!P.relay) { showPlayer(); return; }
  if (P.role !== 'player') {
    P.role = 'player';
    // the same code as last time (kept across a reload): a phone still on the page finds this computer again by itself
    if (!P.code) {
      const kept = P.sim ? '' : recall('phone-code');
      P.code = /^[A-HJ-NP-Z2-9]{6}$/.test(kept || '') ? kept : newCode();
      if (!P.sim) store('phone-code', P.code);
    }
    linkStart();
  }
  showPlayer();
}

function playerDisconnect() {
  stopPlayerNoise();
  linkClose(true);
  P.role = null;
  P.phoneMic = null;
  showPlayer();
}

function playerLinked() {
  say({ t: 'hi', role: 'player' });
  sendState();
  playerNote('Phone connected.', false);
  showPlayer();
}

function sendState() {
  const out = $('out') && !$('out').disabled && $('out').selectedOptions[0] ? $('out').selectedOptions[0].textContent : 'the system output';
  say({ t: 'state', chans: S.chans, seconds: S.seconds, level: S.level, out, busy: P.busy, noise: P.noise, bt: S.bt,
        cal: S.cal ? S.cal.name : '' });
}

function playerGot(m) {
  if (m.t === 'mic') { P.phoneMic = m.ready ? (m.label || 'mic') : null; showPlayer(); }
  else if (m.t === 'meter') { P.phoneDb = m.db; showPhoneMeter(); }
  else if (m.t === 'noise') setPlayerNoise(!!m.on);
  else if (m.t === 'measure') playerMeasure(m.name || '');
  else if (m.t === 'stop') { P.abort = true; if (P.src) { try { P.src.stop(); } catch (e) {} } }
  else if (m.t === 'cal' && typeof m.text === 'string' && m.text.length < 4e6) { applyCal(String(m.name || 'mic.txt'), m.text, true); sendState(); }
  else if (m.t === 'recording') resolveWait('recording:' + m.id, m);
}

function playerNote(text, err) {
  const el = pq('#phNote');
  if (el) { el.textContent = text; el.classList.toggle('warn', !!err); }
  status(text, err);
}

function showPlayer() {
  const body = pq('#phBody');
  if (!body) return;
  const btn = $('phoneMic');
  btn.textContent = P.open ? 'PHONE MIC: CONNECTED' : 'USE A PHONE AS THE MIC';
  btn.classList.toggle('on', P.open);
  if (!P.relay) {
    body.innerHTML = '<p>The pairing relay isn\'t set up in this build yet (relay/README.md).</p>';
    return;
  }
  const want = P.open ? 'linked' : (P.role === 'player' ? 'pair' : 'off');
  if (body.dataset.state !== want) {
    body.dataset.state = want;
    if (want === 'pair') {
      body.innerHTML = `
        <div class="ph-pair">
          <canvas id="phQr" width="240" height="240" aria-label="QR code to open on the phone"></canvas>
          <div>
            <p><b>1.</b> Plug your measurement mic into the phone.</p>
            <p><b>2.</b> Point the phone's camera at this code and open the link. The phone and this computer must be on the same Wi-Fi.</p>
            <p class="ph-code">CODE <b>${P.code}</b></p>
            <p class="ph-url"><a href="${esc(phoneUrl())}" target="_blank" rel="noopener">${esc(phoneUrl())}</a></p>
            <p>This computer plays the sweeps through its output (set OUTPUT once audio is started), so connect it to the speakers the way you listen.</p>
          </div>
        </div>
        <div class="calbox" id="phNote">Waiting for the phone…</div>`;
      drawQr(pq('#phQr'), phoneUrl());
    } else if (want === 'linked') {
      body.innerHTML = `
        <div class="readout" id="phInfo"></div>
        <div class="meter" title="The phone's mic level"><div class="meter-target"></div><div class="meter-bar" id="phBar"></div></div>
        <div class="row small"><span>PHONE MIC</span><b class="num" id="phLevel">—</b></div>
        <div class="keys2"><button class="key" id="phNoise">TEST LEVEL</button><button class="key hot" id="phMeasure">MEASURE</button></div>
        <div class="progress"><div id="phProg"></div></div>
        <div class="calbox" id="phNote">Connected. Start the mic on the phone.</div>
        <div class="keys2"><button class="key" id="phOff">DISCONNECT</button></div>
        <p class="hint">The SWEEP settings on the left apply (speakers, length, level), and the mic's calibration file can be loaded here or on the phone. Measurements join the list like any other.</p>`;
      pq('#phNoise').onclick = () => setPlayerNoise(!P.noise);
      pq('#phMeasure').onclick = () => (P.busy ? (P.abort = true, P.src && P.src.stop()) : playerMeasure(''));
      pq('#phOff').onclick = playerDisconnect;
    } else {
      body.innerHTML = '<p>Not connected.</p><div class="keys2"><button class="key hot" id="phPair">CONNECT A PHONE</button></div><div class="calbox" id="phNote"></div>';
      pq('#phPair').onclick = playerOpen;
    }
  }
  if (want === 'linked') {
    pq('#phInfo').innerHTML = `MIC <b>${P.phoneMic ? esc(P.phoneMic) : 'not started on the phone'}</b> · SPEAKERS <b>${S.chans === 'LR' ? 'BOTH' : (S.chans === 'L' ? 'LEFT' : 'RIGHT')}</b> · SWEEP <b>${S.seconds} S</b> at <b>${S.level} dB</b>`;
    pq('#phMeasure').textContent = P.busy ? 'STOP' : 'MEASURE';
    pq('#phMeasure').disabled = !P.phoneMic && !P.busy;
    pq('#phNoise').disabled = P.busy;
    pq('#phNoise').classList.toggle('active', P.noise);
    pq('#phNoise').textContent = P.noise ? 'STOP NOISE' : 'TEST LEVEL';
    showPhoneMeter();
  }
}

function showPhoneMeter() {
  const bar = pq('#phBar');
  if (!bar) return;
  const db = P.phoneDb;
  bar.style.width = db === null ? '0%' : Math.max(0, Math.min(100, (db + 60) / 60 * 100)) + '%';
  bar.classList.toggle('hot', db !== null && db > -1);
  pq('#phLevel').textContent = db === null || db < -100 ? '—' : db.toFixed(1) + ' dBFS';
}

// the QR code: black modules on white with a 4-module quiet zone (scanners need the contrast, whatever the theme)
function drawQr(canvas, text) {
  if (!canvas || typeof qrcode !== 'function') return;
  const q = qrcode(0, 'M');
  q.addData(text);
  q.make();
  const n = q.getModuleCount(), cell = Math.floor(canvas.width / (n + 8)), off = Math.floor((canvas.width - cell * n) / 2);
  const g = canvas.getContext('2d');
  g.fillStyle = '#fff';
  g.fillRect(0, 0, canvas.width, canvas.height);
  g.fillStyle = '#000';
  for (let r = 0; r < n; ++r) for (let c = 0; c < n; ++c) if (q.isDark(r, c)) g.fillRect(off + c * cell, off + r * cell, cell, cell);
}

async function playerCtx() {
  if (!P.ctx) {
    P.ctx = new AudioContext({ sampleRate: PHONE.fs, latencyHint: 'playback' });
    if (S.outId && P.ctx.setSinkId) { try { await P.ctx.setSinkId(S.outId); } catch (e) {} }
  }
  if (P.ctx.state !== 'running') await P.ctx.resume();
  if (P.ctx.sampleRate !== PHONE.fs) throw new Error('This browser won\'t play at 48 kHz.');
  return P.ctx;
}

async function playOut(out) {
  const ctx = await playerCtx();
  const b = ctx.createBuffer(2, out[0].length, ctx.sampleRate);
  b.copyToChannel(out[0], 0);
  b.copyToChannel(out[1], 1);
  const src = ctx.createBufferSource();
  src.buffer = b;
  src.connect(ctx.destination);
  P.src = src;
  await new Promise((res) => { src.onended = res; src.start(); });
  P.src = null;
}

// pink noise as loud as the sweeps (worklet.js's: the same filter, RMS and soft peaks), looped
function setPlayerNoise(on) {
  stopPlayerNoise();
  if (on && !P.busy && P.open) {
    playerCtx().then((ctx) => {
      if (P.noiseSrc || P.busy) return;
      const n = ctx.sampleRate * 8, l = new Float32Array(n), gain = Math.pow(10, (S.level - 3 + 14.2) / 20);
      const b = [0, 0, 0, 0, 0, 0, 0];
      for (let i = 0; i < n; ++i) {
        const w = Math.random() * 2 - 1;
        b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759;
        b[2] = 0.96900 * b[2] + w * 0.1538520; b[3] = 0.86650 * b[3] + w * 0.3104856;
        b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
        let v = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11 * gain;
        b[6] = w * 0.115926;
        const av = Math.abs(v);
        if (av > 0.7) v = Math.sign(v) * (0.7 + 0.25 * Math.tanh((av - 0.7) / 0.25));
        l[i] = v;
      }
      const buf = ctx.createBuffer(2, n, ctx.sampleRate);
      const flip = S.flip && S.chans === 'LR';
      if (S.chans !== 'R') buf.copyToChannel(l, 0);
      if (S.chans !== 'L') buf.copyToChannel(flip ? l.map((v) => -v) : l, 1);
      const src = ctx.createBufferSource();
      src.buffer = buf; src.loop = true; src.connect(ctx.destination); src.start();
      P.noiseSrc = src;
    }).catch((e) => playerNote('Could not play: ' + e.message, true));
  }
  P.noise = on && !P.busy && P.open;
  sendState();
  showPlayer();
  if (P.noise) playerNote('Test noise playing. Set the volume so the phone\'s MIC LEVEL sits in the shaded zone; the sweeps play at this loudness.', false);
}

function stopPlayerNoise() {
  if (P.noiseSrc) { try { P.noiseSrc.stop(); } catch (e) {} P.noiseSrc = null; }
  P.noise = false;
}

async function playerMeasure(name) {
  if (P.busy || !P.open) return;
  if (S.busy) { playerNote('Wait for the measurement here to finish.', true); return; }
  if (!P.phoneMic) { playerNote('Start the mic on the phone first.', true); say({ t: 'says', text: 'Start the mic first.', err: true }); return; }
  stopPlayerNoise();
  if (S.noise) setNoise(false, false);
  P.busy = true; P.abort = false; S.busy = true;
  sendState(); showPlayer();
  const fs = PHONE.fs, seconds = S.seconds, level = S.level;
  const sides = S.chans === 'LR' ? ['L', 'R'] : [S.chans];
  const id = ++P.runId;
  const progress = (p) => { const el = pq('#phProg'); if (el) el.style.width = (100 * p).toFixed(1) + '%'; say({ t: 'progress', p }); };
  try {
    progress(0);
    // the run: silence, a ping from the first speaker, a sweep from each speaker with its decay, the same ping again
    const pingN = W.x.room_sweep(fs, 10, 0, PHONE.pingS, level);
    const ping = W.f32(W.x.room_sweep_ptr(), pingN).slice();
    const n = W.x.room_sweep(fs, 10, 0, seconds, level);
    const sweep = W.f32(W.x.room_sweep_ptr(), n).slice();
    const pre = Math.round(PHONE.preS * fs), seg = n + Math.round(PHONE.tailS * fs), pseg = pingN + Math.round(PHONE.pingTailS * fs);
    const ping0 = pre, offs = sides.map((_, k) => pre + pseg + k * seg), pingAt = pre + pseg + sides.length * seg;
    const total = pingAt + pseg;
    const out = [new Float32Array(total), new Float32Array(total)];
    sides.forEach((s, k) => out[s === 'L' ? 0 : 1].set(sweep, offs[k]));
    const pc = out[sides[0] === 'L' ? 0 : 1];
    pc.set(ping, ping0);
    pc.set(ping, pingAt);
    if (S.bt) for (const o of out) for (let i = 0; i < total; ++i) if (o[i] === 0) o[i] = (Math.random() * 2 - 1) * BT.keepAlive;
    const length = Math.round((PHONE.warmS + PHONE.searchS) * fs) + total;

    playerNote(`Measuring ${sides.map(sideName).join(' then ')} with the phone… keep quiet and still.`, false);
    say({ t: 'says', text: 'Measuring… keep quiet and still.' });
    say({ t: 'record', id, length, warm: PHONE.warmS });
    const ready = await waitFor('recording:' + id, 8000);
    if (!ready.ok) throw new Error(ready.why || 'The phone couldn\'t record.');
    const t0 = performance.now();
    const ticker = setInterval(() => progress(Math.min(0.9, (performance.now() - t0) / (total / fs * 1000) * 0.9)), 200);
    try {
      if (P.sim) { await sendBlob('simL', id, out[0]); await sendBlob('simR', id, out[1]); }
      else await playOut(out);
    } finally { clearInterval(ticker); }
    if (P.abort) { say({ t: 'abort', id }); throw new Error('Stopped'); }
    playerNote('Getting the recording from the phone…', false);
    const rec = await waitFor('blob:rec:' + id, (length / fs) * 1000 + 30000);
    progress(1);

    const cap = analysePhoneRun(rec, { fs, sides, offs, seg, seconds, level, ping0, pingAt, pingN });
    const label = name.trim() || 'Phone ' + (S.runs.filter((r) => r.phone).length + 1);
    if (cap.fail) throw new Error(cap.fail);
    const run = runFrom(cap, label, seconds, level);
    run.phone = true;
    for (const c of run.ch) c.arrival -= cap.frac[c.side];
    if (run.ch.length === 0) throw new Error(run.notes.join(' ') || 'Nothing was measured.');
    finishRun(run);
    S.runs.push(run);
    S.unsaved = true;
    $('save').disabled = false;
    renderRuns();
    draw();
    const warn = runWarnings(run);
    const clock = cap.ppm === null ? '' : ` (clocks ${Math.abs(cap.ppm).toFixed(0)} ppm apart)`;
    const text = warn.length ? warn.join(' ') : `Measured ${label}${run.dtMs !== undefined && !run.bt ? `: right ${run.dtMs.toFixed(2)} ms after left` : ''}${clock}.`;
    playerNote(text, warn.length > 0);
    say({ t: 'says', text, err: warn.length > 0, done: true });
  } catch (e) {
    const text = e.message === 'Stopped' ? 'Measurement stopped.' : 'Measurement failed: ' + e.message;
    playerNote(text, e.message !== 'Stopped');
    say({ t: 'says', text, err: e.message !== 'Stopped', done: true });
  } finally {
    W.x.room_sweep(fs, 10, 0, seconds, level);   // leave the main sweep set, as MEASURE does
    P.busy = false; S.busy = false; P.src = null;
    progress(0);
    sendState();
    showPlayer();
  }
}

// The phone's recording, from a moment before the run: find it, measure the clocks, cut out each sweep.
// sc: { fs, sides, offs (each sweep's start in the run, samples), seg, seconds, level, ping0 / pingAt (the pings'
// starts), pingN }. Returns a capture() result plus `frac` (per side, the cut's fraction of a sample: subtracted from
// its arrival) and `ppm` (the phone's clock against the computer's), or { fail }.
function analysePhoneRun(rec, sc) {
  const { fs, sides, offs, seg } = sc;
  const lead = PHONE.leadS * fs, pseg = sc.pingN + PHONE.pingTailS * fs;
  const analyse = (x, from, len, search) => {
    const a = Math.max(0, Math.floor(from)), b = Math.min(x.length, a + Math.round(len));
    if (b - a < len * 0.5) return { id: -2, start: a };
    const part = x.subarray(a, b);
    const p = W.putF32(part);
    const id = W.x.room_analyze(p, part.length, Math.round(search));
    W.x.room_release_buf(p);
    return { id, start: a };
  };
  const arrivalAt = (x, from, len, search) => {
    const m = analyse(x, from, len, search);
    if (m.id < 0) return null;
    const at = m.start + W.x.room_info(m.id, 0);
    W.x.room_free(m.id);
    return at;
  };
  // 1) roughly where the run is: the first speaker's sweep, anywhere in the first seconds
  W.x.room_sweep(fs, 10, 0, sc.seconds, sc.level);
  const late = (PHONE.warmS + PHONE.searchS) * fs + (offs[0] - sc.ping0);
  const rough = arrivalAt(rec, 0, late + seg, late);
  if (rough === null) return { fail: `Nothing heard from the ${sideName(sides[0])} speaker: check the output, the volume and the phone's mic.` };
  // 2) the clocks: the two pings (same speaker, same spot, same length) a known time apart on the computer
  W.x.room_sweep(fs, 10, 0, PHONE.pingS, sc.level);
  const p0 = arrivalAt(rec, rough - (offs[0] - sc.ping0) - 2 * lead, pseg, 4 * lead);
  const d = sc.pingAt - sc.ping0, slack = PHONE.maxPpm * 1e-6 * d;
  const p1 = p0 === null ? null : arrivalAt(rec, p0 + d - slack - 2 * lead, pseg + 2 * slack, 4 * lead + 2 * slack);
  if (p0 === null || p1 === null) return { fail: 'A timing ping wasn\'t heard: measure again, and keep the mic still.' };
  const ratio = (p1 - p0) / (sc.pingAt - sc.ping0), ppm = (ratio - 1) * 1e6;
  if (!(Math.abs(ppm) < PHONE.maxPpm)) return { fail: 'The timing pings didn\'t line up: measure again, and keep the mic still.' };
  // 3) the recording on the computer's clock, then each sweep where it landed (a little before its sound)
  const y = Math.abs(ppm) < 0.01 ? rec : resample(rec, ratio);
  const q0 = p0 / ratio;
  W.x.room_sweep(fs, 10, 0, sc.seconds, sc.level);
  const chans = [], frac = {};
  let noiseDb = null;
  {
    const a = Math.max(Math.round(0.05 * fs), Math.round(q0 - 0.45 * fs)), b = Math.round(q0 - 0.03 * fs);
    if (b - a > 0.1 * fs) noiseDb = rmsDb(y.subarray(a, b));
  }
  sides.forEach((side, k) => {
    const exact = q0 + (offs[k] - sc.ping0) - lead, at = Math.floor(exact);
    frac[side] = exact - at;
    const m = analyse(y, at, seg, 2 * lead);
    if (m.id < 0) { chans.push({ side, id: null, note: `Nothing heard from the ${sideName(side)} speaker.` }); return; }
    if (noiseDb !== null && W.x.room_info(m.id, 3) - noiseDb < 10) {
      W.x.room_free(m.id);
      chans.push({ side, id: null, note: `The ${sideName(side)} sweep was not heard over the noise: check the output, the volume and the mic.` });
      return;
    }
    chans.push({ side, id: m.id, note: '' });
  });
  return { fs, noiseDb, dropped: 0, chans, frac, ppm };
}

// x read at every `ratio` samples (x's clock to the computer's), band-limited: a 32-tap Blackman-windowed sinc,
// its phases tabulated at 1/512 of a sample. The ratio is within a fraction of a percent of 1: no filter needed.
const RESAMPLE = (() => {
  const taps = 32, half = taps / 2, phases = 512, t = new Float32Array((phases + 1) * taps);
  for (let p = 0; p <= phases; ++p) {
    const f = p / phases;
    let sum = 0;
    for (let k = 0; k < taps; ++k) {
      const d = k - half + 1 - f;   // tap k sits at x[i - half + 1 + k], the read point at i + f
      const sinc = Math.abs(d) < 1e-9 ? 1 : Math.sin(Math.PI * d) / (Math.PI * d);
      const w = 0.42 + 0.5 * Math.cos(Math.PI * d / half) + 0.08 * Math.cos(2 * Math.PI * d / half);
      t[p * taps + k] = Math.abs(d) >= half ? 0 : sinc * w;
      sum += t[p * taps + k];
    }
    for (let k = 0; k < taps; ++k) t[p * taps + k] /= sum;
  }
  return { taps, half, phases, t };
})();

function resample(x, ratio) {
  const { taps, half, phases, t } = RESAMPLE, n = Math.floor((x.length - 1) / ratio), y = new Float32Array(n);
  for (let j = 0; j < n; ++j) {
    const pos = j * ratio, i = Math.floor(pos), p = Math.round((pos - i) * phases), base = p * taps, i0 = i - half + 1;
    let s = 0;
    if (i0 >= 0 && i0 + taps <= x.length) for (let k = 0; k < taps; ++k) s += x[i0 + k] * t[base + k];
    else for (let k = 0; k < taps; ++k) { const ii = i0 + k; if (ii >= 0 && ii < x.length) s += x[ii] * t[base + k]; }
    y[j] = s;
  }
  return y;
}

// ---------------------------------------------------------------------------------------------------------------------
// the phone: the mic page

const PHONE_WORKLET = `
class PhoneRec extends AudioWorkletProcessor {
  constructor() {
    super();
    this.rec = null; this.pos = 0; this.peak = 0; this.n = 0; this.every = Math.round(sampleRate * 0.05);
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'rec') { this.rec = new Float32Array(m.length); this.pos = 0; }
      else if (m.type === 'stop') { this.rec = null; }
    };
  }
  process(inputs) {
    const x = inputs[0] && inputs[0].length ? inputs[0][0] : null;
    const n = x ? x.length : 128;
    for (let i = 0; i < n; ++i) {
      const v = x ? x[i] : 0, a = Math.abs(v);
      if (a > this.peak) this.peak = a;
      if (this.rec) {
        this.rec[this.pos++] = v;
        if (this.pos >= this.rec.length) { this.port.postMessage({ type: 'done', rec: this.rec }, [this.rec.buffer]); this.rec = null; }
      }
    }
    this.n += n;
    if (this.n >= this.every) { this.port.postMessage({ type: 'meter', peak: this.peak }); this.peak = 0; this.n = 0; }
    return true;
  }
}
registerProcessor('phone-rec', PhoneRec);`;

function phoneInit() {
  document.body.classList.add('phone-mode');
  const page = document.createElement('section');
  page.className = 'panel phone-page';
  page.id = 'phonePage';
  page.innerHTML = `
    <div class="cap">PHONE MIC</div>
    <div class="calbox ph-status" id="ppLink">Connecting to the computer…</div>
    <button class="key wide hot" id="ppStart">START MIC</button>
    <label class="row"><span>MIC</span><select id="ppMic" disabled><option>—</option></select></label>
    <div class="meter" title="Mic level"><div class="meter-target"></div><div class="meter-bar" id="ppBar"></div><div class="meter-hold" id="ppHold"></div></div>
    <div class="row small"><span>MIC LEVEL</span><b class="num" id="ppLevel">—</b></div>
    <label class="row"><span>POSITION</span><input type="text" id="ppPos" value="" placeholder="Listening position" maxlength="40"></label>
    <div class="keys2"><button class="key" id="ppNoise" disabled>TEST LEVEL</button><button class="key hot" id="ppMeasure" disabled>MEASURE</button></div>
    <div class="progress"><div id="ppProg"></div></div>
    <div class="calbox" id="ppSays">Plug in your measurement mic, then START MIC.</div>
    <div class="cap">MIC CALIBRATION</div>
    <div class="calbox" id="ppCalInfo">None loaded on the computer.</div>
    <div class="keys2"><button class="key" id="ppCal" disabled>SEND MIC FILE…</button></div>
    <input type="file" id="ppCalFile" accept=".txt,.cal,.frd,.csv,text/plain" hidden>
    <p class="hint">Use a USB measurement mic: the phone's own mic is tuned for voice and isn't flat. Set the level with the mic where you'll measure, at ear height. Play TEST LEVEL and turn the speakers up until MIC LEVEL sits in the shaded zone. Keep still while it measures.</p>`;
  document.querySelector('main').before(page);
  pq('#ppStart').onclick = phoneStartMic;
  pq('#ppMic').onchange = () => { P.micId = pq('#ppMic').value; store('phone-mic', P.micId); phoneStartMic(); };
  pq('#ppNoise').onclick = () => say({ t: 'noise', on: !P.state?.noise });
  pq('#ppMeasure').onclick = () => (P.state && P.state.busy ? say({ t: 'stop' }) : say({ t: 'measure', name: pq('#ppPos').value }));
  pq('#ppCal').onclick = () => pq('#ppCalFile').click();
  pq('#ppCalFile').onchange = async () => {
    const f = pq('#ppCalFile').files[0];
    pq('#ppCalFile').value = '';
    if (!f) return;
    if (f.size > 4e6) { phoneSays('That file is too large to be a calibration file.', true); return; }
    say({ t: 'cal', name: f.name, text: await f.text() });
  };
  P.micId = recall('phone-mic') || '';
  if (P.sim) { P.micReady = true; P.micLabel = 'Simulated room'; pq('#ppStart').hidden = true; }
  if (!P.relay) { phoneSays('No pairing relay is set up in this build.', true); return; }
  linkStart();
  showPhone();
}

function phoneSays(text, err) {
  const el = pq('#ppSays');
  if (!el) return;
  el.textContent = text;
  el.classList.toggle('warn', !!err);
}

function showPhone() {
  const st = P.state || {};
  pq('#ppLink').textContent = P.open ? `Connected to the computer · SPEAKERS ${st.chans === 'LR' ? 'BOTH' : (st.chans === 'L' ? 'LEFT' : 'RIGHT')} · SWEEP ${st.seconds || '—'} S at ${st.level ?? '—'} dB` : 'Connecting to the computer…';
  pq('#ppLink').classList.toggle('on', P.open);
  const can = P.open && P.micReady;
  pq('#ppNoise').disabled = !can || !!st.busy;
  pq('#ppNoise').textContent = st.noise ? 'STOP NOISE' : 'TEST LEVEL';
  pq('#ppNoise').classList.toggle('active', !!st.noise);
  pq('#ppMeasure').disabled = !can;
  pq('#ppMeasure').textContent = st.busy ? 'STOP' : 'MEASURE';
  pq('#ppCal').disabled = !P.open;
  pq('#ppCalInfo').textContent = st.cal ? 'On the computer: ' + st.cal : 'None loaded on the computer: the mic is taken as flat.';
}

function phoneLinked() {
  say({ t: 'mic', ready: P.micReady, label: P.micLabel || '' });
  phoneSays(P.micReady ? 'Connected. Play TEST LEVEL to set the volume, then MEASURE.' : 'Connected. Plug in your measurement mic, then START MIC.', false);
  keepPhoneAwake(true);
  showPhone();
}

function phoneGot(m) {
  if (m.t === 'state') { P.state = m; showPhone(); }
  else if (m.t === 'says') { phoneSays(m.text, m.err); if (m.done) pq('#ppProg').style.width = '0%'; }
  else if (m.t === 'progress') pq('#ppProg').style.width = (100 * m.p).toFixed(1) + '%';
  else if (m.t === 'record') phoneRecord(m);
  else if (m.t === 'abort') { if (P.micNode) P.micNode.port.postMessage({ type: 'stop' }); if (P.recWait) { P.recWait.reject(new Error('Stopped')); P.recWait = null; } }
}

async function phoneStartMic() {
  await phoneStopMic();
  try {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) throw new Error('This browser has no microphone access here. Use Chrome.');
    const audio = { echoCancellation: false, noiseSuppression: false, autoGainControl: false };
    if (P.micId) audio.deviceId = { exact: P.micId };
    P.micStream = await navigator.mediaDevices.getUserMedia({ audio });
    P.micCtx = new AudioContext({ sampleRate: PHONE.fs, latencyHint: 'interactive' });
    await P.micCtx.audioWorklet.addModule('data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(PHONE_WORKLET))));
    P.micNode = new AudioWorkletNode(P.micCtx, 'phone-rec', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1], channelCount: 1, channelCountMode: 'explicit' });
    P.micNode.port.onmessage = (e) => phoneFromWorklet(e.data);
    P.micSrc = P.micCtx.createMediaStreamSource(P.micStream);
    const sink = P.micCtx.createGain();
    sink.gain.value = 0;   // the worklet only runs when it's pulled: through a silent output
    P.micSrc.connect(P.micNode);
    P.micNode.connect(sink);
    sink.connect(P.micCtx.destination);
    await P.micCtx.resume();
    if (P.micCtx.sampleRate !== PHONE.fs) throw new Error('This phone won\'t record at 48 kHz.');
    const track = P.micStream.getAudioTracks()[0];
    P.micLabel = (track && track.label) || 'mic';
    const devs = (await navigator.mediaDevices.enumerateDevices()).filter((d) => d.kind === 'audioinput');
    const cur = track && track.getSettings ? track.getSettings().deviceId : '';
    pq('#ppMic').innerHTML = devs.map((d, i) => `<option value="${esc(d.deviceId)}">${esc(d.label || 'Input ' + (i + 1))}</option>`).join('');
    pq('#ppMic').value = P.micId || cur;
    pq('#ppMic').disabled = false;
    P.micReady = true;
    pq('#ppStart').textContent = 'RESTART MIC';
    pq('#ppStart').classList.remove('hot');
    keepPhoneAwake(true);
    phoneSays(`Mic running: ${P.micLabel}. Play TEST LEVEL to set the volume, then MEASURE.`, false);
  } catch (e) {
    await phoneStopMic();
    phoneSays(e.name === 'NotAllowedError' ? 'Microphone access was refused. Allow it and try again.' : 'Could not start the mic: ' + e.message, true);
  }
  say({ t: 'mic', ready: P.micReady, label: P.micLabel || '' });
  showPhone();
}

async function phoneStopMic() {
  if (P.recWait) { P.recWait.reject(new Error('Mic stopped')); P.recWait = null; }
  if (P.micNode) { P.micNode.port.onmessage = null; P.micNode.disconnect(); P.micNode = null; }
  if (P.micSrc) { P.micSrc.disconnect(); P.micSrc = null; }
  if (P.micStream) { P.micStream.getTracks().forEach((t) => t.stop()); P.micStream = null; }
  if (P.micCtx) { try { await P.micCtx.close(); } catch (e) {} P.micCtx = null; }
  if (!P.sim) P.micReady = false;
}

let ppHoldDb = -120, ppHoldUntil = 0;
function phoneFromWorklet(m) {
  if (m.type === 'meter') {
    const db = 20 * Math.log10(Math.max(m.peak, 1e-9));
    pq('#ppBar').style.width = Math.max(0, Math.min(100, (db + 60) / 60 * 100)) + '%';
    pq('#ppBar').classList.toggle('hot', db > -1);
    const now = performance.now();
    if (db > ppHoldDb || now > ppHoldUntil) { ppHoldDb = db; ppHoldUntil = now + 1500; }
    pq('#ppHold').style.left = `calc(${Math.max(0, Math.min(100, (ppHoldDb + 60) / 60 * 100))}% - 1px)`;
    pq('#ppLevel').textContent = db < -100 ? '—' : db.toFixed(1) + ' dBFS';
    if (now - P.lastMeter > PHONE.meterEvery) { P.lastMeter = now; say({ t: 'meter', db: ppHoldDb }); }
  } else if (m.type === 'done' && P.recWait) {
    const w = P.recWait;
    P.recWait = null;
    w.resolve(m.rec);
  }
}

async function phoneRecord(m) {
  const { id, length, warm } = m;
  try {
    if (!P.micReady) throw new Error('Start the mic first.');
    if (length > PHONE.fs * 120) throw new Error('That run is too long.');
    let rec;
    if (P.sim) rec = await simRecord(id, length, warm);
    else {
      const done = new Promise((resolve, reject) => { P.recWait = { resolve, reject }; });
      P.micNode.port.postMessage({ type: 'rec', length });
      await new Promise((r) => setTimeout(r, warm * 1000));
      say({ t: 'recording', id, ok: true });
      rec = await done;
    }
    phoneSays('Sending the recording to the computer…', false);
    await sendBlob('rec', id, rec);
  } catch (e) {
    if (e.message !== 'Stopped') say({ t: 'recording', id, ok: false, why: e.message });
  }
}

async function keepPhoneAwake(on) {
  try {
    if (on && navigator.wakeLock && !P.wake) { P.wake = await navigator.wakeLock.request('screen'); P.wake.addEventListener('release', () => { P.wake = null; }); }
    else if (!on && P.wake) { await P.wake.release(); P.wake = null; }
  } catch (e) { P.wake = null; }
}

// Test mode (#sim on both pages): the computer sends the run itself instead of playing it, and the phone puts it
// through a simulated room on its own clock: started at a random moment, its clock `simPpm` fast, the left speaker
// 2.9 m away and the right 3.1 m, 12 ms of latency, one reflection each, a little noise.
async function simRecord(id, length, warm) {
  const fs = PHONE.fs, rec = new Float32Array(length);
  let seed = 99;
  for (let i = 0; i < length; ++i) { seed = (seed * 1664525 + 1013904223) >>> 0; rec[i] = (seed / 4294967296 - 0.5) * 2e-4; }
  say({ t: 'recording', id, ok: true });
  const [L, R] = await Promise.all([waitFor('blob:simL:' + id, 60000), waitFor('blob:simR:' + id, 60000)]);
  const k = 1 + P.simPpm * 1e-6, start = (warm + 0.05 + Math.random() * 0.35) * fs, lat = 0.012 * fs;
  const taps = [[L, 2.9, 0.5], [L, 4.1, 0.2], [R, 3.1, 0.5], [R, 3.8, 0.18]].map(([x, d, g]) => ({ x, d: lat + d / 343 * fs, g }));
  for (let i = Math.floor(start); i < length; ++i) {
    const t = (i - start) / k;   // the computer's sample at this moment of the phone's
    let y = 0;
    for (const tp of taps) {
      const u = t - tp.d, j = Math.floor(u), f = u - j;
      if (j >= 0 && j + 1 < tp.x.length) y += tp.g * (tp.x[j] + f * (tp.x[j + 1] - tp.x[j]));
    }
    rec[i] += y;
  }
  return rec;
}

// ---------------------------------------------------------------------------------------------------------------------

function phoneMicInit() {
  if (P.role === 'phone') { phoneInit(); return; }
  const dlg = document.createElement('div');
  dlg.className = 'guide-wrap';
  dlg.id = 'phoneDlg';
  dlg.hidden = true;
  dlg.innerHTML = `
    <div class="panel guide phone-dlg" role="dialog" aria-modal="true" aria-label="A phone as the mic">
      <div class="guide-head"><span class="cap">PHONE AS THE MIC</span><span class="spacer"></span><button class="key" id="phClose">CLOSE</button></div>
      <div class="guide-body" id="phBody"></div>
    </div>`;
  document.body.appendChild(dlg);
  const close = () => { dlg.hidden = true; };
  pq('#phClose').onclick = close;
  dlg.onclick = (e) => { if (e.target === dlg) close(); };
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !dlg.hidden) close(); });
  $('phoneMic').onclick = playerOpen;
  // the phone shows the SWEEP settings and calibration: keep it up to date
  for (const id of ['chans', 'len', 'level', 'calFile', 'calClear', 'link', 'flip'])
    $(id).addEventListener(id === 'level' ? 'input' : (id === 'calFile' ? 'change' : 'click'), () => setTimeout(() => {
      if (!P.open) return;
      sendState();
      if (P.noise) setPlayerNoise(true);
      showPlayer();
    }, 50));
  showPlayer();
}

phoneMicInit();
window.pumpRoomPhone = { P, PHONE, analysePhoneRun, playerOpen, playerMeasure };   // for tests
