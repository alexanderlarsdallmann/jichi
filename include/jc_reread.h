/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_reread.h - byte-identical re-read detection (M231, corrected M287).
 *
 * Telemetry from a private downstream workspace showed the same ~80-93 KB file
 * read dozens of times across a session. The read_file tool uses this to append
 * a factual advisory when a file is read again with byte-for-byte identical
 * content -- nudging the model to work from the copy it already has instead of
 * spending another round-trip. The check is keyed on size + a content hash, so
 * ANY edit (which changes the bytes) makes it NOT fire: no false positive after
 * a legitimate post-edit re-read.
 *
 * M287: the record is keyed on the (path, offset, limit) RANGE and the hash
 * covers the bytes actually SHOWN. M231 hashed the whole file and kept one
 * record per path, so a model paging a large file -- read lines 1-100, then
 * 100-250, then 200-280 -- was told every page after the first was
 * "byte-for-byte identical to your earlier read", which it plainly was not. On
 * one measured project the advisory fired 142 times while only 12 calls were
 * genuinely redundant: ~130 false accusations, and an advisory that is usually
 * wrong is one a model learns to ignore. Keying on the range makes paging
 * silent by construction, which is the property M231 claimed for edits.
 */
#ifndef JC_REREAD_H
#define JC_REREAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cap the per-session table. Paging a large file legitimately creates one record
 * per range, so this is bounded rather than unbounded-per-path; past the cap a
 * new range simply is not recorded, i.e. the worst case is a MISSED advisory,
 * never a false one -- the same direction of failure the rest of this module
 * chooses. */
#define JC_READ_RECS_MAX 256

/* Record of the last read of one (path, offset, limit) range this session. */
struct jc_read_rec {
    char         *path;   /* session-arena-owned resolved path (shared per path) */
    long          offset; /* 1-based first line requested (as resolved)         */
    long          limit;  /* max lines requested; 0 = to end of file            */
    unsigned long size;   /* byte length of the SHOWN bytes                     */
    unsigned long hash;   /* jc_reread_hash of those shown bytes                */
    int           count;  /* times this range was read (saturating) */
};

/* djb2 over `len` bytes -- NUL-safe (unlike a C-string hash), pure and
 * deterministic. NULL data hashes as an empty buffer. */
unsigned long jc_reread_hash(const char *data, unsigned long len);

#ifdef __cplusplus
}
#endif

#endif /* JC_REREAD_H */
