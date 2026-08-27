/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
#include <stdio.h>
#include "net.h"

int main(void)
{
    printf("%d\n", net_listen(DEFAULT_PORT));
    return 0;
}
