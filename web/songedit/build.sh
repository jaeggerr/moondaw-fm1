#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The editor page's audition engine -> build/songedit/audition.wasm
# The core alone, no app and no platform: audition.c says why. Same compilers
# as platform/web/build.sh -- emcc when there is one, else zig cc, which is
# enough because nothing here needs a libc.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/songedit
SRC="web/songedit/audition.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c core/sampler.c"
if command -v emcc >/dev/null 2>&1; then
    # shellcheck disable=SC2086
    emcc -O2 -std=gnu11 --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -o build/songedit/audition.wasm $SRC
elif command -v zig >/dev/null 2>&1; then
    # shellcheck disable=SC2086
    zig cc -target wasm32-freestanding -O2 -std=gnu11 -nostdlib -fvisibility=hidden \
        -Wl,--no-entry -Wl,--export-dynamic -o build/songedit/audition.wasm $SRC
else
    echo "songedit: neither emcc nor zig found"; exit 1
fi
# The page itself stays in web/songedit/ and is served from the repository root
# (README.md): it imports web/installer/ modules, so moving it would break those.
echo "songedit: build/songedit ($(wc -c < build/songedit/audition.wasm | tr -d ' ') B wasm)"
