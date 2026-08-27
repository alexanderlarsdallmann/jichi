/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_pdf.c - PDF extraction helpers (jc_pdf). The extractor is exercised with
 * stand-in commands so no real pdftotext / PDF is needed. */

#include "jc_test.h"
#include "jc_pdf.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

static void test_is_pdf(void)
{
    JC_CHECK(jc_pdf_is_pdf("a.pdf") == 1);
    JC_CHECK(jc_pdf_is_pdf("/x/y/report.PDF") == 1);
    JC_CHECK(jc_pdf_is_pdf("Mixed.Pdf") == 1);
    JC_CHECK(jc_pdf_is_pdf("a.txt") == 0);
    JC_CHECK(jc_pdf_is_pdf("notpdf") == 0);
    JC_CHECK(jc_pdf_is_pdf("x.pdf.bak") == 0);
    JC_CHECK(jc_pdf_is_pdf(".pdf") == 1);
    JC_CHECK(jc_pdf_is_pdf(NULL) == 0);
}

static void test_extract(void)
{
    struct jc_sb sb;

    /* A missing extractor is reported as not-found (not a crash / garbage). */
    jc_sb_init(&sb);
    JC_CHECK(jc_pdf_extract("/x/y.pdf", "jichi_no_such_extractor_zzz", 4096, &sb,
                            NULL) == JC_ERR_NOTFOUND);
    jc_sb_free(&sb);

    /* A stand-in command runs as `<cmd> <path> -`; its stdout is captured.
     * `echo HELLO_PDF -` prints the path arg, proving the dispatch + capture. */
    jc_sb_init(&sb);
    JC_CHECK(jc_pdf_extract("HELLO_PDF", "echo", 4096, &sb, NULL) == JC_OK);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "HELLO_PDF") != NULL);
    jc_sb_free(&sb);

    /* A non-zero exit (here: `false`) maps to a provider error. */
    jc_sb_init(&sb);
    JC_CHECK(jc_pdf_extract("x.pdf", "false", 4096, &sb, NULL)
             == JC_ERR_PROVIDER);
    jc_sb_free(&sb);
}

void test_pdf(void)
{
    test_is_pdf();
    test_extract();
}
