/* SPDX-License-Identifier: GPL-3.0-only */
#include "instrument.h"

#include "../core/bank.h"
#include "../core/drum.h"
#include "../core/player.h"
#include "app.h"
#include "edit.h"
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
 *   DRUM    TUNE TONE DECAY SNAP (a DRUM kit only: 64 as designed)
 *   LAY n   SOUND SEMI FINE GAIN, one row a layer; + LAY makes one (KNOB 3 up)
 *
 *   SELECT, KNOB 1  the row     KNOB 2  the column     KNOB 3  the value
 *   PLAY    the preview: a few notes up, round and round, until PLAY again
 *   the keys  play the sound, OCT-/OCT+ their octave (a kit: the first C its kick)
 *   PRESETS  the next instrument
 *   HOME    back to PATTERN  GLO  the PROJECT     ALGO  UNDO left, REDO right     SAVE  as printed
 *
 * Five cells of 33 px a row; a column takes one or two (an engine's name). */
enum { LABEL_X = 8, CELL_X = 66, CELL_W = 33 };
enum { L_NAME, L_SOUND, L_ENV, L_FILTER, L_LEVEL, L_VOICE, L_DRUM, L_SAMPLE, L_LAYER };

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

/* ---------------------------------------------------------- accents ---
 * A voice made on the ENGINE knob sings in English (30/10, his): the
 * phoneme set is one alphabet and the accent only says how it is read,
 * and what he writes on the machine he writes in English.
 *
 * Only a voice MADE here, though. `accent` is one byte and FR is its
 * zero, so a voice that chose French and a fresh instrument that never
 * chose anything look alike -- and the engine knob can never land on VOX
 * while already on it (VOX sits mid-list), so "was it a voice a moment
 * ago" cannot tell them apart either. Hence this: the instruments that
 * already carry an accent somebody chose, which is the voices of the song
 * as the screen opened it, plus any this screen has since made a voice.
 * Turning away from VOX and back therefore keeps the FR a file gave it or
 * he set by hand; it holds for as long as the screen is open.
 *
 * Nothing of this reaches a file. A `.trk` with no `accent` line still
 * means French, `core/trk.c` still reads and writes it that way, and
 * every song written before today sings as it always did. */
static uint8_t voiced[(SONG_MAX_INSTR + 7) / 8];
static bool voiced_get(int i) { return (voiced[i >> 3] >> (i & 7)) & 1u; }
static void voiced_set(int i) { voiced[i >> 3] |= (uint8_t)(1u << (i & 7)); }

/* ----------------------------------------------------------- zones ---
 * The SAMPLE row edits ONE zone over the whole keyboard: what a one-shot
 * wants, and what every drum of SMPL-DEMO already is. An instrument that
 * came from a file with several (a multisampled piano) keeps every one of
 * them -- the row shows and edits the first and never touches the rest,
 * saying how many there are. Several zones on the machine is a screen of
 * its own. */
static Zone *zone_first(void)
{
    for (int z = 0; z < app_song.nzones; z++)
        if (app_song.zones[z].inst == inst)
            return &app_song.zones[z];
    return 0;
}

static int zone_count(void)
{
    int n = 0;
    for (int z = 0; z < app_song.nzones; z++)
        if (app_song.zones[z].inst == inst)
            n++;
    return n;
}

/* The zone the row edits, made over the whole keyboard if the instrument
 * has none. NULL when the song's list is full. Journals what it takes. */
static Zone *zone_make(void)
{
    Zone *z = zone_first();
    if (z)
        return z;
    if (app_song.nzones >= SONG_ZONE_MAX)
        return 0;
    undo_bytes(&app_song.nzones, sizeof app_song.nzones);
    z = &app_song.zones[app_song.nzones];
    undo_bytes(z, sizeof *z);
    app_song.nzones++;
    z->inst = (uint8_t)inst;
    /* NOTE_MIN..NOTE_MAX, which is what a file writes as C-0 G-9: a zone
     * made here and one read back from a save are the same zone. */
    z->lo = NOTE_MIN, z->hi = NOTE_MAX, z->root = 60, z->smp = 0;
    z->tune = 0, z->gain = 0;
    return z;
}

/* The song's sample table entry naming bank entry `b`, made if the song
 * does not name it yet; -1 when the table is full. The bare name is what
 * is written: bank_find reads the last part without .wav, so it resolves
 * here and in a saved file alike. */
static int smp_slot(int b)
{
    char nm[BANK_NAME_LEN];
    uint32_t frames;
    int free_at = -1;

    if (!bank_entry(b, nm, &frames))
        return -1;
    for (int i = 0; i < SONG_SMP_MAX; i++) {
        if (!app_song.smp[i].name[0]) {
            if (free_at < 0)
                free_at = i;
        } else if (bank_find(app_song.smp[i].name) == b) {
            return i;
        }
    }
    if (free_at < 0)
        return -1;
    undo_bytes(&app_song.smp[free_at], sizeof app_song.smp[free_at]);
    for (int k = 0; k < SONG_SMP_NAME; k++)
        app_song.smp[free_at].name[k] = k < BANK_NAME_LEN ? nm[k] : 0;
    app_song.smp[free_at].name[SONG_SMP_NAME - 1] = 0;
    app_song.smp[free_at].begin = app_song.smp[free_at].loop_start = app_song.smp[free_at].loop_end = 0;
    app_song.smp[free_at].loop = 0;
    return free_at;
}

/* Which bank entry the row sits on, -1 when the zone names none. */
static int zone_bank_at(void)
{
    const Zone *z = zone_first();
    return z && z->smp < SONG_SMP_MAX ? bank_find(app_song.smp[z->smp].name) : -1;
}

/* Whether this instrument would make no sound, for the colour its engine
 * is drawn in. PLUCK never does. SMPL does since 29/10, but only once a
 * zone sends a note to a sample: fresh off the knob it has none, and the
 * dim name says so rather than lying either way. */
static bool inst_mute(const InstrumentDef *in)
{
    if (in->engine == ENGINE_SAMPLE) { /* a zone is not enough: the bank must have what it names */
        for (int z = 0; z < app_song.nzones; z++) {
            const Zone *zz = &app_song.zones[z];
            if (zz->inst == inst && zz->smp < SONG_SMP_MAX && bank_find(app_song.smp[zz->smp].name) >= 0)
                return false;
        }
        return true;
    }
    return !engine_sounds(in->engine) && in->engine != ENGINE_VOX && in->engine != ENGINE_DRUM;
}

/* ------------------------------------------------------------- rows --- */
/* The engine's own row: VOICE for a voice, DRUM for a kit, none else. */
static int own_row(void)
{
    return I()->engine == ENGINE_VOX    ? L_VOICE
           : I()->engine == ENGINE_DRUM  ? L_DRUM
           : I()->engine == ENGINE_SAMPLE ? L_SAMPLE
                                          : -1;
}

/* The rows as drawn: the fixed ones, the engine's own, a row a layer,
 * and + LAY while there is room for one more. */
static int nrows(void)
{
    return L_VOICE + (own_row() >= 0) + I()->nlayers + (I()->nlayers < INSTR_MAX_LAYERS);
}

static int kind(int r)
{
    if (r < L_VOICE)
        return r;
    if (own_row() >= 0 && r == L_VOICE)
        return own_row();
    return L_LAYER;
}

/* The layer row r stands for, nlayers for + LAY. */
static int layer_of(int r) { return r - L_VOICE - (own_row() >= 0); }

/* How many columns the row really has. A cell that can hold nothing is
 * not drawn at all, nor its head: his, from the panel (30/10) -- a "--"
 * under a head is for a value one could set and has not, not for one
 * this engine does not have. SMPL has no timbre; the zone row has
 * nothing to set until the instrument has exactly one zone. */
static int ncols(int r)
{
    static const int n[] = { 0, 2, 4, 4, 3, 2, 4, 4 };
    int k = kind(r);
    if (k == L_LAYER)
        return layer_of(r) < I()->nlayers ? 4 : 1;
    if (k == L_SOUND && engine_timbre_max(I()->engine) == 0)
        return 1;
    if (k == L_SAMPLE && zone_count() != 1)
        return 1;
    return n[k];
}

static int span(int r, int c)
{
    int k = kind(r);
    /* the columns that can hold a word take two cells: the engine, the
     * sample's name, a layer's wave, and FILTER's cutoff -- "OPEN" is 37 px
     * and a cell is 33, so it used to spill over its neighbour (30/10). */
    return (k == L_SOUND) || ((k == L_LAYER || k == L_SAMPLE || k == L_FILTER) && c == 0) ? 2 : 1;
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
    if (engine == ENGINE_DRUM)
        return drum_kit_name(timbre);
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

/* The SAMPLE cell: the bank name the zone plays, trimmed to the cell it
 * is drawn in; "N ZONES" when the instrument has several, which this row
 * leaves alone; "--" when it has none. A name the bank has not got is
 * shown all the same -- it is what the song asks for, and the engine's
 * own cell is already saying that nothing sounds. */
static const char *zone_sample_word(void)
{
    static char w[BANK_NAME_LEN + 8];
    const Zone *z = zone_first();
    const char *nm, *key;
    int n = zone_count(), len = 0;

    if (n > 1) {
        char *q = ui_dec(w, n);
        while (*q)
            q++;
        q[0] = ' ', q[1] = 'Z', q[2] = 'O', q[3] = 'N', q[4] = 'E', q[5] = 'S', q[6] = 0;
        return w;
    }
    if (!z || z->smp >= SONG_SMP_MAX || !app_song.smp[z->smp].name[0])
        return "--";
    nm = key = app_song.smp[z->smp].name;
    for (const char *q = nm; *q; q++)
        if (*q == '/' || *q == '\\')
            key = q + 1;
    while (key[len] && len < (int)sizeof w - 1)
        w[len] = key[len], len++;
    w[len] = 0;
    if (len > 4 && w[len - 4] == '.')
        w[len -= 4] = 0;
    while (len > 1 && gfx_text_w(F_COND20, w) > 2 * CELL_W - 4)
        w[--len] = 0;
    return len ? w : "--";
}

/* This screen draws its cells and their heads flush left, three pixels
 * in, so that every column and the instrument's name agree down the left
 * edge -- his, from the panel (30/10): centred values of different widths
 * read as ragged. `grid_cell` centres and MIXER and PHRASE want that, so
 * it is left alone and this screen has its own pair. */
enum { CELL_PAD = 3 };

static void cell(int v, int x, int w, const char *s, bool band, bool on, uint16_t ink)
{
    if (on)
        grid_hollow(v, x, w);
    gfx_text(F_COND20, x + CELL_PAD, grid_base(v), s, grid_ink(band, on, ink));
}

static void head(int x, const char *s, bool on)
{
    gfx_text(F_COND12, x + CELL_PAD, GRID_HEAD - 3, s, on ? C_TEXT : C_DIM);
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
    case L_DRUM:
        return ui_dec(b, in->drum[c] + 64);
    case L_SAMPLE: {
        const Zone *z = zone_count() == 1 ? zone_first() : 0;
        if (c == 0)
            return zone_sample_word();
        if (!z)
            return "--";
        if (c == 1)
            return note_name(z->root, b);
        return sdec(b, c == 2 ? z->tune : z->gain);
    }
    default: {
        const Layer *l = &in->layers[layer_of(r)];
        if (c == 0)
            return timbre_word(l->engine, l->timbre);
        return sdec(b, c == 1 ? l->transpose : c == 2 ? l->detune : l->gain);
    }
    }
}

/* ------------------------------------------------------------ edits --- */
/* The engines in KNOB 3's order. SMPL joined them on 30/10: it had been
 * left out while it was silent, and an instrument already on it was
 * thrown to the first of the list at a turn -- a song's sampled
 * instrument lost to one cran of the knob. PLUCK is still out: the FM-1
 * does not have it, and a turn away from it does not come back. */
static const uint8_t engines[] = { ENGINE_PULSE, ENGINE_NOISE, ENGINE_WAVE, ENGINE_SAMPLE,
                                   ENGINE_VOX, ENGINE_DRUM };

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
        if (in->engine == ENGINE_VOX) { /* made a voice here: English, unless it had an accent already */
            if (!voiced_get(inst))
                in->accent = VOX_ACCENT_EN;
            voiced_set(inst);
        }
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
    } else if (k == L_DRUM) {
        in->drum[col] = (int8_t)clampi(in->drum[col] + d, -64, 63);
    } else if (k == L_SAMPLE) {
        Zone *z = zone_first();
        if (zone_count() > 1) { /* several zones: this row does not touch them */
            said = "MANY ZONES: ON THE COMPUTER";
            return;
        }
        if (col == 0) {
            int n = bank_count(), at, slot;
            if (n <= 0) /* nothing in the bank to choose from */
                return;
            at = zone_bank_at();
            at = clampi(at < 0 ? 0 : at + d, 0, n - 1);
            slot = smp_slot(at);
            if (slot < 0 || !(z = zone_make()))
                return;
            undo_bytes(z, sizeof *z);
            z->smp = (uint8_t)slot;
            bank_resolve(&app_song); /* the table changed: what each entry plays */
        } else if (z) {
            undo_bytes(z, sizeof *z);
            if (col == 1)
                z->root = (uint8_t)clampi(z->root + d, NOTE_MIN, NOTE_MAX);
            else if (col == 2)
                z->tune = (int8_t)clampi(z->tune + d, -64, 63);
            else
                z->gain = (int8_t)clampi(z->gain + d, GAIN_MIN_DB, GAIN_MAX_DB);
        }
    } else {
        int j = layer_of(row);
        if (j >= in->nlayers) { /* + LAY: turning up makes the layer */
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

/* The preview: C E G C an octave on, a note every 250 ms, a rest, again.
 * A kit plays its drums instead: kick, closed hat, snare, open hat, clap,
 * two toms, a rest, a 16th at 120 bpm each. */
static void preview(uint32_t ms)
{
    static const uint8_t tune[] = { 60, 64, 67, 72, 0, 0 };
    static const uint8_t kit[] = { 36, 42, 38, 46, 36, 39, 48, 45, 0, 0 };
    bool drum = I()->engine == ENGINE_DRUM;
    int at = drum ? (int)((ms - preview_t0) / 125u % sizeof kit) : (int)((ms - preview_t0) / 250u % sizeof tune);
    uint8_t n = drum ? kit[at] : tune[at];
    if (at == preview_at)
        return;
    preview_at = at;
    if (n)
        player_note_on(inst, (uint8_t)(n + (drum ? 0 : 12 * kbd_oct)));
    else
        player_note_off();
}

/* ---------------------------------------------------------- drawing --- */
static void draw(void)
{
    static const char *const heads[][4] = {
        { 0 }, { "ENGINE", "TIMBRE" }, { "ATK", "DEC", "SUS", "REL" }, { "CUT", "RES", "ENV", "VEL" },
        { "GAIN", "LEVEL", "VIB" }, { "TRACT", "ACCENT" }, { "TUNE", "TONE", "DECAY", "SNAP" },
        { "SAMPLE", "ROOT", "TUNE", "GAIN" }, { "SOUND", "SEMI", "FINE", "GAIN" },
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
    /* the column names: this row's -- but a word in the corner is drawn on
     * the same line and runs into them, so it has the line while it lasts */
    if (!said && (layer_of(row) < in->nlayers || kind(row) != L_LAYER))
        for (int c = 0; c < ncols(row); c++)
            head(col_x(row, c), heads[kind(row)][c], c == col);
    for (int v = 0; v < GRID_ROWS && top + v < n; v++) {
        int r = top + v, k = kind(r);
        bool band = r == row, adder = k == L_LAYER && layer_of(r) >= in->nlayers;
        if (band)
            grid_band(v, PLAT_W - 4 - (n > GRID_ROWS ? 6 : 0));
        if (k == L_NAME && previewing) /* what plays */
            for (int i = 0; i < 6; i++)
                gfx_fill(PLAT_W - 16 + i, grid_y(v) + 8 + i, 1, 12 - 2 * i, band ? C_ON_ACC : C_TITLE, 255);
        {
            static const char *const label[] = { "NAME", "SOUND", "ENV", "FILTER", "LEVEL", "VOICE",
                                                 "DRUM", "ZONE" };
            /* LAY, not LYR: he read the old one as "lyrics", which on a
             * machine that does have a singing voice with syllables is a
             * fair reading (30/10). LAY is the file's own keyword, so the
             * screen, the format and moondaw say the same word, and it is
             * 41 px against the 53 the label column has -- "+ LAYER" is 59
             * and does not fit. */
            char lyr[6] = "LAY 1";
            lyr[4] = (char)('1' + layer_of(r));
            gfx_text(F_COND20, LABEL_X, grid_base(v), k != L_LAYER ? label[k] : adder ? "+ LAY" : lyr,
                     band ? C_ON_ACC : C_DIM);
        }
        if (k == L_NAME) {
            if (band)
                grid_hollow(v, CELL_X - 4, 130);
            gfx_text(F_COND20, CELL_X + CELL_PAD, grid_base(v), in->name[0] ? in->name : "--",
                     band ? C_ACCENT : C_TEXT);
            continue;
        }
        if (adder)
            continue;
        for (int c = 0; c < ncols(r); c++) {
            bool on = band && c == col;
            /* the engine of an instrument that makes no sound, and the zone
             * cell of one this row will not touch: neither is an edit */
            uint16_t ink = k == L_SOUND && c == 0 && inst_mute(in)  ? C_FX
                           : k == L_SAMPLE && c == 0 && zone_count() > 1 ? C_DIM
                                                                         : C_TEXT;
            cell(v, col_x(r, c), span(r, c) * CELL_W, value(r, c, s), band, on, ink);
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
    /* the song's own voices already have their accent; see `voiced` above */
    for (uint32_t i = 0; i < sizeof voiced; i++)
        voiced[i] = 0;
    for (int i = 0; i < SONG_MAX_INSTR; i++)
        if (app_song.instruments[i].engine == ENGINE_VOX)
            voiced_set(i);
    row = col = top = 0;
    naming = previewing = false;
    said = 0;
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

void instrument_resume(void)
{
    prev_btns = prev_keys = 0xFFFFFFFFu;
    gfx_invalidate();
}

int instrument_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    if (btns || keys || in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1] || in->enc[PE_K3] ||
        in->enc[PE_PRESET]) /* PRESETS too: a word about this instrument is not about the next */
        said = 0;

    if (naming) {
        naming = namer_frame(in, btns) == NAMER_TYPING;
        if (!naming)
            app_mark_dirty();
        ui_led_guide(0); /* the namer: SEL keeps, GLO goes back */
        gfx_frame(draw);
        return INSTRUMENT_STAY;
    }

    if (in->enc[PE_PRESET]) {
        inst = clampi(inst + in->enc[PE_PRESET], 0, SONG_MAX_INSTR - 1);
        row = row >= nrows() ? nrows() - 1 : row;
    }
    row = clampi(row + in->enc[PE_SELECT] + in->enc[PE_K1], 0, nrows() - 1);
    col = clampi(col + in->enc[PE_K2], 0, ncols(row) ? ncols(row) - 1 : 0);
    if (in->enc[PE_K3] && kind(row) != L_NAME) {
        turn(in->enc[PE_K3]);
        row = row >= nrows() ? nrows() - 1 : row; /* a layer gone, or VOICE or DRUM */
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
    for (int k = 0; k < PLAT_KEYS; k++) { /* edit_key_note's own: a kit's keys are its drums */
        int n = edit_key_note(&app_song, inst, kbd_oct, k);
        if (((keys >> k) & 1u) && n >= NOTE_MIN && n <= NOTE_MAX) {
            previewing = false;
            player_note_on(inst, (uint8_t)n);
        }
    }
    if (!in->keys && prev_keys && prev_keys != 0xFFFFFFFFu && !previewing)
        player_note_off();
    prev_keys = in->keys;
    /* And a MIDI keyboard, on the sound in front: it stops the preview as a
     * key does, and its pitch is its own -- on a kit, the note picks the
     * drum off the General MIDI map, as the keys do (ui.h) */
    if (in->midi_n)
        previewing = false;
    ui_midi_play(in, inst);

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
    ui_led_guide(1u << PB_GLO | 1u << PB_OCTDN | 1u << PB_OCTUP); /* the PROJECT, the keys' octave */
    gfx_frame(draw);
    if (PRESSED(PB_HOME) || PRESSED(PB_GLO)) {
        previewing = false;
        player_note_off();
        return PRESSED(PB_HOME) ? INSTRUMENT_BACK : INSTRUMENT_PROJECT;
    }
    return INSTRUMENT_STAY;
#undef PRESSED
}
