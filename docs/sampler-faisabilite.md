# A sampler in MOONDAW on the FM-1: feasibility

29/10/2026. Research only: no code touched, nothing installed. The device was
queried once, read-only (`fm1_ctl.py status`).

## In short

- **Flash**: a 1 MiB chip. There are **~396 KiB** free in the firmware
  slot and **273 KiB** free in the song store (measured on the
  device). The rest of the chip is taken by Felucca, the OTA and the boot.
- **RAM**: 512 KiB in all, almost all of it taken. There are **~65 KiB** left in
  the pool, 8.6 KiB in .data+.bss, and 32 KiB never used at the bottom of the
  SRAM. That is about **1 s of 16-bit sound**: too little to keep the
  samples in RAM.
- **But playback can be done directly from flash**: the store area is
  already mapped as plain XIP at boot. A sample is read there like an
  array, with no copy into RAM.
- **What fits**: in IMA ADPCM (4:1), about **13 s in the store** and
  **18 s in the firmware**, so roughly **100 drum hits of
  0.3 s**, or fifteen or so notes of 2 s. In raw 16-bit, it is four
  times less.
- **No audio input** on the FM-1: samples come from the computer,
  over the cable.
- **Recommendation**: yes, but only in a **reduced form**. One-shots
  (drums, percussion, short hits) in ADPCM, read from
  flash, chosen by `fSMP`, with no multisampling and no recording on
  the machine. No pianos and no multisampled instruments.

## 1. What is free today

### Flash

The chip answers JEDEC ID `0x856014`, so 2^0x14 = **1 MiB**
(`firmware/src/firmware.c:332`). Here is its map, taken from
`firmware/hal/fm1_flash.h:38-47` and `tools/fm1pkg_make.py`:

| area | addresses | size | use |
|---|---|---|---|
| header | 0x00000–0x04000 | 16 KiB | SPL, JLFS entries |
| app area | 0x04000–0x93000 | 588 KiB | app slot `APP_SLOT` = 0x8DFBC = **581 564 B**, then cfg_tool |
| (VM start) | 0x93000–0x97000 | 16 KiB | not used by MOONDAW, not checked |
| **store** | 0x97000–0xE0000 | **292 KiB** | 73 sectors of 4 KiB (`PLAT_FLASH_SECTORS`) |
| OTA | 0xE0000–0xE5000 | 20 KiB | staging for the update loader |
| Felucca | 0xE5000–0xE8000 | 12 KiB | autosave, FM6 bank |
| VM backup, BTIF | 0xE8000–0xEA000 | 8 KiB | SDK, never written |
| "USR" | 0xEA000–0xFC000 | 72 KiB | declared by the package, unused here (to check before using it) |
| globals | 0xFC000–0xFF000 | 12 KiB | |
| key_mac | 0xFF000–0x100000 | 4 KiB | |

- **The firmware** weighs 175 912 B (`build/build.log`). In the
  581 564 B slot, that leaves **405 652 B, ~396 KiB**. Factory samples
  can be compiled into it, read over XIP like any other constant. These bytes
  are also the ones the firmware will need to grow.
- **The store** holds 32 song slots (`core/store.h`). A
  song is at most 25 KiB (NOTES, "Songs from the computer").
  The device answers `free=273`: **273 KiB free** out of 292, with the
  songs that are in it today. Samples stored there would take the place
  of songs, and the other way round.
- **In reserve, without guarantee**: the "USR" area (72 KiB) and
  0x93000–0x97000 (16 KiB). `FL_STORE_OK` does not allow them today.

### RAM

The SRAM is 512 KiB (0x01C00000–0x01C80000). The layout comes from
`firmware/app.ld`, the figures from `build/build.log`:

- **.data+.bss**: 64 952 B out of 73 728, so **8.6 KiB free**.
- **pool**: 261 016 B out of 335 872, so 73 KiB free. `tools/build.py`
  requires keeping 8 KiB of it, which leaves **~65 KiB usable**. The big
  occupants: `scratch` 128 KiB (the delay lines, which also serve as the
  working copy while loading), `app_song` 79 KiB, the reverb 25 KiB, the
  tracks 14.6 KiB, `vox_pool` 7.8 KiB, and the two `lanes` 7 KiB (the
  second of which is CPU1's).
- **RAMTEXT** (the audio code run from RAM): full to within 1.5 KiB.
- **0x01C00000–0x01C08000**: 32 KiB unused since 28/10 (comment
  in `app.ld`).
- **The second core** has no memory of its own. It takes its `Lane` from the
  pool and its stacks. Its idle loop runs from RAM so that a CPU1
  at rest never reads the flash.

In total, **~97 KiB** could serve as a buffer, but only by cutting into
the margin.

## 2. What a sample costs

The engine runs at `SYNTH_FS` 44 100 Hz (`core/synth.h:15`), 44 117.6 Hz on
the device. Samples are mono.

| format | B/s | ms per KiB |
|---|---|---|
| 16-bit | 88 235 | 11.6 |
| 8-bit or µ-law (2:1) | 44 118 | 23.2 |
| IMA ADPCM (4:1) | 22 059 | 46.4 |

Here is what fits in each area, in hits of 0.3 s and notes of 2 s.
In brackets, the seconds if resampled to 22 kHz:

| where | 16-bit | µ-law | ADPCM |
|---|---|---|---|
| store, 273 KiB | 3.2 s: 10 hits, 1 note | 6.3 s: 21 hits, 3 notes | **12.7 s: 42 hits, 6 notes** (25 s) |
| firmware, 396 KiB | 4.6 s: 15 hits, 2 notes | 9.2 s: 30 hits, 4 notes | **18.4 s: 61 hits, 9 notes** (37 s) |
| RAM, ~97 KiB | 1.1 s: 3 hits | 2.3 s: 7 hits | 4.5 s: 15 hits |

To judge with real kits, those in `../moondaw/instruments`:

- **TR-808**: 12 sounds, 479 KB as 16-bit WAV, 5.4 s. In ADPCM, that is
  **~120 KiB**, and ~60 KiB at 22 kHz.
- **TR-909**: 10 sounds, 495 KB, 5.6 s. In ADPCM, **~124 KiB**.
- **One piano note** (`pf_c4.wav`): 370 KB on its own. A
  multisampled piano is therefore out of reach.

Both drum kits fit together in the firmware in ADPCM
(~245 KiB).

### Read from flash, or from RAM?

**From flash.** At boot, `fl_plain_window_init()`
(`firmware/hal/fm1_flash.h:222`, called in `firmware.c:335`) maps all of
0x93000–0x100000 as **plain XIP**. `FM1_XIP(off)` (`fm1_xip.h`) gives a
direct pointer into it, so the voice reads `ptr[i]` without copying anything. Samples
compiled into the firmware are read the same way: XIP decrypts them
on the fly.

This is not the path the store takes today. `plat_flash_read`
(`storage_hw.c`) turns XIP off and reads over SPI, in 256 B windows
with interrupts off. That suits loading a song, not audio
rendering.

## 3. What the other MOONDAW apps already have

In `../moondaw`:

- **The `smp` table** (FORMAT.md, "Samples"): an index, a WAV file
  name, and optionally `begin`, `loop a b` and `ping`. The audio is never in
  the `.trk`, only its name. The file is looked for in the bundle, then
  in `samples/`, then in `instruments/<folder>/`.
- **Zones**: each SMPL instrument is a key map (`zone 05
  C-2 B-3 root E-2 smp 00`, plus `tune` and `gain`). The first zone that
  contains the note wins.
- **`fSMP`**: a step plays take no. NN, whatever the zones,
  with the root note at C-4. The command only applies to the SMPL engine and
  only lasts one hit (a one-shot). The envelope, filter and level
  stay the instrument's.
- **The engine** (`src/instruments/sample.c`, 348 lines): 16-bit
  playback with a 32.32 fixed-point phase, pitch ratio computed in
  `double`, `exp2`, loop, ping-pong. A pre-attack triggers the note
  early so that `begin` lands exactly on the beat.
- **The pool** (`samplepool.c`, 374 lines): 16-bit mono PCM in RAM, at the
  file's rate, with a 128 MB budget.
- **Takes** (`src/audio/take.c`): recording from the audio input,
  up to 5 min.

**On the FM-1 side today** (`core/song.h:11-15, 92, 98` and `core/trk.c:228,
296`): `FX_SMP` and `ENGINE_SAMPLE` exist but are never stored. The
reader rejects an `engine SMPL` layer, counts the `fSMP` in its report
and drops them, and the `smp`/`zone` are not read. A song that uses
samples therefore loads, but those tracks stay silent. This is the case for the kick
and the rim of DAFT-PUNK-EN (`smp 00 kick/kick_bk.wav`, `smp 14
tr808/tr8_rim.wav`).

**What can be reused**: the format (`smp`, `zone`, `fSMP`, `begin`, `loop`)
and its rules, as they are. **What cannot be reused**: the code. It
relies on floats, `malloc`, WAV files and threads, while
the FM-1 has no floats, no libc and no dynamic allocation. A
fixed-point voice reading over XIP is written like the other engines of
`core/synth.c`, a few hundred lines. The bulk of the work is
elsewhere: storage, transfer and conversion on the computer side.

## 4. How a sample gets in, and what it costs in CPU

### Where it comes in

- **No audio input.** `fm1_audio.h` only does output (I2S to
  the codec). The SARADC only reads the MASTER knob and the battery
  (`fm1_adc.h`). USB audio (`FELUCCA_UAC`) goes from the device to
  the computer, not the other way. **Sampling on the machine
  is not possible.**
- **Over the USB link** (`app/link.h`), which already sends songs: SysEx,
  240 B per message, 8 bits in 7, stop-and-wait, written directly into the
  store and validated by CRC at the end. A sample would take the same path,
  as a new file type. The throughput has never been measured.
  Counting at best one round trip per USB millisecond, expect
  a few seconds for 100 KiB. It is the Mac or web app that would convert the
  WAV to ADPCM before sending.
- **Erasing a sector blocks interrupts for ~50 ms** (NOTES, "reverb,
  delay, flash store"). Transfer and saving would therefore happen
  with the music stopped, like SAVE today.

### What it costs in CPU

- An audio block lasts 2 902 µs. On DPEN-CHORUS, the worst block measured with
  both cores is at **MAX 57 %** (NOTES 29/10): there is margin left.
- The core does ~51 simple instructions per µs (NOTES 28/10). A sample voice
  with linear interpolation would cost ~10 instructions per frame, so **~25 µs
  per block**. ADPCM adds about as much. That is little next to the VOX
  (V = 272 µs for two voices) or the per-track chain (C = 556 µs). The
  filter and envelope already exist and are shared.
- **Main unknown: XIP cache misses.** The UI code runs
  over XIP and the audio code from RAM. A voice reads 256 B per block, so a
  handful of cache lines, but nobody has measured the cost of a miss
  from the audio interrupt.

## Constraints and risks

1. **The second core and the flash.** On some FM-1s, CPU1 misreads shared RAM
   or flash (NOTES 28/10, felucca-bsp). This is why its
   idle loop runs in RAM. A sample voice rendered by CPU1 would read
   the flash in the middle of rendering. We would have to either measure, or keep
   SMPL tracks on CPU0.
2. **The 32x32→64 multiply defect** (NOTES 28/10) also concerns
   interpolation, even though a 16x16 product is enough.
3. **A sample stored in the store is not contiguous**: each 4 KiB
   sector has its header and its CRC. Either the header is skipped at each
   sector during playback, or a contiguous area is reserved for samples.
4. **The firmware and the factory samples share the 396 KiB.** Each
   KiB of kit less is a KiB of code less. The `.fwsc` package and
   the update time grow too.
5. **Names**: in a `.trk`, a sample is a file name
   (`tr808/tr8_rim.wav`). The FM-1 will have to find that name in its table, and a
   missing name will stay silent, which already matches the FORMAT.md rule.
6. **Felucca** stores its own "user samples" in the same area
   (comment on `FL_STORE_OK`). The problem is the same as with
   songs: neither format reads the other.

## Recommendation

**Yes, it is worth it, in a reduced form:**

- **One-shots only**, played by `fSMP` and by simple zones (a
  key range, a root note). No loop, no `ping` and no
  multisampling to begin with.
- **In IMA ADPCM, at 44.1 kHz**, or 22 kHz by choice for low sounds.
- **Read over XIP** from flash, with no buffer in RAM.
- **Two sources**:
  - a factory kit compiled into the firmware, for example TR-808 and TR-909
    (~245 KiB in ADPCM);
  - the user's samples in the store, sent over the USB link and
    converted by the app.
- **What stays out**: sampling on the machine (impossible without an
  input) and multisampled melodic instruments (a piano
  would need more than the whole flash).

What it brings: the Mac songs that already use kits
(DAFT-PUNK-EN) would at last sound complete on the FM-1, and the drums would gain
in realism compared with NOISE and PULSE.

If we go ahead, the first step is a measurement: a test sample compiled
into the firmware and read by a voice on CPU0 then on CPU1, to see
the real cost of XIP in the audio block and make sure CPU1 reads the
flash correctly. That is what will tell whether the reduced form holds.
