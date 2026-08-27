/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* xdrive - inject real keystrokes into a real X11 terminal emulator, for the
 * Tier V row V6 ("terminal reality") in docs/plans/2026-07-hardware-testing.md.
 *
 * Everything else in tests/tools drives jichi through a pty. That is the right
 * instrument for the line editor's logic, and it is what tests/smoke/paste.sh,
 * typed.sh and typeahead.sh already use -- but it cannot answer what a real
 * terminal *emulator* does: whether xterm's bracketed paste wraps the way M156
 * assumes, whether a window resize delivers the SIGWINCH the redraw expects,
 * whether Ctrl-C from a keyboard arrives as the same thing a pty write does.
 * V6 exists for those questions, and this tool is how they get asked without a
 * human at the keyboard.
 *
 * NOT part of any automatic suite: it needs a running X server, so it is built
 * by `make xdrive` and used by scripts/tier-v-terminals.sh, never by
 * `make test`, `make smoke` or `make ci`.
 *
 * libX11 and libXtst are loaded with dlopen at RUNTIME and their handful of
 * entry points hand-declared here, so the project acquires no X build
 * dependency and no header requirement: a machine without X simply cannot run
 * the binary, and every other machine builds it with `cc -ldl`.
 *
 * Usage (commands compose left to right, one X connection for all of them):
 *
 *   xdrive focus <winid>              raise the window and give it input focus
 *   xdrive type <text>                type text at full speed (a burst)
 *   xdrive slow <ms> <text>           type text with <ms> between characters
 *   xdrive key <spec>                 one key, e.g. Return / ctrl+c / ctrl+shift+v
 *   xdrive click <button>             1 left, 2 middle, 3 right
 *   xdrive move <x> <y>               warp the pointer
 *   xdrive resize <winid> <w> <h>     resize the window in PIXELS -- the
 *                                     emulator recomputes rows/cols and the
 *                                     kernel delivers SIGWINCH, which is the
 *                                     only way to test a real resize
 *   xdrive sleep <ms>                 wait
 *   xdrive probe                      report whether XTEST is usable, then exit
 *
 *   xdrive focus 0x3400007 slow 40 "hello" key Return sleep 500
 *
 * In <text>, \n is Return and \t is Tab, so a multi-line block can be typed in
 * one argument. C89, no warnings under -pedantic -Wall -Wextra.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>

/* X types, hand-declared -- we never include a header. These are the stable
 * public ABI: Window/KeySym are XID/unsigned long, KeyCode is unsigned char. */
typedef void *xdisplay;
typedef unsigned long xwindow;
typedef unsigned long xkeysym;
typedef unsigned char xkeycode;

#define XD_REVERT_TO_PARENT 2
#define XD_CURRENT_TIME     0UL

static xdisplay (*p_open)(const char *);
static int (*p_close)(xdisplay);
static int (*p_flush)(xdisplay);
static int (*p_sync)(xdisplay, int);
static int (*p_query_ext)(xdisplay, const char *, int *, int *, int *);
static xkeysym (*p_str2ks)(const char *);
static xkeycode (*p_ks2kc)(xdisplay, xkeysym);
static xkeysym (*p_kc2ks)(xdisplay, xkeycode, int);
static int (*p_set_focus)(xdisplay, xwindow, int, unsigned long);
static int (*p_raise)(xdisplay, xwindow);
static int (*p_resize)(xdisplay, xwindow, unsigned int, unsigned int);
static int (*p_fake_key)(xdisplay, unsigned int, int, unsigned long);
static int (*p_fake_button)(xdisplay, unsigned int, int, unsigned long);
static int (*p_fake_motion)(xdisplay, int, int, int, unsigned long);

static xdisplay dpy;

static void die(const char *msg)
{
    fprintf(stderr, "xdrive: %s\n", msg);
    exit(1);
}

/* dlsym through a union-free cast: ISO C forbids object<->function pointer
 * conversion, and -pedantic says so, hence the memcpy. */
static void bind_sym(void *lib, const char *name, void *slot, int required)
{
    void *sym = dlsym(lib, name);
    if (!sym && required) {
        fprintf(stderr, "xdrive: missing symbol %s\n", name);
        exit(1);
    }
    memcpy(slot, &sym, sizeof sym);
}

static void x_connect(void)
{
    void *x11, *xtst;
    int maj = 0, ev = 0, err = 0;

    x11 = dlopen("libX11.so.6", RTLD_NOW);
    if (!x11) die("libX11.so.6 not loadable -- is this an X machine?");
    xtst = dlopen("libXtst.so.6", RTLD_NOW);
    if (!xtst) die("libXtst.so.6 not loadable (package libxtst6)");

    bind_sym(x11, "XOpenDisplay", &p_open, 1);
    bind_sym(x11, "XCloseDisplay", &p_close, 1);
    bind_sym(x11, "XFlush", &p_flush, 1);
    bind_sym(x11, "XSync", &p_sync, 1);
    bind_sym(x11, "XQueryExtension", &p_query_ext, 1);
    bind_sym(x11, "XStringToKeysym", &p_str2ks, 1);
    bind_sym(x11, "XKeysymToKeycode", &p_ks2kc, 1);
    bind_sym(x11, "XKeycodeToKeysym", &p_kc2ks, 1);
    bind_sym(x11, "XSetInputFocus", &p_set_focus, 1);
    bind_sym(x11, "XRaiseWindow", &p_raise, 1);
    bind_sym(x11, "XResizeWindow", &p_resize, 1);
    bind_sym(xtst, "XTestFakeKeyEvent", &p_fake_key, 1);
    bind_sym(xtst, "XTestFakeButtonEvent", &p_fake_button, 1);
    bind_sym(xtst, "XTestFakeMotionEvent", &p_fake_motion, 1);

    dpy = p_open(NULL);
    if (!dpy) die("XOpenDisplay failed -- is DISPLAY set and reachable?");
    if (!p_query_ext(dpy, "XTEST", &maj, &ev, &err))
        die("the X server has no XTEST extension");
}

static void nap_ms(long ms)
{
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec = ms / 1000L;
    ts.tv_nsec = (ms % 1000L) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Does this keysym sit in the shifted position of its key? */
static int needs_shift(xkeysym ks, xkeycode kc)
{
    return p_kc2ks(dpy, kc, 0) != ks;
}

static void tap_keysym(xkeysym ks, long delay_ms)
{
    xkeycode kc, shift_kc;
    int shifted;

    kc = p_ks2kc(dpy, ks);
    if (kc == 0) {
        fprintf(stderr, "xdrive: no keycode for keysym 0x%lx (skipped)\n", ks);
        return;
    }
    shifted = needs_shift(ks, kc);
    shift_kc = p_ks2kc(dpy, p_str2ks("Shift_L"));

    if (shifted) p_fake_key(dpy, (unsigned int)shift_kc, 1, XD_CURRENT_TIME);
    p_fake_key(dpy, (unsigned int)kc, 1, XD_CURRENT_TIME);
    p_fake_key(dpy, (unsigned int)kc, 0, XD_CURRENT_TIME);
    if (shifted) p_fake_key(dpy, (unsigned int)shift_kc, 0, XD_CURRENT_TIME);
    p_flush(dpy);
    nap_ms(delay_ms);
}

/* ASCII printables share their code point with their keysym; the two escapes
 * we care about are the ones a pasted block contains. */
static xkeysym keysym_for_char(char c)
{
    if (c == '\n') return 0xFF0DUL;      /* Return */
    if (c == '\t') return 0xFF09UL;      /* Tab    */
    if (c == '\033') return 0xFF1BUL;    /* Escape */
    return (xkeysym)(unsigned char)c;
}

static void type_text(const char *s, long delay_ms)
{
    size_t i;
    for (i = 0; s[i]; i++) {
        if (s[i] == '\\' && s[i + 1] == 'n') { tap_keysym(0xFF0DUL, delay_ms); i++; continue; }
        if (s[i] == '\\' && s[i + 1] == 't') { tap_keysym(0xFF09UL, delay_ms); i++; continue; }
        tap_keysym(keysym_for_char(s[i]), delay_ms);
    }
}

/* "ctrl+shift+v", "Return", "c" -- modifiers held around a single key. */
static void press_spec(const char *spec)
{
    char buf[256];
    char *tok, *last = NULL;
    xkeycode mods[4];
    int nmods = 0, i;
    xkeysym ks;
    xkeycode kc;

    if (strlen(spec) >= sizeof buf) die("key spec too long");
    strcpy(buf, spec);

    for (tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
        if (last) {
            const char *name = NULL;
            if (!strcmp(last, "ctrl") || !strcmp(last, "control")) name = "Control_L";
            else if (!strcmp(last, "shift")) name = "Shift_L";
            else if (!strcmp(last, "alt") || !strcmp(last, "meta")) name = "Alt_L";
            else { fprintf(stderr, "xdrive: unknown modifier '%s'\n", last); exit(1); }
            if (nmods < 4) mods[nmods++] = p_ks2kc(dpy, p_str2ks(name));
        }
        last = tok;
    }
    if (!last) die("empty key spec");

    ks = p_str2ks(last);
    if (ks == 0 && last[1] == '\0') ks = keysym_for_char(last[0]);
    if (ks == 0) { fprintf(stderr, "xdrive: unknown key '%s'\n", last); exit(1); }
    kc = p_ks2kc(dpy, ks);
    if (kc == 0) { fprintf(stderr, "xdrive: no keycode for '%s'\n", last); exit(1); }

    for (i = 0; i < nmods; i++)
        p_fake_key(dpy, (unsigned int)mods[i], 1, XD_CURRENT_TIME);
    /* A shifted character in a chord (e.g. ctrl+shift+V) already has Shift
     * held, so do not add a second one here. */
    p_fake_key(dpy, (unsigned int)kc, 1, XD_CURRENT_TIME);
    p_fake_key(dpy, (unsigned int)kc, 0, XD_CURRENT_TIME);
    for (i = nmods - 1; i >= 0; i--)
        p_fake_key(dpy, (unsigned int)mods[i], 0, XD_CURRENT_TIME);
    p_flush(dpy);
    p_sync(dpy, 0);
}

static void usage(void)
{
    fputs("usage: xdrive <command>...\n"
          "  probe | focus <winid> | type <text> | slow <ms> <text>\n"
          "  key <spec> | click <button> | move <x> <y> | sleep <ms>\n"
          "  resize <winid> <wpx> <hpx>\n", stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    int i = 1;

    if (argc < 2) usage();
    x_connect();

    if (!strcmp(argv[1], "probe")) {
        printf("xdrive: XTEST usable, display open\n");
        p_close(dpy);
        return 0;
    }

    while (i < argc) {
        const char *cmd = argv[i++];
        if (!strcmp(cmd, "focus")) {
            xwindow w;
            if (i >= argc) usage();
            w = (xwindow)strtoul(argv[i++], NULL, 0);
            p_raise(dpy, w);
            p_set_focus(dpy, w, XD_REVERT_TO_PARENT, XD_CURRENT_TIME);
            p_sync(dpy, 0);
            nap_ms(200);
        } else if (!strcmp(cmd, "type")) {
            if (i >= argc) usage();
            type_text(argv[i++], 0);
        } else if (!strcmp(cmd, "slow")) {
            long ms;
            if (i + 1 >= argc) usage();
            ms = atol(argv[i++]);
            type_text(argv[i++], ms);
        } else if (!strcmp(cmd, "key")) {
            if (i >= argc) usage();
            press_spec(argv[i++]);
        } else if (!strcmp(cmd, "click")) {
            unsigned int b;
            if (i >= argc) usage();
            b = (unsigned int)atoi(argv[i++]);
            p_fake_button(dpy, b, 1, XD_CURRENT_TIME);
            p_fake_button(dpy, b, 0, XD_CURRENT_TIME);
            p_flush(dpy);
        } else if (!strcmp(cmd, "move")) {
            int x, y;
            if (i + 1 >= argc) usage();
            x = atoi(argv[i++]);
            y = atoi(argv[i++]);
            p_fake_motion(dpy, -1, x, y, XD_CURRENT_TIME);
            p_flush(dpy);
        } else if (!strcmp(cmd, "resize")) {
            xwindow w;
            unsigned int cw, ch;
            if (i + 2 >= argc) usage();
            w  = (xwindow)strtoul(argv[i++], NULL, 0);
            cw = (unsigned int)atoi(argv[i++]);
            ch = (unsigned int)atoi(argv[i++]);
            p_resize(dpy, w, cw, ch);
            p_sync(dpy, 0);
        } else if (!strcmp(cmd, "sleep")) {
            if (i >= argc) usage();
            nap_ms(atol(argv[i++]));
        } else {
            fprintf(stderr, "xdrive: unknown command '%s'\n", cmd);
            usage();
        }
    }

    p_sync(dpy, 0);
    p_close(dpy);
    return 0;
}
