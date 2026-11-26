/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* longredirect - a raw TCP listener that answers GET with a 3xx status line
 * of a configurable minimum length, for the smoke tier's redirect guard tests
 * (M1208-S1, batch SEC).
 *
 *   longredirect <minlen> <target-url> [--count N] [--capture DIR]
 *
 * Binds 127.0.0.1:0, prints the bound port to stdout exactly once
 * ("PORT=<port>\n"), then serves connections until --count is reached
 * (default: unlimited until killed). Each connection: reads the request
 * until \r\n\r\n, writes the raw request to DIR/req.N if DIR is given,
 * then answers with:
 *   HTTP/1.1 302 <reason-phrase-padded-to-minlen> \r\n
 *   Location: <target-url> \r\n
 *   Content-Length: 0 \r\n
 *   \r\n
 *
 * Exit codes: 0 clean (served --count requests), 2 usage error, 3 signal
 * (SIGPIPE, etc). Test-only; never installed.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* INADDR_LOOPBACK is not in POSIX.1-2001; define it if missing */
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001UL
#endif

static const char *g_prog = "longredirect";

static void usage(void)
{
    fprintf(stderr,
            "usage: %s <minlen> <target-url> [--count N] [--capture DIR]\n", g_prog);
}

int main(int argc, char **argv)
{
    int minlen = 0;
    const char *target_url = NULL;
    int max_requests = -1;  /* -1 means unlimited */
    const char *capture_dir = NULL;
    int i;
    int sock;
    struct sockaddr_in addr;
    socklen_t addrlen;
    int port;

    if (argc < 3) {
        usage();
        return 2;
    }

    /* Parse minlen */
    if (sscanf(argv[1], "%d", &minlen) != 1 || minlen <= 0) {
        fprintf(stderr, "%s: minlen must be a positive integer\n", g_prog);
        usage();
        return 2;
    }

    target_url = argv[2];

    /* Parse optional --count N and --capture DIR */
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--count") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --count requires an argument\n", g_prog);
                usage();
                return 2;
            }
            if (sscanf(argv[++i], "%d", &max_requests) != 1 || max_requests <= 0) {
                fprintf(stderr, "%s: --count must be a positive integer\n", g_prog);
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--capture") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --capture requires an argument\n", g_prog);
                usage();
                return 2;
            }
            capture_dir = argv[++i];
        } else {
            fprintf(stderr, "%s: unknown argument: %s\n", g_prog, argv[i]);
            usage();
            return 2;
        }
    }

    /* Create TCP socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror(g_prog);
        return 2;
    }

    /* Bind to 127.0.0.1:0 (ephemeral port) */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* ephemeral */

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror(g_prog);
        close(sock);
        return 2;
    }

    /* Get the bound port */
    addrlen = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &addrlen) < 0) {
        perror(g_prog);
        close(sock);
        return 2;
    }
    port = ntohs(addr.sin_port);

    /* Print the port exactly once to stdout */
    printf("PORT=%d\n", port);
    fflush(stdout);

    /* Listen for connections */
    if (listen(sock, 10) < 0) {
        perror(g_prog);
        close(sock);
        return 2;
    }

    /* Accept loop */
    for (i = 0; max_requests < 0 || i < max_requests; i++) {
        int clisock;
        struct sockaddr_in cliaddr;
        socklen_t cliaddrlen;
        char buf[4096];
        char *reason_start;
        char reason_buf[128];
        const char *status_line = "HTTP/1.1 302 ";
        size_t status_len = strlen(status_line);
        size_t reason_needed;
        ssize_t n;

        cliaddrlen = sizeof(cliaddr);
        clisock = accept(sock, (struct sockaddr *)&cliaddr, &cliaddrlen);
        if (clisock < 0) {
            perror(g_prog);
            break;
        }

        /* Read the request until \r\n\r\n (end of headers) */
        n = 0;
        while (n < (ssize_t)sizeof(buf) - 1) {
            ssize_t r = read(clisock, buf + n, sizeof(buf) - n - 1);
            if (r <= 0) {
                close(clisock);
                goto next_conn;
            }
            n += r;
            buf[n] = '\0';
            /* Check for \r\n\r\n */
            if (strstr(buf, "\r\n\r\n") != NULL) {
                break;
            }
        }
        if (n == sizeof(buf) - 1) {
            /* Request too long or no \r\n\r\n found */
            close(clisock);
            goto next_conn;
        }

        /* Capture the request to DIR/req.N if capture_dir is set */
        if (capture_dir != NULL) {
            static int req_count = 0;
            char path[512];
            FILE *f;
            req_count++;
            if (snprintf(path, sizeof(path), "%s/req.%d", capture_dir, req_count) < 0) {
                /* Path too long, skip capture but continue serving */
            } else if ((f = fopen(path, "wb")) != NULL) {
                fwrite(buf, 1, n, f);
                fclose(f);
            }
        }

        /* Find where the status line ends (first \r\n) */
        reason_start = strstr(buf, "\r\n");
        if (reason_start == NULL) {
            close(clisock);
            goto next_conn;
        }

        /* Calculate how long the reason phrase needs to be */
        /* status_line + " " + reason + "\r\n" */
        reason_needed = minlen - status_len;
        if (reason_needed > sizeof(reason_buf) - 1) {
            reason_needed = sizeof(reason_buf) - 1;
        }

        /* Build the response */
        {
            char response[8192];
            size_t response_len = 0;
            size_t len;

            /* Status line with padded reason */
            /* "Status" is 6 bytes, pad with 'x' to reach required length */
            const char *base_reason = "Status";
            size_t base_len = strlen(base_reason);
            if (base_len < reason_needed) {
                memcpy(reason_buf, base_reason, base_len);
                memset(reason_buf + base_len, 'x', reason_needed - base_len);
                reason_buf[reason_needed] = '\0';
            } else {
                memcpy(reason_buf, base_reason, reason_needed);
                reason_buf[reason_needed] = '\0';
            }

            /* Build the full status line */
            len = snprintf(response + response_len,
                           sizeof(response) - response_len,
                           "%s%s\r\n", status_line, reason_buf);
            if (len >= sizeof(response) - response_len) {
                close(clisock);
                goto next_conn;
            }
            response_len += len;

            /* Location header */
            len = snprintf(response + response_len,
                           sizeof(response) - response_len,
                           "Location: %s\r\n", target_url);
            if (len >= sizeof(response) - response_len) {
                close(clisock);
                goto next_conn;
            }
            response_len += len;

            /* Content-Length header */
            len = snprintf(response + response_len,
                           sizeof(response) - response_len,
                           "Content-Length: 0\r\n");
            if (len >= sizeof(response) - response_len) {
                close(clisock);
                goto next_conn;
            }
            response_len += len;

            /* Empty line (end of headers) */
            len = snprintf(response + response_len,
                           sizeof(response) - response_len,
                           "\r\n");
            if (len >= sizeof(response) - response_len) {
                close(clisock);
                goto next_conn;
            }
            response_len += len;

            /* Send the response */
            if ((ssize_t)write(clisock, response, response_len) != (ssize_t)response_len) {
                perror(g_prog);
            }
        }

        close(clisock);
    next_conn:
        continue;
    }

    close(sock);
    return 0;
}
