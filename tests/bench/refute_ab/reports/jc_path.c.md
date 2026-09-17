### src/util/jc_path.c
Here are the defects I found in `src/util/jc_path.c`:

1. **Line 181-182**: In `resolve_depth()`, `len = strlen(path);` followed by `if (len + 1 > sizeof(work))` may overflow when computing `len + 1` if `path` is extremely long (SIZE_MAX-1). The comparison should use `len >= sizeof(work)` to avoid potential integer overflow.

2. **Line 187-188**: In `resolve_depth()`, the trailing slash stripping loop:
   ```c
   while (len > 1 && work[len - 1] == '/') {
       work[--len] = '\0';
   }
   ```
   This modifies the `work` buffer in place, but the original `path` pointer passed to `resolve_depth` may point to static/readonly memory. The copy to `work` happens after this check on line 185.

3. **Line 221-222**: In `resolve_depth()`, when handling relative symlink targets:
   ```c
   char *slash = strrchr(work, '/');
   size_t dlen = (slash == NULL) ? 0 : (size_t)(slash - work);
   ```
   The `strrchr` is called on `work` which may not be null-terminated if the original path was truncated. The copy on line 185 includes the null terminator, so this is safe, but the comment on line 186-189 says "Strip a single trailing slash" which doesn't match the actual behavior (it strips all trailing slashes).

4. **Line 231-232**: In `resolve_depth()`, when handling relative symlink targets:
   ```c
   if (dlen == 0) {
       joined[dlen++] = '/'; /* the link sat in "/" */
   }
   ```
   The comment is misleading. If `dlen == 0`, it means there was no '/' in `work`, which means the link is in the current directory (not "/"). The code correctly adds a '/' but the comment is wrong.

5. **Line 259**: In `resolve_depth()`, when splitting parent/leaf:
   ```c
   *slash = '\0';
   ```
   This modifies the `work` buffer in place, overwriting the original path data. Since `work` is a local copy, this is safe but destructive - the original path is lost after this point.

6. **Line 270**: In `resolve_depth()`, the buffer size check:
   ```c
   if (plen + 1 + llen + 1 > cap) {
   ```
   This computes `plen + 1 + llen + 1` which could overflow if `plen` or `llen` are very large. Should check each component separately or use a safer comparison.

7. **Line 154-156**: The `resolve_depth` function signature uses `jc_size cap` but then compares against `sizeof(work)` (size_t) at line 182. Type mismatch could cause issues on 32-bit vs 64-bit platforms.

8. **Line 169**: In `resolve_depth()`, `realpath(path, resolved)` is called with `path` which may contain null bytes if the input was malformed. The function checks for empty strings but not for internal null bytes.

9. **Line 223**: In `resolve_depth()`, calculating needed size for joined path:
   ```c
   size_t need = dlen + 1 + (size_t)tl + 1;
   ```
   The cast `(size_t)tl` is redundant since `tl` is already `ssize_t` and the comparison will promote it. More importantly, if `tl` is negative (readlink error), the cast to size_t will make it very large, but this is caught earlier at line 211.

10. **Line 289**: In `jc_path_in_root()`, using `JC_PATH_MAX` for the resolved buffer may be too small if `PATH_MAX > JC_PATH_MAX`. The code should use `JC_RESOLVE_BUF` instead to be consistent with `resolve_depth()`.


11. **Line 47**: In `jc_path_under_root()`, the prefix test is inverted: `if (strncmp(path, root, rlen) == 0) { return 0; }` rejects every path that IS under the root and accepts every path that is not.
