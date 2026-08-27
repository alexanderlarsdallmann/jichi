/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_gradecore.c - the one grading mechanic (M529; own TU since M614 so the
 * TUI can be its fourth caller instead of its fourth implementation). See the
 * header for the lineage; the behaviour here is byte-for-byte the main.c
 * original except that the test report is handed out instead of freed. */
#include "jc_gradecore.h"
#include "jc_str.h"
#include "jc_proc.h"
#include "jc_snprintf.h"

#include <string.h>

int jc_grade_missing_dir(const char *verify, char *buf, jc_size cap)
{
    const char *p = verify;
    if (verify == NULL || buf == NULL || cap == 0) {
        return 0;
    }
    while (*p != '\0') {
        const char *tok;
        jc_size n;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (*p == '"' || *p == '\'') {
            p++; /* a quoted path: judge the bytes inside */
        }
        tok = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        n = (jc_size)(p - tok);
        /* strip trailing quotes/); a path rarely ends in them */
        while (n > 0 && (tok[n - 1] == '"' || tok[n - 1] == '\'' ||
                         tok[n - 1] == ')' || tok[n - 1] == ';')) {
            n--;
        }
        if (n > 1 && n < cap && tok[0] != '-' && memchr(tok, '/', n) != NULL) {
            char cand[512];
            jc_size i;
            if (n < sizeof(cand)) {
                memcpy(cand, tok, n);
                cand[n] = '\0';
                for (i = n; i > 0; i--) {
                    if (cand[i - 1] == '/') {
                        cand[i - 1] = '\0';
                        break;
                    }
                }
                if (cand[0] != '\0' && strcmp(cand, ".") != 0 &&
                    !jc_is_dir(cand)) {
                    jc_snprintf(buf, cap, "%s", cand);
                    return 1;
                }
            }
        }
    }
    return 0;
}

void jc_grade_out_free(struct jc_grade_out *o)
{
    if (o != NULL && o->have_rep) {
        jc_test_report_free(&o->rep);
        o->have_rep = 0;
    }
}

void jc_grade_core(const char *path, struct jc_arena *arena,
                   struct jc_grade_out *o)
{
    struct jc_sb out;
    char *argv[4];

    memset(o, 0, sizeof(*o));
    if (jc_read_file(path, &o->text, NULL, arena) != JC_OK) {
        o->fail = JC_GRADE_UNREADABLE;
        return;
    }
    if (jc_assign_parse(o->text, &o->spec, arena) != JC_OK) {
        o->fail = JC_GRADE_NO_TASK;
        return;
    }
    if (o->spec.verify == NULL || o->spec.verify[0] == '\0') {
        o->fail = JC_GRADE_NO_VERIFY;
        return;
    }
    if (o->spec.setup != NULL && o->spec.setup[0] != '\0') {
        struct jc_sb tmp;
        char *sv[4];
        jc_sb_init(&tmp);
        sv[0] = (char *)jc_shell_path(); sv[1] = "-c";
        sv[2] = (char *)o->spec.setup; sv[3] = 0;
        jc_proc_capture(sv, NULL, NULL, &tmp, 65536, 120, NULL);
        jc_sb_free(&tmp);
    }
    /* M502: can the gate RUN from here at all? Only the PROGRAM is examined,
     * never the arguments -- a verify like `test -f docs/DESIGN.md` names a file
     * the LEARNER is supposed to create, so a missing argument is the assignment
     * working while a missing program is the harness broken. */
    if (jc_assign_verify_program(o->spec.verify, o->prog, sizeof o->prog) != NULL
        && strchr(o->prog, '/') != NULL && !jc_file_exists(o->prog)) {
        o->fail = JC_GRADE_CANNOT_RUN;
        return;
    }
    jc_sb_init(&out);
    argv[0] = (char *)jc_shell_path(); argv[1] = "-c";
    argv[2] = (char *)o->spec.verify; argv[3] = 0;
    o->verify_exit = jc_proc_capture(argv, NULL, NULL, &out, 262144, 600, NULL);
    jc_test_report_init(&o->rep);
    o->have_rep = 1;
    jc_testparse(out.data, &o->rep);
    jc_assign_score(&o->rep, o->verify_exit == 0, &o->res);
    /* M617: a FAIL that could not even find the directory it grades in gets a
     * pointed note (never on PASS -- a green grade needs no doubt). */
    if (!o->res.passed) {
        (void)jc_grade_missing_dir(o->spec.verify, o->miss_dir,
                                   sizeof o->miss_dir);
    }
    jc_sb_free(&out);
}
