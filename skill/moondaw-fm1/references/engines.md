# The six engines that sound, and the one that does not

An instrument picks an engine and a `timbre`, then goes through the same
chain: envelope, filter, level, sends to the reverb and the echo, and the
final limiter. The ranges are in `format.md`.

## PULSE — timbre 0–3

The square wave of the game consoles, and the thinner, more nasal shapes
beside it: **12.5%, 25%, 50%, 75%**. `fDUT` changes the width step by
step, over the instrument. Timbre 1 (25%) is the classic bass; 2 (50%)
the plain square.

## NOISE — timbre 0–1

A shift register, the same silicon. **0 LONG** hisses, **1 SHORT** is
metallic and tonal. Hats, snares and wind. The note still matters: it
clocks the register, so NOISE is tuned, not pitchless.

## WAVE — timbre 0–4

A 4-bit wavetable: **0 SAW, 1 TRI, 2 SINE, 3 HALF, 4 ABS**. `fWAV`
changes the shape step by step, as `fDUT` does for PULSE.

## SMPL — timbre 0

The bank's short one-shots, 22 kHz. The instrument plays whatever its
**zones** say (`format.md`, "Samples"), and the sound itself lives in the
device's sample bank, not in the song: a `smp` entry is a name.

The bank is filled from the computer, separately from any song:

```bash
python3 tools/fm1_samples.py add kick.wav snare.wav
python3 tools/fm1_samples.py list
python3 tools/fm1_samples.py remove tr9_kick
```

`add` takes any WAV and makes it what the device plays: mono, resampled
to 22 050 Hz, IMA ADPCM, silence trimmed at both ends, one second at
most, normalised to −1 dBFS. The sample is named after its file. The
bank is one file in the flash beside the songs and is rewritten whole
each time, so a sample more is a song less.

## VOX — timbre 0–4

A sung voice that pronounces the syllables the steps carry. Styles
**0 GLIDE, 1 HARD, 2 SOFT, 3 MELO, 4 SINGR** — the first three a formant
bank, the last two a Klatt cascade. Plus `tract` (50–150%, the length of
the vocal tract, low to piercing) and `accent` (`FR` or `EN`).

The consonants strike at the note-on, the first plain vowel holds as long
as the note does, and the rest is pronounced at the release. Under `fSLI`
the next note keeps the vowel going instead of striking again, which is a
melisma.

A syllable is written phonetically, one character a phoneme, up to five,
in an effect slot: `'MA`. **Every digit is a vowel, every letter is
itself.**

| signs | sound |
|---|---|
| `A E I O U Y` | a, é, i, o, ou (loup), u (tu) |
| `7 2 9 6` | è (s**ept**), eu (d**eux**), eur (n**euf**), open o (pomme) |
| `3 0 5` | the nasals: an (tr**ente**), on, in/un (c**inq**) |
| `1 4 8` | the English three: short i (b**i**t), a (c**a**t), oo (b**oo**k) |
| `P B T D K G F V S Z M N L R H` | as written; `R` is the French r |
| `C J X Q` | ch (chat), j (jour), th (**thin**), English r |
| `+` | the voiced th of **the** |

A `.` anywhere in the syllable marks it **unstressed**: the vowel is
pulled two thirds of the way to the schwa and sung 4 dB softer, which is
what English does to its small words — `'.+2` is *the*, `'.T2` is *to*.
Glides come from position: `I`, `Y`, `W` before a vowel glide (`NYI`
nuit, `WI` oui, `RI5` rien), and a vowel after the vowel makes a
diphthong (`MAI` my, `NAW` now). Liaisons are written where they are
sung: *les amis* is `LE ZA MI`.

Two signs are easy to reach for and wrong, both for English. `2` is the
front rounded eu of *deux* and this alphabet's **schwa**: it belongs to
weak syllables and nothing else — `K2P` for *cup* is heard as *ceup*,
and the right answer is `KAP`. And `6` is a British *stop*; an American
one says the vowel of *father*, which is `A`.

Unknown characters are skipped, never fatal. The syllable is a
**setter**: the notes that follow keep singing it until another is
written, so *la la la* costs one cell.

## DRUM — timbre 0–9

Felucca's drum voices as an engine: the timbre picks the kit, the note
picks the piece, and `drum TUNE TONE DECAY SNAP` (0–127, `64` as
designed) moves every piece of the kit at once.

<!-- GEN:drums -->
### The kits, and the pieces each one has

`timbre` picks the kit. A kit has 8 pieces, or 11 for Felucca's own two, and the note picks the piece. The note beside each piece is where it sounds as it was designed; the rest of the map is those same pieces retuned.

| timbre | kit | pieces |
|---|---|---|
| 0 | `STD` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CY C#3 CB G#3 CG D-4 CL D#5 |
| 1 | `ROUND` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CY C#3 CB G#3 CG D-4 CL D#5 |
| 2 | `80` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CB C#3 |
| 3 | `10` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CY C#3 |
| 4 | `66` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 CG A-2 OH A#2 CB C#3 |
| 5 | `55` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CB C#3 |
| 6 | `77` | BD C-2 CL C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CY C#3 |
| 7 | `DEEP` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CY C#3 |
| 8 | `TIGHT` | BD C-2 RS C#2 SD D-2 CP D#2 CH F#2 TM A-2 OH A#2 CB C#3 |
| 9 | `PERC` | BD C-2 CL C#2 SD D-2 CP D#2 CH F#2 CG A-2 OH A#2 CB C#3 |

### The map

The General MIDI convention, notes `B-1` to `A-5`. A note outside it plays its pitch class of `C-2`–`B-2`, so nothing is ever silent.

| note | | note | | note | |
|---|---|---|---|---|---|
| `B-1` | BD | `D#3` | CY | `G-4` | CB |
| `C-2` | BD | `E-3` | CY | `G#4` | CB |
| `C#2` | RS | `F-3` | CB | `A-4` | CH |
| `D-2` | SD | `F#3` | CH | `A#4` | CH |
| `D#2` | CP | `G-3` | CY | `B-4` | CL |
| `E-2` | SD | `G#3` | CB | `C-5` | CL |
| `F-2` | TM | `A-3` | CY | `C#5` | CH |
| `F#2` | CH | `A#3` | CL | `D-5` | CH |
| `G-2` | TM | `B-3` | CY | `D#5` | CL |
| `G#2` | CH | `C-4` | CG | `E-5` | CL |
| `A-2` | TM | `C#4` | CG | `F-5` | CL |
| `A#2` | OH | `D-4` | CG | `F#5` | CG |
| `B-2` | TM | `D#4` | CG | `G-5` | CG |
| `C-3` | TM | `E-4` | CG | `G#5` | CB |
| `C#3` | CY | `F-4` | TM | `A-5` | CB |
| `D-3` | TM | `F#4` | TM |  |  |

`BD` kick, `SD` snare, `RS` rim, `CP` clap, `CH` closed hat, `OH` open hat, `TM` tom, `CY` cymbal, `CB` bell, `CG` conga, `CL` clave.

Each piece of a track is one lane, and a lane is mono: a hit cuts that lane's last hit, so a retuned tom cuts the tom before it, and a closed hat chokes the open one. Different pieces ring together.
<!-- /GEN -->

## PLUCK — named, silent

The name survives a round trip so a song written on another MOONDAW comes
back unharmed, but there is no such engine here: the slot makes no sound.
Anything you write for PLUCK is a silent track on this machine.
