// SPDX-License-Identifier: GPL-3.0-only
//
// The audition engine in an AudioWorklet: audition.wasm is the FM-1's core
// (core/player.c and the engines under it), so what is heard here is what the
// device plays. The shape is platform/web/worklet.js's -- each render quantum
// asks the module for that many frames, and the module's clock is the audio.
//
// Unlike platform/web/ this is not the firmware: there is no panel, no flash
// and no main loop. A song, a bank, and notes.
class Audition extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.sent = "";
    this.since = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        const ex = instance.exports;
        if (ex._initialize) ex._initialize();
        this.ex = ex;
        this.port.postMessage({ type: "ready", rate: ex.aud_rate() });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String((err && err.message) || err) });
      }
      return;
    }
    const ex = this.ex;
    if (!ex) return;
    if (m.type === "bank") {
      const b = new Uint8Array(m.bytes);
      let n = 0;
      if (b.length <= ex.aud_bank_cap()) {
        new Uint8Array(ex.memory.buffer, ex.aud_bank_buf(), b.length).set(b);
        n = ex.aud_bank_mount(b.length);
      }
      this.port.postMessage({ type: "bank", count: n, tooBig: b.length > ex.aud_bank_cap() });
    } else if (m.type === "song") {
      const t = new Uint8Array(m.text);
      let ok = 0;
      if (t.length <= ex.aud_song_cap()) {
        new Uint8Array(ex.memory.buffer, ex.aud_song_buf(), t.length).set(t);
        ok = ex.aud_song_load(t.length);
      }
      this.port.postMessage({ type: "song", ok: !!ok });
    } else if (m.type === "noteOn") {
      if (m.syl) {
        const s = new Uint8Array(ex.memory.buffer, ex.aud_syl_buf(), 8);
        s.fill(0);
        for (let i = 0; i < m.syl.length && i < 5; i++) s[i] = m.syl.charCodeAt(i) & 0x7F;
        ex.aud_note_sing(m.instr, m.note);
      } else {
        ex.aud_note_on(m.instr, m.note);
      }
    } else if (m.type === "noteOff") ex.aud_note_off();
    else if (m.type === "playSong") ex.aud_play_song(m.entry | 0);
    else if (m.type === "playEntry") ex.aud_play_entry(m.entry | 0);
    else if (m.type === "stop") ex.aud_stop();
    else if (m.type === "master") ex.aud_master(m.value | 0);
  }

  process(inputs, outputs) {
    const ex = this.ex, out = outputs[0];
    if (!ex || !out.length) return true;
    const n = out[0].length;
    const pcm = new Int16Array(ex.memory.buffer, ex.aud_render(n), n * 2);
    for (let i = 0; i < n; i++) {
      out[0][i] = pcm[2 * i] / 32768;
      if (out[1]) out[1][i] = pcm[2 * i + 1] / 32768;
    }
    this.since += n;
    if (this.since >= sampleRate / 20) {
      this.since = 0;
      const state = `${ex.aud_playing()},${ex.aud_row()},${ex.aud_entry()}`;
      if (state !== this.sent) {
        this.sent = state;
        this.port.postMessage({ type: "state", playing: !!ex.aud_playing(), row: ex.aud_row(), entry: ex.aud_entry() });
      }
    }
    return true;
  }
}
registerProcessor("audition", Audition);
