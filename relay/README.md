# pump ROOM pairing relay

PHONE MIC in pump ROOM uses a phone as the measurement mic for a computer that plays the sweeps. The two pages need
to swap a few hundred bytes to open a direct connection to each other over the local network; this relay passes
them. No audio goes through it, nothing is stored, and both pages disconnect from it as soon as the direct
connection is open.

- `core.mjs`: the pairing rooms (one per 6-character code, a `player` and a `phone` side).
- `worker.mjs`, `wrangler.toml`: the hosted relay, for a serverless host with WebSocket support and one small
  stateful object per code (free plan). Deploy from this folder with the host's command-line tool (log in, then
  deploy); it prints the relay's address (`https://…`).
- `local.mjs`: the same relay on this computer (`node relay/local.mjs [port]`, needs the `ws` package): for tests,
  or to try PHONE MIC without the hosted one.

The page's relay address is `RELAY_URL` in `room/remote.js` (as `wss://…`); a page opened with `#relay=wss://…`
(or `ws://` for a local one) uses that one instead.
