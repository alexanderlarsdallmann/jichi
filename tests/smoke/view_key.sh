#!/bin/sh
# smoke: the view key shows the call, not its JSON encoding (M571).
#
# WHAT THE OPERATOR MET. `v`/`5` did `printf("\n%s\n", args)` for every tool --
# the raw argument string. On apply_patch that is:
#
#   {"edits": [{"path": "...", "old_string": "static void greet(const char
#   *who)\n{\n    printf(\"hello, %s\\n\", who);\n}", "new_string": "..."}]}
#
# Spoken: braces, quotes, "backslash n", and escaped escapes. The key advertised
# as "view" -- the one that exists so a person can understand a change before
# authorising it -- was the least readable thing in the session. It is also the
# key M564 had just promoted to a first-class digit, so it was being advertised
# more loudly than before while showing this.
#
# WHAT IT SHOWS NOW: one field per line, values DECODED (cJSON has already
# turned \n into a real newline), arrays announced by item count and then walked.
# The values are CONTENT and pass through untouched -- the operator's rule,
# "within program code all symbols are important, and must be read" -- so only
# the structure is reworded. Each value is bounded at VIEW_VALUE_MAX with a
# marker naming the true size, so nothing is silently hidden.
#
# NOT the diff, deliberately: edit_file and apply_patch both render a diff
# preview above the prompt already, so re-rendering it here would repeat what
# the listener just heard. View's job is the whole call.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The exact shape from the report: a nested edits[] whose strings carry newlines
# and escaped quotes.
cat > "$tmp/m.mm" <<'EOF'
wire openai
rule
  count 1
  tool apply_patch {"edits":[{"path":"greet.c","old_string":"static void greet(const char *who)\n{\n    printf(\"hello\");\n}","new_string":"static void say_hello(const char *who)\n{\n    printf(\"hello\");\n}"}]}
rule
  text ALL_DONE
EOF

cat > "$tmp/p.pd" <<'EOF'
expect "> " 15
send "rename greet\r"
expect "Allow?" 25
delay 700
send "5"
delay 1800
send "0"
delay 2000
send "/exit\r"
waitexit 20
EOF

printf 'static void greet(const char *who)\n{\n    printf("hello");\n}\n' \
    > "$ws/greet.c"
mm_start "$tmp/m.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 80 "$SMOKE_TOOLS/ptydrive" --deadline 75 --cols 100 \
    --log "$tmp/v.log" "$tmp/p.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1) || true
mm_stop
tr '\r' '\n' < "$tmp/v.log" > "$tmp/v.txt"

# ---- 1: the denominator -- the prompt was reached and 5 was accepted -------
# Checks 2-4 are absence assertions about the view output; on a capture where
# the prompt never appeared, or where 5 was swallowed, every one holds trivially.
if $G -q 'Allow?' "$tmp/v.txt" && $G -q 'greet.c' "$tmp/v.txt"; then
    t_ok "the prompt was reached and the call names its target"
else
    t_fail "no prompt, or no view output -- nothing below tests anything. Tail: \
$(tail -3 "$tmp/v.txt" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 2: the JSON ENCODING is gone from what a listener hears --------------
# Three markers, because each is a different kind of noise: a brace is
# structure, an escaped quote is an escape of an escape, and `old_string` is a
# schema field name no user asked about.
if ! $G -q '{"edits"' "$tmp/v.txt" && ! $G -q '\\"' "$tmp/v.txt"; then
    t_ok "no raw JSON encoding in the session output"
else
    t_fail "the view key still prints encoded JSON. Braces: \
$($G -c '{"edits"' "$tmp/v.txt"), escaped quotes: $($G -c '\\"' "$tmp/v.txt"). \
print_args_readable must decode the values and name the fields."
fi

# ---- 3: and the CONTENT survived, decoded ---------------------------------
# The property, not the absence. A build that printed nothing at all would pass
# check 2. The old code's `\n` must appear as a real line break, so the two
# function signatures land on their own lines.
if $G -qx 'static void greet(const char \*who)' "$tmp/v.txt" &&
   $G -qx 'static void say_hello(const char \*who)' "$tmp/v.txt"
then
    t_ok "both versions of the line appear decoded, one per line"
else
    t_fail "the content was not decoded onto its own lines -- \\n must become a \
real newline, since these values are code the user is being asked to approve. \
Lines seen: $($G -c 'static void' "$tmp/v.txt")"
fi

# ---- 4: the fields are NAMED, so a listener knows what they are hearing ---
if $G -q '^ *path:' "$tmp/v.txt" && $G -q 'old_string:' "$tmp/v.txt" &&
   $G -q 'item 1:' "$tmp/v.txt"
then
    t_ok "fields and array items are announced by name"
else
    t_fail "the view output does not name its fields, so two blocks of code \
arrive with nothing to distinguish them. path: $($G -c 'path:' "$tmp/v.txt"), \
old_string: $($G -c 'old_string:' "$tmp/v.txt"), item: $($G -c 'item 1:' "$tmp/v.txt")"
fi

# ---- 5: and 5 DECIDED NOTHING -- the prompt came back --------------------
# M564's contract for the view key. If viewing approved the call, the file would
# have changed; if it denied, there would be one prompt. Both are wrong.
if [ "$($G -c 'Allow?' "$tmp/v.txt")" -ge 2 ] &&
   $G -q 'static void greet' "$ws/greet.c"
then
    t_ok "viewing re-asked instead of deciding, and changed nothing"
else
    t_fail "the view key decided the call: prompts=$($G -c 'Allow?' "$tmp/v.txt") \
(want >= 2), file still original=$($G -c 'static void greet' "$ws/greet.c"). \
5 shows and hands the decision back -- it is not a yes and not a no."
fi

t_done
