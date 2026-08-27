/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* mockmodel - scripted loopback HTTP+SSE model server for the smoke tier
 * (tests/tools, M209).
 *
 *   mockmodel --script FILE --capture DIR [--port-file PATH]
 *             [--deadline SECS] [--max-requests N]
 *
 * Binds 127.0.0.1:0 and serves requests sequentially (jichi issues one
 * model call at a time and every non-stall reply is Connection: close, so
 * a single-threaded accept loop is exactly faithful -- M209 decision D4).
 * The reply table (mm_core.h documents the format) selects a canned reply
 * per request by index and/or body substring; every full raw request is
 * captured to DIR/req.N so the sh driver can assert on the outgoing body
 * after the fact.
 *
 * The ephemeral port is announced twice: "PORT=N" on stdout, and (with
 * --port-file) written atomically to PATH (tmp + rename) for the driver's
 * poll loop.
 *
 * Request completion is decided ONLY by the incremental parser
 * (mm_http_feed, Content-Length-complete): the recv loop below can merely
 * feed bytes, so the ANECDOTES #18 truncated-request bug cannot recur
 * here. --deadline arms a self-watchdog alarm (exit 3) so a wedged run
 * cannot outlive its driver even on a box without timeout(1).
 *
 * Exit codes: 0 (served --max-requests / clean shutdown), 2 usage/script
 * error, 3 deadline. Test-only; never installed.
 */

#include "mm_core.h"
#include "tt.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <arpa/inet.h>
#include <netinet/in.h>

/* INADDR_LOOPBACK is NOT in POSIX.1-2001 -- <netinet/in.h> standardises
 * INADDR_ANY and INADDR_BROADCAST and not this one. Linux libcs expose it
 * regardless; FreeBSD guards it behind __BSD_VISIBLE, and this tree builds with
 * -D_POSIX_C_SOURCE=200112L, so on FreeBSD 15.1 it is simply undeclared and the
 * mock model -- and with it the whole smoke tier -- fails to compile (M459).
 * Same shape as _SC_NPROCESSORS_ONLN in jc_platform_posix.c, found by the same
 * row: a symbol four Linux libcs made look portable. */
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001UL
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const char *g_prog = "mockmodel";

static void usage(void)
{
    fprintf(stderr,
            "usage: %s --script FILE --capture DIR [--port-file PATH]\n"
            "       [--deadline SECS] [--max-requests N]\n", g_prog);
}

static void on_alarm(int sig)
{
    (void)sig;
    _exit(TT_EXIT_DEADLINE);
}

static char *read_file_all(const char *path, size_t *outlen)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    size_t cap = 8192, len = 0;
    if (f == NULL)
        return NULL;
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        size_t n;
        if (len + 4096 + 1 > cap) {
            char *nb;
            cap *= 2;
            nb = (char *)realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        n = fread(buf + len, 1, 4096, f);
        len += n;
        if (n < 4096)
            break;
    }
    fclose(f);
    buf[len] = '\0';
    if (outlen != NULL)
        *outlen = len;
    return buf;
}

/* Write the whole buffer (blocking socket; short writes retried). */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        long n = (long)write(fd, buf + off, len - off);
        if (n <= 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

/* The embed action (M213): parse the request's "input" (string or array)
 * and answer an OpenAI embeddings reply whose vector per input is
 * [count(word1), ..., 0.1] for the rule's space-separated words. The
 * response SHAPE depends on the request, which is why this cannot be a
 * canned reply-table body. */
static void send_embed_reply(int conn, const char *words,
                             const char *body)
{
    cJSON *req = cJSON_Parse(body);
    cJSON *input = NULL;
    cJSON *resp = cJSON_CreateObject();
    cJSON *data = cJSON_AddArrayToObject(resp, "data");
    cJSON *usage = cJSON_AddObjectToObject(resp, "usage");
    int i, ninputs = 0;

    if (usage != NULL)
        cJSON_AddNumberToObject(usage, "prompt_tokens", 1);
    if (req != NULL)
        input = cJSON_GetObjectItem(req, "input");
    if (input != NULL && cJSON_IsArray(input))
        ninputs = cJSON_GetArraySize(input);
    else if (input != NULL && cJSON_IsString(input))
        ninputs = 1;

    for (i = 0; i < ninputs; i++) {
        const cJSON *item = cJSON_IsArray(input)
            ? cJSON_GetArrayItem(input, i) : input;
        const char *text = (item != NULL && cJSON_IsString(item))
            ? item->valuestring : "";
        cJSON *entry = cJSON_CreateObject();
        cJSON *vec = cJSON_CreateArray();
        const char *w = words;
        cJSON_AddNumberToObject(entry, "index", (double)i);
        while (*w != '\0') {
            char word[128];
            size_t n = 0;
            while (*w == ' ' || *w == '\t')
                w++;
            while (*w != '\0' && *w != ' ' && *w != '\t' &&
                   n + 1 < sizeof(word))
                word[n++] = *w++;
            word[n] = '\0';
            if (n > 0)
                cJSON_AddItemToArray(vec,
                    cJSON_CreateNumber((double)mm_count_ci(text, word)));
        }
        cJSON_AddItemToArray(vec, cJSON_CreateNumber(0.1));
        cJSON_AddItemToObject(entry, "embedding", vec);
        cJSON_AddItemToArray(data, entry);
    }

    {
        char *json = cJSON_PrintUnformatted(resp);
        if (json != NULL) {
            char *out = NULL;
            size_t outlen = 0;
            if (mm_render_status_body(200, json, strlen(json),
                                      &out, &outlen) == 0) {
                send_all(conn, out, outlen);
                free(out);
            }
            cJSON_free(json);
        }
    }
    cJSON_Delete(resp);
    cJSON_Delete(req);
}

/* Hold a stalled connection open until the peer gives up (close/reset).
 * Never writes; returns when the socket dies. The --deadline alarm is the
 * backstop against a peer that never disconnects. */
static void hold_until_close(int fd)
{
    char sink[512];
    for (;;) {
        fd_set rf;
        int rc;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        rc = select(fd + 1, &rf, NULL, NULL, NULL);
        if (rc < 0)
            return;
        if (FD_ISSET(fd, &rf)) {
            long n = (long)read(fd, sink, sizeof(sink));
            if (n <= 0)
                return;
        }
    }
}

/* Capture the raw request to DIR/req.N. Best-effort: a capture failure is
 * reported on stderr but does not fail the serve loop (the driver's grep
 * will fail loudly instead). */
static void capture_request(const char *dir, int index,
                            const char *bytes, size_t len)
{
    char path[512];
    FILE *f;
    int n = jc_snprintf(path, sizeof(path), "%s/req.%d", dir, index);
    if (n < 0 || (size_t)n >= sizeof(path))
        return;
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "%s: cannot write %s\n", g_prog, path);
        return;
    }
    if (len > 0)
        fwrite(bytes, 1, len, f);
    fclose(f);
}

/* Read one full request from conn, capture it as req.<req_index>, select a
 * rule, and send the reply (holding the socket for stall rules). Returns 1
 * if a complete request was served, 0 if the peer never completed one --
 * the caller advances its counter only on 1, so an incomplete connection
 * never burns a req.N number (preserving pre-M216 numbering). */
static int serve_one(int conn, int req_index, const struct mm_script *script,
                     const char *capture_dir)
{
    struct mm_http http;
    const struct mm_rule *rule;
    const char *body;
    size_t body_len = 0;

    mm_http_init(&http);
    for (;;) {
        char chunk[4096];
        long n = (long)read(conn, chunk, sizeof(chunk));
        if (n <= 0)
            break;              /* peer closed / error: give up */
        if (mm_http_feed(&http, chunk, (size_t)n) != MM_HTTP_NEED_MORE)
            break;
    }
    if (http.state != MM_HTTP_COMPLETE) {
        mm_http_free(&http);
        return 0;
    }

    capture_request(capture_dir, req_index, http.buf, http.len);
    body = mm_http_body(&http, &body_len);
    rule = mm_select(script, body, body_len, req_index);

    if (rule != NULL && rule->delay_ms > 0) {
        struct timeval tv;
        tv.tv_sec = rule->delay_ms / 1000;
        tv.tv_usec = (rule->delay_ms % 1000) * 1000;
        select(0, NULL, NULL, NULL, &tv);
    }

    if (rule == NULL) {
        static const char nomatch[] =
            "HTTP/1.1 500 Status\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\nContent-Length: 34\r\n\r\n"
            "{\"error\":\"mockmodel: no rule hit\"}";
        fprintf(stderr, "%s: request %d matched no rule\n", g_prog, req_index);
        send_all(conn, nomatch, sizeof(nomatch) - 1);
    } else if (rule->action == MM_ACT_EMBED) {
        send_embed_reply(conn, rule->arg1, body);
    } else if (rule->action == MM_ACT_STATUS && rule->body_file != NULL) {
        size_t flen = 0;
        char *fbody = read_file_all(rule->body_file, &flen);
        if (fbody == NULL) {
            fprintf(stderr, "%s: cannot read body-file %s\n",
                    g_prog, rule->body_file);
        } else {
            char *resp = NULL;
            size_t rlen = 0;
            if (mm_render_status_body(rule->status, fbody, flen,
                                      &resp, &rlen) == 0) {
                send_all(conn, resp, rlen);
                free(resp);
            }
            free(fbody);
        }
    } else if (rule->action == MM_ACT_SSE_FILE) {
        size_t flen = 0;
        char *fbody = read_file_all(rule->arg1, &flen);
        if (fbody == NULL) {
            fprintf(stderr, "%s: cannot read sse-file %s\n",
                    g_prog, rule->arg1);
        } else {
            char *resp = NULL;
            size_t rlen = 0;
            if (mm_render_sse_body(fbody, flen, &resp, &rlen) == 0) {
                send_all(conn, resp, rlen);
                free(resp);
            }
            free(fbody);
        }
    } else {
        char *resp = NULL;
        size_t rlen = 0;
        if (mm_render_response(rule, &resp, &rlen) == 0) {
            send_all(conn, resp, rlen);
            free(resp);
            if (rule->action == MM_ACT_STALL_HEADER ||
                rule->action == MM_ACT_STALL_MID)
                hold_until_close(conn);
        }
    }

    mm_http_free(&http);
    return 1;
}

int main(int argc, char **argv)
{
    const char *script_path = NULL;
    const char *capture_dir = NULL;
    const char *port_file = NULL;
    long deadline = 0;
    long max_requests = 0;
    struct mm_script script;
    char err[256];
    char *script_text;
    int srv;
    struct sockaddr_in addr;
    socklen_t alen;
    int port;
    int req_index = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
        } else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            capture_dir = argv[++i];
        } else if (strcmp(argv[i], "--port-file") == 0 && i + 1 < argc) {
            port_file = argv[++i];
        } else if (strcmp(argv[i], "--deadline") == 0 && i + 1 < argc) {
            deadline = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--max-requests") == 0 && i + 1 < argc) {
            max_requests = strtol(argv[++i], NULL, 10);
        } else {
            usage();
            return TT_EXIT_USAGE;
        }
    }
    if (script_path == NULL || capture_dir == NULL) {
        usage();
        return TT_EXIT_USAGE;
    }

    script_text = read_file_all(script_path, NULL);
    if (script_text == NULL) {
        fprintf(stderr, "%s: cannot read %s\n", g_prog, script_path);
        return TT_EXIT_USAGE;
    }
    if (mm_script_parse(script_text, &script, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s: %s: %s\n", g_prog, script_path, err);
        free(script_text);
        return TT_EXIT_USAGE;
    }
    free(script_text);

    signal(SIGPIPE, SIG_IGN);
    if (deadline > 0) {
        /* Scale with the tier's timeout knob (M273): unscaled, this watchdog
         * shot the server out from under a healthy run on slow silicon, and
         * the driver then hung waiting for a reply that could never arrive. */
        signal(SIGALRM, on_alarm);
        alarm((unsigned)(deadline * tt_timeout_mult()));
    }

    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        fprintf(stderr, "%s: socket failed\n", g_prog);
        return TT_EXIT_USAGE;
    }
    {
        int one = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(srv, 8) != 0) {
        fprintf(stderr, "%s: bind/listen failed\n", g_prog);
        return TT_EXIT_USAGE;
    }
    alen = sizeof(addr);
    if (getsockname(srv, (struct sockaddr *)&addr, &alen) != 0) {
        fprintf(stderr, "%s: getsockname failed\n", g_prog);
        return TT_EXIT_USAGE;
    }
    port = (int)ntohs(addr.sin_port);

    if (port_file != NULL) {
        char tmp[512];
        FILE *f;
        if (jc_snprintf(tmp, sizeof(tmp), "%s.tmp", port_file) < 0)
            return TT_EXIT_USAGE;
        f = fopen(tmp, "w");
        if (f == NULL) {
            fprintf(stderr, "%s: cannot write %s\n", g_prog, tmp);
            return TT_EXIT_USAGE;
        }
        fprintf(f, "%d\n", port);
        fclose(f);
        if (rename(tmp, port_file) != 0) {
            fprintf(stderr, "%s: cannot rename %s\n", g_prog, tmp);
            return TT_EXIT_USAGE;
        }
    }
    printf("PORT=%d\n", port);
    fflush(stdout);

    for (;;) {
        int conn = accept(srv, NULL, NULL);
        if (conn < 0)
            continue;
        /* Advance the counter only when a full request was served, so an
         * incomplete connection never consumes a req.N (pre-M216 numbering).
         * A single-threaded sequential accept is faithful even for the
         * spawn_parallel drivers: a stalled child's connection is unblocked
         * in bounded time by the watchdog/abort closing it, and the merge
         * drivers' calls are quick -- verified 9/9, so no concurrent accept
         * was needed (D4's assumption corrected in M216). */
        if (serve_one(conn, req_index + 1, &script, capture_dir))
            req_index++;
        close(conn);

        if (max_requests > 0 && req_index >= max_requests)
            break;
    }

    close(srv);
    mm_script_free(&script);
    return TT_EXIT_OK;
}
