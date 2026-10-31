# Licensing

MOONDAW is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`). The full text is in `LICENSE`.

Copyright (C) 2026 Jaeger

That covers every file in this tree carrying an `SPDX-License-Identifier:
GPL-3.0-only` header and no other attribution: the tracker (`core/`), the
machine's screens and logic (`app/`), the platform layers (`platform/`), the
tools (`tools/`), the web pages (`web/`), the tests (`tests/`) and the firmware
plumbing that was written here rather than taken. Where a file came from
somewhere else its own header says so, and `UPSTREAM.md` names every one of
them, file by file, with what was changed.

If you distribute MOONDAW, or firmware derived from it, you must give your
recipients the complete corresponding source under the same licence.

## What came from elsewhere

| What | Where it is here | Licence | Text |
| --- | --- | --- | --- |
| **Felucca** by Leo Kuroshita (@kurogedelic), Hügelton Instruments: the hardware abstraction, drivers, update loader, build tools, and the DRUM engine's voices | `firmware/hal/`, `firmware/loader/`, parts of `firmware/src/`, `core/drum.c`, `core/drum_voice.inc` | GPL-3.0-only | `LICENSE` |
| **Felucca-Engines**: the DSP helpers and the tables generated from them | `core/drum_dsp.h`, `core/drum_tables.h`, `tools/gen_drum_tables.c` | MIT | `LICENSES/MIT-Felucca-Engines.txt` |
| **felucca-bsp**: the second core | `firmware/hal/fm1_cpu1.h`, `fm1_cpu1.S`, `fm1_cpu1_stop.h`, and the additions to `fm1_irq.h` and `fm1_vec.S` | MPL-2.0 | `LICENSES/MPL-2.0.txt` |
| **blip_buf 1.1.0** by Shay Green: the band-limited step table and `blep_add`, rewritten in 32 bits | `core/synth.c` | LGPL-2.1-or-later | `LICENSES/LGPL-2.1.txt` |
| **font8x8** by Daniel Hepper | `app/font/font8x8_basic.h` | public domain | — |
| **Barlow Condensed** and **JetBrains Mono** | `assets/fonts/`, and the headers `tools/gen_fonts.py` generates from them | SIL OFL 1.1 | `LICENSES/OFL-BarlowCondensed.txt`, `LICENSES/OFL-JetBrainsMono.txt` |
| **JieLi AC79 SDK**: three files of the firmware package (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) | not in this repository: `tools/fm1pkg_make.py` takes them from the SDK at build time, and they travel inside `moondaw.fwsc` | Apache-2.0 | `LICENSES/Apache-2.0.txt`, shipped beside every package |

## Texts in `LICENSES/` that this tree does not use

`LICENSES/` came over from Felucca whole, so it still carries licences for parts
MOONDAW does not ship: `MIT-DaisySP.txt` and `MIT-Rings.txt` (Felucca's
`phys_dsp.c` and `phys_symp.c`), `Apache-2.0-msfa.txt` (its `fm6_core.c`),
`MIT-Fukiai.txt` (its icon font), `OFL-DotGothic16.txt` and `OFL-InterTight.txt`
(the fonts of its web pages). None of that code and none of those fonts is in
this repository. They are kept so that `docs/FELUCCA-LICENSING.md` — Felucca's
own licensing document, carried here as it stands — still points at something.

## The name

"MOONDAW" is this project's name and belongs to its author; the GPL grants no
rights to it. Forks are welcome, under another name. "M-VAVE" and "FM-1" are
trademarks of their respective owners, and MOONDAW is independent of them.
