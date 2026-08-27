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

t_plan 3
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

t_done
