/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
#include "net.h"

int net_listen(int port)
{
    if (port <= 0) {
        port = DEFAULT_PORT;
    }
    return port;
}
