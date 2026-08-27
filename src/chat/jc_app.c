/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_app.c - jc_app helper functions (see jc_app.h). */

#include "jc_app.h"
#include "jc_platform.h"
#include "jc_toolout.h"
#include "jc_provider.h"
#include "jc_perm.h"
#include "jc_envelope.h"
#include "jc_net.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_utf8.h"
#include "jc_snprintf.h"
#include "jc_path.h"
#include "jc_image.h"
#include "jc_base64.h"
#include "jc_message.h"
#include "jc_confbench.h"
#include "jc_proc.h"
#include "jc_agent.h"
#include "jc_log.h"
#include "jc_telemetry.h"
#include "jc_eventlog.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>

#define JC_REACH_MAX 64 /* model-count cap for the reachability cache */

void jc_app_set_mode(struct jc_app *app, int mode)
{
    app->mode = mode;
    switch (mode) {
    case JC_MODE_PLAN:
        app->readonly = 1;
        app->auto_approve = 0;
        break;
    case JC_MODE_AUTO:
        app->readonly = 0;
        app->auto_approve = 1;
        break;
    case JC_MODE_CHAT:
    default:
        app->readonly = 0;
        app->auto_approve = 0;
        break;
    }
}

const struct jc_agentdef *jc_app_command_agent_apply(
    struct jc_app *app, const char *name, struct jc_command_agent_save *save)
{
    const struct jc_agentdef *def;

    save->prev_persona = app->persona_override;
    save->prev_style = app->style_override;
    save->prev_readonly = app->readonly;
    save->applied = 0;
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    def = jc_agentdef_find(&app->agents, name);
    if (def == NULL) {
        return NULL; /* unknown profile: caller runs the command unmodified */
    }
    /* Make the profile authoritative for this turn: its body replaces the base
     * persona (see jc_sysmsg_build), and a readonly profile tightens the turn.
     * The profile's model/tools fence are a subagent-only concern; a command
     * runs in the current turn with the current model and permission posture
     * (plus this readonly tightening). */
    if (def->system_prompt != NULL && def->system_prompt[0] != '\0') {
        app->persona_override = def->system_prompt;
    }
    /* M302: the profile's tone, by NAME, resolved against the session's output
     * styles. Applied alongside the persona so "this reviewer is blunt" is
     * configured once and shared, rather than restated in every profile body. */
    if (def->style != NULL && def->style[0] != '\0') {
        app->style_override = def->style;
    }
    if (def->has_readonly && def->readonly) {
        app->readonly = 1;
    }
    save->applied = 1;
    return def;
}

void jc_app_command_agent_restore(struct jc_app *app,
                                  const struct jc_command_agent_save *save)
{
    if (!save->applied) {
        return;
    }
    app->persona_override = save->prev_persona;
    app->style_override = save->prev_style;   /* M302 */
    app->readonly = save->prev_readonly;
}

void jc_app_command_model_apply(struct jc_app *app, const char *selector,
                                struct jc_command_model_save *save)
{
    int idx;
    save->prev_idx = app->config.active;
    save->switched = 0;
    if (selector == NULL || selector[0] == '\0') {
        return;
    }
    idx = jc_config_find_model(&app->config, selector);
    if (idx < 0) {
        return; /* unknown selector: graceful no-op */
    }
    /* The selector RESOLVED, so the author pinned a model for this turn: suppress
     * turn-start routing, which would otherwise replace it with the fast tier and
     * make the declaration a no-op. Set even when no switch is needed -- naming
     * the already-active model is still a pin, and routing would still move off it. */
    app->model_pinned = 1;
    if (idx == app->config.active) {
        return; /* already active: nothing to switch or restore */
    }
    if (jc_app_switch_model(app, idx) == JC_OK) {
        save->switched = 1;
    }
}

void jc_app_command_model_restore(struct jc_app *app,
                                  const struct jc_command_model_save *save)
{
    app->model_pinned = 0;
    if (save->switched) {
        jc_app_switch_model(app, save->prev_idx);
    }
}

void jc_app_mark_read(struct jc_app *app, const char *path)
{
    char *copy;
    if (jc_app_was_read(app, path)) {
        return;
    }
    copy = jc_arena_strdup(app->arena, path);
    if (copy != NULL) {
        jc_vec_push(&app->read_files, &copy);
    }
}

int jc_app_reread_check(struct jc_app *app, const char *path,
                        const char *data, jc_size len,
                        long offset, long limit)
{
    unsigned long hash;
    jc_size i;
    struct jc_read_rec nrec;
    char *copy = NULL;

    if (app == NULL || path == NULL) {
        return 0;
    }
    hash = jc_reread_hash(data, (unsigned long)len);
    for (i = 0; i < app->read_recs.len; i++) {
        struct jc_read_rec *rec =
            (struct jc_read_rec *)jc_vec_at(&app->read_recs, i);
        if (rec->path == NULL || strcmp(rec->path, path) != 0) {
            continue;
        }
        /* Same path: reuse its arena-owned spelling for any new range record
         * below, so paging a file does not strdup the path once per page. */
        if (copy == NULL) {
            copy = rec->path;
        }
        /* M287: only a read of the SAME RANGE can be a redundant re-read. A
         * different offset/limit is the model paging through a large file --
         * different bytes, different information, not waste. */
        if (rec->offset != offset || rec->limit != limit) {
            continue;
        }
        {
            int identical;
            identical = (rec->size == (unsigned long)len && rec->hash == hash);
            rec->size = (unsigned long)len;
            rec->hash = hash;
            if (rec->count < 1000000) { /* saturate; the value is only a tally */
                rec->count++;
            }
            return identical ? 1 : 0;
        }
    }
    /* First read of this (path, range) this session: record it, never nudge. */
    if (app->read_recs.len >= (jc_size)JC_READ_RECS_MAX) {
        return 0;               /* table full: miss the advisory, never fake one */
    }
    if (copy == NULL) {
        copy = jc_arena_strdup(app->arena, path);
        if (copy == NULL) {
            return 0;
        }
    }
    nrec.path = copy;
    nrec.offset = offset;
    nrec.limit = limit;
    nrec.size = (unsigned long)len;
    nrec.hash = hash;
    nrec.count = 1;
    jc_vec_push(&app->read_recs, &nrec);
    return 0;
}

int jc_app_path_fence_on(const struct jc_app *app)
{
    if (app->config.path_fence == 1) {
        return 1;
    }
    if (app->config.path_fence == 0) {
        return 0;
    }
    /* auto (-1): fence the autonomous postures, where there is no human in the
     * loop to catch a stray path. */
    return app->mode == JC_MODE_AUTO || app->auto_approve;
}

int jc_app_path_denied_ex(const struct jc_app *app, const char *path,
                          int for_write)
{
    char resolved[JC_PATH_MAX];
    const struct jc_vec *refs;
    jc_size i;

    if (!jc_app_path_fence_on(app) || app->root[0] == '\0') {
        return 0;
    }
    /* Resolve once; fail closed (deny) if the path can't be canonicalized. */
    if (jc_path_resolve(path, resolved, sizeof(resolved)) != JC_OK) {
        return 1;
    }
    if (jc_path_under_root(app->root, resolved)) {
        return 0;
    }
    /* M339: jichi's own tool-output spill directory is readable. It is outside
     * the workspace by design (a rollback must not delete the evidence of the
     * run it rolled back), so without this the model would be handed a path the
     * fence then refuses. Read side only, one fixed directory -- narrower than a
     * referenceRoot and never a reason to turn the fence off. */
    if (!for_write) {
        char tdbuf[512];
        const char *td = jc_toolout_dir((struct jc_app *)app, tdbuf,
                                        sizeof(tdbuf));
        char canon[JC_PATH_MAX];
        if (td != NULL && jc_path_resolve(td, canon, sizeof(canon)) == JC_OK
                && jc_path_under_root(canon, resolved)) {
            return 0;
        }
    }
    /* Reads may also fall under a configured read-only reference root; writes
     * stay confined to the workspace (M54). */
    refs = &app->config.reference_roots;
    if (!for_write) {
        for (i = 0; i < refs->len; i++) {
            const char *rr = *(char **)jc_vec_at((struct jc_vec *)refs, i);
            char canon[JC_PATH_MAX];
            if (rr != NULL && rr[0] != '\0' &&
                jc_path_resolve(rr, canon, sizeof(canon)) == JC_OK &&
                jc_path_under_root(canon, resolved)) {
                return 0;
            }
        }
    }
    return 1;
}

int jc_app_path_denied(const struct jc_app *app, const char *path)
{
    return jc_app_path_denied_ex(app, path, 1);
}

/* Newest ".jsonl" under ~/.jichi.d/<subdir>/ into `out`. 0 on success, -1 when
 * the directory is missing/empty or holds no .jsonl. */
static int app_newest_jsonl(struct jc_arena *arena, const char *subdir,
                            char *out, jc_size cap)
{
    char dir[1100];
    char best[256];
    struct jc_vec names;
    double best_mt = -1.0;
    jc_size i;

    best[0] = '\0';
    jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/%s", jc_home_dir(), subdir);
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, arena) != JC_OK || names.len == 0) {
        jc_vec_free(&names);
        return -1;
    }
    for (i = 0; i < names.len; i++) {
        const char *n = *(char **)jc_vec_at(&names, i);
        char full[1400];
        jc_size nl = strlen(n);
        double mt;
        if (nl < 6 || strcmp(n + nl - 6, ".jsonl") != 0) {
            continue;
        }
        jc_snprintf(full, sizeof(full), "%s/%s", dir, n);
        mt = jc_file_mtime(full);
        if (mt > best_mt) {
            best_mt = mt;
            jc_snprintf(best, sizeof(best), "%s", n);
        }
    }
    jc_vec_free(&names);
    if (best[0] == '\0') {
        return -1;
    }
    jc_snprintf(out, cap, "%s/%s", dir, best);
    return 0;
}

int jc_app_pick_telemetry_log(const char *ws, struct jc_arena *a,
                              char *out, jc_size cap)
{
    char def[1300];
    jc_telemetry_default_path(jc_home_dir(), ws, def, sizeof(def));
    if (jc_is_regular_file(def)) {
        jc_snprintf(out, cap, "%s", def);
        return 0;
    }
    return app_newest_jsonl(a, "telemetry", out, cap);
}

int jc_app_load_telemetry(struct jc_app *app, struct jc_telemetry_summary *out,
                          char *label, jc_size label_cap)
{
    char path[1300];
    char *text = NULL;
    struct jc_arena *la;

    if (app == NULL || out == NULL) {
        return 0;
    }
    jc_telemetry_summary_init(out);
    /* A LOCAL arena, freed before returning: the log can be megabytes and the
     * TUI can ask for this once per `/context tools`, so app->arena (freed only
     * at process exit) would accumulate a copy per invocation -- the M197/M198/
     * M199 lifetime bug class, and tests/smoke/arena_lint.sh says so. The
     * summary's fields are fixed-size buffers that jc_telemetry_feed copies
     * into, so nothing points into `text` after this call. Same reason, and same
     * shape, as the Tab completer's per-keypress arena. */
    la = jc_arena_new(0);
    if (la == NULL) {
        return 0;
    }
    /* M599: this workspace's own log first, then the newest (write where the
     * reader reads -- the M533 rule). */
    if (jc_app_pick_telemetry_log(app->root[0] != '\0' ? app->root : app->cwd,
                                  la, path, sizeof(path)) != 0 ||
        jc_read_file(path, &text, NULL, la) != JC_OK) {
        jc_arena_free(la);
        return 0;
    }
    /* Filtered to this workspace: "never called HERE" is the per-project claim
     * (M56 stamps every event with the canonical root). */
    jc_snprintf(out->ws_filter, sizeof(out->ws_filter), "%s", app->root);
    jc_telemetry_feed(out, text);
    /* An empty result after filtering is NOT evidence of disuse -- this
     * workspace simply does not appear in the log. Same answer as no log. */
    jc_arena_free(la);
    if (out->events <= 0) {
        jc_telemetry_summary_free(out);
        jc_telemetry_summary_init(out);
        return 0;
    }
    if (label != NULL && label_cap > 0) {
        const char *base = strrchr(path, '/');
        jc_snprintf(label, label_cap, "%s, this workspace",
                    (base != NULL) ? base + 1 : path);
    }
    return 1;
}

/* See jc_app.h for why this lives here rather than staying static in the agent
 * loop. Kept beside the app struct because it reads three of its fields and
 * nothing else; jc_eventlog.c must not learn about jc_app (it is the low-level
 * sink, unit-tested without an application at all). */
struct cJSON *jc_app_telem_begin(struct jc_app *app, const char *event)
{
    cJSON *o;
    if (app == NULL || app->telemetry == NULL) {
        return NULL;
    }
    o = jc_eventlog_begin(app->telemetry, event);
    if (o != NULL) {
        cJSON_AddNumberToObject(o, "depth", (double)app->agent_depth);
        cJSON_AddNumberToObject(o, "turn", (double)app->turn);
        /* Conditional on purpose (M420): a turn with no envelope has no run id,
         * and an empty-string field would be a lie every reader must
         * special-case. ABSENT MEANS "not a bounded run", which is itself
         * information. */
        if (app->env != NULL && app->env->run_id != NULL &&
            app->env->run_id[0] != '\0') {
            cJSON_AddStringToObject(o, "run", app->env->run_id);
        }
    }
    return o;
}

void jc_app_telem_end(struct jc_app *app, struct cJSON *o)
{
    jc_eventlog_end((app != NULL) ? app->telemetry : NULL, o);
}

jc_status jc_app_read_file(struct jc_app *app, const char *path, char **out,
                           jc_size *len, struct jc_arena *a)
{
    if (jc_app_path_denied_ex(app, path, 0)) {
        return JC_ERR_DENIED;
    }
    if (app->fs != NULL && app->fs->read != NULL) {
        jc_status st = app->fs->read(app->fs->ctx, path, out, len, a);
        if (st == JC_OK) {
            return JC_OK;
        }
        /* The editor couldn't serve it (no such buffer / error): use disk. */
    }
    return jc_read_file(path, out, len, a);
}

jc_status jc_app_write_file(struct jc_app *app, const char *path,
                            const char *data, jc_size len)
{
    if (jc_app_path_denied(app, path)) {
        return JC_ERR_DENIED;
    }
    /* Note a write to a test-looking path, for the tests-not-wired check. Set at
     * this chokepoint rather than in each tool because every write goes through
     * here (write_file, edit_file, apply_patch, the ACP fs delegate) -- and the
     * case that motivated the check was write_file CREATING a new test file, which
     * the M88 edit-only hook never sees. */
    if (app->env != NULL && jc_env_is_test_path(path)) {
        app->env->test_file_written = 1;
    }
    /* M501: and remember that WE wrote it. Same chokepoint, same reason -- every
     * write passes here -- and it is what lets the end-of-turn sweep revert the
     * run's own out-of-scope writes without touching a change someone else made
     * to the tree while the run was going. Recorded before the write is
     * attempted: a failed write leaves the path in the set, which is the safe
     * direction (a revert of an unchanged file is a no-op, while forgetting a
     * successful write would leave a violation in place). */
    if (app->env != NULL) {
        jc_env_wrote_mark(app->env, app->root, path);
    }
    if (app->fs != NULL && app->fs->write != NULL) {
        jc_status st = app->fs->write(app->fs->ctx, path, data, len);
        if (st == JC_OK) {
            return JC_OK;
        }
    }
    return jc_write_file(path, data, len);
}

struct jc_arena *jc_app_scratch(struct jc_app *app)
{
    return app->scratch != NULL ? app->scratch : app->arena;
}

struct jc_arena *jc_app_tool_scratch(struct jc_app *app)
{
    if (app->tool_scratch != NULL) {
        return app->tool_scratch;
    }
    /* No per-call arena installed (subcommands, tests): fall back to the
     * per-turn one, which is still better than the session arena. */
    return jc_app_scratch(app);
}

jc_status jc_app_load_image(struct jc_app *app, const char *path,
                            struct jc_message *m)
{
    const char *mt;
    char *bytes = NULL;
    jc_size len = 0;
    char *b64;
    jc_size b64len;
    jc_status st;

    mt = jc_image_media_type(path);
    if (mt == NULL) {
        return JC_ERR_INVALID; /* unsupported extension */
    }
    /* Refuse out-of-fence paths before touching the filesystem at all (a read:
     * reference roots are allowed). */
    if (jc_app_path_denied_ex(app, path, 0)) {
        return JC_ERR_DENIED;
    }
    /* Reject an oversized image by its size *before* slurping it, so a 60 MB
     * file isn't read into the arena only to be rejected (the spike the M29
     * cap was meant to avoid). jc_file_size returns -1 for a path with no local
     * file (e.g. an editor-served buffer); the post-read cap below is the
     * backstop for that case. */
    {
        long sz = jc_file_size(path);
        if (sz > JC_IMAGE_MAX_BYTES) {
            return JC_ERR_TOOBIG;
        }
    }
    /* Read through the fence-aware chokepoint (the read cap also applies). Use
     * the scratch arena so the raw bytes are reclaimed at the next top-level
     * turn -- only the base64 (handed to the message) persists. */
    st = jc_app_read_file(app, path, &bytes, &len, jc_app_scratch(app));
    if (st != JC_OK) {
        return st;
    }
    if (len > (jc_size)JC_IMAGE_MAX_BYTES) {
        return JC_ERR_TOOBIG;
    }
    b64len = jc_base64_encoded_len(len);
    b64 = (char *)malloc(b64len + 1);
    if (b64 == NULL) {
        return JC_ERR_OOM;
    }
    if (jc_base64_encode((const unsigned char *)bytes, len, b64, b64len + 1)
        != JC_OK) {
        free(b64);
        return JC_ERR_TOOBIG;
    }
    /* Hand the base64 to the message (ownership transfers) -- no second copy. */
    st = jc_msg_add_image_owned(m, mt, b64);
    if (st != JC_OK) {
        free(b64);
    }
    return st;
}

/* Run `command` with a memory watchdog (M117): fork /bin/sh in its own process
 * group, read output via select, and sample the group's RSS every ~500ms.
 * On WARN (>=80% of budget) emit one status note; on KILL (>=100%) SIGTERM then
 * SIGKILL the group and append a note. Used whenever a budget OR a wall-clock
 * timeout is set. Every kill's note must name the cause that actually fired
 * (M342): byte cap, memory budget, and abort each end here, and until M342 all
 * three claimed "exceeded the memory budget". */
static jc_status run_command_watched(struct jc_app *app, const char *command,
                                     jc_size byte_limit, struct jc_sb *out,
                                     int *exit_code, int *truncated,
                                     long budget_kb, long timeout_sec)
{
    char shell[8400];
    int outp[2];
    pid_t pid;
    int status = 0;
    int warned = 0, killed = 0, timed_out = 0;
    int cap_killed = 0, mem_killed = 0;
    double next_check;
    double deadline = 0.0;

    jc_snprintf(shell, sizeof(shell), "%s 2>&1", command);
    if (jc_pipe_cloexec(outp) != 0) return JC_ERR_IO;
    pid = fork();
    if (pid < 0) { close(outp[0]); close(outp[1]); return JC_ERR_IO; }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(outp[1], 1);
        dup2(outp[1], 2);
        close(outp[0]);
        close(outp[1]);
        jc_proc_scrub_secret_env(); /* model-issued shell must not see API keys */
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset(); /* M461 */
        execl(jc_shell_path(), "sh", "-c", shell, (char *)NULL);
        _exit(127);
    }
    close(outp[1]);
    setpgid(pid, pid); /* race-free group set (parent side) */
    next_check = jc_now_millis() + 500.0;
    if (timeout_sec > 0) {
        deadline = jc_now_millis() + (double)timeout_sec * 1000.0;
    }
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int rc;
        char chunk[4096];
        FD_ZERO(&rfds);
        FD_SET(outp[0], &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 300000;
        rc = select(outp[0] + 1, &rfds, NULL, NULL, &tv);
        if (rc == 0) {
            jc_app_tick(app);   /* M258: idle tick, as in the popen path */
        }
        if (app->abort_flag) { kill(-pid, SIGTERM); killed = 1; break; }
        if (timeout_sec > 0 && jc_now_millis() >= deadline) {
            kill(-pid, SIGTERM);
            killed = 1;
            timed_out = 1;
            break;
        }
        if (rc > 0 && FD_ISSET(outp[0], &rfds)) {
            ssize_t n = read(outp[0], chunk, sizeof(chunk));
            if (n <= 0) break; /* EOF or error */
            if (byte_limit > 0 && out->len + (jc_size)n > byte_limit) {
                jc_sb_append_n(out, chunk, byte_limit - out->len);
                if (truncated != NULL) *truncated = 1;
                kill(-pid, SIGTERM);
                killed = 1;
                cap_killed = 1;
                break;
            }
            jc_sb_append_n(out, chunk, (jc_size)n);
        }
        if (budget_kb > 0 && jc_now_millis() >= next_check) {
            long rss = jc_proc_group_rss_kb((long)pid);
            int v = jc_memwatch_decision(rss, budget_kb);
            if (v == JC_MEMWATCH_WARN && !warned) {
                warned = 1;
                if (app->cb != NULL && app->cb->on_status != NULL) {
                    char m[160];
                    jc_snprintf(m, sizeof m,
                        "[mem] command using ~%ld MB (budget %ld MB)",
                        rss / 1024, budget_kb / 1024);
                    app->cb->on_status(app->cb->user, m);
                }
            } else if (v == JC_MEMWATCH_KILL) {
                killed = 1;
                mem_killed = 1;
                if (app->cb != NULL && app->cb->on_status != NULL) {
                    char m[160];
                    jc_snprintf(m, sizeof m,
                        "[mem] KILLED: exceeded the %ld MB budget (~%ld MB)",
                        budget_kb / 1024, rss / 1024);
                    app->cb->on_status(app->cb->user, m);
                }
                kill(-pid, SIGTERM);
                break;
            }
            next_check = jc_now_millis() + 500.0;
        }
    }
    close(outp[0]);
    if (killed) {
        kill(-pid, SIGKILL); /* escalate; the run is over either way */
    }
    waitpid(pid, &status, 0);
    if (timed_out) {
        char m[96];
        jc_snprintf(m, sizeof m,
            "\n[stopped: command timed out after %lds and was terminated]\n",
            timeout_sec);
        jc_sb_append(out, m);
        if (app->cb != NULL && app->cb->on_status != NULL) {
            app->cb->on_status(app->cb->user, m + 1); /* skip the leading \n */
        }
        if (exit_code != NULL) *exit_code = 124; /* timeout(1) convention */
    } else if (killed) {
        /* Three kills share this path and must not share a diagnosis: told
         * "memory budget" for a byte-cap kill, a driven model re-ran the same
         * over-cap command 32 times in one turn, escalating its output instead
         * of narrowing it (M342). The remedies differ, so the note names the
         * limit that fired; the caller's own truncation/spill note carries the
         * advice. */
        if (cap_killed) {
            char m[112];
            jc_snprintf(m, sizeof m,
                        "\n[stopped: output exceeded the %lu-byte capture "
                        "limit and the command was killed]\n",
                        (unsigned long)byte_limit);
            jc_sb_append(out, m);
        } else if (mem_killed) {
            jc_sb_append(out, "\n[stopped: exceeded the memory budget]\n");
        } else {
            /* abort_flag: the operator interrupted the turn mid-command */
            jc_sb_append(out, "\n[stopped: the run was interrupted]\n");
        }
        if (exit_code != NULL) *exit_code = 137; /* 128 + SIGKILL */
    } else if (exit_code != NULL) {
        if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
        else *exit_code = -1;
    }
    return JC_OK;
}

void jc_app_tick(struct jc_app *app)
{
    if (app != NULL && app->cb != NULL && app->cb->on_progress != NULL) {
        app->cb->on_progress(app->cb->user);
    }
}

jc_status jc_app_run_command(struct jc_app *app, const char *command,
                             jc_size byte_limit, struct jc_sb *out,
                             int *exit_code, int *truncated)
{
    /* The global effective timeout (config runTimeout, overridden by
     * --run-timeout) applies to every model-issued command -- run_tests,
     * @diff, and the shell tool alike. The shell tool adds a per-call
     * override via jc_app_run_command_ex directly. */
    long t = jc_config_run_timeout(0, app->config.run_timeout_cli,
                                   app->config.run_timeout);
    return jc_app_run_command_ex(app, command, byte_limit, t, out,
                                 exit_code, truncated);
}

jc_status jc_app_run_command_ex(struct jc_app *app, const char *command,
                                jc_size byte_limit, long timeout_sec,
                                struct jc_sb *out, int *exit_code,
                                int *truncated)
{
    char shell[8400];
    char chunk[4096];
    FILE *pipe;
    size_t n;
    int status;
    int code;

    if (truncated != NULL) {
        *truncated = 0;
    }
    if (exit_code != NULL) {
        *exit_code = -1;
    }
    if (command == NULL || command[0] == '\0') {
        return JC_ERR_INVALID;
    }

    /* Prefer the client terminal delegate (the editor runs the command in its
     * own terminal); fall back to a local subprocess on any delegate failure. */
    if (app->cmd != NULL && app->cmd->run != NULL) {
        int tr = 0;
        int ec = -1;
        if (app->cmd->run(app->cmd->ctx, command, byte_limit, out, &ec, &tr)
            == JC_OK) {
            if (truncated != NULL) {
                *truncated = tr;
            }
            if (exit_code != NULL) {
                *exit_code = ec;
            }
            return JC_OK;
        }
    }

    /* When a memory budget or a wall-clock timeout is set, use the watched
     * fork path (which can sample RSS and enforce a deadline, killing the
     * process group); otherwise the popen path below. (That path was "plain
     * popen + fread, byte-for-byte unchanged" until M258 gave it a select loop
     * so a front-end can be ticked while a command runs; with no tick consumer
     * installed it passes select a NULL timeout and still blocks exactly as
     * fread did -- see the note at the loop.) */
    if (app->config.mem_budget_mb > 0 || timeout_sec > 0) {
        long budget_kb = app->config.mem_budget_mb > 0
                       ? app->config.mem_budget_mb * 1024 : 0;
        return run_command_watched(app, command, byte_limit, out, exit_code,
                                   truncated, budget_kb, timeout_sec);
    }

    /* Local path: combine stderr into stdout so the model sees diagnostics.
     * Prefix an `unset` of the provider keys so a model-issued command cannot
     * read them from the environment (popen's child can't call the scrub). */
    {
        char unset[1024];
        jc_proc_secret_env_prefix(unset, sizeof(unset));
        jc_snprintf(shell, sizeof(shell), "%s%s 2>&1", unset, command);
    }
    pipe = jc_proc_popen(shell, "r");
    if (pipe == NULL) {
        return JC_ERR_IO;
    }
    /* M258: read the pipe directly rather than via fread, so a front-end that
     * wants liveness ticks (the TUI, for type-ahead) gets one every idle 200ms
     * instead of nothing at all for the minutes a build takes. With no tick
     * consumer installed, select() is passed a NULL timeout and blocks exactly
     * as the fread loop did -- identical bytes, identical waiting, so headless
     * and ACP keep the previous behaviour to the syscall. */
    {
        int fd = fileno(pipe);
        int ticking = (app->cb != NULL && app->cb->on_progress != NULL);
        for (;;) {
            ssize_t r;
            fd_set rfds;
            struct timeval tv;
            struct timeval *tvp = NULL;
            int sel;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            if (ticking) {
                tv.tv_sec = 0;
                tv.tv_usec = 200000;
                tvp = &tv;
            }
            sel = select(fd + 1, &rfds, NULL, NULL, tvp);
            if (sel < 0) {
                if (errno == EINTR) {
                    continue;   /* a signal (SIGWINCH, SIGINT) is not an error */
                }
                break;
            }
            if (sel == 0) {
                jc_app_tick(app);   /* idle: let the front-end breathe */
                continue;
            }
            r = read(fd, chunk, sizeof(chunk));
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (r == 0) {
                break;              /* the child closed its end */
            }
            n = (size_t)r;
            if (byte_limit > 0 && out->len + n > byte_limit) {
                jc_sb_append_n(out, chunk, byte_limit - out->len);
                if (truncated != NULL) {
                    *truncated = 1;
                }
                break;
            }
            jc_sb_append_n(out, chunk, (jc_size)n);
            if (ticking) {
                jc_app_tick(app);
            }
        }
    }
    status = pclose(pipe);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        code = 128 + WTERMSIG(status);
    } else {
        code = -1;
    }
    if (exit_code != NULL) {
        *exit_code = code;
    }
    return JC_OK;
}

int jc_app_was_read(const struct jc_app *app, const char *path)
{
    jc_size i;
    for (i = 0; i < app->read_files.len; i++) {
        const char *p =
            *(char **)jc_vec_at((struct jc_vec *)&app->read_files, i);
        if (strcmp(p, path) == 0) {
            return 1;
        }
    }
    return 0;
}

jc_status jc_app_switch_model(struct jc_app *app, int i)
{
    struct jc_provider *p;

    if (jc_config_set_active(&app->config, i) != JC_OK) {
        return JC_ERR_INVALID;
    }
    p = jc_provider_create(&app->config.model);
    if (p == NULL) {
        return JC_ERR_INVALID;
    }
    if (app->provider != NULL) {
        app->provider->vt->free(app->provider);
    }
    app->provider = p;
    return JC_OK;
}

jc_status jc_app_route_to(struct jc_app *app, int model_idx, const char *reason)
{
    jc_status st;
    const char *name;

    if (model_idx < 0 || model_idx == app->config.active) {
        return JC_OK; /* already active, or nothing to do */
    }
    st = jc_app_switch_model(app, model_idx);
    if (st != JC_OK) {
        return st;
    }
    name = (app->config.model.name != NULL) ? app->config.model.name
                                            : app->config.model.model;
    if (!app->quiet) {
        fprintf(stderr, "[route] -> %s (%s)\n", name != NULL ? name : "?",
                reason != NULL ? reason : "");
    }
    if (app->env != NULL) {
        cJSON *o = jc_env_journal_begin(app->env, "route");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "to", name != NULL ? name : "");
            cJSON_AddStringToObject(o, "reason", reason != NULL ? reason : "");
        }
        jc_env_journal_end(app->env, o);
    }
    return JC_OK;
}

#ifdef JC_HAVE_CURL
/* Cached reachability of model `i`'s endpoint (probe once per distinct apiBase,
 * applying the result to every model that shares it). */
static int model_reachable(struct jc_app *app, int i)
{
    struct jc_model_cfg *m = jc_config_model_at(&app->config, i);
    if (m == NULL) {
        return 0;
    }
    if (app->reach[i] == 0) {
        int up = jc_net_reachable(m->api_base, m->api_key, 2, &app->abort_flag);
        int n = jc_config_model_count(&app->config);
        int j;
        for (j = 0; j < n && j < JC_REACH_MAX; j++) {
            struct jc_model_cfg *mj = jc_config_model_at(&app->config, j);
            if (mj != NULL && mj->api_base != NULL && m->api_base != NULL &&
                strcmp(mj->api_base, m->api_base) == 0) {
                app->reach[j] = up ? 1 : -1;
            }
        }
    }
    return app->reach[i] > 0;
}
#endif

int jc_app_effective_model(struct jc_app *app, int idx)
{
#ifdef JC_HAVE_CURL
    int n;
    struct jc_model_cfg *m;
    unsigned char reach[JC_REACH_MAX];
    int i;
    int out = idx;

    if (app == NULL) {
        return idx;
    }
    n = jc_config_model_count(&app->config);
    if (idx < 0 || idx >= n || n > JC_REACH_MAX) {
        return idx;
    }
    m = jc_config_model_at(&app->config, idx);
    /* No fallback configured => never probe; behaviour is unchanged. */
    if (m == NULL || m->fallback == NULL || m->fallback[0] == '\0') {
        return idx;
    }
    if (app->reach == NULL) {
        app->reach = (signed char *)jc_arena_calloc(app->arena,
                                                    (jc_size)JC_REACH_MAX);
        if (app->reach == NULL) {
            return idx;
        }
    }
    for (i = 0; i < n; i++) {
        reach[i] = (unsigned char)(model_reachable(app, i) ? 1 : 0);
    }
    if (!jc_config_fallback_chain(&app->config, idx, reach, &out)) {
        return idx; /* dead end: keep the original (let it fail/retry) */
    }
    if (out != idx && !app->quiet) {
        struct jc_model_cfg *mo = jc_config_model_at(&app->config, out);
        fprintf(stderr, "[fallback] %s unreachable -> %s\n",
                m->name != NULL ? m->name : m->model,
                (mo != NULL && mo->name != NULL) ? mo->name
                    : (mo != NULL ? mo->model : "?"));
    }
    return out;
#else
    (void)app;
    return idx;
#endif
}

struct jc_model_cfg *jc_app_model_for_role(struct jc_app *app, unsigned role)
{
    int i;
    if (app == NULL) {
        return NULL;
    }
    i = jc_config_find_by_role(&app->config, role);
    if (i < 0) {
        return NULL;
    }
    i = jc_app_effective_model(app, i);
    return jc_config_model_at(&app->config, i);
}

/* --- Constraints (M110) --------------------------------------------------- */

static void constraints_path(struct jc_app *app, char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi/constraints.md", app->cwd);
}

void jc_app_constraints_load(struct jc_app *app)
{
    char path[1200];
    char *data;
    jc_size len = 0;
    if (app == NULL) return;
    app->n_constraints = 0;
    constraints_path(app, path, sizeof path);
    if (jc_read_file(path, &data, &len, app->arena) != JC_OK || len == 0) {
        return;
    }
    app->n_constraints = jc_constraint_parse(data, app->constraints,
                                             JC_CONSTRAINT_MAX, app->arena);
    /* M167: constraints persist per workspace, so a rule adopted in one run --
     * possibly misparsed from one sentence -- is silently enforced in every
     * later run in this directory. Name them at startup. This is the half of the
     * bug that turned a misparse into a lasting mystery: the operator only found
     * out when the agent refused something mid-task and explained itself in
     * prose. Cheap and quiet in the common case (no file => no output). */
    if (app->n_constraints > 0) {
        char names[512];
        jc_constraint_join_text(app->constraints, 0, app->n_constraints,
                                names, sizeof names);
        jc_logf(JC_LOG_WARN,
                "[constraint] %d active from %s: %s -- delete that file or run "
                "`/constraints clear` to lift them",
                app->n_constraints, path, names);
    }
}

void jc_app_constraints_save(struct jc_app *app)
{
    char path[1200];
    char dir[1100];
    struct jc_sb sb;
    int authored = 0;
    int i;
    if (app == NULL) return;
    for (i = 0; i < app->n_constraints; i++) {
        if (app->constraints[i].origin != JC_CONSTRAINT_INFERRED) {
            authored++;
        }
    }
    constraints_path(app, path, sizeof path);
    /* M169: with nothing authored there is nothing to persist, so REMOVE the
     * store rather than leaving a header-only file behind. Two reasons: a run
     * that merely inferred a constraint from its prompt should not litter the
     * workspace with a file implying a saved policy, and `/constraints clear`
     * genuinely clearing the store is the behaviour its name promises. Also
     * avoids creating `.jichi/` in a directory that had none. */
    if (authored == 0) {
        remove(path);
        return;
    }
    jc_snprintf(dir, sizeof dir, "%s/.jichi", app->cwd);
    jc_mkdir_p(dir);
    jc_sb_init(&sb);
    jc_constraint_serialize(app->constraints, app->n_constraints, &sb);
    jc_write_file(path, sb.data != NULL ? sb.data : "", sb.len);
    jc_sb_free(&sb);
}

int jc_app_constraint_add(struct jc_app *app, const struct jc_constraint *c)
{
    struct jc_constraint dup;
    if (app == NULL || c == NULL) return 0;
    if (app->n_constraints >= JC_CONSTRAINT_MAX) return 0;
    if (jc_constraint_has(app->constraints, app->n_constraints, c)) {
        /* M169: already held. If the new one is AUTHORED and the held one was
         * only inferred, promote it -- an operator typing `/constraints add`
         * for a rule jichi had already guessed is asking for it to stick, and
         * without this the guess would shadow the explicit request forever. */
        if (c->origin == JC_CONSTRAINT_AUTHORED) {
            int i;
            for (i = 0; i < app->n_constraints; i++) {
                if (jc_constraint_has(&app->constraints[i], 1, c) &&
                    app->constraints[i].origin == JC_CONSTRAINT_INFERRED) {
                    app->constraints[i].origin = JC_CONSTRAINT_AUTHORED;
                    jc_app_constraints_save(app);
                    break;
                }
            }
        }
        return 0;
    }
    dup.kind = c->kind;
    dup.subject = (c->subject != NULL)
                ? jc_arena_strdup(app->arena, c->subject) : NULL;
    dup.text = (c->text != NULL) ? jc_arena_strdup(app->arena, c->text) : NULL;
    dup.origin = c->origin;   /* M169: carry provenance, it decides persistence */
    app->constraints[app->n_constraints++] = dup;
    /* Saving is still unconditional: jc_constraint_serialize filters the inferred
     * ones out, so this rewrites the store with the authored set only. */
    jc_app_constraints_save(app);
    return 1;
}

int jc_app_constraints_adopt(struct jc_app *app, const char *msg, int authored)
{
    struct jc_constraint found[JC_CONSTRAINT_MAX];
    int nf, i, added = 0;
    if (app == NULL || msg == NULL) return 0;
    /* Scan temporaries live on the per-turn SCRATCH arena (M180): this runs
     * on every AUTO-mode turn even when nothing is adopted, and the session
     * arena kept every candidate string for the life of the process. An
     * ADOPTED constraint is safe: jc_app_constraint_add strdups its own
     * session-arena copies (dup.subject/dup.text) above. */
    nf = jc_constraint_scan(msg, found, JC_CONSTRAINT_MAX,
                            jc_app_scratch(app));
    for (i = 0; i < nf; i++) {
        /* jc_constraint_scan stamps everything INFERRED; an explicit
         * `/constraints add` overrides that, since the operator typed it. */
        if (authored) {
            found[i].origin = JC_CONSTRAINT_AUTHORED;
        }
        added += jc_app_constraint_add(app, &found[i]);
    }
    return added;
}

int jc_app_constraints_scan_adopt(struct jc_app *app, const char *msg)
{
    return jc_app_constraints_adopt(app, msg, 0);
}

void jc_app_constraints_clear(struct jc_app *app)
{
    if (app == NULL) return;
    app->n_constraints = 0;
    jc_app_constraints_save(app);
}

/* --- Config benchmark facts (M113) ---------------------------------------- */

void jc_app_confbench_facts(const struct jc_app *app,
                            struct jc_confbench_facts *f)
{
    struct jc_app *a = (struct jc_app *)app; /* role lookup is non-const */
    int fi = 0, si = 0;
    if (app == NULL || f == NULL) return;
    memset(f, 0, sizeof(*f));

    f->has_active_model = (app->config.model.name != NULL &&
                           app->config.model.name[0] != '\0');
    f->has_embed = jc_app_model_for_role(a, JC_ROLE_EMBED) != NULL;
    f->has_rerank = jc_app_model_for_role(a, JC_ROLE_RERANK) != NULL;
    f->has_summarize = jc_app_model_for_role(a, JC_ROLE_SUMMARIZE) != NULL;
    f->routing_ok = jc_config_routing_resolve(&app->config, &fi, &si);
    f->snapshots = app->config.snapshots ? 1 : 0;
    f->verify_set = (app->config.verify != NULL ||
                     app->config.test_command != NULL);
    f->constraints = app->n_constraints > 0;
    f->edit_scope = app->config.edit_scope.len > 0;
    f->path_fence = jc_app_path_fence_on(app);
    f->has_skills = app->skills.skills.len > 0;
    f->has_agents = app->agents.defs.len > 0;
    f->has_commands = app->commands.commands.len > 0;
    f->has_memory = (app->memory != NULL && app->memory[0] != '\0');
    f->lsp_set = app->config.lsp_servers.len > 0;
    f->mcp_set = app->config.mcp_servers.len > 0;
    /* Satisfied when cache reads are priced, OR the model is free (no pricing
     * needed) -- so a free backend isn't penalized. */
    f->cache_priced = (app->config.model.cache_read_cost > 0.0) ||
                      (app->config.model.input_cost == 0.0);
    f->context_declared = (app->config.model.context_limit > 0 ||
                           app->config.context_limit > 0);
    f->telemetry_on = app->config.log_level > 0;
}

/* Load the design/spec documents into app->design (M-C; multi-doc at M462).
 *
 * Sources, in this order: the config `design: [...]` list, then each CLI
 * --design/--spec in the order given. CLI APPENDS to config rather than
 * replacing it -- a project pins its standing architecture doc in config while
 * a task adds its own spec, and replacement would silently drop the former
 * while the prompt still called the section authoritative. Config first, CLI
 * last, because that is the order a person briefs someone in: standing context,
 * then the specific task, with the most task-specific material nearest the end.
 *
 * Deduped by RESOLVED path, so naming one document in both places costs the
 * always-sent prefix once, and `./d.md` and `docs/../d.md` are one document.
 *
 * The cap is on the TOTAL, not per document: per-document caps multiply, and
 * what is scarce is the prefix, which re-bills on every call of the run.
 *
 * These are user-specified paths, so they bypass the workspace path fence
 * (like --config); a missing file warns and is skipped, never fatal. */
static void design_add(struct jc_arena *scratch, struct jc_sb *sb,
                       const char *path, char seen[][JC_PATH_MAX],
                       int *nseen, int multi)
{
    char resolved[JC_PATH_MAX];
    char *data = NULL;
    jc_size len = 0;
    int i;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (jc_path_resolve(path, resolved, sizeof(resolved)) != JC_OK) {
        jc_snprintf(resolved, sizeof(resolved), "%s", path);
    }
    for (i = 0; i < *nseen; i++) {
        if (strcmp(seen[i], resolved) == 0) {
            return;             /* already included; do not pay for it twice */
        }
    }
    if (jc_read_file(path, &data, &len, scratch) != JC_OK || data == NULL) {
        jc_logf(JC_LOG_WARN, "design: could not read %s", path);
        return;
    }
    if (*nseen < JC_DESIGN_MAX_DOCS) {
        jc_snprintf(seen[*nseen], JC_PATH_MAX, "%s", resolved);
        (*nseen)++;
    }
    /* Name each document only when there is more than one. With a single doc a
     * heading is noise; with several it is the only way the model can tell
     * which plan a given paragraph belongs to. */
    if (multi) {
        const char *base = strrchr(path, '/');
        base = (base != NULL) ? base + 1 : path;
        if (sb->len > 0) {
            jc_sb_append(sb, "\n");
        }
        jc_sb_append(sb, "## ");
        jc_sb_append(sb, base);
        jc_sb_append(sb, "\n\n");
    }
    jc_sb_append_n(sb, data, len);
    jc_sb_append(sb, "\n");
}

void jc_app_load_design(struct jc_app *app, const char *const *paths,
                        int npaths)
{
    static char seen[JC_DESIGN_MAX_DOCS][JC_PATH_MAX];
    struct jc_arena *scratch;
    struct jc_sb sb;
    int nseen = 0;
    int ndocs;
    int multi;
    int i;

    ndocs = (int)app->config.design_docs.len + (npaths > 0 ? npaths : 0);
    if (ndocs == 0) {
        return;
    }
    multi = (ndocs > 1);

    /* A scratch arena freed before we return, and a malloc-owned result: the
     * M199 shape (see jc_memory_refresh, "exactly one live copy at a time").
     * Both halves matter now that /design can re-load mid-session -- on the
     * session arena each reload would abandon the previous document AND the
     * file bytes it was built from, for the life of the process. That is the
     * bug class that cost M197/M198, in miniature. */
    scratch = jc_arena_new(0);
    if (scratch == NULL) {
        return;
    }
    jc_sb_init(&sb);
    for (i = 0; i < (int)app->config.design_docs.len; i++) {
        char **e = (char **)jc_vec_at(&app->config.design_docs, (jc_size)i);
        design_add(scratch, &sb, (e != NULL) ? *e : NULL, seen, &nseen, multi);
    }
    for (i = 0; i < npaths; i++) {
        design_add(scratch, &sb, paths[i], seen, &nseen, multi);
    }

    if (sb.len == 0) {
        jc_sb_free(&sb);
        jc_arena_free(scratch);
        return;
    }
    if (sb.len > (jc_size)JC_DESIGN_MAX) {
        sb.len = jc_utf8_trunc_len(sb.data, (jc_size)JC_DESIGN_MAX);
        sb.data[sb.len] = '\0';
        jc_sb_append(&sb, "\n\n[design truncated to fit the context budget]\n");
    }
    free(app->design);          /* M199: exactly one live copy at a time */
    app->design = jc_strdup(sb.data);
    jc_sb_free(&sb);
    jc_arena_free(scratch);
}
