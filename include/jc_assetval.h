/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_assetval.h - validate project-asset frontmatter (agents/skills/commands/
 * output-styles).
 *
 * The asset loaders (jc_agentdef/jc_skill/jc_command/jc_output_style) accept-or-skip silently:
 * a typo'd key, an unterminated frontmatter block, or a command that collides
 * with a built-in slash command all pass unnoticed. This pure module flags those
 * so `doctor` can report them. No I/O — the caller reads + parses the file (via
 * jc_md/jc_yaml) and hands the result here.
 */
#ifndef JC_ASSETVAL_H
#define JC_ASSETVAL_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_yaml;  /* jc_yaml.h */
struct jc_arena; /* jc_mem.h  */

/* M389 added JC_ASSET_STYLE: output styles were the one frontmatter-bearing
 * project asset doctor never validated, so a typo'd key, a missing description
 * (the text `/output-style` shows a human choosing one) or an unterminated
 * `---` (which swallows the body, leaving a style with no content) all passed
 * silently. Adding a kind means adding its *_KEYS table AND its `case` in
 * jc_assetval_check -- the switch's `default:` maps an unknown kind to
 * CMD_KEYS, so a missing case validates against the wrong table without
 * saying so. tests/smoke/asset_keys_lint.sh enforces both. */
enum jc_asset_kind {
    JC_ASSET_AGENT, JC_ASSET_SKILL, JC_ASSET_COMMAND, JC_ASSET_STYLE
};

/* Validate one asset of `kind`. `raw` is the file's full text; `front` is its
 * parsed frontmatter map (from jc_md_parse), or NULL when there is none. Appends
 * one short issue phrase per problem (arena-owned char*) to `issues` (a
 * caller-init'd jc_vec of char*) and returns the number appended. Checks:
 * a `---` block opened but never closed; unknown/misspelled frontmatter keys;
 * and a missing `description`. Pure. */
int jc_assetval_check(int kind, const char *raw, const struct jc_yaml *front,
                      struct jc_arena *a, struct jc_vec *issues);

/* Whether `name` (no leading slash) is a built-in TUI slash command, so a custom
 * command of that name would be shadowed by the built-in and never run. Pure.
 * (The list mirrors the TUI's built-ins in src/tui/jc_tui.c.) */
int jc_assetval_is_builtin_command(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* JC_ASSETVAL_H */
