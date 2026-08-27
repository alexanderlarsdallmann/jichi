/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_learn.c - parse a mentor lessons draft (see jc_learn.h). */

#include "jc_learn.h"
#include "jc_str.h"
#include "jc_insights.h"
#include "jc_telemetry.h"
#include "jc_session.h"
#include "jc_message.h"
#include "jc_json.h"
#include "jc_path.h"
#include "jc_snprintf.h"
#include "jc_app.h"
#include "jc_memory.h"
#include "jc_skill.h"

#include <string.h>
#include <ctype.h>

enum { SEC_NONE = 0, SEC_MEMORY, SEC_SKILLS, SEC_CORRECTIONS, SEC_RULES,
       SEC_OTHER, SEC_CHECKS /* M602 */ };

void jc_learn_draft_init(struct jc_learn_draft *d)
{
    jc_vec_init(&d->checks, sizeof(char *)); /* M602 */
    d->checks_unsupported = 0;
    d->corrections_malformed = 0; /* M600 */
    jc_vec_init(&d->memory, sizeof(char *));
    jc_vec_init(&d->skills, sizeof(struct jc_learn_skill));
    jc_vec_init(&d->corrections, sizeof(struct jc_learn_correction));
    jc_vec_init(&d->rules, sizeof(char *));
}

void jc_learn_draft_free(struct jc_learn_draft *d)
{
    jc_vec_free(&d->checks); /* M602 */
    jc_vec_free(&d->memory);
    jc_vec_free(&d->skills);
    jc_vec_free(&d->corrections);
    jc_vec_free(&d->rules);
}

/* Case-insensitive: does `hay` contain `needle`? */
static int ci_contains(const char *hay, const char *needle)
{
    jc_size hn = (jc_size)strlen(hay);
    jc_size nn = (jc_size)strlen(needle);
    jc_size i, j;
    if (nn == 0) {
        return 1;
    }
    for (i = 0; i + nn <= hn; i++) {
        for (j = 0; j < nn; j++) {
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == nn) {
            return 1;
        }
    }
    return 0;
}

/* Trim leading/trailing ASCII whitespace; returns an arena copy. */
static char *trim_dup(const char *s, jc_size len, struct jc_arena *a)
{
    jc_size b = 0, e = len;
    while (b < e && (s[b] == ' ' || s[b] == '\t')) {
        b++;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) {
        e--;
    }
    {
        char *out = (char *)jc_arena_alloc(a, e - b + 1);
        if (out != NULL) {
            memcpy(out, s + b, e - b);
            out[e - b] = '\0';
        }
        return out;
    }
}

/* Case-insensitive check that `s` begins with `prefix` (ASCII). */
static int ci_prefix(const char *s, const char *prefix)
{
    jc_size i;
    for (i = 0; prefix[i] != '\0'; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
            return 0;
        }
    }
    return 1;
}

/* Parse a "## Corrections" bullet body (the text after "- "): "remove: <substr>"
 * or "replace: <substr> => <new note>". Pushes a jc_learn_correction on success.
 * `body` need not be NUL-only-that-line; it is a trimmed single line. */
static void parse_correction(struct jc_learn_draft *out, struct jc_arena *a,
                             const char *body)
{
    struct jc_learn_correction c;
    const char *rest;

    c.match = NULL;
    c.replacement = NULL;
    if (ci_prefix(body, "remove:")) {
        rest = body + 7;
        while (*rest == ' ' || *rest == '\t') {
            rest++;
        }
        if (*rest == '\0') {
            return;
        }
        c.match = trim_dup(rest, (jc_size)strlen(rest), a);
    } else if (ci_prefix(body, "replace:")) {
        const char *arrow;
        rest = body + 8;
        while (*rest == ' ' || *rest == '\t') {
            rest++;
        }
        arrow = strstr(rest, "=>");
        if (arrow == NULL || arrow == rest) {
            return; /* need "<substr> => <new note>" */
        }
        c.match = trim_dup(rest, (jc_size)(arrow - rest), a);
        c.replacement = trim_dup(arrow + 2, (jc_size)strlen(arrow + 2), a);
        if (c.replacement != NULL && c.replacement[0] == '\0') {
            c.replacement = NULL; /* empty replacement => a removal */
        }
    } else {
        out->corrections_malformed++; /* M600: prose where a directive was due */
        return;
    }
    if (c.match != NULL && c.match[0] != '\0') {
        jc_vec_push(&out->corrections, &c);
    }
}

static void flush_skill(struct jc_learn_draft *out, struct jc_arena *a,
                        char *name, char *desc, struct jc_sb *body)
{
    struct jc_learn_skill sk;
    if (name == NULL || name[0] == '\0') {
        return;
    }
    sk.name = name;
    sk.description = (char *)((desc != NULL) ? desc : "");
    sk.body = jc_arena_strdup(a, body->data != NULL ? body->data : "");
    jc_vec_push(&out->skills, &sk);
}

void jc_learn_parse_draft(const char *text, struct jc_arena *a,
                          struct jc_learn_draft *out)
{
    const char *p;
    int section = SEC_NONE;
    char *sk_name = NULL;
    char *sk_desc = NULL;
    struct jc_sb sk_body;
    int in_skill = 0;

    if (text == NULL || out == NULL) {
        return;
    }
    jc_sb_init(&sk_body);

    p = text;
    while (*p != '\0') {
        const char *eol = p;
        jc_size len;
        while (*eol != '\0' && *eol != '\n') {
            eol++;
        }
        len = (jc_size)(eol - p);

        if (len >= 1 && p[0] == '#') {
            /* A markdown heading at ANY level. Classify by CONTENT, not by the
             * '#' count, so the parser tolerates a model that shifts heading
             * levels (e.g. "# Memory notes" / "## <skill>" instead of the
             * canonical "## Memory notes" / "### <skill>: <desc>"). M75. */
            const char *h = p;
            jc_size hl = len;
            char hdr[128];
            jc_size n;
            const char *colon;
            while (hl > 0 && *h == '#') {
                h++;
                hl--;
            }
            while (hl > 0 && *h == ' ') {
                h++;
                hl--;
            }
            n = (hl < sizeof(hdr) - 1) ? hl : sizeof(hdr) - 1;
            memcpy(hdr, h, n);
            hdr[n] = '\0';
            colon = (const char *)memchr(h, ':', hl);
            if (section == SEC_SKILLS && colon != NULL) {
                /* A skill header "name: desc". A colon marks it as a skill
                 * entry, not a new section (canonical section titles -- Memory
                 * notes / Skills / Suggested -- carry no colon), so it is
                 * classified BEFORE the memory/suggested keyword branches: a
                 * skill whose name or description merely mentions "memory" or
                 * "suggested" is no longer misrouted into a section header and
                 * lost. M75 follow-up. */
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                sk_name = trim_dup(h, (jc_size)(colon - h), a);
                sk_desc = trim_dup(colon + 1,
                                   (jc_size)(hl - (colon - h) - 1), a);
                jc_sb_clear(&sk_body);
                in_skill = 1;
            /* The keyword tests below are ordered MOST SPECIFIC FIRST, and the
             * order is load-bearing. A mentor heading may name two sections at
             * once, and first-match-wins then decides where its bullets go.
             *
             * Measured 2026-08-07 while driving a downstream project: a mentor
             * wrote "## Memory Note Corrections" rather than the canonical
             * "## Corrections". "memory" used to be tested first, so the section
             * became SEC_MEMORY and the correction DIRECTIVES underneath were
             * filed as durable memory notes -- `- **Remove**: "..."` appended
             * verbatim to memory.md, taking it to 14 bytes under JC_MEMORY_MAX,
             * where the OLDEST notes silently drop. Instructions to modify memory
             * became facts, which is the one outcome a propose-only loop must not
             * be able to produce.
             *
             * "memory" is the most generic of these words (every one of these
             * sections is about remembered state), so it is tested LAST: a heading
             * that also says "correction", "rule" or "skill" means that. */
            } else if (ci_contains(hdr, "correction")) {
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                section = SEC_CORRECTIONS;
            } else if (ci_contains(hdr, "rule") ||
                       ci_contains(hdr, "convention") ||
                       ci_contains(hdr, "agents")) {
                /* M106: durable project rules -> AGENTS.md (not just memory). */
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                section = SEC_RULES;
            } else if (ci_contains(hdr, "suggested")) {
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                section = SEC_OTHER;
            } else if (ci_contains(hdr, "check")) {
                /* M602: after "suggested", so "## Suggested checks" stays a
                 * proposal for the human; before "memory" for the M291 reason. */
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                section = SEC_CHECKS;
            } else if (section != SEC_SKILLS && ci_contains(hdr, "skill")) {
                /* The "Skills" section header (entered once). */
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                section = SEC_SKILLS;
            } else if (ci_contains(hdr, "memory")) {
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                section = SEC_MEMORY;
            } else if (section == SEC_SKILLS) {
                /* A colon-less skill header (name only, no description). */
                if (in_skill) {
                    flush_skill(out, a, sk_name, sk_desc, &sk_body);
                    in_skill = 0;
                }
                sk_name = trim_dup(h, hl, a);
                sk_desc = jc_arena_strdup(a, "");
                jc_sb_clear(&sk_body);
                in_skill = 1;
            }
            /* else: a heading outside any known section -- ignore. */
        } else if (section == SEC_MEMORY && len >= 2 &&
                   (p[0] == '-' || p[0] == '*') && p[1] == ' ') {
            /* A memory bullet. M600: the "[evidence: …]" / "[pins: …]" trailers
             * are kept -- they are the note's provenance, the thing a later
             * Corrections pass checks it against, and `learn analyze` counts the
             * pinned share and resolves the cited paths. A bullet that is ONLY
             * an annotation is skipped, as before. */
            char *note = trim_dup(p + 2, len - 2, a);
            if (note != NULL && note[0] != '\0' && note[0] != '[') {
                jc_vec_push(&out->memory, &note);
            }
        } else if (section == SEC_CORRECTIONS && len >= 2 &&
                   (p[0] == '-' || p[0] == '*') && p[1] == ' ') {
            char *body = trim_dup(p + 2, len - 2, a);
            if (body != NULL && body[0] != '\0') {
                parse_correction(out, a, body);
            }
        } else if (section == SEC_CHECKS && len >= 2 &&
                   (p[0] == '-' || p[0] == '*') && p[1] == ' ') {
            /* M602: `constraint: <phrase>` is the one kind apply can commit. */
            char *body = trim_dup(p + 2, len - 2, a);
            if (body != NULL && body[0] != '\0') {
                if (ci_prefix(body, "constraint:")) {
                    const char *rest = body + 11;
                    while (*rest == ' ' || *rest == '\t') {
                        rest++;
                    }
                    if (*rest != '\0') {
                        char *phrase = trim_dup(rest, (jc_size)strlen(rest), a);
                        jc_vec_push(&out->checks, &phrase);
                    }
                } else {
                    out->checks_unsupported++;
                }
            }
        } else if (section == SEC_RULES && len >= 2 &&
                   (p[0] == '-' || p[0] == '*') && p[1] == ' ') {
            char *rule = trim_dup(p + 2, len - 2, a);
            if (rule != NULL && rule[0] != '\0') {
                jc_vec_push(&out->rules, &rule);
            }
        } else if (in_skill) {
            jc_sb_append_n(&sk_body, p, len);
            jc_sb_append_char(&sk_body, '\n');
        }

        if (*eol == '\0') {
            break;
        }
        p = eol + 1;
    }
    if (in_skill) {
        flush_skill(out, a, sk_name, sk_desc, &sk_body);
    }
    jc_sb_free(&sk_body);
}

/* ---- the offline `learn analyze` report (M70; moved here at M292) -------
 *
 * Lived as two statics in main.c, which meant only the CLI could produce it.
 * The TUI needs the same report for `/learn analyze`, and a report that two
 * front-ends render differently is exactly the drift M286 cost us -- so the
 * logic lives here once and both callers render its jc_sb. */

static void learn_redo_scan(struct jc_arena *arena, const char *ws,
                            int limit, struct jc_vec *findings)
{
    struct jc_vec metas;
    jc_size i;
    int scanned = 0;

    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    if (jc_session_list(&metas, arena) != JC_OK) {
        jc_vec_free(&metas);
        return;
    }
    for (i = 0; i < metas.len && scanned < limit; i++) {
        struct jc_session_meta *m =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        struct jc_session sess;
        struct jc_vec paths;
        jc_size j, n;
        if (ws != NULL && ws[0] != '\0' &&
            (m->workspace == NULL || strcmp(m->workspace, ws) != 0)) {
            continue;
        }
        if (jc_session_load_by_id(m->id, &sess, arena) != JC_OK) {
            continue;
        }
        scanned++;
        jc_vec_init(&paths, sizeof(char *));
        n = jc_history_len(&sess.history);
        for (j = 0; j < n; j++) {
            struct jc_message *msg = jc_history_get(&sess.history, j);
            jc_size k, nc = jc_msg_tool_call_count(msg);
            for (k = 0; k < nc; k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(msg, k);
                if (tc->name != NULL &&
                    (strcmp(tc->name, "edit_file") == 0 ||
                     strcmp(tc->name, "write_file") == 0)) {
                    cJSON *a = jc_json_parse(tc->arguments_json);
                    const char *p = (a != NULL)
                        ? jc_json_get_str(a, "path", NULL) : NULL;
                    if (p != NULL && p[0] != '\0') {
                        char *dup = jc_arena_strdup(arena, p);
                        jc_vec_push(&paths, &dup);
                    }
                    if (a != NULL) {
                        cJSON_Delete(a);
                    }
                }
            }
        }
        {
            /* M474: stamp with THIS session's workspace, inside the loop, before
             * the next session's findings arrive. When `ws` is NULL every session
             * is scanned regardless of workspace, so a finding here can concern a
             * different checkout entirely -- which is exactly what happened
             * (docs/analysis/2026-08-18-dogfooding-on-chrtext.md §3) and why the
             * workspace has to travel with the finding rather than be inferred. */
            jc_size mark = findings->len;
            jc_insights_redo_loops((const char *const *)paths.data,
                                   (int)paths.len, findings);
            jc_insights_stamp(findings, mark, "session",
                              (m->workspace != NULL) ? m->workspace : "");
        }
        jc_vec_free(&paths);
        jc_session_free(&sess);
    }
    jc_vec_free(&metas);
}

/* M600: does a note's cited path resolve in the workspace the report is about?
 * Relative to the workspace root; an absolute path is checked as given. */
static int analyze_path_exists(const char *path, void *ctx)
{
    const char *ws = (const char *)ctx;
    char full[1400];
    if (path == NULL || path[0] == '\0') {
        return 1; /* nothing to check is not a failure */
    }
    if (path[0] == '/') {
        return jc_file_exists(path);
    }
    jc_snprintf(full, sizeof(full), "%s/%s", (ws != NULL) ? ws : ".", path);
    return jc_file_exists(full);
}

void jc_learn_analyze_render(struct jc_arena *arena, const char *text,
                             const char *ws_arg, struct jc_sb *out)
{
    long mem_bytes = 0;     /* M600 */
    int have_mem = 0;       /* M600 */
    struct jc_telemetry_summary s;
    struct jc_vec findings;

    jc_telemetry_summary_init(&s);
    if (ws_arg != NULL && ws_arg[0] != '\0') {
        char canon[JC_PATH_MAX];
        if (jc_path_resolve(ws_arg, canon, sizeof(canon)) == JC_OK) {
            jc_snprintf(s.ws_filter, sizeof(s.ws_filter), "%s", canon);
        } else {
            jc_snprintf(s.ws_filter, sizeof(s.ws_filter), "%s", ws_arg);
        }
    }
    jc_telemetry_feed(&s, text);

    jc_vec_init(&findings, sizeof(struct jc_insight));
    /* Recency-aware: skip tool failures that have since recovered or gone quiet
     * so a cumulative log doesn't re-surface already-fixed problems (ANECDOTES
     * #15). */
    jc_insights_from_telemetry_ex(&s, &findings, JC_INSIGHTS_RECENT_SEC);
    jc_insights_stamp(&findings, 0, "telemetry", NULL);
    learn_redo_scan(arena, (s.ws_filter[0] != '\0') ? s.ws_filter : NULL, 10,
                    &findings);
    if (s.ws_filter[0] != '\0') {
        char mpath[1300];
        char *mem = NULL;
        jc_snprintf(mpath, sizeof(mpath), "%s/.jichi/memory.md", s.ws_filter);
        if (jc_read_file(mpath, &mem, NULL, arena) == JC_OK && mem != NULL) {
            jc_size mark = findings.len;
            /* M600: the review can now check two things mechanically -- which
             * cited paths still resolve here, and how many notes carry a pin --
             * and the report states the injection budget as a fraction, because
             * the cap is silent and tail-kept: a project measured at 8,092 of
             * 8,192 bytes was one note from losing its oldest lesson. */
            jc_insights_stale_review_ex(mem, analyze_path_exists, s.ws_filter,
                                        &findings);
            jc_insights_stamp(&findings, mark, "memory", mpath);
            mem_bytes = (long)strlen(mem);
            have_mem = 1;
        }
    }
    jc_insights_render(&findings, out);
    if (have_mem) {
        long cap = (long)JC_MEMORY_MAX;
        if (mem_bytes > cap) {
            jc_sb_append_fmt(out, "\nMemory: %ld bytes in .jichi/memory.md, "
                   "%ld over the %ld-byte injection cap -- the OLDEST %ld bytes "
                   "no longer reach the prompt. Retract stale notes (`learn "
                   "corrections`) before adding more.\n", mem_bytes,
                   mem_bytes - cap, cap, mem_bytes - cap);
        } else {
            jc_sb_append_fmt(out, "\nMemory: %ld of %ld bytes injected (%ld%%); "
                   "%ld bytes of headroom before the oldest note drops.\n",
                   mem_bytes, cap, cap > 0 ? (mem_bytes * 100L) / cap : 0L,
                   cap - mem_bytes);
        }
    }
    if (s.out_completed + s.out_verify_failed + s.out_budget_kept +
        s.out_budget_reverted > 0) {
        jc_sb_append_fmt(out, "\nAutonomy outcomes: %ld completed, %ld "
               "budget-exhausted (work kept), %ld reverted, %ld verify-failed "
               "-- budget stops that kept green work are NOT failures; do not "
               "draft a lesson treating them as one.\n",
               s.out_completed, s.out_budget_kept, s.out_budget_reverted,
               s.out_verify_failed);
    }
    jc_vec_free(&findings);
    jc_telemetry_summary_free(&s);
}

/* --- applying a draft (M293) -----------------------------------------------
 *
 * This lived in main.c as three statics until M293. Moving it here is what lets
 * the TUI commit a draft -- and the reason that matters is correctness, not
 * convenience: jc_memory_add does NOT refresh app->memory (the `remember` tool
 * calls jc_memory_refresh itself), and a `learn apply` run in a SECOND process
 * cannot refresh a live session at all. So a TUI session kept serving notes that
 * a `## Corrections` section had just superseded, until it was restarted. */

/* Slugify a skill name into a directory-safe folder name. */
static void learn_slug(const char *name, char *out, jc_size cap)
{
    jc_size o = 0;
    int prev_dash = 1; /* trim leading dashes */
    const char *p = name;
    if (name == NULL) {
        out[0] = '\0';
        return;
    }
    for (; *p != '\0' && o + 1 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[o++] = (char)c;
            prev_dash = 0;
        } else if (c >= 'A' && c <= 'Z') {
            out[o++] = (char)(c - 'A' + 'a');
            prev_dash = 0;
        } else if (!prev_dash) {
            out[o++] = '-';
            prev_dash = 1;
        }
    }
    while (o > 0 && out[o - 1] == '-') {
        o--;
    }
    out[o] = '\0';
}

/* M106: commit mentor-proposed project rules to the project's rules file (deduped
 * by substring; appended under a "## Learned conventions" heading). Returns the
 * number of new rules written.
 *
 * M533: WRITE WHERE THE READER READS. This targeted `<cwd>/AGENTS.md`
 * unconditionally, while `jc_rules.c:add_dir_rules` reads `AGENTS.md` and, only
 * if it is absent, `CLAUDE.md`. On a project that uses `CLAUDE.md` -- jichi
 * itself -- one `learn apply` therefore CREATED an `AGENTS.md` holding just the
 * new rules, which then shadowed the entire `CLAUDE.md` for every subsequent
 * run. `CLAUDE.md` warns against exactly that file existing, in a bullet that
 * cites this same reader. The dedup broke the same way: the substring check only
 * ever read `AGENTS.md`, so a rule already present in `CLAUDE.md` was appended
 * again.
 *
 * The same defect as `config set` orphaning `.jichi/config.json` in this
 * milestone, and the same fix: mirror the reader's precedence exactly. A project
 * with neither file still gets `AGENTS.md`, the convention jichi scaffolds into
 * other people's projects. */
/* The rules file `learn apply` writes to: AGENTS.md, else CLAUDE.md when only
 * that exists -- mirroring jc_rules.c:add_dir_rules (M533: write where the
 * reader reads). Shared by the appender and, since M601, the retractor. */
static void learn_rules_path(const struct jc_app *app, char *path, jc_size cap)
{
    jc_snprintf(path, cap, "%s/AGENTS.md", app->cwd);
    if (!jc_file_exists(path)) {
        char alt[1200];
        jc_snprintf(alt, sizeof(alt), "%s/CLAUDE.md", app->cwd);
        if (jc_file_exists(alt)) {
            jc_snprintf(path, cap, "%s", alt);
        }
    }
}

/* Does `s[0..len)` contain `needle`? The same test jc_memory_apply_correction
 * applies to a memory bullet (its helper is file-static there), so a directive
 * matches a convention exactly as it matches a note. */
static int rules_range_contains(const char *s, jc_size len, const char *needle)
{
    jc_size nn = (jc_size)strlen(needle);
    jc_size i, j;
    if (nn == 0 || nn > len) {
        return 0;
    }
    for (i = 0; i + nn <= len; i++) {
        for (j = 0; j < nn && s[i + j] == needle[j]; j++) {
        }
        if (j == nn) {
            return 1;
        }
    }
    return 0;
}

int jc_learn_rules_correct(const char *text, const char *match,
                           const char *replacement, struct jc_sb *out)
{
    const char *p;
    int in_section = 0;
    int removed = 0;
    int added = 0;
    int seen_section = 0;
    const char *heading = "## Learned conventions";

    if (text == NULL || match == NULL || match[0] == '\0' || out == NULL) {
        return 0;
    }
    for (p = text; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        jc_size ll = nl != NULL ? (jc_size)(nl - p) : (jc_size)strlen(p);
        int is_heading = (ll >= 3 && p[0] == '#' && p[1] == '#' && p[2] == ' ');
        if (is_heading) {
            if (in_section && replacement != NULL && replacement[0] != '\0' &&
                removed > 0 && !added) {
                /* Leaving the section: append the replacement inside it. */
                jc_sb_append(out, "- ");
                jc_sb_append(out, replacement);
                jc_sb_append_char(out, '\n');
                added = 1;
            }
            in_section = (ll == strlen(heading) &&
                          strncmp(p, heading, ll) == 0);
            if (in_section) {
                seen_section = 1;
            }
        }
        if (in_section && !is_heading && ll >= 2 && p[0] == '-' && p[1] == ' ' &&
            rules_range_contains(p + 2, ll - 2, match)) {
            removed++; /* drop this retracted convention */
        } else {
            jc_sb_append_n(out, p, ll);
            if (nl != NULL) {
                jc_sb_append_char(out, '\n');
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    if (in_section && replacement != NULL && replacement[0] != '\0' &&
        removed > 0 && !added) {
        if (out->len > 0 && out->data != NULL && out->data[out->len - 1] != '\n') {
            jc_sb_append_char(out, '\n');
        }
        jc_sb_append(out, "- ");
        jc_sb_append(out, replacement);
        jc_sb_append_char(out, '\n');
        added = 1;
    }
    (void)seen_section;
    return removed + added;
}

/* M601: apply one correction to the rules file's learned conventions. Returns
 * the number of bullets removed/added there (0 when nothing matched or the file
 * or section is absent). Effectful shell around jc_learn_rules_correct. */
static int learn_rules_apply_correction(struct jc_app *app, const char *match,
                                        const char *replacement)
{
    char path[1200];
    char *existing = NULL;
    struct jc_sb out;
    int n;

    learn_rules_path(app, path, sizeof(path));
    if (!jc_file_exists(path) ||
        jc_read_file(path, &existing, NULL, jc_app_tool_scratch(app)) != JC_OK ||
        existing == NULL) {
        return 0;
    }
    jc_sb_init(&out);
    n = jc_learn_rules_correct(existing, match, replacement, &out);
    if (n > 0) {
        if (jc_write_file(path, out.data != NULL ? out.data : "", out.len)
                != JC_OK) {
            n = 0;
        }
    }
    jc_sb_free(&out);
    return n;
}

static int learn_apply_rules(struct jc_app *app, struct jc_vec *rules)
{
    char path[1200];
    char *existing = NULL;
    int have_existing;
    struct jc_sb add;
    struct jc_sb full;
    jc_size i;
    int added = 0;

    if (rules->len == 0) {
        return 0;
    }
    learn_rules_path(app, path, sizeof(path));
    have_existing = (jc_read_file(path, &existing, NULL,
                                  jc_app_tool_scratch(app)) == JC_OK &&
                     existing != NULL);
    jc_sb_init(&add);
    for (i = 0; i < rules->len; i++) {
        const char *rule = *(char **)jc_vec_at(rules, i);
        if (have_existing && strstr(existing, rule) != NULL) {
            continue; /* already present */
        }
        jc_sb_append(&add, "- ");
        jc_sb_append(&add, rule);
        jc_sb_append_char(&add, '\n');
        added++;
    }
    if (added == 0) {
        jc_sb_free(&add);
        return 0;
    }
    jc_sb_init(&full);
    if (have_existing) {
        jc_sb_append(&full, existing);
        if (existing[0] != '\0' && existing[strlen(existing) - 1] != '\n') {
            jc_sb_append_char(&full, '\n');
        }
    } else {
        jc_sb_append(&full, "# Project rules\n");
    }
    if (!have_existing || strstr(existing, "## Learned conventions") == NULL) {
        jc_sb_append(&full, "\n## Learned conventions\n\n");
    }
    jc_sb_append(&full, add.data != NULL ? add.data : "");
    if (jc_write_file(path, full.data != NULL ? full.data : "", full.len)
            != JC_OK) {
        added = 0;
    }
    jc_sb_free(&full);
    jc_sb_free(&add);
    return added;
}

void jc_learn_draft_path(const struct jc_app *app, char *out, jc_size cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (app == NULL) {
        out[0] = '\0';
        return;
    }
    jc_snprintf(out, cap, "%s/.jichi/lessons.draft.md", app->cwd);
}

jc_status jc_learn_apply(struct jc_app *app, unsigned sections, int force,
                         struct jc_learn_apply_stats *st, struct jc_sb *detail)
{
    char draft[1100];
    char *text = NULL;
    struct jc_learn_draft d;
    struct jc_arena *scratch;
    jc_size i;
    int touched_memory = 0;

    if (st == NULL || app == NULL) {
        return JC_ERR_INVALID;
    }
    memset(st, 0, sizeof(*st));
    st->sections = sections;

    jc_learn_draft_path(app, draft, sizeof(draft));
    /* Per-turn scratch, not the session arena: in the TUI this runs once per
     * /learn apply for the life of the process, and the draft text plus every
     * parsed string is consumed before this function returns (the arena rules in
     * CLAUDE.md -- the M197/M198/M199 bug class). */
    scratch = jc_app_scratch(app);
    if (jc_read_file(draft, &text, NULL, scratch) != JC_OK || text == NULL) {
        return JC_ERR_NOTFOUND;
    }
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(text, scratch, &d);

    if ((sections & JC_LEARN_MEMORY) != 0) {
        for (i = 0; i < d.memory.len; i++) {
            const char *note = *(char **)jc_vec_at(&d.memory, i);
            int was_new = 0;
            if (jc_memory_add(app, note, &was_new) == JC_OK && was_new) {
                st->memory_added++;
                touched_memory = 1;
            }
        }
    }
    if ((sections & JC_LEARN_SKILLS) != 0) {
        for (i = 0; i < d.skills.len; i++) {
            const struct jc_learn_skill *sk =
                (const struct jc_learn_skill *)jc_vec_at(&d.skills, i);
            char slug[96];
            char dir[1200];
            char path[1320];
            struct jc_sb sb;
            learn_slug(sk->name, slug, sizeof(slug));
            if (slug[0] == '\0') {
                continue;
            }
            jc_snprintf(dir, sizeof(dir), "%s/.jichi/skills/%s", app->cwd, slug);
            jc_snprintf(path, sizeof(path), "%s/SKILL.md", dir);
            if (!force && jc_file_exists(path)) {
                st->skills_skipped++;
                if (detail != NULL) {
                    jc_sb_append_fmt(detail, "  skill '%s' exists; skipping "
                                     "(use --force)\n", slug);
                }
                continue;
            }
            jc_mkdir_p(dir);
            jc_sb_init(&sb);
            jc_sb_append_fmt(&sb, "---\nname: %s\ndescription: %s\n---\n%s\n",
                             slug,
                             sk->description != NULL ? sk->description : "",
                             sk->body != NULL ? sk->body : "");
            if (jc_write_file(path, sb.data != NULL ? sb.data : "", sb.len)
                    == JC_OK) {
                st->skills_added++;
            }
            jc_sb_free(&sb);
        }
    }
    /* Corrections (M78): supersede now-stale memory notes -- the loop's way of
     * *correcting*, not only teaching. */
    if ((sections & JC_LEARN_CORRECTIONS) != 0) {
        st->corrections_malformed = d.corrections_malformed;
        for (i = 0; i < d.corrections.len; i++) {
            const struct jc_learn_correction *c =
                (const struct jc_learn_correction *)jc_vec_at(&d.corrections, i);
            int changed = 0;
            int rchanged;
            /* M601: the same directive retracts a learned convention in the
             * rules file -- the store the loop appends to but could not, until
             * now, take anything back from. Applied first so a match in both
             * places is reported in both. */
            rchanged = learn_rules_apply_correction(app, c->match,
                                                    c->replacement);
            if (rchanged > 0) {
                st->rules_retracted++;
                if (detail != NULL) {
                    jc_sb_append_fmt(detail, "  retracted learned convention: "
                                     "%s \"%s\"\n",
                                     c->replacement != NULL ? "replaced"
                                                            : "removed",
                                     c->match);
                }
            }
            if (jc_memory_correct(app, c->match, c->replacement, &changed)
                    == JC_OK && changed > 0) {
                st->corrections_applied++;
                touched_memory = 1;
                if (detail != NULL) {
                    jc_sb_append_fmt(detail, "  corrected memory: %s \"%s\"%s%s\n",
                                     c->replacement != NULL ? "replaced"
                                                            : "removed",
                                     c->match,
                                     c->replacement != NULL ? " => " : "",
                                     c->replacement != NULL ? c->replacement
                                                            : "");
                }
            } else if (rchanged > 0) {
                /* Matched a convention and no memory note: not "unmatched". */
            } else {
                st->corrections_unmatched++;
                if (detail != NULL) {
                    /* M294: name the likely cause. Applying a draft twice is
                     * expected -- a `learn corrections` pass now, the rest later
                     * -- and on the second run every already-applied directive
                     * lands here. "no note matched" read as a broken draft; it
                     * usually means the correction already succeeded. */
                    jc_sb_append_fmt(detail, "  correction skipped: no note "
                                     "matches \"%s\" (already applied, or the "
                                     "substring does not occur in "
                                     ".jichi/memory.md)\n", c->match);
                }
            }
        }
    }
    /* M602: "## Checks" -> AUTHORED constraints, through the same scanner
     * `/constraints add` uses. A phrase the scanner cannot read is counted, not
     * guessed at: the vocabulary is six command keys, `do not use the tool X`
     * and `read-only`, and a lesson that does not fit it stays a memory note. */
    if ((sections & JC_LEARN_CHECKS) != 0) {
        st->checks_unsupported = d.checks_unsupported;
        for (i = 0; i < d.checks.len; i++) {
            const char *phrase = *(char **)jc_vec_at(&d.checks, i);
            int n = jc_app_constraints_adopt(app, phrase, 1 /* authored */);
            if (n > 0) {
                st->checks_added += n;
                if (detail != NULL) {
                    jc_sb_append_fmt(detail, "  authored constraint: %s\n", phrase);
                }
            } else {
                st->checks_unrecognised++;
                if (detail != NULL) {
                    jc_sb_append_fmt(detail, "  check skipped: the constraint "
                                     "scanner does not read \"%s\"\n", phrase);
                }
            }
        }
    }
    /* M106: durable project rules -> AGENTS.md. */
    if ((sections & JC_LEARN_RULES) != 0) {
        st->rules_added = learn_apply_rules(app, &d.rules);
        if (st->rules_added > 0 && detail != NULL) {
            /* M533: name the file actually written -- it may be CLAUDE.md. */
            jc_sb_append_fmt(detail, "  wrote %d project rule(s) to the rules file\n",
                             st->rules_added);
        }
    }

    /* M294: what the mask left on the table. A corrections-only run is a partial
     * apply BY DESIGN, so the caller can tell the user the rest of the draft is
     * still waiting rather than letting them assume it was all committed. */
    if ((sections & JC_LEARN_MEMORY) == 0) {
        st->pending_other += (int)d.memory.len;
    }
    if ((sections & JC_LEARN_CHECKS) == 0) {
        st->pending_other += (int)d.checks.len;
    }
    if ((sections & JC_LEARN_SKILLS) == 0) {
        st->pending_other += (int)d.skills.len;
    }
    if ((sections & JC_LEARN_CORRECTIONS) == 0) {
        st->pending_other += (int)d.corrections.len;
    }
    if ((sections & JC_LEARN_RULES) == 0) {
        st->pending_other += (int)d.rules.len;
    }

    /* The draft has content but no parseable sections -- the mentor's raw output
     * often needs light curation into the strict format. Judged on the DRAFT, not
     * on the mask: a corrections-only run over a draft that only proposes memory
     * notes applied nothing, but the draft parsed fine. */
    if (st->memory_added == 0 && st->skills_added == 0 &&
        st->skills_skipped == 0 && st->corrections_applied == 0 &&
        st->corrections_unmatched == 0 && st->rules_added == 0 &&
        d.memory.len == 0 && d.skills.len == 0 && d.corrections.len == 0 &&
        d.rules.len == 0 && text[0] != '\0') {
        st->parsed_nothing = 1;
    }
    jc_learn_draft_free(&d);

    /* The refresh this whole extraction exists for. jc_memory_add does not do it
     * (the `remember` tool calls it separately), so without this a session keeps
     * the superseded notes -- and no second process could ever fix that. */
    if (touched_memory) {
        jc_memory_refresh(app);
    }
    /* Newly written SKILL.md files are invisible to the catalog in the system
     * prompt until reloaded. app->arena is the right LIFETIME (skills live as
     * long as the session) but does not reclaim the previous load; bounded here
     * because this is a rare, human-initiated action. */
    if (st->skills_added > 0) {
        jc_skill_load(&app->skills, app->cwd, app->arena);
    }
    return JC_OK;
}

void jc_learn_apply_summary(const struct jc_learn_apply_stats *st,
                            const char *draft_path, struct jc_sb *out)
{
    const char *path;
    if (st == NULL || out == NULL) {
        return;
    }
    path = (draft_path != NULL) ? draft_path : ".jichi/lessons.draft.md";
    if (st->parsed_nothing) {
        jc_sb_append_fmt(out, "Applied nothing: %s has no '## Memory notes' "
                         "bullets or '## Skills' (### name: desc) sections. ",
                         path);
        jc_sb_append(out, "Edit the draft into those exact headings (keep the "
                     "lessons worth keeping), then re-run. See "
                     "docs/LEARNING.md.\n");
        return;
    }
    /* Report only the sections that were ASKED for, so a corrections-only run
     * does not claim "0 skill(s)" about work it never attempted (M294).
     *
     * The full-mask sentence is byte-for-byte what `learn apply` has always
     * printed, Oxford-comma "and" included, because tests/smoke/learn.sh pins it
     * -- so the fragments are joined rather than each terminated. Dropping the
     * "and" would have been a gratuitous wording change that broke a green
     * driver, which is not a trade this milestone needs to make. */
    {
        char frag[5][64]; /* M602: + checks */
        int nfrag = 0;
        int i;
        if ((st->sections & JC_LEARN_MEMORY) != 0) {
            jc_snprintf(frag[nfrag++], sizeof(frag[0]), "%d memory note(s)",
                        st->memory_added);
        }
        if ((st->sections & JC_LEARN_SKILLS) != 0) {
            jc_snprintf(frag[nfrag++], sizeof(frag[0]), "%d skill(s)",
                        st->skills_added);
        }
        if ((st->sections & JC_LEARN_CORRECTIONS) != 0) {
            jc_snprintf(frag[nfrag++], sizeof(frag[0]), "%d correction(s)",
                        st->corrections_applied);
        }
        /* The joined sentence follows; M600's malformed-corrections line is
         * appended after it (see below) so the pinned sentence stays byte-exact. */
        if ((st->sections & JC_LEARN_RULES) != 0) {
            jc_snprintf(frag[nfrag++], sizeof(frag[0]), "%d rule(s)",
                        st->rules_added);
        }
        if ((st->sections & JC_LEARN_CHECKS) != 0 &&
            (st->checks_added + st->checks_unrecognised +
             st->checks_unsupported) > 0) {
            /* Only when the draft HAD a Checks section: the four-fragment
             * sentence tests/smoke/learn.sh pins must not grow a fifth. */
            jc_snprintf(frag[nfrag++], sizeof(frag[0]), "%d check(s)",
                        st->checks_added);
        }
        if (nfrag == 0) {
            /* An empty mask applied nothing and should say so plainly rather
             * than render "Applied  from <path>." */
            jc_sb_append_fmt(out, "Applied nothing (no sections selected) from "
                             "%s.\n", path);
            return;
        }
        jc_sb_append(out, "Applied ");
        for (i = 0; i < nfrag; i++) {
            if (i > 0) {
                jc_sb_append(out, (nfrag == 2) ? " and " : ", ");
                if (nfrag > 2 && i == nfrag - 1) {
                    jc_sb_append(out, "and ");
                }
            }
            jc_sb_append(out, frag[i]);
        }
        jc_sb_append_fmt(out, " from %s.\n", path);
        /* M294: a masked run is a PARTIAL apply by design. Saying what is still
         * waiting is the difference between "corrections done" and the user
         * believing the whole draft was committed. The draft is deliberately NOT
         * rewritten (see docs/LEARNING.md), so this is the only signal. */
        if (st->pending_other > 0) {
            jc_sb_append_fmt(out, "%d other draft item(s) not applied by this "
                             "command -- run `learn apply` for the rest.\n",
                             st->pending_other);
        }
        /* M600: a Corrections bullet that is prose retracts nothing, and used to
         * vanish without a count. The zigodot draft measured on 2026-08-27 had a
         * whole "## Corrections" section of such bullets -- each saying a note
         * "remains valid" -- and `learn apply` would have reported 0 corrections
         * as if the section were empty. Name the syntax the parser needs. */
        if ((st->sections & JC_LEARN_CHECKS) != 0 &&
            st->checks_unrecognised > 0) {
            jc_sb_append_fmt(out, "%d check(s) skipped: the constraint scanner "
                             "reads `do not run the build|tests|commit|push|"
                             "deploy|install`, `do not use the tool <name>` and "
                             "`read-only` -- rephrase, or keep the lesson as a "
                             "memory note.\n", st->checks_unrecognised);
        }
        if ((st->sections & JC_LEARN_CHECKS) != 0 &&
            st->checks_unsupported > 0) {
            jc_sb_append_fmt(out, "%d check(s) of a kind apply cannot commit "
                             "(only `constraint:` is built; hooks live in "
                             "config.json -- see DEFERRED.md).\n",
                             st->checks_unsupported);
        }
        if ((st->sections & JC_LEARN_CORRECTIONS) != 0 &&
            st->rules_retracted > 0) {
            jc_sb_append_fmt(out, "%d learned convention(s) retracted from the "
                             "rules file by the same directives.\n",
                             st->rules_retracted);
        }
        if ((st->sections & JC_LEARN_CORRECTIONS) != 0 &&
            st->corrections_malformed > 0) {
            jc_sb_append_fmt(out, "%d correction bullet(s) ignored: not "
                             "`remove: <substring>` or `replace: <substring> => "
                             "<new note>`, so they retract nothing -- rewrite "
                             "them as directives or delete them.\n",
                             st->corrections_malformed);
        }
    }
}
