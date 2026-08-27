/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_memtrim.h - return freed heap to the OS (M218).
 *
 * The agent's request path is balanced malloc/free, but the PATTERN is
 * hostile to glibc's defaults: every model call (and every retry -- the body
 * is rebuilt per attempt, M20e) allocates and frees ~3x the request text,
 * hundreds of KB at 100k-token inputs, thousands of times per marathon
 * session. Each free of an mmap'd chunk ratchets glibc's *dynamic* mmap
 * threshold upward, after which the big bodies come from brk -- and the brk
 * high-water never returns to the OS on its own. The result is RSS that only
 * grows while every leak checker reports zero and the arenas read near-empty
 * (/context: big "Process resident" vs tiny arena numbers).
 *
 * Two counter-measures, both no-ops when the libc lacks them (the Makefile
 * probes for <malloc.h> mallopt/malloc_trim as JC_HAVE_MALLOC_TRIM -- probed,
 * not #ifdef __GLIBC__, because uClibc-ng masquerades as glibc):
 *
 *   jc_mem_tune()       -- once at startup: PIN M_MMAP_THRESHOLD (128 KB) so
 *                          large transient bodies always mmap/munmap (the
 *                          dynamic ratchet is the actual bug), and lower
 *                          M_TRIM_THRESHOLD so brk shrinks eagerly. Costs one
 *                          syscall + page faults per >=128 KB allocation --
 *                          noise next to an HTTPS round-trip.
 *   jc_mem_release_os() -- at top-level turn boundaries: malloc_trim(0), the
 *                          sweep for sub-threshold residue (cJSON node churn).
 *                          Not per-attempt: the threshold pin already handles
 *                          the big per-retry bodies.
 */
#ifndef JC_MEMTRIM_H
#define JC_MEMTRIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pin the malloc tunables described above. Call once, early in main(). */
void jc_mem_tune(void);

/* Return free heap to the OS. Returns 1 if memory was released, 0 otherwise
 * (including the no-op build). Cheap; safe at any frequency. */
int jc_mem_release_os(void);

#ifdef __cplusplus
}
#endif

#endif /* JC_MEMTRIM_H */
