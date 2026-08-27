/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jq_core.h - the pure core of jsonq (tests/tools): dot-path parsing and
 * lookup over a cJSON tree.
 *
 * Path syntax (leading '.' required):
 *   .              the whole document
 *   .key           object member
 *   .key.sub       nested member
 *   .key[2]        array element (0-based)
 *   .[0].name      index into a top-level array
 *
 * Keys are bare (no quoting) and end at '.' or '['. This is deliberately
 * not jq -- just enough to assert on jichi's machine-readable output
 * (--output json/jsonl, ls/describe/export). jsonq links src/json/cJSON.c,
 * the same parser the product ships, so the tier asserts through
 * production code (M209 decision D8).
 */
#ifndef JQ_CORE_H
#define JQ_CORE_H

#include <stddef.h>
#include "cJSON.h"

#define JQ_MAX_STEPS 32

enum jq_step_kind { JQ_STEP_KEY = 1, JQ_STEP_INDEX };

struct jq_step {
    enum jq_step_kind kind;
    char *key;      /* JQ_STEP_KEY */
    int index;      /* JQ_STEP_INDEX */
};

struct jq_path {
    struct jq_step steps[JQ_MAX_STEPS];
    int nsteps;     /* 0 = the whole document ('.') */
};

/* Parse a path. Returns 0 on success, -1 with a diagnostic in err. */
int jq_path_parse(const char *text, struct jq_path *out,
                  char *err, size_t errcap);
void jq_path_free(struct jq_path *p);

/* Walk the tree; NULL when any step is missing / out of bounds. */
cJSON *jq_lookup(cJSON *doc, const struct jq_path *p);

/* Does the node match a -t type name (string|number|bool|object|array|
 * null)? Returns 1/0; -1 for an unknown type name. */
int jq_type_matches(const cJSON *item, const char *tname);

#endif /* JQ_CORE_H */
