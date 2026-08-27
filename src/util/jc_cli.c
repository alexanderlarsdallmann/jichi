/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_cli.c - small pure CLI helpers (see jc_cli.h). */

#include "jc_utf8.h"
#include "jc_cli.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_perm.h"
#include "cJSON.h"

#include <string.h>

int jc_output_format_parse(const char *s, int *out_json)
{
    if (s == NULL) {
        return -1;
    }
    if (strcmp(s, "text") == 0) {
        *out_json = 0;
        return 0;
    }
    if (strcmp(s, "json") == 0) {
        *out_json = 1;
        return 0;
    }
    if (strcmp(s, "jsonl") == 0) {
        *out_json = 2;   /* streaming JSON events (one object per line) */
        return 0;
    }
    return -1;
}

/* M439: the same UTF-8 boundary rule as jc_agentjson_preview. This fed
 * jc_tool_arg_summary, whose result reaches stderr AND the telemetry `args` field,
 * and a byte cut through a multi-byte path or query put invalid UTF-8 in both. It
 * was found while fixing the jsonl preview -- one bug class, two call sites, and
 * the second was not in the register because nobody had looked. */
static void copy_trunc(char *dst, jc_size cap, const char *s)
{
    jc_size n;
    if (cap == 0) return;
    n = (jc_size)strlen(s);
    if (n > cap - 1) n = jc_utf8_trunc_len(s, cap - 1);
    if (n > 0) memcpy(dst, s, n);
    dst[n] = '\0';
}

/* M571: WHAT A TOOL CALL IS CALLED, in one bounded line.
 *
 * THE DEFECT, reported by an operator listening to a screen reader. The old
 * fallback was `copy_trunc(buf, cap, args_json)` -- the RAW ARGUMENT JSON --
 * and it was reached far more often than anyone had checked. It looked like:
 *
 *   Calling the tool apply_patch, with {"edits": [{"path": "...", "old_string":
 *   "static void greet(const char *who)\n{\n    printf(\"hello, %s\\n\", who);
 *
 * Spoken, that is braces, quotes, "backslash n", and escaped escapes. The worst
 * chrome in the product for a listener, and it announced two of the tools the
 * model reaches for most.
 *
 * MEASURED UNIVERSE, not the two tools that happened to be reported: of 43
 * registered tools, 17 have no top-level key from the list below. Many of those
 * take no arguments at all and are harmless (an empty summary just yields
 * "Calling the tool git_status."), but the ones that carry a payload were all
 * dumping it: ask_user and ask_for_help read `question`, remember reads `note`,
 * board reads `note`/`title`, and apply_patch's path is NESTED inside `edits[]`
 * where no top-level lookup could ever find it.
 *
 * THREE STAGES, in order of how well they read:
 *   1. a known scalar key -- the value itself, which is what a human would say
 *   2. an ARRAY OF OBJECTS naming targets -- "<first path> and N more", which is
 *      what apply_patch needs and what any future batch tool will need
 *   3. the KEY NAMES ONLY, never the values -- so an unrecognised shape is
 *      announced as "todos" rather than read out as JSON
 *
 * Raw arguments stay reachable ON PURPOSE, for whoever actually wants them:
 * `-v` prints them in the headless path, and the approval prompt's view key
 * shows them in full. What changed is the DEFAULT, which is spoken aloud.
 *
 * `name` IS STILL UNUSED, and that is a decision rather than an oversight. My
 * first draft of this comment claimed the opposite -- that the tool name was now
 * read -- while the code below still says `(void)name;`. Stage 2 keys on the
 * SHAPE of the arguments, not on `strcmp(name, "apply_patch")`, so the tool name
 * genuinely is not needed; and keying on names would mean editing this function
 * every time a batch tool is added, which is the maintenance trap the shape test
 * avoids. The sentence was wrong before the code was, which is the same species
 * of defect this project keeps finding in its own test headers. */
void jc_tool_arg_summary(const char *name, const char *args_json,
                         char *buf, jc_size cap)
{
    /* Content-bearing keys first, then the generic ones: a tool carrying both
     * `question` and `name` should announce the question. */
    static const char *keys[] = {
        "path", "command", "query", "pattern", "symbol", "url",
        "question", "note", "title", "name", "file", "task", 0
    };
    cJSON *o;
    int i;

    (void)name;
    if (cap == 0) return;
    buf[0] = '\0';
    if (args_json == NULL || args_json[0] == '\0') return;
    o = jc_json_parse(args_json);
    if (o == NULL) {
        return;   /* unparseable: say nothing rather than read bytes aloud */
    }
    for (i = 0; keys[i] != 0; i++) {
        const char *v = jc_json_get_str(o, keys[i], NULL);
        if (v != NULL && v[0] != '\0') {
            copy_trunc(buf, cap, v);
            break;
        }
    }
    /* Stage 2: the first array of objects that names a target. Generic rather
     * than a strcmp on "apply_patch", because the shape is what matters -- a
     * batch of edits, each with a path -- and the next batch tool will have it
     * too. */
    if (buf[0] == '\0') {
        cJSON *m;
        for (m = o->child; m != NULL; m = m->next) {
            if (cJSON_IsArray(m) && cJSON_GetArraySize(m) > 0) {
                cJSON *first = cJSON_GetArrayItem(m, 0);
                const char *tgt = NULL;
                if (first != NULL && cJSON_IsObject(first)) {
                    tgt = jc_json_get_str(first, "path", NULL);
                    if (tgt == NULL) {
                        tgt = jc_json_get_str(first, "file", NULL);
                    }
                }
                if (tgt != NULL && tgt[0] != '\0') {
                    int n = cJSON_GetArraySize(m);
                    if (n == 1) {
                        copy_trunc(buf, cap, tgt);
                    } else {
                        char tmp[220];
                        jc_snprintf(tmp, sizeof tmp, "%s and %d more", tgt,
                                    n - 1);
                        copy_trunc(buf, cap, tmp);
                    }
                    break;
                }
            }
        }
    }
    /* Stage 3: the shape, by key name. Values are deliberately excluded -- the
     * whole defect was values being read aloud. */
    if (buf[0] == '\0') {
        cJSON *m;
        char tmp[220];
        jc_size used = 0;
        tmp[0] = '\0';
        for (m = o->child; m != NULL; m = m->next) {
            jc_size need;
            if (m->string == NULL || m->string[0] == '\0') continue;
            need = strlen(m->string) + 2;
            if (used + need >= sizeof tmp) break;
            if (used > 0) {
                tmp[used++] = ',';
                tmp[used++] = ' ';
            }
            memcpy(tmp + used, m->string, strlen(m->string));
            used += strlen(m->string);
            tmp[used] = '\0';
        }
        if (tmp[0] != '\0') {
            copy_trunc(buf, cap, tmp);
        }
    }
    cJSON_Delete(o);
}

const char *jc_model_short_name(const char *name)
{
    const char *p;
    const char *slash = NULL;

    if (name == NULL) return "";
    for (p = name; *p != '\0'; p++) {
        if (*p == '/') slash = p;
    }
    return (slash != NULL) ? slash + 1 : name;
}

jc_size jc_model_display(const char *name, const char *id, char *out,
                         jc_size cap)
{
    const char *shortnm;

    if (out == NULL || cap == 0) {
        return 0;
    }
    if (name != NULL && name[0] == '\0') {
        name = NULL;
    }
    if (id != NULL && id[0] == '\0') {
        id = NULL;
    }
    if (name == NULL && id == NULL) {
        return (jc_size)jc_snprintf(out, cap, "?");
    }
    if (id == NULL) {
        return (jc_size)jc_snprintf(out, cap, "%s",
                                    jc_model_short_name(name));
    }
    if (name == NULL) {
        return (jc_size)jc_snprintf(out, cap, "%s", id);
    }
    /* Same string twice carries nothing. Two forms of that: the config named the
     * model after its own wire id, or the shortened name has collapsed onto it. */
    shortnm = jc_model_short_name(name);
    if (strcmp(name, id) == 0 || strcmp(shortnm, id) == 0) {
        return (jc_size)jc_snprintf(out, cap, "%s", id);
    }
    return (jc_size)jc_snprintf(out, cap, "%s (%s)", shortnm, id);
}

int jc_color_enabled(int mode, int is_tty)
{
    if (mode < 0) return is_tty ? 1 : 0;
    return mode ? 1 : 0;
}

const char *jc_mode_color(int mode)
{
    switch (mode) {
    case JC_MODE_CHAT: return "\x1b[32m"; /* green: safe (asks before changes) */
    case JC_MODE_PLAN: return "\x1b[34m"; /* blue: read-only planning          */
    case JC_MODE_AUTO: return "\x1b[33m"; /* yellow: runs tools unattended      */
    default:           return "\x1b[36m"; /* cyan                               */
    }
}

void jc_fmt_elapsed(double secs, char *buf, jc_size cap)
{
    long t;

    if (cap == 0) {
        return;
    }
    if (!(secs >= 0.0)) {
        secs = 0.0;   /* negatives and NaN both fail the test above */
    }
    if (secs < 60.0) {
        jc_snprintf(buf, cap, "%.1fs", secs);
        return;
    }
    t = (long)secs;   /* floor: a stopwatch shows 1m 29s until 90 arrives */
    if (t < 3600) {
        jc_snprintf(buf, cap, "%ldm %02lds", t / 60, t % 60);
        return;
    }
    jc_snprintf(buf, cap, "%ldh %02ldm", t / 3600, (t % 3600) / 60);
}

void jc_reltime(long delta_secs, char *buf, jc_size cap)
{
    long n;
    const char *unit;

    if (cap == 0) return;
    if (delta_secs < 0) delta_secs = 0;
    if (delta_secs < 45) {
        jc_snprintf(buf, cap, "just now");
        return;
    }
    if (delta_secs < 3600) { n = delta_secs / 60; unit = "m"; }
    else if (delta_secs < 86400) { n = delta_secs / 3600; unit = "h"; }
    else if (delta_secs < 7L * 86400) { n = delta_secs / 86400; unit = "d"; }
    else { n = delta_secs / (7L * 86400); unit = "w"; }
    if (n < 1) n = 1;
    jc_snprintf(buf, cap, "%ld%s ago", n, unit);
}

int jc_text_is_context_overflow(const char *s)
{
    static const char *sigs[] = {
        "n_ctx",                            /* llama.cpp / LM Studio */
        "than the context length",          /* llama.cpp full phrase */
        "Context size has been exceeded",   /* terse llama.cpp variant */
        "context_length_exceeded",          /* OpenAI error code */
        "maximum context length",           /* OpenAI message */
        "exceeds the model's context"       /* misc OpenAI-compatible */
    };
    int i;
    int n = (int)(sizeof(sigs) / sizeof(sigs[0]));

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (strstr(s, sigs[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

int jc_id_prefix_unique(const char *const *ids, int n, const char *prefix)
{
    jc_size plen;
    int i;
    int hit = -1;

    if (ids == NULL || prefix == NULL || prefix[0] == '\0') return -1;
    plen = (jc_size)strlen(prefix);
    for (i = 0; i < n; i++) {
        if (ids[i] == NULL) continue;
        if (strcmp(ids[i], prefix) == 0) return i;       /* exact wins */
        if (strncmp(ids[i], prefix, plen) == 0) {
            if (hit >= 0) hit = -2;                       /* ambiguous */
            else if (hit == -1) hit = i;
        }
    }
    return hit;
}

int jc_interrupt_should_exit(int consecutive)
{
    /* First empty-prompt Ctrl-C cancels and stays; a second consecutive one
     * quits (so the muscle-memory "Ctrl-C twice to leave" still works). */
    return consecutive >= 2;
}

jc_size jc_tui_indicator(int n_constraints, int n_bg, int n_todos,
                         int unicode, char *buf, jc_size cap)
{
    jc_size o = 0;
    int first = 1;
    struct { int n; const char *sing; const char *plur; } parts[3];
    const char *sep = unicode ? " \xc2\xb7 " : " / "; /* · vs / */
    int i;

    if (buf == NULL || cap == 0) return 0;
    buf[0] = '\0';
    if (n_constraints <= 0 && n_bg <= 0 && n_todos <= 0) return 0;

    parts[0].n = n_constraints; parts[0].sing = "constraint";
    parts[0].plur = "constraints";
    parts[1].n = n_bg; parts[1].sing = "job"; parts[1].plur = "jobs";
    parts[2].n = n_todos; parts[2].sing = "todo"; parts[2].plur = "todos";

    o += (jc_size)jc_snprintf(buf + o, cap - o, "%s ",
                              unicode ? "\xe2\x97\x86" : "*"); /* ◆ / * */
    for (i = 0; i < 3; i++) {
        if (parts[i].n <= 0) continue;
        o += (jc_size)jc_snprintf(buf + o, cap - o, "%s%d %s",
            first ? "" : sep, parts[i].n,
            parts[i].n == 1 ? parts[i].sing : parts[i].plur);
        first = 0;
        if (o >= cap - 1) break;
    }
    return o;
}

jc_size jc_os_line(const char *sysname, const char *release, const char *machine,
                   jc_size max_cols, char *out, jc_size cap)
{
    jc_size fixed;
    jc_size rlen;
    jc_size budget;
    char rel[128];

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (sysname == NULL || sysname[0] == '\0') {
        sysname = "?";
    }
    if (release == NULL) {
        release = "";
    }
    if (machine != NULL && machine[0] == '\0') {
        machine = NULL;
    }

    /* Everything the line costs apart from the release itself: the two-space
     * indent the caller prints, the sysname, the space before the release, and
     * " (machine)" when there is one. */
    fixed = 2 + (jc_size)strlen(sysname) + 1;
    if (machine != NULL) {
        fixed += 2 + (jc_size)strlen(machine) + 1;
    }

    rlen = (jc_size)strlen(release);
    budget = (max_cols > fixed) ? (max_cols - fixed) : 0;
    if (rlen <= budget || budget < 4) {
        /* Fits, or there is no room even for a marked truncation -- in which
         * case truncate nothing here and let the bounded write below clip,
         * rather than emit a "..." that is most of the field. */
        jc_snprintf(rel, sizeof(rel), "%s", release);
    } else {
        jc_size keep = budget - 3;      /* room for "..." */
        if (keep >= sizeof(rel)) {
            keep = sizeof(rel) - 1;
        }
        memcpy(rel, release, keep);
        rel[keep] = '\0';
        jc_snprintf(rel + keep, sizeof(rel) - keep, "...");
    }

    if (machine != NULL) {
        return jc_snprintf(out, cap, "%s %s (%s)", sysname, rel, machine);
    }
    return jc_snprintf(out, cap, "%s %s", sysname, rel);
}
