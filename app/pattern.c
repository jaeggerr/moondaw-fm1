/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern.h"

#include "../core/player.h"
#include "gfx.h"

/* The palette: the COL_* of moondaw's ui, to the byte. A shape tells one
 * screen from another, never a hue. */
#define C_BG     GFX_RGB(0x10, 0x14, 0x1C)
#define C_TEXT   GFX_RGB(0xC8, 0xD0, 0xC8)
#define C_DIM    GFX_RGB(0x5A, 0x64, 0x70)
#define C_ACCENT GFX_RGB(0x7F, 0xD9, 0x62) /* the cursor, and what plays */
#define C_TITLE  GFX_RGB(0x62, 0xC8, 0xD9) /* where one is */
#define C_PLAY   GFX_RGB(0x24, 0x3A, 0x28)
#define C_SEL    GFX_RGB(0x6E, 0x55, 0x18)
#define C_FX     GFX_RGB(0xF0, 0xBE, 0x5A)
#define C_MUTE   GFX_RGB(0x9E, 0x55, 0x55)
#define C_BEAT   GFX_RGB(0x91, 0x9A, 0x9C)
#define C_ON_ACC GFX_RGB(0x0B, 0x14, 0x09)

/* The measures of proto/pattern.html, in panel pixels. */
enum {
    NAV_Y = 2, NAV_H = 17,
    CARDS_Y = 22, CARDS_H = 22,
    LEFT_Y = 49,
    HEAD_Y = 59,
    ROWS_Y = 68, ROW_H = 12, ROW_PITCH = 13, ROWS = 11,
    BTN_Y = 215, BTN_H = 10,
    X0 = 3, INNER = 234,
};
/* Field 0 is the step number, 1..4 the notes, 5 the instrument, 6..8 the fx. */
enum { COL_STEP, COL_N1, COL_N4 = 4, COL_IN, COL_FX1, COL_FX3 = 8, COLS };
static const uint8_t col_x[COLS] = { 9, 23, 45, 67, 89, 116, 137, 168, 199 };
static const uint8_t col_w[COLS] = { 12, 20, 20, 20, 20, 14, 29, 29, 29 };

/* The twelve buttons, in the two rows of six they sit in under the knobs. */
enum { B_FX, B_LOOP, B_INSTR, B_CLEAR, B_SELECT, B_MIXER,
       B_BACK, B_SAVE, B_UNDO, B_SONG, B_PLAY, B_REC, BTNS };
static const uint8_t btn_of[BTNS] = { PB_FX, PB_SCL, PB_ENV, PB_LFO, PB_EDIT, PB_GLO,
                                      PB_HOME, PB_SAVE, PB_ARP, PB_SEQ, PB_PLAY, PB_REC };
static const char *const btn_word[BTNS] = { "FX", "LOOP", "INSTR", "CLEAR", "SELECT", "MIXER",
                                            "\xE2\x80\xB9 PHR", "SAVE", "UNDO", "SONG", "PLAY", "REC" };
/* What this build does not do yet is drawn dim: a button keeps its place
 * and says it does nothing. */
static const bool btn_live[BTNS] = { true, false, false, true, false, false,
                                     true, true, false, false, true, false };

static Song *song;
static int entry, track, pat;
static int cur_row, cur_col, top;
static int kbd_oct;        /* octaves the keyboard is moved by */
static int chord_next;     /* the note slot the next held key fills, 0 = none held */
static bool pool_full;
static int8_t saved;       /* 0 nothing to say, 1 saved, -1 the save failed */
static uint32_t prev_btns, prev_keys;
static uint8_t fx_order[FX_SYL]; /* alphabetical, --- first */
static int fx_count;

/* ----------------------------------------------------------- text --- */
static char *hex2(char *b, int v)
{
    b[0] = "0123456789ABCDEF"[(v >> 4) & 15];
    b[1] = "0123456789ABCDEF"[v & 15];
    b[2] = '\0';
    return b;
}

static char *dec(char *b, int v)
{
    char t[6];
    int n = 0, i = 0;
    do
        t[n++] = (char)('0' + v % 10);
    while ((v /= 10) > 0);
    while (n)
        b[i++] = t[--n];
    b[i] = '\0';
    return b;
}

static bool fx_is_pair(uint8_t cmd)
{
    return cmd == FX_VIB || cmd == FX_TRM || cmd == FX_SCP;
}

/* ----------------------------------------------------------- data --- */
static int pat_len(void) { return song->patterns[track][pat].length; }
static const Step *step_at(int row) { return song_step(song, track, pat, row); }

static Step *step_w(int row)
{
    Step *st = song_step_w(song, track, pat, row);
    pool_full = st == 0;
    return st;
}

/* The note an empty slot starts from: its neighbour on the left, else
 * the first note of the nearest step above, else middle C. */
static uint8_t default_note(int row, int k)
{
    const Step *st = step_at(row);
    if (k > 0 && st->notes[k - 1] <= NOTE_MAX)
        return st->notes[k - 1];
    for (int r = row - 1; r >= 0; r--)
        if (step_at(r)->notes[0] <= NOTE_MAX)
            return step_at(r)->notes[0];
    return 60;
}

static void write_note(int row, int k, uint8_t note)
{
    Step *st = step_w(row);
    if (!st)
        return;
    if (st->notes[0] == NOTE_OFF)
        st->notes[0] = NOTE_NONE; /* a cut and a note cannot share a step */
    st->notes[k] = note;
}

/* KNOB 1 on a note: the semitones, and under the lowest one OFF (on N1
 * only -- a cut is the whole step's) and then the empty slot. The
 * encoders have no push: emptying is turning all the way down. */
static void turn_note(int row, int k, int d)
{
    uint8_t n = step_at(row)->notes[k];
    int v = n <= NOTE_MAX ? n : n == NOTE_OFF ? NOTE_MIN - 1 : NOTE_MIN - 2;
    Step *st;

    if (v < NOTE_MIN && d > 0)
        v = default_note(row, k) + d - 1;
    else
        v += d;
    if (v > NOTE_MAX)
        v = NOTE_MAX;
    if (v < NOTE_MIN - 2)
        v = NOTE_MIN - 2;
    if (v == NOTE_MIN - 1 && k != 0)
        v = NOTE_MIN - 2;
    if (v >= NOTE_MIN) {
        write_note(row, k, (uint8_t)v);
        return;
    }
    if (!(st = step_w(row)))
        return;
    if (v == NOTE_MIN - 1)
        for (int i = 0; i < STEP_MAX_NOTES; i++)
            st->notes[i] = i == 0 ? NOTE_OFF : NOTE_NONE;
    else
        st->notes[k] = NOTE_NONE;
}

static void turn_octave(int row, int k, int d)
{
    int n = step_at(row)->notes[k];
    if (n > NOTE_MAX)
        return;
    n += d * 12;
    while (n > NOTE_MAX)
        n -= 12;
    while (n < NOTE_MIN)
        n += 12;
    write_note(row, k, (uint8_t)n);
}

static void turn_instr(int row, int d)
{
    const Step *rd = step_at(row);
    int v = rd->instrument == INSTR_NONE ? -1 : rd->instrument;
    Step *st;
    v += d;
    v = v < -1 ? -1 : v >= SONG_MAX_INSTR ? SONG_MAX_INSTR - 1 : v;
    if ((st = step_w(row)) != 0)
        st->instrument = v < 0 ? INSTR_NONE : (uint8_t)v;
}

/* KNOB 1 on an fx: the commands in alphabetical order, --- first. One
 * that would say the same thing twice on the row is stepped over. */
static void turn_fx_cmd(int row, int slot, int d)
{
    Step *st = step_w(row);
    int at = 0;
    if (!st)
        return;
    for (int i = 0; i < fx_count; i++)
        if (fx_order[i] == st->fx[slot].cmd)
            at = i;
    for (int n = d < 0 ? -d : d; n > 0; n--)
        for (int guard = 0; guard < fx_count; guard++) {
            at += d < 0 ? -1 : 1;
            if (at < 0 || at >= fx_count) {
                at = at < 0 ? 0 : fx_count - 1;
                break;
            }
            if (!fx_conflicts(st, slot, fx_order[at]))
                break;
        }
    if (fx_conflicts(st, slot, fx_order[at]))
        return;
    st->fx[slot].cmd = fx_order[at];
    st->fx[slot].val = st->fx[slot].cmd == FX_NONE
                           ? 0
                           : fx_clamp_val(st->fx[slot].cmd, song->ticks_per_step,
                                          st->fx[slot].val);
}

/* part 0: the whole value. 1 and 2: the high and the low nibble of a pair. */
static void turn_fx_val(int row, int slot, int part, int d)
{
    Step *st = step_at(row)->fx[slot].cmd != FX_NONE ? step_w(row) : 0;
    int v, hi, lo;
    if (!st)
        return;
    v = st->fx[slot].val;
    hi = v >> 4;
    lo = v & 15;
    if (part == 1)
        hi += d;
    else if (part == 2)
        lo += d;
    else
        v += d;
    if (part) {
        hi = hi < 0 ? 0 : hi > 15 ? 15 : hi;
        lo = lo < 0 ? 0 : lo > 15 ? 15 : lo;
        v = hi << 4 | lo;
    }
    st->fx[slot].val = fx_clamp_val(st->fx[slot].cmd, song->ticks_per_step, v);
}

static void clear_slot(int row, int col)
{
    Step *st = step_is_empty(step_at(row)) ? 0 : step_w(row);
    if (!st)
        return;
    if (col == COL_STEP) {
        for (int i = 0; i < STEP_MAX_NOTES; i++)
            st->notes[i] = NOTE_NONE;
        st->instrument = INSTR_NONE;
        for (int i = 0; i < STEP_MAX_FX; i++)
            st->fx[i].cmd = st->fx[i].val = 0;
    } else if (col <= COL_N4) {
        st->notes[col - COL_N1] = NOTE_NONE;
    } else if (col == COL_IN) {
        st->instrument = INSTR_NONE;
    } else {
        st->fx[col - COL_FX1].cmd = st->fx[col - COL_FX1].val = 0;
    }
}

/* PRESETS: the blocks of this track that hold something or are played
 * somewhere, in order, wrapping. It is the PHRASE's cell that changes:
 * the phrase plays what the screen shows. */
static void step_block(int d)
{
    for (int guard = 0; guard < TRACK_MAX_PATTERNS; guard++) {
        bool used = false;
        pat = (pat + (d < 0 ? TRACK_MAX_PATTERNS - 1 : 1)) % TRACK_MAX_PATTERNS;
        for (int i = 0; i < song->arrange_len; i++)
            used |= song->arrangement[i].pattern[track] == pat;
        if (used || !song_pattern_blank(song, track, pat))
            break;
    }
    song->arrangement[entry].pattern[track] = (uint8_t)pat;
    if (cur_row >= pat_len())
        cur_row = pat_len() - 1;
}

/* --------------------------------------------------------- drawing --- */
static int play_row = -1;
static uint32_t held_btns;
static bool is_playing;

static void slot_bg(int x, int y, int w, bool full, bool cur, uint16_t tint, int a)
{
    if (cur)
        gfx_slab(x, y, w, 10, C_ACCENT, 255);
    else if (full)
        gfx_slab(x, y, w, 10, tint, a);
    else
        gfx_frame_rect(x, y, w, 10, C_DIM, 76);
}

static void card(int k, const char *label, const char *value, const char *unit)
{
    int w = (INNER - 6) / 4, x = X0 + k * (w + 2);
    char kn[3] = { 'K', (char)('1' + k), '\0' };
    gfx_slab(x, CARDS_Y, w, CARDS_H, C_TEXT, label ? 20 : 8);
    gfx_text(F_MONO8, gfx_text(F_MONO8, x + 3, CARDS_Y + 8, kn, label ? C_ACCENT : C_DIM) + 3,
             CARDS_Y + 8, label ? label : "", C_DIM);
    if (value) {
        int vx = gfx_text(F_COND12, x + 3, CARDS_Y + 19, value, C_TEXT);
        if (unit)
            gfx_text(F_MONO8, vx + 3, CARDS_Y + 19, unit, C_DIM);
    }
}

static void draw_cards(void)
{
    const Step *st = step_at(cur_row);
    char a[8], b[8];

    if (cur_col == COL_STEP) {
        card(0, "LEN", dec(a, pat_len()), "steps");
        card(1, 0, 0, 0);
        card(2, 0, 0, 0);
    } else if (cur_col <= COL_N4) {
        uint8_t n = st->notes[cur_col - COL_N1];
        card(0, "NOTE", n <= NOTE_MAX ? note_name(n, a) : n == NOTE_OFF ? "OFF" : "---", 0);
        if (n <= NOTE_MAX)
            card(1, "OCT", dec(b, n / 12 - 1), 0);
        else
            card(1, 0, 0, 0);
        card(2, 0, 0, 0);
    } else if (cur_col == COL_IN) {
        bool has = st->instrument != INSTR_NONE;
        card(0, "INSTR", has ? hex2(a, st->instrument) : "--",
             has ? song->instruments[st->instrument % SONG_MAX_INSTR].name : 0);
        card(1, 0, 0, 0);
        card(2, 0, 0, 0);
    } else {
        StepFx f = st->fx[cur_col - COL_FX1];
        card(0, "EFFECT", fx_name(f.cmd), 0);
        if (f.cmd == FX_NONE) {
            card(1, 0, 0, 0);
            card(2, 0, 0, 0);
        } else if (fx_is_pair(f.cmd)) {
            a[0] = "0123456789ABCDEF"[f.val >> 4];
            b[0] = "0123456789ABCDEF"[f.val & 15];
            a[1] = b[1] = '\0';
            card(1, "SPEED", a, 0);
            card(2, f.cmd == FX_SCP ? "SEMIS" : "DEPTH", b, 0);
        } else {
            card(1, "VALUE", hex2(a, f.val), 0);
            card(2, 0, 0, 0);
        }
    }
    card(3, 0, 0, 0);
}

static void draw_row(int r, int y)
{
    const Step *st = step_at(r);
    bool playing = r == play_row;
    char t[4];

    if (playing) {
        gfx_slab(X0, y, INNER, ROW_H, C_PLAY, 255);
        gfx_fill(X0 + 1, y, 3, ROW_H, C_ACCENT, 255);
    } else {
        gfx_slab(X0, y, INNER, ROW_H, C_TEXT, r % 16 == 0 ? 43 : r % 4 == 0 ? 26 : 11);
    }
    if (cur_row == r && cur_col == COL_STEP)
        gfx_slab(col_x[0], y + 1, col_w[0], 10, C_ACCENT, 255);
    gfx_text_mid(F_MONO8, col_x[0], col_w[0], y + 9, hex2(t, r),
                 cur_row == r && cur_col == COL_STEP ? C_ON_ACC
                 : r % 16 == 0 ? C_TEXT : r % 4 == 0 ? C_BEAT : C_DIM);
    for (int k = 0; k < STEP_MAX_NOTES; k++) {
        int c = COL_N1 + k;
        uint8_t n = st->notes[k];
        bool cur = cur_row == r && cur_col == c, full = n != NOTE_NONE;
        slot_bg(col_x[c], y + 1, col_w[c], full, cur, C_TEXT, 18);
        if (full)
            gfx_text_mid(F_COND11, col_x[c], col_w[c], y + 10,
                         n == NOTE_OFF ? "OFF" : note_name(n, t), cur ? C_ON_ACC : C_TEXT);
    }
    {
        bool cur = cur_row == r && cur_col == COL_IN, full = st->instrument != INSTR_NONE;
        slot_bg(col_x[COL_IN], y + 1, col_w[COL_IN], full, cur, C_TITLE, 26);
        if (full)
            gfx_text_mid(F_MONO8, col_x[COL_IN], col_w[COL_IN], y + 9,
                         hex2(t, st->instrument), cur ? C_ON_ACC : C_TITLE);
    }
    for (int k = 0; k < STEP_MAX_FX; k++) {
        int c = COL_FX1 + k, x = col_x[c];
        StepFx f = st->fx[k];
        bool cur = cur_row == r && cur_col == c, full = f.cmd != FX_NONE;
        slot_bg(x, y + 1, col_w[c], full, cur, C_FX, 28);
        if (full) {
            int w = gfx_text_w(F_COND9, fx_name(f.cmd)) + 2 + gfx_text_w(F_MONO8, hex2(t, f.val));
            int tx = gfx_text(F_COND9, x + (col_w[c] - w) / 2, y + 9, fx_name(f.cmd),
                              cur ? C_ON_ACC : C_FX);
            gfx_text(F_MONO8, tx + 2, y + 9, t, cur ? C_ON_ACC : C_TEXT);
        }
    }
}

static void draw(void)
{
    static const char *const head[COLS] = { "#", "N1", "N2", "N3", "N4", "IN", "FX1", "FX2", "FX3" };
    static const char names[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    char t[12], k[12];
    int x, len = pat_len();

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);

    /* The bar: where one is, and the way out. */
    t[0] = (char)0xE2, t[1] = (char)0x80, t[2] = (char)0xB9;
    t[3] = ' ', t[4] = 'P', t[5] = 'H', t[6] = 'R', t[7] = ' ';
    hex2(t + 8, entry);
    x = gfx_text(F_COND12, X0, NAV_Y + 13, t, C_TITLE);
    k[0] = 'T';
    dec(k + 1, track + 1);
    x = gfx_text(F_COND12, x + 6, NAV_Y + 13, k, C_TEXT);
    gfx_slab(x + 6, NAV_Y + 3, 40, 12, C_TEXT, 18);
    gfx_text(F_COND12, gfx_text(F_MONO8, x + 10, NAV_Y + 12, "LEN", C_DIM) + 3, NAV_Y + 13,
             dec(k, len), C_TEXT);
    gfx_slab(X0 + INNER - 36, NAV_Y + 3, 36, 12, C_TEXT, 18);
    gfx_text(F_COND12, gfx_text(F_MONO8, X0 + INNER - 32, NAV_Y + 12, "PAT", C_DIM) + 3,
             NAV_Y + 13, hex2(k, pat), C_TEXT);
    if (pool_full)
        gfx_text(F_COND9, x + 52, NAV_Y + 12, "POOL FULL", C_MUTE);
    else if (saved)
        gfx_text(F_COND9, x + 52, NAV_Y + 12, saved > 0 ? "SAVED" : "NOT SAVED",
                 saved > 0 ? C_ACCENT : C_MUTE);
    gfx_fill(X0, NAV_Y + NAV_H, INNER, 1, C_DIM, 77);

    draw_cards();
    gfx_fill(X0, CARDS_Y + CARDS_H + 3, INNER, 1, C_ACCENT, 71);

    /* The left hand's manual: one line, always. */
    x = gfx_text(F_MONO8, X0, LEFT_Y + 8, "SELECT", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "STEP", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "ALGO", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "FIELD", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "PRESETS", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "PAT", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "OCT", C_DIM);
    {   /* where the 27 keys are: F3..G5 unmoved */
        int lo = 53 + kbd_oct * 12, hi = lo + 26;
        k[0] = names[lo % 12 * 2], k[1] = (char)('0' + lo / 12 - 1), k[2] = '-';
        k[3] = names[hi % 12 * 2], k[4] = (char)('0' + hi / 12 - 1), k[5] = '\0';
        gfx_text(F_MONO8, x + 3, LEFT_Y + 8, k, C_TEXT);
    }

    for (int c = 0; c < COLS; c++)
        gfx_text_mid(F_MONO8, col_x[c], col_w[c], HEAD_Y + 7, head[c],
                     c == cur_col ? C_ACCENT : C_DIM);

    for (int i = 0; i < ROWS && top + i < len; i++)
        draw_row(top + i, ROWS_Y + i * ROW_PITCH);
    {   /* the two seams: notes | instrument | fx are three different things */
        int n = len - top < ROWS ? len - top : ROWS;
        gfx_dash_v(112, HEAD_Y, ROWS_Y - HEAD_Y + n * ROW_PITCH - 1, C_TITLE, 90);
        gfx_dash_v(133, HEAD_Y, ROWS_Y - HEAD_Y + n * ROW_PITCH - 1, C_TITLE, 90);
    }

    /* The twelve buttons, where they are under the hand. */
    for (int b = 0; b < BTNS; b++) {
        int w = (INNER - 10) / 6, bx = X0 + (b % 6) * (w + 2), y = BTN_Y + (b / 6) * (BTN_H + 1);
        bool down = (held_btns >> btn_of[b]) & 1u, on = b == B_PLAY && is_playing;
        const char *word = b == B_PLAY && is_playing ? "STOP" : btn_word[b];
        gfx_slab(bx, y, w, BTN_H, on || down ? C_ACCENT : C_TEXT,
                 on || down ? 255 : btn_live[b] ? 18 : 8);
        gfx_text_mid(F_COND9, bx, w, y + 8, word,
                     on || down ? C_ON_ACC : !btn_live[b] ? C_BEAT : b == B_BACK ? C_TITLE : C_TEXT);
    }
}

/* ------------------------------------------------------------- api --- */
bool pattern_open(Song *s, int e, int t)
{
    if (e < 0 || e >= s->arrange_len || t < 0 || t >= s->num_tracks ||
        s->arrangement[e].pattern[t] >= TRACK_MAX_PATTERNS)
        return false;
    song = s;
    entry = e;
    track = t;
    pat = s->arrangement[e].pattern[t];
    cur_row = top = 0;
    cur_col = COL_N1;
    pool_full = false;
    saved = 0;
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    /* The picker's order: alphabetical, the empty slot first. */
    fx_count = 0;
    fx_order[fx_count++] = FX_NONE;
    for (int c = 1; c < FX_SYL; c++) {
        int i = fx_count++;
        while (i > 1) {
            const char *a = fx_name(fx_order[i - 1]), *b = fx_name((uint8_t)c);
            int k = 0;
            while (k < 3 && a[k] == b[k])
                k++;
            if (k == 3 || a[k] < b[k])
                break;
            fx_order[i] = fx_order[i - 1];
            i--;
        }
        fx_order[i] = (uint8_t)c;
    }
    gfx_invalidate();
    return true;
}

bool pattern_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
    int len = pat_len(), d;
#define PRESSED(b) ((btns >> btn_of[b]) & 1u)

    prev_btns = in->btns;
    prev_keys = in->keys;
    held_btns = in->btns;
    is_playing = player_playing();

    /* The left hand moves. */
    cur_row += in->enc[PE_SELECT];
    cur_row = cur_row < 0 ? 0 : cur_row >= len ? len - 1 : cur_row;
    cur_col += in->enc[PE_ALGO];
    cur_col = cur_col < 0 ? 0 : cur_col >= COLS ? COLS - 1 : cur_col;
    if (in->enc[PE_PRESET])
        step_block(in->enc[PE_PRESET]);
    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;

    /* The right hand writes: KNOB 1-4 are the cards. */
    if ((d = in->enc[PE_K1]) != 0) {
        if (cur_col == COL_STEP) {
            int n = len + d;
            song->patterns[track][pat].length =
                (uint16_t)(n < 1 ? 1 : n > PATTERN_MAX_STEPS ? PATTERN_MAX_STEPS : n);
        } else if (cur_col <= COL_N4) {
            turn_note(cur_row, cur_col - COL_N1, d);
        } else if (cur_col == COL_IN) {
            turn_instr(cur_row, d);
        } else {
            turn_fx_cmd(cur_row, cur_col - COL_FX1, d);
        }
    }
    if ((d = in->enc[PE_K2]) != 0) {
        if (cur_col >= COL_N1 && cur_col <= COL_N4)
            turn_octave(cur_row, cur_col - COL_N1, d);
        else if (cur_col >= COL_FX1)
            turn_fx_val(cur_row, cur_col - COL_FX1,
                        fx_is_pair(step_at(cur_row)->fx[cur_col - COL_FX1].cmd) ? 1 : 0, d);
    }
    if ((d = in->enc[PE_K3]) != 0 && cur_col >= COL_FX1 &&
        fx_is_pair(step_at(cur_row)->fx[cur_col - COL_FX1].cmd))
        turn_fx_val(cur_row, cur_col - COL_FX1, 2, d);

    /* The keys write notes: the first one in the slot under the cursor,
     * the ones pressed while it is held in the slots after it. */
    if (!in->keys)
        chord_next = 0;
    for (int k = 0; k < PLAT_KEYS; k++) {
        int n = 53 + kbd_oct * 12 + k, slot;
        if (!((keys >> k) & 1u) || n < NOTE_MIN || n > NOTE_MAX)
            continue;
        slot = chord_next ? chord_next
               : cur_col >= COL_N1 && cur_col <= COL_N4 ? cur_col - COL_N1 : 0;
        if (slot >= STEP_MAX_NOTES)
            continue;
        write_note(cur_row, slot, (uint8_t)n);
        chord_next = slot + 1;
    }

    /* SAVED stays up until the hand does something else. */
    if (btns || keys || in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3])
        saved = 0;
    if (PRESSED(B_SAVE))
        saved = app_save() == 0 ? 1 : -1;
    if (PRESSED(B_CLEAR))
        clear_slot(cur_row, cur_col);
    if (PRESSED(B_FX)) {
        /* to the first free fx slot of the step, else the first */
        const Step *st = step_at(cur_row);
        cur_col = COL_FX1;
        for (int i = STEP_MAX_FX - 1; i >= 0; i--)
            if (st->fx[i].cmd == FX_NONE)
                cur_col = COL_FX1 + i;
    }
    if (PRESSED(B_PLAY)) {
        if (is_playing)
            player_stop();
        else
            player_play_entry(entry);
    }

    len = pat_len();
    if (cur_row >= len)
        cur_row = len - 1;
    if (cur_row < top)
        top = cur_row;
    if (cur_row >= top + ROWS)
        top = cur_row - ROWS + 1;
    play_row = is_playing && player_entry() == entry ? player_row() % len : -1;
    gfx_frame(draw);
    if (PRESSED(B_BACK)) {
        song_pool_compact(song);
        return false;
    }
    return true;
#undef PRESSED
}
