### src/util/jc_diff.c
## Defects Found in `src/util/jc_diff.c`

1. **Line 64 — `split_lines()`**
   - **Issue**: Potential signed/unsigned mismatch and undefined behavior when casting `strlen(p)` to `jc_size`.
   - **Quote**: `arr[n].len = nl != NULL ? (jc_size)(nl - p) : (jc_size)strlen(p);`
   - **Explanation**: When `nl == NULL`, `strlen(p)` returns `size_t`, but the code casts it to `jc_size` (which may be `long`). If `jc_size` is signed and the string is very long, this could cause undefined behavior or incorrect length values.

2. **Line 118 — `lcs_middle()`**
   - **Issue**: Incorrect fallback recursion degrades to treating all lines as additions, losing the distinction between deletions and additions.
   - **Quote**: `return lcs_middle(old, new_, p, 0, mm + nn, ops, base); /* degrade */`
   - **Explanation**: When memory allocation fails, the fallback incorrectly passes `0` for old length (`mm`) and `mm + nn` for new length (`nn`). This means all original lines are treated as deletions (which were already emitted earlier in the function) plus additions for all new lines, resulting in incorrect diff output.

3. **Line 228 — `jc_diff_unified()`**
   - **Issue**: Off-by-one error in computing the `hi` boundary for context lines.
   - **Quote**: `int hi = i + context >= nops ? nops - 1 : i + context;`
   - **Explanation**: The condition `i + context >= nops` correctly identifies when the upper bound would exceed the array, but the logic should use `i + context >= nops - 1` or similar to avoid unnecessary `- 1` calculations. More critically, when `i + context == nops - 1`, the condition `>= nops` is false, so `hi = i + context = nops - 1`, which is correct. However, the formula is unnecessarily complex and error-prone.

4. **Line 116 — `lcs_middle()`**
   - **Issue**: Integer overflow in `cells` calculation before checking against `DIFF_LCS_CELL_CAP`.
   - **Quote**: `long cells = (long)(mm + 1) * (long)(nn + 1);`
   - **Explanation**: The multiplication `(mm + 1) * (nn + 1)` can overflow before being cast to `long`. This should cast each operand to `long` first before multiplication: `(long)(mm + 1) * (long)(nn + 1)` is correct, but the multiplication is performed in the original type first. Actually, the cast happens before multiplication due to C's usual arithmetic conversions, so this may be safe. Re-checking: `(long)(mm + 1)` is computed first, then `(long)(nn + 1)`, then multiplied — this is safe from overflow in the cast step but could still overflow in the multiplication if `mm` and `nn` are large enough. The `DIFF_LCS_CELL_CAP` check happens *after* this calculation, so overflow could occur before the check.

**Most Critical Defects:**
- Line 118: Incorrect fallback logic that corrupts diff output
- Line 64: Potential signed/unsigned mismatch causing incorrect line lengths
- Line 116: Integer overflow risk before the safety cap is checked


5. **Line 140 — `diff_ops_shrink()`**
   - **Issue**: The ops array is shrunk with `realloc` and a NULL return is ignored, so a failed shrink leaves `ops` dangling.
   - **Quote**: `ops = realloc(ops, (size_t)n * sizeof *ops);`
   - **Explanation**: `realloc` may fail even when shrinking; the old block is still valid then, but the code overwrites the pointer with NULL and frees nothing.
