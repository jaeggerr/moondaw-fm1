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
 * So: no hold, no delay, nothing waits on it. An instrument is playable
 * the moment it can be. */
#ifndef SPLASH_H
#define SPLASH_H

void splash_show(void);

#endif
