/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_relay.c -- the context relay's decision core (M920).
 *
 * A pure truth table: the loop hands in what the round measured, this returns
 * what to do and why. See include/jc_relay.h for the design and the proposal
 * (docs/proposals/2026-10-context-relay.md section 6.2) for why each trigger
 * is ordered where it is. C89, zero warnings, no I/O. M921 adds the baton
 * renderer (section 6.4): still pure -- the note is built in the caller's
 * buffer from records the caller names. */

#include "jc_relay.h"
#include <string.h>         /* strstr/memcpy (the unticket scan) */
#include <stdlib.h>         /* malloc/free (the unticket tail save) */
#include <jc_snprintf.h>    /* jc_snprintf (bounded note) */

/* The E3 comparison in integer math, 50..95 percent:
 * server_tokens >= hard_pct/100 * limit  <=>  server_tokens * 100 >=
 * limit * hard_pct. long arithmetic is safe: a prompt count near a 1M-token
 * window times 100 is ~1e8, well inside long. */
static int hard_limit_reached(const struct relay_signal *sig,
                              const struct relay_cfg *cfg)
{
    if (sig->limit <= 0 || sig->server_tokens <= 0) {
        return 0;   /* no measurement: the safety net cannot fire */
    }
    return (int)(sig->server_tokens * 100) >=
           (int)(sig->limit * (long)cfg->hard_pct);
}

enum relay_action relay_decide(const struct relay_signal *sig,
                               const struct relay_cfg *cfg,
                               struct relay_state *state,
                               enum relay_reason *reason)
{
    *reason = JC_RELAY_NONE;
    if (sig == NULL || cfg == NULL || state == NULL) {
        return JC_RELAY_CONTINUE;
    }
    if (!cfg->relay_on) {
        return JC_RELAY_CONTINUE;   /* off by default: the core never fires */
    }

    /* E4: the server refused a request as too long. The strongest evidence
     * of exhaustion, so it outranks the streak logic: one refusal hands over,
     * regardless of how the passes measured. */
    if (sig->overflow) {
        *reason = JC_RELAY_OVERFLOW;
        return JC_RELAY_HAND_OVER;
    }

    /* E3: the server's own count at or above the hard percentage of the
     * window. A safety net for a contextLength that understates (or the
     * server's bookkeeping that overstates) the model's real limit. */
    if (hard_limit_reached(sig, cfg)) {
        *reason = JC_RELAY_HARD_LIMIT;
        return JC_RELAY_HAND_OVER;
    }

    /* E1: the unrelieved streak, counted by the state. A pass that ran under
     * pressure and found nothing to elide will re-trigger next round -- two
     * in a row means elision is DONE relieving this history, which is the
     * exhaustion R5 defines by effect. A round without pressure resets the
     * streak: the window is not filling at all between pressured rounds. */
    if (sig->pressed && sig->unrelieved) {
        state->unrelieved_streak++;
    } else if (!sig->pressed) {
        state->unrelieved_streak = 0;
    }
    if (state->unrelieved_streak >= 2) {
        *reason = JC_RELAY_UNRELIEVED;
        return JC_RELAY_HAND_OVER;
    }

    /* E2: the latch armed while pressed. The M361 latch means a previous pass
     * PROVED the eligible range dry, so the trims are skipped; pressed means
     * the window is still over the high-water. Nothing elidable is left. */
    if (sig->pressed && sig->latched) {
        *reason = JC_RELAY_LATCHED;
        return JC_RELAY_HAND_OVER;
    }

    /* The "short" trigger: the operator's cheaper-on-cache choice. Pressed and
     * the pass missed its target -- the request is going out bigger than
     * needed, so hand over before it costs a full cold start. Off unless
     * configured (the default "exhausted" does not fire here). */
    if (cfg->trigger == 1 && sig->pressed && !sig->reached) {
        *reason = JC_RELAY_SHORT;
        return JC_RELAY_HAND_OVER;
    }

    /* E5: the leg's tool-call budget. The only trigger that can fire on a
     * round that measured NO pressure: a topology-B leg is capped by
     * construction, and a handover at the cap is the budget controller
     * (V2, next milestone) in its simplest form. Checked last, so a pressured
     * round reports the pressure, not the counter. */
    if (cfg->leg_calls_max > 0 && sig->leg_calls >= cfg->leg_calls_max) {
        *reason = JC_RELAY_LEG_BUDGET;
        return JC_RELAY_HAND_OVER;
    }

    return JC_RELAY_CONTINUE;
}

enum relay_action relay_may_continue(const struct relay_state *state,
                                     const struct relay_cfg *cfg,
                                     enum relay_reason *reason)
{
    *reason = JC_RELAY_NONE;
    if (state == NULL || cfg == NULL) {
        return JC_RELAY_STOP_STALLED;   /* no state: refuse to speculate */
    }
    if (state->legs >= cfg->legs_max) {
        *reason = JC_RELAY_MAX_LEGS;
        return JC_RELAY_STOP_STALLED;
    }
    if (state->no_progress_legs >= 2) {
        *reason = JC_RELAY_NO_PROGRESS;
        return JC_RELAY_STOP_STALLED;
    }
    return JC_RELAY_CONTINUE;
}

void relay_leg_budget(long P0, long g, long W, long P0_next,
                      jc_size cap, long fence, jc_size *out_B)
{
    long B;

    if (cap == 0) {
        /* cap 0: the caller means "no leg budget" (E5 off), so out_B 0 keeps
         * the signal's leg_calls_max 0 and the trigger never fires. Total:
         * even this branch must write an answer, never leave *out_B stale. */
        *out_B = 0;
        return;
    }
    if (g <= 0 || W <= 0) {
        /* No growth, or no window: the prompt is not filling, so there is
         * nothing to tighten -- the operator's cap stands. These are the
         * DEGENERACY cases the design's truth table names: a budget that
         * could not be measured must not invent a limit. */
        B = (long)cap;
    } else {
        /* The baton's notes make the next leg's first prompt bigger than this
         * one's (6.10's own remark), so P0_next < P0 is a caller error:
         * clamp it UP to P0 and keep going -- a budget computed from a
         * too-small P0 would be too big, and the error is in the input, not
         * the arithmetic. */
        if (P0_next < P0) {
            P0_next = P0;
        }
        {
            long num = (JC_RELAY_BUDGET_HIGH_PCT * W) / 100L - P0_next
                       - JC_RELAY_BUDGET_RESERVE;
            long n_fit;
            if (num <= 0) {
                /* P0_next alone is at or past the high-water: the next leg can
                 * barely fit a handover call. The floor is still the defined
                 * answer -- BUDGET_MIN, never 0 (0 means "off", a different
                 * fact). */
                B = JC_RELAY_BUDGET_MIN;
            } else {
                n_fit = num / g;   /* floor by construction (positive
                                      * operands) */
                if (n_fit > (long)cap) {
                    B = (long)cap;          /* tighten-only: never above cap */
                } else if (n_fit < JC_RELAY_BUDGET_MIN) {
                    B = JC_RELAY_BUDGET_MIN;
                } else {
                    B = n_fit;
                }
            }
        }
    }
    /* The relay's fence bounds the WHOLE relay, so the next leg's budget can
     * never carry it past (6.10's last line) -- on EVERY path, a measured
     * one or a degenerate one. A leg of `fence` calls at most, where fence 0
     * means "none set" and the cap alone bounds. This cap runs LAST and wins
     * over the floor: a relay with 3 calls left gives a 3-call leg, never a
     * 25-call one that would run the relay out. */
    if (fence > 0 && B > fence) {
        B = fence;
    }
    *out_B = (jc_size)B;
}

const char *relay_reason_name(enum relay_reason r)
{
    switch (r) {
    case JC_RELAY_OVERFLOW:    return "overflow";
    case JC_RELAY_HARD_LIMIT:  return "hard_limit";
    case JC_RELAY_UNRELIEVED:  return "unrelieved";
    case JC_RELAY_LATCHED:     return "latched";
    case JC_RELAY_SHORT:       return "short";
    case JC_RELAY_LEG_BUDGET:  return "leg_budget";
    case JC_RELAY_MAX_LEGS:    return "max_legs";
    case JC_RELAY_NO_PROGRESS: return "no_progress";
    case JC_RELAY_NONE:        return "none";
    }
    return "unknown";
}

/* --- M921: the baton (proposal 6.4), rendered --------------------------- */

/* A path that dies with the process must never reach a note that outlives it
 * (6.4's last bullet; R01-F4): the claim-ticket and spill files live under
 * the per-pid store named "<...>/tool-output/<pid>/...". Replace the
 * "tool-output/" prefix (12 chars) with the mark (21): the note grows by
 * 9 bytes per ticket. Since in-place right-shift is impossible (it would
 * overwrite bytes before they are moved), we rebuild into a fresh buffer
 * and let the caller update the jc_sb.
 *
 * Returns NULL with *st == JC_OK when no ticket paths are found (leave as-is);
 * returns NULL with *st == JC_ERR_OOM when malloc failed (render fails);
 * returns non-NULL with *st == JC_OK when the buffer was built successfully.
 * The caller must treat any *st != JC_OK as render failure and write nothing. */
static char *relay_baton_unticket_build(const char *in, jc_size *out_len,
                                        jc_status *st)
{
    const char *mark = "[ticket path omitted]";   /* 21 + NUL */
    const char *tool_prefix = "tool-output/";    /* 12 chars */
    long count = 0;
    size_t in_len;
    size_t new_len;
    char *out;
    const char *hit;
    const char *inp;
    char *outp;

    *st = JC_OK;

    in_len = strlen(in);

    /* Count occurrences first to know how much space we need */
    hit = in;
    while ((hit = strstr(hit, tool_prefix)) != NULL) {
        count++;
        hit += 12;  /* move past this occurrence */
    }
    if (count == 0) {
        /* No occurrences - signal no change needed */
        *out_len = (jc_size)in_len;
        return NULL;  /* leave as-is */
    }

    /* Size the new buffer: original + 9 extra bytes per ticket (21 - 12 = 9) */
    new_len = in_len + (size_t)count * 9;
    out = (char *)malloc(new_len + 1);  /* +1 for NUL terminator */
    if (out == NULL) {
        *out_len = (jc_size)in_len;
        *st = JC_ERR_OOM;
        return NULL;  /* allocation failed - render fails */
    }

    /* Build the output: copy from input, replacing "tool-output/" with mark */
    inp = in;
    outp = out;
    while ((hit = strstr(inp, tool_prefix)) != NULL) {
        /* Copy everything up to (but not including) the match */
        size_t prefix_len = (size_t)(hit - inp);
        memcpy(outp, inp, prefix_len);
        outp += prefix_len;

        /* Write the mark (21 bytes including NUL) */
        memcpy(outp, mark, 21);
        outp += 21;

        /* Move past the "tool-output/" prefix */
        inp = hit + 12;
    }

    /* Copy the remaining tail after the last replacement */
    strcpy(outp, inp);

    *out_len = (jc_size)new_len;
    *st = JC_OK;
    return out;
}

/* Cut `in` to at most `max` bytes on a UTF-8 character boundary (6.4: "cut
 * first, at a UTF-8 boundary, with a visible note"): a byte with the high bit
 * set that is NOT a continuation byte (10xxxxxx) opens a character, so the
 * cut walks back to the last such opening byte at or before max. `out` is a
 * caller buffer of at least max+1. Returns the cut length. */
static jc_size relay_baton_utf8_cut(const char *in, jc_size max)
{
    jc_size i;
    jc_size n = strlen(in);
    if (n <= max) {
        return n;
    }
    for (i = max; i > 0; i--) {
        int c = (unsigned char)in[i - 1];
        if ((c & 0xC0) != 0x80) {   /* an opening byte (ASCII or lead) */
            return i - 1;
        }
    }
    return 0;   /* the first max bytes are all continuations: impossible in
                 * valid UTF-8, but the cut must stay defined */
}

jc_status relay_baton_render(const struct relay_baton_facts *f,
                             struct jc_sb *out)
{
    jc_size i;
    jc_status st;

    if (f == NULL || out == NULL) {
        return JC_ERR_INVALID;
    }
    /* jc_sb's appends return JC_ERR_OOM when their grow fails; the renderer
     * propagates it and the caller writes NOTHING -- all-or-nothing. */

    st = jc_sb_append_fmt(out,
        "# Relay notes -- leg %d of relay %s\n"
        "Previous leg ended: %s\n\n",
        f->leg,
        f->relay_id != NULL ? f->relay_id : "(id unknown)",
        f->reason != NULL ? f->reason : "none");
    if (st != JC_OK) {
        return st;
    }
    if (f->commit != NULL && f->commit[0] != '\0') {
        st = jc_sb_append_fmt(out, "Commit: %s   Tree: %s\n\n", f->commit,
                              f->dirty ? "dirty (uncommitted changes)"
                                       : "clean");
        if (st != JC_OK) {
            return st;
        }
    }

    /* --- section 1: the task, verbatim ---------------------------------- */
    st = jc_sb_append_fmt(out, "## 1. The task (verbatim, from leg 1)\n%s\n\n",
                          f->task != NULL ? f->task : "(no task recorded)");
    if (st != JC_OK) {
        return st;
    }

    /* --- section 2: the mechanical part (jichi's own records) ------------ */
    st = jc_sb_append(out,
        "## 2. Where the work stands (mechanical -- written by jichi, "
        "not by the model)\n");
    if (st != JC_OK) {
        return st;
    }
    if (f->fence_total > 0) {
        st = jc_sb_append_fmt(out,
            "- Calls: %ld in this leg, %ld in the relay; fence: %ld of %ld\n",
            f->leg_calls, f->relay_calls, f->relay_calls, f->fence_total);
    } else {
        st = jc_sb_append_fmt(out,
            "- Calls: %ld in this leg, %ld in the relay (no relay fence set)\n",
            f->leg_calls, f->relay_calls);
    }
    if (st != JC_OK) {
        return st;
    }
    /* M951: the next leg's budget, measured from this leg's growth and
     * carried across the process by the baton. A stable "Next-leg budget:"
     * label: --handoff-in parses it back (relay_seed_next_leg_budget_from_
     * notes). 0 = not measured -- the next leg uses its configured cap. */
    if (f->next_leg_budget > 0) {
        st = jc_sb_append_fmt(out,
            "- Next-leg budget: %ld tool calls (measured from this leg's "
            "prompt growth)\n",
            f->next_leg_budget);
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->peak_prompt > 0) {
        st = jc_sb_append_fmt(out, "- Last prompt: %ld tokens per the server\n",
                              f->peak_prompt);
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->changed != NULL && f->changed_n > 0) {
        jc_size n = ((jc_size)f->changed_n > JC_RELAY_BATON_TOPN)
                    ? JC_RELAY_BATON_TOPN : (jc_size)f->changed_n;
        st = jc_sb_append_fmt(out, "- Changed since the relay began (%d): ",
                              f->changed_n);
        if (st != JC_OK) {
            return st;
        }
        for (i = 0; i < n; i++) {
            st = jc_sb_append_fmt(out, "%s%s",
                                  f->changed[i] != NULL ? f->changed[i] : "?",
                                  i + 1 < n ? ", " : "");
            if (st != JC_OK) {
                return st;
            }
        }
        if ((jc_size)f->changed_n > n) {
            st = jc_sb_append_fmt(out, " (+%ld more)",
                                  f->changed_n - (long)n);
            if (st != JC_OK) {
                return st;
            }
        }
        st = jc_sb_append(out, "\n");
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->uncommitted != NULL && f->uncommitted_n > 0) {
        jc_size n = ((jc_size)f->uncommitted_n > JC_RELAY_BATON_TOPN)
                    ? JC_RELAY_BATON_TOPN : (jc_size)f->uncommitted_n;
        st = jc_sb_append_fmt(out, "- Uncommitted: ");
        if (st != JC_OK) {
            return st;
        }
        for (i = 0; i < n; i++) {
            st = jc_sb_append_fmt(out, "%s%s",
                                  f->uncommitted[i] != NULL
                                      ? f->uncommitted[i] : "?",
                                  i + 1 < n ? ", " : "");
            if (st != JC_OK) {
                return st;
            }
        }
        if ((jc_size)f->uncommitted_n > n) {
            st = jc_sb_append_fmt(out, " (+%ld more)",
                                  f->uncommitted_n - (long)n);
            if (st != JC_OK) {
                return st;
            }
        }
        st = jc_sb_append(out, "\n");
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->commits != NULL && f->commits_n > 0) {
        jc_size n = ((jc_size)f->commits_n > JC_RELAY_BATON_TOPN)
                    ? JC_RELAY_BATON_TOPN : (jc_size)f->commits_n;
        st = jc_sb_append(out, "- Commits in this leg:\n");
        if (st != JC_OK) {
            return st;
        }
        for (i = 0; i < n; i++) {
            st = jc_sb_append_fmt(out, "  %s",
                                  f->commits[i] != NULL ? f->commits[i] : "?");
            if (st != JC_OK) {
                return st;
            }
            if (i + 1 < n) {
                st = jc_sb_append(out, "\n");
                if (st != JC_OK) {
                    return st;
                }
            }
        }
        if ((jc_size)f->commits_n > n) {
            st = jc_sb_append_fmt(out, "\n  (+%ld more)",
                                  f->commits_n - (long)n);
            if (st != JC_OK) {
                return st;
            }
        }
        st = jc_sb_append(out, "\n");
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->todo != NULL && f->todo[0] != '\0') {
        st = jc_sb_append_fmt(out, "- Todo (as last written): %s\n", f->todo);
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->last_verify != NULL && f->last_verify[0] != '\0') {
        st = jc_sb_append_fmt(out, "- Last verify: %s\n", f->last_verify);
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->verify_fail != NULL && f->verify_fail_n > 0) {
        jc_size n = ((jc_size)f->verify_fail_n > JC_RELAY_BATON_TOPN)
                    ? JC_RELAY_BATON_TOPN : (jc_size)f->verify_fail_n;
        st = jc_sb_append_fmt(out, "  failing: ");
        if (st != JC_OK) {
            return st;
        }
        for (i = 0; i < n; i++) {
            st = jc_sb_append_fmt(out, "%s%s",
                                  f->verify_fail[i] != NULL
                                      ? f->verify_fail[i] : "?",
                                  i + 1 < n ? ", " : "");
            if (st != JC_OK) {
                return st;
            }
        }
        st = jc_sb_append(out, "\n");
        if (st != JC_OK) {
            return st;
        }
    }
    if (f->last_calls != NULL && f->last_calls_n > 0) {
        jc_size n = ((jc_size)f->last_calls_n > JC_RELAY_BATON_TOPN)
                    ? JC_RELAY_BATON_TOPN : (jc_size)f->last_calls_n;
        st = jc_sb_append_fmt(out, "- Last %d tool calls: ", (int)n);
        if (st != JC_OK) {
            return st;
        }
        for (i = 0; i < n; i++) {
            st = jc_sb_append_fmt(out, "%s%s",
                                  f->last_calls[i] != NULL
                                      ? f->last_calls[i] : "?",
                                  i + 1 < n ? "; " : "");
            if (st != JC_OK) {
                return st;
            }
        }
        st = jc_sb_append(out, "\n");
        if (st != JC_OK) {
            return st;
        }
    }
    st = jc_sb_append(out, "\n");
    if (st != JC_OK) {
        return st;
    }

    /* --- section 3: the model's part (the labelled, cut-first account) --- */
    st = jc_sb_append_fmt(out,
        "## 3. The previous leg's own account (written by the model; verify "
        "before relying on it)\n");
    if (st != JC_OK) {
        return st;
    }
    if (f->account != NULL && f->account[0] != '\0') {
        jc_size n = strlen(f->account);
        if (f->account_max > 0 && n > f->account_max) {
            jc_size cut = relay_baton_utf8_cut(f->account, f->account_max);
            st = jc_sb_append_n(out, f->account, cut);
            if (st != JC_OK) {
                return st;
            }
            st = jc_sb_append_fmt(out,
                "\n[account cut: %ld of %ld bytes shown]\n",
                (long)cut, (long)n);
        } else {
            st = jc_sb_append(out, f->account);
            if (st != JC_OK) {
                return st;
            }
            st = jc_sb_append(out, "\n");
        }
    } else {
        st = jc_sb_append(out,
            "(no account: the model's part was not obtained -- the last "
            "answer, if any, is in the session history)\n");
    }
    if (st != JC_OK) {
        return st;
    }

    /* --- section 4: the fixed text --------------------------------------- */
    st = jc_sb_append_fmt(out,
        "\n## 4. How to use these notes\n"
        "The repository is the truth; these notes are a map. Re-read a file "
        "before you edit it. If HEAD is not the commit above, the tree has "
        "moved.\n");
    if (st != JC_OK) {
        return st;
    }

    /* A claim ticket is a path into a store that dies with the process (6.4):
     * the whole finished note is scanned and unticketed. We build the
     * modified version into a fresh buffer and replace out->data with it. */
    if (out->data != NULL) {
        jc_size new_len = 0;
        jc_status st_unticket;
        char *unticketed = relay_baton_unticket_build(out->data, &new_len,
                                                      &st_unticket);
        if (unticketed != NULL) {
            /* We have a new buffer - free the old one and replace */
            free(out->data);
            out->data = unticketed;
            out->len = new_len;
            /* out->cap must track the exact allocation size so any future
             * jc_sb_use of this buffer is safe; jc_sb_reserve early-returns
             * when need <= cap, so a stale larger cap would let an append
             * write past the fresh allocation's end. */
            out->cap = new_len + 1;
        } else if (st_unticket != JC_OK) {
            /* OOM or other failure: propagate the error, caller writes nothing */
            return st_unticket;
        }
        /* If unticketed is NULL and st_unticket == JC_OK, no changes needed */
    }

    return JC_OK;
}
