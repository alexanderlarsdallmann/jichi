/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* convert_internal.h - helpers shared among the jc_convert_*.c translation
 * units. Not a public API. */
#ifndef JC_CONVERT_INTERNAL_H
#define JC_CONVERT_INTERNAL_H

#include "jc_convert.h"

/* Map a source provider string onto one our app understands. Anything that is
 * not "anthropic" becomes "openai" (OpenAI-compatible, relying on apiBase);
 * *mapped is set to 1 when a non-native provider was remapped. NULL => the
 * anthropic default. */
const char *jc_convert_map_provider(const char *p, int *mapped);

/* The conventional API-key env var for a mapped provider. */
const char *jc_convert_provider_key_env(const char *provider);

/* True when `key` is a usable literal (non-empty and not a template/env ref:
 * no "${", not a bare "$VAR", not a "{env:...}" form). */
int jc_convert_key_is_literal(const char *key);

/* Resolve a model's provider + key onto an IR model, recording any warnings.
 * `raw_provider`/`raw_key` come from the source; `raw_key_env` is an explicit
 * env-var name if the source gave one (else NULL). */
void jc_convert_fill_provider(struct jc_ir *ir, struct jc_ir_model *m,
                              const char *raw_provider, const char *raw_key,
                              const char *raw_key_env);

/* Append a role string to an IR model (deduped, capped). */
void jc_ir_model_add_role(struct jc_ir_model *m, const char *role);

/* Allocate a zeroed IR sub-object on the IR arena. */
struct jc_ir_model *jc_ir_new_model(struct jc_ir *ir);
struct jc_ir_mcp   *jc_ir_new_mcp(struct jc_ir *ir);
struct jc_ir_lsp   *jc_ir_new_lsp(struct jc_ir *ir);

/* Lowercase, non-alphanumeric-to-dash slug of `name` (arena copy). */
const char *jc_convert_slug(struct jc_arena *a, const char *name);

/* Claude Code (#1): read a project/global config TREE rooted at `base_dir`
 * (settings.json, CLAUDE.md, .claude agents + commands, mcpServers) and fill
 * *out (out->json malloc'd; free() by caller). */
jc_status jc_convert_run_claude(const char *base_dir,
                                struct jc_convert_result *out,
                                struct jc_arena *a);

/* --- asset emission (jc_convert_assets.c) --- */

/* Append an asset (arena-owned relpath + contents) to the IR, capped. */
void jc_ir_add_asset(struct jc_ir *ir, const char *relpath,
                     const char *contents);

/* Build "<dir>/<slug>.<ext>", suffixing -2, -3, ... on collision with an
 * already-added asset. Returns an arena copy. */
const char *jc_ir_unique_relpath(struct jc_ir *ir, const char *dir,
                                 const char *slug, const char *ext);

/* Render an agent profile markdown (frontmatter + body). readonly: -1 omit,
 * else 0/1. tools may be NULL/empty. */
const char *jc_asset_agent_md(struct jc_arena *a, const char *description,
                              const char *model, int readonly,
                              const char *const *tools, int ntools,
                              const char *body);

/* Render a custom-command markdown (frontmatter + body). agent/model NULL. */
const char *jc_asset_command_md(struct jc_arena *a, const char *description,
                                const char *agent, const char *model,
                                const char *body);

#endif /* JC_CONVERT_INTERNAL_H */
