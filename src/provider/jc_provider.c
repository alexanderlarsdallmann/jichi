/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_provider.c - provider factory and shared streaming scratch. */

#include "prov_internal.h"
#include "jc_log.h"
#include "jc_utf8.h"
#include <stdlib.h>
#include <string.h>

struct jc_provider *jc_provider_create(const struct jc_model_cfg *model)
{
    const char *p = (model != NULL) ? model->provider : NULL;
    if (p != NULL && strcmp(p, "openai") == 0) {
        return jc_provider_openai_create(model);
    }
    if (p != NULL && strcmp(p, "anthropic") == 0) {
        return jc_provider_anthropic_create(model);
    }
    /* Heuristic fallback by model id, matching the original's detection. */
    if (model != NULL && model->model != NULL &&
        (strstr(model->model, "gpt") != NULL ||
         strstr(model->model, "openai") != NULL)) {
        return jc_provider_openai_create(model);
    }
    return jc_provider_anthropic_create(model);
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
    s->done = 0;
    s->usage_in = 0.0;
    s->usage_out = 0.0;
    s->cache_read_in = 0.0;
    s->cache_write_in = 0.0;
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
    s->done = 0;
    s->usage_in = 0.0;
    s->usage_out = 0.0;
    s->cache_read_in = 0.0;
    s->cache_write_in = 0.0;
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
    if (n == 0) {
        return;
    }
    jc_sb_append_n(&s->text, delta, n);
    s->text_started = 1;
    if (sink != NULL && sink->on_text != NULL) {
        sink->on_text(sink->user, delta, n);
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
