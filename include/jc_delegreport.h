/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_delegreport.h - one structured report shape for a delegated run (M437).
 *
 * THE DEFECT. `spawn_subagent` returned the delegate's prose answer and, on
 * failure, one of two fixed strings: "error: sub-agent interrupted" or
 * "error: sub-agent run failed". A parent could not tell an edit-scope denial
 * from a tool error from a model refusal, so its only two moves were to
 * re-delegate identically -- paying the whole subtask twice -- or to give up. On
 * a run metered by tokens, both are expensive and neither is informed.
 *
 * Every field here already existed somewhere. The fork pool pipes `tokens` and
 * `tool_calls` per child; `run_agent_loop` sets `last_run_capped` and
 * `last_run_budget_stopped`; the loop's own `is_error` branch already classifies
 * each failure with `jc_fail_classify` (M432), which is precisely the
 * denied/not_found/bad_args distinction a parent was missing. What was absent was
 * a shape to say it in, and one renderer so the two delegation tools cannot
 * describe the same outcome two ways.
 *
 * NOT INCLUDED, deliberately: the delegate's transcript. Forwarding it defeats
 * the purpose of delegating -- the parent delegated to avoid holding that context.
 *
 * NOT INCLUDED YET: `files_changed[]` for a synchronous subagent. Deriving it
 * needs a shadow-repo baseline taken at the delegate's start, and taking a
 * checkpoint per delegation would insert entries into the stack `/undo`,
 * `/rewind` and `checkpoints` present to the user as their own turns. A write
 * child of `spawn_parallel` runs in a worktree whose changed files the parent
 * already parses, so the field is populated there and absent for the sync tool --
 * an asymmetry stated rather than papered over. See docs/SUBAGENTS.md.
 */
#ifndef JC_DELEGREPORT_H
#define JC_DELEGREPORT_H

/* M867: its own C++ linkage guard. Without it this header's functions had C
 * linkage in a unit that reached it through another header's extern "C" block
 * (jc_agent.h wraps its includes) and C++ linkage where it was included
 * directly -- the C++ build then failed to link (undefined jc_run_stop_wire). */
#ifdef __cplusplus
extern "C" {
#endif

#include "jc_platform.h"
#include "jc_str.h"
#include "jc_toolloop.h"   /* enum jc_fail_class */

/* Why the delegated run ended. Ordered from best to worst outcome, and named for
 * what the PARENT should do about it -- the whole point of the field. */
enum jc_deleg_stop {
    JC_DELEG_DONE = 0,    /* produced a final answer                          */
    JC_DELEG_MAX_ITERS,   /* hit its own iteration cap: answer may be partial */
    JC_DELEG_BUDGET,      /* the RUN's budget is spent: do not delegate again */
    JC_DELEG_NO_ANSWER,   /* ran, then ended with nothing to say              */
    JC_DELEG_ABORTED,     /* the operator interrupted the run                 */
    JC_DELEG_ERROR,       /* the run itself failed (transport, provider, OOM)  */
    JC_DELEG_TASK_BUDGET  /* M861: its task's own max_tool_calls is reached;
                           * the run goes on, so delegating again is allowed */
};

#define JC_DELEG_TOOL_MAX 64
#define JC_DELEG_MSG_MAX 200

struct jc_delegate_report {
    enum jc_deleg_stop stop;
    /* The delegate's own last failing tool call, or an empty `tool` when it had
     * none. `cls` is jc_fail_classify's verdict on the message -- the field that
     * answers "was I denied, or did the tool fail?". */
    char               fail_tool[JC_DELEG_TOOL_MAX];
    enum jc_fail_class fail_cls;
    char               fail_msg[JC_DELEG_MSG_MAX];
    /* Known only when the run carries an envelope (--auto), which is where the
     * counters live. -1 means "not measured", NOT zero: a zero would assert the
     * delegate made no calls, which is a different and usually false claim. */
    long               tool_calls;
    double             tokens;
    /* Bounded, comma-separated, already-rendered list, or empty. Populated for a
     * spawn_parallel write child; see the header comment for why not here. */
    char               files_changed[JC_DELEG_MSG_MAX];
    /* M858 (N42): the delegate's last reply was cut at the output ceiling, so its
     * answer may stop mid-sentence. 0 = not cut, or not known. */
    int                cut;
    /* M869 (def-305): a MAX_ITERS stop that was a LOOP stop (jc_app's
     * last_run_looped), so the advice does not tell a parent to narrow a task that
     * merely repeated itself. 0 = a real iteration cap, or not known. */
    int                looped;
    /* M861: a BUDGET stop that spent only this delegate's SHARE of the run's
     * budget -- a spawn_parallel child's slice -- while the run itself has budget
     * left. BUDGET's own advice ("do not delegate again") assumes the run's budget
     * is spent, which holds for spawn_subagent and not for a share. */
    int                share_only;
    /* M876 (the demos drive's N47 (a)): the run's OWN failure when its model
     * calls failed -- the transport's diagnosis or the provider's status -- with
     * the attempts made and the wall clock of the first and last; empty when the
     * run did not end on a model call. Rendered first for a stop of `error`: a
     * worker whose gateway went down was reported by its last failing TOOL call
     * alone, a time limit, and its lead planned around the wrong cause. */
    char               run_error[JC_DELEG_MSG_MAX];
    int                run_error_attempts;
    long               run_error_t0;
    long               run_error_t1;
    /* M876 (N47 (c)): how many of the run's failing calls had fail_cls; 0 = not
     * known (an older child), which reads as "once". */
    int                fail_count;
    /* M885 (the demos drive's N47, continued): the delegate was stopped by a
     * SIGTERM from outside the run -- a supervisor's, not the operator's
     * interrupt and not its batch's own stop. Run 0102's lead copied "the
     * operator interrupted the run -- stop and report" into the team's log
     * after a supervisor stopped one worker. 0 = not known, or not that. */
    int                term;
    /* M885: the last failing call was a command killed because the delegate's
     * run was being stopped, so it met no limit and splitting the task would
     * not help. 0 = not known, or a real kill. */
    int                fail_stopped;
};

/* Zero a report to the "nothing known" state (tool_calls/tokens = not measured). */
void jc_delegreport_init(struct jc_delegate_report *r);

const char *jc_delegreport_stop_name(enum jc_deleg_stop s);

/* The inverse, for the fork pool: a child sends its stop reason as the NAME over
 * the pipe (a name survives an enum renumbering; an integer does not), and the
 * parent parses it back so it can render through the same function. An
 * unrecognised or empty name yields JC_DELEG_DONE -- the caller checks for an
 * empty string first, so this never has to invent a reason. */
enum jc_deleg_stop jc_delegreport_stop_parse(const char *name);

/* Render the report as ONE bracketed block for the model, appended to `out`.
 * Pure. Emits nothing at all for a plain JC_DELEG_DONE with no measurements and
 * no failure -- a clean delegation should not pay for a report saying so. */
void jc_delegreport_render(const struct jc_delegate_report *r, struct jc_sb *out);

/* The stop reason from the three facts the agent loop already tracks. Pure, so
 * the two delegation tools cannot disagree about what "done" means. */
enum jc_deleg_stop jc_delegreport_stop_from(int run_status_ok, int aborted,
                                            int capped, int budget_stopped,
                                            int have_answer);

#ifdef __cplusplus
}
#endif

#endif /* JC_DELEGREPORT_H */
