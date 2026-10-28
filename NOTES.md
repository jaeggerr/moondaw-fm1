# Notes

Decisions and traps, newest section last. The FM-1 arrived on 28 October
2026; everything before that date was written without it.

## Rules

- Never flash before the stock flash is backed up and a recovery path
  (Seeed XIAO RP2040 + FM-1-transporter) is on the desk.
- Never write flash outside `FL_RANGE_OK` (`hal/fm1_flash.h`), never touch
  the stock firmware's VM backup at 0xE8000 and above.

## 2026-10-10 — portability audit of the existing tracker (moondaw)

- It does not port; its ideas do. `sizeof(Song)` is 588 KB against 512 KB of
  SRAM, the undo history 9.4 MB, the binary's .bss 23 MB, the font atlas
  1.9 MB against ~568 KB of XIP.
- Its model is arrangement → sections → per-track numbered patterns with 3
  FX per step. There are no chains, phrases or LSDJ tables in it (tables
  were ruled out on purpose).
- The audio path is float from the vtable down (`render(void*, float*, int)`),
  with `exp2f` on every pitch change inside the audio thread.
- Carries over as is: the vtable's shape, the tick counted inside the audio
  callback, the UI→audio command ring, the 11-byte step, the FX mnemonics,
  the pulse / LFSR / 4-bit wave algorithms.

## 2026-10-10 — the Felucca extraction

- **Felucca is one compilation unit.** The drivers have no `#include` and
  no header: each uses whatever the files included before it define.
  "Cutting dependencies" meant rewriting the entry file, not unlinking.
  `firmware/src/firmware.c` is that file here.
- **The FELUCCA_* macro names stay** (`FELUCCA_OTA`, `FELUCCA_CDC`,
  `FELUCCA_ID`, `FELUCCA_LOADER`…): `usb.c`, `ota.c` and the loader read
  them, and those files are verbatim.
- **The MIDI IN ring must be drained.** `usb.c` NAKs EP1 while fewer than
  24 slots are free, and the update's SysEx arrives on EP1. A firmware
  that ignores MIDI and never empties the ring locks itself out of
  updates. The main loop does `mi_r = mi_w` every frame.
- **Three ways back in are kept from Felucca**, all in `firmware.c`: the
  M-UPGRADE entry over USB, OCT- + OCT+ held 5 s → UBOOT, and the boot
  guard (two boots that die within 30 s → UBOOT). Do not remove any of
  them while changing the main loop.
- **MASTER is not a hardware volume.** It is a pot on an ADC; the app has
  to apply it. `PlatInput.master` carries it, 0..1023.
- **The LCD blit is a DMA out of the caller's buffer**, which must be in
  RAM (not XIP) and untouched until the transfer ends. `plat_blit` waits
  for it (`lcd_sync`). Pixels are sent high byte first: `PLAT_PX` swaps
  when the colour is built, not when it is drawn.
- **PA2 is both the backlight and the key matrix's /OE** (lcd.c): turning
  the backlight off blinds every key, button and encoder.
- **No framebuffer on the device.** The screen is 30x30 text cells
  (`app/term.c`, 2.7 KB) and only changed row spans are sent. A full
  240x240 RGB565 buffer would be 115 KB.
- The panel table (`PANEL_DEFAULT`) is Felucca's measurement. Its
  calibration screen and the flash copy were not taken.
- Sample rate is 44117.6 Hz on the device, 44100 everywhere else: 0.7
  cent sharp.
- Output is Q15 shifted left 7 into 24 bits: a -6 dBFS ceiling, as Felucca.
- Package identity is `FM-1_900`, Felucca's own development identity, and
  the USB strings still say Hügelton / Felucca (usb.c is verbatim). Both
  to be decided before anything is published.
- **Not found** in Felucca or SLOOP (isod89/sloop-fm1): the CPU clock.
  Neither sets it; both run on what the boot loader leaves. Timing comes
  from TIMER4 on the 24 MHz crystal. On hardware, `felucca_dbg.last_us`
  (time of one audio half) is the way to measure what we really have.
- Not found either: whether pi32v2 has an FPU. Felucca uses no float at
  all; nothing here does.

## 2026-10-10 — build

- Toolchain: `~/.jieli/toolchain` (jieli-linux-toolchains-20260730.1,
  clang 4.0.1). SDK: `~/fw-AC79_AIoT_SDK` at tag
  AC79NN_SDK_V1.2.1_2023-12-13 (1 GB; three files of it are used).
- Docker here is colima, and it has to be started first (`colima start`).
- Under colima's amd64 emulation one objdump out of six parallel ones
  died once with no output, leaving a `core` file in the tree (git-ignored).
  `tools/build.py` retries a silent failure once.
- First build: app 17,920 B, .data+.bss 12,720 B of 73,728, pool 2,688 B
  of 368,640, package 610,086 B.
- `app.ld` is still Felucca's split (72 KB RAM / 360 KB pool). Recut it
  when the tracker's real tables exist, not before.
- emcc is not installed; `platform/web/build.sh` falls back to
  `zig cc -target wasm32-freestanding`, which works because nothing above
  `platform.h` needs a libc.

## 2026-10-10 — what is verified, and how

- Firmware: compiles, links, passes build.py's checks (entry stub, no
  call out of `.ram_text`, no ROM reference, register access in `hal/`
  only), and `fm1_install.py`'s parser reads the package back. **Never
  run.**
- SDL (`make sim`) and web (`make web`): `make test` holds A4 on both and
  gets the same 5509 lit pixels, the key's LED, and 440 Hz. The web page
  was opened in a browser: screen, keys, buttons, encoder and LEDs answer.

## 2026-10-10 — the model is moondaw's own

Decided by Jaeger: the FM-1 runs MOONDAW as written — arrangement,
sections, per-track numbered patterns, 3 FX per step, the same FX
mnemonics, no tables — with whatever limits the machine forces. The
song/chain/phrase/LSDJ wording of the first brief was a pasted prompt
and is void.

Measured on moondaw, to size those limits:

- 95 % of `Song` is the pattern grid: 16 tracks x 16 patterns x 2178 B =
  557 KB, of which a third is the VOX syllable pool. Everything else
  (instruments 13.8 KB, arrangement 6.7 KB, sections, track init) is
  under 25 KB.
- What the 13 songs in `songs/` really use: 8 tracks at most, 39
  patterns at most in one song, patterns up to 128 steps, 22 arrangement
  rows at most.
- Their instruments: WAVE 32, NOISE 21, SMPL 21, PULSE 19, VOX 7,
  PLUCK 1. So a synth-only FM-1 cannot play most of them unchanged.

## 2026-10-10 — limits agreed, and the file stays a .trk

Agreed by Jaeger: 8 tracks; a shared pool of about 6000 steps instead of
the fixed grid; undo as a journal of edits (not written yet); PULSE,
NOISE and WAVE first, SMPL / PLUCK later, VOX probably never (typing
syllables on a piano keyboard is the opposite of a tracker's flow —
although Felucca runs a 4-voice formant engine on this chip, so the
hardware is not the obstacle).

- **The FM-1 stores the .trk text itself.** His songs are 8 to 28 KB;
  292 KB of flash holds ten to thirty. A dump of the step pool would be
  66 KB a song. What had to change was the reader, not the format:
  `core/trk.c` reads and writes from memory with no libc.
- Version 3 only. Older files go through moondaw once (nobody else has
  the app; no compatibility shim). `patch` files are refused for now.
- **What is not carried is counted, not hidden**: `TrkReport` says how
  many sample, zone, reference and preview lines, syllables, `fSMP`
  and drive / amp / tone / tract / accent fields a file had. A save from
  the FM-1 loses them. Whether to keep those lines verbatim is open.
- An instrument on SMPL, VOX or PLUCK keeps its engine NAME (so the file
  round-trips) and is silent. moondaw's own rule for an unknown engine
  is "fall back to the pulse"; a drum kit on a pulse is worse than
  silence.
- The step pool is chunks of 16 steps, taken when a step is first
  written. tremolo-chip, the largest song, uses 212 of 375.
- `sizeof(Song)` is 77 KB. Loading goes through a second one (a
  malformed file must leave the song in memory intact), so 155 KB of the
  360 KB pool are the song. An undo history has to fit in what is left.
- moondaw truncates an instrument name to 7 characters on load although
  the files carry 8 (`CHIPBASS` → `CHIPBAS`). Same here, for the same
  files to come out the same.

## 2026-10-10 — the engine in fixed point

- **One band-limited buffer per track, not per voice.** moondaw gives
  each of a track's 20 voices three engines with a blip_buf each. The
  synthesis is linear, so all the voices of a track add their steps into
  one buffer: same sound, 600 bytes a track. The buffer holds
  sample << 10 instead of blip_buf's << 15, the five bits being the
  headroom for the sum.
- **The clock ratio is exact**: 4 MHz against 44100 Hz is 40000 clocks
  for 441 samples. The first version rounded it to Q20 and the pitch
  drifted 1.7 samples a second against moondaw.
- **A layer's gain is capped at +12 dB** (moondaw: +24): the steps of a
  whole track share 32 bits.
- **The noise register shifts 32 times per cycle of the written note**
  — that is the "five octaves above". Transposing the note AND dividing
  by 32 plays it 32 times too fast, straight into the top clamp.
- **A sweep goes below the lowest octave**: `fSWP` on a kick walks the
  pitch four octaves under note 0 before the 1 Hz floor. Clamping at
  note 0 changed every kick.
- **Finished voices are stopped once a tick, not once a block.** A noise
  register keeps shifting until its voice stops, so when that happens
  decides the hiss of the next hit. In moondaw it depends on the audio
  buffer size; a tick does not.
- Curves that need a libm (periods a cent apart, dB, tan for the filter,
  the crush tables, the wave shapes) are generated once by
  `tools/gen_tables.c` into `core/tables.h`, 33 KB of source.
- 64-bit multiplies are used freely (Felucca does: Q30 formant
  coefficients); the JieLi clang inlines them — the firmware links with
  no helper call. No 64-bit division by a variable anywhere.
- Not ported: the reverb and the delay (`rev` / `ech` / `fREV` / `fECH`
  are read and held, nothing is sent), the master limiter (the sum is
  hard-clipped), the metronome, the loop range, the instrument preview,
  patches, undo.

### How it is verified

`tests/difftest.sh` reduces each song to what the FM-1 carries, removes
the two sends, and renders the SAME file through moondaw's own engine
(linked from its build directory, 44.1 kHz, no limiter) and through
`core/`. Track by track, 30 s:

- level identical to 0.1 dB on every track of every song;
- short-time spectra within -28 dB on the worst track (KIDS t2), -33 to
  -51 dB on the others; several songs match sample for sample at -40 dB.

Why track by track, and spectra: free-running oscillators cannot be
compared sample for sample over a song (one clock in a period and two
identical sounds are out of phase ten seconds later — a vibrato does it
in two), and two tracks whose phases have drifted interfere differently
when summed, which the spectrum of the mix reports although neither is
wrong.

AMPTEST, piano-velocity and song03 have nothing the FM-1 can play.

### On the device

- Firmware with the core: image 52,328 B; .data+.bss 16,816 B of 73,728;
  pool 170,592 B of 368,640. `MOONDAW_APP=hello ./build.sh` is the
  bring-up build, without the core.
- **The CPU cost is unknown and is the first thing to measure** on
  hardware (`felucca_dbg.last_us`, one audio half). The expensive parts:
  a 16-tap kernel per oscillator edge (a wave voice makes 32 edges a
  cycle), and four 64-bit multiplies a sample in each filtered track.
  If it does not fit: fewer voices per track, the filter at half rate,
  a cheaper kernel for the wave engine.
- `app/app.c` is a player screen, NOT the interface: PLAY, keys 1-8 mute,
  SELECT picks the phrase. It plays a built-in demo (`app/demo_trk.h`),
  since nothing is stored yet.

## 2026-10-10 — the interface: not a character grid

- **The 8x8 cell grid of `app/term.c` is not the interface.** "30x30
  characters" came from the pasted brief. The panel is 240x240 RGB565 and
  Felucca paints antialiased type on it; a constraint of my first code is
  not a constraint of the machine. The same mistake was made once on the
  console (moondaw, 24/08: "on dirait toujours un truc en DOS moche").
  The design language is the iOS app's — slabs, nine slots a step, cyan
  where one is, green the cursor, the fixed palette — remeasured.
- `proto/pattern.html` is the PATTERN mockup (three states and the panel
  map), proposed, **not validated**. Its notes carry what is open. When
  he validates something, the proto and its notes are updated first.
- Its proposal in one line: the left hand moves (SELECT = step,
  ALGORITHM = field, PRESETS = block), the right hand writes (KNOB 1-4 =
  the four cards of the bottom bar), the keys write notes, and the twelve
  buttons are redrawn on screen at their physical place because four of
  them do not do what is printed on them.
- **Validated by Jaeger, 10/10**: the left hand moves, the right hand
  writes; and the four cards sit at the TOP, under the nav bar, level
  with the knobs they name. The twelve buttons too, same day: FX LOOP
  INSTR CLEAR SELECT MIXER / ‹PHR SAVE UNDO SONG PLAY REC, redrawn on
  screen at their place. Open: what REC does, where REDO lives.
- Felucca's panel photo (`docs/controls.jpg`) is where the physical
  layout was read from: MASTER / SELECT / PRESETS / ALGORITHM left of the
  screen, KNOB 1-4 right of it, two rows of six buttons under those,
  OCT-/OCT+ under the left knobs. The encoders have no push switch.
- **Not found**: the diagonal of the panel. It decides the row height.
- Engineering prerequisite if the design is kept: baked antialiased
  fonts and a strip renderer in the firmware, as Felucca does
  (`gen_aa_font.py`, `gfx.c` — not taken yet).

## 2026-10-10 — PATTERN written

- `app/gfx.c`: no framebuffer. A screen is a function that draws
  everything; it is called once per band of 16 lines, clipped to it, and
  a band goes down the SPI only when its hash changed. 7.7 KB instead of
  115. The cost is that the draw code runs 15 times a frame — unmeasured
  on the chip, like everything else.
- Type is baked by `tools/gen_fonts.py` (Pillow) into `app/fonts.h`, 4
  bits of coverage a pixel: Barlow Condensed 12/11/9/8 and JetBrains Mono
  7/6, both OFL (`assets/fonts/`, `LICENSES/`). The header is committed.
  **Mono at 6 px is at the edge of legible** even on a desk screen; the
  panel's diagonal will decide whether it stays.
- `app/pattern.c` follows `proto/pattern.html` measure for measure.
  Written: the cursor (SELECT, ALGORITHM), PRESETS through the track's
  blocks, the four cards (note/octave, instrument, effect/value, the two
  nibbles of a pair, LEN on the step number), the keys and chords,
  OCT-/OCT+, CLEAR, FX, PLAY (the phrase, looped), HOME. Emptying is
  turning all the way down: OFF then `---` under the lowest note.
- **Not written, and drawn dim for that reason**: LOOP, INSTR, SELECT,
  MIXER, SAVE, UNDO, SONG, REC. Also missing against the mockup: the
  effect list that opens beside the slot, the RMP span in the right
  margin, hearing a note as it is written (the core has no audition).
- The way in is provisional: EDIT on the player screen, on the phrase
  and track picked there. SONG and PHRASE will replace it.
- An `int8_t` holds a frame's worth of detents: a test that turned a
  knob by 200 turned it by -56.
- Both simulators now lay the panel out as on the machine, and the
  arrows are SELECT and ALGORITHM. `tests/pattern_test.c` drives the
  screen with no display and reads the song back.
- Simulator keys carry no function key (macOS keeps F11 and wants fn for
  the rest): 1-6 and Q-Y are the two rows of six buttons, A and ' the
  OCT pair, space is PLAY too; the notes moved to the bottom rows and
  to 7 U I 9 O 0 P [ = ]. By scancode, so AZERTY is the same hands.
- **The simulator cannot say how the type looks.** Jaeger's first word
  on PATTERN in the SDL window was "très flou". Part of it was macOS
  stretching a low-density window (fixed: high-DPI window, nearest
  scaling); the rest is real — antialiased glyphs of 6 to 12 pixels
  shown at twice their size are soft by nature. On the panel a pixel is
  about a tenth of a millimetre. Judge type on the device, nowhere else.
- **Small type: one-bit, not smoothed.** Tried side by side at panel
  resolution: JetBrains Mono antialiased at 6 and 7 px is fog (LEN read
  as LBI); the same face hinted and rendered in whole pixels at 8 px is
  crisp. Barlow Condensed is the opposite, clogged without smoothing.
  So `gen_fonts.py` bakes the mono face in mode "1" at 8 px and the
  condensed one smoothed, nothing under 8 px mono or 9 px condensed.
  The button words went from 8 px semibold to 9 px bold for the same
  reason. A browser always smooths, so the proto cannot show this.
- Firmware with it: image 71,144 B, .data+.bss 24,688 B of 73,728.

## 2026-10-10 — reverb, delay, flash store

Agreed order while the device is away: what can be MEASURED now (sends,
storage), then the arrival checklist. Everything judged by eye or by
hand waits for the machine — PATTERN's polish stopped there.

- **Reverb and delay** (`core/fx.c`): moondaw's, in 16-bit lines held at
  half scale (a bus is a sum and goes past full scale; a clipped
  feedback loop is heard for seconds). Multiplies round TOWARD ZERO: a
  line that rounds down settles on -1 and its tail never dies, so the
  "empty room" test never fires.
- **The delay is capped at 743 ms** (32768 frames a line). moondaw's
  holds twelve seconds in four megabytes. `delay 18` at 120 bpm is 375
  ms; below 61 bpm in `ticks 6` it would be shortened.
- **The delay's lines and the load's working copy are the same 128 KB**
  (`song_scratch`). They never overlap: a load happens with the audio
  silent and is followed by `player_init`, which clears the delay.
  Pool: 250 KB of 360 used.
- Verified: on a song without vibrato, reverb, delay and both together
  match moondaw sample for sample at -38 to -45 dB. `make difftest` now
  leaves the sends in: nine songs at -28 dB or better per track, and
  KIDS track 3 at -20 — free-phase vibrato under a heavy delay, where a
  note beats against its own repeats and the beat's phase is not the
  same in two engines. The threshold is -20 for that one case.
- **The reader and the writer are streams** (`trk_begin/feed/end`,
  `trk_write` to a sink): a file is read and written a sector at a time
  and is never whole in RAM.
- **The store** (`core/store.c`): a file is a run of 4 KB sectors, each
  with header and CRC; a save writes a new generation beside the old one
  and commits it by programming four bytes (a count and its complement)
  into a sector already written — flash clears bits without an erase.
  Tested against a NOR that only clears bits, with the power cut at
  every operation of a save: the old file every time, never a broken
  one. Two traps found on rereading, both fixed: a cut save leaves
  sectors of generation N+1, and the next save must not reuse N+1;
  deleting must erase EVERY generation of the slot or an older one comes
  back as the file.
- One song lives in the machine for now (slot 0): SAVE in PATTERN writes
  it, boot reads it, the built-in demo is the fallback. SAVE stops the
  song first — an erase holds the interrupts for ~50 ms on the device.
  The simulators keep the flash in `moondaw-fm1.flash` (SDL, current
  directory) and in the browser's localStorage (web).
- On the FM-1 the store is 0x97000..0xE0000, Felucca's main data area,
  bounded twice: in `firmware.c` and again by `FL_STORE_OK` in
  `storage_hw.c`. Nothing is touched if the flash chip's ID is not the
  expected one. **Installing MOONDAW over Felucca would read Felucca's
  projects as garbage and overwrite them sector by sector** — the two
  stores share the area and not the format.
- `plat_audio_load()` puts the audio interrupt's load on the player
  screen and on the bring-up screen (CPU, MAX): the unknown number,
  readable without dumping RAM.
- `docs/ARRIVEE.md` is the arrival-day checklist (in French, like the
  rest of docs/). Its one opinion: install Felucca first, to prove the
  way in and the way back with a firmware known to work.

## 2026-10-10 — the recovery tool is built

FM-1-transporter at `a632d92` compiles on this Mac into
`~/Documents/Code/FM-1-transporter/build/fm1_transporter.uf2`, with
`wl82loader.bin` (24,064 B, from kagaimiq/jl-uboot-tool, MIT) embedded.
Installed for it, outside any repository: pico-sdk 2.2.0 in
`~/pico-sdk`, Arm's GCC 13.3 tarball in `~/.fm1/arm` (no system
install), jl-uboot-tool in `~/.fm1`. The SDK builds its own picotool;
none is needed to load the UF2 (BOOT button, copy to `RPI-RP2`).
**Not done and not testable without hardware**: the XIAO itself, the
wiring, and whether the tool talks to this FM-1.

## 2026-10-10 — MENU proposed

Jaeger on the player screen: "pourquoi l'écran d'accueil est moche ?" It
is the provisional 8x8 screen from before the design direction was
known; it goes. `proto/menu.html` is the screen that replaces it, and
the list of songs the store already knows how to keep (32 slots) but
nothing showed. **Proposed, not validated**: one column (the song in
memory, the songs in flash, + NEW), SELECT walks it, OPEN on the EDIT
button, DELETE where PATTERN has CLEAR and guarded by a second press,
PLAY to hear without opening, no cards. Open: renaming (no letters on
this machine), getting a song in from a computer, where OPEN leads until
SONG exists.

## 2026-10-10 — MENU written

Validated as drawn, then written (`app/menu.c`, `app/app.c` rewritten
around it; the 8x8 player screen is gone, `term.c` stays for the crash
and update screens and the bring-up app).

- One song in memory, up to 31 in flash. `app_slot` is its place, -1
  while it has none; `app_dirty` is set by every write in PATTERN and
  asked about when another song would replace it (SAVE / DISCARD on the
  button map). Nothing saves itself.
- **Slot 31 holds one byte: which song was open**, so the machine comes
  back on it. It is rewritten only when it would change.
- The list is read from flash when the screen opens and after each
  change, never while drawing: a frame is drawn fifteen times.
- **PLAY is lit on the song in memory only.** Hearing a song of the list
  without opening it needs a second Song in RAM; the scratch buffer that
  could hold one is the delay's memory.
- A file handed over by the host (command line, web page) becomes the
  song in memory, takes the slot of the song of the same name if there
  is one, and is not written until SAVE.
- **Regression to repair with the SONG screen**: the count of what a
  file had that the FM-1 does not carry (samples, syllables...) was on
  the old screen and is shown nowhere now. A save still loses them.
- PATTERN's back label still says `‹ PHR` and goes to the MENU.
- A new song has one block, on track 1: there is no way to reach another
  track until PHRASE exists.
- `tests/menu_test.c` walks all of it on a simulated flash.

## 2026-10-10 — SONG proposed

`proto/song.html`, **not validated**. One row a phrase, the eight tracks
side by side, the block number in each cell. SELECT the phrase,
ALGORITHM the track; all the way left is the phrase's number (the
buttons then speak of the phrase, KNOB 1 is its length), all the way up
is the song's own line (the four cards become BPM, SWING, TICKS,
TRACKS). KNOB 1 on a cell walks the track's blocks, then ON, then empty;
KNOB 2 is the cell's level. The buttons keep PATTERN's places; SEQ
becomes `+ PHRASE`. What the FM-1 does not carry is written in the bar,
in amber — the information the old screen gave.

None of the 13 songs has a section, a title, a reference, a cell level
or an ON cell (checked). Nothing real to draw, so none is drawn: the
proto says so and proposes to DISPLAY sections and references as the app
does and not to edit them here at first.

## 2026-10-10 — SONG written

Validated as drawn, then written (`app/songview.c`). The road is now
MENU -> SONG -> PATTERN, and a block is opened from its cell, so every
track of every phrase is reachable.

- `app/ui.c` is where the cards and the button map are drawn, once, for
  the three screens: a button is NONE, OFF, ON, BACK, LIVE or ASK.
- Edits on SONG: KNOB 1 on a cell walks empty / ON / the track's blocks,
  up to one past the last that holds something (that is how a block is
  made); KNOB 2 the cell's level; KNOB 1 on a phrase's number its length
  (up from AUTO starts at the phrase's own cycle); `+ PHRASE` copies the
  phrase below itself without its name; DELETE with a second press.
- **References are rows, so inserting or deleting a phrase renumbers
  their targets** (`shift_refs`). A reference whose phrase is deleted
  becomes an empty phrase — FORMAT.md says such references are deleted
  with it and the screen says how many first; simplified here, and no
  song has a reference yet.
- Sections, titles and references are displayed minimally (a reference
  shows `REF nn`) and not edited. Titles are not drawn at all.
- PATTERN's back now says `‹ SONG` with the phrase beside it; the SONG
  button leads there too.
- The count of what the file had that the FM-1 does not carry is back,
  in SONG's bar.
- `tests/song_test.c` drives the screen with no display.

## 2026-10-10 — INSTRUMENT proposed

`proto/instrument.html`, **not validated**. The settings in rows of four
because the right hand has four knobs: SELECT picks a row and the four
cards become its four settings; each cell sits under the card, so under
the knob, that sets it. Rows: engine / timbre / gain / level; attack /
decay / sustain / release; cutoff / resonance / env / vel; vibphase;
then the four layers, one row each (a layer's engine and timbre are one
knob: PULSE 25%, WAVE SAW...). The envelope is drawn above. The keys
play the instrument — which needs the audition the core does not have.
Reached from PATTERN's INSTR button; PRESETS steps to the next
instrument. Open: the name (letters again), a sound library, what to
show for an instrument on SMPL / VOX / PLUCK.

## 2026-10-10 — INSTRUMENT written, and the audition

Validated as drawn, then written (`app/instrument.c`), reached from
PATTERN's INSTR button on the instrument in force at the cursor.

- **The audition** (`player_note_on` / `player_note_off`): a ninth track
  no phrase plays, dry, one note and its layers. INSTRUMENT's keys use
  it, and PATTERN now sounds the note a key just wrote.
- **That track is put away clean when its envelope closes.** A song's
  track keeps being rendered between notes, so its DC block and filter
  settle; the audition stops being rendered, and whatever they still
  held came out under the next key.
- **Two leaks that never reached zero**, found by a test that expected
  silence: the band-limited buffer's high-pass as blip_buf writes it
  (steps of 64) leaves up to 31 of DC standing after a note, and the DC
  block's `y1 >> 10` stalls under 1024 for a positive value — 64 steps
  of DC behind every note. Both now come back to rest. `make difftest`
  is unchanged by it.
- **Two undefined shifts** of negative values (`<<` on the mix), caught
  by the sanitizer the first time a test rendered audio under it. Now
  multiplications. The render tests should all run under the sanitizer.
- A layer's engine and timbre are one knob (12 positions, 0 = no layer).
  Layers are a list: SELECT goes down to the first free row and no
  further; turning KNOB 1 all the way down removes a layer and the ones
  under it move up.
- Not as drawn: the cards show the file's numbers, not yet milliseconds
  or percent.
- `gfx_line` exists for the envelope.

## 2026-10-10 — MIXER proposed

`proto/mixer.html`, validated the same day. A track has four things to say
about its place in the mix — volume, pan, reverb send, delay send — and
the right hand has four knobs: one ROW per track (not a vertical strip),
SELECT picks it. The first row is the two returns: ROOM, DAMP, DELAY,
FB. MUTE and SOLO act on the track under the cursor (the question left
open on SONG). Open: level meters and the metronome (neither is in the
engine), whether MASTER shows anywhere.

## 2026-10-10 — MIXER written

Validated as drawn; `app/mixer.c`, `tests/mixer_test.c`.

- Reached from SONG and from PATTERN (GLO); HOME goes back to whichever
  it came from, and the back label says so.
- Row -1 is the returns (ROOM, DAMP, DELAY 0..96, FB); rows 0..n-1 are
  the tracks (volume, pan, reverb send, delay send). MUTE and SOLO are
  dark on the returns.
- SOLO lives in the player (`solo_mask`), not in the song: it silences
  the others without muting them, so the mutes come back as they were.
  Nothing of it is saved.
- Trap: pan is -64..63, as in the file. Hard right leaves 1/64 of the
  track on the left; a test that expects an exact zero there fails.
  The test compares the two channels by ratio instead.
- Not there: UNDO, level meters, metronome.

## 2026-10-10 — PHRASE proposed

`proto/phrase.html`, validated the same day. One row a step, one column a
track, the cell is the step's first note. SELECT the step, ALGORITHM the
track, PRESETS the phrase; all the way up is the column head (track and
block number). Keys write N1; KNOB 1 semitone, KNOB 2 octave. OPEN goes
to PATTERN on that track and step. MUTE where MIXER has it; no SOLO here
(its MIXER place is CLEAR's).

- The app stacks N2..N4 small beside N1. No room at 8 px in a 23 px
  cell: one cyan dot per extra note instead.
- A step with no note shows its first FX in amber, as the app does.
- Open: the phrase's name (shown, not edited), selection.

## 2026-10-10 — PHRASE written

Validated as drawn; `app/phrase.c`, `tests/phrase_test.c`. Every screen
of the road now exists: MENU -> SONG -> PHRASE -> PATTERN -> INSTRUMENT,
and MIXER from SONG, PHRASE and PATTERN.

- `app/edit.c` is new: writing a note, the semitone and octave knobs,
  clearing a step, the instrument in force on a row. PATTERN and PHRASE
  call the same code.
- PATTERN knows where it was opened from: HOME returns there
  (`PATTERN_BACK`, label `‹ PHR` or `‹ SONG`), its SONG button always
  goes to SONG (`PATTERN_SONG`).
- A block shorter than the phrase repeats in the grid; writing on row r
  writes step `r % length` of the block.
- A track the phrase leaves out draws nothing and ALGORITHM steps over
  it. A reference is not opened and PRESETS steps over it.
- The page follows the playhead a page at a time until SELECT turns;
  the next PLAY gives it back.
- Trap (tests): `*_resume()` treats everything held as not a press, so a
  press sent in the very next frame is swallowed. Send one idle frame.
- Not as drawn: `PRESETS PHR` (room for `OCT F3-G5`), MUTE lit is red as
  on MIXER.

## 2026-10-10 — UNDO written

`app/undo.c`, `tests/undo_test.c`. Lit on PATTERN, SONG, PHRASE, MIXER
and INSTRUMENT (added there, at its usual place: it was not on that
mockup). Dark when there is nothing to take back.

- A journal of the bytes an edit is about to replace, not copies of the
  song: 12 KB of data and 256 records, in .bss (RAM 48.5 KB of 72).
- One press takes back one GESTURE: what was written in one frame, plus
  what went on being written at the same place within a second. A knob
  turned twelve notches is one press, two edits a pause apart are two.
- A gesture that changed nothing (a knob against its stop) is dropped.
- When the journal is full the oldest gestures are forgotten; opening
  another song empties it. SAVE does not.
- **Trap: a step is recorded by (track, block, row), never by address.**
  `song_pool_compact` hands back emptied chunks; an address kept from
  before would write into a chunk that belongs to something else.
- Deleting or inserting a phrase records the arrangement as it was
  (rows, references and section names move together): 36 B a phrase.
- An undo can be older than the screen it is pressed on. PATTERN
  re-reads its block afterwards and leaves for SONG if the cell no
  longer names one; the others clamp their cursor.
- No redo: no button for it, still open.

## 2026-10-10 — LOOP proposed

`proto/loop.html`, validated the same day. The app has a switch and a
START / END bar one taps rows into; here one button does it, where the
cursor is: first press sets the start (the button then says END), second
sets the end, third forgets both. Shown by a cyan gutter and step
numbers, and a `LOOP 03-08` slot in the bar. One loop for PATTERN and
PHRASE of the same phrase, in phrase steps; gone on another phrase or
back on SONG. Never saved, not in UNDO. On SONG the button stays dark:
there it means something else in the app.

## 2026-10-10 — LOOP written

Validated as drawn. `app/loop.c` holds the three presses and whose
phrase the loop is; `player_set_loop(from, len)` keeps a phrase played
by `player_play_entry` to those steps. `tests/loop_test.c` listens to
which steps the player is seen on.

- The range reaches the audio side as ONE word (from | len << 16), so
  an interrupt never sees half of it.
- Song mode ignores it: MIXER and SONG play the song. INSTRUMENT's PLAY
  plays the phrase, so the loop.
- A loop past the end of a phrase that got shorter is ignored, not
  clamped to nothing.
- `UI_BTN_HERE` is new: a cyan button, a state that is on or waits for
  its other half.
- Trap (again): a press in the frame right after a screen opens is
  swallowed. The capture walk in `platform/sdl/main.c` needs an idle
  frame before each press.
- LOOP on SONG stays dark: in the app it means "loop this phrase rather
  than follow the song", another mockup.

## 2026-10-10 — SELECT proposed

`proto/select.html`, validated the same day. On PATTERN only. SELECT (the
EDIT button) is a mode: the cell under the cursor becomes the fixed
corner, SELECT and ALGORITHM carry the other one as they carry the
cursor, what lies between is taken (amber). While it is on, LOOP and
INSTR read COPY and PASTE, CLEAR empties the rectangle, the four knobs
and the keys write nothing. COPY keeps the rectangle (COPY then CLEAR is
a cut); PASTE lands at the cursor and ends the mode. Pasting follows the
app: only the copied fields are written, nothing wraps past the end of
the block, a conflicting fx is refused, an fx copied alone lands in the
fx slot under the cursor. Open: PHRASE (its EDIT button is OPEN),
transposing a selection.

## 2026-10-10 — SELECT written

Validated as drawn. The mode is PATTERN's (`selecting`, the anchor);
the clipboard is in `app/edit.c` (`edit_clip_copy`, `edit_clip_paste`,
`edit_erase_range`), one buffer of 128 steps and a mask of fields, so
PHRASE can use it later. `tests/select_test.c`.

- In the mode the frame works on a copy of the inputs with the four
  knobs and the keys zeroed, and the buttons that changed meaning are
  taken out before the ordinary code sees them.
- CLEAR and PASTE end the mode, COPY does not, PRESETS does.
- Paste and erase go through the same `step_w` as a note, so one frame
  is one UNDO.
- Not said on screen: rows lost past the end of the block, an fx
  refused. The app has a status line for that; this screen has none.
- **A bug found by this test, made in the PHRASE commit**: moving the
  note code to `app/edit.c` left `clear_slot` with an empty branch, and
  CLEAR on a note in PATTERN did nothing. `pattern_test` only pressed
  CLEAR on an fx and on a step number. Fixed, and checked here.

## 2026-10-10 — Songs from the computer

`app/link.c` on the device, `tools/fm1_send.py` on the Mac, the protocol
in `app/link.h`. USB-MIDI SysEx, the pipe Felucca's updater uses, under
another signature (`F0 7D 4D 44 ...`).

- **What arrives goes straight to the store, never to memory and never
  parsed on the device.** A song is up to 25 KB and there is no RAM for
  a second one; parsing needs the delay's memory, which would stop the
  sound. So the host reduces the file first with the firmware's own
  reader and writer (`tests/trkdump.c`) and sends that.
- Stop and wait, one message and its answer: the firmware holds a single
  SysEx at a time. 240 bytes of song a message, 8 bits packed in 7.
- The new file is the file only at END, when length and crc32 are right
  (the store's own commit). A cable pulled halfway, a damaged byte, five
  seconds of silence: the flash is as it was. `tests/link_test.c`.
- Accepted on the MENU only, and the screen says RECEIVING while it
  lasts. Replacing needs `--replace`; replacing the OPEN song is refused
  while it has unsaved changes, and reloads it otherwise.
- **Trap, on the firmware side**: the updater's `ota_take` frees
  whatever SysEx it finds, ours included, and a message left in the slot
  blocks the updater's. `link_poll` copies ours out and frees the slot
  at once, before `ota_service`, in the main loop -- so M-UPGRADE, one
  of the ways back in, never waits on the app (the `hello` build has no
  link at all and must still update).
- `platform/udp_sysex.h`: the same three calls over UDP on 127.0.0.1,
  for the simulator and `tests/linktest.sh`, which sends KIDS with the
  real tool and compares what the store holds byte for byte.
- Not verified: the MIDI path itself (python-rtmidi, the port's name,
  the real throughput). The RECEIVING panel was written without a mockup
  and has not been looked at.
- The 13 songs take 100 KB of the 290 free; the largest 25.

## 2026-10-10 — Songs back to the computer

`tools/fm1_send.py --list` and `--get NAME [-o FILE]`; INFO and READ in
`app/link.c`.

- The host asks for a slot piece by piece, BY OFFSET: a piece whose
  answer was lost is asked again and the device still has it. The store
  reads forward only, so any other offset starts the slot over and
  skips to it.
- 120 bytes a piece, not 240: the answer then fits the firmware's SysEx
  queue (64 packets) without `ota_wire_send` having to wait for USB.
- From the MENU only, and the screen says SENDING: the store's read
  cursor is the link's while it lasts, an OPEN in the middle would
  move it.
- What comes back is the file as the device saved it, a version 3 .trk
  the app opens. `tests/linktest.sh` sends KIDS, lists, fetches it and
  compares the three byte for byte.

## 2026-10-10 — The list of effects

Frame 2 of `proto/pattern.html`, written in `app/pattern.c`
(`draw_pop`). It opens to the left of the fx slot when KNOB 1 turns on
it, stays 1.5 s after the last notch, and any other gesture closes it.
Six entries around the current one.

- `fx_desc()` in `core/song.c`: moondaw's own one-line descriptions,
  word for word (18 characters at most).
- Not as drawn: the descriptions are in the 8 px mono like everything
  else (the mockup had them at 6.6 px, below what is readable here), so
  the panel is 124 px wide, not 112.
- Drawn only; no test reads it.

## 2026-10-10 — REC proposed

`proto/rec.html`, **not validated**. What he settled for the app on
03/09 (one press: a four-beat count-in, the transport, the capture;
snap to the nearest step and nothing finer), on a machine with keys.
PATTERN first. REC red, `REC 4..1` in the bar with a click, then the
phrase loops (inside the LOOP range when there is one) and each key is
written at the nearest step; several keys on a step make a chord; a key
landing on a written step replaces its notes. REC again stops the take
and lets it play, STOP stops everything. One UNDO for the whole take.
Open, asked: whether releasing a key writes OFF (proposed: yes, on an
empty step, when no other key is held). Known limit: the keyboard
audition has one voice, a chord is written but only its last note heard
while recording. The player has no click yet.

## 2026-10-10 — What "validated" means in these notes

He said it himself at the end of the day: without the machine he has no
opinion, and his answers to the mockups were given without weighing
them. So every "validated" above dated 10/10 (PATTERN's layout aside,
which he did correct: cards on top, readable type, ordinary keys) means
"not objected to", not "chosen". Nothing here is settled about how the
screens FEEL; what is settled is what the tests prove.

To re-open with the FM-1 in hand, in the order they will be met: the
size of type and rows, the left hand / right hand split, the twelve
button words, MIXER rows, PHRASE's dots for chords, UNDO's one-second
gesture, LOOP's three presses, SELECT hiding LOOP and INSTR, the effect
list covering the notes, the RECEIVING panel.

REC stays a proposal (`proto/rec.html`), unwritten, with its question
about OFF unanswered. Not to be written before he has played the keys.

## 2026-10-28 — the machine is here

The day is in `docs/ARRIVEE.md`. What it changes here:

- **Everything below the interface works on hardware**: the panel table,
  LCD, keys, knobs, MASTER, audio, the store's link over USB-MIDI, the
  update path, and OCT- + OCT+ into the ROM UBOOT.
- **CPU is not a problem.** The demo playing: load under 1 % on average,
  MAX 0..7 % of a 2.9 ms half (under 200 µs). The fallbacks listed in
  "the engine in fixed point" are not needed; the meter itself is
  proved by MAX moving.
- **Recovery needs no XIAO**: felucca-bsp's `tools/fm1_rescue.py` talks
  to the ROM UBOOT (4C4A:8057) from the Mac. Stock V15 enters it on the
  USB-MIDI soft key `F0 22 24 35 7D F7`; our firmware on OCT- + OCT+ or
  the boot guard. A full flash backup was taken that way.
- **Felucca itself is no longer actively developed (16/10)**; its hardware
  layer lives on as felucca-bsp (MPL-2.0), the base most alternative
  firmwares now use. Ours is a hand copy of Felucca's drivers of 10/10.
- M-VAVE's FM1 Open (20/10) opens its app, not its engine (binary SDK,
  Windows build): not a base for MOONDAW, which has its own engine.

**The interface, seen on the panel: "horrible, trop petit", to be
rethought from A to Z.** Every screen above was drawn without the machine
and, as said on 10/10, "validated" meant "not objected to". None of the
layout decisions stand: type size, row height, what one screen shows,
the button words on screen, the four cards. What stands is what the
tests prove (core, store, link, undo, the edit code under the screens).

## 2026-10-28 — the type size, chosen on the panel

`MOONDAW_APP=type` (`app/type.c`): a ruler, then 17 pages of one face and
size each, filled with tracker rows. Jaeger, with the FM-1 at playing
distance: readable "sans trop d'effort" from 18, **read at a glance at
20 -- CONDENSED 20 is the size, whatever it costs in layout.**

- What it costs: **9 lines of 26 characters a screen**, against 15 to 20
  rows on the screens drawn on 10/10. Every screen is to be redesigned
  from that, from the start; nothing of the old layouts is kept.
- Barlow Condensed is proportional: columns of notes and numbers line up
  only if each field is placed at its own x, not by counting characters.
  Whether its figures are tabular is to be checked before relying on it.
- No mono face was offered at 18 or 20; if one is wanted for numbers, it
  is to be put on the panel at that size before being chosen.
- The 100 px square has not been measured yet (the panel's pixel pitch).

## 2026-10-28 — the new interface, one screen at a time

Jaeger's rule from now on: one screen at a time, few decisions put to
him, each judged on the panel (`MOONDAW_APP=mock`, `app/mock.c`). A
screen is settled when he has looked at it on the FM-1 and said so; that
is what "validated" means from here, unlike 10/10.

**HOME — validated on the panel, 28/10.** The projects by name only
(no tempo, no counts: they do not help to choose), one a row, CONDENSED
20 on rows of 28 px, eight to a screen; the open project marked by a
bar at its left, an amber dot after its name while it has unsaved
changes; `+ NEW` last; the cursor starts on the open project; a thin
scroll bar on the right only when the list is longer than a screen; the
firmware version small (CONDENSED 12) at the bottom. Names as the files
spell them.

**HOME written** (`app/menu.c`, same day), with what Jaeger added:

- **The button printed SEL was read SCL** from Felucca's photo; renamed
  everywhere (`PB_SEL`). **SEL selects**: on HOME it opens the project
  under the cursor and goes straight into it.
- **HOME leads home from every screen** (`app.c`, before any screen sees
  the press). The screens that used HOME as "back" (PATTERN to PHRASE,
  INSTRUMENT, MIXER) lose that way back until they are redesigned.
- Opening another project over unsaved changes puts a question in place
  of the list: SAVE keeps them, SEL loses them, anything else stays.
- A song in memory with no place in flash (the built-in DEMO, a new one)
  has its row too, among the others by name.
- Not on HOME any more: DELETE (it was not asked for), the flash usage,
  CPU/MAX.
- **PLAY plays the project under the cursor** (Jaeger, 28/10). One song
  fits in memory, so on another row PLAY opens it -- asking first over
  unsaved changes, "then plays" -- and the hand stays on HOME.
- Trap of the day: a build that crashes (clang under colima's emulation,
  again) leaves the previous `build/moondaw.fwsc` in place; installing
  without checking the build's exit status put the mockup back on the
  device.

## 2026-10-28 — colours, and SONG on block numbers

- **The palette is in `app/theme.h`**, every colour named by what it
  means; no screen writes a colour value (the type specimen aside).
- **Black background and pure white text**, chosen on the panel: the
  blue-black and the green-grey text of 10/10 made the type stand out
  less. The other colours keep their meaning: green the cursor, cyan
  what is open or plays, amber what is not saved, grey what is secondary.
- **SONG opens a project** (Jaeger: on opening he wants the structure,
  and to listen, not the last block edited). The cursor starts on the
  first phrase. One row a phrase, its number fixed at the left; **the
  block each track plays, `--` when silent** (chosen over one square a
  track: it says more), six tracks a screen with their numbers small
  above, **KNOB 1 scrolls the tracks sideways**. Phrase numbers in hex as
  in the files. Mocked up, not yet settled as a whole.
- **Cells on SONG** (Jaeger's proposal, mocked up): SELECT the row,
  **KNOB 1 a cell cursor** (the cell hollowed out of the green row, the
  grid scrolling sideways to follow), **KNOB 2 the cell's block** (down
  to `--`, up to one new block past the track's last). **SEL on a cell
  opens the track's blocks in a panel** over the right three quarters
  (the phrase numbers stay visible): SELECT walks it from the block in
  place, PLAY plays the one under the cursor alone, looped, SEL takes it,
  **GLO goes back** unchanged. The two buttons are written at the bottom,
  each on the side of its button. HOME stays "home" even there.
- **SONG validated on the panel, 28/10** ("c'est parfait"), as above.
  No button to empty a cell: KNOB 2 stops at `--` and a flick to the
  left gets there from any block (tried on the device: "ça va vite").
  Piano keys are kept for notes on every screen, never for commands.

**The road down, agreed 28/10: HOME → SONG → SECTION → block.** A block
is never edited from SONG: KNOB 1 goes left of the first track onto the
phrase's number, SEL there opens its SECTION, SEL on a track there opens
its block. **GLO is "back" everywhere** (one level up, or out of a
panel); HOME is "home". ("Section" is Jaeger's word for a phrase.)

**SECTION, mocked up**: SONG's grid turned to steps. One row a step, its
number at the left, white on the beat (every 4) and grey between; one
column a track with the note it plays on that step, `--` when none.
Five tracks a screen, not six: at six, `A-3C#3` ran together. SELECT
the step, KNOB 1 the track; the section's number small at the top
left. No role for KNOB 2 yet (transposing the note is a candidate).
**SECTION validated on the panel, 28/10.**

**One grammar for every grid** (agreed 28/10): SELECT the row, KNOB 1
the column, KNOB 2 the value under the cursor, SEL one level down, GLO
one level up (or out of a panel), HOME home. Piano keys write notes,
OCT-/OCT+ their octave. A knob per field was turned down: it would be a
second grammar.

**SEQ on a SONG cell opens its block straight away** (Jaeger's idea: it
is a sequence); GLO then goes back to SONG.

**PATTERN, proposed, in three views that FX turns through** (rendered
off the device, `build/mock/pattern-views.png`):
- NOTES: the four notes of a step, all on screen (4 x 38 px); KNOB 2 a
  semitone, the keys write.
- FX: the three effects (3 x 60 px; the widest, `WAV 88`, is 56).
  KNOB 2 the value, **KNOB 3 the type**: the only knob with a meaning of
  its own.
- INS: one column, so the instrument's NAME fits; grey where the step
  keeps the track's own. The instrument stays out of NOTES and FX: with
  it, three effects no longer fit.
- An instrument per track was weighed and kept only as the default: 7 of
  the 13 songs change instrument inside a track (KIDS t1 and t7,
  tremolo t4 with four), and KIDS has all 8 tracks taken.
**PATTERN validated on the panel, 28/10**, as above (KNOB 2, KNOB 3 and
the keys were described, not yet tried: the mockup does not edit).

## 2026-10-28 — SONG, SECTION and PATTERN written

The three validated screens replace the 10/10 ones, behind the same
interfaces (`songview`, `phrase`, `pattern`); `app/grid.c` draws the grid
they share. Installed on the FM-1 the same day; the tests pass.

- **Buttons with no mockup yet, provisional**: ARP = UNDO (where the
  10/10 map had it), EDIT = MIXER, ENV = INSTRUMENT (from PATTERN), on
  SONG's phrase number SEQ = a copy of the phrase below and LFO twice =
  delete it. SAVE and PLAY as printed. To weigh with the machine.
- KNOB 2 on a number is a length: the phrase's (AUTO, then steps) on
  SONG, the block's on PATTERN; the corner says it while the cursor is
  there.
- In the block panel, PLAY puts the block in the cell for real, solos
  the track and loops the phrase; GLO, SEL or HOME end it and put back
  what was there (unless SEL took another).
- **Gone with the old screens**, not redesigned yet: LOOP, the PATTERN
  selection (COPY / PASTE / CLEAR a rectangle; `edit_clip_*` stays,
  untested now), MUTE on the section, a cell's level, the song's line
  (BPM, SWING, TICKS, TRACKS: unreachable for now), PRESETS stepping
  through blocks or phrases, the effect list beside the slot.
  `tests/loop_test.c` and `tests/select_test.c` went with them.
- INSTRUMENT and MIXER keep the 10/10 look until their turn.

## 2026-10-28 — the voice will be supported

Jaeger, hearing his songs on the FM-1: the voice synthesizer is needed.
This reverses "VOX probably never" (10/10). Later, not now. Four songs
use it: robot-demo, LEOMOTEUR, DAFT-PUNK, DAFT-PUNK-EN.

- Two separate pieces: the SOUND (moondaw's VOX in fixed point, as the
  other engines; Felucca runs a 4-voice formant engine on this chip, and
  the CPU is under 1 % on the demo) and WRITING syllables on the device
  (the interface; the original objection). The sound can come first,
  with the words written on the computer.
- Memory is the question: syllables were a third of moondaw's grid.
- **Trap until then**: `fm1_send.py` reduces a song to what the FM-1
  carries, so the copies on the device have no syllables (`'J2`,
  `'LA`...). Fetching one back with `--get` over the original loses the
  words. The originals on the computer are the only full copies.

## 2026-10-28 — FX and EDIT, MIXER validated

Decided with Jaeger: **FX shows the effects of where one is** -- in
PATTERN the steps' effects (its views), elsewhere the MIXER. **EDIT opens
the PROJECT**: NAME, BPM, SWING, TICKS, METER, TRACKS. The tempo is not
an effect.

- The meter exists in the file (`meter 4 4` in all 13 songs) and in
  moondaw's player; the FM-1 reads and writes it and uses it nowhere.
  It should at least set which step numbers are white (the beat), today
  every 4 by hand.

**MIXER validated on the panel, 28/10** (`app/mock.c`): one row a track,
VOL PAN REV ECH as the file writes them (pan `C`, `L18`, `R18`; a send
at 0 in grey); KNOB 2 on a track's number goes normal, MUTE (`M`, red),
SOLO (`S`, cyan; the others grey). Above track 1, out of sight until
SELECT goes up, the `FX` row of the returns: ROOM DAMP TIME FB -- **the
column names change with the row** (Jaeger: "très malin"). A trick to
keep wherever a grid has a row of another kind.

## 2026-10-28 — MIXER and PROJECT written

Both as validated (`app/mixer.c` rewritten on the grid, `app/project.c`
new), installed the same day. FX opens the MIXER from SONG and the
SECTION, EDIT the PROJECT from SONG, the SECTION and PATTERN; GLO goes
back to whichever opened them.

- **The meter is used**: the step numbers in white in the SECTION and
  PATTERN follow STEP (steps a beat), no longer every 4 by hand.
- A new name is written at the next SAVE, in the same slot; renaming
  is not in UNDO.
- MUTE and SOLO from the mixer are monitoring, as before: not saved.
- The song's line is reachable again (PROJECT), so it leaves the list
  of what the old screens took with them.

## 2026-10-28 — the voice sounds

moondaw's VOX is in `core/synth.c`, all five styles (GLIDE, HARD, SOFT,
MELO, SINGR), integers only, with the player's two look-aheads for it
(`core/player.c`: consonants fired early, codas closed before the next
note). The words are still written on the computer; nothing on the
screens changed.

**Memory.**

- **Syllables: one table for the song, not a pool per block.** An
  `FX_SYL` step holds an index into `Song.syl`, 255 entries of 6 bytes
  (1,530 B). A syllable is stored once however often it is sung; the
  four singing songs use 10 to 30 distinct ones. moondaw's per-block
  pools were 128 x 6 B x 256 blocks = 186 KB, a third of its grid.
  When the table is full, entries no step names any more are taken
  back (moondaw's lazy reclaim). `sizeof(Song)` 77,486 -> 79,144 B
  (+1,658: the table, and `tract` / `accent` on each instrument), so
  +3.3 KB with the load's working copy, which still fits the 128 KB
  scratch.
- **Voices: eight `Vox` of 980 B shared by every track** (7,840 B in
  `.pool`). moondaw gives each of a track's 20 voices a state of its own:
  160 + the audition would be 160 KB here. A voice keeps the one it was
  lent after its note so the syllable runs to its end; another voice
  takes a free one first, then one that has finished, then one whose
  note is no longer held. A ninth note held at once is not sung.
- In all, **+9.6 KB of RAM**: the song +1,658 B, the tracks +72 (the
  syllable setter and two flags), the voices 7,856. The tables are
  17.6 KB of flash (sinc 8 KB, the phase steps 4.8, the cosine 4.1).
- `./build.sh`: image 129,556 B; .data+.bss 51,464 B of 73,728; pool
  261,016 B of 368,640; package 610,086 B. No call to a float or 64-bit
  division helper anywhere in the image. Not installed.

**What the file carries now**: the syllables (`'J2` after the note),
read, written back in slot order and kept through `trkdump`, so
`fm1_send.py` sends the words and `--get` brings them back. The trap of
the section above is gone. `tract` and `accent` are carried too and no
longer counted under "shaping". `fm1_send.py` rebuilds its `trkdump`
when the reader's sources are newer: a stale one would still drop the
words.

**The engine in integers.** Signals Q28 (Q24 in the cascade),
coefficients Q30, every length a whole millisecond (all of moondaw's
are). New tables in `tools/gen_tables.c`: a quarter cosine of 1025
steps (resonator tuning), the 128 x 32 Blackman sinc (Q14, 8 KB), the
voice's phase step a cent apart, the burst's exp(-3t), and the lag and
tilt constants. Bandwidths go through exp(-x)'s series, gains through an
integer square root, the anti-resonator's 1 / a through a bit-by-bit
division once every 64 samples: no 64-bit division helper. One trap:
**a lag that floors settles under its target.** The pitch lag of the
cascade sat 0.004 Hz flat and SINGR drifted to -27 dB against moondaw
in half a second. Rounded, it stays at -58 dB.

**Measured** (`make difftest`, now with VOX in the reference and
`tests/vox-styles.trk`, a song for the three styles no song uses):

- Level identical to 0.1 dB on every VOX track, over 180 s: robot-demo
  t0 -20.6, LEOMOTEUR t5 -15.0, DAFT-PUNK t6 -12.1 / t7 -26.6,
  DAFT-PUNK-EN t6 -18.2 / t7 -27.4 dB, both engines.
- Short-time spectra over 180 s: DAFT-PUNK -39.6 / -34.7, DAFT-PUNK-EN
  -37.1 / -34.6, robot-demo -23.9, LEOMOTEUR -22.4 dB. The five styles
  over 20 s: -38 to -40, SINGR -34 dB.
- **Sample for sample** the voice matches at -40 to -50 dB, until two
  things it does not own decorrelate the phases. (1) After an OFF the
  voice holds until the track's envelope is reaped, and its pulse
  phase stops there. moondaw reaps every 1024-frame buffer, the core
  once a tick (the rule of 10/10). The next note then starts some
  samples apart: robot-demo goes from -37 dB to -26 dB at its first OFF.
  Reaping on moondaw's grid in a throwaway build brought it back to
  -33 dB. Not changed: the device must not depend on a buffer size.
  (2) An fVIB, from LEOMOTEUR's 11 s on: the vibrato in cents is the
  chip engines' own and drifts the same way. moondaw's float phase
  also gains ~1e-4 cycle a second on the exact one, which moves a naive
  pulse by a sample now and then.
- `tests/vox_test.c` (in `make test`): each singing song reduced as
  `fm1_send` does, written to flash, opened by the app, played with only
  its voice tracks, saved. All 11 / 30 / 76 / 118 syllables arrive and
  are written back; the voice plays at -11 to -21 dBFS.
- The chip engines are bit for bit what they were: the eight songs
  without a voice render identically before and after.
- KIDS t2 is at -20.2 dB, not -28 as on 10/10, and was so before this
  change (same figure from HEAD): moondaw has moved since.
- CPU: not measured on hardware. On the Mac a sung track costs about as
  much as two chip tracks (robot-demo, 600 s: 2.34 s without the voice,
  2.70 s with it).

**Not done**: writing syllables on the device. Until then the screens
do not know the voice. INSTRUMENT still says SILENT HERE on a VOX
instrument, and a turn of its ENGINE knob sends it to PULSE. PATTERN
and PHRASE show a syllable's slot as `SYL` and its table index.

## 2026-10-28 — INSTRUMENT agreed

Jaeger's order: the instrument's NAME first, then hearing it -- **PLAY on
INSTRUMENT is a preview of the instrument, not the song** -- then its
settings by importance. Agreed on the images (`build/mock/
instrument-proposal.png`), not yet on the panel: the FM-1 was busy with
the chorus measurements.

- The settings that go together share a row of up to four columns, and
  **the column names at the top change with the row** (the MIXER's
  returns row, generalised): NAME; SOUND (ENGINE, TIMBRE); ENV (ATK DEC
  SUS REL); FILTER (CUT RES ENV VEL); LEVEL (GAIN, LEVEL); one row a
  layer, LYR n (SOUND, SEMI, FINE, GAIN); `+ LYR` to add one. A VOICE
  row (tract, accent) for a VOX instrument.
- Five cells of 33 px a row; a column takes one or two (an engine's name).
- A layer shows its timbre alone: each timbre belongs to one engine
  (SAW is WAVE, 25% is PULSE).

## Open

- The voice (VOX): writing syllables on the device, and the screens
  that do not know it sounds (above).
- LOOP, the PATTERN selection, cell levels: gone with the old screens,
  to be redesigned.
- INSTRUMENT: still the 10/10 look.
- The limiter; redo (no button for it).
- `web/installer/` is copied, not adapted, not run.
