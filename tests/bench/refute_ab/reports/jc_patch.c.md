### src/util/jc_patch.c
## Most Critical Defects (Summary)

**1. Memory leak in `patch_lines()` (lines 157-162)** - The function leaks memory when only one of two allocations fails.

**2. Memory leak in `jc_patch_nearmatch_hint()` (lines 473-476)** - Missing `free()` calls before early return when `best_score == 0`.

**3. Uninitialized `*ml` in `patch_locate_fuzzy()` (lines 250-254)** - When tier 1 finds matches, `*ml` is never set before returning.

**4. Off-by-one/indexing error in `patch_locate_fuzzy()` (line 225)** - Accessing `noff[nN-1]` after `nN` has been decremented could cause out-of-bounds access.

**5. Potential buffer overflow in `patch_locate_fuzzy()` (line 290)** - Computing `end_byte = hend[last] + 1` could exceed buffer bounds.

**6. Missing NULL check in `jc_patch_build()` (line 96)** - `strlen(old_s)` is called without verifying `old_s` is non-NULL.

**7. Missing memory cleanup in `jc_patch_nearmatch_hint()` (lines 473-476)** - No `free()` before the return when `best_score == 0`.

These defects could lead to memory corruption, undefined behavior, crashes, or security vulnerabilities.


**8. Unchecked write in `patch_backup()` (line 330)** - The backup copy written before a patch is applied ignores `fwrite`'s return value, so a full disk leaves a truncated backup that a later rollback trusts.
