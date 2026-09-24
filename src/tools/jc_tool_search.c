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
#include <string.h>
#include <sys/wait.h>

/* -I IS NOT UNIVERSAL, and the fifth grep is the one that proved it (M658).
 *
 * The comment below records that `-rnI` was "checked against each usage string,
 * not assumed" for GNU, FreeBSD, NetBSD and OpenBSD. illumos was not in that
 * set, and its grep -- /usr/bin/grep and /usr/xpg4/bin/grep alike -- accepts
 * -r and rejects -I: `usage: grep [-E|-F] [-bchHilLnoqrRsvx] ...`, exit 2. So
 * every search_code call on illumos failed, which the unit suite caught as one
 * check in test_tool.c. That is the SAME shape as the M461 --color defect, one
 * platform later: a flag that four greps accept is not a flag every grep
 * accepts, and the honest form of "portable" here is to ask.
 *
 * What -I buys is skipping binary files. Where it is unavailable the search
 * still runs and binary files are no longer skipped; the existing output cap
 * bounds what that can cost, and saying so is better than dropping the flag
 * everywhere or keeping a tool that cannot run.
 *
 * -E IS THE DIALECT MODELS WRITE (M714). Without it grep reads a POSIX *basic*
 * regular expression, where `|` and `+` are literal characters and `\(` opens a
 * group -- and models write the extended form: in the 2026-09-23 zigodot corpus
 * 230 of 888 patterns used a bare `|` against 5 using GNU-basic `\|`. In basic
 * mode those came back "(no matches)" for code that was there: re-running the
 * corpus pilot's 45 searches against the same tree, 11 were false negatives and
 * 1 a spurious error. That is the M461 lie again in another costume -- the tool
 * did not fail, it said the code does not contain what it contains. -E is POSIX
 * and every grep in the platform matrix takes it; illumos's own usage line is
 * `grep [-E|-F] ...`. */
const char *jc_search_grep_prefix(int have_dash_i)
{
    return have_dash_i ? "GREP_OPTIONS= grep -rnIE" : "GREP_OPTIONS= grep -rnE";
}

static void append_quoted(struct jc_sb *sb, const char *s);

/* When grep fails and prints nothing, say WHICH thing failed. `2>/dev/null`
 * keeps grep's stderr out of the result on the normal path (a stray warning must
 * not read as a match), so the diagnostic is recovered by asking grep about the
 * pattern alone: /dev/null cannot match, so a valid pattern exits 1 and an
 * invalid one exits 2 with grep's own message. Only on the failure path, and
 * grep reading /dev/null is free. Fills `msg` with grep's first line when the
 * PATTERN is invalid and returns 1; returns 0 when the pattern is fine (so the
 * failure was about the files) or the probe itself could not run. */
/* M732: the probe, generalised. Asks grep about the pattern alone against
 * /dev/null and returns grep's exit status (-1 when the probe could not run),
 * with grep's first line of output in `msg` ("" when it printed nothing).
 * /dev/null cannot match, so any line IS a diagnostic: with exit >= 2 it is the
 * reason the pattern is invalid, with exit 1 it is a WARNING about a pattern
 * grep accepted -- `(?:x)` gives "? at start of expression" and then matches
 * something other than what was meant. */
static int pattern_probe(const char *pattern, char *msg, jc_size cap)
{
    struct jc_sb cmd;
    FILE *p;
    char line[256];
    int st;
    jc_size n;

    msg[0] = '\0';
    line[0] = '\0';
    jc_sb_init(&cmd);
    jc_sb_append(&cmd, "GREP_OPTIONS= grep -E -e ");
    append_quoted(&cmd, pattern);
    jc_sb_append(&cmd, " /dev/null 2>&1");
    p = jc_proc_popen(cmd.data, "r");
    jc_sb_free(&cmd);
    if (p == NULL) {
        return 0;
    }
    if (fgets(line, (int)sizeof line, p) == NULL) {
        line[0] = '\0';
    }
    while (fgets(msg, (int)cap, p) != NULL) {
        /* drain, so grep never blocks on a full pipe */
    }
    msg[0] = '\0';
    st = pclose(p);
    n = (jc_size)strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
    jc_snprintf(msg, cap, "%s", line);
    if (st == -1 || !WIFEXITED(st)) {
        return -1;
    }
    return WEXITSTATUS(st);
}

/* The failure-path question: is the PATTERN invalid (exit >= 2)? */
static int pattern_error(const char *pattern, char *msg, jc_size cap)
{
    if (pattern_probe(pattern, msg, cap) < 2) {
        msg[0] = '\0';
        return 0;
    }
    if (msg[0] == '\0') {
        jc_snprintf(msg, cap, "grep rejected it");
    }
    return 1;
}

/* Asked once per process, cached. The question is the one the linker-style
 * probes in the Makefile ask: not "which platform is this" but "does the tool
 * in front of me accept this flag". /dev/null is the cheapest possible subject
 * and cannot match, so a 0 or 1 exit both mean the flag was understood. */
static int grep_has_dash_i(void)
{
    static int cached = -1;
    FILE *p;
    int st;

    if (cached >= 0) {
        return cached;
    }
    p = jc_proc_popen("GREP_OPTIONS= grep -I -e x /dev/null >/dev/null 2>&1", "r");
    if (p == NULL) {
        cached = 1;          /* cannot probe: keep the historical behaviour */
        return cached;
    }
    st = pclose(p);
    cached = (st != -1 && WIFEXITED(st) && WEXITSTATUS(st) < 2) ? 1 : 0;
    return cached;
}


static cJSON *search_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "pattern",
                     "Text or extended regular expression (grep -E): a|b "
                     "alternates, + ? {n} repeat, ( ) groups; backslash a "
                     "literal ( ) | + ? { } . * [ ] ^ $", 1);
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
    jc_sb_append(&cmd, jc_search_grep_prefix(grep_has_dash_i()));
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
        char why[256];
        char msg[400];
        jc_sb_free(&result);
        if (pattern_error(pattern, why, sizeof why)) {
            /* M714: name the pattern, quote grep, and say how to fix it -- the
             * generic message sent a model round again with the same pattern */
            jc_snprintf(msg, sizeof msg,
                        "error: the pattern is not a valid extended regular "
                        "expression (grep -E): %s -- put a backslash before a "
                        "literal ( ) | + ? { or [", why);
            tu_err(out, msg);
        } else {
            tu_err(out, "error: search failed -- grep exited with an error "
                        "reading the path (the pattern itself is valid)");
        }
        return JC_OK;
    }

    if (result.len == 0) {
        char warn[256];
        jc_sb_append(&result, "(no matches)");
        /* M732: say so when grep WARNED about the pattern. The `2>/dev/null`
         * above keeps warnings out of real results, and pattern_error() only
         * runs when grep fails -- so a pattern grep accepts with a warning
         * (exit 1) used to come back as a bare "(no matches)", and in the M731
         * drive a model repeated one such search thirteen times. */
        if (pattern_probe(pattern, warn, sizeof warn) == 1 && warn[0] != '\0') {
            jc_sb_append(&result, "\ngrep warned about the pattern: ");
            jc_sb_append(&result, warn);
            jc_sb_append(&result, " -- grep -E is not Perl: it has no (?:...) "
                                  "groups, lookaround or \\d, and matches one "
                                  "line at a time, so \\n never matches. Write "
                                  "( ) for a group and [0-9] for a digit.");
        }
    } else if (truncated) {
        jc_sb_append(&result, "\n... [output truncated]");
    }
    tu_ok_owned(out, jc_sb_finish(&result));
    jc_sb_free(&result);
    return JC_OK;
}

static const struct jc_tool SEARCH_TOOL = {
    "search_code",
    "Search for a pattern across files using grep -rnE (extended regular "
    "expressions).",
    search_schema,
    1, /* readonly */
    search_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

const struct jc_tool *jc_tool_search(void)
{
    return &SEARCH_TOOL;
}
