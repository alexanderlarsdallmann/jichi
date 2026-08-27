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

t_plan 17
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
_gf=$(tracked_files tests scripts | xargs -r grep -n -- \
        "--include=\|--exclude=\|--exclude-dir=" 2>/dev/null \
      | grep "grep" | grep -v '^[^:]*:[0-9]*: *#' \
      | grep -v "posix_utils_lint.sh" | wc -l | tr -d '[:space:]')
if [ "$_gf" -eq 0 ]; then
    t_ok "no GNU-only --include/--exclude file filters on grep"
else
    t_fail "$_gf grep file-filter flag(s) BSD grep silently ignores:
$(grep -rn -- "--include=\|--exclude=\|--exclude-dir=" "$ROOT/tests" "$ROOT/scripts" 2>/dev/null | grep "grep" | grep -v '^[^:]*:[0-9]*: *#' | grep -v posix_utils_lint | head -n 5)
use smoke_md_corpus, or find -name ... -exec grep"
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
       | grep -E "grep |sed " \
       | grep -vE "grep -[a-zA-Z]*E|sed -E" \
       | grep -v '^[^:]*:[0-9]*: *#' \
       | grep -v "posix_utils_lint.sh" | wc -l | tr -d '[:space:]')
if [ "$_bre" -eq 0 ]; then
    t_ok "no GNU BRE alternation outside -E patterns"
else
    t_fail "$_bre GNU-only \\| alternation(s) BSD tools read as a literal pipe:
$({ tracked_files tests scripts | xargs -r grep -n '\\|' 2>/dev/null; grep -n '\\|' "$docs_sh" 2>/dev/null | sed 's/^[0-9]*://'; } | grep -E "grep |sed " | grep -vE "grep -[a-zA-Z]*E|sed -E" | grep -v '^[^:]*:[0-9]*: *#' | grep -v posix_utils_lint | head -n 5)
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

t_done
