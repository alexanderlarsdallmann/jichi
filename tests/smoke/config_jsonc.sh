#!/bin/sh
# smoke: jichi's own config accepts the JSONC its documentation shows (M459).
#
# THE DEFECT THIS EXISTS FOR. Fifteen config examples across docs/ and
# README.md sit inside a ```jsonc fence and carry // comments --
# CONFIG_TUTORIAL.md twice, LOCAL_MODELS.md twice, ROUTING.md's opening
# example. Every one of them was unpasteable: the loader rejected the comment
# and said only "malformed config JSON", naming the file and not the reason.
#
# The machinery existed and was already trusted. jc_jsonc_strip is unit-tested
# and is what jichi uses to read Claude Code configs and workflow files; only
# jichi's OWN config never got it. Lenient with other tools' files, strict with
# its own, and the documentation had long since assumed otherwise.
#
# THE HALF THAT MATTERS MORE is check 3. Accepting comments must not turn the
# loader into something that accepts anything: a genuinely malformed config has
# to keep failing, with the same message. A widening that also swallows real
# errors would trade fifteen unpasteable examples for silently-wrong configs.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

# lowResource is pinned false on purpose: auto-lite reshapes a config on a
# low-RAM host, and this driver is measuring the PARSER, not the tier. smoke_lint
# check 7 enforces the pin, and caught its absence here.
model_line() {
    printf '"models":[{"name":"m","provider":"openai","model":"x",'
    printf '"apiBase":"http://127.0.0.1:9/v1","apiKey":"k","roles":["chat"]}],'
    printf '"lowResource":false'
}

# --- 1: a line comment, the shape every doc example uses ----------------------
{
    echo '{'
    echo '  // which model answers; see docs/MODELS.md'
    printf '  %s\n' "$(model_line)"
    echo '}'
} > "$tmp/line.json"
if "$BIN" --config "$tmp/line.json" models < /dev/null 2>&1 | grep -q 'roles=chat'; then
    t_ok "a // line comment loads"
else
    t_fail "the documented jsonc shape still fails: \
$("$BIN" --config "$tmp/line.json" models < /dev/null 2>&1 | tail -1)"
fi

# --- 2: a block comment and a trailing comma ---------------------------------
# jc_jsonc_strip handles all three; a config edited by hand grows trailing
# commas the same way it grows comments.
{
    echo '{'
    echo '  /* a block comment'
    echo '     spanning lines */'
    printf '  %s,\n' "$(model_line)"
    echo '}'
} > "$tmp/block.json"
if "$BIN" --config "$tmp/block.json" models < /dev/null 2>&1 | grep -q 'roles=chat'; then
    t_ok "a block comment and a trailing comma load"
else
    t_fail "block comment / trailing comma rejected"
fi

# --- 3: THE GUARD -- broken JSON must still be rejected -----------------------
# Stripping comments from a file that has none is the identity, so nothing that
# parsed before may change; and nothing that was broken before may now pass.
printf '{ "models": [ { "name": \n' > "$tmp/broken.json"
if "$BIN" --config "$tmp/broken.json" models < /dev/null 2>&1 \
   | grep -q 'malformed config JSON'; then
    t_ok "genuinely malformed JSON is still rejected, and still says so"
else
    t_fail "the widening swallowed a real syntax error: \
$("$BIN" --config "$tmp/broken.json" models < /dev/null 2>&1 | tail -1)"
fi

# --- 4: a comment inside a STRING is not a comment ----------------------------
# The one way a comment stripper corrupts working configs: an apiBase with //
# in it is every http:// URL there is. If this breaks, every config breaks.
{
    echo '{'
    printf '  %s\n' "$(model_line)"
    echo '}'
} > "$tmp/url.json"
if "$BIN" --config "$tmp/url.json" models < /dev/null 2>&1 \
   | grep -q 'http://127.0.0.1:9/v1'; then
    t_ok "the // inside an http:// URL survives stripping"
else
    t_fail "stripping ate the // in a URL -- every config with an apiBase breaks"
fi

t_done
