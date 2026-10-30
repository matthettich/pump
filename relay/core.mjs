// pump ROOM pairing relay: the few small messages a computer and a phone swap to open a direct connection to each
// other (PHONE MIC in pump ROOM). Audio never goes through it: once the direct connection is open, both close it.
//
// One pairing room per code. Two roles, `player` (the computer playing the sweeps) and `phone` (the mic). Each side
// is told the ICE servers on joining ({type:'hello', ice}), both are told when the other arrives or leaves
// ({type:'peer', here}), and every other text message goes to the other side as it is. Used by worker.mjs (the hosted
// relay) and local.mjs (the same relay on this computer, for tests).

export const LIMITS = { maxMessage: 16384, maxMessages: 400, lifeMs: 15 * 60 * 1000 };

// 6 characters, no 0/O, 1/I: easy to read off a screen and type if the QR code won't scan
export const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
export const isCode = (c) => typeof c === 'string' && /^[A-HJ-NP-Z2-9]{6}$/.test(c);

export function parseIce(text) {
  try {
    const v = JSON.parse(text || '[]');
    return Array.isArray(v) ? v.filter((s) => s && (typeof s.urls === 'string' || Array.isArray(s.urls))) : [];
  } catch (e) {
    return [];
  }
}

// sock: { send(text), close(code, reason) }. Returns { message(data), close() } for the caller to feed, or null when
// the role is unknown (the socket is closed).
export function makeRoom(ice) {
  const peers = { player: null, phone: null };
  const say = (p, o) => { try { p.sock.send(JSON.stringify(o)); } catch (e) { /* gone */ } };
  return {
    join(role, sock) {
      if (role !== 'player' && role !== 'phone') { sock.close(1008, 'role'); return null; }
      const other = role === 'player' ? 'phone' : 'player';
      if (peers[role]) peers[role].sock.close(4000, 'replaced');   // a page reloaded: the new one wins
      const me = { sock, count: 0 };
      peers[role] = me;
      say(me, { type: 'hello', ice });
      if (peers[other]) { say(me, { type: 'peer', here: true }); say(peers[other], { type: 'peer', here: true }); }
      return {
        message(data) {
          if (typeof data !== 'string' || data.length > LIMITS.maxMessage || ++me.count > LIMITS.maxMessages) {
            sock.close(1009, 'limit');
            return;
          }
          const o = peers[other];
          if (o) { try { o.sock.send(data); } catch (e) { /* gone */ } }
        },
        close() {
          if (peers[role] !== me) return;
          peers[role] = null;
          if (peers[other]) say(peers[other], { type: 'peer', here: false });
        },
      };
    },
    empty: () => !peers.player && !peers.phone,
  };
}
