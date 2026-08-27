/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_assetval.c - asset frontmatter validation (pure; see jc_assetval.h). */

#include "jc_assetval.h"
#include "jc_yaml.h"
#include "jc_md.h"
#include "jc_mem.h"
#include "jc_snprintf.h"

#include <string.h>

/* Allowed frontmatter keys per asset kind (keep in sync with the loaders:
 * jc_agentdef.c / jc_skill.c / jc_command.c). */
static const char *const AGENT_KEYS[] = {
    /* M302 added "style" (names an output style). Adding a frontmatter key to a
     * parser without adding it here makes doctor report the new FEATURE as a
     * typo -- which is what happened, caught by M302's own smoke driver. */
    "description", "model", "readonly", "tools", "style", 0
};
static const char *const SKILL_KEYS[] = {
    "name", "description", "allowed-tools", "tools", "restrict-tools",
    "style", 0
};
static const char *const CMD_KEYS[] = {
    "description", "model", "agent", "subtask", "output", "language", 0
};
/* M389: output styles read ONLY `description` from frontmatter -- their name
 * comes from the filename, so a `name:` here does nothing and is now reported
 * (a user who wrote `name: friendly` in terse.md got a style called "terse"
 * with no warning: the M285 declared-but-dead shape). */
static const char *const STYLE_KEYS[] = {
    "description", 0
};

/* Built-in TUI slash commands (no leading slash), used to warn when a project
 * command file would be shadowed -- the TUI dispatches built-ins BEFORE looking
 * up .jichi/commands/, so a user's `context.md` would silently never run.
 *
 * This list used to carry a "keep in sync with the cmds[] list in jc_tui.c"
 * comment, and had drifted to 25 of 53 built-ins: every command added after it
 * was written (context, cost, cache, board, grade, hint, language, rewind,
 * export, fork, output-style, timeouts, ...) could be shadowed with no warning.
 * A hand-maintained invariant is a promise nobody can keep, so it is now
 * mechanical: tests/smoke/builtin_cmds_lint.sh fails the build when jc_tui.c
 * offers a slash command this list does not know (M262). Easter eggs
 * (zen/tea/thanks/credits) are hidden from Tab completion on purpose but still
 * belong here -- they shadow just as effectively. */
static const char *const BUILTIN_CMDS[] = {
    "help", "clear", "model", "mode", "plan", "auto", "mcp", "skills", "map",
    "status", "review", "verify", "compact", "diff", "memory", "markdown",
    "typeahead",
    "quiet", "undo", "checkpoints", "sessions", "resume", "title", "exit",
    "quit",
    "accessible", "assignment", "assignments", "autocontext", "benchmark",
    "board", "cache", "config", "constraints", "context", "cost", "credits",
    "export", "fork", "grade", "hint", "language", "name", "output-style",
    "packages", "rewind", "route", "tea", "thanks", "thankyou", "timeouts",
    "chat", "listen", "voice",
    "tutor", "wisdom", "zen", 0
};

static int in_list(const char *const *list, const char *s)
{
    int i;
    for (i = 0; list[i] != 0; i++) {
        if (strcmp(list[i], s) == 0) {
            return 1;
        }
    }
    return 0;
}

static void push_issue(struct jc_vec *issues, struct jc_arena *a, const char *s)
{
    char *d = jc_arena_strdup(a, s);
    if (d != NULL) {
        jc_vec_push(issues, &d);
    }
}

int jc_assetval_is_builtin_command(const char *name)
{
    return name != NULL && name[0] != '\0' && in_list(BUILTIN_CMDS, name);
}

int jc_assetval_check(int kind, const char *raw, const struct jc_yaml *front,
                      struct jc_arena *a, struct jc_vec *issues)
{
    const char *const *allowed;
    int before = (int)issues->len;
    jc_size i;

    switch (kind) {
    case JC_ASSET_AGENT:   allowed = AGENT_KEYS; break;
    case JC_ASSET_SKILL:   allowed = SKILL_KEYS; break;
    case JC_ASSET_STYLE:   allowed = STYLE_KEYS; break;
    case JC_ASSET_COMMAND:
    default:               allowed = CMD_KEYS;   break;
    }

    if (front == NULL) {
        /* Distinguish "opened a `---` block but never closed it" (a real error)
         * from "no frontmatter at all" (allowed: a body-only asset). */
        if (jc_md_frontmatter_unterminated(raw)) {
            push_issue(issues, a,
                       "frontmatter opened with '---' but never closed");
        }
        return (int)issues->len - before;
    }

    /* Unknown / misspelled keys. */
    for (i = 0; i < front->keys.len; i++) {
        const char *k = *(char **)jc_vec_at((struct jc_vec *)&front->keys, i);
        if (!in_list(allowed, k)) {
            char buf[160];
            jc_snprintf(buf, sizeof(buf), "unknown frontmatter key '%s'", k);
            push_issue(issues, a, buf);
        }
    }

    /* A human-facing description is expected on every asset. */
    if (jc_yaml_get_str(front, "description", NULL) == NULL) {
        push_issue(issues, a, "no 'description'");
    }

    return (int)issues->len - before;
}
