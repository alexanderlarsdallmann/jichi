/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_configedit.h - persist config changes (M111).
 *
 * The config is otherwise LOAD-only. This layer edits the on-disk config JSON
 * *in place* -- load the raw file as a cJSON tree, set specific keys, write it
 * back atomically -- so keys jichi does not model (and the rest of the file) are
 * PRESERVED, rather than serializing the whole in-memory struct (which would drop
 * unknown keys and duplicate merged-in global settings into the project file).
 *
 * Edits target the PROJECT-local config by default (local/config.json) so they
 * stay project-scoped and overlay the global ~/.jichi; an explicit
 * --config path overrides. The pure set_* helpers are unit-tested; the load/save
 * are the thin I/O shell.
 */
#ifndef JC_CONFIGEDIT_H
#define JC_CONFIGEDIT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_json.h" /* cJSON */

/* Replace-or-add a top-level key on `root` (a cJSON object). Unlike a bare
 * cJSON_Add*, these do NOT create a duplicate key when one already exists. Pure;
 * no-op on NULL root/key. Unit-tested. */
void jc_configedit_set_bool(cJSON *root, const char *key, int val);
void jc_configedit_set_str(cJSON *root, const char *key, const char *val);
void jc_configedit_set_num(cJSON *root, const char *key, double val);

/* Resolve the file config edits should be written to: `explicit_path` when
 * non-NULL/non-empty (an explicit --config/$JC_CONFIG), else the project-local
 * `local/config.json` (created on save if absent; git-ignored). Writes the path
 * into `out`. */
void jc_config_edit_target(const char *explicit_path, char *out, jc_size cap);

/* Load the raw JSON of `path` into a cJSON object. Returns a fresh empty object
 * when the file is absent/empty/unparseable (so edits can start from scratch).
 * NULL only on allocation failure. Caller cJSON_Delete()s the result. */
cJSON *jc_config_edit_load(const char *path);

/* Atomically persist `root` (pretty-printed) to `path`: write `<path>.tmp` then
 * rename() over `path`, keeping a `<path>.bak` copy of any prior content. Creates
 * parent directories as needed. JC_OK on success. */
jc_status jc_config_edit_save(const char *path, cJSON *root);

/* High-level single-key edit shared by the TUI /config and the `config`
 * subcommand: resolve the target (explicit_path or local/config.json), load it,
 * set `key` to `value` with type inferred ("true"/"false" -> bool, numeric ->
 * number, else string), and write it back. Refuses a literal `apiKey` (steer to
 * apiKeyEnv). Writes a human-readable outcome into `msg` (cap). Returns JC_OK on
 * success, JC_ERR_DENIED for a refused key, JC_ERR_* on I/O failure. */
jc_status jc_configedit_apply(const char *explicit_path, const char *key,
                              const char *value, char *msg, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONFIGEDIT_H */
