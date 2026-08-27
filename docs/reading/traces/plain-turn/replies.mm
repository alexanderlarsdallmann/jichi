# plain-turn/replies.mm -- the reply table for tests/tools/mockmodel.
#
# One rule, because one round trip. `text` makes the mock stream an assistant
# message with content and a stop finish reason -- the shape that ends a turn.
wire openai

rule
  count 1
  text A translation unit is one source file after preprocessing, with every #include pulled in and every macro expanded -- the thing the compiler actually compiles.

# A second request would mean the loop did not stop where this trace claims it
# stops. With MAX_REQUESTS=1 the mock is already gone by then; this rule is
# what answers if someone raises that bound to look.
rule
  status 500
  body {"error":"unexpected request -- this trace expects exactly 1"}
