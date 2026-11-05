#!/bin/sh
# smoke: the edit-scope fence reads arguments exactly as the executor (M981).
#
# THE DEFECT. Two bypasses opened because the fence and executor saw different
# argument trees:
# - Face one (B-04): 'src/../secret.txt' matched glob 'src/**' textually, but
#   the executor resolved it to 'secret.txt' at workspace root
# - Face two (A-01): self-named wrapper '{"write_file":{"path":"..."}}' had no
#   top-level 'path' (fence saw 'none', in scope); stringified edits
#   '{"edits":"[{...}]"}' was a string not an array (fence saw no edits)
#
# The fix: jc_tool_normalize_args reproduces the executor's exact sequence
# (parse, repair, unwrap, unstring) and both fence and executor use it.

. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

# --- 1: control -- write_file inside scope must work ------------------------
control() {
    ws=$(smoke_tmp); mkdir -p "$ws/src"
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool write_file {"path":"src/a.txt","content":"hello"}
rule
  text done
EOF
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT" '"editScope":["src/**"]'
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --auto -p "write src/a.txt" < /dev/null) > "$tmp/out" 2>&1
    mm_stop
    if [ -f "$ws/src/a.txt" ]; then
        t_ok "control: write_file inside scope works"
    else
        t_fail "control failed: write_file did not write src/a.txt"
    fi
}

# --- 2: face one -- path with '..' must be refused --------------------------
face_one_dotdot() {
    ws=$(smoke_tmp); mkdir -p "$ws/src"
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool write_file {"path":"src/../secret.txt","content":"bad"}
rule
  text done
EOF
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT" '"editScope":["src/**"]'
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --auto -p "write secret.txt" < /dev/null) > "$tmp/out" 2>&1
    mm_stop
    # The file must NOT exist
    if [ ! -f "$ws/secret.txt" ]; then
        t_ok "face one: path with '..' is refused (secret.txt not created)"
    else
        t_fail "face one: path with '..' should have been refused but secret.txt was created"
    fi
}

# --- 3: face two wrapper -- self-named arguments must be refused ------------
face_two_wrapper() {
    ws=$(smoke_tmp); mkdir -p "$ws/src"
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool write_file {"write_file":{"path":"src/../secret2.txt","content":"bad"}}
rule
  text done
EOF
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT" '"editScope":["src/**"]'
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --auto -p "write secret2.txt" < /dev/null) > "$tmp/out" 2>&1
    mm_stop
    # The file must NOT exist
    if [ ! -f "$ws/secret2.txt" ]; then
        t_ok "face two wrapper: self-named args are refused (secret2.txt not created)"
    else
        t_fail "face two wrapper: self-named args should have been refused but secret2.txt was created"
    fi
}

# --- 4: face two stringified -- stringified edits must be refused -----------
face_two_stringified() {
    ws=$(smoke_tmp); mkdir -p "$ws/src"
    # Seed the target file with content 'a' BEFORE the mock starts
    printf 'a\n' > "$ws/secret3.txt"
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool read_file {"path":"secret3.txt"}
rule
  count 2
  tool apply_patch {"edits":"[{\"path\":\"secret3.txt\",\"old_string\":\"a\",\"new_string\":\"b\",\"replace_all\":true}]"}
rule
  text done
EOF
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT" '"editScope":["src/**"]'
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --auto -p "patch secret3.txt" < /dev/null) > "$tmp/out" 2>&1
    mm_stop
    # The file must STILL contain 'a' (patch was refused by fence)
    # Post-fix: fence unstrings edits, sees secret3.txt, out of scope -> refuses
    # Pre-fix: fence sees edits as a string (no paths) -> in scope -> lets write through -> becomes 'b'
    # Guarded grep (portability_lint 36): a system without the merged /usr
    # (Debian 9) has no /usr/bin/grep; the bare path would make the condition
    # false and print t_fail -- a false red on that platform, not a green.
    G=/usr/bin/grep
    [ -x "$G" ] || G=grep
    if [ -f "$ws/secret3.txt" ] && "$G" -q '^a$' "$ws/secret3.txt"; then
        t_ok "face two stringified: stringified edits are refused (patch not applied)"
    else
        t_fail "face two stringified: stringified edits should have been refused but file content changed"
    fi
}

# Run all tests
control
face_one_dotdot
face_two_wrapper
face_two_stringified

t_done
