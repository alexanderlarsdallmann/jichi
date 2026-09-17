/* loadcfg.h - read a whole small file into memory.
 *
 * The contract the implementation must honour. Read it before loadcfg.c:
 * every rule here is one the pristine loadcfg.c breaks.
 */
#ifndef LOADCFG_H
#define LOADCFG_H

/* Largest file cfg_load accepts, in bytes. Anything larger is REFUSED --
 * and refused before the allocation, not after it, so a file far bigger
 * than memory can never become an allocation attempt. */
#define CFG_MAX_BYTES 65536L

/* Result codes. */
#define CFG_OK      0
#define CFG_ENOFILE 1  /* the path cannot be opened */
#define CFG_ETOOBIG 2  /* the file is larger than CFG_MAX_BYTES */
#define CFG_EIO     3  /* a read failed part-way through */

/* Read the whole file at `path`.
 *
 * On CFG_OK:
 *   *out is a malloc'd, NUL-TERMINATED buffer the caller frees;
 *   *len is the byte length of the contents, NOT counting the terminator,
 *        and it is the file's REAL length -- not a capacity, not a guess.
 *
 * On any error:
 *   *out is NULL, *len is 0, and a one-line message that CONTAINS THE PATH
 *   has been written to stderr. A caller who did not pass the path cannot
 *   tell the user which file was missing; you did, so you can.
 */
int cfg_load(const char *path, char **out, long *len);

#endif /* LOADCFG_H */
