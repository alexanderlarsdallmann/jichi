/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_routing.c - tiered model routing: config parse + resolve predicate
 * (jc_config_routing_resolve) and the jc_app_route_to switch. The mid-turn
 * escalation wiring is verified end-to-end with a live model. */

#include "jc_test.h"
#include "jc_config.h"
#include "jc_app.h"
#include "jc_provider.h"
#include "jc_mem.h"
#include "jc_platform.h"

#include <stdio.h>
#include <string.h>

/* Write a two-model config (+ optional routing block) and load it. */
static void write_cfg(const char *path, const char *routing_json)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    fputs("{ \"models\": ["
          "{\"provider\":\"openai\",\"name\":\"fastone\","
          "\"model\":\"gpt-4o-mini\",\"apiKey\":\"k\"},"
          "{\"provider\":\"openai\",\"name\":\"strongone\","
          "\"model\":\"gpt-4o\",\"apiKey\":\"k\"}]", f);
    if (routing_json != NULL) {
        fputs(", ", f);
        fputs(routing_json, f);
    }
    fputs(" }", f);
    fclose(f);
}

static void test_parse_and_resolve(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config cfg;
    const char *path = jc_test_tmp("jichi_test_routing.json");
    int f = -1, s = -1;

    write_cfg(path, "\"routing\": {\"enabled\":true,"
              "\"fast\":\"fastone\",\"strong\":\"strongone\"}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);

    /* Parsed fields + defaults. */
    JC_CHECK(cfg.routing.enabled == 1);
    JC_CHECK_STR(cfg.routing.fast, "fastone");
    JC_CHECK_STR(cfg.routing.strong, "strongone");
    JC_CHECK(cfg.routing.escalate_on_verify == 1); /* default */
    JC_CHECK(cfg.routing.escalate_on_error == 0);  /* default */
    JC_CHECK(cfg.routing.escalate_on_stall == 1);  /* default on (M23a) */

    /* escalateOnStall is configurable and defaults on when the block omits it. */
    jc_config_free(&cfg);
    write_cfg(path, "\"routing\": {\"fast\":\"fastone\",\"strong\":"
              "\"strongone\",\"escalateOnStall\":false}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.escalate_on_stall == 0); /* explicit off honoured */

    /* Resolves to the two distinct indices. */
    JC_CHECK(jc_config_routing_resolve(&cfg, &f, &s) == 1);
    JC_CHECK(f == 0 && s == 1);

    /* Same selector for both => not distinct => inert. */
    cfg.routing.strong = cfg.routing.fast;
    JC_CHECK(jc_config_routing_resolve(&cfg, &f, &s) == 0);

    /* Disabled => inert regardless of selectors. */
    jc_config_free(&cfg);
    write_cfg(path, "\"routing\": {\"enabled\":false,"
              "\"fast\":\"fastone\",\"strong\":\"strongone\"}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.enabled == 0);
    JC_CHECK(jc_config_routing_resolve(&cfg, &f, &s) == 0);

    /* Unresolvable selector => inert. */
    jc_config_free(&cfg);
    write_cfg(path, "\"routing\": {\"fast\":\"nope\",\"strong\":\"strongone\"}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.enabled == 1); /* default on */
    JC_CHECK(jc_config_routing_resolve(&cfg, &f, &s) == 0);

    /* No routing block => inert (no selectors). */
    jc_config_free(&cfg);
    write_cfg(path, NULL);
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(jc_config_routing_resolve(&cfg, &f, &s) == 0);

    jc_config_free(&cfg);
    remove(path);
    jc_arena_free(a);
}

/* Integration: jc_app_route_to actually switches the active model + provider. */
static void test_route_to(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    const char *path = jc_test_tmp("jichi_test_routing2.json");

    memset(&app, 0, sizeof(app));
    app.arena = a;
    app.quiet = 1;

    write_cfg(path, NULL);
    JC_CHECK(jc_config_load(path, 0, &app.config, a) == JC_OK);
    app.provider = jc_provider_create(&app.config.model);
    JC_CHECK(app.provider != NULL);
    JC_CHECK(app.config.active == 0);

    /* Switch to the strong (index 1) model. */
    JC_CHECK(jc_app_route_to(&app, 1, "test") == JC_OK);
    JC_CHECK(app.config.active == 1);
    JC_CHECK_STR(app.config.model.model, "gpt-4o");

    /* Re-routing to the current model is a no-op. */
    JC_CHECK(jc_app_route_to(&app, 1, "test") == JC_OK);
    JC_CHECK(app.config.active == 1);

    if (app.provider != NULL) {
        app.provider->vt->free(app.provider);
    }
    jc_config_free(&app.config);
    remove(path);
    jc_arena_free(a);
}

/* M288: context-pressure escalation. The other three triggers react to a
 * failure; this one reacts to running out of room, which is the reason a
 * wide-window strong tier exists -- and its absence is why one project logged
 * routes=0 across 174 turns. */
static void test_context_escalate(void)
{
    /* Below the threshold: no escalation. 74% of 100000 with pct=75. */
    JC_CHECK(jc_config_context_escalate(74000, 100000, 200000, 75) == 0);
    /* At and above it: escalate. The boundary is inclusive. */
    JC_CHECK(jc_config_context_escalate(75000, 100000, 200000, 75) == 1);
    JC_CHECK(jc_config_context_escalate(99000, 100000, 200000, 75) == 1);
    /* Past the fast window entirely: certainly escalate. */
    JC_CHECK(jc_config_context_escalate(150000, 100000, 200000, 75) == 1);

    /* pct = 0 disables the trigger outright, however dire the pressure. */
    JC_CHECK(jc_config_context_escalate(999000, 100000, 200000, 0) == 0);

    /* The strong tier must be strictly ROOMIER -- escalating for room you do
     * not gain is pure cost, and this is the guard that makes the trigger
     * inert when a global `contextLimit` flattens both tiers to one number
     * (measured in the wild: 150k fast, 256k strong, contextLimit 128000). */
    JC_CHECK(jc_config_context_escalate(90000, 100000, 100000, 75) == 0);
    JC_CHECK(jc_config_context_escalate(90000, 100000, 50000, 75) == 0);
    /* The measured config, with an estimate well past 75% of 128000 (=96000) so
     * this case fails loudly if the roomier-tier guard is ever dropped -- the
     * point being that jichi must NOT escalate here, because the user's explicit
     * global budget has made both tiers equally roomy. */
    JC_CHECK(jc_config_context_escalate(120000, 128000, 128000, 75) == 0);
    /* Same pressure, but with the global budget removed so the declared windows
     * apply: now escalating genuinely buys room, so it fires. */
    JC_CHECK(jc_config_context_escalate(120000, 150000, 256000, 75) == 1);

    /* An unknown window on either side: refuse rather than guess. */
    JC_CHECK(jc_config_context_escalate(90000, 0, 200000, 75) == 0);
    JC_CHECK(jc_config_context_escalate(90000, 100000, 0, 75) == 0);

    /* A custom threshold is honoured in both directions. */
    JC_CHECK(jc_config_context_escalate(50000, 100000, 200000, 50) == 1);
    JC_CHECK(jc_config_context_escalate(50000, 100000, 200000, 60) == 0);

    /* The default sits BELOW the compaction trigger's 80%, so a roomier model
     * is preferred over discarding history -- the whole point of the ordering. */
    JC_CHECK(JC_ROUTE_CONTEXT_PCT < 80);
    JC_CHECK(jc_config_context_escalate(76000, 100000, 200000,
                                        JC_ROUTE_CONTEXT_PCT) == 1);
    JC_CHECK(jc_config_context_escalate(70000, 100000, 200000,
                                        JC_ROUTE_CONTEXT_PCT) == 0);
}

/* M298: the counterpart M288 never had. Escalation-on-context reacts to running
 * out of room; mid-turn compaction can give the room back, and before M298 nothing
 * ever went back down -- a long --auto run that escalated once stayed on the strong
 * tier for every remaining iteration. The trigger and the remedy did not speak. */
static void test_context_deescalate(void)
{
    /* THE GAP IS THE DESIGN. Escalation fires at 75%; de-escalation must not fire
     * at 74%, or the pair oscillates escalate -> compact -> de-escalate ->
     * escalate, each switch rebuilding the provider and dropping the cached
     * prefix. With the default gap of 20 the way down is 55%. */
    JC_CHECK(jc_config_context_deescalate(74000, 100000, 75) == 0);
    JC_CHECK(jc_config_context_deescalate(60000, 100000, 75) == 0);
    JC_CHECK(jc_config_context_deescalate(56000, 100000, 75) == 0);
    /* At and below the way down: come back. The boundary is inclusive, mirroring
     * the escalate side. */
    JC_CHECK(jc_config_context_deescalate(55000, 100000, 75) == 1);
    JC_CHECK(jc_config_context_deescalate(10000, 100000, 75) == 1);
    JC_CHECK(jc_config_context_deescalate(0, 100000, 75) == 1);

    /* There must be NO overlap: nothing may satisfy both predicates, or a single
     * estimate would both escalate and de-escalate. Walk the whole band. */
    {
        long est;
        for (est = 0; est <= 100000; est += 1000) {
            int up = jc_config_context_escalate(est, 100000, 200000, 75);
            int down = jc_config_context_deescalate(est, 100000, 75);
            JC_CHECK(!(up && down));
        }
    }

    /* Disabled trigger: never come back down either, so a user who turned the
     * feature off gets no routing churn from it in either direction. */
    JC_CHECK(jc_config_context_deescalate(1000, 100000, 0) == 0);

    /* An unknown window: refuse rather than guess (as the escalate side does). */
    JC_CHECK(jc_config_context_deescalate(1000, 0, 75) == 0);

    /* A threshold at or below the gap leaves no room for a way down, so the
     * feature disables itself rather than de-escalating at a negative percentage. */
    JC_CHECK(jc_config_context_deescalate(0, 100000, JC_ROUTE_DEESCALATE_GAP) == 0);
    JC_CHECK(jc_config_context_deescalate(0, 100000, 5) == 0);

    /* The gap is real, not zero -- if it were, this whole test would be about a
     * threshold rather than hysteresis. */
    JC_CHECK(JC_ROUTE_DEESCALATE_GAP > 0);
    JC_CHECK(JC_ROUTE_DEESCALATE_GAP < JC_ROUTE_CONTEXT_PCT);
}

/* The config surface: a percentage, or a bool as a convenience. */
static void test_context_config(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config cfg;
    const char *path = jc_test_tmp("jichi_test_routing_ctx.json");

    /* Default when the routing block says nothing about it. */
    write_cfg(path, "\"routing\": {\"fast\":\"fastone\",\"strong\":\"strongone\"}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.escalate_on_context == JC_ROUTE_CONTEXT_PCT);
    jc_config_free(&cfg);

    /* An explicit percentage. */
    write_cfg(path, "\"routing\": {\"fast\":\"fastone\",\"strong\":\"strongone\","
                    "\"escalateOnContext\": 60}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.escalate_on_context == 60);
    jc_config_free(&cfg);

    /* `false` disables; `true` selects the default threshold. */
    write_cfg(path, "\"routing\": {\"fast\":\"fastone\",\"strong\":\"strongone\","
                    "\"escalateOnContext\": false}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.escalate_on_context == 0);
    jc_config_free(&cfg);

    write_cfg(path, "\"routing\": {\"fast\":\"fastone\",\"strong\":\"strongone\","
                    "\"escalateOnContext\": true}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.escalate_on_context == JC_ROUTE_CONTEXT_PCT);
    jc_config_free(&cfg);

    /* A stringified number, as a model or a hand-edited config may spell it. */
    write_cfg(path, "\"routing\": {\"fast\":\"fastone\",\"strong\":\"strongone\","
                    "\"escalateOnContext\": \"60\"}");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.routing.escalate_on_context == 60);
    jc_config_free(&cfg);

    remove(path);
    jc_arena_free(a);
}

void test_routing(void)
{
    test_parse_and_resolve();
    test_route_to();
    test_context_escalate();
    test_context_deescalate();
    test_context_config();
}
