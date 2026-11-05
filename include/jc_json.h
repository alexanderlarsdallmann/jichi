/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_json.h - thin convenience layer over the in-tree cJSON.
 *
 * We do not reimplement JSON; cJSON does the work. These helpers provide
 * null-safe typed accessors so call sites stay terse, and a couple of
 * arena-aware string copies.
 */
#ifndef JC_JSON_H
#define JC_JSON_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "cJSON.h"

/* Parse `text`; returns NULL on malformed input. Caller cJSON_Delete()s. */
cJSON *jc_json_parse(const char *text);

/* Typed object-field accessors. Each returns `dflt` if the key is missing
 * or has the wrong type. Returned strings are owned by the cJSON tree. */
const char *jc_json_get_str(const cJSON *o, const char *key, const char *dflt);
double      jc_json_get_num(const cJSON *o, const char *key, double dflt);
int         jc_json_get_bool(const cJSON *o, const char *key, int dflt);

/* Like jc_json_get_bool but also accepts the unambiguous NON-BOOL encodings of
 * a boolean: a JSON number (nonzero = true) and the strings "true"/"false",
 * "yes"/"no", "1"/"0" (case-insensitive). Anything else -- prose, an object, an
 * array -- falls through to `dflt`: the same boundary jc_json_get_num_lenient
 * draws (M168), and for the same reason.
 *
 * WHY (M519, measured): humans write `1` for true in a config file, and the
 * strict accessor read that as "wrong type" and returned `dflt`. Two silent
 * inversions followed, both found by pointing jichi at its own example configs:
 *
 *   1. `"pathFence": 1` in FIFTEEN shipped examples/ configs. The key's
 *      presence check fired, so path_fence was set to the dflt 0 -- turning the
 *      fence OFF, overriding the -1 tri-state that means "on in autonomous
 *      postures". A config that reads as fencing was unfencing.
 *   2. The seven config keys whose default is 1 (wisdom, fuzzyEdit, ...): a
 *      written `0` fell back to 1, so the feature could not be switched off.
 *
 * Use this for values a HUMAN or a FOREIGN PROGRAM writes -- config files,
 * another agent's config being converted, an MCP server's `isError` (where a
 * numeric 1 otherwise reports a failed tool call as a success). Wire data from
 * our own providers stays on the strict accessor: there, a non-bool is a bug to
 * see, not a dialect to forgive. */
int         jc_json_get_bool_lenient(const cJSON *o, const char *key, int dflt);

/* Like jc_json_get_num but also accepts a NUMERIC STRING ("200", "200.0"), which
 * is what a model sends when it gets the JSON type wrong (M168). Prose such as
 * "200 lines" is NOT a number and falls through to `dflt` -- guessing at prose
 * would be the silent misreading this exists to prevent.
 *
 * Shared (M287) so the tool layer's tu_arg_int and the compaction layer's
 * read-identity keying cannot disagree about what `{"limit": "100.0"}` means.
 * Two readers of one quantity, each with its own parser, is the drift that cost
 * M286 (see docs/ANECDOTES.md #32). */
double      jc_json_get_num_lenient(const cJSON *o, const char *key,
                                    double dflt);
/* Untrusted-JSON double -> integer clamps (A-02, M1077).
 *
 * Feed these the value a JSON number read returned -- jc_json_get_num, or a
 * cJSON node's ->valuedouble -- when the target is an integer type the sender
 * might have exceeded. Casting such a double straight to int/long/unsigned is
 * undefined behaviour outside the target's range (UBSan: "1e+10 is outside the
 * range of representable values of type 'int'"), and the double comes from a
 * model or a server -- untrusted. Each returns the double clamped to the
 * target's range:
 *
 *   NaN            -> 0
 *   +infinity      -> the type's maximum
 *   -infinity      -> the type's minimum (0 for the unsigned pair)
 *   in-range       -> passed EXACTLY (integral doubles at 2^53 and below are
 *                     exact, so a number the sender could write is undistorted)
 *
 * The shape is cJSON's number_to_int (src/json/cJSON.c:282), extended to the
 * other widths. There is deliberately NO node-taking key variant: a caller that
 * has the key does jc_json_get_num(o, key, dflt) and clamps the double it gets
 * back -- the sweep (M1077, part 2) does exactly that at every site.
 */
int          jc_json_clamp_int(double val);
long         jc_json_clamp_long(double val);
unsigned int jc_json_clamp_uint(double val);
jc_size      jc_json_clamp_size(double val);

cJSON      *jc_json_get_obj(const cJSON *o, const char *key);

/* Like jc_json_get_str but the result is copied into `a` (so it survives the
 * cJSON tree being freed). Returns NULL if missing. */
char *jc_json_dup_str(const cJSON *o, const char *key, struct jc_arena *a);

/* Compact serialisation into a malloc'd buffer (free with free()). */
char *jc_json_print(const cJSON *o);

#ifdef __cplusplus
}
#endif
#endif /* JC_JSON_H */
