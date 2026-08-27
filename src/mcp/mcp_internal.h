/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* mcp_internal.h - MCP connection + transport vtable (internal to src/mcp).
 *
 * A connection owns a transport (stdio or http). The protocol lifecycle in
 * jc_mcp.c is transport-agnostic: it builds JSON-RPC lines with the pure
 * builders and hands them to vt->request / vt->notify, which deal with framing
 * and pairing responses to requests.
 */
#ifndef JC_MCP_INTERNAL_H
#define JC_MCP_INTERNAL_H

#include "jc_mcp.h"

struct jc_mcp_conn;

struct jc_mcp_transport_vt {
    /* Send the request `line` (compact JSON, no newline) carrying numeric id
     * `id`, then read and return its matching response message as malloc'd
     * JSON text in *resp_out (caller frees). */
    jc_status (*request)(struct jc_mcp_conn *c, const char *line, long id,
                         char **resp_out);
    /* Send a notification `line` (no response expected). */
    jc_status (*notify)(struct jc_mcp_conn *c, const char *line);
    /* Tear down the transport and free its state. */
    void (*close)(struct jc_mcp_conn *c);
};

struct jc_mcp_conn {
    char                             *name;       /* server logical name      */
    long                              next_id;    /* JSON-RPC id counter      */
    int                               tool_count; /* set after tools/list     */
    const struct jc_mcp_transport_vt *vt;
    void                             *t;          /* transport state          */
    volatile int                     *abort;      /* SIGINT flag (may be NULL) */
};

/* Allocate a bare connection with `name` copied; transport fills vt/t. */
struct jc_mcp_conn *jc_mcp_conn_alloc(const char *name, volatile int *abort);

/* Next JSON-RPC request id for this connection. */
long jc_mcp_conn_next_id(struct jc_mcp_conn *c);

/* Open a transport for `cfg`, returning a ready connection (vt/t set) in
 * *out. The protocol handshake is run separately by the caller. */
jc_status jc_mcp_stdio_open(struct jc_mcp_conn **out,
                            const struct jc_mcp_server_cfg *cfg,
                            volatile int *abort);
jc_status jc_mcp_http_open(struct jc_mcp_conn **out,
                           const struct jc_mcp_server_cfg *cfg,
                           volatile int *abort);

#endif /* JC_MCP_INTERNAL_H */
