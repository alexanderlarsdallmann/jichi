/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_parallel.c - pure helpers for the parallel agent swarm (see jc_parallel.h).
 *
 * No process, file, or network state here; the spawn_parallel tool
 * (jc_tool_parallel.c) drives these to size its fork pool, read each worktree's
 * change list, and resolve the file-level merge. */

#include "jc_parallel.h"
#include "jc_envelope.h"   /* M865: jc_env_test_assertion_edit */
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <limits.h>   /* M875: LONG_MAX bounds the metric's cast */
#include <math.h>

#include <string.h>

void jc_parallel_board_line(char *buf, jc_size cap, int idx, const char *model,
                            int state, const char *detail, double tokens)
{
    const char *tag;
    char tok[24];

    if (buf == NULL || cap == 0) {
        return;
    }
    switch (state) {
    case JC_BOARD_DONE:    tag = "done"; break;
    case JC_BOARD_FAIL:    tag = "FAIL"; break;
    case JC_BOARD_TIMEOUT: tag = "time"; break;
    default:               tag = "run "; break;
    }
    tok[0] = '\0';
    if (tokens > 0.0) {
        jc_snprintf(tok, sizeof(tok), " (%.1fk)", tokens / 1000.0);
    }
    jc_snprintf(buf, cap, "[%d] %s %-12s %s%s", idx + 1, tag,
                model != NULL ? model : "?",
                detail != NULL ? detail : "", tok);
}

int jc_parallel_eff_max(int n_tasks, int cfg_max, int cpu, int ceiling)
{
    int autocap = (cpu < ceiling) ? cpu : ceiling;
    int cap;
    int r;

    if (autocap < 1) {
        autocap = 1;
    }
    cap = (cfg_max > 0) ? cfg_max : autocap;
    r = (n_tasks < cap) ? n_tasks : cap;
    if (r < 1) {
        r = 1;
    }
    return r;
}

void jc_parallel_parse_changes(const char *text, struct jc_arena *a,
                               struct jc_vec *out)
{
    const char *p;

    if (text == NULL) {
        return;
    }
    p = text;
    while (*p != '\0') {
        const char *line = p;
        const char *nl = strchr(p, '\n');
        jc_size linelen = (nl != NULL) ? (jc_size)(nl - line)
                                       : (jc_size)strlen(line);
        const char *tab;

        p = (nl != NULL) ? nl + 1 : line + linelen;

        if (linelen < 2) {
            continue;
        }
        tab = (const char *)memchr(line, '\t', linelen);
        if (tab == NULL) {
            continue;
        }
        {
            const char *pathstart = tab + 1;
            jc_size remain = (jc_size)((line + linelen) - pathstart);
            struct jc_change ch;

            while (remain > 0 && pathstart[remain - 1] == '\r') {
                remain--;
            }
            if (remain == 0) {
                continue;
            }
            ch.status = line[0];
            ch.path = jc_arena_strndup(a, pathstart, remain);
            jc_vec_push(out, &ch);
        }
    }
}

/* Bounded copy that leaves the buffer EMPTY for a NULL source -- the M437 fields
 * are optional on the wire, and "absent" must not become "the empty string means
 * something". */
static void cp(char *dst, jc_size cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (src != NULL) {
        jc_snprintf(dst, cap, "%s", src);
    }
}

int jc_parallel_parse_msg(const char *line, struct jc_pmsg *out)
{
    cJSON *o;
    const char *t;

    memset(out, 0, sizeof(*out));
    if (line == NULL) {
        return JC_PMSG_NONE;
    }
    o = jc_json_parse(line);
    if (o == NULL) {
        return JC_PMSG_NONE;
    }
    t = jc_json_get_str(o, "t", "");
    if (strcmp(t, "tool") == 0) {
        out->kind = JC_PMSG_TOOL;
        jc_snprintf(out->tool, sizeof(out->tool), "%s",
                    jc_json_get_str(o, "name", "?"));
    } else if (strcmp(t, "tok") == 0) {
        out->kind = JC_PMSG_TOK;
        out->tokens = jc_json_get_num(o, "n", 0.0);
    } else if (strcmp(t, "done") == 0) {
        const char *ans = jc_json_get_str(o, "answer", NULL);
        const char *err = jc_json_get_str(o, "error", NULL);
        out->kind = JC_PMSG_DONE;
        out->tokens = jc_json_get_num(o, "tokens", 0.0);
        out->tool_calls = jc_json_clamp_int(jc_json_get_num(o, "tools", 0.0));
        /* M437: absent fields leave the buffers empty, which the parent reads as
         * "the child did not report" -- so an older child message degrades to the
         * pre-M437 behaviour instead of asserting a stop reason it never sent. */
        cp(out->stop, sizeof out->stop, jc_json_get_str(o, "stop", NULL));
        cp(out->ftool, sizeof out->ftool, jc_json_get_str(o, "ftool", NULL));
        cp(out->fmsg, sizeof out->fmsg, jc_json_get_str(o, "fmsg", NULL));
        out->fcls = jc_json_clamp_int(jc_json_get_num(o, "fcls", 0.0));
        out->repeated = jc_json_clamp_int(jc_json_get_num(o, "repeated", 0.0));  /* M808 */
        out->fence_hints = jc_json_clamp_int(jc_json_get_num(o, "fhints", 0.0)); /* M852 */
        out->term = jc_json_clamp_int(jc_json_get_num(o, "term", 0.0));          /* M854 */
        out->overflow = jc_json_clamp_int(jc_json_get_num(o, "overflow", 0.0));  /* M854 */
        out->cut = jc_json_clamp_int(jc_json_get_num(o, "cut", 0.0));            /* M858 */
        out->tused = jc_json_clamp_int(jc_json_get_num(o, "tused", 0.0));        /* M861 */
        out->looped = jc_json_clamp_int(jc_json_get_num(o, "looped", 0.0));      /* M869 */
        out->fcount = jc_json_clamp_int(jc_json_get_num(o, "fcount", 0.0));      /* M876 */
        out->fstop = jc_json_clamp_int(jc_json_get_num(o, "fstop", 0.0));        /* M885 */
        cp(out->rerr, sizeof out->rerr, jc_json_get_str(o, "rerr", NULL));
        out->rerrn = jc_json_clamp_int(jc_json_get_num(o, "rerrn", 0.0));
        out->rerr0 = jc_json_clamp_long(jc_json_get_num(o, "rerr0", 0.0));
        out->rerr1 = jc_json_clamp_long(jc_json_get_num(o, "rerr1", 0.0));
        out->npinned = jc_json_clamp_int(jc_json_get_num(o, "npinned", 0.0));    /* M863 */
        cp(out->pinned, sizeof out->pinned, jc_json_get_str(o, "pinned", NULL));
        {
            const char *last = jc_json_get_str(o, "last", NULL);   /* M854 */
            if (last != NULL) {
                out->last = jc_strdup(last);
            }
        }
        if (ans != NULL) {
            out->answer = jc_strdup(ans);
        }
        if (err != NULL) {
            out->error = jc_strdup(err);
        }
    }
    cJSON_Delete(o);
    return out->kind;
}

const char *jc_parallel_ending(int write, int has_worktree, int finished,
                               int batch_aborted, const char *stop, int term,
                               int overflow, int cut)
{
    if (!write || !has_worktree || !finished || batch_aborted) {
        return NULL;
    }
    if (stop == NULL) {
        stop = "";
    }
    if (term && strcmp(stop, "aborted") == 0) {
        return "was stopped from outside (SIGTERM)";
    }
    if (overflow) {
        return "ran out of context (a request exceeded the model's window)";
    }
    /* M858 (N42): a turn that ended on a reply cut at the output ceiling --
     * run 0090's worker, 331 calls, ended mid-sentence and its text became its
     * answer. A cut tool call is not an ending (the loop goes on). */
    if (cut && (strcmp(stop, "done") == 0 || strcmp(stop, "no_answer") == 0)) {
        return "had its last reply cut off at the output ceiling";
    }
    if (strcmp(stop, "max_iters") == 0) {
        return "stopped at its iteration cap";
    }
    /* M861 (N28): a budget stop is not an answer the child chose to give either.
     * Its own max_tool_calls, or its slice of the run's envelope -- before M861
     * that one merged unverified whenever the child had narrated, the merge
     * question N28 asked and the branch review answered as a defect. */
    if (strcmp(stop, "task_budget") == 0) {
        return "reached the max_tool_calls set for it";
    }
    if (strcmp(stop, "budget") == 0) {
        return "reached its share of the run's budget";
    }
    return NULL;
}

const char *jc_parallel_loop_ending(int looped)
{
    switch (looped) {
    case 1:  return "was stopped for repeating the same successful call";
    case 2:  return "was stopped for repeating the same refused call";
    case 3:  return "was stopped for copying placeholders instead of arguments";
    case 4:  return "was stopped for cycling through the same calls with nothing new";
    default: return NULL;
    }
}

int jc_parallel_parse_max_calls(const struct cJSON *v, int *out)
{
    const cJSON *j = (const cJSON *)v;
    double d;

    if (j == NULL || cJSON_IsNull(j)) {
        return 0;
    }
    if (cJSON_IsNumber(j)) {
        d = j->valuedouble;
        if (!(d >= 1.0) || d != floor(d)) {
            return -1;   /* 0, negative, NaN, or a fraction */
        }
    } else if (cJSON_IsString(j) && j->valuestring != NULL) {
        const char *p = j->valuestring;
        d = 0.0;
        if (*p == '\0') {
            return -1;
        }
        for (; *p != '\0'; p++) {
            if (*p < '0' || *p > '9') {
                return -1;
            }
            if (d <= (double)JC_PAR_MAX_CALLS_CAP) {
                d = d * 10.0 + (double)(*p - '0');
            }
        }
        if (d < 1.0) {
            return -1;
        }
    } else {
        return -1;
    }
    if (d > (double)JC_PAR_MAX_CALLS_CAP) {
        d = (double)JC_PAR_MAX_CALLS_CAP;
    }
    if (out != NULL) {
        *out = (int)d;
    }
    return 1;
}

/* M865: judge one hunk's removed and added lines with M88's predicate. */
static int hunk_edits_assertion(const char *path, struct jc_sb *minus,
                                struct jc_sb *plus)
{
    int r = 0;
    if (minus->len > 0 && plus->len > 0) {
        r = jc_env_test_assertion_edit(path, minus->data, plus->data);
    }
    jc_sb_free(minus);
    jc_sb_free(plus);
    jc_sb_init(minus);
    jc_sb_init(plus);
    return r;
}

int jc_parallel_diff_edits_assertion(const char *path, const char *diff)
{
    struct jc_sb minus;
    struct jc_sb plus;
    const char *p = diff;
    int hit = 0;

    if (path == NULL || diff == NULL) {
        return 0;
    }
    jc_sb_init(&minus);
    jc_sb_init(&plus);
    while (*p != '\0' && !hit) {
        const char *eol = strchr(p, '\n');
        jc_size n = (eol != NULL) ? (jc_size)(eol - p) : strlen(p);
        if (n >= 2 && p[0] == '@' && p[1] == '@') {
            hit = hunk_edits_assertion(path, &minus, &plus);
        } else if (n >= 3 && (strncmp(p, "---", 3) == 0 ||
                              strncmp(p, "+++", 3) == 0)) {
            /* the file header, not a line of the file */
        } else if (n >= 1 && p[0] == '-') {
            jc_sb_append_n(&minus, p + 1, n - 1);
            jc_sb_append(&minus, "\n");
        } else if (n >= 1 && p[0] == '+') {
            jc_sb_append_n(&plus, p + 1, n - 1);
            jc_sb_append(&plus, "\n");
        }
        p = (eol != NULL) ? eol + 1 : p + n;
    }
    if (!hit) {
        hit = hunk_edits_assertion(path, &minus, &plus);
    }
    jc_sb_free(&minus);
    jc_sb_free(&plus);
    return hit;
}

int jc_parallel_budget_note_at(int eff)
{
    if (eff < 5) {
        return 0;
    }
    return eff - eff / 5;
}

/* M862: does [s, s+n) carry one of the failure markers? Bounded, NUL-safe. */
static int vfail_marked(const char *s, jc_size n)
{
    static const char *const marks[] = {
        "FAIL", "fail", "Fail", "error:", "Error", "ERROR", "xpected", "panic",
        "ssert", "not ok", "Traceback", NULL
    };
    int k;
    for (k = 0; marks[k] != NULL; k++) {
        jc_size ml = strlen(marks[k]);
        jc_size i;
        if (ml > n) {
            continue;
        }
        for (i = 0; i + ml <= n; i++) {
            if (s[i] == marks[k][0] && memcmp(s + i, marks[k], ml) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* M862: append n bytes, a NUL as a space -- the excerpt joins the batch result as
 * a C string, which an embedded NUL would cut (the ticket keeps the raw bytes). */
static void append_clean(struct jc_sb *dst, const char *s, jc_size n)
{
    jc_size i = 0;
    while (i < n) {
        jc_size j = i;
        while (j < n && s[j] != '\0') {
            j++;
        }
        if (j > i) {
            jc_sb_append_n(dst, s + i, j - i);
        }
        if (j < n) {
            jc_sb_append(dst, " ");
            j++;
        }
        i = j;
    }
}

void jc_parallel_verify_excerpt(const char *out, jc_size len, jc_size tail,
                                struct jc_sb *dst)
{
    jc_size toff;
    jc_size p = 0;
    int shown = 0;
    int cut = 0;
    long more = 0;

    if (out == NULL || len == 0) {
        jc_sb_append(dst, "(no output)\n");
        return;
    }
    toff = (len > tail) ? len - tail : 0;
    /* Start the tail where its first line starts, when that is near: a line
     * cut in two is hard to read, and a failing line that straddled the cut
     * would be whole in neither part. A longer line stays cut, marked. */
    if (toff > 0) {
        jc_size b = toff;
        while (b > 0 && toff - b < JC_PAR_VFAIL_LINE_MAX && out[b - 1] != '\n') {
            b--;
        }
        if (b == 0 || out[b - 1] == '\n') {
            toff = b;
        } else {
            cut = 1;
        }
    }
    while (p < toff) {
        const char *nl = (const char *)memchr(out + p, '\n', len - p);
        jc_size e = (nl != NULL) ? (jc_size)(nl - out) : len;
        jc_size ll = e - p;
        if (e > toff) {
            break;   /* this line reaches into the tail, which shows it */
        }
        if (ll > 0 && vfail_marked(out + p, ll)) {
            if (shown < JC_PAR_VFAIL_LINES) {
                if (shown == 0) {
                    jc_sb_append(dst, "failing lines (before the tail):\n");
                }
                append_clean(dst, out + p,
                             ll > JC_PAR_VFAIL_LINE_MAX ? JC_PAR_VFAIL_LINE_MAX : ll);
                jc_sb_append(dst, ll > JC_PAR_VFAIL_LINE_MAX ? " ...\n" : "\n");
                shown++;
            } else {
                more++;
            }
        }
        p = e + 1;
    }
    if (more > 0) {
        jc_sb_append_fmt(dst, "(%ld more such line(s) before the tail)\n", more);
    }
    jc_sb_append(dst, cut ? "output tail:\n..." : "output tail:\n");
    append_clean(dst, out + toff, len - toff);
    if (out[len - 1] != '\n') {
        jc_sb_append(dst, "\n");
    }
}

void jc_parallel_budget_note(int used, int eff, struct jc_sb *out)
{
    jc_sb_append_fmt(out,
        "[jichi] You have used %d of the %d tool calls this task allows. Finish "
        "the step you are on, then end your turn with a short report: what is "
        "done, and what is left. At %d calls the task ends, and its work merges "
        "only after a green verify.", used, eff, eff);
}

int jc_parallel_claim(struct jc_vec *seen, const char *path)
{
    jc_size i;

    if (path == NULL) {
        return 0;
    }
    for (i = 0; i < seen->len; i++) {
        const char *s = *(char **)jc_vec_at(seen, i);
        if (s != NULL && strcmp(s, path) == 0) {
            return 0;
        }
    }
    jc_vec_push(seen, &path);
    return 1;
}

const char *jc_parallel_verify_cmd(const char *env_cmd,
                                   const char *config_verify,
                                   const char *test_command)
{
    if (env_cmd != NULL && env_cmd[0] != '\0') {
        return env_cmd;
    }
    if (config_verify != NULL && config_verify[0] != '\0') {
        return config_verify;
    }
    if (test_command != NULL && test_command[0] != '\0') {
        return test_command;
    }
    return NULL;
}

/* ---- the metric gate (M875) ------------------------------------------------
 * The demos drive's §9 (b), built beside M865's hold at the operator's decision
 * of 2026-10-02: a moved assertion in a declared pin file may merge when a
 * declared metric, measured in the child's worktree, is not worse than the
 * base's run of the same command. See docs/proposals/2026-10-metric-gate.md. */

static int mg_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int mg_digit(int c)
{
    return c >= '0' && c <= '9';
}

/* A number starts at p: a digit, or '-' and a digit, not inside a word ("L1",
 * "x86_64", "v1.2" hold none), and not right after a '.' (the fraction of one
 * already read). */
static int mg_num_at(const char *s, const char *p)
{
    int prev = (p > s) ? (unsigned char)p[-1] : 0;
    if (prev != 0 && (mg_word(prev) || prev == '.')) {
        return 0;
    }
    if (mg_digit((unsigned char)p[0])) return 1;
    return p[0] == '-' && mg_digit((unsigned char)p[1]);
}

/* Read the number at p into *v; returns the pointer just past it. */
static const char *mg_read(const char *p, double *v)
{
    double x = 0.0;
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    while (mg_digit((unsigned char)*p)) {
        x = x * 10.0 + (double)(*p - '0');
        p++;
    }
    if (*p == '.' && mg_digit((unsigned char)p[1])) {
        double f = 0.1;
        p++;
        while (mg_digit((unsigned char)*p)) {
            x += f * (double)(*p - '0');
            f /= 10.0;
            p++;
        }
    }
    *v = neg ? -x : x;
    return p;
}

int jc_parallel_metric_parse(const char *out, const char *label, double *v)
{
    const char *p;
    int found = 0;
    double last = 0.0;

    if (out == NULL || v == NULL) {
        return 0;
    }
    if (label != NULL && label[0] != '\0') {
        p = strstr(out, label);
        if (p == NULL) {
            return 0;
        }
        for (p += strlen(label); *p != '\0'; p++) {
            if (mg_num_at(out, p)) {
                (void)mg_read(p, v);
                return 1;
            }
        }
        return 0;
    }
    p = out;
    while (*p != '\0') {
        if (mg_num_at(out, p)) {
            p = mg_read(p, &last);
            found = 1;
            continue;
        }
        p++;
    }
    if (found) {
        *v = last;
    }
    return found;
}

int jc_parallel_metric_worse(double base, double value, int higher_better)
{
    return higher_better ? (value < base) : (value > base);
}

int jc_parallel_path_is_pin(const char *path, const struct jc_vec *pins)
{
    jc_size i;
    if (path == NULL || pins == NULL) {
        return 0;
    }
    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    for (i = 0; i < pins->len; i++) {
        const char *pin = *(const char *const *)jc_vec_at((struct jc_vec *)pins, i);
        if (pin == NULL) continue;
        if (pin[0] == '.' && pin[1] == '/') pin += 2;
        if (strcmp(pin, path) == 0 || jc_glob_match(pin, path)) {
            return 1;
        }
    }
    return 0;
}

void jc_parallel_metric_fmt(double v, char *buf, jc_size cap)
{
    double r = (v < 0.0) ? -v : v;
    if (r >= 1e15) {
        /* past what "%f" shows whole in a short buffer: a mantissa and an
         * exponent, never a number cut to a different one */
        char m[40];
        int e = 0;
        while (r >= 10.0 && e < 400) {
            r /= 10.0;
            e++;
        }
        jc_parallel_metric_fmt(v < 0.0 ? -r : r, m, sizeof m);
        jc_snprintf(buf, cap, "%se%d", m, e);
        return;
    }
    /* the cast only inside long's range: beyond it the conversion is
     * undefined (M866's class, which UBSan catches and gcc does not) */
    if (r < (double)LONG_MAX && (double)(long)v == v) {
        jc_snprintf(buf, cap, "%ld", (long)v);
    } else {
        jc_size n;
        jc_snprintf(buf, cap, "%f", v);
        n = (jc_size)strlen(buf);
        while (n > 1 && buf[n - 1] == '0' && strchr(buf, '.') != NULL) {
            buf[--n] = '\0';
        }
        if (n > 1 && buf[n - 1] == '.') {
            buf[n - 1] = '\0';
        }
    }
}

/* M927 (R04-F1): the NUL form. `git diff --name-status --no-renames -z`
 * records are "<status>\0<path>\0" -- never C-quoted, and a path containing a
 * newline, tab or quote is a path, not a line. Measured (2026-10-10): with
 * -z git emits raw bytes for every case set (café.md, new\nline.txt,
 * tab\there, quo"te, back\slash) whether or not core.quotepath is set, so
 * the flag is omitted. The line form above stays for the telemetry
 * files_changed field (M415 -- a stable wire value); a merge that consumed
 * it lost every such file silently (reported as merged, worktree deleted). */
static jc_size strnul_len(const char *s);   /* M927 */

void jc_parallel_parse_changes_z(const char *text, struct jc_arena *a,
                                 struct jc_vec *out)
{
    const char *p;

    if (text == NULL) {
        return;
    }
    p = text;
    while (*p != '\0') {
        const char *path;
        jc_size pathlen;
        struct jc_change ch;

        /* git diff -z (name-status): "<status>\0<path>\0" -- the NUL is a
         * FIELD separator, so after the one-char status the next byte IS the
         * separator; the path ends at the following NUL. A path that itself
         * contained a NUL would split here, but git cannot store one, so the
         * NUL is unambiguous (C.UTF-8 filenames keep their raw bytes). */
        if (p[1] != '\0') {
            break;   /* not a -z stream (defensive): nothing to parse */
        }
        path = p + 2;
        pathlen = (jc_size)strnul_len(path);
        if (pathlen == 0) {
            p = path;
            continue;
        }
        ch.status = p[0];
        ch.path = jc_arena_strndup(a, path, pathlen);
        jc_vec_push(out, &ch);
        p = path + pathlen + 1;
    }
}

/* Length up to the first NUL (jc_size; the -z path is bounded by the pipe). */
static jc_size strnul_len(const char *s)
{
    jc_size n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}
