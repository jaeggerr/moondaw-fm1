/* SPDX-License-Identifier: GPL-3.0-only */
#include "instrument.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "namer.h"
#include "ui.h"
#include "undo.h"

/* INSTRUMENT, as agreed on 28/10 (NOTES.md): the name first, PLAY a preview of the sound,
 * then the settings by importance -- those that go together on one row, and the column
 * names at the top change with the row.
 *
 *   NAME    SEL types it
 *   SOUND   ENGINE TIMBRE           ENV     ATK DEC SUS REL
 *   FILTER  CUT RES ENV VEL         LEVEL   GAIN LEVEL VIB
 *   VOICE   TRACT ACCENT (a VOX instrument only)
 *   LYR n   SOUND SEMI FINE GAIN, one row a layer; + LYR makes one (KNOB 3 up)
 *
 *   SELECT, KNOB 1  the row     KNOB 2  the column     KNOB 3  the value
 *   PLAY    the preview: a few notes up, round and round, until PLAY again
 *   the keys  play the sound, OCT-/OCT+ their octave     PRESETS  the next instrument
 *   GLO     back to PATTERN     ALGO  UNDO left, REDO right     SAVE  as printed
 *
 * Five cells of 33 px a row; a column takes one or two (an engine's name). */
enum { LABEL_X = 8, CELL_X = 66, CELL_W = 33 };
enum { L_NAME, L_SOUND, L_ENV, L_FILTER, L_LEVEL, L_VOICE, L_LAYER };

static int inst, row, col, top, kbd_oct;
static bool naming;
static const char *said;
static uint32_t prev_btns, prev_keys;
/* The preview: when it started, and the note it is on (-1 none yet). */
static bool previewing;
static uint32_t preview_t0;
static int preview_at;

static InstrumentDef *I(void) { return &app_song.instruments[inst]; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------- rows --- */
/* The rows as drawn: the fixed ones, VOICE only for a voice, a row a layer,
 * and + LYR while there is room for one more. */
static int nrows(void)
{
    return L_VOICE + (I()->engine == ENGINE_VOX) + I()->nlayers + (I()->nlayers < INSTR_MAX_LAYERS);
}

static int kind(int r)
{
    if (r < L_VOICE)
        return r;
    if (I()->engine == ENGINE_VOX && r == L_VOICE)
        return L_VOICE;
    return L_LAYER;
}

/* The layer row r stands for, nlayers for + LYR. */
static int layer_of(int r) { return r - L_VOICE - (I()->engine == ENGINE_VOX); }

static int ncols(int r)
{
    static const int n[] = { 0, 2, 4, 4, 3, 2 };
    int k = kind(r);
    return k == L_LAYER ? (layer_of(r) < I()->nlayers ? 4 : 1) : n[k];
}

static int span(int r, int c)
{
    int k = kind(r);
    return (k == L_SOUND) || (k == L_LAYER && c == 0) ? 2 : 1;
}

static int col_x(int r, int c)
{
    int x = CELL_X;
    for (int k = 0; k < c; k++)
        x += span(r, k) * CELL_W;
    return x;
}

/* ------------------------------------------------------------ words --- */
static const char *timbre_word(uint8_t engine, uint8_t timbre)
{
    static const char *const duty[4] = { "12.5%", "25%", "50%", "75%" };
    static const char *const len[2] = { "LONG", "SHORT" };
    static const char *const shape[5] = { "SAW", "TRI", "SINE", "HALF", "ABS" };
    static const char *const style[5] = { "GLIDE", "HARD", "SOFT", "MELO", "SINGR" };
    if (engine == ENGINE_PULSE)
        return duty[timbre & 3];
    if (engine == ENGINE_NOISE)
        return len[timbre & 1];
    if (engine == ENGINE_WAVE)
        return shape[timbre < 5 ? timbre : 0];
    if (engine == ENGINE_VOX)
        return style[timbre < 5 ? timbre : 0];
    return "--";
}

static char *sdec(char *b, int v)
{
    int i = 0;
    if (v < 0) {
        b[i++] = '-';
        v = -v;
    } else if (v > 0) {
        b[i++] = '+';
    }
    ui_dec(b + i, v);
    return b;
}

/* A layer's engine and timbre are one knob: 0 is no layer, then the four
 * duties, the two noise lengths, the five shapes. A layer shows its timbre
 * alone: each belongs to one engine. */
#define WAVES 12
static int wave_of(const Layer *l)
{
    return l->engine == ENGINE_PULSE ? 1 + (l->timbre & 3)
           : l->engine == ENGINE_NOISE ? 5 + (l->timbre & 1) : 7 + (l->timbre < 5 ? l->timbre : 0);
}

static void wave_set(Layer *l, int w)
{
    l->engine = w < 5 ? ENGINE_PULSE : w < 7 ? ENGINE_NOISE : ENGINE_WAVE;
    l->timbre = (uint8_t)(w < 5 ? w - 1 : w < 7 ? w - 5 : w - 7);
}

static const char *value(int r, int c, char *b)
{
    const InstrumentDef *in = I();
    switch (kind(r)) {
    case L_SOUND:
        return c == 0 ? engine_name(in->engine) : timbre_word(in->engine, in->timbre);
    case L_ENV:
        return ui_dec(b, c == 0 ? in->attack : c == 1 ? in->decay : c == 2 ? in->sustain : in->release);
    case L_FILTER:
        if (c == 0)
            return in->cutoff == FILTER_OPEN ? "OPEN" : ui_dec(b, in->cutoff);
        if (c == 1)
            return ui_dec(b, in->reso);
        return sdec(b, (c == 2 ? in->filt_env : in->filt_vel) - FILTER_ENV_NONE);
    case L_LEVEL:
        if (c == 2)
            return in->vib_phase == VIB_PHASE_FREE ? "FREE" : ui_dec(b, in->vib_phase);
        return sdec(b, c == 0 ? in->gain : in->level);
    case L_VOICE:
        if (c == 1)
            return in->accent == VOX_ACCENT_EN ? "EN" : "FR";
        ui_dec(b, in->tract ? in->tract : 100);
        for (char *p = b;; p++)
            if (!*p) {
                p[0] = '%', p[1] = 0;
                break;
            }
        return b;
    default: {
        const Layer *l = &in->layers[layer_of(r)];
        if (c == 0)
            return timbre_word(l->engine, l->timbre);
        return sdec(b, c == 1 ? l->transpose : c == 2 ? l->detune : l->gain);
    }
    }
}

/* ------------------------------------------------------------ edits --- */
/* The engines that sound here, in KNOB 3's order. One that does not (SMPL,
 * PLUCK) leaves for the first of them at a turn. */
static const uint8_t engines[] = { ENGINE_PULSE, ENGINE_NOISE, ENGINE_WAVE, ENGINE_VOX };

static void turn(int d)
{
    InstrumentDef *in = I();
    int k = kind(row);

    undo_bytes(in, sizeof *in);
    if (k == L_SOUND && col == 0) {
        int at = -1;
        for (int i = 0; i < (int)sizeof engines; i++)
            if (engines[i] == in->engine)
                at = i;
        in->engine = engines[at < 0 ? 0 : clampi(at + d, 0, (int)sizeof engines - 1)];
        if (in->timbre > engine_timbre_max(in->engine))
            in->timbre = engine_timbre_max(in->engine);
    } else if (k == L_SOUND) {
        in->timbre = (uint8_t)clampi(in->timbre + d, 0, engine_timbre_max(in->engine));
    } else if (k == L_ENV) {
        uint8_t *f[4] = { &in->attack, &in->decay, &in->sustain, &in->release };
        *f[col] = (uint8_t)clampi(*f[col] + d, 0, 255);
    } else if (k == L_FILTER) {
        uint8_t *f[4] = { &in->cutoff, &in->reso, &in->filt_env, &in->filt_vel };
        *f[col] = (uint8_t)clampi(*f[col] + d, 0, col < 2 ? 255 : 127);
    } else if (k == L_LEVEL) {
        if (col == 2)
            in->vib_phase = (int16_t)clampi(in->vib_phase + d, VIB_PHASE_FREE, 255);
        else if (col == 0)
            in->gain = (int8_t)clampi(in->gain + d, GAIN_MIN_DB, GAIN_MAX_DB);
        else
            in->level = (int8_t)clampi(in->level + d, GAIN_MIN_DB, GAIN_MAX_DB);
    } else if (k == L_VOICE) {
        if (col == 0)
            in->tract = (uint8_t)clampi((in->tract ? in->tract : 100) + d, VOX_TRACT_MIN, VOX_TRACT_MAX);
        else
            in->accent = (uint8_t)clampi(in->accent + d, VOX_ACCENT_FR, VOX_ACCENT_EN);
    } else {
        int j = layer_of(row);
        if (j >= in->nlayers) { /* + LYR: turning up makes the layer */
            if (d <= 0)
                return;
            j = in->nlayers++;
            in->layers[j].transpose = in->layers[j].detune = in->layers[j].gain = 0;
            wave_set(&in->layers[j], clampi(d, 1, WAVES - 1));
        } else {
            Layer *l = &in->layers[j];
            if (col == 0) {
                int w = clampi(wave_of(l) + d, 0, WAVES - 1);
                if (w == 0) { /* all the way down: the layer goes, the ones under it move up */
                    for (int i = j; i < in->nlayers - 1; i++)
                        in->layers[i] = in->layers[i + 1];
                    in->nlayers--;
                } else {
                    wave_set(l, w);
                }
            } else if (col == 1) {
                l->transpose = (int8_t)clampi(l->transpose + d, -48, 48);
            } else if (col == 2) {
                l->detune = (int8_t)clampi(l->detune + d, -100, 100);
            } else {
                l->gain = (int8_t)clampi(l->gain + d, GAIN_MIN_DB, GAIN_MAX_DB);
            }
        }
    }
    app_mark_dirty();
}

/* The preview: C E G C an octave on, a note every 250 ms, a rest, again. */
static void preview(uint32_t ms)
{
    static const uint8_t tune[] = { 60, 64, 67, 72, 0, 0 };
    int at = (int)((ms - preview_t0) / 250u % sizeof tune);
    if (at == preview_at)
        return;
    preview_at = at;
    if (tune[at])
        player_note_on(inst, (uint8_t)(tune[at] + 12 * kbd_oct));
    else
        player_note_off();
}

/* ---------------------------------------------------------- drawing --- */
static void draw(void)
{
    static const char *const heads[][4] = {
        { 0 }, { "ENGINE", "TIMBRE" }, { "ATK", "DEC", "SUS", "REL" }, { "CUT", "RES", "ENV", "VEL" },
        { "GAIN", "LEVEL", "VIB" }, { "TRACT", "ACCENT" }, { "SOUND", "SEMI", "FINE", "GAIN" },
    };
    const InstrumentDef *in = I();
    char s[12];
    int n = nrows();

    grid_clear();
    if (said)
        grid_corner(said, grid_said_ink(said), 0, 0);
    else
        grid_corner("INS", C_DIM, grid_hex2(s, inst), C_TITLE);
    if (naming) {
        namer_draw();
        return;
    }
    if (layer_of(row) < in->nlayers || kind(row) != L_LAYER) /* the column names: this row's */
        for (int c = 0; c < ncols(row); c++)
            grid_head(col_x(row, c), span(row, c) * CELL_W, heads[kind(row)][c], c == col);
    for (int v = 0; v < GRID_ROWS && top + v < n; v++) {
        int r = top + v, k = kind(r);
        bool band = r == row, adder = k == L_LAYER && layer_of(r) >= in->nlayers;
        if (band)
            grid_band(v, PLAT_W - 4 - (n > GRID_ROWS ? 6 : 0));
        if (k == L_NAME && previewing) /* what plays */
            for (int i = 0; i < 6; i++)
                gfx_fill(PLAT_W - 16 + i, grid_y(v) + 8 + i, 1, 12 - 2 * i, band ? C_ON_ACC : C_TITLE, 255);
        {
            static const char *const label[] = { "NAME", "SOUND", "ENV", "FILTER", "LEVEL", "VOICE" };
            char lyr[6] = "LYR 1";
            lyr[4] = (char)('1' + layer_of(r));
            gfx_text(F_COND20, LABEL_X, grid_base(v), k != L_LAYER ? label[k] : adder ? "+ LYR" : lyr,
                     band ? C_ON_ACC : C_DIM);
        }
        if (k == L_NAME) {
            if (band)
                grid_hollow(v, CELL_X - 4, 130);
            gfx_text(F_COND20, CELL_X, grid_base(v), in->name[0] ? in->name : "--", band ? C_ACCENT : C_TEXT);
            continue;
        }
        if (adder)
            continue;
        for (int c = 0; c < ncols(r); c++) {
            bool on = band && c == col, silent = k == L_SOUND && c == 0 && !engine_sounds(in->engine) &&
                                                 in->engine != ENGINE_VOX;
            grid_cell(v, col_x(r, c), span(r, c) * CELL_W, value(r, c, s), band, on, silent ? C_FX : C_TEXT);
        }
    }
    gfx_fill(CELL_X - 5, GRID_HEAD, 1, (n < GRID_ROWS ? n : GRID_ROWS) * GRID_ROW, C_DIM, 255);
    if (n > GRID_ROWS) {
        int h = GRID_ROWS * GRID_ROW * GRID_ROWS / n, y = GRID_HEAD + GRID_ROWS * GRID_ROW * top / n;
        gfx_fill(PLAT_W - 4, GRID_HEAD, 2, GRID_ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
}

/* -------------------------------------------------------------- api --- */
void instrument_open(int instr, int e)
{
    (void)e; /* PLAY is the preview here, not the phrase */
    inst = instr < 0 || instr >= SONG_MAX_INSTR ? 0 : instr;
    row = col = top = 0;
    naming = previewing = false;
    said = 0;
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

bool instrument_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    if (btns || keys || in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1] || in->enc[PE_K3])
        said = 0;

    if (naming) {
        naming = namer_frame(in, btns) == NAMER_TYPING;
        if (!naming)
            app_mark_dirty();
        ui_led_guide(0); /* the namer: SEL keeps, GLO goes back */
        gfx_frame(draw);
        return true;
    }

    if (in->enc[PE_PRESET]) {
        inst = clampi(inst + in->enc[PE_PRESET], 0, SONG_MAX_INSTR - 1);
        row = row >= nrows() ? nrows() - 1 : row;
    }
    row = clampi(row + in->enc[PE_SELECT] + in->enc[PE_K1], 0, nrows() - 1);
    col = clampi(col + in->enc[PE_K2], 0, ncols(row) ? ncols(row) - 1 : 0);
    if (in->enc[PE_K3] && kind(row) != L_NAME) {
        turn(in->enc[PE_K3]);
        row = row >= nrows() ? nrows() - 1 : row; /* a layer gone, or VOICE */
        col = clampi(col, 0, ncols(row) ? ncols(row) - 1 : 0);
    }
    if (PRESSED(PB_SEL) && kind(row) == L_NAME) {
        namer_open(I()->name, INSTR_NAME_LEN - 1);
        naming = true;
    }

    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;
    /* The keys play the sound: the last one pressed, until all are up. */
    for (int k = 0; k < PLAT_KEYS; k++) {
        int n = 53 + kbd_oct * 12 + k;
        if (((keys >> k) & 1u) && n >= NOTE_MIN && n <= NOTE_MAX) {
            previewing = false;
            player_note_on(inst, (uint8_t)n);
        }
    }
    if (!in->keys && prev_keys && prev_keys != 0xFFFFFFFFu && !previewing)
        player_note_off();
    prev_keys = in->keys;

    if (PRESSED(PB_PLAY)) { /* the preview, not the song */
        if (player_playing())
            player_stop();
        previewing = !previewing;
        preview_t0 = in->ms;
        preview_at = -1;
        if (!previewing)
            player_note_off();
    }
    if (previewing)
        preview(in->ms);
    undo_turn(in->enc[PE_ALGO], &said);
    if (PRESSED(PB_SAVE))
        said = app_save() == 0 ? "SAVED" : "NOT SAVED";

    grid_follow(&row, &top, nrows(), GRID_ROWS);
    ui_led_state(PB_PLAY, previewing);
    ui_led_guide(1u << PB_OCTDN | 1u << PB_OCTUP); /* the keys' octave */
    gfx_frame(draw);
    if (PRESSED(PB_GLO)) {
        previewing = false;
        player_note_off();
        return false;
    }
    return true;
#undef PRESSED
}
