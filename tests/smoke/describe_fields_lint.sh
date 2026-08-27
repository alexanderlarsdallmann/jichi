#!/bin/sh
# smoke: `describe --output json` must agree with the code that emits the events
# (M431). A pure lint -- it reads the source, it does not run the binary.
#
# WHY THIS EXISTS. describe is declared **Stable** in docs/EMBEDDING.md, whose own
# operational advice is to "diff `jichi describe --output json` between versions in
# CI". So consumers are told to depend on it -- while its contents are hand-written
# string literals (main.c: "Static by design (these are the contract)") that nothing
# compared against the emitters. Four drifts had accumulated:
#
#   * the `text` event's field is `delta` on the wire; describe said `text`
#   * `usage` carries `cost`; describe said `cache`
#   * the `status` event (M99) was missing from describe entirely
#   * `heartbeat` omitted rss_kb; `done` omitted model, tool_calls and the whole
#     M97 econ block; and `stop_reasons` omitted `scope_tainted` (M332)
#
# A consumer generating a parser from describe would read `.text` and get
# undefined. tests/smoke/describe.sh could not catch any of it: it checks that
# four event types are PRESENT and that field entries look like identifiers, never
# that the names match what is emitted.
#
# GROUND TRUTH is extracted from the emitters themselves:
#   * per-event fields: each jc_agentjson_event("T") in src/main.c, then every
#     cJSON_Add*ToObject(o, "field") up to that event's hl_emit()
#   * the `done` object: jc_agentjson_result in src/util/jc_agentjson.c, counting
#     only fields added to `o` (sub-objects like tokens/cache/tools are their own)
#   * stop reasons: every `stop = "..."` assignment in run_headless
# Every event also carries v and type, stamped by jc_agentjson_event itself.
#
# Extraction FLOORS (docs/TEST_INTEGRITY.md: put a floor under the ground truth so
# a changed source shape fails loudly instead of leaving the lint checking
# nothing). If any floor trips, fix the extraction -- never the floor.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)
root=$(cd "$(dirname "$0")/../.." && pwd)
MAIN="$root/src/main.c"
AJ="$root/src/util/jc_agentjson.c"

# --- ground truth: what the emitters actually write ---------------------------
# "<type> <field>" per line, sorted unique. v/type come from jc_agentjson_event.
{
    # The object variable is CAPTURED from the assignment rather than assumed to be
    # named `o`. M431c added an emitter using `ro` and the first cut of this lint --
    # which hardcoded `ToObject(o,` -- silently extracted none of its fields, so the
    # event appeared to carry only v and type. A lint that quietly checks less than
    # it claims is the failure mode this whole file exists to prevent, so the
    # variable name is no longer a convention this depends on.
    awk '
        match($0, /[A-Za-z_][A-Za-z0-9_]* *= *jc_agentjson_event\("/) {
            v = substr($0, RSTART, RLENGTH)
            sub(/ *= *jc_agentjson_event\("/, "", v)      # the variable
            t = $0; sub(/.*jc_agentjson_event\("/, "", t); sub(/".*/, "", t)
            cur = t; obj = v; nf[cur] = 0
            print cur " v"; print cur " type"; next
        }
        cur != "" && index($0, "ToObject(" obj ", \"") > 0 {
            f = $0
            sub(".*ToObject\\(" obj ", \"", "", f)   # dynamic regex: obj varies
            sub(/".*/, "", f)
            print cur " " f; nf[cur]++
        }
        /hl_emit\(/ {
            # A field-less event is almost certainly an extraction miss, not a real
            # event: every one of them carries payload. Fail loudly rather than
            # quietly contribute nothing.
            if (cur != "" && nf[cur] == 0) {
                print "EXTRACTION_MISS " cur > "/dev/stderr"
            }
            cur = ""
        }
    ' "$MAIN" 2>"$tmp/miss"
    # jc_agentjson_result builds `done`; it has no hl_emit terminator, so the
    # whole function is the region and only additions to `o` are top-level.
    awk '
        /^cJSON \*jc_agentjson_result/ {
            inr = 1
            # It opens with jc_agentjson_event("done"), which stamps these two
            # exactly as the main.c emitters do. Omitting them here reported v
            # and type as declared-but-never-emitted -- a false positive from
            # the lint, on its first run, in the direction that would have had
            # someone DELETE two correct entries from the contract.
            print "done v"; print "done type"
        }
        inr && /cJSON_Add[A-Za-z]*ToObject\(o, "/ {
            f = $0; sub(/.*ToObject\(o, "/, "", f); sub(/".*/, "", f)
            print "done " f
        }
        inr && /^}/ { inr = 0 }
    ' "$AJ"
} | sort -u > "$tmp/emitted"

# --- what describe declares ---------------------------------------------------
# describe_event(arr, "<type>", "<fields>"[, "<note>"]). The fields argument may
# be several C literals concatenated across lines, so the call is flattened and
# adjacent literals are merged ("a" "b" -> "ab") BEFORE splitting on quotes;
# `", "` keeps arguments apart, so field 2 is the type and field 4 the fields.
awk '
    /describe_event\(arr, "/ { acc = $0; collecting = 1 }
    collecting && !/describe_event\(arr, "/ { acc = acc " " $0 }
    collecting && /\);/ {
        gsub(/"[ \t]+"/, "", acc)
        n = split(acc, q, "\"")
        if (n >= 4) {
            m = split(q[4], fl, /[ \t]+/)
            for (i = 1; i <= m; i++) {
                if (fl[i] != "") { print q[2] " " fl[i] }
            }
        }
        collecting = 0; acc = ""
    }
' "$MAIN" | sort -u > "$tmp/declared"

# --- floors -------------------------------------------------------------------
ne=$(cut -d' ' -f1 "$tmp/emitted" | sort -u | grep -c .)
nd=$(cut -d' ' -f1 "$tmp/declared" | sort -u | grep -c .)
# The floor asks only "did each side parse at all?". It deliberately does NOT
# require the two counts to be equal -- that is check 3's job, and a floor that
# pre-judged the answer would report a genuine drift as its own breakage (this
# happened: with the floor at >=8 declared, a missing `status` event tripped it and
# the message told the reader to fix the extraction).
nmiss=$(grep -c "EXTRACTION_MISS" "$tmp/miss" 2>/dev/null || true)
if [ "$ne" -ge 8 ] && [ "$nd" -ge 6 ] && [ "${nmiss:-0}" -eq 0 ]; then
    t_ok "extraction floor: $ne emitted event types, $nd declared, 0 field-less"
else
    t_fail "extraction floor tripped (emitted=$ne want>=8, declared=$nd want>=6, field-less=${nmiss:-0} want 0) -- the source shape changed; fix the extraction, never the floor"
    sed 's/^/# /' "$tmp/miss" 2>/dev/null | head -10
fi

# --- 1: every declared field is really emitted --------------------------------
comm -13 "$tmp/emitted" "$tmp/declared" > "$tmp/ghost"
if [ ! -s "$tmp/ghost" ]; then
    t_ok "describe declares no field the emitters do not write"
else
    t_fail "describe declares $(grep -c . "$tmp/ghost") field(s) the emitters never write: see below"
    sed 's/^/# /' "$tmp/ghost" | head -20
fi

# --- 2: every emitted field is declared ---------------------------------------
comm -23 "$tmp/emitted" "$tmp/declared" > "$tmp/undocumented"
if [ ! -s "$tmp/undocumented" ]; then
    t_ok "every emitted field is declared in describe"
else
    t_fail "$(grep -c . "$tmp/undocumented") emitted field(s) are undeclared in describe: see below"
    sed 's/^/# /' "$tmp/undocumented" | head -20
fi

# --- 3: every emitted event TYPE is declared ----------------------------------
cut -d' ' -f1 "$tmp/emitted" | sort -u > "$tmp/et"
cut -d' ' -f1 "$tmp/declared" | sort -u > "$tmp/dt"
missing_types=$(comm -23 "$tmp/et" "$tmp/dt")
if [ -z "$missing_types" ]; then
    t_ok "every emitted event type appears in describe"
else
    t_fail "emitted event types missing from describe: $(echo $missing_types)"
fi

# --- stop reasons -------------------------------------------------------------
grep -o 'stop = "[a-z_]*"' "$MAIN" | sed 's/.*"\(.*\)"/\1/' | sort -u > "$tmp/stop_code"
awk '
    /arr = cJSON_CreateArray\(\);/ { buf = ""; next }
    /cJSON_CreateString\("/ {
        s = $0; sub(/.*cJSON_CreateString\("/, "", s); sub(/".*/, "", s)
        buf = buf s "\n"
    }
    /"stop_reasons", arr/ { printf "%s", buf; buf = "" }
' "$MAIN" | sort -u > "$tmp/stop_declared"

ns=$(grep -c . "$tmp/stop_code")
nsd=$(grep -c . "$tmp/stop_declared")
if [ "$ns" -ge 7 ] && [ "$nsd" -ge 7 ]; then
    t_ok "extraction floor: $ns stop reasons in code, $nsd declared"
else
    t_fail "stop-reason floor tripped (code=$ns declared=$nsd, want >=7) -- fix the extraction"
fi

stop_missing=$(comm -23 "$tmp/stop_code" "$tmp/stop_declared")
stop_ghost=$(comm -13 "$tmp/stop_code" "$tmp/stop_declared")
if [ -z "$stop_missing" ] && [ -z "$stop_ghost" ]; then
    t_ok "describe's stop_reasons matches the values run_headless emits"
else
    t_fail "stop_reasons drift -- emitted but undeclared: [$(echo $stop_missing)]; declared but never emitted: [$(echo $stop_ghost)]"
fi

t_done
