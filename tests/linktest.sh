#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The host tool against the firmware's receiver, end to end, over UDP:
# what the device's store holds afterwards must be the file as trkdump
# writes it, byte for byte, and so must what --get brings back.
set -e
SONG=${1:-}
[ -f "$SONG" ] || { echo "link end-to-end: no song given, skipped"; exit 0; }
export MOONDAW_LINK_PORT=19791
mkdir -p build
cc -std=c11 -O2 tests/trkdump.c core/song.c core/trk.c -o build/trkdump
cc -std=c11 -O2 -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE tests/link_dev.c app/link.c core/store.c -o build/link_dev
build/trkdump "$SONG" > build/link_want.trk
build/link_dev build/link_got.trk > build/link_dev.log &
DEV=$!
sleep 0.3
python3 tools/fm1_send.py "$SONG" --udp $MOONDAW_LINK_PORT > build/link_send.log || { cat build/link_send.log; kill $DEV 2>/dev/null; exit 1; }
python3 tools/fm1_send.py --list --udp $MOONDAW_LINK_PORT > build/link_list.log
python3 tools/fm1_send.py --get kids -o build/link_back.trk --force --udp $MOONDAW_LINK_PORT > build/link_get.log || { cat build/link_get.log; kill $DEV 2>/dev/null; exit 1; }
wait $DEV
cmp build/link_want.trk build/link_got.trk
cmp build/link_want.trk build/link_back.trk
grep -q '^  KIDS ' build/link_list.log
echo "link end-to-end: $(sed -n 's/^link_dev: //p' build/link_dev.log), $(wc -c < build/link_got.trk | tr -d ' ') bytes there and back, identical; listed -> ok"
