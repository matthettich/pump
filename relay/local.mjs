// pump ROOM pairing relay on this computer (tests, or trying PHONE MIC without the hosted one): the same rooms as
// worker.mjs over a plain WebSocket server. Needs the `ws` package.
//   node relay/local.mjs [port]      (default 8787; the page takes it as #relay=ws://HOST:PORT)
import { WebSocketServer } from 'ws';
import { makeRoom, isCode } from './core.mjs';

const port = +process.argv[2] || 8787;
const rooms = new Map();
const wss = new WebSocketServer({ port });
wss.on('connection', (ws, req) => {
  const url = new URL(req.url, 'http://relay');
  const m = url.pathname.match(/^\/pair\/([^/]+)$/);
  if (!m || !isCode(m[1])) { ws.close(1008, 'code'); return; }
  let room = rooms.get(m[1]);
  if (!room) rooms.set(m[1], (room = makeRoom([])));
  const h = room.join(url.searchParams.get('role'), { send: (t) => ws.send(t), close: (c, r) => ws.close(c, r) });
  if (!h) return;
  ws.on('message', (data, binary) => h.message(binary ? null : data.toString()));
  ws.on('close', () => { h.close(); if (room.empty()) rooms.delete(m[1]); });
});
wss.on('listening', () => console.log(`pairing relay on ws://localhost:${port}`));
