/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_convert.h - convert a Continue or opencode config into a jichi one.
 *
 * Accepts Continue's modern config.yaml, a legacy config.json, or an opencode
 * opencode.json/opencode.jsonc, maps them onto a neutral intermediate
 * representation (struct jc_ir), and emits our JSON config plus an optional
 * .jichi/ asset tree.
 */
#ifndef JC_CONVERT_H
#define JC_CONVERT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_yaml.h"
#include "cJSON.h"

/* Which source configuration a given input is. */
enum jc_src_format {
    JC_SRC_CONTINUE_YAML,  /* Continue modern config.yaml (Zod schema)   */
    JC_SRC_CONTINUE_JSON,  /* Continue legacy config.json (models[])     */
    JC_SRC_OPENCODE,       /* opencode.json / opencode.jsonc             */
    JC_SRC_CLAUDE,         /* Claude Code config tree (.claude/ + CLAUDE.md) */
    JC_SRC_UNKNOWN
};

/* Human label for a format (for diagnostics). Never NULL. */
const char *jc_src_format_name(enum jc_src_format fmt);

/* Detect the source format from an optional filename plus the file text.
 * YAML is decided lexically; a JSON input is stripped of JSONC extensions,
 * parsed, and classified by its top-level shape (jc_convert_classify_json). */
enum jc_src_format jc_convert_detect(const char *filename, const char *text,
                                     struct jc_arena *a);

/* Classify an already-parsed JSON tree. opencode markers are checked first
 * (they are unambiguous): a $schema mentioning opencode, or an object-valued
 * provider/agent/mcp/command/permission, or a string-valued model. Otherwise
 * a top-level models[] array is Continue legacy JSON. */
enum jc_src_format jc_convert_classify_json(const cJSON *root);

/* --- intermediate representation --- */

#define JC_IR_MAX_MODELS 64
#define JC_IR_MAX_ROLES  16
#define JC_IR_MAX_MCP    32
#define JC_IR_MAX_LSP    16
#define JC_IR_MAX_DOCS   32
#define JC_IR_MAX_ASSETS 256
/* One shared cap for the small per-item string lists (args, env, headers,
 * instructions, permission entries, warnings). */
#define JC_IR_MAX_LIST   64

struct jc_ir_model {
    const char *name;
    const char *provider;      /* mapped: "anthropic" | "openai"        */
    const char *model;
    const char *api_base;
    const char *api_key;       /* literal key, or NULL                  */
    const char *api_key_env;   /* env var NAME, or NULL                 */
    const char *roles[JC_IR_MAX_ROLES];
    int    role_count;
    int    has_max;
    long   max_tokens;
    int    has_temp;
    double temperature;
    int    has_ctx;
    long   context_length;
};

struct jc_ir_mcp {
    const char *name;
    const char *command;       /* stdio: single command                 */
    const char *url;           /* remote: endpoint                      */
    const char *args[JC_IR_MAX_LIST];
    int    arg_count;
    const char *env_keys[JC_IR_MAX_LIST];
    const char *env_vals[JC_IR_MAX_LIST];
    int    env_count;
    const char *headers[JC_IR_MAX_LIST]; /* "Name: value" strings       */
    int    header_count;
};

struct jc_ir_lsp {
    const char *name;
    const char *command;
    const char *args[JC_IR_MAX_LIST];
    int    arg_count;
    const char *exts[JC_IR_MAX_LIST];
    int    ext_count;
};

struct jc_ir_doc {
    const char *name;
    const char *url;   /* one of url / path is set */
    const char *path;
};

struct jc_ir_asset {
    const char *relpath;  /* e.g. "agents/reviewer.md", "AGENTS.md" */
    const char *contents;
};

/* Everything is arena-owned; no jc_ir_free is needed (the arena owns it). */
struct jc_ir {
    struct jc_arena *a;
    struct jc_ir_model *models[JC_IR_MAX_MODELS];
    int    model_count;
    int    active_model;
    struct jc_ir_mcp *mcp[JC_IR_MAX_MCP];
    int    mcp_count;
    struct jc_ir_lsp *lsp[JC_IR_MAX_LSP];
    int    lsp_count;
    struct jc_ir_doc *docs[JC_IR_MAX_DOCS];
    int    doc_count;
    struct jc_ir_asset *assets[JC_IR_MAX_ASSETS];
    int    asset_count;
    const char *instructions[JC_IR_MAX_LIST];
    int    instr_count;
    const char *perm_allow[JC_IR_MAX_LIST];
    int    allow_count;
    const char *perm_deny[JC_IR_MAX_LIST];
    int    deny_count;
    const char *mode;                /* "plan" | "auto" | NULL          */
    const char *warnings[JC_IR_MAX_LIST];
    int    warning_count;
};

/* Zero the IR and bind its arena. */
void jc_ir_init(struct jc_ir *ir, struct jc_arena *a);

/* Record a human-readable warning (printf-style), bounded to the cap. */
void jc_ir_warn(struct jc_ir *ir, const char *fmt, ...);

struct jc_convert_result {
    char *json;               /* output config JSON (malloc'd; free())   */
    char *model_name;         /* chosen model display name, or NULL       */
    int   model_count;        /* number of models found                   */
    char  warning[256];       /* first note (back-compat), or ""          */
    const char *const *warnings; /* all notes (-> ir->warnings)           */
    int   warning_count;
    struct jc_ir *ir;         /* arena-owned; for asset emission          */
};

/* Convert `input_text` of the given format into *out. out->json must be
 * free()d by the caller. */
jc_status jc_convert_run(const char *input_text, enum jc_src_format fmt,
                         struct jc_convert_result *out, struct jc_arena *a);

/* Per-source mappers: fill `ir` from a parsed source tree. */
jc_status jc_convert_continue_yaml(const struct jc_yaml *root,
                                   struct jc_ir *ir, struct jc_arena *a);
jc_status jc_convert_continue_json(const cJSON *root,
                                   struct jc_ir *ir, struct jc_arena *a);
jc_status jc_convert_opencode(const cJSON *root,
                              struct jc_ir *ir, struct jc_arena *a);

/* Emit the jichi config JSON from the IR (malloc'd; free()). */
char *jc_ir_to_config(const struct jc_ir *ir);

/* Build the .jichi/ asset tree into ir->assets from source-specific inputs.
 * (Populated by the mappers; this is a no-op hook for now.) */
jc_status jc_ir_to_assets(struct jc_ir *ir);

/* Legacy heuristic kept for back-compat: 0 => YAML, 1 => JSON. */
int jc_convert_is_json(const char *filename, const char *text);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONVERT_H */
