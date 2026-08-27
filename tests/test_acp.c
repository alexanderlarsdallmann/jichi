/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_acp.c - pure ACP protocol shaping (jc_acp_proto.c). */

#include "jc_test.h"
#include "jc_acp.h"
#include "jc_app.h"
#include "jc_message.h"
#include "jc_mem.h"
#include "jc_json.h"
#include "jc_str.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

/* Helper: does compact JSON `s` contain the substring `needle`? */
static int has(const char *s, const char *needle)
{
    return s != NULL && strstr(s, needle) != NULL;
}

static void test_envelopes(void)
{
    char *r;

    /* Response envelope wraps the result under "result" with the id echoed. */
    {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "stopReason", "end_turn");
        r = jc_acp_build_response(7, result);
        JC_CHECK(has(r, "\"jsonrpc\":\"2.0\""));
        JC_CHECK(has(r, "\"id\":7"));
        JC_CHECK(has(r, "\"result\""));
        JC_CHECK(has(r, "\"stopReason\":\"end_turn\""));
        JC_CHECK(!has(r, "\n")); /* compact: no embedded newline */
        free(r);
    }

    /* NULL result => an empty result object, still a valid response. */
    r = jc_acp_build_response(1, NULL);
    JC_CHECK(has(r, "\"result\":{}"));
    free(r);

    /* Error envelope. */
    r = jc_acp_build_error(3, -32601, "method not found");
    JC_CHECK(has(r, "\"id\":3"));
    JC_CHECK(has(r, "\"error\""));
    JC_CHECK(has(r, "\"code\":-32601"));
    JC_CHECK(has(r, "method not found"));
    free(r);
}

static void test_init_result(void)
{
    cJSON *res = jc_acp_build_init_result(JC_ACP_PROTOCOL_VERSION, 0, 0);
    char *s = jc_json_print(res);
    JC_CHECK(has(s, "\"protocolVersion\":1"));
    JC_CHECK(has(s, "\"agentCapabilities\""));
    JC_CHECK(has(s, "\"loadSession\":true"));
    JC_CHECK(has(s, "\"promptCapabilities\""));
    JC_CHECK(has(s, "\"authMethods\":[]"));
    /* Non-vision, no-transcribe model: image + audio advertised false. */
    JC_CHECK(has(s, "\"image\":false"));
    JC_CHECK(has(s, "\"audio\":false"));
    free(s);
    cJSON_Delete(res);

    /* Vision model: image capability is advertised true (M29d). */
    res = jc_acp_build_init_result(JC_ACP_PROTOCOL_VERSION, 1, 0);
    s = jc_json_print(res);
    JC_CHECK(has(s, "\"image\":true"));
    JC_CHECK(has(s, "\"audio\":false"));
    free(s);
    cJSON_Delete(res);

    /* Transcribe-role model present: audio capability advertised true (M33). */
    res = jc_acp_build_init_result(JC_ACP_PROTOCOL_VERSION, 0, 1);
    s = jc_json_print(res);
    JC_CHECK(has(s, "\"audio\":true"));
    free(s);
    cJSON_Delete(res);
}

static void test_updates(void)
{
    char *line;

    /* agent_message_chunk wrapped in a session/update notification. */
    line = jc_acp_build_update("sess-1", jc_acp_update_message_chunk("hi"));
    JC_CHECK(has(line, "\"method\":\"session/update\""));
    JC_CHECK(has(line, "\"sessionId\":\"sess-1\""));
    JC_CHECK(has(line, "\"sessionUpdate\":\"agent_message_chunk\""));
    JC_CHECK(has(line, "\"text\":\"hi\""));
    JC_CHECK(!has(line, "\"id\":")); /* a notification has no id */
    free(line);

    /* user_message_chunk (used when replaying a loaded session). */
    line = jc_acp_build_update("sess-1",
                               jc_acp_update_user_message_chunk("hello"));
    JC_CHECK(has(line, "\"sessionUpdate\":\"user_message_chunk\""));
    JC_CHECK(has(line, "\"text\":\"hello\""));
    free(line);

    /* tool_call with parsed rawInput. */
    line = jc_acp_build_update("s",
        jc_acp_update_tool_call("tc-1", "read_file notes.txt", "read",
                                "in_progress", "{\"path\":\"notes.txt\"}"));
    JC_CHECK(has(line, "\"sessionUpdate\":\"tool_call\""));
    JC_CHECK(has(line, "\"toolCallId\":\"tc-1\""));
    JC_CHECK(has(line, "\"kind\":\"read\""));
    JC_CHECK(has(line, "\"status\":\"in_progress\""));
    JC_CHECK(has(line, "\"rawInput\":{"));
    JC_CHECK(has(line, "\"path\":\"notes.txt\""));
    free(line);

    /* tool_call_update completed, with content. */
    line = jc_acp_build_update("s",
        jc_acp_update_tool_call_status("tc-1", "completed", "done"));
    JC_CHECK(has(line, "\"sessionUpdate\":\"tool_call_update\""));
    JC_CHECK(has(line, "\"status\":\"completed\""));
    JC_CHECK(has(line, "\"type\":\"content\""));
    JC_CHECK(has(line, "\"text\":\"done\""));
    free(line);

    /* failed status, no content text => no content array. */
    line = jc_acp_build_update("s",
        jc_acp_update_tool_call_status("tc-1", "failed", NULL));
    JC_CHECK(has(line, "\"status\":\"failed\""));
    JC_CHECK(!has(line, "\"content\""));
    free(line);
}

static void test_permission(void)
{
    cJSON *p = jc_acp_permission_params("s", "tc-9", "write_file x", "edit");
    char *s = jc_json_print(p);
    JC_CHECK(has(s, "\"sessionId\":\"s\""));
    JC_CHECK(has(s, "\"toolCall\""));
    JC_CHECK(has(s, "\"toolCallId\":\"tc-9\""));
    JC_CHECK(has(s, "\"options\""));
    JC_CHECK(has(s, "\"optionId\":\"allow_once\""));
    JC_CHECK(has(s, "\"optionId\":\"allow_always\""));
    JC_CHECK(has(s, "\"optionId\":\"reject_once\""));
    free(s);
    cJSON_Delete(p);

    /* Parse the various outcomes from a response. */
    JC_CHECK(jc_acp_parse_permission_outcome(
        "{\"id\":1,\"result\":{\"outcome\":{\"outcome\":\"selected\","
        "\"optionId\":\"allow_once\"}}}") == JC_ACP_PERM_ALLOW_ONCE);
    JC_CHECK(jc_acp_parse_permission_outcome(
        "{\"id\":1,\"result\":{\"outcome\":{\"outcome\":\"selected\","
        "\"optionId\":\"allow_always\"}}}") == JC_ACP_PERM_ALLOW_ALWAYS);
    JC_CHECK(jc_acp_parse_permission_outcome(
        "{\"id\":1,\"result\":{\"outcome\":{\"outcome\":\"selected\","
        "\"optionId\":\"reject_once\"}}}") == JC_ACP_PERM_REJECT);
    JC_CHECK(jc_acp_parse_permission_outcome(
        "{\"id\":1,\"result\":{\"outcome\":{\"outcome\":\"cancelled\"}}}")
        == JC_ACP_PERM_CANCELLED);
    /* Garbage / missing outcome defaults to reject (safe). */
    JC_CHECK(jc_acp_parse_permission_outcome("{}") == JC_ACP_PERM_REJECT);
    JC_CHECK(jc_acp_parse_permission_outcome("not json")
        == JC_ACP_PERM_REJECT);
}

static void test_prompt_text(void)
{
    cJSON *params;
    char *text;

    /* Two text blocks join with a newline. */
    params = jc_json_parse(
        "{\"sessionId\":\"s\",\"prompt\":["
        "{\"type\":\"text\",\"text\":\"hello\"},"
        "{\"type\":\"text\",\"text\":\"world\"}]}");
    text = jc_acp_prompt_text(params);
    JC_CHECK(strcmp(text, "hello\nworld") == 0);
    free(text);
    cJSON_Delete(params);

    /* An embedded resource contributes its text; a resource_link its uri. */
    params = jc_json_parse(
        "{\"prompt\":["
        "{\"type\":\"text\",\"text\":\"see\"},"
        "{\"type\":\"resource\",\"resource\":{\"uri\":\"file:///a.c\","
        "\"text\":\"int main(){}\"}}]}");
    text = jc_acp_prompt_text(params);
    JC_CHECK(strstr(text, "see") != NULL);
    JC_CHECK(strstr(text, "int main(){}") != NULL);
    JC_CHECK(strstr(text, "file:///a.c") != NULL);
    free(text);
    cJSON_Delete(params);

    /* No prompt array => empty string (not NULL). */
    params = jc_json_parse("{\"sessionId\":\"s\"}");
    text = jc_acp_prompt_text(params);
    JC_CHECK(text != NULL && text[0] == '\0');
    free(text);
    cJSON_Delete(params);
}

static void test_prompt_images(void)
{
    cJSON *params;
    struct jc_history h;
    struct jc_message *m;

    /* An image block is attached; text blocks are not images. */
    params = jc_json_parse(
        "{\"prompt\":["
        "{\"type\":\"text\",\"text\":\"what is this\"},"
        "{\"type\":\"image\",\"mimeType\":\"image/png\",\"data\":\"QQ==\"},"
        "{\"type\":\"image\",\"data\":\"Zm8=\"}]}"); /* no mimeType => default */
    jc_history_init(&h);
    m = jc_history_add(&h, JC_ROLE_USER, "what is this");
    JC_CHECK(jc_acp_prompt_images(params, m) == 2);
    JC_CHECK(jc_msg_image_count(m) == 2);
    JC_CHECK_STR(jc_msg_image_at(m, 0)->media_type, "image/png");
    JC_CHECK_STR(jc_msg_image_at(m, 0)->data, "QQ==");
    JC_CHECK_STR(jc_msg_image_at(m, 1)->media_type, "image/png"); /* default */
    jc_history_free(&h);
    cJSON_Delete(params);

    /* No image blocks => nothing attached. */
    params = jc_json_parse("{\"prompt\":[{\"type\":\"text\",\"text\":\"x\"}]}");
    jc_history_init(&h);
    m = jc_history_add(&h, JC_ROLE_USER, "x");
    JC_CHECK(jc_acp_prompt_images(params, m) == 0);
    jc_history_free(&h);
    cJSON_Delete(params);
}

static void test_classify(void)
{
    JC_CHECK(strcmp(jc_acp_tool_kind("read_file"), "read") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind("git_status"), "read") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind("edit_file"), "edit") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind("write_file"), "edit") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind("search_code"), "search") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind("run_terminal_command"), "execute") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind("fetch_url"), "fetch") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind("some_mcp_tool"), "other") == 0);
    JC_CHECK(strcmp(jc_acp_tool_kind(NULL), "other") == 0);

    JC_CHECK(strcmp(jc_acp_stop_reason(0), "end_turn") == 0);
    JC_CHECK(strcmp(jc_acp_stop_reason(1), "cancelled") == 0);
}

static void test_fs_proto(void)
{
    cJSON *params;
    char *s;
    int r = 9, w = 9;

    /* Client advertises both fs capabilities. */
    params = jc_json_parse(
        "{\"clientCapabilities\":{\"fs\":{\"readTextFile\":true,"
        "\"writeTextFile\":true}}}");
    jc_acp_client_fs_caps(params, &r, &w);
    JC_CHECK(r == 1 && w == 1);
    cJSON_Delete(params);

    /* Read-only. */
    r = 9; w = 9;
    params = jc_json_parse(
        "{\"clientCapabilities\":{\"fs\":{\"readTextFile\":true}}}");
    jc_acp_client_fs_caps(params, &r, &w);
    JC_CHECK(r == 1 && w == 0);
    cJSON_Delete(params);

    /* No fs object => neither. */
    r = 9; w = 9;
    params = jc_json_parse("{\"clientCapabilities\":{}}");
    jc_acp_client_fs_caps(params, &r, &w);
    JC_CHECK(r == 0 && w == 0);
    cJSON_Delete(params);

    /* Request params carry sessionId + path (+ content for write). */
    {
        cJSON *rp = jc_acp_fs_read_params("sess", "/a/b.c");
        s = jc_json_print(rp);
        JC_CHECK(has(s, "\"sessionId\":\"sess\""));
        JC_CHECK(has(s, "\"path\":\"/a/b.c\""));
        free(s);
        cJSON_Delete(rp);
    }
    {
        cJSON *wp = jc_acp_fs_write_params("sess", "/a/b.c", "hi");
        s = jc_json_print(wp);
        JC_CHECK(has(s, "\"path\":\"/a/b.c\""));
        JC_CHECK(has(s, "\"content\":\"hi\""));
        free(s);
        cJSON_Delete(wp);
    }

    /* Read result parsing: content extracted; error/absent => NULL. */
    s = jc_acp_parse_fs_read_result(
        "{\"id\":1,\"result\":{\"content\":\"file body\"}}");
    JC_CHECK(s != NULL && strcmp(s, "file body") == 0);
    free(s);
    JC_CHECK(jc_acp_parse_fs_read_result(
        "{\"id\":1,\"error\":{\"code\":-1,\"message\":\"x\"}}") == NULL);
    JC_CHECK(jc_acp_parse_fs_read_result("not json") == NULL);
}

static void test_terminal_proto(void)
{
    cJSON *params;
    char *s;
    char *tid;
    int code;
    int trunc;
    int exited;
    char *text;

    /* Capability parsing: present/true, absent. */
    params = jc_json_parse("{\"clientCapabilities\":{\"terminal\":true}}");
    JC_CHECK(jc_acp_client_terminal_cap(params) == 1);
    cJSON_Delete(params);
    params = jc_json_parse("{\"clientCapabilities\":{}}");
    JC_CHECK(jc_acp_client_terminal_cap(params) == 0);
    cJSON_Delete(params);

    /* create params: shell-wrapped command, cwd, byte limit. */
    {
        cJSON *cp = jc_acp_terminal_create_params("sess", "make test",
                                                  "/work", 4096);
        s = jc_json_print(cp);
        JC_CHECK(has(s, "\"sessionId\":\"sess\""));
        JC_CHECK(has(s, "\"command\":\"/bin/sh\""));
        JC_CHECK(has(s, "\"-c\""));
        JC_CHECK(has(s, "make test"));
        JC_CHECK(has(s, "\"cwd\":\"/work\""));
        JC_CHECK(has(s, "\"outputByteLimit\":4096"));
        free(s);
        cJSON_Delete(cp);
    }
    /* cwd omitted when empty; no byte limit when <= 0. */
    {
        cJSON *cp = jc_acp_terminal_create_params("sess", "ls", NULL, 0);
        s = jc_json_print(cp);
        JC_CHECK(!has(s, "\"cwd\""));
        JC_CHECK(!has(s, "outputByteLimit"));
        free(s);
        cJSON_Delete(cp);
    }
    /* id params shared by output/wait/kill/release. */
    {
        cJSON *ip = jc_acp_terminal_id_params("sess", "term-7");
        s = jc_json_print(ip);
        JC_CHECK(has(s, "\"sessionId\":\"sess\""));
        JC_CHECK(has(s, "\"terminalId\":\"term-7\""));
        free(s);
        cJSON_Delete(ip);
    }

    /* terminalId extraction; error/absent => NULL. */
    tid = jc_acp_parse_terminal_id("{\"result\":{\"terminalId\":\"t1\"}}");
    JC_CHECK(tid != NULL && strcmp(tid, "t1") == 0);
    free(tid);
    JC_CHECK(jc_acp_parse_terminal_id(
        "{\"error\":{\"code\":-1,\"message\":\"x\"}}") == NULL);

    /* exitStatus parsing: exited with code 0; null => not exited. */
    code = 9;
    JC_CHECK(jc_acp_parse_exit_status(
        "{\"result\":{\"exitStatus\":{\"exitCode\":0}}}", &code) == 1);
    JC_CHECK(code == 0);
    code = 9;
    JC_CHECK(jc_acp_parse_exit_status(
        "{\"result\":{\"exitStatus\":{\"exitCode\":2}}}", &code) == 1);
    JC_CHECK(code == 2);
    code = 9;
    JC_CHECK(jc_acp_parse_exit_status(
        "{\"result\":{\"exitStatus\":null}}", &code) == 0);

    /* output parsing: text + truncated + exit code. */
    text = NULL; trunc = 9; code = 9; exited = 9;
    JC_CHECK(jc_acp_parse_terminal_output(
        "{\"result\":{\"output\":\"hi\\n\",\"truncated\":true,"
        "\"exitStatus\":{\"exitCode\":1}}}",
        &text, &trunc, &code, &exited) == 1);
    JC_CHECK(text != NULL && strcmp(text, "hi\n") == 0);
    JC_CHECK(trunc == 1);
    JC_CHECK(exited == 1 && code == 1);
    free(text);

    /* output still running: exitStatus absent => exited 0, output captured. */
    text = NULL; exited = 9;
    JC_CHECK(jc_acp_parse_terminal_output(
        "{\"result\":{\"output\":\"partial\"}}",
        &text, &trunc, &code, &exited) == 1);
    JC_CHECK(text != NULL && strcmp(text, "partial") == 0);
    JC_CHECK(exited == 0);
    free(text);

    /* live-terminal tool_call_update embeds the terminalId. */
    {
        char *line = jc_acp_build_update("s",
            jc_acp_update_tool_call_terminal("tc-9", "term-9"));
        JC_CHECK(has(line, "\"sessionUpdate\":\"tool_call_update\""));
        JC_CHECK(has(line, "\"toolCallId\":\"tc-9\""));
        JC_CHECK(has(line, "\"type\":\"terminal\""));
        JC_CHECK(has(line, "\"terminalId\":\"term-9\""));
        free(line);
    }
}

/* --- jc_app fs-delegate routing (stub delegate, no ACP/IO) --- */

static int stub_read_called;
static int stub_read_fail;
static int stub_write_called;

static jc_status stub_read(void *ctx, const char *path, char **out,
                           jc_size *len, struct jc_arena *a)
{
    (void)ctx; (void)path; (void)a;
    stub_read_called++;
    if (stub_read_fail) {
        return JC_ERR_NOTFOUND;
    }
    *out = jc_arena_strdup(a, "from-editor");
    if (len != NULL) {
        *len = (jc_size)strlen("from-editor");
    }
    return JC_OK;
}

static jc_status stub_write(void *ctx, const char *path, const char *data,
                            jc_size len)
{
    (void)ctx; (void)path; (void)data; (void)len;
    stub_write_called++;
    return JC_OK;
}

static void test_fs_delegate_routing(void)
{
    struct jc_app app;
    struct jc_fs_delegate del;
    struct jc_arena *a = jc_arena_new(0);
    char *out = NULL;
    jc_size len = 0;

    memset(&app, 0, sizeof(app));
    app.arena = a;

    /* No delegate => goes to disk; a missing path fails (proves no stub). */
    app.fs = NULL;
    stub_read_called = 0;
    JC_CHECK(jc_app_read_file(&app, "/no/such/jichi-test-file", &out, &len, a)
             != JC_OK);
    JC_CHECK(stub_read_called == 0);

    /* Delegate installed => read is served from the editor buffer. */
    memset(&del, 0, sizeof(del));
    del.read = stub_read;
    del.write = stub_write;
    del.ctx = NULL;
    app.fs = &del;
    stub_read_called = 0;
    stub_read_fail = 0;
    JC_CHECK(jc_app_read_file(&app, "anything", &out, &len, a) == JC_OK);
    JC_CHECK(stub_read_called == 1);
    JC_CHECK(out != NULL && strcmp(out, "from-editor") == 0);

    /* Delegate read failure falls back to disk (missing path => error). */
    stub_read_called = 0;
    stub_read_fail = 1;
    JC_CHECK(jc_app_read_file(&app, "/no/such/jichi-test-file", &out, &len, a)
             != JC_OK);
    JC_CHECK(stub_read_called == 1); /* tried the delegate, then disk */

    /* Write routes through the delegate. */
    stub_write_called = 0;
    JC_CHECK(jc_app_write_file(&app, "anything", "data", 4) == JC_OK);
    JC_CHECK(stub_write_called == 1);

    jc_arena_free(a);
}

/* --- jc_app cmd-delegate routing (stub delegate + local fallback) --- */

static int stub_cmd_called;
static int stub_cmd_fail;

static jc_status stub_cmd(void *ctx, const char *command, jc_size byte_limit,
                          struct jc_sb *out, int *exit_code, int *truncated)
{
    (void)ctx; (void)command; (void)byte_limit; (void)truncated;
    stub_cmd_called++;
    if (stub_cmd_fail) {
        return JC_ERR_IO;
    }
    jc_sb_append(out, "from-editor-terminal");
    if (exit_code != NULL) {
        *exit_code = 0;
    }
    return JC_OK;
}

static void test_cmd_delegate_routing(void)
{
    struct jc_app app;
    struct jc_cmd_delegate del;
    struct jc_sb out;
    int code = 9;
    int trunc = 9;

    memset(&app, 0, sizeof(app));

    /* No delegate => runs locally; echo output is captured, exit 0. */
    app.cmd = NULL;
    jc_sb_init(&out);
    JC_CHECK(jc_app_run_command(&app, "echo jichi_local", 0, &out, &code, &trunc)
             == JC_OK);
    JC_CHECK(out.data != NULL && strstr(out.data, "jichi_local") != NULL);
    JC_CHECK(code == 0);
    jc_sb_free(&out);

    /* Empty command => invalid. */
    jc_sb_init(&out);
    JC_CHECK(jc_app_run_command(&app, "", 0, &out, &code, &trunc) != JC_OK);
    jc_sb_free(&out);

    /* Delegate installed => routed through it. */
    memset(&del, 0, sizeof(del));
    del.run = stub_cmd;
    del.ctx = NULL;
    app.cmd = &del;
    stub_cmd_called = 0;
    stub_cmd_fail = 0;
    code = 9;
    jc_sb_init(&out);
    JC_CHECK(jc_app_run_command(&app, "whatever", 0, &out, &code, &trunc)
             == JC_OK);
    JC_CHECK(stub_cmd_called == 1);
    JC_CHECK(out.data != NULL && strcmp(out.data, "from-editor-terminal") == 0);
    JC_CHECK(code == 0);
    jc_sb_free(&out);

    /* Delegate failure falls back to local execution. */
    stub_cmd_called = 0;
    stub_cmd_fail = 1;
    jc_sb_init(&out);
    JC_CHECK(jc_app_run_command(&app, "echo jichi_fallback", 0, &out, &code,
                                &trunc) == JC_OK);
    JC_CHECK(stub_cmd_called == 1); /* tried delegate, then local */
    JC_CHECK(out.data != NULL && strstr(out.data, "jichi_fallback") != NULL);
    jc_sb_free(&out);
}

void test_acp(void)
{
    test_envelopes();
    test_init_result();
    test_updates();
    test_permission();
    test_prompt_text();
    test_prompt_images();
    test_classify();
    test_fs_proto();
    test_terminal_proto();
    test_fs_delegate_routing();
    test_cmd_delegate_routing();
}
