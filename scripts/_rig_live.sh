# shellcheck shell=sh
# _rig_live.sh -- the ONE definition of the DRIVEN task. Source it.
#
# WHY THIS FILE EXISTS (M676). `PLATFORMS.md` carries a fourth verdict beyond
# Verified: **Driven** -- a row where jichi has actually called a model and run
# a tool, rather than merely passing gates that never open a socket. Three rigs
# implemented that step, and by the time the third existed the code had been
# copied twice. `_rig_ship.sh`'s header states the rule this file obeys: *a
# second rig is where drift starts, not the fourth.*
#
# Measured before extracting, because "they have drifted" is a claim: the three
# copies agreed on the TASK -- same two prompts, same nonce shape, same
# assertions -- and differed only in the row name inside filenames, two `note`
# lines the FreeBSD copy lacked, and one failure message reworded. So the
# decision `DEFERRED.md` item 6 asks for ("decide the minimum driven task first,
# so the rows are comparable") had already been taken in practice and written
# down nowhere. This file is where it is written down.
#
# ============================ THE DRIVEN TASK =============================
#
# TWO TURNS, because the first proves only the wire. `reply with OK` exercises
# the provider, the request and the SSE framing; it chooses no tool, executes
# none, and consumes no result. Every documented failure in this area lives past
# that point -- a model that DESCRIBES tool calls terminates cleanly with an
# empty workspace (AUTONOMOUS_LOOPS.md, "done is not a success verdict").
#
# THE ASSERTION IS A RANDOM PHRASE, NOT A SENTENCE. Quotes drift -- measured, a
# model quoted three passages and dropped an article from one -- but a token
# generated this second can only be produced by having read the file. That is
# what makes the second turn evidence rather than an impression.
#
# WHAT A PASS MEANS, stated so a row is not over-read: request build, SSE
# framing, a native tool call, tool execution, and a second turn that consumes
# the result. It does NOT mean the platform runs long sessions, survives
# compaction, or handles concurrency. It means the loop closed once, here.
#
# ======================= WHAT THE CALLER MUST PROVIDE =====================
#
# Shell functions, defined before the call (the same contract `jc_rig_ship_*`
# has with $REPO):
#   g  CMD [<<EOF]   run CMD on the guest; stdin is forwarded (used to write files)
#   gl CMD           run CMD on the guest WITH the reverse tunnel for the model
#   ok / bad / note  the rig's three reporters
#
# THE TUNNEL IS THE POINT OF `gl`. The guest is behind QEMU user-mode NAT and LM
# Studio binds 127.0.0.1 on the host, so rather than exposing the model server on
# the LAN the rig carries a REVERSE forward on the very connection that runs the
# turn: the forward lives exactly as long as the command does.
#
# THE PROMPTS CROSS AS BASE64 and the fixture goes over STDIN. Both were quoted
# strings first, and both lost their quotes inside the remote `sh -c`: jichi got
# `-p reply` and swallowed `--output json`, and the fixture file was never
# written, so the check blamed the model for a file that did not exist. jichi
# ships --prompt-b64 for exactly this. (tier-b-device.sh, the same day.)

# ===================== THE TASK, SEPARATED FROM THE TRANSPORT ==============
#
# WHY THE SPLIT (M677). The three VM rigs share a transport: ssh into a guest
# behind QEMU NAT, carrying a reverse forward so it reaches the host's
# loopback model server. `tier-b-device.sh` does not -- it drives PHYSICAL
# boards over the LAN, with a `dev` wrapper that cds into a run directory under
# a nested $HOME, so every path it uses is relative and `$HOME/jichi/jichi`
# means nothing there. Converting it to jc_rig_live wholesale would have broken
# a working rig to satisfy a lint.
#
# What must be identical across rows is the TASK -- the two prompts, the nonce,
# the config, the fixture -- because that is what makes two rows comparable.
# The transport is each rig's own business. So the task is these five
# functions, and jc_rig_live below is simply the VM-shaped caller of them.

# The two prompts, base64 so they cross two shells with no quoting at all.
jc_rig_live_prompt_wire() { printf '%s' 'reply with OK' | base64 | tr -d '\n'; }
jc_rig_live_prompt_tool() {
    printf '%s' 'Use the read_file tool to read note.txt in this directory, then report the pass phrase.' \
        | base64 | tr -d '\n'
}

# jc_rig_live_phrase PREFIX -- a nonce for THIS run.
#
# Generated per run: a phrase reused between runs is a phrase a cached answer
# could carry, and then the row proves something about last week.
jc_rig_live_phrase() {
    _jlp="$1-$(od -An -N3 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n' | tr 'a-f' 'A-F')"
    [ "$_jlp" != "$1-" ] || _jlp="$1-FALLBACK"
    printf '%s' "$_jlp"
}

# jc_rig_live_config MODEL URL -- the guest config, on stdout.
#
# A REAL CONFIG, because JICHI_API_BASE IS NOT A THING. The device rig once ran
# `JICHI_API_BASE='$LIVE' ./jichi -p ...`; that variable appears nowhere in
# jichi, so with no config jichi fell back to its DEFAULT provider and the
# "local live turn" sent a request to Anthropic. It 401'd, so nothing was spent
# -- but on a box with ANTHROPIC_API_KEY exported it would have quietly billed a
# priced model for a run the operator believed was local.
# THE THIRD ARGUMENT, AND WHY IT IS NOT A FOURTH CONFIG (M698). Every transport
# that existed when this was extracted reached a KEYLESS server: LM Studio on the
# host's loopback, through a reverse tunnel or an existing forward. `apiKey` could
# therefore be the literal "unused" and nothing noticed.
#
# The Windows-family rows cannot use that transport at all -- they are not guests,
# there is nothing to tunnel from, and LM Studio is not installed on the machine
# that has Cygwin and MSYS2 -- so they drive a real gateway over TLS, which wants
# a key. The choice was between a second config generator in the Windows rigs and
# one optional argument here. `_rig_ship.sh`'s header settles it: *a second rig is
# where drift starts, not the fourth.*
#
# It is `apiKeyEnv`, never `apiKey`: the NAME of a variable, so the secret stays
# out of the config file and out of `ps`. jichi's own doctor warns about a literal
# apiKey in a config, and a rig that generated one would be teaching the habit the
# product warns against.
#
# jc_rig_live_config MODEL URL [KEY_ENV_NAME]
#   With no third argument this emits what it emitted before, byte for byte --
#   which is what keeps every existing row's config unchanged.
jc_rig_live_config() {
    if [ -n "${3:-}" ]; then
        _jlc_auth="\"apiKeyEnv\":\"$3\""
    else
        _jlc_auth="\"apiKey\":\"unused\""
    fi
    cat <<JCLIVECFG
{"models":[{"name":"live","provider":"openai","model":"$1",
 "apiBase":"$2",$_jlc_auth,"roles":["chat"]}],
 "snapshots":false,"repoMap":false,"maxRetries":1,"lowResource":false}
JCLIVECFG
}

# jc_rig_live_fixture PHRASE -- the file the model must read, on stdout.
jc_rig_live_fixture() { printf 'The pass phrase is %s.\n' "$1"; }

# ==========================================================================

# jc_rig_live_skip ROW -- say out loud that the step did not run.
#
# NOT a failure and NOT a pass. Saying so is the difference between "this row is
# Verified" and "this row is Driven", and a rig that stays silent here is how the
# matrix came to look emptier than the work actually done.
jc_rig_live_skip() {
    note "    live turns NOT attempted (no --live-port) -- every step above is offline"
    echo "-- live turns not attempted: this row is Verified, not Driven"
}

# jc_rig_live ROW PORT MODEL DIR -- the VM-shaped caller of the task above.
#
# Requires g/gl/ok/bad/note and a guest laid out as the tier-v rigs lay one out
# ($HOME/jichi built, $HOME/ws as the workspace). A rig with a different shape
# calls the task functions directly -- see tier-b-device.sh.
jc_rig_live() {
    _rl_row="$1"; _rl_port="$2"; _rl_model="$3"; _rl_dir="$4"

    jc_rig_live_config "$_rl_model" "http://127.0.0.1:$_rl_port/v1" \
        | g "cat > \$HOME/live.json" 2>/dev/null || true

    _rl_phrase=$(jc_rig_live_phrase TIER-V)
    g "mkdir -p \$HOME/ws" >/dev/null 2>&1 || true
    jc_rig_live_fixture "$_rl_phrase" | g "cat > \$HOME/ws/note.txt" 2>/dev/null || true

    _rl_p_live=$(jc_rig_live_prompt_wire)
    _rl_p_tool=$(jc_rig_live_prompt_tool)

    # Turn 1: the wire.
    if gl "cd \$HOME/jichi && ./jichi --config \$HOME/live.json --prompt-b64 $_rl_p_live --output json" \
            > "$_rl_dir/live-$_rl_row.txt" 2>&1 \
       && grep -q '"text"' "$_rl_dir/live-$_rl_row.txt"; then
        ok "live turn answered over the reverse tunnel ($_rl_model)"
        note "    $(grep -o '"text":"[^"]*"' "$_rl_dir/live-$_rl_row.txt" | head -1)"
    else
        bad "live turn did not answer -- see $_rl_dir/live-$_rl_row.txt"
        tail -5 "$_rl_dir/live-$_rl_row.txt" 2>/dev/null | sed 's/^/    /' >> "$RESULTS"
    fi

    # Turn 2: the loop. This is the one the verdict is named for.
    if gl "cd \$HOME/ws && \$HOME/jichi/jichi --config \$HOME/live.json --auto -q --prompt-b64 $_rl_p_tool" \
            > "$_rl_dir/live-tool-$_rl_row.txt" 2>&1 \
       && grep -q "$_rl_phrase" "$_rl_dir/live-tool-$_rl_row.txt"; then
        ok "agentic turn: the model called a tool and reported $_rl_phrase"
        note "    the tool ran and its result was consumed by a second turn"
    else
        bad "agentic turn did NOT return $_rl_phrase -- the model may have described \
the tool call instead of invoking it (doctor --live calls that \`text\`), or the \
loop does not execute tools on this platform"
        tail -8 "$_rl_dir/live-tool-$_rl_row.txt" 2>/dev/null | sed 's/^/    /' >> "$RESULTS"
    fi
}
