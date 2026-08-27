/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_apply_patch.c - the apply_patch tool: apply many exact-string edits
 * across one or more files in a single atomic call.
 *
 * Each edit is {path, old_string, new_string, replace_all?} with the same
 * semantics as edit_file (the file must have been read first; old_string must be
 * found, and unique unless replace_all). Edits to the same file are applied in
 * order, so a later edit sees the result of earlier ones. The call is atomic:
 * every edit is validated and applied to in-memory buffers first, and the files
 * are written only if all of them succeed -- on any validation failure nothing
 * is written. If a write then fails partway (I/O error, fence denial, delegate
 * reject), the already-written files are reverted to their originals (M138;
 * best-effort, through the same jc_app_write_file chokepoint) and the error
 * reports every file's state, so a half-applied multi-file patch is never
 * left behind silently.
 *
 * This is the agent-friendly companion to edit_file: one round-trip for a
 * multi-hunk change, and (via the shared jc_patch core) a result the TUI can
 * preview as a unified diff before approval.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_agent.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_patch.h"
#include "jc_diff.h"
#include "jc_snprintf.h"
#include "jc_envelope.h"
#include "jc_eventlog.h"
#include "jc_log.h"

#include <stdlib.h>
#include <string.h>

/* One target file's evolving content across the edits that touch it. */
struct fileent {
    const char  *path;  /* points into the args cJSON (lives for the call) */
    const char  *orig;  /* original content (arena-owned, for the diff)     */
    jc_size      orig_len; /* byte length of orig (binary-safe revert, M138) */
    struct jc_sb buf;   /* current working content                         */
    int          reps;  /* replacements applied so far                     */
    int          fuzzy; /* an edit here matched via the fuzzy fallback (M38) */
};

static cJSON *apply_schema(void)
{
    cJSON *s = tu_schema_begin();
    cJSON *props = cJSON_GetObjectItem(s, "properties");
    cJSON *req = cJSON_GetObjectItem(s, "required");
    cJSON *edits = cJSON_CreateObject();
    cJSON *items = cJSON_CreateObject();
    cJSON *iprops = cJSON_CreateObject();
    cJSON *ireq = cJSON_CreateArray();
    cJSON *p;

    cJSON_AddStringToObject(edits, "type", "array");
    cJSON_AddStringToObject(edits, "description",
        "List of edits applied atomically, in order. Edits to the same file "
        "compound. The file must have been read first.");

    cJSON_AddStringToObject(items, "type", "object");

    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", "Path to the file to edit");
    cJSON_AddItemToObject(iprops, "path", p);

    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", "Exact text to find and replace");
    cJSON_AddItemToObject(iprops, "old_string", p);

    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", "Replacement text");
    cJSON_AddItemToObject(iprops, "new_string", p);

    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "boolean");
    cJSON_AddStringToObject(p, "description",
                            "Replace all occurrences (default false)");
    cJSON_AddItemToObject(iprops, "replace_all", p);

    cJSON_AddItemToArray(ireq, cJSON_CreateString("path"));
    cJSON_AddItemToArray(ireq, cJSON_CreateString("old_string"));
    cJSON_AddItemToArray(ireq, cJSON_CreateString("new_string"));

    cJSON_AddItemToObject(items, "properties", iprops);
    cJSON_AddItemToObject(items, "required", ireq);
    cJSON_AddItemToObject(edits, "items", items);
    cJSON_AddItemToObject(props, "edits", edits);
    cJSON_AddItemToArray(req, cJSON_CreateString("edits"));
    return s;
}

/* Find (or create) the working entry for `path`, reading the file on first use.
 * Returns NULL and sets *err on a read failure. */
static struct fileent *get_file(struct jc_vec *files, const char *path,
                                struct jc_app *app, const char **err)
{
    jc_size i;
    struct fileent ent;
    char *text;
    jc_size len;

    for (i = 0; i < files->len; i++) {
        struct fileent *e = (struct fileent *)jc_vec_at(files, i);
        if (strcmp(e->path, path) == 0) {
            return e;
        }
    }
    /* M197: scratch -- `orig` must live until every edit is validated and the
     * diffs are rendered (and for the M138 revert-in-place), which is still
     * within this tool call, not the session. */
    if (jc_app_read_file(app, path, &text, &len,
                         jc_app_tool_scratch(app)) != JC_OK) {
        *err = "could not read file";
        return NULL;
    }
    ent.path = path;
    ent.orig = text;
    ent.orig_len = len;
    ent.reps = 0;
    ent.fuzzy = 0;
    jc_sb_init(&ent.buf);
    jc_sb_append(&ent.buf, text);
    jc_vec_push(files, &ent);
    return (struct fileent *)jc_vec_at(files, files->len - 1);
}

static void free_files(struct jc_vec *files)
{
    jc_size i;
    for (i = 0; i < files->len; i++) {
        jc_sb_free(&((struct fileent *)jc_vec_at(files, i))->buf);
    }
    jc_vec_free(files);
}

static jc_status apply_run(const cJSON *args, struct jc_tool_result *out,
                           struct jc_app *app)
{
    cJSON *edits = cJSON_GetObjectItem((cJSON *)args, "edits");
    cJSON *e;
    struct jc_vec files;
    int idx = 0;
    int total_edits = 0;
    const char *test_edit_path = NULL; /* M88: a modified test assertion */
    char gpnote[600];              /* M435: moved-goalpost note, "" when none */
    char msg[1200];

    if (!cJSON_IsArray(edits) || cJSON_GetArraySize(edits) == 0) {
        tu_err(out, "error: 'edits' must be a non-empty array");
        return JC_OK;
    }

    jc_vec_init(&files, sizeof(struct fileent));

    /* Phase 1: validate + apply to in-memory buffers (no writes yet). */
    cJSON_ArrayForEach(e, edits) {
        const char *path = tu_arg_str(e, "path");
        const char *old_s = tu_arg_str(e, "old_string");
        const char *new_s = tu_arg_str(e, "new_string");
        int replace_all = tu_arg_bool(e, "replace_all", 0);
        struct fileent *ent;
        const char *err = NULL;
        struct jc_sb next;
        enum jc_patch_strategy strat;
        int count = 0;

        idx++;
        if (path == NULL || old_s == NULL || new_s == NULL) {
            jc_snprintf(msg, sizeof(msg),
                "error: edit %d: 'path', 'old_string', and 'new_string' are "
                "required", idx);
            tu_err(out, msg);
            free_files(&files);
            return JC_OK;
        }
        if (old_s[0] == '\0') {
            jc_snprintf(msg, sizeof(msg),
                "error: edit %d: 'old_string' must not be empty (use write_file "
                "to create a file)", idx);
            tu_err(out, msg);
            free_files(&files);
            return JC_OK;
        }
        if (!jc_app_was_read(app, path)) {
            jc_snprintf(msg, sizeof(msg),
                "error: edit %d: read %s before editing it", idx, path);
            tu_err(out, msg);
            free_files(&files);
            return JC_OK;
        }
        ent = get_file(&files, path, app, &err);
        if (ent == NULL) {
            jc_snprintf(msg, sizeof(msg), "error: edit %d: %s '%s'", idx,
                        err != NULL ? err : "failed", path);
            tu_err(out, msg);
            free_files(&files);
            return JC_OK;
        }
        jc_sb_init(&next);
        strat = jc_patch_apply(ent->buf.data != NULL ? ent->buf.data : "",
                               old_s, new_s, replace_all,
                               app->config.fuzzy_edit, &next, &count);
        if (strat == JC_PATCH_NONE) {
            struct jc_sb es;
            jc_sb_free(&next);
            jc_snprintf(msg, sizeof(msg),
                "error: edit %d: old_string not found in %s", idx, path);
            jc_sb_init(&es);
            jc_sb_append(&es, msg);
            jc_patch_nearmatch_hint(ent->buf.data != NULL ? ent->buf.data : "",
                                    old_s, &es);
            tu_err(out, es.data != NULL ? es.data : msg);
            jc_sb_free(&es);
            free_files(&files);
            return JC_OK;
        }
        if (strat == JC_PATCH_AMBIGUOUS) {
            struct jc_sb es;
            jc_sb_free(&next);
            jc_sb_init(&es);
            jc_snprintf(msg, sizeof(msg),
                "error: edit %d: old_string is not unique in %s (%d matches)",
                idx, path, count);
            jc_sb_append(&es, msg);
            /* M208: name the colliding lines, as the not-found path does. */
            jc_patch_matchlines_hint(ent->buf.data != NULL ? ent->buf.data : "",
                                     old_s, &es);
            tu_err(out, es.data != NULL ? es.data : msg);
            jc_sb_free(&es);
            free_files(&files);
            return JC_OK;
        }
        jc_sb_free(&ent->buf);
        ent->buf = next;
        ent->reps += count;
        if (strat != JC_PATCH_EXACT) {
            ent->fuzzy = 1;
        }
        total_edits++;
        /* M88: note (but don't block) an edit that modifies a test assertion. */
        if (test_edit_path == NULL && app->env != NULL &&
            jc_env_test_assertion_edit(path, old_s, new_s)) {
            test_edit_path = path;
        }
    }

    /* Phase 2: every edit validated -- now write the files. */
    {
        jc_size i;
        struct jc_sb summary;
        jc_sb_init(&summary);
        jc_sb_append_fmt(&summary, "Applied %d edit%s across %lu file%s:",
                         total_edits, total_edits == 1 ? "" : "s",
                         (unsigned long)files.len,
                         files.len == 1 ? "" : "s");
        for (i = 0; i < files.len; i++) {
            struct fileent *ent = (struct fileent *)jc_vec_at(&files, i);
            jc_status wst = jc_app_write_file(app, ent->path,
                              ent->buf.data != NULL ? ent->buf.data : "",
                              ent->buf.len);
            if (wst != JC_OK) {
                /* M138: revert-in-place. The originals are still in memory
                 * (ent->orig, held for the diff), so put them back in the
                 * files already written and report every file's state --
                 * all-or-nothing holds on disk too, or says exactly how it
                 * fell short. No mark_read bookkeeping: the read-guard is a
                 * membership set and every path here passed was_read in
                 * phase 1; a reverted file equals what was read. */
                struct jc_sb es;
                jc_size k;
                int revert_failed = 0;

                jc_sb_init(&es);
                jc_sb_append_fmt(&es,
                    "error: could not write '%s'; per-file state:", ent->path);
                for (k = 0; k < i; k++) {
                    struct fileent *w =
                        (struct fileent *)jc_vec_at(&files, k);
                    if (jc_app_write_file(app, w->path,
                                          w->orig != NULL ? w->orig : "",
                                          w->orig_len) == JC_OK) {
                        jc_sb_append_fmt(&es, "\n  %s: reverted to original",
                                         w->path);
                    } else {
                        revert_failed = 1;
                        jc_sb_append_fmt(&es,
                            "\n  %s: REVERT FAILED -- still contains the "
                            "patched content", w->path);
                    }
                }
                if (wst == JC_ERR_DENIED) {
                    /* Denial happens before any byte is written, so this is
                     * provably untouched (a restore would be equally denied). */
                    jc_sb_append_fmt(&es, "\n  %s: write denied -- untouched",
                                     ent->path);
                } else if (jc_app_write_file(app, ent->path,
                                             ent->orig != NULL ? ent->orig : "",
                                             ent->orig_len) == JC_OK) {
                    jc_sb_append_fmt(&es,
                        "\n  %s: write failed -- restored to original",
                        ent->path);
                } else {
                    revert_failed = 1;
                    jc_sb_append_fmt(&es,
                        "\n  %s: write failed -- restore also failed (may be "
                        "truncated)", ent->path);
                }
                for (k = i + 1; k < files.len; k++) {
                    jc_sb_append_fmt(&es, "\n  %s: untouched (never written)",
                        ((struct fileent *)jc_vec_at(&files, k))->path);
                }
                jc_sb_append(&es, revert_failed
                    ? "\nWARNING: files marked REVERT FAILED / truncated still "
                      "differ from their originals"
                    : "\nno edits from this call are applied; fix the cause "
                      "and re-send the whole patch");
                tu_err(out, es.data != NULL ? es.data
                                            : "error: write failed");
                jc_sb_free(&es);
                jc_sb_free(&summary);
                free_files(&files);
                return JC_OK;
            }
            jc_app_mark_read(app, ent->path);
            jc_sb_append_fmt(&summary, "\n  %s (%d replacement%s)%s", ent->path,
                             ent->reps, ent->reps == 1 ? "" : "s",
                             ent->fuzzy ? " [fuzzy match]" : "");
        }
        gpnote[0] = '\0';
        /* M88 + M435: one reporter, six destinations -- see tu_report_test_edit.
         * apply_patch reports the FIRST test-assertion path it found across the
         * whole patch, which is what test_edit_path already holds. */
        if (test_edit_path != NULL) {
            tu_report_test_edit(app, "apply_patch", test_edit_path,
                                gpnote, sizeof gpnote);
        }
        /* A unified diff per file, so the model sees the whole change. */
        for (i = 0; i < files.len; i++) {
            struct fileent *ent = (struct fileent *)jc_vec_at(&files, i);
            jc_sb_append_fmt(&summary, "\n\n--- %s\n", ent->path);
            jc_diff_unified(ent->orig != NULL ? ent->orig : "",
                            ent->buf.data != NULL ? ent->buf.data : "",
                            3, 0, 200, &summary);
        }
        /* M435: after the diffs, so the model sees WHAT it changed first. */
        if (gpnote[0] != '\0') {
            jc_sb_append(&summary, gpnote);
        }
        tu_ok_copy(out, summary.data != NULL ? summary.data : "");
        jc_sb_free(&summary);
        /* Surface diagnostics for each touched file. */
        for (i = 0; i < files.len; i++) {
            tu_append_diagnostics(out, app,
                ((struct fileent *)jc_vec_at(&files, i))->path);
        }
    }

    free_files(&files);
    return JC_OK;
}

static const struct jc_tool APPLY_TOOL = {
    "apply_patch",
    "Apply several exact-string edits across one or more files in a single "
    "atomic call. Each edit is {path, old_string, new_string, replace_all?} "
    "(same rules as edit_file: the file must have been read first; old_string "
    "must be found, and unique unless replace_all). Edits to the same file "
    "compound in order. If any edit fails, no files are written; if a write "
    "fails partway, already-written files are reverted (each file's state "
    "reported). Prefer this "
    "over multiple edit_file calls for a multi-part change.",
    apply_schema,
    0, /* mutating */
    apply_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_apply_patch(void)
{
    return &APPLY_TOOL;
}
