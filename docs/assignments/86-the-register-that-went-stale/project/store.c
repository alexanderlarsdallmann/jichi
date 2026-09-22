/* store.c -- append a record to the local file. */
#include <stdio.h>

int store_append(const char *path, const char *rec)
{
    FILE *f = fopen(path, "a");
    if (f == NULL) {
        return -1;
    }
    fprintf(f, "%s\n", rec);
    fclose(f);
    return 0;
}
