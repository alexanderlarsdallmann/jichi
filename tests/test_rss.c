/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_rss.c - the pure RSS/Atom -> text reducer (W4). No network. */

#include "jc_test.h"
#include "jc_rss.h"
#include "jc_str.h"

#include <string.h>

static const char RSS20[] =
    "<?xml version=\"1.0\"?>\n"
    "<rss version=\"2.0\"><channel>\n"
    "  <title>Example Blog</title>\n"
    "  <item>\n"
    "    <title>First &amp; Foremost</title>\n"
    "    <link>https://ex.com/1</link>\n"
    "    <pubDate>Mon, 01 Jan 2026 00:00:00 GMT</pubDate>\n"
    "    <description><![CDATA[<p>Hello <b>world</b>.</p>]]></description>\n"
    "  </item>\n"
    "  <item>\n"
    "    <title>Second Post</title>\n"
    "    <link>https://ex.com/2</link>\n"
    "    <description>Plain summary text.</description>\n"
    "  </item>\n"
    "</channel></rss>\n";

static const char ATOM[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
    "  <title>Atom Feed</title>\n"
    "  <entry>\n"
    "    <title>Atom Entry One</title>\n"
    "    <link href=\"https://ex.com/a1\" rel=\"alternate\"/>\n"
    "    <updated>2026-01-02T10:00:00Z</updated>\n"
    "    <summary>An atom summary.</summary>\n"
    "  </entry>\n"
    "</feed>\n";

void test_rss(void)
{
    struct jc_sb out;

    /* Detection. */
    JC_CHECK(jc_rss_looks_like_feed(RSS20) == 1);
    JC_CHECK(jc_rss_looks_like_feed(ATOM) == 1);
    JC_CHECK(jc_rss_looks_like_feed("<html><body>hi</body></html>") == 0);
    JC_CHECK(jc_rss_looks_like_feed(NULL) == 0);

    /* RSS 2.0: feed title + both items, entities decoded, CDATA+HTML stripped. */
    jc_sb_init(&out);
    jc_rss_to_text(RSS20, &out);
    JC_CHECK(out.data != NULL);
    JC_CHECK(strstr(out.data, "Example Blog") != NULL);
    JC_CHECK(strstr(out.data, "First & Foremost") != NULL);   /* &amp; decoded */
    JC_CHECK(strstr(out.data, "https://ex.com/1") != NULL);
    JC_CHECK(strstr(out.data, "Hello") != NULL &&
             strstr(out.data, "world") != NULL);              /* CDATA/HTML */
    JC_CHECK(strstr(out.data, "<b>") == NULL);                /* tags stripped */
    JC_CHECK(strstr(out.data, "Second Post") != NULL);
    JC_CHECK(strstr(out.data, "Plain summary text.") != NULL);
    jc_sb_free(&out);

    /* Atom: entry title, href-derived link, summary. */
    jc_sb_init(&out);
    jc_rss_to_text(ATOM, &out);
    JC_CHECK(strstr(out.data, "Atom Entry One") != NULL);
    JC_CHECK(strstr(out.data, "https://ex.com/a1") != NULL);  /* from href= */
    JC_CHECK(strstr(out.data, "An atom summary.") != NULL);
    jc_sb_free(&out);

    /* No items => nothing appended. */
    jc_sb_init(&out);
    jc_rss_to_text("<html>no feed here</html>", &out);
    JC_CHECK(out.len == 0);
    jc_sb_free(&out);
}
