/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_write.c - the write_file tool. */

#include "tool_util.h"
#include "jc_envelope.h"
#include "jc_app.h"
#include "jc_platform.h"
#include "jc_path.h"
#include "jc_snprintf.h"

#include <string.h>

static cJSON *write_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "Path to the file to write (created if absent)", 1);
    tu_schema_string(s, "content", "The full content to write to the file", 1);
    return s;
}

/* Ensure the parent directory of `path` exists. Returns JC_ERR_TOOBIG when the
 * path is longer than the buffer (rather than silently skipping, which would
 * then fail confusingly at write time). */
static jc_status ensure_parent(const char *path)
{
    char dir[JC_PATH_MAX];
    jc_size n = strlen(path);
    jc_size i;
    if (n >= sizeof(dir)) {
        return JC_ERR_TOOBIG;
    }
    memcpy(dir, path, n + 1);
    for (i = n; i > 0; i--) {
        if (dir[i] == '/') {
            dir[i] = '\0';
            jc_mkdir_p(dir);
            return JC_OK;
        }
    }
    return JC_OK;
}

static jc_status write_run(const cJSON *args, struct jc_tool_result *out,
                           struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    const char *content = tu_arg_str(args, "content");
    jc_size len;
    char msg[1100];
    char *oldc = NULL;
    char gpnote[256];

    gpnote[0] = '\0';
    if (path == NULL || content == NULL) {
        tu_err(out, "error: 'path' and 'content' are required");
        return JC_OK;
    }
    /* Check the fence before ensure_parent, so a denied write never gets to
     * mkdir parent directories outside the workspace. */
    if (jc_app_path_denied(app, path)) {
        tu_err_policy(out, "error: refused by safety fence (path outside workspace)");
        return JC_OK;
    }
    /* M615: capture the OLD content of a test-looking file before it is
     * replaced. tu_report_test_edit fired only from edit_file/apply_patch, so
     * a model that overwrote the gate WHOLESALE with write_file earned a clean
     * PASS from `attempt` -- the M410 verdict's sole input (env->test_edits)
     * never moved. Read onto the per-call arena, only when a bounded run is
     * watching and the path looks like a test. */
    if (app->env != NULL && jc_env_is_test_path(path)) {
        (void)jc_read_file(path, &oldc, NULL, jc_app_tool_scratch(app));
    }
    if (ensure_parent(path) != JC_OK) {
        tu_err(out, "error: path is too long");
        return JC_OK;
    }
    len = strlen(content);
    if (jc_app_write_file(app, path, content, len) != JC_OK) {
        jc_snprintf(msg, sizeof(msg), "error: could not write '%s'", path);
        tu_err(out, msg);
        return JC_OK;
    }
    /* M615: the same predicate edit_file applies (M88 shape: a test file, an
     * assertion on both sides, the old text not preserved) -- one reporter,
     * now for the third write path too. */
    if (oldc != NULL && jc_env_test_assertion_edit(path, oldc, content)) {
        tu_report_test_edit(app, "write_file", path, gpnote, sizeof gpnote);
    }
    /* We now know the file's contents, so the edit guard is satisfied. */
    jc_app_mark_read(app, path);
    jc_snprintf(msg, sizeof(msg), "Wrote %lu bytes to %s",
                (unsigned long)len, path);
    if (gpnote[0] != '\0') {
        /* Built unbounded (M613's lesson: no fixed staging for prose). */
        struct jc_sb res;
        jc_sb_init(&res);
        jc_sb_append(&res, msg);
        jc_sb_append(&res, gpnote);
        tu_ok_owned(out, jc_sb_finish(&res));
        jc_sb_free(&res);
    } else {
        tu_ok_copy(out, msg);
    }
    tu_append_diagnostics(out, app, path);
    return JC_OK;
}

static const struct jc_tool WRITE_TOOL = {
    "write_file",
    "Write (create or overwrite) a file with the given content.",
    write_schema,
    0, /* mutating */
    write_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_write(void)
{
    return &WRITE_TOOL;
}
