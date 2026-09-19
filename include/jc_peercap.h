/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_peercap.h - the bound on one line from a peer, and why it announces itself.
 *
 * jichi reads newline-delimited JSON from two peers it does not control: an MCP
 * server over stdio (src/mcp/jc_mcp_stdio.c) and an ACP client over jichi's own
 * stdin (src/acp/jc_acp.c). Both accumulated an inbound message into a growable
 * builder with no byte bound, so a peer that streams without ever sending a
 * newline grows it until the OOM killer fires. Found by the 2026-08-27 hardening
 * survey, reported at M609 with the LSP framer's header block fixed and these
 * two left, and built at M659.
 *
 * ONE NUMBER, not two. The two protocols do not have to agree -- but a reader
 * asking "how big may a peer's line be?" should find one answer, and two
 * constants that drift apart is the shape M296 forbids. 8 MiB is generous
 * against real traffic (a large tool result is a few hundred KB) and small
 * enough that hitting it is a protocol violation rather than a busy day.
 *
 * AND IT SAYS SO WHEN IT FIRES. That is the part worth stating as a rule rather
 * than as an implementation detail: **a cap that says nothing is a cap nobody
 * can test, and nobody can debug either.** The M609 rows were parked for
 * milestones behind "the born-red test needs a memory-pressure harness", which
 * was a claim about the test and was wrong -- the RSS was never the property.
 * What makes the bound observable is that crossing it produces a distinct,
 * named failure, and that is what the driver reads. The same sentence answers
 * the tool-output row's standing revisit condition (M326w): "a way to make the
 * adaptation visible at the point it happens -- the truncation notice naming
 * the reason, not just the byte count."
 */
#ifndef JC_PEERCAP_H
#define JC_PEERCAP_H

/* Largest single newline-delimited message accepted from a peer, in bytes. */
#define JC_PEER_LINE_MAX 8388608L

#endif /* JC_PEERCAP_H */
