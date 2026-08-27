/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_search.c - the search_code tool (grep -rn under the hood). */

#include "jc_toolcaps.h"
#include "jc_proc.h"
#include "tool_util.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>


static cJSON *search_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "pattern", "Text or regex to search for", 1);
    tu_schema_string(s, "path", "Directory or file to search (default '.')", 0);
    tu_schema_int(s, "context",
                  "Lines of context to show around each match (default 0)", 0);
    return s;
}

/* Append `s` to `sb` single-quoted and shell-escaped. */
static void append_quoted(struct jc_sb *sb, const char *s)
{
    jc_sb_append_char(sb, '\'');
    while (*s) {
        if (*s == '\'') {
            /* close quote, escaped quote, reopen quote */
            jc_sb_append(sb, "'\\''");
        } else {
            jc_sb_append_char(sb, *s);
        }
        s++;
    }
    jc_sb_append_char(sb, '\'');
}

static jc_status search_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *pattern = tu_arg_str(args, "pattern");
    const char *path = tu_arg_str(args, "path");
    int context = tu_arg_int(args, "context", 0);
    struct jc_sb cmd;
    struct jc_sb result;
    char chunk[4096];
    jc_size cap = jc_config_cap(app->config.search_max_bytes, JC_CAP_SEARCH_DEFAULT);
    FILE *pipe;
    size_t n;
    int truncated = 0;
    int status;

    if (pattern == NULL) {
        tu_err(out, "error: 'pattern' argument is required");
        return JC_OK;
    }
    if (path == NULL) {
        path = ".";
    }
    /* M383: fence the search path. search_code is a READ tool that returns file
     * CONTENTS, yet it consulted no fence -- a model could `grep -rn` any path
     * the process can read, in any mode incl. plan, unprompted. list_files was
     * fenced for the weaker leak (names only) at M324; the same reasoning binds
     * here with more force. Read intent (0), so referenceRoots are honored, and
     * grep -r does not follow symlinks so fencing the top path suffices. */
    if (jc_app_path_denied_ex(app, path, 0)) {
        tu_err(out, "error: path is outside the workspace (path fence)");
        return JC_OK;
    }
    if (context < 0) {
        context = 0;
    }
    if (context > 10) {
        context = 10; /* keep the output bounded */
    }

    jc_sb_init(&cmd);
    /* M461: this line used to pass GNU's `--color` flag with an explicit
     * value, which made the tool USELESS on OpenBSD -- BSD grep rejects the
     * option outright and exits 2, the `2>/dev/null` below hid the message,
     * and so every search returned "(no matches)". A model reads that as "the
     * code does not contain this", which is worse than an error: the tool did
     * not fail, it lied. Found by the OpenBSD row (BSD grep 0.9).
     *
     * The flag existed to stop a colour-injecting environment from putting
     * ANSI escapes in a captured pipe. GREP_OPTIONS is the only vector that
     * survives `sh -c` (an alias does not), so neutralising it does the same
     * job in a way every grep understands. `-rnI -e -C<n>` are all accepted by
     * GNU, FreeBSD, NetBSD and OpenBSD greps -- checked against each usage
     * string, not assumed. */
    jc_sb_append(&cmd, "GREP_OPTIONS= grep -rnI");
    if (context > 0) {
        char copt[24];
        jc_snprintf(copt, sizeof(copt), " -C%d", context);
        jc_sb_append(&cmd, copt);
    }
    jc_sb_append(&cmd, " -e ");
    append_quoted(&cmd, pattern);
    jc_sb_append_char(&cmd, ' ');
    append_quoted(&cmd, path);
    jc_sb_append(&cmd, " 2>/dev/null");

    pipe = jc_proc_popen(cmd.data, "r");
    jc_sb_free(&cmd);
    if (pipe == NULL) {
        tu_err(out, "error: failed to run search");
        return JC_OK;
    }
    jc_sb_init(&result);
    while ((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        if (result.len + n > cap) {
            jc_sb_append_n(&result, chunk, cap - result.len);
            truncated = 1;
            break;
        }
        jc_sb_append_n(&result, chunk, (jc_size)n);
    }
    status = pclose(pipe);

    /* grep's contract: 0 = matched, 1 = no match, >=2 = grep itself failed.
     * Only the LAST of those may not be reported as "(no matches)", which is
     * the lie the OpenBSD row caught. The check is deliberately narrowed to a
     * run that produced NOTHING: grep also exits 2 for a partial problem (an
     * unreadable file under -r) while still printing real matches, and
     * discarding those would trade a silent wrong answer for a loud one. */
    if (result.len == 0 && status != -1 &&
        WIFEXITED(status) && WEXITSTATUS(status) >= 2) {
        jc_sb_free(&result);
        tu_err(out, "error: search failed -- grep exited with an error "
                    "(is the pattern a valid regex for this platform's grep?)");
        return JC_OK;
    }

    if (result.len == 0) {
        jc_sb_append(&result, "(no matches)");
    } else if (truncated) {
        jc_sb_append(&result, "\n... [output truncated]");
    }
    tu_ok_owned(out, jc_sb_finish(&result));
    jc_sb_free(&result);
    return JC_OK;
}

static const struct jc_tool SEARCH_TOOL = {
    "search_code",
    "Search for a pattern across files using grep -rn.",
    search_schema,
    1, /* readonly */
    search_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_search(void)
{
    return &SEARCH_TOOL;
}
