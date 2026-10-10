#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# tests/solo.sh NAME [SECONDS] : each track of build/diff/NAME.trk on its own, both engines.
# Run tests/difftest.sh on the song first: it builds the tools and the reduced file.
cd "$(dirname "$0")/.."
f=build/diff/$1.trk; secs=${2:-30}
for t in 0 1 2 3 4 5 6 7; do
    build/ref_render "$f" build/diff/solo.f32 "$secs" $t
    build/fm_render "$f" build/diff/solo.i32 "$secs" $t
    printf 't%d  ' $t; python3 tests/compare.py build/diff/solo.f32 build/diff/solo.i32
done
