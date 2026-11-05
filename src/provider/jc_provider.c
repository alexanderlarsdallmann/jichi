/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_provider.c - provider factory and shared streaming scratch. */

#include "prov_internal.h"
#include "jc_log.h"
#include "jc_utf8.h"
#include <stdlib.h>
#include <string.h>

/* A-07 / M1090: the two per-stream accumulation caps. The SSE field cap
 * (JC_SSE_FIELD_MAX) bounds ONE event's data, not the sum across a stream.
 * Kept as plain file-static globals (not in the header) so the header carries
 * no definitions; initialised from the #defines and lowered by the test. */
static size_t g_jc_text_cap = JC_PROV_MAX_TEXT;
static size_t g_jc_args_cap = JC_PROV_MAX_ARGS;

size_t jc_prov_get_max_text(void)  { return g_jc_text_cap; }
size_t jc_prov_get_max_args(void)  { return g_jc_args_cap; }
void   jc_prov_set_max_text(size_t n) { g_jc_text_cap = n; }
void   jc_prov_set_max_args(size_t n) { g_jc_args_cap = n; }
void   jc_prov_reset_max_text(void) { g_jc_text_cap = JC_PROV_MAX_TEXT; }
void   jc_prov_reset_max_args(void) { g_jc_args_cap = JC_PROV_MAX_ARGS; }

struct jc_provider *jc_provider_create(const struct jc_model_cfg *model)
{
    const char *p = (model != NULL) ? model->provider : NULL;
    if (p != NULL && strcmp(p, "openai") == 0) {
        return jc_provider_openai_create(model);
    }
    if (p != NULL && strcmp(p, "anthropic") == 0) {
        return jc_provider_anthropic_create(model);
    }
    /* NO HOUSE DIALECT (M718, plan D4). Until M718 an entry naming no provider,
     * or one jichi does not speak, had its dialect GUESSED from the model id --
     * "gpt" or "openai" in it meant OpenAI -- and otherwise got Anthropic's, so
     * `"provider": "ollama"` sent the Anthropic Messages API to an Ollama server,
     * with ANTHROPIC_API_KEY attached. A heuristic that picks a wire dialect is a
     * house default under another name (M709's argument), so this refuses, and
     * every caller already handles NULL. jc_config_provider_problem() is the
     * sentence a caller prints. */
    return NULL;
}

/* ----- shared scratch ------------------------------------------------- */

void jc_prov_state_init(struct prov_state *s)
{
    int i;
    s->endpoint[0] = '\0';
    s->cache_key[0] = '\0';
    jc_sb_init(&s->text);
    s->text_started = 0;
    s->saw_reasoning = 0;
    s->hit_length_cap = 0;
    s->truncated_reply = 0;
    s->done = 0;
    s->usage_in = 0.0;
    s->usage_out = 0.0;
    s->cache_read_in = 0.0;
    s->cache_write_in = 0.0;
    s->stream_err[0] = '\0';
    for (i = 0; i < JC_PROV_MAX_CALLS; i++) {
        s->calls[i].used = 0;
        s->calls[i].flushed = 0;
        s->calls[i].id[0] = '\0';
        jc_sb_init(&s->calls[i].name);
        jc_sb_init(&s->calls[i].args);
    }
}

void jc_prov_state_free(struct prov_state *s)
{
    int i;
    jc_sb_free(&s->text);
    for (i = 0; i < JC_PROV_MAX_CALLS; i++) {
        jc_sb_free(&s->calls[i].name);
        jc_sb_free(&s->calls[i].args);
    }
}

void jc_prov_state_reset(struct prov_state *s)
{
    int i;
    /* M218: shrink-on-reset (see JC_PROV_SB_KEEP_* in prov_internal.h) --
     * this state outlives every stream, so a one-off huge message must not
     * pin its capacity until provider destroy. Names stay a plain clear
     * (tool names are tiny; the buffer never outgrows a block). */
    jc_sb_clear_shrink(&s->text, (jc_size)JC_PROV_SB_KEEP_TEXT);
    s->text_started = 0;
    s->saw_reasoning = 0;
    s->hit_length_cap = 0;
    s->truncated_reply = 0;
    s->done = 0;
    s->usage_in = 0.0;
    s->usage_out = 0.0;
    s->cache_read_in = 0.0;
    s->cache_write_in = 0.0;
    s->stream_err[0] = '\0';
    for (i = 0; i < JC_PROV_MAX_CALLS; i++) {
        s->calls[i].used = 0;
        s->calls[i].flushed = 0;
        s->calls[i].id[0] = '\0';
        jc_sb_clear(&s->calls[i].name);
        jc_sb_clear_shrink(&s->calls[i].args, (jc_size)JC_PROV_SB_KEEP_ARGS);
    }
}

struct prov_call *jc_prov_call_slot(struct prov_state *s, int idx)
{
    if (idx < 0 || idx >= JC_PROV_MAX_CALLS) {
        return NULL;
    }
    return &s->calls[idx];
}

void jc_prov_emit_text(struct prov_state *s, struct jc_stream_sink *sink,
                       const char *delta, jc_size n)
{
    jc_size room;
    jc_size take;
    if (n == 0) {
        return;
    }
    /* A-07 / M1090: the cap bounds the RUNNING TOTAL across the whole stream,
     * not one delta. Append (and emit) only up to it; once it is reached the
     * bytes past the cap are dropped so s->text and the sink stay byte-for-byte
     * the same as the message that gets flushed, and truncated_reply marks the
     * cut. (Distinct from hit_length_cap, which is the server's
     * finish_reason == "length" verdict.) */
    room = (s->text.len < (jc_size)g_jc_text_cap)
           ? ((jc_size)g_jc_text_cap - s->text.len)
           : (jc_size)0;
    take = (n < room) ? n : room;
    if (take < n) {
        s->truncated_reply = 1;
    }
    if (take > 0) {
        jc_sb_append_n(&s->text, delta, take);
    }
    s->text_started = 1;
    if (sink != NULL && sink->on_text != NULL && take > 0) {
        sink->on_text(sink->user, delta, take);
    }
}

/* A-07 / M1090: the shared tool-call argument sink. All four dialect sites
 * (openai stream + full, anthropic stream + full) append NUL-terminated
 * argument fragments here so the per-call cap is enforced once, in one place,
 * and the truncated_reply flag is set on the same prov_state the text cap
 * uses. slot->name is deliberately NOT capped -- tool names are tiny; the
 * finding is args + text. */
void jc_prov_append_args(struct prov_state *s, struct prov_call *slot,
                         const char *args)
{
    jc_size n;
    jc_size room;
    jc_size take;
    if (s == NULL || slot == NULL || args == NULL) {
        return;
    }
    n = (jc_size)strlen(args);
    if (n == 0) {
        return;
    }
    room = (slot->args.len < (jc_size)g_jc_args_cap)
           ? ((jc_size)g_jc_args_cap - slot->args.len)
           : (jc_size)0;
    take = (n < room) ? n : room;
    if (take < n) {
        s->truncated_reply = 1;
    }
    if (take > 0) {
        jc_sb_append_n(&slot->args, args, take);
    }
}

void jc_prov_get_usage(struct jc_provider *self, double *in_tok,
                       double *out_tok)
{
    struct prov_state *s = (struct prov_state *)self->state;
    if (in_tok != NULL) {
        *in_tok = s->usage_in;
    }
    if (out_tok != NULL) {
        *out_tok = s->usage_out;
    }
}

int jc_prov_stream_started(struct jc_provider *self)
{
    const struct prov_state *s = (const struct prov_state *)self->state;
    int i;

    if (s->text_started || s->saw_reasoning) {
        return 1;
    }
    for (i = 0; i < JC_PROV_MAX_CALLS; i++) {
        if (s->calls[i].used) {
            return 1;
        }
    }
    return 0;
}

const char *jc_prov_stream_error(struct jc_provider *self)
{
    const struct prov_state *s = (const struct prov_state *)self->state;
    return s->stream_err[0] != '\0' ? s->stream_err : NULL;
}

void jc_prov_set_stream_error(struct prov_state *s, const char *msg)
{
    jc_size n;
    if (msg == NULL || msg[0] == '\0') {
        msg = "provider error";
    }
    n = (jc_size)strlen(msg);
    if (n > (jc_size)(sizeof s->stream_err - 1)) {
        n = jc_utf8_trunc_len(msg, (jc_size)(sizeof s->stream_err - 1));
    }
    memcpy(s->stream_err, msg, n);
    s->stream_err[n] = '\0';
}

void jc_prov_get_cache_usage(struct jc_provider *self, double *read_in,
                             double *write_in)
{
    struct prov_state *s = (struct prov_state *)self->state;
    if (read_in != NULL) {
        *read_in = s->cache_read_in;
    }
    if (write_in != NULL) {
        *write_in = s->cache_write_in;
    }
}

int jc_prov_msg_is_placeholder(const struct jc_message *m)
{
    if (m == NULL || m->role != JC_ROLE_ASSISTANT) {
        return 0;
    }
    if (jc_msg_tool_call_count((struct jc_message *)m) > 0) {
        return 0;
    }
    return (m->content == NULL || m->content[0] == '\0');
}

jc_status jc_prov_print_body(cJSON *root, char **body_out)
{
    char *body;
    char *fixed = NULL;

    if (body_out == NULL) {
        return JC_ERR_INVALID;
    }
    *body_out = NULL;
    body = cJSON_PrintUnformatted(root);
    if (body == NULL) {
        return JC_ERR_OOM;
    }
    if (jc_utf8_sanitize(body, (jc_size)strlen(body), &fixed, NULL)) {
        /* Rare once the producers truncate on boundaries, so say so out loud:
         * a silent repair here would hide a real defect upstream. */
        jc_logf(JC_LOG_WARN,
                "[provider] request body contained ill-formed UTF-8; replaced "
                "with U+FFFD (a tool result or prompt section was likely cut "
                "mid-character)");
        free(body);
        body = fixed;
    }
    *body_out = body;
    return JC_OK;
}

char *jc_prov_args_wire(const char *args_json)
{
    cJSON *parsed;
    cJSON *wrap;
    char *out;

    if (args_json == NULL || args_json[0] == '\0') {
        return jc_strdup("{}");
    }
    parsed = cJSON_Parse(args_json);
    if (parsed != NULL) {
        int ok = cJSON_IsObject(parsed);
        cJSON_Delete(parsed);
        if (ok) {
            return jc_strdup(args_json);
        }
        /* Valid JSON but not an object (a bare string, array or number): the
         * server expects an object, so wrap it like the unparseable case. */
    }
    wrap = cJSON_CreateObject();
    if (wrap == NULL) {
        return jc_strdup("{}");
    }
    cJSON_AddStringToObject(wrap, "_unparsed_arguments", args_json);
    out = cJSON_PrintUnformatted(wrap);
    cJSON_Delete(wrap);
    return (out != NULL) ? out : jc_strdup("{}");
}

void jc_prov_flush(struct prov_state *s, struct jc_message *out)
{
    int i;
    /* M334: carry the truncation forward. Both providers funnel through here,
     * so the flag cannot be set on one path and forgotten on the other. */
    if (s->hit_length_cap) {
        out->truncated = 1;
    }
    /* A-07 / M1090: the same single funnel carries the ACCUMULATION verdict.
     * The server said nothing about a length cap; jichi clipped its own
     * scratch buffers to the per-stream ceiling. out->truncated means "the
     * message the agent sees is incomplete", true for either cause. The two
     * causes are carried apart (the model-facing text names which one):
     * truncated_by_cap is set ONLY when the cap fired and the server did NOT
     * also report its own length cap -- when BOTH fired, the model's
     * output-token limit is the cause the model can act on, and the prose
     * says that (hit_length_cap wins for the prose). */
    if (s->truncated_reply) {
        out->truncated = 1;
        if (!s->hit_length_cap) {
            out->truncated_by_cap = 1;
        }
    }
    /* M937 (D3): carry the reasoning-cause forward the same way -- the single
     * funnel, so the OpenAI and Anthropic dialects set it identically. It is
     * the assistant message that the agent loop will later inspect to nudge a
     * reasoning-only delegate child (the cause the loop cannot otherwise see). */
    if (s->saw_reasoning) {
        out->saw_reasoning = 1;
    }
    if (s->text_started) {
        jc_msg_set_content(out, s->text.data != NULL ? s->text.data : "");
    }
    for (i = 0; i < JC_PROV_MAX_CALLS; i++) {
        struct prov_call *c = &s->calls[i];
        if (c->used && !c->flushed) {
            const char *args = (c->args.data != NULL && c->args.len > 0)
                               ? c->args.data : "{}";
            jc_msg_add_tool_call(out,
                                 c->id[0] ? c->id : NULL,
                                 c->name.data != NULL ? c->name.data : "",
                                 args);
            c->flushed = 1;
        }
    }
}
