/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_provider_anthropic.c - Anthropic Messages API provider. */

#include "prov_internal.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_promptcache.h"

#include <stdlib.h>
#include <string.h>

#define ANTHROPIC_VERSION "2023-06-01"

/* Map a neutral tools array to Anthropic [{name, description, input_schema}]. */
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
        const char *name = jc_json_get_str(t, "name", "");
        const char *desc = jc_json_get_str(t, "description", "");
        cJSON *params = cJSON_GetObjectItem(t, "parameters");
        cJSON_AddStringToObject(wrap, "name", name);
        cJSON_AddStringToObject(wrap, "description", desc);
        if (params != NULL) {
            char *s = cJSON_PrintUnformatted(params);
            cJSON *copy = (s != NULL) ? cJSON_Parse(s) : NULL;
            free(s);
            if (copy == NULL) {
                copy = cJSON_CreateObject();
            }
            cJSON_AddItemToObject(wrap, "input_schema", copy);
        }
        cJSON_AddItemToArray(arr, wrap);
    }
    return arr;
}

/* Parse a tool-call arguments JSON string into an object. The API requires
 * `input` to be an object, but a malformed blob must NOT silently become {}
 * (M145): the rebuilt request would show the model a clean empty input right
 * beside a tool_result saying its arguments failed to parse -- contradicting
 * the evidence it needs to self-correct. Preserve the raw text under a
 * self-describing key instead, so the request stays valid AND truthful. */
static cJSON *args_to_object(const char *args_json)
{
    cJSON *obj = NULL;
    if (args_json != NULL && args_json[0] != '\0') {
        obj = cJSON_Parse(args_json);
    }
    if (obj != NULL && cJSON_IsObject(obj)) {
        return obj;
    }
    if (obj != NULL) {
        cJSON_Delete(obj); /* valid JSON but not an object: preserve raw too */
    }
    obj = cJSON_CreateObject();
    if (obj != NULL && args_json != NULL && args_json[0] != '\0') {
        cJSON_AddStringToObject(obj, "_unparsed_arguments", args_json);
    }
    return obj;
}

/* Build the Anthropic messages array, merging consecutive tool results into a
 * single user message of tool_result blocks (required by the API). */
static cJSON *build_messages(const struct jc_history *hist)
{
    cJSON *arr = cJSON_CreateArray();
    jc_size i;
    jc_size n = jc_history_len((struct jc_history *)hist);

    i = 0;
    while (i < n) {
        struct jc_message *m = jc_history_get((struct jc_history *)hist, i);

        if (m->role == JC_ROLE_SYSTEM) {
            i++;
            continue; /* handled as top-level system */
        }

        /* Never serialise the in-flight assistant placeholder (M166): a
         * content-free trailing assistant turn stops a small local model from
         * calling any tool at all, and the Messages API rejects an empty text
         * block outright. See docs/ANECDOTES.md #19. */
        if (jc_prov_msg_is_placeholder(m)) {
            i++;
            continue;
        }

        if (m->role == JC_ROLE_TOOL) {
            /* Coalesce a run of tool results into one user message. */
            cJSON *jm = cJSON_CreateObject();
            cJSON *content = cJSON_CreateArray();
            cJSON_AddStringToObject(jm, "role", "user");
            while (i < n) {
                struct jc_message *tm =
                    jc_history_get((struct jc_history *)hist, i);
                cJSON *block;
                if (tm->role != JC_ROLE_TOOL) {
                    break;
                }
                block = cJSON_CreateObject();
                cJSON_AddStringToObject(block, "type", "tool_result");
                cJSON_AddStringToObject(block, "tool_use_id",
                                        tm->tool_call_id ? tm->tool_call_id : "");
                cJSON_AddStringToObject(block, "content",
                                        tm->content ? tm->content : "");
                if (tm->is_error) {
                    cJSON_AddBoolToObject(block, "is_error", 1);
                }
                cJSON_AddItemToArray(content, block);
                i++;
            }
            cJSON_AddItemToObject(jm, "content", content);
            cJSON_AddItemToArray(arr, jm);
            continue;
        }

        if (m->role == JC_ROLE_ASSISTANT && jc_msg_tool_call_count(m) > 0) {
            cJSON *jm = cJSON_CreateObject();
            cJSON *content = cJSON_CreateArray();
            jc_size k;
            cJSON_AddStringToObject(jm, "role", "assistant");
            if (m->content != NULL && m->content[0] != '\0') {
                cJSON *tb = cJSON_CreateObject();
                cJSON_AddStringToObject(tb, "type", "text");
                cJSON_AddStringToObject(tb, "text", m->content);
                cJSON_AddItemToArray(content, tb);
            }
            for (k = 0; k < jc_msg_tool_call_count(m); k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
                cJSON *ub = cJSON_CreateObject();
                cJSON_AddStringToObject(ub, "type", "tool_use");
                cJSON_AddStringToObject(ub, "id", tc->id ? tc->id : "");
                cJSON_AddStringToObject(ub, "name", tc->name ? tc->name : "");
                cJSON_AddItemToObject(ub, "input",
                                      args_to_object(tc->arguments_json));
                cJSON_AddItemToArray(content, ub);
            }
            cJSON_AddItemToObject(jm, "content", content);
            cJSON_AddItemToArray(arr, jm);
            i++;
            continue;
        }

        /* A user message carrying image attachments (M29): content becomes a
         * block array -- an optional text block plus one image block each. */
        if (m->role == JC_ROLE_USER && jc_msg_image_count(m) > 0) {
            cJSON *jm = cJSON_CreateObject();
            cJSON *content = cJSON_CreateArray();
            jc_size k;
            cJSON_AddStringToObject(jm, "role", "user");
            if (m->content != NULL && m->content[0] != '\0') {
                cJSON *tb = cJSON_CreateObject();
                cJSON_AddStringToObject(tb, "type", "text");
                cJSON_AddStringToObject(tb, "text", m->content);
                cJSON_AddItemToArray(content, tb);
            }
            for (k = 0; k < jc_msg_image_count(m); k++) {
                struct jc_image *img = jc_msg_image_at(m, k);
                cJSON *ib = cJSON_CreateObject();
                cJSON *src = cJSON_CreateObject();
                cJSON_AddStringToObject(ib, "type", "image");
                cJSON_AddStringToObject(src, "type", "base64");
                cJSON_AddStringToObject(src, "media_type",
                    img->media_type ? img->media_type : "image/png");
                cJSON_AddStringToObject(src, "data", img->data ? img->data : "");
                cJSON_AddItemToObject(ib, "source", src);
                cJSON_AddItemToArray(content, ib);
            }
            cJSON_AddItemToObject(jm, "content", content);
            cJSON_AddItemToArray(arr, jm);
            i++;
            continue;
        }

        /* Plain user or assistant text. */
        {
            cJSON *jm = cJSON_CreateObject();
            cJSON_AddStringToObject(jm, "role",
                m->role == JC_ROLE_ASSISTANT ? "assistant" : "user");
            cJSON_AddStringToObject(jm, "content", m->content ? m->content : "");
            cJSON_AddItemToArray(arr, jm);
            i++;
        }
    }
    return arr;
}

/* Collect all system-role contents (plus an optional extra) into one string. */
static char *collect_system(const struct jc_history *hist, const char *extra)
{
    struct jc_sb sb;
    jc_size i;
    char *result;
    jc_sb_init(&sb);
    for (i = 0; i < jc_history_len((struct jc_history *)hist); i++) {
        struct jc_message *m = jc_history_get((struct jc_history *)hist, i);
        if (m->role == JC_ROLE_SYSTEM && m->content != NULL) {
            if (sb.len > 0) {
                jc_sb_append(&sb, "\n\n");
            }
            jc_sb_append(&sb, m->content);
        }
    }
    if (extra != NULL && extra[0] != '\0') {
        if (sb.len > 0) {
            jc_sb_append(&sb, "\n\n");
        }
        jc_sb_append(&sb, extra);
    }
    result = jc_sb_finish(&sb);
    jc_sb_free(&sb);
    return result;
}

/* Attach a cache_control:{"type":"ephemeral"[,"ttl":"1h"]} marker to a content
 * block (M31b; the 1-hour TTL is M31e -- the 5-minute default is implicit). */
static void add_cache_control(cJSON *block, int ttl_1h)
{
    cJSON *cc = cJSON_CreateObject();
    cJSON_AddStringToObject(cc, "type", "ephemeral");
    if (ttl_1h) {
        cJSON_AddStringToObject(cc, "ttl", "1h");
    }
    cJSON_AddItemToObject(block, "cache_control", cc);
}

/* Put a cache breakpoint on a message's last content block. Plain user/assistant
 * messages carry their text as a bare string; convert that to a one-element text
 * block array so the marker has a block to ride on. A block array gets the
 * marker on its final element. */
static void cache_control_last_block(cJSON *msg, int ttl_1h)
{
    cJSON *content;
    if (msg == NULL) {
        return;
    }
    content = cJSON_GetObjectItem(msg, "content");
    if (content == NULL) {
        return;
    }
    if (cJSON_IsString(content)) {
        const char *txt = content->valuestring ? content->valuestring : "";
        cJSON *arr = cJSON_CreateArray();
        cJSON *blk = cJSON_CreateObject();
        cJSON_AddStringToObject(blk, "type", "text");
        cJSON_AddStringToObject(blk, "text", txt);
        add_cache_control(blk, ttl_1h);
        cJSON_AddItemToArray(arr, blk);
        cJSON_ReplaceItemInObject(msg, "content", arr);
    } else if (cJSON_IsArray(content)) {
        int n = cJSON_GetArraySize(content);
        if (n > 0) {
            add_cache_control(cJSON_GetArrayItem(content, n - 1), ttl_1h);
        }
    }
}

static jc_status an_build_request(struct jc_provider *self,
                                  const struct jc_history *hist,
                                  const char *system_msg,
                                  const cJSON *tools,
                                  int streaming,
                                  char **body_out)
{
    cJSON *root;
    cJSON *jtools;
    cJSON *msgs;
    char *system;
    long max_tokens;
    int cache_on = self->model->prompt_cache != 0;
    int ttl_1h = self->model->prompt_cache_1h != 0;
    struct jc_promptcache_plan plan;

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", self->model->model);

    max_tokens = jc_config_effective_max_tokens(self->model->max_tokens,
                                                self->model->context_limit);
    cJSON_AddNumberToObject(root, "max_tokens", (double)max_tokens);

    system = collect_system(hist, system_msg);
    if (system != NULL && system[0] != '\0') {
        if (cache_on) {
            /* system as a one-block array carrying cache_control -- caches the
             * tools + system prefix together (tools render before system). */
            cJSON *sysarr = cJSON_CreateArray();
            cJSON *blk = cJSON_CreateObject();
            cJSON_AddStringToObject(blk, "type", "text");
            cJSON_AddStringToObject(blk, "text", system);
            add_cache_control(blk, ttl_1h);
            cJSON_AddItemToArray(sysarr, blk);
            cJSON_AddItemToObject(root, "system", sysarr);
        } else {
            cJSON_AddStringToObject(root, "system", system);
        }
    }
    free(system);

    msgs = build_messages(hist);
    cJSON_AddItemToObject(root, "messages", msgs);

    /* Incremental history caching: a breakpoint on the conversation tail so each
     * turn reads the prior prefix from cache (M31b). */
    if (cache_on) {
        jc_promptcache_plan(1, cJSON_GetArraySize(msgs), &plan);
        if (plan.msg_index >= 0) {
            cache_control_last_block(cJSON_GetArrayItem(msgs, plan.msg_index),
                                     ttl_1h);
        }
    }

    if (streaming) {
        cJSON_AddBoolToObject(root, "stream", 1);
    }
    if (self->model->temperature >= 0.0) {
        cJSON_AddNumberToObject(root, "temperature", self->model->temperature);
    }
    jtools = map_tools(tools);
    if (jtools != NULL) {
        cJSON_AddItemToObject(root, "tools", jtools);
    }

    {
        jc_status st = jc_prov_print_body(root, body_out);
        cJSON_Delete(root);
        return st;
    }
}

static void an_add_headers(struct jc_provider *self, struct jc_http_headers *h)
{
    char key[1100];
    jc_http_headers_add(h, "Content-Type: application/json");
    jc_http_headers_add(h, "anthropic-version: " ANTHROPIC_VERSION);
    if (self->model->api_key != NULL) {
        jc_snprintf(key, sizeof(key), "x-api-key: %s", self->model->api_key);
        jc_http_headers_add(h, key);
    }
}

static const char *an_endpoint(struct jc_provider *self)
{
    struct prov_state *s = (struct prov_state *)self->state;
    return s->endpoint;
}

static void an_stream_reset(struct jc_provider *self)
{
    jc_prov_state_reset((struct prov_state *)self->state);
}

/* Determine the event type, preferring the SSE event: line, falling back to
 * the JSON "type" field. */
static const char *event_type(const struct jc_sse_event *ev, cJSON *root)
{
    if (ev->event != NULL && ev->event[0] != '\0') {
        return ev->event;
    }
    return jc_json_get_str(root, "type", "");
}

static void an_on_event(struct jc_provider *self,
                        const struct jc_sse_event *ev,
                        struct jc_message *out,
                        struct jc_stream_sink *sink,
                        int *done)
{
    struct prov_state *s = (struct prov_state *)self->state;
    cJSON *root;
    const char *type;

    if (ev->data == NULL || ev->data[0] == '\0') {
        return;
    }
    root = cJSON_Parse(ev->data);
    if (root == NULL) {
        return;
    }
    type = event_type(ev, root);

    if (strcmp(type, "content_block_start") == 0) {
        int idx = (int)jc_json_get_num(root, "index", 0.0);
        cJSON *cb = cJSON_GetObjectItem(root, "content_block");
        const char *btype = jc_json_get_str(cb, "type", "");
        if (strcmp(btype, "tool_use") == 0) {
            struct prov_call *slot = jc_prov_call_slot(s, idx);
            if (slot != NULL) {
                const char *id = jc_json_get_str(cb, "id", "");
                const char *name = jc_json_get_str(cb, "name", "");
                slot->used = 1;
                jc_snprintf(slot->id, sizeof(slot->id), "%s", id);
                if (slot->name.len == 0) {
                    jc_sb_append(&slot->name, name);
                }
            }
        }
    } else if (strcmp(type, "content_block_delta") == 0) {
        int idx = (int)jc_json_get_num(root, "index", 0.0);
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        const char *dtype = jc_json_get_str(delta, "type", "");
        if (strcmp(dtype, "text_delta") == 0) {
            const char *text = jc_json_get_str(delta, "text", NULL);
            if (text != NULL) {
                jc_prov_emit_text(s, sink, text, (jc_size)strlen(text));
            }
        } else if (strcmp(dtype, "input_json_delta") == 0) {
            const char *pj = jc_json_get_str(delta, "partial_json", NULL);
            struct prov_call *slot = jc_prov_call_slot(s, idx);
            if (pj != NULL && slot != NULL) {
                slot->used = 1;
                jc_sb_append(&slot->args, pj);
            }
        }
    } else if (strcmp(type, "message_start") == 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        cJSON *usage = (msg != NULL) ? cJSON_GetObjectItem(msg, "usage") : NULL;
        if (usage != NULL) {
            s->usage_in = jc_json_get_num(usage, "input_tokens", s->usage_in);
            s->usage_out = jc_json_get_num(usage, "output_tokens", s->usage_out);
            /* Prompt-cache accounting (M31a): cache_control GA, no beta header. */
            s->cache_read_in = jc_json_get_num(usage, "cache_read_input_tokens",
                                               s->cache_read_in);
            s->cache_write_in = jc_json_get_num(usage,
                                                "cache_creation_input_tokens",
                                                s->cache_write_in);
        }
    } else if (strcmp(type, "message_delta") == 0) {
        cJSON *usage = cJSON_GetObjectItem(root, "usage");
        cJSON *dl = cJSON_GetObjectItem(root, "delta");
        const char *stop = jc_json_get_str(dl, "stop_reason", NULL);
        if (usage != NULL) {
            s->usage_out = jc_json_get_num(usage, "output_tokens", s->usage_out);
        }
        /* M334: the Anthropic spelling of the same event. */
        if (stop != NULL && strcmp(stop, "max_tokens") == 0) {
            s->hit_length_cap = 1;
        }
    } else if (strcmp(type, "message_stop") == 0) {
        jc_prov_flush(s, out);
        *done = 1;
    } else if (strcmp(type, "error") == 0) {
        cJSON *err = cJSON_GetObjectItem(root, "error");
        const char *msg = jc_json_get_str(err, "message", "provider error");
        jc_prov_emit_text(s, sink, msg, (jc_size)strlen(msg));
        jc_prov_flush(s, out);
        *done = 1;
    }
    /* content_block_stop, ping: no action. */

    cJSON_Delete(root);
}

static jc_status an_parse_full(struct jc_provider *self, const char *body,
                               struct jc_message *out)
{
    struct prov_state *s = (struct prov_state *)self->state;
    cJSON *root = cJSON_Parse(body);
    cJSON *content;
    cJSON *block;
    int idx = 0;
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    /* Surface API errors. */
    if (strcmp(jc_json_get_str(root, "type", ""), "error") == 0) {
        cJSON_Delete(root);
        return JC_ERR_PROVIDER;
    }
    jc_prov_state_reset(s);
    content = cJSON_GetObjectItem(root, "content");
    if (cJSON_IsArray(content)) {
        cJSON_ArrayForEach(block, content) {
            const char *btype = jc_json_get_str(block, "type", "");
            if (strcmp(btype, "text") == 0) {
                const char *text = jc_json_get_str(block, "text", NULL);
                if (text != NULL) {
                    jc_prov_emit_text(s, NULL, text, (jc_size)strlen(text));
                }
            } else if (strcmp(btype, "tool_use") == 0) {
                struct prov_call *slot = jc_prov_call_slot(s, idx);
                if (slot != NULL) {
                    cJSON *input = cJSON_GetObjectItem(block, "input");
                    char *args = (input != NULL)
                                 ? cJSON_PrintUnformatted(input) : NULL;
                    slot->used = 1;
                    jc_snprintf(slot->id, sizeof(slot->id), "%s",
                                jc_json_get_str(block, "id", ""));
                    jc_sb_append(&slot->name, jc_json_get_str(block, "name", ""));
                    if (args != NULL) {
                        jc_sb_append(&slot->args, args);
                        free(args);
                    }
                }
                idx++;
            }
        }
    }
    jc_prov_flush(s, out);
    cJSON_Delete(root);
    return JC_OK;
}

static void an_free(struct jc_provider *self)
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

/* M521: as the OpenAI provider -- flush what the stream produced when it ended
 * without a terminal event, so text the user already saw reaches the message.
 * Both dialects funnel through jc_prov_flush, so the fix cannot exist on one
 * path and be forgotten on the other. */
static void an_stream_end(struct jc_provider *self, struct jc_message *out)
{
    struct prov_state *s = (struct prov_state *)self->state;
    jc_prov_flush(s, out);
}

static const struct jc_provider_vtable AN_VTABLE = {
    an_build_request,
    an_add_headers,
    an_endpoint,
    an_stream_reset,
    an_on_event,
    an_stream_end,
    an_parse_full,
    jc_prov_get_usage,
    jc_prov_get_cache_usage,
    an_free
};

struct jc_provider *jc_provider_anthropic_create(const struct jc_model_cfg *model)
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

    base = (model->api_base != NULL) ? model->api_base
                                     : "https://api.anthropic.com";
    if (strstr(base, "/v1") != NULL) {
        jc_snprintf(s->endpoint, sizeof(s->endpoint), "%s/messages", base);
    } else {
        jc_snprintf(s->endpoint, sizeof(s->endpoint), "%s/v1/messages", base);
    }

    p->vt = &AN_VTABLE;
    p->model = model;
    p->state = s;
    return p;
}
