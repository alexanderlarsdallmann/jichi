/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_mcp.c - offline tests for the MCP protocol layer (jc_mcp_proto.c).
 *
 * No network or subprocess: the JSON-RPC builders and the tools/list and
 * tools/call result parsers are pure, so they are exercised on in-memory
 * strings just like the SSE and embed parsers.
 */

#include "jc_test.h"
#include "jc_mcp.h"
#include "jc_json.h"
#include "jc_vec.h"

#include <stdlib.h>
#include <string.h>

static void test_build_request(void)
{
    cJSON *params = cJSON_CreateObject();
    char *line;
    cJSON *root;

    cJSON_AddStringToObject(params, "name", "read");
    line = jc_mcp_build_request(7, "tools/call", params);
    JC_CHECK(line != NULL);

    /* No embedded newline (newline-framed transport requires it). */
    JC_CHECK(strchr(line, '\n') == NULL);

    root = jc_json_parse(line);
    JC_CHECK(root != NULL);
    JC_CHECK_STR(jc_json_get_str(root, "jsonrpc", NULL), "2.0");
    JC_CHECK(jc_json_get_num(root, "id", -1.0) == 7.0);
    JC_CHECK_STR(jc_json_get_str(root, "method", NULL), "tools/call");
    JC_CHECK_STR(jc_json_get_str(jc_json_get_obj(root, "params"), "name", NULL),
                 "read");
    cJSON_Delete(root);
    free(line);
}

static void test_build_notification(void)
{
    char *line = jc_mcp_build_notification("notifications/initialized", NULL);
    cJSON *root;
    JC_CHECK(line != NULL);
    root = jc_json_parse(line);
    /* A notification carries no id. */
    JC_CHECK(cJSON_GetObjectItem(root, "id") == NULL);
    JC_CHECK_STR(jc_json_get_str(root, "method", NULL),
                 "notifications/initialized");
    cJSON_Delete(root);
    free(line);
}

static void test_message_id(void)
{
    long id = -1;
    JC_CHECK(jc_mcp_message_id("{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{}}",
                               &id) == 1);
    JC_CHECK(id == 42);
    /* A notification has no id. */
    JC_CHECK(jc_mcp_message_id(
        "{\"jsonrpc\":\"2.0\",\"method\":\"x\"}", &id) == 0);
    /* Malformed input is rejected, not crashed on. */
    JC_CHECK(jc_mcp_message_id("not json", &id) == 0);
}

static void test_parse_tools(void)
{
    const char *resp =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"tools\":["
        "{\"name\":\"read_file\",\"description\":\"Read a file\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":"
        "{\"path\":{\"type\":\"string\"}}},"
        "\"annotations\":{\"readOnlyHint\":true}},"
        "{\"name\":\"write_file\",\"description\":\"Write\"}"
        "]}}";
    struct jc_vec tools;
    struct jc_mcp_tool_desc *d;
    jc_status st;

    jc_vec_init(&tools, sizeof(struct jc_mcp_tool_desc));
    st = jc_mcp_parse_tools(resp, &tools);
    JC_CHECK(st == JC_OK);
    JC_CHECK(tools.len == 2);

    d = (struct jc_mcp_tool_desc *)jc_vec_at(&tools, 0);
    JC_CHECK_STR(d->name, "read_file");
    JC_CHECK_STR(d->description, "Read a file");
    JC_CHECK(d->readonly == 1);
    JC_CHECK(d->input_schema_json != NULL);
    /* The serialised schema round-trips to an object with a "path" property. */
    if (d->input_schema_json != NULL) {
        cJSON *s = jc_json_parse(d->input_schema_json);
        JC_CHECK(cJSON_IsObject(jc_json_get_obj(s, "properties")));
        cJSON_Delete(s);
    }

    d = (struct jc_mcp_tool_desc *)jc_vec_at(&tools, 1);
    JC_CHECK_STR(d->name, "write_file");
    JC_CHECK(d->readonly == 0);
    JC_CHECK(d->input_schema_json == NULL);

    {
        jc_size i;
        for (i = 0; i < tools.len; i++) {
            jc_mcp_tool_desc_free(
                (struct jc_mcp_tool_desc *)jc_vec_at(&tools, i));
        }
    }
    jc_vec_free(&tools);
}

static void test_parse_tools_error(void)
{
    const char *resp =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":"
        "{\"code\":-32601,\"message\":\"Method not found\"}}";
    struct jc_vec tools;
    jc_vec_init(&tools, sizeof(struct jc_mcp_tool_desc));
    JC_CHECK(jc_mcp_parse_tools(resp, &tools) == JC_ERR_PROVIDER);
    jc_vec_free(&tools);
}

static void test_parse_call_result(void)
{
    const char *resp =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":["
        "{\"type\":\"text\",\"text\":\"line one\"},"
        "{\"type\":\"text\",\"text\":\"line two\"}],\"isError\":false}}";
    char *text = NULL;
    int is_err = -1;
    jc_status st = jc_mcp_parse_call_result(resp, &text, &is_err);
    JC_CHECK(st == JC_OK);
    JC_CHECK(is_err == 0);
    JC_CHECK_STR(text, "line one\nline two");
    free(text);
}

static void test_parse_call_result_iserror(void)
{
    const char *resp =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":["
        "{\"type\":\"text\",\"text\":\"boom\"}],\"isError\":true}}";
    char *text = NULL;
    int is_err = -1;
    JC_CHECK(jc_mcp_parse_call_result(resp, &text, &is_err) == JC_OK);
    JC_CHECK(is_err == 1);
    JC_CHECK_STR(text, "boom");
    free(text);
}

static void test_parse_call_result_rpc_error(void)
{
    const char *resp =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"error\":"
        "{\"code\":-32602,\"message\":\"Invalid params\"}}";
    char *text = NULL;
    int is_err = -1;
    JC_CHECK(jc_mcp_parse_call_result(resp, &text, &is_err) == JC_OK);
    JC_CHECK(is_err == 1);
    JC_CHECK(text != NULL && strstr(text, "Invalid params") != NULL);
    free(text);
}

static void test_fqname(void)
{
    char buf[80];
    const char *p = buf; /* via a pointer so JC_CHECK_STR's NULL test is real */

    jc_mcp_tool_fqname("git", "list_branches", buf, sizeof(buf));
    JC_CHECK_STR(p, "git__list_branches");

    /* Illegal characters are replaced; the join uses "__". */
    jc_mcp_tool_fqname("my server", "do/it", buf, sizeof(buf));
    JC_CHECK_STR(p, "my_server__do_it");

    /* Bounded write: never overflows the buffer. */
    {
        char small[8];
        jc_mcp_tool_fqname("aaaa", "bbbbbbbb", small, sizeof(small));
        JC_CHECK(strlen(small) < sizeof(small));
    }
}

/* M43: resources / prompts parsers. */
static void test_parse_resources(void)
{
    struct jc_vec rs;
    struct jc_mcp_resource_desc *d;
    const char *resp =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"resources\":["
        "{\"uri\":\"file:///a.txt\",\"name\":\"A\","
        "\"description\":\"first\",\"mimeType\":\"text/plain\"},"
        "{\"name\":\"no uri\"}]}}"; /* the second entry is skipped (no uri) */

    jc_vec_init(&rs, sizeof(struct jc_mcp_resource_desc));
    JC_CHECK(jc_mcp_parse_resources(resp, &rs) == JC_OK);
    JC_CHECK(rs.len == 1);
    d = (struct jc_mcp_resource_desc *)jc_vec_at(&rs, 0);
    JC_CHECK_STR(d->uri, "file:///a.txt");
    JC_CHECK_STR(d->name, "A");
    JC_CHECK_STR(d->description, "first");
    JC_CHECK_STR(d->mime_type, "text/plain");
    jc_mcp_resource_desc_free(d);
    jc_vec_free(&rs);

    /* A JSON-RPC error => JC_ERR_PROVIDER; a server with none => JC_OK, empty. */
    jc_vec_init(&rs, sizeof(struct jc_mcp_resource_desc));
    JC_CHECK(jc_mcp_parse_resources(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32601,"
        "\"message\":\"no\"}}", &rs) == JC_ERR_PROVIDER);
    JC_CHECK(jc_mcp_parse_resources(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}", &rs) == JC_OK);
    JC_CHECK(rs.len == 0);
    jc_vec_free(&rs);
}

static void test_parse_resource_read(void)
{
    char *text = NULL;
    JC_CHECK(jc_mcp_parse_resource_read(
        "{\"result\":{\"contents\":[{\"uri\":\"x\",\"text\":\"hello\"},"
        "{\"uri\":\"y\",\"text\":\"world\"}]}}", &text) == JC_OK);
    JC_CHECK_STR(text, "hello\nworld");
    free(text);

    /* A JSON-RPC error is rendered as text (so a caller can show it). */
    JC_CHECK(jc_mcp_parse_resource_read(
        "{\"error\":{\"code\":-1,\"message\":\"boom\"}}", &text) == JC_OK);
    JC_CHECK(text != NULL && strstr(text, "boom") != NULL);
    free(text);
}

static void test_parse_prompts(void)
{
    struct jc_vec ps;
    struct jc_mcp_prompt_desc *d;
    jc_vec_init(&ps, sizeof(struct jc_mcp_prompt_desc));
    JC_CHECK(jc_mcp_parse_prompts(
        "{\"result\":{\"prompts\":[{\"name\":\"review\","
        "\"description\":\"code review\",\"arguments\":["
        "{\"name\":\"path\",\"required\":true},"
        "{\"name\":\"style\"}]}]}}", &ps) == JC_OK);
    JC_CHECK(ps.len == 1);
    d = (struct jc_mcp_prompt_desc *)jc_vec_at(&ps, 0);
    JC_CHECK_STR(d->name, "review");
    JC_CHECK_STR(d->description, "code review");
    JC_CHECK(d->nargs == 2);
    JC_CHECK_STR(d->args[0].name, "path");
    JC_CHECK(d->args[0].required == 1);
    JC_CHECK_STR(d->args[1].name, "style");
    JC_CHECK(d->args[1].required == 0);
    jc_mcp_prompt_desc_free(d);
    jc_vec_free(&ps);
}

static void test_build_prompt_args(void)
{
    static const char *const names[] = { "path", "style" };
    cJSON *o;
    cJSON *it;

    /* positional fills declared args in order */
    o = jc_mcp_build_prompt_args(names, 2, "src/main.c terse");
    JC_CHECK(o != NULL);
    it = cJSON_GetObjectItemCaseSensitive(o, "path");
    if (JC_REQUIRE(it != NULL && cJSON_IsString(it))) {
        JC_CHECK_STR(it->valuestring, "src/main.c");
    }
    it = cJSON_GetObjectItemCaseSensitive(o, "style");
    if (JC_REQUIRE(it != NULL)) {
        JC_CHECK_STR(it->valuestring, "terse");
    }
    cJSON_Delete(o);

    /* key=value sets by name (and may be declared or not); positional fills
       the remaining declared arg */
    o = jc_mcp_build_prompt_args(names, 2, "style=verbose src/x.c extra=1");
    it = cJSON_GetObjectItemCaseSensitive(o, "style");
    if (JC_REQUIRE(it != NULL)) {
        JC_CHECK_STR(it->valuestring, "verbose");
    }
    it = cJSON_GetObjectItemCaseSensitive(o, "path");
    if (JC_REQUIRE(it != NULL)) {
        JC_CHECK_STR(it->valuestring, "src/x.c");
    }
    it = cJSON_GetObjectItemCaseSensitive(o, "extra");
    if (JC_REQUIRE(it != NULL)) { /* undeclared key=value still passed through */
        JC_CHECK_STR(it->valuestring, "1");
    }
    cJSON_Delete(o);

    /* NULL raw -> empty object; extra positionals beyond declared are dropped */
    o = jc_mcp_build_prompt_args(names, 2, NULL);
    JC_CHECK(o != NULL && cJSON_GetArraySize(o) == 0);
    cJSON_Delete(o);
    o = jc_mcp_build_prompt_args(names, 2, "a b c d");
    JC_CHECK(cJSON_GetArraySize(o) == 2); /* only path + style filled */
    cJSON_Delete(o);
}

static void test_parse_prompt_get(void)
{
    char *text = NULL;
    JC_CHECK(jc_mcp_parse_prompt_get(
        "{\"result\":{\"messages\":["
        "{\"role\":\"user\",\"content\":{\"type\":\"text\",\"text\":\"hi\"}},"
        "{\"role\":\"assistant\",\"content\":{\"type\":\"text\","
        "\"text\":\"yo\"}}]}}", &text) == JC_OK);
    JC_CHECK_STR(text, "user: hi\nassistant: yo");
    free(text);
}

void test_mcp(void)
{
    test_build_request();
    test_build_notification();
    test_message_id();
    test_parse_tools();
    test_parse_tools_error();
    test_parse_call_result();
    test_parse_call_result_iserror();
    test_parse_call_result_rpc_error();
    test_fqname();
    test_parse_resources();
    test_parse_resource_read();
    test_parse_prompts();
    test_build_prompt_args();
    test_parse_prompt_get();
}
