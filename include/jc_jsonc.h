/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_jsonc.h - strip JSONC extensions so a plain JSON parser can read it.
 *
 * opencode configs are JSONC: they may carry // line comments, block comments,
 * and trailing commas, none of which cJSON accepts. jc_jsonc_strip removes
 * those (string-literal and escape aware, so a "//" or "," inside a string is
 * preserved) and returns plain JSON. It is idempotent on already-clean input,
 * so it is safe to run on every JSON source unconditionally.
 */
#ifndef JC_JSONC_H
#define JC_JSONC_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_mem.h"

/* Copy `src` into arena `a`, removing // and block comments and trailing
 * commas before } or ]. Returns the cleaned NUL-terminated string, or NULL on
 * NULL input / OOM. The result is never longer than `src`. */
char *jc_jsonc_strip(const char *src, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_JSONC_H */
