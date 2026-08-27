/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* vtdrive - expect-lite driver for the LINUX VIRTUAL CONSOLE (tests/tools).
 *
 *   vtdrive [--vt N] [--gap MS] [--deadline SECS] [--log FILE]
 *           SCRIPT -- PROG [ARGS...]
 *
 * Tier V row V6's most interesting cell (docs/plans/2026-07-hardware-testing.md)
 * is the Linux virtual console: no bracketed paste, 8 colours, the KERNEL's own
 * terminal emulation rather than xterm's. jichi supports it (the ASCII glyph
 * fallbacks in docs/ACCESSIBILITY.md, M156's burst-paste path) and it had never
 * been exercised outside a pty harness -- a pty gives you a terminal emulator
 * written by us, which is exactly the thing under test.
 *
 * ptydrive cannot reach it (a pty is not a VC) and xdrive cannot either (XTEST
 * needs an X server). So this tool drives the real thing:
 *
 *   input   a VIRTUAL KEYBOARD via /dev/uinput -- real EV_KEY events through
 *           the kernel's input layer and keymap into the active VC, which is
 *           the only faithful way to "type" on a console. Not TIOCSTI: that
 *           is disabled by default on modern kernels (a privilege-escalation
 *           vector), and enabling it would trade a security setting for a
 *           shortcut. Which key produces which character is READ FROM THE
 *           KERNEL (KDGKBENT, inverted) rather than assumed, so the tool works
 *           on any layout -- see the keymap section for why that is not
 *           optional.
 *   output  /dev/vcsa<N> -- the kernel's own screen memory: a 4-byte header
 *           (rows, cols, x, y) then one char+attribute pair per cell. This is
 *           ground truth about what the console DISPLAYS, not what we think we
 *           wrote to it.
 *
 * The script language is ptydrive's, parsed by the same pd_core, so a V6 check
 * reads the same in either tool. Two semantic differences are forced by the
 * medium and are deliberate:
 *
 *   - `expect` matches the CURRENT SCREEN, not an accumulated transcript. A VC
 *     has no transcript: what scrolled off is gone. So a pattern must be on
 *     screen when it is looked for; where a check needs history, assert on the
 *     mock's captured request instead (what the model received), which is the
 *     M268 lesson from the xfce4-terminal paste check.
 *   - `winsize` is a real TIOCSWINSZ on the console, which does deliver
 *     SIGWINCH; a genuine geometry change needs setfont(8) and belongs in the
 *     runner, not here.
 *
 * ROOT is required (uinput, vcsa and /dev/tty<N> are all root-only), and the
 * run switches the active VT while it works and switches back afterwards.
 * Deliberately outside make ci / check-target, like xdrive: it needs a console
 * and it takes the keyboard.
 *
 * Exit codes: 0 script completed; 2 usage / script parse error; 3 expect
 * timeout (the screen is dumped to stderr) or --deadline hit; 4 setup failure
 * (uinput / vcsa / vt / spawn); 5 waitexit timeout; 6 assertexit mismatch.
 * Test-only; never installed.
 */

#include "pd_core.h"
#include "tt.h"
#include "jc_snprintf.h"

#include <linux/input.h>
#include <linux/kd.h>
#include <linux/uinput.h>
#include <linux/vt.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* From linux/keyboard.h, spelled out here: including that header drags in a
 * definition that collides with glibc's <sys/wait.h> (idtype_t). struct
 * kbentry comes from linux/kd.h, which is clash-free. */
#define VT_KTYP(x)   (((x) >> 8) & 0x0f)
#define VT_KVAL(x)   ((x) & 0xff)
#define VT_KT_LATIN  0
#define VT_KT_LETTER 11

/* Declared here rather than via a feature macro: vhangup() is a Linux
 * syscall whose <unistd.h> visibility depends on _GNU_SOURCE/_DEFAULT_SOURCE,
 * and this tool builds with the tier's -D_XOPEN_SOURCE=600. */
extern int vhangup(void);

#define VT_EXIT_SETUP 4
#define VT_EXIT_WAITEXIT 5
#define VT_EXIT_ASSERTEXIT 6

static const char *g_prog = "vtdrive";
static int g_ui = -1;              /* /dev/uinput                          */
static int g_console = -1;         /* /dev/tty0, for VT_ACTIVATE            */
static int g_orig_vt = -1;         /* VT to return to                      */
static int g_tty = -1;             /* /dev/tty<vt>, held open to ALLOCATE it */
static int g_vt = 12;              /* the VT under test                     */
static volatile sig_atomic_t g_child = 0;

static void on_alarm(int sig)
{
    (void)sig;
    if (g_child > 0) {
        kill((pid_t)g_child, SIGKILL);
    }
    /* Hand the console back even on the deadline path: leaving an operator
     * staring at a dead text console is a bad way to end a test run. */
    if (g_console >= 0 && g_orig_vt > 0) {
        ioctl(g_console, VT_ACTIVATE, g_orig_vt);
    }
    _exit(TT_EXIT_DEADLINE);
}

static void on_term(int sig)
{
    (void)sig;
    if (g_child > 0) {
        kill((pid_t)g_child, SIGKILL);
    }
    if (g_console >= 0 && g_orig_vt > 0) {
        ioctl(g_console, VT_ACTIVATE, g_orig_vt);
    }
    _exit(130);
}

static void usage(void)
{
    fprintf(stderr, "usage: %s [--vt N] [--gap MS] [--deadline SECS] "
                    "[--log FILE] SCRIPT -- PROG [ARGS...]\n", g_prog);
}

/* --- millisecond clock + sleep -------------------------------------------- */

static long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

static void sleep_ms(long ms)
{
    struct timeval tv;
    if (ms <= 0) {
        return;
    }
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    select(0, NULL, NULL, NULL, &tv);
}

/* --- the keymap ------------------------------------------------------------ */
/* Typing a CHARACTER means pressing whichever key produces it under the
 * console's CURRENT keymap, so the map is read from the kernel (KDGKBENT) and
 * inverted -- char -> (keycode, modifiers) -- rather than assumed. The static
 * US table below is only a fallback for when that ioctl is unavailable.
 *
 * This is not hypothetical tidiness. The first run of this tool assumed US and
 * typed `_` as shift+KEY_MINUS; on the German keymap this host actually uses,
 * that key is `ß` and shift gives `?`, so the self-test read back
 * "VTDRIVE?SELFTEST?OK" -- and `localectl` had reported "VC Keymap: (unset)",
 * which is exactly the kind of second-hand witness docs/TEST_INTEGRITY.md
 * warns about. Ask the kernel; it knows. */

struct keymap_entry {
    unsigned char ch;
    unsigned short code;
    int shift;
};

static const struct keymap_entry KEYMAP[] = {
    {'a', KEY_A, 0}, {'b', KEY_B, 0}, {'c', KEY_C, 0}, {'d', KEY_D, 0},
    {'e', KEY_E, 0}, {'f', KEY_F, 0}, {'g', KEY_G, 0}, {'h', KEY_H, 0},
    {'i', KEY_I, 0}, {'j', KEY_J, 0}, {'k', KEY_K, 0}, {'l', KEY_L, 0},
    {'m', KEY_M, 0}, {'n', KEY_N, 0}, {'o', KEY_O, 0}, {'p', KEY_P, 0},
    {'q', KEY_Q, 0}, {'r', KEY_R, 0}, {'s', KEY_S, 0}, {'t', KEY_T, 0},
    {'u', KEY_U, 0}, {'v', KEY_V, 0}, {'w', KEY_W, 0}, {'x', KEY_X, 0},
    {'y', KEY_Y, 0}, {'z', KEY_Z, 0},
    {'A', KEY_A, 1}, {'B', KEY_B, 1}, {'C', KEY_C, 1}, {'D', KEY_D, 1},
    {'E', KEY_E, 1}, {'F', KEY_F, 1}, {'G', KEY_G, 1}, {'H', KEY_H, 1},
    {'I', KEY_I, 1}, {'J', KEY_J, 1}, {'K', KEY_K, 1}, {'L', KEY_L, 1},
    {'M', KEY_M, 1}, {'N', KEY_N, 1}, {'O', KEY_O, 1}, {'P', KEY_P, 1},
    {'Q', KEY_Q, 1}, {'R', KEY_R, 1}, {'S', KEY_S, 1}, {'T', KEY_T, 1},
    {'U', KEY_U, 1}, {'V', KEY_V, 1}, {'W', KEY_W, 1}, {'X', KEY_X, 1},
    {'Y', KEY_Y, 1}, {'Z', KEY_Z, 1},
    {'1', KEY_1, 0}, {'2', KEY_2, 0}, {'3', KEY_3, 0}, {'4', KEY_4, 0},
    {'5', KEY_5, 0}, {'6', KEY_6, 0}, {'7', KEY_7, 0}, {'8', KEY_8, 0},
    {'9', KEY_9, 0}, {'0', KEY_0, 0},
    {'!', KEY_1, 1}, {'@', KEY_2, 1}, {'#', KEY_3, 1}, {'$', KEY_4, 1},
    {'%', KEY_5, 1}, {'^', KEY_6, 1}, {'&', KEY_7, 1}, {'*', KEY_8, 1},
    {'(', KEY_9, 1}, {')', KEY_0, 1},
    {' ', KEY_SPACE, 0},
    {'-', KEY_MINUS, 0}, {'_', KEY_MINUS, 1},
    {'=', KEY_EQUAL, 0}, {'+', KEY_EQUAL, 1},
    {'[', KEY_LEFTBRACE, 0}, {'{', KEY_LEFTBRACE, 1},
    {']', KEY_RIGHTBRACE, 0}, {'}', KEY_RIGHTBRACE, 1},
    {'\\', KEY_BACKSLASH, 0}, {'|', KEY_BACKSLASH, 1},
    {';', KEY_SEMICOLON, 0}, {':', KEY_SEMICOLON, 1},
    {'\'', KEY_APOSTROPHE, 0}, {'"', KEY_APOSTROPHE, 1},
    {',', KEY_COMMA, 0}, {'<', KEY_COMMA, 1},
    {'.', KEY_DOT, 0}, {'>', KEY_DOT, 1},
    {'/', KEY_SLASH, 0}, {'?', KEY_SLASH, 1},
    {'`', KEY_GRAVE, 0}, {'~', KEY_GRAVE, 1}
};

/* The kernel's map, inverted: for each character, the key to press and the
 * modifier bits (bit 0 shift, bit 1 AltGr) needed. code 0 = unmapped. */
static unsigned short g_code[256];
static unsigned char g_mods[256];
static int g_have_kernel_map = 0;

/* Read the console keymap through KDGKBENT and invert it. `fd` must be a
 * console (/dev/tty0). Returns the number of characters mapped, or -1. */
static int keymap_from_kernel(int fd)
{
    int table;
    int kc;
    int mapped = 0;

    memset(g_code, 0, sizeof(g_code));
    memset(g_mods, 0, sizeof(g_mods));

    /* Tables are indexed by modifier bitmask: 0 plain, 1 shift, 2 AltGr.
     * Plain first, so the cheapest way to reach a character wins. */
    for (table = 0; table <= 2; table++) {
        for (kc = 1; kc < 128; kc++) {
            struct kbentry ke;
            unsigned short val;
            unsigned char ch;
            int type;

            memset(&ke, 0, sizeof(ke));
            ke.kb_table = (unsigned char)table;
            ke.kb_index = (unsigned char)kc;
            if (ioctl(fd, KDGKBENT, &ke) != 0) {
                return -1;
            }
            val = ke.kb_value;
            type = VT_KTYP(val);
            /* KT_LATIN carries a Latin-1 byte; KT_LETTER is the same but
             * CapsLock-sensitive, which is what plain letters are. */
            if (type != VT_KT_LATIN && type != VT_KT_LETTER) {
                continue;
            }
            ch = (unsigned char)VT_KVAL(val);
            if (ch == 0 || g_code[ch] != 0) {
                continue;
            }
            g_code[ch] = (unsigned short)kc;
            g_mods[ch] = (unsigned char)table;
            mapped++;
        }
    }
    return mapped;
}

static void keymap_from_table(void)
{
    size_t i;
    size_t n = sizeof(KEYMAP) / sizeof(KEYMAP[0]);

    memset(g_code, 0, sizeof(g_code));
    memset(g_mods, 0, sizeof(g_mods));
    for (i = 0; i < n; i++) {
        unsigned char ch = KEYMAP[i].ch;
        if (g_code[ch] == 0) {
            g_code[ch] = KEYMAP[i].code;
            g_mods[ch] = (unsigned char)(KEYMAP[i].shift ? 1 : 0);
        }
    }
}

static int keymap_lookup(unsigned char ch, unsigned short *code, int *mods)
{
    if (g_code[ch] == 0) {
        return -1;
    }
    *code = g_code[ch];
    *mods = (int)g_mods[ch];
    return 0;
}

/* --- the virtual keyboard ------------------------------------------------- */

static int emit(int fd, unsigned short type, unsigned short code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) != (int)sizeof(ev)) {
        return -1;
    }
    return 0;
}

static int uinput_open(void)
{
    struct uinput_user_dev dev;
    int fd;
    int i;

    fd = open("/dev/uinput", O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot open /dev/uinput (root?): %s\n",
                g_prog, strerror(errno));
        return -1;
    }
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) != 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) != 0) {
        fprintf(stderr, "%s: UI_SET_EVBIT failed\n", g_prog);
        close(fd);
        return -1;
    }
    /* Enable every keycode rather than only the ones the table names: a
     * missing KEYBIT drops that key silently, which is the kind of gap that
     * reads as a product bug. */
    for (i = 1; i < 256; i++) {
        ioctl(fd, UI_SET_KEYBIT, i);
    }
    memset(&dev, 0, sizeof(dev));
    strncpy(dev.name, "jichi-vtdrive", UINPUT_MAX_NAME_SIZE - 1);
    dev.id.bustype = BUS_USB;
    dev.id.vendor = 0x1;
    dev.id.product = 0x1;
    dev.id.version = 1;
    if (write(fd, &dev, sizeof(dev)) != (int)sizeof(dev)) {
        fprintf(stderr, "%s: uinput_user_dev write failed\n", g_prog);
        close(fd);
        return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) != 0) {
        fprintf(stderr, "%s: UI_DEV_CREATE failed\n", g_prog);
        close(fd);
        return -1;
    }
    /* The input layer needs a moment to attach the new device; keystrokes
     * written before that are dropped on the floor. */
    sleep_ms(400);
    return fd;
}

static void uinput_close(void)
{
    if (g_ui >= 0) {
        ioctl(g_ui, UI_DEV_DESTROY);
        close(g_ui);
        g_ui = -1;
    }
}

/* Type one byte as key events. Returns 0, or -1 for a byte with no mapping. */
static int type_byte(unsigned char ch)
{
    unsigned short code = 0;
    int mods = 0;              /* bit 0 shift, bit 1 AltGr */
    int ctrl = 0;

    if (ch == '\r') {
        code = KEY_ENTER;      /* the Enter KEY sends CR */
    } else if (ch == '\t') {
        code = KEY_TAB;
    } else if (ch == 0x7f || ch == '\b') {
        code = KEY_BACKSPACE;
    } else if (ch == 0x1b) {
        code = KEY_ESC;
    } else if (ch >= 1 && ch <= 26) {
        /* Ctrl-A .. Ctrl-Z; \t (9), \r (13) and ESC (27) are handled above,
         * so what reaches here is a genuine control keystroke -- including
         * LF (0x0a) as Ctrl-J, which is deliberate and load-bearing. A VC has
         * no bracketed paste, so a pasted line break arrives as a raw LF
         * byte, while the Enter KEY sends CR. Folding '\n' into KEY_ENTER (as
         * the first draft did) would submit three separate lines and test
         * nothing about M156's burst path. */
        if (keymap_lookup((unsigned char)('a' + ch - 1), &code, &mods) != 0) {
            return -1;
        }
        ctrl = 1;
        mods = 0;
    } else if (keymap_lookup(ch, &code, &mods) != 0) {
        return -1;
    }

    if (ctrl && emit(g_ui, EV_KEY, KEY_LEFTCTRL, 1) != 0) {
        return -1;
    }
    if ((mods & 1) && emit(g_ui, EV_KEY, KEY_LEFTSHIFT, 1) != 0) {
        return -1;
    }
    if ((mods & 2) && emit(g_ui, EV_KEY, KEY_RIGHTALT, 1) != 0) {
        return -1;   /* AltGr: a non-US layout needs it for /, @, backslash */
    }
    if (emit(g_ui, EV_KEY, code, 1) != 0 ||
        emit(g_ui, EV_SYN, SYN_REPORT, 0) != 0 ||
        emit(g_ui, EV_KEY, code, 0) != 0 ||
        emit(g_ui, EV_SYN, SYN_REPORT, 0) != 0) {
        return -1;
    }
    if ((mods & 2) && emit(g_ui, EV_KEY, KEY_RIGHTALT, 0) != 0) {
        return -1;
    }
    if ((mods & 1) && emit(g_ui, EV_KEY, KEY_LEFTSHIFT, 0) != 0) {
        return -1;
    }
    if (ctrl && emit(g_ui, EV_KEY, KEY_LEFTCTRL, 0) != 0) {
        return -1;
    }
    emit(g_ui, EV_SYN, SYN_REPORT, 0);
    return 0;
}

/* Make sure our VT is still the active one. Nothing guarantees it stays: a
 * compositor or logind can take the console back, and then every further
 * keystroke goes to the DESKTOP session instead of the terminal under test --
 * observed on run 3 of M274, where the self-test read back exactly one
 * character. Checked before every keystroke, because a string half-typed into
 * someone's desktop is both a broken test and a rude surprise. */
static void vt_ensure_active(void)
{
    struct vt_stat st;

    if (g_console < 0) {
        return;
    }
    if (ioctl(g_console, VT_GETSTATE, &st) != 0) {
        return;
    }
    if (st.v_active == (unsigned short)g_vt) {
        return;
    }
    fprintf(stderr, "%s: VT %d lost the console to VT %d; re-activating\n",
            g_prog, g_vt, (int)st.v_active);
    ioctl(g_console, VT_ACTIVATE, g_vt);
    ioctl(g_console, VT_WAITACTIVE, g_vt);
    sleep_ms(150);
}

/* Type a byte string. `gap_ms` paces keystrokes: 0 is a burst (a VC has no
 * bracketed paste, so a burst IS what a paste looks like there -- M156). */
static int type_bytes(const char *s, size_t len, long gap_ms)
{
    size_t i;
    for (i = 0; i < len; i++) {
        vt_ensure_active();
        if (type_byte((unsigned char)s[i]) != 0) {
            fprintf(stderr, "%s: no keymap entry for byte 0x%02x\n",
                    g_prog, (unsigned char)s[i]);
            return -1;
        }
        if (gap_ms > 0) {
            sleep_ms(gap_ms);
        }
    }
    return 0;
}

/* --- the screen ----------------------------------------------------------- */

/* Read /dev/vcsa<vt> into `out` (cap bytes) as rows of text separated by
 * newlines. Sets rows and cols through their pointers when non-NULL.
 * Returns the text length, or -1. */
static int screen_read(int vt, char *out, size_t cap, int *rows, int *cols)
{
    char path[64];
    unsigned char hdr[4];
    unsigned char *cells;
    int fd;
    int r;
    int c;
    size_t want;
    size_t got = 0;
    size_t n = 0;

    jc_snprintf(path, sizeof(path), "/dev/vcsa%d", vt);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    if (read(fd, hdr, 4) != 4) {
        close(fd);
        return -1;
    }
    r = hdr[0];
    c = hdr[1];
    if (r <= 0 || c <= 0) {
        close(fd);
        return -1;
    }
    want = (size_t)r * (size_t)c * 2;
    cells = (unsigned char *)malloc(want);
    if (cells == NULL) {
        close(fd);
        return -1;
    }
    while (got < want) {
        int k = (int)read(fd, cells + got, want - got);
        if (k <= 0) {
            break;
        }
        got += (size_t)k;
    }
    close(fd);

    {
        int y;
        int x;
        for (y = 0; y < r; y++) {
            for (x = 0; x < c; x++) {
                size_t idx = ((size_t)y * (size_t)c + (size_t)x) * 2;
                unsigned char ch = (idx < got) ? cells[idx] : (unsigned char)' ';
                if (n + 1 >= cap) {
                    break;
                }
                out[n++] = (char)(ch == 0 ? ' ' : ch);
            }
            if (n + 1 >= cap) {
                break;
            }
            out[n++] = '\n';
        }
    }
    free(cells);
    out[n] = '\0';
    if (rows != NULL) {
        *rows = r;
    }
    if (cols != NULL) {
        *cols = c;
    }
    return (int)n;
}

static void screen_dump(int vt, FILE *to)
{
    char buf[32768];
    int rows = 0;
    int cols = 0;
    if (screen_read(vt, buf, sizeof(buf), &rows, &cols) < 0) {
        fprintf(to, "%s: (screen unreadable)\n", g_prog);
        return;
    }
    fprintf(to, "%s: screen %dx%d:\n%s\n", g_prog, rows, cols, buf);
}

/* --- the VT --------------------------------------------------------------- */

static int vt_setup(int vt)
{
    struct vt_stat st;
    char path[64];

    g_console = open("/dev/tty0", O_RDWR);
    if (g_console < 0) {
        fprintf(stderr, "%s: cannot open /dev/tty0 (root?): %s\n",
                g_prog, strerror(errno));
        return -1;
    }
    if (ioctl(g_console, VT_GETSTATE, &st) == 0) {
        g_orig_vt = st.v_active;
    }
    {   /* Invert the console's ACTUAL keymap; fall back to the US table. */
        int n = keymap_from_kernel(g_console);
        if (n > 0) {
            g_have_kernel_map = 1;
            fprintf(stderr, "%s: keymap: kernel, %d characters mapped\n",
                    g_prog, n);
        } else {
            keymap_from_table();
            fprintf(stderr, "%s: keymap: KDGKBENT unavailable, assuming US\n",
                    g_prog);
        }
    }
    /* Open the target console BEFORE switching to it, and keep the fd: that
     * open is what ALLOCATES the VC. An unallocated console can be made
     * "active" by VT_ACTIVATE without being able to receive a single
     * keystroke -- the events are accepted by uinput, the kbd handler is
     * attached, and they land nowhere. That is the whole of M274's
     * cold-console mystery: every passing run had had the VC allocated by an
     * earlier run in the same boot, and the first run after a reboot got
     * nothing. VT_ACTIVATE is not allocation. */
    jc_snprintf(path, sizeof(path), "/dev/tty%d", vt);
    g_tty = open(path, O_RDWR);
    if (g_tty < 0) {
        fprintf(stderr, "%s: cannot open %s to allocate it: %s\n",
                g_prog, path, strerror(errno));
        return -1;
    }
    if (ioctl(g_console, VT_ACTIVATE, vt) != 0 ||
        ioctl(g_console, VT_WAITACTIVE, vt) != 0) {
        fprintf(stderr, "%s: cannot activate VT %d: %s\n",
                g_prog, vt, strerror(errno));
        return -1;
    }
    return 0;
}

static void vt_restore(void)
{
    if (g_tty >= 0) {
        close(g_tty);
        g_tty = -1;
    }
    if (g_console >= 0) {
        if (g_orig_vt > 0) {
            ioctl(g_console, VT_ACTIVATE, g_orig_vt);
            ioctl(g_console, VT_WAITACTIVE, g_orig_vt);
        }
        close(g_console);
        g_console = -1;
    }
}

/* Spawn PROG with /dev/tty<vt> as its controlling terminal, the way
 * openvt(1) does. The console is reset first so screen matching starts from
 * a known state. */
static int spawn_on_vt(int vt, char **argv, pid_t *out)
{
    char path[64];
    pid_t pid;
    int fd;

    jc_snprintf(path, sizeof(path), "/dev/tty%d", vt);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot open %s: %s\n",
                g_prog, path, strerror(errno));
        return -1;
    }
    if (write(fd, "\033c", 2) != 2) {          /* full reset */
        /* not fatal */
    }
    close(fd);

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        int tfd;
        struct termios t;
        setsid();
        tfd = open(path, O_RDWR);
        if (tfd < 0) {
            _exit(127);
        }
        if (ioctl(tfd, TIOCSCTTY, 1) != 0) {
            _exit(127);
        }
        /* NO vhangup() here, deliberately (M274). getty's sequence includes
         * it, but getty owns the console; we are borrowing one whose
         * ALLOCATION is held by the parent's fd, and vhangup invalidates every
         * fd on the tty -- including that one -- which quietly un-allocates
         * the console and sends every subsequent keystroke nowhere. The state
         * it was there to scrub (a previous run's raw mode with echo off) is
         * fully handled by the explicit termios below, which does not need a
         * hangup to be authoritative. */
        if (tcgetattr(tfd, &t) == 0) {
            t.c_iflag = BRKINT | ICRNL | IXON;
            t.c_oflag = OPOST | ONLCR;
            t.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
            t.c_cc[VINTR] = 3;      /* Ctrl-C */
            t.c_cc[VEOF] = 4;       /* Ctrl-D */
            t.c_cc[VKILL] = 21;     /* Ctrl-U */
            t.c_cc[VERASE] = 0177;
            t.c_cc[VMIN] = 1;
            t.c_cc[VTIME] = 0;
            tcsetattr(tfd, TCSANOW, &t);
        }
        tcflush(tfd, TCIOFLUSH);
        dup2(tfd, 0);
        dup2(tfd, 1);
        dup2(tfd, 2);
        if (tfd > 2) {
            close(tfd);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    *out = pid;
    return 0;
}

/* --- main ----------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int vt = 12;               /* outside logind's NAutoVTs (6), so no getty */
    long deadline = 120;
    long gap = 12;             /* ms between keystrokes; 0 = a burst */
    const char *log_path = NULL;
    const char *script_path = NULL;
    char **child_argv = NULL;
    struct pd_script script;
    char err[256];
    char *script_text = NULL;
    FILE *logf = NULL;
    pid_t child = -1;
    int have_status = 0;
    int child_status = -1;
    int i;
    int rc = TT_EXIT_OK;

    memset(&script, 0, sizeof(script));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vt") == 0 && i + 1 < argc) {
            vt = (int)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--gap") == 0 && i + 1 < argc) {
            gap = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--deadline") == 0 && i + 1 < argc) {
            deadline = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        } else if (strcmp(argv[i], "--") == 0) {
            if (i + 1 >= argc) {
                usage();
                return TT_EXIT_USAGE;
            }
            child_argv = &argv[i + 1];
            break;
        } else if (script_path == NULL) {
            script_path = argv[i];
        } else {
            usage();
            return TT_EXIT_USAGE;
        }
    }
    if (script_path == NULL || child_argv == NULL) {
        usage();
        return TT_EXIT_USAGE;
    }

    {   /* read the script */
        FILE *f = fopen(script_path, "rb");
        long len;
        if (f == NULL) {
            fprintf(stderr, "%s: cannot open %s\n", g_prog, script_path);
            return TT_EXIT_USAGE;
        }
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len < 0) {
            fclose(f);
            return TT_EXIT_USAGE;
        }
        script_text = (char *)malloc((size_t)len + 1);
        if (script_text == NULL) {
            fclose(f);
            return TT_EXIT_USAGE;
        }
        if (fread(script_text, 1, (size_t)len, f) != (size_t)len) {
            fclose(f);
            free(script_text);
            return TT_EXIT_USAGE;
        }
        script_text[len] = '\0';
        fclose(f);
    }
    if (pd_script_parse(script_text, &script, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s: %s: %s\n", g_prog, script_path, err);
        free(script_text);
        return TT_EXIT_USAGE;
    }
    if (log_path != NULL) {
        logf = fopen(log_path, "wb");
    }

    if (geteuid() != 0) {
        fprintf(stderr, "%s: needs root (uinput, vcsa, /dev/tty%d)\n",
                g_prog, vt);
        rc = VT_EXIT_SETUP;
        goto done;
    }
    /* Order matters, and it cost three runs to see it (M274): switch the
     * console BEFORE creating the virtual keyboard. A uinput device that
     * appears while the graphical session is still active gets handed to that
     * session by logind/the compositor, and then our keystrokes go there
     * instead of to the console -- which showed up as exactly ONE character
     * arriving and the rest vanishing, alternating run to run as the race
     * fell one way or the other. Created after the switch, the device is born
     * into an inactive-session world and only the console consumes it. */
    g_vt = vt;
    if (vt_setup(vt) != 0) {
        rc = VT_EXIT_SETUP;
        goto done;
    }
    g_ui = uinput_open();
    if (g_ui < 0) {
        rc = VT_EXIT_SETUP;
        goto done;
    }
    /* Warm-up handshake (M274). A console switched to for the FIRST time is
     * not instantly ready to receive keys: the events are written, accepted by
     * uinput, and dropped somewhere between the switch and the console's
     * keyboard focus settling -- which showed up as the first keystroke (or
     * all of them) vanishing, and passing whenever a previous run had left the
     * VT warm. So do not assume: type a character into the bare tty (the line
     * discipline echoes it with no reader present) and read the screen back
     * until it lands. The screen is reset and the input queue flushed before
     * the child starts, so the probe character reaches neither.
     *
     * Verifying the instrument beats trusting it -- the same reason the runner
     * opens with a self-test. This just moves that guarantee inside the tool,
     * where it can retry instead of merely reporting. */
    {
        int ready = 0;
        int tries;
        unsigned short pcode = 0;
        int pmods = 0;

        if (keymap_lookup((unsigned char)'x', &pcode, &pmods) != 0) {
            fprintf(stderr, "%s: no key produces 'x' on this keymap\n", g_prog);
            rc = VT_EXIT_SETUP;
            goto done;
        }
        for (tries = 0; tries < 20 && !ready; tries++) {
            char probe[16384];
            if (type_byte('x') != 0) {
                break;
            }
            sleep_ms(150);
            if (screen_read(vt, probe, sizeof(probe), NULL, NULL) > 0 &&
                pd_match(probe, strlen(probe), "x", 1) != NULL) {
                ready = 1;
            }
        }
        if (!ready) {
            fprintf(stderr, "%s: the console never accepted a keystroke "
                            "(uinput -> VT %d input path is not live)\n",
                    g_prog, vt);
            rc = VT_EXIT_SETUP;
            goto done;
        }
        if (tries > 0) {
            fprintf(stderr, "%s: input path live after %d probe keystroke(s)\n",
                    g_prog, tries + 1);
        }
    }
    if (spawn_on_vt(vt, child_argv, &child) != 0) {
        rc = VT_EXIT_SETUP;
        goto done;
    }
    g_child = (sig_atomic_t)child;
    {   /* A child that dies on its own tty setup would otherwise present as
         * "nothing was typed", which is indistinguishable from a broken input
         * path. Name it. */
        int st = 0;
        sleep_ms(250);
        if (waitpid(child, &st, WNOHANG) == child) {
            fprintf(stderr, "%s: the child exited immediately (status %d) -- "
                            "it never reached the terminal\n",
                    g_prog, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
            child = -1;
            g_child = 0;
            rc = VT_EXIT_SETUP;
            goto done;
        }
    }
    signal(SIGALRM, on_alarm);
    signal(SIGINT, on_term);
    signal(SIGTERM, on_term);
    if (deadline > 0) {
        alarm((unsigned)(deadline * tt_timeout_mult()));
    }

    for (i = 0; i < script.ncmds; i++) {
        const struct pd_cmd *c = &script.cmds[i];
        switch (c->kind) {
        case PD_CMD_EXPECT: {
            long end = now_ms() + c->a * 1000L * tt_timeout_mult();
            int found = 0;
            char buf[32768];
            for (;;) {
                if (screen_read(vt, buf, sizeof(buf), NULL, NULL) > 0 &&
                    pd_match(buf, strlen(buf), c->text, c->text_len) != NULL) {
                    found = 1;
                    break;
                }
                if (now_ms() >= end) {
                    break;
                }
                sleep_ms(100);
            }
            if (!found) {
                fprintf(stderr, "%s: line %d: expect \"%s\" timed out (%lds)\n",
                        g_prog, c->line, c->text,
                        c->a * tt_timeout_mult());
                screen_dump(vt, stderr);
                rc = TT_EXIT_DEADLINE;
                goto done;
            }
            break;
        }
        case PD_CMD_SEND:
            /* --gap paces keystrokes: the default 12 ms is human-scale (the
             * M209 driver rule), and --gap 0 is a burst, which on a console is
             * what a paste physically IS -- there is no bracketed-paste
             * wrapper to mark one. See the runner's paste check. */
            if (type_bytes(c->text, c->text_len, gap) != 0) {
                rc = VT_EXIT_SETUP;
                goto done;
            }
            break;
        case PD_CMD_DELAY:
            sleep_ms(c->a);
            break;
        case PD_CMD_DRAIN:
            /* No transcript to drain on a VC; a drain is just a settle. */
            sleep_ms(c->a);
            break;
        case PD_CMD_WINSIZE: {
            char path[64];
            int fd;
            struct winsize ws;
            jc_snprintf(path, sizeof(path), "/dev/tty%d", vt);
            fd = open(path, O_RDWR);
            if (fd >= 0) {
                memset(&ws, 0, sizeof(ws));
                ws.ws_row = (unsigned short)c->a;
                ws.ws_col = (unsigned short)c->b;
                ioctl(fd, TIOCSWINSZ, &ws);
                close(fd);
            }
            break;
        }
        case PD_CMD_SIGNAL:
            if (child > 0) {
                kill(child, (int)c->a);
            }
            break;
        case PD_CMD_WAITEXIT: {
            long end = now_ms() + c->a * 1000L * tt_timeout_mult();
            for (;;) {
                int st = 0;
                pid_t w = waitpid(child, &st, WNOHANG);
                if (w == child) {
                    if (WIFEXITED(st)) {
                        child_status = WEXITSTATUS(st);
                    } else if (WIFSIGNALED(st)) {
                        child_status = 128 + WTERMSIG(st);
                    }
                    have_status = 1;
                    child = -1;
                    g_child = 0;
                    break;
                }
                if (now_ms() >= end) {
                    fprintf(stderr, "%s: line %d: waitexit timed out (%lds)\n",
                            g_prog, c->line, c->a * tt_timeout_mult());
                    screen_dump(vt, stderr);
                    rc = VT_EXIT_WAITEXIT;
                    goto done;
                }
                sleep_ms(100);
            }
            break;
        }
        case PD_CMD_ASSERTEXIT:
            if (!have_status || child_status != (int)c->a) {
                fprintf(stderr, "%s: line %d: exit status %d, expected %ld\n",
                        g_prog, c->line, child_status, c->a);
                rc = VT_EXIT_ASSERTEXIT;
                goto done;
            }
            break;
        default:
            break;
        }
    }

done:
    if (logf != NULL) {
        char buf[32768];
        if (screen_read(vt, buf, sizeof(buf), NULL, NULL) > 0) {
            fputs(buf, logf);
        }
        fclose(logf);
    }
    if (child > 0) {
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
    }
    uinput_close();
    vt_restore();
    pd_script_free(&script);
    free(script_text);
    return rc;
}
