/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
#include "queue.h"

static int items[MAX_ITEMS];
static int n = 0;

int queue_push(int v)
{
    if (n >= MAX_ITEMS) {
        return -1;
    }
    items[n++] = v;
    return 0;
}
