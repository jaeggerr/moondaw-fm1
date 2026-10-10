// SPDX-License-Identifier: GPL-3.0-only
// The wasm alone, without a browser: boot it, press PLAY, and check the picture, the light and
// that a second of the built-in song is neither silent nor clipped. Then hand it a song as the
// page does, and a file that is not one.
import { readFileSync } from "node:fs";

const wasm = readFileSync(new URL("../../build/web/moondaw.wasm", import.meta.url));
const { instance } = await WebAssembly.instantiate(wasm, {});
const ex = instance.exports;
if (ex._initialize) ex._initialize();
ex.web_flash_erase();
ex.web_boot();

const second = () => {
  let sum = 0, peak = 0, n = 0;
  for (let h = 0; h < Math.round(ex.web_sample_rate() / 128); h++) {
    const pcm = new Int16Array(ex.memory.buffer, ex.web_half(), 256);
    for (let i = 0; i < 128; i++) {
      sum += pcm[2 * i] * pcm[2 * i];
      peak = Math.max(peak, Math.abs(pcm[2 * i]));
      n++;
    }
  }
  return { rms: Math.round(Math.sqrt(sum / n)), peak };
};
const load = (text, name) => {
  const bytes = new TextEncoder().encode(text);
  new Uint8Array(ex.memory.buffer, ex.web_song_buf(), bytes.length).set(bytes);
  new Uint8Array(ex.memory.buffer, ex.web_song_name(), 20).set(new TextEncoder().encode(name + "\0"));
  return ex.web_song_load(bytes.length);
};

ex.web_btns(1 << 10);                                  // PLAY
const demo = second();
const fb = new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240);
const lit = fb.reduce((n, p) => n + (p !== 0), 0);
const led = (ex.web_led_btns() >> 10) & 1;

const refused = load("this is not a song\n", "junk");
const taken = load("version 3\nbpm 140\ntracks 1\ninst 00 engine PULSE timbre 2 adsr 0 0 255 10\n" +
                   "pat t0 00 len 4\n00 A-4 i00\narr 00 steps auto\nt0 00\n", "one");
ex.web_btns(0); second();                              // release PLAY; a new song comes in stopped
ex.web_btns(1 << 10);
const one = second();

const ok = lit > 1000 && led === 1 && demo.rms > 200 && demo.peak < 32767 &&
           refused === 0 && taken === 1 && one.rms > 200;
console.log(`web test: ${lit} lit pixels, PLAY led ${led}, demo rms ${demo.rms} peak ${demo.peak}, ` +
            `junk ${refused ? "TAKEN" : "refused"}, song ${taken ? "loaded" : "REFUSED"} rms ${one.rms}` +
            ` -> ${ok ? "ok" : "FAIL"}`);
process.exit(ok ? 0 : 1);
