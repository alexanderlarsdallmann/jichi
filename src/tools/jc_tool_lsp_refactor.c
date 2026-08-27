/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_lsp_refactor.c - LSP-backed editing tools (M40).
 *
 * rename_symbol  - textDocument/rename: a semantically-correct, workspace-wide
 *                  rename (vs. error-prone grep-and-replace).
 * format_file    - textDocument/formatting: reformat a file via its language
 *                  server.
 *
 * Both turn the server's edits (a WorkspaceEdit / TextEdit[]) into real file
 * writes through the path-fenced jc_app_write_file, reusing the pure
 * jc_lsp_apply_text_edits applier. Mutating => permission-gated; registered only
 * when lspServers exist. */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_lsp.h"
#include "jc_str.h"
#include "jc_json.h"
#include "jc_fmtcmd.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

/* A file:// URI as a plain path (points within `uri`). */
static const char *uri_path(const char *uri)
{
    if (uri == NULL) {
        return NULL;
    }
    if (strncmp(uri, "file://", 7) == 0) {
        return uri + 7;
    }
    return uri;
}

/* Apply one file's TextEdit[] (the cJSON `edits` array) to `path`. Returns the
 * number of edits applied (>0), 0 when nothing changed / the file was skipped
 * (with a noted reason appended to `summary`), or -1 when `edits` is not an
 * edit array (e.g. a documentChanges file-operation entry — skip silently). */
static int apply_file_edits(struct jc_app *app, const char *path, cJSON *edits,
                            struct jc_sb *summary)
{
    char *content = NULL;
    char *edits_str;
    struct jc_sb nb;
    jc_size len = 0;
    int applied;

    if (path == NULL || !cJSON_IsArray(edits)) {
        return -1;
    }
    if (jc_app_path_denied(app, path)) {
        jc_sb_append_fmt(summary, "\n  %s (skipped: outside workspace)", path);
        return 0;
    }
    if (jc_app_read_file(app, path, &content, &len, jc_app_tool_scratch(app))
        != JC_OK) {
        jc_sb_append_fmt(summary, "\n  %s (skipped: could not read)", path);
        return 0;
    }
    edits_str = jc_json_print(edits);
    if (edits_str == NULL) {
        return -1;
    }
    jc_sb_init(&nb);
    applied = jc_lsp_apply_text_edits(content, edits_str, &nb);
    free(edits_str);
    if (applied <= 0) {
        jc_sb_free(&nb);
        if (applied < 0) {
            jc_sb_append_fmt(summary, "\n  %s (skipped: malformed edits)", path);
        }
        return 0;
    }
    if (jc_app_write_file(app, path, nb.data != NULL ? nb.data : "", nb.len)
        != JC_OK) {
        jc_sb_free(&nb);
        jc_sb_append_fmt(summary, "\n  %s (ERROR: write failed)", path);
        return 0;
    }
    jc_sb_free(&nb);
    jc_app_mark_read(app, path);
    jc_sb_append_fmt(summary, "\n  %s (%d edit%s)", path, applied,
                     applied == 1 ? "" : "s");
    return applied;
}

/* Apply a server WorkspaceEdit (`root`): walk documentChanges (ordered) or the
 * legacy changes map, applying each file's TextEdit[] via apply_file_edits.
 * Accumulates the edit/file counts + a per-file summary. Shared by rename and
 * apply_code_action (M44). */
static void apply_workspace_edit(struct jc_app *app, cJSON *root,
                                 struct jc_sb *summary, int *nfiles, int *nedits)
{
    cJSON *dc = cJSON_GetObjectItem(root, "documentChanges");
    cJSON *changes = cJSON_GetObjectItem(root, "changes");
    if (cJSON_IsArray(dc)) {
        cJSON *e;
        cJSON_ArrayForEach(e, dc) {
            cJSON *tdoc = jc_json_get_obj(e, "textDocument");
            const char *uri = (tdoc != NULL) ? jc_json_get_str(tdoc, "uri", NULL)
                                              : NULL;
            int k = apply_file_edits(app, uri_path(uri),
                                     jc_json_get_obj(e, "edits"), summary);
            if (k > 0) { (*nfiles)++; *nedits += k; }
        }
    } else if (cJSON_IsObject(changes)) {
        cJSON *child;
        cJSON_ArrayForEach(child, changes) {
            int k = apply_file_edits(app, uri_path(child->string), child,
                                     summary);
            if (k > 0) { (*nfiles)++; *nedits += k; }
        }
    }
}

/* Apply each WorkspaceEdit in a JSON-array string (the edits collected from a
 * command's workspace/applyEdit requests, M50). */
static void apply_edit_array(struct jc_app *app, const char *edits_json,
                             struct jc_sb *summary, int *nfiles, int *nedits)
{
    cJSON *arr = jc_json_parse(edits_json);
    cJSON *e;
    if (cJSON_IsArray(arr)) {
        cJSON_ArrayForEach(e, arr) {
            apply_workspace_edit(app, e, summary, nfiles, nedits);
        }
    }
    if (arr != NULL) {
        cJSON_Delete(arr);
    }
}

static cJSON *rename_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "symbol", "The symbol to rename (as it appears now).", 1);
    tu_schema_string(s, "path",
        "File where the symbol occurs (used to locate it for the server).", 1);
    tu_schema_string(s, "new_name", "The new name for the symbol.", 1);
    tu_schema_int(s, "line",
        "Optional 1-based line of the symbol in 'path' to disambiguate it.", 0);
    return s;
}

static jc_status rename_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *symbol = tu_arg_str(args, "symbol");
    const char *path = tu_arg_str(args, "path");
    const char *new_name = tu_arg_str(args, "new_name");
    int line = tu_arg_int(args, "line", 0);
    char *wse;
    cJSON *root;
    struct jc_sb summary;
    int nfiles = 0, nedits = 0;

    if (symbol == NULL || symbol[0] == '\0' || path == NULL || path[0] == '\0' ||
        new_name == NULL || new_name[0] == '\0') {
        tu_err(out, "error: 'symbol', 'path', and 'new_name' are required");
        return JC_OK;
    }
    if (app->lsp == NULL) {
        tu_err(out, "error: no language server is configured (lspServers)");
        return JC_OK;
    }
    wse = jc_lsp_rename(app->lsp, path, line, symbol, new_name);
    if (wse == NULL) {
        tu_err(out, "error: rename failed (no matching server, the symbol was "
                    "not found in the file, or the server returned no edits)");
        return JC_OK;
    }
    root = jc_json_parse(wse);
    free(wse);
    if (root == NULL) {
        tu_err(out, "error: could not parse the server's rename result");
        return JC_OK;
    }
    jc_sb_init(&summary);
    apply_workspace_edit(app, root, &summary, &nfiles, &nedits);
    cJSON_Delete(root);
    if (nedits == 0) {
        jc_sb_free(&summary);
        tu_err(out, "error: no edits were applied (the symbol may be "
                    "unrenamable, or every target was outside the workspace)");
        return JC_OK;
    }
    {
        struct jc_sb res;
        jc_sb_init(&res);
        jc_sb_append_fmt(&res,
            "Renamed '%s' to '%s': %d edit%s across %d file%s:", symbol,
            new_name, nedits, nedits == 1 ? "" : "s", nfiles,
            nfiles == 1 ? "" : "s");
        jc_sb_append(&res, summary.data != NULL ? summary.data : "");
        tu_ok_owned(out, jc_sb_finish(&res));
        jc_sb_free(&res);
    }
    jc_sb_free(&summary);
    return JC_OK;
}

static cJSON *format_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "The file to reformat via its language server.",
                     1);
    return s;
}

/* M263: the second formatting backend -- a configured `formatCommand` shell
 * string that rewrites the file in place. Used when no language server formats
 * this file (none configured, or the server returned nothing usable), so
 * languages with no LSP formatter are reachable at all. Returns 1 when it
 * handled the request (out is populated), 0 when no formatCommand is set. */
static int format_via_command(struct jc_app *app, const char *path,
                              struct jc_tool_result *out)
{
    const char *fmt = app->config.format_command;
    char cmd[4096];
    struct jc_sb sb;
    int code = -1;
    int truncated = 0;
    char msg[1200];

    if (fmt == NULL || fmt[0] == '\0') {
        return 0;
    }
    /* The path came from the model; quoting it is what makes this safe to hand
     * to a shell (jc_fmtcmd.h). A build failure here means it did not fit, and
     * an unquoted retry is exactly the wrong fallback -- refuse instead. */
    if (jc_fmtcmd_build(fmt, path, cmd, sizeof(cmd)) != 0) {
        tu_err(out, "error: formatCommand + path is too long to run safely");
        return 1;
    }
    jc_sb_init(&sb);
    if (jc_app_run_command_ex(app, cmd, 65536, app->config.run_timeout, &sb,
                              &code, &truncated) != JC_OK) {
        jc_sb_free(&sb);
        tu_err(out, "error: could not run formatCommand");
        return 1;
    }
    if (code != 0) {
        jc_snprintf(msg, sizeof(msg),
                    "error: formatCommand exited %d for %s\n%s", code, path,
                    sb.data != NULL ? sb.data : "");
        tu_err(out, msg);
        jc_sb_free(&sb);
        return 1;
    }
    /* The formatter rewrote the file itself, so the read-before-edit record
     * must be refreshed: what jichi last read is no longer what is on disk. */
    jc_app_mark_read(app, path);
    jc_snprintf(msg, sizeof(msg), "Formatted %s with formatCommand.%s%s", path,
                (sb.data != NULL && sb.len > 0) ? "\n" : "",
                (sb.data != NULL && sb.len > 0) ? sb.data : "");
    tu_ok_copy(out, msg);
    jc_sb_free(&sb);
    return 1;
}

static jc_status format_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    char *te;
    char *content = NULL;
    struct jc_sb nb;
    jc_size len = 0;
    int applied;
    char msg[1100];

    if (path == NULL || path[0] == '\0') {
        tu_err(out, "error: 'path' is required");
        return JC_OK;
    }
    if (jc_app_path_denied(app, path)) {
        tu_err_policy(out, "error: refused by safety fence (path outside workspace)");
        return JC_OK;
    }
    if (app->lsp == NULL) {
        if (format_via_command(app, path, out)) {
            return JC_OK;
        }
        tu_err(out, "error: no language server is configured (lspServers) and "
                    "no formatCommand is set");
        return JC_OK;
    }
    te = jc_lsp_format(app->lsp, path);
    if (te == NULL) {
        /* No server matched this file, or it answered with nothing usable --
         * the formatCommand backend is exactly for that gap. */
        if (format_via_command(app, path, out)) {
            return JC_OK;
        }
        tu_err(out, "error: formatting failed (no matching language server or "
                    "no response, and no formatCommand is set)");
        return JC_OK;
    }
    if (jc_app_read_file(app, path, &content, &len, jc_app_tool_scratch(app))
        != JC_OK) {
        free(te);
        jc_snprintf(msg, sizeof(msg), "error: could not read '%s'", path);
        tu_err(out, msg);
        return JC_OK;
    }
    jc_sb_init(&nb);
    applied = jc_lsp_apply_text_edits(content, te, &nb);
    free(te);
    if (applied < 0) {
        jc_sb_free(&nb);
        tu_err(out, "error: the server returned malformed formatting edits");
        return JC_OK;
    }
    if (applied == 0) {
        jc_sb_free(&nb);
        /* M195: "no edits" has TWO causes and they are not interchangeable.
         *
         * The 2026-07-08 fix rightly stopped treating a null reply as an error
         * (that produced a false failure on an already-tidy file), but it
         * collapsed "nothing to change" together with "I could not parse this
         * well enough to change anything". zls returns null for BOTH. Telling a
         * model that has just introduced a syntax error "Already formatted" tells
         * it the file is fine -- an invisible failure, and the consumer here is
         * the model (the M198 lesson).
         *
         * The two are distinguishable for free: an unparseable file has
         * error-severity diagnostics, and jichi already collects
         * publishDiagnostics for every opened document. Message only -- this
         * changes no edit and cannot touch the file.
         * See docs/analysis/2026-07-29-zls-format.md. */
        {
            int ndiag = 0;
            char *diag = jc_lsp_diagnostics(app->lsp, path, &ndiag);
            if (diag != NULL) {
                free(diag);
            }
            if (ndiag > 0) {
                jc_snprintf(msg, sizeof(msg),
                    "No changes: the language server returned no edits, and this "
                    "file has %d diagnostic(s) -- if they include syntax errors it "
                    "cannot be formatted until they are fixed (run the "
                    "find_references/list_symbols tools or read the file to see "
                    "them). This is NOT a confirmation that the file is tidy.",
                    ndiag);
                tu_ok_copy(out, msg);
                return JC_OK;
            }
        }
        tu_ok_copy(out, "Already formatted (no changes).");
        return JC_OK;
    }
    if (jc_app_write_file(app, path, nb.data != NULL ? nb.data : "", nb.len)
        != JC_OK) {
        jc_sb_free(&nb);
        jc_snprintf(msg, sizeof(msg), "error: could not write '%s'", path);
        tu_err(out, msg);
        return JC_OK;
    }
    jc_sb_free(&nb);
    jc_app_mark_read(app, path);
    jc_snprintf(msg, sizeof(msg), "Formatted %s (%d edit%s).", path, applied,
                applied == 1 ? "" : "s");
    tu_ok_copy(out, msg);
    return JC_OK;
}

/* Case-insensitive substring test (ASCII). */
static int ci_contains(const char *hay, const char *needle)
{
    jc_size nl, i;
    if (hay == NULL || needle == NULL) {
        return 0;
    }
    nl = (jc_size)strlen(needle);
    if (nl == 0) {
        return 1;
    }
    for (; *hay != '\0'; hay++) {
        for (i = 0; i < nl; i++) {
            char a = hay[i];
            char b = needle[i];
            if (a == '\0') {
                return 0; /* hay too short here on */
            }
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) {
                break;
            }
        }
        if (i == nl) {
            return 1;
        }
    }
    return 0;
}

static cJSON *action_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "File the action applies to.", 1);
    tu_schema_int(s, "line", "1-based line where the action applies.", 1);
    tu_schema_string(s, "title",
        "Title of the action to apply, as shown by list_code_actions "
        "(case-insensitive substring match).", 1);
    tu_schema_string(s, "kind",
        "Optional: restrict the offered actions to CodeActionKind(s), "
        "comma-separated (e.g. \"quickfix\", \"refactor\"); omit for all.", 0);
    return s;
}

static jc_status action_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    const char *title = tu_arg_str(args, "title");
    int line = tu_arg_int(args, "line", 0);
    char *res_json;
    cJSON *root;
    cJSON *e;
    cJSON *match = NULL;
    cJSON *edit = NULL;
    cJSON *resolved = NULL;
    struct jc_sb summary;
    int nfiles = 0, nedits = 0;
    int cmd_attempted = 0, cmd_ok = 0;
    cJSON *node;
    char msg[1100];

    if (path == NULL || path[0] == '\0' || title == NULL || title[0] == '\0' ||
        line <= 0) {
        tu_err(out, "error: 'path', 'line' (1-based), and 'title' are required");
        return JC_OK;
    }
    if (app->lsp == NULL) {
        tu_err(out, "error: no language server is configured (lspServers)");
        return JC_OK;
    }
    res_json = jc_lsp_code_actions(app->lsp, path, (long)line,
                                   tu_arg_str(args, "kind"));
    if (res_json == NULL) {
        tu_err(out, "error: no code actions (no matching server, or none "
                    "offered at that line)");
        return JC_OK;
    }
    root = jc_json_parse(res_json);
    free(res_json);
    if (!cJSON_IsArray(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        tu_err(out, "error: malformed code-action response");
        return JC_OK;
    }
    cJSON_ArrayForEach(e, root) {
        if (cJSON_IsObject(e) &&
            ci_contains(jc_json_get_str(e, "title", ""), title)) {
            match = e;
            break;
        }
    }
    if (match == NULL) {
        cJSON_Delete(root);
        jc_snprintf(msg, sizeof(msg),
            "error: no offered action matches '%s' (run list_code_actions)",
            title);
        tu_err(out, msg);
        return JC_OK;
    }
    edit = jc_json_get_obj(match, "edit");
    if (edit == NULL) {
        /* Lazily-resolved action: re-send it so the server fills the edit. */
        char *aj = jc_json_print(match);
        if (aj != NULL) {
            char *rj = jc_lsp_code_action_resolve(app->lsp, path, aj);
            free(aj);
            if (rj != NULL) {
                resolved = jc_json_parse(rj);
                free(rj);
                if (resolved != NULL) {
                    edit = jc_json_get_obj(resolved, "edit");
                }
            }
        }
    }
    /* Apply the edit (if any), then run the action's command (if any). A
     * command-only action (no edit) runs the command via
     * workspace/executeCommand and applies whatever the server pushes back as
     * workspace/applyEdit; an action with both does the edit first (M50). */
    node = (resolved != NULL) ? resolved : match;
    jc_sb_init(&summary);
    if (edit != NULL) {
        apply_workspace_edit(app, edit, &summary, &nfiles, &nedits);
    }
    {
        char *aj = jc_json_print(node);
        if (aj != NULL) {
            char *cmd = NULL;
            char *cargs = NULL;
            if (jc_lsp_action_command(aj, &cmd, &cargs)) {
                char *cedits = NULL;
                cmd_attempted = 1;
                if (jc_lsp_execute_command(app->lsp, path, cmd, cargs,
                                           &cedits) == JC_OK) {
                    cmd_ok = 1;
                    if (cedits != NULL) {
                        apply_edit_array(app, cedits, &summary, &nfiles,
                                         &nedits);
                    }
                }
                free(cedits);
            }
            free(cmd);
            free(cargs);
            free(aj);
        }
    }
    cJSON_Delete(root);
    if (resolved != NULL) {
        cJSON_Delete(resolved);
    }

    if (edit == NULL && !cmd_attempted) {
        jc_sb_free(&summary);
        tu_err(out, "error: that action provided no edit or command to apply");
        return JC_OK;
    }
    if (cmd_attempted && !cmd_ok) {
        jc_sb_free(&summary);
        tu_err(out, "error: the action's server command "
                    "(workspace/executeCommand) failed to execute");
        return JC_OK;
    }
    if (nedits == 0 && !cmd_ok) {
        jc_sb_free(&summary);
        tu_err(out, "error: the action's edit applied no changes (targets may "
                    "be outside the workspace)");
        return JC_OK;
    }
    {
        struct jc_sb r;
        jc_sb_init(&r);
        if (nedits > 0) {
            jc_sb_append_fmt(&r, "Applied code action '%s': %d edit%s across %d "
                "file%s:", title, nedits, nedits == 1 ? "" : "s", nfiles,
                nfiles == 1 ? "" : "s");
            jc_sb_append(&r, summary.data != NULL ? summary.data : "");
        } else {
            jc_sb_append_fmt(&r, "Ran code action '%s' (server command); "
                "no file edits were returned.", title);
        }
        tu_ok_owned(out, jc_sb_finish(&r));
        jc_sb_free(&r);
    }
    jc_sb_free(&summary);
    return JC_OK;
}

static const struct jc_tool RENAME_TOOL = {
    "rename_symbol",
    "Rename a symbol across the project via the language server "
    "(textDocument/rename) — semantically correct, unlike grep-and-replace. "
    "Give the current 'symbol', the 'path' it appears in, and the 'new_name'. "
    "Mutating; registered only when lspServers is configured.",
    rename_schema, 0, rename_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool FORMAT_TOOL = {
    "format_file",
    "Reformat a file: via its language server (textDocument/formatting) when "
    "one formats it, otherwise via the configured formatCommand. Mutating; "
    "registered when lspServers or formatCommand is set.",
    format_schema, 0, format_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool ACTION_TOOL = {
    "apply_code_action",
    "Apply a language-server code action / quick-fix at a line "
    "(textDocument/codeAction) — e.g. organize imports, add a missing import, "
    "apply a fix-it. Give 'path', 'line' (1-based), and the action 'title' from "
    "list_code_actions. Applies the action's edit and/or runs its server command "
    "(workspace/executeCommand, applying any edits the server pushes back). "
    "Mutating; registered only when lspServers is configured.",
    action_schema, 0, action_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_rename_symbol(void) { return &RENAME_TOOL; }
const struct jc_tool *jc_tool_format_file(void)   { return &FORMAT_TOOL; }
const struct jc_tool *jc_tool_apply_code_action(void) { return &ACTION_TOOL; }
