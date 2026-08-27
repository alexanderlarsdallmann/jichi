/* candidate b - answers correctly; borrows the WHOLE file and returns it */
#include "digest.h"
#include "track.h"

#include <stdio.h>

unsigned long digest_file_lines(const char *path)
{
    FILE *f;
    long size;
    char *all;
    size_t i, n, len;
    unsigned long d = 0;

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0xffffffffUL;
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return 0xffffffffUL;
    }
    n = (size_t)size;
    if (n == 0) {
        fclose(f);
        return 0;
    }
    all = (char *)xmalloc(n);
    if (all == NULL) {
        fclose(f);
        return 0xffffffffUL;
    }
    if (fread(all, 1, n, f) != n) {
        xfree(all, n);
        fclose(f);
        return 0xffffffffUL;
    }
    fclose(f);
    len = 0;
    for (i = 0; i < n; i++) {
        if (all[i] == '\n') {
            d = (d * 31 + (unsigned long)len) & 0xffffffffUL;
            len = 0;
        } else {
            len++;
        }
    }
    if (len > 0) {
        d = (d * 31 + (unsigned long)len) & 0xffffffffUL;
    }
    xfree(all, n);
    return d;
}
