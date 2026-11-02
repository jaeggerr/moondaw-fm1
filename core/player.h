/* SPDX-License-Identifier: GPL-3.0-only */
/* The player: moondaw's sequencer/player.c and instruments/chain.c in
 * fixed point, for the three engines of core/synth.h. The tick is the
 * atom, counted inside the audio render; the rules of every fx are
 * moondaw's and so is the order they are applied in.
 *
 * After the MASTER knob, a peak limiter keeps the output under clipping
 * (player.c, LIM_*). */
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
/* The same for the song played by player_play_song: it goes round the
 * phrases [from, to] instead of running on, at once, even mid-playback;
 * `from` < 0 = the whole song. A listening tool as the one above, and as
 * apart from the file: ignored while one phrase plays on its own, cleared
 * by player_init. A `to` past the song's end is taken as its last phrase. */
void player_set_song_loop(int from, int to);
/* A note outside the song, on an instrument of the song: what a key
 * does on the INSTRUMENT screen, and the note PATTERN just wrote. One
 * at a time; the next one replaces it. */
void player_note_on(int instr, uint8_t note);
/* The same, a VOX instrument singing `syl` (the SYLLABLE screen). */
void player_note_sing(int instr, uint8_t note, const char *syl);
/* The same, a whole step: `n` notes struck together, as the step would
 * sound them (PATTERN's INS column auditions the step under the cursor).
 * One audition at a time, as above; `n` past a step's notes is clamped. */
void player_note_chord(int instr, const uint8_t *notes, int n);
/* The same under FX_ARP: one voice walks the notes, `arp` ticks each, at the
 * song's tempo whether it plays or not. */
void player_note_arp(int instr, const uint8_t *notes, int n, int arp);
void player_note_off(void);
void player_toggle_mute(int track);
void player_set_master(int pot);   /* 0..1023 */

/* What the audio side last did, for the screen. */
bool player_playing(void);
uint16_t player_row(void);
/* The tick inside player_row()'s step, 0 .. ticks_per_step - 1. */
uint8_t player_tick_in_step(void);
/* A click beside the song (the count-in of REC): `first`, the higher one. */
void player_click(bool first);
uint16_t player_entry(void);
bool player_muted(int track);
/* Monitoring, like the mutes: never in the file, cleared by player_init. */
void player_toggle_solo(int track);
bool player_soloed(int track);
/* The metronome: the count-in's click kept on, beating the song's tempo
 * while it plays -- four steps to a beat, the bar's first higher. Monitoring
 * too: never in the file, off at every power-on (EDIT + REC, 02/11). */
void player_toggle_metro(void);
bool player_metro(void);

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
