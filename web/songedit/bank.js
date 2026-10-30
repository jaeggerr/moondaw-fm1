// SPDX-License-Identifier: GPL-3.0-only
//
// The sample bank, as the editor needs to know it: which sounds the FM-1
// holds, and which of a song's names reach one. A name the bank has not got
// is silent, never an error (core/bank.h) -- so the page has to say it, since
// nothing else will.
//
// Two ways in: a packed bank file from the computer (tools/fm1_samples.py
// pack), and the index read over the link from the device itself
// (mdlink.js bankIndex). Both end as the same {name, frames} list.

export const BANK_MAX = 128;
export const BANK_NAME_LEN = 24; // 23 + NUL
export const BANK_ENTRY = BANK_NAME_LEN + 8;
export const BANK_HEAD = 8;
export const SMP_RATE = 22050;

const lower = (c) => (c >= "A" && c <= "Z" ? c.toLowerCase() : c);

// core/bank.c bank_find, rule for rule: a song names a sample by its file,
// the bank answers to the last part of it without .wav, whatever the case.
// -> the entry's index in `samples`, or -1. audition.c runs the same test in
// C and test/roundtrip.mjs holds the two against each other.
export function bankFind(samples, songName) {
  let key = songName;
  for (let i = 0; i < songName.length; i++)
    if (songName[i] === "/" || songName[i] === "\\") key = songName.slice(i + 1);
  let len = key.length;
  if (len > 4 && key[len - 4] === "." && lower(key[len - 3]) === "w" &&
      lower(key[len - 2]) === "a" && lower(key[len - 1]) === "v") len -= 4;
  if (len === 0 || len >= BANK_NAME_LEN) return -1;
  const want = key.slice(0, len).toLowerCase();
  for (let i = 0; i < samples.length; i++)
    if (samples[i].name.toLowerCase() === want) return i;
  return -1;
}

// A packed bank file -> {size, samples}. Only the index is read; the blocks
// are the device's business. Throws when it is not one.
export function bankFromFile(bytes) {
  if (bytes.length < BANK_HEAD || bytes[0] !== 0x53 || bytes[1] !== 0x4D ||
      bytes[2] !== 0x50 || bytes[3] !== 0x31)
    throw new Error("not a packed bank: it does not begin with SMP1 (tools/fm1_samples.py pack writes one)");
  const n = bytes[4];
  if (n > BANK_MAX || BANK_HEAD + n * BANK_ENTRY > bytes.length)
    throw new Error(`the index says ${n} samples, which does not fit in ${bytes.length} bytes`);
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const samples = [];
  for (let i = 0; i < n; i++) {
    const e = BANK_HEAD + i * BANK_ENTRY;
    let k = 0;
    while (k < BANK_NAME_LEN && bytes[e + k]) k++;
    samples.push({
      name: String.fromCharCode(...bytes.subarray(e, e + k)),
      frames: dv.getUint32(e + BANK_NAME_LEN, true),
    });
  }
  return { size: bytes.length, samples, bytes };
}

export const bankSeconds = (frames) => frames / SMP_RATE;
