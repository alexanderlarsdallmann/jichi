### src/util/jc_progress.c
After carefully reviewing `src/util/jc_progress.c`, I found the following defects:

1. **`jc_progress_append` (line 161)**: Memory leak. When `cJSON_PrintUnformatted(o)` succeeds but `fopen` fails, the code correctly frees `line`, but when `cJSON_PrintUnformatted(o)` returns NULL, the function returns early without freeing `line`. However, the issue is actually that `cJSON_PrintUnformatted` allocates memory that must be freed, and if `fopen` fails after `line` is assigned, the code does free it - so this isn't a leak. Let me re-examine more carefully.

Actually, the defect is in **`jc_progress_append` (line 161-164)**: If `cJSON_PrintUnformatted(o)` returns NULL (which can happen), `line` is NULL, the function returns `JC_ERR_OOM`, but `o` was already deleted at line 162. This is correct behavior.

Let me look for actual bugs:

1. **`jc_progress_append` (line 161)**: The code calls `cJSON_PrintUnformatted(o)` and stores the result in `line`. If this succeeds, it then deletes `o` at line 162. However, if `fopen` at line 166 fails, the code frees `line` and returns. This is correct.

2. **`jc_progress_append` (line 171)**: The code uses `fprintf(f, "%s\n", line);` without checking the return value. If the write fails, the function doesn't detect it.

3. **`jc_progress_hint_append` (line 211)**: Same issue - no check of `fprintf` return value.

4. **`predict_write` (line 296)**: Same issue - no check of `fprintf` return value.

5. **`jc_progress_predict_resolve` (line 379)**: The code calls `jc_read_file` and checks if it returns `JC_OK`, but if it doesn't, `text` is set to NULL. Then at line 382, `jc_progress_predict_scan` is called with `text` which may be NULL. Looking at `jc_progress_predict_scan`, it checks `if (jsonl == NULL)` and returns early, so this is safe.

6. **`jc_progress_scan` and `jc_progress_hints_scan`**: Both have the same pattern - they pass a buffer to `scan_line`/`scan_hint_line`. The buffer is allocated with `malloc(len + 1)`. If `len` is close to `SIZE_MAX`, the allocation could overflow. However, `len < 4096` is checked, so this is unlikely.

After careful analysis, the main defect is:

**`jc_progress_append` (line 171)**: No error checking on `fprintf`. If the write to file fails, the function doesn't detect it and returns `JC_OK`.

**`jc_progress_hint_append` (line 211)**: Same issue - no error checking on `fprintf`.

**`predict_write` (line 296)**: Same issue - no error checking on `fprintf`.

Actually, let me check the return types and error handling more carefully:

Looking at all three functions, they all call `fprintf(f, "%s\n", line);` and then `free(line); fclose(f);` without checking if `fprintf` succeeded. If `fprintf` fails, it returns a negative value, but the code doesn't check this.

The proper fix would be to check the return value of `fprintf` and return an error if it fails.

Here are the findings:

1. **`jc_progress_append` (line 171)**: No error checking on `fprintf`. The function should check if `fprintf` returns a negative value and return `JC_ERR_IO` if so. Quoted line: `fprintf(f, "%s\n", line);`

2. **`jc_progress_hint_append` (line 211)**: Same issue. Quoted line: `fprintf(f, "%s\n", line);`

3. **`predict_write` (line 296)**: Same issue. Quoted line: `fprintf(f, "%s\n", line);`


4. **`jc_progress_rewrite` (line 340)**: Rewrites `progress.jsonl` with `fopen(path, "w")`, truncating the learner's whole record before the new content is written -- an interrupted rewrite loses every attempt ever recorded. Quoted line: `f = fopen(path, "w");`
