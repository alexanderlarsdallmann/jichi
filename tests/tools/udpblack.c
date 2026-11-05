/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* udpblack - a UDP black hole for the smoke tier (tests/tools, M1062-c).
 *
 *   udpblack [PORT]
 *
 * Binds 127.0.0.1:PORT (default 53) as a UDP socket and recvfrom-loops,
 * discarding every datagram and answering to NOTHING. A DNS query pointed at
 * this socket is a black hole: the resolver never gets a reply, so a name
 * lookup blocks until the client gives up -- the exact signature the
 * resolve_timeout_connect.sh e2e cell needs to fire curl's connect bound
 * inside the resolve.
 *
 * Binds 127.0.0.1 (not INADDR_ANY): the cell's nameserver is 127.0.0.1, so a
 * loopback socket catches it, and a loopback bind is what keeps the helper
 * killable and scoped to the netns. In the netns the helper runs as root, so
 * binding :53 needs no host privileges.
 *
 * Exits on SIGTERM or SIGINT (the driver kills it by pid before unshare
 * returns; the netns dies with it, so no signal path is strictly required --
 * but a clean kill is the shape the driver relies on). Test-only; never
 * installed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>

static volatile int g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

int main(int argc, char **argv)
{
    int fd;
    int port = (argc > 1) ? atoi(argv[1]) : 53;
    struct sockaddr_in sa;
    char buf[1024];
    struct timeval tv;
    struct sockaddr_in from;
    socklen_t fromlen;

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = (unsigned short)port << 8;
    sa.sin_addr.s_addr = htonl(127u << 24);  /* 127.0.0.1 */
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }
    fromlen = (socklen_t)sizeof(from);
    for (;;) {
        if (g_stop) {
            break;
        }
        {
            fd_set rfds;
            /* A blocking recvfrom would never return to see the signal flag,
             * so the loop polls on a 1 s select: the kill path is real, not
             * left to the netns dying. Discard the datagram, answer nothing. */
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0 &&
                FD_ISSET(fd, &rfds)) {
                (void)recvfrom(fd, buf, sizeof(buf), 0,
                               (struct sockaddr *)&from, &fromlen);
            }
        }
    }
    close(fd);
    return 0;
}
