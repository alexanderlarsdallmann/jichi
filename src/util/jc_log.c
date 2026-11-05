/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_log.c - leveled logging (see jc_log.h). */

#include "jc_log.h"
#include "jc_snprintf.h"
#include "jc_fault.h"   /* JC_FAULT_HIT: the M1137 (C-19) redaction-failure probe */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
/* M926 (R05-F1): the registry copies secrets (process-lifetime). */
#include <stdlib.h>
#include <time.h>

static int g_level = JC_LOG_WARN;
static char g_prefix[96];   /* M864: a parallel child's id, or "" */

/* ----- secret-redaction registry (defined early so jc_logf can consult it) - */
#define JC_REDACT_MAX 32
static const char *g_secrets[JC_REDACT_MAX];
static int         g_nsecrets;

void jc_log_set_level(int level)
{
    g_level = level;
}

int jc_log_get_level(void)
{
    return g_level;
}

void jc_log_set_prefix(const char *id)
{
    jc_snprintf(g_prefix, sizeof g_prefix, "%s", (id != NULL) ? id : "");
}

void jc_logf(int level, const char *fmt, ...)
{
    va_list ap;
    const char *tag;
    char head[160];
    char msg[4096];
    char line[4400];
    int need;

    if (level < g_level) {
        return;
    }
    switch (level) {
        case JC_LOG_DEBUG: tag = "debug"; break;
        case JC_LOG_INFO:  tag = "info";  break;
        case JC_LOG_WARN:  tag = "warn";  break;
        case JC_LOG_ERROR: tag = "error"; break;
        default:           tag = "log";   break;
    }
    /* M864: in a parallel child, the UTC time and the child's id first. */
    if (g_prefix[0] != '\0') {
        time_t now = time(NULL);
        struct tm *t = gmtime(&now);
        char ts[32];
        if (t == NULL || strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", t) == 0) {
            jc_snprintf(ts, sizeof ts, "%ld", (long)now);
        }
        jc_snprintf(head, sizeof head, "%s %s [jichi %s] ", ts, g_prefix, tag);
    } else {
        jc_snprintf(head, sizeof head, "[jichi %s] ", tag);
    }
    /* M864: the whole line is formatted first and written with ONE call, so the
     * lines of children sharing one stderr do not interleave mid-line (before,
     * the tag, the message and the newline were three writes). */
    va_start(ap, fmt);
    need = jc_vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (g_nsecrets > 0) {
        /* A secret is registered: scrub the formatted text before it is
         * printed, so a diagnostic that interpolates a key can't leak it. */
        char red[4096];
        if (need >= 0 && (jc_size)need < sizeof msg) {
            /* M1131 (C-13): the message FITS the 4096-byte buffer, so msg is
             * the full text and redacting it is byte-identical to today. */
            jc_redact_secrets(msg, red, sizeof(red));
        } else {
            /* M1131 (C-13): the message does NOT fit -- it was cut at 4095. A
             * registered secret straddling that cut leaves its near half
             * unmatched (redaction is an exact strncmp over the WHOLE secret)
             * and printed. Re-format into a heap buffer of exactly the needed
             * size and redact the FULL text so the secret matches in whole.
             * C89 has no va_copy, but the arguments may be walked again from
             * va_start (branch 3 below already does this). */
            char *heaptxt;
            heaptxt = (char *)malloc((size_t)need + 1);
            if (heaptxt == NULL) {
                /* OOM: best effort -- redact the cut msg as before. */
                jc_redact_secrets(msg, red, sizeof(red));
            } else {
                va_start(ap, fmt);
                (void)jc_vsnprintf(heaptxt, (jc_size)need + 1, fmt, ap);
                va_end(ap);
                jc_logf_redact_full(heaptxt, red, sizeof red);
                free(heaptxt);
            }
        }
        jc_snprintf(line, sizeof line, "%s%s\n", head, red);
        fputs(line, stderr);
    } else if (need >= 0 && (jc_size)need < sizeof msg) {
        jc_snprintf(line, sizeof line, "%s%s\n", head, msg);
        fputs(line, stderr);
    } else {
        /* Longer than the buffer (a debug dump of a request body): in pieces,
         * as before, so nothing is cut. C89 has no va_copy, but the arguments
         * may be walked again from va_start. */
        fputs(head, stderr);
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
    }
}

/* ----- secret redaction ------------------------------------------------- */

int jc_redact_apply(const char *const *secrets, int nsecrets,
                    const char *in, char *out, jc_size cap)
{
    jc_size o = 0;
    int hits = 0;
    const char *p;

    if (out == NULL || cap == 0) {
        return 0;
    }
    if (in == NULL) {
        out[0] = '\0';
        return 0;
    }
    for (p = in; *p != '\0'; ) {
        int matched = 0;
        int i;
        for (i = 0; i < nsecrets; i++) {
            const char *sec = secrets[i];
            size_t slen;
            if (sec == NULL) {
                continue;
            }
            slen = strlen(sec);
            if (slen < (size_t)JC_REDACT_MIN) {
                continue;
            }
            if (strncmp(p, sec, slen) == 0) {
                /* Emit "***" (bounded). */
                int k;
                for (k = 0; k < 3 && o + 1 < cap; k++) {
                    out[o++] = '*';
                }
                p += slen;
                hits++;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            if (o + 1 < cap) {
                out[o++] = *p;
            }
            p++;
        }
    }
    out[o] = '\0';
    return hits;
}

void jc_redact_register(const char *secret)
{
    char *cp;
    int i;

    /* NULL / too-short -> no-op (pre-M1040 behaviour; the production caller
     * passes real pointers; NULL-clearing wipes the registry, which we must
     * not do). */
    if (secret == NULL) {
        return;
    }

    if (strlen(secret) < (size_t)JC_REDACT_MIN) {
        return;
    }
    for (i = 0; i < g_nsecrets; i++) {
        if (g_secrets[i] != NULL && strcmp(g_secrets[i], secret) == 0) {
            return; /* already registered */
        }
    }
    if (g_nsecrets >= JC_REDACT_MAX) {
        return; /* full: drop this secret (parity with the pre-M926 silent overflow) */
    }
    /* M926 (R05-F1): COPY, do not store the caller's pointer. The registry's
     * own contract (jc_log.h) says the stored string must outlive the
     * process, but the @ref alias site hands us a per-arena pointer and the
     * arena is reset every turn (jc_agent.c, agent_depth == 0): the registry
     * held a dangling pointer, and every redaction from the second turn on
     * read freed memory (UAF) while the newest secret was never scrubbed. */
    cp = (char *)malloc(strlen(secret) + 1);
    if (cp == NULL) {
        return;
    }
    strcpy(cp, secret);
    g_secrets[g_nsecrets++] = cp;
}

/* jc_redact_reset() -- test isolation only; production never calls it.
 * Frees every registered secret copy and sets g_nsecrets = 0. */
void jc_redact_reset(void)
{
    int i;
    for (i = 0; i < g_nsecrets; i++) {
        if (g_secrets[i] != NULL) {
            free((void *)g_secrets[i]);
            g_secrets[i] = NULL;
        }
    }
    g_nsecrets = 0;
}

int jc_redact_secrets(const char *in, char *out, jc_size cap)
{
    return jc_redact_apply(g_secrets, g_nsecrets, in, out, cap);
}

/* M1137 (C-19): the shared shape of the six malloc-fails-open redaction sinks
 * (jc_toolout_preserve, jc_toolout_dump_request, jc_eventlog_add_text,
 * jc_session_save, hl_emit, relay_write_baton). Each of those used to do
 * `red = malloc(len+1); if (red != NULL) { jc_redact_secrets(...); swap }`
 * and, when the allocation failed, FALL THROUGH with the UNREDACTED buffer --
 * which then reached disk or stdout. This helper lifts that idiom into one
 * place that fails CLOSED: it returns a malloc'd NUL-terminated redacted copy
 * (the caller owns and frees it), or NULL meaning 'degrade'.
 *
 * It returns NULL in two cases, and the CALLER is what decides what 'degrade'
 * means at its own site (a dropped line, a refused save, a discarded spill):
 *   - the JC_FAULT_REDACT probe is armed (JICHI_FAULT_REDACT_AFTER=n): a
 *     deterministic stand-in for an OOM, so the sinks' fail-closed arms are
 *     testable without actually exhausting a small board's memory;
 *   - the real malloc(len+1) fails.
 * In a non-FAULT build JC_FAULT_HIT is the constant 0, so the probe call
 * compiles to nothing and only the genuine malloc outcome decides.
 *
 * jc_redact_secrets never lengthens its output (it copies `in` and replaces
 * registered secrets with the shorter "***"), so a redacted copy of a NUL-
 * terminated `in` of length `len` always fits in len+1 bytes. */
char *jc_redact_scrub(const char *in, jc_size len)
{
    char *red;

    if (JC_FAULT_HIT(JC_FAULT_REDACT)) {
        return NULL;
    }
    red = (char *)malloc((size_t)len + 1);
    if (red == NULL) {
        return NULL;
    }
    jc_redact_secrets(in, red, len + 1);
    return red;
}

/* M1131 (C-13): jc_logf's secrets path uses this on the FULL (heap-formatted)
 * message when it exceeds the 4096-byte stack buffer. Redact the entire NUL-
 * terminated `in` into `out` (capacity `out_cap`), NUL-terminating `out` even
 * on a tight cap. `in` and `out` must not be NULL (jc_logf's contract). */
void jc_logf_redact_full(const char *in, char *out, jc_size out_cap)
{
    jc_redact_secrets(in, out, out_cap);
}

int jc_redact_active(void)
{
    return g_nsecrets > 0;
}

jc_size jc_redact_maxlen(void)
{
    int i;
    jc_size m = 0;
    for (i = 0; i < g_nsecrets; i++) {
        if (g_secrets[i] != NULL) {
            jc_size l = (jc_size)strlen(g_secrets[i]);
            if (l > m) {
                m = l;
            }
        }
    }
    return m;
}

/* M1040 (the 2047-copy-boundary leak): on a TRUNCATED capture (cap_len ==
 * cap_total - 1), replace the captured tail of a registered secret (a strict
 * prefix of the secret) with "***". The function hardens the helper:
 *
 * (a) TRUNCATION GATE: act ONLY when cap_len == cap_total - 1 (the capture
 *     is full; a non-truncated capture never cuts, killing over-redaction).
 * (b) MINIMUM CUT: the matched tail length must be >= JC_REDACT_MIN (6);
 *     shorter tails do not fire (the "down to 1 byte" over-redaction case).
 * (c) NEW LENGTH + ZEROING: on a cut at position S = cap_len - tail_len,
 *     write min(3, tail_len) stars at S, NUL-terminate after the stars,
 *     set *new_len_out = S + stars_written, and ZERO every byte from the
 *     end of the stars to cap_len (the remainder of the captured secret
 *     head that would otherwise sit past the NUL and be read by length-based
 *     consumers like jc_http_body_ctx_overflow(bridge.raw, bridge.raw_len)).
 * On no cut: *new_len_out = cap_len, nothing touched.
 *
 * Algorithm: for each registered secret of length L >= JC_REDACT_MIN,
 * consider T = L-1, L-2, ..., JC_REDACT_MIN (a strict prefix length < L
 * and at least the minimum cut threshold):
 *   tail_len = T (the captured tail portion, a prefix of the secret)
 *   if tail_len <= cap_len and cap[cap_len-tail_len .. cap_len) matches
 *                             secret[0 .. tail_len) (the prefix of the secret)
 *     then overwrite that tail region with "***" and stop.
 * Stop at the first match (longest cut wins). Return 1 on a cut, 0 otherwise.
 *
 * The function rewrites its argument and uses the g_secrets registry. */
int jc_redact_capture_tail(char *cap, jc_size cap_len, jc_size cap_total,
                           jc_size *new_len_out)
{
    int i;

    if (cap == NULL || cap_len == 0) {
        if (new_len_out != NULL) {
            *new_len_out = cap_len;
        }
        return 0;
    }

    /* (a) TRUNCATION GATE: act only when the capture is full.
     * cap_len == cap_total - 1 means the capture buffer is completely filled
     * and the next byte would overflow; only then can a secret be cut. */
    if (cap_len != cap_total - 1) {
        if (new_len_out != NULL) {
            *new_len_out = cap_len;
        }
        return 0;
    }

    if (new_len_out != NULL) {
        *new_len_out = cap_len;  /* default: no cut */
    }

    for (i = 0; i < g_nsecrets; i++) {
        int T;
        const char *sec = g_secrets[i];
        int L;

        if (sec == NULL) {
            continue;
        }
        L = (int)strlen(sec);
        if (L < JC_REDACT_MIN) {
            continue;
        }

        /* For each possible prefix length T from L-1 down to JC_REDACT_MIN:
         *   tail_len = T (the captured tail portion, a prefix of the secret)
         *   We want to match cap[cap_len - tail_len .. cap_len) with sec[0 .. tail_len) */
        if (L <= JC_REDACT_MIN) {
            continue;
        }

        /* T = L-1, L-2, ..., JC_REDACT_MIN (longest tail first, per M1040) */
        for (T = L - 1; T >= JC_REDACT_MIN; T--) {
            int tail_len = T;
            int k;
            int match = 1;
            int S;  /* start position of the tail in the capture */

            if ((jc_size)tail_len > cap_len) {
                continue;
            }

            S = (int)(cap_len - tail_len);

            /* Compare cap[S .. S+tail_len) with sec[0 .. tail_len) */
            for (k = 0; k < tail_len; k++) {
                if (cap[(jc_size)(S + k)] != sec[k]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                int stars = tail_len < 3 ? tail_len : 3;
                int end_of_stars = S + stars;
                int remainder = (int)cap_len - end_of_stars;

                /* (c) write stars, NUL-terminate, set new_len_out */
                for (k = 0; k < stars; k++) {
                    cap[(jc_size)(S + k)] = '*';
                }
                cap[(jc_size)end_of_stars] = '\0';
                if (new_len_out != NULL) {
                    *new_len_out = (jc_size)end_of_stars;
                }

                /* (c) ZERO the remainder of the captured secret head */
                if (remainder > 0) {
                    memset(cap + end_of_stars + 1, 0, (size_t)remainder);
                }
                return 1;  /* cut done */
            }
        }
    }

    return 0;  /* no cut */
}
