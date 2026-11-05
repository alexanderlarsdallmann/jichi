/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_repomap.h - repository map: a compact index of the project's source files
 * and their top-level symbols, to orient the agent in an unfamiliar codebase.
 *
 * The map is generated once (at startup) and injected into the system prompt;
 * it is also printed by the `map` CLI subcommand and the TUI `/map`. Symbol
 * extraction is a pure, language-keyed heuristic line scan (no LSP, no parser)
 * so it is fast across a whole tree and unit-tested offline.
 */
#ifndef JC_REPOMAP_H
#define JC_REPOMAP_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_app;

/* Default total byte budget for the rendered map (overridable via config
 * repoMapLimit), and per-file / per-scan symbol caps. */
#define JC_REPOMAP_MAX       (12 * 1024)
#define JC_REPOMAP_FILE_SYMS 16
#define JC_REPOMAP_SCAN_CAP  64
#define JC_REPOMAP_MAX_FILES 4000

/* Pure: append the top-level symbol names found in `text` for the language of
 * file extension `ext` (no leading dot, e.g. "c", "py") to `out` -- a
 * caller-initialised jc_vec of char* whose pushed strings are malloc'd (the
 * caller frees them). Unknown/unsupported extensions add nothing. Returns the
 * number of symbols appended (bounded by JC_REPOMAP_SCAN_CAP). */
int jc_repomap_scan(const char *ext, const char *text, struct jc_vec *out);

/* M1081 (F-01): test hook. The high-water mark, in bytes, of the arena that
 * held the scanned-file TEXTS during the last jc_repomap_build / _render /
 * _build_dir call. Exposed as an accessor because the arena is build-local and
 * a test cannot otherwise observe its peak. F-01: before the fix this equals
 * the sum of every file read before the 12 KiB rendered limit stops the loop;
 * after, it equals one file (<= RM_MAX_FILE_BYTES). 0 if no build has run. */
jc_size jc_repomap_last_arena_peak(void);

/* Walk the workspace (`app->cwd`), scan recognised source files, and render a
 * bounded map section (with its own "## Repository map" header) into an
 * arena-owned string, or NULL when there are no source files. Honours the
 * configured byte limit. */
/* Should a directory walk descend into a directory called `name`? Shared by
 * BOTH walkers (the repo map and the search index), because a directory that is
 * not worth mapping is not worth indexing either, and two lists would drift.
 *
 * Pure on purpose -- no filesystem, no app -- so the decision is unit-testable;
 * the walk that uses it has no test at all (M520). `extra` is the config's
 * `ignoreDirs` (of char*), or NULL.
 *
 * Returns 1 to SKIP, 0 to descend. */
int jc_walk_skip_dir(const char *name, const struct jc_vec *extra);

char *jc_repomap_build(struct jc_app *app);

/* Like jc_repomap_build but the caller owns the returned string (free()).
 * M218: for transient consumers (the TUI /map, the map subcommand) -- the
 * session-arena copy is only for app->repo_map, which lives on the prompt.
 * Deliberately does NOT cache into app->repo_map: that would inject the map
 * into the system prompt of a session configured with repoMap off. */
char *jc_repomap_render(struct jc_app *app);

/* Like jc_repomap_build but scoped to the source tree rooted at `dir` (paths
 * shown relative to `dir`), with no "## Repository map" header -- just the
 * `<relpath>: symbols` listing. NULL `dir` falls back to app->cwd. Returns an
 * arena-owned string, or NULL when the tree has no recognised source files.
 * Backs the @folder:<dir> reference (M34/F5). */
char *jc_repomap_build_dir(struct jc_app *app, const char *dir);

#ifdef __cplusplus
}
#endif
#endif /* JC_REPOMAP_H */
