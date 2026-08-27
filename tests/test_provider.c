/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_provider.c - provider request building and streaming accumulation.
 *
 * No network: streaming is driven by feeding synthetic SSE events straight
 * into the provider's on_event handler. */

#include "jc_test.h"
#include "jc_provider.h"
#include "jc_config.h"
#include "jc_message.h"
#include "../src/provider/prov_internal.h"
#include <stdlib.h>
#include <string.h>

static void feed(struct jc_provider *p, struct jc_message *out, int *done,
                 const char *event, const char *data)
{
    struct jc_sse_event ev;
    struct jc_stream_sink sink;
    sink.on_text = NULL;
    sink.user = NULL;
    ev.event = event;
    ev.data = data;
    p->vt->on_event(p, &ev, out, &sink, done);
}

static cJSON *sample_tools(void)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "name", "read_file");
    cJSON_AddStringToObject(t, "description", "read a file");
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddItemToObject(t, "parameters", params);
    cJSON_AddItemToArray(arr, t);
    return arr;
}

static void make_model(struct jc_model_cfg *m, char *provider, char *model)
{
    memset(m, 0, sizeof(*m));
    m->provider = provider;
    m->model = model;
    m->api_base = NULL;
    m->api_key = (char *)"test-key";
    m->temperature = -1.0;
    m->max_tokens = 0;
}

static void test_openai(void)
{
    char prov_name[] = "openai";
    char model_id[] = "gpt-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    char *body;
    cJSON *tools;
    struct jc_message out;
    int done = 0;

    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "hi");
    tools = sample_tools();

    p->vt->build_request(p, &h, "system prompt", tools, 1, &body);
    JC_CHECK(strstr(body, "\"model\":\"gpt-test\"") != NULL);
    JC_CHECK(strstr(body, "\"stream\":true") != NULL);
    JC_CHECK(strstr(body, "\"type\":\"function\"") != NULL);
    JC_CHECK(strstr(body, "\"role\":\"system\"") != NULL);
    free(body);
    cJSON_Delete(tools);

    /* Streaming accumulation. */
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);

    feed(p, &out, &done, "", "{\"choices\":[{\"delta\":{\"content\":\"Hi\"}}]}");
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
         "\"id\":\"call_1\",\"function\":{\"name\":\"read_file\","
         "\"arguments\":\"{\\\"p\"}}]}}]}");
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
         "\"function\":{\"arguments\":\"ath\\\":1}\"}}]}}]}");
    feed(p, &out, &done, "", "[DONE]");

    JC_CHECK(done == 1);
    JC_CHECK_STR(out.content, "Hi");
    JC_CHECK(out.tool_calls.len == 1);
    if (out.tool_calls.len == 1) {
        struct jc_tool_call *tc =
            (struct jc_tool_call *)jc_vec_at(&out.tool_calls, 0);
        JC_CHECK_STR(tc->name, "read_file");
        JC_CHECK_STR(tc->id, "call_1");
        JC_CHECK_STR(tc->arguments_json, "{\"path\":1}");
    }

    /* Manually free `out` fields. */
    {
        jc_size i;
        free(out.content);
        for (i = 0; i < out.tool_calls.len; i++) {
            struct jc_tool_call *tc =
                (struct jc_tool_call *)jc_vec_at(&out.tool_calls, i);
            free(tc->id); free(tc->name); free(tc->arguments_json);
        }
        jc_vec_free(&out.tool_calls);
    }

    jc_history_free(&h);
    p->vt->free(p);
}

/* A reasoning model (LM Studio / vLLM) streams reasoning_content deltas that are
 * NOT the answer. They must never leak into the assistant text; a reasoning-only
 * turn yields empty content (and the provider logs a maxTokens hint). */
static void test_openai_reasoning_only(void)
{
    char prov_name[] = "openai";
    char model_id[] = "reasoner";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_message out;
    int done = 0;

    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }

    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);

    /* Two reasoning-only deltas, an empty-content delta (must not "start"
     * text as far as the answer is concerned), a length-capped finish, DONE. */
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"reasoning_content\":\"Let me think\"}}]}");
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"reasoning_content\":\" harder\"}}]}");
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"content\":\"\"},"
         "\"finish_reason\":\"length\"}]}");
    feed(p, &out, &done, "", "[DONE]");

    JC_CHECK(done == 1);
    /* No answer text leaked from the reasoning; no tool call fabricated. */
    JC_CHECK(out.content == NULL || out.content[0] == '\0');
    JC_CHECK(out.tool_calls.len == 0);

    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);
}

static void test_anthropic(void)
{
    char prov_name[] = "anthropic";
    char model_id[] = "claude-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    char *body;
    cJSON *tools;
    struct jc_message out;
    int done = 0;

    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "hi");
    tools = sample_tools();

    p->vt->build_request(p, &h, "system prompt", tools, 1, &body);
    JC_CHECK(strstr(body, "\"model\":\"claude-test\"") != NULL);
    JC_CHECK(strstr(body, "\"max_tokens\":") != NULL);
    JC_CHECK(strstr(body, "\"system\":\"system prompt\"") != NULL);
    JC_CHECK(strstr(body, "\"input_schema\"") != NULL);
    free(body);
    cJSON_Delete(tools);

    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);

    feed(p, &out, &done, "message_start", "{\"type\":\"message_start\"}");
    feed(p, &out, &done, "content_block_start",
         "{\"type\":\"content_block_start\",\"index\":0,"
         "\"content_block\":{\"type\":\"text\"}}");
    feed(p, &out, &done, "content_block_delta",
         "{\"type\":\"content_block_delta\",\"index\":0,"
         "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}");
    feed(p, &out, &done, "content_block_delta",
         "{\"type\":\"content_block_delta\",\"index\":0,"
         "\"delta\":{\"type\":\"text_delta\",\"text\":\" world\"}}");
    feed(p, &out, &done, "content_block_start",
         "{\"type\":\"content_block_start\",\"index\":1,"
         "\"content_block\":{\"type\":\"tool_use\",\"id\":\"tu_1\","
         "\"name\":\"read_file\"}}");
    feed(p, &out, &done, "content_block_delta",
         "{\"type\":\"content_block_delta\",\"index\":1,"
         "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\":\"}}");
    feed(p, &out, &done, "content_block_delta",
         "{\"type\":\"content_block_delta\",\"index\":1,"
         "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"x\\\"}\"}}");
    feed(p, &out, &done, "message_stop", "{\"type\":\"message_stop\"}");

    JC_CHECK(done == 1);
    JC_CHECK_STR(out.content, "Hello world");
    JC_CHECK(out.tool_calls.len == 1);
    if (out.tool_calls.len == 1) {
        struct jc_tool_call *tc =
            (struct jc_tool_call *)jc_vec_at(&out.tool_calls, 0);
        JC_CHECK_STR(tc->name, "read_file");
        JC_CHECK_STR(tc->id, "tu_1");
        JC_CHECK_STR(tc->arguments_json, "{\"path\":\"x\"}");
    }

    {
        jc_size i;
        free(out.content);
        for (i = 0; i < out.tool_calls.len; i++) {
            struct jc_tool_call *tc =
                (struct jc_tool_call *)jc_vec_at(&out.tool_calls, i);
            free(tc->id); free(tc->name); free(tc->arguments_json);
        }
        jc_vec_free(&out.tool_calls);
    }

    jc_history_free(&h);
    p->vt->free(p);
}

/* M31a: both providers parse the cache-token counts the wire already carries,
 * exposed via get_cache_usage. OpenAI-compatible servers report only the cached
 * read (prompt_tokens_details.cached_tokens); Anthropic reports both the cache
 * read and the cache write (message_start usage). */
static void test_cache_usage(void)
{
    char oa_prov[] = "openai";
    char oa_id[] = "gpt-test";
    char an_prov[] = "anthropic";
    char an_id[] = "claude-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_message out;
    int done = 0;
    double read_in = -1.0, write_in = -1.0;
    double in_tok = 0.0, out_tok = 0.0;

    /* OpenAI: cached_tokens on the final usage chunk; no write count. */
    make_model(&m, oa_prov, oa_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }
    JC_CHECK(p->vt->get_cache_usage != NULL);
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);
    feed(p, &out, &done, "",
         "{\"choices\":[],\"usage\":{\"prompt_tokens\":100,"
         "\"completion_tokens\":20,"
         "\"prompt_tokens_details\":{\"cached_tokens\":80}}}");
    p->vt->get_usage(p, &in_tok, &out_tok);
    p->vt->get_cache_usage(p, &read_in, &write_in);
    /* usage_in is the uncached remainder: prompt_tokens(100) - cached(80) = 20
     * (M31c normalizes OpenAI to the Anthropic "input = full-price" semantics). */
    JC_CHECK(in_tok == 20.0);
    JC_CHECK(read_in == 80.0);
    JC_CHECK(write_in == 0.0);
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);

    /* Anthropic: cache_read_input_tokens + cache_creation_input_tokens. */
    done = 0;
    read_in = -1.0;
    write_in = -1.0;
    make_model(&m, an_prov, an_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }
    JC_CHECK(p->vt->get_cache_usage != NULL);
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);
    feed(p, &out, &done, "message_start",
         "{\"type\":\"message_start\",\"message\":{\"usage\":{"
         "\"input_tokens\":12,\"output_tokens\":1,"
         "\"cache_read_input_tokens\":40,"
         "\"cache_creation_input_tokens\":7}}}");
    feed(p, &out, &done, "message_stop", "{\"type\":\"message_stop\"}");
    p->vt->get_cache_usage(p, &read_in, &write_in);
    JC_CHECK(read_in == 40.0);
    JC_CHECK(write_in == 7.0);
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);

    /* A stream with no cache fields reports zero (the default). */
    read_in = -1.0;
    write_in = -1.0;
    make_model(&m, oa_prov, oa_id);
    p = jc_provider_create(&m);
    p->vt->stream_reset(p);
    p->vt->get_cache_usage(p, &read_in, &write_in);
    JC_CHECK(read_in == 0.0);
    JC_CHECK(write_in == 0.0);
    p->vt->free(p);
}

/* M31b: with promptCache on, the Anthropic request carries cache_control
 * breakpoints -- the system block becomes a one-element array with an ephemeral
 * marker (caches tools+system), and the conversation tail carries one too. With
 * it off the request is byte-for-byte the pre-M31b shape (system is a string,
 * no cache_control). */
static void test_anthropic_cache(void)
{
    char prov_name[] = "anthropic";
    char model_id[] = "claude-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    cJSON *tools;
    char *body;

    make_model(&m, prov_name, model_id);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "hello there");
    tools = sample_tools();

    /* Off (the make_model default): no caching markers, system stays a string. */
    m.prompt_cache = 0;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h, "system prompt", tools, 1, &body);
    JC_CHECK(strstr(body, "cache_control") == NULL);
    JC_CHECK(strstr(body, "\"system\":\"system prompt\"") != NULL);
    free(body);
    p->vt->free(p);

    /* On: system is an array with an ephemeral marker, and the tail message also
     * carries one (its plain string content is promoted to a text block). */
    m.prompt_cache = 1;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h, "system prompt", tools, 1, &body);
    JC_CHECK(strstr(body, "\"cache_control\":{\"type\":\"ephemeral\"}") != NULL);
    JC_CHECK(strstr(body, "\"system\":[{") != NULL);
    JC_CHECK(strstr(body, "\"text\":\"system prompt\"") != NULL);
    /* Two breakpoints: one on system, one on the message tail. */
    {
        const char *q = strstr(body, "cache_control");
        int count = 0;
        while (q != NULL) {
            count++;
            q = strstr(q + 1, "cache_control");
        }
        JC_CHECK(count == 2);
    }
    /* Default TTL (5-minute): no explicit ttl field on the marker. */
    JC_CHECK(strstr(body, "\"ttl\"") == NULL);
    free(body);
    p->vt->free(p);

    /* M31e: with prompt_cache_1h set, the markers carry ttl:"1h". */
    m.prompt_cache = 1;
    m.prompt_cache_1h = 1;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h, "system prompt", tools, 1, &body);
    JC_CHECK(strstr(body,
        "\"cache_control\":{\"type\":\"ephemeral\",\"ttl\":\"1h\"}") != NULL);
    free(body);
    p->vt->free(p);

    cJSON_Delete(tools);
    jc_history_free(&h);
}

/* M31c: with promptCache on, the OpenAI request carries a stable
 * prompt_cache_key (server-side caching is automatic; the key just helps
 * routing). With it off there is no key. */
static void test_openai_cache_key(void)
{
    char prov_name[] = "openai";
    char model_id[] = "gpt-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    cJSON *tools;
    char *body;

    make_model(&m, prov_name, model_id);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "hi");
    tools = sample_tools();

    m.prompt_cache = 0;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h, "sys", tools, 1, &body);
    JC_CHECK(strstr(body, "prompt_cache_key") == NULL);
    free(body);
    p->vt->free(p);

    m.prompt_cache = 1;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h, "sys", tools, 1, &body);
    JC_CHECK(strstr(body, "\"prompt_cache_key\":\"") != NULL);
    free(body);
    p->vt->free(p);

    cJSON_Delete(tools);
    jc_history_free(&h);
}

/* M31d prefix-stability guard: two successive build_request calls on the same
 * provider, with identical inputs, must produce a byte-identical body. Caching
 * only pays off if the rendered tools+system+history prefix is byte-stable
 * across turns, so any nondeterminism the provider injected (a timestamp, an
 * unsorted map, a per-call random) would break it here. The OpenAI
 * prompt_cache_key is generated once per provider instance, so it is stable
 * across calls on the same provider. */
static void test_prefix_stable(void)
{
    char oa_prov[] = "openai";
    char oa_id[] = "gpt-test";
    char an_prov[] = "anthropic";
    char an_id[] = "claude-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    cJSON *tools;
    char *b1;
    char *b2;

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "stable?");
    tools = sample_tools();

    make_model(&m, oa_prov, oa_id);
    m.prompt_cache = 1;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h, "system prompt", tools, 1, &b1);
    p->vt->build_request(p, &h, "system prompt", tools, 1, &b2);
    JC_CHECK(b1 != NULL && b2 != NULL && strcmp(b1, b2) == 0);
    free(b1);
    free(b2);
    p->vt->free(p);

    make_model(&m, an_prov, an_id);
    m.prompt_cache = 1;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h, "system prompt", tools, 1, &b1);
    p->vt->build_request(p, &h, "system prompt", tools, 1, &b2);
    JC_CHECK(b1 != NULL && b2 != NULL && strcmp(b1, b2) == 0);
    free(b1);
    free(b2);
    p->vt->free(p);

    cJSON_Delete(tools);
    jc_history_free(&h);
}

/* M145: a malformed tool-call arguments blob in history must not be silently
 * rebuilt as input:{} -- the model would see a clean empty input beside a
 * tool_result saying its args failed to parse. The raw text is preserved
 * under "_unparsed_arguments"; valid args rebuild unchanged. */
static void test_anthropic_unparsed_args(void)
{
    char prov_name[] = "anthropic";
    char model_id[] = "claude-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    struct jc_message *asst;
    char *body;

    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "do the thing");
    asst = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "c1", "read_file", "{broken");
    jc_history_add_tool_result(&h, "c1",
        "error: could not parse tool arguments as JSON", 1);

    p->vt->build_request(p, &h, "sys", NULL, 1, &body);
    JC_CHECK(strstr(body, "_unparsed_arguments") != NULL);
    JC_CHECK(strstr(body, "{broken") != NULL);
    free(body);
    jc_history_free(&h);

    /* Valid args: rebuilt as the parsed object, no preservation key. */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "again");
    asst = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "c2", "read_file", "{\"path\":\"a.c\"}");
    jc_history_add_tool_result(&h, "c2", "int a;", 0);

    p->vt->build_request(p, &h, "sys", NULL, 1, &body);
    JC_CHECK(strstr(body, "_unparsed_arguments") == NULL);
    JC_CHECK(strstr(body, "\"path\":\"a.c\"") != NULL);
    free(body);
    jc_history_free(&h);

    p->vt->free(p);
}

/* M269: the OpenAI twin of the test above, and it is a HARDER requirement.
 * `arguments` is a string the server re-parses, so echoing an unparseable blob
 * yields a request document that is itself valid -- a lenient server takes it --
 * while litellm/vLLM answers HTTP 400 "Unterminated string" and kills the turn.
 * 400 is not transient, and the bad text stays in the history, so every later
 * turn dies identically: a wedged run, not a glitch. Found live: a small model
 * truncated a large spawn_parallel arguments blob, reproducibly.
 *
 * The contract: the emitted `arguments` string must ALWAYS parse as a JSON
 * object, and when the original did not, it must still carry the original text
 * (M145's reasoning -- a silent {} beside a "your arguments failed to parse"
 * tool_result contradicts the evidence the model needs to self-correct). */
static void test_openai_unparsed_args_wire(void)
{
    char prov_name[] = "openai";
    char model_id[] = "gpt-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    struct jc_message *asst;
    char *body;
    char *wire;

    /* The pure helper, directly. */
    wire = jc_prov_args_wire("{\"path\":\"a.c\"}");
    JC_CHECK(wire != NULL && strcmp(wire, "{\"path\":\"a.c\"}") == 0);
    free(wire);

    wire = jc_prov_args_wire("{\"task\": \"unterminated");
    JC_CHECK(wire != NULL);
    if (wire != NULL) {
        cJSON *o = cJSON_Parse(wire); /* MUST parse: this is the whole point */
        JC_CHECK(o != NULL && cJSON_IsObject(o));
        JC_CHECK(strstr(wire, "_unparsed_arguments") != NULL);
        JC_CHECK(strstr(wire, "unterminated") != NULL);
        cJSON_Delete(o);
        free(wire);
    }

    /* Valid JSON that is not an object still has to become one. */
    wire = jc_prov_args_wire("[1,2,3]");
    JC_CHECK(wire != NULL);
    if (wire != NULL) {
        cJSON *o = cJSON_Parse(wire);
        JC_CHECK(o != NULL && cJSON_IsObject(o));
        cJSON_Delete(o);
        free(wire);
    }

    wire = jc_prov_args_wire(NULL);
    JC_CHECK(wire != NULL && strcmp(wire, "{}") == 0);
    free(wire);

    /* End to end: the built request's arguments field must be parseable. */
    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "review it");
    asst = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "c1", "spawn_parallel",
                         "{\"tasks\": [{\"task\": \"cut off mid-str");
    jc_history_add_tool_result(&h, "c1",
        "error: could not parse tool arguments as JSON", 1);

    p->vt->build_request(p, &h, "sys", NULL, 1, &body);
    JC_CHECK(body != NULL);
    if (body != NULL) {
        cJSON *root = cJSON_Parse(body);
        cJSON *msgs;
        JC_CHECK(root != NULL);
        msgs = (root != NULL) ? cJSON_GetObjectItem(root, "messages") : NULL;
        JC_CHECK(msgs != NULL);
        if (msgs != NULL) {
            int i;
            int checked = 0;
            for (i = 0; i < cJSON_GetArraySize(msgs); i++) {
                cJSON *msg = cJSON_GetArrayItem(msgs, i);
                cJSON *tcs = cJSON_GetObjectItem(msg, "tool_calls");
                cJSON *tc;
                cJSON *fn;
                cJSON *ar;
                if (tcs == NULL || cJSON_GetArraySize(tcs) == 0) continue;
                tc = cJSON_GetArrayItem(tcs, 0);
                fn = cJSON_GetObjectItem(tc, "function");
                ar = (fn != NULL) ? cJSON_GetObjectItem(fn, "arguments") : NULL;
                JC_CHECK(ar != NULL && cJSON_IsString(ar));
                if (ar != NULL && cJSON_IsString(ar)) {
                    cJSON *inner = cJSON_Parse(ar->valuestring);
                    /* The server does exactly this, and 400s if it fails. */
                    JC_CHECK(inner != NULL && cJSON_IsObject(inner));
                    JC_CHECK(strstr(ar->valuestring, "cut off mid-str") != NULL);
                    cJSON_Delete(inner);
                    checked = 1;
                }
            }
            JC_CHECK(checked == 1); /* the tool_call was actually reached */
        }
        cJSON_Delete(root);
        free(body);
    }
    jc_history_free(&h);
    p->vt->free(p);
}

/* M166: the in-flight assistant placeholder must never reach the wire. The
 * agent loop appends an empty assistant message to stream into; serialising it
 * puts a content-free assistant turn at the end of the request, which made a
 * small local model close the turn with one end-of-turn token instead of
 * calling any tool (docs/ANECDOTES.md #19). A *completed* empty assistant
 * message that carries tool calls must still be sent. */
static void test_placeholder_not_serialised(void)
{
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    struct jc_message *ph;
    char *body;
    const char *prov[2];
    int i;

    prov[0] = "openai";
    prov[1] = "anthropic";
    for (i = 0; i < 2; i++) {
        make_model(&m, (char *)prov[i], (char *)"m1");
        p = jc_provider_create(&m);
        JC_CHECK(p != NULL);
        if (p == NULL) {
            continue;
        }

        /* user turn + the empty assistant placeholder the loop just appended */
        jc_history_init(&h);
        jc_history_add(&h, JC_ROLE_USER, "read config.txt");
        jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
        body = NULL;
        p->vt->build_request(p, &h, "sys", NULL, 1, &body);
        JC_CHECK(body != NULL);
        if (body != NULL) {
            /* the user turn survives; no assistant turn is emitted at all */
            JC_CHECK(strstr(body, "read config.txt") != NULL);
            JC_CHECK(strstr(body, "\"assistant\"") == NULL);
            free(body);
        }
        jc_history_free(&h);

        /* An assistant message with tool calls but no text is NOT a
         * placeholder -- it is a real turn and must be serialised. */
        jc_history_init(&h);
        jc_history_add(&h, JC_ROLE_USER, "go");
        ph = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
        jc_msg_add_tool_call(ph, "id1", "read_file", "{\"path\":\"a.c\"}");
        body = NULL;
        p->vt->build_request(p, &h, "sys", NULL, 1, &body);
        JC_CHECK(body != NULL);
        if (body != NULL) {
            JC_CHECK(strstr(body, "read_file") != NULL);
            free(body);
        }
        jc_history_free(&h);
        p->vt->free(p);
    }
}

/* M218: the mid-turn argument-elision marker must round-trip both wires as a
 * real JSON object. The Anthropic serializer re-parses arguments_json -- a
 * non-object would degrade to _unparsed_arguments -- and the OpenAI one emits
 * the string verbatim inside function.arguments. */
static void test_elided_args_marker_roundtrip(void)
{
    static const char *marker =
        "{\"elided\":\"arguments (4211 bytes) elided mid-turn to fit the "
        "context window\",\"path\":\"src/foo.c\"}";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    struct jc_message *a;
    char *body;

    /* Anthropic: parsed into a clean input object. */
    {
        char prov_name[] = "anthropic";
        char model_id[] = "claude-test";
        make_model(&m, prov_name, model_id);
        jc_history_init(&h);
        jc_history_add(&h, JC_ROLE_USER, "write it");
        a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
        jc_msg_add_tool_call(a, "w1", "write_file", marker);
        jc_history_add_tool_result(&h, "w1", "ok", 0);
        jc_history_add(&h, JC_ROLE_ASSISTANT, "done");
        p = jc_provider_create(&m);
        body = NULL;
        p->vt->build_request(p, &h, "sys", NULL, 1, &body);
        JC_CHECK(body != NULL);
        if (body != NULL) {
            JC_CHECK(strstr(body, "_unparsed_arguments") == NULL);
            JC_CHECK(strstr(body, "\"path\":\"src/foo.c\"") != NULL);
            JC_CHECK(strstr(body, "elided mid-turn") != NULL);
            free(body);
        }
        jc_history_free(&h);
        p->vt->free(p);
    }
    /* OpenAI: the exact marker string, JSON-escaped, inside arguments. */
    {
        char prov_name[] = "openai";
        char model_id[] = "gpt-test";
        make_model(&m, prov_name, model_id);
        jc_history_init(&h);
        jc_history_add(&h, JC_ROLE_USER, "write it");
        a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
        jc_msg_add_tool_call(a, "w1", "write_file", marker);
        jc_history_add_tool_result(&h, "w1", "ok", 0);
        jc_history_add(&h, JC_ROLE_ASSISTANT, "done");
        p = jc_provider_create(&m);
        body = NULL;
        p->vt->build_request(p, &h, "sys", NULL, 1, &body);
        JC_CHECK(body != NULL);
        if (body != NULL) {
            JC_CHECK(strstr(body, "elided mid-turn") != NULL);
            JC_CHECK(strstr(body, "\\\"path\\\":\\\"src/foo.c\\\"") != NULL);
            free(body);
        }
        jc_history_free(&h);
        p->vt->free(p);
    }
}

static void test_length_cap_marks_truncated(void)
{
    /* M334: the provider must record that the model was cut off at its
     * output-token ceiling. Without this the tool layer sees a severed tool
     * call as merely malformed, M148 "repairs" it into a valid object with no
     * fields, and the model is told to fix an argument shape that was never
     * wrong -- 197 identical calls and 6,977,850 tokens in one measured run. */
    char prov_name[] = "openai";
    char model_id[] = "m";
    char an_name[] = "anthropic";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_message out;
    int done = 0;

    /* OpenAI: finish_reason "length". */
    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"content\":\"partial\"},"
         "\"finish_reason\":\"length\"}]}");
    feed(p, &out, &done, "", "[DONE]");
    JC_CHECK(out.truncated == 1);
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);

    /* OpenAI: an ordinary stop must NOT be marked. A flag that is always on
     * would suppress M148's repair for every genuinely malformed call. */
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    done = 0;
    p->vt->stream_reset(p);
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"content\":\"all of it\"},"
         "\"finish_reason\":\"stop\"}]}");
    feed(p, &out, &done, "", "[DONE]");
    JC_CHECK(out.truncated == 0);
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);

    /* Anthropic: the same event, spelled stop_reason "max_tokens". */
    make_model(&m, an_name, model_id);
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    done = 0;
    p->vt->stream_reset(p);
    feed(p, &out, &done, "message_delta",
         "{\"type\":\"message_delta\",\"delta\":"
         "{\"stop_reason\":\"max_tokens\"},\"usage\":{\"output_tokens\":9}}");
    feed(p, &out, &done, "message_stop", "{\"type\":\"message_stop\"}");
    JC_CHECK(out.truncated == 1);
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);

    /* Anthropic: end_turn is not a truncation. */
    p = jc_provider_create(&m);
    /* JC_REQUIRE, not JC_CHECK: the lines below dereference p. JC_CHECK
     * records and continues, so it is not a guard (M457). Nothing is
     * allocated yet at this point, so returning here leaks nothing. */
    if (!JC_REQUIRE(p != NULL)) { return; }
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    done = 0;
    p->vt->stream_reset(p);
    feed(p, &out, &done, "message_delta",
         "{\"type\":\"message_delta\",\"delta\":"
         "{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":9}}");
    feed(p, &out, &done, "message_stop", "{\"type\":\"message_stop\"}");
    JC_CHECK(out.truncated == 0);
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);
}

void test_provider(void)
{
    test_length_cap_marks_truncated();
    test_placeholder_not_serialised();
    test_openai();
    test_openai_reasoning_only();
    test_anthropic();
    test_anthropic_unparsed_args();
    test_openai_unparsed_args_wire();
    test_cache_usage();
    test_anthropic_cache();
    test_openai_cache_key();
    test_prefix_stable();
    test_elided_args_marker_roundtrip();
}
