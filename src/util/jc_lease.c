/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lease.c - the per-workspace run lease (see jc_lease.h). */

#include "jc_lease.h"
#include "jc_snapshot.h"   /* jc_workspace_key: one derivation, two stores */
#include "jc_snprintf.h"
#include "jc_json.h"
#include "jc_log.h"
#include "jc_fault.h"   /* M760: the take's window, held open in FAULT builds */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>

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

void jc_lease_path(const char *home, const char *work_tree, const char *run,
                   char *buf, jc_size cap)
{
    char key[65];
    jc_workspace_key(work_tree, key, sizeof(key));
    if (run != NULL && run[0] != '\0') {
        /* M1101 (D-17): one record per holder. The run id goes into the name,
         * so a warn-take writes <key>.<my run>.json BESIDE the live holder's
         * file instead of over it, and a release can unlink only its own. */
        jc_snprintf(buf, cap, "%s/.jichi.d/leases/%s.%s.json",
                    home != NULL ? home : ".", key, run);
    } else {
        /* Legacy bare name: a run id that is NULL or empty yields <key>.json,
         * the pre-D-17 shape the scan still reads. */
        jc_snprintf(buf, cap, "%s/.jichi.d/leases/%s.json",
                    home != NULL ? home : ".", key);
    }
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
    if (in->root[0] != '\0') {
        cJSON_AddStringToObject(o, "root", in->root);   /* M759 */
    }
    if (in->control[0] != '\0') {
        cJSON_AddStringToObject(o, "control", in->control);   /* M759 */
    }
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
        out->pid = jc_json_clamp_long(it->valuedouble);
    }
    it = cJSON_GetObjectItem(o, "started");
    if (it != NULL) {
        out->started = jc_json_clamp_long(it->valuedouble);
    }
    it = cJSON_GetObjectItem(o, "mode");
    if (it != NULL && it->valuestring != NULL) {
        jc_snprintf(out->mode, sizeof(out->mode), "%s", it->valuestring);
    }
    it = cJSON_GetObjectItem(o, "root");   /* M759 */
    if (it != NULL && it->valuestring != NULL) {
        jc_snprintf(out->root, sizeof(out->root), "%s", it->valuestring);
    }
    it = cJSON_GetObjectItem(o, "control");   /* M759 */
    if (it != NULL && it->valuestring != NULL) {
        jc_snprintf(out->control, sizeof(out->control), "%s", it->valuestring);
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
/* M760: the lock that makes a take one step, beside the record it guards:
 * <key>.json -> <key>.lock. Derived from the lease path, so the key has one
 * derivation. Empty, and kept (jc_platform.h says why). */
static void lease_lock_path(const char *lease, char *buf, jc_size cap)
{
    jc_size n;

    jc_snprintf(buf, cap, "%s", lease);
    n = strlen(buf);
    if (n > 5 && strcmp(buf + n - 5, ".json") == 0) {
        buf[n - 5] = '\0';
    }
    n = strlen(buf);
    jc_snprintf(buf + n, cap - n, "%s", ".lock");
}

/* Long enough for any take (microseconds, or a FAULT pause); short enough that a
 * holder stopped inside one costs the next run seconds, not the run. */
#define JC_LEASE_LOCK_WAIT_MS 5000L

static int lease_lock(const char *lease, const char *why)
{
    char lpath[1200];
    int lk;

    lease_lock_path(lease, lpath, sizeof(lpath));
    lk = jc_lock_take(lpath, JC_LEASE_LOCK_WAIT_MS);
    if (lk == JC_LOCK_BUSY) {
        jc_logf(JC_LOG_WARN,
                "lease: another process has held %s for more than %ld s -- "
                "%s without it, so a run starting at this moment may not be "
                "seen", lpath, JC_LEASE_LOCK_WAIT_MS / 1000L, why);
    } else if (lk == JC_LOCK_NONE) {
        jc_logf(JC_LOG_DEBUG, "lease: no lock on %s here -- %s unlocked",
                lpath, why);
    }
    return lk;
}

static int lease_read(const char *path, struct jc_lease_info *out)
{
    char buf[4096];   /* M759: two paths now, each up to 1 KiB */
    FILE *f;
    size_t n;

    f = fopen(path, "rb");
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
    char path[1200];    /* the legacy name: the take's lock is derived from it */
    char my_path[1200]; /* our own record, M1101 (D-17) */
    char dir[1100];
    struct jc_lease_info most;
    int n_live = 0;
    int n_stale = 0;
    int held;
    int alive = 0;
    int lk;
    enum jc_lease_verdict v;
    char *text;

    if (holder != NULL) {
        memset(holder, 0, sizeof(*holder));
    }
    if (mode == JC_LEASE_OFF || me == NULL) {
        return JC_LEASE_TAKE;
    }

    jc_lease_path(home, work_tree, NULL, path, sizeof(path));
    jc_lease_path(home, work_tree, me->run, my_path, sizeof(my_path));
    jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/leases",
                home != NULL ? home : ".");
    jc_mkdir_p(dir);
    /* M760: read, decide and write as ONE step. Taken read-then-write, two runs
     * started together both read "free" and both took the lease quietly, so
     * `--lease fail` let both through and `--lease warn` warned neither. Under
     * the lock the second reads the first's record. Without one (JC_LOCK_NONE,
     * or a holder stuck past the wait) the take proceeds as it did before: a
     * diagnostic must not become an outage. */
    lk = lease_lock(path, "taking the lease");
    /* M1101 (D-17): the decision is a SCAN of this workspace's records, not a
     * read of one file: one record per holder, so a warn-take writes its OWN
     * file beside the live holder's instead of over it, and any LIVE record
     * means the tree is held. Stale records are counted but left on disk (jichi
     * ps keeps them as evidence) and, alone, mean "take quietly". */
    (void)jc_lease_scan(home, work_tree, &most, &n_live, &n_stale);
    held = (n_live > 0 || n_stale > 0);
    if (n_live > 0) {
        alive = 1;
        if (holder != NULL) {
            *holder = most;   /* the most recently started live holder */
        }
    }
    v = jc_lease_decide(held, alive, mode);
    if (v == JC_LEASE_REFUSE) {
        jc_lock_release(lk);
        return v;
    }
    jc_fault_lease_pause();   /* M760: the window, held open in FAULT builds */

    text = jc_lease_render(me);
    if (text != NULL) {
        /* Atomic + 0600, like every other private sink jichi writes (M146): a
         * half-written lease read by a sibling would name a garbage pid.
         * M1101 (D-17): to OUR OWN name, never over another holder's file. */
        if (jc_write_file_atomic(my_path, text, (jc_size)strlen(text))
                != JC_OK) {
            jc_logf(JC_LOG_WARN,
                    "lease: could not write %s -- continuing without a lease "
                    "(concurrent runs on this workspace will not be detected)",
                    my_path);
        }
        free(text);
    }
    jc_lock_release(lk);
    return v;
}

void jc_lease_release(const char *home, const char *work_tree,
                      const char *my_run)
{
    char path[1200];    /* the legacy name: the take's lock is derived from it */
    char my_path[1200]; /* OUR record, M1101 (D-17) */
    int lk;

    if (my_run == NULL || my_run[0] == '\0') {
        return;
    }
    jc_lease_path(home, work_tree, NULL, path, sizeof(path));
    jc_lease_path(home, work_tree, my_run, my_path, sizeof(my_path));
    /* M760: under the take's lock too, or a run could read its own record,
     * lose the processor while a sibling warns and takes over, and then remove
     * the sibling's record -- the tree would look free while it is worked in. */
    lk = lease_lock(path, "releasing the lease");
    /* M1101 (D-17): the NAME is the identity. <key>.<my run>.json can only be
     * our record, so removing it can never touch a sibling's -- no re-read and
     * match, and a sibling that warned and proceeded past us keeps its file.
     * (A same-run-id collision is us re-releasing; run ids are uuids.) */
    remove(my_path); /* C89 stdio; the lease is a plain file */
    jc_lock_release(lk);
}

/* M1101 (D-17): the scan acquire's decision is built from. Reads this
 * workspace's records straight off the directory:
 *
 *   <key>.json           -- the legacy bare name (a hand-planted or old-format
 *                           record; the smoke drivers write it)
 *   <key>.<...>.json     -- one file per holder, M1101
 *
 * A name matches this workspace when it is exactly the legacy name or begins
 * with "<key>." and ends with ".json". The take's <key>.lock and every other
 * workspace's files are skipped. Each matching record is parsed and checked for
 * liveness: a live pid counts *n_live and may become *most_live; a dead one
 * counts *n_stale. STALE RECORDS ARE LEFT ON DISK -- jichi ps counts them as
 * evidence of how a run ended and removes nothing (its M759 comment), and this
 * scan is read-only for the same reason.
 *
 * `most_live` is the most recently started live record -- the holder a warning
 * or refusal should name -- with ties (equal started) broken by the
 * lexicographically newest NAME, so the choice is deterministic.
 */
int jc_lease_scan(const char *home, const char *work_tree,
                  struct jc_lease_info *most_live, int *n_live, int *n_stale)
{
    char dir[1100];
    char legacy[1200];
    char prefix[200];
    const char *base;
    size_t prefix_len;
    DIR *d;
    struct dirent *ent;
    char best_name[256];   /* the name of the current *most_live, for ties */

    if (home == NULL) {
        return 0;
    }
    if (most_live != NULL) {
        memset(most_live, 0, sizeof(*most_live));
    }
    if (n_live != NULL) {
        *n_live = 0;
    }
    if (n_stale != NULL) {
        *n_stale = 0;
    }

    jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/leases", home);
    d = opendir(dir);
    if (d == NULL) {
        return 0;
    }

    /* The two shapes a record for THIS workspace can have, one derivation.
     * The workspace KEY is the common part:
     *   <key>.json           -- the legacy bare name
     *   <key>.<run>.json     -- one file per holder, M1101
     * So a name belongs to this workspace when it equals "<key>.json" or
     * begins with "<key>." and ends with ".json" (with a non-empty run between
     * the dots). The take's <key>.lock and every other workspace's files are
     * not this workspace's lease and are skipped. */
    jc_lease_path(home, work_tree, NULL, legacy, sizeof(legacy));
    base = strrchr(legacy, '/');
    base = (base != NULL) ? base + 1 : legacy;   /* "<key>.json" */
    jc_snprintf(prefix, sizeof(prefix), "%s", base);
    if (strlen(prefix) > 5 && strcmp(prefix + strlen(prefix) - 5, ".json") == 0) {
        prefix[strlen(prefix) - 5] = '\0';       /* "<key>" */
    }
    jc_snprintf(prefix + strlen(prefix), sizeof(prefix) - strlen(prefix),
                "%s", ".");                       /* "<key>." */
    prefix_len = strlen(prefix);
    best_name[0] = '\0';

    while ((ent = readdir(d)) != NULL) {
        const char *nm = ent->d_name;
        size_t len = strlen(nm);
        char path[1400];
        struct jc_lease_info li;
        int alive;
        int take;

        if (strcmp(nm, base) == 0) {
            take = 1;   /* the legacy bare name */
        } else if (len >= prefix_len + 6 &&
                   strncmp(nm, prefix, prefix_len) == 0 &&
                   strcmp(nm + len - 5, ".json") == 0) {
            take = 1;   /* <key>.<run>.json, run non-empty */
        } else {
            take = 0;
        }
        if (!take) {
            continue;
        }

        jc_snprintf(path, sizeof(path), "%s/%s", dir, nm);
        memset(&li, 0, sizeof(li));
        if (!lease_read(path, &li)) {
            continue;   /* unreadable or malformed: not a record jichi wrote */
        }
        alive = jc_lease_pid_alive(li.pid);
        if (!alive) {
            if (n_stale != NULL) {
                *n_stale += 1;
            }
            continue;   /* taken-over material: counted, left on disk */
        }
        if (n_live != NULL) {
            *n_live += 1;
        }
        if (most_live == NULL) {
            continue;
        }
        /* most recently started wins; on a tie the lexicographically newest
         * NAME, so the choice is deterministic independent of readdir order. */
        if (*n_live == 1) {
            *most_live = li;
            jc_snprintf(best_name, sizeof(best_name), "%s", nm);
        } else {
            int newer;
            if (li.started > most_live->started) {
                newer = 1;
            } else if (li.started < most_live->started) {
                newer = 0;
            } else {
                newer = (strcmp(nm, best_name) > 0);
            }
            if (newer) {
                *most_live = li;
                jc_snprintf(best_name, sizeof(best_name), "%s", nm);
            }
        }
    }
    closedir(d);
    return 1;
}
