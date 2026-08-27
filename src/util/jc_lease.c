/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lease.c - the per-workspace run lease (see jc_lease.h). */

#include "jc_lease.h"
#include "jc_snapshot.h"   /* jc_workspace_key: one derivation, two stores */
#include "jc_snprintf.h"
#include "jc_json.h"
#include "jc_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

int jc_lease_mode_parse(const char *s, enum jc_lease_mode *out)
{
    if (s == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(s, "warn") == 0) {
        *out = JC_LEASE_WARN;
        return 1;
    }
    if (strcmp(s, "fail") == 0) {
        *out = JC_LEASE_FAIL;
        return 1;
    }
    if (strcmp(s, "off") == 0) {
        *out = JC_LEASE_OFF;
        return 1;
    }
    return 0;
}

const char *jc_lease_mode_name(enum jc_lease_mode m)
{
    switch (m) {
    case JC_LEASE_WARN: return "warn";
    case JC_LEASE_FAIL: return "fail";
    case JC_LEASE_OFF:  return "off";
    }
    return "?";
}

enum jc_lease_verdict jc_lease_decide(int held, int holder_alive,
                                      enum jc_lease_mode m)
{
    if (m == JC_LEASE_OFF) {
        return JC_LEASE_TAKE;
    }
    if (!held) {
        return JC_LEASE_TAKE;
    }
    if (!holder_alive) {
        /* A crashed or killed run left its file behind. Take it quietly: a stale
         * lease that blocks every later run is the classic lockfile failure, and
         * the operator has no way to tell it from a live one by looking. */
        return JC_LEASE_TAKE;
    }
    return (m == JC_LEASE_FAIL) ? JC_LEASE_REFUSE : JC_LEASE_WARN_TAKE;
}

void jc_lease_path(const char *home, const char *work_tree,
                   char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi.d/leases/%lu.json",
                home != NULL ? home : ".",
                jc_workspace_key(work_tree));
}

char *jc_lease_render(const struct jc_lease_info *in)
{
    cJSON *o;
    char *s;

    if (in == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", 1.0);
    cJSON_AddStringToObject(o, "run", in->run);
    cJSON_AddNumberToObject(o, "pid", (double)in->pid);
    cJSON_AddNumberToObject(o, "started", (double)in->started);
    cJSON_AddStringToObject(o, "mode", in->mode);
    s = jc_json_print(o);
    cJSON_Delete(o);
    return s;
}

int jc_lease_parse(const char *json, struct jc_lease_info *out)
{
    cJSON *o;
    cJSON *it;

    if (json == NULL || out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    o = cJSON_Parse(json);
    if (o == NULL) {
        return 0;
    }
    it = cJSON_GetObjectItem(o, "run");
    if (it != NULL && it->valuestring != NULL) {
        jc_snprintf(out->run, sizeof(out->run), "%s", it->valuestring);
    }
    it = cJSON_GetObjectItem(o, "pid");
    if (it != NULL) {
        out->pid = (long)it->valuedouble;
    }
    it = cJSON_GetObjectItem(o, "started");
    if (it != NULL) {
        out->started = (long)it->valuedouble;
    }
    it = cJSON_GetObjectItem(o, "mode");
    if (it != NULL && it->valuestring != NULL) {
        jc_snprintf(out->mode, sizeof(out->mode), "%s", it->valuestring);
    }
    cJSON_Delete(o);
    /* A record with no pid names nobody, so it cannot be checked for liveness and
     * must not be trusted as a holder. */
    return (out->pid > 0) ? 1 : 0;
}

int jc_lease_pid_alive(long pid)
{
    if (pid <= 0) {
        return 0;
    }
    if (kill((int)pid, 0) == 0) {
        return 1;
    }
    /* EPERM: it exists and belongs to someone else. Still a live holder. */
    return (errno == EPERM) ? 1 : 0;
}

/* Read a lease file, if any. Returns 1 when a usable record was parsed.
 *
 * Plain stdio into a fixed buffer, NOT jc_read_file: that one allocates from an
 * arena and this module has none to offer -- passing NULL segfaulted inside
 * jc_arena_alloc, found by running the binary rather than by reading the header.
 * A lease record is bounded by construction (a uuid, a pid, a timestamp, a mode
 * word), so anything larger than this buffer is not a record jichi wrote and is
 * treated as "no lease" -- which is also the right answer for a truncated file:
 * it must not wedge every later run. */
static int lease_read(const char *path, struct jc_lease_info *out)
{
    char buf[512];
    FILE *f;
    size_t n;

    f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    if (n == 0) {
        return 0;
    }
    return jc_lease_parse(buf, out);
}

enum jc_lease_verdict jc_lease_acquire(const char *home, const char *work_tree,
                                       const struct jc_lease_info *me,
                                       enum jc_lease_mode mode,
                                       struct jc_lease_info *holder)
{
    char path[1200];
    char dir[1100];
    struct jc_lease_info cur;
    int held;
    int alive = 0;
    enum jc_lease_verdict v;
    char *text;

    if (holder != NULL) {
        memset(holder, 0, sizeof(*holder));
    }
    if (mode == JC_LEASE_OFF || me == NULL) {
        return JC_LEASE_TAKE;
    }

    jc_lease_path(home, work_tree, path, sizeof(path));
    memset(&cur, 0, sizeof(cur));
    held = lease_read(path, &cur);
    if (held) {
        alive = jc_lease_pid_alive(cur.pid);
        if (holder != NULL) {
            *holder = cur;
        }
    }
    v = jc_lease_decide(held, alive, mode);
    if (v == JC_LEASE_REFUSE) {
        return v;
    }

    jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/leases",
                home != NULL ? home : ".");
    jc_mkdir_p(dir);
    text = jc_lease_render(me);
    if (text != NULL) {
        /* Atomic + 0600, like every other private sink jichi writes (M146): a
         * half-written lease read by a sibling would name a garbage pid. */
        if (jc_write_file_atomic(path, text, (jc_size)strlen(text)) != JC_OK) {
            jc_logf(JC_LOG_WARN,
                    "lease: could not write %s -- continuing without a lease "
                    "(concurrent runs on this workspace will not be detected)",
                    path);
        }
        free(text);
    }
    return v;
}

void jc_lease_release(const char *home, const char *work_tree,
                      const char *my_run)
{
    char path[1200];
    struct jc_lease_info cur;

    if (my_run == NULL || my_run[0] == '\0') {
        return;
    }
    jc_lease_path(home, work_tree, path, sizeof(path));
    memset(&cur, 0, sizeof(cur));
    if (!lease_read(path, &cur)) {
        return;
    }
    /* ONLY ours. A run that warned and proceeded past someone else's lease must
     * not delete it on the way out -- that would hand the tree to a third run
     * while the second is still working in it. */
    if (strcmp(cur.run, my_run) == 0) {
        remove(path); /* C89 stdio; the lease is a plain file */
    }
}
