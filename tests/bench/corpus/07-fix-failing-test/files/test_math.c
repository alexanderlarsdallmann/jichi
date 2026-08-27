/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
#include <stdio.h>
#include "math_ops.h"

int main(void)
{
    int fails = 0;
    if (add(2, 3) != 5) { printf("FAIL: add(2,3)\n"); fails++; }
    if (mul(4, 5) != 20) { printf("FAIL: mul(4,5)\n"); fails++; }
    if (fails == 0) { printf("1 passed\n"); return 0; }
    printf("%d failed\n", fails);
    return 1;
}
