/* SPDX-License-Identifier: GPL-3.0-only */
/* Written against FORMAT.md and moondaw's songfile.c, whose order of
 * tests this follows line for line where the two could disagree. No
 * sscanf here: the few shapes a line can take are read by hand. */
#include "trk.h"

#define LINE_MAX_LEN 512

/* ---------------------------------------------------------- reading --- */
static bool is_ws(char c) { return c == ' ' || c == '\t'; }

static const char *skip_ws(const char *p)
{
    while (is_ws(*p))
        p++;
    return p;
}

static bool starts(const char *t, const char *kw)
{
    while (*kw)
        if (*t++ != *kw++)
            return false;
    return true;
}

static const char *find(const char *t, const char *kw)
{
    for (; *t; t++)
        if (starts(t, kw))
            return t;
    return 0;
}

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int hex2(const char *s)
{
    int hi = hexval(s[0]), lo = hi < 0 ? -1 : hexval(s[1]);
    return lo < 0 ? -1 : hi * 16 + lo;
}

/* Both leave *p after the number and return false without moving it
 * when there is none -- what a %d and a %x that failed do. */
static bool rd_int(const char **p, int *v)
{
    const char *q = skip_ws(*p);
    bool neg = *q == '-';
    int32_t n = 0;
    if (*q == '-' || *q == '+')
        q++;
    if (*q < '0' || *q > '9')
        return false;
    while (*q >= '0' && *q <= '9') {
        if (n < 100000000)
            n = n * 10 + (*q - '0');
        q++;
    }
    *v = neg ? -n : n;
    *p = q;
    return true;
}

static bool rd_hex(const char **p, int *v)
{
    const char *q = skip_ws(*p);
    int32_t n = 0;
    if (hexval(*q) < 0)
        return false;
    while (hexval(*q) >= 0) {
        if (n < 0x1000000)
            n = n * 16 + hexval(*q);
        q++;
    }
    *v = n;
    *p = q;
    return true;
}

/* "<kw><int>" anywhere in the line. */
static bool field(const char *t, const char *kw, int *v)
{
    const char *p = find(t, kw);
    if (!p)
        return false;
    while (*kw++)
        p++;
    return rd_int(&p, v);
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void copy_name(char *dst, const char *src, int cap)
{
    int i = 0;
    for (; src[i] && i < cap - 1; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* Cuts the comment and the blanks at both ends. A '#' opens a comment
 * at the start of the line or after a blank; inside a token it is an
 * accidental. */
static char *clean_line(char *line)
{
    for (char *c = line; *c; c++)
        if (*c == '#' && (c == line || is_ws(c[-1]))) {
            *c = '\0';
            break;
        }
    char *t = line;
    while (is_ws(*t))
        t++;
    char *end = t;
    while (*end)
        end++;
    while (end > t && (is_ws(end[-1]) || end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return t;
}

static void parse_inst(const char *t, InstrumentDef *in, TrkReport *rep)
{
    const char *p;
    int v;

    in->engine = ENGINE_PULSE;
    in->cutoff = FILTER_OPEN;
    in->reso = 0;
    in->filt_env = FILTER_ENV_NONE;
    in->filt_vel = FILTER_ENV_NONE;
    in->gain = 0;
    in->vib_phase = 0;
    in->tract = 0;
    in->accent = VOX_ACCENT_FR;
    for (int k = 0; k < 4; k++)
        in->drum[k] = 0;
    in->nlayers = 0;

    if ((p = find(t, "engine ")) != 0) {
        int e = engine_from_name(p + 7);
        in->engine = (uint8_t)(e < 0 ? ENGINE_PULSE : e);
    }
    p = find(t, "timbre ");
    if (!p)
        p = find(t, "duty ");
    if (p) {
        while (*p && !is_ws(*p))
            p++;
        if (rd_int(&p, &v))
            in->timbre = (uint8_t)(v < 0 ? 0 : v);
    }
    if (in->timbre > engine_timbre_max(in->engine))
        in->timbre = engine_timbre_max(in->engine);
    if ((p = find(t, "adsr ")) != 0) {
        int e[4];
        p += 5;
        if (rd_int(&p, &e[0]) && rd_int(&p, &e[1]) && rd_int(&p, &e[2]) &&
            rd_int(&p, &e[3])) {
            in->attack = (uint8_t)e[0];
            in->decay = (uint8_t)e[1];
            in->sustain = (uint8_t)e[2];
            in->release = (uint8_t)e[3];
        }
    }
    if ((p = find(t, "filter ")) != 0) {
        int f[4], n = 0;
        p += 7;
        while (n < 4 && rd_int(&p, &f[n]))
            n++;
        if (n >= 3) {
            in->cutoff = (uint8_t)f[0];
            in->reso = (uint8_t)f[1];
            in->filt_env = (uint8_t)(f[2] > 127 ? 127 : f[2] & 0x7F);
            if (n >= 4)
                in->filt_vel = (uint8_t)(f[3] > 127 ? 127 : f[3] & 0x7F);
        }
    }
    if (field(t, "gain ", &v))
        in->gain = (int8_t)clampi(v, GAIN_MIN_DB, GAIN_MAX_DB);
    if (field(t, "level ", &v))
        in->level = (int8_t)clampi(v, -64, 24);
    if ((p = find(t, "vibphase ")) != 0) {
        if (starts(p + 9, "free"))
            in->vib_phase = VIB_PHASE_FREE;
        else if (field(p, "vibphase ", &v))
            in->vib_phase = (int16_t)clampi(v, 0, 255);
    }
    if (field(t, "tract ", &v))
        in->tract = (uint8_t)clampi(v, VOX_TRACT_MIN, VOX_TRACT_MAX);
    if ((p = find(t, "accent ")) != 0)
        in->accent = starts(p + 7, "EN") ? VOX_ACCENT_EN : VOX_ACCENT_FR;
    if ((p = find(t, "drum ")) != 0) {
        /* TUNE TONE DECAY SNAP, 0..127, 64 as designed */
        p += 5;
        for (int k = 0; k < 4 && rd_int(&p, &v); k++)
            in->drum[k] = (int8_t)(clampi(v, 0, 127) - 64);
    }
    if ((p = find(t, "name ")) != 0)
        copy_name(in->name, p + 5, INSTR_NAME_LEN);

    if (rep) {
        rep->silent_instr += in->engine == ENGINE_PLUCK;
        rep->shaping += find(t, "drive ") || find(t, "amp ") || find(t, "tone ");
    }
}

static bool parse_layer(const char *rest, Layer *l)
{
    const char *p = find(rest, "engine ");
    int v, e;
    if (!p)
        return false;
    e = engine_from_name(p + 7);
    if (e < 0 || e == ENGINE_SAMPLE)
        return false;
    l->engine = (uint8_t)e;
    l->timbre = 0;
    l->transpose = l->detune = l->gain = 0;
    if (field(rest, "timbre ", &v))
        l->timbre = (uint8_t)(v < 0 ? 0 : v);
    if (l->timbre > engine_timbre_max(l->engine))
        l->timbre = engine_timbre_max(l->engine);
    if (field(rest, "transpose ", &v))
        l->transpose = (int8_t)clampi(v, -48, 48);
    if (field(rest, "detune ", &v))
        l->detune = (int8_t)clampi(v, -100, 100);
    if (field(rest, "gain ", &v))
        l->gain = (int8_t)clampi(v, GAIN_MIN_DB, GAIN_MAX_DB);
    return true;
}

/* `smp NN name [begin N] [loop A B [ping]]`, past the index. Absent is
 * zero, as moondaw reads it: a slot taken again starts clean. */
static void parse_sample(const char *p, SongSample *e)
{
    const char *q;
    int i = 0, a, b;

    p = skip_ws(p);
    for (; p[i] && !is_ws(p[i]) && i < SONG_SMP_NAME - 1; i++)
        e->name[i] = p[i];
    e->name[i] = '\0';
    e->begin = e->loop_start = e->loop_end = 0;
    e->loop = SMP_LOOP_OFF;
    if ((q = find(p + i, "begin ")) != 0 && (q += 6, rd_int(&q, &a)) && a >= 0)
        e->begin = (uint32_t)a;
    if ((q = find(p + i, "loop ")) != 0 && (q += 5, rd_int(&q, &a)) && rd_int(&q, &b) &&
        a >= 0 && b > a) {
        e->loop_start = (uint32_t)a;
        e->loop_end = (uint32_t)b;
        e->loop = find(q, "ping") ? SMP_LOOP_PING : SMP_LOOP_FWD;
    }
}

/* `lo hi root R smp NN [tune T] [gain G]`, past the instrument. A zone
 * that does not read whole is dropped whole: half a keymap entry could
 * take notes it was never given. */
static bool parse_zone(const char *p, Zone *z)
{
    int lo, hi, rt, si, v;

    p = skip_ws(p);
    lo = note_from_name(p);
    p = skip_ws(p + 3);
    hi = note_from_name(p);
    p = skip_ws(p + 3);
    if (lo < 0 || lo == NOTE_OFF || hi < 0 || hi == NOTE_OFF || lo > hi || !starts(p, "root "))
        return false;
    p = skip_ws(p + 5);
    rt = note_from_name(p);
    p = skip_ws(p + 3);
    if (rt < 0 || rt == NOTE_OFF || !starts(p, "smp ") || (p += 4, !rd_hex(&p, &si)) || si >= SONG_SMP_MAX)
        return false;
    z->lo = (uint8_t)lo;
    z->hi = (uint8_t)hi;
    z->root = (uint8_t)rt;
    z->smp = (uint8_t)si;
    z->tune = z->gain = 0;
    if (field(p, "tune ", &v))
        z->tune = (int8_t)clampi(v, -ZONE_TUNE_MAX, ZONE_TUNE_MAX);
    if (field(p, "gain ", &v))
        z->gain = (int8_t)clampi(v, GAIN_MIN_DB, GAIN_MAX_DB);
    return true;
}

/* One step line into *st; returns its row, or -1. The row is the whole
 * truth of its line: what the line does not say is empty. */
static int parse_step(char *line, Step *st, Song *song, TrkReport *rep)
{
    char *tok = line, *end;
    int row, nfx = 0, len = 0;

    while (tok[len] && !is_ws(tok[len]))
        len++;
    row = len == 1 ? hexval(tok[0]) : hex2(tok);
    if (row < 0 || row >= PATTERN_MAX_STEPS)
        return -1;
    for (int i = 0; i < STEP_MAX_NOTES; i++)
        st->notes[i] = NOTE_NONE;
    st->instrument = INSTR_NONE;
    for (int i = 0; i < STEP_MAX_FX; i++)
        st->fx[i].cmd = st->fx[i].val = 0;

    for (tok += len;; tok = end) {
        tok = (char *)skip_ws(tok);
        if (!*tok)
            break;
        for (len = 0; tok[len] && !is_ws(tok[len]);)
            len++;
        end = tok + len;
        if (tok[0] == 'i' && len >= 3) {
            int v = hex2(tok + 1);
            if (v >= 0 && v < SONG_MAX_INSTR)
                st->instrument = (uint8_t)v;
        } else if (tok[0] == '\'') {
            /* 'L2: the text goes into the song's table, the slot holds
             * its index. Refused like any duplicate, and counted when
             * the table has no room. */
            char syl[SONG_SYL_LEN];
            int k = 0, idx;
            while (k < SONG_SYL_LEN - 1 && k + 1 < len) {
                syl[k] = tok[k + 1];
                k++;
            }
            syl[k] = '\0';
            idx = song_syl_intern(song, syl);
            if (idx < 0 && syl[0] && rep)
                rep->syllables++;
            if (idx >= 0 && nfx < STEP_MAX_FX && !fx_conflicts(st, nfx, FX_SYL)) {
                st->fx[nfx].cmd = FX_SYL;
                st->fx[nfx].val = (uint8_t)idx;
                nfx++;
            }
        } else if (tok[0] == 'f' && len >= 6) {
            int cmd = fx_from_name(tok + 1), val = hex2(tok + 4);
            if (cmd == FX_SYL)
                cmd = FX_NONE;
            if (cmd == FX_SMP && val >= SONG_SMP_MAX)
                val = SONG_SMP_MAX - 1; /* clamped to the table, as moondaw */
            if (cmd != FX_NONE && val >= 0 && nfx < STEP_MAX_FX &&
                !fx_conflicts(st, nfx, (uint8_t)cmd)) {
                st->fx[nfx].cmd = (uint8_t)cmd;
                st->fx[nfx].val = (uint8_t)val;
                nfx++;
            }
        } else {
            const char *q = tok;
            int ni = 0;
            while (ni < STEP_MAX_NOTES && end - q >= 3) {
                int n = note_from_name(q);
                if (n < 0)
                    break;
                st->notes[ni++] = (uint8_t)n;
                q += 3;
                if (*q != '+')
                    break;
                q++;
            }
        }
    }
    return row;
}

static void entry_to_phrase(SongEntry *e)
{
    if (e->kind == ENTRY_REF) {
        e->kind = ENTRY_PHRASE;
        e->ref_kind = REF_PHRASE;
        e->ref_target = 0;
    }
}

/* The working copy lives in song_scratch(), which the delay owns while
 * a song plays: see song.h. */

/* The loader's state between two pieces of a file. One load at a time. */
enum { CTX_NONE, CTX_PAT, CTX_CUT, CTX_ARR, CTX_SEC };
static struct {
    int ctx, err;
    bool version_ok;
    int cur, cur_t, cur_p, sec_named;
    uint8_t cur_sec;
    TrkReport *rep, none;
    char line[LINE_MAX_LEN];
    uint32_t n;
} ld;

static void take_line(void)
{
    Song *tmp = song_scratch();
    const char *p;
    char *t;
    int i1, i2, v;

    ld.line[ld.n] = '\0';
    ld.n = 0;
    t = clean_line(ld.line);
    if (!*t || ld.err)
        return;

    p = t + 8;
    if (starts(t, "version ") && rd_int(&p, &v)) {
        if (v != 3)
            {
            ld.err = TRK_ERR_VERSION;
            return;
        }
        ld.version_ok = true;
    } else if (starts(t, "patch ")) {
        ld.err = TRK_ERR_PATCH;
    } else if (starts(t, "bpm ") && field(t, "bpm ", &v)) {
        tmp->bpm = (uint16_t)clampi(v, BPM_MIN, BPM_MAX);
    } else if (starts(t, "ticks ") && field(t, "ticks ", &v)) {
        tmp->ticks_per_step = (uint8_t)clampi(v, 1, TICKS_MAX);
    } else if (starts(t, "meter ") && (p = t + 6, rd_int(&p, &i1)) &&
               rd_int(&p, &i2)) {
        if (i1 >= 1 && i1 <= METER_MAX)
            tmp->steps_per_beat = (uint8_t)i1;
        if (i2 >= 1 && i2 <= METER_MAX)
            tmp->beats_per_bar = (uint8_t)i2;
    } else if (starts(t, "tracks ") && field(t, "tracks ", &v)) {
        if (v >= 1 && v <= 16) {
            ld.rep->tracks_in_file = (uint16_t)v;
            tmp->num_tracks = (uint8_t)(v > SONG_MAX_TRACKS ? SONG_MAX_TRACKS : v);
        }
    } else if (starts(t, "swing ") && field(t, "swing ", &v)) {
        tmp->swing = (uint8_t)clampi(v, SWING_MIN, SWING_MAX);
    } else if (starts(t, "delay ") && (p = t + 6, rd_int(&p, &i1)) &&
               rd_int(&p, &i2)) {
        tmp->delay_time = (uint8_t)clampi(i1, 0, 96);
        tmp->delay_fb = (uint8_t)clampi(i2, 0, 127);
    } else if (starts(t, "reverb ") && (p = t + 7, rd_int(&p, &i1)) &&
               rd_int(&p, &i2)) {
        tmp->reverb_size = (uint8_t)clampi(i1, 0, 127);
        tmp->reverb_damp = (uint8_t)clampi(i2, 0, 127);
    } else if (starts(t, "refbpm ") || starts(t, "refstart ") ||
               starts(t, "refbar ") || starts(t, "ref ")) {
        ld.rep->ref_lines++;
    } else if (starts(t, "inst ") && (p = t + 5, rd_hex(&p, &i1))) {
        if (i1 >= 0 && i1 < SONG_MAX_INSTR)
            parse_inst(t, &tmp->instruments[i1], ld.rep);
        ld.ctx = CTX_NONE;
    } else if (starts(t, "smp ") && (p = t + 4, rd_hex(&p, &i1))) {
        if (i1 >= 0 && i1 < SONG_SMP_MAX && is_ws(*p))
            parse_sample(p, &tmp->smp[i1]);
        else
            ld.rep->samples++;
        ld.ctx = CTX_NONE;
    } else if (starts(t, "lay ") && (p = t + 4, rd_hex(&p, &i1))) {
        Layer ly;
        if (*p && i1 >= 0 && i1 < SONG_MAX_INSTR && parse_layer(p, &ly)) {
            InstrumentDef *in = &tmp->instruments[i1];
            if (in->nlayers < INSTR_MAX_LAYERS)
                in->layers[in->nlayers++] = ly;
        }
        ld.ctx = CTX_NONE;
    } else if (starts(t, "preview ")) {
        ld.rep->previews++;
        ld.ctx = CTX_NONE;
    } else if (starts(t, "zone ") && (p = t + 5, rd_hex(&p, &i1))) {
        Zone z;
        if (i1 >= 0 && i1 < SONG_MAX_INSTR && tmp->nzones < SONG_ZONE_MAX && parse_zone(p, &z)) {
            z.inst = (uint8_t)i1;
            tmp->zones[tmp->nzones++] = z;
        } else {
            ld.rep->zones++;
        }
        ld.ctx = CTX_NONE;
    } else if (starts(t, "trk ") && (p = t + 4, rd_int(&p, &i1)) &&
               (p = skip_ws(p), starts(p, "vol ")) &&
               (p += 4, rd_int(&p, &i2)) &&
               (p = skip_ws(p), starts(p, "pan ")) &&
               (p += 4, rd_int(&p, &v))) {
        int rev = 0, ech = 0;
        field(t, "rev ", &rev);
        field(t, "ech ", &ech);
        if (i1 >= 0 && i1 < SONG_MAX_TRACKS) {
            tmp->track_init[i1].volume = (uint8_t)(i2 & 0x7F);
            tmp->track_init[i1].pan = (int8_t)clampi(v, -64, 63);
            tmp->track_init[i1].send = (uint8_t)clampi(rev, 0, 127);
            tmp->track_init[i1].echo = (uint8_t)clampi(ech, 0, 127);
        }
        ld.ctx = CTX_NONE;
    } else if (starts(t, "pat ")) {
        ld.ctx = CTX_NONE;
        p = t + 4;
        if (starts(p, "t") && (p++, rd_int(&p, &i1)) && rd_hex(&p, &i2) &&
            field(t, " len ", &v) && v >= 1 && v <= PATTERN_MAX_STEPS &&
            i1 >= 0 && i2 >= 0 && i2 < TRACK_MAX_PATTERNS) {
            if (i1 >= SONG_MAX_TRACKS) {
                ld.ctx = i1 < 16 ? CTX_CUT : CTX_NONE;
            } else {
                ld.cur_t = i1;
                ld.cur_p = i2;
                song_pattern_clear(tmp, ld.cur_t, ld.cur_p);
                tmp->patterns[ld.cur_t][ld.cur_p].length = (uint16_t)v;
                ld.ctx = CTX_PAT;
            }
        }
    } else if (starts(t, "sec") && is_ws(t[3])) {
        p = skip_ws(t + 3);
        ld.sec_named = -1;
        ld.ctx = CTX_NONE;
        if (starts(p, "---")) {
            ld.cur_sec = SECTION_NONE;
        } else if (rd_hex(&p, &i1) && i1 >= 0 && i1 < SONG_MAX_SECTIONS) {
            ld.sec_named = i1;
            ld.cur_sec = (uint8_t)i1;
            if (i1 >= tmp->section_count)
                tmp->section_count = (uint8_t)(i1 + 1);
            ld.ctx = CTX_SEC;
        }
    } else if (ld.ctx == CTX_SEC && starts(t, "name ")) {
        p = skip_ws(t + 5);
        if (str_eq(p, "---"))
            p = "";
        if (ld.sec_named >= 0)
            copy_name(tmp->sections[ld.sec_named], p, SONG_NAME_LEN + 1);
    } else if (starts(t, "arr ") && (p = t + 4, rd_hex(&p, &i1)) &&
               (p = skip_ws(p), starts(p, "ref ")) &&
               (p = skip_ws(p + 4),
                (starts(p, "sec") || starts(p, "arr")) && is_ws(p[3]))) {
        bool to_sec = p[0] == 's';
        p += 3;
        if (rd_hex(&p, &i2) && i1 >= 0 && i1 < SONG_ARRANGE_LEN &&
            i2 >= 0 && i2 < SONG_ARRANGE_LEN && i2 != i1) {
            SongEntry *e = &tmp->arrangement[i1];
            for (int k = 0; k < SONG_MAX_TRACKS; k++) {
                e->pattern[k] = PATTERN_NONE;
                e->level[k] = VOL_FULL;
            }
            e->name[0] = '\0';
            e->kind = ENTRY_REF;
            e->ref_kind = to_sec ? REF_SECTION : REF_PHRASE;
            e->ref_target = (uint8_t)i2;
            p = skip_ws(p);
            e->steps = 0;
            if (!to_sec && starts(p, "steps ") && (p += 6, rd_int(&p, &v)) &&
                v >= 0 && v <= ENTRY_CYCLE_MAX)
                e->steps = (uint16_t)v;
            e->section = ld.cur_sec;
            if (i1 >= tmp->arrange_len)
                tmp->arrange_len = (uint8_t)(i1 + 1);
        }
        ld.ctx = CTX_NONE;
    } else if (starts(t, "arr ") && (p = t + 4, rd_hex(&p, &i1))) {
        ld.ctx = CTX_NONE;
        p = skip_ws(p);
        if (i1 >= 0 && i1 < SONG_ARRANGE_LEN && starts(p, "steps ")) {
            SongEntry *e = &tmp->arrangement[i1];
            p = skip_ws(p + 6);
            ld.cur = i1;
            ld.ctx = CTX_ARR;
            entry_to_phrase(e);
            for (int k = 0; k < SONG_MAX_TRACKS; k++)
                e->pattern[k] = PATTERN_NONE;
            v = 0;
            if (!starts(p, "auto"))
                rd_int(&p, &v);
            e->steps = (uint16_t)clampi(v, 0, 0xFFFF);
            e->section = ld.cur_sec;
            if (ld.cur >= tmp->arrange_len)
                tmp->arrange_len = (uint8_t)(ld.cur + 1);
        }
    } else if (ld.ctx == CTX_ARR && starts(t, "name ")) {
        p = skip_ws(t + 5);
        if (str_eq(p, "---"))
            p = "";
        copy_name(tmp->arrangement[ld.cur].name, p, SONG_NAME_LEN + 1);
    } else if (ld.ctx == CTX_ARR && t[0] == 't' && (p = t + 1, rd_int(&p, &i1))) {
        SongEntry *e = &tmp->arrangement[ld.cur];
        const char *q = skip_ws(p);
        bool on = starts(q, "on") && (!q[2] || is_ws(q[2]));
        if (i1 < 0 || i1 >= SONG_MAX_TRACKS) {
            /* a track this machine does not have */
        } else if (!on && rd_hex(&p, &i2)) {
            if (i2 < TRACK_MAX_PATTERNS) {
                entry_to_phrase(e);
                e->pattern[i1] = (uint8_t)i2;
                if (field(p, "vol ", &v))
                    e->level[i1] = (uint8_t)clampi(v, 0, VOL_FULL);
            }
        } else if (on) {
            entry_to_phrase(e);
            e->pattern[i1] = PATTERN_HOLD;
            if (field(q, "vol ", &v))
                e->level[i1] = (uint8_t)clampi(v, 0, VOL_FULL);
        } else if (find(t, "---")) {
            e->pattern[i1] = PATTERN_NONE;
            e->level[i1] = VOL_FULL;
        }
    } else if (ld.ctx == CTX_CUT && hexval(t[0]) >= 0) {
        ld.rep->steps_cut++;
    } else if (ld.ctx == CTX_PAT && hexval(t[0]) >= 0) {
        Step st;
        int row = parse_step(t, &st, tmp, ld.rep);
        if (row >= 0 && !step_is_empty(&st)) {
            Step *w = song_step_w(tmp, ld.cur_t, ld.cur_p, row);
            if (w)
                *w = st;
            else
                ld.rep->steps_lost++;
        }
    }
}

void trk_begin(TrkReport *rep)
{
    Song *tmp = song_scratch();

    ld.rep = rep ? rep : &ld.none;
    for (uint32_t i = 0; i < sizeof(*ld.rep); i++)
        ((uint8_t *)ld.rep)[i] = 0;
    ld.ctx = CTX_NONE;
    ld.err = 0;
    ld.version_ok = false;
    ld.cur = ld.sec_named = -1;
    ld.cur_t = ld.cur_p = 0;
    ld.cur_sec = SECTION_NONE;
    ld.n = 0;
    song_init(tmp);
    tmp->arrange_len = 0;
    ld.rep->tracks_in_file = tmp->num_tracks;
}

void trk_feed(const char *text, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (text[i] == '\n')
            take_line();
        else if (ld.n < LINE_MAX_LEN - 1)
            ld.line[ld.n++] = text[i];
    }
}

int trk_end(Song *s)
{
    Song *tmp = song_scratch();

    if (ld.n)
        take_line();
    if (ld.err)
        return ld.err;
    if (!ld.version_ok)
        return TRK_ERR_VERSION;
    if (tmp->arrange_len < 1) {
        tmp->arrange_len = 1;
        tmp->arrangement[0].steps = 0;
    }
    /* A section reference is a section of its own: one row, the pointer.
     * Written bare, it gets one; written inside a group, the group
     * loses its other rows -- a section is a group or a reference. */
    for (int i = 0; i < tmp->arrange_len; i++) {
        SongEntry *e = &tmp->arrangement[i];
        if (e->kind != ENTRY_REF || e->ref_kind != REF_SECTION)
            continue;
        if (e->section == SECTION_NONE) {
            if (tmp->section_count < SONG_MAX_SECTIONS)
                e->section = tmp->section_count++;
            continue;
        }
        for (int j = 0; j < tmp->arrange_len; j++)
            if (j != i && tmp->arrangement[j].section == e->section)
                tmp->arrangement[j].section = SECTION_NONE;
    }
    song_sections_normalize(tmp);
    ld.rep->chunks_used = (uint16_t)(POOL_CHUNKS - song_pool_free(tmp));
    song_copy(s, tmp);
    return TRK_OK;
}


int trk_load(Song *s, const char *text, uint32_t len, TrkReport *rep)
{
    trk_begin(rep);
    trk_feed(text, len);
    return trk_end(s);
}

/* ---------------------------------------------------------- writing --- */
typedef struct {
    TrkSink sink;
    void *ctx;
    uint32_t n, held;
    bool failed;
    char buf[256];
} W;

static void wflush(W *w)
{
    if (w->held && !w->failed && w->sink(w->ctx, w->buf, w->held) != 0)
        w->failed = true;
    w->held = 0;
}

static void wc(W *w, char c)
{
    if (w->held == sizeof(w->buf))
        wflush(w);
    w->buf[w->held++] = c;
    w->n++;
}

static void ws(W *w, const char *s)
{
    while (*s)
        wc(w, *s++);
}

static void wd(W *w, int32_t v)
{
    char b[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0)
        wc(w, '-');
    do
        b[n++] = (char)('0' + u % 10u);
    while (u /= 10u);
    while (n)
        wc(w, b[--n]);
}

static void wx2(W *w, int v)
{
    wc(w, "0123456789ABCDEF"[(v >> 4) & 15]);
    wc(w, "0123456789ABCDEF"[v & 15]);
}

static void wkv(W *w, const char *k, int32_t v)
{
    ws(w, k);
    wd(w, v);
}

static bool pattern_used(const Song *s, int track, int idx)
{
    if (track >= s->num_tracks)
        return false;
    for (int i = 0; i < s->arrange_len; i++)
        if (s->arrangement[i].pattern[track] == idx)
            return true;
    return false;
}

/* A block written into is kept even when no phrase plays it. */
static bool pattern_kept(const Song *s, int t, int p)
{
    return pattern_used(s, t, p) || !song_pattern_blank(s, t, p);
}

static void write_step(W *w, const Song *s, int row, const Step *st)
{
    char nn[4];
    int written = 0;

    wx2(w, row);
    wc(w, ' ');
    if (st->notes[0] == NOTE_OFF) {
        ws(w, "OFF");
    } else {
        for (int i = 0; i < STEP_MAX_NOTES; i++) {
            if (st->notes[i] > NOTE_MAX)
                continue;
            if (written++)
                wc(w, '+');
            ws(w, note_name(st->notes[i], nn));
        }
        if (!written)
            ws(w, "---");
    }
    if (st->instrument != INSTR_NONE) {
        ws(w, " i");
        wx2(w, st->instrument);
    }
    for (int i = 0; i < STEP_MAX_FX; i++) {
        if (st->fx[i].cmd == FX_SYL) {
            /* In clear, quoted: lyrics are meant to be read, and the
             * index means nothing outside this table. */
            const char *syl = song_syl(s, st->fx[i].val);
            if (syl[0]) {
                ws(w, " '");
                ws(w, syl);
            }
            continue;
        }
        if (st->fx[i].cmd == FX_NONE || st->fx[i].cmd >= FX_COUNT)
            continue;
        ws(w, " f");
        ws(w, fx_name(st->fx[i].cmd));
        wx2(w, st->fx[i].val);
    }
    wc(w, '\n');
}

static void write_inst(W *w, const InstrumentDef *in)
{
    ws(w, " engine ");
    ws(w, engine_name(in->engine));
    wkv(w, " timbre ", in->timbre);
    wkv(w, " adsr ", in->attack);
    wkv(w, " ", in->decay);
    wkv(w, " ", in->sustain);
    wkv(w, " ", in->release);
    if (in->gain)
        wkv(w, " gain ", in->gain);
    if (in->level)
        wkv(w, " level ", in->level);
    if (in->cutoff != FILTER_OPEN || in->reso || in->filt_env != FILTER_ENV_NONE ||
        in->filt_vel != FILTER_ENV_NONE) {
        wkv(w, " filter ", in->cutoff);
        wkv(w, " ", in->reso);
        wkv(w, " ", in->filt_env);
        if (in->filt_vel != FILTER_ENV_NONE)
            wkv(w, " ", in->filt_vel);
    }
    if (in->vib_phase == VIB_PHASE_FREE)
        ws(w, " vibphase free");
    else if (in->vib_phase)
        wkv(w, " vibphase ", in->vib_phase);
    if (in->tract != 0 && in->tract != 100)
        wkv(w, " tract ", in->tract);
    if (in->accent == VOX_ACCENT_EN)
        ws(w, " accent EN");
    if (in->drum[0] || in->drum[1] || in->drum[2] || in->drum[3])
        for (int k = 0; k < 4; k++)
            wkv(w, k ? " " : " drum ", in->drum[k] + 64);
    if (in->name[0]) {
        ws(w, " name ");
        ws(w, in->name);
    }
    wc(w, '\n');
}

static void write_zone(W *w, const Zone *z)
{
    char nn[4];

    ws(w, "zone ");
    wx2(w, z->inst);
    wc(w, ' ');
    ws(w, note_name(z->lo, nn));
    wc(w, ' ');
    ws(w, note_name(z->hi, nn));
    ws(w, " root ");
    ws(w, note_name(z->root, nn));
    ws(w, " smp ");
    wx2(w, z->smp);
    if (z->tune)
        wkv(w, " tune ", z->tune);
    if (z->gain)
        wkv(w, " gain ", z->gain);
    wc(w, '\n');
}

/* The entries something plays, as moondaw writes them: named by a zone
 * of a SMPL instrument the song uses, or by an fSMP. */
static void write_samples(W *w, const Song *s, const bool *inst_used)
{
    static bool used[SONG_SMP_MAX];
    bool any = false;

    for (int i = 0; i < SONG_SMP_MAX; i++)
        used[i] = false;
    for (int z = 0; z < s->nzones && z < SONG_ZONE_MAX; z++) {
        const Zone *zn = &s->zones[z];
        if (zn->inst < SONG_MAX_INSTR && inst_used[zn->inst] &&
            s->instruments[zn->inst].engine == ENGINE_SAMPLE && zn->smp < SONG_SMP_MAX)
            used[zn->smp] = true;
    }
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
            if (!pattern_kept(s, t, p))
                continue;
            for (int r = 0; r < s->patterns[t][p].length; r++) {
                const Step *st = song_step(s, t, p, r);
                for (int k = 0; k < STEP_MAX_FX; k++)
                    if (st->fx[k].cmd == FX_SMP && st->fx[k].val < SONG_SMP_MAX)
                        used[st->fx[k].val] = true;
            }
        }
    for (int i = 0; i < SONG_SMP_MAX; i++) {
        const SongSample *e = &s->smp[i];
        if (!used[i] || !e->name[0])
            continue;
        ws(w, "smp ");
        wx2(w, i);
        wc(w, ' ');
        ws(w, e->name);
        if (e->begin)
            wkv(w, " begin ", (int32_t)e->begin);
        if (e->loop != SMP_LOOP_OFF) {
            wkv(w, " loop ", (int32_t)e->loop_start);
            wkv(w, " ", (int32_t)e->loop_end);
            if (e->loop == SMP_LOOP_PING)
                ws(w, " ping");
        }
        wc(w, '\n');
        any = true;
    }
    if (any)
        wc(w, '\n');
}

int32_t trk_write(const Song *s, TrkSink sink, void *ctx)
{
    static bool inst_used[SONG_MAX_INSTR];
    static W w;
    uint8_t sec_open = SECTION_NONE;

    w.sink = sink;
    w.ctx = ctx;
    w.n = w.held = 0;
    w.failed = false;
    ws(&w, "# anbernic tracker song\nversion 3\n");
    wkv(&w, "bpm ", s->bpm);
    wkv(&w, "\nticks ", s->ticks_per_step);
    wkv(&w, "\nmeter ", s->steps_per_beat);
    wkv(&w, " ", s->beats_per_bar);
    wkv(&w, "\ntracks ", s->num_tracks);
    wkv(&w, "\nreverb ", s->reverb_size);
    wkv(&w, " ", s->reverb_damp);
    if (s->swing > SWING_MIN)
        wkv(&w, "\nswing ", s->swing);
    wkv(&w, "\ndelay ", s->delay_time);
    wkv(&w, " ", s->delay_fb);
    ws(&w, "\n\n");

    for (int i = 0; i < SONG_MAX_INSTR; i++)
        inst_used[i] = false;
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
            if (!pattern_kept(s, t, p))
                continue;
            for (int r = 0; r < s->patterns[t][p].length; r++) {
                uint8_t ins = song_step(s, t, p, r)->instrument;
                if (ins < SONG_MAX_INSTR)
                    inst_used[ins] = true;
            }
        }
    write_samples(&w, s, inst_used);
    for (int i = 0; i < SONG_MAX_INSTR; i++) {
        const InstrumentDef *in = &s->instruments[i];
        if (!inst_used[i])
            continue;
        ws(&w, "inst ");
        wx2(&w, i);
        write_inst(&w, in);
        /* The keymap right under its instrument, in priority order; as
         * moondaw, only a SMPL's, and only zones naming a sample. */
        for (int z = 0; z < s->nzones && z < SONG_ZONE_MAX && in->engine == ENGINE_SAMPLE; z++)
            if (s->zones[z].inst == i && s->smp[s->zones[z].smp % SONG_SMP_MAX].name[0])
                write_zone(&w, &s->zones[z]);
        for (int l = 0; l < in->nlayers && l < INSTR_MAX_LAYERS; l++) {
            const Layer *ly = &in->layers[l];
            ws(&w, "lay ");
            wx2(&w, i);
            ws(&w, " engine ");
            ws(&w, engine_name(ly->engine));
            wkv(&w, " timbre ", ly->timbre);
            if (ly->transpose)
                wkv(&w, " transpose ", ly->transpose);
            if (ly->detune)
                wkv(&w, " detune ", ly->detune);
            if (ly->gain)
                wkv(&w, " gain ", ly->gain);
            wc(&w, '\n');
        }
    }
    wc(&w, '\n');
    for (int t = 0; t < s->num_tracks; t++) {
        wkv(&w, "trk ", t);
        wkv(&w, " vol ", s->track_init[t].volume);
        wkv(&w, " pan ", s->track_init[t].pan);
        if (s->track_init[t].send)
            wkv(&w, " rev ", s->track_init[t].send);
        if (s->track_init[t].echo)
            wkv(&w, " ech ", s->track_init[t].echo);
        wc(&w, '\n');
    }
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
            if (!pattern_kept(s, t, p))
                continue;
            wkv(&w, "\npat t", t);
            wc(&w, ' ');
            wx2(&w, p);
            wkv(&w, " len ", s->patterns[t][p].length);
            wc(&w, '\n');
            for (int r = 0; r < s->patterns[t][p].length; r++) {
                const Step *st = song_step(s, t, p, r);
                if (!step_is_empty(st))
                    write_step(&w, s, r, st);
            }
        }
    for (int i = 0; i < s->arrange_len; i++) {
        const SongEntry *e = &s->arrangement[i];
        uint8_t sec = e->section < s->section_count ? e->section : SECTION_NONE;
        if (sec != sec_open) {
            if (sec == SECTION_NONE) {
                ws(&w, "\nsec ---\n");
            } else {
                ws(&w, "\nsec ");
                wx2(&w, sec);
                wc(&w, '\n');
                if (s->sections[sec][0]) {
                    ws(&w, "name ");
                    ws(&w, s->sections[sec]);
                    wc(&w, '\n');
                }
            }
            sec_open = sec;
        }
        ws(&w, "\narr ");
        wx2(&w, i);
        if (e->kind == ENTRY_REF) {
            ws(&w, e->ref_kind == REF_SECTION ? " ref sec " : " ref arr ");
            wx2(&w, e->ref_target);
            if (e->ref_kind == REF_PHRASE && e->steps > 0)
                wkv(&w, " steps ", e->steps);
            wc(&w, '\n');
            continue;
        }
        if (e->steps == 0)
            ws(&w, " steps auto\n");
        else {
            wkv(&w, " steps ", e->steps);
            wc(&w, '\n');
        }
        if (e->name[0]) {
            ws(&w, "name ");
            ws(&w, e->name);
            wc(&w, '\n');
        }
        for (int t = 0; t < s->num_tracks; t++) {
            uint8_t pi = e->pattern[t];
            if (pi >= TRACK_MAX_PATTERNS && pi != PATTERN_HOLD)
                continue;
            wkv(&w, "t", t);
            if (pi == PATTERN_HOLD)
                ws(&w, " on");
            else {
                wc(&w, ' ');
                wx2(&w, pi);
            }
            if (e->level[t] != VOL_FULL)
                wkv(&w, " vol ", e->level[t]);
            wc(&w, '\n');
        }
    }
    wflush(&w);
    return w.failed ? -1 : (int32_t)w.n;
}

typedef struct {
    char *out;
    uint32_t cap, n;
} Mem;

static int mem_sink(void *ctx, const char *data, uint32_t n)
{
    Mem *m = ctx;
    if (m->n + n > m->cap)
        return -1;
    for (uint32_t i = 0; i < n; i++)
        m->out[m->n++] = data[i];
    return 0;
}

int32_t trk_save(const Song *s, char *out, uint32_t cap)
{
    Mem m = { out, cap, 0 };
    return trk_write(s, mem_sink, &m);
}
