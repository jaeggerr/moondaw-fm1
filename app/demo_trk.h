/* SPDX-License-Identifier: GPL-3.0-only */
/* What plays when nothing else was loaded: sixteen bars written for
 * this file, so the firmware has something to say before it has any
 * storage. A .trk like any other. */
static const char demo_trk[] =
    "version 3\n"
    "bpm 124\nticks 6\nmeter 4 4\ntracks 4\n"
    "inst 00 engine PULSE timbre 1 adsr 0 34 170 14 filter 150 60 84 name BASS\n"
    "inst 01 engine WAVE timbre 1 adsr 4 40 200 30 vibphase free name LEAD\n"
    "inst 02 engine WAVE timbre 2 adsr 0 18 0 18 name KICK\n"
    "inst 03 engine NOISE timbre 1 adsr 0 10 0 8 gain -8 name HAT\n"
    "inst 04 engine NOISE timbre 0 adsr 0 24 0 18 gain -3 filter 190 30 78 name SNARE\n"
    "trk 0 vol 96 pan -10\ntrk 1 vol 80 pan 14\ntrk 2 vol 110 pan 0\ntrk 3 vol 84 pan 0\n"
    "pat t0 00 len 16\n"
    "00 A-2 i00\n02 A-2 fGAT03\n04 A-3 fGAT03\n06 A-2 fGAT03\n"
    "08 C-3\n0A C-3 fGAT03\n0C G-2\n0E G-3 fGAT03\n"
    "pat t0 01 len 16\n"
    "00 F-2 i00\n02 F-2 fGAT03\n04 F-3 fGAT03\n06 F-2 fGAT03\n"
    "08 G-2\n0A G-2 fGAT03\n0C E-2\n0E E-3 fGAT03\n"
    "pat t1 00 len 32\n"
    "00 E-4 i01 fVIB53\n04 A-4\n06 C-5\n08 B-4\n0C G-4\n0E E-4\n"
    "10 D-4\n14 G-4\n16 B-4\n18 A-4 fSCP42\n1E OFF\n"
    "pat t2 00 len 16\n"
    "00 C-3 i02 fSWP2E\n04 C-3\n08 C-3\n0C C-3\n0E C-3 fVEL50\n"
    "pat t3 00 len 16\n"
    "02 A-3 i03 fVEL50\n04 A-2 i04 fVEL7F\n06 A-3 i03 fVEL38\n0A A-3 i03 fVEL50\n"
    "0C A-2 i04 fVEL7F\n0E A-3 i03 fVEL38\n0F A-3 i03 fVEL24\n"
    "arr 00 steps auto\nname INTRO\nt0 00\nt2 00\n"
    "arr 01 steps auto\nt0 00\nt1 00\nt2 00\nt3 00\n"
    "arr 02 steps auto\nt0 01\nt1 00\nt2 00\nt3 00\n"
    "arr 03 ref arr 01\n";
