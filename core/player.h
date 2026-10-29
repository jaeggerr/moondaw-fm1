/* SPDX-License-Identifier: GPL-3.0-only */
/* The player: moondaw's sequencer/player.c and instruments/chain.c in
 * fixed point, for the three engines of core/synth.h. The tick is the
 * atom, counted inside the audio render; the rules of every fx are
 * moondaw's and so is the order they are applied in.
 *
 * Not here yet: the master limiter, the metronome. */
#ifndef PLAYER_H
#define PLAYER_H

#include "song.h"

void player_init(Song *s); /* the song it plays; call once, and stopped */

/* From the main loop. Queued, taken up at the next audio block. */
void player_play_song(int entry, bool once);
void player_play_entry(int entry); /* loops one phrase */
void player_stop(void);
/* Restricts a phrase played by player_play_entry to the steps
 * [from, from + len), at once, even mid-playback. len 0 = the whole
 * phrase. A listening tool: never in the file, ignored when the song
 * plays, cleared by player_init. */
void player_set_loop(int from, int len);
/* A note outside the song, on an instrument of the song: what a key
 * does on the INSTRUMENT screen, and the note PATTERN just wrote. One
 * at a time; the next one replaces it. */
void player_note_on(int instr, uint8_t note);
/* The same, a VOX instrument singing `syl` (the SYLLABLE screen). */
void player_note_sing(int instr, uint8_t note, const char *syl);
void player_note_off(void);
void player_toggle_mute(int track);
void player_set_master(int pot);   /* 0..1023 */

/* What the audio side last did, for the screen. */
bool player_playing(void);
uint16_t player_row(void);
uint16_t player_entry(void);
bool player_muted(int track);
/* Monitoring, like the mutes: never in the file, cleared by player_init. */
void player_toggle_solo(int track);
bool player_soloed(int track);

/* From the audio interrupt: interleaved stereo, any number of frames. */
void player_render(int16_t *out, int frames);
/* Where the audio block's time goes, measured on the machine (28/10: the
 * chorus of DAFT-PUNK-EN overran its block). The platform sets the clock,
 * microseconds; unset, nothing is measured. player_profile() gives the
 * breakdown of the worst block since the last reset: PROF_* microseconds,
 * then the block's whole. The stages are the first core's; PROF_WAIT is
 * what it waited for the second core's share. */
enum { PROF_TICK, PROF_OSC, PROF_VOX, PROF_CHAIN, PROF_REV, PROF_ECHO, PROF_WAIT, PROF_N };
extern uint32_t (*player_clock)(void);
void player_profile(uint32_t out[PROF_N + 1]);
void player_profile_reset(void);

/* A second core, where the platform has one (the FM-1's CPU1): run(fn, arg)
 * hands fn(arg) over and returns 1, or 0 (do it yourself); wait() returns 0
 * once it is done, -1 if it was given up (do it yourself). Unset: one core.
 * The player hands it every other sounding track of each render. */
extern int (*player_run_other)(void (*fn)(uint32_t), uint32_t arg);
extern int (*player_wait_other)(void);

/* Offline only: the same, the sum before the clamp and the MASTER pot. */
void player_render_raw(int32_t *out, int frames);

#endif
