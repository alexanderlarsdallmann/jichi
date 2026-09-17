### src/util/jc_plan.c
**Defects Found in `/home/u/development/journey/jichi/src/util/jc_plan.c`**

| Line | Function | Issue |
|------|----------|-------|
| 30 | `heading_is` | Buffer over-read: `line[2]` accessed without verifying `len >= 3` |
| 73 | `jc_plan_parse` | Early return after `jc_plan_init` may leave vector in inconsistent state |
| 102 | `jc_plan_parse` | Potential integer overflow: `bl + 1` not checked for overflow before allocation |
| 213 | `jc_plan_drift` | Redundant cast `(struct jc_vec *)wrote` that casts away const-ness |
| 220 | `jc_plan_drift` | Unnecessary cast `(struct jc_vec *)&p->touches` |
| 208 | `jc_plan_drift` | Counter `n` uses `int` instead of `jc_size`/`size_t`, potential overflow |
| 236 | `jc_plan_load` | Fixed-size buffer `path[1200]` without path length validation |
| 246 | `jc_plan_load` | Missing null check after `jc_plan_parse` on failure |
| 47-54 | `has_reason` | Length check insufficient for em-dash detection (needs `i + 2 < len` before accessing 3 bytes) |
| 217 | `jc_plan_drift` | `jc_arena_strdup` return not validated before use in `jc_vec_push` |
| 225 | `jc_plan_drift` | Path `copy` validated but not checked for NULL before `jc_vec_push` |

**Summary**: Main issues are buffer safety violations, missing null checks after allocations, integer overflow risks, and inconsistent error handling. The most critical are the potential buffer over-read in `heading_is` at line 30 and the fixed-size buffer vulnerability in `jc_plan_load` at line 236.


| 122 | `jc_plan_missing` | Inverted test: `if (p->has_claim)` returns the "missing '## Claim'" message when the claim IS present, so every complete plan is refused |
