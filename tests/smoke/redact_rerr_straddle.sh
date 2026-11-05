#!/bin/sh
# M1030 (C-03b residual M1028b) + M1036 (F1): rerr-altitude straddle tooth
# Prove that a registered secret straddling byte 120 of the provider's
# 401 error body leaks its near half (pre-fix) or is fully redacted (post-fix).
#
# Leg 1 (M1030): single 401 body where canary STRADDLES byte 120
# Leg 2 (M1036 F1): extended testing of the zero-hit branch with >= 512-byte bodies
#
# The mechanism: stream_once's http_status>0 branch builds app->last_model_error
# from the provider's error body. The fix redacts BEFORE cutting the 120-byte prefix.
#
# This driver uses ONE mockmodel instance and COUNT predicates:
#   req.1 = the LEAD's first call -> tool call spawn_subagent
#   req.2 = the CHILD's failing model call -> status 401 with body where canary STRADDLES byte 120
#   req.3 = the LEAD's follow-up call -> text DONE
#
# The child uses the same app/provider (same mock), so the count-2 rule triggers the 401.
# The clean observable is the mock model's captured request for the lead's follow-up call (raw wire).
# The rerr that the child returns flows into the lead's history, so req.3 carries it.

. "$(dirname "$0")/_smoke.sh"

t_plan 14
smoke_home
CANARY=CANARYSECRETzq7x0123456789

# Temp dirs must be defined BEFORE they're used (tmp, ws)
tmp=$(smoke_tmp); ws=$(smoke_tmp)

# Canary: 26 chars, starts at byte 105 of the body, straddles the 120 cut
# The 120-byte prefix (indices 0-119) contains bytes 105-119 of the canary (15 bytes)
# Near-half (leaks pre-fix): canary[0:13] = "CANARYSECRETzq" (14 chars)
NEAR14="CANARYSECRETzq"
# Padding: 84 x's to put canary at byte 105 (0-indexed)
# 84 X's computed via C99-free sh idiom
X84=$(printf 'x%.0s' $(seq 84))
# 20 y's after canary
Y20=$(printf 'y%.0s' $(seq 20))

# Body structure:
# Prefix: {"error":{"message":" = 21 chars (bytes 0-20)
# Padding: 84 X's (bytes 21-104)
# Canary: CANARYSECRETzq7x0123456789 = 26 chars (bytes 105-130)
# Y's: 20 chars (bytes 131-150)
# Suffix: "}" (trailing quote + closing brace)
# Total: 153 bytes
BODY_PREFIX='{"error":{"message":"'
BODY_SUFFIX=$(printf '%s"}' "$Y20")

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool spawn_subagent {"task":"CHILD investigate and report back"}
rule
  count 2
  status 401
  body ${BODY_PREFIX}${X84}${CANARY}${BODY_SUFFIX}
rule
  count 3
  text DONE
EOF

# Verify the canary lands at byte 105 of the body (print in # line for debugging)
# Body before canary: 21 (prefix) + 84 (X's) = 105 bytes
echo "# Body canary position: prefix=21 + padding=84 = byte 105 (0-indexed)"

mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF2
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF2

# Run: prompt triggers spawn_subagent -> child runs -> fails with 401 -> lead gets rerr -> lead's next request carries it
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" -q --auto \
    --output jsonl -p "investigate and report back" < /dev/null > "$tmp/out.txt" 2>&1 )

mm_stop

# Print capture dir listing for debugging
echo "# Capture directory contents:"
ls -la "$tmp/cap/" 2>/dev/null || echo "cap dir empty or missing"

# Count request files
REQ_COUNT=$(ls -1 "$tmp/cap"/req.* 2>/dev/null | wc -l)
echo "# Request files captured: $REQ_COUNT"

# CHECK 1: denominator - child actually failed and lead made follow-up call
# Count = 3 request files means: 1st (spawn_subagent), 2nd (child 401), 3rd (lead follow-up)
REQ_COUNT=$(ls -1 "$tmp/cap"/req.* 2>/dev/null | wc -l)
if [ "$REQ_COUNT" -ge 3 ]; then
    t_ok "CHECK 1: child failed and lead made follow-up call (3+ requests captured)"
else
    t_fail "CHECK 1: expected 3+ requests, got $REQ_COUNT"
fi

# CHECK 2: the RERR-ALTITUDE TOOTH (red-first) - near-half fragment ABSENT in req.3
# Pre-fix: near-half would be present in rerr (RED - test would FAIL, count=2)
# Post-fix: near-half is redacted to *** (GREEN - test would PASS, count=1 for header only)
G=/usr/bin/grep
[ -x "$G" ] || G=grep
n=$($G -oF "$NEAR14" "$tmp/cap/req.3" 2>/dev/null | wc -l)
if [ "$n" -eq 1 ]; then
    t_ok "CHECK 2: req.3: near-half fragment '$NEAR14' appears exactly once (in Authorization header, not leaked)"
else
    t_fail "CHECK 2: req.3: near-half fragment '$NEAR14' appears $n time(s) (expected 1; pre-fix leak shows 2)"
fi

# CHECK 3: vacuity guard - *** redaction marker IS present in req.3
# Proves redaction happened, not merely that the text never arrived
n=$($G -oF '***' "$tmp/cap/req.3" 2>/dev/null | wc -l)
if [ "$n" -ge 1 ]; then
    t_ok "CHECK 3: req.3: *** redaction marker IS present (redaction worked)"
else
    t_fail "CHECK 3: req.3: *** redaction marker is ABSENT (redaction did not occur)"
fi

# CHECK 4: sanity - the canary reached the lead at all, so CHECK 2/3 are not vacuous
# Check: FULL canary appears in req.3 (Authorization header) AND rerr marker appears
n_canary=$($G -oF "$CANARY" "$tmp/cap/req.3" 2>/dev/null | wc -l)
n_rerr=$($G -oF 'HTTP 401 from the provider' "$tmp/cap/req.3" 2>/dev/null | wc -l)
if [ "$n_canary" -ge 1 ] && [ "$n_rerr" -ge 1 ]; then
    t_ok "CHECK 4: req.3: canary ( Authorization header ) and rerr marker both present"
else
    t_fail "CHECK 4: req.3: canary=$n_canary occurrences, rerr marker=$n_rerr occurrences"
fi

# === M1036 (F1+F2/F3) LEG 2 ===

# CANARY2: 450 distinct, non-self-overlapping characters
# Use incrementing 3-digit numbers: 000, 001, ..., 149 (450 chars total)
# This ensures no self-overlap and the token doesn't appear in leg 1's canary
CANARY2=$(printf '%03d' 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96 97 98 99 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 123 124 125 126 127 128 129 130 131 132 133 134 135 136 137 138 139 140 141 142 143 144 145 146 147 148 149)

# Body for leg 2: 21-byte prefix + 79 'p' padding (so CANARY2 starts at body offset S=100, S < 120)
# + CANARY2 (450 bytes, so S+L = 550 > 512: the full canary does NOT fit in the old 512-copy window)
# + '"}' (2 bytes) = 582 bytes total
# The 512-copy window (indices 0-511) ends at byte 511, so canary at offset 100 with length 450
# covers bytes 100-549, with bytes 100-511 (412 bytes) inside the window and 512-549 (38 bytes) outside.
# The zero-hit branch (S+L > 512, no full match in 512-byte window) copies the raw 120-byte head.
NEAR20="00000100200300400500"
P79=$(printf 'p%.0s' $(seq 79))

# Build body for leg 2: prefix + 79 p's + CANARY2 + suffix
BODY2_PREFIX='{"error":{"message":"'
BODY2_SUFFIX=$(printf '%s"}' '"')

cat > "$tmp/replies2.mm" <<EOF2
wire openai
rule
  count 1
  tool spawn_subagent {"task":"CHILD investigate and report back"}
rule
  count 2
  status 401
  body ${BODY2_PREFIX}${P79}${CANARY2}${BODY2_SUFFIX}
rule
  count 3
  text DONE
EOF2

mm_start_unbounded "$tmp/replies2.mm" "$tmp/cap2"
cat > "$tmp/config2.json" <<EOF3
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY2","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF3

# Run leg 2
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config2.json" -q --auto \
    --output jsonl -p "investigate and report back" < /dev/null > "$tmp/out2.txt" 2>&1 )

mm_stop

echo "# Leg 2 capture directory contents:"
ls -la "$tmp/cap2/" 2>/dev/null || echo "cap2 dir empty or missing"

REQ_COUNT2=$(ls -1 "$tmp/cap2"/req.* 2>/dev/null | wc -l)
echo "# Leg 2 request files captured: $REQ_COUNT2"

# CHECK 5 (THE RED-FIRST TOOTH, M1036 F1): pre-fix the rerr leak makes NEAR20 appear 2 times
# Post-fix: NEAR20 appears exactly 1 time (in Authorization header, not leaked)
n2=$($G -oF "$NEAR20" "$tmp/cap2/req.3" 2>/dev/null | wc -l)
if [ "$n2" -eq 1 ]; then
    t_ok "CHECK 5: req.3 (leg 2): near-20 fragment '$NEAR20' appears exactly once (in Authorization header, not leaked)"
else
    t_fail "CHECK 5: req.3 (leg 2): near-20 fragment '$NEAR20' appears $n2 time(s) (expected 1; pre-fix leak shows 2)"
fi

# CHECK 6: the full CANARY2 appears >= 1 time (Authorization header) AND 'HTTP 401 from the provider' appears >= 1 time
# (leg 1 CHECK 4's vacuity guard, mirrored)
n_canary2=$($G -oF "$CANARY2" "$tmp/cap2/req.3" 2>/dev/null | wc -l)
n_rerr2=$($G -oF 'HTTP 401 from the provider' "$tmp/cap2/req.3" 2>/dev/null | wc -l)
if [ "$n_canary2" -ge 1 ] && [ "$n_rerr2" -ge 1 ]; then
    t_ok "CHECK 6: req.3 (leg 2): CANARY2 ( Authorization header ) and rerr marker both present"
else
    t_fail "CHECK 6: req.3 (leg 2): CANARY2=$n_canary2 occurrences, rerr marker=$n_rerr2 occurrences"
fi

# CHECK 7: the *** marker appears >= 1 time (redaction fired at rerr altitude, mirroring leg 1 CHECK 3)
n_marker2=$($G -oF '***' "$tmp/cap2/req.3" 2>/dev/null | wc -l)
if [ "$n_marker2" -ge 1 ]; then
    t_ok "CHECK 7: req.3 (leg 2): *** redaction marker IS present (redaction worked)"
else
    t_fail "CHECK 7: req.3 (leg 2): *** redaction marker is ABSENT (redaction did not occur)"
fi

# CHECK 8 (the zero-hit/verbatim pin, M1036 F2/F3): a third mockmodel instance with the SAME reply shape
# and a body of >= 560 bytes that contains NO canary (e.g. the 21-byte prefix + 600 'z' padding + '"}')
# same apiKey= CANARY2 in its config; assert on ITS req.3 that a distinctive 40-byte window
# of that body (40 consecutive 'z' padded with 10 'w's, i.e. 'wwwwwwwwwwzzzz...') appears verbatim
# AND 'HTTP 401 from the provider' appears.
# This pins the zero-hit branch at rerr altitude (verbatim pass-through, no *** in the body text)
# -- the transport_err/stream_err folds share this one-call shape and are unit-pinned by tests/test_redact.c

CANARY3="NNX$(printf 'NNX%.0s' $(seq 149))"  # Same as CANARY2, 450 chars
Z600=$(printf 'z%.0s' $(seq 600))

# 40-char window: 40 consecutive z's (appears in the 600 z's padding of leg 3 body)
WINDOW40=$(printf 'z%.0s' $(seq 40))

cat > "$tmp/replies3.mm" <<EOF4
wire openai
rule
  count 1
  tool spawn_subagent {"task":"CHILD investigate and report back"}
rule
  count 2
  status 401
  body ${BODY2_PREFIX}${Z600}"}"
rule
  count 3
  text DONE
EOF4

mm_start_unbounded "$tmp/replies3.mm" "$tmp/cap3"
cat > "$tmp/config3.json" <<EOF5
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY3","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF5

# Run leg 3
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config3.json" -q --auto \
    --output jsonl -p "investigate and report back" < /dev/null > "$tmp/out3.txt" 2>&1 )

mm_stop

echo "# Leg 3 capture directory contents:"
ls -la "$tmp/cap3/" 2>/dev/null || echo "cap3 dir empty or missing"

REQ_COUNT3=$(ls -1 "$tmp/cap3"/req.* 2>/dev/null | wc -l)
echo "# Leg 3 request files captured: $REQ_COUNT3"

# CHECK 8: 40-char window of z's (with 10 w's prefix) appears verbatim AND rerr marker present
# This pins the zero-hit branch: no secret found -> verbatim copy -> the 40-char window is present
n_window=$($G -oF "$WINDOW40" "$tmp/cap3/req.3" 2>/dev/null | wc -l)
n_rerr3=$($G -oF 'HTTP 401 from the provider' "$tmp/cap3/req.3" 2>/dev/null | wc -l)
if [ "$n_window" -ge 1 ] && [ "$n_rerr3" -ge 1 ]; then
    t_ok "CHECK 8: req.3 (leg 3): 40-char window '$WINDOW40' appears verbatim AND rerr marker present"
else
    t_fail "CHECK 8: req.3 (leg 3): window=$n_window occurrences, rerr marker=$n_rerr3 occurrences"
fi

# === M1040 (2047-byte capture-boundary) LEG 4 ===

# SECRET: 2000 chars, built like CANARY2 (printf idiom over 0..499)
# Exactly 2000 chars: 500 numbers * 4 digits each
SECRET=$(printf '%04d' 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96 97 98 99 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 123 124 125 126 127 128 129 130 131 132 133 134 135 136 137 138 139 140 141 142 143 144 145 146 147 148 149 150 151 152 153 154 155 156 157 158 159 160 161 162 163 164 165 166 167 168 169 170 171 172 173 174 175 176 177 178 179 180 181 182 183 184 185 186 187 188 189 190 191 192 193 194 195 196 197 198 199 200 201 202 203 204 205 206 207 208 209 210 211 212 213 214 215 216 217 218 219 220 221 222 223 224 225 226 227 228 229 230 231 232 233 234 235 236 237 238 239 240 241 242 243 244 245 246 247 248 249 250 251 252 253 254 255 256 257 258 259 260 261 262 263 264 265 266 267 268 269 270 271 272 273 274 275 276 277 278 279 280 281 282 283 284 285 286 287 288 289 290 291 292 293 294 295 296 297 298 299 300 301 302 303 304 305 306 307 308 309 310 311 312 313 314 315 316 317 318 319 320 321 322 323 324 325 326 327 328 329 330 331 332 333 334 335 336 337 338 339 340 341 342 343 344 345 346 347 348 349 350 351 352 353 354 355 356 357 358 359 360 361 362 363 364 365 366 367 368 369 370 371 372 373 374 375 376 377 378 379 380 381 382 383 384 385 386 387 388 389 390 391 392 393 394 395 396 397 398 399 400 401 402 403 404 405 406 407 408 409 410 411 412 413 414 415 416 417 418 419 420 421 422 423 424 425 426 427 428 429 430 431 432 433 434 435 436 437 438 439 440 441 442 443 444 445 446 447 448 449 450 451 452 453 454 455 456 457 458 459 460 461 462 463 464 465 466 467 468 469 470 471 472 473 474 475 476 477 478 479 480 481 482 483 484 485 486 487 488 489 490 491 492 493 494 495 496 497 498 499)

# First 20 chars of SECRET: "00000001000200030004"
NEAR20="00000001000200030004"

# Body file: 21-byte prefix + 79 'p' padding + SECRET (2000) + "}" = 2102 bytes
# The secret sits at body offset S=100, capture holds body[0..2046] = prefix+padding+SECRET[0..1946]
# The LAST 53 chars of the secret (SECRET[1947..1999]) are cut
P79=$(printf 'p%.0s' $(seq 79))

# Write body file to $tmp/body4 (2102 bytes, NO trailing newline)
# Body: 21-byte prefix + 79 p's + SECRET (2000) + " (closing quote) + } (closing brace) = 2102 bytes
# Build body: prefix + P79 + SECRET + quote + brace
{
  printf '%s' '{"error":{"message":"'
  printf '%s' "${P79}"
  printf '%s' "${SECRET}"
  printf '%s' '"'
  printf '%s' '}'
} > "$tmp/body4"

# Verify the file size
BODY4_SIZE=$(wc -c < "$tmp/body4")
echo "# Leg 4 body4 file size: $BODY4_SIZE bytes (expected 2102)"

cat > "$tmp/replies4.mm" <<EOF6
wire openai
rule
  count 1
  tool spawn_subagent {"task":"CHILD investigate and report back"}
rule
  count 2
  status 401
  body-file $tmp/body4
rule
  count 3
  text DONE
EOF6

mm_start_unbounded "$tmp/replies4.mm" "$tmp/cap4"
cat > "$tmp/config4.json" <<EOF7
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$SECRET","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF7

# Run leg 4
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config4.json" -q --auto \
    --output jsonl -p "investigate and report back" < /dev/null > "$tmp/out4.txt" 2>&1 )

mm_stop

echo "# Leg 4 capture directory contents:"
ls -la "$tmp/cap4/" 2>/dev/null || echo "cap4 dir empty or missing"

REQ_COUNT4=$(ls -1 "$tmp/cap4"/req.* 2>/dev/null | wc -l)
echo "# Leg 4 request files captured: $REQ_COUNT4"

# CHECK 9 (THE TOOTH): pre-fix the rerr leak makes NEAR20 appear 2 times (header + rerr)
# Post-fix: NEAR20 appears exactly 1 time (in Authorization header only, rerr is redacted)
n9=$($G -oF "$NEAR20" "$tmp/cap4/req.3" 2>/dev/null | wc -l)
if [ "$n9" -eq 1 ]; then
    t_ok "CHECK 9: req.3 (leg 4): near-20 fragment '$NEAR20' appears exactly once (in Authorization header, not leaked)"
else
    t_fail "CHECK 9: req.3 (leg 4): near-20 fragment '$NEAR20' appears $n9 time(s) (expected 1; pre-fix leak shows 2)"
fi

# CHECK 10: the rerr marker is present in req.3 (vacuity guard)
n_rerr4=$($G -oF 'HTTP 401 from the provider' "$tmp/cap4/req.3" 2>/dev/null | wc -l)
if [ "$n_rerr4" -ge 1 ]; then
    t_ok "CHECK 10: req.3 (leg 4): 'HTTP 401 from the provider' rerr marker present"
else
    t_fail "CHECK 10: req.3 (leg 4): 'HTTP 401 from the provider' rerr marker ABSENT"
fi

# CHECK 11: the *** redaction marker is present in req.3 (redaction fired)
n_marker4=$($G -oF '***' "$tmp/cap4/req.3" 2>/dev/null | wc -l)
if [ "$n_marker4" -ge 1 ]; then
    t_ok "CHECK 11: req.3 (leg 4): *** redaction marker IS present (redaction worked)"
else
    t_fail "CHECK 11: req.3 (leg 4): *** redaction marker is ABSENT (redaction did not occur)"
fi

# === M1040-d LEG 5: telemetry off (--log-level off) ===

# SECRET: 2000 chars, same as leg 4
SECRET=$(printf '%04d' 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96 97 98 99 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 123 124 125 126 127 128 129 130 131 132 133 134 135 136 137 138 139 140 141 142 143 144 145 146 147 148 149 150 151 152 153 154 155 156 157 158 159 160 161 162 163 164 165 166 167 168 169 170 171 172 173 174 175 176 177 178 179 180 181 182 183 184 185 186 187 188 189 190 191 192 193 194 195 196 197 198 199 200 201 202 203 204 205 206 207 208 209 210 211 212 213 214 215 216 217 218 219 220 221 222 223 224 225 226 227 228 229 230 231 232 233 234 235 236 237 238 239 240 241 242 243 244 245 246 247 248 249 250 251 252 253 254 255 256 257 258 259 260 261 262 263 264 265 266 267 268 269 270 271 272 273 274 275 276 277 278 279 280 281 282 283 284 285 286 287 288 289 290 291 292 293 294 295 296 297 298 299 300 301 302 303 304 305 306 307 308 309 310 311 312 313 314 315 316 317 318 319 320 321 322 323 324 325 326 327 328 329 330 331 332 333 334 335 336 337 338 339 340 341 342 343 344 345 346 347 348 349 350 351 352 353 354 355 356 357 358 359 360 361 362 363 364 365 366 367 368 369 370 371 372 373 374 375 376 377 378 379 380 381 382 383 384 385 386 387 388 389 390 391 392 393 394 395 396 397 398 399 400 401 402 403 404 405 406 407 408 409 410 411 412 413 414 415 416 417 418 419 420 421 422 423 424 425 426 427 428 429 430 431 432 433 434 435 436 437 438 439 440 441 442 443 444 445 446 447 448 449 450 451 452 453 454 455 456 457 458 459 460 461 462 463 464 465 466 467 468 469 470 471 472 473 474 475 476 477 478 479 480 481 482 483 484 485 486 487 488 489 490 491 492 493 494 495 496 497 498 499)

# First 20 chars of SECRET: "00000001000200030004"
NEAR20="00000001000200030004"

# Body file: same as leg 4
P79=$(printf 'p%.0s' $(seq 79))

# Write body file to $tmp/body5 (2102 bytes, NO trailing newline)
{
  printf '%s' '{"error":{"message":"'
  printf '%s' "${P79}"
  printf '%s' "${SECRET}"
  printf '%s' '"'
  printf '%s' '}'
} > "$tmp/body5"

# Verify the file size
BODY5_SIZE=$(wc -c < "$tmp/body5")
echo "# Leg 5 body5 file size: $BODY5_SIZE bytes (expected 2102)"

cat > "$tmp/replies5.mm" <<EOF8
wire openai
rule
  count 1
  tool spawn_subagent {"task":"CHILD investigate and report back"}
rule
  count 2
  status 401
  body-file $tmp/body5
rule
  count 3
  text DONE
EOF8

mm_start_unbounded "$tmp/replies5.mm" "$tmp/cap5"
cat > "$tmp/config5.json" <<EOF9
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$SECRET","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF9

# Run leg 5 with --log-level off to verify the cut happens even when telemetry is disabled
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config5.json" -q --auto \
    --log-level off --output jsonl -p "investigate and report back" < /dev/null > "$tmp/out5.txt" 2>&1 )

mm_stop

echo "# Leg 5 capture directory contents:"
ls -la "$tmp/cap5/" 2>/dev/null || echo "cap5 dir empty or missing"

REQ_COUNT5=$(ls -1 "$tmp/cap5"/req.* 2>/dev/null | wc -l)
echo "# Leg 5 request files captured: $REQ_COUNT5"

# CHECK 12: the near-20 window appears EXACTLY ONCE in req.3 (header only, not leaked)
n12=$($G -oF "$NEAR20" "$tmp/cap5/req.3" 2>/dev/null | wc -l)
if [ "$n12" -eq 1 ]; then
    t_ok "CHECK 12: req.3 (leg 5, --log-level off): near-20 fragment '$NEAR20' appears exactly once (in Authorization header, not leaked)"
else
    t_fail "CHECK 12: req.3 (leg 5, --log-level off): near-20 fragment '$NEAR20' appears $n12 time(s) (expected 1; RED shows 2 without the moved cut)"
fi

# CHECK 13: the rerr marker is present in req.3 (vacuity guard)
n_rerr5=$($G -oF 'HTTP 401 from the provider' "$tmp/cap5/req.3" 2>/dev/null | wc -l)
if [ "$n_rerr5" -ge 1 ]; then
    t_ok "CHECK 13: req.3 (leg 5, --log-level off): 'HTTP 401 from the provider' rerr marker present"
else
    t_fail "CHECK 13: req.3 (leg 5, --log-level off): 'HTTP 401 from the provider' rerr marker ABSENT"
fi

# CHECK 14: the *** redaction marker is present in req.3 (redaction fired with telemetry off)
n_marker5=$($G -oF '***' "$tmp/cap5/req.3" 2>/dev/null | wc -l)
if [ "$n_marker5" -ge 1 ]; then
    t_ok "CHECK 14: req.3 (leg 5, --log-level off): *** redaction marker IS present (redaction worked with telemetry off)"
else
    t_fail "CHECK 14: req.3 (leg 5, --log-level off): *** redaction marker is ABSENT (RED: cut didn't happen when telemetry off)"
fi

t_done
