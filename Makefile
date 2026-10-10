# MOONDAW FM-1 -- everything that needs no hardware and no Docker.
# The firmware itself is ./build.sh (JieLi toolchain, see BUILDING.md).
#
#   make sim        the FM-1 simulated in an SDL2 window  -> build/sim/moondaw-fm1 [song.trk]
#   make web        the same in the browser               -> build/web/
#   make test       both, headless, and the song file reader
#   make difftest   the engine against moondaw's own, on real songs (needs ../moondaw, built)
#
#   APP=hello       the bring-up app instead of the player (keys, knobs, a square wave)
CC      ?= cc
CFLAGS  += -std=c11 -O2 -g -Wall -Wextra
APP     ?= app
MOONDAW ?= ../moondaw
CORE    := core/song.c core/trk.c core/store.c core/synth.c core/fx.c core/player.c
ifeq ($(APP),hello)
SRCS    := app/hello.c app/term.c
else
SRCS    := app/$(APP).c app/pattern.c app/gfx.c app/term.c $(CORE)
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

test: sim web
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/sim/moondaw-fm1 --selftest build/sim/selftest.bmp $(SELFTEST_SONG)
	node platform/web/test.mjs
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/pattern_test.c app/pattern.c app/gfx.c $(CORE) -o build/pattern_test
	build/pattern_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/store_test.c core/store.c -o build/store_test
	build/store_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/persist_test.c app/app.c app/pattern.c app/gfx.c app/term.c $(CORE) -o build/persist_test
	build/persist_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined tests/trkcheck.c core/song.c core/trk.c -o build/trkcheck
	@if ls $(MOONDAW)/songs/*.trk >/dev/null 2>&1; then build/trkcheck $(MOONDAW)/songs/*.trk | cut -c1-72; \
	 else echo "trkcheck: no songs in $(MOONDAW)/songs, skipped"; fi

difftest:
	MOONDAW=$(MOONDAW) tests/difftest.sh 30 $(MOONDAW)/songs/*.trk

clean:
	rm -rf build

.PHONY: sim web test difftest clean
