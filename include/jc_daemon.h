/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_daemon.h - the warm-process ("daemon") request protocol.
 *
 * A persistent jichi process keeps config + MCP + LSP + index + prompt cache hot
 * across requests, amortizing cold-start latency and (on a cacheless backend)
 * cost. A client frames one newline-delimited JSON request per connection; the
 * server streams back the existing jc_agentjson event objects, then a `done`.
 *
 * This header holds the PURE request codec (parse/build), unit-tested offline.
 * The socket transport + warm-app loop live in main.c's run_daemon.
 */
#ifndef JC_DAEMON_H
#define JC_DAEMON_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"

/* The daemon protocol version carried as "v" on every request. */
#define JC_DAEMON_PROTO_V 1

/* M528: the largest request line the daemon will accept, in bytes.
 *
 * There was no limit at all: `daemon_read_line` read one byte at a time into an
 * unbounded buffer until a newline, so a client that connected and sent data
 * without ever sending '\n' made the server allocate until it died. Same uid or
 * not, a warm process that can be killed by a stuck client is a warm process you
 * cannot rely on -- and the protocol proposal's threat table (§10, "exhaust the
 * receiver") calls for a declared, enforced cap whose breach is an ERROR rather
 * than a truncation, because a silently truncated request is a request that
 * meant something else.
 *
 * 1 MiB: a pasted file in a prompt fits comfortably; a runaway does not. */
#define JC_DAEMON_MAX_LINE (1024L * 1024L)

enum jc_daemon_req_type {
    JC_DREQ_PROMPT,    /* run one agent turn                        */
    JC_DREQ_PING,      /* liveness check (server replies pong)      */
    JC_DREQ_SHUTDOWN,  /* ask the server to exit                    */
    JC_DREQ_HELLO,     /* M528: capability + auth-posture discovery */
    /* M529: the `assignment` verb group -- jichi's teaching features reachable
     * by other software (a course platform, a marking service, an editor
     * plugin) instead of only by shelling out to the CLI and parsing human
     * output. Read-and-grade only: see the `name` field below for why there is
     * no submit verb. */
    JC_DREQ_ASSIGN_LIST,
    JC_DREQ_ASSIGN_GET,
    JC_DREQ_ASSIGN_GRADE,
    JC_DREQ_UNKNOWN
};

/* M528: what the server tells a caller about itself, in reply to `hello`.
 *
 * WHY IT REPORTS THE MECHANISM. This socket's access control is its file mode
 * and nothing else, so a caller's real question is not "which version are you"
 * but "what is actually guarding what I just connected to". Two fields answer
 * it without flattering the implementation: `modeVerified` says the mode was
 * READ BACK after bind and found to be owner-only (not merely requested), and
 * `peercred` says whether the peer's kernel credentials are checked -- which
 * today is always false, because `SO_PEERCred`/`struct ucred` are hidden under
 * `_POSIX_C_SOURCE` on glibc and `getpeereid` is BSD-only. Claiming an
 * authentication that is not performed is worse than having none, so the field
 * exists in order to say `false`.
 *
 * There is exactly ONE limit because there is exactly one enforced: the request
 * line. A `maxPromptBytes` was drafted and removed -- the prompt shares the line
 * budget and has no separate cap, and advertising a limit nobody enforces is the
 * same dishonesty the `peercred` field exists to avoid.
 *
 * `hello` is ADDITIVE and OPTIONAL. docs/EMBEDDING.md declares the bare
 * `prompt`/`ping`/`shutdown` shapes Stable; requiring a handshake would break
 * that promise, so a client that never says hello keeps working exactly as
 * before. See proposals/2026-08-jichi-protocol.md §4 and §9. */
struct jc_daemon_hello {
    const char   *agent;         /* "jichi <version>"                        */
    long          max_line;      /* bytes accepted on one request line       */
    int           workers;       /* concurrent turns this server will run    */
    unsigned long uid;           /* the euid the server runs as              */
    int           mode_verified; /* socket mode read back and owner-only     */
    int           peercred;      /* peer credentials checked (0 today)       */
};

struct jc_daemon_req {
    enum jc_daemon_req_type type;
    const char *prompt;   /* PROMPT: the user turn (arena-owned)     */
    const char *cwd;      /* optional workspace to bind for the turn */
    const char *mode;     /* optional "chat"|"plan"|"auto", else NULL */
    /* M431g: the OUTPUT FORMAT, using run_headless's own codes -- 0 text, 1 json
     * (one terminal object), 2 jsonl (streamed events). It was a BOOLEAN `jsonl`,
     * so `format` could only say text-or-jsonl and `--output json` over --connect
     * was silently served as TEXT: the single-object contract, which
     * docs/EMBEDDING.md lists as Stable, was simply unavailable over the daemon.
     * Widening the field is additive on the wire (a new VALUE for `format`, not a
     * new key), and an old client sending "text"/"jsonl" still parses as before. */
    int         fmt;      /* 0 text, 1 json, 2 jsonl                  */
    /* M529: which assignment, for the ASSIGN_* verbs. A NAME, never a path --
     * `jc_assign_name_ok` refuses anything that expresses a location, because
     * grading RUNS the spec's own `verify` command and a caller who could name
     * a file could name one they wrote. The server resolves it inside
     * `<workspace>/docs/assignments/`. */
    const char *name;
};

/* Parse one request line into *out (strings arena-owned). Returns JC_ERR_PARSE
 * if `line` is not a JSON object, JC_ERR_INVALID if a PROMPT carries no prompt.
 * An unknown/missing "type" yields JC_DREQ_UNKNOWN (still JC_OK). */
jc_status jc_daemon_parse_request(const char *line, struct jc_daemon_req *out,
                                  struct jc_arena *a);

/* Build a request line (client side; malloc'd, caller frees). `cwd`/`mode` may
 * be NULL. A trailing newline is included so it can be written as-is. */
char *jc_daemon_build_prompt(const char *prompt, const char *cwd,
                             const char *mode, int fmt);

/* Build a bare control request line ("ping"/"shutdown"; malloc'd). */
char *jc_daemon_build_ctl(const char *type);

/* Build a `hello` request (client side; malloc'd, newline-framed). `client` may
 * be NULL. */
char *jc_daemon_build_hello(const char *client);

/* Build the `hello.ok` reply (server side; malloc'd, newline-framed). Pure, so
 * the posture fields are unit-testable without a socket. */
char *jc_daemon_build_hello_ok(const struct jc_daemon_hello *h);

/* Map a type enum to/from its wire string (never NULL for known types). */
const char *jc_daemon_type_name(enum jc_daemon_req_type t);

#ifdef __cplusplus
}
#endif
#endif /* JC_DAEMON_H */
