# jichi gap analysis — semantic codebase search (retrieval)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

The hybrid retrieval pipeline (dense cosine + BM25-lite + RRF + optional
rerank/query-rewrite) has its *integrity* half rigorously guarded and tested —
M483 tells the model an index has a hole even on a *populated* result, and
dim-mismatch / embed-failure are distinct errors, not silent empties
(`src/index/jc_search.c:42–67,135–138`). What has no guard and no measurement
is the half the tool exists for: whether the ranked top-k contains the code a
human would point at. The register says so in the shape of a question —
`ALGORITHMS.md:522` ("what measurement would tell you the simple thing had
stopped being right?") answers it for BM25-lite's *cost* and leaves the
relevance answer blank; `test_index.c` routes the real-index path to "exercised
manually"; the smoke drivers are retention (M612) and coverage (M483). A model
acting on "the most relevant code chunks" is trusting exactly the unmeasured
axis.

## 1. What exists

`codebase_search` is a real, documented, hybrid retrieval tool: embed the workspace
(cached on disk), dense cosine ranking, optional BM25-lite lexical pass
(`src/index/jc_lexical.c`), Reciprocal Rank Fusion (`jc_rrf_fuse`), optional rerank
(`src/net/jc_rerank.c`), query rewrite (`jc_queryrewrite_run`) — the whole stack in
`jc_retrieve_from_index` (`src/index/jc_retrieve.c`). Its math is read in
`ALGORITHMS.md` ("four lessons, each measured on this tree").

The *integrity* half is rigorously guarded:

- **Index holes are reported to both audiences (M483).** `jc_search.c:46`:
  "'No matching code found' is what a model reads as 'the code does not contain
  this', and it is exactly the wrong conclusion when part of the tree was never
  indexed" — an unreadable directory now appends a coverage note **on hits too**
  ("an incomplete ranking misleads just as much as an empty one").
- **Dimension mismatch is a distinct error, not an empty result**
  (`jc_search.c:135`: "query/index embedding dimension mismatch (try: jichi
  index --reindex)").
- **Embed-failure is a distinct error** (`jc_search.c:138`).
- The pure pieces are unit-tested: `test_embed.c` (parser), `test_index.c`
  ("offline tests for the index's pure helpers and cosine ranking. Building a
  real index needs an embedding endpoint"), `test_vec.c`.

## 2. The one thing definitely lacking

**An end-to-end measurement that a query returns the *relevant* code.** Everything
above proves the pipeline does not *lie about its coverage* — an index hole, a dim
mismatch, and an embed failure are each told, not hidden. Nothing proves it
*retrieves well*.

## 3. The gap, in its own words

The gap is not a hole the register missed — it is a hole the register *named and
left open*. `ALGORITHMS.md:522`, the page's closing section ("What jichi does not
have, and how you would know it had stopped being right"), frames every algorithm
around one question: **"what measurement would tell you the simple thing had
stopped being right?"** For the BM25-lite pass the register names it concretely:
"a repository large enough that re-reading every document per query is the
slowest step of a turn." For *retrieval quality itself* — does the fused, reranked
top-k actually contain the chunk a human would point at for this query — **no
such measurement is named, built, or recorded.** The lessons measured on this tree
are about the *ranking functions* (BM25's `k1`/`b`, repetition saturation), not
about whether the *dense+lexical+RRF+rerank* stack, on a real embed model, returns
the right files.

The unit tests confirm the boundary: `test_index.c` tests cosine and chunk
grouping in isolation ("Building a real index needs an embedding endpoint");
`test_embed.c` "exercise only the response parsers; no sockets are opened." No
smoke/e2e driver builds a real index, embeds a real query, and asserts the expected
file is in the top-k.

## 4. Why "definitely lacking" (alternatives and their status)

- **Is it covered by the M318 "one model measured" line?** No — M318/M326b was the
  local 31B *chat* model's compaction benefit; the *embedding* model is a
  different, operator-chosen endpoint (ROADMAP: "an HRZ lane… embeds with
  jlu/qwen3-embedding… a full index in 75 s, 4096 dimensions"). No measurement of
  its retrieval quality is recorded.
- **Is quality tested against a small rig?** No — `grep codebase tests/smoke/`
  hits only `prune_index.sh` and `index_coverage.sh` (both *integrity*, the M483
  hole/coverage, not relevance).
- **Is the rerank pass tested for effect?** No — no driver shows rerank moves the
  right chunk up over the wrong ones.
- **Is query-rewrite measured?** No.

So the alternatives are each either integrity (tested) or a different model's
measurement (M318) — none is a retrieval-quality measurement.
## 5. Evidence

- **`src/index/jc_search.c:42-67`** - the M483 integrity net: `n == 0` appends
  "No matching code found" **and** `append_coverage_note` ("Stated on hits too,
  below, since an incomplete ranking misleads just as much as an empty one").
  The pipeline's *honesty* is the tested, documented strength. (Verified.)
- **`src/index/jc_search.c:135,138`** - dim-mismatch and embed-failure are
  distinct `error:` strings, not empty results. (Verified.)
- **`docs/ALGORITHMS.md:522`** - "what measurement would tell you the simple
  thing had stopped being right?" names a measurement for BM25-lite's *cost*
  ("a repository large enough that re-reading every document per query is the
  slowest step of a turn") but names **no retrieval-quality measurement**.
  (Verified.)
- **`tests/test_index.c:4-7`** - "offline tests for the index's pure helpers
  (chunk grouping and cosine ranking). Building a real index needs an
  embedding endpoint." (Verified - the boundary is stated in the test.)
- **`tests/test_embed.c`** - "offline parser tests... These exercise only the
  response parsers; no sockets are opened." (Verified.)
- **`tests/smoke/prune_index.sh` (M612), `index_coverage.sh` (M483)** - the
  only smoke drivers exercising the index *pipeline* (the other `codebase`
  hits in `tests/smoke/` are doc/config lints - verified); both
  *integrity/retention*, neither relevance.
- **`docs/ROADMAP.md:44141`** - the embed model is operator-chosen
  (`jlu/qwen3-embedding`, 4096 dimensions, "a full index in 75 s"); no
  retrieval-quality number recorded for it. (Verified.)

## 6. Implications

- **The tool's *trust* depends on a quality property that is unmeasured.** A
  model that acts on `codebase_search`'s top-k - "the most relevant code
  chunks, ranked" - is trusting retrieval *accuracy*. The register's integrity
  net (M483) protects against the tool *claiming coverage it lacks*; it does
  not protect against it *returning the wrong files with full, honest
  coverage*. Those are different failures: M483 makes the *empty/incomplete*
  case honest; a *populated-but-misranked* result is exactly the case no
  measurement guards.
- **It is the M318 shape at the scale of a capability.** M318 measured one 31B
  model and shipped the conclusion its evidence licenses ("off under --lite
  only"), registering that the *frontier* case is unmeasured. Here the
  retrieval stack's *whole job* is the unmeasured case: no embed model's top-k
  has been scored against a ground truth of "the code a human would point at."
  The operator picks the embedding model and jichi ranks by it, with no
  recorded signal of whether the ranking is *right*.
- **The honest framing is the register's own.** It already says "here is the
  question to ask" (ALGORITHMS.md:522) and answers it for cost, not for
  relevance. The gap is that the relevance answer is blank.
## 7. Recommendation

1. **A retrieval-quality micro-benchmark, one recorded run.** A small
   ground-truth set (say 20-50 queries, each with the file(s) a human says is
   the right chunk), embedded against the operator's actual embed endpoint,
   scoring recall@5 / MRR for the fused+reranked top-k - and the numbers
   recorded in ALGORITHMS.md's "how you would know it stopped being right"
   section, the way the BM25 k1/b lessons are. That converts the blank
   relevance answer into a measured one, and names the signal a future
   embed-model or stack change would break.
2. **A red-first offline driver for the one quality failure the rig *can*
   reach.** Even without a live endpoint, a mock embed server that returns
   deterministic vectors lets a test assert the pipeline's *ranking* is sane
   (a chunk's own query outranks an unrelated chunk) - catching a regression
   in jc_rrf_fuse/cosine ordering before it ships.
3. **One line in ALGORITHMS.md** alongside the BM25 cost-measurement:
   retrieval *quality* is the measurement the operator supplies by choosing an
   embed model jichi has not scored - and the micro-benchmark of step 1 is
   what would close it.

Rejected, in the register's spirit:

* *"It's an operator-chosen model, so quality is the operator's problem"* -
  jichi does *rank* with that model and *tell the model it is the most
  relevant chunk*; a ranking presented as relevant but never scored is jichi's
  claim, not the operator's. The doc already takes this on by framing the
  question to ask of each algorithm.
* *"The M483 integrity net covers it"* - M483 covers the *coverage* axis
  (holes, dim mismatch, embed failure - all *told*). It does not touch the
  *relevance* axis (right file, ranked first). Confusing the two would be the
  M326b shape: a check that passes because its universe shrank to the one
  measured.

## 8. Limitations and honest counterarguments

1. **The pipeline may be genuinely good.** Nothing here claims codebase_search
   returns bad results - it claims no measurement shows it returns *good*
   ones. The integrity net is strong, and the operator *can* look at top-k and
   judge; the gap is that the judgment is not recorded, so it cannot be
   defended after an embed-model or stack change.
2. **The cost measurement IS recorded (ALGORITHMS.md:522).** The register names
   a measurement for BM25-lite's *performance*; I scope the gap to *relevance*,
   which is the axis left blank. I do not claim the page omits *all*
   measurements.
3. **I have not run the stack against a live embed endpoint** (the offline
   tier and this analysis's read-only constraint). The "no relevance
   measurement recorded" claim is a register-silence claim (verified by
   grep); a quality run is what step 1 produces. Note test_index.c says the
   real-index path is "exercised manually" - the register already routes the
   live case to a human, which is exactly why it has no recorded signal.
4. **Query-rewrite and rerank add moving parts the quality number would have
   to hold together.** A single recall@5 hides which stage helps; the
   recommendation scores the *end-to-end* top-k (the user-facing quantity) and
   notes that a per-stage breakdown is the honest follow-on, not the fix.

## 9. Appendix: the survey, reproducible

All from /home/u/development/journey/jichi, at HEAD 1b71247d:

    # the integrity net (the tested strength)
    sed -n '42,67p' src/index/jc_search.c        # M483: holes told, on hits too
    grep -n "dimension mismatch\|failed to embed" src/index/jc_search.c

    # the pipeline (dense + BM25 + RRF + rerank)
    grep -n "jc_retrieve_from_index\|jc_rrf_fuse\|jc_lexical_topn" src/index/jc_search.c src/index/jc_retrieve.c | head

    # the register's own framing of the missing measurement
    sed -n '522,524p' docs/ALGORITHMS.md         # "what measurement would tell you..."

    # the tests: pure helpers + parsers only, no relevance
    sed -n '4,7p' tests/test_index.c             # "is exercised manually"
    sed -n '5,6p' tests/test_embed.c             # "no sockets are opened"
    grep -rln "codebase" tests/smoke/            # prune_index.sh, index_coverage.sh

    # the embed model is operator-chosen; no quality number recorded
    grep -n "qwen3-embedding\|4096 dimensions" docs/ROADMAP.md | head

**Verdict line for the register this could seed:** jichi's hybrid semantic
search (codebase_search: dense cosine + BM25-lite, RRF, optional rerank and
query-rewrite, src/index/jc_retrieve.c) has its integrity half rigorously
guarded and tested - M483 tells the model an index has a hole on hits as well
as empties, and dim-mismatch and embed-failure are distinct errors, not empty
results (jc_search.c:135,138). What is not guarded or measured is the half the
tool exists for: whether the ranked top-k actually contains the code a human
would point at. ALGORITHMS.md:522 asks, of each algorithm, "what measurement
would tell you the simple thing had stopped being right?" and answers it for
BM25-lite's cost but leaves the relevance answer blank; the unit tests stop at
"the pure helpers... is exercised manually" (test_index.c), the smoke drivers
are retention (M612) and coverage (M483), and the operator's chosen embed model
(ROADMAP: qwen3-embedding, 4096 dimensions) carries no recorded
retrieval-quality number. The M483 net makes the empty and incomplete cases
honest; it does not make the populated-but-misranked case honest - and that is
the one a model acting on "the most relevant code chunks" is trusting. Revisit
when: a small ground-truth query set is scored for recall@k / MRR against the
operator's embed endpoint and the number recorded in ALGORITHMS.md's "stopped
being right" section, and an offline mock-embed driver asserts the RRF/cosine
ranking is sane so a stack regression is caught before it ships.
