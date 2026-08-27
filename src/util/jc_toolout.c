/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolout.c - M339: keep the remainder of an over-cap tool output.
 * See jc_toolout.h and docs/proposals/2026-08-managed-tool-output.md. */
#include "jc_toolout.h"
#include "jc_app.h"
#include "jc_log.h"
#include "jc_vec.h"
#include "jc_proc.h"
#include "jc_snprintf.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void jc_toolout_split(jc_size len, jc_size show, jc_size *head, jc_size *tail)
{
    jc_size h;

    if (head != NULL) { *head = 0; }
    if (tail != NULL) { *tail = 0; }
    if (len <= show || show == 0) {
        return;
    }
    /* Two thirds head, one third tail. Head is larger because it carries the
     * command and its early context; the tail is never zero when there is room,
     * because a build log's error is at the end -- a head-only preview is what
     * today's truncation already does and it is the thing being fixed. */
    h = (show / 3) * 2;
    if (h == 0) {
        h = show;
    }
    if (head != NULL) { *head = h; }
    if (tail != NULL) { *tail = show - h; }
}

const char *jc_toolout_dir(struct jc_app *app, char *buf, jc_size cap)
{
    const char *home;

    if (app == NULL || buf == NULL || cap == 0) {
        return NULL;
    }
    buf[0] = '\0';
    home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return NULL;
    }
    jc_snprintf(buf, cap, "%s/.jichi.d/tool-output/%ld", home,
                (long)getpid());
    return buf;
}

int jc_toolout_preserve(struct jc_app *app, const char *tag,
                        const char *text, jc_size len,
                        char *path_out, jc_size path_cap)
{
    const char *dir;
    char dirbuf[512];
    static int seq;

    if (text == NULL || path_out == NULL || path_cap == 0) {
        return 0;
    }
    path_out[0] = '\0';
    dir = jc_toolout_dir(app, dirbuf, sizeof(dirbuf));
    if (dir == NULL || jc_mkdir_p(dir) != JC_OK) {
        return 0;
    }
    seq++;
    jc_snprintf(path_out, path_cap, "%s/%s-%d.txt", dir,
                (tag != NULL && tag[0] != '\0') ? tag : "output", seq);
    /* 0600 via the atomic writer: this is jichi's private sink, and it can
     * hold anything a command printed. */
    if (jc_write_file_atomic(path_out, text, len) != JC_OK) {
        /* D3: a storage failure must not turn a working tool into a failed
         * one. Fall back to exactly the old behaviour and tell the OPERATOR,
         * never the model -- it must not be given a path that is not there. */
        jc_logf(JC_LOG_WARN, "tool output: could not preserve %lu bytes to "
                "%s; the remainder is discarded as before",
                (unsigned long)len, path_out);
        path_out[0] = '\0';
        return 0;
    }
    return 1;
}

int jc_toolout_spill(struct jc_app *app, const char *tag,
                     const char *text, jc_size len, jc_size show,
                     struct jc_sb *out)
{
    char path[700];
    jc_size head = 0, tail = 0;

    if (out == NULL || text == NULL) {
        return 0;
    }
    if (len <= show) {
        jc_sb_append_n(out, text, len);
        return 0;
    }
    jc_toolout_split(len, show, &head, &tail);

    if (!jc_toolout_preserve(app, tag, text, len, path, sizeof(path))) {
        path[0] = '\0';
    }

    jc_sb_append_n(out, text, head);
    if (path[0] != '\0') {
        jc_sb_append_fmt(out, "\n\n... [%lu bytes total; this is the first %lu "
                         "and last %lu. The COMPLETE output is at %s -- read or "
                         "search that path for the rest instead of running this "
                         "again] ...\n\n",
                         (unsigned long)len, (unsigned long)head,
                         (unsigned long)tail, path);
    } else {
        jc_sb_append(out, "\n... [output truncated]\n");
    }
    if (tail > 0) {
        jc_sb_append_n(out, text + (len - tail), tail);
    }
    return path[0] != '\0';
}

int jc_toolout_dump_request(const char *dir, const char *url,
                            const char *body, jc_size len)
{
    static int nreq;
    char path[900];
    struct jc_sb sb;
    char *red = NULL;
    jc_status st;

    if (dir == NULL || dir[0] == '\0' || body == NULL) {
        return 0;
    }
    if (jc_mkdir_p(dir) != JC_OK) {
        jc_logf(JC_LOG_WARN, "dump-requests: cannot create %s", dir);
        return 0;
    }
    nreq++;
    jc_snprintf(path, sizeof(path), "%s/req-%04d.json", dir, nreq);

    /* A body can hold anything jichi read. Redact into a scratch buffer sized
     * from the input -- the redactor only ever shortens or keeps length. */
    red = (char *)malloc((size_t)len + 1);
    if (red != NULL && jc_redact_secrets(body, red, len + 1)) {
        body = red;
        len = (jc_size)strlen(red);
    }

    /* One file per call, with the endpoint on a leading comment line: JSON has
     * no comments, so it goes in a sibling header the reader can strip, rather
     * than corrupting the body a reader may want to replay verbatim. */
    jc_sb_init(&sb);
    jc_sb_append_fmt(&sb, "{\"endpoint\":\"%s\",\"bytes\":%lu,\"body\":",
                     (url != NULL) ? url : "", (unsigned long)len);
    jc_sb_append_n(&sb, body, len);
    jc_sb_append(&sb, "}\n");
    st = jc_write_file_atomic(path, sb.data != NULL ? sb.data : "",
                              sb.len);
    jc_sb_free(&sb);
    free(red);
    if (st != JC_OK) {
        jc_logf(JC_LOG_WARN, "dump-requests: could not write %s", path);
        return 0;
    }
    jc_logf(JC_LOG_INFO, "dump-requests: wrote %s", path);
    return 1;
}

void jc_toolout_cleanup(struct jc_app *app)
{
    char dirbuf[512];
    const char *dir = jc_toolout_dir(app, dirbuf, sizeof(dirbuf));
    char root[512];
    const char *home = getenv("HOME");
    struct jc_vec names;
    struct jc_arena *a;
    jc_size i;
    double now = jc_now_seconds();

    if (dir != NULL) {
        char *argv[4];
        argv[0] = (char *)"rm";
        argv[1] = (char *)"-rf";
        argv[2] = (char *)dir;
        argv[3] = NULL;
        (void)jc_proc_capture(argv, NULL, NULL, NULL, 0, 30L, NULL);
    }
    /* The age sweep: a crashed or SIGKILLed session never reaches the line
     * above, so without this the store grows forever -- which is precisely the
     * defect M338 had to go back and fix for preserved states. Designed in, not
     * bolted on. */
    if (home == NULL || app == NULL) {
        return;
    }
    a = jc_app_scratch(app);
    if (a == NULL) {
        return;
    }
    jc_snprintf(root, sizeof(root), "%s/.jichi.d/tool-output", home);
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(root, &names, a) == JC_OK) {
        for (i = 0; i < names.len; i++) {
            const char *nm = *(char **)jc_vec_at(&names, i);
            char sub[700];
            double age;
            if (nm == NULL || nm[0] == '.') {
                continue;
            }
            jc_snprintf(sub, sizeof(sub), "%s/%s", root, nm);
            age = now - jc_file_mtime(sub);
            if (age > (double)JC_TOOLOUT_MAX_AGE_DAYS * 86400.0) {
                char *argv[4];
                argv[0] = (char *)"rm";
                argv[1] = (char *)"-rf";
                argv[2] = sub;
                argv[3] = NULL;
                (void)jc_proc_capture(argv, NULL, NULL, NULL, 0, 30L, NULL);
            }
        }
    }
    jc_vec_free(&names);
}
