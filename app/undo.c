/* SPDX-License-Identifier: GPL-3.0-only */
#include "undo.h"

#include "app.h"

enum { UNDO_BYTES = 12288, UNDO_RECS = 256, JOIN_MS = 1000 };
enum { K_STEP, K_BYTES };

typedef struct {
    uint32_t addr; /* K_STEP: track | pat << 8 | row << 16.  K_BYTES: offset in the Song */
    uint16_t len, at;
    uint8_t kind, first; /* first: this record opens a gesture */
} Rec;

static Song *song;
static uint8_t buf[UNDO_BYTES];
static Rec rec[UNDO_RECS];
/* rec[0, nrec) can be taken back; rec[nrec, ntop) were, and can be done
 * again: their bytes in buf are then what the undo replaced. */
static int nrec, ntop;
static uint32_t used, now, last_ms;
static bool open;     /* the last gesture is being written this frame */
static bool can_join; /* false after an undo: what comes next is a new gesture */

static uint8_t *live(const Rec *r)
{
    if (r->kind == K_STEP)
        return (uint8_t *)song_step(song, r->addr & 255, (r->addr >> 8) & 255, (int)(r->addr >> 16));
    return (uint8_t *)song + r->addr;
}

static int last_start(void)
{
    int i = nrec - 1;
    while (i > 0 && !rec[i].first)
        i--;
    return i;
}

static bool in_last(int kind, uint32_t addr, uint32_t len)
{
    for (int i = nrec ? last_start() : 0; i < nrec; i++)
        if (rec[i].kind == kind && rec[i].addr == addr && rec[i].len == len)
            return true;
    return false;
}

static void drop_oldest(void)
{
    int g = 1;
    uint32_t cut;
    while (g < nrec && !rec[g].first)
        g++;
    cut = g < nrec ? rec[g].at : used;
    for (uint32_t i = cut; i < used; i++)
        buf[i - cut] = buf[i];
    for (int i = g; i < ntop; i++) {
        rec[i - g] = rec[i];
        rec[i - g].at = (uint16_t)(rec[i - g].at - cut);
    }
    nrec -= g;
    ntop -= g;
    used -= cut;
}

/* A gesture that changed nothing (a knob against its stop) is not one. */
static void prune(void)
{
    int s;
    if (!nrec)
        return;
    s = last_start();
    for (int i = s; i < nrec; i++) {
        const uint8_t *a = live(&rec[i]), *b = buf + rec[i].at;
        for (int k = 0; k < rec[i].len; k++)
            if (a[k] != b[k])
                return;
    }
    used = rec[s].at;
    nrec = ntop = s;
}

static void note(int kind, uint32_t addr, uint32_t len, const uint8_t *src)
{
    bool first = false;

    if (!song || len == 0 || len > UNDO_BYTES / 2)
        return;
    if (ntop > nrec) { /* an edit after an undo: what could be done again is gone */
        used = rec[nrec].at;
        ntop = nrec;
    }
    if (in_last(kind, addr, len) && (open || (can_join && now - last_ms < JOIN_MS))) {
        open = true; /* the same place again: the oldest value is the one to keep */
        last_ms = now;
        return;
    }
    if (!open)
        first = true;
    while (nrec && (nrec >= UNDO_RECS || used + len > UNDO_BYTES)) {
        bool whole = last_start() == 0; /* the gesture being written is all that is left */
        drop_oldest();
        if (whole)
            first = true;
    }
    rec[nrec] = (Rec){ addr, (uint16_t)len, (uint16_t)used, (uint8_t)kind, first || nrec == 0 };
    for (uint32_t i = 0; i < len; i++)
        buf[used + i] = src[i];
    used += len;
    ntop = ++nrec;
    open = true;
    can_join = true;
    last_ms = now;
}

void undo_reset(Song *s)
{
    song = s;
    nrec = ntop = 0;
    used = 0;
    open = can_join = false;
}

void undo_frame(uint32_t ms)
{
    now = ms;
    if (open)
        prune();
    open = false;
}

void undo_step(int track, int pat, int row)
{
    if (song)
        note(K_STEP, (uint32_t)track | (uint32_t)pat << 8 | (uint32_t)row << 16, sizeof(Step),
             (const uint8_t *)song_step(song, track, pat, row));
}

void undo_bytes(void *p, uint32_t len)
{
    if (song)
        note(K_BYTES, (uint32_t)((uint8_t *)p - (uint8_t *)song), len, p);
}

bool undo_can(void) { return nrec > 0; }
bool undo_can_redo(void) { return ntop > nrec; }

/* The song's bytes and the journal's change places, for records [a, b):
 * once to take a gesture back, once more to do it again. */
static void swap(int a, int b)
{
    for (int i = b - 1; i >= a; i--) {
        uint8_t *d = rec[i].kind == K_STEP
                         ? (uint8_t *)song_step_w(song, rec[i].addr & 255, (rec[i].addr >> 8) & 255,
                                                  (int)(rec[i].addr >> 16))
                         : live(&rec[i]);
        /* no chunk left to put a step back in: it was empty before, and stays as it is */
        for (int k = 0; d && k < rec[i].len; k++) {
            uint8_t t = d[k];
            d[k] = buf[rec[i].at + k];
            buf[rec[i].at + k] = t;
        }
    }
    app_mark_dirty();
}

bool undo_pop(void)
{
    int s;
    if (open)
        prune();
    open = can_join = false;
    if (!song || !nrec)
        return false;
    s = last_start();
    swap(s, nrec);
    nrec = s;
    return true;
}

bool undo_redo(void)
{
    int e = nrec + 1;
    if (open)
        prune();
    open = can_join = false;
    if (!song || ntop <= nrec)
        return false;
    while (e < ntop && !rec[e].first)
        e++;
    swap(nrec, e);
    nrec = e;
    return true;
}

bool undo_turn(int d, const char **said)
{
    bool done = false;
    if (d == 0)
        return false;
    for (int n = d < 0 ? -d : d; n > 0; n--)
        done |= d < 0 ? undo_pop() : undo_redo();
    *said = done ? (d < 0 ? "UNDO" : "REDO") : d < 0 ? "NO UNDO" : "NO REDO";
    return done;
}
