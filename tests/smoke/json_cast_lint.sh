#!/bin/sh
# smoke: no out-of-range integer cast of an untrusted JSON double (A-02, M1077).
#
# THE DEFECT. A model or server can send a JSON number the target type cannot
# hold -- {"limit":1e10} -- and a plain cast of that double to int/long is
# UNDEFINED BEHAVIOUR: UBSan reports "1e+10 is outside the range of
# representable values of type 'int'" (measured end-to-end at
# src/tools/tool_util.c:123, the tu_arg_int path, on a SAN build), x86-64
# silently reads INT_MIN, and a zig-cc build traps and aborts. The helpers that
# make the conversion defined (jc_json_clamp_int/long/uint/size, M1077 part 1)
# are already in src/json/jc_json.c; the 117 pre-sweep sites (measured on head
# bd00035b, M1077 part 1) have been converted, and this lint keeps the tree
# from ever introducing a bare cast again.
#
# UNIVERSE, stated (the M305 rule): every .c and .h file under src/. It is
# enumerated with find -name '*.c'/'*.h' | xargs -- /dev/null, NOT grep --include
# (a GNU flag BSD grep silently drops; the project idiom, _smoke.sh
# smoke_srcfiles, is the find|xargs form). The two forbidden shapes, both
# extracted with the gate's own tool (/usr/bin/grep -rnE, the project rule):
#
#   P1  an integer cast applied directly to a JSON number reader --
#       (int|long|unsigned int|unsigned long|jc_size) on jc_json_get_num*
#   P2  an (int|long) cast applied directly to a node's ->valuedouble field
#
# A sweep (M1077 part 2, a LATER wave) converts every P1/P2 site to the clamp
# helpers. THIS is part 1: the helpers are in, the sweep has not run, so the
# unfixed tree still carries 117 of them (100 of P1 + 17 of P2, confirmed on
# head bd00035b in LOG-3 run 0091). The driver is therefore RED BY DESIGN right
# now: the floor (MAX_HITS below) is 117, the count IS 117, and the check that
# demands a clean tree fails with the count and the file:line list. That red is
# the instrument the sweep wave works against; when it lands the lead flips
# MAX_HITS to 0 (a one-line change) and the same driver goes green.
#
# WHAT IS AND IS NOT CHECKED:
#   checked  -- the two cast shapes above, anywhere in src/'s .c/.h.
#   NOT checked -- a cast that goes through a helper (jc_json_clamp_*) is the
#                  FIX and matches neither pattern; a cast of a jichi-written,
#                  in-range value is not this class. The sweep decides per-site
#                  which clamp a call takes; this lint only forbids the bare
#                  cast the sweep replaces.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
root="$SMOKE_ROOT"
# /usr/bin/grep only with its fallback (portability_lint check 36; the merged-/usr
# is a Debian-10 thing, Debian 9 has /bin/grep). In an agent session bare grep can
# be a shell function, so the absolute path is used -- guarded, as the other drivers.
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# THE FLOOR. MAX_HITS is the count the tree is allowed to carry.
#   117 pre-sweep (measured on head bd00035b, M1077 part 1) -> the clean-tree
#       check was red by design: the 117 hits were exactly the sites the sweep
#       would convert, and the red was the evidence it had not run yet.
#   0 now (post-sweep, M1077 part 2) -> the driver asserts a clean tree, and
#       check 4 still pins the count so a NEW cast added later fails loudly
#       instead of being absorbed into a loosened floor.
MAX_HITS=0

# The two patterns, in single quotes (BRE/ERE metacharacters stay literal).
# P1 casts a jc_json_get_num* call; P2 casts a ->valuedouble field access.
p1='\((int|long|unsigned (int|long)|jc_size)\) *jc_json_get_num'
p2='\((int|long)\) *[A-Za-z_][A-Za-z_0-9]*->valuedouble'

# scan PAT DIR... -> file:line hits of PAT, .c/.h only, via find|xargs.
# find enumerates (portable, no --include); xargs runs the gate's own tool on
# the named files, /dev/null guaranteeing an operand and a path prefix.
scan() {
    _sp_pat="$1"; shift
    find "$@" -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null \
        | xargs "$G" -nE "$_sp_pat" /dev/null 2>/dev/null
}

# --- 1: the matcher is alive (a floor under the ground truth) -----------------
# A lint that matches nothing passes while checking nothing (the M285
# discipline). Prove both patterns on a planted file, and prove a clamp-helper
# call and a spaced/jc_size variant are seen where they must be.
mkdir -p "$tmp/planted"
printf 'int a = (int)jc_json_get_num(o, "k", 0.0);\n'            > "$tmp/planted/cast.c"
printf 'long b = (long)n->valuedouble;\n'                        >> "$tmp/planted/cast.c"
printf 'int c = jc_json_clamp_int(jc_json_get_num(o, "k", 0.0));\n' >> "$tmp/planted/cast.c"
printf 'jc_size d = (jc_size) jc_json_get_num(o, "k", 0.0);\n'   >> "$tmp/planted/cast.c"
_np1=$(scan "$p1" "$tmp/planted" | grep -c .)
[ -n "$_np1" ] || _np1=0
_np2=$(scan "$p2" "$tmp/planted" | grep -c .)
[ -n "$_np2" ] || _np2=0
# The planted file has 4 lines: (int)get_num (P1), (long)->valuedouble (P2),
# the clamp helper (must match NEITHER), and (jc_size) get_num (P1, the spaced
# form). So P1 sees 2, P2 sees 1, and the clamp is spared.
if [ "$_np1" -eq 2 ] && [ "$_np2" -eq 1 ]; then
    t_ok "the matcher flags both cast shapes and spares the clamp helper"
else
    t_fail "matcher is broken (P1 planted hits $_np1/2, P2 $_np2/1) -- every \
count below is meaningless. A lint that cannot catch a planted cast is a lint \
that cannot catch a real one."
fi

# --- 2: the universe is non-empty ---------------------------------------------
_ns=$(find "$root/src" -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null | wc -l | tr -d '[:space:]')
[ -n "$_ns" ] || _ns=0
if [ "$_ns" -ge 100 ]; then
    t_ok "scanning $_ns .c/.h files under src/"
else
    t_fail "only $_ns .c/.h files found under src/ (floor 100) -- the find \
broke or src/ moved; this driver would then be checking nothing."
fi

# The extraction: every P1/P2 hit in src/, one file:line each, de-duplicated.
_hits=$( { scan "$p1" "$root/src"; scan "$p2" "$root/src"; } | sort -u )
_n=$(printf '%s\n' "$_hits" | grep -c .)
[ -n "$_n" ] || _n=0

# --- 3: the tree is clean -- RED BY DESIGN until the sweep lands --------------
# THE INSTRUMENT. It demands a clean tree (n == 0). Pre-sweep the tree carries
# the 117 sites M1077 part 2 (the sweep) will convert, so this FAILS now -- that
# failure IS the red-first evidence: "hits present, sweep not done" is not green
# until the sweep lands and the count drops to 0. The message carries the count
# AND the file:line head, so a reader sees both how many remain and which.
#
# THE ONE-LINE FLIP: when the sweep wave lands, the lead changes nothing here --
# this check already wants 0, and the count IS 0. (MAX_HITS in check 4 is the
# pre-sweep floor the sweep works against; it does not gate this check.)
if [ "$_n" -eq 0 ]; then
    t_ok "no out-of-range int/long cast of an untrusted JSON double (clean tree)"
else
    t_fail "$_n untrusted-JSON double->int/long cast(s) in src/ remain -- each is \
undefined behaviour on an out-of-range double (UBSan: \"1e+10 is outside the \
range of representable values of type 'int'\").
This is the A-02 instrument, RED BY DESIGN pre-sweep (M1077 part 1): the \
$_n hits are exactly the sites M1077 part 2 (the sweep) converts to the \
jc_json_clamp_* helpers -- they are floored at $MAX_HITS in check 4 and must \
reach 0 when the sweep lands. The full enumeration (the sweep wave works from
it, grouped per file in the report):
$_hits
Convert the listed sites to the clamps (see src/json/jc_json.h); do not cast."
fi

# --- 4: the count is EXACTLY the floor -- the universe has not moved ----------
# MAX_HITS is the PRE-SWEEP universe: 117, confirmed on head bd00035b (LOG-3 run
# 0091). It is a FLOOR on the count, not a permission to fail check 3: while the
# tree is unfixed the count must be EXACTLY 117, so a sweep that runs early, or a
# cast added/deleted outside the sweep, is caught instead of read as progress.
#   if [ $n -ne $MAX_HITS ] -> "the universe moved (sweep happened or new casts
#   appeared)"
# After the sweep wave the count is 0 and MAX_HITS flips to 0 in the SAME commit
# that lands it, so this stays an exact pin. It is a one-line change: MAX_HITS.
if [ "$_n" -ne "$MAX_HITS" ]; then
    t_fail "the universe moved (sweep happened or new casts appeared): count is \
$_n, the floor is $MAX_HITS. Before the sweep the tree must carry exactly \
$MAX_HITS sites for the red-first to be honest; re-measure and set MAX_HITS to \
the real number (0 once the sweep lands), or the count is lying about what the \
sweep is owed."
else
    t_ok "the count is exactly the post-sweep floor ($_n) -- the tree is clean \
and stays floored so a new bare cast fails loudly (M1077 sweep landed)"
fi

t_done
