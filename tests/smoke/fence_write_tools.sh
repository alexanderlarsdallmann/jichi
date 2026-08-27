#!/bin/sh
# smoke: every tool that WRITES is a tool the edit-scope fence knows (M535).
#
# THE DEFECT. Two lists answered "which tools write?" and they disagreed:
# `jc_agent_tool_category` counted six (write_file, edit_file, apply_patch,
# rename_symbol, format_file, apply_code_action) while `call_out_of_scope_path`
# knew three. Measured: with an edit scope covering only `src`,
# `format_file {"path":"README.md"}` REWROTE README.md and the envelope reported
# "verified ok". M501 fixed exactly this drift in the sibling collector
# (jc_argpath_collect, which fails closed) and did not carry it here.
#
# Two write tools cannot be fenced on their arguments at all: `rename_symbol` and
# `apply_code_action` apply a language-server WorkspaceEdit whose paths are not in
# the call. Fencing them on their anchor `path` would check the one file we can
# see and imply safety for every file we cannot -- so while a scope is armed they
# are REFUSED, with the reason and the alternative named. Fail closed.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- format_file is fenced outside the scope and works inside it;
#                   the two WorkspaceEdit tools are refused while a scope is
#                   armed and not refused by this rule when none is; and that the
#                   two source lists still agree.
#   NOT checked  -- the WorkspaceEdit refusal itself, in a run. `rename_symbol`
#                   and `apply_code_action` are only REGISTERED when
#                   `lsp_servers` is non-empty (jc_tool.c), so this tier cannot
#                   reach them without standing up a language server; check 3
#                   covers that they are named in the loop at all. The first
#                   version of this driver tried to drive them and its failure is
#                   what established why it cannot.
#   NOT checked  -- whether the language server's edits are individually inside
#                   the scope. That is the fix the refusal stands in for, and it
#                   needs the LSP layer to check each edit as it applies it.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

# The formatter is a written script rather than an inline `sed -i`: BSD sed
# requires a backup suffix for -i (posix_utils_lint check 3), and a script also
# avoids nesting quotes inside a JSON string inside a heredoc.
printf '#!/bin/sh\nprintf REFORMATTED > "$1"\n' > "$tmp/fmt.sh"
chmod +x "$tmp/fmt.sh"

run_fmt() {   # run_fmt <path-arg> <scope-json> -> CASE_WS
    ws=$(smoke_tmp); mkdir -p "$ws/src"
    printf 'ORIGINAL\n' > "$ws/README.md"
    printf 'ORIGINAL\n' > "$ws/src/a.txt"
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool format_file {"path":"$1"}
rule
  text done
EOF
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT" \
      "\"editScope\":$2,\"formatCommand\":\"$tmp/fmt.sh\""
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --auto -p "format it" < /dev/null) > "$tmp/out" 2>&1
    mm_stop
    CASE_WS="$ws"
}

# --- 1: format_file outside the scope must be refused -----------------------
run_fmt README.md '["src/**"]'
if grep -q REFORMATTED "$CASE_WS/README.md"; then
    t_fail "format_file rewrote README.md while the edit scope covered only src --
 the fence knows three write tools and the event layer counts six"
else
    t_ok "format_file is fenced outside the edit scope"
fi

# --- 2: control -- inside the scope it must still work ----------------------
run_fmt src/a.txt '["src/**"]'
if grep -q REFORMATTED "$CASE_WS/src/a.txt"; then
    t_ok "control: format_file still works inside the scope"
else
    t_fail "control failed: format_file did not run inside its own scope, so
 check 1 may pass because formatting is broken: $(tail -c 200 "$tmp/out")"
fi

# --- 3: and the two source lists still agree -------------------------------
# The durable half. If a seventh write tool is added to the event layer and not to
# the fence, this fails here rather than in somebody's repository.
# The write set: the lines from the branch's first comparison through its
# `return JC_TOOLCAT_WRITE`. The first version of this used an awk that began
# printing at the first sight of JC_TOOLCAT_WRITE -- which is the RETURN line, so
# it captured almost nothing and the check compared a two-element set against a
# superset and passed trivially. A drift check that was itself too narrow, which
# is the exact failure it exists to catch (docs/TEST_INTEGRITY.md).
writes=$(sed -n '/strcmp(name, "write_file")/,/return JC_TOOLCAT_WRITE/p' \
         "$SMOKE_ROOT/src/util/jc_agentjson.c" \
         | grep -o '"[a-z_]*"' | tr -d '"' | sort -u | tr '\n' ' ')
# Not vacuous: the branch names six tools and this must find all of them.
if [ "$(printf '%s' "$writes" | wc -w)" -lt 6 ]; then
    t_fail "the write-set extraction found only $(printf '%s' "$writes" | wc -w)
 name(s) [$writes] where the branch names six -- fix the extraction, do not lower
 the floor"
    t_fail -
    t_done
fi
# The agent loop names a tool through `name` (inside the fence helper) or
# `gate_name` (every gate since M532). Searching only the first is how the first
# version of this check reported two tools that are in fact handled.
fence=$(grep -oE 'strcmp\((name|gate_name), "[a-z_]*"\)' \
        "$SMOKE_ROOT/src/chat/jc_agent.c" | grep -o '"[a-z_]*"' | tr -d '"' \
        | sort -u | tr '\n' ' ')
missing=""
for w in $writes; do
    case " $fence " in *" $w "*) ;; *) missing="$missing $w" ;; esac
done
if [ -z "$missing" ]; then
    t_ok "every write tool the event layer names is named in the agent loop too"
else
    t_fail "write tool(s) the event layer counts but the agent loop never names:
$missing
 Either fence it, or refuse it as the WorkspaceEdit tools are refused."
fi

t_done
