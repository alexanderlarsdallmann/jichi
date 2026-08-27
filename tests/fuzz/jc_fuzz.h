/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fuzz.h - fuzz target registry (M123).
 *
 * A fuzz "target" is a thin function that feeds `len` bytes of (possibly
 * malformed) input to a pure parser and must never crash / read out of bounds /
 * leak (verified under ASan+UBSan). The same targets are driven by the in-tree
 * deterministic fuzzer (jc_fuzz_main.c) and, optionally, by libFuzzer
 * (jc_fuzz_libfuzzer.c, FUZZ=1). See docs/proposals/2026-07-fuzzing-suite.md.
 */
#ifndef JC_FUZZ_H
#define JC_FUZZ_H

#include <stddef.h>

typedef void (*jc_fuzz_fn)(const unsigned char *data, size_t len);

struct jc_fuzz_target {
    const char *name;  /* selector for `make fuzz TARGET=<name>` */
    jc_fuzz_fn  fn;
    const char *seed;  /* a small valid-ish seed to mutate from (may be "") */
};

extern const struct jc_fuzz_target JC_FUZZ_TARGETS[];
extern const int JC_FUZZ_N_TARGETS;

/* A NUL-terminated malloc'd copy of data[0..len) (parsers expect C strings).
 * Caller frees. NULL only on allocation failure. */
char *jc_fuzz_dup0(const unsigned char *data, size_t len);

/* Lives in jc_fuzz_targets_fs.c (needs _XOPEN_SOURCE for realpath/symlink). */
void jc_fuzz_pathfence(const unsigned char *data, size_t len);

#endif /* JC_FUZZ_H */
