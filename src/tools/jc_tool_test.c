/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_test.c - the run_tests tool.
 *
 * Runs a test command (combined stdout+stderr, capped) and returns a *parsed*
 * summary -- which tests failed and where -- ahead of a bounded raw tail, so
 * the model gets the signal without re-reading build chatter. The command is
 * the tool's `command` arg, else config testCommand, else config verify.
 * Mutating, like run_terminal_command (tests can build/write).
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_testparse.h"

#define TEST_MAX_OUTPUT (64 * 1024)
#define TEST_RAW_TAIL    (4 * 1024)

static cJSON *test_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "command",
        "Shell command to run the tests. Omit to use the configured "
        "testCommand (or verify command).", 0);
    return s;
}

static jc_status test_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    const char *command = tu_arg_str(args, "command");
    struct jc_sb raw;
    struct jc_sb full;
    struct jc_test_report rep;
    const char *tail;
    jc_size off = 0;
    int truncated = 0;
    int status = -1;

    if (command == NULL || command[0] == '\0') command = app->config.test_command;
    if (command == NULL || command[0] == '\0') command = app->config.verify;
    if (command == NULL || command[0] == '\0') {
        tu_err(out, "error: no test command given and none configured "
                    "(pass 'command', or set testCommand/verify in config)");
        return JC_OK;
    }

    jc_sb_init(&raw);
    if (jc_app_run_command(app, command,
                           jc_config_cap(app->config.run_max_bytes,
                                         TEST_MAX_OUTPUT),
                           &raw, &status, &truncated) != JC_OK) {
        jc_sb_free(&raw);
        tu_err(out, "error: failed to start the test command");
        return JC_OK;
    }

    jc_test_report_init(&rep);
    jc_testparse(raw.data, &rep);

    jc_sb_init(&full);
    if (jc_testparse_render(&rep, &full) == 0 && rep.failed < 0 &&
        rep.passed < 0) {
        jc_sb_append(&full, status == 0 ? "Tests passed.\n"
                                        : "No structured results parsed.\n");
    }
    jc_test_report_free(&rep);

    /* A bounded raw tail for context (the parsed summary carries the signal). */
    if (raw.data != NULL && raw.len > TEST_RAW_TAIL) off = raw.len - TEST_RAW_TAIL;
    tail = (raw.data != NULL) ? raw.data + off : "";
    jc_sb_append(&full, "\n--- raw output");
    jc_sb_append(&full, off > 0 ? " (tail) ---\n" : " ---\n");
    if (tail[0] != '\0') jc_sb_append(&full, tail);
    if (truncated) jc_sb_append(&full, "\n... [output truncated]");
    jc_sb_append_fmt(&full, "\n[exit status: %d]", status);
    jc_sb_free(&raw);

    out->content = jc_sb_finish(&full);
    out->is_error = (status != 0);
    out->exit_status = status; /* M168: red tests are not a broken tool */
    jc_sb_free(&full);
    return JC_OK;
}

static const struct jc_tool TEST_TOOL = {
    "run_tests",
    "Run the project's tests and return a structured summary (which tests "
    "failed, with file:line) plus a bounded raw tail. Omit 'command' to use "
    "the configured testCommand/verify. Prefer this over run_terminal_command "
    "for running tests.",
    test_schema,
    0, /* mutating: tests can build/write */
    test_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_run_tests(void)
{
    return &TEST_TOOL;
}
