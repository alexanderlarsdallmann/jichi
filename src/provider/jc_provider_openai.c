/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_provider_openai.c - OpenAI-compatible chat completions provider. */

#include "prov_internal.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_uuid.h"
#include "jc_log.h"

#include <stdlib.h>
#include <string.h>

/* Map a neutral tools array to OpenAI's [{type:function, function:{...}}]. */
static cJSON *map_tools(const cJSON *tools)
{
    cJSON *arr;
    cJSON *t;
    if (tools == NULL || !cJSON_IsArray(tools)) {
        return NULL;
    }
    arr = cJSON_CreateArray();
    cJSON_ArrayForEach(t, tools) {
        cJSON *wrap = cJSON_CreateObject();
        cJSON *fn = cJSON_CreateObject();
        const char *name = jc_json_get_str(t, "name", "");
        const char *desc = jc_json_get_str(t, "description", "");
        cJSON *params = cJSON_GetObjectItem(t, "parameters");
        cJSON_AddStringToObject(wrap, "type", "function");
        cJSON_AddStringToObject(fn, "name", name);
        cJSON_AddStringToObject(fn, "description", desc);
        if (params != NULL) {
            /* Re-serialise/parse to deep-copy the schema object. */
            char *s = cJSON_PrintUnformatted(params);
            cJSON *copy = (s != NULL) ? cJSON_Parse(s) : NULL;
            free(s);
            if (copy == NULL) {
                copy = cJSON_CreateObject();
            }
            cJSON_AddItemToObject(fn, "parameters", copy);
        }
        cJSON_AddItemToObject(wrap, "function", fn);
        cJSON_AddItemToArray(arr, wrap);
    }
    return arr;
}

/* Read a `usage` object into the provider's token counters, if present.
 *
 * Shared by the streaming path (usage arrives on the final chunk when
 * include_usage is set) and by parse_full (M167: non-streaming replies carry
 * usage top-level, and it was previously dropped -- so a one-shot call's real
 * token counts were invisible, including to the `doctor --live` probe that wants
 * to report the true prefix size). */
static void oa_read_usage(struct prov_state *s, const cJSON *root)
{
    cJSON *usage = cJSON_GetObjectItem((cJSON *)root, "usage");
    cJSON *det;
    double prompt;
    double cached = 0.0;
    if (usage == NULL) {
        return;
    }
    prompt = jc_json_get_num(usage, "prompt_tokens", 0.0);
    s->usage_out = jc_json_get_num(usage, "completion_tokens", s->usage_out);
    /* Server-side prompt caching is automatic on OpenAI-compatible backends
     * (vLLM/LM Studio/OpenAI); they report the cached portion of the prompt
     * under prompt_tokens_details.cached_tokens (M31a). prompt_tokens is the
     * TOTAL (cached + uncached) -- subtract the cached portion so usage_in is
     * the full-price input, matching the Anthropic semantics the cost model
     * (M31c) expects. There is no separate cache-write count. */
    det = cJSON_GetObjectItem(usage, "prompt_tokens_details");
    if (det != NULL) {
        cached = jc_json_get_num(det, "cached_tokens", 0.0);
    }
    s->cache_read_in = cached;
    s->usage_in = prompt - cached;
    if (s->usage_in < 0.0) {
        s->usage_in = 0.0;
    }
}

static cJSON *build_messages(const struct jc_history *hist)
{
    cJSON *arr = cJSON_CreateArray();
    jc_size i;
    for (i = 0; i < jc_history_len((struct jc_history *)hist); i++) {
        struct jc_message *m =
            jc_history_get((struct jc_history *)hist, i);
        cJSON *jm;
        /* Never serialise the in-flight assistant placeholder (M166): a
         * content-free trailing assistant turn stops a small local model from
         * calling any tool at all. See docs/ANECDOTES.md #19. */
        if (jc_prov_msg_is_placeholder(m)) {
            continue;
        }
        jm = cJSON_CreateObject();
        cJSON_AddStringToObject(jm, "role", jc_role_str(m->role));

        if (m->role == JC_ROLE_TOOL) {
            cJSON_AddStringToObject(jm, "tool_call_id",
                                    m->tool_call_id ? m->tool_call_id : "");
            cJSON_AddStringToObject(jm, "content",
                                    m->content ? m->content : "");
        } else if (m->role == JC_ROLE_ASSISTANT &&
                   jc_msg_tool_call_count(m) > 0) {
            cJSON *tcs = cJSON_CreateArray();
            jc_size k;
            if (m->content != NULL) {
                cJSON_AddStringToObject(jm, "content", m->content);
            } else {
                cJSON_AddNullToObject(jm, "content");
            }
            for (k = 0; k < jc_msg_tool_call_count(m); k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
                cJSON *jt = cJSON_CreateObject();
                cJSON *fn = cJSON_CreateObject();
                char *wire = jc_prov_args_wire(tc->arguments_json);
                cJSON_AddStringToObject(jt, "id", tc->id ? tc->id : "");
                cJSON_AddStringToObject(jt, "type", "function");
                cJSON_AddStringToObject(fn, "name", tc->name ? tc->name : "");
                /* M269: never echo an unparseable blob -- a strict server
                 * re-parses this string and 400s the whole request. See
                 * jc_prov_args_wire. */
                cJSON_AddStringToObject(fn, "arguments",
                                        wire != NULL ? wire : "{}");
                free(wire);
                cJSON_AddItemToObject(jt, "function", fn);
                cJSON_AddItemToArray(tcs, jt);
            }
            cJSON_AddItemToObject(jm, "tool_calls", tcs);
        } else if (m->role == JC_ROLE_USER && jc_msg_image_count(m) > 0) {
            /* Image attachments (M29): content is an array of text + image_url
             * (data-URI) parts. */
            cJSON *content = cJSON_CreateArray();
            jc_size k;
            if (m->content != NULL && m->content[0] != '\0') {
                cJSON *tb = cJSON_CreateObject();
                cJSON_AddStringToObject(tb, "type", "text");
                cJSON_AddStringToObject(tb, "text", m->content);
                cJSON_AddItemToArray(content, tb);
            }
            for (k = 0; k < jc_msg_image_count(m); k++) {
                struct jc_image *img = jc_msg_image_at(m, k);
                cJSON *ib = cJSON_CreateObject();
                cJSON *iu = cJSON_CreateObject();
                struct jc_sb url;
                cJSON_AddStringToObject(ib, "type", "image_url");
                jc_sb_init(&url);
                jc_sb_append_fmt(&url, "data:%s;base64,%s",
                    img->media_type ? img->media_type : "image/png",
                    img->data ? img->data : "");
                cJSON_AddStringToObject(iu, "url", url.data ? url.data : "");
                jc_sb_free(&url);
                cJSON_AddItemToObject(ib, "image_url", iu);
                cJSON_AddItemToArray(content, ib);
            }
            cJSON_AddItemToObject(jm, "content", content);
        } else {
            cJSON_AddStringToObject(jm, "content", m->content ? m->content : "");
        }
        cJSON_AddItemToArray(arr, jm);
    }
    return arr;
}

static jc_status oa_build_request(struct jc_provider *self,
                                  const struct jc_history *hist,
                                  const char *system_msg,
                                  const cJSON *tools,
                                  int streaming,
                                  char **body_out)
{
    cJSON *root;
    cJSON *messages;
    cJSON *jtools;

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", self->model->model);

    /* A system message, if provided, is prepended as a system role. */
    messages = build_messages(hist);
    if (system_msg != NULL && system_msg[0] != '\0') {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_msg);
        /* Insert at the front. */
        sys->next = messages->child;
        if (messages->child != NULL) {
            messages->child->prev = sys;
        }
        messages->child = sys;
    }
    cJSON_AddItemToObject(root, "messages", messages);

    if (streaming) {
        cJSON *so;
        cJSON_AddBoolToObject(root, "stream", 1);
        /* Ask the API to include a final usage chunk. */
        so = cJSON_AddObjectToObject(root, "stream_options");
        cJSON_AddBoolToObject(so, "include_usage", 1);
    }
    /* Always send an explicit max_tokens (M84): an omitted value makes some
     * OpenAI-compatible proxies default to the full context window, which can't
     * fit any prompt and 400s every request. */
    cJSON_AddNumberToObject(root, "max_tokens",
                            (double)jc_config_effective_max_tokens(
                                self->model->max_tokens,
                                self->model->context_limit));
    if (self->model->temperature >= 0.0) {
        cJSON_AddNumberToObject(root, "temperature", self->model->temperature);
    }
    /* Stable per-session prompt_cache_key (M31c): harmless for vLLM/LM Studio,
     * and nudges OpenAI's automatic caching to route the session to a warm
     * backend. Set only when caching is enabled (the key is generated at
     * provider-create time). */
    {
        struct prov_state *st = (struct prov_state *)self->state;
        if (st->cache_key[0] != '\0') {
            cJSON_AddStringToObject(root, "prompt_cache_key", st->cache_key);
        }
    }
    jtools = map_tools(tools);
    if (jtools != NULL) {
        cJSON_AddItemToObject(root, "tools", jtools);
        cJSON_AddStringToObject(root, "tool_choice", "auto");
    }

    {
        jc_status st = jc_prov_print_body(root, body_out);
        cJSON_Delete(root);
        return st;
    }
}

static void oa_add_headers(struct jc_provider *self, struct jc_http_headers *h)
{
    char auth[1100];
    jc_http_headers_add(h, "Content-Type: application/json");
    if (self->model->api_key != NULL) {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s",
                    self->model->api_key);
        jc_http_headers_add(h, auth);
    }
}

static const char *oa_endpoint(struct jc_provider *self)
{
    struct prov_state *s = (struct prov_state *)self->state;
    return s->endpoint;
}

static void oa_stream_reset(struct jc_provider *self)
{
    jc_prov_state_reset((struct prov_state *)self->state);
}

static void oa_handle_tool_calls(struct prov_state *s, cJSON *delta)
{
    cJSON *tcs = cJSON_GetObjectItem(delta, "tool_calls");
    cJSON *tc;
    if (!cJSON_IsArray(tcs)) {
        return;
    }
    cJSON_ArrayForEach(tc, tcs) {
        int idx = (int)jc_json_get_num(tc, "index", 0.0);
        struct prov_call *slot = jc_prov_call_slot(s, idx);
        const char *id;
        cJSON *fn;
        if (slot == NULL) {
            continue;
        }
        slot->used = 1;
        id = jc_json_get_str(tc, "id", NULL);
        if (id != NULL && slot->id[0] == '\0') {
            jc_snprintf(slot->id, sizeof(slot->id), "%s", id);
        }
        fn = cJSON_GetObjectItem(tc, "function");
        if (fn != NULL) {
            const char *name = jc_json_get_str(fn, "name", NULL);
            const char *args = jc_json_get_str(fn, "arguments", NULL);
            if (name != NULL && slot->name.len == 0) {
                jc_sb_append(&slot->name, name);
            }
            if (args != NULL) {
                jc_sb_append(&slot->args, args);
            }
        }
    }
}

/* A reasoning model that spent its whole output budget on reasoning_content can
 * finish a turn with empty answer text and no tool call -- silent across every
 * surface (TUI shows nothing, ACP end_turn with no chunks, one-shot helpers
 * return NULL). Surface it at the source so the cause is obvious. Fires only
 * when it actually bit: reasoning seen, no text, no tool call. */
static void oa_reasoning_empty_hint(struct prov_state *s)
{
    /* "no answer" means the accumulated text has no non-whitespace content --
     * not merely !text_started, since an empty-string content delta ("") still
     * flips text_started while adding zero bytes. */
    int has_text = 0;
    jc_size i;
    for (i = 0; s->text.data != NULL && i < s->text.len; i++) {
        unsigned char c = (unsigned char)s->text.data[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            has_text = 1;
            break;
        }
    }
    if (!has_text && s->saw_reasoning && !s->calls[0].used) {
        /* M521: say WHICH of the two it is, from evidence already collected.
         * `hit_length_cap` comes from finish_reason == "length" (M334), so
         * blaming the output ceiling is checkable rather than assumed -- and
         * measured on 2026-08-21 both cases occurred in ONE bake-off:
         * gemma-4-12b spent all 13,107 derived output tokens reasoning and
         * answered nothing (the ceiling really was the limit), while
         * qwen3.5-9b ended a turn after 62 tokens of the same budget with
         * nothing left to say (the ceiling was irrelevant). The old text said
         * "raise the model's maxTokens" in both cases, sending the second
         * reader to tune a number that was never the limit. A diagnostic that
         * asserts an unchecked cause is the same defect as a classifier whose
         * else-branch is a finding (docs/TEST_INTEGRITY.md). */
        if (s->hit_length_cap) {
            jc_logf(JC_LOG_WARN,
                "model returned reasoning but no answer text, and the reply hit "
                "the OUTPUT CEILING -- raise this model's maxTokens, or declare "
                "a larger contextLength (max_tokens defaults to a fifth of it)");
        } else if (s->usage_out > 0.0) {
            jc_logf(JC_LOG_WARN,
                "model returned reasoning but no answer text, ending on its own "
                "after %.0f output tokens -- it did NOT hit the output ceiling, "
                "so raising maxTokens will not change this",
                s->usage_out);
        } else {
            jc_logf(JC_LOG_WARN,
                "model returned reasoning but no answer text, and did not hit "
                "the output ceiling -- this turn produced nothing to show");
        }
    }
}

static void oa_on_event(struct jc_provider *self,
                        const struct jc_sse_event *ev,
                        struct jc_message *out,
                        struct jc_stream_sink *sink,
                        int *done)
{
    struct prov_state *s = (struct prov_state *)self->state;
    cJSON *root;
    cJSON *choices;
    cJSON *choice0;
    cJSON *delta;
    const char *content;

    if (ev->data == NULL || ev->data[0] == '\0') {
        return;
    }
    if (strcmp(ev->data, "[DONE]") == 0) {
        oa_reasoning_empty_hint(s);
        jc_prov_flush(s, out);
        *done = 1;
        return;
    }
    root = cJSON_Parse(ev->data);
    if (root == NULL) {
        return;
    }
    /* Surface API errors. */
    if (cJSON_GetObjectItem(root, "error") != NULL) {
        const char *msg = jc_json_get_str(cJSON_GetObjectItem(root, "error"),
                                          "message", "provider error");
        jc_prov_emit_text(s, sink, msg, (jc_size)strlen(msg));
        jc_prov_flush(s, out);
        *done = 1;
        cJSON_Delete(root);
        return;
    }

    /* A usage object may arrive on the final chunk (include_usage). */
    oa_read_usage(s, root);

    choices = cJSON_GetObjectItem(root, "choices");
    choice0 = cJSON_GetArrayItem(choices, 0);
    if (choice0 != NULL) {
        /* M334: "length" means the model was cut off at max_tokens. Until now
         * jichi parsed no finish reason at all, so a tool call severed mid-
         * argument arrived looking merely malformed -- and M148's repair then
         * closed the braces and produced a VALID object with no fields, which
         * sent the model to fix an argument-shape problem it did not have.
         * Measured: 197 such calls in one run, 6,977,850 tokens. */
        const char *fin = jc_json_get_str(choice0, "finish_reason", NULL);
        if (fin != NULL && strcmp(fin, "length") == 0) {
            s->hit_length_cap = 1;
        }
        delta = cJSON_GetObjectItem(choice0, "delta");
        if (delta != NULL) {
            const char *reasoning;
            content = jc_json_get_str(delta, "content", NULL);
            if (content != NULL) {
                jc_prov_emit_text(s, sink, content, (jc_size)strlen(content));
            }
            /* Reasoning models stream their scratchpad here (LM Studio / vLLM
             * use reasoning_content; some use reasoning). Not the answer -- we
             * don't emit it -- but track it to explain an empty turn. */
            reasoning = jc_json_get_str(delta, "reasoning_content", NULL);
            if (reasoning == NULL) {
                reasoning = jc_json_get_str(delta, "reasoning", NULL);
            }
            if (reasoning != NULL && reasoning[0] != '\0') {
                s->saw_reasoning = 1;
            }
            oa_handle_tool_calls(s, delta);
        }
    }
    cJSON_Delete(root);
}

static jc_status oa_parse_full(struct jc_provider *self, const char *body,
                               struct jc_message *out)
{
    struct prov_state *s = (struct prov_state *)self->state;
    cJSON *root = cJSON_Parse(body);
    cJSON *choice0;
    cJSON *message;
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    jc_prov_state_reset(s);
    oa_read_usage(s, root); /* M167: non-streaming replies carry usage too */
    choice0 = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "choices"), 0);
    message = (choice0 != NULL) ? cJSON_GetObjectItem(choice0, "message") : NULL;
    if (message != NULL) {
        const char *content = jc_json_get_str(message, "content", NULL);
        const char *reasoning = jc_json_get_str(message, "reasoning_content",
                                                NULL);
        cJSON *tcs = cJSON_GetObjectItem(message, "tool_calls");
        cJSON *tc;
        if (reasoning == NULL) {
            reasoning = jc_json_get_str(message, "reasoning", NULL);
        }
        if (reasoning != NULL && reasoning[0] != '\0') {
            s->saw_reasoning = 1;
        }
        if (content != NULL) {
            jc_prov_emit_text(s, NULL, content, (jc_size)strlen(content));
        }
        if (cJSON_IsArray(tcs)) {
            int idx = 0;
            cJSON_ArrayForEach(tc, tcs) {
                struct prov_call *slot = jc_prov_call_slot(s, idx);
                cJSON *fn = cJSON_GetObjectItem(tc, "function");
                const char *id = jc_json_get_str(tc, "id", NULL);
                if (slot != NULL) {
                    slot->used = 1;
                    if (id != NULL) {
                        jc_snprintf(slot->id, sizeof(slot->id), "%s", id);
                    }
                    if (fn != NULL) {
                        const char *name = jc_json_get_str(fn, "name", "");
                        const char *args = jc_json_get_str(fn, "arguments", "{}");
                        jc_sb_append(&slot->name, name);
                        jc_sb_append(&slot->args, args);
                    }
                }
                idx++;
            }
        }
    }
    oa_reasoning_empty_hint(s);
    jc_prov_flush(s, out);
    cJSON_Delete(root);
    return JC_OK;
}

static void oa_free(struct jc_provider *self)
{
    if (self == NULL) {
        return;
    }
    if (self->state != NULL) {
        jc_prov_state_free((struct prov_state *)self->state);
        free(self->state);
    }
    free(self);
}

/* M521: the stream ended without a terminal event. Same finishing work the
 * [DONE] branch does, minus setting *done -- the agent already knows. */
static void oa_stream_end(struct jc_provider *self, struct jc_message *out)
{
    struct prov_state *s = (struct prov_state *)self->state;
    oa_reasoning_empty_hint(s);
    jc_prov_flush(s, out);
}

static const struct jc_provider_vtable OA_VTABLE = {
    oa_build_request,
    oa_add_headers,
    oa_endpoint,
    oa_stream_reset,
    oa_on_event,
    oa_stream_end,
    oa_parse_full,
    jc_prov_get_usage,
    jc_prov_get_cache_usage,
    oa_free
};

struct jc_provider *jc_provider_openai_create(const struct jc_model_cfg *model)
{
    struct jc_provider *p;
    struct prov_state *s;
    const char *base;

    p = (struct jc_provider *)malloc(sizeof(struct jc_provider));
    if (p == NULL) {
        return NULL;
    }
    s = (struct prov_state *)malloc(sizeof(struct prov_state));
    if (s == NULL) {
        free(p);
        return NULL;
    }
    jc_prov_state_init(s);

    base = (model->api_base != NULL) ? model->api_base : "https://api.openai.com";
    if (strstr(base, "/v1") != NULL) {
        jc_snprintf(s->endpoint, sizeof(s->endpoint),
                    "%s/chat/completions", base);
    } else {
        jc_snprintf(s->endpoint, sizeof(s->endpoint),
                    "%s/v1/chat/completions", base);
    }

    /* A stable per-session prompt_cache_key (M31c) when caching is enabled. */
    if (model->prompt_cache != 0) {
        jc_uuid_v4(s->cache_key);
    }

    p->vt = &OA_VTABLE;
    p->model = model;
    p->state = s;
    return p;
}
