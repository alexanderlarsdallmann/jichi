/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_checkinred.c - the checkin redactor unit tests (M1224, stretch S1). */

#include "jc_test.h"
#include "jc_hp_checkinred.h"

void test_checkinred(void);   /* the suite's entry (missing-prototypes) */

static void test_boundary_40(void);
static void test_boundary_39(void);
static void test_split_non_class(void);
static void test_one_pass_no_rescan(void);
static void test_truncation(void);
static void test_null_input(void);
static void test_empty_input(void);

void test_checkinred(void)
{
    test_boundary_40();
    test_boundary_39();
    test_split_non_class();
    test_one_pass_no_rescan();
    test_truncation();
    test_null_input();
    test_empty_input();
}

/* C8: the 40-char boundary is the redaction threshold. A run of exactly 40
 * chars from [A-Za-z0-9_-] becomes <r>; a run of 39 stays as-is. */
static void test_boundary_40(void)
{
    char out[128];
    const char *canary40 = "0123456789abcdef0123456789abcdef01234567"; /* 40 hex chars */

    /* A 40-char run becomes <r> */
    int rc = jc_checkinred(canary40, out, sizeof(out));
    JC_CHECK(rc == 0);
    JC_CHECK_STR(out, "<r>");

    /* Verify the input was not modified */
    JC_CHECK_STR(canary40, "0123456789abcdef0123456789abcdef01234567");
}

/* A run of 39 chars stays as-is (below threshold). */
static void test_boundary_39(void)
{
    char out[128];
    const char *short39 = "0123456789abcdef0123456789abcdef0123456"; /* 39 hex chars */

    int rc = jc_checkinred(short39, out, sizeof(out));
    JC_CHECK(rc == 0);
    JC_CHECK_STR(out, short39); /* passes through unchanged */
}

/* A run of 40+ chars followed by a non-class char: the non-class char is copied,
 * then a new run starts. */
static void test_split_non_class(void)
{
    char out[256];

    /* 44-char run, then a space, then another 44-char run */
    const char *in = "0123456789abcdef0123456789abcdef0123456789ab text 0123456789abcdef0123456789abcdef0123456789ab";
    int rc = jc_checkinred(in, out, sizeof(out));
    JC_CHECK(rc == 0);
    /* Each 44-char run becomes <r> */
    JC_CHECK_STR(out, "<r> text <r>");
}

/* The redactor is ONE PASS: <r> output is never re-scanned. A <r> followed by
 * more class chars does not extend the redaction. */
static void test_one_pass_no_rescan(void)
{
    char out[256];

    /* <r> is NOT in the class [A-Za-z0-9_-], so after <r> a new run starts */
    const char *in = "0123456789abcdef0123456789abcdef01234567<rsome text";
    int rc = jc_checkinred(in, out, sizeof(out));
    JC_CHECK(rc == 0);
    /* The first 40 chars become <r>, the rest is copied */
    JC_CHECK_STR(out, "<r><rsome text");
}

/* Output truncation: if the output buffer is too small, returns -1. */
static void test_truncation(void)
{
    char tiny[8]; /* Only 8 bytes */
    char too_small[2]; /* Only 2 bytes, needs 3 for <r> + NUL */
    const char *in = "0123456789abcdef0123456789abcdef01234567"; /* 40 chars -> <r> */
    int rc;

    /* <r> needs 3 bytes, tiny has 8 (with NUL), should fit */
    rc = jc_checkinred(in, tiny, sizeof(tiny));
    JC_CHECK(rc == 0);
    JC_CHECK_STR(tiny, "<r>");

    /* Now test with a buffer too small */
    rc = jc_checkinred(in, too_small, sizeof(too_small));
    JC_CHECK(rc == -1); /* truncation */
}

/* NULL input returns empty string. */
static void test_null_input(void)
{
    char out[32];
    int rc = jc_checkinred(NULL, out, sizeof(out));
    JC_CHECK(rc == 0);
    JC_CHECK_STR(out, "");
}

/* Empty input returns empty string. */
static void test_empty_input(void)
{
    char out[32];
    const char *in = "";
    int rc = jc_checkinred(in, out, sizeof(out));
    JC_CHECK(rc == 0);
    JC_CHECK_STR(out, "");
}
