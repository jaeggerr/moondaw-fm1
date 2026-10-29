# Getting a record into the FM-1: feasibility

29/10/2026. Research only: no code touched, nothing installed, the device
was not plugged in for this study.

The question: hear, through the FM-1, a track playing on the phone or the
computer, while writing, to transcribe it by ear. This is what REF (the
record, on the song's grid) and AMP (what is plugged in, heard live) do in
the other MOONDAW apps (`../moondaw/docs/ui-ios.md`, rows REF and AMP).

## In short

| path | possible? | effort | what you hear |
|---|---|---|---|
| **1. USB sound card** (the computer or the phone sends audio to the FM-1) | **yes** | medium: ~1 to 2 weeks | any app (Spotify, YouTube, Logic), live, no time limit |
| 2. Bluetooth audio (A2DP) | **no**, in practice | very large, blocked by licences | — |
| 3. Analog input (jack, mic) | **no** | — | there is no input on the board |
| 4. An excerpt sent over the cable and kept in flash | yes | medium | **20 to 60 s** of sound, telephone to AM radio quality |

**Recommendation: path 1, the USB sound card.** The FM-1 would show up on
the computer or the phone as one more audio output. You send the record to
it and the FM-1 mixes it into its output, next to the song. The hardware
allows it (JieLi's SDK has a "USB speaker" class for this chip), Felucca
already does the reverse path (the FM-1 seen as an audio input), and it costs
little RAM and CPU. Path 4 remains a good complement for replaying a section
without a computer. It does not replace path 1.

## 1. The FM-1 as a USB sound card

### What already exists

- **Felucca has a USB audio input** (UAC1, class compliant, no driver):
  `firmware/src/usb.c:15-24`, option `FELUCCA_UAC`. The FM-1's output goes
  to the computer as 16-bit stereo at 44.1 or 48 kHz, on an asynchronous
  isochronous IN endpoint (EP4). It works on macOS 26/27 and iOS/iPadOS
  (comment `usb.c:204-206`). This is the **device → computer** direction,
  the opposite of what we want. In MOONDAW, the option is 0 and the audio
  tap was removed from `audio.c` (`UPSTREAM.md:18`).
- **The controller can do isochronous in both directions.** It is a
  MUSB-type core: each endpoint has a TX half and an RX half, the ISO bit is
  in TXCSR2/RXCSR2, and EP4 already has a receive DMA address register,
  `FM1_USB_EP4_RADR` (`firmware/hal/fm1_usb.h:30-32`). Today it points at
  EP0's buffer and does nothing.
- **JieLi's SDK has a "speaker" class for this family**
  (`~/fw-AC79_AIoT_SDK/apps/common/usb/device/uac.c:54-182`): a "USB
  streaming" input terminal, a "Speaker" output terminal, an **adaptive
  OUT** isochronous endpoint, read by `usb_g_iso_read`. Defaults: 48 kHz,
  16 bits, stereo, EP3 (`usb_std_class_def.h:72-90`). This is the proof that
  the chip's hardware receives audio over USB. We do not take the code (it
  depends on JieLi's RTOS), only the descriptors, which are the standard's.
- **The stock firmware (V15)**: one review describes it as a USB audio
  interface to the computer ([Synth Anatomy](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html)).
  [Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/)
  and the product pages do not mention it. Nothing says it accepts audio
  coming from the computer. **Not verified.** It takes a minute to check:
  V15 plugged in, Audio MIDI Setup on the Mac, see whether "USB Composite
  Device" has outputs.

### What would have to be written

1. **The descriptors** (in `usb.c`): in the audio control interface, an
   input terminal of type USB streaming (0x0101), possibly a volume unit,
   and an output terminal of type speaker (0x0301). The AC header must list
   one more interface. A new streaming interface is also needed: alt 0 with
   no bandwidth, alt 1 in 16-bit stereo PCM at **44.1 kHz only**, with an
   **adaptive isochronous EP4 OUT** endpoint of 180 bytes (45 frames of 4
   bytes) plus a margin. That is about a hundred bytes of tables, built like
   those of the existing input (`usb.c:229-268`).
   - 44.1 kHz only: macOS and iOS resample by themselves to an output that
     offers only one rate. This avoids a 48 → 44.1 on the machine (Felucca
     has one for the other direction, `uac_fir.h`, which could be reused if
     needed).
   - In adaptive mode there is no feedback endpoint: the device follows the
     host's rate. This is what the SDK does. Asynchronous mode with feedback
     would need one more IN endpoint, and EP1 to EP4 are already all
     assigned (`usb.c:200-201`).
2. **Receiving**: `usb_poll` already runs at 2 kHz in the TIMER5 ISR
   (`firmware.c:246-276`). Every USB millisecond, it must drain the packet
   received on EP4 (size in RXCOUNT, `ssync`, `fm1_usb_rx_sync`), copy it
   into a ring and rearm. One packet per millisecond and a poll every 0.5
   ms: it holds, as for Felucca's input.
3. **Clock drift**: this is the real difficulty. The FM-1's I2S runs at
   ~44,117.6 Hz and the host sends 44,100 frames per second (`usb.c:67`,
   `core/synth.h:15`). The gap is **+400 ppm**: the FM-1 consumes ~17.6
   more frames per second than it receives. Without correction, the ring
   empties in a few seconds, then clicks at every underrun. The correction
   is a small **resampler** (linear or Hermite interpolation) whose ratio
   (~0.9996) is servoed to the ring's fill level by a slow PI loop.
   Duplicating a frame every 57 ms would also work, but would be audible on
   a sustained record.
4. **Mixing**: in `app_audio`, after the song's master and before the move
   to 24 bits (`OUT_SHIFT`, `audio.c:13`), without going through the reverb
   or the delay. A level, and the DISQUE / MOI / DEUX cycle (record / me /
   both) of the iOS RÉF button. Incoming audio never goes into an export or
   a recording.
5. **The interface**: a row or a screen in the manner of AMP (level, and a
   meter to answer the question "is it arriving"). To be designed when the
   time comes, one screen at a time.

### What it costs

- **RAM**: a ring of 1,024 16-bit stereo frames (23 ms), 4 KiB, plus a
  184-byte packet buffer. 8.6 KiB remain in .data+.bss and ~65 KiB in the
  pool (`docs/sampler-faisabilite.md`, § 1). It fits.
- **CPU**: copying 45 frames per millisecond costs nothing. The stereo
  resampler costs ~10 to 20 instructions per frame, so **~30 to 60 µs per
  2,902 µs block (1 to 2 %)**, on a machine that peaks at MAX 57 % on
  DPEN-CHORUS (NOTES 29/10).
- **Flash**: a few KiB of code.
- **Time**: descriptors and receiving in 2-3 days, the drift servo and tests
  in 2-3 days, the screen and trials on Mac and iPhone in a few days. So
  **one to two weeks**.

### The risks

- **`usb.c` is a verbatim copy** of Felucca (`UPSTREAM.md:16`). Adding an
  audio output to it makes it a file of ours. To be decided together with
  the move to felucca-bsp (`docs/ARRIVEE.md`, end of § 4).
- **Interrupts disabled**: erasing a flash sector blocks for ~50 ms (NOTES,
  "reverb, delay, flash store"). During SAVE, the record skips. The song
  already does, so it is acceptable.
- **macOS 13-15 and the serial port (CDC)**: on these versions, Apple's CDC
  driver prevents the audio driver from taking the interfaces of a composite
  device that has a CDC (`usb.c:204-206`). It must be tested with
  `usb_cdc_on = 0`, or on macOS 26+, which Jaeger has.
- **Windows** is not tested (Felucca says nothing about the OUT direction).
- **The phone**: an iPhone on USB-C (or Lightning with the camera adapter)
  sends all its audio to a compliant USB output, with no setting. It also
  powers the FM-1 over VBUS, so it charges it and drains the phone's battery
  faster. MIDI (and so MOONDAW's link) goes over the same cable.
- **On the Mac**, the FM-1 must be chosen as the output (or a "Multi-Output
  Device" made): the record no longer comes out of the Mac's speakers.

## 2. Bluetooth

### The hardware: yes, there is a radio and an antenna

- The AC791N ("WL82" in the SDK) has 802.11b/g/n Wi-Fi, classic Bluetooth
  and BLE ([AC791N/AC792N comparison](https://gitcode.csdn.net/69e9857554b52172bc6bb671.html),
  secondary source, no official datasheet found).
- The stock FM-1 does **Bluetooth MIDI (BLE MIDI)**, usable at the same
  time as USB and TRS ([Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/),
  [Sonicstate](https://sonicstate.com/news/2026/07/10/m-vave-fm-1-reviews-/)).
  Synth Anatomy writes "Bluetooth MIDI and audio", but no product page
  mentions Bluetooth audio: to be taken as unconfirmed.
- The alternative firmware **fm1-dock** got BLE MIDI working on a real
  FM-1 on 25/10: it transmits at -45 dBm in the office and a Mac connects to
  it without pairing ([fm1-dock, PLAN.md "BLE-MIDI"](https://github.com/joakimeriksson/fm1-dock)).
  So the antenna is there and the radio can be driven outside the stock
  firmware.
- Felucca, and so MOONDAW, never turn the radio on
  (`docs/FELUCCA-LICENSING.md`, "Radio").

### What can be done without JieLi: BLE, not audio

- fm1-dock uses the **Optimist** stack (w0ts/optimist, GPL-3.0), written
  from the Bluetooth specifications: link layer, host, ATT, BLE-MIDI and a
  driver for the WL82 radio block, with no vendor code. It weighs **31 KiB
  of flash and 8 KiB of RAM**. The radio's startup tables were captured on
  V15, and each device's factory settings are read from V15's VM area, at
  0xE8000 (`fm1-dock/firmware/src/ble/README.md`, PLAN.md). MOONDAW never
  touches that area (NOTES, "Rules"), which preserves it.
- **BLE does not carry audio a phone knows how to send.** BLE audio (LE
  Audio, LC3) needs Bluetooth 5.2 isochronous channels, and the chip is
  announced as BLE 5.0. A custom stream over GATT would need an app of ours
  on both sides: we would lose the point of "any app on the phone".

### A2DP (the FM-1 as a Bluetooth speaker): no

- A2DP needs **classic Bluetooth** (BR/EDR) with L2CAP, SDP, AVDTP, secure
  pairing and an SBC decoder. The only stack available for this chip is
  JieLi's: `btctrler.a` (5.2 MB archive) and `btstack.a` (3.1 MB) in
  `~/fw-AC79_AIoT_SDK/cpu/wl82/liba/`, with A2DP in
  `include_lib/btstack/a2dp_media_codec.h`.
- This stack is **closed** (bitcode), built around JieLi's RTOS, and weighs
  120 to 200 KiB once linked (fm1-dock, PLAN.md). A GPL-3.0 firmware like
  MOONDAW, derived from Felucca, cannot link it. MOONDAW would also have to
  be ported to the SDK's RTOS.
- Writing a classic Bluetooth stack and a BR/EDR driver from scratch, as
  Optimist did for BLE, is a matter of months, with no guarantee at all: the
  classic radio block is not documented.
- **Conclusion: not realistic.** BLE MIDI, on the other hand, is within reach
  (take Optimist, GPL-3.0 like us), but it does not answer the question.

## 3. An analog input

**There is none.**

- The FM-1's jacks: a 3.5 mm stereo output (the mono signal copied to both
  sides), a 3.5 mm TRS MIDI input, USB-C and the speaker. No line input, no
  mic ([Synth Anatomy](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html),
  [Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/)).
- `fm1_audio.h` only does output: I2S ALNK0 to an external codec, driven by
  PC0/1/2/6. Nothing is read in the other direction.
- The SARADC (`firmware/hal/fm1_adc.h`) reads the MASTER knob (PB6) and the
  battery (PB1), at 10 bits, by polling. Even at 44 kHz, it would be 10 bits
  with no anti-aliasing filter, and no free pin is wired to a jack.
- The AC791N may have internal mic inputs, but nothing indicates they are
  connected on this board. It would take soldering, and that is not the
  point.

## 4. An excerpt kept in flash (nothing new on the hardware side)

The idea: the Mac app cuts out the part of the record being transcribed (a
verse, a chorus), compresses it and sends it over the USB-MIDI link that
already sends songs (`app/link.h`). The FM-1 stores it in flash and plays it
next to the song, like REF on iOS.

### The space

The flash is 1 MiB and almost all of it is taken
(`docs/sampler-faisabilite.md`, § 1):

- **the store**: 273 KiB free out of 292, shared with the songs;
- the "USR" area (0xEA000–0xFC000, 72 KiB): declared but unused, not
  allowed by `FL_STORE_OK` today, to be checked before use;
- the rest of the firmware slot (~396 KiB): it is only written by an
  update, and it is the room the code needs to grow. To be excluded.

Reserving **256 KiB** of the store for a single excerpt, mono:

| format | rate | length in 256 KiB | quality |
|---|---|---|---|
| 16 bits, 44.1 kHz | 86 KiB/s | 3 s | CD |
| IMA ADPCM 4 bits, 22 kHz | 10.8 KiB/s | **24 s** | FM radio, highs a little grainy |
| IMA ADPCM 4 bits, 16 kHz | 7.8 KiB/s | **33 s** | band up to 8 kHz: everything is heard but the air of the cymbals |
| IMA ADPCM 4 bits, 11 kHz | 5.4 KiB/s | **47 s** | AM radio: bass, voice and chords clear |
| IMA ADPCM 4 bits, 8 kHz | 3.9 KiB/s | **66 s** | telephone: melody and bass stay readable |

For transcribing notes, **16 kHz ADPCM** is the right compromise: ~30 s, so
16 bars at 120 bpm. An MP3 or an Opus would roughly double the length, but
the decoder weighs several tens of KiB of code and is expensive on a core
doing ~51 simple instructions per µs (NOTES 28/10). It is not worth it.

### The cost

- **CPU**: an ADPCM decoder and fixed-step playback, ~25 µs per block (same
  calculation as the sample voice in `docs/sampler-faisabilite.md`, § 4).
  Read in XIP from the plain window (`fl_plain_window_init`), with no copy
  to RAM.
- **The transfer**: 256 KiB over SysEx, 8 bits in 7, stop-and-wait. The
  throughput has never been measured; expect **from a few seconds to a
  minute**, music stopped. There are also 64 sectors to erase, at ~50 ms
  each, so ~3 s.
- **Wear**: each new excerpt rewrites 64 sectors. At ~100,000 cycles per
  sector, this is not a problem for studio use.
- **Room for songs**: a 256 KiB excerpt leaves ~17 KiB for songs, less than
  one full song (25 KiB). So either the excerpt is cut to ~128 KiB (15 s at
  16 kHz), or the USR area is freed after checking.
- **The code**: this path is shared with the sampler (the same ADPCM player,
  the same file type in the store, the same conversion on the Mac side). If
  the sampler gets done, the excerpt costs little more.

### What it gives

It works without a computer once the excerpt is loaded, and the excerpt
stays locked to the song's grid. On the other hand, you only have a piece of
the record, at reduced quality, and you have to go back through the Mac for
each section. For "transcribing a whole track by ear", path 1 is much
better.

### In between: a live stream over the MIDI link

The record could also be sent **continuously** over the existing SysEx,
with no new USB descriptor: 16 kHz ADPCM is 8 KiB/s, and a 64-byte USB-MIDI
packet carries ~42 useful bytes. But it would need a program of ours that
reads the file and sends it, which does not work from Spotify or the phone.
The link protocol (stop-and-wait) is not made for this, and the clock drift
would still have to be handled. It takes the difficulty of path 1 without
its advantage. To be set aside.

## Recommendation

1. **Build the USB sound card (path 1).** It is the only path that gives "I
   start the record on my phone or my Mac and I hear it in the FM-1", with
   no time limit and with any app. The hardware allows it, RAM and CPU are
   enough, and the method is known: the speaker class of JieLi's SDK,
   Felucca's UAC input, the drift servo that is needed anyway.
2. **First step, a measurement**: a test build that only announces the
   audio output and plays the ring as is, with no resampler. Check that
   macOS 26 and the iPhone see it, that EP4 OUT does receive the packets,
   and read on the screen how fast the ring drifts (~17.6 frames/s
   expected). Then add the servo, then the level and the DISQUE / MOI /
   DEUX button.
3. **The excerpt in flash (path 4)**: only if the sampler gets done, as one
   more use of its ADPCM player. Not for its own sake.
4. **Bluetooth**: do nothing for audio. Optimist's BLE MIDI is another
   question, to be raised separately if it is of interest.

## Sources

- Local: `firmware/src/usb.c` (l.3-24, 66-130, 198-268), `firmware/hal/fm1_usb.h`,
  `firmware/hal/fm1_audio.h`, `firmware/hal/fm1_adc.h`, `firmware/src/audio.c`,
  `firmware/src/firmware.c:246-276`, `core/synth.h:15`, `NOTES.md`, `UPSTREAM.md`,
  `docs/FELUCCA-LICENSING.md`, `docs/ARRIVEE.md`, `docs/sampler-faisabilite.md`,
  `../moondaw/docs/ui-ios.md` (REF, AMP).
- JieLi AC79 SDK (`~/fw-AC79_AIoT_SDK`): `apps/common/usb/device/uac.c`,
  `apps/common/usb/usb_std_class_def.h`, `cpu/wl82/liba/btctrler.a`, `btstack.a`,
  `include_lib/btstack/a2dp_media_codec.h`.
- fm1-dock: [github.com/joakimeriksson/fm1-dock](https://github.com/joakimeriksson/fm1-dock)
  (PLAN.md "BLE-MIDI", `firmware/src/ble/README.md`), and its
  [version 0.6](https://newreleases.io/project/github/joakimeriksson/fm1-dock/release/v0.6);
  the [Optimist](https://github.com/w0ts/optimist) stack.
- FM-1 product pages and reviews: [Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/),
  [Synth Anatomy](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html),
  [Sonicstate](https://sonicstate.com/news/2026/07/10/m-vave-fm-1-reviews-/),
  [M-VAVE opens the firmware (Synthtopia)](https://synthtopia.com/content/2026/10/20/m-vave-officially-opens-fm-1-firmware-to-developers-with-new-workbench-sdk).
- AC791N: [AC791N / AC792N comparison](https://gitcode.csdn.net/69e9857554b52172bc6bb671.html)
  (secondary; no official datasheet found).
