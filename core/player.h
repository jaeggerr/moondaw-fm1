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
/* Offline only: the same, the sum before the clamp and the MASTER pot. */
void player_render_raw(int32_t *out, int frames);

#endif
