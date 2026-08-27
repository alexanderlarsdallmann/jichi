/* digest.h - the contract.
 *
 * digest_file_lines(path) reads a text file and returns a rolling digest of
 * its lines:
 *
 *     d = 0
 *     for each line (split on '\n'; a final line without a trailing
 *                    newline still counts; the '\n' is NOT part of the line):
 *         d = (d * 31 + line_length) modulo 2^32
 *
 * Returns the digest; 0 for an empty file. On any I/O or allocation error,
 * returns 0xffffffff (the file in this task always fits and opens).
 *
 * MEMORY CONTRACT -- as binding as the arithmetic: the implementation must
 * allocate through track.h (xmalloc/xrealloc/xfree), its live bytes must be
 * ZERO after it returns, and its PEAK must scale with the LONGEST LINE, not
 * with the file. A digest that answers correctly while holding the whole
 * file in memory does not satisfy this contract.
 */
#ifndef DIGEST_H
#define DIGEST_H

unsigned long digest_file_lines(const char *path);

#endif
