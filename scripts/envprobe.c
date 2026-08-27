/* envprobe.c -- can this (possibly emulated) environment run the smoke/unit tests
 * that spawn subprocesses? Built and run per target by scripts/tier-v-arch.sh.
 *
 * WHY (M470). M469's sweep reported six MIPS rows as "unit suite RAN and reported
 * failures: 11,627 checks, 73 failures", which reads as an accusation against jichi.
 * The cause was one level below it: pipe() fails under qemu-mips with zig's musl --
 * MIPS is the one Linux architecture whose pipe syscall returns both descriptors in
 * registers rather than writing a user array -- and all 73 failures were downstream,
 * the seven failing files being exactly those that spawn a subprocess or read /proc.
 *
 * This probe exists so a row can say "the environment cannot run these tests" instead.
 * It only ever downgrades an accusation; a passing suite is reported the same either
 * way. Test-only, never installed, C89. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int fd[2];
    fd_set rf;
    struct timeval tv;
    pid_t pid;
    int st = -1;
    int ok_pipe = 0, ok_select = 0, ok_fork = 0, ok_proc = 0;
    char buf[8];
    FILE *f;

    if (pipe(fd) == 0) {
        ok_pipe = 1;
        if (write(fd[1], "hi", 2) == 2) {
            FD_ZERO(&rf);
            FD_SET(fd[0], &rf);
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            if (select(fd[0] + 1, &rf, NULL, NULL, &tv) > 0 &&
                read(fd[0], buf, sizeof buf) == 2) {
                ok_select = 1;
            }
        }
        close(fd[0]);
        close(fd[1]);
    }

    pid = fork();
    if (pid == 0) {
        execlp("true", "true", (char *)0);
        _exit(127);
    } else if (pid > 0) {
        if (waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0) {
            ok_fork = 1;
        }
    }

    /* Present-but-different procfs is its own hazard: an fopen that succeeds on a
     * binary /proc is worse than one that fails, because the caller takes the parsing
     * path. Reported, not judged -- illumos is the case in mind (PLATFORMS.md). */
    f = fopen("/proc/self/stat", "r");
    if (f != NULL) {
        ok_proc = 1;
        fclose(f);
    }

    printf("pipe=%s select=%s forkexec=%s procfs=%s\n",
           ok_pipe ? "ok" : "FAIL",
           ok_select ? "ok" : "FAIL",
           ok_fork ? "ok" : "FAIL",
           ok_proc ? "present" : "absent");
    return 0;
}
