/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_testparse.h - pure test-output parser.
 *
 * Turns combined stdout+stderr from a test/build run into a structured report:
 * pass/fail counts (when determinable) and a bounded list of failures, each
 * with an optional name, file, line, and message. No I/O, no process, no
 * network -- a pure function over a string, so it is unit-tested offline.
 *
 * Three formats are recognised, detected in order:
 *   - JUnit-XML  (<testsuite>/<testcase>/<failure>) -- emitted by pytest
 *                --junitxml, ctest, and most runners on request.
 *   - TAP        (a "1..N" plan and "ok"/"not ok" lines).
 *   - generic    a heuristic line scan for "<file>:<line>: <message>" and
 *                failure markers (FAIL/FAILED/not ok/Error/assert). The generic
 *                pass also runs as a backstop over the other formats' messages
 *                so a file:line location is never lost.
 */
#ifndef JC_TESTPARSE_H
#define JC_TESTPARSE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_str.h"

/* Cap the failures we retain and the rendered/stored message length so the
 * report and its rendering stay bounded regardless of input size. */
#define JC_TEST_MAX_FAILURES 50
#define JC_TEST_MAX_MSG       512

struct jc_test_failure {
    char *name;     /* test/case name, or NULL                 */
    char *file;     /* source path, or NULL                    */
    long  line;     /* 1-based line, or 0 when unknown          */
    char *message;  /* failure message (truncated), or NULL    */
};

struct jc_test_report {
    struct jc_vec failures;  /* of struct jc_test_failure (owned)              */
    int passed;              /* count, or -1 when not determinable             */
    int failed;              /* count, or -1 when not determinable             */
    int total;               /* count, or -1 when not determinable             */
    int truncated;           /* failures discovered beyond JC_TEST_MAX_FAILURES */
    const char *format;      /* "junit" | "tap" | "generic" (static string)    */
};

/* Initialise an empty report (failed/passed/total = -1). */
void jc_test_report_init(struct jc_test_report *r);

/* Free every owned string and the failures vector. Idempotent. */
void jc_test_report_free(struct jc_test_report *r);

/* Parse `output` into `r` (which must be init'd). Always succeeds; on
 * unrecognised input `r` simply holds no failures and -1 counts. A NULL or
 * empty `output` is treated as no output. */
void jc_testparse(const char *output, struct jc_test_report *r);

/* Best-effort count of tests observed: `total` when known, else `passed`+
 * `failed` when either is known, else -1 (no test signal at all). Pure. Used by
 * the M86 green-gate sanity check to spot a "green" run that ran no tests. */
int jc_test_report_count(const struct jc_test_report *r);

/* Render a compact, bounded summary of `r` into `out`: a count line (omitted
 * when counts are unknown) followed by one bullet per failure
 * ("- <name> @ <file>:<line>: <message>"), then "... and K more" when capped.
 * Returns the number of failure bullets written. */
int jc_testparse_render(const struct jc_test_report *r, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_TESTPARSE_H */
