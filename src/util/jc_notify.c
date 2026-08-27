/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_notify.c - completion notification (see jc_notify.h). */

#include "jc_notify.h"
#include "jc_platform.h"
#include "jc_proc.h"
#include "jc_str.h"
#include "jc_vec.h"

#include <stdio.h>
#include <stdlib.h>

/* Cap on the notify command's captured output (discarded) and its kill
 * deadline -- a notifier should be quick and must never wedge the front-end. */
#define NOTIFY_OUT_CAP 4096
#define NOTIFY_TIMEOUT 10L

/* Build a heap-allocated "KEY=VALUE" env entry and push it onto `env`. */
static void push_env(struct jc_vec *env, const char *key, const char *val)
{
    struct jc_sb sb;
    char *s;
    jc_sb_init(&sb);
    jc_sb_append(&sb, key);
    jc_sb_append_char(&sb, '=');
    jc_sb_append(&sb, val != NULL ? val : "");
    s = jc_sb_finish(&sb);
    jc_sb_free(&sb);
    if (s != NULL) {
        jc_vec_push(env, &s);
    }
}

void jc_notify_fire(const char *command, int bell, const char *cwd,
                    const char *summary)
{
    if (bell) {
        /* The bell goes to stderr so it never corrupts stdout (e.g. `-q
         * --output json` or a piped answer); a terminal still rings it. */
        fputc('\a', stderr);
        fflush(stderr);
    }

    if (command != NULL && command[0] != '\0') {
        char *argv[4];
        struct jc_vec env;
        struct jc_sb sink;
        jc_size i;

        jc_vec_init(&env, sizeof(char *));
        push_env(&env, "JICHI_NOTIFY", summary);
        push_env(&env, "JICHI_CWD", cwd);

        argv[0] = (char *)jc_shell_path();
        argv[1] = (char *)"-c";
        argv[2] = (char *)command;
        argv[3] = NULL;

        jc_sb_init(&sink);
        jc_proc_capture(argv, &env, NULL, &sink, NOTIFY_OUT_CAP,
                        NOTIFY_TIMEOUT, NULL);
        jc_sb_free(&sink);

        for (i = 0; i < env.len; i++) {
            free(*(char **)jc_vec_at(&env, i));
        }
        jc_vec_free(&env);
    }
}
