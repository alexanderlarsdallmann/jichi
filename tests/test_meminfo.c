/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_meminfo.c - self-RSS status parsing (M180). */

#include "jc_test.h"
#include "jc_meminfo.h"

static const char *STATUS =
    "Name:\tjichi\n"
    "Umask:\t0022\n"
    "State:\tR (running)\n"
    "VmPeak:\t   20000 kB\n"
    "VmSize:\t   19000 kB\n"
    "VmHWM:\t   12288 kB\n"
    "VmRSS:\t   10240 kB\n"
    "Threads:\t1\n";

static void test_parse(void)
{
    long rss = -1;
    long hwm = -1;

    JC_CHECK(jc_meminfo_parse(STATUS, &rss, &hwm) == 1);
    JC_CHECK(rss == 10240);
    JC_CHECK(hwm == 12288);

    /* Either out pointer may be NULL. */
    rss = -1;
    JC_CHECK(jc_meminfo_parse(STATUS, &rss, NULL) == 1);
    JC_CHECK(rss == 10240);

    /* A key must match at line start: "VmRSS" inside another token or a
     * text without the field reports not-found, values zeroed. */
    rss = -1;
    hwm = -1;
    JC_CHECK(jc_meminfo_parse("Name:\tx\nXVmRSS:\t5 kB\n", &rss, &hwm) == 0);
    JC_CHECK(rss == 0 && hwm == 0);
    JC_CHECK(jc_meminfo_parse(NULL, &rss, &hwm) == 0);
}

/* The illumos hazard, measured instead of reasoned (M647).
 *
 * PLATFORMS.md's Solaris/illumos analysis says /proc/self/status is PRESENT
 * there and is a binary `pstatus_t`, so `fopen` SUCCEEDS where a reader might
 * hope it had failed -- and then predicts that `jc_meminfo_parse` "hunts for
 * VmRSS: in binary bytes, finds nothing and reports zero -- no data rather than
 * wrong data, which is the right failure but is reasoned from the source, not
 * measured." Nobody has an illumos box, but the prediction is about a pure
 * function over bytes, so it is testable here, on Linux, today. That is the
 * whole point: a portability claim that can be reduced to a pure core should be
 * tested rather than asserted, and only what genuinely needs the kernel should
 * wait for the kernel.
 *
 * What is NOT tested here: that illumos's fopen succeeds, that its pstatus_t
 * has this shape, or that jc_meminfo_self's fread behaves the same there. Those
 * need the platform. The row stays NEVER COMPILED. */
static void test_binary_status(void)
{
    /* A pstatus_t-shaped buffer: little-endian words, embedded NULs, and -- the
     * part that matters -- stray 0x0A bytes, so field_kb's line walk really does
     * iterate rather than bailing on the first pass. */
    static const char PSTATUS[] =
        "\x01\x00\x00\x00\x2a\x00\x00\x00"
        "\x0a\xff\x00\x00\x10\x27\x00\x00"
        "\x0a\x00\x0a\x00\x80\x3e\x00\x00";
    long rss = -1;
    long hwm = -1;

    JC_CHECK(jc_meminfo_parse(PSTATUS, &rss, &hwm) == 0);
    JC_CHECK(rss == 0 && hwm == 0);

    /* The mechanism that protects us is the NUL, and it is worth pinning:
     * bytes that DO spell VmRSS: are unreachable once an embedded NUL precedes
     * them, so the answer stays "no data" rather than becoming a number read out
     * of a binary struct. If this ever reports 99999, the reader has started
     * scanning past NULs and illumos would get a fabricated RSS. */
    rss = -1;
    hwm = -1;
    JC_CHECK(jc_meminfo_parse("\x01\x02\n\0VmRSS:\t99999 kB\n", &rss, &hwm) == 0);
    JC_CHECK(rss == 0);

    /* And the converse, so this pair cannot both pass vacuously: the same bytes
     * with the NUL removed ARE found. This is the control -- without it, a
     * parser that found nothing anywhere would satisfy every check above. */
    rss = -1;
    JC_CHECK(jc_meminfo_parse("\x01\x02\nVmRSS:\t99999 kB\n", &rss, &hwm) == 1);
    JC_CHECK(rss == 99999);
}

static void test_self(void)
{
    long rss = 0;
    long hwm = 0;

    /* On Linux (the only supported platform) /proc/self/status exists and a
     * running test binary is certainly resident. Tolerate absence (rc 0)
     * for exotic build hosts, but when present the numbers must be sane. */
    if (jc_meminfo_self(&rss, &hwm)) {
        JC_CHECK(rss > 0);
        /* hwm == 0 is jc_meminfo_parse's documented "VmHWM absent" sentinel: it
         * zeroes the out-param and fills it only `if (hwm_found)`. Cygwin's
         * /proc/self/status carries no VmHWM line, so a bare `hwm >= rss`
         * asserted something the module never promised, and failed there (M477).
         * Where a high-water mark IS reported it must still be >= current RSS. */
        JC_CHECK(hwm == 0 || hwm >= rss);
    }
}

void test_meminfo(void)
{
    test_parse();
    test_binary_status();
    test_self();
}
