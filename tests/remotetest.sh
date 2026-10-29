#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# tools/fm1_ctl.py against the whole app, end to end, over UDP: a song sent,
# opened, read, written into step by step, played, driven by buttons and
# knobs, saved, and fetched back to check what landed in the file.
set -e
SONG=${1:-}
[ -f "$SONG" ] || { echo "remote end-to-end: no song given, skipped"; exit 0; }
export MOONDAW_LINK_PORT=19792
P="--udp $MOONDAW_LINK_PORT"
CTL="python3 tools/fm1_ctl.py $P"
mkdir -p build
cc -std=c11 -O2 -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -Wall tests/remote_dev.c app/app.c $UI $CORE -o build/remote_dev -lm
build/remote_dev > build/remote_dev.log &
DEV=$!
sleep 0.3
fail() { echo "remote end-to-end: $1"; kill $DEV 2>/dev/null; exit 1; }
python3 tools/fm1_send.py "$SONG" $P > /dev/null || fail "send"
$CTL status | grep -q 'screen=HOME' || fail "status on HOME"
# the sample bank (tools/fm1_samples.py): two in, listed, one out, read back
W=$(dirname "$SONG")/../instruments/tr909
if [ -f "$W/tr9_kick.wav" ] && [ -f "$W/tr9_snare.wav" ]; then
    SMP="python3 tools/fm1_samples.py"
    $SMP add "$W/tr9_kick.wav" "$W/tr9_snare.wav" $P > build/remote_bank.log || fail "samples add"
    $SMP list $P | grep -q '^  tr9_snare ' || fail "samples list"
    $SMP remove tr9_snare $P > /dev/null || fail "samples remove"
    rm -rf build/remote_bank
    $SMP get $P -o build/remote_bank > /dev/null || fail "samples get"
    [ -f build/remote_bank/tr9_kick.wav ] && [ ! -f build/remote_bank/tr9_snare.wav ] || fail "the bank read back"
fi
$CTL open kids || fail "open"
$CTL status | grep -q 'song=KIDS dirty=0' || fail "the song in memory"
$CTL cell 13 1 05 || fail "a new phrase's cell"
$CTL length 1 05 8 || fail "a block's length"
$CTL step 1 05 00 'C-4+E-4+G-4 i09 fVOL40' || fail "a step"
$CTL step 1 05 04 'OFF' || fail "an OFF"
$CTL status | grep -q 'dirty=1 .*phrases=20' || fail "the edits counted"
$CTL loop 13 || fail "play the phrase round"
sleep 0.3
$CTL status | grep -q 'playing=1 phrase=19' || fail "it plays phrase 13 (19)"
$CTL stop
$CTL status | grep -q 'cpu=3 max=12' || fail "cpu and the worst block"
$CTL press SEL || fail "press"
sleep 0.1
$CTL status | grep -q 'screen=SONG' || fail "SEL opened SONG"
$CTL turn K2 1 || fail "turn"
$CTL screen -o build/remote_screen.png > /dev/null || fail "screen"
$CTL led FX lit C-4 breath all glow || fail "led"
$CTL led all app || fail "led given back"
$CTL led FX dazzling 2> /dev/null && fail "a level that is not one"
python3 -c "
from PIL import Image
im = Image.open('build/remote_screen.png')
assert im.size == (240, 240) and im.getpixel((0, 239)) == (0, 0, 0)
assert (255, 255, 255) in [c for _, c in im.getcolors(65536)], 'no white text'
" || fail "what the screen shows"
$CTL press HOME
sleep 0.1
$CTL canvas clear '#102030' || fail "canvas clear"
$CTL canvas fill 10 20 100 28 green || fail "canvas fill"
$CTL canvas text 14 42 'HELLO' --colour black || fail "canvas text"
$CTL canvas line 0 0 239 239 amber || fail "canvas line"
$CTL status | grep -q 'screen=CANVAS' || fail "the canvas in front"
$CTL press GLO
sleep 0.1
$CTL status | grep -q 'screen=CANVAS' || fail "GLO is the script's"
$CTL press HOME
sleep 0.1
$CTL status | grep -q 'screen=HOME' || fail "HOME back to where it was"
# alive: GLO the script's, the gestures collected, HOME leaves
python3 - <<PY || fail "the live canvas"
import subprocess, sys, time
sys.path.insert(0, "tools")
from fm1_proto import Canvas
c = Canvas(udp=$MOONDAW_LINK_PORT)
c.keep_glo().clear("black").fill(0, 0, 240, 28, "green").text(10, 22, "LIVE", "black").show()
c.events()
ctl = ["python3", "tools/fm1_ctl.py", "--udp", "$MOONDAW_LINK_PORT"]
subprocess.run(ctl + ["press", "GLO"], check=True)
subprocess.run(ctl + ["turn", "K2", "3"], check=True)
time.sleep(0.15)
ev = c.events()
assert ev.shown and "GLO" in ev.pressed and ev.turns.get("K2") == 3, ev
subprocess.run(ctl + ["press", "HOME"], check=True)
time.sleep(0.15)
assert not c.events().shown
PY
$CTL save > /dev/null || fail "save"
python3 tools/fm1_send.py --get kids -o build/remote_back.trk --force $P > /dev/null || fail "get"
grep -q '^pat t0 05 len 8$' build/remote_back.trk || fail "the block's length in the file"
grep -q '^00 C-4+E-4+G-4 i09 fVOL40$' build/remote_back.trk || fail "the step in the file"
grep -q '^04 OFF$' build/remote_back.trk || fail "the OFF in the file"
grep -A2 '^arr 13 ' build/remote_back.trk | grep -q '^t0 05$' || fail "the cell in the file"
wait $DEV
echo "remote end-to-end: $(sed -n 's/^remote_dev: //p' build/remote_dev.log) -> ok"
