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
#include "jc_snprintf.h"
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

/* M909 helper: feed one SSE event to a fresh `prov_name_in` provider and
 * return the stream-error message it recorded (NULL when none), writing it to
 * `msg`. Also reports whether any ANSWER text accumulated (`*answer_len`) --
 * an in-stream error must set the message and emit nothing as the answer. */
static const char *error_after(const char *prov_name_in, const char *event,
                               const char *data, char *msg, size_t cap,
                               int *answer_len)
{
    char prov_name[16];
    char model_id[] = "m";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_message out;
    const char *e;
    int done = 0;

    jc_snprintf(prov_name, sizeof prov_name, "%s", prov_name_in);
    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    if (!JC_REQUIRE(p != NULL)) { return NULL; }
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);
    msg[0] = '\0';
    *answer_len = -1;
    JC_CHECK(p->vt->stream_error != NULL);
    if (p->vt->stream_error != NULL) {
        JC_CHECK(p->vt->stream_error(p) == NULL);
        feed(p, &out, &done, event, data);
        e = p->vt->stream_error(p);
        if (e != NULL) {
            jc_snprintf(msg, cap, "%s", e);
        }
        *answer_len = out.content != NULL ? (int)strlen(out.content) : 0;
        p->vt->stream_reset(p);
        JC_CHECK(p->vt->stream_error(p) == NULL);   /* a reset clears it */
    }
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);
    return msg[0] != '\0' ? msg : NULL;
}

/* M909: an error the server sends inside a stream that began 200 must fail
 * the call (set stream_error, emit no answer text), for both dialects, in the
 * shapes LM Studio and OpenAI actually send. Each case red under its own
 * perturbation; the pre-M909 binary sets the message and answer_len together
 * (the error was the answer). */
static void test_stream_error(void)
{
    char msg[600];
    char big[2000];
    int alen = 0;
    /* LM Studio, streamed, over its context: `event: error` and this data */
    JC_CHECK(error_after("openai", "error",
        "{\"error\":{\"message\":\"Engine protocol predict request returned "
        "400: {\\\"error\\\":{\\\"code\\\":400,\\\"message\\\":\\\"request "
        "(4020 tokens) exceeds the available context size (2048 tokens), try "
        "increasing it\\\",\\\"type\\\":\\\"exceed_context_size_error\\\","
        "\\\"n_prompt_tokens\\\":4020,\\\"n_ctx\\\":2048}}\"},\"message\":"
        "\"Engine protocol predict request returned 400\"}",
        msg, sizeof msg, &alen) != NULL);
    JC_CHECK(strstr(msg, "exceeds the available context size") != NULL);
    JC_CHECK(alen == 0);                         /* not the answer */
    /* the OpenAI spelling, no event name */
    JC_CHECK(error_after("openai", "",
        "{\"error\":{\"message\":\"rate limited\",\"type\":\"x\"}}",
        msg, sizeof msg, &alen) != NULL);
    JC_CHECK(strcmp(msg, "rate limited") == 0 && alen == 0);
    /* the error IS the message (LM Studio's unstreamed body shape) */
    JC_CHECK(error_after("openai", "", "{\"error\":\"engine gone\"}",
                         msg, sizeof msg, &alen) != NULL);
    JC_CHECK(strcmp(msg, "engine gone") == 0);
    /* an error event whose data is not JSON */
    JC_CHECK(error_after("openai", "error", "upstream died",
                         msg, sizeof msg, &alen) != NULL);
    JC_CHECK(strcmp(msg, "upstream died") == 0);
    /* CONTROL: ordinary content is no error, and stays the answer */
    JC_CHECK(error_after("openai", "",
        "{\"choices\":[{\"delta\":{\"content\":\"Hi\"}}]}",
        msg, sizeof msg, &alen) == NULL);
    /* Anthropic's in-stream error */
    JC_CHECK(error_after("anthropic", "error",
        "{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\","
        "\"message\":\"Overloaded\"}}", msg, sizeof msg, &alen) != NULL);
    JC_CHECK(strcmp(msg, "Overloaded") == 0 && alen == 0);
    /* a message longer than the slot is cut to fit, on a UTF-8 boundary */
    memset(big, 'x', sizeof big);
    memcpy(big, "{\"error\":\"", 10);
    memcpy(big + sizeof big - 3, "\"}", 3);
    JC_CHECK(error_after("openai", "", big, msg, sizeof msg, &alen) != NULL);
    JC_CHECK(strlen(msg) == 511);
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

/* M897: stream_started -- the agent's first-token clock asks this after each
 * SSE event. Text, reasoning or a tool call starts a stream, in both
 * dialects; a role-only or empty-content delta does not; a reset clears it. */
static int started_after(const char *prov_name_in, const char *event,
                         const char *data)
{
    char prov_name[16];
    char model_id[] = "m";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_message out;
    int done = 0;
    int r;

    jc_snprintf(prov_name, sizeof prov_name, "%s", prov_name_in);
    make_model(&m, prov_name, model_id);
    p = jc_provider_create(&m);
    if (!JC_REQUIRE(p != NULL)) { return -1; }
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);
    JC_CHECK(p->vt->stream_started != NULL);
    if (p->vt->stream_started == NULL) {
        r = -1;
    } else {
        JC_CHECK(p->vt->stream_started(p) == 0);
        feed(p, &out, &done, event, data);
        r = p->vt->stream_started(p);
        p->vt->stream_reset(p);
        JC_CHECK(p->vt->stream_started(p) == 0);
    }
    free(out.content);
    jc_vec_free(&out.tool_calls);
    p->vt->free(p);
    return r;
}

static void test_stream_started(void)
{
    /* OpenAI wire */
    JC_CHECK(started_after("openai", "",
        "{\"choices\":[{\"delta\":{\"content\":\"H\"}}]}") == 1);
    JC_CHECK(started_after("openai", "",
        "{\"choices\":[{\"delta\":{\"reasoning_content\":\"hm\"}}]}") == 1);
    JC_CHECK(started_after("openai", "",
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c\","
        "\"function\":{\"name\":\"read_file\",\"arguments\":\"\"}}]}}]}") == 1);
    JC_CHECK(started_after("openai", "",
        "{\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"\"}}]}") == 0);
    /* Anthropic wire */
    JC_CHECK(started_after("anthropic", "content_block_delta",
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"H\"}}") == 1);
    JC_CHECK(started_after("anthropic", "content_block_delta",
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"hm\"}}") == 1);
    JC_CHECK(started_after("anthropic", "content_block_start",
        "{\"type\":\"content_block_start\",\"index\":1,\"content_block\":"
        "{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"read_file\",\"input\":{}}}") == 1);
    JC_CHECK(started_after("anthropic", "message_start",
        "{\"type\":\"message_start\",\"message\":{\"usage\":"
        "{\"input_tokens\":5,\"output_tokens\":1}}}") == 0);
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

/* The bytes of the request that the prompt cache depends on staying
 * IDENTICAL across turns: for Anthropic, model + max_tokens + the tools+system
 * prefix that the first cache_control breakpoint covers (up to the conversation
 * array); for OpenAI, model + the system message (tools render later, so the
 * anchor is the head up to the messages array). The tail -- the conversation --
 * is meant to grow. Returns 0 on success. */
static long stable_prefix(const char *body, char *out, size_t out_sz,
                          const char *anchor)
{
    const char *a = strstr(body, anchor);
    if (a == NULL) {
        return -1;
    }
    if ((size_t)(a - body) >= out_sz) {
        return -1;
    }
    memcpy(out, body, (size_t)(a - body));
    out[a - body] = '\0';
    return 0;
}

/* M31d's test_prefix_stable asserts byte-identical bodies for two calls on the
 * SAME inputs -- no nondeterminism within one call. It does NOT cover the
 * property the cache actually buys: that the tools+system PREFIX stays byte-
 * identical as the CONVERSATION GROWS from turn 1 to turn 2 (PROMPT_CACHING
 * rec 1; the 2026-10-05 self-audit found no affirmative test for it). A live
 * number injected into the system prefix (a per-turn percent, a timestamp)
 * would pass M31d (same inputs, same output) but make the prefix churn turn to
 * turn, so the cache silently stops matching and the M365 sentinel fires on
 * legitimate operation. This is the falsifier for the standing note: it must
 * stay green after that change, and it goes red if the change lands in the
 * prefix rather than the message tail. */
static void test_prefix_stable_across_turns(void)
{
    char an_prov[] = "anthropic";
    char an_id[] = "claude-test";
    char oa_prov[] = "openai";
    char oa_id[] = "gpt-test";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h1;
    struct jc_history h2;
    cJSON *tools;
    char *b1;
    char *b2;
    char p1[1024];
    char p2[1024];

    jc_history_init(&h1);
    jc_history_add(&h1, JC_ROLE_USER, "turn one");
    jc_history_init(&h2);
    jc_history_add(&h2, JC_ROLE_USER, "turn one");
    jc_history_add(&h2, JC_ROLE_ASSISTANT, "here is my answer");
    jc_history_add(&h2, JC_ROLE_USER, "turn two");
    tools = sample_tools();

    /* Anthropic: prefix = head through the tools+system block (anchor the
     * messages array, which holds only the growing conversation). */
    make_model(&m, an_prov, an_id);
    m.prompt_cache = 1;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h1, "system prompt", tools, 1, &b1);
    p->vt->build_request(p, &h2, "system prompt", tools, 1, &b2);
    JC_CHECK(b1 != NULL && b2 != NULL);
    if (b1 != NULL && b2 != NULL) {
        JC_CHECK(stable_prefix(b1, p1, sizeof(p1), "\"messages\":[") == 0);
        JC_CHECK(stable_prefix(b2, p2, sizeof(p2), "\"messages\":[") == 0);
        JC_CHECK(strcmp(p1, p2) == 0);
        if (strcmp(p1, p2) != 0) {
            printf("  anthropic prefix churned across turns:\n    t1: %s\n"
                   "    t2: %s\n", p1, p2);
        }
        /* The tail IS allowed to grow: the two full bodies differ. */
        JC_CHECK(strcmp(b1, b2) != 0);
        free(b1);
        free(b2);
    }
    p->vt->free(p);

    /* OpenAI: prefix = model + system (anchor the messages array; tools render
     * after it, so they are not part of the stable head). */
    make_model(&m, oa_prov, oa_id);
    m.prompt_cache = 1;
    p = jc_provider_create(&m);
    p->vt->build_request(p, &h1, "system prompt", tools, 1, &b1);
    p->vt->build_request(p, &h2, "system prompt", tools, 1, &b2);
    JC_CHECK(b1 != NULL && b2 != NULL);
    if (b1 != NULL && b2 != NULL) {
        JC_CHECK(stable_prefix(b1, p1, sizeof(p1), "\"messages\":") == 0);
        JC_CHECK(stable_prefix(b2, p2, sizeof(p2), "\"messages\":") == 0);
        JC_CHECK(strcmp(p1, p2) == 0);
        if (strcmp(p1, p2) != 0) {
            printf("  openai prefix churned across turns:\n    t1: %s\n"
                   "    t2: %s\n", p1, p2);
        }
        JC_CHECK(strcmp(b1, b2) != 0);
        free(b1);
        free(b2);
    }
    p->vt->free(p);

    cJSON_Delete(tools);
    jc_history_free(&h1);
    jc_history_free(&h2);
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
    /* M1090 (b): the model's own output-token limit is the cause, NOT jichi's
     * per-stream cap -- so the by-cap marker must stay clear. */
    JC_CHECK(out.truncated_by_cap == 0);
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
    /* M1090 (b): the model's own output-token limit is the cause, not jichi's
     * per-stream cap. */
    JC_CHECK(out.truncated_by_cap == 0);
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

/* M858 (N42): a reply that used the whole output budget is cut, whatever its
 * finish_reason says. Measured on the institution's gateway: a TOOL-CALL reply cut
 * at max_tokens ends "tool_calls", with completion_tokens equal to max_tokens --
 * M334's finish_reason check never saw it. One below the ceiling, and a reply with
 * no usage at all, are NOT marked: a size that is unknown is never a verdict. */
static void usage_case(long out_tok, int with_usage, int want)
{
    char prov_name[] = "openai";
    char model_id[] = "m";
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_message out;
    int done = 0;
    char usage[200];

    make_model(&m, prov_name, model_id);
    m.max_tokens = 48;
    p = jc_provider_create(&m);
    if (!JC_REQUIRE(p != NULL)) { return; }
    memset(&out, 0, sizeof(out));
    out.role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
    p->vt->stream_reset(p);
    feed(p, &out, &done, "",
         "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c1\","
         "\"type\":\"function\",\"function\":{\"name\":\"write_file\","
         "\"arguments\":\"{\\\"path\\\":\\\"a\\\",\\\"content\\\":\\\"ab\"}}]},"
         "\"finish_reason\":\"tool_calls\"}]}");
    if (with_usage) {
        jc_snprintf(usage, sizeof usage,
                    "{\"choices\":[],\"usage\":{\"prompt_tokens\":20,"
                    "\"completion_tokens\":%ld}}", out_tok);
        feed(p, &out, &done, "", usage);
    }
    feed(p, &out, &done, "", "[DONE]");
    JC_CHECK(out.truncated == want);
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
    p->vt->free(p);
}

static void test_usage_at_ceiling_marks_truncated(void)
{
    usage_case(48, 1, 1);    /* the gateway's cut tool call */
    usage_case(47, 1, 0);    /* one below the ceiling: complete */
    usage_case(0, 0, 0);     /* no usage: unknown, never marked */
}

/* M718 (plan D4): NO HOUSE DIALECT. The factory used to guess the dialect from
 * the model id -- "gpt" in it meant OpenAI -- and default to Anthropic's, so an
 * entry naming no provider, or "ollama", spoke the Anthropic Messages API to
 * whatever server it named. Every row below used to return a provider. */
static void test_no_house_dialect(void)
{
    struct jc_model_cfg m;
    struct jc_provider *p;

    make_model(&m, NULL, (char *)"some-model");
    JC_CHECK(jc_provider_create(&m) == NULL);
    make_model(&m, NULL, (char *)"gpt-4o");          /* the old heuristic */
    JC_CHECK(jc_provider_create(&m) == NULL);
    make_model(&m, (char *)"ollama", (char *)"qwen");
    JC_CHECK(jc_provider_create(&m) == NULL);
    make_model(&m, (char *)"", (char *)"qwen");
    JC_CHECK(jc_provider_create(&m) == NULL);
    JC_CHECK(jc_provider_create(NULL) == NULL);
    /* The two dialects still construct. */
    make_model(&m, (char *)"openai", (char *)"some-model");
    p = jc_provider_create(&m);
    JC_CHECK(p != NULL);
    if (p != NULL) {
        p->vt->free(p);
    }
    make_model(&m, (char *)"anthropic", (char *)"some-model");
    p = jc_provider_create(&m);
    JC_CHECK(p != NULL);
    if (p != NULL) {
        p->vt->free(p);
    }
}

/* A-07 / M1090 (b): free an out message built for a cap test -- its text and
 * every tool call's strings, then the two vectors. (Same teardown the blocks
 * below used to inline; one helper so a new block cannot forget a free.) */
static void free_msg_for_test(struct jc_message *out)
{
    jc_size j;
    free(out->content);
    for (j = 0; j < out->tool_calls.len; j++) {
        struct jc_tool_call *tc =
            (struct jc_tool_call *)jc_vec_at(&out->tool_calls, j);
        free(tc->id); free(tc->name); free(tc->arguments_json);
    }
    jc_vec_free(&out->tool_calls);
}

/* A fresh out for a cap-test flush phase. */
static void prep_out_for_test(struct jc_message *out)
{
    memset(out, 0, sizeof(*out));
    out->role = JC_ROLE_ASSISTANT;
    jc_vec_init(&out->tool_calls, sizeof(struct jc_tool_call));
}

/* M1090 (A-07): a stream's accumulated text and per-tool-call args are capped,
 * and the cap is honoured by marking the flushed message truncated. The test
 * lowers each cap to 16 bytes (no 4 MiB fixture needed), feeds deltas that
 * request 40 text bytes / 24 arg bytes, and asserts the accumulation stops at
 * 16 and out.truncated is set. A final sanity case at the default cap proves a
 * normal short stream is NOT falsely truncated.
 *
 * The ARGS half covers EACH of the four dialect routes (the supervisor proved
 * the wave-1 test drove only the openai STREAM route: reverting any of the
 * other three append sites to a plain jc_sb_append left `make test` green).
 * Route 1 (openai stream) via on_event; route 2 (openai full) via
 * vt->parse_full on a body whose tool-call arguments string is longer than the
 * cap; route 3 (anthropic stream) via on_event (content_block_start + several
 * input_json_delta past the cap); route 4 (anthropic full) via vt->parse_full
 * on a body whose tool_use input serialises past the cap. Each block asserts
 * the accumulated args stay <= 16, the cap set truncated_reply, and the
 * flush carried it to out.truncated (and, for the full routes, the flushed
 * tool call's own arguments string is <= 16).
 *
 * RED before the fix: with no cap on a route, that slot's args.len is the full
 * requested length (24 openai stream/full, 27 anthropic) -- not <= 16 -- and
 * the flush leaves out.truncated == 0. */
static void test_provider_stream_caps(void)
{
    size_t oldt = jc_prov_get_max_text();
    size_t olda = jc_prov_get_max_args();
    char model_id[] = "m";

    /* ---- TEXT case ------------------------------------------------------ */
    jc_prov_set_max_text(16);
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;
        int i;

        make_model(&m, "openai", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            jc_prov_reset_max_text();
            jc_prov_reset_max_args();
            return;
        }
        memset(&out, 0, sizeof(out));
        out.role = JC_ROLE_ASSISTANT;
        jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
        p->vt->stream_reset(p);
        /* Five 8-byte content deltas = 40 bytes requested. */
        for (i = 0; i < 5; i++) {
            feed(p, &out, &done, "",
                 "{\"choices\":[{\"delta\":{\"content\":\"aaaaaaaa\"}}]}");
        }
        s = (struct prov_state *)p->state;
        /* Pre-fix: s->text.len is 40, so this is the red line. */
        JC_CHECK(s->text.len <= 16);
        JC_CHECK(s->truncated_reply == 1);
        memset(&out, 0, sizeof(out));
        out.role = JC_ROLE_ASSISTANT;
        jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
        jc_prov_flush(s, &out);
        /* Pre-fix: flush never sets this for a cap, so red. */
        JC_CHECK(out.truncated == 1);
        {
            jc_size j;
            free(out.content);
            for (j = 0; j < out.tool_calls.len; j++) {
                struct jc_tool_call *tc =
                    (struct jc_tool_call *)jc_vec_at(&out.tool_calls, j);
                free(tc->id); free(tc->name); free(tc->arguments_json);
            }
            jc_vec_free(&out.tool_calls);
        }
        p->vt->free(p);
    }
    jc_prov_reset_max_text();

    /* ---- ARGS: route 1, openai STREAM (the wave-1 route) ---------------- */
    jc_prov_set_max_args(16);
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;
        int i;

        make_model(&m, "openai", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            jc_prov_reset_max_args();
            return;
        }
        prep_out_for_test(&out);
        p->vt->stream_reset(p);
        /* Three 8-byte argument deltas = 24 bytes of args requested. */
        for (i = 0; i < 3; i++) {
            feed(p, &out, &done, "",
                 "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
                 "\"function\":{\"arguments\":\"abcdefgh\"}}]}}]}");
        }
        s = (struct prov_state *)p->state;
        JC_CHECK(s->calls[0].used == 1);
        /* Pre-fix: s->calls[0].args.len is 24, so this is the red line. */
        JC_CHECK(s->calls[0].args.len <= 16);
        JC_CHECK(s->truncated_reply == 1);
        prep_out_for_test(&out);
        jc_prov_flush(s, &out);
        JC_CHECK(out.truncated == 1);
        /* M1090 (b): the cause is jichi's own cap, not the model's limit. */
        JC_CHECK(out.truncated_by_cap == 1);
        free_msg_for_test(&out);
        p->vt->free(p);
    }

    /* ---- ARGS: route 2, openai FULL (vt->parse_full) --------------------- */
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        jc_status st;
        /* A 24-byte arguments string (cap 16): parse_full must clip it. */
        const char *body =
            "{\"choices\":[{\"message\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"c1\",\"type\":\"function\",\"function\":{\"name\":"
            "\"write_file\",\"arguments\":"
            "\"aaaaaaaaaaaaaaaaaaaaaaaa\"}}],"
            "\"finish_reason\":\"tool_calls\"}}]}";

        make_model(&m, "openai", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            jc_prov_reset_max_args();
            return;
        }
        prep_out_for_test(&out);
        s = (struct prov_state *)p->state;
        st = p->vt->parse_full(p, body, &out);
        JC_CHECK(st == JC_OK);
        JC_CHECK(s->calls[0].used == 1);
        /* Pre-fix: 24, so this is the red line for the openai full route. */
        JC_CHECK(s->calls[0].args.len <= 16);
        JC_CHECK(s->truncated_reply == 1);
        /* parse_full already flushed; the flushed tool call is clipped too. */
        JC_CHECK(out.truncated == 1);
        /* M1090 (b): the cause is jichi's own cap, not the model's limit. */
        JC_CHECK(out.truncated_by_cap == 1);
        JC_CHECK(out.tool_calls.len == 1);
        if (out.tool_calls.len == 1) {
            struct jc_tool_call *tc =
                (struct jc_tool_call *)jc_vec_at(&out.tool_calls, 0);
            JC_CHECK(tc->arguments_json != NULL &&
                     strlen(tc->arguments_json) <= 16);
        }
        free_msg_for_test(&out);
        p->vt->free(p);
    }

    /* ---- ARGS: route 3, anthropic STREAM (on_event) ---------------------- */
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;

        make_model(&m, "anthropic", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            jc_prov_reset_max_args();
            return;
        }
        prep_out_for_test(&out);
        p->vt->stream_reset(p);
        feed(p, &out, &done, "content_block_start",
             "{\"type\":\"content_block_start\",\"index\":0,"
             "\"content_block\":{\"type\":\"tool_use\",\"id\":\"tu_1\","
             "\"name\":\"write_file\"}}");
        /* Three 9-byte input_json_delta fragments = 27 bytes of args. */
        feed(p, &out, &done, "content_block_delta",
             "{\"type\":\"content_block_delta\",\"index\":0,"
             "\"delta\":{\"type\":\"input_json_delta\","
             "\"partial_json\":\"aaaaaaaaa\"}}");
        feed(p, &out, &done, "content_block_delta",
             "{\"type\":\"content_block_delta\",\"index\":0,"
             "\"delta\":{\"type\":\"input_json_delta\","
             "\"partial_json\":\"bbbbbbbbb\"}}");
        feed(p, &out, &done, "content_block_delta",
             "{\"type\":\"content_block_delta\",\"index\":0,"
             "\"delta\":{\"type\":\"input_json_delta\","
             "\"partial_json\":\"ccccccccc\"}}");
        s = (struct prov_state *)p->state;
        JC_CHECK(s->calls[0].used == 1);
        /* Pre-fix: 27, so this is the red line for the anthropic stream. */
        JC_CHECK(s->calls[0].args.len <= 16);
        JC_CHECK(s->truncated_reply == 1);
        /* message_stop flushes into `out` and sets *done. */
        feed(p, &out, &done, "message_stop", "{\"type\":\"message_stop\"}");
        JC_CHECK(done == 1);
        JC_CHECK(out.truncated == 1);
        /* M1090 (b): the cause is jichi's own cap, not the model's limit. */
        JC_CHECK(out.truncated_by_cap == 1);
        JC_CHECK(out.tool_calls.len == 1);
        if (out.tool_calls.len == 1) {
            struct jc_tool_call *tc =
                (struct jc_tool_call *)jc_vec_at(&out.tool_calls, 0);
            JC_CHECK(tc->arguments_json != NULL &&
                     strlen(tc->arguments_json) <= 16);
        }
        free_msg_for_test(&out);
        p->vt->free(p);
    }

    /* ---- ARGS: route 4, anthropic FULL (vt->parse_full) ------------------ */
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        jc_status st;
        /* input {"x":"aaaaa","y":"bbbbb"} serialises to 27 bytes (cap 16). */
        const char *body =
            "{\"type\":\"message\",\"role\":\"assistant\",\"content\":["
            "{\"type\":\"tool_use\",\"id\":\"tu_1\",\"name\":\"write_file\","
            "\"input\":{\"x\":\"aaaaa\",\"y\":\"bbbbb\"}}]}";

        make_model(&m, "anthropic", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            jc_prov_reset_max_args();
            return;
        }
        prep_out_for_test(&out);
        s = (struct prov_state *)p->state;
        st = p->vt->parse_full(p, body, &out);
        JC_CHECK(st == JC_OK);
        JC_CHECK(s->calls[0].used == 1);
        /* Pre-fix: 27, so this is the red line for the anthropic full route. */
        JC_CHECK(s->calls[0].args.len <= 16);
        JC_CHECK(s->truncated_reply == 1);
        JC_CHECK(out.truncated == 1);
        /* M1090 (b): the cause is jichi's own cap, not the model's limit. */
        JC_CHECK(out.truncated_by_cap == 1);
        JC_CHECK(out.tool_calls.len == 1);
        if (out.tool_calls.len == 1) {
            struct jc_tool_call *tc =
                (struct jc_tool_call *)jc_vec_at(&out.tool_calls, 0);
            JC_CHECK(tc->arguments_json != NULL &&
                     strlen(tc->arguments_json) <= 16);
        }
        free_msg_for_test(&out);
        p->vt->free(p);
    }

    jc_prov_reset_max_args();

    /* ---- SANITY: default caps, a short stream is NOT truncated ---------- */
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;
        int i;

        make_model(&m, "openai", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            /* restore for a clean exit even on this unexpected failure */
            jc_prov_reset_max_text();
            jc_prov_reset_max_args();
            return;
        }
        memset(&out, 0, sizeof(out));
        out.role = JC_ROLE_ASSISTANT;
        jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
        p->vt->stream_reset(p);
        for (i = 0; i < 3; i++) {
            feed(p, &out, &done, "",
                 "{\"choices\":[{\"delta\":{\"content\":\"abc\"}}]}");
        }
        s = (struct prov_state *)p->state;
        JC_CHECK(s->truncated_reply == 0);
        memset(&out, 0, sizeof(out));
        out.role = JC_ROLE_ASSISTANT;
        jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
        jc_prov_flush(s, &out);
        JC_CHECK(out.truncated == 0);   /* normal streaming is not cut */
        JC_CHECK(out.truncated_by_cap == 0);  /* no cap cause either */
        {
            jc_size j;
            free(out.content);
            for (j = 0; j < out.tool_calls.len; j++) {
                struct jc_tool_call *tc =
                    (struct jc_tool_call *)jc_vec_at(&out.tool_calls, j);
                free(tc->id); free(tc->name); free(tc->arguments_json);
            }
            jc_vec_free(&out.tool_calls);
        }
        p->vt->free(p);
    }

    /* ---- PRECEDENCE: BOTH causes at once -> the model's limit wins ------- */
    /* M1090 (b): when the server reports finish_reason "length" AND jichi's
     * per-stream cap also clips the same stream, out.truncated is set by both,
     * but the model-facing prose must name the MODEL's output-token limit (the
     * cause the model can act on), not jichi's cap. jc_prov_flush expresses
     * that as: truncated == 1, truncated_by_cap == 0 (the cap marker is
     * suppressed because hit_length_cap is also set). */
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;
        int i;

        jc_prov_set_max_text(16);
        make_model(&m, "openai", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            jc_prov_set_max_text(oldt);
            jc_prov_set_max_args(olda);
            return;
        }
        prep_out_for_test(&out);
        p->vt->stream_reset(p);
        /* Text delta that will be clipped by the 16-byte cap, then a finish
         * event whose reason is "length" (the model's own limit). */
        for (i = 0; i < 3; i++) {
            feed(p, &out, &done, "",
                 "{\"choices\":[{\"delta\":{\"content\":\"aaaaaaaa\"}}]}");
        }
        feed(p, &out, &done, "",
             "{\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}");
        s = (struct prov_state *)p->state;
        JC_CHECK(s->truncated_reply == 1);    /* the cap clipped */
        JC_CHECK(s->hit_length_cap == 1);     /* the server said length */
        prep_out_for_test(&out);
        jc_prov_flush(s, &out);
        JC_CHECK(out.truncated == 1);         /* the message is cut */
        JC_CHECK(out.truncated_by_cap == 0);  /* ...but by the MODEL's limit */
        free_msg_for_test(&out);
        p->vt->free(p);
        jc_prov_reset_max_text();
    }

    /* belt: make sure we left the globals at the defaults we found them */
    jc_prov_set_max_text(oldt);
    jc_prov_set_max_args(olda);
}

/* M1094 (G3): an SSE event whose data is REFUSED by the cJSON per-parse node
 * cap must not vanish silently -- the streamed reply is marked truncated and
 * the cause is jichi's own cap. Both dialects: a content delta that needs 6
 * nodes, the cap lowered to 5 (measured: the events parse at cap 6, refuse at
 * 5), fed through the existing feed() helper; [DONE] (openai) / message_stop
 * (anthropic) flushes into a fresh out. RED before the fix: the stub
 * cJSON_jc_parse_refused_by_cap returns 0, so s->truncated_reply stays 0 and
 * the flush leaves out.truncated == 0 and out.truncated_by_cap == 0 (three red
 * lines per dialect). The NEGATIVE leg (a malformed-JSON content event under
 * the DEFAULT cap) is NOT a node-cap refusal: invalid JSON is not truncation,
 * and it must stay GREEN both before and after the fix (it guards the M909
 * error-event path from being misfiled as truncation).
 *
 * The non-streamed routes (oa_parse_full / an_parse_full) are deliberately not
 * tested here: a node-cap refusal there returns JC_ERR_PARSE and the call
 * fails loudly (M1094 design note). */
static void test_provider_sse_node_cap_refusal(void)
{
    size_t oldcap = cJSON_jc_get_max_nodes();
    char model_id[] = "m";

    /* ---- openai: 6-node content delta refused under cap 5 --------------- */
    cJSON_jc_set_max_nodes(5);
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;

        make_model(&m, "openai", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            cJSON_jc_set_max_nodes(oldcap);
            return;
        }
        memset(&out, 0, sizeof(out));
        out.role = JC_ROLE_ASSISTANT;
        jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
        p->vt->stream_reset(p);
        feed(p, &out, &done, "",
             "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"HELLO\"}}]}");
        s = (struct prov_state *)p->state;
        /* RED before the fix: the stub keeps this 0. */
        JC_CHECK(s->truncated_reply == 1);
        /* [DONE] does not parse (the cap no longer matters for it) and its
         * flush lands in THIS out -- so do not reset `out` before checking. */
        feed(p, &out, &done, "", "[DONE]");
        /* RED before the fix: flush never carries the cap refusal. */
        JC_CHECK(out.truncated == 1);
        /* RED before the fix: and the cause is jichi's own cap. */
        JC_CHECK(out.truncated_by_cap == 1);
        free_msg_for_test(&out);
        p->vt->free(p);
    }
    cJSON_jc_set_max_nodes(oldcap);

    /* ---- anthropic: 6-node text_delta refused under cap 5 --------------- */
    cJSON_jc_set_max_nodes(5);
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;

        make_model(&m, "anthropic", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            cJSON_jc_set_max_nodes(oldcap);
            return;
        }
        prep_out_for_test(&out);
        p->vt->stream_reset(p);
        feed(p, &out, &done, "message_start", "{\"type\":\"message_start\"}");
        feed(p, &out, &done, "content_block_delta",
             "{\"type\":\"content_block_delta\",\"index\":0,"
             "\"delta\":{\"type\":\"text_delta\",\"text\":\"HELLO\"}}");
        s = (struct prov_state *)p->state;
        /* RED before the fix: the stub keeps this 0. */
        JC_CHECK(s->truncated_reply == 1);
        /* message_stop is small (parses at cap 5) and flushes + sets done. */
        feed(p, &out, &done, "message_stop", "{\"type\":\"message_stop\"}");
        JC_CHECK(done == 1);
        /* RED before the fix: flush never carries the cap refusal. */
        JC_CHECK(out.truncated == 1);
        /* RED before the fix: and the cause is jichi's own cap. */
        JC_CHECK(out.truncated_by_cap == 1);
        free_msg_for_test(&out);
        p->vt->free(p);
    }
    cJSON_jc_set_max_nodes(oldcap);

    /* ---- NEGATIVE: malformed JSON under the DEFAULT cap is not a cap
     *      refusal, and must not mark the reply truncated ------------------ */
    {
        struct jc_model_cfg m;
        struct jc_provider *p;
        struct jc_message out;
        struct prov_state *s;
        int done = 0;

        make_model(&m, "openai", model_id);
        p = jc_provider_create(&m);
        if (!JC_REQUIRE(p != NULL)) {
            return;
        }
        memset(&out, 0, sizeof(out));
        out.role = JC_ROLE_ASSISTANT;
        jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
        p->vt->stream_reset(p);
        feed(p, &out, &done, "", "{\"choices\":");
        s = (struct prov_state *)p->state;
        JC_CHECK(s->truncated_reply == 0);
        p->vt->free(p);
    }

    /* belt: the cap is back at the default we found it at */
    JC_CHECK(cJSON_jc_get_max_nodes() == oldcap);
}

void test_provider(void)
{
    test_no_house_dialect();
    test_provider_stream_caps();   /* M1090 (A-07) */
    test_provider_sse_node_cap_refusal();   /* M1094 (G3) */
    test_length_cap_marks_truncated();
    test_usage_at_ceiling_marks_truncated();   /* M858 */
    test_placeholder_not_serialised();
    test_openai();
    test_openai_reasoning_only();
    test_stream_started();
    test_anthropic();
    test_anthropic_unparsed_args();
    test_openai_unparsed_args_wire();
    test_cache_usage();
    test_anthropic_cache();
    test_openai_cache_key();
    test_prefix_stable();
    test_prefix_stable_across_turns();
    test_elided_args_marker_roundtrip();
    test_stream_error();
}
