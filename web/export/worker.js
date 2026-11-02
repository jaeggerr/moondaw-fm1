// SPDX-License-Identifier: GPL-3.0-only
//
// The render, off the page's thread. A three-minute song is some eight
// thousand calls into the engine; on the main thread the progress bar would
// be the one thing that could not move while it happened.
//
// It owns nothing but the job: the MIDI link, the files and the ZIP stay on
// the page's side. A module worker -- the page starts it with { type:
// "module" } -- so it imports the same modules the page does.
import { job } from "./render.js";

self.onmessage = async ({ data: m }) => {
  if (m.type !== "render") return;
  try {
    const out = await job({
      wasm: m.wasm, trk: m.trk, bank: m.bank, mode: m.mode, songName: m.songName,
      onProgress: (p) => self.postMessage({ type: "progress", ...p }),
    });
    // The WAVs go over as buffers rather than copies: they are the big thing
    // here, a stereo minute being 10 MB.
    self.postMessage({ type: "done", ...out },
      out.files.map((f) => f.data.buffer));
  } catch (e) {
    self.postMessage({ type: "failed", message: String((e && e.message) || e) });
  }
};
