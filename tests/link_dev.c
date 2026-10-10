/* SPDX-License-Identifier: GPL-3.0-only */
/* link_dev OUT : a device with nothing but the link and an empty flash,
 * listening where the simulator does. It waits for one song from
 * tools/fm1_send.py --udp, writes what its store then holds to OUT and
 * prints the name -- the end-to-end check of the host tool against the
 * firmware's receiver, without a machine. Gives up after ten seconds. */
#include <stdio.h>
#include <time.h>

#include "../app/app.h"
#include "../app/link.h"
#include "../platform/udp_sysex.h"
#include "ramflash.h"

Song app_song;
char app_name[STORE_NAME + 1] = "TEST";
int app_slot = -1;
bool app_dirty;

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

int main(int argc, char **argv)
{
    static char piece[4096];
    char name[STORE_NAME + 1];
    uint32_t bytes, t0 = now_ms();
    int32_t n;
    FILE *f;

    if (argc != 2 || udp_sysex_open() < 0)
        return 2;
    ramflash_blank();
    store_mount();
    link_reset();
    while (link_frame(now_ms(), true) != LINK_DONE) {
        if (now_ms() - t0 > 10000)
            return 3;
        usleep(1000);
    }
    if (store_info(link_slot(), name, &bytes) != STORE_OK || store_load_begin(link_slot()) != STORE_OK ||
        !(f = fopen(argv[1], "wb")))
        return 1;
    while ((n = store_load_get(piece, sizeof piece)) > 0)
        fwrite(piece, 1, (size_t)n, f);
    fclose(f);
    printf("link_dev: %s, slot %d\n", name, link_slot());
    return n < 0;
}
