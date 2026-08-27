/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_command.c - custom command template expansion + loading. */

#include "jc_test.h"

#include <stdio.h>
#include "jc_command.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

void test_command(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_command c;
    char *out;

    memset(&c, 0, sizeof(c));
    c.name = "t";

    /* $ARGUMENTS and positional substitution. */
    c.body = "Hi $ARGUMENTS | a=$1 b=$2 | end";
    jc_command_expand(&c, "alpha beta", ".", a, &out);
    JC_CHECK(strstr(out, "Hi alpha beta") != NULL);
    JC_CHECK(strstr(out, "a=alpha b=beta") != NULL);

    /* Missing positional => empty. */
    c.body = "x=$3.";
    jc_command_expand(&c, "only", ".", a, &out);
    JC_CHECK_STR(out, "x=.");

    /* @file inclusion, relative to the cwd the caller passes.
     *
     * M461: this read <repo>/include/jc_md.h and relied on the binary running
     * from the repo root -- true under `make test`, false on any target you
     * push only a binary to, which is exactly what `make check-target` is for.
     * It failed on an Android 12 phone. The fixture is now written and read
     * under $TMPDIR, so the check tests @-inclusion rather than the cwd. */
    {
        const char *fx = jc_test_tmp("jc_cmd_include.txt");
        FILE *fh = fopen(fx, "w");
        if (fh != NULL) {
            fputs("MARKER_JC_CMD_INCLUDE\n", fh);
            fclose(fh);
            c.body = "@jc_cmd_include.txt";
            jc_command_expand(&c, "", jc_test_tmpdir(), a, &out);
            JC_CHECK(strstr(out, "MARKER_JC_CMD_INCLUDE") != NULL);
            remove(fx);
        }
    }

    /* !`cmd` shell injection (echo is hermetic). */
    c.body = "out:!`echo hi`:done";
    jc_command_expand(&c, "", ".", a, &out);
    JC_CHECK(strstr(out, "out:hi") != NULL);

    /* Loader from a written project dir. */
    setenv("HOME", jc_test_tmp("jichi_cmd_home"), 1);
    jc_mkdir_p(jc_test_tmp("jichi_cmd_test/.jichi/commands"));
    {
        const char *md = "---\ndescription: greet\n---\nHello there $1\n";
        jc_write_file(jc_test_tmp("jichi_cmd_test/.jichi/commands/greet.md"), md,
                      strlen(md));
    }
    {
        struct jc_command_set set;
        const struct jc_command *g;
        jc_command_set_init(&set);
        jc_command_load(&set, jc_test_tmp("jichi_cmd_test"), a);
        g = jc_command_find(&set, "greet");
        JC_CHECK(g != NULL);
        if (g != NULL) {
            JC_CHECK_STR(g->description, "greet");
            JC_CHECK(strstr(g->body, "Hello there") != NULL);
        }
        JC_CHECK(jc_command_find(&set, "missing") == NULL);

        /* M79: subtask + output frontmatter parse. */
        {
            const char *md = "---\ndescription: draft lessons\n"
                             "subtask: true\noutput: .jichi/lessons.draft.md\n"
                             "---\nMine the logs.\n";
            const struct jc_command *m;
            struct jc_command_set set2;
            jc_write_file(jc_test_tmp("jichi_cmd_test/.jichi/commands/learn.md"), md,
                          strlen(md));
            jc_command_set_init(&set2);
            jc_command_load(&set2, jc_test_tmp("jichi_cmd_test"), a);
            m = jc_command_find(&set2, "learn");
            JC_CHECK(m != NULL);
            if (m != NULL) {
                JC_CHECK(m->subtask == 1);
                JC_CHECK_STR(m->output, ".jichi/lessons.draft.md");
                /* M597: no `language:` here => NULL (the session language). */
                JC_CHECK(m->language == NULL);
            }
            /* A command with no output frontmatter leaves it NULL. */
            m = jc_command_find(&set2, "greet");
            JC_CHECK(m != NULL && m->output == NULL);
            jc_command_set_free(&set2);
        }

        /* Listing render (the `commands` subcommand core). */
        {
            struct jc_sb sb;
            jc_sb_init(&sb);
            jc_command_render_list(&set, &sb);
            JC_CHECK(sb.data != NULL);
            JC_CHECK(strstr(sb.data, "/greet") != NULL);
            JC_CHECK(strstr(sb.data, "greet") != NULL);
            jc_sb_free(&sb);
        }
        jc_command_set_free(&set);
    }

    jc_arena_free(a);
}
