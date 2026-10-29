/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample bank: every sound SMPL can play, one file of the store
 * (slot BANK_SLOT), filled from the computer (tools/fm1_samples.py over
 * app/link.h). It shares the flash with the songs: nothing is set aside,
 * a sample more is a song less.
 *
 * The file, after the store's name:
 *
 *    0  'S' 'M' 'P' '1'
 *    4  count, then three zero bytes
 *    8  count entries of BANK_ENTRY bytes:
 *         name, BANK_NAME_LEN bytes, NUL-padded (the file's name
 *           without its folder and its .wav)
 *         frames u32: how long, at SMP_RATE
 *         at u32: where its first block starts in this file
 *    then the blocks (core/sampler.h), each sample a run of whole ones.
 *    A block starts where (at + STORE_NAME) is a multiple of SMP_BLOCK:
 *    16 of them fill a sector's payload, so none crosses a sector and
 *    the voice reads each in place, over XIP.
 *
 * A song names a sample by its file (`smp 00 tr909/tr9_kick.wav`); the
 * bank answers to the last part of it, without `.wav`, whatever the case
 * (tr9_kick). A name the bank does not have is silent, never an error. */
#ifndef BANK_H
#define BANK_H

#include <stdbool.h>
#include <stdint.h>

#include "song.h"
#include "store.h"

#define BANK_SLOT     32
#define BANK_FILE     ".SAMPLES"
#define BANK_MAX      128
#define BANK_NAME_LEN 24 /* 23 + NUL */
#define BANK_ENTRY    (BANK_NAME_LEN + 8)
#define BANK_HEAD     8

/* Reads the bank's index, after store_mount and after any change to the
 * bank's slot, and plays the last song resolved against it again. */
int bank_mount(void);
/* Nothing of the bank is read until bank_mount: before writing its slot. */
void bank_hold(void);
/* Which of the song's sample names the bank has. Again after the song's
 * table changes. The song is remembered for the next bank_mount. */
void bank_resolve(const Song *s);

int bank_count(void);      /* samples in the bank */
uint32_t bank_bytes(void); /* its file's size */
/* Sample `i`'s name and length in frames; false past the last. */
bool bank_entry(int i, char name[BANK_NAME_LEN], uint32_t *frames);
/* The bank's sample a song's name plays, -1 none. */
int bank_find(const char *song_name);

#endif
