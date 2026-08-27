/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tui.c - interactive REPL (see jc_tui.h). */

#include "jc_tui.h"
#include "jc_platform.h"
#include "jc_voice.h"
#include "jc_term.h"
#include "jc_agent.h"
#include "jc_message.h"
#include "jc_session.h"
#include "jc_mcp.h"
#include "jc_perm.h"
#include "jc_compact.h"
#include "jc_context.h"
#include "jc_telemetry.h"
#include "jc_snapshot.h"
#include "jc_configedit.h"
#include "jc_confbench.h"
#include "jc_packages.h"
#include "jc_oneshot.h"
#include "jc_bg.h"
#include "jc_skill.h"
#include "jc_repomap.h"
#include "jc_refs.h"
#include "jc_autocontext.h"
#include "jc_notify.h"
#include "jc_learn.h"
#include "jc_eventlog.h"
#include "jc_assign.h"
#include "jc_gradecore.h"
#include "jc_progress.h"
#include "jc_testparse.h"
#include "jc_proc.h"
#include "jc_tool.h"
#include "jc_complete.h"
#include "jc_suggest.h"
#include "jc_json.h"
#include "jc_patch.h"
#include "jc_diff.h"
#include "jc_toolloop.h"  /* M571: a refusal is not a failure */
#include "jc_mdrender.h"
#include "jc_msg.h"
#include "jc_provider.h"
#include "jc_http.h"
#include "jc_session.h"
#include "jc_command.h"
#include "jc_envelope.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_cli.h"
#include "jc_term.h"
#include "jc_vec.h"
#include "jc_utf8.h"   /* jc_ctrl_sanitize (M472) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ANSI helpers (only emitted to a TTY). */
#define C_RESET  "\x1b[0m"
#define C_DIM    "\x1b[2m"
#define C_BOLD   "\x1b[1m"
#define C_GREEN  "\x1b[1;32m"
#define C_CYAN   "\x1b[36m"
#define C_RED    "\x1b[31m"
#define C_YELLOW "\x1b[33m"

struct tui_ctx {
    struct jc_app  *app;
    struct jc_term *term;    /* for single-key approval prompts            */
    int             color;
    int             unicode; /* UTF-8 locale: use ▸/✓/✗ vs ASCII fallback  */
    int             markdown;/* render assistant text as markdown/syntax    */
    int             accessible; /* M118: reduce motion (no spinner), linear   */
    int             quiet;   /* minimal output: hide header/tokens/preview   */
    int             nested_bol; /* nested (subagent) output at a line start  */
    struct jc_sb    voice_buf;  /* M303: the reply text, accumulated so it can
                                 * be spoken as ONE utterance at message end --
                                 * synthesising each delta would stutter, and
                                 * TTS needs a whole sentence to prosody it   */
    struct jc_sb    acc_buf;    /* M549: accessible mode -- the SAME argument
                                 * as voice_buf, for the reader the USER runs.
                                 * A screen reader announces terminal text as
                                 * it appears, so a 3-character delta is a
                                 * 3-character utterance. Measured: 266 deltas
                                 * for 926 chars, 62% of them <= 3 bytes. Held
                                 * here until a newline, so Orca gets whole
                                 * lines instead of stuttered fragments.      */
    int             thinking;   /* a transient "working…" line is on screen  */
    int             spin;       /* spinner frame index (M66)                 */
    double          think_start_ms;  /* when the wait began (elapsed display) */
    double          think_render_ms; /* last spinner re-render (throttle)     */
    char            board[32][96];   /* parallel board rows by agent index    */
    int             board_n;     /* highest agent index seen (rows tracked)   */
    int             board_shown; /* rows currently on screen (cursor-up count)*/
    int             board_live;  /* the board block is on screen below cursor */
    struct jc_mdr   md;      /* streaming markdown renderer state           */
    struct jc_vec   always;  /* of char*: tools the user said "always" to   */
    double          tot_in;  /* session token totals                       */
    double          tot_out;
    double          tot_cache_read;  /* session prompt-cache totals (M31c)   */
    double          tot_cache_write;
    double          last_in; /* this message's usage (printed at msg end)   */
    double          last_out;
    double          last_cache_read;  /* prompt-cache hit portion of last_in */
    double          last_cache_write; /* prompt-cache write portion (M31a)   */
    char          **wisdom;  /* #11: pre-formatted idle proverb lines (arena) */
    int             n_wisdom;
    int             wisdom_idx;   /* rotation cursor                          */
    int             show_wisdom;  /* config wisdom + /wisdom toggle           */
    /* M574: `/wisdom on` typed BY THE USER, as distinct from the config default.
     * Idle proverbs are on by default (jc_config.c: out->wisdom = 1), so a
     * listener hears one before every prompt without ever having asked -- and
     * must sit through it to reach the prompt, where a sighted reader glances
     * past it. Accessible mode therefore suppresses them, and this flag is what
     * keeps that from overriding somebody who explicitly wants them. */
    int             wisdom_explicit;
    /* M254 type-ahead: what the human types WHILE the agent works. */
    int             type_ahead;   /* feature on for this session (tty + config) */
    int             indicator;    /* the working line may be drawn (echo host)  */
    int             in_tool;      /* a tool is executing (ticks may arm the line)*/
    int             holding;      /* the turn-scoped terminal hold is active   */
    struct jc_sb    qbuf;         /* the line being typed right now            */
    struct jc_sb    qpend;        /* committed lines awaiting the next boundary */
    int             qesc;         /* escape-sequence swallow state (0/1/2)     */
    int             at_bol;       /* cursor known to sit at column 0           */
};

static void put(const char *s)
{
    fputs(s, stdout);
}

/* Whether the locale advertises UTF-8 (so we may emit non-ASCII glyphs). */

/* Depth (0 at top level) of the currently-rendering message. */
static int cb_depth(const struct tui_ctx *c)
{
    return (c->app != NULL) ? c->app->agent_depth : 0;
}

/* Write `n` two-space indents for the given depth (capped). */
static void put_indent(int depth)
{
    int k;
    if (depth > 6) depth = 6;
    for (k = 0; k < depth * 2; k++) fputc(' ', stdout);
}

/* Stream nested (subagent) prose raw + dim, indented at each line start. */
static void nested_write(struct tui_ctx *c, const char *s, jc_size n)
{
    jc_size i;
    for (i = 0; i < n; i++) {
        if (c->nested_bol) {
            put_indent(cb_depth(c));
            if (c->color) put(C_DIM);
            c->nested_bol = 0;
        }
        fputc(s[i], stdout);
        if (s[i] == '\n') {
            if (c->color) put(C_RESET);
            c->nested_bol = 1;
        }
    }
}

/* Forget any on-screen parallel board (called when other output intervenes, so
 * a later board redraw doesn't move the cursor into unrelated lines). */
static void board_reset(struct tui_ctx *c)
{
    if (c != NULL) {
        c->board_n = 0;
        c->board_shown = 0;
        c->board_live = 0;
    }
}

/* If `line` is a board row ("[<n>] …"), return its 1-based index, else 0. */
static int board_index(const char *line)
{
    int n = 0;
    const char *p = line;
    if (p == NULL || *p != '[' || p[1] < '1' || p[1] > '9') {
        return 0;
    }
    p++;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        p++;
    }
    return (*p == ']') ? n : 0;
}

/* A dim, depth-indented status line. Parallel-board rows ("[i] …", top level,
 * color) are redrawn as a fixed block in place (cursor up + reprint) so the
 * swarm reads as a live table rather than a scrolling log; everything else
 * (e.g. a subagent banner) prints as a one-off line. */
static void cb_status(void *user, const char *line)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    int idx = board_index(line);

    if (idx >= 1 && idx <= 32 && cb_depth(c) == 0 && c->color) {
        int i;
        jc_snprintf(c->board[idx - 1], sizeof(c->board[0]), "%.78s", line);
        if (idx > c->board_n) {
            c->board_n = idx;
        }
        if (c->board_live && c->board_shown > 0) {
            printf("\x1b[%dA", c->board_shown);   /* up to the board's top */
        }
        for (i = 0; i < c->board_n; i++) {
            printf("\r\x1b[K" C_DIM "  %s" C_RESET "\n",
                   c->board[i][0] != '\0' ? c->board[i] : "");
        }
        c->board_shown = c->board_n;
        c->board_live = 1;
        c->at_bol = 1;
        fflush(stdout);
        return;
    }

    board_reset(c);   /* a non-board status line ends any board block */
    put_indent(cb_depth(c));
    if (c->color) put(C_DIM);
    fputs(line, stdout);
    if (c->color) put(C_RESET);
    fputc('\n', stdout);
    c->at_bol = 1;
    fflush(stdout);
}

/* Erase the transient "working…" line (CR + clear-to-EOL) once real output is
 * about to appear. No-op unless one is showing. */
static void clear_thinking(struct tui_ctx *c)
{
    if (c != NULL && c->thinking) {
        put("\r\x1b[K");
        c->thinking = 0;
        c->at_bol = 1;   /* the erase leaves the cursor at column 0 (M254) */
        fflush(stdout);
    }
}

/* Total committed type-ahead text held at once, and the tail of the line being
 * typed that fits beside the working indicator. Bounds, not policy: a paste or
 * a leaned-on key must not grow either without limit. */
#define TUI_QUEUE_MAX      4096
#define TUI_QUEUE_ECHO_MAX 56

/* Offset into `s` at which a tail-anchored echo of at most `max` bytes starts,
 * snapped forward to a UTF-8 lead byte so a multi-byte character is never split
 * across the truncation. Pure. */
static jc_size queue_echo_off(const char *s, jc_size len, jc_size max)
{
    jc_size off;
    if (s == NULL || len <= max) {
        return 0;
    }
    off = len - max;
    while (off < len && ((unsigned char)s[off] & 0xc0) == 0x80) {
        off++;
    }
    return off;
}

/* Render the current spinner frame + elapsed seconds in place (CR + clear). */
static void render_thinking(struct tui_ctx *c)
{
    static const char *FR_U[8] = {
        "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
        "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\x87"
    };
    static const char *FR_A[4] = { "|", "/", "-", "\\" };
    const char *fr = c->unicode ? FR_U[c->spin & 7] : FR_A[c->spin & 3];
    const char *ell = c->unicode ? "\xe2\x80\xa6" : "...";
    double el = (jc_now_millis() - c->think_start_ms) / 1000.0;
    char eb[24];
    put("\r\x1b[K");
    if (c->color) put(C_DIM);
    /* M346: "2m 07s" past the minute -- a human parses that instantly and
     * "127.4s" never; under a minute the old "%.1fs" form is kept exactly
     * (the ticking decimal is the liveness signal). */
    jc_fmt_elapsed(el, eb, sizeof eb);
    printf("  %s %s%s %s", fr, jc_msg(JC_MSG_WORKING), ell, eb);
    /* M254: the line being typed WHILE the agent works shares this line. It is
     * the safe place for a live echo -- the working indicator already owns the
     * last row and redraws it with CR + erase, so nothing in the scrolling
     * transcript can be overwritten. Tail-anchored, so long input keeps its
     * cursor end visible instead of wrapping and breaking the redraw. */
    if (c->qbuf.len > 0 && c->qbuf.data != NULL) {
        jc_size off = queue_echo_off(c->qbuf.data, c->qbuf.len,
                                     TUI_QUEUE_ECHO_MAX);
        printf("  %s%s%s", c->unicode ? "\xc2\xbb " : "> ",
               off > 0 ? (c->unicode ? "\xe2\x80\xa6" : "...") : "",
               c->qbuf.data + off);
    }
    if (c->color) put(C_RESET);
    c->at_bol = 0;   /* the indicator leaves the cursor mid-line */
    fflush(stdout);
}

/* Print a queue notice on its own line, without ever landing in the middle of
 * the assistant's prose: the transient working line is erased and redrawn
 * around it, and a half-printed streamed line is closed first. */
static void queue_notice(struct tui_ctx *c, const char *label, const char *text)
{
    int was_thinking = c->thinking;
    clear_thinking(c);
    if (!c->at_bol) {
        fputc('\n', stdout);
        c->at_bol = 1;
    }
    if (c->color) put(C_DIM);
    /* M569: the LEADING GLYPH gets an accessible arm, which it never had --
     * the one chrome site in this file that reached for a glyph without asking
     * who was listening. Every neighbour (cb_tool_start, cb_tool_result, the
     * token line) tests `accessible` first. Whether Orca voices U+25B8 is
     * unmeasured and deliberately not guessed at: espeak-ng renders both this
     * and the ">" fallback as silence, but Orca does punctuation
     * verbalisation BEFORE the synthesizer sees the text (M559), so espeak
     * cannot answer it -- and ">" under a SOME punctuation style is very
     * likely spoken. The inconsistency is reason enough; two spoken words per
     * notice is the worst case and zero is the best. */
    if (c->accessible) {
        printf("  %s", label);
    } else {
        printf("  %s %s", c->unicode ? "\xe2\x96\xb8" : ">", label);
    }
    if (text != NULL && text[0] != '\0') {
        printf(": %s", text);   /* subject-less notices skip the separator */
    }
    if (c->color) put(C_RESET);
    fputc('\n', stdout);
    fflush(stdout);
    if (was_thinking) {
        c->thinking = 1;
        render_thinking(c);
    }
}

/* Enter: the typed line joins the pending queue and is confirmed on screen.
 * Only committed text is ever sent -- see queue_hold_end for the uncommitted
 * remainder. */
static void queue_commit(struct tui_ctx *c)
{
    char *typed;
    if (c->qbuf.len == 0 || c->qbuf.data == NULL) {
        return;   /* a bare Enter queues nothing */
    }
    /* Empty the typed line BEFORE announcing it: queue_notice redraws the
     * working indicator, and that redraw must no longer show the text as still
     * being typed -- otherwise the echo lingers beside the spinner until the
     * next progress tick overwrites it. */
    typed = jc_strdup(c->qbuf.data);
    jc_sb_free(&c->qbuf);
    jc_sb_init(&c->qbuf);
    if (typed == NULL) {
        return;
    }
    if (c->qpend.len + (jc_size)strlen(typed) + 1 > (jc_size)TUI_QUEUE_MAX) {
        queue_notice(c, jc_msg(JC_MSG_QUEUE_FULL), typed);
    } else {
        if (c->qpend.len > 0) {
            jc_sb_append_char(&c->qpend, '\n');
        }
        jc_sb_append(&c->qpend, typed);
        queue_notice(c, jc_msg(JC_MSG_QUEUED), typed);
    }
    free(typed);
}

/* One keystroke collected during a hold. This is a queue, not the line editor:
 * it accepts text, Enter, Backspace and Ctrl-U, and deliberately swallows
 * escape sequences (arrows, function keys, paste markers) instead of letting
 * their bytes become text. Ctrl-C never arrives here -- the hold keeps ISIG, so
 * it still raises SIGINT and aborts the turn. */
static void queue_key(struct tui_ctx *c, unsigned char ch)
{
    if (c->qesc == 1) {
        c->qesc = (ch == '[' || ch == 'O') ? 2 : 0;
        return;
    }
    if (c->qesc == 2) {
        if (ch >= 0x40 && ch <= 0x7e) {
            c->qesc = 0;   /* final byte of a CSI/SS3 sequence */
        }
        return;
    }
    if (ch == 0x1b) {
        c->qesc = 1;
        return;
    }
    if (ch == '\r' || ch == '\n') {
        queue_commit(c);
        return;
    }
    if (ch == 0x7f || ch == 0x08) {
        /* Backspace one CHARACTER: drop trailing UTF-8 continuation bytes with
         * the lead byte, so deleting never leaves half a character behind. */
        while (c->qbuf.len > 0 &&
               ((unsigned char)c->qbuf.data[c->qbuf.len - 1] & 0xc0) == 0x80) {
            c->qbuf.len--;
        }
        if (c->qbuf.len > 0) {
            c->qbuf.len--;
        }
        if (c->qbuf.data != NULL) {
            c->qbuf.data[c->qbuf.len] = '\0';
        }
        return;
    }
    if (ch == 0x15) {                 /* Ctrl-U: clear the line being typed */
        jc_sb_free(&c->qbuf);
        jc_sb_init(&c->qbuf);
        return;
    }
    if (ch == 0x0b) {
        /* Ctrl-K: drop what is QUEUED but not yet sent (M258). The two scopes
         * are deliberately separate keys: Ctrl-U clears the line you are typing,
         * Ctrl-K un-queues the lines you already committed. Without this a typo
         * committed while the echo was not visible could not be recalled -- the
         * limitation D10 named. Silent when there is nothing queued. */
        if (c->qpend.len > 0) {
            jc_sb_free(&c->qpend);
            jc_sb_init(&c->qpend);
            queue_notice(c, jc_msg(JC_MSG_QUEUE_DROPPED), "");
        }
        return;
    }
    if (ch < 0x20) {
        return;                       /* other control keys: ignored here */
    }
    if (c->qbuf.len < (jc_size)TUI_QUEUE_MAX) {
        jc_sb_append_char(&c->qbuf, (char)ch);
    }
}

/* Drain whatever the tty has buffered for us. Called from every callback the
 * front-end gets during a turn (the progress tick carries most of it), so
 * keystrokes are collected within a tick of being typed. Bounded so a paste
 * flood cannot hold the loop. No-op unless a hold is active. */
static void queue_poll(struct tui_ctx *c)
{
    int ch;
    int got = 0;
    if (c == NULL || !c->holding) {
        return;
    }
    while (got < TUI_QUEUE_MAX && (ch = jc_term_poll_key(c->term)) >= 0) {
        queue_key(c, (unsigned char)ch);
        got++;
    }
    if (got > 0 && c->thinking) {
        render_thinking(c);   /* show the new keystrokes immediately */
    }
}

/* Hold the terminal for the duration of a turn so typed keys reach queue_key
 * instead of being echoed into the output and flushed away. */
static void queue_hold_begin(struct tui_ctx *c)
{
    if (c == NULL || !c->type_ahead || c->holding) {
        return;
    }
    if (jc_term_hold_begin(c->term) == 0) {
        c->holding = 1;
    }
}

static void queue_hold_end(struct tui_ctx *c)
{
    if (c == NULL || !c->holding) {
        return;
    }
    queue_poll(c);            /* keys typed in the last instant still count */
    jc_term_hold_end(c->term);
    c->holding = 0;
    /* A line typed but never committed with Enter is NOT silently swallowed --
     * that silent loss is the whole defect this feature exists to fix. Show it
     * so the user can see (and retype) what did not get queued. */
    if (c->qbuf.len > 0) {
        queue_notice(c, jc_msg(JC_MSG_QUEUE_UNSENT), c->qbuf.data);
        jc_sb_free(&c->qbuf);
        jc_sb_init(&c->qbuf);
    }
}

/* The take_input callback: hand the loop everything committed so far, which it
 * appends as one "[operator]" user message at this tool-call boundary. */
static char *tui_take_input(void *user)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    char *out;
    if (c == NULL || !c->type_ahead) {
        return NULL;   /* the runtime gate: /typeahead off yields nothing */
    }
    queue_poll(c);
    if (c->qpend.len == 0 || c->qpend.data == NULL) {
        return NULL;
    }
    out = jc_strdup(c->qpend.data);
    jc_sb_free(&c->qpend);
    jc_sb_init(&c->qpend);
    return out;
}

/* Liveness tick during a model call (M66): advance the spinner, throttled to
 * ~11 fps so the curl progress firing rate doesn't flood the terminal. */
static void cb_progress(void *user)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    double now;
    if (c == NULL) {
        return;
    }
    /* M254: collect type-ahead FIRST, before any of the spinner's early
     * returns. Most of a turn's wall-clock is inside the model call this tick
     * comes from, so this is the main pickup point -- and it must keep working
     * when there is no spinner at all (no colour, or --accessible). */
    queue_poll(c);
    if (!c->thinking) {
        /* M258: a tick with no indicator on screen, while a TOOL is running,
         * means the command runner is idle-waiting on output -- the window where
         * a user is most likely to type and where there was previously nothing
         * to echo into. Arm one there, and ONLY there: `in_tool` deliberately
         * excludes the streaming-prose case, where cb_text clears the line on
         * the very next delta and an armed indicator would flicker between
         * paragraphs. `at_bol` keeps it off a half-printed line. */
        if (c->in_tool && c->type_ahead && c->indicator && !c->accessible &&
            c->at_bol) {
            c->thinking = 1;
            c->spin = 0;
            c->think_start_ms = jc_now_millis();
            c->think_render_ms = 0.0;
            render_thinking(c);
        }
        return;
    }
    if (c->accessible) {
        return; /* M118: no animated spinner (screen-reader friendly) */
    }
    now = jc_now_millis();
    if (now - c->think_render_ms < 90.0) {
        return;
    }
    c->think_render_ms = now;
    c->spin++;
    render_thinking(c);
}

/* M549: in ACCESSIBLE mode, write only WHOLE LINES to the terminal.
 *
 * THE DEFECT. Both write paths below did `fwrite(delta, ...)` + `fflush` per
 * delta, and cb_text had no accessible branch at all -- so accessible mode fixed
 * the labels (M184) and the working line (M118/M362) and left output granularity
 * exactly as it was. A screen reader announces terminal text as it appears, so a
 * one-character delta becomes a one-character utterance. Measured on a real reply:
 * 266 deltas for 926 characters, mean 3.5, and 165 of them three bytes or fewer.
 * The operator's words, listening through Orca: "it read every single character",
 * "very annoying to follow along" -- and identical with markdown on and off,
 * because the granularity is upstream of the renderer.
 *
 * M303 HAD ALREADY MADE THIS ARGUMENT, for jichi's own speech: voice_buf exists
 * because "synthesising each delta would stutter, and TTS needs a whole sentence
 * to prosody it". The same sentence is true of a reader the USER runs; nobody
 * carried it across. See docs/ANECDOTES.md and ROADMAP M549.
 *
 * WHY LINES AND NOT SENTENCES. A line is a boundary both parties already agree
 * on: the renderer emits them, the terminal wraps on them, and every reader
 * treats one as an utterance. Sentence detection would need to know about "e.g.",
 * decimal points and code, and would be wrong in prose written by a model.
 *
 * ORDERING. The token line and the tool callbacks fire at or after message end
 * (verified in a PTY capture), so a remainder held here cannot be overtaken:
 * acc_flush runs first in cb_message_end. ACC_FLUSH_CAP bounds a pathological
 * stream with no newline at all -- it is deliberately large, because cutting
 * mid-sentence is the thing being fixed.
 */
#define ACC_FLUSH_CAP 4096

/* Write `n` bytes, line-buffered when accessible. Returns the last byte actually
 * SENT to the terminal, or -1 when everything was held back -- the caller needs
 * that to track at_bol from what is on screen rather than from the delta (M254). */
static int acc_out(struct tui_ctx *c, const char *buf, jc_size n)
{
    jc_size i;
    jc_size cut = 0;
    int last;

    if (c == NULL || buf == NULL || n == 0) {
        return -1;
    }
    if (!c->accessible || cb_depth(c) > 0) {
        fwrite(buf, 1, n, stdout);
        return (int)(unsigned char)buf[n - 1];
    }
    if (jc_sb_append_n(&c->acc_buf, buf, n) != JC_OK ||
        c->acc_buf.data == NULL || c->acc_buf.len == 0) {
        /* Out of memory is not a reason to lose the user's answer: fall back to
         * the unbuffered path rather than dropping bytes. */
        fwrite(buf, 1, n, stdout);
        return (int)(unsigned char)buf[n - 1];
    }
    for (i = c->acc_buf.len; i > 0; i--) {
        if (c->acc_buf.data[i - 1] == '\n') {
            cut = i;
            break;
        }
    }
    if (cut == 0) {
        if (c->acc_buf.len < (jc_size)ACC_FLUSH_CAP) {
            return -1;                  /* still mid-line: hold it */
        }
        cut = c->acc_buf.len;           /* no newline in 4 KB: let it out */
    }
    fwrite(c->acc_buf.data, 1, cut, stdout);
    last = (int)(unsigned char)c->acc_buf.data[cut - 1];
    memmove(c->acc_buf.data, c->acc_buf.data + cut, c->acc_buf.len - cut);
    c->acc_buf.len -= cut;
    c->acc_buf.data[c->acc_buf.len] = '\0';
    return last;
}

/* Release a held partial line. Called before anything else writes. */
static void acc_flush(struct tui_ctx *c)
{
    if (c == NULL || !c->accessible || c->acc_buf.len == 0) {
        return;
    }
    fwrite(c->acc_buf.data, 1, c->acc_buf.len, stdout);
    c->at_bol = (c->acc_buf.data[c->acc_buf.len - 1] == '\n');
    jc_sb_clear(&c->acc_buf);
    fflush(stdout);
}

static void cb_text(void *user, const char *delta, jc_size n)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    char *ctrl_free = NULL;
    /* M472: strip C0 controls and DEL from the MODEL's bytes here, once, before
     * any of the three write paths below -- and it has to be here rather than at
     * the writes, because the markdown renderer INSERTS jichi's own SGR colour,
     * so a strip after it would erase jichi's escapes along with the model's.
     * Stripping the delta on the way in leaves jichi's own output untouched.
     *
     * A terminal executes some of these bytes: OSC 52 writes the user's system
     * clipboard, ESC[2K erases the line jichi just printed. Measured before this:
     * an assistant message's OSC 0 and OSC 52 reached stdout byte-for-byte. The
     * voice buffer benefits too -- there is no point speaking a control byte.
     *
     * One byte per control character, so unlike UTF-8 it cannot straddle two SSE
     * chunks. Nothing is allocated in the common case (jc_ctrl_sanitize returns
     * 0), and the buffer is freed at every return below. */
    if (delta != NULL && n > 0) {
        jc_size clean_n = 0;
        if (jc_ctrl_sanitize(delta, n, &ctrl_free, &clean_n)) {
            delta = ctrl_free;
            n = clean_n;
        }
    }
    /* M303: keep a copy for speech (top level only -- a subagent's stream is
     * progress, not the answer). Cheap: appended, never re-scanned. */
    if (c != NULL && c->app != NULL && c->app->config.voice &&
        cb_depth(c) == 0 && delta != NULL && n > 0) {
        jc_sb_append_n(&c->voice_buf, delta, n);
    }
    queue_poll(c);
    clear_thinking(c);
    board_reset(c);
    if (c != NULL && cb_depth(c) > 0) {
        /* Nested subagent prose: raw, dim, indented (markdown is top-level). */
        nested_write(c, delta, n);
        c->at_bol = c->nested_bol;
        fflush(stdout);
        free(ctrl_free);
        return;
    }
    if (c != NULL && c->markdown) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_mdr_feed(&c->md, delta, n, &sb);
        if (sb.data != NULL && sb.len > 0) {
            /* Track column-0-ness from what was actually WRITTEN, not from the
             * delta: the markdown renderer holds bytes back, so the delta's
             * last byte is not necessarily on screen (M254) -- and M549's line
             * buffer holds bytes back too, which is why acc_out reports the last
             * byte it actually sent and -1 when it sent nothing. */
            int wrote = acc_out(c, sb.data, sb.len);
            if (wrote >= 0) {
                c->at_bol = (wrote == '\n');
            }
        }
        jc_sb_free(&sb);
    } else if (c != NULL) {
        int wrote = acc_out(c, delta, n);
        if (wrote >= 0) {
            c->at_bol = (wrote == '\n');
        }
    }
    fflush(stdout);
    free(ctrl_free);
}

/* M555: the thousands separator, per audience -- and this is a DEFECT REPORT
 * rather than a preference. The operator ran a German-locale screen reader over
 * jichi's token counts and heard:
 *
 *     "The reader reads 4.946 as digits, and reading the ."
 *
 * i.e. `4.946` is spoken "four Punkt nine four six" instead of as a number: a
 * listener gets four digits and the name of a punctuation mark instead of a
     * number.
     *
     * WHICH LAYER, corrected at M559: not the synthesizer. `espeak-ng -v de -q
     * -x 4.946` says "vier tausend neunhundert sechsundvierzig", correctly. It
     * is **Orca's punctuation verbalisation** (`verbalizePunctuationStyle` at
     * SOME) splitting the numeral at the dot before espeak sees it. The fix
     * stands; the explanation was asserted without measurement.
 *
 * IT IS NOT A GERMAN PROBLEM. `jc_config.c` sets `.` as the fallback separator
 * *when the locale has none*, which is precisely what LC_ALL=C gives -- so a
 * plain English user with no locale configured hears the same thing. The comma
 * form `4,946` is the same shape and is untested by ear; the mechanism does not
 * care which mark it is.
 *
 * So: no grouping in accessible mode. `jc_group_num` already treats sep == 0 as
 * "do not group", so this is the whole fix. The SIGHTED rendering keeps its
 * separator, because a bare six-digit integer is harder to scan -- the two
 * audiences want opposite things here, which is the same split M553 drew
 * between chrome and content.
 *
 * EVERY SITE GOES THROUGH THIS FUNCTION. There are five that format a number for
 * a human -- the per-call token line, the session total, /context, and two in
 * /cost, and /promptcache -- and fixing one while leaving its neighbours is the
 * mistake this project has now recorded five times (M536 x4, M544, M549, M462,
 * M551).
 *
 * I ENUMERATED FIVE AND THERE WERE EIGHT. Six in this file -- the sixth is
 * /promptcache's hit-rate line, found by grepping for what was LEFT after the
 * first five were changed -- and two more in `src/main.c`, the headless token
 * line and the envelope summary, which honour `--accessible` just as this file
 * does. Auditing the universe a second way, by a different route, is what found
 * the other three; the list I wrote from reading was short by 37%.
 *
 * The rule itself lives in `jc_group_sep_audience` (jc_str.h) so both
 * front-ends share one copy of it. `tests/smoke/group_sep_lint.sh` fails the
 * build if any call site reaches for config.group_sep without it. */
static char chrome_group_sep(const struct tui_ctx *c)
{
    if (c == NULL || c->app == NULL) {
        return 0;
    }
    return jc_group_sep_audience(c->app->config.group_sep, c->accessible);
}

/* Print the per-message token line: label/brackets dim, the input count cyan and
 * the output count green so the in/out split reads at a glance (plain when colour
 * is off). The caller emits any indent first; this writes the trailing newline. */
static void print_token_line(struct tui_ctx *c, double in_tok, double out_tok)
{
    /* A trailing "(cached N)" notes the prompt-cache hit portion of the input
     * (M31a) -- only when the provider reported one, so non-caching backends
     * read exactly as before. */
    double cached = c->last_cache_read;
    char sep = chrome_group_sep(c);
    char sin[40], sout[40], scache[40];
    jc_group_num(in_tok, sep, sin, sizeof sin);
    jc_group_num(out_tok, sep, sout, sizeof sout);
    jc_group_num(cached, sep, scache, sizeof scache);
    /* M563: ACCESSIBLE IS TESTED FIRST, and the order is the whole bug.
     *
     * This read `if (c->color) ... else if (c->accessible)`, so the prose form
     * was DEAD CODE for any accessible user whose terminal had colour -- and
     * accessible mode does not imply NO_COLOR. The operator ran the shipped
     * build and got the accessible header beside a bracketed token line:
     *
     *     Model chat (jlu/qwen3-coder-next) responds with the following:
     *     [tokens in=3960 out=10]        <- should have been prose
     *
     * WHY THE TESTS MISSED IT: tests/smoke/_smoke.sh exports NO_COLOR=1 for the
     * whole tier, so the accessible arm has never had colour on. Every other
     * accessible branch in this file happens to test `accessible` first, so this
     * was the only site where the ordering mattered -- and the one combination
     * that would have caught it was the one nobody ran. Same shape as M551
     * (accessible.sh had eight checks and every arm passed --auto) and M558
     * (redraw.py ran one mode). accessible.sh now has a third arm: accessible
     * WITH colour. */
    if (c->accessible) {
        /* M553: a SENTENCE, because this is chrome and chrome is read aloud.
         * `[tokens in=4,946 out=37]` is spoken roughly as "bracket tokens in
         * equals four thousand nine hundred forty six out equals thirty seven
         * bracket" -- five of its nine spoken tokens are punctuation, and it
         * repeats after EVERY model call. The operator's instruction was
         * exactly this: "I think we need to use phrases, and sentences: 100
         * input tokens used, and 30 output tokens used."
         *
         * Note what this is NOT: the design's first draft proposed SUPPRESSING
         * the line and exposing it through a command. The operator asked for a
         * sentence instead, which keeps the information and fixes the reason it
         * was painful. Suppression would have been me solving a volume problem
         * the reader did not report. */
        /* M554: SPLIT, on a measurement. The single-sentence cached form had a
         * 62-column fixed part and three numbers to substitute -- roughly 89
         * columns, so it wrapped an 80-column terminal every time a provider
         * reported a cache hit. Two sentences also read better aloud than one
         * with three clauses. */
        /* M557: the sentence comes from the catalog now, so a translator can
         * reach it. The trailing newline stays at the CALL SITE -- a translator
         * should not have to preserve a control character to avoid breaking
         * the layout. */
        printf(jc_msg(JC_MSG_TOKENS), sin, sout);
        printf("\n");
        if (cached > 0.0) {
            printf(jc_msg(JC_MSG_TOKENS_CACHED), scache);
            printf("\n");
        }
    } else if (c->color) {
        printf(C_DIM "[tokens in=" C_RESET C_CYAN "%s" C_RESET
               C_DIM " out=" C_RESET C_GREEN "%s" C_RESET, sin, sout);
        if (cached > 0.0) {
            printf(C_DIM " cached=" C_RESET C_CYAN "%s" C_RESET, scache);
        }
        printf(C_DIM "]" C_RESET "\n");
    } else if (cached > 0.0) {
        printf("[tokens in=%s out=%s cached=%s]\n", sin, sout, scache);
    } else {
        printf("[tokens in=%s out=%s]\n", sin, sout);
    }
}

/* Before each assistant message: a "<model> · <mode>" header (so the model that
 * actually replies — after any routing/fallback — and the mode are visible, with
 * the model in bold cyan and the mode in its mode colour), and a fresh
 * markdown-renderer state. */
static void cb_message_begin(void *user)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    /* M296: name the MODEL, not only the tier. `fast` is a config intent label --
     * it says which tier is active and nothing about which model is answering, and
     * since M288 made escalation actually fire, `strong` is equally opaque. This
     * header is emitted per model call (before each stream_once), so unlike the
     * prompt it tracks an escalation as it happens and is the honest place to
     * answer "which model wrote this". */
    char modelbuf[192];
    const char *model = modelbuf;
    const char *mode = jc_agent_mode_name((enum jc_agent_mode)c->app->mode);
    const char *sep = c->unicode ? "\xc2\xb7" : "-";
    int depth = cb_depth(c);
    jc_model_display(c->app->config.model.name, c->app->config.model.model,
                     modelbuf, sizeof modelbuf);
    if (depth == 0 && c->markdown) {
        jc_mdr_reset(&c->md);
    }
    c->last_in = 0.0;
    c->last_out = 0.0;
    c->last_cache_read = 0.0;
    c->last_cache_write = 0.0;
    board_reset(c);
    if (depth > 0) {           /* nested subagent: no header; raw dim stream */
        c->nested_bol = 1;
        return;
    }
    if (c->quiet) {            /* minimal: no per-message model/mode header */
        return;
    }
    {
        /* Wall-clock timestamp appended to the header (locale-aware via
         * LC_TIME + config timeFormat, default %X). */
        char ts[64];
        jc_now_timestr(c->app->config.time_format, ts, sizeof ts);
        if (c->accessible) {
            /* M184 gave this a linear form; M553 makes it a SENTENCE, in the
             * operator's own words: "Model qwen3-coder-next responds with the
             * following:". What M184 produced was
             * `assistant (m (mock) - chat - 11:01:20):` -- a label, two
             * parenthesised fields, two separators (a MIDDOT under a UTF-8
             * locale, spoken as "middle dot") and a wall-clock time read in
             * full on every single message.
             *
             * DROPPED, and why each is safe to drop:
             *   - the TIMESTAMP. The strongest "useful to somebody" candidate
             *     here, and it is read on every message. The session log has
             *     one; a listener does not need the clock narrated. A config
             *     knob to restore it was approved in principle and is
             *     DELIBERATELY NOT ADDED YET -- a config key is a stability
             *     surface (EMBEDDING.md) and this wording is still being tuned
             *     by ear. It becomes a knob when a listening test says someone
             *     wants it, not before.
             *   - the MODE. It stays in the prompt, which is where a listener
             *     meets it before acting rather than after.
             * The model is NAMED EVERY TIME rather than only on change: the
             * operator wrote it that way, and whether the repetition is worth
             * the identity is exactly the sort of thing the next listening test
             * answers. (void)-ing `ts` and `mode` keeps the C89 build clean
             * without pretending they were never computed. */
            (void)sep; (void)mode; (void)ts;
            printf(jc_msg(JC_MSG_MODEL_RESPONDS), model);
            printf("\n");
        } else if (c->color) {
            /* Model in bold cyan, the separator dim, the mode in its own colour
             * (chat green / plan blue / auto yellow -- the latter flags the
             * unattended posture), then the dim time. */
            printf(C_BOLD C_CYAN "%s" C_RESET C_DIM " %s " C_RESET "%s%s" C_RESET
                   C_DIM " %s %s" C_RESET "\n",
                   model, sep, jc_mode_color(c->app->mode), mode, sep, ts);
        } else {
            /* M560 (stage A3): NO COLOUR, SO THE ROLE HAS TO BE A WORD.
             *
             * The colour branch above carries "this is the assistant" in bold
             * cyan and the mode's own colour. Strip colour and that information
             * is simply gone: the line read `m (mock) - chat - 13:05:42`, four
             * fields and two separators with nothing saying what it is. In a
             * piped transcript, a pasted bug report, or a transcript a model
             * reads back, that is indistinguishable from a log line.
             *
             * M553 already made the argument -- "prose survives copy-paste;
             * ANSI styling does not" -- and this is the case it was about.
             *
             * WHY NOT THE FULL PROSE FORM used by accessible mode. Because the
             * audiences differ, which is M553's own rule: prose where a human
             * must UNDERSTAND, compression where a human must SCAN. A NO_COLOR
             * user is reading with their eyes and wants the timestamp and the
             * compact fields; what they were missing was one word. Adopting the
             * accessible sentence here would be an aesthetic choice I have no
             * evidence for -- there is no NO_COLOR sighted user to ask -- where
             * restoring the absent information is a defect fix.
             *
             * THE TOOL LINES DO NOT NEED THIS, checked rather than assumed:
             * cb_tool_start prints a `>` glyph and cb_tool_result a `ok`/`x`
             * glyph, and a glyph survives NO_COLOR. The header was the only
             * role line whose marker was carried by colour alone. */
            printf("assistant: %s %s %s %s %s\n", model, sep, mode, sep, ts);
        }
    }
    c->at_bol = 1;
    /* Transient animated "working…" line covering the model round-trip; ticked
     * by cb_progress and cleared the moment the first text/tool arrives
     * (clear_thinking). Color only, since it relies on CR + erase-line. */
    if (c->accessible) {
        /* M118: a single static line (no CR-overwrite animation), so a screen
         * reader announces "working" once instead of a stream of spinner frames. */
        printf("  %s...\n", jc_msg(JC_MSG_WORKING));
    } else if (c->indicator) {
        c->thinking = 1;
        c->spin = 0;
        c->think_start_ms = jc_now_millis();
        c->think_render_ms = 0.0;
        render_thinking(c);
    }
    fflush(stdout);
}

static void cb_message_end(void *user)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    /* M549: release any held partial line FIRST, so the token line and the tool
     * callbacks that follow cannot overtake the answer they belong to. */
    acc_flush(c);
    clear_thinking(c);   /* in case the turn ended with no text/tool output */
    /* M303: speak the finished reply, then reset the buffer. After the text is on
     * screen, so a sighted user is never waiting on the speaker, and only at the
     * top level. */
    if (c != NULL && cb_depth(c) == 0 && c->app != NULL &&
        c->app->config.voice && c->voice_buf.len > 0) {
        fflush(stdout);
        jc_voice_say(c->app, c->voice_buf.data);
        jc_sb_clear(&c->voice_buf);
    }
    if (c != NULL && cb_depth(c) > 0) {
        /* Close the nested raw stream; show an indented token line unless quiet. */
        if (!c->nested_bol) {
            if (c->color) put(C_RESET);
            fputc('\n', stdout);
            c->nested_bol = 1;
        }
        if (!c->quiet && (c->last_in > 0.0 || c->last_out > 0.0)) {
            put_indent(cb_depth(c));
            print_token_line(c, c->last_in, c->last_out);
        }
        fflush(stdout);
        return;
    }
    if (c != NULL && c->markdown) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_mdr_flush(&c->md, &sb);
        if (sb.data != NULL && sb.len > 0) {
            fwrite(sb.data, 1, sb.len, stdout);
        }
        jc_sb_free(&sb);
    }
    fputc('\n', stdout);
    /* The per-message usage line, after the reply (see cb_usage); hidden when
     * quiet. */
    if (c != NULL && !c->quiet && (c->last_in > 0.0 || c->last_out > 0.0)) {
        print_token_line(c, c->last_in, c->last_out);
    }
    fflush(stdout);
}

/* Glyphs: pretty on UTF-8 terminals, ASCII elsewhere. */
static const char *g_run(const struct tui_ctx *c)  { return c->unicode ? "\xe2\x96\xb8" : ">"; }
static const char *g_ok(const struct tui_ctx *c)   { return c->unicode ? "\xe2\x9c\x93" : "ok"; }
static const char *g_bad(const struct tui_ctx *c)  { return c->unicode ? "\xe2\x9c\x97" : "x"; }

/* Read `path` into a malloc'd NUL-terminated buffer (caller frees). Returns 0
 * with *out=="" allocated when the file is missing/unreadable (a new file). */
static int tui_slurp(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    *out = NULL;
    if (f == NULL) {
        *out = jc_strdup("");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
        fclose(f);
        *out = jc_strdup("");
        return 0;
    }
    rewind(f);
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    n = (long)fread(buf, 1, (size_t)n, f);
    buf[n] = '\0';
    fclose(f);
    *out = buf;
    return 0;
}

/* Render a unified diff of old_t -> new_t to stdout (colored if enabled). */
static void preview_diff(struct tui_ctx *c, const char *old_t,
                         const char *new_t)
{
    struct jc_sb d;
    jc_sb_init(&d);
    if (jc_diff_unified(old_t, new_t, 3, c->color, 200, &d) > 0 &&
        d.data != NULL) {
        fputs(d.data, stdout);
    }
    jc_sb_free(&d);
}

/* One file's evolving content for the apply_patch preview. */
struct prevfile { const char *path; char *orig; struct jc_sb cur; };

static struct prevfile *prev_get(struct jc_vec *v, const char *path)
{
    jc_size i;
    struct prevfile e;
    char *orig = NULL;
    for (i = 0; i < v->len; i++) {
        struct prevfile *p = (struct prevfile *)jc_vec_at(v, i);
        if (strcmp(p->path, path) == 0) {
            return p;
        }
    }
    if (tui_slurp(path, &orig) != 0) {
        return NULL;
    }
    e.path = path;
    e.orig = orig;
    jc_sb_init(&e.cur);
    jc_sb_append(&e.cur, orig);
    jc_vec_push(v, &e);
    return (struct prevfile *)jc_vec_at(v, v->len - 1);
}

/* Preview of an apply_patch call: apply each edit to in-memory buffers (exactly
 * as the tool will), then show the per-file diff. */
static void preview_apply(struct tui_ctx *c, cJSON *args)
{
    cJSON *edits = cJSON_GetObjectItem(args, "edits");
    cJSON *e;
    struct jc_vec files;
    jc_size i;

    if (!cJSON_IsArray(edits)) {
        return;
    }
    jc_vec_init(&files, sizeof(struct prevfile));
    cJSON_ArrayForEach(e, edits) {
        const char *path = jc_json_get_str(e, "path", NULL);
        const char *os = jc_json_get_str(e, "old_string", NULL);
        const char *ns = jc_json_get_str(e, "new_string", "");
        /* M530: the LENIENT reader, because the tool that will perform this
         * edit uses tu_arg_bool -- which is now the same function. They used to
         * differ: a model sending {"replace_all": "true"} got a preview showing
         * ONE occurrence replaced and an edit that replaced ALL of them. The
         * user approved a narrower change than the one that ran, which is an
         * approval-integrity defect rather than a display bug. A preview must
         * read every argument exactly as the executor will. */
        int ra = jc_json_get_bool_lenient(e, "replace_all", 0);
        struct prevfile *pf;
        if (path == NULL || os == NULL) {
            continue;
        }
        pf = prev_get(&files, path);
        if (pf == NULL) {
            continue;
        }
        {
            struct jc_sb nb;
            jc_sb_init(&nb);
            if (jc_patch_apply(pf->cur.data != NULL ? pf->cur.data : "",
                               os, ns, ra, c->app->config.fuzzy_edit, &nb, NULL)
                >= JC_PATCH_EXACT) {
                jc_sb_free(&pf->cur);
                pf->cur = nb;
            } else {
                jc_sb_free(&nb);
            }
        }
    }
    for (i = 0; i < files.len; i++) {
        struct prevfile *pf = (struct prevfile *)jc_vec_at(&files, i);
        if (files.len > 1) {
            if (c->color) put(C_DIM);
            printf("  %s\n", pf->path);
            if (c->color) put(C_RESET);
        }
        preview_diff(c, pf->orig, pf->cur.data != NULL ? pf->cur.data : "");
        free(pf->orig);
        jc_sb_free(&pf->cur);
    }
    jc_vec_free(&files);
}

/* Show what an edit tool is about to change, as a unified diff, before the
 * approval prompt (and before it runs in auto mode). Uses the same jc_patch
 * core as the tools, so the preview matches what gets written. */
static void render_edit_preview(struct tui_ctx *c, const char *name,
                                const char *args_json)
{
    cJSON *args;
    if (strcmp(name, "write_file") != 0 && strcmp(name, "edit_file") != 0 &&
        strcmp(name, "apply_patch") != 0) {
        return;
    }
    args = jc_json_parse(args_json);
    if (args == NULL) {
        return;
    }
    if (strcmp(name, "write_file") == 0) {
        const char *path = jc_json_get_str(args, "path", NULL);
        const char *content = jc_json_get_str(args, "content", "");
        char *old = NULL;
        if (path != NULL && tui_slurp(path, &old) == 0) {
            preview_diff(c, old, content);
            free(old);
        }
    } else if (strcmp(name, "edit_file") == 0) {
        const char *path = jc_json_get_str(args, "path", NULL);
        const char *os = jc_json_get_str(args, "old_string", NULL);
        const char *ns = jc_json_get_str(args, "new_string", "");
        int ra = jc_json_get_bool_lenient(args, "replace_all", 0);  /* M530 */
        char *old = NULL;
        if (path != NULL && os != NULL && tui_slurp(path, &old) == 0) {
            struct jc_sb nb;
            jc_sb_init(&nb);
            if (jc_patch_apply(old, os, ns, ra, c->app->config.fuzzy_edit,
                               &nb, NULL) >= JC_PATCH_EXACT) {
                preview_diff(c, old, nb.data != NULL ? nb.data : "");
            }
            jc_sb_free(&nb);
            free(old);
        }
    } else {
        preview_apply(c, args);
    }
    cJSON_Delete(args);
}

/* M442's tool-call id is not used here: the TUI renders one line per call in the
 * order they run, for a human watching. An id would be noise on screen, and the
 * pairing problem it solves is a machine consumer's. */
static void cb_tool_start(void *user, const char *name, const char *args,
                          const char *id)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    char summary[200];
    int depth = cb_depth(c);
    (void)id;
    queue_poll(c);       /* M254: a tool boundary is also a pickup point */
    clear_thinking(c);   /* first tool: drop the "working…" line */
    board_reset(c);
    jc_tool_arg_summary(name, args, summary, sizeof summary);
    printf("\n");
    put_indent(depth);          /* indent nested (subagent) tool activity */
    if (c->color) put(C_DIM);
    if (c->accessible) {
        /* M553: prose. `tool call: edit_file  edit_me.txt` is a label, two
         * spaces and a bare argument; a listener gets no grammar to hang it on.
         * "with" rather than "on" because the summary is not always a path --
         * jc_tool_arg_summary also yields a command, a query or a question. */
        if (summary[0] != '\0') {
            printf(jc_msg(JC_MSG_TOOL_CALL_ARG), name, summary);
        } else {
            printf(jc_msg(JC_MSG_TOOL_CALL), name);
        }
    } else {
        printf("%s %s", g_run(c), name);
        if (summary[0] != '\0') printf("  %s", summary);
    }
    if (c->color) put(C_RESET);
    printf("\n");
    if (!c->quiet && depth == 0) { /* diff preview: top-level, non-quiet only */
        render_edit_preview(c, name, args);
    }
    c->at_bol = 1;
    c->in_tool = 1;   /* M258: ticks from the command runner may arm the echo */
    fflush(stdout);
}

static void cb_tool_result(void *user, const char *name, const char *result,
                           int is_error, const char *id)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    char *ctrl_free = NULL;
    jc_size len;
    /* M472: this surface PRINTS tool output, so it is the second way untrusted
     * bytes reach the terminal -- not via the model at all: a file in the repo
     * containing OSC 52, shown by read_file, is enough. That makes this reachable
     * by M300's untrusted-content class. Stripped BEFORE the head/tail
     * truncation below, so the byte counts it reports describe what is shown. */
    if (result != NULL) {
        jc_size clean_n = 0;
        if (jc_ctrl_sanitize(result, (jc_size)strlen(result), &ctrl_free,
                             &clean_n)) {
            result = ctrl_free;
        }
    }
    len = (jc_size)strlen(result);
    (void)id;
    queue_poll(c);    /* M254: a long tool call is a long time to type in */
    clear_thinking(c);/* M258: drop an indicator armed during the tool's wait */
    c->in_tool = 0;
    board_reset(c);   /* the parallel board (if any) is complete */
    put_indent(cb_depth(c));    /* align with the nested tool-start line */
    if (c->color) put(is_error ? C_CYAN : C_DIM);
    if (c->accessible) {
        /* M553: prose. The result BODY follows on this same line, so the
         * sentence has to end cleanly before it -- "The tool read_file finished
         * successfully." then the file. Note the body is already bounded at 360
         * bytes (head 240 + tail 100, below); the operator's step-2 volume
         * finding was a file UNDER that bound, so the bound is not the fix and
         * is not touched here. */
        /* M557: TWO WHOLE SENTENCES rather than one with a substituted verb.
         * German puts the verb last and Japanese is SOV, so a fragment order
         * that reads in English cannot be reordered by a translator -- which is
         * the classic localisation mistake this avoids by construction. */
        /* M571: A REFUSAL IS NOT A FAILURE. The operator answered the prompt
         * themselves and heard "The tool edit_file failed. denied" -- their own
         * decision announced back to them as a malfunction, and for a listener
         * the word "failed" arrives before the word that explains it.
         *
         * jc_fail_classify is the existing, tested classifier and it already
         * distinguishes this: JC_FAIL_DENIED covers a human's no and a fence's
         * refusal alike, which is the right generalisation -- neither is the
         * tool going wrong. Everything else keeps JC_MSG_TOOL_FAIL. */
        if (is_error && jc_fail_classify(result, 0) == JC_FAIL_DENIED) {
            printf(jc_msg(JC_MSG_TOOL_REFUSED), name);
        } else {
            printf(jc_msg(is_error ? JC_MSG_TOOL_FAIL : JC_MSG_TOOL_OK), name);
        }
    } else {
        printf("%s %s ", is_error ? g_bad(c) : g_ok(c),
               is_error ? "error" : name);
    }
    /* Preview: small results in full; large ones as head + tail, so a trailing
     * summary or error (often the useful part) isn't hidden behind a head-only
     * truncation. */
    if (len > 360) {
        const char *ell = c->unicode ? "\xe2\x80\xa6" : "...";
        fwrite(result, 1, 240, stdout);
        printf(" %s (%lu bytes) %s ", ell, (unsigned long)len, ell);
        fwrite(result + len - 100, 1, 100, stdout);
    } else {
        fwrite(result, 1, len, stdout);
    }
    if (c->color) put(C_RESET);
    printf("\n");
    c->at_bol = 1;
    fflush(stdout);
    free(ctrl_free);
}

/* Usage fires during the stream (before on_message_end). We only accumulate
 * here and remember this call's counts; the per-message line is printed at
 * message end, after the (line-buffered) reply, so the reply always comes
 * first. */
static void cb_usage(void *user, double in_tok, double out_tok,
                     double cache_read, double cache_write)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    c->tot_in += in_tok;
    c->tot_out += out_tok;
    c->tot_cache_read += cache_read;
    c->tot_cache_write += cache_write;
    c->last_in = in_tok;
    c->last_out = out_tok;
    c->last_cache_read = cache_read;
    c->last_cache_write = cache_write;
}

static int always_has(struct tui_ctx *c, const char *name)
{
    jc_size i;
    for (i = 0; i < c->always.len; i++) {
        if (strcmp(*(char **)jc_vec_at(&c->always, i), name) == 0) return 1;
    }
    return 0;
}

static void always_add(struct tui_ctx *c, const char *name)
{
    char *dup;
    if (always_has(c, name)) return;
    dup = jc_strdup(name);
    if (dup != NULL && jc_vec_push(&c->always, &dup) != JC_OK) free(dup);
}

static void confirm_echo(struct tui_ctx *c, const char *what)
{
    if (c->color) put(C_DIM);
    if (c->accessible) {
        /* M553: the arrow is alignment, and alignment is a visual service --
         * `->` is spoken as "dash greater-than" or skipped, and either way it
         * is not the information. The information is the word. */
        printf("  %s.\n", what);
    } else {
        printf("  %s %s\n", c->unicode ? "\xe2\x86\x92" : "->", what);
    }
    if (c->color) put(C_RESET);
    fflush(stdout);
}

/* Ask the user to approve a tool call. Single keypress: y=once, a=always (this
 * session), v=view full args, anything else=deny. */
/* The ask_user front-end delegate (F4): print the question + any suggested
 * answers and block on a line of input. A bare number selecting a suggestion is
 * expanded to that option's text. Returns JC_ERR_ABORTED on EOF/Ctrl-C so the
 * tool reports no answer and the model proceeds. */
static jc_status tui_ask(void *user, const char *question,
                         const char *const *options, int noptions,
                         struct jc_sb *out)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    char *line = NULL;
    jc_read_result rr;
    int i;

    printf("\n");
    if (c->color) put(C_CYAN);
    printf("? %s", question != NULL ? question : "(question)");
    if (c->color) put(C_RESET);
    printf("\n");
    for (i = 0; i < noptions; i++) {
        printf("  %d) %s\n", i + 1, options[i]);
    }
    /* M304: this is the moment the human is ALREADY in the loop, and the moment
     * they are most likely to want the run reined in -- "stop working unattended,
     * show me the plan first". So the answer may be a posture instead of text. */
    if (c->app->mode == JC_MODE_AUTO) {
        if (c->color) put(C_DIM);
        put("  (or /plan or /chat to narrow the posture from here)\n");
        if (c->color) put(C_RESET);
    }
    rr = jc_term_readline(c->term, "  answer > ", &line);
    if (rr != JC_READ_LINE || line == NULL) {
        free(line);
        return JC_ERR_ABORTED;
    }
    /* M304: a SLASH-prefixed posture narrows the run instead of answering. The
     * slash is required so a legitimate answer that happens to be the word "plan"
     * is still an answer -- an ask_user question could easily be "which approach?"
     * with "plan" among the options. Narrowing only, and it persists past the turn
     * (a human who said plan mode meant it); then the question is re-asked, because
     * the model still needs its answer and the narrowed posture may change what the
     * user wants to say. */
    if (line[0] == '/') {
        enum jc_agent_mode want;
        if (jc_agent_mode_parse(line + 1, &want)) {
            enum jc_agent_mode from = (enum jc_agent_mode)c->app->mode;
            if (want == from) {
                printf("  already in %s mode\n", jc_agent_mode_name(want));
            } else if (jc_perm_mode_narrows(from, want)) {
                jc_app_set_mode(c->app, want);
                printf("  narrowed to %s mode%s\n", jc_agent_mode_name(want),
                       want == JC_MODE_PLAN ? " (read-only from here)" : "");
                if (c->app->config.voice) {
                    char m[96];
                    jc_snprintf(m, sizeof(m), "Narrowed to %s mode.",
                                jc_agent_mode_name(want));
                    jc_voice_say(c->app, m);
                }
            } else {
                printf("  refused: %s -> %s would widen what the agent may do. "
                       "Only narrowing is allowed here.\n",
                       jc_agent_mode_name(from), jc_agent_mode_name(want));
            }
            free(line);
            /* Ask again: the model still needs an answer. */
            return tui_ask(user, question, options, noptions, out);
        }
    }
    /* A bare "N" picking a suggested option expands to that option's text. */
    if (noptions > 0 && line[0] >= '1' && line[0] <= '9') {
        char *end = NULL;
        long n = strtol(line, &end, 10);
        if (end != NULL && *end == '\0' && n >= 1 && n <= (long)noptions) {
            jc_sb_append(out, options[n - 1]);
            free(line);
            return JC_OK;
        }
    }
    jc_sb_append(out, line);
    free(line);
    return JC_OK;
}

/* M153: the privileged-command prompt. Distinct from cb_confirm and,
 * critically, it NEVER consults the `always` set -- privilege is asked afresh
 * every time, so a prior "allow always" on the shell tool cannot cover a
 * later sudo. Single keypress y/n only (no `a`: there is deliberately no
 * "always allow privileged"). */
static int cb_confirm_privileged(void *user, const char *launcher,
                                 const char *command)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    int k;

    printf("\n");
    if (c->color) put(C_BOLD C_RED);
    printf("%s PRIVILEGED (%s)", c->unicode ? "\xe2\x9a\xa0" : "!!",
           launcher != NULL ? launcher : "sudo");
    if (c->color) put(C_RESET);
    if (c->color) put(C_DIM);
    printf("  %s", command != NULL ? command : "");
    if (c->color) put(C_RESET);
    /* M551: the bracket form is a VISUAL affordance -- see
     * JC_MSG_ALLOW_PROMPT_ACC. This prompt guards a `sudo`, and it was left
     * spelling itself out for one milestone longer than the tool-approval
     * prompt because the fix was made where the defect was reported instead
     * of where the defect was. There is deliberately no `a` here. */
    printf("\n  %s  %s ",
           c->accessible
               ? jc_msg(JC_MSG_PRIV_PROMPT_ACC)
               : "Run this with elevated privilege? [y]es  [n]o",
           c->unicode ? "\xe2\x80\xba" : ">");
    fflush(stdout);

    k = jc_term_read_key(c->term);
    if (k == 'y' || k == 'Y' || k == '1') {   /* M564: digits too */
        confirm_echo(c, c->accessible
            ? "The privileged command was allowed for this call only"
            : "privileged: allowed (this call only)");
        return 1;
    }
    confirm_echo(c, c->accessible ? "The privileged command was denied"
                                  : "privileged: denied");
    return 0;
}

/* M163a: a fresh, never-`always` confirm for a kinetic (physical-actuation)
 * action -- clone of the privileged prompt with distinct wording. */
static int cb_confirm_kinetic(void *user, const char *subject,
                              const char *detail)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    int k;

    printf("\n");
    if (c->color) put(C_BOLD C_RED);
    printf("%s KINETIC (%s)", c->unicode ? "\xe2\x9a\xa0" : "!!",
           subject != NULL ? subject : "actuator");
    if (c->color) put(C_RESET);
    if (c->color) put(C_DIM);
    printf("  %s", detail != NULL ? detail : "");
    if (c->color) put(C_RESET);
    /* M551: as above. A prompt that moves something in the physical world is
     * the last place a key list should be unreadable. No `a` here either. */
    printf("\n  %s  %s ",
           c->accessible
               ? jc_msg(JC_MSG_KINETIC_PROMPT_ACC)
               : "Allow this physical actuation? [y]es  [n]o",
           c->unicode ? "\xe2\x80\xba" : ">");
    fflush(stdout);

    k = jc_term_read_key(c->term);
    if (k == 'y' || k == 'Y' || k == '1') {   /* M564: digits too */
        confirm_echo(c, c->accessible
            ? "The physical actuation was allowed for this call only"
            : "kinetic: allowed (this call only)");
        return 1;
    }
    confirm_echo(c, c->accessible ? "The physical actuation was denied"
                                  : "kinetic: denied");
    return 0;
}

/* M565: how many unrecognised keypresses a single approval prompt will absorb
 * before it gives up and denies. Small on purpose: it exists to tolerate a
 * fumble, not to argue with a garbage stream. */
#define UNRECOGNISED_MAX 3

/* M571: THE VIEW KEY SHOWS THE CALL, not its JSON encoding.
 *
 * What `v`/`5` used to do, for every tool, was `printf("\n%s\n", args)` -- the
 * raw argument string. The operator met it on apply_patch:
 *
 *   {"edits": [{"path": "...", "old_string": "static void greet(const char
 *   *who)\n{\n    printf(\"hello, %s\\n\", who);\n}", "new_string": "..."}]}
 *
 * Spoken: braces, quotes, "backslash n", and escaped escapes. The key advertised
 * as "view" -- the one that exists so a person can understand a change BEFORE
 * authorising it -- was the least readable thing in the session.
 *
 * ONE FIELD PER LINE, with the value DECODED: cJSON has already turned \n into a
 * real newline and \" into a quote, so printing valuestring gives back the code
 * as it will be written. That matters more than formatting: the values here are
 * CONTENT, and content is the channel this project does not reflow (the
 * operator's rule -- "within program code all symbols are important, and must be
 * read"). So the structure is announced and the content is passed through
 * untouched.
 *
 * BOUNDED PER VALUE, and the bound says so. A write_file of a 200 KB file would
 * otherwise read for an hour; the marker names the true size so nothing is
 * silently hidden -- the same contract as the tool-result preview.
 *
 * NOT the diff. Both edit_file and apply_patch already render a diff preview
 * above the prompt (render_edit_preview / preview_apply), so re-rendering it
 * here would repeat what the listener just heard. What view adds is the whole
 * call, which is what it is for. */
#define VIEW_VALUE_MAX 1200

static void print_field(struct tui_ctx *c, const char *key, cJSON *v,
                        int indent)
{
    int k;
    for (k = 0; k < indent; k++) { fputc(' ', stdout); }
    printf("%s:", (key != NULL) ? key : "value");
    if (cJSON_IsString(v) && v->valuestring != NULL) {
        jc_size len = strlen(v->valuestring);
        const char *nl = strchr(v->valuestring, '\n');
        if (nl == NULL && len <= 60) {
            printf(" %s\n", v->valuestring);   /* short and single-line */
            return;
        }
        printf("\n");
        if (len > (jc_size)VIEW_VALUE_MAX) {
            fwrite(v->valuestring, 1, VIEW_VALUE_MAX, stdout);
            printf("\n  %s(%lu bytes in total)\n",
                   c->accessible ? "" : "... ", (unsigned long)len);
        } else {
            fwrite(v->valuestring, 1, len, stdout);
            if (len > 0 && v->valuestring[len - 1] != '\n') { printf("\n"); }
        }
        return;
    }
    if (v != NULL && cJSON_IsArray(v)) {
        printf(" %d item(s)\n", cJSON_GetArraySize(v));
        return;
    }
    {
        char *txt = cJSON_PrintUnformatted(v);
        printf(" %s\n", (txt != NULL) ? txt : "?");
        free(txt);
    }
}

static void print_args_readable(struct tui_ctx *c, const char *args)
{
    cJSON *o;
    cJSON *m;

    if (args == NULL || args[0] == '\0') { return; }
    o = jc_json_parse(args);
    if (o == NULL) {
        /* Unparseable: show it verbatim rather than nothing. The user asked to
         * see the call, and a malformed call is exactly when they need to. */
        printf("\n%s\n", args);
        return;
    }
    printf("\n");
    for (m = o->child; m != NULL; m = m->next) {
        if (cJSON_IsArray(m)) {
            int i;
            int n = cJSON_GetArraySize(m);
            printf("%s: %d item(s)\n", (m->string != NULL) ? m->string : "?", n);
            for (i = 0; i < n; i++) {
                cJSON *e = cJSON_GetArrayItem(m, i);
                cJSON *f;
                if (e == NULL || !cJSON_IsObject(e)) { continue; }
                printf("  item %d:\n", i + 1);
                for (f = e->child; f != NULL; f = f->next) {
                    print_field(c, f->string, f, 4);
                }
            }
            continue;
        }
        print_field(c, m->string, m, 0);
    }
    cJSON_Delete(o);
}

static int cb_confirm(void *user, const char *name, const char *args,
                      char **edited)
{
    int unrecognised = 0;
    struct tui_ctx *c = (struct tui_ctx *)user;
    char summary[200];
    int spoken = 0;

    if (always_has(c, name)) return 1;
    /* M254: collect type-ahead BEFORE prompting. jc_term_read_key enters raw
     * mode with TCSAFLUSH -- deliberately, so stray type-ahead can never
     * answer a permission prompt -- which would otherwise discard whatever was
     * typed since the last poll. Draining here keeps the text and keeps the
     * prompt un-answerable by it. */
    queue_poll(c);
    jc_tool_arg_summary(name, args, summary, sizeof summary);

    for (;;) {
        int k;
        printf("\n");
        /* M553: this line heads the approval block VISUALLY -- glyph, tool
         * name, dim argument summary. In accessible mode it is the tool's THIRD
         * announcement in the same turn: `Calling the tool edit_file, with
         * edit_me.txt.` came from cb_tool_start, the approval question names it
         * again, and this sat between them. One event, three readings.
         *
         * The M184 role label is the one that is kept, deliberately -- it is
         * what makes the transcript linear -- so this is the copy that goes. */
        if (!c->accessible) {
            if (c->color) put(C_CYAN);
            printf("%s %s", c->unicode ? "\xe2\x96\xb8" : ">", name);
            if (c->color) put(C_RESET);
            if (summary[0] != '\0') {
                if (c->color) put(C_DIM);
                printf("  %s", summary);
                if (c->color) put(C_RESET);
            }
        }
        /* M551: in accessible mode the KEY LIST is spoken, not seen, so the
         * bracket form has to go -- see JC_MSG_ALLOW_PROMPT_ACC. Note what
         * this is the FOURTH instance of, in the very function that holds the
         * third: the right form already existed twenty lines below, in the
         * voice branch, and was never applied here. A rule written down in
         * one place and not applied in the neighbouring one. */
        /* M553: no input ARROW in accessible mode. The transcript showed
         * `  >   denied.` -- the glyph is the visual "type here" cue, and the
         * question above it already tells a listener that jichi is waiting.
         * Spoken, it is "greater than" in front of the answer. */
        if (c->accessible) {
            printf("\n  %s\n",
                   jc_msg(JC_MSG_ALLOW_PROMPT_ACC));
        } else {
            printf("\n  %s\n  %s ", jc_msg(JC_MSG_ALLOW_PROMPT),
                   c->unicode ? "\xe2\x80\xba" : ">");
        }
        fflush(stdout);

        /* M303: SPEAK the question. This is the case voice mode exists for: with
         * no screen being read, an unspoken approval prompt is a session that
         * stops for no audible reason. Spoken before the keypress is awaited, and
         * only on the first pass (`v`/`e` loop back round and re-render). */
        if (c->app->config.voice && !spoken) {
            char q[320];
            /* M552: "as in" here too. This string is spoken by jichi's OWN
             * TTS rather than read by the user's reader, but the ambiguity is
             * in the PHRASE, not the delivery: the same synthesiser saying
             * "a for always" cannot distinguish the letter from the article.
             * NOT verified by ear -- voice mode needs an audio-role model,
             * which this bench does not have (see the audit's environment
             * section), so this rides on the reasoning that produced the
             * accessible fix rather than on a listening test of its own. */
            jc_snprintf(q, sizeof(q), "May I run %s%s%s? Press y as in yes, "
                        "n as in no, a as in always.", name,
                        summary[0] != '\0' ? ", " : "",
                        summary[0] != '\0' ? summary : "");
            jc_voice_say(c->app, q);
            spoken = 1;
        }

        k = jc_term_read_key(c->term);
        /* M564: DIGITS ARE ACCEPTED EVERYWHERE, alongside the letters.
         *
         * The letter keys are English initials -- y/yes, a/always -- and that is
         * the root of a problem four milestones worked around rather than
         * solved: `a wie immer` is FALSE in German because `a` is not immer's
         * initial, so M552's "as in" cue cannot be translated, DIN 5009 was
         * needed to name the letter instead, and the resulting German prompt
         * measured ~108 columns against a 78-column budget.
         *
         * Digits have no language. `1` is `1` in German, Japanese and Chinese,
         * they are the most reliably spoken tokens any TTS has, and the set
         * 1/0/8/3/5 is phonetically distinct in all three -- one/zero/three/
         * five/eight, eins/null/drei/fuenf/acht, ichi/zero/san/go/hachi.
         * Consecutive digits would have collided: German zwei/drei rhyme.
         *
         * BOTH SETS ALWAYS WORK, which makes the old invariant stronger rather
         * than weaker. jc_msg.h used to say "the keys are never localized --
         * every translation must keep them"; it now says the accepted keys are a
         * fixed SUPERSET and each translation advertises the subset that reads
         * best in it. So muscle memory survives everywhere (y/n never stops
         * working, in any language) and a German user who learned 1/0 can use
         * them in an English session.
         *
         * `0` needs no branch: anything that is not an explicit yes/always/
         * view/edit already denies, which is the fence's deliberate default. */
        if (k == 'y' || k == 'Y' || k == '1') {
            confirm_echo(c, jc_msg(JC_MSG_ALLOWED));
            return 1;
        }
        if (k == 'a' || k == 'A' || k == '8') {
            always_add(c, name);
            confirm_echo(c, jc_msg(JC_MSG_ALLOWED_ALWAYS));
            return 1;
        }
        if (k == 'v' || k == 'V' || k == '5') {
            if (c->color) put(C_DIM);
            print_args_readable(c, args);
            if (c->color) put(C_RESET);
            fflush(stdout);
            continue;
        }
        if (k == 'e' || k == 'E' || k == '3') {
            /* Edit the args JSON before approving: show the current value, read
             * a replacement (blank keeps it), accept only if it parses. */
            char *ln = NULL;
            jc_read_result rr;
            printf("\n");
            if (c->color) put(C_DIM);
            printf("  current: %s\n", args);
            if (c->color) put(C_RESET);
            rr = jc_term_readline(c->term, "  new args (blank = keep) > ", &ln);
            if (rr == JC_READ_LINE && ln != NULL && ln[0] != '\0') {
                cJSON *chk = jc_json_parse(ln);
                if (chk != NULL) {
                    cJSON_Delete(chk);
                    if (edited != NULL) {
                        *edited = jc_strdup(ln);
                    }
                    free(ln);
                    confirm_echo(c, jc_msg(JC_MSG_ALLOWED_EDITED));
                    return 1;
                }
                put("  (not valid JSON; keeping original)\n");
            }
            free(ln);
            continue;  /* re-show the prompt */
        }
        /* M564: THE ADVERTISED "NO" KEYS ARE EXPLICIT, and every other byte
         * lands on the same result one line below.
         *
         * The outcome is identical either way -- `0` and `n` denied by
         * fall-through before this branch existed, exactly as `q` or a space
         * still do. The reason to name them is that a test could not otherwise
         * tell "0 is the documented no key" from "0 is not a yes key": both
         * assertions pass on a build where `0` was never advertised at all. An
         * advertised key in a SAFETY prompt should be pinned in the code, so
         * that giving `0` a function later fails a test instead of silently
         * changing what a documented keypress means.
         *
         * WHAT "ANY OTHER KEY MEANS NO" LEAVES OUT, all three verified:
         *   - `v`/`5` and `e`/`3` decide NOTHING. They `continue` above and
         *     re-show the prompt, so four of the ten advertised keys are neither
         *     yes nor no.
         *   - CTRL-C DENIES; it does not abort. enter_raw clears ISIG, so it
         *     arrives as byte 3 and falls through here. Safe, and not what a
         *     user pressing it to escape necessarily expects.
         *   - EOF denies. jc_term_read_key returns -1, which falls through.
         *
         * And one thing that is NOT a hazard: a multi-byte key (an arrow sends
         * ESC [ A) has its first byte read here and the remainder discarded,
         * because leave_raw uses TCSAFLUSH. The tail cannot leak into the next
         * prompt. */
        if (k == 'n' || k == 'N' || k == '0') {
            confirm_echo(c, jc_msg(JC_MSG_DENIED));
            return 0;
        }
        /* M565: AN UNRECOGNISED KEY IS NOT AN ANSWER, so re-ask instead of
         * denying. The operator's reason, and it is the right one: "any other
         * key can be hit accidentally. if 0 means no, 0 means no."
         *
         * Denying was SAFE but it was not silent -- it resolved a fence with a
         * byte the user never meant and reported "denied by the user" to the
         * model for a decision nobody made. A listener hears that announcement
         * and has no way to know it came from a fumble. A deliberate `0` is an
         * answer; a stray `q` is noise, and noise should not close a prompt that
         * guards a file.
         *
         * TWO KEYS STILL DENY IMMEDIATELY, and they are not accidents:
         *   EOF (-1)  -- there is nobody left to re-ask, and re-asking a closed
         *                stream is an infinite loop. Denying is the fence's
         *                correct default.
         *   Ctrl-C (3) -- enter_raw clears ISIG so it arrives as a byte, and it
         *                is the documented way out. A user pressing it wants to
         *                leave, not to be asked again.
         *
         * AND THE RETRIES ARE BOUNDED, because an unbounded re-ask is its own
         * hazard: a pipe full of garbage, or a wedged terminal sending a stream,
         * would loop forever. After UNRECOGNISED_MAX attempts the prompt gives
         * up and denies -- back to the safe default, having given a person
         * several chances to answer. */
        if (k == -1 || k == 3) {
            /* M572: CTRL-C NOW STOPS THE RUN, not just this call.
             *
             * It used to deny and return, which meant the model asked again and
             * the next Ctrl-C answered THAT prompt -- so Ctrl-C could never
             * reach the input line, and there was no way out of a retry loop
             * except to out-wait the model. The operator hit ten prompts for one
             * rename before a counter fired. I had also told them, wrongly, that
             * "Ctrl-C twice interrupts the turn"; it never could.
             *
             * Denying and stopping is what Ctrl-C means everywhere else in this
             * program (M107: at the input line the first cancels and a second
             * quits; mid-turn it aborts a model call). Answering one question
             * with it was the surprising behaviour, not this.
             *
             * EOF is the same decision for a different reason: there is nobody
             * left to re-ask, so continuing to prompt a closed stream would
             * loop until a threshold caught it.
             *
             * Safe to set here: the TUI clears abort_flag before reading each
             * new input line, so the session continues normally afterwards. */
            confirm_echo(c, jc_msg(JC_MSG_DENIED));
            if (c->app != NULL) {
                c->app->abort_flag = 1;
            }
            return 0;
        }
        if (++unrecognised >= UNRECOGNISED_MAX) {
            confirm_echo(c, jc_msg(JC_MSG_DENIED));
            return 0;
        }
        printf("  %s\n", jc_msg(JC_MSG_UNRECOGNISED_KEY));
        fflush(stdout);
        continue;
    }
}

/* Print a git-style diff (e.g. from jc_snapshot_diff) line by line, coloring
 * +/- lines and @@ headers. */
static void print_git_diff(struct tui_ctx *c, const char *txt)
{
    const char *p = txt;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        jc_size n = nl != NULL ? (jc_size)(nl - p) : (jc_size)strlen(p);
        const char *col = NULL;
        if (c->color) {
            if (n >= 2 && p[0] == '@' && p[1] == '@') col = C_CYAN;
            else if (n >= 4 && (strncmp(p, "+++ ", 4) == 0 ||
                                strncmp(p, "--- ", 4) == 0)) col = C_DIM;
            else if (p[0] == '+') col = C_GREEN;
            else if (p[0] == '-') col = C_RED;
            else if (strncmp(p, "diff ", 5) == 0 ||
                     strncmp(p, "index ", 6) == 0) col = C_DIM;
        }
        if (col != NULL) put(col);
        fwrite(p, 1, n, stdout);
        if (col != NULL) put(C_RESET);
        fputc('\n', stdout);
        if (nl == NULL) break;
        p = nl + 1;
    }
    fflush(stdout);
}

static void print_status(struct tui_ctx *c)
{
    struct jc_app *app = c->app;
    struct jc_routing_cfg *r = &app->config.routing;
    int sr = (app->config.self_review == 1) ||
             (app->config.self_review != 0 && app->mode == JC_MODE_AUTO);

    /* M296: this line was the format the other surfaces adopted, but it built the
     * pair by hand -- so a config with no `name` (the common case; it is NOT
     * mirrored from the wire id) passed NULL to "%s" and printed
     * "(null) (jlu/...)". Undefined behaviour in C89, and glibc's placeholder
     * leaking into user-facing output. */
    {
        char mdisp[192];
        jc_model_display(app->config.model.name, app->config.model.model,
                         mdisp, sizeof mdisp);
        printf("model:       %s\n", mdisp);
    }
    if (r->enabled && r->fast != NULL && r->strong != NULL) {
        printf("routing:     fast=%s  strong=%s\n", r->fast, r->strong);
    }
    printf("mode:        %s\n",
           jc_agent_mode_name((enum jc_agent_mode)app->mode));
    printf("snapshots:   %s\n",
           jc_snapshot_available(app->snapshots) ? "on" : "off");
    printf("self-review: %s\n", sr ? "on" : "off");
    printf("path-fence:  %s\n", jc_app_path_fence_on(app) ? "on" : "off");
    printf("vision:      %s\n", app->config.model.vision ? "yes" : "no");
    if (jc_config_find_by_role(&app->config, JC_ROLE_IMAGE) >= 0) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_config_models_for_role_list(&app->config, JC_ROLE_IMAGE, &sb);
        printf("image models:\n%s", sb.data != NULL ? sb.data : "");
        jc_sb_free(&sb);
    }
    {
        char si[40], so[40];
        jc_group_num(c->tot_in, chrome_group_sep(c), si, sizeof si);
        jc_group_num(c->tot_out, chrome_group_sep(c), so, sizeof so);
        /* M574: `tokens: 3,960 in / 10 out` puts the whole relation in a
         * SLASH. Everything else on this screen survives being read aloud --
         * the alignment is whitespace, which a reader collapses, and the labels
         * are words -- so this is the one line that needed an arm. The sentence
         * already exists and already has a German translation, which is the
         * argument for a catalog over a literal (M566). */
        if (c->accessible) {
            printf(jc_msg(JC_MSG_SESSION_TOKENS), si, so);
            printf("\n");
        } else {
            printf("tokens:      %s in / %s out\n", si, so);
        }
    }
    printf("cwd:         %s\n", app->cwd);
    fflush(stdout);
}

/* /cost: the running token + estimated-cost rollup for this session, plus the
 * last turn. Cost is billed at the *active* model's pricing (an approximation
 * when routing/fallback used more than one model -- the same one the exit total
 * makes). */
static void print_cost(struct tui_ctx *c)
{
    struct jc_model_cfg *m = &c->app->config.model;
    double cost = jc_config_cost(m, c->tot_in, c->tot_out,
                                 c->tot_cache_read, c->tot_cache_write);

    {
        char sep = chrome_group_sep(c);
        char si[40], so[40], cr[40], cw[40];
        jc_group_num(c->tot_in, sep, si, sizeof si);
        jc_group_num(c->tot_out, sep, so, sizeof so);
        printf("session tokens: in=%s  out=%s", si, so);
        if (c->tot_cache_read > 0.0 || c->tot_cache_write > 0.0) {
            jc_group_num(c->tot_cache_read, sep, cr, sizeof cr);
            jc_group_num(c->tot_cache_write, sep, cw, sizeof cw);
            printf("  cache(read=%s write=%s)", cr, cw);
        }
        printf("\n");
    }
    if (cost > 0.0) {
        printf("session cost:   ~$%.4f  (%s)\n", cost,
               m->model != NULL ? m->model : "?");
    } else {
        printf("session cost:   (no pricing for %s; set inputCostPer1M / "
               "outputCostPer1M)\n", m->model != NULL ? m->model : "this model");
    }
    if (c->last_in > 0.0 || c->last_out > 0.0) {
        double lc = jc_config_cost(m, c->last_in, c->last_out,
                                   c->last_cache_read, c->last_cache_write);
        char si[40], so[40];
        jc_group_num(c->last_in, chrome_group_sep(c), si, sizeof si);
        jc_group_num(c->last_out, chrome_group_sep(c), so, sizeof so);
        printf("last turn:      in=%s  out=%s", si, so);
        if (lc > 0.0) {
            printf("  (~$%.4f)", lc);
        }
        printf("\n");
    }
    fflush(stdout);
}

static void print_sessions(struct tui_ctx *c, int all)
{
    struct jc_vec metas;
    long now = (long)time(NULL);
    jc_size i;
    int shown = 0;
    int skipped = 0; /* M198: sessions present but unlistable */
    struct jc_arena *la; /* M197: the listing is printed and dropped */
    jc_status st;

    /* The metadata is consumed entirely inside this function, so it must not go
     * on app->arena (freed only at process exit) -- that made every /sessions
     * cost the store's metadata forever. */
    la = jc_arena_new(0);
    if (la == NULL) {
        put("(out of memory listing sessions)\n");
        return;
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    /* M482: same split as run_ls -- "the listing failed" is not "you have no
     * sessions". There is no exit code here, so the words are the whole signal. */
    st = jc_session_list_ex(&metas, la, &skipped);
    if (st != JC_OK && st != JC_ERR_NOTFOUND) { /* NOTFOUND = no store yet */
        put("(could not list sessions -- ");
        put(st == JC_ERR_OOM ? "out of memory)\n"
                             : "the session directory exists but could not "
                               "be read)\n");
        jc_vec_free(&metas);
        jc_arena_free(la);
        return;
    }
    if (metas.len == 0) {
        put("(no saved sessions)\n");
        if (skipped > 0) {
            printf("(%d session file%s could not be read -- unreadable, "
                   "corrupt, or over the 64 MB limit)\n",
                   skipped, skipped == 1 ? "" : "s");
        }
        jc_vec_free(&metas);
        jc_arena_free(la);
        return;
    }
    /* jc_session_list is newest-first; iterate in REVERSE so the most recent
     * prints LAST, nearest the prompt (no scroll-back to find it) -- M108. */
    for (i = metas.len; i-- > 0;) {
        struct jc_session_meta *m =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        char when[24];
        char tag[80];
        if (!all && m->workspace != NULL &&
            strcmp(m->workspace, c->app->cwd) != 0) {
            continue;
        }
        jc_reltime(now - (long)m->mtime, when, sizeof(when));
        /* An alias, when set, is the quick-find handle -- show it up front. */
        if (m->alias != NULL) {
            jc_snprintf(tag, sizeof(tag), "@%s", m->alias);
        } else {
            tag[0] = '\0';
        }
        printf("  %-8.8s  %-10s %-9s  %s\n", m->id, tag, when,
               m->title ? m->title : "(untitled)");
        shown = 1;
    }
    jc_vec_free(&metas);
    jc_arena_free(la);
    if (skipped > 0) {
        /* M198: never let a session vanish from the listing without a word. */
        printf("(%d session file%s could not be read -- unreadable, corrupt, "
               "or over the 64 MB limit)\n", skipped, skipped == 1 ? "" : "s");
    }
    if (!shown) {
        put("(none for this project; /sessions --all for every project)\n");
    } else {
        put(C_DIM "  (most recent last; /resume <id|@alias>, "
            "/name <alias>, /sessions clear)" C_RESET "\n");
    }
}

/* Delete saved sessions (M108). `what`: "" = this project's sessions, "--all" =
 * every project's, "@alias" / "<id|prefix>" = one specific. Never deletes the
 * current live session (`cur_id`). Confirms with a single keypress. */
static void clear_sessions(struct tui_ctx *c, const char *cur_id,
                           const char *what)
{
    struct jc_vec metas;
    jc_size i;
    int all = (strcmp(what, "--all") == 0);
    char one[64];
    int have_one = 0;
    int n = 0, deleted = 0, k;
    struct jc_arena *la; /* M197: the listing is consumed inside this call */

    if (!all && what[0] != '\0') {
        int r = (what[0] == '@')
            ? jc_session_resolve_alias(what + 1, one, sizeof one, c->app->arena)
            : jc_session_resolve_prefix(what, one, sizeof one, c->app->arena);
        if (r != 0) {
            printf("no %ssession matching '%s'\n",
                   r == -2 ? "unambiguous " : "", what);
            return;
        }
        have_one = 1;
    }

    la = jc_arena_new(0);
    if (la == NULL) {
        put("(out of memory listing sessions)\n");
        return;
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    if (jc_session_list(&metas, la) != JC_OK || metas.len == 0) {
        put("(no saved sessions)\n");
        jc_vec_free(&metas);
        jc_arena_free(la);
        return;
    }
    for (i = 0; i < metas.len; i++) {
        struct jc_session_meta *m =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        if (cur_id != NULL && m->id != NULL && strcmp(m->id, cur_id) == 0)
            continue; /* never delete the live session */
        if (have_one) {
            if (m->id == NULL || strcmp(m->id, one) != 0) continue;
        } else if (!all) {
            if (m->workspace == NULL || strcmp(m->workspace, c->app->cwd) != 0)
                continue;
        }
        n++;
    }
    if (n == 0) {
        put("(nothing to clear)\n");
        jc_vec_free(&metas);
        jc_arena_free(la);
        return;
    }
    printf("  Delete %d saved session%s%s? [y/N] ", n, n == 1 ? "" : "s",
           have_one ? "" : (all ? " (all projects)" : " (this project)"));
    fflush(stdout);
    k = jc_term_read_key(c->term);
    put("\n");
    if (k != 'y' && k != 'Y') {
        put("(cancelled)\n");
        jc_vec_free(&metas);
        jc_arena_free(la);
        return;
    }
    for (i = 0; i < metas.len; i++) {
        struct jc_session_meta *m =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        if (cur_id != NULL && m->id != NULL && strcmp(m->id, cur_id) == 0)
            continue;
        if (have_one) {
            if (m->id == NULL || strcmp(m->id, one) != 0) continue;
        } else if (!all) {
            if (m->workspace == NULL || strcmp(m->workspace, c->app->cwd) != 0)
                continue;
        }
        if (jc_session_delete(m->id) == JC_OK) deleted++;
    }
    jc_vec_free(&metas);
    jc_arena_free(la);
    printf("(deleted %d session%s)\n", deleted, deleted == 1 ? "" : "s");
}

/* Hidden gems (M109). Intentionally UNDOCUMENTED: these slash commands are absent
 * from /help, from Tab completion, and from the docs -- they live only here, in the
 * source, as a small reward for the curious reader. Keep them tasteful and on-brand
 * (a learning/teaching companion): a nod to the people this stands on, and a gentle
 * nudge to be kind to yourself while you build. Returns 1 if `line` was a gem (and
 * handled it), 0 otherwise. TUI-only, so they never leak into --output json/headless.
 *
 * Known gems: /credits, /tea, /zen, /thanks (+ /thankyou). Add sparingly. */
static int easter_egg(struct tui_ctx *c, const char *line)
{
    int color = c->color;
    if (strcmp(line, "/credits") == 0) {
        if (color) put(C_GREEN);
        put("jichi");
        if (color) put(C_RESET);
        put(" - a learning & building companion.\n");
        put("  Lead developers: Claude (Anthropic) & "
            "Alexander-Lars Dallmann.\n");
        put("  Standing on the shoulders of Continue and opencode - thank you.\n");
        put("  Made with care for learners, teachers, and builders. ");
        put(c->unicode ? "\xf0\x9f\x8c\xb1\n" : "<3\n"); /* seedling / <3 */
        return 1;
    }
    if (strcmp(line, "/tea") == 0) {
        put(c->unicode ? "  \xe2\x98\x95  " : "  [tea]  ");
        put("Take a breath. The code will wait.\n");
        if (color) put(C_DIM);
        put("  (Some of the best fixes arrive between sips.)\n");
        if (color) put(C_RESET);
        return 1;
    }
    if (strcmp(line, "/zen") == 0) {
        if (color) put(C_DIM);
        put("  Make it run, make it right, make it clear.\n");
        put("  A small function understood beats a clever one feared.\n");
        if (color) put(C_RESET);
        return 1;
    }
    if (strcmp(line, "/thanks") == 0 || strcmp(line, "/thankyou") == 0) {
        put("  The pleasure is mine. Let's keep building. ");
        put(c->unicode ? "\xf0\x9f\xa4\x9d\n" : ":)\n"); /* handshake */
        return 1;
    }
    return 0;
}

static void banner(int color)
{
    if (color) put(C_GREEN);
    put("jichi");
    if (color) put(C_RESET);
    put(" - interactive agent. Type /exit to quit, /help for commands.\n\n");
}

/* A section header in /help (dim when color is on for visual grouping). */
static void help_section(int color, const char *title)
{
    printf("\n");
    if (color) put(C_BOLD);
    printf("%s\n", title);
    if (color) put(C_RESET);
}

static void help(struct jc_app *app)
{
    int color = jc_color_enabled(app->color_mode, 1);

    put("Type anything to send it to the model. Commands:\n");

    help_section(color, "Mode & model");
    put("  /mode [m]      show mode, or switch: chat | plan | auto\n");
    put("  /plan          enter plan mode (/plan off to leave)\n");
    put("  /auto          toggle auto mode (run tools without asking)\n");
    put("  /model [sel]   list models, or switch by number/name\n");
    put("  /route [...]   routing: on|off | stall on|off | context <pct>|off "
        "| fast <m> | "
        "strong <m>\n");
    put("  /timeouts      show the model-call timeouts in effect\n");

    help_section(color, "Context & cost");
    put("  /context       context-budget breakdown (prompt/tools/history)\n");
    put("  /context tools per-tool definition sizes; /context history where the\n");
    put("                 history went (by role, by tool, largest messages)\n");
    put("  /compact       summarize older history to free up context\n");
    put("  /cost          this session's token usage and estimated cost\n");
    put("  /status        model, mode, routing, tokens, and cwd\n");
    put("  /autocontext [on|off]  toggle automatic retrieval of context (RAG)\n");
    put("  /cache [on|off]     toggle prompt caching; bare shows hit-rate\n");

    help_section(color, "Workspace & undo");
    put("  /diff          show changes since the last checkpoint\n");
    put("  /undo          revert the workspace to the last checkpoint\n");
    put("  /rewind [n]    revert files AND conversation to a checkpoint's turn "
        "(--dry-run to preview)\n");
    put("  /checkpoints   list this session's snapshots\n");
    put("  /review        toggle the self-review pass on edits\n");
    put("  /verify        run the configured verifier command\n");
    put("  /chat          return to chat mode (mutating tools ask first)\n");
    put("  /voice [on|off] speak replies, approval questions and errors "
        "aloud\n");
    put("  /listen [secs]  record a fixed window, transcribe it, use it as the "
        "prompt\n");
    put("  /memory        show persisted notes (.jichi/memory.md)\n");
    put("  /learn         draft lessons from this project's logs (the mentor)\n");
    put("  /learn analyze [log]  rank recurring problems -- offline, no model "
        "call\n");
    put("  /learn apply [--force]  commit the reviewed draft (memory, skills, "
        "corrections, rules)\n");
    put("  /learn corrections  commit ONLY the draft's corrections (retract "
        "stale notes)\n");
    put("  /constraints [add <text>|clear]  hard limits, ENFORCED every turn "
        "(.jichi/constraints.md)\n");
    put("  /config [show|set <k> <v>|telemetry <lvl>]  view/edit config "
        "(edits need configEditable)\n");
    put("  /benchmark     score this project's config (best-practice coverage)\n");
    put("  /packages [recommend]  browse packs/presets (recommend uses the "
        "model)\n");

    help_section(color, "Learning (assignments)");
    put("  /assignments   list docs/assignments/ (briefs to work on)\n");
    put("  /assignment <spec.md>  load a brief and study it here -- the model\n"
        "                 becomes a TUTOR (guides, never solves); 'off' ends\n");
    put("  /hint          reveal the next graded hint for the active brief\n");
    put("  /grade         run the brief's own verify; PASS/FAIL + failures\n");
    put("  /tutor <q>     ask the read-only helper (a nudge, never the code)\n");

    help_section(color, "Session");
    put("  /sessions      list saved conversations (recent last; --all "
        "for every project)\n");
    put("  /sessions clear [--all|<id>|@alias]  delete saved sessions "
        "(confirms)\n");
    put("  /resume [id|@alias]  resume a conversation (bare = most recent)\n");
    put("  /fork          branch a new session from here (original kept)\n");
    put("  /title <text>  set the conversation title\n");
    put("  /name <alias>  set a quick-find name (resume via /resume @alias)\n");
    put("  /export [--html] [file]  write the transcript to Markdown/HTML\n");
    put("  /clear         clear the conversation history\n");

    help_section(color, "Knowledge & display");
    put("  /mcp           list connected MCP servers and their tools\n");
    put("  /skills        list available agent skills\n");
    put("  /map           show the repository map (files + symbols)\n");
    put("  /board         show the kanban phase board (.jichi/board.json)\n");
    put("  /output-style [name|off]  show/switch the response style\n");
    put("  /design [file|off]       show/set the authoritative design doc\n");
    put("  /language [lang|off]  answer in this natural language\n");
    put("  /markdown [on|off]  toggle markdown/syntax rendering of replies\n");
    put("  /typeahead [on|off]  type while I work; Enter queues, Ctrl-K "
        "un-queues\n");
    put("  /wisdom [on|off|reload]  idle proverbs from .jichi/wisdom.json\n");
    put("  /quiet [on|off]     toggle minimal output\n");
    put("  /help          show this help; /exit or /quit to leave\n");

    /* Keys, not commands -- and the reason they are listed at all: a gesture
     * nobody can discover is a gesture nobody uses. Ctrl-G and Ctrl-Q are the
     * two model-assisted ones and are easy to confuse, so they are described
     * by what they DO to your line: one fills it in, one comments on it. */
    help_section(color, "Composing a prompt (keys)");
    put("  Tab            complete a command, model, session id or @path\n");
    put("  Ctrl-G         suggest how to finish this line (Tab accepts, any "
        "other key dismisses)\n");
    put("  Ctrl-Q         advice: is this request clear? (printed below, "
        "never inserted)\n");
    put("  Ctrl-R         search your history; \\ at end of line continues "
        "on the next\n");
    put("  Ctrl-A/E       start / end of line;  Ctrl-U/K  kill to start / "
        "end\n");
    put("  Ctrl-L         clear the screen;  Ctrl-C  cancel;  Ctrl-D  exit "
        "on an empty line\n");

    /* M573: THE APPROVAL FENCE WAS NOWHERE IN /help, which the operator's
     * question exposed -- "the user, or agent has to know about the three
     * refusals rule". It was written down only in the engineering records. A
     * rule that ends your turn belongs somewhere you can read BEFORE it
     * happens, not only in the sentence that announces it afterwards. */
    help_section(color, "When jichi asks permission");
    put("  y / 1  yes      n / 0  no      a / 8  always (this session)\n");
    put("  e / 3  edit the call first     v / 5  view the whole call\n");
    put("  Any other key does nothing and asks again -- a slip is not an "
        "answer.\n");
    put("  Ctrl-C stops the whole run, not just the one call.\n");
    put("  Three refusals in a row also stop it. Approving anything clears "
        "that count,\n");
    put("  and it resets for every new message you send.\n");

    help_section(color, "Line editing (emacs/bash-style)");
    put("  Ctrl-A/E  start/end    Ctrl-B/F or arrows  char left/right\n");
    put("  Alt-B/F   word left/right    Ctrl-P/N or arrows  history\n");
    put("  Ctrl-W    kill word back     Alt-D  kill word forward\n");
    put("  Ctrl-U/K  kill to start/end  Ctrl-Y  yank (paste last kill)\n");
    put("  Ctrl-D    delete char (EOF on empty)  Ctrl-T  transpose\n");
    put("  Ctrl-_    undo    Alt-U/L/C  upcase/downcase/capitalize word\n");
    put("  Ctrl-R    search history      Ctrl-L  clear screen\n");
    put("  Tab  complete   Ctrl-G  suggest   \\ + Enter  continue on a new line\n");

    if (app->commands.commands.len > 0) {
        jc_size i;
        put("Custom commands:\n");
        for (i = 0; i < app->commands.commands.len; i++) {
            struct jc_command *c =
                (struct jc_command *)jc_vec_at(&app->commands.commands, i);
            printf("  /%-12s %s\n", c->name,
                   c->description != NULL ? c->description : "");
        }
    }
    if (app->mcp != NULL && jc_mcp_prompt_count(app->mcp) > 0) {
        int i;
        int np = jc_mcp_prompt_count(app->mcp);
        put("MCP prompts (run as a slash command):\n");
        for (i = 0; i < np; i++) {
            const char *desc = "";
            const char *nm = jc_mcp_prompt_at(app->mcp, i, NULL, &desc);
            if (nm != NULL) {
                printf("  /%-12s %s\n", nm, desc != NULL ? desc : "");
            }
        }
    }
    put("\n");
    put("Keys: Tab completes commands/paths; Ctrl-G suggests a continuation "
        "(Tab accepts).\n");
}

/* --- learner-support session state (M173b) --------------------------------
 * Storage for the /assignment-loaded spec. File-static because the TUI runs
 * once per process; the spec's strings live on app->arena (session-lived), so
 * the pointer stays valid for as long as the assignment is active. */
static struct jc_assign_spec g_assignment;
static char g_assignment_path[1100]; /* as given to /assignment, for --record */

/* Sort a jc_vec of char* names so a numbered curriculum lists in order. */
static int tui_name_cmp(const void *a, const void *b)
{
    return strcmp(*(char * const *)a, *(char * const *)b);
}

static void assignment_off(struct jc_app *app)
{
    app->assignment = NULL;
    app->assignment_tutor = 0;
    app->hints_used = 0;
    g_assignment_path[0] = '\0';
    app->assignment_spec = NULL;              /* M536 */
    app->assignment_dir[0] = '\0';
}

/* Grade the active assignment THROUGH THE ONE MECHANIC (jc_grade_core, M529/
 * M614) and print a grade line. This used to be a fourth inline
 * implementation: setup+verify+score re-typed here, without the M502
 * reachability guard, and it recorded UNCONDITIONALLY -- so a verify that
 * could not run from this directory wrote FAIL 0%% into the learner's
 * permanent progress file as if the work were wrong. Now: a refusal is not a
 * grade, is said so in M502's words, and records nothing. */
static void assignment_grade(struct jc_app *app, int color)
{
    const struct jc_assign_spec *spec = app->assignment;
    struct jc_grade_out g;
    struct jc_assign_result res;

    if (spec->verify == NULL || spec->verify[0] == '\0') {
        printf("(this assignment has no `verify` command -- it is graded by "
               "review, not mechanically; use /check or ask your instructor)\n");
        return;
    }
    if (g_assignment_path[0] == '\0') {
        printf("(no spec path recorded for the active assignment -- reload it "
               "with /assignment <spec.md>)\n");
        return;
    }
    jc_grade_core(g_assignment_path, jc_app_scratch(app), &g);
    switch (g.fail) {
    case JC_GRADE_UNREADABLE:
        printf("(could not re-read '%s' to grade it)\n", g_assignment_path);
        return;
    case JC_GRADE_NO_TASK:
        printf("('%s' has no task body)\n", g_assignment_path);
        return;
    case JC_GRADE_CANNOT_RUN:
        printf("the verify command cannot run from here -- '%s' does not "
               "exist relative to this directory.\n  verify: %s\n"
               "This is NOT a grade (nothing was recorded). Run jichi from "
               "the directory the spec's paths are relative to.\n",
               g.prog, g.spec.verify);
        return;
    default:
        break;
    }
    res = g.res;

    if (color) {
        printf("%s%s%s", res.passed ? C_GREEN : C_RED,
               res.passed ? "PASS" : "FAIL", C_RESET);
    } else {
        printf("%s", res.passed ? "PASS" : "FAIL");
    }
    printf("  %s\n", spec->title != NULL ? spec->title : "(assignment)");
    printf("  verify: %s (exit %d)\n", g.spec.verify, g.verify_exit);
    if (res.tests_run > 0) {
        printf("  tests: %d run, %d failed  (%d%%)\n", res.tests_run,
               res.tests_failed, res.pct);
    }
    if (!res.passed && g.miss_dir[0] != '\0') {
        /* M617: same wrong-directory note the CLI grade prints. */
        printf("  note: the verify references %s/, which does not exist from "
               "here.\n  If this FAIL surprises you, run jichi from the "
               "repository root\n  (a missing directory can also be part of "
               "the task).\n", g.miss_dir);
    }
    if (!res.passed && g.have_rep && g.rep.failures.len > 0) {
        jc_size k;
        for (k = 0; k < g.rep.failures.len && k < 3; k++) {
            const struct jc_test_failure *f =
                (const struct jc_test_failure *)jc_vec_at(&g.rep.failures, k);
            printf("  - %s\n", (f != NULL && f->message != NULL)
                   ? f->message : "(failure)");
        }
    }
    /* Record the attempt (C5, M174): the TUI is the self-learner's loop, so
     * every REAL /grade lands in the progress file (headless `grade` records
     * only with --record). The TUI knows the hint count; record it -- visible,
     * never penalised. Refusals above never reach this line. */
    if (jc_progress_append(app->cwd, g_assignment_path, res.passed, res.pct,
                           res.tests_run, res.tests_failed,
                           app->hints_used) == JC_OK) {
        if (color) {
            printf(C_DIM "  (recorded to .jichi/progress.jsonl)"
                   C_RESET "\n");
        } else {
            printf("  (recorded to .jichi/progress.jsonl)\n");
        }
    }
    jc_grade_out_free(&g);
}

static void cmpl_push(struct jc_vec *out, const char *s)
{
    char *d = jc_strdup(s);
    if (d != NULL && jc_vec_push(out, &d) != JC_OK) free(d);
}

/* The built-in slash commands, shared by Tab completion and the M345
 * did-you-mean below (one list, so the two surfaces cannot disagree about
 * what exists). */
static const char *TUI_CMDS[] = {
    "/help", "/clear", "/model", "/mode", "/plan", "/auto", "/mcp",
    "/skills", "/output-style", "/design", "/language", "/map", "/board",
    "/status",
    "/cost",
    "/timeouts",
    /* M295: /route was handled but absent here, so it never Tab-completed and
     * never appeared in the completion list -- found by building that lint's
     * source of truth, which is the sort of thing comparing two lists finds and
     * reading one does not. The easter eggs (/tea, /zen, /thanks, /thankyou,
     * /credits) stay out on purpose: undiscoverable is the point -- and the
     * M345 suggester deliberately inherits that: it must never hint at them. */
    "/route",
    "/review",
    "/verify", "/compact", "/context", "/diff", "/memory", "/constraints",
    "/learn analyze", "/learn apply", "/learn corrections",
    "/voice", "/listen", "/chat",
    "/assignments", "/assignment", "/hint", "/grade", "/tutor",
    "/config", "/benchmark", "/packages", "/markdown", "/typeahead",
    "/cache", "/autocontext", "/wisdom", "/accessible",
    "/quiet",
    "/undo", "/rewind", "/checkpoints", "/sessions", "/resume", "/fork",
    "/title", "/name", "/export", "/exit",
    "/quit", 0
};

/* M350: on resume, tell the model which files it worked with moved while the
 * conversation slept (a human edit, a git pull, a CLI undo) -- the M349 undo
 * notice's sibling for the sleeping half. The restored history describes the
 * files as they WERE; this one [resume] user message reconciles it with the
 * disk as it IS. Saved immediately, so a second resume detects nothing twice;
 * an unchanged workspace injects nothing. */
static void tui_note_drift(struct jc_session *s, struct jc_history *hist)
{
    struct jc_sb names, note;

    jc_sb_init(&names);
    jc_sb_init(&note);
    if (jc_session_drift_names(s, &names) == JC_OK &&
        names.data != NULL && names.len > 0) {
        jc_session_drift_render(names.data, &note);
        if (note.len > 0 && note.data != NULL) {
            jc_history_add(hist, JC_ROLE_USER, note.data);
            jc_session_save(s);
            put("(noted for the model: files changed since this conversation "
                "last ran)\n");
        }
    }
    jc_sb_free(&names);
    jc_sb_free(&note);
}

/* M345: the nearest known command to a typo'd one, or NULL to stay silent.
 * The model has had "did you mean 'search_code'?" since M91 (jc_tool.c); the
 * human typing /hlep got "(try /help)" -- the same mistake answered less
 * kindly on the surface a person actually types at. Same closeness rule
 * (jc_str_close_enough), same universe Tab completion offers: built-ins,
 * custom commands, MCP prompts. Bounded: past ~250 candidates the rest are
 * simply not considered, which degrades a hint, never a command. */
static const char *tui_suggest_cmd(struct jc_app *app, const char *cname)
{
    const char *cands[256];
    jc_size n = 0, k;

    for (k = 0; TUI_CMDS[k] != NULL && n < 250; k++) {
        cands[n++] = TUI_CMDS[k];
    }
    for (k = 0; k < app->commands.commands.len && n < 250; k++) {
        struct jc_command *cmd =
            (struct jc_command *)jc_vec_at(&app->commands.commands, k);
        if (cmd->name != NULL) {
            cands[n++] = cmd->name;
        }
    }
    if (app->mcp != NULL) {
        int pi;
        int np = jc_mcp_prompt_count(app->mcp);
        for (pi = 0; pi < np && n < 250; pi++) {
            const char *nm = jc_mcp_prompt_at(app->mcp, pi, NULL, NULL);
            if (nm != NULL) {
                cands[n++] = nm;
            }
        }
    }
    cands[n] = NULL;
    return jc_str_closest(cname, cands);
}

/* Tab-completion provider for the input line: slash commands, /resume session
 * ids, /model names, /mode values, and @file paths. */
static int tui_complete(void *vctx, const char *buf, jc_size cursor,
                        jc_size *tstart, struct jc_vec *out)
{
    const char *const *cmds = TUI_CMDS;
    struct tui_ctx *c = (struct tui_ctx *)vctx;
    jc_size start;
    const char *tok = jc_complete_token(buf, cursor, &start);
    jc_size tl = cursor - start;
    *tstart = start;

    /* Slash-command name (token at the line start). */
    if (start == 0 && buf[0] == '/') {
        int i;
        jc_size k;
        for (i = 0; cmds[i] != NULL; i++) {
            if (strncmp(cmds[i], tok, tl) == 0) cmpl_push(out, cmds[i]);
        }
        for (k = 0; k < c->app->commands.commands.len; k++) {
            struct jc_command *cmd = (struct jc_command *)
                jc_vec_at(&c->app->commands.commands, k);
            char full[160];
            jc_snprintf(full, sizeof(full), "/%s", cmd->name);
            if (strncmp(full, tok, tl) == 0) cmpl_push(out, full);
        }
        if (c->app->mcp != NULL) {
            int pi;
            int np = jc_mcp_prompt_count(c->app->mcp);
            for (pi = 0; pi < np; pi++) {
                const char *nm = jc_mcp_prompt_at(c->app->mcp, pi, NULL, NULL);
                char full[160];
                if (nm == NULL) continue;
                jc_snprintf(full, sizeof(full), "/%s", nm);
                if (strncmp(full, tok, tl) == 0) cmpl_push(out, full);
            }
        }
        return (int)out->len;
    }
    /* /resume <session-id prefix> */
    if (strncmp(buf, "/resume ", 8) == 0) {
        struct jc_vec metas;
        jc_size i;
        /* M197: this runs on EVERY Tab press, and cmpl_push strdups what it
         * keeps -- so the listing must not touch app->arena, which is freed
         * only at process exit. It used to cost the whole store per keypress. */
        struct jc_arena *la = jc_arena_new(0);
        if (la == NULL) {
            return (int)out->len;
        }
        jc_vec_init(&metas, sizeof(struct jc_session_meta));
        if (jc_session_list(&metas, la) == JC_OK) {
            for (i = 0; i < metas.len; i++) {
                struct jc_session_meta *m =
                    (struct jc_session_meta *)jc_vec_at(&metas, i);
                if (m->id != NULL && strncmp(m->id, tok, tl) == 0) {
                    cmpl_push(out, m->id);
                }
            }
        }
        jc_vec_free(&metas);
        jc_arena_free(la);
        return (int)out->len;
    }
    /* /model <name prefix> */
    if (strncmp(buf, "/model ", 7) == 0) {
        int nm = jc_config_model_count(&c->app->config);
        int i;
        for (i = 0; i < nm; i++) {
            struct jc_model_cfg *m = jc_config_model_at(&c->app->config, i);
            if (m->name != NULL && strncmp(m->name, tok, tl) == 0) {
                cmpl_push(out, m->name);
            }
        }
        return (int)out->len;
    }
    /* /context <view> (M317) */
    if (strncmp(buf, "/context ", 9) == 0) {
        static const char *views[] = { "tools", "history", 0 };
        int i;
        for (i = 0; views[i] != NULL; i++) {
            if (strncmp(views[i], tok, tl) == 0) {
                cmpl_push(out, views[i]);
            }
        }
        return (int)out->len;
    }
    /* /mode <value> */
    if (strncmp(buf, "/mode ", 6) == 0) {
        static const char *modes[] = { "chat", "plan", "auto", 0 };
        int i;
        for (i = 0; modes[i] != NULL; i++) {
            if (strncmp(modes[i], tok, tl) == 0) cmpl_push(out, modes[i]);
        }
        return (int)out->len;
    }
    /* @file path */
    if (tl >= 1 && tok[0] == '@') {
        static const char *prov[] = { "@diff", "@url:", "@sym:", 0 };
        const char *pathpart = tok + 1;
        const char *slash = strrchr(pathpart, '/');
        char dir[1024];
        const char *partial;
        char listdir[1100];
        struct jc_vec names;
        struct jc_arena *la;
        jc_size k;
        int i;
        for (i = 0; prov[i] != NULL; i++) {
            if (strncmp(prov[i], tok, tl) == 0) cmpl_push(out, prov[i]);
        }
        if (slash != NULL) {
            jc_size dl = (jc_size)(slash - pathpart);
            if (dl >= sizeof(dir)) dl = sizeof(dir) - 1;
            memcpy(dir, pathpart, dl);
            dir[dl] = '\0';
            partial = slash + 1;
        } else {
            dir[0] = '\0';
            partial = pathpart;
        }
        if (dir[0] == '/') jc_snprintf(listdir, sizeof(listdir), "%s", dir);
        else if (dir[0] == '\0')
            jc_snprintf(listdir, sizeof(listdir), "%s", c->app->cwd);
        else jc_snprintf(listdir, sizeof(listdir), "%s/%s", c->app->cwd, dir);
        /* M197: a local arena, not app->arena and not scratch. Tab completion
         * runs BETWEEN turns, so scratch (reset per top-level turn) would still
         * accumulate a directory listing per keypress; cmpl_push strdups what it
         * keeps, so nothing here outlives the call. */
        la = jc_arena_new(0);
        if (la == NULL) {
            return (int)out->len;
        }
        jc_vec_init(&names, sizeof(char *));
        if (jc_list_dir(listdir, &names, la) == JC_OK) {
            jc_size pl = (jc_size)strlen(partial);
            for (k = 0; k < names.len; k++) {
                const char *nm = *(char **)jc_vec_at(&names, k);
                char sub[1200];
                char chk[1400];
                char full[1300];
                if (nm[0] == '.' && partial[0] != '.') continue; /* hide dotfiles */
                if (strncmp(nm, partial, pl) != 0) continue;
                if (dir[0] == '\0') jc_snprintf(sub, sizeof(sub), "%s", nm);
                else jc_snprintf(sub, sizeof(sub), "%s/%s", dir, nm);
                if (sub[0] == '/') jc_snprintf(chk, sizeof(chk), "%s", sub);
                else jc_snprintf(chk, sizeof(chk), "%s/%s", c->app->cwd, sub);
                jc_snprintf(full, sizeof(full), "@%s%s", sub,
                            jc_is_dir(chk) ? "/" : "");
                cmpl_push(out, full);
            }
        }
        jc_vec_free(&names);
        jc_arena_free(la);
        return (int)out->len;
    }
    return 0;
}

/* Inline-suggestion ("ghost text") callback for the line editor: one non-
 * streaming completion of the current input via the autocomplete-role model
 * (falling back to the active model), returned as a single trimmed, capped line.
 * Mirrors the headless `complete` one-shot; a short timeout keeps the editor
 * responsive, and SIGINT (abort_flag) cancels it. */
static jc_size tui_suggest(void *vctx, const char *buf, jc_size cursor,
                           char *out, jc_size cap)
{
    char sys[JC_SUGGEST_SYS_CAP];
    struct tui_ctx *c = (struct tui_ctx *)vctx;
    const struct jc_model_cfg *m;
    struct jc_provider *prov;
    struct jc_history mini;
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *body = NULL;
    char *resp = NULL;
    long http_status = 0;
    jc_size n = 0;
    (void)cursor;

    out[0] = '\0';
    jc_suggest_system(sys, sizeof(sys));
    m = jc_app_model_for_role(c->app, JC_ROLE_AUTOCOMPLETE);
    if (m == NULL) {
        m = &c->app->config.model;
    }
    prov = jc_provider_create(m);
    if (prov == NULL) {
        return 0;
    }
    jc_history_init(&mini);
    jc_history_add(&mini, JC_ROLE_USER, buf);
    if (prov->vt->build_request(prov, &mini, sys, NULL, 0, &body) != JC_OK) {
        jc_history_free(&mini);
        prov->vt->free(prov);
        return 0;
    }
    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = prov->vt->endpoint(prov);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = 12;
    req.abort_flag = &c->app->abort_flag;
    if (jc_http_perform(&req, &http_status, &resp, NULL) == JC_OK &&
        http_status < 400 && resp != NULL) {
        struct jc_message *reply = jc_history_add(&mini, JC_ROLE_ASSISTANT,
                                                  NULL);
        if (prov->vt->parse_full(prov, resp, reply) == JC_OK &&
            reply->content != NULL) {
            /* The cleaner (pure, unit-tested) handles what models do despite
             * the prompt: a leading blank line, an "output:" label copied from
             * the few-shot examples, quotes, an echo of the typed line. */
            n = jc_suggest_clean(buf, reply->content, out, cap);
        }
    }
    jc_http_headers_free(&headers);
    free(body);
    free(resp);
    jc_history_free(&mini);
    prov->vt->free(prov);
    /* M462 (docs/ACCESSIBILITY.md deferral): speak it when voice is on.
     *
     * Ghost text is DIM GREY OVERLAY TEXT, which is the one rendering a screen
     * reader is least likely to convey -- so for the users voice mode exists
     * for, Ctrl-G was a feature that produced no observable output at all. It
     * is not enough to ship a voice mode and leave the visual-only surfaces
     * visual: that is how a feature becomes inaccessible without anyone
     * deciding it should be.
     *
     * Guarded on n > 0 so an empty suggestion says nothing rather than
     * announcing silence, and it runs only on the explicit keypress -- there is
     * no per-keystroke path here to make chatty. */
    if (n > 0 && c->app != NULL && c->app->config.voice) {
        jc_voice_say(c->app, out);
    }
    return n;
}

/* Prompt-advice callback for the line editor (Ctrl-Q, M280): one non-streaming
 * call asking what is unclear about the request being composed, returned as a
 * single line for jc_term to print above a redrawn prompt.
 *
 * Deliberately the ACTIVE model, not the autocomplete-role one: judging whether
 * a request is answerable is the job the user is already paying the chat model
 * for, and a 0.5B completion model would invent nitpicks. The mirror-image
 * choice to tui_suggest above, and the reason both exist separately.
 *
 * Never touches the input buffer -- that is the difference from ghost text, and
 * the reason a model's clarifying question is useful here instead of garbling
 * the line it was spliced into. */
static jc_size tui_advise(void *vctx, const char *buf, jc_size cursor,
                          char *out, jc_size cap)
{
    char sys[JC_SUGGEST_SYS_CAP];
    struct tui_ctx *c = (struct tui_ctx *)vctx;
    const struct jc_model_cfg *m;
    struct jc_provider *prov;
    struct jc_history mini;
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *body = NULL;
    char *resp = NULL;
    long http_status = 0;
    jc_size n = 0;
    (void)cursor;

    out[0] = '\0';
    jc_advice_system(sys, sizeof(sys));
    m = &c->app->config.model;
    prov = jc_provider_create(m);
    if (prov == NULL) {
        return 0;
    }
    jc_history_init(&mini);
    jc_history_add(&mini, JC_ROLE_USER, buf);
    if (prov->vt->build_request(prov, &mini, sys, NULL, 0, &body) != JC_OK) {
        jc_history_free(&mini);
        prov->vt->free(prov);
        return 0;
    }
    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = prov->vt->endpoint(prov);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = 20;      /* a judgement call, not a completion: allow more */
    req.abort_flag = &c->app->abort_flag;
    if (jc_http_perform(&req, &http_status, &resp, NULL) == JC_OK &&
        http_status < 400 && resp != NULL) {
        struct jc_message *reply = jc_history_add(&mini, JC_ROLE_ASSISTANT,
                                                  NULL);
        if (prov->vt->parse_full(prov, resp, reply) == JC_OK &&
            reply->content != NULL) {
            n = jc_advice_clean(reply->content, out, cap);
        }
    }
    jc_http_headers_free(&headers);
    free(body);
    free(resp);
    jc_history_free(&mini);
    prov->vt->free(prov);
    return n;
}

/* #11: parse one wisdom.json into `acc` (a jc_vec of char*), each entry
 * pre-formatted onto the session arena. Tolerant: missing/malformed => no-op.
 * Accepts {"entries":[{text,reading?,translation?}]} or a bare array. */
static void wisdom_add_file(struct tui_ctx *c, const char *path,
                            struct jc_vec *acc)
{
    char *text = NULL;
    jc_size len = 0;
    cJSON *root, *arr, *it;
    if (jc_read_file(path, &text, &len, c->app->arena) != JC_OK ||
        text == NULL) {
        return;
    }
    root = cJSON_Parse(text);
    if (root == NULL) {
        return;
    }
    arr = cJSON_IsArray(root) ? root
        : cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (cJSON_IsArray(arr)) {
        for (it = arr->child; it != NULL; it = it->next) {
            const char *txt = jc_json_get_str(it, "text", NULL);
            const char *rd = jc_json_get_str(it, "reading", NULL);
            const char *tr = jc_json_get_str(it, "translation", NULL);
            struct jc_sb sb;
            char *line;
            if (txt == NULL) {
                continue;
            }
            jc_sb_init(&sb);
            if (rd != NULL && rd[0] != '\0') {
                jc_sb_append(&sb, rd);
                jc_sb_append(&sb, " \xC2\xB7 "); /* " · " */
            }
            jc_sb_append(&sb, txt);
            if (tr != NULL && tr[0] != '\0') {
                jc_sb_append(&sb, " \xE2\x80\x94 "); /* " — " */
                jc_sb_append(&sb, tr);
            }
            line = jc_arena_strdup(c->app->arena,
                                   sb.data != NULL ? sb.data : txt);
            jc_sb_free(&sb);
            if (line != NULL) {
                jc_vec_push(acc, &line);
            }
        }
    }
    cJSON_Delete(root);
}

static void tui_load_wisdom(struct tui_ctx *c)
{
    struct jc_vec acc;
    char path[1200];
    const char *home;
    c->wisdom = NULL;
    c->n_wisdom = 0;
    c->wisdom_idx = 0;
    /* Show by default per config (never in quiet mode); /wisdom on can flip it
     * even when config-disabled, so always load the data regardless. */
    c->show_wisdom = c->app->config.wisdom && !c->quiet;
    jc_vec_init(&acc, sizeof(char *));
    home = jc_home_dir();
    if (home != NULL) {
        jc_snprintf(path, sizeof path,
                    "%s/.config/jichi/wisdom.json", home);
        wisdom_add_file(c, path, &acc);
    }
    jc_snprintf(path, sizeof path, "%s/.jichi/wisdom.json", c->app->cwd);
    wisdom_add_file(c, path, &acc);
    if (acc.len > 0) {
        /* Copy the pointer array onto the arena so it survives jc_vec_free
         * (the strings are already arena-owned). */
        char **out = (char **)jc_arena_alloc(c->app->arena,
                                             acc.len * sizeof(char *));
        if (out != NULL) {
            memcpy(out, acc.data, acc.len * sizeof(char *));
            c->wisdom = out;
            c->n_wisdom = (int)acc.len;
        }
    }
    jc_vec_free(&acc);
}

/* Print one dim proverb line (rotating) before the idle prompt. */
static void print_wisdom(struct tui_ctx *c)
{
    const char *line;
    if (!c->show_wisdom || c->n_wisdom <= 0) {
        return;
    }
    /* M574: gated HERE rather than where show_wisdom is computed, for two
     * reasons: `/accessible on` can be typed mid-session (so the decision must
     * be re-made each time), and show_wisdom is initialised BEFORE
     * ctx.accessible is assigned, so an init-time test would read a stale
     * zero. */
    if (c->accessible && !c->wisdom_explicit) {
        return;
    }
    line = c->wisdom[c->wisdom_idx % c->n_wisdom];
    c->wisdom_idx++;
    if (line == NULL) {
        return;
    }
    if (c->color) {
        printf(C_DIM "  %s" C_RESET "\n", line);
    } else {
        printf("  %s\n", line);
    }
}

int jc_tui_run(struct jc_app *app)
{
    struct jc_term term;
    struct jc_session session;
    struct jc_history *hist;
    struct jc_agent_callbacks cb;
    struct tui_ctx ctx;
    struct jc_ask_delegate ask;
    char prompt[256];
    int consecutive_intr = 0; /* back-to-back empty-prompt Ctrl-C (M107) */

    jc_term_init(&term);
    /* Zero first: several fields (the spinner state, the parallel board, the
     * prompt-cache totals cb_usage accumulates into) were only ever assigned
     * on their first use, so they were read from an indeterminate stack. The
     * M254 type-ahead fields join them, and one memset covers the lot. */
    memset(&ctx, 0, sizeof(ctx));
    jc_sb_init(&ctx.voice_buf);          /* M303 */
    jc_sb_init(&ctx.acc_buf);            /* M549 */
    ctx.app = app;
    ctx.term = &term;
    ctx.color = jc_color_enabled(app->color_mode, term.is_tty);
    ctx.unicode = jc_locale_is_utf8();
    ctx.accessible = app->config.accessible; /* M118: reduce motion */
    /* M137 picked the UI message language HERE, which made it a TUI decision.
     * M566 moved it to main.c, before either front-end starts, because the
     * headless path needs the same answer and had no way to get it: nobody
     * called jc_msg_set_lang on that route, so a headless run served English
     * whatever $JICHI_LANG said. The `/language` command below re-resolves at
     * runtime, which is still this file's job. */
    ctx.markdown = ctx.color && app->config.markdown;
    ctx.quiet = app->quiet;
    ctx.nested_bol = 1;
    ctx.at_bol = 1;
    /* M254: type-ahead needs a real terminal to hold. Config `typeAhead` /
     * --type-ahead opts IN -- off by default since M257, because jichi cannot
     * promise the typing is visible in every window and input you cannot see is
     * input you cannot correct (docs/TYPE_AHEAD.md D1). */
    ctx.type_ahead = term.is_tty && app->config.type_ahead;
    jc_sb_init(&ctx.qbuf);
    jc_sb_init(&ctx.qpend);
    /* M257: the working line is the only safe host for the type-ahead echo, and
     * it used to exist only when colour did -- so a NO_COLOR terminal typed
     * blind for the whole turn. Its MECHANISM is CR + erase-line, which is
     * cursor control, not styling: the line editor on this very fd already
     * emits \r\x1b[J and \x1b[<n>C with NO_COLOR set. So existence now depends
     * on having something to show, and only the SGR codes depend on colour.
     * Kept narrow deliberately: without type-ahead a NO_COLOR session still
     * gets exactly the output it got before. --accessible keeps its static line
     * (M118) -- repainting per keystroke would spam a screen reader. */
    ctx.indicator = !ctx.accessible &&
                    (ctx.color || (ctx.type_ahead && term.is_tty));
    /* The TUI is the one front-end that displays nested subagent activity; quiet
     * collapses it back to a start banner + result. (Headless/ACP leave this 0.) */
    app->stream_subagents = !app->quiet;
    jc_mdr_init(&ctx.md, ctx.color);
    jc_vec_init(&ctx.always, sizeof(char *));
    ctx.tot_in = 0.0;
    ctx.tot_out = 0.0;
    ctx.last_in = 0.0;
    ctx.last_out = 0.0;
    tui_load_wisdom(&ctx); /* #11: idle proverbs from wisdom.json */
    jc_term_set_completer(&term, tui_complete, &ctx);
    jc_term_set_suggester(&term, tui_suggest, &ctx);
    jc_term_set_adviser(&term, tui_advise, &ctx);
    /* M578: set once here and refreshed in the loop, because `/accessible on`
     * can be typed mid-session and the decision must follow it. */
    jc_term_set_suggest_announce(&term,
        ctx.accessible ? jc_msg(JC_MSG_SUGGESTION) : NULL);
    /* M362: accessible mode also switches the line editor to incremental
     * echo (append/erase one cell) instead of the full wrap-aware redraw --
     * the redraw is one ESC[J + ~39 bytes per keystroke, which a screen
     * reader re-announces as a changed line on every character typed. */

    {
        enum jc_session_open_result r = jc_session_open(&session,
            app->session_id, app->resume,
            app->session_all ? NULL : app->cwd, app->cwd, app->arena);
        if (r == JC_SESSION_AMBIGUOUS || r == JC_SESSION_NONE) {
            printf("no %ssession matching '%s'; starting fresh.\n",
                   r == JC_SESSION_AMBIGUOUS ? "unambiguous " : "",
                   app->session_id ? app->session_id : "");
            jc_session_new(&session, app->cwd, app->arena);
        } else if (r == JC_SESSION_OPENED) {
            printf("(resumed: %s)\n", session.title ? session.title : session.id);
            tui_note_drift(&session, &session.history);
            if (!app->mode_pinned) {
                jc_app_set_mode(app, session.mode);
            }
        }
    }
    hist = &session.history;
    app->todos = &session.todos; /* M606: the list lives with the session */

    memset(&cb, 0, sizeof(cb));
    cb.on_assistant_text = cb_text;
    cb.on_message_begin = cb_message_begin;
    cb.on_message_end = cb_message_end;
    cb.on_tool_start = cb_tool_start;
    cb.on_tool_result = cb_tool_result;
    cb.on_usage = cb_usage;
    cb.on_status = cb_status;
    cb.on_progress = cb_progress;
    /* M254: the keyboard's mid-turn channel. Always installed, like
     * confirm_tool above -- the runtime flag inside tui_take_input decides
     * whether it yields anything, so /typeahead takes effect immediately (M258).
     * Installing it conditionally at startup meant `/typeahead on` enabled
     * capture but not injection, and the queued line silently fell through to
     * the next prompt instead of steering the running turn. */
    cb.take_input = tui_take_input;
    /* Always install the prompt; the permission verdict (resolved per call from
     * the mode + policy) decides whether it is actually invoked, so /auto and
     * /plan take effect immediately. */
    cb.confirm_tool = cb_confirm;
    cb.confirm_privileged = cb_confirm_privileged; /* M153: fresh, always-proof */
    cb.confirm_kinetic = cb_confirm_kinetic;       /* M163a: fresh, always-proof */
    cb.user = &ctx;

    /* Install the ask_user front-end delegate so the agent can pose a blocking
     * clarifying question (F4); headless/ACP leave app->ask NULL. */
    ask.ask = tui_ask;
    ask.ctx = &ctx;
    app->ask = &ask;

    if (term.is_tty) {
        banner(ctx.color);
        printf("(mode: %s)\n\n",
               jc_agent_mode_name((enum jc_agent_mode)app->mode));
    }

    for (;;) {
        char *line = NULL;
        jc_read_result rr;
        const char *cmd_agent = NULL; /* a command's `agent:`, or NULL */
        const char *cmd_model = NULL; /* a command's `model:`, or NULL */
        const char *cmd_output = NULL;/* a command's `output:`, or NULL */
        const char *cmd_language = NULL; /* a command's `language:` (M597) */
        int cmd_subtask = 0;          /* a command's `subtask:` flag    */
        struct jc_command_agent_save agent_save;
        struct jc_command_model_save model_save;
        const char *mname = jc_agent_mode_name((enum jc_agent_mode)app->mode);
        /* M296 deliberately does NOT put the full wire id here. The prompt is
         * rebuilt once per turn, BEFORE the turn runs, while routing mutates the
         * active model DURING one -- so after a turn that escalated, the prompt
         * drawn next reads `strong` and jc_agent_run_turn immediately re-routes to
         * `fast` for that very turn. This segment cannot be authoritative once
         * routing is live, and a full id here would make a sometimes-stale value
         * look precise (~22 columns on every line, at that). The reply header is
         * per model call, so it is the honest place; see cb_message_begin.
         *
         * It does fall back to the id when there is no config `name`, because
         * jc_model_short_name(NULL) is "" and the segment rendered EMPTY. */
        const char *model = jc_model_short_name(
            app->config.model.name != NULL ? app->config.model.name
                                           : app->config.model.model);
        const char *sep = ctx.unicode ? "\xc2\xb7" : ":";   /* middot or ':' */
        const char *arrow = ctx.unicode ? "\xe2\x80\xba" : ">";
        char status[80];

        /* M146: SIGTERM asked for session end, not just a turn abort --
         * leave the REPL gracefully (session saved, teardown runs). */
        if (app->term_flag) {
            break;
        }

        /* Live status segment: context-budget % (amber/red as it fills) and the
         * session's running cost, so the user sees both without /context //cost.
         * Built into `status` and spliced into the prompt below. */
        {
            /* M536: the SAME quantity the 80% compaction trigger and the
             * 75%/55% routing thresholds evaluate -- history plus the measured
             * system prompt and tool schemas. This badge used to pass only the
             * history, so it read comfortably grey while compaction fired and
             * disagreed with the /context line printed right beside it. */
            long used = jc_compact_effective_est(app, hist);
            long limit = jc_compact_context_limit(app);
            int pct = (limit > 0) ? (int)(used * 100 / limit) : 0;
            double cost = jc_config_cost(&app->config.model, ctx.tot_in,
                                         ctx.tot_out, ctx.tot_cache_read,
                                         ctx.tot_cache_write);
            if (pct > 100) pct = 100;
            if (ctx.accessible) {
                /* M562 (stage A7): the PROMPT, which is the most-repeated string
                 * in a session -- it is redrawn on every turn and, before M558,
                 * on every keystroke.
                 *
                 * `[chat.qwen3-coder-next.2%] >` costs a listener five spoken
                 * symbols (two brackets, two separators, a percent) plus a long
                 * model name, every single time. At Orca's punctuation level
                 * SOME every one of those is voiced -- which is M559's finding,
                 * and this line is the largest remaining instance of it.
                 *
                 * WHAT IS KEPT AND WHY, each on its own reason:
                 *  - the MODE stays. It is the one segment that changes what a
                 *    keypress DOES: `auto` acts without asking.
                 *  - the PERCENTAGE appears only at the threshold where it
                 *    changes something. 80% is where compaction fires and
                 *    rewrites the conversation, and it is the same figure the
                 *    red badge uses for a sighted user -- so this is the
                 *    existing policy expressed for a listener, not a new one.
                 *    At 6% it is not news.
                 *  - the MODEL goes. M296 added it because the TUI named the
                 *    tier and never the model, and that intent is satisfied
                 *    better in accessible mode: `Model X responds with the
                 *    following:` names it on EVERY reply (M553), where the
                 *    prompt named it before every keystroke.
                 *  - the COST goes. `$` is a spoken symbol, it is per-keystroke
                 *    chrome, and `/cost` answers it on request.
                 *
                 * `percent` is spelled out rather than `%` for the same reason
                 * the brackets went. */
                if (pct >= 80) {
                    jc_snprintf(status, sizeof(status),
                                ", %d percent full", pct);
                } else {
                    status[0] = '\0';
                }
            } else if (ctx.color) {
                const char *pc = (pct >= 80) ? C_RED
                               : (pct >= 60) ? C_YELLOW : C_DIM;
                if (cost > 0.0) {
                    jc_snprintf(status, sizeof(status),
                                C_DIM "%s" C_RESET "%s%d%%" C_RESET
                                C_DIM " %s $%.2f" C_RESET, sep, pc, pct, sep,
                                cost);
                } else {
                    jc_snprintf(status, sizeof(status),
                                C_DIM "%s" C_RESET "%s%d%%" C_RESET, sep, pc,
                                pct);
                }
            } else {
                if (cost > 0.0) {
                    jc_snprintf(status, sizeof(status), "%s%d%% %s $%.2f",
                                sep, pct, sep, cost);
                } else {
                    jc_snprintf(status, sizeof(status), "%s%d%%", sep, pct);
                }
            }
        }

        /* #11: a rotating proverb/affirmation in the idle moment before input. */
        print_wisdom(&ctx);

        /* M122: a compact indicator line for noteworthy session state not already
         * in the prompt -- active constraints, background jobs, open todos. Shown
         * (dim) only when there is something to show. */
        {
            char ind[128];
            int nbg = 0;
            if (app->bg != NULL) {
                int bi;
                for (bi = 0; bi < JC_BG_MAX; bi++) {
                    if (app->bg->procs[bi].id != 0) nbg++;
                }
            }
            if (jc_tui_indicator(app->n_constraints, nbg,
                                 app->todos != NULL
                                     ? (int)app->todos->items.len : 0,
                                 ctx.unicode,
                                 ind, sizeof ind) > 0) {
                if (ctx.color) put(C_DIM);
                printf("  %s\n", ind);
                if (ctx.color) put(C_RESET);
            }
        }

        /* Rebuild the prompt each turn so it reflects the current mode/model.
         * The mode is shown in its own colour (matching the reply header), the
         * brackets/separator/model in cyan, then the dim live status segment. */
        if (ctx.accessible) {
            /* M562: mode, the threshold notice when there is one, and the arrow.
             * The arrow stays because it is the readiness cue -- a listener needs
             * to know jichi is waiting -- and it is one symbol against the five
             * removed. Whether even that earns its place is a listening
             * question, not one I can answer from here. */
            jc_snprintf(prompt, sizeof(prompt), "%s%s %s ",
                        mname, status, arrow);
        } else if (ctx.color) {
            jc_snprintf(prompt, sizeof(prompt),
                        C_CYAN "[" C_RESET "%s%s" C_RESET
                        C_CYAN "%s%s" C_RESET "%s" C_CYAN "]" C_RESET
                        C_GREEN " %s" C_RESET " ",
                        jc_mode_color(app->mode), mname, sep, model, status,
                        arrow);
        } else {
            jc_snprintf(prompt, sizeof(prompt), "[%s%s%s%s] %s ",
                        mname, sep, model, status, arrow);
        }

        app->abort_flag = 0;
        /* M578: re-decided every prompt -- `/accessible` toggles at runtime and
         * the language can change under `/language`, so both inputs to this are
         * live. jc_msg returns a static catalog pointer, which satisfies the
         * setter's "must outlive the readline call". */
        jc_term_set_suggest_announce(&term,
            ctx.accessible ? jc_msg(JC_MSG_SUGGESTION) : NULL);
        /* M254, the queue-ahead half: a line committed while the previous turn
         * ran, but after its last tool boundary -- or during a turn that made
         * no tool calls at all, so there was no boundary -- becomes the next
         * message instead of being dropped. Echoed behind the prompt exactly
         * as a typed line would appear, so the transcript reads the same. */
        line = tui_take_input(&ctx);
        if (line != NULL) {
            printf("%s%s\n", prompt, line);
            fflush(stdout);
            /* It never passed through the line editor, so record it for recall
             * here -- a queued message must behave like a typed one. */
            jc_term_history_add(&term, line);
            rr = JC_READ_LINE;
        } else {
            rr = jc_term_readline(&term, prompt, &line);
        }
        if (rr == JC_READ_EOF) {
            /* Ctrl-D on an empty line: exit (standard). */
            if (term.is_tty) {
                put("\n");
            }
            break;
        }
        if (rr == JC_READ_INTR) {
            /* Ctrl-C at the prompt STOPS, it does not exit (M107): the first one
             * cancels and stays; a second consecutive one quits. Mid-turn Ctrl-C
             * already aborts the turn (SIGINT -> abort_flag) and returns here. */
            consecutive_intr++;
            if (jc_interrupt_should_exit(consecutive_intr)) {
                if (term.is_tty) {
                    put("\n");
                }
                break;
            }
            if (term.is_tty) {
                put("\n" C_DIM
                    "  (interrupted — press Ctrl-C again or type /exit to quit)"
                    C_RESET "\n");
            }
            free(line);
            continue;
        }
        consecutive_intr = 0; /* any non-interrupt read resets the counter */
        if (line == NULL) {
            continue;
        }
        if (line[0] == '\0') {
            free(line);
            continue;
        }
        /* M550: a leading blank must not turn a COMMAND into a PROMPT.
         *
         * Every dispatch below is a strcmp or a `line[0] == '/'`, so " /markdown"
         * matched nothing and was sent to the model as a prompt. The operator hit
         * it through a screen reader and it cost two full round-trips -- and
         * because "/markdown" is meaningless as a message, the model RE-ANSWERED
         * the previous question, so a listener sat through the whole reply twice
         * (~280 output tokens each, from the transcript).
         *
         * The sharp end is `/exit`: " /exit" did not quit, it became a prompt. The
         * documented way out of the program silently stopped working because of a
         * character that occupies no visual space -- and a listener cannot see the
         * space at all. That is the same shape as the TTY trap recorded in
         * ANECDOTES #66: the escape route failing without saying so.
         *
         * Only lines whose first non-blank byte is '/' are rewritten, so a PROMPT
         * that legitimately begins with whitespace is untouched. Trailing blanks go
         * too, since " /markdown " is equally invisible and equally broken. */
        {
            char *cs = line;
            while (*cs == ' ' || *cs == '\t') {
                cs++;
            }
            if (*cs == '/') {
                jc_size cl;
                if (cs != line) {
                    memmove(line, cs, strlen(cs) + 1);
                }
                cl = (jc_size)strlen(line);
                while (cl > 0 && (line[cl - 1] == ' ' || line[cl - 1] == '\t')) {
                    line[--cl] = '\0';
                }
            }
        }
        if (strcmp(line, "/exit") == 0 || strcmp(line, "/quit") == 0) {
            free(line);
            break;
        }
        if (strcmp(line, "/help") == 0) {
            help(app);
            free(line);
            continue;
        }
        /* Undocumented hidden gems (M109): handled before the model sees them. */
        if (line[0] == '/' && easter_egg(&ctx, line)) {
            free(line);
            continue;
        }
        if (strcmp(line, "/clear") == 0) {
            jc_history_free(hist);
            jc_history_init(hist);
            /* M606: the task list is this session's state too; a cleared
             * conversation with yesterday's plan still attached would be the
             * split M606 exists to remove. */
            if (app->todos != NULL) {
                jc_todo_clear(app->todos);
            }
            put("(history and task list cleared)\n");
            free(line);
            continue;
        }
        if (strcmp(line, "/mcp") == 0) {
            if (app->mcp == NULL || jc_mcp_server_count(app->mcp) == 0) {
                put("(no MCP servers connected; add an \"mcpServers\" array "
                    "to your config)\n");
            } else {
                int n = jc_mcp_server_count(app->mcp);
                int total = jc_mcp_tool_count(app->mcp);
                int k;
                for (k = 0; k < n; k++) {
                    int tc = 0;
                    const char *nm = jc_mcp_server_name(app->mcp, k, &tc);
                    printf("  %s  (%d tool%s)\n", nm ? nm : "?", tc,
                           tc == 1 ? "" : "s");
                    if (nm != NULL) {
                        char pfx[128];
                        jc_size pl;
                        int j;
                        jc_snprintf(pfx, sizeof(pfx), "%s__", nm);
                        pl = (jc_size)strlen(pfx);
                        for (j = 0; j < total; j++) {
                            const char *fq =
                                jc_mcp_tool_at(app->mcp, j, NULL, NULL);
                            if (fq != NULL && strncmp(fq, pfx, pl) == 0) {
                                printf("      %s\n", fq + pl);
                            }
                        }
                    }
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/undo") == 0) {
            const char *what = NULL;
            struct jc_sb names;
            jc_status ust;
            /* M349: capture what the restore is about to touch BEFORE the
             * undo pops the checkpoint it diffs against. */
            jc_sb_init(&names);
            (void)jc_snapshot_undo_changes(app->snapshots, &names);
            ust = jc_snapshot_undo(app->snapshots, &what);
            if (ust == JC_OK) {
                if (what != NULL && what[0] != '\0') {
                    printf("(reverted: %s)\n", what);
                } else {
                    put("(workspace reverted to last checkpoint)\n");
                }
                /* M337b: there is no /redo, so say where the old state went. */
                if (jc_snapshot_preserved_last(app->snapshots) != NULL) {
                    printf("(preserved at %.12s -- `jichi recover %.12s"
                           " --into <dir>`)\n",
                           jc_snapshot_preserved_last(app->snapshots),
                           jc_snapshot_preserved_last(app->snapshots));
                }
                /* M349: the human just saw all of the above; the MODEL saw
                 * nothing, and its history still contains tool results
                 * describing the files this restore reverted. One user-role
                 * note (the control-inject shape, cache-safe) corrects the
                 * record; saved immediately so a resume cannot revive the
                 * stale beliefs without the correction beside them. Empty
                 * diff => no note: nothing became stale. */
                if (names.data != NULL && names.len > 0) {
                    struct jc_sb note;
                    jc_sb_init(&note);
                    jc_snapshot_undo_note(what, names.data, &note);
                    if (note.len > 0 && note.data != NULL) {
                        jc_history_add(hist, JC_ROLE_USER, note.data);
                        jc_session_save(&session);
                        put("(noted for the model: earlier reads of the "
                            "reverted files are stale)\n");
                    }
                    jc_sb_free(&note);
                }
            } else if (!jc_snapshot_available(app->snapshots)) {
                put("(snapshots unavailable; need git and \"snapshots\": true)\n");
            } else {
                put("(nothing to undo)\n");
            }
            jc_sb_free(&names);
            free(line);
            continue;
        }
        if (strncmp(line, "/rewind", 7) == 0 &&
            (line[7] == '\0' || line[7] == ' ')) {
            const char *arg = line + 7;
            int dry = (strstr(line, "--dry-run") != NULL);
            int nc = jc_snapshot_count(app->snapshots);
            int n = 0;
            while (*arg == ' ') arg++;
            if (*arg >= '0' && *arg <= '9') {
                n = atoi(arg);
            }
            if (!jc_snapshot_available(app->snapshots)) {
                put("(snapshots unavailable; need git and \"snapshots\": true)\n");
            } else if (nc == 0) {
                put("(no checkpoints to rewind to)\n");
            } else if (n < 1) {
                /* Bare /rewind: list the checkpoints as rewind targets. */
                int ci;
                put("rewind to which checkpoint? (/rewind <n> [--dry-run])\n");
                for (ci = 0; ci < nc; ci++) {
                    const char *lbl =
                        jc_snapshot_label(app->snapshots, nc - 1 - ci);
                    printf("  %d. %s\n", ci + 1, lbl != NULL ? lbl : "");
                }
            } else {
                int cut = jc_snapshot_rewind_cut(app->snapshots, hist, n);
                if (cut < 0) {
                    printf("no checkpoint %d, or its turn isn't in this "
                           "session (see /checkpoints)\n", n);
                } else {
                    int dropped = (int)jc_history_len(hist) - cut;
                    const char *lbl = NULL;
                    if (dry) {
                        struct jc_sb pv;
                        jc_sb_init(&pv);
                        jc_snapshot_preview_index(app->snapshots, n, &pv, &lbl);
                        printf("Dry run - /rewind %d would restore files to "
                               "checkpoint %d%s%s and drop %d message%s:\n", n,
                               n, (lbl && lbl[0]) ? ": " : "",
                               (lbl && lbl[0]) ? lbl : "",
                               dropped, dropped == 1 ? "" : "s");
                        if (pv.len > 0) print_git_diff(&ctx, pv.data);
                        put("(nothing changed; drop --dry-run to apply)\n");
                        jc_sb_free(&pv);
                    } else if (jc_snapshot_restore_index(app->snapshots, n,
                                                         &lbl) == JC_OK) {
                        jc_history_truncate(hist, (jc_size)cut);
                        jc_session_save(&session);
                        printf("(rewound to checkpoint %d%s%s; dropped %d "
                               "message%s)\n", n, (lbl && lbl[0]) ? ": " : "",
                               (lbl && lbl[0]) ? lbl : "",
                               dropped, dropped == 1 ? "" : "s");
                    } else {
                        printf("rewind: failed to restore checkpoint %d\n", n);
                    }
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/diff") == 0) {
            struct jc_sb sb;
            jc_status dst;
            jc_sb_init(&sb);
            dst = jc_snapshot_diff(app->snapshots, 1, &sb);
            if (dst == JC_OK && sb.len > 0) {
                print_git_diff(&ctx, sb.data);
            } else if (!jc_snapshot_available(app->snapshots)) {
                put("(snapshots unavailable; need git and \"snapshots\": true)\n");
            } else {
                put("(no changes since the last checkpoint)\n");
            }
            jc_sb_free(&sb);
            free(line);
            continue;
        }
        if (strcmp(line, "/markdown") == 0 ||
            strncmp(line, "/markdown ", 10) == 0) {
            const char *arg = line[9] == ' ' ? line + 10 : "";
            if (strcmp(arg, "on") == 0) {
                ctx.markdown = ctx.color;
                put(ctx.color ? "(markdown rendering on)\n"
                              : "(markdown needs color; it is disabled)\n");
            } else if (strcmp(arg, "off") == 0) {
                ctx.markdown = 0;
                put("(markdown rendering off)\n");
            } else {
                printf("markdown rendering: %s\n", ctx.markdown ? "on" : "off");
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/typeahead") == 0 ||
            strncmp(line, "/typeahead ", 11) == 0) {
            const char *arg = line[10] == ' ' ? line + 11 : "";
            if (strcmp(arg, "on") == 0) {
                if (!term.is_tty) {
                    put("(type-ahead needs a terminal)\n");
                } else {
                    ctx.type_ahead = 1;
                    put("(type-ahead on: type while I work, Enter queues, "
                        "Ctrl-K un-queues)\n");
                    put("  note: while a tool runs or prose streams your typing "
                        "is captured but not shown\n");
                }
            } else if (strcmp(arg, "off") == 0) {
                ctx.type_ahead = 0;
                /* Anything already queued would otherwise still be applied on
                 * the next turn, which is not what "off" reads like. */
                if (ctx.qpend.len > 0) {
                    jc_sb_free(&ctx.qpend);
                    jc_sb_init(&ctx.qpend);
                    put("(type-ahead off; queued input dropped)\n");
                } else {
                    put("(type-ahead off)\n");
                }
            } else {
                printf("type-ahead: %s (see docs/TYPE_AHEAD.md)\n",
                       ctx.type_ahead ? "on" : "off");
            }
            /* The working line is the echo's only host, so whether it may be
             * drawn depends on this setting (M257). */
            ctx.indicator = !ctx.accessible &&
                            (ctx.color || (ctx.type_ahead && term.is_tty));
            free(line);
            continue;
        }
        if (strcmp(line, "/wisdom") == 0 ||
            strncmp(line, "/wisdom ", 8) == 0) {
            const char *arg = line[7] == ' ' ? line + 8 : "";
            if (strcmp(arg, "on") == 0) {
                if (ctx.n_wisdom == 0) {
                    tui_load_wisdom(&ctx); /* pick up a freshly written file */
                }
                ctx.show_wisdom = 1;
                ctx.wisdom_explicit = 1;   /* M574: asked for, so honour it */
                printf("(idle proverbs on; %d loaded)\n", ctx.n_wisdom);
            } else if (strcmp(arg, "off") == 0) {
                ctx.show_wisdom = 0;
                put("(idle proverbs off)\n");
            } else if (strcmp(arg, "reload") == 0) {
                tui_load_wisdom(&ctx);
                printf("(reloaded; %d proverbs)\n", ctx.n_wisdom);
            } else {
                printf("idle proverbs: %s (%d loaded) -- /wisdom on|off|reload\n",
                       ctx.show_wisdom ? "on" : "off", ctx.n_wisdom);
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/cache") == 0 ||
            strncmp(line, "/cache ", 7) == 0) {
            const char *arg = line[6] == ' ' ? line + 7 : "";
            if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
                ctx.app->config.prompt_cache = (arg[1] == 'n') ? 1 : 0;
                jc_config_resolve_prompt_cache(&ctx.app->config);
                printf("(prompt caching %s)\n",
                       ctx.app->config.model.prompt_cache ? "on" : "off");
            } else {
                double cached = ctx.last_cache_read;
                double total = ctx.last_in + cached;
                printf("prompt caching: %s\n",
                       ctx.app->config.model.prompt_cache ? "on" : "off");
                if (total > 0.0) {
                    char sep = chrome_group_sep(&ctx);
                    char si[40], scr[40], scw[40];
                    jc_group_num(ctx.last_in, sep, si, sizeof si);
                    jc_group_num(ctx.last_cache_read, sep, scr, sizeof scr);
                    jc_group_num(ctx.last_cache_write, sep, scw, sizeof scw);
                    printf("last call: in=%s cached read=%s write=%s "
                           "(hit-rate %.1f%%)\n",
                           si, scr, scw, cached * 100.0 / total);
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/autocontext") == 0 ||
            strncmp(line, "/autocontext ", 13) == 0) {
            const char *arg = line[12] == ' ' ? line + 13 : "";
            if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
                ctx.app->config.auto_context = (arg[1] == 'n') ? 1 : 0;
                printf("(auto-context %s)\n",
                       ctx.app->config.auto_context ? "on" : "off");
                if (ctx.app->config.auto_context &&
                    jc_app_model_for_role(ctx.app, JC_ROLE_EMBED) == NULL) {
                    put("  note: needs a model with role \"embed\" to retrieve\n");
                }
            } else {
                printf("auto-context: %s\n",
                       ctx.app->config.auto_context ? "on" : "off");
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/accessible") == 0 ||
            strncmp(line, "/accessible ", 12) == 0) {
            const char *arg = line[11] == ' ' ? line + 12 : "";
            if (strcmp(arg, "on") == 0) ctx.accessible = 1;
            else if (strcmp(arg, "off") == 0) ctx.accessible = 0;
            printf("(accessible mode %s -- reduced motion, no spinner)\n",
                   ctx.accessible ? "on" : "off");
            free(line);
            continue;
        }
        if (strcmp(line, "/quiet") == 0 ||
            strncmp(line, "/quiet ", 7) == 0) {
            const char *arg = line[6] == ' ' ? line + 7 : "";
            if (strcmp(arg, "on") == 0) {
                ctx.quiet = 1;
                put("(quiet mode on)\n");
            } else if (strcmp(arg, "off") == 0) {
                ctx.quiet = 0;
                put("(quiet mode off)\n");
            } else {
                printf("quiet mode: %s\n", ctx.quiet ? "on" : "off");
            }
            free(line);
            continue;
        }
        /* --- learner-support commands (M173b) --------------------------- */
        if (strcmp(line, "/assignments") == 0) {
            struct jc_vec names;
            /* M200: the listing reads progress.jsonl, the directory names AND
             * every assignment file's spec -- all of it printed and dropped. On
             * app->arena (freed only at process exit) each /assignments in a
             * long TUI session retained the lot again: the same shape as the
             * M197 /sessions bug, one command over. */
            struct jc_arena *la = jc_arena_new(0);
            char adir[1100];
            char solp[1300];
            char ppath[1160];
            char *progress = NULL;
            jc_size ai;
            int an = 0;
            jc_snprintf(adir, sizeof(adir), "%s/docs/assignments", app->cwd);
            jc_snprintf(ppath, sizeof(ppath), "%s/.jichi/progress.jsonl",
                        app->cwd);
            if (la == NULL) {
                put("(out of memory listing assignments)\n");
                free(line);
                continue;
            }
            if (jc_read_file(ppath, &progress, NULL, la) != JC_OK) {
                progress = NULL;
            }
            jc_vec_init(&names, sizeof(char *));
            jc_list_dir(adir, &names, la);
            if (names.len > 1) {
                qsort(names.data, (size_t)names.len, sizeof(char *),
                      tui_name_cmp);
            }
            for (ai = 0; ai < names.len; ai++) {
                const char *nm = *(char **)jc_vec_at(&names, ai);
                jc_size alen = (jc_size)strlen(nm);
                char *atext = NULL;
                struct jc_assign_spec aspec;
                struct jc_progress aprog;
                char row[512];
                if (alen < 4 || strcmp(nm + alen - 3, ".md") != 0) continue;
                if (alen >= 12 &&
                    strcmp(nm + alen - 12, ".solution.md") == 0) continue;
                if (strcmp(nm, "INDEX.md") == 0) continue;
                jc_snprintf(solp, sizeof(solp), "%s/%.*s.solution.md", adir,
                            (int)(alen - 3), nm);
                if (an == 0) {
                    jc_progress_row_header(row, sizeof(row));
                    printf(C_DIM "  %s" C_RESET "\n", row);
                }
                memset(&aspec, 0, sizeof(aspec));
                {
                    char ap[1300];
                    jc_snprintf(ap, sizeof(ap), "%s/%s", adir, nm);
                    if (jc_read_file(ap, &atext, NULL, la) == JC_OK) {
                        jc_assign_parse(atext, &aspec, la);
                    }
                }
                jc_progress_scan(progress, nm, &aprog);
                jc_progress_row(nm, aspec.phase, aspec.points,
                                jc_file_exists(solp), &aprog,
                                row, sizeof(row));
                printf("  %s\n", row);
                an++;
            }
            if (an == 0) {
                put("(no assignments under docs/assignments/ -- ask the agent "
                    "to write one with /assign, or copy one from the "
                    "curriculum)\n");
            } else {
                put(C_DIM "  load one with /assignment "
                    "docs/assignments/<name>.md" C_RESET "\n");
            }
            jc_vec_free(&names);
            jc_arena_free(la);
            free(line);
            continue;
        }
        if (strncmp(line, "/assignment", 11) == 0 &&
            (line[11] == '\0' || line[11] == ' ')) {
            const char *arg = line + 11;
            while (*arg == ' ') arg++;
            if (strcmp(arg, "off") == 0) {
                if (app->assignment != NULL) {
                    assignment_off(app);
                    put("(assignment closed -- normal session resumed)\n");
                } else {
                    put("(no active assignment)\n");
                }
            } else if (arg[0] == '\0') {
                if (app->assignment == NULL) {
                    put("(no active assignment; /assignment <spec.md> loads "
                        "one, /assignments lists them)\n");
                } else {
                    printf("Active: %s\n",
                           app->assignment->title != NULL
                               ? app->assignment->title : "(untitled)");
                    printf("  hints used: %d of %d   grade with /grade, close "
                           "with /assignment off\n",
                           app->hints_used, app->assignment->nhints);
                }
            } else {
                char *atext;
                if (jc_read_file(arg, &atext, NULL, app->arena) != JC_OK) {
                    printf("(could not read '%s')\n", arg);
                } else if (jc_assign_parse(atext, &g_assignment,
                                           app->arena) != JC_OK) {
                    printf("('%s' has no task body)\n", arg);
                } else {
                    char *brief;
                    app->assignment = &g_assignment;
                    app->assignment_tutor = 1;   /* the HUMAN is the learner */
                    app->hints_used = 0;
                    jc_snprintf(g_assignment_path,
                                sizeof(g_assignment_path), "%s", arg);
                    /* M536: the same two fields the headless paths arm, so a
                     * rung the tutor reveals is recorded exactly like one the
                     * CLI reveals. No worktree here -- app->cwd IS the real
                     * workspace, and it is what the grade at jc_progress_append
                     * below already uses. */
                    app->assignment_spec = g_assignment_path;
                    jc_snprintf(app->assignment_dir,
                                sizeof app->assignment_dir, "%s", app->cwd);
                    brief = jc_assign_render(&g_assignment, app->arena);
                    if (brief != NULL) {
                        printf("%s\n", brief);
                    }
                    printf("%d hint%s available (/hint) -- the model is now a "
                           "TUTOR for this task: it will guide, not solve. "
                           "/grade checks your work; /assignment off ends.\n",
                           g_assignment.nhints,
                           g_assignment.nhints == 1 ? "" : "s");
                    if (!app->config.assignments) {
                        put(C_DIM "  note: \"assignments\" is off in config, "
                            "so the model-side hint/help tools are not "
                            "registered; /hint and /grade still work"
                            C_RESET "\n");
                    }
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/hint") == 0) {
            if (app->assignment == NULL) {
                put("(no active assignment -- /assignment <spec.md> first)\n");
            } else if (app->assignment->nhints <= 0) {
                put("(this assignment carries no hints)\n");
            } else if (app->hints_used >= app->assignment->nhints) {
                printf("(no more hints -- you have seen all %d; /tutor "
                       "<question> can nudge, or work it through)\n",
                       app->assignment->nhints);
            } else {
                printf("Hint %d of %d:\n\n%s\n", app->hints_used + 1,
                       app->assignment->nhints,
                       app->assignment->hints[app->hints_used]);
                app->hints_used++;
                /* M614: record the pull, exactly like the CLI (M502) and the
                 * tool (M536). This surface -- the one a HUMAN types at, and
                 * per M319/M320 the ladder's real path, since models pull no
                 * hints -- was the one writer that recorded nothing, while
                 * /assignment's own comment claimed the arming existed so "a
                 * rung the tutor reveals is recorded exactly like one the CLI
                 * reveals". A write failure keeps the hint and says so. */
                if (app->assignment_spec != NULL &&
                    app->assignment_dir[0] != '\0' &&
                    jc_progress_hint_append(app->assignment_dir,
                                            app->assignment_spec,
                                            app->hints_used) != JC_OK) {
                    printf("(could not record the pull in "
                           ".jichi/hints.jsonl -- the hint stands, the "
                           "ladder record does not)\n");
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/grade") == 0) {
            if (app->assignment == NULL) {
                put("(no active assignment -- /assignment <spec.md> first)\n");
            } else {
                assignment_grade(app, ctx.color);
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/tutor", 6) == 0 &&
            (line[6] == '\0' || line[6] == ' ')) {
            const char *q = line + 6;
            while (*q == ' ') q++;
            if (q[0] == '\0') {
                put("usage: /tutor <question>  (a read-only helper answers "
                    "with a nudge, never the solution)\n");
            } else {
                char *answer = NULL;
                put(C_DIM "  asking the tutor..." C_RESET "\n");
                if (jc_tool_help_tutor(app, q, &answer) == JC_OK &&
                    answer != NULL && answer[0] != '\0') {
                    printf("%s\n", answer);
                } else {
                    put("(no tutor available -- check the model connection, or "
                        "re-read the task and try /hint)\n");
                }
                free(answer);
            }
            free(line);
            continue;
        }
        /* M292: `/learn analyze [path]` in the TUI. The report was CLI-only, so a
         * TUI user had to leave the session to see what their own logs said --
         * and `/learn` (the mentor) embeds it but shows only the mentor's
         * conclusions, not the evidence. Offline and instant: no model call.
         *
         * Log resolution, most specific first: an explicit path argument, then
         * the log THIS session is writing (jc_eventlog_path, M292 -- the most
         * useful default, since it is the run you are asking about), then the
         * configured `logging.path`. Nothing found is an actionable message, not
         * a silent empty report. */
        if (strncmp(line, "/learn analyze", 14) == 0 &&
            (line[14] == '\0' || line[14] == ' ')) {
            const char *arg = line + 14;
            const char *path = NULL;
            char *text = NULL;
            while (*arg == ' ') {
                arg++;
            }
            if (arg[0] != '\0') {
                path = arg;
            } else if (jc_eventlog_path(app->telemetry) != NULL) {
                path = jc_eventlog_path(app->telemetry);
            } else if (app->config.log_path != NULL &&
                       app->config.log_path[0] != '\0') {
                path = app->config.log_path;
            }
            if (path == NULL) {
                put("no telemetry log to analyse: this session is not logging "
                    "(set \"logging\": {\"level\": \"metrics\"} in the config, "
                    "or pass a path: /learn analyze <file.jsonl>)\n");
            } else if (jc_read_file(path, &text, NULL, jc_app_scratch(app))
                           != JC_OK || text == NULL) {
                printf("could not read %s\n", path);
            } else {
                struct jc_sb rep;
                jc_sb_init(&rep);
                /* Filter to THIS workspace: a shared telemetry dir mixes
                 * projects, and an unfiltered report would rank another
                 * project's problems as this one's (M56). */
                jc_learn_analyze_render(jc_app_scratch(app), text,
                                        app->root[0] != '\0' ? app->root
                                                              : app->cwd,
                                        &rep);
                printf("%s", rep.data != NULL ? rep.data : "");
                printf("\n(analysed %s -- propose lessons with /learn)\n", path);
                jc_sb_free(&rep);
            }
            free(line);
            continue;
        }
        /* M293: `/learn apply [--force]` in the TUI. `learn apply` was CLI-only
         * and jichi has no shell escape, so committing a reviewed draft meant
         * leaving the session -- and leaving it was not merely inconvenient:
         * jc_memory_add does not refresh app->memory, and a second process
         * cannot refresh this one at all, so a session went on serving notes
         * that a `## Corrections` section had already superseded. Applying here
         * reloads in place. Offline: no model call. */
        /* M294 shares this handler: `/learn corrections` is the same call with a
         * narrower mask, which is the whole point of jc_learn_apply taking one. */
        if ((strncmp(line, "/learn apply", 12) == 0 &&
             (line[12] == '\0' || line[12] == ' ')) ||
            (strncmp(line, "/learn corrections", 18) == 0 &&
             (line[18] == '\0' || line[18] == ' '))) {
            int only_corr = (strncmp(line, "/learn corrections", 18) == 0);
            const char *arg = line + (only_corr ? 18 : 12);
            unsigned sections = only_corr ? JC_LEARN_CORRECTIONS : JC_LEARN_ALL;
            int force = 0;
            int bad = 0;
            while (*arg == ' ') {
                arg++;
            }
            /* --force overwrites an existing SKILL.md, so require it explicitly
             * rather than inferring it from anything. It is meaningless for a
             * corrections-only run, so it is refused there rather than ignored:
             * silently accepting a flag that does nothing teaches the wrong
             * model of what the command does. */
            if (strcmp(arg, "--force") == 0 && !only_corr) {
                force = 1;
            } else if (arg[0] != '\0') {
                printf(only_corr ? "usage: /learn corrections  (no options; "
                                   "--force only affects skills)\n"
                                 : "usage: /learn apply [--force]\n");
                bad = 1;
            }
            if (!bad) {
                char draft[1100];
                struct jc_learn_apply_stats st;
                struct jc_sb detail;
                struct jc_sb summary;
                jc_learn_draft_path(app, draft, sizeof(draft));
                jc_sb_init(&detail);
                if (jc_learn_apply(app, sections, force, &st, &detail)
                        == JC_ERR_NOTFOUND) {
                    printf("no %s -- draft lessons first with /learn\n", draft);
                } else {
                    jc_sb_init(&summary);
                    jc_learn_apply_summary(&st, draft, &summary);
                    printf("%s", detail.data != NULL ? detail.data : "");
                    printf("%s", summary.data != NULL ? summary.data : "");
                    if (st.memory_added > 0 || st.corrections_applied > 0 ||
                        st.skills_added > 0) {
                        put("(this session now sees them -- no restart "
                            "needed)\n");
                    }
                    jc_sb_free(&summary);
                }
                jc_sb_free(&detail);
            }
            free(line);
            continue;
        }
        /* M303: /voice [on|off] -- and it must REFUSE LOUDLY. Turning voice on
         * without a TTS backend and then simply not speaking is the worst possible
         * behaviour for the users this feature exists for: silence is
         * indistinguishable from a hung session. So the reason is printed (and
         * would be spoken, if speaking were possible). */
        if (strncmp(line, "/voice", 6) == 0 &&
            (line[6] == '\0' || line[6] == ' ')) {
            const char *arg = line + 6;
            while (*arg == ' ') {
                arg++;
            }
            if (strcmp(arg, "off") == 0) {
                app->config.voice = 0;
                put("voice: off\n");
            } else if (arg[0] == '\0' || strcmp(arg, "on") == 0) {
                char why[320];
                if (jc_voice_available(app, why, sizeof why)) {
                    app->config.voice = 1;
                    put("voice: on -- replies, approval questions and errors "
                        "are spoken\n");
                    jc_voice_say(app, "Voice is on.");
                } else {
                    printf("voice: unavailable -- %s\n", why);
                    put("  (leaving it off: silence would be worse than this "
                        "message)\n");
                }
            } else {
                put("usage: /voice [on|off]\n");
            }
            free(line);
            continue;
        }
        /* M303: /listen [seconds] -- record, transcribe, and use it as the prompt.
         * A FIXED WINDOW, not "until you stop talking": record_audio takes a
         * `seconds` argument enforced by a timeout, and jichi has no silence
         * detection. Stated rather than hidden, because a user who expects
         * auto-stop will think it is broken. */
        if (strncmp(line, "/listen", 7) == 0 &&
            (line[7] == '\0' || line[7] == ' ')) {
            const char *arg = line + 7;
            int secs;
            while (*arg == ' ') {
                arg++;
            }
            secs = (arg[0] != '\0') ? atoi(arg) : 5;
            if (secs <= 0 || secs > 300) {
                put("usage: /listen [seconds]  (1-300, default 5)\n");
                free(line);
                continue;
            }
            {
                /* An EXPLICIT path, so the transcribe step is deterministic rather
                 * than parsing the path back out of a prose result. */
                const char *rel = ".jichi-listen.wav";
                char argsj[256];
                struct jc_tool_result res;
                char *spoken = NULL;

                memset(&res, 0, sizeof(res));
                jc_snprintf(argsj, sizeof(argsj),
                            "{\"seconds\":%d,\"path\":\"%s\"}", secs, rel);
                printf("listening for %d second%s (fixed window -- there is no "
                       "silence detection)...\n", secs, secs == 1 ? "" : "s");
                fflush(stdout);
                if (jc_tool_execute(app->tools, "record_audio", argsj, &res,
                                    app) != JC_OK || res.is_error) {
                    printf("listen: %s\n", res.content != NULL ? res.content
                           : "recording failed (is \"sound\": {\"record\": ...} "
                             "configured?)");
                    jc_tool_result_free(&res);
                    free(line);
                    continue;
                }
                jc_tool_result_free(&res);

                memset(&res, 0, sizeof(res));
                jc_snprintf(argsj, sizeof(argsj), "{\"path\":\"%s\"}", rel);
                if (jc_tool_execute(app->tools, "transcribe_audio", argsj, &res,
                                    app) != JC_OK || res.is_error ||
                    res.content == NULL || res.content[0] == '\0') {
                    printf("listen: %s\n", res.content != NULL ? res.content
                           : "transcription failed (is a model with the "
                             "\"transcribe\" role configured?)");
                    jc_tool_result_free(&res);
                    free(line);
                    continue;
                }
                spoken = jc_strdup(res.content);
                jc_tool_result_free(&res);
                if (spoken == NULL) {
                    free(line);
                    continue;
                }
                /* Echo it, and speak it back when voice is on: a user who cannot
                 * see the screen has no other way to catch a misheard prompt
                 * before it becomes a turn. */
                printf("heard: %s\n", spoken);
                if (app->config.voice) {
                    char back[400];
                    jc_snprintf(back, sizeof(back), "I heard: %s", spoken);
                    jc_voice_say(app, back);
                }
                /* Replace the command line with the transcript and fall through
                 * to the normal submit path, so a spoken turn is an ordinary turn
                 * -- refs, commands and history all behave identically. */
                free(line);
                line = spoken;
            }
        }
        if (strcmp(line, "/memory") == 0) {
            if (app->memory != NULL && app->memory[0] != '\0') {
                printf("%s\n", app->memory);
            } else {
                put("(no remembered notes; the agent saves them with the "
                    "remember tool, in .jichi/memory.md)\n");
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/packages", 9) == 0 &&
            (line[9] == '\0' || line[9] == ' ')) {
            const char *arg = line + 9;
            struct jc_sb psb;
            while (*arg == ' ') arg++;
            jc_sb_init(&psb);
            if (strncmp(arg, "recommend", 9) == 0 && app->provider != NULL) {
                char *ans;
                jc_packages_recommend_prompt(app->repo_map != NULL
                    ? app->repo_map : "(no repo map)", &psb);
                put(C_DIM "  asking the model for a recommendation..." C_RESET
                    "\n");
                ans = jc_oneshot(app->provider, NULL, psb.data, 60,
                                 &app->abort_flag);
                if (ans != NULL) { printf("%s\n", ans); free(ans); }
                else { put("(recommendation call failed)\n"); }
            } else {
                jc_packages_render_catalog(&psb);
                printf("%s", psb.data != NULL ? psb.data : "");
                if (strncmp(arg, "recommend", 9) == 0) {
                    put("\n(no model connected; showing the catalog)\n");
                }
            }
            jc_sb_free(&psb);
            free(line);
            continue;
        }
        if (strcmp(line, "/benchmark") == 0) {
            struct jc_confbench_facts facts;
            struct jc_confbench_report rep;
            struct jc_sb bsb;
            jc_app_confbench_facts(app, &facts);
            jc_confbench_score(&facts, &rep);
            jc_sb_init(&bsb);
            jc_confbench_render(&rep, ctx.color, ctx.unicode, &bsb);
            printf("%s", bsb.data != NULL ? bsb.data : "");
            jc_sb_free(&bsb);
            free(line);
            continue;
        }
        if (strncmp(line, "/config", 7) == 0 &&
            (line[7] == '\0' || line[7] == ' ')) {
            const char *arg = line + 7;
            const char *lvlname[3];
            lvlname[0] = "off"; lvlname[1] = "metrics"; lvlname[2] = "full";
            while (*arg == ' ') arg++;
            if (*arg == '\0' || strncmp(arg, "show", 4) == 0) {
                int lv = app->config.log_level;
                char mdisp[192];
                printf("Configuration:\n");
                /* M296: "(none)" was wrong as well as unhelpful -- a config with
                 * no `name` still has a model, it just has no intent label. */
                jc_model_display(app->config.model.name, app->config.model.model,
                                 mdisp, sizeof mdisp);
                printf("  model:      %s\n", mdisp);
                printf("  mode:       %s\n",
                       jc_agent_mode_name((enum jc_agent_mode)app->mode));
                printf("  telemetry:  %s\n",
                       (lv >= 0 && lv <= 2) ? lvlname[lv] : "off");
                printf("  snapshots:  %s\n", app->config.snapshots ? "on":"off");
                printf("  editable:   %s\n",
                       app->config.config_editable ? "on"
                       : "off (set configEditable:true to allow edits)");
                put(C_DIM "  (/config set <key> <value>, "
                    "/config telemetry off|metrics|full)" C_RESET "\n");
            } else if (!app->config.config_editable) {
                put("config editing is off; enable with configEditable:true in "
                    "your config (or --config-editable)\n");
            } else if (strncmp(arg, "telemetry", 9) == 0) {
                const char *v = arg + 9;
                char msg[512];
                while (*v == ' ') v++;
                if (strcmp(v, "on") == 0) v = "metrics";
                if (*v == '\0') {
                    put("usage: /config telemetry off|metrics|full\n");
                } else if (jc_configedit_apply(NULL, "logging", v,
                                               msg, sizeof msg) == JC_OK) {
                    printf("%s\n", msg);
                } else {
                    printf("%s\n", msg);
                }
            } else if (strncmp(arg, "set ", 4) == 0) {
                const char *rest = arg + 4;
                char key[128];
                jc_size k = 0;
                char msg[512];
                while (*rest == ' ') rest++;
                while (*rest != '\0' && *rest != ' ' && k < sizeof(key) - 1) {
                    key[k++] = *rest++;
                }
                key[k] = '\0';
                while (*rest == ' ') rest++;
                if (key[0] == '\0' || *rest == '\0') {
                    put("usage: /config set <key> <value>\n");
                } else {
                    jc_configedit_apply(NULL, key, rest, msg, sizeof msg);
                    printf("%s\n", msg);
                }
            } else {
                put("usage: /config [show|set <key> <value>|"
                    "telemetry <level>]\n");
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/constraints", 12) == 0 &&
            (line[12] == '\0' || line[12] == ' ')) {
            const char *arg = line + 12;
            while (*arg == ' ') arg++;
            if (strncmp(arg, "clear", 5) == 0) {
                jc_app_constraints_clear(app);
                put("(constraints cleared)\n");
            } else if (strncmp(arg, "add ", 4) == 0) {
                /* M169: typed by the operator, so it persists. */
                int added = jc_app_constraints_adopt(app, arg + 4, 1);
                if (added > 0) {
                    printf("(added %d constraint%s -- now enforced)\n",
                           added, added == 1 ? "" : "s");
                } else {
                    put("(no constraint recognized; phrase it as e.g. "
                        "\"do not run the build or tests\")\n");
                }
            } else {
                int i;
                if (app->n_constraints == 0) {
                    put("(no active constraints)\n");
                } else {
                    put("Active constraints (enforced every turn):\n");
                    for (i = 0; i < app->n_constraints; i++) {
                        /* M169: mark provenance. Both are enforced identically;
                         * only "saved" ones outlive the session, and an operator
                         * debugging a surprising refusal needs to know which
                         * kind they are looking at. */
                        const char *tag =
                            (app->constraints[i].origin == JC_CONSTRAINT_INFERRED)
                            ? "this session" : "saved";
                        printf("  - %s  [%s]\n", app->constraints[i].text != NULL
                               ? app->constraints[i].text : "(constraint)", tag);
                    }
                }
                put(C_DIM "  (/constraints add <text> saves one; constraints "
                    "inferred from a prompt last this session only; "
                    "/constraints clear; or edit .jichi/constraints.md)"
                    C_RESET "\n");
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/checkpoints") == 0) {
            int nc = jc_snapshot_count(app->snapshots);
            if (nc == 0) {
                put("(no checkpoints for this workspace)\n");
            } else {
                int ci;
                for (ci = 0; ci < nc; ci++) {
                    const char *lbl =
                        jc_snapshot_label(app->snapshots, nc - 1 - ci);
                    printf("  %d. %s\n", ci + 1, lbl != NULL ? lbl : "");
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/skills") == 0) {
            int ns = jc_skill_count(&app->skills);
            if (ns == 0) {
                put("(no skills; add .jichi/skills/<name>/SKILL.md files)\n");
            } else {
                int si;
                for (si = 0; si < ns; si++) {
                    const struct jc_skill *sk = jc_skill_at(&app->skills, si);
                    printf("  %s - %s\n", sk->name,
                           sk->description != NULL ? sk->description : "");
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/map") == 0) {
            /* M218: a rebuilt map is malloc-owned and freed here -- the old
             * jc_repomap_build call parked one full copy on the session arena
             * per /map invocation. */
            char *built = (app->repo_map != NULL) ? NULL
                                                  : jc_repomap_render(app);
            const char *m = (app->repo_map != NULL) ? app->repo_map : built;
            put(m != NULL ? m : "(no recognised source files)\n");
            free(built);
            free(line);
            continue;
        }
        if (strcmp(line, "/board") == 0) {
            /* #7: show the kanban board (reload from disk so external edits or
             * the board tool's writes are reflected). */
            struct jc_sb bsb;
            jc_board_free(&app->board);
            jc_board_init(&app->board);
            jc_board_load(&app->board, app->cwd);
            jc_sb_init(&bsb);
            jc_board_render(&app->board, &bsb);
            put(bsb.data != NULL ? bsb.data : "");
            jc_sb_free(&bsb);
            free(line);
            continue;
        }
        /* M462: set or clear the authoritative design doc mid-session. The
         * missing half of a feature you could otherwise only choose at launch
         * -- and the point of a design doc is that you reach for it when the
         * work turns out to be deeper than you thought, which is mid-session
         * by definition. */
        if (strncmp(line, "/design", 7) == 0 &&
            (line[7] == '\0' || line[7] == ' ')) {
            const char *arg = line + 7;
            while (*arg == ' ') {
                arg++;
            }
            if (*arg == '\0') {
                if (app->design == NULL || app->design[0] == '\0') {
                    put("no design doc loaded "
                        "(/design <file>, or `design: [...]` in the config)\n");
                } else {
                    char msg[128];
                    jc_snprintf(msg, sizeof(msg),
                                "design loaded: %lu bytes\n",
                                (unsigned long)strlen(app->design));
                    put(msg);
                }
            } else if (strcmp(arg, "off") == 0 || strcmp(arg, "none") == 0) {
                free(app->design);   /* M199: malloc-owned, one live copy */
                app->design = NULL;
                put("design cleared\n");
                put("note: the system prompt changed, so the next call "
                    "re-bills the cached prefix\n");
            } else {
                const char *one[1];
                one[0] = arg;
                /* jc_app_load_design frees the previous copy itself, so a
                 * failed load leaves the old design in place rather than
                 * silently dropping it -- which is the behaviour you want
                 * when you mistype a path mid-session. */
                jc_app_load_design(app, one, 1);
                if (app->design == NULL || app->design[0] == '\0') {
                    put("could not read that design doc; nothing loaded\n");
                } else {
                    char msg[128];
                    jc_snprintf(msg, sizeof(msg),
                                "design loaded: %lu bytes\n",
                                (unsigned long)strlen(app->design));
                    put(msg);
                    put("note: the system prompt changed, so the next call "
                        "re-bills the cached prefix\n");
                }
            }
            continue;
        }
        if (strncmp(line, "/output-style", 13) == 0 &&
            (line[13] == '\0' || line[13] == ' ')) {
            const char *arg = line + 13;
            while (*arg == ' ') {
                arg++;
            }
            if (*arg == '\0') {
                struct jc_sb sb;
                jc_sb_init(&sb);
                jc_output_style_render_list(&app->output_styles, &sb);
                if (sb.len == 0) {
                    put("(no output styles; add "
                        ".jichi/output-styles/<name>.md)\n");
                } else {
                    put(sb.data);
                }
                jc_sb_free(&sb);
            } else if (strcmp(arg, "off") == 0 || strcmp(arg, "none") == 0) {
                jc_output_style_set_active(&app->output_styles, NULL);
                put("output style cleared\n");
            } else if (jc_output_style_set_active(&app->output_styles, arg)) {
                printf("output style: %s\n", arg);
            } else {
                printf("no output style named '%s'\n", arg);
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/language", 9) == 0 &&
            (line[9] == '\0' || line[9] == ' ')) {
            const char *arg = line + 9;
            while (*arg == ' ') {
                arg++;
            }
            if (*arg == '\0') {
                if (app->config.language != NULL &&
                    app->config.language[0] != '\0') {
                    printf("answer language: %s\n", app->config.language);
                } else {
                    put("(no answer language set; /language <lang> to set, "
                        "e.g. /language Japanese)\n");
                }
            } else if (strcmp(arg, "off") == 0 || strcmp(arg, "none") == 0) {
                app->config.language = NULL;
                put("answer language cleared (model default)\n");
            } else {
                /* Session-lived: the system prompt is rebuilt each turn, so
                 * the change takes effect on the next message. */
                app->config.language = jc_arena_strdup(app->arena, arg);
                printf("answer language: %s\n", arg);
            }
            /* M137: the UI catalog follows the answer language (unless
             * $JICHI_LANG pins it). */
            jc_msg_set_lang(jc_msg_lang_resolve(app->config.language,
                                                getenv("JICHI_LANG"),
                                                getenv("LANG"), ctx.unicode));
            free(line);
            continue;
        }
        if (strcmp(line, "/verify") == 0) {
            const char *cmd = (app->env != NULL && app->env->verify_cmd != NULL)
                ? app->env->verify_cmd : app->config.verify;
            if (cmd == NULL) {
                put("(no verifier configured; set \"verify\" in config or "
                    "pass --verify)\n");
            } else {
                struct jc_sb vout;
                int code;
                jc_sb_init(&vout);
                printf("running: %s\n", cmd);
                code = jc_env_run_verify(cmd, app->cwd, &vout,
                                         &app->abort_flag, 0);
                if (vout.data != NULL && vout.data[0] != '\0') {
                    printf("%s", vout.data);
                    if (vout.data[vout.len - 1] != '\n') {
                        put("\n");
                    }
                }
                printf("[exit %d] %s\n", code, code == 0 ? "ok" : "FAILED");
                jc_sb_free(&vout);
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/status") == 0) {
            print_status(&ctx);
            free(line);
            continue;
        }
        if (strcmp(line, "/cost") == 0) {
            print_cost(&ctx);
            free(line);
            continue;
        }
        if (strcmp(line, "/timeouts") == 0) {
            long ct = 0, st = 0, rt = 0;
            jc_config_resolve_timeouts(&app->config, &app->config.model,
                                       &ct, &st, &rt);
            printf("model-call timeouts (active model):\n");
            printf("  connect: %lds\n", ct);
            if (st > 0) printf("  stall:   %lds\n", st);
            else        printf("  stall:   off\n");
            if (rt > 0) printf("  request: %lds\n", rt);
            else        printf("  request: off\n");
            printf("  escalate on stall: %s\n",
                   app->config.routing.escalate_on_stall ? "on" : "off");
            free(line);
            continue;
        }
        if (strncmp(line, "/sessions", 9) == 0 &&
            (line[9] == '\0' || line[9] == ' ')) {
            const char *arg = line + 9;
            while (*arg == ' ') arg++;
            if (strncmp(arg, "clear", 5) == 0 &&
                (arg[5] == '\0' || arg[5] == ' ')) {
                const char *what = arg + 5;
                while (*what == ' ') what++;
                clear_sessions(&ctx, session.id, what);
            } else {
                print_sessions(&ctx, strstr(line, "--all") != NULL);
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/resume", 7) == 0 &&
            (line[7] == '\0' || line[7] == ' ')) {
            const char *arg = line + 7;
            char resolved[64];
            while (*arg == ' ') arg++;
            {
                struct jc_session tmp;
                enum jc_session_open_result r;
                /* Save the current conversation before switching. */
                jc_session_autotitle(&session);
                session.mode = app->mode;
                jc_session_save(&session);
                if (*arg == '\0') {
                    /* bare /resume: the most recent OTHER session for this
                     * project (M108 #6), for ease of use.
                     *
                     * M198: excluding the live session is load-bearing. The
                     * jc_session_save above just set its mtime to now, so a
                     * plain newest-first lookup always returned the session the
                     * user is already in -- bare /resume was a silent no-op and
                     * the "(no earlier session)" branch below was unreachable
                     * whenever the live session belonged to this cwd. */
                    if (jc_session_load_recent_scoped_ex(app->cwd, session.id,
                                                         &tmp, app->arena)
                            != JC_OK) {
                        put("(no earlier session for this project to resume)\n");
                        free(line);
                        continue;
                    }
                    r = JC_SESSION_OPENED;
                } else if (arg[0] == '@' ||
                           (jc_session_resolve_alias(arg, resolved,
                                sizeof resolved, app->arena) == 0)) {
                    /* @alias, or a bare token that matches an alias (M108 #7):
                     * the alias wins; fall through to id-prefix otherwise.
                     * M197: for a bare token the condition above ALREADY
                     * resolved it into `resolved`, so only the '@'-prefixed
                     * form -- which short-circuits the condition -- still needs
                     * a lookup. This used to resolve twice, reading the whole
                     * session store one extra time per command. */
                    const char *al = (arg[0] == '@') ? arg + 1 : arg;
                    if (arg[0] == '@' &&
                        jc_session_resolve_alias(al, resolved, sizeof resolved,
                                                 app->arena) != 0) {
                        printf("no session with alias '%s'\n", al);
                        free(line);
                        continue;
                    }
                    r = jc_session_open(&tmp, resolved, 0, NULL, app->cwd,
                                        app->arena);
                } else {
                    r = jc_session_open(&tmp, arg, 0, NULL, app->cwd, app->arena);
                }
                if (r == JC_SESSION_OPENED) {
                    jc_session_free(&session);
                    session = tmp;
                    hist = &session.history;
                    app->todos = &session.todos; /* M606: not the old list */
                    if (!app->mode_pinned) jc_app_set_mode(app, session.mode);
                    printf("(resumed: %s)\n",
                           session.title ? session.title : session.id);
                    tui_note_drift(&session, hist);
                } else {
                    printf("no %ssession matching '%s'\n",
                           r == JC_SESSION_AMBIGUOUS ? "unambiguous " : "", arg);
                }
            }
            free(line);
            continue;
        }
        if (strcmp(line, "/fork") == 0) {
            /* Branch a new session from here: persist the current one, copy its
             * history into a fresh id, and switch to the fork. The original is
             * left intact on disk (resumable via /sessions). */
            struct jc_session fork;
            jc_session_autotitle(&session);
            session.mode = app->mode;
            jc_session_save(&session);
            if (jc_session_fork(&session, &fork, app->arena) == JC_OK) {
                jc_session_save(&fork);
                jc_session_free(&session);
                session = fork;
                hist = &session.history;
                app->todos = &session.todos; /* M606: the fork's own copy */
                printf("(forked to %.8s; the original is saved -- /sessions to "
                       "return)\n", session.id != NULL ? session.id : "?");
            } else {
                put("(fork failed)\n");
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/title", 6) == 0 &&
            (line[6] == '\0' || line[6] == ' ')) {
            const char *t = line + 6;
            while (*t == ' ') t++;
            if (*t == '\0') {
                printf("current title: %s\n",
                       session.title ? session.title : "(none)");
            } else {
                jc_session_set_title(&session, t);
                jc_session_save(&session);
                printf("(titled: %s)\n", session.title);
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/name", 5) == 0 &&
            (line[5] == '\0' || line[5] == ' ')) {
            const char *nm = line + 5;
            while (*nm == ' ') nm++;
            if (*nm == '\0') {
                printf("current quick-find name: %s\n"
                       "  usage: /name <alias>  (then: /resume @<alias>)\n",
                       session.alias ? session.alias : "(none)");
            } else if (!jc_session_alias_valid(nm)) {
                put("invalid alias (use up to 64 of [A-Za-z0-9._-], no spaces)\n");
            } else {
                jc_session_set_alias(&session, nm);
                jc_session_save(&session);
                printf("(named: @%s -- resume with /resume @%s)\n",
                       session.alias, session.alias);
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/export", 7) == 0 &&
            (line[7] == '\0' || line[7] == ' ')) {
            const char *arg = line + 7;
            int html = (strstr(line, "--html") != NULL);
            char dest[256];
            const char *path;
            struct jc_sb sb;
            while (*arg == ' ') arg++;
            /* An explicit path (the first non-flag token) wins; else derive one
             * from the session id under the cwd. */
            path = NULL;
            if (*arg != '\0' && strncmp(arg, "--html", 6) != 0) {
                path = arg;
            }
            if (path == NULL) {
                jc_snprintf(dest, sizeof(dest), "jichi-%.8s.%s",
                            session.id != NULL ? session.id : "session",
                            html ? "html" : "md");
                path = dest;
            }
            jc_session_autotitle(&session);
            jc_sb_init(&sb);
            jc_session_render(&session, html ? JC_EXPORT_HTML : JC_EXPORT_MD,
                              &sb);
            if (jc_write_file(path, sb.data != NULL ? sb.data : "",
                              sb.len) == JC_OK) {
                printf("(exported to %s)\n", path);
            } else {
                printf("error: could not write %s\n", path);
            }
            jc_sb_free(&sb);
            free(line);
            continue;
        }
        if (strcmp(line, "/review") == 0) {
            /* Toggle the self-review pass for this session (on <-> off). */
            int on = (app->config.self_review == 1) ||
                     (app->config.self_review != 0 && app->mode == JC_MODE_AUTO);
            app->config.self_review = on ? 0 : 1;
            printf("self-review %s\n",
                   app->config.self_review ? "on" : "off");
            free(line);
            continue;
        }
        if (strcmp(line, "/compact") == 0) {
            int did = 0;
            jc_compact_force(app, hist, &did);
            put(did ? "(history compacted)\n"
                    : "(nothing to compact yet)\n");
            free(line);
            continue;
        }
        if (strcmp(line, "/context") == 0 ||
            strncmp(line, "/context ", 9) == 0) {
            /* M317: the sub-views M313/M315 built for the CLI, reachable where
             * people actually work. `/context history` is BETTER here than the
             * subcommand: the TUI holds the live history, so there is no
             * one-turn save lag and it works before the first save. */
            const char *arg = line + 8;
            struct jc_sb sb;
            while (*arg == ' ') {
                arg++;
            }
            jc_sb_init(&sb);
            if (strcmp(arg, "tools") == 0) {
                /* Join the telemetry here too (M317): a TUI user who HAS
                 * logging on would otherwise be told to turn it on. Same
                 * loader as the subcommand, so the two cannot disagree. */
                struct jc_telemetry_summary ts;
                char tlabel[320];
                int have = jc_app_load_telemetry(app, &ts, tlabel,
                                                 sizeof(tlabel));
                jc_context_tools_report(app, have ? &ts : NULL,
                                        have ? tlabel : NULL, &sb);
                jc_telemetry_summary_free(&ts);
            } else if (strcmp(arg, "history") == 0) {
                jc_context_history_report(app, hist, "this session", &sb);
            } else if (*arg != '\0') {
                jc_sb_append_fmt(&sb, "unknown view '%s' "
                                 "(use /context, /context tools, or "
                                 "/context history)\n", arg);
            } else {
                jc_context_report(app, hist, &sb);
            }
            put(sb.data != NULL ? sb.data : "");
            jc_sb_free(&sb);
            free(line);
            continue;
        }
        if (strcmp(line, "/mode") == 0 || strncmp(line, "/mode ", 6) == 0) {
            const char *arg = line + 5;
            enum jc_agent_mode m;
            while (*arg == ' ') {
                arg++;
            }
            if (*arg == '\0') {
                printf("mode: %s\n",
                       jc_agent_mode_name((enum jc_agent_mode)app->mode));
            } else if (jc_agent_mode_parse(arg, &m)) {
                jc_app_set_mode(app, (int)m);
                printf("mode: %s\n", jc_agent_mode_name(m));
            } else {
                printf("unknown mode '%s' (use chat|plan|auto)\n", arg);
            }
            free(line);
            continue;
        }
        if (strncmp(line, "/route", 6) == 0 &&
            (line[6] == '\0' || line[6] == ' ')) {
            const char *arg = line + 6;
            while (*arg == ' ') {
                arg++;
            }
            if (*arg == '\0') {
                int f, s;
                int usable = jc_config_routing_resolve(&app->config, &f, &s);
                printf("routing: %s\n",
                       app->config.routing.enabled ? "on" : "off");
                printf("  fast:   %s\n", app->config.routing.fast != NULL
                       ? app->config.routing.fast : "(unset)");
                printf("  strong: %s\n", app->config.routing.strong != NULL
                       ? app->config.routing.strong : "(unset)");
                printf("  escalate: verify=%s error=%s stall=%s\n",
                       app->config.routing.escalate_on_verify ? "on" : "off",
                       app->config.routing.escalate_on_error ? "on" : "off",
                       app->config.routing.escalate_on_stall ? "on" : "off");
                if (app->config.routing.escalate_on_context > 0) {
                    printf("            context=%d%% of the fast window\n",
                           app->config.routing.escalate_on_context);
                } else {
                    printf("            context=off\n");
                }
                {
                    /* M296: the tier alone was especially unhelpful HERE -- this
                     * is the routing view, where knowing which of fast/strong is
                     * active only matters alongside which model that is. */
                    char mdisp[192];
                    jc_model_display(app->config.model.name,
                                     app->config.model.model,
                                     mdisp, sizeof mdisp);
                    printf("  active model: %s%s\n", mdisp,
                           usable ? "" : "  (inert: set distinct fast+strong)");
                }
            } else if (strcmp(arg, "on") == 0) {
                app->config.routing.enabled = 1;
                put("routing: on\n");
            } else if (strcmp(arg, "off") == 0) {
                app->config.routing.enabled = 0;
                put("routing: off\n");
            } else if (strcmp(arg, "stall on") == 0) {
                app->config.routing.escalate_on_stall = 1;
                put("routing: escalate on stall = on\n");
            } else if (strcmp(arg, "stall off") == 0) {
                app->config.routing.escalate_on_stall = 0;
                put("routing: escalate on stall = off\n");
            } else if (strcmp(arg, "context off") == 0) {
                app->config.routing.escalate_on_context = 0;
                put("routing: escalate on context pressure = off\n");
            } else if (strncmp(arg, "context ", 8) == 0) {
                /* M288: a percentage of the fast tier's window. */
                const char *v = arg + 8;
                char *end = NULL;
                long pct;
                while (*v == ' ') {
                    v++;
                }
                pct = strtol(v, &end, 10);
                if (end == v || pct < 1 || pct > 99) {
                    put("usage: /route context <1-99>|off\n");
                } else {
                    app->config.routing.escalate_on_context = (int)pct;
                    printf("routing: escalate at %ld%% of the fast tier's "
                           "window\n", pct);
                }
            } else if (strncmp(arg, "fast ", 5) == 0 ||
                       strncmp(arg, "strong ", 7) == 0) {
                int is_fast = (arg[0] == 'f');
                const char *sel = arg + (is_fast ? 5 : 7);
                unsigned rf;
                while (*sel == ' ') {
                    sel++;
                }
                rf = jc_config_role_flag(sel);
                if (jc_config_find_model(&app->config, sel) < 0 &&
                    (rf == 0u || jc_config_find_by_role(&app->config, rf) < 0)) {
                    printf("no model matching '%s'\n", sel);
                } else {
                    char *copy = jc_arena_strdup(app->arena, sel);
                    if (is_fast) {
                        app->config.routing.fast = copy;
                    } else {
                        app->config.routing.strong = copy;
                    }
                    app->config.routing.enabled = 1;
                    printf("routing %s = %s\n", is_fast ? "fast" : "strong",
                           sel);
                }
            } else {
                put("usage: /route [on|off | stall on|off | "
                    "context <1-99>|off | fast <model> | strong <model>]\n");
            }
            free(line);
            continue;
        }
        /* M304: /chat existed only as `/mode chat`, while /plan and /auto were
         * one word each -- so there was no short way BACK to the default posture,
         * and the ask_user hint promised a command that did not exist (caught by
         * M295's slash-command lint, which is exactly the class of promise it was
         * built to find). */
        if (strcmp(line, "/chat") == 0) {
            jc_app_set_mode(app, JC_MODE_CHAT);
            put("mode: chat (mutating tools ask first)\n");
            free(line);
            continue;
        }
        if (strcmp(line, "/plan") == 0) {
            jc_app_set_mode(app, JC_MODE_PLAN);
            put("mode: plan (read-only; the agent will propose a plan)\n");
            free(line);
            continue;
        }
        if (strcmp(line, "/plan off") == 0) {
            jc_app_set_mode(app, JC_MODE_CHAT);
            put("mode: chat\n");
            free(line);
            continue;
        }
        if (strcmp(line, "/auto") == 0) {
            jc_app_set_mode(app, app->mode == JC_MODE_AUTO
                                 ? JC_MODE_CHAT : JC_MODE_AUTO);
            printf("mode: %s\n",
                   jc_agent_mode_name((enum jc_agent_mode)app->mode));
            free(line);
            continue;
        }
        if (strncmp(line, "/model", 6) == 0 &&
            (line[6] == '\0' || line[6] == ' ')) {
            const char *arg = line + 6;
            while (*arg == ' ') {
                arg++;
            }
            if (*arg == '\0') {
                int n = jc_config_model_count(&app->config);
                int k;
                for (k = 0; k < n; k++) {
                    struct jc_model_cfg *m = jc_config_model_at(&app->config, k);
                    printf("%s %d) %s  [%s/%s]\n",
                           k == app->config.active ? "*" : " ", k + 1,
                           m->name ? m->name : "(unnamed)",
                           m->provider ? m->provider : "?",
                           m->model ? m->model : "?");
                }
                if (n <= 1) {
                    put("(only one model configured; add a \"models\" array "
                        "to switch)\n");
                }
            } else {
                int idx = jc_config_find_model(&app->config, arg);
                if (idx < 0) {
                    printf("no model matching '%s'\n", arg);
                } else if (jc_app_switch_model(app, idx) == JC_OK) {
                    printf("switched to %s (%s)\n",
                           app->config.model.name ? app->config.model.name
                                                   : app->config.model.model,
                           app->config.model.model);
                } else {
                    put("could not switch model\n");
                }
            }
            free(line);
            continue;
        }
        /* Custom slash command? (Built-ins above are matched first.) */
        if (line[0] == '/') {
            char cname[128];
            const char *rest = line + 1;
            const struct jc_command *cmd;
            jc_size cn = 0;
            while (rest[cn] != '\0' && rest[cn] != ' ' &&
                   cn < sizeof(cname) - 1) {
                cname[cn] = rest[cn];
                cn++;
            }
            cname[cn] = '\0';
            cmd = jc_command_find(&app->commands, cname);
            if (cmd != NULL) {
                const char *args_raw = rest + cn;
                char *expanded;
                while (*args_raw == ' ') {
                    args_raw++;
                }
                jc_command_expand(cmd, args_raw, app->cwd, jc_app_scratch(app),
                                  &expanded);
                cmd_agent = cmd->agent;     /* honor `agent:` below   */
                cmd_model = cmd->model;     /* honor `model:` below   */
                cmd_output = cmd->output;   /* honor `output:` below  */
                cmd_language = cmd->language; /* honor `language:` (M597) */
                cmd_subtask = cmd->subtask; /* honor `subtask:` below */
                if (ctx.color) put(C_DIM);
                printf("(running /%s%s%s)\n", cname,
                       (cmd_agent != NULL && cmd_agent[0] != '\0')
                           ? " via " : "",
                       (cmd_agent != NULL && cmd_agent[0] != '\0')
                           ? cmd_agent : "");
                if (ctx.color) put(C_RESET);
                free(line);
                line = jc_strdup(expanded != NULL ? expanded : "");
            } else {
                /* Not a file command — maybe a prompt from an MCP server (M43),
                 * exposed under its bare name. Fetch it and submit its rendered
                 * messages as this turn; any trailing args map to the prompt's
                 * declared arguments (M49). */
                const char *pargs = rest + cn;
                char *ptext = NULL;
                jc_status pst;
                while (*pargs == ' ') {
                    pargs++;
                }
                pst = (app->mcp != NULL)
                    ? jc_mcp_get_prompt_args(app->mcp, cname, pargs, &ptext)
                    : JC_ERR_INVALID;
                if (pst == JC_ERR_INVALID) {
                    /* M345: the same kindness the model gets on a typo'd tool
                     * name. Silence on a wild guess; never an easter egg. */
                    const char *near = tui_suggest_cmd(app, cname);
                    if (near != NULL) {
                        printf("unknown command '/%s' (did you mean /%s?)\n",
                               cname, near);
                    } else {
                        printf("unknown command '/%s' (try /help)\n", cname);
                    }
                    free(line);
                    continue;
                }
                if (pst != JC_OK || ptext == NULL) {
                    free(ptext);
                    printf("could not fetch MCP prompt '/%s'\n", cname);
                    free(line);
                    continue;
                }
                if (ctx.color) put(C_DIM);
                printf("(running /%s from an MCP server)\n", cname);
                if (ctx.color) put(C_RESET);
                free(line);
                line = ptext; /* malloc'd rendered prompt => the user message */
            }
        } else {
            /* Plain message: expand @file / @diff / @url references (if
             * enabled), then inject auto-RAG context (M61, if enabled). Both
             * no-op unless configured; auto-context skips a message that
             * already carries @-references. */
            if (app->config.references) {
                char *ex;
                if (jc_refs_expand(app, line, jc_app_scratch(app), &ex) ==
                        JC_OK && ex != NULL && strcmp(ex, line) != 0) {
                    if (ctx.color) put(C_DIM);
                    put("(referenced context attached)\n");
                    if (ctx.color) put(C_RESET);
                    free(line);
                    line = jc_strdup(ex);
                }
            }
            {
                char *ac;
                if (jc_autocontext_expand(app, line, jc_app_scratch(app),
                        &ac) == JC_OK && ac != NULL && strcmp(ac, line) != 0) {
                    if (ctx.color) put(C_DIM);
                    put("(auto-retrieved context attached)\n");
                    if (ctx.color) put(C_RESET);
                    free(line);
                    line = jc_strdup(ac);
                }
            }
        }

        {
            jc_status st;
            /* Honor a command's `agent:` / `model:` / `subtask:` frontmatter. */
            jc_app_command_agent_apply(app, cmd_agent, &agent_save);
            jc_app_command_model_apply(app, cmd_model, &model_save);
            /* M254: hold the terminal for the whole turn so keystrokes typed
             * while the agent works are collected instead of being echoed into
             * the output and then flushed away. Straight-line region -- nothing
             * between here and queue_hold_end returns or continues -- so the
             * hold cannot leak past the turn. */
            queue_hold_begin(&ctx);
            if (cmd_subtask) {
                st = jc_agent_run_command_subtask(app, hist, line, cmd_output,
                                                  cmd_language, &cb);
            } else {
                struct jc_message *um =
                    jc_history_add(hist, JC_ROLE_USER, line);
                /* Attach any @image references (M29), gated on vision. */
                if (um != NULL && app->config.model.vision &&
                    app->config.references) {
                    jc_refs_attach_images(app, line, um);
                }
                st = jc_agent_run_turn(app, hist, &cb);
            }
            queue_hold_end(&ctx);
            jc_app_command_model_restore(app, &model_save);
            jc_app_command_agent_restore(app, &agent_save);
            free(line);
            if (st == JC_ERR_ABORTED) {
                put("\n[interrupted]\n");
            } else if (st != JC_OK) {
                printf("\n[error: %s]\n", jc_status_str(st));
            }
        }

        /* Persist after each turn so a crash or Ctrl-C keeps progress. */
        jc_session_autotitle(&session);
        session.mode = app->mode;
        jc_session_save(&session);

        /* Completion notification (F6): ring the bell / run the notify command
         * now that the agent is done with this turn and waiting on you. */
        jc_notify_fire(app->config.notify, app->config.notify_bell, app->cwd,
                       session.title);
    }

    session.mode = app->mode;
    jc_session_save(&session);
    jc_session_free(&session);
    app->ask = NULL; /* `ask` lived on this stack frame */
    jc_sb_free(&ctx.qbuf);
    jc_sb_free(&ctx.voice_buf);   /* M303 */
    jc_sb_free(&ctx.acc_buf);     /* M549 */
    jc_sb_free(&ctx.qpend);
    jc_term_free(&term);

    if (ctx.tot_in > 0.0 || ctx.tot_out > 0.0) {
        double cost = jc_config_cost(&app->config.model,
                                     ctx.tot_in, ctx.tot_out,
                                     ctx.tot_cache_read, ctx.tot_cache_write);
        char si[40], so[40];
        jc_group_num(ctx.tot_in, chrome_group_sep(&ctx), si, sizeof si);
        jc_group_num(ctx.tot_out, chrome_group_sep(&ctx), so, sizeof so);
        if (ctx.accessible) {
            /* M553: prose, and `/` and `$` in particular -- a slash between two
             * numbers reads as "slash" and a dollar sign as "dollar sign". */
            /* M554: shortened and split, on a measurement. The cost form had
             * an **84-column fixed part** -- it wrapped before a single number
             * was substituted -- and the plain form measured 79 with its two
             * numbers, one column over budget. "In total this session used" is
             * six words to say what "This session used" says in three. */
            printf(jc_msg(JC_MSG_SESSION_TOKENS), si, so);
            printf("\n");
            if (cost > 0.0) {
                printf(jc_msg(JC_MSG_SESSION_COST), cost);
                printf("\n");
            }
        } else if (cost > 0.0) {
            printf("session total: %s in / %s out tokens (~$%.4f)\n",
                   si, so, cost);
        } else {
            printf("session total: %s in / %s out tokens\n", si, so);
        }
    }
    {
        jc_size i;
        for (i = 0; i < ctx.always.len; i++) {
            free(*(char **)jc_vec_at(&ctx.always, i));
        }
        jc_vec_free(&ctx.always);
    }
    jc_mdr_free(&ctx.md);
    return 0;
}
