// SPDX-License-Identifier: GPL-3.0-only
//
// The page's side of the audition: the AudioContext, the worklet, and the
// song and bank it is holding. A browser only starts audio on a gesture, so
// nothing here happens until start() is called from a click.
//
// The engine is the FM-1's own core compiled to wasm (audition.c, built by
// web/songedit/build.sh). Without that file the page still opens, edits and
// writes songs -- audition is the one thing that needs it, and says so.

export class Audition {
  constructor({ wasmUrl, workletUrl }) {
    this.wasmUrl = wasmUrl;
    this.workletUrl = workletUrl;
    this.ctx = null;
    this.node = null;
    this.ready = false;
    this.bankCount = 0;
    this.onState = () => {};
    this.onError = () => {};
    this.state = { playing: false, row: 0, entry: 0 };
  }

  // Throws with a plain message when the wasm is missing: the page shows it.
  async start() {
    if (this.ready) return;
    const r = await fetch(this.wasmUrl, { cache: "no-cache" });
    if (!r.ok)
      throw new Error(`${this.wasmUrl}: HTTP ${r.status}. Build it with web/songedit/build.sh (it needs zig or emcc).`);
    const wasm = await r.arrayBuffer();
    this.ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
    await this.ctx.audioWorklet.addModule(this.workletUrl);
    this.node = new AudioWorkletNode(this.ctx, "audition", { numberOfInputs: 0, outputChannelCount: [2] });
    const waiting = new Map();
    this.node.port.onmessage = ({ data: m }) => {
      if (m.type === "state") { this.state = m; this.onState(m); return; }
      if (m.type === "error") { this.onError(new Error(m.message)); return; }
      const w = waiting.get(m.type);
      if (w) { waiting.delete(m.type); w(m); }
    };
    this.await1 = (type) => new Promise((res) => waiting.set(type, res));
    this.node.connect(this.ctx.destination);
    const first = this.await1("ready");
    this.node.port.postMessage({ type: "load", wasm }, [wasm]);
    const got = await Promise.race([first, new Promise((_, rej) =>
      setTimeout(() => rej(new Error("the audition engine did not start")), 8000))]);
    await this.ctx.resume();
    this.ready = true;
    this.rate = got.rate;
  }

  // The bank's own bytes, so the samples really sound. The index alone (read
  // over the link) is not enough to hear anything -- that is why a packed
  // bank file is asked for separately.
  async loadBank(bytes) {
    if (!this.ready) return 0;
    const copy = bytes.slice().buffer;
    const p = this.await1("bank");
    this.node.port.postMessage({ type: "bank", bytes: copy }, [copy]);
    const m = await p;
    this.bankCount = m.count;
    if (m.tooBig) throw new Error("that bank is larger than the FM-1's flash can hold");
    return m.count;
  }

  // The song as the device would read it: the .trk text trk.js just wrote, so
  // what is auditioned is the file that will be sent.
  async loadSong(text) {
    if (!this.ready) return false;
    const buf = new TextEncoder().encode(text).buffer;
    const p = this.await1("song");
    this.node.port.postMessage({ type: "song", text: buf }, [buf]);
    return (await p).ok;
  }

  noteOn(instr, note, syl = "") { this.node?.port.postMessage({ type: "noteOn", instr, note, syl }); }
  noteOff() { this.node?.port.postMessage({ type: "noteOff" }); }
  playSong(entry = 0) { this.node?.port.postMessage({ type: "playSong", entry }); }
  playEntry(entry = 0) { this.node?.port.postMessage({ type: "playEntry", entry }); }
  stop() { this.node?.port.postMessage({ type: "stop" }); }
  master(v) { this.node?.port.postMessage({ type: "master", value: v }); }
}
