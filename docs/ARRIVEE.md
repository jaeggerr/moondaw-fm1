# The day the FM-1 arrives

The order of operations, written before having the device. Nothing in this
repository has ever run on a real FM-1: the firmware compiles, links, passes
Felucca's checks, and that is all we know.

Two rules above everything else:

- **Nothing gets flashed before there is a backup of the stock flash,
  verified, in two places.**
- **Nothing gets flashed before we have proved we know how to go back.**

## 0. To prepare beforehand (without the device)

Done on 10/10/2026, on this Mac:

- [x] The [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) firmware
      **built**: `~/Documents/Code/FM-1-transporter/build/fm1_transporter.uf2`
      (commit `a632d92`, with `wl82loader.bin` embedded).
- [x] `pyserial` installed, `tools/fm1t.py` starts.

What was installed for this, outside any repository:

| What | Where |
|---|---|
| pico-sdk 2.2.0 | `~/pico-sdk` |
| GCC ARM 13.3 (Arm's archive, no system install) | `~/.fm1/arm/` |
| jl-uboot-tool, for `wl82loader.bin` (24 064 bytes) | `~/.fm1/jl-uboot-tool/` |

To rebuild:

```bash
cd ~/Documents/Code/FM-1-transporter && ninja -C build
```

Still to do:

- [ ] A **Seeed XIAO RP2040** (not another board: the other RP2040s are
      untested, the Waveshare RP2350-USB-A does not work).
- [ ] **Load the firmware onto the XIAO**: plug it in while holding its BOOT
      button, it shows up as an `RPI-RP2` disk; copy
      `fm1_transporter.uf2` onto it. It restarts on its own.
- [ ] A sacrificed USB cable to reach the FM-1's D+, D− and ground.
- [x] `pip3 install --user mido python-rtmidi` (done on 28/10).
- [x] The **official V15** file, stored in `~/.fm1/stock/FM-1_015.fwsc`:
      699 956 bytes, the hash `fm1_install.py` expects, identity
      `FM-1_015`. Taken from M-VAVE's repository
      (`m-vave/fm1_open`, `sdk/firmware/FM-1_015.fwsc`): since 21/10,
      the `FM-1.fwsc` on m-vave.com/download is **V16**, not V15.

Wiring, three wires and not one more:

| XIAO RP2040 | FM-1 |
|---|---|
| D6 (GP0) | D+ |
| D7 (GP1) | D− |
| GND | GND |

**Never connect VBUS (the 5 V).** The FM-1 runs on its battery.

## 1. The device as it comes out of the box

- [x] Received on 28/10, on **V14**. The stock firmware does not show its
      version at startup (it starts instantly); it is read over
      USB: `python3 tools/fm1_install.py --info` (identity request only,
      writes nothing). `FM-1_014` = V14, `FM-1_015` = V15.
- [x] Moved to **V15** with M-VAVE's Workbench
      (open.m-vave.com/fm1-workbench, Chrome): the "back to factory V15"
      button, V15 built into the page. Trap: the default button
      installs **FM1 Open** (identity `FM-1_8000014`, port `FM1 Community`),
      not V15. `--info` now answers `FM-1_015`.
- [x] V15's MIDI port: `USB Composite Device`. Felucca's handshake
      answers on it the first time.
- [ ] **Measure the screen diagonal** (or its visible width, with
      calipers). It is the missing figure for setting the height of a line.
- [x] The layout matches `proto/pattern.html`: four knobs to the
      left of the screen, four to the right, two rows of six buttons, OCT−
      and OCT+ under the left knobs.

Since 20/10, M-VAVE also publishes **FM1 Open** (a GPLv3 application,
engine as a binary SDK) and a Workbench that installs and goes back to V15.
Nothing there says how to recover a device whose firmware crashes before
USB: the XIAO remains the only way out in that case.

## 2. The backup

**Done on 28/10 without a XIAO**, with `fm1_rescue.py` from felucca-bsp
(`tools/`, commit `5942e68`), which talks to the chip's loader
(`WL80UBOOT1.00`, 4C4A:8057) directly from the Mac:

1. `sudo ~/fm1-rescue/env/bin/python ~/fm1-rescue/fm1_rescue.py
   ~/fm1-rescue/FM-1.fwsc --loader ~/fm1-rescue/wl82loader.bin`
   (without `--write`: reads, backs up, compares, writes nothing);
2. while it waits, V15 receives `F0 22 24 35 7D F7` over USB-MIDI
   and drops into the loader; found in 8 s;
3. chip 980F, flash 856014, 1 MB read in 2.6 s, **0 sectors out of 147
   differ from V15**; the FM-1 restarts on its own on V15.

- [x] `fm1-backup-20261028-181751.bin`, 1 048 576 bytes, sha256
      `d562b0e8…534f62`, in `~/fm1-rescue` and `~/.fm1/backups`.
- [ ] A copy off the machine (external disk or cloud).

This is also the rescue path without a XIAO: a firmware that crashes at
startup ends up in this loader (boot guard), and the same script
with `--write` puts V15 back.

With the XIAO, the original plan:

```bash
python3 tools/fm1t.py info
```
```bash
python3 tools/fm1t.py dump backup-1.bin
```
```bash
python3 tools/fm1t.py dump backup-2.bin --compare backup-1.bin
```
```bash
shasum -a 256 backup-1.bin backup-2.bin
```

(to run from `~/Documents/Code/FM-1-transporter`)

- [ ] The two hashes are identical, the files are 1 048 576 bytes.
- [ ] A copy off the machine (external disk or cloud).

Worth knowing: the transporter reads the whole flash but **only writes 0x4000
to 0x93000**, the firmware area. So it does not restore the data area
(0x97000 and beyond), and does not need to: we never write to the
stock firmware's backup (0xE8000 and beyond).

## 3. Prove the way back before the first way there

```bash
python3 tools/fm1t.py write --package FM-1.fwsc --ref backup-1.bin
```

Without `--write`, this command **checks and writes nothing**. It must pass.

The transporter is a tool for **going back**, not for installing: `write`
only accepts the official V15 package (or a package explicitly validated by
its author) and refuses the rest. So MOONDAW will never be installed through it,
but through the normal USB update (`tools/fm1_install.py`, Felucca's
one).

## 4. Felucca first, not MOONDAW

My recommendation: the first alternative firmware to install is
**Felucca**, through its web installer, not ours. It is a firmware known
to work. If it installs and we know how to go back to V15 (the
"Return to official V15" button of the installer, or M-VAVE), we have proved the
install path and the way back with something other than a firmware
that has never run.

- [x] Felucca 1.7.0 installed on 28/10 through its web installer, direct
      cable, Chrome: identity `FM-1_917`, MIDI port `Felucca`. Sound,
      screen and controls respond.
- [x] Back to the official V15: `FM-1_015`, port `USB Composite Device`.

Felucca has not been actively developed since 16/10 ("occasional"
maintenance); its hardware layer lives in felucca-bsp, on
which Felucca 1.7 and most alternative firmwares are built.
MOONDAW carries a hand-made copy of Felucca's drivers from 10/10: moving
to felucca-bsp is to be decided after MOONDAW's first boot.

## 5. The test firmware (`MOONDAW_APP=hello`)

As small as possible: text, every control read and shown,
a square wave under the keys. 18 KB.

```bash
MOONDAW_APP=hello ./build.sh
```

Installed on 28/10 with `tools/fm1_install.py build/moondaw.fwsc` (image
18 484 B): write, restart, identity `FM-1_900` accepted, MIDI port
`Felucca` (the USB strings of usb.c).

- [x] The screen lights up and the text is the right way up, the colours right
      (otherwise: byte order or inversion, `lcd.c`).
- [x] Each **key** lights its cell, its LED, and plays its note, from F3 to
      G5 from left to right.
- [x] Each **button** lights the name printed on it:
      `PANEL_DEFAULT` holds for this unit.
- [x] Each **knob** counts in the right direction, and the right counter moves.
- [x] MASTER goes from 0 to about 1023 and sets the volume.
- [x] **CPU and MAX**: 0 and 0. The counter is in whole percent
      rounded down over a half of 2.9 ms: under 12 µs on
      average, under 29 µs at worst. Below its resolution, then; it
      does not yet prove that TIMER4 counts. MOONDAW must show a
      clearly non-zero figure, otherwise it is the measurement that is wrong.
- [x] **OCT− and OCT+ held 5 seconds**: the screen goes dark (the
      UBOOT banner is not visible) and the Mac sees `WL80UBOOT1.00` (4C4A:8057).
      The exit door works; this is where `fm1_rescue` takes over.
- [x] An update on top goes through: MOONDAW installed over `hello`.

## 6. MOONDAW

```bash
./build.sh
```

Installed on 28/10 over `hello` (image 102 172 B). `--info` does not tell it
apart from `hello` (both `FM-1_900`); `fm1_send.py --ping` does:
`link version 1, 290 KB free`, on the `Felucca` port.

- [x] The demo plays, cleanly. **CPU 0, MAX between 0 and 7 %**: the
      counter works (MAX is not zero), and the demo takes under 1 %
      on average, 200 µs at worst out of a half of 2.9 ms. The margin is
      huge; the planned fallbacks are not needed.
- [x] EDIT opens PATTERN: **unreadable**. "Horrible, too small";
      the interface has to be rethought from A to Z (NOTES.md, 28/10).
- [ ] Write a note, SAVE, power off, power on: the note is there. Put
      off for later: navigating is too painful as it stands.
- [ ] During SAVE the sound stops (expected: a sector erase blocks
      interrupts for around fifty milliseconds).

If the CPU does not keep up, the fallbacks are in `NOTES.md`: fewer voices per
track, the filter at half rate, a cheaper kernel for the WAVE engine,
fewer bands redrawn per frame.

## 7. The songs

Once MOONDAW is installed and the device is on its MENU screen, connected over USB:

```
tools/fm1_send.py --ping
tools/fm1_send.py ../moondaw/songs/KIDS.trk
```

`--ping` must answer with the free space (290 KB on an empty flash;
the 13 songs take 100, the biggest 25). Sending first reduces
the song to what the FM-1 carries, then writes it into the device's
flash, which shows RECEIVING. Nothing there is replaced without `--replace`.

The other way, to back up what was written on the device:

```
tools/fm1_send.py --list
tools/fm1_send.py --get KIDS -o KIDS-fm1.trk
```

**Done on 28/10**: the 13 songs sent in one go, 131 KB, 159 KB
left free. The device only accepts on the home screen ("busy" otherwise,
without writing anything). Names are upper-cased and cut to 12
characters (`PIANO-VELOCI`).

If no MIDI port is found, the tool lists the ones it sees:
`--port` with part of the right name. **This MIDI path has never run**:
everything else in the transfer is checked without a device, but the port name
and the real throughput are to be discovered on the day.

## What we do not know and will know on the day

- The processor's frequency, so what eight tracks really cost.
- The size of the screen, so whether eleven steps per screen are readable.
- Whether Felucca's button table holds for this unit.
- Whether sending a song by SysEx goes at the expected rate (V15's
  port is called `USB Composite Device`; MOONDAW's remains to be seen).
- Whether our package's `FM-1_900` identity is accepted as it is by
  the installer (it is the one of Felucca's development builds).
