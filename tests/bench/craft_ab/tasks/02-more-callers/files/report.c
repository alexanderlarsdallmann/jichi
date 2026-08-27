/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* report.c -- the one existing caller. Single-threaded, always has been. */
#include "kv.h"
#include <stdio.h>

int main(void)
{
    kv_set("host", "sc1-w1-1");
    kv_set("run",  "nightly");
    printf("host=%s run=%s\n", kv_get("host"), kv_get("run"));
    kv_clear();
    return 0;
}
