/* SPDX-License-Identifier: GPL-3.0-only */
/* What the FM-1 shows while it is still coming up: the Mac app's
 * wordmark (app/logo.h, from its own generator) and the version.
 *
 * It costs no boot time. The panel's own wake-up already spends about
 * 350 ms with the backlight on and nothing to see (firmware/src/lcd.c:
 * 80 ms of settling, then SWRESET's 150 ms and SLPOUT's 120 ms, which
 * the panel wants whatever we do), and after that the firmware still has
 * app_init(), the ADC, the audio and USB to go before there is a first
 * frame. The mark is drawn into that window and left there; the app's
 * first frame overwrites it band by band, as any other screen change.
 *
 * That window is not quite a second, and the mark is worth looking at,
 * so it is held to one: SPLASH_HOLD_MS counts from the moment it is
 * drawn, not from the end of the boot, so the time already spent coming
 * up is time served -- what is added is only the remainder, and only to
 * the first frame. firmware.c keeps that clock (the machine's business,
 * not this file's) and goes on running its loop while it ticks: the
 * audio, USB, the link and the OCT- + OCT+ way into UBOOT are all live
 * behind the picture. Nothing is blocked; the app's first frame waits,
 * and that is all. */
#ifndef SPLASH_H
#define SPLASH_H

#define SPLASH_HOLD_MS 1000

void splash_show(void);

#endif
