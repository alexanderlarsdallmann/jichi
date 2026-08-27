#!/bin/sh
# smoke: list_files takes a pattern, and `glob` is a real alias now (M324).
#
# Measured motivation: in one 13,783-tool-call workload the model invented
# `glob` 46 times and NEVER once succeeded -- the most-invented name in the
# corpus -- beside 7,761 run_terminal_command calls (56% of every tool call).
# It wanted pattern-based file finding, jichi had none, so it shelled out.
#
# `glob` used to be hint-only for a good reason: its `pattern` argument did not
# fit list_files' `path`, so a transparent alias would have failed validation
# and taught nothing. M324 fixed the objection instead of arguing with it --
# list_files gained an optional `pattern`, so the schemas match and glob can
# resolve.
#
# The checks below are about semantics and bounds, because a recursive walk
# driven by a model's guess is the part that can go wrong quietly.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/src/deep" "$ws/docs" "$ws/.git/objects"
: > "$ws/src/a.c"
: > "$ws/src/b.c"
: > "$ws/src/deep/c.c"
: > "$ws/docs/x.md"
: > "$ws/top.c"
# A .c inside .git: it must never appear. Nobody globbing sources wants git's
# object store, and including it would burn the entry budget first.
: > "$ws/.git/objects/hidden.c"

# run_glob PATTERN -> the tool result's preview, newlines flattened to spaces
run_glob() {
    cat > "$tmp/g.mm" <<EOF
wire openai
rule
  count 1
  tool glob {"pattern":"$1"}
rule
  match "\\"role\\":\\"tool\\""
  text GLOB_DONE
EOF
    mm_start "$tmp/g.mm" "$tmp/cap" 2
    write_config "$tmp/c.json" "$MM_PORT"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
        --no-session --output jsonl -p "find" < /dev/null) 2>/dev/null | \
        sed -n 's/.*"type":"tool_result"[^}]*"preview":"\([^"]*\)".*/\1/p'
    mm_stop
}

# --- 1: it resolves at all, and finds recursively -----------------------------
r=$(run_glob '**/*.c')
# NOT merely "non-empty": `error: unknown tool 'glob'` is non-empty too, and a
# first draft of this check passed with the alias reverted. Require a real match.
case "$r" in
    *unknown\ tool*) t_fail "glob is still not a resolvable tool name: $r" ;;
    *a.c*)           t_ok "glob resolves to list_files and returns matches" ;;
    *)               t_fail "glob returned no file paths: $r" ;;
esac

# --- 2: ** crosses path segments ---------------------------------------------
case "$r" in
    *deep/c.c*) t_ok "'**' crosses segments (found the nested file)" ;;
    *)          t_fail "'**' did not reach src/deep/c.c: $r" ;;
esac

# --- 3: .git is excluded -----------------------------------------------------
case "$r" in
    *hidden.c*) t_fail "a file inside .git was returned" ;;
    *)          t_ok ".git is excluded from the walk" ;;
esac

# --- 4: '*' does NOT cross segments (the distinction that makes ** mean
#        something). 'src/*.c' must find a.c and b.c but not deep/c.c.
r2=$(run_glob 'src/*.c')
case "$r2" in
    *deep*) t_fail "'src/*.c' crossed a segment into src/deep: $r2" ;;
    *a.c*)  t_ok "'*' stays within one segment" ;;
    *)      t_fail "'src/*.c' found nothing: $r2" ;;
esac

# --- 5: no match says so, rather than looking like an empty directory --------
r3=$(run_glob '**/*.nosuchext')
case "$r3" in
    *"no files match"*) t_ok "an empty result says no files matched" ;;
    *) t_fail "a non-matching pattern gave '$r3' -- ambiguous with an empty dir" ;;
esac

# --- 6: the plain listing is UNCHANGED --------------------------------------
# The pattern is additive; a call without one must behave exactly as before,
# including the trailing '/' on directories.
cat > "$tmp/p.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {}
rule
  match "\"role\":\"tool\""
  text LS_DONE
EOF
mm_start "$tmp/p.mm" "$tmp/cap2" 2
write_config "$tmp/c2.json" "$MM_PORT"
plain=$( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c2.json" --auto \
    --no-lite --no-session --output jsonl -p "list" < /dev/null) 2>/dev/null | \
    sed -n 's/.*"type":"tool_result"[^}]*"preview":"\([^"]*\)".*/\1/p' )
mm_stop
case "$plain" in
    *"src/"*) t_ok "a call with no pattern still lists entries, dirs marked" ;;
    *) t_fail "the plain listing changed: $plain" ;;
esac

# --- 7: the path fence now applies to list_files ----------------------------
# It consulted no fence at all before M324 -- tolerable for one level of names,
# not for a recursive walk. With the fence on, a path outside the workspace is
# refused (read intent, so referenceRoots would still be allowed).
cat > "$tmp/f.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {"path":"/etc"}
rule
  match "\"role\":\"tool\""
  text FENCE_DONE
EOF
mm_start "$tmp/f.mm" "$tmp/cap3" 2
write_config "$tmp/c3.json" "$MM_PORT" '"pathFence":true'
fenced=$( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c3.json" --auto \
    --no-lite --no-session --output jsonl -p "list etc" < /dev/null) 2>/dev/null | \
    sed -n 's/.*"type":"tool_result"\(.*\)/\1/p' )
mm_stop
case "$fenced" in
    *"path fence"*) t_ok "list_files honours the path fence" ;;
    *) t_fail "listing /etc was not refused with the fence on: $fenced" ;;
esac

t_done
