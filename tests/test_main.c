/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_main.c - runs all jichi unit tests. */

#include "jc_test.h"
#include "jc_snprintf.h"   /* jc_snprintf */
#include "jc_platform.h"   /* jc_list_dir */
#include "jc_vec.h"
#include "jc_mem.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>   /* getenv */
#include <string.h>
#include <unistd.h>   /* unlink, rmdir */
#include <sys/types.h>
#include <sys/stat.h> /* lstat */

int jc_test_checks = 0;
int jc_test_fails = 0;

/* jc_test_tmp - build a fixture path under $TMPDIR, falling back to /tmp.
 *
 * The suite used to hardcode 158 literal "/tmp/..." paths across 35 of its test
 * files and consult TMPDIR nowhere, so on a host without a writable /tmp every
 * fixture silently failed to open and the run collapsed -- measured on an
 * Android 4.4 tablet where jichi itself runs fine (M452). The SMOKE tier has
 * always done this correctly (`_smoke.sh`'s smoke_tmp uses "${TMPDIR:-/tmp}"),
 * so this brings one tier up to the other's standard rather than inventing a
 * convention.
 *
 * Returns a pointer into a small rotating set of static buffers, so several
 * fixture paths can be live at once in the same scope; the suite is
 * single-threaded, and 16 is far more than any test holds simultaneously.
 * Callers keep using the result as a plain `const char *`, which is what makes
 * the conversion one line per site. */
/* The fixture directory itself ($TMPDIR, else /tmp), for tests that need a
 * workspace root rather than a file path. Same reason as jc_test_tmp. */
const char *jc_test_tmpdir(void)
{
    const char *dir = getenv("TMPDIR");
    return (dir != NULL && dir[0] != '\0') ? dir : "/tmp";
}

const char *jc_test_tmp(const char *name)
{
    static char bufs[16][512];
    static int next = 0;
    const char *dir = getenv("TMPDIR");
    char *b;

    if (dir == NULL || dir[0] == '\0') {
        dir = "/tmp";
    }
    b = bufs[next];
    next = (next + 1) % 16;
    if (jc_snprintf(b, sizeof bufs[0], "%s/%s", dir, name)
        >= (int)sizeof bufs[0]) {
        /* M728: a fixture path cut to fit is a DIFFERENT path, and the
         * prefix it leaves can be $TMPDIR itself or a parent of it. Say so,
         * and fail; jc_test_rm_rf refuses whatever such a path would name. */
        jc_test_checks++;
        jc_test_fails++;
        printf("  FAIL jc_test_tmp: \"%s/%s\" does not fit %lu bytes -- "
               "set a shorter TMPDIR (M728)\n",
               dir, name, (unsigned long)sizeof bufs[0]);
    }
    return b;
}

/* jc_test_rm_rf - remove a fixture tree, WITHOUT a shell, and only strictly
 * below the fixture directory (M728).
 *
 * Twenty-five sites used to build "rm -rf %s" into a fixed buffer and hand it
 * to system(). A path is cut short when the buffer is, and a cut path is a
 * PREFIX: under a 127-character TMPDIR, tests/test_bounds.c's 128-byte `home`
 * came out as the TMPDIR itself, and a traced run of the suite executed
 * `rm -rf "$TMPDIR"` four times. A longer TMPDIR cuts to a prefix of itself,
 * which can be a parent directory; a TMPDIR with a space split the argument in
 * two. Nothing checked any of it: the gate runs with TMPDIR unset, and a
 * system() whose result is ignored reports nothing either way.
 *
 * So the removal takes the path itself, never a command line, and refuses any
 * path that is not strictly below jc_test_tmpdir() -- the directory itself, a
 * parent, a sibling that merely shares a prefix ("/tmpx" for "/tmp"), a `.` or
 * `..` step, or anything at all when the fixture directory is "/". A refusal is
 * a harness bug, so it FAILS the suite rather than skipping. The walk uses
 * lstat and never follows a symlink out of the tree, and each child path lives
 * in an arena, not on the stack (FreeMiNT's is a fixed 512 KB, M727). */
int jc_test_rm_rf_allowed(const char *path)
{
    const char *td = jc_test_tmpdir();
    size_t tl = strlen(td);
    const char *c;

    while (tl > 1 && td[tl - 1] == '/') {
        tl--; /* "/tmp/" is "/tmp" */
    }
    if (path == NULL || tl == 0 || (tl == 1 && td[0] == '/')) {
        return 0; /* no path, or a fixture directory of "/" */
    }
    if (strncmp(path, td, tl) != 0 || path[tl] != '/') {
        return 0; /* not below it: a parent, a prefix-sibling, elsewhere */
    }
    c = path + tl;
    while (*c == '/') {
        c++;
    }
    if (*c == '\0') {
        return 0; /* the fixture directory itself */
    }
    for (;;) {
        const char *e = strchr(c, '/');
        size_t cl = (e != NULL) ? (size_t)(e - c) : strlen(c);
        if ((cl == 1 && c[0] == '.') ||
            (cl == 2 && c[0] == '.' && c[1] == '.')) {
            return 0; /* a step, which could climb back out */
        }
        if (e == NULL) {
            break;
        }
        c = e + 1;
    }
    return 1;
}

static int rm_tree(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        return (errno == ENOENT) ? 0 : -1; /* absent is already removed */
    }
    if (S_ISDIR(st.st_mode)) {
        struct jc_arena *a = jc_arena_new(0);
        struct jc_vec names;
        jc_size i;
        int rc = 0;

        if (a == NULL) {
            return -1;
        }
        jc_vec_init(&names, sizeof(char *));
        if (jc_list_dir(path, &names, a) != JC_OK) {
            rc = -1;
        }
        for (i = 0; rc == 0 && i < names.len; i++) {
            const char *name = *(char **)jc_vec_at(&names, i);
            jc_size need = (jc_size)(strlen(path) + 1 + strlen(name) + 1);
            char *child = (char *)jc_arena_alloc(a, need);
            if (child == NULL) {
                rc = -1;
                break;
            }
            jc_snprintf(child, need, "%s/%s", path, name);
            if (rm_tree(child) != 0) {
                rc = -1;
            }
        }
        jc_vec_free(&names);
        jc_arena_free(a);
        if (rc == 0 && rmdir(path) != 0) {
            rc = -1;
        }
        return rc;
    }
    return (unlink(path) == 0) ? 0 : -1; /* a file, or a link itself */
}

int jc_test_rm_rf(const char *path)
{
    if (!jc_test_rm_rf_allowed(path)) {
        jc_test_checks++;
        jc_test_fails++;
        printf("  FAIL jc_test_rm_rf refused \"%s\": not strictly below the "
               "fixture directory \"%s\" (M728)\n",
               path != NULL ? path : "(null)", jc_test_tmpdir());
        return -1;
    }
    return rm_tree(path);
}

/* The longest TMPDIR the fixtures are MEASURED clean under (M728): 0 failures
 * at 160 characters, plain and under ASan/UBSan; at 200, four truncation false
 * reds in test_session.c; at 240, a segfault. Their buffers were sized for
 * "/tmp", and a TMPDIR past this length is refused before any test runs,
 * rather than run into buffers nobody sized for it. Raising it means measuring
 * again, not editing the number. */
#define JC_TEST_TMPDIR_MAX 160

int main(void)
{
    /* The binary ignores SIGPIPE at its entry points (headless, daemon); the
     * test harness must too, or a fork/pipe race -- a user-tool child dying
     * before the parent writes its stdin JSON, a window ASan's slowness
     * widens -- kills the whole suite with "Broken pipe" instead of failing
     * one check (seen once on the M146 ci run; ANECDOTES #17). */
    signal(SIGPIPE, SIG_IGN);

    if (strlen(jc_test_tmpdir()) > JC_TEST_TMPDIR_MAX) {
        printf("run_tests: TMPDIR is %lu characters, and the fixtures are "
               "measured clean up to %d (M728).\n"
               "run_tests: set a shorter one, for example TMPDIR=/tmp.\n",
               (unsigned long)strlen(jc_test_tmpdir()), JC_TEST_TMPDIR_MAX);
        return 2;
    }

    /* First: every later test removes its fixtures through this fence. */
    printf("test_harness\n");
    test_harness();
    printf("test_str\n");
    test_str();
    printf("test_sb_reserve_bounds\n");
    test_sb_reserve_bounds();
    test_suggest();
    printf("test_vec\n");
    test_vec();
    printf("test_json\n");
    test_json();
    printf("test_json_number_range\n");
    test_json_number_range();
    printf("test_json_depth_limit\n");
    test_json_depth_limit();
    printf("test_json_string_alloc\n");
    test_json_string_alloc();
    printf("test_json_bool_lenient\n");
    test_json_bool_lenient();
    printf("test_walk_skip_dir\n");
    test_walk_skip_dir();
    printf("test_priv_verdict\n");
    test_priv_verdict();
    printf("test_assign_name_ok\n");
    test_assign_name_ok();
    printf("test_tu_arg_bool_numbers\n");
    test_tu_arg_bool_numbers();
    printf("test_bool_from_word\n");
    test_bool_from_word();
    printf("test_dir_holds_private\n");
    test_dir_holds_private();
    printf("test_config\n");
    test_config();
    printf("test_configedit\n");
    test_configedit();
    printf("test_confbench\n");
    test_confbench();
    printf("test_utf8\n");
    test_utf8();
    printf("test_ctrl_sanitize\n");
    test_ctrl_sanitize();
    printf("test_packages\n");
    test_packages();
    printf("test_sse\n");
    test_sse();
    printf("test_message\n");
    test_message();
    printf("test_history_check\n");
    test_history_check();
    printf("test_gradecore\n");
    test_gradecore();
    printf("test_session_roundtrip\n");
    test_session_roundtrip();
    printf("test_session_store_version\n");
    test_session_store_version();
    printf("test_tool\n");
    test_tool();
    printf("test_tool_unstring\n");
    test_tool_unstring();
    printf("test_tool_nameless\n");
    test_tool_nameless();
    printf("test_toolcall_scan\n");
    test_toolcall_scan();
    printf("test_jsonrepair\n");
    test_jsonrepair();
    printf("test_args_repair_execute\n");
    test_args_repair_execute();
    printf("test_priv\n");
    test_priv();
    printf("test_provider\n");
    test_provider();
    printf("test_golden_request\n");
    test_golden_request();
    printf("test_toolprobe\n");
    test_toolprobe();
    printf("test_reach\n");
    test_reach();
    printf("test_outcome\n");
    test_outcome();
    printf("test_plan\n");
    test_plan();
    printf("test_promptcache\n");
    test_promptcache();
    printf("test_imagegen\n");
    test_imagegen();
    printf("test_audiogen\n");
    test_audiogen();
    printf("test_neterr\n");
    test_neterr();
    printf("test_argpath\n");
    test_argpath();
    printf("test_multipart\n");
    test_multipart();
    printf("test_multipart_header_injection\n");
    test_multipart_header_injection();
    printf("test_transcribe\n");
    test_transcribe();
    printf("test_platform\n");
    test_platform();
    printf("test_bounds\n");
    test_bounds();
    printf("test_escape_ctrl\n");
    test_escape_ctrl();
    printf("test_sessmeta\n");
    test_sessmeta();
    printf("test_session\n");
    test_session();
    printf("test_session_drift\n");
    test_session_drift();
    printf("test_session_dirty_skip\n");
    test_session_dirty_skip();
    printf("test_session_serialize\n");
    test_session_serialize();
    printf("test_session_prune_select\n");
    test_session_prune_select();
    printf("test_session_footprint\n");
    test_session_footprint();
    printf("test_session_foreign_file\n");
    test_session_foreign_file();
    printf("test_rewind\n");
    test_rewind();
    printf("test_convert\n");
    test_convert();
    printf("test_jsonc\n");
    test_jsonc();
    printf("test_daemon\n");
    test_daemon();
    printf("test_assign\n");
    test_assign();
    printf("test_progress\n");
    test_progress();
    printf("test_meminfo\n");
    test_meminfo();
    printf("test_memtrim\n");
    test_memtrim();
    printf("test_hint\n");
    test_hint();
    printf("test_improve\n");
    test_improve();
    printf("test_selfheal\n");
    test_selfheal();
    printf("test_cacheaudit\n");
    test_cacheaudit();
    printf("test_workflow\n");
    test_workflow();
    printf("test_embed\n");
    test_embed();
    printf("test_index\n");
    test_index();
    printf("test_lexical\n");
    test_lexical();
    printf("test_retrieve\n");
    test_retrieve();
    printf("test_autocontext\n");
    test_autocontext();
    printf("test_mcp\n");
    test_mcp();
    printf("test_perm\n");
    test_perm();
    printf("test_subagent\n");
    test_subagent();
    printf("test_orchestration\n");
    test_orchestration();
    printf("test_md\n");
    test_md();
    printf("test_rules\n");
    test_rules();
    printf("test_todo\n");
    test_todo();
    printf("test_untrusted\n");
    test_untrusted();
    printf("test_voice\n");
    test_voice();
    printf("test_perm_mode_narrow\n");
    test_perm_mode_narrow();
    printf("test_agentdef\n");
    test_agentdef();
    printf("test_command\n");
    test_command();
    printf("test_cli\n");
    test_cli();
    printf("test_agentjson\n");
    test_agentjson();
    printf("test_lsp\n");
    test_lsp();
    printf("test_lsp_frame_bounds\n");
    test_lsp_frame_bounds();
    printf("test_compact\n");
    test_compact();
    printf("test_compact_message_estimate\n");
    test_compact_message_estimate();
    printf("test_snapshot\n");
    test_snapshot();
    printf("test_skill\n");
    test_skill();
    printf("test_envelope\n");
    test_envelope();
    printf("test_parallel\n");
    test_parallel();
    printf("test_routing\n");
    test_routing();
    printf("test_fallback\n");
    test_fallback();
    printf("test_lsp_nav\n");
    test_lsp_nav();
    printf("test_testparse\n");
    test_testparse();
    printf("test_repomap\n");
    test_repomap();
    printf("test_git\n");
    test_git();
    printf("test_user_tools\n");
    test_user_tools();
    printf("test_refs\n");
    test_refs();
    printf("test_complete\n");
    test_complete();
    printf("test_acp\n");
    test_acp();
    printf("test_fim\n");
    test_fim();
    test_fmtcmd();
    printf("test_patch\n");
    test_patch();
    printf("test_diff\n");
    test_diff();
    printf("test_reread\n");
    test_reread();
    printf("test_sysmsg_fit\n");
    test_sysmsg_fit();
    printf("test_sysmsg_verify_gate\n");
    test_sysmsg_verify_gate();
    printf("test_sysmsg_scope_reach\n");
    test_sysmsg_scope_reach();
    printf("test_sysmsg_date\n");
    test_sysmsg_date();
    printf("test_sysmsg_envelope\n");
    test_sysmsg_envelope();
    printf("test_sysmsg_context_window\n");
    test_sysmsg_context_window();
    printf("test_prefix_watch\n");
    test_prefix_watch();
    printf("test_sysmsg_fit_budget\n");
    test_sysmsg_fit_budget();
    printf("test_sysmsg_cost_model\n");
    test_sysmsg_cost_model();
    printf("test_sysmsg_design\n");
    test_sysmsg_design();
    printf("test_sysmsg_language\n");
    test_sysmsg_language();
    printf("test_sysmsg_sub_persona\n");
    test_sysmsg_sub_persona();
    printf("test_sysmsg_stances\n");
    test_sysmsg_stances();
    printf("test_sysmsg_craft\n");
    test_sysmsg_craft();
    printf("test_sysmsg_parts\n");
    test_sysmsg_parts();
    printf("test_sysmsg_solving_stance_tools\n");
    test_sysmsg_solving_stance_tools();
    printf("test_sysmsg_style\n");
    test_sysmsg_style();
    printf("test_msg\n");
    test_msg();
    test_width();
    printf("test_arena\n");
    test_arena();
    printf("test_auditview\n");
    test_auditview();
    printf("test_runsview\n");
    test_runsview();
    printf("test_control\n");
    test_control();
    printf("test_kinetic\n");
    test_kinetic();
    printf("test_sound\n");
    test_sound();
    printf("test_memory\n");
    test_memory();
    printf("test_glossary\n");
    test_glossary();
    printf("test_constraint\n");
    test_constraint();
    printf("test_doctor\n");
    test_doctor();
    printf("test_scaffold\n");
    test_scaffold();
    printf("test_assetval\n");
    test_assetval();
    printf("test_term\n");
    test_term();
    printf("test_mdrender\n");
    test_mdrender();
    printf("test_lineno\n");
    test_lineno();
    printf("test_count_lines\n");
    test_count_lines();
    printf("test_app\n");
    test_app();
    printf("test_eventlog\n");
    test_eventlog();
    printf("test_telemetry\n");
    test_telemetry();
    printf("test_insights\n");
    test_insights();
    printf("test_learn\n");
    test_learn();
    printf("test_calib\n");
    test_calib();
    printf("test_http\n");
    test_http();
    printf("test_http_describe_failure\n");
    test_http_describe_failure();
    printf("test_http_describe_redirect\n");
    test_http_describe_redirect();
    printf("test_path\n");
    test_path();
    printf("test_redact\n");
    test_redact();
    printf("test_hooks\n");
    test_hooks();
    printf("test_bg\n");
    test_bg();
    printf("test_websearch\n");
    test_websearch();
    printf("test_output_style\n");
    test_output_style();
    printf("test_base64_image\n");
    test_base64_image();
    printf("test_vision\n");
    test_vision();
    printf("test_proc\n");
    test_proc();
    printf("test_pdf\n");
    test_pdf();
    printf("test_setup\n");
    test_setup();
    printf("test_board\n");
    test_board();
    printf("test_workerpool\n");
    test_workerpool();
    printf("test_rss\n");
    test_rss();
    printf("test_ttools\n");
    test_ttools();
    printf("test_lease\n");
    test_lease();
    printf("test_env_panel\n");
    test_env_panel();
    printf("test_delegreport\n");
    test_delegreport();
    printf("test_toolloop\n");
    test_toolloop();
    printf("test_noprogress\n");
    test_noprogress();
    printf("test_constraint_source_line\n");
    test_constraint_source_line();

    printf("\n%d checks, %d failures\n", jc_test_checks, jc_test_fails);
    return jc_test_fails == 0 ? 0 : 1;
}
