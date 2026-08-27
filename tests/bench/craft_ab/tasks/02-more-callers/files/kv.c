/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* kv.c -- see kv.h. Written in an afternoon; used by report.c since 2024. */
#include "kv.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct entry {
    char *key;
    char  value[KV_MAX_VALUE];
};

static struct entry g_entries[KV_MAX_ENTRIES];
static int g_count;

int kv_set(const char *key, const char *value)
{
    int i;
    for (i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0) {
            strncpy(g_entries[i].value, value, KV_MAX_VALUE - 1);
            g_entries[i].value[KV_MAX_VALUE - 1] = '\0';
            return 0;
        }
    }
    if (g_count >= KV_MAX_ENTRIES) {
        return -1;
    }
    g_entries[g_count].key = malloc(strlen(key) + 1);
    strcpy(g_entries[g_count].key, key);
    strncpy(g_entries[g_count].value, value, KV_MAX_VALUE - 1);
    g_entries[g_count].value[KV_MAX_VALUE - 1] = '\0';
    g_count++;
    return 0;
}

const char *kv_get(const char *key)
{
    static char buf[KV_MAX_VALUE];
    int i;
    for (i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0) {
            strcpy(buf, g_entries[i].value);
            return buf;
        }
    }
    return NULL;
}

void kv_clear(void)
{
    int i;
    for (i = 0; i < g_count; i++) {
        free(g_entries[i].key);
    }
    g_count = 0;
}
