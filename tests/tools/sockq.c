/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* sockq - a one-shot AF_UNIX line client for the smoke tier
 * (tests/tools, M214).
 *
 *   sockq [--deadline SECS] SOCKET
 *
 * Connects to the AF_UNIX stream socket SOCKET, sends all of stdin,
 * half-closes the write side, then reads the reply until the peer closes
 * (or --deadline) and writes it to stdout. This is the client half of
 * jichi's daemon/control protocols (newline-framed JSON, one request per
 * connection) -- POSIX sh has no socket support, and a data-only client
 * is deliberately a SEPARATE binary from mockmodel the server (a server
 * and a client in one binary is a smell; M209 plan D3).
 *
 * Exit codes: 0 clean; 2 usage; 3 deadline; 4 connect/socket failure.
 * Test-only; never installed.
 */

#include "tt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static const char *g_prog = "sockq";

static void usage(void)
{
    fprintf(stderr, "usage: %s [--deadline SECS] SOCKET\n", g_prog);
}

static void on_alarm(int sig)
{
    (void)sig;
    _exit(TT_EXIT_DEADLINE);
}

/* Read all of stdin into a malloc'd buffer. */
static char *read_stdin(size_t *outlen)
{
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (buf == NULL)
        return NULL;
    for (;;) {
        long n;
        if (len + 4096 + 1 > cap) {
            char *nb;
            cap *= 2;
            nb = (char *)realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
        n = (long)read(0, buf + len, 4096);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        if (n == 0)
            break;
        len += (size_t)n;
    }
    *outlen = len;
    return buf;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    long deadline = 30;
    int fd;
    struct sockaddr_un addr;
    char *body;
    size_t body_len = 0, off = 0;
    char chunk[4096];
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--deadline") == 0 && i + 1 < argc) {
            deadline = strtol(argv[++i], NULL, 10);
        } else if (path == NULL) {
            path = argv[i];
        } else {
            usage();
            return TT_EXIT_USAGE;
        }
    }
    if (path == NULL || strlen(path) >= sizeof(addr.sun_path)) {
        usage();
        return TT_EXIT_USAGE;
    }

    signal(SIGPIPE, SIG_IGN);
    if (deadline > 0) {
        /* Scale with the tier's timeout knob (M273) -- see tt.h: every
         * deadline layer scales, or the knob is a lie. */
        signal(SIGALRM, on_alarm);
        alarm((unsigned)(deadline * tt_timeout_mult()));
    }

    body = read_stdin(&body_len);
    if (body == NULL) {
        fprintf(stderr, "%s: stdin read failed\n", g_prog);
        return TT_EXIT_USAGE;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "%s: socket failed\n", g_prog);
        free(body);
        return 4;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "%s: cannot connect to %s\n", g_prog, path);
        free(body);
        close(fd);
        return 4;
    }

    while (off < body_len) {
        long n = (long)write(fd, body + off, body_len - off);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    free(body);
    shutdown(fd, SHUT_WR);      /* half-close: signal end-of-request */

    for (;;) {
        long n = (long)read(fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        fwrite(chunk, 1, (size_t)n, stdout);
    }
    fflush(stdout);
    close(fd);
    return TT_EXIT_OK;
}
