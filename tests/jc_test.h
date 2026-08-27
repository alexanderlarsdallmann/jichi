/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_test.h - tiny assertion harness for jichi (no framework).
 *
 * Each test file implements one `void test_<name>(void)` function declared
 * here; test_main.c runs them and reports a pass/fail summary. Assertions
 * increment global counters and print failures; they do not abort, so one
 * test can surface several problems.
 */
#ifndef JC_TEST_H
#define JC_TEST_H

#include <stdio.h>
#include <string.h>

extern int jc_test_checks;
extern int jc_test_fails;

#define JC_CHECK(cond) \
    do { \
        jc_test_checks++; \
        if (!(cond)) { \
            jc_test_fails++; \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/* JC_REQUIRE - record a check exactly like JC_CHECK, and YIELD ITS RESULT so the
 * caller can branch:
 *
 *     if (JC_REQUIRE(p != NULL)) {
 *         JC_CHECK(p->field == x);      / * only reached when p is real * /
 *     }
 *
 * This exists because JC_CHECK deliberately RECORDS AND CONTINUES, so it is not
 * a guard -- and a null check written as one is a SIGSEGV waiting for the day
 * the environment changes. M265 fixed that shape once (a test that segfaulted
 * on git < 2.5); M452 hit it again on an Android tablet with no /tmp, where a
 * fixture was never written and the suite ABORTED AT THE 4TH OF 123 TEST FILES,
 * reporting nothing about the other ~119. An audit then found 19 sites across 8
 * files, which is why this is a harness verb rather than 19 hand-written ifs:
 * the idiom is now one line, greppable, and the same everywhere.
 *
 * Use it ONLY in a condition. It is an expression, not a statement, so a bare
 * `JC_REQUIRE(x);` discards its value and reads like a guard while guarding
 * nothing -- the precise mistake it exists to prevent. */
#define JC_REQUIRE(cond) \
    (jc_test_checks++, (cond) ? 1 : \
        (jc_test_fails++, \
         printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond), 0))

/* JC_VEC_STR - element i of a vec-of-`char *`, or NULL when i is out of range.
 *
 * `*(char **)jc_vec_at(v, i)` dereferences BEFORE any assertion can inspect it,
 * so on a vec that is shorter than the test expected it is a SIGSEGV, not a
 * red -- and the preceding `JC_CHECK(v->len == 1)` does not prevent it, because
 * JC_CHECK records and continues. That is how the suite died at the 4th of 123
 * test files on a device where a fixture could not be written (M457).
 * JC_CHECK_STR already prints a NULL operand as "(null)", so routing the fetch
 * through here converts the crash into the honest failure it should always have
 * been. Requires jc_vec.h at the use site, which every caller already has. */
#define JC_VEC_STR(v, i) \
    (jc_vec_at((v), (i)) != NULL ? *(char **)jc_vec_at((v), (i)) : NULL)

/* Assign the arguments to pointer locals first: this decays an array argument
 * (e.g. a `char buf[N]`) to a pointer, so the `== NULL` guards are genuine
 * pointer comparisons and do not trip -Waddress ("address of array is always
 * true"). */
/* JC_CHECK_NEAR - compare two doubles with a tolerance, for a value that came
 * from text or from arithmetic.
 *
 * WHY THIS EXISTS (M469). `JC_CHECK(x == 0.2)` passed on every platform this
 * project had ever run on and failed on m68k, where five checks went red at once.
 * The values were RIGHT -- `strtod("0.2")` printed 0.20000000000000001 there,
 * byte-for-byte what x86-64 prints -- and the COMPARISON was wrong: m68k
 * evaluates floating point in the 68881's 80-bit extended format
 * (`sizeof(long double)` is 12 there against 16 here), so the literal `0.2`
 * carries more bits than the nearest double and the two are unequal at extended
 * precision. C89 permits exactly this: FLT_EVAL_METHOD 2. Historically i386/x87
 * behaves the same way, so the suite had a latent inability to validate on a
 * whole class of targets.
 *
 * Only NON-representable literals are affected. `== 0.5`, `== 0.75` and every
 * `== 3.0` are exact in both formats and are left alone deliberately -- changing
 * them would trade a precise assertion for a fuzzy one to no benefit.
 *
 * 1e-9 absolute, not relative: every value compared this way is a price, a
 * temperature or a unit-scale score, all within a couple of orders of magnitude
 * of 1, and an absolute epsilon is the one a reader can check by eye. No <math.h>
 * and no fabs, so this stays C89 with no new include in fifteen test files.
 */
#define JC_CHECK_NEAR(a, b) \
    do { \
        double jcn_a = (a); \
        double jcn_b = (b); \
        jc_test_checks++; \
        if (!(jcn_a - jcn_b < 1e-9 && jcn_b - jcn_a < 1e-9)) { \
            jc_test_fails++; \
            printf("  FAIL %s:%d: %.17g != %.17g (%s)\n", __FILE__, __LINE__, \
                   jcn_a, jcn_b, #a); \
        } \
    } while (0)

#define JC_CHECK_STR(a, b) \
    do { \
        const char *jcs_a = (a); \
        const char *jcs_b = (b); \
        jc_test_checks++; \
        if (jcs_a == NULL || jcs_b == NULL || strcmp(jcs_a, jcs_b) != 0) { \
            jc_test_fails++; \
            printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, \
                   jcs_a ? jcs_a : "(null)", jcs_b ? jcs_b : "(null)"); \
        } \
    } while (0)

/* Build a fixture path under $TMPDIR (falling back to /tmp). See the definition
 * in test_main.c for why the suite may not hardcode /tmp. Use as:
 *     const char *path = jc_test_tmp("jichi_test_config.json");
 * The returned pointer is valid until 16 further calls have been made. */
const char *jc_test_tmp(const char *name);
const char *jc_test_tmpdir(void);

/* Test entry points (one per tests/test_*.c). */
void test_str(void);
void test_sb_reserve_bounds(void);
void test_suggest(void);
void test_vec(void);
void test_json(void);
void test_json_number_range(void);
void test_json_depth_limit(void);
void test_json_bool_lenient(void);
void test_walk_skip_dir(void);
void test_priv_verdict(void);
void test_assign_name_ok(void);
void test_tu_arg_bool_numbers(void);
void test_bool_from_word(void);
void test_dir_holds_private(void);
void test_config(void);
void test_configedit(void);
void test_confbench(void);
void test_utf8(void);
void test_ctrl_sanitize(void);
void test_packages(void);
void test_sse(void);
void test_message(void);
void test_history_check(void);
void test_session_roundtrip(void);
void test_gradecore(void); /* M614 */
void test_session_store_version(void); /* M606 */
void test_tool(void);
void test_tool_unstring(void);
void test_tool_nameless(void);
void test_toolcall_scan(void);
void test_jsonrepair(void);
void test_args_repair_execute(void);
void test_priv(void);
void test_provider(void);
void test_golden_request(void);
void test_toolprobe(void);
void test_promptcache(void);
void test_calib(void);
void test_imagegen(void);
void test_audiogen(void);
void test_neterr(void);
void test_argpath(void);
void test_multipart(void);
void test_multipart_header_injection(void);
void test_transcribe(void);
void test_platform(void);
void test_bounds(void);
void test_escape_ctrl(void);
void test_sessmeta(void);
void test_session(void);
void test_session_dirty_skip(void);
void test_session_serialize(void);
void test_session_prune_select(void);
void test_session_footprint(void);
void test_session_foreign_file(void);
void test_session_drift(void);
void test_rewind(void);
void test_convert(void);
void test_jsonc(void);
void test_daemon(void);
void test_assign(void);
void test_progress(void);
void test_meminfo(void);
void test_memtrim(void);
void test_hint(void);
void test_improve(void);
void test_selfheal(void);
void test_cacheaudit(void);
void test_workflow(void);
void test_embed(void);
void test_index(void);
void test_lexical(void);
void test_retrieve(void);
void test_autocontext(void);
void test_mcp(void);
void test_perm(void);
void test_subagent(void);
void test_orchestration(void);
void test_md(void);
void test_rules(void);
void test_todo(void);
void test_agentdef(void);
void test_command(void);
void test_cli(void);
void test_agentjson(void);
void test_lsp(void);
void test_lsp_frame_bounds(void);
void test_compact(void);
void test_compact_message_estimate(void);
void test_snapshot(void);
void test_skill(void);
void test_envelope(void);
void test_parallel(void);
void test_routing(void);
void test_fallback(void);
void test_lsp_nav(void);
void test_testparse(void);
void test_repomap(void);
void test_git(void);
void test_user_tools(void);
void test_refs(void);
void test_complete(void);
void test_acp(void);
void test_fim(void);
void test_fmtcmd(void);
void test_patch(void);
void test_diff(void);
void test_reread(void);
void test_memory(void);
void test_glossary(void);
void test_constraint(void);
void test_doctor(void);
void test_scaffold(void);
void test_assetval(void);
void test_term(void);
void test_mdrender(void);
void test_lineno(void);
void test_count_lines(void);
void test_app(void);
void test_eventlog(void);
void test_telemetry(void);
void test_insights(void);
void test_learn(void);
void test_http(void);
void test_http_describe_failure(void);
void test_http_describe_redirect(void);
void test_path(void);
void test_redact(void);
void test_hooks(void);
void test_bg(void);
void test_websearch(void);
void test_output_style(void);
void test_base64_image(void);
void test_vision(void);
void test_proc(void);
void test_pdf(void);
void test_setup(void);
void test_board(void);
void test_workerpool(void);
void test_rss(void);
void test_sysmsg_fit(void);
void test_sysmsg_verify_gate(void);
void test_sysmsg_scope_reach(void);
void test_sysmsg_date(void);
void test_sysmsg_envelope(void);
void test_sysmsg_context_window(void);
void test_prefix_watch(void);
void test_sysmsg_fit_budget(void);
void test_sysmsg_cost_model(void);
void test_sysmsg_design(void);
void test_sysmsg_language(void);
void test_sysmsg_sub_persona(void);
void test_sysmsg_stances(void);
void test_sysmsg_craft(void);
void test_sysmsg_parts(void);
void test_sysmsg_solving_stance_tools(void);
void test_sysmsg_style(void);
void test_msg(void);
void test_width(void);
void test_arena(void);
void test_auditview(void);
void test_runsview(void);
void test_control(void);
void test_kinetic(void);
void test_sound(void);
void test_untrusted(void);
void test_voice(void);
void test_perm_mode_narrow(void);
void test_ttools(void);
void test_lease(void);
void test_env_panel(void);
void test_delegreport(void);
void test_toolloop(void);
void test_constraint_source_line(void);

#endif /* JC_TEST_H */
