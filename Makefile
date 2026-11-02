# MOONDAW FM-1 -- everything that needs no hardware and no Docker.
# The firmware itself is ./build.sh (JieLi toolchain, see BUILDING.md).
#
#   make sim        the FM-1 simulated in an SDL2 window  -> build/sim/moondaw-fm1 [song.trk]
#   make web        the same in the browser               -> build/web/
#   make songedit   the song editor's audition engine     -> build/songedit/
#   make export     the page that renders a song to WAV   -> build/export/
#   make test       both, headless, the song file reader, and the voice through the app
#   make difftest   the engine against moondaw's own, on real songs (needs ../moondaw, built)
#
#   APP=hello       the bring-up app instead of the player (keys, knobs, a square wave)
#   APP=type        the type specimen: a ruler, then one face and size a page
CC      ?= cc
CFLAGS  += -std=c11 -O2 -g -Wall -Wextra
APP     ?= app
MOONDAW ?= ../moondaw
# A MIDI keyboard into the simulator is CoreMIDI, on macOS (platform/sdl/main.c).
MIDILIBS := $(if $(filter Darwin,$(shell uname -s)),-framework CoreMIDI -framework CoreFoundation,)
CORE    := core/song.c core/trk.c core/store.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c core/bank.c
ifeq ($(APP),hello)
SRCS    := app/hello.c app/term.c
else ifneq ($(filter $(APP),type),)
SRCS    := app/$(APP).c app/gfx.c app/term.c
CFLAGS  += -DGFX_SPECIMEN=1
else
UI      := app/grid.c app/link.c app/menu.c app/songview.c app/pattern.c app/instrument.c app/mixer.c app/namer.c app/syllable.c app/chords.c app/room.c app/settings.c app/system.c app/help.c app/fx_help.c app/project.c app/remote.c app/canvas.c app/phrase.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c app/splash.c
SRCS    := app/$(APP).c $(UI) app/term.c $(CORE)
endif
HDRS    := platform/platform.h $(wildcard app/*.h core/*.h)

sim: build/sim/moondaw-fm1

build/sim/moondaw-fm1: platform/sdl/main.c $(SRCS) $(HDRS)
	@mkdir -p build/sim
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sdl2) platform/sdl/main.c $(SRCS) \
	    -o $@ $(shell pkg-config --libs sdl2) $(MIDILIBS)

web:
	APP=$(APP) platform/web/build.sh

# The song editor's audition engine: the core alone, no app (web/songedit/).
songedit:
	web/songedit/build.sh

# The export page, assembled with the modules it borrows (web/export/).
export:
	web/export/build.sh

app/fonts.h: tools/gen_fonts.py
	python3 tools/gen_fonts.py > $@

core/tables.h: tools/gen_tables.c
	@mkdir -p build
	$(CC) tools/gen_tables.c -lm -o build/gen_tables && build/gen_tables > $@

core/drum_tables.h: tools/gen_drum_tables.c
	@mkdir -p build
	$(CC) tools/gen_drum_tables.c -lm -o build/gen_drum_tables && build/gen_drum_tables > $@

test: sim web songedit export
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/sim/moondaw-fm1 --selftest build/sim/selftest.bmp $(SELFTEST_SONG)
	node platform/web/test.mjs
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/pattern_test.c app/pattern.c app/syllable.c app/chords.c app/grid.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c $(CORE) -o build/pattern_test
	build/pattern_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/store_test.c core/store.c -o build/store_test
	build/store_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/midi_test.c app/pattern.c app/syllable.c app/chords.c app/grid.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c $(CORE) -o build/midi_test
	build/midi_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/persist_test.c app/app.c $(UI) $(CORE) -o build/persist_test
	build/persist_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/song_test.c app/songview.c app/grid.c app/namer.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c -o build/song_test
	build/song_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/instrument_test.c app/instrument.c app/namer.c app/grid.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c core/bank.c core/store.c -o build/instrument_test
	build/instrument_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/mixer_test.c app/mixer.c app/grid.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c -o build/mixer_test
	build/mixer_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/project_test.c app/project.c app/namer.c app/grid.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c -o build/project_test
	build/project_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/phrase_test.c app/phrase.c app/grid.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c -o build/phrase_test
	build/phrase_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/undo_test.c app/phrase.c app/songview.c app/pattern.c app/syllable.c app/chords.c app/grid.c app/namer.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c -o build/undo_test
	build/undo_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/link_test.c app/link.c core/store.c core/bank.c core/sampler.c core/song.c -o build/link_test
	build/link_test
	tests/linktest.sh $(MOONDAW)/songs/KIDS.trk
	UI="$(UI)" CORE="$(CORE)" tests/remotetest.sh $(MOONDAW)/songs/KIDS.trk
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/menu_test.c app/app.c $(UI) $(CORE) -o build/menu_test
	build/menu_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/vox_test.c app/app.c $(UI) $(CORE) -o build/vox_test -lm
	@V="tests/vox-styles.trk"; for s in robot-demo LEOMOTEUR DAFT-PUNK DAFT-PUNK-EN; do \
	   [ -f $(MOONDAW)/songs/$$s.trk ] && V="$$V $(MOONDAW)/songs/$$s.trk"; done; build/vox_test $$V
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/drum_test.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c -o build/drum_test
	build/drum_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/sampler_test.c $(CORE) -o build/sampler_test -lm
	@if ls $(MOONDAW)/instruments/tr909/*.wav >/dev/null 2>&1 && [ -f $(MOONDAW)/songs/KIDS.trk ]; then \
	   python3 tools/fm1_samples.py pack build/test-bank.bin $(MOONDAW)/instruments/tr909/*.wav >/dev/null && \
	   rm -rf build/test-bank && python3 tools/fm1_samples.py get --bank build/test-bank.bin -o build/test-bank >/dev/null && \
	   build/sampler_test build/test-bank.bin build/test-bank $(MOONDAW)/songs/KIDS.trk; \
	 else build/sampler_test; fi
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/trkcheck.c core/song.c core/trk.c -o build/trkcheck
# A pipeline exits with the status of its LAST command, so a failure read
# through `cut` or `tail` came back green -- thirteen real round-trip
# failures did, on 31/10. Both go through a file: the whole output on a
# failure, the shortened form on a pass.
	@if ls $(MOONDAW)/songs/*.trk >/dev/null 2>&1; then \
	   build/trkcheck $(MOONDAW)/songs/*.trk > build/trkcheck.log || { cat build/trkcheck.log; exit 1; }; \
	   cut -c1-72 build/trkcheck.log; \
	 else echo "trkcheck: no songs in $(MOONDAW)/songs, skipped"; fi
	@node web/songedit/test/roundtrip.mjs > build/roundtrip.log 2>&1 || { cat build/roundtrip.log; exit 1; }
	@tail -4 build/roundtrip.log
# The export page's WAVs against tests/fm_render.c -- the same engine, native:
# the test asks for them sample for sample, so "what comes out of the file is
# what came out of the speaker" is a measurement and not a promise.
	$(CC) $(CFLAGS) tests/fm_render.c $(CORE) -o build/fm_render
	node web/export/test/render.mjs
	python3 tools/gen_skill.py --check

difftest:
	MOONDAW=$(MOONDAW) tests/difftest.sh 30 $(MOONDAW)/songs/*.trk tests/vox-styles.trk

clean:
	rm -rf build

.PHONY: sim web songedit export test difftest clean
