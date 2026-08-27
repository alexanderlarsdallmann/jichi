/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* kv.h -- a very small string key/value store. */
#ifndef KV_H
#define KV_H

#define KV_MAX_ENTRIES 64
#define KV_MAX_VALUE   128

/* Store a copy of `value` under `key`. Returns 0 on success. */
int kv_set(const char *key, const char *value);

/* Look up `key`. Returns the value, or NULL if it is not present. */
const char *kv_get(const char *key);

/* Remove every entry. */
void kv_clear(void);

#endif
