/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_checkinred.h - the checkin redactor core (M1224, the sed -E port). */

#ifndef JC_HP_CHECKINRED_H
#define JC_HP_CHECKINRED_H

#include <stddef.h>   /* size_t, for the cap parameter */

#ifdef __cplusplus
extern "C" {
#endif

/* jc_checkinred() -- the C89 port of `sed -E 's/[A-Za-z0-9_-]{40,}/<r>/g'`:
 *   ONE left-to-right pass over `in`; a maximal run of >=40 chars from
 *   [A-Za-z0-9_-] is replaced by the 2-char `<r>`; a run <40 is copied
 *   verbatim; a non-class char splits runs; the `<r>` output is never
 *   re-scanned.
 *
 *   Returns 0 on success, -1 if the output does not fit `cap`.
 *
 *   The caller must provide `out` with `cap >= strlen(in) + 1` (the worst
 *   case is no redactions, the output is identical to the input). */
int jc_checkinred(const char *in, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* JC_HP_CHECKINRED_H */
