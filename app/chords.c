/* SPDX-License-Identifier: GPL-3.0-only */
#include "chords.h"

#include "../core/player.h"
#include "gfx.h"
#include "grid.h"
#include "theme.h"
#include "ui.h"

enum { Q_MAJ, Q_MIN, Q_DIM, Q_MAJ7, Q_7, Q_M7, Q_M7B5, Q_SUS2, Q_SUS4, Q_ADD9, Q_MADD9 };
static const struct {
    const char *suffix;
    uint8_t n, iv[STEP_MAX_NOTES];
} quality[] = {
    [Q_MAJ] = { "", 3, { 0, 4, 7 } },         [Q_MIN] = { "m", 3, { 0, 3, 7 } },
    [Q_DIM] = { "dim", 3, { 0, 3, 6 } },      [Q_MAJ7] = { "maj7", 4, { 0, 4, 7, 11 } },
    [Q_7] = { "7", 4, { 0, 4, 7, 10 } },      [Q_M7] = { "m7", 4, { 0, 3, 7, 10 } },
    [Q_M7B5] = { "m7b5", 4, { 0, 3, 6, 10 } }, [Q_SUS2] = { "sus2", 3, { 0, 2, 7 } },
    [Q_SUS4] = { "sus4", 3, { 0, 5, 7 } },    [Q_ADD9] = { "add9", 4, { 0, 4, 7, 14 } },
    [Q_MADD9] = { "madd9", 4, { 0, 3, 7, 14 } },
};

enum { SECTIONS = 5 };
static const char *const section_name[SECTIONS] = { "TRIADS", "SEVENTHS", "SUS / ADD", "BORROWED", "SECONDARY" };

/* The degree in numerals, semitones over the tonic, the quality; the name of
 * the chord says the quality, so the numeral says only where it stands. */
typedef struct {
    uint8_t section, semis, q;
    const char *degree;
} Chord;

enum { CHORDS = 29 };
static const Chord table[2][CHORDS] = {
    [KEY_MAJOR] = {
        { 0, 0, Q_MAJ, "I" },      { 0, 5, Q_MAJ, "IV" },     { 0, 7, Q_MAJ, "V" },     { 0, 9, Q_MIN, "vi" },
        { 0, 2, Q_MIN, "ii" },     { 0, 4, Q_MIN, "iii" },    { 0, 11, Q_DIM, "vii" },
        { 1, 0, Q_MAJ7, "I" },     { 1, 5, Q_MAJ7, "IV" },    { 1, 7, Q_7, "V" },       { 1, 9, Q_M7, "vi" },
        { 1, 2, Q_M7, "ii" },      { 1, 4, Q_M7, "iii" },     { 1, 11, Q_M7B5, "vii" },
        { 2, 0, Q_SUS2, "I" },     { 2, 0, Q_SUS4, "I" },     { 2, 5, Q_SUS2, "IV" },   { 2, 7, Q_SUS4, "V" },
        { 2, 0, Q_ADD9, "I" },     { 2, 5, Q_ADD9, "IV" },
        { 3, 10, Q_MAJ, "bVII" },  { 3, 5, Q_MIN, "iv" },     { 3, 8, Q_MAJ, "bVI" },   { 3, 3, Q_MAJ, "bIII" },
        { 4, 2, Q_7, "V/V" },      { 4, 4, Q_7, "V/vi" },     { 4, 9, Q_7, "V/ii" },    { 4, 0, Q_7, "V/IV" },
        { 4, 11, Q_7, "V/iii" },
    },
    [KEY_MINOR] = {
        { 0, 0, Q_MIN, "i" },      { 0, 5, Q_MIN, "iv" },     { 0, 7, Q_MIN, "v" },     { 0, 8, Q_MAJ, "VI" },
        { 0, 3, Q_MAJ, "III" },    { 0, 10, Q_MAJ, "VII" },   { 0, 2, Q_DIM, "ii" },
        { 1, 0, Q_M7, "i" },       { 1, 5, Q_M7, "iv" },      { 1, 7, Q_M7, "v" },      { 1, 8, Q_MAJ7, "VI" },
        { 1, 3, Q_MAJ7, "III" },   { 1, 10, Q_7, "VII" },     { 1, 2, Q_M7B5, "ii" },
        { 2, 0, Q_SUS2, "i" },     { 2, 0, Q_SUS4, "i" },     { 2, 5, Q_SUS2, "iv" },   { 2, 7, Q_SUS4, "v" },
        { 2, 0, Q_MADD9, "i" },    { 2, 8, Q_ADD9, "VI" },
        { 3, 7, Q_MAJ, "V" },      { 3, 5, Q_MAJ, "IV" },     { 3, 1, Q_MAJ, "bII" },   { 3, 0, Q_MAJ, "I" },
        { 4, 7, Q_7, "V" },        { 4, 0, Q_7, "V/iv" },     { 4, 3, Q_7, "V/VI" },    { 4, 10, Q_7, "V/III" },
        { 4, 2, Q_7, "V/V" },
    },
};

enum { TOP = 24, ROWS = 7, LIST = CHORDS + SECTIONS, ARP_MAX = 15 };

static const Song *song;
static int instr, *octave;
static int cur, top, inversion, arp;
static bool room; /* an effect slot for the arpeggio: its own, or an empty one */
static bool strike, listening;
static const char *said;
static uint32_t prev_held;

static const Chord *chord(int i) { return &table[song->key_mode == KEY_MINOR][i]; }
/* Its row in the list: the sections' names stand between the chords. */
static int row_of(int i) { return i + chord(i)->section + 1; }

/* -------------------------------------------------------------- notes --- */
int chords_notes(uint8_t out[STEP_MAX_NOTES])
{
    const Chord *c = chord(cur);
    int n = quality[c->q].n, f = 53 + *octave * 12; /* the keyboard's lowest key, an F */
    int root = f + (song->key_root + c->semis + 12 - f % 12) % 12;
    for (int i = 0; i < n; i++)
        out[i] = (uint8_t)(root + quality[c->q].iv[i] + (i < inversion ? 12 : 0));
    for (int i = 1; i < n; i++) /* lowest first, N1 being where an arpeggio starts */
        for (int j = i; j > 0 && out[j] < out[j - 1]; j--) {
            uint8_t t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return n;
}

int chords_arp(void) { return arp; }

static void sound(void)
{
    uint8_t nn[STEP_MAX_NOTES];
    int n = chords_notes(nn);
    if (arp)
        player_note_arp(instr, nn, n, arp);
    else
        player_note_chord(instr, nn, n);
}

/* -------------------------------------------------------------- hands --- */
void chords_open(const Song *s, int in, int *oct, const Step *st)
{
    song = s;
    instr = in;
    octave = oct;
    cur = top = inversion = arp = 0;
    room = false;
    for (int i = 0; i < STEP_MAX_FX; i++) {
        if (st->fx[i].cmd == FX_ARP)
            arp = fx_clamp_val(FX_ARP, s->ticks_per_step, st->fx[i].val);
        if (st->fx[i].cmd == FX_ARP || st->fx[i].cmd == FX_NONE)
            room = true;
    }
    strike = listening = false;
    said = 0;
    prev_held = 0xFFFFFFFFu;
    gfx_invalidate();
}

static void section_turn(int d)
{
    int sec = chord(cur)->section + d;
    sec = sec < 0 ? 0 : sec >= SECTIONS ? SECTIONS - 1 : sec;
    for (int i = 0; i < CHORDS; i++)
        if (chord(i)->section == sec) {
            cur = i;
            return;
        }
}

int chords_frame(const PlatInput *in, uint32_t pressed)
{
#define PRESSED(b) ((pressed >> (b)) & 1u)
    int was = cur, d;

    if (pressed || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3] || in->enc[PE_K4])
        said = 0;
    cur += in->enc[PE_SELECT] + in->enc[PE_K1];
    cur = cur < 0 ? 0 : cur >= CHORDS ? CHORDS - 1 : cur;
    if (in->enc[PE_K2])
        section_turn(in->enc[PE_K2] > 0 ? 1 : -1);
    if (cur != was) {
        strike = true;
        if (inversion >= quality[chord(cur)->q].n) /* fewer notes than the last */
            inversion = quality[chord(cur)->q].n - 1;
    }
    if ((d = in->enc[PE_K3]) != 0) {
        int v = inversion + d, hi = quality[chord(cur)->q].n - 1;
        inversion = v < 0 ? 0 : v > hi ? hi : v;
        strike = true;
    }
    if ((d = in->enc[PE_K4]) != 0) {
        if (room) {
            int v = arp + d;
            arp = v < 0 ? 0 : v > ARP_MAX ? ARP_MAX : v;
            strike = true;
        } else {
            said = "FX FULL";
        }
    }
    if (PRESSED(PB_OCTDN) && *octave > -3)
        --*octave, strike = true;
    if (PRESSED(PB_OCTUP) && *octave < 3)
        ++*octave, strike = true;
    if (PRESSED(PB_PLAY)) {
        if (player_playing()) /* the chords, not the song: as CHOOSE INSTRUMENT's PLAY */
            player_stop();
        listening = !listening;
        if (listening)
            strike = true;
        else
            player_note_off();
    }
    if (PRESSED(PB_SEL) || PRESSED(PB_HOME) || PRESSED(PB_ARP)) {
        player_note_off();
        return PRESSED(PB_SEL) ? CHORDS_KEPT : CHORDS_LEFT;
    }
    if (strike && listening)
        sound();
    strike = false;
    ui_led_state(PB_PLAY, listening);
    return CHORDS_OPEN;
#undef PRESSED
}

uint32_t chords_buttons(void) { return 1u << PB_OCTDN | 1u << PB_OCTUP; }

/* ------------------------------------------------------------ drawing --- */
static void hint(int x, const char *key, const char *word, bool right)
{
    int w = gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND14, x + 5, PLAT_H - 6, word, C_DIM);
}

void chords_draw(void)
{
    char s[40], h[3], nm[4];
    const char *q;
    int n = 0, r = row_of(cur);

    grid_follow(&r, &top, LIST, ROWS);
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    for (q = key_root_name(song->key_root); *q; q++)
        s[n++] = *q;
    for (q = song->key_mode == KEY_MINOR ? " MINOR" : " MAJOR"; *q; q++)
        s[n++] = *q;
    s[n] = 0;
    gfx_text(F_COND14, 6, 17, s, C_TITLE);
    if (said) {
        gfx_text(F_COND14, PLAT_W - 6 - gfx_text_w(F_COND14, said), 17, said, C_FX);
    } else {
        n = 0;
        for (q = "K3 INV "; *q; q++)
            s[n++] = *q;
        s[n++] = (char)('0' + inversion);
        for (q = "  K4 ARP "; *q; q++)
            s[n++] = *q;
        if (arp) {
            grid_hex2(h, arp);
            s[n++] = h[0], s[n++] = h[1];
        } else {
            s[n++] = '-', s[n++] = '-';
        }
        s[n] = 0;
        gfx_text(F_COND14, PLAT_W - 6 - gfx_text_w(F_COND14, s), 17, s, arp ? C_FX : C_DIM);
    }
    gfx_fill(0, TOP - 3, PLAT_W, 1, C_TRACK, 255);

    for (int i = 0, last = -1; i < CHORDS; i++) {
        const Chord *c = chord(i);
        if (c->section != last) { /* the section's name, on a row of its own */
            int y = TOP + (row_of(i) - 1 - top) * GRID_ROW;
            last = c->section;
            if (row_of(i) - 1 >= top && row_of(i) - 1 < top + ROWS)
                gfx_text(F_COND14, 6, y + 20, section_name[last], C_TITLE);
        }
        if (row_of(i) < top || row_of(i) >= top + ROWS)
            continue;
        {
            int y = TOP + (row_of(i) - top) * GRID_ROW, base = y + 2 + FONTS[F_COND20].ascent, x;
            bool here = i == cur;
            if (here)
                gfx_slab(4, y + 1, PLAT_W - 14, GRID_ROW - 2, C_ACCENT, 255);
            x = gfx_text(F_COND20, 14, base, key_root_name(song->key_root + c->semis), here ? C_ON_ACC : C_TEXT);
            gfx_text(F_COND20, x, base, quality[c->q].suffix, here ? C_ON_ACC : C_TEXT);
            /* 2 up, the small type centred on the row, as CHOOSE PATTERN's */
            gfx_text(F_COND14, 82, base - 2, c->degree, here ? C_ON_ACC : C_DIM);
            if (here) { /* what SEL would write, as the step will show it */
                uint8_t nn[STEP_MAX_NOTES];
                int k = chords_notes(nn);
                x = 112;
                for (int j = 0; j < k; j++)
                    x = gfx_text(F_COND14, x, base - 2, note_name(nn[j], nm), C_ON_ACC) + 3;
            }
            grid_play_sign(PLAT_W - 32, y + 8, here ? C_ON_ACC : C_DIM, here && listening);
        }
    }
    {
        int hh = ROWS * GRID_ROW * ROWS / LIST, y = TOP + ROWS * GRID_ROW * top / LIST;
        gfx_fill(PLAT_W - 4, TOP, 2, ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, hh, C_DIM, 255);
    }
    hint(6, "SEL", "TAKE", false);
    hint(96, "PLAY", "LISTEN", false);
    hint(PLAT_W - 6, "HOME", "BACK", true);
}
