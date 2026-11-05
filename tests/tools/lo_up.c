/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* lo_up - bring up the netns loopback for the smoke tier (tests/tools, M1062-c).
 *
 *   lo_up
 *
 * In a freshly created network namespace (unshare -n) the loopback interface
 * exists but is DOWN. A DNS resolver that queries 127.0.0.1:53 -- the
 * resolve_timeout_connect.sh e2e cell's black hole -- needs loopback up or the
 * query never leaves the socket. This one-shot helper toggles IFF_UP |
 * IFF_RUNNING on "lo" via SIOCSIFFLAGS (it needs no privileges beyond the
 * user+mount+net namespace, where "lo" is owned by the ns root). Test-only;
 * never installed.
 *
 * Exits 0 on success, 1 on any ioctl failure. Its own printf/perror are kept:
 * it is a standalone test instrument, not a product file.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

int main(void)
{
    int fd;
    int rc = 0;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("SIOCGIFFLAGS");
        rc = 1;
        goto done;
    }
    printf("lo before=0x%x\n", (unsigned)ifr.ifr_flags);
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
        perror("SIOCSIFFLAGS");
        rc = 1;
        goto done;
    }
    /* Read the flags back: proves the write stuck (SIOCSIFFLAGS can silently
     * no-op on a read-only link, which would leave the resolver's query dead). */
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("SIOCGIFFLAGS(readback)");
        rc = 1;
        goto done;
    }
    printf("lo after=0x%x\n", (unsigned)ifr.ifr_flags);
done:
    close(fd);
    return rc;
}
