// SPDX-License-Identifier: GPL-3.0-only
//
// The installer's JS against a simulated FM-1, for tools/installer_compare.py:
//
//   node web/installer/test/harness.mjs SCENARIO.json RESULT.json
//
// SCENARIO.json is written by installer_compare.py; the simulated FM-1 here
// behaves exactly as the one there (FakeDevice), so the two transcripts of
// what the host sent can be compared byte for byte. Not part of the site.

import fs from "node:fs";
import { Updater, HS_QUERY, UPGRADE, parseIdentity, parseRequest, response, pack7 as otaPack7 } from "../fm1ota.js";
import { checkPackage, validateStockPackage, sha256hex } from "../fm1pkg.js";
import { SongLink, pack7 as mdPack7, unpack7 as mdUnpack7, message, zip } from "../mdlink.js";

const hex = (a) => Buffer.from(a).toString("hex");
const unhex = (s) => [...Buffer.from(s, "hex")];

// node harness.mjs --udp PORT OUT.json: the song link against the simulator
// (build/sim/moondaw-fm1, app/link.c over UDP): the list, and every song read
if (process.argv[2] === "--udp") {
  const dgram = await import("node:dgram");
  const sock = dgram.createSocket("udp4");
  const q = []; let waiter = null;
  sock.on("message", (m) => { q.push([0xF0, ...m, 0xF7]); if (waiter) { const w = waiter; waiter = null; w(); } });
  await new Promise((r) => sock.bind(0, "127.0.0.1", r));
  const port = +process.argv[3], sent = [];
  const link = {
    drain() { q.length = 0; },
    send(m) { const body = Buffer.from(m.slice(1, -1)); sent.push(body.toString("hex")); sock.send(body, port, "127.0.0.1"); return true; },
    async read(ms) {
      if (!q.length) { let t; await Promise.race([new Promise((r) => { waiter = r; }), new Promise((r) => { t = setTimeout(r, ms); })]); clearTimeout(t); waiter = null; }
      return q.shift() || null;
    },
  };
  const sl = new SongLink(link);
  const ping = await sl.ping(), list = await sl.list(), got = [];
  for (const s of list) got.push([s.name, Buffer.from(await sl.fetch(s.slot)).toString("hex")]);
  fs.writeFileSync(process.argv[4], JSON.stringify({ ping, list: list.map((s) => [s.slot, s.name, s.size]), got, sent }));
  process.exit(0);
}

const sc = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));

// ------------------------------------------------------- simulated FM-1 ---

function identityPkt(text) {
  const d = [0x00, 0x59, 0x11, 0x1D, 0x00, 0x00];
  for (let i = 0; i < 27; i++) d.push(i < text.length ? text.charCodeAt(i) : 0);
  d.push(0x00);
  return [0xF0, ...otaPack7(d), 0xF7];
}

function requestPkt(fl, addr, n) {
  const u = [0x00, 0x59, 0x30, 0x08, 0x00, 0x00, fl, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF];
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  u.push(~s & 0xFF);
  return [0xF0, ...otaPack7(u), 0xF7];
}

const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

// the same states and rules as installer_compare.py FakeDevice
class FakeDevice {
  constructor(s) {
    this.s = s; this.phase = s.start; this.sent = []; this.reqs = []; this.i = 0; this.finishing = false;
    this.ports = []; this.access = { inputs: new Map(), outputs: new Map() };
    this.publish();
  }
  portName() { return { app: this.s.app_port, loader: this.s.loader_port, back: this.s.back_port }[this.phase] || null; }
  ident() { return { app: this.s.app_id, loader: this.s.loader_id, back: this.s.back_id }[this.phase]; }
  publish() {
    for (const p of this.ports) p.state = "disconnected";
    this.ports = [];
    this.access.inputs.clear(); this.access.outputs.clear();
    const name = this.portName();
    if (!name) return;
    const dev = this;
    const input = { name, state: "connected", onmidimessage: null, open: async () => {} };
    const output = { name, state: "connected", open: async () => {},
      send(bytes) { if (this.state === "disconnected") throw new Error("disconnected"); dev.receive(Array.from(bytes)); } };
    this.ports = [input, output];
    this.access.inputs.set("in-" + name, input);
    this.access.outputs.set("out-" + name, output);
    // another MIDI device the installer must leave alone
    this.access.inputs.set("other", { name: "Some Synth", state: "connected", open: async () => { dev.sent.push("PROBED-OTHER-GEAR"); throw new Error("probed"); } });
  }
  emit(pkt) {
    const input = this.ports[0];
    setTimeout(() => { if (input && input.state !== "disconnected" && input.onmidimessage) input.onmidimessage({ data: Uint8Array.from(pkt) }); }, 0);
  }
  advance(to) { this.phase = to; this.publish(); }
  receive(pkt) {
    this.sent.push(hex(pkt));
    if (same(pkt, HS_QUERY)) { if (this.ident()) this.emit(identityPkt(this.ident())); return; }
    if (same(pkt, UPGRADE)) {
      const st = this.s[this.phase];
      if (!st) return;
      this.reqs = st.reqs; this.finish = st.finish; this.finishAddr = this.phase === "app" ? 0xE0000000 : 0xF0000000;
      this.dropAt = st.drop_at ?? -1; this.i = 0; this.finishing = false;
      this.next(); return;
    }
    if (this.finishing) { this.finishing = false; this.advance(this.phase === "app" ? "loader" : "back"); return; }
    this.i++;
    this.next();
  }
  next() {
    if (this.i === this.dropAt) { this.advance("gone"); return; }
    if (this.i < this.reqs.length) { const [fl, a, n] = this.reqs[this.i]; this.emit(requestPkt(fl, a, n)); return; }
    if (this.finish) { this.finishing = true; this.emit(requestPkt(0, this.finishAddr, 8)); }
  }
}

// --------------------------------------------------------------- runs ---

const delay = { open: 0, start: 0, reply: 0, loader: 0, reboot: 0, retry: 10, poll: 20, hs: 200,
  idleCheck: 300, idleWrite: 300, waitLoader: 1000, waitReboot: 1000 };

const out = { scenarios: {} };
const pkgBytes = new Uint8Array(fs.readFileSync(sc.package));
const p = sc.stock ? await validateStockPackage(pkgBytes) : await checkPackage(pkgBytes);
out.product = p.product;
out.image_sha256 = await sha256hex(p.image);
out.image_len = p.image.length;

for (const s of sc.scenarios) {
  const dev = new FakeDevice(s);
  const up = new Updater(dev.access, delay);
  const steps = [];
  let result;
  try {
    const found = await up.find();
    if (!found) result = "error:notfound";
    else result = "ok:" + await up.install(found, p.image, s.product || p.product, (k, a) => steps.push([k, a ?? null]));
  } catch (e) { result = "error:" + (e.code || e.message); }
  out.scenarios[s.name] = { result, steps, sent: dev.sent };
}

// ------------------------------------------------------------ units ---

out.identities = sc.identities.map((h) => { const id = parseIdentity(unhex(h)); return id && id.text; });
out.requests = sc.requests.map((h) => { const r = parseRequest(unhex(h)); return r && [r.fl, r.addr, r.len]; });
out.responses = sc.responses.map(([addr, data, fl]) => hex(response(addr, unhex(data), fl)));
out.md_pack7 = sc.md_data.map((h) => hex(mdPack7(unhex(h))));
out.md_unpack7 = sc.md_data.map((h) => hex(mdUnpack7(mdPack7(unhex(h)))));
out.md_messages = sc.md_messages.map(([cmd, h]) => hex(message(cmd, unhex(h)).slice(1, -1)));

// the song link against a simulated MOONDAW (installer_compare.py FakeSongs)
{
  const songs = sc.songs.map((x) => x && [x[0], unhex(x[1])]);
  const sent = [];
  const SIG = [0x7D, 0x4D, 0x44];
  const fake = { q: [], drain() { this.q = []; },
    send(m) {
      const body = m.slice(1, -1); sent.push(hex(body));
      const cmd = body[3], raw = mdUnpack7(body.slice(4));
      let ans;
      if (cmd === 5) ans = [5, 0, 4, 0, 0x10, 0x6D, 0x04, 0x00];          // version 4, 290 064 bytes free
      if (cmd === 6) { const sl = raw[0], s = songs[sl]; ans = s ? [6, 0, songs.length, 0, s[1].length & 0xFF, s[1].length >> 8, 0, 0, ...[...s[0]].map((c) => c.charCodeAt(0))] : [6, 9, songs.length, 0, 0, 0, 0, 0]; }
      if (cmd === 7) {
        const sl = raw[0], off = raw[1] | raw[2] << 8 | raw[3] << 16 | raw[4] << 24, piece = songs[sl][1].slice(off, off + 120);
        ans = [7, 0, piece.length, 0, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF, 0, ...piece];
      }
      this.q.push([0xF0, ...SIG, 0x7F, ...mdPack7(ans), 0xF7]); return true; },
    async read() { return this.q.shift() || null; } };
  const sl = new SongLink(fake, 50);
  const ping = await sl.ping();
  const list = await sl.list();
  const got = [];
  for (const s of list) got.push([s.name, hex(await sl.fetch(s.slot))]);
  out.songlink = { ping, list: list.map((s) => [s.slot, s.name, s.size]), got, sent };
  out.zip = hex(zip(got.map(([n, h]) => ({ name: n + ".trk", data: Uint8Array.from(unhex(h)) })), new Date(2026, 9, 29, 12, 0, 0)));
}

fs.writeFileSync(process.argv[3], JSON.stringify(out));
process.exit(0);
