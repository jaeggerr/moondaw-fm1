// SPDX-License-Identifier: GPL-3.0-only
//
// A WAV file around the PCM the engine just gave us. The engine's output is
// already what a WAV holds -- interleaved 16-bit stereo, little-endian -- so
// this is a 44-byte header and the samples underneath, with no conversion
// and no resampling anywhere between the device's arithmetic and the file.

export const WAV_HEAD = 44;

// pcm: Int16Array, interleaved L,R. -> Uint8Array, a whole .wav
export function wav(pcm, rate) {
  const chans = 2, bits = 16;
  const bytes = pcm.length * 2;
  const out = new Uint8Array(WAV_HEAD + bytes);
  const d = new DataView(out.buffer);
  const tag = (at, s) => { for (let i = 0; i < s.length; i++) out[at + i] = s.charCodeAt(i); };

  tag(0, "RIFF");
  d.setUint32(4, 36 + bytes, true);        // everything after this field
  tag(8, "WAVEfmt ");
  d.setUint32(16, 16, true);               // the fmt chunk's length
  d.setUint16(20, 1, true);                // PCM, uncompressed
  d.setUint16(22, chans, true);
  d.setUint32(24, rate, true);
  d.setUint32(28, rate * chans * bits / 8, true);
  d.setUint16(32, chans * bits / 8, true);
  d.setUint16(34, bits, true);
  tag(36, "data");
  d.setUint32(40, bytes, true);
  out.set(new Uint8Array(pcm.buffer, pcm.byteOffset, bytes), WAV_HEAD);
  return out;
}

// The other way round, for the test: a .wav this wrote -> {rate, pcm}.
export function unwav(bytes) {
  const d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const n = d.getUint32(40, true);
  return { rate: d.getUint32(24, true), pcm: new Int16Array(bytes.buffer.slice(bytes.byteOffset + WAV_HEAD, bytes.byteOffset + WAV_HEAD + n)) };
}
