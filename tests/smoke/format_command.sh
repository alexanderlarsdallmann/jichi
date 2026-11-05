#!/bin/sh
# smoke: the formatCommand backend for format_file (M263).
#
# Languages with no LSP formatter had no formatting path at all. `formatCommand`
# is a shell string that rewrites a file in place, used when no language server
# formats it. Three assertions:
#
#   1. it runs: with formatCommand set and NO lspServers, format_file rewrites
#      the file (the fake formatter uppercases it) and reports success;
#   2. it is advertised: the tool reaches the model when only formatCommand is
#      configured -- previously format_file existed only with lspServers, so a
#      formatCommand-only project could not have called it;
#   3. it is injection-proof: a file whose NAME contains shell metacharacters is
#      formatted, and the metacharacters do not execute. The path comes from the
#      model, so this is the property that makes a shell backend safe at all.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A fake formatter: uppercase the file in place. Takes the path as its last
# argument, like clang-format -i.
cat > "$tmp/fakefmt.sh" <<'EOF'
#!/bin/sh
f="$1"
tr 'a-z' 'A-Z' < "$f" > "$f.tmp" && mv "$f.tmp" "$f"
echo "formatted $f"
EOF
chmod +x "$tmp/fakefmt.sh"

printf 'hello world\n' > "$ws/a.txt"
# A filename that is also a shell injection attempt. If the path is not quoted,
# `id > pwned` runs and the sentinel file appears.
printf 'boom\n' > "$ws/x; id > pwned; echo .txt"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool format_file {"path":"a.txt"}
rule
  count 2
  tool format_file {"path":"x; id > pwned; echo .txt"}
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" \
    "\"formatCommand\":\"$tmp/fakefmt.sh\""

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto \
    --no-session -q -p "format the files" < /dev/null > "$tmp/out" 2>&1)
mm_stop

if [ "$(cat "$ws/a.txt")" = "HELLO WORLD" ]; then
    t_ok "formatCommand rewrote the file (no language server configured)"
else
    t_fail "file not formatted: $(cat "$ws/a.txt")"
fi

# Vacuity guard: the run must actually have advertised format_file, else
# assertion 1 could pass for the wrong reason on a future refactor.
if grep -q '"format_file"' "$tmp/req.1"; then
    t_ok "format_file is advertised with formatCommand and no lspServers"
else
    t_fail "format_file was not in the advertised tool array"
fi

if [ -f "$ws/pwned" ]; then
    t_fail "shell injection via the filename EXECUTED -- path was not quoted"
elif [ "$(cat "$ws/x; id > pwned; echo .txt")" = "BOOM" ]; then
    t_ok "a filename full of shell metacharacters is formatted, not executed"
else
    t_fail "the metacharacter filename was not formatted: $(cat "$ws/x; id > pwned; echo .txt" 2>/dev/null)"
fi

# --- M847 (S1): a {} inside the author's quotes is refused, file untouched -----
ws2=$(smoke_tmp)
printf 'keep me\n' > "$ws2/q.txt"
cat > "$tmp/replies2.mm" <<EOF
wire openai
rule
  count 1
  tool format_file {"path":"q.txt"}
rule
  text DONE
EOF
mm_start "$tmp/replies2.mm" "$tmp/cap2"
# {} sits inside the -c script's single quotes AND a double-quoted word: the
# shape the R/elisp packs and FORMATTING.md once used. It must be refused, not
# run with a broken-out path.
write_config "$tmp/config2.json" "$MM_PORT" \
    "\"formatCommand\":\"sh -c 'printf FMT >> \\\"{}\\\"'\""
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/config2.json" --auto \
    --no-session -q -p "format it" < /dev/null > "$tmp/out2" 2>&1)
mm_stop
# The refusal is returned to the model as the tool result, so it rides in the
# mock's capture of the follow-up request (req.2), not on stdout under -q.
if [ "$(cat "$ws2/q.txt")" = "keep me" ] \
   && grep -qi 'inside quotes' "$tmp/cap2/req.2" 2>/dev/null; then
    t_ok "a formatCommand with {} inside quotes is refused, naming the fix"
else
    t_fail "inside-quotes {} was not refused cleanly: file=$(cat "$ws2/q.txt") req2=$(tail -c 200 "$tmp/cap2/req.2" 2>/dev/null)"
fi

# --- M847: {} OUTSIDE quotes formats a filename with a SPACE correctly ----------
ws3=$(smoke_tmp)
printf 'lower\n' > "$ws3/my doc.txt"
cat > "$tmp/replies3.mm" <<EOF
wire openai
rule
  count 1
  tool format_file {"path":"my doc.txt"}
rule
  text DONE
EOF
mm_start "$tmp/replies3.mm" "$tmp/cap3"
write_config "$tmp/config3.json" "$MM_PORT" \
    "\"formatCommand\":\"$tmp/fakefmt.sh {}\""
(cd "$ws3" && with_deadline 60 "$BIN" --config "$tmp/config3.json" --auto \
    --no-session -q -p "format it" < /dev/null > "$tmp/out3" 2>&1)
mm_stop
if [ "$(cat "$ws3/my doc.txt" 2>/dev/null)" = "LOWER" ] \
   && [ ! -e "$ws3/my" ] && [ ! -e "$ws3/doc.txt" ]; then
    t_ok "{} outside quotes formats a spaced filename as one argument"
else
    t_fail "spaced filename via {} outside quotes was mishandled:
 my doc.txt=$(cat "$ws3/my doc.txt" 2>/dev/null) stray(my)=$([ -e "$ws3/my" ] && echo yes)"
fi

t_done
