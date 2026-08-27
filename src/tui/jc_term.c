/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_term.c - raw-mode terminal handling and line editor (see jc_term.h). */

#include "jc_term.h"
#include "jc_str.h"
#include "jc_complete.h"
#include "jc_utf8.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>

void jc_term_init(struct jc_term *t)
{
    t->in_fd = STDIN_FILENO;
    t->out_fd = STDOUT_FILENO;
    t->is_tty = isatty(t->in_fd) && isatty(t->out_fd);
    t->complete = NULL;
    t->complete_ctx = NULL;
    t->suggest = NULL;
    t->suggest_ctx = NULL;
    /* M578: advise/advise_ctx were NEVER INITIALISED HERE, and the readline
     * loop tests `t->advise != NULL` on Ctrl-Q. The TUI happens to install one
     * immediately after init, which is why nothing had gone wrong -- but
     * src/main.c builds a `struct jc_term` on the STACK for the setup wizard
     * and for the model picker and installs neither callback, so those two
     * readline loops were testing an uninitialised function pointer against
     * NULL and would have CALLED it on a keystroke.
     *
     * Found while adding suggest_announce below: the new field needed
     * initialising, and the two beside it turned out not to be. */
    t->advise = NULL;
    t->advise_ctx = NULL;
    t->suggest_announce = NULL;
    t->holding = 0;
    jc_vec_init(&t->history, sizeof(char *));
}

void jc_term_set_completer(struct jc_term *t, jc_completer_fn fn, void *ctx)
{
    t->complete = fn;
    t->complete_ctx = ctx;
}

void jc_term_set_suggester(struct jc_term *t, jc_suggest_fn fn, void *ctx)
{
    t->suggest = fn;
    t->suggest_ctx = ctx;
}

void jc_term_set_adviser(struct jc_term *t, jc_advise_fn fn, void *ctx)
{
    t->advise = fn;
    t->advise_ctx = ctx;
}

void jc_term_set_suggest_announce(struct jc_term *t, const char *label)
{
    t->suggest_announce = label;
}

void jc_term_free(struct jc_term *t)
{
    jc_size i;
    jc_term_hold_end(t);   /* never leave the tty without echo (M254) */
    for (i = 0; i < t->history.len; i++) {
        free(*(char **)jc_vec_at(&t->history, i));
    }
    jc_vec_free(&t->history);
}

static void hist_add(struct jc_term *t, const char *line)
{
    char *copy;
    if (line[0] == '\0') {
        return;
    }
    /* M363: never store a multi-line entry. The editor is a ONE-row editor:
     * recalling an entry with embedded newlines (a pasted block, a
     * backslash-continuation submit) sets raw \n bytes into the buffer, and
     * render() then emits them under OPOST-off raw mode -- a bare line feed
     * with no carriage return, stair-stepping the input region, with the
     * column math off by every folded row. Skipping is the honest option
     * until the editor learns rows: arrow-up gives the previous SINGLE-line
     * entry instead of corrupting the screen. This is the one chokepoint --
     * jc_term_history_add routes here too. */
    if (strchr(line, '\n') != NULL) {
        return;
    }
    copy = jc_strdup(line);
    if (copy != NULL) {
        jc_vec_push(&t->history, &copy);
    }
}

void jc_term_history_add(struct jc_term *t, const char *line)
{
    if (t != NULL && line != NULL) {
        hist_add(t, line);
    }
}

/* --- raw mode --- */

/* M503: is there input the tty has buffered but nobody has read? enter_raw is
 * about to throw it away (TCSAFLUSH, deliberately -- see below), and until now
 * it did so in silence.
 *
 * MEASURED, 2026-08-17 (M464), while explaining OpenBSD's "lost first send":
 * anything typed between process start and the first prompt is echoed by the tty
 * and then discarded. The window is under 100 ms on this bench -- but over FIVE
 * SECONDS on the OpenBSD guest, which is long enough for a person to type a
 * whole line, watch it appear, and see it vanish.
 *
 * The probe itself already existed for M156's burst-paste fallback, further down
 * this file; it is forward-declared rather than duplicated, because two
 * readability checks that could drift apart is exactly the kind of duplication
 * this tree spends lints on. */
static int input_pending(int fd);

static int enter_raw(int fd, struct termios *saved, int *flushed)
{
    struct termios raw;
    if (tcgetattr(fd, saved) != 0) {
        return -1;
    }
    if (flushed != NULL && input_pending(fd)) {
        *flushed = 1;
    }
    raw = *saved;
    raw.c_iflag &= ~(tcflag_t)(ICRNL | INPCK | ISTRIP | IXON | BRKINT);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSAFLUSH, &raw) != 0) {
        return -1;
    }
    return 0;
}

static void leave_raw(int fd, const struct termios *saved)
{
    tcsetattr(fd, TCSAFLUSH, saved);
}

/* --- the turn-scoped hold (M254) ---
 *
 * enter_raw above is for EDITING: it flushes pending input (TCSAFLUSH, which is
 * what keeps stray type-ahead from answering a y/n approval prompt) and clears
 * OPOST because the editor emits its own CR/LF. A hold is for LISTENING while
 * the agent works, so it differs on exactly three points, each load-bearing:
 *
 *   - ISIG stays ON: Ctrl-C must keep raising SIGINT, which is the existing
 *     abort path (app->abort_flag). The hold adds a channel; it must not take
 *     the interrupt away.
 *   - OPOST stays ON: everything printed during a turn (streamed markdown,
 *     tool lines) ends lines with a bare "\n" and relies on the tty for the
 *     carriage return. Clearing OPOST would stair-step the whole transcript.
 *   - TCSANOW, not TCSAFLUSH: flushing here would discard the very keystrokes
 *     this exists to keep -- the M254 bug, reintroduced at the fix site.
 */
int jc_term_hold_begin(struct jc_term *t)
{
    struct termios h;
    if (t == NULL || !t->is_tty || t->holding) {
        return -1;
    }
    if (tcgetattr(t->in_fd, &t->hold_saved) != 0) {
        return -1;
    }
    h = t->hold_saved;
    h.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
    h.c_cc[VMIN] = 0;    /* a read() returns 0 immediately when idle */
    h.c_cc[VTIME] = 0;
    if (tcsetattr(t->in_fd, TCSANOW, &h) != 0) {
        return -1;
    }
    t->holding = 1;
    return 0;
}

void jc_term_hold_end(struct jc_term *t)
{
    if (t == NULL || !t->holding) {
        return;
    }
    tcsetattr(t->in_fd, TCSANOW, &t->hold_saved);
    t->holding = 0;
}

int jc_term_poll_key(struct jc_term *t)
{
    unsigned char ch;
    ssize_t n;
    if (t == NULL || !t->holding) {
        return -1;
    }
    n = read(t->in_fd, &ch, 1);
    return (n == 1) ? (int)ch : -1;
}

int jc_term_read_key(struct jc_term *t)
{
    if (t->is_tty) {
        struct termios saved;
        unsigned char ch;
        ssize_t n;
        int got;
        /* NULL: this path reads ONE key (a y/n answer), and the flush there is
         * the safety behaviour itself -- announcing it would fire on every
         * approval prompt. The notice is for a LINE prompt. */
        if (enter_raw(t->in_fd, &saved, NULL) != 0) {
            int c = getchar();
            return (c == EOF) ? -1 : c;
        }
        n = read(t->in_fd, &ch, 1);
        got = (n == 1) ? (int)ch : -1;
        leave_raw(t->in_fd, &saved);
        return got;
    } else {
        /* Non-TTY (piped): read a line, return its first byte, drain the rest. */
        int first = getchar();
        int c = first;
        while (c != EOF && c != '\n') c = getchar();
        return (first == EOF) ? -1 : first;
    }
}

/* --- editor state --- */

struct editor {
    struct jc_sb buf;
    jc_size      cursor;     /* byte index in buf                            */
    int          out_fd;
    int          hist_pos;   /* -1 = not navigating, else index into history */
    int          cursor_row; /* cursor's row offset within the input region  */
    char         ghost[256]; /* pending inline suggestion (shown dim), or "" */
    int          has_ghost;
    struct jc_sb kill;       /* kill-ring: last killed text, for Ctrl-Y (M126)*/
    char        *undo_buf[64];   /* undo stack of buffer snapshots (M-C)      */
    jc_size      undo_pos[64];   /* saved cursor per snapshot                 */
    int          undo_n;         /* stack depth                               */
    int          last_insert;    /* coalesce a run of typed chars into 1 undo */
    int          fast_echo;      /* M362: accessible incremental echo         */
};

/* Best-effort terminal write of `n` bytes. Short writes / errors are ignored
 * (terminal output is advisory); the return value is consumed so the build is
 * warn_unused_result-clean under optimized profiles (e.g. make SIZE=1). */
static void ed_write_n(struct editor *e, const char *s, jc_size n)
{
    ssize_t w = write(e->out_fd, s, n);
    (void)w;
}

static void ed_write(struct editor *e, const char *s)
{
    ed_write_n(e, s, strlen(s));
}

int jc_term_str_cols_from(int start_col, const char *s, jc_size n)
{
    int cols = 0;
    jc_size i = 0;
    if (s == NULL) {
        return 0;
    }
    if (start_col < 0) {
        start_col = 0;
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1b) {                 /* ESC: skip an escape sequence */
            i++;
            if (i < n && s[i] == '[') {  /* CSI: ... <final 0x40-0x7e> */
                i++;
                while (i < n) {
                    unsigned char d = (unsigned char)s[i++];
                    if (d >= 0x40 && d <= 0x7e) {
                        break;
                    }
                }
            } else if (i < n) {
                i++;                     /* other 2-byte escape */
            }
            continue;
        }
        /* M363: a tab advances to the next 8-column stop of the ABSOLUTE
         * column, which is why this walk needs `start_col`: the buffer's
         * columns sit after the prompt's, and a tab's width depends on
         * where it lands, not on what it is. Tabs enter the buffer only
         * via paste (typed Tab is completion) and via history recall of a
         * pasted line; before this the math counted a tab as width 1
         * while the terminal jumped up to 8, and every keystroke after a
         * pasted tab repositioned the cursor from wrong geometry. */
        if (c == '\t') {
            int abs = start_col + cols;
            cols += 8 - (abs % 8);
            i++;
            continue;
        }
        /* A codepoint: add its display width (CJK/fullwidth = 2, combining = 0,
         * else 1) and advance by its byte length (M127). */
        {
            jc_size adv = 1;
            unsigned long cp = jc_utf8_decode(s, n, i, &adv);
            cols += jc_utf8_width(cp);
            i += (adv == 0) ? 1 : adv;
        }
    }
    return cols;
}

int jc_term_str_cols(const char *s, jc_size n)
{
    return jc_term_str_cols_from(0, s, n);
}

/* The terminal's column count (TIOCGWINSZ, then $COLUMNS, then 80). */
static int term_cols(int fd)
{
    struct winsize ws;
    const char *env;
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    env = getenv("COLUMNS");
    if (env != NULL) {
        int c = atoi(env);
        if (c > 0) {
            return c;
        }
    }
    return 80;
}

/* Redraw the prompt + buffer and place the cursor, accounting for input that
 * wraps across multiple terminal rows. The previous draw left the cursor
 * `e->cursor_row` rows below the region's first row; we go back up there, erase
 * to the end of the screen, repaint, then move the cursor to its logical spot. */
static void render(struct editor *e, const char *prompt)
{
    char seq[32];
    int cols = term_cols(e->out_fd);
    int plen = jc_term_str_cols(prompt, (jc_size)strlen(prompt));
    /* M363: buffer columns are computed FROM the prompt's end column, so a
     * pasted tab's width matches what the terminal will actually do. */
    int blen = (e->buf.data != NULL)
                   ? jc_term_str_cols_from(plen, e->buf.data, e->buf.len)
                   : 0;
    int ccol = (e->buf.data != NULL)
                   ? jc_term_str_cols_from(plen, e->buf.data, e->cursor)
                   : 0;
    int total = plen + blen;
    int end_row, target_row, target_col, up;

    if (cols < 1) {
        cols = 80;
    }
    end_row = total / cols;
    target_row = (plen + ccol) / cols;
    target_col = (plen + ccol) % cols;

    /* Up to the first row of the region, then erase everything from there down. */
    if (e->cursor_row > 0) {
        jc_snprintf(seq, sizeof(seq), "\x1b[%dA", e->cursor_row);
        ed_write(e, seq);
    }
    ed_write(e, "\r\x1b[J");

    ed_write(e, prompt);
    if (e->buf.data != NULL && e->buf.len > 0) {
        ed_write_n(e, e->buf.data, e->buf.len);
    }
    /* When the text fills the final row exactly, terminals leave the cursor in a
     * "phantom" last column; a CR/LF resolves it to a known (row, col 0). */
    if (total > 0 && (total % cols) == 0) {
        ed_write(e, "\r\n");
    }

    /* Now at the end position (end_row, end_col); move back to the cursor. */
    up = end_row - target_row;
    if (up > 0) {
        jc_snprintf(seq, sizeof(seq), "\x1b[%dA", up);
        ed_write(e, seq);
    }
    ed_write(e, "\r");
    if (target_col > 0) {
        jc_snprintf(seq, sizeof(seq), "\x1b[%dC", target_col);
        ed_write(e, seq);
    }
    e->cursor_row = target_row;
}

/* Redraw, then overlay the pending suggestion dimmed after the cursor, leaving
 * the cursor at its logical (pre-ghost) position. Only used with the cursor at
 * end of buffer; a short ghost on a non-wrapping line positions exactly, and a
 * very long one self-heals on the next keystroke (render() erases to screen
 * end). Kept separate from render() so the wrap-aware redraw stays untouched. */
static void render_ghost(struct editor *e, const char *prompt)
{
    char seq[32];
    int gcols;
    int plen;
    int bcols;
    render(e, prompt);
    if (!e->has_ghost || e->ghost[0] == '\0') {
        return;
    }
    ed_write(e, "\x1b[2m");
    ed_write_n(e, e->ghost, strlen(e->ghost));
    ed_write(e, "\x1b[0m");
    /* M363: the ghost starts at the cursor's absolute column, so its width
     * (and the cursor-back distance) is computed from there. */
    plen = jc_term_str_cols(prompt, (jc_size)strlen(prompt));
    bcols = (e->buf.data != NULL)
                ? jc_term_str_cols_from(plen, e->buf.data, e->buf.len) : 0;
    gcols = jc_term_str_cols_from(plen + bcols, e->ghost,
                                  (jc_size)strlen(e->ghost));
    if (gcols > 0) {
        jc_snprintf(seq, sizeof(seq), "\x1b[%dD", gcols);
        ed_write(e, seq);
    }
}

/* Push the current buffer + cursor onto the undo stack (for Ctrl-_). Drops the
 * oldest entry when full (bounded). Best-effort: a failed malloc just skips it. */
static void ed_snapshot(struct editor *e)
{
    char *s;
    int cap = (int)(sizeof(e->undo_buf) / sizeof(e->undo_buf[0]));
    if (e->undo_n >= cap) {
        free(e->undo_buf[0]);
        memmove(e->undo_buf, e->undo_buf + 1, (cap - 1) * sizeof(char *));
        memmove(e->undo_pos, e->undo_pos + 1, (cap - 1) * sizeof(jc_size));
        e->undo_n--;
    }
    s = (char *)malloc(e->buf.len + 1);
    if (s == NULL) return;
    if (e->buf.len > 0) memcpy(s, e->buf.data, e->buf.len);
    s[e->buf.len] = '\0';
    e->undo_buf[e->undo_n] = s;
    e->undo_pos[e->undo_n] = e->cursor;
    e->undo_n++;
}

int jc_term_fast_echo_ok(int ch, int at_end, int prompt_cols,
                         int line_cols_after, int cols)
{
    if (!at_end || cols < 1) {
        return 0;
    }
    if (ch < 32 || ch > 126) {
        return 0; /* multi-byte UTF-8 arrives byte-wise: let render() place it */
    }
    /* Landing exactly on a column boundary leaves the terminal's phantom
     * last-column state, which only render()'s CR/LF resolution handles. */
    if ((prompt_cols + line_cols_after) % cols == 0) {
        return 0;
    }
    return 1;
}

int jc_term_fast_bs_ok(int last_ch, int at_end, int prompt_cols,
                       int line_cols_before, int cols)
{
    if (!at_end || cols < 1) {
        return 0;
    }
    if (last_ch < 32 || last_ch > 126) {
        return 0;
    }
    /* At column 0 of a wrapped row, "\b" cannot cross back up a row. */
    if ((prompt_cols + line_cols_before) % cols == 0) {
        return 0;
    }
    return 1;
}

static void ed_insert(struct editor *e, char c)
{
    /* Coalesce a run of typed chars into one undo step (snapshot only at the
     * start of the run). */
    if (!e->last_insert) ed_snapshot(e);
    e->last_insert = 1;
    /* Append then shift if inserting mid-line. */
    if (e->cursor == e->buf.len) {
        jc_sb_append_char(&e->buf, c);
        e->cursor++;
    } else {
        jc_sb_append_char(&e->buf, c); /* grow by one */
        memmove(e->buf.data + e->cursor + 1, e->buf.data + e->cursor,
                e->buf.len - 1 - e->cursor);
        e->buf.data[e->cursor] = c;
        e->cursor++;
    }
}

static void ed_backspace(struct editor *e)
{
    jc_size start;
    jc_size nbytes;
    if (e->cursor == 0) {
        return;
    }
    ed_snapshot(e);
    e->last_insert = 0;
    /* Delete the whole UTF-8 codepoint before the cursor, not one byte (M127). */
    start = jc_utf8_prev(e->buf.data, e->cursor);
    nbytes = e->cursor - start;
    memmove(e->buf.data + start, e->buf.data + e->cursor,
            e->buf.len - e->cursor);
    e->buf.len -= nbytes;
    e->cursor = start;
    e->buf.data[e->buf.len] = '\0';
}

static void ed_set(struct editor *e, const char *s)
{
    jc_sb_clear(&e->buf);
    jc_sb_append(&e->buf, s);
    e->cursor = e->buf.len;
}

/* Replace the span [start, cursor) with `repl`; leaves the cursor after it. */
static void ed_replace(struct editor *e, jc_size start, const char *repl)
{
    struct jc_sb nb;
    jc_size rl = (jc_size)strlen(repl);
    if (start > e->buf.len) start = e->buf.len;
    jc_sb_init(&nb);
    if (e->buf.data != NULL && start > 0) {
        jc_sb_append_n(&nb, e->buf.data, start);
    }
    jc_sb_append(&nb, repl);
    if (e->buf.data != NULL && e->cursor < e->buf.len) {
        jc_sb_append_n(&nb, e->buf.data + e->cursor, e->buf.len - e->cursor);
    }
    jc_sb_free(&e->buf);
    e->buf = nb;
    e->cursor = start + rl;
}

/* Kill the byte range [from, to): save it to the kill-ring (for Ctrl-Y), splice
 * it out, and put the cursor at `from`. Clamps + no-op on an empty range (M126). */
static void ed_kill_range(struct editor *e, jc_size from, jc_size to)
{
    if (to > e->buf.len) to = e->buf.len;
    if (from >= to) return;
    ed_snapshot(e);
    e->last_insert = 0;
    jc_sb_clear(&e->kill);
    jc_sb_append_n(&e->kill, e->buf.data + from, to - from);
    memmove(e->buf.data + from, e->buf.data + to, e->buf.len - to);
    e->buf.len -= (to - from);
    e->buf.data[e->buf.len] = '\0';
    e->cursor = from;
}

/* Yank (paste) the kill-ring at the cursor (Ctrl-Y). */
static void ed_yank(struct editor *e)
{
    jc_size n;
    if (e->kill.data == NULL || e->kill.len == 0) return;
    ed_snapshot(e);
    e->last_insert = 0;
    n = e->kill.len;
    /* grow buf by n, shift the tail, drop the killed text in */
    { jc_size i; for (i = 0; i < n; i++) jc_sb_append_char(&e->buf, ' '); }
    memmove(e->buf.data + e->cursor + n, e->buf.data + e->cursor,
            e->buf.len - n - e->cursor);
    memcpy(e->buf.data + e->cursor, e->kill.data, n);
    e->cursor += n;
    e->buf.data[e->buf.len] = '\0';
}

/* Transpose the two chars around the cursor (Ctrl-T), readline-style. */
static void ed_transpose(struct editor *e)
{
    char tmp;
    jc_size a;
    if (e->buf.len < 2 || e->cursor == 0) return;
    /* Transpose ASCII only -- swapping raw bytes of a multibyte char would
     * corrupt the UTF-8 (M127). No-op when either side is multibyte. */
    a = (e->cursor >= e->buf.len) ? e->buf.len - 2 : e->cursor - 1;
    if (((unsigned char)e->buf.data[a] & 0x80) ||
        ((unsigned char)e->buf.data[a + 1] & 0x80)) return;
    ed_snapshot(e);
    e->last_insert = 0;
    if (e->cursor >= e->buf.len) {
        tmp = e->buf.data[e->buf.len - 1];
        e->buf.data[e->buf.len - 1] = e->buf.data[e->buf.len - 2];
        e->buf.data[e->buf.len - 2] = tmp;
    } else {
        tmp = e->buf.data[e->cursor];
        e->buf.data[e->cursor] = e->buf.data[e->cursor - 1];
        e->buf.data[e->cursor - 1] = tmp;
        e->cursor++;
    }
}

/* Delete the char under the cursor (forward-delete; the Delete key / Ctrl-D). */
static void ed_forward_delete(struct editor *e)
{
    jc_size end;
    jc_size nbytes;
    if (e->cursor >= e->buf.len) return;
    ed_snapshot(e);
    e->last_insert = 0;
    /* Delete the whole UTF-8 codepoint at the cursor, not one byte (M127). */
    end = jc_utf8_next(e->buf.data, e->buf.len, e->cursor);
    nbytes = end - e->cursor;
    memmove(e->buf.data + e->cursor, e->buf.data + end, e->buf.len - end);
    e->buf.len -= nbytes;
    e->buf.data[e->buf.len] = '\0';
}

/* Undo: pop the last snapshot and restore the buffer + cursor (Ctrl-_). */
static void ed_undo(struct editor *e)
{
    char *s;
    jc_size pos;
    if (e->undo_n == 0) return;
    e->undo_n--;
    s = e->undo_buf[e->undo_n];
    pos = e->undo_pos[e->undo_n];
    ed_set(e, s);                              /* leaves cursor at end */
    e->cursor = (pos <= e->buf.len) ? pos : e->buf.len;
    free(s);
    e->last_insert = 0;
}

/* Change case of the word from the cursor to its end, moving the cursor there
 * (Alt-U upcase / Alt-L downcase / Alt-C capitalize), readline-style. */
static void ed_case_word(struct editor *e, int mode) /* 0=up 1=down 2=cap */
{
    jc_size end = jc_line_word_right(e->buf.data, e->buf.len, e->cursor);
    jc_size i;
    int seen = 0;
    if (end <= e->cursor) return; /* no word ahead */
    ed_snapshot(e);
    e->last_insert = 0;
    for (i = e->cursor; i < end; i++) {
        char c = e->buf.data[i];
        int lower = (c >= 'a' && c <= 'z');
        int upper = (c >= 'A' && c <= 'Z');
        if (!lower && !upper) continue;
        if (mode == 0) {
            if (lower) e->buf.data[i] = (char)(c - 32);
        } else if (mode == 1) {
            if (upper) e->buf.data[i] = (char)(c + 32);
        } else { /* capitalize: first letter up, rest down */
            if (!seen) { if (lower) e->buf.data[i] = (char)(c - 32); seen = 1; }
            else { if (upper) e->buf.data[i] = (char)(c + 32); }
        }
    }
    e->cursor = end;
}

/* Replace the buffer from history at the current navigation position. */
static void hist_show(struct editor *e, struct jc_term *t)
{
    if (e->hist_pos >= 0 && (jc_size)e->hist_pos < t->history.len) {
        const char *h = *(char **)jc_vec_at(&t->history, (jc_size)e->hist_pos);
        ed_set(e, h);
    } else {
        ed_set(e, "");
    }
}

/* History step to an older / newer entry (Up / Ctrl-P and Down / Ctrl-N). */
static void hist_older(struct editor *e, struct jc_term *t)
{
    if (t->history.len == 0) return;
    if (e->hist_pos < 0) e->hist_pos = (int)t->history.len - 1;
    else if (e->hist_pos > 0) e->hist_pos--;
    hist_show(e, t);
}

static void hist_newer(struct editor *e, struct jc_term *t)
{
    if (e->hist_pos < 0) return;
    e->hist_pos++;
    if ((jc_size)e->hist_pos >= t->history.len) e->hist_pos = -1;
    hist_show(e, t);
}

/* Ctrl-R incremental reverse history search (M69): build a query and show the
 * newest matching history entry on the current line. Enter loads the match into
 * the editor for further editing; Ctrl-R steps to the next older match;
 * Esc/Ctrl-C cancels (buffer untouched). Stays on one physical line. */
static void reverse_search(struct jc_term *t, struct editor *e)
{
    char q[256];
    jc_size ql = 0;
    int from = (int)t->history.len - 1;
    q[0] = '\0';
    for (;;) {
        int matched = -1;
        int i;
        unsigned char ch;
        for (i = from; i >= 0; i--) {
            const char *h = *(char **)jc_vec_at(&t->history, (jc_size)i);
            if (ql == 0 || (h != NULL && strstr(h, q) != NULL)) {
                matched = i;
                break;
            }
        }
        ed_write(e, "\r\x1b[K(reverse-i-search)`");
        if (ql > 0) {
            ed_write_n(e, q, ql);
        }
        ed_write(e, "': ");
        if (matched >= 0) {
            ed_write(e, *(char **)jc_vec_at(&t->history, (jc_size)matched));
        }
        if (read(t->in_fd, &ch, 1) <= 0) {
            break;
        }
        if (ch == '\r' || ch == '\n') {
            if (matched >= 0) {
                ed_set(e, *(char **)jc_vec_at(&t->history, (jc_size)matched));
            }
            break;
        } else if (ch == 27 || ch == 3) {        /* Esc / Ctrl-C: cancel */
            break;
        } else if (ch == 18) {                    /* Ctrl-R: next older match */
            from = (matched >= 0) ? matched - 1 : from - 1;
        } else if (ch == 127 || ch == 8) {        /* backspace */
            if (ql > 0) {
                ql--;
                q[ql] = '\0';
            }
            from = (int)t->history.len - 1;
        } else if (ch >= 32 && ql < sizeof(q) - 1) {
            q[ql++] = (char)ch;
            q[ql] = '\0';
            from = (int)t->history.len - 1;
        }
    }
    ed_write(e, "\r\x1b[K");   /* clear the search line; caller re-renders */
    e->cursor_row = 0;
}

/* True if a byte is immediately readable without blocking. The burst-paste
 * fallback (M156): at a bare newline, pending input means the newline is part
 * of a paste, not a typed Enter -- a human can't have the next keystroke
 * already buffered in the inter-byte instant (VMIN=1). */
static int input_pending(int fd)
{
    fd_set r;
    struct timeval tv;
    FD_ZERO(&r);
    FD_SET(fd, &r);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return select(fd + 1, &r, NULL, NULL, &tv) > 0;
}

/* Commit the current edit line to the multi-line accumulator and start a
 * fresh row -- the shared tail of both the M69 trailing-`\` path and a paste
 * line break. Echoes the committed row, resets the buffer. */
static void commit_line(struct editor *e, const char *prompt,
                        struct jc_sb *multi, int *multiline)
{
    e->cursor = e->buf.len;
    render(e, prompt);
    ed_write(e, "\r\n");
    e->cursor_row = 0;
    jc_sb_append_n(multi, e->buf.data != NULL ? e->buf.data : "", e->buf.len);
    jc_sb_append_char(multi, '\n');
    *multiline = 1;
    ed_set(e, "");
    render(e, prompt);
}

/* M156: splice pasted text (raw, possibly multi-line) into the buffer at the
 * cursor, reusing the multi-line accumulator so no newline-aware render is
 * needed. Each line but the last is committed (echoed) via commit_line; the
 * last segment becomes the editable buffer with the cursor just past the
 * pasted text. A paste never submits -- a real Enter does. */
static void apply_paste(struct editor *e, const char *prompt,
                        struct jc_sb *multi, int *multiline,
                        const char *pasted)
{
    char *before;
    char *combined;
    jc_size cur = 0;
    jc_size i;
    jc_size seg_start;
    jc_size consumed;
    jc_size clen;

    /* Split the current buffer at the cursor into before/after. */
    before = (char *)malloc(e->cursor + 1);
    if (before == NULL) {
        return;
    }
    if (e->cursor > 0 && e->buf.data != NULL) {
        memcpy(before, e->buf.data, e->cursor);
    }
    before[e->cursor] = '\0';

    combined = jc_paste_splice(before,
        (e->buf.data != NULL) ? e->buf.data + e->cursor : "", pasted, &cur);
    free(before);
    if (combined == NULL) {
        return;
    }
    clen = (jc_size)strlen(combined);

    /* Commit every line except the last; the last becomes the editable row. */
    seg_start = 0;
    consumed = 0;
    for (i = 0; i <= clen; i++) {
        if (i == clen || combined[i] == '\n') {
            if (i < clen) {
                /* a committed line: [seg_start, i) */
                char *line = (char *)malloc(i - seg_start + 1);
                if (line != NULL) {
                    if (i > seg_start) {
                        memcpy(line, combined + seg_start, i - seg_start);
                    }
                    line[i - seg_start] = '\0';
                    ed_set(e, line);
                    commit_line(e, prompt, multi, multiline);
                    free(line);
                }
                consumed = i + 1; /* past this line + its '\n' */
                seg_start = i + 1;
            } else {
                /* the final segment: the editable tail */
                char *tail = (char *)malloc(i - seg_start + 1);
                if (tail != NULL) {
                    if (i > seg_start) {
                        memcpy(tail, combined + seg_start, i - seg_start);
                    }
                    tail[i - seg_start] = '\0';
                    ed_set(e, tail);
                    free(tail);
                }
                e->cursor = (cur >= consumed) ? (cur - consumed) : 0;
                if (e->cursor > e->buf.len) {
                    e->cursor = e->buf.len;
                }
                render(e, prompt);
            }
        }
    }
    free(combined);
}

/* Read a bracketed paste's body from `fd` (already past ESC[200~) until the
 * ESC[201~ terminator, into `out`; the terminator is not included. Bounded so
 * a malformed stream can't grow unbounded. */
static void read_paste_body(int fd, struct jc_sb *out)
{
    static const char END[] = "\x1b[201~";
    const jc_size endlen = 6;
    const jc_size cap = 1024 * 1024;
    for (;;) {
        unsigned char ch;
        if (read(fd, &ch, 1) <= 0) {
            return; /* EOF / error: stop with what we have */
        }
        jc_sb_append_char(out, (char)ch);
        if (out->len >= endlen &&
            memcmp(out->data + out->len - endlen, END, endlen) == 0) {
            out->len -= endlen;              /* drop the terminator */
            out->data[out->len] = '\0';
            return;
        }
        if (out->len > cap) {
            /* M363: a paste past the cap used to just RETURN here -- which
             * left the rest of the paste, terminator included, in the input
             * queue to be replayed as KEYSTROKES: pasted ESC sequences ran
             * as key escapes, a pasted 'q' answered the next prompt. That
             * is the exact injection bracketed paste exists to prevent,
             * reintroduced at the overflow edge. Keep the capped content and
             * DRAIN to the terminator with a rolling window seeded from the
             * stream's current tail, so a terminator STRADDLING the cap
             * boundary is still found; any of its bytes that had landed in
             * the kept content are trimmed off. Nothing past the cap reaches
             * the key loop. */
            char win[6];
            jc_size d = 0;      /* bytes drained after the cap */
            memcpy(win, out->data + out->len - endlen, endlen);
            out->len = cap;
            out->data[out->len] = '\0';
            for (;;) {
                if (read(fd, &ch, 1) <= 0) {
                    return;
                }
                d++;
                memmove(win, win + 1, endlen - 1);
                win[endlen - 1] = (char)ch;
                if (memcmp(win, END, endlen) == 0) {
                    /* Stream tail = kept straddle + the 1 dropped byte +
                     * the d drained bytes; trim the straddle from content. */
                    jc_size straddle =
                        (d + 1 < endlen) ? (endlen - 1 - d) : 0;
                    if (straddle > out->len) {
                        straddle = out->len;
                    }
                    out->len -= straddle;
                    out->data[out->len] = '\0';
                    return;
                }
            }
        }
    }
}

jc_read_result jc_term_readline(struct jc_term *t, const char *prompt,
                                char **out)
{
    struct termios saved;
    struct editor e;
    jc_read_result result = JC_READ_LINE;
    int running = 1;
    struct jc_sb multi;   /* committed lines when using \-continuation (M69) */
    int multiline = 0;
    int last_cr = 0;  /* M363: the previous key was a committed '\r' */

    *out = NULL;

    if (!t->is_tty) {
        /* Non-interactive: read a cooked line from stdin. */
        struct jc_sb sb;
        int c;
        jc_sb_init(&sb);
        while ((c = getchar()) != EOF && c != '\n') {
            jc_sb_append_char(&sb, (char)c);
        }
        if (c == EOF && sb.len == 0) {
            jc_sb_free(&sb);
            return JC_READ_EOF;
        }
        *out = jc_sb_finish(&sb);
        jc_sb_free(&sb);
        return JC_READ_LINE;
    }

    if (enter_raw(t->in_fd, &saved, &t->flushed_pending) != 0) {
        return JC_READ_EOF;
    }
    if (t->flushed_pending) {
        /* Say it, then forget it. The bytes are NOT recovered on purpose: the
         * flush is what stops stray type-ahead from answering a y/n approval
         * prompt, which is a safety property worth more than the keystrokes.
         * So the honest fix is to tell the person to retype, not to smuggle the
         * input back in. Written with \r\n because OPOST is off in raw mode. */
        t->flushed_pending = 0;
        {
            static const char msg[] =
                "\r\nnote: input typed before this prompt was discarded (it "
                "could otherwise have answered a prompt you had not read yet) "
                "-- please retype it.\r\n";
            ssize_t w = write(t->out_fd, msg, sizeof(msg) - 1);
            (void)w;
        }
    }

    jc_sb_init(&e.buf);
    jc_sb_init(&multi);
    e.cursor = 0;
    e.out_fd = t->out_fd;
    e.hist_pos = -1;
    e.cursor_row = 0;
    e.ghost[0] = '\0';
    e.has_ghost = 0;
    jc_sb_init(&e.kill);
    e.undo_n = 0;
    e.last_insert = 0;
    /* M558: unconditional -- see jc_term.h. The predicates below refuse every
     * case where this differs from a full repaint, so the old mode flag only
     * chose whether to take the cheaper of two identical outcomes. */
    e.fast_echo = 1;

    render(&e, prompt);

    /* M156: enable bracketed paste for the duration of this read, so a pasted
     * newline arrives inside ESC[200~ ... ESC[201~ and is treated as content,
     * not as Enter. Scoped here (not in enter_raw), so single-key prompts via
     * jc_term_read_key are unaffected. Disabled at teardown below. */
    ed_write(&e, "\x1b[?2004h");

    while (running) {
        unsigned char ch;
        int was_cr;
        ssize_t n = read(t->in_fd, &ch, 1);
        if (n <= 0) {
            result = JC_READ_EOF;
            break;
        }
        /* M363: the CRLF flag lives exactly one key -- any byte other than
         * the immediate LF half of a CRLF pair clears it, so a lone newline
         * typed later is never swallowed. */
        was_cr = last_cr;
        last_cr = 0;

        /* A pending ghost suggestion: Tab accepts it; anything else dismisses
         * it (and is then handled normally below). */
        if (e.has_ghost) {
            e.has_ghost = 0;
            if (ch == 9) {                        /* Tab accepts */
                jc_sb_append(&e.buf, e.ghost);
                e.cursor = e.buf.len;
                e.ghost[0] = '\0';
                render(&e, prompt);
                continue;
            }
            e.ghost[0] = '\0';
            render(&e, prompt);                   /* erase the overlay */
            /* fall through to handle ch normally */
        }

        if (ch == '\r' || ch == '\n') {
            /* M363: a non-bracketed CRLF paste delivers BOTH bytes of each
             * line break through this branch -- the '\r' commits the row,
             * then the pending '\n' used to commit a second, EMPTY row, so
             * a Windows-lineage paste gained a blank line per row. The LF
             * half of a CRLF pair is swallowed; the flag was captured and
             * cleared at the loop top, so it lives exactly one key. */
            last_cr = (ch == '\r');
            if (ch == '\n' && was_cr) {
                continue;
            }
            /* A newline commits a row (not submit) in two cases: a trailing
             * backslash (M69 explicit continuation), or -- the burst-paste
             * fallback for terminals without bracketed paste (M156) -- when
             * more input is immediately pending, meaning this newline is part
             * of a pasted block rather than a typed Enter. Otherwise submit. */
            if (e.buf.len > 0 && e.buf.data[e.buf.len - 1] == '\\') {
                e.buf.len--;                      /* strip the continuation '\' */
                e.buf.data[e.buf.len] = '\0';
                commit_line(&e, prompt, &multi, &multiline);
            } else if (input_pending(t->in_fd)) {
                commit_line(&e, prompt, &multi, &multiline);
            } else {
                running = 0;
            }
        } else if (ch == 18) {                    /* Ctrl-R: history search */
            reverse_search(t, &e);
            render(&e, prompt);
        } else if (ch == 127 || ch == 8) {       /* backspace */
            /* M362: accessible fast path -- erase one cell in place. The
             * last-char and geometry checks run BEFORE the edit, on the
             * state the predicate describes. */
            int fast = 0;
            if (e.fast_echo && e.buf.len > 0 && e.cursor == e.buf.len) {
                int plen = jc_term_str_cols(prompt,
                                            (jc_size)strlen(prompt));
                fast = jc_term_fast_bs_ok(
                    (int)(unsigned char)e.buf.data[e.buf.len - 1], 1,
                    plen,
                    jc_term_str_cols_from(plen, e.buf.data, e.buf.len),
                    term_cols(e.out_fd));
            }
            ed_backspace(&e);
            if (fast) {
                ed_write(&e, "\b \b");
            } else {
                render(&e, prompt);
            }
        } else if (ch == 3) {                     /* Ctrl-C */
            if (e.buf.len > 0) {
                ed_set(&e, "");
                render(&e, prompt);
            } else {
                result = JC_READ_INTR;
                running = 0;
            }
        } else if (ch == 4) {                     /* Ctrl-D */
            if (e.buf.len == 0) {
                result = JC_READ_EOF;
                running = 0;
            } else {
                /* bash parity: delete the char under the cursor. */
                ed_forward_delete(&e);
                render(&e, prompt);
            }
        } else if (ch == 12) {                    /* Ctrl-L */
            ed_write(&e, "\x1b[2J\x1b[H");
            e.cursor_row = 0;                     /* cursor is now at home */
            render(&e, prompt);
        } else if (ch == 7) {                     /* Ctrl-G: inline suggestion */
            if (t->suggest != NULL && e.cursor == e.buf.len &&
                e.buf.len > 0) {
                jc_size m = t->suggest(t->suggest_ctx,
                                       e.buf.data != NULL ? e.buf.data : "",
                                       e.cursor, e.ghost, sizeof(e.ghost));
                if (m > 0) {
                    e.has_ghost = 1;
                    if (t->suggest_announce != NULL) {
                        /* M578: ANNOUNCED, not ghosted. The inline ghost marks
                         * itself as "not yours" with \x1b[2m and nothing else,
                         * so a listener hears the model's words inside their own
                         * sentence -- measured by the operator, whose reader
                         * spoke a suggested Japanese word as though they had
                         * typed it.
                         *
                         * This is the adviser's rendering (Ctrl-Q, M280), which
                         * this header already argues for on the same grounds: a
                         * model's reply spliced into the line produces garbled
                         * input, so give it its own labelled line. The label is
                         * a WORD, which is what carries the distinction when
                         * dim does not. No escape sequence at all here: the
                         * whole point is that the signal survives without one.
                         *
                         * has_ghost STAYS SET, so Tab still accepts it. What
                         * changes is where it is drawn, not what it is. */
                        ed_write(&e, "\r\n  ");
                        ed_write_n(&e, t->suggest_announce,
                                   strlen(t->suggest_announce));
                        ed_write(&e, " ");
                        ed_write_n(&e, e.ghost, strlen(e.ghost));
                        ed_write(&e, "\r\n");
                        render(&e, prompt);
                    } else {
                        render_ghost(&e, prompt);
                    }
                }
            }
        } else if (ch == 17) {                    /* Ctrl-Q: prompt advice */
            /* Printed on a fresh line ABOVE a redrawn prompt -- the same
             * mechanism Tab uses to list candidates -- and never spliced into
             * the buffer. That separation is the whole point of the gesture
             * (M280): the reply is commentary ON the request, not a
             * continuation OF it. Ctrl-Q is safe to bind because raw mode
             * clears IXON, so it is not flow control here. */
            if (t->advise != NULL && e.buf.len > 0) {
                char note[256];
                jc_size m = t->advise(t->advise_ctx,
                                      e.buf.data != NULL ? e.buf.data : "",
                                      e.cursor, note, sizeof(note));
                if (m > 0) {
                    ed_write(&e, "\r\n");
                    ed_write(&e, "\x1b[2m  advice: ");
                    ed_write_n(&e, note, strlen(note));
                    ed_write(&e, "\x1b[0m\r\n");
                    e.cursor_row = 0;   /* printed on fresh lines */
                    render(&e, prompt);
                }
            }
        } else if (ch == 1) {                     /* Ctrl-A: home */
            e.cursor = 0;
            render(&e, prompt);
        } else if (ch == 5) {                     /* Ctrl-E: end */
            e.cursor = e.buf.len;
            render(&e, prompt);
        } else if (ch == 2) {                     /* Ctrl-B: char left */
            e.cursor = jc_utf8_prev(e.buf.data, e.cursor);
            render(&e, prompt);
        } else if (ch == 6) {                     /* Ctrl-F: char right */
            e.cursor = jc_utf8_next(e.buf.data, e.buf.len, e.cursor);
            render(&e, prompt);
        } else if (ch == 16) {                    /* Ctrl-P: older history */
            hist_older(&e, t);
            render(&e, prompt);
        } else if (ch == 14) {                    /* Ctrl-N: newer history */
            hist_newer(&e, t);
            render(&e, prompt);
        } else if (ch == 21) {                    /* Ctrl-U: kill to start */
            ed_kill_range(&e, 0, e.cursor);
            render(&e, prompt);
        } else if (ch == 11) {                    /* Ctrl-K: kill to end */
            ed_kill_range(&e, e.cursor, e.buf.len);
            render(&e, prompt);
        } else if (ch == 23) {                    /* Ctrl-W: kill word back */
            jc_size w = jc_line_word_left(e.buf.data, e.buf.len, e.cursor);
            ed_kill_range(&e, w, e.cursor);
            render(&e, prompt);
        } else if (ch == 25) {                    /* Ctrl-Y: yank */
            ed_yank(&e);
            render(&e, prompt);
        } else if (ch == 20) {                    /* Ctrl-T: transpose */
            ed_transpose(&e);
            render(&e, prompt);
        } else if (ch == 31) {                    /* Ctrl-_ : undo */
            ed_undo(&e);
            render(&e, prompt);
        } else if (ch == 9) {                     /* Tab: completion */
            if (t->complete != NULL) {
                struct jc_vec cands;
                jc_size tstart = e.cursor;
                int n;
                jc_vec_init(&cands, sizeof(char *));
                n = t->complete(t->complete_ctx,
                                e.buf.data != NULL ? e.buf.data : "",
                                e.cursor, &tstart, &cands);
                if (n == 1) {
                    ed_replace(&e, tstart, *(char **)jc_vec_at(&cands, 0));
                    render(&e, prompt);
                } else if (n > 1) {
                    char pfx[512];
                    jc_size plen = jc_complete_common_prefix(
                        (const char *const *)cands.data, n, pfx, sizeof(pfx));
                    if (plen > e.cursor - tstart) {
                        ed_replace(&e, tstart, pfx);
                        render(&e, prompt);
                    } else {
                        jc_size k;
                        ed_write(&e, "\r\n");
                        for (k = 0; k < cands.len && k < 40; k++) {
                            ed_write(&e, *(char **)jc_vec_at(&cands, k));
                            ed_write(&e, "  ");
                        }
                        if (cands.len > 40) ed_write(&e, "...");
                        ed_write(&e, "\r\n");
                        e.cursor_row = 0; /* candidates printed on fresh lines */
                        render(&e, prompt);
                    }
                }
                {
                    jc_size k;
                    for (k = 0; k < cands.len; k++) {
                        free(*(char **)jc_vec_at(&cands, k));
                    }
                }
                jc_vec_free(&cands);
            }
        } else if (ch == 27) {                    /* escape sequence */
            unsigned char a, b;
            if (read(t->in_fd, &a, 1) <= 0) continue;
            /* Alt-<key>: ESC followed by a plain byte (not a CSI/SS3 prefix).
             * These are the word-level readline bindings (M126). */
            if (a != '[' && a != 'O') {
                if (a == 'b') {                   /* Alt-B: word left */
                    e.cursor = jc_line_word_left(e.buf.data, e.buf.len,
                                                 e.cursor);
                    render(&e, prompt);
                } else if (a == 'f') {            /* Alt-F: word right */
                    e.cursor = jc_line_word_right(e.buf.data, e.buf.len,
                                                  e.cursor);
                    render(&e, prompt);
                } else if (a == 'd') {            /* Alt-D: kill word forward */
                    jc_size w = jc_line_word_right(e.buf.data, e.buf.len,
                                                   e.cursor);
                    ed_kill_range(&e, e.cursor, w);
                    render(&e, prompt);
                } else if (a == 127 || a == 8) {  /* Alt-Backspace: kill word back */
                    jc_size w = jc_line_word_left(e.buf.data, e.buf.len,
                                                  e.cursor);
                    ed_kill_range(&e, w, e.cursor);
                    render(&e, prompt);
                } else if (a == 'u') {            /* Alt-U: upcase word */
                    ed_case_word(&e, 0);
                    render(&e, prompt);
                } else if (a == 'l') {            /* Alt-L: downcase word */
                    ed_case_word(&e, 1);
                    render(&e, prompt);
                } else if (a == 'c') {            /* Alt-C: capitalize word */
                    ed_case_word(&e, 2);
                    render(&e, prompt);
                }
                continue;
            }
            if (read(t->in_fd, &b, 1) <= 0) continue;
            if (b == 'C') {                       /* right */
                e.cursor = jc_utf8_next(e.buf.data, e.buf.len, e.cursor);
                render(&e, prompt);
            } else if (b == 'D') {                /* left */
                e.cursor = jc_utf8_prev(e.buf.data, e.cursor);
                render(&e, prompt);
            } else if (b == 'A') {                /* up: older history */
                if (t->history.len > 0) {
                    if (e.hist_pos < 0) {
                        e.hist_pos = (int)t->history.len - 1;
                    } else if (e.hist_pos > 0) {
                        e.hist_pos--;
                    }
                    hist_show(&e, t);
                    render(&e, prompt);
                }
            } else if (b == 'B') {                /* down: newer history */
                if (e.hist_pos >= 0) {
                    e.hist_pos++;
                    if ((jc_size)e.hist_pos >= t->history.len) {
                        e.hist_pos = -1;
                    }
                    hist_show(&e, t);
                    render(&e, prompt);
                }
            } else if (b == 'H') {
                e.cursor = 0; render(&e, prompt);
            } else if (b == 'F') {
                e.cursor = e.buf.len; render(&e, prompt);
            } else if (b == '3') {                /* delete: read trailing ~ */
                unsigned char tilde;
                if (read(t->in_fd, &tilde, 1) > 0 && e.cursor < e.buf.len) {
                    ed_forward_delete(&e); /* whole codepoint (M127) */
                    render(&e, prompt);
                }
            } else if (b == '2') {                /* ESC[2..~: paste or Insert */
                unsigned char c2, c3, c4;
                if (read(t->in_fd, &c2, 1) <= 0) continue;
                if (c2 == '~') {                  /* ESC[2~ Insert: ignore */
                    continue;
                }
                if (c2 != '0') continue;          /* not 20x~: ignore */
                if (read(t->in_fd, &c3, 1) <= 0) continue;
                if (read(t->in_fd, &c4, 1) <= 0 || c4 != '~') continue;
                if (c3 == '0') {                  /* ESC[200~ paste start */
                    struct jc_sb pb;
                    jc_sb_init(&pb);
                    read_paste_body(t->in_fd, &pb);
                    apply_paste(&e, prompt, &multi, &multiline,
                                pb.data != NULL ? pb.data : "");
                    jc_sb_free(&pb);
                }
                /* c3 == '1' (a stray ESC[201~): ignore. */
            }
        } else if (ch >= 32) {                    /* printable */
            /* M362: accessible fast path -- append-echo the one character,
             * exactly what a cooked terminal would do, instead of the full
             * wrap-aware redraw (~39 bytes + ESC[J per keystroke, which a
             * screen reader re-announces as a changed line every time). Any
             * ghost overlay was already erased by the dismissal render
             * above; every non-trivial case falls back to render(). */
            int at_end = (e.cursor == e.buf.len);
            int fplen = jc_term_str_cols(prompt, (jc_size)strlen(prompt));
            ed_insert(&e, (char)ch);
            if (e.fast_echo &&
                jc_term_fast_echo_ok(ch, at_end,
                    fplen,
                    (e.buf.data != NULL)
                        ? jc_term_str_cols_from(fplen, e.buf.data, e.buf.len)
                        : 0,
                    term_cols(e.out_fd))) {
                char cb[2];
                cb[0] = (char)ch;
                cb[1] = '\0';
                ed_write(&e, cb);
            } else {
                render(&e, prompt);
            }
        }
    }

    /* Put the cursor at the end of the (possibly wrapped) input before the
     * closing newline, so nothing is left mid-region. */
    e.cursor = e.buf.len;
    render(&e, prompt);
    ed_write(&e, "\x1b[?2004l"); /* M156: disable bracketed paste (all exits) */
    leave_raw(t->in_fd, &saved);
    ed_write(&e, "\r\n");

    if (result == JC_READ_LINE) {
        if (multiline) {
            /* Append the final line to the committed continuation lines. */
            jc_sb_append_n(&multi, e.buf.data != NULL ? e.buf.data : "",
                           e.buf.len);
            *out = jc_sb_finish(&multi);
        } else {
            *out = jc_sb_finish(&e.buf);
        }
        hist_add(t, *out);
    }
    jc_sb_free(&e.buf);
    jc_sb_free(&e.kill);
    while (e.undo_n > 0) free(e.undo_buf[--e.undo_n]);
    jc_sb_free(&multi);
    return result;
}
