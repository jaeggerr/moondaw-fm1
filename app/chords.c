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
 * the chord says the quality, so the numeral says only where it stands. `key`
 * is the degree whose white key plays it, 0 for the knobs alone: a section
 * holds one chord a degree at most on the keys, and a borrowed chord or a
 * dominant sits on the degree it stands for or leads to.
 *
 * Within a section they run by degree, I to VII, and the ones sharing a degree
 * stand together under it, the one its white key plays first. They were by
 * likelihood until 2/11 -- I IV V vi ii iii vii, the fewest detents to the
 * common chord -- which stopped paying the day the keyboard began writing them:
 * the key under the finger *is* the degree, so the list has to read as the
 * keyboard is laid out, or the eye and the hand tell two different stories
 * ("au sein d'une meme section les accords doivent etre tries par degres, la je
 * ne comprends pas trop l'ordre choisi"). */
typedef struct {
    uint8_t section, semis, q, key;
    const char *degree;
} Chord;

enum { CHORDS = 34 };
static const Chord table[2][CHORDS] = {
    [KEY_MAJOR] = {
        { 0, 0, Q_MAJ, 1, "I" },      { 0, 2, Q_MIN, 2, "ii" },     { 0, 4, Q_MIN, 3, "iii" },   { 0, 5, Q_MAJ, 4, "IV" },
        { 0, 7, Q_MAJ, 5, "V" },      { 0, 9, Q_MIN, 6, "vi" },     { 0, 11, Q_DIM, 7, "vii" },
        { 1, 0, Q_MAJ7, 1, "I" },     { 1, 2, Q_M7, 2, "ii" },      { 1, 4, Q_M7, 3, "iii" },    { 1, 5, Q_MAJ7, 4, "IV" },
        { 1, 7, Q_7, 5, "V" },        { 1, 9, Q_M7, 6, "vi" },      { 1, 11, Q_M7B5, 7, "vii" },
        { 2, 0, Q_SUS4, 1, "I" },     { 2, 0, Q_SUS2, 0, "I" },     { 2, 0, Q_ADD9, 0, "I" },    { 2, 2, Q_SUS4, 2, "ii" },
        { 2, 4, Q_SUS4, 3, "iii" },   { 2, 5, Q_SUS4, 4, "IV" },    { 2, 5, Q_SUS2, 0, "IV" },   { 2, 5, Q_ADD9, 0, "IV" },
        { 2, 7, Q_SUS4, 5, "V" },     { 2, 9, Q_SUS4, 6, "vi" },    { 2, 11, Q_SUS4, 7, "vii" },
        { 3, 3, Q_MAJ, 3, "bIII" },   { 3, 5, Q_MIN, 4, "iv" },     { 3, 8, Q_MAJ, 6, "bVI" },   { 3, 10, Q_MAJ, 7, "bVII" },
        { 4, 9, Q_7, 2, "V/ii" },     { 4, 11, Q_7, 3, "V/iii" },   { 4, 0, Q_7, 4, "V/IV" },    { 4, 2, Q_7, 5, "V/V" },
        { 4, 4, Q_7, 6, "V/vi" },
    },
    [KEY_MINOR] = {
        { 0, 0, Q_MIN, 1, "i" },      { 0, 2, Q_DIM, 2, "ii" },     { 0, 3, Q_MAJ, 3, "III" },   { 0, 5, Q_MIN, 4, "iv" },
        { 0, 7, Q_MIN, 5, "v" },      { 0, 8, Q_MAJ, 6, "VI" },     { 0, 10, Q_MAJ, 7, "VII" },
        { 1, 0, Q_M7, 1, "i" },       { 1, 2, Q_M7B5, 2, "ii" },    { 1, 3, Q_MAJ7, 3, "III" },  { 1, 5, Q_M7, 4, "iv" },
        { 1, 7, Q_M7, 5, "v" },       { 1, 8, Q_MAJ7, 6, "VI" },    { 1, 10, Q_7, 7, "VII" },
        { 2, 0, Q_SUS4, 1, "i" },     { 2, 0, Q_SUS2, 0, "i" },     { 2, 0, Q_MADD9, 0, "i" },   { 2, 2, Q_SUS4, 2, "ii" },
        { 2, 3, Q_SUS4, 3, "III" },   { 2, 5, Q_SUS4, 4, "iv" },    { 2, 5, Q_SUS2, 0, "iv" },   { 2, 7, Q_SUS4, 5, "v" },
        { 2, 8, Q_SUS4, 6, "VI" },    { 2, 8, Q_ADD9, 0, "VI" },    { 2, 10, Q_SUS4, 7, "VII" },
        { 3, 0, Q_MAJ, 1, "I" },      { 3, 1, Q_MAJ, 2, "bII" },    { 3, 5, Q_MAJ, 4, "IV" },    { 3, 7, Q_MAJ, 5, "V" },
        { 4, 7, Q_7, 1, "V" },        { 4, 10, Q_7, 3, "V/III" },   { 4, 0, Q_7, 4, "V/iv" },    { 4, 2, Q_7, 5, "V/V" },
        { 4, 3, Q_7, 6, "V/VI" },
    },
};

/* The white keys, the first fourteen: a degree a key, then the same an octave
 * up. The black ones the sections hang on, OP1 to OP5. */
enum { WHITES = 14 };
static const uint8_t white[WHITES] = { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23 };
static const uint8_t op_key[SECTIONS] = { 1, 3, 5, 8, 10 };

enum { TOP = 24, ROWS = 7, LIST = CHORDS + SECTIONS, ARP_MAX = 15 };

static const Song *song;
static int instr, *octave;
static int cur, top, inversion, arp;
static bool high;     /* played from the keys' second octave */
static int held = -1; /* the key a chord is sounding under, -1 for none */
static bool room; /* an effect slot for the arpeggio: its own, or an empty one */
static bool strike, listening;
static const char *said;
static uint32_t prev_held;
/* The chord mode's own (2/11): the section, inversion and arpeggio the list
 * was last left on, which is what the keyboard writes with. */
static int m_sec, m_inv, m_arp;

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
    if (high && out[n - 1] + 12 <= NOTE_MAX)
        for (int i = 0; i < n; i++)
            out[i] = (uint8_t)(out[i] + 12);
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
    /* It opens where the mode stands, as the mode plays what it is left on:
     * the two say the same thing whichever way one goes between them. The
     * arpeggio is the step's own where the step has one -- a fact about the
     * step, and SEL writes back what the row shows -- and the mode's where
     * it has none. */
    cur = top = 0;
    for (int i = 0; i < CHORDS; i++)
        if (chord(i)->section == m_sec) {
            cur = i;
            break;
        }
    inversion = m_inv < quality[chord(cur)->q].n ? m_inv : quality[chord(cur)->q].n - 1;
    arp = m_arp;
    high = false;
    held = -1;
    room = false;
    for (int i = 0; i < STEP_MAX_FX; i++) {
        if (st->fx[i].cmd == FX_ARP)
            arp = fx_clamp_val(FX_ARP, s->ticks_per_step, st->fx[i].val);
        if (st->fx[i].cmd == FX_ARP || st->fx[i].cmd == FX_NONE)
            room = true;
    }
    if (!room)
        arp = 0; /* nowhere to write it: the row must not promise one */
    strike = listening = false;
    said = 0;
    prev_held = 0xFFFFFFFFu;
    gfx_invalidate();
}

static void section_go(int sec)
{
    sec = sec < 0 ? 0 : sec >= SECTIONS ? SECTIONS - 1 : sec;
    if (sec == chord(cur)->section)
        return;
    for (int i = 0; i < CHORDS; i++)
        if (chord(i)->section == sec) {
            cur = i;
            return;
        }
}

/* The chord on degree `deg` of section `sec`, -1 for none. */
static int chord_in(int sec, int deg)
{
    for (int i = 0; i < CHORDS; i++)
        if (chord(i)->section == sec && chord(i)->key == deg)
            return i;
    return -1;
}

/* What the keys do, a frame: the last key down has the chord, as one step is
 * one chord, and it sounds with or without PLAY, a key being its own request. */
static void keys_frame(uint32_t down, uint32_t keys)
{
    for (int s = 0; s < SECTIONS; s++)
        if ((keys >> op_key[s]) & 1u)
            section_go(s);
    for (int w = 0; w < WHITES; w++) {
        int i;
        if (!((keys >> white[w]) & 1u) || (i = chord_in(chord(cur)->section, w % 7 + 1)) < 0)
            continue;
        cur = i;
        high = w >= 7;
        held = white[w];
        strike = true;
    }
    if (held >= 0 && !((down >> held) & 1u)) {
        held = -1;
        if (!listening) /* armed, it rings on as the knobs leave it */
            player_note_off();
    }
}

static void keys_light(int sec)
{
    uint32_t usable = 0;
    for (int s = 0; s < SECTIONS; s++)
        usable |= 1u << op_key[s];
    for (int w = 0; w < WHITES; w++)
        if (chord_in(sec, w % 7 + 1) >= 0)
            usable |= 1u << white[w];
    ui_led_keys(usable, 1u << op_key[sec], 0);
}

int chords_frame(const PlatInput *in, uint32_t pressed, uint32_t keys)
{
#define PRESSED(b) ((pressed >> (b)) & 1u)
    int was = cur, d;

    if (pressed || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3] || in->enc[PE_K4])
        said = 0;
    cur += in->enc[PE_SELECT] + in->enc[PE_K1];
    cur = cur < 0 ? 0 : cur >= CHORDS ? CHORDS - 1 : cur;
    if (in->enc[PE_K2])
        section_go(chord(cur)->section + (in->enc[PE_K2] > 0 ? 1 : -1));
    if (cur != was)
        high = false; /* the knobs say what the row says, in the keyboard's octave */
    keys_frame(in->keys, keys);
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
        m_sec = chord(cur)->section; /* what it is left on is what the keys play */
        m_inv = inversion;
        m_arp = arp;
        return PRESSED(PB_SEL) ? CHORDS_KEPT : CHORDS_LEFT;
    }
    if (strike && (listening || held >= 0))
        sound();
    strike = false;
    ui_led_state(PB_PLAY, listening);
    keys_light(chord(cur)->section);
    return CHORDS_OPEN;
#undef PRESSED
}

uint32_t chords_buttons(void) { return 1u << PB_OCTDN | 1u << PB_OCTUP; }

/* --------------------------------------------------------- the mode --- */
void chords_mode_bind(const Song *s, int in, int *oct)
{
    song = s;
    instr = in;
    octave = oct;
}

bool chords_mode_keys(uint32_t pressed, uint32_t down)
{
    bool struck = false;
    for (int s = 0; s < SECTIONS; s++)
        if ((pressed >> op_key[s]) & 1u)
            m_sec = s;
    for (int w = 0; w < WHITES; w++) {
        int i, n;
        if (!((pressed >> white[w]) & 1u) || (i = chord_in(m_sec, w % 7 + 1)) < 0)
            continue;
        cur = i;
        high = w >= 7;
        n = quality[chord(i)->q].n;
        inversion = m_inv < n ? m_inv : n - 1; /* fewer notes than the list had */
        arp = m_arp;
        held = white[w];
        struck = true;
    }
    if (struck)
        sound();
    if (held >= 0 && !((down >> held) & 1u)) {
        held = -1;
        player_note_off();
    }
    return struck;
}

void chords_mode_leds(void) { keys_light(m_sec); }
const char *chords_mode_word(void) { return section_name[m_sec]; }

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
            gfx_text(F_COND14, 88, base - 2, c->degree, here ? C_ON_ACC : C_DIM);
            if (here) { /* what SEL would write, as the step will show it */
                uint8_t nn[STEP_MAX_NOTES];
                int k = chords_notes(nn);
                x = 116;
                for (int j = 0; j < k; j++)
                    x = gfx_text(F_COND14, x, base - 2, note_name(nn[j], nm), C_ON_ACC) + 3;
            }
            grid_play_sign(PLAT_W - 26, y + 8, here ? C_ON_ACC : C_DIM, here && listening);
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
