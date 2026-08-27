/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
#include "buf.h"

static int len = 0;

void buf_reset(void)
{
    len = 0;
}
