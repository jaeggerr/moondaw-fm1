# Upstream

The hardware plumbing of this project comes from **Felucca**, the open-source
firmware for the M-VAVE FM-1 by Leo Kuroshita (@kurogedelic), Hügelton
Instruments — <https://github.com/hugelton/Felucca>, GPL-3.0-only.

Taken at commit `129a4cf4e98e6ef06a4a89e97b65092247d69412` ("Felucca 1.5",
2026-10-09). This is not a fork: only the files below were taken, none of
Felucca's sequencer, UI, editor, MIDI learn or project storage, and of its
engines only the drum voices (below, "Felucca's DRUM").

| Here | From Felucca | State |
| --- | --- | --- |
| `firmware/hal/` (18 files) | `firmware/hal/` | verbatim but for the three below; `fm1_irq.h`, `fm1_sys.h` and `fm1_vec.S` also carry the second core's additions (felucca-bsp, further down) |
| `firmware/hal/fm1_input.h` | same | `fm1_led_duty[4][]` added: four steady LED levels between the mid level and lit (50, 62.5, 75, 87.5 % of lit), with their frame phase in `FM1__LIT` and `fm1_input_tick` |
| `firmware/hal/fm1_guard.h` | same | the PC guard's RAM-code window runs to `_ra_end` instead of `_rt_end`, so the audio core in `.ram_audio` is inside it |
| `firmware/hal/fm1_uart.h` | same | `fm1_uart1_rx_sync()` added (the build refuses inline asm outside `hal/`); the file was Felucca's and unused until the MIDI jack was read |
| `firmware/crt0.S` | same | verbatim |
| `firmware/app.ld` | same | the memory map recut (28/10): RAMTEXT moved to the top of the SRAM and grown to 0x8000, POOL shortened by as much, `.ram_audio` (the audio core run from RAM) and `.c1_text` (the second core's idle loop) added, `.text` excluding their objects |
| `firmware/loader/` | `firmware/loader/` | verbatim |
| `firmware/src/libc.c`, `lcd.c`, `storage_hw.c`, `ota.c`, `usb.c` | `firmware/src/` | verbatim |
| `firmware/src/panel.c` | `firmware/src/panel.c` | cut after `panel_enc`: the palette, HOLD and LEDS settings are gone |
| `firmware/src/audio.c` | `firmware/src/audio.c` | the ISR and buffers kept; mixer, USB audio tap, scope, click and voice shedding removed; a half is rendered by `app_audio` |
| `firmware/src/ota_hw.c` | `firmware/src/ota_hw.c` | `ota_show` redrawn with the text cells |
| `firmware/src/firmware.c` | `firmware/src/felucca.c`, `main.c`, `master.c` | rewritten around them: same boot order, TIMER5 interrupt, boot guard, UBOOT and update entries |
| `tools/build.py`, `build.sh` | same | generated headers and the minsize pass removed, outputs renamed, one retry under Docker |
| `tools/fm1pkg_make.py`, `lz4blk.py`, `get_toolchain.sh` | `tools/` | verbatim |
| `tools/fm1_install.py` | `tools/` | verbatim but one line: `fm1` added to `PORT_RE`, which did not match the `FM1 …` port names this device shows |
| `web/installer/` | `web/fm1pkg.js`, `fm1ota.js`, `index_pkg.html`, `make_site.py` | adapted to MOONDAW (the page rewritten as `index.html`, `fm1ota.js` step for step as `fm1_install.py`, a song backup in `mdlink.js`); run on a device on 29/10 (Chrome, macOS): Connect, Save songs and Install over MOONDAW. The return to V15, the install from V15, a cable pulled mid-write, Windows, Linux and Edge are still untried |
| `platform/web/` | — | new code; the design (everything in the AudioWorklet, audio as the device clock) follows `web/emu/felucca_web.c` and `worklet.js` |
| `LICENSE`, `LICENSES/`, `docs/FELUCCA-LICENSING.md` | `LICENSE`, `LICENSES/`, `LICENSING.md` | verbatim |

`usb.c` was not on the original list of files to take. It is here because
`ota.c` receives the update through it: without USB there is no way to
replace the firmware short of FM-1-transporter.

Not Felucca's:

- `app/font/font8x8_basic.h` — Daniel Hepper, public domain.
- The kernel table `bl_step` and the routine `blep_add` in `core/synth.c` —
  from **blip_buf 1.1.0** by Shay Green (<http://www.slack.net/~ant/>),
  LGPL-2.1-or-later, rewritten here in 32 bits. Used in this GPL-3.0 work
  as the LGPL allows (section 3).
- `core/` as a whole is a fixed-point rewrite of the author's own tracker,
  MOONDAW (`sequencer/song.c`, `songfile.c`, `player.c`,
  `instruments/chain.c`, `pulse.c`, `noise.c`, `wave.c`), and reads the
  same `.trk` files; its `FORMAT.md` is the specification.

To compare with a later Felucca: `diff -ru Felucca/firmware/hal firmware/hal`,
and the same for each verbatim file. The six HAL files named above, `app.ld`
and `fm1_install.py` will differ, and the three `fm1_cpu1*` files are not
Felucca's at all (felucca-bsp, below); nothing else should.

## Felucca's DRUM (29/10/2026)

MOONDAW's DRUM engine plays Felucca's drum voices, from **Felucca** at commit
`5d9694d0475a74c63c71b9aab4edff408f48cd5f` (2026-10-28), and the few DSP helpers and tables
they use, from **Felucca-Engines** (<https://github.com/hugelton/Felucca-Engines>, MIT) at
commit `d0260074ce100a47675814b7f38d61f11176c926`. Why this one: `docs/drums-demo/`.

| Here | From | State |
| --- | --- | --- |
| `core/drum_voice.inc` | Felucca `firmware/src/drum_voice.c` (GPL-3.0-only) | verbatim, two lines of origin added; `.inc` so that the firmware build does not compile it alone |
| `core/drum.c` | Felucca `firmware/src/eng_drum.c` (GPL-3.0-only) | its GM map, kit names and lane swaps; the rest new: a pool of lanes shared by the tracks, blocks of 32 under renders of any length |
| `core/drum_dsp.h` | Felucca-Engines `src/dsp.c` (MIT) | `mulq16`, `clamp`, `sine_i`, `softclip`, `noise32`, `tsvf_coef_k`, verbatim |
| `core/drum_tables.h`, `tools/gen_drum_tables.c` | Felucca-Engines `tools/gen_engine_tables.py` (MIT) | its SINE, PITCH_OCT / `pitch_inc`, SVF_G and TANH_Q15 at 44.1 kHz, the same numbers, from a C generator |
| `LICENSES/MIT-Felucca-Engines.txt` | Felucca-Engines `LICENSE` | verbatim |

## felucca-bsp (28/10/2026)

The second core, from **felucca-bsp** (<https://github.com/hugelton/felucca-bsp>, MPL-2.0), at commit
`5942e68b89ab9ccd23f2c541d712ca38fe635f11`. Its own credits, kept in the files: Melodee by Kerem Kilic (keremimo),
MPL-2.0 by agreement (felucca-bsp issue 5); Charles Vestal; Leo Kuroshita.

| Here | From felucca-bsp | State |
| --- | --- | --- |
| `firmware/hal/fm1_cpu1.h`, `fm1_cpu1.S`, `fm1_cpu1_stop.h` | `chip/` | verbatim |
| `firmware/hal/fm1_irq.h`, `fm1_vec.S` | `chip/` | the CPU1 fault hook, its resume, `fm1_cnum`, `fm1_uptime_ms`, `FM1_DBG_C0/C1` added |
| `firmware/hal/fm1_sys.h` | `chip/fm1_sys.h` | `fm1_cpu1_stop` before the resets, `fm1_vdc14_raise`, `fm1_sysvdd_raise` added |
| `LICENSES/MPL-2.0.txt` | `LICENSE` | verbatim |
