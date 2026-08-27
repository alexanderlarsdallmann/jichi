/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_log.c - leveled logging (see jc_log.h). */

#include "jc_log.h"
#include "jc_snprintf.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int g_level = JC_LOG_WARN;

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

void jc_logf(int level, const char *fmt, ...)
{
    va_list ap;
    const char *tag;

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
    fprintf(stderr, "[jichi %s] ", tag);
    va_start(ap, fmt);
    if (g_nsecrets > 0) {
        /* A secret is registered: format into a buffer, scrub it, then print,
         * so a diagnostic that interpolates a key can't leak it. */
        char buf[2048];
        char red[2048];
        jc_vsnprintf(buf, sizeof(buf), fmt, ap);
        jc_redact_secrets(buf, red, sizeof(red));
        fputs(red, stderr);
    } else {
        vfprintf(stderr, fmt, ap);
    }
    va_end(ap);
    fputc('\n', stderr);
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
    int i;
    if (secret == NULL || strlen(secret) < (size_t)JC_REDACT_MIN) {
        return;
    }
    for (i = 0; i < g_nsecrets; i++) {
        if (g_secrets[i] != NULL && strcmp(g_secrets[i], secret) == 0) {
            return; /* already registered */
        }
    }
    if (g_nsecrets < JC_REDACT_MAX) {
        g_secrets[g_nsecrets++] = secret;
    }
}

int jc_redact_secrets(const char *in, char *out, jc_size cap)
{
    return jc_redact_apply(g_secrets, g_nsecrets, in, out, cap);
}

int jc_redact_active(void)
{
    return g_nsecrets > 0;
}
