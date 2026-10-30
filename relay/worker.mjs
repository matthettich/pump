// pump ROOM pairing relay, hosted: a serverless worker with one small stateful object per pairing code (so both
// sides of a code meet in the same place). See core.mjs for what it passes, and README.md to set it up.
import { makeRoom, isCode, parseIce, LIMITS } from './core.mjs';

export class PairRoom {
  constructor(state, env) {
    this.room = makeRoom(parseIce(env.ICE_SERVERS));
  }

  async fetch(request) {
    const role = new URL(request.url).searchParams.get('role');
    const [client, server] = Object.values(new WebSocketPair());
    server.accept();
    const h = this.room.join(role, {
      send: (t) => server.send(t),
      close: (code, reason) => { try { server.close(code, reason); } catch (e) { /* closed */ } },
    });
    if (h) {
      server.addEventListener('message', (e) => h.message(e.data));
      server.addEventListener('close', () => h.close());
      server.addEventListener('error', () => h.close());
      // pairing takes seconds: nobody needs a relay connection for long
      setTimeout(() => { try { server.close(1000, 'time'); } catch (e) { /* closed */ } }, LIMITS.lifeMs);
    }
    return new Response(null, { status: 101, webSocket: client });
  }
}

export default {
  async fetch(request, env) {
    const m = new URL(request.url).pathname.match(/^\/pair\/([^/]+)$/);
    if (!m) return new Response('pump ROOM pairing relay\n', { headers: { 'content-type': 'text/plain' } });
    if (!isCode(m[1])) return new Response('bad code\n', { status: 400 });
    if (request.headers.get('Upgrade') !== 'websocket') return new Response('websocket only\n', { status: 426 });
    return env.PAIR.get(env.PAIR.idFromName(m[1])).fetch(request);
  },
};
