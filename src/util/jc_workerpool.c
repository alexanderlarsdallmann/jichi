/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_workerpool.c - shared fork-child supervision primitives (see header). */

#include "jc_workerpool.h"
#include "jc_platform.h"

#include <stddef.h>
#include <signal.h>
#include <sys/wait.h>

void jc_worker_reap_grace(pid_t pid, int grace_ms)
{
    int waited = 0;
    if (pid <= 0) {
        return;
    }
    while (waited < grace_ms) {
        if (waitpid(pid, NULL, WNOHANG) == pid) {
            return;
        }
        jc_sleep_ms(10, NULL);
        waited += 10;
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

int jc_worker_over_deadline(double now_ms, double start_ms, long timeout_ms)
{
    if (timeout_ms <= 0) {
        return 0;
    }
    return (now_ms - start_ms) > (double)timeout_ms;
}
