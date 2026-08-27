#!/bin/sh
# smoke: no terminal control byte from the model or a tool reaches stdout (M472).
#
# THE DEFECT THIS EXISTS FOR. A terminal is an INTERPRETER, not a display: some
# bytes are printed and some are commands. Model text reached jichi's stdout
# byte-for-byte, so an assistant message could carry:
#
#   ESC ] 52 ; c ; <base64> BEL   write the user's SYSTEM CLIPBOARD (xterm, kitty,
#                                 alacritty, foot, wezterm, iTerm2, tmux with
#                                 set-clipboard on). Plant a command, wait for the
#                                 next paste into a shell.
#   ESC ] 0 ; text BEL            set the window title
#   ESC [ 2 K                     erase the line jichi just printed -- in an agent,
#                                 that means hiding what it ran
#
# M363 had already decided this rule and written down why ("output-side paste
# injection"), but applied it at the PASTE chokepoint -- the input side. The rule
# now lives in jc_ctrl_display_safe and both sides call it.
#
# TWO PATHS, because there are two ways untrusted bytes reach the terminal and one
# test would have covered one of them:
#   1. the model's own text        -> jc_prov_emit_text -> the front-end's write
#   2. a TOOL RESULT's bytes       -> read_file over a file that contains them.
#      Path 2 needs no model cooperation at all: a file in the repo is enough,
#      which puts it in M300's untrusted-content class.
#
# THIS driver covers path 1. Path 2 has its own, because in headless mode
# hl_tool_result does `(void)result` -- it prints the tool NAME and status, never
# the bytes -- so path 2 exists only in the TUI's cb_tool_result and needs a PTY:
# tests/smoke/tui_tool_escapes.sh. That one asserts on the OSC 52 byte SEQUENCE
# rather than on "no ESC anywhere", because the TUI emits its own SGR colour and a
# blanket check there would be red by construction.
#
# NOT asserted, deliberately: the --output json/jsonl paths. cJSON escapes a
# control byte to the six characters backslash-u-0-0-1-b, so it is inert there,
# and stripping would cost a machine consumer fidelity about what actually
# arrived. Check 4 pins that exemption rather than leaving it implicit.
#
# See docs/analysis/2026-08-17-source-hardening-audit.md §H3.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Build the payload with \uXXXX -- the form a provider actually puts on the wire,
# and the form that survives being written by a shell script. A literal ESC in a
# driver is its own hazard (editors and pagers mangle it), and `printf '\033'`
# would be a GNU-ism away from portable.
BS=$(printf '\\')
E="${BS}u001b"
B="${BS}u0007"

{
  printf 'data: {"choices":[{"delta":{"content":"BEGIN'
  printf '%s]0;TITLE-HIJACK%s' "$E" "$B"
  printf '%s]52;c;cHduZWQ=%s' "$E" "$B"
  printf '%s[2KEND"}}]}\n\n' "$E"
  printf 'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
  printf 'data: [DONE]\n\n'
} > "$tmp/body.sse"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  sse-file $tmp/body.sse
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 6
write_config "$tmp/config.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --no-session -p hi < /dev/null 2>/dev/null)
mm_stop

# 1/2. No raw ESC and no raw BEL on stdout. Counted with od so the check does not
#      itself have to handle a control byte -- and so a failure prints a number
#      rather than something that reconfigures the terminal running the suite.
esc=$(printf '%s' "$out" | od -An -c | tr -s ' ' '\n' | grep -cx '033' || true)
# od -c renders BEL as the C escape "\a", NOT as "007" -- only bytes without a C
# escape print as octal. The first cut grepped for 007 and could therefore never
# match: with the strip neutered, check 1 went red and this one stayed green. The
# fail-proof is what caught it.
bel=$(printf '%s' "$out" | od -An -c | tr -s ' ' '\n' | grep -cx '\\a' || true)
if [ "$esc" = "0" ]; then
    t_ok "no ESC byte from model text reached stdout"
else
    t_fail "$esc raw ESC byte(s) from model text reached stdout"
fi
if [ "$bel" = "0" ]; then
    t_ok "no BEL byte from model text reached stdout"
else
    t_fail "$bel raw BEL byte(s) from model text reached stdout"
fi

# 3. The text SURVIVES minus the control bytes -- a strip that ate the content
#    would pass checks 1 and 2 while being useless.
if printf '%s' "$out" | grep -q 'BEGIN' && printf '%s' "$out" | grep -q 'END'; then
    t_ok "the surrounding text is preserved (only the control bytes went)"
else
    t_fail "content was lost, not just the control bytes: $(printf '%s' "$out" | head_bytes 120)"
fi

# 4. The json path keeps the bytes, ESCAPED. This pins the deliberate exemption:
#    a future "strip everywhere" change would break a machine consumer, and this
#    check is where it would be noticed.
{
  printf 'data: {"choices":[{"delta":{"content":"J%s]52;c;x%s"}}]}\n\n' "$E" "$B"
  printf 'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
  printf 'data: [DONE]\n\n'
} > "$tmp/body2.sse"
cat > "$tmp/replies2.mm" <<EOF
wire openai
rule
  sse-file $tmp/body2.sse
EOF
mm_start "$tmp/replies2.mm" "$tmp/cap2" 6
write_config "$tmp/config2.json" "$MM_PORT"
jout=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
       --no-session --output json -p hi < /dev/null 2>/dev/null)
mm_stop
jesc=$(printf '%s' "$jout" | od -An -c | tr -s ' ' '\n' | grep -cx '033' || true)
if [ "$jesc" = "0" ] && printf '%s' "$jout" | grep -q 'u001b'; then
    t_ok "--output json keeps the byte as an escaped \\u001b, not raw"
else
    t_fail "json path: raw ESC count=$jesc, escaped form present=$(printf '%s' "$jout" | grep -c 'u001b')"
fi

t_done
