/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_lsp_nav.c - LSP-backed code navigation tools.
 *
 * find_definition / find_references / list_symbols query the configured language
 * server (see src/lsp). Read-only; registered only when lspServers exist. */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_lsp.h"
#include "jc_str.h"

#include <stdlib.h>

static cJSON *def_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "symbol",
        "The symbol (function/type/variable) name to locate.", 1);
    tu_schema_string(s, "path",
        "Optional file path to disambiguate the symbol; omit to search the "
        "whole project.", 0);
    return s;
}

static cJSON *refs_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "symbol", "The symbol to find references to.", 1);
    tu_schema_string(s, "path",
        "Optional file path where the symbol appears; omit to resolve it "
        "project-wide first.", 0);
    return s;
}

static cJSON *sym_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "The file whose symbols (outline) to list.", 1);
    return s;
}

static jc_status def_run(const cJSON *args, struct jc_tool_result *out,
                         struct jc_app *app)
{
    const char *symbol = tu_arg_str(args, "symbol");
    const char *path = tu_arg_str(args, "path");
    char *rep;

    if (path != NULL && path[0] == '\0') {
        path = NULL; /* empty path == omitted => search the workspace */
    }
    if (symbol == NULL || symbol[0] == '\0') {
        tu_err(out, "error: 'symbol' is required");
        return JC_OK;
    }
    if (app->lsp == NULL) {
        tu_err(out, "error: no language server is configured (lspServers)");
        return JC_OK;
    }
    rep = jc_lsp_definition(app->lsp, path, symbol, 0, NULL);
    if (rep == NULL) {
        tu_err(out, "error: no language server matches that file type");
        return JC_OK;
    }
    tu_ok_owned(out, rep);
    return JC_OK;
}

static jc_status refs_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    const char *symbol = tu_arg_str(args, "symbol");
    const char *path = tu_arg_str(args, "path");
    char *rep;

    if (path != NULL && path[0] == '\0') {
        path = NULL;
    }
    if (symbol == NULL || symbol[0] == '\0') {
        tu_err(out, "error: 'symbol' is required");
        return JC_OK;
    }
    if (app->lsp == NULL) {
        tu_err(out, "error: no language server is configured (lspServers)");
        return JC_OK;
    }
    rep = jc_lsp_references(app->lsp, path, symbol, 0, NULL);
    if (rep == NULL) {
        tu_err(out, "error: no language server matches that file type");
        return JC_OK;
    }
    tu_ok_owned(out, rep);
    return JC_OK;
}

static jc_status sym_run(const cJSON *args, struct jc_tool_result *out,
                         struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    char *rep;

    if (path == NULL || path[0] == '\0') {
        tu_err(out, "error: 'path' is required");
        return JC_OK;
    }
    if (app->lsp == NULL) {
        tu_err(out, "error: no language server is configured (lspServers)");
        return JC_OK;
    }
    rep = jc_lsp_symbols(app->lsp, path, NULL);
    if (rep == NULL) {
        tu_err(out, "error: no language server matches that file type");
        return JC_OK;
    }
    tu_ok_owned(out, rep);
    return JC_OK;
}

static cJSON *actions_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "The file to query for code actions.", 1);
    tu_schema_int(s, "line", "1-based line to query.", 1);
    tu_schema_string(s, "kind",
        "Optional: restrict to CodeActionKind(s), comma-separated "
        "(e.g. \"quickfix\", \"refactor\", \"source.organizeImports\"). "
        "Prefix match; omit for all kinds.", 0);
    return s;
}

static jc_status actions_run(const cJSON *args, struct jc_tool_result *out,
                             struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    int line = tu_arg_int(args, "line", 0);
    char *res;
    struct jc_sb sb;
    int n = 0;

    if (path == NULL || path[0] == '\0' || line <= 0) {
        tu_err(out, "error: 'path' and 'line' (1-based) are required");
        return JC_OK;
    }
    if (app->lsp == NULL) {
        tu_err(out, "error: no language server is configured (lspServers)");
        return JC_OK;
    }
    res = jc_lsp_code_actions(app->lsp, path, (long)line,
                              tu_arg_str(args, "kind"));
    if (res == NULL) {
        tu_err(out, "error: no language server matches that file type");
        return JC_OK;
    }
    jc_sb_init(&sb);
    jc_lsp_format_code_actions(res, &sb, &n);
    free(res);
    if (n == 0) {
        jc_sb_free(&sb);
        tu_ok_copy(out, "(no code actions offered at that line)");
        return JC_OK;
    }
    tu_ok_owned(out, jc_sb_finish(&sb));
    jc_sb_free(&sb);
    return JC_OK;
}

static const struct jc_tool DEF_TOOL = {
    "find_definition",
    "Find where a symbol (function/type/variable) is defined, using the "
    "language server. Give the symbol name; optionally a file path to "
    "disambiguate, otherwise the whole project is searched. More precise than "
    "grep for jumping to a definition.",
    def_schema, 1, def_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool REFS_TOOL = {
    "find_references",
    "Find all references/uses of a symbol via the language server. Give the "
    "symbol name; optionally the file where it appears. Use it to gauge the "
    "blast radius of a change.",
    refs_schema, 1, refs_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool SYM_TOOL = {
    "list_symbols",
    "List the symbols (functions, types, variables) defined in a file as an "
    "outline, via the language server. A fast way to understand a file's "
    "structure.",
    sym_schema, 1, sym_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool ACTIONS_TOOL = {
    "list_code_actions",
    "List the code actions / quick-fixes a language server offers at a line "
    "(textDocument/codeAction): organize imports, add import, fix-its, refactors. "
    "Give 'path' and 'line' (1-based); apply one with apply_code_action.",
    actions_schema, 1, actions_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_find_definition(void) { return &DEF_TOOL; }
const struct jc_tool *jc_tool_find_references(void) { return &REFS_TOOL; }
const struct jc_tool *jc_tool_list_symbols(void)    { return &SYM_TOOL; }
const struct jc_tool *jc_tool_list_code_actions(void) { return &ACTIONS_TOOL; }
