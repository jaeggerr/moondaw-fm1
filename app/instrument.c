/* SPDX-License-Identifier: GPL-3.0-only */
#include "instrument.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "ui.h"

enum {
    NAV_Y = 2, NAV_H = 17,
    LEFT_Y = 49,
    PLOT_Y = 60, PLOT_H = 34,
    ROWS_Y = 97, ROW_H = 12, ROW_PITCH = 13,
    LAYERS_Y = 150, LAYER_ROWS_Y = 160,
    CELL_W = 57, CELL_PITCH = 59,
};
enum { ROW_SOUND, ROW_ENV, ROW_FILTER, ROW_VIB, ROW_LAYER, ROWS_ALL = ROW_LAYER + INSTR_MAX_LAYERS };
enum { AT_BACK = 6, AT_SAVE = 7, AT_PLAY = 10 };

static int inst, entry, row, kbd_oct;
static uint32_t prev_btns, prev_keys, held;
static bool playing;
static int8_t saved;

static InstrumentDef *I(void) { return &app_song.instruments[inst]; }

/* ------------------------------------------------------------ words --- */
static const char *timbre_word(uint8_t engine, uint8_t timbre)
{
    static const char *const duty[4] = { "12.5%", "25%", "50%", "75%" };
    static const char *const len[2] = { "LONG", "SHORT" };
    static const char *const shape[5] = { "SAW", "TRI", "SINE", "HALF", "ABS" };
    if (engine == ENGINE_PULSE)
        return duty[timbre & 3];
    if (engine == ENGINE_NOISE)
        return len[timbre & 1];
    if (engine == ENGINE_WAVE)
        return shape[timbre < 5 ? timbre : 0];
    return "--";
}

static const char *timbre_label(uint8_t engine)
{
    return engine == ENGINE_NOISE ? "LENGTH" : engine == ENGINE_WAVE ? "SHAPE" : "DUTY";
}

static char *sdec(char *b, int v, bool plus)
{
    int i = 0;
    if (v < 0) {
        b[i++] = '-';
        v = -v;
    } else if (plus && v > 0) {
        b[i++] = '+';
    }
    ui_dec(b + i, v);
    return b;
}

/* A layer's engine and timbre are one knob: 0 is no layer, then the four
 * duties, the two noise lengths, the five shapes. */
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

static char *wave_word(char *b, const Layer *l)
{
    const char *e = engine_name(l->engine), *t = timbre_word(l->engine, l->timbre);
    int i = 0;
    while (*e)
        b[i++] = *e++;
    b[i++] = ' ';
    while (*t)
        b[i++] = *t++;
    b[i] = '\0';
    return b;
}

/* ------------------------------------------------------------ edits --- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void turn(int k, int d)
{
    InstrumentDef *in = I();

    if (row == ROW_SOUND) {
        if (k == 0) {
            /* The three engines this machine has. One that is silent here
             * (SMPL, VOX, PLUCK) leaves for the first of them at a turn. */
            int e = engine_sounds(in->engine) ? clampi(in->engine + d, 0, ENGINE_WAVE) : ENGINE_PULSE;
            in->engine = (uint8_t)e;
            if (in->timbre > engine_timbre_max(in->engine))
                in->timbre = engine_timbre_max(in->engine);
        } else if (k == 1) {
            in->timbre = (uint8_t)clampi(in->timbre + d, 0, engine_timbre_max(in->engine));
        } else if (k == 2) {
            in->gain = (int8_t)clampi(in->gain + d, GAIN_MIN_DB, GAIN_MAX_DB);
        } else {
            in->level = (int8_t)clampi(in->level + d, GAIN_MIN_DB, GAIN_MAX_DB);
        }
    } else if (row == ROW_ENV) {
        uint8_t *f[4] = { &in->attack, &in->decay, &in->sustain, &in->release };
        *f[k] = (uint8_t)clampi(*f[k] + d, 0, 255);
    } else if (row == ROW_FILTER) {
        uint8_t *f[4] = { &in->cutoff, &in->reso, &in->filt_env, &in->filt_vel };
        *f[k] = (uint8_t)clampi(*f[k] + d, 0, k < 2 ? 255 : 127);
    } else if (row == ROW_VIB) {
        if (k != 0)
            return;
        in->vib_phase = (int16_t)clampi(in->vib_phase + d, VIB_PHASE_FREE, 255);
    } else {
        int j = row - ROW_LAYER;
        if (j >= in->nlayers) {
            /* A free row: turning KNOB 1 up makes the layer. */
            if (k != 0 || d <= 0 || in->nlayers >= INSTR_MAX_LAYERS)
                return;
            j = in->nlayers++;
            in->layers[j].transpose = in->layers[j].detune = in->layers[j].gain = 0;
            wave_set(&in->layers[j], clampi(d, 1, WAVES - 1));
        } else {
            Layer *l = &in->layers[j];
            if (k == 0) {
                int w = clampi(wave_of(l) + d, 0, WAVES - 1);
                if (w == 0) { /* all the way down: the layer goes, the ones under it move up */
                    for (int i = j; i < in->nlayers - 1; i++)
                        in->layers[i] = in->layers[i + 1];
                    in->nlayers--;
                } else {
                    wave_set(l, w);
                }
            } else if (k == 1) {
                l->transpose = (int8_t)clampi(l->transpose + d, -48, 48);
            } else if (k == 2) {
                l->detune = (int8_t)clampi(l->detune + d, -100, 100);
            } else {
                l->gain = (int8_t)clampi(l->gain + d, GAIN_MIN_DB, GAIN_MAX_DB);
            }
        }
    }
    app_mark_dirty();
}

/* ---------------------------------------------------------- drawing --- */
static void cell(int k, int y, bool cur, const char *label, const char *value, bool warn)
{
    int x = APP_X0 + k * CELL_PITCH;
    if (!value) {
        if (label) /* a free layer */
            gfx_frame_rect(x, y, CELL_W, ROW_H, C_DIM, 76);
        return;
    }
    gfx_slab(x, y, CELL_W, ROW_H, cur ? C_ACCENT : C_TEXT, cur ? 255 : 18);
    gfx_text(F_MONO8, x + 3, y + 9, label, cur ? C_ON_ACC : C_DIM);
    gfx_text(F_MONO8, x + CELL_W - 3 - gfx_text_w(F_MONO8, value), y + 9, value,
             cur ? C_ON_ACC : warn ? C_FX : C_TEXT);
}

static int ms_of(int v) { return v * v / 8; }

static void draw_cards(void)
{
    const InstrumentDef *in = I();
    char a[16], b[12];

    if (row == ROW_SOUND) {
        ui_card(0, "ENGINE", engine_name(in->engine), 0);
        if (engine_sounds(in->engine))
            ui_card(1, timbre_label(in->engine), timbre_word(in->engine, in->timbre), 0);
        else
            ui_card(1, 0, 0, 0);
        ui_card(2, "GAIN", sdec(a, in->gain, true), "dB");
        ui_card(3, "LEVEL", sdec(b, in->level, true), "dB");
    } else if (row == ROW_ENV) {
        ui_card(0, "ATTACK", ui_dec(a, in->attack), 0);
        ui_card(1, "DECAY", ui_dec(a, in->decay), 0);
        ui_card(2, "SUSTAIN", ui_dec(a, in->sustain), 0);
        ui_card(3, "RELEASE", ui_dec(a, in->release), 0);
    } else if (row == ROW_FILTER) {
        ui_card(0, "CUTOFF", in->cutoff == FILTER_OPEN ? "OPEN" : ui_dec(a, in->cutoff), 0);
        ui_card(1, "RESO", ui_dec(a, in->reso), 0);
        ui_card(2, "ENV", sdec(a, in->filt_env - FILTER_ENV_NONE, true), 0);
        ui_card(3, "VEL", sdec(a, in->filt_vel - FILTER_ENV_NONE, true), 0);
    } else if (row == ROW_VIB) {
        ui_card(0, "VIBPHASE", in->vib_phase == VIB_PHASE_FREE ? "FREE" : ui_dec(a, in->vib_phase), 0);
        ui_card(1, 0, 0, 0);
        ui_card(2, 0, 0, 0);
        ui_card(3, 0, 0, 0);
    } else if (row - ROW_LAYER < in->nlayers) {
        const Layer *l = &in->layers[row - ROW_LAYER];
        ui_card(0, "WAVE", wave_word(a, l), 0);
        ui_card(1, "TRANSP", sdec(b, l->transpose, true), "st");
        ui_card(2, "DETUNE", sdec(b, l->detune, true), "ct");
        ui_card(3, "GAIN", sdec(b, l->gain, true), "dB");
    } else {
        ui_card(0, "WAVE", "--", 0);
        ui_card(1, 0, 0, 0);
        ui_card(2, 0, 0, 0);
        ui_card(3, 0, 0, 0);
    }
}

static void draw(void)
{
    static const char *const words[12] = { "", "", "", "", "", "", "\xE2\x80\xB9 PAT", "SAVE", "", "", "PLAY", "" };
    static const char *const playing_words[12] = { "", "", "", "", "", "", "\xE2\x80\xB9 PAT", "SAVE", "", "", "STOP", "" };
    const InstrumentDef *in = I();
    uint8_t st[12];
    char a[16], b[12];
    int x;
    bool silent = !engine_sounds(in->engine);

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    x = gfx_text(F_COND12, APP_X0, NAV_Y + 13, "\xE2\x80\xB9 PAT", C_TITLE);
    x = gfx_text(F_MONO8, x + 6, NAV_Y + 12, ui_hex2(a, inst), C_TITLE);
    gfx_text(F_COND12, x + 4, NAV_Y + 13, in->name[0] ? in->name : "--", C_TEXT);
    if (saved) {
        const char *m = saved > 0 ? "SAVED" : "NOT SAVED";
        gfx_text(F_COND9, APP_X0 + APP_INNER - gfx_text_w(F_COND9, m), NAV_Y + 12, m,
                 saved > 0 ? C_ACCENT : C_MUTE);
    } else if (silent) {
        gfx_text(F_MONO8, APP_X0 + APP_INNER - gfx_text_w(F_MONO8, "SILENT HERE"), NAV_Y + 12,
                 "SILENT HERE", C_FX);
    }
    gfx_fill(APP_X0, NAV_Y + NAV_H, APP_INNER, 1, C_DIM, 77);

    draw_cards();
    ui_cards_rule();

    x = gfx_text(F_MONO8, APP_X0, LEFT_Y + 8, "SELECT", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "ROW", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "PRESETS", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "INSTR", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "KEYS", C_DIM);
    gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "PLAY", C_TEXT);

    {   /* The envelope: its times to scale, the sustain on a fixed width. */
        int ta = ms_of(in->attack), td = ms_of(in->decay), tr = ms_of(in->release), hold = 300;
        int total = ta + td + hold + tr, w = APP_INNER - 8, h = PLOT_H - 9;
        int x0 = APP_X0 + 4, yb = PLOT_Y + PLOT_H - 4, ys = yb - h * in->sustain / 255;
        int xa = x0 + w * ta / total, xd = x0 + w * (ta + td) / total,
            xh = x0 + w * (ta + td + hold) / total;

        gfx_slab(APP_X0, PLOT_Y, APP_INNER, PLOT_H, C_TEXT, 9);
        gfx_line(x0, yb, xa, yb - h, C_ACCENT);
        gfx_line(xa, yb - h, xd, ys, C_ACCENT);
        gfx_line(xd, ys, xh, ys, C_ACCENT);
        gfx_line(xh, ys, x0 + w, yb, C_ACCENT);
        ui_dec(a, total - hold);
        x = APP_X0 + APP_INNER - 4 - gfx_text_w(F_MONO8, " ms");
        gfx_text(F_MONO8, x, PLOT_Y + 9, " ms", C_DIM);
        gfx_text(F_MONO8, x - gfx_text_w(F_MONO8, a), PLOT_Y + 9, a, C_DIM);
    }

    {
        int y = ROWS_Y;
        bool c = row == ROW_SOUND;
        cell(0, y, c, "", engine_name(in->engine), silent);
        /* LEN, not LENGTH: the word and SHORT do not both fit a cell; the card says it whole */
        cell(1, y, c, in->engine == ENGINE_NOISE ? "LEN" : timbre_label(in->engine),
             silent ? "--" : timbre_word(in->engine, in->timbre), false);
        cell(2, y, c, "GAIN", sdec(a, in->gain, false), false);
        cell(3, y, c, "LEVEL", sdec(b, in->level, false), false);
        y += ROW_PITCH;
        c = row == ROW_ENV;
        cell(0, y, c, "ATK", ui_dec(a, in->attack), false);
        cell(1, y, c, "DEC", ui_dec(a, in->decay), false);
        cell(2, y, c, "SUS", ui_dec(a, in->sustain), false);
        cell(3, y, c, "REL", ui_dec(a, in->release), false);
        y += ROW_PITCH;
        c = row == ROW_FILTER;
        cell(0, y, c, "CUT", ui_dec(a, in->cutoff), false);
        cell(1, y, c, "RES", ui_dec(a, in->reso), false);
        cell(2, y, c, "ENV", sdec(a, in->filt_env - FILTER_ENV_NONE, true), false);
        cell(3, y, c, "VEL", sdec(a, in->filt_vel - FILTER_ENV_NONE, true), false);
        y += ROW_PITCH;
        cell(0, y, row == ROW_VIB, "VIB", in->vib_phase == VIB_PHASE_FREE ? "FREE" : ui_dec(a, in->vib_phase), false);
    }
    gfx_text(F_MONO8, APP_X0 + 2, LAYERS_Y + 8, "LAYERS", C_DIM);
    for (int j = 0; j < INSTR_MAX_LAYERS; j++) {
        int y = LAYER_ROWS_Y + j * ROW_PITCH;
        bool c = row == ROW_LAYER + j;
        if (j < in->nlayers) {
            const Layer *l = &in->layers[j];
            cell(0, y, c, "", wave_word(a, l), false);
            cell(1, y, c, "TSP", sdec(b, l->transpose, true), false);
            cell(2, y, c, "DET", sdec(b, l->detune, true), false);
            cell(3, y, c, "GAIN", sdec(b, l->gain, true), false);
        } else {
            for (int k = 0; k < 4; k++)
                cell(k, y, false, "", 0, false);
            if (c) /* the cursor on a free row: its first cell, the one KNOB 1 fills */
                gfx_slab(APP_X0, y, CELL_W, ROW_H, C_ACCENT, 255);
        }
    }

    for (int k = 0; k < 12; k++)
        st[k] = k == AT_BACK ? UI_BTN_BACK : k == AT_SAVE ? UI_BTN_ON
                : k == AT_PLAY ? (playing ? UI_BTN_LIVE : UI_BTN_ON) : UI_BTN_NONE;
    ui_btn_map(playing ? playing_words : words, st, held);
}

/* -------------------------------------------------------------- api --- */
void instrument_open(int instr, int e)
{
    inst = instr < 0 || instr >= SONG_MAX_INSTR ? 0 : instr;
    entry = e;
    row = ROW_ENV;
    saved = 0;
    prev_btns = prev_keys = 0xFFFFFFFFu;
    gfx_invalidate();
}

bool instrument_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
    int last;
#define PRESSED(at) ((btns >> APP_BTN_AT[at]) & 1u)

    prev_btns = in->btns;
    held = in->btns;
    playing = player_playing();
    if (btns || keys || in->enc[PE_SELECT] || in->enc[PE_PRESET])
        saved = 0;

    inst += in->enc[PE_PRESET];
    inst = clampi(inst, 0, SONG_MAX_INSTR - 1);
    /* down to the first free layer row and no further */
    last = ROW_LAYER + (I()->nlayers < INSTR_MAX_LAYERS ? I()->nlayers : INSTR_MAX_LAYERS - 1);
    row = clampi(row + in->enc[PE_SELECT], 0, last);
    for (int k = 0; k < 4; k++)
        if (in->enc[PE_K1 + k]) {
            turn(k, in->enc[PE_K1 + k]);
            saved = 0;
        }
    last = ROW_LAYER + (I()->nlayers < INSTR_MAX_LAYERS ? I()->nlayers : INSTR_MAX_LAYERS - 1);
    if (row > last)
        row = last;

    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;
    /* The keys play the sound: the last one pressed, until all are up. */
    for (int k = 0; k < PLAT_KEYS; k++) {
        int n = 53 + kbd_oct * 12 + k;
        if (((keys >> k) & 1u) && n >= NOTE_MIN && n <= NOTE_MAX)
            player_note_on(inst, (uint8_t)n);
    }
    if (!in->keys && prev_keys && prev_keys != 0xFFFFFFFFu)
        player_note_off();
    prev_keys = in->keys;

    if (PRESSED(AT_SAVE))
        saved = app_save() == 0 ? 1 : -1;
    if (PRESSED(AT_PLAY)) {
        if (playing)
            player_stop();
        else
            player_play_entry(entry);
    }
    plat_led_btn(PB_PLAY, playing);
    gfx_frame(draw);
    if (PRESSED(AT_BACK)) {
        player_note_off();
        return false;
    }
    return true;
#undef PRESSED
}
