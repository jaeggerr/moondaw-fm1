# Driving the machine over USB

`tools/fm1_ctl.py` is a remote control for a running FM-1: it reads what
the machine is doing, presses its buttons, turns its knobs, plays its
keys, writes into the song in memory, lights its LEDs, takes a picture of
its screen, and can take the screen over entirely and draw on it.

The device needs no special mode — this works while somebody is playing
on it. The protocol is `app/link.h` (version 4) over USB-MIDI, so
`python-rtmidi` is needed. `--udp` drives the simulator on this computer
instead (`build/sim/moondaw-fm1`), which is the right place to try
something out.

**Everything below is the hands' own.** An edit lands exactly where a
thumb would have put it: `ALGO` turned left takes it back, and nothing reaches
the flash until `save`.

## Reading what it is doing

```bash
python3 tools/fm1_ctl.py status
```

One line of `key=value`, meant to be read as it is:

| | |
|---|---|
| `screen=` | `HOME` `SONG` `PHRASE` `PATTERN` `INSTRUMENT` `MIXER` `PROJECT` `CANVAS` `SETTINGS` `HELP` `SYSTEM` |
| `song=` `slot=` `dirty=` | what is open, where it lives, whether it has unsaved edits |
| `playing=` `phrase=` `step=` | what is playing and exactly where the playhead is |
| `bpm=` `phrases=` `tracks=` | the song's shape |
| `cpu=` `max=` | load now, and the worst block since the last `--reset` |
| `batt=` `battraw=` | the cell, as a percentage and as the converter reads it |
| `free=` | chunks left in the step pool |

The other readings are for when something is wrong: `--profile` (the
worst block stage by stage, in µs, and the second core), `--boot` (why it
last started), `--trail` (where the previous run was when it ended),
`--crash` (the last crash's registers). `--reset` clears the worst-block
high-water mark.

```bash
python3 tools/fm1_ctl.py screen -o shot.png
```

The panel as a 240×240 picture, band by band. This is the one that makes
a model able to judge its own work: do something, take the screen, look
at it.

## Pressing things

```bash
python3 tools/fm1_ctl.py press SEL
python3 tools/fm1_ctl.py press HOME SEQ        # several in one go, together
python3 tools/fm1_ctl.py turn K2 -3            # three clicks anticlockwise
python3 tools/fm1_ctl.py key C-4 --hold 20     # a key, held 20 frames
```

The buttons, as printed on the panel:

    FX  SEL  ENV  LFO  EDIT  GLO
    HOME  SAVE  ARP  SEQ  PLAY  REC
    OCT-  OCT+

The knobs: `SELECT` `ALGO` `PRESETS` `K1` `K2` `K3` `K4`. A turn is
decimal and signed.

The keyboard is 27 keys, `F-3` to `G-5`, and `OCT-`/`OCT+` do not move
them — they move what the keys play.

Note the panel's names against the words the screens use: `ALGO` is UNDO
turned left and REDO turned right, `ARP` opens the chords of the key on
PATTERN's notes, `SEQ` `FX` `ENV` pick the column on
PATTERN, `GLO` opens the project, `FX` the mixer. `HOME` goes back one
step and **held** returns to the menu from anywhere.

<!-- GEN:gestures -->
What each screen answers, as HELP on the device says it. The words are the machine's own.

### HOME

| | |
|---|---|
| `SELECT, KNOB 1` | move |
| `SEL` | go: CONTINUE, PROJECTS... |
| `SYSTEM` | flash left, version, CPU |
| `PLAY on CONTINUE` | play the open project |
| `in PROJECTS: SEL` | open the project |
| `in PROJECTS: PLAY` | play it, staying here |
| `in PROJECTS: EDIT` | rename, duplicate, delete |
| `HOME` | back to the menu |

### SONG

| | |
|---|---|
| `SELECT, KNOB 1` | phrase |
| `KNOB 2` | track |
| `KNOB 3 on a cell` | its pattern |
| `  at rest` | it plays by itself |
| `  past the last` | a new pattern, NEW |
| `KNOB 3 on a number` | phrase, or a reference |
| `KNOB 3 on a REF` | the phrase it plays |
| `  past the phrases` | a whole section |
| `KNOB 4 on a cell` | its level in the phrase |
| `KNOB 4 on a number` | the phrase's length |
| `KNOB 4 on a REF` | how long it plays |
| `KNOB 4 on a marker` | phrases the section holds |
| `PRESETS on a cell` | choose a pattern |
| `PRESETS on a REF` | choose a phrase |
| `ENV` | section: these phrases |
| `  with no selection` | the one under the cursor |
| `  in the list` | --, a section, or NEW |
| `  NEW` | makes one, and names it |
| `SEL on a cell` | edit the pattern |
| `SEL on a number` | the phrase, step by step |
| `SEL on a REF` | go to what it plays |
| `SEL on a marker` | name the section |
| `EDIT held` | black keys: commands |
| `EDIT tapped` | the same, as a caps lock |
| `  tapped again` | out |
| `EDIT + OP5` | selection, in and out |
| `  then OP5 alone` | the same, once it is on |
| `  on a number` | whole phrases |
| `OP1 OP2 OP3` | copy, paste, cut |
| `OP4` | clear |
| `OP4 on a marker` | the section, not its phrases |
| `OP6` | loop these phrases |
| `OP6, none stretched` | a loop's end, then other |
| `MONO` | solo; held: mute |
| `SEQ on a number` | copy the phrase below |
| `LFO on a number` | delete the phrase: twice |
| `keys` | the track's instrument |
| `OCT- OCT+` | the keyboard's octave |
| `FX` | the mixer |
| `GLO` | the project |
| `PLAY` | the song, from the cursor |
| `  again` | stop, the cursor there |
| `ALGO` | undo left, redo right |
| `HOME` | back to the menu |

### PHRASE

| | |
|---|---|
| `SELECT, KNOB 1` | step |
| `KNOB 2` | track |
| `keys` | write the note |
| `OCT- OCT+` | the keyboard's octave |
| `SEL, EDIT` | the track's pattern |
| `EDIT held + MONO` | solo; held: mute |
| `EDIT held + OP6` | a loop's end, then other |
| `FX` | the mixer |
| `GLO` | the project |
| `PLAY` | the phrase, round |
| `ALGO` | undo left, redo right |
| `HOME` | back to SONG |

### PATTERN

| | |
|---|---|
| `SELECT, KNOB 1` | step |
| `KNOB 2` | column |
| `KNOB 3` | note, instr., effect |
| `KNOB 4` | an effect's value |
| `SEQ, FX, ENV` | notes, effects, instr. |
| `ENV, SEL on instr.` | edit the instrument |
| `PRESETS on instr.` | choose an instrument |
| `SEL on an effect` | the syllable |
| `ARP on notes` | chords on the keyboard |
| `  white keys` | degrees, octave up |
| `  OP1 to OP5` | the section |
| `EDIT held + ARP` | the list: section, inv. |
| `keys` | write in a note column |
| `keys elsewhere` | play, write nothing |
| `OCT- OCT+` | the keyboard's octave |
| `EDIT held` | black keys: commands |
| `EDIT tapped` | the same, as a caps lock |
| `  tapped again` | out |
| `EDIT + OP5` | selection, in and out |
| `  then OP5 alone` | the same, once it is on |
| `OP1 OP2 OP3` | copy, paste, cut |
| `OP4` | clear |
| `OP6` | loop the selection |
| `OP6, no selection` | a loop's end, then other |
| `MONO` | solo; held: mute |
| `ALGO` | undo left, redo right |
| `PLAY` | round, from the cursor |
| `  again` | stop; the cursor stays |
| `REC` | armed: PLAY counts in |
| `  then keys` | record; OFF when let go |
| `EDIT held + REC` | the metronome, on, off |
| `EDIT held + SELECT` | listening speed, 25-100 % |
| `GLO` | the project |
| `HOME` | back |

### SYLLABLE

| | |
|---|---|
| `PRESETS` | keys or wheel |
| `KEYS: white keys` | consonants |
| `KEYS: black keys` | vowels |
| `KEYS: OCT` | the other signs |
| `KEYS: LFO` | rub the last out |
| `KEYS: ARP` | weak (.) |
| `WHEEL: KNOB 2` | the slot |
| `WHEEL: KNOB 3` | its sign |
| `WHEEL: LFO` | empty the slot |
| `PLAY` | sing it again |
| `ALGO` | undo left, redo right |
| `SEL` | write it in the step |
| `HOME` | back, nothing written |

### INSTRUMENT

| | |
|---|---|
| `SELECT, KNOB 1` | row |
| `KNOB 2` | column |
| `KNOB 3` | value |
| `PRESETS` | another instrument |
| `SEL on NAME` | name it |
| `keys` | play it |
| `OCT- OCT+` | the keyboard's octave |
| `PLAY` | a preview |
| `GLO` | the project |
| `ALGO` | undo left, redo right |
| `HOME` | back to the pattern |

### MIXER

| | |
|---|---|
| `SELECT, KNOB 1` | track |
| `KNOB 2` | column |
| `KNOB 3` | value |
| `KNOB 3 on a number` | normal, mute, solo |
| `MONO` | solo; held: mute |
| `EDIT, GLO` | the project |
| `SEQ` | SONG |
| `PLAY` | the song |
| `ALGO` | undo left, redo right |
| `HOME` | back |

### PROJECT

| | |
|---|---|
| `SELECT, KNOB 1` | setting |
| `KNOB 3` | its value |
| `SEL on NAME` | name the project |
| `FX` | the mixer |
| `SEQ` | SONG |
| `PLAY` | the song |
| `ALGO` | undo left, redo right |
| `HOME` | back |

### SETTINGS

| | |
|---|---|
| `SELECT, KNOB 1` | setting |
| `KNOB 3` | its value |
| `LEDS` | off, state, guide |
| `LIGHT` | how bright the guide |
| `COUNT-IN` | bars before REC records |
| `HOME` | back, kept |

### GESTURES

| | |
|---|---|
| `SELECT, KNOB 1` | move: the row |
| `KNOB 2` | move: the column |
| `KNOB 3, KNOB 4` | change |
| `SEL` | into what is under |
| `HOME` | back |
| `HOME held` | the menu, from anywhere |
| `GLO` | the project |
| `ALGO` | undo left, redo right |
| `EDIT held` | black keys: commands |
| `EDIT held + REC` | the metronome, on, off |
| `EDIT held + SELECT` | listening speed, 25-100 % |
| `lit buttons` | what this screen does |
| `SAVE` | save the project |
| `PLAY` | play, stop |
<!-- /GEN -->

## Writing into the song

```bash
python3 tools/fm1_ctl.py open NAME          # a song from the flash into memory
python3 tools/fm1_ctl.py new                # an empty song
python3 tools/fm1_ctl.py step 3 03 10 'C-4+E-4+G-4 i03 fVOL40 fREV20'
python3 tools/fm1_ctl.py step 3 03 10 --    # empties the step
python3 tools/fm1_ctl.py cell 00 3 03       # track 3 plays block 03 in phrase 00
python3 tools/fm1_ctl.py cell 00 3 --       # and now it plays nothing
python3 tools/fm1_ctl.py length 3 03 32
python3 tools/fm1_ctl.py play 00            # or loop 00, or stop
python3 tools/fm1_ctl.py save
```

**Tracks are 1 to 8 here**, as they are printed on the screens — not
`t0`–`t7` as in the file. Phrases, blocks, rows, instruments and effect
values are hex, as on the screens and in a `.trk`; `length`'s steps and a
knob's turn are decimal.

A step is written exactly as a `.trk` line writes it, minus the row:
notes joined by `+`, `OFF`, `i<hex>`, `f<EFFECT><hex>`. So
`step-fx.md` is the reference here too.

`open` and `new` refuse to drop unsaved changes unless `--force` is
given. A whole song goes over with `tools/fm1_send.py`, then `open`.

## Taking the screen over

The machine can show a **canvas** the computer owns (`app/canvas.c`): no
firmware is built for it, the script draws and the machine shows it.

```bash
python3 tools/fm1_ctl.py canvas clear black
python3 tools/fm1_ctl.py canvas fill 20 40 200 60 amber
python3 tools/fm1_ctl.py canvas text 12 80 'HELLO' --font 20 --colour cyan
python3 tools/fm1_ctl.py canvas line 0 120 239 120 grey
python3 tools/fm1_ctl.py canvas image picture.png     # 240x240, a row at a time
python3 tools/fm1_ctl.py canvas events                # what the hands did since the last look
```

The screen is 240×240. A colour is `#RRGGBB` or one of the theme's words:
`black white grey green cyan amber red`. The `y` of a text is its
**baseline**. The fonts are `20`, `14`, `12` (the condensed face) and
`mono` (8 px). A picture's pixels are drawn as they arrive and do not
survive a later `fill`, `text` or `line`.

`HOME` leaves the canvas and gives the screen back to the firmware;
`GLO` leaves it too unless the script keeps it.

`canvas events` is the other half: it reports the buttons pressed and
released, the keys down, up and held, and how far each knob was turned
since the last look. With that, a loop of draw-and-read is a whole
interface running on the panel while the program lives on the computer.
For anything beyond one-off commands, write a script against
`tools/fm1_proto.py` instead:

```python
from fm1_proto import Canvas

c = Canvas()             # the FM-1 on USB-MIDI; Canvas(udp=19780) for the simulator
c.keep_glo()             # GLO becomes the script's own "back"

def frame(ev):           # called once at the start, then whenever something happened
    c.clear("black")
    c.text(12, 30, f"K2 {ev.turns.get('K2', 0)}", "cyan")
    if "SEL" in ev.pressed:
        c.fill(0, 100, 240, 40, "amber")
    c.show()             # what was drawn goes over in as few messages as it can

c.run(frame)             # until the hand leaves the canvas
```

`tools/protos/` holds a dozen of these, each a screen that was tried on
the panel before it was built into the firmware.

## The LEDs

```bash
python3 tools/fm1_ctl.py led FX lit
python3 tools/fm1_ctl.py led C-4 breath A-4 mid
python3 tools/fm1_ctl.py led all off
python3 tools/fm1_ctl.py led all app          # give them back to the firmware
```

Any button or any key, by its name. Levels: `off` `glow` `mid` `lit`
`breath`, the percentages `50` `62` `75` `87` `100`, or `app` to hand
that LED back. `all` names every one of them.

## What this is good for

The firmware's own status line was written for this — *"one line of words
a person, or a model, reads as it is"*. Taken together the commands make
a few things possible that nothing else on this machine can do:

- **Judging a change without hands.** Press the buttons that get to a
  screen, take `screen -o shot.png`, and look at what the panel actually
  shows. This is how the screens of this firmware are checked.
- **Writing a passage and hearing it at once.** `step` it in, `loop` the
  phrase, read `status` for the playhead, fix the step, and nothing
  touches the flash until `save`.
- **Finding what is slow.** `status --profile --reset`, play the heaviest
  phrase, read it again.
- **Trying a screen that does not exist yet.** The canvas, a script, and
  the real panel with the real buttons under real thumbs.
