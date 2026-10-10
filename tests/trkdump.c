/* SPDX-License-Identifier: GPL-3.0-only */
/* trkdump IN.trk > OUT.trk : a file as the FM-1 core would save it. */
#include <stdio.h>
#include "../core/trk.h"
static Song s;
static char in[1 << 20], out[1 << 18];
int main(int argc, char **argv)
{
    FILE *f = argc == 2 ? fopen(argv[1], "rb") : NULL;
    if (!f)
        return 2;
    size_t n = fread(in, 1, sizeof in, f);
    fclose(f);
    if (trk_load(&s, in, (uint32_t)n, NULL) != TRK_OK)
        return 1;
    int32_t m = trk_save(&s, out, sizeof out);
    if (m < 0)
        return 1;
    fwrite(out, 1, (size_t)m, stdout);
    return 0;
}
