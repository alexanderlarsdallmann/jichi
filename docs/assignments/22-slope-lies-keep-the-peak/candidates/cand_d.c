/* candidate d - clean memory; counts the newline into the line length */
#include "digest.h"
#include "track.h"

#include <stdio.h>

unsigned long digest_file_lines(const char *path)
{
    FILE *f;
    char *line;
    size_t cap = 64;
    size_t len = 0;
    unsigned long d = 0;
    int ch;

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0xffffffffUL;
    }
    line = (char *)xmalloc(cap);
    if (line == NULL) {
        fclose(f);
        return 0xffffffffUL;
    }
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') {
            d = (d * 31 + (unsigned long)len + 1) & 0xffffffffUL;
            len = 0;
            continue;
        }
        if (len + 1 > cap) {
            char *nl = (char *)xrealloc(line, cap, cap * 2);
            if (nl == NULL) {
                xfree(line, cap);
                fclose(f);
                return 0xffffffffUL;
            }
            line = nl;
            cap *= 2;
        }
        line[len++] = (char)ch;
    }
    if (len > 0) {
        d = (d * 31 + (unsigned long)len) & 0xffffffffUL;
    }
    xfree(line, cap);
    fclose(f);
    return d;
}
