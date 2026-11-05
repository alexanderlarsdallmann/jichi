/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* rsshwm - run a command and report its resident-set HIGH-WATER MARK (M1092-c).
 *
 *   rsshwm COMMAND [ARGS...]
 *
 * The kernel's own measurement of a process's peak resident set is the
 * rusage.ru_maxrss of its children, read at reap time -- it catches a
 * transient peak that a /proc/<pid>/statm sampler running at 20 Hz can
 * miss entirely between two ticks. M1092-c (SV:0302 R2): http_resp_cap's
 * 50 ms sampler missed the brief loopback peak of a 50 MiB body, so the
 * memory checks (4 and 8) stayed green even with the response ceiling
 * raised to 1 GiB, while ru_maxrss said 63,552 KB vs 45,312 KB -- a check
 * that cannot go red proves nothing. This helper makes the driver
 * measure the high-water mark directly: fork() the command, execvp() it in
 * the child, wait4() it in the parent and print the child's ru_maxrss.
 *
 * Output: one decimal line, "maxrss_kb N" (N = ru_maxrss, in KILOBYTES as
 * the kernel reports it on Linux). Exit status mirrors the child: 0 when
 * the child exited cleanly (code 0), 1 when the child exited non-zero
 * (its code is printed), 2 when the child was signalled (the signal
 * number is printed). Test-only; never installed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h> /* pid_t: MiNTLib's <unistd.h> declares it only under an X/Open level (portability_lint 29, M723) */
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    pid_t child;
    int status;
    struct rusage ru;

    if (argc < 2) {
        fprintf(stderr, "usage: rsshwm COMMAND [ARGS...]\n");
        return 2;
    }
    child = fork();
    if (child < 0) {
        perror("fork");
        return 2;
    }
    if (child == 0) {
        execvp(argv[1], argv + 1);
        perror("execvp");
        _exit(127);
    }
    ru.ru_maxrss = 0;
    if (wait4(-1, &status, 0, &ru) < 0) {
        perror("wait4");
        return 2;
    }
    printf("maxrss_kb %ld\n", (long)ru.ru_maxrss);
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0)
            return 0;
        printf("exit %d\n", WEXITSTATUS(status));
        return 1;
    }
    if (WIFSIGNALED(status)) {
        printf("signal %d\n", WTERMSIG(status));
        return 2;
    }
    return 1;
}
