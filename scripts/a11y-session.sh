#!/bin/sh
# a11y-session.sh -- prepare a throwaway workspace for the manual screen-reader
# protocol in docs/ACCESSIBILITY.md, and report what this host still needs (M500).
#
# WHY A SCRIPT. The protocol is a LISTENING exercise, and the twenty minutes it
# takes should be spent listening, not assembling a config, deciding which model
# to point at, or wondering whether a workspace is safe to let an agent edit.
# This does the assembling; it changes nothing outside its own temp directory and
# nothing about your desktop session.
#
# WHAT IT DELIBERATELY DOES NOT DO: enable accessibility, start a screen reader,
# install a package, or load a kernel module. Those touch a live session or need
# root, so they are PRINTED for you to run. A script that reconfigures someone's
# desktop without asking is the opposite of an accessibility tool.
#
# usage: scripts/a11y-session.sh [--check]
#        --check   report readiness only; create nothing
set -e

ME=a11y-session
CHECK=no
[ "$1" = "--check" ] && CHECK=yes

say() { printf '%s\n' "$*"; }
have() { command -v "$1" > /dev/null 2>&1; }

say "== what this host has =="
for t in orca spd-say speech-dispatcher brltty espeakup fenrir gnome-terminal; do
    if have "$t"; then say "  yes  $t"; else say "  NO   $t"; fi
done
if [ -d /sys/module/speakup_soft ]; then
    say "  yes  speakup_soft (loaded)"
elif modinfo speakup_soft > /dev/null 2>&1; then
    say "  yes  speakup_soft (available, not loaded)"
else
    say "  NO   speakup_soft"
fi
_tk=$(gsettings get org.gnome.desktop.interface toolkit-accessibility 2>/dev/null \
      || echo "unknown")
say "  toolkit-accessibility = $_tk"
say ""

say "== the desktop path (Orca in your GNOME session) =="
say "  Orca needs the toolkit switch on, and GTK apps read it at startup, so"
say "  reopen the terminal afterwards. Reversible: set it back to false."
say ""
say "    gsettings set org.gnome.desktop.interface toolkit-accessibility true"
say "    orca --replace &"
say "    # then open a NEW gnome-terminal (xterm is not accessible)"
say ""
say "== the console path (speakup + espeakup, no desktop involved) =="
say "  The protocol calls this the more representative path: a Linux virtual"
say "  console, no terminal emulator between jichi and the reader."
say ""
say "    sudo apt install espeakup"
say "    sudo modprobe speakup_soft"
say "    sudo systemctl start espeakup"
say "    # then switch to a text console with Ctrl-Alt-F3 and log in"
say ""

[ "$CHECK" = yes ] && exit 0

# ---- the throwaway workspace ------------------------------------------------
WS=$(mktemp -d "${TMPDIR:-/tmp}/jichi_a11y.XXXXXX")
mkdir -p "$WS/src"
cat > "$WS/src/greet.c" <<'EOS'
#include <stdio.h>

/* A deliberately small file: step 1 of the protocol reads it, step 3 asks for a
 * code answer about it, and step 5's approval prompt offers to edit it. */
int main(void)
{
    printf("hello\n");
    return 0;
}
EOS
cat > "$WS/README.md" <<'EOS'
# a11y listening workspace

Throwaway. Nothing here matters; let the agent edit it freely.
EOS

# The key comes from the environment, or from a file the caller NAMES. Not from a
# path baked in here: snapshot_lint refuses a real account name in the publishable
# tree, and it was right to -- the first draft of this script hardcoded one.
KEY="${JICHI_A11Y_KEY:-}"
# M543: a config you already have wins over the default. The default names the
# institutional gateway, which needs a key -- and requiring a key to verify
# ACCESSIBILITY excludes exactly the tester who has a screen reader and no
# institutional account. A local OpenAI-compatible server (LM Studio, llama.cpp)
# needs no real key, so pointing this at one is the shortest path to a session:
#
#   JICHI_A11Y_CONFIG=~/my-local.json scripts/a11y-session.sh
#
if [ -n "${JICHI_A11Y_CONFIG:-}" ] && [ -r "${JICHI_A11Y_CONFIG}" ]; then
    cp "${JICHI_A11Y_CONFIG}" "$WS/config.json"
    KEY=""            # a local endpoint needs none; the note below stays quiet
    say "  config    : copied from ${JICHI_A11Y_CONFIG}"
else
cat > "$WS/config.json" <<'EOS'
{
  "models": [
    { "name": "chat", "provider": "openai", "model": "jlu/qwen3-coder-next",
      "apiBase": "https://api.hrz.uni-giessen.de/v1",
      "apiKeyEnv": "JICHI_API_KEY", "contextLength": 196608,
      "roles": ["chat"] }
  ],
  "snapshots": true, "repoMap": false, "references": false,
  "toolProfile": "full", "maxRetries": 1
}
EOS
fi
cat > "$WS/run.sh" <<EOS
#!/bin/sh
# Start the session the protocol describes. CHAT mode on purpose: step 5 needs a
# real approval prompt, which --auto would skip.
cd "$WS"
if [ -z "\$JICHI_API_KEY" ] && [ -n "$KEY" ] && [ -r "$KEY" ]; then
    JICHI_API_KEY=\$(tr -d ' \\t\\n\\r' < "$KEY"); export JICHI_API_KEY
fi
exec jichi --config "$WS/config.json" --accessible "\$@"
EOS
chmod +x "$WS/run.sh"

if [ -z "$KEY" ] && [ -z "$JICHI_API_KEY" ] && [ -z "${JICHI_A11Y_CONFIG:-}" ]; then
    say "  NOTE: no key. Either export JICHI_API_KEY before starting, or set"
    say "  JICHI_A11Y_KEY=/path/to/keyfile and re-run this script."
    say ""
fi

say "== ready =="
say "  workspace : $WS"
say "  start     : $WS/run.sh"
say ""
say "  Step 6 of the protocol wants the SAME session without --accessible, to"
say "  hear the difference. There is no --no-accessible; run jichi without the"
say "  flag instead:"
say ""
say "    cd $WS && jichi --config $WS/config.json"
say ""
say "  The sheet to hold open while listening:"
say "    docs/ACCESSIBILITY.md  ->  'The manual screen-reader test protocol'"
