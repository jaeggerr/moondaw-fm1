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
  updates. The main loop empties it every frame -- into the notes a
  keyboard plays since 30/10 (`midi_drain`), a plain `mi_r = mi_w`
  before that.
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
  console (moondaw, 24/08: "it still looks like an ugly DOS thing").
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
  on PATTERN in the SDL window was "very blurry". Part of it was macOS
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

Jaeger on the player screen: "why is the home screen ugly?" It
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

**The interface, seen on the panel: "horrible, too small", to be
rethought from A to Z.** Every screen above was drawn without the machine
and, as said on 10/10, "validated" meant "not objected to". None of the
layout decisions stand: type size, row height, what one screen shows,
the button words on screen, the four cards. What stands is what the
tests prove (core, store, link, undo, the edit code under the screens).

## 2026-10-28 — the type size, chosen on the panel

`MOONDAW_APP=type` (`app/type.c`): a ruler, then 17 pages of one face and
size each, filled with tracker rows. Jaeger, with the FM-1 at playing
distance: readable "without much effort" from 18, **read at a glance at
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
- **SONG validated on the panel, 28/10** ("it's perfect"), as above.
  No button to empty a cell: KNOB 2 stops at `--` and a flick to the
  left gets there from any block (tried on the device: "that's quick").
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
column names change with the row** (Jaeger: "very clever"). A trick to
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

## 2026-10-28 — the audio block overran: measured, and what was tried

DAFT-PUNK-EN crackled at its chorus once the voice was in. HOME now shows,
while a song plays, the worst audio block since PLAY: MAX in percent, and
its microseconds stage by stage (`player_profile`, the firmware's TIMER4):
O oscillators, V voice, C each track's filter/envelope/mix, R reverb,
E delay, T the tick. `tests/dpen-chorus.trk` is the chorus alone (phrases
06 and 07: all 8 tracks, both voices), looped by PLAY.

| build | O | V | C | R | E | T | MAX |
|---|---|---|---|---|---|---|---|
| -Os, from XIP | 519 | 723 | 1078 | 584 | 72 | 93 | 101-102 |
| core at -O2 | 543 | 523 | 1079 | 398 | 71 | 82 | 89 |
| + 32-bit sat/pan/reverb | 503 | 556 | 1030 | 400 | 71 | 84 | 87 |

(microseconds of a 2,902 us block; KIDS, no voice, was already MAX 89 at -Os.)

- **Not the cache.** The audio core (synth, player, fx) now runs from RAM
  (`.ram_audio`, `firmware/app.ld`), copied at boot: no change at all.
  Kept: it costs nothing and removes a doubt. RAMTEXT moved to the top of
  the SRAM (0x01C6C000, 32 KiB, POOL 328 KiB) because **a direct call
  reaches +-4 MiB and 0x01C00000 is just past that from the XIP code**;
  `-mlong-calls` is accepted by the JieLi clang and does nothing.
- **-O2 for synth, player and fx** (the rest stays -Os): V and R fell by
  a quarter to a third; C and O did not move.
- Exact rewrites, the five reference renders hashed identical before and
  after: `sat_add` with the carry instead of in 64 bits, the pan in 32
  bits under 2^25, the reverb's feedback in 32 bits, the chain's state in
  locals for the block. A few percent: **the chip has too much work, not
  wasteful arithmetic.**
- The clock is not the lever: felucca-bsp (`chip/fm1_clock.h`) measured
  360 MHz under M-VAVE's boot loader, ours, and about 51 one-instruction
  loops a microsecond -- the core is slow per instruction.
- **felucca-bsp has a working second core** (`chip/fm1_cpu1.h`, from
  Melodee, MPL-2.0): CPU0 hands it one job (half the render), does its
  share, waits. On some FM-1s CPU1 now and then misreads shared RAM, so
  each job carries a check word and a failure restarts (or falls back to
  one core). Not tried here yet.
- **A hardware fault to keep in mind** (felucca-bsp `docs/CPU.md`):
  inlined 32x32->64 multiplies among 16-bit loads now and then corrupt a
  register and the firmware crashes; fm1-radio cured it by moving the
  multiplies into `noinline` helpers (half the speed). Our synth and
  voice do exactly that (int16 tables beside 64-bit products). Not seen
  here yet.

## 2026-10-28 — the second core: prepared, not yet run

Decided with Jaeger: the second core rather than a lighter sound. Built,
committed, **not installed**: the test waits for his tool to talk to the
machine directly.

- **From felucca-bsp at 5942e68** (MPL-2.0, `LICENSES/MPL-2.0.txt`):
  `firmware/hal/fm1_cpu1.h`, `fm1_cpu1.S`, `fm1_cpu1_stop.h` verbatim
  (Melodee's worker); into our HAL: the CPU1 fault hook in `fm1_irq.h`
  and its resume in `fm1_vec.S`, `fm1_cnum`, `fm1_uptime_ms`, the
  DBG_MSG bits per core; `fm1_cpu1_stop()` before every reset, UBOOT and
  update; **VDC14 7 and SYSVDD 15 at boot** (M-VAVE's own values: at the
  boot loader's, under two cores, some units misread flash and SRAM).
- `FM1_CPU1_FALLBACK 1`: a misread, a fault or a job late (8 ms) holds
  CPU1 until power-off and CPU0 renders everything, as before; the job is
  then rendered by CPU0 (CPU1 may have begun it: a glitch at worst). A new
  build lifts the bar (`fm1_cpu1_bar_build`, from __DATE__ __TIME__).
  `MOONDAW_CPU1=0 ./build.sh` builds with one core.
- **The split** (`core/player.c`): CPU0 first does what touches shared
  state (mutes, VOX voices put away, reaping) and lists the tracks that
  sound; every other one goes to CPU1 (`player_run_other`), so the two
  voices of the chorus (t6, t7) land on both; each core renders into its
  own `Lane` (scratch, mix, sends); CPU0 waits (`player_wait_other`),
  sums the mixes with saturation and the sends, then the audition,
  reverb and delay as before. Profile: `W` on HOME is the wait.
- Same samples: the five reference renders hash identical with one core
  and with a simulated second core that runs its share at once (23,000
  jobs a minute). Saturation is the only place the order could differ.
- RAM code: `.c1_text` (CPU1's idle loop) is in `.ram_audio`; RAMTEXT is
  full to 1.5 KiB. RAM .data+.bss 59.4 KiB of 72 (the second lane, CPU1's
  stacks).
- To watch on the device: that CPU1 answers at boot (else one core,
  silently: `W0` and the old figures), MAX on DPEN-CHORUS, and crashes
  (the misreading units).

## 2026-10-28 — INSTRUMENT written

Written on its own branch (`instrument-grid`) while another session
changed the firmware on main for the chorus crackles, then merged.

- `app/instrument.c` on the agreed rows: NAME, SOUND, ENV, FILTER,
  LEVEL (GAIN LEVEL and VIB, the vibrato's phase, which had no row of
  its own), VOICE for a VOX instrument (TRACT 50..150 %, ACCENT FR/EN),
  a row a layer, `+ LYR` (KNOB 2 up makes one; the layer's SOUND all
  the way down removes it).
- ENGINE turns through PULSE, NOISE, WAVE and VOX; SMPL and PLUCK, silent
  here, are shown in amber and leave for PULSE at a turn. VOX's timbres
  are its styles (GLIDE HARD SOFT MELO SINGR).
- **PLAY is the preview**: C E G C an octave up, 250 ms a note, a rest,
  round, until PLAY again; a key stops it and plays itself. The song
  stops if it was playing. PRESETS still steps to the next instrument.
- The name editor is shared now (`app/namer.c`): PROJECT and INSTRUMENT
  (seven letters there, the file's limit).

## 2026-10-28 — the remote and the canvas

Jaeger: a model working on this machine should not have to ask him to
read figures off the screen, and should be able to compose on it. Built
on its own branch (`link-control`, from `instrument-grid`) while the
chorus crackles were worked on main, then merged.

- **Link version 2** (`app/link.h`): from STATUS on, the commands go to
  `link_control`, a hook the app sets (`app/remote.c`), so `link.c` and
  its test stay without the app. Nothing runs until a message arrives;
  they work on any screen, except while a song is being sent or fetched.
- STATUS answers one line of words (screen, song, dirty, playing,
  phrase, step, cpu, the worst block since the last reset, bpm, phrases,
  tracks, pool chunks free). INPUT presses, holds and turns as the hands
  do, merged into the next frames' input (a repeated seq is not done
  twice). OPEN, NEW, STEP, CELL, LENGTH, SAVE, PLAY write into the song
  in memory and play it, each in UNDO; a CELL past the last phrase makes
  the phrases between, a STEP past a block's end lengthens it.
- **The CANVAS** (`app/canvas.c`, command 11): the computer draws on a
  screen of its own -- rectangles, text in the machine's fonts, lines,
  kept as a 4 KB list and redrawn band by band; raw pixels straight to
  the panel (no frame buffer to keep them), 120 a message. GLO goes back
  to the screen it covered. For trying a screen on the panel without a
  firmware: `fm1_ctl.py canvas image mock.png` sends a picture in about
  10 s (a message a frame).
- `app_cpu`, `app_block`, `app_worst` are read once a frame in `app.c`:
  HOME's MAX and STATUS no longer take turns resetting the same counter.
- `tools/fm1_ctl.py` is the other end (status, press, turn, key, play,
  loop, stop, open, new, step, cell, length, save, canvas ...), on
  `fm1_send.py`'s transport. `tests/remotetest.sh` drives the whole app
  headless over UDP (`tests/remote_dev.c`) and checks what lands in the
  file.

## 2026-10-29 — the second core on the device

Installed and measured with the remote (`fm1_ctl.py status --profile`,
new: the worst block stage by stage over the link, and `--boot`: the
reset source, the last crash, CPU1's state).

- **CPU1 starts on this FM-1** (`cpu1=1`) and took every job it was
  handed: 94,146 over four minutes of DAFT-PUNK-EN, none refused, never
  retired, its stack 424 bytes deep.
- **DPEN-CHORUS: MAX 87 -> 57**, the worst block 1,760 us of 2,902
  (T171 O158 V272 C556 R383 E82, W54: CPU0 waits 54 us for CPU1's share).
  DAFT-PUNK-EN whole: 36 to 57 by phrase, CPU 28 to 50. Room for more.
- **One restart, not explained yet.** The first full play of
  DAFT-PUNK-EN stopped answering around phrase 0B, about 75 s in, and
  came back reopened, nothing playing, CPU1 still allowed (so not a CPU0
  fault beside a running CPU1: that would have barred it). The P33 reset
  source after it read 0x04, the watchdog: the main loop stopped feeding
  it. The next full play (four minutes, the same song, the same spot
  twice) ran clean. To watch: `status --boot` after any silence.

## 2026-10-29 — the canvas alive

Prototypes that answer the hands, still without a firmware: the script on
the computer draws, the machine keeps the gestures made on the canvas,
the script collects them and draws again.

- **EVENTS** (link command 12): buttons pressed and released, keys
  down, up and held, the seven knobs' turns, since the last EVENTS (27
  bytes); extra says whether the canvas is still in front.
- **CANVAS carries several orders a message** (`op, length, bytes` each):
  a redraw of a screen is one or two messages, not one a rectangle (a
  message is answered once a frame).
- **KEEP**: GLO goes to the script instead of leaving; HOME always
  leaves. `fm1_proto.Canvas.keep_glo()` sends it at once -- queued, the
  first `clear()` dropped it.
- Only the bands that changed go down the SPI, unless raw pixels were
  drawn behind gfx's back.
- `tools/fm1_proto.py`: `Canvas` (clear, fill, text, line, image,
  keep_glo, show, events, run). `tools/protos/list_demo.py` is a
  prototype to copy: a list SELECT walks, SEL marks, KNOB 2 changes,
  GLO answered. `fm1_ctl.py canvas` uses the same library.
- Trap: the event block is 27 bytes, five u32 and seven i8; counted 23
  at first, which cut the last four knobs off the answer.
- Checked headless over UDP (`tests/remotetest.sh`, and the demo driven
  by `fm1_ctl.py`), then **on the FM-1 over USB, 29/10**: `fm1_ctl.py
  status` answered through the real cable, and `list_demo.py` ran on the
  panel -- Jaeger: "perfect, nothing to add" (fast enough, drawn clean).

## 2026-10-29 — an idea for later: the band on stage

Jaeger, "useless but fun": a little cartoon engine in the manner of
Animaconda (his project, `~/Documents/Animaconda`), minimal, in C. You
build a band -- one musician per track -- with a simple face editor
(hair, eyes, mouth, skin, clothes), give each an instrument, pick a stage
set, and the song is played as a clip on the panel, the musicians moving
in time. Not started; only weighed for resources (no implementation
research asked for):

- Flash: characters as vector shapes, 2-4 KB each (Ginette as a
  bitmap: 85 KB raw, 11-19 KB as 8-16 colours run-length, 6.7 KB as PNG);
  a library of ~50 face parts 10-15 KB; instruments and sets a few KB;
  the drawing engine 10-20 KB of code. **40-60 KB in all**, against ~440
  KB free in the program's area (the songs' store is separate).
- RAM: no frame buffer needed (the screen is drawn in bands already); a
  scene's state is a few hundred bytes; a musician saved as ~10 bytes of
  part numbers.
- CPU: the audio interrupt keeps priority, so a heavy song slows the
  animation, never the sound; two cores, 57 % at worst on DAFT-PUNK-EN's
  chorus.
- **The real limit is the panel's bandwidth**: moving figures change many
  bands a frame; 10-20 frames a second is a guess (the SPI's speed is not
  measured) and enough for a cartoon.
- Animations predefined in code: two or three poses an instrument,
  triggered by its track's notes (a stick hit, a strum, a mouth for VOX),
  heads on the beat (the player already says which step plays).
- On a 240 px wide stage 4-5 musicians fit; 8 tracks would be crowded.
- Where the band is kept: with the song (check that moondaw keeps lines
  it does not know in a .trk) or apart in the device's flash.

## 2026-10-29 — the mockup app removed

`app/mock.c` (MOONDAW_APP=mock) and `app/mock_kids.h` are gone (Jaeger:
mockups do not belong in the source). The canvas does it without a
firmware: a script in `tools/protos/` (copy `list_demo.py`) draws on the
panel and reads the hands, HOME goes back to the app. Its limits: the
app's fonts only (20, 14, 12, mono 8), and the computer plugged in. To
weigh other sizes, `MOONDAW_APP=type` (`app/type.c`) stays. The old
mockups (HOME, SONG, PATTERN, MIXER, INSTRUMENT...) are in the history.

## 2026-10-29 — the SCREEN capture, and CHOOSE PATTERN

`tools/fm1_ctl.py screen` saves what the panel shows as a PNG (link
version 3, SCREEN): each band drawn again into gfx's own band and sent
run-length coded, about 3 s a screen, no memory added. The canvas's raw
pictures are not in it.

The first thing it showed: the panel of a SONG cell's blocks, drawn over
the grid with the phrase numbers still at its left, read as "the phrase
view with one track" -- Jaeger forgot in a day what it was for. Redrawn
(prototype `tools/protos/pattern_choice.py`, validated): the whole screen,
titled CHOOSE PATTERN with PHR nn / TRK n, a play sign on every block,
filled green on the one PLAY plays, and SEL TAKE / PLAY LISTEN / GLO BACK
at the foot.

PLAY there is a listening switch (his question: automatic or not?). Off,
moving the cursor is silent -- passing through the list on stage starts
nothing. On, the block the cursor rests on for 200 ms (12 frames) plays
by itself; `--` is silence, the switch still on. PLAY again, SEL or GLO
turn it off; the PLAY light says it is on. The blocks have no names, so
one is found by ear: this spares the press on each.

## 2026-10-29 — FX on PROJECT, EDIT on MIXER

FX did nothing on PROJECT (Jaeger expected the song's effects there: FX
is the mixer everywhere but PATTERN). It goes to the MIXER now, and EDIT
on the MIXER to PROJECT. The two are a pair across: GLO from either goes
back to the screen that opened the first, never to the other one.

## 2026-10-29 — PATTERN's INS: what is written, and what plays

Jaeger read the INS view's grey names as "the instrument written on every
step" -- it was the one in force, a shade apart from a written one, which
the panel does not show. Writing a note never wrote an instrument; a step
without one keeps the one above. Now the shape says it (his idea, against
mine of -- everywhere and the one in force in the head; prototype
`tools/protos/ins_column.py`): on the left the number the step names, or
--; on the right the name of the instrument that plays, white where the
step names it, grey where it keeps the one in force. A step with no note
shows -- alone.

## 2026-10-29 — SONG: EDIT edits, GLO is the global

On SONG, EDIT edits what is under the cursor -- a cell's block in
PATTERN, the number's phrase in SECTION (Jaeger: "intuitive"); SEQ on a
cell is left free for later, rather than kept as a second way that hands
would get used to; SEQ on the number still copies the phrase. PROJECT
moved to GLO, the button's own word: GLObal, what holds for the whole
song. GLO did nothing on SONG (HOME goes home), so nothing was taken
away; on PROJECT, GLO goes back. Not on the device yet: changes are being
gathered one by one, installed together.

## 2026-10-29 — PATTERN: a button a view, CHOOSE INSTRUMENT

FX no longer turns PATTERN's views round: SEQ shows the NOTES, FX the
effects, ENV the instruments (Jaeger: ENV was said to be the
instrument's). ENV again on INS goes into the INSTRUMENT that plays on
the step -- one press deeper than before, the column first. SEL on INS
opens CHOOSE INSTRUMENT, CHOOSE PATTERN's twin: `-- keep` (name none, the
one in force plays) then the 64 instruments; PLAY listens, the one the
cursor rests on sounding the step's note, struck again every 45 frames;
SEL takes, GLO goes back. KNOB 2 still turns the step's instrument.

EDIT edits what is under the cursor or nothing: on SECTION it opens the
block, as SEL; on PATTERN it does nothing. Neither leads to PROJECT any
more (GLO on SONG does).

## 2026-10-29 — UNDO and REDO on ALGO

The ALGO knob is the journal, on every screen: a notch left takes a
gesture back, a notch right does it again; the corner says UNDO or REDO
in amber, NO UNDO / NO REDO in grey at either end. ALGO rather than
KNOB 3 or 4 (Jaeger's question): KNOB 1-4 are the editing row, whose
meaning changes with the screen, and KNOB 4 is the natural fourth
setting some day (the MIXER has four columns); ALGO means nothing in
MOONDAW, PRESETS may one day mean presets. ARP no longer undoes: it is
free, as SEQ on a SONG cell.

REDO in the journal: taking a gesture back swaps the song's bytes with
the journal's, so the records keep what the undo replaced, and doing it
again swaps them back. The next edit forgets what could be done again.
No memory added.

## 2026-10-29 — the SYLLABLE screen

Writing a VOX syllable on the device (`app/syllable.c`), from three
prototypes tried by hand (`tools/protos/syllables.py`): Jaeger kept two
and set the third aside (onset / vowel / coda on three knobs). PRESETS
turns from one to the other on the same syllable, as the hand is at ease:

- KEYS: a sign a key -- consonants on the white keys in QWERTY's order
  (R T P, S D F G J K L, Z C V B N M), vowels on the black; OCT+/- the
  other layer (Q H X +, and 0 5 1 4 8); LFO rubs out, ARP marks it weak.
  Quick once learnt; no room to say what a sign sounds like.
- WHEEL: five slots, KNOB 1 the slot, KNOB 2 the sign in alphabetical
  order (letters, digits, + .), the sign's sound written below.

Each change is sung on the step's note by its instrument
(`player_note_sing`, the audition singing a given syllable), PLAY sings it
again; SEL writes it, GLO leaves it. Reached by SEL on an FX slot in
PATTERN: the row's syllable where it is, else in an empty slot; another
effect is left alone. An emptied syllable empties its slot. The FX view
now shows a syllable as the file writes it, `'AQ`, not `SYL 03`; KNOB 2
no longer turns its index.

## 2026-10-29 — the guide: what can be pressed glows

Every button and key of the FM-1 has its LED, one colour, at several
levels (hal/fm1_input.h: dark, the glow ~1/30 of lit, a mid level, lit,
a breath). Jaeger's idea: light the buttons that do something on the
screen in front. As there is no colour, the level says it -- the glow:
can be pressed here; lit: a state (PLAY playing or listening); dark:
nothing here. `plat_led_glow` (fm1_led_dim), `ui_led_guide(mask)` once a
frame; `ui_guide`, on, until a SETTINGS screen (OFF / STATE / GUIDE) can
turn it off. First on SONG only, as a trial, by where the cursor is: SEL,
EDIT where there is something to edit, SEQ and LFO on the number, FX,
GLO, HOME, PLAY, SAVE; in CHOOSE PATTERN SEL GLO HOME PLAY. The other
screens put the glow out.

The glow did not read on the white LEDs (his words: the labels drown, it
mixes with the keyboard's cyan): the guide lights the usable buttons fully
(`ui_guide_lit`). Weighed on the panel with the link's LED: the glow is
worse than dark, mid acceptable, lit best -- kept. PLAY (and REC, later)
are apart: they light only for their state, never as "usable".

## 2026-10-29 — LED on the link

`fm1_ctl.py led FX lit C-4 breath`: the computer lights any button or key
at any of the panel's levels -- off, glow, mid, lit, breath
(`plat_led_level`) -- held over what the app lights until `led all app`
gives it back (link version 4, LED). To show him a button, or to weigh
levels, without building a firmware (his idea).

Then four steady levels between mid and lit, to weigh the guide's
brightness: `fm1_led_duty` in hal/fm1_input.h (MOONDAW's addition to
Felucca's scan), set k lit on 4 + k frames of 8 -- 50, 62.5, 75, 87.5 % --
in step, ~114 Hz at the slowest. `led ENV 50 LFO 62 EDIT 75 ARP 87 SEQ 100`.
He saw little difference between the five: the guide lights at 50 %.
HOME and SAVE left out of it: they do the same on every screen, nothing
to learn from them; SEL too, a way through the screens rather than a
feature (his words). GLO only where it is not "back": on SONG (PROJECT).

On every screen since (29/10, the rule settled): SONG FX GLO, EDIT where
there is something under the cursor, SEQ LFO on the number; SECTION FX,
EDIT on a track with a block, OCT-/+ (the keys write); PATTERN the two
other views' buttons, ENV always (twice: the instrument), OCT-/+;
SYLLABLE LFO, and in KEYS OCT (the layer) and ARP (weak); INSTRUMENT
OCT-/+; MIXER EDIT; PROJECT FX; HOME, the choosers, the namer, the canvas
none.

## 2026-10-29 — PATTERN: the SELECTION

Two prototypes (`tools/protos/selection.py`): A, KNOB 4 selecting steps
down from the cursor -- not intuitive, and the cursor not free (his
words); B, an anchor -- kept. The actions on ARP and LFO were refused:
labels that say something else are worse than a generic name. His
design, built:

- EDIT enters the SELECTION and drops the anchor at the cursor; EDIT
  again leaves. SELECT and KNOB 1 stretch the rectangle; KNOB 1 left of
  the first column takes whole steps (notes, instrument, effects). The
  other knobs do nothing meanwhile, the keys write no note.
- The black keys, printed OP1.. on the FM-1: OP1 copy, OP2 paste at the
  cursor, OP3 cut, OP4 clear. COPIED n / PASTED n / CUT n / CLEARED n in
  the corner, SELECT there otherwise.
- A copy, a cut or a clear lets the anchor go: the cursor is free, the
  clipboard's outline (amber) follows it to show where OP2 puts it. A
  new selection: EDIT out and in.
- The guide lights EDIT, and while selecting the OP keys that do
  something (OP2 once something was copied) -- the keys' own guide,
  `ui_led_keys`.
- The cursor is a white frame while selecting: the hollow cell read as a
  black hole in the selection (his eye on the screenshot).
- edit.c's clipboard (moondaw's): notes paste into the slots they came
  from, effects onto the cursor's slot.

## 2026-10-29 — KNOB 2 moves, KNOB 3 changes

SELECT is on the left of the panel, KNOB 1 on the right: moving the
cursor in a grid took both hands, or a hand going back and forth (his
complaint, playing). The knobs move by one: KNOB 1 the column, KNOB 2 the
row as well as SELECT (the wheel for long runs, KNOB 2 for steps), KNOB 3
the value, KNOB 4 the effect's type. The rule has no exception: KNOB 1
and 2 move, KNOB 3 and 4 change -- on SONG, SECTION, PATTERN, MIXER,
PROJECT, INSTRUMENT, the namer and SYLLABLE's wheel (KNOB 1 the slot,
KNOB 3 the sign). HOME and the CHOOSE lists keep SELECT alone: no value.

Then KNOB 1 and 2 swapped (his question, agreed): every screen has rows,
only the grids have columns, so the first knob takes the move every
screen has. KNOB 1 the row (with SELECT), KNOB 2 the column -- the
namer's and SYLLABLE's wheel's slots too -- KNOB 3 the value, KNOB 4 the
effect's type. KNOB 1 now walks HOME, PROJECTS and the CHOOSE lists as
well.

## 2026-10-29 — SETTINGS

What holds for the machine rather than the song (`app/settings.c`),
reached by GLO from HOME -- GLO is what is global, as it opens PROJECT
from SONG; the guide lights it on HOME. One setting a row, as PROJECT:
LEDS (OFF: none, PLAY's state either; STATE: PLAY's alone; GUIDE, the
default) and LIGHT, the guide's brightness (15, 50, 62, 75, 87, 100 %;
50 by default), a few buttons lighting at it on its row to judge. Kept in
the last slot beside which song was open (`.LAST`, three bytes since: a
file of one is from before), written once when the screen is left, by
GLO or HOME: an erase stops the audio for 50 ms.

## 2026-10-29 — HOME, a root menu

SETTINGS was reachable only by GLO from HOME: hidden (his question, "how
does one get there?"). HOME is now a root menu, as the other MOONDAW
apps': CONTINUE (the open project's name at its right, the amber dot if
unsaved; SEL goes into it, PLAY plays it), PROJECTS (how many; SEL opens
the list HOME was, GLO back to the root), SETTINGS, HELP (grey: to come,
the effects' help as in the other apps, then the FM-1's own, its buttons).
HOME leads to the root from anywhere (his call), the cursor on CONTINUE;
back from SETTINGS it is on SETTINGS. GLO no longer opens SETTINGS.
Prototype `tools/protos/home_root.py`.

## 2026-10-29 — HOME is back, GLO is global

GLO had become "back" because it was free, and its label says global (his
words, as with ARP and LFO for the selection). Now:

- HOME pressed: a step back, at once, on every screen -- what GLO did:
  out of PATTERN, SECTION, MIXER, PROJECT, INSTRUMENT, SETTINGS,
  PROJECTS, the CHOOSE lists, SYLLABLE and the namer (without keeping),
  the SELECTION first while in it, the canvas; SONG's step is the root.
- HOME held 0.6 s (36 frames): the root menu from anywhere -- the first
  step taken at the press is not lost, the way is up anyway.
- GLO: what is global where one is -- PROJECT from SONG, SECTION,
  PATTERN, MIXER, INSTRUMENT. The guide lights it there; HOME never. (GLO
  opening SETTINGS from the root was taken out the same day: it made
  little sense to him -- SETTINGS is the root's own row.)
- The canvas: HOME leaves; GLO is always the script's (KEEP says nothing
  now).

And CHOOSE PATTERN's listening put the "new" row's empty block in the cell
to play it -- which made it, and offered another past it: each notch down
grew the list (his find). The empty block is silence now, as `--`.

HOME in CHOOSE PATTERN went to the root (his find): the app took SONG's
step back before SONG could close its list. SONG takes HOME itself now
(SONGVIEW_BACK), the list closed first.

## 2026-10-29 — LOOP, from the SELECTION

The loop PLAY goes round in PATTERN is set from the SELECTION, no mode
and no button of its own (prototype `tools/protos/loop.py`): EDIT, stretch,
then OP6 -- the first black key of the third group. The black keys are
grouped by the hand, as a piano's: the clipboard in the first three (copy
paste cut), clearing in the two after (OP4 clear, OP5 left free for a
cousin of it), listening in the next three (OP6 the loop) -- his call
against OP5, which sat by "clear" and had nothing to do with it. OP6 on
the same steps, or with no selection, forgets the loop. It shows as a
cyan rule at the left edge and cyan numbers, LOOP 02-05 in the corner.
`loop_set` in loop.c; the player's `player_set_loop`, as before.

And PLAY in PROJECTS on another project went back to the root (his
find): loading a song put the hand at HOME's start. It stays where it is
now (`menu_reload`).

## 2026-10-29 — SELECT to the number, keys that only play, EDIT a shift

- SELECT, besides the row, brings the cursor onto the row's number (SONG,
  PATTERN, MIXER): the row as a whole, at once; KNOB 1 moves and keeps
  the column. Lists and INSTRUMENT have no number column: SELECT only
  moves there. (His idea.)
- PATTERN's keys write only in a note column of NOTES; on the step's
  number, in FX and INS they play the note with the step's instrument and
  write nothing -- a slot was chosen for the hand before, in FX and INS
  one it could not see. SELECT then KEYS: playing to try. (His idea; FX and
  INS mine, agreed.) SECTION has no number column: its keys always write.
- The black keys are labelled OP1-6, PIT, GLO, MONO, POLY (his list).
  MONO: the track alone (the MIXER's solo), again: among the others. To
  reach it without the SELECTION, EDIT held is a shift: the keys are
  commands while it is down, the SELECTION left as it was; a tap of EDIT
  still takes the SELECTION in and out, at the release. The track's number
  in the corner is green while it plays alone.
- MONO's LED lit (as PLAY's state, at full) while the track in front plays
  alone: PATTERN's track, the track under the cursor in SONG and SECTION,
  the row's in the MIXER (his ask). A CHOOSE PATTERN preview's solo is
  not MONO's: it stays dark then. `ui_led_keys(usable, lit)`.

## 2026-10-29 — autoscroll

PATTERN and SECTION follow what plays (his ask): while the phrase plays
and the hand has been still 1.5 s (GRID_FOLLOW_IDLE), the view goes with
the playhead a page at a time, the playhead at the top of the new page;
the hand moving anything gives the view back to the cursor at once, the
cursor never moved by it. `grid_follow_play`.

## 2026-10-29 — mute and solo, on MONO

One gesture wherever a track is in front (PATTERN's track, the track
under the cursor in SONG and SECTION, the MIXER's row): EDIT held + MONO.
Tapped: the track alone (solo, cumulative), or back to normal from solo
or mute; held 0.5 s: muted (his design, over a MONO cycle normal/MUTE/SOLO
and over OP5, too far from MONO; POLY was too much of a lie). EDIT is a
shared shift now (`ui_shift`, `UiShift`): a tap of it is the screen's own
EDIT, given at the release; held, the black keys are commands. SECTION's
keys write nothing while it is down.

Seen at once: a track's number at the top of its column green alone, red
muted, in SONG and SECTION (`grid_track_head`); the cells of a track one
does not hear grey (`grid_heard`); PATTERN's corner as well. MONO's LED
lit alone, half muted. The MIXER keeps KNOB 3 on a track's number.
In the MIXER, MONO needs no EDIT (his remark): the keys play nothing
there, so they are commands as they are; EDIT + MONO still works.

## 2026-10-29 — HELP

From HOME's root (`app/help.c`): a list of topics -- a page a screen
(HOME, SONG, SECTION, PATTERN, SYLLABLE, INSTRUMENT, MIXER, PROJECT,
SETTINGS), GESTURES, EFFECTS -- the cursor already on the last screen the
hand worked on, so a help of where one is needs no button. A page says a
gesture in two lines, the control white and what it does grey under it,
both CONDENSED 20 (layout B of `tools/protos/help.py`: he found A
prettier but too small, and a long meaning would wrap). EFFECTS is the
effects themselves with moondaw's 18-character line for each. Each page
holds only its screen's gestures (the prototype showed PATTERN's on every
topic, which he caught). The pages are written from the screens' own
comments: a gesture changed there is to be changed here.

EFFECTS then became what the other apps have (his ask, prototype
`tools/protos/effects.py`): a list of the effects with their short line,
SEL opening an effect's whole page -- moondaw's fx_help text word for
word, copied by `tools/gen_fx_help.py` into `app/fx_help.c` (12 KB, SMP
has no page there yet) -- wrapped to the panel in CONDENSED 20 by measured
width, read a line at a time; KNOB 2 the effect before or after it.

## 2026-10-29 — REC

PATTERN records what the keys play (his choices, asked one by one):
REC arms (its LED lit, REC ARMED in the corner; armed and stopped, the
keys only play); PLAY then counts a bar in -- the meter's beats, a click
each, the first higher (`player_click`, a short square beside the song)
-- and plays the phrase; a key is written on the step NEAREST the
playhead (`player_tick_in_step`: past the middle of a step, the next),
REPLACING what was there, a chord held together filling the slots; when
the last key is let go an OFF goes where that falls (the next step when
let go within the note's own), so the lengths played are kept. REC again,
or STOP, ends it; ALGO takes a recording back, step by step.

How many bars are counted in is a SETTING (his ask): COUNT-IN OFF, 1 BAR
(the default), 2 BARS -- the first beat of each bar the higher click; OFF
records at once. The fourth byte of `.LAST` (three: from before it).

## 2026-10-29 — a cell's level in SONG

KNOB 4 on a SONG cell turns its level in the phrase (the file's `vol`,
0..127, 4 a notch): the corner says VOL n while it turns; a cell below
full carries a thin amber bar at its foot, its length the level, so the
grid stays clean while all is full. KNOB 4 changes, as the rule says.

## 2026-10-29 — DRUM, Felucca's drum voices

He did not like the drums made of WAVE and NOISE, heard four candidates
(`docs/drums-demo/`, the listening board) and chose Felucca's: "Felucca;
you can just delete mine". They are a new engine, DRUM
(`core/drum.c`, `core/drum.h`; `UPSTREAM.md` lists what was taken):

- **An instrument is a kit**: TIMBRE picks it (STD, ROUND -- STD with the
  round kick --, 80 10 66 55 77 DEEP TIGHT PERC: Felucca's model kits).
  **The note picks the drum**, on the General MIDI map as Felucca's
  engine has it: C-2 (36) the kick, D-2 the snare, D#2 the clap, F#2 the
  closed hat, A#2 the open hat, the toms on F G A B C D, cymbals, bells,
  congas and claves up to 81; a note outside 35..81 plays its pitch class
  of 36..47. A chord strikes several drums at once.
- **Four knobs, the DRUM row of INSTRUMENT**: TUNE TONE DECAY SNAP, 64 as
  designed, moving every drum of the kit as Felucca's own knobs do. In the
  file: `drum TUNE TONE DECAY SNAP`, written only when one is not 64.
  Velocity is Felucca's accent (its ACC at 100).
- **The hit is the envelope**: a kit's track is held open (no attack ramp,
  no fast fall under a new hit), each drum of a track is one lane, mono,
  the lanes ring together, a closed hat chokes the open one. A note-off
  (OFF, GAT) still closes the track with the instrument's release. On
  INSTRUMENT a key does not end the hit; the keys put the kick on the
  first C (key 7), and so do PATTERN's and PHRASE's (`edit_key_note`).
- **Twelve lanes shared by every track** (`DRUM_VOICES`), 284 bytes each
  (3.4 KB, `.pool`): a hit takes its own lane back, else a free one, else
  the quietest. Felucca renders by blocks of 32 (its slow envelopes step
  once a block); a render here stops at every tick, so a lane renders a
  block ahead and hands it out: the same samples in any block sizes.
- **Level**: a hit peaks near -12 dBFS (Felucca's -6, halved), as a PULSE
  voice; the demo song with its kit plays as loud as it did.
- **Cost, estimated, not measured**: eight lanes ringing at once take
  0.25 % of real time on the Mac; DAFT-PUNK-EN takes 0.54 % there and 28
  to 50 % on the device, so about 2 % of a core a lane ringing, 18 % for
  eight. To measure on the FM-1.
- The demo song's KICK, HAT and SNARE are one kit now. DAFT-PUNK,
  DAFT-PUNK-EN and KIDS converted (`tools/drums/to_drum.py`) in
  `docs/drums-demo/songs/`; KIDS' drums, samples, were silent here and
  now play. moondaw does not know DRUM yet (it plays the pulse there):
  its songs are left as they are until it does; `difftest.sh` silences
  DRUM on both sides.

## 2026-10-29 — a project's panel: RENAME, DUPLICATE, DELETE

EDIT on a project in PROJECTS (his idea; EDIT edits what is under the
cursor) opens a panel: RENAME (the namer, the project kept in its place),
DUPLICATE (a copy named KIDS 2, 3..., which becomes the open one -- his
yes to my addition), DELETE (asked first, SEL delete, HOME keep: a flash
erase has no UNDO; deleting the open one opens the first project left in
its place, or an empty one when none is -- first it stayed in memory as an
unsaved row, which read as a bug to him: deleted is deleted). The flash cannot read one file while it
writes another (load and save share the sector buffer), so RENAME and
DUPLICATE go through the song in memory, as SAVE does: the project is
opened first when it is not the open one, and refused over unsaved
changes ("SAVE THE OPEN ONE FIRST"). The guide lights EDIT in the list.

Then OPEN first in the panel, for EDIT pressed where SEL was meant; and
PLAY stops what plays from wherever it has nothing else to do -- the
panel (where, stopped, it plays the project, as on its row), the namer,
HOME's question, SETTINGS, HELP (his find: stuck listening in the panel,
only HOME got out).

## 2026-10-29 — the limiter, and DRUM's cost measured

A master limiter after the MASTER knob (`player.c`, LIM_*): stereo-linked,
the gain drops at once to put a peak on 30000 and comes back over ~90 ms
(a one-pole step a sample); below the ceiling it does nothing, so
`make difftest` still passes as before. 32-bit division only, once per
sample over the ceiling (no 64-bit division in the image).

DRUM on the FM-1 (`status --profile`, 25 s of each, the worst block held):
KIDS with its WAVE/NOISE drums cpu 48 % max 59 %; KIDS-DRUM cpu 50 % max
65 %; DPUNK-EN-DRM cpu 40 % max 48 %. DRUM costs about 2 % on average and
6 points on the worst block: room left.

## 2026-10-29 — LOOP in SECTION

SECTION has no SELECTION (its EDIT opens the block), so its loop is set
by ends: EDIT held + OP6 at a step sets one end, a second press at
another step the other, a third forgets it (`loop_press`). The same
cyan rule and LOOP 02-05 in the corner as PATTERN. PATTERN does the same
with OP6 when no selection is anchored; with one, OP6 loops it.

## 2026-10-29 — SYSTEM, and a version

A SYSTEM row on HOME's root (his ask, with the samples to come in mind):
what is FREE of the flash the songs share, the SONGS (how many, how much),
SAMPLES (-- until the bank exists), a bar of it all (songs cyan, free
green, the dark between them the files' headers and what unfinished
saves left), the FIRMWARE's version and the CPU's worst block since the
start. Read once when the page opens. The version is `app/version.h`,
MOONDAW_VERSION "0.1" (it said "dev"); the identity stays FM-1_900.

The flash is one pool, nothing set aside: samples will take room from
the songs and give it back; the store's 31 song slots are the other
limit, the sample bank to be one file of them.

## 2026-10-29 — SMPL, stage 1: the sample bank

Decided with him: short one-shots only, one bank filled from the computer,
the other apps' format unchanged. No screen yet (CHOOSE SAMPLE and a
SAMPLES page are stage 2) but SYSTEM's line. Built and tested on the
Mac, then installed and played on the device (below).

- **A sample**: mono, 22 050 Hz, IMA ADPCM (4 bits a frame), a second at
  most. Blocks of 255 bytes (a predictor, an index, 504 codes): 16 fill a
  sector's 4080-byte payload, so the bank lays them where none crosses a
  sector (`core/sampler.h`). ~11 KB a second: the 909 kit, ten sounds,
  is 4.3 s in 50 KB, 13 sectors.
- **The bank** is one file of the store, slot 32 (`core/bank.h`: a
  header, 32-byte entries -- a name of 23, frames, where its blocks
  start --, then the blocks). `STORE_SLOTS` 32 -> 33; `.LAST` stays 31
  (`LAST_SLOT` pinned, `APP_SONG_SLOTS` 31): a flash written before
  mounts the same. A firmware from before sees slot 32 as free space and
  will write over it, so going back loses the bank, not a song.
- **Names**: `smp 00 tr909/tr9_kick.wav` plays the bank's `tr9_kick`
  (the last part of the name, without `.wav`, any case). Missing:
  silent. Resolved when a song is adopted and when the bank changes
  (`bank_resolve`, `bank_mount`).
- **The format carried** (`core/trk.c`, `core/song.h`): the `smp` table
  (64 entries of 47 characters, `begin`, `loop`, `ping` kept), the zones
  (one list of 128 for the song, file order = priority, `tune`, `gain`)
  and `fSMP`, read and written back as moondaw writes them (entries a
  zone of a used SMPL or an fSMP names; zones only under a SMPL). Every
  song of `../moondaw/songs` keeps its lines through a save, but
  tremolo's eleven zones on a PLUCK instrument, which moondaw drops too.
  `begin` and `loop` are kept and not played: the bank is trimmed, and
  one-shots only. Song +4.9 KB (84 396 B).
- **The voice** (`core/sampler.c`): reads the flash in place
  (`plat_flash_map`, the XIP window the boot maps plain; `store_map`
  gives the file's sectors), decodes a code at a time, interpolates
  linearly, ~40 bytes and no sound in RAM. Pitch from the zone's root
  (fSMP: C-4 over the whole keyboard) through a cents table
  (`SMP_INC_Q16`), the track's bends on top; the level is moondaw's
  (16000/32768 of full scale) times the zone's gain; envelope, filter,
  level are the chain's, as for every engine, and a note-off does not
  stop it (moondaw's one-shot). 16 voices shared, lent as VOX's are.
- **Safe against the flash**: `bank_hold()` before anything writes slot
  32 (the link stops the song too) -- a generation counter, and every
  voice dies at its next render; an erase runs with the interrupts off
  on CPU0, so no render meets it. A track with a sample sounding is
  rendered by CPU0 only: CPU1 never fetches the bank.
- **RAMTEXT was full**: the first build overflowed by 916 bytes. The
  sample pool's code lives in `sampler.c` (from the flash, -O2, like
  DRUM), and the player's main-loop functions (`player_init`, the
  commands) moved out of RAM (`.flash_text`, `firmware/app.ld`). 196
  bytes of RAMTEXT left. Image 210 120 B; .data+.bss 66 392 of 73 728;
  pool 270 380 of 335 872. No 64-bit division in the image.
- **The link, version 5**: BEGIN's flag bit 1 is the bank (the name is
  not read); INFO and READ reach slot 32. A bank replaces the bank
  whole; when the new one does not fit beside the old, the old is
  deleted first (the computer holds it: it read it to build the new).
- **The tool**, `tools/fm1_samples.py add | list | remove | get`, and
  `pack` to make a bank file without a device. `add` converts any WAV:
  mono, 22 050 Hz (a windowed-sinc low-pass first), the silence cut at
  both ends (-50 dB under the peak), cut to a second (5 ms fade),
  **normalised to -1 dBFS** -- so a sample is louder or softer than the
  file moondaw plays, and a song's gains set against those files will
  sound out of balance here; to hear on the device. It keeps the last
  bank it saw (`build/fm1-bank.bin`) and reads only the index back when
  nothing changed. Free space before and after.
- **SYSTEM**: SAMPLES says how many and how much, and the bar has the
  bank's own colour after the songs' (`C_SAMPLE`, a violet; to judge on
  the panel).
- **Tests**: `tests/sampler_test.c` (ADPCM round trip, 35 dB on a test
  tone; the bank in a store whose free sectors are scattered, found by
  every spelling, read across its sectors frame for frame; a damaged
  bank is none; `smp`/`zone`/`fSMP` written back the same twice; an
  octave up rings half as long; zone, fSMP, a missing name, the bank
  held; the tool's decoder and this one agree to the frame; KIDS' first
  track plays its 909 kick); `link_test` (the bank through the link);
  `remotetest.sh` (add, list, remove, get against the whole app).
- **On the FM-1** (later the same day, his yes): installed over the old
  flash, which mounted as it was (CLAVIOLINE reopened from `.LAST`, 31).
  The 909 kit sent in ~4.5 s for 50 KB (196 -> 144 KB free); KIDS (the
  moondaw one, with `smp` and zones) sent and played through. CPU 48 to
  68 % by phrase, **max 79** (KIDS with WAVE/NOISE 59, KIDS-DRUM 65). The
  worst block: T179 O653 V187 C858 R384 E72, **W2**: the sample tracks
  stay on CPU0 with their chains, and CPU1 waits on nothing -- the split
  is lopsided, the voices themselves (V) are cheap. To win back: decode
  into a buffer on CPU0 and let CPU1 run the chain.
- **Two restarts** while KIDS looped, after ~2.5 and ~3.5 min (P33 0x04
  then 0x40, no crash recorded); a third play of 3.5 min ran clean. Not
  explained; DAFT-PUNK-EN restarted the same way before the sampler
  (29/10, above). Found the same evening, below: not the samples.
- **Still to measure on the FM-1**: what a voice costs from the audio
  interrupt alone (V above holds it, with DRUM's), and the balance after
  normalising, by ear.

## 2026-10-29 — the restarts: `status --profile` after 179 s

The restarts while KIDS played (and DAFT-PUNK-EN's earlier on 29/10) were the
profile itself. The player's stopwatch was `fm1_ticks() / 24`: 32 bits
of 24 MHz turn over every 179 s, divided they do not, so a block timed
across the turn measured 178 956 970 us. Asked for over the link, that
figure went through `ui_dec`, whose digits buffer held 8: the ninth and
tenth landed on the stack, and what came next was a crash in the delay or
the render, or the watchdog. Every `--profile` after three minutes of
play restarted the machine (4 of 4, KIDS and KIDS-DRUM alike); plain
`status` never did.

- **Fixed**: `ui_dec` holds any int (and its sign); `fm1_us` counts on
  over the turn. KIDS 4 min, then five `--profile`: all answered, the
  block 2 442 us, no restart.
- **Found with a trail** (kept, `firmware.c`, `audio.c`): .noinit
  records the main loop's stage and ms, the last audio half's ms,
  whether a half was being rendered, the track each core renders and its
  stage (`player_trail`), the last sample block read (`smp_trail`); read
  at the next start with `fm1_ctl.py status --trail`. And `status
  --crash`: the crash record's rets, EMU, DBG, sp and last jumps, which
  the firmware kept and never reported (here DBG bit 4: a write to
  unmapped space). The record survives an install.

## 2026-10-29 — PRB, the chance a step plays

The one effect this firmware has that moondaw does not, and the first
divergence in the list: `FX_PRB`, appended after `FX_SMP` so no saved
song moves, written in the file as `fPRB40` like any other cell. A .trk
that carries it loses the cell when the other MOONDAW apps read it
(`fx_from_name` does not know the name) -- that is the price, and it is
why nothing else was taken from BEETLE's groove page.

What it does: the chance this step's NOTES play, v/127, 00 never and 7F
always. Only the notes. The other fx of the row are read on time, as
they are under a DLY, because a setter firing at random would leave the
track in a state the page does not show -- a VOL beside it still moves
the fader, a CUT beside it still ends what sounds. A failed roll is an
empty step and not a rest: nothing struck, nothing released, so a long
note under it carries on.

The roll is one LCG for the song, drawn ONLY where a step carries the
command, so a song that never writes one renders sample for sample as
before: KIDS at 10 s is byte for byte what it was (`fm_render` against
the same build from HEAD). The seed is taken back at every PLAY, so a
take from the top plays the same twice -- a passage can be judged,
edited and judged again -- while inside the take the loop does vary,
pass by pass. Both checked by rendering: PRB7F is byte for byte the
song without it, PRB00 is silence, PRB40 differs between its two passes.

VOX: a PRB step is not anticipated (`anticipate_vox` bails on it as it
bails on a DLY). The roll happens when the row comes, once, and a
consonant cannot be formed early for a note that may not happen; the
syllable strikes on the beat instead.

HELP: `tools/gen_fx_help.py` refused a list of effects that was not
moondaw's, word for word. Now moondaw's list must be the START of ours,
and the pages of the effects we add on the end live in the tool
(`EXTRA`), so the file stays generated.

PATTERN's KNOB 4 used to walk `1 .. FX_SYL`, which is why nothing after
SYL could be reached; it walks the whole list now and leaves out the two
that are written elsewhere (SYL on its screen, SMP in the sample table).

## 2026-10-30 — SEQ to SONG from MIXER and PROJECT

The two screens that hang off every other one had one way out, HOME,
and it goes back where they were opened from -- from PATTERN to the
pattern, from SONG to the song. SEQ was free on both: it says the
sequence, so it now goes to SONG, wherever the screen was opened from
(his words: "retourner dans la vue Song via SEQ, ca aurait plus de
sens"). HOME is untouched -- a step back, as on every screen; MIX from
a pattern still goes back to that pattern.

`MIXER_SONG` and `PROJECT_SONG`, taken by `back_to(AT_SONG)` in app.c:
the song as it was left, the loop of a phrase cleared. The guide lights
SEQ on both, and HELP's MIXER and PROJECT pages say it.

## 2026-10-30 — EDIT as a CAPS LOCK, and the keys play in SONG

His rule, one for every screen that has the alternate keyboard: EDIT held
down is the shift and nothing else -- let it go and the mode goes with it
-- while a quick tap locks it on, as a CAPS LOCK, and another tap takes
it off. `ui_shift` counts the frames EDIT has been down (`frames`), and
the screens that lock a mode on ask for the tap to be quick as well,
`frames < EDIT_TAP` (15 frames, 0.25 s). Before, holding EDIT in PATTERN,
changing one's mind and letting go opened the SELECTION. MIXER and
INSTRUMENT, where the tap only goes to the PROJECT, take any press.

And the keys play in SONG: the first instrument the block under the
cursor names (the song's first when it names none), the note written
nowhere -- what a track sounds like, heard without opening its block, and
a line tried over the arrangement. OCT- and OCT+ move the keyboard as
everywhere else, the guide lights them where there is something to play,
and the note stops when the screen is left. On a REF row, on the phrase's
number or on a cell with no block there is nothing to play; EDIT held,
the keys are commands, not notes.

To come, in this order: SEL opens the block (CHOOSE PATTERN goes), KNOB 3
auditions what it lands on -- without stopping a song that is playing,
the block is already in the cell and comes round on its own -- then
EDIT's selection of blocks with OP1-4, and the loop over phrases (new
work in the player: `loop.c` counts in steps of one phrase).

## 2026-10-30 — CHOOSE PATTERN goes: SEL opens the block, KNOB 3 is the ear

The panel of a track's blocks is gone, and with it `pop`, its drawing, its
PLAY-to-listen and the cell it borrowed to preview (`preview_here` put the
block in the cell for real and put the old one back on the way out). What
it showed was a column of numbers -- `--  silent`, `00`, `01`, the last
one `new` -- and KNOB 3 says all three of those by itself (his call: "ça
ne dit rien d'intéressant"). So:

- SEL goes in, wherever the cursor is: the SECTION on the phrase's number,
  the block itself in PATTERN on a cell. One rule with PATTERN, where SEL
  has always been the way into what is under the cursor. An empty cell has
  no block to open. EDIT still does the same on a cell; EDIT's own job, the
  selection of blocks, is the next step.
- KNOB 3 chooses the block and the ear follows: what it lands on plays by
  itself, the track soloed, once the hand has rested SETTLE frames (200 ms
  -- a quick turn through the blocks starts nothing on the way, the rule
  the panel already had). PLAY ends the audition, and the PLAY after it
  plays the song. Moving the cursor ends it too, as does leaving the
  screen; a solo MONO had put on is left alone.
- And a song that is playing is never interrupted (his rule): the block is
  already in its cell, so it comes round on its own at the next pass. That
  keeps the gesture that matters while arranging -- the song runs, the hand
  changes the block of a phrase that has not come yet.

The new block, his find: turning past the last one makes a fresh block, and
nothing says so once the list is gone. It wears C_FX in the grid (the
theme's colour for what is not written) and the corner says NEW while the
cursor is on it -- the cell under the cursor wears the cursor's own green,
so the colour alone would not reach it. And it is not kept: `block_limit`
no longer counts a blank block that only the cursor's own cell names, so
turning on past it makes no second new block (the walk that grew the
panel's list, 29/10), and turning back leaves nothing behind. A blank block
is silence, so the ear starts nothing for it.

`song_test` pumps a short `player_render` where it asks what is playing:
PLAY and STOP are commands the audio side takes, as on the machine.

## 2026-10-30 — EDIT in SONG: the SELECTION of blocks

EDIT does in SONG what it does in PATTERN, which is what he asked for: a
quick tap locks the SELECTION on, the cursor stretches it from where it
was tapped, another tap takes it off, and held it gives the commands for
as long as it is down. The black keys are the same three groups as in
PATTERN (`UI_OP_*`, moved to ui.h so the two screens read one set of
numbers): OP1 copy, OP2 paste, OP3 cut, OP4 clear, MONO solo and mute.
On the phrase's number the rectangle is the whole row, every track.

What a cell holds is its block and the level it plays at, and that is
what the clipboard takes -- the arrangement, not the notes: pasting puts
the same blocks somewhere else in the song. 32 rows by 8 tracks of it,
`clip_pat` and `clip_lvl` in songview.c (PATTERN's clipboard is steps,
and lives in edit.c because PHRASE shares it; this one is SONG's alone).
A reference has no cells of its own: it is passed over, never written.
Every write goes through `undo_bytes`, so ALGO takes a whole op back.

OP1 collapses the SELECTION onto the cursor, as it does in PATTERN -- that
is what makes copy, move, paste one gesture, and the C_FX frame showing
where OP2 would land is drawn the same way. While the SELECTION is on the
knobs only move: KNOB 3 and KNOB 4 do not change a cell under it, so no
block is altered and nothing is auditioned by accident. HOME takes a step
out of the SELECTION before it leaves the screen, as in PATTERN.

EDIT no longer opens a block (SEL does that since this morning), so the
guide lights EDIT everywhere on this screen now, a reference row included.
The loop over a selection of phrases (OP6) is still to come: `loop.c`
counts in steps of one phrase, and a song-level loop is new work in the
player.

## 2026-10-30 — the loop over phrases: OP6 in SONG

The last of the four steps. OP6 on a SELECTION of phrases makes them the
loop PLAY goes round, the same phrases again forget it, and with nothing
stretched it sets one end at the cursor and then the other -- PATTERN's
gesture, phrase for step.

One loop still, for the three screens: `loop.c` keeps `of_entry`, and -1
is the song itself. `tell_player` sends `player_set_song_loop(a, b)` for
that one and `player_set_loop(a, b - a + 1)` for a phrase's steps, so the
two can never both hold; `loop_clear` tells the player both are off.
SONG calls `loop_keep(-1)` each frame -- a loop a phrase's steps carried
is not this screen's -- and clears the loop on its way out, so PATTERN
and SECTION never draw phrase numbers as steps. The cyan rule at the left
edge, the cyan numbers and LOOP 02-05 in the corner are drawn as PATTERN
draws them.

In the player, `song_loop_word` beside `loop_word`: `from | (to + 1) << 16`
in one word, so the audio side never reads half of it, and the + 1 so that
a loop on phrase 00 alone is not 0. It bites in two places, both in
`player_tick` and both only in `song_mode`: at a step's edge the playhead
is brought into the loop if it is outside (PLAY starts in it, and a loop
set while the song runs takes at once), and where the last phrase of the
loop ends -- its references exhausted, as `advance_entry` tests it -- the
playhead goes to the loop's first phrase instead of advancing. `song_once`
still stops at the song's end first. A `to` past the end is taken as the
last phrase, so an edit under the playhead cannot send it nowhere.

`song_test` plays the song with a loop on phrase 00 while the cursor sits
on 01: the first render brings the playhead to 00, and 2.9 s later (past
the 16 steps of that phrase) it is still there instead of having moved on.
Checked by hand that the test fails with the player's hook taken out.

## 2026-10-30 — SONG on the panel, driven from the computer

Installed on the FM-1 (`./build.sh`, then `fm1_install.py`: 610 257 B,
FM-1_900) and tried on the device itself with `fm1_ctl.py`, on an empty
song made in memory -- nothing of his was opened, nothing written to the
flash, and SONG 01 was put back from it at the end. What the panel shows,
screen by screen:

- The new block: KNOB 3 one notch past the last gives it, NEW stands in
  the corner in amber, and the cell under the cursor keeps the cursor's
  green -- which is why the corner carries the word, as drawn.
- The ear: on a block that holds notes the phrase starts by itself, the
  track soloed (the other track's number goes dim, the play mark comes up
  at the left). On the fresh blank block nothing starts, as it should.
- PLAY ends the audition, the PLAY after it plays the song. With the song
  running, turning KNOB 3 changed the cell and did not interrupt it: the
  step went on counting and no track was soloed. That was the rule to get
  right, and it holds on the machine.
- The SELECTION: rows 00-02 by tracks 1-2 in C_SEL, the cursor row keeping
  its green outside the selected columns and the cursor's own cell hollowed
  with a white frame. It reads at a glance.
- OP6 and the loop: the phrase numbers turn cyan, the rule runs down the
  left edge, LOOP 00-02 sits in the corner, and the song played 02 -> 00
  -> 01 (never 03). The phrases go round on the device.

One nit, and it is PATTERN's too: the word OP6 says for a moment, "LOOP",
comes out red -- `grid_said_ink` gives C_MUTE to anything it does not know,
and red means muted everywhere else. The LOOP 00-02 that follows is cyan.

Not judged here: the sound itself, the 200 ms before the ear takes a block
(SETTLE), and whether C_FX is the right amber for an empty block. His call,
on the panel.

## 2026-10-30 — SONG follows what plays

His ask: the autoscroll SONG did not have. The rule is PATTERN's and
SECTION's, word for word (29/10) -- with the hand still GRID_FOLLOW_IDLE
frames (90, 1.5 s) the view goes with the phrase that plays, a page at a
time, and anything the hand does gives it back to the cursor at once. The
cursor is never moved by it. `still` in songview.c, `grid_follow_play`.

Tried on the device, on a song of twelve phrases made in memory: the page
showed 00-07, the playhead reached 08 and the view went to the last page
(04-0B, the clamp at the end of the song), the playing phrase cyan with
its mark; a turn of KNOB 2 brought the view back to the cursor on 00 at
once, the cursor still where it was left.

And a thing to know about `fm1_ctl.py screen` while the song plays: the
picture comes back a row at a time over SysEx, so a capture taken while
the grid scrolls can hold two pages at once (numbers out of order). The
screen itself is fine -- stop, or capture a screen that is not scrolling.

## 2026-10-30 — STOP leaves the cursor where the song stopped

His find, on SONG: the song plays, PLAY stops it, and the cursor is still
where it was when PLAY was pressed -- at the top of the song, most of the
time, because that is where one starts it. The hand then has to walk back
to the passage it was just listening to. Now STOP puts the cursor on the
phrase the song stopped in (`player_entry()` read before `player_stop()`).

It goes with the autoscroll of this morning rather than against it: while
it plays, the view follows and the cursor stays put; at the stop the
cursor comes to where the view already is, and because a button press
gives the view back to the cursor, nothing jumps. The audition is not
concerned -- the block it plays is the one under the cursor already.

## 2026-10-30 — PATTERN stops where it played, and EDIT stops shouting

Two of his, in the same breath.

**STOP in PATTERN** does what SONG's does since an hour: it leaves the
cursor on the step it stopped on, instead of where the hand had left it
before PLAY. Same reading as there -- the view has been following the
playhead, so the cursor comes to where the eye already is and nothing
jumps. `player_row() % len`, read before `player_stop()` and only while
this screen's own phrase is the one playing. SECTION has the same grid
and the same PLAY and has NOT been changed: he asked for PATTERN.

**EDIT is no longer lit by the guide.** His words: it is not a button of
the screen in front, it is a shift, "un peu comme le bouton de selection"
-- and SEL has never lit (ui.h, 29/10). So EDIT joins HOME, SAVE and SEL
in the set the guide leaves dark, with one exception: where a quick tap
locks its mode on (PATTERN's and SONG's SELECTION) the light says the
mode is on, as a CAPS LOCK's does, and goes out with it. MIXER and
SECTION, where EDIT only repeats what GLO or SEL already do, light it no
more. PROJECTS keeps its EDIT lit: there it opens that list's panel
(rename, duplicate, delete) and is nobody's shift -- the one screen where
EDIT is the screen's own.

Done through each screen's guide mask rather than `ui_led_state`, so that
no screen can leave the light on behind it: every `ui_led_guide` writes
it. One consequence: with SETTINGS' LEDS on STATE, where only PLAY's
state shows, the CAPS LOCK light does not.

## 2026-10-30 — the echo was being swung, and that is the wet sound

His, and old: "il y a comme un delay, un echo plutot, que j'entends sur
l'ecouteur de droite... ce son qui n'est pas bon". He had heard it for as
long as the song existed, on the computer as here, and said so -- which
was the clue that mattered, because it ruled out everything written for
the FM-1.

Measuring first said reverb: in the intro of DAFT-PUNK-EN (phrase 00, one
track, `t4`, a gated STAB panned left) the right ear carries reverb at
-32.6 dBFS against the echo's -39.3, and the reverb fills every gap the
gating opens. He listened to six renders and said no: the reverb is not
it, it is the repeats. He was right.

`echo_set(ticks, feedback, tick_samples())` -- and `tick_samples()` is
the SWUNG tick. With `swing 54` the same twelve ticks are 263 ms on one
step and 224 ms on the next, so the line stretches and relaxes, for ever.
The read head does not jump, it glides, a thousandth of the way per
sample, which is right for a tempo change and wrong for this: the catch-up
runs at two samples per sample, so for 27 ms after every step the tail is
read at three times speed, or backwards. Twice a step, under every repeat.
That is the spring.

The swing is where a note falls, not a change of tempo. What is counted in
seconds asks for the tempo's own tick now: `tick_samples_of(false)` for
the echo, `tick_samples()` (swung) for everything else. The notes do not
move by a sample.

moondaw did the same thing in `player_tick_samples` and had always done
it, which is why he had always heard it, and it has the same split now
(`player_tick_samples_steady`, his call, his app; its own tests pass).
The two engines agree better for it than they did before either was
touched: of the three songs that have both swing and delay, DAFT-PUNK-EN
goes from -14.1 dB of spectral difference to -35.6, DAFT-PUNK to -35.2,
LEOMOTEUR to -28.6. The sliding read head was itself a source of
divergence -- a glide counted in integers here and in floats there does
not land on the same sample.

## 2026-10-30 — a MIDI keyboard plays it

The FM-1 is a USB **device**, not a host: nothing plugs into it. A
keyboard therefore reaches it one of two ways, and both end in the same
ring (`usb.c`, `midi_in_q`): through the computer it is plugged into,
which forwards its notes to the device's port (`tools/fm1_midi.py`), or
straight into the 3.5 mm TRS MIDI jack -- that one is not read yet, the
HAL being there (`hal/fm1_uart.h`, RX verified on hardware) and the
parser it names never written.

- **The instrument is the screen's, not the channel's.** Every screen
  already turns one of its keys into a note on the instrument the cursor
  is on (`edit_instr_at`); a MIDI note is given that same instrument, so
  the sound follows the cursor and there is nothing to set up. The channel
  is not read at all. His call, and the right one: a channel would say
  nothing about a machine whose instrument is where the hand is. Should a
  channel pick a track one day, this stays the default.
- **The pitch is the keyboard's.** A MIDI note goes neither through
  `edit_key_note` nor through OCT- / OCT+: a keyboard has its own octaves,
  and the 27 keys' window is the panel's business. On a DRUM kit the note
  picks the drum off the General MIDI map, which is what the keys do too.
- **One voice, and the two hands share it.** The audition is monophonic
  (`player_note_on`, one at a time, the next replacing it), so a chord on
  the keyboard sounds its last note and a panel key let go takes the voice
  with it. That is the panel's own rule, not a MIDI compromise; polyphony
  there is a change to `player.c` and nothing to do with this.
- **A MIDI note is never a command.** EDIT held turns the panel's black
  keys into commands; the keyboard's notes play on regardless.
- **Nothing is written.** They play, and they do not land in a step --
  not under REC either. Writing from the keyboard is the next step, if he
  wants it: the screens' writing is still the panel's.
- **The ring must still be emptied every frame** (the rule above):
  `midi_drain` does it now, at the top of the loop where `mi_r = mi_w`
  was, and a frame that cannot hold what arrived lets every note go
  rather than leave one hanging.
- **One parser for the three streams** (`platform/midi_parse.h`):
  USB-MIDI's event packets, CoreMIDI's packets in the simulator, the TRS
  jack's bytes later. Running status lives there and nowhere else, and
  only notes come out of it -- the MiniLab's knobs and pads send
  controllers while it plays, and they are stepped over.
- **The simulator plays it too**: every CoreMIDI source on the Mac is
  listened to (`platform/sdl/main.c`), which is how this was written
  before the device had it.
- Verified: `tests/midi_test.c` (the parser, and on PATTERN a note that
  sounds, stops, keeps its pitch across OCT+ and takes the cursor's
  instrument), the host tests, and the firmware builds.
- **On the machine (30/10)**: installed with `fm1_install.py`, then played
  with no keyboard at all -- an arpeggio sent from the computer to the
  device's own port, heard on SONG on the cell the cursor was on. A cell
  with no block stays silent, which is the rule and not a fault: no
  instrument there, no note (`cell_instr` < 0). It cost an hour to learn
  that **the profile cannot answer this**: one voice costs less than the
  microsecond `status --profile` counts in, so O and R move by noise
  alone and only an ear settles it.
- Not tried yet: the MiniLab itself, either way -- the notes above were
  sent by a script, and the DIN-to-jack cable is not on the desk. The
  cable's arpeggio was heard again after the UART arrived, so the jack's
  init disturbs nothing USB does.
- A keyboard cannot be plugged into the FM-1 over USB, whatever the
  power: its USB0 is a device controller and the firmware has no host
  stack, so two devices face each other and nothing enumerates. He
  tried it the same evening; it is worth saying out loud before
  anyone else does.
- **The 3.5 mm MIDI jack, the same day.** UART1 on PH8 at 31250 baud, the
  DMA writing into a 256-byte ring (`hal/fm1_uart.h`, Felucca's, RX
  verified on hardware; nothing else used it). `midi_drain` empties it
  every frame beside the USB ring, a byte at a time into a parser of its
  own -- running status belongs to one stream, and the two know nothing
  of each other -- and both queues go into the one frame the app reads.
  A keyboard with its own MIDI output then plays the FM-1 with no
  computer anywhere, though it still needs its own power: MIDI carries
  none, and the FM-1's jack is an input.
- **The MiniLab 3's MIDI out is a 5-pin DIN**, not a mini-jack (I said
  the opposite twice, from memory, before checking Arturia's own pages).
  Its one big jack, marked CONTROL, is the 6.35 mm pedal input. So the
  cable into the FM-1 is DIN to 3.5 mm TRS, and the adapter in the
  keyboard's box goes the other way. Which TRS standard the FM-1's jack
  follows, A or B, is still unknown -- an adapter settles it, and no
  code changes either way.
- **The one thing hardware alone can settle**: `fm1_uart1_rx_take()` is
  documented as the bytes the DMA wrote since the last call, and the
  drain trusts that word. If it turned out to be an absolute count the
  jack would read noise while USB stayed perfect -- the first thing to
  look at if a cable gives nothing.

## 2026-10-30 — a song with samples in it, and the renderer that plays them

His ask: he has never heard a sample in a song. There was no way to, on
the Mac -- `tests/fm_render.c` never had a bank, so every SMPL track in
`difftest` was silent and the suite said so ("nothing the FM-1 can play").

- **`fm_render --bank BANK.bin`**: the bank from `fm1_samples.py pack`
  written into a flash in RAM (`tests/ramflash.h`) at slot 32, then
  `store_mount` / `bank_mount` / `bank_resolve` -- the link's work
  without the link, so the voice reads it over the same sectors as on
  the device. Without the flag nothing changes: the output is byte for
  byte what it was (checked on KIDS t2). `difftest.sh` and
  `tools/drums/drums.py` gained the three sources (`drums.py` had been
  failing to link since the sampler landed: no `sampler.c`).
- **`docs/sampler-demo/`**: `songs/SMPL-DEMO.trk`, every track SMPL --
  a 909 kit, a Precision bass, the Iowa piano, a marimba, a kalimba;
  twelve samples, 104 KB, 26 sectors. Am-F-C-G at 96 BPM, four steps of
  four bars that come in one group at a time. `SMPL-DEMO.wav` is the
  render, 40.5 s, peak -2.1 dBFS, nothing clipped.
- **On the FM-1** (his yes, the same evening): the eight melodic samples
  added beside the 909 kit rather than over it, so KIDS keeps its toms --
  18 samples, 12.3 s, 124 KB free before and 36 after, the bank written
  in one pass; the song 2624 bytes, 31 KB left. Opened and played: CPU
  **21 to 39 %** on the three melodic instruments alone (max 43), **50
  to 55** once bass, kick and hat join (62), **54 to 63** on the full
  eight tracks, **max 77** -- KIDS is 79, and for the same reason: the
  sample tracks all stay on CPU0 with their chains. Eight sample voices
  at once are comfortable; much more is not. The balance was set by peak
  here, not by ear on the panel.
- **The balance is the track volumes', not the files'**: normalised to
  -1 dBFS, the 909 hat came out as loud as the kick (peak 978 against
  15474 at the volumes a moondaw mix suggests). Set by peak, hit by hit:
  kick 12379, snare 8160, bass 10853, piano 10189, marimba 6885, hat
  2668, clap 3278, then everything scaled 0.8 for headroom.
- **One second a sample shows**: the piano, bass, marimba and kalimba are
  all longer in moondaw, and what plays is their first second with a
  5 ms fade. Short notes do not notice. Worth hearing before deciding
  whether stage 2 needs longer samples or loops more than it needs
  CHOOSE SAMPLE.
- **No screen for a sample yet**: nothing on the device cuts, trims or
  sets a start point. The tool does the only trimming there is (silence
  off both ends, a second at most, normalise), so a sample is cut on the
  computer before it is sent. `begin`, `loop` and `ping` ride in the
  file and are not played.

## 2026-10-30 — the piano's zones: what was measured, and what was not

His words after the first listen: the kalimba really good, the piano "a
bit less pro, like a small Casio, a cheap one for children" -- and, in
the same breath, "it's nice, it isn't bothering me, honestly it's cool".
He was describing a timbre, not reporting a fault. I read a complaint
into it and offered a repair; he said to try it. **Said plainly so the
record is right: he never asked for this, and afterwards he could not
tell whether it was better, nor can I.**

What is true is that the demo gave the piano three zones where moondaw
gives it nine, so a note could be played from a sample a whole
**tritone** away -- the F# of the fourth bar was a C stretched down six
semitones. Transposing a piano that far moves its whole attack spectrum
in one block: the pitch stays right and the timbre does not. That is a
real artefact of resampling and the reason for the five zones.

**But the measurement below proves a change, not an improvement**, and I
presented the two as one thing when I first wrote this up. "The
difference is as loud as the signal" says the output moved a lot. There
is no number here for better, and for a piano under drums and bass, at
-10 dBFS, already through 4-bit ADPCM, the predicted gain may simply not
survive to the speaker. Worth remembering the next time a figure is
reached for to back a judgement of sound.

- **Five zones now** (`pf_gb3`, `pf_gb4` added, moondaw's own spacing):
  no note of the song is more than three semitones from its sample. The
  piano track alone differs from the old render by as much as the signal
  itself (difference RMS 1069 against 1047), so this is a different
  sound and not a tweak; its RMS fell 1.2 dB, the upward shifts having
  been what pushed energy up. Bank 104 -> 126 KB, 26 -> 32 sectors.
- **On the FM-1**: 20 samples, 14.3 s, **12 KB free** -- the ceiling is
  in sight, and `tr9_crash` and `tr9_rim` (12 KB) are the only samples
  no song on the device plays. CPU unchanged, 23 to 39 % on the intro:
  zones cost nothing, a voice is a voice.
- **The marimba has the same flaw**, two zones and up to five semitones,
  and he has not complained about it. `marimba_gb4` is 11 KB if he does.
- **What this says about stage 2**: the pleasant sounds here (kalimba,
  marimba) are short plucked ones that fit in a second and that 4-bit
  ADPCM encodes cleanly; the piano is the worst case for both limits.
  Longer samples and loops would buy more than CHOOSE SAMPLE would, and
  zone spacing is worth a screen of its own before either.

## 2026-10-30 — the SYSTEM line read as a ratio

His, from the panel: SAMPLES said `20 / 159 KB` and he read it as 20 KB
used out of 159 available. SONGS had it worse -- `12 / 91 KB`, where both
numbers are plausible as sizes. The slash promises a ratio, and this
screen has a real one right below it in the bar, so the eye arrives
expecting one.

- **Shown on the panel before choosing**, four ways at once on a canvas
  driven from the computer (`fm1_ctl.py canvas`, a throwaway script):
  today's slash, A three columns with no sign, B a comma, C the count
  folded into the label (`20 SAMPLES   159 KB`). Each screen carried his
  real figures and, under them, the worst the format allows -- 31 songs,
  128 samples, 291 KB -- which is the only place the line can crowd. He
  flipped with SEL and **chose A**; I had come round to C while drawing
  them, and said so, and he took A anyway. His screen.
- **A, as built** (`app/system.c`): `row` stays for FIRMWARE and CPU MAX;
  `size_row` takes a count (NO_COUNT for FREE) at VALUE_X and puts the
  size against the right edge with `gfx_text_w`, so FREE's, SONGS' and
  SAMPLES' sizes line up under one another and can be read against each
  other -- which the old layout did not allow either. `count_kb` is gone.
- **The canvas is ASCII only** (`Canvas.text` encodes with `replace`), so
  the middle dot I had suggested in chat is not available: B was a comma.
- **Checked** in the simulator filled to look like his machine (20
  samples 159 KB, 11 songs 85 KB) rather than on the device, the panel
  having already served for the choice; `menu_test` and the sim selftest
  pass.

## 2026-10-30 — SMPL on the knob, and the trap it was before

His, from the machine: in INSTRUMENT he could not find how to pick a
sound already in the bank. He cannot -- CHOOSE SAMPLE is stage 2 and is
not written. But looking for it turned up something worse than missing.

- **SMPL was not on the ENGINE knob at all** (`engines[]` in
  `app/instrument.c`: PULSE, NOISE, WAVE, VOX, DRUM). The turn looked
  the current engine up in that list, did not find SMPL, and fell to
  `engines[0]`: **one cran on a sampled instrument turned it into a
  PULSE**, silently, and SMPL-DEMO's eight instruments were all one
  knock away from that. Deliberate while SMPL was silent, wrong since
  29/10. SMPL now sits between WAVE and VOX, so a turn steps and comes
  back. PLUCK stays out: the FM-1 does not have it, and a turn away
  would not return.
- **The engine's name was painted as "does not sound"** for every SMPL
  instrument. `inst_mute()` replaces that test: PLUCK always, and SMPL
  only while no zone of the song names this instrument -- which is the
  truth and is useful, since a fresh SMPL off the knob has no zone and
  says so in C_FX until one is given. Checked both ways in the
  simulator: MARIMBA of SMPL-DEMO white, a fresh instrument 08 amber.
- **`instrument_test`**: the engine walk gained SMPL and a turn back
  from it; the old "an instrument this machine cannot sound" block now
  checks that the knob steps a SMPL instrument along instead of throwing
  it away, and that it is still SMPL when the screen opens on it.
  `menu_test`, `persist_test`, `song_test` and the sim selftest pass.
- **Still not there**, and this is what he actually asked for: no way to
  name a sample or edit a zone on the device. The data is ready --
  `bank_count`, `bank_entry`, `bank_find` -- so CHOOSE SAMPLE is a
  screen and a zone row, one screen at a time.

## 2026-10-30 — CHOOSE SAMPLE, first screen: the SAMPLE row

What he went looking for. A SMPL instrument now has its own row on
INSTRUMENT, as VOX has VOICE and a kit has DRUM: **SAMPLE ROOT TUNE
GAIN**, the knob on SAMPLE walking the bank's names.

- **One zone over the whole keyboard**, made on the first turn
  (`zone_make`: NOTE_MIN..NOTE_MAX, which is what a file writes as
  C-0 G-9, so a zone made here and one read back from a save are the same
  zone). That is what a one-shot wants and what every drum of SMPL-DEMO
  already is.
- **An instrument with several zones is not touched.** The cell reads
  "N ZONES", the other three read "--", and every turn is refused: a
  multisampled piano cannot be quietly collapsed into one zone by a knob.
  Editing several is the next screen.
- **Choosing writes the song's table, not the bank**: `smp_slot` looks
  for an entry the song already has that resolves to the same bank
  sample and reuses it, else takes a free one and writes the bare name
  (`bank_find` reads the last part without `.wav`, so it resolves here
  and in a file alike). Then `bank_resolve`, or the voice would still
  play what the entry used to name. Seen end to end on the simulator:
  KICK909 turned from `tr9_kick` to `pf_c3`, saved, fetched back, and the
  file says `zone 00 C-0 G-9 root C-4 smp 06` pointing at the existing
  `piano9/pf_c3.wav` -- no duplicate entry, and `tr909/tr9_kick.wav`
  dropped out of the file by itself, no zone naming it any more.
- **`inst_mute` got stricter**: a SMPL instrument is drawn as silent
  until a zone of it names a sample **the bank actually has**. A zone
  pointing at a missing name is not a sound, and the screen now says so.
- **Two tables were missed at first**, both caught by the sanitiser in
  `instrument_test` rather than by eye: `heads[][4]` took its new column
  names, and the row *labels* `label[]` did not -- a global-buffer-overflow
  read at `instrument.c` draw. Worth remembering that this screen keeps
  its rows in three parallel tables (`label`, `heads`, `n`).
- **Tests**: `instrument_test` gains the row (no bank mounted: nothing to
  choose and nothing moves; ROOT, TUNE and GAIN write the zone; the zone
  still spans the keyboard and no second one appears), and its Makefile
  line now links `core/bank.c core/store.c` with `tests/ramflash.h`.
  `menu_test`, `persist_test`, `song_test`, `undo_test`, `pattern_test`
  and the sim selftest pass.
- **Still missing**: `lo`/`hi`, so one zone a keyboard only; no way to add
  or remove a zone; `begin`, `loop` and `ping` still carried and not
  played.

## 2026-10-30 — INSTRUMENT, from the panel: what he found testing it

He tried the SAMPLE row on the machine and sent a list. Every item was
right, and three were fixed at once.

- **The label ran into the rule.** "SAMPLE" at LABEL_X 8 is wider than
  the 55 px before the rule at CELL_X - 3, and its E crossed it. The row
  is called **ZONE** now, which is shorter and truer: sample, root, tune
  and gain together are a zone, and it explains the "2 ZONES" in the cell.
- **A cell that can hold nothing is no longer drawn, nor its head.** His
  rule, and a good one: a "--" under a head means a value one could set
  and has not; it must not mean one this engine does not have. `ncols()`
  answers 1 for SOUND when `engine_timbre_max` is 0 (only SMPL), and 1
  for the zone row until the instrument has exactly one zone. The column
  cursor was already clamped against `ncols`, so nothing else moved.
- **The refusal says why.** Turning on a "N ZONES" cell did nothing at
  all and he could not tell whether he had even pressed something. It
  sets the corner word now -- `MANY ZONES: ON THE COMPUTER` -- and the
  cell is drawn in C_DIM, not in the editable ink.
- **Which uncovered the same bug as his first**: the corner word is drawn
  on the line the column heads use, so a long one ran straight through
  them (`MANY ZONES: ON TSAMPLEPUTER` on the panel). The heads give up
  the line while a word is showing. Seen, fixed, seen again.
- **And one more**: the word did not clear when PRESETS stepped to the
  next instrument, so it hung over an instrument it was not about.
  `PE_PRESET` joins the encoders that clear it.
- **Not done, his to decide**: the values are centred in their cells
  (`grid_cell` -> `gfx_text_mid`) while the instrument's name is flush
  left, so the left edges disagree down the screen. Making them agree
  touches every row here and `grid_cell`, which MIXER and PHRASE share,
  so it is a change to show him on the panel first rather than to make.
- **Two things to remember from installing**: his song was `dirty=1` and
  the install restarted the device, which lost what he had not saved --
  the state is worth reading before flashing. And one `build.sh` failed
  (`pi32v2/bin/clang failed`, transient) while `fm1_install.py` went on
  to install the **previous** package without complaint: a build that
  fails must stop the install, or the machine quietly runs old code.

## 2026-10-30 — INSTRUMENT's cells go flush left, and OPEN gets its room

Shown on the panel before deciding, as the SYSTEM line was: three ways
drawn on a canvas from the computer -- centred (today), all left, and
words left with numbers right -- at the screen's own pixel positions and
with glyph advances parsed out of `app/fonts.h`, so variant A was what
the code actually draws and he could hold it against his own screen.
**He chose all left.**

- **This screen has its own `cell()` and `head()`**, flush left with a
  3 px pad. `grid_cell` centres and MIXER and PHRASE are built on that,
  so it is untouched: no screen moved that he did not ask to move. The
  instrument's name moves to the same edge, which is what made the
  ragged look in the first place -- it was flush left while every value
  was centred.
- **Measuring for the mock found a third overflow**, the same family as
  his two: **"OPEN" is 37 px and a cell is 33**. It has always spilled;
  centring hid it by splitting the excess two pixels each side, and
  left-aligning sent all seven into the next column (`OPEND 0 0` on the
  panel). FILTER's cutoff now takes two cells like the engine, the
  sample's name and a layer's wave -- four columns over five cells,
  66..231, which is exactly the room there is.
- For the record, the widths that settle these: SAMPLE 57 for the 53 px
  before the rule (his first complaint), ZONE 36, FILTER 49, OPEN 37 for
  33, tr9_kick 60 for 66, KICK909 61.

## 2026-10-30 — LYR read as "lyrics", so the layer rows say LAY

From the panel: the rows were `+ LYR` and `LYR 1`..`LYR 4`, and he read
LYR as lyrics -- a fair reading on a machine whose voice engine really
does sing syllables.

- **`LAY`, not `LAYER`**: `LAY` is 41 px and the label column has 53
  before its rule, where `+ LAYER` is 59 and does not fit. The same
  measurement that settled SAMPLE, ZONE and OPEN.
- **It is the `.trk` file's own keyword**, so the screen, the format and
  moondaw now say one word for one thing. A label the file already
  writes is always the better label.

## 2026-10-30 — a voice made on the machine sings in English

He asked for it plainly: an instrument he turns into a VOX on the device
should start on the English accent, not the French one. Asked whether the
**file's** meaning should move with it, he said no -- and that no is the
whole of this entry.

- **What changed**: one place, `turn()` in `app/instrument.c`, the ENGINE
  knob's own branch. It is the only place on the device where an
  instrument becomes a voice by his hand: nothing else in `app/` writes
  `engine` (PATTERN only reads instrument names, there is no
  new-instrument action and no presets -- `PRESETS` steps to the next
  instrument, it does not make one).
- **What deliberately did not**: `song_init`, `core/trk.c`, and the
  `VOX_ACCENT_*` values. A `.trk` writes ` accent EN` only for an English
  voice, so **the absence of the line means French** -- and that is how
  every song written until today was written. Had the default moved to
  where loading reads it, his old songs and moondaw's alike would have
  started singing English without a word, and the two engines would have
  disagreed where they agree today. `tests/difftest.sh` would have been
  the one to say so: robot-demo -25.8 dB, LEOMOTEUR -28.9 dB, unmoved by
  this change because loading was not touched.
- **"Only when it was not already a voice" needed more than it sounds**:
  `accent` is one byte whose zero is FR, so a voice that chose French and
  a fresh instrument that never chose anything look identical; and the
  ENGINE knob can never land on VOX while already on VOX (VOX sits
  mid-list, between SMPL and DRUM), so comparing the engine before and
  after the turn can never catch the case it was meant to catch -- the
  guard reads well and does nothing. Hence `voiced` in
  `app/instrument.c`: a bit an instrument, seeded at `instrument_open`
  from the song's own voices and set whenever this screen makes one. A
  voice **made** here gets English; one that already had an accent keeps
  it, so turning away from VOX and back loses nothing. Those bits live in
  the screen for as long as it is open and go into no file.
- **One edge, recorded rather than fixed**: `undo_bytes` snapshots the
  whole instrument, so UNDO puts the engine and the accent back -- but not
  the bit. Making the voice again then keeps the restored accent instead
  of English. The bit says "this instrument has been a voice", and after
  an undo that is still true; clearing it would mean clearing it when the
  engine leaves VOX too, which is the case the bit exists to protect.
- Locked by three checks in `tests/instrument_test.c`: the knob onto VOX
  gives EN, ROBOT (the fixture's voice, written with no `accent` line)
  loads FR and is FR still after a turn away and back, and the ACCENT
  knob itself still works both ways.

## 2026-10-30 — PATTERN says which view is next

Nothing on PATTERN said the three views existed. He spent a long while
looking for how to change a step's sound while the instrument column was
one button away, and the chooser (SEL there, with audition, and KNOB 3)
one more -- he found neither, because nothing told him the column was
there. So the top right of the header now names the view the next press
reaches: `INS ->`, `FX ->`, `SEQ ->`. He chose this over showing all
three at once: "there are only three, I will learn them quickly".

- **The order is his, not the enum's.** He thinks notes, then the
  instrument column, then the effects, round again; the enum is
  `V_NOTES, V_FX, V_INS`, so the marker is a second table and never the
  enum's neighbour -- from the notes it says `INS ->`, from the column
  `FX ->`, from the effects `SEQ ->`.
- **`SEQ` for the notes**, because it is printed on the button one
  presses to get back there, and because `NOTES` is 28 px in the header
  font and there is nowhere near that much room. `INS` is his word.
- **The width that decided the type.** The FX view all but fills the
  line: `FX 3` ends at x 209 and `PLAT_W - 4` is 236, so there are 27 px.
  In the header's own CONDENSED 12 `SEQ ->` is 27 px exactly -- it would
  start on the pixel where `FX 3` ends, and on the panel the two read as
  one word, `FX 3 SEQ ->`. In CONDENSED 9 it is 21 px, starting at 215,
  six clear of the head; and being the smallest type on the screen it
  reads as furniture, which is what it is. The other two views have room
  to spare (the notes head `4` ends at 186, `INSTRUMENT` at 168).
- **Display only.** Nothing was wired: in particular KNOB 2 still stops
  at the last column instead of wrapping into the next view. He raised
  that and left it undecided, to judge from use.

## 2026-10-30 — picking an instrument by ear, in PATTERN's INS column

His taste, and it had already won once that morning: in SONG the panel of
blocks that stood between the hand and the ear is gone, "KNOB 3 chooses
and the ear says which" (`app/songview.c`). The instrument column now
works the same way, and the full-screen list it used to need is on a
knob of its own.

**KNOB 3 is heard.** It already walked the song's instruments; what was
missing was the sound. The hand rests twelve frames (200 ms, SONG's own
SETTLE) and the step under the cursor sounds with the instrument it has
just been given, rings 400 ms and stops by itself -- the audition never
outlives the gesture that asked for it. While the song plays nothing is
struck at all: the transport is not fought, the music speaks.

**What sounds is the step, not a note.** A step holds up to four notes
and `player_note_on` has only ever carried one, so the audition track now
takes a chord: `player_note_chord`, which strikes the step's notes
together exactly as `trigger_step` does, through the same `voice_start`
and one `chain_note_on`. A step that names no note of its own is still
heard in its place, on the nearest note above or below it in the track's
column -- above first, since an empty step carries on the note before it.
An OFF is not a note, so the search goes past it; with no note anywhere
in the block, nothing sounds.

**SEL on the slot opens INSTRUMENT.** He found reaching it by pressing
ENV twice counter-intuitive, and SEL was free the moment the list moved
off it. **The ENV-twice way still works**: he has not asked for it to go,
and taking it out is one line on the day he does.

**The list is the PRESETS knob's.** Nothing else on this screen used
PRESETS -- it walks from sound to sound in INSTRUMENT and turns SYLLABLE
between its keys and its wheel, and in PATTERN it did nothing at all. A
notch either way opens CHOOSE INSTRUMENT on the step.

**PLAY in the list plays the sound's own preview phrase**, the one
INSTRUMENT's PLAY plays: C E G C an octave up, a note every 250 ms, a
rest, round and round, and a kit plays its drums instead. One note is too
short to judge a sound by, which is the whole reason the list is still
worth opening. Starting it stops the song, as INSTRUMENT's PLAY does.

**+ NEW**, the last row, as INSTRUMENT's `+ LAY` is the last of its
layers: it takes the first instrument slot no step of any block names and
nobody has named, puts it back to neutral (`song_instr_init`, now shared
with `song_init`), writes it on the step and leaves the list -- one SEL
more and its INSTRUMENT screen is open. The row carries the number it
would take, so he can see it before pressing; with all 64 gone it says
FULL in red and SEL takes nothing, rather than failing silently. The
whole thing goes through the journal: one ALGO notch takes the slot and
the step back together. The wording and the shape went to the sibling
session putting a list of blocks on SONG's own PRESETS knob, so that the
two lists read as one idea.

Left alone deliberately: the SELECTION and its ops, SEL on FX (the
step's syllable), what the keys do, and the list's own `--` row with the
marker that says what the step names now.

One thing the testing turned up that is not about instruments:
`pattern_test`'s STOP check used to pass because the player had never
actually started -- the command queue (eight slots, main loop to audio)
had overflowed long before and `CMD_PLAY_ENTRY` was dropped. The new
audition tests render audio, which drains the queue, and the test now
says out loud that the transport is at rest before it checks STOP.

## 2026-10-30 — SONG: a list of blocks again, behind PRESETS, for NEW

The panel of blocks that stood between the hand and the cell came out
this morning: its rows were bare numbers and said nothing KNOB 3 does
not. KNOB 3 still chooses and the ear still says which -- none of that
moved. What was missing was elsewhere: **a fresh block meant working out
which number was free**. A list that only names numbers is not worth a
screen; a list with somewhere to put NEW is, and he said so in those
words.

- **PRESETS opens it**, the one knob this screen had no use for. On the
  phrase's number, where there is no cell, it does nothing. SEL still
  opens the block in PATTERN and KNOB 3 is untouched.
- **Its rows: `--`, the track's blocks, then NEW.** A block is listed
  when it holds something or when the arrangement names it somewhere --
  the same reckoning KNOB 3's last block is made of (`block_used`), so
  the knob and the list never disagree about what this track has.
- **Beside each number, the two things a number never said**: `empty`
  when nothing is written in it, and how long it is, `16 steps` or
  `64 steps`. The tick at the left marks the block the cell holds now,
  as HOME marks the open project.
- **NEW takes the first number the track does not use.** A block
  shortened once keeps what lies past its length and never saves it, so
  the block it hands over is set to `PATTERN_DEF_STEPS` and rubbed out
  over that whole length first: new must mean empty. Both writes are one
  gesture of the journal -- one UNDO takes the new block back out of the
  cell.
- **With all sixteen used it says so.** The row reads `NEW all sixteen
  used` in grey, and SEL on it leaves `ALL SIXTEEN USED` in the corner,
  red. Nothing fails quietly.
- **HOME or GLO leaves and changes nothing.** The shape is CHOOSE
  INSTRUMENT's, down to the title line, the rule, the rows of 28 and the
  hints at the foot: where two screens do the same thing they say it the
  same way.
- **PLAY listens, after all.** It was left out at first -- the ear on this
  screen is KNOB 3 on the grid, and the panel had just been taken out for
  saying nothing new. He remembered that the old panel could preview and
  said that was what he missed, which settles it: the reason to remove it
  was the bare numbers, never the ear. So the preview of 29/10 comes back
  as it was, `preview_here` and all: PLAY turns the listening on, the block the
  cursor has rested SETTLE frames on plays alone and round and round, a
  quick walk down the list starts nothing on the way, `--`, an empty
  block and NEW are silence, and each row that can be heard carries its
  own play sign.
- **The ear borrows the cell and gives it back.** The player plays
  phrases, never a block on its own, so listening writes the block into
  the cell for real -- that is how the old panel did it and there is no
  other way. None of it is journalled: `ch_hush` puts the cell's own byte
  back on every way out, `ON` included (`ch_was`, not the block number --
  `ON` is not a block and would have come back as `--`), and
  `songview_cancel` is one of those ways out: holding HOME from the list
  restores it too. A solo MONO had put on is left alone, as the grid's ear
  already does. `dirty` stays 0 through a whole audition, checked on the
  simulator.

## 2026-10-30 — a page to build sampled instruments on the computer

Making a sampled instrument meant writing `inst`, `smp` and `zone` lines
by hand in a `.trk`. `web/songedit/` is the page that does it instead:
open a song, edit an instrument, drag its zones over a keyboard, hear it,
write the file back or send it to the FM-1. It is deliberately the
computer side only -- the device's own zone editing is a separate
question, and a 240x240 panel will never show a zone map the way a
browser can.

It rests on the division taken today: **samples are global, instruments
are not.** The bank is the machine's and holds raw sounds; an instrument
is the song's private configuration and lives nowhere else. So there is
no instrument library to edit, and the page edits a song file.

- **`trk.js` is a port of `core/trk.c`, not a reader of it.** The page
  writes the file itself, so the port can drift, so it is held against
  the C by `web/songedit/test/roundtrip.mjs` (in `make test`), the way
  `tools/installer_compare.py` holds `fm1ota.js` against
  `fm1_install.py`. Every `.trk` here and in `../moondaw/songs` -- 18
  files, 275 B to 27 726 B -- must come out of `trk.js` byte for byte as
  `build/trkdump` gives it. It did on the first run and the truncations
  are what made it: `cutoff` is a `uint8_t` and `filt_env` is `& 0x7F`,
  a field absent from an `inst` line keeps what the struct had (so a
  slot written twice carries the first line's `timbre`), and a `timbre`
  past `engine_timbre_max` is clamped on the way in -- a page that set
  one anyway would write a file that came back changed.
- **The test also edits.** Canonical is not enough: it proves the page
  can read, not that it can write something new. So each song is edited
  the way the page edits -- every played instrument's envelope, filter,
  gain, level, name, and for each SMPL one a sample swapped, a zone
  moved, one added, one dropped -- and the result must come back out of
  `core/trk.c` unchanged. 31 SMPL instruments across the corpus. The
  edited SMPL-DEMO goes through the real `build/trkdump` as well, not
  only through `trk.js`.
- **The page says the three things the file does not.** An instrument no
  step plays is not written at all (`trk_write`'s own rule) -- shown
  dashed. A zone naming a sample the bank has not got is silent and
  never an error (`core/bank.h`) -- shown red, once a bank is known. And
  the first zone holding a note wins, so the table's order is the
  priority and the rest of an overlapped zone will never sound -- that
  is the red on the map and the `6 of 46` in the KEYS column, which is
  the one thing the panel could never draw.
- **The audition is the core alone, not the firmware.** `platform/web/`
  already runs the whole machine in a browser, and that is the wrong
  instrument here: auditioning one zone means playing a chosen note on a
  chosen instrument, which through the panel would mean steering the
  screens. `web/songedit/audition.c` links `player.c`, `synth.c`,
  `fx.c`, `drum.c`, `sampler.c` and nothing else -- 455 KB of wasm, no
  app, no flash, no `platform.h` -- and exports a song, a bank and
  `player_note_on`. `core/bank.c` is the one piece not linked, because
  it reads the index through the store's sector map; the 60 lines of it
  the engine needs are in `audition.c`, and `bank_find`'s rules are
  copied word for word. Those two copies are checked against each other
  over 16 names (the folder, the `.wav`, the case, 23 and 24 characters)
  in the same test.
- **The bank twice over.** The index read over the link (`INFO`/`READ`
  on slot 32, as `fm1_samples.py list`) says which names resolve, and is
  enough to mark the zones. It is not enough to hear them: the bytes are
  160 KB and stay on the device. So a packed bank file from
  `fm1_samples.py pack` is asked for separately, and the page says which
  of the two it has.
- **Checked here, not on the device.** The page opens SMPL-DEMO, swaps a
  sample in the piano's five zones, moves one, drags a handle; the file
  it writes is 2 714 B and `build/trkcheck` reports `ok ... dropped: smp
  0 zone 0 ref 0 syl 0 fSMP 0 shaping 0 preview 0 cutsteps 0 lost 0`,
  with `trkdump` reproducing it byte for byte. The engine plays the song
  and single notes with the demo bank's 14 samples mounted. **Nothing of
  this has been on an FM-1**: the send path is `fm1_send.py`'s messages
  in `mdlink.js` and has never had a device answer it.
- **What it does not do.** No patterns, no arrangement, no song
  settings: they are carried through untouched and edited on the machine
  or in a text editor. No undo. No sending samples to the bank --
  `tools/fm1_samples.py` keeps that. A sample entry's `begin`, `loop`
  and `ping` are carried and not shown, since the FM-1 does not play
  them.

## 2026-10-30 — the page in front of the machine

`web/songedit/` had never seen an FM-1: it was written against
`core/trk.c` and checked against it. Two halves of it exist only on
hardware -- reading the bank's index over the link, and sending a song --
so both were run on the device, with its 20-sample bank and its 12 songs.

The index reads. The page says `20 samples, 159 KB, from the FM-1` and
names them in the order `tools/fm1_samples.py list` names them, durations
and all. That the list is the machine's and not the file's shows in the
zone picker: SMPL-DEMO names 14 samples, the bank holds 20, and the six
the song never mentions -- `tr9_crash`, `tr9_htom`, `tr9_ltom`,
`tr9_mtom`, `tr9_ohh`, `tr9_rim` -- are offered under the song's own.

The song goes and comes back unchanged. The piano's third zone
(`A#3 D#4 root C-4`) moved from `pf_c4` to `marimba_c4` and went over
WebMIDI as ZONETEST, 2714 bytes; `fm1_send.py --get` brought back 2714
bytes `diff` calls identical, and the page's own bytes and the ones
written here agree by SHA-256. On the device it is audibly not a piano
any more, and only just: the changed zone holds one note of three in
three of the four chords and two in the fourth, so what is heard is a
piano chord with a bell in it, which is what that zone means.

One thing the hardware taught that no test had. Dropping the last
reference to a sample drops its `smp` line, so the file went from 14
names to 13 and from 2738 to 2714 bytes -- and the page's song line said
2714 bytes beside `14 sample names`. Every other number on that line is
about the file as the FM-1 writes it; that one is now too
(`samplesUsed()`, the writer's own test).

## 2026-10-30 — KNOB 2 goes through the whole step, across the views

The marker at the top right has been promising an order since this
morning -- the notes, the instrument column, the effects, round again --
and KNOB 2 stopped at the edge of whichever view it was in. That was left
undecided then, to judge from use; he has now asked for it twice. His
words: "si on a le curseur sur le quatrième slot de note et qu'on tourne
encore un coup à droite, on passe dans le slot d'instrument, et si on
tourne encore le curseur à droite on passe dans les FX".

**One strip, and the cursor's place in it is the only truth there is.**

    #  N1 N2 N3 N4   #  INSTRUMENT   #  FX1 FX2 FX3

`cur_at` is that place; `view` and `cur_col` are read back from it and
nothing else writes them (`strip_read`). The clamp that held the cursor
inside its view is gone rather than widened, and with it a whole class of
disagreement between the two states it used to take to say where the
cursor was. The three views and their tables stay as they were: a view is
now which part of the strip the screen is showing.

**The step's number heads every group, and the two directions are not the
same.** This is the part he dictated on top, in the same breath as the
rest: going right the numbers are stepped over -- the fourth note reaches
the instrument, the instrument reaches `FX 1`, and the last effect comes
round to the first note -- and going left they are walked into, `FX 1`
reaching the number first and only the turn after it crossing to the
group before, where it arrives on that group's last column. It reads as
the screen does: the number is drawn at the left edge of the grid in
front, so one walks into it on the way out to the left, while turning
right is going further into the step and landing on the far left would
be a jump backwards. And the step as a whole stays one detent to the left
of the first column of wherever one is, as it always was.

**The three buttons keep their role**: SEQ, FX and ENV jump to the head
of their group, which is its first column, or its number when the cursor
was already on one -- exactly what they did before.

**The SELECTION does not cross.** While it is on, KNOB 2 still stops at
the edges of the group, its number included. Half a block of notes and
half of effects is nothing one could paste, and the clipboard's fields
are per view (`EDIT_CLIP_N1 << c`, `EDIT_CLIP_FX1 << c`,
`EDIT_CLIP_INSTR`). My call, told to him; he can contradict it.

**Verified in `tests/pattern_test.c`**, which reads the cursor's place
back from what the knobs and the keys do there -- a key writes in its own
note slot, KNOB 3 names the instrument, KNOB 4 the effect, and on a
number KNOB 3 is the block's length -- walking the strip right across the
two frontiers and round, then left through both numbers, and showing the
SELECTION held inside the effects (what it clears is the effects, the
note beside them surviving). Thirteen checks, of which eleven fail
against the previous `app/pattern.c` -- so they are about this and not
about the harness; the two that pass there pass for want of anything to
say, not by agreeing. Not on an FM-1: the device was out of bounds
(another session held the MIDI port) and he judges it on the panel
himself.

## 2026-10-30 — a kit's step says which drum, not which pitch

"Ecrire des noms de note qui font un effet de drum c'est assez penible.
Comment je sais que F# = le snare?" The firmware already knew:
`drum_note_abbr()` in `core/drum.c` turns a note into BD SD CP CH OH TM RS
CB, and nothing in the tree had ever called it.

Naming the cell is not enough on its own. `DV_TYPE_LANE` folds twelve drum
types onto **eight lanes**, and `lane_for()` keys on the lane, so a lane is
mono: in eight of the ten kits, fifteen different notes read `TM` because
they are one drum at fifteen tunings, not fifteen drums. The honest unit is
the lane — a kit is eight pieces, eleven for Felucca's own two, which name
their congas, claves and cymbals apart from the lane they play on. Every
piece of every kit has a note at the pitch it was designed for; the other
47 notes are those pieces retuned. That is `drum_drums()` and
`drum_drum_of()`, worked out once per kit and cached, with no C library in
core (an abbreviation is two letters, so the comparison is two letters).

**The keys are the kit's drums, one a key, and the lit ones say which.**
`edit_key_note` based a kit at note 29 and `gm_note()` folded anything under
35 back into 36..47 — the fold was bit-identical, so keys 0..5 played exactly
what keys 12..17 played, only eight or nine drums were reachable from
twenty-seven keys, and `CB` at its own pitch sat at note 56, out of reach
without OCT+. A third of the keyboard was a lie, which is why nothing on the
machine said where the snare was. Now key k is the kit's k-th drum, the
octave does not move them (there is nothing above or below to find), and
PATTERN passes the mask to `ui_led_mono`, so the eight or eleven that play
are lit and the rest are dark. `app/instrument.c` had its own copy of the
old arithmetic, commented "as edit_key_note"; it calls the function now.

**KNOB 3 steps from one drum to the next.** It moved the MIDI note by one,
so on a kit two notches out of three changed nothing audible: 128 notes hold
39 pieces in the model kits (45 in STD), the rest retuned or folded, and
`gm_note` brings the same twelve round three times. `OFF` keeps its rung
under the first drum in the first note slot only — giving the other slots
that rung walls them in, because the knob writes OFF, the step refuses it
outside slot 0 and puts `---` back, and `---` is where the cursor stays
however long one turns. `pattern_test` pins that, and fails without the
guard.

The cell reads the two letters, plus a `+` or a `-` when the note is one of
the pieces retuned, since the name alone could not tell those apart: `TM+`
is 29 px where the cell gives 34 and `C#3` took 30.

**Chosen on the panel.** Three variants were drawn on the machine
(`tools/protos/drum_names.py`, still there): A the strip with the drum
named, B and C the eight lanes as rows against one bar and two. He took A —
"on ne peut pas soudainement passer d'une vue verticale a horizontale". On
this machine time descends everywhere, in PATTERN's steps and in SONG's
blocks; B and C laid it across, so moving between the notes view and a drum
view would have meant turning the head ninety degrees each way. A mockup
judged on its own cannot show that cost; it is only paid in circulating.
So the drum is never identified by its place, only by the name in the cell —
which is why the name has to be right.

Not fixed, and he knows: A names the drums, it does not change the flow. The
four note columns are slots, not lanes, so `BD` still moves between them
depending on what else the step holds.

## Open

- SMPL, stage 2: CHOOSE SAMPLE and a SAMPLES page; INSTRUMENT still
  calls a SMPL instrument silent (`engine_sounds`) and cannot edit zones;
  `begin`, `loop` and `ping` carried, not played. On the computer
  `web/songedit/` now does the zones (30/10), which is where the map
  belongs; the device's own editing is the open part.
- `web/songedit/`: the editor page, lossless against `core/trk.c` over
  18 songs in `make test`. On the device (30/10, Chrome, macOS): the
  bank's index reads, and a song sent over WebMIDI came back from
  `--get` byte for byte (above). Not yet tried: a song the flash has no
  room for, a bank near `BANK_MAX`, Windows, Linux, Edge. Open to taste:
  `replace a song of that name` is ticked by default, so a name that
  collides overwrites -- the `confirm()` says so, but the safer default
  would be off.
- The voice (VOX): the screens that do not know it sounds (above);
  syllables are written on the device since 29/10.
- DRUM in moondaw (the same `drum_voice.inc`), then its songs on it;
  PATTERN naming a kit's notes by their drum (`drum_note_abbr` is there).
- `web/installer/`: adapted (29/10), checked byte for byte against
  `fm1_install.py` on a simulated FM-1 (`tools/installer_compare.py`). On
  the device (29/10, Chrome, macOS): Connect, Save songs (13 songs, two
  compared byte for byte with `fm1_send.py --get`) and Install over
  MOONDAW work. Not yet tried: back to V15 and installing from V15 (they
  erase the songs), the cut-off cable, Windows, Linux, Edge.

## 2026-10-30 — the root holds no song

He asked for it plainly: "quand je retourne sur l'écran d'accueil je ne
veux plus que le projet soit encore chargé en RAM. Si je retourne sur
l'accueil et que le projet est dirty c'est là qu'il faut me dire si je
veux sauvegarder. Une fois sur l'accueil le projet est déchargé."

The reason it matters is on the desk already: a song can sit in memory
changed and unsaved for as long as one likes, and nothing says so. That is
how a flash took his work on the 29th. Leaving is the last moment the
question can still be answered, so the question is asked there.

- `go_root()` is now the one way in: HOME held, and stepping back out of
  SONG, both go through it. Clean, the song is put down (`app_unload()`:
  the player stopped, `song_init`, `app_loaded` false). Not saved, the
  question comes up first.
- "Not saved" is `app_dirty || app_slot < 0`. A song that was never written
  has no place in flash to be picked up from, so putting it down without a
  word would lose it outright -- `app_new()` makes one that is not dirty,
  which is exactly the trap. Both read as "is not saved." to the hand.
- The answers, and the rule they set for every confirmation that comes
  after: **losing changes is always a hold, never a press.** SAVE keeps
  them, HOME held loses them, a press of HOME (a let-go before the hold is
  through) goes back into the song. A button hit on a question nobody read
  must not destroy. The question already in `menu.c` had `SEL  lose them`
  on a single press -- the very trap -- and now follows the same rule.
- The root hold is guarded against itself: `held == HOME_HOLD` no longer
  fires from `AT_MENU`, or the hold that answers the question would keep
  putting the question back up, forever.
- `CONTINUE` picks the song up again from its slot (`continue_song()`), and
  so do RENAME and DUPLICATE, which work through the song in memory and
  found none there.
- `ASK_OPEN` / `ASK_PLAY` stay: the hand can no longer reach PROJECTS over
  unsaved changes, but `app_load()` -- a song pushed in over USB -- still
  lands changed with the hand at HOME. Their coverage in `menu_test.c` went
  with the path the hand used to take.

Not done: choosing the answer with the SELECT knob, which he also asked
for. The buttons are in; the knob and its cursor are not.

## 2026-10-30 — the effects' two knobs, and the question made plainer

Both from him, on the panel, within the hour of the last entry.

- In FX, KNOB 3 turned the value and KNOB 4 the effect; the cell reads the
  other way round. "j'aurais fait l'inverse vu qu'en ordre d'affichage
  c'est d'abord le type d'effet puis la valeur." Swapped -- and it is more
  than reading order: KNOB 3 now says *what* the cell holds in all three
  views (the note, the instrument, the effect) and KNOB 4 *how much*.
- The hold to lose changes, written an hour earlier as the rule for every
  confirmation to come, is already gone. "le home maintenu est inutile vu
  que de toute façon tous les autres boutons annulent déjà." He is right
  that eleven buttons of twelve already cancel; the hold bought one thing,
  the reflex of tapping HOME twice when the first press seems not to have
  worked, and that is the one case it now costs. His machine, his call.
  So: SAVE keeps, HOME again leaves without keeping, anything else stays.
  Nothing counts until the HOME that reached the root has been let go.
- "anything else" was only the buttons and the SELECT knob: the keys and
  the other knobs did nothing, under a line that promised they would.
  "quand tu dis anything else ça doit être anything else non?" -- it does
  now. A screen that states a rule has to keep it.

## 2026-10-30 — five more from the panel, in one sitting

All five his, found by using the machine rather than by reading it.

- **FX before the instrument column** on the KNOB 2 strip. "on change plus
  souvent les FX que l'instrument", so the order follows how often the hand
  goes there and the rare one sits at the far end. `strip_order` and the
  marker's `view_next` are the whole change; the strip's own code did not
  move, which is what making the cursor the only truth bought us.
- **PRESETS keeps driving the list it opened**, in CHOOSE PATTERN and in
  CHOOSE INSTRUMENT, exactly as SELECT and KNOB 1 do, ear included. What
  opens a thing should keep driving it, or the hand is made to change knob
  for nothing. The detent that opens the list no longer walks it as well --
  SONG returned early for that; PATTERN already ran its list before the
  opener, so it needed nothing.
- **A kit's keys are lit on INSTRUMENT too**, as they are in PATTERN:
  `edit_key_drums` was already there and `ui_led_keys` only had to stop
  being cleared for that screen in `app.c`. This is where a kit is
  auditioned, so it is where the question is asked first.
- **DELETE is red in PROJECTS' EDIT panel** wherever the cursor is, not
  only under it: the one row of that panel that does not come back.
- **The namer is KNOB 1 and KNOB 2**: the letter, then what it is. They sat
  a knob further along to keep SELECT and KNOB 1 the cursor as every list
  has them -- "je sais la logique que tu as implémentée [...] mais ce n'est
  pas intuitif car KNOB 1 ne fait rien, tout est décalé". On a row of
  letters the first knob is the one the hand reaches for. Three test files
  drove the old knobs; one of them, PROJECT's "HOME leaves the name as it
  was", would have passed by typing nothing at all.

## 2026-10-31 — ten sounds in the first slots

The first complaint that was not about a screen: there are no presets,
and it is not fun. Three days on the panel against Skiff, which opens
with six tracks already sonorised, and the verdict was not about capacity
-- MOONDAW holds more of everything -- but about the first thirty
seconds.

- **`song_init` laid the same neutral sustained pulse in all sixty-four
  slots.** A track taken fresh was a drone to be sculpted, filter and
  envelope by hand, before it was music. The function's own comment
  had been admitting it for weeks: "moondaw puts ten presets in the first
  slots; they are not carried yet." They are now -- BASS SUB LEAD KEYS
  HARP PAD ORGAN BELL VOICE KIT -- eighteen bytes a row over the neutral
  ground, which `song_instr_default` still lays underneath.
- **The chooser was already built for this.** PRESETS on the INS column
  opens CHOOSE INSTRUMENT, which previews each slot on PLAY with its own
  four-note phrase, or a kit's drums. Nothing there needed writing: it had
  been listing sixty-four identical pulses. The whole change is data.
- **Measured, not guessed.** `core/player.c` linked natively against a WAV
  writer, so every number was heard and then read back: peak, RMS, attack
  and release in milliseconds, spectral centroid. Levels were set against
  the chooser's own phrase, since that is where a sound is met first, and
  checked again on a held triad so that three notes of one preset do not
  lean on the master limiter. A dense four-track mix still reaches that
  ceiling -- that is the machine's design, and trimming the presets seven
  dB to avoid it would have made one note sound weak, which is the very
  thing being fixed.
- **PAD's attack came down from 800 ms to 220.** At 800 it never arrived
  inside the chooser's 250 ms note: the preset that read PAD previewed at
  a twelfth of the others and seemed broken exactly where a newcomer meets
  it. A preset has to work in the place it is auditioned.
- **A file's `inst` line now starts from the neutral ground**, not from
  whatever the slot held. `parse_inst` reset most fields and let the rest
  stand, which was harmless while every slot began identical. With presets
  underneath it was not: the writer leaves out a LEVEL of 0 and an empty
  name, so a saved song reloaded with the preset's level and the preset's
  name in slots the file had named itself. Thirteen round-trip failures.
- **`make test` reported that as a pass.** The last line of the target is
  `node web/songedit/test/roundtrip.mjs | tail -4`, and a pipeline exits
  with `tail`'s status, so thirteen real failures came back green. The
  regression was found by reading the output, not by the suite failing.

Then his second look at it, the same evening: a preset could have a demo
melody of its own, and the bass should be shown on lower notes.

- **A phrase per sound, and it lives in the sound.** `tune[] = {60,64,67,72}`
  sat copied in `app/instrument.c` and `app/pattern.c`, so every one of ten
  presets was auditioned on the same four notes in the same octave -- a bass
  judged at middle C, a bell judged where a bell has nothing to say. Now
  `song_instr_demo` in `core/song.c` hands each one its own register, its own
  rhythm and its own note length, and both screens ask it rather than holding
  a copy. BASS plays C-2 and an octave off it, SUB sits on A-1, LEAD is up at
  C-5, HARP runs up with no rest so each note decays under the next, BELL is
  struck twice high and left alone, PAD holds notes of 1100 ms.
- **The phrase follows the preset, not the slot.** Its own while the slot
  still carries that preset's engine and name; rename it or turn its engine
  and you have taken it over, so it falls back to the four notes the screens
  always played -- or to a kit's drums, which follow the engine wherever they
  are, since on a drum map it is the engine and not the slot that decides
  what a note means.
- **PAD's attack came back.** It had been cut to 220 ms that afternoon only
  because the chooser's 250 ms note was all a sound ever got; with its own
  1100 ms notes the swell has room again at 512 ms, which arrives at about
  half the note and then holds. Measured, not assumed: all of 220, 378, 512,
  648 and 800 ms reach the same peak inside that phrase, so the number is
  free to be the musical one. The compromise was never about the sound, only
  about where it was heard.
- **`step_ms` is a divisor**, so a row that lost it would take the screen
  down rather than merely sound wrong. `song_instr_demo` refuses a phrase
  without one, and the test walks every preset's notes for NOTE_MIN.

And his third pass, on three of them: propose better for SUB and PAD, and
could BELL not play the little London-tower chime? (The tower is the
Elizabeth Tower at Westminster -- Big Ben.)

- **A phrase can hold a chord.** The honest answer for PAD was not a better
  melody, it was that a pad held one note at a time is not a pad: it is a
  slow note. `Demo` gained a `chord` count, read in groups struck together
  through `player_note_chord` -- the same path a step with several notes
  takes, so the preview is what the step would sound. PAD now swells
  through two triads, C-3+G-3+C-4 into A-2+E-3+A-3, 1600 ms each. ORGAN and
  KEYS could take the same treatment later; they were left alone for now.
- **That cost PAD three dB.** Three notes at once are three times the
  energy: at -4 its row came through the chooser at 0.83 against 0.25 to
  0.64 for everything beside it, which is a jolt when walking the list.
  At -7 it previews at 0.59, and removing it from a four-track mix still
  drops the RMS by eight per cent and the centroid from 1003 to 871, so it
  is there. The number follows the phrase: one note at a time it wants -4
  back, and the table says so.
- **SUB walks down instead of sitting still.** E-2, C-2, A-1 and a rest,
  520 ms a step: a sub is weight and nothing else, so the phrase goes to
  the bottom in three steps and ends on the lowest note it has. The old
  A-1 / rest / E-2 / rest showed two isolated notes and no range.
- **BELL strikes the Westminster quarters**, his ask, and it is the
  quarter-past phrase the tower really strikes on its four bells: G#4 F#4
  E4 B3, then room to ring. Not a tune picked for being pretty -- it is
  the one a bell is known by, and the drop of a fourth onto B3 at the end
  is the part the ear recognises. Two rests after it, because the sound
  rings 1.2 s and a chime that starts again over itself is a doorbell
  fault, not a chime.

## 2026-10-31 — the SECTION, made by hand (and SECTION becomes the group)

The model has carried sections since the first day — `sec 01`, `name
CHORUS`, `song_sections_normalize()` keeping them whole when rows move,
`REF SEC` playing one, CHOOSE PHRASE listing them by name — and nothing in
`app/` ever wrote one. They could only arrive in a file, so a song composed
on the machine could never have a section at all. He saw the gap the hour
after the REF landed.

**The word first.** `SEC 07` in the corner of the steps screen meant the
*phrase* 07; `REF SEC 02` on SONG meant the *group* 02; HELP's SONG page
said "the section" for one and "a whole section" for the other, eight lines
apart. Three letters, two meanings, two adjacent screens. SECTION goes to
the group — the file's word, and a musician's — and the screen of one
phrase's steps becomes **PHRASE**, which is what SONG, CHOOSE PHRASE, the
help and the file already called that row. Its corner reads `PHR 07`, the
same abbreviation CHOOSE PATTERN's header already used. No logic moved: a
corner, a topic in HELP, a few help lines, a README caption, and the
comments that named the screen.

**ENV, not PRESETS — his.** I proposed PRESETS on the number; he said no,
"le bouton preset n'a rien à voir avec des sections", and proposed ENV:
*envelopper* la sélection. He is right beyond the pun. PRESETS opens the
list of what is UNDER the cursor, and a section is not under the cursor, it
is around the selection. ENV is a **verb** here, and CHOOSE SECTION is the
one question the verb has to ask; PRESETS keeps its single meaning. ENV was
free on SONG (only PATTERN reads it), and it works wherever the cursor
stands in the row, his ask: a section is an affair of rows.

The list is `--`, the song's sections with their names and how many
phrases each holds, and NEW. It opens on the section those rows are in, or
on NEW when they are in none — the same courtesy CHOOSE PATTERN pays by
opening on the block already in the cell, so the common path is ENV, SEL,
type. NEW opens the namer at once: a section is born named, and HOME there
leaves it UNNAMED, which the file allows.

**The marker — his, against my rules.** I had drawn a cyan rule where a
section opens and closes. He wanted what the app and the web page do, and
he was right: a framed tag in amber with the name, a hairline to the right
edge, taking a row of the grid. Lifted from `SongScreen.swift:673` and
`web/ui/song.css:213` (settled there 25/08) down to the air it breathes,
more above than below. CONDENSED 20's capitals are 15 px tall, which is
what centres the name in the frame — my first try put the baseline one
pixel above the floor of the tag and he saw it at once.

Taken from the same place and not asked for: **membership is an INK, not
an indentation** (`.pos.insec`, and the app's note of 28/08 on why —
indenting a member would slide its cells out from under the header). A
phrase in a section wears its number in the same amber. It also pays for
itself twice: it says where you are when the marker has scrolled off, and
it means the corner never has to carry the name, which did not fit beside
AUTO.

The grid stops being "one visible row, one phrase": `lay_out()` builds the
eight slots, `slot_of()` finds a row's, `follow_marked()` pushes `top` down
until the cursor has one. `grid_frame` was split into `grid_rule` (one
row's) and `grid_bar`, so SONG can leave the rule out where a marker
crosses the width — he found it crossing the name.

**And the marker is a place.** He asked for two prototypes rather than a
question, which was the right call: the difference is one detent and it had
to be seen. The cursor stops on it, the tag goes green, and the gestures
land on the thing under the cursor — **KNOB 4 how many phrases it holds**
(right swallows the free row below, left gives the last one back, and it
never takes a phrase that belongs elsewhere), **SEL its name**, **EDIT held
+ OP4** clears it away and leaves the phrases.

**Not LFO, his call.** I had put the dissolve on LFO twice, reasoning that
LFO already deletes whatever is under the cursor. He was not convinced and
proposed OP4, and the better argument is his: LFO exists to ask twice
before something is *lost*, and a section dissolved loses nothing. Worse,
LFO on a marker and LFO on the phrase one detent below would wear the same
red band and the same two presses while one removes a phrase and its blocks
and the other removes nothing. OP4 already means "clear what is under", the
black keys are the commands, and LFO goes on meaning one thing here.

**A REF SEC row is not a group.** Reading a file gives it a section of its
own (`core/trk.c`): one row pointing elsewhere. It wears no marker, it is
kept out of the list, and a run stops before it — a group that swallowed
one would be taken apart by the next load, and a song that changes when you
reload it is the worst kind of bug to leave in a file format.

`song_test` drives all of it, the save and the load included. `proto/
sections.html` is the record, pictures out of the framebuffer as `ref.html`'s
are.

Open: whether a marker should light as the song plays through it
(`player_entry()` reports a row, and a marker is not one); and a SECTIONS
screen, the song's shape at a glance, which is a screen and not a gesture.

## 2026-10-31 — the SELECTION moves to OP5, and EDIT is a shift again

His, from using the machine: "quand EDIT est sélectionné je n'aime pas que
ça soit en mode de sélection par défaut", on SONG as on PATTERN, with the
fix already in it — one of the black keys we have left, acting as a toggle.

He is right, and for a reason worth writing down. EDIT meant two things on
those two screens and what separated them was `EDIT_TAP`: fifteen frames, a
quarter of a second the hand cannot see. A threshold nobody can perceive is
a coin toss — hold a hair too short and you are in a mode you did not ask
for, a hair too long and you are not in the one you wanted. Every other
mode on this machine is entered by pressing something, never by pressing
something *for a certain length of time*. And it gives EDIT back the single
meaning `ui.h` has always claimed for it: a shift, the same on every screen
that has one.

**OP5**, the only free OP, his pick over PIT and POLY. It is where it
belongs by family — the SELECTION is what OP1 to OP4 act on — though it
does land next to OP4 with nothing to do with clearing, which is what OP5
had been kept free for on 29/10. The synth words said nothing about a
selection and sit further under the hand.

The asymmetry is in the grammar, not a wart: **EDIT held + OP5** to turn it
on, because a black key alone is a note on both screens — it writes in
PATTERN and plays the track's instrument in SONG — and **OP5 alone** to
turn it off, the keys being commands already once the mode is on. Two hands
in, one hand out.

The CAPS LOCK light moves with it. EDIT lit while the SELECTION was locked
(30/10); now EDIT is never lit and OP5 carries the state through
`ui_led_keys`'s `lit`. The key that opens the mode is the key that says it
is open, which is what a CAPS LOCK has always been. `ui_led_mono` was the
wrong shape for that — it only ever lights MONO — so both screens call
`ui_led_keys` directly and fold MONO's solo and mute into the same call.

EDIT tapped now does nothing at all on SONG and PATTERN. PHRASE and the
MIXER are untouched: their tap opens the block and goes to the PROJECT, and
neither ever locked a mode.

`pattern_test` and `song_test` drove the old tap sixteen times between
them; they drive EDIT + OP5 now, and each gained a check that EDIT alone —
tapped or held past the old threshold — leaves the SELECTION exactly as it
was. The LED check that read "lit once a tap locks it on" now reads OP5's
own lamp.

## 2026-10-31 — EDIT tapped is a CAPS LOCK again, on the shift alone

Same evening, from the panel: "pourquoi le EDIT n'est plus togglable comme
un CAPS LOCK dans SONG. Avant on pouvait, maintenant il faut rester
enfoncé." And then, when I had answered the wrong question: "tu as mal
compris je n'aimais pas qu'appuyer sur EDIT activait le mode de sélection
de plage."

He is right and I had fused two things he was keeping apart. The old tap
latched *two* modes at once — the black keys become commands, **and** the
cursor anchors a range — and only the second was the one he refused. This
morning I removed both and sent the lot to OP5, so I took away a caps lock
he wanted to keep. The nuisance was never the latch. It was what the latch
dragged in with it.

So the tap comes back, on the shift and nothing else. **EDIT tapped**: the
black keys stay commands and the keys sound nothing, until EDIT is tapped
again. **EDIT held**: the same, momentary, as before. **OP5**: the
SELECTION of a range, on top, reachable now with the shift held *or*
latched. `UiShift` carries a `latch` and `ui_shifted()` reads it with
`held`, so the screens stopped saying `shift.held` and say what they mean.

The argument I made this morning dissolves, and that is the point. It held
because a hold a hair too short dropped you into the SELECTION — another
mode, with an anchor, which changes what the encoders do. Now both outcomes
of a clumsy press are "the black keys are commands": one that sticks, one
that does not. You can be wrong about the *duration* of the mode, not about
the mode. So `EDIT_TAP` is gone entirely, and with it `UiShift.frames`,
which nothing had read since this morning. What tells a tap from a hold is
whether a key was used while EDIT was down — the thing the hand actually
did, not how long it took to do it.

EDIT lights again, and only while the latch is on: a shift under the finger
says itself, a shift that sticks has to be told. It goes in the screens'
`usable()` mask rather than through `ui_led_state`, so `ui_led_guide` owns
it like every other button and nothing fights over the lamp. OP5 keeps the
SELECTION's own light.

The cost I did not pay: with the LEDs off there is nothing on the screen
that says the latch is on. I left it there rather than invent a corner word
he has not seen — SONG's corner is already the one number KNOB 4 turns, and
PATTERN's already says SELECT. To be judged on the panel.
## 2026-10-31 — the battery on HOME, and what its number is worth

- **The bar.** Three proposals shot from the firmware (`proto/home.html`),
  C chosen on the panel: the wordmark left, the charge as a figure AND a
  cell, a rule under them at y=28. The rows start at y=32. The foot gave
  up the version (the bar says MOONDAW now) and `MAX` (SYSTEM holds the
  same number as CPU MAX); the profile line while a song plays stays, and
  PROJECTS, which has no bar, keeps the version.
- **`plat_battery(int *raw)`** joins `plat_audio_load()` on the platform
  surface: 0..100, or -1 where the machine does not know, which is what
  SDL and the browser answer. **-1 draws no gauge at all** — not an empty
  cell, which reads as a flat one, and not a dash, which reads as a
  fault.
- **It is polled once a frame and only on the root.** A screen is DRAWN
  fifteen times for one frame, once a band; a gauge read from a draw
  would be fifteen conversions on the ADC the MASTER pot shares.
  `tests/menu_test.c` counts the reads so it stays one.
- **The read.** PB1, SARADC channel 3 (`fm1_adc.h`), every sixteenth
  frame, the conversion right after the channel moves dropped, then the
  same `/8` filter the MASTER pot runs through.
- **BATT_MV_FS = 6600 IS AN ASSUMPTION, NOT A MEASUREMENT.** It is the
  cell voltage that would read full scale: the converter's reference
  times the divider on PB1. Nothing in the chip or in Felucca documents
  either, so 6600 is the ordinary wiring — a 2:1 divider on a 3.3 V
  reference. Measured on the machine on 31/10: **625 counts**, flat to
  the count over three minutes and under a 52 % audio load (which proves
  less than it looks: one count is 6.45 mV and the sag under that load is
  smaller than that). 625 counts under the assumption is 4.03 V, so the
  panel reads 84 %.
- **What the number is worth: the shape is right, the offset is not
  pinned.** The plausible family is a 2:1 divider on a 2.95–3.45 V
  reference, so 625 counts is somewhere in 3.60–4.21 V. Near the flat
  part of a lithium curve that is tens of points.
- **How to pin it, in one measurement.** `battraw=` is on the link beside
  `batt=` for exactly this. Put a voltmeter on the cell, read
  `fm1_ctl.py status` at the same moment, and
  `BATT_MV_FS = mV * 1023 / raw`. A second point near empty would also
  check the curve itself. Until then the gauge is a gauge, not an
  instrument.
- **Charging is not detected.** No VBUS sense goes through the HAL, so a
  cell that is filling looks like a cell that is full.
- **One point, taken on a full cell: 623 counts.** Jaeger charged the
  machine to its charger's own green lamp, unplugged it, put the XIAO
  bridge back on and asked. Six `fm1_ctl.py status` in a row, fifteen
  seconds: `batt=83 battraw=623`, flat to the count. His panel had read
  85 % earlier in the session, which is 626 counts, and the note above
  recorded 625: the reading is walking down, three counts over the day.
  **The read is NOT a cell in charge.** The bridge carries D+, D- and
  GND and deliberately no VBUS (ARRIVEE.md), so the FM-1 is on its
  battery whenever the link is up. The green lamp is the charger's, on
  another cable, before.
- **So the point is open-circuit full, which is what BATT_CURVE's top
  already means** (4190 = full, at rest). `BATT_MV_FS = 4190 * 1023 /
  623 = 6880`, against the 6600 that was assumed. At 6880 the gauge
  reads 99 % on 623 counts -- one millivolt under the top, lost to the
  integer divide; taking the cell at 4.20 V instead gives 6897 and 100 %.
- **6880 is a FLOOR, not an estimate, and that is the finding.** The
  counts were still falling when they were read, whether the cell was
  still relaxing or the machine was simply draining, so the settled
  full-charge reading is 623 counts or fewer -- and fewer counts for the
  same 4190 mV means a HIGHER full scale. 6880 already implies a 2.95-3.45
  V reference at **3.44 V** on a 2:1 divider, ten millivolts under the
  ceiling of the plausible family; 620 counts would put it at 3.46 V,
  past it. The assumption that is most likely wrong is therefore not the
  reference's value but the shape of the wiring: a divider that is not
  exactly 2:1 would explain it without pushing the reference off its own
  range. One point cannot separate the two.
- **What is still owed: the voltmeter.** `BATT_MV_FS = mV * 1023 / raw`
  with mV read on the cell at the moment of the status, as the note above
  says. The cell is near full now, so the useful one to take is a second
  point near empty: it pins the divider and checks the curve at the same
  time. Until one of them is taken the constant stays at 6600 -- moving
  it to 6880 would trade a number that is known to be a guess for a
  number that looks measured and is not.
- **What the other firmwares do: none of them converts counts to volts.**
  Looked at all three on 31/10.
  - **Felucca 1.8** (`firmware/src/ui_draw.c batt_level`) has no millivolts
    anywhere. It is three raw thresholds on ch3 -- `>= 591` three bars,
    `>= 561` two, `>= 531` one, under that empty -- and its comment calls
    them "the stock thresholds": they were taken off M-VAVE's own closed
    firmware, not derived. Its read is slower than ours too (once a
    second, `/32`, `kt_adc_batt` pausing the key scan for the one
    conversion) where we take one every sixteenth frame with `/8`.
  - **Mycelium FM-1** (community, built on Felucca) carries the same
    three numbers over verbatim, down to the `/32`.
  - **FM1 Open**, M-VAVE's OWN open firmware, does not use the value at
    all. `sdk/include/fm1.h` publishes it as `uint16_t knob, battery; /*
    ADC values, 0..1023 */`, `app/board/panel.c` fills it from
    `fm1_device_adc(3U)`, and nothing reads it: no gauge, no curve, no
    scale. The only number in the tree is `controls.battery = 900` in the
    web simulator, a stub beside `knob = 1023` -- it is not a
    measurement, and 900 counts would put the full scale at 4763, outside
    the plausible family. Do not read it as one.
  - So the constant we need is one nobody upstream ever needed: we are
    the only one of the four that claims a percentage. The divider ratio
    and the reference are documented nowhere because no one else converts.
- **But the stock thresholds ARE evidence, and they point at 6880.** Run
  531 / 561 / 591 through `BATT_CURVE` and ask what full scale makes a
  four-level gauge evenly spaced -- the one thing M-VAVE certainly did
  intend when they picked them:
  | BATT_MV_FS | thresholds, in % | the four bands |
  | --- | --- | --- |
  | 6600 (assumed) | 4.6 / 11.2 / 52.6 | 5 / 7 / 41 / 47 |
  | 6857 (most even) | 8.5 / 40.0 / 75.9 | 9 / 32 / 36 / 24 |
  | 6880 (the measured cell) | 8.9 / 43.2 / 77.8 | 9 / 34 / 35 / 22 |
  At 6600 the bottom two bars share 12 % of the cell's life and the top
  two share 88 %: nobody draws that gauge. The evenest spacing lands at
  **6857**, and the full-charge point of the entry above lands at
  **6880** -- two estimates from unrelated evidence, 0.3 % apart, about
  one sixth of an ADC count. That is the corroboration the last entry
  said one point could not give.
- **It also settles which assumption was wrong.** 6880 is the composite
  (reference x divider ratio), and only that composite matters to the
  gauge; the split does not. The note above read the composite as a 2:1
  divider on a 3.44 V reference and worried that 3.44 sat at the ceiling
  of the plausible family. The thresholds say the ceiling is where the
  truth is: it is the 3.3 V "ordinary wiring" guess that was wrong, not
  the range.
- **Applied: 6880, and round instead of truncate.** At 6880
  a full cell at 623 counts reads 99 %, one millivolt short, lost to
  `r * FS / 1023` flooring. Rounding it -- `(r * FS + 511) / 1023` --
  gives 4190 mV exactly, the curve's own top, and 623 becomes the first
  count that reads 100 %. Bending the scale to 6897 to get the same 100 %
  would be fitting the constant to a rounding error. The voltmeter is
  still owed: this is three agreeing inferences, not a measurement of the
  divider -- and it is not being taken, because it means opening the
  machine and Jaeger would rather not. What replaces it costs nothing:
  the gauge reaches 0 % at 486 counts, so ONE real discharge says whether
  the scale is right. If the FM-1 dies announcing a few percent, it is;
  if it dies announcing a quarter full, it is not, and the fallback is
  the upstream's -- bars off raw thresholds, no volts claimed, which
  would cost the figure on HOME that was chosen on the panel.
- **What covers it, and what does not.** `tests/menu_test.c` stubs
  `plat_battery` out (it counts the reads), so NOTHING in the suite
  exercises the arithmetic: the conversion lives in `firmware/src/firmware.c`,
  which is only ever built for the device. The screen test still passes
  and the firmware still builds (229 104 B, identity FM-1_900). The
  arithmetic itself was checked by pulling the real `#define`, the real
  `BATT_CURVE` and the real `plat_battery` out of the file with `sed`
  into a native harness -- not retyped -- and running the table above
  through it: 486 -> 0, 492 -> 1, 531 -> 8, 561 -> 43, 591 -> 77,
  620 -> 98, 623 -> 100, and -1 while nothing has been read. It agrees
  to the point. **Not yet seen on the panel: nothing has been installed.**

## 2026-11-01 — 0.1 goes out: what the hashes said, and what a build id is worth

The question was whether the firmware can go out as a public beta now.
The repository was already public and the README already said *Beta*, so
what was left was the thing nobody had looked at: whether what is
published and what is in the tree are the same firmware.

- **They were not.** The package served from `moondaw.com/install/` and a
  build of `main` differ over **101 900 of 610 257 bytes**. The site's
  `site/install/` is gitignored — it is assembled on the way out by the
  other repository's `tools/site/build.sh` from whatever
  `build/moondaw.fwsc` happens to sit in this one — so nothing anywhere
  recorded which commit the public package came from. A bug report could
  have been about code that is not in the repository.
- **What makes that number readable is the build id.** `build_id()` in
  `firmware/src/firmware.c` is an FNV hash of `__DATE__ __TIME__`, used
  twice, so the image carries **8 bytes that change with the minute**.
  The package's cipher is a keyed XOR per 32-byte block, which is
  positional: those 8 bytes, plus the CRCs and entries around them, come
  out as **22 differing bytes and no more**. Two builds of one tree, at
  different paths and in different minutes: 22 bytes. The published
  package against the same tree: 101 900. So the comparison is not
  "firmware is never reproducible", it is **22 means the same code and
  another minute, anything larger means other code** — worth keeping,
  because it is the only check there is until someone wants reproducible
  builds enough to drop the build id.
- **So the release is one package, built once, used twice.** `v0.1` is a
  tag on `main`; the `.fwsc` built from it is what `make_site.py` puts
  behind the install page AND what is attached to the GitHub release, so
  the manifest's sha256 and the asset's are the same string and both
  point at a commit. That is the whole of what was missing.
- **`make test` could not fail where it mattered.** A pipeline exits with
  its last command's status, so `trkcheck | cut` and `roundtrip.mjs |
  tail` reported their own failures as passes — the thirteen round-trip
  failures of 31/10 came back green, found by reading the output. Both
  write a log and read it back now. The suite on `main` after the fix:
  green, 13 songs through trkcheck, 18 files round-tripped, 0 failures.
- **The README's status line said "used every day", which is not true.**
  It is built and tried on the machine every day; nobody has lived with
  it. The install bullet now points at the live page rather than at a
  directory to serve yourself.
- **The cold build crashes the toolchain, and a second run fixes it.**
  Twice out of two cold builds, `pi32v2/bin/clang` died on the loader
  (the crash the `.gitignore` already has a line about, since it drops a
  `core` file at the root); the retry, incremental, went straight
  through. `tools/build.py` compiles through a `ThreadPoolExecutor`, so
  a race in the emulated toolchain is the suspect. Anyone building from
  a clone will meet it on their first try.
- **What is still owed, and it is not code.** Nothing has been installed
  on the machine since the ten presets landed, so they have not been
  judged by ear, and the battery gauge on HOME — three agreeing
  inferences, no voltmeter — has never been seen on the panel. And the
  first thirty seconds a stranger gets, DEMO loading on a virgin flash
  (`app/app.c`), is the one path the author's own machine can never
  show: its store has songs in it. That is the test to run before the
  link is handed to anyone.

## 2026-11-01 — the first thirty seconds, lived on the machine

The one line of the release notes written from the code rather than from
the panel: *on an FM-1 whose flash holds no MOONDAW songs, the firmware
opens a built-in demo song*. It has been run now, on the device, after
0.1 was flashed and published.

- **The twelve songs came off first** (`fm1_send.py --get`), into a
  sibling folder, and every one was put through `trkcheck` before
  anything was erased: 12 files, byte-stable in and out. The sample bank
  was left alone — the boot path looks at song slots only, so wiping the
  bank would have cost twenty samples and proved nothing.
- **Deleting is a panel gesture, not a link command.** There is no delete
  over the USB link: it is PROJECTS, EDIT on a project, DELETE, then SEL
  on "Delete it for good?". Driven from the computer twelve times, then
  the same package reinstalled to force a clean start.
- **What a stranger gets.** HOME, the cursor already on CONTINUE, the
  song reading **DEMO**, PROJECTS reading 1. SEL and PLAY: `playing=1`,
  CPU 16 % of 22 max, the SONG screen showing INTRO and VERSE over BASS
  LEAD KIT KIT. **Three presses from power-on to sound**, with the ten
  presets already in the instrument slots behind it. The verdict of
  31/10 — menus before any sound — does not describe what a new machine
  does any more.
- **Then everything was put back**: the twelve songs in their old order,
  the same names and sizes, 11 KB free as before, DPUNK-EN-DRM open
  again, the twenty samples untouched.
- **A note for anyone driving the panel from a script.** Back-to-back
  `fm1_ctl.py` invocations fail — each one opens and closes the MIDI
  port, and the next one comes too soon. They exit non-zero and say so,
  so a shell loop that sends output to `/dev/null` deletes nothing and
  reports twelve successes. What works is one process that paces the
  calls and retries, and that checks the song count after each delete
  rather than trusting the gesture.

## 2026-11-01 — OP1 and OP3 lit with nothing selected

Since EDIT became a shift alone (31/10), OP1 and OP3 work with no
SELECTION: a slot in a column, the whole step on its number. Their LEDs
still waited for a selection, a rule from 29/10 nobody went back to, so
two keys that worked looked dead, and he took them for broken. Lit now
whenever EDIT is down, in PATTERN and in SONG.

**The number takes the whole step, not what the view shows.** His question,
my advice: the number already means the step as a whole, and a chord in
`ARP 01` has to stay an arpeggio once pasted. Four notes alone are still
an OP5 selection away. Since NOTES hides what was taken with them, a
single step says `COPIED STEP` rather than `COPIED 1`.

## 2026-11-01 — ARP: the chords of the key

ARP had done nothing on PATTERN since UNDO went to ALGO. It now opens
CHORDS, on the notes of an instrument that is not a kit (app/chords.h), and
PROJECT gains a KEY row, C MAJOR to B MINOR, `key A minor` in the file and
left out at C major. Nothing else listens to the key: no note plays
differently for it.

I had refused this the same night, on two grounds. The menu: the verdict of
31/10 was about menus before any sound, and a list that sounds on every notch
is the ear choosing, as the INS column and CHOOSE INSTRUMENT already do. That
objection did not hold. The label held: a list of chords behind ARP is the lie
refused for the SELECTION on 29/10. What settles it is that FX_ARP has no
intervals of its own, it walks the notes written on the step, so choosing the
chord is choosing what it walks; and KNOB 4 on this screen is that arpeggio,
heard as it is set, written with the chord by SEL. Without KNOB 4 the label
would lie again.

The list opens silent, on the panel's verdict: PLAY arms it, and it stays armed,
so no chord needs a press of its own; PLAY again disarms. Armed, a chord rings
for as long as the envelope holds it, the arpeggio goes round, and nothing cuts
it on a timer. A filled sign on the row says which is sounding.

CHOOSE PATTERN's "x steps" sat on the number's baseline, and being small it hung
2 pixels under the number and the play sign, both centred on the row. Measured on
a capture: the sign was right, the text is what moved.

- **Sections, likeliest first, fixed.** Triads, sevenths, sus and add, borrowed,
  secondary dominants: 34 chords a mode. The order does not look at the phrase
  so far, so the same turn finds the same chord. Four notes at most, what a step
  holds. Sharps only, as the notes are spelled on the grid.
- **The keys play it by position.** The first white key is degree I of the section
  whatever the song's key, so the hand learns 1-5-6-4 once; C is not C major. Seven
  keys, the same seven an octave up (not another inversion: KNOB 3 holds that, and
  higher on the keyboard should never sound lower), the last two nothing. OP1 to
  OP5 are the sections, the one in force lit. A key sounds while held, armed or
  not. A section keeps the rule by lighting only the degrees it has: borrowed chords
  on the degree they stand for, a dominant on the degree it leads to, and SUS / ADD
  on its sus4s, one a degree; sus2 and add9 stay on the knobs. Two of those sus4s
  hold a note out of the key (IV and vii in major, ii and VI in minor).
- **The audition arpeggio is new in the player** (`player_note_arp`): one voice,
  stepped on the song's ticks by the render loop, because `process_fx_tick` only
  runs while the song does. Any other audition ends it.
- **No slot, no arpeggio.** Three effects on the step: KNOB 4 says FX FULL and
  stays at --; nothing is overwritten. Set back to --, the step's ARP leaves.
- **Two choices on one step within a second are one gesture**, by undo.h's rule.
- **RAMTEXT was full again**: 156 bytes over with the audition arpeggio. `player_profile`, `player_profile_reset`, `fx_init` and `osc_init`, which only the main loop reaches, moved to `.flash_text`. Nothing on the render path left RAM.

## 2026-11-01 — a song that no longer fits beside itself

TREMOLO-CHIP, 23 KB, with 19 KB of flash left: SAVE cannot succeed, on any change
to that song, because a save writes the new file beside the old and commits it
with one write. That guarantee stays. What was wrong is what the machine did about
it. Every corner said NOT SAVED with no reason, and the question on the way to the
root took a failed SAVE for "stay" and went back into the song without a word. So
SAVE seemed to do nothing, and PROJECTS, the one place room can be made, was out of
reach while the changes were unsaved: losing them was the only way out.

The first answer put the reason in the corner and the way out in that question,
and the panel's verdict was right: the question only comes up on the way to the
root. Press SAVE, miss a corner, switch off, and the work is gone. So:

- **FLASH FULL is a window, on every SAVE that fails on room**, opened by app_save
  itself whichever screen asked (app/room.h). The corners say nothing about it.
- **SEL: the other projects and what each takes. SEL: are you sure. SEL: gone**, and
  back to the list for another. HOME is a step back at each.
- **As soon as the song fits it is saved**, SAVE having been asked for already. What
  it takes is measured by writing the file to nowhere, so the flash is not worn on
  an attempt that cannot succeed.
- **The open project is not offered.** Deleting its old file to fit the new one
  would give up what a save promises. Nor is the sample bank: it is not a project.
- **It lets go only when the hand is off everything**, or the HOME that closes it
  would be read as a step back by the screen under it.

Nothing is freed for the hand: which song goes is its own choice.

## 2026-11-02 — the column stays, and PLAY starts where one is pointing

Everything for the beta being in, the app was finally played rather than built,
and two things that had been asked for read wrong under the hand.

- **SELECT no longer jumps the cursor to the row's number**, in SONG and in
  PATTERN. It was asked for on 29/10 — the row as a whole, one detent away — and
  beside KNOB 1, which moves and leaves the column alone, the column vanishing
  out from under one knob and not the other is a lurch. The two knobs are now
  the same hand down to the column. The number is still one detent to the left,
  on KNOB 2, which is where it was all along.
- **PATTERN plays from the step under the cursor**, as SONG plays from the
  phrase under it, and **STOP leaves the cursor where it was**: the view scrolls
  back to the step one was pointing at instead of leaving the hand wherever the
  playhead happened to stop. The other way round was asked for on 30/10 and was
  the less intuitive of the two with a loop under the ear — the step one had
  picked out was lost on every stop. The player takes the step in
  `player_play_entry_at`, the free byte of its PLAY command; REC's count-in
  starts there too.
