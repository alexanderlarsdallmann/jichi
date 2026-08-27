/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_ls.c - the list_files tool. */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_snprintf.h"
#include "jc_lineno.h"
#include "jc_envelope.h"   /* jc_glob_match: one definition of "pattern" */

#include <stdlib.h>
#include <string.h>

/* M324: bounds on the pattern walk. A recursive listing is unbounded by nature
 * and this one is driven by a model's guess, so all three are hard limits rather
 * than advice:
 *
 *   RESULTS   what the model gets. Past this the answer is noise anyway, and the
 *             result says it was truncated so the model can narrow the pattern.
 *   ENTRIES   how much work we do regardless of how little matches. A pattern
 *             that matches nothing must still terminate on a huge tree.
 *   DEPTH     symlink loops. jc_is_dir follows links, so a link to ".." makes an
 *             unbounded descent; nothing else here would stop it.
 */
#define LS_MAX_RESULTS 1000
#define LS_MAX_ENTRIES 200000
#define LS_MAX_DEPTH   32

struct ls_walk {
    struct jc_sb *out;
    const char   *pattern;
    struct jc_app *app;
    long          nresults;
    long          nentries;
    int           truncated;   /* hit RESULTS  */
    int           exhausted;   /* hit ENTRIES  */
    /* M493: subdirectories that could not be READ, so their contents are absent
     * from the results. Skipping them is right -- one unreadable subtree must not
     * fail a whole listing -- but skipping them SILENTLY is the M483 defect in a
     * second walk: with the matches all inside such a subtree this tool reported
     * "(no files match ...)", which is the "it does not exist" answer, not the
     * "I could not look" answer. `root_failed` is the other half of that
     * distinction: a root that cannot be listed is not a partial result. */
    int           unreadable;
    int           root_failed;
};

static cJSON *ls_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "Directory to list (default current directory)", 0);
    /* M324: models invent a `glob` tool constantly -- 46 calls, never once
     * succeeding, in one 13,783-call workload, beside 7,761 shell calls. They
     * want pattern-based file finding and jichi had no tool for it, so they
     * shelled out. The key is named `pattern` so `glob {"pattern": ...}` maps
     * onto this schema unchanged, which is what lets glob become a real alias
     * instead of a hint. */
    tu_schema_string(s, "pattern",
        "Optional glob to find files recursively under `path`: '*' matches "
        "within one path segment, '**' crosses segments, '?' one character "
        "(e.g. '**/*.c', 'src/*.h'). Matched against paths relative to `path`. "
        "Without it, the immediate entries of `path` are listed.", 0);
    return s;
}

/* Append one matching path (control chars escaped, dirs marked). */
static void ls_emit(struct ls_walk *w, const char *rel, int is_dir)
{
    char safe[1024];
    jc_escape_ctrl(rel, safe, sizeof(safe));
    jc_sb_append(w->out, safe);
    if (is_dir) {
        jc_sb_append_char(w->out, '/');
    }
    jc_sb_append_char(w->out, '\n');
    w->nresults++;
}

/* Recursive pattern walk. `dir` is the filesystem path to read; `rel` is the
 * path relative to the listing root, which is what the pattern matches -- so a
 * pattern behaves the same wherever the workspace happens to live on disk. */
static void ls_walk_dir(struct ls_walk *w, const char *dir, const char *rel,
                        int depth)
{
    struct jc_vec names;
    jc_size i;

    if (depth > LS_MAX_DEPTH || w->truncated || w->exhausted) {
        return;
    }
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, jc_app_tool_scratch(w->app)) != JC_OK) {
        jc_vec_free(&names);
        /* Still skipped, now COUNTED -- and the root is separated out, because
         * "part of the tree was unreadable" and "the directory you named cannot
         * be listed" are different answers and the caller renders them so. */
        if (depth == 0) {
            w->root_failed = 1;
        } else {
            w->unreadable++;
        }
        return;
    }
    for (i = 0; i < names.len; i++) {
        const char *name = *(char **)jc_vec_at(&names, i);
        char child[2048];
        char childrel[2048];
        int isdir;

        if (w->nresults >= LS_MAX_RESULTS) {
            w->truncated = 1;
            break;
        }
        if (w->nentries >= LS_MAX_ENTRIES) {
            w->exhausted = 1;
            break;
        }
        w->nentries++;
        /* .git is skipped unconditionally: nobody globbing source files wants
         * its thousands of objects, and including it would burn the entry
         * budget before reaching the tree the user meant. Nothing else is
         * skipped -- a pattern for object files has every right to find build
         * output, and guessing which directories are "noise" is how a tool
         * starts lying about what is on disk. */
        if (strcmp(name, ".git") == 0) {
            continue;
        }
        jc_snprintf(child, sizeof(child), "%s/%s", dir, name);
        if (rel[0] == '\0') {
            jc_snprintf(childrel, sizeof(childrel), "%s", name);
        } else {
            jc_snprintf(childrel, sizeof(childrel), "%s/%s", rel, name);
        }
        isdir = jc_is_dir(child);
        if (jc_glob_match(w->pattern, childrel)) {
            ls_emit(w, childrel, isdir);
        }
        if (isdir) {
            ls_walk_dir(w, child, childrel, depth + 1);
        }
    }
    jc_vec_free(&names);
}

static jc_status ls_run(const cJSON *args, struct jc_tool_result *out,
                        struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    const char *pattern = tu_arg_str(args, "pattern");
    struct jc_vec names;
    struct jc_sb sb;
    jc_size i;

    if (path == NULL || path[0] == '\0') {
        path = ".";
    }
    /* M324: fence the listing. Until now list_files did NOT consult the path
     * fence at all -- it could enumerate any directory the process could read, which was
     * a mild gap for a one-level listing of names and a much bigger one for the
     * recursive walk added here. Read intent (0), so configured referenceRoots
     * are permitted exactly as they are for read_file. */
    if (jc_app_path_denied_ex(app, path, 0)) {
        tu_err(out, "error: path is outside the workspace (path fence)");
        return JC_OK;
    }

    if (pattern != NULL && pattern[0] != '\0') {
        struct ls_walk w;
        memset(&w, 0, sizeof(w));
        jc_sb_init(&sb);
        w.out = &sb;
        w.pattern = pattern;
        w.app = app;
        ls_walk_dir(&w, path, "", 0);
        if (w.root_failed) {
            /* The flat branch below has always said this; the pattern branch
             * returned an empty match list instead (M493). */
            jc_sb_free(&sb);
            tu_err(out, "error: could not list directory");
            return JC_OK;
        }
        if (w.nresults == 0) {
            jc_sb_append(&sb, "(no files match ");
            jc_sb_append(&sb, pattern);
            jc_sb_append(&sb, ")");
        } else if (w.truncated) {
            jc_sb_append_fmt(&sb,
                "[... truncated at %d matches; narrow the pattern ...]\n",
                LS_MAX_RESULTS);
        } else if (w.exhausted) {
            /* Distinct from truncation on purpose: "I stopped looking" is not
             * "there were too many", and a model told the wrong one will narrow
             * a pattern that was already fine. */
            jc_sb_append_fmt(&sb,
                "[... stopped after scanning %d entries; the results above are "
                "incomplete -- list a narrower directory ...]\n",
                LS_MAX_ENTRIES);
        }
        /* NOT part of the else-if chain above, deliberately. Those three are
         * mutually exclusive because they are competing explanations of the same
         * result; this one COMPOSES with all of them, and the pairing that
         * matters most is the one the chain would have hidden -- zero matches
         * AND an unreadable subtree, which is the difference between "these
         * files do not exist" and "I could not look everywhere". */
        if (w.unreadable > 0) {
            jc_sb_append_fmt(&sb,
                "\n[... %d director%s under this path could not be READ and %s "
                "not searched, so the results above are incomplete -- absence "
                "here does not mean the file does not exist ...]\n",
                w.unreadable, w.unreadable == 1 ? "y" : "ies",
                w.unreadable == 1 ? "was" : "were");
        }
        tu_ok_owned(out, jc_sb_finish(&sb));
        jc_sb_free(&sb);
        return JC_OK;
    }

    jc_vec_init(&names, sizeof(char *));
    /* M197: scratch -- the names are formatted into the result and dropped. */
    if (jc_list_dir(path, &names, jc_app_tool_scratch(app)) != JC_OK) {
        jc_vec_free(&names);
        tu_err(out, "error: could not list directory");
        return JC_OK;
    }
    jc_sb_init(&sb);
    for (i = 0; i < names.len; i++) {
        const char *name = *(char **)jc_vec_at(&names, i);
        char full[2048];
        char safe[1024];
        jc_snprintf(full, sizeof(full), "%s/%s", path, name);
        /* M200: escape control characters before emitting. A filename may
         * legally contain the newline this format uses as its record separator,
         * so `evil\nplanted.txt` used to render as two entries -- one of them a
         * file that does not exist. jc_is_dir still uses the RAW name. */
        jc_escape_ctrl(name, safe, sizeof(safe));
        jc_sb_append(&sb, safe);
        if (jc_is_dir(full)) {
            jc_sb_append_char(&sb, '/'); /* mark directories */
        }
        jc_sb_append_char(&sb, '\n');
    }
    if (sb.len == 0) {
        jc_sb_append(&sb, "(empty directory)");
    }
    jc_vec_free(&names);
    tu_ok_owned(out, jc_sb_finish(&sb));
    jc_sb_free(&sb);
    return JC_OK;
}

static const struct jc_tool LS_TOOL = {
    "list_files",
    "List a directory's entries, or find files matching a glob pattern "
    "recursively.",
    ls_schema,
    1, /* readonly */
    ls_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_ls(void)
{
    return &LS_TOOL;
}
