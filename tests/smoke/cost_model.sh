#!/bin/sh
# smoke: the `# Cost model` prompt section (M440).
#
# WHAT IT CLOSES. docs/TOOL_OUTPUT_COST.md §6 items 5-8 are measured advice --
# search before reading, read a range, do not re-read, batch shell work -- addressed
# to a HUMAN, to be copied by hand into an AGENTS.md. Meanwhile jichi knew every input
# at runtime: the effective caps (jc_config_cap over jc_toolcaps.h) and whether prompt
# caching is on. The reference workload measured 36% of all tool output in whole-file
# reads and a 72% re-read rate, against 174 search_code calls for 2,056 reads.
#
# THE TWO PROPERTIES THAT MATTER MORE THAN THE PROSE:
#
#   THE GATE. The correct read policy is OPPOSITE on the two backend classes, so
#   unconditional frugality prose is wrong on one of them -- and on a caching backend
#   it is billed once into a prefix where it buys little. Auto therefore emits it only
#   when caching is off; an explicit flag wins either way, which is the escape hatch
#   for a backend that silently ignores a caching request (a measured case).
#
#   PREFIX STABILITY (M31). The tempting thing to state here is the observed cache
#   hit-rate. Stating it would change the cached prefix on every turn and destroy the
#   caching it describes. Checks 5 and 7 guard that from two directions, and the split
#   is the result of a failed perturbation: threading a live number in did NOT break
#   check 5, because the system prompt is built ONCE PER TURN and a headless `-p` run
#   is one turn -- so two captured requests share one built prompt. Check 5 therefore
#   guards the caller passing varying CAPS; check 7 guards the real hazard
#   structurally, and structurally is stronger than any runtime comparison here.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Two turns' worth of replies: a tool call, then an answer. Two model calls means two
# captured request bodies, which is what check 5 compares.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text COST_DONE
rule
  tool read_file {"path":"seed.txt"}
EOF

echo "seed" > "$ws/seed.txt"

# --- 1+2: `sysmsg` shows the section, gated on the cache setting ----------------
# Offline, so no mock is needed: `sysmsg` renders the prompt jichi would send.
#
# `lowResource: false` is load-bearing, not boilerplate: auto-lite on a low-RAM box would
# tighten the five caps, and checks 1 and 4 assert specific numbers. Check 4 still gets
# the lite caps because an explicit --lite outranks the config key (M272) -- which is
# exactly the precedence that makes pinning safe here.
cat > "$tmp/nocache.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x"}],
"promptCache":false,"snapshots":false,"repoMap":false,"references":false,
"lowResource":false}
EOF
cat > "$tmp/cache.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x"}],
"promptCache":true,"snapshots":false,"repoMap":false,"references":false,
"lowResource":false}
EOF

off=$(cd "$ws" && "$BIN" --config "$tmp/nocache.json" sysmsg < /dev/null 2>/dev/null)
on=$(cd "$ws" && "$BIN" --config "$tmp/cache.json" sysmsg < /dev/null 2>/dev/null)

if printf '%s' "$off" | grep -q '# Cost model' &&
   printf '%s' "$off" | grep -q 'read_file 256 KB' &&
   printf '%s' "$off" | grep -q 'Search before you read'; then
    t_ok "with caching off, the section is emitted with its caps and rules"
else
    t_fail "no section on a cacheless config: $(printf '%s' "$off" | grep -c 'Cost model') matches"
fi

# The other half of the gate. A section that appeared unconditionally would satisfy
# check 1 and be the thing the design rejected.
if ! printf '%s' "$on" | grep -q '# Cost model'; then
    t_ok "with caching on, auto leaves it out (the prefix is cached; it buys little)"
else
    t_fail "the section is unconditional -- that is what --no-cost-model would be for"
fi

# --- 3: the explicit flag overrides the verdict --------------------------------
# The escape hatch that matters: a backend can accept a caching request and return no
# cached tokens, which the configured-value gate cannot see. `doctor` and
# `telemetry --cache-audit` detect it; this flag is how the operator acts on it.
forced=$(cd "$ws" && "$BIN" --config "$tmp/cache.json" --cost-model sysmsg \
            < /dev/null 2>/dev/null)
suppressed=$(cd "$ws" && "$BIN" --config "$tmp/nocache.json" --no-cost-model sysmsg \
            < /dev/null 2>/dev/null)
if printf '%s' "$forced" | grep -q '# Cost model' &&
   ! printf '%s' "$suppressed" | grep -q '# Cost model'; then
    t_ok "--cost-model and --no-cost-model both override the cache verdict"
else
    t_fail "flags do not override: forced=$(printf '%s' "$forced" | grep -c 'Cost model') suppressed=$(printf '%s' "$suppressed" | grep -c 'Cost model')"
fi

# --- 4: the caps reported are the EFFECTIVE ones -------------------------------
# A section quoting the built-in numbers while the run enforces --lite's would be worse
# than no section: it would teach the model a cap that is not the one it will hit.
lite=$(cd "$ws" && "$BIN" --config "$tmp/nocache.json" --lite sysmsg \
          < /dev/null 2>/dev/null)
if printf '%s' "$lite" | grep -q 'read_file 64 KB' &&
   printf '%s' "$lite" | grep -q 'the git tools 8 KB' &&
   ! printf '%s' "$lite" | grep -q 'read_file 256 KB'; then
    t_ok "--lite's tighter caps are what the section reports"
else
    t_fail "wrong caps under --lite: $(printf '%s' "$lite" | grep -o 'capped at[^.]*' | head_bytes 150)"
fi

# --- 5: the caps are identical across the run's requests -----------------------
# What this DOES cover: the caller passing a cap that varies between requests.
# What it does NOT cover, stated because the first version of this driver claimed
# otherwise: a value that changes from TURN to turn. jc_sysmsg_build runs once per
# top-level turn, and a headless `-p` run has one turn, so both captured requests carry
# the same built prompt. Breaking it required adding a static counter to the renderer --
# not a realistic mistake. Check 7 covers the realistic one.
mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT" '"promptCache":false'

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session -p "read the seed" < /dev/null > /dev/null 2>&1)
mm_stop

n=$(ls "$tmp"/cap/req.* 2>/dev/null | wc -l | tr -d ' ')
# Extract each request's Cost model paragraph. The system prompt is inside a JSON
# string, so the caps line is compared as it appears on the wire.
for f in "$tmp"/cap/req.*; do
    grep -o 'Your tool output is capped at:[^.]*' "$f" >> "$tmp/caps.txt" 2>/dev/null
done
uniq_caps=$(sort -u "$tmp/caps.txt" 2>/dev/null | grep -c . )
if [ "$n" -ge 2 ] && [ "$uniq_caps" = "1" ]; then
    t_ok "the section is byte-identical across $n requests (prefix stays cacheable)"
else
    t_fail "requests=$n distinct cost sections=$uniq_caps -- a live number leaked into the prompt"
fi

# --- 6: it does not instruct the model to refuse a request ---------------------
# TOOL_OUTPUT_COST §7 rejected auto-bounding reads: the model asked for the file, and a
# silently partial answer to an explicit request produces a wrong conclusion two turns
# later. Prose telling the model to do by hand what jichi declined to do in code would
# re-open that decision through the back door.
if ! printf '%s' "$off" | grep -A 20 '# Cost model' | grep -qi 'refuse'; then
    t_ok "the section informs the decision; it never tells the model to refuse"
else
    t_fail "the section re-opens §7's rejection of auto-bounding reads"
fi

# --- 7: the renderer CANNOT see run state -------------------------------------
# The structural guarantee, and the one that actually holds. The realistic mistake is
# stating the observed cache hit-rate, the tokens spent, or the elapsed time -- all of
# which live on `jc_app`, `jc_envelope` or the calibration. The renderer's signature
# takes a buffer, a flag and five integers, so it has no handle on any of them: it
# cannot read a live number even if someone tries to make it.
#
# A lint over the signature rather than over the prose, per the M295 rule: pin facts
# about C, not guesses about English. Derived from the header, so a future signature
# change that admits `app` fails here loudly instead of quietly permitting the hazard.
root=$(cd "$(dirname "$0")/../.." && pwd)
sig=$(awk '/^void jc_sysmsg_append_cost_model/,/;/' "$root/include/jc_sysmsg.h")
nl=$(printf '%s' "$sig" | grep -c .)
if [ "$nl" -lt 2 ]; then
    t_fail "extraction floor: could not read the declaration (renamed or reformatted?)"
elif printf '%s' "$sig" | grep -qE 'jc_app|jc_envelope|jc_calib|jc_config'; then
    t_fail "the renderer now takes run state -- a live number in this section breaks the cached prefix (M31)"
else
    t_ok "the renderer takes no run state, so it cannot state a live number"
fi

t_done
