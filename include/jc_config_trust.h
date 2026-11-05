/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_config_trust.h - the project-config trust step (M851, the branch review's
 * CFG-1; the operator's decision of 2026-10-01).
 *
 * A project config (`local/config.json`, else `.jichi/config.json`) used to be
 * merged over the user's `~/.jichi` with nothing asked: a cloned repository
 * could redirect model requests (and the user's key) to its own endpoint, start
 * programs, pre-approve tools, or arm the envelope that switches a headless run
 * to auto. Now an UNTRUSTED project file applies only in the safe direction:
 *
 *   SAFE    display and behaviour keys -- applied as before;
 *   NARROW  keys that can only tighten from an untrusted file -- a stricter
 *           mode or policy, a fence switched on, an opt-in switched off, a
 *           smaller bound, a `permissions.deny` -- applied AFTER the user's
 *           config is parsed, and only where they are stricter than it (so the
 *           comparison is against the real, machine-aware defaults);
 *   GATED   everything that widens a permission, starts a program, redirects a
 *           request or a credential, or reads or writes outside the project --
 *           ignored until the user trusts the file.
 *
 * A key the table does not know is ignored too (fail closed), and a lint holds
 * the table against every key the loader reads, so a new key cannot slip
 * through unclassified. Trust is bound to the file's CONTENT (SHA-256), and
 * stored outside every workspace, in ~/.jichi.d/trusted-configs.json.
 * `--config`, `$JC_CONFIG` and `--config-json` are the user's explicit choice
 * and never pass through here. */
#ifndef JC_CONFIG_TRUST_H
#define JC_CONFIG_TRUST_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "cJSON.h"

struct jc_vec;
struct jc_arena;
struct jc_config;

#define JC_CFGTRUST_SAFE    0
#define JC_CFGTRUST_GATED   1
#define JC_CFGTRUST_NARROW  2

/* --- the classification ----------------------------------------------------- */
int jc_cfgtrust_count(void);
const char *jc_cfgtrust_key_at(int i);
int jc_cfgtrust_class_at(int i);
/* The class of a top-level key, or -1 when the table does not know it. */
int jc_cfgtrust_class_of(const char *key);

/* --- the store (~/.jichi.d/trusted-configs.json, 0600) ------------------------ */
void jc_cfgtrust_store_path(char *out, jc_size cap);
/* 1 when `abs_path` is recorded as trusted WITH this content hash. */
int jc_cfgtrust_is_trusted(const char *abs_path, const char *sha_hex);
jc_status jc_cfgtrust_add(const char *abs_path, const char *sha_hex);
/* 1 removed, 0 was not recorded, -1 the store could not be written. */
int jc_cfgtrust_remove(const char *abs_path);

/* --- the filter ---------------------------------------------------------------- */
/* In place on an UNTRUSTED project config: keep the SAFE keys; move the NARROW
 * ones into `narrow` (an object the caller owns) for
 * jc_cfgtrust_apply_narrowing; drop the GATED and unknown ones, pushing one
 * line per key ("key: what it would do") onto `ignored` (char*, on `a`). */
void jc_cfgtrust_filter(cJSON *proj, cJSON *narrow, struct jc_vec *ignored,
                        struct jc_arena *a);

/* After the merged config is parsed: apply each parked value only where it is
 * stricter than what was parsed; describe the rest onto `ignored`. */
void jc_cfgtrust_apply_narrowing(struct jc_config *cfg, const cJSON *narrow,
                                 struct jc_vec *ignored, struct jc_arena *a);

/* One line saying what `key` set to `val` would do, for the prompt and notice. */
void jc_cfgtrust_describe(const char *key, const cJSON *val, char *out,
                          jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONFIG_TRUST_H */
