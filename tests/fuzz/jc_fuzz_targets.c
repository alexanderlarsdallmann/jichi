/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fuzz_targets.c - Tier 1 parser fuzz targets (M123). */

#include "jc_fuzz.h"

#include "jc_json.h"
#include "jc_sse.h"
#include "jc_testparse.h"
#include "jc_docs.h"
#include "jc_rss.h"
#include "jc_base64.h"
#include "jc_constraint.h"
#include "jc_lsp.h"
#include "jc_envelope.h"
#include "jc_cli.h"
#include "jc_yaml.h"
#include "jc_mcp.h"
#include "jc_configedit.h"
#include "jc_setup.h"
#include "jc_provider.h"
#include "jc_config.h"
#include "jc_message.h"
#include "jc_str.h"
#include "jc_mem.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *jc_fuzz_dup0(const unsigned char *data, size_t len)
{
    char *s = (char *)malloc(len + 1);
    if (s == NULL) return NULL;
    if (len > 0) memcpy(s, data, len);
    s[len] = '\0';
    return s;
}

/* ---- targets ------------------------------------------------------------- */

static void t_json(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    cJSON *j;
    if (s == NULL) return;
    j = jc_json_parse(s);
    if (j != NULL) cJSON_Delete(j);
    free(s);
}

static void sse_noop(const struct jc_sse_event *ev, void *user)
{
    (void)ev; (void)user;
}

static void t_sse(const unsigned char *data, size_t len)
{
    struct jc_sse_parser p;
    jc_sse_init(&p, sse_noop, NULL);
    jc_sse_feed(&p, (const char *)data, (jc_size)len);
    jc_sse_free(&p);
}

static void t_testparse(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    struct jc_test_report r;
    if (s == NULL) return;
    jc_test_report_init(&r);
    jc_testparse(s, &r);
    jc_test_report_free(&r);
    free(s);
}

static void t_html(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    struct jc_sb sb;
    if (s == NULL) return;
    jc_sb_init(&sb);
    jc_docs_html_to_text(s, &sb);
    jc_sb_free(&sb);
    free(s);
}

static void t_rss(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    struct jc_sb sb;
    if (s == NULL) return;
    (void)jc_rss_looks_like_feed(s);
    jc_sb_init(&sb);
    jc_rss_to_text(s, &sb);
    jc_sb_free(&sb);
    free(s);
}

static void t_base64(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    jc_size cap;
    unsigned char *out;
    jc_size out_len = 0;
    if (s == NULL) return;
    cap = jc_base64_decoded_len((jc_size)len) + 1;
    out = (unsigned char *)malloc(cap);
    if (out != NULL) {
        (void)jc_base64_decode(s, out, cap, &out_len);
        free(out);
    }
    free(s);
}

static void t_constraint(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    struct jc_arena *a;
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    if (s == NULL) return;
    a = jc_arena_new(0);
    if (a != NULL) {
        (void)jc_constraint_parse(s, cs, JC_CONSTRAINT_MAX, a);
        (void)jc_constraint_scan(s, cs, JC_CONSTRAINT_MAX, a);
        jc_arena_free(a);
    }
    free(s);
}

static void t_lsp_framer(const unsigned char *data, size_t len)
{
    struct jc_lsp_framer f;
    char *body;
    int guard = 0;
    jc_lsp_framer_init(&f);
    jc_lsp_framer_push(&f, (const char *)data, (jc_size)len);
    while (jc_lsp_framer_pop(&f, &body) == 1 && guard < 4096) {
        if (body != NULL) free(body);
        guard++;
    }
    jc_lsp_framer_free(&f);
}

static void t_glob(const unsigned char *data, size_t len)
{
    /* Split the input at the first NUL/newline into pattern + path. */
    char *s = jc_fuzz_dup0(data, len);
    char *sep;
    if (s == NULL) return;
    sep = strchr(s, '\n');
    if (sep != NULL) {
        *sep = '\0';
        (void)jc_glob_match(s, sep + 1);
    } else {
        (void)jc_glob_match(s, s);
    }
    free(s);
}

static void t_output_format(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    int j = 0;
    if (s == NULL) return;
    (void)jc_output_format_parse(s, &j);
    (void)jc_text_is_context_overflow(s);
    free(s);
}

static void t_yaml(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    struct jc_arena *a;
    if (s == NULL) return;
    a = jc_arena_new(0);
    if (a != NULL) {
        /* Nodes/scalars are arena-owned, but each node's child vectors own heap
         * backing -- jc_yaml_free releases those (see jc_yaml.h). */
        struct jc_yaml *root = jc_yaml_parse(s, a);
        jc_yaml_free(root);
        jc_arena_free(a);
    }
    free(s);
}

static void t_mcp(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    char *text = NULL;
    if (s == NULL) return;
    if (jc_mcp_parse_resource_read(s, &text) == JC_OK && text != NULL) {
        free(text);
    }
    free(s);
}

/* ---- Tier 2: property / round-trip targets (abort on a violated invariant) - */

static void t_base64_roundtrip(const unsigned char *data, size_t len)
{
    jc_size enc_cap = jc_base64_encoded_len((jc_size)len) + 1;
    char *enc = (char *)malloc(enc_cap);
    if (enc == NULL) return;
    if (jc_base64_encode(data, (jc_size)len, enc, enc_cap) == JC_OK) {
        jc_size dcap = jc_base64_decoded_len((jc_size)strlen(enc)) + 1;
        unsigned char *dec = (unsigned char *)malloc(dcap);
        jc_size dlen = 0;
        if (dec != NULL) {
            if (jc_base64_decode(enc, dec, dcap, &dlen) == JC_OK) {
                if (dlen != (jc_size)len ||
                    (len > 0 && memcmp(dec, data, len) != 0)) {
                    fprintf(stderr, "FUZZ: base64 encode->decode != identity\n");
                    abort();
                }
            }
            free(dec);
        }
    }
    free(enc);
}

static void t_configedit_prop(const unsigned char *data, size_t len)
{
    char *k = jc_fuzz_dup0(data, len);
    cJSON *root, *c;
    int count = 0;
    if (k == NULL) return;
    if (k[0] == '\0') { free(k); return; }
    root = cJSON_CreateObject();
    jc_configedit_set_bool(root, k, 1);
    jc_configedit_set_str(root, k, "v"); /* must REPLACE, not duplicate */
    for (c = root->child; c != NULL; c = c->next) {
        if (c->string != NULL && strcmp(c->string, k) == 0) count++;
    }
    if (count != 1) {
        fprintf(stderr, "FUZZ: configedit set duplicated key\n");
        abort();
    }
    cJSON_Delete(root);
    free(k);
}

static void t_setup_config(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    struct jc_setup_answers a;
    struct jc_sb sb;
    if (s == NULL) return;
    memset(&a, 0, sizeof a);
    a.provider = "openai";
    a.model = s;          /* arbitrary model id from the fuzz input */
    a.api_key_env = "K";
    jc_sb_init(&sb);
    if (jc_setup_build_config(&a, &sb) == JC_OK && sb.data != NULL) {
        cJSON *j = jc_json_parse(sb.data);
        if (j == NULL) {
            fprintf(stderr, "FUZZ: setup_build_config produced invalid JSON\n");
            abort();
        }
        cJSON_Delete(j);
        if (strstr(sb.data, "\"apiKey\"") != NULL) {
            fprintf(stderr, "FUZZ: setup_build_config emitted a literal apiKey\n");
            abort();
        }
    }
    jc_sb_free(&sb);
    free(s);
}

static void t_constraint_consistency(const unsigned char *data, size_t len)
{
    char *s = jc_fuzz_dup0(data, len);
    struct jc_arena *a;
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    char reason[128];
    int n, i;
    if (s == NULL) return;
    a = jc_arena_new(0);
    if (a != NULL) {
        n = jc_constraint_scan(s, cs, JC_CONSTRAINT_MAX, a);
        for (i = 0; i < n; i++) {
            /* a scanned deny-tool MUST block that exact tool (scan/enforce agree) */
            if (cs[i].kind == JC_CONSTRAINT_DENY_TOOL && cs[i].subject != NULL) {
                if (!jc_constraint_blocks(cs, n, cs[i].subject, NULL, 0,
                                          reason, sizeof reason)) {
                    fprintf(stderr, "FUZZ: constraint scan/enforce inconsistent\n");
                    abort();
                }
            }
        }
        jc_arena_free(a);
    }
    free(s);
}

/* ---- Tier 3: provider-event fuzzing (adversarial model output) ----------- */

/* Free the tool-call/content that a provider accumulated into `out`. */
static void free_msg(struct jc_message *out)
{
    jc_size i;
    free(out->content);
    for (i = 0; i < out->tool_calls.len; i++) {
        struct jc_tool_call *tc =
            (struct jc_tool_call *)jc_vec_at(&out->tool_calls, i);
        free(tc->id);
        free(tc->name);
        free(tc->arguments_json);
    }
    jc_vec_free(&out->tool_calls);
}

/* Drive a provider's on_event with fuzzed frame `data`. `anthropic` cycles the
 * event names its handler keys on; OpenAI ignores the event name. */
static void run_provider(char *pname, const unsigned char *data, size_t len,
                         int anthropic)
{
    char model[] = "m";
    struct jc_model_cfg m;
    struct jc_provider *p;
    char *s = jc_fuzz_dup0(data, len);
    if (s == NULL) return;
    memset(&m, 0, sizeof m);
    m.provider = pname;
    m.model = model;
    m.api_key = (char *)"k";
    m.temperature = -1.0;
    p = jc_provider_create(&m);
    if (p != NULL) {
        struct jc_message out;
        struct jc_sse_event ev;
        struct jc_stream_sink sink;
        int done = 0;
        memset(&out, 0, sizeof out);
        out.role = JC_ROLE_ASSISTANT;
        jc_vec_init(&out.tool_calls, sizeof(struct jc_tool_call));
        p->vt->stream_reset(p);
        sink.on_text = NULL;
        sink.user = NULL;
        ev.data = s;
        if (anthropic) {
            static const char *EVS[] = {
                "message_start", "content_block_start", "content_block_delta",
                "content_block_stop", "message_delta", "message_stop", "ping"
            };
            int i;
            for (i = 0; i < 7 && !done; i++) {
                ev.event = EVS[i];
                p->vt->on_event(p, &ev, &out, &sink, &done);
            }
        } else {
            ev.event = "";
            p->vt->on_event(p, &ev, &out, &sink, &done);
        }
        free_msg(&out);
        p->vt->free(p);
    }
    free(s);
}

static void t_provider_openai(const unsigned char *data, size_t len)
{
    char prov[] = "openai";
    run_provider(prov, data, len, 0);
}

static void t_provider_anthropic(const unsigned char *data, size_t len)
{
    char prov[] = "anthropic";
    run_provider(prov, data, len, 1);
}

/* ---- registry ------------------------------------------------------------ */

const struct jc_fuzz_target JC_FUZZ_TARGETS[] = {
    { "json",       t_json,       "{\"a\":[1,2,{\"b\":true}],\"c\":null}" },
    { "sse",        t_sse,        "event: message\ndata: {\"t\":1}\n\n" },
    { "testparse",  t_testparse,  "FAIL foo.c:12: expected 1 got 2\n3 passed" },
    { "html",       t_html,       "<p>hi <b>there</b></p><script>x</script>" },
    { "rss",        t_rss,        "<rss><channel><item><title>x</title></item></channel></rss>" },
    { "base64",     t_base64,     "aGVsbG8gd29ybGQ=" },
    { "constraint", t_constraint, "do not run the build; deny-tool run_tests" },
    { "lsp_framer", t_lsp_framer, "Content-Length: 2\r\n\r\n{}" },
    { "glob",       t_glob,       "src/**/*.c\nsrc/util/x.c" },
    { "output_fmt", t_output_format, "jsonl" },
    { "yaml",       t_yaml,       "name: x\ntools:\n  - a\n  - b\n" },
    { "mcp",        t_mcp,        "{\"result\":{\"contents\":[{\"text\":\"hi\"}]}}" },
    /* Tier 2: property / round-trip (abort on a violated invariant). */
    { "prop_base64",     t_base64_roundtrip,     "the quick brown fox" },
    { "prop_configedit", t_configedit_prop,      "snapshots" },
    { "prop_setup",      t_setup_config,         "gpt-4o-mini" },
    { "prop_constraint", t_constraint_consistency,
      "do not run the tests, do not commit" },
    { "prop_pathfence",  jc_fuzz_pathfence,      "esc/../sub/./f" },
    /* Tier 3: adversarial model output through each provider's event parser. */
    { "prov_openai",    t_provider_openai,
      "{\"choices\":[{\"delta\":{\"content\":\"hi\",\"tool_calls\":[{\"index\":0,"
      "\"id\":\"c1\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{}\"}}]}}]}" },
    { "prov_anthropic", t_provider_anthropic,
      "{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\","
      "\"text\":\"hi\"}}" }
};

const int JC_FUZZ_N_TARGETS =
    (int)(sizeof(JC_FUZZ_TARGETS) / sizeof(JC_FUZZ_TARGETS[0]));
