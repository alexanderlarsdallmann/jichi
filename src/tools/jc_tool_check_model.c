/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_check_model.c - the check_model tool (M1198, G3).
 *
 * The sanctioned live check the model can call from inside a run. In a clean
 * environment the trial model cannot verify that its configured model works
 * and goes looking for the key instead; check_model removes the motive. It
 * makes ONE real request -- the `doctor --live` probe: JC_TOOLPROBE_PROMPT
 * with the single jichi_probe_echo tool advertised -- against the run's
 * active model, and reports what it observed.
 *
 * The request is the doctor's own (jc_oneshot_probe, src/main.c:15838): a
 * one-user-message mini history plus the empty assistant placeholder the M166
 * fix serialises, the probe tool array, the doctor's 60-second timeout,
 * &app->abort_flag. The reply is read through the STREAM path
 * (jc_http_stream + the provider's on_event) the way the agent loop reads
 * every other reply. Reason: the probe is an end-to-end self-test of jichi's
 * OWN request construction, and a live tool-calling endpoint (and the smoke
 * mockmodel) answers a tool-calling request as an SSE stream; the
 * non-streaming request parses that same reply as no answer (measured:
 * jc_oneshot_probe reads "parse error, HTTP 200" for every arm that is not an
 * HTTP 4xx, which doctor --live reports even today against mockmodel). The
 * request bytes are the same either way; only the reply's framing differs.
 *
 * D-G3 (the operator's decision, SV:0911): the result carries the verdict,
 * its timings, the class, and the HTTP status number; it carries NO text the
 * endpoint or the model wrote -- not the reply, not an excerpt, not the
 * server's error body. Everything the tool prints is jichi-composed; the
 * reply is only CLASSIFIED (native / text / none / unknown) and then freed,
 * and the HTTP-error bytes are never retained at all. The check runs
 * in-process, in the parent, on a provider that already holds the key; the
 * model never runs the request, so the key never reaches its shell.
 *
 * Read-only, full profile only (a network tool, like fetch_url: NOT in the
 * core-exempt list, so a core-profile run makes no network call).
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_provider.h"
#include "jc_message.h"
#include "jc_sse.h"
#include "jc_http.h"
#include "jc_net.h"
#include "jc_toolprobe.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>


static cJSON *check_model_schema(void)
{
    return tu_schema_begin();
}

/* The neutral one-tool array the M167 live probe advertises -- a byte-for-byte
 * mirror of the doctor's static probe_tool_array (src/main.c:13857). It lives
 * there as a static, so the tool duplicates it: one tool, the probe name, a
 * text parameter. Built in the tool (not the pure jc_toolprobe core) for the
 * same reason the doctor builds it in main.c: the core stays cJSON-free. */
static cJSON *probe_tool_array(void)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON *props = cJSON_CreateObject();
    cJSON *text = cJSON_CreateObject();
    cJSON *req = cJSON_CreateArray();
    cJSON_AddStringToObject(t, "name", JC_TOOLPROBE_TOOL);
    cJSON_AddStringToObject(t, "description",
                            "Echo back the given text. Used to verify tool "
                            "calling works.");
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddStringToObject(text, "type", "string");
    cJSON_AddStringToObject(text, "description", "The text to echo.");
    cJSON_AddItemToObject(props, "text", text);
    cJSON_AddItemToObject(params, "properties", props);
    cJSON_AddItemToArray(req, cJSON_CreateString("text"));
    cJSON_AddItemToObject(params, "required", req);
    cJSON_AddItemToObject(t, "parameters", params);
    cJSON_AddItemToArray(arr, t);
    return arr;
}

/* Stream bridge: the minimal copy of the agent loop's (jc_agent.c). The sink
 * is unused (a probe streams no UI text), and no raw response prefix is kept
 * (D-G3: reply bytes must not reach the result, so none are retained). */
struct cm_bridge {
    struct jc_provider   *prov;
    struct jc_message    *out;
    struct jc_stream_sink sink;
    struct jc_sse_parser  sse;
    int                   done;
    volatile int         *abort_flag;
};

static void cm_bridge_event(const struct jc_sse_event *ev, void *user)
{
    struct cm_bridge *b = (struct cm_bridge *)user;
    b->prov->vt->on_event(b->prov, ev, b->out, &b->sink, &b->done);
}

static int cm_bridge_chunk(const char *buf, jc_size n, void *user)
{
    struct cm_bridge *b = (struct cm_bridge *)user;
    jc_sse_feed(&b->sse, buf, n);
    if (b->abort_flag != NULL && *b->abort_flag) {
        return 1; /* abort the transfer */
    }
    return 0;
}

/* One live probe request, streamed. `reply` is the assistant message the
 * stream is read into (it must outlive the call). Returns 0 when the HTTP
 * exchange completed and the reply was read (whatever it said), 1 when the
 * request did not complete (`http_status` then carries the server's number
 * when the server said one). */
static int cm_probe_request(struct jc_provider *pp, struct jc_message *reply,
                            long *http_status, volatile int *abort_flag)
{
    struct jc_history mini;
    struct cm_bridge bridge;
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *body = NULL;
    cJSON *tools = NULL;
    jc_status st;
    int incomplete = 1;

    /* A one-user-message mini history plus the empty assistant placeholder
     * the agent loop serialises (M166): a probe that omits it would build a
     * request no real turn ever builds, and would not test the thing it
     * exists to test. */
    jc_history_init(&mini);
    jc_history_add(&mini, JC_ROLE_USER, JC_TOOLPROBE_PROMPT);
    jc_history_add(&mini, JC_ROLE_ASSISTANT, NULL);
    tools = probe_tool_array();
    st = pp->vt->build_request(pp, &mini, NULL, tools, 1, &body);
    if (tools != NULL) {
        cJSON_Delete(tools);
    }
    if (st != JC_OK || body == NULL) {
        jc_history_free(&mini);
        free(body);
        return 1;
    }
    jc_http_headers_init(&headers);
    pp->vt->add_headers(pp, &headers);
    memset(&bridge, 0, sizeof(bridge));
    bridge.prov = pp;
    bridge.out = reply;
    bridge.abort_flag = abort_flag;
    bridge.done = 0;
    jc_sse_init(&bridge.sse, cm_bridge_event, &bridge);
    pp->vt->stream_reset(pp);

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = pp->vt->endpoint(pp);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = 60; /* the doctor's own probe timeout */
    req.abort_flag = abort_flag;
    jc_net_side_fences(&req, NULL); /* only the provider in scope: built-in */
    req.stream_body = 1; /* ownership of body transfers to jc_http_stream */

    st = jc_http_stream(&req, http_status, cm_bridge_chunk, &bridge);
    jc_sse_free(&bridge.sse);
    jc_http_headers_free(&headers);

    /* M521: the stream ended cleanly but the provider never signalled the
     * terminal event -- finish whatever accumulated. */
    if (st == JC_OK && !bridge.done && pp->vt->stream_end != NULL) {
        pp->vt->stream_end(pp, reply);
    }
    incomplete = (st == JC_OK && *http_status < 400) ? 0 : 1;
    jc_history_free(&mini);
    return incomplete;
}

static jc_status check_model_run(const cJSON *args,
                                 struct jc_tool_result *out,
                                 struct jc_app *app)
{
    struct jc_model_cfg *m;
    struct jc_provider *pp;

    (void)args; /* no parameters */
    m = jc_config_model_at(&app->config, app->config.active);
    if (m == NULL || jc_config_model_count(&app->config) <= 0) {
        tu_err(out, "error: no active model configured -- check_model has "
                    "nothing to verify");
        return JC_OK;
    }
    /* A probe-only provider, freed after the probe -- the doctor --live
     * ownership, mirrored exactly. */
    pp = jc_provider_create(m);
    if (pp == NULL) {
        tu_err(out, "error: could not create a provider for the active model "
                    "-- check the provider, apiBase and key configuration");
        return JC_OK;
    }
    {
        struct jc_history mini;
        struct jc_message *reply;
        long http_status = 0;
        double t0;
        int incomplete;
        char msg[512];
        char http_part[32];

        jc_history_init(&mini);
        reply = jc_history_add(&mini, JC_ROLE_ASSISTANT, NULL);
        t0 = jc_now_millis();
        incomplete = cm_probe_request(pp, reply, &http_status,
                                      &app->abort_flag);
        {
            long ms = (long)(jc_now_millis() - t0);
            if (incomplete) {
                /* D-G3: the HTTP status NUMBER when the endpoint said one
                 * (the doctor's http_part shape), and NO other server text --
                 * the body bytes are never retained. */
                http_part[0] = '\0';
                if (http_status > 0) {
                    jc_snprintf(http_part, sizeof(http_part),
                                " -- HTTP status %ld", http_status);
                }
                jc_snprintf(msg, sizeof(msg),
                            "fail: the probe request did not complete%s -- "
                            "%ld ms", http_part, ms);
            } else {
                /* The reply is classified, then freed with its history: its
                 * words never enter the result (D-G3). */
                enum jc_toolprobe_verdict v;
                int ncalls;
                char call_name[256];
                double in_tok = 0.0, out_tok = 0.0;

                ncalls = (int)jc_msg_tool_call_count(reply);
                call_name[0] = '\0';
                if (ncalls > 0) {
                    const struct jc_tool_call *tc =
                        jc_msg_tool_call_at(reply, 0);
                    if (tc != NULL && tc->name != NULL &&
                        tc->name[0] != '\0') {
                        jc_snprintf(call_name, sizeof(call_name), "%s",
                                     tc->name);
                    }
                }
                if (pp->vt->get_usage != NULL) {
                    pp->vt->get_usage(pp, &in_tok, &out_tok);
                }
                v = jc_toolprobe_classify(ncalls, call_name,
                                          (reply->content != NULL)
                                              ? reply->content
                                              : "");
                if (v == JC_TOOLPROBE_NATIVE) {
                    if (in_tok > 0.0) {
                        jc_snprintf(msg, sizeof(msg),
                                    "pass: tool calling observed \"%s\" -- "
                                    "the model called %s natively; probe "
                                    "took %ld ms, server counted %.0f prompt "
                                    "tokens",
                                    jc_toolprobe_verdict_str(v),
                                    JC_TOOLPROBE_TOOL, ms, in_tok);
                    } else {
                        jc_snprintf(msg, sizeof(msg),
                                    "pass: tool calling observed \"%s\" -- "
                                    "the model called %s natively; probe "
                                    "took %ld ms",
                                    jc_toolprobe_verdict_str(v),
                                    JC_TOOLPROBE_TOOL, ms);
                    }
                } else {
                    jc_snprintf(msg, sizeof(msg),
                                "fail: tool calling observed \"%s\" -- %ld ms",
                                jc_toolprobe_verdict_str(v), ms);
                }
            }
            tu_ok_copy(out, msg);
        }
        jc_history_free(&mini);
        pp->vt->free(pp);
    }
    return JC_OK;
}

static const struct jc_tool CHECK_MODEL_TOOL = {
    "check_model",
    "Verify that the run's active model works: sends one live tool-calling "
    "probe and reports the verdict (pass/fail), the observed class, its own "
    "timings and, on an incomplete request, the HTTP status number. It never "
    "shows the model's or the server's words. No parameters.",
    check_model_schema,
    1, /* readonly: it modifies nothing local */
    check_model_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631) */
};

const struct jc_tool *jc_tool_check_model(void)
{
    return &CHECK_MODEL_TOOL;
}
