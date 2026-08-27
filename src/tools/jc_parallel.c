/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_parallel.c - pure helpers for the parallel agent swarm (see jc_parallel.h).
 *
 * No process, file, or network state here; the spawn_parallel tool
 * (jc_tool_parallel.c) drives these to size its fork pool, read each worktree's
 * change list, and resolve the file-level merge. */

#include "jc_parallel.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"

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
        out->tool_calls = (int)jc_json_get_num(o, "tools", 0.0);
        /* M437: absent fields leave the buffers empty, which the parent reads as
         * "the child did not report" -- so an older child message degrades to the
         * pre-M437 behaviour instead of asserting a stop reason it never sent. */
        cp(out->stop, sizeof out->stop, jc_json_get_str(o, "stop", NULL));
        cp(out->ftool, sizeof out->ftool, jc_json_get_str(o, "ftool", NULL));
        cp(out->fmsg, sizeof out->fmsg, jc_json_get_str(o, "fmsg", NULL));
        out->fcls = (int)jc_json_get_num(o, "fcls", 0.0);
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
