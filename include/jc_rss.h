/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rss.h - RSS 2.0 / Atom feed -> plain text (pure, no network, no XML lib).
 *
 * A hand-rolled feed reducer in the same spirit as jc_docs_html_to_text: it scans
 * <item> (RSS) and <entry> (Atom) elements and emits a compact, readable digest
 * (title, date, link, and a short description snippet) so a feed can feed the
 * agent as @rss:<url> context or a docs RAG source (docs entry with type "rss").
 * Entity/CDATA decoding + any HTML in the description body is handled by reusing
 * jc_docs_html_to_text. Pure and unit-tested with fixture feeds.
 */
#ifndef JC_RSS_H
#define JC_RSS_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_str.h"

/* Append a plain-text digest of the feed `xml` to `out`. Recognizes RSS 2.0
 * (<item>) and Atom (<entry>); per item emits title, date, link, and a bounded
 * description snippet. Caps the number of items rendered. A no-op on NULL input
 * or a document with no items (appends nothing). */
void jc_rss_to_text(const char *xml, struct jc_sb *out);

/* Heuristic: does this text look like an RSS/Atom feed (rather than HTML)?
 * True when a <rss, <feed, or <rdf root (or an <item>/<entry> element) appears
 * near the start. Pure; used to auto-route a fetched page. */
int jc_rss_looks_like_feed(const char *xml);

#ifdef __cplusplus
}
#endif
#endif /* JC_RSS_H */
