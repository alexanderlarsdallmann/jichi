#!/bin/sh
# smoke lint: every headless run a living page shows pins its stdin (M944; plan
# 2026-10-03, step 7.6).
#
# WHY THIS EXISTS. A headless `jichi -p` reads a stdin that is not a terminal, to EOF,
# as context for the prompt. Under ssh, CI, cron, a supervisor or a harness, stdin is
# a pipe or a socket that may never close, and the run waits forever (ANECDOTES #64,
# #78). At a terminal nothing blocks -- which is why the pages' examples looked fine:
# on 2026-10-04, 152 of the 179 headless runs on living pages had no redirect,
# including `ssh host 'cd ~/project && jichi -p ...'`, which does hang. The rule was
# prose in CLAUDE.md ("run every command you publish, `< /dev/null` included") and a
# lint for the smoke tier's own runs; the operator chose one rule for every example
# (2026-10-04), so a learner who pastes one into a script does not inherit a hang.
#
# CHECKED      -- living pages: every *.md under docs/ and at the root, except the
#                 dated records (docs/analysis, plans, dialogues, proposals, reviews,
#                 internal; ROADMAP, ANECDOTES, CHANGELOG, DECISIONS, DEFERRED,
#                 DRIVE_LOG), which quote runs as they were. In a shell-like fenced
#                 block (no language, sh, bash, shell, console, zsh), each command --
#                 continuation lines joined, comment lines skipped -- that runs
#                 `jichi` (or `./jichi`) with `-p`/`--print` outside quotes.
# A RUN PINS STDIN when its command carries `< /dev/null`, `--no-stdin`, a here-doc
#                 or another input redirect, reads the prompt from stdin (`-p -`), or
#                 is fed by a pipe earlier on the line.
# THE COMMAND  -- runs from `jichi` to the first unquoted `;`, `|`, `)`, comment or
#                 `&` that is not part of a redirection (`2>&1` is one command) --
#                 `$( )` inside the command is followed, not taken as its end --
#                 or to the quote that closes the string it sits in (`ssh host '...'`,
#                 `tmux new '...'`), so another program's `-p` is not read as jichi's.
# THE FLOOR    -- the runs extracted, 179 on 2026-10-04: a parser that stopped
#                 finding runs would otherwise find none unpinned.
# NOT CHECKED  -- runs in prose or in other block languages, and whether a run's
#                 command is otherwise right (that is doc_commands_lint's subject).
. "$(dirname "$0")/_smoke.sh"

t_plan 2
root=$(cd "$(dirname "$0")/../.." && pwd)
JC_STDIN_FLOOR=179

files=$( { find "$root/docs" -type f -name '*.md'; ls "$root"/*.md; } 2>/dev/null \
    | sed "s#^$root/##" \
    | grep -v -E '^docs/(internal|analysis|plans|dialogues|proposals|reviews)/' \
    | grep -v -x -E 'docs/(ROADMAP|ANECDOTES|DECISIONS|DEFERRED|DRIVE_LOG)\.md|CHANGELOG\.md' \
    | sort)

out=$(cd "$root" && printf '%s\n' "$files" | while IFS= read -r f; do
    [ -n "$f" ] && awk -v F="$f" '
# qstate(s): the quote still open at the end of s ("" for none), scanning as sh does
# for single and double quotes (an apostrophe inside "..." opens nothing, and a
# comment ends the scan, so an apostrophe in a comment opens no quote).
function qstate(s,   i, c, q) {
    q = ""
    for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (q == "") {
            if (c == "#" && (i == 1 || substr(s, i - 1, 1) ~ /[ \t]/)) break   # a comment
            if (c == "\047" || c == "\"") q = c
        } else if (c == q) q = ""
    }
    return q
}
function check(cmd, ln,   rest, off, m, k, p, pre, outer, i, c, q, seg, plain, hit, depth) {
    sub(/^[ \t]*\$[ \t]+/, "", cmd)
    if (cmd ~ /^[ \t]*#/) return
    rest = cmd; off = 0
    while (match(rest, /(^|[^A-Za-z0-9_.\/-])(\.\/)?jichi([ \t]|$)/)) {
        m = substr(rest, RSTART, RLENGTH)
        k = index(m, "./jichi"); if (k == 0) k = index(m, "jichi")
        p = off + RSTART - 1 + k
        off = p + 4
        rest = substr(cmd, off + 1)
        pre = substr(cmd, 1, p - 1)
        outer = qstate(pre)
        q = ""; seg = ""; plain = ""; depth = 0
        for (i = p; i <= length(cmd); i++) {
            c = substr(cmd, i, 1)
            if (q == "") {
                if (c == "\047" || c == "\"") {
                    if (outer != "" && c == outer) break
                    q = c; plain = plain "Q"; seg = seg c; continue
                }
                if (c == "(") depth++
                if (c == ")" && depth-- == 0) break       # the ) of a $( ) or ( ) we sit in
                if (depth == 0 && (c == ";" || c == "|")) break
                # `&` ends a command, except inside a redirection: 2>&1, >&2, &>
                if (depth == 0 && c == "&" && substr(cmd, i - 1, 1) != ">" && substr(cmd, i + 1, 1) != ">") break
                if (c == "#" && substr(cmd, i - 1, 1) ~ /[ \t]/) break
                plain = plain c
            } else if (c == q) {
                q = ""
            }
            seg = seg c
        }
        if (plain !~ /(^|[ \t])(-p|--print)([ \t]|$)/) continue
        runs++
        hit = 0
        if (plain ~ /<[ \t]*\/dev\/null/ || plain ~ /--no-stdin/ || plain ~ /<</) hit = 1
        if (plain ~ /(^|[ \t])0?<[ \t]*[^<& \t]/) hit = 1
        if (plain ~ /(-p|--print)[ \t]+-([ \t]|$)/) hit = 1
        if (pre ~ /\|/) hit = 1
        if (!hit) printf "BAD %s:%d: %s\n", F, ln, substr(cmd, 1, 140)
    }
}
BEGIN { inb = 0; shell = 0; acc = ""; start = 0; runs = 0 }
/^[ \t]*```/ {
    if (!inb) {
        lang = $0; sub(/^[ \t]*```+[ \t]*/, "", lang); sub(/[ \t].*$/, "", lang)
        inb = 1; shell = (lang == "" || lang ~ /^(sh|bash|shell|console|zsh|sh-session)$/)
    } else { if (acc != "") check(acc, start); inb = 0; acc = ""; cont = 0 }
    next
}
inb && shell {
    line = $0
    if (acc == "") { if (line ~ /^[ \t]*#/) next; start = NR }
    if (line ~ /\\[ \t]*$/) { sub(/\\[ \t]*$/, " ", line); acc = acc line; next }
    acc = acc line
    # a quoted prompt may span lines with no backslash: keep joining while a quote is open
    if (qstate(acc) != "" && ++cont < 20) { acc = acc " "; next }
    check(acc, start); acc = ""; cont = 0
}
END { printf "RUNS %d\n", runs }
' "$f"
done)

runs=$(printf '%s\n' "$out" | awk '/^RUNS /{n += $2} END{print n + 0}')
bad=$(printf '%s\n' "$out" | grep '^BAD ' | sed 's/^BAD //')
nbad=$(printf '%s\n' "$bad" | grep -c .)

if [ "$runs" -ge "$JC_STDIN_FLOOR" ]; then
    t_ok "the headless runs read are the pages' ($runs runs, floor $JC_STDIN_FLOOR)"
else
    t_fail "only $runs headless runs extracted from living pages (floor $JC_STDIN_FLOOR) -- the parser stopped finding them"
fi
if [ "$nbad" -eq 0 ]; then
    t_ok "every headless run on a living page pins its stdin"
else
    t_fail "$nbad headless run(s) leave stdin open (add \`< /dev/null\`): $(printf '%s\n' "$bad" | head -5)"
fi

t_done
