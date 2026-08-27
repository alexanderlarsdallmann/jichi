/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* main.c - jichi entry point.
 *
 * Wires argument parsing -> config -> provider/tools -> the agent loop.
 * Headless mode (-p / positional prompt) runs one turn and streams the reply
 * to stdout. The interactive TUI is provided by the tui subsystem (M5).
 */

#include "jc_platform.h"
#include "jc_msg.h"   /* M566: the headless renderers speak prose too */
#include "jc_mem.h"
#include "jc_config.h"
#include "jc_configedit.h"
#include "jc_confbench.h"
#include "jc_packages.h"
#include "jc_app.h"
#include "jc_path.h"
#include "jc_hooks.h"
#include "jc_bg.h"
#include "jc_provider.h"
#include "jc_tool.h"
#include "jc_json.h"
#include "jc_perm.h"
#include "jc_notify.h"
#include "jc_rules.h"
#include "jc_sysmsg.h"
#include "jc_compact.h"
#include "jc_context.h"
#include "jc_promptcache.h"
#include "jc_memory.h"
#include "jc_glossary.h"
#include "jc_doctor.h"
#include "jc_scaffold.h"
#include "jc_assetval.h"
#include "jc_convert.h"
#include "jc_net.h"
#include "jc_oneshot.h"
#include "jc_toolprobe.h"
#include "jc_md.h"
#include "jc_todo.h"
#include "jc_cli.h"
#include "jc_agent.h"
#include "jc_message.h"
#include "jc_session.h"
#include "jc_http.h"
#include "jc_net.h"
#include "jc_log.h"
#include "jc_str.h"
#include "jc_base64.h"
#include "jc_tui.h"
#include "jc_acp.h"
#include "jc_daemon.h"
#include "jc_workerpool.h"
#include "jc_index.h"
#include "jc_pdf.h"
#include "jc_setup.h"
#include "jc_term.h"
#include "jc_search.h"
#include "jc_docs.h"
#include "jc_embed.h"
#include "jc_rerank.h"
#include "jc_mcp.h"
#include "jc_tool_user.h"
#include "jc_refs.h"
#include "jc_autocontext.h"
#include "jc_fim.h"
#include "jc_lsp.h"
#include "jc_snapshot.h"
#include "jc_lease.h"
#include "jc_toolout.h"
#include "jc_envelope.h"
#include "jc_agentjson.h"
#include "jc_eventlog.h"
#include "jc_telemetry.h"
#include "jc_auditview.h"
#include "jc_runsview.h"
#include "jc_control.h"
#include "jc_sound.h"
#include "jc_insights.h"
#include "jc_learn.h"
#include "jc_testparse.h"
#include "jc_assign.h"
#include "jc_gradecore.h"
#include "jc_progress.h"
#include "jc_meminfo.h"
#include "jc_memtrim.h"
#include "jc_improve.h"
#include "jc_cacheaudit.h"
#include "jc_workflow.h"
#include "jc_parallel.h"
#include "jc_proc.h"
#include "jc_repomap.h"
#include "jc_uuid.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <ctype.h>      /* isalnum: the flag-shaped -p prompt guard (M375) */
#include <sys/wait.h>
#include <sys/stat.h>   /* umask: the daemon socket's ACL (M322) */
#include <termios.h>    /* echo off for the setup key prompt (M326e) */
#include <sys/utsname.h> /* uname: the OS half of the machine probe (M326p) */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#include "jc_version.h"
#include "jc_buildrev.h"
#include "jc_license.h"
#include "jc_utf8.h"   /* jc_ctrl_sanitize (M472) */

static struct jc_app *g_app_for_signal = NULL;
static volatile sig_atomic_t g_got_sigterm = 0;

/* Shared SIGINT/SIGTERM handler (M146): both request a graceful abort of the
 * current work. SIGTERM (systemd, containers, a supervisor, plain `kill`)
 * additionally requests session end and is graceful ONCE -- the handler
 * restores the default disposition, so a second SIGTERM terminates
 * immediately if the graceful path is wedged. Only sig_atomic_t/volatile
 * flags are touched here, per the signal-handler invariant. */
static void on_sigint(int sig)
{
    if (sig == SIGTERM) {
        g_got_sigterm = 1;
        signal(SIGTERM, SIG_DFL);
        if (g_app_for_signal != NULL) {
            g_app_for_signal->term_flag = 1;
        }
    }
    if (g_app_for_signal != NULL) {
        g_app_for_signal->abort_flag = 1;
    }
}

static void print_version(void)
{
    const char *rev = jc_build_rev();
    printf("jichi %s\n", JC_VERSION);
    /* M497: the holder, and what the reader may do with the code. `--version` is
     * where a stranger looks for this, and a tool that prints neither leaves them
     * guessing at both. Since M619 the licence line names the decided identifier
     * (Apache-2.0, 2026-08-27); it said LicenseRef-UNDECIDED while the
     * institutional answer was pending, because silence reads as "permissively
     * licensed, presumably". */
    printf("%s\n", JC_COPYRIGHT);
    /* M619: the holder grants; the author wrote. § 69b UrhG separates the two,
     * and the notice carries both so a reader knows whom to ask and whom to
     * cite. */
    printf("%s\n", JC_AUTHOR);
    printf("licence: %s\n", JC_LICENSE_SPDX);
    /* M495: WHICH BUILD, not just which version. JC_VERSION moves once per
     * release; a binary can be 50 milestones behind the tree and still print the
     * same string, which is exactly what happened on 2026-08-19 -- an install from
     * 12 days earlier rejected a flag its own --help documented, and doctor's
     * context-window check printed NOTHING rather than failing. Same reasoning as
     * the FAULT=1 line below (M198): the build you are running should not have to
     * be inferred from its behaviour. NULL when there was no repository at build
     * time, and then nothing is printed -- see jc_buildrev.c. */
    if (rev != NULL) {
        printf("build: %s\n", rev);
    }
#ifdef JC_FAULT
    /* M198: a build with fault injection compiled in must SAY so. It is a test
     * build -- allocations and reads can be made to fail from the environment --
     * and nobody should discover that from behaviour. Also the reliable probe
     * tests/smoke/faults.sh uses to decide whether to run or skip. */
    printf("build: FAULT=1 (deterministic fault injection compiled in; "
           "see include/jc_fault.h)\n");
#endif
}

static void print_help(const char *argv0)
{
    printf("Usage: %s [options] [prompt...]\n\n", argv0);
    printf("Options:\n");
    printf("  -p, --print <prompt>   Headless mode: print the reply and exit\n");
    printf("      --config <path>    Use the given JSON config file\n");
    printf("      --config-json <s>  Use inline JSON as the whole config "
           "(self-contained runs;\n");
    printf("                         visible in `ps` -- use apiKeyEnv, not a "
           "literal key)\n");
    printf("      --config-json-b64 <b64>  Base64 of the inline config "
           "(quoting-safe transport)\n");
    printf("      --config-json -    Read the config JSON from stdin "
           "(alias --config-stdin;\n");
    printf("                         keeps it out of `ps`; prompt must then use "
           "-p/--prompt-b64)\n");
    printf("      --prompt-b64 <b64> Base64 of the prompt text (multiline/quote"
           "-safe)\n");
    printf("      --design <file>    Inject a design/spec doc as the "
           "authoritative plan for the task (alias --spec)\n");
    printf("      --model <sel>      Select a configured model (name/index/id substring), "
           "or a raw id\n");
    printf("                         on the active model if nothing matches\n");
    printf("      --plan             Start in plan mode (investigate, propose "
           "a plan, change nothing)\n");
    printf("      --auto             Start in auto mode (run approved tools "
           "without asking)\n");
    printf("      --readonly         Disable mutating tools\n");
    /* M539: two more undocumented off-switches, in the section a reader of
     * `--help` actually reaches. --config-editable is a privilege escalation: a
     * model that can write the config can widen its own fences. */
    printf("      --config-editable  Let the model write the config file via "
           "`config set` (it can then widen its own fences)\n");
    printf("      --path-fence/--no-path-fence  Force the workspace path "
           "containment fence on/off (default: auto = on in --auto)\n");
    printf("      --prompt-cache/--no-prompt-cache  Force prompt caching "
           "on/off (default: auto = on)\n");
    printf("      --cost-model/--no-cost-model  Tell the model its output caps "
           "and what follows (default: auto = on when caching is off)\n");
    printf("      --auto-context/--no-auto-context  Auto-retrieve relevant "
           "code/docs into each turn (default: off)\n");
    printf("      --learn-on-stop/--no-learn-on-stop  Draft lessons via the "
           "mentor after a completed --auto run (default: off)\n");
    printf("      --no-hooks         Disable configured lifecycle hooks for "
           "this run\n");
    printf("      --output-style <name>  Activate a custom output style "
           "(.jichi/output-styles/<name>.md)\n");
    printf("      --language <lang>  Answer in this natural language "
           "(e.g. Japanese, Deutsch; config \"language\")\n");
    printf("      --image <path>     Attach an image to the prompt (repeatable; "
           "needs a vision model). In a message, @photo.png / @img:<path> also "
           "attach.\n");
    printf("  -c, --continue         Resume the most recent session for this "
           "directory (alias: --resume)\n");
    printf("      --session <id>     Resume a specific session (id or "
           "unambiguous prefix; see `ls`)\n");
    printf("      --all              With ls / --continue: every project, not "
           "just this directory\n");
    printf("  -v, --verbose          Enable debug logging\n");
    printf("      --voice / --no-voice   Speak replies, approval prompts and "
           "errors aloud (needs an\n");
    printf("                         audio-role model + sound.play; off by "
           "default)\n");
    printf("  -q, --quiet, --silent  Minimal output: only essential info "
           "(errors still shown)\n");
    printf("      --output <fmt>     Output format: text (default), json (one "
           "object), or jsonl (streaming events)\n");
    printf("      --heartbeat <secs> With --output jsonl: emit a periodic "
           "\"heartbeat\" event\n");
    printf("                         while a model call is in flight (0 = off; "
           "for supervisors)\n");
    printf("      --color/--no-color Force ANSI color on/off (default: auto; "
           "honors NO_COLOR)\n");
    printf("      --no-stdin         Do not read piped stdin as/with the "
           "prompt\n");
    printf("      --no-session       Do not save this run as a session\n");
    printf("      --lite             Low-resource profile (lean defaults for "
           "RAM/CPU/disk;\n");
    printf("                         alias --low-memory; config: \"lowResource\""
           ")\n");
    printf("      --no-lite          Force the normal profile (wins over the "
           "config key\n");
    printf("                         and the low-RAM auto-detection)\n");
    printf("      --reindex          Rebuild the index cache from scratch\n");
    printf("  --                     Treat all following args as the prompt\n");
    printf("\nAutonomous envelope (bounds an unsupervised run; implies --auto "
           "in headless mode):\n");
    printf("      --verify <cmd>     Run <cmd> when the agent finishes; it must "
           "pass (exit 0)\n");
    printf("      --with-rules       attempt: load the project's AGENTS.md/CLAUDE.md "
           "into the graded run\n");
    printf("                         (off by default -- a big rules file dominated "
           "every call: M309)\n");
    printf("      --verify-retries <n>  Fix-forward attempts before rollback "
           "(default 3)\n");
    printf("      --verify-every <n>  Run the verifier every <n> tool calls "
           "mid-turn (bank green / feed failures back)\n");
    printf("      --verify-timeout <dur>  Kill the verifier if it runs longer "
           "(e.g. 5m)\n");
    printf("      --verify-baseline  Run the verifier once at start (records "
           "if the tree is already broken)\n");
    printf("      --verify-kind <invariant|goal>  Declare what the verifier "
           "asserts: an invariant (green before the work) or a goal (red "
           "until the work lands).\n");
    printf("                         Declaring arms the start probe, which "
           "CHECKS the declaration: a goal gate that already passes forces "
           "nothing (config \"verifyKind\")\n");
    printf("      --no-rollback      On failure, leave the workspace as-is "
           "instead of reverting\n");
    /* M539: the safety OFF-switches. All six were accepted by the parser and
     * documented in NO help output and NO man page -- an operator could not
     * discover that these guards can be turned off, let alone that a run had
     * turned one off. --no-preserve-discarded is the sharpest: it removes the
     * `jichi recover <hash>` handle that makes an `undo` recoverable, which is
     * the safety net M537's incident actually landed in. A fence nobody can
     * find is a fence nobody can reason about. */
    printf("      --no-self-review   Skip the self-review pass at the end of "
           "an autonomous run\n");
    printf("      --no-strict-green  Accept a green verify without the checks "
           "for a gate that passed for the wrong reason\n");
    printf("      --no-revert-out-of-scope  Report out-of-scope edits but do "
           "NOT revert them (--edit-scope stops being enforced)\n");
    printf("      --no-preserve-discarded   Do not keep the state `undo` "
           "discards -- no `jichi recover` handle, the revert is final\n");
    printf("      --strict-scope     With --edit-scope, also forbid "
           "run_terminal_command (no shell escape)\n");
    printf("      --privileged-commands <ask|deny|allow>  Policy for "
           "model-issued sudo/doas/pkexec (default ask; config "
           "\"privilegedCommands\")\n");
    printf("      --kinetic-commands <ask|deny|allow>  Policy for "
           "model-issued physical actuation (default ask; config "
           "\"kineticCommands\")\n");
    printf("      --revert-out-of-scope  Restore files changed outside the "
           "edit scope at turn end (config \"revertOutOfScope\")\n");
    printf("      --strict-green     Refuse a passing verify if the run changed "
           "a file outside the edit scope (config \"strictGreen\")\n");
    printf("      --lease MODE       Concurrent runs on one workspace: "
           "warn (default) | fail | off (M431e)\n");
    printf("      --budget-panel     Give the agent a periodic budget reading "
           "with its spend RATE (off by default; config \"budgetPanel\")\n");
    printf("      --preserve-discarded  Commit the tree a rollback would throw "
           "away, under refs/jichi/discarded/ (config \"preserveDiscarded\")\n");
    printf("      --parallel-verify  Verify each parallel write-child's "
           "worktree before merging it (config \"parallelVerify\")\n");
    printf("      --budget-tokens <n>   Token cap for the run (e.g. 200k, 1m)\n");
    printf("      --deadline <dur>   Wall-clock cap (e.g. 30s, 20m, 2h)\n");
    printf("      --max-tool-calls <n>  Cap on tool calls ATTEMPTED (a\n");
    printf("                            gate-refused call still counts)\n");
    printf("      --max-reads <n>       Cap on read-category tool calls (prevents over-reading)\n");
    printf("      --context-limit <n>   Context budget in tokens (sizes the "
           "system-prompt fit + compaction; use when the model's real context "
           "is smaller than its declared contextLength)\n");
    printf("      --tool-profile <p>    auto | core | full -- advertise the lean "
           "core tool set on a small-context model (auto: core when context is "
           "small or --lite)\n");
    printf("      --edit-scope <glob>   Restrict file edits to a path glob "
           "(repeatable)\n");
    printf("      --reference-root <path>  Allow READS under this external "
           "root with the fence on (repeatable; writes stay in-workspace)\n");
    printf("      --journal <path|->    Audit log path "
           "(default ~/.jichi.d/runs/<id>.jsonl; - disables)\n");
    printf("      --control [path]      Open a mid-run control socket "
           "(default ~/.jichi.d/control/<pid>.sock;\n");
    printf("                            steer with `jichi control "
           "<sock> status|inject|pause|resume|abort|mode`)\n");
    printf("\nEvent logging / telemetry (metrics on by default; what `learn "
           "analyze` reads):\n");
    printf("      --log-level <tier>    off | metrics | full "
           "(default metrics; full adds prompt/response content)\n");
    printf("      --log <path|->        Telemetry JSONL path "
           "(default ~/.jichi.d/telemetry/<workspace>-<key>.jsonl; - disables;\n");
    printf("                            implies metrics if no --log-level)\n");
    printf("\nModel routing (fast-first, escalate to strong on a hard step):\n");
    printf("      --route-fast <model>    Routine turns run on this model\n");
    printf("      --route-strong <model>  Escalate to this model on a verify "
           "failure\n");
    printf("      --no-route              Disable routing (use the single "
           "active model)\n");
    printf("      --route-on-stall        Escalate to the strong model when "
           "the fast one stalls (default)\n");
    printf("      --no-route-on-stall     Don't escalate on a model stall\n");
    printf("      --route-on-context <pct> Escalate when the history reaches "
           "pct%% of the\n");
    printf("                              fast tier's window (default 75; "
           "needs a roomier\n");
    printf("                              strong tier to have any effect)\n");
    printf("      --no-route-on-context   Don't escalate on context pressure\n");
    printf("      --timeout-connect <s>   Model-call connect timeout, seconds "
           "(0/off disables)\n");
    printf("      --timeout-stall <s>     Abort a model stream stalled for this "
           "many seconds\n");
    printf("      --timeout-request <s>   Hard cap on a whole model call, "
           "seconds (0/off = none)\n");
    printf("      --run-timeout <s>       Wall-clock cap on a model-run "
           "shell command, seconds (0 = none)\n");
    printf("      --no-markdown           Don't render markdown/syntax in the "
           "TUI (raw text)\n");
    printf("      --type-ahead            Collect keys typed while the agent "
           "works, apply at its next step\n");
    printf("      --no-type-ahead         Don't (the default; see "
           "docs/TYPE_AHEAD.md)\n");
    printf("      --accessible            Reduce motion, keep output "
           "screen-reader-friendly (docs/ACCESSIBILITY.md)\n");
    printf("      --no-fuzzy-edit         Require exact-string edits (disable "
           "whitespace/anchor fallback)\n");
    printf("      --bell                  Ring the terminal bell when a turn / "
           "--auto run finishes\n");
    printf("      --notify <cmd>          Run <cmd> on completion ($JICHI_NOTIFY, "
           "$JICHI_CWD)\n");
    printf("\nWith no prompt and a non-terminal stdin, the piped input is the "
           "prompt; with a prompt, piped stdin is appended as context.\n");
    printf("\nCommands:\n");
    printf("  ls [--all]             List saved sessions (this project; "
           "--all for every project)\n");
    printf("  prune [--keep N] [--older-than DUR] [--dry-run]\n");
    printf("                         Delete old sessions AND dreams (both "
           "criteria must agree; --dry-run lists)\n");
    printf("  export [id] [--html]   Export a session transcript (Markdown, "
           "--html, or --output\n");
    printf("                         json for a structured projection); "
           "-o <file>\n");
    printf("  index [--reindex]      Build/update the codebase search index\n");
    printf("  docs [index|search]    List/index/search external documentation "
           "sources\n");
    printf("  setup                  Guided project setup: role preset -> "
           "assets + config +\n");
    printf("                         start-script + validation (interactive; "
           "--list, --preset)\n");
    printf("                         `--api-base <URL>` sets the endpoint a "
           "locally hosted or\n");
    printf("                         gateway model needs (the flag form of the "
           "wizard's\n");
    printf("                         apiBase question);\n");
    printf("                         `--context-length <tokens|auto>` records "
           "the model's\n");
    printf("                         window (a number stays offline; `auto` "
           "asks the server,\n");
    printf("                         which only a LiteLLM gateway answers);\n");
    printf("                         `setup --import <continue|opencode config>`"
           " converts an\n");
    printf("                         existing config into a jichi config + .jichi/ "
           "tree;\n");
    printf("                         `--journey`/`--machine`/`--stance`"
           " <name> layer onto it\n");
    printf("                         (setup asks for both; --preset <journey>"
           " alone still works);\n");
    printf("                         `setup --advisor` asks the configured model"
           " for tailored\n");
    printf("                         recommendations (writes .jichi/"
           "setup.advice.md);\n");
    printf("                         `setup --from-global` seeds the project "
           "config from\n");
    printf("                         ~/.jichi (--inherit <keys> copies "
           "only some);\n");
    printf("                         `setup --onboard` scaffolds + drafts a "
           "propose-only\n");
    printf("                         project analysis + tutorial (W6)\n");
    printf("  init [pack ...]        Scaffold .jichi/ assets (agents, skills, "
           "commands, AGENTS.md);\n");
    printf("                         --list packs, --global, --force, "
           "--dry-run\n");
    printf("  embed \"text\"           Print an embedding (uses the embed model)\n");
    printf("  rerank \"q\" \"doc\"...    Score documents against a query\n");
    printf("  doctor                 Check config, keys, server reachability, "
           "MCP/LSP, git\n");
    printf("                         (--output json for a machine-readable "
           "report;\n");
    printf("                         --live makes ONE real request that "
           "advertises a trivial\n");
    printf("                         tool, to confirm tool calling works "
           "end to end\n");
    printf("                         --unattended judges the config as an "
           "unattended-loop\n");
    printf("                         posture and exits 1 on an unsafe one)\n");
    printf("  config [path|show|validate]  Name the config file(s) in effect, "
           "summarise the\n");
    printf("                         resolved settings, or confirm it parsed; "
           "`config set <key>\n");
    printf("                         <value>` and `config telemetry <level>` "
           "edit it when\n");
    printf("                         configEditable is on\n");
    printf("  assign <spec.md>       Render a machine-checkable assignment for "
           "its audience\n");
    printf("  hint <spec.md> [N]     Print rung N of the spec's hint ladder "
           "(default 1)\n");
    printf("  grade <spec.md>        Run the spec's verify command and score "
           "pass/fail\n");
    printf("                         (--expect-fail: authoring check -- exit 0 "
           "iff the gate\n");
    printf("                         is RED on the untouched tree; a green one "
           "is HOLLOW)\n");
    printf("  attempt <spec.md>      Let a learner solve one assignment in an "
           "isolated worktree,\n");
    printf("                         graded by its verify (--agent <profile> "
           "picks a tier;\n");
    printf("                         --budget-tokens/--deadline/--max-tool-calls "
           "bound it;\n");
    printf("                         a green verify with test-assertion edits "
           "reports TAINTED,\n");
    printf("                         exit 1 -- --keep-worktree keeps the sandbox "
           "for review)\n");
    printf("  dream [path]           Propose-only reflection over telemetry -> "
           "a dated draft\n");
    printf("  workflow <spec.json>   Run a deterministic multi-agent pipeline "
           "(map -> synthesize)\n");
    printf("  improve [specs-dir]    Reflect + grade a spec suite; track the "
           "pass-rate over time\n");
    printf("                         (--attempt: let the agent try each failure "
           "in an isolated\n");
    printf("                         worktree and re-grade -- your tree is "
           "never touched)\n");
    printf("  models                 List configured models, roles, fallback, "
           "and live reachability\n");
    /* M297: these three were dispatched, worked, and appeared in NEITHER --help
     * nor the man page -- undiscoverable except by reading main.c. A working
     * feature nobody can find is a feature that was not shipped. */
    printf("  benchmark [path]       Score a config against the built-in "
           "checks (offline)\n");
    printf("  board [add|move|done|phase] ...\n");
    printf("                         Show or edit the persistent kanban board "
           "(.jichi/board.json)\n");
    printf("  packages [recommend]   List the setup presets (role recipes) "
           "`setup --preset` accepts;\n");
    printf("                         `recommend` suggests one from what is in "
           "the project\n");
    printf("  timeouts               Show the model-call timeouts in effect "
           "for the active model\n");
    /* M297: the verbs were dispatched but named nowhere in --help. */
    printf("  mcp [resources|read <uri>|prompts|prompt <name>|call <tool> "
           "[json]]\n");
    printf("                         Connect to configured MCP servers; list "
           "tools, or use one\n");
    printf("  skills                 List available agent skills\n");
    printf("  agents                 List discovered agent profiles "
           "(.jichi/agents)\n");
    printf("  commands               List custom slash commands "
           "(.jichi/commands)\n");
    printf("  output-styles          List output styles "
           "(.jichi/output-styles)\n");
    printf("  rules                  Print the resolved project rules "
           "(AGENTS.md chain)\n");
    printf("  assignments            List assignments under docs/assignments/ "
           "(see `init assignments`)\n");
    printf("  sysmsg                 Print the resolved system prompt "
           "(debugging)\n");
    printf("  status                 Print the resolved session config "
           "(model, mode, routing, cwd)\n");
    printf("  describe               Print jichi's interface contract for "
           "driving agents (--output json)\n");
    printf("  map                    Print the repository map (files + "
           "top-level symbols)\n");
    printf("  context                Print the context-budget breakdown "
           "(prompt/tools vs limit)\n");
    printf("  context tools          Per-tool definition sizes, largest first, "
           "joined to telemetry (paid-for vs called)\n");
    printf("  context history [<id>] Where a saved session's history went: by "
           "role, by tool, largest messages\n");
    printf("  memory                 Print persisted agent memory "
           "(.jichi/memory.md)\n");
    printf("  glossary               Print the domain-term glossary "
           "(.jichi/glossary.md)\n");
    printf("  brief-check <file|->  Pre-flight a brief with NO model call: which "
           "constraints it would\n                        infer and from which line, "
           "the envelope, and the gate's\n                        baseline colour "
           "(exit 1 if a declared goal gate forces nothing)\n");
    printf("  constraints [scan <f>] List the enforced constraints, or predict "
           "what a\n");
    printf("                         brief/prompt would get ADOPTED "
           "(offline; exit 1 if any)\n");
    printf("  telemetry [path]       Summarize a telemetry log "
           "(default: newest under ~/.jichi.d/telemetry;\n");
    printf("                         --workspace <path> filters to one "
           "project's events;\n");
    printf("                         --since 7d windows a log that spans older "
           "builds;\n");
    printf("                         --cache-audit diagnoses prompt-cache reuse "
           "vs re-billing)\n");
    printf("  audit [path]           Summarize the privileged-command audit "
           "log (--since 7d limits the window;\n");
    printf("                         --output json for a machine-readable "
           "summary)\n");
    printf("  runs [dir]             One triage row per bounded run's journal "
           "(newest first; --all shows every run;\n");
    printf("                         --since 1d windows by activity; --output "
           "json for machine-readable rows)\n");
    printf("  control <sock> <verb>  Steer a running --control run: status | "
           "inject <text> | pause [--extend] |\n");
    printf("                         resume | abort | mode <chat|plan>  "
           "(--extend credits the paused\n");
    printf("                         time back to the deadline; mode only ever "
           "NARROWS the posture)\n");
    printf("  learn analyze [path]   Mine telemetry + recent sessions for "
           "recurring problems (offline)\n");
    printf("  learn apply            Commit a reviewed .jichi/lessons.draft.md "
           "to memory + skills\n");
    printf("  learn corrections      Commit ONLY the draft's corrections "
           "(retract stale notes)\n");
    printf("  learn review           How to run the mentor (it needs a model, "
           "so it is a command)\n");
    /* The command name is backticked so `/learn` reads as a unit: an undelimited
     * `/learn <word>` is a SUBCOMMAND invocation as far as the M295 lint is
     * concerned, and "the /learn command" would be read as a subcommand `command`.
     * Most of the tree already writes it this way. */
    printf("                         (the mentor draft comes from the `/learn` "
           "command)\n");
    printf("  complete [text]        Autocomplete the text (or stdin) with the "
           "autocomplete-role model\n");
    printf("  fim <file> <offset>    Fill-in-the-middle at a byte offset (or "
           "JSON {prefix,suffix} on stdin)\n");
    printf("  test [command]         Run the tests (default: configured "
           "testCommand/verify) and print a parsed summary\n");
    printf("  lsp <file> | symbols <file> | def|refs <file> <symbol> | "
           "actions <file> <line>\n");
    printf("                         Diagnostics, or LSP code navigation\n");
    printf("  serve [--acp]          Run as an ACP agent server over "
           "stdin/stdout (for editors)\n");
    printf("  daemon [--socket P]    Warm process: keep config/MCP/LSP/index "
           "hot and serve\n");
    printf("                         requests over a Unix socket. Talk to it "
           "with:\n");
    printf("                         jichi --connect <socket> -p "
           "\"...\"\n");
    printf("                         (--idle-dream <sec>: reflect over telemetry "
           "when idle)\n");
    printf("  checkpoints            List this workspace's snapshots\n");
    printf("  checkpoints gc         Report the shadow-checkpoint store and "
           "what is orphaned\n");
    /* M539: --yes was accepted and documented nowhere; it removes the
     * confirmation on a command that DELETES checkpoint stores, and there is no
     * undo for a pruned store. */
    printf("                         (--yes to actually delete; there is no "
           "undo for a pruned store)\n");
    printf("  attempts               List work a rollback discarded and "
           "preserved (see --preserve-discarded)\n");
    printf("  recover <commit>       Materialise a preserved attempt into a "
           "worktree (--into <dir>)\n");
    printf("  undo [N] [--dry-run]   Revert the workspace to the N-th most "
           "recent checkpoint\n");
    printf("                         (--dry-run previews the change without "
           "applying it)\n");
    printf("  rewind [N] [--dry-run] Revert files AND the conversation to the "
           "N-th checkpoint's turn\n");
    printf("  -V, --version          Print version and exit\n");
    printf("  -h, --help             Print this help and exit\n");
    printf("\nExit codes: 0 ok, 1 error, 2 usage, 130 interrupted (SIGINT), "
           "143 terminated (SIGTERM, graceful).\n");
}

#define JC_MAX_POS 64

struct cli_args {
    const char *config_path;
    const char *config_json;     /* --config-json: inline config, no file    */
    const char *config_json_b64; /* --config-json-b64: base64 of inline config*/
    int         config_stdin;    /* --config-stdin / --config-json - : stdin  */
    const char *prompt_b64;      /* --prompt-b64: base64 of the prompt text   */
    const char *model_override;
    const char *print_prompt;
    int         auto_approve;
    int         plan;
    int         readonly;
    int         resume;
    const char *session_id;          /* --session <id|prefix>             */
    int         all;                 /* --all (session scope)             */
    int         verbose;
    int         quiet;               /* -q/--quiet                        */
    int         no_session;          /* --no-session                      */
    int         no_stdin;            /* --no-stdin                        */
    int         output_json;         /* --output json (0 => text)         */
    const char *output_style;        /* --output-style <name>, or NULL    */
    const char *language;            /* --language <lang>, or NULL (M135) */
    const char *images[16];          /* --image <path> (repeatable, M29)  */
    int         nimages;
    int         reindex;             /* --reindex (index subcommand)      */
    int         dry_run;             /* --dry-run (undo/init subcommands) */
    long        prune_keep;          /* prune --keep <n> (M219; -1 = unset) */
    const char *prune_older;         /* prune --older-than <dur> (M219)     */
    int         init_global;         /* --global (init: ~/.config target) */
    int         force;               /* --force (init: overwrite)         */
    int         list;                /* --list (init/setup: enumerate)    */
    /* setup wizard (M48) */
    const char *setup_preset;        /* --preset <role>                   */
    const char *setup_provider;      /* --provider <p>                    */
    const char *setup_journey;       /* --journey <name> (M326j): the optional
                                      * second dimension, layered on --preset */
    const char *setup_machine;       /* --machine <name> (M326k): the third   */
    const char *setup_stance;        /* --stance <name> (M326m): the fourth.
                                      * Unset => none, NOT the interactive
                                      * default: a scripted setup must not
                                      * gain assignment scaffolding silently */
    const char *setup_api_base;      /* --api-base <URL> (M488)           */
    const char *setup_key_env;       /* --key-env <VAR> (setup model id is
                                      * the existing --model/model_override)*/
    const char *setup_context_length;/* --context-length <tokens|auto>: the
                                      * model's window. A NUMBER keeps setup
                                      * offline and deterministic (what an agent
                                      * driving another jichi wants when it
                                      * already knows); `auto` is an explicit
                                      * opt-in to one request. Distinct from
                                      * --context-limit, which is the top-level
                                      * budget: these mirror the two config
                                      * keys, contextLength and contextLimit. */
    const char *setup_lang;          /* --lang <pack> (developer/tester)  */
    const char *setup_profile;       /* --profile beginner|advanced|std (M116) */
    const char *setup_target;        /* --config-target local|global|path */
    const char *setup_import;        /* --import <continue/opencode config> */
    int         setup_advisor;       /* --advisor: LLM-tailored recommendations */
    int         setup_from_global;   /* --from-global: seed project cfg from ~/.jichi */
    const char *setup_inherit;       /* --inherit <keys>: subset of global keys to copy */
    int         setup_onboard;       /* --onboard: scaffold + propose-only project analysis (W6) */
    const char *daemon_connect;      /* --connect <socket>: talk to a daemon    */
    const char *daemon_socket;       /* --socket <path> (daemon subcommand)     */
    int         idle_dream;          /* --idle-dream <sec>: daemon idle reflection */
    int         improve_attempt;     /* --attempt: improve rehearses in worktrees */
    const char *attempt_agent;       /* --agent <profile> (attempt subcommand)  */
    int         attempt_keep;        /* --keep: keep the attempt worktree for
                                      * review (M410 -- a TAINTED verdict is
                                      * unreviewable if the evidence is gone) */
    int         expect_fail;         /* grade --expect-fail: the two-sided
                                      * proof's RED half -- succeed iff the
                                      * verify FAILS on the untouched tree
                                      * (M412; a gate that cannot fail grades
                                      * nothing) */
    int         non_interactive;     /* --non-interactive                 */
    const char *telemetry_workspace; /* --workspace <path> (telemetry filter, M56) */
    int         cache_audit;         /* --cache-audit (telemetry prompt-cache view) */
    const char *since;               /* --since <dur> (audit window, M158)   */
    int         unattended;          /* doctor --unattended profile (M158b)  */
    int         live;                /* doctor --live tool probe (M167)      */
    int         record;              /* grade --record -> progress JSONL (M173b) */
    int         control_flag;        /* --control given (M159)               */
    const char *control_path;        /* optional --control <path>, or NULL   */
    int         extend;              /* control pause --extend (M162)        */
    int         heartbeat_secs;      /* --heartbeat <secs> jsonl cadence (M165) */
    const char *verify_cmd;          /* --verify <cmd> (envelope)         */
    const char *budget_tokens;       /* --budget-tokens <n> (raw)         */
    const char *deadline;            /* --deadline <dur> (raw)            */
    int         max_tool_calls;      /* --max-tool-calls <n>              */
    int         max_reads;           /* --max-reads <n> (M98)             */
    int         verify_retries;      /* --verify-retries <n> (0 => def)   */
    int         verify_every;        /* --verify-every <n> (M81; 0 => off) */
    const char *verify_timeout;      /* --verify-timeout <dur> (raw)      */
    int         no_rollback;         /* --no-rollback                     */
    const char *privileged_commands; /* --privileged-commands ask|deny|allow  */
    const char *lease;             /* M431e: --lease warn|fail|off        */
    int         budget_panel;      /* M431f: 1 = on, -1 = off, 0 = config  */
    const char *kinetic_commands;    /* --kinetic-commands ask|deny|allow     */
    const char *recover_into;        /* --into <dir> for `recover`             */
    int         gc_yes;             /* M338: `checkpoints gc --yes` removes    */
    const char *dump_requests;      /* M341: --dump-requests <dir>             */
    int         preserve_disc;       /* --preserve-discarded: 1/-1/0 unset     */
    int         strict_green;        /* --strict-green: 1 on, -1 off, 0 unset  */
    int         revert_oos;          /* --revert-out-of-scope: 1 on, -1 off,
                                      * 0 unset (config decides)  (M142)  */
    int         parallel_verify;     /* --parallel-verify: 1 on, -1 off,
                                      * 0 unset (config decides)  (M144)  */
    int         strict_scope;        /* --strict-scope                    */
    int         verify_baseline;     /* --verify-baseline                 */
    const char *verify_kind;         /* --verify-kind <invariant|goal> (M343);
                                      * validated at parse -- a silently
                                      * mis-declared gate is the exact trap
                                      * the declaration exists to remove */
    const char *journal_path;        /* --journal <path|->                */
    const char *design_path[8];      /* --design/--spec <file>, repeatable */
    int         n_design_path;       /* (M462; appends to config `design`) */
    const char *log_path;            /* --log <path|->  (telemetry, M21)  */
    const char *log_level;           /* --log-level off|metrics|full      */
    long        context_limit;       /* --context-limit <n> (0 => unset)  */
    const char *tool_profile;        /* --tool-profile auto|core|full     */
    const char *edit_scope[32];      /* --edit-scope <glob> (repeatable)  */
    int         n_edit_scope;
    const char *reference_root[16];  /* --reference-root <path> (repeatable) */
    int         n_reference_root;
    const char *route_fast;          /* --route-fast <sel>                */
    const char *route_strong;        /* --route-strong <sel>              */
    int         no_route;            /* --no-route                        */
    int         route_on_stall;      /* --route-on-stall (force on)       */
    int         no_route_on_stall;   /* --no-route-on-stall               */
    int         route_on_context;    /* --route-on-context <pct> (-1 unset) */
    long        timeout_connect;     /* --timeout-connect secs; -1 unset  */
    long        timeout_stall;       /* --timeout-stall secs; -1 unset    */
    long        timeout_request;     /* --timeout-request secs; -1 unset  */
    int         review;              /* --review (force self-review on)   */
    int         no_self_review;      /* --no-self-review                  */
    int         path_fence;          /* --path-fence (force on)           */
    int         no_path_fence;       /* --no-path-fence (force off)       */
    int         prompt_cache;        /* --prompt-cache (force on)         */
    int         no_prompt_cache;     /* --no-prompt-cache (force off)     */
    int         cost_model;          /* --cost-model (force on, M440)     */
    int         no_cost_model;       /* --no-cost-model (force off)       */
    int         auto_context;        /* --auto-context (force on, M61)    */
    int         no_auto_context;     /* --no-auto-context (force off)     */
    int         learn_on_stop;       /* --learn-on-stop (M71)             */
    int         no_learn_on_stop;    /* --no-learn-on-stop                */
    int         no_hooks;            /* --no-hooks (disable lifecycle hooks) */
    int         config_editable;     /* --config-editable (allow config edits M112) */
    long        mem_budget_mb;       /* --mem-budget <MB> (0 = unset; M117)  */
    long        run_timeout;         /* --run-timeout <s> (0 = unset)        */
    int         accessible;          /* --accessible (reduce motion, M118)   */
    int         color;               /* --color (force ANSI color on)     */
    int         no_color;            /* --no-color                        */
    int         no_markdown;         /* --no-markdown (raw assistant text) */
    int         voice;               /* 0 unset, 1 --voice, -1 --no-voice  */
    int         attempt_with_rules;  /* M309: attempt loads project rules   */
    int         type_ahead;          /* 0 unset, 1 --type-ahead, -1 --no-  */
    int         fuzzy_edit;          /* --fuzzy-edit / --no-fuzzy-edit (0 unset) */
    int         acp;                 /* --acp (serve subcommand transport) */
    int         lite;                /* --lite/--low-memory: lean profile  */
    int         no_lite;             /* --no-lite: suppress auto-lite       */
    int         html;                /* --html (export subcommand)        */
    const char *out_path;            /* -o/--out <file> (export)          */
    int         bell;                /* --bell: terminal bell on completion */
    const char *notify;              /* --notify <cmd>: run on completion   */
    const char *pos[JC_MAX_POS];     /* positional args, in order         */
    int         npos;
};

/* Parse a --timeout-* value: a non-negative whole number of seconds, or
 * "off"/"none" (=> 0, disabled). Writes seconds to *secs; returns 0 on a
 * malformed value. */
static int parse_timeout_arg(const char *s, long *secs)
{
    const char *p;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (strcmp(s, "off") == 0 || strcmp(s, "none") == 0) {
        *secs = 0;
        return 1;
    }
    for (p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    *secs = atol(s);
    return 1;
}

static int parse_args(int argc, char **argv, struct cli_args *out)
{
    int i;
    int endopts = 0; /* set by "--": all later args are positional */
    memset(out, 0, sizeof(*out));
    out->verify_retries = -1; /* sentinel: unset (0 is a valid explicit value) */
    out->prune_keep = -1;     /* sentinel: unset (0 = "keep none" is valid) */
    out->timeout_connect = -1; /* -1 unset; 0 (or "off") explicitly disables */
    out->timeout_stall = -1;
    out->timeout_request = -1;
    out->route_on_context = -1; /* -1 unset; 0 = --no-route-on-context (M288) */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!endopts && strcmp(a, "--") == 0) {
            endopts = 1;
            continue;
        }
        if (endopts) {
            if (out->print_prompt == NULL) {
                out->print_prompt = a;
            }
            if (out->npos < JC_MAX_POS) {
                out->pos[out->npos++] = a;
            }
            continue;
        }
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_help(argv[0]);
            return 1;
        } else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            print_version();
            return 1;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            out->verbose = 1;
        } else if (strcmp(a, "-q") == 0 || strcmp(a, "--quiet") == 0 ||
                   strcmp(a, "--silent") == 0) {
            out->quiet = 1;
        } else if (strcmp(a, "--no-session") == 0) {
            out->no_session = 1;
        } else if (strcmp(a, "--no-stdin") == 0) {
            out->no_stdin = 1;
        } else if (strcmp(a, "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --output requires an argument\n");
                return -1;
            }
            if (jc_output_format_parse(argv[++i], &out->output_json) != 0) {
                fprintf(stderr,
                        "error: --output must be 'text', 'json', or 'jsonl'\n");
                return -1;
            }
        } else if (strcmp(a, "--output-style") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --output-style requires a name\n");
                return -1;
            }
            out->output_style = argv[++i];
        } else if (strcmp(a, "--language") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --language requires a language name\n");
                return -1;
            }
            out->language = argv[++i];
        } else if (strcmp(a, "--image") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --image requires a path\n");
                return -1;
            }
            if (out->nimages < (int)(sizeof(out->images) /
                                     sizeof(out->images[0]))) {
                out->images[out->nimages++] = argv[++i];
            } else {
                i++; /* over the cap: consume and ignore */
            }
        } else if (strcmp(a, "--auto") == 0) {
            out->auto_approve = 1;
        } else if (strcmp(a, "--plan") == 0) {
            out->plan = 1;
        } else if (strcmp(a, "--readonly") == 0) {
            out->readonly = 1;
        } else if (strcmp(a, "--resume") == 0 ||
                   strcmp(a, "-c") == 0 || strcmp(a, "--continue") == 0) {
            out->resume = 1;
        } else if (strcmp(a, "--session") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --session requires an id\n");
                return -1;
            }
            out->session_id = argv[++i];
        } else if (strcmp(a, "--all") == 0) {
            out->all = 1;
        } else if (strcmp(a, "--reindex") == 0) {
            out->reindex = 1;
        } else if (strcmp(a, "--dry-run") == 0) {
            out->dry_run = 1;
        } else if (strcmp(a, "--global") == 0) {
            out->init_global = 1;
        } else if (strcmp(a, "--force") == 0) {
            out->force = 1;
        } else if (strcmp(a, "--list") == 0) {
            out->list = 1;
        } else if (strcmp(a, "-p") == 0 || strcmp(a, "--print") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires an argument\n", a);
                return -1;
            }
            /* M375: refuse a flag-shaped prompt. `jichi -p --no-session
             * "question"` once sent the model the FLAG as its prompt and
             * dropped the question as an ignored positional. "-" (stdin)
             * and "- bullet"-style text stay valid; a prompt genuinely
             * starting with an option-like '-' has spellings that say so. */
            if (argv[i + 1][0] == '-' &&
                (argv[i + 1][1] == '-' ||
                 isalnum((unsigned char)argv[i + 1][1]))) {
                fprintf(stderr, "error: %s took '%s', which looks like a "
                        "flag, not a prompt\n", a, argv[i + 1]);
                fprintf(stderr, "  put the prompt text directly after %s; "
                        "for text that really starts with '-', use "
                        "--prompt-b64 or a positional prompt after --\n", a);
                return -1;
            }
            out->print_prompt = argv[++i];
        } else if (strcmp(a, "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --config requires an argument\n");
                return -1;
            }
            out->config_path = argv[++i];
        } else if (strcmp(a, "--config-json") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --config-json requires an argument\n");
                return -1;
            }
            /* "--config-json -" reads the config JSON from stdin. */
            if (strcmp(argv[i + 1], "-") == 0) {
                out->config_stdin = 1;
                i++;
            } else {
                out->config_json = argv[++i];
            }
        } else if (strcmp(a, "--config-json-b64") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "error: --config-json-b64 requires an argument\n");
                return -1;
            }
            out->config_json_b64 = argv[++i];
        } else if (strcmp(a, "--config-stdin") == 0) {
            out->config_stdin = 1;
        } else if (strcmp(a, "--prompt-b64") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --prompt-b64 requires an argument\n");
                return -1;
            }
            out->prompt_b64 = argv[++i];
        } else if (strcmp(a, "--design") == 0 || strcmp(a, "--spec") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires a file argument\n", a);
                return -1;
            }
            if (out->n_design_path <
                (int)(sizeof(out->design_path) / sizeof(out->design_path[0]))) {
                out->design_path[out->n_design_path++] = argv[++i];
            } else {
                fprintf(stderr, "warning: %s: at most %d design docs; "
                        "ignoring %s\n", a,
                        (int)(sizeof(out->design_path) /
                              sizeof(out->design_path[0])), argv[++i]);
            }
        } else if (strcmp(a, "--model") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --model requires an argument\n");
                return -1;
            }
            out->model_override = argv[++i];
        } else if (strcmp(a, "--preset") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --preset requires an argument\n");
                return -1;
            }
            out->setup_preset = argv[++i];
        } else if (strcmp(a, "--journey") == 0 && i + 1 < argc) {
            out->setup_journey = argv[++i];
        } else if (strcmp(a, "--machine") == 0 && i + 1 < argc) {
            out->setup_machine = argv[++i];
        } else if (strcmp(a, "--stance") == 0 && i + 1 < argc) {
            out->setup_stance = argv[++i];
        } else if (strcmp(a, "--provider") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --provider requires an argument\n");
                return -1;
            }
            out->setup_provider = argv[++i];
        } else if (strcmp(a, "--key-env") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --key-env requires an argument\n");
                return -1;
            }
            out->setup_key_env = argv[++i];
        } else if (strcmp(a, "--context-length") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --context-length requires an argument "
                                "(a token count, or `auto`)\n");
                return -1;
            }
            out->setup_context_length = argv[++i];
        } else if (strcmp(a, "--api-base") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --api-base requires an argument\n");
                return -1;
            }
            out->setup_api_base = argv[++i];
        } else if (strcmp(a, "--lang") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --lang requires an argument\n");
                return -1;
            }
            out->setup_lang = argv[++i];
        } else if (strcmp(a, "--profile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --profile requires an argument\n");
                return -1;
            }
            out->setup_profile = argv[++i];
        } else if (strcmp(a, "--config-target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --config-target requires an argument\n");
                return -1;
            }
            out->setup_target = argv[++i];
        } else if (strcmp(a, "--import") == 0 || strcmp(a, "--from") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --import requires a config path\n");
                return -1;
            }
            out->setup_import = argv[++i];
        } else if (strcmp(a, "--advisor") == 0) {
            out->setup_advisor = 1;
        } else if (strcmp(a, "--onboard") == 0) {
            out->setup_onboard = 1;
        } else if (strcmp(a, "--from-global") == 0) {
            out->setup_from_global = 1;
        } else if (strcmp(a, "--inherit") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --inherit requires a key list "
                                "(e.g. models,routing)\n");
                return -1;
            }
            out->setup_from_global = 1; /* --inherit implies --from-global */
            out->setup_inherit = argv[++i];
        } else if (strcmp(a, "--attempt") == 0) {
            out->improve_attempt = 1;
        } else if (strcmp(a, "--agent") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --agent requires a profile name\n");
                return -1;
            }
            out->attempt_agent = argv[++i];
        } else if (strcmp(a, "--keep-worktree") == 0) {
            /* NOT `--keep`: prune owns that spelling and takes a value --
             * a bare `--keep` here would shadow `prune --keep N`. */
            out->attempt_keep = 1;
        } else if (strcmp(a, "--expect-fail") == 0) {
            out->expect_fail = 1;
        } else if (strcmp(a, "--connect") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --connect requires a socket path\n");
                return -1;
            }
            out->daemon_connect = argv[++i];
        } else if (strcmp(a, "--socket") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --socket requires a path\n");
                return -1;
            }
            out->daemon_socket = argv[++i];
        } else if (strcmp(a, "--idle-dream") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --idle-dream requires seconds\n");
                return -1;
            }
            out->idle_dream = atoi(argv[++i]);
        } else if (strcmp(a, "--workspace") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --workspace requires a path\n");
                return -1;
            }
            out->telemetry_workspace = argv[++i];
        } else if (strcmp(a, "--cache-audit") == 0) {
            out->cache_audit = 1;
        } else if (strcmp(a, "--since") == 0) {
            long tmp;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --since requires a duration\n");
                return -1;
            }
            out->since = argv[++i];
            if (jc_env_parse_duration(out->since, &tmp) != 0) {
                fprintf(stderr, "error: --since must be a duration like "
                                "30m / 12h / 7d\n");
                return -1;
            }
        } else if (strcmp(a, "--unattended") == 0) {
            out->unattended = 1;
        } else if (strcmp(a, "--live") == 0) {
            out->live = 1;
        } else if (strcmp(a, "--record") == 0) {
            out->record = 1;
        } else if (strcmp(a, "--heartbeat") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --heartbeat requires a seconds value\n");
                return -1;
            }
            out->heartbeat_secs = atoi(argv[++i]);
            if (out->heartbeat_secs < 0) {
                out->heartbeat_secs = 0;
            }
        } else if (strcmp(a, "--control") == 0) {
            /* Optional value: a following token that doesn't look like a
             * flag is the socket path; else the default path is used. */
            out->control_flag = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                out->control_path = argv[++i];
            }
        } else if (strcmp(a, "--extend") == 0) {
            out->extend = 1; /* `control <sock> pause --extend` (M162) */
        } else if (strcmp(a, "--non-interactive") == 0) {
            out->non_interactive = 1;
        } else if (strcmp(a, "--verify") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --verify requires a command\n");
                return -1;
            }
            out->verify_cmd = argv[++i];
        } else if (strcmp(a, "--budget-tokens") == 0) {
            double tmp;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --budget-tokens requires a value\n");
                return -1;
            }
            out->budget_tokens = argv[++i];
            if (jc_env_parse_size(out->budget_tokens, &tmp) != 0) {
                fprintf(stderr, "error: --budget-tokens must be a number with "
                                "an optional k/m suffix\n");
                return -1;
            }
        } else if (strcmp(a, "--deadline") == 0) {
            long tmp;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --deadline requires a value\n");
                return -1;
            }
            out->deadline = argv[++i];
            if (jc_env_parse_duration(out->deadline, &tmp) != 0) {
                fprintf(stderr, "error: --deadline must be a duration like "
                                "30s/20m/2h\n");
                return -1;
            }
        } else if (strcmp(a, "--max-tool-calls") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --max-tool-calls requires a value\n");
                return -1;
            }
            out->max_tool_calls = atoi(argv[++i]);
        } else if (strcmp(a, "--max-reads") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --max-reads requires a value\n");
                return -1;
            }
            out->max_reads = atoi(argv[++i]);
        } else if (strcmp(a, "--context-limit") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --context-limit requires a value\n");
                return -1;
            }
            out->context_limit = atol(argv[++i]);
            if (out->context_limit <= 0) {
                fprintf(stderr, "error: --context-limit must be a positive "
                                "token count\n");
                return -1;
            }
        } else if (strcmp(a, "--tool-profile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --tool-profile requires a value "
                                "(auto|core|full)\n");
                return -1;
            }
            out->tool_profile = argv[++i];
            if (strcmp(out->tool_profile, "auto") != 0 &&
                strcmp(out->tool_profile, "core") != 0 &&
                strcmp(out->tool_profile, "full") != 0) {
                fprintf(stderr, "error: --tool-profile must be auto, core, "
                                "or full\n");
                return -1;
            }
        } else if (strcmp(a, "--verify-retries") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --verify-retries requires a value\n");
                return -1;
            }
            out->verify_retries = atoi(argv[++i]);
        } else if (strcmp(a, "--keep") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --keep requires a value\n");
                return -1;
            }
            out->prune_keep = atol(argv[++i]);
        } else if (strcmp(a, "--older-than") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --older-than requires a value\n");
                return -1;
            }
            out->prune_older = argv[++i];
        } else if (strcmp(a, "--verify-every") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --verify-every requires a value\n");
                return -1;
            }
            out->verify_every = atoi(argv[++i]);
        } else if (strcmp(a, "--verify-timeout") == 0) {
            long tmp;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --verify-timeout requires a value\n");
                return -1;
            }
            out->verify_timeout = argv[++i];
            if (jc_env_parse_duration(out->verify_timeout, &tmp) != 0) {
                fprintf(stderr, "error: --verify-timeout must be a duration "
                                "like 30s/5m/1h\n");
                return -1;
            }
        } else if (strcmp(a, "--no-rollback") == 0) {
            out->no_rollback = 1;
        } else if (strcmp(a, "--budget-panel") == 0) {
            out->budget_panel = 1;
        } else if (strcmp(a, "--no-budget-panel") == 0) {
            out->budget_panel = -1;
        } else if (strcmp(a, "--lease") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --lease requires warn|fail|off\n");
                return -1;
            }
            out->lease = argv[++i];
        } else if (strcmp(a, "--privileged-commands") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --privileged-commands requires "
                                "ask|deny|allow\n");
                return -1;
            }
            out->privileged_commands = argv[++i];
        } else if (strcmp(a, "--kinetic-commands") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --kinetic-commands requires "
                                "ask|deny|allow\n");
                return -1;
            }
            out->kinetic_commands = argv[++i];
        } else if (strcmp(a, "--dump-requests") == 0 && i + 1 < argc) {
            out->dump_requests = argv[++i];
        } else if (strcmp(a, "--yes") == 0) {
            out->gc_yes = 1;
        } else if (strcmp(a, "--into") == 0 && i + 1 < argc) {
            out->recover_into = argv[++i];
        } else if (strcmp(a, "--preserve-discarded") == 0) {
            out->preserve_disc = 1;
        } else if (strcmp(a, "--no-preserve-discarded") == 0) {
            out->preserve_disc = -1;
        } else if (strcmp(a, "--strict-green") == 0) {
            out->strict_green = 1;
        } else if (strcmp(a, "--no-strict-green") == 0) {
            out->strict_green = -1;
        } else if (strcmp(a, "--revert-out-of-scope") == 0) {
            out->revert_oos = 1;
        } else if (strcmp(a, "--no-revert-out-of-scope") == 0) {
            out->revert_oos = -1;
        } else if (strcmp(a, "--parallel-verify") == 0) {
            out->parallel_verify = 1;
        } else if (strcmp(a, "--no-parallel-verify") == 0) {
            out->parallel_verify = -1;
        } else if (strcmp(a, "--strict-scope") == 0) {
            out->strict_scope = 1;
        } else if (strcmp(a, "--verify-baseline") == 0) {
            out->verify_baseline = 1;
        } else if (strcmp(a, "--verify-kind") == 0) {
            int k;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --verify-kind requires a value "
                                "(invariant|goal)\n");
                return -1;
            }
            out->verify_kind = argv[++i];
            if (!jc_env_verify_kind_parse(out->verify_kind, &k)) {
                fprintf(stderr, "error: --verify-kind: unknown kind '%s' "
                                "(use invariant or goal)\n", out->verify_kind);
                return -1;
            }
        } else if (strcmp(a, "--journal") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --journal requires a path\n");
                return -1;
            }
            out->journal_path = argv[++i];
        } else if (strcmp(a, "--log") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --log requires a path\n");
                return -1;
            }
            out->log_path = argv[++i];
        } else if (strcmp(a, "--log-level") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --log-level requires off|metrics|full\n");
                return -1;
            }
            out->log_level = argv[++i];
            if (jc_eventlog_level_parse(out->log_level) < 0) {
                fprintf(stderr, "error: --log-level must be off|metrics|full\n");
                return -1;
            }
        } else if (strcmp(a, "--edit-scope") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --edit-scope requires a glob\n");
                return -1;
            }
            /* A whitespace-containing pattern is almost always a mis-passed
             * multi-glob ("A B" in one arg) -- it matches nothing, so the
             * out-of-scope guard then flags genuinely in-scope files. --edit-
             * scope is repeatable; warn rather than silently mis-fence. */
            if (strpbrk(argv[i + 1], " \t") != NULL) {
                fprintf(stderr, "warning: --edit-scope pattern '%s' contains "
                        "whitespace; it will match nothing. --edit-scope is "
                        "repeatable -- pass each glob separately "
                        "(--edit-scope A --edit-scope B).\n", argv[i + 1]);
            }
            if (out->n_edit_scope <
                (int)(sizeof(out->edit_scope) / sizeof(out->edit_scope[0]))) {
                out->edit_scope[out->n_edit_scope++] = argv[++i];
            } else {
                i++; /* consume the value; extra patterns are ignored */
                fprintf(stderr, "warning: too many --edit-scope patterns; "
                                "ignoring extras\n");
            }
        } else if (strcmp(a, "--reference-root") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --reference-root requires a path\n");
                return -1;
            }
            if (out->n_reference_root <
                (int)(sizeof(out->reference_root) /
                      sizeof(out->reference_root[0]))) {
                out->reference_root[out->n_reference_root++] = argv[++i];
            } else {
                i++;
                fprintf(stderr, "warning: too many --reference-root paths; "
                                "ignoring extras\n");
            }
        } else if (strcmp(a, "--route-fast") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --route-fast requires a model\n");
                return -1;
            }
            out->route_fast = argv[++i];
        } else if (strcmp(a, "--route-strong") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --route-strong requires a model\n");
                return -1;
            }
            out->route_strong = argv[++i];
        } else if (strcmp(a, "--review") == 0) {
            out->review = 1;
        } else if (strcmp(a, "--no-self-review") == 0) {
            out->no_self_review = 1;
        } else if (strcmp(a, "--path-fence") == 0) {
            out->path_fence = 1;
        } else if (strcmp(a, "--no-path-fence") == 0) {
            out->no_path_fence = 1;
        } else if (strcmp(a, "--prompt-cache") == 0) {
            out->prompt_cache = 1;
        } else if (strcmp(a, "--no-prompt-cache") == 0) {
            out->no_prompt_cache = 1;
        } else if (strcmp(a, "--cost-model") == 0) {
            out->cost_model = 1;
        } else if (strcmp(a, "--no-cost-model") == 0) {
            out->no_cost_model = 1;
        } else if (strcmp(a, "--auto-context") == 0) {
            out->auto_context = 1;
        } else if (strcmp(a, "--no-auto-context") == 0) {
            out->no_auto_context = 1;
        } else if (strcmp(a, "--learn-on-stop") == 0) {
            out->learn_on_stop = 1;
        } else if (strcmp(a, "--no-learn-on-stop") == 0) {
            out->no_learn_on_stop = 1;
        } else if (strcmp(a, "--no-hooks") == 0) {
            out->no_hooks = 1;
        } else if (strcmp(a, "--config-editable") == 0) {
            out->config_editable = 1;
        } else if (strcmp(a, "--accessible") == 0) {
            out->accessible = 1;
        } else if (strcmp(a, "--mem-budget") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --mem-budget requires an argument (MB)\n");
                return -1;
            }
            out->mem_budget_mb = atol(argv[++i]);
        } else if (strcmp(a, "--run-timeout") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --run-timeout requires an argument "
                                "(seconds)\n");
                return -1;
            }
            out->run_timeout = atol(argv[++i]);
        } else if (strcmp(a, "--color") == 0) {
            out->color = 1;
        } else if (strcmp(a, "--no-color") == 0) {
            out->no_color = 1;
        } else if (strcmp(a, "--no-route") == 0) {
            out->no_route = 1;
        } else if (strcmp(a, "--route-on-stall") == 0) {
            out->route_on_stall = 1;
        } else if (strcmp(a, "--no-route-on-stall") == 0) {
            out->no_route_on_stall = 1;
        } else if (strcmp(a, "--route-on-context") == 0) {
            /* M288: a percentage of the fast tier's window. */
            char *end = NULL;
            long v;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --route-on-context requires a "
                                "percentage (1-99)\n");
                return -1;
            }
            v = strtol(argv[++i], &end, 10);
            if (end == NULL || *end != '\0' || v < 1 || v > 99) {
                fprintf(stderr, "error: --route-on-context requires a "
                                "percentage (1-99)\n");
                return -1;
            }
            out->route_on_context = (int)v;
        } else if (strcmp(a, "--no-route-on-context") == 0) {
            out->route_on_context = 0;
        } else if (strcmp(a, "--timeout-connect") == 0) {
            if (i + 1 >= argc ||
                !parse_timeout_arg(argv[++i], &out->timeout_connect)) {
                fprintf(stderr, "error: --timeout-connect requires seconds "
                                "(>= 0) or 'off'\n");
                return -1;
            }
        } else if (strcmp(a, "--timeout-stall") == 0) {
            if (i + 1 >= argc ||
                !parse_timeout_arg(argv[++i], &out->timeout_stall)) {
                fprintf(stderr, "error: --timeout-stall requires seconds "
                                "(>= 0) or 'off'\n");
                return -1;
            }
        } else if (strcmp(a, "--timeout-request") == 0) {
            if (i + 1 >= argc ||
                !parse_timeout_arg(argv[++i], &out->timeout_request)) {
                fprintf(stderr, "error: --timeout-request requires seconds "
                                "(>= 0) or 'off'\n");
                return -1;
            }
        } else if (strcmp(a, "--no-markdown") == 0) {
            out->no_markdown = 1;
        } else if (strcmp(a, "--with-rules") == 0) {
            out->attempt_with_rules = 1;
        } else if (strcmp(a, "--voice") == 0) {
            out->voice = 1;
        } else if (strcmp(a, "--no-voice") == 0) {
            out->voice = -1;
        } else if (strcmp(a, "--type-ahead") == 0) {
            out->type_ahead = 1;
        } else if (strcmp(a, "--no-type-ahead") == 0) {
            out->type_ahead = -1;
        } else if (strcmp(a, "--fuzzy-edit") == 0) {
            out->fuzzy_edit = 1;
        } else if (strcmp(a, "--no-fuzzy-edit") == 0) {
            out->fuzzy_edit = -1;
        } else if (strcmp(a, "--acp") == 0) {
            out->acp = 1;
        } else if (strcmp(a, "--lite") == 0 ||
                   strcmp(a, "--low-memory") == 0) {
            out->lite = 1;
        } else if (strcmp(a, "--no-lite") == 0) {
            out->no_lite = 1;
        } else if (strcmp(a, "--html") == 0) {
            out->html = 1;
        } else if (strcmp(a, "--bell") == 0) {
            out->bell = 1;
        } else if (strcmp(a, "--notify") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --notify requires a command\n");
                return -1;
            }
            out->notify = argv[++i];
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires a file path\n", a);
                return -1;
            }
            out->out_path = argv[++i];
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            return -1;
        } else {
            if (out->print_prompt == NULL) {
                out->print_prompt = a;
            }
            if (out->npos < JC_MAX_POS) {
                out->pos[out->npos++] = a;
            }
        }
    }
    return 0;
}

/* ----- headless front-end callbacks ----------------------------------- */

/* Shared state for the headless callbacks (cb.user). */
struct hl_ctx {
    struct jc_app *app;
    int    quiet;        /* suppress stderr diagnostics      */
    int    verbose;      /* -v: also print raw tool arguments (M326h)        */
    int    had_text;     /* this assistant message streamed text (M326h)     */
    int    json;         /* buffer (json) or stream events (jsonl): no plain out */
    int    jsonl;        /* --output jsonl: one JSON event per line (M63)    */
    double in_tok;       /* accumulated input tokens         */
    double out_tok;      /* accumulated output tokens        */
    double cost_total;   /* accumulated estimated cost (USD)  */
    int    tool_calls;   /* count of tool invocations        */
    int    broken_pipe;  /* downstream stdout closed early   */
    /* M97: run economics for a driving agent (surfaced in the terminal result). */
    double peak_input;   /* largest single-call input tokens (the ramp signal) */
    double cache_read;   /* accumulated cache-read tokens     */
    double cache_write;  /* accumulated cache-write tokens    */
    int    tc_read;      /* tool mix: READ-category calls     */
    int    tc_write;     /* WRITE-category */
    int    tc_shell;     /* SHELL-category */
    int    tc_other;     /* OTHER-category */
    /* M165: jsonl heartbeat (--heartbeat <secs>; 0 = off). */
    long   hb_interval_ms; /* throttle interval; 0 disables            */
    double hb_start_ms;    /* jc_now_millis at run start                */
    double hb_last_ms;     /* last heartbeat emitted (0 = none yet)     */
};

/* Print one compact JSON object + newline to stdout (jsonl event framing),
 * freeing it. Marks broken_pipe + aborts if the downstream closed. */
static void hl_emit(struct hl_ctx *c, cJSON *o)
{
    char *s;
    if (o == NULL) {
        return;
    }
    s = jc_json_print(o);
    cJSON_Delete(o);
    if (s == NULL) {
        return;
    }
    if (!c->broken_pipe) {
        if (printf("%s\n", s) < 0 || ferror(stdout)) {
            c->broken_pipe = 1;
            c->app->abort_flag = 1;
            clearerr(stdout);
        } else {
            fflush(stdout);
        }
    }
    free(s);
}

static void hl_message_begin(void *user)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    cJSON *o;
    c->had_text = 0;   /* M326h: reset BEFORE the jsonl-only early return below,
                        * since text mode never reaches past it */
    if (!c->jsonl || c->broken_pipe) {
        return;
    }
    o = jc_agentjson_event("message_start");
    if (o != NULL) {
        cJSON_AddStringToObject(o, "model",
            c->app->config.model.model != NULL ? c->app->config.model.model : "");
        cJSON_AddStringToObject(o, "mode",
            jc_agent_mode_name((enum jc_agent_mode)c->app->mode));
        hl_emit(c, o);
    }
}

static void hl_text(void *user, const char *delta, jc_size n)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    size_t w;
    if (n > 0) {
        c->had_text = 1;   /* M326h: gates the separator in hl_message_end */
    }
    if (c->broken_pipe || n == 0) {
        return;
    }
    if (c->jsonl) {
        cJSON *o = jc_agentjson_event("text");
        if (o != NULL) {
            char *d = (char *)malloc((size_t)n + 1);
            if (d != NULL) {
                memcpy(d, delta, (size_t)n);
                d[n] = '\0';
                cJSON_AddStringToObject(o, "delta", d);
                free(d);
                hl_emit(c, o);
            } else {
                cJSON_Delete(o);
            }
        }
        return;
    }
    if (c->json) {
        return; /* json mode emits one object at the end */
    }
    /* M472: this is the one place headless text mode writes model bytes RAW to a
     * terminal, so it is where the C0/DEL strip belongs. The jsonl/json branches
     * above already returned -- cJSON escapes a control byte to \u001b, so it is
     * inert there and a machine consumer keeps full fidelity. Measured before
     * this: OSC 0 (window title) and OSC 52 (clipboard write) reached stdout
     * byte-for-byte from a mock provider's assistant text.
     *
     * A control byte is one byte, so unlike UTF-8 it cannot be split across two
     * SSE chunks; per-delta stripping is exact. Nothing is stripped in the common
     * case and nothing is allocated -- jc_ctrl_sanitize returns 0. */
    {
        char *clean = NULL;
        jc_size clean_n = 0;
        if (jc_ctrl_sanitize(delta, n, &clean, &clean_n)) {
            if (clean_n > 0) {
                w = fwrite(clean, 1, (size_t)clean_n, stdout);
            } else {
                w = 0;
            }
            free(clean);
            /* The write is short only because bytes were REMOVED, which is not
             * a broken pipe -- report the caller's n as satisfied. */
            if (!ferror(stdout)) {
                fflush(stdout);
                return;
            }
            c->broken_pipe = 1;
            c->app->abort_flag = 1;
            clearerr(stdout);
            return;
        }
    }
    w = fwrite(delta, 1, (size_t)n, stdout);
    if (w < (size_t)n || ferror(stdout)) {
        /* Downstream pipe closed (e.g. | head): stop the stream cleanly. */
        c->broken_pipe = 1;
        c->app->abort_flag = 1;
        clearerr(stdout);
        return;
    }
    fflush(stdout);
}

static void hl_tool_start(void *user, const char *name, const char *args,
                          const char *id)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    c->tool_calls++;
    switch (jc_agent_tool_category(name)) { /* M97: tool mix for a driving agent */
    case JC_TOOLCAT_READ: c->tc_read++; break;
    case JC_TOOLCAT_WRITE: c->tc_write++; break;
    case JC_TOOLCAT_SHELL: c->tc_shell++; break;
    default: c->tc_other++; break;
    }
    if (c->jsonl) {
        cJSON *o = jc_agentjson_event("tool_call");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "name", name != NULL ? name : "");
            cJSON_AddStringToObject(o, "args", args != NULL ? args : "");
            /* M442: the provider's own tool_call id, so a round with two calls to
             * the SAME tool can be paired with its results. Without it a supervisor
             * building a timeline had to guess by order -- and order is exactly what
             * a concurrent or reordered result set does not preserve. Emitted only
             * when the provider supplied one; an empty field would look like an id
             * that happens to be blank. */
            if (id != NULL && id[0] != '\0') {
                cJSON_AddStringToObject(o, "id", id);
            }
            hl_emit(c, o);
        }
        return;
    }
    if (c->quiet || c->json) {
        return;
    }
    /* M326h: the summarised, BOUNDED form by default -- the same
     * jc_tool_arg_summary the TUI renders, so the two surfaces cannot drift on
     * what a tool call looks like. This used to print the raw argument JSON,
     * which meant a write_file of a 200 KB file wrote 200 KB to stderr, while
     * the jsonl path three lines above deliberately bounded its preview. The
     * raw form is still reachable, under -v, because diagnosing a malformed
     * tool call (the M148 repair path) needs what the model actually sent.
     *
     * The "[tool] " prefix and the result line's shape are kept: stderr is
     * explicitly not an interface (docs/EMBEDDING.md), but a habit built on
     * the prefix costs nothing to preserve, and what was wrong here was the
     * payload, not the label. */
    {
        char summary[200];
        jc_tool_arg_summary(name, args, summary, sizeof summary);
        fprintf(stderr, "\n");
        if (c->app->config.accessible) {
            /* M566: the same catalog entries the TUI uses (jc_tui.c
             * cb_tool_start), so the two front-ends cannot drift on what a
             * tool call SOUNDS like -- which they had, for seventeen
             * milestones. `[tool] edit_file  edit_me.txt` is a bracketed
             * label, two spaces and a bare argument: a listener gets no
             * grammar to hang it on, and Orca reads the brackets aloud. */
            if (summary[0] != '\0') {
                fprintf(stderr, jc_msg(JC_MSG_TOOL_CALL_ARG), name, summary);
            } else {
                fprintf(stderr, jc_msg(JC_MSG_TOOL_CALL), name);
            }
        } else if (summary[0] != '\0') {
            fprintf(stderr, "[tool] %s  %s", name, summary);
        } else {
            fprintf(stderr, "[tool] %s", name);
        }
        fprintf(stderr, "\n");
        if (c->verbose && args != NULL && args[0] != '\0') {
            fprintf(stderr, "       args: %s\n", args);
        }
    }
}

static void hl_tool_result(void *user, const char *name, const char *result,
                           int is_error, const char *id)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    if (c->jsonl) {
        cJSON *o = jc_agentjson_event("tool_result");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "name", name != NULL ? name : "");
            cJSON_AddBoolToObject(o, "is_error", is_error ? 1 : 0);
            /* M442: pairs this result with its tool_call event. */
            if (id != NULL && id[0] != '\0') {
                cJSON_AddStringToObject(o, "id", id);
            }
            /* A bounded preview so an agent can react without the full blob.
             * M439: cut on a UTF-8 boundary. jc_snprintf is not UTF-8 aware, so
             * this used to put invalid UTF-8 on a JSON line whenever the 512-byte
             * cut landed inside a multi-byte character -- on a surface
             * docs/EMBEDDING.md calls stable and tells consumers to parse. */
            if (result != NULL) {
                char prev[513];
                jc_agentjson_preview(result, prev, sizeof prev);
                cJSON_AddStringToObject(o, "preview", prev);
            }
            hl_emit(c, o);
        }
        return;
    }
    (void)result;
    if (c->quiet || c->json) {
        return;
    }
    if (c->app->config.accessible) {
        /* M566. The compact form spends an `->` on the verb, and an arrow is
         * the one glyph a reader cannot guess at: SOME punctuation styles
         * name it, others skip it, so "[tool read_file -> ok]" is heard as
         * either "bracket tool read underscore file dash greater than ok" or
         * as "tool read file ok" with the outcome and the name unseparated.
         *
         * The entry carries a TRAILING SPACE because the TUI appends the
         * result body after it. Nothing follows here -- the headless path
         * discards `result`, see the (void) above -- so this prints one
         * invisible trailing space rather than justifying a duplicate entry
         * that says the same sentence without it. */
        fprintf(stderr, jc_msg(is_error ? JC_MSG_TOOL_FAIL : JC_MSG_TOOL_OK),
                name);
        fprintf(stderr, "\n");
    } else {
        fprintf(stderr, "[tool %s -> %s]\n", name, is_error ? "error" : "ok");
    }
}

static void hl_message_end(void *user)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    if (c->json || c->jsonl || c->broken_pipe) {
        return;
    }
    /* M326h: a tool-only round produces no text, and this newline was written
     * for it anyway -- so a three-tool turn put "\n\n\nAnswer." on stdout,
     * against docs/EMBEDDING.md's "stdout is the answer". Streamed output
     * cannot be trimmed after the fact, so the separator is simply not written
     * for a message that said nothing. */
    if (!c->had_text) {
        return;
    }
    fputc('\n', stdout);
    fflush(stdout);
}

/* M99: a free-form status line (retry / route escalation / compaction / verify /
 * subagent banner) becomes a jsonl `status` event so a driving agent sees mid-run
 * adaptation in real time. jsonl-only (the buffered json/text paths ignore it). */
static void hl_status(void *user, const char *line)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    cJSON *o;
    if (!c->jsonl || c->broken_pipe || line == NULL) {
        return;
    }
    o = jc_agentjson_event("status");
    if (o != NULL) {
        cJSON_AddStringToObject(o, "line", line);
        hl_emit(c, o);
    }
}

/* M165: throttled jsonl heartbeat. on_progress fires often (libcurl progress
 * during a model call); emit a `heartbeat` event at most once per interval so a
 * supervisor can tell "wedged" from "long model call". jsonl-only, opt-in. */
static void hl_progress(void *user)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    double now;
    cJSON *o;
    if (!c->jsonl || c->broken_pipe || c->hb_interval_ms <= 0) {
        return;
    }
    now = jc_now_millis();
    if (c->hb_last_ms != 0.0 &&
        (now - c->hb_last_ms) < (double)c->hb_interval_ms) {
        return;
    }
    c->hb_last_ms = now;
    o = jc_agentjson_event("heartbeat");
    if (o != NULL) {
        double elapsed = (now - c->hb_start_ms) / 1000.0;
        long rss_kb = 0;
        cJSON_AddNumberToObject(o, "elapsed", elapsed < 0.0 ? 0.0 : elapsed);
        /* M180: liveness AND footprint -- a supervisor watching heartbeats
         * sees a leak's slope without any extra tooling. Omitted (not 0)
         * when /proc is unavailable. */
        if (jc_meminfo_self(&rss_kb, NULL)) {
            cJSON_AddNumberToObject(o, "rss_kb", (double)rss_kb);
        }
        hl_emit(c, o);
    }
}

static void hl_usage(void *user, double in_tok, double out_tok,
                     double cache_read, double cache_write)
{
    struct hl_ctx *c = (struct hl_ctx *)user;
    double cost;
    c->in_tok += in_tok;   /* accumulate always (needed for JSON/quiet) */
    c->out_tok += out_tok;
    if (in_tok > c->peak_input) { /* M97: the ramp signal */
        c->peak_input = in_tok;
    }
    c->cache_read += cache_read;
    c->cache_write += cache_write;
    cost = jc_config_cost(&c->app->config.model, in_tok, out_tok,
                          cache_read, cache_write);
    c->cost_total += cost;
    if (c->jsonl) {
        if (in_tok > 0.0 || out_tok > 0.0) {
            cJSON *o = jc_agentjson_event("usage");
            if (o != NULL) {
                cJSON_AddNumberToObject(o, "input", in_tok);
                cJSON_AddNumberToObject(o, "output", out_tok);
                cJSON_AddNumberToObject(o, "cost", cost);
                hl_emit(c, o);
            }
        }
        return;
    }
    if (c->quiet || c->json) {
        return;
    }
    if (in_tok <= 0.0 && out_tok <= 0.0) {
        return;
    }
    {
        char sep = jc_group_sep_audience(c->app->config.group_sep,
                                        c->app->config.accessible);
        char si[40], so[40];
        jc_group_num(in_tok, sep, si, sizeof si);
        jc_group_num(out_tok, sep, so, sizeof so);
        if (c->app->config.accessible) {
            /* M566: the defect the operator found in the TUI at M563, in the
             * front-end that fix never reached. `[tokens in=4,946 out=37]` is
             * spoken roughly as "bracket tokens in equals four nine four six
             * out equals thirty seven bracket" -- the separator was fixed at
             * M555 and the punctuation around it was not. */
            fprintf(stderr, jc_msg(JC_MSG_TOKENS), si, so);
            fprintf(stderr, "\n");
            if (cost > 0.0) {
                /* SESSION_COST reads "The cost was ... dollars." -- scope
                 * neutral on purpose, so it serves this per-turn line and the
                 * TUI's per-session one. See jc_msg.h. */
                fprintf(stderr, jc_msg(JC_MSG_SESSION_COST), cost);
                fprintf(stderr, "\n");
            }
        } else if (cost > 0.0) {
            fprintf(stderr, "[tokens in=%s out=%s cost=$%.4f]\n", si, so, cost);
        } else {
            fprintf(stderr, "[tokens in=%s out=%s]\n", si, so);
        }
    }
}

/* Read all of stdin into an arena-allocated NUL-terminated string. */
static char *read_all_stdin(struct jc_arena *arena)
{
    struct jc_sb sb;
    int c;
    char *result;
    jc_sb_init(&sb);
    while ((c = getchar()) != EOF) {
        jc_sb_append_char(&sb, (char)c);
    }
    result = jc_arena_strdup(arena, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return result;
}

/* Decode a base64 CLI argument into a NUL-terminated arena string (M129). The
 * base64 decoder skips whitespace and stops at padding; the result is treated
 * as text (config JSON / a prompt), so a NUL terminator is appended. Returns
 * NULL on a decode error or empty input. */
static char *decode_b64_arg(const char *b64, struct jc_arena *arena)
{
    jc_size cap;
    jc_size n = 0;
    unsigned char *buf;
    if (b64 == NULL || b64[0] == '\0') {
        return NULL;
    }
    cap = jc_base64_decoded_len(strlen(b64));
    buf = (unsigned char *)jc_arena_alloc(arena, cap + 1);
    if (buf == NULL) {
        return NULL;
    }
    if (jc_base64_decode(b64, buf, cap, &n) != JC_OK || n == 0) {
        return NULL;
    }
    buf[n] = '\0';
    return (char *)buf;
}

/* Attach images to the turn's user message `m` (M29): the --image paths plus any
 * @image references in `prompt`. Gated on the active model's vision capability;
 * a non-vision model drops them with a warning. */
static void hl_attach_images(struct jc_app *app, const char *prompt,
                             struct jc_message *m)
{
    int k;
    if (!app->config.model.vision) {
        if (app->image_args_n > 0 && !app->quiet) {
            fprintf(stderr, "warning: model '%s' is not vision-capable "
                    "(set \"vision\": true); %d image(s) ignored\n",
                    app->config.model.name ? app->config.model.name : "?",
                    app->image_args_n);
        }
        return;
    }
    for (k = 0; k < app->image_args_n; k++) {
        if (jc_app_load_image(app, app->image_args[k], m) != JC_OK &&
            !app->quiet) {
            fprintf(stderr, "warning: could not attach image '%s'\n",
                    app->image_args[k]);
        }
    }
    if (app->config.references) {
        jc_refs_attach_images(app, prompt, m);
    }
}

/* M71: after a completed --auto run, optionally run the scaffolded `/learn`
 * mentor command to draft lessons (propose-only -- it writes
 * .jichi/lessons.draft.md, nothing else). Opt-in (config learnOnStop /
 * --learn-on-stop); a no-op outside AUTO mode or when the command is absent.
 * The mentor turn is intentionally not persisted (it runs after the save). */
/* M598: what the mentor's draft would commit, read back by parsing the output
 * file the learn command declared. `measured` is 0 when there was nothing to
 * parse (no output declared, or the file is absent/empty); `parsed_nothing`
 * is the zigodot shape -- bytes, and no section `learn apply` knows. */
struct learn_draft_report {
    int  measured;
    long memory;
    long skills;
    long corrections;
    long rules;
    int  parsed_nothing;
};

static void learn_draft_inspect(struct jc_app *app, const char *output_path,
                                struct learn_draft_report *r)
{
    char path[1400];
    char *text = NULL;
    jc_size len = 0;
    struct jc_learn_draft d;

    memset(r, 0, sizeof(*r));
    if (output_path == NULL || output_path[0] == '\0') {
        return;
    }
    if (output_path[0] == '/') {
        jc_snprintf(path, sizeof(path), "%s", output_path);
    } else {
        jc_snprintf(path, sizeof(path), "%s/%s", app->cwd, output_path);
    }
    if (jc_read_file(path, &text, &len, jc_app_scratch(app)) != JC_OK ||
        text == NULL || len == 0) {
        return;
    }
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(text, jc_app_scratch(app), &d);
    r->measured = 1;
    r->memory = (long)d.memory.len;
    r->skills = (long)d.skills.len;
    r->corrections = (long)d.corrections.len;
    r->rules = (long)d.rules.len;
    r->parsed_nothing = (r->memory + r->skills + r->corrections + r->rules == 0);
    jc_learn_draft_free(&d);
}

static void learn_on_stop(struct jc_app *app, struct jc_session *session,
                          struct learn_draft_report *report)
{
    const struct jc_command *c;
    char *expanded = NULL;
    struct jc_command_agent_save asave;
    struct jc_command_model_save msave;
    struct jc_agent_callbacks q;

    memset(report, 0, sizeof(*report));
    if (!app->config.learn_on_stop || app->mode != JC_MODE_AUTO) {
        return;
    }
    c = jc_command_find(&app->commands, "learn");
    if (c == NULL) {
        if (!app->quiet) {
            fprintf(stderr, "(learn-on-stop: no 'learn' command -- run `init` "
                    "to scaffold the mentor)\n");
        }
        return;
    }
    if (jc_command_expand(c, "", app->cwd, app->arena, &expanded) != JC_OK ||
        expanded == NULL) {
        return;
    }
    if (!app->quiet) {
        fprintf(stderr, "(learn-on-stop: running the mentor to draft "
                "lessons...)\n");
    }
    memset(&q, 0, sizeof(q)); /* silent: the artifact is the draft file */
    jc_app_command_agent_apply(app, c->agent, &asave);
    jc_app_command_model_apply(app, c->model, &msave);
    jc_agent_run_command_subtask(app, &session->history, expanded, c->output,
                                 c->language, &q);
    jc_app_command_model_restore(app, &msave);
    jc_app_command_agent_restore(app, &asave);
    /* M598: say what the draft would commit, while the run is still on the
     * operator's screen. For weeks the mentor ran without its format block
     * (M596) and produced drafts under invented headings; `learn apply` would
     * have committed nothing, and nothing said so until someone ran it. A
     * count line is the cheap half; the WARN on an unappliable draft is the
     * half that matters, and it is a WARN so -q cannot hide it. */
    learn_draft_inspect(app, c->output, report);
    if (report->measured && report->parsed_nothing) {
        jc_logf(JC_LOG_WARN, "learn-on-stop: %s has none of the sections "
                "`learn apply` parses (## Memory notes, ## Skills, "
                "## Corrections, ## Project rules) -- applying it would commit "
                "nothing. Reformat the lessons worth keeping under those "
                "headings, or check that the mentor received its format block "
                "(M596: `jichi -p /learn` and read the request).",
                c->output != NULL ? c->output : ".jichi/lessons.draft.md");
    } else if (report->measured && !app->quiet) {
        fprintf(stderr, "(learn-on-stop: draft parsed -- %ld memory note(s), "
                "%ld skill(s), %ld correction(s), %ld project rule(s))\n",
                report->memory, report->skills, report->corrections,
                report->rules);
    }
    if (!app->quiet) {
        fprintf(stderr, "(learn-on-stop: review .jichi/lessons.draft.md, then "
                "`jichi learn apply`)\n");
    }
}

static int run_headless(struct jc_app *app, const char *prompt, int fmt)
{
    struct jc_session session;
    struct jc_agent_callbacks cb;
    struct hl_ctx ctx;
    struct jc_command_agent_save agent_save;
    struct jc_command_model_save model_save;
    const char *cmd_agent = NULL; /* a custom command's `agent:`, or NULL */
    const char *cmd_model = NULL; /* a custom command's `model:`, or NULL */
    const char *cmd_output = NULL;/* a custom command's `output:`, or NULL */
    const char *cmd_language = NULL; /* a custom command's `language:` (M597) */
    int cmd_subtask = 0;          /* a custom command's `subtask:` flag    */
    jc_status st;

    {
        enum jc_session_open_result r = jc_session_open(&session,
            app->session_id, app->resume,
            app->session_all ? NULL : app->cwd, app->cwd, app->arena);
        if (r == JC_SESSION_AMBIGUOUS || r == JC_SESSION_NONE) {
            fprintf(stderr, "error: no %ssession matching '%s'\n",
                    r == JC_SESSION_AMBIGUOUS ? "unambiguous " : "",
                    app->session_id);
            return 2;
        }
        app->todos = &session.todos; /* M606: the list lives with the session */
        if (r == JC_SESSION_OPENED) {
            if (!app->quiet && fmt == 0) {
                fprintf(stderr, "(resumed: %s)\n",
                        session.title ? session.title : session.id);
            }
            if (!app->mode_pinned) {
                jc_app_set_mode(app, session.mode);
            }
            /* M350: the resumed history describes files as they WERE; the
             * disk is as it IS. Tell the model which of its believed files
             * moved while the conversation slept (a human edit, a git pull,
             * a CLI undo) -- the M349 undo notice's sibling for the sleeping
             * half. Saved immediately, so a second resume detects nothing
             * twice. Empty drift => no note. */
            {
                struct jc_sb dn, note;
                jc_sb_init(&dn);
                jc_sb_init(&note);
                if (jc_session_drift_names(&session, &dn) == JC_OK &&
                    dn.data != NULL && dn.len > 0) {
                    jc_session_drift_render(dn.data, &note);
                    if (note.len > 0 && note.data != NULL) {
                        jc_history_add(&session.history, JC_ROLE_USER,
                                       note.data);
                        jc_session_save(&session);
                        if (!app->quiet && fmt == 0) {
                            fprintf(stderr, "(noted for the model: files "
                                    "changed since this conversation last "
                                    "ran)\n");
                        }
                    }
                }
                jc_sb_free(&dn);
                jc_sb_free(&note);
            }
        }
    }

    /* A leading "/name ..." that matches a custom command is expanded. */
    if (prompt != NULL && prompt[0] == '/') {
        char name[128];
        const char *rest = prompt + 1;
        const struct jc_command *c;
        jc_size n = 0;
        while (rest[n] != '\0' && rest[n] != ' ' && n < sizeof(name) - 1) {
            name[n] = rest[n];
            n++;
        }
        name[n] = '\0';
        c = jc_command_find(&app->commands, name);
        if (c != NULL) {
            const char *args_raw = rest + n;
            char *expanded;
            while (*args_raw == ' ') {
                args_raw++;
            }
            /* Expansion products live on the per-turn SCRATCH arena
             * (M180), mirroring the TUI submit path: in one-shot -p the
             * difference is nil, but any long-lived process serving many
             * prompts (the daemon's children aside) must not pin every
             * expanded command body on the session arena. */
            jc_command_expand(c, args_raw, app->cwd, jc_app_scratch(app),
                              &expanded);
            prompt = expanded;
            cmd_agent = c->agent;     /* honor `agent:` frontmatter below   */
            cmd_model = c->model;     /* honor `model:` frontmatter below   */
            cmd_output = c->output;   /* honor `output:` frontmatter below  */
            cmd_language = c->language; /* honor `language:` below (M597)  */
            cmd_subtask = c->subtask; /* honor `subtask:` frontmatter below */
        } else {
            int resolved = 0;
            if (app->mcp != NULL) {
                /* Not a file command — try an MCP server prompt (M43; args
                 * M49). */
                const char *args_raw = rest + n;
                char *ptext = NULL;
                while (*args_raw == ' ') {
                    args_raw++;
                }
                if (jc_mcp_get_prompt_args(app->mcp, name, args_raw, &ptext)
                        == JC_OK && ptext != NULL) {
                    prompt = jc_arena_strdup(jc_app_scratch(app), ptext);
                    resolved = 1;
                }
                free(ptext);
            }
            /* M269: an unresolved "/name" is still sent to the model verbatim
             * -- a prompt may legitimately start with a path ("/usr/bin/cc is
             * missing"), so this is deliberately NOT an error. But the silence
             * misleads: a mistyped or not-yet-installed command produced a
             * confident, entirely improvised answer with exit 0 (found running
             * examples/self-hosting's /review-diff before copying its assets
             * into .jichi/). Warn when the token looks like a command name --
             * no '/' inside it -- so the loss is visible. Advisory only: it
             * changes no outcome, and -q silences it with everything else. */
            if (!resolved && strchr(name, '/') == NULL && name[0] != '\0') {
                /* M345: suggest the nearest INSTALLED name (custom commands +
                 * MCP prompts -- the universe headless can actually run;
                 * TUI-only built-ins would be a false promise here). Shares
                 * the model-side closeness rule via jc_str_closest, so a wild
                 * guess keeps the plain warning. */
                const char *cands[256];
                const char *near;
                jc_size k, n = 0;
                for (k = 0; k < app->commands.commands.len && n < 250; k++) {
                    struct jc_command *cm = (struct jc_command *)
                        jc_vec_at(&app->commands.commands, k);
                    if (cm->name != NULL) {
                        cands[n++] = cm->name;
                    }
                }
                if (app->mcp != NULL) {
                    int pi;
                    int np = jc_mcp_prompt_count(app->mcp);
                    for (pi = 0; pi < np && n < 250; pi++) {
                        const char *nm =
                            jc_mcp_prompt_at(app->mcp, pi, NULL, NULL);
                        if (nm != NULL) {
                            cands[n++] = nm;
                        }
                    }
                }
                cands[n] = NULL;
                near = jc_str_closest(name, cands);
                if (near != NULL) {
                    jc_logf(JC_LOG_WARN,
                            "no command '/%s' -- did you mean '/%s'? Sending "
                            "the text to the model as a plain message",
                            name, near);
                } else {
                    jc_logf(JC_LOG_WARN,
                            "no command '/%s' (and no MCP prompt by that "
                            "name); sending it to the model as a plain "
                            "message -- run `jichi commands` to list the "
                            "installed ones", name);
                }
            }
        }
    } else if (prompt != NULL) {
        /* Plain message: expand @file / @diff / @url references (if enabled),
         * then inject auto-RAG context (M61, if enabled). Both no-op unless
         * configured. */
        if (app->config.references) {
            char *ex;
            if (jc_refs_expand(app, prompt, jc_app_scratch(app), &ex)
                    == JC_OK) {
                prompt = ex;
            }
        }
        {
            char *ac;
            if (jc_autocontext_expand(app, prompt, jc_app_scratch(app), &ac)
                    == JC_OK &&
                ac != NULL) {
                prompt = ac;
            }
        }
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.app = app;
    ctx.quiet = app->quiet;
    /* -v is already recorded as the log level (see main); reading it here
     * avoids threading a second copy of the same fact through run_headless. */
    ctx.verbose = (jc_log_get_level() <= JC_LOG_DEBUG);
    ctx.json = (fmt != 0);     /* json or jsonl: no plain text on stdout */
    ctx.jsonl = (fmt == 2);
    /* M165: opt-in jsonl heartbeat (--heartbeat <secs>). */
    if (ctx.jsonl && app->heartbeat_secs > 0) {
        ctx.hb_interval_ms = (long)app->heartbeat_secs * 1000L;
        ctx.hb_start_ms = jc_now_millis();
        ctx.hb_last_ms = 0.0;
    }

    memset(&cb, 0, sizeof(cb));
    cb.on_assistant_text = hl_text;
    cb.on_message_begin = ctx.jsonl ? hl_message_begin : NULL;
    cb.on_tool_start = hl_tool_start;
    cb.on_tool_result = hl_tool_result;
    cb.on_message_end = hl_message_end;
    cb.on_usage = hl_usage;
    cb.on_status = hl_status; /* M99: stream retry/route/compact/verify as `status` */
    cb.on_progress = (ctx.hb_interval_ms > 0) ? hl_progress : NULL; /* M165 */
    cb.user = &ctx;

    /* M431c: announce the run id BEFORE the work starts, so a supervisor can tail
     * this run's journal while it is live. `done.run` carries the same id, but it
     * arrives at the end -- too late for the thing the id is actually for.
     *
     * Emitted only when an envelope is armed, because that is exactly when a run
     * id exists and when there is a journal (or telemetry `run` stamp) to join to;
     * a plain `-p` run has neither. A new event type rather than a field on an
     * existing one, which the jsonl contract permits and M165's `heartbeat` set
     * the precedent for -- consumers must ignore unknown types. */
    if (ctx.jsonl && app->env != NULL && app->env->run_id != NULL) {
        cJSON *ro = jc_agentjson_event("run_start");
        if (ro != NULL) {
            cJSON_AddStringToObject(ro, "run", app->env->run_id);
            hl_emit(&ctx, ro);
        }
    }

    /* Honor a custom command's `agent:` / `model:` / `subtask:` frontmatter. */
    jc_app_command_agent_apply(app, cmd_agent, &agent_save);
    jc_app_command_model_apply(app, cmd_model, &model_save);
    if (cmd_subtask) {
        st = jc_agent_run_command_subtask(app, &session.history, prompt,
                                          cmd_output, cmd_language, &cb);
    } else {
        struct jc_message *um =
            jc_history_add(&session.history, JC_ROLE_USER, prompt);
        if (um != NULL) {
            hl_attach_images(app, prompt, um);
        }
        st = jc_agent_run_turn(app, &session.history, &cb);
    }
    jc_app_command_model_restore(app, &model_save);
    jc_app_command_agent_restore(app, &agent_save);

    if (!app->no_session) {
        jc_session_autotitle(&session);
        session.mode = app->mode;
        jc_session_save(&session);
    }

    /* M73: some servers return a context-overflow message as completion content
     * (HTTP 200), so it can't be caught on the wire. Recognize the signature in
     * the answer and point the user at the fix on stderr (stdout stays the raw
     * answer for scripts). */
    if (!ctx.quiet &&
        jc_text_is_context_overflow(
            jc_agent_last_assistant_text(&session.history))) {
        fprintf(stderr,
            "hint: the model server reported a context-window overflow. Its "
            "real context window looks smaller than jichi assumed -- set "
            "--context-limit (try ~half the server's real window) or declare "
            "\"contextLength\" on the model so the system prompt is trimmed to "
            "fit. See `jichi doctor`.\n");
    }

    /* Structured output: json => one object at the end; jsonl => the terminal
     * "done" event. Emitted for every terminal state (incl. errors) so an agent
     * always gets a machine-readable result with a precise stop_reason. */
    if (ctx.json && !ctx.broken_pipe) {
        const char *ans = jc_agent_last_assistant_text(&session.history);
        const char *stop = "done";
        int aborted = (st == JC_ERR_ABORTED);
        int errc = 0;
        const char *errtype = NULL;
        const char *errmsg = NULL;
        cJSON *root;
        char *s;

        if (st == JC_ERR_ABORTED) {
            stop = "interrupted";
        } else if (st == JC_ERR_TIMEOUT) {
            stop = "timeout";
            errc = (int)st; errtype = "timeout"; errmsg = jc_status_str(st);
        } else if (st != JC_OK) {
            stop = "error";
            errc = (int)st; errtype = "error"; errmsg = jc_status_str(st);
        } else if (app->env != NULL &&
                   app->env->outcome == JC_ENV_BUDGET_EXHAUSTED) {
            stop = "budget";
        } else if (app->env != NULL &&
                   app->env->outcome == JC_ENV_VERIFY_FAILED) {
            stop = "verify_failed";
            errc = 1; errtype = "verify_failed";
            errmsg = "verifier failed after the retry budget";
        } else if (app->env != NULL &&
                   app->env->outcome == JC_ENV_SCOPE_TAINTED) {
            /* M332: the verifier PASSED. The green is refused because the run
             * changed a file outside the edit scope first, so the pass cannot be
             * told apart from a modified gate. A distinct stop_reason, not
             * verify_failed, because saying the verifier failed would be false. */
            stop = "scope_tainted";
            errc = 1; errtype = "scope_tainted";
            errmsg = "verify passed but the run changed files outside the edit "
                     "scope, so the result is not trusted (--strict-green)";
        } else if (app->turn_capped) {
            /* M322: the turn stopped at maxToolIters with work still to do. NOT
             * an error -- the history is intact and another prompt resumes from
             * it -- but a supervisor that cannot tell this from "done" will
             * accept a half-finished task as complete. Exit code stays 0
             * deliberately: nothing failed. */
            stop = "max_iters";
        }
        {
            /* M97: run economics for a driving agent (0/""/false when no
             * envelope). budget_kind is a stable machine string. */
            struct jc_agent_econ econ;
            const char *bkind = "";
            if (app->env != NULL) {
                switch (app->env->tripped) {
                case JC_BUDGET_TOKENS: bkind = "tokens"; break;
                case JC_BUDGET_DEADLINE: bkind = "deadline"; break;
                case JC_BUDGET_TOOLCALLS: bkind = "toolcalls"; break;
                case JC_BUDGET_READS: bkind = "reads"; break;
                default: bkind = ""; break;
                }
            }
            econ.starved = (app->env != NULL) ? app->env->starved : 0;
            econ.budget_kind = bkind;
            econ.budget_used = (app->env != NULL) ? app->env->tokens_used : 0.0;
            econ.budget_limit = (app->env != NULL) ? app->env->budget_tokens : 0.0;
            econ.peak_input = ctx.peak_input;
            econ.cache_read = ctx.cache_read;
            econ.cache_write = ctx.cache_write;
            econ.reads = ctx.tc_read;
            econ.writes = ctx.tc_write;
            econ.shells = ctx.tc_shell;
            econ.other_tools = ctx.tc_other;
            /* M443: the degraded counts, straight from the app. */
            econ.deg_unanswered = app->deg_unanswered;
            econ.deg_approval = app->deg_approval;
            econ.deg_privilege = app->deg_privilege;
            root = jc_agentjson_result(ans, app->config.model.model,
                app->no_session ? NULL : session.id,
                (app->env != NULL) ? app->env->run_id : NULL,
                ctx.in_tok, ctx.out_tok,
                ctx.cost_total, ctx.tool_calls, aborted, stop,
                (app->env == NULL) ? 1 : !app->env->rolled_back,
                errc, errtype, errmsg, &econ);
        }
        s = (root != NULL) ? jc_json_print(root) : NULL;
        if (s != NULL) {
            printf("%s\n", s);
            free(s);
        }
        cJSON_Delete(root);
    }

    /* Learn-on-stop (M71): on a completed --auto run, optionally draft lessons
     * from what just happened (propose-only). Before notify so the bell rings
     * once everything is done.
     *
     * M328: `st == JC_OK` is NOT "completed". run_agent_loop returns JC_OK for
     * every terminal state -- the envelope's `outcome` is what carries budget
     * exhaustion and verify failure, and the process exit code is derived from
     * it, not from `st`. So this fired after a run that had just hit its token
     * budget, and the mentor turn then spent 625k more tokens OUTSIDE the
     * envelope's accounting: an operator who set `--budget-tokens 1m` paid 1.64M
     * for the run, 61% over, with the overshoot invisible in the journal (whose
     * `end` event was already written) and in `jichi runs`.
     *
     * A budget stop is also the case where the mentor is least useful and most
     * expensive: the run was cut off mid-task, so the lessons drafted from it
     * describe an interrupted attempt. Gate on the outcome the comment always
     * claimed. A run with no envelope (no --auto bounds) is unaffected.
     *
     * M330: capture the mentor's token and tool-call cost and emit a journal
     * event so `jichi runs` can report the run's true cost. The envelope's
     * `tokens_used` keeps accumulating after the `end` event is written, so
     * the delta after learn_on_stop is the mentor's cost. */
    if (st == JC_OK && !ctx.broken_pipe
        && (app->env == NULL || app->env->outcome == JC_ENV_OK)) {
        double before_tokens = 0.0;
        long before_calls = 0;
        struct learn_draft_report dr;
        if (app->env != NULL) {
            before_tokens = app->env->tokens_used;
            before_calls = app->env->tool_calls;
        }
        learn_on_stop(app, &session, &dr);
        if (app->env != NULL && app->env->tokens_used > before_tokens) {
            double delta_tokens = app->env->tokens_used - before_tokens;
            long delta_calls = app->env->tool_calls - before_calls;
            cJSON *jo = jc_env_journal_begin(app->env, "learn_on_stop");
            if (jo != NULL) {
                cJSON_AddNumberToObject(jo, "tokens", delta_tokens);
                cJSON_AddNumberToObject(jo, "tool_calls", (double)delta_calls);
                /* M598: what the draft would commit, so `jichi runs` can show
                 * draft=empty on a row whose outcome is `ok`. Fields present
                 * only when a draft was there to parse: the presence of the
                 * key IS the flag (M431c), never a 0 that means "unknown". */
                if (dr.measured) {
                    cJSON_AddNumberToObject(jo, "draft_memory",
                                            (double)dr.memory);
                    cJSON_AddNumberToObject(jo, "draft_skills",
                                            (double)dr.skills);
                    cJSON_AddNumberToObject(jo, "draft_corrections",
                                            (double)dr.corrections);
                    cJSON_AddNumberToObject(jo, "draft_rules",
                                            (double)dr.rules);
                    cJSON_AddNumberToObject(jo, "draft_items",
                                            (double)(dr.memory + dr.skills +
                                                     dr.corrections +
                                                     dr.rules));
                    cJSON_AddNumberToObject(jo, "draft_parsed_nothing",
                                            dr.parsed_nothing ? 1.0 : 0.0);
                }
                jc_env_journal_end(app->env, jo);
            }
        }
    } else if (st == JC_OK && !ctx.broken_pipe && app->config.learn_on_stop
               && app->mode == JC_MODE_AUTO) {
        /* Say why it was skipped: silence here reads as "learnOnStop is broken",
         * and the operator's next move (re-brief smaller, then let the mentor
         * run on a clean completion) depends on knowing which it was. */
        jc_logf(JC_LOG_WARN, "learn-on-stop skipped: the run ended %s, not "
                "completed -- lessons drafted from an interrupted run describe "
                "the interruption. Re-run the increment smaller.",
                jc_env_outcome_name(app->env->outcome));
    }

    /* Completion notification (F6): only for the unattended AUTO posture -- an
     * interactive -p run is watched, so a bell/command would just be noise. */
    if (app->mode == JC_MODE_AUTO) {
        jc_notify_fire(app->config.notify, app->config.notify_bell, app->cwd,
                       session.title);
    }

    jc_session_free(&session);

    if (ctx.broken_pipe) {
        return 0; /* downstream pipe closed normally (e.g. | head) */
    }
    if (st == JC_ERR_ABORTED) {
        if (!app->quiet && fmt == 0) {
            fprintf(stderr, "\n[aborted]\n");
        }
        return g_got_sigterm ? 143 : 130; /* M146: 128+SIGTERM vs 128+SIGINT */
    }
    if (st != JC_OK) {
        fprintf(stderr, "error: %s\n", jc_status_str(st)); /* real error: always */
        return 1;
    }
    return 0;
}

/* Replace a leading $HOME in `path` with "~" for compact display. */
static void abbrev_home(const char *path, char *buf, jc_size cap)
{
    const char *home = jc_home_dir();
    jc_size hl = (home != NULL) ? (jc_size)strlen(home) : 0;
    if (path == NULL) { buf[0] = '\0'; return; }
    if (hl > 0 && strncmp(path, home, hl) == 0 &&
        (path[hl] == '/' || path[hl] == '\0')) {
        jc_snprintf(buf, cap, "~%s", path + hl);
    } else {
        jc_snprintf(buf, cap, "%s", path);
    }
}

/* `ls [--all]` subcommand: list saved sessions, newest first. Scoped to the
 * current workspace unless `all`. */
/* Emit the (scoped) session list as one JSON object (M165, `ls --output json`):
 * {"v":1,"skipped":N,"sessions":[{id,title,alias,workspace,nmsgs,mtime}...]}.
 * Built with cJSON so titles/paths are escaped, never printf'd into a JSON
 * literal.
 *
 * `skipped` (M482) counts session files that could not be read. It is always
 * present, so a supervisor can gate on the number rather than having to know
 * that an absent key means zero -- an added field is compatible under the
 * stability contract `describe` states ("new fields may appear; ignore unknown
 * ones"). The text path had reported this since M198 and the JSON path, the one
 * a supervisor actually parses, could not say it at all. */
static int run_ls_json(struct jc_arena *arena, int all)
{
    struct jc_vec metas;
    char cwd[1024];
    cJSON *root;
    cJSON *arr;
    char *printed;
    jc_size i;
    int skipped = 0;
    jc_status st;

    if (!all && getcwd(cwd, sizeof(cwd)) == NULL) {
        cwd[0] = '\0';
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    /* M482: this called jc_session_list and DISCARDED its status under the
     * comment "empty vec => empty array", so a failed enumeration produced a
     * well-formed `{"v":1,"sessions":[]}` -- a machine-readable lie, and worse
     * than the text path's, because `ls --output json` is a STABLE interface
     * (docs/EMBEDDING.md) that a supervisor parses instead of reading. It also
     * dropped `skipped` entirely, so the JSON could not say that files were
     * unreadable while the text form could. Both fixed: a failure is a non-zero
     * exit with a stderr reason and NO stdout object, and `skipped` is reported
     * as a field (an added field is a compatible change under M301's contract). */
    st = jc_session_list_ex(&metas, arena, &skipped);
    if (st != JC_OK && st != JC_ERR_NOTFOUND) {   /* see run_ls on NOTFOUND */
        fprintf(stderr, "error: could not list sessions (%s)\n",
                st == JC_ERR_OOM ? "out of memory"
                                 : "the session directory exists but could "
                                   "not be read");
        jc_vec_free(&metas);
        return 1;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        jc_vec_free(&metas);
        return 1;
    }
    cJSON_AddNumberToObject(root, "v", 1);
    /* Always present, so a supervisor can gate on it rather than having to know
     * that its absence means zero. */
    cJSON_AddNumberToObject(root, "skipped", (double)skipped);
    arr = cJSON_AddArrayToObject(root, "sessions");
    for (i = 0; i < metas.len && arr != NULL; i++) {
        struct jc_session_meta *m =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        cJSON *o;
        if (!all && cwd[0] != '\0' &&
            (m->workspace == NULL || strcmp(m->workspace, cwd) != 0)) {
            continue;
        }
        o = cJSON_CreateObject();
        if (o == NULL) {
            continue;
        }
        cJSON_AddStringToObject(o, "id", m->id != NULL ? m->id : "");
        if (m->title != NULL) cJSON_AddStringToObject(o, "title", m->title);
        if (m->alias != NULL) cJSON_AddStringToObject(o, "alias", m->alias);
        cJSON_AddStringToObject(o, "workspace",
                                m->workspace != NULL ? m->workspace : "");
        cJSON_AddNumberToObject(o, "nmsgs", (double)m->nmsgs);
        cJSON_AddNumberToObject(o, "mtime", m->mtime);
        cJSON_AddItemToArray(arr, o);
    }
    jc_vec_free(&metas);
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == NULL) {
        return 1;
    }
    puts(printed);
    free(printed);
    return 0;
}

static int run_ls(struct jc_arena *arena, int all, int json)
{
    struct jc_vec metas;
    char cwd[1024];
    long now = (long)time(NULL);
    jc_size i;
    int shown = 0;
    int skipped = 0; /* M198: sessions present but unlistable */
    jc_status st;

    if (json) {
        return run_ls_json(arena, all);
    }
    if (!all && getcwd(cwd, sizeof(cwd)) == NULL) {
        cwd[0] = '\0';
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    /* M482: the ENUMERATION FAILING and there being NO SESSIONS are different
     * answers, and this line used to give both the same one -- "(no saved
     * sessions)" on stdout with exit 0. jc_session_list_ex's own header calls
     * that the defect M198 existed to remove ("the user's sessions appeared to
     * have vanished with no diagnostic anywhere"); M198 fixed the PER-FILE half
     * via `skipped` and left the whole-call half printing the same lie. Found by
     * running tests/smoke/faults.sh against a FAULT=1 binary -- a tier no gate
     * built, so nothing had exercised this path since M198. */
    st = jc_session_list_ex(&metas, arena, &skipped);
    /* NOTFOUND is not a failure: it means no store has been created yet, which is
     * the state of every fresh install. Reporting THAT as an error was the first
     * cut of this fix, and subcommands_lint caught it -- `jichi ls` exited 1 with
     * an empty stdout on a machine that had simply never saved a session. */
    if (st != JC_OK && st != JC_ERR_NOTFOUND) {
        fprintf(stderr, "error: could not list sessions (%s)\n",
                st == JC_ERR_OOM ? "out of memory"
                                 : "the session directory exists but could "
                                   "not be read");
        jc_vec_free(&metas);
        return 1;
    }
    if (metas.len == 0) {
        printf("(no saved sessions)\n");
        if (skipped > 0) {
            fprintf(stderr, "warning: %d session file%s could not be read "
                    "(unreadable, corrupt, or over the 64 MB limit)\n",
                    skipped, skipped == 1 ? "" : "s");
        }
        jc_vec_free(&metas);
        return 0;
    }
    for (i = 0; i < metas.len; i++) {
        struct jc_session_meta *m =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        char when[24];
        char dir[256];
        if (!all && cwd[0] != '\0' &&
            (m->workspace == NULL || strcmp(m->workspace, cwd) != 0)) {
            continue;
        }
        jc_reltime(now - (long)m->mtime, when, sizeof(when));
        abbrev_home(m->workspace, dir, sizeof(dir));
        printf("%-8.8s  %-9s  %3d msg  %-22.22s  %s\n",
               m->id, when, m->nmsgs, dir,
               m->title ? m->title : "(untitled)");
        shown = 1;
    }
    jc_vec_free(&metas);
    if (skipped > 0) {
        /* M198: stderr, so stdout stays a clean machine-readable listing while
         * a supervisor still learns that sessions are missing. */
        fprintf(stderr, "warning: %d session file%s could not be read "
                "(unreadable, corrupt, or over the 64 MB limit)\n",
                skipped, skipped == 1 ? "" : "s");
    }
    if (!shown) {
        printf("(no saved sessions for this project; use --all for every "
               "project)\n");
    }
    return 0;
}

/* `prune [--keep N] [--older-than <dur>] [--dry-run]` (M219): delete old
 * session files -- the store-rotation half deferred at M197 (retention is
 * fixed, but a large store still costs parse time per listing and disk
 * forever). Selection is the pure, unit-tested jc_session_prune_select over
 * the same metas `ls` shows, ALWAYS across every project (a session's
 * workspace does not change its age). Refuses to run with no criterion;
 * --dry-run lists what would go. */
/* M611: newest-first by mtime, for reusing jc_session_prune_select on dream
 * files (its contract wants the list in jc_session_list order). */
static int dream_meta_cmp(const void *a, const void *b)
{
    const struct jc_session_meta *ma = (const struct jc_session_meta *)a;
    const struct jc_session_meta *mb = (const struct jc_session_meta *)b;
    if (ma->mtime < mb->mtime) { return 1; }
    if (ma->mtime > mb->mtime) { return -1; }
    return 0;
}

/* M611: apply the SAME retention selectors to ~/.jichi.d/dreams/ that prune
 * applies to sessions. Dreams had no retention at all -- the daemon's
 * idle-dream writes one per idle stretch -- and prune, "session-store hygiene",
 * is their natural home. The SELECTION reuses jc_session_prune_select (the
 * tested pure core); only the gather (a dir listing, mtime per file) and the
 * delete (unlink) are dream-specific. Reports its own counts via the out
 * params; returns 1 if any delete failed. */
static int prune_dreams(struct jc_arena *arena, long keep, double cutoff,
                        int dry_run, unsigned long *sel_out,
                        unsigned long *del_out, unsigned long *total_out)
{
    char dir[1024];
    struct jc_vec names, metas;
    int *del;
    jc_size i, n, selected;
    unsigned long deleted = 0, failed = 0;

    *sel_out = 0; *del_out = 0; *total_out = 0;
    jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/dreams", jc_home_dir());
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, arena) != JC_OK) {
        jc_vec_free(&names);
        return 0; /* no dreams dir yet: nothing to do, not an error */
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        jc_size nl = strlen(nm);
        char full[1400];
        struct jc_session_meta m;
        if (nl < 9 || strncmp(nm, "dream-", 6) != 0 ||
            strcmp(nm + nl - 3, ".md") != 0) {
            continue;
        }
        jc_snprintf(full, sizeof(full), "%s/%s", dir, nm);
        memset(&m, 0, sizeof m);
        m.id = jc_arena_strdup(arena, nm); /* the filename; only field we need */
        m.mtime = jc_file_mtime(full);
        jc_vec_push(&metas, &m);
    }
    jc_vec_free(&names);
    n = metas.len;
    *total_out = (unsigned long)n;
    if (n == 0) {
        jc_vec_free(&metas);
        return 0;
    }
    qsort(metas.data, n, sizeof(struct jc_session_meta), dream_meta_cmp);
    del = (int *)jc_arena_calloc(arena, n * sizeof(int));
    if (del == NULL) {
        jc_vec_free(&metas);
        return 1;
    }
    selected = jc_session_prune_select(
        (const struct jc_session_meta *)metas.data, n, keep, cutoff, del);
    *sel_out = (unsigned long)selected;
    for (i = 0; i < n; i++) {
        struct jc_session_meta *m;
        char full[1400];
        if (!del[i]) {
            continue;
        }
        m = (struct jc_session_meta *)jc_vec_at(&metas, i);
        jc_snprintf(full, sizeof(full), "%s/%s", dir, m->id);
        if (dry_run) {
            printf("would delete dream %s\n", m->id);
        } else if (remove(full) == 0) {
            deleted++;
        } else {
            fprintf(stderr, "warn: could not delete dream %s\n", m->id);
            failed++;
        }
    }
    *del_out = deleted;
    jc_vec_free(&metas);
    return failed > 0 ? 1 : 0;
}

/* M612: delete one index cache directory -- its known files (manifest.json,
 * vectors.f32) plus anything else in it, then the dir. An index is a
 * REBUILDABLE cache, so over-pruning costs a re-embed, never data; that is why
 * this sweep is safe to run by default (unlike sessions/dreams, which are
 * irreplaceable). Returns 1 on full success. */
static int rmdir_index(struct jc_arena *arena, const char *dir)
{
    struct jc_vec names;
    jc_size i;
    int ok = 1;
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, arena) == JC_OK) {
        for (i = 0; i < names.len; i++) {
            const char *nm = *(char **)jc_vec_at(&names, i);
            char full[1400];
            jc_snprintf(full, sizeof(full), "%s/%s", dir, nm);
            if (remove(full) != 0) {
                ok = 0;
            }
        }
    }
    jc_vec_free(&names);
    if (rmdir(dir) != 0) {
        ok = 0;
    }
    return ok;
}

/* M612: apply prune's retention selectors to the codebase-index cache
 * (~/.jichi.d/index/<key>/), which grew one directory per distinct workspace
 * with no eviction. Same selectors as sessions/dreams, reusing
 * jc_session_prune_select; each index's mtime is its manifest.json's (the file
 * a rebuild rewrites, so it means "last built"). `protect_dir` -- the CURRENT
 * workspace's cache dir -- is never a candidate, so a prune from inside a
 * project never deletes the index that project is about to use. */
static int prune_index(struct jc_arena *arena, long keep, double cutoff,
                       int dry_run, const char *protect_dir,
                       unsigned long *sel_out, unsigned long *del_out,
                       unsigned long *total_out)
{
    char base[1024];
    struct jc_vec names, metas;
    int *del;
    jc_size i, n, selected;
    unsigned long deleted = 0, failed = 0;

    *sel_out = 0; *del_out = 0; *total_out = 0;
    jc_snprintf(base, sizeof(base), "%s/.jichi.d/index", jc_home_dir());
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(base, &names, arena) != JC_OK) {
        jc_vec_free(&names);
        return 0; /* no index cache yet */
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char full[1400];
        char mpath[1500];
        double mt;
        struct jc_session_meta m;
        jc_snprintf(full, sizeof(full), "%s/%s", base, nm);
        if (protect_dir != NULL && strcmp(full, protect_dir) == 0) {
            continue; /* the current workspace's own index */
        }
        jc_snprintf(mpath, sizeof(mpath), "%s/manifest.json", full);
        mt = jc_file_mtime(mpath);           /* "last built"; 0 if absent */
        if (mt <= 0.0) {
            mt = jc_file_mtime(full);         /* partial dir: use its own mtime */
        }
        memset(&m, 0, sizeof m);
        m.id = jc_arena_strdup(arena, nm);   /* the <key> subdir */
        {
            /* A friendlier label for the report: the workspace root the manifest
             * records, falling back to the key. Best-effort; never fatal. */
            char *mtext = NULL;
            m.title = NULL;
            if (jc_read_file(mpath, &mtext, NULL, arena) == JC_OK &&
                mtext != NULL) {
                cJSON *mj = jc_json_parse(mtext);
                if (mj != NULL) {
                    const char *r = jc_json_get_str(mj, "root", NULL);
                    if (r != NULL) {
                        m.title = jc_arena_strdup(arena, r);
                    }
                    cJSON_Delete(mj);
                }
            }
        }
        m.mtime = mt;
        jc_vec_push(&metas, &m);
    }
    jc_vec_free(&names);
    n = metas.len;
    *total_out = (unsigned long)n;
    if (n == 0) {
        jc_vec_free(&metas);
        return 0;
    }
    qsort(metas.data, n, sizeof(struct jc_session_meta), dream_meta_cmp);
    del = (int *)jc_arena_calloc(arena, n * sizeof(int));
    if (del == NULL) {
        jc_vec_free(&metas);
        return 1;
    }
    selected = jc_session_prune_select(
        (const struct jc_session_meta *)metas.data, n, keep, cutoff, del);
    *sel_out = (unsigned long)selected;
    for (i = 0; i < n; i++) {
        struct jc_session_meta *m;
        char full[1400];
        if (!del[i]) {
            continue;
        }
        m = (struct jc_session_meta *)jc_vec_at(&metas, i);
        jc_snprintf(full, sizeof(full), "%s/%s", base, m->id);
        if (dry_run) {
            printf("would delete index %s\n",
                   m->title != NULL ? m->title : m->id);
        } else if (rmdir_index(arena, full)) {
            deleted++;
        } else {
            fprintf(stderr, "warn: could not fully delete index %s\n",
                    m->title != NULL ? m->title : m->id);
            failed++;
        }
    }
    *del_out = deleted;
    jc_vec_free(&metas);
    return failed > 0 ? 1 : 0;
}

/* M616: sweep stale attempt/improve/workflow worktrees under
 * ~/.jichi.d/worktrees/ by prune's selectors. They are removed only on the
 * clean exit path; --keep-worktree, a SIGKILL or a deadline kill leaves them
 * forever, `checkpoints gc` never touches this directory, and STATE.md called
 * it "rebuilt" while nothing rebuilt or removed it -- the dreams/index growth
 * family (M611/M612), third member. Only names this binary MINTS (att-<pid>,
 * imp-<pid>, wf-<pid>-<n>) are candidates, and a directory whose embedded pid
 * is a LIVE process is never one: deleting a running attempt's sandbox is not
 * retention, whatever its mtime says. The shadow repos' worktree admin data
 * self-heals: `git worktree prune` (run by every snapshot manager on remove,
 * and by checkpoints gc) clears entries whose directory is GONE. */
static int wt_name_pid(const char *nm, long *pid_out)
{
    const char *p = NULL;
    long v = 0;
    int seen = 0;
    if (strncmp(nm, "att-", 4) == 0 || strncmp(nm, "imp-", 4) == 0) {
        p = nm + 4;
    } else if (strncmp(nm, "wf-", 3) == 0) {
        p = nm + 3;
    } else {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
        seen = 1;
    }
    if (!seen || (*p != '\0' && *p != '-')) {
        return 0; /* not a name this binary mints */
    }
    *pid_out = v;
    return 1;
}

static int prune_worktrees(struct jc_arena *arena, long keep, double cutoff,
                           int dry_run, unsigned long *sel_out,
                           unsigned long *del_out, unsigned long *total_out)
{
    char base[1024];
    struct jc_vec names, metas;
    int *del;
    jc_size i, n, selected;
    unsigned long deleted = 0, failed = 0;

    *sel_out = 0; *del_out = 0; *total_out = 0;
    jc_snprintf(base, sizeof(base), "%s/.jichi.d/worktrees", jc_home_dir());
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(base, &names, arena) != JC_OK) {
        jc_vec_free(&names);
        return 0; /* no worktree area yet */
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char full[1400];
        long pid = 0;
        struct jc_session_meta m;
        if (!wt_name_pid(nm, &pid)) {
            continue; /* not ours to sweep (e.g. the parallel pool's trees) */
        }
        /* A live process owns this tree; EPERM still means "exists". */
        if (pid == (long)getpid() ||
            kill((pid_t)pid, 0) == 0 || errno == EPERM) {
            continue;
        }
        jc_snprintf(full, sizeof(full), "%s/%s", base, nm);
        memset(&m, 0, sizeof m);
        m.id = jc_arena_strdup(arena, nm);
        m.mtime = jc_file_mtime(full);
        jc_vec_push(&metas, &m);
    }
    jc_vec_free(&names);
    n = metas.len;
    *total_out = (unsigned long)n;
    if (n == 0) {
        jc_vec_free(&metas);
        return 0;
    }
    qsort(metas.data, n, sizeof(struct jc_session_meta), dream_meta_cmp);
    del = (int *)jc_arena_calloc(arena, n * sizeof(int));
    if (del == NULL) {
        jc_vec_free(&metas);
        return 1;
    }
    selected = jc_session_prune_select(
        (const struct jc_session_meta *)metas.data, n, keep, cutoff, del);
    *sel_out = (unsigned long)selected;
    for (i = 0; i < n; i++) {
        struct jc_session_meta *m;
        char full[1400];
        if (!del[i]) {
            continue;
        }
        m = (struct jc_session_meta *)jc_vec_at(&metas, i);
        jc_snprintf(full, sizeof(full), "%s/%s", base, m->id);
        if (dry_run) {
            printf("would delete worktree %s\n", m->id);
        } else if (strstr(full, "/.jichi.d/worktrees/") != NULL) {
            /* rm -rf, guarded to the worktree area the way
             * jc_snapshot_worktree_remove guards it. */
            char *rmargv[4];
            rmargv[0] = "rm"; rmargv[1] = "-rf"; rmargv[2] = full;
            rmargv[3] = 0;
            if (jc_proc_capture(rmargv, NULL, NULL, NULL, 0, 20, NULL) == 0 &&
                !jc_file_exists(full)) {
                deleted++;
            } else {
                fprintf(stderr, "warn: could not delete worktree %s\n", m->id);
                failed++;
            }
        }
    }
    *del_out = deleted;
    jc_vec_free(&metas);
    return failed > 0 ? 1 : 0;
}

static int run_prune(struct jc_arena *arena, struct cli_args *args)
{
    struct jc_vec metas;
    int *del;
    jc_size i, n;
    long now = (long)time(NULL);
    double cutoff = -1.0;
    unsigned long s_sel = 0, s_deleted = 0, s_failed = 0, s_total = 0;
    unsigned long d_sel = 0, d_deleted = 0, d_total = 0;
    unsigned long x_sel = 0, x_deleted = 0, x_total = 0;
    unsigned long w_sel = 0, w_deleted = 0, w_total = 0;
    char protect[1200];
    int d_rc, x_rc, w_rc;

    if (args->prune_older != NULL) {
        long secs = 0;
        if (jc_env_parse_duration(args->prune_older, &secs) != 0 || secs <= 0) {
            fprintf(stderr, "error: --older-than wants a duration like 30d, "
                            "12h, 90m (got '%s')\n", args->prune_older);
            return 2;
        }
        cutoff = (double)(now - secs);
    }
    if (args->prune_keep < 0 && cutoff < 0.0) {
        fprintf(stderr, "error: prune needs a criterion: --keep <n> and/or "
                        "--older-than <dur> (both = both must agree)\n");
        return 2;
    }

    /* M612: the current workspace's own index cache dir -- excluded from the
     * index sweep below so a prune inside a project never deletes the index it
     * is about to use. Resolved like reader_workspace (which is defined lower). */
    protect[0] = '\0';
    {
        char cwd[1024];
        char wsres[1100];
        if (getcwd(cwd, sizeof cwd) != NULL) {
            if (jc_path_resolve(cwd, wsres, sizeof wsres) != JC_OK) {
                jc_snprintf(wsres, sizeof wsres, "%s", cwd);
            }
            jc_index_cache_dir(wsres, protect, sizeof protect);
        }
    }

    /* Sessions (M219). No early return: dreams (M611) are pruned too, by the
     * same selectors, even when there is no session store. */
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    if (jc_session_list(&metas, arena) == JC_OK && metas.len > 0) {
        n = metas.len;
        s_total = (unsigned long)n;
        del = (int *)jc_arena_calloc(arena, n * sizeof(int));
        if (del != NULL) {
            s_sel = (unsigned long)jc_session_prune_select(
                (const struct jc_session_meta *)metas.data, n,
                args->prune_keep, cutoff, del);
            for (i = 0; i < n; i++) {
                struct jc_session_meta *m;
                if (!del[i]) {
                    continue;
                }
                m = (struct jc_session_meta *)jc_vec_at(&metas, i);
                if (args->dry_run) {
                    printf("would delete session %s  %s\n", m->id,
                           m->title != NULL ? m->title : "(untitled)");
                } else if (jc_session_delete(m->id) == JC_OK) {
                    s_deleted++;
                } else {
                    fprintf(stderr, "warn: could not delete %s\n", m->id);
                    s_failed++;
                }
            }
        }
    }
    jc_vec_free(&metas);

    /* M611: dreams accumulate exactly as sessions do and had no retention; the
     * daemon's idle-dream makes that unbounded. Same criteria, reported apart. */
    d_rc = prune_dreams(arena, args->prune_keep, cutoff, args->dry_run,
                        &d_sel, &d_deleted, &d_total);

    /* M612: the codebase-index cache -- rebuildable, so swept by default; the
     * current workspace's own index is protected. */
    x_rc = prune_index(arena, args->prune_keep, cutoff, args->dry_run,
                       protect[0] != '\0' ? protect : NULL,
                       &x_sel, &x_deleted, &x_total);

    /* M616: stale attempt/improve/workflow worktrees -- disposable sandboxes
     * whose live owners are protected by pid. */
    w_rc = prune_worktrees(arena, args->prune_keep, cutoff, args->dry_run,
                           &w_sel, &w_deleted, &w_total);

    if (s_total == 0 && d_total == 0 && x_total == 0 && w_total == 0) {
        printf("nothing to prune (no session store, no dreams, no index "
               "cache, no stale worktrees)\n");
        return 0;
    }
    if (args->dry_run) {
        printf("[dry run] %lu of %lu session(s), %lu of %lu dream(s), %lu "
               "of %lu index(es) and %lu of %lu worktree(s) would be "
               "deleted\n",
               s_sel, s_total, d_sel, d_total, x_sel, x_total, w_sel, w_total);
    } else {
        printf("deleted %lu of %lu session(s), %lu of %lu dream(s), %lu of "
               "%lu index(es) and %lu of %lu worktree(s)%s\n",
               s_deleted, s_total, d_deleted, d_total,
               x_deleted, x_total, w_deleted, w_total,
               (s_failed > 0 || d_rc != 0 || x_rc != 0 || w_rc != 0)
                   ? " (some failed, see above)" : "");
    }
    return (s_failed > 0 || d_rc != 0 || x_rc != 0 || w_rc != 0) ? 1 : 0;
}

/* `export [<id|prefix>] [--html] [-o <file>]`: render a saved session as a
 * Markdown (or HTML) transcript. With no id, the most recent session for this
 * project (or any, with --all) is used. Writes to <file> if given, else stdout.
 * Read-only; no provider or network. */
static int run_export(struct jc_arena *arena, struct cli_args *args)
{
    struct jc_session sess;
    struct jc_sb sb;
    const char *sel = (args->npos >= 2) ? args->pos[1] : args->session_id;
    char cwd[1024];
    /* Precedence: --output json > --html > Markdown (M165). */
    int fmt = (args->output_json == 1) ? JC_EXPORT_JSON
            : args->html ? JC_EXPORT_HTML : JC_EXPORT_MD;
    int rc = 0;

    if (sel != NULL && sel[0] != '\0') {
        char full[64];
        int r = jc_session_resolve_prefix(sel, full, sizeof(full), arena);
        if (r == -2) {
            fprintf(stderr, "error: session id '%s' is ambiguous\n", sel);
            return 1;
        }
        if (r != 0 || jc_session_load_by_id(full, &sess, arena) != JC_OK) {
            fprintf(stderr, "error: no session matching '%s'\n", sel);
            return 1;
        }
    } else {
        const char *scope = NULL;
        if (!args->all && getcwd(cwd, sizeof(cwd)) != NULL) {
            scope = cwd;
        }
        if (jc_session_load_recent_scoped(scope, &sess, arena) != JC_OK) {
            fprintf(stderr, "error: no saved session to export%s\n",
                    args->all ? "" : " for this project (try --all)");
            return 1;
        }
    }

    jc_sb_init(&sb);
    jc_session_render(&sess, fmt, &sb);

    if (args->out_path != NULL && args->out_path[0] != '\0') {
        if (jc_write_file(args->out_path, sb.data != NULL ? sb.data : "",
                          sb.len) != JC_OK) {
            fprintf(stderr, "error: could not write '%s'\n", args->out_path);
            rc = 1;
        } else {
            fprintf(stderr, "exported %s to %s\n",
                    sess.id != NULL ? sess.id : "session", args->out_path);
        }
    } else {
        fwrite(sb.data != NULL ? sb.data : "", 1, sb.len, stdout);
    }
    jc_sb_free(&sb);
    jc_session_free(&sess);
    return rc;
}

/* Resolve the model for an embed/rerank subcommand: an explicit --model
 * selector wins, otherwise the first model declaring `role`. */
static struct jc_model_cfg *pick_model(struct jc_app *app, unsigned role,
                                       const char *override)
{
    if (override != NULL && override[0] != '\0') {
        int i = jc_config_find_model(&app->config, override);
        if (i >= 0) {
            return jc_config_model_at(&app->config, i);
        }
        return NULL;
    }
    return jc_app_model_for_role(app, role);
}

/* `embed <text>`: print the embedding vector's dimension, L2 norm, and a short
 * preview as JSON. */
static int run_embed(struct jc_app *app, const char *text, const char *override)
{
    struct jc_model_cfg *m = pick_model(app, JC_ROLE_EMBED, override);
    float *vec = NULL;
    int dim = 0;
    double norm = 0.0;
    int i;
    int preview;

    if (m == NULL) {
        fprintf(stderr, "error: no embedding model (need role \"embed\" or "
                        "a matching --model)\n");
        return 1;
    }
    if (jc_embed_texts(m, &text, 1, &vec, &dim, &app->abort_flag) != JC_OK) {
        fprintf(stderr, "error: embedding request failed\n");
        return 1;
    }
    for (i = 0; i < dim; i++) {
        norm += (double)vec[i] * (double)vec[i];
    }
    norm = sqrt(norm);
    preview = dim < 8 ? dim : 8;
    printf("{\n  \"model\": \"%s\",\n  \"dim\": %d,\n  \"norm\": %.6f,\n"
           "  \"preview\": [", m->model != NULL ? m->model : "", dim, norm);
    for (i = 0; i < preview; i++) {
        printf("%s%.6f", i ? ", " : "", (double)vec[i]);
    }
    printf("%s]\n}\n", dim > preview ? ", ..." : "");
    free(vec);
    return 0;
}

/* `rerank <query> <doc>...`: print each document's relevance score, best
 * first, as a JSON array. */
static int run_rerank(struct jc_app *app, const char *query,
                      const char *const *docs, int ndocs, const char *override)
{
    struct jc_model_cfg *m = pick_model(app, JC_ROLE_RERANK, override);
    double *scores;
    int *order;
    int i;
    int j;

    if (m == NULL) {
        fprintf(stderr, "error: no rerank model (need role \"rerank\" or "
                        "a matching --model)\n");
        return 1;
    }
    if (ndocs <= 0) {
        fprintf(stderr, "error: rerank needs a query and at least one "
                        "document\n");
        return 1;
    }
    scores = (double *)malloc((jc_size)ndocs * sizeof(double));
    order = (int *)malloc((jc_size)ndocs * sizeof(int));
    if (scores == NULL || order == NULL) {
        free(scores);
        free(order);
        return 1;
    }
    if (jc_rerank_score(m, query, docs, ndocs, scores, &app->abort_flag)
            != JC_OK) {
        fprintf(stderr, "error: rerank request failed\n");
        free(scores);
        free(order);
        return 1;
    }
    for (i = 0; i < ndocs; i++) {
        order[i] = i;
    }
    /* Sort indices by score descending (insertion sort). */
    for (i = 1; i < ndocs; i++) {
        int oi = order[i];
        double os = scores[oi];
        j = i - 1;
        while (j >= 0 && scores[order[j]] < os) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = oi;
    }
    printf("[\n");
    for (i = 0; i < ndocs; i++) {
        int d = order[i];
        printf("  { \"index\": %d, \"score\": %.6f }%s\n",
               d, scores[d], i + 1 < ndocs ? "," : "");
    }
    printf("]\n");
    free(scores);
    free(order);
    return 0;
}

/* `index [--reindex]`: build or update the workspace index cache. */
static int run_index(struct jc_app *app, int reindex, const char *override)
{
    struct jc_model_cfg *m = pick_model(app, JC_ROLE_EMBED, override);
    struct jc_index *idx = NULL;
    struct jc_index_stats stats;
    jc_status st;

    if (m == NULL) {
        fprintf(stderr, "error: no embedding model (need role \"embed\" or "
                        "a matching --model)\n");
        return 1;
    }
    memset(&stats, 0, sizeof(stats));
    fprintf(stderr, "Indexing %s ...\n", app->cwd);
    st = jc_index_build(app->cwd, m, reindex, NULL, &idx, &stats,
                        &app->abort_flag, &app->config.ignore_dirs);
    if (st != JC_OK) {
        fprintf(stderr, "error: index build failed (%s)\n", jc_status_str(st));
        return 1;
    }
    printf("Indexed %d file(s), %d chunk(s) [%d embedded, %d reused]\n",
           stats.files, stats.chunks, stats.embedded, stats.reused);
    /* M483: a file count is a plausible number whether or not a subtree was
     * skipped, so the count alone cannot be trusted as coverage. stderr, so the
     * stdout summary stays clean for a script that parses it. */
    if (stats.unreadable_dirs > 0) {
        fprintf(stderr, "warning: %d director%s could not be read and %s NOT "
                        "indexed -- searches over this index will silently miss "
                        "what they contain\n",
                stats.unreadable_dirs,
                stats.unreadable_dirs == 1 ? "y" : "ies",
                stats.unreadable_dirs == 1 ? "is" : "are");
    }
    printf("Cache: %s/.jichi.d/index/\n", jc_home_dir());
    jc_index_free(idx);
    return 0;
}

static int is_index_subcommand(const char *cmd)
{
    return strcmp(cmd, "embed") == 0 || strcmp(cmd, "rerank") == 0 ||
           strcmp(cmd, "index") == 0;
}

/* `mcp`: connect to every configured MCP server and list the tools each
 * exposes. With `mcp call <tool> [json-args]`, invoke one tool and print its
 * result. A diagnostic that needs config + network but no chat provider --
 * handy for verifying an "mcpServers" entry. */
static int run_mcp(struct jc_app *app, struct cli_args *args)
{
    struct jc_tool_registry reg;
    struct jc_mcp_manager mcp;
    int nservers;
    int i;
    const char *verb = (args->npos >= 2) ? args->pos[1] : NULL;
    int do_call = (args->npos >= 3 && strcmp(args->pos[1], "call") == 0);
    int code = 0;

    if (app->config.mcp_servers.len == 0) {
        printf("(no MCP servers configured; add an \"mcpServers\" array to "
               "your config)\n");
        return 0;
    }

    jc_tool_registry_init(&reg);
    jc_mcp_manager_init(&mcp, app);
    app->mcp = &mcp;

    jc_mcp_connect_all(&mcp, &app->config, &reg);
    nservers = jc_mcp_server_count(&mcp);

    if (do_call) {
        const char *tool = args->pos[2];
        const char *json = (args->npos >= 4) ? args->pos[3] : "{}";
        struct jc_tool_result res;
        jc_tool_execute(&reg, tool, json, &res, app);
        printf("%s\n", res.content ? res.content : "(no result)");
        if (res.is_error) {
            code = 1;
        }
        jc_tool_result_free(&res);
    } else if (verb != NULL && strcmp(verb, "resources") == 0) {
        int n = jc_mcp_resource_count(&mcp);
        if (n == 0) {
            printf("(no resources advertised)\n");
        }
        for (i = 0; i < n; i++) {
            const char *srv = "";
            const char *desc = "";
            const char *uri = jc_mcp_resource_at(&mcp, i, &srv, &desc);
            printf("  %-40s [%s] %s\n", uri ? uri : "?", srv, desc);
        }
    } else if (verb != NULL && strcmp(verb, "read") == 0 && args->npos >= 3) {
        char *text = NULL;
        jc_status st = jc_mcp_read_resource(&mcp, args->pos[2], &text);
        if (st == JC_OK && text != NULL) {
            printf("%s\n", text);
        } else {
            fprintf(stderr, "could not read resource '%s'\n", args->pos[2]);
            code = 1;
        }
        free(text);
    } else if (verb != NULL && strcmp(verb, "prompts") == 0) {
        int n = jc_mcp_prompt_count(&mcp);
        if (n == 0) {
            printf("(no prompts advertised)\n");
        }
        for (i = 0; i < n; i++) {
            const char *srv = "";
            const char *desc = "";
            const char *nm = jc_mcp_prompt_at(&mcp, i, &srv, &desc);
            printf("  %-30s [%s] %s\n", nm ? nm : "?", srv, desc);
        }
    } else if (verb != NULL && strcmp(verb, "prompt") == 0 && args->npos >= 3) {
        char *text = NULL;
        struct jc_sb ab;
        jc_status st;
        int ai;
        jc_sb_init(&ab);
        for (ai = 3; ai < args->npos; ai++) {
            if (ab.len > 0) {
                jc_sb_append_char(&ab, ' ');
            }
            jc_sb_append(&ab, args->pos[ai]);
        }
        st = jc_mcp_get_prompt_args(&mcp, args->pos[2],
                                    ab.len > 0 ? ab.data : NULL, &text);
        jc_sb_free(&ab);
        if (st == JC_OK && text != NULL) {
            printf("%s\n", text);
        } else {
            fprintf(stderr, "could not get prompt '%s'\n", args->pos[2]);
            code = 1;
        }
        free(text);
    } else {
        int ntools = jc_mcp_tool_count(&mcp);
        cJSON *advertised = jc_tool_build_neutral(&reg, 1,
                                                  &app->config.permissions);
        int nadv = (advertised != NULL) ? cJSON_GetArraySize(advertised) : 0;
        cJSON_Delete(advertised);

        printf("Connected %d/%d server(s), %d tool(s) total, %d advertised "
               "to the model.\n\n", nservers,
               (int)app->config.mcp_servers.len, ntools, nadv);
        for (i = 0; i < nservers; i++) {
            int tc = 0;
            const char *nm = jc_mcp_server_name(&mcp, i, &tc);
            printf("  %s  (%d tool%s)\n", nm ? nm : "?", tc, tc == 1 ? "" : "s");
        }
        /* List every discovered tool, tagging the policy. Deny'd tools are
         * hidden from the model but still shown here for visibility. */
        if (ntools > 0) {
            printf("\nTools:\n");
            for (i = 0; i < ntools; i++) {
                int pol = JC_MCP_APPROVAL_ASK;
                const char *desc = "";
                const char *nm = jc_mcp_tool_at(&mcp, i, &pol, &desc);
                const char *tag = pol == JC_MCP_APPROVAL_ALLOW ? "[auto] "
                                : pol == JC_MCP_APPROVAL_DENY  ? "[deny] "
                                                               : "";
                printf("  %-40s %s%s\n", nm ? nm : "?", tag, desc);
            }
        }
        {
            int nr = jc_mcp_resource_count(&mcp);
            int np = jc_mcp_prompt_count(&mcp);
            if (nr > 0 || np > 0) {
                printf("\n%d resource(s), %d prompt(s). List with "
                       "`mcp resources` / `mcp prompts`; fetch with "
                       "`mcp read <uri>` / `mcp prompt <name>`.\n", nr, np);
            }
        }
    }

    jc_mcp_manager_shutdown(&mcp);
    jc_tool_registry_free(&reg);
    return code;
}

/* `test [command]` -> run the tests (arg, else config testCommand, else
 * verify), print a parsed summary + raw output, and exit with the command's
 * exit code. Needs config but no chat provider. */
static int run_test(struct jc_app *app, struct cli_args *args)
{
    const char *command = (args->npos >= 2) ? args->pos[1] : NULL;
    struct jc_sb raw;
    struct jc_sb summary;
    struct jc_test_report rep;
    int code = -1;

    if (command == NULL || command[0] == '\0') command = app->config.test_command;
    if (command == NULL || command[0] == '\0') command = app->config.verify;
    if (command == NULL || command[0] == '\0') {
        fprintf(stderr, "no test command given and none configured "
                        "(pass one, or set testCommand/verify in config)\n");
        return 2;
    }

    /* Route through jc_app_run_command so the memory watchdog (M117,
     * --mem-budget) applies to the test run too. */
    jc_sb_init(&raw);
    if (jc_app_run_command(app, command, 0, &raw, &code, NULL) != JC_OK) {
        fprintf(stderr, "failed to start the test command\n");
        jc_sb_free(&raw);
        return 1;
    }
    if (code < 0) code = 1;

    jc_test_report_init(&rep);
    jc_testparse(raw.data, &rep);
    jc_sb_init(&summary);
    jc_testparse_render(&rep, &summary);

    if (raw.data != NULL && raw.len > 0) {
        fputs(raw.data, stdout);
        if (raw.data[raw.len - 1] != '\n') fputc('\n', stdout);
    }
    printf("=== %s ===\n", rep.format);
    if (summary.data != NULL && summary.len > 0) {
        fputs(summary.data, stdout);
    } else {
        printf("%s\n", code == 0 ? "Tests passed." : "No structured results.");
    }

    jc_test_report_free(&rep);
    jc_sb_free(&summary);
    jc_sb_free(&raw);
    return code;
}

/* `map` -> print the repository map (files + top-level symbols). Needs config
 * but no chat provider. */
static int run_map(struct jc_app *app)
{
    char *map = jc_repomap_render(app);
    if (map == NULL) {
        printf("(no recognised source files in this workspace)\n");
        return 0;
    }
    printf("%s", map);
    free(map);
    return 0;
}

/* `context` subcommand: the static context-budget breakdown (system prompt +
 * tool definitions vs the effective limit; no live conversation, so history is
 * 0). The TUI `/context` shows the same with the running history folded in. */
/* Load every asset jc_sysmsg_build reads, for a subcommand that runs BEFORE
 * main()'s own asset load (M311).
 *
 * `sysmsg` (which prints the system prompt) and `context` (which sizes it) are
 * dispatched early, so each must load these itself. `sysmsg` did; `context` did
 * not -- so it reported `rules ~0` on every project, under-reporting the very
 * contributor M308 measured at 70% of a call, while COMPACTION.md's example output
 * showed `rules ~800`. The gauge read zero for the largest thing in the window.
 *
 * One helper rather than two copies, because the failure was not that the
 * sequence is hard: it is that a report about the prompt and the prompt itself
 * must not be able to disagree. Adding a section to the prompt now reaches both
 * or neither. `tests/smoke/context_assets.sh` holds them to the same numbers.
 *
 * Skills and output styles are already `_set_init`ed in main() before dispatch,
 * so this only loads. Idempotent enough to call from either path; not called on
 * the normal startup path, which does its own loading with the same functions. */
static void load_prompt_assets(struct jc_app *app,
                               const char *const *design_paths, int n_design)
{
    app->rules = jc_rules_load(app);
    jc_memory_refresh(app); /* M199: malloc-owned, one live copy */
    app->glossary = jc_glossary_load(app);
    jc_app_constraints_load(app); /* M110: enforced constraints are in the prompt */
    app->repo_map = app->config.repo_map ? jc_repomap_build(app) : NULL;
    jc_skill_load(&app->skills, app->cwd, app->arena);
    jc_output_style_load(&app->output_styles, app->cwd, app->arena);
    jc_output_style_set_active(&app->output_styles, app->config.output_style);
    jc_app_load_design(app, design_paths, n_design); /* M-C: a spec is prompt text */
}

/* Newest ".jsonl" file under ~/.jichi.d/<subdir>/ into `out`. Returns 0
 * on success, -1 if the directory is missing/empty or has no .jsonl. */

/* `context` (the budget breakdown) and `context tools` (per-tool definition
 * sizes, M313). The sub-verb rather than a flag: it matches `learn analyze` /
 * `mcp prompts` / `board list`, and a global flag that affects one subcommand is
 * a worse contract than a named view that appears in --help. */
/* M444: forward-declared -- the definitions sit next to main()'s arming block, where
 * they belong, but `context` dispatches from further up the file and needs the same
 * journal-less envelope `sysmsg` does. Without it `context` sizes a prompt the run
 * would not send, which is the one thing that subcommand exists to get right. */
static int envelope_is_armed(const struct cli_args *a, const struct jc_app *app);
static int intro_arm_env(struct jc_app *app, struct cli_args *a,
                         struct jc_envelope *env, struct jc_arena *arena);

static int run_context(struct jc_app *app, const char *const *design_paths,
                       int n_design,
                       int per_tool, int per_message, const char *sel,
                       int all_ws, struct cli_args *a, struct jc_arena *arena)
{
    struct jc_sb sb;
    struct jc_tool_registry reg;
    struct jc_envelope cenv;
    int armed;

    /* Tool registration normally happens later in main(); give the report a
     * built-in registry so the tool-definitions figure is representative
     * (conditionally-registered tools like git/lsp/media add a little more). */
    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);
    app->tools = &reg;
    load_prompt_assets(app, design_paths, n_design);
    /* M325b: the conditional built-ins too, so the report advertises what a real
     * turn does. Registered AFTER load_prompt_assets because load_skill's gate is
     * "some skills are loaded". MCP tools are still absent -- naming them would
     * mean connecting to every server -- and the report says so. */
    jc_tool_register_configured(&reg, app);
    /* M444: the same journal-less envelope `sysmsg` arms. Without it this report
     * sized a prompt the run would not send -- the flight plan, the scope-reach
     * paragraph and the gate contract were all missing, and on a run with an edit
     * scope and a verifier those are the largest env-gated block in the prompt. */
    armed = intro_arm_env(app, a, &cenv, arena);
    jc_sb_init(&sb);
    if (per_message) {
        /* M315: the subcommand has no live conversation, so a per-message
         * breakdown reads a SAVED session -- the most recent for this workspace,
         * or an explicit id/prefix, resolved exactly as `export` and --continue
         * do. Not a workaround: "what filled my window on that run?" is asked
         * AFTER the run, often about an unattended one, and the session file is
         * the artifact that survives it. */
        struct jc_session sess;
        char label[128];
        int ok = 0;
        if (sel != NULL && sel[0] != '\0') {
            char full[64];
            int r = jc_session_resolve_prefix(sel, full, sizeof(full),
                                              app->arena);
            if (r == -2) {
                fprintf(stderr, "error: session id '%s' is ambiguous\n", sel);
            } else if (r != 0 ||
                       jc_session_load_by_id(full, &sess, app->arena) != JC_OK) {
                fprintf(stderr, "error: no session matching '%s'\n", sel);
            } else {
                ok = 1;
            }
        } else if (jc_session_load_recent_scoped(all_ws ? NULL : app->cwd, &sess,
                                                 app->arena) == JC_OK) {
            ok = 1;
        } else {
            fprintf(stderr, "error: no saved session%s\n",
                    all_ws ? "" : " for this project (try --all)");
        }
        if (!ok) {
            jc_sb_free(&sb);
            app->tools = NULL;
            jc_tool_registry_free(&reg);
            if (armed) { app->env = NULL; jc_env_free(&cenv); }
            return 1;
        }
        jc_snprintf(label, sizeof(label), "session %s",
                    sess.id != NULL ? sess.id : "?");
        jc_context_history_report(app, &sess.history, label, &sb);
    } else if (per_tool) {
        /* M314: join the newest telemetry log, filtered to THIS workspace, so
         * the listing can say which of these tools was ever called. Automatic
         * rather than behind a flag: the point is that a cost finding reaches
         * the person looking at costs without a second incantation to discover.
         * No log => NULL, which the report renders as a stated absence (a column
         * of zeroes would read as "you use none of these", and telemetry is off
         * by default, so that would be most users). */
        char label[320];
        struct jc_telemetry_summary ts;
        int have = jc_app_load_telemetry(app, &ts, label, sizeof(label));

        jc_context_tools_report(app, have ? &ts : NULL, have ? label : NULL,
                                &sb);
        jc_telemetry_summary_free(&ts);
    } else {
        jc_context_report(app, NULL, &sb);
    }
    printf("%s", sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    app->tools = NULL;
    jc_tool_registry_free(&reg);
    if (armed) { app->env = NULL; jc_env_free(&cenv); }
    return 0;
}

/* M599: the workspace a telemetry READER describes -- `--workspace` when given,
 * else the canonical cwd (the same resolution the writer stamps on every event
 * and keys the default log by). */
static const char *reader_workspace(const char *ws_arg, char *buf, jc_size cap)
{
    char cwd[1024];
    if (ws_arg != NULL && ws_arg[0] != '\0') {
        return ws_arg;
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        jc_snprintf(buf, cap, ".");
        return buf;
    }
    if (jc_path_resolve(cwd, buf, cap) != JC_OK) {
        jc_snprintf(buf, cap, "%s", cwd);
    }
    return buf;
}

/* `telemetry [path]` subcommand: summarize a telemetry JSONL log. With no path,
 * this workspace's own log (M599), else the newest under ~/.jichi.d/telemetry/.
 * Offline. */
static int run_telemetry(struct jc_arena *arena, const char *path_arg,
                         const char *ws_arg, int cache_audit,
                         const char *since)
{
    char path[1300];
    char *text;
    struct jc_telemetry_summary s;
    struct jc_sb out;

    if (path_arg != NULL && path_arg[0] != '\0') {
        jc_snprintf(path, sizeof(path), "%s", path_arg);
    } else {
        char wsb[1024];
        if (jc_app_pick_telemetry_log(reader_workspace(ws_arg, wsb, sizeof(wsb)),
                                      arena, path, sizeof(path)) != 0) {
            fprintf(stderr, "no telemetry logs under ~/.jichi.d/telemetry "
                    "(metrics are on by default -- run a turn in this workspace "
                    "first, or check that `logging.level` / --log-level did not "
                    "turn them off)\n");
            return 1;
        }
    }

    if (jc_read_file(path, &text, NULL, arena) != JC_OK) {
        fprintf(stderr, "could not read %s\n", path);
        return 1;
    }
    jc_telemetry_summary_init(&s);
    /* Optional workspace filter (M56): canonicalize the path so it matches the
     * "ws" stamped on events (the canonical workspace root); fall back to the
     * raw string if it doesn't resolve (e.g. a recorded path no longer present). */
    if (ws_arg != NULL && ws_arg[0] != '\0') {
        char canon[JC_PATH_MAX];
        if (jc_path_resolve(ws_arg, canon, sizeof(canon)) == JC_OK) {
            jc_snprintf(s.ws_filter, sizeof(s.ws_filter), "%s", canon);
        } else {
            jc_snprintf(s.ws_filter, sizeof(s.ws_filter), "%s", ws_arg);
        }
    }
    /* M286: --since <dur> windows the summary, mirroring `runs --since` (M160).
     * A log that outlives a code change otherwise reports one aggregate across
     * both sides of it -- which is how a defect fixed weeks earlier reads as
     * live. Same duration parser, same "silently ignore an unparseable value"
     * behaviour as the other readers. */
    if (since != NULL && since[0] != '\0') {
        long secs = 0;
        if (jc_env_parse_duration(since, &secs) == 0 && secs > 0) {
            s.min_ts = jc_now_seconds() - (double)secs;
        }
    }
    jc_telemetry_feed(&s, text);
    jc_sb_init(&out);
    jc_sb_append_fmt(&out, "Telemetry: %s\n\n", path);
    if (cache_audit) {
        jc_cacheaudit_render(&s, &out);
    } else {
        jc_telemetry_render(&s, &out);
    }
    fputs(out.data != NULL ? out.data : "", stdout);
    jc_sb_free(&out);
    jc_telemetry_summary_free(&s);
    return 0;
}

/* --- `constraints` subcommand (offline; zero model calls) ----------------- */

/* Human label for a constraint kind, for the CLI listing. Mirrors the store
 * directive names accepted by jc_constraint_parse, so what is printed can be
 * pasted back into .jichi/constraints.md. */
static const char *constraint_kind_label(enum jc_constraint_kind k)
{
    switch (k) {
    case JC_CONSTRAINT_DENY_TOOL: return "deny-tool";
    case JC_CONSTRAINT_DENY_CMD:  return "deny-cmd";
    case JC_CONSTRAINT_READ_ONLY: return "read-only";
    case JC_CONSTRAINT_NOTE:      return "note";
    }
    return "?";
}

static void constraints_print_list(const struct jc_constraint *cs, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        printf("  %-10s %-12s %s\n",
               constraint_kind_label(cs[i].kind),
               cs[i].subject != NULL ? cs[i].subject : "",
               cs[i].text != NULL ? cs[i].text : "");
    }
}

/* `constraints scan <file|->`: report what jc_constraint_scan WOULD adopt from a
 * prompt or brief, without running anything.
 *
 * Why this exists. An inferred constraint is enforced for the whole run -- a
 * matching tool call is mechanically refused even if the model forgets -- and
 * extraction is a keyword scan over prose, so it is sometimes wrong; three
 * misparses in a single day are recorded in jc_constraint.h. Until now the only
 * way to learn what a brief would produce was to start the run and read the
 * `[constraint]` line, which on a long unattended drive means finding out after
 * the budget is spent (one recorded misfire cost a 1.56M-token run). The scanner
 * is already pure and unit-tested, so predicting it costs a file read.
 *
 * Exit 1 when anything would be adopted. That is a finding to REVIEW, not
 * necessarily an error -- a brief that deliberately says "work read-only" is
 * meant to adopt one -- but 0-means-nothing-inferred is the polarity a supervisor
 * wants to gate a brief on, and it follows `doctor`, where the exit code reports
 * findings rather than whether the check itself ran. */
static int run_constraints_scan(struct jc_arena *arena, const char *path)
{
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    const char *src;
    char *text = NULL;
    jc_size len = 0;
    int n;

    src = (path == NULL || strcmp(path, "-") == 0) ? "(stdin)" : path;
    if (strcmp(src, "(stdin)") == 0) {
        text = read_all_stdin(arena);
    } else if (jc_read_file(path, &text, &len, arena) != JC_OK) {
        fprintf(stderr, "constraints: cannot read %s\n", path);
        return 2;
    }
    if (text == NULL) text = (char *)"";

    n = jc_constraint_scan(text, cs, JC_CONSTRAINT_MAX, arena);
    if (n <= 0) {
        printf("constraints scan: nothing would be adopted from %s\n", src);
        return 0;
    }
    printf("constraints scan: %d would be ADOPTED from %s\n\n", n, src);
    constraints_print_list(cs, n);
    printf("\nEach is enforced for the whole run: a matching tool call is "
           "refused even\n");
    printf("if the model forgets. If one is unintended, state the boundary "
           "positively\n");
    printf("and let --edit-scope / --reference-root carry it -- those are "
           "enforced\n");
    printf("identically and cannot misparse prose.\n");
    return 1;
}

/* `constraints`: list the workspace's persisted store. Parsed directly rather
 * than via jc_app_constraints_load, which logs its own startup WARN -- a listing
 * should not look like a warning about itself. */
static int run_constraints_list(struct jc_arena *arena, const char *cwd)
{
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    char path[1200];
    char *data = NULL;
    jc_size len = 0;
    int n = 0;

    jc_snprintf(path, sizeof path, "%s/.jichi/constraints.md", cwd);
    if (jc_read_file(path, &data, &len, arena) == JC_OK && len > 0) {
        n = jc_constraint_parse(data, cs, JC_CONSTRAINT_MAX, arena);
    }
    if (n <= 0) {
        printf("(no constraints; add one with `/constraints add <text>` in the "
               "TUI, or write %s)\n", path);
        return 0;
    }
    printf("active constraints (%s): %d\n", path, n);
    constraints_print_list(cs, n);
    return 0;
}

/* `audit [path]` (M158): summarize the always-on privileged-command audit log
 * (the M154 sink). Offline; the pure jc_auditview does the parsing/rendering.
 * `--since <dur>` limits the window (e.g. 7d). */
static int run_audit(struct jc_arena *arena, const char *path_arg,
                     const char *since, int json)
{
    char path[1300];
    char *text;
    struct jc_audit_summary s;
    struct jc_sb out;
    double since_ts = 0.0;

    if (path_arg != NULL && path_arg[0] != '\0') {
        jc_snprintf(path, sizeof(path), "%s", path_arg);
    } else {
        jc_snprintf(path, sizeof(path), "%s/.jichi.d/audit/"
                    "privileged.jsonl", jc_home_dir());
    }
    if (!jc_file_exists(path)) {
        if (json) {
            cJSON *o = cJSON_CreateObject();
            char *js;
            if (o == NULL) {
                return 1;
            }
            cJSON_AddNumberToObject(o, "v", 1);
            cJSON_AddStringToObject(o, "path", path);
            cJSON_AddNumberToObject(o, "total", 0);
            js = cJSON_PrintUnformatted(o);
            cJSON_Delete(o);
            if (js == NULL) {
                return 1;
            }
            puts(js);
            free(js);
            return 0;
        }
        printf("no privileged-command audit log at %s\n"
               "(nothing has asked for sudo/doas/pkexec/su yet, or "
               "privilegedAudit is off)\n", path);
        return 0;
    }
    if (since != NULL && since[0] != '\0') {
        long secs = 0;
        if (jc_env_parse_duration(since, &secs) == 0 && secs > 0) {
            since_ts = jc_now_seconds() - (double)secs;
        }
    }
    if (jc_read_file(path, &text, NULL, arena) != JC_OK) {
        fprintf(stderr, "could not read %s\n", path);
        return 1;
    }
    jc_auditview_init(&s);
    jc_auditview_feed(&s, text, since_ts);
    if (json) {
        cJSON *o = jc_auditview_json(&s);
        char *js;
        if (o != NULL) {
            cJSON_AddStringToObject(o, "path", path); /* provenance */
        }
        js = (o != NULL) ? cJSON_PrintUnformatted(o) : NULL;
        cJSON_Delete(o);
        jc_auditview_free(&s);
        if (js == NULL) {
            return 1;
        }
        puts(js);
        free(js);
        return 0;
    }
    jc_sb_init(&out);
    jc_sb_append_fmt(&out, "Privileged-command audit: %s\n\n", path);
    jc_auditview_render(&s, &out);
    fputs(out.data != NULL ? out.data : "", stdout);
    jc_sb_free(&out);
    jc_auditview_free(&s);
    return 0;
}

/* `runs [dir]` (M158): one row per autonomy-envelope journal under
 * ~/.jichi.d/runs -- outcome, tokens, tool calls, verify record, and
 * the notes that matter for triage (rolled_back / budget kind / starved /
 * out-of-scope). Newest-first; capped unless --all. */
#define JC_RUNS_DEFAULT_LIMIT 20

struct runs_entry {
    char name[256];
    double mtime;
};

static int runs_entry_cmp(const void *a, const void *b)
{
    const struct runs_entry *ra = (const struct runs_entry *)a;
    const struct runs_entry *rb = (const struct runs_entry *)b;
    if (ra->mtime > rb->mtime) return -1; /* newest first */
    if (ra->mtime < rb->mtime) return 1;
    return strcmp(ra->name, rb->name);
}

/* The empty-directory result for `runs`: text hint, or a valid empty JSON
 * object (path escaped by cJSON) for a machine caller. */
static int runs_empty(const char *dir, int json)
{
    if (json) {
        cJSON *o = cJSON_CreateObject();
        char *js;
        if (o == NULL) {
            return 1;
        }
        cJSON_AddNumberToObject(o, "v", 1);
        cJSON_AddStringToObject(o, "dir", dir);
        cJSON_AddItemToObject(o, "runs", cJSON_CreateArray());
        cJSON_AddNumberToObject(o, "shown", 0);
        cJSON_AddNumberToObject(o, "total", 0);
        js = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        if (js == NULL) {
            return 1;
        }
        puts(js);
        free(js);
        return 0;
    }
    printf("no run journals under %s\n"
           "(bounded runs write one when --journal / the envelope is "
           "active)\n", dir);
    return 0;
}

static int run_runs(struct jc_arena *arena, const char *dir_arg, int show_all,
                    const char *since, int json)
{
    char dir[1300];
    struct jc_vec names;
    struct jc_vec entries;
    struct jc_sb out;
    cJSON *jroot = NULL;
    cJSON *jarr = NULL;
    double since_ts = 0.0;
    jc_size i;
    jc_size shown = 0;
    jc_size windowed_out = 0;
    jc_size limit;
    char vers[8][32];       /* M290: distinct builds among the listed runs */
    int nvers = 0;

    /* M160: --since <dur> windows the table to runs with activity after the
     * cutoff (journal timestamps; file mtime as the fallback). */
    if (since != NULL && since[0] != '\0') {
        long secs = 0;
        if (jc_env_parse_duration(since, &secs) == 0 && secs > 0) {
            since_ts = jc_now_seconds() - (double)secs;
        }
    }

    if (dir_arg != NULL && dir_arg[0] != '\0') {
        jc_snprintf(dir, sizeof(dir), "%s", dir_arg);
    } else {
        jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/runs",
                    jc_home_dir());
    }
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, arena) != JC_OK || names.len == 0) {
        jc_vec_free(&names);
        return runs_empty(dir, json);
    }

    /* Collect *.jsonl with mtimes, newest first. */
    jc_vec_init(&entries, sizeof(struct runs_entry));
    for (i = 0; i < names.len; i++) {
        const char *n = *(char **)jc_vec_at(&names, i);
        jc_size nl = strlen(n);
        struct runs_entry e;
        char full[1600];
        if (nl < 6 || nl >= sizeof(e.name) ||
            strcmp(n + nl - 6, ".jsonl") != 0) {
            continue;
        }
        jc_snprintf(e.name, sizeof(e.name), "%s", n);
        jc_snprintf(full, sizeof(full), "%s/%s", dir, n);
        e.mtime = jc_file_mtime(full);
        jc_vec_push(&entries, &e);
    }
    jc_vec_free(&names);
    if (entries.len == 0) {
        jc_vec_free(&entries);
        return runs_empty(dir, json);
    }
    qsort(entries.data, (size_t)entries.len, sizeof(struct runs_entry),
          runs_entry_cmp);

    limit = show_all ? entries.len : (jc_size)JC_RUNS_DEFAULT_LIMIT;
    jc_sb_init(&out);
    if (json) {
        jroot = cJSON_CreateObject();
        jarr = cJSON_CreateArray();
        if (jroot == NULL || jarr == NULL) {
            cJSON_Delete(jroot);
            cJSON_Delete(jarr);
            jc_sb_free(&out);
            jc_vec_free(&entries);
            return 1;
        }
        cJSON_AddNumberToObject(jroot, "v", 1);
        cJSON_AddStringToObject(jroot, "dir", dir);
    } else {
        jc_sb_append_fmt(&out, "Runs: %s\n\n", dir);
        jc_runsview_render_header(&out);
    }
    for (i = 0; i < entries.len && shown < limit; i++) {
        const struct runs_entry *e =
            (const struct runs_entry *)jc_vec_at(&entries, i);
        char full[1600];
        char *text;
        struct jc_run_summary s;
        jc_snprintf(full, sizeof(full), "%s/%s", dir, e->name);
        if (jc_read_file(full, &text, NULL, arena) != JC_OK) {
            continue;
        }
        if (jc_runsview_parse(text, &s) != 0) {
            continue;
        }
        /* --since window: keep runs with activity after the cutoff; a
         * journal with no timestamps falls back to the file's mtime. */
        if (since_ts > 0.0) {
            double last = (s.ts_last > 0.0) ? s.ts_last : e->mtime;
            if (last < since_ts) {
                windowed_out++;
                continue;
            }
        }
        if (s.run[0] == '\0') {
            /* Fall back to the file name (sans .jsonl) as the run id. */
            jc_size nl = strlen(e->name);
            jc_snprintf(s.run, sizeof(s.run), "%.*s", (int)(nl - 6), e->name);
        }
        if (json) {
            cJSON *ro = jc_runsview_json(&s);
            if (ro != NULL) {
                cJSON_AddItemToArray(jarr, ro);
            }
        } else {
            jc_runsview_render_row(&s, &out);
        }
        /* M290: remember the distinct builds across the LISTED runs. Per row it
         * would be noise (the same string on every line of a triage table); as a
         * footer it says the one thing that matters -- whether these rows are
         * comparable to each other at all. */
        if (s.jichi[0] != '\0') {
            int seen = 0;
            int k;
            for (k = 0; k < nvers; k++) {
                if (strcmp(vers[k], s.jichi) == 0) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && nvers < (int)(sizeof(vers) / sizeof(vers[0]))) {
                jc_snprintf(vers[nvers], sizeof(vers[0]), "%s", s.jichi);
                nvers++;
            }
        }
        shown++;
    }
    if (json) {
        char *js;
        cJSON_AddItemToObject(jroot, "runs", jarr);
        cJSON_AddNumberToObject(jroot, "shown", (double)shown);
        cJSON_AddNumberToObject(jroot, "total", (double)entries.len);
        if (windowed_out > 0) {
            cJSON_AddNumberToObject(jroot, "windowed_out",
                                    (double)windowed_out);
        }
        js = cJSON_PrintUnformatted(jroot);
        cJSON_Delete(jroot);
        jc_sb_free(&out);
        jc_vec_free(&entries);
        if (js == NULL) {
            return 1;
        }
        puts(js);
        free(js);
        return 0;
    }
    if (nvers == 1) {
        jc_sb_append_fmt(&out, "\njichi %s\n", vers[0]);
    } else if (nvers > 1) {
        int k;
        jc_sb_append(&out, "\njichi: these rows span ");
        for (k = 0; k < nvers; k++) {
            jc_sb_append_fmt(&out, "%s%s", k > 0 ? ", " : "", vers[k]);
        }
        jc_sb_append(&out, " -- outcomes are not directly comparable\n");
    }
    if (windowed_out > 0) {
        jc_sb_append_fmt(&out, "\n(%lu run%s outside the --since window)\n",
                         (unsigned long)windowed_out,
                         windowed_out == 1 ? "" : "s");
    }
    if (!show_all && entries.len > limit) {
        jc_sb_append_fmt(&out, "\n(%lu more; use --all)\n",
                         (unsigned long)(entries.len - limit));
    }
    fputs(out.data != NULL ? out.data : "", stdout);
    jc_sb_free(&out);
    jc_vec_free(&entries);
    return 0;
}

/* Scan up to `limit` recent sessions (matching `ws` when non-NULL) for
 * fix/break/fix edit loops, appending redo-loop findings (M70). */

/* Build the ranked insights report for `text` (telemetry JSONL) into `out`:
 * telemetry summary -> insights + redo-loop scan + stale-memory review + the
 * autonomy-outcomes context line. Shared by `learn analyze` and `dream`. */

/* `learn analyze [path]` subcommand: mine telemetry + recent sessions for
 * recurring problems and print a ranked report. Offline; no provider. */
static int run_learn_analyze(struct jc_arena *arena, const char *path_arg,
                             const char *ws_arg)
{
    char path[1300];
    char *text;
    struct jc_sb out;

    if (path_arg != NULL && path_arg[0] != '\0') {
        jc_snprintf(path, sizeof(path), "%s", path_arg);
    } else {
        char wsb[1024];
        if (jc_app_pick_telemetry_log(reader_workspace(ws_arg, wsb, sizeof(wsb)),
                                      arena, path, sizeof(path)) != 0) {
            fprintf(stderr, "no telemetry logs under ~/.jichi.d/telemetry "
                    "(metrics are on by default -- run a turn in this workspace "
                    "first, or check that `logging.level` / --log-level did not "
                    "turn them off)\n");
            return 1;
        }
    }
    if (jc_read_file(path, &text, NULL, arena) != JC_OK) {
        fprintf(stderr, "could not read %s\n", path);
        return 1;
    }
    jc_sb_init(&out);
    jc_learn_analyze_render(arena, text, ws_arg, &out);
    fputs(out.data != NULL ? out.data : "", stdout);
    jc_sb_free(&out);
    return 0;
}

/* M611: the newest dream-*.md in `dir` by mtime, into `out`. 0 on success, -1
 * when the dir is missing/empty or holds no dream file. Lets run_dream skip a
 * reflection identical to the last one (the daemon idling over unchanging
 * telemetry would otherwise write a fresh dated draft every idle stretch). */
static int newest_dream_path(struct jc_arena *arena, const char *dir,
                             char *out, jc_size cap)
{
    struct jc_vec names;
    char best[256];
    double best_mt = -1.0;
    jc_size i;
    best[0] = '\0';
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, arena) != JC_OK || names.len == 0) {
        jc_vec_free(&names);
        return -1;
    }
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        jc_size nl = strlen(nm);
        char full[1400];
        double mt;
        if (nl < 9 || strncmp(nm, "dream-", 6) != 0 ||
            strcmp(nm + nl - 3, ".md") != 0) {
            continue;
        }
        jc_snprintf(full, sizeof(full), "%s/%s", dir, nm);
        mt = jc_file_mtime(full);
        if (mt > best_mt) {
            best_mt = mt;
            jc_snprintf(best, sizeof(best), "%s", nm);
        }
    }
    jc_vec_free(&names);
    if (best[0] == '\0') {
        return -1;
    }
    jc_snprintf(out, cap, "%s/%s", dir, best);
    return 0;
}

/* M611: a dream path that will not clobber an existing one. The stamp is whole
 * seconds (jc_now_seconds), and jc_write_file truncates, so a manual `dream`
 * racing the daemon's idle-dream in the same second used to overwrite a
 * propose-only draft whose whole point is to be KEPT (STATE.md: "you lose those
 * notes"). Try dream-<sec>.md, then dream-<sec>-2.md ... until one is free. */
static void dream_dest_path(const char *dir, char *out, jc_size cap)
{
    long sec = (long)jc_now_seconds();
    int n;
    jc_snprintf(out, cap, "%s/dream-%ld.md", dir, sec);
    if (!jc_file_exists(out)) {
        return;
    }
    for (n = 2; n < 100000; n++) {
        jc_snprintf(out, cap, "%s/dream-%ld-%d.md", dir, sec, n);
        if (!jc_file_exists(out)) {
            return;
        }
    }
    /* 100000 dreams in one second is pathological and unreachable; fall back to
     * the base name rather than loop, matching the pre-M611 behaviour. */
    jc_snprintf(out, cap, "%s/dream-%ld.md", dir, sec);
}

/* `dream [path]`: sleep-consolidation (M102). Reflect over telemetry offline and
 * write a PROPOSE-ONLY, dated draft under ~/.jichi.d/dreams/ -- outside
 * any workspace (ANECDOTES #1). Never mutates config, memory, or the repo. */
static int run_dream(struct jc_arena *arena, const char *path_arg,
                     const char *ws_arg)
{
    char path[1300];
    char dir[1024];
    char dest[1200];
    char *text;
    struct jc_sb out;
    struct jc_sb doc;

    if (path_arg != NULL && path_arg[0] != '\0') {
        jc_snprintf(path, sizeof(path), "%s", path_arg);
    } else {
        char wsb[1024];
        if (jc_app_pick_telemetry_log(reader_workspace(ws_arg, wsb, sizeof(wsb)),
                                      arena, path, sizeof(path)) != 0) {
            fprintf(stderr, "dream: no telemetry under ~/.jichi.d/telemetry "
                    "(metrics are on by default -- run a turn in this workspace "
                    "first); nothing to reflect on.\n");
            return 1;
        }
    }
    if (jc_read_file(path, &text, NULL, arena) != JC_OK) {
        fprintf(stderr, "dream: could not read %s\n", path);
        return 1;
    }

    jc_sb_init(&out);
    jc_learn_analyze_render(arena, text, ws_arg, &out);

    jc_sb_init(&doc);
    jc_sb_append(&doc, "# Dream (sleep-consolidation)\n\n");
    jc_sb_append(&doc, "_Propose-only reflection over telemetry. Nothing here "
                       "was applied. Review, then act via `learn`/`assign`._\n\n");
    jc_sb_append_fmt(&doc, "Source log: %s\n\n", path);
    jc_sb_append(&doc, "## Recurring problems\n\n");
    jc_sb_append(&doc, out.data != NULL ? out.data : "(no findings)\n");
    jc_sb_append(&doc, "\n## Suggested next actions\n\n");
    jc_sb_append(&doc, "- Run `/learn` (the mentor) to draft durable lessons "
                       "from the above, then `learn apply` after review.\n");
    jc_sb_append(&doc, "- Turn each recurring failure into a rehearsal spec "
                       "(`assign`/`grade`) so the fix can be verified.\n");
    jc_sb_append(&doc, "- Review any stale-memory notes flagged above and "
                       "correct them (M78 corrections).\n");

    jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/dreams", jc_home_dir());
    jc_mkdir_p(dir);
    /* M611: don't record a dream identical to the most recent one. The document
     * has no embedded timestamp (that lives in the filename), so identical
     * findings over the same source log produce a byte-identical doc; the daemon
     * idling over unchanging telemetry would otherwise pile up dated duplicates.
     * A dream records a DELTA, or nothing. */
    {
        char prevp[1200];
        char *prev = NULL;
        if (newest_dream_path(arena, dir, prevp, sizeof(prevp)) == 0 &&
            jc_read_file(prevp, &prev, NULL, arena) == JC_OK && prev != NULL &&
            doc.data != NULL && strcmp(prev, doc.data) == 0) {
            fprintf(stderr, "dream: unchanged since %s; nothing to "
                    "consolidate.\n", prevp);
            jc_sb_free(&out);
            jc_sb_free(&doc);
            return 0;
        }
    }
    dream_dest_path(dir, dest, sizeof(dest));
    if (jc_write_file(dest, doc.data != NULL ? doc.data : "",
                      doc.data != NULL ? doc.len : 0) != JC_OK) {
        fprintf(stderr, "dream: could not write %s\n", dest);
        jc_sb_free(&out);
        jc_sb_free(&doc);
        return 1;
    }
    fprintf(stderr, "dream: wrote %s (propose-only; review it).\n", dest);
    jc_sb_free(&out);
    jc_sb_free(&doc);
    return 0;
}

/* Folder-safe slug from a skill name: lowercase alnum, others -> '-'. */
/* `learn apply` subcommand: commit the (human-edited) lessons draft. The work
 * (and the doc comment for it) is jc_learn_apply -- M293 moved it out of main.c
 * so the TUI can call it too, which is the only way a live session can see the
 * notes a `## Corrections` section just superseded. This shell keeps the CLI's
 * printf voice and its exit code; both surfaces render one set of numbers. */
static int run_learn_apply(struct jc_app *app, unsigned sections, int force)
{
    char draft[1100];
    struct jc_learn_apply_stats st;
    struct jc_sb detail;
    struct jc_sb summary;

    jc_learn_draft_path(app, draft, sizeof(draft));
    jc_sb_init(&detail);
    if (jc_learn_apply(app, sections, force, &st, &detail) == JC_ERR_NOTFOUND) {
        jc_sb_free(&detail);
        fprintf(stderr, "no %s -- run the mentor first "
                "(/learn, or: jichi -p \"/learn\")\n", draft);
        return 1;
    }
    printf("%s", detail.data != NULL ? detail.data : "");
    jc_sb_free(&detail);

    jc_sb_init(&summary);
    jc_learn_apply_summary(&st, draft, &summary);
    /* The no-parseable-sections advice is a diagnostic, so it keeps its stderr
     * channel -- stdout stays the record of what was committed. */
    fprintf(st.parsed_nothing ? stderr : stdout, "%s",
            summary.data != NULL ? summary.data : "");
    jc_sb_free(&summary);
    return 0;
}

/* Create the parent directory of `path` (mkdir -p), if it has one. */
static void scaffold_mkparent(const char *path)
{
    char dir[1100];
    char *slash;
    jc_size n = strlen(path);
    if (n == 0 || n >= sizeof(dir)) {
        return;
    }
    memcpy(dir, path, n + 1);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
        if (dir[0] != '\0') {
            jc_mkdir_p(dir);
        }
    }
}

/* `init [pack ...] [--global] [--force] [--dry-run] [--list]` -> scaffold a starter
 * set of project assets. Needs only the filesystem (no config/provider). */
/* Write a scaffold pack's files (shared by `init` and `setup`). Non-destructive
 * unless `force`; `dry` previews. Accumulates counts into the out-params and
 * returns 1 if any file failed, else 0. */
static int scaffold_write_pack(const struct jc_scaffold_pack *pack, int global,
                               int force, int dry, int *wrote, int *skipped,
                               int *failed)
{
    int i;
    char dest[1100];
    for (i = 0; i < pack->nfiles; i++) {
        const struct jc_scaffold_file *f = &pack->files[i];
        int exists;
        if (jc_scaffold_dest(f->relpath, global, jc_home_dir(),
                             dest, sizeof(dest)) < 0) {
            fprintf(stderr, "  ! %s (path too long)\n", f->relpath);
            (*failed)++;
            continue;
        }
        exists = jc_file_exists(dest);
        if (exists && !force) {
            printf("  = %s (exists; --force to overwrite)\n", dest);
            (*skipped)++;
            continue;
        }
        if (dry) {
            printf("  %c %s\n", exists ? '~' : '+', dest);
            (*wrote)++;
            continue;
        }
        scaffold_mkparent(dest);
        {
            struct jc_sb sb;
            jc_status st;
            jc_sb_init(&sb);
            jc_scaffold_file_text(f, &sb);
            st = jc_write_file(dest, sb.data != NULL ? sb.data : "", sb.len);
            jc_sb_free(&sb);
            if (st != JC_OK) {
                fprintf(stderr, "  ! %s (write failed)\n", dest);
                (*failed)++;
                continue;
            }
        }
        printf("  %c %s\n", exists ? '~' : '+', dest);
        (*wrote)++;
    }
    return *failed > 0;
}

static int run_init(struct cli_args *args)
{
    const char *packname;
    const struct jc_scaffold_pack *pack;
    int wrote = 0, skipped = 0, failed = 0;

    if (args->list) {
        int n = jc_scaffold_pack_count();
        int k;
        printf("Available packs (compiled in):\n");
        for (k = 0; k < n; k++) {
            const struct jc_scaffold_pack *p = jc_scaffold_pack_at(k);
            printf("  %-16s %s\n", p->name, p->description);
        }
        /* The domain benches ship as ready-to-copy assets under examples/
         * rather than compiled in (they are large and per-interest -- see the
         * docs/proposals/2026-08-domain-scaffolds.md decision to keep the
         * binary lean). Point the user at them so they are discoverable. */
        printf("\nDomain benches (copy from the jichi source tree's examples/):"
               "\n  data-analysis, game-design, game-dev, blender-python,"
               "\n  krita-python, project-management, personal-finance,"
               "\n  scheduling, business-plan, academic-writing,"
               "\n  research-notes, web-basics\n"
               "  Each has a START_HERE.md; copy its agents/, commands/,"
               " skills/\n  into your .jichi/ and its AGENTS.md to the project"
               " root.\n  See examples/README.md and docs/SCAFFOLDING.md.\n");
        return 0;
    }

    /* M182: `init <pack> [pack...]` composes several packs in one call.
     * Packs are additive + skip-if-exists, so ORDER IS PRECEDENCE: the
     * first pack to claim a shared path (AGENTS.md, config.example.json)
     * wins and later packs skip it -- identical to running init N times.
     * All names are validated BEFORE anything is written, so a typo in the
     * third pack cannot leave a half-composed project. (Previously the
     * extra arguments were silently ignored -- worse than an error.) */
    {
        int p;
        int npacks = (args->npos >= 2) ? (int)(args->npos - 1) : 1;
        for (p = 0; p < npacks; p++) {
            packname = (args->npos >= 2) ? args->pos[1 + p] : "default";
            if (jc_scaffold_find_pack(packname) == NULL) {
                fprintf(stderr,
                        "init: unknown pack '%s' (try `init --list`)\n",
                        packname);
                return 2;
            }
        }
        for (p = 0; p < npacks; p++) {
            packname = (args->npos >= 2) ? args->pos[1 + p] : "default";
            pack = jc_scaffold_find_pack(packname);
            printf("Scaffolding '%s' into %s%s:\n", pack->name,
                   args->init_global ? "~/.config/jichi"
                                     : ".jichi (this project)",
                   args->dry_run ? "  [dry run]" : "");
            scaffold_write_pack(pack, args->init_global, args->force,
                                args->dry_run, &wrote, &skipped, &failed);
        }
    }

    printf("\n%d %s, %d skipped, %d failed.\n", wrote,
           args->dry_run ? "to write" : "written", skipped, failed);
    if (wrote > 0 && !args->dry_run) {
        printf("Edit the files under %s; they take effect on the next run "
               "(see `jichi skills` / `agents`).\n",
               args->init_global ? "~/.config/jichi" : ".jichi");
    }
    return failed > 0 ? 1 : 0;
}

/* Resolve the config-target arg to a path (local/global/custom). */
static void setup_config_path(const char *target, char *out, jc_size cap)
{
    if (target == NULL || strcmp(target, "local") == 0) {
        jc_snprintf(out, cap, "local/config.json");
    } else if (strcmp(target, "global") == 0) {
        jc_snprintf(out, cap, "%s/.jichi", jc_home_dir());
    } else {
        jc_snprintf(out, cap, "%s", target);
    }
}

/* ----- interactive prompt helpers (used by the wizard) -------------------- */

/* Prompt for a line; returns an arena-owned answer (the `dflt` when the user
 * just hits Enter), or NULL on EOF / Ctrl-C (abort the wizard). */
static const char *setup_line(struct jc_term *t, struct jc_arena *a,
                              const char *label, const char *dflt)
{
    char prompt[320];
    char *line = NULL;
    const char *s;
    jc_size n;
    if (dflt != NULL && dflt[0] != '\0') {
        jc_snprintf(prompt, sizeof(prompt), "  %s [%s]: ", label, dflt);
    } else {
        jc_snprintf(prompt, sizeof(prompt), "  %s: ", label);
    }
    if (jc_term_readline(t, prompt, &line) != JC_READ_LINE) {
        free(line);
        return NULL;
    }
    s = (line != NULL) ? line : "";
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    n = (jc_size)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\n' || s[n - 1] == '\r')) {
        n--;
    }
    if (n == 0) {
        free(line);
        return (dflt != NULL) ? jc_arena_strdup(a, dflt) : NULL;
    }
    {
        char *cp = (char *)jc_arena_alloc(a, n + 1);
        if (cp != NULL) {
            memcpy(cp, s, n);
            cp[n] = '\0';
        }
        free(line);
        return cp;
    }
}

/* Explain a rejected apiKeyEnv answer, on `out` (M326e).
 *
 * THE MESSAGE MUST NEVER ECHO THE VALUE. If the diagnosis is right, the value
 * IS the user's API key -- and this text goes to a terminal, a scrollback, and
 * often into a bug report. The old flow printed a pasted key three times (the
 * prompt echo, the validation line, the next-steps line) and wrote it into the
 * config under a comment promising secrets were never stored there. Naming the
 * mistake without repeating it is the whole point. */
static void setup_key_env_explain(FILE *out, const char *flag_or_null)
{
    if (flag_or_null != NULL) {
        fprintf(out, "setup: %s wants the NAME of an environment variable, "
                "not the key itself.\n", flag_or_null);
    } else {
        fprintf(out, "\n  That is not a variable name -- it looks like the key "
                "itself.\n");
    }
    fprintf(out, "  A name must match [A-Za-z_][A-Za-z0-9_]* (a pasted key "
            "usually fails on a '-').\n");
    fprintf(out, "  jichi never stores the key: it reads it at run time from "
            "the variable you name.\n");
    fprintf(out, "  Put the key in one, then name it here:\n");
    fprintf(out, "    ( umask 077; echo \"export JICHI_API_KEY='<your key>'\" "
            ">> ~/.jichi.env )\n");
    if (flag_or_null != NULL) {
        fprintf(out, "    . ~/.jichi.env && jichi setup %s JICHI_API_KEY ...\n",
                flag_or_null);
    }
}

/* Read a secret with the terminal's echo OFF (M326e). Returns a malloc'd string
 * the caller must free (and should overwrite), or NULL on EOF/error/empty.
 *
 * Deliberately NOT jc_term_readline: that editor renders each keystroke itself,
 * so "don't echo" would mean threading a flag through the renderer for one
 * prompt. Here the line is read in the terminal's own cooked mode with ECHO
 * cleared -- the standard getpass shape, restored on every exit path. This is
 * the ONE prompt in the wizard that legitimately takes a key, which is why it
 * is also the only one that hides what you type. */
static char *setup_secret(const char *prompt)
{
    struct termios saved;
    struct termios quiet;
    char buf[1024];
    char *s;
    jc_size n;
    int have_tty = isatty(STDIN_FILENO);

    fputs(prompt, stdout);
    fflush(stdout);
    if (have_tty) {
        if (tcgetattr(STDIN_FILENO, &saved) != 0) {
            have_tty = 0;
        } else {
            quiet = saved;
            quiet.c_lflag &= ~(tcflag_t)ECHO;
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) != 0) {
                have_tty = 0;
            }
        }
    }
    s = fgets(buf, (int)sizeof buf, stdin);
    if (have_tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
        fputs("\n", stdout);   /* the user's Enter was not echoed */
        fflush(stdout);
    }
    if (s == NULL) {
        return NULL;
    }
    n = (jc_size)strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    if (n == 0) {
        return NULL;
    }
    return jc_strdup(buf);
}

/* Yes/no prompt; returns 0/1 (the `dflt` on empty / EOF). */
static int setup_yesno(struct jc_term *t, const char *label, int dflt)
{
    char prompt[320];
    char *line = NULL;
    int r = dflt;
    jc_snprintf(prompt, sizeof(prompt), "  %s [%s]: ", label,
                dflt ? "Y/n" : "y/N");
    if (jc_term_readline(t, prompt, &line) != JC_READ_LINE) {
        free(line);
        return dflt;
    }
    if (line != NULL) {
        const char *s = line;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == 'y' || *s == 'Y') r = 1;
        else if (*s == 'n' || *s == 'N') r = 0;
    }
    free(line);
    return r;
}

/* Offer to store the key in ~/.jichi.env (0600) and load it from the generated
 * start script. Best-effort and entirely optional: every failure path leaves the
 * user exactly where the old wizard left everyone -- with instructions.
 *
 * This closes the loop setup used to leave open. It wrote a config naming an
 * environment variable and a script that never loaded one, then told the user to
 * `export` it themselves -- which, done in ~/.bashrc, silently does nothing for
 * a cron or systemd run (docs/CONFIG_TUTORIAL.md 1.4). */
static enum jc_setup_key_state setup_offer_key_file(struct jc_term *t,
                                                   const char *var)
{
    char path[1024];
    char *key;
    FILE *f;
    mode_t old_umask;
    size_t klen;
    int exists;

    if (var == NULL || !jc_envvar_name_valid(var)) {
        return JC_SETUP_KEY_NONE;
    }
    if (getenv(var) != NULL && getenv(var)[0] != '\0') {
        printf("\n  $%s is already set in this shell -- nothing to store.\n",
               var);
        return JC_SETUP_KEY_ENV;
    }
    jc_snprintf(path, sizeof path, "%s/.jichi.env", jc_home_dir());
    /* M326f: "~/.jichi.env" in the prose, the resolved path only where the user
     * has to act on it (a failure). A long $HOME wrapped this sentence
     * mid-path, and every doc calls the file ~/.jichi.env anyway. */
    printf("\n  jichi can store your key in ~/.jichi.env (readable only by "
           "you) and load\n  it from the start script it writes. Otherwise you "
           "will need to export\n  $%s yourself before the first run.\n", var);
    if (!setup_yesno(t, "store the key there now?", 1)) {
        return JC_SETUP_KEY_NONE;
    }
    key = setup_secret("  paste your API key (not shown): ");
    if (key == NULL) {
        printf("  nothing entered -- skipped.\n");
        return JC_SETUP_KEY_NONE;
    }
    /* A single quote would break out of the '...' the file uses, so refuse
     * rather than emit a file that fails to source (or worse, executes). No
     * known provider's key contains one. */
    if (strchr(key, '\'') != NULL) {
        printf("  that key contains a quote -- add it to %s by hand.\n", path);
        memset(key, 0, strlen(key));
        free(key);
        return JC_SETUP_KEY_NONE;
    }
    klen = strlen(key);
    exists = jc_file_exists(path);
    old_umask = umask(077);        /* owner-only from the moment it is created */
    f = fopen(path, "a");
    umask(old_umask);
    if (f == NULL) {
        printf("  could not write %s -- export $%s by hand.\n", path, var);
        memset(key, 0, strlen(key));
        free(key);
        return JC_SETUP_KEY_NONE;
    }
    if (!exists) {
        fputs("# API keys for jichi. Loaded by the generated start script, and\n"
              "# by ~/.bashrc if you add:  [ -f ~/.jichi.env ] && . ~/.jichi.env\n"
              "# Keep this file private (chmod 600) and never commit it.\n", f);
    }
    fprintf(f, "export %s='%s'\n", var, key);
    fclose(f);
    memset(key, 0, strlen(key));
    free(key);
    /* fopen("a") on an EXISTING file keeps its mode, so the umask above only
     * covers creation; say what the mode is rather than assume it. */
    chmod(path, 0600);
    /* M326n: say how many characters were read. The prompt deliberately shows
     * nothing while you type, which leaves a screen-reader user with no
     * feedback at all and no way to catch a truncated paste -- a length is
     * confirmation that costs no secrecy. */
    printf("  read %lu characters; written to ~/.jichi.env (mode 600).\n",
           (unsigned long)klen);
    printf("  to have every terminal load it too, add to ~/.bashrc:\n");
    printf("    [ -f \"$HOME/.jichi.env\" ] && . \"$HOME/.jichi.env\"\n");
    return JC_SETUP_KEY_STORED;
}

/* Numbered menu; returns the chosen 0-based index (the `dflt` on empty/EOF). */
static int setup_menu(struct jc_term *t, const char *const *opts, int n,
                      int dflt)
{
    char prompt[64];
    char *line = NULL;
    int r = dflt;
    int i;
    for (i = 0; i < n; i++) {
        printf("    %d) %s\n", i + 1, opts[i]);
    }
    /* M326f: show the default in the prompt. Every one of these menus already
     * treats Enter as "take the default" -- it just never said so, while the
     * role menu (which does not use this helper) prompts "choice [1]:". So
     * three of the four menus in the wizard silently chose for you. `dflt` is a
     * 0-based index into opts[]; the numbers on screen are 1-based. */
    jc_snprintf(prompt, sizeof prompt, "  choice [%d]: ",
                (dflt >= 0 && dflt < n) ? dflt + 1 : 1);
    if (jc_term_readline(t, prompt, &line) != JC_READ_LINE) {
        free(line);
        return dflt;
    }
    if (line != NULL && line[0] != '\0') {
        int v = atoi(line);
        if (v >= 1 && v <= n) {
            r = v - 1;
        }
    }
    free(line);
    return r;
}

/* Lightweight, offline validation pass over what the wizard just wrote: the
 * config parses, the model is set, the key env-var is exported, the assets +
 * script exist. Renders a doctor-style checklist (no provider/network). */
static void setup_validate(const struct jc_setup_answers *ans,
                           const char *cfgpath, const char *scriptname,
                           struct jc_arena *a)
{
    struct jc_doctor d;
    struct jc_sb out;
    char *raw = NULL;
    int color = jc_color_enabled(-1, isatty(STDOUT_FILENO));
    int unicode;
    const char *lc = getenv("LC_ALL");

    if (lc == NULL || lc[0] == '\0') lc = getenv("LC_CTYPE");
    if (lc == NULL || lc[0] == '\0') lc = getenv("LANG");
    unicode = lc != NULL && (strstr(lc, "UTF-8") != NULL ||
                             strstr(lc, "UTF8") != NULL ||
                             strstr(lc, "utf-8") != NULL ||
                             strstr(lc, "utf8") != NULL);

    jc_doctor_init(&d);

    if (jc_read_file(cfgpath, &raw, NULL, a) == JC_OK) {
        cJSON *root = jc_json_parse(raw);
        if (root != NULL) {
            jc_doctor_add(&d, JC_DOC_OK, "config: valid JSON", cfgpath);
            cJSON_Delete(root);
        } else {
            jc_doctor_add(&d, JC_DOC_FAIL, "config: not valid JSON", cfgpath);
        }
    } else {
        jc_doctor_add(&d, JC_DOC_FAIL, "config: could not read", cfgpath);
    }

    if (ans->provider != NULL && ans->model != NULL) {
        char dt[256];
        jc_snprintf(dt, sizeof(dt), "%s / %s", ans->provider, ans->model);
        jc_doctor_add(&d, JC_DOC_OK, "model: configured", dt);
    } else {
        jc_doctor_add(&d, JC_DOC_FAIL, "model: missing provider/model", NULL);
    }

    if (ans->api_key_env != NULL && ans->api_key_env[0] != '\0') {
        const char *v = getenv(ans->api_key_env);
        if (ans->key_state == JC_SETUP_KEY_STORED) {
            /* M326f: the key IS set, just not in this shell -- the start script
             * sources ~/.jichi.env. Warning "env-var not set yet" here (and
             * again in next-steps, and once more in the closing tip) nagged the
             * user three times to do what they had done seconds earlier. */
            char dt[256];
            jc_snprintf(dt, sizeof(dt),
                        "~/.jichi.env (mode 600); %s loads it",
                        (scriptname != NULL) ? scriptname : "the start script");
            jc_doctor_add(&d, JC_DOC_OK, "API key: stored", dt);
        } else if (v != NULL && v[0] != '\0') {
            jc_doctor_add(&d, JC_DOC_OK, "API key: env-var is set",
                          ans->api_key_env);
        } else {
            char dt[256];
            jc_snprintf(dt, sizeof(dt), "export %s=... before the first run",
                        ans->api_key_env);
            jc_doctor_add(&d, JC_DOC_WARN, "API key: env-var not set yet", dt);
        }
    }

    if (jc_is_dir(".jichi")) {
        jc_doctor_add(&d, JC_DOC_OK, "project assets: .jichi/ present", NULL);
    }
    if (scriptname != NULL && jc_file_exists(scriptname)) {
        jc_doctor_add(&d, JC_DOC_OK, "start script: written", scriptname);
    }

    jc_sb_init(&out);
    jc_sb_append(&out, "\nValidation:\n");
    jc_doctor_render(&d, color, unicode, &out);
    fputs(out.data != NULL ? out.data : "", stdout);
    jc_sb_free(&out);
    jc_doctor_free(&d);
}

static void setup_wrap(int indent, const char *text);

/* Look at the machine, say what was found and what it would write, and -- when
 * there is someone to ask -- ask before writing it (M326p).
 *
 * jichi read CPU and RAM here unconditionally and told nobody it was a choice.
 * The reading itself is innocuous (it is local, and nothing leaves the
 * computer); what deserved consent is the WRITE, because the result lands in a
 * config the user may commit, and `maxParallelAgents: 8` discloses the size of
 * their machine to anyone who reads the repository. So: read, show, then ask.
 *
 * `interactive` is 0 on the flag-driven path, where there is nobody to ask; it
 * applies and reports, exactly as before. */
static void setup_apply_machine(struct jc_setup_answers *ans,
                                struct jc_term *t, int interactive)
{
    int cpu = jc_cpu_count();
    unsigned long mb = jc_mem_total_mb();
    int par = 0;
    int lean = 0;
    long ctx = 0;
    struct utsname u;
    int have_os = (uname(&u) == 0);

    if (ans->max_parallel <= 0 && cpu > 0) {
        par = (cpu > 8) ? 8 : cpu;
    }
    if (jc_resource_tier(mb, cpu) != JC_RES_NORMAL) {
        lean = (ans->low_resource <= 0);
        ctx = (ans->context_limit <= 0) ? 16384 : 0;
    }
    if (par == 0 && !lean && ctx == 0) {
        return;                      /* nothing to propose: say nothing */
    }

    printf("\nI looked at this machine:\n");
    printf("  %d core(s)", cpu);
    if (mb > 0) {
        printf(", %lu MB RAM", mb);
    }
    printf("\n");
    if (have_os) {
        /* Its OWN line, and bounded. Inline after the core/RAM figures this
         * reached 94 columns on an Android tablet, because a vendor kernel
         * puts its build provenance in uname's release -- see jc_os_line
         * (M459). Splitting it off is what keeps the real string intact on
         * every platform seen so far; the bound is what keeps the 76-column
         * contract true on the one that has not been seen yet. */
        char osline[160];
        jc_os_line(u.sysname, u.release, u.machine, 76, osline, sizeof(osline));
        printf("  %s\n", osline);
    }
    printf("  Nothing left your computer.\n\nThat suggests:\n");
    if (lean) {
        printf("  \"lowResource\": true       lean tool set, smaller caps\n");
    }
    if (ctx > 0) {
        printf("  \"contextLimit\": %ld\n", ctx);
    }
    if (par > 0) {
        printf("  \"maxParallelAgents\": %d    your core count\n", par);
    }
    if (interactive) {
        setup_wrap(0, "\nThese end up in a config file you may commit, which "
                      "would disclose your machine's size to anyone reading "
                      "it. You can change or delete them there at any time.");
        if (!setup_yesno(t, "write them into the config?", 1)) {
            printf("  skipped -- no machine-derived keys written.\n");
            return;
        }
    }
    if (par > 0) ans->max_parallel = par;
    if (lean)    ans->low_resource = 1;
    if (ctx > 0) ans->context_limit = ctx;
}

/* Scaffold the pack, write the config + start-script, print next steps. Shared
 * by the interactive and flag-driven paths. */
static int setup_emit(const struct jc_setup_preset *preset,
                      const struct jc_setup_preset *journey,
                      const struct jc_setup_preset *machine,
                      const struct jc_setup_preset *stance,
                      const struct jc_scaffold_pack *pack,
                      const struct jc_setup_answers *ans,
                      const char *cfgpath, int force,
                      int from_global, const char *inherit_keys,
                      struct jc_arena *scratch)
{
    const char *scriptname;
    int wrote = 0, skipped = 0, failed = 0;

    /* M326m: name every axis that was applied. With four of them, reporting
     * only the first two leaves a user unable to tell whether the answer they
     * gave three questions ago took effect. */
    printf("Setting up: %s", preset->name);
    if (journey != NULL) printf(" + %s", journey->name);
    if (machine != NULL) printf(" + %s", machine->name);
    if (stance  != NULL) printf(" + %s", stance->name);
    printf("\n");
    printf("\nAssets (pack: %s):\n", pack->name);
    scaffold_write_pack(pack, 0, force, 0, &wrote, &skipped, &failed);
    /* M326j: the journey's assets land BESIDE the role's, not instead of them
     * -- that is what orthogonal means here. scaffold_write_pack already skips
     * files that exist, so anything both packs ship is written once and the
     * second pass reports it as a skip. */
    {
        const struct jc_setup_preset *extra[3];
        const char *what[3];
        int e;
        extra[0] = journey; what[0] = "journey";
        extra[1] = machine; what[1] = "machine";
        extra[2] = stance;  what[2] = "stance";
        for (e = 0; e < 3; e++) {
            const struct jc_scaffold_pack *xp;
            if (extra[e] == NULL) continue;
            xp = jc_scaffold_find_pack(extra[e]->scaffold_pack);
            if (xp == NULL || xp == pack) continue;
            printf("\nAssets (%s pack: %s):\n", what[e], xp->name);
            scaffold_write_pack(xp, 0, force, 0, &wrote, &skipped, &failed);
        }
    }

    printf("\nConfig:\n");
    {
        int exists = jc_file_exists(cfgpath);
        int merged = 0;      /* gap-filled into the target's existing config */
        int inherited = 0;   /* seeded from the global config (--from-global) */
        struct jc_sb cfg;
        jc_status cst;
        jc_sb_init(&cfg);
        if (exists && !force) {
            /* Gap-fill merge into the existing config, preserving hand-edits
             * (M53). Falls back to a fresh build if the file won't parse. */
            char *raw = NULL;
            if (jc_read_file(cfgpath, &raw, NULL, scratch) == JC_OK &&
                jc_setup_merge_config(raw, ans, &cfg) == JC_OK) {
                merged = 1;
                cst = JC_OK;
            } else {
                cst = jc_setup_build_config(ans, &cfg);
            }
        } else if (from_global) {
            /* Seed the project config from the global ~/.jichi (W5): the
             * user's global models/roles/keys-env carry over, and preset answers
             * fill only the gaps. Falls back to a fresh build if the global
             * config is absent/unparseable. */
            char home[1024];
            char *raw = NULL;
            jc_snprintf(home, sizeof(home), "%s/.jichi", jc_home_dir());
            if (jc_file_exists(home) &&
                jc_read_file(home, &raw, NULL, scratch) == JC_OK &&
                jc_setup_inherit_config(raw, inherit_keys, ans, &cfg) == JC_OK) {
                inherited = 1;
                cst = JC_OK;
            } else {
                fprintf(stderr, "  ! no readable global config at %s; "
                                "building a fresh one\n", home);
                cst = jc_setup_build_config(ans, &cfg);
            }
        } else {
            cst = jc_setup_build_config(ans, &cfg);
        }
        if (cst != JC_OK) {
            fprintf(stderr, "  ! could not build config\n");
            jc_sb_free(&cfg);
            return 1;
        }
        scaffold_mkparent(cfgpath);
        if (jc_write_file(cfgpath, cfg.data != NULL ? cfg.data : "", cfg.len)
            != JC_OK) {
            fprintf(stderr, "  ! %s (write failed)\n", cfgpath);
            jc_sb_free(&cfg);
            return 1;
        }
        jc_sb_free(&cfg);
        printf("  %s %s%s\n", (merged || inherited) ? "~" : "+", cfgpath,
               merged ? " (merged; existing keys kept)"
               : inherited ? (inherit_keys != NULL && inherit_keys[0] != '\0'
                              ? " (inherited selected keys from global)"
                              : " (inherited from global config)")
               : "");
    }

    scriptname = jc_setup_script_name(preset);
    printf("\nStart script:\n");
    if (jc_file_exists(scriptname) && !force) {
        printf("  = %s (exists; --force to overwrite)\n", scriptname);
    } else {
        struct jc_sb sc;
        jc_sb_init(&sc);
        jc_setup_start_script(preset, cfgpath, &sc);
        if (jc_write_file(scriptname, sc.data != NULL ? sc.data : "", sc.len)
            == JC_OK) {
            jc_make_executable(scriptname);
            printf("  + %s (chmod +x)\n", scriptname);
        } else {
            fprintf(stderr, "  ! %s (write failed)\n", scriptname);
        }
        jc_sb_free(&sc);
    }

    setup_validate(ans, cfgpath, scriptname, scratch);

    /* M326k: what was left unset, and where to set it. The wizard asks ~15
     * optional questions and a user who pressed Enter through them has no
     * record of what they declined -- so the offer to "configure it later"
     * was true and unusable. Each line names the capability that is absent
     * (not just the key), because "no verify" means nothing to someone who
     * has not read AUTONOMY.md. */
    {
        struct { int unset; const char *what; const char *key; } skipped[] = {
            { 0, "verifier gate for --auto runs", "\"verify\": \"make test\"" },
            { 0, "run_tests default command",     "\"testCommand\": \"make test\"" },
            { 0, "codebase_search / docs search", "a model with \"roles\": [\"embed\"]" },
            { 0, "code navigation (defs, refs)", "\"lspServers\": {...}" },
            { 0, "MCP servers",                   "\"mcpServers\": {...}" },
            { 0, "external doc retrieval",        "\"docs\": [{...}]" },
            { 0, "cheap-then-strong routing",     "\"routing\": {\"fast\",\"strong\"}" },
            { 0, "web_search",                    "\"search\": {\"url\": ...}" },
            { 0, "telemetry (cost + tool stats)", "\"logging\": \"metrics\"" },
            { 0, "model pricing (else costs are $0)",
                 "\"inputCostPer1M\"/\"outputCostPer1M\"" }
        };
        int n = (int)(sizeof skipped / sizeof skipped[0]);
        int i, any = 0;
        skipped[0].unset = (ans->verify == NULL);
        skipped[1].unset = (ans->test_command == NULL);
        skipped[2].unset = (ans->embed_model == NULL);
        skipped[3].unset = (ans->nlsp == 0);
        skipped[4].unset = (ans->nmcp == 0);
        skipped[5].unset = (ans->ndocs == 0);
        skipped[6].unset = (ans->route_fast == NULL || ans->route_strong == NULL);
        skipped[7].unset = (ans->search_url == NULL);
        skipped[8].unset = (ans->log_level == NULL);
        /* M357: setup never asks for per-1M prices, so this is not a decline
         * -- it is named exactly when telemetry WILL be collecting cost rows,
         * because that is when all-zero pricing turns /cost, the prompt's $
         * segment and the telemetry summary into zeros that look like data. */
        skipped[9].unset = (ans->log_level != NULL);
        for (i = 0; i < n; i++) {
            any |= skipped[i].unset;
        }
        if (any) {
            printf("\nLeft unset -- edit %s to add any of these later:\n",
                   cfgpath);
            for (i = 0; i < n; i++) {
                if (skipped[i].unset) {
                    printf("  %-34s %s\n", skipped[i].what, skipped[i].key);
                }
            }
        }
    }

    /* M326n: show the FILE, not just its path. The connection between "I
     * answered four questions" and "this is what a config looks like" is the
     * whole lesson, and printing a path leaves it invisible. Reading it back
     * from disk rather than re-rendering guarantees what is shown is what was
     * written. */
    {
        char *text = NULL;
        struct jc_arena *sc = scratch;
        if (sc != NULL && jc_read_file(cfgpath, &text, NULL, sc) == JC_OK &&
            text != NULL) {
            printf("\nThis is your config (%s). Open it in any editor:\n\n",
                   cfgpath);
            {
                const char *ln = text;
                while (*ln != '\0') {
                    const char *nl = strchr(ln, '\n');
                    int len = (nl != NULL) ? (int)(nl - ln) : (int)strlen(ln);
                    /* Keep the dump inside 76 columns, and SAY when a line
                     * was cut -- a silently truncated JSON line reads as
                     * malformed rather than as elided. */
                    if (len > 70) {
                        printf("  %.*s ...\n", 70, ln);
                    } else {
                        printf("  %.*s\n", len, ln);
                    }
                    if (nl == NULL) break;
                    ln = nl + 1;
                }
            }
            printf("\n  $EDITOR %s\n", cfgpath);
        }
    }
    /* And what the minimum actually is -- so a learner knows the file above is
     * elaborate by choice, not by necessity, and that writing one by hand is
     * not an advanced skill. */
    printf("\nA config only needs this much:\n\n");
    printf("  {\"models\": [{\"provider\": \"openai\",\n");
    printf("               \"model\": \"gpt-4o\",\n");
    printf("               \"apiKeyEnv\": \"JICHI_API_KEY\"}]}\n");

    printf("\nDone. Next steps:\n");
    /* M326f: only advise supplying the key when it is actually missing, and
     * when it is, advise the form that PERSISTS. The bare `export` this used to
     * print unconditionally lasts one terminal, and put in ~/.bashrc it is
     * still not read by a cron or systemd run (that shell is not interactive,
     * so the stock ~/.bashrc returns before reaching it). */
    if (ans->api_key_env != NULL && ans->api_key_env[0] != '\0' &&
        ans->key_state == JC_SETUP_KEY_NONE) {
        printf("  ( umask 077; echo \"export %s='<your key>'\" >> "
               "~/.jichi.env )\n", ans->api_key_env);
        printf("      # the start script loads that file; the key is never "
               "written to the config\n");
    }
    printf("  ./%s\n", scriptname);
    printf("  jichi --config %s doctor   # full validation (+ network)\n",
           cfgpath);
    return 0;
}

/* Recursively tally recognized source-file extensions under `dir` into the
 * per-pack counters `t` (bounded by depth + a file `budget`), skipping hidden
 * and common build/vendor dirs (M52). */
static void setup_tally_lang(const char *dir, int *counts,
                             const char *const *packs, int npacks, int depth,
                             int *budget, struct jc_arena *a)
{
    struct jc_vec names;
    jc_size i;
    if (depth > 4 || *budget <= 0) {
        return;
    }
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, a) != JC_OK) {
        jc_vec_free(&names);
        return;
    }
    for (i = 0; i < names.len && *budget > 0; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char path[2048];
        if (nm[0] == '.') {
            continue; /* hidden, incl .git */
        }
        if (strcmp(nm, "node_modules") == 0 || strcmp(nm, "target") == 0 ||
            strcmp(nm, "build") == 0 || strcmp(nm, "dist") == 0 ||
            strcmp(nm, "vendor") == 0 || strcmp(nm, "third_party") == 0) {
            continue;
        }
        jc_snprintf(path, sizeof(path), "%s/%s", dir, nm);
        if (jc_is_dir(path)) {
            setup_tally_lang(path, counts, packs, npacks, depth + 1, budget, a);
        } else {
            const char *dot = strrchr(nm, '.');
            if (dot != NULL) {
                const char *pack = jc_setup_lang_for_ext(dot + 1);
                int k;
                for (k = 0; pack != NULL && k < npacks; k++) {
                    if (strcmp(packs[k], pack) == 0) {
                        counts[k]++;
                        break;
                    }
                }
            }
            (*budget)--;
        }
    }
    jc_vec_free(&names);
}

/* Detect the language scaffold pack `dir` most looks like, or NULL. (M52) */
static const char *setup_detect_lang(const char *dir, struct jc_arena *a)
{
    static const char *const PACKS[] = {
        "c-cli", "python-cli", "zig-cli", "godot",
        "rust-cli", "go-cli", "web-ts"
    };
    int counts[7];
    int npacks = (int)(sizeof(PACKS) / sizeof(PACKS[0]));
    int budget = 2000;
    int best = -1;
    int i;
    for (i = 0; i < npacks; i++) {
        counts[i] = 0;
    }
    setup_tally_lang(dir, counts, PACKS, npacks, 0, &budget, a);
    for (i = 0; i < npacks; i++) {
        if (counts[i] > 0 && (best < 0 || counts[i] > counts[best])) {
            best = i;
        }
    }
    return (best >= 0) ? PACKS[best] : NULL;
}

/* Print `text` wrapped to <=76 columns, each line prefixed by `indent` spaces.
 * M326n: five of the wizard's lines ran to 118 columns, which wraps mid-word in
 * a narrow terminal and is worst exactly where it matters -- under screen
 * magnification, where ~80 columns is often the whole working width. */
static void setup_wrap_pfx(const char *first, int indent, const char *text);

static void setup_wrap(int indent, const char *text)
{
    setup_wrap_pfx(NULL, indent, text);
}

/* As setup_wrap, but `first` (e.g. "        why:  ") labels the first line and
 * COUNTS toward the width -- printing a label and then wrapping the remainder
 * to the full width is how the why-lines came out at 80 columns. */
static void setup_wrap_pfx(const char *first, int indent, const char *text)
{
    const int WIDTH = 76;
    const char *p = text;
    int col;
    int firstline = 1;
    while (p != NULL && *p != '\0') {
        const char *brk;
        const char *q;
        int i;
        if (firstline && first != NULL) {
            printf("%s", first);
            col = (int)strlen(first);
        } else {
            for (i = 0; i < indent; i++) {
                putchar(' ');
            }
            col = indent;
        }
        firstline = 0;
        brk = NULL;
        for (q = p; *q != '\0'; q++) {
            if (*q == ' ') {
                brk = q;
            }
            if (col + (int)(q - p) >= WIDTH && brk != NULL) {
                break;
            }
        }
        if (*q == '\0') {
            printf("%s\n", p);
            return;
        }
        printf("%.*s\n", (int)(brk - p), p);
        p = brk + 1;
    }
}

/* One menu entry, in whichever of the two shapes is wanted.
 *
 * The teaching shape (M326n) shows the config keys the choice writes, derived
 * from the preset's own feature bitmask, and a line on why you would pick it --
 * so a learner who chooses an option has already read the key they would have
 * typed by hand. Under --accessible the alignment padding goes away in favour
 * of plain `label: value` lines, which a screen reader announces cleanly. */
static void setup_print_option(int n, const struct jc_setup_preset *p,
                               int accessible)
{
    char sets[240];
    jc_setup_preset_sets(p, sets, sizeof sets);
    if (accessible) {
        printf("option %d: %s\n", n, p->name);
        setup_wrap(2, p->description);
        setup_wrap_pfx("  writes: ", 2, sets);
        if (p->why != NULL) {
            setup_wrap_pfx("  why: ", 2, p->why);
        }
        return;
    }
    printf("   %2d) %s\n", n, p->name);
    setup_wrap(8, p->description);
    setup_wrap_pfx("        sets: ", 14, sets);
    if (p->why != NULL) {
        setup_wrap_pfx("        why:  ", 14, p->why);
    }
}

/* Drive the interactive wizard, filling the preset/pack/answers/cfgpath outputs.
 * Returns 0 on success, -1 if the user aborted (EOF / Ctrl-C). */
static int setup_interactive(struct jc_term *t, struct jc_arena *a,
                             int accessible,
                             const struct jc_setup_preset **preset_out,
                             const struct jc_setup_preset **journey_out,
                             const struct jc_setup_preset **machine_out,
                             const struct jc_setup_preset **stance_out,
                             const struct jc_scaffold_pack **pack_out,
                             struct jc_setup_answers *ans, char *cfgpath,
                             jc_size cfgcap)
{
    static const char *const PROVS[] = {
        "anthropic", "openai", "other (OpenAI-compatible apiBase)"
    };
    static const char *const LANGS[] = {
        "default", "c-cli", "python-cli", "zig-cli", "godot",
        "rust-cli", "go-cli", "web-ts"
    };
    static const char *const TGTS[] = {
        "./local/config.json  (project-local, recommended)",
        "~/.jichi  (global)", "custom path"
    };
    static const char *const MODES[] = { "chat", "plan", "auto" };
    const struct jc_setup_preset *preset;
    const char *packname;
    const char *tree_root = NULL;   /* M326p: existing-tree's path, if asked */
    int np = jc_setup_preset_count();
    int i;
    char *line = NULL;

    /* M326j: two questions, because they ARE two questions -- the source has
     * called journeys "orthogonal to the roles" since M183 while the code
     * applied exactly one preset, so the screen promised a composition that
     * did not exist. Roles first; the journey is optional and layers on top,
     * the way --profile already does (jc_setup_apply_complexity). */
    printf("jichi setup -- a helper, not the only way.\n\n");
    setup_wrap(0,
        "This writes a plain text file (a JSON config). Everything it asks, "
        "you can also write by hand in any editor -- and change the same way "
        "afterwards, which is where the real work happens. Nothing chosen "
        "here is permanent, and each option below shows the config keys it "
        "writes, so you can watch the file take shape.");
    {
        char plat[160];
        if (jc_platform_describe(plat, sizeof plat) && !jc_platform_is_linux()) {
            printf("\n");
            setup_wrap(0, "Note: this looks like a system jichi is not tested "
                          "on. Most of it is plain POSIX and should work, but "
                          "the memory watchdog (memBudgetMb) needs procfs and "
                          "will not -- `jichi doctor` says so if you set it.");
            printf("  detected: %s\n", plat);
        }
    }
    printf("\nPress Enter at any question to take the default in "
           "[brackets].\n");
    /* M326m: "who are you?" was never what this asked. Six of the seven
     * remaining entries differ from each other ONLY in which scaffold pack
     * they select -- that is a domain, not a person. The two that differed by
     * mode and start script (`tester`, `reviewer`) moved to the journey axis,
     * where "what am I doing right now" already lives. */
    printf("\nWhat are you working on?\n");
    {
        int shown = 0;
        for (i = 0; i < np; i++) {
            const struct jc_setup_preset *p = jc_setup_preset_at(i);
            if (p->axis != JC_AXIS_ROLE) {
                continue;   /* the later questions' business */
            }
            setup_print_option(++shown, p, accessible);
        }
    }
    i = 0;
    {
        const struct jc_setup_preset *d = jc_setup_preset_at(0);
        char prompt[96];
        jc_snprintf(prompt, sizeof prompt, "  choice [1 = %s]: ",
                    (d != NULL && d->name != NULL) ? d->name : "1");
        if (jc_term_readline(t, prompt, &line) != JC_READ_LINE) {
            free(line);
            return -1;
        }
    }
    if (line != NULL && line[0] != '\0') {
        int v = atoi(line);
        int seen = 0, k;
        /* Walk the table counting ROLES rather than assuming menu position ==
         * array index. That assumption held only while the non-roles sat in a
         * block at the end; M326k moved `small-local` to the machine axis from
         * the MIDDLE of the list, which punched a hole -- and picking "9)
         * learner" silently selected small-local instead. Counting cannot go
         * wrong wherever a future entry lands. */
        for (k = 0; k < np && v > 0; k++) {
            if (jc_setup_preset_at(k)->axis == JC_AXIS_ROLE && ++seen == v) {
                i = k;
                break;
            }
        }
    }
    free(line);
    line = NULL;
    preset = jc_setup_preset_at(i);
    *preset_out = preset;

    /* The second question. Optional, and the default is "no journey" -- so
     * Enter reaches exactly the behaviour setup had before M326j. */
    printf("\nWhat are you walking into?  (optional -- Enter to skip)\n");
    printf("     0) nothing in particular\n");
    {
        int shown = 0;
        for (i = 0; i < np; i++) {
            const struct jc_setup_preset *p = jc_setup_preset_at(i);
            if (p->axis != JC_AXIS_JOURNEY) {
                continue;
            }
            /* The descriptions carry a "Journey: " prefix from when they
             * shared a list with the roles; under this heading it is noise. */
            {
                setup_print_option(++shown, p, accessible);
            }
        }
    }
    if (jc_term_readline(t, "  choice [0 = nothing in particular]: ",
                         &line) != JC_READ_LINE) {
        free(line);
        return -1;
    }
    if (line != NULL && line[0] != '\0') {
        int v = atoi(line);
        int seen = 0, k;
        for (k = 0; k < np && v > 0; k++) {
            const struct jc_setup_preset *p = jc_setup_preset_at(k);
            if (p->axis == JC_AXIS_JOURNEY && ++seen == v) {
                *journey_out = p;
                break;
            }
        }
    }
    free(line);
    line = NULL;

    /* M326k: the third axis. `small-local` used to sit in the role list, where
     * it answered a different question from every entry around it -- your
     * hardware, not your work -- so "a learner on a 7B model" was unsayable. */
    {
        int nmach = 0, k;
        for (k = 0; k < np; k++) {
            if (jc_setup_preset_at(k)->axis == JC_AXIS_MACHINE) nmach++;
        }
        if (nmach > 0) {
            int shown = 0;
            printf("\nWhat are you running on?  (optional -- Enter to skip)\n");
            printf("     0) a normal machine, or a hosted model\n");
            for (i = 0; i < np; i++) {
                const struct jc_setup_preset *p = jc_setup_preset_at(i);
                if (p->axis != JC_AXIS_MACHINE) continue;
                setup_print_option(++shown, p, accessible);
            }
            if (jc_term_readline(t, "  choice [0 = a normal machine]: ",
                                 &line) != JC_READ_LINE) {
                free(line);
                return -1;
            }
            if (line != NULL && line[0] != '\0') {
                int v = atoi(line);
                int seen = 0;
                for (k = 0; k < np && v > 0; k++) {
                    const struct jc_setup_preset *p = jc_setup_preset_at(k);
                    if (p->axis == JC_AXIS_MACHINE && ++seen == v) {
                        *machine_out = p;
                        break;
                    }
                }
            }
            free(line);
            line = NULL;
            /* M326p: the one machine entry that needs an answer of its own.
             * Mirrors the rewrite journey's prompt -- the tree becomes a
             * read-only referenceRoots entry, readable and searchable through
             * the path fence but never writable, even by an unattended run. */
            if (*machine_out != NULL &&
                strcmp((*machine_out)->name, "existing-tree") == 0) {
                /* Held in a LOCAL: jc_setup_answers_init below zeroes the
                 * struct, so anything written into `ans` before that point is
                 * silently discarded. */
                tree_root = setup_line(t, a,
                    "path of the tree you build against (read-only)", "");
            }
        }
    }

    /* M326m: stance -- how you are working, orthogonal to what you build, what
     * you are doing to it, and what you run it on. It is the axis with the
     * most leverage, because it changes how jichi TALKS rather than only what
     * it scaffolds.
     *
     * It DEFAULTS to learner, deliberately (operator's decision): most users
     * are learning, and the self-learner alone is the person least able to
     * configure their way out of a bad default. A professional pays one
     * keystroke; the reverse would cost a learner the machinery they came
     * for. The FLAG path defaults to none instead -- see --stance -- because a
     * scripted `setup --preset developer` silently gaining assignment
     * scaffolding would be a surprise, not a kindness. */
    {
        int nst = 0, k;
        for (k = 0; k < np; k++) {
            if (jc_setup_preset_at(k)->axis == JC_AXIS_STANCE) nst++;
        }
        if (nst > 0) {
            int shown = 0, first = -1;
            printf("\nHow are you working?\n");
            for (i = 0; i < np; i++) {
                const struct jc_setup_preset *p = jc_setup_preset_at(i);
                if (p->axis != JC_AXIS_STANCE) continue;
                if (first < 0) first = i;
                setup_print_option(++shown, p, accessible);
            }
            printf("     0) %-16s no teaching scaffolding\n", "professionally");
            *stance_out = (first >= 0) ? jc_setup_preset_at(first) : NULL;
            if (jc_term_readline(t, "  choice [1 = learner]: ", &line)
                    != JC_READ_LINE) {
                free(line);
                return -1;
            }
            if (line != NULL && line[0] != '\0') {
                int v = atoi(line);
                int seen = 0;
                *stance_out = NULL;              /* 0 / anything else = none */
                for (k = 0; k < np && v > 0; k++) {
                    const struct jc_setup_preset *p = jc_setup_preset_at(k);
                    if (p->axis == JC_AXIS_STANCE && ++seen == v) {
                        *stance_out = p;
                        break;
                    }
                }
            }
            free(line);
            line = NULL;
        }
    }

    jc_setup_answers_init(ans);
    if (tree_root != NULL && tree_root[0] != '\0') {
        ans->reference_root = tree_root;   /* set AFTER the init that zeroes */
    }

    printf("\nModel provider:\n");
    i = setup_menu(t, PROVS, 3, 1);
    ans->provider = (i == 0) ? "anthropic" : "openai";
    if (i == 2) {
        ans->api_base = setup_line(t, a, "apiBase URL",
                                   "http://localhost:1234/v1");
        if (ans->api_base == NULL) return -1;
    }
    ans->model = setup_line(t, a, "model id",
                            (i == 0) ? "claude-opus-4-8" : "gpt-4o");
    if (ans->model == NULL) return -1;
    ans->model_name = "chat";
    /* M326e: this answer is a variable NAME. The old label ("API key env var")
     * named the secret first and the thing being asked for second, so a user
     * holding an sk-... pasted it -- producing a config that could never work,
     * with the key echoed and written to disk. Say what is wanted, then refuse
     * anything a shell could not export and re-ask. */
    printf("\n  Your key is never stored in the config: jichi reads it at run "
           "time from an\n  environment variable. Enter that variable's NAME "
           "(not the key).\n");
    for (;;) {
        ans->api_key_env = setup_line(t, a,
            "name of the env var holding your API key",
            strcmp(ans->provider, "anthropic") == 0 ? "ANTHROPIC_API_KEY"
                                                     : "JICHI_API_KEY");
        if (ans->api_key_env == NULL) return -1;   /* EOF / Ctrl-C */
        if (jc_envvar_name_valid(ans->api_key_env)) {
            break;
        }
        setup_key_env_explain(stdout, NULL);
    }
    /* And now the only prompt that takes the key itself -- optional, no-echo,
     * written to a 0600 file the generated start script loads (M326e). The
     * outcome is recorded (M326f) so the closing checklist and next-steps stop
     * telling the user to export a key the wizard just stored. */
    ans->key_state = setup_offer_key_file(t, ans->api_key_env);

    jc_setup_apply_preset(ans, preset);
    /* M326j: role first, journey second. jc_setup_apply_preset only fills what
     * is unset and ORs features in, so applying it twice composes with no new
     * merge code -- and second means the journey wins wherever the role left a
     * gap, which is the right way round: the role is who you are, the journey
     * is the work in front of you. */
    if (*journey_out != NULL) {
        jc_setup_apply_preset(ans, *journey_out);
    }
    /* Machine last: a hardware constraint is the most specific thing said, and
     * lowResource/contextLimit should not be undone by anything after it. */
    if (*machine_out != NULL) {
        jc_setup_apply_preset(ans, *machine_out);
    }
    /* Stance last: journey owns `mode` (a review journey is plan mode whoever
     * is doing it), so applying stance after it means stance fills only what
     * the work itself left open. */
    if (*stance_out != NULL) {
        jc_setup_apply_preset(ans, *stance_out);
    }

    /* M183 (rewrite journey): the one answer this journey cannot proceed
     * without -- the tree being ported FROM, which becomes a read-only
     * referenceRoots entry. */
    if (strcmp(preset->name, "rewrite") == 0) {
        ans->reference_root = setup_line(t, a,
            "path of the codebase you are porting FROM (read-only)", "");
        if (ans->reference_root != NULL && ans->reference_root[0] == '\0') {
            ans->reference_root = NULL;
        }
    }

    packname = preset->scaffold_pack;
    /* M326k: the ROLE owns the primary pack, so the role decides whether a
     * language pack replaces it -- and a journey does too, so that choosing
     * `small-project` still asks when it is layered rather than picked alone.
     * The MACHINE axis deliberately does not: `small-local` asks when it IS
     * the preset, but a technical-writer on a small model must keep the docs
     * pack, not be swapped onto c-cli by a statement about their hardware. */
    if (preset->asks_language ||
        (*journey_out != NULL && (*journey_out)->asks_language)) {
        const char *det = setup_detect_lang(".", a);
        int dflt = 0;
        if (det != NULL) {
            int k;
            /* M183 drive-by fix: this loop was bounded k < 5 while LANGS
             * has 8 entries, so a detected rust-cli/go-cli/web-ts never
             * became the menu default. */
            for (k = 0; k < (int)(sizeof(LANGS) / sizeof(LANGS[0])); k++) {
                if (strcmp(LANGS[k], det) == 0) {
                    dflt = k;
                    break;
                }
            }
            printf("\nLanguage pack (detected: %s):\n", det);
        } else {
            printf("\nLanguage pack:\n");
        }
        packname = LANGS[setup_menu(t, LANGS,
                                    (int)(sizeof(LANGS) / sizeof(LANGS[0])),
                                    dflt)];
    }
    *pack_out = jc_scaffold_find_pack(packname);
    if (*pack_out == NULL) {
        *pack_out = jc_scaffold_find_pack("default");
    }

    printf("\nWrite the config where?\n");
    i = setup_menu(t, TGTS, 3, 0);
    if (i == 1) {
        jc_snprintf(cfgpath, cfgcap, "%s/.jichi", jc_home_dir());
    } else if (i == 2) {
        const char *cp = setup_line(t, a, "config path", "config.json");
        if (cp == NULL) return -1;
        jc_snprintf(cfgpath, cfgcap, "%s", cp);
    } else {
        jc_snprintf(cfgpath, cfgcap, "local/config.json");
    }

    printf("\n");
    if (setup_yesno(t, "Accept this role's default features?", 1)) {
        return 0;
    }

    /* Comprehensive customization (single-entry per subsystem).
     *
     * M326k: the convention is stated ONCE here rather than repeated on every
     * prompt. Fifteen questions each carrying "(blank = none; config key ...)"
     * is noise a reader stops seeing by the fourth one; the same three facts
     * said once, plus a report at the end of what was actually left unset, is
     * what a user needs to proceed without fear of getting it wrong. */
    printf("\nOptional features. Nothing below is required:\n");
    printf("  * Enter accepts the default shown in [brackets].\n");
    printf("  * A blank line skips the setting entirely.\n");
    printf("  * All of it can be changed later by editing the config file --\n"
           "    setup prints what you skipped, and the key to set it under.\n");
    {
        int md = (ans->mode != NULL && strcmp(ans->mode, "plan") == 0) ? 1
               : (ans->mode != NULL && strcmp(ans->mode, "auto") == 0) ? 2 : 0;
        ans->mode = MODES[setup_menu(t, MODES, 3, md)];
    }
    ans->snapshots = setup_yesno(t, "snapshots (checkpoints + /undo)?",
                                 ans->snapshots == 1);
    ans->references = setup_yesno(t, "@-references in messages?",
                                  ans->references != 0);
    ans->repo_map = setup_yesno(t, "inject a repository map?", 1);
    {
        const char *s = setup_line(t, a, "test command (blank = none)",
                                   ans->test_command);
        ans->test_command = (s != NULL && s[0] != '\0') ? s : NULL;
        s = setup_line(t, a, "verify command (blank = none)", ans->verify);
        ans->verify = (s != NULL && s[0] != '\0') ? s : NULL;
    }
    ans->hooks = setup_yesno(t, "enable lifecycle hooks?", ans->hooks == 1);
    if (setup_yesno(t, "enable telemetry (metrics) logging?",
                    ans->log_level != NULL)) {
        ans->log_level = "metrics";
    } else {
        ans->log_level = NULL;
    }
    {
        const char *em = setup_line(t, a, "embed model id (blank = none)",
                                    ans->embed_model);
        ans->embed_model = (em != NULL && em[0] != '\0') ? em : NULL;
    }
    if (setup_yesno(t, "configure a language server (LSP)?", 0)) {
        struct jc_lsp_suggestion sg;
        const char *dcmd = "clangd", *dext = "c,h";
        /* Default the server from the project's language pack (M114): c-cli ->
         * clangd, zig-cli -> zls, go-cli -> gopls, ... */
        if (packname != NULL && packname[0] != '\0') {
            char lang[32];
            jc_size k = 0;
            while (packname[k] != '\0' && packname[k] != '-' &&
                   k < sizeof(lang) - 1) { lang[k] = packname[k]; k++; }
            lang[k] = '\0';
            if (jc_lsp_suggest(lang, &sg)) {
                dcmd = sg.command;
                dext = sg.extensions;
                printf("  suggested: %s (%s)\n", sg.command, sg.install);
            }
        }
        ans->lsp[0].name = setup_line(t, a, "  lsp name", dcmd);
        ans->lsp[0].command = setup_line(t, a, "  lsp command", dcmd);
        ans->lsp[0].extensions = setup_line(t, a, "  file extensions (csv)",
                                            dext);
        ans->nlsp = 1;
    }
    if (setup_yesno(t, "add an MCP server (stdio)?", 0)) {
        ans->mcp[0].name = setup_line(t, a, "  mcp name", "fs");
        ans->mcp[0].command = setup_line(t, a, "  mcp command", "");
        ans->mcp[0].args = setup_line(t, a, "  mcp args (space-sep)", "");
        ans->nmcp = 1;
    }
    if (setup_yesno(t, "add a docs source (for @docs / search_docs)?", 0)) {
        ans->docs[0].name = setup_line(t, a, "  docs name", "guide");
        ans->docs[0].path = setup_line(t, a, "  docs directory", "docs/");
        ans->ndocs = 1;
    }
    if (setup_yesno(t, "configure fast/strong model routing?", 0)) {
        const char *f = setup_line(t, a, "  fast model selector", "");
        const char *st = setup_line(t, a, "  strong model selector", "");
        ans->route_fast = (f != NULL && f[0] != '\0') ? f : NULL;
        ans->route_strong = (st != NULL && st[0] != '\0') ? st : NULL;
    }
    if (setup_yesno(t, "configure a web-search backend?", 0)) {
        const char *u = setup_line(t, a, "  search URL", "");
        ans->search_url = (u != NULL && u[0] != '\0') ? u : NULL;
        if (ans->search_url != NULL) {
            ans->search_key_env = setup_line(t, a, "  search key env var",
                                             "TAVILY_API_KEY");
        }
    }
    /* M326q: OS-appropriate DEFAULTS for two commands jichi shells out to.
     * The detection picks the value; the user still chooses whether to enable
     * it -- writing a `sound` key registers the play_audio/record_audio tools,
     * so seeding one silently would advertise two mutating tools to a project
     * that never asked for sound. */
    {
        int linux_host = jc_platform_is_linux();
        if (setup_yesno(t, "let jichi play sound / notify you when a run ends?",
                        0)) {
            const char *sp = setup_line(t, a, "  sound player command",
                                        linux_host ? "aplay" : "afplay");
            const char *nc = setup_line(t, a, "  notification command",
                linux_host ? "notify-send jichi"
                           : "osascript -e 'display notification \"jichi\"'");
            ans->sound_play = (sp != NULL && sp[0] != '\0') ? sp : NULL;
            ans->notify_cmd = (nc != NULL && nc[0] != '\0') ? nc : NULL;
            if (ans->sound_play != NULL) {
                setup_wrap(2, "note: a \"sound\" key registers the play_audio "
                              "and record_audio tools, so the model can use "
                              "them. Delete the key to take them away again.");
            }
        }
    }
    return 0;
}

/* `setup` -> the interactive project-setup wizard (M48): interactive on a TTY,
 * or flag-driven (--preset/--provider/--model/--key-env/...) for scripting. */
/* `setup --import <path>`: convert a Continue/opencode config into a jichi config
 * plus its .jichi/ asset tree. Reuses the jichi-convert core (linked into this
 * binary). Non-destructive unless --force. */
static int setup_import(struct cli_args *args, struct jc_arena *arena)
{
    char *text;
    enum jc_src_format fmt;
    struct jc_convert_result res;
    char cfgpath[1100];
    const char *base = ".";
    int i;
    int rc = 0;

    if (jc_read_file(args->setup_import, &text, NULL, arena) != JC_OK) {
        fprintf(stderr, "setup: could not read '%s'\n", args->setup_import);
        return 1;
    }
    fmt = jc_convert_detect(args->setup_import, text, arena);
    if (fmt == JC_SRC_UNKNOWN) {
        fprintf(stderr, "setup: '%s' is not a Continue or opencode config\n",
                args->setup_import);
        return 1;
    }
    if (jc_convert_run(text, fmt, &res, arena) != JC_OK) {
        fprintf(stderr, "setup: failed to convert '%s'\n", args->setup_import);
        return 1;
    }
    fprintf(stderr, "setup: imported %d model(s) from %s.\n",
            res.model_count, jc_src_format_name(fmt));
    for (i = 0; i < res.warning_count; i++) {
        fprintf(stderr, "  note: %s\n", res.warnings[i]);
    }

    setup_config_path(args->setup_target, cfgpath, sizeof(cfgpath));
    if (jc_file_exists(cfgpath) && !args->force) {
        fprintf(stderr, "  = %s (exists; use --force to overwrite)\n", cfgpath);
    } else {
        scaffold_mkparent(cfgpath);
        if (jc_write_file(cfgpath, res.json, strlen(res.json)) != JC_OK) {
            fprintf(stderr, "  ! %s (write failed)\n", cfgpath);
            rc = 1;
        } else {
            fprintf(stderr, "  + %s\n", cfgpath);
        }
    }

    /* Asset tree: top-level files at the project root, nested under .jichi/. */
    for (i = 0; i < res.ir->asset_count; i++) {
        const char *rel = res.ir->assets[i]->relpath;
        char dest[1200];
        if (strchr(rel, '/') != NULL) {
            jc_snprintf(dest, sizeof(dest), "%s/.jichi/%s", base, rel);
        } else {
            jc_snprintf(dest, sizeof(dest), "%s/%s", base, rel);
        }
        if (jc_file_exists(dest) && !args->force) {
            fprintf(stderr, "  = %s (exists)\n", dest);
            continue;
        }
        scaffold_mkparent(dest);
        if (jc_write_file(dest, res.ir->assets[i]->contents,
                          strlen(res.ir->assets[i]->contents)) != JC_OK) {
            fprintf(stderr, "  ! %s (write failed)\n", dest);
            rc = 1;
        } else {
            fprintf(stderr, "  + %s\n", dest);
        }
    }

    free(res.json);
    fprintf(stderr, "\nNext: review %s, set your API key env var, then run "
                    "`jichi doctor`.\n", cfgpath);
    return rc;
}

/* Split so neither literal exceeds the C90 509-char limit. */
static const char *ADVISOR_SYS_A =
    "You are a setup advisor for the jichi coding agent. Given a "
    "project's files, machine, current models, and the models its server "
    "offers, recommend a concrete, practical configuration. Be specific and "
    "brief. Output GitHub-flavored markdown only (no preamble, no code fences "
    "around the whole reply). ";
static const char *ADVISOR_SYS_B =
    "Cover, with short bullets: (1) which available model to assign to each "
    "role (chat/edit, embed, summarize/autocomplete) and why; (2) 2-4 "
    "project-specific agents or skills worth adding (name + one-line purpose); "
    "(3) config knobs to consider (routing fast/strong, docs sources, "
    "toolProfile, contextLimit). Recommend only; do not claim to have changed "
    "anything.";

/* Append a bounded top-level file/dir listing of `dir` to `sb`. */
static void advisor_list_files(struct jc_sb *sb, const char *dir,
                               struct jc_arena *a)
{
    struct jc_vec names;
    jc_size i;
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, a) != JC_OK) {
        jc_vec_free(&names);
        return;
    }
    for (i = 0; i < names.len && i < 40; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        if (nm[0] == '.') {
            continue;
        }
        jc_sb_append(sb, "- ");
        jc_sb_append(sb, nm);
        jc_sb_append_char(sb, '\n');
    }
    jc_vec_free(&names);
}

/* `setup --advisor`: ask the configured model to recommend a project-tailored
 * setup and write a reviewable draft to .jichi/setup.advice.md. Propose-only;
 * never mutates the config. Skips gracefully with no key / unreachable model. */
static int setup_advisor(const char *cfgpath, const char *packname,
                         struct jc_arena *arena)
{
    struct jc_config cfg;
    struct jc_model_cfg *m;
    struct jc_provider *prov;
    struct jc_sb prompt;
    struct jc_vec ids;
    char *advice;
    int reachable;
    int advisor_timeout = 0;
    jc_size i;

    if (jc_config_load(cfgpath, 0, &cfg, arena) != JC_OK) {
        fprintf(stderr, "advisor: could not load %s\n", cfgpath);
        return 1;
    }
    m = &cfg.model;
    if (m->api_key == NULL || m->api_key[0] == '\0') {
        fprintf(stderr, "advisor: the active model has no API key; set its "
                        "apiKeyEnv var, then re-run `setup --advisor`.\n");
        jc_config_free(&cfg);
        return 0;
    }

    jc_http_global_init();
    reachable = jc_net_reachable(m->api_base, m->api_key, 4, NULL);
    if (!reachable) {
        fprintf(stderr, "advisor: model server unreachable (%s); skipping.\n",
                m->api_base != NULL ? m->api_base : "?");
        jc_http_global_cleanup();
        jc_config_free(&cfg);
        return 0;
    }
    jc_vec_init(&ids, sizeof(char *));
    jc_net_list_models(m->api_base, m->api_key, 6, NULL, &ids, arena);

    jc_sb_init(&prompt);
    jc_sb_append_fmt(&prompt, "## Project\nScaffold pack: %s\n\nTop-level "
                             "files:\n", packname != NULL ? packname : "none");
    advisor_list_files(&prompt, ".", arena);
    {
        unsigned long mb = jc_mem_total_mb();
        jc_sb_append_fmt(&prompt, "\n## Machine\n%d core(s)", jc_cpu_count());
        if (mb > 0) {
            jc_sb_append_fmt(&prompt, ", %lu MB RAM", mb);
        }
        jc_sb_append_char(&prompt, '\n');
    }
    jc_sb_append(&prompt, "\n## Currently configured models\n");
    for (i = 0; i < (jc_size)jc_config_model_count(&cfg); i++) {
        struct jc_model_cfg *mm = jc_config_model_at(&cfg, (int)i);
        jc_sb_append_fmt(&prompt, "- %s (provider %s)\n",
                         mm->model != NULL ? mm->model : "?",
                         mm->provider != NULL ? mm->provider : "?");
    }
    if (ids.len > 0) {
        jc_sb_append(&prompt, "\n## Models this server offers\n");
        for (i = 0; i < ids.len && i < 60; i++) {
            jc_sb_append_fmt(&prompt, "- %s\n", *(char **)jc_vec_at(&ids, i));
        }
    }
    jc_sb_append(&prompt, "\nRecommend a configuration per your instructions.");

    fprintf(stderr, "advisor: asking %s for recommendations...\n",
            m->model != NULL ? m->model : "the model");
    prov = jc_provider_create(m);
    {
        char sysbuf[900];
        jc_snprintf(sysbuf, sizeof(sysbuf), "%s%s", ADVISOR_SYS_A,
                    ADVISOR_SYS_B);
        advice = (prov != NULL)
            ? jc_oneshot_ex(prov, sysbuf,
                            prompt.data != NULL ? prompt.data : "", 240, NULL,
                            &advisor_timeout)
            : NULL;
    }
    if (prov != NULL) {
        prov->vt->free(prov);
    }
    jc_sb_free(&prompt);
    jc_vec_free(&ids);
    jc_http_global_cleanup();

    if (advice == NULL || advice[0] == '\0') {
        if (advisor_timeout) {
            fprintf(stderr, "advisor: the model timed out (240s). If it was "
                    "cold-loading, give it a moment and re-run `setup "
                    "--advisor`.\n");
        } else {
            fprintf(stderr, "advisor: no recommendation returned (the model "
                    "replied empty).\n");
        }
        free(advice);
        jc_config_free(&cfg);
        return 1;
    }
    scaffold_mkparent(".jichi/setup.advice.md");
    if (jc_write_file(".jichi/setup.advice.md", advice, strlen(advice))
        != JC_OK) {
        fprintf(stderr, "advisor: could not write .jichi/setup.advice.md\n");
        free(advice);
        jc_config_free(&cfg);
        return 1;
    }
    free(advice);
    jc_config_free(&cfg);
    fprintf(stderr, "advisor: wrote .jichi/setup.advice.md (review, then apply "
                    "what you like).\n");
    return 0;
}

static const char *const ONBOARD_ANALYST_SYS =
    "You analyze an unfamiliar codebase from the summary provided and report "
    "findings. Recommend only; never claim to have changed anything. Produce a "
    "structured markdown report: 1. Stack (languages/frameworks/build system, "
    "naming the proof files); 2. Build & test (the exact commands); 3. Layout "
    "(top-level dirs); 4. Entry points; 5. Conventions; 6. Suggested jichi config "
    "(testCommand, an embed model, docs sources, lspServers). Be concrete.";
static const char *const ONBOARD_TUTORIAL_SYS =
    "You write a getting-started tutorial (markdown) for a project from the "
    "analysis provided, aimed at a new contributor: what it is, prerequisites, "
    "build, run, test, layout at a glance, and a first-change walkthrough. "
    "Prefer copy-pasteable commands grounded in the analysis. Output only the "
    "tutorial body.";

/* `setup --onboard`: scaffold the onboarding pack, then (propose-only) run the
 * project-analyst and tutorial-writer as one-shot model calls, writing
 * .jichi/onboarding.analysis.md and docs/TUTORIAL.draft.md for review. Everything
 * is a draft; nothing is committed. Scaffolding still happens with no key /
 * unreachable model, so `/onboard` stays available for an interactive session. */
static int setup_onboard(const char *cfgpath, struct jc_arena *arena)
{
    const struct jc_scaffold_pack *pack;
    struct jc_config cfg;
    struct jc_model_cfg *m;
    struct jc_provider *prov;
    struct jc_sb prompt;
    char *analysis = NULL;
    char *tutorial = NULL;
    int wrote = 0, skipped = 0, failed = 0;
    int reachable;
    int t = 0;

    printf("Onboarding: scaffolding the onboarding pack...\n");
    pack = jc_scaffold_find_pack("onboarding");
    if (pack != NULL) {
        scaffold_write_pack(pack, 0, 0, 0, &wrote, &skipped, &failed);
    }

    if (!jc_file_exists(cfgpath) ||
        jc_config_load(cfgpath, 0, &cfg, arena) != JC_OK) {
        fprintf(stderr, "onboard: no config at %s; scaffolded the pack only. "
                "Configure a model (see `setup --from-global`), then run "
                "`/onboard` in a session.\n", cfgpath);
        return 0;
    }
    m = &cfg.model;
    if (m->api_key == NULL || m->api_key[0] == '\0') {
        fprintf(stderr, "onboard: the active model has no API key; scaffolded "
                "the pack only. Set its apiKeyEnv var and run `/onboard`.\n");
        jc_config_free(&cfg);
        return 0;
    }

    jc_http_global_init();
    reachable = jc_net_reachable(m->api_base, m->api_key, 4, NULL);
    if (!reachable) {
        fprintf(stderr, "onboard: model server unreachable; scaffolded the pack "
                "only. Run `/onboard` when the model is up.\n");
        jc_http_global_cleanup();
        jc_config_free(&cfg);
        return 0;
    }

    jc_sb_init(&prompt);
    jc_sb_append(&prompt, "## Project\nTop-level files:\n");
    advisor_list_files(&prompt, ".", arena);
    jc_sb_append(&prompt, "\nAnalyze this project per your instructions.");
    fprintf(stderr, "onboard: analyzing with %s...\n",
            m->model != NULL ? m->model : "the model");
    prov = jc_provider_create(m);
    analysis = (prov != NULL)
        ? jc_oneshot_ex(prov, ONBOARD_ANALYST_SYS,
                        prompt.data != NULL ? prompt.data : "", 240, NULL, &t)
        : NULL;
    jc_sb_free(&prompt);

    if (analysis != NULL && analysis[0] != '\0') {
        scaffold_mkparent(".jichi/onboarding.analysis.md");
        if (jc_write_file(".jichi/onboarding.analysis.md", analysis,
                          strlen(analysis)) == JC_OK) {
            fprintf(stderr, "onboard: wrote .jichi/onboarding.analysis.md\n");
        }
        /* Draft the tutorial from the analysis (second one-shot). */
        fprintf(stderr, "onboard: drafting the tutorial...\n");
        tutorial = (prov != NULL)
            ? jc_oneshot_ex(prov, ONBOARD_TUTORIAL_SYS, analysis, 240, NULL, &t)
            : NULL;
        if (tutorial != NULL && tutorial[0] != '\0') {
            scaffold_mkparent("docs/TUTORIAL.draft.md");
            if (jc_write_file("docs/TUTORIAL.draft.md", tutorial,
                              strlen(tutorial)) == JC_OK) {
                fprintf(stderr, "onboard: wrote docs/TUTORIAL.draft.md "
                        "(review, then rename to docs/TUTORIAL.md)\n");
            }
        }
    } else {
        fprintf(stderr, "onboard: the model returned no analysis%s.\n",
                t ? " (timed out)" : "");
    }
    if (prov != NULL) {
        prov->vt->free(prov);
    }
    free(analysis);
    free(tutorial);
    jc_http_global_cleanup();
    jc_config_free(&cfg);
    fprintf(stderr, "onboard: done (propose-only; review the drafts).\n");
    return 0;
}

static int run_setup(struct cli_args *args, struct jc_arena *arena)
{
    const struct jc_setup_preset *preset;
    /* M326j: the optional second dimension. NULL => no journey, which is both
     * the default and exactly pre-M326j behaviour. */
    const struct jc_setup_preset *journey = NULL;
    const struct jc_setup_preset *machine = NULL;
    const struct jc_setup_preset *stance = NULL;
    const struct jc_scaffold_pack *pack;
    const char *packname;
    struct jc_setup_answers ans;
    char cfgpath[1100];
    int interactive;

    if (args->setup_import != NULL) {
        return setup_import(args, arena);
    }

    /* `setup --advisor` on its own (no provider/model given): run the advisor
     * against the already-configured project, don't re-scaffold. An explicit
     * --config wins; otherwise the --config-target default (local/global). */
    if (args->setup_advisor && args->setup_provider == NULL &&
        args->model_override == NULL) {
        char cp[1100];
        if (args->config_path != NULL && args->config_path[0] != '\0') {
            jc_snprintf(cp, sizeof(cp), "%s", args->config_path);
        } else {
            setup_config_path(args->setup_target, cp, sizeof(cp));
        }
        return setup_advisor(cp, args->setup_lang, arena);
    }

    /* `setup --onboard`: scaffold the onboarding pack + run the propose-only
     * project analysis/tutorial drafts against the configured model. */
    if (args->setup_onboard) {
        char cp[1100];
        if (args->config_path != NULL && args->config_path[0] != '\0') {
            jc_snprintf(cp, sizeof(cp), "%s", args->config_path);
        } else {
            setup_config_path(args->setup_target, cp, sizeof(cp));
        }
        return setup_onboard(cp, arena);
    }

    if (args->list) {
        int n = jc_setup_preset_count();
        int k;
        printf("Available setup presets:\n");
        for (k = 0; k < n; k++) {
            const struct jc_setup_preset *p = jc_setup_preset_at(k);
            printf("  %-16s %s\n", p->name, p->description);
        }
        return 0;
    }

    interactive = !args->non_interactive && !args->setup_from_global &&
                  (args->setup_provider == NULL ||
                   args->model_override == NULL) &&
                  isatty(STDIN_FILENO);

    if (interactive) {
        struct jc_term t;
        int rc;
        jc_term_init(&t);
        rc = setup_interactive(&t, arena, args->accessible,
                               &preset, &journey, &machine,
                               &stance, &pack, &ans,
                               cfgpath, sizeof(cfgpath));
        jc_term_free(&t);
        if (rc != 0) {
            fprintf(stderr, "\nsetup: cancelled.\n");
            return 1;
        }
    } else {
        preset = jc_setup_find_preset(args->setup_preset != NULL
                                      ? args->setup_preset : "developer");
        if (args->setup_preset != NULL && preset == NULL) {
            fprintf(stderr, "setup: unknown preset '%s' (try `setup --list`)\n",
                    args->setup_preset);
            return 2;
        }
        if (!args->setup_from_global &&
            (args->setup_provider == NULL || args->model_override == NULL)) {
            fprintf(stderr,
                "setup: not a TTY -- pass flags, e.g.\n"
                "  jichi setup --preset developer --provider openai \\\n"
                "    --model gpt-4o --key-env OPENAI_API_KEY\n"
                "for a local or self-hosted server, add the endpoint:\n"
                "  jichi setup --preset developer --provider openai \\\n"
                "    --api-base http://localhost:1234/v1 --model my-model\n"
                "or inherit the global config:\n"
                "  jichi setup --from-global --preset developer\n"
                "(`setup --list` shows the presets.)\n");
            return 2;
        }
        packname = preset->scaffold_pack;
        if (preset->asks_language && args->setup_lang != NULL) {
            if (jc_scaffold_find_pack(args->setup_lang) == NULL) {
                fprintf(stderr,
                    "setup: unknown --lang pack '%s' (try `init --list`)\n",
                    args->setup_lang);
                return 2;
            }
            packname = args->setup_lang;
        } else if (preset->asks_language) {
            /* No --lang given: auto-detect from the project's files (M52). */
            const char *det = setup_detect_lang(".", arena);
            if (det != NULL) {
                packname = det;
                fprintf(stderr, "setup: detected language pack '%s'\n", det);
            }
        }
        pack = jc_scaffold_find_pack(packname);
        jc_setup_answers_init(&ans);
        ans.provider = args->setup_provider;
        ans.model = args->model_override;
        ans.model_name = "chat";
        ans.api_base = args->setup_api_base;
        /* --context-length <tokens|auto>.
         *
         * setup is OFFLINE by design -- setup_validate says so in as many words,
         * and setup's own closing line points at `doctor` for "full validation
         * (+ network)". So `auto` is an EXPLICIT opt-in to exactly one request,
         * the same shape --advisor already established, and a literal NUMBER
         * stays offline and deterministic: that is the mode for an agent driving
         * another jichi, which usually already knows the window and wants a
         * reproducible run with no egress.
         *
         * The two failure kinds are kept apart on purpose. Missing
         * PREREQUISITES (no endpoint, no exported key) is a usage error and
         * refuses, because the request cannot even be attempted and silently
         * doing nothing would answer an explicit flag with nothing. A server
         * that simply does not PUBLISH a window is not the caller's mistake --
         * most do not -- so that is reported and the run continues, since every
         * other thing setup was asked to do still succeeded.
         */
        if (args->setup_context_length != NULL) {
            const char *cl = args->setup_context_length;
            if (strcmp(cl, "auto") == 0) {
#ifdef JC_HAVE_CURL
                const char *kname = args->setup_key_env;
                const char *key = (kname != NULL) ? getenv(kname) : NULL;
                if (ans.api_base == NULL || ans.api_base[0] == '\0') {
                    fprintf(stderr, "setup: --context-length auto needs "
                            "--api-base; there is no endpoint to ask.\n");
                    return 2;
                }
                if (key == NULL || key[0] == '\0') {
                    fprintf(stderr, "setup: --context-length auto needs the key "
                            "exported -- --key-env names %s, which is not set "
                            "in this environment.\n",
                            kname != NULL ? kname : "(no variable)");
                    return 2;
                }
                {
                    long lim = 0;
                    long outmax = 0;
                    long http = 0;
                    jc_status ps;
                    jc_http_global_init();
                    ps = jc_net_model_limits(ans.api_base, key, ans.model, 8,
                                             NULL, &lim, &outmax, &http);
                    jc_http_global_cleanup();
                    if (ps == JC_OK && lim > 0) {
                        ans.context_length = lim;
                        printf("setup: context window %ld tokens, as published "
                               "by the server", lim);
                        if (outmax > 0) {
                            printf(" (its output limit, %ld, is separate and is "
                                   "NOT part of this budget)", outmax);
                        }
                        printf("\n");
                    } else {
                        /* The same classification doctor uses: a 404, or a 200
                         * carrying something that is not a model table (LM
                         * Studio does exactly that), both mean THIS SERVER HAS
                         * NO SUCH ENDPOINT. That is not the caller's mistake and
                         * must not read like an internal error. */
                        const char *why =
                            (ps == JC_ERR_PARSE ||
                             (ps == JC_ERR_HTTP && http == 404))
                            ? "this server does not publish model limits"
                            : jc_status_str(ps);
                        fprintf(stderr, "setup: --context-length auto: %s "
                                "(HTTP %ld). contextLength is left unset, so "
                                "jichi will assume %d tokens -- pass an "
                                "explicit number if you know better.\n",
                                why, http, JC_COMPACT_DEFAULT_LIMIT);
                    }
                }
#else
                fprintf(stderr, "setup: --context-length auto needs libcurl and "
                        "this build has none. Pass an explicit token count.\n");
                return 2;
#endif
            } else {
                char *endp = NULL;
                long n = strtol(cl, &endp, 10);
                if (n <= 0 || endp == NULL || *endp != '\0') {
                    fprintf(stderr, "setup: --context-length wants a positive "
                            "token count or `auto` (got \"%s\")\n", cl);
                    return 2;
                }
                ans.context_length = n;
            }
        }
        /* M326e: the same refusal on the non-interactive path -- which is the
         * one a script (or a copy-pasted command line) uses, so it cannot be
         * left to the prompt's re-ask loop. Absent is fine; unexportable is
         * not. */
        if (args->setup_key_env != NULL && args->setup_key_env[0] != '\0' &&
            !jc_envvar_name_valid(args->setup_key_env)) {
            setup_key_env_explain(stderr, "--key-env");
            return 2;
        }
        ans.api_key_env = args->setup_key_env;
        /* M488: the interactive wizard prompts for this and model_obj() has
         * always written it, but the non-TTY form had no flag -- so following
         * setup's own printed guidance produced a config pointing at the
         * provider's cloud. It bit exactly the presets that exist for locally
         * hosted models (`small-local`, `constrained`), which by definition
         * need a custom endpoint. */
        jc_setup_apply_preset(&ans, preset);
        /* M326j: the journey layers on top, same order as the wizard. */
        if (args->setup_journey != NULL && args->setup_journey[0] != '\0') {
            journey = jc_setup_find_preset(args->setup_journey);
            if (journey == NULL || journey->axis != JC_AXIS_JOURNEY) {
                fprintf(stderr, "setup: unknown journey '%s'. Journeys: ",
                        args->setup_journey);
                {
                    int k, n = jc_setup_preset_count(), shown = 0;
                    for (k = 0; k < n; k++) {
                        const struct jc_setup_preset *p =
                            jc_setup_preset_at(k);
                        if (p->axis == JC_AXIS_JOURNEY) {
                            fprintf(stderr, "%s%s", shown++ ? ", " : "",
                                    p->name);
                        }
                    }
                }
                fprintf(stderr, "\n");
                return 2;
            }
            jc_setup_apply_preset(&ans, journey);
        }
        if (args->setup_machine != NULL && args->setup_machine[0] != '\0') {
            machine = jc_setup_find_preset(args->setup_machine);
            if (machine == NULL || machine->axis != JC_AXIS_MACHINE) {
                fprintf(stderr, "setup: unknown machine profile '%s'. "
                        "Machines: ", args->setup_machine);
                {
                    int k, n = jc_setup_preset_count(), shown = 0;
                    for (k = 0; k < n; k++) {
                        const struct jc_setup_preset *p =
                            jc_setup_preset_at(k);
                        if (p->axis == JC_AXIS_MACHINE) {
                            fprintf(stderr, "%s%s", shown++ ? ", " : "",
                                    p->name);
                        }
                    }
                }
                fprintf(stderr, "\n");
                return 2;
            }
            jc_setup_apply_preset(&ans, machine);
        }
        if (args->setup_stance != NULL && args->setup_stance[0] != '\0') {
            stance = jc_setup_find_preset(args->setup_stance);
            if (stance == NULL || stance->axis != JC_AXIS_STANCE) {
                fprintf(stderr, "setup: unknown stance '%s'. Stances: ",
                        args->setup_stance);
                {
                    int k, n = jc_setup_preset_count(), shown = 0;
                    for (k = 0; k < n; k++) {
                        const struct jc_setup_preset *p =
                            jc_setup_preset_at(k);
                        if (p->axis == JC_AXIS_STANCE) {
                            fprintf(stderr, "%s%s", shown++ ? ", " : "",
                                    p->name);
                        }
                    }
                }
                fprintf(stderr, "\n");
                return 2;
            }
            jc_setup_apply_preset(&ans, stance);
        }
        /* M183: the rewrite journey's old tree, from the existing repeatable
         * --reference-root flag (first entry; the config array is extendable
         * by hand for multi-tree ports). */
        if (args->n_reference_root > 0) {
            ans.reference_root = args->reference_root[0];
        }
        setup_config_path(args->setup_target, cfgpath, sizeof(cfgpath));
    }

    /* M116: a beginner/advanced complexity level layered over the role preset. */
    if (args->setup_profile != NULL) {
        int cx;
        if (jc_setup_complexity_parse(args->setup_profile, &cx)) {
            jc_setup_apply_complexity(&ans, cx);
        } else {
            fprintf(stderr,
                    "unknown --profile '%s' (beginner|advanced|standard)\n",
                    args->setup_profile);
        }
    }

    if (interactive) {
        struct jc_term mt;
        jc_term_init(&mt);
        setup_apply_machine(&ans, &mt, 1);
        jc_term_free(&mt);
    } else {
        setup_apply_machine(&ans, NULL, 0);
    }
    {
        int rc = setup_emit(preset, journey, machine, stance, pack, &ans,
                            cfgpath, args->force,
                            args->setup_from_global, args->setup_inherit,
                            arena);
        if (rc == 0 && args->setup_advisor) {
            setup_advisor(cfgpath, pack != NULL ? pack->name : NULL, arena);
        } else if (rc == 0) {
            /* M326f: this trailed AFTER "Done. Next steps:", so a completed run
             * ended by suggesting the user run setup again -- and its "set your
             * API key" was the third nag in a row at someone who had just
             * stored one. It is a next step, so it goes in that list, and it
             * only mentions the key when the key is actually missing. */
            if (ans.key_state == JC_SETUP_KEY_NONE) {
                printf("  jichi setup --advisor      # once the key is set: "
                       "AI-tailored recommendations\n");
            } else {
                printf("  jichi setup --advisor      # AI-tailored "
                       "recommendations for this project\n");
            }
        }
        return rc;
    }
}

/* `status` -> print the resolved session configuration (no provider/network). */
static int run_status(struct jc_app *app, int json)
{
    struct jc_routing_cfg *r = &app->config.routing;
    struct jc_model_cfg *m = &app->config.model;

    if (json) {
        cJSON *o = cJSON_CreateObject();
        char *text;
        cJSON_AddStringToObject(o, "name", m->name != NULL ? m->name : "");
        cJSON_AddStringToObject(o, "model", m->model != NULL ? m->model : "");
        cJSON_AddStringToObject(o, "provider",
                                m->provider != NULL ? m->provider : "");
        cJSON_AddStringToObject(o, "apiBase",
                                m->api_base != NULL ? m->api_base : "");
        cJSON_AddStringToObject(o, "mode",
            jc_agent_mode_name((enum jc_agent_mode)app->mode));
        cJSON_AddBoolToObject(o, "snapshots", app->config.snapshots);
        cJSON_AddBoolToObject(o, "repoMap", app->config.repo_map);
        cJSON_AddBoolToObject(o, "fromDefaults", app->config.from_defaults);
        cJSON_AddStringToObject(o, "cwd", app->cwd);
        if (m->context_limit > 0) {
            cJSON_AddNumberToObject(o, "contextLength",
                                    (double)m->context_limit);
        }
        if (r->enabled && r->fast != NULL && r->strong != NULL) {
            cJSON *rt = cJSON_AddObjectToObject(o, "routing");
            cJSON_AddStringToObject(rt, "fast", r->fast);
            cJSON_AddStringToObject(rt, "strong", r->strong);
        }
        {
            cJSON *mc = cJSON_AddObjectToObject(o, "machine");
            unsigned long mb = jc_mem_total_mb();
            cJSON_AddNumberToObject(mc, "cores", (double)jc_cpu_count());
            if (mb > 0) {
                cJSON_AddNumberToObject(mc, "ram_mb", (double)mb);
            }
        }
        text = jc_json_print(o);
        if (text != NULL) {
            printf("%s\n", text);
            free(text);
        }
        cJSON_Delete(o);
        return 0;
    }

    /* M296: the `status` subcommand's twin of the TUI's /status line. It built the
     * pair by hand too, so a config with no `name` printed "(null) (jlu/...)" --
     * NULL to "%s" is undefined behaviour in C89, and this is the FIRST thing the
     * subcommand prints. Two surfaces, one hand-built format, the same defect:
     * exactly why it is now one function. */
    {
        char mdisp[192];
        jc_model_display(m->name, m->model, mdisp, sizeof mdisp);
        printf("model:       %s\n", mdisp);
    }
    if (r->enabled && r->fast != NULL && r->strong != NULL) {
        printf("routing:     fast=%s  strong=%s\n", r->fast, r->strong);
    }
    printf("mode:        %s\n",
           jc_agent_mode_name((enum jc_agent_mode)app->mode));
    printf("snapshots:   %s\n", app->config.snapshots ? "on" : "off");
    printf("repo-map:    %s\n", app->config.repo_map ? "on" : "off");
    printf("cwd:         %s\n", app->cwd);
    return 0;
}

/* Add {"type":t,"fields":[...]} to the jsonl-events array. `fields` is a
 * space-separated list split into a JSON string array. */
/* M301: `fields` is split on spaces into an array, so ANY prose passed here
 * becomes fake field names -- the heartbeat entry shipped
 * ["v","type","elapsed","(only","with","--heartbeat", ...] to every consumer
 * reading the machine contract. Prose belongs in `note`, which may be NULL. */
static void describe_event(cJSON *arr, const char *t, const char *fields,
                           const char *note)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *f = cJSON_CreateArray();
    const char *p = fields;
    cJSON_AddStringToObject(o, "type", t);
    while (p != NULL && *p != '\0') {
        const char *sp = strchr(p, ' ');
        char word[48];
        jc_size n = (sp != NULL) ? (jc_size)(sp - p) : strlen(p);
        if (n >= sizeof(word)) {
            n = sizeof(word) - 1;
        }
        memcpy(word, p, n);
        word[n] = '\0';
        if (word[0] != '\0') {
            cJSON_AddItemToArray(f, cJSON_CreateString(word));
        }
        p = (sp != NULL) ? sp + 1 : NULL;
    }
    cJSON_AddItemToObject(o, "fields", f);
    if (note != NULL && note[0] != '\0') {
        cJSON_AddStringToObject(o, "note", note);
    }
    cJSON_AddItemToArray(arr, o);
}

/* Add {"name":n,"summary":s} (optionally "readonly") to an array. */
static void describe_named(cJSON *arr, const char *n, const char *s,
                           int has_ro, int ro)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", n);
    if (has_ro) {
        cJSON_AddBoolToObject(o, "readonly", ro);
    }
    if (s != NULL) {
        cJSON_AddStringToObject(o, "summary", s);
    }
    cJSON_AddItemToArray(arr, o);
}

/* M538: one conditional_tools row per TOOL, with the condition in its own field.
 *
 * The rows used to collapse whole families into a slash-joined `name` --
 * "git_status/git_diff/git_log/git_blame/git_add/git_commit/git_branch/git_stash"
 * -- while `summary` held the CONDITION ("in a git repository") rather than a
 * summary. Two fields misused at once, and the effect is that the table cannot
 * answer the only question it exists for: is `rename_symbol` available to me, and
 * under what condition? A consumer had to know to split on '/' and to read
 * `summary` as a predicate.
 *
 * `pattern` marks a row whose name is a naming CONVENTION rather than a tool --
 * `<server>__<tool>` is not a name and never will be, so it says so instead of
 * being silently indistinguishable from one. */
static void describe_cond(cJSON *arr, const char *name, const char *when,
                          int is_pattern)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddStringToObject(o, "when", when);
    if (is_pattern) {
        cJSON_AddBoolToObject(o, "pattern", 1);
    }
    cJSON_AddItemToArray(arr, o);
}

/* M538: one key_flags row whose `name` is a NAME.
 *
 * The rows used to carry a human synopsis in the `name` field -- "-p, --print
 * bracket, and --session carrying its aliases inside the same string -- so a
 * consumer parsing
 * .key_flags[].name got a string that is not a flag, cannot be passed to the
 * binary, and cannot be split on any single separator. describe is advertised as
 * "the stable contract a driving agent needs without reading the source"; a name
 * field holding prose is the one thing such an agent cannot use.
 *
 * The alias and argument information is not lost, it is SEPARATED: `name` is the
 * canonical long flag, `aliases` the alternates, `arg` the placeholder. Additive,
 * so a consumer that only rendered `name` still renders something -- now a flag
 * instead of a sentence. tests/smoke/describe_names_lint.sh checks every name
 * against the real parser. */
static void describe_flag(cJSON *arr, const char *name, const char *arg,
                          int arg_optional, const char *aliases,
                          const char *summary)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", name);
    if (arg != NULL) {
        /* M538: the argument's NAME, without the <> or [] a synopsis would wrap
         * it in -- those are human notation, and this milestone is about not
         * putting human notation in machine fields. Optionality is a boolean, not
         * a bracket. The first cut wrapped the placeholder in square brackets and
         * notice_tags_lint promptly read it as an emitted notice tag needing a
         * NOTICES.md row -- its pattern is a quote followed directly by a
         * bracketed word, which is exactly the shape a synopsis has. It was right
         * for the right reason, and the lesson had arrived one level deeper than I
         * applied it. */
        cJSON_AddStringToObject(o, "arg", arg);
        if (arg_optional) {
            cJSON_AddBoolToObject(o, "arg_optional", 1);
        }
    }
    if (aliases != NULL) {
        cJSON *a = cJSON_CreateArray();
        const char *p = aliases;
        while (*p != '\0') {
            const char *e = p;
            char one[32];
            jc_size n;
            while (*e != '\0' && *e != ' ') {
                e++;
            }
            n = (jc_size)(e - p);
            if (n > 0 && n < sizeof(one)) {
                memcpy(one, p, (size_t)n);
                one[n] = '\0';
                cJSON_AddItemToArray(a, cJSON_CreateString(one));
            }
            p = (*e == '\0') ? e : e + 1;
        }
        cJSON_AddItemToObject(o, "aliases", a);
    }
    if (summary != NULL) {
        cJSON_AddStringToObject(o, "summary", summary);
    }
    cJSON_AddItemToArray(arr, o);
}

/* `describe [--output json]` -> jichi's machine-readable interface contract:
 * output formats, exit codes, the jsonl event schema + stop reasons, the daemon
 * socket protocol, key driving flags, subcommands, and the built-in tool set.
 * The stable contract a driving agent needs without reading the source. Static
 * by design (these are the contract); the tool list is built from the real
 * registry. No provider/network. */
static int run_describe(int json)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr;
    cJSON *sub;
    struct jc_tool_registry reg;
    jc_size i;
    char *text;

    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "product", "jichi");
    cJSON_AddStringToObject(root, "version", JC_VERSION);
    /* M495: the commit, so a supervisor can tell two binaries with the same
     * `version` apart. An ADDED field, which M301's contract permits ("new fields
     * may appear; ignore unknown ones"); omitted entirely when unknown, so its
     * presence is the signal. */
    if (jc_build_rev() != NULL) {
        cJSON_AddStringToObject(root, "build", jc_build_rev());
    }
    /* M497: added fields, compatible under M301's contract ("new fields may
     * appear"). A supervisor embedding jichi has a legitimate need to read the
     * licence programmatically; while the decision was open the value was the
     * truthful placeholder `LicenseRef-UNDECIDED`, and since M619 it names the
     * decided identifier. The author field (M619) rides the same contract. */
    cJSON_AddStringToObject(root, "copyright", JC_COPYRIGHT);
    cJSON_AddStringToObject(root, "author", JC_AUTHOR); /* M619 */
    cJSON_AddStringToObject(root, "license", JC_LICENSE_SPDX);
    cJSON_AddStringToObject(root, "summary",
        "C89 reimplementation of the Continue CLI coding agent. Drive it "
        "headless with -p + --output jsonl, over a warm Unix socket "
        "(daemon), or as an ACP server (serve).");

    arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("text"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("json"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("jsonl"));
    cJSON_AddItemToObject(root, "output_formats", arr);

    arr = cJSON_CreateArray();
    describe_named(arr, "0", "success", 0, 0);
    describe_named(arr, "1", "runtime error (see stderr / done.error)", 0, 0);
    describe_named(arr, "2", "usage or config error", 0, 0);
    describe_named(arr, "130", "interrupted (SIGINT)", 0, 0);
    /* M301: 143 was in the TEXT rendering and missing from the JSON -- and it is
     * the code M146 added so a supervisor could tell a graceful SIGTERM from a
     * crash. The machine contract omitted the entry that exists for machines. */
    describe_named(arr, "143", "terminated (SIGTERM, graceful)", 0, 0);
    cJSON_AddItemToObject(root, "exit_codes", arr);

    arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("chat"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("plan"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("auto"));
    cJSON_AddItemToObject(root, "modes", arr);

    /* jsonl streaming contract (--output jsonl). */
    sub = cJSON_CreateObject();
    cJSON_AddStringToObject(sub, "note",
        "One JSON object per line; every object carries \"v\" (schema "
        "version) and \"type\". Built by jc_agentjson_event/_result.");
    arr = cJSON_CreateArray();
    /* M431: these lists are checked against the emitters by
     * tests/smoke/describe_fields_lint.sh. They were hand-maintained and nothing
     * compared them, so four drifts had accumulated in a surface this file itself
     * calls the contract and docs/EMBEDDING.md tells consumers to diff in CI:
     * `text` named a field the wire calls `delta`, `usage` named `cache` where the
     * wire carries `cost`, the whole `status` event was missing, and `done` omitted
     * model, tool_calls and every M97 econ field. Add a field here whenever you
     * add one to an emitter -- the lint will tell you if you forget. */
    describe_event(arr, "run_start", "v type run",
        "once, first, only when an envelope is armed; `run` is the id the "
        "journal and telemetry stamp, so a supervisor can tail this run's "
        "audit trail while it is still live");
    describe_event(arr, "message_start", "v type model mode", NULL);
    describe_event(arr, "text", "v type delta",
        "delta is ONE streamed chunk; concatenate them");
    describe_event(arr, "tool_call", "v type name args id",
        "M442: `id` is the provider's tool_call id, present when the provider "
        "sent one -- pair a tool_result to its tool_call by it, not by order, "
        "which a round with two calls to the same tool does not preserve");
    describe_event(arr, "tool_result", "v type name is_error preview id",
        "preview is capped at 512 bytes and cut on a UTF-8 boundary (M439), so "
        "it may be a few bytes short of the cap; `id` matches the tool_call");
    describe_event(arr, "usage", "v type input output cost", NULL);
    describe_event(arr, "status", "v type line",
        "line is free-form prose for humans: log it, do not match on it");
    describe_event(arr, "heartbeat", "v type elapsed rss_kb",
        "only with --heartbeat <secs>; liveness during a model call; "
        "rss_kb is omitted where the process RSS cannot be read");
    describe_event(arr, "done",
        "v type text model stop_reason error session_id run cost tokens "
        "tool_calls aborted work_kept starved budget_kind budget "
        "peak_input cache tools degraded",
        "the terminal event, also the whole --output json object; "
        "session_id, run, error, budget, budget_kind and degraded are "
        "conditional; `run` matches the run_start event and the "
        "journal/telemetry key. `degraded` (M443) is PRESENT ONLY when this run "
        "made a decision in the operator's absence -- test for the key, not a "
        "boolean; its counts are unanswered / approval_unavailable / "
        "privilege_refused. A tool auto-approved under --auto is NOT degraded: "
        "that is the operator's instruction, not a decision taken for them");
    cJSON_AddItemToObject(sub, "events", arr);
    arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("done"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("interrupted"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("timeout"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("budget"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("verify_failed"));
    /* M431: emitted since M332 and never declared here. */
    cJSON_AddItemToArray(arr, cJSON_CreateString("scope_tainted"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("max_iters"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("error"));
    cJSON_AddItemToObject(sub, "stop_reasons", arr);
    cJSON_AddItemToObject(root, "jsonl", sub);

    /* Warm-process daemon socket protocol. */
    sub = cJSON_CreateObject();
    cJSON_AddStringToObject(sub, "transport",
        "AF_UNIX SOCK_STREAM; newline-framed JSON; one request per "
        "connection; the turn's output streams back on the same socket.");
    cJSON_AddStringToObject(sub, "socket",
        "--socket <path>, else $JICHI_DAEMON_SOCK, else "
        "~/.jichi.d/daemon.sock");
    arr = cJSON_CreateArray();
    {
        cJSON *r = cJSON_CreateObject();
        cJSON *fl = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "type", "prompt");
        cJSON_AddStringToObject(fl, "prompt", "string (required)");
        cJSON_AddStringToObject(fl, "cwd", "string (workspace for the turn)");
        cJSON_AddStringToObject(fl, "mode", "chat|plan|auto (optional)");
        cJSON_AddStringToObject(fl, "format",
                                "text|json|jsonl (default text; json added M431g)");
        cJSON_AddItemToObject(r, "fields", fl);
        cJSON_AddStringToObject(r, "response",
            "the turn output: plain text, one event per line when format=jsonl, "
            "or a single terminal object when format=json. An unknown format is "
            "served as text rather than refused.");
        cJSON_AddItemToArray(arr, r);
    }
    describe_named(arr, "ping", "liveness check; replies {\"type\":\"pong\"}",
                   0, 0);
    describe_named(arr, "shutdown", "ask the daemon to exit; replies "
                   "{\"type\":\"bye\"}", 0, 0);
    cJSON_AddItemToObject(sub, "requests", arr);
    cJSON_AddItemToObject(root, "daemon", sub);

    /* The driving-relevant flags (run --help for the exhaustive list). */
    arr = cJSON_CreateArray();
    describe_flag(arr, "--print", "text", 1, "-p",
        "headless: run one turn and print the answer (stdin if text is '-' "
        "or omitted on a pipe)");
    describe_flag(arr, "--output", "text|json|jsonl", 0, NULL, "stdout format");
    describe_flag(arr, "--heartbeat", "secs", 0, NULL,
        "jsonl: periodic liveness event during a model call (0=off)");
    describe_flag(arr, "--auto", NULL, 0, NULL,
        "autonomous mode: auto-approve tools within the envelope");
    describe_flag(arr, "--quiet", NULL, 0, "-q", "silence stderr diagnostics");
    describe_flag(arr, "--config", "path", 0, NULL, "config file");
    describe_flag(arr, "--session", "id|prefix", 0, "-c --continue --resume",
        "resume a session");
    describe_flag(arr, "--no-session", NULL, 0, NULL,
        "do not persist the session");
    describe_flag(arr, "--connect", "socket", 0, NULL,
        "send the prompt to a running daemon and relay its output");
    cJSON_AddItemToObject(root, "key_flags", arr);

    /* Subcommands (name + one line). */
    arr = cJSON_CreateArray();
    describe_named(arr, "daemon", "warm process serving turns over a socket",
                   0, 0);
    describe_named(arr, "serve", "ACP agent server on stdin/stdout", 0, 0);
    describe_named(arr, "doctor", "setup health check (--output json)", 0, 0);
    describe_named(arr, "status", "resolved model/mode/config (--output json)",
                   0, 0);
    describe_named(arr, "config",
                   "path|show|validate the resolved config "
                   "(+ set/telemetry when configEditable)", 0, 0);
    describe_named(arr, "describe", "this interface-contract dump", 0, 0);
    describe_named(arr, "setup", "guided/flag-driven project setup + --import",
                   0, 0);
    describe_named(arr, "init", "scaffold a .jichi/ asset pack", 0, 0);
    describe_named(arr, "telemetry", "offline log summary (+ --cache-audit)",
                   0, 0);
    describe_named(arr, "audit", "privileged-command audit summary "
                   "(+ --since)", 0, 0);
    describe_named(arr, "runs", "bounded-run journal triage table (+ --all)",
                   0, 0);
    describe_named(arr, "control", "steer a running --control run "
                   "(status/inject/pause/resume/abort)", 0, 0);
    describe_named(arr, "learn", "analyze|apply the propose-only lesson loop",
                   0, 0);
    describe_named(arr, "constraints",
                   "list the enforced store, or `scan <file|->` to predict what "
                   "a prompt would adopt (exit 1 if any)", 0, 0);
    describe_named(arr, "mcp", "list/call configured MCP servers + tools", 0, 0);
    describe_named(arr, "lsp", "diagnostics/symbols/def/refs via a language "
                   "server", 0, 0);
    /* M538: this row used to read "session/export/rewind/undo" -- a `name` that
     * is not a command. `session` is not a subcommand at all: it is the
     * --session FLAG (listed under key_flags), and `jichi session` falls through
     * to being sent to the model as a prompt. The other three are real, and so
     * are `checkpoints` and `recover`, which the collapsed row omitted entirely.
     *
     * This is the row that cost something. Reading it as a list of subcommand
     * names, I probed all four by running them -- and `undo` reverted this
     * repository's working tree, 768 files, because a contract that names a
     * destructive command inside a slash-separated string gives a reader no way
     * to see which of the four writes. ANECDOTES #66; the blast-radius report it
     * produced is M537. */
    describe_named(arr, "export", "export a session transcript (Markdown, "
                   "--html, or --output json)", 0, 0);
    describe_named(arr, "checkpoints", "list this workspace's snapshots "
                   "(+ `gc` to report and prune the shadow store)", 0, 0);
    describe_named(arr, "undo", "REVERTS THE WORKSPACE to the N-th most recent "
                   "checkpoint -- destructive; use --dry-run first", 0, 0);
    describe_named(arr, "rewind", "reverts files AND the conversation to the "
                   "N-th checkpoint's turn -- destructive; --dry-run first",
                   0, 0);
    describe_named(arr, "recover", "materialise a preserved attempt into a "
                   "worktree (--into <dir>)", 0, 0);
    cJSON_AddItemToObject(root, "subcommands", arr);

    /* Built-in tools from the real registry (readonly flag included). */
    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);
    arr = cJSON_CreateArray();
    for (i = 0; i < reg.tools.len; i++) {
        const struct jc_tool *t = *(const struct jc_tool **)
            jc_vec_at(&reg.tools, i);
        describe_named(arr, t->name, t->description, 1, t->readonly);
    }
    cJSON_AddItemToObject(root, "tools", arr);
    jc_tool_registry_free(&reg);

    /* Tools registered only when their prerequisite is present. */
    arr = cJSON_CreateArray();
    {
        static const char *const GITT[] = {
            "git_status", "git_diff", "git_log", "git_blame",
            "git_add", "git_commit", "git_branch", "git_stash", NULL };
        static const char *const LSPT[] = {
            "find_definition", "find_references", "list_symbols",
            "list_code_actions", "rename_symbol", "format_file",
            "apply_code_action", NULL };
        static const char *const MEDT[] = {
            "generate_image", "generate_audio", "transcribe_audio", NULL };
        int k;
        for (k = 0; GITT[k] != NULL; k++) {
            describe_cond(arr, GITT[k], "in a git repository", 0);
        }
        for (k = 0; LSPT[k] != NULL; k++) {
            describe_cond(arr, LSPT[k], "when lspServers is configured", 0);
        }
        describe_cond(arr, "load_skill", "when .jichi/skills exist", 0);
        describe_cond(arr, "web_search", "when search.url is configured", 0);
        describe_cond(arr, "search_docs",
                      "when a docs source + embed model exist", 0);
        for (k = 0; MEDT[k] != NULL; k++) {
            describe_cond(arr, MEDT[k],
                "when a model declares the image/audio/transcribe role", 0);
        }
        describe_cond(arr, "read_mcp_resource",
            "when an MCP server is connected (dropped by the core tool "
            "profile on a small context)", 0);
        describe_cond(arr, "<server>__<tool>",
            "one per tool on each connected MCP server -- a naming "
            "convention, not a tool name", 1);
    }
    cJSON_AddItemToObject(root, "conditional_tools", arr);

    cJSON_AddStringToObject(root, "docs",
        "docs/EMBEDDING.md (which surface, and the STABILITY CONTRACT), "
        "docs/SCRIPTING.md (headless/jsonl), docs/DAEMON.md, docs/ACP.md, "
        "docs/AGENTS_GUIDE.md (writing AGENTS.md)");
    /* M301: point a consumer at the stability policy from inside the contract
     * itself. Before this, which parts were a promise had to be inferred from the
     * ROADMAP -- a design history, not an interface. Kept short for the C89
     * literal limit; docs/EMBEDDING.md section 4 is the full policy. */
    cJSON_AddStringToObject(root, "stability",
        "STABLE: exit codes, jsonl event objects (v+type; existing fields keep "
        "their meaning, new fields/types may appear -- ignore unknown ones), "
        "stop_reason values, daemon request shapes, the core headless flags, this "
        "describe output, and the json projections of ls/export/status/doctor. "
        "PROVISIONAL: prose summaries, the tool set, telemetry JSONL, .jichi/ "
        "assets. NOT an interface: stderr text, the on-disk session store, "
        "internal C symbols. Full policy: docs/EMBEDDING.md section 4.");

    if (json) {
        text = jc_json_print(root);
        printf("%s\n", text != NULL ? text : "{}");
        free(text);
    } else {
        /* A compact human rendering of the same contract. */
        printf("jichi %s -- interface contract\n\n", JC_VERSION);
        printf("Output formats: text, json, jsonl (one object/line, "
               "versioned).\n");
        printf("Exit codes: 0 ok, 1 error, 2 usage/config, 130 interrupted "
               "(SIGINT), 143 terminated (SIGTERM, graceful).\n");
        printf("Modes: chat, plan, auto.\n\n");
        printf("Drive headless:  jichi -p 'task' --output jsonl\n");
        printf("Drive warm:      jichi daemon &  then  --connect "
               "<socket> -p 'task'\n");
        printf("Drive as server: jichi serve   (ACP over "
               "stdin/stdout)\n\n");
        printf("For the full machine-readable contract: "
               "jichi describe --output json\n");
    }
    cJSON_Delete(root);
    return 0;
}

/* `lsp <file>...`           -> diagnostics for each file (exit 1 if any)
 * `lsp symbols <file>`       -> document-symbol outline
 * `lsp def <file> <symbol>`  -> definition of <symbol>
 * `lsp refs <file> <symbol>` -> references to <symbol>
 * Needs config but no chat provider. */
static int run_lsp(struct jc_app *app, struct cli_args *args)
{
    struct jc_lsp_manager lsp;
    int i;
    int code = 0;
    const char *verb;

    if (app->config.lsp_servers.len == 0) {
        printf("(no LSP servers configured; add an \"lspServers\" array to "
               "your config)\n");
        return 0;
    }
    if (args->npos < 2) {
        fprintf(stderr, "usage: jichi lsp <file>... | symbols <file> | "
                        "def <file> <symbol> | refs <file> <symbol> | "
                        "actions <file> <line>\n");
        return 2;
    }
    jc_lsp_manager_init(&lsp, app);
    app->lsp = &lsp;

    verb = args->pos[1];
    if (strcmp(verb, "actions") == 0) {
        char *rep = NULL;
        if (args->npos < 4) {
            fprintf(stderr,
                "usage: jichi lsp actions <file> <line> [kind]\n");
            code = 2;
        } else {
            rep = jc_lsp_code_actions(&lsp, args->pos[2], atol(args->pos[3]),
                                      args->npos > 4 ? args->pos[4] : NULL);
        }
        if (code != 2) {
            if (rep == NULL) {
                printf("no language server matches this file type\n");
            } else {
                struct jc_sb sb;
                int n = 0;
                jc_sb_init(&sb);
                jc_lsp_format_code_actions(rep, &sb, &n);
                free(rep);
                printf("%s", (n > 0 && sb.data != NULL) ? sb.data
                                          : "(no code actions offered)\n");
                jc_sb_free(&sb);
            }
        }
        jc_lsp_manager_shutdown(&lsp);
        return code;
    }
    if (strcmp(verb, "symbols") == 0 || strcmp(verb, "def") == 0 ||
        strcmp(verb, "refs") == 0) {
        char *rep = NULL;
        if (strcmp(verb, "symbols") == 0) {
            if (args->npos < 3) {
                fprintf(stderr, "usage: jichi lsp symbols <file>\n");
                code = 2;
            } else {
                rep = jc_lsp_symbols(&lsp, args->pos[2], NULL);
            }
        } else {
            if (args->npos < 4) {
                fprintf(stderr, "usage: jichi lsp %s <file> <symbol>\n",
                        verb);
                code = 2;
            } else if (strcmp(verb, "def") == 0) {
                rep = jc_lsp_definition(&lsp, args->pos[2], args->pos[3], 0,
                                        NULL);
            } else {
                rep = jc_lsp_references(&lsp, args->pos[2], args->pos[3], 0,
                                        NULL);
            }
        }
        if (code != 2) {
            if (rep == NULL) {
                printf("no language server matches this file type\n");
            } else {
                printf("%s\n", rep);
                free(rep);
            }
        }
        jc_lsp_manager_shutdown(&lsp);
        return code;
    }

    /* Default: diagnostics for each file argument. */
    for (i = 1; i < args->npos; i++) {
        const char *path = args->pos[i];
        int cnt = 0;
        char *rep = jc_lsp_diagnostics(&lsp, path, &cnt);
        if (rep == NULL) {
            printf("%s: no language server matches this file type\n", path);
        } else {
            printf("=== %s ===\n%s\n", path, rep);
            free(rep);
            if (cnt > 0) {
                code = 1;
            }
        }
    }
    jc_lsp_manager_shutdown(&lsp);
    return code;
}

/* M337 `attempts` / `recover`: read back what M336 preserved.
 *
 * Preservation without a retrieval surface is a store nobody can read, and the
 * incident that motivated it (ANECDOTES #48) was as much "could not look at the
 * work" as "could not restore it".
 *
 * `recover` materialises a discarded state into a git WORKTREE, never into the live
 * tree. That is the whole safety property: a command whose purpose is preventing
 * data loss must not be able to cause it by overwriting current work. The user
 * diffs, cherry-picks, or deletes the worktree at leisure. */
static int run_attempts(struct jc_app *app, struct cli_args *args)
{
    struct jc_snapshot_mgr m;
    struct jc_sb list;
    int n = 0;
    char *p;

    jc_snapshot_manager_init(&m, app);
    app->snapshots = &m;
    jc_sb_init(&list);
    if (jc_snapshot_discarded_list(&m, &list) != JC_OK || list.data == NULL) {
        list.data = NULL;
    }
    if (args->output_json) {
        printf("{\"v\":1,\"attempts\":[");
    }
    p = list.data;
    while (p != NULL && *p != '\0') {
        char *eol = strchr(p, '\n');
        char *sha, *ref, *when, *subj;
        if (eol != NULL) {
            *eol = '\0';
        }
        sha = p;
        ref = strchr(sha, '\t');
        if (ref != NULL) { *ref++ = '\0'; }
        when = (ref != NULL) ? strchr(ref, '\t') : NULL;
        if (when != NULL) { *when++ = '\0'; }
        subj = (when != NULL) ? strchr(when, '\t') : NULL;
        if (subj != NULL) { *subj++ = '\0'; }
        if (sha[0] != '\0' && ref != NULL) {
            n++;
            if (args->output_json) {
                printf("%s{\"commit\":\"%s\",\"ref\":\"%s\",\"date\":\"%s\","
                       "\"subject\":\"%s\"}", n > 1 ? "," : "", sha, ref,
                       when != NULL ? when : "", subj != NULL ? subj : "");
            } else {
                printf("%.12s  %s  %s\n              %s\n", sha,
                       when != NULL ? when : "", subj != NULL ? subj : "", ref);
            }
        }
        p = (eol != NULL) ? eol + 1 : NULL;
    }
    if (args->output_json) {
        printf("],\"count\":%d}\n", n);
    } else if (n == 0) {
        printf("(no preserved attempts for this workspace)\n");
        printf("\nA rollback only preserves what it discards when the run is given "
               "--preserve-discarded\n(config \"preserveDiscarded\"); it is off by "
               "default. See docs/AUTONOMY.md.\n");
    } else {
        printf("\n%d preserved attempt(s). Recover one into a worktree with:\n"
               "  jichi recover <commit> --into <dir>\n", n);
    }
    jc_sb_free(&list);
    jc_snapshot_manager_shutdown(&m);
    app->snapshots = NULL;
    return 0;
}

static int run_recover(struct jc_app *app, struct cli_args *args)
{
    struct jc_snapshot_mgr m;
    const char *what = args->pos[1];
    const char *into = args->recover_into;
    char fallback[300];
    int code = 0;

    if (what == NULL || what[0] == '\0') {
        fprintf(stderr, "usage: jichi recover <commit|ref> [--into <dir>]\n"
                "       `jichi attempts` lists what there is to recover\n");
        return 2;
    }
    if (into == NULL || into[0] == '\0') {
        /* A path the user did not choose must be obviously temporary and must not
         * be inside the workspace, where it would look like their own work. */
        jc_snprintf(fallback, sizeof(fallback), "/tmp/jichi-recover-%.12s", what);
        into = fallback;
    }
    if (jc_is_dir(into)) {
        fprintf(stderr, "recover: %s already exists; pick another --into path\n",
                into);
        return 1;
    }
    jc_snapshot_manager_init(&m, app);
    app->snapshots = &m;
    if (!jc_snapshot_available(&m)) {
        fprintf(stderr, "recover: snapshots unavailable here (needs git and "
                "\"snapshots\": true)\n");
        code = 1;
    } else if (jc_snapshot_worktree_add(&m, what, into) != JC_OK) {
        fprintf(stderr, "recover: could not materialise %s at %s -- is that a "
                "commit in this workspace's shadow repo?\n"
                "         `jichi attempts` lists the ones there are.\n",
                what, into);
        code = 1;
    } else {
        printf("recovered %s into %s\n", what, into);
        printf("\nThe live workspace is untouched. Compare with:\n"
               "  diff -ru . %s\n"
               "and remove it when done:\n"
               "  git --git-dir=<shadow> worktree remove %s\n", into, into);
    }
    jc_snapshot_manager_shutdown(&m);
    app->snapshots = NULL;
    return code;
}

/* M335 `checkpoints gc`: report the shadow-checkpoint store and what is garbage.
 *
 * READ-ONLY in this milestone, deliberately. Measured 2026-08-09: 21 shadow
 * repositories, 61 MB, 10 of them pointing at work trees that no longer exist --
 * every throwaway /tmp workspace and every deleted git worktree leaves one behind
 * forever, because `do_prune` bounds the commits INSIDE a live repo and nothing
 * had ever removed a repo. Naming them is most of the value and carries none of
 * the risk; removal needs `--yes` plus a guarded recursive delete and is its own
 * step (the M83-before-M142 order).
 *
 * Works from anywhere: it inspects the whole store, not the current workspace, so
 * it deliberately does NOT go through jc_snapshot_manager_init's availability
 * check the way `checkpoints`/`undo` do. */
/* M338: how many kilobytes a directory occupies, or -1 if `du` cannot say.
 * Shelling to POSIX `du` argv-style (no shell, like every git call here) rather
 * than walking the tree ourselves: gc reports a size for the operator's benefit,
 * and a wrong number is worse than an absent one. */
#define JC_GC_KEEP_DEFAULT      20
#define JC_GC_KEEP_DAYS_DEFAULT   7

static long gc_dir_kb(const char *dir)
{
    char *argv[4];
    struct jc_sb out;
    long kb = -1;

    jc_sb_init(&out);
    argv[0] = (char *)"du";
    argv[1] = (char *)"-sk";
    argv[2] = (char *)dir;
    argv[3] = NULL;
    if (jc_proc_capture(argv, NULL, NULL, &out, 4096, 20L, NULL) == JC_OK
            && out.data != NULL) {
        kb = atol(out.data);
    }
    jc_sb_free(&out);
    return kb;
}

/* M338: apply the section-10.4 retention policy to one repo's discarded refs.
 *
 * Counts what is expired and, when `remove` is set, deletes those refs and packs
 * the repo so the objects behind them can actually be reclaimed -- a ref makes
 * its commit reachable, so deleting the ref is the ONLY way `gc --prune=now`
 * will ever collect it. Reporting without removing is the default because these
 * refs are the last copy of work a rollback or an undo already destroyed. */
static void gc_repo_discarded(const char *gitdir, int keep_days, int keep_n,
                              int remove, int *n_kept, int *n_dropped)
{
    char *argv[7];
    struct jc_sb out;
    char *p;
    int rank = 0;
    double now = jc_now_seconds();

    jc_sb_init(&out);
    argv[0] = (char *)"git";
    argv[1] = (char *)"--git-dir";
    argv[2] = (char *)gitdir;
    argv[3] = (char *)"for-each-ref";
    argv[4] = (char *)"--sort=-committerdate";
    argv[5] = (char *)"--format=%(refname) %(committerdate:unix)";
    argv[6] = NULL;
    /* refs/jichi/discarded is passed as the pattern via a 8th slot below; git
     * accepts the pattern after the options, so build the full argv here. */
    {
        char *full[8];
        int k;
        for (k = 0; k < 6; k++) {
            full[k] = argv[k];
        }
        full[6] = (char *)"refs/jichi/discarded";
        full[7] = NULL;
        if (jc_proc_capture(full, NULL, NULL, &out, 1 << 20, 30L, NULL)
                != JC_OK) {
            jc_sb_free(&out);
            return;
        }
    }
    p = out.data;
    while (p != NULL && *p != '\0') {
        char *eol = p;
        char *sp;
        while (*eol != '\0' && *eol != '\n') {
            eol++;
        }
        if (*eol == '\n') {
            *eol = '\0';
            eol++;
        }
        sp = p;
        while (*sp != '\0' && *sp != ' ') {
            sp++;
        }
        if (*sp == ' ') {
            long secs;
            long age_days;
            *sp = '\0';
            secs = atol(sp + 1);
            age_days = (long)((now - (double)secs) / 86400.0);
            if (age_days < 0) {
                age_days = 0;   /* a clock skew must not delete anything */
            }
            if (jc_snapshot_retain(age_days, rank, keep_days, keep_n)) {
                (*n_kept)++;
            } else {
                (*n_dropped)++;
                if (remove) {
                    char *del[7];
                    del[0] = (char *)"git";
                    del[1] = (char *)"--git-dir";
                    del[2] = (char *)gitdir;
                    del[3] = (char *)"update-ref";
                    del[4] = (char *)"-d";
                    del[5] = p;
                    del[6] = NULL;
                    (void)jc_proc_capture(del, NULL, NULL, NULL, 0, 30L, NULL);
                }
            }
            rank++;
        }
        p = eol;
    }
    jc_sb_free(&out);
    if (remove && *n_dropped > 0) {
        char *gcv[6];
        gcv[0] = (char *)"git";
        gcv[1] = (char *)"--git-dir";
        gcv[2] = (char *)gitdir;
        gcv[3] = (char *)"gc";
        gcv[4] = (char *)"--prune=now";
        gcv[5] = NULL;
        (void)jc_proc_capture(gcv, NULL, NULL, NULL, 0, 300L, NULL);
    }
}

/* M338: delete one orphaned shadow repository.
 *
 * `rm -rf` on a computed path, so the guards are the whole safety argument and
 * every one of them is checked by the caller before we are reached:
 *   - `name` came from jc_list_dir of the store root, so it is a single entry
 *   - it is re-checked here for '/' and for '.'/'..', so no traversal
 *   - the state was JC_STORE_ORPHANED: work tree gone, parent still present
 *   - --yes was passed explicitly
 * argv-style via jc_proc_capture, never a shell, so the path cannot be
 * reinterpreted as anything else. */
static int gc_remove_repo(const char *root, const char *name)
{
    char dir[700];
    char *argv[4];
    const char *c;

    if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0
            || strcmp(name, "..") == 0) {
        return 0;
    }
    for (c = name; *c != '\0'; c++) {
        if (*c == '/') {
            return 0;
        }
    }
    jc_snprintf(dir, sizeof(dir), "%s/%s", root, name);
    if (!jc_is_dir(dir)) {
        return 0;
    }
    argv[0] = (char *)"rm";
    argv[1] = (char *)"-rf";
    argv[2] = dir;
    argv[3] = NULL;
    if (jc_proc_capture(argv, NULL, NULL, NULL, 0, 120L, NULL) != JC_OK) {
        return 0;
    }
    return jc_is_dir(dir) ? 0 : 1;
}

static int run_checkpoints_gc(struct jc_app *app, struct cli_args *args)
{
    char root[600];
    struct jc_vec names;
    struct jc_arena *a = jc_app_scratch(app);
    jc_size i;
    int n_live = 0, n_orph = 0, n_unre = 0, n_unk = 0;
    int as_json = (args->output_json != 0);
    int d_kept = 0, d_dropped = 0, removed = 0;
    long kb_total = 0, kb_orph = 0, kb_freed = 0;
    /* M338: retention reuses `prune`'s EXISTING --keep / --older-than vocabulary
     * rather than inventing --keep-days. A second retention vocabulary in one
     * binary is the drift the lints exist to catch, and `--keep` was already
     * taken -- my colliding flag silently lost the else-if chain and reported the
     * default while appearing to accept the value, which is how this was found. */
    int keep_n = (args->prune_keep >= 0) ? (int)args->prune_keep
                                         : JC_GC_KEEP_DEFAULT;
    int keep_days = JC_GC_KEEP_DAYS_DEFAULT;
    const char *home = getenv("HOME");

    if (args->prune_older != NULL) {
        long secs = 0;
        if (jc_env_parse_duration(args->prune_older, &secs) != 0 || secs <= 0) {
            fprintf(stderr, "checkpoints gc: --older-than wants a duration like "
                            "7d, 12h (got '%s')\n", args->prune_older);
            return 2;
        }
        keep_days = (int)(secs / 86400L);
    }
    jc_snprintf(root, sizeof(root), "%s/.jichi.d/checkpoints",
                home != NULL ? home : ".");
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(root, &names, a) != JC_OK) {
        if (!as_json) {
            printf("checkpoint store: %s (absent or unreadable)\n", root);
        } else {
            printf("{\"v\":1,\"store\":\"%s\",\"repos\":[]}\n", root);
        }
        return 0;
    }

    if (as_json) {
        printf("{\"v\":1,\"store\":\"%s\",\"repos\":[", root);
    } else {
        printf("checkpoint store: %s\n\n", root);
    }

    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char gitdir[700];
        char parent[700];
        struct jc_sb out;
        char *wt;
        jc_size k;
        enum jc_store_state st;
        char *argv[6];
        const char *label;
        long kb;

        jc_snprintf(gitdir, sizeof(gitdir), "%s/%s", root, nm);
        if (!jc_is_dir(gitdir)) {
            continue;
        }
        /* Ask git rather than parsing config ourselves: it is the authority on
         * where this repo believes its work tree is. */
        jc_sb_init(&out);
        argv[0] = (char *)"git";
        argv[1] = (char *)"--git-dir";
        argv[2] = gitdir;
        argv[3] = (char *)"config";
        argv[4] = (char *)"--get";
        argv[5] = NULL;
        {
            char *argv2[7];
            argv2[0] = argv[0]; argv2[1] = argv[1]; argv2[2] = argv[2];
            argv2[3] = argv[3]; argv2[4] = argv[4];
            argv2[5] = (char *)"core.worktree";
            argv2[6] = NULL;
            (void)jc_proc_capture(argv2, NULL, NULL, &out, 4096, 10L, NULL);
        }
        wt = out.data;
        if (wt != NULL) {
            for (k = 0; wt[k] != '\0'; k++) {
                if (wt[k] == '\n' || wt[k] == '\r') { wt[k] = '\0'; break; }
            }
        }
        /* M338: jc_proc_capture MERGES stderr, so any warning git prints on this
         * call arrives here as if it were the worktree path. Found with a `git
         * init --bare` fixture, whose "core.bare and core.worktree do not make
         * sense" warning became the reported work tree.
         *
         * It failed safe -- unparseable text yields UNREACHABLE, which refuses
         * to remove -- but this value now decides what `--yes` DELETES, so it is
         * validated rather than trusted: core.worktree is always absolute, so
         * anything that does not start with '/' is not an answer. */
        if (wt != NULL && wt[0] != '/') {
            wt = NULL;
        }
        /* The parent is the (imperfect) discriminator between a deleted project
         * and an unmounted volume -- see jc_snapshot_store_state. */
        parent[0] = '\0';
        if (wt != NULL && wt[0] != '\0') {
            jc_size len = (jc_size)strlen(wt);
            jc_size cut = 0;
            for (k = 0; k < len; k++) {
                if (wt[k] == '/') { cut = k; }
            }
            if (cut > 0 && cut < sizeof(parent)) {
                memcpy(parent, wt, cut);
                parent[cut] = '\0';
            }
        }
        st = jc_snapshot_store_state(wt,
                                    (wt != NULL && wt[0] != '\0')
                                        ? jc_is_dir(wt) : 0,
                                    parent[0] != '\0' ? jc_is_dir(parent) : 0);
        switch (st) {
        case JC_STORE_LIVE:        label = "live";        n_live++; break;
        case JC_STORE_ORPHANED:    label = "orphaned";    n_orph++; break;
        case JC_STORE_UNREACHABLE: label = "unreachable"; n_unre++; break;
        default:                   label = "unknown";     n_unk++;  break;
        }
        kb = gc_dir_kb(gitdir);
        if (kb > 0) {
            kb_total += kb;
            if (st == JC_STORE_ORPHANED) {
                kb_orph += kb;
            }
        }
        /* Discarded refs are swept in LIVE repos only. In an orphaned one the
         * whole directory is the unit of removal, and in an unreachable one we
         * must touch nothing at all -- the work tree may simply be unmounted. */
        if (st == JC_STORE_LIVE) {
            gc_repo_discarded(gitdir, keep_days, keep_n,
                              args->gc_yes, &d_kept, &d_dropped);
        }
        if (st == JC_STORE_ORPHANED && args->gc_yes) {
            if (gc_remove_repo(root, nm)) {
                removed++;
                if (kb > 0) {
                    kb_freed += kb;
                }
            }
        }
        if (as_json) {
            printf("%s{\"dir\":\"%s\",\"state\":\"%s\",\"worktree\":\"%s\","
                   "\"kb\":%ld}",
                   (n_live + n_orph + n_unre + n_unk) > 1 ? "," : "",
                   nm, label, (wt != NULL) ? wt : "", kb);
        } else if (st != JC_STORE_LIVE) {
            printf("  %-11s %s\n               -> %s\n", label, nm,
                   (wt != NULL && wt[0] != '\0') ? wt : "(no core.worktree)");
        }
        jc_sb_free(&out);
    }

    if (as_json) {
        printf("],\"live\":%d,\"orphaned\":%d,\"unreachable\":%d,"
               "\"unknown\":%d,\"kb\":%ld,\"orphaned_kb\":%ld,"
               "\"discarded_kept\":%d,\"discarded_expired\":%d,"
               "\"removed\":%d,\"freed_kb\":%ld,\"applied\":%s}\n",
               n_live, n_orph, n_unre, n_unk, kb_total, kb_orph,
               d_kept, d_dropped, removed, kb_freed,
               args->gc_yes ? "true" : "false");
    } else {
        printf("\n  %d live, %d orphaned, %d unreachable, %d unknown"
               "  (%ld KB total)\n",
               n_live, n_orph, n_unre, n_unk, kb_total);
        /* M338: preserved states are pinned by a ref, so `gc --prune=now` can
         * never reclaim them on its own -- this is the only thing that can. */
        if (d_kept + d_dropped > 0) {
            printf("\n  %d preserved attempt(s): %d within the retention policy "
                   "(%d days / newest %d),\n  %d expired.\n",
                   d_kept + d_dropped, d_kept, keep_days,
                   keep_n, d_dropped);
        }
        if (n_orph > 0 && !args->gc_yes) {
            printf("\n  %d orphaned repo(s): their work tree is gone, so their "
                   "history is unreachable\n  from jichi (%ld KB).\n", n_orph,
                   kb_orph);
        }
        if (!args->gc_yes && (n_orph > 0 || d_dropped > 0)) {
            printf("\n  nothing was removed. `checkpoints gc --yes` deletes the "
                   "%d orphaned repo(s)\n  and the %d expired attempt(s); "
                   "--keep/--older-than change the policy.\n",
                   n_orph, d_dropped);
        }
        if (args->gc_yes) {
            printf("\n  removed %d orphaned repo(s) and %d expired attempt(s)"
                   " (%ld KB reclaimed).\n", removed, d_dropped, kb_freed);
        }
        if (n_unre > 0) {
            printf("\n  %d unreachable repo(s): the work tree AND its parent are "
                   "missing, which looks\n  like an unmounted volume rather than "
                   "a deleted project. These are NOT garbage\n  unless you know "
                   "the disk is gone for good.\n", n_unre);
        }
    }
    return 0;
}

/* `checkpoints` lists the workspace's snapshots (newest first); `undo [N]`
 * restores the work tree to the N-th most recent checkpoint (default 1). Needs
 * no provider or network. Exit 1 on error / out-of-range. */
static int run_snapshot(struct jc_app *app, struct cli_args *args)
{
    struct jc_snapshot_mgr m;
    const char *cmd = args->pos[0];
    int code = 0;

    /* M335: `checkpoints gc` inspects the whole store, not this workspace, so it
     * runs before the availability gate below -- which would refuse in exactly
     * the case you most want to tidy up (a workspace that no longer qualifies). */
    if (strcmp(cmd, "checkpoints") == 0 && args->pos[1] != NULL
            && strcmp(args->pos[1], "gc") == 0) {
        return run_checkpoints_gc(app, args);
    }

    jc_snapshot_manager_init(&m, app); /* also refreshes from git history */
    app->snapshots = &m;

    if (!jc_snapshot_available(&m)) {
        fprintf(stderr, "snapshots unavailable here (needs git, a workspace "
                "that isn't huge, and \"snapshots\": true)\n");
        jc_snapshot_manager_shutdown(&m);
        return 1;
    }

    if (strcmp(cmd, "checkpoints") == 0) {
        int n = jc_snapshot_count(&m);
        if (n == 0) {
            printf("(no checkpoints for this workspace)\n");
        } else {
            int i;
            for (i = 0; i < n; i++) {
                const char *lbl = jc_snapshot_label(&m, n - 1 - i);
                printf("%3d  %s\n", i + 1, lbl != NULL ? lbl : "");
            }
        }
    } else { /* "undo" */
        int which = 1;
        const char *lbl = NULL;
        struct jc_sb scope;          /* M537: blast radius, measured pre-restore */
        if (args->npos >= 2) {
            which = atoi(args->pos[1]);
            if (which < 1) {
                which = 1;
            }
        }
        if (args->dry_run) {
            struct jc_sb prev;
            jc_sb_init(&prev);
            if (jc_snapshot_preview_index(&m, which, &prev, &lbl) == JC_OK) {
                printf("Dry run - `undo %d` would revert to checkpoint %d%s%s\n",
                       which, which, (lbl != NULL && lbl[0] != '\0') ? ": " : "",
                       (lbl != NULL && lbl[0] != '\0') ? lbl : "");
                printf("%s", prev.data != NULL ? prev.data : "");
                printf("(nothing changed; re-run without --dry-run to apply)\n");
            } else {
                fprintf(stderr, "undo: no checkpoint %d "
                        "(see `jichi checkpoints`)\n", which);
                code = 1;
            }
            jc_sb_free(&prev);
            jc_snapshot_manager_shutdown(&m);
            return code;
        }
        /* M537: measure BEFORE restoring -- afterwards there is nothing left to
         * measure, which is exactly why `undo` had gone thirteen milestones
         * printing a label and no magnitude. A failure here must not stop the
         * undo: not knowing the size is bad, refusing to work is worse. */
        jc_sb_init(&scope);
        (void)jc_snapshot_scope_index(&m, which, &scope);
        if (jc_snapshot_restore_index(&m, which, &lbl) == JC_OK) {
            if (lbl != NULL && lbl[0] != '\0') {
                printf("reverted to checkpoint %d: %s\n", which, lbl);
            } else {
                printf("reverted to checkpoint %d\n", which);
            }
            if (scope.data != NULL && scope.data[0] != '\0') {
                printf("  %s\n", scope.data);
            }
            /* M337b: undo destroys whatever was in the tree, and there is no
             * redo. When preservation is on, say where it went -- a save the
             * user is not told about is a store nobody knows to read. */
            if (jc_snapshot_preserved_last(&m) != NULL) {
                printf("the discarded state is preserved at %.12s"
                       " (`jichi recover %.12s --into <dir>`)\n",
                       jc_snapshot_preserved_last(&m),
                       jc_snapshot_preserved_last(&m));
            }
        } else {
            fprintf(stderr, "undo: no checkpoint %d "
                    "(see `jichi checkpoints`)\n", which);
            code = 1;
        }
        jc_sb_free(&scope);
    }

    jc_snapshot_manager_shutdown(&m);
    return code;
}

/* `rewind [<n>] [--dry-run]`: return both the files AND the conversation to an
 * earlier point -- restore the work tree to the n-th most recent checkpoint
 * (n=1 latest, the default) and truncate the session's history back to the user
 * message that started that turn, then re-save. The session is the one named by
 * --session, else the most recent for this project. Read-only on --dry-run. */
static int run_rewind(struct jc_app *app, struct cli_args *args)
{
    struct jc_snapshot_mgr m;
    struct jc_session sess;
    const char *lbl = NULL;
    char cwd[1024];
    int n = 1;
    int cut;
    int dropped;
    int rc = 0;

    if (args->npos >= 2) {
        n = atoi(args->pos[1]);
        if (n < 1) {
            n = 1;
        }
    }

    /* Resolve the session: explicit --session id/prefix, else recent-for-cwd. */
    if (app->session_id != NULL && app->session_id[0] != '\0') {
        char full[64];
        int r = jc_session_resolve_prefix(app->session_id, full, sizeof(full),
                                          app->arena);
        if (r != 0 ||
            jc_session_load_by_id(full, &sess, app->arena) != JC_OK) {
            fprintf(stderr, "error: no session matching '%s'\n",
                    app->session_id);
            return 1;
        }
    } else {
        const char *scope = NULL;
        if (!args->all && getcwd(cwd, sizeof(cwd)) != NULL) {
            scope = cwd;
        }
        if (jc_session_load_recent_scoped(scope, &sess, app->arena) != JC_OK) {
            fprintf(stderr, "error: no saved session to rewind%s\n",
                    args->all ? "" : " for this project");
            return 1;
        }
    }

    jc_snapshot_manager_init(&m, app);
    app->snapshots = &m;
    if (!jc_snapshot_available(&m)) {
        fprintf(stderr, "snapshots unavailable here (needs git, a workspace "
                "that isn't huge, and \"snapshots\": true)\n");
        jc_snapshot_manager_shutdown(&m);
        jc_session_free(&sess);
        return 1;
    }

    cut = jc_snapshot_rewind_cut(&m, &sess.history, n);
    if (cut < 0) {
        fprintf(stderr, "rewind: no checkpoint %d, or its turn is not in this "
                "session's history (see `jichi checkpoints`)\n", n);
        jc_snapshot_manager_shutdown(&m);
        jc_session_free(&sess);
        return 1;
    }
    dropped = (int)jc_history_len(&sess.history) - cut;

    if (args->dry_run) {
        struct jc_sb prev;
        jc_sb_init(&prev);
        jc_snapshot_preview_index(&m, n, &prev, &lbl);
        printf("Dry run - `rewind %d` would:\n", n);
        printf("  * restore files to checkpoint %d%s%s\n", n,
               (lbl != NULL && lbl[0] != '\0') ? ": " : "",
               (lbl != NULL && lbl[0] != '\0') ? lbl : "");
        printf("%s", prev.data != NULL ? prev.data : "");
        printf("  * drop %d message%s from the conversation\n", dropped,
               dropped == 1 ? "" : "s");
        printf("(nothing changed; re-run without --dry-run to apply)\n");
        jc_sb_free(&prev);
        jc_snapshot_manager_shutdown(&m);
        jc_session_free(&sess);
        return 0;
    }

    if (jc_snapshot_restore_index(&m, n, &lbl) != JC_OK) {
        fprintf(stderr, "rewind: failed to restore checkpoint %d\n", n);
        rc = 1;
    } else {
        jc_history_truncate(&sess.history, (jc_size)cut);
        if (jc_session_save(&sess) != JC_OK) {
            fprintf(stderr, "rewind: restored files but could not save the "
                    "trimmed session\n");
            rc = 1;
        } else {
            printf("rewound to checkpoint %d%s%s (dropped %d message%s)\n", n,
                   (lbl != NULL && lbl[0] != '\0') ? ": " : "",
                   (lbl != NULL && lbl[0] != '\0') ? lbl : "",
                   dropped, dropped == 1 ? "" : "s");
            if (jc_snapshot_preserved_last(&m) != NULL) {
                printf("the discarded state is preserved at %.12s"
                       " (`jichi recover %.12s --into <dir>`)\n",
                       jc_snapshot_preserved_last(&m),
                       jc_snapshot_preserved_last(&m));
            }
        }
    }

    jc_snapshot_manager_shutdown(&m);
    jc_session_free(&sess);
    return rc;
}

/* `skills`: list the workspace's agent skills (name + description). Needs no
 * provider or network. */
static int run_skills(struct jc_app *app, struct cli_args *args)
{
    int n;
    int i;

    (void)args;
    jc_skill_load(&app->skills, app->cwd, app->arena);
    n = jc_skill_count(&app->skills);
    if (n == 0) {
        printf("(no skills; add .jichi/skills/<name>/SKILL.md files)\n");
        return 0;
    }
    for (i = 0; i < n; i++) {
        const struct jc_skill *sk = jc_skill_at(&app->skills, i);
        printf("  %s - %s\n", sk->name,
               (sk->description != NULL) ? sk->description : "");
    }
    return 0;
}

/* `agents` -> list discovered named agent profiles (no provider/network). */
static int run_agents(struct jc_app *app)
{
    struct jc_sb sb;
    jc_agentdef_load(&app->agents, app->cwd, app->arena);
    if (app->agents.defs.len == 0) {
        printf("(no agent profiles; add .jichi/agents/<name>.md files, "
               "or run `init`)\n");
        return 0;
    }
    jc_sb_init(&sb);
    jc_agentdef_render_list(&app->agents, &sb);
    fputs(sb.data != NULL ? sb.data : "", stdout);
    jc_sb_free(&sb);
    return 0;
}

/* `commands` -> list discovered custom slash commands (no provider/network). */
static int run_commands(struct jc_app *app)
{
    struct jc_sb sb;
    jc_command_load(&app->commands, app->cwd, app->arena);
    if (app->commands.commands.len == 0) {
        printf("(no custom commands; add .jichi/commands/<name>.md files, "
               "or run `init`)\n");
        return 0;
    }
    jc_sb_init(&sb);
    jc_command_render_list(&app->commands, &sb);
    fputs(sb.data != NULL ? sb.data : "", stdout);
    jc_sb_free(&sb);
    return 0;
}

/* `output-styles` -> list discovered output styles, marking the active one
 * (config `outputStyle`). No provider/network. */
static int run_output_styles(struct jc_app *app)
{
    struct jc_sb sb;
    jc_output_style_set_init(&app->output_styles);
    jc_output_style_load(&app->output_styles, app->cwd, app->arena);
    if (app->config.output_style != NULL) {
        jc_output_style_set_active(&app->output_styles,
                                   app->config.output_style);
    }
    if (app->output_styles.styles.len == 0) {
        printf("(no output styles; add .jichi/output-styles/<name>.md files)\n");
        return 0;
    }
    jc_sb_init(&sb);
    jc_output_style_render_list(&app->output_styles, &sb);
    fputs(sb.data != NULL ? sb.data : "", stdout);
    jc_sb_free(&sb);
    return 0;
}

/* Sort a jc_vec of char* names so a numbered curriculum lists in order. */
static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(char * const *)a, *(char * const *)b);
}

/* M529: ONE discovery path for the assignment set.
 *
 * `assignments` renders it two ways (a text table and a JSON array) and the
 * daemon's `assignment.list` verb renders it a third, and all three have to
 * agree about what counts as an assignment -- which of `.md` files is a spec,
 * which is a solution sibling, that INDEX.md is a map and not a task. Those
 * rules lived inside the listing loop, so a second reader meant a second copy
 * of them. Collect once, render N times. */
struct assign_row {
    const char *name;                 /* bare filename, arena-owned  */
    struct jc_assign_spec spec;       /* parsed frontmatter + body   */
    int has_sol;                      /* a .solution.md sibling      */
    struct jc_progress prog;          /* the learner's standing      */
    struct jc_hints hints;            /* hint pulls, deepest rung    */
};

static void assignments_collect(struct jc_app *app, struct jc_vec *rows)
{
    struct jc_vec names;
    char dir[1100];
    char solp[1300];
    char ppath[1160];
    char *progress = NULL;
    char *hintlog = NULL;
    jc_size i;

    jc_snprintf(dir, sizeof(dir), "%s/docs/assignments", app->cwd);
    jc_snprintf(ppath, sizeof(ppath), "%s/.jichi/progress.jsonl", app->cwd);
    if (jc_read_file(ppath, &progress, NULL, app->arena) != JC_OK) {
        progress = NULL; /* no record yet -- every status renders "-" */
    }
    /* M502: the hint log is a separate sink, so it is read separately and can
     * never be mistaken for an attempt. */
    jc_snprintf(ppath, sizeof(ppath), "%s/.jichi/hints.jsonl", app->cwd);
    if (jc_read_file(ppath, &hintlog, NULL, app->arena) != JC_OK) {
        hintlog = NULL;
    }
    jc_vec_init(&names, sizeof(char *));
    jc_list_dir(dir, &names, app->arena);
    if (names.len > 1) {
        qsort(names.data, (size_t)names.len, sizeof(char *), name_cmp);
    }
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        jc_size len = (jc_size)strlen(nm);
        struct assign_row row;
        char *text = NULL;

        if (len < 4 || strcmp(nm + len - 3, ".md") != 0) {
            continue; /* not a markdown file (fixture dirs are skipped too) */
        }
        if (len >= 12 && strcmp(nm + len - 12, ".solution.md") == 0) {
            continue; /* the solution sibling is listed against its assignment */
        }
        if (strcmp(nm, "INDEX.md") == 0) {
            continue; /* the set's map, not an assignment */
        }
        memset(&row, 0, sizeof(row));
        row.name = nm;
        jc_snprintf(solp, sizeof(solp), "%s/%.*s.solution.md", dir,
                    (int)(len - 3), nm);
        row.has_sol = jc_file_exists(solp);
        jc_snprintf(solp, sizeof(solp), "%s/%s", dir, nm);
        if (jc_read_file(solp, &text, NULL, app->arena) == JC_OK) {
            jc_assign_parse(text, &row.spec, app->arena); /* tolerate failure */
        }
        jc_progress_scan(progress, nm, &row.prog);
        jc_progress_hints_scan(hintlog, nm, &row.hints);
        jc_vec_push(rows, &row);
    }
    jc_vec_free(&names);
}

/* One row as the machine-readable object. Shared by `assignments --output json`
 * and the daemon verb for the same reason the collector is shared. */
static cJSON *assign_row_json(const struct assign_row *r)
{
    cJSON *o = cJSON_CreateObject();
    char rel[1200];
    if (o == NULL) {
        return NULL;
    }
    jc_snprintf(rel, sizeof(rel), "docs/assignments/%s", r->name);
    cJSON_AddStringToObject(o, "file", rel);
    /* M529: the bare name too, because it is what the daemon's assignment.get
     * and .grade verbs take -- a caller should not have to strip a prefix to
     * turn a listing into a request. */
    cJSON_AddStringToObject(o, "name", r->name);
    if (r->spec.title != NULL) {
        cJSON_AddStringToObject(o, "title", r->spec.title);
    }
    if (r->spec.phase != NULL) {
        cJSON_AddStringToObject(o, "phase", r->spec.phase);
    }
    if (r->spec.difficulty != NULL) {
        cJSON_AddStringToObject(o, "difficulty", r->spec.difficulty);
    }
    cJSON_AddNumberToObject(o, "points", (double)r->spec.points);
    cJSON_AddBoolToObject(o, "solution", r->has_sol);
    cJSON_AddNumberToObject(o, "attempts", (double)r->prog.attempts);
    cJSON_AddBoolToObject(o, "passed", r->prog.passed);
    cJSON_AddNumberToObject(o, "best_pct", (double)r->prog.best_pct);
    /* M502: emitted always, not only when non-zero -- an absent key would be
     * indistinguishable from "this jichi does not report hints", which is the
     * M301 lesson about additive fields. */
    cJSON_AddNumberToObject(o, "hint_pulls", (double)r->hints.pulls);
    cJSON_AddNumberToObject(o, "hint_rung", (double)r->hints.max_rung);
    return o;
}

/* `assignments` -> list assignment files under docs/assignments/ (M17), with
 * phase/points/status columns (C4/C5, M174): the spec's own frontmatter plus
 * the learner's standing from .jichi/progress.jsonl (written by
 * `grade --record` and the TUI /grade). No provider/network. */
static int run_assignments(struct jc_app *app, int json)
{
    struct jc_vec rows;
    jc_size i;

    jc_vec_init(&rows, sizeof(struct assign_row));
    assignments_collect(app, &rows);

    if (json) {
        cJSON *arr = cJSON_CreateArray();
        char *s;
        for (i = 0; i < rows.len && arr != NULL; i++) {
            cJSON *o = assign_row_json((struct assign_row *)jc_vec_at(&rows, i));
            if (o != NULL) {
                cJSON_AddItemToArray(arr, o);
            }
        }
        s = (arr != NULL) ? cJSON_Print(arr) : NULL;
        printf("%s\n", s != NULL ? s : "[]");
        if (s != NULL) {
            free(s);
        }
        cJSON_Delete(arr);
    } else if (rows.len == 0) {
        printf("(no assignments under docs/assignments/; enable "
               "\"assignments\" in config and `init assignments`, then ask the "
               "agent to write one)\n");
    } else {
        char row[512];
        for (i = 0; i < rows.len; i++) {
            const struct assign_row *r =
                (const struct assign_row *)jc_vec_at(&rows, i);
            if (i == 0) {
                jc_progress_row_header(row, sizeof(row));
                printf("  %s\n", row);
            }
            jc_progress_row(r->name, r->spec.phase, r->spec.points, r->has_sol,
                            &r->prog, row, sizeof(row));
            /* Appended rather than a column, so the pinned row layout (and the
             * tests over it) is unchanged, and a learner who pulled no hints
             * sees nothing extra. */
            if (r->hints.pulls > 0) {
                printf("  %s  hints %d (deepest rung %d)\n", row,
                       r->hints.pulls, r->hints.max_rung);
            } else {
                printf("  %s\n", row);
            }
        }
    }
    jc_vec_free(&rows);
    return 0;
}

/* `board [add <title> | move <id> <state> | done <id> | phase <name>]` (#7):
 * view or edit the persisted kanban board from the CLI. No model needed. */
static int run_board(struct jc_app *app, struct cli_args *args)
{
    const char *sub = (args->npos > 1) ? args->pos[1] : NULL;
    jc_board_load(&app->board, app->cwd);
    if (sub == NULL || strcmp(sub, "list") == 0) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_board_render(&app->board, &sb);
        printf("%s", sb.data != NULL ? sb.data : "");
        jc_sb_free(&sb);
        return 0;
    }
    if (strcmp(sub, "add") == 0) {
        const char *title = (args->npos > 2) ? args->pos[2] : NULL;
        const char *phase = (args->npos > 3) ? args->pos[3] : NULL;
        int id;
        if (title == NULL) {
            fprintf(stderr, "usage: board add <title> [phase]\n");
            return 2;
        }
        id = jc_board_add(&app->board, title, phase, NULL);
        jc_board_save(&app->board, app->cwd);
        printf("added card [%d]\n", id);
        return 0;
    }
    if (strcmp(sub, "move") == 0 || strcmp(sub, "done") == 0) {
        int id = (args->npos > 2) ? atoi(args->pos[2]) : 0;
        int st = (strcmp(sub, "done") == 0) ? 2
               : jc_board_state_from_str((args->npos > 3) ? args->pos[3] : "");
        if (id <= 0 || st < 0) {
            fprintf(stderr, "usage: board move <id> <todo|doing|done>  |  "
                            "board done <id>\n");
            return 2;
        }
        if (!jc_board_move(&app->board, id, st)) {
            fprintf(stderr, "board: no card with id %d\n", id);
            return 1;
        }
        jc_board_save(&app->board, app->cwd);
        printf("card [%d] -> %s\n", id, jc_board_state_word(st));
        return 0;
    }
    if (strcmp(sub, "phase") == 0) {
        jc_board_set_active_phase(&app->board,
                                  (args->npos > 2) ? args->pos[2] : NULL);
        jc_board_save(&app->board, app->cwd);
        printf("active phase set\n");
        return 0;
    }
    fprintf(stderr, "usage: board [list | add <title> [phase] | "
                    "move <id> <state> | done <id> | phase <name>]\n");
    return 2;
}

/* `rules` -> print the resolved project-rules chain (AGENTS.md/CLAUDE.md +
 * config instructions, with their source paths). No provider/network. */
static int run_rules(struct jc_app *app)
{
    char *rules = jc_rules_load(app);
    if (rules == NULL || rules[0] == '\0') {
        printf("(no project rules; add AGENTS.md or CLAUDE.md, or run `init`)\n");
        return 0;
    }
    printf("%s", rules);
    if (rules[strlen(rules) - 1] != '\n') {
        printf("\n");
    }
    return 0;
}

/* `sysmsg` -> print the fully resolved system prompt (a debugging aid: shows
 * exactly what rules/memory/repo-map/skills the agent is given). No network. */
/* M444: is the autonomy envelope armed by this invocation?
 *
 * Factored out of main() so an INTROSPECTION subcommand can ask the same question a
 * run asks. `jichi sysmsg` and `context` dispatch long before main() reaches the
 * arming block, so every envelope-gated prompt section was missing from their output --
 * the M355 flight plan, the M387 STATE-THE-REACH paragraph with its glob list, the
 * M332 gate contract -- while DESIGN_INPUT.md offers `jichi --design <f> sysmsg` as the
 * way to "print the full system prompt". It was not wrong about the design section; it
 * was incomplete about everything env-gated, which is now more of the prompt than it
 * was when that sentence was written. */
static int envelope_is_armed(const struct cli_args *a, const struct jc_app *app)
{
    return (a->verify_cmd != NULL || a->budget_tokens != NULL ||
        a->deadline != NULL || a->max_tool_calls > 0 || a->max_reads > 0 ||
        a->verify_retries >= 0 || a->no_rollback || a->revert_oos > 0 ||
        a->n_edit_scope > 0 ||
        a->journal_path != NULL || a->verify_timeout != NULL ||
        a->strict_scope || a->verify_baseline || a->verify_every > 0 ||
        a->verify_kind != NULL ||
        app->config.verify != NULL || app->config.edit_scope.len > 0);
}

/* Arm `env` from the flags and config, exactly as a real run does.
 *
 * `journal_path` NULL means NO JOURNAL, and that split is the whole point: the arming
 * is pure computation, the journal is the only side effect, and a read-only
 * introspection command must not create a file under ~/.jichi.d/runs because someone
 * asked to see the prompt.
 *
 * Deliberately does NOT set app->env or promote the mode. Those belong to a run, and
 * doing them here is exactly how a refactor like this turns a read-only command into a
 * side-effecting one -- so the caller decides, and the two callers decide differently. */
static void envelope_arm(struct cli_args *a, struct jc_app *app,
                         struct jc_envelope *env, struct jc_arena *arena,
                         const char *run_id, const char *journal_path)
{
    jc_env_init(env, arena, run_id, journal_path);


    /* Limits: CLI overrides config. With no explicit verifier, fall back to
     * the project's testCommand so an armed --auto run actually verifies
     * (and routing.escalateOnVerify becomes meaningful) — mirroring how
     * run_tests/`test` resolve testCommand→verify (M55a). */
    env->verify_cmd = (a->verify_cmd != NULL)
        ? jc_arena_strdup(arena, a->verify_cmd)
        : (app->config.verify != NULL ? app->config.verify
                                     : app->config.test_command);
    /* M503: and WHERE it came from, so the journal can say. An empty --verify is
     * the documented opt-out ("no gate"), and it is a FLAG decision, not an
     * absence -- an operator who disarmed the gate deliberately must not read
     * the same as one who never had one. */
    env->verify_source = (a->verify_cmd != NULL) ? "flag"
        : ((env->verify_cmd != NULL && env->verify_cmd[0] != '\0') ? "config"
                                                                  : "");
    /* M205: make a CLI --verify visible to the run_tests TOOL as well.
     *
     * The resolution above is one-way: --verify reached the envelope and
     * nothing else, while run_tests falls back to config.test_command then
     * config.verify (jc_tool_test.c). So an --auto run launched with
     * `--verify 'python3 -m unittest ...'` had a working verifier while the
     * agent's own run_tests reported "no test command given and none
     * configured" -- and then compensated with run_terminal_command.
     *
     * Measured in the M196 drive: 19 run_terminal_command calls against 1
     * failed run_tests. That lopsidedness is the signature, and it plausibly
     * explains the dogfood log's 1311 run_terminal_command vs 309 run_tests
     * with ZERO exit fields (a run_tests that errors before executing
     * anything leaves exit_status at -1, so the field is correctly omitted).
     *
     * Only fills a GAP: an explicit config verify/testCommand still wins for
     * the tool, because a project may deliberately give the agent a narrower
     * test command than the run's final gate. */
    if (a->verify_cmd != NULL &&
        (app->config.verify == NULL || app->config.verify[0] == '\0') &&
        (app->config.test_command == NULL ||
         app->config.test_command[0] == '\0')) {
        app->config.verify = jc_arena_strdup(arena, a->verify_cmd);
    }
    if (a->budget_tokens != NULL) {
        jc_env_parse_size(a->budget_tokens, &env->budget_tokens);
    }
    if (a->deadline != NULL) {
        jc_env_parse_duration(a->deadline, &env->deadline_secs);
    }
    if (a->max_tool_calls > 0) {
        env->max_tool_calls = a->max_tool_calls;
    }
    if (a->max_reads > 0) { /* M98: per-run read cap */
        env->max_reads = a->max_reads;
    }
    if (a->verify_retries >= 0) {
        env->verify_retries = a->verify_retries; /* 0 disables fix-forward */
    }
    env->retries_left = env->verify_retries;
    if (a->verify_timeout != NULL) {
        jc_env_parse_duration(a->verify_timeout, &env->verify_timeout);
    }
    if (a->no_rollback) {
        env->rollback_on_fail = 0;
    }
    env->strict_scope = a->strict_scope;
    env->verify_baseline = a->verify_baseline;
    /* M343: the CLI declaration wins over the config's; the CLI value was
     * validated at parse (a bad one is a hard usage error -- running with a
     * silently dropped declaration is the trap the flag exists to remove),
     * while a bad CONFIG value warns and is ignored, because hard-failing
     * every session on one typo'd key in a shared config is worse. The
     * declaration is meaningless without a gate to declare, so warn when
     * no verifier resolved. */
    {
        const char *ks = (a->verify_kind != NULL)
                       ? a->verify_kind : app->config.verify_kind;
        if (ks != NULL) {
            int k;
            if (!jc_env_verify_kind_parse(ks, &k)) {
                jc_logf(JC_LOG_WARN, "config verifyKind: unknown kind "
                        "'%s' (use invariant or goal); ignored", ks);
            } else if (env->verify_cmd == NULL) {
                jc_logf(JC_LOG_WARN, "verifyKind is declared but no "
                        "verifier resolves (set verify or testCommand); "
                        "nothing to check the declaration against");
            } else {
                env->verify_kind = k;
            }
        }
    }
    /* M142: CLI tri-state wins over the config bool (default off). */
    env->revert_out_of_scope = (a->revert_oos != 0)
        ? (a->revert_oos > 0) : app->config.revert_out_of_scope;
    env->strict_green = (a->strict_green != 0)
        ? (a->strict_green > 0) : app->config.strict_green;
    /* M431f: same tri-state shape. Default off -- the flag exists so M347's
     * "no per-round nag" decision can be measured against a real run rather
     * than overruled on a preference. */
    env->budget_panel = (a->budget_panel != 0)
        ? (a->budget_panel > 0) : app->config.budget_panel;
    /* M338: the envelope resolves the tri-state the OTHER way -- unset means
     * off for a rollback, which happens mid-failure. An explicit config
     * value or a CLI flag still wins in both directions. */
    env->preserve_discarded = (a->preserve_disc != 0)
        ? (a->preserve_disc > 0)
        : (app->config.preserve_discarded > 0);
    if (a->verify_every > 0) {
        env->verify_every = a->verify_every; /* M81: periodic mid-turn gate */
    }
    if (a->n_edit_scope > 0) {
        int k;
        for (k = 0; k < a->n_edit_scope; k++) {
            char *p = jc_arena_strdup(arena, a->edit_scope[k]);
            jc_vec_push(&env->edit_scope, &p);
        }
    } else {
        jc_size si;
        for (si = 0; si < app->config.edit_scope.len; si++) {
            char *p = *(char **)jc_vec_at(&app->config.edit_scope, si);
            jc_vec_push(&env->edit_scope, &p);
        }
    }

}

/* M444: arm a JOURNAL-LESS envelope for an introspection subcommand, or return 0 if
 * this invocation arms none. The caller must jc_env_free it and clear app->env.
 *
 * `sysmsg` and `context` dispatch before main()'s arming block, so until M444 the
 * envelope-gated sections were simply absent from both -- and `context` sized a prompt
 * the run would not send. Whether that matters is measurable: on a run with
 * `--edit-scope` and a verifier the missing sections are the flight plan, the
 * scope-reach paragraph and the gate contract, which together are the largest
 * env-gated block in the prompt. */
static int intro_arm_env(struct jc_app *app, struct cli_args *a,
                         struct jc_envelope *env, struct jc_arena *arena)
{
    char run_id[40];

    if (a == NULL || !envelope_is_armed(a, app)) {
        return 0;
    }
    jc_uuid_v4(run_id);
    /* NULL journal: showing the prompt must not create a run record. That is the
     * whole reason envelope_arm takes the path as a parameter. */
    envelope_arm(a, app, env, arena, run_id, NULL);
    app->env = env;
    return 1;
}

static int run_sysmsg(struct jc_app *app, const char *const *design_paths,
                      int n_design,
                      struct cli_args *a, struct jc_arena *arena)
{
    char *msg;
    struct jc_envelope env;
    int armed;

    /* M311: the same load `context` does -- one helper, so the prompt and the
     * report that sizes it cannot describe different prompts. */
    load_prompt_assets(app, design_paths, n_design);
    armed = intro_arm_env(app, a, &env, arena);
    msg = jc_sysmsg_build(app);
    printf("%s\n", msg != NULL ? msg : "");
    if (armed) {
        app->env = NULL;
        jc_env_free(&env);
    }
    return 0;
}

/* Append the role names set in `flags` to `buf` (e.g. "chat,embed"). */
static void format_roles(unsigned flags, char *buf, size_t cap)
{
    static const struct { unsigned f; const char *n; } R[] = {
        { JC_ROLE_CHAT, "chat" }, { JC_ROLE_EDIT, "edit" },
        { JC_ROLE_AUTOCOMPLETE, "autocomplete" }, { JC_ROLE_EMBED, "embed" },
        { JC_ROLE_RERANK, "rerank" }, { JC_ROLE_SUMMARIZE, "summarize" },
        { JC_ROLE_APPLY, "apply" }, { JC_ROLE_IMAGE, "image" },
        { JC_ROLE_AUDIO, "audio" }, { JC_ROLE_TRANSCRIBE, "transcribe" }
    };
    size_t i;
    int first = 1;
    buf[0] = '\0';
    for (i = 0; i < sizeof(R) / sizeof(R[0]); i++) {
        if (flags & R[i].f) {
            if (!first) {
                strncat(buf, ",", cap - strlen(buf) - 1);
            }
            strncat(buf, R[i].n, cap - strlen(buf) - 1);
            first = 0;
        }
    }
    if (first) {
        strncat(buf, "-", cap - strlen(buf) - 1);
    }
}

/* `complete [text]`: one-shot autocomplete of `text` (or stdin) using the
 * autocomplete-role model (falling back to the active model). Prints only the
 * continuation. For editors/scripts. Needs config + network, no tool loop. */
static int run_complete(struct jc_app *app, struct cli_args *args)
{
    static const char *sys =
        "You are a code/text autocomplete engine. Continue the user's text "
        "naturally and concisely. Output ONLY the continuation that should "
        "follow their text - no preamble, no explanation, no code fences, no "
        "repetition of the input.";
    const char *prefix = (args->npos >= 2) ? args->pos[1] : NULL;
    const struct jc_model_cfg *m;
    struct jc_provider *prov;
    struct jc_history mini;
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *body = NULL;
    char *resp = NULL;
    long http_status = 0;
    jc_status st;
    int code = 1;

    if (prefix == NULL) {
        prefix = read_all_stdin(app->arena);
    }
    if (prefix == NULL || prefix[0] == '\0') {
        fprintf(stderr, "usage: jichi complete \"text\"  (or pipe text "
                        "on stdin)\n");
        return 2;
    }

    m = jc_app_model_for_role(app, JC_ROLE_AUTOCOMPLETE);
    if (m == NULL) {
        m = &app->config.model;
    }
    prov = jc_provider_create(m);
    if (prov == NULL) {
        fprintf(stderr, "error: could not initialise the model\n");
        return 1;
    }

    jc_history_init(&mini);
    jc_history_add(&mini, JC_ROLE_USER, prefix);
    st = prov->vt->build_request(prov, &mini, sys, NULL, 0, &body);
    if (st != JC_OK) {
        jc_history_free(&mini);
        prov->vt->free(prov);
        fprintf(stderr, "error: could not build the request\n");
        return 1;
    }
    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = prov->vt->endpoint(prov);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = 60;
    req.abort_flag = &app->abort_flag;

    st = jc_http_perform(&req, &http_status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);

    if (st == JC_OK && http_status < 400 && resp != NULL) {
        struct jc_message *reply = jc_history_add(&mini, JC_ROLE_ASSISTANT,
                                                  NULL);
        if (prov->vt->parse_full(prov, resp, reply) == JC_OK &&
            reply->content != NULL) {
            printf("%s\n", reply->content);
            code = 0;
        }
    }
    if (code != 0) {
        fprintf(stderr, "error: completion failed (HTTP %ld)\n", http_status);
    }
    free(resp);
    jc_history_free(&mini);
    prov->vt->free(prov);
    return code;
}

/* Keep the cursor-nearest `JC_FIM_DEFAULT_BUDGET` bytes of one FIM side and copy
 * the window into the arena as a NUL-terminated string. keep_tail=1 for the
 * prefix (last bytes), 0 for the suffix (first bytes). */
static const char *fim_window(struct jc_arena *a, const char *s, jc_size len,
                              int keep_tail)
{
    jc_size klen;
    jc_size kstart = jc_fim_bound(len, JC_FIM_DEFAULT_BUDGET, keep_tail, &klen);
    return jc_arena_strndup(a, s + kstart, klen);
}

/* `fim <file> <offset>` (split the file at the byte offset) or, with no
 * positionals, a JSON object {"prefix":..,"suffix":..} on stdin: fill-in-the-
 * middle completion using the autocomplete-role model (fallback active). Prints
 * only the inserted middle. For editor "tab autocomplete". Needs config +
 * network, no tool loop. */
static int run_fim(struct jc_app *app, struct cli_args *args)
{
    const char *prefix = NULL;
    const char *suffix = NULL;
    cJSON *json = NULL;
    struct jc_sb user;
    const struct jc_model_cfg *m;
    struct jc_provider *prov;
    struct jc_history mini;
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *body = NULL;
    char *resp = NULL;
    long http_status = 0;
    jc_status st;
    int code = 1;

    if (args->npos >= 3) {
        char *content = NULL;
        jc_size flen = 0;
        long off;
        if (jc_read_file(args->pos[1], &content, &flen, app->arena) != JC_OK) {
            fprintf(stderr, "error: cannot read '%s'\n", args->pos[1]);
            return 1;
        }
        off = strtol(args->pos[2], NULL, 10);
        if (off < 0) {
            off = 0;
        }
        if ((jc_size)off > flen) {
            off = (long)flen;
        }
        prefix = fim_window(app->arena, content, (jc_size)off, 1);
        suffix = fim_window(app->arena, content + off, flen - (jc_size)off, 0);
    } else {
        char *in = read_all_stdin(app->arena);
        if (in == NULL || in[0] == '\0') {
            fprintf(stderr, "usage: jichi fim <file> <byte-offset>\n"
                            "   or: echo '{\"prefix\":\"..\",\"suffix\":\"..\"}'"
                            " | jichi fim\n");
            return 2;
        }
        json = jc_json_parse(in);
        if (json == NULL) {
            fprintf(stderr, "error: stdin is not valid JSON "
                            "{\"prefix\":..,\"suffix\":..}\n");
            return 2;
        }
        {
            const char *p = jc_json_get_str(json, "prefix", "");
            const char *s = jc_json_get_str(json, "suffix", "");
            prefix = fim_window(app->arena, p, (jc_size)strlen(p), 1);
            suffix = fim_window(app->arena, s, (jc_size)strlen(s), 0);
        }
    }

    m = jc_app_model_for_role(app, JC_ROLE_AUTOCOMPLETE);
    if (m == NULL) {
        m = &app->config.model;
    }
    prov = jc_provider_create(m);
    if (prov == NULL) {
        fprintf(stderr, "error: could not initialise the model\n");
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return 1;
    }

    jc_sb_init(&user);
    jc_fim_build_user(prefix, suffix, &user);

    jc_history_init(&mini);
    jc_history_add(&mini, JC_ROLE_USER, user.data);
    st = prov->vt->build_request(prov, &mini, JC_FIM_SYSTEM, NULL, 0, &body);
    jc_sb_free(&user);
    if (json != NULL) {
        cJSON_Delete(json);
    }
    if (st != JC_OK) {
        jc_history_free(&mini);
        prov->vt->free(prov);
        fprintf(stderr, "error: could not build the request\n");
        return 1;
    }
    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = prov->vt->endpoint(prov);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = 60;
    req.abort_flag = &app->abort_flag;

    st = jc_http_perform(&req, &http_status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);

    if (st == JC_OK && http_status < 400 && resp != NULL) {
        struct jc_message *reply = jc_history_add(&mini, JC_ROLE_ASSISTANT,
                                                  NULL);
        if (prov->vt->parse_full(prov, resp, reply) == JC_OK &&
            reply->content != NULL) {
            struct jc_sb clean;
            jc_sb_init(&clean);
            jc_fim_strip_fences(reply->content, &clean);
            printf("%s\n", clean.data != NULL ? clean.data : "");
            jc_sb_free(&clean);
            code = 0;
        }
    }
    if (code != 0) {
        fprintf(stderr, "error: fim failed (HTTP %ld)\n", http_status);
    }
    free(resp);
    jc_history_free(&mini);
    prov->vt->free(prov);
    return code;
}

/* `models`: list configured models with roles, fallback, and a live
 * reachability probe of each one's server. Needs config + curl, no provider. */
/* Is `cmd` an executable we can find (absolute/relative path, or on $PATH)? */
static int cmd_on_path(const char *cmd)
{
    const char *path;
    char buf[2048];

    if (cmd == NULL || cmd[0] == '\0') {
        return 0;
    }
    if (strchr(cmd, '/') != NULL) {
        return access(cmd, X_OK) == 0;
    }
    path = getenv("PATH");
    if (path == NULL) {
        return 0;
    }
    while (*path != '\0') {
        const char *colon = strchr(path, ':');
        jc_size len = colon != NULL ? (jc_size)(colon - path)
                                    : (jc_size)strlen(path);
        if (len > 0 && len + 1 + strlen(cmd) + 1 < sizeof(buf)) {
            memcpy(buf, path, len);
            buf[len] = '/';
            jc_snprintf(buf + len + 1, sizeof(buf) - len - 1, "%s", cmd);
            if (access(buf, X_OK) == 0) {
                return 1;
            }
        }
        if (colon == NULL) {
            break;
        }
        path = colon + 1;
    }
    return 0;
}

/* Classify one `model:` selector (an agent profile's, a command's, or a routing
 * tier's) and append a human-readable finding to `bad` (unresolvable -- a FAIL)
 * or `warn` (resolvable but probably not what was meant -- a WARN), bumping the
 * matching counter. Absent/empty selectors and clean ones append nothing (M284).
 *
 * The point is timing, not novelty: all three selector kinds already resolve at
 * use time, so a typo used to reach the user as "error: no model matches 'x'"
 * from inside a spawned subagent -- mid-run, in an --auto turn that then spends
 * budget recovering from it. Here it costs one line of `doctor` output. */
static void doctor_check_selector(const struct jc_config *cfg,
                                  const char *kind, const char *owner,
                                  const char *sel, struct jc_sb *bad,
                                  struct jc_sb *warn, int *nbad, int *nwarn)
{
    enum jc_selector_status st;
    int nmatch = 0;
    char buf[320];

    if (sel == NULL || sel[0] == '\0') {
        return;
    }
    st = jc_config_selector_check(cfg, sel, &nmatch);
    if (st == JC_SEL_OK) {
        return;
    }
    if (st == JC_SEL_NONE) {
        jc_snprintf(buf, sizeof(buf),
                    "%s%s%s: model '%s' matches no configured model or role",
                    kind, (owner != NULL) ? " " : "",
                    (owner != NULL) ? owner : "", sel);
        if (bad->len > 0) {
            jc_sb_append(bad, "; ");
        }
        jc_sb_append(bad, buf);
        (*nbad)++;
        return;
    }
    if (st == JC_SEL_AMBIGUOUS) {
        int win = jc_config_find_model(cfg, sel);
        const struct jc_model_cfg *m = (win >= 0)
            ? jc_config_model_at((struct jc_config *)cfg, win) : NULL;
        const char *wname = (m != NULL && m->name != NULL) ? m->name : "?";
        jc_snprintf(buf, sizeof(buf),
                    "%s%s%s: model '%s' matches %d models; '%s' wins by "
                    "position -- name it exactly",
                    kind, (owner != NULL) ? " " : "",
                    (owner != NULL) ? owner : "", sel, nmatch, wname);
    } else { /* JC_SEL_ROLE_EMPTY */
        jc_snprintf(buf, sizeof(buf),
                    "%s%s%s: model '%s' names a role no configured model "
                    "declares",
                    kind, (owner != NULL) ? " " : "",
                    (owner != NULL) ? owner : "", sel);
    }
    if (warn->len > 0) {
        jc_sb_append(warn, "; ");
    }
    jc_sb_append(warn, buf);
    (*nwarn)++;
}

/* Can a fence entry named `name` ever match a call (M285)? Returns 1 when yes,
 * 0 when the entry is dead -- and on 0, *suggest gets the name to use instead
 * when one can be inferred, else NULL.
 *
 * The subtlety that makes this worth checking: resolution and fencing use
 * DIFFERENT matching. jc_tool_registry_find is alias-aware (jc_tool_canonical_name,
 * M219: todo_write -> todowrite, create_file -> write_file), but jc_tool_allowed
 * is exact strcmp. So an alias in a fence is dead weight even though the same
 * string works fine as a model's call -- which is precisely the sort of asymmetry
 * nobody notices by reading. jc_tool_semantic_alias supplies the hint for a
 * hint-only guess (grep -> search_code) that never resolves at all.
 *
 * Accepted without proof: anything MCP-namespaced (<server>__<tool>), since
 * confirming it would mean connecting every server, and a false "no such tool"
 * is worse than a missed one. */
static int doctor_fence_entry_ok(const struct jc_app *app, const char *name,
                                 const char **suggest)
{
    jc_size i;
    const char *canon;

    *suggest = NULL;
    if (name == NULL || name[0] == '\0') {
        return 1; /* nothing to say about an empty entry */
    }
    if (jc_tool_name_known(name)) {
        return 1;
    }
    if (strstr(name, "__") != NULL) {
        return 1; /* an MCP tool we cannot confirm without connecting */
    }
    for (i = 0; i < app->config.user_tools.len; i++) {
        const struct jc_user_tool_cfg *ut =
            (const struct jc_user_tool_cfg *)jc_vec_at(
                (struct jc_vec *)&app->config.user_tools, i);
        if (ut->name != NULL && strcmp(ut->name, name) == 0) {
            return 1;
        }
    }
    /* A silent alias resolves as a CALL but not in a fence: name the real tool. */
    canon = jc_tool_canonical_name(name);
    if (canon != NULL && strcmp(canon, name) != 0 && jc_tool_name_known(canon)) {
        *suggest = canon;
        return 0;
    }
    /* A hint-only guess never resolves; still, say what was probably meant. */
    *suggest = jc_tool_semantic_alias(name);
    return 0;
}

/* Accumulator for the M285 fence lint. The two defect classes are counted and
 * reported separately because their remedies are opposite: an unknown name is
 * fixed in the asset, a dropped one by raising toolProfile (or trimming the
 * fence). Samples are bounded -- zigodot under `core` produces 43 findings, and
 * a single unreadable line is a check nobody acts on. */
#define DOCTOR_FENCE_SAMPLES 4

struct doctor_fence_stats {
    int n_unknown;
    int n_dropped;
    int shown_unknown;
    int shown_dropped;
    struct jc_sb ex_unknown;
    struct jc_sb ex_dropped;
};

static void fence_sample(struct jc_sb *sb, int *shown, const char *kind,
                         const char *owner, const char *tool,
                         const char *suggest)
{
    char buf[256];
    if (*shown >= DOCTOR_FENCE_SAMPLES) {
        return;
    }
    if (suggest != NULL) {
        jc_snprintf(buf, sizeof(buf), "%s%s %s: '%s' (use '%s')",
                    (*shown > 0) ? ", " : "", kind,
                    owner != NULL ? owner : "?", tool, suggest);
    } else {
        jc_snprintf(buf, sizeof(buf), "%s%s %s: '%s'", (*shown > 0) ? ", " : "",
                    kind, owner != NULL ? owner : "?", tool);
    }
    jc_sb_append(sb, buf);
    (*shown)++;
}

/* Lint one asset's `tools:` fence (M285). Two distinct defects, both silent:
 *
 *   unknown  -- the name is not a tool at all (a typo, or a foreign vocabulary:
 *               zigodot's telemetry shows a model reaching for Claude Code's
 *               `grep`/`glob`/`todo_write`). The fence quietly narrows and the
 *               model can never call it.
 *   dropped  -- the name IS a tool, but the resolved tool profile is `core`,
 *               which never advertises it. This is what made `format_file` fail
 *               0/3 in zigodot while its profiles declared the LSP tools.
 *
 * Both are warnings, not failures: unlike an unresolvable model selector (M284,
 * which aborts the subagent with a tool error), a bad fence entry leaves a
 * working-but-degraded profile.
 */
static void doctor_check_fence(const struct jc_app *app, const char *kind,
                              const char *owner, const struct jc_vec *tools,
                              int core_profile,
                              struct doctor_fence_stats *st)
{
    jc_size i;

    if (tools == NULL) {
        return;
    }
    for (i = 0; i < tools->len; i++) {
        const char *tn = *(char **)jc_vec_at((struct jc_vec *)tools, i);
        const char *suggest = NULL;
        if (tn == NULL || tn[0] == '\0') {
            continue;
        }
        if (!doctor_fence_entry_ok(app, tn, &suggest)) {
            st->n_unknown++;
            fence_sample(&st->ex_unknown, &st->shown_unknown, kind, owner, tn,
                         suggest);
        } else if (core_profile && jc_tool_name_known(tn) &&
                   !jc_tool_is_core(tn)) {
            st->n_dropped++;
            fence_sample(&st->ex_dropped, &st->shown_dropped, kind, owner, tn,
                         NULL);
        }
    }
}

/* Validate every asset under <base>/<sub>, appending "<sub>/<rel>: <issue>"
 * (semicolon-joined) to `out` for each problem. `skill_layout` selects the
 * <sub>/<name>/SKILL.md layout (skills) vs the flat one-.md-per-asset layout
 * (agents/commands). Returns the number of issues found. Read-only; reuses the
 * pure jc_assetval checks. */
static int doctor_validate_assets(struct jc_app *app, const char *base,
                                  const char *sub, int kind, int skill_layout,
                                  struct jc_sb *out)
{
    char dir[1200];
    struct jc_vec names;
    jc_size i;
    int n = 0;

    jc_snprintf(dir, sizeof(dir), "%s/%s", base, sub);
    jc_vec_init(&names, sizeof(char *));
    jc_list_dir(dir, &names, app->arena);
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char path[1400];
        char *raw = NULL;
        struct jc_md_doc doc;
        struct jc_vec iss;
        jc_size j;
        jc_size nl = (jc_size)strlen(nm);

        if (skill_layout) {
            jc_snprintf(path, sizeof(path), "%s/%s/SKILL.md", dir, nm);
        } else {
            if (nl < 4 || strcmp(nm + nl - 3, ".md") != 0) {
                continue; /* not a markdown asset */
            }
            jc_snprintf(path, sizeof(path), "%s/%s", dir, nm);
        }
        if (!jc_file_exists(path) ||
            jc_read_file(path, &raw, NULL, app->arena) != JC_OK) {
            continue;
        }

        memset(&doc, 0, sizeof(doc));
        jc_md_parse(raw, app->arena, &doc);
        jc_vec_init(&iss, sizeof(char *));
        jc_assetval_check(kind, raw, doc.front, app->arena, &iss);

        /* A custom command shadowed by a built-in slash command never runs. */
        if (kind == JC_ASSET_COMMAND && nl > 3) {
            char base_name[160];
            jc_snprintf(base_name, sizeof(base_name), "%.*s",
                        (int)(nl - 3), nm);
            if (jc_assetval_is_builtin_command(base_name)) {
                char b[200];
                char *d;
                jc_snprintf(b, sizeof(b), "shadowed by the built-in /%s",
                            base_name);
                d = jc_arena_strdup(app->arena, b);
                if (d != NULL) {
                    jc_vec_push(&iss, &d);
                }
            }
        }

        for (j = 0; j < iss.len; j++) {
            const char *msg = *(char **)jc_vec_at(&iss, j);
            if (out->len > 0) {
                jc_sb_append(out, "; ");
            }
            if (skill_layout) {
                jc_sb_append_fmt(out, "%s/%s/SKILL.md: %s", sub, nm, msg);
            } else {
                jc_sb_append_fmt(out, "%s/%s: %s", sub, nm, msg);
            }
            n++;
        }
        jc_md_free(&doc);
        jc_vec_free(&iss);
    }
    jc_vec_free(&names);
    return n;
}

/* `doctor`: run setup health checks and print a pass/warn/fail checklist.
 * Exit code is 1 if any check FAILED, else 0. Needs config + network (for
 * reachability and MCP connect). */
/* `assign <spec.md>` -> render a machine-checkable assignment spec for its
 * audience (M103). Offline; no model. */
/* Warn (once, to stderr) when a spec's frontmatter was opened with `---` but
 * never closed -- a common authoring slip that makes jc_assign_parse silently
 * swallow the whole file as the task body, dropping verify/hints/points. Names
 * the file + the fix so the failure isn't opaque. Returns 1 when it warned. */
static int assign_warn_unterminated(const char *path, const char *text)
{
    if (jc_md_frontmatter_unterminated(text)) {
        fprintf(stderr, "warning: %s: frontmatter opened with '---' but never "
                "closed -- add a closing '---' line; verify/hints/points were "
                "ignored.\n", path);
        return 1;
    }
    return 0;
}

static int run_assign(struct cli_args *args, struct jc_arena *arena)
{
    const char *path = (args->npos > 1) ? args->pos[1] : NULL;
    char *text;
    struct jc_assign_spec spec;

    if (path == NULL) {
        fprintf(stderr, "usage: assign <spec.md>\n");
        return 2;
    }
    if (jc_read_file(path, &text, NULL, arena) != JC_OK) {
        fprintf(stderr, "assign: could not read '%s'\n", path);
        return 1;
    }
    if (jc_assign_parse(text, &spec, arena) != JC_OK) {
        fprintf(stderr, "assign: '%s' has no task body\n", path);
        return 1;
    }
    assign_warn_unterminated(path, text);
    printf("%s", jc_assign_render(&spec, arena));
    return 0;
}

/* `grade <spec.md>` -> run the spec's verify command and score it (M103).
 * Offline; runs the command via /bin/sh, no model. Exit 0 iff it passed. */
/* `hint <spec.md> [N]` -> print rung N (1-based, default 1) of the spec's
 * graded hint ladder. Stateless on purpose: the ladder's session state lives in
 * an interactive session (/hint) or an `attempt`; here the learner (or an
 * editor integration -- emacs/vim/nano and the web bridge all ride headless)
 * names the rung, so nothing needs a state file. M173b. */
static int run_hint(struct cli_args *args, struct jc_arena *arena)
{
    const char *path = (args->npos > 1) ? args->pos[1] : NULL;
    int n = (args->npos > 2) ? atoi(args->pos[2]) : 1;
    char *text;
    struct jc_assign_spec spec;

    if (path == NULL) {
        fprintf(stderr, "usage: hint <spec.md> [N]\n");
        return 2;
    }
    if (jc_read_file(path, &text, NULL, arena) != JC_OK) {
        fprintf(stderr, "hint: could not read '%s'\n", path);
        return 1;
    }
    if (jc_assign_parse(text, &spec, arena) != JC_OK) {
        fprintf(stderr, "hint: '%s' has no task body\n", path);
        return 1;
    }
    /* M409: a ladder shorter than the file must SAY so. jc_yaml's subset
     * cannot read some hint forms (an unquoted value containing `: `, a
     * trailing comment after the closing quote); M289 rightly skips them so
     * no rung is ever empty -- but "the ladder has 2 rungs" then reads as a
     * fact about the assignment. 64 of this repo's own 80 ladders were
     * silently short before this note existed. */
    if (spec.hints_skipped > 0) {
        fprintf(stderr, "note: %d hint line%s in the file could not be read "
                "(this YAML subset needs the whole hint plain or fully "
                "quoted; a bare `: ` inside an unquoted value splits it) -- "
                "the ladder below is missing %s\n",
                spec.hints_skipped, spec.hints_skipped == 1 ? "" : "s",
                spec.hints_skipped == 1 ? "it" : "them");
    }
    if (spec.nhints <= 0 || spec.hints == NULL) {
        printf("(this assignment carries no hints)\n");
        return 0;
    }
    if (n < 1 || n > spec.nhints) {
        fprintf(stderr, "hint: N must be 1..%d (the ladder has %d rung%s)\n",
                spec.nhints, spec.nhints, spec.nhints == 1 ? "" : "s");
        return 2;
    }
    printf("Hint %d of %d:\n\n%s\n", n, spec.nhints, spec.hints[n - 1]);
    if (n < spec.nhints) {
        printf("\n(%d more if you stay stuck: hint %s %d)\n",
               spec.nhints - n, path, n + 1);
    }
    /* M502: record the pull. The 74 shipped specs, the scaffold glossary and
     * CURRICULUM.md have promised "free, and recorded" since M174, and nothing
     * wrote it -- so a teacher could not see that a learner needed all three
     * rungs, which is the diagnostic half of a hint ladder. It lands in a
     * SEPARATE sink (.jichi/hints.jsonl), never in progress.jsonl, because
     * every reader there counts a line as a graded attempt; that separation is
     * what keeps "never penalised" true by construction rather than by
     * everyone remembering. A write failure is not worth failing the command
     * over: the learner still got their hint. */
    if (jc_progress_hint_append(".", path, n) != JC_OK) {
        jc_logf(JC_LOG_DEBUG, "hint: could not append to .jichi/hints.jsonl");
    }
    return 0;
}

/* The grading mechanic itself lives in src/util/jc_gradecore.c since M614 --
 * the TUI's /grade was a FOURTH implementation (no M502 guard, recorded
 * unconditionally), and unifying it required a TU main.c and jc_tui.c share.
 * See jc_gradecore.h for the M529/M502 reasoning that moved with it. */

static int run_grade(struct cli_args *args, struct jc_arena *arena)
{
    const char *path = (args->npos > 1) ? args->pos[1] : NULL;
    struct jc_grade_out g;
    struct jc_assign_spec spec;
    struct jc_assign_result res;
    int rc;

    if (path == NULL) {
        fprintf(stderr, "usage: grade <spec.md>\n");
        return 2;
    }
    if (args->expect_fail && args->record) {
        /* M412: a red-proof is an AUTHORING check on the untouched tree, not
         * a learner grade -- recording its inverted verdict would poison the
         * progress file's meaning. Refused, not ignored (the M294 rule). */
        fprintf(stderr, "grade: --expect-fail is an authoring check and "
                "cannot be combined with --record\n");
        return 2;
    }
    jc_grade_core(path, arena, &g);
    switch (g.fail) {
    case JC_GRADE_UNREADABLE:
        fprintf(stderr, "grade: could not read '%s'\n", path);
        return 1;
    case JC_GRADE_NO_TASK:
        fprintf(stderr, "grade: '%s' has no task body\n", path);
        jc_grade_out_free(&g);
        return 1;
    case JC_GRADE_NO_VERIFY:
        fprintf(stderr, "grade: '%s' has no `verify` command to run\n", path);
        assign_warn_unterminated(path, g.text);
        return 2;
    case JC_GRADE_CANNOT_RUN:
        fprintf(stderr,
            "grade: the verify command cannot run from here -- '%s' does "
            "not exist relative to this directory.\n"
            "  verify: %s\n"
            "This is NOT a grade. Run `grade` from the directory the "
            "spec's paths are relative to (for the shipped curriculum, the "
            "repository root).\n",
            g.prog, g.spec.verify);
        return 2;
    default:
        break;
    }
    spec = g.spec;
    res = g.res;
    rc = g.verify_exit;

    /* M412: --expect-fail is the two-sided proof's RED half, for any project.
     * jichi's own curriculum proves every grader red-first in CI; an
     * assignment authored elsewhere inherits no such proof, and the first one
     * authored in anger shipped with a verify that was already green -- PASS
     * at 100% with the target function still panicking. Run right after
     * authoring, on the untouched tree: a FAILING verify is the success case,
     * an already-green one is a HOLLOW gate that grades nothing. */
    if (args->expect_fail) {
        if (args->output_json == 1) {
            cJSON *o = cJSON_CreateObject();
            char *s;
            cJSON_AddStringToObject(o, "spec", path);
            if (spec.title != NULL) {
                cJSON_AddStringToObject(o, "title", spec.title);
            }
            cJSON_AddBoolToObject(o, "expect_fail", 1);
            cJSON_AddBoolToObject(o, "hollow", res.passed ? 1 : 0);
            cJSON_AddNumberToObject(o, "verify_exit", (double)rc);
            s = cJSON_PrintUnformatted(o);
            if (s != NULL) {
                printf("%s\n", s);
                free(s);
            }
            cJSON_Delete(o);
        } else if (!res.passed) {
            printf("%s: RED as expected -- the gate can fail on this tree\n",
                   spec.title != NULL ? spec.title : path);
            printf("  verify: %s (exit %d)\n", spec.verify, rc);
        } else {
            printf("%s: HOLLOW -- verify is already green, so this gate "
                   "cannot fail\n", spec.title != NULL ? spec.title : path);
            printf("  verify: %s (exit 0)\n", spec.verify);
            printf("  a gate that cannot fail grades nothing: add a failing "
                   "test the work will\n  turn green first (see "
                   "docs/TEST_INTEGRITY.md)\n");
        }
        jc_grade_out_free(&g);
        return res.passed ? 1 : 0;
    }

    if (args->output_json == 1) {
        /* One object, machine-readable -- the gradebook row an instructor's
         * batch loop collects (M173b). */
        cJSON *o = cJSON_CreateObject();
        char *s;
        cJSON_AddStringToObject(o, "spec", path);
        if (spec.title != NULL) {
            cJSON_AddStringToObject(o, "title", spec.title);
        }
        cJSON_AddBoolToObject(o, "passed", res.passed);
        cJSON_AddNumberToObject(o, "pct", (double)res.pct);
        cJSON_AddNumberToObject(o, "tests_run", (double)res.tests_run);
        cJSON_AddNumberToObject(o, "tests_failed", (double)res.tests_failed);
        cJSON_AddNumberToObject(o, "verify_exit", (double)rc);
        s = cJSON_PrintUnformatted(o);
        if (s != NULL) {
            printf("%s\n", s);
            free(s);
        }
        cJSON_Delete(o);
    } else {
        printf("%s: %s\n", spec.title != NULL ? spec.title : path,
               res.passed ? "PASS" : "FAIL");
        printf("  verify: %s (exit %d)\n", spec.verify, rc);
        if (res.tests_run > 0) {
            printf("  tests: %d run, %d failed  (%d%%)\n", res.tests_run,
                   res.tests_failed, res.pct);
        } else {
            printf("  score: %d%%\n", res.pct);
        }
        if (!res.passed && g.miss_dir[0] != '\0') {
            /* M617: the wrong-directory note for the no-slash-program tier. */
            printf("  note: the verify references %s/, which does not exist "
                   "from here.\n  If this FAIL surprises you, run `grade` "
                   "from the repository root\n  (a missing directory can "
                   "also be part of the task).\n", g.miss_dir);
        }
    }
    /* --record: append one JSONL line to the per-workspace progress file, so
     * "am I ready for the next module?" is a command over a file, not a
     * feeling. Append-only; the learner owns and may edit it. The line format
     * is owned by jc_progress_append (M174); headless grading is stateless so
     * no hints count is known (-1 omits the field). */
    if (args->record) {
        if (jc_progress_append(".", path, res.passed, res.pct, res.tests_run,
                               res.tests_failed, -1) != JC_OK) {
            fprintf(stderr, "grade: could not append to "
                    ".jichi/progress.jsonl\n");
        }
    }
    jc_grade_out_free(&g);
    return res.passed ? 0 : 1;
}

/* Grade one spec file: 1 pass, 0 fail, -1 ungradeable (no verify command). */
static int improve_grade_one(const char *path, struct jc_arena *arena)
{
    struct jc_grade_out g;
    jc_grade_core(path, arena, &g);
    jc_grade_out_free(&g);
    /* M529: a refusal is not a failure. This path used to lack the M502
     * reachability guard entirely, so a spec whose verify script could not be
     * opened from here was scored FAIL and dragged the pass-rate down; it is
     * now -1 (ungradeable) like the other refusals, which is what "this is NOT
     * a grade" has to mean for the metric as well as for the message. */
    if (g.fail != JC_GRADE_NONE) {
        return -1;
    }
    return g.res.passed ? 1 : 0;
}

/* `improve [specs-dir]` (M109): the synthesis-loop entry point. Reflect over
 * telemetry, grade a suite of assignment specs for a pass-rate, track it over
 * time, and write a PROPOSE-ONLY report. Offline; the live sandboxed rehearsal
 * (agent attempt per failing spec) is the documented next slice. */
static int run_improve(struct jc_arena *arena, const char *specs_dir)
{
    const char *dir = (specs_dir != NULL) ? specs_dir : ".jichi/assignments";
    struct jc_vec names;
    struct jc_sb findings;
    struct jc_sb doc;
    struct jc_sb hist;
    char tlpath[1300];
    char hpath[1200];
    char rpath[1200];
    char idir[1024];
    char *htext = NULL;
    int passed = 0;
    int total = 0;
    int cur;
    int prev;
    jc_size i;

    /* Reflect (best-effort; a missing telemetry log is not fatal here). */
    jc_sb_init(&findings);
    {
        char wsb[1024];
        if (jc_app_pick_telemetry_log(reader_workspace(NULL, wsb, sizeof(wsb)),
                                      arena, tlpath, sizeof(tlpath)) == 0) {
        char *tl;
        if (jc_read_file(tlpath, &tl, NULL, arena) == JC_OK) {
            jc_learn_analyze_render(arena, tl, NULL, &findings);
        }
        }
    }

    /* Grade the spec suite. */
    jc_vec_init(&names, sizeof(char *));
    jc_list_dir(dir, &names, arena);
    jc_sb_init(&doc);
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char spath[1200];
        jc_size ln = strlen(nm);
        int r;
        if (ln < 4 || strcmp(nm + ln - 3, ".md") != 0) {
            continue;
        }
        jc_snprintf(spath, sizeof(spath), "%s/%s", dir, nm);
        r = improve_grade_one(spath, arena);
        if (r < 0) {
            continue; /* not a gradeable spec */
        }
        total++;
        if (r == 1) {
            passed++;
        }
        jc_sb_append_fmt(&doc, "- %s: %s\n", nm, r == 1 ? "PASS" : "FAIL");
    }
    jc_vec_free(&names);

    cur = (total > 0) ? (passed * 100 / total) : -1;

    /* Track the pass-rate over time. */
    jc_snprintf(idir, sizeof(idir), "%s/.jichi.d/improve",
                jc_home_dir());
    jc_mkdir_p(idir);
    jc_snprintf(hpath, sizeof(hpath), "%s/history.jsonl", idir);
    prev = -1;
    if (jc_read_file(hpath, &htext, NULL, arena) == JC_OK && htext != NULL) {
        prev = jc_improve_last_pct(htext);
    }
    if (cur >= 0) {
        jc_sb_init(&hist);
        if (htext != NULL) {
            jc_sb_append(&hist, htext);
        }
        jc_sb_append_fmt(&hist, "{\"pct\":%d,\"passed\":%d,\"total\":%d,"
                               "\"t\":%ld}\n", cur, passed, total,
                         (long)jc_now_seconds());
        jc_write_file(hpath, hist.data != NULL ? hist.data : "",
                      hist.data != NULL ? hist.len : 0);
        jc_sb_free(&hist);
    }

    /* Propose-only report. */
    {
        struct jc_sb r;
        jc_sb_init(&r);
        jc_sb_append(&r, "# Improve cycle (synthesis loop)\n\n");
        jc_sb_append(&r, "_Propose-only. Baseline grading + reflection; nothing "
                         "was applied._\n\n");
        if (cur >= 0) {
            jc_sb_append_fmt(&r, "## Pass-rate: %d%% (%d/%d) -- %s",
                             cur, passed, total,
                             jc_improve_trend_word(prev, cur));
            if (prev >= 0) {
                jc_sb_append_fmt(&r, " (was %d%%)", prev);
            }
            jc_sb_append(&r, "\n\n");
            jc_sb_append(&r, doc.data != NULL ? doc.data : "");
        } else {
            jc_sb_append_fmt(&r, "No gradeable specs under `%s` "
                             "(need `verify:` frontmatter).\n", dir);
        }
        jc_sb_append(&r, "\n## Reflection\n\n");
        jc_sb_append(&r, (findings.data != NULL && findings.len > 0)
                     ? findings.data : "(no telemetry to reflect on)\n");
        jc_sb_append(&r, "\n## Next actions\n\n");
        jc_sb_append(&r, "- Run `/learn` to draft lessons from the reflection, "
                         "then `learn apply` after review.\n");
        jc_sb_append(&r, "- For each FAIL above, have the agent attempt the "
                         "spec (`-p` / daemon) and re-run `grade`.\n");
        jc_snprintf(rpath, sizeof(rpath), "%s/report-%ld.md", idir,
                    (long)jc_now_seconds());
        jc_write_file(rpath, r.data != NULL ? r.data : "",
                      r.data != NULL ? r.len : 0);
        jc_sb_free(&r);

        if (cur >= 0) {
            fprintf(stderr, "improve: pass-rate %d%% (%d/%d), %s; wrote %s\n",
                    cur, passed, total, jc_improve_trend_word(prev, cur),
                    rpath);
        } else {
            fprintf(stderr, "improve: no gradeable specs under %s; wrote %s\n",
                    dir, rpath);
        }
    }
    jc_sb_free(&findings);
    jc_sb_free(&doc);
    return 0;
}

/* Run one silent agent turn on `task` against a fresh throwaway history
 * (AUTO mode assumed set by the caller; zeroed callbacks => no stdout). */
static void improve_attempt_turn(struct jc_app *app, const char *task)
{
    struct jc_history hist;
    struct jc_agent_callbacks q;
    memset(&q, 0, sizeof(q));
    jc_history_init(&hist);
    jc_history_add(&hist, JC_ROLE_USER, task);
    jc_agent_run_turn(app, &hist, &q);
    jc_history_free(&hist);
}

/* Run `verify` via /bin/sh in the current directory and score it: 1 pass,
 * 0 fail, -1 no command. */
static int improve_run_verify(const char *verify)
{
    struct jc_sb out;
    struct jc_test_report rep;
    struct jc_assign_result res;
    char *argv[4];
    int rc;

    if (verify == NULL || verify[0] == '\0') {
        return -1;
    }
    jc_sb_init(&out);
    argv[0] = (char *)jc_shell_path(); argv[1] = "-c"; argv[2] = (char *)verify; argv[3] = 0;
    rc = jc_proc_capture(argv, NULL, NULL, &out, 262144, 600, NULL);
    jc_test_report_init(&rep);
    jc_testparse(out.data, &rep);
    jc_assign_score(&rep, rc == 0, &res);
    jc_test_report_free(&rep);
    jc_sb_free(&out);
    return res.passed;
}

/* `improve --attempt [specs-dir]` (M109 live): the loop that measurably closes,
 * SAFELY. Each failing spec is attempted by the agent in an isolated git
 * worktree materialised from a checkpoint of the current tree, then graded
 * there and the worktree discarded. The user's working tree is NEVER reset or
 * cleaned (ANECDOTES #10) -- the worst case is "couldn't sandbox", never data
 * loss. Needs snapshots (git) + a model. */
static int run_improve_attempt(struct jc_app *app, const char *specs_dir)
{
    const char *dir = (specs_dir != NULL) ? specs_dir : ".jichi/assignments";
    struct jc_snapshot_mgr m;
    struct jc_arena *a;
    struct jc_vec names;
    struct jc_sb doc;
    char base[128];
    char wtbase[1100];
    char orig_cwd[1024];
    enum jc_agent_mode am;
    const char *c0;
    int saved_mode;
    int total = 0;
    int base_passed = 0;
    int attempted = 0;
    int fixed = 0;
    int cap = 5;
    int cur;
    int wi = 0;
    jc_size i;
    char idir[1024];
    char rpath[1200];

    jc_snapshot_manager_init(&m, app);
    if (!jc_snapshot_available(&m)) {
        fprintf(stderr, "improve --attempt needs snapshots (git + "
                "\"snapshots\": true) to sandbox attempts in a worktree; run "
                "plain `improve`.\n");
        jc_snapshot_manager_shutdown(&m);
        return 1;
    }
    if (getcwd(orig_cwd, sizeof(orig_cwd)) == NULL) {
        orig_cwd[0] = '\0';
    }
    /* One checkpoint of the current tree; every attempt's worktree is
     * materialised from it. Copying the SHA (parallel does the same) avoids a
     * dangle when the vector reallocs. */
    jc_snapshot_take(&m, "improve base");
    c0 = jc_snapshot_commit(&m, jc_snapshot_count(&m) - 1);
    if (c0 == NULL) {
        fprintf(stderr, "improve --attempt: could not checkpoint the tree.\n");
        jc_snapshot_manager_shutdown(&m);
        return 1;
    }
    jc_snprintf(base, sizeof(base), "%s", c0);
    jc_snprintf(wtbase, sizeof(wtbase), "%s/.jichi.d/worktrees/imp-%ld",
                jc_home_dir(), (long)getpid());
    jc_mkdir_p(wtbase);

    a = jc_arena_new(0);
    saved_mode = app->mode;
    if (jc_agent_mode_parse("auto", &am)) {
        jc_app_set_mode(app, (int)am);
    }

    jc_vec_init(&names, sizeof(char *));
    jc_list_dir(dir, &names, a);
    jc_sb_init(&doc);
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char spath[1200];
        char *text;
        struct jc_assign_spec spec;
        int baseline;
        jc_size ln = strlen(nm);

        if (app->abort_flag) {
            break; /* Ctrl-C: stop the suite, don't grind through the rest */
        }
        if (ln < 4 || strcmp(nm + ln - 3, ".md") != 0) {
            continue;
        }
        jc_snprintf(spath, sizeof(spath), "%s/%s", dir, nm);
        if (jc_read_file(spath, &text, NULL, a) != JC_OK ||
            jc_assign_parse(text, &spec, a) != JC_OK ||
            spec.verify == NULL || spec.verify[0] == '\0') {
            continue;
        }
        baseline = improve_run_verify(spec.verify); /* in the real tree (RO) */
        total++;
        if (baseline == 1) {
            base_passed++;
            jc_sb_append_fmt(&doc, "- %s: PASS (baseline)\n", nm);
            continue;
        }
        if (attempted >= cap) {
            jc_sb_append_fmt(&doc, "- %s: FAIL (not attempted; cap %d)\n",
                             nm, cap);
            continue;
        }
        /* Sandbox the attempt in a fresh worktree off the base checkpoint. */
        {
            char wt[1200];
            char saved_cwd[1024];
            char saved_root[4096];
            struct jc_snapshot_mgr *saved_snap = app->snapshots;
            int saved_fence = app->config.path_fence;
            int after = -1;
            /* M615: a metering envelope for the rehearsal, so test_edits is
             * COUNTED. This path armed no envelope at all, so a rehearsal
             * model that gutted the gate registered "FIXED by attempt" and
             * RAISED the pass-rate -- the metric this loop exists to track,
             * inflated by exactly the cheat `attempt` refuses (M410). */
            struct jc_envelope ienv;
            struct jc_envelope *ienv_saved;
            char irid[40];
            int ied = 0;

            jc_snprintf(wt, sizeof(wt), "%s/wt-%d", wtbase, wi++);
            if (jc_snapshot_worktree_add(&m, base, wt) != JC_OK) {
                jc_sb_append_fmt(&doc, "- %s: FAIL (worktree unavailable)\n", nm);
                continue;
            }
            fprintf(stderr, "improve: attempting '%s' in a worktree...\n", nm);
            jc_snprintf(saved_cwd, sizeof(saved_cwd), "%s", app->cwd);
            jc_snprintf(saved_root, sizeof(saved_root), "%s", app->root);
            if (chdir(wt) == 0) {
                char canon[JC_PATH_MAX];
                jc_snprintf(app->cwd, sizeof(app->cwd), "%s", wt);
                if (jc_path_resolve(wt, canon, sizeof(canon)) == JC_OK) {
                    jc_snprintf(app->root, sizeof(app->root), "%s", canon);
                } else {
                    jc_snprintf(app->root, sizeof(app->root), "%s", wt);
                }
                app->snapshots = NULL; /* the worktree IS the sandbox */
                /* Force the path fence on (rooted at the worktree) so an
                 * absolute-path edit_file/write_file can't escape to the real
                 * tree even when config pathFence is off -- see the same fix in
                 * run_assignment_attempt. */
                app->config.path_fence = 1;
                /* Make the assignment active so the hint/ask_for_help tools
                 * (registered under config.assignments) work during the
                 * attempt; spec is stable for this iteration. */
                app->assignment = &spec;
                app->assignment_spec = spath;             /* M536 */
                jc_snprintf(app->assignment_dir,
                            sizeof app->assignment_dir, "%s", saved_cwd);
                app->hints_used = 0;
                /* M615: setup inside the worktree, as `attempt` and `grade`
                 * run it -- one surface, one meaning. */
                if (spec.setup != NULL && spec.setup[0] != '\0') {
                    struct jc_sb stmp;
                    char *sv[4];
                    jc_sb_init(&stmp);
                    sv[0] = (char *)jc_shell_path(); sv[1] = "-c";
                    sv[2] = (char *)spec.setup; sv[3] = 0;
                    jc_proc_capture(sv, NULL, NULL, &stmp, 65536, 120, NULL);
                    jc_sb_free(&stmp);
                }
                jc_uuid_v4(irid);
                jc_env_init(&ienv, a, irid, NULL);
                ienv.rollback_on_fail = 0;
                ienv_saved = app->env;
                app->env = &ienv;
                improve_attempt_turn(app, spec.task);
                app->env = ienv_saved;
                ied = ienv.test_edits;
                jc_env_free(&ienv);
                app->assignment = NULL;
                app->assignment_spec = NULL;
                app->assignment_dir[0] = '\0';
                after = improve_run_verify(spec.verify); /* graded in the wt */
            }
            /* Restore the process + app to the real workspace. */
            if (orig_cwd[0] != '\0' && chdir(orig_cwd) != 0) {
                jc_logf(JC_LOG_WARN, "could not restore the working directory "
                        "to %s -- relative paths after this point may resolve "
                        "elsewhere", orig_cwd);
            }
            jc_snprintf(app->cwd, sizeof(app->cwd), "%s", saved_cwd);
            jc_snprintf(app->root, sizeof(app->root), "%s", saved_root);
            app->snapshots = saved_snap;
            app->config.path_fence = saved_fence;
            jc_snapshot_worktree_remove(&m, wt);

            attempted++;
            if (after == 1 && ied > 0) {
                /* M410, applied to the metric: green earned by moving the
                 * gate is not a fix, in the pass-rate either. */
                jc_sb_append_fmt(&doc, "- %s: TAINTED -- verify green but "
                                 "%d test-assertion edit(s) during the "
                                 "rehearsal; NOT counted as fixed\n", nm, ied);
            } else if (after == 1) {
                fixed++;
                jc_sb_append_fmt(&doc, "- %s: FIXED by attempt (discarded)\n",
                                 nm);
            } else {
                jc_sb_append_fmt(&doc, "- %s: still FAIL after attempt\n", nm);
            }
        }
    }
    jc_vec_free(&names);
    jc_app_set_mode(app, saved_mode);
    /* Remove the (now-empty) per-run worktree base dir. */
    {
        char *rmargv[4];
        rmargv[0] = "rm"; rmargv[1] = "-rf"; rmargv[2] = wtbase; rmargv[3] = 0;
        jc_proc_capture(rmargv, NULL, NULL, NULL, 0, 20, NULL);
    }

    cur = (total > 0) ? ((base_passed + fixed) * 100 / total) : -1;
    jc_snprintf(idir, sizeof(idir), "%s/.jichi.d/improve",
                jc_home_dir());
    jc_mkdir_p(idir);
    if (cur >= 0) {
        char hpath[1200];
        struct jc_sb h;
        char *htext = NULL;
        int prev;
        struct jc_sb r;

        jc_snprintf(hpath, sizeof(hpath), "%s/history.jsonl", idir);
        prev = (jc_read_file(hpath, &htext, NULL, a) == JC_OK && htext != NULL)
               ? jc_improve_last_pct(htext) : -1;
        jc_sb_init(&h);
        if (htext != NULL) {
            jc_sb_append(&h, htext);
        }
        jc_sb_append_fmt(&h, "{\"pct\":%d,\"passed\":%d,\"total\":%d,"
                         "\"mode\":\"attempt\",\"t\":%ld}\n", cur,
                         base_passed + fixed, total, (long)jc_now_seconds());
        jc_write_file(hpath, h.data != NULL ? h.data : "",
                      h.data != NULL ? h.len : 0);
        jc_sb_free(&h);

        jc_sb_init(&r);
        jc_sb_append(&r, "# Improve cycle (live rehearsal, worktree-isolated)\n\n");
        jc_sb_append(&r, "_Propose-only. Each attempt ran in a throwaway git "
                         "worktree and was discarded; your tree was untouched._"
                         "\n\n");
        jc_sb_append_fmt(&r, "## Pass-rate: %d%% attempted (%d/%d) -- %s",
                         cur, base_passed + fixed, total,
                         jc_improve_trend_word(prev, cur));
        if (prev >= 0) {
            jc_sb_append_fmt(&r, " (was %d%%)", prev);
        }
        jc_sb_append_fmt(&r, "\n\nBaseline %d/%d passed; the agent fixed %d of "
                         "%d attempted failures (in isolation).\n\n",
                         base_passed, total, fixed, attempted);
        jc_sb_append(&r, doc.data != NULL ? doc.data : "");
        jc_sb_append(&r, "\n## Next actions\n\n");
        jc_sb_append(&r, "- FIXED attempts were discarded; re-run the agent on "
                         "those specs (`-p`) to keep a fix.\n");
        jc_sb_append(&r, "- Feed still-FAIL specs to `/learn` as recurring "
                         "gaps.\n");
        jc_snprintf(rpath, sizeof(rpath), "%s/rehearsal-%ld.md", idir,
                    (long)jc_now_seconds());
        jc_write_file(rpath, r.data != NULL ? r.data : "",
                      r.data != NULL ? r.len : 0);
        jc_sb_free(&r);

        fprintf(stderr, "improve: attempted pass-rate %d%% (%d/%d); agent fixed "
                "%d/%d; wrote %s\n", cur, base_passed + fixed, total, fixed,
                attempted, rpath);
    } else {
        fprintf(stderr, "improve: no gradeable specs under %s\n", dir);
    }

    jc_sb_free(&doc);
    jc_arena_free(a);
    jc_snapshot_manager_shutdown(&m);
    return 0;
}

/* `attempt <spec.md> [--agent <profile>]` (B6): the tiered-learner test harness.
 * Load ONE unified assignment onto app->assignment (so the hint/ask_for_help
 * tools go live), optionally run it AS a learner profile (persona + readonly),
 * attempt it in an isolated git worktree materialised from a checkpoint, grade
 * it there via `verify`, and report pass + hints used. The user's tree is NEVER
 * touched (the worktree is the sandbox, discarded after). Needs snapshots +
 * a model; config.assignments must be on for the hint tools to be registered. */
static int run_assignment_attempt(struct jc_app *app, struct cli_args *args)
{
    const char *spec_path = (args->npos > 1) ? args->pos[1] : NULL;
    const char *agent_name = args->attempt_agent;
    struct jc_snapshot_mgr m;
    struct jc_arena *a;
    struct jc_assign_spec spec;
    struct jc_command_agent_save asave;
    const struct jc_agentdef *def = NULL;
    struct jc_envelope att_env;
    struct jc_envelope *saved_env;
    char *text;
    char base[128];
    char wtbase[1100];
    char wt[1200];
    char orig_cwd[1024];
    char saved_cwd[1024];
    char saved_root[4096];
    char vprog[512];
    int harness_broken = 0;
    const char *c0;
    struct jc_snapshot_mgr *saved_snap;
    enum jc_agent_mode am;
    int saved_mode;
    int saved_fence;
    char *saved_rules;
    int after = -1;
    int test_edits = 0;    /* M410: goalpost-edit count off the envelope */
    const char *verdict;   /* M410: PASS / FAIL / TAINTED */
    int passed_clean;      /* M415: verdict as a boolean (PASS and untainted) */
    struct jc_sb files_sb; /* M415: the run's changed files, one per line */
    int hints_used = 0;
    double tokens_used = 0.0;
    int bound_reached = 0;

    if (spec_path == NULL) {
        fprintf(stderr, "usage: attempt <spec.md> [--agent <profile>]\n");
        return 2;
    }
    a = jc_arena_new(0);
    if (jc_read_file(spec_path, &text, NULL, a) != JC_OK ||
        jc_assign_parse(text, &spec, a) != JC_OK) {
        fprintf(stderr, "attempt: could not read/parse '%s'\n", spec_path);
        jc_arena_free(a);
        return 1;
    }
    if (spec.verify == NULL || spec.verify[0] == '\0') {
        fprintf(stderr, "attempt: '%s' has no `verify` command to grade\n",
                spec_path);
        assign_warn_unterminated(spec_path, text);
        jc_arena_free(a);
        return 2;
    }
    /* M615: the M502 guard, on the attempt path. A verify whose PROGRAM does
     * not resolve from here is a broken harness, not a failing grade -- and
     * until M615 `attempt` scored it FAIL (exit 1) and --record wrote
     * passed:false into the learner's progress file. Checked from the real
     * cwd BEFORE any checkpoint, worktree or model call is paid for: the
     * worktree mirrors this tree, so what is unreachable here is unreachable
     * there. Only the program is examined, never the arguments (a missing
     * argument may be the learner's deliverable -- jc_assign.h). */
    if (jc_assign_verify_program(spec.verify, vprog, sizeof vprog) != NULL
        && strchr(vprog, '/') != NULL && !jc_file_exists(vprog)) {
        fprintf(stderr,
            "attempt: the verify command cannot run from here -- '%s' does "
            "not exist relative to this directory.\n  verify: %s\n"
            "This is NOT a grade (nothing was recorded). Run `attempt` from "
            "the directory the spec's paths are relative to.\n",
            vprog, spec.verify);
        jc_arena_free(a);
        return 2;
    }

    if (agent_name != NULL && agent_name[0] != '\0') {
        def = jc_agentdef_find(&app->agents, agent_name);
        if (def == NULL) {
            fprintf(stderr, "attempt: agent profile '%s' not found; using the "
                    "active persona\n", agent_name);
        }
    }

    jc_snapshot_manager_init(&m, app);
    if (!jc_snapshot_available(&m)) {
        fprintf(stderr, "attempt needs snapshots (git + \"snapshots\": true) to "
                "sandbox the attempt in a worktree.\n");
        jc_snapshot_manager_shutdown(&m);
        jc_arena_free(a);
        return 1;
    }
    if (getcwd(orig_cwd, sizeof(orig_cwd)) == NULL) {
        orig_cwd[0] = '\0';
    }
    jc_snapshot_take(&m, "attempt base");
    c0 = jc_snapshot_commit(&m, jc_snapshot_count(&m) - 1);
    if (c0 == NULL) {
        fprintf(stderr, "attempt: could not checkpoint the tree.\n");
        jc_snapshot_manager_shutdown(&m);
        jc_arena_free(a);
        return 1;
    }
    jc_snprintf(base, sizeof(base), "%s", c0);
    jc_snprintf(wtbase, sizeof(wtbase), "%s/.jichi.d/worktrees/att-%ld",
                jc_home_dir(), (long)getpid());
    jc_mkdir_p(wtbase);
    jc_snprintf(wt, sizeof(wt), "%s/wt-0", wtbase);
    if (jc_snapshot_worktree_add(&m, base, wt) != JC_OK) {
        fprintf(stderr, "attempt: worktree unavailable.\n");
        jc_snapshot_manager_shutdown(&m);
        jc_arena_free(a);
        return 1;
    }

    saved_mode = app->mode;
    if (jc_agent_mode_parse("auto", &am)) {
        jc_app_set_mode(app, (int)am);
    }
    /* Bound the attempt with a metering-only envelope (budget / deadline /
     * tool-call caps) built from the CLI flags. Deliberately carries no
     * verify_cmd or edit_scope: the attempt grades itself by the spec's verify
     * in the worktree, and the worktree IS the isolation -- so this replaces any
     * config-derived envelope for the duration to keep the two decoupled. Zero
     * caps (no flags) => unbounded, exactly as before. */
    saved_env = app->env;
    {
        char rid[40];
        jc_uuid_v4(rid);
        jc_env_init(&att_env, a, rid, NULL);
        if (args->budget_tokens != NULL) {
            jc_env_parse_size(args->budget_tokens, &att_env.budget_tokens);
        }
        if (args->deadline != NULL) {
            jc_env_parse_duration(args->deadline, &att_env.deadline_secs);
        }
        if (args->max_tool_calls > 0) {
            att_env.max_tool_calls = args->max_tool_calls;
        }
        att_env.verify_cmd = NULL;
        att_env.rollback_on_fail = 0;
        app->env = &att_env;
    }
    saved_snap = app->snapshots;
    saved_fence = app->config.path_fence;
    jc_snprintf(saved_cwd, sizeof(saved_cwd), "%s", app->cwd);
    jc_snprintf(saved_root, sizeof(saved_root), "%s", app->root);

    fprintf(stderr, "attempt: '%s'%s%s in a worktree%s...\n",
            spec.title != NULL ? spec.title : spec_path,
            def != NULL ? " as " : "", def != NULL ? agent_name : "",
            /* M309: never let the prompt differ from an interactive session
             * silently -- an operator comparing the two must not have to guess. */
            args->attempt_with_rules ? " (project rules loaded)"
                                     : " (project rules skipped)");

    if (chdir(wt) == 0) {
        char canon[JC_PATH_MAX];
        char *task;
        jc_snprintf(app->cwd, sizeof(app->cwd), "%s", wt);
        if (jc_path_resolve(wt, canon, sizeof(canon)) == JC_OK) {
            jc_snprintf(app->root, sizeof(app->root), "%s", canon);
        } else {
            jc_snprintf(app->root, sizeof(app->root), "%s", wt);
        }
        app->snapshots = NULL; /* the worktree IS the sandbox */
        /* Force the path fence ON, rooted at the worktree, for the duration --
         * even when config pathFence is off. Without this, a solver that uses
         * edit_file/write_file with an ABSOLUTE real-tree path escapes the
         * worktree entirely (observed: a run wrote its solution into the live
         * repo, so the empty worktree then failed to grade -- a false negative
         * AND a real-tree mutation). The fence denies writes outside app->root
         * (= the worktree). NOTE residual: run_terminal_command can still `cd`
         * out via the shell; the fence covers the file tools, not shell cwd. */
        app->config.path_fence = 1;
        /* M309: do NOT inject the host project's rules file into a graded attempt,
         * unless --with-rules asks for it.
         *
         * Measured (M308): running the curriculum's assignments inside the jichi
         * repo, 70% of every call's ~21k prompt tokens was the system prompt, and
         * almost all of that was the 121 KB CLAUDE.md loaded as project rules and
         * fitted to ~45% of the window (M73). The one-POINT task cost the same as
         * the three-point one -- 128k tokens to write a single line -- so a learner
         * on a modest budget sees FAIL on assignment 00 and concludes they did the
         * exercise wrong. The gate lied.
         *
         * Safe because an assignment spec is self-contained and its `verify` command
         * is the grader: the host project's contributor guide is not graded, is not
         * what the exercise teaches, and describes a codebase the learner is not
         * modifying. Restored, not deleted, so an instructor who *does* want project
         * conventions in play can ask. See
         * docs/proposals/2026-08-attempt-rules-and-plain-assignments.md for the
         * rejected alternatives (trimming harder, a config key, doing it for every
         * --auto run). */
        saved_rules = app->rules;
        if (!args->attempt_with_rules) {
            app->rules = NULL;
        }
        app->assignment = &spec;
        /* M536: the REAL workspace, captured before the chdir into the
         * worktree, so a hint pulled mid-attempt is recorded where the grade
         * lands rather than inside the sandbox that is about to be removed. */
        app->assignment_spec = spec_path;
        jc_snprintf(app->assignment_dir, sizeof app->assignment_dir,
                    "%s", saved_cwd);
        app->hints_used = 0;
        if (def != NULL) {
            jc_app_command_agent_apply(app, agent_name, &asave);
        }
        /* M615: run the spec's `setup` INSIDE the worktree, before the turn.
         * `grade` has always run it before verify; `attempt` never did, so a
         * setup-dependent spec graded differently per surface. The worktree is
         * the sandbox, so a reset like `git checkout -- .` or a fixture
         * `touch` lands here and never in the user's tree. */
        if (spec.setup != NULL && spec.setup[0] != '\0') {
            struct jc_sb stmp;
            char *sv[4];
            jc_sb_init(&stmp);
            sv[0] = (char *)jc_shell_path(); sv[1] = "-c";
            sv[2] = (char *)spec.setup; sv[3] = 0;
            jc_proc_capture(sv, NULL, NULL, &stmp, 65536, 120, NULL);
            jc_sb_free(&stmp);
        }
        /* Give the solver the audience-framed brief (advertises the hints). */
        task = jc_assign_render(&spec, a);
        improve_attempt_turn(app, task != NULL ? task : spec.task);
        app->rules = saved_rules;                        /* M309 */
        if (def != NULL) {
            jc_app_command_agent_restore(app, &asave);
        }
        hints_used = app->hints_used;
        app->assignment = NULL;
        app->assignment_spec = NULL;                     /* M536 */
        app->assignment_dir[0] = '\0';
        after = improve_run_verify(spec.verify);
    } else {
        /* M615: a worktree we could not enter is a HARNESS failure. after==-1
         * used to fall through to the verdict as a plain FAIL. */
        harness_broken = 1;
    }
    tokens_used = att_env.tokens_used;
    bound_reached = (att_env.tripped != JC_BUDGET_NONE);

    /* Restore the process + app to the real workspace. */
    if (orig_cwd[0] != '\0' && chdir(orig_cwd) != 0) {
        /* M332: this was unchecked. If it fails the process is left in the
         * sub-run's directory while app->cwd below claims otherwise, and every
         * later relative path resolves against the wrong root. Nothing here can
         * fix it; saying so is the least that is owed. */
        jc_logf(JC_LOG_WARN, "could not restore the working directory to %s -- "
                "relative paths after this point may resolve elsewhere",
                orig_cwd);
    }
    jc_snprintf(app->cwd, sizeof(app->cwd), "%s", saved_cwd);
    jc_snprintf(app->root, sizeof(app->root), "%s", saved_root);
    app->snapshots = saved_snap;
    app->config.path_fence = saved_fence;
    app->env = saved_env;
    /* Fold the attempt's metering back into any process-level envelope so its
     * teardown summary reflects the real usage (not a misleading "tokens 0"). */
    if (saved_env != NULL) {
        saved_env->tokens_used += att_env.tokens_used;
        saved_env->tool_calls += att_env.tool_calls;
    }
    /* M410: read the goalpost counter BEFORE the envelope is freed. */
    test_edits = att_env.test_edits;
    jc_env_free(&att_env);
    jc_app_set_mode(app, saved_mode);
    /* M415: the changed-file list, collected while the worktree still exists.
     * It earned its place in a real review: a junior's out-of-task edit to a
     * core type was invisible to its own filtered verify and surfaced only in
     * the kept worktree -- a machine consumer deserves the same visibility. */
    jc_sb_init(&files_sb);
    (void)jc_snapshot_worktree_changes(&m, wt, base, &files_sb);
    /* M410: `--keep-worktree` leaves the sandbox for review. Before this, the
     * worktree was removed unconditionally -- so a TAINTED run's diff (the
     * only evidence behind its green verify) was destroyed in the same breath
     * as the verdict was printed. Kept trees live under ~/.jichi.d/worktrees
     * and are the user's to delete. */
    if (args->attempt_keep) {
        fprintf(stderr, "attempt: worktree kept for review: %s\n", wt);
    } else {
        jc_snapshot_worktree_remove(&m, wt);
        {
            char *rmargv[4];
            rmargv[0] = "rm"; rmargv[1] = "-rf"; rmargv[2] = wtbase;
            rmargv[3] = 0;
            jc_proc_capture(rmargv, NULL, NULL, NULL, 0, 20, NULL);
        }
    }
    jc_snapshot_manager_shutdown(&m);

    /* M410: the verdict must not contradict the run's own log. A green verify
     * during which M88's moved-goalpost warning fired is reported TAINTED, not
     * PASS -- measured on a real learner run (2026-08-12): ten warnings, gate
     * tests gutted, and the old line still said "PASS (0 hints used)". Exit
     * code 1: a supervisor gating on it must not accept unreviewed green. */
    if (harness_broken) {
        fprintf(stderr, "attempt: could not enter the worktree -- harness, "
                "not a grade. This is NOT a grade (nothing was recorded).\n");
        jc_sb_free(&files_sb);
        jc_arena_free(a);
        return 2;
    }
    verdict = jc_assign_attempt_verdict(after == 1, test_edits);
    fprintf(stderr, "attempt: %s%s%s -- %s (%d hint%s used, %.0fk tokens%s)\n",
            spec.title != NULL ? spec.title : spec_path,
            def != NULL ? " as " : "", def != NULL ? agent_name : "",
            verdict,
            hints_used, hints_used == 1 ? "" : "s",
            tokens_used / 1000.0,
            bound_reached ? ", bound reached" : "");
    if (test_edits > 0) {
        fprintf(stderr, "attempt: %d test-assertion edit%s during the run -- "
                "verify green is not evidence that the task was solved; "
                "re-run with --keep-worktree and read the diff\n",
                test_edits, test_edits == 1 ? "" : "s");
    }
    passed_clean = (after == 1 && test_edits == 0) ? 1 : 0;
    /* M415: one machine row on stdout (`--output json`) -- the gradebook line
     * an instructor's batch loop collects, mirroring `grade --output json`.
     * The human verdict above stays on stderr, so one run serves both. */
    if (args->output_json == 1) {
        cJSON *o = cJSON_CreateObject();
        char *s;
        cJSON_AddStringToObject(o, "spec", spec_path);
        if (spec.title != NULL) {
            cJSON_AddStringToObject(o, "title", spec.title);
        }
        if (def != NULL) {
            cJSON_AddStringToObject(o, "agent", agent_name);
        }
        cJSON_AddStringToObject(o, "verdict", verdict);
        cJSON_AddBoolToObject(o, "passed", passed_clean);
        cJSON_AddNumberToObject(o, "test_edits", (double)test_edits);
        cJSON_AddNumberToObject(o, "hints_used", (double)hints_used);
        cJSON_AddNumberToObject(o, "tokens", tokens_used);
        cJSON_AddBoolToObject(o, "bound_reached", bound_reached ? 1 : 0);
        {
            cJSON *arr = cJSON_CreateArray();
            char *line = files_sb.data;
            while (line != NULL && *line != '\0') {
                char *nl = strchr(line, '\n');
                if (nl != NULL) {
                    *nl = '\0';
                }
                if (*line != '\0') {
                    cJSON_AddItemToArray(arr, cJSON_CreateString(line));
                }
                line = (nl != NULL) ? nl + 1 : NULL;
            }
            cJSON_AddItemToObject(o, "files_changed", arr);
        }
        if (args->attempt_keep) {
            cJSON_AddStringToObject(o, "worktree", wt);
        }
        s = cJSON_PrintUnformatted(o);
        if (s != NULL) {
            printf("%s\n", s);
            free(s);
        }
        cJSON_Delete(o);
    }
    /* M415: `--record` appends the attempt to .jichi/progress.jsonl -- with
     * the VERDICT'S truth, not the verify's: a TAINTED attempt records
     * passed=false, because green earned by moving the gate is not a pass in
     * the gradebook either. hints_used is finally recorded by a writer that
     * knows it (grade is stateless and passes -1). */
    if (args->record) {
        if (jc_progress_append(".", spec_path, passed_clean,
                               passed_clean ? 100 : 0, 0, 0,
                               hints_used) != JC_OK) {
            fprintf(stderr, "attempt: could not append to "
                    ".jichi/progress.jsonl\n");
        }
    }
    jc_sb_free(&files_sb);
    jc_arena_free(a);
    return passed_clean ? 0 : 1;
}

/* Copy (A/M) or delete (D) one changed path from worktree `wt` into the live
 * workspace (app->cwd). Mirrors spawn_parallel's apply_change. */
static int wf_apply_change(struct jc_app *app, const char *wt, char status,
                           const char *rel, struct jc_arena *a)
{
    char dst[2048];
    jc_snprintf(dst, sizeof(dst), "%s/%s", app->cwd, rel);
    if (status == 'D') {
        remove(dst);
        return 1;
    }
    {
        char src[2048];
        char dstdir[2048];
        char *slash;
        char *content = NULL;
        jc_size len = 0;
        jc_snprintf(src, sizeof(src), "%s/%s", wt, rel);
        if (jc_read_file(src, &content, &len, a) != JC_OK) {
            return 0;
        }
        jc_snprintf(dstdir, sizeof(dstdir), "%s", dst);
        slash = strrchr(dstdir, '/');
        if (slash != NULL) {
            *slash = '\0';
            jc_mkdir_p(dstdir);
        }
        return jc_write_file(dst, content, len) == JC_OK ? 1 : 0;
    }
}

/* Run one map item's subagent inside worktree `wt` (write map): chdir + repoint
 * app->cwd/root, run the (mutating) turn, restore. Appends the answer to
 * `stage_out`. */
static void wf_run_in_worktree(struct jc_app *app, const char *wt,
                               const char *prompt, const char *sysmsg,
                               struct jc_provider *prov,
                               const char *item, struct jc_sb *stage_out)
{
    char saved_cwd[1024];
    char saved_root[4096];
    struct jc_snapshot_mgr *saved_snap = app->snapshots;
    char orig[1024];

    if (getcwd(orig, sizeof(orig)) == NULL) {
        orig[0] = '\0';
    }
    jc_snprintf(saved_cwd, sizeof(saved_cwd), "%s", app->cwd);
    jc_snprintf(saved_root, sizeof(saved_root), "%s", app->root);
    if (chdir(wt) == 0) {
        char canon[JC_PATH_MAX];
        struct jc_history sub;
        char *answer = NULL;
        jc_snprintf(app->cwd, sizeof(app->cwd), "%s", wt);
        if (jc_path_resolve(wt, canon, sizeof(canon)) == JC_OK) {
            jc_snprintf(app->root, sizeof(app->root), "%s", canon);
        } else {
            jc_snprintf(app->root, sizeof(app->root), "%s", wt);
        }
        app->snapshots = NULL; /* the worktree IS the sandbox */
        jc_history_init(&sub);
        jc_history_add(&sub, JC_ROLE_USER, prompt);
        jc_agent_run_subagent(app, &sub, prov, sysmsg, 1,
                              app->config.max_subagent_iters, NULL, NULL,
                              &answer);
        jc_sb_append_fmt(stage_out, "### %s\n%s\n\n", item,
                         answer != NULL ? answer : "(no answer)");
        jc_history_free(&sub);
    }
    if (orig[0] != '\0' && chdir(orig) != 0) {
        jc_logf(JC_LOG_WARN, "could not restore the working directory to %s -- "
                "relative paths after this point may resolve elsewhere", orig);
    }
    jc_snprintf(app->cwd, sizeof(app->cwd), "%s", saved_cwd);
    jc_snprintf(app->root, sizeof(app->root), "%s", saved_root);
    app->snapshots = saved_snap;
}

/* `workflow <spec.json>` (M101): run a deterministic multi-agent pipeline.
 * Stages: `map` (one subagent per $ITEM item -- read-only on the live tree, or
 * write in an isolated worktree merged back first-wins), `verify` (run a shell
 * command, fold pass/fail into context), and `synthesize` (fold the collected
 * output via a one-shot). The harness drives the stages deterministically. */
static int run_workflow(struct jc_app *app, const char *spec_path)
{
    struct jc_arena *a;
    char *text;
    struct jc_workflow wf;
    struct jc_sb ctx; /* accumulated output carried between stages */
    const char *sysmsg;
    struct jc_snapshot_mgr wfm; /* for write-map worktrees */
    int snap_ok;
    int si;

    if (spec_path == NULL) {
        fprintf(stderr, "usage: workflow <spec.json>\n");
        return 2;
    }
    a = jc_arena_new(0);
    if (jc_read_file(spec_path, &text, NULL, a) != JC_OK) {
        fprintf(stderr, "workflow: could not read '%s'\n", spec_path);
        jc_arena_free(a);
        return 1;
    }
    if (jc_workflow_parse(text, &wf, a) != JC_OK) {
        fprintf(stderr, "workflow: '%s' has no usable stages (need a JSON "
                "{stages:[{type:\"map\"|\"synthesize\",...}]})\n", spec_path);
        jc_arena_free(a);
        return 1;
    }
    fprintf(stderr, "workflow: %s (%d stage(s))\n",
            wf.name != NULL ? wf.name : "(unnamed)", wf.nstages);
    /* M610: never run a truncated spec silently. A 70-item fan-out that runs 64
     * and reports success is the "no silent caps" rule broken in the structure
     * that represents the task itself. */
    if (wf.stages_dropped > 0 || wf.items_dropped > 0) {
        fprintf(stderr, "workflow: WARNING -- the spec exceeded built-in limits "
                "(max %d stages, %d items/stage): %d stage(s) and %d item(s) "
                "were DROPPED and will not run\n",
                JC_WF_MAX_STAGES, JC_WF_MAX_ITEMS,
                wf.stages_dropped, wf.items_dropped);
    }
    /* Copy the subagent system prompt into our arena (decouple from scratch). */
    sysmsg = jc_arena_strdup(a, jc_sysmsg_build_sub(app));
    jc_snapshot_manager_init(&wfm, app); /* used only by write-map stages */
    snap_ok = jc_snapshot_available(&wfm);

    jc_sb_init(&ctx);
    for (si = 0; si < wf.nstages; si++) {
        struct jc_wf_stage *s = &wf.stages[si];
        if (app->abort_flag) {
            break; /* Ctrl-C: stop the pipeline between stages */
        }
        if (s->type == JC_WF_MAP) {
            struct jc_sb stage_out;
            struct jc_provider *prov = app->provider;
            struct jc_provider *tmpprov = NULL;
            int j;
            if (s->model != NULL && s->model[0] != '\0') {
                int mi = jc_config_find_model(&app->config, s->model);
                struct jc_model_cfg *mc = (mi >= 0)
                    ? jc_config_model_at(&app->config, mi) : NULL;
                if (mc != NULL) {
                    tmpprov = jc_provider_create(mc);
                    if (tmpprov != NULL) {
                        prov = tmpprov;
                    }
                }
            }
            jc_sb_init(&stage_out);
            if (s->readonly) {
                /* Read-only fan-out on the live tree. */
                for (j = 0; j < s->nitems; j++) {
                    char *prompt;
                    struct jc_history sub;
                    char *answer = NULL;
                    if (app->abort_flag) {
                        break; /* Ctrl-C: stop the fan-out, don't grind on */
                    }
                    prompt = jc_workflow_expand(s->prompt, s->items[j], a);
                    fprintf(stderr, "workflow: [stage %d] map %d/%d: %s\n",
                            si + 1, j + 1, s->nitems, s->items[j]);
                    jc_history_init(&sub);
                    jc_history_add(&sub, JC_ROLE_USER, prompt);
                    jc_agent_run_subagent(app, &sub, prov, sysmsg, 0,
                                          app->config.max_subagent_iters, NULL,
                                          NULL, &answer);
                    jc_sb_append_fmt(&stage_out, "### %s\n%s\n\n", s->items[j],
                                     answer != NULL ? answer : "(no answer)");
                    jc_history_free(&sub);
                }
            } else if (!snap_ok) {
                fprintf(stderr, "workflow: [stage %d] write map needs snapshots "
                        "(git + \"snapshots\": true); skipping.\n", si + 1);
            } else {
                /* Write fan-out: each item edits an isolated worktree off a
                 * baseline checkpoint; changes merge back first-wins. The live
                 * tree is only touched by the merge, never reset/cleaned. */
                char base[128];
                char wtbase[1100];
                const char *c0;
                char *wts[JC_WF_MAX_ITEMS];
                struct jc_vec seen;
                int applied = 0;
                int conflicts = 0;
                /* Zero every slot: an abort break leaves later slots unset,
                 * and the merge loop below reads all nitems entries. */
                memset(wts, 0, sizeof(wts));
                jc_snapshot_take(&wfm, "workflow write base");
                c0 = jc_snapshot_commit(&wfm, jc_snapshot_count(&wfm) - 1);
                if (c0 == NULL) {
                    fprintf(stderr, "workflow: could not checkpoint; skipping "
                            "stage %d.\n", si + 1);
                } else {
                    jc_snprintf(base, sizeof(base), "%s", c0);
                    jc_snprintf(wtbase, sizeof(wtbase),
                                "%s/.jichi.d/worktrees/wf-%ld-%d",
                                jc_home_dir(), (long)getpid(), si);
                    jc_mkdir_p(wtbase);
                    for (j = 0; j < s->nitems; j++) {
                        char wt[1200];
                        char *prompt;
                        wts[j] = NULL;
                        if (app->abort_flag) {
                            break; /* Ctrl-C: stop before spawning the next */
                        }
                        prompt = jc_workflow_expand(s->prompt, s->items[j], a);
                        jc_snprintf(wt, sizeof(wt), "%s/wt-%d", wtbase, j);
                        fprintf(stderr,
                                "workflow: [stage %d] write %d/%d: %s\n",
                                si + 1, j + 1, s->nitems, s->items[j]);
                        if (jc_snapshot_worktree_add(&wfm, base, wt) != JC_OK) {
                            jc_sb_append_fmt(&stage_out,
                                "### %s\n(worktree unavailable)\n\n",
                                s->items[j]);
                            continue;
                        }
                        wts[j] = jc_arena_strdup(a, wt);
                        wf_run_in_worktree(app, wt, prompt, sysmsg, prov,
                                           s->items[j], &stage_out);
                    }
                    jc_vec_init(&seen, sizeof(char *));
                    for (j = 0; j < s->nitems; j++) {
                        struct jc_sb chg;
                        struct jc_vec changes;
                        jc_size k;
                        if (wts[j] == NULL) {
                            continue;
                        }
                        jc_sb_init(&chg);
                        if (jc_snapshot_worktree_changes(&wfm, wts[j], base,
                                                         &chg) == JC_OK &&
                            chg.data != NULL) {
                            jc_vec_init(&changes, sizeof(struct jc_change));
                            jc_parallel_parse_changes(chg.data, a, &changes);
                            for (k = 0; k < changes.len; k++) {
                                struct jc_change *c = (struct jc_change *)
                                    jc_vec_at(&changes, k);
                                if (jc_parallel_claim(&seen, c->path)) {
                                    if (wf_apply_change(app, wts[j], c->status,
                                                        c->path, a)) {
                                        applied++;
                                    }
                                } else {
                                    conflicts++;
                                }
                            }
                            jc_vec_free(&changes);
                        }
                        jc_sb_free(&chg);
                    }
                    jc_vec_free(&seen);
                    for (j = 0; j < s->nitems; j++) {
                        if (wts[j] != NULL) {
                            jc_snapshot_worktree_remove(&wfm, wts[j]);
                        }
                    }
                    rmdir(wtbase);
                    fprintf(stderr,
                            "workflow: [stage %d] merged %d file(s)%s\n",
                            si + 1, applied,
                            conflicts > 0 ? " (conflicts skipped)" : "");
                }
            }
            if (tmpprov != NULL) {
                tmpprov->vt->free(tmpprov);
            }
            jc_sb_clear(&ctx);
            jc_sb_append(&ctx, stage_out.data != NULL ? stage_out.data : "");
            jc_sb_free(&stage_out);
        } else if (s->type == JC_WF_VERIFY) {
            /* Run a shell command; fold its pass/fail + a short tail into the
             * context so a later synthesize stage can react to it. */
            struct jc_sb vout;
            struct jc_test_report rep;
            char *argv[4];
            int rc;
            const char *cmd = (s->command != NULL && s->command[0] != '\0')
                              ? s->command : s->prompt;
            fprintf(stderr, "workflow: [stage %d] verify: %s\n", si + 1,
                    cmd != NULL ? cmd : "(no command)");
            if (cmd == NULL || cmd[0] == '\0') {
                jc_sb_append(&ctx, "\nverify: (no command)\n");
                continue;
            }
            jc_sb_init(&vout);
            argv[0] = (char *)jc_shell_path(); argv[1] = "-c"; argv[2] = (char *)cmd;
            argv[3] = 0;
            rc = jc_proc_capture(argv, NULL, NULL, &vout, 262144, 600, NULL);
            jc_test_report_init(&rep);
            jc_testparse(vout.data, &rep);
            jc_sb_append_fmt(&ctx, "\nverify `%s`: %s (exit %d)\n", cmd,
                             rc == 0 ? "PASS" : "FAIL", rc);
            if (rep.total >= 0 || rep.failed > 0) {
                jc_sb_append_fmt(&ctx, "  tests: %d passed, %d failed\n",
                                 rep.passed < 0 ? 0 : rep.passed,
                                 rep.failed < 0 ? 0 : rep.failed);
            }
            jc_test_report_free(&rep);
            jc_sb_free(&vout);
        } else if (s->type == JC_WF_SYNTHESIZE) {
            struct jc_sb up;
            char *answer;
            int synth_timeout = 0;
            fprintf(stderr, "workflow: [stage %d] synthesize\n", si + 1);
            jc_sb_init(&up);
            jc_sb_append(&up, (s->prompt != NULL && s->prompt[0] != '\0')
                         ? s->prompt
                         : "Synthesize the following into one coherent result.");
            jc_sb_append(&up, "\n\n--- inputs ---\n");
            jc_sb_append(&up, ctx.data != NULL ? ctx.data : "");
            answer = jc_oneshot_ex(app->provider,
                "You synthesize sub-results into one coherent, deduplicated "
                "answer. Output only the result.",
                up.data != NULL ? up.data : "", 180, &app->abort_flag,
                &synth_timeout);
            /* Don't silently drop the map results: if synthesize yields
             * nothing, say why and keep the inputs as the output. */
            if (answer == NULL || answer[0] == '\0') {
                fprintf(stderr, "workflow: [stage %d] synthesize produced no "
                    "output (%s); keeping the stage inputs.\n", si + 1,
                    synth_timeout ? "model timed out"
                                  : "model replied empty -- a reasoning model "
                                    "may need a larger maxTokens");
            } else {
                jc_sb_clear(&ctx);
                jc_sb_append(&ctx, answer);
            }
            free(answer);
            jc_sb_free(&up);
        }
    }
    if (ctx.len == 0) {
        fprintf(stderr, "workflow: produced no output.\n");
    }
    fputs(ctx.data != NULL ? ctx.data : "", stdout);
    if (ctx.len == 0 || ctx.data[ctx.len - 1] != '\n') {
        printf("\n");
    }
    jc_sb_free(&ctx);
    jc_snapshot_manager_shutdown(&wfm);
    jc_arena_free(a);
    return 0;
}

/* The neutral one-tool array the M167 live probe advertises. Built here rather
 * than in the pure jc_toolprobe core so that core stays cJSON-free. */
#ifdef JC_HAVE_CURL
static cJSON *probe_tool_array(void)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON *props = cJSON_CreateObject();
    cJSON *text = cJSON_CreateObject();
    cJSON *req = cJSON_CreateArray();
    cJSON_AddStringToObject(t, "name", JC_TOOLPROBE_TOOL);
    cJSON_AddStringToObject(t, "description",
                            "Echo back the given text. Used to verify tool "
                            "calling works.");
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddStringToObject(text, "type", "string");
    cJSON_AddStringToObject(text, "description", "The text to echo.");
    cJSON_AddItemToObject(props, "text", text);
    cJSON_AddItemToObject(params, "properties", props);
    cJSON_AddItemToArray(req, cJSON_CreateString("text"));
    cJSON_AddItemToObject(params, "required", req);
    cJSON_AddItemToObject(t, "parameters", params);
    cJSON_AddItemToArray(arr, t);
    return arr;
}
#endif

static int run_doctor(struct jc_app *app, int json, int unattended, int live)
{
    struct jc_doctor d;
    struct jc_sb out;
    char detail[1400];
    int color = jc_color_enabled(app->color_mode, isatty(STDOUT_FILENO));
    int unicode;
    int n, i, code;
    const char *lc = getenv("LC_ALL");

    if (lc == NULL || lc[0] == '\0') lc = getenv("LC_CTYPE");
    if (lc == NULL || lc[0] == '\0') lc = getenv("LANG");
    unicode = lc != NULL && (strstr(lc, "UTF-8") != NULL ||
                             strstr(lc, "UTF8") != NULL ||
                             strstr(lc, "utf-8") != NULL ||
                             strstr(lc, "utf8") != NULL);

    /* Lead with the version: the first thing a support conversation needs
     * (M178). Text mode only -- the json output keeps its existing shape. */
    if (!json) {
        printf("jichi %s\n", JC_VERSION);
    }

    jc_doctor_init(&d);

    /* The host platform, and whether jichi has ever been verified on it (M400).
     *
     * The setup wizard already told a non-Linux user "this looks like a system
     * jichi is not tested on"; doctor -- the command every page says to run
     * first, and the one a support conversation starts with -- did not. So a
     * macOS user learned it only if they happened to run `setup` interactively
     * and read a paragraph. It is a WARN, not a FAIL: unverified is not broken,
     * and a FAIL here would gate `doctor --unattended` on a platform question no
     * run can answer. Linux says nothing at all -- the verified case must not
     * cost a line. */
    if (!jc_platform_verified_row()) {
        char plat[160];
        if (jc_platform_describe(plat, sizeof plat)) {
            jc_doctor_add(&d, JC_DOC_WARN, plat,
                          "jichi has never been compiled on this platform "
                          "(docs/PLATFORMS.md); expect to be the first to find "
                          "what does not work -- and please report it");
        } else {
            jc_doctor_add(&d, JC_DOC_WARN, "host platform not recognised",
                          "docs/PLATFORMS.md lists what was measured where");
        }
    }

    /* State root (M472). Everything private is rooted at jc_home_dir() -- the
     * config, ~/.jichi.env, sessions, telemetry, the audit log -- so an unset
     * HOME silently relocates all of them at once. It used to relocate them to
     * /tmp, which another local user can pre-populate.
     *
     * WARN interactively, FAIL under --unattended, using M158b's explicit
     * escalation pattern (the escalation is per-check, not automatic -- there is
     * no blanket WARN->FAIL rule, and assuming one is how a check ends up
     * gating nothing). An unattended loop whose state root moved is writing its
     * audit trail somewhere nobody will look for it, which is precisely the
     * class of posture problem a supervisor should stop on. */
    {
        const char *env_home = getenv("HOME");
        const char *root = jc_home_dir();
        if (env_home == NULL || env_home[0] == '\0') {
            char detail[512];
            jc_snprintf(detail, sizeof(detail),
                        "HOME is unset; state resolved to %s. Set HOME: config, "
                        "sessions, telemetry and the audit log all live under it.",
                        root);
            jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                          "state root: HOME not set", detail);
        } else {
            jc_doctor_add(&d, JC_DOC_OK, "state root", root);
        }
    }

    /* M505: was the active model's id SUBSTITUTED rather than configured?
     *
     * `default_model(provider)` fills a missing "model" field -- `gpt-4o` for
     * openai, `claude-opus-4-8` otherwise -- and nothing said so. Measured while
     * reviewing this project's own documentation: `{"models":[{"name":"a"}]}`
     * gave `config validate: OK` and doctor's GREEN `configuration loaded --
     * active: a (claude-opus-4-8)`, which is indistinguishable from a config
     * that named that model.
     *
     * Two reasons that matters more than it looks. The substitution reaches for
     * a PRICED frontier id, which is the hazard ANECDOTES #63 records in this
     * project's own history; and a hardcoded model id in a fallback is a stale
     * claim by construction -- newer ids exist already.
     *
     * WARN, and FAIL under --unattended: a supervisor starting a loop against a
     * model nobody chose is a posture problem of exactly the kind M158b's
     * escalation set is for. The default itself is left alone -- removing it
     * would change behaviour for configs that rely on it, and this is a
     * reporting defect, not a resolution one (the M503 verify_source argument). */
    if (app->config.model.model_defaulted) {
        char detail[420];
        jc_snprintf(detail, sizeof(detail),
            "the config names no \"model\" for the active entry, so '%s' was "
            "substituted from the built-in default for provider '%s'. Name the "
            "model id you intend -- a defaulted id can be a priced model you "
            "did not choose, and it goes stale as ids change.",
            app->config.model.model != NULL ? app->config.model.model : "?",
            app->config.model.provider != NULL ? app->config.model.provider
                                               : "(unset)");
        jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                      "active model id was DEFAULTED, not configured", detail);
    }

    /* M503: does `chmod` actually DO anything here? `jc_make_private()` calls it
     * and believes the return value -- its own comment notes a failure is not
     * fatal -- and on a filesystem that ignores POSIX modes that return value is
     * a lie. Measured on MSYS2 with the default `noacl` mount: `chmod 0600`
     * reports SUCCESS and leaves the mode at 0644, so the API key file, the
     * daemon socket and the audit log are all readable by any local user while
     * jichi reports no problem at all. (Cygwin, same machine, same NTFS, same
     * user, yields 0600 -- it is the mount option, not Windows.)
     *
     * Nothing in jichi can distinguish "the file is owner-only" from "the call
     * was accepted and ignored", so the honest thing is to find out: create,
     * chmod, stat back, compare. Cheap, platform-independent, and it belongs in
     * doctor because doctor exists to report what the environment will not do
     * for you.
     *
     * Tested INSIDE the state directory, not /tmp: mount options differ per
     * filesystem, and the question is about the directory that actually holds
     * the secrets.
     *
     * WARN interactively, FAIL under --unattended -- M158b's explicit,
     * per-check escalation, and this belongs in that set for the same reason
     * `privilegedAudit: false` and a disabled path fence do: an unattended run
     * whose daemon socket is world-readable lets any local user drive a process
     * that runs shell commands. */
    {
        char dir[1100];
        char probe[1200];
        long mode = -1;
        long created = -1;
        jc_snprintf(dir, sizeof(dir), "%s/.jichi.d", jc_home_dir());
        jc_mkdir_p(dir);
        jc_snprintf(probe, sizeof(probe), "%s/.modeprobe", dir);
        {
            FILE *f = fopen(probe, "w");
            if (f != NULL) {
                fputc('x', f);
                fclose(f);
                created = jc_file_mode(probe);
                jc_make_private(probe);
                mode = jc_file_mode(probe);
                remove(probe);
            }
        }
        if (mode < 0) {
            jc_doctor_add(&d, JC_DOC_WARN,
                "could not test whether file permissions take effect",
                "created no probe file under ~/.jichi.d -- private-file "
                "guarantees are unverified here");
        } else if ((mode & 0077) != 0) {
            char detail[640];
            jc_snprintf(detail, sizeof(detail),
                "chmod 0600 reported success and the mode is %04lo -- this "
                "filesystem ignores POSIX modes (an MSYS2 `noacl` mount and "
                "some network mounts do). Your API key file, the daemon socket "
                "and the audit log are readable by other local users. Fix the "
                "mount (MSYS2: add `acl` in /etc/fstab) or keep the state "
                "directory on a filesystem that honours modes.",
                (unsigned long)(mode & 07777));
            jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                          "private files are NOT private on this filesystem",
                          detail);
        } else {
            char detail[300];
            /* What was actually exercised depends on the umask, and saying so is
             * the M305 rule: with umask 077 the probe is already 0600 before
             * jc_make_private runs, so the GUARANTEE is verified while the
             * TIGHTENING is not. Both are worth reporting, differently. */
            if ((created & 0077) == 0) {
                jc_snprintf(detail, sizeof(detail),
                            "mode read back as %04lo. Note: your umask (%04lo "
                            "on the probe) already created it private, so this "
                            "confirms the guarantee but did not exercise the "
                            "tightening itself",
                            (unsigned long)(mode & 07777),
                            (unsigned long)(created & 07777));
            } else {
                jc_snprintf(detail, sizeof(detail),
                            "created %04lo, tightened to %04lo, read back -- "
                            "verified rather than trusting chmod's return value",
                            (unsigned long)(created & 07777),
                            (unsigned long)(mode & 07777));
            }
            jc_doctor_add(&d, JC_DOC_OK, "private files really are private",
                          detail);
        }
    }

    /* Transport secrecy per configured model (M472). An `apiBase` of http:// sends
     * the API key over the network in cleartext, and nothing said so -- doctor's
     * posture checks covered whether a key EXISTS, not whether it is protected in
     * flight. That silence also amplified H1 (a MITM can inject the 302 that used
     * to walk the key off to another host) and H2 (an inherited socket carrying
     * plaintext is readable, where TLS ciphertext is not).
     *
     * Loopback is exempt and stays silent: `http://127.0.0.1` is the documented,
     * normal shape for a local model (docs/LOCAL_MODELS.md), there is no network
     * to sniff, and warning about it would train operators to ignore the check.
     * jc_net_host_is_blocked already answers "is this loopback or private" and is
     * unit-tested, so this reuses it rather than re-deciding what local means.
     *
     * FAIL under --unattended (M158b's explicit pattern): a supervised user can
     * see the warning and judge; an unattended loop shipping keys in cleartext
     * across a network should stop. */
    {
        int nm = jc_config_model_count(&app->config);
        int mi;
        int plain = 0;
        char detail[512];
        detail[0] = '\0';
        for (mi = 0; mi < nm; mi++) {
            struct jc_model_cfg *m = jc_config_model_at(&app->config, mi);
            char host[256];
            if (m == NULL || m->api_base == NULL) {
                continue;
            }
            if (strncmp(m->api_base, "http://", 7) != 0) {
                continue;
            }
            if (jc_url_host(m->api_base, host, sizeof(host)) == JC_OK &&
                jc_net_host_is_blocked(host)) {
                continue; /* loopback / private: a local model, not a leak */
            }
            plain++;
            if (detail[0] == '\0') {
                jc_snprintf(detail, sizeof(detail),
                            "%s uses http:// -- the API key crosses the network in "
                            "cleartext. Use https://, or point apiBase at a local "
                            "endpoint.",
                            m->name != NULL ? m->name : m->api_base);
            }
        }
        if (plain > 0) {
            jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                          "provider transport: plaintext http://", detail);
        } else if (nm > 0) {
            jc_doctor_add(&d, JC_DOC_OK,
                          "provider transport: https (or loopback)", NULL);
        }
    }

    /* Networking. */
#ifdef JC_HAVE_CURL
    jc_doctor_add(&d, JC_DOC_OK, "libcurl available (networking enabled)", NULL);
#else
    jc_doctor_add(&d, JC_DOC_FAIL, "built without libcurl (no networking)",
                  "rebuild with libcurl installed to reach model servers");
#endif

    /* Config sources (which file(s) were loaded / merged). */
    if (app->config.config_sources[0] != '\0') {
        jc_doctor_add(&d, JC_DOC_OK, "config source", app->config.config_sources);
    }

    /* Config + models. */
    n = jc_config_model_count(&app->config);
    if (n == 0) {
        jc_doctor_add(&d, JC_DOC_FAIL, "no models configured",
            "run jichi-convert on a Continue config, or create ~/.jichi");
    } else {
        jc_snprintf(detail, sizeof(detail), "%d model(s); active: %s (%s)", n,
                    app->config.model.name != NULL ? app->config.model.name
                                                   : "?",
                    app->config.model.model != NULL ? app->config.model.model
                                                    : "?");
        jc_doctor_add(&d, JC_DOC_OK, "configuration loaded", detail);

        if (app->config.model.model == NULL ||
            app->config.model.model[0] == '\0') {
            jc_doctor_add(&d, JC_DOC_FAIL, "active model has no model id",
                          "set \"model\" on the active model entry");
        }
        if (app->config.model.api_key != NULL &&
            app->config.model.api_key[0] != '\0') {
            jc_doctor_add(&d, JC_DOC_OK,
                          "API key present for the active model", NULL);
        } else {
            jc_doctor_add(&d, JC_DOC_WARN,
                "no API key for the active model",
                "ok for keyless local servers; else set apiKey / apiKeyEnv");
        }
        /* M194: no pricing at all => every cost number is silently zero. All
         * 4471 model calls in the dogfood log reported cost=$0.0000 because the
         * HRZ models declare no inputCostPer1M, and nothing said so: /cost, the
         * session exit total, the telemetry cost column and any cost-based
         * budget all read zero, which is indistinguishable from "this run was
         * free". This converts a meaningless number into a known-absent one.
         * Checked before the M31c cache-pricing warn below, which deliberately
         * only fires on a model that IS priced. */
        if (app->config.model.input_cost <= 0.0 &&
            app->config.model.output_cost <= 0.0) {
            jc_doctor_add(&d, JC_DOC_WARN,
                "no pricing for the active model: every cost reads $0.00",
                "set inputCostPer1M / outputCostPer1M so /cost, the exit total, "
                "the telemetry cost column and any cost budget mean something "
                "(a local/keyless server may genuinely be free -- then this is "
                "expected)");
        } else if (app->config.model.input_cost <= 0.0 ||
                   app->config.model.output_cost <= 0.0) {
            /* Half-priced is worse than unpriced: the total looks plausible and
             * is wrong by whichever side is missing. */
            jc_doctor_add(&d, JC_DOC_WARN,
                "only half the pricing is set for the active model",
                "set BOTH inputCostPer1M and outputCostPer1M; a partial pair "
                "makes cost totals look plausible while under-reporting");
        } else {
            jc_doctor_add(&d, JC_DOC_OK, "pricing set for the active model",
                          NULL);
        }
        /* M589: THE STALL TIMEOUT AGAINST THE LATENCY THIS MACHINE HAS
         * ALREADY MEASURED. jichi aborts a model stream that sends nothing for
         * `timeouts.stall` seconds -- 30 by default. That is right for a remote
         * tier answering in seconds and wrong for a local one that thinks.
         *
         * Measured 2026-08-25 on a local 9B: mean 14.3s per call, **max 228s**.
         * The project driving it set no `timeouts` block at all, so the 30s
         * default applied and every call in the latency tail was killed --
         * `error: model stalled (timed out)`, ten minutes into an autonomous
         * run. The number that predicted it was in this tool''s own telemetry,
         * written the night before, and nothing compared the two.
         *
         * So: compare them. The measurement is already on disk; this is the
         * reader it lacked, which is M584''s lesson in a third place. Warn only
         * when the evidence is real -- a handful of calls proves nothing about a
         * tail -- and name the flag, because a warning that states only a cause
         * is the message class that amplifies retries (M342/M360). */
        {
            struct jc_telemetry_summary ts;
            char tlabel[512];
            long t_connect = 0, t_stall = 0, t_request = 0;

            jc_config_resolve_timeouts(&app->config, &app->config.model,
                                       &t_connect, &t_stall, &t_request);
            tlabel[0] = '\0';
            if (t_stall > 0 &&
                jc_app_load_telemetry(app, &ts, tlabel, sizeof tlabel)) {
                jc_size mi;
                double worst = 0.0;
                long worst_calls = 0;
                const char *worst_name = NULL;
                for (mi = 0; mi < ts.models.len; mi++) {
                    const struct jc_telem_model *tm =
                        (const struct jc_telem_model *)jc_vec_at(
                            (struct jc_vec *)&ts.models, mi);
                    /* JC_DOCTOR_LAT_MIN_CALLS: below this the max is one slow
                     * call, not a tail, and a warning on it is noise. */
                    if (tm->lat_n >= JC_DOCTOR_LAT_MIN_CALLS &&
                        tm->lat_max > worst) {
                        worst = tm->lat_max;
                        worst_calls = tm->lat_n;
                        worst_name = tm->key;
                    }
                }
                if (worst > (double)t_stall * 1000.0) {
                    char msg[220], fix[320];
                    jc_snprintf(msg, sizeof msg,
                        "the stall timeout is %lds, but %s has answered as "
                        "slowly as %.0fs here",
                        t_stall, (worst_name != NULL) ? worst_name : "a model",
                        worst / 1000.0);
                    jc_snprintf(fix, sizeof fix,
                        "a call in that tail is aborted as a stall, mid-run. "
                        "Raise it with --timeout-stall <s> or a \"timeouts\": "
                        "{\"stall\": N} block (per-model overrides the global). "
                        "Measured over %ld call(s) in %s",
                        worst_calls, tlabel[0] != '\0' ? tlabel : "telemetry");
                    jc_doctor_add(&d, JC_DOC_WARN, msg, fix);
                } else if (worst > 0.0) {
                    char msg[220];
                    jc_snprintf(msg, sizeof msg,
                        "the stall timeout (%lds) clears the slowest measured "
                        "call (%.0fs, %ld call(s))",
                        t_stall, worst / 1000.0, worst_calls);
                    jc_doctor_add(&d, JC_DOC_OK, msg, NULL);
                }
                jc_telemetry_summary_free(&ts);
            }
        }

        /* M567: THE INTERFACE LANGUAGE AND THE VOICE THAT WILL READ IT.
         *
         * Reported by the operator, listening: jichi's German chrome came out
         * in English pronunciation. `1 ja` was heard as "one ya", `0 nein` as
         * "zero nine", `abgelehnt` as "ab-jeh-laynt" with an English soft g.
         * Measured with espeak-ng -q -x, which prints phonemes: the German
         * voice renders every one of those strings correctly, so THE TEXT WAS
         * NEVER THE PROBLEM. The voice was English because the desktop was
         * (LANG=en_US.UTF-8, speech-dispatcher DefaultLanguage commented out,
         * Orca's voices all `established: False`).
         *
         * WHY THIS CANNOT BE FIXED IN OUR OUTPUT, which is the whole reason it
         * belongs in `doctor` rather than in a renderer: A TERMINAL HAS NO
         * LANGUAGE CHANNEL. HTML says lang="de" and a reader switches voices;
         * a TTY carries bytes. So an application cannot tell a screen reader
         * what language it is printing, and no work on the strings will fix a
         * voice mismatch. Same conclusion as the Japanese finding (M556) by a
         * different mechanism: kanji had NO reading, German has the WRONG one.
         *
         * WHY THIS IS NOT NOISE. A mismatch takes a deliberate override --
         * $JICHI_LANG or config `language` pointing away from the locale --
         * because the locale is otherwise the last fallback and agrees with
         * itself. So this fires for a configuration somebody chose, which is
         * exactly when a warning is worth reading. It is NOT gated on
         * --accessible: a user who has not found that flag is the one who most
         * needs to be told, and a sighted reader is told plainly that it does
         * not affect them. */
        {
            const char *lang_env = getenv("LANG");
            enum jc_msg_lang ui;
            enum jc_msg_lang loc = JC_MSGL_EN;
            int loc_named;
            ui = jc_msg_lang_resolve(app->config.language,
                                     getenv("JICHI_LANG"), lang_env,
                                     jc_locale_is_utf8());
            loc_named = jc_msg_lang_match(lang_env, &loc);
            if (ui != JC_MSGL_EN && (!loc_named || loc != ui)) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "the interface language and the locale disagree",
                    "a screen reader takes its voice from the desktop, not "
                    "from the text -- so non-English chrome is read with "
                    "English pronunciation (\"1 ja\" becomes \"one ya\"). "
                    "Either set $LANG to match the interface, or set the "
                    "reader's voice to match it (for Orca: DefaultLanguage in "
                    "speech-dispatcher's speechd.conf, then restart both). "
                    "Harmless if you are reading the screen yourself.");
            } else if (ui == JC_MSGL_EN && loc_named && loc != JC_MSGL_EN &&
                       !jc_locale_is_utf8()) {
                /* The locale asks for a catalog we have and gets English
                 * anyway, because every non-English catalog is UTF-8 and
                 * mojibake is worse than English (jc_msg_lang_resolve). Worth
                 * saying: the user configured a language and did not get it. */
                jc_doctor_add(&d, JC_DOC_WARN,
                    "the locale names a language but the terminal is not UTF-8",
                    "the interface stays English on purpose: every translation "
                    "is UTF-8, and mojibake reads worse than English. Set a "
                    "UTF-8 locale (LANG=..._....UTF-8) to get the translation.");
            } else {
                jc_doctor_add(&d, JC_DOC_OK,
                              "interface language agrees with the locale", NULL);
            }
            /* AND THE ONE CASE WHERE A MATCHING LOCALE IS STILL NOT ENOUGH.
             * Measured at M556: espeak-ng -v ja has no readings for kanji and
             * says "Chinese letter" for each, or drops it -- 46 kanji in the ja
             * catalog, including the view option in the approval prompt. This
             * is about the DEFAULT synthesizer, not about every reader, and it
             * is phrased that way: probing the user's actual TTS stack is not
             * portable, and a wrong "you are missing a synthesizer" is worse
             * than silence. */
            if (ui == JC_MSGL_JA || ui == JC_MSGL_ZH) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "a CJK interface needs a synthesizer that reads Han "
                    "characters",
                    "the default Linux voice (espeak-ng) has no kanji/hanzi "
                    "readings and says \"Chinese letter\" for each one, or "
                    "drops it. For Japanese install open-jtalk-mecab-naist-jdic "
                    "and hts-voice-nitech-jp-atr503-m001, then AddModule "
                    "\"openjtalk\" in speechd.conf. Ignore this if you are not "
                    "using a screen reader.");
            }
        }
        /* Prompt caching on, but no cache pricing => cost numbers bill cached
         * reads at the full input rate (M31c). Only flag it when the model is
         * priced at all (else there's no cost tracking to skew). */
        if (app->config.model.prompt_cache != 0 &&
            app->config.model.input_cost > 0.0 &&
            app->config.model.cache_read_cost <= 0.0) {
            jc_doctor_add(&d, JC_DOC_WARN,
                "promptCache on but no cache pricing for the active model",
                "set cacheReadCostPer1M (and cacheWriteCostPer1M) so cost "
                "reflects the cheaper cached reads");
        }

        /* M326q: what this machine is, and one shipped key that silently does
         * nothing on it. `memBudgetMb` (documented at M325 as one of the four
         * knobs nobody could find) is enforced by an RSS watchdog that walks
         * /proc/<pid>/stat; where that is absent the budget NEVER FIRES, and
         * nothing said so. A safety key that is quietly inert is worse than
         * one that refuses.
         *
         * The platform line uses the NAME (for a human); the memBudget check
         * uses a CAPABILITY probe, because /proc can be missing on Linux (a
         * container or chroot) and the name would lie. */
        {
            char plat[160];
            if (jc_platform_describe(plat, sizeof plat)) {
                if (jc_platform_is_linux()) {
                    jc_doctor_add(&d, JC_DOC_OK, "platform", plat);
                } else {
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "platform is not Linux; jichi is developed and tested "
                        "there", plat);
                }
            }
            if (app->config.mem_budget_mb > 0 && !jc_have_proc_rss()) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "memBudgetMb is set but cannot be enforced here",
                    "the watchdog reads /proc/<pid>/stat, which this system "
                    "does not provide, so the budget never fires -- drop the "
                    "key, or bound the command with runTimeout instead");
            }
        }

        /* Security lint (M55b): a literal "apiKey" in the config (vs the
         * apiKeyEnv env-var indirection) is a smell — the value sits in
         * plaintext. Never print the key; just name the offending models. */
        {
            int li;
            int nm = jc_config_model_count(&app->config);
            struct jc_sb names;
            jc_sb_init(&names);
            for (li = 0; li < nm; li++) {
                struct jc_model_cfg *m = jc_config_model_at(&app->config, li);
                if (m != NULL && m->api_key_literal) {
                    if (names.len > 0) {
                        jc_sb_append(&names, ", ");
                    }
                    jc_sb_append(&names, m->name != NULL && m->name[0] != '\0'
                        ? m->name : (m->model != NULL ? m->model : "?"));
                }
            }
            if (names.len > 0) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "literal apiKey in config (prefer apiKeyEnv)",
                    names.data != NULL ? names.data : "");
            }
            jc_sb_free(&names);
        }

        /* #3: mixed providers/keys -- audit EVERY model's key, not just the
         * active one. A specialist/fallback model with no key 401s only when
         * orchestration switches to it mid-run; surface it up front. A keyless
         * entry is legitimate for a local server, so this is a WARN naming the
         * models (with their provider) rather than a failure. */
        {
            int li;
            int nm = jc_config_model_count(&app->config);
            struct jc_sb names;
            jc_sb_init(&names);
            for (li = 0; li < nm; li++) {
                struct jc_model_cfg *m = jc_config_model_at(&app->config, li);
                if (m != NULL &&
                    (m->api_key == NULL || m->api_key[0] == '\0')) {
                    if (names.len > 0) {
                        jc_sb_append(&names, ", ");
                    }
                    jc_sb_append(&names, m->name != NULL && m->name[0] != '\0'
                        ? m->name : (m->model != NULL ? m->model : "?"));
                    if (m->provider != NULL) {
                        jc_sb_append(&names, " (");
                        jc_sb_append(&names, m->provider);
                        jc_sb_append(&names, ")");
                    }
                }
            }
            if (names.len > 0) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "model(s) with no API key (fine for local servers; else set "
                    "apiKey/apiKeyEnv before orchestration uses them)",
                    names.data != NULL ? names.data : "");
            }
            jc_sb_free(&names);
        }

        /* M326e: an `apiKeyEnv` no shell can export -- almost always a pasted
         * key, from a `setup` prompt that used to invite exactly that. It is a
         * FAIL, not a WARN: getenv() can never find such a name, so the model
         * has no key and every call 401s. The "no API key" WARN above already
         * fires, but it advises "set apiKey/apiKeyEnv" -- which the user
         * believes they did. This names the actual mistake.
         *
         * The detail names the MODEL and never the value: if the diagnosis is
         * right the value is the key, and a doctor report is the single most
         * likely thing to be pasted into an issue. */
        {
            int li;
            int nm = jc_config_model_count(&app->config);
            struct jc_sb bad;
            jc_sb_init(&bad);
            for (li = 0; li < nm; li++) {
                struct jc_model_cfg *m = jc_config_model_at(&app->config, li);
                if (m == NULL || m->api_key_env == NULL ||
                    m->api_key_env[0] == '\0' ||
                    jc_envvar_name_valid(m->api_key_env)) {
                    continue;
                }
                if (bad.len > 0) {
                    jc_sb_append(&bad, ", ");
                }
                jc_sb_append(&bad, m->name != NULL && m->name[0] != '\0'
                    ? m->name : (m->model != NULL ? m->model : "?"));
            }
            if (bad.len > 0) {
                /* One finding, not two: a fix belongs in the detail, and a
                 * second jc_doctor_add would count the same defect twice in
                 * the "N problems" tally. */
                jc_sb_append(&bad, " -- apiKeyEnv names a VARIABLE, so put the "
                    "key in one: \"apiKeyEnv\": \"JICHI_API_KEY\" + export "
                    "JICHI_API_KEY='<your key>'");
                jc_doctor_add(&d, JC_DOC_FAIL,
                    "apiKeyEnv is not a usable environment-variable name "
                    "(it looks like the key itself; not shown here on purpose)",
                    bad.data != NULL ? bad.data : "");
            }
            jc_sb_free(&bad);
        }

        /* M149: a toolCalling:none active model can't run tools. Note the
         * degraded posture (info), and WARN if it's paired with autonomy
         * machinery (verify/testCommand/routing) that assumes tool use. */
        if (app->config.model.tool_calling == 1) {
            int autonomy =
                (app->config.verify != NULL && app->config.verify[0] != '\0') ||
                (app->config.test_command != NULL &&
                 app->config.test_command[0] != '\0') ||
                app->config.routing.escalate_on_verify;
            if (autonomy) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "active model is toolCalling: none but autonomy is "
                    "configured",
                    "a no-tools model can't edit/verify; point verify/routing "
                    "at a tool-capable model or clear toolCalling");
            } else {
                jc_doctor_add(&d, JC_DOC_OK,
                    "active model is toolCalling: none (Q&A/plan agent)",
                    "tools are not advertised; skills + context still load");
            }
        }

        /* M155: privilege posture lints. (1) Running as root makes the whole
         * privileged-command policy moot -- the agent's shell is already
         * uid 0. (2) privilegedCommands:allow lets the model run sudo with no
         * prompt. (3) the audit being off removes the record of what ran.
         * M158b: under --unattended these escalate WARN -> FAIL, so the exit
         * code (1 iff any FAIL) can gate a loop supervisor's startup. */
        if (geteuid() == 0) {
            jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                "running as root -- the privileged-command policy is moot",
                "run jichi as a dedicated non-root user without passwordless "
                "sudo; the OS boundary is the real control");
        }
        if (app->config.privileged_commands == JC_PRIVPOL_ALLOW) {
            jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                "privilegedCommands: allow (model may run sudo unprompted)",
                "prefer \"ask\" (or \"deny\"); use privilegedCommandsAllow to "
                "pre-approve specific commands instead");
        }
        if (!app->config.privileged_audit) {
            jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                "privilegedAudit: off (privileged commands are not recorded)",
                "leave it on; the log is owner-only and best-effort");
        }

        /* M163a: kinetic (physical-actuation) posture lints, mirroring the
         * privilege ones. Only relevant when kinetic tools/servers exist, but
         * the posture/audit checks are cheap and worth flagging regardless. */
        {
            jc_size ti;
            int has_kinetic = 0;
            int kin_ro = 0, kin_shellform = 0, kin_in_ws = 0;
            char kroot[JC_PATH_MAX];
            const struct jc_vec *ut = &app->config.user_tools;
            if (jc_path_resolve(app->cwd, kroot, sizeof(kroot)) != JC_OK) {
                kroot[0] = '\0';
            }
            for (ti = 0; ti < ut->len; ti++) {
                const struct jc_user_tool_cfg *c =
                    (const struct jc_user_tool_cfg *)
                    jc_vec_at((struct jc_vec *)ut, ti);
                if (!c->kinetic) continue;
                has_kinetic = 1;
                if (c->readonly) kin_ro = 1;   /* loader clears this, defensive */
                if (c->command == NULL && c->shell != NULL) kin_shellform = 1;
                if (c->command != NULL && kroot[0] != '\0') {
                    char cres[JC_PATH_MAX];
                    if (jc_path_resolve(c->command, cres, sizeof(cres))
                            == JC_OK &&
                        jc_path_under_root(kroot, cres)) {
                        kin_in_ws = 1;
                    }
                }
            }
            if (app->config.kinetic_commands == JC_PRIVPOL_ALLOW) {
                jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                    "kineticCommands: allow (model may actuate hardware "
                    "unprompted)",
                    "prefer \"ask\" (or \"deny\"); allowlist a safe-state tool "
                    "(e.g. stop_all) via kineticCommandsAllow instead");
            }
            if (has_kinetic && !app->config.kinetic_audit) {
                jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN,
                    "kineticAudit: off (physical actions are not recorded)",
                    "leave it on; the log is owner-only and best-effort");
            }
            if (kin_ro) {
                jc_doctor_add(&d, JC_DOC_FAIL,
                    "a kinetic tool is marked readonly (contradiction)",
                    "a kinetic tool actuates hardware; remove readonly:true");
            }
            if (kin_shellform) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "a kinetic tool uses shell form (cannot be shadow-matched "
                    "against direct shell invocation)",
                    "prefer command/args form, or list its binary in "
                    "kineticShellPrefixes");
            }
            if (kin_in_ws) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "a kinetic tool's command lives inside the workspace "
                    "(model-editable script)",
                    "keep device scripts outside the workspace by absolute "
                    "path so the path fence protects them");
            }
        }

        /* M158b: the unattended-loop posture profile. Only judged when asked
         * (`doctor --unattended`) -- these are the controls an unsupervised
         * looped run needs but an interactive session legitimately may not
         * (see docs/AUTONOMOUS_LOOPS.md). Unsafe => FAIL (gates the loop);
         * risky-but-legitimate => WARN. */
        if (unattended) {
            if (app->config.path_fence == 0) {
                jc_doctor_add(&d, JC_DOC_FAIL,
                    "unattended: pathFence is explicitly off",
                    "set pathFence: true (or rely on the --auto default); use "
                    "referenceRoots for read-only trees outside the workspace");
            } else {
                jc_doctor_add(&d, JC_DOC_OK,
                    app->config.path_fence == 1
                        ? "unattended: path fence on"
                        : "unattended: path fence auto (on under --auto)",
                    NULL);
            }
            if ((app->config.verify == NULL ||
                 app->config.verify[0] == '\0') &&
                (app->config.test_command == NULL ||
                 app->config.test_command[0] == '\0')) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "unattended: no verify/testCommand configured",
                    "without a gate a bad change is kept, not rolled back; "
                    "set verify (or pass --verify per run)");
            } else {
                jc_doctor_add(&d, JC_DOC_OK,
                    "unattended: a verify gate is resolvable", NULL);
            }
            if (app->config.edit_scope.len == 0) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "unattended: no editScope configured",
                    "writes may touch the whole workspace; narrow them with "
                    "editScope (or --edit-scope per run)");
            }
            if (!app->config.snapshots) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "unattended: snapshots disabled (no rollback-to-green)",
                    "the verify gate cannot restore a red tree without "
                    "checkpoints; enable snapshots");
            }
        }

        /* M55a: routing.escalateOnVerify only helps if a verifier runs; warn
         * when it's on but neither verify nor testCommand is set.
         *
         * M203: ...and only when ROUTING ITSELF resolves. escalate_on_verify
         * defaults to 1, so on any config WITHOUT routing.fast/routing.strong
         * this warned about a setting that can never fire: routing is off, so
         * there is nothing to escalate. Found by configuring a real server and
         * reading doctor's own output -- two warnings, one of them inapplicable.
         * A lint that cries wolf gets ignored wholesale, which costs more than
         * the check is worth. */
        {
            int rt_fast = -1;
            int rt_strong = -1;
            if (jc_config_routing_resolve(&app->config, &rt_fast, &rt_strong) &&
                app->config.routing.escalate_on_verify &&
                (app->config.verify == NULL ||
                 app->config.verify[0] == '\0') &&
                (app->config.test_command == NULL ||
                 app->config.test_command[0] == '\0')) {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "routing.escalateOnVerify on but no verifier configured",
                    "set \"verify\" (or \"testCommand\") so --auto can verify "
                    "and escalate on failure");
            }
        }

        /* M288: a wide-window strong tier that a global `contextLimit` has
         * flattened. Context-pressure escalation compares EFFECTIVE windows, and
         * a top-level `contextLimit` overrides every model's `contextLength` --
         * so declaring a 256k strong tier next to a 150k fast one and then
         * pinning `contextLimit: 128000` leaves both tiers equally roomy and the
         * trigger permanently inert. jichi will not quietly reinterpret an
         * explicit budget, so it says so instead. This is a real config found in
         * the wild, and the `contextLimit` was set on guidance that M286
         * obsoleted (the byte estimate used to run ~2x optimistic, so halving the
         * window was prudent; it is now calibrated). */
        {
            int rt_fast = -1;
            int rt_strong = -1;
            if (app->config.context_limit > 0 &&
                app->config.routing.escalate_on_context > 0 &&
                jc_config_routing_resolve(&app->config, &rt_fast, &rt_strong)) {
                const struct jc_model_cfg *mf = (const struct jc_model_cfg *)
                    jc_vec_at(&app->config.models, (jc_size)rt_fast);
                const struct jc_model_cfg *ms = (const struct jc_model_cfg *)
                    jc_vec_at(&app->config.models, (jc_size)rt_strong);
                if (mf != NULL && ms != NULL &&
                    ms->context_limit > mf->context_limit &&
                    mf->context_limit > 0) {
                    jc_snprintf(detail, sizeof(detail),
                        "the strong tier declares %ld tokens vs the fast tier's "
                        "%ld, but \"contextLimit\": %ld caps both -- so "
                        "escalating for room gains nothing and never fires. Drop "
                        "the global \"contextLimit\" so each model's "
                        "\"contextLength\" applies (the estimate is calibrated "
                        "since M286). RAISING it does not help: it overrides "
                        "every model, so the tiers stay equal to each other.",
                        ms->context_limit, mf->context_limit,
                        app->config.context_limit);
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "contextLimit flattens the routing tiers' windows",
                        detail);
                }
            }
        }

        /* M73: context-budget safety. (1) When neither the active model's
         * `contextLength` nor a top-level `contextLimit` is set, jichi assumes a
         * default window; if the server's real context is smaller, every turn
         * overflows. (2) When the instruction files are large relative to the
         * effective window, the system-prompt fit will truncate them. */
        {
            long limit = jc_compact_context_limit(app);
            char *rules;
            long rt;

            if (app->config.model.context_limit <= 0 &&
                app->config.context_limit <= 0) {
                jc_snprintf(detail, sizeof(detail),
                    "jichi assumes ~%ld tokens; if the server's real window is "
                    "smaller, prompts overflow. Declare \"contextLength\" on "
                    "the model, or set \"contextLimit\" / --context-limit.",
                    (long)JC_COMPACT_DEFAULT_LIMIT);
                jc_doctor_add(&d, JC_DOC_WARN,
                    "active model declares no contextLength", detail);
            }

            rules = jc_rules_load(app);
            rt = jc_compact_estimate_text_cal(app, rules);
            if (limit > 0 && rt > limit / 2) {
                jc_snprintf(detail, sizeof(detail),
                    "instruction files ~%ld tok vs context ~%ld; with the repo "
                    "map + tools they may overflow. jichi truncates rules/repo "
                    "map to fit (M73) -- consider a larger-context model, a "
                    "smaller repoMapLimit, repoMap:false / --lite, or trimming "
                    "AGENTS.md/CLAUDE.md.", rt, limit);
                jc_doctor_add(&d, JC_DOC_WARN,
                    "instruction files large for the model context", detail);
            }

            /* M74: which tool profile the top-level turn will advertise. */
            if (jc_config_tool_profile_core(&app->config, limit)) {
                jc_doctor_add(&d, JC_DOC_OK,
                    "tool profile: core (lean tool set for a small context)",
                    "set toolProfile / --tool-profile full to advertise all "
                    "tools");
                /* The core set is the built-in essentials only; configured
                 * MCP/user/LSP tools are silently dropped. That is surprising
                 * to someone who wired up an MCP server, so name it. */
                if (app->config.mcp_servers.len > 0 ||
                    app->config.user_tools.len > 0 ||
                    app->config.lsp_servers.len > 0) {
                    jc_snprintf(detail, sizeof(detail),
                        "core advertises only built-in essentials; your "
                        "configured MCP (%lu), user (%lu) and LSP (%lu) tools "
                        "are NOT sent to the model. Raise contextLength to "
                        ">= %d or set toolProfile:full to use them.",
                        (unsigned long)app->config.mcp_servers.len,
                        (unsigned long)app->config.user_tools.len,
                        (unsigned long)app->config.lsp_servers.len,
                        JC_TOOL_PROFILE_AUTO_BELOW);
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "configured tools dropped by the core tool profile",
                        detail);
                }
            } else {
                jc_doctor_add(&d, JC_DOC_OK, "tool profile: full", NULL);
            }
        }
    }

#ifdef JC_HAVE_CURL
    /* Server reachability (active model failing is fatal; others warn). */
    for (i = 0; i < n; i++) {
        struct jc_model_cfg *m = jc_config_model_at(&app->config, i);
        int active = (i == app->config.active);
        int up = jc_net_reachable(m->api_base, m->api_key, 3, &app->abort_flag);
        if (up) {
            jc_snprintf(detail, sizeof(detail), "%s: %s",
                        m->name != NULL ? m->name : "?",
                        m->api_base != NULL ? m->api_base : "(provider default)");
            jc_doctor_add(&d, JC_DOC_OK, "model server reachable", detail);
        } else {
            jc_snprintf(detail, sizeof(detail), "%s: %s",
                        m->name != NULL ? m->name : "?",
                        m->api_base != NULL ? m->api_base : "(provider default)");
            jc_doctor_add(&d, active ? JC_DOC_FAIL : JC_DOC_WARN,
                active ? "active model's server is unreachable"
                       : "a configured model's server is unreachable",
                detail);
        }

        /* The window the SERVER publishes, against the one this config
         * DECLARES. Only for the active model, and only when it answered:
         * asking an unreachable server would restate the failure above as a
         * second, vaguer one.
         *
         * This is the check jc_agent.c's under-declared-window warning wishes
         * it could have done BEFORE the run instead of inferring it afterwards
         * from an oversized request the server happened to accept. Where a
         * gateway publishes max_input_tokens, a wrong contextLength is a fact
         * jichi can state up front rather than a suspicion it reaches after
         * seven wasted compactions.
         */
        if (active && up) {
            long srv_in = 0;
            long srv_out = 0;
            long http = 0;
            jc_status ms = jc_net_model_limits(m->api_base, m->api_key,
                                               m->model, 6, &app->abort_flag,
                                               &srv_in, &srv_out, &http);
            if (ms == JC_OK && srv_in > 0) {
                if (m->context_limit <= 0) {
                    jc_snprintf(detail, sizeof(detail),
                        "%s publishes max_input_tokens %ld; with no "
                        "contextLength jichi budgets %d, so it would compact "
                        "toward a target %ld tokens too small",
                        m->model != NULL ? m->model : "?", srv_in,
                        JC_COMPACT_DEFAULT_LIMIT,
                        srv_in - (long)JC_COMPACT_DEFAULT_LIMIT);
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "the server publishes a context window, the config "
                        "declares none", detail);
                } else if (m->context_limit > srv_in) {
                    jc_snprintf(detail, sizeof(detail),
                        "%s: contextLength %ld exceeds the server's "
                        "max_input_tokens %ld -- a full budget invites an "
                        "HTTP 400 rather than a compaction",
                        m->model != NULL ? m->model : "?",
                        m->context_limit, srv_in);
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "declared context window is larger than the server's",
                        detail);
                } else if (m->context_limit * 2 <= srv_in) {
                    jc_snprintf(detail, sizeof(detail),
                        "%s: contextLength %ld against the server's %ld -- "
                        "jichi will compact earlier than it needs to",
                        m->model != NULL ? m->model : "?",
                        m->context_limit, srv_in);
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "declared context window is much smaller than the "
                        "server's", detail);
                } else {
                    jc_snprintf(detail, sizeof(detail),
                        "%s: contextLength %ld, server max_input_tokens %ld",
                        m->model != NULL ? m->model : "?",
                        m->context_limit, srv_in);
                    jc_doctor_add(&d, JC_DOC_OK,
                        "declared context window agrees with the server",
                        detail);
                }
            } else if ((ms == JC_ERR_HTTP && http == 404) ||
                       ms == JC_ERR_PARSE) {
                /* "This server has no such endpoint", in the two ways servers
                 * actually say it. A 404 is the polite one. LM Studio answers
                 * HTTP **200** with {"error":"Unexpected endpoint or method.
                 * (GET /v1/model/info)"} -- measured against 134.176.150.160 --
                 * so the STATUS alone cannot separate absence from failure and
                 * the SHAPE has to (the M476 rule, met again in a new costume).
                 * Treating the 200-with-an-error case as a warning would fire on
                 * nearly every non-LiteLLM server, which trains people to ignore
                 * warnings. Reported as a fact about the endpoint, NOT a problem
                 * with this config -- but reported, because a reader must be able
                 * to tell "nothing to check" from "not checked" (M458).
                 */
                jc_snprintf(detail, sizeof(detail),
                    "%s has no /v1/model/info, so the window cannot be "
                    "verified -- declare contextLength from the model's docs",
                    m->api_base != NULL ? m->api_base : "?");
                jc_doctor_add(&d, JC_DOC_OK,
                    "this server does not publish model limits", detail);
            } else if (ms == JC_ERR_NOTFOUND) {
                jc_snprintf(detail, sizeof(detail),
                    "the endpoint answered but lists no limit for %s",
                    m->model != NULL ? m->model : "?");
                jc_doctor_add(&d, JC_DOC_OK,
                    "the server publishes limits, but not for this model",
                    detail);
            } else {
                jc_snprintf(detail, sizeof(detail),
                    "%s: %s (HTTP %ld) -- the window was NOT checked",
                    m->model != NULL ? m->model : "?", jc_status_str(ms),
                    http);
                jc_doctor_add(&d, JC_DOC_WARN,
                    "could not read the server's model limits", detail);
            }
        }
    }

    /* Live tool-calling probe (M167, opt-in `--live`). One real request that
     * advertises a single trivial tool and asks the model to call it, then
     * classifies the answer. Two jobs: it sets the per-model `toolCalling` flag
     * by observation instead of by hand, and -- the reason it exists -- it is an
     * end-to-end self-test of jichi's own request construction. Run against the
     * pre-M166 build, this reports `none` for a model that demonstrably supports
     * native calling, which is why the advice for that case points at the
     * request before the model (see jc_toolprobe_advice). */
    if (live && n > 0) {
        struct jc_model_cfg *m = jc_config_model_at(&app->config,
                                                    app->config.active);
        /* `doctor` runs before the chat provider is built (it is a diagnostic,
         * not a session), so make one just for the probe and free it after. */
        struct jc_provider *pp = (m != NULL) ? jc_provider_create(m) : NULL;
        if (m == NULL) {
            jc_doctor_add(&d, JC_DOC_WARN, "--live: no active model", NULL);
        } else if (pp == NULL) {
            jc_doctor_add(&d, JC_DOC_WARN, "--live: could not create a provider "
                          "for the active model", NULL);
        } else {
            struct jc_oneshot_result res;
            cJSON *tools = probe_tool_array();
            jc_status pst = jc_oneshot_probe(pp, NULL,
                                             JC_TOOLPROBE_PROMPT, tools,
                                             60, &app->abort_flag, &res);
            if (tools != NULL) {
                cJSON_Delete(tools);
            }
            if (pst != JC_OK) {
                jc_snprintf(detail, sizeof(detail),
                            "%s: the probe request did not complete (%s%s)",
                            m->name != NULL ? m->name : "?",
                            jc_status_str(pst),
                            res.http_status >= 400 ? ", HTTP error" : "");
                jc_doctor_add(&d, JC_DOC_FAIL,
                              "--live: tool-calling probe failed", detail);
            } else {
                enum jc_toolprobe_verdict v =
                    jc_toolprobe_classify(res.ncalls, res.call_name, res.text);
                /* tool_calling is 0=native / 1=none (M149). */
                const char *cfg = (m->tool_calling == 1) ? "none"
                                                         : "native";
                const char *advice = jc_toolprobe_advice(v, cfg);
                char headline[220];
                jc_snprintf(headline, sizeof(headline),
                            "--live: tool calling observed \"%s\" (configured "
                            "\"%s\")",
                            jc_toolprobe_verdict_str(v),
                            (cfg != NULL && cfg[0] != '\0') ? cfg : "native");
                /* Report the server's own prompt-token count next to the
                 * verdict: it is the real tokenizer's number, and every context
                 * decision keyed off the byte/4 estimate is calibrated against
                 * it (docs/COMPACTION.md). Free evidence from a request we are
                 * making anyway. */
                if (res.in_tokens > 0.0) {
                    jc_snprintf(detail, sizeof(detail),
                                "%s -- probe prefix was %.0f real prompt tokens",
                                advice, res.in_tokens);
                } else {
                    jc_snprintf(detail, sizeof(detail), "%s", advice);
                }
                jc_doctor_add(&d,
                    jc_toolprobe_is_failure(v, cfg) ? JC_DOC_FAIL
                        : (v == JC_TOOLPROBE_NATIVE ? JC_DOC_OK : JC_DOC_WARN),
                    headline, detail);
            }
            jc_oneshot_result_free(&res);
        }
        if (pp != NULL) {
            pp->vt->free(pp);
        }
    }
#else
    if (live) {
        jc_doctor_add(&d, JC_DOC_WARN,
                      "--live: built without libcurl, cannot probe", NULL);
    }
#endif

    /* Role coverage (embed/rerank gate semantic search; others fall back). */
    if (n > 0) {
        int embed = jc_config_find_by_role(&app->config, JC_ROLE_EMBED);
        int rerank = jc_config_find_by_role(&app->config, JC_ROLE_RERANK);
        if (embed >= 0) {
            jc_doctor_add(&d, JC_DOC_OK,
                "embed-role model present (codebase_search / index enabled)",
                NULL);
            if (rerank < 0) {
                jc_doctor_add(&d, JC_DOC_WARN, "no rerank-role model",
                    "search works without it, but results are coarser");
            }
        } else {
            jc_doctor_add(&d, JC_DOC_WARN, "no embed-role model",
                "semantic codebase_search / index disabled; add a model with "
                "role \"embed\"");
        }
        /* Auto-context (M61) needs an embed-role model to retrieve. */
        if (app->config.auto_context) {
            if (embed >= 0) {
                jc_doctor_add(&d, JC_DOC_OK,
                    "auto-context on (retrieves codebase/docs each turn)", NULL);
            } else {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "auto-context on but no embed-role model",
                    "nothing will be retrieved; add a model with role \"embed\"");
            }
        }
        /* Media-generation roles are optional: note when present, stay quiet
         * otherwise (the tools simply aren't registered). M32. */
        if (jc_config_find_by_role(&app->config, JC_ROLE_IMAGE) >= 0) {
            jc_doctor_add(&d, JC_DOC_OK,
                "image-role model present (generate_image enabled)", NULL);
        }
        if (jc_config_find_by_role(&app->config, JC_ROLE_AUDIO) >= 0) {
            jc_doctor_add(&d, JC_DOC_OK,
                "audio-role model present (generate_audio enabled)", NULL);
        }
        if (jc_config_find_by_role(&app->config, JC_ROLE_TRANSCRIBE) >= 0) {
            jc_doctor_add(&d, JC_DOC_OK,
                "transcribe-role model present (transcribe_audio enabled)", NULL);
        }
        /* Session-store size (M219, the warning half of the M197 deferral):
         * retention is fixed, but a big store still costs parse time on every
         * listing and disk forever. Advisory only. */
        {
            long sfiles = 0;
            double sbytes = 0.0;
            jc_session_store_stats(&sfiles, &sbytes);
            if (sfiles > 500 || sbytes > 100.0 * 1024.0 * 1024.0) {
                char sd[96];
                jc_snprintf(sd, sizeof(sd),
                            "%ld files, %.0f MB -- `jichi prune --keep N` or "
                            "`--older-than 90d`",
                            sfiles, sbytes / (1024.0 * 1024.0));
                jc_doctor_add(&d, JC_DOC_WARN, "session store is large", sd);
            }
        }
        /* External docs sources (M34a) gate search_docs / @docs; they need an
         * embed-role model to index. */
        if (app->config.docs.len > 0) {
            char dd[96];
            jc_snprintf(dd, sizeof(dd), "%lu source%s",
                        (unsigned long)app->config.docs.len,
                        app->config.docs.len == 1 ? "" : "s");
            if (embed >= 0) {
                jc_doctor_add(&d, JC_DOC_OK,
                    "docs sources configured (search_docs / @docs enabled)", dd);
            } else {
                jc_doctor_add(&d, JC_DOC_WARN,
                    "docs sources configured but no embed-role model",
                    "search_docs / @docs disabled; add a model with role "
                    "\"embed\"");
            }
        }

        /* #6: named @ref aliases -- list names + types only, NEVER values
         * (secret aliases carry env/cmd, not the secret itself, but names +
         * types are safe to surface for debuggability). */
        if (app->config.aliases.len > 0) {
            struct jc_sb al;
            jc_size ai;
            jc_sb_init(&al);
            for (ai = 0; ai < app->config.aliases.len; ai++) {
                const struct jc_alias_cfg *a1 =
                    (const struct jc_alias_cfg *)jc_vec_at(
                        &app->config.aliases, ai);
                if (al.len > 0) {
                    jc_sb_append(&al, ", ");
                }
                jc_sb_append(&al, a1->name != NULL ? a1->name : "?");
                jc_sb_append(&al, ":");
                jc_sb_append(&al, a1->type != NULL ? a1->type : "?");
            }
            jc_doctor_add(&d, JC_DOC_OK, "reference aliases (@ref:<name>)",
                          al.data != NULL ? al.data : "");
            jc_sb_free(&al);
        }
    }

    /* git + snapshots. */
    if (jc_tool_git_available(app->cwd)) {
        jc_doctor_add(&d, JC_DOC_OK,
                      "git repository (git tools available)", NULL);
    } else {
        jc_doctor_add(&d, JC_DOC_WARN, "not a git repository",
                      "git tools are off; snapshots/undo need git");
    }
    if (app->config.snapshots) {
        if (cmd_on_path("git")) {
            jc_doctor_add(&d, JC_DOC_OK, "snapshots enabled (git found)", NULL);
        } else {
            jc_doctor_add(&d, JC_DOC_FAIL,
                "snapshots enabled but git is not on PATH",
                "install git, or set \"snapshots\": false");
        }
    } else {
        jc_doctor_add(&d, JC_DOC_WARN, "snapshots disabled",
                      "set \"snapshots\": true to enable checkpoints / undo");
    }
    /* M335: report the shadow-checkpoint store here, because `doctor` is run
     * routinely and `checkpoints gc` is not. A cleanup command nobody knows to
     * reach for is the same failure as the multi-toolchain gate nobody ran
     * (ANECDOTES #47): discoverability is part of the feature. Measured on the
     * author's machine before this existed: 21 repos, 10 orphaned, 61 MB. */
    {
        char st_root[600];
        struct jc_vec st_names;
        const char *hm = getenv("HOME");
        int n_repo = 0;
        jc_snprintf(st_root, sizeof(st_root), "%s/.jichi.d/checkpoints",
                    hm != NULL ? hm : ".");
        jc_vec_init(&st_names, sizeof(char *));
        if (jc_list_dir(st_root, &st_names, jc_app_scratch(app)) == JC_OK) {
            jc_size si;
            for (si = 0; si < st_names.len; si++) {
                char sd[700];
                jc_snprintf(sd, sizeof(sd), "%s/%s", st_root,
                            *(char **)jc_vec_at(&st_names, si));
                if (jc_is_dir(sd)) {
                    n_repo++;
                }
            }
        }
        if (n_repo > 0) {
            char sdet[160];
            jc_snprintf(sdet, sizeof(sdet),
                        "%d shadow repo(s); `jichi checkpoints gc` reports which "
                        "are orphaned", n_repo);
            jc_doctor_add(&d, JC_DOC_OK, "checkpoint store", sdet);
        }
    }
    {
        long rt = jc_config_run_timeout(0, app->config.run_timeout_cli,
                                        app->config.run_timeout);
        if (rt > 0) {
            char dt[48];
            jc_snprintf(dt, sizeof(dt), "%lds wall-clock cap", rt);
            jc_doctor_add(&d, JC_DOC_OK, "shell-command timeout", dt);
        } else {
            jc_doctor_add(&d, JC_DOC_OK, "shell-command timeout: none",
                          "set \"runTimeout\" or --run-timeout to bound a "
                          "hangable build/test");
        }
    }

    /* PDF text extraction (M42): on-demand, so a missing tool is only a WARN. */
    {
        const char *pdfcmd = (app->config.pdf_command != NULL &&
                              app->config.pdf_command[0] != '\0')
                                 ? app->config.pdf_command : "pdftotext";
        if (cmd_on_path(pdfcmd)) {
            char ok[128];
            jc_snprintf(ok, sizeof(ok), "PDF extractor found (%s)", pdfcmd);
            jc_doctor_add(&d, JC_DOC_OK, ok, NULL);
        } else {
            jc_doctor_add(&d, JC_DOC_WARN, "no PDF extractor on PATH",
                "reading .pdf files needs poppler's pdftotext (or set "
                "\"pdfCommand\")");
        }
    }

    /* Formatter (M263): only when one is configured -- format_file falls back
     * to it for files no language server formats, so a missing binary is a
     * WARN, not a failure. The check takes the command's first word. */
    if (app->config.format_command != NULL &&
        app->config.format_command[0] != '\0') {
        char first[256];
        char note[512];
        jc_size i = 0;
        const char *fc = app->config.format_command;
        while (fc[i] != '\0' && fc[i] != ' ' && i + 1 < sizeof(first)) {
            first[i] = fc[i];
            i++;
        }
        first[i] = '\0';
        if (cmd_on_path(first)) {
            jc_snprintf(note, sizeof(note), "formatter found (%s)", first);
            jc_doctor_add(&d, JC_DOC_OK, note, NULL);
        } else {
            jc_snprintf(note, sizeof(note),
                        "formatCommand names '%s', which is not on PATH", first);
            jc_doctor_add(&d, JC_DOC_WARN, note,
                "format_file will fail until it is installed (or drop the key)");
        }
    }

    /* Music toolchain (M186): only when the project's own gates name
     * lilypond (the music pack's config.example.json wires `verify` to an
     * engraving check), so unrelated projects never see the line. */
    {
        const char *v = app->config.verify;
        const char *tc = app->config.test_command;
        if ((v != NULL && strstr(v, "lilypond") != NULL) ||
            (tc != NULL && strstr(tc, "lilypond") != NULL)) {
            if (cmd_on_path("lilypond")) {
                jc_doctor_add(&d, JC_DOC_OK, "lilypond found (engraving)",
                              NULL);
            } else {
                jc_doctor_add(&d, JC_DOC_WARN, "lilypond not on PATH",
                    "the verify/testCommand names lilypond; install it "
                    "(e.g. apt install lilypond) or the gate cannot run");
            }
        }
    }

    /* Path-containment fence (M24). */
    if (app->config.path_fence == 1) {
        jc_doctor_add(&d, JC_DOC_OK, "path fence on (all modes)", NULL);
    } else if (app->config.path_fence == 0) {
        jc_doctor_add(&d, JC_DOC_WARN, "path fence off",
                      app->config.edit_scope.len > 0
                      ? "file tools may read/write outside the workspace "
                        "(editScope still bounds --auto writes)"
                      : "file tools may read/write outside the workspace; "
                        "set editScope to bound --auto writes (or referenceRoots "
                        "+ fence on for read-only external trees)");
    } else {
        jc_doctor_add(&d, JC_DOC_OK, "path fence auto",
                      "active only in autonomous postures (--auto)");
    }

    /* Reference roots (M54): read-allowed external trees with the fence on. */
    if (app->config.reference_roots.len > 0) {
        struct jc_sb det;
        jc_size i;
        int missing = 0;
        jc_sb_init(&det);
        for (i = 0; i < app->config.reference_roots.len; i++) {
            const char *rr =
                *(char **)jc_vec_at(&app->config.reference_roots, i);
            if (rr == NULL || rr[0] == '\0') {
                continue;
            }
            if (det.len > 0) {
                jc_sb_append(&det, ", ");
            }
            jc_sb_append(&det, rr);
            if (!jc_is_dir(rr)) {
                jc_sb_append(&det, " (not a directory)");
                missing++;
            }
        }
        jc_doctor_add(&d, missing > 0 ? JC_DOC_WARN : JC_DOC_OK,
                      missing > 0 ? "reference roots: some not found"
                                  : "reference roots (reads allowed)",
                      det.data != NULL ? det.data : "");
        jc_sb_free(&det);
    }

    /* Edit scope + STATE-THE-REACH (M387): if an edit scope is configured, say
     * plainly that it fences the FILE TOOLS only -- the shell reaches past it
     * and is detected after the fact, not prevented (GATE_INTEGRITY.md §9). An
     * operator who reads the fence as a wall is the exact surprise this
     * corrects; informational, not a warning, because the scope is working as
     * designed. */
    if (app->config.edit_scope.len > 0) {
        jc_doctor_add(&d, JC_DOC_OK, "edit scope: fences the file tools",
                      "a shell command (run_terminal_command) can reach past "
                      "it and is detected afterward, not prevented -- keep "
                      "unattended shell use bounded (see GATE_INTEGRITY.md)");
    }

    /* MCP servers: actually connect and report. */
    n = (int)app->config.mcp_servers.len;
    if (n == 0) {
        jc_doctor_add(&d, JC_DOC_OK, "MCP: none configured", NULL);
    } else {
        struct jc_mcp_manager mcp;
        struct jc_tool_registry reg;
        int connected;
        jc_tool_registry_init(&reg);
        jc_mcp_manager_init(&mcp, app);
        app->mcp = &mcp;
        jc_mcp_connect_all(&mcp, &app->config, &reg);
        connected = jc_mcp_server_count(&mcp);
        jc_snprintf(detail, sizeof(detail), "%d of %d server(s) connected",
                    connected, n);
        jc_doctor_add(&d, connected == n ? JC_DOC_OK : JC_DOC_WARN,
            connected == n ? "MCP servers connected"
                           : "some MCP servers failed to connect",
            connected == n ? detail
                           : "run `jichi -v mcp` to see why");
        jc_mcp_manager_shutdown(&mcp);
        jc_tool_registry_free(&reg);
        app->mcp = NULL;
    }

    /* LSP servers: check each command resolves on PATH. */
    n = (int)app->config.lsp_servers.len;
    if (n == 0) {
        jc_doctor_add(&d, JC_DOC_OK, "LSP: none configured", NULL);
    } else {
        struct jc_sb miss;
        int nmiss = 0;
        jc_sb_init(&miss);
        for (i = 0; i < n; i++) {
            struct jc_lsp_server_cfg *s =
                (struct jc_lsp_server_cfg *)jc_vec_at(&app->config.lsp_servers,
                                                      (jc_size)i);
            if (!cmd_on_path(s->command)) {
                if (nmiss > 0) jc_sb_append(&miss, ", ");
                jc_sb_append(&miss, s->command != NULL ? s->command : "?");
                nmiss++;
            }
        }
        if (nmiss == 0) {
            jc_doctor_add(&d, JC_DOC_OK,
                          "LSP server commands found on PATH", NULL);
        } else {
            jc_snprintf(detail, sizeof(detail), "not found: %s",
                        miss.data != NULL ? miss.data : "");
            jc_doctor_add(&d, JC_DOC_WARN,
                          "some LSP server commands are not on PATH", detail);
        }
        jc_sb_free(&miss);
    }

    /* M198: the session store's own health. `/sessions` and `/resume`
     * read and cJSON-parse every file, so a large store costs latency on
     * every invocation (the M197 retention is fixed, but a ~480 MB store
     * is still ~3 s per listing). The store has no rotation and grows by
     * one file per run; the only removal path is `/sessions clear`. Also
     * report crash-left `*.json.tmp<pid>` files -- inert (they fail the
     * .json suffix test) but a sign of an interrupted write. Reported,
     * never auto-deleted: removing files under the user's HOME on a
     * health check is a worse risk than the litter. */
    {
        char sdir[1100];
        struct jc_vec names;
        struct jc_arena *sa = jc_arena_new(0);
        if (sa != NULL) {
            jc_snprintf(sdir, sizeof(sdir), "%s/.jichi.d/sessions",
                        jc_home_dir());
            jc_vec_init(&names, sizeof(char *));
            if (jc_list_dir(sdir, &names, sa) == JC_OK) {
                long total = 0;
                int nsess = 0, ntmp = 0;
                jc_size k;
                for (k = 0; k < names.len; k++) {
                    const char *nm = *(char **)jc_vec_at(&names, k);
                    jc_size ln = strlen(nm);
                    char fp[1200];
                    jc_snprintf(fp, sizeof(fp), "%s/%s", sdir, nm);
                    if (strstr(nm, ".json.tmp") != NULL) {
                        ntmp++;
                        continue;
                    }
                    if (ln >= 6 && strcmp(nm + ln - 5, ".json") == 0) {
                        long fs = jc_file_size(fp);
                        nsess++;
                        if (fs > 0) total += fs;
                    }
                }
                jc_snprintf(detail, sizeof(detail),
                            "%d session(s), %ld MB", nsess,
                            total / (1024L * 1024L));
                if (total > (256L * 1024L * 1024L)) {
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "session store is large (every /sessions and "
                        "/resume parses all of it)",
                        "prune with /sessions clear; there is no "
                        "automatic rotation");
                } else {
                    jc_doctor_add(&d, JC_DOC_OK, "session store", detail);
                }
                if (ntmp > 0) {
                    jc_snprintf(detail, sizeof(detail),
                                "%d leftover *.json.tmp file(s)", ntmp);
                    jc_doctor_add(&d, JC_DOC_WARN,
                        "interrupted session write(s) left temp files",
                        detail);
                }
            }
            jc_vec_free(&names);
            jc_arena_free(sa);
        }
    }

    /* Project assets discovered under .jichi/ (informational; see the `agents`,
     * `commands`, `skills`, and `rules` subcommands for the details). */
    {
        int na, nc, nk;
        jc_agentdef_load(&app->agents, app->cwd, app->arena);
        jc_command_load(&app->commands, app->cwd, app->arena);
        jc_skill_load(&app->skills, app->cwd, app->arena);
        /* M302: doctor loaded agents/commands/skills but NOT output styles, so the
         * new style lint compared every `style:` against an empty set and called
         * all of them dead -- the empty-universe failure its own comment warns
         * about, found by its own smoke driver on the first run. */
        jc_output_style_load(&app->output_styles, app->cwd, app->arena);
        na = (int)app->agents.defs.len;
        nc = (int)app->commands.commands.len;
        nk = jc_skill_count(&app->skills);
        /* M143: durable memory outgrowing the injection budget must not be
         * silent -- the oldest notes stop reaching the prompt. */
        {
            long msz = jc_memory_file_size(app);
            if (msz > (long)JC_MEMORY_MAX) {
                jc_snprintf(detail, sizeof(detail),
                            "%ld KB on disk; only the most recent %d KB is "
                            "loaded -- prune or consolidate",
                            msz / 1024, JC_MEMORY_MAX / 1024);
                jc_doctor_add(&d, JC_DOC_WARN,
                              ".jichi/memory.md exceeds the injection budget",
                              detail);
            }
        }
        if (na + nc + nk == 0) {
            jc_doctor_add(&d, JC_DOC_OK,
                          "no project assets (run `init` to scaffold some)",
                          NULL);
        } else {
            jc_snprintf(detail, sizeof(detail),
                        "%d agent%s, %d command%s, %d skill%s",
                        na, na == 1 ? "" : "s", nc, nc == 1 ? "" : "s",
                        nk, nk == 1 ? "" : "s");
            jc_doctor_add(&d, JC_DOC_OK, "project assets", detail);

            /* Validate their frontmatter (typo'd/unknown keys, unterminated
             * blocks, missing descriptions, command-vs-built-in collisions). */
            {
                char jichi[1100];
                struct jc_sb iss;
                int nbad;
                jc_snprintf(jichi, sizeof(jichi), "%s/.jichi", app->cwd);
                jc_sb_init(&iss);
                nbad  = doctor_validate_assets(app, jichi, "agents",
                                               JC_ASSET_AGENT, 0, &iss);
                nbad += doctor_validate_assets(app, jichi, "commands",
                                               JC_ASSET_COMMAND, 0, &iss);
                nbad += doctor_validate_assets(app, jichi, "skills",
                                               JC_ASSET_SKILL, 1, &iss);
                /* M389: output styles are the fourth frontmatter-bearing asset
                 * and were validated by nothing -- the one kind M302 pointed AT
                 * (agents/skills gained `style:`) while leaving it unchecked. */
                nbad += doctor_validate_assets(app, jichi, "output-styles",
                                               JC_ASSET_STYLE, 0, &iss);
                if (nbad == 0) {
                    jc_doctor_add(&d, JC_DOC_OK, "asset frontmatter valid",
                                  NULL);
                } else {
                    jc_snprintf(detail, sizeof(detail), "%s",
                                iss.data != NULL ? iss.data : "");
                    jc_doctor_add(&d, JC_DOC_WARN, "asset frontmatter issues",
                                  detail);
                }
                jc_sb_free(&iss);
            }
        }
    }

    /* Model selectors used by project assets and the routing tiers (M284). The
     * assets above are already loaded, so this is pure resolution. */
    {
        struct jc_sb bad;
        struct jc_sb warn;
        int nbad = 0;
        int nwarn = 0;
        jc_size i;

        jc_sb_init(&bad);
        jc_sb_init(&warn);

        for (i = 0; i < app->agents.defs.len; i++) {
            const struct jc_agentdef *ad =
                (const struct jc_agentdef *)jc_vec_at(&app->agents.defs, i);
            doctor_check_selector(&app->config, "agent", ad->name, ad->model,
                                  &bad, &warn, &nbad, &nwarn);
        }
        for (i = 0; i < app->commands.commands.len; i++) {
            const struct jc_command *cm =
                (const struct jc_command *)jc_vec_at(&app->commands.commands, i);
            doctor_check_selector(&app->config, "command", cm->name, cm->model,
                                  &bad, &warn, &nbad, &nwarn);
        }
        /* Routing tiers: same resolution, same failure mode -- except routing
         * fails silently (jc_config_routing_resolve just returns 0 and the run
         * quietly never escalates), so a typo here is even harder to notice. */
        doctor_check_selector(&app->config, "routing", "fast",
                              app->config.routing.fast, &bad, &warn,
                              &nbad, &nwarn);
        doctor_check_selector(&app->config, "routing", "strong",
                              app->config.routing.strong, &bad, &warn,
                              &nbad, &nwarn);

        if (nbad > 0) {
            jc_doctor_add(&d, JC_DOC_FAIL, "unresolvable model selector(s)",
                          bad.data != NULL ? bad.data : "");
        }
        if (nwarn > 0) {
            jc_doctor_add(&d, JC_DOC_WARN, "ambiguous model selector(s)",
                          warn.data != NULL ? warn.data : "");
        }
        if (nbad == 0 && nwarn == 0) {
            jc_doctor_add(&d, JC_DOC_OK, "model selectors resolve", NULL);
        }
        jc_sb_free(&bad);
        jc_sb_free(&warn);
    }

    /* Tool fences on project assets (M285). An agent profile's `tools:` is an
     * ENFORCED allow-list for a subagent and a skill's is (with restrict-tools)
     * the same, so a name in it that is not a real tool, or one the resolved tool
     * profile never advertises, quietly shrinks what that specialist can do. In
     * zigodot this cost six profiles their LSP navigation and showed up only as
     * `format_file` failing 0/3 in 31 MB of telemetry. */
    {
        struct doctor_fence_stats st;
        int core_profile;
        jc_size i;
        long tp_limit = jc_compact_context_limit(app);

        core_profile = jc_config_tool_profile_core(&app->config, tp_limit);
        st.n_unknown = 0;
        st.n_dropped = 0;
        st.shown_unknown = 0;
        st.shown_dropped = 0;
        jc_sb_init(&st.ex_unknown);
        jc_sb_init(&st.ex_dropped);

        for (i = 0; i < app->agents.defs.len; i++) {
            const struct jc_agentdef *ad =
                (const struct jc_agentdef *)jc_vec_at(&app->agents.defs, i);
            doctor_check_fence(app, "agent", ad->name, &ad->tools,
                               core_profile, &st);
        }
        for (i = 0; i < (jc_size)jc_skill_count(&app->skills); i++) {
            const struct jc_skill *sk = jc_skill_at(&app->skills, (int)i);
            if (sk == NULL) {
                continue;
            }
            doctor_check_fence(app, "skill", sk->name, &sk->tools,
                               core_profile, &st);
        }

        if (st.n_unknown > 0) {
            jc_snprintf(detail, sizeof(detail),
                        "%d entr%s cannot match a call, so the fence drops "
                        "%s silently (a fence is exact-match, unlike a call, "
                        "which resolves aliases): %s%s -- fix the name in the "
                        "asset",
                        st.n_unknown, st.n_unknown == 1 ? "y" : "ies",
                        st.n_unknown == 1 ? "it" : "them",
                        st.ex_unknown.data != NULL ? st.ex_unknown.data : "",
                        st.n_unknown > st.shown_unknown ? ", ..." : "");
            jc_doctor_add(&d, JC_DOC_WARN, "asset tool fence names no such tool",
                          detail);
        }
        if (st.n_dropped > 0) {
            jc_snprintf(detail, sizeof(detail),
                        "%d fenced entr%s never advertised under toolProfile "
                        "core: %s%s -- set toolProfile:full (or raise "
                        "contextLength to >= %d), else trim the fences",
                        st.n_dropped, st.n_dropped == 1 ? "y is" : "ies are",
                        st.ex_dropped.data != NULL ? st.ex_dropped.data : "",
                        st.n_dropped > st.shown_dropped ? ", ..." : "",
                        JC_TOOL_PROFILE_AUTO_BELOW);
            jc_doctor_add(&d, JC_DOC_WARN,
                          "asset tool fences unreachable under the core profile",
                          detail);
        }
        if (st.n_unknown == 0 && st.n_dropped == 0 &&
            (app->agents.defs.len > 0 || jc_skill_count(&app->skills) > 0)) {
            jc_doctor_add(&d, JC_DOC_OK, "asset tool fences resolve", NULL);
        }
        jc_sb_free(&st.ex_unknown);
        jc_sb_free(&st.ex_dropped);
    }

    /* M302: a `style:` naming no output style. Shipped in the SAME milestone as the
     * feature on purpose -- M285's lesson is that a declared-but-dead name is worse
     * than an absent one, because it looks like it is doing something. The
     * behaviour is a silent fallback to the session style, so nothing breaks and
     * nothing works either: exactly the failure that needs a check rather than a
     * user's patience. WARN, not FAIL, matching M285's asymmetry -- a specialist
     * with the wrong tone is degraded, not broken. */
    {
        struct jc_sb bad;
        int nbad = 0;
        int shown = 0;
        jc_size i;

        jc_sb_init(&bad);
        for (i = 0; i < app->agents.defs.len; i++) {
            const struct jc_agentdef *ad =
                (const struct jc_agentdef *)jc_vec_at(&app->agents.defs, i);
            if (ad->style == NULL || ad->style[0] == '\0') {
                continue;
            }
            if (jc_output_style_find(&app->output_styles, ad->style) == NULL) {
                nbad++;
                if (shown < 3) {
                    jc_sb_append_fmt(&bad, "%sagent %s -> %s",
                                     shown > 0 ? ", " : "", ad->name, ad->style);
                    shown++;
                }
            }
        }
        for (i = 0; i < (jc_size)jc_skill_count(&app->skills); i++) {
            const struct jc_skill *sk = jc_skill_at(&app->skills, (int)i);
            if (sk == NULL || sk->style == NULL || sk->style[0] == '\0') {
                continue;
            }
            if (jc_output_style_find(&app->output_styles, sk->style) == NULL) {
                nbad++;
                if (shown < 3) {
                    jc_sb_append_fmt(&bad, "%sskill %s -> %s",
                                     shown > 0 ? ", " : "", sk->name, sk->style);
                    shown++;
                }
            }
        }
        if (nbad > 0) {
            jc_snprintf(detail, sizeof(detail),
                        "%d asset%s name an output style that does not exist, so "
                        "the tone silently falls back to the session's: %s%s -- "
                        "add the style under .jichi/output-styles/, or fix the "
                        "name (see `jichi output-styles`)",
                        nbad, nbad == 1 ? "" : "s",
                        bad.data != NULL ? bad.data : "",
                        nbad > shown ? ", ..." : "");
            jc_doctor_add(&d, JC_DOC_WARN, "asset style names no such style",
                          detail);
        } else if (app->output_styles.styles.len > 0) {
            jc_doctor_add(&d, JC_DOC_OK, "asset styles resolve", NULL);
        }
        jc_sb_free(&bad);
    }

    {
        /* Machine profile: cores + RAM + free disk, and the resolved resource
         * tier (#10 -- centralized in jc_resource_tier). */
        unsigned long mb = jc_mem_total_mb();
        int cpu = jc_cpu_count();
        unsigned long disk = jc_disk_free_mb(app->cwd);
        enum jc_resource_tier tier = jc_resource_tier(mb, cpu);
        const char *tname = (tier == JC_RES_MINIMAL) ? " -- tier: minimal (lite)"
                          : (tier == JC_RES_LITE) ? " -- tier: lite auto-enabled"
                          : "";
        if (mb > 0) {
            jc_snprintf(detail, sizeof(detail),
                        "%d core(s), %lu MB RAM%s", cpu, mb, tname);
        } else {
            jc_snprintf(detail, sizeof(detail), "%d core(s)", cpu);
        }
        jc_doctor_add(&d, JC_DOC_OK, "machine profile", detail);
        if (disk > 0) {
            char dd[64];
            jc_snprintf(dd, sizeof(dd), "%lu MB free on the workspace filesystem",
                        disk);
            jc_doctor_add(&d, disk < 256UL ? JC_DOC_WARN : JC_DOC_OK,
                          "disk space", dd);
        }
    }

    /* Tools paid for and never called (M316). The token cost of a tool
     * definition is paid on EVERY model call, used or not, so a workspace that
     * advertises tools its sessions never choose is spending on nothing.
     *
     * This is advice, which is doctor's job and deliberately NOT `context tools`'s
     * (M314 reports and refuses to conclude). Advice needs a bar that report does
     * not try to clear, so:
     *
     *   - the evidence axis is DISTINCT SESSIONS, not turns: one long session is
     *     still one task, and only a plural claim about different tasks is
     *     informative;
     *   - below the threshold, an OK line says the check is quiet and why --
     *     silence would be indistinguishable from a pass (the M314 stated-absence
     *     principle, applied to a checklist);
     *   - the advice is the LEVER (`toolProfile: core`), never per-tool surgery:
     *     core is a fixed considered set whose cost doctor already warns about,
     *     and naming individual tools would invite removing one that is rare and
     *     right.
     *
     * WARN at most, never FAIL -- nothing is broken. Follows M285's fence lint
     * (a degraded profile) rather than M284's selector lint (an unresolvable
     * name), and is NOT escalated by --unattended, which exists for posture
     * problems; token efficiency is not one. */
    {
        struct jc_telemetry_summary ts;
        struct jc_tooluse_stats use;
        struct jc_tool_registry treg;
        int have = 0;
        double cache_in = 0.0;
        double cache_read = 0.0;
        double cache_prefix = 0.0;
        long cache_calls = 0;

        /* Tool registration happens after doctor is dispatched, so build the
         * built-in registry here -- the same thing `context` does, and the same
         * caveat: configured MCP/user/LSP tools are not in this count. */
        jc_tool_registry_init(&treg);
        jc_tool_register_builtins(&treg);
        jc_tool_register_configured(&treg, app);   /* M325b: as a turn sees it */
        app->tools = &treg;

        memset(&use, 0, sizeof(use));
        if (jc_app_load_telemetry(app, &ts, NULL, 0)) {
            have = jc_context_tool_use(app, &ts, &use);
            /* M326w: the prompt-cache verdict, from the SAME summary. Loading
             * telemetry twice would mean parsing a log that reaches 40 MB in
             * the field a second time, for two lines of the same checklist. */
            jc_cacheaudit_totals(&ts, &cache_in, &cache_read, NULL, &cache_calls);
            cache_prefix = jc_cacheaudit_prefix(&ts);
        }
        /* M340: promptCache on, but the prefix is too small for the backend to
         * cache it -- which caches NOTHING and reports nothing. Checked here
         * because this is the one place in doctor with a live tool registry, and
         * the cacheable block is tools + system, not system alone.
         *
         * Gated on the ANTHROPIC provider: that is where jichi sends explicit
         * breakpoints, so a too-small block is jichi's own request being wasted.
         * For an OpenAI-compatible server caching is automatic and server-side --
         * jichi does not know that server's rules, and a warning it cannot
         * substantiate is worse than silence (a local llama.cpp has no minimum
         * at all). */
        if (app->config.model.prompt_cache != 0 &&
            app->config.model.provider != NULL &&
            strcmp(app->config.model.provider, "anthropic") == 0) {
            long minimum = jc_promptcache_min_tokens(app->config.model.model);
            long prefix = jc_context_prefix_tokens(app);
            if (minimum > 0 && prefix >= 0 && prefix < minimum) {
                jc_snprintf(detail, sizeof(detail),
                    "the cacheable prefix (tools + system) is ~%ld tokens and "
                    "this model will not cache a block under %ld, so every call "
                    "is billed in full and NOTHING reports it. Raise the prefix "
                    "(craft:true, repoMap:true, a fuller toolProfile, "
                    "instruction files) or accept no caching. Trimming the "
                    "prefix is the right lever on an UNCACHED backend and the "
                    "wrong one here -- the two regimes invert.",
                    prefix, minimum);
                jc_doctor_add(&d, JC_DOC_WARN,
                    "promptCache on but the prefix is too small to cache",
                    detail);
            } else if (minimum > 0 && prefix >= minimum) {
                jc_snprintf(detail, sizeof(detail),
                    "~%ld tokens, over this model's %ld-token minimum",
                    prefix, minimum);
                jc_doctor_add(&d, JC_DOC_OK, "cacheable prefix", detail);
            }
        }
        jc_telemetry_summary_free(&ts);
        app->tools = NULL;
        jc_tool_registry_free(&treg);

        if (!have) {
            jc_doctor_add(&d, JC_DOC_OK, "tool use: no telemetry for this project",
                          "none recorded here yet -- metrics are on by default "
                          "(M599); after a few turns this check names tools you "
                          "advertise but never call. If `logging.level` is off, "
                          "the learner has nothing to learn from");
        } else if (!use.enough) {
            jc_snprintf(detail, sizeof(detail),
                        "%d session(s), %ld tool call(s) -- needs at least %d and "
                        "%d before a never-called tool means anything; `context "
                        "tools` shows the raw numbers",
                        use.sessions, use.calls, JC_TOOLUSE_MIN_SESSIONS,
                        JC_TOOLUSE_MIN_CALLS);
            jc_doctor_add(&d, JC_DOC_OK, "tool use: not enough telemetry to judge",
                          detail);
        } else if (use.unused > 0) {
            jc_snprintf(detail, sizeof(detail),
                        "%d of %d advertised tools were never called across %d "
                        "sessions (%ld tool calls), costing ~%ld tokens on EVERY "
                        "model call. `toolProfile: core` advertises the essentials "
                        "only; run `context tools` to see which and what core "
                        "would keep",
                        use.unused, use.advertised, use.sessions, use.calls,
                        use.unused_tokens);
            jc_doctor_add(&d, JC_DOC_WARN, "paying for tools you never call",
                          detail);
        } else {
            jc_snprintf(detail, sizeof(detail),
                        "all %d advertised tools were called at least once across "
                        "%d sessions", use.advertised, use.sessions);
            jc_doctor_add(&d, JC_DOC_OK, "tool use", detail);
        }

        /* Is the backend actually serving the prompt prefix? (M326w)
         *
         * `telemetry --cache-audit` has answered this since M104, and answered
         * it well -- but nothing ever told anyone to run it. A workload was
         * measured at 0% over 1.24 BILLION input tokens, with the absence
         * repeated in three anecdotes and tested in none.
         *
         * jichi's own prefix is not the suspect: it is byte-stable by the M31d
         * guard, and 27 of 29 top-level sessions in that workload held sys_tok
         * constant end to end. So a 0% hit-rate is a fact about the SERVER, and
         * the honest verdict names it as such rather than implying a
         * misconfiguration.
         *
         * Same shape as the tool-use check above (M316): distinct evidence bar,
         * a stated absence rather than silence, WARN at most, and the advice is
         * the lever. It is NOT escalated by --unattended -- a cacheless backend
         * is a cost, not a posture problem. */
        if (cache_calls > 0) {
            int hr = jc_cacheaudit_hitrate(cache_read, cache_in);
            int verdict = jc_cacheaudit_verdict(hr, cache_read + cache_in);

            if (verdict == JC_CACHE_NODATA) {
                jc_doctor_add(&d, JC_DOC_OK,
                              "prompt cache: not enough traffic to judge",
                              "keep logging; `telemetry --cache-audit` shows the "
                              "raw numbers");
            } else if (verdict == JC_CACHE_NONE) {
                if (cache_prefix > 0.0) {
                    jc_snprintf(detail, sizeof(detail),
                                "0%% of %.0f input tokens over %ld calls were "
                                "served from cache -- this backend re-reads the "
                                "whole prompt every call. Your fixed prefix "
                                "(system + tool definitions) is ~%.0f tokens, "
                                "re-sent every time. `toolProfile: core` and a "
                                "smaller repoMap/instruction set are the levers; "
                                "`telemetry --cache-audit` has the breakdown",
                                cache_read + cache_in, cache_calls, cache_prefix);
                } else {
                    jc_snprintf(detail, sizeof(detail),
                                "0%% of %.0f input tokens over %ld calls were "
                                "served from cache -- this backend re-reads the "
                                "whole prompt every call. `telemetry "
                                "--cache-audit` has the breakdown",
                                cache_read + cache_in, cache_calls);
                }
                jc_doctor_add(&d, JC_DOC_WARN, "backend is not caching the prompt",
                              detail);
            } else {
                jc_snprintf(detail, sizeof(detail),
                            "%d%% of %.0f input tokens served from cache over "
                            "%ld calls (%s)", hr, cache_read + cache_in,
                            cache_calls, jc_cacheaudit_verdict_name(verdict));
                jc_doctor_add(&d, JC_DOC_OK, "prompt cache", detail);
            }
        }
    }

    jc_sb_init(&out);
    if (json) {
        jc_doctor_render_json(&d, &out);
    } else {
        jc_doctor_render(&d, color, unicode, &out);
    }
    fputs(out.data != NULL ? out.data : "", stdout);
    jc_sb_free(&out);
    code = jc_doctor_exit_code(&d);
    jc_doctor_free(&d);
    return code;
}

/* `docs` -> list configured external documentation sources.
 * `docs index [<name>]` -> build/refresh the index for one source (or all).
 * `docs search <name> <query...>` -> retrieve passages for a query.
 * Index/search need an embed-role model + network; list is offline. */
static int run_docs(struct jc_app *app, struct cli_args *args)
{
    const char *sub = (args->npos >= 2) ? args->pos[1] : NULL;
    jc_size i;

    if (app->config.docs.len == 0) {
        printf("(no documentation sources configured; add a \"docs\": "
               "[{\"name\":...,\"path\":...}] array to your config)\n");
        return 0;
    }

    /* `docs` with no subcommand (or an explicit "list"): list the sources. */
    if (sub == NULL || strcmp(sub, "list") == 0) {
        printf("Configured documentation sources:\n");
        for (i = 0; i < app->config.docs.len; i++) {
            const struct jc_docs_cfg *d =
                (const struct jc_docs_cfg *)jc_vec_at(&app->config.docs, i);
            const char *loc = (d->url != NULL) ? d->url
                            : (d->path != NULL) ? d->path : "?";
            printf("  %-16s %s%s\n", d->name != NULL ? d->name : "?",
                   loc, (d->url != NULL) ? "  [url]" : "");
        }
        return 0;
    }

    if (strcmp(sub, "index") == 0) {
        const char *only = (args->npos >= 3) ? args->pos[2] : NULL;
        struct jc_model_cfg *embed = jc_app_model_for_role(app, JC_ROLE_EMBED);
        int rc = 0;
        if (embed == NULL) {
            fprintf(stderr, "error: no embedding model configured "
                            "(add a model with role \"embed\")\n");
            return 1;
        }
        for (i = 0; i < app->config.docs.len; i++) {
            const struct jc_docs_cfg *d =
                (const struct jc_docs_cfg *)jc_vec_at(&app->config.docs, i);
            char root[4096];
            struct jc_index *idx = NULL;
            struct jc_index_stats st;
            jc_status s;
            char *derr = NULL;
            if (only != NULL && (d->name == NULL || strcmp(d->name, only) != 0)) {
                continue;
            }
            if (jc_docs_source_root(app, d, root, sizeof(root), &derr) != JC_OK) {
                fprintf(stderr, "%s: %s\n", d->name != NULL ? d->name : "?",
                        derr != NULL ? derr : "could not resolve source");
                free(derr);
                rc = 1;
                continue;
            }
            memset(&st, 0, sizeof(st));
            s = jc_index_build(root, embed, 1,
                               jc_pdf_command(app->config.pdf_command), &idx,
                               &st, &app->abort_flag,
                               &app->config.ignore_dirs);
            if (s != JC_OK) {
                fprintf(stderr, "%s: indexing failed\n",
                        d->name != NULL ? d->name : "?");
                rc = 1;
                continue;
            }
            printf("%s: %d files, %d chunks (%d embedded, %d reused)\n",
                   d->name != NULL ? d->name : "?", st.files, st.chunks,
                   st.embedded, st.reused);
            jc_index_free(idx);
        }
        return rc;
    }

    if (strcmp(sub, "search") == 0) {
        const char *name = (args->npos >= 3) ? args->pos[2] : NULL;
        const struct jc_docs_cfg *src;
        struct jc_sb q;
        char *text = NULL;
        jc_status s;
        if (args->npos < 4) {
            fprintf(stderr, "usage: docs search <name> <query>\n");
            return 1;
        }
        src = jc_docs_find(app, name);
        if (src == NULL) {
            fprintf(stderr, "error: unknown docs source '%s'\n",
                    name != NULL ? name : "");
            return 1;
        }
        jc_sb_init(&q);
        for (i = 3; i < (jc_size)args->npos; i++) {
            if (i > 3) {
                jc_sb_append_char(&q, ' ');
            }
            jc_sb_append(&q, args->pos[i]);
        }
        s = jc_docs_run(app, src, q.data != NULL ? q.data : "", 5, &text);
        jc_sb_free(&q);
        if (text != NULL) {
            fputs(text, stdout);
            if (text[0] != '\0' && text[strlen(text) - 1] != '\n') {
                fputc('\n', stdout);
            }
            free(text);
        }
        return (s == JC_OK) ? 0 : 1;
    }

    fprintf(stderr, "usage: docs [list | index [<name>] | "
                    "search <name> <query>]\n");
    return 1;
}

static int run_models(struct jc_app *app)
{
    int n = jc_config_model_count(&app->config);
    int i;

    if (n == 0) {
        printf("(no models configured)\n");
        return 0;
    }
    printf("Configured models (* = active):\n");
    for (i = 0; i < n; i++) {
        struct jc_model_cfg *m = jc_config_model_at(&app->config, i);
        char roles[160];
        int up = jc_net_reachable(m->api_base, m->api_key, 2, &app->abort_flag);
        format_roles(m->roles, roles, sizeof(roles));
        printf("%s %d) %-16s %-28s %s\n",
               i == app->config.active ? "*" : " ", i + 1,
               m->name != NULL ? m->name : "(unnamed)",
               m->model != NULL ? m->model : "?",
               up ? "[reachable]" : "[UNREACHABLE]");
        printf("       %s  roles=%s%s%s\n",
               m->api_base != NULL ? m->api_base : "(default)", roles,
               m->fallback != NULL ? "  fallback=" : "",
               m->fallback != NULL ? m->fallback : "");
    }
    return 0;
}

/* Print one resolved timeout, rendering 0 as "off". */
static void print_timeout(const char *label, long secs)
{
    if (secs > 0) {
        printf("  %-9s %lds\n", label, secs);
    } else {
        printf("  %-9s off\n", label);
    }
}

/* `timeouts` subcommand (M23c): show the model-call timeouts in effect for the
 * active model (after CLI > per-model > global > built-in resolution). */
static int run_timeouts(struct jc_app *app)
{
    long ct = 0, st = 0, rt = 0;
    const struct jc_model_cfg *m = &app->config.model;

    jc_config_resolve_timeouts(&app->config, m, &ct, &st, &rt);
    printf("Model-call timeouts for %s:\n",
           m->name != NULL ? m->name
                           : (m->model != NULL ? m->model : "(active model)"));
    print_timeout("connect", ct);
    print_timeout("stall", st);
    print_timeout("request", rt);
    printf("\nStall escalation (routing): %s\n",
           app->config.routing.escalate_on_stall ? "on" : "off");
    return 0;
}

/* Dispatch embed/rerank/index. Returns 1 if `cmd` was one of them (with the
 * process exit code in *code), else 0. Assumes config is loaded and curl is
 * initialised. */
static int run_index_subcommand(struct jc_app *app, struct cli_args *args,
                                int *code)
{
    const char *cmd = args->pos[0];
    if (strcmp(cmd, "embed") == 0) {
        if (args->npos < 2) {
            fprintf(stderr, "usage: jichi embed [--model <sel>] "
                            "\"text\"\n");
            *code = 2;
        } else {
            *code = run_embed(app, args->pos[1], args->model_override);
        }
        return 1;
    }
    if (strcmp(cmd, "rerank") == 0) {
        if (args->npos < 3) {
            fprintf(stderr, "usage: jichi rerank [--model <sel>] "
                            "\"query\" \"doc\"...\n");
            *code = 2;
        } else {
            *code = run_rerank(app, args->pos[1], args->pos + 2,
                               args->npos - 2, args->model_override);
        }
        return 1;
    }
    if (strcmp(cmd, "index") == 0) {
        *code = run_index(app, args->reindex, args->model_override);
        return 1;
    }
    return 0;
}

/* ----- daemon (warm process, M100) ------------------------------------- */

static volatile sig_atomic_t g_daemon_stop = 0;

static void on_daemon_signal(int sig)
{
    (void)sig;
    g_daemon_stop = 1;
}

/* Resolve the daemon socket path: explicit --socket, else $JICHI_DAEMON_SOCK,
 * else ~/.jichi.d/daemon.sock. */
static void daemon_socket_path(const char *explicit_path, char *buf,
                               jc_size cap)
{
    const char *env;
    if (explicit_path != NULL && explicit_path[0] != '\0') {
        jc_snprintf(buf, cap, "%s", explicit_path);
        return;
    }
    env = getenv("JICHI_DAEMON_SOCK");
    if (env != NULL && env[0] != '\0') {
        jc_snprintf(buf, cap, "%s", env);
        return;
    }
    jc_snprintf(buf, cap, "%s/.jichi.d/daemon.sock", jc_home_dir());
}

/* Read one newline-terminated line from `fd` into `sb` (without the newline).
 * Returns 1 on a line, 0 on EOF before any data, -1 on error. */
/* Returns 1 on a line, 0 on EOF before any data, -1 on error, and -2 when the
 * line exceeded JC_DAEMON_MAX_LINE (M528) -- distinct because the caller must
 * answer that case with an error the client can read, not with a truncated
 * request that means something else.
 *
 * M530: reads in CHUNKS. It read one byte per read(2), which was tolerable while
 * requests were small and became a defect the moment M528 declared a 1 MiB
 * limit: a legitimate large prompt (a pasted file) then cost up to a million
 * syscalls for one request, and the limit is what made that reachable rather
 * than theoretical. Bytes after the newline are discarded, which is correct for
 * a protocol of one request per connection -- nothing else ever reads this
 * descriptor (the worker's copy is write-only). */
static int daemon_read_line(int fd, struct jc_sb *sb)
{
    char buf[4096];
    int got = 0;
    jc_sb_clear(sb);
    for (;;) {
        const char *nl;
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return got ? 1 : 0;
        }
        got = 1;
        nl = (const char *)memchr(buf, '\n', (size_t)n);
        if (nl != NULL) {
            jc_sb_append_n(sb, buf, (jc_size)(nl - buf));
            return 1;
        }
        /* Checked BEFORE appending, so the buffer never exceeds the declared
         * cap even transiently -- a limit you overshoot and then complain about
         * is not a limit. */
        if ((long)sb->len + (long)n > JC_DAEMON_MAX_LINE) {
            return -2;
        }
        jc_sb_append_n(sb, buf, (jc_size)n);
    }
}

/* `daemon [--socket <path>]`: a warm process that keeps the app (config, MCP,
 * LSP, index, prompt cache) hot and serves one request per connection over a
 * Unix socket. Output of each turn is streamed to the connection by redirecting
 * stdout for the turn's duration. */
/* Bounded pool of forked request workers for the daemon (W3). Each accepted
 * PROMPT is served by a short-lived child (fork = a copy-on-write snapshot of the
 * warm app, so a turn's mutations never leak into the parent or a sibling), while
 * the parent keeps accepting. A per-child watchdog force-kills a wedged turn so
 * one bad request can't hang the listener. Reuses jc_workerpool primitives. */
#define JC_DAEMON_MAX_WORKERS 64

struct daemon_pool {
    pid_t  pid[JC_DAEMON_MAX_WORKERS];
    double start_ms[JC_DAEMON_MAX_WORKERS];
    int    n;
    int    max;
    long   timeout_ms;
};

static void dpool_remove(struct daemon_pool *p, int i)
{
    p->n--;
    p->pid[i] = p->pid[p->n];
    p->start_ms[i] = p->start_ms[p->n];
}

/* Reap finished workers (non-blocking) and force-kill any past the watchdog. */
static void dpool_tick(struct daemon_pool *p)
{
    double now = jc_now_millis();
    int i = 0;
    while (i < p->n) {
        pid_t r = waitpid(p->pid[i], NULL, WNOHANG);
        if (r == p->pid[i] || r < 0) { /* exited, or already gone */
            dpool_remove(p, i);
            continue;
        }
        if (jc_worker_over_deadline(now, p->start_ms[i], p->timeout_ms)) {
            fprintf(stderr, "daemon: worker %ld exceeded %lds -> killing\n",
                    (long)p->pid[i], p->timeout_ms / 1000);
            kill(p->pid[i], SIGTERM);
            jc_worker_reap_grace(p->pid[i], JC_WORKER_TERM_GRACE_MS);
            dpool_remove(p, i);
            continue;
        }
        i++;
    }
}

/* --- M529: the `assignment` verb group over the daemon socket --------------
 *
 * WHY A WIRE AT ALL. jichi's teaching features -- a spec set, a rubric, a
 * machine-checkable verify, a progress record -- were reachable only by running
 * the CLI and parsing output meant for a human. A course platform, a marking
 * service or an editor plugin had to scrape. These three verbs hand back the
 * same data the CLI renders, from the same collector and the same grading core,
 * so the wire and the terminal cannot disagree about a grade.
 *
 * WHY THERE IS NO SUBMIT VERB. proposals/2026-08-jichi-protocol.md §5.2 also
 * specifies `assignment.attempt`, which would take an attempt as an artifact --
 * i.e. write files a caller sent into the workspace, and then grade them. That
 * is deliberately NOT in this slice: it means executing a verify over content
 * that arrived over a socket, in a workspace the daemon did not choose (its
 * workspace is the directory it started in), with no artifact validation
 * anywhere. §P5 of the same proposal says a peer's task is untrusted input; the
 * honest response is to build the read-and-grade half first and leave submission
 * to a milestone that can give it a fence of its own. */
static void daemon_write_line(int fd, cJSON *o)
{
    char *body;
    if (o == NULL) {
        return;
    }
    body = jc_json_print(o);
    cJSON_Delete(o);
    if (body != NULL) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_sb_append(&sb, body);
        jc_sb_append_char(&sb, '\n');
        free(body);
        if (sb.data != NULL) {
            (void)!write(fd, sb.data, sb.len);
        }
        jc_sb_free(&sb);
    }
}

/* An error line with a stable dotted code, per the protocol's error model. */
static void daemon_write_err(int fd, const char *code, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return;
    }
    cJSON_AddNumberToObject(o, "v", (double)JC_DAEMON_PROTO_V);
    cJSON_AddStringToObject(o, "type", "error");
    cJSON_AddStringToObject(o, "code", code);
    cJSON_AddStringToObject(o, "message", msg);
    daemon_write_line(fd, o);
}

/* Resolve a wire-supplied NAME to a spec path inside the workspace, or NULL if
 * the name is refused or there is no such spec. `why` receives the error code. */
static const char *assign_resolve(struct jc_app *app, const char *name,
                                  char *buf, jc_size cap, const char **why)
{
    if (name == NULL || !jc_assign_name_ok(name)) {
        *why = "assignment.name";
        return NULL;
    }
    jc_snprintf(buf, cap, "%s/docs/assignments/%s", app->cwd, name);
    if (!jc_file_exists(buf)) {
        *why = "assignment.not_found";
        return NULL;
    }
    *why = NULL;
    return buf;
}

/* Terminate + reap all live workers (on shutdown/abort). */
static void dpool_kill_all(struct daemon_pool *p)
{
    int i;
    for (i = 0; i < p->n; i++) {
        if (p->pid[i] > 0) {
            kill(p->pid[i], SIGTERM);
        }
    }
    for (i = 0; i < p->n; i++) {
        jc_worker_reap_grace(p->pid[i], JC_WORKER_TERM_GRACE_MS);
    }
    p->n = 0;
}

static int run_daemon(struct jc_app *app, struct cli_args *args)
{
    char path[1024];
    struct sockaddr_un addr;
    struct jc_sb line;
    struct jc_arena *reqarena;
    struct daemon_pool dp;
    int lfd;
    int stopping = 0;
    int idle_secs = args->idle_dream;   /* 0 => idle reflection off */
    double last_activity;
    int dreamed = 0;                    /* dreamed once this idle stretch */

    reqarena = jc_arena_new(0);
    if (reqarena == NULL) {
        return 1;
    }
    daemon_socket_path(args->daemon_socket, path, sizeof(path));
    if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        fprintf(stderr, "daemon: socket path too long (max %lu bytes): %s\n"
                "  pass a shorter --socket or set JICHI_DAEMON_SOCK.\n",
                (unsigned long)sizeof(((struct sockaddr_un *)0)->sun_path) - 1,
                path);
        jc_arena_free(reqarena);
        return 1;
    }
    {
        char dir[1024];
        jc_snprintf(dir, sizeof(dir), "%s/.jichi.d", jc_home_dir());
        jc_mkdir_p(dir);
    }
    unlink(path); /* clear a stale socket from a crashed daemon */

    lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) {
        fprintf(stderr, "daemon: socket() failed\n");
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    jc_snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    /* M322: the socket mode IS the whole access-control list, and this daemon
     * accepts arbitrary prompts -- it runs tools, writes files and executes shell
     * commands as its own user. Until now it took whatever the umask allowed:
     * with the common default 022 that is 0755, so ANY local user could drive it.
     * The M159 control channel had this right from the start
     * (jc_make_private, "0600: same-user only -- the whole ACL"); the more
     * dangerous of the two sockets did not.
     *
     * The umask is set around bind() rather than chmod()ing afterwards, because
     * a chmod leaves a window in which the socket exists at the permissive mode
     * and a connect can already succeed. jc_make_private follows as a belt for
     * platforms that ignore the umask for sockets. */
    {
        mode_t old_umask = umask(0177);   /* 0600 for anything created below */
        int rc_bind = bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
        umask(old_umask);
        if (rc_bind != 0 || listen(lfd, 4) != 0) {
            fprintf(stderr, "daemon: could not bind/listen on %s\n", path);
            close(lfd);
            return 1;
        }
        jc_make_private(path);
    }

    /* M528: VERIFY the access control instead of trusting the two calls that
     * asked for it. The umask above and the jc_make_private after it both only
     * *request* 0600; the comment above names the case neither can rule out --
     * "there are platforms that ignore the umask for sockets" -- and a chmod's
     * return value says the call worked, not that the resulting mode is the one
     * you wanted. On such a platform the kernel creates a 0755 socket and this
     * daemon, which runs tools and executes shell commands as its own user,
     * becomes a shell prompt for every local account with nothing anywhere
     * saying so. So: read the mode back, and refuse to serve if it is wrong.
     * This is `doctor`'s own discipline for the key file ("created 0664,
     * tightened to 0600, read back") applied to the more dangerous of the two
     * sockets. */
    {
        enum jc_priv_verdict v = jc_path_private_check(path);
        if (v != JC_PRIV_OK) {
            fprintf(stderr,
                    "daemon: refusing to serve -- %s is %s.\n"
                    "  This daemon runs tools and shell commands as %lu; its file\n"
                    "  mode is its ENTIRE access-control list. Fix the mode (or the\n"
                    "  path) and start again.\n",
                    path, jc_priv_verdict_str(v), (unsigned long)geteuid());
            close(lfd);
            unlink(path);
            return 1;
        }
    }
    /* And the directory holding it: a mode of 0600 on the socket is no defence
     * if another user can unlink it and bind their own in its place. Writable by
     * others is acceptable only with the sticky bit, which is what makes /tmp a
     * legitimate location and 0777 not one. A warning rather than a refusal: the
     * socket itself is verified above, the hijack needs a second actor, and an
     * operator who chose the path may know something we do not. */
    {
        struct stat dst;
        char dir[1100];
        const char *slash = strrchr(path, '/');
        if (slash != NULL && (jc_size)(slash - path) < sizeof(dir)) {
            jc_snprintf(dir, (jc_size)(slash - path) + 1, "%s", path);
            if (stat(dir, &dst) == 0 && !jc_dir_holds_private(
                    (unsigned long)dst.st_mode)) {
                fprintf(stderr,
                        "daemon: warning -- %s is writable by others and not "
                        "sticky,\n  so another user could replace the socket. "
                        "Prefer ~/.jichi.d/.\n", dir);
            }
        }
    }

    signal(SIGINT, on_daemon_signal);
    signal(SIGTERM, on_daemon_signal);
    signal(SIGCHLD, SIG_DFL);  /* so waitpid reaps workers */
    signal(SIGPIPE, SIG_IGN);  /* a client that disconnects mid-stream */

    /* Size the worker pool: config daemonWorkers, else auto = min(cpu, 4). */
    dp.n = 0;
    dp.max = app->config.daemon_workers > 0 ? app->config.daemon_workers
             : (jc_cpu_count() < 4 ? jc_cpu_count() : 4);
    if (dp.max < 1) { dp.max = 1; }
    if (dp.max > JC_DAEMON_MAX_WORKERS) { dp.max = JC_DAEMON_MAX_WORKERS; }
    dp.timeout_ms = (app->config.daemon_worker_timeout > 0
                     ? app->config.daemon_worker_timeout : 300) * 1000L;

    fprintf(stderr, "daemon: listening on %s (Ctrl-C to stop); %d worker(s)%s\n",
            path, dp.max, idle_secs > 0 ? "; idle reflection on" : "");
    last_activity = jc_now_seconds();

    jc_sb_init(&line);
    while (!g_daemon_stop && !stopping) {
        fd_set rf;
        struct timeval tv;
        int connfd;
    int rc_line;
        struct jc_daemon_req req;
        int have_room = (dp.n < dp.max);
        int rc;

        /* Reap finished workers and watchdog-kill any wedged turn every tick. */
        dpool_tick(&dp);
        have_room = (dp.n < dp.max);

        FD_ZERO(&rf);
        if (have_room) {
            FD_SET(lfd, &rf);
        }
        /* Short tick so the watchdog/reaper stays responsive even while full. */
        tv.tv_sec = 0;
        tv.tv_usec = 250000;
        rc = select(have_room ? lfd + 1 : 0, have_room ? &rf : NULL, NULL, NULL,
                    &tv);
        if (rc <= 0 || !have_room) {
            /* Idle tick: once per idle stretch, and only when NO worker is busy,
             * run a propose-only reflection (dream) -- offline, no model call,
             * writes a dated draft outside the workspace. */
            if (idle_secs > 0 && !dreamed && dp.n == 0 &&
                jc_now_seconds() - last_activity >= (double)idle_secs) {
                fprintf(stderr, "daemon: idle %ds -> dream (propose-only)\n",
                        idle_secs);
                run_dream(reqarena, NULL, NULL);
                jc_arena_reset(reqarena);
                dreamed = 1;
            }
            continue; /* timeout / full / EINTR (re-check stop flag) */
        }
        last_activity = jc_now_seconds();
        dreamed = 0; /* activity resets the idle stretch */
        connfd = accept(lfd, NULL, NULL);
        jc_fd_cloexec(connfd); /* M472: not inherited by anything we spawn */
        if (connfd < 0) {
            continue;
        }
        jc_arena_reset(reqarena); /* stable across the parse (not app scratch) */
        rc_line = daemon_read_line(connfd, &line);
        if (rc_line == -2) {
            /* M528: over the declared cap. A named error, not a truncation --
             * a request cut at a megabyte parses as something its sender did
             * not ask for, which is worse than a refusal. */
            const char *big = "{\"v\":1,\"type\":\"error\","
                              "\"code\":\"limit.line\",\"message\":"
                              "\"request line exceeds maxLine; see hello.ok\"}\n";
            (void)!write(connfd, big, strlen(big));
            close(connfd);
            continue;
        }
        if (rc_line <= 0 ||
            jc_daemon_parse_request(line.data != NULL ? line.data : "",
                                    &req, reqarena) != JC_OK) {
            const char *err = "{\"type\":\"error\",\"message\":"
                              "\"bad request\"}\n";
            /* Best-effort: the connection is closed on the next line either
             * way, so a failed write costs the caller its error message and
             * nothing else. Cast is deliberate and narrow. */
            (void)!write(connfd, err, strlen(err));
            close(connfd);
            continue;
        }
        if (req.type == JC_DREQ_HELLO) {
            /* M528: capability + auth-posture discovery. `mode_verified` is 1
             * unconditionally because this process REFUSED TO START otherwise
             * (see the bind-time check above) -- so it is a fact about this
             * server, not a hope. `peercred` is 0 because no peer-credential
             * check is performed; the field exists in order to say so. */
            struct jc_daemon_hello hi;
            char *reply;
            memset(&hi, 0, sizeof(hi));
            hi.agent = "jichi " JC_VERSION;
            hi.max_line = JC_DAEMON_MAX_LINE;
            hi.workers = dp.max;
            hi.uid = (unsigned long)geteuid();
            hi.mode_verified = 1;
            hi.peercred = 0;
            reply = jc_daemon_build_hello_ok(&hi);
            if (reply != NULL) {
                (void)!write(connfd, reply, strlen(reply));
                free(reply);
            }
            close(connfd);
            continue;
        }
        if (req.type == JC_DREQ_ASSIGN_LIST) {
            struct jc_vec rows;
            cJSON *o = cJSON_CreateObject();
            cJSON *arr = cJSON_CreateArray();
            jc_size k;
            jc_vec_init(&rows, sizeof(struct assign_row));
            assignments_collect(app, &rows);
            for (k = 0; k < rows.len && arr != NULL; k++) {
                cJSON *row = assign_row_json(
                    (struct assign_row *)jc_vec_at(&rows, k));
                if (row != NULL) {
                    cJSON_AddItemToArray(arr, row);
                }
            }
            if (o != NULL) {
                cJSON_AddNumberToObject(o, "v", (double)JC_DAEMON_PROTO_V);
                cJSON_AddStringToObject(o, "type", "assignment.list.ok");
                cJSON_AddItemToObject(o, "assignments",
                                      arr != NULL ? arr : cJSON_CreateArray());
                daemon_write_line(connfd, o);
            } else {
                cJSON_Delete(arr);
            }
            jc_vec_free(&rows);
            close(connfd);
            continue;
        }
        if (req.type == JC_DREQ_ASSIGN_GET) {
            char path[1300];
            const char *why = NULL;
            if (assign_resolve(app, req.name, path, sizeof path, &why) == NULL) {
                daemon_write_err(connfd, why,
                    strcmp(why, "assignment.name") == 0
                    ? "name must be a plain <file>.md inside the assignment set"
                    : "no such assignment in docs/assignments/");
            } else {
                char *text = NULL;
                struct jc_assign_spec spec;
                memset(&spec, 0, sizeof(spec));
                if (jc_read_file(path, &text, NULL, reqarena) != JC_OK ||
                    jc_assign_parse(text, &spec, reqarena) != JC_OK) {
                    daemon_write_err(connfd, "assignment.unreadable",
                                     "the spec could not be read or has no task body");
                } else {
                    cJSON *o = cJSON_CreateObject();
                    if (o != NULL) {
                        char *rendered = jc_assign_render(&spec, reqarena);
                        cJSON_AddNumberToObject(o, "v",
                                                (double)JC_DAEMON_PROTO_V);
                        cJSON_AddStringToObject(o, "type", "assignment.get.ok");
                        cJSON_AddStringToObject(o, "name", req.name);
                        if (spec.title != NULL) {
                            cJSON_AddStringToObject(o, "title", spec.title);
                        }
                        if (spec.audience != NULL) {
                            cJSON_AddStringToObject(o, "audience", spec.audience);
                        }
                        if (spec.phase != NULL) {
                            cJSON_AddStringToObject(o, "phase", spec.phase);
                        }
                        if (spec.difficulty != NULL) {
                            cJSON_AddStringToObject(o, "difficulty",
                                                    spec.difficulty);
                        }
                        cJSON_AddNumberToObject(o, "points",
                                                (double)spec.points);
                        /* The verify command is returned deliberately: a grade
                         * whose basis a caller cannot inspect is an opinion. */
                        if (spec.verify != NULL) {
                            cJSON_AddStringToObject(o, "verify", spec.verify);
                        }
                        cJSON_AddNumberToObject(o, "hints",
                                                (double)spec.nhints);
                        /* M409: say when the ladder is shorter than the file,
                         * rather than presenting a truncation as a fact. */
                        cJSON_AddNumberToObject(o, "hints_skipped",
                                                (double)spec.hints_skipped);
                        cJSON_AddStringToObject(o, "task",
                                                spec.task != NULL ? spec.task : "");
                        if (rendered != NULL) {
                            cJSON_AddStringToObject(o, "rendered", rendered);
                        }
                        daemon_write_line(connfd, o);
                    }
                }
            }
            close(connfd);
            continue;
        }
        if (req.type == JC_DREQ_ASSIGN_GRADE) {
            char path[1300];
            const char *why = NULL;
            if (assign_resolve(app, req.name, path, sizeof path, &why) == NULL) {
                daemon_write_err(connfd, why,
                    strcmp(why, "assignment.name") == 0
                    ? "name must be a plain <file>.md inside the assignment set"
                    : "no such assignment in docs/assignments/");
            } else {
                struct jc_grade_out g;
                jc_grade_core(path, reqarena, &g);
                if (g.fail == JC_GRADE_NO_VERIFY) {
                    daemon_write_err(connfd, "assignment.no_verify",
                        "the spec has no `verify` command, so nothing defines "
                        "success");
                } else if (g.fail == JC_GRADE_CANNOT_RUN) {
                    /* M502 on the wire. This is NOT a failing grade and must
                     * not be reported as one: the harness could not run, which
                     * is a different fact from the work being wrong. */
                    daemon_write_err(connfd, "assignment.not_gradeable",
                        "the verify command cannot run from this workspace -- "
                        "this is not a grade");
                } else if (g.fail != JC_GRADE_NONE) {
                    daemon_write_err(connfd, "assignment.unreadable",
                        "the spec could not be read or has no task body");
                } else {
                    cJSON *o = cJSON_CreateObject();
                    if (o != NULL) {
                        cJSON *ver = cJSON_CreateObject();
                        cJSON *tst = cJSON_CreateObject();
                        cJSON_AddNumberToObject(o, "v",
                                                (double)JC_DAEMON_PROTO_V);
                        cJSON_AddStringToObject(o, "type",
                                                "assignment.grade.ok");
                        cJSON_AddStringToObject(o, "name", req.name);
                        cJSON_AddBoolToObject(o, "passed", g.res.passed);
                        cJSON_AddNumberToObject(o, "pct", (double)g.res.pct);
                        /* points AWARDED and points AVAILABLE, so a caller does
                         * not have to infer a rubric from a percentage. */
                        cJSON_AddNumberToObject(o, "points",
                            (double)(g.res.passed ? g.spec.points : 0));
                        cJSON_AddNumberToObject(o, "of",
                                                (double)g.spec.points);
                        if (tst != NULL) {
                            cJSON_AddNumberToObject(tst, "run",
                                                    (double)g.res.tests_run);
                            cJSON_AddNumberToObject(tst, "failed",
                                                    (double)g.res.tests_failed);
                            cJSON_AddItemToObject(o, "tests", tst);
                        }
                        if (ver != NULL) {
                            cJSON_AddStringToObject(ver, "command",
                                g.spec.verify != NULL ? g.spec.verify : "");
                            cJSON_AddNumberToObject(ver, "exitCode",
                                                    (double)g.verify_exit);
                            cJSON_AddItemToObject(o, "verify", ver);
                        }
                        daemon_write_line(connfd, o);
                    }
                    jc_grade_out_free(&g); /* M614: the report is ours now */
                }
            }
            close(connfd);
            continue;
        }
        if (req.type == JC_DREQ_PING) {
            const char *pong = "{\"type\":\"pong\"}\n";
            (void)!write(connfd, pong, strlen(pong));
            close(connfd);
            continue;
        }
        if (req.type == JC_DREQ_SHUTDOWN) {
            const char *ok = "{\"type\":\"bye\"}\n";
            (void)!write(connfd, ok, strlen(ok));
            close(connfd);
            stopping = 1;
            continue;
        }
        if (req.type != JC_DREQ_PROMPT) {
            close(connfd);
            continue;
        }
        /* Serve the PROMPT in a forked worker: the child gets a COW snapshot of
         * the warm app, streams this turn's output to the connection, and exits;
         * the parent keeps accepting. In plain-text mode stderr is also routed to
         * the client so a failed turn's diagnostics are visible; in jsonl mode it
         * stays OFF the socket (the structured `done` object carries the error). */
        {
            pid_t pid;
            fflush(stdout);
            fflush(stderr);
            pid = fork();
            if (pid < 0) {
                const char *err = "{\"type\":\"error\",\"message\":"
                                  "\"fork failed\"}\n";
                (void)!write(connfd, err, strlen(err));
                close(connfd);
                continue;
            }
            if (pid == 0) {
                /* Worker child: isolate, redirect fd 1 (and fd 2 in plain mode)
                 * onto the socket, run one turn, exit. Its copy of connfd is the
                 * only one open (the parent closes its copy below), so the client
                 * sees EOF the moment this child exits. */
                /* M431g: stderr joins the socket only in TEXT mode. Under json
                 * it would corrupt the single terminal object the client parses,
                 * and under jsonl it would break line-per-event framing. */
                int plain = (req.fmt == 0);
                int crc;
                signal(SIGINT, SIG_DFL);
                signal(SIGTERM, SIG_DFL);
                close(lfd);
                if (req.mode != NULL) {
                    enum jc_agent_mode m;
                    if (jc_agent_mode_parse(req.mode, &m)) {
                        jc_app_set_mode(app, m);
                    }
                }
                dup2(connfd, STDOUT_FILENO);
                if (plain) {
                    dup2(connfd, STDERR_FILENO);
                }
                app->abort_flag = 0;
                crc = run_headless(app, req.prompt, req.fmt);
                fflush(stdout);
                fflush(stderr);
                _exit(crc == 0 ? 0 : 1);
            }
            /* Parent: hand the connection to the child and track the worker. */
            close(connfd);
            dp.pid[dp.n] = pid;
            dp.start_ms[dp.n] = jc_now_millis();
            dp.n++;
        }
    }
    dpool_kill_all(&dp);
    jc_sb_free(&line);
    jc_arena_free(reqarena);
    close(lfd);
    unlink(path);
    fprintf(stderr, "daemon: stopped\n");
    return 0;
}

/* `--connect <socket> -p <prompt>`: a thin client that frames one request and
 * relays the streamed response to stdout. Needs no app of its own. */
static int run_daemon_client(struct cli_args *args, struct jc_arena *arena)
{
    struct sockaddr_un addr;
    const char *prompt = args->print_prompt;
    char *reqline;
    char cwd[1024];
    int fd;
    int rc = 0;

    if (prompt != NULL && strcmp(prompt, "-") == 0) {
        prompt = read_all_stdin(arena);
    } else if (prompt == NULL && !isatty(STDIN_FILENO)) {
        prompt = read_all_stdin(arena);
    }
    if (prompt == NULL || prompt[0] == '\0') {
        fprintf(stderr, "error: --connect needs a prompt (-p or stdin)\n");
        return 2;
    }
    if (strlen(args->daemon_connect) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "error: socket path too long (max %lu bytes): %s\n",
                (unsigned long)sizeof(addr.sun_path) - 1, args->daemon_connect);
        return 2;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "error: socket() failed\n");
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    jc_snprintf(addr.sun_path, sizeof(addr.sun_path), "%s",
                args->daemon_connect);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "error: could not connect to daemon at %s\n",
                args->daemon_connect);
        close(fd);
        return 1;
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        cwd[0] = '\0';
    }
    /* M431g: pass the format THROUGH. This was `args->output_json == 2`, so
     * --output json (1) became text on the wire -- the downgrade this fixes. */
    reqline = jc_daemon_build_prompt(prompt, cwd, NULL, args->output_json);
    if (reqline != NULL) {
        /* M332: `write` was unchecked here. A short write truncates the request
         * and the daemon then blocks on a line that never arrives, so the loop
         * matters as much as the error check. Same shape as write_all() in
         * jc_tool_parallel.c. */
        size_t len = strlen(reqline);
        size_t off = 0;
        while (off < len) {
            ssize_t w = write(fd, reqline + off, len - off);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) {
                    continue;
                }
                jc_logf(JC_LOG_WARN, "daemon: short write (%lu of %lu bytes)",
                        (unsigned long)off, (unsigned long)len);
                break;
            }
            off += (size_t)w;
        }
        free(reqline);
    }
    /* Relay the streamed response to stdout until the daemon closes. */
    for (;;) {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n) {
            rc = 1;
            break;
        }
    }
    fflush(stdout);
    close(fd);
    return rc;
}

/* `control <socket> <verb> [text...]` (M159): drive a running bounded run's
 * control channel -- status | inject <text> | pause | resume | abort. One
 * request line out, one response line back. The reply may take until the
 * run's next TOOL BOUNDARY (that is the design: commands never interleave
 * with a streaming response), so the read waits generously. Exit 0 iff the
 * server answered ok:true. */
static int run_control(const struct cli_args *args)
{
    struct sockaddr_un addr;
    const char *sock;
    const char *verb;
    char *text = NULL;
    char *reqline;
    char reply[JC_CONTROL_LINE_MAX + 1];
    jc_size used = 0;
    long deadline;
    int fd;
    int okrc = 1;

    if (args->npos < 3) {
        fprintf(stderr, "usage: jichi control <socket> "
                        "status|pause|resume|abort|inject <text...>"
                        "|mode <chat|plan>\n");
        return 2;
    }
    sock = args->pos[1];
    verb = args->pos[2];
    if (jc_control_type_parse(verb) == JC_CTL_UNKNOWN) {
        fprintf(stderr, "error: unknown control verb '%s' "
                        "(status|inject|pause|resume|abort|mode)\n", verb);
        return 2;
    }
    if (strcmp(verb, "mode") == 0) {
        /* M304: narrow the posture. Validated client-side so a typo costs a local
         * error rather than a round trip, and the one-way rule is stated up front
         * -- the server refuses a widening regardless. */
        enum jc_agent_mode m;
        if (args->npos < 4) {
            fprintf(stderr, "error: mode needs a posture "
                            "(chat|plan; narrowing only)\n");
            return 2;
        }
        if (!jc_agent_mode_parse(args->pos[3], &m)) {
            fprintf(stderr, "error: unknown mode '%s' (chat|plan|auto)\n",
                    args->pos[3]);
            return 2;
        }
        text = jc_strdup(args->pos[3]);
    }
    if (strcmp(verb, "inject") == 0) {
        /* Join the remaining positional args as the steering text. */
        struct jc_sb sb;
        int i;
        if (args->npos < 4) {
            fprintf(stderr, "error: inject needs the steering text\n");
            return 2;
        }
        jc_sb_init(&sb);
        for (i = 3; i < args->npos; i++) {
            if (i > 3) {
                jc_sb_append(&sb, " ");
            }
            jc_sb_append(&sb, args->pos[i]);
        }
        text = jc_sb_finish(&sb);
        jc_sb_free(&sb);
    }
    if (strlen(sock) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "error: socket path too long: %s\n", sock);
        free(text);
        return 2;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "error: socket() failed\n");
        free(text);
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    jc_snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "error: could not connect to %s (is the run "
                        "alive and started with --control?)\n", sock);
        close(fd);
        free(text);
        return 1;
    }
    reqline = jc_control_build_request(verb, text,
                                       args->extend &&
                                       strcmp(verb, "pause") == 0);
    free(text);
    if (reqline == NULL || write(fd, reqline, strlen(reqline)) < 0) {
        fprintf(stderr, "error: could not send the request\n");
        free(reqline);
        close(fd);
        return 1;
    }
    free(reqline);

    /* Wait for the one-line reply -- served at the run's next tool boundary,
     * which behind a slow model call can be minutes away. */
    deadline = (long)time(NULL) + 300;
    while (used < JC_CONTROL_LINE_MAX && (long)time(NULL) <= deadline) {
        fd_set rf;
        struct timeval tv;
        long n;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0) {
            continue;
        }
        n = (long)read(fd, reply + used, JC_CONTROL_LINE_MAX - used);
        if (n <= 0) {
            break;
        }
        used += (jc_size)n;
        if (memchr(reply, '\n', used) != NULL) {
            break;
        }
    }
    close(fd);
    if (used == 0) {
        fprintf(stderr, "error: no reply within 300s (the run may be inside "
                        "a long model call; the command was still queued if "
                        "the connect succeeded)\n");
        return 1;
    }
    reply[used] = '\0';
    fputs(reply, stdout);
    if (memchr(reply, '\n', used) == NULL) {
        fputc('\n', stdout);
    }
    {
        cJSON *o = cJSON_Parse(reply);
        if (o != NULL) {
            okrc = jc_json_get_bool(o, "ok", 0) ? 0 : 1;
            cJSON_Delete(o);
        }
    }
    return okrc;
}

/* Tell the operator how to bring pre-rename state forward (M170).
 *
 * The project was `jlu_continue` until the release rename, and its state lived in
 * `~/.jlu_continue`, `~/.jlu_continue.d/` and per-project `.jlu/`. The code now
 * knows only the new locations.
 *
 * Design decision -- detect and instruct, do NOT migrate. Two rejected
 * alternatives: (1) dual-path lookups, which would have meant a fallback at ~650
 * call sites and a permanently doubled surface where bugs hide; (2) moving the
 * directories automatically at startup, which silently relocates a user's
 * telemetry, sessions, calibration and checkpoints -- tens of megabytes of
 * history -- as a side effect of running an unrelated command. Printing the two
 * commands costs the operator ten seconds, cannot lose data, and leaves exactly
 * one path scheme in the tree. Fires only when the OLD path exists and the NEW
 * one does not, so it is silent for everyone else and stops on its own once the
 * move is done. */
static void warn_legacy_paths(void)
{
    static const char *const PAIRS[][2] = {
        { "%s/.jlu_continue.d", "%s/.jichi.d" },
        { "%s/.jlu_continue",   "%s/.jichi"   },
        { "%s/.config/jlu_continue", "%s/.config/jichi" },
        /* M204: the session store moved out of Continue CLI's directory, where the
         * M170 rename had left it behind. Same detect-and-instruct rule: fires
         * only while the old store exists and the new one does not. */
        { "%s/.continue/sessions", "%s/.jichi.d/sessions" }
    };
    char old[1200];
    char new_[1200];
    char cwd[1024];
    int i;
    int said = 0;

    for (i = 0; i < (int)(sizeof PAIRS / sizeof PAIRS[0]); i++) {
        jc_snprintf(old, sizeof old, PAIRS[i][0], jc_home_dir());
        jc_snprintf(new_, sizeof new_, PAIRS[i][1], jc_home_dir());
        if (!jc_file_exists(old)) {
            continue;
        }
        if (!said) {
            jc_logf(JC_LOG_WARN, "some jichi state is in an older location and "
                    "is not read automatically");
            said = 1;
        }
        /* M326v: the BOTH-EXIST case used to be silent, and it is the one that
         * loses data. Run jichi once after upgrading and it creates the new
         * directory itself -- from then on `!exists(new)` is false and this
         * warning never fired again, so nobody was told the old state was
         * stranded. Whoever noticed later reached for `mv OLD NEW`, and because
         * NEW now exists as a DIRECTORY, mv puts OLD *inside* it. Observed in
         * the field as ~/.jichi.d/.jichi.d holding 5.5 GB that jichi does not
         * read and does not mention. So: still advise the plain rename when it
         * is safe, and when it is not, say why and refuse to suggest it. */
        if (!jc_file_exists(new_)) {
            jc_logf(JC_LOG_WARN, "  mv %s %s", old, new_);
        } else {
            jc_logf(JC_LOG_WARN, "  %s AND %s both exist -- merge by hand",
                    old, new_);
            jc_logf(JC_LOG_WARN, "  do NOT `mv` the first onto the second: it "
                    "would move it INSIDE, where nothing reads it");
        }
    }

    /* The shape that mistake leaves behind, named so it can be recognised. A
     * state directory nested inside the state directory is never something
     * jichi creates. */
    {
        static const char *const NESTED[] = { ".jichi.d", ".jlu_continue.d" };
        int j;
        for (j = 0; j < (int)(sizeof NESTED / sizeof NESTED[0]); j++) {
            jc_snprintf(old, sizeof old, "%s/.jichi.d/%s", jc_home_dir(),
                        NESTED[j]);
            if (jc_file_exists(old)) {
                jc_logf(JC_LOG_WARN, "a state directory is nested inside the "
                        "state directory and is NOT read: %s", old);
                jc_logf(JC_LOG_WARN, "  it is almost certainly an interrupted "
                        "migration; move its contents up one level or delete it");
            }
        }
    }
    /* The per-project assets (agents, skills, commands, memory, glossary) are the
     * ones whose absence is most confusing: the agent simply behaves as if the
     * project had never been configured. */
    if (getcwd(cwd, sizeof cwd) != NULL) {
        jc_snprintf(old, sizeof old, "%s/.jlu", cwd);
        jc_snprintf(new_, sizeof new_, "%s/.jichi", cwd);
        if (jc_file_exists(old) && !jc_file_exists(new_)) {
            if (!said) {
                jc_logf(JC_LOG_WARN, "some jichi state is in an older location and "
                        "is not read automatically");
                said = 1;
            }
            jc_logf(JC_LOG_WARN, "  mv .jlu .jichi        "
                    "(this project's agents/skills/commands/memory)");
        }
    }
    if (said) {
        jc_logf(JC_LOG_WARN, "  see docs/MIGRATION.md; this notice stops once "
                "the new paths exist");
    }
}

int main(int argc, char **argv)
{
    struct cli_args args;
    struct jc_arena *arena;
    struct jc_app app;
    struct jc_tool_registry tools;
    struct jc_mcp_manager mcp;
    struct jc_user_tool_mgr umgr;
    struct jc_bg_mgr bgmgr;
    struct jc_lsp_manager lsp;
    struct jc_snapshot_mgr snaps;
    struct jc_envelope env;
    int lease_held = 0;                        /* M431e */
    enum jc_lease_mode lease_mode_used = JC_LEASE_WARN;
    int env_active_run = 0;
    struct jc_control ctl;      /* M159: mid-run control socket */
    int control_open = 0;
    struct jc_eventlog telemetry;
    int telem_active = 0;
    int rc;
    int exit_code = 0;

    /* Localize wall-clock time display only (LC_TIME): the assistant-response
     * timestamp uses %X / the locale's format. Deliberately NOT LC_ALL/LC_NUMERIC
     * -- a locale decimal comma would corrupt cJSON number output (see
     * jc_locale_group_sep, which restores LC_NUMERIC="C"). */
    setlocale(LC_TIME, "");

    /* M218: pin the malloc tunables before anything allocates in volume, so
     * the per-call/per-retry request bodies mmap+munmap instead of ratcheting
     * the dynamic threshold and growing brk for the life of the process. */
    jc_mem_tune();

    rc = parse_args(argc, argv, &args);
    if (rc == 1) {
        return 0;
    }
    if (rc < 0) {
        return 2;
    }
    if (args.verbose) {
        jc_log_set_level(JC_LOG_DEBUG);
    }
    if (args.quiet) {
        jc_log_set_level(JC_LOG_NONE); /* quiet beats verbose */
    }
    /* A closed downstream pipe (e.g. `... | head`) must not kill us; detect the
     * short write in hl_text instead and stop cleanly. */
    signal(SIGPIPE, SIG_IGN);

    warn_legacy_paths();

    /* Export our own executable path so scaffolded commands / subtasks can
     * re-invoke jichi regardless of how it was launched -- installed on PATH, a
     * ./jichi wrapper, or an absolute build path. The scaffolded /learn command
     * prefers "$JICHI_BIN" over a bare `jichi`, so mining telemetry works
     * even when jichi isn't on PATH (otherwise the mentor "learns" from a
     * `jichi: not found` shell error). */
    {
        char selfpath[JC_PATH_MAX];
        ssize_t sn = readlink("/proc/self/exe", selfpath, sizeof(selfpath) - 1);
        if (sn > 0) {
            selfpath[sn] = '\0';
            setenv("JICHI_BIN", selfpath, 1);
        } else if (argv[0] != NULL && argv[0][0] != '\0') {
            setenv("JICHI_BIN", argv[0], 1);
        }
    }

    /* M376: -p beside a dispatched subcommand used to pick the subcommand
     * and silently discard the prompt (`-p "x" describe` ran describe; the
     * M375 sin one dispatch layer up), so it is refused BEFORE the first
     * pos[0] dispatch below. pos[0] == print_prompt (the same argv pointer)
     * is the legitimate positional-prompt case. --acp likewise selects a
     * run mode that cannot also take a -p prompt. */
    if (args.print_prompt != NULL && args.npos > 0 &&
        args.pos[0] != args.print_prompt) {
        fprintf(stderr, "error: unexpected argument '%s' -- the prompt was "
                "already given via -p/--print\n", args.pos[0]);
        return 2;
    }
    if (args.acp && args.print_prompt != NULL) {
        fprintf(stderr, "error: --acp and -p/--print are different run "
                "modes; use one\n");
        return 2;
    }

    /* `describe` dumps jichi's machine-readable interface contract and exits.
     * Needs no config/provider/network, so a driving agent can introspect it
     * on a fresh machine. */
    if (args.npos > 0 && strcmp(args.pos[0], "describe") == 0) {
        return run_describe(args.output_json == 1);
    }

    arena = jc_arena_new(0);
    if (arena == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    /* `ls` subcommand lists sessions and exits (no provider needed). */
    if (args.print_prompt != NULL && strcmp(args.print_prompt, "ls") == 0) {
        int code = run_ls(arena, args.all, args.output_json == 1);
        jc_arena_free(arena);
        return code;
    }

    /* `prune [--keep N] [--older-than <dur>] [--dry-run]`: session-store
     * hygiene (M219). Filesystem only. */
    if (args.npos > 0 && strcmp(args.pos[0], "prune") == 0) {
        int code = run_prune(arena, &args);
        jc_arena_free(arena);
        return code;
    }

    /* `export [<id|prefix>] [--html] [-o file]`: render a saved session as a
     * Markdown/HTML transcript and exit (no provider or network). */
    if (args.npos > 0 && strcmp(args.pos[0], "export") == 0) {
        int code = run_export(arena, &args);
        jc_arena_free(arena);
        return code;
    }

    /* `init [pack]` scaffolds project assets. Filesystem only — needs neither a
     * config nor a model, so it works in a brand-new project (run it before any
     * config load so a missing/empty config is irrelevant). */
    if (args.npos > 0 && strcmp(args.pos[0], "init") == 0) {
        int code = run_init(&args);
        jc_arena_free(arena);
        return code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "setup") == 0) {
        int code = run_setup(&args, arena);
        jc_arena_free(arena);
        return code;
    }
    /* `--connect <socket>`: a thin client to a running daemon. Needs no app of
     * its own, so short-circuit before the (expensive) config/app setup. */
    if (args.daemon_connect != NULL) {
        int code = run_daemon_client(&args, arena);
        jc_arena_free(arena);
        return code;
    }
    /* `assign`/`grade`: machine-checkable assignment specs (M103). Offline
     * (grade runs the spec's verify command via /bin/sh), so no app needed. */
    if (args.npos > 0 && strcmp(args.pos[0], "assign") == 0) {
        int code = run_assign(&args, arena);
        jc_arena_free(arena);
        return code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "hint") == 0) {
        int code = run_hint(&args, arena);
        jc_arena_free(arena);
        return code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "grade") == 0) {
        int code = run_grade(&args, arena);
        jc_arena_free(arena);
        return code;
    }
    /* `dream [path]`: propose-only sleep-consolidation over telemetry (M102).
     * Offline; writes a dated draft outside any workspace. */
    /* `control <socket> <verb> [text...]` (M159): offline client for a
     * running bounded run's control channel. No config/app needed. */
    if (args.npos > 0 && strcmp(args.pos[0], "control") == 0) {
        int code = run_control(&args);
        jc_arena_free(arena);
        return code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "dream") == 0) {
        int code = run_dream(arena,
                             args.npos > 1 ? args.pos[1] : NULL,
                             args.telemetry_workspace);
        jc_arena_free(arena);
        return code;
    }
    /* `improve [specs-dir]`: the synthesis loop (M109) -- reflect + grade a
     * spec suite for a pass-rate tracked over time + a propose-only report. */
    if (args.npos > 0 && strcmp(args.pos[0], "improve") == 0 &&
        !args.improve_attempt) {
        int code = run_improve(arena, args.npos > 1 ? args.pos[1] : NULL);
        jc_arena_free(arena);
        return code;
    }

    memset(&app, 0, sizeof(app));
    app.arena = arena;
    app.constraints_on = 1;        /* M110: enforce active constraints (default) */
    app.constraints_autoadopt = 1; /* M110: AUTO scans+enforces from messages    */
    jc_vec_init(&app.read_files, sizeof(char *));
    jc_vec_init(&app.read_recs, sizeof(struct jc_read_rec)); /* M231 */
    app.todos = NULL; /* M606: the live session's list; set when one opens */
    jc_board_init(&app.board);
    jc_agentdef_set_init(&app.agents);
    jc_command_set_init(&app.commands);
    jc_skill_set_init(&app.skills);
    jc_output_style_set_init(&app.output_styles);
    app.resume = args.resume;
    app.session_id = args.session_id;
    /* M341: the flag wins, else the environment -- so ACP, the daemon and any
     * surface that never parses argv can still be asked for a dump without
     * plumbing the option through each of them. Off unless one is set. */
    app.dump_requests = (args.dump_requests != NULL)
        ? args.dump_requests : getenv("JICHI_DUMP_REQUESTS");
    if (app.dump_requests != NULL && app.dump_requests[0] == '\0') {
        app.dump_requests = NULL;
    }
    if (app.dump_requests != NULL) {
        /* Loud on purpose: this writes every prompt, file excerpt and tool
         * result jichi sends into a directory, and a user who forgets it is on
         * has a growing transcript of their work on disk. */
        fprintf(stderr, "note: dumping every model request body to %s "
                        "(diagnostic; contains your prompts and file contents)\n",
                app.dump_requests);
    }
    app.session_all = args.all;
    app.image_args = (args.nimages > 0) ? args.images : NULL;
    app.image_args_n = args.nimages;
    app.quiet = args.quiet;
    app.heartbeat_secs = args.heartbeat_secs; /* M165 */
    app.no_session = args.no_session;
    app.abort_flag = 0;

    {
        /* Auto-lite: on a low-RAM machine, default to the lean profile.
         * Precedence (M272): an explicit flag (--lite / --no-lite) wins, then
         * an explicit config "lowResource" key, then this detection -- so the
         * hint is JC_LITE_HINT_AUTO, not _ON, and the config can veto it. The
         * notice prints after the load, only when auto-lite actually won. */
        int low = JC_LITE_HINT_NONE;
        unsigned long auto_mb = 0;
        if (args.lite) {
            low = JC_LITE_HINT_ON;
        } else if (args.no_lite) {
            low = JC_LITE_HINT_OFF;
        } else {
            auto_mb = jc_mem_total_mb();
            if (jc_resource_tier(auto_mb, jc_cpu_count()) != JC_RES_NORMAL) {
                low = JC_LITE_HINT_AUTO;
            }
        }
        /* Resolve the single config source (M128/M129): a --config file, or one
         * of the inline forms -- --config-json, --config-json-b64 (base64), or
         * --config-stdin / "--config-json -" (JSON on stdin). At most one. */
        {
        const char *inline_cfg = args.config_json; /* NULL unless inline */
        int nsrc = (args.config_path != NULL) + (args.config_json != NULL) +
                   (args.config_json_b64 != NULL) + (args.config_stdin != 0);
        if (nsrc > 1) {
            fprintf(stderr, "error: --config, --config-json, --config-json-b64, "
                            "and --config-stdin are mutually exclusive\n");
            jc_arena_free(arena);
            return 1;
        }
        if (args.config_json_b64 != NULL) {
            inline_cfg = decode_b64_arg(args.config_json_b64, arena);
            if (inline_cfg == NULL) {
                fprintf(stderr, "error: --config-json-b64: invalid or empty "
                                "base64\n");
                jc_arena_free(arena);
                return 1;
            }
        } else if (args.config_stdin) {
            /* Config on stdin => stdin is consumed here; the prompt must then
             * come from -p / --prompt-b64 (guarded at the prompt-read sites). */
            inline_cfg = read_all_stdin(arena);
        }
        if (inline_cfg != NULL) {
            if (jc_config_load_json(inline_cfg, low, &app.config, arena)
                != JC_OK) {
                fprintf(stderr, "error: invalid or empty config JSON\n");
                jc_arena_free(arena);
                return 1;
            }
            /* Refine the source label to the actual entry point (M129). */
            if (args.config_json_b64 != NULL) {
                jc_snprintf(app.config.config_sources,
                            sizeof app.config.config_sources,
                            "inline (--config-json-b64)");
            } else if (args.config_stdin) {
                jc_snprintf(app.config.config_sources,
                            sizeof app.config.config_sources,
                            "stdin (--config-json)");
            }
        } else if (jc_config_load(args.config_path, low, &app.config, arena)
                   != JC_OK) {
            /* The reason was named by the config loader; this is the context
             * and the way out. Without it, a config that exists but cannot be
             * parsed exited 1 with nothing on either stream -- the two other
             * jc_config_load call sites (advisor, onboard) both report, and
             * this, the primary path, did not. */
            fprintf(stderr, "[jichi] could not load configuration%s%s -- fix "
                            "the file above, or pass --config <file> to use "
                            "another one\n",
                    args.config_path != NULL ? ": " : "",
                    args.config_path != NULL ? args.config_path : "");
            jc_arena_free(arena);
            return 1;
        }
        if (low == JC_LITE_HINT_AUTO && app.config.low_resource
            && !args.quiet) {
            fprintf(stderr, "[jichi] low RAM (%lu MB) detected; enabling "
                            "lite profile (--no-lite or \"lowResource\": "
                            "false to disable)\n", auto_mb);
        }
        }
    }
    /* Register every model's resolved API key for redaction (M24), and the env
     * NAME so a forked child (a shell tool, hook, MCP stdio server, verifier)
     * can't read it (M130). The keys live on the session arena, so the stored
     * pointers stay valid.
     *
     * M608: this block used to sit ~1,100 lines below, AFTER the subcommand
     * dispatch chain -- so every child a subcommand forked before the agent loop
     * existed ran against an EMPTY registry: `brief-check --verify CMD` handed
     * CMD the configured key variable intact (measured: count=1 in the child's
     * environment; tests/smoke/secret_env_subcommands.sh). M444 fixed the same
     * ordering for the envelope's arming; this is the sibling. Armed here, the
     * moment the config is final and before anything can fork. */
    {
        int ki;
        int kn = jc_config_model_count(&app.config);
        for (ki = 0; ki < kn; ki++) {
            struct jc_model_cfg *km = jc_config_model_at(&app.config, ki);
            if (km != NULL) {
                jc_redact_register(km->api_key);
                jc_proc_secret_env_add(km->api_key_env);
            }
        }
        jc_redact_register(app.config.search.api_key);
        jc_proc_secret_env_add(app.config.search.api_key_env);
    }
    /* --prompt-b64 (M129): decode the base64 prompt into print_prompt. Mutually
     * exclusive with an explicit -p; the decoded text then flows through the
     * normal headless prompt path (no stdin needed). */
    if (args.prompt_b64 != NULL) {
        if (args.print_prompt != NULL) {
            fprintf(stderr, "error: --prompt-b64 and -p are mutually "
                            "exclusive\n");
            jc_arena_free(arena);
            return 1;
        }
        args.print_prompt = decode_b64_arg(args.prompt_b64, arena);
        if (args.print_prompt == NULL) {
            fprintf(stderr, "error: --prompt-b64: invalid or empty base64\n");
            jc_arena_free(arena);
            return 1;
        }
    }
    /* Config-on-stdin consumes stdin, so the prompt cannot also read it (M129):
     * reject an explicit "-p -" and the implicit "no -p on a non-TTY => stdin"
     * case. An explicit -p "text" or --prompt-b64 is fine. */
    if (args.config_stdin) {
        int wants_stdin_prompt =
            (args.print_prompt != NULL && strcmp(args.print_prompt, "-") == 0) ||
            (args.print_prompt == NULL && !args.no_stdin &&
             !isatty(STDIN_FILENO));
        if (wants_stdin_prompt) {
            fprintf(stderr, "error: stdin was consumed by the config "
                            "(--config-stdin); pass the prompt via -p \"text\" "
                            "or --prompt-b64\n");
            jc_arena_free(arena);
            return 1;
        }
    }
    /* First-run hint: no config file was found and the active model has no key,
     * so the first model call would just fail. Point the user at setup/doctor
     * (stderr, so stdout stays clean for piping). */
    if (app.config.from_defaults && !args.quiet &&
        (app.config.model.api_key == NULL ||
         app.config.model.api_key[0] == '\0')) {
        fprintf(stderr,
            "No config found and no API key set. Run `jichi setup` for "
            "a guided setup,\nimport an existing one with `jichi setup "
            "--import <config>`, or\n`jichi doctor` to see what's "
            "missing. (Set ANTHROPIC_API_KEY / OPENAI_API_KEY\nto run with no "
            "config file.)\n");
    }
    /* M77: load the persisted per-model token-estimate calibration (kept OUTSIDE
     * any workspace). Learned from usage as the agent runs; applied to every
     * context decision that reads the byte estimate; saved at teardown. */
    {
        char cpath[1024];
        jc_calib_init(&app.calib, arena);
        jc_snprintf(cpath, sizeof(cpath), "%s/.jichi.d/calibration.json",
                    jc_home_dir());
        jc_calib_load(&app.calib, cpath);
    }
    if (args.model_override != NULL) {
        /* `--model X` first tries to *select a configured model* by the same
         * rules as `models`/embed/rerank (1-based index, or a case-insensitive
         * substring of a model's name or id; see jc_config_find_model). That is
         * the intuitive meaning and keeps the prompt/header, apiBase, key, and
         * roles consistent with the entry chosen.
         *
         * Only when nothing matches do we fall back to the old behavior:
         * treat X as a raw backend model id on the *active* entry (reusing its
         * apiBase/key). That's the power-user escape hatch for hitting a model
         * the config doesn't list yet on the same server. The note keeps that
         * fallback from being a silent surprise. */
        int mi = jc_config_find_model(&app.config, args.model_override);
        if (mi >= 0) {
            jc_config_set_active(&app.config, mi);
        } else {
            app.config.model.model = jc_arena_strdup(arena,
                                                     args.model_override);
            if (!args.quiet) {
                fprintf(stderr, "[model] '%s' matches no configured model; "
                        "using it as a raw id on '%s'\n", args.model_override,
                        app.config.model.name != NULL ? app.config.model.name
                                                       : "(active)");
            }
        }
    }
    /* CLI routing flags override the config's routing block. */
    if (args.route_fast != NULL) {
        app.config.routing.fast = jc_arena_strdup(arena, args.route_fast);
        app.config.routing.enabled = 1;
    }
    if (args.route_strong != NULL) {
        app.config.routing.strong = jc_arena_strdup(arena, args.route_strong);
        app.config.routing.enabled = 1;
    }
    if (args.no_route) {
        app.config.routing.enabled = 0;
    }
    /* M411: an explicit `--model` PINS the run. Everywhere else in jichi an
     * explicit CLI flag beats config; routing's turn-start route-to-fast made
     * a config POLICY beat an explicit CLI CHOICE -- `status --model X`
     * printed X as active, the first journal event re-routed to the fast
     * tier, and with -q the [route] stderr line was the only witness and it
     * was silenced (measured: work addressed to a 31B model was silently done
     * by the coder tier). Naming tiers on the command line
     * (--route-fast/--route-strong) is the escape hatch: that user asked for
     * routing in the same breath, so --model then only picks the starting
     * model. The note respects -q like its `[model]` sibling above: silence
     * was only dangerous while it hid an OVERRIDE -- once the explicit choice
     * wins, there is nothing surprising left to hide. */
    if (args.model_override != NULL && app.config.routing.enabled &&
        args.route_fast == NULL && args.route_strong == NULL) {
        int rfi;
        int rsi;
        if (jc_config_routing_resolve(&app.config, &rfi, &rsi)) {
            app.config.routing.enabled = 0;
            if (!args.quiet) {
                fprintf(stderr, "[route] --model pins '%s' for this run; "
                        "tiered routing disabled (add --route-fast/"
                        "--route-strong to keep it)\n", args.model_override);
            }
        }
    }
    if (args.route_on_stall) {
        app.config.routing.escalate_on_stall = 1;
    } else if (args.no_route_on_stall) {
        app.config.routing.escalate_on_stall = 0;
    }
    if (args.route_on_context >= 0) {
        app.config.routing.escalate_on_context = args.route_on_context;
    }
    /* CLI model-call timeout overrides (M22b): highest precedence tier. */
    if (args.timeout_connect >= 0) {
        app.config.timeouts_cli.connect = args.timeout_connect;
    }
    if (args.timeout_stall >= 0) {
        app.config.timeouts_cli.stall = args.timeout_stall;
    }
    if (args.timeout_request >= 0) {
        app.config.timeouts_cli.request = args.timeout_request;
    }
    if (args.review) {
        app.config.self_review = 1;
    } else if (args.no_self_review) {
        app.config.self_review = 0;
    }
    if (args.path_fence) {
        app.config.path_fence = 1;
    } else if (args.no_path_fence) {
        app.config.path_fence = 0;
    }
    /* --reference-root paths add to the config's read-allowed roots (M54). */
    {
        int ri;
        for (ri = 0; ri < args.n_reference_root; ri++) {
            char *p = jc_arena_strdup(arena, args.reference_root[ri]);
            if (p != NULL) {
                jc_vec_push(&app.config.reference_roots, &p);
            }
        }
    }
    /* --prompt-cache / --no-prompt-cache override the global, then re-resolve the
     * per-model effective flag the providers read (M31d). */
    if (args.prompt_cache) {
        app.config.prompt_cache = 1;
    } else if (args.no_prompt_cache) {
        app.config.prompt_cache = 0;
    }
    if (args.prompt_cache || args.no_prompt_cache) {
        jc_config_resolve_prompt_cache(&app.config);
    }
    /* M440: applied AFTER the cache re-resolve above, because `auto` reads the
     * resolved per-model prompt_cache -- ordering them the other way would decide
     * the cost section against a stale cache verdict. */
    if (args.cost_model) {
        app.config.cost_model = 1;
    } else if (args.no_cost_model) {
        app.config.cost_model = 0;
    }
    /* --context-limit overrides the effective context budget (M73): the lever
     * to use when a served model's real context is smaller than its (unset or
     * over-stated) contextLength, so the system-prompt fit + compaction size
     * to the real window. */
    if (args.context_limit > 0) {
        app.config.context_limit = args.context_limit;
    }
    /* --tool-profile overrides the config (M74). */
    if (args.tool_profile != NULL) {
        if (strcmp(args.tool_profile, "core") == 0) {
            app.config.tool_profile = 1;
        } else if (strcmp(args.tool_profile, "full") == 0) {
            app.config.tool_profile = 0;
        } else {
            app.config.tool_profile = -1; /* auto */
        }
    }
    /* --auto-context / --no-auto-context override the config (M61). */
    if (args.auto_context) {
        app.config.auto_context = 1;
    } else if (args.no_auto_context) {
        app.config.auto_context = 0;
    }
    /* --learn-on-stop / --no-learn-on-stop override the config (M71). */
    if (args.learn_on_stop) {
        app.config.learn_on_stop = 1;
    } else if (args.no_learn_on_stop) {
        app.config.learn_on_stop = 0;
    }
    if (args.no_hooks) {
        app.config.hooks_enabled = 0;
    }
    if (args.config_editable) {
        app.config.config_editable = 1; /* M112: allow config edits this run */
    }
    if (args.mem_budget_mb > 0) {
        app.config.mem_budget_mb = args.mem_budget_mb; /* M117 */
    }
    if (args.run_timeout > 0) {
        app.config.run_timeout_cli = args.run_timeout; /* wins over runTimeout */
    }
    if (args.accessible) {
        app.config.accessible = 1; /* M118: reduce motion */
    }
    /* M566: resolve the UI message language ONCE, here, for every front-end.
     * $JICHI_LANG > config `language` (the M135 answer language) > $LANG; a
     * non-UTF-8 terminal falls back to English like the glyphs do.
     *
     * WHY HERE AND NOT IN THE TUI, which is where M137 put it. This is the
     * last point at which the config is final and the first that precedes
     * every renderer: `run_headless` (three call sites below), `jc_tui_run`,
     * and `run_daemon`, whose forked workers inherit it. With the call inside
     * the TUI, a headless run never resolved anything -- so `JICHI_LANG=de`
     * printed English chrome and a translator's work reached one front-end of
     * two. Being downstream of the --accessible fold matters as well: both
     * decisions must be settled before the first line is rendered. */
    jc_msg_set_lang(jc_msg_lang_resolve(app.config.language,
                                        getenv("JICHI_LANG"), getenv("LANG"),
                                        jc_locale_is_utf8()));
    /* --output-style overrides the configured one; fold it into the config so
     * both the interactive path and the `sysmsg`/`output-styles` subcommands see
     * a single source of truth. (Points into argv; never freed.) */
    if (args.output_style != NULL) {
        app.config.output_style = (char *)args.output_style;
    }
    /* --language overrides the configured answer language (M135). Points into
     * argv; never freed. */
    if (args.language != NULL) {
        app.config.language = (char *)args.language;
    }
    /* M144: CLI tri-state wins over the config bool (default off). */
    if (args.parallel_verify != 0) {
        app.config.parallel_verify = (args.parallel_verify > 0);
    }
    /* M153: --privileged-commands overrides the configured posture. */
    if (args.privileged_commands != NULL) {
        if (strcmp(args.privileged_commands, "deny") == 0) {
            app.config.privileged_commands = JC_PRIVPOL_DENY;
        } else if (strcmp(args.privileged_commands, "allow") == 0) {
            app.config.privileged_commands = JC_PRIVPOL_ALLOW;
        } else if (strcmp(args.privileged_commands, "ask") == 0) {
            app.config.privileged_commands = JC_PRIVPOL_ASK;
        } else {
            fprintf(stderr, "error: --privileged-commands must be "
                            "ask|deny|allow\n");
            return 2;
        }
    }
    /* M163a: --kinetic-commands overrides the configured kinetic posture. */
    if (args.kinetic_commands != NULL) {
        if (strcmp(args.kinetic_commands, "deny") == 0) {
            app.config.kinetic_commands = JC_PRIVPOL_DENY;
        } else if (strcmp(args.kinetic_commands, "allow") == 0) {
            app.config.kinetic_commands = JC_PRIVPOL_ALLOW;
        } else if (strcmp(args.kinetic_commands, "ask") == 0) {
            app.config.kinetic_commands = JC_PRIVPOL_ASK;
        } else {
            fprintf(stderr, "error: --kinetic-commands must be "
                            "ask|deny|allow\n");
            return 2;
        }
    }
    if (args.no_markdown) {
        app.config.markdown = 0;
    }
    if (args.voice != 0) {
        app.config.voice = (args.voice > 0) ? 1 : 0;
    }
    if (args.type_ahead != 0) {
        app.config.type_ahead = (args.type_ahead > 0) ? 1 : 0;
    }
    if (args.fuzzy_edit != 0) {
        app.config.fuzzy_edit = (args.fuzzy_edit > 0) ? 1 : 0;
    }
    /* Completion notification (F6): CLI overrides config. */
    if (args.bell) {
        app.config.notify_bell = 1;
    }
    if (args.notify != NULL) {
        app.config.notify = (char *)args.notify; /* argv-owned; never freed */
    }
    /* Color: auto (follow TTY) unless NO_COLOR is set or a flag forces it.
     * An explicit --color overrides NO_COLOR (user intent wins). */
    app.color_mode = -1;
    if (getenv("NO_COLOR") != NULL) {
        app.color_mode = 0;
    }
    if (args.no_color) {
        app.color_mode = 0;
    }
    if (args.color) {
        app.color_mode = 1;
    }
    if (getcwd(app.cwd, sizeof(app.cwd)) == NULL) {
        app.cwd[0] = '.';
        app.cwd[1] = '\0';
    }
    /* Canonical workspace root for the path-containment fence (M24). On failure
     * leave it empty, which disables the fence (fail open to disk) rather than
     * blocking every file op. */
    if (jc_path_resolve(app.cwd, app.root, sizeof(app.root)) != JC_OK) {
        app.root[0] = '\0';
    }

    /* Resolve the starting mode: --plan beats --auto beats the config default.
     * --readonly then pins a read-only fence on top of the chosen mode. */
    {
        int mode = app.config.default_mode;
        if (args.plan) {
            mode = JC_MODE_PLAN;
        } else if (args.auto_approve) {
            mode = JC_MODE_AUTO;
        }
        if (args.plan && args.auto_approve) {
            fprintf(stderr,
                    "warning: --plan and --auto both given; using plan mode\n");
        }
        jc_app_set_mode(&app, mode);
        /* An explicit --plan/--auto pins the mode so a resumed session's saved
         * mode does not override the user's choice for this run. */
        app.mode_pinned = (args.plan || args.auto_approve);
        if (args.readonly) {
            app.readonly = 1;
        }
    }

    /* embed/rerank/index subcommands: need config + network but no chat
     * provider or tool registry. */
    if (args.npos > 0 && is_index_subcommand(args.pos[0])) {
        int sub_code = 0;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        jc_http_global_init();
        run_index_subcommand(&app, &args, &sub_code);
        jc_http_global_cleanup();
        jc_index_free(app.index);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `mcp` subcommand: connect + list MCP tools, then exit (no provider). */
    if (args.npos > 0 && strcmp(args.pos[0], "mcp") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        jc_http_global_init();
        sub_code = run_mcp(&app, &args);
        jc_http_global_cleanup();
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `lsp <file>...` subcommand: print language-server diagnostics, then exit
     * (no provider or network). */
    if (args.npos > 0 && strcmp(args.pos[0], "lsp") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        sub_code = run_lsp(&app, &args);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `test [command]` subcommand: run the tests, print a parsed summary,
     * exit with the command's exit code (no provider or network). */
    if (args.npos > 0 && strcmp(args.pos[0], "test") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        sub_code = run_test(&app, &args);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `status` subcommand: print the resolved session config (no provider). */
    if (args.npos > 0 && strcmp(args.pos[0], "status") == 0) {
        int sub_code = run_status(&app, args.output_json == 1);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `map` subcommand: print the repository map (no provider or network). */
    if (args.npos > 0 && strcmp(args.pos[0], "map") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        sub_code = run_map(&app);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `memory` subcommand: print the persisted agent memory. */
    if (args.npos > 0 && strcmp(args.pos[0], "memory") == 0) {
        char *mem = jc_memory_load(&app);
        if (mem != NULL && mem[0] != '\0') {
            printf("%s\n", mem);
        } else {
            printf("(no remembered notes in %s/.jichi/memory.md)\n", app.cwd);
        }
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return 0;
    }

    /* `config` subcommand (M112): inspect or edit config (headless/automation).
     *
     * M326c: this handler used to serve show/set/telemetry only, while a
     * SECOND `config` dispatch further down called a run_config() serving
     * path/show/validate. The first dispatch always returns, so that one was
     * unreachable -- `config path` and `config validate` were advertised by
     * --help, by `describe` (an interface-contract surface) and by CONVERT.md,
     * and answered "config editing is off": the read-only verbs fell through
     * to the editable gate. Both are folded in here, ahead of that gate since
     * neither writes anything, and the dead copy is gone. `show` keeps THIS
     * handler's summary (the live behaviour, pinned by tui_model_name.sh), not
     * the dead copy's raw-file dump. */
    if (args.npos > 0 && strcmp(args.pos[0], "config") == 0) {
        const char *sub = args.npos > 1 ? args.pos[1] : "show";
        int rc = 0;
        if (strcmp(sub, "path") == 0) {
            /* The resolved source(s). Not re-derived from the precedence rules
             * -- config_sources is what jc_config_load actually loaded, so this
             * cannot drift from it, and it names BOTH files in the merged case
             * (the dead copy reported one, which was wrong whenever a project
             * config overlaid the global). */
            printf("%s\n", app.config.config_sources[0] != '\0'
                   ? app.config.config_sources : "built-in defaults");
        } else if (strcmp(sub, "validate") == 0) {
            /* Reaching here means it parsed: main exits 1 on a malformed or
             * missing config before any subcommand dispatch. */
            printf("OK: %s\n", app.config.config_sources[0] != '\0'
                   ? app.config.config_sources : "built-in defaults");
            printf("  %d model(s); active: %s\n",
                   jc_config_model_count(&app.config),
                   app.config.model.model != NULL
                   ? app.config.model.model : "?");
        } else if (strcmp(sub, "show") == 0) {
            const char *lvln[3];
            int lv = app.config.log_level;
            char mdisp[192];
            lvln[0] = "off"; lvln[1] = "metrics"; lvln[2] = "full";
            /* M296: "(none)" was wrong as well as unhelpful -- a config with no
             * `name` still has a model, it just has no intent label. */
            jc_model_display(app.config.model.name, app.config.model.model,
                             mdisp, sizeof mdisp);
            printf("model:      %s\n", mdisp);
            printf("telemetry:  %s\n", (lv >= 0 && lv <= 2) ? lvln[lv] : "off");
            printf("snapshots:  %s\n", app.config.snapshots ? "on" : "off");
            printf("editable:   %s\n",
                   app.config.config_editable ? "on"
                   : "off (configEditable:true or --config-editable)");
        } else if (strcmp(sub, "set") == 0 || strcmp(sub, "telemetry") == 0) {
            /* The editing verbs, gated. The gate is tested INSIDE this branch
             * rather than as a catch-all `else if`: as a catch-all it also
             * swallowed every unknown verb, so a typo was answered with
             * "config editing is off" -- an error about a permission the user
             * had not asked for, naming a key that would not have helped. */
            if (!app.config.config_editable) {
                fprintf(stderr, "config editing is off; set configEditable:true "
                        "in your config, or pass --config-editable\n");
                rc = 1;
            } else if (strcmp(sub, "set") == 0 && args.npos >= 4) {
                char msg[512];
                jc_status st = jc_configedit_apply(args.config_path,
                                                   args.pos[2], args.pos[3],
                                                   msg, sizeof msg);
                printf("%s\n", msg);
                rc = (st == JC_OK) ? 0 : 1;
            } else if (strcmp(sub, "telemetry") == 0 && args.npos >= 3) {
                char msg[512];
                const char *v = args.pos[2];
                jc_status st;
                if (strcmp(v, "on") == 0) v = "metrics";
                st = jc_configedit_apply(args.config_path, "logging", v,
                                         msg, sizeof msg);
                printf("%s\n", msg);
                rc = (st == JC_OK) ? 0 : 1;
            } else {
                fprintf(stderr, strcmp(sub, "set") == 0
                        ? "usage: config set <key> <value>\n"
                        : "usage: config telemetry <off|metrics|full|on>\n");
                rc = 1;
            }
        } else {
            fprintf(stderr, "usage: config [path|show|validate"
                    "|set <key> <value>|telemetry <level>]\n");
            fprintf(stderr, "  path|show|validate are read-only; "
                    "set/telemetry need configEditable\n");
            rc = 1;
        }
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return rc;
    }

    /* `packages` subcommand (M115): browse packs/presets, or get an
     * LLM-assisted recommendation (fallback = the catalog). */
    if (args.npos > 0 && strcmp(args.pos[0], "packages") == 0) {
        const char *sub = args.npos > 1 ? args.pos[1] : "list";
        struct jc_sb sb;
        jc_sb_init(&sb);
        if (strcmp(sub, "recommend") == 0 &&
            app.config.model.name != NULL && app.config.model.name[0] != '\0') {
            struct jc_provider *prov = jc_provider_create(&app.config.model);
            char *ans = NULL;
            if (prov != NULL) {
                struct jc_sb sum;
                struct jc_vec names;
                jc_size i;
                jc_sb_init(&sum);
                jc_sb_append(&sum, "top-level files: ");
                jc_vec_init(&names, sizeof(char *));
                if (jc_list_dir(app.cwd, &names, arena) == JC_OK) {
                    for (i = 0; i < names.len && i < 30; i++) {
                        if (i > 0) jc_sb_append(&sum, ", ");
                        jc_sb_append(&sum, *(char **)jc_vec_at(&names, i));
                    }
                }
                jc_vec_free(&names);
                jc_packages_recommend_prompt(sum.data != NULL ? sum.data : "",
                                             &sb);
                ans = jc_oneshot(prov, NULL, sb.data, 60, &app.abort_flag);
                jc_sb_free(&sum);
                prov->vt->free(prov);
            }
            if (ans != NULL) {
                printf("%s\n", ans);
                free(ans);
            } else {
                struct jc_sb cat;
                fprintf(stderr, "(recommendation unavailable; catalog:)\n");
                jc_sb_init(&cat);
                jc_packages_render_catalog(&cat);
                printf("%s", cat.data != NULL ? cat.data : "");
                jc_sb_free(&cat);
            }
        } else {
            jc_packages_render_catalog(&sb);
            printf("%s", sb.data != NULL ? sb.data : "");
            if (strcmp(sub, "recommend") == 0) {
                printf("\n(no model configured; configure one for an "
                       "LLM recommendation.)\n");
            }
        }
        jc_sb_free(&sb);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return 0;
    }

    /* `benchmark` subcommand (M113): score the project's config (offline). */
    if (args.npos > 0 && strcmp(args.pos[0], "benchmark") == 0) {
        struct jc_confbench_facts facts;
        struct jc_confbench_report rep;
        struct jc_sb sb;
        /* Load the assets the facts depend on. */
        app.memory = jc_memory_load(&app);
        jc_app_constraints_load(&app);
        jc_skill_load(&app.skills, app.cwd, app.arena);
        jc_agentdef_load(&app.agents, app.cwd, app.arena);
        jc_command_load(&app.commands, app.cwd, app.arena);
        jc_app_confbench_facts(&app, &facts);
        jc_confbench_score(&facts, &rep);
        jc_sb_init(&sb);
        jc_confbench_render(&rep, 0, 0, &sb);
        printf("%s", sb.data != NULL ? sb.data : "");
        jc_sb_free(&sb);
        jc_skill_set_free(&app.skills);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return 0;
    }

    /* `constraints [scan <file|->]` subcommand (M327): list the persisted store,
     * or predict what a brief/prompt would get adopted. Offline either way. */
    if (args.npos > 0 && strcmp(args.pos[0], "constraints") == 0) {
        int sub_code;
        if (args.npos > 1 && strcmp(args.pos[1], "scan") == 0) {
            sub_code = run_constraints_scan(arena,
                args.npos > 2 ? args.pos[2] : "-");
        } else if (args.npos > 1) {
            fprintf(stderr, "constraints: unknown argument '%s' (use "
                    "`constraints` or `constraints scan <file|->`)\n",
                    args.pos[1]);
            sub_code = 2;
        } else {
            sub_code = run_constraints_list(arena, app.cwd);
        }
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `glossary` subcommand: print the loaded domain-term glossary. */
    /* `brief-check <file|->`: a PRE-FLIGHT that spends no tokens (M433).
     *
     * Measured across 7 driven runs: 4 SILENTLY ADOPTED a constraint from
     * DESCRIPTIVE prose -- "force never-compiled core code to compile", "47 of 88
     * files never compiled". Knowing about the footgun did not prevent it; the
     * warning was written into a plan and tripped in the next brief authored. Two
     * other runs spent ~3M tokens against gates that could not pass.
     *
     * Every part already existed -- jc_constraint_scan and M343's baseline probe --
     * and the only thing missing was a way to run them WITHOUT spending a run. That
     * is the "prefer a lint to an audit" rule applied to the brief itself.
     *
     * Exit 1 only on a DECLARED-kind contradiction (a `goal` gate that is already
     * green forces nothing), so a wrapper can gate on it; findings alone exit 0,
     * because an inferred constraint may well be intended. */
    if (args.npos > 0 && strcmp(args.pos[0], "brief-check") == 0) {
        char *brief = NULL;
        jc_size blen = 0;
        struct jc_constraint cs[JC_CONSTRAINT_MAX];
        int ncs = 0;
        int i;
        int code = 0;

        if (args.npos < 2) {
            fprintf(stderr, "usage: jichi brief-check <file|-> "
                            "[--verify CMD] [--verify-kind invariant|goal]\n");
            jc_config_free(&app.config);
            jc_arena_free(arena);
            return 2;
        }
        if (strcmp(args.pos[1], "-") == 0) {
            struct jc_sb sb;
            char chunk[4096];
            size_t got;
            jc_sb_init(&sb);
            while ((got = fread(chunk, 1, sizeof chunk, stdin)) > 0) {
                jc_sb_append_n(&sb, chunk, (jc_size)got);
            }
            brief = jc_arena_strdup(arena, sb.data != NULL ? sb.data : "");
            blen = sb.len;
            jc_sb_free(&sb);
        } else if (jc_read_file(args.pos[1], &brief, &blen, arena) != JC_OK) {
            fprintf(stderr, "error: cannot read brief '%s'\n", args.pos[1]);
            jc_config_free(&app.config);
            jc_arena_free(arena);
            return 2;
        }
        printf("== brief-check: %s (%lu bytes) ==\n",
               args.pos[1], (unsigned long)blen);

        /* 1. What a run WOULD infer, and from which line. */
        ncs = jc_constraint_scan(brief, cs, JC_CONSTRAINT_MAX, arena);
        if (ncs == 0) {
            printf("\nconstraints inferred: none\n");
        } else {
            printf("\nconstraints inferred: %d -- these will be ENFORCED for the "
                   "run\n", ncs);
            for (i = 0; i < ncs; i++) {
                int ln = jc_constraint_source_line(brief, &cs[i], arena);
                printf("  %d. %s\n", i + 1,
                       cs[i].text != NULL ? cs[i].text : "(unnamed)");
                if (ln > 0) {
                    printf("     from line %d\n", ln);
                } else {
                    printf("     (no single line reproduces it; the scan matched "
                           "across the message)\n");
                }
            }
            printf("\n  If any of these is NOT intended, reword the line: the "
                   "phrases that\n  trigger inference are DESCRIPTIONS, not "
                   "instructions, so keep negation\n  words out of the same clause "
                   "as a build or tool verb.\n");
        }

        /* 2. The envelope this brief would run under, as given. */
        printf("\nenvelope, as the flags declare it:\n");
        printf("  token budget   : %s\n",
               args.budget_tokens != NULL ? args.budget_tokens : "(unset)");
        printf("  deadline       : %s\n",
               args.deadline != NULL ? args.deadline : "(unset)");
        printf("  max tool calls : %d\n", args.max_tool_calls);
        printf("  max reads      : %d\n", args.max_reads);
        printf("  edit scope     : %d glob(s)%s\n", args.n_edit_scope,
               args.n_edit_scope == 0 ? " -- writes are NOT fenced" : "");
        printf("  verifier       : %s\n",
               args.verify_cmd != NULL ? args.verify_cmd
                                       : (app.config.verify != NULL
                                          ? app.config.verify : "(unset)"));
        printf("  verify kind    : %s\n",
               args.verify_kind != NULL ? args.verify_kind : "(undeclared)");

        /* 3. The gate's baseline colour -- M343's probe, no model call. */
        {
            const char *vc = (args.verify_cmd != NULL) ? args.verify_cmd
                                                       : app.config.verify;
            if (vc != NULL && vc[0] != '\0') {
                struct jc_sb vout;
                int vexit;
                int kind = JC_VERIFY_KIND_UNSET;
                enum jc_env_baseline_verdict bv;

                if (args.verify_kind != NULL) {
                    (void)jc_env_verify_kind_parse(args.verify_kind, &kind);
                }
                printf("\nrunning the gate once, before any work "
                       "(no model call)...\n");
                jc_sb_init(&vout);
                vexit = jc_env_run_verify(vc, app.cwd, &vout, &app.abort_flag, 0);
                jc_sb_free(&vout);
                bv = jc_env_baseline_check(kind, vexit);
                printf("  baseline exit  : %d (%s)\n", vexit,
                       vexit == 0 ? "GREEN" : "RED");
                switch (bv) {
                case JC_BASELINE_FORCES_NOTHING:
                    printf("  VERDICT        : this gate FORCES NOTHING. You "
                           "declared it a `goal`\n"
                           "                   gate, and it already passes -- so "
                           "it can pass without\n"
                           "                   the work. Fix the gate or the "
                           "declaration before\n"
                           "                   spending a run.\n");
                    code = 1;
                    break;
                case JC_BASELINE_EXPECTED_RED:
                    printf("  VERDICT        : red before the work, which is "
                           "correct for a `goal`\n                   gate.\n");
                    break;
                case JC_BASELINE_NOT_KNOWN_GOOD:
                    printf("  VERDICT        : the tree does not start green. An "
                           "invariant gate cannot\n                   tell your "
                           "breakage from the existing kind -- fix it first, or\n"
                           "                   declare `--verify-kind goal` if red "
                           "is the intended start.\n");
                    break;
                case JC_BASELINE_OK:
                    printf("  VERDICT        : green before the work, as an "
                           "invariant gate should be.\n");
                    break;
                }
                printf("\n  What this canNOT prove: that a goal gate is "
                       "SATISFIABLE -- only that it\n  is red without the work. "
                       "Proving it can go green needs a stub or a\n  hand-completed "
                       "fixture; see docs/TEST_INTEGRITY.md.\n");
            } else {
                printf("\nno verifier: nothing gates this run, so a green result "
                       "means only\n  that the model stopped.\n");
            }
        }
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return code;
    }

    if (args.npos > 0 && strcmp(args.pos[0], "glossary") == 0) {
        char *g = jc_glossary_load(&app);
        if (g != NULL && g[0] != '\0') {
            printf("%s\n", g);
        } else {
            printf("(no glossary; add %s/.jichi/glossary.md or "
                   "~/.config/jichi/glossary.md)\n", app.cwd);
        }
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return 0;
    }

    /* `telemetry [path]` subcommand: summarize a telemetry JSONL log (offline). */
    if (args.npos > 0 && strcmp(args.pos[0], "telemetry") == 0) {
        int sub_code = run_telemetry(arena,
            args.npos > 1 ? args.pos[1] : NULL, args.telemetry_workspace,
            args.cache_audit, args.since);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `audit [path]` subcommand (M158): summarize the privileged-command
     * audit log (offline; `--since <dur>` limits the window). */
    if (args.npos > 0 && strcmp(args.pos[0], "audit") == 0) {
        int sub_code = run_audit(arena,
            args.npos > 1 ? args.pos[1] : NULL, args.since,
            args.output_json == 1);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `runs [dir]` subcommand (M158): one triage row per autonomy-envelope
     * run journal (offline; newest first, capped unless --all). */
    if (args.npos > 0 && strcmp(args.pos[0], "runs") == 0) {
        int sub_code = run_runs(arena,
            args.npos > 1 ? args.pos[1] : NULL, args.all, args.since,
            args.output_json == 1);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `learn [analyze|apply|corrections]` subcommand (M70): the offline halves of
     * the learning loop. `analyze` mines telemetry + recent sessions for recurring
     * problems; `apply` commits a (human-edited) lessons draft to memory/skills;
     * `corrections` (M294) commits only the draft's `## Corrections` section.
     * The mentor "review" step is the scaffolded `/learn` command (needs a
     * model): run it in the TUI or `jichi -p "/learn"`. */
    if (args.npos > 0 && strcmp(args.pos[0], "learn") == 0) {
        const char *sub = (args.npos > 1) ? args.pos[1] : "analyze";
        int sub_code;
        if (strcmp(sub, "analyze") == 0) {
            sub_code = run_learn_analyze(arena,
                args.npos > 2 ? args.pos[2] : NULL, args.telemetry_workspace);
        } else if (strcmp(sub, "apply") == 0) {
            sub_code = run_learn_apply(&app, JC_LEARN_ALL, args.force);
        } else if (strcmp(sub, "corrections") == 0) {
            /* M294: apply ONLY the `## Corrections` section. Two shipped
             * warnings told users to run this for years before it existed
             * (M292 retired the wrong advice; this makes the operation real).
             * It is the right command for the situation that prints them:
             * memory.md has outgrown the 8 KB injection budget, so what is
             * needed is to RETRACT stale notes, not to add more. `--force`
             * only affects skills, so it is not accepted here. */
            sub_code = run_learn_apply(&app, JC_LEARN_CORRECTIONS, 0);
        } else if (strcmp(sub, "review") == 0) {
            fprintf(stderr, "learn review: run the mentor with the scaffolded "
                    "command -- in the TUI type /learn, or headless: "
                    "jichi -p \"/learn\"  (needs `init` to have written "
                    "the 'learn' command + 'mentor' agent), then `learn apply` "
                    "the reviewed draft.\n");
            sub_code = 2;
        } else {
            fprintf(stderr, "usage: learn [analyze [path] | apply [--force] "
                    "| corrections]\n");
            sub_code = 2;
        }
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `complete [text]` subcommand: one-shot autocomplete (autocomplete-role
     * model), prints the continuation. Needs config + network. */
    if (args.npos > 0 && strcmp(args.pos[0], "complete") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        jc_http_global_init();
        sub_code = run_complete(&app, &args);
        jc_http_global_cleanup();
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `fim` subcommand: one-shot fill-in-the-middle completion. */
    if (args.npos > 0 && strcmp(args.pos[0], "fim") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        jc_http_global_init();
        sub_code = run_fim(&app, &args);
        jc_http_global_cleanup();
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `doctor` subcommand: setup health checks. */
    if (args.npos > 0 && strcmp(args.pos[0], "doctor") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        jc_http_global_init();
        sub_code = run_doctor(&app, args.output_json == 1, args.unattended,
                              args.live);
        jc_http_global_cleanup();
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `models` subcommand: list configured models + probe reachability. */
    if (args.npos > 0 && strcmp(args.pos[0], "models") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        jc_http_global_init();
        sub_code = run_models(&app);
        jc_http_global_cleanup();
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `docs` subcommand (M34a): list/index/search external documentation
     * sources. index/search need network (embeddings); list is offline. */
    if (args.npos > 0 && strcmp(args.pos[0], "docs") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        jc_http_global_init();
        sub_code = run_docs(&app, &args);
        jc_http_global_cleanup();
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `timeouts` subcommand (M23c): show the resolved model-call timeouts for
     * the active model (offline; no provider or network). */
    if (args.npos > 0 && strcmp(args.pos[0], "timeouts") == 0) {
        int sub_code = run_timeouts(&app);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `undo [N]` / `checkpoints` subcommands: workspace snapshot ops, then exit
     * (no provider or network). */
    if (args.npos > 0 && (strcmp(args.pos[0], "undo") == 0 ||
                          strcmp(args.pos[0], "attempts") == 0 ||
                          strcmp(args.pos[0], "recover") == 0 ||
                          strcmp(args.pos[0], "checkpoints") == 0)) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        /* M337: read-only retrieval of what M336 preserved; both go through the
         * snapshot manager but neither touches the live tree. */
        if (strcmp(args.pos[0], "attempts") == 0) {
            sub_code = run_attempts(&app, &args);
        } else if (strcmp(args.pos[0], "recover") == 0) {
            sub_code = run_recover(&app, &args);
        } else {
            sub_code = run_snapshot(&app, &args);
        }
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `rewind [N] [--dry-run]`: restore files AND truncate the saved session's
     * history to that turn, then exit (no provider or network). */
    if (args.npos > 0 && strcmp(args.pos[0], "rewind") == 0) {
        int sub_code;
        g_app_for_signal = &app;
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);
        sub_code = run_rewind(&app, &args);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `skills` subcommand: list agent skills, then exit (no provider/network). */
    if (args.npos > 0 && strcmp(args.pos[0], "skills") == 0) {
        int sub_code = run_skills(&app, &args);
        jc_skill_set_free(&app.skills);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    /* `agents` / `commands` / `rules` / `sysmsg`: read-only asset introspection
     * (no provider or network; the assets are loaded on demand). */
    if (args.npos > 0 && strcmp(args.pos[0], "agents") == 0) {
        int sub_code = run_agents(&app);
        jc_agentdef_set_free(&app.agents);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "commands") == 0) {
        int sub_code = run_commands(&app);
        jc_command_set_free(&app.commands);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "assignments") == 0) {
        int sub_code = run_assignments(&app, args.output_json == 1);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "board") == 0) {
        int sub_code = run_board(&app, &args);
        jc_board_free(&app.board);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "output-styles") == 0) {
        int sub_code = run_output_styles(&app);
        jc_output_style_set_free(&app.output_styles);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "rules") == 0) {
        int sub_code = run_rules(&app);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "sysmsg") == 0) {
        int sub_code = run_sysmsg(&app, args.design_path, args.n_design_path, &args, arena);
        jc_skill_set_free(&app.skills);
        jc_output_style_set_free(&app.output_styles);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }
    if (args.npos > 0 && strcmp(args.pos[0], "context") == 0) {
        int sub_code = run_context(&app, args.design_path, args.n_design_path,
                                   args.npos > 1 &&
                                   strcmp(args.pos[1], "tools") == 0,
                                   args.npos > 1 &&
                                   strcmp(args.pos[1], "history") == 0,
                                   args.npos > 2 ? args.pos[2]
                                                 : args.session_id,
                                   args.all, &args, arena);
        jc_skill_set_free(&app.skills);
        jc_output_style_set_free(&app.output_styles);
        jc_config_free(&app.config);
        jc_arena_free(arena);
        return sub_code;
    }

    jc_tool_registry_init(&tools);
    jc_tool_register_builtins(&tools);
    app.tools = &tools;

    app.provider = jc_provider_create(&app.config.model);
    if (app.provider == NULL) {
        fprintf(stderr, "error: could not initialise provider\n");
        jc_tool_registry_free(&tools);
        jc_arena_free(arena);
        return 1;
    }

    if (app.config.model.api_key == NULL && !args.quiet) {
        /* M378: give the project's own advice. The model knows which env var
         * it reads (apiKeyEnv), so name it; and never recommend a literal
         * apiKey -- the config shape doctor's M55 lint warns against. */
        if (app.config.model.api_key_env != NULL) {
            fprintf(stderr,
                    "warning: no API key found: $%s (this model's apiKeyEnv) "
                    "is not set in the environment.\n",
                    app.config.model.api_key_env);
        } else {
            fprintf(stderr,
                    "warning: no API key found (set the model's apiKeyEnv "
                    "and export that variable, or set ANTHROPIC_API_KEY / "
                    "OPENAI_API_KEY; see docs/MODELS.md).\n");
        }
    }

    /* The secret registry (redaction + child-env scrub) is armed right after the
     * config loads, above -- M608 moved it up from here. See that block. */

    jc_http_global_init();
    g_app_for_signal = &app;
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    /* If the active model declares a fallback and its server is unreachable,
     * switch to the first reachable model in its chain. (No-op when the active
     * model has no fallback — e.g. a reliable remote default.) */
    {
        int eff = jc_app_effective_model(&app, app.config.active);
        if (eff != app.config.active) {
            jc_app_switch_model(&app, eff);
        }
    }

    /* Connect any configured MCP servers and register their tools. Failures
     * are logged and skipped; the agent runs with whatever connected. */
    jc_mcp_manager_init(&mcp, &app);
    app.mcp = &mcp;
    {
        int ntools = jc_mcp_connect_all(&mcp, &app.config, &tools);
        if (ntools > 0 && !args.quiet) {
            fprintf(stderr, "[mcp] registered %d tool(s) from %d server(s)\n",
                    ntools, jc_mcp_server_count(&mcp));
        }
        /* M43: expose MCP resources to the agent when any server has them. */
        if (jc_mcp_resource_count(&mcp) > 0) {
            jc_tool_registry_register(&tools, jc_tool_read_mcp_resource(&app));
            if (!args.quiet) {
                fprintf(stderr, "[mcp] %d resource(s) available "
                        "(read_mcp_resource)\n", jc_mcp_resource_count(&mcp));
            }
        }
    }

    /* User-defined tools from config "tools" (registered as dynamic tools). */
    jc_user_tools_init(&umgr);
    {
        int nut = jc_user_tools_register(&umgr, &app.config, &tools);
        if (nut > 0 && !args.quiet) {
            fprintf(stderr, "[tools] registered %d user tool(s)\n", nut);
        }
    }

    /* Background-process registry (M26): run_terminal_command can detach a
     * process; read_background_output / kill_background manage it. */
    jc_bg_mgr_init(&bgmgr);
    app.bg = &bgmgr;
    /* The two tools themselves are registered by jc_tool_register_configured
     * below (M325b), shared with the reporting surfaces. */

    /* M325b: the conditional built-ins are registered by
     * jc_tool_register_configured, called ONCE further down -- after
     * jc_skill_load, because load_skill's gate is "some skills exist". Loading
     * .jichi/board.json stays here: registering the board tool is the registrar's
     * job, loading the file is main's. */
    if (app.config.board) {
        jc_board_load(&app.board, app.cwd);
    }

    /* SessionStart hooks (M25): fire once now; any context the hook prints is
     * appended to the system prompt (via system_prompt_extra) for the session. */
    if (jc_hooks_active(&app)) {
        struct jc_hook_result hr;
        jc_hook_result_init(&hr);
        jc_hooks_fire(&app, JC_HOOK_SESSION_START, NULL, NULL, NULL, 0, NULL,
                      &hr);
        if (hr.context.len > 0) {
            struct jc_sb se;
            jc_sb_init(&se);
            if (app.config.system_prompt_extra != NULL) {
                jc_sb_append(&se, app.config.system_prompt_extra);
                jc_sb_append(&se, "\n\n");
            }
            jc_sb_append(&se, hr.context.data);
            app.config.system_prompt_extra =
                jc_arena_strdup(arena, se.data != NULL ? se.data : "");
            jc_sb_free(&se);
        }
        jc_hook_result_free(&hr);
    }

    /* LSP servers are spawned lazily on first use (after an edit). */
    jc_lsp_manager_init(&lsp, &app);
    app.lsp = &lsp;

    /* Workspace snapshots: shadow git repo for /undo (no-op if git absent). */
    jc_snapshot_manager_init(&snaps, &app);
    app.snapshots = &snaps;

    /* Autonomy envelope: budgets + a verifier gate + rollback-to-green + an
     * audit journal. Active when any envelope flag is given, or the config
     * supplies a verifier / edit-scope. */
    if (envelope_is_armed(&args, &app)) {
        char run_id[40];
        char jpath[1200];
        int headless = (args.print_prompt != NULL) || !isatty(STDIN_FILENO);

        jc_uuid_v4(run_id);
        if (args.journal_path != NULL && strcmp(args.journal_path, "-") == 0) {
            jpath[0] = '\0'; /* journaling disabled */
        } else if (args.journal_path != NULL) {
            jc_snprintf(jpath, sizeof(jpath), "%s", args.journal_path);
        } else {
            char dir[1100];
            jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/runs", jc_home_dir());
            jc_mkdir_p(dir);
            jc_snprintf(jpath, sizeof(jpath), "%s/%s.jsonl", dir, run_id);
        }
        /* M444: the arming moved into envelope_arm so `sysmsg`/`context` can do the
         * same thing without a journal. The journal decision stays HERE, because a
         * run is the only thing entitled to create one. */
        envelope_arm(&args, &app, &env, arena, run_id,
                     jpath[0] != '\0' ? jpath : NULL);
        app.env = &env;
        env_active_run = 1;

        /* An unsupervised, bounded run needs to act without prompts; promote to
         * AUTO in headless mode unless the user pinned a mode explicitly. */
        if (headless && !args.plan && !args.auto_approve &&
            app.mode != JC_MODE_AUTO) {
            jc_app_set_mode(&app, JC_MODE_AUTO);
            app.mode_pinned = 1;
            if (!args.quiet) {
                fprintf(stderr, "[envelope] auto-approving tools for this "
                                "bounded unsupervised run\n");
            }
        }
    }

    /* M159: open the mid-run control socket (--control / config `control`).
     * After the envelope block so `status` snapshots see the budgets. Off by
     * default; the resolved path is printed so a supervisor can find it. */
    if (args.control_flag || app.config.control_on) {
        char cpath[1024];
        const char *want = (args.control_path != NULL)
                               ? args.control_path
                               : app.config.control_path;
        if (want != NULL && want[0] != '\0') {
            jc_snprintf(cpath, sizeof(cpath), "%s", want);
        } else {
            jc_control_default_path(cpath, sizeof(cpath));
        }
        if (jc_control_open(&ctl, cpath) == JC_OK) {
            app.control = &ctl;
            control_open = 1;
            if (!args.quiet) {
                fprintf(stderr, "[control] listening on %s\n", ctl.path);
            }
        } else {
            fprintf(stderr, "warning: could not open control socket at %s "
                            "(continuing without)\n", cpath);
        }
    }

    /* Discover project/global instruction files (AGENTS.md etc.) and named
     * agent profiles. */
    app.rules = jc_rules_load(&app);
    app.memory = jc_memory_load(&app);
    jc_app_constraints_load(&app); /* M110: active enforced constraints */
    app.glossary = jc_glossary_load(&app);
    jc_app_load_design(&app, args.design_path, args.n_design_path); /* M-C/M462 */
    app.repo_map = app.config.repo_map ? jc_repomap_build(&app) : NULL;
    jc_agentdef_load(&app.agents, app.cwd, arena);
    jc_command_load(&app.commands, app.cwd, arena);

    /* Agent skills: advertise the load_skill tool only when some exist. */
    jc_skill_load(&app.skills, app.cwd, arena);
    /* load_skill is registered by jc_tool_register_configured (M325b), which is
     * called AFTER this so its "some skills exist" gate can see them. */

    /* Custom output styles (M28): load and activate the configured/CLI one
     * (config.output_style already folds in --output-style). */
    jc_output_style_load(&app.output_styles, app.cwd, arena);
    if (app.config.output_style != NULL &&
        !jc_output_style_set_active(&app.output_styles,
                                    app.config.output_style) && !args.quiet) {
        fprintf(stderr, "warning: no output style named '%s'\n",
                app.config.output_style);
    }

    /* M325b: the LSP, format_file and git registrations moved into
     * jc_tool_register_configured, shared with `context tools` and `doctor`. They
     * were duplicated here for one build of this milestone -- a bare
     * jc_tool_registry_register is a vec push with no duplicate check, so ten
     * tools were advertised twice until a request capture showed it. */
    jc_tool_register_configured(&tools, &app);

    /* M21: optional event-logging / telemetry sink. Off by default; resolved
     * from config `logging` + the --log/--log-level flags (CLI overrides config).
     * Written OUTSIDE the workspace (under ~/.jichi.d/telemetry) so a
     * snapshot rollback can't revert it (the ANECDOTES blast-radius rule). */
    jc_eventlog_disable(&telemetry);
    {
        int level = app.config.log_level;
        const char *path = app.config.log_path;
        int disabled = 0;
        if (args.log_level != NULL) {
            level = jc_eventlog_level_parse(args.log_level); /* validated above */
        }
        if (args.log_path != NULL) {
            if (strcmp(args.log_path, "-") == 0) {
                disabled = 1;
            } else {
                path = args.log_path;
                if (level <= JC_EVENTLOG_OFF) {
                    level = JC_EVENTLOG_METRICS; /* --log <path> implies metrics */
                }
            }
        }
        if (!disabled && level > JC_EVENTLOG_OFF) {
            char tpath[1200];
            char tid[40];
            jc_uuid_v4(tid);
            if (path != NULL && path[0] != '\0') {
                jc_snprintf(tpath, sizeof(tpath), "%s", path);
            } else {
                /* M599: one appended log per workspace, named for it, not one
                 * <run-id>.jsonl per run -- `learn analyze` reads ONE log, so a
                 * per-run file gave the learner a one-run memory. The same
                 * derivation the readers use (jc_app_pick_telemetry_log). */
                jc_telemetry_default_path(jc_home_dir(),
                                          app.root[0] != '\0' ? app.root
                                                               : app.cwd,
                                          tpath, sizeof(tpath));
            }
            if (jc_eventlog_open(&telemetry, tpath, tid, level) == JC_OK) {
                app.telemetry = &telemetry;
                telem_active = 1;
                /* Stamp the workspace so the summarizer can filter by project
                 * (M56): the shared telemetry dir mixes every project's runs. */
                jc_eventlog_set_workspace(&telemetry,
                    app.root[0] != '\0' ? app.root : app.cwd);
                jc_logf(JC_LOG_INFO, "[telemetry] %s -> %s",
                        jc_eventlog_level_name(level), tpath);
            } else {
                jc_logf(JC_LOG_WARN, "telemetry: could not open %s", tpath);
            }
        }
    }

    /* M20a: a per-turn scratch arena for transient allocations (system message,
     * command/@-ref expansion), reset at each top-level turn so long interactive
     * sessions don't accumulate that scratch on the session arena. Created only
     * for the run loop; the subcommand paths above never touch it (jc_app_scratch
     * falls back to app->arena when this is NULL). */
    app.scratch = jc_arena_new(0);
    /* M199: per-tool-call transients, reset before every tool call so one
     * long turn's file reads cannot accumulate. See jc_app_tool_scratch. */
    app.tool_scratch = jc_arena_new(0);

    {
        int stdin_tty = isatty(STDIN_FILENO);
        const char *prompt = args.print_prompt;
        int serve_acp = args.acp ||
                        (args.npos > 0 && strcmp(args.pos[0], "serve") == 0);
        int daemon_mode = args.npos > 0 &&
                          strcmp(args.pos[0], "daemon") == 0;

        int improve_live = args.npos > 0 &&
                           strcmp(args.pos[0], "improve") == 0 &&
                           args.improve_attempt;
        int attempt_mode = args.npos > 0 &&
                           strcmp(args.pos[0], "attempt") == 0;
        int wf_mode = args.npos > 0 && strcmp(args.pos[0], "workflow") == 0;

        /* M431e: take the per-workspace run lease HERE, immediately before the work
         * begins -- not where the envelope arms. The envelope is configured well
         * before dispatch, and several paths between the two return early (a missing
         * config, a usage error, an introspection subcommand). Acquiring up there
         * leaked a lease file on every one of them: harmless, because a holder whose
         * pid is gone is taken quietly, but it littered ~/.jichi.d/leases and would
         * have had `doctor` reporting stale holders for runs that never ran. Measured
         * by hand before any test existed. */
        if (app.env != NULL) {
            enum jc_lease_mode lmode = JC_LEASE_WARN;
            struct jc_lease_info me;
            struct jc_lease_info holder;
            enum jc_lease_verdict lv;

            if (args.lease != NULL && !jc_lease_mode_parse(args.lease, &lmode)) {
                fprintf(stderr, "error: --lease expects warn|fail|off (got %s)\n",
                        args.lease);
                return 2;
            }
            memset(&me, 0, sizeof(me));
            jc_snprintf(me.run, sizeof(me.run), "%s",
                        env.run_id != NULL ? env.run_id : "");
            me.pid = (long)getpid();
            me.started = (long)time(NULL);
            jc_snprintf(me.mode, sizeof(me.mode), "%s",
                        jc_agent_mode_name((enum jc_agent_mode)app.mode));
            lv = jc_lease_acquire(jc_home_dir(), app.root, &me, lmode, &holder);
            if (lv == JC_LEASE_REFUSE) {
                fprintf(stderr,
                    "error: this workspace is already held by a live jichi run "
                    "(pid %ld, run %s, mode %s) and --lease fail was given.\n"
                    "Wait for it to finish, or re-run with --lease warn to proceed "
                    "anyway -- see docs/AUTONOMY.md on why concurrent runs on one "
                    "tree are unsafe.\n",
                    holder.pid, holder.run[0] ? holder.run : "?",
                    holder.mode[0] ? holder.mode : "?");
                return 2;
            }
            if (lv == JC_LEASE_WARN_TAKE) {
                jc_logf(JC_LOG_WARN,
                    "lease: this workspace is ALREADY held by a live run (pid %ld, "
                    "run %s). Concurrent runs on one tree are unsafe: the "
                    "out-of-scope guard cannot tell your edits from theirs, and "
                    "revertOutOfScope may revert work neither run made. Serialise "
                    "with --lease fail, or give each run its own tree.",
                    holder.pid, holder.run[0] ? holder.run : "?");
            }
            lease_held = (lmode != JC_LEASE_OFF);
            lease_mode_used = lmode;
        }

        if (wf_mode) {
            /* Deterministic multi-agent pipeline (M101), warm app + subagents. */
            exit_code = run_workflow(&app, args.npos > 1 ? args.pos[1] : NULL);
        } else if (attempt_mode) {
            /* Tiered-learner rehearsal of ONE unified assignment (B6); the
             * envelope flags (budget/deadline/max-tool-calls) bound it. */
            exit_code = run_assignment_attempt(&app, &args);
        } else if (improve_live) {
            /* Live rehearsal in isolated worktrees (M109), using the warm app +
             * a model. */
            exit_code = run_improve_attempt(&app,
                                            args.npos > 1 ? args.pos[1] : NULL);
        } else if (daemon_mode) {
            /* Warm process: keep this fully-configured app hot and serve
             * requests over a Unix socket (M100). */
            exit_code = run_daemon(&app, &args);
        } else if (serve_acp) {
            /* ACP server: drive jichi as an agent server over stdin/stdout for an
             * editor. Reuses the fully-configured app (provider/tools/MCP/...).
             * The `--acp` flag is accepted explicitly; `serve` implies it. */
            exit_code = jc_acp_serve(&app);
        } else if (prompt != NULL && strcmp(prompt, "-") == 0) {
            /* Explicit: the prompt IS stdin (reads even with --no-stdin). */
            prompt = read_all_stdin(arena);
            exit_code = run_headless(&app, prompt, args.output_json);
        } else if (prompt != NULL) {
            /* M375: a positional prompt may span several argv words
             * (unquoted, or after `--`, whose --help line has always
             * promised "all following args as the prompt"); print_prompt
             * held only pos[0], so join the rest back in. */
            if (args.npos > 1 && args.pos[0] == args.print_prompt) {
                struct jc_sb sb;
                int pi;
                jc_sb_init(&sb);
                for (pi = 0; pi < args.npos; pi++) {
                    if (pi > 0) {
                        jc_sb_append(&sb, " ");
                    }
                    jc_sb_append(&sb, args.pos[pi]);
                }
                prompt = jc_arena_strdup(arena, sb.data);
                jc_sb_free(&sb);
            }
            /* A prompt was given; append piped stdin as context if any. */
            if (!args.no_stdin && !stdin_tty) {
                char *extra = read_all_stdin(arena);
                if (extra != NULL && extra[0] != '\0') {
                    struct jc_sb sb;
                    jc_sb_init(&sb);
                    jc_sb_append(&sb, prompt);
                    jc_sb_append(&sb, "\n\n");
                    jc_sb_append(&sb, extra);
                    prompt = jc_arena_strdup(arena, sb.data);
                    jc_sb_free(&sb);
                }
            }
            exit_code = run_headless(&app, prompt, args.output_json);
        } else if (!stdin_tty) {
            /* No prompt, piped stdin: all of stdin is the prompt (headless). */
            prompt = read_all_stdin(arena);
            if (prompt == NULL || prompt[0] == '\0') {
                if (!args.quiet) {
                    fprintf(stderr, "error: no prompt (empty stdin)\n");
                }
                exit_code = 2;
            } else {
                exit_code = run_headless(&app, prompt, args.output_json);
            }
        } else {
            /* No prompt, interactive terminal: the TUI. */
            exit_code = jc_tui_run(&app);
        }
    }

    /* The agent loop returns JC_OK even when the verifier failed (the turn
     * completed); the envelope's outcome carries the verdict. Surface it. */
    if (env_active_run) {
        enum jc_env_outcome oc = app.env->outcome;
        char tk[40];
        jc_group_num(app.env->tokens_used,
                     jc_group_sep_audience(app.config.group_sep,
                                           app.config.accessible),
                     tk, sizeof tk);
        if (oc == JC_ENV_VERIFY_FAILED || oc == JC_ENV_BUDGET_EXHAUSTED
                || oc == JC_ENV_SCOPE_TAINTED) {
            if (!args.quiet) {
                fprintf(stderr, "[envelope] %s (tokens %s, tool calls %d)\n",
                        jc_env_outcome_name(oc), tk, app.env->tool_calls);
            }
            if (exit_code == 0) {
                exit_code = 1; /* preserve 130 (interrupt) / 2 (usage) */
            }
        } else if (oc == JC_ENV_OK && !args.quiet) {
            fprintf(stderr, "[envelope] verified ok (tokens %s, "
                    "tool calls %d)\n", tk, app.env->tool_calls);
        }
    }

    if (telem_active) {
        jc_eventlog_close(&telemetry);
        app.telemetry = NULL;
    }
    /* M431e: drop the lease. jc_lease_release re-reads the file and unlinks ONLY
     * when the run id matches, so a run that warned past someone else's lease
     * cannot delete it here. */
    if (lease_held && lease_mode_used != JC_LEASE_OFF) {
        jc_lease_release(jc_home_dir(), app.root, env.run_id);
    }
    jc_calib_save(&app.calib);   /* M77: persist any newly-learned ratios */
    jc_calib_free(&app.calib);
    app.provider->vt->free(app.provider);
    jc_tool_registry_free(&tools);
    jc_mcp_manager_shutdown(&mcp); /* closes servers, frees dynamic tools */
    jc_user_tools_free(&umgr);     /* frees the user-tool wrappers */
    /* M339: this session's spilled tool output goes, plus an age sweep for the
     * directories crashed sessions leave behind -- the eviction is part of the
     * feature, because M338 had to go back and add it for preserved states. */
    jc_toolout_cleanup(&app);
    jc_bg_mgr_free(&bgmgr);        /* SIGTERM/KILL + reap background procs */
    jc_lsp_manager_shutdown(&lsp);
    jc_snapshot_manager_shutdown(&snaps);
    jc_board_free(&app.board);
    jc_agentdef_set_free(&app.agents);
    jc_command_set_free(&app.commands);
    jc_skill_set_free(&app.skills);
    jc_output_style_set_free(&app.output_styles);
    if (control_open) {
        jc_control_close(&ctl); /* close + unlink the socket */
    }
    if (env_active_run) {
        jc_env_free(&env);
    }
    jc_index_free(app.index);
    jc_config_free(&app.config);
    jc_http_global_cleanup();
    jc_arena_free(app.scratch); /* M20a: per-turn scratch (NULL-safe) */
    jc_arena_free(app.tool_scratch); /* M199: per-tool-call (NULL-safe) */
    free(app.memory);        /* M199: malloc-owned since the notes reload */
    jc_arena_free(arena);
    return exit_code;
}
