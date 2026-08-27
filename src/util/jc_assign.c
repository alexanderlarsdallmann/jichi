/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_assign.c - pure assignment-spec parse/render/score (see jc_assign.h). */

#include "jc_assign.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

int jc_assign_name_ok(const char *name)
{
    jc_size n;
    jc_size i;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    /* No dotfiles, and no bare "." or ".." however they are spelled. */
    if (name[0] == '.') {
        return 0;
    }
    n = (jc_size)strlen(name);
    for (i = 0; i < n; i++) {
        if (name[i] == '/' || name[i] == '\\') {
            return 0;   /* a name may not express a location */
        }
        /* ".." anywhere, not only as a whole component: the name is also used to
         * build the `<base>.solution.md` sibling, and a rule that holds for one
         * construction and not the other is a rule someone will get wrong. */
        if (name[i] == '.' && i + 1 < n && name[i + 1] == '.') {
            return 0;
        }
    }
    /* It must be a spec: ".md", and something before it. */
    if (n < 4 || strcmp(name + n - 3, ".md") != 0) {
        return 0;
    }
    return 1;
}

jc_status jc_assign_parse(const char *text, struct jc_assign_spec *out,
                          struct jc_arena *a)
{
    struct jc_md_doc doc;

    memset(out, 0, sizeof(*out));
    jc_md_parse(text, a, &doc);
    if (doc.front != NULL) {
        const char *s;
        out->title = jc_yaml_get_str(doc.front, "title", NULL);
        out->audience = jc_yaml_get_str(doc.front, "audience", NULL);
        out->verify = jc_yaml_get_str(doc.front, "verify", NULL);
        out->setup = jc_yaml_get_str(doc.front, "setup", NULL);
        out->phase = jc_yaml_get_str(doc.front, "phase", NULL);
        out->difficulty = jc_yaml_get_str(doc.front, "difficulty", NULL);
        s = jc_yaml_get_str(doc.front, "points", NULL);
        if (s != NULL) {
            out->points = (int)strtol(s, NULL, 10);
        }
        /* Copy retained strings into the arena so they survive jc_md_free. */
        out->title = (out->title != NULL) ? jc_arena_strdup(a, out->title)
                                          : NULL;
        out->audience = (out->audience != NULL)
                        ? jc_arena_strdup(a, out->audience) : NULL;
        out->verify = (out->verify != NULL) ? jc_arena_strdup(a, out->verify)
                                            : NULL;
        out->setup = (out->setup != NULL) ? jc_arena_strdup(a, out->setup)
                                          : NULL;
        out->phase = (out->phase != NULL) ? jc_arena_strdup(a, out->phase)
                                          : NULL;
        out->difficulty = (out->difficulty != NULL)
                          ? jc_arena_strdup(a, out->difficulty) : NULL;
        /* Optional `hints:` block-sequence ladder (graded nudges). Only a block
         * sequence of scalars is supported (the jc_yaml subset -- no flow []). */
        {
            struct jc_yaml *hn = jc_yaml_get(doc.front, "hints");
            jc_size n = jc_yaml_seq_len(hn); /* 0 when hn is NULL / not a seq */
            if (n > 0) {
                jc_size i;
                out->hints = (const char **)jc_arena_calloc(
                    a, n * (jc_size)sizeof(const char *));
                if (out->hints != NULL) {
                    for (i = 0; i < n; i++) {
                        struct jc_yaml *it = jc_yaml_seq_at(hn, i);
                        const char *v = (it != NULL && it->scalar != NULL)
                                        ? it->scalar : NULL;
                        /* M289: an entry with no readable text is NOT a hint.
                         * It used to be recorded as "" and still counted, so the
                         * ladder announced hints it could not give: a real run
                         * shows `hint` returning "Hint 1 of 4:" with an empty
                         * body (59 bytes -- header and footer only), twice out
                         * of four. That spends a tool call to say nothing, and
                         * tells a learner the ladder is broken. Skipping keeps
                         * nhints equal to the number of hints that exist,
                         * whether the blank came from the source or from a form
                         * this YAML subset cannot read (block scalars, flow
                         * sequences -- see jc_yaml). */
                        while (v != NULL && (*v == ' ' || *v == '\t')) {
                            v++;
                        }
                        if (v == NULL || v[0] == '\0') {
                            /* M409: count what M289 skips, so run_hint can
                             * name the gap -- a silently shorter ladder reads
                             * as a fact about the assignment, and 64 of 80
                             * shipped ladders were short before anyone saw. */
                            out->hints_skipped++;
                            continue;
                        }
                        out->hints[out->nhints++] = jc_arena_strdup(a, v);
                    }
                }
            }
        }
    }
    out->task = jc_arena_strdup(a, doc.body != NULL ? doc.body : "");
    jc_md_free(&doc);

    if (out->task == NULL || out->task[0] == '\0') {
        return JC_ERR_INVALID;
    }
    return JC_OK;
}

void jc_assign_score(const struct jc_test_report *rep, int verify_ok,
                     struct jc_assign_result *out)
{
    int total = (rep != NULL) ? rep->total : -1;
    int passed = (rep != NULL) ? rep->passed : -1;
    int failed = (rep != NULL) ? rep->failed : -1;

    out->passed = verify_ok ? 1 : 0;
    out->tests_run = (rep != NULL) ? jc_test_report_count(rep) : 0;
    if (out->tests_run < 0) {
        out->tests_run = 0;
    }
    out->tests_failed = (failed > 0) ? failed : 0;

    if (total > 0 && passed >= 0) {
        out->pct = (int)((passed * 100L) / total);
    } else if (out->tests_run > 0 && failed >= 0) {
        int ok = out->tests_run - failed;
        if (ok < 0) {
            ok = 0;
        }
        out->pct = (int)((ok * 100L) / out->tests_run);
    } else {
        out->pct = verify_ok ? 100 : 0;
    }
}

static int audience_is(const char *a, const char *want)
{
    return a != NULL && strcmp(a, want) == 0;
}

char *jc_assign_render(const struct jc_assign_spec *spec, struct jc_arena *a)
{
    struct jc_sb sb;
    char *out;

    jc_sb_init(&sb);
    jc_sb_append(&sb, "# ");
    jc_sb_append(&sb, spec->title != NULL ? spec->title : "Assignment");
    jc_sb_append(&sb, "\n\n");

    if (audience_is(spec->audience, "agent")) {
        jc_sb_append(&sb, "## Task (machine-checkable)\n\n");
        jc_sb_append(&sb, spec->task);
        jc_sb_append(&sb, "\n\nProduce a change to this repository such that "
                          "the verification command succeeds:\n\n");
        jc_sb_append(&sb, "```\n");
        jc_sb_append(&sb, spec->verify != NULL ? spec->verify
                                               : "(no verify command set)");
        jc_sb_append(&sb, "\n```\n");
        jc_sb_append(&sb, "\nSuccess is defined solely by that command's exit "
                          "status and test counts.\n");
    } else if (audience_is(spec->audience, "junior")) {
        jc_sb_append(&sb, "## What to build\n\n");
        jc_sb_append(&sb, spec->task);
        jc_sb_append(&sb, "\n\n## Suggested steps\n\n");
        jc_sb_append(&sb, "1. Read the code you'll touch before changing it.\n");
        jc_sb_append(&sb, "2. Make the smallest change that could work.\n");
        jc_sb_append(&sb, "3. Run the check below; iterate on failures.\n");
        jc_sb_append(&sb, "4. Re-read your diff for edge cases before "
                          "finishing.\n");
        if (spec->verify != NULL) {
            jc_sb_append(&sb, "\n## How your work is checked\n\n`");
            jc_sb_append(&sb, spec->verify);
            jc_sb_append(&sb, "` must pass.\n");
        }
    } else if (audience_is(spec->audience, "senior")) {
        jc_sb_append(&sb, spec->task);
        if (spec->verify != NULL) {
            jc_sb_append(&sb, "\n\nAcceptance: `");
            jc_sb_append(&sb, spec->verify);
            jc_sb_append(&sb, "` passes.\n");
        }
    } else {
        /* student (default) */
        jc_sb_append(&sb, "## Objective\n\n");
        jc_sb_append(&sb, spec->task);
        jc_sb_append(&sb, "\n\n## Learning goals\n\n");
        jc_sb_append(&sb, "- Understand the code path you are changing.\n");
        jc_sb_append(&sb, "- Cover the edge cases, not just the happy path.\n");
        if (spec->verify != NULL) {
            jc_sb_append(&sb, "\n## Self-check\n\nYour solution is complete "
                              "when `");
            jc_sb_append(&sb, spec->verify);
            jc_sb_append(&sb, "` passes.\n");
        }
    }

    /* Advertise the hint ladder (availability only -- the content is revealed
     * on demand by the `hint` tool while solving). Framed by audience. */
    if (spec->nhints > 0) {
        /* M613: straight into the sb -- this was staged through a char[192]
         * and jc_snprintf truncates silently, so the 206-byte student/junior
         * note ended mid-word ("...or delega") on every one of the 79 shipped
         * specs, and in the brief `attempt` hands the solver. A fixed staging
         * buffer for prose is the same bug at the next edit; the builder is
         * already unbounded. */
        if (audience_is(spec->audience, "senior") ||
            audience_is(spec->audience, "agent")) {
            jc_sb_append_fmt(&sb,
                "\n%d hint(s) available via the `hint` tool.\n", spec->nhints);
        } else {
            jc_sb_append_fmt(&sb,
                "\n## Stuck?\n\n%d hint(s) are available -- request the next "
                "one with the `hint` tool when you get stuck (use them "
                "sparingly; their use is recorded). You can also ask for a "
                "clarification or delegate a sub-part.\n", spec->nhints);
        }
    }
    /* M618: a ladder shorter than the file is SAID in the brief itself.
     * run_hint's stderr note (M409) reached only the CLI; `assign`, the TUI
     * /assignment and the `attempt` brief advertised "N hint(s)" with the
     * shortfall unsaid -- and this note must fire even when EVERY rung was
     * unreadable (nhints 0), which is the worst case, not the exempt one.
     * One place, all four surfaces. */
    if (spec->hints_skipped > 0) {
        jc_sb_append_fmt(&sb,
            "\n(%d hint line%s in the spec could not be read by the YAML "
            "subset and %s missing from the ladder above -- the author should "
            "quote the whole hint.)\n",
            spec->hints_skipped, spec->hints_skipped == 1 ? "" : "s",
            spec->hints_skipped == 1 ? "is" : "are");
    }
    out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return out;
}

/* M410: the attempt verdict, as one word. PASS is reserved for a green verify
 * that was not undermined: when M88's moved-goalpost heuristic fired during
 * the run (a test assertion MODIFIED, counted on the envelope), a green gate
 * is no longer evidence -- the cheap path to green is editing the test, and a
 * learner-tier model reliably finds it (measured 2026-08-12: an unfenced
 * attempt "passed" by gutting the gate tests, warned ten times, and was still
 * reported PASS). TAINTED says exactly that: verify exit 0, trust withheld,
 * review the diff. A red verify stays FAIL regardless -- a moved goalpost that
 * still fails needs no separate word. Pure; unit-tested. */
const char *jc_assign_attempt_verdict(int passed, int test_edits)
{
    if (!passed) {
        return "FAIL";
    }
    return (test_edits > 0) ? "TAINTED" : "PASS";
}

/* Is `tok` a command interpreter, i.e. does its next non-flag argument name the
 * real script? Kept to the interpreters a course actually uses; an unknown
 * program is treated as the program, which is the safe answer. */
static int is_interpreter(const char *tok)
{
    static const char *known[] = {
        "sh", "bash", "dash", "ksh", "zsh", "python", "python3", "perl",
        "ruby", "node", "tclsh", "awk", NULL
    };
    const char *base = tok;
    const char *p;
    int i;
    for (p = tok; *p != '\0'; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    for (i = 0; known[i] != NULL; i++) {
        if (strcmp(base, known[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *jc_assign_verify_program(const char *cmd, char *buf, jc_size cap)
{
    const char *p;
    const char *tok;
    jc_size n;
    int interp = 0;
    int round;

    if (cmd == NULL || buf == NULL || cap == 0) {
        return NULL;
    }
    buf[0] = '\0';
    p = cmd;
    /* Two rounds at most: the first token, then (if it was an interpreter) the
     * script it runs. */
    for (round = 0; round < 2; round++) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            return NULL;
        }
        /* A shape whose program cannot be read off the front: give up rather
         * than guess. `VAR=x cmd` and `(`/`{` subshells, a quoted program, and
         * anything the shell would re-parse. */
        if (*p == '\'' || *p == '"' || *p == '(' || *p == '{' || *p == '$') {
            return NULL;
        }
        tok = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        n = (jc_size)(p - tok);
        if (n == 0 || n >= cap) {
            return NULL;
        }
        /* An interpreter FLAG means the script is inline (`sh -c '...'`) or the
         * shape is beyond this helper: unknowable. */
        if (tok[0] == '-') {
            return NULL;
        }
        /* `VAR=value cmd ...`: an assignment prefix, not a program. */
        {
            jc_size k;
            int assign = 0;
            for (k = 0; k < n; k++) {
                if (tok[k] == '=') {
                    assign = 1;
                    break;
                }
                if (tok[k] == '/') {
                    break;      /* a path, not an assignment */
                }
            }
            if (assign) {
                return NULL;
            }
        }
        memcpy(buf, tok, (size_t)n);
        buf[n] = '\0';
        /* A shell operator anywhere in the command means more than one program
         * runs; the first one is still the one that must exist for anything to
         * happen, so keep it -- but a leading operator is nonsense. */
        if (round == 0 && is_interpreter(buf)) {
            interp = 1;
            continue;
        }
        break;
    }
    (void)interp;
    if (buf[0] == '\0') {
        return NULL;
    }
    return buf;
}
