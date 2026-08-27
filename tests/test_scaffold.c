/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_scaffold.c - the pure scaffolding core (jc_scaffold.c).
 *
 * Verifies the destination-path logic, the pack table, and that every shipped
 * default-pack asset parses as the loaders expect (valid frontmatter, required
 * fields), so `init` can't ship a file the discovery layer would reject.
 */

#include "jc_test.h"
#include "jc_scaffold.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_json.h"
#include "cJSON.h"

#include <string.h>

static void test_dest(void)
{
    char buf[256];
    int n;

    /* Project mode: a top-level file lands at the project root as-is. */
    n = jc_scaffold_dest("AGENTS.md", 0, NULL, buf, sizeof(buf));
    JC_CHECK(n > 0);
    JC_CHECK(strcmp(buf, "AGENTS.md") == 0);

    /* Project mode: nested files land under .jichi/. */
    n = jc_scaffold_dest("agents/reviewer.md", 0, NULL, buf, sizeof(buf));
    JC_CHECK(n > 0);
    JC_CHECK(strcmp(buf, ".jichi/agents/reviewer.md") == 0);

    n = jc_scaffold_dest("skills/commit-message/SKILL.md", 0, NULL,
                         buf, sizeof(buf));
    JC_CHECK(strcmp(buf, ".jichi/skills/commit-message/SKILL.md") == 0);

    /* glossary.md is top-level in the pack but nests in project mode -- the
     * glossary loader reads <ws>/.jichi/glossary.md (M175); globally it lands
     * at the loader's other path, ~/.config/jichi/glossary.md. */
    n = jc_scaffold_dest("glossary.md", 0, NULL, buf, sizeof(buf));
    JC_CHECK(n > 0);
    JC_CHECK(strcmp(buf, ".jichi/glossary.md") == 0);
    n = jc_scaffold_dest("glossary.md", 1, "/home/u", buf, sizeof(buf));
    JC_CHECK(strcmp(buf, "/home/u/.config/jichi/glossary.md") == 0);

    /* Global mode: everything under <home>/.config/jichi/, including the
     * top-level rules file. */
    n = jc_scaffold_dest("AGENTS.md", 1, "/home/u", buf, sizeof(buf));
    JC_CHECK(strcmp(buf, "/home/u/.config/jichi/AGENTS.md") == 0);

    n = jc_scaffold_dest("agents/reviewer.md", 1, "/home/u", buf, sizeof(buf));
    JC_CHECK(strcmp(buf, "/home/u/.config/jichi/agents/reviewer.md")
             == 0);

    /* Truncation is reported, not silently clipped. */
    n = jc_scaffold_dest("agents/reviewer.md", 0, NULL, buf, 8);
    JC_CHECK(n < 0);

    /* NULL relpath is rejected. */
    JC_CHECK(jc_scaffold_dest(NULL, 0, NULL, buf, sizeof(buf)) < 0);
}

static void test_pack_table(void)
{
    const struct jc_scaffold_pack *p;
    int i;

    JC_CHECK(jc_scaffold_pack_count() >= 1);
    JC_CHECK(jc_scaffold_pack_at(-1) == NULL);
    JC_CHECK(jc_scaffold_pack_at(jc_scaffold_pack_count()) == NULL);

    JC_CHECK(jc_scaffold_find_pack("nope") == NULL);
    JC_CHECK(jc_scaffold_find_pack(NULL) == NULL);

    p = jc_scaffold_find_pack("default");
    if (JC_REQUIRE(p != NULL)) {
        JC_CHECK(p->nfiles > 0);
        JC_CHECK(p->description != NULL && p->description[0] != '\0');
    }

    /* The M12 archetype packs exist. */
    JC_CHECK(jc_scaffold_find_pack("c-cli") != NULL);
    JC_CHECK(jc_scaffold_find_pack("zig-cli") != NULL);
    JC_CHECK(jc_scaffold_find_pack("python-cli") != NULL);
    JC_CHECK(jc_scaffold_find_pack("cpp") != NULL);
    JC_CHECK(jc_scaffold_find_pack("perl") != NULL);
    JC_CHECK(jc_scaffold_find_pack("r") != NULL);
    JC_CHECK(jc_scaffold_find_pack("guile") != NULL);
    JC_CHECK(jc_scaffold_find_pack("racket") != NULL);
    JC_CHECK(jc_scaffold_find_pack("clojure") != NULL);
    JC_CHECK(jc_scaffold_find_pack("haskell") != NULL);
    JC_CHECK(jc_scaffold_find_pack("elixir") != NULL);
    JC_CHECK(jc_scaffold_find_pack("erlang") != NULL);
    JC_CHECK(jc_scaffold_find_pack("elisp") != NULL);
    JC_CHECK(jc_scaffold_find_pack("godot") != NULL);
    JC_CHECK(jc_scaffold_find_pack("docs") != NULL);
    JC_CHECK(jc_scaffold_find_pack("systems-analysis") != NULL);
    JC_CHECK(jc_scaffold_find_pack("log-analysis") != NULL); /* M151 */
    JC_CHECK(jc_scaffold_find_pack("sysadmin") != NULL);     /* M151 */
    JC_CHECK(jc_scaffold_find_pack("sdlc") != NULL);         /* M183 */
    JC_CHECK(jc_scaffold_find_pack("contributor") != NULL);  /* M183 */
    JC_CHECK(jc_scaffold_find_pack("refactor") != NULL);     /* M183 */
    JC_CHECK(jc_scaffold_find_pack("rewrite") != NULL);      /* M183 */
    JC_CHECK(jc_scaffold_find_pack("music") != NULL);        /* M186 */

    /* Every pack: a description and well-formed files (relpath without a leading
     * slash, at least one content chunk). */
    {
        int k;
        for (k = 0; k < jc_scaffold_pack_count(); k++) {
            const struct jc_scaffold_pack *q = jc_scaffold_pack_at(k);
            JC_CHECK(q->name != NULL && q->name[0] != '\0');
            JC_CHECK(q->description != NULL && q->description[0] != '\0');
            JC_CHECK(q->nfiles > 0);
            for (i = 0; i < q->nfiles; i++) {
                const struct jc_scaffold_file *f = &q->files[i];
                JC_CHECK(f->relpath != NULL && f->relpath[0] != '\0');
                JC_CHECK(f->relpath[0] != '/');
                JC_CHECK(f->lines != NULL && f->lines[0] != NULL);
            }
        }
    }
}

/* The shipped .md assets must parse the way the agent/skill/command loaders
 * expect: a `---` frontmatter block with the right key for their kind. */
static void test_assets_parse(void)
{
    struct jc_arena *a = jc_arena_new(0);
    int n_agents = 0, n_skills = 0, n_cmds = 0;
    int saw_investigate = 0;
    int n_priced_cfg = 0;
    int k;

    /* Across EVERY pack, each shipped .md must parse the way its loader expects
     * (valid frontmatter, the right required field). This guards init from ever
     * writing a file the discovery layer would reject. */
    for (k = 0; k < jc_scaffold_pack_count(); k++) {
        const struct jc_scaffold_pack *p = jc_scaffold_pack_at(k);
        int i;
        for (i = 0; i < p->nfiles; i++) {
            const struct jc_scaffold_file *f = &p->files[i];
            struct jc_md_doc doc;
            struct jc_sb sb;
            const char *text;
            int is_agent = (strncmp(f->relpath, "agents/", 7) == 0);
            int is_skill = (strncmp(f->relpath, "skills/", 7) == 0);
            int is_cmd = (strncmp(f->relpath, "commands/", 9) == 0);
            jc_size rl = (jc_size)strlen(f->relpath);
            int is_json = (rl > 5 && strcmp(f->relpath + rl - 5, ".json") == 0);

            jc_sb_init(&sb);
            jc_scaffold_file_text(f, &sb);
            text = sb.data != NULL ? sb.data : "";
            JC_CHECK(text[0] != '\0');

            if (is_json) {
                /* shipped config.example.json must be valid JSON */
                cJSON *j = jc_json_parse(text);
                JC_CHECK(j != NULL);
                /* M357: EVERY model entry in an example config must carry the
                 * pricing keys (0 = unpriced, honest for the local endpoints
                 * these examples point at) -- the example is where a user
                 * learns the key EXISTS; without it, telemetry and /cost
                 * render zeros that look like data. Per MODEL, structurally:
                 * the first draft was a file-level strstr, and its own teeth
                 * run passed with one model unpriced because the other still
                 * named the keys. Counted, then asserted after the loops (the
                 * M356 presence-flag rule). */
                if (j != NULL) {
                    cJSON *ms = cJSON_GetObjectItem(j, "models");
                    if (ms != NULL) {
                        int nm = cJSON_GetArraySize(ms);
                        int mi;
                        n_priced_cfg++;
                        JC_CHECK(nm > 0);
                        for (mi = 0; mi < nm; mi++) {
                            cJSON *m = cJSON_GetArrayItem(ms, mi);
                            JC_CHECK(m != NULL && cJSON_GetObjectItem(
                                         m, "inputCostPer1M") != NULL);
                            JC_CHECK(m != NULL && cJSON_GetObjectItem(
                                         m, "outputCostPer1M") != NULL);
                        }
                    }
                }
                cJSON_Delete(j);
            }
            if (is_agent || is_skill || is_cmd) {
                JC_CHECK(jc_md_parse(text, a, &doc) == JC_OK);
                JC_CHECK(doc.front != NULL); /* has frontmatter */
                JC_CHECK(jc_yaml_get_str(doc.front, "description", NULL)
                         != NULL);
                if (is_agent) {
                    JC_CHECK(doc.body != NULL && doc.body[0] != '\0');
                    n_agents++;
                } else if (is_skill) {
                    JC_CHECK(jc_yaml_get_str(doc.front, "name", NULL) != NULL);
                    n_skills++;
                } else {
                    JC_CHECK(doc.body != NULL && doc.body[0] != '\0');
                    n_cmds++;
                }
                jc_md_free(&doc); /* frees the frontmatter's heap vectors */
            }
            /* M75: the /learn command must resolve the binary
             * invocation-agnostic -- prefer the running binary's own path
             * ($JICHI_BIN, exported by main), then ./jichi, then a bare
             * jichi on PATH -- so mining telemetry never fails with a
             * `jichi: not found` the mentor would then "learn" from. */
            if (strcmp(f->relpath, "commands/learn.md") == 0) {
                JC_CHECK(strstr(text, "JICHI_BIN") != NULL);
                JC_CHECK(strstr(text, "./jichi") != NULL);
                JC_CHECK(strstr(text, "--workspace") != NULL);
                JC_CHECK(strstr(text, "!`jichi learn analyze`") == NULL);
            }
            /* M356: the worked orchestration example. Its load-bearing lines:
             * it must teach the fan-out shape (spawn_parallel, independent
             * angles), the fresh-context rule (a sub-agent sees only what
             * you write), and the counter-case (one angle: no fan-out) --
             * a demonstration that omits when NOT to delegate teaches
             * over-delegation. The presence flag is asserted after the
             * loops: these conditional checks VANISH with the file, so
             * without it, removing the pack entry went green at a lower
             * count -- the M310 artifact-must-exist rule, applied to this
             * test's own first draft, which was watched not-failing. */
            if (strcmp(f->relpath, "commands/investigate.md") == 0) {
                saw_investigate++;
                JC_CHECK(strstr(text, "spawn_parallel") != NULL);
                JC_CHECK(strstr(text, "INDEPENDENT") != NULL);
                JC_CHECK(strstr(text, "starts fresh") != NULL);
                JC_CHECK(strstr(text, "ONE angle") != NULL);
                JC_CHECK(strstr(text, "Do not edit") != NULL);
            }
            jc_sb_free(&sb);
        }
    }
    /* Sanity: the packs collectively ship many assets of each kind. */
    JC_CHECK(n_agents >= 20);
    JC_CHECK(n_skills >= 10);
    JC_CHECK(n_cmds >= 10);
    /* M356: the default pack must actually ship the orchestration example. */
    JC_CHECK(saw_investigate >= 1);
    /* M357: the model-carrying example configs exist and are priced. */
    JC_CHECK(n_priced_cfg >= 3);

    jc_arena_free(a);
}

void test_scaffold(void)
{
    test_dest();
    test_pack_table();
    test_assets_parse();
}
