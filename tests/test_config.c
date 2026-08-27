/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_config.c - exercises jc_config loading from a temp file. */

#include "jc_test.h"
#include "jc_config.h"
#include "jc_perm.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_http.h"
#include "jc_str.h"
#include "jc_base64.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- M505: was the model id configured, or substituted? -------------------
 *
 * The defect: `{"models":[{"name":"a"}]}` parsed into an active model of
 * `claude-opus-4-8` -- a PRICED frontier id from a built-in default -- and
 * `config validate` said OK while doctor rendered it as a green
 * "configuration loaded" line, indistinguishable from a config that named it.
 * The substitution is deliberate and stays; being unable to SEE it is the bug,
 * the same argument M503's verify_source makes. */
static void test_model_defaulted_flag(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config c;


    /* No "model" key: the id is substituted, and the flag says so. */
    JC_CHECK(jc_config_load_json("{\"models\":[{\"name\":\"a\"}]}", 0, &c, a)
             == JC_OK);
    JC_CHECK(c.model.model_defaulted == 1);
    JC_CHECK(c.model.model != NULL && c.model.model[0] != '\0');
    /* M519: each load allocates HEAP vectors (the models vector, via
     * push_model) that the arena does not own, and the next load overwrites the
     * handles -- so a free per load, not one at the end. Found by `make ci`,
     * whose ASan stage had been reporting 4,224 bytes in 3 allocations here
     * since this test was written; the leak is invisible to `make test`. */
    jc_config_free(&c);

    /* Named explicitly: no substitution, no flag. A false positive here would
     * teach an operator to ignore the warning, which is worse than silence. */
    JC_CHECK(jc_config_load_json(
        "{\"models\":[{\"name\":\"a\",\"model\":\"jlu/qwen3-coder-next\"}]}",
        0, &c, a) == JC_OK);
    JC_CHECK(c.model.model_defaulted == 0);
    JC_CHECK_STR(c.model.model, "jlu/qwen3-coder-next");
    jc_config_free(&c);

    /* An empty string is a NAMED id as far as the config is concerned -- it is
     * not absent -- so the flag stays clear and the (useless) id travels. Pinned
     * because the alternative reading would make the warning fire on a
     * different defect and say the wrong thing about it. */
    JC_CHECK(jc_config_load_json(
        "{\"models\":[{\"name\":\"a\",\"model\":\"\"}]}", 0, &c, a) == JC_OK);
    JC_CHECK(c.model.model_defaulted == 0);
    jc_config_free(&c);

    jc_arena_free(a);
}

void test_config(void)
{
    struct jc_arena *a;
    struct jc_config cfg;
    const char *path = jc_test_tmp("jichi_test_config.json");
    FILE *f;

    test_model_defaulted_flag();
    a = jc_arena_new(0);
    JC_CHECK(a != NULL);

    /* Defaults when no file is named and none exists. */
    {
        struct jc_config d;
        jc_config_load(jc_test_tmp("does_not_exist_jlu.json"), 0, &d, a);
        /* Explicit missing path => error path, but struct still defaulted. */
        JC_CHECK(d.max_tool_iters == 25);
    }

    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fputs("{ \"model\": { \"provider\": \"openai\", "
              "\"model\": \"gpt-4o-mini\", \"apiBase\": \"https://x.test\", "
              "\"apiKey\": \"sk-literal\", \"maxTokens\": 1234, "
              "\"temperature\": 0.5, \"inputCostPer1M\": 3.0, "
              "\"outputCostPer1M\": 15.0 }, "
              "\"maxToolIters\": 9, \"maxRetries\": 7 }", f);
        fclose(f);
    }

    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK_STR(cfg.model.provider, "openai");
    JC_CHECK_STR(cfg.model.model, "gpt-4o-mini");
    JC_CHECK_STR(cfg.model.api_base, "https://x.test");
    JC_CHECK_STR(cfg.model.api_key, "sk-literal");
    JC_CHECK(cfg.model.api_key_literal == 1); /* M55: literal-key flag set */
    JC_CHECK(cfg.model.max_tokens == 1234);
    JC_CHECK(cfg.model.temperature == 0.5);
    JC_CHECK(cfg.max_tool_iters == 9);
    JC_CHECK(cfg.max_retries == 7);

    /* Cost: 1M in @ $3 + 1M out @ $15 = $18 (no cache tokens). */
    JC_CHECK(jc_config_cost(&cfg.model, 1000000.0, 1000000.0, 0.0, 0.0) == 18.0);
    /* No pricing => zero cost. */
    {
        struct jc_model_cfg bare;
        memset(&bare, 0, sizeof(bare));
        JC_CHECK(jc_config_cost(&bare, 1000.0, 1000.0, 500.0, 500.0) == 0.0);
    }
    /* Cache-aware cost (M31c). With no cache pricing, cached reads/writes bill
     * at the input rate: 1M in + 1M read + 1M write all @ $3 = $9. */
    {
        struct jc_model_cfg m;
        memset(&m, 0, sizeof(m));
        m.input_cost = 3.0;
        m.output_cost = 15.0;
        JC_CHECK(jc_config_cost(&m, 1000000.0, 0.0, 1000000.0, 1000000.0)
                 == 9.0);
        /* With explicit cache pricing: in @ $3 + read 1M @ $0.30 +
         * write 1M @ $3.75 = 3 + 0.30 + 3.75 = $7.05. */
        m.cache_read_cost = 0.30;
        m.cache_write_cost = 3.75;
        JC_CHECK_NEAR(jc_config_cost(&m, 1000000.0, 0.0, 1000000.0, 1000000.0),
                      7.05);
    }

    /* Prompt-cache tri-state resolution (M31d): per-model key wins; otherwise
     * the global default (auto => on). */
    {
        struct jc_config c;
        struct jc_model_cfg m0, m1;
        memset(&c, 0, sizeof(c));
        jc_vec_init(&c.models, sizeof(struct jc_model_cfg));
        memset(&m0, 0, sizeof(m0));
        m0.prompt_cache_cfg = -1;   /* inherit the global */
        memset(&m1, 0, sizeof(m1));
        m1.prompt_cache_cfg = 0;    /* explicit off */
        jc_vec_push(&c.models, &m0);
        jc_vec_push(&c.models, &m1);
        c.model = m0;               /* active is the inheriting model */

        c.prompt_cache = -1;        /* auto => inheritors on */
        jc_config_resolve_prompt_cache(&c);
        JC_CHECK(((struct jc_model_cfg *)jc_vec_at(&c.models, 0))->prompt_cache
                 == 1);
        JC_CHECK(((struct jc_model_cfg *)jc_vec_at(&c.models, 1))->prompt_cache
                 == 0);
        JC_CHECK(c.model.prompt_cache == 1);

        c.prompt_cache = 0;         /* global off => inheritors off; explicit stays */
        jc_config_resolve_prompt_cache(&c);
        JC_CHECK(((struct jc_model_cfg *)jc_vec_at(&c.models, 0))->prompt_cache
                 == 0);
        JC_CHECK(((struct jc_model_cfg *)jc_vec_at(&c.models, 1))->prompt_cache
                 == 0);

        c.prompt_cache = 1;         /* global on => inheritors on; explicit off stays */
        jc_config_resolve_prompt_cache(&c);
        JC_CHECK(((struct jc_model_cfg *)jc_vec_at(&c.models, 0))->prompt_cache
                 == 1);
        JC_CHECK(((struct jc_model_cfg *)jc_vec_at(&c.models, 1))->prompt_cache
                 == 0);

        /* The global 1-hour TTL choice propagates to every model (M31e). */
        c.prompt_cache_1h = 1;
        jc_config_resolve_prompt_cache(&c);
        JC_CHECK(((struct jc_model_cfg *)jc_vec_at(&c.models, 0))->prompt_cache_1h
                 == 1);
        JC_CHECK(c.model.prompt_cache_1h == 1);
        jc_vec_free(&c.models);
    }

    jc_config_free(&cfg);
    remove(path);

    /* Multi-model config: models[] array, switching, and selectors. */
    {
        struct jc_config mc;
        const char *mpath = jc_test_tmp("jichi_test_multi.json");
        FILE *mf = fopen(mpath, "wb");
        JC_CHECK(mf != NULL);
        if (mf != NULL) {
            fputs("{ \"models\": ["
                  "{ \"name\": \"Alpha\", \"provider\": \"anthropic\", "
                  "\"model\": \"claude-x\", \"apiKey\": \"k1\", "
                  "\"roles\": [\"chat\", \"edit\"] },"
                  "{ \"name\": \"Beta\", \"provider\": \"openai\", "
                  "\"model\": \"gpt-x\", \"apiKey\": \"k2\" },"
                  "{ \"name\": \"Emb\", \"provider\": \"openai\", "
                  "\"model\": \"emb-x\", \"apiKey\": \"k3\", "
                  "\"roles\": [\"embed\"] },"
                  "{ \"name\": \"Rr\", \"provider\": \"openai\", "
                  "\"model\": \"rr-x\", \"apiKey\": \"k4\", "
                  "\"roles\": [\"rerank\"] } ] }", mf);
            fclose(mf);
        }
        JC_CHECK(jc_config_load(mpath, 0, &mc, a) == JC_OK);
        JC_CHECK(jc_config_model_count(&mc) == 4);
        JC_CHECK(mc.active == 0);
        JC_CHECK_STR(mc.model.model, "claude-x");

        JC_CHECK(jc_config_set_active(&mc, 1) == JC_OK);
        JC_CHECK_STR(mc.model.model, "gpt-x");
        JC_CHECK(jc_config_set_active(&mc, 5) == JC_ERR_INVALID);

        JC_CHECK(jc_config_find_model(&mc, "2") == 1);      /* 1-based index */
        JC_CHECK(jc_config_find_model(&mc, "alpha") == 0);  /* case-insensitive */
        JC_CHECK(jc_config_find_model(&mc, "gpt") == 1);    /* model-id match */
        JC_CHECK(jc_config_find_model(&mc, "nope") == -1);

        /* Roles: parsed into a flag bitmask and looked up by role. */
        {
            struct jc_model_cfg *m0 = jc_config_model_at(&mc, 0);
            struct jc_model_cfg *m1 = jc_config_model_at(&mc, 1);
            struct jc_model_cfg *emb;
            /* JC_CHECK RECORDS AND CONTINUES. So a null here must be checked
             * and then RETURNED FROM, not checked and dereferenced on the next
             * line -- otherwise one environment failure upstream turns a red
             * into a SIGSEGV and takes the remaining ~118 test files with it.
             * That is the M265 lesson (a null check followed by a dereference,
             * found on git < 2.5) recurring; it was found again on an Android
             * tablet (M452), where /tmp does not exist, so the fixture above
             * was never written, jc_config_load failed, and this block
             * dereferenced a model that had never been parsed. The suite
             * aborted at test_config and reported nothing after it. */
            JC_CHECK(m0 != NULL);
            JC_CHECK(m1 != NULL);
            if (m0 != NULL && m1 != NULL) {
                JC_CHECK(m0->roles == (JC_ROLE_CHAT | JC_ROLE_EDIT));
                JC_CHECK(m1->roles == 0u);
            }
            JC_CHECK(jc_config_find_by_role(&mc, JC_ROLE_EMBED) == 2);
            JC_CHECK(jc_config_find_by_role(&mc, JC_ROLE_RERANK) == 3);
            JC_CHECK(jc_config_find_by_role(&mc, JC_ROLE_AUTOCOMPLETE) == -1);
            emb = jc_config_model_for_role(&mc, JC_ROLE_EMBED);
            JC_CHECK(emb != NULL);
            if (emb != NULL) {
                JC_CHECK_STR(emb->model, "emb-x");
            }
            JC_CHECK(jc_config_role_flag("rerank") == JC_ROLE_RERANK);
            JC_CHECK(jc_config_role_flag("bogus") == 0u);
        }

        jc_config_free(&mc);
        remove(mpath);
    }

    /* mcpServers: transport inference, args/env, and the approval policy. */
    {
        struct jc_config sc;
        const char *spath = jc_test_tmp("jichi_test_mcp.json");
        FILE *sf = fopen(spath, "wb");
        JC_CHECK(sf != NULL);
        if (sf != NULL) {
            fputs("{ \"model\": { \"provider\": \"anthropic\", "
                  "\"model\": \"x\", \"apiKey\": \"k\" }, "
                  "\"mcpServers\": ["
                  "{ \"name\": \"fs\", \"command\": \"npx\", "
                  "\"args\": [\"-y\", \"srv\"], \"env\": { \"K\": \"V\" }, "
                  "\"autoApprove\": [\"read\"], \"deny\": [\"rm\"] },"
                  "{ \"name\": \"remote\", \"url\": \"https://h/mcp\", "
                  "\"headers\": [\"Authorization: Bearer T\"], "
                  "\"autoApprove\": \"*\" },"
                  "{ \"command\": \"x\" }" /* no name => skipped */
                  "] }", sf);
            fclose(sf);
        }
        JC_CHECK(jc_config_load(spath, 0, &sc, a) == JC_OK);
        JC_CHECK(sc.mcp_servers.len == 2); /* the unnamed entry is dropped */
        if (sc.mcp_servers.len == 2) {
            struct jc_mcp_server_cfg *s0 =
                (struct jc_mcp_server_cfg *)jc_vec_at(&sc.mcp_servers, 0);
            struct jc_mcp_server_cfg *s1 =
                (struct jc_mcp_server_cfg *)jc_vec_at(&sc.mcp_servers, 1);

            JC_CHECK_STR(s0->name, "fs");
            JC_CHECK_STR(s0->type, "stdio"); /* inferred (no url) */
            JC_CHECK_STR(s0->command, "npx");
            JC_CHECK(s0->args.len == 2);
            JC_CHECK_STR(JC_VEC_STR(&s0->args, 0), "-y");
            JC_CHECK(s0->env.len == 1);
            JC_CHECK_STR(JC_VEC_STR(&s0->env, 0), "K=V");
            JC_CHECK(s0->auto_approve.len == 1);
            JC_CHECK_STR(JC_VEC_STR(&s0->auto_approve, 0), "read");
            JC_CHECK(s0->auto_approve_all == 0);
            JC_CHECK(s0->deny.len == 1);
            JC_CHECK(s0->deny_all == 0);

            JC_CHECK_STR(s1->name, "remote");
            JC_CHECK_STR(s1->type, "http"); /* inferred from url */
            JC_CHECK_STR(s1->url, "https://h/mcp");
            JC_CHECK(s1->headers.len == 1);
            JC_CHECK(s1->auto_approve_all == 1); /* "*" => all */
            JC_CHECK(s1->auto_approve.len == 0);
        }
        jc_config_free(&sc);
        remove(spath);
    }

    /* mode + top-level permissions. */
    {
        struct jc_config pc;
        const char *ppath = jc_test_tmp("jichi_test_perm.json");
        FILE *pf = fopen(ppath, "wb");
        JC_CHECK(pf != NULL);
        if (pf != NULL) {
            fputs("{ \"model\": { \"provider\": \"anthropic\", "
                  "\"model\": \"x\", \"apiKey\": \"k\" }, "
                  "\"mode\": \"plan\", "
                  "\"permissions\": { \"allow\": [\"read_file\"], "
                  "\"deny\": \"*\" } }", pf);
            fclose(pf);
        }
        JC_CHECK(jc_config_load(ppath, 0, &pc, a) == JC_OK);
        JC_CHECK(pc.default_mode == JC_MODE_PLAN);
        JC_CHECK(pc.permissions.allow.len == 1);
        JC_CHECK_STR(JC_VEC_STR(&pc.permissions.allow, 0), "read_file");
        JC_CHECK(pc.permissions.allow_all == 0);
        JC_CHECK(pc.permissions.deny_all == 1); /* "*" => all */
        jc_config_free(&pc);
        remove(ppath);
    }

    /* Default mode is chat when unspecified. */
    {
        struct jc_config dc;
        const char *dpath = jc_test_tmp("jichi_test_defmode.json");
        FILE *df = fopen(dpath, "wb");
        if (df != NULL) {
            fputs("{ \"model\": { \"provider\": \"anthropic\", "
                  "\"model\": \"x\", \"apiKey\": \"k\" } }", df);
            fclose(df);
        }
        JC_CHECK(jc_config_load(dpath, 0, &dc, a) == JC_OK);
        JC_CHECK(dc.default_mode == JC_MODE_CHAT);
        jc_config_free(&dc);
        remove(dpath);
    }

    /* M20b: the low-resource / --lite profile. */
    {
        struct jc_config lc;
        const char *lpath = jc_test_tmp("jichi_test_lite.json");
        FILE *lf;
        const char *minimal =
            "{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
            "\"apiKey\": \"k\" } }";

        /* M20c: the cap resolver. 0 => the tool's built-in; >0 => configured. */
        JC_CHECK(jc_config_cap(0, 4096) == 4096);
        JC_CHECK(jc_config_cap(1000, 4096) == 1000);
        JC_CHECK(jc_config_cap(-5, 4096) == 4096); /* negative => built-in */

        /* The run-timeout resolver: per_call > cli > cfg, first >0 wins;
         * <=0 is unset, all-unset => 0 (no limit). */
        JC_CHECK(jc_config_run_timeout(0, 0, 0) == 0);       /* all off */
        JC_CHECK(jc_config_run_timeout(0, 0, 120) == 120);   /* cfg only */
        JC_CHECK(jc_config_run_timeout(0, 60, 120) == 60);   /* cli beats cfg */
        JC_CHECK(jc_config_run_timeout(30, 60, 120) == 30);  /* per-call wins */
        JC_CHECK(jc_config_run_timeout(30, 0, 0) == 30);     /* per-call only */
        JC_CHECK(jc_config_run_timeout(0, -1, 120) == 120);  /* neg cli unset */
        JC_CHECK(jc_config_run_timeout(-9, -1, 0) == 0);     /* all unset/off */

        /* Baseline: a normal load has the normal defaults and lite off. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) { fputs(minimal, lf); fclose(lf); }
        JC_CHECK(jc_config_load(lpath, 0, &lc, a) == JC_OK);
        JC_CHECK(lc.low_resource == 0);
        JC_CHECK(lc.repo_map == 1 && lc.snapshots == 1 && lc.references == 1);
        JC_CHECK(lc.markdown == 1 && lc.max_subagent_depth == 2);
        JC_CHECK(lc.craft == 1);   /* M318: on outside the lean tier */
        JC_CHECK(lc.context_limit == 0 && lc.max_parallel_agents == 0);
        /* Caps default to 0 (=> built-in) when not lite and not set. */
        JC_CHECK(lc.read_max_bytes == 0 && lc.run_max_bytes == 0);
        JC_CHECK(lc.fetch_max_bytes == 0 && lc.search_max_bytes == 0);
        JC_CHECK(lc.git_max_bytes == 0);
        jc_config_free(&lc);

        /* The --lite CLI hint shifts the resource-heavy defaults. */
        JC_CHECK(jc_config_load(lpath, 1, &lc, a) == JC_OK);
        JC_CHECK(lc.low_resource == 1);
        JC_CHECK(lc.repo_map == 0 && lc.snapshots == 0 && lc.references == 0);
        JC_CHECK(lc.markdown == 0 && lc.max_subagent_depth == 0);
        /* M318: measured off under lite -- 329-386 tokens per call for no
         * measurable difference on a 31B model (18/18 pass either way). */
        JC_CHECK(lc.craft == 0);
        JC_CHECK(lc.max_parallel_agents == 1);
        JC_CHECK(lc.context_limit == 16384);
        JC_CHECK(lc.max_tool_iters == 12 && lc.max_retries == 2);
        /* Lite shrinks the tool caps. */
        JC_CHECK(lc.read_max_bytes == 65536 && lc.run_max_bytes == 16384);
        JC_CHECK(lc.fetch_max_bytes == 32768 && lc.search_max_bytes == 16384);
        JC_CHECK(lc.git_max_bytes == 8192);
        jc_config_free(&lc);
        remove(lpath);

        /* The config "lowResource": true does the same (no CLI hint), and an
         * explicitly-set sibling key still wins over the lean default. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" }, \"lowResource\": true, "
                  "\"repoMap\": true, \"contextLimit\": 50000, "
                  "\"craft\": true, "
                  "\"readMaxBytes\": 100000 }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, 0, &lc, a) == JC_OK);
        JC_CHECK(lc.low_resource == 1);
        JC_CHECK(lc.repo_map == 1);        /* explicit key wins */
        JC_CHECK(lc.context_limit == 50000); /* explicit key wins */
        JC_CHECK(lc.read_max_bytes == 100000); /* explicit cap wins */
        JC_CHECK(lc.run_max_bytes == 16384);   /* unset cap => lean default */
        JC_CHECK(lc.snapshots == 0 && lc.references == 0); /* lean defaults */
        JC_CHECK(lc.craft == 1);   /* M318: an explicit key wins BOTH ways */
        JC_CHECK(lc.max_parallel_agents == 1);
        jc_config_free(&lc);
        remove(lpath);
    }

    /* M272: lean-profile precedence -- flag > config key > auto-detection.
     * Before M272 the key was OR-ed with the hint, so "lowResource": false
     * could not veto auto-lite (JC_LITE_HINT_AUTO, main.c's low-RAM
     * detection) -- found when the smoke tier first ran on a 256 MB guest. */
    {
        struct jc_config lc;
        const char *lpath = jc_test_tmp("jichi_test_lite_tri.json");
        FILE *lf;

        /* Explicit config false vetoes the auto-detection hint. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" }, \"lowResource\": false }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, JC_LITE_HINT_AUTO, &lc, a) == JC_OK);
        JC_CHECK(lc.low_resource == 0);
        JC_CHECK(lc.repo_map == 1 && lc.snapshots == 1);
        JC_CHECK(lc.max_subagent_depth == 2);
        jc_config_free(&lc);

        /* Key absent: the auto hint applies (lean profile on). */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" } }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, JC_LITE_HINT_AUTO, &lc, a) == JC_OK);
        JC_CHECK(lc.low_resource == 1);
        JC_CHECK(lc.max_subagent_depth == 0);
        jc_config_free(&lc);

        /* --no-lite (HINT_OFF) beats an explicit config true: CLI > config,
         * jichi's precedence rule everywhere else. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" }, \"lowResource\": true }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, JC_LITE_HINT_OFF, &lc, a) == JC_OK);
        JC_CHECK(lc.low_resource == 0);
        jc_config_free(&lc);

        /* --lite (HINT_ON) beats an explicit config false. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" }, \"lowResource\": false }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, JC_LITE_HINT_ON, &lc, a) == JC_OK);
        JC_CHECK(lc.low_resource == 1);
        jc_config_free(&lc);
        remove(lpath);
    }

    /* The lite context cap, and what it outranks. LOW_MEMORY.md publishes
     * `contextLimit 16384` as part of the lean profile, and effective_limit()
     * (jc_compact.c) prefers the TOP-LEVEL limit over the active model's
     * contextLength -- so under lite a model declaring a large window is
     * budgeted small. That is deliberate (the 965 MB Archos row depends on it)
     * and stays; what was wrong is that a DEFAULT outranked an EXPLICIT
     * declaration in silence. jichi now says so; the message itself is checked
     * by tests/smoke/lite_context_cap.sh, and these checks pin the numbers the
     * message is about.
     */
    {
        struct jc_config lc;
        const char *lpath = jc_test_tmp("jichi_test_lite_ctx.json");
        FILE *lf;

        /* Declared window, no explicit contextLimit: the cap is in force AND
         * the declaration survives on the model, which is what lets the
         * warning state both numbers. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\", \"contextLength\": 196608 } }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, JC_LITE_HINT_ON, &lc, a) == JC_OK);
        JC_CHECK(lc.context_limit == 16384);
        JC_CHECK(lc.model.context_limit == 196608);
        jc_config_free(&lc);

        /* An EXPLICIT contextLimit is the operator's own choice and beats
         * lite's default -- so no cap is imposed behind their back, and the
         * warning must stay quiet for this config. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"contextLimit\": 200000, \"model\": { \"provider\": "
                  "\"openai\", \"model\": \"m\", \"apiKey\": \"k\", "
                  "\"contextLength\": 196608 } }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, JC_LITE_HINT_ON, &lc, a) == JC_OK);
        JC_CHECK(lc.context_limit == 200000);
        jc_config_free(&lc);

        /* Without lite there is no top-level number at all, so
         * effective_limit() falls through to the model's declaration. */
        lf = fopen(lpath, "wb");
        if (lf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\", \"contextLength\": 196608 } }", lf);
            fclose(lf);
        }
        JC_CHECK(jc_config_load(lpath, JC_LITE_HINT_OFF, &lc, a) == JC_OK);
        JC_CHECK(lc.context_limit == 0);
        JC_CHECK(lc.model.context_limit == 196608);
        jc_config_free(&lc);
        remove(lpath);
    }

    /* M21b: the "logging" block (event-log tier + path). */
    {
        struct jc_config gc;
        const char *gpath = jc_test_tmp("jichi_test_logging.json");
        FILE *gf;

        /* No logging block => METRICS (M599: on by default, so the learner
         * does not forget), no path (the per-workspace default is resolved at
         * open time, not stored in the config). */
        gf = fopen(gpath, "wb");
        if (gf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" } }", gf);
            fclose(gf);
        }
        JC_CHECK(jc_config_load(gpath, 0, &gc, a) == JC_OK);
        JC_CHECK(gc.log_level == 1 /* JC_EVENTLOG_METRICS */ && gc.log_path == NULL);
        jc_config_free(&gc);

        /* logging:{level,path} parses to the tier + path. */
        gf = fopen(gpath, "wb");
        if (gf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" }, \"logging\": { \"level\": \"full\", "
                  "\"path\": \"/tmp/t.jsonl\" } }", gf);
            fclose(gf);
        }
        JC_CHECK(jc_config_load(gpath, 0, &gc, a) == JC_OK);
        JC_CHECK(gc.log_level == 2); /* JC_EVENTLOG_FULL */
        JC_CHECK(gc.log_path != NULL && strcmp(gc.log_path, "/tmp/t.jsonl") == 0);
        jc_config_free(&gc);
        remove(gpath);
    }

    /* M22b: the "timeouts" block (global + per-model) and the resolver. */
    {
        struct jc_config tc;
        const char *tpath = jc_test_tmp("jichi_test_timeouts.json");
        FILE *tf;
        long c0, s0, r0;

        /* No block anywhere => every tier unset => the built-in defaults. */
        tf = fopen(tpath, "wb");
        if (tf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKey\": \"k\" } }", tf);
            fclose(tf);
        }
        JC_CHECK(jc_config_load(tpath, 0, &tc, a) == JC_OK);
        JC_CHECK(tc.timeouts.connect == -1 && tc.timeouts.stall == -1);
        JC_CHECK(tc.timeouts.request == -1);
        JC_CHECK(tc.model.timeouts.connect == -1); /* per-model unset too */
        jc_config_resolve_timeouts(&tc, &tc.model, &c0, &s0, &r0);
        JC_CHECK(c0 == JC_HTTP_CONNECT_TIMEOUT_DEFAULT);
        JC_CHECK(s0 == JC_HTTP_STALL_TIMEOUT_DEFAULT);
        JC_CHECK(r0 == 0); /* no hard request cap by default */
        jc_config_free(&tc);
        remove(tpath);

        /* A global block applies; a per-model block overrides it; an absent
         * per-model key still falls through to the global value; 0 disables. */
        tf = fopen(tpath, "wb");
        if (tf != NULL) {
            fputs("{ \"timeouts\": { \"connect\": 5, \"stall\": 45, "
                  "\"request\": 120 }, \"models\": ["
                  "{ \"name\": \"slow\", \"provider\": \"openai\", "
                  "\"model\": \"m1\", \"apiKey\": \"k\", "
                  "\"timeouts\": { \"stall\": 90, \"connect\": 0 } },"
                  "{ \"name\": \"plain\", \"provider\": \"openai\", "
                  "\"model\": \"m2\", \"apiKey\": \"k\" } ] }", tf);
            fclose(tf);
        }
        JC_CHECK(jc_config_load(tpath, 0, &tc, a) == JC_OK);
        JC_CHECK(tc.timeouts.connect == 5 && tc.timeouts.stall == 45);
        JC_CHECK(tc.timeouts.request == 120);

        /* Model 0 ("slow"): per-model stall=90 and connect=0 (disabled) win;
         * request unset => falls through to the global 120. */
        {
            struct jc_model_cfg *m0 = jc_config_model_at(&tc, 0);
            jc_config_resolve_timeouts(&tc, m0, &c0, &s0, &r0);
            JC_CHECK(c0 == 0);   /* per-model 0 => explicitly disabled */
            JC_CHECK(s0 == 90);  /* per-model override */
            JC_CHECK(r0 == 120); /* inherited from the global block */
        }
        /* Model 1 ("plain"): no per-model block => all from the global block. */
        {
            struct jc_model_cfg *m1 = jc_config_model_at(&tc, 1);
            jc_config_resolve_timeouts(&tc, m1, &c0, &s0, &r0);
            JC_CHECK(c0 == 5 && s0 == 45 && r0 == 120);
        }

        /* A CLI override is the strongest tier (beats per-model + global). */
        tc.timeouts_cli.stall = 7;
        {
            struct jc_model_cfg *m0 = jc_config_model_at(&tc, 0);
            jc_config_resolve_timeouts(&tc, m0, &c0, &s0, &r0);
            JC_CHECK(s0 == 7);   /* CLI wins over per-model 90 */
            JC_CHECK(c0 == 0);   /* unchanged: still per-model 0 */
            JC_CHECK(r0 == 120);
        }
        jc_config_free(&tc);
        remove(tpath);

        /* NULL config / model => the built-in defaults (defensive). */
        jc_config_resolve_timeouts(NULL, NULL, &c0, &s0, &r0);
        JC_CHECK(c0 == JC_HTTP_CONNECT_TIMEOUT_DEFAULT);
        JC_CHECK(s0 == JC_HTTP_STALL_TIMEOUT_DEFAULT && r0 == 0);
    }

    /* docs: external documentation sources (M34a). Well-formed entries parse to
     * {name, path}; entries missing either field are skipped. */
    {
        struct jc_config dc;
        const char *dpath = jc_test_tmp("jichi_test_docs.json");
        FILE *df = fopen(dpath, "wb");
        JC_CHECK(df != NULL);
        if (df != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", "
                  "\"model\": \"x\", \"apiKey\": \"k\" }, "
                  "\"docs\": ["
                  "{ \"name\": \"react\", \"path\": \"/tmp/react-docs\" },"
                  "{ \"name\": \"style\", \"path\": \"./style\" },"
                  "{ \"name\": \"nopath\" }," /* missing path => skipped */
                  "{ \"path\": \"/x\" }" /* missing name => skipped */
                  "] }", df);
            fclose(df);
        }
        JC_CHECK(jc_config_load(dpath, 0, &dc, a) == JC_OK);
        JC_CHECK(dc.docs.len == 2);
        {
            struct jc_docs_cfg *d0 =
                (struct jc_docs_cfg *)jc_vec_at(&dc.docs, 0);
            struct jc_docs_cfg *d1 =
                (struct jc_docs_cfg *)jc_vec_at(&dc.docs, 1);
            if (JC_REQUIRE(d0 != NULL && d1 != NULL)) {
                JC_CHECK_STR(d0->name, "react");
                JC_CHECK_STR(d0->path, "/tmp/react-docs");
                JC_CHECK_STR(d1->name, "style");
                JC_CHECK_STR(d1->path, "./style");
            }
        }
        jc_config_free(&dc);
        remove(dpath);
    }

    /* notify / notifyBell: completion notification config (M34f/F6). */
    {
        struct jc_config nc;
        const char *npath = jc_test_tmp("jichi_test_notify.json");
        FILE *nf = fopen(npath, "wb");
        JC_CHECK(nf != NULL);
        if (nf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", "
                  "\"model\": \"x\", \"apiKey\": \"k\" }, "
                  "\"notify\": \"notify-send done\", \"notifyBell\": true }",
                  nf);
            fclose(nf);
        }
        JC_CHECK(jc_config_load(npath, 0, &nc, a) == JC_OK);
        JC_CHECK_STR(nc.notify, "notify-send done");
        JC_CHECK(nc.notify_bell == 1);
        jc_config_free(&nc);
        remove(npath);

        /* Default: neither set. */
        {
            struct jc_config d2;
            jc_config_load(jc_test_tmp("does_not_exist_jlu2.json"), 0, &d2, a);
            JC_CHECK(d2.notify == NULL);
            JC_CHECK(d2.notify_bell == 0);
        }
    }

    /* Per-model "description" + the image-role model menu (per-workflow
     * selection). Two image models + one non-image model. */
    {
        struct jc_config ic;
        struct jc_sb sb;
        const char *ipath = jc_test_tmp("jichi_test_config_image.json");
        FILE *imf = fopen(ipath, "wb");
        JC_CHECK(imf != NULL);
        if (imf != NULL) {
            fputs("{ \"models\": ["
                  "{ \"name\": \"chat\", \"provider\": \"openai\", "
                  "\"model\": \"m\", \"apiKey\": \"k\", \"roles\": [\"chat\"] },"
                  "{ \"name\": \"flux\", \"provider\": \"openai\", "
                  "\"model\": \"flux.1-schnell\", \"apiBase\": \"http://x\", "
                  "\"description\": \"fast generalist\", "
                  "\"roles\": [\"image\"] },"
                  "{ \"name\": \"anime\", \"provider\": \"openai\", "
                  "\"model\": \"illustrious\", \"apiBase\": \"http://x\", "
                  "\"roles\": [\"image\"] } ] }", imf);
            fclose(imf);
        }
        JC_CHECK(jc_config_load(ipath, 0, &ic, a) == JC_OK);
        /* The description is parsed onto the model. */
        {
            int fi = jc_config_find_model(&ic, "flux");
            struct jc_model_cfg *fm = jc_config_model_at(&ic, fi);
            if (JC_REQUIRE(fm != NULL)) {
                JC_CHECK_STR(fm->description, "fast generalist");
            }
        }
        /* The menu lists both image models (named, with the description), and
         * not the chat model. */
        jc_sb_init(&sb);
        jc_config_models_for_role_list(&ic, JC_ROLE_IMAGE, &sb);
        JC_CHECK(sb.data != NULL);
        JC_CHECK(strstr(sb.data, "flux") != NULL);
        JC_CHECK(strstr(sb.data, "fast generalist") != NULL);
        JC_CHECK(strstr(sb.data, "anime") != NULL);
        JC_CHECK(strstr(sb.data, "chat\n") == NULL);
        jc_sb_free(&sb);
        jc_config_free(&ic);
        remove(ipath);
    }

    /* M54/M55: referenceRoots parses into a vec, and an apiKeyEnv model leaves
     * api_key_literal unset (so the doctor literal-key lint doesn't fire). */
    {
        struct jc_config rc;
        FILE *rf = fopen(path, "wb");
        JC_CHECK(rf != NULL);
        if (rf != NULL) {
            fputs("{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
                  "\"apiKeyEnv\": \"JC_TEST_UNSET_VAR_55\" }, "
                  "\"referenceRoots\": [\"/tmp/ref-a\", \"/tmp/ref-b\"] }", rf);
            fclose(rf);
        }
        JC_CHECK(jc_config_load(path, 0, &rc, a) == JC_OK);
        JC_CHECK(rc.model.api_key_literal == 0);
        JC_CHECK(rc.reference_roots.len == 2);
        jc_config_free(&rc);
        remove(path);
    }

    /* M74: tool-profile resolution. */
    {
        struct jc_config c;
        memset(&c, 0, sizeof(c));
        /* explicit wins regardless of context. */
        c.tool_profile = 1;
        JC_CHECK(jc_config_tool_profile_core(&c, 100000) == 1);
        c.tool_profile = 0;
        JC_CHECK(jc_config_tool_profile_core(&c, 2000) == 0);
        /* auto: core when small or lite, full otherwise. */
        c.tool_profile = -1;
        c.low_resource = 0;
        JC_CHECK(jc_config_tool_profile_core(&c, 8000) == 1);   /* < 12000 */
        JC_CHECK(jc_config_tool_profile_core(&c, 32000) == 0);  /* large */
        JC_CHECK(jc_config_tool_profile_core(&c, 0) == 0);      /* unknown */
        c.low_resource = 1;
        JC_CHECK(jc_config_tool_profile_core(&c, 32000) == 1);  /* lite => core */
        JC_CHECK(jc_config_tool_profile_core(NULL, 8000) == 0);
    }

    /* Effective max_tokens (M84): always > 0 so the request carries one. */
    {
        /* Configured value wins. */
        JC_CHECK(jc_config_effective_max_tokens(16384, 150000) == 16384);
        JC_CHECK(jc_config_effective_max_tokens(100, 0) == 100);
        /* Unset (<=0): derive ~1/5 of context, clamped to [512, 16384]. */
        JC_CHECK(jc_config_effective_max_tokens(0, 150000) == 16384); /* /5 capped */
        JC_CHECK(jc_config_effective_max_tokens(0, 40000) == 8000);   /* 40000/5 */
        JC_CHECK(jc_config_effective_max_tokens(0, 8192) == 1638);    /* 8192/5 */
        JC_CHECK(jc_config_effective_max_tokens(0, 1000) == 512);     /* floor */
        /* Unknown context: built-in default, never 0. */
        JC_CHECK(jc_config_effective_max_tokens(0, 0) == JC_DEFAULT_MAX_TOKENS);
        JC_CHECK(jc_config_effective_max_tokens(-1, 0) == JC_DEFAULT_MAX_TOKENS);
    }

    /* Global+project config merge: project scalars win; arrays union with
     * project entries first; base-only keys kept; overlay untouched. */
    {
        cJSON *base = cJSON_Parse(
            "{\"contextLimit\":1000,\"models\":[{\"name\":\"g\"}],"
            "\"markdown\":true}");
        cJSON *over = cJSON_Parse(
            "{\"contextLimit\":2000,\"models\":[{\"name\":\"p\"}],"
            "\"wisdom\":false}");
        cJSON *cl, *ms, *first, *nm;
        JC_CHECK(base != NULL && over != NULL);
        jc_config_merge_json(base, over);
        cl = cJSON_GetObjectItemCaseSensitive(base, "contextLimit");
        JC_CHECK(cl != NULL && (int)cl->valuedouble == 2000);
        ms = cJSON_GetObjectItemCaseSensitive(base, "models");
        JC_CHECK(cJSON_IsArray(ms) && cJSON_GetArraySize(ms) == 2);
        first = cJSON_GetArrayItem(ms, 0);
        nm = (first != NULL)
             ? cJSON_GetObjectItemCaseSensitive(first, "name") : NULL;
        JC_CHECK(nm != NULL && strcmp(nm->valuestring, "p") == 0);
        JC_CHECK(cJSON_GetObjectItemCaseSensitive(base, "markdown") != NULL);
        JC_CHECK(cJSON_GetObjectItemCaseSensitive(base, "wisdom") != NULL);
        JC_CHECK(cJSON_GetArraySize(
            cJSON_GetObjectItemCaseSensitive(over, "models")) == 1);
        cJSON_Delete(base);
        cJSON_Delete(over);
    }

    /* M284b: the merge's sharp edge, where it meets the M284 selector lint.
     * `models` UNIONS rather than replaces, so a project config that repeats a
     * model already in ~/.jichi yields TWO entries with that name -- and since
     * selectors are substring matches, every selector naming it becomes
     * ambiguous (resolvable, but by array position). This is exactly what the
     * lint found in the zigodot dogfood project, whose local/config.json
     * restated three global models. The docs claimed the project config
     * "shadowed" the global, which the merge test above disproves; this pins the
     * consequence the docs should have described instead. */
    {
        cJSON *base = cJSON_Parse(
            "{\"models\":[{\"name\":\"jlu/qwen3-coder-next\","
            "\"provider\":\"openai\",\"model\":\"jlu/qwen3-coder-next\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\"}]}");
        cJSON *over = cJSON_Parse(
            "{\"models\":[{\"name\":\"jlu/qwen3-coder-next\","
            "\"provider\":\"openai\",\"model\":\"jlu/qwen3-coder-next\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\","
            "\"contextLength\":150000}]}");
        char *merged;
        struct jc_config mc;
        int nm = 0;
        JC_CHECK(base != NULL && over != NULL);
        jc_config_merge_json(base, over);
        merged = cJSON_PrintUnformatted(base);
        JC_CHECK(merged != NULL);
        JC_CHECK(jc_config_load_json(merged, 0, &mc, a) == JC_OK);
        /* Two models, same name -- the duplication is silent. */
        JC_CHECK(jc_config_model_count(&mc) == 2);
        /* So any selector naming it is ambiguous, and the winner is positional
         * (the project's entry, which is why it still "works" in practice). */
        JC_CHECK(jc_config_selector_check(&mc, "jlu/qwen3-coder-next", &nm) ==
                 JC_SEL_AMBIGUOUS);
        JC_CHECK(nm == 2);
        JC_CHECK(jc_config_find_model(&mc, "jlu/qwen3-coder-next") == 0);
        /* A distinct intent name is the documented fix: unambiguous again. */
        JC_CHECK(jc_config_selector_check(&mc, "150000", NULL) == JC_SEL_NONE);
        jc_config_free(&mc);
        free(merged);
        cJSON_Delete(base);
        cJSON_Delete(over);
    }

    /* Inline config (--config-json / jc_config_load_json, M128): the text is
     * parsed as THE config -- a single explicit source, no file merge. */
    {
        struct jc_config ic;
        jc_status st = jc_config_load_json(
            "{\"model\":{\"provider\":\"openai\",\"model\":\"gpt-inline\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"FOO_KEY\"},"
            "\"maxToolIters\":7,\"contextLimit\":4000,"
            "\"language\":\"Japanese\"}", 0, &ic, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(ic.max_tool_iters == 7);
        JC_CHECK((int)ic.context_limit == 4000);
        /* M135: the answer-language key parses; absent it stays NULL (the
         * model's default) -- the earlier cases in this test cover that. */
        JC_CHECK(ic.language != NULL && strcmp(ic.language, "Japanese") == 0);
        /* M142: absent, the out-of-scope auto-revert stays off (opt-in). */
        JC_CHECK(ic.revert_out_of_scope == 0);
        /* M144: absent, the per-child verify gate stays off (opt-in). */
        JC_CHECK(ic.parallel_verify == 0);
        /* M149: absent, the model's tool-calling defaults to native (0). */
        JC_CHECK(ic.model.tool_calling == 0);
        /* M153/M154: privileged-command defaults (ask, audit on). */
        JC_CHECK(ic.privileged_commands == JC_PRIVPOL_ASK);
        JC_CHECK(ic.privileged_audit == 1);
        JC_CHECK(jc_config_model_count(&ic) == 1);
        JC_CHECK(strstr(ic.config_sources, "inline") != NULL);
        jc_config_free(&ic);
    }

    /* M149: toolCalling "none" parses to 1; unknown/reserved reads as native. */
    {
        struct jc_config nc;
        JC_CHECK(jc_config_load_json(
            "{\"model\":{\"provider\":\"openai\",\"model\":\"m\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\","
            "\"toolCalling\":\"none\"}}", 0, &nc, a) == JC_OK);
        JC_CHECK(nc.model.tool_calling == 1);
        jc_config_free(&nc);
        JC_CHECK(jc_config_load_json(
            "{\"model\":{\"provider\":\"openai\",\"model\":\"m\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\","
            "\"toolCalling\":\"text\"}}", 0, &nc, a) == JC_OK);
        JC_CHECK(nc.model.tool_calling == 0); /* reserved -> native today */
        jc_config_free(&nc);
    }

    /* M153/M154: privilegedCommands posture + allowlist + audit toggle. */
    {
        struct jc_config pc;
        JC_CHECK(jc_config_load_json(
            "{\"model\":{\"provider\":\"openai\",\"model\":\"m\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\"},"
            "\"privilegedCommands\":\"deny\","
            "\"privilegedCommandsAllow\":[\"sudo systemctl restart x\"],"
            "\"privilegedAudit\":false}", 0, &pc, a) == JC_OK);
        JC_CHECK(pc.privileged_commands == JC_PRIVPOL_DENY);
        JC_CHECK(pc.privileged_allow.len == 1);
        JC_CHECK(pc.privileged_audit == 0);
        jc_config_free(&pc);
        JC_CHECK(jc_config_load_json(
            "{\"model\":{\"provider\":\"openai\",\"model\":\"m\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\"},"
            "\"privilegedCommands\":\"allow\"}", 0, &pc, a) == JC_OK);
        JC_CHECK(pc.privileged_commands == JC_PRIVPOL_ALLOW);
        jc_config_free(&pc);
    }

    /* Empty input is rejected up front; malformed JSON is a parse error. */
    {
        struct jc_config ic;
        JC_CHECK(jc_config_load_json("", 0, &ic, a) == JC_ERR_INVALID);
        JC_CHECK(jc_config_load_json(NULL, 0, &ic, a) == JC_ERR_INVALID);
        JC_CHECK(jc_config_load_json("{ not json", 0, &ic, a) == JC_ERR_PARSE);
    }

    /* M129: base64-transport pipeline -- encode a config JSON, decode it, and
     * load it (what --config-json-b64 does). Round-trip fidelity + load. */
    {
        const char *json =
            "{\"model\":{\"provider\":\"openai\",\"model\":\"gpt-b64\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"FOO_KEY\"},"
            "\"maxToolIters\":9}";
        jc_size jn = (jc_size)strlen(json);
        jc_size enc_cap = jc_base64_encoded_len(jn) + 1;
        char *enc = (char *)jc_arena_alloc(a, enc_cap);
        JC_CHECK(enc != NULL);
        JC_CHECK(jc_base64_encode((const unsigned char *)json, jn, enc,
                                  enc_cap) == JC_OK);
        {
            jc_size dcap = jc_base64_decoded_len((jc_size)strlen(enc));
            unsigned char *dec = (unsigned char *)jc_arena_alloc(a, dcap + 1);
            jc_size dn = 0;
            struct jc_config ic;
            JC_CHECK(dec != NULL);
            JC_CHECK(jc_base64_decode(enc, dec, dcap, &dn) == JC_OK);
            dec[dn] = '\0';
            JC_CHECK(strcmp((const char *)dec, json) == 0);
            JC_CHECK(jc_config_load_json((const char *)dec, 0, &ic, a) == JC_OK);
            JC_CHECK(ic.max_tool_iters == 9);
            jc_config_free(&ic);
        }
    }

    /* M284: model-selector classification. Agent-profile / command `model:` and
     * the routing tiers all resolve the same way (index, then a name/id
     * substring, then a role), and an unresolvable one used to surface only as a
     * tool error inside a spawned subagent. jc_config_selector_check predicts the
     * verdict at config time so `doctor` can lint it. */
    {
        struct jc_config sc;
        int nm = -1;
        JC_CHECK(jc_config_load_json(
            "{\"models\": ["
            "{\"name\":\"fast\",\"provider\":\"openai\",\"model\":\"gemma-4b\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\","
            "\"roles\":[\"chat\",\"summarize\"]},"
            "{\"name\":\"strong\",\"provider\":\"openai\",\"model\":\"gemma-31b\","
            "\"apiBase\":\"https://x.test\",\"apiKeyEnv\":\"K\"},"
            "{\"name\":\"embedder\",\"provider\":\"openai\","
            "\"model\":\"nomic-embed\",\"apiBase\":\"https://x.test\","
            "\"apiKeyEnv\":\"K\",\"roles\":[\"embed\"]} ] }",
            0, &sc, a) == JC_OK);

        /* Unique name match, and the empty selector (=> the active model). */
        JC_CHECK(jc_config_selector_check(&sc, "strong", &nm) == JC_SEL_OK);
        JC_CHECK(nm == 1);
        JC_CHECK(jc_config_selector_check(&sc, NULL, NULL) == JC_SEL_OK);
        JC_CHECK(jc_config_selector_check(&sc, "", NULL) == JC_SEL_OK);

        /* Ambiguous: "gemma" is a substring of two models' ids. Still resolvable
         * (first wins), so it is a warning -- but it is never what was meant. */
        JC_CHECK(jc_config_selector_check(&sc, "gemma", &nm) ==
                 JC_SEL_AMBIGUOUS);
        JC_CHECK(nm == 2);
        /* The winner is position-dependent, which is exactly the hazard. */
        JC_CHECK(jc_config_find_model(&sc, "gemma") == 0);

        /* A role name is the third resolution step: held => OK, unheld => a
         * distinct verdict (the config is fine, the role is just not staffed). */
        JC_CHECK(jc_config_selector_check(&sc, "summarize", NULL) == JC_SEL_OK);
        JC_CHECK(jc_config_selector_check(&sc, "embed", NULL) == JC_SEL_OK);
        JC_CHECK(jc_config_selector_check(&sc, "rerank", NULL) ==
                 JC_SEL_ROLE_EMPTY);
        JC_CHECK(jc_config_selector_check(&sc, "transcribe", NULL) ==
                 JC_SEL_ROLE_EMPTY);

        /* Nothing at all: neither a name/id substring nor a role. */
        JC_CHECK(jc_config_selector_check(&sc, "gpt-5-mni", &nm) == JC_SEL_NONE);
        JC_CHECK(nm == 0);

        /* All-decimal selectors are 1-based indices, bounds-checked. */
        JC_CHECK(jc_config_selector_check(&sc, "1", NULL) == JC_SEL_OK);
        JC_CHECK(jc_config_selector_check(&sc, "3", NULL) == JC_SEL_OK);
        JC_CHECK(jc_config_selector_check(&sc, "4", NULL) == JC_SEL_NONE);
        JC_CHECK(jc_config_selector_check(&sc, "0", NULL) == JC_SEL_NONE);

        /* The check must agree with the resolver it predicts: every OK verdict
         * resolves, every NONE verdict does not. This is the anti-drift
         * assertion -- the two share sel_all_digits and ci_contains, but the
         * ORDER of the three steps is duplicated logic. */
        JC_CHECK(jc_config_find_model(&sc, "strong") >= 0);
        JC_CHECK(jc_config_find_model(&sc, "gpt-5-mni") < 0);
        JC_CHECK(jc_config_find_by_role(&sc, jc_config_role_flag("rerank")) < 0);

        jc_config_free(&sc);
    }

    jc_arena_free(a);
}
