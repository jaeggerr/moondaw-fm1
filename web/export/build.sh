#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The export page -> build/export/, ready to serve.
#
# The page is NOT served from web/export/: it is assembled here, the way
# platform/web/ is, because three of the modules it loads are not its own --
# the .trk format (web/songedit/trk.js), the link (web/installer/mdlink.js)
# and the engine (build/songedit/audition.wasm). Copied rather than imported
# across directories, so that what is tried locally and what moondaw.com
# publishes are the same flat directory, with every import a sibling.
#
# moondaw/tools/site/build.sh rsyncs build/export/ into site/export/.
set -e
cd "$(dirname "$0")/../.."

[ -f build/songedit/audition.wasm ] || sh web/songedit/build.sh
# The page needs aud_play_once, which audition.c grew for it: an old wasm
# lying in build/ would load and then refuse the first render, so rebuild
# when the C is newer.
if [ web/songedit/audition.c -nt build/songedit/audition.wasm ]; then
    sh web/songedit/build.sh
fi

rm -rf build/export
mkdir -p build/export
cp web/export/index.html web/export/render.js web/export/stems.js web/export/wav.js \
   web/export/worker.js build/export/
cp web/songedit/trk.js build/export/trk.js
cp web/installer/mdlink.js build/export/mdlink.js
cp web/installer/fm1ota.js build/export/fm1ota.js
cp build/songedit/audition.wasm build/export/audition.wasm

echo "export: build/export ($(du -sh build/export | cut -f1))"
