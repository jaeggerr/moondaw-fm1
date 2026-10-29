# MOONDAW FM-1 -- everything that needs no hardware and no Docker.
# The firmware itself is ./build.sh (JieLi toolchain, see BUILDING.md).
#
#   make sim        the FM-1 simulated in an SDL2 window  -> build/sim/moondaw-fm1 [song.trk]
#   make web        the same in the browser               -> build/web/
#   make test       both, headless, the song file reader, and the voice through the app
#   make difftest   the engine against moondaw's own, on real songs (needs ../moondaw, built)
#
#   APP=hello       the bring-up app instead of the player (keys, knobs, a square wave)
#   APP=type        the type specimen: a ruler, then one face and size a page
CC      ?= cc
CFLAGS  += -std=c11 -O2 -g -Wall -Wextra
APP     ?= app
MOONDAW ?= ../moondaw
CORE    := core/song.c core/trk.c core/store.c core/synth.c core/fx.c core/player.c core/drum.c
ifeq ($(APP),hello)
SRCS    := app/hello.c app/term.c
else ifneq ($(filter $(APP),type),)
SRCS    := app/$(APP).c app/gfx.c app/term.c
CFLAGS  += -DGFX_SPECIMEN=1
else
UI      := app/grid.c app/link.c app/menu.c app/songview.c app/pattern.c app/instrument.c app/mixer.c app/namer.c app/syllable.c app/settings.c app/system.c app/help.c app/fx_help.c app/project.c app/remote.c app/canvas.c app/phrase.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c app/splash.c
SRCS    := app/$(APP).c $(UI) app/term.c $(CORE)
endif
HDRS    := platform/platform.h $(wildcard app/*.h core/*.h)

sim: build/sim/moondaw-fm1

build/sim/moondaw-fm1: platform/sdl/main.c $(SRCS) $(HDRS)
	@mkdir -p build/sim
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sdl2) platform/sdl/main.c $(SRCS) \
	    -o $@ $(shell pkg-config --libs sdl2)

web:
	APP=$(APP) platform/web/build.sh

app/fonts.h: tools/gen_fonts.py
	python3 tools/gen_fonts.py > $@

core/tables.h: tools/gen_tables.c
	@mkdir -p build
	$(CC) tools/gen_tables.c -lm -o build/gen_tables && build/gen_tables > $@

core/drum_tables.h: tools/gen_drum_tables.c
	@mkdir -p build
	$(CC) tools/gen_drum_tables.c -lm -o build/gen_drum_tables && build/gen_drum_tables > $@

test: sim web
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/sim/moondaw-fm1 --selftest build/sim/selftest.bmp $(SELFTEST_SONG)
	node platform/web/test.mjs
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/pattern_test.c app/pattern.c app/syllable.c app/grid.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c $(CORE) -o build/pattern_test
	build/pattern_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/store_test.c core/store.c -o build/store_test
	build/store_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/persist_test.c app/app.c $(UI) $(CORE) -o build/persist_test
	build/persist_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/song_test.c app/songview.c app/grid.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/song_test
	build/song_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/instrument_test.c app/instrument.c app/namer.c app/grid.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/instrument_test
	build/instrument_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/mixer_test.c app/mixer.c app/grid.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/mixer_test
	build/mixer_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/project_test.c app/project.c app/namer.c app/grid.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/project_test
	build/project_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/phrase_test.c app/phrase.c app/grid.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/phrase_test
	build/phrase_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/undo_test.c app/phrase.c app/songview.c app/pattern.c app/syllable.c app/grid.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/undo_test
	build/undo_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/link_test.c app/link.c core/store.c -o build/link_test
	build/link_test
	tests/linktest.sh $(MOONDAW)/songs/KIDS.trk
	UI="$(UI)" CORE="$(CORE)" tests/remotetest.sh $(MOONDAW)/songs/KIDS.trk
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/menu_test.c app/app.c $(UI) $(CORE) -o build/menu_test
	build/menu_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/vox_test.c app/app.c $(UI) $(CORE) -o build/vox_test -lm
	@V="tests/vox-styles.trk"; for s in robot-demo LEOMOTEUR DAFT-PUNK DAFT-PUNK-EN; do \
	   [ -f $(MOONDAW)/songs/$$s.trk ] && V="$$V $(MOONDAW)/songs/$$s.trk"; done; build/vox_test $$V
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/drum_test.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/drum_test
	build/drum_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/trkcheck.c core/song.c core/trk.c -o build/trkcheck
	@if ls $(MOONDAW)/songs/*.trk >/dev/null 2>&1; then build/trkcheck $(MOONDAW)/songs/*.trk | cut -c1-72; \
	 else echo "trkcheck: no songs in $(MOONDAW)/songs, skipped"; fi

difftest:
	MOONDAW=$(MOONDAW) tests/difftest.sh 30 $(MOONDAW)/songs/*.trk tests/vox-styles.trk

clean:
	rm -rf build

.PHONY: sim web test difftest clean
