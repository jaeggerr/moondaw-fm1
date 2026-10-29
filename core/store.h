/* SPDX-License-Identifier: GPL-3.0-only */
/* Songs in flash. A file is a run of 4 KB sectors, each with its own
 * header and checksum; saving writes a whole new generation beside the
 * old one and commits it with a single two-byte write, so a battery that
 * dies mid-save leaves the song as it was or as it is now, never half.
 *
 * Nothing here knows what a song is: bytes in, bytes out, a piece at a
 * time, so a file is never whole in memory. */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/platform.h"

/* 0..30 the songs, 31 the app's .LAST record, 32 the sample bank
 * (core/bank.h). 32 until 29/10: a flash written then mounts the same,
 * and a firmware from before takes slot 32's sectors for free space. */
#define STORE_SLOTS 33
#define STORE_NAME  12 /* characters, not NUL-terminated when full */
/* A sector: STORE_HEAD bytes of header, then STORE_PAYLOAD of the file;
 * the first one opens with the name. */
#define STORE_HEAD    16u
#define STORE_PAYLOAD (PLAT_FLASH_SECTOR - STORE_HEAD)

enum {
    STORE_OK = 0,
    STORE_ERR_IO = -1,     /* the flash refused, or read back wrong */
    STORE_ERR_FULL = -2,   /* no free sector: the old file is untouched */
    STORE_ERR_NONE = -3,   /* nothing in that slot */
    STORE_ERR_CORRUPT = -4 /* a sector failed its checksum */
};

/* Reads every sector header. Call once, and again after a failed save. */
int store_mount(void);
bool store_has(int slot);
/* Name (STORE_NAME + 1 bytes, NUL-terminated here) and size on flash. */
int store_info(int slot, char *name, uint32_t *bytes);
uint32_t store_free(void); /* payload bytes a new file could take */

int store_save_begin(int slot, const char *name);
int store_save_put(const char *data, uint32_t n);
int store_save_commit(void); /* from here on the new file is the file */

int store_load_begin(int slot);
int32_t store_load_get(char *buf, uint32_t cap); /* bytes, 0 at the end, < 0 on error */

int store_delete(int slot);

/* Where a file lies: the flash sector holding each STORE_PAYLOAD bytes
 * of it, in order (the name counts: the file's byte k is at payload
 * position STORE_NAME + k). Returns the count, or a STORE_ERR_*. For
 * what is read in place (the sample bank, over XIP); good until the next
 * save, commit or delete. */
int store_map(int slot, uint8_t *sectors, int max);

#endif
