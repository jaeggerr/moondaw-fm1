/* SPDX-License-Identifier: GPL-3.0-only */
/* The two master sends: one room, one delay. moondaw's fx/reverb.c and
 * fx/echo.c in 16-bit lines and integer arithmetic.
 *
 * Buses are Q15 mono, int32. The returns are added to a stereo Q15 mix. */
#ifndef FX_H
#define FX_H

#include <stdbool.h>
#include <stdint.h>

void fx_init(void); /* clears both; the delay's lines are song_scratch() */

void reverb_set(uint8_t size, uint8_t damp);
bool reverb_ringing(void);
/* `in` may be 0: nothing sent, the tail alone. */
void reverb_process(const int32_t *in, int32_t *out, int frames);

/* The delay is counted in ticks, so the length of a tick comes with it.
 * The lines hold 743 ms: a longer delay is shortened to that (moondaw's
 * hold twelve seconds, in four megabytes). */
void echo_set(uint8_t ticks, uint8_t feedback, int tick_samples);
bool echo_ringing(void);
void echo_process(const int32_t *in, int32_t *out, int frames);

#endif
