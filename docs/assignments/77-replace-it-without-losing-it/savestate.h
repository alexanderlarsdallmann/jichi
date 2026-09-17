/* savestate.h - replace a file's contents without ever losing the old ones.
 *
 * The contract the implementation must honour. Read it before savestate.c:
 * the pristine savestate.c breaks every rule below in one line.
 */
#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <stddef.h>

/* Result codes. */
#define SAVE_OK   0
#define SAVE_EIO  1  /* a write, close or rename failed; the OLD file is intact */
#define SAVE_EARG 2  /* a bad argument (NULL path, path too long) */

/* Replace the contents of the file at `path` with `data[0..len)`.
 *
 * The one rule that matters: AT NO MOMENT may a reader of `path` see anything
 * but the complete old contents or the complete new contents. Not an empty
 * file. Not half of the new one. In particular:
 *
 *   - `path` itself is never opened for writing. The new bytes go to a
 *     temporary file IN THE SAME DIRECTORY as `path` (rename() is atomic only
 *     within one filesystem, and /tmp may be another), and that file is
 *     rename()d over `path` only once every byte is on it and fclose() has
 *     said so.
 *   - if anything fails before the rename, the temporary is removed and the
 *     old file is untouched; the return value is SAVE_EIO, and nothing is
 *     left behind in the directory.
 *   - when `secret` is non-zero the file is created with mode 0600 -- at
 *     creation, not chmod()ed afterwards, so there is no window in which it
 *     is readable by anyone else. That mode carries to `path` through the
 *     rename. When `secret` is zero, an ordinary 0644 (subject to umask).
 *   - `fclose()`'s return value is checked, and the byte count fwrite()
 *     returned is compared with `len`. A full disk reports itself at close;
 *     code that checks only fwrite reports success on a truncated file.
 *
 * Durability (whether the bytes survive a power cut) is deliberately NOT part
 * of this contract -- see the brief, and FILE_HANDLING.md section 2, for why
 * jichi itself never calls fsync(), and when that would be a bug.
 */
int state_save(const char *path, const char *data, size_t len, int secret);

#endif /* SAVESTATE_H */
