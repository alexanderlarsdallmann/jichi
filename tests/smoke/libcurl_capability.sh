#!/bin/sh
# smoke: --version and doctor report what the LINKED libcurl can do (C-15/M1133).
#
# THE GAP THIS EXISTS FOR. A TLS-free libcurl fails at the FIRST https call with
# a transfer error, not a verdict; an old libcurl behaves by surprise; a
# static binary may carry no CA bundle at all. Until this driver, none of that was
# visible -- `jichi --version` printed no libcurl line and `jichi doctor` printed
# exactly one networking row, "libcurl available (networking enabled)", that said
# nothing about the version, the TLS backend or the default CA path. The fix
# (jc_net_curl_snapshot) makes all three facts printable, adds the one missing CA
# knob (config "caBundle" -> CURLOPT_CAINFO), and FAILs doctor when an https://
# model endpoint meets a library whose ssl_version says "No TLS".
#
# CHECK 4 is the no-TLS arm. This bench's linked library HAS OpenSSL, so the
# arm is unreachable without the deterministic probe: under FAULT=1, env
# JICHI_FAULT_NO_TLS=1 makes jc_net_curl_has_tls answer 0 for the whole process
# (the same class as JICHI_FAULT_NET_AFTER). A NORMAL binary never reads the env
# -- so check 4 asserts the probe FIRES on a fault build and is IGNORED on a
# normal build (a release binary cannot be spoofed into a safety failure). The
# driver detects the build at runtime via `--version`'s "fault injection
# compiled in" line (refinement (e)); that does NOT match smoke_lint check 16's
# `grep -q ... FAULT=1` universe, so this is a NORMAL run.sh driver, ALSO named
# in the Makefile's smoke-faults target so its probe leg runs under FAULT in
# `make ci`.
#
# All five checks need a curl-linked build (the finding is about the LINKED
# library); a build without the library legitimately has nothing to report, so
# the driver skips before it plans (smoke_lint check 20: a t_skip after t_plan
# emits a second TAP plan line and reads as a failure).
#
# See docs/analysis/2026-10-17-hardening-04-network-and-secrets.md C-15.
. "$(dirname "$0")/_smoke.sh"

smoke_home
tmp=$(smoke_tmp)

# A curl-free build has no linked library at all, so there is nothing to assert;
# skip rather than weaken. Detect it via doctor's "built without libcurl" fail
# (NOT `--version`'s libcurl line: the UNFIXED build also prints no such
# line, and skipping there would turn the red-first witness into a silent pass).
# (< /dev/null: an open non-TTY stdin is read as prompt context and blocks a
# headless run forever -- the tier's invariant.)
if "$BIN" doctor --output json < /dev/null 2>/dev/null \
    | grep -q 'built without libcurl'; then
    t_skip "curl-free build: the linked library has nothing to report"
fi

t_plan 5

# The host libcurl version, as the NUMBER curl_version_info reports (the bare
# "8.18.0" out of "libcurl 8.18.0"). Self-anchoring: the driver greps the same
# number the host's library claims, so a stale link (a different version) fails.
hostver=$(curl-config --version 2>/dev/null | sed 's/^libcurl //')

mk_cfg() {  # $1 = path, $2 = apiBase scheme (https or http), $3 = caBundle ("" => omit the key)
    if [ "$3" = "" ]; then
        cat > "$1" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock","apiBase":"$2://api.example.com/v1",
"apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF
    else
        cat > "$1" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock","apiBase":"$2://api.example.com/v1",
"apiKey":"x","roles":["chat"]}],
"caBundle":"$3",
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF
    fi
}

# The check reads the CONFIG, not the network, so no model has to be reachable.
doct() {  # $1 = config path
    "$BIN" doctor --output json --config "$1" 2>/dev/null
}

# The JSON report's label for the one check whose label begins "$2", or "".
# The report is a single line and a label may itself contain a comma (the CA
# bundle line names "config caBundle, CURLOPT_CAINFO"), so grep -o on
# "label":<prefix>[^"]* stops at the closing quote -- robust to commas.
label_of() {  # $1 = config, $2 = label prefix
    doct "$1" | grep -o "\"label\":\"$2[^\"]*\"" \
        | sed -n 's/^"label":"\([^"]*\)"$/\1/p' | head -1
}

# 1. --version prints the libcurl line: "libcurl: <host version> (TLS: <backend>;
#    CA path: ...)" with a NON-EMPTY TLS backend that is not "no tls". RED on the
#    unfixed tree (no such line) and on a TLS-free library (the backend would be
#    "No TLS", caught by the no-tls guard).
vout=$("$BIN" --version < /dev/null)
curlline=$(printf '%s\n' "$vout" | grep '^libcurl: ' | head -1)
backend=$(printf '%s\n' "$curlline" | sed -n 's/.* (TLS: \([^;]*\); CA path: .*/\1/p')
case "$backend" in
    "") ok=0 ;;
    "(none)") ok=0 ;;
    *)
        case "$(printf '%s\n' "$backend" | tr 'A-Z' 'a-z')" in
            *"no tls"*) ok=0 ;;
            *) ok=1 ;;
        esac
        ;;
esac
if [ "$ok" = 1 ] && printf '%s\n' "$curlline" | grep -q "libcurl: $hostver (TLS: $backend;"; then
    t_ok "--version reports libcurl $hostver with TLS backend '$backend'"
else
    t_fail "--version libcurl line missing or malformed (want 'libcurl: $hostver (TLS: <non-empty, not no-tls backend>'); got: '$curlline' / backend '$backend'"
fi

# 2. doctor (https config) prints the BYTE-IDENTICAL ok label WITH a detail line
#    containing the host version + "TLS:" + "CA path:", and a "fail" count in the
#    JSON summary (the problems marker). RED on the unfixed tree (no detail line).
mk_cfg "$tmp/https.json" "https" ""
dlabel=$(label_of "$tmp/https.json" "libcurl available (networking enabled)")
dout=$(doct "$tmp/https.json")
# The detail for the libcurl check: its check object is
# {"status":"ok","label":"libcurl available (networking enabled)","detail":"..."},
# so grep -o from the label to the detail quote (the label has no commas that
# would matter; the detail is the next "detail":"..." field).
ddet=$(printf '%s\n' "$dout" | grep -o 'libcurl available (networking enabled)","detail":"[^"]*"' \
        | sed -n 's/.*"detail":"\([^"]*\)"$/\1/p' | head -1)
if [ "$dlabel" = "libcurl available (networking enabled)" ] \
   && printf '%s\n' "$ddet" | grep -q "$hostver" \
   && printf '%s\n' "$ddet" | grep -q 'TLS:' \
   && printf '%s\n' "$ddet" | grep -q 'CA path:' \
   && printf '%s\n' "$dout" | grep -q '"fail":'; then
    t_ok "doctor prints the libcurl detail line (version + TLS + CA path) with the summary"
else
    t_fail "doctor libcurl detail line or summary missing (label '$dlabel', detail '$ddet'; want version $hostver, 'TLS:', 'CA path:' and a 'fail:' summary field)"
fi

# 3. doctor with caBundle set to a real temp .crt path prints a "CA bundle:" line
#    naming it (OK); with a NON-EXISTENT caBundle path the line is a FAIL (the
#    doctor probes the path with lstat + S_ISREG; libcurl would die with
#    CURLE_SSL_CACERT at the first https transfer, so exit code 1 -- M1133
#    follow-up-2, finding b); with caBundle ABSENT the line says "library
#    default"; and with caBundle present but the EMPTY-STRING value
#    ("caBundle": "") the parser's empty->NULL mapping (jc_config.c) makes it
#    read as absent too, so it reports "library default" as well -- all four
#    shapes pinned here. RED on the unfixed tree (the key is ignored, so no
#    "CA bundle:" line exists; on the tree before the probe, the bad path was
#    reported as an OK line).
printf 'x\n' > "$tmp/ca.crt"
mk_cfg "$tmp/ca.json" "https" "$tmp/ca.crt"
ca_line=$(label_of "$tmp/ca.json" "CA bundle:")
# Fourth leg (M1133 follow-up-2, finding b): a caBundle pointing at a
# NON-EXISTENT path must be a FAIL -- the doctor probes it (lstat + S_ISREG);
# libcurl would die with CURLE_SSL_CACERT at the first https transfer. The
# doctor's exit code must be 1. Each leg runs its own config file, so this
# run's FAIL is scoped to it and cannot trip the other three legs. RED before
# the probe: the unfixed doctor reported the bad path as an OK line. (Note the
# reachability check FAILs on these example.com configs too, so the leg
# asserts the CA-bundle check's STATUS, not just the exit code.)
_badhex=$(od -An -N8 -tx1 /dev/urandom | tr -d ' \n')
[ -n "$_badhex" ] || _badhex="red$$_"
badpath="$tmp/does-not-exist-$_badhex.crt"
mk_cfg "$tmp/cab.json" "https" "$badpath"
bad_line=$(label_of "$tmp/cab.json" "CA bundle:")
bad_json=$(doct "$tmp/cab.json")
bad_rc=$?
bad_status=$(printf '%s\n' "$bad_json" \
    | grep -o '"status":"[a-z]*","label":"CA bundle:' | head -1 \
    | sed -n 's/.*"status":"\([a-z]*\)".*/\1/p')
if [ "$bad_status" = "fail" ] \
   && printf '%s\n' "$bad_json" | grep -q 'not a readable regular file' \
   && printf '%s\n' "$bad_json" | grep -q 'first https transfer will fail' \
   && [ "$bad_rc" = 1 ]; then
    bad_ok=1
else
    bad_ok=0
fi
mk_cfg "$tmp/https.json" "https" ""
default_line=$(label_of "$tmp/https.json" "CA bundle:")
# The empty-string shape: the key is in the config, its value is "".
cat > "$tmp/cae.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock","apiBase":"https://api.example.com/v1",
"apiKey":"x","roles":["chat"]}],
"caBundle":"",
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF
empty_line=$(label_of "$tmp/cae.json" "CA bundle:")
case "$ca_line" in
    "CA bundle: $tmp/ca.crt (config caBundle, CURLOPT_CAINFO)") ca_ok=1 ;;
    *) ca_ok=0 ;;
esac
case "$default_line" in
    "CA bundle: library default ("*) def_ok=1 ;;
    *) def_ok=0 ;;
esac
case "$empty_line" in
    "CA bundle: library default ("*) emp_ok=1 ;;
    *) emp_ok=0 ;;
esac
if [ "$ca_ok" = 1 ] && [ "$def_ok" = 1 ] && [ "$emp_ok" = 1 ] \
   && [ "$bad_ok" = 1 ]; then
    t_ok "doctor names the configured CA bundle; library default when absent or empty-string; a non-existent path FAILs"
else
    t_fail "CA bundle lines wrong (set: '$ca_line'; non-existent: '$bad_line' [status $bad_status, rc $bad_rc]; unset: '$default_line'; empty-string: '$empty_line')"
fi

# 4. The no-TLS arm, via the deterministic probe. Under FAULT=1, JICHI_FAULT_NO_TLS=1
#    makes the doctor FAIL with the no-TLS line on an https config; WITHOUT the env
#    the same config does not (this host HAS TLS). On a NORMAL binary the env is
#    IGNORED (a release build cannot be spoofed into a safety failure). RED on the
#    unfixed tree: the arm is absent, so neither leg behaves.
mk_cfg "$tmp/nhttps.json" "https" ""
if printf '%s\n' "$vout" | grep -q 'fault injection compiled in'; then
    armed=$(JICHI_FAULT_NO_TLS=1 "$BIN" doctor --output json --config "$tmp/nhttps.json" 2>/dev/null)
    unarmed=$(doct "$tmp/nhttps.json")
    if printf '%s\n' "$armed" | grep -q 'no TLS backend' \
       && printf '%s\n' "$unarmed" | grep -qv 'no TLS backend'; then
        t_ok "FAULT probe: JICHI_FAULT_NO_TLS=1 fires the no-TLS FAIL, absent env does not"
    else
        t_fail "FAULT probe leg wrong (armed no-TLS count: $(printf '%s\n' "$armed" | grep -c 'no TLS backend'); unarmed no-TLS count: $(printf '%s\n' "$unarmed" | grep -c 'no TLS backend'))"
    fi
else
    probe=$(JICHI_FAULT_NO_TLS=1 "$BIN" doctor --output json --config "$tmp/nhttps.json" 2>/dev/null)
    if printf '%s\n' "$probe" | grep -q 'no TLS backend'; then
        t_fail "normal binary must IGNORE JICHI_FAULT_NO_TLS (a release build cannot be spoofed into a safety failure)"
    else
        t_ok "normal build ignores the no-TLS probe env (release-safe)"
    fi
fi

# 5. The arm is https-only: a doctor with an http:// apiBase prints NO no-TLS fail
#    even under the probe (plain http needs no TLS, so a TLS-free library is fine
#    for it) -- the M472/M1132 loopback/private behaviours stay green. This guards
#    against a fix that fails http:// configs too.
mk_cfg "$tmp/http.json" "http" ""
hout=$(JICHI_FAULT_NO_TLS=1 "$BIN" doctor --output json --config "$tmp/http.json" 2>/dev/null)
if printf '%s\n' "$hout" | grep -q 'no TLS backend'; then
    t_fail "http:// apiBase must not trip the no-TLS arm (it is https-only); the line appeared"
else
    t_ok "http:// apiBase stays silent under the probe (the arm is https-only)"
fi

t_done
