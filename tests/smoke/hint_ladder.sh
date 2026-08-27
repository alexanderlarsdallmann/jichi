#!/bin/sh
# smoke: every written hint is a rung jichi actually serves (M409).
#
# THE DEFECT THIS EXISTS FOR. jc_yaml's subset mis-parsed a sequence item whose
# value contains a colon -- `- "Run it first: cd ..."` became a MAPPING node, and
# jc_assign (M289, deliberately) skips items with no readable scalar. So a colon
# inside a hint SILENTLY DELETED it from the ladder, and nobody could see it:
# "the ladder has 2 rungs" reads as a fact about the assignment, not a parse
# failure. Found by driving jichi on another repository (zigodot, 2026-08-12),
# where an authored spec wrote 3 hints and `jichi hint` served none; measured
# back here, jichi's OWN curriculum was truncated -- 00-hello served 2 of 3,
# 22-slope-lies-keep-the-peak 1 of 3, 39-elixir-make-it-pass 1 of 3.
#
# GROUND TRUTH IS THE BINARY, not a re-implementation of the parser: for every
# spec, count the written `- ` lines under `hints:`, then ask `jichi hint` for
# rung 99 and read the ladder size out of its own error message. If the two
# numbers differ, a learner is being served less than the author wrote.
#
# Runs $BIN (hence not *_lint.sh). cwd is an isolated tmp dir so any progress
# recording lands there, never in the repository.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
cd "$tmp" || exit 1

served_rungs() {
    # `hint <spec> 99` answers one of:
    #   "hint: N must be 1..K (the ladder has K rungs)"  -> K
    #   "(this assignment carries no hints)"             -> 0
    #   "Hint 99 of 99:" cannot happen (no 99-rung ladder here; floor guards it)
    _sr_out=$(with_deadline 30 "$BIN" hint "$1" 99 < /dev/null 2>&1)
    case "$_sr_out" in
        *"carries no hints"*) echo 0 ;;
        *"the ladder has "*)
            echo "$_sr_out" | sed -n 's/.*the ladder has \([0-9][0-9]*\) rung.*/\1/p' | head -1 ;;
        *) echo "-1" ;;
    esac
}

# --- 1: written == served, across the whole graded corpus (en + de) ----------
: > "$tmp/short"
nspec=0
nladder=0
for f in "$SMOKE_ROOT"/docs/assignments/*.md \
         "$SMOKE_ROOT"/docs/i18n/de/assignments/*.md; do
    [ -f "$f" ] || continue
    case "$f" in *.solution.md|*INDEX.md) continue ;; esac
    nspec=$((nspec + 1))
    written=$(awk '
        /^hints:/ { inh = 1; next }
        inh && /^[ \t]+-[ \t]/ { n++; next }
        inh && /^[a-zA-Z_-]+:/ { inh = 0 }
        inh && /^---/ { inh = 0 }
        END { print n + 0 }
    ' "$f")
    [ "$written" -eq 0 ] && continue
    nladder=$((nladder + 1))
    served=$(served_rungs "$f")
    if [ "$served" != "$written" ]; then
        echo "$(basename "$f"): wrote $written, serves $served" >> "$tmp/short"
    fi
done
nshort=$(grep -c . "$tmp/short" || true)
if [ "$nspec" -lt 60 ] || [ "$nladder" -lt 40 ]; then
    t_fail "scanned only $nspec specs / $nladder ladders -- the extraction broke; fix it, not the floor"
elif [ "$nshort" -eq 0 ]; then
    t_ok "all $nladder hint ladders serve exactly what was written ($nspec specs scanned)"
else
    t_fail "$nshort of $nladder ladders are silently short: $(tr '\n' '; ' < "$tmp/short" | head_bytes 220)"
fi

# --- 2: a hint jichi cannot read is SAID, not silently dropped --------------
# The M289 skip stays (an unreadable entry must not become an empty rung), but
# it now counts what it skipped and `hint` names the gap -- otherwise the next
# unparseable form re-creates this defect invisibly.
cat > "$tmp/unread.md" <<'EOF'
---
title: unreadable rung fixture
verify: "true"
points: 1
hints:
  - a readable hint
  - an unquoted hint with a colon: still unreadable by the subset
---
body
EOF
out=$(with_deadline 30 "$BIN" hint "$tmp/unread.md" 1 < /dev/null 2>&1)
case "$out" in
    *"could not be read"*)
        t_ok "hint names the unreadable rung instead of hiding it" ;;
    *)
        t_fail "an unparseable hint was dropped without a word: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)" ;;
esac

# --- 2b (M618): the BRIEF says it too, on every surface at once ----------------
# run_hint's stderr note (M409) reached only the CLI hint; `assign`, the TUI
# /assignment and the `attempt` brief rendered "N hint(s) available" with the
# shortfall unsaid. The note now lives in jc_assign_render itself -- one place,
# all four surfaces.
out=$(with_deadline 30 "$BIN" assign "$tmp/unread.md" < /dev/null 2>&1)
case "$out" in
    *"could not be read"*)
        t_ok "the rendered brief names the unreadable rung (assign/TUI/attempt share it)" ;;
    *)
        t_fail "assign rendered a silently short ladder: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)" ;;
esac

# --- 3: the quoted-colon form -- the one authors actually write -- parses ----
# This is the exact shape from the zigodot finding and from this repo's own
# curriculum ("Run it first: ..."). It must be a rung, byte-for-byte.
cat > "$tmp/colon.md" <<'EOF'
---
title: quoted colon fixture
verify: "true"
points: 1
hints:
  - "Run it first: read which check failed and the input it used."
---
body
EOF
out=$(with_deadline 30 "$BIN" hint "$tmp/colon.md" 1 < /dev/null 2>&1)
case "$out" in
    *"Run it first: read which check failed"*)
        t_ok "a quoted hint containing a colon is served verbatim" ;;
    *)
        t_fail "the quoted-colon hint did not survive: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)" ;;
esac

t_done
