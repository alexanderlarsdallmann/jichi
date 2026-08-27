/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_read.c - the read_file tool. */

#include "jc_toolcaps.h"
#include "tool_util.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_lineno.h"
#include "jc_pdf.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>


static cJSON *read_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "Path to the file to read", 1);
    tu_schema_int(s, "offset",
                  "1-based line to start at (default 1). Line numbers in the "
                  "output are a display gutter only; they are not part of the "
                  "file (don't include them in edit_file's old_string).", 0);
    tu_schema_int(s, "limit", "Maximum number of lines to return (default all)",
                  0);
    return s;
}

static jc_status read_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    int offset = tu_arg_int(args, "offset", 1);
    int limit = tu_arg_int(args, "limit", 0);
    char *data;
    jc_size dlen;
    jc_size cap = jc_config_cap(app->config.read_max_bytes, JC_CAP_READ_DEFAULT);
    struct jc_sb raw;
    struct jc_sb num;
    int truncated = 0;
    int total = 0;
    /* M594: the file's REAL line count, when it is knowable. `total` above is
     * the count of what was RETURNED, which after a cap is a different number --
     * and saying the second where the first belongs is what sent a model looking
     * for content this tool had just told it did not exist. */
    int total_true = 0;
    int have_true = 0;
    int shown;

    if (path == NULL) {
        tu_err(out, "error: 'path' argument is required");
        return JC_OK;
    }
    jc_sb_init(&raw);
    if (jc_pdf_is_pdf(path)) {
        /* PDFs are binary: extract their text via an external tool (M42). The
         * path fence still applies (don't run the extractor on files outside
         * the workspace). jc_proc_capture bounds the output to `cap`. */
        jc_status pst;
        char msg[1100];
        if (jc_app_path_denied_ex(app, path, 0)) {
            jc_sb_free(&raw);
            tu_err_policy(out, "error: refused by safety fence (path outside "
                        "workspace)");
            return JC_OK;
        }
        pst = jc_pdf_extract(path, app->config.pdf_command, cap, &raw,
                             &app->abort_flag);
        if (pst != JC_OK) {
            jc_sb_free(&raw);
            if (pst == JC_ERR_NOTFOUND) {
                jc_snprintf(msg, sizeof(msg),
                    "error: reading PDFs needs 'pdftotext' (poppler-utils) on "
                    "PATH; install it or set \"pdfCommand\" in config");
            } else {
                jc_snprintf(msg, sizeof(msg),
                            "error: could not extract text from '%s'", path);
            }
            tu_err(out, msg);
            return JC_OK;
        }
    } else {
        /* Read via the app helper, so when an ACP client offers fs we see its
         * (possibly unsaved) buffer; otherwise this reads from disk. The content
         * is owned by the arena. */
        /* M197: scratch, not the session arena. The bytes are copied into
         * `raw` on the next lines and never referenced again, but landing them
         * on app->arena (freed only at exit) retained EVERY file this session
         * ever read -- the whole file, note, not the readMaxBytes-capped
         * output. Measured at ~200 KB/turn reading one 200 KB file. */
        jc_status rst = jc_app_read_file(app, path, &data, &dlen,
                                         jc_app_tool_scratch(app));
        if (rst != JC_OK) {
            char msg[1100];
            jc_sb_free(&raw);
            /* M291: "could not open" hid a fence DENIAL behind what reads like a
             * missing file -- so the model retried, and routing counted it a
             * malfunction and escalated, which cannot help because the stronger
             * model meets the same fence. Say which it was. */
            if (rst == JC_ERR_DENIED) {
                jc_snprintf(msg, sizeof(msg),
                    "error: refused by safety fence (path outside workspace "
                    "and any referenceRoots): '%s'", path);
                tu_err_policy(out, msg);
            } else {
                jc_snprintf(msg, sizeof(msg), "error: could not open '%s'",
                            path);
                tu_err(out, msg);
            }
            return JC_OK;
        }
        if (dlen > cap) {
            jc_sb_append_n(&raw, data, cap);
            truncated = 1;
            /* The whole file is already in `data` -- only the OUTPUT is capped --
             * so the true count is one pass over memory we are holding anyway. */
            total_true = jc_count_lines(data, dlen);
            have_true = 1;
        } else {
            jc_sb_append_n(&raw, data, dlen);
        }
    }

    /* Number the requested slice (display-only gutter). */
    jc_sb_init(&num);
    shown = jc_format_numbered(raw.data != NULL ? raw.data : "", offset, limit,
                               &num, &total);
    if (shown == 0 && total > 0) {
        char msg[300];
        if (have_true) {
            /* The requested range may exist in the FILE and simply lie past the
             * readable window. Saying "file has <window>" denies it. */
            jc_snprintf(msg, sizeof(msg),
                "(no lines in range; the file has %d lines but only the first "
                "%d were read -- readMaxBytes is %lu; raise it, or read within "
                "line 1-%d)",
                total_true, total, (unsigned long)cap, total);
        } else if (truncated) {
            /* PDF path: the extractor was bounded, so the true size is unknown
             * here and this must not invent one. */
            jc_snprintf(msg, sizeof(msg),
                "(no lines in range; only the first %d lines were read -- the "
                "extracted text was truncated at %lu bytes)",
                total, (unsigned long)cap);
        } else {
            jc_snprintf(msg, sizeof(msg),
                        "(no lines in range; file has %d line%s)", total,
                        total == 1 ? "" : "s");
        }
        jc_sb_append(&num, msg);
    }
    if (truncated) {
        /* M594: how much is missing, so a second read can be aimed. The marker
         * substring is unchanged; the counts follow it. */
        if (have_true) {
            char tmsg[160];
            jc_snprintf(tmsg, sizeof(tmsg),
                " [output truncated] (read lines 1-%d of %d; readMaxBytes=%lu)\n",
                total, total_true, (unsigned long)cap);
            jc_sb_append(&num, "...");
            jc_sb_append(&num, tmsg);
        } else {
            jc_sb_append(&num, "... [output truncated]\n");
        }
    }

    /* Only mark a real file read (PDFs are extracted text, not editable). */
    if (!jc_pdf_is_pdf(path)) {
        jc_app_mark_read(app, path);
        /* M231: nudge on a byte-for-byte identical re-read. The content is still
         * returned in full above -- this only discourages the wasted
         * round-trip, never withholds.
         *
         * M287: hash `num` (the bytes actually SHOWN for this offset/limit), not
         * `raw` (the whole file), and key the record on the range. M231 passed
         * `raw`, so every page of a paged read hashed the same whole file and
         * was accused of being "byte-for-byte identical to your earlier read"
         * -- 142 firings against 12 genuinely redundant calls on one measured
         * project. Hashing what was shown also catches a repeat the old keying
         * MISSED: an unchanged slice of a file that changed elsewhere. */
        if (jc_app_reread_check(app, path, num.data, num.len,
                                (long)offset, (long)limit)) {
            jc_sb_append(&num,
                "\n[note: byte-for-byte identical to your earlier read of this "
                "file this session -- re-reading spends a round-trip; work from "
                "the copy you already have unless you changed it]\n");
        }
    }
    tu_ok_owned(out, jc_sb_finish(&num));
    jc_sb_free(&num);
    jc_sb_free(&raw);
    return JC_OK;
}

static const struct jc_tool READ_TOOL = {
    "read_file",
    "Read the contents of a file at the given path.",
    read_schema,
    1, /* readonly */
    read_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_read(void)
{
    return &READ_TOOL;
}
