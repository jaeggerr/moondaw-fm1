/* SPDX-License-Identifier: GPL-3.0-only */
/* remote_dev : the whole app on an empty flash, listening where the
 * simulator does, with nothing on a screen -- for tests/remotetest.sh to
 * drive with tools/fm1_ctl.py --udp, as a computer drives the machine.
 * A frame every 16 ms; it stops after two seconds without a message
 * (fifteen before the first). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../app/app.h"
#include "../core/player.h"
#include "../platform/udp_sysex.h"
#include "ramflash.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
void plat_led_level(int l, int v) { (void)l; (void)v; }
int plat_audio_load(int *worst) { *worst = 12; return 3; } /* figures to read back */
int plat_battery(int *raw) { if (raw) *raw = -1; return -1; }

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

int main(void)
{
    static int16_t pcm[2 * 705];
    PlatInput in;
    uint32_t seen = 0, quiet = now_ms(), wait = 15000;

    if (udp_sysex_open() < 0)
        return 2;
    ramflash_blank();
    app_init();
    memset(&in, 0, sizeof in);
    in.master = 1023;
    while (now_ms() - quiet < wait) {
        in.ms = now_ms();
        app_frame(&in);
        app_audio(pcm, 705); /* a frame's worth of sound: the song moves on */
        if (udp_seen != seen)
            seen = udp_seen, quiet = now_ms(), wait = 2000;
        usleep(16000);
    }
    printf("remote_dev: %u messages, last screen %s, song %s\n", (unsigned)seen, app_screen(), app_name);
    return 0;
}
