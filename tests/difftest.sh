#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The FM-1 core against moondaw itself, on real songs.
#   tests/difftest.sh [SECONDS] SONG.trk...      (MOONDAW=path to the moondaw checkout, built)
# Each song is first reduced to what the FM-1 carries (build/trkdump), and PLUCK instruments are
# made silent in the reference as they are on the FM-1; DRUM ones (moondaw has no DRUM yet) on both
# sides. Both engines then render the SAME file,
# reverb and delay included -- the voice too: VOX, its syllables, tract and accent are carried.
set -e
cd "$(dirname "$0")/.."
M="${MOONDAW:-../moondaw}"
SECS=20
case "$1" in [0-9]*) SECS=$1; shift;; esac
OBJS=$(find "$M/build" -name '*.o' -not -path '*/san/*' -not -path '*/test/*' -not -path '*/listen/*' \
    -not -path '*/ui/*' -not -path '*/input/*' -not -path '*/session/*' \
    | grep -v -E '/build/main.o|/audio/audio.o|/app/app.o|/audio/export.o')
mkdir -p build/diff
# shellcheck disable=SC2086
cc -std=c11 -O2 -I"$M/src" tests/ref_render.c $OBJS -lm -o build/ref_render
cc -std=c11 -O2 -Wall -Wextra tests/fm_render.c core/song.c core/trk.c core/synth.c core/fx.c core/player.c core/drum.c -o build/fm_render
cc -std=c11 -O2 -Wall -Wextra tests/trkdump.c core/song.c core/trk.c -o build/trkdump
bad=0
for song in "$@"; do
    name=$(basename "$song" .trk)
    build/trkdump "$song" | sed -E -e '/^inst /s/engine (PLUCK|DRUM)/engine SMPL/' > "build/diff/$name.trk"
    # Track by track: summed, two tracks whose phases have drifted apart interfere differently
    # in each render, and the spectrum of the sum says so although neither track is wrong.
    worst=-200; n=0; fails=""
    for t in 0 1 2 3 4 5 6 7; do
        build/ref_render "build/diff/$name.trk" build/diff/solo.f32 "$SECS" $t
        build/fm_render "build/diff/$name.trk" build/diff/solo.i32 "$SECS" $t
        line=$(python3 tests/compare.py build/diff/solo.f32 build/diff/solo.i32 --max-db="${MAXDB:--20}") || {
            case "$line" in SILENT*) continue;; esac
            fails="$fails t$t"
        }
        case "$line" in SILENT*) continue;; esac
        n=$((n + 1))
        spec=$(echo "$line" | sed -E 's/.*spec +(-?[0-9.]+) dB.*/\1/')
        worst=$(python3 -c "print(max($worst, $spec))")
    done
    if [ $n = 0 ]; then
        printf '%-18s --    nothing the FM-1 can play (samples or string only)\n' "$name"
    elif [ -z "$fails" ]; then
        printf '%-18s ok    %d tracks, worst spectral difference %6.1f dB\n' "$name" $n "$worst"
    else
        printf '%-18s FAIL  %d tracks, worst %6.1f dB:%s\n' "$name" $n "$worst" "$fails"
        bad=1
    fi
done
exit $bad
