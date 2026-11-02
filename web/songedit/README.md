# The song editor

A page that edits a MOONDAW song's instruments — above all its **sampled**
ones — and sends the result to the FM-1. It exists because a 240×240 panel
will never show a zone map well and a browser will.

The division it rests on, which is the FM-1's: **samples are global,
instruments are not.** The bank holds raw sounds and lives in the machine's
flash; an instrument — engine, envelope, filter, gain, layers, zones — lives
inside the song file. There is no instrument library on the device, by
decision. So an instrument is edited where the song is, and that is here.

| file | what it is |
| --- | --- |
| `index.html` | the page |
| `app.js` | the wiring: the file in, the editors, the file out |
| `trk.js` | the `.trk` format: `core/trk.c` and the part of `core/song.c` under it, in JavaScript |
| `zones.js` | the zone map: the keyboard ruler, the handles, the shadows |
| `bank.js` | the sample bank's index, and `core/bank.c`'s name rules |
| `audio.js`, `worklet.js` | the audition: the FM-1's core in an AudioWorklet |
| `audition.c`, `build.sh` | that core compiled to wasm — `core/player.c` and the engines, no app, no platform |
| `test/roundtrip.mjs` | `trk.js` against `core/trk.c`, on every `.trk` this computer has |

## Running it

The page needs no server for the editing, but ES modules do, and WebMIDI needs
a secure context. Serve the **repository root** — the page imports
`web/installer/`'s modules and reads the wasm out of `build/` :

```sh
make songedit                                  # build/songedit/audition.wasm
python3 -m http.server -b 127.0.0.1 -d . 8794
```

Then <http://127.0.0.1:8794/web/songedit/> in Chrome or Edge.

`?song=URL` and `?bank=URL` open a file straight away, which is what the demo
link on the page uses:

    /web/songedit/?song=../../docs/sampler-demo/songs/SMPL-DEMO.trk&bank=../../build/demo-bank.bin

The demo's bank is not in the repository (126 KB of samples that live in
`../moondaw`); `docs/sampler-demo/README.md` has the one command that packs it.

## Lossless, and how that is known

The page writes a `.trk` with `trk.js`, not with the C. So `trk.js` is a port
of `core/trk.c` kept honest by a test rather than by hope:

```sh
node web/songedit/test/roundtrip.mjs      # also part of `make test`
```

For every `.trk` in the repository and in `../moondaw/songs` it asks three
things. **Canonical**: `trk.js` load-and-save gives byte for byte what
`build/trkdump` (the FM-1's own reader and writer) gives. **Stable**: loading
that output and saving it again gives the same bytes. **Lossless under
editing**: a song the test edits the way the page edits — every instrument's
envelope, filter, gain, name, and for each sampled one a sample swapped, a
zone moved, one added and one dropped — comes back out of `core/trk.c`
unchanged. It then checks `bank.js`'s name resolution against
`core/bank.c`'s, compiled into the wasm, over the cases that differ (the
folder, the `.wav`, the case, a name too long).

`build/trkcheck` is the other half of the claim and is run on a file the page
wrote by hand:

```sh
build/trkcheck build/page-edited.trk
```

## What the page says that the file does not

Three rules of the FM-1 are invisible in a text editor and are the reason this
page is worth having:

- **An instrument no step plays is not written.** `core/trk.c`'s writer puts
  down only the instruments a pattern names. The list shows the others dashed.
- **A zone naming a sample the bank has not got is silent, never an error.**
  Load the bank (a packed file, or the index over the link) and those zones go
  red, in the table and on the map.
- **The first zone holding a note wins.** So the table's order is the
  priority, and the part of a zone an earlier one already answers will never
  sound. That is the red on the map, and the “6 of 46” in the KEYS column.

## The link

Reading the bank's index and sending the song go over `app/link.h` — the same
route `web/installer/` uses, in the same module (`../installer/mdlink.js`):
`INFO`/`READ` on slot 32 for the bank, `BEGIN`/`DATA`/`END` for the song, as
`tools/fm1_samples.py` and `tools/fm1_send.py` do them. The FM-1 must be on
its MENU screen; it refuses a transfer anywhere else.

## The audition

`audition.c` links `core/player.c`, `core/synth.c`, `core/fx.c`,
`core/drum.c` and `core/sampler.c` and nothing else — no app, no panel, no
flash. So a note played here goes through the same envelope, filter, reverb
and delay the device runs, and a sampled note through the same IMA ADPCM
decoder at the same rate.

It is not `platform/web/`, which runs the *whole machine* including its
screens. That is the right thing for trying the firmware and the wrong one
here: auditioning one zone means playing a chosen note on a chosen
instrument, which through the panel would mean steering the screens.

It is used twice now. `web/export/` renders a song to WAV with this same
wasm — one file per track — and the one thing it needed that was not already
here is `aud_play_once`: the arrangement once, so the sequencer says when a
render ends instead of JavaScript working that out a second time. Nothing else
about this file is the export page's; it remains the editor's engine, which
happens to be the machine's.

`core/bank.c` is the one piece not linked — it reads the index through the
store's sector map, over a flash this has none of — so the small part of it
the engine needs is in `audition.c`, with `bank_find`'s rules word for word.
