# The `.trk` format, as the FM-1 reads and writes it

The truth is `core/trk.c`, the reader and the writer the firmware itself
runs; the numbers below come out of `core/song.c` through
`tools/gen_skill.py`. Where this file and the machine disagree, the
machine is right and this file is a bug.

UTF-8 text, one instruction a line, LF. A `#` at the start of a line or
after a blank opens a comment to the end of the line — inside a token it
is an accidental (`C#4`), not a comment. Empty lines and lines nothing
recognises are **ignored in silence**, which is the single most
expensive thing about writing this format by hand: a misspelt keyword
does not fail, it vanishes. Run the file through `trkdump` (below) and
read what comes back.

## The shape of a file

    version 3                  <- first, and mandatory
    bpm / ticks / meter / tracks / swing / reverb / delay

    smp NN NAME                <- the sample table
    inst NN engine ...         <- the instruments, and their zones and layers
    zone NN lo hi root R smp NN
    lay NN engine ...

    trk N vol V pan P          <- where the mixer starts

    pat tN BB len L            <- a block of steps, then its rows
    RR NOTES iNN fFXVV 'SYL

    sec NN                     <- optional grouping of the phrases
    arr NN steps auto          <- the arrangement, one phrase an entry
    tN BB

Order mostly does not matter — the reader takes each line as it comes —
with three exceptions that do:

- `version 3` **must be the first line that parses**. Any other version
  is refused outright, and a file with no `version` line at all is
  refused too.
- A step row belongs to the `pat` line above it, and a `tN BB` row to
  the `arr` above it. These are the only two places where a line needs
  context, and anything else in between ends it.
- A `name` line belongs to the `arr` or `sec` above it.

`patch 3` is **refused by this machine**. The other MOONDAW apps take an
edit in the same grammar that replays onto the song they hold; the FM-1
does not. Every file you send it is a whole song, carrying every field
of the one you were given, changed only where you were asked —
**anything you leave out is not "unchanged", it is gone.**

<!-- GEN:limits -->
### What the machine holds

| | |
|---|---|
| tracks | 8, written `t0`–`t7` |
| blocks a track | 16, `00`–`0F` |
| steps a block | 128, rows `00`–`7F` |
| steps in the whole song | 6000, from one pool |
| notes a step | 4 |
| effects a step | 3 |
| instruments | 64, `i00`–`i3F`, 4 layers each |
| phrases | 128, `arr 00`–`arr 7F` |
| steps a phrase may be held for | 1024 |
| sample slots | 64, and 128 zones |
| syllables | 255 distinct, 5 characters each |
| names | 7 characters an instrument, 12 a phrase or a section |

A step nobody wrote costs nothing: the pool is shared, so an empty block and the bars of silence inside a long one are free. What costs is steps actually written, and they are counted for the whole song.

### Ranges, and what a field is when the file leaves it out

| field | range | left out |
|---|---|---|
| `bpm` | 20–400 | 120 |
| `ticks` | 1–24 | 6 |
| `meter` | 1–16 each | 4 4 |
| `tracks` | 1–16, cut to 8 | 8 |
| `swing` | 50–75 | 50 |
| `reverb` | 0–127 both | 64 64 |
| `delay` | 0–96, 0–127 | 18 60 |
| `trk` | vol 0–127, pan -64–63, rev/ech 0–127 | vol 100 pan 0 |
| `len` | 1–128 | 16 |
| a note | 12–127 as MIDI, so `C-4` is middle C and `A-4` is 440 Hz | nothing on that step |
| `gain`, `level` | -64 to 24 dB | 0 |
| `tract` | 50–150 percent | 100 |
| `tune` of a zone | ±100 cents | 0 |

An `inst` line puts the whole slot back to neutral before it reads what the line names: what a line leaves out is the ground, never the slot's last tenant. The ground is

    inst NN engine PULSE timbre 2 adsr 0 0 255 10 filter 255 0 64 64

### The engines

| engine | timbre |
|---|---|
| `PULSE` | 0–3 |
| `NOISE` | 0–1 |
| `WAVE` | 0–4 |
| `SMPL` | 0–0 |
| `VOX` | 0–4 |
| `PLUCK` | 0–2 |
| `DRUM` | 0–9 |

### The instruments a song gets for free

These slots already hold a sound, so a song can play without one `inst` line of its own. They come back written out in full when the machine saves.

| slot | | engine |
|---|---|---|
| `i00` | BASS | `PULSE` timbre 1 |
| `i01` | SUB | `WAVE` timbre 2 |
| `i02` | LEAD | `PULSE` timbre 0 |
| `i03` | KEYS | `WAVE` timbre 1 |
| `i04` | HARP | `WAVE` timbre 0 |
| `i05` | PAD | `WAVE` timbre 3 |
| `i06` | ORGAN | `PULSE` timbre 2 |
| `i07` | BELL | `WAVE` timbre 2 |
| `i08` | VOICE | `VOX` timbre 2 |
| `i09` | KIT | `DRUM` timbre 0 |
<!-- /GEN -->

## The header

    version 3
    bpm 123
    ticks 6
    meter 4 4
    tracks 8
    swing 54
    reverb 64 64
    delay 18 60

`ticks` is how finely a step is divided, and every tick-valued effect
(`DLY`, `CUT`, `GAT`, `RET`, `STR`) is measured in them: at `ticks 6` a
gate can say 1 to 6, and the same `fGAT03` is half a step whatever the
tempo. `meter` is steps a beat and beats a bar — it draws the grid and
changes nothing you hear. `swing 50` is straight, `75` the hardest.
`delay` is its time in ticks and its feedback.

## Instruments

    inst 05 engine PULSE timbre 1 adsr 3 40 70 22 filter 160 60 82 96 name STAB

One line a slot, the index in hex, the fields in any order. All of them
are `keyword value`:

| | |
|---|---|
| `engine` | `PULSE` `NOISE` `WAVE` `SMPL` `VOX` `PLUCK` `DRUM` |
| `timbre` | the variant, clamped to the engine's own maximum |
| `adsr` | four numbers 0–255: attack, decay, sustain, release |
| `filter` | cutoff, resonance, envelope amount, and optionally velocity amount; `255 0 64 64` is no filter at all |
| `gain` | dB into the instrument's stages |
| `level` | dB out of them |
| `vibphase` | `0`–`255`, or `free` |
| `tract` | VOX only: the vocal tract, 50 to 150 percent |
| `accent` | VOX only: `FR` or `EN` |
| `drum` | DRUM only: TUNE TONE DECAY SNAP, 0–127, `64` as the kit was designed |
| `name` | up to 7 characters, to the end of the line |

`name` must come **last** on the line: it is read to the end, so
anything after it is swallowed into the name.

A `lay` line adds a layer to the instrument above it — up to 4, each a
second voice at an interval:

    lay 05 engine WAVE timbre 2 transpose -12 detune 7 gain -4

`transpose` is ±48 semitones, `detune` ±100 cents. A layer cannot be
`SMPL`.

### An instrument no step plays is not saved

The writer only writes the instruments that a kept block's steps actually
name with an `i` token — and with them, only the zones and samples those
instruments use. So a slot you define and never play comes back **gone**,
with no warning from `trkdump` or `trkcheck`: both are right, the machine
simply had nothing to save.

This bites when you write the instruments first and the notes later, and
when you take the last `i0A` out of a song and expect the sound to stay
behind for next time. If a sound is meant to survive, something has to
play it.

A block is kept on gentler terms: one that was written into is kept even
when no phrase names it, so writing a block and placing it later works.

### What this machine does not carry

Named here because a file that uses them loads anyway and comes back
**without them**:

- `drive`, `amp`, `tone` on an instrument — the shaping stages the other
  MOONDAW apps have.
- `preview` and `ref` / `refbpm` / `refstart` / `refbar` lines — the
  reference recording.
- `engine PLUCK` — the name survives a round trip, the sound does not
  exist here. The slot is silent.
- Tracks past the eighth, and any `pat t8`..`t15` with them.

## Samples

    smp 00 kick/kick_bk.wav
    inst 00 engine SMPL timbre 0 adsr 0 0 255 12 name KICK
    zone 00 C-1 C-3 root C-2 smp 00

A `smp` entry is a **name**, not a sound: the FM-1 plays the bank entry
of the same name, which is sent separately with
`tools/fm1_samples.py add`. A song cannot carry its audio, and a name
nothing matches is silence.

A `zone` spreads samples over the keyboard: the notes it covers, the
pitch the sample was recorded at, and which entry to play. The zones of
every instrument share one list and **file order is priority** — the
first zone holding a note wins. Optional `tune` (±100 cents) and `gain`
(dB). A zone that does not read whole is dropped whole.

`begin` and `loop A B [ping]` on a `smp` line are kept in the file and
**not played** here.

## Blocks of steps

    pat t2 03 len 32
    00 C-3+E-3+G-3 i02 fVOL40 fDLY03
    04 OFF
    08 D-3 i02 'MA fGAT02

`pat t<track> <block> len <steps>`: the track in decimal 0–7, the block
in hex, the length in decimal. A `pat` line **clears the block** before
reading the rows under it.

A row is `<row in hex> <tokens...>`, and the row is the whole truth of
its line: what the line does not say is empty, not inherited. Tokens, in
any order:

| token | |
|---|---|
| `C-4` | a note. Two characters then the octave, so every note is exactly 3 wide: `C-`, `C#`, `D-`, `D#`, `E-`, `F-`, `F#`, `G-`, `G#`, `A-`, `A#`, `B-`. No flats. |
| `C-4+E-4+G-4` | up to 4 notes struck together, joined by `+` |
| `OFF` | release what is sounding on this track |
| `---` | an explicitly empty step; normally just left out |
| `i03` | the instrument, two hex digits |
| `fVOL40` | an effect: `f`, the three-letter mnemonic, two hex digits. Three a step. See `step-fx.md`. |
| `'MA` | a sung syllable, in an effect slot. Phonemes, up to 5. |

The octaves are **MIDI's**: `C-4` is middle C and `A-4` is 440 Hz.
Worth saying because the other common convention puts middle C an octave
away, and a part written under it comes out right in shape and an octave
wrong in register.

A step that reads as empty is not stored at all, so writing `---` alone
on a row is the same as not writing the row.

An instrument an `i` token names but no `inst` line defines is whatever
that slot already holds — one of the ten presets for `i00`–`i09`, a
neutral pulse above them. That is a legitimate way to write a song, and
the machine writes the slot out in full when it saves.

## The arrangement

A **phrase** is one row of the arrangement, and it names one block per
track. The song plays the phrases in order, `arr 00` first.

    arr 00 steps auto
    name INTRO
    t0 00
    t1 03 vol 60
    t2 on
    t3 ---

| | |
|---|---|
| `steps auto` | the phrase lasts one full cycle: the LCM of the block lengths it names |
| `steps N` | or exactly N steps, up to 1024 |
| `name` | up to 12 characters |
| `tN BB` | track N plays block BB, optionally `vol` 0–127 for this phrase |
| `tN on` | hold: nothing struck, nothing released, whatever rings carries on |
| `tN ---` | the track is silent in this phrase. Also what a missing `tN` means. |

A phrase can instead be a **reference**, which plays something already
written:

    arr 05 ref arr 00 steps 16    <- phrase 00 again, for 16 steps
    arr 06 ref sec 01             <- the whole of section 01

`sec NN` above a run of `arr` lines groups them, and `name` under the
`sec` names the group; `sec ---` ends the grouping. A section reference
is a section of its own — written inside a group, the group loses its
other rows.

## Checking the file, which you should always do

Two host tools build from this repository with `cc` and need no device.
They are the firmware's own reader and writer, so what they say is what
the machine would do:

```bash
# what the FM-1 would save, given this file
cc -std=c11 -O2 tests/trkdump.c core/song.c core/trk.c -o build/trkdump
build/trkdump song.trk

# load, report what is not carried, write, reload, require the two writings
# to be identical, and look up every line it claims to carry in the original
cc -std=c11 -O2 tests/trkcheck.c core/song.c core/trk.c -o build/trkcheck
build/trkcheck song.trk
```

`trkdump` is the loop worth living in: write the file, dump it, and diff
the two. Everything you got wrong is in that diff — a keyword the reader
ignored is a line that came back missing, a value out of range is a
value that came back clamped, and a field this machine does not carry is
simply not there. `tools/fm1_send.py` runs the same dump before sending,
so the device receives exactly what the dump shows.
