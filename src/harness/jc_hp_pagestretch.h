/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_pagestretch.h -- the model-failure stretch (M1213, batch H12; SV:0433's option (a),
 * S1 of the three-step stretch). The productized form of the lead's C89
 * oracle `docs/internal/drive/2026-10-27-hardening-host/page_stretch_oracle.c`
 * (proven byte-for-byte against the reference page_stretch.py on nine
 * fixtures, run 0101 LOG). The oracle stays in the tree as the reference
 * implementation, marked NOT product code; this header is the seam the
 * worker's .c implements.
 *
 * THE FUNCTION IS PURE IN THE DATA: it reads a drive dir's telemetry files
 * (the I/O half) and computes ONE stretch line (the logic half). It writes
 * NO state, prints NOTHING to stdout itself, and takes every allocation from
 * the caller's arena (the three-arena rule: a page check is per-call data,
 * and the arena passed in outlives the call). It is not wired into the
 * harness verb in S1: S2 adds the unit test, S3 the driver cells, and the
 * `page`/`page-watch` verbs (A2/A3) call it.
 *
 * C89 at zero warnings under -std=c89 -pedantic -Wall -Wextra (WERROR=1):
 * jc_snprintf never snprintf/sprintf; declarations at block top; no
 * compound literals; double arithmetic as in the oracle. */
#ifndef JC_HP_PAGESTRETCH_H
#define JC_HP_PAGESTRETCH_H

#include <stddef.h>

#include "jc_mem.h" /* struct jc_arena */

#ifdef __cplusplus
extern "C" {
#endif

/* The model-failure stretch of a drive dir (b1-b5 of notes/M1213.md).
 *
 * Runs (b1): the LAST TWO 4-digit-named entries of <drive_dir>/runs
 * (sorted ascending by name; a name qualifies when strlen == 4 and every
 * character is '0'-'9'; a missing runs dir means zero runs). Each run's
 * telemetry file is <drive_dir>/runs/<name>/telemetry.jsonl; a missing or
 * non-regular file is SKIPPED (os.path.isfile), not an error.
 *
 * A call (b2): one JSON object per line, parsed with jichi's own cJSON
 * (src/json/cJSON.h -- the API-compatible original; the line is freed with
 * cJSON_Delete after each parse). A line counts when the "event" member is
 * the STRING "model_call" AND the "ts" member is a JSON number
 * (cJSON_IsNumber; a string ts, a null ts or any other type is skipped --
 * the string-ts edge fixture). sent = ts - (latency_ms or 0)/1000.0, where
 * a missing or non-number latency_ms is 0. cls is "ok" when the "ok"
 * member is JSON-true (cJSON_IsTrue); otherwise cls is "<status>/<class>"
 * where each member is formatted like the reference's `%s % value`:
 * absent or JSON-null -> "None"; a JSON number that is an integral value ->
 * its digits via %d (cast to int); a JSON number that is not integral ->
 * %g; a JSON string -> the string as-is. The None/None form is KEPT, not
 * cleaned (b5's note; the reference prints it).
 *
 * The stretch (b3-b4): the combined call list of both runs is sorted by
 * (ts, then sent, then cls) -- a STABLE sort on the sort keys (the oracle's
 * bubble sort; equal keys keep input order). last_ok is the LARGEST ts of
 * any ok call (0 when there is none); the fails are the not-ok calls with
 * ts > last_ok. Exactly ONE line is written to line_out (NUL-terminated,
 * cut to cap including the NUL):
 *   OPEN 0: `0 <last_ok> 0 0 0 - <last run>\n`
 *   OPEN 1: `1 <last_ok> <min sent of fails> <max ts of fails> <n> <cls>
 * <last run>\n`
 * The three times carry the reference's %.0f byte guarantee (round-half-to-even: 50.5 -> 50, 105.5 -> 106), emitted in the implementation via a half-to-even integer round + %lu because jc_snprintf's %f truncates (M1213 S1-b; the sttb 106 / stta 50 fixtures pin it). <last run> is the NEWER of the two run names (the second of the sorted set), or "-" when there is no run. <cls> is the class count:
 * first-seen order of the classes walking the fails, each "cls:n",
 * comma-joined, MOST COMMON FIRST -- and on equal counts, FIRST-SEEN order
 * (collections.Counter.most_common; the tie-order fixture pins it).
 *
 * Returns 0 on success (the line is always produced, even for the empty
 * drive: `0 0 0 0 0 - -\n`); -1 on a NULL argument (drive_dir, a, line_out)
 * or when the line does not fit line_cap. All intermediate storage
 * (the run names, the call list, the class table) is arena-allocated in
 * `a`; the function allocates no heap and frees nothing. No output goes to
 * stdout/stderr. */
int jc_pagestretch_line(const char *drive_dir, struct jc_arena *a,
                        char *line_out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* JC_HP_PAGESTRETCH_H */
