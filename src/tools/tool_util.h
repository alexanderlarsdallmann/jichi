/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* tool_util.h - small helpers shared by the built-in tools (not public). */
#ifndef JC_TOOL_UTIL_H
#define JC_TOOL_UTIL_H

#include "jc_tool.h"
#include "cJSON.h"

/* Result setters. */
void tu_ok_owned(struct jc_tool_result *out, char *owned); /* takes ownership */
void tu_ok_copy(struct jc_tool_result *out, const char *s);
void tu_err(struct jc_tool_result *out, const char *msg);

/* If a successful result touched `path` and an LSP server handles that file
 * type, run diagnostics and append any reported issues to out->content. A
 * no-op when out is an error, there is no LSP manager, or nothing is wrong. */
/* M435: report an M88 moved-goalpost edit -- ONE place, five destinations.
 *
 * edit_file and apply_patch each carried a near-identical block doing the journal
 * event, the telemetry event, the WARN, the on_status and the counter; M435 adds a
 * sixth destination (the model), and six duplicated destinations across two tools is
 * exactly the drift M296 forbids. Renders the model-facing note into `note`
 * (empty when there is no envelope, so a caller may append it unconditionally).
 * No-ops when app->env is NULL. */
void tu_report_test_edit(struct jc_app *app, const char *tool, const char *path,
                         char *note, jc_size note_cap);

void tu_append_diagnostics(struct jc_tool_result *out, struct jc_app *app,
                           const char *path);

/* Argument access (default/NULL if missing or wrong type). */
const char *tu_arg_str(const cJSON *args, const char *key);
int tu_arg_bool(const cJSON *args, const char *key, int dflt);
int tu_arg_int(const cJSON *args, const char *key, int dflt);

/* Schema construction. */
cJSON *tu_schema_begin(void);  /* {"type":"object","properties":{},"required":[]} */
void tu_schema_string(cJSON *schema, const char *name, const char *desc,
                      int required);
void tu_schema_bool(cJSON *schema, const char *name, const char *desc,
                    int required);
void tu_schema_int(cJSON *schema, const char *name, const char *desc,
                   int required);

#endif /* JC_TOOL_UTIL_H */
