/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_yaml.h - a minimal block-style YAML parser (subset).
 *
 * This is NOT a full YAML implementation. It supports the subset that
 * Continue's config.yaml uses: indentation-based mappings and sequences with
 * scalar values. It deliberately omits flow style ({}/[]), anchors/aliases,
 * block scalars (|, >), multi-document streams, and complex keys.
 *
 * Tree nodes and scalar strings are allocated from a caller-provided arena (so
 * they are released when the arena is), but each node's child vectors own heap
 * backing buffers: call jc_yaml_free on the root to release those.
 */
#ifndef JC_YAML_H
#define JC_YAML_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"

typedef enum {
    JC_YAML_SCALAR,
    JC_YAML_SEQ,
    JC_YAML_MAP
} jc_yaml_type;

struct jc_yaml {
    jc_yaml_type type;
    char        *scalar;  /* SCALAR */
    struct jc_vec items;  /* SEQ: of struct jc_yaml*           */
    struct jc_vec keys;   /* MAP: of char*                     */
    struct jc_vec vals;   /* MAP: of struct jc_yaml*           */
};

/* Parse `text` into a tree. Returns NULL on allocation failure or empty
 * input. The root is typically a MAP. */
struct jc_yaml *jc_yaml_parse(const char *text, struct jc_arena *a);

/* Release the heap backing buffers of every node's child vectors (the nodes
 * and scalars themselves are arena-owned). Safe on NULL. */
void jc_yaml_free(struct jc_yaml *node);

/* MAP lookup: returns the value node for `key`, or NULL. */
struct jc_yaml *jc_yaml_get(const struct jc_yaml *node, const char *key);

/* MAP lookup returning a scalar string, or `dflt` if absent / not scalar. */
const char *jc_yaml_get_str(const struct jc_yaml *node, const char *key,
                            const char *dflt);

/* SEQ access. */
jc_size jc_yaml_seq_len(const struct jc_yaml *node);
struct jc_yaml *jc_yaml_seq_at(const struct jc_yaml *node, jc_size i);

#ifdef __cplusplus
}
#endif
#endif /* JC_YAML_H */
