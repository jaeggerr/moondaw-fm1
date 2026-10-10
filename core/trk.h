/* SPDX-License-Identifier: GPL-3.0-only */
/* The .trk song file, read from and written to memory. The format is
 * moondaw's FORMAT.md, version 3, and the file is the same one: what
 * this machine cannot hold is skipped on the way in and counted, so a
 * screen can say what a save from here would lose. */
#ifndef TRK_H
#define TRK_H

#include "song.h"

typedef struct {
    uint16_t tracks_in_file; /* what `tracks` said; > 8 means some are cut */
    uint16_t silent_instr;   /* instruments on SMPL, VOX or PLUCK */
    uint16_t samples;        /* smp lines */
    uint16_t zones;          /* zone lines */
    uint16_t ref_lines;      /* ref, refbpm, refstart, refbar */
    uint16_t previews;       /* preview lines */
    uint16_t shaping;        /* instruments carrying drive, amp, tone, tract or accent */
    uint16_t syllables;      /* 'XX tokens */
    uint16_t smp_fx;         /* fSMP tokens */
    uint16_t steps_cut;      /* step lines on a track past the eighth */
    uint16_t steps_lost;     /* step lines the pool had no room for */
    uint16_t chunks_used;
} TrkReport;

enum {
    TRK_OK = 0,
    TRK_ERR_VERSION = -1, /* no `version 3` line: older files go through moondaw first */
    TRK_ERR_PATCH = -2,   /* a `patch` file: not applied here yet */
};

/* Loads through a working copy: on error *s is untouched. `rep` may be 0. */
int trk_load(Song *s, const char *text, uint32_t len, TrkReport *rep);
/* Returns the length written, or -1 when `cap` is too small. */
int32_t trk_save(const Song *s, char *out, uint32_t cap);

/* The same in pieces, for a file that is never whole in memory: the
 * flash is read and written a sector at a time. A piece may end in the
 * middle of a line. One load and one save at a time. */
void trk_begin(TrkReport *rep);
void trk_feed(const char *text, uint32_t len);
int trk_end(Song *s);
typedef int (*TrkSink)(void *ctx, const char *data, uint32_t n); /* 0 = taken */
int32_t trk_write(const Song *s, TrkSink sink, void *ctx);       /* bytes, or -1 */

#endif
