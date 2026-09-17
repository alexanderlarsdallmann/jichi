/* savestate.c - saves the state. Usually.
 *
 * This is the save function most programs ship. It works every time the
 * process is not interrupted, the disk is not full, and nobody is reading
 * the file while it is being written. The first line is the bug: fopen()
 * with "w" TRUNCATES the file to zero bytes before a single new byte is
 * written, so from that instant until fclose() returns, the user's old data
 * exists nowhere.
 */
#include "savestate.h"
#include <stdio.h>

int state_save(const char *path, const char *data, size_t len, int secret)
{
    FILE *f;

    (void)secret;
    f = fopen(path, "w");
    if (f == NULL) {
        return SAVE_EIO;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    return SAVE_OK;
}
