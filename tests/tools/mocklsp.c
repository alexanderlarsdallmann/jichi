/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* mocklsp.c - a stdio LSP server for the smoke tier (M938).
 *
 * Byte-framed JSON-RPC (Content-Length headers) - the LSP wire - unlike MCP's
 * newline framing that the sh mock in _smoke.sh speaks. Staging the real
 * diagnostic set 1.4 s after the empty one needs sub-second precision, so the
 * timing lives here, in C, on the MONOTONIC clock (the product code under test
 * is exactly the one that must not use the wall clock for this wait; a mock
 * that paced on time(NULL) would inherit the bug it is proving against).
 *
 * Modes (at most one of --version / --clean; the default is staged):
 *   (default)  staged: on EVERY textDocument/didOpen, publish an EMPTY
 *        diagnostic set immediately (main loop, synchronous), then the
 *        REAL set ~1.4 s later (a background thread, the re-index pass)
 *        -- a server that re-publishes after re-indexing. The 1.4 s sits
 *        between the old 1.0 s grace expiry and the 2.0 s grace of the
 *        fix, so the clock fix is the only thing that can read the file
 *        as dirty.
 *   --version  version-aware staged: like the default, but the re-index
 *        thread is also answered INLINE by a didOpen carrying a LARGER
 *        version than the last one it saw for that URI (the spec
 *        behaviour of a server that sees a newer document: it re-syncs
 *        at once); a didOpen at the SAME or a LOWER version gets only the
 *        empty set inline. The re-index thread still publishes ~1.4 s
 *        after each didOpen (a real server re-indexes after editing too).
 *        jichi's version counter is what makes the second edit's didOpen
 *        carry the newer version and earn the inline re-sync.
 *   --clean    publishes only an empty set, immediately (the control: a
 *        file with no errors must still read clean after the fix).
 *
 * --record FILE appends one line per didOpen, "OPEN <version> <uri>", so
 * the driver can verify what versions the client sent (the counter's own
 * teeth: with the fix the versions increase per connection; the
 * perturbation that hardcodes 1 shows 1, 1, ... in this file).
 *
 * Every mode answers `initialize` and `shutdown` (the client sends
 * initialize before any textDocument notification, and shutdown before
 * exit), and ignores `exit`, `initialized`, `didChange`, `didClose`.
 *
 * Exit codes: TT_EXIT_OK, TT_EXIT_USAGE (bad args), TT_EXIT_DEADLINE. The
 * --deadline watchdog (scaled by the tier's timeout knob, like every other
 * helper's, smoke_lint check 10) exists so a wedged run dies on its own.
 */

#include "tt.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ML_URI_MAX    300
#define ML_DOCS       8
#define ML_DELAY_MS   1400 /* the re-index pass: > the 1.0 s the old grace provably waits, < the 2.0 s grace of the fix */
#define ML_FAST_MS    600  /* --fast: the review's own reproduction (real set 0.6 s behind the empty one) */
#define ML_BODY_MAX   (1 << 16)

static volatile sig_atomic_t g_deadline = 0;
static void on_alarm(int sig)
{
    (void)sig;
    g_deadline = 1;
}

/* C89 environ: declare extern to access the process's environment. */
extern char **environ;

/* --- string append (the tier's no-snprintf discipline; see sockq) -------- */
static void ap(char *b, size_t n, size_t *k, const char *s)
{
    size_t l = strlen(s);
    if (n - *k > l) {
        memcpy(b + *k, s, l);
        *k += l;
    }
}
static void apn(char *b, size_t n, size_t *k, long v)
{
    char t[24];
    size_t i = 0, j = 0;
    if (v < 0) { t[i++] = '-'; v = -v; }
    if (v == 0) { t[i++] = '0'; }
    while (v > 0) { t[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > j && *k < n) { b[(*k)++] = t[--i]; }
}

/* --- one framed message to the client ------------------------------------ */
static pthread_mutex_t g_wmu = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_record; /* forward (declared in main, used by publish) */
static struct timespec g_start_mono; /* mock start; record's t= in ms */

static void send_body(const char *body)
{
    char hdr[64];
    size_t k = 0;
    ap(hdr, sizeof(hdr), &k, "Content-Length: ");
    apn(hdr, sizeof(hdr), &k, (long)strlen(body));
    ap(hdr, sizeof(hdr), &k, "\r\n\r\n");
    pthread_mutex_lock(&g_wmu);
    (void)write(1, hdr, k);
    (void)write(1, body, strlen(body));
    fflush(stdout);
    pthread_mutex_unlock(&g_wmu);
}

static void publish(const char *uri, int real)
{
    char b[1024];
    size_t k = 0;
    ap(b, sizeof(b), &k,
       "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
       "\"params\":{\"uri\":\"");
    ap(b, sizeof(b), &k, uri);
    ap(b, sizeof(b), &k, "\",\"diagnostics\":[");
    if (real) {
        ap(b, sizeof(b), &k,
           "{\"range\":{\"start\":{\"line\":0,\"character\":0},"
           "\"end\":{\"line\":0,\"character\":8}},\"severity\":1,"
           "\"source\":\"mocklsp\",\"code\":\"MOCK1\","
           "\"message\":\"MOCKLSP intentional error\"}");
    }
    ap(b, sizeof(b), &k, "]}}");
    if (g_record != NULL) {
        char tag[32];
        size_t tk = 0;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        ap(tag, sizeof(tag), &tk, real ? "PUBREAL " : "PUBEMPTY ");
        ap(tag, sizeof(tag), &tk, "t=");
        apn(tag, sizeof(tag), &tk,
            (long)((now.tv_sec - g_start_mono.tv_sec) * 1000 +
                   (now.tv_nsec - g_start_mono.tv_nsec) / 1000000));
        ap(tag, sizeof(tag), &tk, "|");
        /* apn left tag unterminated and fputs() would strlen() it: terminate
         * so the record line is exactly tk bytes. */
        tag[tk] = '\0';
        fputs(tag, g_record);
        fflush(g_record);
    }
    /* as in reply(): ap() never terminates b, and send_body() strlen()s it. */
    b[k] = '\0';
    send_body(b);
}

static void reply(long id, const char *result)
{
    char b[512];
    size_t k = 0;
    char idbuf[24];
    size_t ik = 0;
    ap(b, sizeof(b), &k, "{\"jsonrpc\":\"2.0\",\"id\":");
    apn(idbuf, sizeof(idbuf), &ik, id);
    /* apn writes the digits with NO terminator, and ap() below does strlen()
     * on idbuf. A single-digit id (the handshake starts at id 1) would read
     * idbuf[1] off the stack. Terminate it. */
    idbuf[ik] = '\0';
    ap(b, sizeof(b), &k, idbuf);
    ap(b, sizeof(b), &k, ",\"result\":");
    ap(b, sizeof(b), &k, result);
    ap(b, sizeof(b), &k, "}");
    /* b is built with ap(), which never writes a terminator; send_body() does
     * strlen(b) for BOTH the Content-Length and the write. Without the NUL the
     * frame length walks into uninitialised stack -- zero on x86-64 (correct by
     * luck), five stray bytes on the 16 KB-page Pi 500+ that broke the
     * initialise handshake (2026-10-15-pi500plus-row.md). */
    b[k] = '\0';
    send_body(b);
}

/* --- server state ---------------------------------------------------------- */
static int g_mode_version = 0; /* --version */
static int g_mode_clean = 0;   /* --clean */
static long g_delay_ms = ML_DELAY_MS; /* --fast lowers it to ML_FAST_MS */
static char *g_env_dump_path = NULL; /* --env-dump FILE path */

struct ml_doc { char *uri; long last_ver; int have; int seen; };
static struct ml_doc g_docs[ML_DOCS];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_open_count = 0;   /* main loop: a didOpen arrived (under g_mu) */
static int g_done = 0;         /* main loop finished; threads must exit */
static struct timespec g_last_open_mono; /* monotonic time of the LAST didOpen */
static int g_last_reindex = 0; /* doc_opened: the last didOpen should re-index */

/* --- the re-index thread: one publish ~ML_DELAY_MS after EVERY didOpen ---- */
static void *staged_thread(void *arg)
{
    struct timespec wait, rem;
    char *uri = NULL;
    int seen_count = 0;
    int reindex = 0;
    (void)arg;
    /* Snapshot the uri and the monotonic open time under the same lock, then
     * publish ML_DELAY_MS later. Repeats for every didOpen (a server
     * re-indexes after every edit), skipping ones that arrive after we shut
     * down. */
    for (;;) {
        pthread_mutex_lock(&g_mu);
        while (g_open_count == seen_count && !g_deadline && !g_done) {
            pthread_cond_wait(&g_cv, &g_mu);
        }
        if (g_deadline || (g_done && g_open_count == seen_count)) {
            pthread_mutex_unlock(&g_mu);
            break;
        }
        seen_count = g_open_count;
        reindex = g_last_reindex;
        if (g_open_count > 0) {
            if (uri != NULL) {
                free(uri);
            }
            uri = strdup(g_docs[0].uri);
            wait = g_last_open_mono;
        }
        pthread_mutex_unlock(&g_mu);
        if (uri == NULL) {
            continue;
        }
        wait.tv_sec += g_delay_ms / 1000;
        wait.tv_nsec += (long)(g_delay_ms % 1000) * 1000000L;
        if (wait.tv_nsec >= 1000000000L) {
            wait.tv_sec += 1;
            wait.tv_nsec -= 1000000000L;
        }
        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &rem);
            rem.tv_sec = wait.tv_sec - rem.tv_sec;
            rem.tv_nsec = wait.tv_nsec - rem.tv_nsec;
            if (rem.tv_nsec < 0) {
                rem.tv_sec -= 1;
                rem.tv_nsec += 1000000000L;
            }
            if (rem.tv_sec <= 0 && rem.tv_nsec <= 0) {
                break;
            }
            if (nanosleep(&rem, &rem) == 0) {
                break;
            }
            if (g_deadline || g_done) {
                break;
            }
        }
        if (g_deadline || g_done) {
            break;
        }
        /* publish the real set only if this didOpen was a re-index (default:
         * every open; --clean: none; --version: first or version-increase). */
        if (reindex) {
            publish(uri, 1);
        }
    }
    free(uri);
    return NULL;
}

/* --- doc bookkeeping (called under g_mu) ----------------------------------- */
/* Records the open and decides whether the server re-indexes it, via
 * g_last_reindex (consumed by the staged thread, which publishes the real set
 * delay ms later). default: every open; --clean: never; --version: the first
 * observation of a URI or one at a LARGER version -- a same/lower version is
 * an unchanged document (the teeth for jichi's incrementing counter). */
static void doc_opened(const char *uri, long ver)
{
    int i;
    int slot = -1;
    int reindex = 1;
    for (i = 0; i < ML_DOCS; i++) {
        if (g_docs[i].have && strcmp(g_docs[i].uri, uri) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < ML_DOCS; i++) {
            if (!g_docs[i].have) {
                g_docs[i].uri = strdup(uri);
                g_docs[i].last_ver = 0;
                g_docs[i].have = 1;
                g_docs[i].seen = 0;
                slot = i;
                break;
            }
        }
    }
    if (slot >= 0) {
        if (g_mode_clean) {
            reindex = 0;
        } else if (g_mode_version) {
            if (g_docs[slot].seen && ver <= g_docs[slot].last_ver) {
                reindex = 0;
            }
        }
        g_docs[slot].last_ver = ver;
        g_docs[slot].seen = 1;
    }
    g_last_reindex = reindex;
    g_open_count++;
    clock_gettime(CLOCK_MONOTONIC, &g_last_open_mono);
    pthread_cond_broadcast(&g_cv);
}

/* --- small JSON field extraction (well-formed LSP bodies) ------------------ */
static const char *field(const char *body, const char *fname)
{
    const char *p = strstr(body, fname);
    if (p == NULL) {
        return NULL;
    }
    p += strlen(fname);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != ':') {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int get_str_field(const char *body, const char *fname, char *out, long cap)
{
    const char *v = field(body, fname);
    long i = 0;
    if (v == NULL || *v != '"') {
        return 0;
    }
    v++;
    while (v[0] != '"' && v[0] != '\0' && i + 1 < cap) {
        out[i++] = v[0];
        v++;
    }
    out[i] = '\0';
    return i > 0 ? 1 : 0;
}

static long get_num_field(const char *body, const char *fname)
{
    const char *v = field(body, fname);
    long n = 0;
    int neg = 0;
    if (v == NULL) {
        return 0;
    }
    if (*v == '-') {
        neg = 1;
        v++;
    }
    if (*v < '0' || *v > '9') {
        return 0;
    }
    while (*v >= '0' && *v <= '9') {
        n = n * 10 + (*v - '0');
        v++;
    }
    return neg ? -n : n;
}

static void handle_body(char *body)
{
    const char *m = strstr(body, "\"method\"");
    long id;

    if (m != NULL && strstr(m, "\"initialize\"")) {
        id = get_num_field(body, "\"id\"");
        reply(id,
              "{\"capabilities\":{\"textDocumentSync\":1},"
              "\"serverInfo\":{\"name\":\"mocklsp\"}}");
        return;
    }
    if (m != NULL && strstr(m, "\"shutdown\"")) {
        id = get_num_field(body, "\"id\"");
        reply(id, "null");
        return;
    }
    if (m != NULL && strstr(m, "\"textDocument/didOpen\"")) {
        char uri[ML_URI_MAX];
        long ver;
        if (!get_str_field(body, "\"uri\"", uri, sizeof(uri))) {
            return;
        }
        ver = get_num_field(body, "\"version\"");
        if (g_record != NULL) {
            char line[ML_URI_MAX + 64];
            size_t k = 0;
            ap(line, sizeof(line), &k, "OPEN ");
            apn(line, sizeof(line), &k, ver);
            ap(line, sizeof(line), &k, " ");
            ap(line, sizeof(line), &k, uri);
            ap(line, sizeof(line), &k, "\n");
            fwrite(line, 1, k, g_record);
            fflush(g_record);
        }
        pthread_mutex_lock(&g_mu);
        doc_opened(uri, ver);
        pthread_mutex_unlock(&g_mu);
        /* the stale/first pass: the EMPTY set immediately on every didOpen
         * (the server's initial scan, before it has indexed). The re-index
         * (the real set) is published by the staged thread, g_delay_ms later,
         * for every open the server re-indexes (doc_opened decided via
         * g_last_reindex; --clean never does). */
        publish(uri, 0);
        return;
    }
    /* initialized, exit, didChange, didClose, and anything else: no reply. */
}

int main(int argc, char **argv)
{
    long deadline = 0;
    char *rec_path = NULL;
    char *buf = NULL;
    long bufn = 0, buflen = 0;
    long content_length = 0;
    int in_header = 1;
    int i;
    pthread_t staged = 0;
    int have_staged = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--deadline") == 0 && i + 1 < argc) {
            deadline = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--version") == 0) {
            g_mode_version = 1;
        } else if (strcmp(argv[i], "--clean") == 0) {
            g_mode_clean = 1;
        } else if (strcmp(argv[i], "--fast") == 0) {
            g_delay_ms = ML_FAST_MS; /* the review's 0.6 s reproduction */
        } else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            rec_path = argv[++i];
        } else if (strcmp(argv[i], "--env-dump") == 0 && i + 1 < argc) {
            g_env_dump_path = argv[++i];
        } else {
            fprintf(stderr, "mocklsp: unknown arg %s\n", argv[i]);
            return TT_EXIT_USAGE;
        }
    }
    if (rec_path != NULL) {
        g_record = fopen(rec_path, "a");
        if (g_record == NULL) {
            fprintf(stderr, "mocklsp: cannot open record file %s\n", rec_path);
            return TT_EXIT_USAGE;
        }
    }

    /* Write the environment dump if requested, before any child threads start. */
    if (g_env_dump_path != NULL) {
        FILE *f = fopen(g_env_dump_path, "w");
        if (f == NULL) {
            fprintf(stderr, "mocklsp: cannot open env-dump file %s\n", g_env_dump_path);
            return TT_EXIT_USAGE;
        }
        for (i = 0; environ[i] != NULL; i++) {
            fprintf(f, "%s\n", environ[i]);
        }
        fclose(f);
    }

    signal(SIGPIPE, SIG_IGN);
    clock_gettime(CLOCK_MONOTONIC, &g_start_mono);
    if (deadline > 0) {
        signal(SIGALRM, on_alarm);
        alarm((unsigned)(deadline * tt_timeout_mult()));
    }

    /* the re-index thread runs in the staged and --version modes (a server
     * re-indexes after every edit); --clean never re-publishes. */
    if (!g_mode_clean) {
        if (pthread_create(&staged, NULL, staged_thread, NULL) == 0) {
            have_staged = 1;
        }
    }

    for (;;) {
        char chunk[4096];
        ssize_t n;
        if (g_deadline) {
            break;
        }
        n = read(0, chunk, sizeof(chunk));
        if (n <= 0) {
            break; /* EOF: the client closed after shutdown/exit. */
        }
        if (buflen + n + 1 > bufn) {
            long nb = bufn ? bufn * 2 : (4 * 1024);
            char *nb2 = (char *)realloc(buf, nb);
            if (nb2 == NULL) {
                break;
            }
            buf = nb2;
            bufn = nb;
        }
        memcpy(buf + buflen, chunk, (size_t)n);
        buflen += n;
        buf[buflen] = '\0';
        /* parse the header block, then whole frames, from the front. */
        for (;;) {
            if (in_header) {
                char *nl = (char *)memchr(buf, '\n', (size_t)buflen);
                if (nl == NULL) {
                    break; /* need more header data */
                }
                {
                    long linelen = (long)(nl - buf);
                    int blank;
                    *nl = '\0';
                    blank = (linelen == 0) ||
                            (linelen == 1 && buf[0] == '\r');
                    if (blank) {
                        in_header = 0; /* the blank line ends the header */
                    } else if (strncmp(buf, "Content-Length:", 15) == 0) {
                        content_length = strtol(buf + 15, NULL, 10);
                    }
                    memmove(buf, nl + 1, (size_t)(buflen - linelen - 1));
                    buflen -= linelen + 1;
                    buf[buflen] = '\0';
                }
            } else {
                if (content_length > 0 && buflen >= content_length) {
                    char body[ML_BODY_MAX];
                    if (content_length > ML_BODY_MAX - 1) {
                        content_length = 0;
                        in_header = 1; /* overlong: resync on next header */
                        break;
                    }
                    memcpy(body, buf, (size_t)content_length);
                    body[content_length] = '\0';
                    handle_body(body);
                    memmove(buf, buf + content_length,
                            (size_t)(buflen - content_length));
                    buflen -= content_length;
                    buf[buflen] = '\0';
                    content_length = 0;
                    in_header = 1;
                    if (g_deadline) {
                        break;
                    }
                } else {
                    break; /* need the rest of the body */
                }
            }
        }
    }

    /* tell the thread the main loop is done and wake it; join before
     * returning so it cannot write to the client pipe after it closes. */
    pthread_mutex_lock(&g_mu);
    g_done = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    if (have_staged) {
        pthread_join(staged, NULL);
    }
    {
        int i2;
        for (i2 = 0; i2 < ML_DOCS; i2++) {
            if (g_docs[i2].have) {
                free(g_docs[i2].uri);
            }
        }
    }
    if (g_record != NULL) {
        fclose(g_record);
    }
    free(buf);
    return g_deadline ? TT_EXIT_DEADLINE : TT_EXIT_OK;
}
