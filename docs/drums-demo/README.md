# Drums: four candidate engines, to listen to

**Choice made on 29/10: A, Felucca.** Its engine is now MOONDAW's,
the **DRUM** engine (see "The DRUM engine, integrated" at the bottom).
`1-drum.mp3` is that engine, the real one, rendered by the FM-1 core; the
rest of this page is the study that led to the choice.

Nothing was installed on the FM-1. Everything is regenerated with

```sh
python3 tools/drums/drums.py
for f in docs/drums-demo/*.wav; do ffmpeg -y -i "$f" -q:a 2 "${f%.wav}.mp3"; done
```

(numpy; the "current" reference is rendered by `tests/fm_render.c`, the
FM-1 core itself, from `tools/drums/current-kit.trk`).

## The files

All mono at 44 100 Hz (`SYNTH_FS`), at the same loudness (each file's
beat at −17 dBFS RMS): we compare colours, not volumes. What is kept here
is MP3 (LAME `-q:a 2`) of the 16-bit WAV the generator writes; the WAV
itself is not kept in the repository.

| File | Contents |
| --- | --- |
| `0-actuel.mp3` | the drums from before DRUM (see below) |
| `1-drum.mp3` | **the integrated DRUM engine** (Felucca's STD kit), same grid |
| `A-felucca.mp3` | candidate A, the design of Felucca's DRUM engine |
| `B-analog.mp3` | candidate B, the "analog" models of Plaits/Peaks (808) |
| `C-synth.mp3` | candidate C, the "synthetic" models of Plaits (909-ish) |
| `D-chipplus.mp3` | candidate D, the current engines with what they lack |
| `comparatif-rythmes.mp3` | the bar of beat with its break, in the order current, DRUM, A, B, C, D, separated by 0.6 s |
| `comparatif-petit-haut-parleur.mp3` | the same, through a 200 Hz high-pass (12 dB/oct), one gain for all: roughly what the FM-1 speaker lets through |

Each kit file follows the same grid (120 bpm, 16 steps per bar,
the grid of `current-kit.trk`):

| Time | What you hear |
| --- | --- |
| 0 s | kick alone |
| 1 s | snare alone |
| 2 s | closed hi-hat alone |
| 3 s | open hi-hat alone |
| 4 s | clap alone |
| 5 s | tom (high) alone |
| 6 → 10 s | two bars of beat: kick on quarter notes, snare on 2 and 4, closed hi-hat on the beats, open on the off-beats (the closed cuts the open), then at the end of the 2nd bar a clap and two toms |

The prototypes are in floating point, sample by sample: they say what
each engine does, not how the FM-1 would compute it. The settings are
each source's design settings, retouched by ear on paper
only — I could not listen.

## Today

MOONDAW has no drum engine. Drums are made with the three
"chip" engines of `core/synth.c`:

- **kick**: WAVE (the 32-step triangle shape), `drive`, and
  `fSWP` which drops the pitch — in steps, one per tick (≈ 20 ms at
  120 bpm); DAFT-PUNK: `fSWP2E`, −5.8 semitones per tick;
- **snare, clap, hi-hats**: NOISE, the 15-bit shift register
  (or 7 bits for the "metallic" one, timbre 1) under the track's ADSR and filter;
  no real body for the snare (here a PULSE layer);
- **toms**: WAVE with a gentler `fSWP`.

The measurements on `0-actuel.mp3` say what you hear: the kick
drops by 30 dB in **60 ms** (the candidates: 360 to 490 ms) and its spectral
centroid is at 1.5 kHz — that is the buzz of the triangle's steps
and the drive, not a "boom"; the tom glides down to 76 Hz. No
continuous pitch envelope, no attack click, no band-filtered noise,
no metallic source.

(KIDS, for its part, plays its drums on 909 samples — silent on the FM-1,
which has no SMPL.)

## The candidates

### A — FELUCCA (recommended)

**From**: Felucca's `firmware/src/drum_voice.c` and `eng_drum.c`, the
open firmware for the FM-1 by Leo Kuroshita (Hügelton Instruments) from which MOONDAW
already takes its hardware layer. Felucca's own design, tuned by
ear, with 5 "model" kits (80, 10, 66, 55, 77) and 3 composite ones.

**Licence**: **GPL-3.0-only**, like MOONDAW: the code can be taken
as is, keeping the copyright header.

**Technique**: everything tuned is a damped sine (phase
accumulator + SINE table + envelope), so stable and in tune, with no
high-Q resonator:

- kick: a sine swept in two stages (+2 octaves over 1.5 ms, then
  +1.5 octave held 12 ms that falls back), 2nd and 3rd harmonics phase-locked,
  a click (two periods of a high sine under a cosine window) and a
  soft-clip. Designed to keep at least −12 dB of energy above
  150 Hz, **to be heard on the small speaker** — it is the only
  candidate that aims for it;
- snare: two sines (f, 1.6 f) that glide down a little,
  band-pass noise under a tip and a body, a little
  high-pass noise on the tip;
- clap: band-pass noise around 1.1 kHz, struck 3 times at 10 ms, then
  a tail;
- hi-hats: the 808's "metal" source (six squares at 205, 304, 370,
  523, 540 and 800 Hz) into a band-pass at 7.1 kHz then a high-pass
  (12 kHz closed, 8.8 kHz open), plus a "sizzle"; the closed one
  chokes the open one;
- tom: a sine whose pitch follows the square of the amplitude, a little
  low noise for the stick.

It also has RIM, BELL, CONGA, CLAVE, CYM, not rendered here.

**CPU cost**: already written in 32-bit integers for this processor (only
32-bit products in the loops, envelopes in Q15/Q16, trapezoidal SVF
filters). Felucca's reference (`tests/cpu_baseline.txt`) gives a
whole DRUM kit at ≈ 1 100–1 270 host instructions per sample, as much
as a preset of its ANALOG engine (1 000–1 450); roughly, 20 to
40 operations per sample per sounding voice, the six squares shared
between hi-hats. A silent voice (−84 dB) costs nothing.

**Caution**: `A-felucca.mp3` is my floating-point recreation from the
description and the numbers in the code, **not** Felucca's exact sound. Its
test `tests/drum_test.c` renders its own demos (`build/drum_demo/`), but
I was not allowed to run the downloaded code here; run it
on your side to hear the original.

### B — ANALOG (Plaits / Peaks, 808-style)

**From**: Mutable Instruments Plaits (`plaits/dsp/drums/analog_bass_drum.h`,
`analog_snare_drum.h`, `hi_hat.h`) and Peaks (`peaks/drums/`), by Émilie
Gillet; also ported in DaisySP (`Source/Drums/`, Electrosmith).

**Licence**: **MIT** (Plaits, Peaks, DaisySP) — compatible with GPL-3.0; the
MIT notice is kept.

**Technique**: simulated analog circuits.

- kick: a very high-Q SVF filter (the 808's bridged T)
  struck by a 1 ms impulse; an attack FM and a self-FM
  bend its pitch;
- snare: five modes (1, 2, 3.18, 4.16, 5.62 × f) rendered by five
  resonators, and rectified noise in a band-pass at 16 × f;
- hi-hats: the six squares, a resonant band-pass, an asymmetric VCA
  ("swing"), a high-pass;
- clap: noise around 1 kHz under three sawtooth teeth, then a tail
  (the 808; Plaits has no clap);
- tom: the same resonator as the kick, higher.

**The sound**: the most "real" on headphones, round and deep — but the
kick puts 98 % of its energy below 150 Hz: on the FM-1 speaker
it almost disappears (listen to `comparatif-petit-haut-parleur.mp3`).

**CPU cost**: the most expensive. An SVF whose frequency moves every
sample (it needs tan(πf): a table), and the snare runs
**six** SVFs per sample (≈ 80–100 operations). At high Q, a fixed-point SVF
needs Q30 coefficients and care (Felucca ruled out
this option for that reason: "no high-Q resonator"). To be rewritten in integers.

### C — SYNTH (Plaits, 909-style)

**From**: Plaits, `synthetic_bass_drum.h`, `synthetic_snare_drum.h` and
the second model of `hi_hat.h`; DaisySP `SyntheticBassDrum`,
`SyntheticSnareDrum`. **MIT licence.**

**Technique**:

- kick: a sine held at its peak 1.3 ms then released under an
  FM envelope (the pitch drop), a "rounded triangle" sine in
  a transistor VCA, a click and an attack breath;
- snare: two distorted sines (f, 1.47 f) under an FM envelope, and
  white noise between a high-pass at 10 f and a low-pass at 35 f, held
  40 ms before decaying;
- hi-hats: three square × sawtooth pairs, ring-modulated
  (200 × 7 530, 510 × 8 075, 730 × 10 500 Hz), band-pass, high-pass, a
  two-stage decay;
- clap: noise at 1.2 kHz, four tight teeth, a bright layer, a
  120 ms tail;
- tom: a sine with a fast pitch drop and a burst of noise.

**The sound**: drier and more "digital" than B; the same low-end problem
for the kick on the speaker.

**CPU cost**: medium, ≈ 40–60 operations per voice (one or three SVFs,
divisions in the saturations, to be replaced by tables). No
high-Q resonator: easier in fixed point than B.

### D — CHIP+ (the cheapest)

**From**: MOONDAW itself. No outside code.

**Technique**: keep WAVE and NOISE, but give them what they lack:

- an **exponential pitch envelope computed every sample**
  (instead of the per-tick `fSWP`): the kick starts at 8 × f and falls back
  in 22 ms; plus a 1.5 ms PULSE edge for the click;
- a **per-sample decay** (not a per-tick ADSR);
- the shift-register noise, **high-passed** (7 kHz for the
  hi-hats, 1.5 kHz for the snare, a 0.8–2.5 kHz band for the
  three-tooth clap);
- snare: triangle + noise into a soft-clip.

**The sound**: stays "chiptune" (the triangle's steps, the register's
noise), but with a real "boom" and clean attacks. The best
improvement/effort ratio if we want to stay in the chip spirit.

**CPU cost**: almost nil, 5–15 operations per voice; the noise and the WAVE
stay in the band-limited buffer, the pitch envelope is computed
once per edge.

## And elsewhere

- **M-VAVE stock firmware**: a six-operator DX7-style FM,
  128 presets, arpeggiator, 16-step sequencer; no documented drums,
  and closed code.
- **FM1 Open** (the open firmware announced by M-VAVE, October 2026) mentions
  "808-style" sounds; **Sloop** (isod89/sloop-fm1) announces 37 drum
  kits, **Hortator** 45 drum engines. I did not check their
  sources or their licences: worth a look if A is not enough.
- **LMMS / kicker** (GPL-2.0+): Kicker's technique is that of D and
  of A (a sine, a start→end pitch envelope with a slope, a
  click, a distortion), in floating point; nothing more to take than A.

## How it would fit into MOONDAW

**A new `ENGINE_DRUM` engine**, on VOX's path: an engine that
computes its samples and adds them to the track's after reading
the buffer (`vox_render` in `player.c`), not an edge engine.

- `timbre` = the piece: KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM, BELL
  (and Felucca's variants); one instrument = one piece, as we already
  write KICK, HATC, HATO, CLAP as separate instruments;
- the **note** tunes the piece around its design pitch (C-4 =
  as designed, like Felucca's TUNE);
- the ADSR's **decay** gives DECAY; the filter's `cutoff` could give
  TONE; a field is missing for the "extra" (SNAP: drive, snare
  more or less snappy, spacing of the clap's teeth…) — one more field
  in `InstrumentDef`, or reuse `tract`, which only makes sense for VOX;
- velocity / `fVOL` gives the accent; two HAT instruments on the same
  track already choke each other (mono track);
- the `.trk` format is shared: **moondaw** (iOS, Mac, web,
  console) must also know `engine DRUM`, otherwise these instruments fall back
  to the pulse there. The synthesis core being shared, the same `drum_voice.c`
  can go in there.

D, for its part, would be done **without a new engine**: two parameters on WAVE and
NOISE (a pitch envelope: depth and time; a high-pass on
NOISE), or an `fSWP` smoothed per sample.

## Recommendation

**A, Felucca's DRUM engine, ported as is.** On paper it is by far the
best choice:

1. it was designed **for this device**: this processor (32-bit integers,
   no unstable resonator), this speaker (the kick keeps its
   energy above 150 Hz), this budget (a silent voice costs nothing);
2. same licence as MOONDAW (GPL-3.0-only), same author as the hardware
   layer already taken: no rewrite, tests that come with it
   (`drum_test.c`: pitches, durations, no clipping, hi-hat
   choking);
3. it brings 8 pieces and 8 kits, not just the 6 of this demo.

B sounds the most "808" on headphones but gets lost on the speaker and costs
the most; C is a good second if we want a 909 character; D is the
fallback plan if we want to stay 100 % chiptune and not touch the
format.

**Before deciding**: listen to `comparatif-rythmes.mp3` on headphones, then
`comparatif-petit-haut-parleur.mp3`, and run Felucca's `drum_test`
to hear A's original (my version is only an
approximation of it).

**What integrating A would take**:

1. copy `drum_voice.c` (≈ 1 000 lines) with what it takes from
   Felucca-Engines' `dsp.c` (the SINE table, `pitch_inc`, the trapezoidal SVF,
   `softclip`; MIT) into `core/`, adapt it to our block (128 samples);
2. `ENGINE_DRUM` in `song.h`, `trk.c` (read and write `engine DRUM`), the
   rendering in `player.c` next to VOX, the split between the two cores;
3. the FM-1 instrument screen (piece, TUNE, DECAY, TONE, SNAP);
4. a host test (take the targets of `drum_test.c`) and a CPU measurement on
   the device (a dense beat on DAFT-PUNK-EN);
5. the same engine in moondaw, so that songs stay the same
   everywhere.

## Sources

- Felucca: <https://github.com/hugelton/Felucca> (`firmware/src/drum_voice.c`,
  `eng_drum.c`, `tests/drum_test.c`, `tests/cpu_baseline.txt`, `LICENSING.md`)
- Mutable Instruments: <https://github.com/pichenettes/eurorack>
  (`plaits/dsp/drums/`, `peaks/drums/`)
- DaisySP: <https://github.com/electro-smith/DaisySP> (`Source/Drums/`)
- FM-1: [Synthtopia, FM1 Open](https://synthtopia.com/content/2026/10/20/m-vave-officially-opens-fm-1-firmware-to-developers-with-new-workbench-sdk),
  [Synthtopia, alternative firmwares](https://www.synthtopia.com/?p=155069),
  [Synth Anatomy, FM-1 review](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html)

## The DRUM engine, integrated

Felucca's drum voices (`core/drum_voice.inc`, taken
as is) in a new engine, `core/drum.c`. `UPSTREAM.md` says what
comes from where.

- **One instrument = one kit.** TIMBRE chooses which: STD (Felucca's
  own), ROUND (STD with the round kick), then Felucca's model
  kits: 80, 10, 66, 55, 77, DEEP, TIGHT, PERC.
- **The note chooses the piece**, on the General MIDI map as in
  Felucca: C-2 kick, C#2 rim, D-2 snare, D#2 clap, F#2
  closed hi-hat, A#2 open hi-hat, toms on F G A B C D, cymbals,
  bells, congas and claves up to A-5 (81). Outside 35..81, a note
  plays its pitch class between C-2 and B-2. A chord strikes several
  pieces.
- **The DRUM line of the INSTRUMENT screen**: TUNE, TONE, DECAY, SNAP, 64 =
  as designed. In the file: `drum 64 64 64 64`, written only if
  one is not at 64. Velocity gives the accent.
- **The hit is the envelope**: a kit's track stays open, each
  piece of a track is a mono lane, the lanes sound together, the
  closed hi-hat chokes the open one. An OFF or a GAT still closes the
  track with the instrument's release. On INSTRUMENT, PATTERN and
  PHRASE, the first C key (the 8th) is the kick.
- **12 lanes shared** by all tracks, 3.4 KB of RAM; about
  20 KB of flash.
- **Estimated cost** (not yet measured on the device): about 2 % of a
  core per sounding piece, 18 % for eight at once.
- **Songs**: the demo built into the firmware now plays a kit. In
  `songs/`, DAFT-PUNK, DAFT-PUNK-EN and KIDS converted by
  `tools/drums/to_drum.py` (KIDS, whose drums were samples
  silent on the FM-1, finally has its own). The originals in the moondaw
  repository are not touched: moondaw does not know DRUM yet and would play
  the pulse there.
