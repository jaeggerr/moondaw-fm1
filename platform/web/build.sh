#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MOONDAW FM-1 in the browser -> build/web/{index.html, worklet.js, moondaw.wasm}
# With emcc when there is one (as Felucca's web/emu/build.sh); otherwise zig cc, which
# is enough because nothing here needs a libc.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/web
if [ "${APP:-app}" = hello ]; then
    SRC="platform/web/web.c app/hello.c app/term.c"
elif [ "${APP:-app}" = type ] || [ "${APP:-app}" = mock ]; then
    SRC="-DGFX_SPECIMEN=1 platform/web/web.c app/$APP.c app/gfx.c app/term.c"
else
    SRC="platform/web/web.c app/${APP:-app}.c app/grid.c app/link.c app/menu.c app/songview.c app/pattern.c app/instrument.c app/mixer.c app/namer.c app/project.c app/remote.c app/phrase.c app/edit.c app/undo.c app/loop.c app/ui.c app/gfx.c app/term.c core/song.c core/trk.c core/store.c core/synth.c core/fx.c core/player.c"
fi
if command -v emcc >/dev/null 2>&1; then
    # shellcheck disable=SC2086
    emcc -O2 -std=gnu11 --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -o build/web/moondaw.wasm $SRC
elif command -v zig >/dev/null 2>&1; then
    # shellcheck disable=SC2086
    zig cc -target wasm32-freestanding -O2 -std=gnu11 -nostdlib -fvisibility=hidden \
        -Wl,--no-entry -Wl,--export-dynamic -o build/web/moondaw.wasm $SRC
else
    echo "web: neither emcc nor zig found"; exit 1
fi
cp platform/web/index.html platform/web/worklet.js build/web/
echo "web: build/web ($(wc -c < build/web/moondaw.wasm | tr -d ' ') B wasm)"
