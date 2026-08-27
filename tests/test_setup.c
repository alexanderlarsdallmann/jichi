/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_setup.c - the setup-wizard pure core (jc_setup): preset table, config
 * builder, start-script builder. Offline; no I/O. */

#include "jc_test.h"
#include "jc_setup.h"
#include "jc_scaffold.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_platform.h"

#include <stdlib.h>
#include <string.h>

static void test_presets(void)
{
    int i;
    int n = jc_setup_preset_count();
    JC_CHECK(n >= 8);
    /* Every preset references a real scaffold pack and has the basics. */
    for (i = 0; i < n; i++) {
        const struct jc_setup_preset *p = jc_setup_preset_at(i);
        if (!JC_REQUIRE(p != NULL)) { continue; }
        JC_CHECK(p->name != NULL && p->name[0] != '\0');
        JC_CHECK(p->description != NULL && p->description[0] != '\0');
        JC_CHECK(jc_scaffold_find_pack(p->scaffold_pack) != NULL);
    }
    /* The named roles all exist. */
    JC_CHECK(jc_setup_find_preset("developer") != NULL);
    JC_CHECK(jc_setup_find_preset("technical-writer") != NULL);
    JC_CHECK(jc_setup_find_preset("tester") != NULL);
    JC_CHECK(jc_setup_find_preset("reviewer") != NULL);
    JC_CHECK(jc_setup_find_preset("generic") != NULL);
    JC_CHECK(jc_setup_find_preset("devops") != NULL);
    JC_CHECK(jc_setup_find_preset("support") != NULL);
    JC_CHECK(jc_setup_find_preset("data") != NULL);
    JC_CHECK(jc_setup_find_preset("small-local") != NULL);
    JC_CHECK(jc_setup_find_preset("learner") != NULL);
    JC_CHECK(jc_setup_find_preset("instructor") != NULL);
    /* The five project journeys (M183). */
    JC_CHECK(jc_setup_find_preset("small-project") != NULL);
    JC_CHECK(jc_setup_find_preset("contributor") != NULL);
    JC_CHECK(jc_setup_find_preset("refactor") != NULL);
    JC_CHECK(jc_setup_find_preset("rewrite") != NULL);
    JC_CHECK(jc_setup_find_preset("architect") != NULL);
    JC_CHECK(jc_setup_find_preset("composer") != NULL);   /* M186 */
    JC_CHECK(jc_setup_find_preset("nope") == NULL);
}

/* M183: the journey presets' recipes -- pack names resolve (covered by the
 * loop in test_presets), the load-bearing fields hold, and the rewrite
 * journey's reference_root answer is emitted as a referenceRoots array. */
static void test_journey_presets(void)
{
    const struct jc_setup_preset *p;
    struct jc_setup_answers a;
    struct jc_sb sb;

    p = jc_setup_find_preset("contributor");
    JC_CHECK(strcmp(p->scaffold_pack, "contributor") == 0);
    JC_CHECK(p->mode != NULL && strcmp(p->mode, "plan") == 0);
    JC_CHECK(p->asks_language == 0); /* a language swap would replace the
                                      * journey pack (the learner rationale) */

    p = jc_setup_find_preset("rewrite");
    JC_CHECK(strcmp(p->scaffold_pack, "rewrite") == 0);
    JC_CHECK(p->asks_language == 0);

    p = jc_setup_find_preset("architect");
    JC_CHECK(strcmp(p->scaffold_pack, "sdlc") == 0);
    JC_CHECK(p->profile == JC_SETUP_PLAN);

    p = jc_setup_find_preset("small-project");
    JC_CHECK(strcmp(p->scaffold_pack, "default") == 0);
    JC_CHECK(p->asks_language == 1); /* the language pack IS the point here */

    /* reference_root -> "referenceRoots":["..."] in the emitted config. */
    jc_setup_answers_init(&a);
    a.provider = "openai";
    a.model = "m";
    a.api_key_env = "K";
    jc_setup_apply_preset(&a, jc_setup_find_preset("rewrite"));
    a.reference_root = "/old/tree";
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_OK);
    JC_CHECK(sb.data != NULL);
    JC_CHECK(strstr(sb.data, "\"referenceRoots\"") != NULL);
    JC_CHECK(strstr(sb.data, "/old/tree") != NULL);
    jc_sb_free(&sb);

    /* Without the answer the key is omitted entirely. */
    jc_setup_answers_init(&a);
    a.provider = "openai";
    a.model = "m";
    a.api_key_env = "K";
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_OK);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "referenceRoots") == NULL);
    jc_sb_free(&sb);
}

/* M150: the small-local preset sets the lean profile + a small context floor. */
static void test_small_local_preset(void)
{
    struct jc_setup_answers a;
    const struct jc_setup_preset *p = jc_setup_find_preset("small-local");

    JC_CHECK(p != NULL);
    jc_setup_answers_init(&a);
    jc_setup_apply_preset(&a, p);
    JC_CHECK(a.low_resource == 1);
    JC_CHECK(a.context_limit == 6000);
    JC_CHECK(a.snapshots == 1);

    /* An explicit context_limit set beforehand is NOT overridden. */
    jc_setup_answers_init(&a);
    a.context_limit = 16384;
    jc_setup_apply_preset(&a, p);
    JC_CHECK(a.context_limit == 16384);
}

/* Curriculum C1: one command from empty directory to a study environment.
 * Before these presets, a student's first contact was `setup` + `init
 * assignments` + a hand-edit of local/config.json to add "assignments": true --
 * and WORKFLOWS.md pointed at a preset that did not exist. */
static void test_teaching_presets(void)
{
    struct jc_setup_answers a;
    const struct jc_setup_preset *lp = jc_setup_find_preset("learner");
    const struct jc_setup_preset *ip = jc_setup_find_preset("instructor");
    struct jc_sb sb;

    JC_CHECK(lp != NULL && ip != NULL);
    if (lp == NULL || ip == NULL) {
        return;
    }
    /* Both scaffold the assignments pack -- and must NOT ask for a language
     * pack, since the swap would replace that pack (the whole point). */
    JC_CHECK_STR(lp->scaffold_pack, "assignments");
    JC_CHECK_STR(ip->scaffold_pack, "assignments");
    JC_CHECK(lp->asks_language == 0);
    JC_CHECK(ip->asks_language == 0);
    /* chat mode: the learner's leash starts short */
    JC_CHECK(lp->mode == NULL);

    jc_setup_answers_init(&a);
    JC_CHECK(a.assignments == -1);          /* tri-state starts unset */
    jc_setup_apply_preset(&a, lp);
    JC_CHECK(a.assignments == 1);
    JC_CHECK(a.snapshots == 1);             /* /undo is lesson one */

    /* The generated config carries the feature switch, so the hint/help tools
     * register on first run without any hand-editing. */
    a.provider = "openai";
    a.model = "some-model";
    a.api_key_env = "LLM_API_KEY";
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_OK);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "\"assignments\":") != NULL &&
             strstr(sb.data, "true") != NULL);
    jc_sb_free(&sb);
}

static void test_apply_preset(void)
{
    struct jc_setup_answers a;
    const struct jc_setup_preset *dev = jc_setup_find_preset("developer");
    const struct jc_setup_preset *rev = jc_setup_find_preset("reviewer");

    jc_setup_answers_init(&a);
    JC_CHECK(a.snapshots == -1 && a.references == -1);
    jc_setup_apply_preset(&a, dev);
    JC_CHECK(a.snapshots == 1);             /* developer enables snapshots  */
    JC_CHECK(a.references == 1);
    JC_CHECK(a.test_command != NULL);       /* JC_SF_TESTCMD => a default    */

    jc_setup_answers_init(&a);
    jc_setup_apply_preset(&a, rev);
    JC_CHECK(a.mode != NULL && strcmp(a.mode, "plan") == 0); /* reviewer=plan */
}

static void test_build_config(void)
{
    struct jc_setup_answers a;
    struct jc_sb sb;
    cJSON *root;
    cJSON *models;
    cJSON *m0;
    cJSON *roles;

    /* Missing required fields => JC_ERR_INVALID. */
    jc_setup_answers_init(&a);
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_ERR_INVALID);
    jc_sb_free(&sb);

    /* A full-ish config round-trips through the JSON parser. */
    jc_setup_answers_init(&a);
    a.provider = "anthropic";
    a.model = "claude-opus-4-8";
    a.api_key_env = "ANTHROPIC_API_KEY";
    a.model_name = "chat";
    a.snapshots = 1;
    a.test_command = "make test";
    a.embed_model = "text-embed-3";
    a.lsp[0].name = "clangd";
    a.lsp[0].command = "clangd";
    a.lsp[0].extensions = "c,h";
    a.nlsp = 1;
    a.docs[0].name = "guide";
    a.docs[0].path = "docs/";
    a.ndocs = 1;

    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_OK);
    root = jc_json_parse(sb.data);
    JC_CHECK(root != NULL);

    /* Secrets are NEVER emitted as a literal key. */
    JC_CHECK(strstr(sb.data, "apiKeyEnv") != NULL);
    JC_CHECK(strstr(sb.data, "\"apiKey\"") == NULL);

    models = jc_json_get_obj(root, "models");
    JC_CHECK(cJSON_IsArray(models) && cJSON_GetArraySize(models) == 2);
    m0 = cJSON_GetArrayItem(models, 0);
    JC_CHECK_STR(jc_json_get_str(m0, "provider", ""), "anthropic");
    JC_CHECK_STR(jc_json_get_str(m0, "model", ""), "claude-opus-4-8");
    JC_CHECK_STR(jc_json_get_str(m0, "apiKeyEnv", ""), "ANTHROPIC_API_KEY");
    roles = jc_json_get_obj(m0, "roles");
    JC_CHECK(cJSON_IsArray(roles) && cJSON_GetArraySize(roles) == 3);

    /* the embed model carries the embed role */
    {
        cJSON *m1 = cJSON_GetArrayItem(models, 1);
        cJSON *er = jc_json_get_obj(m1, "roles");
        JC_CHECK(cJSON_IsArray(er) &&
                 strcmp(cJSON_GetArrayItem(er, 0)->valuestring, "embed") == 0);
    }
    /* top-level + nested subsystems present + well-formed */
    JC_CHECK(jc_json_get_bool(root, "snapshots", 0) == 1);
    JC_CHECK_STR(jc_json_get_str(root, "testCommand", ""), "make test");
    {
        cJSON *lsp = jc_json_get_obj(root, "lspServers");
        cJSON *l0 = cJSON_GetArrayItem(lsp, 0);
        cJSON *ext = jc_json_get_obj(l0, "extensions");
        JC_CHECK(cJSON_IsArray(lsp) && cJSON_GetArraySize(lsp) == 1);
        JC_CHECK_STR(jc_json_get_str(l0, "command", ""), "clangd");
        /* "c,h" split into two extension entries */
        JC_CHECK(cJSON_IsArray(ext) && cJSON_GetArraySize(ext) == 2);
    }
    {
        cJSON *docs = jc_json_get_obj(root, "docs");
        cJSON *d0 = cJSON_GetArrayItem(docs, 0);
        JC_CHECK(cJSON_IsArray(docs) && cJSON_GetArraySize(docs) == 1);
        JC_CHECK_STR(jc_json_get_str(d0, "path", ""), "docs/");
    }
    cJSON_Delete(root);
    jc_sb_free(&sb);
}

static void test_start_script(void)
{
    struct jc_sb sb;
    const struct jc_setup_preset *rev = jc_setup_find_preset("reviewer");
    const struct jc_setup_preset *dev = jc_setup_find_preset("developer");

    /* reviewer => --plan, review.sh */
    JC_CHECK_STR(jc_setup_script_name(rev), "review.sh");
    jc_sb_init(&sb);
    jc_setup_start_script(rev, "local/config.json", &sb);
    JC_CHECK(strncmp(sb.data, "#!/bin/sh", 9) == 0);
    JC_CHECK(strstr(sb.data, "--plan") != NULL);
    JC_CHECK(strstr(sb.data, "local/config.json") != NULL);
    jc_sb_free(&sb);

    /* developer => plain TUI, run.sh, no --plan/--auto */
    JC_CHECK_STR(jc_setup_script_name(dev), "run.sh");
    jc_sb_init(&sb);
    jc_setup_start_script(dev, "local/config.json", &sb);
    JC_CHECK(strstr(sb.data, "--plan") == NULL);
    JC_CHECK(strstr(sb.data, "--auto") == NULL);
    /* M326e: every generated script loads the private key file, guarded so a
     * machine without one still runs. Before this the wizard wrote a config
     * naming an env var and a script that never loaded it, then told the user
     * to export it by hand -- which does nothing for a cron/systemd run. */
    JC_CHECK(strstr(sb.data, ".jichi.env") != NULL);
    JC_CHECK(strstr(sb.data, "[ -f \"$HOME/.jichi.env\" ]") != NULL);
    /* Guarded, not bare: an absent file must not fail the script. */
    JC_CHECK(strstr(sb.data, "&& . \"$HOME/.jichi.env\"") != NULL);
    jc_sb_free(&sb);

    /* ...and the plan-mode script too, not just the default one. */
    jc_sb_init(&sb);
    jc_setup_start_script(rev, "local/config.json", &sb);
    JC_CHECK(strstr(sb.data, ".jichi.env") != NULL);
    jc_sb_free(&sb);

    /* M357: the tester's script arms the envelope -- a token budget plus a
     * periodic verify -- so the M347 budget notice, the M355 flight plan and
     * the default run journal (~/.jichi.d/runs/) all activate on first
     * contact. An unbounded `--auto` script left every one of those dark,
     * and the comment must name the reader (`jichi runs`) or the journal is
     * data nobody meets. */
    {
        const struct jc_setup_preset *tp = jc_setup_find_preset("tester");
        JC_CHECK(tp != NULL);
        JC_CHECK_STR(jc_setup_script_name(tp), "test.sh");
        jc_sb_init(&sb);
        jc_setup_start_script(tp, "local/config.json", &sb);
        /* Pin the EXEC continuation row (leading spaces), not the comment
         * block -- the comment also names both flags, and a first teeth run
         * showed it satisfying a bare strstr while the exec line carried
         * nothing (a script bragging about a budget it does not set). The
         * `jichi runs` pin IS a comment pin, deliberately: naming the
         * journal's reader is the comment's job. */
        JC_CHECK(sb.data != NULL
                 && strstr(sb.data, "  --budget-tokens 400k") != NULL);
        JC_CHECK(sb.data != NULL
                 && strstr(sb.data, "--verify-every 8 ") != NULL);
        JC_CHECK(sb.data != NULL && strstr(sb.data, "jichi runs") != NULL);
        jc_sb_free(&sb);
    }
}

/* M357: the measurement seam. The presets whose work telemetry can explain
 * carry JC_SF_TELEMETRY, the recipe maps it to logging:"metrics", and the
 * emitted config carries the key -- the learning loop (`learn analyze`) mines
 * telemetry, and it was dead on arrival for every setup-created project
 * because the bit existed and NO preset set it. The absences are pinned too:
 * they are choices, not omissions. */
static void test_measurement_presets(void)
{
    static const char *const ARMED[] = {
        "tester", "learner", "instructor",
        "small-project", "refactor", "rewrite", "architect", 0
    };
    static const char *const UNARMED[] = {
        "developer", "generic", "small-local", "constrained", 0
    };
    struct jc_setup_answers a;
    struct jc_sb sb;
    int k;

    for (k = 0; ARMED[k] != 0; k++) {
        const struct jc_setup_preset *p = jc_setup_find_preset(ARMED[k]);
        JC_CHECK(p != NULL);
        if (p == NULL) {
            continue;
        }
        JC_CHECK((p->features & JC_SF_TELEMETRY) != 0u);
        jc_setup_answers_init(&a);
        jc_setup_apply_preset(&a, p);
        JC_CHECK(a.log_level != NULL
                 && strcmp(a.log_level, "metrics") == 0);
    }
    for (k = 0; UNARMED[k] != 0; k++) {
        const struct jc_setup_preset *p = jc_setup_find_preset(UNARMED[k]);
        JC_CHECK(p != NULL && (p->features & JC_SF_TELEMETRY) == 0u);
    }

    /* The recipe reaches the emitted config. */
    jc_setup_answers_init(&a);
    jc_setup_apply_preset(&a, jc_setup_find_preset("tester"));
    a.provider = "openai";
    a.model = "m";
    a.api_key_env = "K";
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_OK);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "\"logging\"") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "\"metrics\"") != NULL);
    jc_sb_free(&sb);

    /* Beginner complexity still narrows: clearing wins over the preset's
     * arming (the explicit-narrowing rule, same as everywhere in jichi). */
    jc_setup_answers_init(&a);
    jc_setup_apply_preset(&a, jc_setup_find_preset("learner"));
    JC_CHECK(a.log_level != NULL);
    jc_setup_apply_complexity(&a, JC_SETUP_BEGINNER);
    JC_CHECK(a.log_level == NULL);
}

static void test_preset_axes(void)
{
    /* M326i: `journey` is a FIELD because it used to be
     * strcmp(p->name, "small-project") in main.c -- the table's ORDER decided
     * which heading a preset appeared under, and nothing said so. A role
     * inserted after that point would have been silently relabelled a journey.
     *
     * So this pins the SET by name, not the count: adding a preset is fine,
     * adding one on the wrong side of the line is what must fail. */
    static const char *const JOURNEYS[] = {
        "small-project", "contributor", "refactor", "rewrite", "architect",
        /* M326m: never roles -- they differ from the entries around them only
         * in mode and start script, i.e. in what you are DOING. */
        "tester", "reviewer", 0
    };
    static const char *const MACHINES[] = { "small-local", "constrained",
                                            "existing-tree", 0 };
    static const char *const STANCES[] = { "learner", "instructor", 0 };
    int n = jc_setup_preset_count();
    int i, k, njourney = 0, nmachine = 0, nstance = 0;

    for (i = 0; i < n; i++) {
        const struct jc_setup_preset *p = jc_setup_preset_at(i);
        int expect = JC_AXIS_ROLE;
        if (!JC_REQUIRE(p != NULL)) { continue; }
        for (k = 0; JOURNEYS[k] != 0; k++) {
            if (strcmp(p->name, JOURNEYS[k]) == 0) {
                expect = JC_AXIS_JOURNEY;
                break;
            }
        }
        for (k = 0; MACHINES[k] != 0; k++) {
            if (strcmp(p->name, MACHINES[k]) == 0) {
                expect = JC_AXIS_MACHINE;
                break;
            }
        }
        for (k = 0; STANCES[k] != 0; k++) {
            if (strcmp(p->name, STANCES[k]) == 0) {
                expect = JC_AXIS_STANCE;
                break;
            }
        }
        JC_CHECK(p->axis == expect);
        njourney += (p->axis == JC_AXIS_JOURNEY);
        nmachine += (p->axis == JC_AXIS_MACHINE);
        nstance += (p->axis == JC_AXIS_STANCE);
    }
    /* Every name in the list resolved to a real preset (a typo above would
     * otherwise make the loop vacuously pass). */
    JC_CHECK(njourney == 7);
    JC_CHECK(nmachine == 3);
    JC_CHECK(nstance == 2);
    for (k = 0; JOURNEYS[k] != 0; k++) {
        JC_CHECK(jc_setup_find_preset(JOURNEYS[k]) != NULL);
    }
    /* Back-compat: `--preset learner` appears in README.md, the curriculum and
     * the presentations; `--preset reviewer` in SDLC.md and WORKFLOWS.md.
     * Changing an entry's AXIS must not change whether its NAME resolves. */
    for (k = 0; STANCES[k] != 0; k++) {
        JC_CHECK(jc_setup_find_preset(STANCES[k]) != NULL);
    }
    /* THE AXES ARE INTERLEAVED, and a menu must not assume otherwise.
     *
     * `small-local` sits in the MIDDLE of the table (M326k moved it to the
     * machine axis without reordering, so the surrounding roles keep their
     * familiar positions). A role menu that skips non-roles therefore has
     * menu-position != array-index, and the old `i = v - 1` would have
     * selected the entry one place too early for every role after it. Each
     * menu walks the table counting its own axis instead.
     *
     * This asserts the fact that makes the walk necessary, so that a future
     * reordering which happens to make the axes contiguous again cannot make
     * a position-based shortcut look safe. */
    {
        int seen_non_role = 0, role_after_non_role = 0;
        for (i = 0; i < n; i++) {
            int ax = jc_setup_preset_at(i)->axis;
            if (ax != JC_AXIS_ROLE) {
                seen_non_role = 1;
            } else if (seen_non_role) {
                role_after_non_role = 1;
            }
        }
        JC_CHECK(role_after_non_role == 1);
    }
}

static void test_role_journey_compose(void)
{
    /* M326j: a journey layers onto a role. There are 12 x 5 = 60 combinations
     * and nobody will exercise them all, so this asserts the PROPERTY that
     * makes them safe rather than enumerating pairs: composition is additive.
     * Applying role-then-journey can only add -- never unset a field the role
     * set, never drop a feature. */
    /* M326m: `reviewer` and `tester` used to be here; they are journeys now,
     * so the role side of this matrix is domains only. */
    static const char *const ROLES[] = { "developer", "technical-writer",
                                         "data", 0 };
    static const char *const JOURNEYS[] = { "rewrite", "reviewer",
                                            "tester", 0 };
    int ri, ji;

    for (ri = 0; ROLES[ri] != 0; ri++) {
        for (ji = 0; JOURNEYS[ji] != 0; ji++) {
            struct jc_setup_answers solo;
            struct jc_setup_answers both;
            const struct jc_setup_preset *r = jc_setup_find_preset(ROLES[ri]);
            const struct jc_setup_preset *j =
                jc_setup_find_preset(JOURNEYS[ji]);
            if (!JC_REQUIRE(r != NULL && j != NULL)) { continue; }
            JC_CHECK(j->axis == JC_AXIS_JOURNEY);
            JC_CHECK(r->axis == JC_AXIS_ROLE);

            jc_setup_answers_init(&solo);
            jc_setup_apply_preset(&solo, r);

            jc_setup_answers_init(&both);
            jc_setup_apply_preset(&both, r);
            jc_setup_apply_preset(&both, j);

            /* Nothing the role established may be lost. */
            if (solo.mode != NULL) JC_CHECK(both.mode != NULL);
            if (solo.test_command != NULL) JC_CHECK(both.test_command != NULL);
            if (solo.verify != NULL) JC_CHECK(both.verify != NULL);
            if (solo.snapshots == 1) JC_CHECK(both.snapshots == 1);
            if (solo.references == 1) JC_CHECK(both.references == 1);
            /* And a journey that asks for a thing the role did not must get
             * it -- otherwise "layering" is a no-op wearing a name. */
            if (j->features & JC_SF_VERIFY) JC_CHECK(both.verify != NULL);
            if (j->features & JC_SF_SNAPSHOTS) JC_CHECK(both.snapshots == 1);
            if (j->features & JC_SF_REFERENCES) JC_CHECK(both.references == 1);
            if (j->mode != NULL) JC_CHECK(both.mode != NULL);
        }
    }

    /* All four axes at once: the stance must still land after three layers.
     * This is the combination the four questions can now express and the old
     * single-preset table could not say at all. */
    {
        struct jc_setup_answers a;
        const struct jc_setup_preset *r = jc_setup_find_preset("developer");
        const struct jc_setup_preset *j = jc_setup_find_preset("refactor");
        const struct jc_setup_preset *m = jc_setup_find_preset("small-local");
        const struct jc_setup_preset *st = jc_setup_find_preset("learner");
        JC_CHECK(r != NULL && j != NULL && m != NULL && st != NULL);
        JC_CHECK(m->axis == JC_AXIS_MACHINE);
        JC_CHECK(st->axis == JC_AXIS_STANCE);
        jc_setup_answers_init(&a);
        jc_setup_apply_preset(&a, r);
        jc_setup_apply_preset(&a, j);
        jc_setup_apply_preset(&a, m);
        jc_setup_apply_preset(&a, st);
        JC_CHECK(a.verify != NULL);          /* from the journey  */
        JC_CHECK(a.low_resource == 1);       /* from the machine  */
        JC_CHECK(a.context_limit == 6000);   /* from the machine  */
        JC_CHECK(a.assignments == 1);        /* from the stance   */
        JC_CHECK(a.snapshots == 1);          /* from the role     */
    }

    /* Back-compat: a journey is still resolvable as a preset on its own, which
     * is what `--preset rewrite` (documented in SDLC.md, exercised by
     * tests/smoke/setup.sh) depends on. */
    {
        struct jc_setup_answers a;
        const struct jc_setup_preset *j = jc_setup_find_preset("rewrite");
        JC_CHECK(j != NULL);
        jc_setup_answers_init(&a);
        jc_setup_apply_preset(&a, j);
        JC_CHECK(a.verify != NULL);
    }
}

static void test_preset_sets(void)
{
    /* M326n: the wizard shows the config keys each choice writes, DERIVED from
     * the feature bitmask so the explanation cannot drift from the behaviour.
     * These assertions are the check on that derivation, not on the prose. */
    char b[256];
    const struct jc_setup_preset *p;

    p = jc_setup_find_preset("small-local");
    jc_setup_preset_sets(p, b, sizeof b);
    JC_CHECK(strstr(b, "lowResource") != NULL);
    JC_CHECK(strstr(b, "contextLimit") != NULL);

    p = jc_setup_find_preset("reviewer");            /* a journey, plan mode */
    jc_setup_preset_sets(p, b, sizeof b);
    JC_CHECK(strstr(b, "\"mode\": \"plan\"") != NULL);
    JC_CHECK(strstr(b, "snapshots") != NULL);

    p = jc_setup_find_preset("learner");
    jc_setup_preset_sets(p, b, sizeof b);
    JC_CHECK(strstr(b, "assignments") != NULL);

    /* generic declares no features: say so rather than printing an empty line
     * the reader has to interpret. */
    p = jc_setup_find_preset("generic");
    jc_setup_preset_sets(p, b, sizeof b);
    JC_CHECK(strstr(b, "nothing beyond") != NULL);

    /* Degenerate inputs never write past the buffer. */
    jc_setup_preset_sets(NULL, b, sizeof b);
    JC_CHECK(b[0] == '\0');
    b[0] = 'x';
    jc_setup_preset_sets(jc_setup_find_preset("developer"), b, 0);
    JC_CHECK(b[0] == 'x');                            /* cap 0 => untouched */
}

static void test_lean_host_vs_small_model(void)
{
    /* M326p: "the machine is small" and "the model is small" are different
     * constraints, and conflating them was the reason one entry could not
     * express the other. Both want the lean profile; only the small-MODEL case
     * wants a small context window, because there it is the model that cannot
     * use a big one. A hosted 200k model on a Raspberry Pi must keep its
     * window. */
    struct jc_setup_answers host;
    struct jc_setup_answers model;

    jc_setup_answers_init(&host);
    jc_setup_apply_preset(&host, jc_setup_find_preset("constrained"));
    jc_setup_answers_init(&model);
    jc_setup_apply_preset(&model, jc_setup_find_preset("small-local"));

    JC_CHECK(host.low_resource == 1);       /* both go lean ... */
    JC_CHECK(model.low_resource == 1);
    JC_CHECK(host.context_limit == 0);      /* ... only the model shrinks the */
    JC_CHECK(model.context_limit == 6000);  /*     window                     */
    JC_CHECK(host.max_parallel == 1);       /* the host-side cost that matters */

    /* existing-tree reaches machinery neither of the others does. */
    {
        struct jc_setup_answers t;
        const struct jc_setup_preset *p = jc_setup_find_preset("existing-tree");
        if (JC_REQUIRE(p != NULL)) {
            JC_CHECK(p->axis == JC_AXIS_MACHINE);
        }
        jc_setup_answers_init(&t);
        jc_setup_apply_preset(&t, p);
        JC_CHECK(t.references == 1);
    }
}

static void test_sound_notify_emit(void)
{
    /* M326q: OS-appropriate commands reach the config when the user opts in,
     * and are absent when they do not -- a `sound` key registers the
     * play_audio/record_audio tools, so it must never appear by accident. */
    struct jc_setup_answers a;
    struct jc_sb sb;
    cJSON *root;

    jc_setup_answers_init(&a);
    a.provider = "openai";
    a.model = "gpt-4o";
    a.api_key_env = "K";
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_OK);
    JC_CHECK(strstr(sb.data, "\"sound\"") == NULL);
    JC_CHECK(strstr(sb.data, "\"notify\"") == NULL);
    jc_sb_free(&sb);

    a.sound_play = "afplay";
    a.notify_cmd = "osascript -e 'x'";
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_build_config(&a, &sb) == JC_OK);
    root = jc_json_parse(sb.data);
    JC_CHECK(root != NULL);
    if (root != NULL) {
        cJSON *snd = jc_json_get_obj(root, "sound");
        JC_CHECK(snd != NULL);
        JC_CHECK_STR(jc_json_get_str(snd, "play", ""), "afplay");
        JC_CHECK_STR(jc_json_get_str(root, "notify", ""), "osascript -e 'x'");
        cJSON_Delete(root);
    }
    jc_sb_free(&sb);
}

static void test_merge_config(void)
{
    struct jc_setup_answers a;
    struct jc_sb sb;
    cJSON *root;
    cJSON *models;

    jc_setup_answers_init(&a);
    a.provider = "openai";
    a.model = "gpt-4o";
    a.model_name = "chat";
    a.api_key_env = "OPENAI_API_KEY";
    a.references = 1;
    a.mode = "auto";

    /* An existing config with a hand-set mode and its own chat model: the merge
     * must keep both and only add the missing key (references). */
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_merge_config(
        "{\"mode\":\"plan\",\"models\":[{\"name\":\"mine\","
        "\"model\":\"local\",\"roles\":[\"chat\"]}]}", &a, &sb) == JC_OK);
    root = jc_json_parse(sb.data);
    JC_CHECK(root != NULL);
    JC_CHECK_STR(jc_json_get_str(root, "mode", ""), "plan"); /* kept, not "auto" */
    models = cJSON_GetObjectItem(root, "models");
    JC_CHECK(cJSON_GetArraySize(models) == 1); /* no duplicate chat model added */
    JC_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "references"))); /* added */
    cJSON_Delete(root);
    jc_sb_free(&sb);

    /* A config with models but no chat role: the chat model is added. */
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_merge_config(
        "{\"models\":[{\"model\":\"e\",\"roles\":[\"embed\"]}]}", &a, &sb)
        == JC_OK);
    root = jc_json_parse(sb.data);
    models = cJSON_GetObjectItem(root, "models");
    JC_CHECK(cJSON_GetArraySize(models) == 2); /* embed kept + chat added */
    cJSON_Delete(root);
    jc_sb_free(&sb);

    /* Malformed existing config => JC_ERR_PARSE. */
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_merge_config("not json", &a, &sb) == JC_ERR_PARSE);
    jc_sb_free(&sb);

    /* Missing required answer fields => JC_ERR_INVALID. */
    jc_setup_answers_init(&a);
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_merge_config("{}", &a, &sb) == JC_ERR_INVALID);
    jc_sb_free(&sb);
}

static void test_inherit_config(void)
{
    struct jc_setup_answers a;
    struct jc_sb sb;
    cJSON *root, *models;
    const char *GLOBAL =
        "{\"models\":[{\"name\":\"g\",\"model\":\"gpt\",\"roles\":[\"chat\"]}],"
        "\"routing\":{\"enabled\":true,\"fast\":\"g\"},"
        "\"snapshots\":true}";

    /* Pure inheritance: no --model needed; the whole global config carries over,
     * and only-missing answers are gap-filled. */
    jc_setup_answers_init(&a);
    a.mode = "plan";
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_inherit_config(GLOBAL, NULL, &a, &sb) == JC_OK);
    root = jc_json_parse(sb.data);
    JC_CHECK(root != NULL);
    models = cJSON_GetObjectItem(root, "models");
    JC_CHECK(cJSON_GetArraySize(models) == 1);              /* global model kept */
    JC_CHECK(cJSON_GetObjectItem(root, "routing") != NULL); /* inherited */
    JC_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "snapshots")));
    JC_CHECK_STR(jc_json_get_str(root, "mode", ""), "plan"); /* gap-filled */
    cJSON_Delete(root);
    jc_sb_free(&sb);

    /* Subset: --inherit "models" copies only the models key. */
    jc_setup_answers_init(&a);
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_inherit_config(GLOBAL, "models", &a, &sb) == JC_OK);
    root = jc_json_parse(sb.data);
    JC_CHECK(cJSON_GetObjectItem(root, "models") != NULL);
    JC_CHECK(cJSON_GetObjectItem(root, "routing") == NULL); /* not selected */
    JC_CHECK(cJSON_GetObjectItem(root, "snapshots") == NULL);
    cJSON_Delete(root);
    jc_sb_free(&sb);

    /* Non-object source => JC_ERR_PARSE. */
    jc_sb_init(&sb);
    JC_CHECK(jc_setup_inherit_config("not json", NULL, &a, &sb) == JC_ERR_PARSE);
    jc_sb_free(&sb);
}

static void test_lang_for_ext(void)
{
    JC_CHECK_STR(jc_setup_lang_for_ext("c"), "c-cli");
    JC_CHECK_STR(jc_setup_lang_for_ext("H"), "c-cli");     /* case-insensitive */
    JC_CHECK_STR(jc_setup_lang_for_ext("cpp"), "c-cli");
    JC_CHECK_STR(jc_setup_lang_for_ext("py"), "python-cli");
    JC_CHECK_STR(jc_setup_lang_for_ext("zig"), "zig-cli");
    JC_CHECK_STR(jc_setup_lang_for_ext("gd"), "godot");
    JC_CHECK(jc_setup_lang_for_ext("txt") == NULL);
    JC_CHECK(jc_setup_lang_for_ext("") == NULL);
    JC_CHECK(jc_setup_lang_for_ext(NULL) == NULL);
}

static void test_complexity(void)
{
    struct jc_setup_answers a;
    int c = -1;

    /* parse */
    JC_CHECK(jc_setup_complexity_parse("beginner", &c) == 1 &&
             c == JC_SETUP_BEGINNER);
    JC_CHECK(jc_setup_complexity_parse("advanced", &c) == 1 &&
             c == JC_SETUP_ADVANCED);
    JC_CHECK(jc_setup_complexity_parse("std", &c) == 1 && c == JC_SETUP_STD);
    JC_CHECK(jc_setup_complexity_parse("nonsense", &c) == 0);

    /* beginner: safety on, power-user surface off, not autonomous */
    memset(&a, 0, sizeof a);
    a.hooks = 1; a.log_level = "full"; a.route_fast = "x";
    a.route_strong = "y"; a.mode = "auto"; a.search_url = "http://x";
    jc_setup_apply_complexity(&a, JC_SETUP_BEGINNER);
    JC_CHECK(a.snapshots == 1);
    JC_CHECK(a.references == 1);
    JC_CHECK(a.hooks == 0);
    JC_CHECK(a.log_level == NULL);
    JC_CHECK(a.route_fast == NULL && a.route_strong == NULL);
    JC_CHECK(a.search_url == NULL);
    JC_CHECK(a.mode == NULL); /* auto downgraded */

    /* advanced: everything sensible on */
    memset(&a, 0, sizeof a);
    a.test_command = "make test";
    jc_setup_apply_complexity(&a, JC_SETUP_ADVANCED);
    JC_CHECK(a.snapshots == 1 && a.references == 1 && a.hooks == 1);
    JC_CHECK(a.log_level != NULL);
    JC_CHECK(a.verify != NULL); /* derived from test_command */

    /* std: no-op */
    memset(&a, 0, sizeof a);
    jc_setup_apply_complexity(&a, JC_SETUP_STD);
    JC_CHECK(a.snapshots == 0 && a.hooks == 0);
}

void test_setup(void)
{
    test_presets();
    test_apply_preset();
    test_small_local_preset();
    test_teaching_presets();
    test_journey_presets();
    test_complexity();
    test_build_config();
    test_merge_config();
    test_inherit_config();
    test_start_script();
    test_measurement_presets();
    test_preset_axes();
    test_role_journey_compose();
    test_preset_sets();
    test_lean_host_vs_small_model();
    test_sound_notify_emit();
    test_lang_for_ext();

    /* #10: resource-tier thresholds (centralizes the old bare mb<1024). */
    {
        JC_CHECK(jc_resource_tier(0UL, 8) == JC_RES_NORMAL);   /* unknown */
        JC_CHECK(jc_resource_tier(256UL, 1) == JC_RES_MINIMAL);
        JC_CHECK(jc_resource_tier(512UL, 2) == JC_RES_LITE);
        JC_CHECK(jc_resource_tier(1023UL, 4) == JC_RES_LITE);
        JC_CHECK(jc_resource_tier(1024UL, 4) == JC_RES_NORMAL);
        JC_CHECK(jc_resource_tier(8192UL, 8) == JC_RES_NORMAL);
    }
}
