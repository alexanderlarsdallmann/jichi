/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_outcome.c - see the header. Pure: no app state, no I/O, no allocation.
 *
 * EVERY SWITCH HERE OMITS `default:` ON PURPOSE. That is the whole mechanism
 * (M688): `-Wall` implies `-Wswitch`, CI builds `-Werror`, so a new
 * `jc_run_stop` value fails the build at each of these sites until somebody
 * decides what it means. A `default:` would silently give it the behaviour of
 * whichever case happened to be written first -- which is how a stop reason
 * comes to be reported on one surface and not another. */
#include "jc_outcome.h"
#include <stddef.h>

void jc_run_outcome_set(struct jc_run_outcome *out, enum jc_run_stop stop,
                        int verifier_concluded)
{
    if (out == NULL) {
        return;
    }
    out->stop = stop;
    out->result_tested = verifier_concluded ? 1 : 0;

    /* Does the answer stand on its own? Read this as "did the model stop
     * because it had finished", not "did anything go wrong" -- VERIFY_FAILED
     * and SCOPE_TAINTED are both complete answers that were then refused, and
     * saying their answer is truncated would be false. */
    switch (stop) {
    case JC_STOP_DONE:
    case JC_STOP_VERIFY_FAILED:
    case JC_STOP_SCOPE_TAINTED:
        out->answer_complete = 1;
        break;
    case JC_STOP_INTERRUPTED:
    case JC_STOP_TIMEOUT:
    case JC_STOP_ERROR:
    case JC_STOP_BUDGET:
    case JC_STOP_MAX_ITERS:
        out->answer_complete = 0;
        break;
    }
}

const char *jc_run_stop_wire(enum jc_run_stop stop)
{
    /* STABLE STRINGS. These are the `stop_reason` values in `--output json`,
     * which docs/EMBEDDING.md lists as a stability tier. They predate this file
     * and are reproduced exactly; the point of moving them here is that they
     * now have one definition instead of a chain of else-ifs in main.c. */
    switch (stop) {
    case JC_STOP_DONE:          return "done";
    case JC_STOP_INTERRUPTED:   return "interrupted";
    case JC_STOP_TIMEOUT:       return "timeout";
    case JC_STOP_ERROR:         return "error";
    case JC_STOP_BUDGET:        return "budget";
    case JC_STOP_VERIFY_FAILED: return "verify_failed";
    case JC_STOP_SCOPE_TAINTED: return "scope_tainted";
    case JC_STOP_MAX_ITERS:     return "max_iters";
    }
    return "done";   /* unreachable; -Wswitch guards the enum */
}

const char *jc_run_stop_clause(enum jc_run_stop stop)
{
    /* Phrased to be embedded after "the turn " or "the run ", and to say what
     * it costs the READER rather than name an internal state. NULL for the
     * clean case so a caller can test it instead of comparing strings. */
    switch (stop) {
    case JC_STOP_DONE:
        return NULL;
    case JC_STOP_INTERRUPTED:
        return "was interrupted";
    case JC_STOP_TIMEOUT:
        return "ran out of wall-clock time";
    case JC_STOP_ERROR:
        return "stopped on an error";
    case JC_STOP_BUDGET:
        return "ran out of budget (tokens, time or tool calls)";
    case JC_STOP_VERIFY_FAILED:
        return "finished, but the verifier was still failing";
    case JC_STOP_SCOPE_TAINTED:
        return "finished, but wrote outside the edit scope first";
    case JC_STOP_MAX_ITERS:
        return "stopped at the tool-call cap";
    }
    return NULL;
}
