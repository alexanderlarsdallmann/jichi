/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_diskguard.c - the disk-space fence (see jc_diskguard.h). */

#include "jc_diskguard.h"
#include "jc_snprintf.h"

#include <string.h>

void jc_diskguard_init(struct jc_diskguard *g, double floor_mb,
                       double floor_files)
{
    if (g == NULL) {
        return;
    }
    memset(g, 0, sizeof(*g));
    g->floor_mb = floor_mb;
    g->floor_files = floor_files;
    g->last_ms = -1.0;
}

int jc_diskguard_below(double free_mb, double free_files,
                       double floor_mb, double floor_files)
{
    /* UNKNOWN (a negative reading) never trips; a floor <= 0 is disabled. */
    if (floor_mb > 0.0 && free_mb >= 0.0 && free_mb < floor_mb) {
        return 1;
    }
    if (floor_files > 0.0 && free_files >= 0.0 && free_files < floor_files) {
        return 1;
    }
    return 0;
}

int jc_diskguard_due(double last_ms, double now_ms, int force)
{
    if (force) {
        return 1;
    }
    if (last_ms < 0.0) {
        return 1;
    }
    return (now_ms - last_ms) >= JC_DISKGUARD_INTERVAL_MS;
}

int jc_diskguard_check(struct jc_diskguard *g, const char *path, int force,
                       double now_ms)
{
    double mb = -1.0;
    double files = -1.0;

    if (g == NULL) {
        return 0;
    }
    if (g->tripped) {
        return 1;   /* latched */
    }
    if (g->floor_mb <= 0.0 && g->floor_files <= 0.0) {
        return 0;   /* disabled */
    }
    if (!jc_diskguard_due(g->last_ms, now_ms, force)) {
        return 0;
    }
    g->last_ms = now_ms;
    if (jc_disk_space(path, &mb, &files) != JC_OK) {
        return 0;   /* UNKNOWN never stops */
    }
    if (jc_diskguard_below(mb, files, g->floor_mb, g->floor_files)) {
        g->tripped = 1;
        jc_snprintf(g->why, sizeof g->why,
            "disk floor reached: %.0f MiB free (floor %.0f MiB), "
            "%.0f inodes free (floor %.0f)",
            mb, g->floor_mb, files, g->floor_files);
        return 1;
    }
    return 0;
}
