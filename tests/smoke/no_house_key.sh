#!/bin/sh
# smoke: no house key, no house dialect (M718, plan D4 -- decided "strict" by the
# operator on 2026-09-23).
#
# THE DEFECT. M709 removed the vendor jichi used to choose for a config that named
# none, and stopped at two fallbacks of the same kind. For a model entry that names
# no key (`apiKey` / `apiKeyEnv`), `resolve_key` took `OPENAI_API_KEY` when the
# provider was "openai" -- and `ANTHROPIC_API_KEY` for EVERYTHING ELSE, an unset or
# unrecognised provider included. And `jc_provider_create` guessed the wire dialect
# from the model id, defaulting to Anthropic's. So an entry such as
#
#     {"name":"local","provider":"ollama","model":"qwen","apiBase":"http://gpu-box:11434/v1"}
#
# -- Continue's own vocabulary, which is what jichi's audience brings -- spoke the
# Anthropic dialect to that server and attached whatever `ANTHROPIC_API_KEY` held.
# The review that found it (docs/analysis/2026-09-23-what-to-build-next.md §3.4)
# could only READ it: its loopback probe was refused. This driver is the
# reproduction it could not run, with the tier's own mockmodel capture.
#
# THE DECISION, and what each check holds it to:
#   - an unset or unrecognised provider is REFUSED before any request (checks 1-5);
#   - a vendor's conventional key variable is read only when the entry names that
#     vendor AND the request goes to that vendor's own endpoint -- so provider
#     "openai" at any other host sends no implicit key, and says which line would
#     (checks 6-7);
#   - naming the variable in `apiKeyEnv` is honoured anywhere: strict is about what
#     jichi does WITHOUT being told, never about overriding what it was told (8);
#   - doctor says where the key comes from and which host receives it (9, 12), and
#     never prints the key itself (10); an entry naming no provider fails it (11).
#
# THE CANARIES are fake keys, one per variable, and the ambient environment is
# cleared first: a developer's shell may hold REAL vendor keys, and a check that
# passes or fails because of them is not a check.
#
# Portability: `VAR=x func` is unspecified by POSIX as to whether the assignment
# is EXPORTED to the function's children, and with_deadline is a function -- so the
# canaries are exported inside a subshell instead. No `env -u` (not POSIX).
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

unset ANTHROPIC_API_KEY OPENAI_API_KEY JC_PROVIDER

CA=house-key-canary-anthropic-9d41
CO=house-key-canary-openai-5b2e

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text HOUSE_KEY_DONE
EOF

# run_case NAME FRAGMENT -- one headless turn against a fresh mock, with both
# canaries in the environment. FRAGMENT is raw JSON for the model entry, ending in
# a comma or empty. Leaves $tmp/NAME/{req.*,out,err,rc}.
run_case() {
    _rn=$1; _rf=$2; _rd="$tmp/$_rn"
    mm_start "$tmp/replies.mm" "$_rd" 1
    cat > "$_rd/config.json" <<EOF
{"models":[{"name":"m",${_rf}"model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false,
"maxRetries":0}
EOF
    ( ANTHROPIC_API_KEY=$CA; OPENAI_API_KEY=$CO
      export ANTHROPIC_API_KEY OPENAI_API_KEY
      cd "$ws" && with_deadline 30 "$BIN" --config "$_rd/config.json" \
          --no-session --no-stdin -p "say done" < /dev/null \
          > "$_rd/out" 2> "$_rd/err"
      echo $? > "$_rd/rc" )
    mm_stop
}

# doctor_case NAME JSON -- doctor (offline) on a config, both canaries set.
doctor_case() {
    _dn=$1; _dd="$tmp/$_dn"
    mkdir -p "$_dd"
    printf '%s\n' "$2" > "$_dd/config.json"
    ( ANTHROPIC_API_KEY=$CA; OPENAI_API_KEY=$CO
      export ANTHROPIC_API_KEY OPENAI_API_KEY
      cd "$ws" && with_deadline 30 "$BIN" --config "$_dd/config.json" doctor \
          < /dev/null > "$_dd/out" 2>&1
      echo $? > "$_dd/rc" )
}

# carries DIR CANARY -- 0 when any captured request in DIR contains CANARY.
carries() {
    for _f in "$1"/req.*; do
        [ -f "$_f" ] || continue
        grep -qF "$2" "$_f" && return 0
    done
    return 1
}
nreq() {
    _n=0
    for _f in "$1"/req.*; do [ -f "$_f" ] && _n=$((_n + 1)); done
    echo "$_n"
}
tailerr() { head_bytes 240 < "$1/err" | tr '\n' ' '; }

run_case noprov ''
run_case ollama '"provider":"ollama",'
run_case implicit '"provider":"openai",'
run_case explicit '"provider":"openai","apiKeyEnv":"OPENAI_API_KEY",'

# ---- 1-3. an entry naming NO provider: no key sent, no request, a clear refusal --
if carries "$tmp/noprov" "$CA" || carries "$tmp/noprov" "$CO"; then
    t_fail "an entry with no provider sent a vendor's key it was never given \
(the house key): $(grep -lF -e "$CA" -e "$CO" "$tmp"/noprov/req.* 2>/dev/null | head -1)"
else
    t_ok "an entry with no provider sends no vendor key"
fi
n=$(nreq "$tmp/noprov")
if [ "$n" -eq 0 ]; then
    t_ok "an entry with no provider makes no request at all"
else
    t_fail "an entry with no provider still made $n request(s) -- jichi guessed \
a dialect nobody chose"
fi
if [ "$(cat "$tmp/noprov/rc")" -ne 0 ] &&
   grep -q 'names no provider' "$tmp/noprov/err"; then
    t_ok "the refusal says the entry names no provider, and exits non-zero"
else
    t_fail "no clear refusal for a missing provider (rc=$(cat "$tmp/noprov/rc")): \
$(tailerr "$tmp/noprov")"
fi

# ---- 4-5. an UNRECOGNISED provider ("ollama"): refused the same way ---------------
if carries "$tmp/ollama" "$CA" || carries "$tmp/ollama" "$CO"; then
    t_fail "provider \"ollama\" sent a vendor's key to a server nobody said was \
that vendor's"
else
    t_ok "an unrecognised provider sends no vendor key"
fi
if [ "$(nreq "$tmp/ollama")" -eq 0 ] &&
   grep -q 'provider "ollama"' "$tmp/ollama/err" &&
   grep -q '"openai"' "$tmp/ollama/err"; then
    t_ok "an unrecognised provider is refused, named, and pointed at \"openai\""
else
    t_fail "provider \"ollama\" was not refused with its name and the fix \
($(nreq "$tmp/ollama") request(s)): $(tailerr "$tmp/ollama")"
fi

# ---- 6-7. provider "openai" at a host that is not OpenAI's: no implicit key -------
if [ "$(nreq "$tmp/implicit")" -ge 1 ] &&
   ! carries "$tmp/implicit" "$CO" && ! carries "$tmp/implicit" "$CA"; then
    t_ok "provider openai at another host runs keyless -- OPENAI_API_KEY is not sent"
else
    t_fail "OPENAI_API_KEY went to a host that is not OpenAI's, or no request \
was made ($(nreq "$tmp/implicit") request(s))"
fi
# The note itself, not the word: the generic no-key warning also says
# "apiKeyEnv", so a bare grep for it stayed green with the note deleted
# (found by perturbing this check before it was trusted).
if grep -q 'OPENAI_API_KEY is set but not sent' "$tmp/implicit/err" &&
   grep -q '"apiKeyEnv": "OPENAI_API_KEY"' "$tmp/implicit/err"; then
    t_ok "the run says the key was withheld, and which line would send it"
else
    t_fail "a withheld key was not explained: $(tailerr "$tmp/implicit")"
fi

# ---- 8. naming the variable is honoured anywhere --------------------------------
if carries "$tmp/explicit" "$CO" && ! carries "$tmp/explicit" "$CA"; then
    t_ok "an entry that names apiKeyEnv sends exactly that key"
else
    t_fail "apiKeyEnv OPENAI_API_KEY was not honoured (or the other key went too)"
fi

# ---- 9-12. doctor ----------------------------------------------------------------
doctor_case vendor '{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"some-model"}]}'
doctor_case dnoprov '{"lowResource":false,"models":[{"name":"m","model":"some-model","apiBase":"http://127.0.0.1:9/v1"}]}'
doctor_case laundered '{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"some-model","apiBase":"http://127.0.0.1:9/v1","apiKeyEnv":"OPENAI_API_KEY"}]}'

if grep 'OPENAI_API_KEY' "$tmp/vendor/out" |
   grep -q 'sent only to https://api\.openai\.com'; then
    t_ok "doctor names the key's variable and the one host it goes to"
else
    t_fail "doctor did not say where the key comes from and where it is sent: \
$(grep -i 'key' "$tmp/vendor/out" | head_bytes 240 | tr '\n' ' ')"
fi
leak=""
for d in vendor dnoprov laundered; do
    grep -qF -e "$CA" -e "$CO" "$tmp/$d/out" && leak="$leak doctor:$d"
done
for r in noprov ollama implicit explicit; do
    grep -qF -e "$CA" -e "$CO" "$tmp/$r/out" "$tmp/$r/err" && leak="$leak run:$r"
done
if [ -z "$leak" ]; then
    t_ok "no output of any case prints a key"
else
    t_fail "a key value was printed:$leak"
fi
if grep -q 'names no provider' "$tmp/dnoprov/out" &&
   [ "$(cat "$tmp/dnoprov/rc")" -ne 0 ]; then
    t_ok "doctor fails an active entry that names no provider"
else
    t_fail "doctor did not fail a missing provider (rc=$(cat "$tmp/dnoprov/rc")): \
$(grep -i 'provider' "$tmp/dnoprov/out" | head_bytes 240 | tr '\n' ' ')"
fi
if grep 'OPENAI_API_KEY' "$tmp/laundered/out" | grep -q 'not api\.openai\.com'; then
    t_ok "doctor warns when a vendor's variable is sent to another host"
else
    t_fail "doctor was silent about OPENAI_API_KEY going to 127.0.0.1: \
$(grep -i 'key' "$tmp/laundered/out" | head_bytes 240 | tr '\n' ' ')"
fi

t_done
