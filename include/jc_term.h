/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_term.h - raw-mode terminal handling and a line editor.
 *
 * jichi keeps full raw mode active only while editing an input line; the
 * assistant's streamed output is printed with the terminal in its normal
 * (cooked) mode. This sidesteps manual CR/LF translation and keeps the
 * implementation small while still giving real line editing and history.
 *
 * One exception: the turn-scoped HOLD (M254). While the agent works, the
 * terminal is held in a no-echo, no-canonical mode so keystrokes typed during
 * the turn are collected by jichi (jc_term_poll_key) instead of being echoed
 * into the streamed output and then discarded. Output post-processing is left
 * alone, so the streaming renderer is unaffected -- see jc_term_hold_begin.
 */
#ifndef JC_TERM_H
#define JC_TERM_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

#include <termios.h>

/* Tab-completion callback: for the line `buf` with the cursor at byte `cursor`,
 * fill `out` (a jc_vec of char*, each a malloc'd full replacement for the token
 * being completed; jc_term frees them) and set *token_start to that token's
 * start offset. Returns the candidate count. */
typedef int (*jc_completer_fn)(void *ctx, const char *buf, jc_size cursor,
                               jc_size *token_start, struct jc_vec *out);

/* Inline-suggestion ("ghost text") callback: synchronously produce a completion
 * of `buf` (the full current line) into `out` (capacity `cap`, NUL-terminated)
 * and return its length, or 0 for no suggestion. Called when the user presses
 * the suggest key (Ctrl-G) at end of line; the implementation (the TUI) makes a
 * one-shot model call, so it may block briefly. */
typedef jc_size (*jc_suggest_fn)(void *ctx, const char *buf, jc_size cursor,
                                 char *out, jc_size cap);

/* Prompt-advice callback (Ctrl-Q, M280): synchronously produce ONE short line
 * about the request being composed -- what is unclear, what to decide -- into
 * `out`, returning its length or 0 for nothing to say. Same signature as the
 * suggester, and deliberately a DIFFERENT gesture: a suggestion is spliced
 * into the line when accepted, whereas advice is printed on its own labelled
 * line above a redrawn prompt and never touches the buffer. The two were split
 * because a model that answers instead of continuing produces garbled input
 * (docs/AUTOCOMPLETE.md); giving that reply its own rendering makes it useful
 * rather than a defect. */
typedef jc_size (*jc_advise_fn)(void *ctx, const char *buf, jc_size cursor,
                                char *out, jc_size cap);

struct jc_term {
    int             in_fd;
    int             out_fd;
    int             is_tty;
    struct jc_vec   history;     /* of char*, newest last */
    jc_completer_fn complete;    /* Tab completion, or NULL */
    const char     *suggest_announce; /* M578: label => announce, NULL => ghost */
    void           *complete_ctx;
    jc_suggest_fn   suggest;     /* Ctrl-G inline suggestion, or NULL */
    void           *suggest_ctx;
    jc_advise_fn    advise;      /* Ctrl-Q prompt advice, or NULL      */
    void           *advise_ctx;
    int             holding;     /* a turn-scoped hold is active (M254) */
    struct termios  hold_saved;  /* the mode to restore when it ends    */
    int             flushed_pending; /* M503: the last enter_raw discarded
                                      * type-ahead the tty had buffered. Set by
                                      * enter_raw, consumed (and cleared) by
                                      * jc_term_readline, which says so. */
};

/* Install a Tab-completion callback (NULL to disable). */
void jc_term_set_completer(struct jc_term *t, jc_completer_fn fn, void *ctx);

/* Install an inline-suggestion callback (NULL to disable). */
void jc_term_set_suggester(struct jc_term *t, jc_suggest_fn fn, void *ctx);

/* Install a prompt-advice callback (NULL to disable). */
void jc_term_set_adviser(struct jc_term *t, jc_advise_fn fn, void *ctx);

/* M578: ANNOUNCE a suggestion on its own labelled line instead of ghosting it
 * inline. NULL (the default) keeps the dim inline ghost; a label switches to
 * the adviser's rendering -- a fresh line above a redrawn prompt.
 *
 * WHY, and the answer is two paragraphs up in this file. The ghost is
 * distinguished from the user's own text by ONE THING: `\x1b[2m`, dim. That is
 * COLOUR, and colour is exactly the channel that vanishes for a listener --
 * which by M561's test makes this a defect rather than a preference. The
 * operator measured it by ear: typing "what is the Japanese word for free" and
 * pressing Ctrl-G, their reader spoke the suggestion as though they had typed
 * it.
 *
 * The remedy is already described in this header, for the neighbouring gesture:
 * advice "is printed on its own labelled line above a redrawn prompt and never
 * touches the buffer", split from suggestions BECAUSE splicing a model's reply
 * into the line produces garbled input. Under a screen reader a suggestion has
 * the same problem, so it gets the same rendering -- while Tab still accepts it,
 * because it is still a suggestion.
 *
 * The LABEL is passed in rather than built here so the catalog stays in the TUI
 * layer and the text stays translatable; jc_term has no business knowing about
 * jc_msg. Pointer is borrowed, must outlive the readline call. */
void jc_term_set_suggest_announce(struct jc_term *t, const char *label);

/* M362 introduced INCREMENTAL INPUT ECHO; M558 made it unconditional.
 *
 * The wrap-aware redraw rewrites the WHOLE prompt+line (cursor-up, CR,
 * erase-below, reprint) on every keystroke -- ~39 bytes and one ESC[J per typed
 * character -- which a screen reader re-announces as a changed line each time.
 * The incremental path emits just the character (or "\b \b") for the common
 * case, like a plain cooked terminal, and falls back to the full redraw for
 * everything else. The predicates below are pure and unit-tested.
 *
 * IT USED TO BE GATED ON --accessible, FOR NO TECHNICAL REASON. `t->accessible`
 * gated exactly one line, and `jc_term_fast_echo_ok` / `jc_term_fast_bs_ok`
 * already refuse every case where appending a byte differs from a repaint:
 * cursor not at the end, non-ASCII, or landing on a column boundary. A mode flag
 * on top of a correctness predicate only chose whether to take the cheaper of
 * two identical outcomes. M362 scoped it narrowly out of caution, which was the
 * right instinct with an unknown blast radius.
 *
 * MEASURED at M558: the DEFAULT session goes from **37 erase-belows to 4** --
 * the figure accessible mode already had -- and halves its terminal traffic,
 * while the request the model receives is byte-identical (two captured requests
 * differed only in the run's own temp directory and its session UUID).
 *
 * There is no flag any more, deliberately: a setter with no observable effect is
 * a trap for the next reader, and nothing needs to disable a path whose
 * preconditions are already checked.
 *
 * READ tests/e2e/redraw.py's docstring BEFORE CHANGING THIS. Removing the
 * per-keystroke repaint exposed a bug in that test's VT emulator, which had been
 * masked for ~200 milestones precisely BY the redundant repaint -- and the
 * symptom was indistinguishable from a redraw defect here. It cost two wrong
 * diagnoses. The emulator is fixed and now self-tests.
 *
 * First property retired from the `--accessible` bundle -- seven behaviours with
 * different costs to a sighted user, to be decided one at a time:
 * docs/proposals/2026-08-accessibility-by-default.md §3. */

/* Fast append echo is safe: printable single-byte ASCII, cursor at end, and
 * the line after the append does not land on a column boundary (the phantom
 * last column needs render()'s CR/LF resolution). */
int jc_term_fast_echo_ok(int ch, int at_end, int prompt_cols,
                         int line_cols_after, int cols);

/* Fast backspace echo is safe: the removed char was printable single-byte
 * ASCII, cursor at end, and the cursor was not at column 0 of a wrapped row
 * ("\b" cannot cross a row boundary). */
int jc_term_fast_bs_ok(int last_ch, int at_end, int prompt_cols,
                       int line_cols_before, int cols);

void jc_term_init(struct jc_term *t);
void jc_term_free(struct jc_term *t);

/* Outcome of an interactive read. */
typedef enum {
    JC_READ_LINE,   /* a line was submitted (in *out)         */
    JC_READ_EOF,    /* Ctrl-D on empty / stream closed        */
    JC_READ_INTR    /* Ctrl-C requesting exit on empty input  */
} jc_read_result;

/* Read one line of input with editing. `prompt` is shown at the left. On
 * JC_READ_LINE, *out points to a malloc'd NUL-terminated string the caller
 * owns. On return the terminal is back in whatever mode was active when the
 * call was made: cooked normally, or the turn-scoped hold when one is active
 * (the ask_user prompt runs mid-turn, inside a hold -- M254). */
jc_read_result jc_term_readline(struct jc_term *t, const char *prompt,
                                char **out);

/* Read a single keypress (no Enter needed) with the terminal briefly in raw
 * mode; returns the byte, or -1 on EOF/error. On a non-TTY it reads a line and
 * returns its first byte (draining the rest). Used for y/n/a/v prompts. */
int jc_term_read_key(struct jc_term *t);

/* Begin a turn-scoped hold (M254): echo and canonical mode off, so keystrokes
 * typed while the agent works are buffered for jc_term_poll_key rather than
 * echoed into the streamed output. Deliberately keeps ISIG (Ctrl-C must still
 * raise SIGINT -- the abort path) and OPOST (the streaming renderer writes bare
 * "\n" and relies on the tty's LF -> CRLF translation), and does NOT flush
 * pending input on entry, so a keystroke racing the call is kept. Returns 0 on
 * success, -1 on a non-TTY / already holding / termios failure. readline and
 * read_key nest inside a hold: they save and restore around themselves, so the
 * hold is still in force when they return. */
int  jc_term_hold_begin(struct jc_term *t);

/* End a hold, restoring the mode captured at hold_begin. Idempotent. */
void jc_term_hold_end(struct jc_term *t);

/* Next buffered input byte during a hold, or -1 when nothing is waiting (the
 * hold sets VMIN=0/VTIME=0, so this never blocks). -1 also when not holding. */
int  jc_term_poll_key(struct jc_term *t);

/* Record `line` in the editor's recall history, exactly as a submitted line
 * would be (empty input is ignored). Needed because a line can now reach the
 * agent WITHOUT going through jc_term_readline -- the M254 type-ahead queue --
 * and such a line must still be recallable with Up / Ctrl-R. */
void jc_term_history_add(struct jc_term *t, const char *line);

/* Display width (in terminal columns) of the first `n` bytes of `s`: ANSI CSI
 * escape sequences count 0, and each UTF-8 character counts 1 (continuation
 * bytes count 0). Pure; used by the line editor's wrap-aware redraw and
 * unit-tested. */
int jc_term_str_cols(const char *s, jc_size n);

/* M363: like jc_term_str_cols, but starting at absolute column `start_col`,
 * which is what makes a TAB accountable: it advances to the next 8-column
 * stop of the absolute position, so the buffer's width depends on where the
 * prompt ended. Tabs enter the buffer only via paste; approximate on lines
 * that wrap (tab stops reset per screen row, this walk is linear), exact
 * otherwise -- the same wrapped-edge honesty as the fast-echo predicates. */
int jc_term_str_cols_from(int start_col, const char *s, jc_size n);

#ifdef __cplusplus
}
#endif
#endif /* JC_TERM_H */
