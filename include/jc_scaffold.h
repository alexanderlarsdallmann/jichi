/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_scaffold.h - project asset scaffolding (the `init` subcommand core).
 *
 * jichi discovers project assets (agents, skills, commands, rules) read-only from
 * `.jichi/` and `~/.config/jichi/`. This module supplies the *write* side:
 * a set of compiled-in "packs", each a list of files with their contents, that
 * `jichi init [pack]` writes into a project so users start from a useful
 * baseline instead of an empty directory.
 *
 * Everything here is pure (no I/O): the pack tables and the destination-path
 * computation. The actual file writing (existence check, mkdir -p, write) is the
 * thin shell in src/main.c's run_init, so this core is unit-tested offline.
 */
#ifndef JC_SCAFFOLD_H
#define JC_SCAFFOLD_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* One file a pack installs: a path relative to the asset root -- no `.jichi/`
 * prefix and no leading slash, e.g. "agents/reviewer.md" or "AGENTS.md" -- plus
 * its contents as a NULL-terminated array of chunks. The contents are split
 * into per-line chunks so no single string literal exceeds the C89 509-char
 * minimum; jc_scaffold_file_text joins them. All static storage (compiled in). */
struct jc_scaffold_file {
    const char        *relpath;
    const char *const *lines;
};

/* A named bundle of files (e.g. the language-agnostic "default" set). */
struct jc_scaffold_pack {
    const char                    *name;
    const char                    *description;
    const struct jc_scaffold_file *files;
    int                            nfiles;
};

/* The compiled-in pack table. */
int                            jc_scaffold_pack_count(void);
const struct jc_scaffold_pack *jc_scaffold_pack_at(int i);
const struct jc_scaffold_pack *jc_scaffold_find_pack(const char *name);

/* Append a file's full contents (its joined chunks) to `sb` (pure). */
void jc_scaffold_file_text(const struct jc_scaffold_file *f, struct jc_sb *sb);

/* Compute the on-disk destination for a pack file `relpath` (pure).
 *
 * - When `global`, the destination is "<home>/.config/jichi/<relpath>".
 * - Otherwise (project mode) a top-level file (no '/' in `relpath`, e.g.
 *   "AGENTS.md") lands at the project root as-is; anything nested lands under
 *   ".jichi/<relpath>".
 *
 * Writes a NUL-terminated path into `out` (capacity `cap`) and returns its
 * length, or -1 if it would not fit. `home` is read only when `global`. */
int jc_scaffold_dest(const char *relpath, int global, const char *home,
                     char *out, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_SCAFFOLD_H */
