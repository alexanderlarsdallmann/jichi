/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_golden_request.c - byte-exact wire-shape guard for both providers.
 *
 * M166 shipped a defect that 7000+ offline checks could not see: every request
 * ended with a content-free `{"role":"assistant","content":""}` turn, because
 * nothing asserted on the *shape* of a built request -- only on individual
 * fields somebody had thought to check. A frontier model ignored it; a small
 * local model stopped calling tools entirely.
 *
 * This test closes that class. It builds one request per provider from a fixed
 * history that exercises the interesting shapes -- a system prompt, a user turn,
 * an assistant turn whose only content is a tool call, a tool result, and the
 * in-flight assistant placeholder the agent loop appends and which must never
 * reach the wire -- and compares the result byte for byte against a golden
 * literal.
 *
 * Maintaining it: when the request format changes *deliberately*, the test
 * prints the actual body and the offset of the first difference. Read that diff,
 * satisfy yourself the change is intended, then update the golden below. The
 * friction is the feature -- a wire-format change should require a human to look
 * at it.
 *
 * The goldens are NULL-terminated chunk arrays rather than one long literal:
 * C89 guarantees only 509 characters in a string literal *after* concatenation
 * (5.2.4.1), so adjacent-literal splicing does not help. Same idiom as the
 * jc_scaffold packs. Chunks are split at JSON boundaries so a diff reads well.
 *
 * No network: build_request is pure with respect to I/O.
 */

#include "jc_test.h"
#include "jc_provider.h"
#include "jc_config.h"
#include "jc_message.h"
#include <stdlib.h>

static const char *const GOLDEN_OPENAI[] = {
    "{\"model\":\"m-test\",\"messages\":[",
    "{\"role\":\"system\",\"content\":\"SYS\"},",
    "{\"role\":\"user\",\"content\":\"read a.c\"},",
    "{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[",
    "{\"id\":\"call_1\",\"type\":\"function\",\"function\":{",
    "\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"a.c\\\"}\"}}]},",
    "{\"role\":\"tool\",\"tool_call_id\":\"call_1\",",
    "\"content\":\"int main(void);\"}],",
    "\"stream\":true,\"stream_options\":{\"include_usage\":true},",
    "\"max_tokens\":256,\"temperature\":0.2,\"tools\":[",
    "{\"type\":\"function\",\"function\":{\"name\":\"read_file\",",
    "\"description\":\"Read a file.\",\"parameters\":{\"type\":\"object\",",
    "\"properties\":{\"path\":{\"type\":\"string\"}},",
    "\"required\":[\"path\"]}}}],\"tool_choice\":\"auto\"}",
    NULL
};

static const char *const GOLDEN_ANTHROPIC[] = {
    "{\"model\":\"m-test\",\"max_tokens\":256,\"system\":\"SYS\",",
    "\"messages\":[",
    "{\"role\":\"user\",\"content\":\"read a.c\"},",
    "{\"role\":\"assistant\",\"content\":[",
    "{\"type\":\"tool_use\",\"id\":\"call_1\",\"name\":\"read_file\",",
    "\"input\":{\"path\":\"a.c\"}}]},",
    "{\"role\":\"user\",\"content\":[",
    "{\"type\":\"tool_result\",\"tool_use_id\":\"call_1\",",
    "\"content\":\"int main(void);\"}]}],",
    "\"stream\":true,\"temperature\":0.2,\"tools\":[",
    "{\"name\":\"read_file\",\"description\":\"Read a file.\",",
    "\"input_schema\":{\"type\":\"object\",",
    "\"properties\":{\"path\":{\"type\":\"string\"}},",
    "\"required\":[\"path\"]}}]}",
    NULL
};

/* Offset of the first byte where `s` departs from the joined chunks, or -1 when
 * they match exactly (including length). */
static long chunk_diff_at(const char *s, const char *const *chunks)
{
    long off = 0;
    int i;
    for (i = 0; chunks[i] != NULL; i++) {
        size_t n = strlen(chunks[i]);
        size_t k;
        for (k = 0; k < n; k++) {
            if (s[off + (long)k] != chunks[i][k]) {
                return off + (long)k;
            }
        }
        off += (long)n;
    }
    return (s[off] == '\0') ? -1L : off;
}

static void report(const char *label, const char *body,
                   const char *const *chunks, long at)
{
    int i;
    printf("  golden mismatch (%s) at byte %ld\n", label, at);
    printf("  actual:   %s\n", body);
    printf("  expected: ");
    for (i = 0; chunks[i] != NULL; i++) {
        printf("%s", chunks[i]);
    }
    printf("\n  If the change is intended, update the golden in %s\n", __FILE__);
}

/* One tool, spelled out, so the golden pins the neutral->provider tool mapping
 * (OpenAI nests under function{}, Anthropic renames parameters->input_schema). */
static cJSON *one_tool(void)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON *props = cJSON_CreateObject();
    cJSON *path = cJSON_CreateObject();
    cJSON *req = cJSON_CreateArray();
    cJSON_AddStringToObject(t, "name", "read_file");
    cJSON_AddStringToObject(t, "description", "Read a file.");
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddStringToObject(path, "type", "string");
    cJSON_AddItemToObject(props, "path", path);
    cJSON_AddItemToObject(params, "properties", props);
    cJSON_AddItemToArray(req, cJSON_CreateString("path"));
    cJSON_AddItemToObject(params, "required", req);
    cJSON_AddItemToObject(t, "parameters", params);
    cJSON_AddItemToArray(arr, t);
    return arr;
}

static void check_provider(const char *provider, const char *const *golden)
{
    struct jc_model_cfg m;
    struct jc_provider *p;
    struct jc_history h;
    struct jc_message *a;
    cJSON *tools;
    char *body = NULL;
    long at;

    memset(&m, 0, sizeof(m));
    m.provider = (char *)provider;
    m.model = (char *)"m-test";
    m.api_key = (char *)"test-key";
    m.temperature = 0.2;      /* explicit: an unset temperature is omitted     */
    m.max_tokens = 256;       /* explicit: keeps max_tokens off contextLimit   */
    m.context_limit = 4096;
    /* prompt_cache stays 0, so no per-session prompt_cache_key (a UUID) can
     * make the golden non-deterministic. Caching is covered by test_provider. */

    p = jc_provider_create(&m);
    JC_CHECK(p != NULL);
    if (p == NULL) {
        return;
    }

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "read a.c");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(a, "call_1", "read_file", "{\"path\":\"a.c\"}");
    jc_history_add_tool_result(&h, "call_1", "int main(void);", 0);
    /* M166: the in-flight placeholder. Its absence from the golden IS the test. */
    jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);

    tools = one_tool();
    p->vt->build_request(p, &h, "SYS", tools, 1, &body);
    JC_CHECK(body != NULL);
    if (body != NULL) {
        at = chunk_diff_at(body, golden);
        JC_CHECK(at == -1L);
        if (at != -1L) {
            report(provider, body, golden, at);
        }
        /* Stated twice on purpose: if the golden is ever updated carelessly,
         * this still fails. */
        JC_CHECK(strstr(body, "\"content\":\"\"") == NULL);
        free(body);
    }

    cJSON_Delete(tools);
    jc_history_free(&h);
    p->vt->free(p);
}

void test_golden_request(void)
{
    check_provider("openai", GOLDEN_OPENAI);
    check_provider("anthropic", GOLDEN_ANTHROPIC);
}
