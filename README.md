# MOONDAW FM-1

MOONDAW is a tracker groovebox. This is MOONDAW as a firmware for the
**M-VAVE FM-1** pocket synth: write songs step by step on the device, with
the same `.trk` files, the same steps and effects as the other MOONDAW
apps (and one of its own, PRB), on a machine with 512 KB of RAM and no floating point.

It is an independent project, built on the open
[Felucca](https://github.com/hugelton/Felucca) firmware. M-VAVE has nothing
to do with it. **Status: beta.** It is used every day on a real FM-1, but it
has not been through many hands yet.

## What it does

- **Songs**: an arrangement of phrases (SONG), each phrase the blocks its
  tracks play (SECTION), each block its steps (PATTERN): up to four notes,
  an instrument and three effects a step, 8 tracks.
- **Sounds**: PULSE, NOISE and WAVE chip voices, VOX (a singing voice that
  sings the syllables you write) and DRUM (Felucca's drum voices), with an
  envelope, a filter, layers, reverb and delay sends, and a master limiter.
- **Writing**: the keyboard writes notes, REC records what you play over a
  count-in, a selection copies, pastes, cuts and clears, a loop plays a
  passage round, UNDO and REDO on the ALGO knob.
- **Playing**: MIXER (volume, pan, sends, mute and solo), per-cell levels
  in SONG, solo and mute from any screen with a track in front.
- **On the device**: HELP, a page for every screen and every effect; the
  buttons that do something on a screen light up (SETTINGS turns it off).
- **From a computer**: send and fetch songs over USB, and drive the device
  from a script (`tools/fm1_ctl.py`: read its state, press its buttons,
  write steps, capture its screen).

## Installing

**Installing firmware on an FM-1 is at your own risk** and can leave it
unable to start. Back up the stock flash first, and have
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) ready to
restore it.

- **In a browser**: `web/installer/` is a page that installs the firmware
  over USB from Chrome or Edge, with no tools to install; see
  `web/installer/README.md` for what it checks and its state.
- **From a terminal**: `python3 tools/fm1_install.py build/moondaw.fwsc`
  (Python 3 and `python-rtmidi`), after building (below) or with a released
  `moondaw.fwsc`.
- **A device that does not start**: `docs/ARRIVEE.md` walks through the
  first install, the backup and the rescue path that needs no extra
  hardware.

## Songs

```
python3 tools/fm1_send.py song.trk        # a song onto the device
python3 tools/fm1_send.py --list          # what is on it
python3 tools/fm1_send.py --get NAME -o song.trk   # a song back
```

The device holds 31 songs; a song is a plain-text `.trk` file, the same as
the other MOONDAW apps read and write (the format is MOONDAW's `FORMAT.md`).

## Building

| Backend | Build | What it is |
| --- | --- | --- |
| FM-1 | `./build.sh` → `build/moondaw.fwsc` | the firmware (see `BUILDING.md`) |
| SDL2 | `make sim` → `build/sim/moondaw-fm1 [song.trk]` | the FM-1 simulated in a window |
| Browser | `make web` → `build/web/` | the same in an AudioWorklet |

`make test MOONDAW=<path to MOONDAW>` runs the screens, the store, the link
and the remote headless, and the song reader on real songs; `make difftest`
renders those songs through this engine and through MOONDAW's own and
compares them.

```
core/        the tracker: song model, .trk, player, engines -- freestanding C,
             no libc, no malloc, no float
app/         the screens, the USB link and the remote, the on-device help
platform/    platform.h and its SDL and web implementations
firmware/    the FM-1 implementation, on Felucca's HAL and drivers
tools/       build, package, install, transfer and remote-control tools
tests/       host tests and the differential test against MOONDAW
docs/        studies and notes; NOTES.md is the design diary
```

## Credits and licence

GPL-3.0-only (`LICENSE`).

- The hardware abstraction, drivers, update loader and build tools are
  from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
  (@kurogedelic), Hügelton Instruments, and so are the DRUM engine's voices
  (Felucca's own, GPL-3.0, with DSP helpers from Felucca-Engines, MIT).
  `UPSTREAM.md` lists every file taken and what was changed.
- The band-limited step table in `core/synth.c` is from blip_buf by Shay
  Green (LGPL-2.1-or-later), rewritten in 32 bits.
- The effects' help pages (`app/fx_help.c`) are MOONDAW's own text, PRB's apart.
- Fonts: Barlow Condensed and JetBrains Mono (SIL Open Font License),
  font8x8 (public domain).
- Three Apache-2.0 files from JieLi's AC79 SDK are in the package.

Every third-party licence is in `LICENSES/`.
