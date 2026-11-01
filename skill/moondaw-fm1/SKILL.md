---
name: moondaw-fm1
description: Write, read and edit songs for MOONDAW on the M-VAVE FM-1 in its `.trk` text format, and drive the machine itself over USB. Use this whenever an FM-1 or MOONDAW song is pasted or attached (lines like `version 3`, `bpm 120`, `pat t2 03 len 32`, `inst 00 engine PULSE`, `arr 00 steps auto`), whenever the user asks for a bassline, a drum pattern, a melody, a chord progression, lyrics for the VOX voice or step effects on their FM-1, whenever they ask about the format's fields, engines, kits, zones or FX mnemonics, and whenever they want a song built from scratch for the machine. Use it too for anything done TO the device from a computer: sending or fetching a song, filling the sample bank, reading what the machine is doing, pressing its buttons, turning its knobs, taking a picture of its panel, lighting its LEDs, or drawing on its screen with the canvas. Also use it when they say "the FM-1", "MOONDAW", "my groovebox", "the tracker", "a .trk file", or paste something they pulled off the device — even if they never name the format.
---

# MOONDAW on the FM-1

The M-VAVE FM-1 is a pocket synth — a 240×240 screen, 27 keys, 12
buttons, 7 knobs, 512 KB of RAM and no floating point. MOONDAW is a
firmware that turns it into a tracker: eight tracks, instruments you
build, up to four notes and three named effects a step, and songs that
are plain text.

Two things can be done from a computer, and they are different jobs:

1. **Write the song as a file.** A `.trk` is text; you write it, send it
   over USB, and it lands in the device's flash. → `references/format.md`
2. **Drive the machine as it runs.** Read what it is doing, press its
   buttons, write single steps into the song in memory, photograph its
   panel, draw on its screen. → `references/control.md`

Most requests are the first. Reach for the second when the question is
about the machine's state, when something has to be *seen* on the panel,
or when a change is small enough that one step beats a whole file.

## Not the same machine as the other MOONDAW

There is a sibling skill, `moondaw`, for the MOONDAW that runs in the
browser, on the Mac, on iOS and on a handheld. **It does not describe
this machine**, and following it here produces files the FM-1 refuses or
quietly empties out. The differences that bite:

- **`patch 3` does not exist here.** The sibling's whole exchange is two
  forms, a whole song or a patch that replays onto the live song. This
  machine **refuses a patch outright**. Every file is a whole song, and
  anything you leave out is deleted, not unchanged.
- **Eight tracks, not sixteen.** `t0`–`t7`; `pat t8` and up are dropped.
- **Seven engines, one of them silent.** `DRUM` exists here and not
  there; `PLUCK` exists there and is silent here.
- **`PRB` is this machine's own effect**, 28 commands against 27. A file
  carrying one comes back from the other apps without it.
- **`drive`, `amp`, `tone`, `preview`, `ref`** are read and thrown away.
- Songs travel by **USB into flash slots**, not by clipboard.

## Writing a song

Read `references/format.md` first — the grammar, the limits, and what the
machine drops on the way in. Then:

- `references/step-fx.md` — the 28 step effects, each with its range and
  the machine's own help page. A step is three effects at most, and the
  difference between a **setter** (holds until rewritten, across the
  loop) and a **one-shot** (dies with its step) decides where a cell
  goes.
- `references/engines.md` — the engines and their timbres, the ten drum
  kits with their General MIDI map, and the phonetic alphabet the VOX
  voice sings.

Two habits worth keeping:

**Check the file before claiming it works.** The firmware's own reader
and writer build as host tools and need no device:

```bash
cc -std=c11 -O2 tests/trkdump.c core/song.c core/trk.c -o build/trkdump
build/trkdump song.trk            # what the FM-1 would save, given this file
```

Diff that against what you wrote. Unrecognised lines are **ignored in
silence** by this format, so a misspelt keyword is not an error — it is a
line that vanishes, and the diff is the only place it shows. Values out
of range come back clamped, and fields the machine does not carry are
simply gone.

**Start from a preset when nothing asks otherwise.** Slots `i00`–`i09`
already hold BASS, SUB, LEAD, KEYS, HARP, PAD, ORGAN, BELL, VOICE and
KIT, so a song can play without a single `inst` line.

## Sending it

```bash
python3 tools/fm1_send.py song.trk               # onto the device
python3 tools/fm1_send.py --list                 # what it is carrying
python3 tools/fm1_send.py --get NAME -o song.trk # one back
```

The device must be on HOME. The song is reduced by the same reader
and writer the firmware uses before it goes, so what arrives is exactly
what the dump showed. Nothing on the device is replaced without
`--replace`.

Samples do not travel in a song — a `smp` line is a *name*, and the bank
is filled separately with `tools/fm1_samples.py` (`references/engines.md`,
SMPL).

## Working with the person, not around them

The FM-1 is played with two thumbs, and a song is carved on it a step at
a time. What is being asked for is usually the typing, not the music: a
bassline under a chord progression that already exists, a section
doubled, lyrics spelt into phonemes, the same figure moved up a third.
So:

- **Read the song you were given before changing it.** The arrangement
  says what the piece is: which phrases exist, which blocks each track
  plays, where the sections are.
- **Keep what you were not asked about.** Same instruments in the same
  slots, same block numbers, same phrase order. A model that renumbers
  things has destroyed the correspondence with what is on the panel.
- **Say what the machine will drop.** If the request needs `drive` or a
  ninth track, say so instead of writing a file that comes back
  different.
- **Judge on the device, not on paper.** A verdict on a passage comes
  from hearing it on the panel. `references/control.md` is how to get a
  picture of the screen and the playhead's position without asking
  anybody to describe them.

## Keeping this skill honest

`references/step-fx.md`, and the generated blocks of the other three
files, come out of the firmware itself — `app/fx_help.c`, `core/song.c`,
`app/help.c` — through `tools/gen_skill.py`. After touching the format,
the effects, the engines or the on-device help:

```bash
python3 tools/gen_skill.py            # rewrite what is generated
python3 tools/gen_skill.py --check    # 1 if anything is out of date
```
