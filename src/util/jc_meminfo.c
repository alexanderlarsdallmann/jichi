/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_meminfo.c - self-RSS reader (M180). See jc_meminfo.h. */

#include "jc_meminfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Find "<key>" at line start and parse its "<n> kB" value. Pure. */
static long field_kb(const char *text, const char *key, int *found)
{
    const char *p = text;
    size_t klen = strlen(key);

    *found = 0;
    while (p != NULL && *p != '\0') {
        if (strncmp(p, key, klen) == 0) {
            const char *v = p + klen;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            *found = 1;
            return strtol(v, NULL, 10);
        }
        p = strchr(p, '\n');
        if (p != NULL) {
            p++;
        }
    }
    return 0;
}

int jc_meminfo_parse(const char *status_text, long *rss_kb, long *hwm_kb)
{
    int rss_found = 0;
    int hwm_found = 0;
    long rss;
    long hwm;

    if (rss_kb != NULL) {
        *rss_kb = 0;
    }
    if (hwm_kb != NULL) {
        *hwm_kb = 0;
    }
    if (status_text == NULL) {
        return 0;
    }
    rss = field_kb(status_text, "VmRSS:", &rss_found);
    hwm = field_kb(status_text, "VmHWM:", &hwm_found);
    if (rss_kb != NULL && rss_found) {
        *rss_kb = rss;
    }
    if (hwm_kb != NULL && hwm_found) {
        *hwm_kb = hwm;
    }
    return rss_found;
}

int jc_meminfo_self(long *rss_kb, long *hwm_kb)
{
    char buf[4096];
    size_t n;
    FILE *f;

    if (rss_kb != NULL) {
        *rss_kb = 0;
    }
    if (hwm_kb != NULL) {
        *hwm_kb = 0;
    }
    f = fopen("/proc/self/status", "r");
    if (f == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return jc_meminfo_parse(buf, rss_kb, hwm_kb);
}
