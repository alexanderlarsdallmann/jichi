/* fields.c - field splitting for csvsum. */
#include "csvsum.h"

int split_fields(const char *line, int *out, int max)
{
    int n = 0;
    int cur = 0;
    int i;
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == ',') {
            if (n < max) {
                out[n++] = cur;
            }
            cur = 0;
        } else if (line[i] >= '0' && line[i] <= '9') {
            cur = cur * 10 + (line[i] - '0');
        }
    }
    return n;
}
