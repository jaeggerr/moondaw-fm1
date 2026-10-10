// SPDX-License-Identifier: GPL-3.0-only
// MOONDAW FM-1 in an AudioWorklet: moondaw.wasm is the app against platform/web/web.c. Each render
// quantum asks it for one half buffer (128 frames), which is what moves its clock; input comes in as
// messages, and about 30 times a second the screen and the lights go out when they have changed.
// The shape is Felucca's web/emu/worklet.js (Leo Kuroshita, GPL-3.0).
class Moondaw extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.sentDraws = -1;
    this.sentLeds = "";
    this.sinceFrame = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        const ex = instance.exports;
        if (ex._initialize) ex._initialize();
        ex.web_flash_erase();
        if (m.flash && m.flash.byteLength === ex.web_flash_size())
          new Uint8Array(ex.memory.buffer, ex.web_flash(), ex.web_flash_size()).set(new Uint8Array(m.flash));
        ex.web_boot();
        this.sentWrites = ex.web_flash_writes();
        this.ex = ex;
        this.port.postMessage({ type: "ready", rate: ex.web_sample_rate() });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String((err && err.message) || err) });
      }
      return;
    }
    const ex = this.ex;
    if (!ex) return;
    if (m.type === "song") {
      const text = new Uint8Array(m.text), name = new TextEncoder().encode(m.name).subarray(0, 19);
      let ok = 0;
      if (text.length <= ex.web_song_cap()) {
        new Uint8Array(ex.memory.buffer, ex.web_song_buf(), text.length).set(text);
        const nm = new Uint8Array(ex.memory.buffer, ex.web_song_name(), 20);
        nm.fill(0);
        nm.set(name);
        ok = ex.web_song_load(text.length);
      }
      this.port.postMessage({ type: "song", ok: !!ok, name: m.name });
    } else if (m.type === "keys") ex.web_keys(m.held >>> 0);
    else if (m.type === "btns") ex.web_btns(m.held >>> 0);
    else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
    else if (m.type === "master") ex.web_master(m.value | 0);
  }

  publish() {
    const ex = this.ex;
    if (ex.web_flash_writes() !== this.sentWrites) {     // a save: the page keeps the flash
      this.sentWrites = ex.web_flash_writes();
      const flash = new Uint8Array(ex.memory.buffer, ex.web_flash(), ex.web_flash_size()).slice();
      this.port.postMessage({ type: "flash", flash: flash.buffer }, [flash.buffer]);
    }
    const leds = ex.web_led_keys() + "," + ex.web_led_btns();
    const draws = ex.web_draws();
    if (draws === this.sentDraws && leds === this.sentLeds) return;
    const msg = { type: "frame", ledKeys: ex.web_led_keys(), ledBtns: ex.web_led_btns() };
    if (draws !== this.sentDraws)
      msg.fb = new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240).slice();
    this.sentDraws = draws;
    this.sentLeds = leds;
    this.port.postMessage(msg, msg.fb ? [msg.fb.buffer] : []);
  }

  process(inputs, outputs) {
    const ex = this.ex, out = outputs[0];
    if (!ex || !out.length) return true;
    const n = out[0].length;                           // 128 by the spec: one half
    const pcm = new Int16Array(ex.memory.buffer, ex.web_half(), n * 2);
    for (let i = 0; i < n; i++) {
      out[0][i] = pcm[2 * i] / 32768;
      if (out[1]) out[1][i] = pcm[2 * i + 1] / 32768;
    }
    this.sinceFrame += n;
    if (this.sinceFrame >= sampleRate / 30) {
      this.sinceFrame = 0;
      this.publish();
    }
    return true;
  }
}
registerProcessor("moondaw", Moondaw);
