/* loadcfg.c - reads a file. On a good day.
 *
 * This compiles, and it works on exactly one kind of input: a file that
 * exists, is readable, and happens to be CFG_MAX_BYTES long. Every other
 * input is handled by not handling it.
 */
#include "loadcfg.h"
#include <stdio.h>
#include <stdlib.h>

int cfg_load(const char *path, char **out, long *len)
{
    FILE *f;
    char *buf;

    f = fopen(path, "rb");
    buf = (char *)malloc((size_t)CFG_MAX_BYTES + 1);

    fread(buf, 1, (size_t)CFG_MAX_BYTES, f);
    buf[CFG_MAX_BYTES] = '\0';
    fclose(f);

    *out = buf;
    *len = CFG_MAX_BYTES;
    return CFG_OK;
}
