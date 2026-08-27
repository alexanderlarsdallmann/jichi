/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_convert.c - Continue -> jichi config conversion. */

#include "jc_test.h"
#include "jc_convert.h"
#include "../src/convert/convert_internal.h"
#include "jc_yaml.h"
#include "jc_json.h"
#include "jc_md.h"
#include "jc_mem.h"
#include "jc_platform.h"
#include "jc_snprintf.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *YAML_SAMPLE =
    "name: my-assistant\n"
    "version: 0.0.1\n"
    "models:\n"
    "  - name: Editor\n"
    "    provider: openai\n"
    "    model: gpt-4o\n"
    "    roles:\n"
    "      - edit\n"
    "  - name: Chatter\n"
    "    provider: anthropic\n"
    "    model: claude-3-5-sonnet\n"
    "    apiKey: sk-abc123\n"
    "    defaultCompletionOptions:\n"
    "      maxTokens: 2048\n"
    "      temperature: 0.2\n"
    "    roles:\n"
    "      - chat\n"
    "      - edit\n";

static const char *JSON_SAMPLE =
    "{ \"models\": [ { \"title\": \"GPT\", \"provider\": \"openai\", "
    "\"model\": \"gpt-4o\", \"apiBase\": \"https://api.example/v1\", "
    "\"completionOptions\": { \"maxTokens\": 1000 } } ] }";

static const char *YAML_TEMPLATED_KEY =
    "models:\n"
    "  - name: Claude\n"
    "    provider: anthropic\n"
    "    model: claude-3-5-sonnet\n"
    "    apiKey: ${{ secrets.ANTHROPIC }}\n";

static void test_yaml_parser_basics(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_yaml *root = jc_yaml_parse(YAML_SAMPLE, a);
    struct jc_yaml *models;
    struct jc_yaml *m0;
    struct jc_yaml *m1;

    JC_CHECK(root != NULL);
    JC_CHECK_STR(jc_yaml_get_str(root, "name", "?"), "my-assistant");

    models = jc_yaml_get(root, "models");
    JC_CHECK(models != NULL && jc_yaml_seq_len(models) == 2);

    m0 = jc_yaml_seq_at(models, 0);
    JC_CHECK_STR(jc_yaml_get_str(m0, "name", "?"), "Editor");
    JC_CHECK_STR(jc_yaml_get_str(m0, "provider", "?"), "openai");

    m1 = jc_yaml_seq_at(models, 1);
    JC_CHECK_STR(jc_yaml_get_str(m1, "model", "?"), "claude-3-5-sonnet");
    {
        struct jc_yaml *dco = jc_yaml_get(m1, "defaultCompletionOptions");
        JC_CHECK_STR(jc_yaml_get_str(dco, "maxTokens", "?"), "2048");
        {
            struct jc_yaml *roles = jc_yaml_get(m1, "roles");
            JC_CHECK(jc_yaml_seq_len(roles) == 2);
            JC_CHECK_STR(jc_yaml_seq_at(roles, 0)->scalar, "chat");
        }
    }
    jc_yaml_free(root);
    jc_arena_free(a);
}

/* M409: a sequence item whose WHOLE value is one quoted string is a scalar,
 * colons and all. Before this, `- "Run it first: cd ..."` was mis-read as a
 * mapping (find_colon saw the `: ` inside the quotes), its scalar was NULL,
 * and jc_assign's M289 skip silently DELETED the hint -- 64 of the 80 shipped
 * assignment ladders were serving fewer rungs than their authors wrote. */
static void test_yaml_quoted_seq_scalar(void)
{
    struct jc_arena *a = jc_arena_new(0);
    static const char *SRC =
        "hints:\n"
        "  - \"Run it first: read which check failed\"\n"
        "  - plain unquoted rung\n"
        "  - 'single: quoted too'\n";
    struct jc_yaml *root = jc_yaml_parse(SRC, a);
    struct jc_yaml *h;

    JC_CHECK(root != NULL);
    h = jc_yaml_get(root, "hints");
    JC_CHECK(h != NULL && jc_yaml_seq_len(h) == 3);
    JC_CHECK(jc_yaml_seq_at(h, 0) != NULL &&
             jc_yaml_seq_at(h, 0)->scalar != NULL);
    JC_CHECK_STR(jc_yaml_seq_at(h, 0)->scalar,
                 "Run it first: read which check failed");
    JC_CHECK_STR(jc_yaml_seq_at(h, 1)->scalar, "plain unquoted rung");
    JC_CHECK(jc_yaml_seq_at(h, 2) != NULL &&
             jc_yaml_seq_at(h, 2)->scalar != NULL);
    JC_CHECK_STR(jc_yaml_seq_at(h, 2)->scalar, "single: quoted too");

    /* The fix must NOT swallow a real mapping: an unquoted `key: value` item
     * stays a map (that is genuine YAML), and a quoted KEY with a value after
     * the closing quote is not a whole-line scalar either. */
    {
        static const char *MAPSRC =
            "items:\n"
            "  - name: alpha\n"
            "  - \"name\": beta\n";
        struct jc_yaml *r2 = jc_yaml_parse(MAPSRC, a);
        struct jc_yaml *it = jc_yaml_get(r2, "items");
        JC_CHECK(it != NULL && jc_yaml_seq_len(it) == 2);
        JC_CHECK(jc_yaml_seq_at(it, 0)->scalar == NULL);
        JC_CHECK(jc_yaml_seq_at(it, 1)->scalar == NULL);
        jc_yaml_free(r2);
    }
    /* Both documents need this even though the nodes live in the arena: a
     * jc_yaml's child/pair vectors are jc_vec, i.e. realloc'd heap, so freeing
     * the arena reclaims the nodes and leaks the vectors. The two tests either
     * side of this one always did it; this one did not, and gcc-only gating
     * cannot see the difference -- `make ci`'s ASan tier reported 640 bytes in
     * 10 allocations, four milestones after M409 shipped it. */
    jc_yaml_free(root);
    jc_arena_free(a);
}

static void test_convert_yaml(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_convert_result res;
    cJSON *root;
    cJSON *model;

    JC_CHECK(jc_convert_run(YAML_SAMPLE, JC_SRC_CONTINUE_YAML, &res, a) ==
             JC_OK);
    JC_CHECK(res.model_count == 2);
    /* The first chat-capable model (Chatter) must be selected. */
    JC_CHECK_STR(res.model_name, "Chatter");

    root = jc_json_parse(res.json);
    JC_CHECK(root != NULL);
    /* Output is a models[] array with the active (chat) model first. */
    {
        cJSON *models = jc_json_get_obj(root, "models");
        JC_CHECK(cJSON_GetArraySize(models) == 2);
        model = cJSON_GetArrayItem(models, 0);
    }
    JC_CHECK_STR(jc_json_get_str(model, "provider", "?"), "anthropic");
    JC_CHECK_STR(jc_json_get_str(model, "model", "?"), "claude-3-5-sonnet");
    JC_CHECK_STR(jc_json_get_str(model, "apiKey", "?"), "sk-abc123");
    JC_CHECK(jc_json_get_num(model, "maxTokens", -1.0) == 2048.0);
    JC_CHECK_NEAR(jc_json_get_num(model, "temperature", -1.0), 0.2);
    JC_CHECK(jc_json_get_num(root, "maxToolIters", -1.0) == 25.0);
    /* Roles are carried through from the source config. */
    {
        cJSON *roles = jc_json_get_obj(model, "roles");
        JC_CHECK(cJSON_GetArraySize(roles) == 2);
        JC_CHECK_STR(cJSON_GetArrayItem(roles, 0)->valuestring, "chat");
        JC_CHECK_STR(cJSON_GetArrayItem(roles, 1)->valuestring, "edit");
    }

    cJSON_Delete(root);
    free(res.json);
    jc_arena_free(a);
}

static void test_convert_json(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_convert_result res;
    cJSON *root;
    cJSON *model;

    JC_CHECK(jc_convert_is_json("config.json", JSON_SAMPLE) == 1);
    JC_CHECK(jc_convert_is_json(NULL, JSON_SAMPLE) == 1);
    JC_CHECK(jc_convert_is_json(NULL, YAML_SAMPLE) == 0);

    JC_CHECK(jc_convert_run(JSON_SAMPLE, JC_SRC_CONTINUE_JSON, &res, a) ==
             JC_OK);
    JC_CHECK(res.model_count == 1);
    JC_CHECK_STR(res.model_name, "GPT");

    root = jc_json_parse(res.json);
    model = cJSON_GetArrayItem(jc_json_get_obj(root, "models"), 0);
    JC_CHECK_STR(jc_json_get_str(model, "provider", "?"), "openai");
    JC_CHECK_STR(jc_json_get_str(model, "model", "?"), "gpt-4o");
    JC_CHECK_STR(jc_json_get_str(model, "apiBase", "?"),
                 "https://api.example/v1");
    /* No literal key in legacy sample => env reference. */
    JC_CHECK_STR(jc_json_get_str(model, "apiKeyEnv", "?"), "OPENAI_API_KEY");
    JC_CHECK(jc_json_get_num(model, "maxTokens", -1.0) == 1000.0);

    cJSON_Delete(root);
    free(res.json);
    jc_arena_free(a);
}

static void test_convert_templated_key(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_convert_result res;
    cJSON *root;
    cJSON *model;

    JC_CHECK(jc_convert_run(YAML_TEMPLATED_KEY, JC_SRC_CONTINUE_YAML, &res, a)
             == JC_OK);
    root = jc_json_parse(res.json);
    model = cJSON_GetArrayItem(jc_json_get_obj(root, "models"), 0);
    /* A ${{...}} secret template is not a literal key. */
    JC_CHECK(jc_json_get_str(model, "apiKey", NULL) == NULL);
    JC_CHECK_STR(jc_json_get_str(model, "apiKeyEnv", "?"), "ANTHROPIC_API_KEY");

    cJSON_Delete(root);
    free(res.json);
    jc_arena_free(a);
}

static const char *YAML_BLOCK = "prompts:\n"
    "  - name: review\n"
    "    prompt: |\n"
    "      First line.\n"
    "      Second line.\n"
    "    role: system\n"
    "  - name: folded\n"
    "    prompt: >\n"
    "      alpha\n"
    "      beta\n"
    "  - name: strip\n"
    "    prompt: |-\n"
    "      only line\n";

static void test_yaml_block_scalars(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_yaml *root = jc_yaml_parse(YAML_BLOCK, a);
    struct jc_yaml *prompts;
    struct jc_yaml *p0;
    struct jc_yaml *p1;
    struct jc_yaml *p2;

    JC_CHECK(root != NULL);
    prompts = jc_yaml_get(root, "prompts");
    JC_CHECK(jc_yaml_seq_len(prompts) == 3);

    /* Literal block keeps line breaks and gets one trailing newline (clip). */
    p0 = jc_yaml_seq_at(prompts, 0);
    JC_CHECK_STR(jc_yaml_get_str(p0, "prompt", "?"),
                 "First line.\nSecond line.\n");
    /* A sibling key after the block is still parsed. */
    JC_CHECK_STR(jc_yaml_get_str(p0, "role", "?"), "system");

    /* Folded block joins lines with spaces. */
    p1 = jc_yaml_seq_at(prompts, 1);
    JC_CHECK_STR(jc_yaml_get_str(p1, "prompt", "?"), "alpha beta\n");

    /* Strip chomping removes the trailing newline. */
    p2 = jc_yaml_seq_at(prompts, 2);
    JC_CHECK_STR(jc_yaml_get_str(p2, "prompt", "?"), "only line");

    jc_yaml_free(root);
    jc_arena_free(a);
}

/* Split into two literals so neither exceeds the C90 509-char limit. */
static const char *OPENCODE_SAMPLE_A =
    "{\n"
    "  \"$schema\": \"https://opencode.ai/config.json\",\n"
    "  \"model\": \"anthropic/claude-sonnet-4\",\n"
    "  \"small_model\": \"openai/gpt-4o-mini\",\n"
    "  \"provider\": {\n"
    "    \"anthropic\": { \"options\": { \"baseURL\": \"https://ac/v1\",\n"
    "      \"apiKey\": \"{env:MY_ANTHROPIC}\" } }\n"
    "  },\n"
    "  \"mcp\": {\n"
    "    \"fs\": { \"type\": \"local\", \"command\": [\"mcp-fs\", \"--root\","
    " \".\"], \"environment\": { \"TOKEN\": \"x\" } },\n"
    "    \"web\": { \"type\": \"remote\", \"url\": \"https://mcp/x\",\n"
    "      \"headers\": { \"X-Key\": \"abc\" } },\n";
static const char *OPENCODE_SAMPLE_B =
    "    \"off\": { \"type\": \"local\", \"command\": [\"z\"],"
    " \"enabled\": false }\n"
    "  },\n"
    "  \"lsp\": { \"myls\": { \"command\": [\"my-ls\", \"--stdio\"],"
    " \"extensions\": [\".ml\", \"mli\"] } },\n"
    "  \"instructions\": [\"AGENTS.md\", \"docs/x.md\"],\n"
    "  \"permission\": { \"bash\": \"deny\", \"edit\": \"deny\","
    " \"webfetch\": \"allow\" },\n"
    "}\n";

static void test_convert_opencode(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_convert_result res;
    char src[2048];
    cJSON *root;
    cJSON *models;
    cJSON *m0;
    cJSON *mcp;
    cJSON *perm;

    jc_snprintf(src, sizeof(src), "%s%s", OPENCODE_SAMPLE_A, OPENCODE_SAMPLE_B);
    JC_CHECK(jc_convert_run(src, JC_SRC_OPENCODE, &res, a) == JC_OK);
    root = jc_json_parse(res.json);
    JC_CHECK(root != NULL);

    models = jc_json_get_obj(root, "models");
    JC_CHECK(cJSON_GetArraySize(models) == 2);
    m0 = cJSON_GetArrayItem(models, 0); /* active = main model */
    JC_CHECK_STR(jc_json_get_str(m0, "provider", "?"), "anthropic");
    JC_CHECK_STR(jc_json_get_str(m0, "model", "?"), "claude-sonnet-4");
    JC_CHECK_STR(jc_json_get_str(m0, "apiBase", "?"), "https://ac/v1");
    /* {env:MY_ANTHROPIC} resolves to an apiKeyEnv, never a literal key. */
    JC_CHECK(jc_json_get_str(m0, "apiKey", NULL) == NULL);
    JC_CHECK_STR(jc_json_get_str(m0, "apiKeyEnv", "?"), "MY_ANTHROPIC");

    /* Two MCP servers (the disabled one is skipped). */
    mcp = jc_json_get_obj(root, "mcpServers");
    JC_CHECK(cJSON_GetArraySize(mcp) == 2);
    {
        cJSON *fs = cJSON_GetArrayItem(mcp, 0);
        cJSON *args = jc_json_get_obj(fs, "args");
        cJSON *web = cJSON_GetArrayItem(mcp, 1);
        cJSON *hdrs = jc_json_get_obj(web, "headers");
        JC_CHECK_STR(jc_json_get_str(fs, "command", "?"), "mcp-fs");
        JC_CHECK(cJSON_GetArraySize(args) == 2);
        JC_CHECK_STR(cJSON_GetArrayItem(args, 0)->valuestring, "--root");
        JC_CHECK_STR(jc_json_get_str(web, "url", "?"), "https://mcp/x");
        JC_CHECK_STR(cJSON_GetArrayItem(hdrs, 0)->valuestring, "X-Key: abc");
    }
    /* LSP command array split into command + args, dots trimmed. */
    {
        cJSON *lsp = cJSON_GetArrayItem(jc_json_get_obj(root, "lspServers"), 0);
        cJSON *exts = jc_json_get_obj(lsp, "extensions");
        JC_CHECK_STR(jc_json_get_str(lsp, "command", "?"), "my-ls");
        JC_CHECK_STR(cJSON_GetArrayItem(exts, 0)->valuestring, "ml");
    }
    /* bash+edit both denied -> plan mode; instructions carried through. */
    JC_CHECK_STR(jc_json_get_str(root, "mode", "?"), "plan");
    JC_CHECK(cJSON_GetArraySize(jc_json_get_obj(root, "instructions")) == 2);
    perm = jc_json_get_obj(root, "permissions");
    JC_CHECK(cJSON_GetArraySize(jc_json_get_obj(perm, "deny")) >= 2);

    cJSON_Delete(root);
    free(res.json);
    jc_arena_free(a);
}

static const char *YAML_MCP =
    "models:\n"
    "  - name: C\n"
    "    provider: anthropic\n"
    "    model: claude\n"
    "    roles:\n"
    "      - chat\n"
    "mcpServers:\n"
    "  - name: local-fs\n"
    "    command: mcp-fs\n"
    "    args:\n"
    "      - --root\n"
    "      - .\n"
    "    env:\n"
    "      TOKEN: secret\n";

static void test_convert_continue_mcp(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_convert_result res;
    cJSON *root;
    cJSON *mcp;
    cJSON *s0;

    JC_CHECK(jc_convert_run(YAML_MCP, JC_SRC_CONTINUE_YAML, &res, a) == JC_OK);
    root = jc_json_parse(res.json);
    mcp = jc_json_get_obj(root, "mcpServers");
    JC_CHECK(cJSON_GetArraySize(mcp) == 1);
    s0 = cJSON_GetArrayItem(mcp, 0);
    JC_CHECK_STR(jc_json_get_str(s0, "command", "?"), "mcp-fs");
    JC_CHECK(cJSON_GetArraySize(jc_json_get_obj(s0, "args")) == 2);
    JC_CHECK_STR(jc_json_get_str(jc_json_get_obj(s0, "env"), "TOKEN", "?"),
                 "secret");

    cJSON_Delete(root);
    free(res.json);
    jc_arena_free(a);
}

static const char *OPENCODE_ASSETS =
    "{\n"
    "  \"model\": \"anthropic/claude\",\n"
    "  \"agent\": {\n"
    "    \"reviewer\": {\n"
    "      \"description\": \"Reviews code: finds bugs\",\n"
    "      \"model\": \"anthropic/claude\",\n"
    "      \"prompt\": \"You are a careful reviewer.\",\n"
    "      \"tools\": { \"*\": false, \"read\": true, \"grep\": true }\n"
    "    }\n"
    "  },\n"
    "  \"command\": {\n"
    "    \"ship\": { \"description\": \"Ship it\", \"agent\": \"reviewer\",\n"
    "      \"template\": \"Review then summarize $ARGUMENTS\" }\n"
    "  }\n"
    "}\n";

/* Find an asset whose relpath starts with `prefix`. */
static const char *find_asset(const struct jc_ir *ir, const char *prefix)
{
    int i;
    jc_size n = strlen(prefix);
    for (i = 0; i < ir->asset_count; i++) {
        if (strncmp(ir->assets[i]->relpath, prefix, n) == 0) {
            return ir->assets[i]->contents;
        }
    }
    return NULL;
}

static void test_convert_assets_roundtrip(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_convert_result res;
    const char *agent_md;
    const char *cmd_md;
    struct jc_md_doc doc;

    JC_CHECK(jc_convert_run(OPENCODE_ASSETS, JC_SRC_OPENCODE, &res, a) == JC_OK);

    /* The emitted agent markdown must parse back through jichi's own loader. */
    agent_md = find_asset(res.ir, "agents/reviewer");
    JC_CHECK(agent_md != NULL);
    jc_md_parse(agent_md, a, &doc);
    JC_CHECK(doc.front != NULL);
    /* A description containing a colon survives the quote/round-trip. */
    JC_CHECK_STR(jc_yaml_get_str(doc.front, "description", "?"),
                 "Reviews code: finds bugs");
    JC_CHECK_STR(jc_yaml_get_str(doc.front, "model", "?"), "anthropic/claude");
    JC_CHECK_STR(jc_yaml_get_str(doc.front, "readonly", "?"), "true");
    {
        struct jc_yaml *tools = jc_yaml_get(doc.front, "tools");
        JC_CHECK(jc_yaml_seq_len(tools) == 2);
        JC_CHECK_STR(jc_yaml_seq_at(tools, 0)->scalar, "read_file");
        JC_CHECK_STR(jc_yaml_seq_at(tools, 1)->scalar, "search_code");
    }
    /* Body is the agent prompt. */
    JC_CHECK(strstr(doc.body, "careful reviewer") != NULL);
    jc_md_free(&doc);

    cmd_md = find_asset(res.ir, "commands/ship");
    JC_CHECK(cmd_md != NULL);
    jc_md_parse(cmd_md, a, &doc);
    JC_CHECK(doc.front != NULL);
    JC_CHECK_STR(jc_yaml_get_str(doc.front, "agent", "?"), "reviewer");
    JC_CHECK(strstr(doc.body, "$ARGUMENTS") != NULL);
    jc_md_free(&doc);

    free(res.json);
    jc_arena_free(a);
}

/* #1: Claude Code config tree -> jichi config + assets. Uses a temp dir. */
static void test_convert_claude(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char base[512];
    char sub[600];
    struct jc_convert_result res;
    long pid = (long)getpid();

    jc_snprintf(base, sizeof base, "%s/jctest-claude-%ld", jc_test_tmpdir(), pid);
    jc_snprintf(sub, sizeof sub, "%s/.claude/agents", base);
    jc_mkdir_p(sub);
    jc_snprintf(sub, sizeof sub, "%s/.claude/commands", base);
    jc_mkdir_p(sub);

    {
        const char *s =
            "{\"model\":\"claude-sonnet-4-5\",\"mcpServers\":{\"fs\":{"
            "\"command\":\"mcp-fs\",\"args\":[\"--root\",\".\"]}},"
            "\"hooks\":{}}";
        jc_snprintf(sub, sizeof sub, "%s/.claude/settings.json", base);
        jc_write_file(sub, s, strlen(s));
    }
    jc_snprintf(sub, sizeof sub, "%s/CLAUDE.md", base);
    jc_write_file(sub, "# Rules\nBe careful.\n", strlen("# Rules\nBe careful.\n"));
    jc_snprintf(sub, sizeof sub, "%s/.claude/agents/reviewer.md", base);
    jc_write_file(sub, "---\ndescription: r\n---\nReview.\n",
                  strlen("---\ndescription: r\n---\nReview.\n"));

    JC_CHECK(jc_convert_run_claude(base, &res, a) == JC_OK);
    JC_CHECK(res.json != NULL);
    if (res.json != NULL) {
        JC_CHECK(strstr(res.json, "\"anthropic\"") != NULL);
        JC_CHECK(strstr(res.json, "claude-sonnet-4-5") != NULL);
        JC_CHECK(strstr(res.json, "ANTHROPIC_API_KEY") != NULL);
        JC_CHECK(strstr(res.json, "mcp-fs") != NULL);
        free(res.json);
    }
    /* assets: AGENTS.md (from CLAUDE.md) + agents/reviewer.md carried over. */
    if (res.ir != NULL) {
        int i, saw_agents = 0, saw_reviewer = 0;
        for (i = 0; i < res.ir->asset_count; i++) {
            const char *rp = res.ir->assets[i]->relpath;
            if (rp != NULL && strcmp(rp, "AGENTS.md") == 0) saw_agents = 1;
            if (rp != NULL && strcmp(rp, "agents/reviewer.md") == 0)
                saw_reviewer = 1;
        }
        JC_CHECK(saw_agents == 1);
        JC_CHECK(saw_reviewer == 1);
    }
    /* warnings mention hooks (unmapped). */
    JC_CHECK(res.warning_count > 0);

    /* cleanup (best-effort). */
    {
        char rm[600];
        jc_snprintf(rm, sizeof rm, "rm -rf %s", base);
        if (system(rm) != 0) { /* ignore */ }
    }
    jc_arena_free(a);
}

void test_convert(void)
{
    test_yaml_parser_basics();
    test_yaml_quoted_seq_scalar();
    test_yaml_block_scalars();
    test_convert_yaml();
    test_convert_json();
    test_convert_templated_key();
    test_convert_opencode();
    test_convert_continue_mcp();
    test_convert_assets_roundtrip();
    test_convert_claude();
}
