#!/bin/sh
# smoke: no GNU-only utility flags in code that runs on a target (M461).
#
# THE DEFECT THIS EXISTS FOR. The OpenBSD row found `search_code` -- one of the
# agent's most-used tools -- completely broken, because it shells out
# `grep -rnI --color=never` and BSD grep has no --color. It exited 2, the
# tool's own `2>/dev/null` hid the message, and every search on that platform
# returned "(no matches)". A model reads that as "the code does not contain
# this", so the tool did not fail: it LIED, silently, on every call.
#
# The same sweep found 139 uses of `head -c` in this tier. OpenBSD's head has
# only -n, which is all POSIX requires. 137 of the 139 sat inside t_fail
# messages, so on that platform a failing driver would print head's usage
# string INSTEAD of the diagnostic explaining the failure -- the instrument
# breaking precisely when it is needed.
#
# WHY A LINT AND NOT AN AUDIT. Both defects were invisible to five Linux libcs
# and to every reviewer who read the code, because the flags are correct on
# every machine this project is developed on. An audit finds what it knows to
# look for; this lint fails the build the next time one is typed, without
# anybody having to own a BSD.
#
# SCOPE, stated so it is not mistaken for more: shell sources only (this tier,
# scripts/, and the shell fragments in src/ string literals are NOT parsed).
# It bans a specific, checked list of flags -- not "all non-POSIX usage".
. "$(dirname "$0")/_smoke.sh"

t_plan 28
tmp=$(smoke_tmp)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

# Each entry: PATTERN|WHY|USE-INSTEAD. Every one was verified against the
# actual usage string of OpenBSD 7.9's utility, not assumed from memory.
BANNED="head -c|OpenBSD head has only -n|head_bytes N (this tier) or head -n
grep -P|BSD grep has no PCRE mode|a POSIX ERE with grep -E
--color=|BSD grep rejects the flag entirely|GREP_OPTIONS= and no flag
sed -i |BSD sed -i REQUIRES a backup suffix|a temp file and mv
stat -c|BSD stat uses -f with different verbs|a portable probe
xargs -r|BSD xargs has no -r (and does not need it)|test the input first
sort -V|BSD sort has no version sort|sort -n on a split field"

# THE CORPUS, WIDENED AT M511. Until then this lint scanned tests/ and scripts/
# -- the code the project runs -- and not the code it asks OTHER PEOPLE to run:
# 79 shell scripts under docs/ (every graded assignment's test.sh, and the trace
# capturers) plus the ```sh blocks of the three learner corpora. That was 24 real
# defects, found by sweeping the lints' universes rather than their results:
#
# M514 added examples/ for the same reason and with no defects to fix: 13 scripts
# a reader is invited to run, including the self-hosting launcher. Naming the
# rule rather than the directories -- CODE WE ASK OTHER PEOPLE TO RUN -- is what
# stops the next population from being missed.
#
#   10x GNU BRE alternation in a plain grep -- `grep -qi 'stack\|fold\|reduce'`
#       in EIGHT capstone graders and two others. On a BSD that searches for the
#       literal string `stack|fold|reduce`, so a learner whose DESIGN.md says
#       "a fold" is FAILED by a grader that is itself wrong.
#   14x GNU `\b` word boundaries, including two safety traps
#       (`\b(sprintf|strcpy|strcat|gets)`, `\b(new|delete|malloc|free)\b`) which
#       on a BSD match nothing and therefore PASS a solution that does the
#       forbidden thing -- a false green on the check the task exists for.
#
# All 24 are fixed. The corpus below is what keeps the next one out, and the
# learner-facing half of it matters more than the tier's own: a learner on
# OpenBSD who is failed by a broken grader has no way to know it was the grader.
#
# Markdown PROSE is deliberately not scanned -- only fenced ```sh blocks. A page
# that explains why `\b` is unportable has to be able to write it down, which is
# the same reason comment lines are skipped throughout this lint.

# The learner-facing corpus, flattened once into file:line:text form so the
# checks below can grep ONE file and still print a usable location.
docs_sh="$tmp/docs_sh"
: > "$docs_sh"
find "$ROOT/docs" "$ROOT/examples" -name '*.sh' 2>/dev/null | while read -r f; do
    awk -v F="${f#$ROOT/}" '{ printf "%s:%d:%s\n", F, NR, $0 }' "$f" >> "$docs_sh"
done
for _d in docs/reading docs/curriculum docs/assignments; do
    find "$ROOT/$_d" -name '*.md' 2>/dev/null | while read -r f; do
        awk -v F="${f#$ROOT/}" '
            /^```sh$/ { inb = 1; next }
            /^```/    { inb = 0; next }
            inb       { printf "%s:%d:%s\n", F, NR, $0 }' "$f" >> "$docs_sh"
    done
done
_docs_lines=$(grep -c . "$docs_sh" 2>/dev/null || true)
[ -n "$_docs_lines" ] || _docs_lines=0

# ---- 1: the matcher itself works (the floor under the ground truth) ---------
# A lint nobody has watched catch anything is a lint that may be scanning zero
# files and reporting success. Plant one positive and require a hit.
mkdir -p "$tmp/self"
printf 'x=$(head -c 10 "$f")\n' > "$tmp/self/bad.sh"
printf 'x=$(head -n 10 "$f")\n' > "$tmp/self/good.sh"
_hits=$(grep -l -- "head -c" "$tmp/self"/*.sh 2>/dev/null | wc -l | tr -d '[:space:]')
if [ "$_hits" -eq 1 ]; then
    t_ok "the matcher flags a planted positive and spares the clean file"
else
    t_fail "matcher is broken: $_hits/1 files flagged -- every result below is meaningless"
fi

# ---- 2: the corpus is non-empty (a second floor) ---------------------------
_files=$(find "$ROOT/tests/smoke" "$ROOT/scripts" "$ROOT/docs" "$ROOT/examples" -name '*.sh' 2>/dev/null | wc -l | tr -d '[:space:]')
if [ "$_files" -ge 300 ] && [ "$_docs_lines" -ge 400 ]; then
    t_ok "scanning $_files shell files + $_docs_lines lines of learner-facing shell"
else
    t_fail "corpus too thin ($_files files, $_docs_lines doc-shell lines) -- the tree
 moved and this lint is checking less than it claims"
fi

# ---- 3: no banned flag in the smoke tier or scripts/ -----------------------
# Comment lines are skipped, so a source can NAME the thing it bans. The one
# path skipped wholesale is this file itself, for the same reason and with the
# reasoning spelled out at the skip.
: > "$tmp/findings"
echo "$BANNED" | while IFS='|' read -r pat why alt; do
    [ -n "$pat" ] || continue
    # The learner-facing corpus is already flattened to file:line:text, so it is
    # searched as one file and its hits keep their location.
    grep -n -- "$pat" "$docs_sh" 2>/dev/null | sed 's/^[0-9]*://' \
      | grep -v ':[0-9]*: *#' \
      | while IFS= read -r hit; do
            echo "$hit: '$pat' -- $why; use $alt" >> "$tmp/findings"
        done
    find "$ROOT/tests/smoke" "$ROOT/scripts" -name '*.sh' 2>/dev/null | while read -r f; do
        # THIS file is skipped, and only this one. The source that defines a ban
        # has to be able to write the banned string down -- the same reason
        # comment lines are skipped above. It is one path by construction, not a
        # growing list of excused files, and check 1 independently proves the
        # matcher still catches a real occurrence.
        case "$f" in *[/]posix_utils_lint.sh) continue ;; esac
        grep -n -- "$pat" "$f" 2>/dev/null \
          | grep -v '^[0-9]*: *#' \
          | while IFS= read -r hit; do
                echo "${f#$ROOT/}:${hit%%:*}: '$pat' -- $why; use $alt" >> "$tmp/findings"
            done
    done
done
_n=$(wc -l < "$tmp/findings" 2>/dev/null | tr -d '[:space:]')
[ -n "$_n" ] || _n=0
if [ "$_n" -eq 0 ]; then
    t_ok "no GNU-only utility flags in the shell sources"
else
    t_fail "$_n GNU-only flag use(s):
$(head -n 12 "$tmp/findings")"
fi

# ---- 4: the C sources build no --color= shell command ----------------------
# Narrow on purpose. jichi's OWN --color/--no-color CLI flags are legitimate and
# appear all over main.c; only the =VALUE form is a grep/ls invocation. That is
# why this checks for the equals sign rather than the word.
_c=$(grep -rn -- "--color=" "$ROOT/src" "$ROOT/include" 2>/dev/null | wc -l | tr -d '[:space:]')
if [ "$_c" -eq 0 ]; then
    t_ok "no --color=VALUE shell flag in src/ or include/"
else
    t_fail "a --color=VALUE flag is back in the C sources:
$(grep -rn -- "--color=" "$ROOT/src" "$ROOT/include" 2>/dev/null | head -n 5)"
fi

# ---- 5: search_code's grep line still neutralises the colour environment ----
# The fix has two halves and only one of them is visible as an absence. If a
# later edit drops GREP_OPTIONS= while keeping the flag removed, the tool stays
# portable but regains the escape-injection hole the flag was there to close.
if grep -q 'GREP_OPTIONS= grep -rnI' "$ROOT/src/tools/jc_tool_search.c"; then
    t_ok "search_code still neutralises GREP_OPTIONS instead of passing --color"
else
    t_fail "search_code no longer sets GREP_OPTIONS= -- a colour-injecting \
environment can put ANSI escapes back into a captured pipe"
fi

# ---- 6: every exec in src/ resets the child's signal dispositions -----------
# See jc_proc.h. jichi ignores SIGPIPE, exec preserves an IGNORED disposition,
# and so every command the agent ran inherited it -- a pipeline producer then
# spins on EPIPE instead of dying when its consumer exits. Measured on this
# workstation with dash: 6.7 s and 59 MB of error spew, against 0.002 s once
# the child resets. GNU coreutils check their write result, which is the only
# reason Linux never showed it; OpenBSD's yes(1) does not, and did.
#
# The rule is positional: the reset must be the statement before the exec, so
# nothing can be inserted between them later.
_bad=""
for f in $(grep -rl "execvp\|execv(\|execl(\|execlp" "$ROOT/src" 2>/dev/null); do
    _n=$(grep -cE "(^|[^_a-zA-Z])exec[a-z]*\(" "$f" 2>/dev/null)
    _g=$(grep -B1 -E "(^|[^_a-zA-Z])exec[a-z]*\(" "$f" 2>/dev/null | grep -c "jc_proc_child_sigreset")
    [ "$_n" -eq "$_g" ] || _bad="$_bad ${f#$ROOT/}($_g/$_n)"
done
if [ -z "$_bad" ]; then
    t_ok "every exec site in src/ resets signals in the child first"
else
    t_fail "exec without jc_proc_child_sigreset():$_bad -- the child inherits \
jichi's ignored SIGPIPE and a piped producer will hang instead of exiting"
fi

# ---- 6b (M472): nothing calls pipe() directly except the wrapper ------------
# Same shape as the popen rule below, and for a neighbouring reason: a bare pipe()
# hands both ends to every child jichi execs, and one of those children is
# `sh -c <whatever the model chose>`. jc_pipe_cloexec sets FD_CLOEXEC on both ends;
# dup2 does not copy the flag, so a child that installs an end as its stdio keeps
# working. Measured before the wrapper: a model-issued shell holding jichi's run
# journal, telemetry sink and provider socket.
_bp=$(grep -rn "[^_a-zA-Z]pipe(" "$ROOT/src" 2>/dev/null \
      | grep -v "jc_proc.c:" | grep -v "jc_pipe_cloexec" | wc -l | tr -d '[:space:]')
if [ "$_bp" -eq 0 ]; then
    t_ok "pipe() is called only through jc_pipe_cloexec"
else
    t_fail "$_bp direct pipe() call(s) outside jc_proc.c:
$(grep -rn "[^_a-zA-Z]pipe(" "$ROOT/src" 2>/dev/null | grep -v "jc_proc.c:" | head -n 5)"
fi

# ---- 6c (M472): every exec site closes inherited descriptors ----------------
# The child-side backstop, counted per file exactly as check 6 counts sigreset.
# Two layers guard the same boundary and both are easy to forget at a NEW exec
# site, so both are counted.
_bad=""
for f in $(grep -rl "execvp\|execv(\|execl(\|execlp" "$ROOT/src" 2>/dev/null); do
    _n=$(grep -cE "(^|[^_a-zA-Z])exec[a-z]*\(" "$f" 2>/dev/null)
    _g=$(grep -B2 -E "(^|[^_a-zA-Z])exec[a-z]*\(" "$f" 2>/dev/null | grep -c "jc_proc_child_close_fds")
    [ "$_n" -eq "$_g" ] || _bad="$_bad ${f#$ROOT/}($_g/$_n)"
done
if [ -z "$_bad" ]; then
    t_ok "every exec site in src/ closes inherited descriptors first"
else
    t_fail "exec without jc_proc_child_close_fds():$_bad -- the child inherits \
jichi's sinks and sockets; see docs/analysis/2026-08-17-source-hardening-audit.md"
fi

# ---- 7: nothing calls popen() directly except the wrapper -------------------
# A popen'd child cannot fix this itself: POSIX forbids a non-interactive shell
# from trapping or resetting a signal ignored on entry, so `trap - PIPE` in the
# command string does nothing at all. jc_proc_popen drops the disposition for
# the duration of the fork instead.
_p=$(grep -rn "[^_a-zA-Z]popen(" "$ROOT/src" 2>/dev/null \
     | grep -v "jc_proc.c:" | grep -v "jc_proc_popen" | wc -l | tr -d '[:space:]')
if [ "$_p" -eq 0 ]; then
    t_ok "popen() is called only through jc_proc_popen"
else
    t_fail "$_p direct popen() call(s) outside jc_proc.c:
$(grep -rn "[^_a-zA-Z]popen(" "$ROOT/src" 2>/dev/null | grep -v "jc_proc.c:" | head -n 5)"
fi

# ---- 7c: a directory is made private only through jc_mkdir_p_private -------
# THE DEFECT THIS EXISTS FOR (M488). Four sites did `jc_mkdir_p(d); jc_make_private(d);`
# -- which re-permissions the directory whether or not jichi created it -- and two of
# them take the path from the user (`--log`, `--control`). Run as root, every container
# and most CI, `--log /tmp/jichi.jsonl` turned /tmp into 0700 root-only for the whole
# machine: measured 1777 before, 700 after. Non-root was inert only BY ACCIDENT (chmod
# fails EPERM on a directory you do not own, and the return was discarded), so it
# reproduces at any privilege on a directory the user DOES own.
#
# The pair is now one call. `jc_make_private` on a FILE is untouched and correct -- a
# file jichi is about to write is jichi's -- so this bans the pairing, not the function.
_mp=$(grep -rn -A3 "jc_mkdir_p(" "$ROOT/src" 2>/dev/null \
      | grep "jc_make_private(" | grep -v "jc_platform_posix.c" | wc -l | tr -d '[:space:]')
if [ "$_mp" -eq 0 ]; then
    t_ok "no jc_mkdir_p followed by jc_make_private (use jc_mkdir_p_private)"
else
    t_fail "$_mp site(s) re-permission a directory they may not have created -- use jc_mkdir_p_private():
$(grep -rn -A3 "jc_mkdir_p(" "$ROOT/src" 2>/dev/null | grep "jc_make_private(" | grep -v "jc_platform_posix.c" | head -n 4)"
fi

# M546: the corpus is the TRACKED tree, for the reason license_lint learned the same
# day. `grep -r "$ROOT/tests"` walks the WORKING directory, and
# tests/bench/craft_ab/results/ holds `events.jsonl` files recording every shell
# command a benchmarked MODEL chose to run. Those are captured model output, not this
# project's shell: checks 8 and 9 failed on three `grep --include` calls and two
# `grep -v "a\|b"` alternations that a 9B model had typed into
# run_terminal_command. Linting them is linting someone else's shell.
#
# Measured before generalising: EIGHT lints walk the working tree, and only TWO are
# actually reachable by bench results (license_lint, now fixed, and this one). The
# other six scan narrower paths. A meta-lint on "walks the working tree" would have
# reported six false positives -- the same over-selection that killed the vacuous-check
# lint (scripts/mutant-sweep.sh records that one).
#
# Falls back to find when git is absent (a released tarball, an old platform), and
# there it excludes /results/ explicitly.
tracked_files() {
    if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
        git -C "$ROOT" ls-files -- "$@" 2>/dev/null | sed "s|^|$ROOT/|"
    else
        find "$@" -type f 2>/dev/null | grep -v '/results/'
    fi
}

# ---- 8: no GNU-only FILE FILTERS on grep -----------------------------------
# --include= / --exclude= / --exclude-dir= are the nastiest of the family,
# which is why they get their own check instead of a row in the table above.
# BSD grep does not reject them -- it reads the argument as a FILENAME, warns
# "No such file or directory", and keeps searching WITHOUT the filter. So the
# failure is silent where the result is used directly, and INVERTED where the
# grep sits inside `!`: docs_flags reported 146 of 153 flags "documented
# nowhere" on OpenBSD against docs that were entirely fine.
#
# Matched together with the word `grep` on the same line, deliberately: tar's
# --exclude is portable across GNU tar and bsdtar and is not this bug.
#
# AND WITH `$G`, WHICH IS WHY THIS CHECK MISSED ONE (2026-09-19).
# ctx_estimate_lint ran `"$G" -rl ... src/ --include=*.c` and this check stayed
# green, because that line never contains the literal word `grep`. OpenBSD found
# it the way BSD grep always does -- silently, by ignoring the filter and walking
# the BUILT tree, so the check reported `src/chat/jc_compact.o`, an object file,
# as a source using the symbol. That is the THIRD check in two days whose
# extraction matched the spelling `grep` while the real call sites spell it `$G`.
# When a tier abstracts its own tool behind a variable, every pattern about that
# tool must know the variable's name.
# CONTINUATION LINES ARE JOINED FIRST (2026-09-19). A shell command wrapped
# over two lines puts `$G` on one and `--include=` on the next, and a line-based
# match sees neither together: doc_claims_lint carried exactly that shape and
# this check stayed green while OpenBSD collapsed its extraction to zero. awk
# joins any line ending in a backslash to the next before the patterns run, so
# the unit matched is the COMMAND rather than the line. (Same error as the README
# check, which was line-based against hard-wrapped markdown two days ago.)
_gf=$(tracked_files tests scripts \
      | while read -r _gf_f; do
            awk -v F="$_gf_f" '
              { line = $0
                while (sub(/\\$/, "", line) && (getline nxt) > 0) { line = line nxt }
                printf "%s:%d:%s\n", F, NR, line }' "$_gf_f" 2>/dev/null
        done \
      | grep -- "--include=\|--exclude=\|--exclude-dir=" 2>/dev/null \
      | grep -E "grep|[$]G" | grep -v '^[^:]*:[0-9]*: *#' \
      | grep -v "posix_utils_lint.sh")
# ONE extraction, counted AND printed. The failure branch used to re-extract with
# a plain `grep -rn`, which joined no continuation lines and swept in generated
# bench output -- so the count said 1 and the evidence showed two unrelated
# `events.jsonl` lines. A check whose evidence is not the thing it counted
# cannot be acted on, and sends the next reader to the wrong file.
_gfn=$(printf '%s' "$_gf" | grep -c . | tr -d '[:space:]')
[ -n "$_gf" ] || _gfn=0
if [ "${_gfn:-0}" -eq 0 ]; then
    t_ok "no GNU-only --include/--exclude file filters on grep"
else
    t_fail "$_gfn grep file-filter flag(s) BSD grep silently ignores -- it reads
the argument as a FILENAME and searches on WITHOUT the filter:
$_gf
use smoke_srcfiles (tests/smoke/_smoke.sh), or find -name ... | xargs grep"
fi

# ---- 9: no GNU BRE alternation ----------------------------------------------
# `\|` is a GNU extension to BASIC regular expressions. POSIX BRE has no
# alternation operator at all, so BSD grep and BSD sed read `\|` as a literal
# pipe and the pattern simply never matches -- silently, with exit 1, which is
# indistinguishable from an honest "not found".
#
# docs_locators_lint reported "0 of 3 index pages explain where commands run"
# on OpenBSD against pages that all said so; doctor.sh, config_defaults_lint
# and subagent_itercap carried the same construct. The fix is always the same:
# -E, where `(a|b)` is POSIX and portable.
#
# A line is only flagged when its grep/sed is NOT already in -E mode, because
# inside an ERE `\|` is the correct way to write a LITERAL pipe -- three sites
# in this tier do exactly that and are right to.
_bre=$({ tracked_files tests scripts | xargs -r grep -n '\\|' 2>/dev/null;
         grep -n '\\|' "$docs_sh" 2>/dev/null | sed 's/^[0-9]*://'; } \
       | grep -E "grep |sed |[$]G" \
       | grep -vE "grep -[a-zA-Z]*E|sed -E|[$]G\" -[a-zA-Z]*E|[$]G -[a-zA-Z]*E" \
       | grep -v '^[^:]*:[0-9]*: *#' \
       | grep -v "posix_utils_lint.sh" | wc -l | tr -d '[:space:]')
if [ "$_bre" -eq 0 ]; then
    t_ok "no GNU BRE alternation outside -E patterns"
else
    t_fail "$_bre GNU-only \\| alternation(s) BSD tools read as a literal pipe:
$({ tracked_files tests scripts | xargs -r grep -n '\\|' 2>/dev/null; grep -n '\\|' "$docs_sh" 2>/dev/null | sed 's/^[0-9]*://'; } | grep -E "grep |sed |[$]G" | grep -vE "grep -[a-zA-Z]*E|sed -E|[$]G\" -[a-zA-Z]*E|[$]G -[a-zA-Z]*E" | grep -v '^[^:]*:[0-9]*: *#' | grep -v posix_utils_lint | head -n 5)
use -E and (a|b)"
fi

# ---- 10: no hardcoded /bin/sh in src/ ---------------------------------------
# Android has no /bin. Its shell is /system/bin/sh, so sixteen hardcoded
# "/bin/sh" literals meant that on an Android 4.4.2 tablet EVERY shell-backed
# feature failed at once -- run_terminal_command, the verify gate, user tools,
# hooks, notify, sound, and `!`cmd`` expansion: 53 unit checks across 12 files.
# The hardware plan predicted this in writing and nothing measured it for
# months, because every other platform in the matrix keeps /bin/sh and Termux
# fakes it with an LD_PRELOAD.
#
# jc_shell_path() resolves it once. The two allowed mentions are its own
# implementation and the ACP protocol builder, which names a command for the
# CLIENT to spawn and must stay pure -- both reasoned at the site, not excused
# here: this check counts them rather than skipping their files, so a THIRD
# mention fails even inside them.
_sh=$(grep -rn '"/bin/sh"' "$ROOT/src" 2>/dev/null | wc -l | tr -d '[:space:]')
if [ "$_sh" -le 4 ]; then
    t_ok "/bin/sh appears only in jc_shell_path and the ACP payload ($_sh mention(s))"
else
    t_fail "$_sh hardcoded \"/bin/sh\" in src/ -- Android has no /bin; use jc_shell_path():
$(grep -rn '"/bin/sh"' "$ROOT/src" 2>/dev/null | head -n 6)"
fi

# ---- 11: no GNU word-boundary escapes ---------------------------------------
# `\b` is a GNU regex extension. POSIX BRE and ERE define no word-boundary
# operator at all, so a BSD grep/sed/awk reads it as an undefined escape and the
# pattern matches NOTHING -- exit 1, indistinguishable from an honest "not
# found". `\<` and `\>` are the same family; BSD spells those [[:<:]]/[[:>:]].
#
# THE DEFECT THIS EXISTS FOR: milestone_currency_lint (M463) extracted the
# highest milestone the docs cite with `grep -ohE '\bM[0-9]{3}\b'`. On OpenBSD
# 7.9 that returned nothing, so the ground truth was the empty string. It failed
# LOUDLY only because that lint had a floor asserting its own extraction found
# something -- without the floor it would have compared against nothing and
# reported success. This tier had a lint for `grep -P` and `\|` already; it was
# one row short of catching a construct from the same family, typed by the same
# author, the same day.
#
# Portable replacements: tokenise with `tr -c '0-9A-Za-z_' '\n'` and anchor with
# ^...$, or match the delimiters explicitly as (^|[^0-9A-Za-z_]).
#
# Narrowed to lines that also invoke grep/sed/awk, deliberately: in a printf
# format `\b` is a BACKSPACE and entirely legitimate, and that is a different
# construct that happens to share two characters.
_wb_pat='\\[b<>]'
printf 'grep -E "\\bM[0-9]" f\n' > "$tmp/wb_positive"
if grep -q "$_wb_pat" "$tmp/wb_positive" 2>/dev/null; then
    _wb=$({ grep -rn "$_wb_pat" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null;
            grep -n "$_wb_pat" "$docs_sh" 2>/dev/null | sed 's/^[0-9]*://'; } \
          | grep -E "grep |sed |awk " \
          | grep -v '^[^:]*:[0-9]*: *#' \
          | grep -v "posix_utils_lint.sh" | wc -l | tr -d '[:space:]')
    if [ "$_wb" -eq 0 ]; then
        t_ok "no GNU word-boundary escapes in grep/sed/awk patterns"
    else
        t_fail "$_wb GNU word-boundary escape(s) that match NOTHING on BSD:
$({ grep -rn "$_wb_pat" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null; grep -n "$_wb_pat" "$docs_sh" 2>/dev/null | sed 's/^[0-9]*://'; } | grep -E "grep |sed |awk " | grep -v '^[^:]*:[0-9]*: *#' | grep -v posix_utils_lint | head -n 5)
tokenise with tr -c '0-9A-Za-z_' and anchor, or match (^|[^0-9A-Za-z_])"
    fi
else
    t_fail "the word-boundary matcher does not flag a planted positive -- \
check 11 is scanning for something it cannot find and every pass is meaningless"
fi

# ---- 12: no GNU hex escapes in a sed/grep/awk pattern -----------------------
# `\xNN` is a GNU extension. POSIX sed, grep and awk have no hex escape, so a BSD
# reads `\x1b` as a literal `x` followed by `1b` and the pattern matches NOTHING --
# silently, with exit 1, indistinguishable from an honest "not found".
#
# THE DEFECT THIS EXISTS FOR (M471): five drivers stripped ANSI escapes with
# `sed 's/\x1b[\[][0-9;?]*[a-zA-Z]//g'`, which on OpenBSD stripped nothing at all.
# That was ALL THREE of that platform's remaining smoke failures, one cause:
# sessions_footprint and turn_scratch then hunted for a /context gauge number
# inside a line still full of escapes ("before='' after=''"), and setup_keyfile's
# width check counted the escapes as columns and reported lines "over 76 columns"
# that were nothing of the kind. The last one is the tell -- the quoted evidence
# was mostly escape sequences, so the stripper had visibly done nothing and the
# check still blamed the width.
#
# Portable: build the byte with printf (`_esc=$(printf '\033')`) and interpolate,
# or use the shared smoke_plain helper. printf's OCTAL escapes are POSIX; it is
# only the hex form in a *pattern* that is not.
#
# Restricted to lines that also invoke sed/grep/awk, deliberately: `printf '\x41'`
# is a different construct, and comment lines are skipped so a source can name
# what it bans.
_hx_pat='\\x[0-9a-fA-F]'
printf 'sed "s/\\x1b//" f\n' > "$tmp/hx_positive"
if grep -q "$_hx_pat" "$tmp/hx_positive" 2>/dev/null; then
    _hx=$(grep -rn "$_hx_pat" "$ROOT/tests/smoke" "$ROOT/scripts" 2>/dev/null \
          | grep -E 'sed |grep |awk ' \
          | grep -v '^[^:]*:[0-9]*: *#' \
          | grep -v "posix_utils_lint.sh" | wc -l | tr -d '[:space:]')
    if [ "$_hx" -eq 0 ]; then
        t_ok "no GNU hex escapes in sed/grep/awk patterns"
    else
        t_fail "$_hx GNU hex escape(s) that match NOTHING on BSD:
$(grep -rn "$_hx_pat" "$ROOT/tests/smoke" "$ROOT/scripts" 2>/dev/null | grep -E 'sed |grep |awk ' | grep -v '^[^:]*:[0-9]*: *#' | grep -v posix_utils_lint | head -n 5)
build the byte with printf and interpolate it, or use smoke_plain"
    fi
else
    t_fail "the hex-escape matcher does not flag a planted positive -- check 12 \
is scanning for something it cannot find and every pass is meaningless"
fi

# ---- 15+16: no `grep -o` pattern that can match the EMPTY string -------------
# THE DEFECT THIS EXISTS FOR (M481). Two drivers read a gauge out of a PTY
# transcript with a two-stage pipeline ending in
#
#     ... | grep -o '[0-9]*'
#
# `[0-9]*` is zero-or-more, so the pattern matches the empty string at every
# position. GNU grep skips empty matches and prints the digits; OpenBSD's
# `grep version 0.9` prints NOTHING **and exits 0**. So `sessions_footprint` and
# `turn_scratch` failed on that row with `before='' after=''` -- and because the
# output was empty rather than wrong, the message read exactly like jichi never
# printing the gauge. They were the last two red checks in the platform matrix
# and stood undiagnosed for months across three sessions.
#
# It was diagnosed by DIFFERENCE, not by reading: NetBSD passes both drivers,
# because it is a BSD whose userland ships **GNU grep 2.5.1a**. Two BSDs, one
# passing, one failing, same source -- which turns a months-old mystery into a
# one-line probe.
#
# WHY THIS SHAPE AND NOT THE FLAG TABLE ABOVE: nothing here is a non-POSIX flag.
# `grep -o` is portable and every one of the ~40 other uses in this tier is
# correct, because they all carry at least one MANDATORY atom
# (`grep -o 'Arenas: session [0-9]* KB'` works fine on OpenBSD). The hazard is
# the pattern being nullable *as a whole*.
#
# SCOPE, stated so it is not mistaken for more: this flags a pattern that is a
# SINGLE starred atom -- `'[0-9]*'`, `'.*'`, `'[a-z]*'`. A compound nullable
# pattern such as `'[0-9]*[a-z]*'` would slip through. That is a deliberate
# trade: a general nullability check needs a regex parser, and this catches the
# spelling that actually occurred plus its near neighbours with no false
# positives on the existing corpus.
# The literal `[` is spliced in from _lb rather than written next to the class
# that follows it: a bare `\[` immediately before `[^...]` puts the two
# two adjacent open-brackets in this file, and smoke_lint's bashism check is
# deliberately blunt -- it flags that pair anywhere in a driver unless a colon
# follows (a POSIX character class). Note this comment cannot SHOW the pair for
# the same reason, which is the M466 lesson in miniature: a finding whose own
# report is corrupted by the thing it reports. It
# caught this, and it caught it on the OpenBSD FULL-TIER run rather than here,
# because I had re-run only the lint I edited. Run the tier, not the driver.
_lb='\['
_nul_pat="grep -o ['\"](${_lb}[^]]*\]|\\\\?.)\*['\"]"
mkdir -p "$tmp/self15"
printf "n=\$(printf 'x 12 y' | grep -o '[0-9]*')\n"      > "$tmp/self15/bad.sh"
printf "n=\$(printf 'x 12 y' | grep -o 'x [0-9]* y')\n"  > "$tmp/self15/good.sh"
_self=$(grep -lE "$_nul_pat" "$tmp/self15"/*.sh 2>/dev/null | wc -l | tr -d '[:space:]')
if [ "$_self" -eq 1 ]; then
    t_ok "the nullable-pattern matcher flags a planted positive and spares the mandatory-atom form"
else
    t_fail "matcher is broken: $_self/1 files flagged -- check 16 below is meaningless"
fi

_nul=$(grep -rnE "$_nul_pat" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null \
       | grep -v '^[^:]*:[0-9]*: *#' \
       | grep -v "posix_utils_lint.sh" | wc -l | tr -d '[:space:]')
if [ "$_nul" -eq 0 ]; then
    t_ok "no nullable \`grep -o\` pattern (OpenBSD's grep prints nothing and exits 0)"
else
    t_fail "$_nul nullable \`grep -o\` pattern(s) that silently produce NO OUTPUT on OpenBSD:
$(grep -rnE "$_nul_pat" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null | grep -v '^[^:]*:[0-9]*: *#' | grep -v posix_utils_lint | head -n 5)
require a mandatory atom ([0-9][0-9]*), or extract in one pass with
sed -n 's/.*prefix \([0-9][0-9]*\) suffix.*/\1/p'"
fi

# ---- 18: grep -a and grep -m are GNU-only -----------------------------------
# MEASURED, not anticipated (M661). illumos's grep -- /usr/bin/grep and
# /usr/xpg4/bin/grep alike -- rejects both outright:
#
#     grep: illegal option -- a
#     usage:  grep [-E|-F] [-bchHilLnoqrRsvx] [-A num] [-B num] ...
#
# `grep -a` is what three pty drivers used to read a capture containing NUL
# bytes, and on illumos it made them fail with an exit 2 that looks nothing like
# the assertion they were making. `smoke_bgrep` in _smoke.sh does the same job
# portably by stripping the NULs, which is the ONLY thing making grep treat such
# a file as binary. -m (max-count) is in the same class and is banned with it.
#
# The universe is tests/ and scripts/: the product's own grep usage is check 11's
# and check 12's business, and this driver's own text is excluded because it
# quotes the forms it forbids.
_ga_pat='grep -[A-Za-z]*[am][A-Za-z]*[ "]'
_ga=$(grep -rnE "$_ga_pat" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null \
      | grep -v '^[^:]*:[0-9]*: *#' \
      | grep -v 'posix_utils_lint.sh' \
      | grep -vE 'grep -[A-Za-z]*(A|B)[A-Za-z]* ' | wc -l | tr -d '[:space:]')
if [ "$_ga" -eq 0 ]; then
    t_ok "no GNU-only \`grep -a\` / \`grep -m\` (illumos rejects both; use smoke_bgrep)"
else
    t_fail "$_ga use(s) of GNU-only \`grep -a\`/\`-m\`, which illumos rejects with
\"illegal option\" and exit 2 -- a failure that looks nothing like the assertion:
$(grep -rnE "$_ga_pat" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null | grep -v '^[^:]*:[0-9]*: *#' | grep -v posix_utils_lint.sh | grep -vE 'grep -[A-Za-z]*(A|B)[A-Za-z]* ' | head -6)
For a capture with NUL bytes use smoke_bgrep FILE [args] (tests/smoke/_smoke.sh)."
fi

# ---- 19: SIGTERM is never followed by a BLOCKING waitpid --------------------
# THE DEFECT (M609, built M661b). MCP and LSP shutdown did
#
#     kill(pid, SIGTERM);
#     waitpid(pid, &status, 0);     /* no timeout */
#
# so a server that traps or ignores SIGTERM hung jichi's exit forever: no journal
# finalisation, no lease release, and nothing on screen saying why.
# jc_worker_reap_grace is the answer and was already in the tree, used by the
# parallel pool and the daemon.
#
# WHY A LINT AND NOT A SECOND DRIVER. `peer_reap_grace.sh` drives the MCP site
# with a real deaf server, which is the honest proof that the HELPER works. The
# LSP sites cannot be driven without a second mock speaking a second protocol
# for one shared call -- and this check covers them, and every future site,
# forever. It is also how the FOURTH site was found: M661b fixed three and
# missed jc_lsp.c's alloc-failure path, which this check reported on its first
# run. "When a hazard earns a documented fix, grep for its family" (M475).
#
# The window is six lines, which is what separates the pair at every site seen so
# far; a `waitpid` further away than that is not obviously the same reap and a
# lint should not guess.
# Comment lines are skipped, and the SAME line is checked as well as the six
# after it: both mattered on the first run. The comments explaining this very fix
# say "waitpid" and were reported as violations, and the real fourth site put
# `kill(pid, SIGTERM); waitpid(pid, NULL, 0);` on ONE line, which a
# next-line-only window steps straight over.
# THE FILE LIST IS CAPTURED AND FLOORED BEFORE awk SEES IT. An awk with a
# program but NO file operands reads stdin, and both outcomes are wrong: where
# stdin never delivers EOF (a terminal, a socket, `make ci` from a tty) the
# check HANGS FOREVER -- measured 2026-09-21, this one ran 15h47m against a
# tree whose src/ was absent -- and where stdin is /dev/null it prints a
# cheerful `ok 19` having read zero source files. A floor turns both into a
# loud failure. `< /dev/null` below is belt-and-braces: if a future edit
# bypasses the floor, the check dies instead of hanging the gate.
_rp_files=$(tracked_files src | grep '\.c$' | sort)
_rp_n=$(printf '%s\n' "$_rp_files" | grep -c . | tr -d '[:space:]')
[ -n "$_rp_files" ] || _rp_n=0
if [ "${_rp_n:-0}" -lt 175 ]; then
    t_fail "check 19 scanned $_rp_n .c files under \$ROOT/src (floor 175) --
the universe is empty or the find broke, so a pass here would mean nothing."
else
_rp=$(awk '
    { line = $0; sub(/^[ \t]+/, "", line) }
    line ~ /^[*]/ { next }
    line ~ /^[/][*]/ { next }
    {
        hit = ($0 ~ /waitpid *[(]/ && $0 !~ /WNOHANG/)
        if (armed > 0 && hit) { printf "%s:%d:%s\n", FILENAME, FNR, $0 }
        if ($0 ~ /kill *[(].*SIGTERM/) {
            if (hit) { printf "%s:%d:%s\n", FILENAME, FNR, $0 }
            armed = 6
        } else if (armed > 0) { armed-- }
    }
' $_rp_files < /dev/null 2>/dev/null | head -8)
if [ -z "$_rp" ]; then
    t_ok "no blocking waitpid within six lines of a SIGTERM, $_rp_n files scanned"
else
    t_fail "blocking waitpid after SIGTERM -- a child that traps or ignores the
signal hangs the parent forever. Use jc_worker_reap_grace(pid, JC_WORKER_TERM_GRACE_MS):
$_rp"
fi
fi

# ---- 20: `grep -r` is never pointed at a single named FILE -------------------
# MEASURED on FreeBSD 15.1, 2026-09-18, and it cost two checks of
# ctx_estimate_lint.sh at once. Both did
#
#     n=$(grep -rc 'sym' src/chat/jc_compact.c | tr -d ' ')
#     [ "$n" = "2" ] || fail
#
# The file is NAMED, so `-r` had nothing to recurse into and was pure noise on
# GNU grep. On FreeBSD's grep it is not noise: **-r implies -H**, so the count
# came back `src/chat/jc_compact.c:2`, the comparison against "2" failed, and the
# driver reported that the source had drifted on a tree where nothing whatever
# was wrong. `tr -d ' '` deletes spaces, not a path prefix.
#
# This is the same shape as check 18 (`grep -a`) and the illumos `grep -o`
# family: a GNU-ism that is invisible here and only ever fires on a row that
# costs a VM boot to reach. A lint is the cheap end of that trade -- and it is
# the general form of the fix, not the instance, per CLAUDE.md "prefer a lint to
# an audit".
#
# THE UNIVERSE is tests/ and scripts/, the same as check 18, and the RULE is
# narrow on purpose: `-r` over a DIRECTORY is correct and common here (five call
# sites parse the `file:count` prefix deliberately, with `awk -F:` or
# `grep -v ':0$'`). What is flagged is only an -r whose operand is a path with a
# file extension. Flagging the directory form would ban the idiom the tier is
# built on.
# Captured and floored before awk sees it, for the reason spelled out at check
# 19: an awk with no file operands reads stdin, which hangs the gate forever on
# an open stdin and reports a vacuous `ok` on /dev/null.
_rf_files=$(tracked_files tests scripts | grep '\.sh$' | sort)
_rf_n=$(printf '%s\n' "$_rf_files" | grep -c . | tr -d '[:space:]')
[ -n "$_rf_files" ] || _rf_n=0
if [ "${_rf_n:-0}" -lt 360 ]; then
    t_fail "check 20 scanned $_rf_n tracked .sh files under \$ROOT/tests and \$ROOT/scripts
(floor 360) -- the universe is empty or the find broke, so a pass means nothing."
else
_rf=$(awk '
    FILENAME ~ /posix_utils_lint\.sh$/ { next }
    { line = $0; sub(/^[ \t]*/, "", line) }
    line ~ /^#/ { next }
    # `grep` OR the $G indirection. The first cut of this check matched only
    # the literal word and was therefore VACUOUS against the two call sites it
    # was written for, which spell it "$G" -- and floored at 0, a broken
    # extraction reads exactly like a clean tree. The perturbation caught it;
    # the count could not have.
    line ~ /(grep|\$G|\$\{G\})[^|]*[ \t]-[A-Za-z]*r[A-Za-z]*[ \t]/ {
        n = split($0, tok, /[ \t]/)
        for (i = 1; i <= n; i++) {
            t = tok[i]
            if (t ~ /^-/) continue                     # --include=*.c is fine
            if (t ~ /["'"'"'`()$]/) continue           # a pattern, not an operand
            if (t ~ /^[A-Za-z0-9_.\/-]+\.(c|h|sh|md|py|json|txt|mk)$/) {
                printf "%s:%d: %s\n", FILENAME, FNR, $0
                next
            }
        }
    }
' $_rf_files < /dev/null 2>/dev/null)
# `grep -c .` prints 0 AND exits 1 on empty input, so a `|| echo 0` fallback
# appends a SECOND zero and `[ "0
# 0" -eq 0 ]` is a syntax error, not a pass. wc -l, the idiom check 18 uses.
_nrf=$(printf '%s' "$_rf" | grep -c . | tr -d '[:space:]')
[ -n "$_rf" ] || _nrf=0
if [ "${_nrf:-0}" -eq 0 ]; then
    t_ok "no \`grep -r\` pointed at a single named file, $_rf_n scripts scanned"
else
    t_fail "$_nrf \`grep -r\` invocation(s) whose operand is a FILE, not a directory.
FreeBSD grep treats -r as -H, so the output gains a \"path:\" prefix that GNU grep
does not add -- the comparison then fails on a healthy tree. Drop the -r; it does
nothing on a named file:
$_rf"
fi
fi

# --- 21: no ERE interval {n} inside an awk regex literal --------------------
# THE DEFECT, diagnosed on a live illumos guest at M680 and invisible before it.
# illumos ships one-true-awk "version Aug 27, 2018", which does NOT support ERE
# interval expressions. Measured on the guest itself:
#
#     echo "a<brace>4<brace>b" | awk '/a{4}b/ {print "matched"}'   ->  matched
#
# `{4}` is a literal brace there, not a repetition count. So a date pattern
# spelled [0-9]{4}-[0-9]{2}-[0-9]{2} matches NOTHING on that platform -- and it
# fails **silently**, because a regex that matches nothing is not an error. In
# `bibliography_lint` it reported dozens of correctly-marked entries as unmarked
# and the check looked like a content problem for a day.
#
# In `grep -E` intervals are POSIX and portable, and this tree uses them there
# deliberately -- so the rule is narrow: inside an awk REGEX LITERAL only. Spell
# the date [0-9][0-9][0-9][0-9] instead; it is uglier and it works everywhere.
#
# Plain `grep`, not a $G indirection: this driver defines none, and the first
# draft used one anyway -- an empty variable that dash ran as the empty
# command, printing ": Permission denied" beside a green check. A check that
# prints an error and still says ok is worse than one that fails.
#
# This finds zero today, which is the point of a prohibition rather than an
# extraction: its teeth come from perturbation, exactly like checks 8, 9 and 14
# above, not from a standing population.
_iv=$(for f in "$SMOKE_DIR"/*.sh; do
          [ -f "$f" ] || continue
          grep -nE '/[^/]*\{[0-9]+(,[0-9]*)?\}[^/]*/' "$f" 2>/dev/null \
            | grep -v 'grep' \
            | grep -v '^[0-9]*:#' \
            | sed "s#^#$(basename "$f"):#"
      done)
if [ -z "$_iv" ]; then
    t_ok "no ERE interval {n} inside an awk regex literal (illumos awk reads it literally)"
else
    t_fail "interval expression(s) inside an awk regex literal:
$_iv
illumos's awk matches {n} as a literal brace, so the pattern silently matches
nothing there -- no error, just a check that stops checking. Spell it out:
[0-9][0-9][0-9][0-9] rather than [0-9]{4}. In grep -E intervals are fine."
fi

# --- 22: no newline-less stream feeds `grep -o` -----------------------------
# THE DEFECT, measured on a live illumos guest at M681 -- and the register had
# the mechanism wrong in a way that made the work look thirty times bigger.
#
# DEFERRED.md recorded it as "illumos `grep -o` returns only the FIRST match per
# line". As stated that is false, and the guest says so:
#
#     printf "item1 item22 item333\n" | grep -o "item[0-9]*"   ->  3 matches
#     printf "item1 item22 item333"    | grep -o "item[0-9]*"   ->  1 match
#
# One character apart. The trigger is a line with **no trailing newline**, and
# `printf '%s'` produces exactly that. On the real case -- jc_config.c flattened
# -- it was 1 match against the 593 the unflattened file gives, and appending a
# newline restores all 593.
#
# It cost `config_defaults_lint` its whole first check on that platform: every
# field's extraction returned one statement, nothing compared, and the driver
# reported "only 0 defaults were comparable -- the extraction broke, so this
# lint is vacuous". That check exists for exactly this and it fired.
#
# WHY THIS SHAPE AND NOT `tr '\n' ' ' | grep -o`: the register predicted the
# pipeline form and there are **zero** of those in the tier. The real shape is a
# shell variable printed without a newline. Counting the wrong shape is how a
# 13-site one-character fix was filed as a 30-driver rewrite.
#
# `grep -q` is deliberately NOT covered: a match is a match with or without the
# newline, so including it would make the diff larger than the finding.
_nl=$(for f in "$SMOKE_DIR"/*.sh; do
          [ -f "$f" ] || continue
          grep -nE "printf '%s' [^|]*\| *grep -o" "$f" 2>/dev/null \
            | grep -v '^[0-9]*:#' \
            | sed "s#^#$(basename "$f"):#"
      done)
if [ -z "$_nl" ]; then
    t_ok "no newline-less stream feeds grep -o (illumos returns one match)"
else
    t_fail "a stream with no trailing newline is piped into grep -o:
$_nl
On illumos that yields ONE match instead of all of them, silently -- the
extraction collapses and the check goes vacuous rather than red. Use
printf '%s\\n'. (grep -q is unaffected and not flagged.)"
fi

# --- 23: `env -u` is a GNU extension ----------------------------------------
# illumos /usr/bin/env accepts only `env [-i] [name=value ...] [utility ...]`
# and answers `env: illegal option -- u`. tests/smoke/doctor_language.sh used
# it to clear JICHI_LANG for two of its five doctor runs, and on OmniOS those
# two produced NO OUTPUT AT ALL (M683).
#
# WHY IT SURVIVED A DEDICATED rc GUARD, which is the part worth keeping: that
# driver already floors every run's exit status, and accepts 0 or 1 because
# doctor's own verdict is 1 when any check FAILs. illumos env exits **1**. The
# status was therefore indistinguishable from a legitimate result, and only the
# driver's other half -- "every capture must CONTAIN a language row" -- could
# see it. A rc guard is not an output guard.
#
# tests/smoke/state_root.sh line 31 has said "`env -u` is not POSIX" in a
# comment since it was written, and chose the portable form; the knowledge was
# in the tree and the other driver still shipped the GNU one. That gap is what
# a lint closes and a comment does not.
#
# The portable form is a subshell:  ( unset VAR; FOO=1 cmd )
# `env -i` IS POSIX and is deliberately not flagged.
#
# THE UNIVERSE is tests/ and scripts/, with this driver's own text excluded --
# the same hole, for the same reason, as check 18: a lint that quotes the form
# it forbids matches itself forever and buries the real finding. The hole is
# paid for below by proving the matcher on planted files, which check 18 does
# not do: an excluded file plus an unproven matcher is a check that can pass
# while reading nothing.
_eu_pat='(^|[^-[:alnum:]_])env +-u'
mkdir -p "$tmp/self23"
_eu_lit='env'
printf '%s -u FOO BAR=1 cmd\n' "$_eu_lit"        > "$tmp/self23/bad.sh"
printf '%s -i BAR=1 cmd\n'     "$_eu_lit"        > "$tmp/self23/posix.sh"
printf '( unset FOO; BAR=1 cmd )\n'              > "$tmp/self23/good.sh"
_s23=$(grep -lE "$_eu_pat" "$tmp/self23"/*.sh 2>/dev/null | wc -l | tr -d ' ')
if [ "$_s23" = "1" ] && grep -qE "$_eu_pat" "$tmp/self23/bad.sh"; then
    t_ok "the matcher flags a planted GNU-only unset and spares \`-i\` and the subshell form"
else
    t_fail "the matcher is broken ($_s23/1 planted files flagged) -- the check below would be meaningless"
fi

_envu=$(grep -rnE "$_eu_pat" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null \
        | grep -v '^[^:]*:[0-9]*: *#' \
        | grep -v 'posix_utils_lint.sh')
if [ -z "$_envu" ]; then
    t_ok "no GNU-only unset-one-variable form (illumos env exits 1, which reads as a verdict)"
else
    t_fail "a GNU-only option to \`env\` that illumos rejects:
$_envu
Use a subshell instead -- ( unset VAR; FOO=1 cmd ) -- which is POSIX everywhere.
illumos exits 1 for the refused option, and 1 is a status many tools return as
a legitimate result, so the run looks like a finding rather than a broken
fixture."
fi

# --- 24: awk RS is a SINGLE CHARACTER ----------------------------------------
# POSIX defines RS as one character; gawk extends it to a regular expression.
# illumos one-true-awk (Aug 27, 2018) uses only the FIRST character. Measured on
# OmniOS: with RS="-->" the input
#     A <!-- hidden 999 --> B 12345 C
# split on '-' alone and came back as `A <!` / ` hidden 999 ` / `> B 12345 C`.
#
# tests/smoke/i18n_tracks_lint.sh used RS="-->" to strip HTML comments before
# extracting figures. On illumos nothing was stripped, so the page's own
# `<!-- figures-behind: N -->` declaration counted as an undeclared figure --
# the check's stated premise ("a declaration's own numbers are not evidence")
# inverted (M683). It failed loudly there; the same stripper going quiet on
# another awk would have made the check vacuous instead, which is worse.
#
# RS="" is PARAGRAPH MODE, defined by POSIX, and is not flagged --
# portability_lint.sh:518 relies on it and passes on illumos.
# THE PATTERN IS ANCHORED, and the first draft was not: bare `RS *= *"..."`
# matched SMOKE_TMPDIRS= and READERS= -- any shell variable whose name happens
# to END in RS. Two false positives in the tier's own files, caught only
# because they were printed. A matcher that is never shown its own positives
# and negatives is the thing this driver exists to refuse, so it is proved
# below before it is trusted.
_rs_pat='(^|[^A-Za-z0-9_])RS *= *"[^"]{2,}"'
mkdir -p "$tmp/self24"
# The planted positive is assembled from a %s so the forbidden literal never
# appears in THIS file: written out plainly, check 25 below flags the fixture
# that proves it works. A self-matching lint reports itself forever and the
# real finding is lost in the noise.
_rs_lit='RS'
printf 'awk %sBEGIN{%s="-->"}%s f\n' "'" "$_rs_lit" "'" > "$tmp/self24/bad.sh"
printf 'awk %sBEGIN{%s=""}%s f\n'    "'" "$_rs_lit" "'" > "$tmp/self24/para.sh"
printf 'SMOKE_TMPDI%s="$SMOKE_TMPDI%s $d"\n' "$_rs_lit" "$_rs_lit" > "$tmp/self24/var.sh"
_s24=$(grep -lE "$_rs_pat" "$tmp/self24"/*.sh 2>/dev/null | wc -l | tr -d ' ')
if [ "$_s24" = "1" ] && grep -qE "$_rs_pat" "$tmp/self24/bad.sh"; then
    t_ok "the RS matcher flags a planted multi-char RS and spares RS=\"\" and a variable ending in RS"
else
    t_fail "the RS matcher is broken ($_s24/1 planted files flagged) -- check 24 below would be meaningless"
fi

_rs=$(for f in "$SMOKE_DIR"/*.sh "$ROOT"/scripts/*.sh; do
          [ -f "$f" ] || continue
          grep -nE "$_rs_pat" "$f" 2>/dev/null \
            | grep -v '^[0-9]*:[[:space:]]*#' \
            | sed "s#^#$(basename "$f"):#"
      done)
if [ -z "$_rs" ]; then
    t_ok "no multi-character awk RS (illumos uses only its first character)"
else
    t_fail "a multi-character awk RS, which only gawk reads as a regex:
$_rs
illumos one-true-awk splits on the FIRST CHARACTER alone, so the records are
not the ones the program means. Use index()/substr() and carry the state in a
global, or a single-character RS. RS=\"\" (paragraph mode) is POSIX and exempt."
fi

# ---- 27: a filter fed operands from an expansion must CLOSE ITS STDIN -------
# MEASURED 2026-09-21, twice, and the second time was this check's own fault.
#
# An `awk`/`sed`/`grep` given a program and NO file operands reads STDIN. Where
# stdin never delivers EOF (a terminal, a socket, `make ci` from a tty) the
# check HANGS FOREVER -- one run sat in pipe_read for 15h47m -- and where stdin
# is /dev/null it prints a cheerful `ok` having read nothing.
#
# WHY THIS CHECK WAS WRONG THE FIRST TIME, which is the useful part. Its first
# version matched only `$(find ...)` used inline as operands, because that was
# the spelling of the two sites that had just bitten me. It reported 0 hits and
# was believed. The commoner spelling is a VARIABLE -- `}' $targets` -- and
# there were five of those, including `sprintf_lint.sh`, which was measured
# hanging for 45s on a tree with no src/ and then printing
# `ok 2 - no raw sprintf outside the audited allowlist` after SIGTERM, having
# scanned zero files. Enumerating one way and agreeing with myself is exactly
# what "audit the universe, not the result" is about; the lint found the shape
# it was built from and nothing else.
#
# A FLOOR IS NOT ENOUGH, and that is why the rule is about stdin rather than
# about counting. `sprintf_lint` DID floor its file count and DID report
# `not ok 1 - scanned only 0 files` -- and then hung anyway, because `t_fail`
# records and continues. The floor tells you the universe is empty; only
# `< /dev/null` stops the filter waiting forever to be told what to read.
#
# THE RULE, in one sentence a reader can apply without this comment: if a
# filter's operands come from an expansion, close its stdin. That is mechanical,
# it needs no judgement about whether the expansion can be empty, and it is
# cheap to satisfy.
_cs_pat='^[[:space:]]*[}]?['\''"][[:space:]]+\$|(awk|sed|grep|cut|tr|sort|wc|head|tail|nl|paste|cat|od)[[:space:]]+['\''"][^'\''"]*['\''"][[:space:]]+\$'
mkdir -p "$tmp/cs"
printf "%s\n" "}' \$targets > \"\$tmp/offenders\""            > "$tmp/cs/bad1.sh"
printf "%s\n" "' \$(find \"\$R/src\" -name '*.c' | sort)"      > "$tmp/cs/bad2.sh"
printf "%s\n" "awk '{print}' \$files"                          > "$tmp/cs/bad3.sh"
printf "%s\n" "}' \$targets < /dev/null > \"\$tmp/offenders\"" > "$tmp/cs/good1.sh"
printf "%s\n" "_f=\$(find \"\$R/src\" -name '*.c' | sort)"     > "$tmp/cs/good2.sh"
printf "%s\n" "t_fail \"left: \$(find \"\$R\" -name x | tr '\\n' ' ')\"" > "$tmp/cs/good3.sh"
_csb=$(grep -lE "$_cs_pat" "$tmp/cs"/bad*.sh 2>/dev/null | wc -l | tr -d '[:space:]')
_csg=$(grep -E "$_cs_pat" "$tmp/cs"/good*.sh 2>/dev/null | grep -vc "/dev/null" | tr -d '[:space:]')
if [ "${_csb:-0}" -eq 3 ] && [ "${_csg:-0}" -eq 0 ]; then
    t_ok "the operand matcher flags all three planted shapes and spares a closed stdin, an assignment and a piped display string"
else
    t_fail "matcher broken: flagged $_csb/3 planted positives and $_csg/0 clean
forms -- check 28 below is meaningless until this passes"
fi

# ---- 28: the real scan ------------------------------------------------------
_cs_files=$(tracked_files tests scripts | grep '\.sh$' | sort)
_cs_n=$(printf '%s\n' "$_cs_files" | grep -c . | tr -d '[:space:]')
[ -n "$_cs_files" ] || _cs_n=0
if [ "${_cs_n:-0}" -lt 360 ]; then
    t_fail "check 28 scanned $_cs_n tracked .sh files (floor 360) -- the universe is
empty or the find broke, so a pass here would mean nothing."
else
    _cs=$(grep -nE "$_cs_pat" $_cs_files < /dev/null 2>/dev/null \
          | grep -v "/dev/null" | head -8)
    if [ -z "$_cs" ]; then
        t_ok "every filter fed operands from an expansion closes its stdin, $_cs_n scripts scanned"
    else
        t_fail "a filter takes its FILE OPERANDS from an expansion and leaves stdin open:
$_cs
When the expansion is empty the filter falls back to stdin -- hanging the gate
forever where stdin stays open, and reporting a vacuous pass on /dev/null. A
floor does not prevent this: t_fail records and continues. Add \`< /dev/null\`."
    fi
fi

t_done
