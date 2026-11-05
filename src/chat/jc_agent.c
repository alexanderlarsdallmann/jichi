/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_agent.c - the streaming agent loop (see jc_agent.h). */

#include "jc_agent.h"
#include "jc_scopededit.h"  /* D6 (M975): the scoped edit check */
#include "jc_utf8.h"   /* M911: hand-back head cut on a char boundary */
#include "jc_outcome.h"
#include "jc_delegreport.h"
#include "jc_parallel.h"   /* M861: the task budget's note */
#include "jc_toolout.h"
#include "jc_meminfo.h"
#include "jc_memtrim.h"
#include "jc_provider.h"
#include "jc_tool.h"
#include "jc_landlock.h"   /* M852: the fence hint */
#include "jc_jsonrepair.h"
#include "jc_mcp.h"
#include "jc_perm.h"
#include "jc_sysmsg.h"
#include "jc_compact.h"
#include "jc_snapshot.h"
#include "jc_envelope.h"
#include "jc_eventlog.h"
#include "jc_hooks.h"
#include "jc_bg.h"
#include "jc_control.h"
#include "jc_cli.h"
#include "jc_testparse.h"
#include "jc_agentjson.h" /* M98: jc_agent_tool_category for the read budget */
#include "jc_selfheal.h"
#include "jc_toolloop.h"
#include "jc_relay.h"    /* M920: the relay's decision core */
#include "jc_msg.h"   /* M570: the denied-stop notice */
#include "jc_json.h"
#include "jc_priv.h"
#include "jc_session.h"   /* M1083 (D-08): the mid-turn persist helper */
#include "jc_argpath.h"
#include "jc_kinetic.h"
#include "jc_audit.h"
#include "jc_snprintf.h"
#include "jc_http.h"
#include "jc_sse.h"
#include "jc_log.h"

/* M501: how many declared paths one tool call may have and still be considered
 * for the explicit-scope exemption. `apply_patch` is the only tool that can
 * exceed one, and a change touching more than 32 files is not the case this
 * exemption exists for -- an overflow leaves the constraint applied, which is
 * the safe direction. */
#define JC_AGENT_MAX_ARG_PATHS 32

/* M1038: model-count cap for the reachability cache (same as in jc_app.c). */
#define JC_REACH_MAX 64

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>   /* M1083 (D-08): offsetof for the mid-turn persist helper */

/* Bridges libcurl byte chunks -> SSE parser -> provider event handler. */
struct stream_bridge {
    struct jc_provider   *prov;
    struct jc_message    *out;
    struct jc_stream_sink sink;
    struct jc_sse_parser  sse;
    int                   done;
    volatile int         *abort_flag;
    /* Bounded prefix of the raw response bytes, kept so an HTTP-error body
     * (e.g. a provider's JSON `{"error":{"message":...}}`) can be surfaced
     * instead of just the status code (M85). */
    char                  raw[2048];
    jc_size               raw_len;
    /* M897: jc_now_millis() at the first SSE event after which the provider
     * reports output (text, reasoning or a tool call); 0 until then. */
    double                first_ms;
};

static void bridge_on_text(void *user, const char *delta, jc_size n)
{
    struct jc_agent_callbacks *cb = (struct jc_agent_callbacks *)user;
    if (cb != NULL && cb->on_assistant_text != NULL) {
        cb->on_assistant_text(cb->user, delta, n);
    }
}

/* Liveness tick from libcurl's progress callback (M66): forward to the agent
 * callback's on_progress so a front-end can animate a spinner during the wait. */
static void bridge_on_progress(void *user)
{
    const struct jc_agent_callbacks *cb =
        (const struct jc_agent_callbacks *)user;
    if (cb != NULL && cb->on_progress != NULL) {
        cb->on_progress(cb->user);
    }
}

static void bridge_sse_event(const struct jc_sse_event *ev, void *user)
{
    struct stream_bridge *b = (struct stream_bridge *)user;
    b->prov->vt->on_event(b->prov, ev, b->out, &b->sink, &b->done);
    if (b->first_ms == 0.0 && b->prov->vt->stream_started != NULL &&
        b->prov->vt->stream_started(b->prov)) {
        b->first_ms = jc_now_millis();
    }
}

static int bridge_chunk(const char *buf, jc_size n, void *user)
{
    struct stream_bridge *b = (struct stream_bridge *)user;
    /* Keep a bounded prefix of the raw bytes for error diagnostics (M85). */
    if (b->raw_len + 1 < sizeof(b->raw)) {
        jc_size room = sizeof(b->raw) - 1 - b->raw_len;
        jc_size take = n < room ? n : room;
        memcpy(b->raw + b->raw_len, buf, take);
        b->raw_len += take;
        b->raw[b->raw_len] = '\0';
    }
    jc_sse_feed(&b->sse, buf, n);
    if (b->abort_flag != NULL && *b->abort_flag) {
        return 1; /* abort the transfer */
    }
    return 0;
}

/* True for errors worth retrying: transport failures and 429 / 5xx. */
static int is_transient(jc_status st, long http_status)
{
    if (st == JC_ERR_HTTP || st == JC_ERR_TIMEOUT) {
        return 1;
    }
    if (http_status == 429) {
        return 1;
    }
    if (http_status >= 500 && http_status < 600) {
        return 1;
    }
    /* M1125: a provider error that arrived as an in-stream engine error
     * (M909) is transient. The reclassification at :377 leaves http_status at
     * 200, so the arm is scoped to http_status == 200 exactly: the ONLY other
     * path to JC_ERR_PROVIDER is a non-streamed st == JC_OK &&
     * http_status >= 400 reply, whose status is >= 400, never 200, and is
     * therefore excluded. The duplication guard (emitted) still stops a retry
     * when real answer bytes were shown.
     * M1142 (SV:0419) CORRECTION: the M1125 claim that the over-context case is
     * excluded here was FALSE for the in-stream shape -- an in-stream
     * over-context reply (the LM Studio fixture) reclassifies exactly like any
     * other M909 error, so it hits this arm with http_status == 200 and WAS
     * retried. The exclusion is now the ctx_overflow flag at the retry gate:
     * !app->ctx_overflow. (The non-streamed over-context reply is an HTTP 400,
     * and 400 is not one of this function's arms -- 429 and 5xx are, and are
     * meant to be retried -- so the 400 never reaches the gate as transient,
     * and the in-stream shape is the one the ctx_overflow flag excludes.) */
    if (st == JC_ERR_PROVIDER && http_status == 200) {
        return 1;
    }
    return 0;
}

/* M536: ONE expression for "did this tool call fail", stamped into both sinks.
 *
 * M420 gave telemetry and the run journal a shared `run` key precisely so a
 * supervisor could join them (docs/proposals/2026-08-observability-seams.md S1).
 * What it did not reconcile is how each sink NAMES the outcome: telemetry wrote
 * `ok` (true = fine) and the journal wrote `error` (true = broken), from the same
 * `res.is_error`, 200 lines apart in one function. Joining on `run` and then
 * filtering `.ok` over journal rows yields undefined -> falsy -> EVERY row reads
 * as a failure; filtering `.error` over telemetry rows yields undefined -> falsy
 * -> EVERY row reads as a success. Both directions are silently wrong and both
 * look right, which is the worst property an audit trail can have.
 *
 * Both names now ride on both rows, and because they are written here from one
 * argument they cannot drift apart -- two fields derived from one predicate in
 * two places is the very shape M532/M535 were about. Additive, so no existing
 * reader breaks; docs/EMBEDDING.md states the join.
 *
 * `exit` carries M168's distinction: a red test gate is the agent working, not a
 * malfunction (199 of 294 apparent tool failures in real dogfood data were red
 * gates). Telemetry has had that column since M168 and the JOURNAL -- the audit
 * trail, the sink an operator reads to judge a run -- did not, so every correctly
 * failing test was recorded there as indistinguishable from a broken tool.
 * Pass -1 when the tool ran no command. */
static void stamp_outcome(cJSON *o, int is_error, int exit_status)
{
    if (o == NULL) {
        return;
    }
    cJSON_AddBoolToObject(o, "ok", is_error ? 0 : 1);
    cJSON_AddBoolToObject(o, "error", is_error ? 1 : 0);
    if (exit_status >= 0) {
        cJSON_AddNumberToObject(o, "exit", (double)exit_status);
    }
}

/* A local alias for jc_app_telem_begin(), kept because seventeen call sites in
 * this file read better as telem(app, "x") -- and because at M583 the BODY moved
 * out (to jc_app.c) while the name stayed. The body had to move: nine emitters in
 * four other files were reaching jc_eventlog_begin() directly and carried none of
 * depth/turn/run, so M420's join was partial by exactly those events. A static
 * helper cannot be shared, and that is the whole defect -- see jc_app.h. */
/* M855: does this shell call's `command` argument run `git stash`? */
static int args_runs_git_stash(const char *args_json)
{
    cJSON *aj = jc_json_parse(args_json);
    int r = 0;
    if (aj != NULL) {
        r = jc_cmd_runs_git_stash(jc_json_get_str(aj, "command", NULL));
        cJSON_Delete(aj);
    }
    return r;
}

/* M859 (N42 (d), M965: the default for a headless top-level run when nothing is
 * set): may this turn be asked once to continue a reply cut at the output ceiling?
 * A delegate, unless continueCutReply is "off": nobody else can say "continue" to
 * it. The top level only with "headless" or the new default (a supervised headless
 * lead gets the continuation by default since M965; set "delegates" to restore the
 * old default, "off" to disable), and only in a headless run -- in chat a person
 * can, and the editor is the person in ACP. */
static int cut_continue_allowed(const struct jc_app *app)
{
    if (app->config.continue_cut_reply == JC_CUTCONT_OFF) {
        return 0;
    }
    /* M965: when nothing was explicitly set, the default becomes headless
     * semantics for a headless top-level run. Delegates are unaffected. */
    if (!app->config.has_continue_cut_reply && app->headless_run &&
        app->agent_depth == 0) {
        return 1;
    }
    if (app->agent_depth >= 1) {
        return 1;
    }
    return app->config.continue_cut_reply == JC_CUTCONT_HEADLESS &&
           app->headless_run;
}

static cJSON *telem(struct jc_app *app, const char *event)
{
    return jc_app_telem_begin(app, event);
}

/* M951: the median-growth sorter (a long is always a valid qsort key). */
static int relay_diff_cmp(const void *a, const void *b)
{
    long x = *(const long *)a;
    long y = *(const long *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* Perform one streaming request, retrying transient failures with exponential
 * backoff. Populates `assistant` with the streamed text + tool calls and
 * reports token usage via the callback.
 *
 * `sys_est`/`tools_est` (M286) are the byte-estimates of this request's system
 * prompt and tool schemas, measured by the caller (which already holds both) and
 * passed in rather than assumed. They are the non-history part of the very
 * request being sent, so the calibration basis below and the M192 attribution
 * fields describe the same bytes the provider is about to tokenize. */
static jc_status stream_once(struct jc_app *app, struct jc_history *hist,
                             const char *system_msg, cJSON *tools,
                             struct jc_message *assistant,
                             const struct jc_agent_callbacks *cb,
                             struct jc_provider *prov,
                             long sys_est, long tools_est)
{
    struct jc_http_headers headers;
    jc_status st = JC_OK;
    int attempt;
    long backoff_ms = 500;
    long fail_t0 = 0;     /* M876: the first failed attempt's wall clock */
    char redacted[2048];  /* M1040: redaction buffer for bridge.raw */

    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);

    for (attempt = 0; ; attempt++) {
        char *body = NULL;
        double req_bytes = 0.0;
        struct jc_http_request req;
        struct stream_bridge bridge;
        char transport_err[256];
        char stream_err[512];             /* M909: an error inside the stream */
        long http_status = 0;
        double t0 = 0.0;
        double latency = 0.0;

        /* Build the request fresh each attempt. The body is uploaded via a
         * read-callback and freed the moment it is fully sent (req.stream_body),
         * so it isn't held while the response streams back -- which means it
         * cannot be reused across retries, hence the rebuild here. */
        st = prov->vt->build_request(prov, hist, system_msg, tools, 1, &body);
        if (st != JC_OK) {
            break;
        }
        if (attempt == 0) {
            jc_logf(JC_LOG_DEBUG, "request: %s", body);
        }

        prov->vt->stream_reset(prov);

        bridge.prov = prov;
        bridge.out = assistant;
        bridge.sink.on_text = bridge_on_text;
        bridge.sink.user = (void *)cb;
        bridge.done = 0;
        bridge.abort_flag = &app->abort_flag;
        bridge.raw_len = 0;
        bridge.raw[0] = '\0';
        bridge.first_ms = 0.0;
        jc_sse_init(&bridge.sse, bridge_sse_event, &bridge);

        memset(&req, 0, sizeof(req));

        transport_err[0] = '\0';
        stream_err[0] = '\0';

        req.err_out = transport_err;

        req.err_out_cap = sizeof(transport_err);
        req.method = "POST";
        req.url = prov->vt->endpoint(prov);
        req.headers = &headers;
        req.body = body;
        req.body_len = strlen(body);
        /* M326v: keep the size after ownership transfers. `in_tok` comes from
         * the response, so a call that never got one records 0 -- which made
         * the cost of a retried attempt invisible in telemetry and defeated an
         * attempt to correlate failures with request size. Bytes, not tokens:
         * this is the one figure we know exactly at the moment of sending. */
        req_bytes = (double)req.body_len;
        /* M341: dump BEFORE ownership transfers -- one line later the pointer
         * belongs to jc_http and may be freed mid-upload. This is the exact
         * string on the wire, which is what --log-level full is NOT. */
        if (app->dump_requests != NULL) {
            (void)jc_toolout_dump_request(app->dump_requests, req.url,
                                          body, req.body_len);
        }
        req.stream_body = 1; /* transfers ownership of body to jc_http */
        /* M22: bound the call so a stalled/unreachable model can't hang the
         * agent forever. Fail fast on connect; abort a frozen stream after a
         * stall window; optionally cap the whole request. Values resolve by
         * precedence CLI > per-model > global > built-in (M22b). A timeout
         * returns JC_ERR_HTTP, which the retry/backoff path below treats as
         * transient. */
        {
            long ct = 0, st = 0, rt = 0;
            jc_config_resolve_timeouts(&app->config, &app->config.model,
                                       &ct, &st, &rt);
            req.connect_timeout_secs = ct;
            req.stall_timeout_secs = st;
            req.timeout_secs = rt; /* hard cap; 0 => none */
            /* M-L2: the dead-stream fence -- ON even when stall is 0 (a
             * measurement run), because it can only fire when nothing arrives. */
            req.dead_stream_secs = app->config.dead_stream_secs;
        }
        req.abort_flag = &app->abort_flag;
        if (cb != NULL && cb->on_progress != NULL) {
            req.on_progress = bridge_on_progress;
            req.progress_user = (void *)cb;
        }

        t0 = jc_now_millis();
        st = jc_http_stream(&req, &http_status, bridge_chunk, &bridge);
        latency = jc_now_millis() - t0;
        /* body is now owned and freed by jc_http_stream; do not touch it. */
        jc_sse_free(&bridge.sse);

        /* M1042: for aborted transfers, set fail_class via the classifier.
         * The third branch of jc_http_fail_class returns "aborted", reached
         * at runtime. */
        if (st == JC_ERR_ABORTED && req.fail_class == NULL) {
            req.fail_class = jc_http_fail_class(0, 0, 1, 0, 0, 0, 0);
        }

        /* M909: the server reported an error INSIDE a stream that began with
         * HTTP 200 (LM Studio over its loaded context, Anthropic overloaded).
         * The same request unstreamed is an HTTP 4xx/5xx, so it fails here the
         * same way: no answer, no retry, the message logged and kept for the
         * run's report. Before, the message became the answer and the call was
         * recorded as a success with 0 tokens. */
        stream_err[0] = '\0';
        if (st == JC_OK && http_status < 400 && prov->vt->stream_error != NULL) {
            const char *se = prov->vt->stream_error(prov);
            if (se != NULL) {
                jc_snprintf(stream_err, sizeof stream_err, "%s", se);
                jc_logf(JC_LOG_ERROR, "provider reported an error inside the "
                        "stream (HTTP %ld): %s", http_status, stream_err);
                /* M1142 (SV:0419): mark overflow when EITHER classifier matches.
                 * M854, as for an HTTP 400 -- and the jc_cli classifier too,
                 * because the in-stream message can carry LM Studio's wording
                 * ("exceeds the available context size", "n_ctx",
                 * "exceed_context_size"), which only the jc_cli classifier knows.
                 * Without it the retry gate (M1125) re-sent this deterministic
                 * error maxRetries times. */
                if (jc_http_body_ctx_overflow(stream_err,
                                              (jc_size)strlen(stream_err)) ||
                    jc_text_is_context_overflow(stream_err)) {
                    app->ctx_overflow = 1;
                }
                st = JC_ERR_PROVIDER;
            }
        }
        /* M1042: if a provider error was detected (st != JC_OK after M909 block),
         * mark the failure class via the classifier. For M909 HTTP-200 status
         * that reaches here, the classifier returns "provider-error". */
        if (st != JC_OK && req.fail_class == NULL) {
            req.fail_class = jc_http_fail_class(0, http_status, 0, 0, 1, 0, 0);
        }
        /* The stream OK path (jc_http.c:1230-1236) now covers the 4xx/5xx case
         * via the classifier, so this block is deleted. */

        if (st == JC_OK && http_status < 400) {
            /* Success: report usage and finish. Usage is also metered into the
             * envelope (at every depth, so subagent tokens count toward the
             * run's budget). */
            double in_tok = 0.0, out_tok = 0.0;
            double cache_read = 0.0, cache_write = 0.0;
            /* M876: a call that worked; an earlier failure did not end the run */
            app->last_model_error[0] = '\0';
            app->last_model_attempts = 0;
            /* M521: the stream ended cleanly at the HTTP level but the provider
             * never signalled completion -- no terminal event arrived (a cut
             * connection, a server that omits it, or a final SSE frame missing
             * its terminating blank line, which is never dispatched at all).
             * Everything accumulated so far had been streamed to the user and
             * then dropped: measured on `--output json`, a run that printed
             * HELLO reported `"text": "", "stop_reason": "done"`. The provider
             * finishes the message; the agent does not learn a dialect to do it
             * (the vtable is the only place a dialect lives). */
            if (!bridge.done && prov->vt->stream_end != NULL) {
                prov->vt->stream_end(prov, assistant);
            }
            if (prov->vt->get_usage != NULL) {
                prov->vt->get_usage(prov, &in_tok, &out_tok);
                if (prov->vt->get_cache_usage != NULL) {
                    prov->vt->get_cache_usage(prov, &cache_read, &cache_write);
                }
                /* Record the real prompt size (full input) so mid-turn
                 * compaction can calibrate its byte estimate to reality (M76). */
                app->last_prompt_tokens =
                    (long)(in_tok + cache_read + cache_write);
                /* M494: and the high-water mark. Only updated on a call that
                 * SUCCEEDED, which is what makes it evidence: the server counted
                 * this many input tokens for a request it answered. */
                if (app->last_prompt_tokens > app->max_prompt_tokens) {
                    app->max_prompt_tokens = app->last_prompt_tokens;
                }
                /* M951: the leg's growth series -- the SERVER's prompt count
                 * at each depth-0 model call, relay on. Depth 0 only: a
                 * delegate's calls grow a CHILD's prompt, and the account
                 * call (relay_account_call) reuses this helper AFTER the
                 * handover set the flag, so relay_handover gates it too.
                 * OOM on the push is a leaf: the series is a measurement,
                 * never a gate -- drop the sample, keep the run. */
                if (app->config.relay && app->agent_depth == 0 &&
                    !app->relay_handover) {
                    long sample = app->last_prompt_tokens;
                    if (jc_vec_push(&app->relay_prompt_series, &sample)
                            == JC_OK) {
                        app->relay_model_calls++;
                    }
                }
                /* Learn the model's real tokens/estimate ratio (M77): the
                 * reported prompt size vs our byte estimate of the SAME request
                 * -- system prompt + tool schemas + history, all three measured
                 * (M286).
                 *
                 * This basis used to be `history + 2000`, a flat allowance for
                 * the non-history part chosen to match the compaction trigger's
                 * SYS_TOOLS_OVERHEAD. Measured on 34 MB of dogfood telemetry the
                 * real non-history part was 7421 tokens under the `core` tool
                 * profile and 11167 under `full` -- so the ratio was absorbing a
                 * 5-9k ADDITIVE error into a MULTIPLICATIVE correction, which
                 * makes the learned "per-model constant" a function of how large
                 * the history happened to be: 3.98 for calls under 2k of
                 * history, 1.37 for calls over 30k, a 2.9x spread. On this basis
                 * the same log yields a flat 1.07-1.19 -- an actual property of
                 * the tokenizer, which is what M77 set out to learn. Small
                 * histories (turn starts, subagents) pushed the average up, and
                 * it was then applied to large-history calls where it
                 * over-stated: one model persisted 2.717 while jichi's own
                 * telemetry reader, which always summed all three parts, read
                 * 1.17 from the same events. See docs/COMPACTION.md. */
                jc_calib_observe(&app->calib,
                    app->config.model.model != NULL
                        ? app->config.model.model : app->config.model.name,
                    app->last_prompt_tokens,
                    jc_compact_estimate_tokens(hist) + sys_est + tools_est);
                if (cb != NULL && cb->on_usage != NULL) {
                    cb->on_usage(cb->user, in_tok, out_tok,
                                 cache_read, cache_write);
                }
                if (app->env != NULL) {
                    /* Budget counts the full input processed, cache included
                     * (cache_read for OpenAI keeps the total == prompt_tokens;
                     * cache_write is Anthropic cache-creation input). */
                    jc_env_record_tokens(app->env,
                                         in_tok + cache_read + cache_write,
                                         out_tok);
                }
            }
            {
                cJSON *o = telem(app, "model_call");
                if (o != NULL) {
                    /* M800: name the model actually called. A spawn_parallel
                     * child runs its own model via a private provider without
                     * switching app->config.model, so that mirror still holds
                     * the lead's -- prefer the child's override when set. */
                    const char *tm_name = app->telem_model_name != NULL
                        ? app->telem_model_name
                        : (app->config.model.name != NULL
                            ? app->config.model.name
                            : (app->config.model.model != NULL
                               ? app->config.model.model : ""));
                    const char *tm_id = app->telem_model_id != NULL
                        ? app->telem_model_id : app->config.model.model;
                    cJSON_AddStringToObject(o, "model", tm_name);
                    /* M289: the WIRE id alongside the config name, so the
                     * summarizer can group by the same key calibration.json
                     * uses. Without it a config rename split one model's
                     * history into two reader rows. */
                    if (tm_id != NULL) {
                        cJSON_AddStringToObject(o, "model_id", tm_id);
                    }
                    cJSON_AddNumberToObject(o, "attempt", (double)attempt);
                    cJSON_AddNumberToObject(o, "status", (double)http_status);
                    /* M858 (N42): a cut reply is visible as such, not only as
                     * out_tok equal to the ceiling. Present only when true. */
                    if (assistant != NULL && assistant->truncated) {
                        cJSON_AddBoolToObject(o, "truncated", 1);
                    }
                    cJSON_AddNumberToObject(o, "latency_ms", latency);
                    /* M897 (plan 1.1): the first-token time -- what a visitor
                     * waits before anything appears. Absent when no output
                     * arrived. */
                    if (bridge.first_ms > 0.0) {
                        cJSON_AddNumberToObject(o, "ttft_ms",
                                                bridge.first_ms - t0);
                    }
                    /* M536: both names on every telemetry row, not just the
                     * tool_call ones -- a reader filtering `.error` across the
                     * sink would otherwise silently skip every model call. */
                    stamp_outcome(o, 0, -1);
                    cJSON_AddNumberToObject(o, "in_tok", in_tok);
                    cJSON_AddNumberToObject(o, "out_tok", out_tok);
                    cJSON_AddNumberToObject(o, "cache_read_in", cache_read);
                    cJSON_AddNumberToObject(o, "cache_write_in", cache_write);
                    cJSON_AddNumberToObject(o, "cost_usd",
                        jc_config_cost(&app->config.model, in_tok, out_tok,
                                       cache_read, cache_write));
                    /* M192: attribute the input. 99.63% of a dogfood log's
                     * 273.8M tokens were input, and nothing said whether they
                     * were system prompt, tool schemas, or history -- so the
                     * 43% budget-exhaustion rate had no diagnosable cause.
                     * These use the SAME byte heuristic as the compaction
                     * trigger (jc_compact_estimate_*), so the attribution and
                     * the compaction decisions can never disagree. Against the
                     * real in_tok on this event, their sum also yields the M77
                     * calibration ratio per call.
                     *
                     * M286: sys/tools are now the values the CALLER measured and
                     * handed to jc_calib_observe above, not recomputed here --
                     * which is what makes "the ratio this log reports" and "the
                     * ratio jichi acts on" the same arithmetic. While each
                     * computed its own basis the two disagreed by 2.3x. The
                     * measurement therefore no longer lives inside the telem()
                     * guard: a run without telemetry now pays one tool-array
                     * serialization per model call, which is nothing beside the
                     * request it precedes, and buys a calibration basis that is
                     * measured rather than assumed. */
                    cJSON_AddNumberToObject(o, "sys_tok", (double)sys_est);
                    cJSON_AddNumberToObject(o, "tools_tok", (double)tools_est);
                    cJSON_AddNumberToObject(o, "hist_tok",
                        (double)jc_compact_estimate_tokens(hist));
                    if (jc_eventlog_full(app->telemetry) &&
                        assistant->content != NULL) {
                        jc_eventlog_add_text(o, "response", assistant->content,
                                             JC_EVENTLOG_TEXT_MAX);
                    }
                }
                jc_eventlog_end(app->telemetry, o);
                /* M807: mirror this reply into a spawn_parallel child's own
                 * transcript -- the reasoning record a successor reads (P3 builds
                 * the end record from its tail). Born redacted: jc_eventlog_add_text
                 * scrubs registered secrets. Only ever set in a forked child, so a
                 * lead run writes nothing here. */
                if (app->transcript != NULL) {
                    cJSON *to = jc_eventlog_begin(app->transcript, "model_call");
                    if (to != NULL) {
                        cJSON_AddNumberToObject(to, "in_tok", in_tok);
                        cJSON_AddNumberToObject(to, "out_tok", out_tok);
                        if (assistant->content != NULL) {
                            jc_eventlog_add_text(to, "response",
                                assistant->content, JC_EVENTLOG_TEXT_MAX);
                        }
                    }
                    jc_eventlog_end(app->transcript, to);
                }
            }
            break;
        }

        /* Non-success (error, timeout, or abort): record the call. The `result`
         * field distinguishes a stall ("timeout") from a generic transport
         * error ("error") so operators can tell a frozen model from a network
         * blip (M22c). */
        if (st != JC_ERR_ABORTED && fail_t0 == 0) {
            fail_t0 = (long)time(NULL);   /* M876 */
        }
        /* M1040: redact any registered secret that straddles the capture boundary
         * BEFORE any sink reads bridge.raw. The cut must run once, independent of
         * whether telemetry is on, before error_body telemetry, logf, and rerr.
         * The gate on truncation (bridge.raw_len == sizeof(bridge.raw) - 1) ensures
         * we only act when a registered secret might be cut at the boundary. */
        if (jc_redact_active() && http_status >= 400 && bridge.raw_len > 0 &&
            bridge.raw_len == sizeof(bridge.raw) - 1) {
            jc_size new_len = bridge.raw_len;
            if (jc_redact_capture_tail(bridge.raw, bridge.raw_len,
                                       sizeof(bridge.raw), &new_len) > 0) {
                bridge.raw_len = new_len;
            }
        }
        {
            const char *result = st == JC_ERR_ABORTED ? "aborted"
                               : st == JC_ERR_TIMEOUT ? "timeout" : "error";
            cJSON *o = telem(app, "model_call");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "model",
                    app->config.model.name != NULL ? app->config.model.name
                                                   : "");
                cJSON_AddNumberToObject(o, "attempt", (double)attempt);
                cJSON_AddNumberToObject(o, "status", (double)http_status);
                cJSON_AddNumberToObject(o, "latency_ms", latency);
                /* M897: a stream that began and then failed (a stall, a cut)
                 * keeps its first-token time; it says the model answered. */
                if (bridge.first_ms > 0.0) {
                    cJSON_AddNumberToObject(o, "ttft_ms", bridge.first_ms - t0);
                }
                stamp_outcome(o, 1, -1);                        /* M536 */
                cJSON_AddStringToObject(o, "result", result);
                /* M1039: the stable failure class for telemetry greppability */
                if (req.fail_class != NULL) {
                    cJSON_AddStringToObject(o, "fail_class", req.fail_class);
                }
                cJSON_AddNumberToObject(o, "req_bytes", req_bytes);
                /* M321: the TRANSPORT diagnosis, so an after-the-fact log says
                 * which timeout fired instead of only `status: 0`. A 34k-event
                 * workload lost 6.5 h to a connect timeout that was
                 * indistinguishable from network flakiness in the log. */
                if (http_status == 0 && transport_err[0] != '\0') {
                    jc_eventlog_add_text(o, "transport", transport_err, 0);
                }
                /* M909: the server's message from an error event in the stream */
                if (stream_err[0] != '\0') {
                    jc_eventlog_add_text(o, "stream_error", stream_err, 0);
                }
                /* error_body is bounded at 2047 bytes to keep the event
                 * size reasonable. The cut already happened outside the
                 * telemetry block, so bridge.raw is ready for use. */
                jc_eventlog_add_text(o, "error_body", bridge.raw, 2047);
                /* The stall-campaign datum the 30 s default needs (M22a): a cut
                 * attempt where NOTHING arrived records req_bytes (prompt size)
                 * but no ttft_ms, so it was censored out of the first-byte
                 * distribution -- the cap that would be used to justify itself.
                 * stall_ms is the time the stream sat before the low-speed cut;
                 * with it, "cut at 30 s" can be correlated with "how big a
                 * prompt", the exact failure mode (slow local prefill on a big
                 * context) that the default must not paper over. Only on a
                 * timeout with no bytes -- a call that began then failed keeps
                 * ttft_ms (M897) and is a different shape. */
                if (st == JC_ERR_TIMEOUT && bridge.first_ms == 0.0) {
                    cJSON_AddNumberToObject(o, "stall_ms", latency);
                }
            }
            jc_eventlog_end(app->telemetry, o);
        }

        if (st == JC_ERR_TIMEOUT) {
            jc_logf(JC_LOG_WARN,
                    "model stalled: no response data within the timeout "
                    "(stall=%lds, request=%lds)",
                    req.stall_timeout_secs, req.timeout_secs);
            /* M23b: surface the stall in the front-end (TUI); headless/ACP have
             * no on_status, so they stay quiet. */
            if (cb != NULL && cb->on_status != NULL) {
                char banner[160];
                jc_snprintf(banner, sizeof(banner),
                            "model stalled: no data for %lds (%s)",
                            req.stall_timeout_secs,
                            app->config.model.name != NULL
                                ? app->config.model.name
                                : (app->config.model.model != NULL
                                   ? app->config.model.model : "model"));
                cb->on_status(cb->user, banner);
            }
        }

        if (st == JC_ERR_ABORTED) {
            break;
        }

        /* Failure. Retry only when transient and nothing has been emitted. */
        {
            int emitted = (assistant->content != NULL) ||
                          (jc_msg_tool_call_count(assistant) > 0);

            /* M1038: handle connect failures (no HTTP status) that indicate the
             * model's endpoint is down. Re-probe reachability and walk the
             * fallback chain if configured. This only applies when the active
             * model has a fallback (so behavior is unchanged when no fallback). */
            if (http_status == 0 && !emitted &&
                (st == JC_ERR_HTTP || st == JC_ERR_TIMEOUT)) {
                int active_idx = app->config.active;
                struct jc_model_cfg *active_m =
                    jc_config_model_at(&app->config, active_idx);
                /* Only process if the active model has a fallback configured. */
                if (active_m != NULL && active_m->fallback != NULL &&
                    active_m->fallback[0] != '\0') {
                    int n = jc_config_model_count(&app->config);
                    int j;
                    /* Invalidate reachability cache for this apiBase.
                     * Every model sharing the same apiBase is marked unknown.
                     * Guard on app->reach != NULL: if NULL, skip invalidation
                     * entirely (there is no stale cache to clear -- jc_app_effective_model
                     * will allocate and probe fresh). This prevents a NULL deref when
                     * n > JC_REACH_MAX or when the cache was never allocated. */
                    if (app->reach != NULL) {
                        for (j = 0; j < n && j < JC_REACH_MAX; j++) {
                            struct jc_model_cfg *mj =
                                jc_config_model_at(&app->config, j);
                            if (mj != NULL && mj->api_base != NULL &&
                                active_m->api_base != NULL &&
                                strcmp(mj->api_base, active_m->api_base) == 0) {
                                app->reach[j] = 0;  /* unknown */
                            }
                        }
                    }
                    /* Re-probe and walk the fallback chain. */
                    {
                        int eff = jc_app_effective_model(app, active_idx);
                        if (eff != active_idx) {
                            jc_status route_st;
                            route_st = jc_app_route_to(app, eff, "connect-fallback");
                            if (route_st == JC_OK) {
                                /* Emit telemetry event for the route. */
                                {
                                    cJSON *o = telem(app, "route");
                                    if (o != NULL) {
                                        cJSON_AddStringToObject(o, "to",
                                            app->config.model.name != NULL
                                                ? app->config.model.name : "");
                                        cJSON_AddStringToObject(o, "reason",
                                            "connect-fallback");
                                    }
                                    jc_eventlog_end(app->telemetry, o);
                                }
                                prov = app->provider;
                                /* Restart the loop with the new provider. */
                                continue;
                            }
                        }
                    }
                }
            }

            /* M1142 (SV:0419): !app->ctx_overflow -- a context-overflow reply
             * is DETERMINISTIC: the same request against the same window
             * fails identically, so retrying it only spends budget and
             * telemetry rows on a call that cannot succeed. M1125 made the
             * in-stream shape (st == JC_ERR_PROVIDER, http_status 200) hit the
             * gate; the in-stream over-context reply must be the one excluded.
             * Stale-flag safety: ctx_overflow is reset to 0 at the start of
             * every run (app->ctx_overflow = 0 in run's preamble); inside the
             * model-call loop the only call that can SET it is the very call
             * whose error we are judging here -- so no later model call in the
             * same run ever sees a stale 1. (The other stream_once caller,
             * relay_account_call, is terminal: it runs at the relay handover
             * and the loop breaks right after, so no gate is evaluated after
             * it -- and a real overflow there should indeed flag the run;
             * M917's reset covers the next run.) Across runs the reset in the
             * run preamble covers it.
             * (A set inside this call means the run ends on this call; there
             * is no path where a later call inherits it.) */
            if (is_transient(st, http_status) && !app->ctx_overflow && !emitted &&
                attempt < app->config.max_retries) {
                jc_logf(JC_LOG_WARN,
                        "transient error (status %ld); retry %d/%d in %ldms",
                        http_status, attempt + 1, app->config.max_retries,
                        backoff_ms);
                {
                    cJSON *o = telem(app, "model_retry");
                    if (o != NULL) {
                        cJSON_AddNumberToObject(o, "attempt", (double)attempt);
                        cJSON_AddNumberToObject(o, "status", (double)http_status);
                        cJSON_AddNumberToObject(o, "backoff_ms",
                                                (double)backoff_ms);
                        cJSON_AddStringToObject(o, "reason",
                            st == JC_ERR_TIMEOUT ? "timeout" : "transient");
                    }
                    jc_eventlog_end(app->telemetry, o);
                }
                if (cb != NULL && cb->on_status != NULL) { /* M99: stream to a driver */
                    char msg[96];
                    jc_snprintf(msg, sizeof(msg), "retry %d/%d in %ldms (%s)",
                                attempt + 1, app->config.max_retries, backoff_ms,
                                st == JC_ERR_TIMEOUT ? "timeout" : "transient");
                    cb->on_status(cb->user, msg);
                }
                if (jc_sleep_ms(backoff_ms, &app->abort_flag)) {
                    st = JC_ERR_ABORTED;
                    break;
                }
                backoff_ms *= 2;
                if (backoff_ms > 8000) {
                    backoff_ms = 8000;
                }
                continue;
            }
            if (st == JC_OK && http_status >= 400) {
                /* Surface the response body, not just the status: providers put
                 * the actionable reason there (e.g. ContextWindowExceededError).
                 * The capture cut ran earlier outside the telemetry block, so
                 * bridge.raw is already redacted if needed. M85. */
                if (bridge.raw_len > 0) {
                    jc_logf(JC_LOG_ERROR, "provider returned HTTP %ld: %.*s",
                            http_status, (int)bridge.raw_len, bridge.raw);
                    /* M854 (N24c): a request over the model's window. A
                     * parallel write child that ends this way answers with a
                     * note, and its work merges after a green verify. */
                    if (http_status == 400 &&
                        jc_http_body_ctx_overflow(bridge.raw, bridge.raw_len)) {
                        app->ctx_overflow = 1;
                    }
                } else {
                    jc_logf(JC_LOG_ERROR, "provider returned HTTP %ld",
                            http_status);
                }
                st = JC_ERR_PROVIDER;
            }
            /* M876 (N47 (a)): what ended this call, after its retries, for the
             * run's delegate report -- the transport's own diagnosis, or the
             * provider's status and the start of its body. Without it a parent
             * saw only "run failed" and the run's last failing TOOL call. */
            if (st != JC_OK && st != JC_ERR_ABORTED) {
                if (http_status == 0 && transport_err[0] != '\0') {
                    char redacted[sizeof app->last_model_error];
                    if (jc_redact_secrets(transport_err, redacted,
                                          sizeof redacted) > 0) {
                        jc_snprintf(app->last_model_error,
                                    sizeof app->last_model_error, "%s",
                                    redacted);
                    } else {
                        jc_snprintf(app->last_model_error,
                                    sizeof app->last_model_error, "%s",
                                    transport_err);
                    }
                } else if (stream_err[0] != '\0') {
                    /* M909: HTTP 200, then an error event -- say so, not "200" */
                    char redacted[sizeof app->last_model_error];
                    char fmt[64];
                    if (jc_redact_secrets(stream_err, redacted,
                                          sizeof redacted) > 0) {
                        jc_snprintf(fmt, sizeof fmt,
                                    "an error inside the provider's stream: %%s");
                        jc_snprintf(app->last_model_error,
                                    sizeof app->last_model_error, fmt,
                                    redacted);
                    } else {
                        jc_snprintf(app->last_model_error,
                                    sizeof app->last_model_error,
                                    "an error inside the provider's stream: %s",
                                    stream_err);
                    }
                } else if (http_status > 0) {
                    /* M1036 F1/F2/F3: redact the FULL body window, then take the 120-byte
                     * prefix of the REDACTED text (zero hits: bridge.raw is a verbatim source,
                     * the head is byte-identical to the pre-M1036 else branch). This fixes the
                     * 512-copy-boundary leak: a registered secret at body offset S < 120 with
                     * S+L > 512 (hence L >= 394) had no full match in the old 512-byte window,
                     * so the zero-hit branch copied its near half (bytes S..119) into
                     * last_model_error. Cost: one 2048-byte stack local, on the failure path
                     * only; this branch runs at most once per failed model call, and it joins
                     * a frame that already holds bridge.raw (struct stream_bridge, char
                     * raw[2048]) on the same stack; src/ creates no thread with a shrunk
                     * stack (no pthread_attr_setstacksize, no RLIMIT_STACK). */
                    if (jc_redact_secrets(bridge.raw, redacted, sizeof redacted) > 0) {
                        char head[121];
                        jc_size hl = (strlen(redacted) < 120) ? strlen(redacted) : 120;
                        memcpy(head, redacted, hl);
                        head[hl] = '\0';
                        jc_snprintf(app->last_model_error,
                                    sizeof app->last_model_error,
                                    "HTTP %ld from the provider%s%s", http_status,
                                    hl > 0 ? ": " : "", head);
                    } else {
                        /* Zero hits: verbatim copy, safe because secret is not present */
                        char head[121];
                        jc_size hl = (strlen(bridge.raw) < 120) ? strlen(bridge.raw) : 120;
                        memcpy(head, bridge.raw, hl);
                        head[hl] = '\0';
                        jc_snprintf(app->last_model_error,
                                    sizeof app->last_model_error,
                                    "HTTP %ld from the provider%s%s", http_status,
                                    hl > 0 ? ": " : "", head);
                    }
                } else {
                    jc_snprintf(app->last_model_error,
                                sizeof app->last_model_error,
                                "the model call failed");
                }
                app->last_model_attempts = attempt + 1;
                app->last_model_t0 = fail_t0;
                app->last_model_t1 = (long)time(NULL);
            }
            break;
        }
    }

    jc_http_headers_free(&headers);
    return st;
}

/* Per-run knobs that differ between the top-level agent and a subagent.
 * Threaded through run_agent_loop so neither path mutates parent app state. */
struct jc_run_opts {
    struct jc_provider *provider;      /* stream against this provider      */
    const char         *system_msg;    /* prebuilt system prompt            */
    int                 include_mutating; /* -> jc_tool_build_neutral_ex    */
    const char         *exclude_tool;  /* hide this tool name, or NULL      */
    const char         *exclude_tool2; /* hide a second tool name, or NULL  */
    const struct jc_vec *allow;        /* tool allow-list for this run, or NULL
                                        * (a subagent profile's `tools:`)    */
    int                 max_iters;     /* tool-iteration cap                */
    int                 auto_posture;  /* ASK verdicts run without a prompt */
    const char         *checkpoint_label; /* snapshot label (top-level only) */
};

/* True when the autonomy envelope governs this run (top-level agent only). */
/* M895 (N54 (b)): count one silent success of the normalised command `key` in this
 * turn's table and return its count -- 0 when the table is full, which counts
 * nothing rather than guess. */
#define JC_SILENT_KEYS 16
static int silent_count(unsigned long *hs, unsigned long *ns, int *cs, int *n,
                        const char *key)
{
    unsigned long kn = (unsigned long)strlen(key);
    unsigned long kh = jc_reread_hash(key, kn);
    int i;

    for (i = 0; i < *n; i++) {
        if (hs[i] == kh && ns[i] == kn) {
            return ++cs[i];
        }
    }
    if (*n >= JC_SILENT_KEYS) {
        return 0;
    }
    hs[*n] = kh;
    ns[*n] = kn;
    cs[*n] = 1;
    (*n)++;
    return 1;
}

static int env_active(const struct jc_app *app)
{
    return app->env != NULL && app->agent_depth == 0;
}

/* M921 (sub-slice B): record this run's last verify verdict so the relay baton
 * can name it in section 2 (6.4: "Last verify: red, 2 failures"). Called at
 * both verify sites (the completion gate and the periodic cadence) immediately
 * after jc_testparse has filled `rep` and `code` is the exit code. The names
 * come from the parsed failing tests' source paths, top 5, comma-joined and
 * cut to the field's width; a green or an unparseable red leaves it empty
 * (the note still says green / red, N failures). */
static void app_record_last_verify(struct jc_app *app, int code,
                                   struct jc_test_report *rep)
{
    char names[200];
    jc_size i;
    int added = 0;

    app->last_verify_code = code;
    app->last_verify_fail = (rep != NULL) ? rep->failed : -1;
    names[0] = '\0';
    if (rep != NULL) {
        for (i = 0; i < rep->failures.len && added < 5; i++) {
            const struct jc_test_failure *f =
                (const struct jc_test_failure *)jc_vec_at(&rep->failures, i);
            const char *p = (f != NULL && f->file != NULL && f->file[0] != '\0')
                             ? f->file : NULL;
            if (p == NULL) {
                continue;
            }
            if (names[0] != '\0') {
                jc_snprintf(names + strlen(names),
                            sizeof(names) - strlen(names), ", ");
            }
            jc_snprintf(names + strlen(names),
                        sizeof(names) - strlen(names), "%s", p);
            added++;
        }
    }
    jc_snprintf(app->last_verify_failnames,
                sizeof(app->last_verify_failnames), "%s", names);
}

/* M163a: is the tool named `name` kinetic? A user tool with kinetic:true, or
 * a tool from an MCP server marked kinetic:true. */
static int kinetic_tool_named(const struct jc_app *app, const char *name)
{
    const struct jc_vec *ut = &app->config.user_tools;
    jc_size i;
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < ut->len; i++) {
        const struct jc_user_tool_cfg *c =
            (const struct jc_user_tool_cfg *)jc_vec_at((struct jc_vec *)ut, i);
        if (c->name != NULL && strcmp(c->name, name) == 0) {
            return c->kinetic;
        }
    }
    return jc_mcp_tool_kinetic(app->mcp, name);
}

/* M163a: collect the kinetic shell-command prefixes (each kinetic user tool's
 * argv-form `command`, plus the operator `kineticShellPrefixes`) into `buf`
 * (borrowed config pointers, not copied). Returns the count (capped at cap). */
static int kinetic_collect_prefixes(const struct jc_app *app,
                                     const char **buf, int cap)
{
    const struct jc_vec *ut = &app->config.user_tools;
    const struct jc_vec *kp = &app->config.kinetic_prefixes;
    int n = 0;
    jc_size i;
    for (i = 0; i < ut->len && n < cap; i++) {
        const struct jc_user_tool_cfg *c =
            (const struct jc_user_tool_cfg *)jc_vec_at((struct jc_vec *)ut, i);
        if (c->kinetic && c->command != NULL && c->command[0] != '\0') {
            buf[n++] = c->command;
        }
    }
    for (i = 0; i < kp->len && n < cap; i++) {
        const char *p = *(char **)jc_vec_at((struct jc_vec *)kp, i);
        if (p != NULL && p[0] != '\0') {
            buf[n++] = p;
        }
    }
    return n;
}

/* True when an edit-scope fence is configured, at ANY depth (M133). A subagent
 * or a spawn_parallel write child inherits app->env (COW) but env_active() is
 * false for it, so before M133 the --edit-scope was silently unenforced for
 * delegated writers -- a write child could edit any file in its worktree and
 * first-wins-merge it back. This predicate re-arms just the scope fence for
 * them, without turning on the depth-0-only budget baseline / verify / review. */
static int env_scope_fence(const struct jc_app *app)
{
    return app->env != NULL && app->env->edit_scope.len > 0;
}

/* True when the run's BUDGET governs this agent, at ANY depth (M431) -- the same
 * shape as env_scope_fence above, for the next member of the same family.
 *
 * The envelope metered at every depth and enforced only at depth 0:
 * jc_env_record_tokens is gated on env != NULL alone, so a delegate's tokens were
 * always counted, while jc_env_over_budget sat behind env_active() and so was
 * never evaluated for one. Measured consequences: `--max-tool-calls 3` let a run
 * execute 9 (M422), and a spawn_parallel child -- which run_child starts at depth
 * 1 -- had its carefully computed 1/ntasks slice applied and then never consulted,
 * leaving the watchdog clock as the only thing bounding it.
 *
 * Deliberately NOT a loosening of env_active(): the journal, the verify gate, the
 * baseline, self-review and the periodic verify must stay depth-0 (a delegate that
 * ran the verifier could roll the tree back mid-parent-turn), and M133 already
 * declined that same loosening for the scope fence. Enforcement splits from
 * settlement instead: a delegate over budget stops and returns JC_OK with the work
 * intact, and the top level's own check then runs env_stop_for_budget. */
static int env_budget_applies(const struct jc_app *app)
{
    /* Only while the run is still RUNNING. Once an outcome is set the envelope is
     * in accounting mode, not enforcement mode -- and there is post-outcome work
     * that must not be blocked by a budget the run already spent: `learnOnStop`
     * fires the mentor AFTER the `end` event, which is exactly why M328-M330 give
     * it its own `learn_on_stop` journal event and `runs`' separate learn_tokens /
     * learn_calls columns rather than folding it into the run's totals. Charging
     * it here would have made the mentor unrunnable on every run that used its
     * budget -- that is, on every run worth learning from. Caught by
     * learn_on_stop_cost.sh, which arms `--budget-tokens 1` on purpose. */
    return app->env != NULL && app->env->outcome == JC_ENV_RUNNING;
}

/* M429: build the tool result for a POLICY-BLOCKED call, appending a note when the
 * model has just tried the same forbidden thing again.
 *
 * Both block sites route through here so the wording cannot diverge, and both pass
 * a `target` (the path, or the tool name where there is no path) so switching from
 * the shell to write_file for the same file still counts as a repeat.
 *
 * The note deliberately does NOT say "try a different approach" -- M89's advice for
 * a stuck verify, and exactly wrong here. A forbidden action does not succeed
 * however it is rephrased. Measured on chrtext (probe P13): five attempts at one
 * blocked write, the whole 150k token budget spent, and nothing said. A block is
 * journaled `blocked: true` rather than `ok:false`, so neither M89 nor the planned
 * loop detector could have seen it, and it does not count toward --max-tool-calls
 * either -- this notice is the only thing standing between that model and its
 * budget. */
static void block_message(struct jc_app *app, const char *tool,
                          const char *target, const char *base,
                          char *out, jc_size cap)
{
    int rep = jc_env_note_blocked(app->env, tool, target);

    /* M437: record the block for the delegation report. This is the ONE site that
     * matters most and the one that nearly got missed: a policy block never
     * reaches the loop's `is_error` branch (the comment there says so), and a
     * fence DENIAL is precisely the case a parent could not distinguish -- the
     * delegate cannot widen its own fence, so re-delegating the same subtask is
     * guaranteed to fail again. Found by this milestone's own smoke driver, whose
     * denial checks failed while everything else passed.
     *
     * Classified as DENIED directly rather than through jc_fail_classify: the
     * class is known here for certain, and inferring it from wording when the
     * fact is in hand would be strictly worse. */
    if (app != NULL) {
        jc_snprintf(app->last_fail_tool, sizeof app->last_fail_tool, "%s",
                    (tool != NULL) ? tool : "");
        jc_snprintf(app->last_fail_msg, sizeof app->last_fail_msg,
                    "refused: %s is outside this run's edit scope",
                    (target != NULL) ? target : "that path");
        app->last_fail_cls = (int)JC_FAIL_DENIED;
        app->last_fail_stopped = 0;   /* M885: a refusal, not a killed call */
    }

    if (rep < 2) {
        jc_snprintf(out, cap, "%s", (base != NULL) ? base : "");
        return;
    }
    jc_snprintf(out, cap,
        "%s\n\nNOTE: you have now been blocked from this same action %d times in "
        "a row. It is FORBIDDEN by this run's policy -- it is not failing by "
        "accident, and it will not succeed if you rephrase the command or reach "
        "the same target with a different tool. Do the task within the paths you "
        "are allowed, or stop and say in your final answer that it cannot be done "
        "under this run's scope.", (base != NULL) ? base : "", rep);
    jc_logf(JC_LOG_WARN,
            "envelope: blocked the same action %dx (%s -> %s)", rep,
            (tool != NULL) ? tool : "?", (target != NULL) ? target : "?");
    if (env_active(app)) {
        cJSON *jo = jc_env_journal_begin(app->env, "blocked_repeat");
        if (jo != NULL) {
            cJSON_AddStringToObject(jo, "name", (tool != NULL) ? tool : "");
            cJSON_AddStringToObject(jo, "target",
                                    (target != NULL) ? target : "");
            cJSON_AddNumberToObject(jo, "repeat", (double)rep);
        }
        jc_env_journal_end(app->env, jo);
    }
}

/* Return the first path in a file-mutating call that falls OUTSIDE the run's
 * edit scope, or NULL if the call is entirely in scope / not a write tool.
 * Handles edit_file/write_file ("path") and apply_patch ("edits":[{"path"}]).
 * `args_json` is the raw arguments string. The returned pointer is owned by
 * `*hold` (a cJSON tree the caller must delete); NULL leaves *hold untouched.
 *
 * The fence MUST see the EXACT same tree the executor uses, or an argument-shape
 * bypass opens (self-named wrapper, stringified edits). This function calls
 * jc_tool_normalize_args (M981) to reproduce the executor's normalisation. */
static const char *call_out_of_scope_path(struct jc_app *app,
                                          const char *name,
                                          const char *args_json,
                                          cJSON **hold)
{
    const struct jc_tool *t;
    cJSON *aj = NULL;
    const char *bad = NULL;
    /* M535: `format_file` belongs here. It takes a `path` and rewrites exactly
     * that file, and it was NOT in this list -- so `jc_agent_tool_category`
     * counted six write tools while this fence knew three. Measured: with
     * an edit scope covering only src, `format_file {"path":"README.md"}` rewrote
     * README.md and the envelope reported "verified ok". Two lists answering
     * "which tools write?" is the M501 drift, in the sibling M501 did not
     * carry the fix to. */
    int is_single = (strcmp(name, "edit_file") == 0 ||
                     strcmp(name, "write_file") == 0 ||
                     strcmp(name, "format_file") == 0);
    int is_patch = (strcmp(name, "apply_patch") == 0);
    *hold = NULL;
    if (!is_single && !is_patch) {
        return NULL;
    }
    /* Get the tool to obtain its schema (for unstringing). */
    t = jc_tool_registry_find(app->tools, name);
    if (t == NULL) {
        return NULL;  /* Unknown tool: let the executor handle it */
    }
    /* Normalise the arguments exactly as the executor does (M981).
     * Pass the tool's schema so we can unstring nested array/object args.
     * If normalization fails (e.g. unrepairable JSON, elided placeholder),
     * the executor would reject this call, so we also reject it as OUT of scope.
     * Pass NULL for app to suppress telemetry: the fence only checks scope,
     * the executor is the sole emitter of all args_repair events (so a wrapped
     * or repaired call under an armed scope does not log the repair twice). The
     * transforms (unwrap/unstring) are app-independent, so the fence still sees
     * exactly the tree the executor will act on. */
    {
        cJSON *fsch = t->schema ? t->schema() : NULL;
        aj = jc_tool_normalize_args(name, fsch, args_json, NULL, NULL);
        if (fsch != NULL) {
            cJSON_Delete(fsch);
        }
    }
    if (aj == NULL) {
        /* The helper returns NULL when the executor would reject this call:
         * unrepairable JSON, a truncated response (M334), or an elided
         * placeholder (M289). None of those is a scope decision -- report it
         * as one and the model chases the wrong fence (M333: a run emitted
         * 197 pathless write_file calls, was told "out of edit-scope" 177
         * times, and spent 6,977,850 tokens guessing at a fence that was never
         * the obstacle). Return NULL and let the executor's own specific
         * message (truncation / parse / placeholder) reach the model. */
        return NULL;
    }
    if (is_single) {
        const char *path = jc_json_get_str(aj, "path", NULL);
        /* M333: a MISSING or empty `path` is a malformed call, not a scope
         * violation. */
        if (path == NULL || path[0] == '\0') {
            cJSON_Delete(aj);
            return NULL;
        }
        if (!jc_env_path_in_scope(app->env, app->root, path)) {
            *hold = aj;
            return path;
        }
    } else {
        cJSON *edits = cJSON_GetObjectItem(aj, "edits");
        cJSON *it;
        /* The normalized tree should have 'edits' as a real array (unstring was applied).
         * A stringified array was unstringed by the helper, so this should be an array. */
        if (edits != NULL && cJSON_IsArray(edits)) {
            for (it = edits->child; it != NULL; it = it->next) {
                const char *path = jc_json_get_str(it, "path", NULL);
                if (!jc_env_path_in_scope(app->env, app->root, path)) {
                    bad = (path != NULL) ? path : "";
                    break;
                }
            }
        }
        if (bad != NULL) {
            *hold = aj;
            return bad;
        }
    }
    cJSON_Delete(aj);
    return NULL;
}

/* Whether the self-review pass should run: config 1 forces on, 0 forces off,
 * and the -1 default enables it only in AUTO (unsupervised) mode. */
static int self_review_on(const struct jc_app *app)
{
    if (app->config.self_review == 0) return 0;
    if (app->config.self_review == 1) return 1;
    return app->mode == JC_MODE_AUTO;
}

/* Roll the workspace back to the last known-good (green) checkpoint, if one was
 * recorded and rollback is enabled. The turn still returns JC_OK; app->env's
 * outcome carries the verdict to the caller. */
static jc_status env_rollback_and_finish(struct jc_app *app)
{
    struct jc_envelope *e = app->env;

    if (!e->rollback_on_fail) {
        return JC_OK;
    }
    if (e->green_commit[0] != '\0' && jc_snapshot_available(app->snapshots)) {
        /* Capture what the revert will DISCARD (current tip vs green) BEFORE the
         * restore throws it away. */
        struct jc_sb disc_names;
        struct jc_sb disc_sum;
        int disc_n = 0;
        jc_sb_init(&disc_names);
        jc_sb_init(&disc_sum);
        if (jc_snapshot_changed_since(app->snapshots, e->green_commit,
                                      &disc_names) == JC_OK) {
            disc_n = jc_env_summarize_paths(disc_names.data, disc_names.len,
                                            8, &disc_sum);
        }
        /* M336 PRESERVE-BEFORE-DESTROY: the lines above worked out the NAMES of
         * the files this restore is about to discard. Commit their CONTENT first,
         * pinned under a ref outside the undo chain, so the attempt survives its
         * own rejection. One rollback destroyed 711,628 tokens of tests because
         * this did not exist (ANECDOTES #48).
         *
         * Failure here never blocks the rollback: a run that cannot be rolled back
         * is a worse outcome than one whose discarded state was not saved. */
        if (e->preserve_discarded && disc_n > 0
                && jc_snapshot_available(app->snapshots)) {
            char dref[220];
            char dsha[64];
            char dlabel[400];
            e->discarded_n++;
            jc_snprintf(dref, sizeof(dref), "refs/jichi/discarded/%s/%d",
                        e->run_id[0] != '\0' ? e->run_id : "run",
                        e->discarded_n);
            /* The message is all a future reader has (design decision D3). */
            jc_snprintf(dlabel, sizeof(dlabel),
                        "discarded: %s\n\nrun: %s\noutcome: %s\nverify: %s\n"
                        "tokens: %.0f\ncalls: %d\nfiles: %d",
                        jc_env_outcome_name(e->outcome),
                        e->run_id[0] != '\0' ? e->run_id : "(none)",
                        jc_env_outcome_name(e->outcome),
                        e->verify_cmd != NULL ? e->verify_cmd : "(none)",
                        e->tokens_used, e->tool_calls, disc_n);
            if (jc_snapshot_preserve(app->snapshots, dlabel, dref,
                                     dsha, sizeof(dsha)) == JC_OK
                    && dsha[0] != '\0') {
                cJSON *pj;
                /* M337 gave this a command of its own; before that the advice
                 * was a raw git incantation naming a path the user had to find. */
                jc_logf(JC_LOG_WARN, "envelope: preserved the discarded state at "
                        "%s (%.12s) -- `jichi attempts` lists it, `jichi recover "
                        "%.12s --into <dir>` gets it back", dref, dsha, dsha);
                pj = jc_env_journal_begin(e, "preserved");
                if (pj != NULL) {
                    cJSON_AddStringToObject(pj, "ref", dref);
                    cJSON_AddStringToObject(pj, "commit", dsha);
                    cJSON_AddNumberToObject(pj, "files", (double)disc_n);
                }
                jc_env_journal_end(e, pj);
            }
        }
        /* M337b: restore_commit now preserves generically like /undo does. This
         * rollback has already been accounted for -- either preserved above with
         * the run's outcome, tokens and calls, or deliberately not (no changed
         * files, or the feature off) -- so the generic record would at best
         * duplicate it and at worst pin an empty tree. Decided here, once, by the
         * caller that knows; the layer is not given a way to guess. */
        if (jc_snapshot_available(app->snapshots)) {
            app->snapshots->preserve_skip_once = 1;
        }
        if (jc_snapshot_restore_commit(app->snapshots, e->green_commit)
                == JC_OK) {
            /* After the restore the tree IS green; what remains changed vs the
             * fixed run-start baseline is what the revert KEPT. */
            struct jc_sb kept_names;
            struct jc_sb kept_sum;
            int kept_n = 0;
            cJSON *o;
            jc_sb_init(&kept_names);
            jc_sb_init(&kept_sum);
            if (e->baseline_commit[0] != '\0' &&
                jc_snapshot_changed_since(app->snapshots, e->baseline_commit,
                                          &kept_names) == JC_OK) {
                kept_n = jc_env_summarize_paths(kept_names.data,
                                                kept_names.len, 8, &kept_sum);
            }
            o = jc_env_journal_begin(e, "rollback");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "to", e->green_commit);
                cJSON_AddNumberToObject(o, "kept_files", (double)kept_n);
                cJSON_AddNumberToObject(o, "discarded_files", (double)disc_n);
                if (kept_sum.data != NULL) {
                    cJSON_AddStringToObject(o, "kept", kept_sum.data);
                }
                if (disc_sum.data != NULL) {
                    cJSON_AddStringToObject(o, "discarded", disc_sum.data);
                }
            }
            jc_env_journal_end(e, o);
            e->rolled_back = 1; /* M92: the work was actually reverted */
            /* Report the post-revert reality so a supervisor isn't left guessing
             * what survived (B: budget-revert transparency). */
            jc_logf(JC_LOG_WARN,
                    "envelope: rolled back to green -- kept %d file(s): %s | "
                    "discarded %d file(s): %s",
                    kept_n, (kept_sum.data != NULL) ? kept_sum.data : "-",
                    disc_n, (disc_sum.data != NULL) ? disc_sum.data : "-");
            jc_sb_free(&kept_names);
            jc_sb_free(&kept_sum);
        } else {
            jc_logf(JC_LOG_WARN, "envelope: rollback to green failed");
        }
        jc_sb_free(&disc_names);
        jc_sb_free(&disc_sum);
    } else {
        jc_logf(JC_LOG_WARN,
                "envelope: no green checkpoint to roll back to");
    }
    return JC_OK;
}

/* M83: warn (+ journal) about files a run changed OUTSIDE its edit scope --
 * changes the file-write fence (edit_file/write_file) can't catch because they
 * came through the shell (rm/mv/redirect via run_terminal_command). Diffs the
 * final tree against the fixed run-start baseline. Detection only (no revert):
 * loud enough that a stray shell change like `rm .jichi/memory.md` is surfaced on
 * stderr + the journal instead of passing silently on a green run. Top-level. */
static void env_report_out_of_scope(struct jc_app *app,
                                    const struct jc_agent_callbacks *cb)
{
    struct jc_envelope *e = app->env;
    struct jc_sb names;
    struct jc_vec paths;  /* of char*  (into names.data)  */
    struct jc_vec oos;    /* of char*  (out-of-scope subset) */
    char *s;
    char *line;

    if (e == NULL || e->edit_scope.len == 0 || e->baseline_commit[0] == '\0' ||
        !jc_snapshot_available(app->snapshots)) {
        return;
    }
    /* M689: from here the diff is genuinely taken, so its result is evidence.
     * Set before the early return below, which is the "nothing changed" case
     * and therefore the one the footer most wants to know about. */
    e->sweep_ran = 1;
    jc_sb_init(&names);
    if (jc_snapshot_changed_since(app->snapshots, e->baseline_commit, &names)
            != JC_OK || names.data == NULL) {
        jc_sb_free(&names);
        return;
    }
    /* M1095 (D-03): split the NUL-separated raw name list (one path per
     * NUL-terminated segment, trailing NUL). The bounds guard keeps the walk
     * safe even if the final segment lacks its NUL. */
    jc_vec_init(&paths, sizeof(char *));
    line = names.data;
    while (line != NULL && line < names.data + names.len && *line != '\0') {
        s = line;
        while (*s != '\0') {
            s++;
        }
        jc_vec_push(&paths, &line);
        line = s + 1;
    }
    /* M689: the whole changed set, before the edit-scope filter below. The
     * footer needs "did ANYTHING change", not "did anything change outside the
     * scope" -- a shell command that rewrote an in-scope file is still a shell
     * command whose changes are unattributed. */
    if (paths.len > 0) {
        e->tree_changed = 1;
    }
    jc_vec_init(&oos, sizeof(char *));
    jc_env_out_of_scope_paths(e, app->cwd, (const char *const *)paths.data,
                              (int)paths.len, &oos);
    /* M289: this guard runs at EVERY top-level turn end and diffs against the
     * FIXED run-start baseline, so a file changed once stays changed and used to
     * be re-reported every turn after -- one run logged 17 `out_of_scope` events
     * that were all the same path, which reads as 17 violations in `runs` and is
     * the noise that pushed a real user to widen `editScope` instead of looking
     * at the one file. Drop the ones already reported, in place. */
    {
        jc_size r = 0;
        jc_size w = 0;
        for (r = 0; r < oos.len; r++) {
            char *nm = *(char **)jc_vec_at(&oos, r);
            if (jc_env_oos_reported(e, nm)) {
                continue;
            }
            *(char **)jc_vec_at(&oos, w) = nm;
            w++;
        }
        oos.len = w;
    }
    if (oos.len > 0) {
        cJSON *o = jc_env_journal_begin(e, "out_of_scope");
        cJSON *arr = (o != NULL) ? cJSON_CreateArray() : NULL;
        jc_size i;
        /* M332: the count was computed here and discarded. REFUSE-THE-GREEN needs
         * it after the loop returns, so accumulate it on the envelope. Counted
         * AFTER the already-reported paths are dropped above, so a path reported
         * on an earlier turn does not taint the verdict twice. */
        e->out_of_scope_seen += (int)oos.len;
        jc_logf(JC_LOG_WARN, "envelope: %lu file(s) changed OUTSIDE the edit "
                "scope (shell-introduced?) -- review:", (unsigned long)oos.len);
        for (i = 0; i < oos.len; i++) {
            const char *nm = *(char **)jc_vec_at(&oos, i);
            jc_logf(JC_LOG_WARN, "  out-of-scope: %s", nm);
            if (arr != NULL) {
                cJSON_AddItemToArray(arr, cJSON_CreateString(nm));
            }
        }
        if (o != NULL && arr != NULL) {
            cJSON_AddItemToObject(o, "paths", arr);
        }
        /* M684: and WHICH of them git tracks. `paths` keeps its shape -- every
         * existing reader, tests/measure/strict_green_fp.py included, still
         * works -- and `tracked` names the subset. A parallel array of booleans
         * desynchronises silently; rewriting `paths` into objects breaks the
         * one reader this field exists to serve.
         *
         * THE KEY IS ABSENT, NOT EMPTY, when git could not answer -- no
         * repository, no git on PATH. "None of these are tracked" and "nobody
         * asked" are different facts, and a rule that treats the second as the
         * first would downgrade every run in a workspace without version
         * control. M662's measurement was reversed by exactly this kind of
         * conflation, so the distinction is in the wire format rather than in
         * a convention a reader has to know. */
        if (o != NULL && oos.len > 0) {
            const char **names = (const char **)
                malloc(sizeof(char *) * (size_t)oos.len);
            int *istrk = (int *)malloc(sizeof(int) * (size_t)oos.len);
            if (names != NULL && istrk != NULL) {
                for (i = 0; i < oos.len; i++) {
                    names[i] = *(char **)jc_vec_at(&oos, i);
                }
                if (jc_git_tracked_paths(app->cwd, names, (int)oos.len,
                                         istrk) == 0) {
                    cJSON *tr = cJSON_CreateArray();
                    if (tr != NULL) {
                        for (i = 0; i < oos.len; i++) {
                            if (istrk[i]) {
                                cJSON_AddItemToArray(tr,
                                    cJSON_CreateString(names[i]));
                            }
                        }
                        cJSON_AddItemToObject(o, "tracked", tr);
                    }
                }
            }
            free((void *)names);
            free(istrk);
        }
        /* M142 (opt-in): put the out-of-scope files back the way the run
         * found them -- per-path restore from the run-start baseline, so
         * in-scope work is untouched. Prevention where M83 only detected. */
        if (e->revert_out_of_scope) {
            int nrev = 0;
            int nfail = 0;
            /* M501: revert only what THIS RUN wrote. The envelope used to have
             * no provenance for a working-tree change -- "changed since my
             * baseline" and "changed by me" were one predicate -- so a
             * colleague merging reviewed files into the tree while a run was
             * going would have had that merge individually reverted to the
             * run's baseline. That near-miss is recorded in DEFERRED; this is
             * the option it called "scope the revert to paths the run's own
             * tools touched".
             *
             * The two lists are partitioned rather than filtered, because the
             * ones we did NOT write still have to be REPORTED (they always
             * were) and now have to be reported DIFFERENTLY: left alone, and
             * said to be left alone. Silence there would be the worse bug --
             * an operator who asked for reverting would assume it happened.
             *
             * A shell-introduced change is deliberately not in the write set:
             * jichi cannot tell a model-issued `sed -i` from a human's editor,
             * and between reverting a colleague's work and leaving a reported
             * violation in place, the second is recoverable. */
            struct jc_vec mine;
            jc_size k;
            int nforeign = 0;
            jc_vec_init(&mine, sizeof(char *));
            for (k = 0; k < oos.len; k++) {
                const char *nm = *(char **)jc_vec_at(&oos, k);
                if (jc_env_wrote(e, app->root, nm) || e->shell_ran) {
                    /* Ours outright, or attributable: a shell command ran this
                     * run and the shell is the one writer the fence does not
                     * cover, so M142's revert still applies. */
                    jc_vec_push(&mine, &nm);
                } else {
                    /* PROVABLY not ours. Every write path jichi has is either
                     * the chokepoint (checked above) or the shell (which never
                     * ran), and the file tools are fenced to the scope -- so
                     * this change came from outside the run and reverting it
                     * would destroy work jichi did not do. That is the
                     * near-miss this rule exists for. */
                    nforeign++;
                    jc_logf(JC_LOG_WARN, "  NOT reverted -- this run wrote no "
                            "files and ran no shell command, so it cannot have "
                            "made this change: %s", nm);
                    jc_env_oos_mark(e, nm);
                }
            }
            if (mine.len > 0) {
                jc_snapshot_restore_paths(app->snapshots, e->baseline_commit,
                                          (const char *const *)mine.data,
                                          (int)mine.len, &nrev, &nfail);
            }
            jc_vec_free(&mine);
            if (o != NULL) {
                cJSON_AddNumberToObject(o, "reverted", (double)nrev);
                cJSON_AddNumberToObject(o, "revert_failed", (double)nfail);
                /* Named in the journal so a supervisor sees that the sweep
                 * declined to touch something, not just what it touched. */
                cJSON_AddNumberToObject(o, "not_ours", (double)nforeign);
            }
            jc_logf(nfail > 0 ? JC_LOG_WARN : JC_LOG_INFO,
                    "envelope: reverted %d out-of-scope file(s) this run wrote, "
                    "to the run-start baseline%s", nrev,
                    nfail > 0 ? " (some restores FAILED -- see above)" : "");
            jc_env_journal_end(e, o);
            if (cb != NULL && cb->on_status != NULL) {
                cb->on_status(cb->user, nfail > 0
                    ? "envelope: out-of-scope changes reverted (some FAILED)"
                    : (nforeign > 0
                       ? "envelope: reverted this run's out-of-scope writes; "
                         "left changes this run did not make"
                       : "envelope: out-of-scope file change(s) reverted"));
            }
        } else {
            /* Detection-only: remember these so later turns stay quiet about
             * the same files. Not done on the revert path above -- a revert
             * makes the tree clean, so the next diff will not list the path
             * unless it changes AGAIN, which is a new violation. */
            for (i = 0; i < oos.len; i++) {
                jc_env_oos_mark(e, *(char **)jc_vec_at(&oos, i));
            }
            jc_env_journal_end(e, o);
            if (cb != NULL && cb->on_status != NULL) {
                cb->on_status(cb->user,
                    "envelope: out-of-scope file change(s) -- see log");
            }
        }
    }
    jc_vec_free(&oos);
    jc_vec_free(&paths);
    jc_sb_free(&names);
}

/* M331: after a *red* verify, warn when the verdict disagrees with its own
 * evidence -- tests ran, none failed, and the gate still says no. Advisory only,
 * like its M86 sibling: it adds `passed`/`consistency` to the open verify record
 * `jrec`, logs, and pings on_status, never touching the outcome. Returns the
 * finding so the caller can put one sentence in front of the model, which is the
 * whole return on this check: it is what stops a run repairing a phantom.
 * Caller guarantees the verify FAILED (exit != 0) and the envelope is active. */
static enum jc_verify_consistency env_verify_consistency_check(
    struct jc_app *app, const struct jc_agent_callbacks *cb,
    const struct jc_test_report *rep, int code, cJSON *jrec)
{
    enum jc_verify_consistency v;
    const char *msg = NULL;
    const char *tag = NULL;

    v = jc_env_verify_consistency(code, rep->passed, rep->failed,
                                  app->env->verify_max_tests);
    if (v == JC_VERIFY_HOLLOW_RED) {
        msg = "the gate failed but reported no failing test -- look at the "
              "harness (build wrapper, lint step, exit code) before the code";
        tag = "hollow_red";
    } else if (v == JC_VERIFY_RED_TESTS_GONE) {
        msg = "the gate failed and ran fewer tests than earlier -- were tests "
              "removed or un-wired?";
        tag = "red_tests_gone";
    }
    if (msg != NULL) {
        jc_logf(JC_LOG_WARN, "envelope: %s (%d passed, %d failed, was %d)", msg,
                rep->passed, rep->failed, app->env->verify_max_tests);
        if (jrec != NULL) {
            cJSON_AddStringToObject(jrec, "consistency", tag);
        }
        if (cb != NULL && cb->on_status != NULL) {
            cb->on_status(cb->user, msg);
        }
    }
    return v;
}

/* M431: render the M331 finding for the MODEL, as the sentence that must arrive
 * BEFORE the evidence it reframes. A model handed "exit 1" plus a list of zero
 * failures reads the exit code and starts editing code; this is the only thing
 * that redirects it at the harness.
 *
 * Factored because it has two call sites -- the completion fix-forward and the
 * periodic red -- and two copies of a paragraph this load-bearing would drift
 * (the M296 rule: two surfaces must not describe one result two ways). Appends
 * nothing for JC_VERIFY_AGREES, so the caller can call it unconditionally. */
static void verify_consistency_note(enum jc_verify_consistency v,
                                    const struct jc_test_report *rep,
                                    const struct jc_envelope *env,
                                    struct jc_sb *sb)
{
    if (v == JC_VERIFY_HOLLOW_RED) {
        jc_sb_append_fmt(sb,
            "NOTE: %d test(s) passed and NONE failed, yet the "
            "command still reported failure. The fault is "
            "likely in the verification harness itself -- a "
            "wrapper's exit code, a lint or build step beside "
            "the tests, or output the build system treats as "
            "failure -- rather than in the code under test. "
            "Check that before changing code.\n", rep->passed);
    } else if (v == JC_VERIFY_RED_TESTS_GONE) {
        jc_sb_append_fmt(sb,
            "NOTE: this run has seen %d test(s) pass earlier and "
            "only %d now. Check whether tests were removed or "
            "stopped being reached.\n",
            env->verify_max_tests, rep->passed);
    }
}

/* M86: after a *green* verify, warn when the gate looks hollow -- it ran zero
 * tests, or fewer than an earlier green this run (the hollow-gate lesson: a
 * gate can pass while whole subsystems never compile/run). Advisory only: it
 * adds `tests`/`sanity` to the already-open verify journal record `jrec`, logs
 * a warning, and pings on_status -- it never changes the run's outcome. Records
 * the observed count as the run's new test-count high-water. Caller guarantees
 * the verify passed (exit 0) and the envelope is active.
 *
 * M351: `hist` non-NULL additionally tells the MODEL, once per run -- the loop
 * feeds every RED verify back (fix-forward), so the model always hears its
 * failures, but a hollow GREEN went to the operator alone while the model
 * banked the false confidence and built on "tests pass" (the 253-greens-over-
 * six-compile-errors class). The periodic site passes hist (a next model call
 * exists to read it); the completion site passes NULL (a green there ends the
 * run, so a note would be written to nobody -- the journal covers the
 * post-mortem). */
static void env_verify_sanity_check(struct jc_app *app,
                                    const struct jc_agent_callbacks *cb,
                                    const struct jc_test_report *rep,
                                    cJSON *jrec, struct jc_history *hist)
{
    int count;
    enum jc_verify_sanity v;
    const char *msg = NULL;

    count = jc_test_report_count(rep);
    if (jrec != NULL && count >= 0) {
        cJSON_AddNumberToObject(jrec, "tests", (double)count);
    }
    v = jc_env_verify_sanity(count, app->env->verify_max_tests,
                             app->env->test_file_written);
    /* M692: green, but the verifier printed no test figure at all, so none of
     * the three hollow-gate checks below could run. Recorded on the envelope
     * (the reach footer reports it as NOT CHECKED) and in the journal, but
     * deliberately NOT as a warning and NOT to the model: a verifier that is
     * silent on success is a normal setup -- `zig build test` is one -- and a
     * per-run warning about it would fire on every run of every such project,
     * which is how a warning stops being read. */
    if (v == JC_VERIFY_NO_COUNT) {
        app->env->verify_no_count = 1;
        if (jrec != NULL) {
            cJSON_AddStringToObject(jrec, "sanity", "no_count");
        }
    }
    if (v == JC_VERIFY_NO_TESTS) {
        msg = "verify passed but ran 0 tests -- is the gate wired?";
    } else if (v == JC_VERIFY_FEWER_TESTS) {
        msg = "verify passed but ran fewer tests than earlier -- gate shrank?";
    } else if (v == JC_VERIFY_TESTS_NOT_WIRED) {
        msg = "verify passed but you edited a test file and the count did not "
              "grow -- is the new test reachable from what the gate compiles?";
    }
    if (msg != NULL) {
        jc_logf(JC_LOG_WARN, "envelope: %s (%d tests this run, was %d)", msg,
                count, app->env->verify_max_tests);
        if (jrec != NULL) {
            cJSON_AddStringToObject(jrec, "sanity",
                v == JC_VERIFY_NO_TESTS ? "no_tests"
                    : (v == JC_VERIFY_FEWER_TESTS ? "fewer_tests"
                                                  : "tests_not_wired"));
        }
        if (cb != NULL && cb->on_status != NULL) {
            cb->on_status(cb->user, msg);
        }
        if (hist != NULL && app->agent_depth == 0 &&
            !app->env->sanity_noticed) {
            struct jc_sb nb;
            jc_sb_init(&nb);
            jc_env_sanity_note(v, count, app->env->verify_max_tests, &nb);
            if (nb.len > 0 && nb.data != NULL) {
                app->env->sanity_noticed = 1;
                jc_history_add(hist, JC_ROLE_USER, nb.data);
            }
            jc_sb_free(&nb);
        }
    }
    if (count > app->env->verify_max_tests) {
        app->env->verify_max_tests = count;
    }
}

/* Record a budget-exhaustion outcome and journal it. M80: budget/deadline/
 * tool-call exhaustion is NOT a broken state (unlike a verify failure), so it does
 * not by itself discard the run's work. Roll back only if a verifier is configured
 * AND its tree is red at exit (rollback iff the gate is red, never merely because
 * the run ran out of budget) -- otherwise keep the partial work for the human to
 * review or resume. This fixed a design phase whose valid output doc was reverted
 * purely for hitting the token budget. */
static jc_status env_stop_for_budget(struct jc_app *app, enum jc_env_budget b)
{
    struct jc_envelope *e = app->env;
    cJSON *o;
    int has_green;
    int has_verifier;
    int code = 0;

    e->outcome = JC_ENV_BUDGET_EXHAUSTED;
    e->tripped = b; /* M97: remember which budget stopped the run (for the result) */
    o = jc_env_journal_begin(e, "budget");
    if (o != NULL) {
        cJSON_AddStringToObject(o, "kind", jc_env_budget_name(b));
    }
    /* M96: on a run that never made an edit, the deliverable is only the final
     * answer -- and a budget stop truncated it ("all reads, no synthesis"). Flag
     * it distinctly from an edit run's budget stop (which keeps partial work).
     * M97 stores it on the envelope so the headless result carries it too. */
    e->starved = jc_env_analysis_starved(e->outcome,
                                         jc_snapshot_available(app->snapshots),
                                         e->green_commit[0] != '\0');
    if (e->starved) {
        if (o != NULL) {
            cJSON_AddBoolToObject(o, "starved", 1);
        }
        jc_logf(JC_LOG_WARN,
                "envelope: budget exhausted before any edit -- this run's "
                "deliverable is its final answer, which is likely truncated. "
                "Narrow --reference-root, raise --budget-tokens, or instruct the "
                "task to read fewer files before it writes.");
    }
    jc_env_journal_end(e, o);
    jc_logf(JC_LOG_WARN, "envelope: %s budget exhausted", jc_env_budget_name(b));

    /* M207: OBSERVED green only. The pre-edit checkpoint is recorded as
     * green_commit on the premise that the tree started green; when that premise
     * is false (a run launched against an already-red gate) rolling back trades
     * real work for an equally broken baseline, which is precisely what M80's
     * "budget exhaustion is not a broken state" rule exists to prevent. */
    has_green = (e->green_commit[0] != '\0' && e->green_verified &&
                 jc_snapshot_available(app->snapshots));
    has_verifier = (e->verify_cmd != NULL && e->verify_cmd[0] != '\0');
    /* Only run the (possibly slow) verifier when its result could change the
     * decision -- i.e. rollback is armed and there is a green state + a gate. */
    if (e->rollback_on_fail && has_green && has_verifier) {
        struct jc_sb vout;
        cJSON *j;
        jc_sb_init(&vout);
        code = jc_env_run_verify(e->verify_cmd, app->cwd, &vout,
                                 &app->abort_flag, e->verify_timeout);
        jc_sb_free(&vout);
        j = jc_env_journal_begin(e, "verify");
        if (j != NULL) {
            cJSON_AddStringToObject(j, "phase", "budget_exit");
            cJSON_AddNumberToObject(j, "exit", (double)code);
        }
        jc_env_journal_end(e, j);
    }
    /* M506: the SAME verifier, run for the RECORD when its result cannot change
     * the decision. The branch above deliberately skips it then -- correct for a
     * decision, wrong for a report.
     *
     * Measured three times, most recently twice in one afternoon of dogfooding: a
     * run ends `budget_exhausted` with NO `verify` event at all, and one of those
     * runs had done the valuable half of its task -- its gate passed when
     * evaluated by hand a minute later. A supervisor reading the journal saw a
     * budget stop and nothing else, and the operator only found the finished work
     * by running `git status`.
     *
     * Advisory by construction: the exit code is journalled and logged and then
     * DROPPED. It must never turn budget exhaustion into a pass -- that would
     * make a stopped run indistinguishable from a completed one, which is the
     * opposite of the honesty this is for. The journal phase differs from the
     * deciding one (`budget_exit` vs `budget_exit_advisory`) so a reader can tell
     * a verdict that mattered from a verdict that merely informs. */
    if (has_verifier && !(e->rollback_on_fail && has_green)) {
        struct jc_sb vout;
        cJSON *j;
        int acode;
        jc_sb_init(&vout);
        acode = jc_env_run_verify(e->verify_cmd, app->cwd, &vout,
                                  &app->abort_flag, e->verify_timeout);
        jc_sb_free(&vout);
        j = jc_env_journal_begin(e, "verify");
        if (j != NULL) {
            cJSON_AddStringToObject(j, "phase", "budget_exit_advisory");
            cJSON_AddNumberToObject(j, "exit", (double)acode);
            cJSON_AddBoolToObject(j, "advisory", 1);
            cJSON_AddStringToObject(j, "kind",
                jc_env_verify_kind_name(e->verify_kind));
        }
        jc_env_journal_end(e, j);
        /* Said out loud too: "budget_exhausted" with a green gate is the case
         * where a reader most needs the second sentence. */
        if (acode == 0) {
            jc_logf(JC_LOG_WARN,
                    "envelope: the run stopped on a budget, but its verifier "
                    "PASSES on the tree as it stands -- the outcome remains "
                    "budget_exhausted (advisory verdict, it changes nothing)");
        } else {
            jc_logf(JC_LOG_INFO,
                    "envelope: verifier still red at the budget stop "
                    "(advisory, exit %d)", acode);
        }
    }
    if (jc_env_budget_rollback_decision(e->rollback_on_fail, has_green,
                                        has_verifier, code)) {
        jc_logf(JC_LOG_WARN,
                "envelope: verify red at budget exit -- rolling back to green");
        return env_rollback_and_finish(app);
    }
    if (has_verifier && has_green && e->rollback_on_fail) {
        jc_logf(JC_LOG_INFO,
                "envelope: verify green at budget exit -- keeping the work");
    } else if (has_verifier && e->rollback_on_fail && !e->green_verified &&
               e->green_commit[0] != '\0') {
        /* M207: the run edited but never saw a green verify, so there is no
         * known-good state to return to. Keeping the work is right; say so,
         * because silence here reads as "the gate passed". */
        jc_logf(JC_LOG_WARN,
                "envelope: no verify passed during this run, so there is no "
                "known-good checkpoint -- keeping the work rather than reverting "
                "to an unverified baseline. If the gate was already red before "
                "the run, fix it first: a rollback could not have helped.");
    }
    return JC_OK;
}

/* M431: one budget verdict, two settlements. Returns 1 when the caller must stop,
 * with *st set to what to return; 0 to carry on. Factored because the two check
 * sites must not diverge on which depth is allowed to settle a run -- the same
 * reason block_message() owns the block wording for its two sites. */
static int env_budget_should_stop(struct jc_app *app, jc_status *st)
{
    enum jc_env_budget b;

    if (!env_budget_applies(app)) {
        return 0;
    }
    b = jc_env_over_budget(app->env, (long)time(NULL));
    if (b == JC_BUDGET_NONE) {
        return 0;
    }
    if (app->agent_depth == 0) {
        *st = env_stop_for_budget(app, b);
        return 1;
    }
    /* A delegate. Stop where it stands and keep the work -- the iteration cap's
     * circuit-breaker contract, for the same reason: what it produced so far is
     * valid and is already in its history. Settlement (journal, verifier, the
     * rollback decision) belongs to the top level, whose own check runs next. */
    jc_logf(JC_LOG_WARN,
            "envelope: %s budget exhausted at depth %d -- stopping this "
            "sub-agent; the top-level run settles the outcome",
            jc_env_budget_name(b), app->agent_depth);
    app->last_run_budget_stopped = 1;
    *st = JC_OK;
    return 1;
}

/* M861 (N28): a spawn_parallel task's own tool-call budget, the lead's
 * `max_tool_calls` for it, binding in the forked child whether or not the run has
 * an envelope. The demos drive's lead wrote "past about 150 calls, stop" into
 * every task text, and workers went to 189, 302 and beyond: a worker cannot see
 * its own count, so the number has to bind. It stops at any depth of the child's
 * process, as a delegate's budget stop does -- the work kept, and the parent's
 * merge waits for a green verify (M854's endings). Recorded once, at the task's
 * own depth. */
static int task_budget_should_stop(struct jc_app *app, jc_status *st)
{
    if (app->task_tool_max <= 0 || app->task_tool_used < app->task_tool_max) {
        return 0;
    }
    jc_logf(JC_LOG_WARN, "task budget: %d of the %d tool calls this task allows "
            "are used -- stopping at depth %d; the work is kept",
            app->task_tool_used, app->task_tool_max, app->agent_depth);
    if (app->agent_depth == app->task_tool_depth) {
        cJSON *o = telem(app, "task_budget");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "phase", "stop");
            cJSON_AddNumberToObject(o, "used", (double)app->task_tool_used);
            cJSON_AddNumberToObject(o, "max", (double)app->task_tool_max);
        }
        jc_eventlog_end(app->telemetry, o);
        /* M807's child transcript: why this child stopped. */
        if (app->transcript != NULL) {
            cJSON *to = jc_eventlog_begin(app->transcript, "task_budget");
            if (to != NULL) {
                cJSON_AddStringToObject(to, "phase", "stop");
                cJSON_AddNumberToObject(to, "used", (double)app->task_tool_used);
                cJSON_AddNumberToObject(to, "max", (double)app->task_tool_max);
            }
            jc_eventlog_end(app->transcript, to);
        }
    }
    app->last_run_budget_stopped = 2;
    *st = JC_OK;
    return 1;
}

/* M861: the task budget's one note, at four-fifths of the budget the task
 * actually has -- its key, or the envelope's slice when that is smaller -- and
 * only at the task's own depth: a nested delegate's history is its own (M347,
 * which keeps the RUN's budget notice out of delegate histories; this budget is
 * the task's, set for it by its lead). A user-role note at the tail, M358's
 * shape, so the cached prefix stays byte-stable. */
static void task_budget_note(struct jc_app *app, struct jc_history *hist)
{
    struct jc_sb nb;
    int at;

    if (app->task_tool_max <= 0 || app->task_budget_noted ||
        app->agent_depth != app->task_tool_depth) {
        return;
    }
    at = jc_parallel_budget_note_at(app->task_tool_eff);
    if (at <= 0 || app->task_tool_used < at ||
        app->task_tool_used >= app->task_tool_eff) {
        return;
    }
    app->task_budget_noted = 1;
    jc_sb_init(&nb);
    jc_parallel_budget_note(app->task_tool_used, app->task_tool_eff, &nb);
    if (nb.data != NULL) {
        jc_history_add(hist, JC_ROLE_USER, nb.data);
    }
    jc_sb_free(&nb);
    {
        cJSON *o = telem(app, "task_budget");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "phase", "note");
            cJSON_AddNumberToObject(o, "used", (double)app->task_tool_used);
            cJSON_AddNumberToObject(o, "max", (double)app->task_tool_eff);
        }
        jc_eventlog_end(app->telemetry, o);
    }
}

/* Escalate the active model to the routing `strong` tier (top-level only).
 * Returns 1 if it actually switched, so the caller can re-point its provider. */
static int route_escalate(struct jc_app *app, const char *reason)
{
    int fast_idx;
    int strong_idx;

    if (app->agent_depth != 0) {
        return 0;
    }
    if (!jc_config_routing_resolve(&app->config, &fast_idx, &strong_idx)) {
        return 0;
    }
    /* If the strong tier's server is down, fall back per its chain. */
    strong_idx = jc_app_effective_model(app, strong_idx);
    if (app->config.active == strong_idx) {
        return 0;
    }
    if (jc_app_route_to(app, strong_idx, reason) == JC_OK) {
        cJSON *o = telem(app, "route");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "to",
                app->config.model.name != NULL ? app->config.model.name : "");
            cJSON_AddStringToObject(o, "reason", reason != NULL ? reason : "");
        }
        jc_eventlog_end(app->telemetry, o);
        return 1;
    }
    return 0;
}

/* M288: escalate when this turn is about to outgrow the FAST tier's window.
 *
 * The other three triggers all react to a failure; this one reacts to running out
 * of room, which is why a wide-window strong tier gets configured at all. Without
 * it a tier design can sit unused forever: one measured project logged routes=0
 * across 174 turns (verify never failed, nothing stalled, escalateOnError had to
 * be off for red builds). Checked BEFORE the request is built, so the roomier
 * model serves the very call that needed the room, and placed below the
 * compaction threshold so a wider window is preferred over discarding history.
 *
 * Returns 1 if it switched. Top-level only (route_escalate enforces that), and
 * inert unless the strong tier is genuinely roomier -- see
 * jc_config_context_escalate for why a global `contextLimit` makes it so. */
static int route_on_context(struct jc_app *app, struct jc_history *hist,
                            const struct jc_agent_callbacks *cb)
{
    int fast_idx, strong_idx;
    long est, fast_lim, strong_lim;

    if (app->agent_depth != 0 || app->config.routing.escalate_on_context <= 0) {
        return 0;
    }
    if (!jc_config_routing_resolve(&app->config, &fast_idx, &strong_idx)) {
        return 0;
    }
    if (app->config.active == strong_idx) {
        return 0;               /* already there; escalation is sticky per turn */
    }
    fast_lim = jc_compact_context_limit_at(app, fast_idx);
    strong_lim = jc_compact_context_limit_at(app, strong_idx);
    /* The calibrated estimate plus this request's measured non-history part --
     * the same quantity the compaction trigger evaluates (M286), so the two
     * thresholds are comparable and 75% really does come before 80%. */
    est = jc_compact_effective_est(app, hist);              /* M536 */
    if (!jc_config_context_escalate(est, fast_lim, strong_lim,
                                    app->config.routing.escalate_on_context)) {
        return 0;
    }
    if (!route_escalate(app, "context")) {
        return 0;
    }
    /* M298: remember the CAUSE, not just that a switch happened. Mid-turn
     * compaction may hand this room back, and only a room-caused escalation may
     * be undone -- a verify-fail or tool-error escalation said the fast model was
     * not capable enough, which freeing context does not change. */
    app->routed_for_context = 1;
    jc_logf(JC_LOG_INFO,
            "[route] context pressure: ~%ld of %ld tok on the fast tier -- "
            "escalated to a %ld tok window", est, fast_lim, strong_lim);
    if (cb != NULL && cb->on_status != NULL) {
        cb->on_status(cb->user,
                      "escalating: history is outgrowing this model's context");
    }
    return 1;
}

/* M298: undo a context-caused escalation once mid-turn compaction has given the
 * room back. The missing counterpart to route_on_context: M288 added
 * escalate-on-room without a way down, so a long --auto run that escalated once
 * stayed on the strong tier for every remaining iteration even after M76 elided the
 * history that caused it. The trigger and the remedy never spoke.
 *
 * Gated on the CAUSE (app->routed_for_context) and on a hysteresis gap, because a
 * bare threshold oscillates and every switch rebuilds the provider and drops the
 * cached prefix. Returns 1 if it switched back. Top-level only. */
static int route_off_context(struct jc_app *app, struct jc_history *hist,
                             const struct jc_agent_callbacks *cb)
{
    int fast_idx, strong_idx;
    long est, fast_lim;

    if (app->agent_depth != 0 || !app->routed_for_context) {
        return 0;
    }
    if (app->config.routing.escalate_on_context <= 0) {
        return 0;
    }
    if (!jc_config_routing_resolve(&app->config, &fast_idx, &strong_idx)) {
        return 0;
    }
    if (app->config.active != strong_idx) {
        return 0;               /* not on strong (a later switch moved us) */
    }
    fast_lim = jc_compact_context_limit_at(app, fast_idx);
    /* The same quantity both other thresholds evaluate (M286), so 55% is
     * comparable to the 75% that got us here and the 80% compaction trigger. */
    est = jc_compact_effective_est(app, hist);              /* M536 */
    if (!jc_config_context_deescalate(est, fast_lim,
                                      app->config.routing.escalate_on_context)) {
        return 0;
    }
    fast_idx = jc_app_effective_model(app, fast_idx);
    if (app->config.active == fast_idx) {
        return 0;
    }
    if (jc_app_route_to(app, fast_idx, "context-relieved") != JC_OK) {
        return 0;
    }
    app->routed_for_context = 0;
    {
        cJSON *o = telem(app, "route");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "to",
                app->config.model.name != NULL ? app->config.model.name : "");
            cJSON_AddStringToObject(o, "reason", "context-relieved");
        }
        jc_eventlog_end(app->telemetry, o);
    }
    jc_logf(JC_LOG_INFO,
            "[route] context relieved: ~%ld of %ld tok -- back to the fast tier",
            est, fast_lim);
    if (cb != NULL && cb->on_status != NULL) {
        cb->on_status(cb->user, "context relieved: back on the fast model");
    }
    return 1;
}

/* M921 (sub-slice C): the relay's account call (proposal 6.5). At the
 * round-boundary handover, one tagged user message asks the model -- which can
 * still see its whole context, the whole point -- to write a five-heading
 * handover note. The reply becomes the baton's section 3.
 *
 * HOW IT RUNS, and why it is a leaf:
 *  - it appends the instruction to the REAL history (the model writes from its
 *    whole context, not a rendered transcript -- 6.5's "why not 2");
 *  - it streams the reply into a scratch assistant message with a NULL text
 *    sink, so the account never reaches the UI or the headless `result` (which
 *    is jc_agent_turn_answer from the END -- a real reply would be returned as
 *    the run's answer if it were left in the history);
 *  - it TRUNCATES the history back to where it started, so the saved session
 *    and the result are wire-valid and unaffected. Only app->relay_account
 *    carries the account, which the baton writer renders into the notes.
 *  - any failure (st != JC_OK, an empty reply, a refusal) is a leaf: it falls
 *    back to quoting the last real answer, labelled. It NEVER changes the stop
 *    reason (the handover stands) or the return status of the loop.
 * Returns the account text via *out (a malloc'd string, or NULL if there was
 * nothing at all to say -- not even a last answer); *cut is set when the
 * reply was truncated by the provider's length cap. */
static void relay_account_call(struct jc_app *app, struct jc_history *hist,
                               struct jc_provider *prov,
                               const char *system_msg,
                               char **out, int *cut)
{
    static const char *const INSTR =
        "[relay] This session is ending because the context window is "
        "exhausted. Before it is saved, write a handover note for the next "
        "session that continues your work. Use exactly these five headings, "
        "in order, and keep it under 300 words:\n"
        "## Done\n## In progress\n## Next step\n## Decided\n## Dead ends\n"
        "Be concrete: name the files, commits and commands. Do not narrate "
        "the handover; just write the note.";
    struct jc_agent_callbacks silent;
    jc_size from;
    jc_status st;
    struct jc_message *acc;
    const char *last;

    *out = NULL;
    *cut = 0;
    from = jc_history_len(hist);
    /* A zeroed callback: every sink is NULL, so the account's text and
     * status never reach the UI or the headless output. (It is an
     * all-pointer struct, so memset to zero is a valid full initialisation.) */
    memset(&silent, 0, sizeof(silent));

    if (jc_history_add(hist, JC_ROLE_USER, INSTR) == NULL) {
        return;   /* OOM: no account; the handover stands */
    }
    /* The reply lands in a fresh, empty ASSISTANT message, exactly as the loop
     * does it (jc_agent.c:2293) -- NOT in the instruction message above. */
    acc = jc_history_add(hist, JC_ROLE_ASSISTANT, NULL);
    if (acc == NULL) {
        jc_history_truncate(hist, from);
        return;
    }
    st = stream_once(app, hist, system_msg, NULL, acc, &silent, prov, 0, 0);
    /* Re-fetch by index: the stream may have reallocated the message vector,
     * so the `acc` pointer above is not guaranteed stable after stream_once. */
    acc = jc_history_get(hist, jc_history_len(hist) - 1);
    if (st == JC_OK && acc != NULL && acc->content != NULL &&
        acc->content[0] != '\0') {
        *cut = acc->truncated ? 1 : 0;
        {
            size_t L = strlen(acc->content);
            *out = (char *)malloc(L + 1);
            if (*out != NULL) {
                memcpy(*out, acc->content, L + 1);
            }
        }
    } else {
        /* The leaf fallback (6.5): quote the last real answer, labelled. It
         * is the assistant message just BEFORE our appended instruction. */
        last = NULL;
        {
            jc_size i;
            for (i = from; i > 0; i--) {
                struct jc_message *m = jc_history_get(hist, i - 1);
                if (m != NULL && m->role == JC_ROLE_ASSISTANT &&
                    m->content != NULL && m->content[0] != '\0') {
                    last = m->content;
                    break;
                }
            }
        }
        if (last != NULL) {
            struct jc_sb b;
            jc_sb_init(&b);
            (void)jc_sb_append_fmt(&b,
                "(account call failed -- the previous leg's last answer is "
                "quoted instead; it may be partial)\n%s\n", last);
            if (b.data != NULL) {
                *out = (char *)malloc(b.len + 1);
                if (*out != NULL) {
                    memcpy(*out, b.data, b.len);
                    (*out)[b.len] = '\0';
                }
            }
            jc_sb_free(&b);
        }
    }
    /* Truncate back: the instruction and reply must not survive into the
     * saved session or the turn's answer. */
    jc_history_truncate(hist, from);
}

/* M1083 (D-08): persist the live session at a TOOL-ROUND BOUNDARY, where every
 * call of the round has its `jc_history_add_tool_result` landed and the history
 * is therefore a wire-consistent prefix. A SIGKILL or OOM-kill mid-turn then
 * loses at most the in-flight round instead of the whole conversation: the
 * finding's reproduction left NO session for an in-progress `--auto` turn
 * (only the previous turn's file), so `--continue` could not resume the work.
 *
 * The agent loop owns neither the session object nor its save cadence -- the
 * front-end (headless main.c, the TUI, ACP) holds `struct jc_session` and the
 * loop only sees `hist`, which is that session's embedded history
 * (`hist = &session.history`) at the top level. So the session is recovered the
 * container_of way, anchored on `app->todos`, which every top-level caller wires
 * to `&session.todos` (M606) and which the loop's own save throttling reads via
 * the same struct. `offsetof` keeps it portable: the two anchors are stable
 * fields of one non-packed struct, and only the offset is taken, never an
 * absolute address.
 *
 * Gated to the TOP level (agent_depth == 0) and to a session that is actually
 * persisted: a delegate/subtask runs on its OWN scratch history (M853's finish
 * path, run_command_subtask's `struct jc_history sub`) with `app->todos` still
 * pointing at the PARENT's list -- container_of would derive the parent session
 * and write the child's scratch into it, corrupting the parent. depth 0 is the
 * one place `hist` and `app->todos` belong to the same session.
 *
 * Throttled by `jc_session_needs_save` (R05's recommendation, M218's shape): it
 * is 0 the round after a successful save (nothing new landed), so a stall on
 * round N+1 re-saves at most once per changed round, not per call. The durable
 * writer (D-08's fsync half) makes each such round-boundary write survive a
 * power loss; F-resources measured one 200 KB fsync'd replace at 2.00 ms here,
 * noise against a multi-second model call.
 *
 * Best-effort: a failure (full disk) is logged, not returned -- the turn keeps
 * running, and the front-end's own end-of-turn save reports the same condition
 * through jc_app_note_failed_session_save. */
static void agent_loop_midturn_persist(struct jc_app *app,
                                       struct jc_history *hist)
{
    struct jc_session *s;

    if (app == NULL || app->agent_depth != 0 || hist == NULL) {
        return;
    }
    if (app->no_session) {
        return; /* told not to persist this run */
    }
    if (app->todos == NULL) {
        return; /* no live session anchored to this run (defensive) */
    }
    /* Anchor on app->todos, the field every top-level caller sets to
     * &session.todos; the loop's own throttling already depends on it. */
    s = (struct jc_session *)((char *)app->todos -
                              offsetof(struct jc_session, todos));
    /* The caller persists mode at turn end; sync it here so the mid-turn file
     * is mode-correct and needs_save (which compares mode) does not re-fire on
     * every unchanged round for a resumed session saved under an older mode. */
    s->mode = app->mode;
    if (!jc_session_needs_save(s)) {
        return;
    }
    if (jc_session_save(s) != JC_OK) {
        jc_logf(JC_LOG_WARN,
                "mid-turn session save failed; the end-of-turn save will retry"
                " and report it");
    }
}

/* The shared agent loop. The public turn and the subagent path both run here;
 * only `opts` differs. */
static jc_status run_agent_loop(struct jc_app *app, struct jc_history *hist,
                                const struct jc_agent_callbacks *cb,
                                const struct jc_run_opts *opts)
{
    int iter;
    int snapshotted = 0; /* a pre-edit checkpoint taken this turn (top-level) */
    int self_reviewed = 0; /* the one-shot self-review pass has run this turn */
    int nudged = 0; /* M147 prose-call nudge: 0 unused, 1 fired, 2 recovered */
    int md_recovered = 0; /* M949: this turn has used the text-call
                           * recovery pass (0 unused, 1 fired) */
    int cut_continued = 0; /* M859: the one continuation after a cut reply */
    int empty_nudged = 0; /* M937 (D3): the one "give your answer" nudge after a
                           * reasoning-only (no text, no tool call, NOT cut) reply
                           * from a delegate child. 0 unused, 1 fired. A second
                           * reasoning-only reply then falls through to M167 and
                           * ends the turn -- the child is called at most twice. */
    int first_midturn = 1;
    int warned_short = 0;   /* M323: warn once per turn, not per round */
    int warned_underdecl = 0; /* M494: the under-declaration notice's OWN latch --
                               * see below; it must not share warned_short's */
    /* M900 (C1): the [context] note is a STANDING gauge -- it re-fires each
     * mid-turn round while the pass is over the high-water (mrep.pressed), not
     * once per run. The once-per-run latch (M358) gave the model a single
     * water-level reading and then silence, so the audit's complaint -- the
     * note can arrive AFTER an argument is already elided, with no standing
     * awareness -- held. It is still emitted only while pressed (not on every
     * calm round): the static pool sits in the cached prefix (M358), and the
     * dynamic percent rides the cache-stable message-tail vehicle (the M347
     * shape), so the prefix the prompt-cache depends on never churns (the
     * test_prefix_stable_across_turns guard). See the 2026-10-05 self-audit
     * CLAUDECODE C1 and docs/plans/2026-10-gap-audit-development-plan.md §2. */
    int hist_warned = 0;    /* M364: wire-shape violations warn once per run */
    /* M361: per-(sub)turn */
    struct jc_midturn_latch mlatch = { 0, 0 };
    /* M218: the first mid-turn compaction of a run
     * always runs the eager dedup (a resumed history
     * may carry pre-existing duplicate reads) */
    int round_read; /* M218: this round appended a read_file result, so a NEW
                     * superseded pair is possible -- the dedup hint */
    /* M105: per-run redo-loop guard (heap-free; no teardown needed). Tracks how
     * many times each file was edited this run so we can nudge on thrash. */
    struct jc_editwatch editwatch;
    struct jc_toolloop toolloop;   /* M432: per-turn, heap-free */
    struct jc_noprogress noprog;   /* D1 (M733): its success twin, same shape */
    struct relay_state relay_st;   /* M920: the unrelieved streak, per run */
    int relay_stop = 0;            /* M920: the core said hand over; break at
                                    * the round boundary, keep the history    */
    int noprog_stop = 0;           /* M829 (N4b): a depth>=1 child looped -- end it;
                                    * M869: which loop (app->last_run_looped's values) */
    int placeholder_run = 0;       /* M869 (N27 (b)): copied placeholders in a row */
    /* M869, amended: the same failing call since the last CHANGE -- its key's hash
     * and length, and how many times. M432's per-turn counts are never reset, so a
     * child that edits and re-runs the same failing test nine times would have been
     * stopped mid-progress; an edit, or a successful shell command (which may have
     * changed anything, as M829 assumes), starts this run again. */
    unsigned long fail_key_h = 0;
    unsigned long fail_key_n = 0;
    int fail_run = 0;
    /* M895 (N54 (b)): the silent successes of each shell command this turn, keyed by
     * M806's normalised command, so `cmd`, `cmd | tail` and `cmd 2>&1` count as one. */
    unsigned long silent_h[JC_SILENT_KEYS];
    unsigned long silent_n[JC_SILENT_KEYS];
    int silent_c[JC_SILENT_KEYS];
    int nsilent = 0;
    /* M734: which INFERRED rules this turn has already told the model it went
     * against -- once each, since the rule is in the system prompt as well. */
    unsigned char advised[JC_CONSTRAINT_MAX];
    /* M572: CONSECUTIVE refusals this turn, INDEPENDENT OF TOOL. The M570 stop
     * used jc_toolloop's counters, and both of its keys include the tool name --
     * so a model that rotates tools multiplies its budget. The operator measured
     * it: ten prompts for one rename, because edit_file, apply_patch,
     * run_terminal_command and write_file each carried their own count and only
     * edit_file reached four.
     *
     * Reset by an APPROVAL, not by a different tool: what this counts is a
     * person saying no and nothing being accepted in between. */
    int consec_deny = 0;
    /* The provider can change mid-turn when routing escalates (jc_app_switch_model
     * rebuilds app->provider); track it locally so opts stays const. */
    struct jc_provider *prov = opts->provider;
    /* M286: the system prompt is fixed for the whole turn, so estimate it once
     * here rather than per model call. The tool schemas are rebuilt each
     * iteration (the fence can differ), so they are measured inside the loop. */
    long sys_est = jc_compact_estimate_text(opts->system_msg);

    jc_editwatch_init(&editwatch);
    jc_toolloop_init(&toolloop);
    jc_noprogress_init(&noprog);
    memset(advised, 0, sizeof advised);
    app->last_run_capped = 0; /* set only on the iteration-cap exit (M62 #5) */
    app->relay_handover = 0;  /* M920: set only at the relay's round exit */
    app->relay_reason_name[0] = '\0'; /* M921: no trigger until one fires */
    /* M921 (sub-slice C): the account is per-run. The previous run's baton was
     * already rendered (and freed) by the time this run starts, so a stale
     * pointer is freed here and cleared, exactly like relay_handover. */
    free(app->relay_account);
    app->relay_account = NULL;
    app->relay_account_cut = 0;
    /* M951: the leg's measurement is per-run, like relay_handover. The
     * previous run's prompt series is freed here (the baton writer consumed
     * it at that run's handover); a relay-OFF run's is freed here too.
     * relay_leg_calls_prev is NOT reset here: main clears it before the
     * --handoff-in parse, which SETS it, and this run's E5 must use it. */
    app->relay_leg_calls = 0;
    app->relay_model_calls = 0;
    app->relay_next_leg_calls = 0;
    app->relay_measured = 0;
    jc_vec_free(&app->relay_prompt_series);
    jc_vec_init(&app->relay_prompt_series, sizeof(long));
    /* M921 (sub-slice B): a verify this run never ran must not report a
     * verdict from the previous run -- same read-right-after reset as
     * relay_handover, so the baton's "Last verify" line omits stale data. */
    app->last_verify_code = 0;
    app->last_verify_fail = -1;
    app->last_verify_failnames[0] = '\0';
    memset(&relay_st, 0, sizeof relay_st); /* M920: no streak across runs */
    app->last_run_budget_stopped = 0; /* set only on a delegate's budget stop */
    app->last_run_looped = 0;         /* M869: set only on a child's loop stop */
    app->last_fail_tool[0] = '\0';    /* M437: this run's last failing call */
    app->last_fail_msg[0] = '\0';
    app->last_fail_cls = (int)JC_FAIL_OTHER;
    app->last_fail_stopped = 0;       /* M885 */
    app->last_model_error[0] = '\0'; /* M876: this run's model failure, if any */
    /* M917 (R04-F5): a context-window refusal ended the PREVIOUS run, not this
     * one. The flag is set by the model call (stream_once), which runs after this
     * reset, so a 400 this run still sets it -- but a later run in the process
     * (or a forked parallel child, which starts its own loop) no longer inherits
     * a stale one. Before, one 400 poisoned every later child: run_child reports
     * it in the child's done line, and the merge decision read that. */
    app->ctx_overflow = 0;
    app->last_model_attempts = 0;
    memset(app->fail_cls_count, 0, sizeof app->fail_cls_count);
    for (iter = 0; iter < opts->max_iters; iter++) {
        struct jc_message *assistant;
        cJSON *tools;
        int ntools_adv = 0;   /* tools this request advertised (M255) */
        long tools_est = 0;   /* byte-estimate of the tool schemas (M286) */
        jc_status st;
        jc_size ncalls;
        jc_size k;
        jc_size assistant_index;
        const char *finish_answer = NULL; /* M853: a delegate's finish message */

        if (app->abort_flag) {
            return JC_ERR_ABORTED;
        }

        {
            jc_status bst;
            if (env_budget_should_stop(app, &bst) ||
                task_budget_should_stop(app, &bst)) {
                return bst;
            }
        }
        task_budget_note(app, hist);   /* M861 (N28) */

        /* M813 (P4b): the disk-space FENCE. Unlike the budget above it is NOT gated
         * on an envelope -- it is a fence, on always, because a filling disk
         * corrupts the very records a stop needs to leave behind. Throttled here
         * (measured at most every 10 s), but the first iteration always measures,
         * so it also guards run start. UNKNOWN never stops. A trip ends the turn
         * with the records intact; the reason is logged so the operator knows it
         * was the disk, not a Ctrl-C. Any depth: a child fills the same disk. */
        if (jc_diskguard_check(&app->diskguard,
                app->root[0] != '\0' ? app->root : app->cwd, 0,
                jc_now_millis())) {
            /* Straight to stderr, not jc_logf: a fence firing is a hard stop the
             * operator must see, and `-q` sets the log level to NONE. The records
             * (session, transcripts, any preserved trees) are already on disk. */
            fprintf(stderr,
                "[jichi] stopping: %s. Your work and records are preserved; "
                "free disk space and run again.\n", app->diskguard.why);
            app->abort_flag = 1;
            return JC_ERR_ABORTED;
        }

        /* M288: before building the request, check whether this turn has grown
         * past the fast tier's window and a roomier tier is configured. Doing it
         * here (rather than after a failure, like the other triggers) is the
         * point: the escalated model serves the call that needed the room, and
         * the 75% threshold lands before compaction's 80%, so history is kept
         * rather than summarized away. */
        if (route_on_context(app, hist, cb)) {
            prov = app->provider;
        }

        /* Tool fence: a subagent profile's `tools:` allow-list (opts->allow),
         * if any. (Skills do not fence -- their allowed-tools is advisory; see
         * docs/SKILLS.md.) */
        if (app->config.model.tool_calling == 1) {
            /* M149: the active model declares toolCalling "none" -- do not
             * advertise tools at all (a model without native tool calling
             * would either ignore them or narrate calls as prose). The run
             * degrades to a useful Q&A/plan agent: the skills catalog and
             * all prompt-side context still load. Loud, once per session. */
            tools = NULL;
            if (!app->tc_none_noticed) {
                app->tc_none_noticed = 1;
                jc_logf(JC_LOG_WARN, "model '%s' declares toolCalling: none "
                        "-- tools are not advertised; running as a Q&A/plan "
                        "agent (see docs/MODELS.md)",
                        app->config.model.name != NULL
                            ? app->config.model.name : "?");
            }
        } else {
            tools = jc_tool_build_neutral_ex(app->tools,
                                             opts->include_mutating,
                                             &app->config.permissions,
                                             opts->exclude_tool,
                                             opts->exclude_tool2,
                                             (opts->allow != NULL &&
                                              opts->allow->len > 0)
                                                 ? opts->allow
                                                 : NULL,
                                             app->agent_depth);
        }

        /* M364: the wire-shape check, at the one chokepoint every history
         * mutator funnels into -- the settled history, just before the
         * streaming placeholder joins it. ~69 mutation sites across 12 files
         * uphold this contract by hand (compaction, elision, notices,
         * inject, truncation, rewind); a violation here means one of them
         * broke pairing or boundaries, and the request built from it would
         * 400 mid-run or be silently misread. Warned once per loop (the
         * corruption persists; a bell per round buries the finding), with
         * the bounded sample lines, a telemetry event, and a journal event
         * when the run is bounded. Detection only: refusing to run would
         * turn a degraded run into a dead one. */
        if (!hist_warned) {
            struct jc_sb hcv;
            int nviol;
            jc_sb_init(&hcv);
            nviol = jc_history_check(hist, &hcv);
            if (nviol > 0) {
                hist_warned = 1;
                jc_logf(JC_LOG_WARN,
                        "[history] wire-shape check: %d violation(s) -- a "
                        "history mutator broke the contract; the request "
                        "may be rejected or misread:\n%s",
                        nviol, hcv.data != NULL ? hcv.data : "");
                {
                    cJSON *o = telem(app, "history_check");
                    if (o != NULL) {
                        cJSON_AddNumberToObject(o, "violations",
                                                (double)nviol);
                        if (hcv.data != NULL) {
                            cJSON_AddStringToObject(o, "first", hcv.data);
                        }
                    }
                    jc_eventlog_end(app->telemetry, o);
                }
                if (env_active(app)) {
                    cJSON *jo = jc_env_journal_begin(app->env,
                                                     "history_check");
                    if (jo != NULL) {
                        cJSON_AddNumberToObject(jo, "violations",
                                                (double)nviol);
                    }
                    jc_env_journal_end(app->env, jo);
                }
            }
            jc_sb_free(&hcv);
        }

        /* M438: serve the control channel BEFORE the request, not only after the
         * tool round. Until now the only service point was at the end of a round,
         * so a supervisor that connected while the first model call was in flight
         * got no answer to `status` until that whole round finished -- against a
         * client wait hard-coded to 300s, which is how a live run reads as a dead
         * one. This is a clean history point: every turn above is complete and the
         * placeholder is not yet appended, so a queued inject folds in here and
         * reaches the request about to be built rather than the one after it. */
        if (jc_control_boundary(app, hist) &&
            cb != NULL && cb->on_status != NULL) {
            cb->on_status(cb->user, "operator steering injected");
        }

        /* Append the assistant message we will stream into. Its index is
         * stable; we re-fetch by index after the stream (tool-result pushes
         * may realloc the message vector). */
        assistant_index = jc_history_len(hist);
        assistant = jc_history_add(hist, JC_ROLE_ASSISTANT, NULL);
        if (assistant == NULL) {
            if (tools) cJSON_Delete(tools);
            return JC_ERR_OOM;
        }

        if (cb != NULL && cb->on_message_begin != NULL) {
            cb->on_message_begin(cb->user);
        }

        /* M286: measure the tool schemas once per model call. This serializes
         * the tool array, which the M192 telemetry path used to do inside its
         * `telem()` guard -- so with telemetry on the cost is unchanged, and
         * with it off we now pay one JSON print per model call (negligible
         * against a multi-second network round-trip) in exchange for a
         * calibration basis that is measured rather than assumed. */
        if (tools != NULL) {
            char *ts = jc_json_print(tools);
            tools_est = jc_compact_estimate_text(ts);
            free(ts);
        }
        /* Publish the non-history size for the compaction triggers, which run
         * outside this function and have neither the system prompt nor the tool
         * array in scope. */
        app->last_nonhist_est = sys_est + tools_est;

        st = stream_once(app, hist, opts->system_msg, tools, assistant, cb,
                         prov, sys_est, tools_est);
        /* Re-sync prov after stream_once in case it was re-routed (M1038). */
        prov = app->provider;
        /* M334: carry the provider's truncation verdict to the tool layer,
         * which is where a cut-off tool call is about to be misdiagnosed. */
        app->last_response_truncated = (assistant != NULL) ? assistant->truncated
                                                           : 0;
        /* A-07 / M1090: carry the CAUSE of that verdict to the tool layer
         * alongside it. 0 means the model's output-token limit (M334); 1 means
         * jichi's own per-stream accumulation cap clipped the reply. */
        app->last_response_truncated_by_cap =
            (assistant != NULL) ? assistant->truncated_by_cap : 0;
        if (tools != NULL) {
            /* M255: keep the COUNT, drop the pointer. Two diagnostics later in
             * this iteration -- the M147 prose-nudge gate and the M167
             * empty-answer warning -- ask what the request advertised, and the
             * latter read the count out of this object AFTER it was freed. A
             * use-after-free that crashed the run precisely when the
             * diagnostic was needed. Nulling the pointer makes the stale
             * dereference unrepresentable rather than merely fixed. */
            ntools_adv = cJSON_GetArraySize(tools);
            cJSON_Delete(tools);
            tools = NULL;
        }

        if (cb != NULL && cb->on_message_end != NULL) {
            cb->on_message_end(cb->user);
        }

        if (st != JC_OK) {
            /* M23a: a stall on the fast tier means "this model can't serve this
             * turn" -- escalate to the strong tier and retry rather than
             * failing. route_escalate is depth-/reachability-guarded and
             * one-shot per turn (a second stall, now on strong, escalates no
             * further and returns the error), so this stays bounded. Drop the
             * incomplete assistant message first so the retried turn is
             * well-formed. */
            if (st == JC_ERR_TIMEOUT && app->agent_depth == 0 &&
                app->config.routing.escalate_on_stall &&
                route_escalate(app, "stall")) {
                jc_history_truncate(hist, assistant_index);
                prov = app->provider;
                if (cb != NULL && cb->on_status != NULL) {
                    char banner[160];
                    jc_snprintf(banner, sizeof(banner),
                                "escalating to %s after stall",
                                app->config.model.name != NULL
                                    ? app->config.model.name
                                    : (app->config.model.model != NULL
                                       ? app->config.model.model : "strong"));
                    cb->on_status(cb->user, banner);
                }
                continue;
            }
            return st;
        }

        /* Re-fetch: the pointer above is still valid (no pushes during the
         * stream), but be explicit for clarity. */
        assistant = jc_history_get(hist, assistant_index);
        ncalls = jc_msg_tool_call_count(assistant);
        /* M147: the nudge below worked -- the retry produced a native call. */
        if (nudged == 1 && ncalls > 0) {
            nudged = 2;
            jc_logf(JC_LOG_INFO, "[nudge] recovered: native tool call emitted");
            {
                cJSON *o = telem(app, "nudge");
                if (o != NULL) {
                    cJSON_AddStringToObject(o, "phase", "recovered");
                }
                jc_eventlog_end(app->telemetry, o);
            }
        }
        /* M937 (D3): the "give your answer" nudge below worked -- the reasoning-only
         * child's retry now carries answer text. This is the cause that was asked
         * again (the honesty rule's third arm), kept separate from M859's cut and
         * M147's prose-call recovery. */
        if (empty_nudged == 1 && ncalls == 0 &&
            assistant->content != NULL && assistant->content[0] != '\0') {
            empty_nudged = 2;
            jc_logf(JC_LOG_INFO,
                "[nudge] the reasoning-only reply was asked again; it now "
                "carries an answer");
            {
                cJSON *o = telem(app, "empty_reasoning");
                if (o != NULL) {
                    cJSON_AddStringToObject(o, "phase", "recovered");
                }
                jc_eventlog_end(app->telemetry, o);
            }
        }
        /* M949 (def-325): for a model whose textToolCalls flag is set, a
         * well-formed call written as markdown is EXECUTED, not nudged --
         * the nudge below asks for a native emission such a model cannot
         * make (it would just re-emit markdown). The recovered calls are
         * synthetic entries on this assistant message, so the round below
         * runs them through the unchanged executor: the same budget meter,
         * approval preview, path fence and telemetry as a native call.
         * "A recovered call is a call." Extraction failures are recorded,
         * not fatal (one bad block does not poison the others). Guarded on
         * ncalls == 0: a reply with a native call must not have an example
         * block in its prose executed a second time. */
        if (ncalls == 0 && app->config.model.text_tool_calls &&
            md_recovered == 0 && ntools_adv > 0 &&
            assistant->content != NULL) {
            struct jc_vec rc;
            struct jc_sb rsn;
            int nrec;
            jc_vec_init(&rc, sizeof(struct jc_tool_call));
            jc_sb_init(&rsn);
            nrec = jc_toolcall_recover(assistant->content, app->tools,
                                       &rc, &rsn);
            if (nrec > 0) {
                md_recovered = 1;
                assistant = jc_history_get(hist, assistant_index);
                for (k = 0; k < rc.len; k++) {
                    struct jc_tool_call *rct =
                        (struct jc_tool_call *)jc_vec_at(&rc, k);
                    (void)jc_msg_add_tool_call(assistant, rct->id,
                                               rct->name,
                                               rct->arguments_json);
                }
                ncalls = jc_msg_tool_call_count(assistant);
                jc_logf(JC_LOG_INFO,
                        "recovered %d tool call(s) from the reply's text "
                        "(textToolCalls)", nrec);
                {
                    cJSON *o = telem(app, "md_tool_call_recovered");
                    if (o != NULL) {
                        cJSON_AddNumberToObject(o, "count", nrec);
                        if (rsn.data != NULL && rsn.len > 0) {
                            cJSON_AddStringToObject(o, "failed", rsn.data);
                        }
                    }
                    jc_eventlog_end(app->telemetry, o);
                }
            }
            jc_vec_free(&rc);
            jc_sb_free(&rsn);
        }
        if (ncalls == 0) {
            /* M859 (N42 (d), the operator's choice of 2026-10-02): a reply cut
             * off at the output ceiling with no tool call is not an answer the
             * model chose to give -- run 0090's worker ended mid-sentence after 331
             * calls. Ask once to continue (cut_continue_allowed says who). A
             * second cut ends the turn, and a parallel write child then gets
             * M858's note: its work merges only after a green verify. */
            if (assistant->truncated && !cut_continued &&
                cut_continue_allowed(app)) {
                char nudge[256];
                cut_continued = 1;
                /* A-07 / M1090: name the cause in the nudge. The two
                 * truncation causes look identical to the model (a reply that
                 * stopped short), but the fix differs: the model's own
                 * output-token ceiling wants "keep each reply short"; jichi's
                 * own per-reply accumulation cap wants the same advice framed
                 * against the cap, not a token limit that never fired. */
                if (assistant->truncated_by_cap) {
                    jc_snprintf(nudge, sizeof(nudge),
                        "[jichi] Your last reply was cut at jichi's per-reply "
                        "size limit, part-way through. Continue from where it "
                        "stopped, and keep each reply short: if the work is "
                        "done, give your final answer in a few lines.");
                    jc_logf(JC_LOG_INFO,
                            "[cut] the reply hit jichi's per-reply size cap; "
                            "asking the model to continue, once");
                } else {
                    jc_snprintf(nudge, sizeof(nudge),
                        "[jichi] Your last reply was cut off at the output "
                        "ceiling, part-way through. Continue from where it "
                        "stopped, and keep each reply short: if the work is "
                        "done, give your final answer in a few lines.");
                    jc_logf(JC_LOG_INFO,
                            "[cut] the reply hit the output ceiling; "
                            "asking the model to continue, once");
                }
                jc_history_add(hist, JC_ROLE_USER, nudge);
                {
                    cJSON *o = telem(app, "cut_continue");
                    jc_eventlog_end(app->telemetry, o);
                }
                prov = app->provider;
                continue;
            }
            /* M937 (D3, the zigodot drive; widened by M965): a reply that ended
             * on its own with reasoning and NO answer text, and was NOT cut at
             * the ceiling, is not an answer the model chose to give either --
             * the drive's reviewer did this three times and the run accepted an
             * empty review, owing the next phase a review it never got. A
             * delegate is asked once for the final answer. The honesty rule
             * keeps the three arms distinct: ceiling hit (M859), ended-on-its-own
             * empty answer (M937), and the one that was asked again (recovered
             * above). The scope was widened from delegates only (agent_depth >= 1)
             * to every delegate AND a headless top-level run:
             * (agent_depth >= 1 || (agent_depth == 0 && headless_run)). An
             * interactive top-level run is left alone (a person can say
             * "continue"). The nudge text is deliberately general (no "reasoning"
             * word): (c), the later wording refinement, only swaps the sentence
             * when saw_reasoning is known, and stays structurally unchanged. */
            if ((app->agent_depth >= 1 ||
                 (app->agent_depth == 0 && app->headless_run)) &&
                empty_nudged == 0 &&
                !assistant->truncated && assistant->saw_reasoning &&
                (assistant->content == NULL ||
                 assistant->content[0] == '\0')) {
                empty_nudged = 1;
                jc_history_add(hist, JC_ROLE_USER,
                    "[jichi] You ended your last reply with reasoning but gave "
                    "no final answer. Do not restate your reasoning. Give your "
                    "final answer in a few lines now.");
                jc_logf(JC_LOG_INFO,
                    "[nudge] the reply ended on reasoning with no answer; "
                    "asking once for the final answer");
                {
                    cJSON *o = telem(app, "empty_reasoning");
                    if (o != NULL) {
                        cJSON_AddStringToObject(o, "phase", "fired");
                    }
                    jc_eventlog_end(app->telemetry, o);
                }
                prov = app->provider;
                continue;
            }
            /* M147: a tool call written as prose (a fenced JSON block, a bare
             * name+args object, an XML-ish tag) instead of invoked natively --
             * the classic small-model failure -- must not be silently accepted
             * as a final answer. One corrective nudge per turn, top-level
             * only; the scan is high-precision (the name must resolve in the
             * live registry), so a genuine final answer is unaffected. */
            if (app->agent_depth == 0 && ntools_adv > 0 && nudged == 0 &&
                !app->config.model.text_tool_calls &&
                assistant->content != NULL) {
                char tname[64];
                if (jc_toolcall_scan(assistant->content, app->tools, tname,
                                     sizeof(tname))) {
                    struct jc_sb msg;
                    nudged = 1;
                    jc_sb_init(&msg);
                    jc_sb_append_fmt(&msg,
                        "You described calling `%s` but did not invoke it. "
                        "Emit the tool call natively -- do not write it as "
                        "text. Do not repeat your previous answer.", tname);
                    jc_history_add(hist, JC_ROLE_USER,
                                   msg.data != NULL ? msg.data : "");
                    jc_sb_free(&msg);
                    jc_logf(JC_LOG_INFO, "[nudge] prose tool call detected "
                            "(%s); asking for a native invocation", tname);
                    {
                        cJSON *o = telem(app, "nudge");
                        if (o != NULL) {
                            cJSON_AddStringToObject(o, "phase", "fired");
                            cJSON_AddStringToObject(o, "tool", tname);
                        }
                        jc_eventlog_end(app->telemetry, o);
                    }
                    prov = app->provider;
                    continue;
                }
            }
            /* M147: the nudge did NOT recover -- the retry still made no
             * call. Accept the answer, but say so once per session: this is
             * the signature of a model without native tool calling. */
            if (nudged == 1 && !app->noop_warned) {
                app->noop_warned = 1;
                /* M167: name BOTH causes. Saying only "the model may lack
                 * native tool-call support" sent a bench session chasing the
                 * model for a day when the real cause was our own malformed
                 * request (M166) -- and acting on that advice would have
                 * degraded a fully capable model to toolCalling:none. */
                jc_logf(JC_LOG_WARN, "model narrated a tool call but never "
                        "invoked one natively -- either the request is "
                        "malformed or the model lacks native tool-call support; "
                        "check in that order (see docs/LOCAL_MODELS.md, \"When "
                        "the model calls no tool at all\")");
            }
            /* M167: tools were advertised, the model called none, and it
             * returned no text either -- the turn accomplished nothing. That
             * triple is the signature of a malformed request (M166 produced it
             * silently, six runs in a row, while every other diagnostic stayed
             * quiet: the M147 nudge needs narrated text to detect, and the
             * reasoning hint needs reasoning). Never a normal agentic outcome,
             * so say so once per session, at any depth. */
            if (ntools_adv > 0 && !app->empty_warned &&
                (assistant->content == NULL || assistant->content[0] == '\0')) {
                /* D2 (zigodot drive 2026-10-11): order the causes before
                 * asserting one. A reply cut at the output ceiling produces
                 * exactly this shape -- no tool call, no text -- so saying
                 * "the request was rejected or misread" for it names a cause
                 * that did not happen; the ceiling warning already fired at
                 * the source. Only fall through to the M167 text when the
                 * previous reply was NOT cut. The M167 triple is still the
                 * signature of a malformed request (M166: six silent runs),
                 * and that text stays for the case it was written for. */
                app->empty_warned = 1;
                if (app->last_response_truncated) {
                    jc_logf(JC_LOG_WARN, "the model returned no tool call and "
                            "no text, and its previous reply was cut at the "
                            "output ceiling -- the cut is the cause, not a "
                            "misread request; see the ceiling warning above "
                            "(maxTokens, continueCutReply)");
                } else {
                    jc_logf(JC_LOG_WARN, "the model returned no tool call and "
                            "no text while %d tools were advertised -- this "
                            "usually means the request was rejected or "
                            "misread, not that the model is idle; capture and "
                            "replay the request (see docs/LOCAL_MODELS.md, "
                            "\"When the model calls no tool at all\")",
                            ntools_adv);
                }
            }
            /* Self-review: before finishing a top-level turn that changed files,
             * show the agent its own diff once and let it fix problems. Runs
             * before the verify gate so an --auto run reviews, then verifies. */
            if (app->agent_depth == 0 && !self_reviewed && snapshotted &&
                self_review_on(app) && jc_snapshot_available(app->snapshots)) {
                struct jc_sb diff;
                self_reviewed = 1;
                jc_sb_init(&diff);
                if (jc_snapshot_diff(app->snapshots, 1, &diff) == JC_OK &&
                    diff.data != NULL && diff.data[0] != '\0') {
                    struct jc_sb msg;
                    const char *d;
                    jc_size off = 0;
                    if (diff.len > 8192) off = diff.len - 8192;
                    d = diff.data + off;
                    jc_sb_init(&msg);
                    jc_sb_append(&msg,
                        "Before finishing, review the changes you just made for "
                        "bugs, regressions, leftover debug code, and whether "
                        "they fully address the request. Diff of your changes "
                        "this turn:\n\n");
                    if (off > 0) jc_sb_append(&msg, "[... earlier hunks omitted]\n");
                    jc_sb_append(&msg, d);
                    jc_sb_append(&msg,
                        "\n\nIf you find problems, fix them with your tools. "
                        "If the changes are correct and complete, briefly "
                        "confirm and stop.");
                    jc_history_add(hist, JC_ROLE_USER,
                                   msg.data != NULL ? msg.data : "");
                    jc_sb_free(&msg);
                    jc_sb_free(&diff);
                    jc_logf(JC_LOG_INFO,
                            "[self-review] reviewing this turn's changes");
                    if (env_active(app)) {
                        cJSON *o = jc_env_journal_begin(app->env, "self_review");
                        jc_env_journal_end(app->env, o);
                    }
                    prov = app->provider;
                    continue;
                }
                jc_sb_free(&diff);
            }

            /* Final answer. Under an envelope with a verifier, gate the result:
             * run the verifier, advance the green checkpoint on success, or
             * fix-forward / roll back on failure.
             *
             * M1048 (G18): this used to also require `snapshotted` -- a pre-edit
             * checkpoint taken when a mutating tool ran. A run that takes none
             * (`snapshots:false`, or a run that wrote nothing) therefore never
             * ran its verifier and still finished as a clean run, and the
             * footer claimed green from "armed and outcome ok". A snapshot is
             * needed to ROLL BACK, not to CHECK: un-gate. `snapshotted` was set
             * at depth 0 only, so the depth must be made explicit here. */
            if (env_active(app) && app->env->verify_cmd != NULL &&
                app->agent_depth == 0) {
                struct jc_sb vout;
                enum jc_verify_consistency vcons;
                struct jc_test_report rep;
                int code;
                int told = 0;   /* M431d: the hollow-green note just reached the model */
                int predates = 0;   /* M893: the same line failed before any change */
                cJSON *o;

                /* M892 (the demos drive's N52 (a)): the last fix-forward message
                 * offered a way out -- "if your changes did not cause this, say
                 * so and make no further edits: the run then ends on this
                 * verdict". An answer after which nothing changed takes it. The
                 * tree the check failed on is the tree it would run on again, so
                 * the run ends on that verdict and spends no retry re-asking.
                 * "Changed" is the worktree's content where git can fingerprint
                 * it (a shell command that only reads is not a change; the M886
                 * rule), and otherwise any executed call that is not a read. */
                if (app->env->ff_armed) {
                    int unchanged;
                    int by_content = 0;
                    char fp[72];

                    app->env->ff_armed = 0;
                    if (app->env->ff_fp_ok &&
                        jc_snapshot_worktree_fingerprint(app->root, fp, sizeof fp)
                            == JC_OK) {
                        by_content = 1;
                        unchanged = (strcmp(fp, app->env->ff_fp) == 0);
                    } else {
                        unchanged = (app->env->changing_executed ==
                                     app->env->ff_changing_mark);
                    }
                    if (unchanged) {
                        o = jc_env_journal_begin(app->env, "verify_declined");
                        if (o != NULL) {
                            cJSON_AddNumberToObject(o, "retries_left",
                                                    (double)app->env->retries_left);
                            cJSON_AddStringToObject(o, "by",
                                                    by_content ? "content" : "calls");
                        }
                        jc_env_journal_end(app->env, o);
                        jc_logf(JC_LOG_WARN,
                                "envelope: nothing changed after the failed "
                                "end-of-run check -- the run ends on that verdict "
                                "(%d retr%s left unspent)",
                                app->env->retries_left,
                                app->env->retries_left == 1 ? "y" : "ies");
                        if (cb != NULL && cb->on_status != NULL) {
                            cb->on_status(cb->user,
                                "verify: no change after the failed check; "
                                "ending on its verdict");
                        }
                        app->env->outcome = JC_ENV_VERIFY_FAILED;
                        return env_rollback_and_finish(app);
                    }
                }

                jc_sb_init(&vout);
                /* M1048 (G18): the run-fact, set the moment the check is
                 * CALLED -- whatever it returns, and before its code is known.
                 * Deliberately NOT set above in the ff_armed/unchanged path:
                 * that path ends on a PRIOR verdict, so this run did not just
                 * verify. */
                app->env->verifier_ran = 1;
                code = jc_env_run_verify(app->env->verify_cmd, app->cwd, &vout,
                                         &app->abort_flag,
                                         app->env->verify_timeout);
                jc_test_report_init(&rep);
                jc_testparse(vout.data, &rep);
                app_record_last_verify(app, code, &rep);  /* M921: for the baton */
                /* M893 (N52 (b)): the first failing line, compared with the one
                 * recorded before the run's first change. Once per run, so a run
                 * cannot spend its turns on free rounds. */
                if (code != 0 && !app->env->predates_spent &&
                    app->env->pre_edit_fail[0] != '\0') {
                    char eline[JC_ENV_SIG_MAX];
                    if (jc_env_fail_line(vout.data, eline, sizeof eline) &&
                        strcmp(eline, app->env->pre_edit_fail) == 0) {
                        predates = 1;
                    }
                }
                o = jc_env_journal_begin(app->env, "verify");
                if (o != NULL) {
                    cJSON_AddNumberToObject(o, "exit", (double)code);
                    cJSON_AddNumberToObject(o, "retries_left",
                                            (double)app->env->retries_left);
                    if (predates) {
                        cJSON_AddBoolToObject(o, "predates", 1);
                    }
                    if (rep.failed >= 0) {
                        cJSON_AddNumberToObject(o, "failed", (double)rep.failed);
                    }
                    if (rep.passed >= 0) {
                        cJSON_AddNumberToObject(o, "passed", (double)rep.passed);
                    }
                }
                /* M86: on green, sanity-check the gate for hollowness (adds
                 * tests/sanity to `o` before it is flushed). M331: on red, check
                 * the verdict against its own evidence. */
                vcons = JC_VERIFY_AGREES;
                if (code == 0) {
                    /* M431d: hist, not NULL. M351 passed NULL here because a
                     * completion green ends the run, so there was no next model
                     * call to read the note -- which meant that WITHOUT
                     * --verify-every a hollow green was invisible to the model in
                     * every run, and it banked "tests pass" as fact. The fix is to
                     * make a next call exist: see the `told` branch below. */
                    int before = app->env->sanity_noticed;
                    env_verify_sanity_check(app, cb, &rep, o, hist);
                    told = (!before && app->env->sanity_noticed);
                } else {
                    vcons = env_verify_consistency_check(app, cb, &rep, code, o);
                }
                jc_env_journal_end(app->env, o);

                /* M431d: the gate passed, and it looks hollow, and the model has
                 * just been told so. Give it ONE more round -- the note asks it to
                 * fix the gate or else say plainly in its final answer that the
                 * result is unverified, and neither is possible without a turn to
                 * do it in. `sanity_noticed` is the latch, so this cannot loop.
                 *
                 * Deliberately BEFORE banking the green checkpoint, and the gate
                 * re-runs on the next pass: the model may edit files in this round
                 * (fixing the gate is the outcome we want most), so a checkpoint
                 * taken now could describe a tree that no longer exists, and
                 * skipping the re-verify would let that round go ungated. The cost
                 * is one model call plus one verifier run, only when the finding
                 * fires -- which is the trade the maintainer approved. */
                if (code == 0 && told) {
                    jc_logf(JC_LOG_WARN,
                            "envelope: the gate passed but looks hollow -- giving "
                            "the model one more round to fix it or say so");
                    if (cb != NULL && cb->on_status != NULL) {
                        cb->on_status(cb->user,
                                      "hollow green: one more round to answer for it");
                    }
                    jc_test_report_free(&rep);
                    jc_sb_free(&vout);
                    prov = app->provider;
                    continue;
                }

                if (code == 0) {
                    /* Passed: take and remember a new green checkpoint. */
                    char glabel[300];
                    jc_snprintf(glabel, sizeof(glabel), "green: %s",
                                opts->checkpoint_label != NULL
                                    ? opts->checkpoint_label : "verified");
                    if (jc_snapshot_available(app->snapshots)) {
                        const char *gc;
                        jc_snapshot_take(app->snapshots, glabel);
                        gc = jc_snapshot_commit(app->snapshots,
                                                jc_snapshot_count(app->snapshots)
                                                    - 1);
                        if (gc != NULL) {
                            jc_snprintf(app->env->green_commit,
                                        sizeof(app->env->green_commit),
                                        "%s", gc);
                            app->env->green_verified = 1; /* M207: observed */
                            o = jc_env_journal_begin(app->env, "checkpoint");
                            if (o != NULL) {
                                cJSON_AddStringToObject(o, "commit", gc);
                                cJSON_AddBoolToObject(o, "green", 1);
                            }
                            jc_env_journal_end(app->env, o);
                        }
                    }
                    app->env->outcome = JC_ENV_OK;
                    jc_test_report_free(&rep);
                    jc_sb_free(&vout);
                    return JC_OK;
                }

                if (app->env->retries_left > 0) {
                    /* Fix-forward: feed the failure back and let the agent
                     * try again (preserving its progress). */
                    struct jc_sb msg;
                    const char *tail;
                    int structured;
                    jc_size off = 0;

                    /* M893 (N52 (b)): a failure the run saw before its first
                     * change is not a failure of its work, so asking about it
                     * spends no retry. */
                    if (predates) {
                        app->env->predates_spent = 1;
                    } else {
                        app->env->retries_left--;
                    }
                    /* M892: remember the tree this message is about, so that the
                     * next answer can be told apart from a fix (above). */
                    app->env->ff_armed = 1;
                    app->env->ff_changing_mark = app->env->changing_executed;
                    app->env->ff_fp_ok =
                        (jc_snapshot_worktree_fingerprint(app->root,
                            app->env->ff_fp, sizeof app->env->ff_fp) == JC_OK);
                    structured = (rep.failed > 0 || rep.failures.len > 0);
                    jc_sb_init(&msg);
                    /* M892 (N52 (a)): this arrives as a user-role message, and a
                     * bare one is read as the operator's order -- run 0105's lead
                     * wrote "The operator now asks me to make verification pass"
                     * and spent 33 calls porting the project to a toolchain the
                     * operator had decided against. So the message names its
                     * sender, and ends with a way out. */
                    if (code == JC_VERIFY_TIMEOUT) {
                        jc_sb_append_fmt(&msg,
                            "[jichi] jichi's end-of-run check timed out: the verify "
                            "command `%s` ran for %lds and was killed.\n",
                            app->env->verify_cmd, app->env->verify_timeout);
                    } else {
                        jc_sb_append_fmt(&msg,
                            "[jichi] jichi's end-of-run check failed: the verify "
                            "command `%s` exited %d.\n",
                            app->env->verify_cmd, code);
                    }
                    jc_sb_append(&msg,
                        "This message comes from jichi, not from the operator: it is "
                        "the check configured for this run, which jichi runs after a "
                        "final answer that changed files.\n");
                    if (predates) {
                        jc_sb_append_fmt(&msg,
                            "This failure predates your changes: %s failed on the "
                            "same line:\n    %s\n", app->env->pre_edit_from,
                            app->env->pre_edit_fail);
                        jc_sb_append(&msg,
                            "The environment or the starting tree is the likely "
                            "cause, not your work, so this round spends none of the "
                            "run's retries.\n");
                    }
                    /* M331: put the inconsistency FIRST, ahead of the parsed
                     * failures. A model handed "exit 1" plus a list of zero
                     * failures reads the exit code and starts editing code; this
                     * sentence is the only thing that redirects it at the
                     * harness, and it has to arrive before the evidence it
                     * reframes. */
                    verify_consistency_note(vcons, &rep, app->env, &msg);
                    if (structured) {
                        /* Lead with the parsed failures, then a short raw tail
                         * for context (the signal is in the summary). */
                        jc_testparse_render(&rep, &msg);
                        if (vout.data != NULL && vout.len > 2048) {
                            off = vout.len - 2048;
                        }
                        tail = (vout.data != NULL) ? vout.data + off : "";
                        jc_sb_append(&msg, "\n--- output");
                        jc_sb_append(&msg, off > 0 ? " (tail) ---\n" : " ---\n");
                        jc_sb_append(&msg, tail);
                    } else {
                        /* No structure parsed: fall back to the raw tail. */
                        if (vout.data != NULL && vout.len > 4096) {
                            off = vout.len - 4096;
                        }
                        tail = (vout.data != NULL) ? vout.data + off : "";
                        jc_sb_append(&msg, "Output:\n");
                        jc_sb_append(&msg, tail);
                    }
                    {
                        int repeat = jc_env_note_failure(app->env, vout.data);
                        if (repeat >= 2) {
                            cJSON *js;
                            jc_sb_append_fmt(&msg,
                                "\n\nNOTE: this is the SAME error as your "
                                "previous attempt (%dx in a row). The current "
                                "approach is not working -- try a DIFFERENT fix "
                                "(e.g. verify the exact API/signature), not a "
                                "variation of the same one.", repeat);
                            jc_logf(JC_LOG_WARN, "envelope: verify stuck on the "
                                    "same error (%dx): %s", repeat,
                                    app->env->last_fail_sig);
                            js = jc_env_journal_begin(app->env, "verify_stuck");
                            if (js != NULL) {
                                cJSON_AddNumberToObject(js, "repeat",
                                                        (double)repeat);
                                cJSON_AddStringToObject(js, "sig",
                                                        app->env->last_fail_sig);
                            }
                            jc_env_journal_end(app->env, js);
                            if (cb != NULL && cb->on_status != NULL) {
                                cb->on_status(cb->user,
                                    "verify: stuck on the same error");
                            }
                        }
                    }
                    jc_sb_append(&msg,
                        "\nFix the code so the check passes. If your changes did not "
                        "cause this failure -- a broken tool or environment, or a "
                        "tree that was already failing -- say so in your answer and "
                        "make no further edits: the run then ends on this verdict.");
                    jc_history_add(hist, JC_ROLE_USER,
                                   msg.data != NULL ? msg.data : "");
                    jc_sb_free(&msg);
                    jc_test_report_free(&rep);
                    jc_sb_free(&vout);
                    /* The fast tier couldn't pass; escalate for the retry. */
                    if (app->config.routing.escalate_on_verify &&
                        route_escalate(app, "verify_fail")) {
                        prov = app->provider;
                    }
                    continue;
                }

                /* Out of retries: roll back to the green baseline. */
                app->env->outcome = JC_ENV_VERIFY_FAILED;
                jc_test_report_free(&rep);
                jc_sb_free(&vout);
                return env_rollback_and_finish(app);
            }
            return JC_OK; /* model produced a final answer */
        }

        /* Execute each requested tool call and append its result. */
        round_read = 0;
        for (k = 0; k < ncalls; k++) {
            struct jc_tool_call *tc;
            struct jc_tool_result res;
            const struct jc_tool *tool;
            const char *call_id;
            char *name_copy;
            const char *gate_name;   /* M532: canonical, for DECISIONS */
            char *args_copy;
            char *id_copy;
            enum jc_approval verdict;
            const char *perm_why = NULL;   /* M762: the rule the verdict came from */
            const char *allow_why = NULL;  /* M762: what let this call run */
            unsigned long why_seq = 0;     /* M762: this call's record, for its outcome */
            int advise_rule = -1;    /* M734: an inferred rule this call breaks */
            char advise_text[256];
            double tool_dur_ms = 0.0;    /* M806: wall-time, for the R6 at-bound test */
            int tool_at_bound = 0;       /* M806: ran to its own timeout and cut off */
            char tool_normcmd[JC_TOOLLOOP_KEY_MAX]; /* M806: normalised shell key    */
            int pin_rc = 0;              /* M863: kept before a discarding command */
            char pin_ref[256];           /* M863: ...under this ref                */
            char fp_before[72];          /* M886: the worktree before a shell call */
            int fp_ok = 0;               /* M886: ...taken, during a failing run   */

            pin_ref[0] = '\0';
            fp_before[0] = '\0';
            advise_text[0] = '\0';
            tool_normcmd[0] = '\0';
            /* Drain/reap background processes opportunistically (M26). */
            if (app->bg != NULL) {
                jc_bg_poll(app->bg);
            }

            {
                jc_status bst;
                int env_stop, task_stop;
                env_stop = env_budget_should_stop(app, &bst);
                task_stop = env_stop ? 0 : task_budget_should_stop(app, &bst);
                if (env_stop || task_stop) {
                    /* M919 (R01-F1): the budget tripped MID-ROUND, so this
                     * assistant message still carries calls k..ncalls-1 that
                     * will never run. The wire invariant (jc_message.c) is
                     * that every tool call is answered exactly once -- a call
                     * with no result makes the SAVED session resume into a
                     * request the provider rejects with HTTP 400 on its first
                     * model call (exp1 measured it). Answer each remaining
                     * call with an ERROR result -- the tool was asked for and
                     * refused to run, so an error is the truth and a success
                     * would be a lie -- naming what stopped it, then return.
                     * The round-top budget check (before the model call)
                     * already exits with nothing in flight, so only this
                     * mid-round path can strand a call. */
                    enum jc_env_budget bb;
                    const char *kind;
                    struct jc_message *am;
                    jc_size r;
                    if (env_stop && app->env != NULL) {
                        bb = jc_env_over_budget(app->env, (long)time(NULL));
                        kind = (bb != JC_BUDGET_NONE) ? jc_env_budget_name(bb)
                                                       : "budget";
                    } else {
                        kind = "task";
                    }
                    for (r = k; r < ncalls; r++) {
                        struct jc_tool_call *stranded;
                        char reason[144];
                        am = jc_history_get(hist, assistant_index);
                        stranded = (am != NULL) ? jc_msg_tool_call_at(am, r)
                                                : NULL;
                        if (stranded == NULL) {
                            continue;
                        }
                        jc_snprintf(reason, sizeof reason,
                            "not run: this run's %s budget stopped before this "
                            "call; answered as an error so the history stays "
                            "wire-valid for resume", kind);
                        jc_history_add_tool_result(
                            hist,
                            stranded->id != NULL ? stranded->id : "",
                            reason, 1);
                    }
                    return bst;
                }
            }

            assistant = jc_history_get(hist, assistant_index);
            tc = jc_msg_tool_call_at(assistant, k);

            /* Copy the call fields: appending the tool result below may
             * realloc the message vector and invalidate `tc`. The copies are
             * MALLOC-OWNED by this iteration and freed at `next_call` (M218):
             * they are consumed within the iteration (callbacks, history and
             * telemetry all take their own copies). The session arena leaked
             * ~3 strings per call for the life of the process (fixed M180);
             * the per-turn scratch M180 moved them to still accumulated
             * name+id+FULL ARGS x hundreds of calls for the whole of a
             * marathon turn (~190 KB measured by tests/smoke/turn_scratch.sh
             * at 200 x 900 B) -- and tool_scratch is off-limits here because
             * a spawn_* call runs a nested agent loop that resets it while
             * these are still live (the include/jc_app.h invariant). Every
             * early-skip path below must `goto next_call`, never `continue`. */
            name_copy = jc_strdup(tc->name ? tc->name : "");
            args_copy = jc_strdup(tc->arguments_json ? tc->arguments_json
                                                     : "{}");
            id_copy = jc_strdup(tc->id ? tc->id : "");
            /* M532: EVERY GATE BELOW DECIDES ON THIS, not on name_copy.
             *
             * `jc_tool_execute` resolves aliases before running anything
             * (jc_tool_find matches the canonical name), so a model calling
             * `create_file` runs `write_file` -- while every fence here compared
             * the RAW wire name and missed it. Reproduced: with
             * `permissions.deny: ["write_file"]`, a `create_file` call wrote the
             * file and reported ok. The same bypass reached --edit-scope,
             * --strict-scope, an enforced deny-cmd constraint, and
             * `privilegedCommands: deny` -- where the gate never fired, so
             * jc_audit_privileged wrote no row and the audit trail showed
             * nothing at all.
             *
             * This is not adversarial input: the alias table exists BECAUSE
             * models emit these names unprompted (jc_tool.c: "what a model
             * guessing these names sends"), and in the deny case the canonical
             * tool is un-advertised, which is exactly the condition that makes
             * a model guess.
             *
             * name_copy stays the raw name everywhere the MODEL or the OPERATOR
             * is shown what was called -- history, telemetry, refusal messages,
             * the journal. A gate must decide on what will run; a message must
             * say what was asked. */
            gate_name = jc_tool_canonical_name(name_copy != NULL ? name_copy : "");
            if (name_copy == NULL || args_copy == NULL || id_copy == NULL) {
                /* Strictly better than the pre-M218 behaviour (an arena OOM
                 * here aborted the process). id may be missing too, so the
                 * result can be unpairable -- skip the call entirely. */
                jc_logf(JC_LOG_WARN, "out of memory preparing a tool call");
                goto next_call;
            }
            call_id = id_copy;

            /* METER HERE: at the ATTEMPT, before any gate, at any depth.
             *
             * Two readings of --max-tool-calls were possible and the code had
             * silently taken the weaker one. Counting after the gates bounds
             * what the agent was PERMITTED to do; counting here bounds what it
             * ATTEMPTED. Under the old reading the more forbidden things a run
             * tried, the less its cap meant -- probe P13 measured a model
             * repeating one out-of-scope write five times, journaled
             * `tool_calls: 0`, cap of 2 never fired, and the run ended
             * `outcome: ok` having spent its whole token budget on an action
             * that could not succeed.
             *
             * M429 fixed the other half by telling the model it was repeating,
             * which needs the model to cooperate; HARDENING.md 6b is explicit
             * that the defences worth having are the ones that do not.
             *
             * Placed before the gates rather than added to each of them: there
             * are twelve `goto next_call` sites, and the thirteenth would have
             * forgotten. Deliberately AFTER the out-of-memory skip above --
             * that is jichi failing to prepare a call, not the model
             * attempting one.
             *
             * M431 established the at-any-depth half (a delegate's calls spend
             * the run's cap; before it a cap of 3 permitted 9). */
            /* M861 (N28): the task's own budget counts the same attempts, at
             * every depth of the child's process, with or without an envelope
             * (0 and unread in the lead, which has no task budget). */
            app->task_tool_used++;
            /* M951: the leg's tool-call counter, env-INDEPENDENT (a relay leg
             * runs without --auto, where app->env is NULL). It is E5's signal
             * and the baton's "Calls: N in this leg". Same at-any-depth rule
             * as task_tool_used above: a delegate's calls spend the lead's leg
             * budget, which is the point (the leg's PROMPT grew by them). */
            app->relay_leg_calls++;
            if (env_budget_applies(app)) {
                app->env->tool_calls++;
                /* M98: count READ-category tools for the per-run read budget
                 * (the next budget check trips JC_BUDGET_READS). An attempted
                 * read is an attempt on the same reasoning. */
                if (jc_agent_tool_category(gate_name) == JC_TOOLCAT_READ) {
                    app->env->reads++;
                }
            }

            /* M159: remember the tool for the control channel's status
             * snapshot (cheap; no-op without an open control socket). */
            if (app->control != NULL && app->agent_depth == 0) {
                jc_snprintf(app->control->last_tool,
                            sizeof(app->control->last_tool), "%s", name_copy);
            }

            if (cb != NULL && cb->on_tool_start != NULL) {
                cb->on_tool_start(cb->user, name_copy, args_copy,
                                  (call_id != NULL) ? call_id : "");
            }

            /* Resolve the approval verdict for this tool: mode baseline +
             * config allow/deny + MCP policy (see jc_perm_for_tool). */
            tool = jc_tool_registry_find(app->tools, name_copy);

            /* M853 (N32): a DELEGATE ends its turn with a call to a tool named
             * like `finish` that carries a message. jichi has no such tool -- a
             * turn ends with a reply that makes no tool call -- and models call
             * one anyway: the demos drive's children called `finish` 203 times in
             * six runs, each with a finished answer in `message`, each refused as
             * an unknown tool. The workers then ran `echo "Done"` until stopped
             * (run 0087: ~430 calls after the commit), and a stopped worker's work
             * does not merge (N33). The message is the answer: it becomes the
             * assistant's reply once this round's results are in (after this
             * loop), so every caller reads it where it reads any answer, and a
             * parallel child goes on to verify and merge like one that replied.
             *
             * Never at the top level: there a final answer passes the self-review
             * and the envelope's verify gate, and a call that ended the turn would
             * go around both. The top level gets jc_tool_execute's error, which
             * says how a turn ends. Only an UNREGISTERED name -- a registered
             * `submit` (an MCP server's, say) runs as itself -- and only with a
             * non-blank `message`: a call without one gets that same error rather
             * than a guessed answer. Ahead of the fences on purpose: ending the
             * turn runs nothing. */
            if (tool == NULL && app->agent_depth >= 1 &&
                jc_tool_is_finish_alias(name_copy)) {
                char *fmsg = jc_tool_finish_alias_message(args_copy);
                if (fmsg != NULL) {
                    if (finish_answer == NULL) {
                        finish_answer = jc_arena_strdup(jc_app_scratch(app), fmsg);
                    }
                    jc_history_add_tool_result(hist, call_id,
                        "ok: there is no such tool, so jichi took this call as "
                        "the end of your turn, and its message as your answer.", 0);
                    if (cb != NULL && cb->on_tool_result != NULL) {
                        cb->on_tool_result(cb->user, name_copy, "ended the turn", 0,
                                           (call_id != NULL) ? call_id : "");
                    }
                    {
                        cJSON *o = telem(app, "finish_alias");
                        if (o != NULL) {
                            cJSON_AddStringToObject(o, "name", name_copy);
                            cJSON_AddNumberToObject(o, "message_bytes",
                                                    (double)strlen(fmsg));
                        }
                        jc_eventlog_end(app->telemetry, o);
                    }
                    /* M807's child transcript: why this child stopped. */
                    if (app->transcript != NULL) {
                        cJSON *to = jc_eventlog_begin(app->transcript,
                                                      "finish_alias");
                        if (to != NULL) {
                            cJSON_AddStringToObject(to, "name", name_copy);
                            jc_eventlog_add_text(to, "message", fmsg,
                                                 JC_EVENTLOG_TEXT_MAX);
                        }
                        jc_eventlog_end(app->transcript, to);
                    }
                    free(fmsg);
                    goto next_call;
                }
            }

            /* A subagent profile's allowed-tools fence (opts->allow): a backstop
             * in case the model calls a tool it remembers despite the tool not
             * being advertised. */
            if (opts->allow != NULL && opts->allow->len > 0 &&
                !jc_tool_allowed(opts->allow, gate_name)) {
                /* M360: the refusal names the tools that ARE available. The
                 * old text ("not permitted by this agent's allowed-tools")
                 * stated a cause with no way forward -- the M342 message
                 * class that amplifies retry loops -- and under the core
                 * tool profile the TOP-LEVEL agent meets this fence too. */
                struct jc_sb rf;
                jc_sb_init(&rf);
                jc_tool_refusal_render(opts->allow, name_copy, &rf);
                jc_history_add_tool_result(hist, call_id,
                    rf.data != NULL ? rf.data : "Tool not available.", 1);
                jc_app_why_note(app, name_copy, args_copy, 1,
                    "the delegated task's list of allowed tools does not name it",
                    rf.data);
                jc_sb_free(&rf);
                app->tool_refusals++; /* M638: a fence, counted apart */
                if (cb != NULL && cb->on_tool_result != NULL) {
                    cb->on_tool_result(cb->user, name_copy,
                                       "denied (agent fence)", 1,
                                       (call_id != NULL) ? call_id : "");
                }
                goto next_call;
            }

            verdict = jc_perm_for_tool_why((enum jc_agent_mode)app->mode,
                /* M631: plan_allowed passes plan mode's read-only rule -- one
                 * declared write, not a read-only lie. */
                tool != NULL ? (tool->readonly || tool->plan_allowed) : 0,
                (jc_perm_name_in_allow(&app->config.permissions, name_copy) ||
                 jc_perm_name_in_allow(&app->config.permissions, gate_name)),
                (jc_perm_name_in_deny(&app->config.permissions, name_copy) ||
                 jc_perm_name_in_deny(&app->config.permissions, gate_name)),
                (enum jc_approval)jc_mcp_tool_policy(app->mcp, gate_name),
                &perm_why);
            allow_why = perm_why;

            if (verdict == JC_APPROVAL_DENY) {
                jc_history_add_tool_result(hist, call_id,
                    "Tool call denied by policy. It will not be approved in "
                    "this run; take a different approach with the tools you "
                    "have.", 1);
                jc_app_why_note(app, name_copy, args_copy, 1, perm_why,
                    "Tool call denied by policy.");
                app->tool_refusals++; /* M638: a fence, counted apart */
                if (cb != NULL && cb->on_tool_result != NULL) {
                    cb->on_tool_result(cb->user, name_copy,
                                       "denied (policy)", 1,
                                       (call_id != NULL) ? call_id : "");
                }
                goto next_call;
            }

            /* Constraint enforcement (M110): a hard user-set limit ("do not run
             * the build") REFUSES a violating tool call mechanically, even if the
             * model forgot the instruction after a compaction / lost window. The
             * durable constraint text lives in the system prompt; this is the
             * backstop that makes it binding, not advisory.
             *
             * M734 (plan D2 (c)): binding for a rule the operator WROTE. A rule
             * INFERRED from the prompt's wording now advises -- the call runs, is
             * journalled, and its result says which rule it went against -- since
             * M730 measured 6 of 9 inferred rules wrong, each forbidding the gate
             * its own task named. */
            if (app->constraints_on && app->n_constraints > 0) {
                char creason[256];
                const char *cmd = NULL;
                cJSON *aj = NULL;
                cJSON *o2;
                int blocked;
                int scope_exempt = 0;
                int crule = -1;
                enum jc_constraint_verdict cverdict;
                if (jc_tool_name_runs_shell(gate_name)) { /* M846 */
                    aj = cJSON_Parse(args_copy);
                    if (aj != NULL) cmd = jc_json_get_str(aj, "command", NULL);
                }
                /* M459: does an OPERATOR DECLARATION already permit writing
                 * this exact path? `--edit-scope`/`editScope` is a typed flag
                 * naming a file; an inferred read-only is a guess scanned out
                 * of prose. Before this, the guess won -- a run told exactly
                 * which file was writable refused to write that file (probe
                 * P3). A shell command has no path to check and is unaffected.
                 *
                 * M501: the test was `write_file`/`edit_file` BY NAME, and
                 * `apply_patch` -- the tool a model reaches for when making
                 * several edits -- carries its paths in `edits[]`, so it was
                 * never exempt. Same for the three tools that write a file at a
                 * top-level `path` (generate_audio, generate_image,
                 * record_audio). The rule is now a property of the ARGUMENTS
                 * rather than a list of names: exempt when the call declares at
                 * least one path and EVERY path it declares is in scope.
                 *
                 * All-or-nothing on purpose: `apply_patch` is atomic, so one
                 * out-of-scope path must block the whole call -- and a
                 * collection that overflowed (-1) must never widen a
                 * permission, which is why it is treated as "not exempt". */
                if (app->env != NULL && !(tool != NULL && tool->readonly)) {
                    const char *paths[JC_AGENT_MAX_ARG_PATHS];
                    int np, pi;
                    if (aj == NULL) aj = cJSON_Parse(args_copy);
                    np = jc_argpath_collect(aj, paths,
                                            JC_AGENT_MAX_ARG_PATHS);
                    if (np > 0) {
                        scope_exempt = 1;
                        for (pi = 0; pi < np; pi++) {
                            if (!jc_env_path_in_scope(app->env, app->root,
                                                      paths[pi])) {
                                scope_exempt = 0;
                                break;
                            }
                        }
                    }
                }
                cverdict = jc_constraint_judge(app->constraints,
                    app->n_constraints, name_copy, cmd,
                    tool != NULL ? tool->readonly : 0, scope_exempt,
                    creason, sizeof creason, &crule);
                blocked = (cverdict == JC_CONSTRAINT_REFUSE);
                /* M734: advice, not refusal. Remember the rule so the result can
                 * say so, and journal EVERY such call: how often a guessed rule
                 * meets the work is the rate the next decision about it needs. */
                if (cverdict == JC_CONSTRAINT_ADVISE) {
                    advise_rule = crule;
                    jc_snprintf(advise_text, sizeof advise_text, "%s", creason);
                    o2 = jc_env_journal_begin(app->env, "constraint_advisory");
                    if (o2 != NULL) {
                        cJSON_AddStringToObject(o2, "tool", name_copy);
                        cJSON_AddStringToObject(o2, "rule", advise_text);
                        if (crule >= 0 && crule < app->n_constraints &&
                            app->constraints[crule].source != NULL) {
                            cJSON_AddStringToObject(o2, "from",
                                app->constraints[crule].source);  /* M872 */
                        }
                        jc_env_journal_end(app->env, o2);
                    }
                    creason[0] = '\0';
                }
                /* Announce it. A constraint quietly not applied is as opaque as
                 * one quietly applied: either way the operator cannot see which
                 * rule decided. Emitted only when the exemption CHANGED the
                 * outcome, so an ordinary in-scope write stays silent. Since M734
                 * the outcome it changes is advice: without the operator's
                 * declared path, an inferred read-only would have been told. */
                if (scope_exempt && cverdict == JC_CONSTRAINT_ALLOW &&
                    jc_constraint_judge(app->constraints, app->n_constraints,
                                        name_copy, cmd,
                                        tool != NULL ? tool->readonly : 0, 0,
                                        creason, sizeof creason, NULL)
                        == JC_CONSTRAINT_ADVISE) {
                    /* WARN, not INFO. M167 made the adoption of an inferred
                     * rule visible for precisely this reason -- INFO sits below
                     * the default threshold, so a rule that changed behaviour
                     * did so in silence. Declining to apply one changes
                     * behaviour just as much. */
                    jc_logf(JC_LOG_WARN,
                            "[constraint] an inferred read-only does not apply "
                            "to %s: --edit-scope explicitly permits this path",
                            name_copy);
                    if (cb != NULL && cb->on_status != NULL) {
                        cb->on_status(cb->user,
                            "inferred read-only set aside: --edit-scope "
                            "explicitly permits this path");
                    }
                    o2 = telem(app, "constraint_exempt");
                    if (o2 != NULL) {
                        cJSON_AddStringToObject(o2, "tool", name_copy);
                        cJSON_AddStringToObject(o2, "why", "explicit edit-scope");
                        jc_eventlog_end(app->telemetry, o2);
                    }
                    /* ...and into the RUN JOURNAL, as `constraint` already is.
                     * A supervisor reconstructing an unattended run needs to see
                     * that a rule it might have relied on did not fire -- the
                     * same argument M443's `degraded` makes for decisions taken
                     * with no operator present. */
                    o2 = jc_env_journal_begin(app->env, "constraint_exempt");
                    if (o2 != NULL) {
                        cJSON_AddStringToObject(o2, "tool", name_copy);
                        cJSON_AddStringToObject(o2, "why", "explicit edit-scope");
                        jc_env_journal_end(app->env, o2);
                    }
                    creason[0] = '\0';
                }
                if (aj != NULL) cJSON_Delete(aj);
                if (blocked) {
                    cJSON *o;
                    jc_history_add_tool_result(hist, call_id, creason, 1);
                    jc_app_why_note(app, name_copy, args_copy, 1,
                                    "a constraint you wrote refuses it", creason);
                    app->tool_refusals++; /* M638: a fence, counted apart */
                    if (cb != NULL && cb->on_tool_result != NULL) {
                        cb->on_tool_result(cb->user, name_copy,
                                           "blocked (constraint)", 1,
                                       (call_id != NULL) ? call_id : "");
                    }
                    if (cb != NULL && cb->on_status != NULL) {
                        cb->on_status(cb->user, creason);
                    }
                    jc_logf(JC_LOG_INFO, "[constraint] refused %s", name_copy);
                    o = telem(app, "constraint");
                    if (o != NULL) {
                        cJSON_AddStringToObject(o, "tool", name_copy);
                        /* M207: the tool name alone does not say WHICH rule
                         * refused it, so a log full of `constraint` events could
                         * not be told from a permission denial when auditing a
                         * drive that made no progress. */
                        cJSON_AddStringToObject(o, "reason", creason);
                        jc_eventlog_end(app->telemetry, o);
                    }
                    goto next_call;
                }
            }

            /* Privileged-command gate (M153): a model-issued shell command
             * launched under sudo/doas/pkexec/su/run0. Evaluated BELOW the
             * verdict, so the blanket AUTO / TUI-`always` grant can NEVER
             * satisfy it -- that grant is exactly what let an agent escalate
             * privilege unbidden. Default posture `ask`: interactive
             * front-ends prompt afresh (confirm_privileged bypasses the
             * `always` set); unattended runs (headless AUTO, a subagent, or a
             * client with no privileged prompt) refuse. Runs at all depths.
             * The always-on audit (M154) records the decision here. */
            if (jc_tool_name_runs_shell(gate_name)) { /* M846: run_tests too */
                cJSON *pj = cJSON_Parse(args_copy);
                const char *pcmd = (pj != NULL)
                    ? jc_json_get_str(pj, "command", NULL) : NULL;
                const char *ptok = NULL;
                enum jc_priv_kind pk = (pcmd != NULL)
                    ? jc_priv_detect(pcmd, &ptok) : JC_PRIV_NONE;
                if (pk != JC_PRIV_NONE) {
                    int policy = app->config.privileged_commands;
                    const char *launcher = jc_priv_kind_name(pk);
                    const char *decision;
                    int refuse = 0;
                    const char *reason = NULL;
                    /* M849 (S4, the operator's decision of 2026-10-01): the
                     * allowlist is for the user's own turn -- the main turn or
                     * a subtask command (M848) -- and for a top-level --auto run,
                     * its documented CI use; never for a delegate. It matches by
                     * word prefix, so an entry like `sudo systemctl` admitted
                     * `sudo systemctl disable --now sshd` to an unattended
                     * subagent with no prompt at all. Keyed on the delegate's
                     * posture, not on agent_depth: a subtask also runs at depth
                     * 1 and is the user's turn. An explicit privilegedCommands:
                     * allow (allow-all) still admits a delegate's command; that
                     * setting means what it says. */
                    int allow = !opts->auto_posture &&
                        jc_priv_allowlisted(pcmd,
                            (const char *const *)app->config.privileged_allow.data,
                            (int)app->config.privileged_allow.len);
                    if (allow) {
                        decision = "allowlist";
                    } else if (policy == JC_PRIVPOL_DENY) {
                        decision = "deny";
                        refuse = 1;
                        reason = "privileged command refused "
                                 "(privilegedCommands: deny).";
                    } else if (policy == JC_PRIVPOL_ALLOW) {
                        decision = "allow";
                    } else if (cb != NULL && cb->confirm_privileged != NULL &&
                               !opts->auto_posture) {
                        if (cb->confirm_privileged(cb->user, launcher, pcmd)) {
                            decision = "ask_approved";
                        } else {
                            decision = "ask_denied";
                            refuse = 1;
                            reason = "privileged command denied by the user.";
                        }
                    } else if (opts->auto_posture) {
                        /* M849: a delegate's own decision value, so the audit
                         * shows WHO tried, and a reason that does not advise the
                         * allowlist the delegate cannot use. */
                        decision = "delegate_refused";
                        refuse = 1;
                        app->deg_privilege++;   /* M443 */
                        reason = "privileged command refused: a delegate does "
                                 "not run sudo/doas/pkexec/su, not even one "
                                 "privilegedCommandsAllow names -- that list is "
                                 "for your own turn and a top-level --auto run. "
                                 "Run the command in the main turn.";
                    } else {
                        decision = "unattended_refused";
                        refuse = 1;
                        app->deg_privilege++;   /* M443 */
                        reason = "privileged command refused: an unattended "
                                 "agent will not run sudo/doas/pkexec/su. "
                                 "Approve it interactively, or set "
                                 "privilegedCommands: allow, or add it to "
                                 "privilegedCommandsAllow.";
                    }
                    jc_audit_privileged(app, launcher, pcmd, decision);
                    {
                        cJSON *o = telem(app, "privileged");
                        if (o != NULL) {
                            cJSON_AddStringToObject(o, "launcher", launcher);
                            cJSON_AddStringToObject(o, "decision", decision);
                        }
                        jc_eventlog_end(app->telemetry, o);
                    }
                    if (refuse) {
                        jc_history_add_tool_result(hist, call_id, reason, 1);
                        jc_app_why_note(app, name_copy, args_copy, 1,
                            "the privileged-command policy (sudo, doas, pkexec, su)",
                            reason);
                        app->tool_refusals++; /* M638: a fence, counted apart */
                        if (cb != NULL && cb->on_tool_result != NULL) {
                            cb->on_tool_result(cb->user, name_copy,
                                               "privileged: refused", 1,
                                       (call_id != NULL) ? call_id : "");
                        }
                        if (cb != NULL && cb->on_status != NULL) {
                            cb->on_status(cb->user, reason);
                        }
                        jc_logf(JC_LOG_WARN, "[privileged] %s refused (%s)",
                                launcher, decision);
                        if (pj != NULL) cJSON_Delete(pj);
                        goto next_call;
                    }
                }
                if (pj != NULL) cJSON_Delete(pj);
            }

            /* Kinetic gate (M163a): a tool/command that actuates hardware
             * (moves mass or energy). Mirrors the M153 privileged gate --
             * below the verdict (AUTO / TUI-`always` can't satisfy it),
             * allowlist checked FIRST (so a safe-state stop_all stays callable
             * when everything else refuses), default `ask` => unattended
             * refuses. Subject is a kinetic tool by name, OR a shell command
             * that shadow-matches a kinetic tool's command / a
             * kineticShellPrefixes entry (catches
             * `run_terminal_command "./motor.sh"`). Runs at all depths;
             * always-on audit records the decision. */
            {
                int is_kin = 0;
                const char *subject = NULL;   /* "tool:<name>" or "shell" */
                char subjbuf[128];
                const char *detail = args_copy;
                const char *hit = NULL;
                cJSON *kj = NULL;

                if (kinetic_tool_named(app, gate_name)) {
                    is_kin = 1;
                    jc_snprintf(subjbuf, sizeof(subjbuf), "tool:%s", name_copy);
                    subject = subjbuf;
                } else if (jc_tool_name_runs_shell(gate_name)) { /* M846 */
                    const char *prefixes[64];
                    int np = kinetic_collect_prefixes(app, prefixes, 64);
                    if (np > 0) {
                        const char *pcmd;
                        kj = cJSON_Parse(args_copy);
                        pcmd = (kj != NULL)
                            ? jc_json_get_str(kj, "command", NULL) : NULL;
                        if (pcmd != NULL &&
                            jc_kinetic_shell_match(pcmd, prefixes, np, &hit)) {
                            is_kin = 1;
                            subject = "shell";
                            detail = pcmd;
                        }
                    }
                }

                if (is_kin) {
                    int policy = app->config.kinetic_commands;
                    const char *decision;
                    int refuse = 0;
                    const char *reason = NULL;
                    int allow = (subject != NULL &&
                                 strcmp(subject, "shell") != 0)
                        ? jc_kinetic_name_allowlisted(gate_name,
                            (const char *const *)app->config.kinetic_allow.data,
                            (int)app->config.kinetic_allow.len)
                        : jc_priv_allowlisted(detail,
                            (const char *const *)app->config.kinetic_allow.data,
                            (int)app->config.kinetic_allow.len);
                    if (allow) {
                        decision = "allowlist";
                    } else if (policy == JC_PRIVPOL_DENY) {
                        decision = "deny";
                        refuse = 1;
                        reason = "kinetic action refused "
                                 "(kineticCommands: deny).";
                    } else if (policy == JC_PRIVPOL_ALLOW) {
                        decision = "allow";
                    } else if (cb != NULL && cb->confirm_kinetic != NULL &&
                               !opts->auto_posture) {
                        if (cb->confirm_kinetic(cb->user, subject, detail)) {
                            decision = "ask_approved";
                        } else {
                            decision = "ask_denied";
                            refuse = 1;
                            reason = "kinetic action denied by the user.";
                        }
                    } else {
                        decision = "unattended_refused";
                        refuse = 1;
                        app->deg_privilege++;   /* M443 */
                        reason = "kinetic action refused: an unattended agent "
                                 "will not actuate hardware. Approve it "
                                 "interactively, set kineticCommands: allow, "
                                 "or add it to kineticCommandsAllow.";
                    }
                    jc_audit_kinetic(app, subject, detail, decision);
                    {
                        cJSON *o = telem(app, "kinetic");
                        if (o != NULL) {
                            cJSON_AddStringToObject(o, "subject", subject);
                            cJSON_AddStringToObject(o, "decision", decision);
                        }
                        jc_eventlog_end(app->telemetry, o);
                    }
                    if (refuse) {
                        jc_history_add_tool_result(hist, call_id, reason, 1);
                        jc_app_why_note(app, name_copy, args_copy, 1,
                            "the physical-actuation policy (kineticCommands)",
                            reason);
                        app->tool_refusals++; /* M638: a fence, counted apart */
                        if (cb != NULL && cb->on_tool_result != NULL) {
                            cb->on_tool_result(cb->user, name_copy,
                                               "kinetic: refused", 1,
                                       (call_id != NULL) ? call_id : "");
                        }
                        if (cb != NULL && cb->on_status != NULL) {
                            cb->on_status(cb->user, reason);
                        }
                        jc_logf(JC_LOG_WARN, "[kinetic] %s refused (%s)",
                                subject, decision);
                        if (kj != NULL) cJSON_Delete(kj);
                        goto next_call;
                    }
                }
                if (kj != NULL) cJSON_Delete(kj);
            }

            /* ASK: prompt interactively; in headless mode (no confirm_tool)
             * refuse rather than run silently. A subagent's auto posture runs
             * ASK verdicts without a prompt (DENY is still refused above) --
             * EXCEPT a shell call. M849 (S4, the operator's decision of
             * 2026-10-01): a delegate's run_terminal_command, or run_tests with
             * a command, follows the user's own posture, so in chat mode it is
             * asked here exactly as the main turn's is, and refused when the
             * delegate has no way to ask. The shell is the one tool whose reach
             * no other fence bounds (the path fence, the edit scope and the
             * tool list all stop at its argv); a delegate's other tools keep
             * the auto posture (DEFERRED def-286). In --auto the verdict is
             * ALLOW, so this branch is never reached. An unknown tool
             * (tool==NULL) skips the gate so jc_tool_execute can report it
             * cleanly. */
            if (verdict == JC_APPROVAL_ASK && tool != NULL &&
                (!opts->auto_posture || jc_tool_name_runs_shell(gate_name))) {
                if (cb != NULL && cb->confirm_tool != NULL) {
                    char *edited = NULL;
                    app->why_approval = 0;
                    if (!cb->confirm_tool(cb->user, name_copy, args_copy,
                                          &edited)) {
                        /* M570: A HUMAN DENIAL NOW REACHES THE LOOP DETECTOR,
                         * which it never did. This branch used to `goto
                         * next_call` straight past jc_toolloop_note -- so the
                         * detector saw FENCE denials (they return through
                         * jc_tool_execute) and never a person's. The one case
                         * where somebody explicitly said no was the one case
                         * the loop-breaker was blind to.
                         *
                         * The operator found it by pressing 0 seven times:
                         * "And the 0 does not abort, so I kept pressing it
                         * until this happened" -- apply_patch, then edit_file
                         * four times, then run_terminal_command, then finally
                         * ask_user. The model got a bare "denied" each time,
                         * with no signal that repeating was pointless. For a
                         * screen-reader user every retry costs the whole prompt
                         * and preview, read aloud.
                         *
                         * The detector's advice for JC_FAIL_DENIED already says
                         * exactly the right thing -- "refused by policy, not
                         * failing by accident: it will not succeed by
                         * rephrasing" -- and its thresholds (3 exact, 4 class)
                         * would have ended this at the third or fourth call.
                         *
                         * TWO REMEDIES, BOTH NEEDED. The note goes into history
                         * so the record says why, and so a NEXT turn's model
                         * sees it. And abort_flag stops this turn, because
                         * telling the model to behave still leaves the user
                         * waiting on its goodwill -- which is what "0 does not
                         * abort" means. Ending the turn is safe here: the TUI
                         * clears abort_flag before reading each new line, so
                         * the session continues normally.
                         *
                         * It fires on REPEATS OF THE SAME TARGET, not on three
                         * unrelated refusals -- the detector keys on tool plus
                         * argument key, so denying three different reads and
                         * allowing the fourth still works. */
                        enum jc_toolloop_verdict dv;
                        int dcount = 0;
                        const char *dtext = "Tool call denied by the user.";
                        struct jc_sb dmsg;
                        int dstop = 0;
                        char dbudget[160];

                        /* M573: THE MODEL IS TOLD ITS BUDGET. It used to receive
                         * the bare sentence above and nothing else, so it could
                         * not know a third refusal ends the turn -- and the
                         * operator's transcript showed it discovering that the
                         * expensive way: ten prompts before it thought to call
                         * ask_user. Told the rule, that decision is available on
                         * the first refusal instead of the tenth.
                         *
                         * English, deliberately: this is protocol text for the
                         * model, not chrome for a person, so it does not belong
                         * in the message catalog. Counted BEFORE the text is
                         * built, which is why the increment moved up here from
                         * below the history append. */
                        consec_deny++;
                        jc_snprintf(dbudget, sizeof dbudget,
                            " The user has refused %d call(s) in a row this "
                            "turn; at %d the turn ends. Ask what they want "
                            "instead of trying another way.",
                            consec_deny, JC_DENY_STOP_AT);

                        jc_sb_init(&dmsg);
                        dv = jc_toolloop_note(&toolloop, name_copy, args_copy,
                                              JC_FAIL_DENIED, &dcount);
                        if (jc_sb_append(&dmsg, dtext) == JC_OK &&
                            jc_sb_append(&dmsg, dbudget) == JC_OK &&
                            dmsg.data != NULL) {
                            dtext = dmsg.data;
                        }
                        if (dv != JC_TOOLLOOP_NONE) {
                            char dnote[600];
                            jc_toolloop_render(dv, name_copy, JC_FAIL_DENIED,
                                               dcount, dnote, sizeof dnote);
                            if (jc_sb_append(&dmsg, dnote) == JC_OK &&
                                dmsg.data != NULL) {
                                dtext = dmsg.data;
                            }
                            /* M572: THE NOTE NO LONGER ABORTS. M570 made this
                             * branch set dstop, which was the only stop it had.
                             * It is now the wrong place for that decision twice
                             * over: jc_toolloop's keys include the tool name (so
                             * rotating tools multiplies the budget -- ten prompts
                             * for one rename), and its counts run for the whole
                             * turn with no notion of an approval breaking the
                             * streak. That second half is what caught this: the
                             * new driver's arm D denies twice, ALLOWS, then
                             * denies twice, and the old counter still stopped the
                             * run because edit_file had reached four across the
                             * approval.
                             *
                             * The two concerns are now separate and each sits
                             * where it belongs. This note TELLS THE MODEL it is
                             * repeating -- jc_toolloop's original job, and a
                             * turn-wide count is right for it. Whether to stop
                             * ASKING A PERSON is consec_deny, which is
                             * tool-independent and resets on approval. */
                            jc_logf(JC_LOG_WARN,
                                "user denied %s %dx this turn (advising model)",
                                name_copy, dcount);
                        }
                        jc_history_add_tool_result(hist, call_id, dtext, 1);
                        jc_app_why_note(app, name_copy, args_copy, 1,
                                        "you declined it at the prompt", dtext);
                        app->tool_refusals++; /* M638: a fence, counted apart */
                        if (cb->on_tool_result != NULL) {
                            cb->on_tool_result(cb->user, name_copy,
                                               "denied", 1,
                                       (call_id != NULL) ? call_id : "");
                        }
                        jc_sb_free(&dmsg);
                        /* M572: the tool-independent count is incremented
                         * above, before the model's message is built (M573).
                         * THE FIRST refusal also teaches the way out -- once per
                         * turn, because the approval prompt is the
                         * most-repeated string in a session and cannot carry a
                         * sixth advertised option. */
                        if (consec_deny == 1 && cb->on_status != NULL) {
                            char hint[200];
                            jc_snprintf(hint, sizeof hint,
                                        jc_msg(JC_MSG_DENY_HINT),
                                        JC_DENY_STOP_AT);
                            cb->on_status(cb->user, hint);
                        }
                        if (consec_deny >= JC_DENY_STOP_AT) {
                            dstop = 1;
                            jc_logf(JC_LOG_WARN,
                                "%d consecutive refusals this turn -- stopping",
                                consec_deny);
                        }
                        if (dstop) {
                            /* on_status is the front-end-neutral channel for
                             * this, and cb_status already renders a bare line
                             * with no glyph or brackets -- so it is accessible
                             * without a second arm. */
                            if (cb->on_status != NULL) {
                                cb->on_status(cb->user,
                                              jc_msg(JC_MSG_DENIED_STOP));
                            }
                            app->abort_flag = 1;
                        }
                        goto next_call;
                    }
                    /* M572: an approval clears the refusal streak. Denying
                     * three proposals and accepting the fourth is ordinary use
                     * and must not be mistaken for thrashing. */
                    consec_deny = 0;
                    /* M762: how it was approved, as the front end reported it. */
                    switch (app->why_approval) {
                    case JC_WHY_APPROVED_PROMPT:
                        allow_why = "you approved it at the prompt";
                        break;
                    case JC_WHY_APPROVED_ALWAYS:
                        allow_why = "you chose \"always\" for this tool at the prompt";
                        break;
                    case JC_WHY_ALWAYS_LIST:
                        allow_why = "you chose \"always\" for this tool earlier in "
                                    "this session";
                        break;
                    case JC_WHY_APPROVED_EDITED:
                        allow_why = "you edited its arguments, then approved it";
                        break;
                    default:
                        allow_why = "it was approved at the prompt";
                        break;
                    }
                    /* The user edited the args before approving (M68): run with
                     * the edited call, so the scope/hook checks below and the
                     * tool itself see the new args. `edited` is malloc'd, and
                     * so is args_copy (M218) -- take ownership directly. */
                    if (edited != NULL) {
                        free(args_copy);
                        args_copy = edited;
                    }
                } else {
                    /* M849: inside this branch a delegate's call is a shell
                     * call (see the condition above). It reaches here when the
                     * delegate was given no way to ask -- headless, an editor
                     * session (ACP forwards no callbacks into a delegate), a
                     * spawn_parallel child (another process), or the TUI under
                     * -q -- and "headless mode" would be false in three of
                     * those, so the delegate's refusal names its own reason. */
                    const char *nomsg = opts->auto_posture
                        ? "This delegate's shell command needs your approval, and "
                          "the delegate has no way to ask you here (headless, an "
                          "editor session, a parallel child, or the TUI with -q). "
                          "Run the command in the main turn, or re-run with "
                          "--auto to allow it."
                        : "Tool requires approval, unavailable in headless mode; "
                          "re-run with --auto to allow it.";
                    /* M443: a tool the run needed, refused only because nobody
                     * could approve it. The model sees a tool error and may work
                     * around it; the OPERATOR sees nothing unless this is counted. */
                    app->deg_approval++;
                    jc_history_add_tool_result(hist, call_id, nomsg, 1);
                    /* The main turn's /why detail is unchanged, byte for
                     * byte; only a delegate's says something new. */
                    jc_app_why_note(app, name_copy, args_copy, 1,
                        opts->auto_posture
                            ? "a delegate's shell command needs approval, and the "
                              "delegate had no way to ask"
                            : "it needs approval, and nobody was there to give it",
                        opts->auto_posture
                            ? nomsg
                            : "Tool requires approval, unavailable in headless "
                              "mode.");
                    app->tool_refusals++; /* M638: a fence, counted apart */
                    if (cb != NULL && cb->on_tool_result != NULL) {
                        cb->on_tool_result(cb->user, name_copy,
                                           "needs approval", 1,
                                       (call_id != NULL) ? call_id : "");
                    }
                    goto next_call;
                }
            }

            /* Strict edit-scope: the shell can write anywhere, so when a scope
             * is in force and --strict-scope is set, run_terminal_command is
             * refused outright (all edits must go through the scoped file
             * tools). Enforced at ANY depth (M133) so a delegated child can't
             * escape the scope via the shell; journaling stays depth-0 only. */
            if (env_scope_fence(app) && app->env->strict_scope &&
                jc_tool_name_runs_shell(gate_name)) { /* M846 (PAR-2): run_tests too */
                char bmsg[820];
                block_message(app, name_copy, "run_terminal_command",
                    "run_terminal_command is disabled by --strict-scope while an "
                    "edit-scope is active; edit files with edit_file/write_file "
                    "(within the scope) instead.", bmsg, sizeof(bmsg));
                jc_history_add_tool_result(hist, call_id, bmsg, 1);
                jc_app_why_note(app, name_copy, args_copy, 1,
                    "--strict-scope refuses the shell while an edit scope is set",
                    bmsg);
                app->tool_refusals++; /* M638: a fence, counted apart */
                if (cb != NULL && cb->on_tool_result != NULL) {
                    cb->on_tool_result(cb->user, name_copy,
                                       "denied (strict-scope)", 1,
                                       (call_id != NULL) ? call_id : "");
                }
                if (env_active(app)) {
                    cJSON *jo = jc_env_journal_begin(app->env, "tool_call");
                    if (jo != NULL) {
                        cJSON_AddStringToObject(jo, "name", name_copy);
                        cJSON_AddBoolToObject(jo, "blocked", 1);
                    }
                    jc_env_journal_end(app->env, jo);
                }
                goto next_call;
            }

            /* Envelope edit-scope fence: a file-mutating tool may only touch a
             * path inside the run's --edit-scope. Enforced at ANY depth (M133),
             * so a subagent or a spawn_parallel write child is held to the same
             * scope; covers edit_file/write_file/apply_patch. Journaling stays
             * depth-0 only -- a forked child shares the journal fd, so writing
             * to it from the child would interleave. */
            /* M535: two write tools apply a language-server WorkspaceEdit, whose
             * paths are NOT in the tool's arguments -- `rename_symbol` renames
             * "across the project" by design. Fencing them on their anchor
             * `path` would check the one file we can see and imply safety for
             * every file we cannot, which is worse than refusing: a scope that
             * silently does not cover a project-wide rename is a scope the
             * operator will trust wrongly. So while an edit scope is armed they
             * are refused, with the reason and the alternative named. Fail
             * closed, as the sibling collector jc_argpath_collect already does. */
            if (env_scope_fence(app) && tool != NULL && !tool->readonly &&
                (strcmp(gate_name, "rename_symbol") == 0 ||
                 strcmp(gate_name, "apply_code_action") == 0)) {
                char bmsg[820];
                block_message(app, name_copy, gate_name,
                    "This tool applies a language-server edit whose paths are "
                    "not in its arguments, so --edit-scope cannot bound it. "
                    "Refused while an edit scope is set. Use edit_file or "
                    "apply_patch, whose paths jichi can check, or re-run "
                    "without --edit-scope.",
                    bmsg, sizeof(bmsg));
                jc_history_add_tool_result(hist, call_id, bmsg, 1);
                jc_app_why_note(app, name_copy, args_copy, 1,
                    "the edit scope cannot see the paths this tool would change",
                    bmsg);
                app->tool_refusals++; /* M638: a fence, counted apart */
                if (cb != NULL && cb->on_tool_result != NULL) {
                    cb->on_tool_result(cb->user, name_copy, bmsg, 1,
                                       (call_id != NULL) ? call_id : "");
                }
                goto next_call;
            }
            if (env_scope_fence(app) && tool != NULL && !tool->readonly) {
                cJSON *hold = NULL;
                const char *bad =
                    call_out_of_scope_path(app, gate_name, args_copy, &hold);
                if (bad != NULL) {
                    {
                        /* M333: name the path AND the allowed scope. The old
                         * message said neither, so a model that guessed wrong
                         * could only guess again -- which is exactly what one
                         * run did, 177 times. */
                        struct jc_sb sm;
                        jc_sb_init(&sm);
                        jc_sb_append_fmt(&sm,
                            "Path %s is outside this run's --edit-scope; the "
                            "edit was refused. Writable paths are:", bad);
                        /* M431: the shared renderer, so this list and the one
                         * the system prompt states up front cannot differ. */
                        jc_sysmsg_append_scope_list(&sm, &app->env->edit_scope);
                        jc_sb_append(&sm, ". Write only there, or report why the "
                                          "task needs a path outside them.");
                        {
                            char bmsg[820];
                            block_message(app, name_copy, bad,
                                sm.data != NULL ? sm.data
                                    : "Path outside the run's --edit-scope.",
                                bmsg, sizeof(bmsg));
                            jc_history_add_tool_result(hist, call_id, bmsg, 1);
                            jc_app_why_note(app, name_copy, args_copy, 1,
                                "the edit scope does not include that path", bmsg);
                        }
                        jc_sb_free(&sm);
                    }
                    app->tool_refusals++; /* M638: a fence, counted apart */
                    if (cb != NULL && cb->on_tool_result != NULL) {
                        cb->on_tool_result(cb->user, name_copy,
                                           "out of edit-scope", 1,
                                       (call_id != NULL) ? call_id : "");
                    }
                    if (env_active(app)) {
                        cJSON *jo = jc_env_journal_begin(app->env, "tool_call");
                        if (jo != NULL) {
                            cJSON_AddStringToObject(jo, "name", name_copy);
                            cJSON_AddStringToObject(jo, "path", bad);
                            cJSON_AddBoolToObject(jo, "blocked", 1);
                        }
                        jc_env_journal_end(app->env, jo);
                    }
                    cJSON_Delete(hold);
                    goto next_call;
                }
                cJSON_Delete(hold);
            }

            /* PreToolUse hooks (M25): a configured command may veto this tool
             * call (exit 2 / {"decision":"block"}). Fires before any checkpoint
             * so a blocked call leaves no trace. Top-level only (guarded inside
             * jc_hooks_fire). It can only further restrict the verdict. */
            if (jc_hooks_active(app)) {
                struct jc_hook_result hr;
                jc_hook_result_init(&hr);
                jc_hooks_fire(app, JC_HOOK_PRE_TOOL, name_copy, args_copy,
                              NULL, 0, NULL, &hr);
                if (hr.block) {
                    const char *why = (hr.reason.len > 0) ? hr.reason.data
                        : "Tool call blocked by a PreToolUse hook.";
                    jc_history_add_tool_result(hist, call_id, why, 1);
                    jc_app_why_note(app, name_copy, args_copy, 1,
                                    "a PreToolUse hook blocked it", why);
                    if (cb != NULL && cb->on_tool_result != NULL) {
                        cb->on_tool_result(cb->user, name_copy,
                                           "blocked (hook)", 1,
                                       (call_id != NULL) ? call_id : "");
                    }
                    jc_hook_result_free(&hr);
                    goto next_call;
                }
                jc_hook_result_free(&hr);
            }

            /* Checkpoint the workspace once, just before the first mutating
             * tool of a top-level turn actually runs, so /undo can revert this
             * turn's file changes. The same checkpoint is the envelope's first
             * green (known-good) baseline. */
            if (!snapshotted && app->agent_depth == 0 && tool != NULL &&
                !tool->readonly && jc_snapshot_available(app->snapshots)) {
                jc_snapshot_take(app->snapshots, opts->checkpoint_label);
                snapshotted = 1;
                if (app->env != NULL && app->env->green_commit[0] == '\0') {
                    const char *gc = jc_snapshot_commit(app->snapshots,
                        jc_snapshot_count(app->snapshots) - 1);
                    if (gc != NULL) {
                        jc_snprintf(app->env->green_commit,
                                    sizeof(app->env->green_commit), "%s", gc);
                        /* M83: the fixed run-start baseline (green_commit will
                         * advance as verifies pass; this one does not). */
                        jc_snprintf(app->env->baseline_commit,
                                    sizeof(app->env->baseline_commit),
                                    "%s", gc);
                    }
                }
            }

            /* M501: a shell command CAN write outside the edit scope -- the
             * fence covers the file tools, not `sed -i`. Recording that one ran
             * is what makes the revert rule provable rather than a guess: if no
             * shell command ran this run, an out-of-scope change cannot have
             * come from the run, because every other write path is fenced. */
            if (app->env != NULL && jc_tool_name_runs_shell(gate_name)) { /* M846 */
                app->env->shell_ran = 1;
            }

            {
                double t0 = jc_now_millis();
                /* M199: reclaim the PREVIOUS tool call's transients (a file's
                 * bytes while a tool formatted/matched/uploaded them) before
                 * running this one, so a single turn's reads cannot accumulate.
                 * A turn is up to maxToolIters calls -- forced to >= 200 under
                 * an envelope/verify gate -- so per-turn scratch alone left a
                 * ~50 MB peak on a read-heavy --auto turn.
                 *
                 * Every depth: a nested agent run's tool calls reset it too, and
                 * that is safe only because no tool holds tool-scratch data
                 * across a nested run (spawn_subagent's seed task and fence live
                 * on jc_app_scratch, deliberately). Results are malloc-owned
                 * (tu_ok_owned), so nothing here can touch res. */
                if (app->tool_scratch != NULL) {
                    jc_arena_reset(app->tool_scratch);
                }
                /* M437: a delegation runs a whole nested agent loop from inside
                 * this call, and that loop writes the SAME per-run slots this run
                 * reports from. Save and restore them around the call so a nested
                 * run's cap, budget stop or last failure is never attributed to
                 * the run that contained it.
                 *
                 * This is the fix for a hazard jc_app.h documented and solved for
                 * the top-level turn (M322's turn_capped) while leaving it open at
                 * every intermediate depth: the loop cleared the flags on ENTRY
                 * only, so a capped GRANDCHILD left last_run_capped set and a
                 * child that then finished cleanly reported as capped.
                 *
                 * Restoring rather than zeroing matters: this run may have had its
                 * own earlier failure, and zeroing would erase it. The is_error
                 * branch below then overwrites the record with THIS call's
                 * failure, which is the correct precedence. */
                {
                    int sv_capped = app->last_run_capped;
                    int sv_budget = app->last_run_budget_stopped;
                    int sv_looped = app->last_run_looped;   /* M869 */
                    int sv_cls = app->last_fail_cls;
                    int sv_fstop = app->last_fail_stopped;   /* M885 */
                    int sv_repeat = app->last_fail_repeat;   /* M808 */
                    char sv_tool[64];
                    char sv_msg[200];
                    /* M876: a nested run resets and sets these too */
                    char sv_merr[200];
                    int sv_mattempts = app->last_model_attempts;
                    long sv_mt0 = app->last_model_t0;
                    long sv_mt1 = app->last_model_t1;
                    int sv_counts[JC_FAIL_CLASS_COUNT];
                    jc_snprintf(sv_tool, sizeof sv_tool, "%s",
                                app->last_fail_tool);
                    jc_snprintf(sv_msg, sizeof sv_msg, "%s", app->last_fail_msg);
                    jc_snprintf(sv_merr, sizeof sv_merr, "%s",
                                app->last_model_error);
                    memcpy(sv_counts, app->fail_cls_count, sizeof sv_counts);

                    /* M849: not for a shell call -- a delegate's is asked like
                     * the main turn's, and an approval keeps its own why. */
                    if (verdict == JC_APPROVAL_ASK && opts->auto_posture &&
                        !jc_tool_name_runs_shell(gate_name)) {
                        allow_why = "a delegated task's auto posture runs it "
                                    "without asking";
                    }
                    jc_app_why_note(app, name_copy, args_copy, 0, allow_why, NULL);
                    why_seq = app->why.seq;
                    /* M863 (N44 (a)): in a parallel write child, a git command
                     * that discards work or moves HEAD runs only after jichi has
                     * kept the worktree's state. Workers did it against their
                     * task texts (run 0090: 7 `git checkout <file>`, a `reset
                     * --hard`, and two checkouts of jichi's own commit). */
                    if (app->parallel_write && jc_tool_name_runs_shell(gate_name)) {
                        pin_rc = jc_parallel_pre_discard(app, args_copy, pin_ref,
                                                         sizeof pin_ref);
                    }
                    /* M886 (N50): while a child's failing run is being counted,
                     * the worktree's content before a shell call, so that only a
                     * call that changed it ends the run (below). */
                    if (app->agent_depth >= 1 && fail_run > 0 &&
                        jc_tool_name_runs_shell(gate_name)) {
                        fp_ok = (jc_snapshot_worktree_fingerprint(
                                     app->root[0] != '\0' ? app->root : app->cwd,
                                     fp_before, sizeof fp_before) == JC_OK);
                    }
                    app->cmd_stopped = 0;   /* M885: this call's own command */
                    jc_tool_execute(app->tools, name_copy, args_copy, &res, app);
                    /* D6 (M975): detection of shell edits to worker-owned paths. */
                    if (app->agent_depth == 0) {
                        const char *detection = jc_scopededit_post_tool(app);
                        if (detection != NULL) {
                            /* Append the detection notice to the tool result using jc_sb. */
                            struct jc_sb sb;
                            jc_sb_init(&sb);
                            if (res.content != NULL) {
                                jc_sb_append(&sb, res.content);
                                jc_sb_append(&sb, "\n");
                            }
                            jc_sb_append(&sb, detection);
                            free(res.content);
                            res.content = sb.data;
                            /* The tool succeeded; the notice is informational, not a failure.
                             * A successful sed must not read as a tool error. */
                            /* F5: leave res.is_error as 0 (the tool succeeded) */

                            /* Write journal lines for each changed path. The post_tool function
                             * emits telemetry events; the journal lines are written here.
                             * C89 requires declarations at block top. */
                            if (env_active(app)) {
                                const char *p;
                                char path_buf[4096];
                                size_t len;
                                cJSON *jo;
                                /* Get the changed paths from detection. Detection is a comma-separated
                                 * list: "paths changed outside the file tools: path1, path2, ..." */
                                if (strncmp(detection, "paths changed outside the file tools: ", 38) == 0) {
                                    p = detection + 38;
                                    len = 0;
                                    while (*p != '\0') {
                                        if (*p == ',') {
                                            /* End of one path */
                                            if (len > 0 && len < sizeof(path_buf)) {
                                                path_buf[len] = '\0';
                                                jo = jc_env_journal_begin(app->env, "scoped_edit");
                                                if (jo != NULL) {
                                                    cJSON_AddStringToObject(jo, "path", path_buf);
                                                    cJSON_AddStringToObject(jo, "kind", "detected");
                                                }
                                                jc_env_journal_end(app->env, jo);
                                            }
                                            len = 0;
                                            p++;
                                            /* Skip space after comma */
                                            while (*p == ' ')
                                                p++;
                                        } else {
                                            if (len < sizeof(path_buf) - 1) {
                                                path_buf[len] = *p;
                                                len++;
                                            }
                                            p++;
                                        }
                                    }
                                    /* Last path (no trailing comma) */
                                    if (len > 0 && len < sizeof(path_buf)) {
                                        path_buf[len] = '\0';
                                        jo = jc_env_journal_begin(app->env, "scoped_edit");
                                        if (jo != NULL) {
                                            cJSON_AddStringToObject(jo, "path", path_buf);
                                            cJSON_AddStringToObject(jo, "kind", "detected");
                                        }
                                        jc_env_journal_end(app->env, jo);
                                    }
                                }
                            }
                        }
                    }
                    /* M762: a fence inside the tool (the path fence) refused
                     * it, or it ran and failed -- unless a nested run or the
                     * tool's own gate has written a newer record since. */
                    if (app->why.seq == why_seq) {
                        if (res.policy_refusal) {
                            jc_app_why_note(app, name_copy, args_copy, 1,
                                "the tool refused it itself (the path fence, or "
                                "one of its own rules)", res.content);
                        } else if (res.is_error) {
                            char fl[240];
                            jc_size fn = 0;
                            const char *rc = res.content != NULL ? res.content : "";
                            while (rc[fn] != '\0' && rc[fn] != '\n' &&
                                   fn + 1 < sizeof(fl)) {
                                fl[fn] = rc[fn];
                                fn++;
                            }
                            fl[fn] = '\0';
                            app->why.failed = 1;
                            jc_snprintf(app->why.told, sizeof(app->why.told),
                                        "%s", fl);
                        }
                    }

                    app->last_run_capped = sv_capped;
                    app->last_run_budget_stopped = sv_budget;
                    app->last_run_looped = sv_looped;   /* M869 */
                    app->last_fail_cls = sv_cls;
                    app->last_fail_stopped = sv_fstop;   /* M885 */
                    app->last_fail_repeat = sv_repeat;   /* M808 */
                    jc_snprintf(app->last_fail_tool,
                                sizeof app->last_fail_tool, "%s", sv_tool);
                    jc_snprintf(app->last_fail_msg,
                                sizeof app->last_fail_msg, "%s", sv_msg);
                    jc_snprintf(app->last_model_error,
                                sizeof app->last_model_error, "%s", sv_merr);
                    app->last_model_attempts = sv_mattempts;
                    app->last_model_t0 = sv_mt0;
                    app->last_model_t1 = sv_mt1;
                    memcpy(app->fail_cls_count, sv_counts, sizeof sv_counts);
                }
                /* M806 (R6): while t0 is still live, measure this call's wall-time
                 * and, for a shell command, derive its normalised loop key and
                 * whether it was cut off at its own `timeout` bound. Both feed the
                 * loop detector below -- on the SUCCESS path too, since a command
                 * piped into `head` exits 0 yet still ran to its limit. */
                tool_dur_ms = jc_now_millis() - t0;
                if (jc_tool_name_runs_shell(gate_name)) { /* M846: run_tests too */
                    cJSON *aj = jc_json_parse(args_copy);
                    if (aj != NULL) {
                        const char *cmd = jc_json_get_str(aj, "command", NULL);
                        if (cmd != NULL) {
                            jc_toolloop_shell_norm(cmd, tool_normcmd,
                                                   sizeof tool_normcmd);
                            tool_at_bound = jc_toolloop_at_bound(cmd, tool_dur_ms);
                        }
                        cJSON_Delete(aj);
                    }
                }
                /* M1041-d: the fail_class field records the class of a FAILED call only.
                 * It is written with the telemetry event and transcript copy on the same
                 * condition as the loop detector: res.is_error || tool_at_bound. The
                 * classification is computed inside that condition to avoid re-doing it.
                 * The loop detector uses this exact predicate at line 4771. */
                {
                    int is_rec = (call_id != NULL &&
                                  strncmp(call_id, "rec-", 4) == 0);
                    cJSON *o = telem(app, "tool_call");
                    if (o != NULL) {
                        char summary[160];
                        cJSON_AddStringToObject(o, "name", name_copy);
                        if (is_rec) {
                            /* M949: this call arrived recovered from the
                             * reply text, not on the native wire. */
                            cJSON_AddStringToObject(o, "recovered", "text");
                        }
                        /* ok + error + exit, from one predicate (M536). */
                        stamp_outcome(o, res.is_error, res.exit_status);
                        /* M1041-d: write fail_class on failed calls only. This matches
                         * the loop detector's condition (res.is_error || tool_at_bound)
                         * so the field means 'the class of a FAILED call' exactly like
                         * the model_call row (which writes it only on failure). */
                        if (res.is_error || tool_at_bound) {
                            const char *content = res.content ? res.content : "";
                            enum jc_fail_class fcls = tool_at_bound
                                ? JC_FAIL_AT_BOUND
                                : jc_fail_classify(content, res.exit_status, gate_name,
                                                   res.policy_refusal);
                            cJSON_AddStringToObject(o, "fail_class",
                                                    jc_fail_class_name(fcls));
                        }
                        cJSON_AddNumberToObject(o, "duration_ms",
                                                jc_now_millis() - t0);
                        cJSON_AddNumberToObject(o, "output_bytes",
                            (double)(res.content != NULL
                                     ? (long)strlen(res.content) : 0));
                        /* M805 (P4a): free space + free inodes on the workspace
                         * filesystem, sampled the moment this tool finished. It
                         * is a MEASUREMENT, not a guard -- nothing here refuses
                         * to run; the field exists so the telemetry corpus can
                         * tell us where the real floors are before P4b turns any
                         * of them into a fence. Omitted entirely when the sample
                         * could not be taken (jc_disk_space returns non-OK), so a
                         * missing field means "unknown", never "zero free". The
                         * canonical workspace root is where edits and the pool's
                         * worktrees land; fall back to cwd before it is set. */
                        {
                            const char *fs = app->root[0] != '\0'
                                             ? app->root : app->cwd;
                            double free_mb = -1.0, free_files = -1.0;
                            if (fs[0] != '\0' &&
                                jc_disk_space(fs, &free_mb, &free_files)
                                    == JC_OK) {
                                cJSON_AddNumberToObject(o, "disk_free_mb",
                                                        free_mb);
                                cJSON_AddNumberToObject(o, "disk_free_files",
                                                        free_files);
                            }
                        }
                        jc_tool_arg_summary(name_copy, args_copy,
                                            summary, sizeof(summary));
                        if (summary[0] != '\0') {
                            /* M155: the metrics-tier args summary carries a
                             * slice of the command/path and was NOT scrubbed
                             * (only full-tier fields were) -- route it through
                             * the redactor so a secret in a command can't leak
                             * to the metrics log. jc_eventlog_add_text redacts
                             * + is UTF-8-safe; 0 = no truncation (already 160). */
                            jc_eventlog_add_text(o, "args", summary, 0);
                        }
                        /* M715: a read's RANGE beside its path -- two numbers, no
                         * content -- so paging and re-reading are different
                         * events. From the result, i.e. as the tool executed it;
                         * the summary above stays the path alone, because it is
                         * also what the TUI prints and a listener hears. */
                        if (res.has_range) {
                            cJSON_AddNumberToObject(o, "offset",
                                                    (double)res.range_offset);
                            cJSON_AddNumberToObject(o, "limit",
                                                    (double)res.range_limit);
                        }
                        if (jc_eventlog_full(app->telemetry)) {
                            jc_eventlog_add_text(o, "args_full", args_copy,
                                                 JC_EVENTLOG_TEXT_MAX);
                            jc_eventlog_add_text(o, "output",
                                res.content != NULL ? res.content : "",
                                JC_EVENTLOG_TEXT_MAX);
                        }
                    }
                    jc_eventlog_end(app->telemetry, o);
                    /* M807: mirror this call into a child's transcript (name,
                     * outcome, exit, redacted args + output). Same reasoning
                     * record as the model reply above; NULL for a lead run. */
                    if (app->transcript != NULL) {
                        cJSON *to = jc_eventlog_begin(app->transcript, "tool_call");
                        if (to != NULL) {
                            cJSON_AddStringToObject(to, "name", name_copy);
                            stamp_outcome(to, res.is_error, res.exit_status);
                            /* M1041-d: write fail_class on failed calls only. The same
                             * condition as the telemetry event and loop detector:
                             * res.is_error || tool_at_bound. */
                            if (res.is_error || tool_at_bound) {
                                const char *content = res.content ? res.content : "";
                                enum jc_fail_class fcls = tool_at_bound
                                    ? JC_FAIL_AT_BOUND
                                    : jc_fail_classify(content, res.exit_status, gate_name,
                                                       res.policy_refusal);
                                cJSON_AddStringToObject(to, "fail_class",
                                                        jc_fail_class_name(fcls));
                            }
                            cJSON_AddNumberToObject(to, "duration_ms",
                                                    tool_dur_ms);
                            jc_eventlog_add_text(to, "args", args_copy,
                                                 JC_EVENTLOG_TEXT_MAX);
                            jc_eventlog_add_text(to, "output",
                                res.content != NULL ? res.content : "",
                                JC_EVENTLOG_TEXT_MAX);
                        }
                        jc_eventlog_end(app->transcript, to);
                    }
                }
            }
            /* PostToolUse hooks (M25): observe the result; any additional
             * context the hook prints is folded into what the model sees. */
            {
                const char *content = res.content ? res.content : "";
                struct jc_sb combined;
                int have_combined = 0;
                const char *redo_note = NULL;
                /* M105 redo-loop guard: a successful edit to the same file past
                 * the threshold gets a one-time nudge folded into its result, so
                 * a thrashing agent is told to step back before it burns the
                 * budget re-editing (ANECDOTES #8/#9). Top-level only. */
                if (!res.is_error && app->agent_depth == 0 &&
                    (strcmp(gate_name, "edit_file") == 0 ||
                     strcmp(gate_name, "write_file") == 0 ||
                     strcmp(gate_name, "apply_patch") == 0)) {
                    cJSON *aj = jc_json_parse(args_copy);
                    const char *path = NULL;
                    if (aj != NULL) {
                        path = jc_json_get_str(aj, "path", NULL);
                        if (path == NULL) { /* apply_patch: edits[].path */
                            cJSON *ed = cJSON_GetObjectItem(aj, "edits");
                            if (cJSON_IsArray(ed) && cJSON_GetArraySize(ed) > 0) {
                                path = jc_json_get_str(
                                    cJSON_GetArrayItem(ed, 0), "path", NULL);
                            }
                        }
                    }
                    if (jc_editwatch_bump(&editwatch, path) ==
                        JC_SELFHEAL_REDO_THRESHOLD) {
                        redo_note = "You have edited this file several times "
                            "this turn. Repeated edits to the same file usually "
                            "signal a wrong approach -- re-read the current file, "
                            "reconsider the plan, and avoid another blind edit.";
                    }
                    if (aj != NULL) {
                        cJSON_Delete(aj);
                    }
                }
                if (jc_hooks_active(app)) {
                    struct jc_hook_result hr;
                    jc_hook_result_init(&hr);
                    jc_hooks_fire(app, JC_HOOK_POST_TOOL, name_copy, args_copy,
                                  content, res.is_error, NULL, &hr);
                    if (hr.context.len > 0) {
                        jc_sb_init(&combined);
                        jc_sb_append(&combined, content);
                        jc_sb_append(&combined, "\n\n[hook] ");
                        jc_sb_append(&combined, hr.context.data);
                        have_combined = 1;
                    }
                    jc_hook_result_free(&hr);
                }
                if (redo_note != NULL) {
                    if (!have_combined) {
                        jc_sb_init(&combined);
                        jc_sb_append(&combined, content);
                        have_combined = 1;
                    }
                    jc_sb_append(&combined, "\n\n[jichi] ");
                    jc_sb_append(&combined, redo_note);
                }
                /* M852 (N36): in a write child whose shell M831 fences, a shell
                 * command that failed on a permission error was most likely a
                 * write the fence refused -- and nothing said so: the kernel
                 * refuses, the tool prints its own error ("Cannot create
                 * temporary file ...: Permission denied", measured), and the
                 * model chases the build instead of the fence. Fold in one line
                 * naming the fence, worded as a possibility because a READ
                 * error matches the same text. Counted, so the batch result can
                 * say how often it happened. */
                if (app->shell_fenced && jc_tool_name_runs_shell(gate_name) &&
                    jc_landlock_permission_error(content)) {
                    if (!have_combined) {
                        jc_sb_init(&combined);
                        jc_sb_append(&combined, content);
                        have_combined = 1;
                    }
                    jc_sb_append(&combined,
                        "\n\n[jichi] This shell is fenced (M831): it may write only "
                        "in your worktree, in your temp dir ($TMPDIR), and in the "
                        "directories the operator listed in shellFenceAllow. If "
                        "the error above is a write elsewhere being refused, write "
                        "inside your worktree instead, or say in your answer that "
                        "the task needs that directory.");
                    app->fence_hints++;
                }
                /* M863: the state kept before this command, named where the
                 * model reads the command's result. */
                if (pin_rc != 0) {
                    if (!have_combined) {
                        jc_sb_init(&combined);
                        jc_sb_append(&combined, content);
                        have_combined = 1;
                    }
                    if (pin_rc > 0) {
                        jc_sb_append_fmt(&combined,
                            "\n\n[jichi] Before this command, jichi kept your "
                            "worktree as it was -- uncommitted changes, untracked "
                            "files and HEAD -- as %s. `git diff %s` shows what the "
                            "command changed; `git checkout %s -- <path>` brings a "
                            "file back. To compare with the base without discarding "
                            "anything, use `git diff` or `git show HEAD:<path>`.",
                            pin_ref, pin_ref, pin_ref);
                    } else {
                        jc_sb_append(&combined,
                            "\n\n[jichi] jichi could not keep your worktree before "
                            "this command, so what it discarded is not kept.");
                    }
                }
                /* M855 (N34): in a parallel write child, `git stash` uses the
                 * shadow repo's ONE stash stack, which every sibling's worktree
                 * shares (measured, git 2.43: one worktree's `git stash pop`
                 * applied another's entry), and a stashed change is not in the
                 * worktree jichi keeps or merges. Say so where the model acts on
                 * it -- the drive's children stashed 57 times in seven runs. */
                if (app->parallel_write &&
                    (strcmp(gate_name, "git_stash") == 0 ||
                     (jc_tool_name_runs_shell(gate_name) &&
                      args_runs_git_stash(args_copy)))) {
                    if (!have_combined) {
                        jc_sb_init(&combined);
                        jc_sb_append(&combined, content);
                        have_combined = 1;
                    }
                    jc_sb_append(&combined,
                        "\n\n[jichi] In a parallel write child, the stash is "
                        "shared with every other child of this batch: a `git stash "
                        "pop` can apply another child's change, and a stashed "
                        "change is not part of the work jichi keeps and merges for "
                        "you. Commit instead: a commit in your worktree is kept.");
                }
                /* M432: the in-turn loop detector. Folded in HERE, beside the M105
                 * redo note, because this is the one point where the failure text
                 * is still in hand -- at the `metrics` telemetry tier a tool_call
                 * event carries only ok:false and no reason, so an offline reader
                 * could see a loop's shape and never its cause.
                 *
                 * FAILED calls only, and deliberately NOT the two classes already
                 * owned elsewhere: a policy block never reaches here (it goes
                 * through block_message, M429, whose advice is the opposite) and a
                 * verify is not a tool call (M89). Any depth: a subagent loops just
                 * as easily, and the note goes into whichever history is looping. */
                if (res.is_error || tool_at_bound) {
                    /* M806 (R6): a call cut off at its OWN time limit is a loop
                     * signal whatever its exit code -- so at-bound enters here even
                     * when the pipeline reported 0. When it did, the class is
                     * AT_BOUND and the key is the normalised command, so the same
                     * check spelled six ways collapses to one exact key. */
                    enum jc_fail_class fcls = tool_at_bound
                        ? JC_FAIL_AT_BOUND
                        : jc_fail_classify(content, res.exit_status, gate_name,
                                           res.policy_refusal);
                    const char *loopkey = tool_at_bound ? tool_normcmd : args_copy;
                    int lcount = 0;
                    enum jc_toolloop_verdict lv;
                    /* M437: record it for the delegation report -- but only a
                     * genuine error is a failure to report to a parent; an at-bound
                     * SUCCESS is not a failure, only a loop signal. The class is
                     * already computed here, exactly the distinction a parent
                     * needed, so the report costs one copy, not a new analysis. */
                    if (res.is_error) {
                        jc_snprintf(app->last_fail_tool,
                                    sizeof app->last_fail_tool, "%s", name_copy);
                        jc_snprintf(app->last_fail_msg,
                                    sizeof app->last_fail_msg, "%s",
                                    (content != NULL) ? content : "");
                        app->last_fail_cls = (int)fcls;
                        /* M885 (N47, continued): a shell call whose command
                         * jc_app_run_command_ex killed because this run was
                         * being stopped -- the call did not meet a limit. */
                        app->last_fail_stopped =
                            (jc_tool_name_runs_shell(name_copy) &&
                             app->cmd_stopped) ? 1 : 0;
                        if ((int)fcls >= 0 && (int)fcls < JC_FAIL_CLASS_COUNT) {
                            app->fail_cls_count[fcls]++;   /* M876 (N47 (c)) */
                        }
                    }
                    lv = jc_toolloop_note(&toolloop, name_copy, loopkey, fcls,
                                          &lcount);
                    /* M869 (N27): a CHILD's failure loop ends it, as M829 ends a
                     * success loop -- M432 only warned, so run 0015's worker sent one
                     * refused call 742 times until an overflow ended it. (a) the same
                     * refused call at M829's threshold (the exact key: the same tool
                     * and arguments); (b) copied placeholders three in a row, sooner,
                     * because arguments that are gone cannot be rephrased back. */
                    if (content != NULL &&
                        strncmp(content, "error: those are not arguments", 30) == 0) {
                        placeholder_run++;
                    } else {
                        placeholder_run = 0;
                    }
                    {
                        unsigned long kn = (unsigned long)strlen(loopkey);
                        unsigned long kh = jc_reread_hash(loopkey, kn) ^
                            (jc_reread_hash(name_copy,
                                            (unsigned long)strlen(name_copy)) * 31UL);
                        if (fail_run > 0 && kh == fail_key_h && kn == fail_key_n) {
                            fail_run++;
                        } else {
                            fail_key_h = kh;
                            fail_key_n = kn;
                            fail_run = 1;
                        }
                    }
                    if (app->agent_depth >= 1 && noprog_stop == 0) {
                        if (placeholder_run >= JC_PLACEHOLDER_STOP_AT) {
                            noprog_stop = 3;
                            app->last_fail_repeat = placeholder_run;
                        } else if (fail_run >= JC_NOPROGRESS_STOP_AT) {
                            noprog_stop = 2;
                            app->last_fail_repeat = fail_run;
                        }
                    }
                    if (lv != JC_TOOLLOOP_NONE) {
                        char lnote[600];
                        /* M808: remember the repeat count for the end record's
                         * `repeated` line -- the same counter, surfaced, not a new
                         * one. Highest wins, so a later smaller loop cannot mask it. */
                        if (lcount > app->last_fail_repeat) {
                            app->last_fail_repeat = lcount;
                        }
                        jc_toolloop_render(lv, name_copy, fcls, lcount,
                                           lnote, sizeof lnote);
                        if (lnote[0] != '\0') {
                            if (!have_combined) {
                                jc_sb_init(&combined);
                                jc_sb_append(&combined, content);
                                have_combined = 1;
                            }
                            jc_sb_append(&combined, lnote);
                        }
                        jc_logf(JC_LOG_WARN,
                                "tool loop: %s failed %dx this turn (%s, %s key)",
                                name_copy, lcount, jc_fail_class_name(fcls),
                                (lv == JC_TOOLLOOP_EXACT) ? "exact" : "class");
                        /* M422's lesson: the fix is not finished at the model. A
                         * detector that only tells the model leaves the operator's
                         * table blank, which is how mid-turn thrashing stayed
                         * invisible in `runs`. Journal it, top-level. */
                        if (env_active(app)) {
                            cJSON *lo = jc_env_journal_begin(app->env, "tool_loop");
                            if (lo != NULL) {
                                cJSON_AddStringToObject(lo, "name", name_copy);
                                cJSON_AddStringToObject(lo, "class",
                                                        jc_fail_class_name(fcls));
                                cJSON_AddStringToObject(lo, "key",
                                    (lv == JC_TOOLLOOP_EXACT) ? "exact" : "class");
                                cJSON_AddNumberToObject(lo, "repeat",
                                                        (double)lcount);
                            }
                            jc_env_journal_end(app->env, lo);
                        }
                        if (cb != NULL && cb->on_status != NULL) {
                            char lb[160];
                            jc_snprintf(lb, sizeof lb,
                                        "tool loop: %s failed %dx (%s)",
                                        name_copy, lcount,
                                        jc_fail_class_name(fcls));
                            cb->on_status(cb->user, lb);
                        }
                    }
                } else {
                    /* D1 (M733): the success twin of the block above. The same
                     * call answered the same way three times is a loop M432
                     * cannot see -- 0 errors, 200 calls (M687). The role table
                     * and the reset rules are jc_toolloop.h's; the threshold is
                     * the M731 fit. Any depth, like M432: the watch is this
                     * (sub)run's own, and the note goes into the history that
                     * is looping. */
                    enum jc_noprogress_role prole = jc_noprogress_role(
                        gate_name, tool != NULL ? tool->readonly : 0);
                    placeholder_run = 0;   /* M869: a call that worked breaks the run */
                    /* M895 (the demos drive's N54 (b)): a command that succeeds and
                     * prints nothing, again and again in one turn. Six workers re-ran
                     * a passing `zig build test` looking for output -- nine times in
                     * run 0106 -- spelled differently each time, so neither M829 nor
                     * M870 saw a repeat. Counted by the normalised command; from the
                     * third, the result says what the silence means. */
                    if (jc_tool_name_runs_shell(gate_name) && res.exit_status == 0 &&
                        tool_normcmd[0] != '\0' && content != NULL &&
                        strncmp(content, JC_RUN_SILENT_OK,
                                sizeof JC_RUN_SILENT_OK - 1) == 0) {
                        int sc = silent_count(silent_h, silent_n, silent_c, &nsilent,
                                              tool_normcmd);
                        if (sc >= JC_SILENT_NOTE_AT) {
                            char snote[420];
                            jc_snprintf(snote, sizeof snote,
                                "\n[jichi] This command has succeeded silently %d "
                                "times in this turn. Exit status 0 with no output "
                                "means it passed: it prints nothing when it "
                                "succeeds, and running it again on an unchanged tree "
                                "will not show more.", sc);
                            if (!have_combined) {
                                jc_sb_init(&combined);
                                jc_sb_append(&combined, content);
                                have_combined = 1;
                            }
                            jc_sb_append(&combined, snote);
                            jc_logf(JC_LOG_INFO,
                                    "silent success: %s succeeded with no output %dx "
                                    "this turn", name_copy, sc);
                        }
                    }
                    /* M869, amended: a change ends a failing run -- an edit, or a
                     * shell command that worked AND changed the worktree's content
                     * (M886, the demos drive's N50). Run 0104's probe read code with
                     * `sed -n` and `grep` between 15 runs of one failing command;
                     * every read reset the count, so the stop at 9 never came.
                     * Content, not `git status`: a second edit to a file already
                     * modified keeps its status line. When it cannot tell -- the top
                     * level, no failing run, no git -- it resets, as before. */
                    if (prole == JC_NOPROGRESS_RESET) {
                        fail_run = 0;
                    } else if (jc_tool_name_runs_shell(gate_name)) {
                        char fp_after[72];
                        if (!fp_ok ||
                            jc_snapshot_worktree_fingerprint(
                                app->root[0] != '\0' ? app->root : app->cwd,
                                fp_after, sizeof fp_after) != JC_OK ||
                            strcmp(fp_before, fp_after) != 0) {
                            fail_run = 0;
                        }
                    }
                    if (prole == JC_NOPROGRESS_RESET) {
                        jc_noprogress_reset(&noprog);
                    } else if (prole == JC_NOPROGRESS_COUNT) {
                        int pcount = jc_noprogress_note(&noprog, gate_name,
                                                        args_copy, content);
                        /* M829 (N4b): a CHILD (depth >= 1) whose whole task is one
                         * turn has no next turn to change course, so end it when the
                         * same successful call reaches the stop threshold (after the
                         * warnings at 3 and 6). Record the repeat count for its end
                         * record (M808). The top level (depth 0) only warns -- it has
                         * more turns and an operator. */
                        if (pcount >= JC_NOPROGRESS_STOP_AT &&
                            app->agent_depth >= 1) {
                            noprog_stop = 1;
                            app->last_fail_repeat = pcount;
                        }
                        /* M870 (N21): a child cycling through calls it has made
                         * before, in any order, with nothing new among the last
                         * JC_NOVELTY_WINDOW -- the shape M829 cannot see. */
                        if (noprog.stale >= JC_NOVELTY_WINDOW &&
                            app->agent_depth >= 1 && noprog_stop == 0) {
                            noprog_stop = 4;
                            app->last_fail_repeat = noprog.stale;
                        }
                        if (pcount > 0) {
                            char pnote[400];
                            jc_noprogress_render(name_copy, pcount,
                                                 pnote, sizeof pnote);
                            if (!have_combined) {
                                jc_sb_init(&combined);
                                jc_sb_append(&combined, content);
                                have_combined = 1;
                            }
                            jc_sb_append(&combined, pnote);
                            jc_logf(JC_LOG_WARN,
                                    "no progress: %s returned the same result "
                                    "%dx this turn", name_copy, pcount);
                            /* The operator's table, not only the model's
                             * history -- M422's lesson, as for tool_loop. */
                            if (env_active(app)) {
                                cJSON *po = jc_env_journal_begin(app->env,
                                                                 "no_progress");
                                if (po != NULL) {
                                    cJSON_AddStringToObject(po, "name",
                                                            name_copy);
                                    cJSON_AddNumberToObject(po, "repeat",
                                                            (double)pcount);
                                }
                                jc_env_journal_end(app->env, po);
                            }
                            if (cb != NULL && cb->on_status != NULL) {
                                char pb[160];
                                jc_snprintf(pb, sizeof pb,
                                            "no progress: %s returned the "
                                            "same result %dx", name_copy,
                                            pcount);
                                cb->on_status(cb->user, pb);
                            }
                        }
                    }
                }
                /* M734: the inferred rule this call went against, told once a
                 * turn per rule. After the call, because advice about a call
                 * that never ran would be noise, and in the result, because
                 * that is what the model reads when it decides what next. */
                if (advise_rule >= 0 && advise_rule < JC_CONSTRAINT_MAX &&
                    !advised[advise_rule]) {
                    char anote[800];
                    /* M872 (N45 (b)): with the words it was read from, so the
                     * model can tell a rule the request states from one jichi
                     * misread -- the judgement the note asks it to make. */
                    const char *asrc = (advise_rule < app->n_constraints)
                        ? app->constraints[advise_rule].source : NULL;
                    advised[advise_rule] = 1;
                    jc_snprintf(anote, sizeof anote,
                        "\n\n[jichi] note: this call goes against a rule jichi "
                        "inferred from your request -- \"%s\"%s%s%s. Inferred "
                        "rules are advisory, so it ran. If the request really "
                        "forbids this, stop and say so in your final answer.",
                        advise_text,
                        asrc != NULL ? ", read from: \"" : "",
                        asrc != NULL ? asrc : "",
                        asrc != NULL ? "\"" : "");
                    if (!have_combined) {
                        jc_sb_init(&combined);
                        jc_sb_append(&combined, content);
                        have_combined = 1;
                    }
                    jc_sb_append(&combined, anote);
                    jc_logf(JC_LOG_WARN,
                            "[constraint] %s went against an inferred rule "
                            "(advisory, not enforced): %s%s%s%s", name_copy,
                            advise_text,
                            asrc != NULL ? " (from \"" : "",
                            asrc != NULL ? asrc : "",
                            asrc != NULL ? "\")" : "");
                }
                jc_history_add_tool_result(hist, call_id,
                    have_combined ? combined.data : content, res.is_error);
                if (!res.is_error && strcmp(name_copy, "read_file") == 0) {
                    round_read = 1; /* M218: a new superseded pair is possible */
                }
                if (cb != NULL && cb->on_tool_result != NULL) {
                    /* Show what the MODEL actually received -- including any
                     * PostToolUse [hook] context or [jichi] redo note -- so the
                     * TUI/jsonl observer sees the same tool result the model
                     * does, not the pre-injection raw output. */
                    cb->on_tool_result(cb->user, name_copy,
                                       have_combined ? combined.data : content,
                                       res.is_error,
                                       (call_id != NULL) ? call_id : "");
                }
                if (have_combined) {
                    jc_sb_free(&combined);
                }
            }
            /* The ATTEMPT meter moved up to call_id (M459). What remains here
             * is the EXECUTED count -- this point is past every gate, so a call
             * reaching it really ran -- and the JOURNAL row, which stays
             * top-level: a forked parallel child appending to the same file
             * would interleave with its siblings, and the parent already
             * reconciles each child's piped count. */
            if (env_budget_applies(app)) {
                app->env->tool_calls_executed++;
                /* M892: a call that can change a file -- anything but a READ.
                 * The fallback witness, where git cannot fingerprint the
                 * worktree, for "nothing changed after the failed check". */
                if (jc_agent_tool_category(gate_name) != JC_TOOLCAT_READ) {
                    app->env->changing_executed++;
                }
            }
            if (env_active(app)) {
                cJSON *jo;
                jo = jc_env_journal_begin(app->env, "tool_call");
                if (jo != NULL) {
                    cJSON_AddStringToObject(jo, "name", name_copy);
                    /* Same stamper as the telemetry row above, so the two sinks
                     * a supervisor joins on `run` cannot disagree (M536). */
                    stamp_outcome(jo, res.is_error, res.exit_status);
                }
                jc_env_journal_end(app->env, jo);
            }
            /* Optional routing escalation when a tool MALFUNCTIONS. A red build
             * or a failing test is not a malfunction -- it is the gate doing its
             * job inside a fix-forward loop -- and escalating on it fired on
             * nearly every turn, which is why the flag had to be left off in
             * practice (M286; see jc_tool_result_is_malfunction). */
            if (jc_tool_result_is_malfunction(&res) &&
                app->config.routing.escalate_on_error &&
                route_escalate(app, "tool_error")) {
                prov = app->provider;
            }
            jc_tool_result_free(&res);
            /* Single exit for the per-call copies (M218). Early-skip paths
             * jump here (res never populated on those, so its free is
             * correctly above the label); normal flow falls through. */
        next_call:
            free(name_copy);
            free(args_copy);
            free(id_copy);
            /* M438: serve the socket BETWEEN the tool calls of this round, not
             * only after all of them. A round with five calls used to give one
             * service point, so one slow call (a build, a test suite) left
             * `status` and `abort` unanswered for its whole duration -- against a
             * client wait of 300s. Poll-only: an inject stays queued for the round
             * boundary below, because a user message between two tool results of
             * the same round is malformed (M364's contract). */
            jc_control_poll(app);
        }
        /* M1083 (D-08): the tool-round boundary -- every call of THIS round has
         * its result landed, so the history is a consistent prefix. Persist the
         * live session here (top-level only, throttled, durable) so a SIGKILL /
         * OOM-kill mid-turn loses at most the in-flight round rather than the
         * whole conversation. Placed BEFORE the control/steering fold-in below:
         * a boundary that injects a new user message must not land on disk while
         * the round it follows is still only half-saved -- the saved history
         * must always be the consistent prefix, never a prefix that stops in the
         * middle of an in-flight round. */
        agent_loop_midturn_persist(app, hist);
        /* M853 (N32): a delegate's finish-like call ended its turn (above).
         * Every call of the round has its result; the message becomes the
         * assistant's reply, the shape of any final answer. */
        if (finish_answer != NULL) {
            jc_history_add(hist, JC_ROLE_ASSISTANT, finish_answer);
            return JC_OK;
        }
        /* M159: the control-channel boundary. Serve any waiting supervisor
         * commands (status/inject/pause/resume/abort) now that the world is
         * consistent -- one zero-timeout poll; a pause blocks here in slices;
         * queued steering lands as one "[operator]" user message the NEXT
         * model call sees. Top-level only; NULL app->control => no-op. */
        if (jc_control_boundary(app, hist) &&
            cb != NULL && cb->on_status != NULL) {
            cb->on_status(cb->user, "operator steering injected");
        }
        /* M254: the SAME boundary, for the human at the keyboard. The TUI
         * collects keystrokes typed while the agent works (they used to be
         * echoed into the output and then discarded); anything the user
         * committed with Enter lands here as one "[operator]" user message the
         * next model call sees -- identical shape to a control-socket inject,
         * so the M31 cached prefix stays byte-stable. Top-level only: a
         * subagent's history is its own, and steering belongs to the main
         * conversation. */
        if (app->agent_depth == 0 && cb != NULL && cb->take_input != NULL) {
            char *typed = cb->take_input(cb->user);
            if (typed != NULL) {
                if (jc_history_add_operator(hist, typed) != NULL &&
                    cb->on_status != NULL) {
                    cb->on_status(cb->user, "queued input applied");
                }
                free(typed);
            }
        }
        /* M347: the SAME boundary, for the envelope itself. The human reads
         * ctx% and $ live in the TUI prompt; the agent flying a bounded run
         * was told nothing until the engine stopped -- 0/7 implementation
         * runs completed in the 2026-08-07 driving session, every one
         * budget_exhausted, and M96's starved analysis dies with its report
         * unwritten. Once per run, at the first ~80% crossing of any armed
         * budget, one [envelope] line lands as a user message the next model
         * call sees (the control-inject shape, so the M31 cached prefix
         * stays byte-stable). Top-level only, like steering; a subagent's
         * iteration budget is its own affair (M62 tapers it). */
        if (app->env != NULL && app->agent_depth == 0 &&
            !app->env->budget_noticed) {
            enum jc_env_budget nk =
                jc_env_budget_notice_due(app->env, jc_now_seconds());
            if (nk != JC_BUDGET_NONE) {
                struct jc_sb nb;
                cJSON *o;
                app->env->budget_noticed = 1;
                jc_sb_init(&nb);
                jc_env_budget_notice_render(app->env, jc_now_seconds(), &nb);
                if (nb.data != NULL && nb.len > 0) {
                    jc_history_add(hist, JC_ROLE_USER, nb.data);
                }
                o = jc_env_journal_begin(app->env, "budget_notice");
                if (o != NULL) {
                    cJSON_AddStringToObject(o, "kind",
                                            jc_env_budget_name(nk));
                }
                jc_env_journal_end(app->env, o);
                if (cb != NULL && cb->on_status != NULL) {
                    cb->on_status(cb->user,
                                  "budget notice: ~80% used, told the agent");
                }
                jc_sb_free(&nb);
            }
        }
        /* M431f: the ambient panel, at the same boundary and by the same mechanism
         * as M347's one-shot notice -- one user-role message the next model call
         * reads, so the M31 cached prefix stays byte-stable. OFF unless armed.
         *
         * Top-level only, matching M347's own reasoning: the PARENT paces the run,
         * and a subagent's budget is the M62 iteration taper rather than the run
         * envelope. The rate's denominator still counts every depth's calls, so a
         * delegating run's reading does not read low. */
        if (app->env != NULL && app->agent_depth == 0 &&
            jc_env_panel_due(app->env, app->config.budget_panel_every)) {
            struct jc_sb pb;
            jc_sb_init(&pb);
            jc_env_panel_render(app->env, jc_now_seconds(), &pb);
            if (pb.data != NULL && pb.len > 0) {
                jc_history_add(hist, JC_ROLE_USER, pb.data);
            }
            app->env->panel_last_call = app->env->tool_calls;
            app->env->panel_tokens = app->env->tokens_used;
            if (cb != NULL && cb->on_status != NULL) {
                cb->on_status(cb->user, "budget panel: reading given to the agent");
            }
            jc_sb_free(&pb);
        }

        /* M76: mid-turn compaction. A single turn's tool churn can grow the
         * history past the model's context window, which between-turn
         * compaction can't help. Bound it by eliding old tool-output content
         * (structure preserved, no model call). Runs at any depth. */
        {
            struct jc_midturn_report mrep;
            jc_size ndup, nargs, nc;
            int note_now;
            nc = jc_compact_midturn(app, hist, cb, &mrep,
                                    first_midturn || round_read, &mlatch);
            ndup = mrep.dup;
            nargs = mrep.args;
            first_midturn = 0;
            /* M920: the relay's decision, from the pass just measured -- the
             * five triggers of proposal 6.2 as a pure function, so the loop
             * never re-derives the policy. Off by default (config.relay = 0,
             * the core then returns CONTINUE on every signal). The check runs
             * at EVERY round's compaction point, so the unrelieved streak is
             * consecutive passes; the handover itself waits for the round
             * boundary below, where the history is whole. */
            /* Only at depth 0: a nested delegate runs on the SAME app, and
             * relay_handover is a lead-level flag -- a child's exhausted
             * window must not end the LEAD's run as a handover (the M347/
             * last_run_capped depth rule, the same reason). */
            if (app->config.relay && app->agent_depth == 0 && !relay_stop) {
                struct relay_signal rs;
                struct relay_cfg rc;
                enum relay_action ra;
                enum relay_reason rr;
                rs.pressed = mrep.pressed;
                rs.reached = mrep.reached;
                rs.unrelieved = mrep.unrelieved;
                rs.latched = mrep.latched;
                rs.overflow = app->ctx_overflow;
                rs.server_tokens = app->last_prompt_tokens;
                rs.limit = mrep.limit;
                /* M951 (E5, V2): the leg budget, wired. The cap is the
                 * operator's (--max-tool-calls / the seeded leg's baton
                 * budget, which by construction is <= it), and the leg's tool
                 * calls are counted env-independently. 0 cap = E5 off: the
                 * trigger can never fire and the decision is unchanged from
                 * M920. Topology-B legs (leg count) stay next milestone. */
                {
                    jc_size e5_cap =
                        (app->relay_leg_calls_prev > 0)
                            ? (jc_size)app->relay_leg_calls_prev
                            : (app->env != NULL &&
                               app->env->max_tool_calls > 0)
                              ? (jc_size)app->env->max_tool_calls : 0;
                    rs.leg_calls = (e5_cap > 0)
                                         ? (jc_size)app->relay_leg_calls
                                         : 0;
                    rc.leg_calls_max = e5_cap;
                }
                rc.relay_on = 1;
                rc.trigger = 0;
                rc.hard_pct = 90;
                rc.legs_max = 1;
                ra = relay_decide(&rs, &rc, &relay_st, &rr);
                if (ra != JC_RELAY_CONTINUE) {
                    relay_stop = 1;
                    /* M921 (sub-slice B): keep the trigger's name for the
                     * baton's header (the headless tail writes the notes
                     * where rr is out of scope). relay_reason_name is
                     * "unknown" at worst, never NULL. */
                    jc_snprintf(app->relay_reason_name,
                                sizeof(app->relay_reason_name), "%s",
                                relay_reason_name(rr));
                    jc_logf(JC_LOG_WARN,
                            "[relay] window exhausted (%s) -- ending this run "
                            "at the round boundary; the next leg continues "
                            "from the notes",
                            relay_reason_name(rr));
                }
            }
            /* M358: the dynamic half of the context gauge -- the operator
             * already had warned_short and the TUI ctx%, the model had only the
             * per-message elision markers, which explain one output and never
             * say "change how you read". M900 (C1): STANDING -- every PRESSED
             * pass tells the model (not the first, once, and never again), so
             * the water-level reading continues for as long as the window is
             * over the high-water, not a single alarm at the edge. Any depth:
             * each (sub)agent flies its own window. */
            note_now = mrep.pressed;
            /* M323: emit when the pass ELIDED something OR when it ran under
             * pressure and could not get there. The second case used to produce
             * no event at all, and it is the one worth seeing: a workload whose
             * history is many small tool results gives this pass nothing to
             * elide, so it fires every round and the request still goes out over
             * the configured limit. */
            if (nc > 0 || mrep.pressed) {
                cJSON *o = telem(app, "compact");
                if (o != NULL) {
                    cJSON_AddStringToObject(o, "phase", "midturn");
                    cJSON_AddNumberToObject(o, "elided", (double)nc);
                    /* Calibrated real-token terms -- the same units the trigger
                     * compares, so a reader can check the decision. */
                    cJSON_AddNumberToObject(o, "before", (double)mrep.before);
                    cJSON_AddNumberToObject(o, "after", (double)mrep.after);
                    cJSON_AddNumberToObject(o, "target", (double)mrep.target);
                    cJSON_AddNumberToObject(o, "limit", (double)mrep.limit);
                    /* M326x: `pressed` says whether the high-water trigger
                     * actually fired. Without it a reader cannot tell the eager
                     * zero-loss dedup (which runs every round and has no target
                     * to miss) from a pass that was the last thing between the
                     * request and the limit -- and 44% of one workload's 1,057
                     * mid-turn events were the former. */
                    cJSON_AddBoolToObject(o, "pressed", mrep.pressed);
                    /* M348: how many elisions left a claim ticket (the marker
                     * names a preservation-store path). Additive field; jsonl
                     * consumers ignore unknown keys per EMBEDDING.md. */
                    if (mrep.preserved > 0) {
                        cJSON_AddNumberToObject(o, "preserved",
                                                (double)mrep.preserved);
                    }
                    /* M358: this pass also injected the [context] note, so a
                     * reader can join "the model was told" to what it did
                     * next. Additive, present only when it fired. */
                    if (note_now) {
                        cJSON_AddBoolToObject(o, "noticed", 1);
                    }
                    /* M361: the lossy trims were skipped by the exhaustion
                     * latch -- the pressure is real (pressed, above), the
                     * work was not re-done. Additive, present only when
                     * latched, so a reader can split thrash from effort. */
                    if (mrep.latched) {
                        cJSON_AddBoolToObject(o, "latched", 1);
                    }
                    /* And `short` is meaningful ONLY when pressed: an
                     * unpressured pass never had a target to fall short of. It
                     * was emitted as `!reached` alone, so every eager dedup
                     * logged short:true (reached is 0 from the memset), and the
                     * summarizer duly reported "requests went out over the
                     * configured contextLimit" about requests that had not.
                     * 19 of 19 such events in the measured workload were false. */
                    cJSON_AddBoolToObject(o, "short",
                                          mrep.pressed && !mrep.reached);
                    /* M326y: pressed and STILL above the high-water, so this
                     * fires again next round -- the thrash, stated exactly.
                     * Eliding cannot relieve a turn in this state; only smaller
                     * tool output or a different mechanism can. */
                    cJSON_AddBoolToObject(o, "unrelieved",
                                          mrep.pressed && mrep.unrelieved);
                    /* M192: split the zero-loss dedup from the lossy age-based
                     * pass. `elided` alone cannot say whether M93/M94 is doing
                     * the work or the fallback is, which made the dedup's
                     * effectiveness unmeasurable. `dup + age` stays the
                     * CONTENT elisions; `args` (M218) counts tool calls whose
                     * ARGUMENTS were elided. dup + age + args == elided. */
                    cJSON_AddNumberToObject(o, "dup", (double)ndup);
                    cJSON_AddNumberToObject(o, "age",
                                            (double)(nc - ndup - nargs));
                    cJSON_AddNumberToObject(o, "args", (double)nargs);
                }
                jc_eventlog_end(app->telemetry, o);
                /* M323: say it ONCE per turn. The condition persists for every
                 * remaining round -- a history of many small results gives this
                 * pass nothing to elide -- so warning per round would bury the
                 * one fact the operator needs: their contextLimit is not being
                 * honoured, and no amount of eliding will change it. */
                if (mrep.pressed && !mrep.reached) {
                  if (!warned_short) {
                    warned_short = 1;
                    jc_logf(JC_LOG_WARN,
                            "[compact] mid-turn cannot reach its target: "
                            "~%ld real tok vs target ~%ld (contextLimit ~%ld). "
                            "This history is many small tool results rather than "
                            "a few large ones, so there is little left to elide "
                            "-- requests may exceed the limit",
                            mrep.after, mrep.target, mrep.limit);
                    if (cb != NULL && cb->on_status != NULL) {
                        cb->on_status(cb->user,
                            "context over budget: mid-turn compaction has "
                            "little left to elide");
                    }
                  }
                    /* M459: name the likelier cause when the evidence is in.
                     *
                     * The warning above blames the HISTORY -- "many small tool
                     * results" -- which describes the shape correctly and can
                     * point at the wrong remedy. On the run that motivated
                     * this, contextLength was declared 32000 while the server
                     * had been ACCEPTING ~160k-token requests throughout (its
                     * real max_model_len was 256000). jichi compacted seven
                     * times toward a target it never needed, achieved nothing
                     * (7/7 unrelieved), and advised shrinking tool output when
                     * the fix was one config number.
                     *
                     * An under-declared window is the common case, not an
                     * exotic one: doctor's own text says jichi assumes ~32000
                     * when contextLength is absent, and every local server this
                     * project has met declares nothing.
                     *
                     * The proof needs no new measurement: a prompt count the
                     * SERVER produced for a request it ACCEPTED, larger than the
                     * declared limit, is evidence the limit understates the
                     * model. Only said alongside the short-fall warning, so a run
                     * whose limit is merely generous stays quiet.
                     *
                     * M494 -- READ THE HIGH-WATER MARK, NOT THE LATEST SAMPLE.
                     * This tested `last_prompt_tokens`, the most recent count.
                     * The evidence is MONOTONE (once a 32,802-token request is
                     * served, the window is provably >= that from then on) but
                     * the short-fall warning this hangs off fires ONCE PER TURN,
                     * so the notice had exactly one chance and needed the request
                     * immediately before that instant to be the oversized one.
                     * Measured by dogfooding jichi on a real project against the
                     * HRZ gateway: 60 model calls, 9 of them accepted OVER the
                     * declared 32000, 36 short-fall compactions of which 35
                     * relieved nothing -- and the notice printed 0 times. The
                     * operator was told to shrink tool output when the fix was
                     * one config number, which is the exact false remedy this
                     * check exists to prevent. */
                  /* M494 -- ITS OWN LATCH, and this is the half that made the
                   * check unreachable. The notice used to sit INSIDE the
                   * `!warned_short` branch above, so it got the single
                   * once-per-turn opportunity that branch gets. In the measured
                   * run the first short-falling round came at 11,598 accepted
                   * tokens -- under the declared 32000, so there was correctly
                   * nothing to say -- the warning latched, and the nine requests
                   * that later proved the window bigger arrived with the branch
                   * closed for the rest of the turn. Evaluated every short-falling
                   * round now, latched separately, so the notice fires the first
                   * time the evidence exists rather than only if it existed at one
                   * particular instant. */
                  if (!warned_underdecl && mrep.limit > 0 &&
                      app->max_prompt_tokens > mrep.limit) {
                        warned_underdecl = 1;
                        jc_logf(JC_LOG_WARN,
                            "[compact] ...but the server ACCEPTED a %ld-token "
                            "request against a declared limit of ~%ld, so the "
                            "model's real window is larger than jichi was "
                            "told. Raise \"contextLength\" on the model (or "
                            "--context-limit); eliding is not the problem here",
                            app->max_prompt_tokens, mrep.limit);
                  }
                }
                if (cb != NULL && cb->on_status != NULL) { /* M99: stream to a driver */
                    char msg[64];
                    jc_snprintf(msg, sizeof(msg), "compacted context (%lu elided)",
                                (unsigned long)nc);
                    cb->on_status(cb->user, msg);
                }
                /* M298: the elision above may have given back the room that
                 * escalated this turn. This is the only place that can know, so
                 * it is the only place that checks. */
                route_off_context(app, hist, cb);
            }
            /* M358: the injection itself -- one user-role note at the tail
             * (the M347 control-inject shape, so the M31 cached prefix stays
             * byte-stable), from the pass's own calibrated numbers so the
             * note and the trigger cannot disagree. M900 (C1): injected on
             * EVERY pressed round, not latched once -- the standing reading.
             * The tail vehicle (a user message after the breakpoint) keeps the
             * cached prefix byte-stable: test_prefix_stable_across_turns is the
             * guard. */
            if (note_now) {
                struct jc_sb cn;
                jc_sb_init(&cn);
                jc_compact_pressure_note(mrep.before, mrep.limit,
                                         mrep.reached, &cn);
                if (cn.data != NULL && cn.len > 0) {
                    jc_history_add(hist, JC_ROLE_USER, cn.data);
                    if (cb != NULL && cb->on_status != NULL) {
                        cb->on_status(cb->user,
                                      "context notice: window under "
                                      "pressure, told the agent");
                    }
                }
                jc_sb_free(&cn);
            }
        }
        /* M81: periodic verify cadence. Run the gate every `verify_every` tool
         * calls so a long implementation turn stays green incrementally instead
         * of thrashing to a budget with a broken build (the dogfood found a
         * 62-call turn that built only once, then died mid-fix). On pass: bank
         * a green checkpoint (so a later budget-exit rolls back less, M80). On
         * fail: feed the parsed failures back so the model fixes them next
         * round -- no mid-turn rollback (too disruptive to the model's
         * in-flight edits).
         *
         * M1048 (G18): un-gated the same way as the completion site. The check
         * needs a tree, not a checkpoint: green-checkpoint BANKING below stays
         * behind jc_snapshot_available. The depth is made explicit because
         * `snapshotted` used to imply it. */
        if (env_active(app) && app->agent_depth == 0 &&
            app->env->verify_cmd != NULL &&
            jc_env_should_verify_now(app->env->verify_every,
                                     app->env->tool_calls,
                                     app->env->verify_every_last)) {
            struct jc_sb vout;
            struct jc_test_report rep;
            int code;
            cJSON *o;
            /* M431: kept across the journal block so the red branch below can
             * render it. It used to be computed and discarded here. */
            enum jc_verify_consistency vcons = JC_VERIFY_AGREES;

            app->env->verify_every_last = app->env->tool_calls;
            jc_sb_init(&vout);
            code = jc_env_run_verify(app->env->verify_cmd, app->cwd, &vout,
                                     &app->abort_flag, app->env->verify_timeout);
            jc_test_report_init(&rep);
            jc_testparse(vout.data, &rep);
            app_record_last_verify(app, code, &rep);  /* M921: for the baton */
            o = jc_env_journal_begin(app->env, "verify");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "phase", "periodic");
                cJSON_AddNumberToObject(o, "exit", (double)code);
                if (rep.failed >= 0) {
                    cJSON_AddNumberToObject(o, "failed", (double)rep.failed);
                }
                /* M331: the periodic record carried `failed` but not `passed`,
                 * so a reader could not tell "nothing ran" from "everything
                 * passed yet the gate said no" -- which is exactly the
                 * distinction the consistency check turns on. */
                if (rep.passed >= 0) {
                    cJSON_AddNumberToObject(o, "passed", (double)rep.passed);
                }
            }
            /* M86: on green, sanity-check for hollowness; M331: on red, check the
             * verdict against its evidence. */
            if (code == 0) {
                /* M351: the periodic site tells the MODEL too -- the next
                 * model call exists to read it, and this is the moment a
                 * hollow green would otherwise bank false confidence. */
                env_verify_sanity_check(app, cb, &rep, o, hist);
            } else {
                /* M431: the finding is RENDERED for the model below, not just
                 * journaled. It was discarded with a (void) cast here, so a
                 * mid-turn red handed the model the raw failure without the one
                 * sentence that redirects it at the harness -- while the
                 * completion path, whose whole return on this check is that
                 * sentence, did render it. --verify-every exists to catch a
                 * problem EARLIER, so it is the site that most needs it. */
                vcons = env_verify_consistency_check(app, cb, &rep, code, o);
            }
            jc_env_journal_end(app->env, o);

            if (code == 0) {
                /* Bank progress: advance the green checkpoint. */
                if (jc_snapshot_available(app->snapshots)) {
                    const char *gc;
                    jc_snapshot_take(app->snapshots, "green: periodic");
                    gc = jc_snapshot_commit(app->snapshots,
                                            jc_snapshot_count(app->snapshots)
                                                - 1);
                    if (gc != NULL) {
                        jc_snprintf(app->env->green_commit,
                                    sizeof(app->env->green_commit), "%s", gc);
                        app->env->green_verified = 1; /* M207: observed green */
                    }
                }
                /* M89: a green verify breaks any stuck-failure chain. */
                app->env->repeat_fails = 0;
                app->env->last_fail_sig[0] = '\0';
                if (cb != NULL && cb->on_status != NULL) {
                    cb->on_status(cb->user, "verify: green (banked)");
                }
            } else {
                /* Red: feed the failures back so the model fixes them before
                 * piling on more changes (same rendering as fix-forward). */
                struct jc_sb msg;
                jc_size off = 0;
                const char *tail;
                jc_sb_init(&msg);
                jc_sb_append_fmt(&msg,
                    "[periodic verify] `%s` is RED (exit %d). Fix this before "
                    "making further changes.\n", app->env->verify_cmd, code);
                /* Before the evidence, for the reason the completion path states:
                 * the note reframes what follows, so it has to precede it. */
                verify_consistency_note(vcons, &rep, app->env, &msg);
                if (rep.failed > 0 || rep.failures.len > 0) {
                    jc_testparse_render(&rep, &msg);
                }
                if (vout.data != NULL && vout.len > 2048) {
                    off = vout.len - 2048;
                }
                tail = (vout.data != NULL) ? vout.data + off : "";
                jc_sb_append(&msg, off > 0 ? "\n--- output (tail) ---\n"
                                           : "\n--- output ---\n");
                jc_sb_append(&msg, tail);
                {
                    int repeat = jc_env_note_failure(app->env, vout.data);
                    if (repeat >= 2) {
                        cJSON *js;
                        jc_sb_append_fmt(&msg,
                            "\n\nNOTE: this is the SAME error as before (%dx in "
                            "a row) -- the current approach is not working, try "
                            "a DIFFERENT fix.", repeat);
                        jc_logf(JC_LOG_WARN, "envelope: verify stuck on the same "
                                "error (%dx): %s", repeat,
                                app->env->last_fail_sig);
                        /* M422: journal it, as the completion fix-forward path
                         * above already does. Without this, M89's signal reached
                         * the MODEL (the NOTE) and the console (the WARN) but not
                         * the operator's table -- and M420 wired `runs`' stuck=N
                         * to this event, so mid-turn thrashing, which is the very
                         * thing --verify-every exists to catch, could never show
                         * up in it. Measured: a run warned "(2x)", "(3x)", "(4x)"
                         * on stderr and its journal held zero verify_stuck. */
                        js = jc_env_journal_begin(app->env, "verify_stuck");
                        if (js != NULL) {
                            cJSON_AddNumberToObject(js, "repeat", (double)repeat);
                            cJSON_AddStringToObject(js, "sig",
                                                    app->env->last_fail_sig);
                            cJSON_AddStringToObject(js, "phase", "periodic");
                            jc_env_journal_end(app->env, js);
                        }
                    }
                }
                jc_history_add(hist, JC_ROLE_USER,
                               msg.data != NULL ? msg.data : "verify red");
                jc_sb_free(&msg);
                if (cb != NULL && cb->on_status != NULL) {
                    cb->on_status(cb->user, "verify: red (fed back)");
                }
            }
            jc_test_report_free(&rep);
            jc_sb_free(&vout);
        }
        /* M829 (N4b): a depth>=1 child looped on the same successful call to the
         * stop threshold -- end its one turn now, keeping the work and the last
         * answer, rather than spin to the iteration cap. */
        if (noprog_stop) {
            break;
        }
        /* M920: the relay's handover, AT the round boundary -- every tool call
         * of the round has its result, so the saved history is wire-valid and
         * the session resumes intact. proposal 6.3: the template is the loop
         * stop exit above; like it, the work is kept and the run reports WHY. */
        if (relay_stop && app->agent_depth == 0) {
            char *account = NULL;
            int account_cut = 0;
            /* M951 (V2): the NEXT leg's budget, from THIS leg's measured
             * growth -- computed BEFORE the account call, which makes a model
             * call that would otherwise append to the prompt series and skew
             * the growth. g is the median per-call prompt growth over the
             * whole leg; P0_next = P0 (this leg's first measured prompt), the
             * safe direction: the baton's notes make the next prompt bigger,
             * so using P0 can only make the budget a touch LARGER, never
             * shorter than needed (tighten-only). A < 2-point series, no
             * window, or no growth each take the controller's defined
             * degenerate answer (the cap stands) -- a leg that could not be
             * measured must not invent a limit. */
            {
                /* The configured cap is the operator's bound for THIS leg
                 * (env->max_tool_calls; 0 when a plain -p leg has no envelope).
                 * The baton's previous-leg budget only ever SHORTENS it --
                 * min, so an operator who lowers the cap for this leg is
                 * honoured, and the baton can never raise a leg above the
                 * configured bound (tighten-only). */
                long cap_l = (app->env != NULL)
                                 ? (long)app->env->max_tool_calls
                                 : 0;
                if (cap_l > 0 && app->relay_leg_calls_prev > 0 &&
                    (long)app->relay_leg_calls_prev < cap_l) {
                    cap_l = app->relay_leg_calls_prev;
                }
                if (cap_l > 0 && app->relay_prompt_series.len >= 2) {
                    jc_size n = app->relay_prompt_series.len;
                    jc_size nd = n - 1;
                    long *diffs = (long *)malloc(nd * sizeof(long));
                    if (diffs != NULL) {
                        long P0;
                        long g;
                        long W = jc_compact_context_limit(app);
                        jc_size i;
                        for (i = 0; i < nd; i++) {
                            long a = *(long *)jc_vec_at(
                                &app->relay_prompt_series, i);
                            long b = *(long *)jc_vec_at(
                                &app->relay_prompt_series, i + 1);
                            diffs[i] = b - a;
                        }
                        qsort(diffs, nd, sizeof(long), relay_diff_cmp);
                        g = diffs[(nd - 1) / 2];   /* lower median, total */
                        P0 = *(long *)jc_vec_at(
                            &app->relay_prompt_series, 0);
                        app->relay_measured_p0 = P0;
                        app->relay_measured_g = g;
                        app->relay_measured_w = W;
                        relay_leg_budget(P0, g, W, P0, (jc_size)cap_l, 0,
                                         &app->relay_next_leg_calls);
                        /* M951: measured only when the controller actually
                         * derived a limit from growth (g > 0 and a window to
                         * measure against); the degenerate answers (cap
                         * stands) are not an "adjustment" worth journaling. */
                        app->relay_measured = (g > 0 && W > 0) ? 1 : 0;
                        free(diffs);
                    }
                }
                if (app->relay_measured) {
                    /* M951: the session-history half (telemetry); the run's
                     * journal gets `budget_adjust` at the baton write. The
                     * event is metrics-only: the numbers, not any text. */
                    cJSON *bo = telem(app, "relay");
                    if (bo != NULL) {
                        cJSON_AddStringToObject(bo, "phase", "budget");
                        cJSON_AddNumberToObject(bo, "p0",
                            (double)app->relay_measured_p0);
                        cJSON_AddNumberToObject(bo, "g",
                            (double)app->relay_measured_g);
                        cJSON_AddNumberToObject(bo, "w",
                            (double)app->relay_measured_w);
                        cJSON_AddNumberToObject(bo, "old",
                            (double)cap_l);
                        cJSON_AddNumberToObject(bo, "new",
                            (double)app->relay_next_leg_calls);
                        jc_eventlog_end(app->telemetry, bo);
                    }
                }
            }
            /* M921 (sub-slice C): obtain the model's account before the baton
             * is written. A leaf: any failure falls back to the labelled quote
             * of the last answer; the handover and the loop's return stand. */
            relay_account_call(app, hist, prov, opts->system_msg,
                               &account, &account_cut);
            free(app->relay_account);
            app->relay_account = account;
            app->relay_account_cut = account_cut;
            app->relay_handover = 1;
            break;
        }
        /* Loop again so the model can react to the tool results. */
    }

    /* M920: the relay's handover, distinguished from the iteration cap. The
     * break above ended the loop EARLY -- the window was exhausted, not the
     * iteration count -- so it must not fall into the cap's settlement below:
     * that sets turn_capped, and jc_agent_stop_reason would report
     * max_iters over handover. Like the loop stop, the work is kept and the
     * run returns JC_OK; the reason is read from app->relay_handover, set at
     * the break. (The baton the reason points at is the follow-up milestone.) */
    if (relay_stop) {
        return JC_OK;
    }

    /* M829 (N4b): the no-progress stop above, distinguished from the iteration cap
     * so the log and the record say what actually happened. The last answer stands;
     * the child's end record notes `repeated` (last_fail_repeat, set above). Like the
     * cap it returns JC_OK -- the work so far is valid. */
    if (noprog_stop) {
        static const char *const kinds[] = { "", "same_result", "refused",
                                             "placeholder", "rotation" };
        cJSON *lo;
        if (noprog_stop == 2) {
            jc_logf(JC_LOG_WARN, "stopping this sub-agent: the same refused call "
                    "failed the same way %d times", app->last_fail_repeat);
        } else if (noprog_stop == 3) {
            jc_logf(JC_LOG_WARN, "stopping this sub-agent: %d copied placeholders "
                    "in a row instead of arguments", app->last_fail_repeat);
        } else if (noprog_stop == 4) {
            jc_logf(JC_LOG_WARN, "stopping this sub-agent: its last %d calls "
                    "produced nothing it had not seen in the %d before",
                    app->last_fail_repeat, JC_NOVELTY_WINDOW);
        } else {
            jc_logf(JC_LOG_WARN, "stopping this sub-agent: the same successful call "
                    "returned the same result %d times with no progress",
                    app->last_fail_repeat);
        }
        /* M869: which loop, where an offline reader sees it (any depth). */
        lo = telem(app, "loop_stop");
        if (lo != NULL) {
            cJSON_AddStringToObject(lo, "kind",
                kinds[(noprog_stop >= 1 && noprog_stop <= 4) ? noprog_stop : 1]);
            cJSON_AddNumberToObject(lo, "repeat", (double)app->last_fail_repeat);
        }
        jc_eventlog_end(app->telemetry, lo);
        app->last_run_capped = 1;
        app->last_run_looped = noprog_stop;
        return JC_OK;
    }

    /* The cap is a CIRCUIT BREAKER, not a task failure: the work done so far is
     * valid and still in the history, so this returns JC_OK and the next turn
     * resumes from it (an operator types anything; a supervisor sends another
     * prompt). Returning an error would invite callers to discard or roll back
     * work that is fine.
     *
     * M322: but a caller has to be able to TELL. Interactively the warning below
     * is enough; a machine driver was told `stop_reason: "done"` with an empty
     * answer, which is indistinguishable from "finished and had nothing to say".
     * Only the top level sets turn_capped -- see jc_app.h for why a subagent's
     * cap must not answer this question. */
    jc_logf(JC_LOG_WARN, "hit max tool iterations (%d)", opts->max_iters);
    app->last_run_capped = 1; /* M62 #5: surface the truncation to the caller */
    if (app->agent_depth == 0) {
        app->turn_capped = 1;
    }
    return JC_OK;
}

/* M688/M690: WHY the run stopped, decided ONCE for every surface. Before this, the
 * chain below lived inside `if (ctx.json ...)`, so the text path never computed
 * a stop reason at all -- which is why the reach footer could print
 * "not checked: (nothing)" beside an `[envelope] budget_exhausted` and an empty
 * answer. The order of the tests is load-bearing and is preserved exactly from
 * that chain: a transport failure outranks an envelope verdict, and the
 * iteration cap is last because it is the weakest claim -- any of the others
 * explains the stop better. See docs/plans/2026-09-run-outcome.md.
 *
 * M690 moved it here from main.c so the RUN JOURNAL can record the same
 * answer. The journal's `end` event carried only the envelope's `outcome`,
 * which cannot express `max_iters` at all -- measured over 114 journals and
 * 101 completed runs, the recorded values were ok / running / budget_exhausted
 * / verify_failed and nothing else, so DEFERRED item 7's question ("does a
 * capped one-shot usually answer anything?") could not be asked of the corpus
 * that was supposed to answer it. */
enum jc_run_stop jc_agent_stop_reason(struct jc_app *app, jc_status st)
{
    if (st == JC_ERR_ABORTED) {
        return JC_STOP_INTERRUPTED;
    }
    if (st == JC_ERR_TIMEOUT) {
        return JC_STOP_TIMEOUT;
    }
    if (st != JC_OK) {
        return JC_STOP_ERROR;
    }
    if (app->env != NULL) {
        if (app->env->outcome == JC_ENV_BUDGET_EXHAUSTED) {
            return JC_STOP_BUDGET;
        }
        if (app->env->outcome == JC_ENV_VERIFY_FAILED) {
            return JC_STOP_VERIFY_FAILED;
        }
        if (app->env->outcome == JC_ENV_SCOPE_TAINTED) {
            return JC_STOP_SCOPE_TAINTED;
        }
    }
    if (app->turn_capped) {
        return JC_STOP_MAX_ITERS;
    }
    if (app->relay_handover) {
        /* M920: the window was exhausted and the run ended at the round
         * boundary to hand over. Not budget (no fence fired), not error
         * (nothing failed), not max_iters (the cap did not run out) -- a
         * distinct reason, so a supervisor can tell "hand it on" from all
         * three. */
        return JC_STOP_HANDOVER;
    }
    return JC_STOP_DONE;
}


jc_status jc_agent_run_turn(struct jc_app *app, struct jc_history *hist,
                            const struct jc_agent_callbacks *cb)
{
    struct jc_run_opts opts;
    struct jc_vec core_allow;
    struct jc_vec turn_isect;   /* M848: attempt's profile fence x core */
    jc_size i, n;
    jc_status st;
    /* M715: the caller has already added this turn's user message, so every
     * assistant message from here on is THIS turn's -- the end event's
     * answer_bytes counts only those. */
    jc_size turn_from = jc_history_len(hist);

    /* NO HOUSE DIALECT (M718): a turn is where a model is called, so it is where
     * a missing provider is refused -- with the sentence that says why, before
     * any state moves. The program may run without one (main.c says when);
     * every caller of a turn -- the TUI, headless, the daemon, ACP -- passes
     * through here, so this is the single place it can be missed. */
    if (app->provider == NULL) {
        char why[640];
        if (app->config.model.model == NULL ||
            app->config.model.model[0] == '\0') {
            jc_snprintf(why, sizeof(why), "no model is configured -- %s",
                        jc_config_no_model_advice_cfg(&app->config));
        } else if (!jc_config_provider_problem(&app->config.model, why,
                                               sizeof(why))) {
            jc_snprintf(why, sizeof(why), "no provider could be created for "
                        "the active model");
        }
        jc_logf(JC_LOG_ERROR, "%s", why);
        if (cb != NULL && cb->on_status != NULL) {
            cb->on_status(cb->user, why);
        }
        return JC_ERR_INVALID;
    }

    jc_vec_init(&core_allow, sizeof(char *));
    jc_vec_init(&turn_isect, sizeof(char *));
    app->turn_capped = 0;   /* M322: this turn's own cap-exit, not a subagent's */

    /* Expose the active callbacks so tools (e.g. spawn_subagent) can forward
     * them into nested runs when the front-end opts in (app->stream_subagents). */
    app->cb = cb;

    /* M20a: reclaim the previous top-level turn's transient scratch (the system
     * message + any command/@-ref expansion, which the submit path has already
     * copied into history via jc_history_add). Only at the top level, so an
     * in-flight subagent/parallel child sharing the arena is never affected. */
    if (app->agent_depth == 0 && app->scratch != NULL) {
        jc_arena_reset(app->scratch);
    }
    /* M199: and the per-tool-call arena. It is normally reclaimed before each
     * tool call, but callers OUTSIDE the tool loop use it too (jc_memory_load
     * from the TUI's /memory, for one), and those would otherwise accumulate
     * until the next tool call -- or forever, in a session that never runs one. */
    if (app->agent_depth == 0 && app->tool_scratch != NULL) {
        jc_arena_reset(app->tool_scratch);
    }
    /* D6 (M975): per-turn scoped-edit state (reset at top-level turn start). */
    if (app->agent_depth == 0) {
        jc_scopededit_turn_start(app);
    }
    /* M218: the resets above just handed the previous turn's blocks back to
     * malloc; sweep the free heap back to the OS while it is at its emptiest.
     * (The per-retry request bodies never reach here -- jc_mem_tune pins the
     * mmap threshold so those munmap on free.) No-op without glibc. */
    if (app->agent_depth == 0) {
        jc_mem_release_os();
    }

    /* M298: a new top-level turn starts on the fast tier (below), so last turn's
     * escalation cause must not leak into it and license a de-escalation that
     * never happened. */
    app->routed_for_context = 0;

    /* M21c: count and mark the top-level turn for telemetry correlation. */
    app->turn++;
    {
        cJSON *o = telem(app, "turn_start");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "mode",
                jc_agent_mode_name((enum jc_agent_mode)app->mode));
            cJSON_AddStringToObject(o, "model",
                app->config.model.name != NULL ? app->config.model.name : "");
            if (jc_eventlog_full(app->telemetry)) {
                jc_size k = jc_history_len(hist);
                while (k > 0) {
                    struct jc_message *m = jc_history_get(hist, k - 1);
                    if (m != NULL && m->role == JC_ROLE_USER &&
                        m->content != NULL) {
                        jc_eventlog_add_text(o, "prompt", m->content,
                                             JC_EVENTLOG_TEXT_MAX);
                        break;
                    }
                    k--;
                }
            }
        }
        jc_eventlog_end(app->telemetry, o);
    }

    /* Constraint auto-adopt (M110): in AUTO (unsupervised) mode, scan the latest
     * user message for constraints ("do not run the build") and enforce them
     * immediately -- the mode where the model runs unwatched and most needs a hard
     * limit. The interactive TUI proposes-then-confirms instead (a /constraint
     * surface), so it does not auto-adopt here. */
    if (app->constraints_on && app->constraints_autoadopt &&
        app->mode == JC_MODE_AUTO) {
        jc_size k = jc_history_len(hist);
        while (k > 0) {
            struct jc_message *m = jc_history_get(hist, k - 1);
            /* M921 (sub-slice D, proposal 6.7): the relay's SEED is EXEMPT
             * from this scan. It carries the previous leg's baton (fenced,
             * untrusted) plus this leg's task; a sentence in the notes ("do
             * not touch the parser yet") is the previous model's account, not
             * an instruction the operator gave this leg, and adopting it would
             * turn a handover note into an enforced rule for the successor.
             * The fence alone does not exempt: quote_mask only masks
             * ```/~~~/quotes, and the UNTRUSTED marker is none of those -- so
             * the exemption is explicit, on the app, set where the seed is
             * built (headless, depth 0). Checked before the role test so a
             * seed that is not the last user message still skips the scan. */
            if (app->relay_seed_active) {
                break;   /* skip the scan of the seed; the run proceeds */
            }
            if (m != NULL && m->role == JC_ROLE_USER && m->content != NULL) {
                int before = app->n_constraints;
                int added = jc_app_constraints_scan_adopt(app, m->content);
                if (added > 0) {
                    /* M167: say WHAT was adopted, at a level the operator
                     * actually sees. This was JC_LOG_INFO with only a count --
                     * below the default JC_LOG_WARN threshold, so a misparsed
                     * prohibition ("do not change the test file" read as "do
                     * not run tests") was adopted, persisted to
                     * .jichi/constraints.md, and enforced in complete silence.
                     * An inferred rule that changes behaviour must be visible
                     * and attributable.
                     *
                     * M169: these are now session-scoped, so the message no
                     * longer points at the store -- an inferred constraint is
                     * not in it. Saying "this session" is also the honest cue
                     * that the fix for a misparse is to rephrase and re-run,
                     * not to hunt for a file. */
                    char names[512];
                    char said[1400];
                    jc_constraint_join_text(app->constraints, before,
                                            app->n_constraints,
                                            names, sizeof names);
                    /* M872 (N45 (b)): each rule with the sentence it was read
                     * from. The canonical text alone ("do not commit") could
                     * not show that run 0091's came from a quoted phrase in a
                     * question. */
                    jc_constraint_join_sourced(app->constraints, before,
                                               app->n_constraints,
                                               said, sizeof said);
                    /* M734: advisory now, and the notice says so -- an
                     * operator reading "enforced" would expect refusals that
                     * no longer happen. */
                    jc_logf(JC_LOG_WARN,
                            "[constraint] inferred from your request, ADVISORY "
                            "for THIS SESSION (not enforced, not saved): %s -- "
                            "the model is told to follow it unless the task "
                            "needs otherwise; `/constraints add` makes a rule "
                            "binding", said);
                    if (cb != NULL && cb->on_status != NULL) {
                        cb->on_status(cb->user,
                            "inferred constraints from your request "
                            "(advisory, not enforced)");
                    }
                    /* Journal it, so a post-mortem does not depend on having
                     * watched stderr. An adoption announces itself once and never
                     * again, and it silently narrows what the run may do -- a
                     * measured session had 4 of 8 runs adopt one without the
                     * operator noticing, and one of those died with no deliverable
                     * because the ban covered the tools its brief depended on.
                     * `jichi runs` renders this as constraints=N, next to
                     * steered=N, which is the same kind of fact: something outside
                     * the model's choosing changed what the run could do. */
                    if (app->env != NULL) {
                        cJSON *cj = jc_env_journal_begin(app->env, "constraint");
                        if (cj != NULL) {
                            cJSON_AddNumberToObject(cj, "adopted",
                                (double)(app->n_constraints - before));
                            cJSON_AddStringToObject(cj, "names", names);
                            cJSON_AddStringToObject(cj, "source", "inferred");
                            /* M734: what a reader of the journal needs to
                             * know about an adoption -- whether it binds. */
                            cJSON_AddBoolToObject(cj, "advisory", 1);
                            /* M872: the sentences they were read from, each
                             * once ("source" above says where a rule came
                             * from in kind; this says in words). */
                            {
                                cJSON *fr = cJSON_CreateArray();
                                int q;
                                for (q = before; fr != NULL &&
                                     q < app->n_constraints; q++) {
                                    const char *s = app->constraints[q].source;
                                    const char *p = (q > before)
                                        ? app->constraints[q - 1].source : NULL;
                                    if (s == NULL ||
                                        (p != NULL && strcmp(p, s) == 0)) {
                                        continue;
                                    }
                                    cJSON_AddItemToArray(fr,
                                                         cJSON_CreateString(s));
                                }
                                if (fr != NULL) {
                                    cJSON_AddItemToObject(cj, "from", fr);
                                }
                            }
                            jc_env_journal_end(app->env, cj);
                        }
                    }
                }
                break;
            }
            k--;
        }
    }

    /* Tiered routing: start each turn on the fast tier (a no-op when routing is
     * off, inert, or already on fast). Escalation to strong happens mid-turn.
     *
     * M298 -- THIS MUST STAY BEFORE COMPACTION, and the ordering is a fix, not a
     * preference. jc_compact_run measures the history against the ACTIVE model's
     * window (jc_compact.c effective_limit reads app->config.model). Compacting
     * first meant that after any turn which escalated, strong was still active
     * when the decision was made: the history was measured against strong's large
     * window, found to need no trimming, and the turn THEN dropped to fast and ran
     * with a history sized for a window it does not have. That is the reported
     * "autocompact is not applied to the fast model", and on a backend without
     * prompt caching every subsequent turn pays for it.
     *
     * Routing first is preferable to teaching compaction about routing: the
     * summarization itself uses the `summarize`-role model, not the active one, so
     * switching first changes only which window the TRIGGER compares against --
     * precisely the thing that was wrong. */
    {
        int fast_idx;
        int strong_idx;
        /* A command's `model:` pin wins over turn-start routing. This override was
         * unconditional, so a command declaring a model had it applied in main.c and
         * then silently replaced here -- the declaration was a no-op whenever routing
         * was enabled (measured 2026-08-07: a probe command pinning the strong tier
         * was answered by the fast one, and by the pinned model under --no-route).
         * Reactive escalation (verify-fail / error / stall / context) is deliberately
         * NOT suppressed: it responds to a failure the pin cannot foresee, and it
         * announces itself via `[route]` and a journal event. */
        if (!app->model_pinned &&
            jc_config_routing_resolve(&app->config, &fast_idx, &strong_idx)) {
            /* Use the fast tier if its server is up, else its fallback chain. */
            jc_app_route_to(app, jc_app_effective_model(app, fast_idx),
                            "turn-start");
        }
    }

    /* Fold older history into a summary if it has grown large (between-turn
     * only; a no-op below threshold). Failures leave history intact. Runs AFTER
     * the routing above, so it measures the model that will actually run. */
    {
        int did_compact = 0;
        jc_compact_run(app, hist, cb, &did_compact);
        if (did_compact) {
            cJSON *o = telem(app, "compact");
            jc_eventlog_end(app->telemetry, o);
            /* M218: the summarized prefix's messages were just freed --
             * another low-water moment worth sweeping back to the OS. */
            jc_mem_release_os();
        }
    }

    memset(&opts, 0, sizeof(opts));
    opts.provider = app->provider;
    opts.system_msg = jc_sysmsg_build(app);
    /* M365: the prefix sentinel. The system prompt heads the M31 cached
     * prefix; if its hash changes on three consecutive turns, something is
     * rebuilding it differently every time -- no legitimate cause (a memory
     * write, a mode switch, midnight) fires per-turn -- and every request is
     * silently re-billing the largest span it sends. The first proven source
     * was calibration jitter moving the fit-cap truncation byte (fixed by
     * jc_sysmsg_fit_budget's quantization); this watch is for the next one.
     * Top-level only: a subagent's prompt varies by task, legitimately. */
    if (jc_prefix_watch_track(&app->prefix_watch,
                              jc_prefix_hash(opts.system_msg))) {
        jc_logf(JC_LOG_WARN,
                "[prefix] the system prompt has changed on %d consecutive "
                "turns -- the prompt-cache prefix is being re-billed every "
                "call. A section of the prompt is rebuilding differently "
                "each turn (rules/repo-map truncation, a nondeterministic "
                "section); check /cache and the cached= token line",
                JC_PREFIX_CHURN_STREAK);
        {
            cJSON *o = jc_app_telem_begin(app, "prefix_churn");
            if (o != NULL) {
                cJSON_AddNumberToObject(o, "streak",
                                        (double)JC_PREFIX_CHURN_STREAK);
            }
            jc_app_telem_end(app, o);
        }
    }
    opts.include_mutating = !app->readonly;
    opts.exclude_tool = NULL;
    opts.max_iters = app->config.max_tool_iters;
    opts.auto_posture = JC_FALSE; /* preserve the interactive prompt path */

    /* M74: on a small-context (or lite) model, advertise only the lean core tool
     * set so the tool definitions don't crowd out the window. Reuses the run's
     * tool allow-list (the same fence subagent profiles use). Top-level only;
     * core mode also excludes spawn_*, so no subagents are spawned here. */
    if (jc_config_tool_profile_core(&app->config,
                                    jc_compact_context_limit(app))) {
        jc_tool_core_allow(&core_allow);
        opts.allow = &core_allow;
    }
    /* M848 (def-287): `attempt --agent` fences its one turn to the profile's
     * `tools:` (app->turn_allow), so the learner tiers differ by tools as
     * ASSIGNMENTS.md says -- before M848 they differed by persona only, and
     * learner-senior was offered `hint`. Intersected with the core set when
     * that is on, so it can only narrow; a profile sharing no tool with the core
     * set leaves the turn with none (the helper's sentinel), and says so. A
     * main-turn command never sets turn_allow, so its tool prefix is stable. */
    if (app->turn_allow != NULL && app->turn_allow->len > 0) {
        if (opts.allow != NULL) {
            if (jc_tool_allow_intersect(opts.allow, app->turn_allow,
                                        jc_app_scratch(app), &turn_isect) == 0) {
                jc_logf(JC_LOG_WARN,
                        "the agent profile's tools: and the core tool set (a "
                        "small context window) share no tool -- this turn has "
                        "none");
            }
            opts.allow = &turn_isect;
        } else {
            opts.allow = app->turn_allow;
        }
    }

    /* Under an envelope the declared budgets (tokens/deadline/tool-calls) are
     * the real bound, and verify-retries add iterations; give the loop ample
     * headroom so it isn't cut short by the plain iteration cap. */
    if (app->env != NULL && opts.max_iters < 200) {
        opts.max_iters = 200;
    }

    /* Label any snapshot with the turn's triggering user request. */
    n = jc_history_len(hist);
    for (i = n; i > 0; i--) {
        struct jc_message *mm = jc_history_get(hist, i - 1);
        if (mm->role == JC_ROLE_USER && mm->content != NULL) {
            opts.checkpoint_label = mm->content;
            break;
        }
    }

    /* Arm the per-turn fix-forward budget and journal the run's limits. */
    if (app->env != NULL) {
        cJSON *o;
        app->env->retries_left = app->env->verify_retries;
        o = jc_env_journal_begin(app->env, "start");
        if (o != NULL) {
            cJSON_AddNumberToObject(o, "budget_tokens", app->env->budget_tokens);
            cJSON_AddNumberToObject(o, "deadline_secs",
                                    (double)app->env->deadline_secs);
            cJSON_AddNumberToObject(o, "max_tool_calls",
                                    (double)app->env->max_tool_calls);
            cJSON_AddStringToObject(o, "verify",
                app->env->verify_cmd != NULL ? app->env->verify_cmd : "");
            /* M715: no session to resume -- a headless --no-session run. For
             * this shape the work of a capped turn is gone rather than pending,
             * which is the whole of DEFERRED item 7's question, and nothing in
             * the journal could say it. */
            cJSON_AddBoolToObject(o, "one_shot", app->env->one_shot);
            /* M720: WHICH model answered. The journal is read to price decisions
             * about how runs behave, and the model is the first covariate of that
             * -- it recorded the cap, the verifier and the scope, and not the
             * model. It is also how a mockmodel probe that wrote into the real
             * store is told from real work without joining telemetry (on
             * 2026-09-23, 28 of the 32 workstation journals carrying a
             * stop_reason were such probes; tests/measure/corpus_filter.py). The
             * id only -- never the endpoint, never the key. */
            cJSON_AddStringToObject(o, "model",
                app->config.model.model != NULL ? app->config.model.model : "");
            /* M503: a supervisor reading `verify: make test` could not tell an
             * operator's choice from a config inheritance, and the two mean
             * different things when the gate then fails. */
            cJSON_AddStringToObject(o, "verify_source",
                app->env->verify_source != NULL ? app->env->verify_source : "");
            cJSON_AddNumberToObject(o, "edit_scope",
                                    (double)app->env->edit_scope.len);
            /* ...and WHICH globs, not merely how many (M459).
             *
             * The count alone cannot tell a fence from a formality:
             * `--edit-scope AGENTS.md` and `--edit-scope '**'` both record 1,
             * and the second permits the entire workspace. That mattered
             * immediately -- tests/measure/strict_green_fp.py defines its
             * denominator as "runs with a NONZERO edit scope", so a corpus of
             * '**' runs reports a perfect false-positive rate while proving
             * nothing, and no reader of the journal could have noticed.
             *
             * A supervisor auditing an unattended run wants the same fact for
             * its own reasons: "what was this agent allowed to touch" is not
             * answerable by a count. Bounded, because a scope may be long and a
             * journal line is read by machines. */
            if (app->env->edit_scope.len > 0) {
                cJSON *sg = cJSON_CreateArray();
                if (sg != NULL) {
                    jc_size gi;
                    for (gi = 0; gi < app->env->edit_scope.len &&
                                 gi < JC_ENV_SCOPE_JOURNAL_MAX; gi++) {
                        const char *g = *(const char **)
                            jc_vec_at(&app->env->edit_scope, gi);
                        if (g != NULL) {
                            cJSON_AddItemToArray(sg, cJSON_CreateString(g));
                        }
                    }
                    cJSON_AddItemToObject(o, "edit_scope_globs", sg);
                }
            }
            /* M420: which project this run belonged to. Not for the join --
             * telemetry already stamps `ws` -- but for the reader: `runs` had
             * no workspace column at all, so on a machine driving three
             * projects a row could not say whose run it was. Written here
             * rather than in jc_envelope.c because the envelope struct has no
             * workspace pointer and does not need one: this call site has
             * `app`. */
            if (app->root[0] != '\0') {
                cJSON_AddStringToObject(o, "ws", app->root);
            }
        }
        jc_env_journal_end(app->env, o);

        /* M893 (N52 (b)): the worktree as the run's first turn found it, once,
         * so that a batch can tell whether its base is still the tree the run
         * started from (jc_tool_parallel.c). */
        if (!app->env->start_fp_taken && app->env->verify_cmd != NULL) {
            app->env->start_fp_taken = 1;
            app->env->start_fp_ok =
                (jc_snapshot_worktree_fingerprint(app->root, app->env->start_fp,
                                                  sizeof app->env->start_fp)
                     == JC_OK);
        }

        /* Optional baseline check: record whether the workspace already passes
         * the verifier. A failing baseline is expected when the task IS to fix
         * it, so this only informs (and warns that rollback-to-green would
         * restore a still-failing tree). M343: what the result MEANS depends on
         * the declared gate kind, and declaring one ARMS this probe -- a
         * declaration nobody checks is a comment. The verdict table is the pure
         * jc_env_baseline_check; the row that pays for it is a declared GOAL
         * gate that is green on the untouched tree: it can pass without the
         * work, which is the trap that cost two runs ~3M tokens (ANECDOTES
         * #38). A declared goal's red start is the normal state and no longer
         * warns -- a warning that fires on every correct run is one an
         * operator learns to skip. */
        if ((app->env->verify_baseline ||
             app->env->verify_kind != JC_VERIFY_KIND_UNSET) &&
            app->env->verify_cmd != NULL) {
            struct jc_sb bout;
            int code;
            int kept_line = 0;
            enum jc_env_baseline_verdict v;

            jc_sb_init(&bout);
            code = jc_env_run_verify(app->env->verify_cmd, app->cwd, &bout,
                                     &app->abort_flag, app->env->verify_timeout);
            v = jc_env_baseline_check(app->env->verify_kind, code);
            /* M893 (N52 (b)): a red baseline before any change is the evidence
             * that a later failure on the same line is not the run's doing. */
            if (code != 0 && app->env->changing_executed == 0 &&
                app->env->pre_edit_fail[0] == '\0' &&
                jc_env_fail_line(bout.data, app->env->pre_edit_fail,
                                 sizeof app->env->pre_edit_fail)) {
                jc_snprintf(app->env->pre_edit_from,
                            sizeof app->env->pre_edit_from, "%s",
                            "the run-start verify, before any change,");
                kept_line = 1;
            }
            jc_sb_free(&bout);
            o = jc_env_journal_begin(app->env, "baseline");
            if (o != NULL) {
                cJSON_AddNumberToObject(o, "exit", (double)code);
                if (kept_line) {
                    cJSON_AddStringToObject(o, "line", app->env->pre_edit_fail);
                }
                if (app->env->verify_kind != JC_VERIFY_KIND_UNSET) {
                    cJSON_AddStringToObject(o, "kind",
                        jc_env_verify_kind_name(app->env->verify_kind));
                    if (v == JC_BASELINE_FORCES_NOTHING) {
                        cJSON_AddNumberToObject(o, "forces_nothing", 1);
                    }
                }
            }
            jc_env_journal_end(app->env, o);
            if (v == JC_BASELINE_OK) {
                /* M473: a green baseline is the proof M207 said was missing.
                 *
                 * green_verified exists because "the first pre-edit checkpoint is
                 * recorded as green ON THE PREMISE that the tree started green;
                 * nothing checks that premise" (jc_envelope.h) -- so a run whose
                 * gate was already red used to discard its work in favour of an
                 * equally red baseline. M343 then added this probe, which checks
                 * exactly that premise, and the two were never connected: the
                 * verdict was journaled and warned on, and JC_BASELINE_OK -- the
                 * good news -- was dropped.
                 *
                 * Measured cost of the gap (docs/analysis/2026-08-18-dogfooding-
                 * on-chrtext.md): a run stopped on its tool-call budget having
                 * edited three files, and was told "no verify passed during this
                 * run, so there is no known-good checkpoint ... a rollback could
                 * not have helped" -- while its own journal carried
                 * `baseline {"exit":0,"kind":"invariant"}` two lines above. The
                 * message was false and the work was kept unverified.
                 *
                 * Only for an OK verdict, which jc_env_baseline_check already
                 * restricts to unset-or-invariant: a GOAL gate that passes at the
                 * start proves nothing about the tree (it is the M330 trap, and it
                 * gets FORCES_NOTHING instead). */
                app->env->green_verified = 1;
            } else if (v == JC_BASELINE_NOT_KNOWN_GOOD) {
                jc_logf(JC_LOG_WARN, "envelope: baseline verification failed "
                        "(exit %d); the starting tree is not known-good", code);
            } else if (v == JC_BASELINE_FORCES_NOTHING) {
                jc_logf(JC_LOG_WARN, "envelope: the verifier is declared a "
                        "GOAL gate and already passes on the untouched tree "
                        "-- it forces nothing; fix the gate so the missing "
                        "work makes it red, or declare it invariant");
            }
        }
        /* M343: with a declared goal gate, periodic mid-turn verifies cannot
         * bank a green checkpoint until the gate first passes -- each one still
         * costs a full verifier run. Advisory only; the behaviour is unchanged
         * because an early completion mid-turn is still worth catching. */
        if (app->env->verify_kind == JC_VERIFY_KIND_GOAL &&
            app->env->verify_every > 0) {
            jc_logf(JC_LOG_WARN, "envelope: --verify-every with a goal gate "
                    "banks nothing until the gate first passes; each mid-turn "
                    "verify still costs a full run of it");
        }
    }

    /* UserPromptSubmit hooks (M25): a hook may inject context into this turn
     * (appended as a user message, mirroring the self-review pattern) or block
     * the turn outright. opts.checkpoint_label already points at the latest user
     * message. */
    if (jc_hooks_active(app)) {
        struct jc_hook_result hr;
        jc_hook_result_init(&hr);
        jc_hooks_fire(app, JC_HOOK_USER_PROMPT, NULL, NULL, NULL, 0,
                      opts.checkpoint_label, &hr);
        if (hr.block) {
            const char *why = (hr.reason.len > 0) ? hr.reason.data
                : "Prompt blocked by a UserPromptSubmit hook.";
            jc_logf(JC_LOG_INFO, "[hook] prompt blocked");
            jc_history_add(hist, JC_ROLE_ASSISTANT, why);
            jc_hook_result_free(&hr);
            /* M848: this early return skipped the frees at the end of the
             * function -- core_allow's array leaked here before M848, and
             * attempt's turn_isect would have joined it. */
            jc_vec_free(&core_allow);
            jc_vec_free(&turn_isect);
            return JC_OK;
        }
        if (hr.context.len > 0) {
            jc_history_add(hist, JC_ROLE_USER, hr.context.data);
        }
        jc_hook_result_free(&hr);
    }

    st = run_agent_loop(app, hist, cb, &opts);
    jc_vec_free(&core_allow); /* frees the backing array; names are literals */
    jc_vec_free(&turn_isect); /* names are scratch-arena copies or the sentinel */

    /* M83: once the top-level turn's file state is final (any rollback already
     * applied inside the loop), warn about files changed OUTSIDE the edit scope --
     * the shell-introduced changes the write-tool fence can't catch. Diffs the
     * final tree against the fixed run-start baseline, so a reverted run flags
     * nothing and a kept run surfaces stray out-of-scope edits (e.g. an rm). */
    env_report_out_of_scope(app, cb);

    /* Stop hooks (M25): fire once the top-level turn has finished (any outcome).
     * Observe-only -- block/context are ignored here. */
    if (jc_hooks_active(app)) {
        struct jc_hook_result hr;
        jc_hook_result_init(&hr);
        jc_hooks_fire(app, JC_HOOK_STOP, NULL, NULL, NULL, 0, NULL, &hr);
        jc_hook_result_free(&hr);
    }

    if (app->env != NULL) {
        cJSON *o;
        if (st == JC_OK && app->env->outcome == JC_ENV_RUNNING) {
            app->env->outcome = JC_ENV_OK;
        }
        /* M332 REFUSE-THE-GREEN: a passing verify does not survive this run
         * having changed a file it was told to leave alone. The out-of-scope
         * report above (M83) runs BEFORE this point and after the verify already
         * set JC_ENV_OK inside the loop -- so everything needed was measured and
         * simply never consulted. A run has been observed editing its own
         * verifier through the shell, passing the gate it had just neutered, and
         * exiting 0 (ANECDOTES #45).
         *
         * Deliberately no rollback: this refuses a VERDICT, it does not discard
         * the work (the M80 principle). The operator reviews and decides.
         *
         * The exception has vocabulary already: if a run is SUPPOSED to be able
         * to change the gate, put the gate in --edit-scope and no flag is
         * raised. */
        if (jc_env_refuse_green(app->env->strict_green,
                                app->env->out_of_scope_seen,
                                app->env->outcome)) {
            cJSON *t;
            app->env->outcome = JC_ENV_SCOPE_TAINTED;
            jc_logf(JC_LOG_WARN, "envelope: refusing the green -- %d file(s) "
                    "changed outside the edit scope, so a passing verify cannot "
                    "be distinguished from a modified gate (--strict-green)",
                    app->env->out_of_scope_seen);
            t = jc_env_journal_begin(app->env, "strict_green");
            if (t != NULL) {
                cJSON_AddNumberToObject(t, "out_of_scope",
                                        (double)app->env->out_of_scope_seen);
                cJSON_AddStringToObject(t, "was", "ok");
            }
            jc_env_journal_end(app->env, t);
            if (cb != NULL && cb->on_status != NULL) {
                cb->on_status(cb->user, "green refused: files changed outside "
                                        "the edit scope");
            }
        }
        o = jc_env_journal_begin(app->env, "end");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "outcome",
                                    jc_env_outcome_name(app->env->outcome));
            /* M1048 (G18): whether the completion verifier RAN and what it
             * said -- the tri-state the outcome cannot carry. `outcome` says
             * how the run ended; it cannot say the difference between "the
             * check ran and passed" and "armed, outcome ok, the check never
             * ran". true: it ran and exited 0. false: it ran and the run
             * ended red on it. "not_run": armed, and it never ran (which,
             * after the un-gate, is a non-completed run). Absent when no
             * verifier was armed. */
            if (app->env->verify_cmd != NULL && app->env->verify_cmd[0] != '\0') {
                if (app->env->verifier_ran) {
                    cJSON_AddBoolToObject(o, "verified",
                        app->env->outcome == JC_ENV_OK ? 1 : 0);
                } else {
                    cJSON_AddStringToObject(o, "verified", "not_run");
                }
            }
            /* M690: and WHY it stopped, which `outcome` cannot say. Measured
             * over 114 journals and 101 completed runs before this line
             * existed: the recorded values were ok / running /
             * budget_exhausted / verify_failed and nothing else, so a capped
             * turn was indistinguishable from a clean one in the corpus --
             * and DEFERRED item 7's question ("does a capped one-shot usually
             * answer anything?") could not be asked of the record that was
             * supposed to answer it. The row said the journal already carried
             * stop_reason. It did not; that claim was read, not measured.
             *
             * M732: this used to pass JC_OK, "because reaching this line means
             * the loop returned normally -- a transport error never gets here".
             * Both halves were false. An interrupt reaches this line (a drive's
             * wall-clock SIGINT was journalled `done` while its own stream said
             * `interrupted`, M731), and so does a failed model call. `st` is the
             * loop's real status, and the classifier was always able to read it
             * -- which is also what the stream and the reach footer pass. */
            cJSON_AddStringToObject(o, "stop_reason",
                jc_run_stop_wire(jc_agent_stop_reason(app, st)));
            cJSON_AddBoolToObject(o, "rolled_back", app->env->rolled_back);
            /* M715: the size of what this turn answered -- a fact, not a verdict.
             * A capped turn's last words ("Let me try with explicit tabs:") are
             * text but not an answer, so the reading is left to the measurement
             * (tests/measure/capped_oneshot.py) and the journal records bytes. */
            {
                const char *ans = jc_agent_turn_answer(hist, turn_from);
                cJSON_AddNumberToObject(o, "answer_bytes",
                                        ans != NULL ? (double)strlen(ans) : 0.0);
            }
            cJSON_AddNumberToObject(o, "tokens_used", app->env->tokens_used);
            cJSON_AddNumberToObject(o, "tool_calls",
                                    (double)app->env->tool_calls);
            /* Both meanings, side by side (M459). attempted-many with
             * executed-none is the machine-readable signature of a run
             * thrashing against a gate -- exactly what probe P13 measured and
             * what no single number could show. */
            cJSON_AddNumberToObject(o, "tool_calls_executed",
                                    (double)app->env->tool_calls_executed);
            /* Did this run change anything at all? The completion verify was
             * gated on a mutating tool having run (`snapshotted`), so a run
             * that writes NOTHING never executed its own verifier and still
             * exited 0 (M1048 un-gated the verify -- the `verified` field
             * above is the run-fact this comment then argued for); for a run
             * whose deliverable is a file, "wrote nothing" was
             * indistinguishable from success, and M96's `starved` does not
             * cover it -- that fires on budget exhaustion, not on giving up
             * early. A supervisor needs this fact to tell "did the work and
             * it passed" from "did nothing".
             *
             * M496 -- ASK THE TREE, NOT THE CHECKPOINT. This was
             * `green_commit[0] == '\0'` -- "no pre-edit checkpoint was taken",
             * i.e. "no MUTATING TOOL ran". Those are different facts, and they
             * diverge in the commonest case there is: `run_terminal_command` is a
             * mutating tool, so a checkpoint is taken the moment the model runs a
             * shell command, whether or not that command writes anything.
             *
             * Measured twice, on two models and two projects, while dogfooding:
             * a run that made 9 shell calls and one that made 46 both left `git
             * status` clean, and both reported `no_changes: false`. The second is
             * proven by mtime -- the only modified file predates the run window --
             * so the journal told a supervisor that work had been done on a tree
             * nothing had touched. The field's own name and the paragraph above
             * both promise a statement about CHANGES.
             *
             * The true answer was already being computed three times in this file
             * for M83's out-of-scope guard: diff the tree against the run-start
             * baseline. That is what this does now; `green_commit` remains the
             * right signal for M96's `starved`, which really is asking "did a
             * mutating tool ever run".
             *
             * Only meaningful with snapshots on, so the field is OMITTED rather
             * than guessed when they are off (absent = unknown). A failed diff is
             * also omitted rather than reported as "no changes": an unknown answer
             * must not read as a confident one. */
            if (jc_snapshot_available(app->snapshots)) {
                struct jc_sb chg;
                int have = 0;
                int none = 0;
                jc_sb_init(&chg);
                if (app->env->baseline_commit[0] != '\0' &&
                    jc_snapshot_changed_since(app->snapshots,
                                              app->env->baseline_commit,
                                              &chg) == JC_OK) {
                    have = 1;
                    /* M1095 (D-03): 0 changed files is an empty buffer in the
                     * NUL form too, so this emptiness test is unchanged. */
                    none = (chg.data == NULL || chg.data[0] == '\0');
                } else if (app->env->green_commit[0] == '\0') {
                    /* No baseline to diff against (snapshots armed late, or the
                     * diff failed) AND no checkpoint ever taken: nothing can have
                     * been written through a tool, so the old signal is sound
                     * here and is kept rather than dropping the field. */
                    have = 1;
                    none = 1;
                }
                jc_sb_free(&chg);
                if (have) {
                    cJSON_AddBoolToObject(o, "no_changes", none);
                }
            }
        }
        jc_env_journal_end(app->env, o);
    }

    {
        cJSON *o = telem(app, "turn_end");
        if (o != NULL) {
            long rss_kb = 0;
            cJSON_AddStringToObject(o, "result",
                st == JC_OK ? "ok"
                            : (st == JC_ERR_ABORTED ? "aborted" : "error"));
            if (app->env != NULL) {
                cJSON_AddStringToObject(o, "outcome",
                    jc_env_outcome_name(app->env->outcome));
                cJSON_AddBoolToObject(o, "rolled_back", app->env->rolled_back);
                cJSON_AddNumberToObject(o, "tokens_used",
                                        app->env->tokens_used);
                cJSON_AddNumberToObject(o, "tool_calls",
                                        (double)app->env->tool_calls);
            }
            /* M180: the process's own RSS, so a long run's memory curve
             * lives in the telemetry instead of being unrecoverable after
             * the fact (the 12 GB report had no memory data at all). */
            if (jc_meminfo_self(&rss_kb, NULL)) {
                cJSON_AddNumberToObject(o, "rss_kb", (double)rss_kb);
            }
        }
        jc_eventlog_end(app->telemetry, o);
    }
    return st;
}

int jc_subagent_can_spawn(int agent_depth, int max_depth)
{
    return agent_depth < max_depth;
}

int jc_subagent_iters_at_depth(int base_iters, int depth)
{
    int i = base_iters;
    int d = depth;
    if (d <= 0) {
        return base_iters;
    }
    while (d-- > 0) {
        i = i / 2;
    }
    if (i < JC_SUBAGENT_MIN_ITERS) {
        i = JC_SUBAGENT_MIN_ITERS;
    }
    return i;
}

const char *jc_agent_turn_answer(const struct jc_history *hist, jc_size from)
{
    struct jc_history *h = (struct jc_history *)hist;
    jc_size n = jc_history_len(h);
    jc_size i;
    for (i = n; i > from; i--) {
        struct jc_message *m = jc_history_get(h, i - 1);
        if (m->role == JC_ROLE_ASSISTANT && m->content != NULL &&
            m->content[0] != '\0') {
            return m->content;
        }
    }
    return NULL;
}

const char *jc_agent_last_assistant_text(const struct jc_history *hist)
{
    return jc_agent_turn_answer(hist, 0);
}

/* M848 (S2): the delegate runner, with the POSTURE as a parameter. Every
 * delegate -- spawn_subagent, a parallel child, ask_for_help's helper, a
 * workflow stage -- runs with the AUTO posture (jc_agent_run_subagent, below):
 * its ASK verdicts run without a prompt. A subtask command is the exception: it
 * IS the user's turn, so it runs under the user's own posture (auto_posture 0),
 * and in chat mode an ASK verdict prompts exactly as in the main turn, or is
 * refused headless. While an AUTO-posture delegate runs, app->delegate_auto is
 * raised so the path fence's default turns on (1e): a run that asks nobody has
 * nobody in the loop to catch a stray path. */
static jc_status run_subagent_posture(struct jc_app *app,
                                      struct jc_history *hist,
                                      struct jc_provider *provider,
                                      const char *system_msg,
                                      int include_mutating, int max_iters,
                                      const struct jc_vec *allow_tools,
                                      const struct jc_agent_callbacks *cb,
                                      char **answer_out, int auto_posture)
{
    struct jc_run_opts opts;
    jc_status st;
    int saved_readonly;

    if (answer_out != NULL) {
        *answer_out = NULL;
    }
    memset(&opts, 0, sizeof(opts));
    opts.provider = provider;
    opts.system_msg = system_msg;
    opts.include_mutating = include_mutating;
    /* Multi-level nesting: a subagent may itself spawn a *synchronous*
     * sub-subagent while still under the depth cap (app->agent_depth here is
     * this subagent's own depth); spawn_parallel stays top-level only. Bounded
     * by config.max_subagent_depth (default 2 => one nested level; 0 under
     * --lite) and re-checked as
     * a backstop in subagent_run. */
    opts.exclude_tool = "spawn_parallel";
    opts.exclude_tool2 =
        jc_subagent_can_spawn(app->agent_depth, app->config.max_subagent_depth)
            ? NULL : "spawn_subagent";
    opts.allow = (allow_tools != NULL && allow_tools->len > 0)
                 ? allow_tools : NULL;
    opts.max_iters = max_iters;
    /* M848 (S2): the caller's posture. Before M848 this was JC_TRUE for every
     * delegate, commented "run its sandboxed tools without prompting" -- but
     * nothing sandboxed a subtask command: it got every tool and asked nothing,
     * in chat mode and headless without --auto alike. */
    opts.auto_posture = auto_posture ? JC_TRUE : JC_FALSE;

    /* M750: a read-only delegate is read-only at the GATE, not only on its
     * advertised menu. include_mutating 0 keeps the mutating tools out of the
     * advertisement (jc_tool_build_neutral_ex); app->readonly is the separate
     * flag jc_tool_execute checks when a model calls a tool it was never shown.
     * spawn_subagent {"readonly": true}, a read-only spawn_parallel task, and
     * ask_for_help's helper all pass include_mutating 0 and, before this, left
     * app->readonly as the parent's -- so a child that named write_file anyway
     * was served, and a read-only parallel task ran that write against the LIVE
     * workspace (it has no worktree). M634 had closed exactly this for the
     * workflow's map and refute stages by setting app->readonly around each; the
     * three callers it did not touch kept the gap. Enforcing it here, at the one
     * function every delegate runs through, covers them all and any future
     * caller. The invariant each upholds: include_mutating == 0 iff the child
     * must be read-only. Saved and restored so a writable parent is unaffected,
     * and so the workflow stages' own save/restore nests cleanly. */
    saved_readonly = app->readonly;
    if (!include_mutating) {
        app->readonly = 1;
    }

    if (auto_posture) {
        app->delegate_auto++;   /* M848 (1e): the path fence's default is on */
    }
    st = run_agent_loop(app, hist, cb, &opts);
    if (auto_posture) {
        app->delegate_auto--;
    }

    app->readonly = saved_readonly;

    /* M911 (def-318): the hand-back crosses ONE boundary -- cap it here,
     * spill the full text, name the file. Lifetime is M62 #6's per-turn
     * scratch, so a chain of subagent calls does not accumulate one answer
     * per call on the session arena. */
    if (answer_out != NULL) {
        jc_subagent_answer_into(app, hist, (const char **)answer_out);
    }
    return st;
}

/* def-318 (M911): the subagent hand-back, capped at the boundary.
 *
 * The measured wound (2026-10-05 audit): a child's final answer lands in the
 * parent's context VERBATIM -- 14 of 17 sibling-log hand-backs were over
 * 10 KB, max 31 KB, 301 KB total -- a second context-eater behind the
 * mid-turn pressure the M218 elision pass responds to, and the very elision
 * that is lossy on exactly this material (def-317). The cap is here, at the
 * one point every hand-back crosses, not in the child's instructions.
 *
 * Over the cap: the parent gets the head (the answer's opening carries its
 * claim) plus a note naming the file that holds the FULL text in the 0600
 * preservation store -- the M348/M909 shape, spill-and-reference. A storage
 * failure degrades to the old behaviour: the answer passes through whole,
 * because an uncapped answer is better than a note pointing nowhere (D3).
 * Under the cap: byte-identical, no note. Lifetime per M62 #6: the per-turn
 * scratch, so a chain of subagent calls does not accumulate answers on the
 * session arena. */
#define JC_SUBAGENT_ANSWER_CAP 4096

jc_status jc_subagent_answer_into(struct jc_app *app,
                                  const struct jc_history *hist,
                                  const char **answer_out)
{
    const char *ans;
    jc_size len;

    *answer_out = NULL;
    ans = jc_agent_last_assistant_text(hist);
    if (ans == NULL || ans[0] == '\0') {
        return JC_OK;
    }
    len = (jc_size)strlen(ans);
    if (len <= JC_SUBAGENT_ANSWER_CAP) {
        *answer_out = jc_arena_strdup(jc_app_scratch(app), ans);
        return JC_OK;
    }
    {
        struct jc_sb sb;
        char path[700];
        jc_size head = JC_SUBAGENT_ANSWER_CAP - 300;

        if (!jc_toolout_preserve(app, "subagent", ans, len,
                                 path, sizeof(path))) {
            *answer_out = jc_arena_strdup(jc_app_scratch(app), ans);
            return JC_OK;
        }
        head = jc_utf8_trunc_len(ans, head);
        jc_sb_init(&sb);
        jc_sb_append_n(&sb, ans, head);
        jc_sb_append_fmt(&sb,
            "\n\n... [the complete answer is at %s -- read or search that "
            "path for the rest instead of re-running the subtask; this note "
            "carries the opening only, %lu of %lu bytes] ...\n",
            path, (unsigned long)head, (unsigned long)len);
        /* M911 contract: answer_out is ARENA-owned (per-turn scratch), as the
         * pre-M911 full-answer path was. Callers do not free it. Copy the built
         * buffer into the scratch and free the transient sb. */
        *answer_out = jc_arena_strdup(jc_app_scratch(app), sb.data);
        jc_sb_free(&sb);
        return JC_OK;
    }
}

jc_status jc_agent_run_subagent(struct jc_app *app, struct jc_history *hist,
                                struct jc_provider *provider,
                                const char *system_msg,
                                int include_mutating, int max_iters,
                                const struct jc_vec *allow_tools,
                                const struct jc_agent_callbacks *cb,
                                char **answer_out)
{
    /* A delegate: the AUTO posture, as documented in TOOL_DECISIONS.md. Whether
     * spawn_subagent should keep it is open (DEFERRED def-286); since M848 it
     * runs with the path fence on (run_subagent_posture). */
    return run_subagent_posture(app, hist, provider, system_msg,
                                include_mutating, max_iters, allow_tools, cb,
                                answer_out, 1);
}

jc_status jc_agent_run_command_subtask(struct jc_app *app,
                                       struct jc_history *hist,
                                       const char *prompt,
                                       const char *output_path,
                                       const char *language,
                                       const struct jc_agent_callbacks *cb)
{
    const char *lang;
    struct jc_history sub;
    char *sysmsg;
    char *answer = NULL;
    jc_status st;
    int iters = app->config.max_subagent_iters;
    char out_abs[1400];
    double out_mtime_before = 0.0;
    int out_had_content = 0;    /* M423: the target already held an artifact */

    if (iters <= 0) {
        iters = app->config.max_tool_iters;
    }
    /* M596: the command's `agent:` profile (set on app->persona_override by
     * jc_app_command_agent_apply before this call) is the identity the subtask
     * runs under. Until M596 this built the generic delegate prompt and the
     * persona was silently dropped -- the scaffolded mentor never saw its own
     * FORMAT IS STRICT block. M848 (def-287): the profile's `tools:` now fences
     * the subtask as well (app->command_tools, below) -- it can only narrow,
     * and a subtask runs on its own prompt, so no cache prefix is broken. */
    /* M597: the command's own `language:` wins; else the session language. A
     * subtask's answer reaches the user with no parent in between, so it must
     * carry the directive the top-level prompt carries. */
    lang = (language != NULL && language[0] != '\0') ? language
                                                     : app->config.language;
    sysmsg = jc_sysmsg_build_sub_as(app, app->persona_override, lang);

    jc_history_init(&sub);
    jc_history_add(&sub, JC_ROLE_USER, prompt != NULL ? prompt : "");

    /* M79: snapshot the declared output file's mtime so we can tell whether the
     * subtask actually wrote it (vs narrating its result and never calling
     * write_file). */
    out_abs[0] = '\0';
    if (output_path != NULL && output_path[0] != '\0') {
        if (output_path[0] == '/') {
            jc_snprintf(out_abs, sizeof(out_abs), "%s", output_path);
        } else {
            jc_snprintf(out_abs, sizeof(out_abs), "%s/%s", app->cwd,
                        output_path);
        }
        out_mtime_before = jc_file_mtime(out_abs);
        out_had_content = (jc_file_size(out_abs) > 0);
    }

    /* Forward the callbacks directly: a subtask command IS the user's turn, so
     * its output should stream like a normal turn (in both headless and TUI),
     * unlike a nested spawn_subagent which is gated on app->stream_subagents.
     * M848 (S2, decision 1c): and it runs under the user's OWN posture, not a
     * delegate's. Before M848 it ran with the AUTO posture and no allow list, so
     * /learn -- the mentor, every tool -- ran the shell with no prompt in a chat
     * session, and headless without --auto, where a plain prompt was refused.
     * In --auto nothing changes: the verdict there is ALLOW before any posture
     * is consulted, so learn-on-stop runs as it did. */
    app->agent_depth++;
    st = run_subagent_posture(app, &sub, app->provider, sysmsg,
                              !app->readonly, iters, app->command_tools, cb,
                              &answer, 0);
    app->agent_depth--;
    jc_history_free(&sub);

    /* M79: if the command declared an output file and the subtask left it
     * unchanged (the model didn't write it), persist the answer there so the
     * work isn't lost to stdout. The draft parser tolerates surrounding prose,
     * so a narrated proposal still yields usable lessons; the human curates. */
    if (out_abs[0] != '\0' && answer != NULL && answer[0] != '\0' &&
        jc_file_mtime(out_abs) == out_mtime_before) {
        /* M423: write the answer BESIDE an artifact that was already there,
         * never over it. This fallback exists so a narrated result is not lost;
         * writing unconditionally made it destroy the very kind of thing it was
         * built to preserve. Measured: learn-on-stop -- which fires after ANY
         * completed --auto run -- replaced an 85-line reviewed lessons draft
         * with four lines of mid-thought narration from a run about something
         * else entirely. The old file is the human's; the answer is the model's;
         * a fallback may not choose between them, so both are kept and the
         * operator is told. */
        char alt[1400 + 8];
        const char *target = out_abs;

        if (out_had_content) {
            jc_snprintf(alt, sizeof(alt), "%s.answer", out_abs);
            target = alt;
        }
        if (jc_write_file(target, answer, (jc_size)strlen(answer)) == JC_OK) {
            if (out_had_content) {
                jc_logf(JC_LOG_WARN,
                        "[command] subtask did not write %s, which already had "
                        "content -- left it untouched and put the answer "
                        "(%lu bytes) in %s", out_abs,
                        (unsigned long)strlen(answer), target);
            } else {
                jc_logf(JC_LOG_INFO,
                        "[command] subtask did not write %s; persisted its "
                        "answer (%lu bytes) for review", out_abs,
                        (unsigned long)strlen(answer));
            }
        }
    }

    /* Record only the prompt + final answer in the real conversation, keeping
     * the subtask's intermediate tool calls out of the main context. */
    jc_history_add(hist, JC_ROLE_USER, prompt != NULL ? prompt : "");
    if (answer != NULL && answer[0] != '\0') {
        jc_history_add(hist, JC_ROLE_ASSISTANT, answer);
    }
    return st;
}
