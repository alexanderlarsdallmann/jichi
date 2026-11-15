/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* sockbind - leave a REAL unix-socket file behind for the smoke tier
 * (tests/tools, M1152-a, batch H8; the harness_notify driver's cell 3).
 *
 *   sockbind [--deadline SECS] PATH
 *
 * A run is LIVE (the reference's test, gate-note.sh:22-28) iff its
 * ctl/lead.sock is a REAL socket file (the `-S` test), its meta.txt has no
 * `end=` line, and no scratch inbox is set. POSIX sh cannot create a
 * socket file: `touch PATH` leaves a REGULAR file, so `-S` is false and a
 * driver's live-run fixture would never fire the inject (the lead's
 * run-0024 confirmation). This one-shot helper opens AF_UNIX/SOCK_STREAM,
 * binds to PATH and exits, LEAVING the bound socket file in place so the
 * driver's three-half live-run detection sees a real `-S`.
 *
 * A path at or beyond sizeof(sun_path) is refused by name (exit 1): the
 * old strncpy bound the TRUNCATED name, so a re-bind of the same input
 * collided with it (EADDRINUSE) -- measured 2026-11-08, SV:2138/2237.
 *
 * Exit codes: 0 clean (the socket file is left behind); 1 a
 * socket/bind failure or missing/invalid argv; 3 deadline. The --deadline
 * watchdog scales by the tier's timeout knob (tt_timeout_mult, M273), the
 * way sockq's does: a bind that stalls (a full /tmp, an AF_UNIX
 * namespace that cannot create a socket) must not hang a healthy run
 * forever, and smoke_lint check 10 requires every tool that carries a
 * --deadline to scale it. A bind is normally instant, so the deadline is a
 * backstop, not a pacing layer. Its own perror/usage are kept: it is a
 * standalone test instrument, not a product file.
 */
#include "tt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>  /* offsetof (the sun_path bind length); glibc 2.36's <sys/un.h> does not leak it (M1161, SV:1722.2) */
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static void on_alarm(int sig)
{
    (void)sig;
    _exit(TT_EXIT_DEADLINE);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    long deadline = 10;
    int fd;
    int rc = 0;
    int i;
    struct sockaddr_un addr;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--deadline") == 0 && i + 1 < argc) {
            deadline = strtol(argv[++i], NULL, 10);
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "usage: sockbind [--deadline SECS] PATH\n");
            return 1;
        }
    }
    if (path == NULL) {
        fprintf(stderr, "usage: sockbind [--deadline SECS] PATH\n");
        return 1;
    }
    if (strlen(path) >= sizeof(addr.sun_path)) {
        /* A named refusal, never a silent truncation (M1152-d applied to the
         * instrument; SV:2138/2237). The old strncpy bound the TRUNCATED name,
         * so a later re-bind of the same input collided with it (EADDRINUSE,
         * rc 1, instant) -- measured 2026-11-08 under this drive's long TMPDIR. */
        fprintf(stderr, "sockbind: path too long (%lu >= %lu): %s\n",
                (unsigned long)strlen(path),
                (unsigned long)sizeof(addr.sun_path), path);
        return 1;
    }
    if (deadline > 0) {
        /* Scale with the tier's timeout knob (M273) -- see tt.h: every
         * deadline layer scales, or the knob is a lie. */
        signal(SIGALRM, on_alarm);
        alarm((unsigned)(deadline * tt_timeout_mult()));
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("sockbind: socket");
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr,
             (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                        strlen(addr.sun_path))) < 0) {
        perror("sockbind: bind");
        rc = 1;
    }
    /* Close only the FD; the bound socket FILE is left behind (unlinking
     * it would erase the -S mark the driver's live-run test reads). */
    close(fd);
    return rc;
}
