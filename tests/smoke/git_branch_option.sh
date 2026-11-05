#!/bin/sh
# smoke: B-03 / M983 - branch_run refuses branch names beginning with '-'

. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

WS=$(smoke_tmp)
CAP=$(smoke_tmp)
CAP2=$(smoke_tmp)

# Create a test repo with an initial commit
git -C "$WS" init -q
git -C "$WS" config user.email "t@t"
git -C "$WS" config user.name "t"
printf 'seed\n' > "$WS/seed.txt"
git -C "$WS" add seed.txt
git -C "$WS" commit -qm seed

# --- 1: a name beginning with '-' is refused ---------------------------------
cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool git_branch {"name":"--force","create":true}
rule
  match "\"role\":\"tool\""
  text GUARD_OK
EOF

mm_start "$CAP/r.mm" "$CAP" 2
write_config "$tmp/config.json" "$MM_PORT"
cd "$WS"
out=$(with_deadline 60 "$BIN" --config "$tmp/config.json" -q --auto \
    -p "Create a branch" < /dev/null); rc=$?
mm_stop

# Check 1a: the tool result contains the refusal text
# The mock captures the tool result in CAP/req.* files; grep for the refusal text
if grep -h "must not begin with a dash" "$CAP/req."* 2>/dev/null | grep -c . >/dev/null; then
    t_ok "1a: git_branch refused a name beginning with '-'"
else
    t_fail "1a: git_branch did not refuse '-' prefix (red before M983: it would detach HEAD)"
fi

# Check 1b: the workspace is unchanged (HEAD is still on master, no --force branch)
if git -C "$WS" rev-parse --abbrev-ref HEAD 2>/dev/null | grep -q '^master$'; then
    t_ok "1b: HEAD stayed on master (not detached)"
else
    t_fail "1b: HEAD changed to $(git -C "$WS" rev-parse --abbrev-ref HEAD 2>/dev/null || echo 'unknown')"
fi

# --- 2: a normal branch name with hyphen in middle still works ---------------
cat > "$CAP/r2.mm" <<'EOF'
wire openai
rule
  count 1
  tool git_branch {"name":"plain-branch","create":true}
rule
  match "\"role\":\"tool\""
  text WORKS_OK
EOF

mm_start "$CAP/r2.mm" "$CAP2" 2
write_config "$tmp/config2.json" "$MM_PORT"
cd "$WS"
out2=$(with_deadline 60 "$BIN" --config "$tmp/config2.json" -q --auto \
    -p "Create plain-branch" < /dev/null); rc=$?
mm_stop

# Check 2a: the branch was created successfully
if git -C "$WS" show-ref --heads --verify "refs/heads/plain-branch" 2>/dev/null; then
    t_ok "2: plain-branch (hyphen in middle) was created"
else
    t_fail "2: plain-branch was not created (guard over-refused)"
fi

t_done
