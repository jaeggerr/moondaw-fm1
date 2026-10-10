# MOONDAW FM-1

MOONDAW for the M-VAVE FM-1 pocket synth: the same songs, the same
`.trk` files, the same step and fx rules, on a machine with 512 KB of
RAM and no floating point.

What exists today: the engine (three chip voices, the envelope and
filter chain, every step fx but the two sends), the song file reader and
writer, and a screen that only plays. The interface is not designed yet.

| Backend | Build | What it is |
| --- | --- | --- |
| FM-1 | `./build.sh` → `build/moondaw.fwsc` | the firmware (see `BUILDING.md`) |
| SDL2 | `make sim` → `build/sim/moondaw-fm1 [song.trk]` | the FM-1 simulated in a window |
| Browser | `make web` → `build/web/` | the same in an AudioWorklet |

`make test` runs the SDL and web backends headless and the song reader
on real songs; `make difftest` renders those songs through this engine
and through MOONDAW's own and compares them.

```
core/        the tracker: song model, .trk, player, engines -- freestanding C,
             no libc, no malloc, no float
app/         what is on the screen (for now: a player), and the text cells
platform/    platform.h and its SDL and web implementations
firmware/    the FM-1 implementation, on Felucca's HAL and drivers
tools/       build, package and install tools (Felucca's), the table generator
tests/       host tests and the differential test against MOONDAW
```

**Installing firmware on an FM-1 is at your own risk** and can leave it
unable to start. Back up the stock flash first and have
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) ready.

## Credits and licence

GPL-3.0-only. The hardware abstraction, drivers, update loader and build
tools are from [Felucca](https://github.com/hugelton/Felucca) by Leo
Kuroshita (@kurogedelic), Hügelton Instruments; `UPSTREAM.md` lists every
file and what was changed. The package also contains three Apache-2.0
files from JieLi's AC79 SDK (`LICENSES/`).
