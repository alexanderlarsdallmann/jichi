### src/util/jc_rss.c
Here are the defects I found in `src/util/jc_rss.c`:

---

**1. Buffer overflow in `field()` — line 64-70**
```c
char open[24];
...
char closebuf[24];
```
The buffers `open` and `closebuf` are fixed at 24 bytes, but on **line 78** and **line 102**, `strcpy` is used to copy into them without verifying that the tag plus prefix (`<` + tag + null terminator) fits:
```c
strcpy(open + 1, tag);         // line 78
strcpy(closebuf + 2, tag);     // line 102
```
**Problem:** `strlen(tag)` could exceed 22 characters (24 minus `<` and `\0`), causing a buffer overflow.

**Fix suggestion:** Use `snprintf` instead of `strcpy`, or increase buffer size.

---

**2. Signed/unsigned mismatch and potential overflow in `field()` — line 81**
```c
char after = o[1 + (int)strlen(tag)];
```
Casting `strlen` to `int` truncates on 64-bit platforms and could wrap if the tag length is huge. Also, if `strlen(tag)` is large enough, `1 + (int)strlen(tag)` could overflow.

**Fix:** Use `size_t` consistently and perform bounds checking.

---

**3. Off-by-one / potential out-of-bounds read in `field()` — line 81**
```c
char after = o[1 + (int)strlen(tag)];
```
If `o` points within 1+`strlen(tag)` bytes of `end`, this reads past the end of the buffer.

**Fix:** Ensure `o + 1 + strlen(tag) < end` before reading.

---

**4. Memory leak in `emit_field()` — line 163**
```c
jc_sb_free(&v);
return got;
```
If `jc_sb_init(&v)` succeeds but `jc_sb_free(&v)` fails to release memory (e.g., due to bug in `jc_sb_free`), or if the caller doesn’t expect `jc_sb` resources to be managed here, this could leak. More importantly:

If `emit_field()` returns early due to an internal error, `jc_sb_free(&v)` may be skipped.

**However**, reviewing line 163, `jc_sb_free` is called unconditionally *before* `return got`, so no leak exists *in this function* — but the design is fragile and `jc_sb` cleanup should be verified in the `jc_sb` implementation.

---

**5. Off-by-one / undefined behavior in `trim()` — line 47-53**
```c
while (*s < *e && (**s == ' ' || **s == '\t' || **s == '\r' ||
                   **s == '\n')) (*s)++;
while (*e > *s) {
    char c = (*e)[-1];
    ...
```
If `*e == *s`, the second `while` loop is safe, but the first loop could increment `*s` past `*e`. The condition `*s < *e` protects against that.

**However**, there’s a subtle bug: if `*s == *e` *and* the entire range is whitespace, `(*s)` could be incremented past `*e`. This leads to `*s > *e`, which breaks the invariant. The check should be `*s <= *e - 1` or `*s + 1 <= *e`.

Actually, `*s < *e` is correct — but after trimming leading whitespace, if `*s == *e`, the second `while` loop is skipped, which is fine.

Still, the `trim()` function does not guard against `*e == NULL` or `*s == NULL`, which could be passed in.

---

**6. Missing bounds check before dereference in `field()` — line 96**
```c
if (gt > o && gt[-1] == '/') {
```
`gt` is set on line 90:
```c
gt = o;
while (gt < end && *gt != '>') gt++;
```
If the loop exits because `gt == end`, `gt[-1]` is one past the last valid byte (i.e., `*(end-1)`), which is valid only if `end > o`. If `o == end`, then `gt == end`, and `gt[-1]` is out of bounds.

But since `gt = o` and the loop increments `gt`, if `o == end`, then `gt == end`, and `gt[-1]` is `*(end-1)`, which is only valid if `end > 0`. However, if `end == o` and `o == NULL`, dereferencing could crash.

**Actually**, the function already checks `if (o == NULL) return 0;` on line 88, so `o != NULL`, but `end` could be equal to `o`, making `gt[-1]` undefined if `gt == o`.

The safer fix is to check `gt > o` *and* ensure `gt > start` before accessing `gt[-1]`.

---

**7. Memory safety in `field()` — line 120**
```c
jc_docs_html_to_text(tmp.data != NULL ? tmp.data : "", dst);
```
This is defensive, but the `tmp.data` pointer is set by `jc_sb_append_n()` on line 119. If `jc_sb_append_n()` failed to allocate, `tmp.data` may still be `NULL`, and `jc_docs_html_to_text("", dst)` will be called.

**This may be OK**, but it depends on what `jc_docs_html_to_text()` does with an empty string. If it expects a valid string with a length, passing `""` without length is ambiguous.

Also, `jc_sb_init(&tmp)` is not visible here — verify it initializes `tmp.data = NULL`.

---

**8. Potential integer overflow in `jc_rss_to_text()` — line 195**
```c
end = xml + strlen(xml);
```
If `xml` is huge (> SIZE_MAX), `strlen(xml)` could overflow. But realistically, this won’t happen on a 64-bit system with valid memory.

Still, for safety, consider using a maximum safe length (like on line 174).

---

**9. Missing null check in `jc_rss_to_text()` — line 259**
```c
jc_sb_append(out, "");
```
If `out == NULL`, this will crash. But line 194 already checks `if (xml == NULL || out == NULL) return;`, so this is safe.

However, `jc_sb_append(out, "")` is redundant — it appends an empty string. This could be removed.

---

**10. Off-by-one in `field()` CDATA check — line 111**
```c
if (ie - is >= 12 && ci_prefix(is, "<![CDATA[")) {
```
The literal `<![CDATA[` is 9 characters, not 12. So this should be:
```c
if (ie - is >= 9 && ci_prefix(is, "<![CDATA[")) {
```
Or better, use `sizeof("<![CDATA[") - 1` to avoid hardcoding.

Actually, 9, but the code uses 12 — that's fine (it's >= 9), but the comment says ">= 12", which is misleading.

---

**11. Potential use-after-free in `field()` — line 120**
```c
jc_sb_init(&tmp);
jc_sb_append_n(&tmp, is, (jc_size)(ie - is));
jc_docs_html_to_text(tmp.data != NULL ? tmp.data : "", dst);
jc_sb_free(&tmp);
return 1;
```
After `jc_sb_free(&tmp)`, the memory pointed to by `tmp.data` is freed. If `jc_docs_html_to_text()` stores the pointer instead of copying it, that's a use-after-free.

Since `jc_docs_html_to_text()` likely just reads and processes the string immediately (i.e., doesn’t retain the pointer), this may be safe — but it’s fragile and depends on implementation details of `jc_docs_html_to_text()`. If `jc_docs_html_to_text()` were to retain the string, this would be a bug.

**Recommendation:** Pass the length explicitly (e.g., `(jc_size)(ie - is)`) to avoid reliance on `tmp.data` after `jc_sb_free()`.

---

**12. Inconsistent behavior in `emit_field()` — line 154**
```c
if (got && v.len > 0) {
```
If `field()` returns 1 but `v.len == 0`, nothing is appended — that's intentional (e.g., `<title></title>`). However, this means `emit_field()` may return `got == 1` even though no output was produced.

**This could mislead callers** — e.g., in `jc_rss_to_text()`:
```c
has_title = emit_field(open, item_end, "title", "", out);
if (!has_title) {
    jc_sb_append(out, "(untitled)");
}
```
If `<title></title>` exists, `has_title` will be `1`, and the item won’t be marked "(untitled)" — this is likely intended. But the semantics of `emit_field()` returning `got` when no text was emitted is subtle.

**Maybe OK**, but worth documenting.

---

**13. Integer overflow in `RSS_DESC_MAX` truncation — line 156-157**
```c
if (v.len > RSS_DESC_MAX) {
    jc_sb_append_n(out, v.data, RSS_DESC_MAX);
```
If `RSS_DESC_MAX` is larger than `v.len`, this is fine. But `RSS_DESC_MAX` is 1200 bytes. If `v.len` is near `SIZE_MAX`, truncating to 1200 is safe — but the comparison `v.len > RSS_DESC_MAX` is OK for typical feed sizes.

No actual defect here — just noting that this assumes `RSS_DESC_MAX <= v.len <= SIZE_MAX`, which is always true.

---

**14. Potential infinite loop in `jc_rss_to_text()` — line 272**
```c
p = item_end + 1;
n++;
```
If `item_end == end`, then `p = end + 1`, which is past the end of the buffer. On the next iteration, `ci_find(p, end, ...)` is called with `p > end`, which could lead to undefined behavior or infinite loop.

**Fix**: If `item_end == end`, break.

Actually, `ci_find(start, end, ...)` uses the condition `p + nlen <= end`, so if `p > end`, the loop never runs — so it won’t loop forever, but the behavior is undefined.

**Safer**:
```c
if (item_end >= end) break;
p = item_end + 1;
```

Or:
```c
p = item_end;
if (p < end) p++;
```

---

**15. Hardcoded buffer size in `field()` — line 76**
```c
if (strlen(tag) + 2 >= sizeof(open)) return 0;
```
This checks that `tag` fits in `open`, but `sizeof(open) == 24`, and the buffer layout is:
- `open[0] = '<';`
- `strcpy(open + 1, tag);`
- Then `open[1 + len(tag)]` gets the null terminator.

So we need `1 + strlen(tag) + 1 <= 24` → `strlen(tag) <= 22`.

The check `strlen(tag) + 2 >= sizeof(open)` is correct — but it returns 0 if `>=`, meaning `tag` is too long. The condition should be `if (strlen(tag) + 2 > sizeof(open)) return 0;` — with strict `>`.

**Wait**, let’s verify:
- `sizeof(open) = 24`
- Need space for `<` + tag + `\0`
- So space needed = `1 + strlen(tag) + 1 = strlen(tag) + 2`
- If `strlen(tag) + 2 == 24`, it fits (uses indices 0..23).
- If `strlen(tag) + 2 > 24`, it overflows.

So the check `if (strlen(tag) + 2 >= sizeof(open))` is incorrect — it should be `>`.

Example: `strlen(tag) = 22` → `22 + 2 = 24 >= 24` → returns 0, even though it fits.

**Fix**: Change `>=` to `>`.

---

**16. Missing null check in `trim()` — line 45-54**
The function receives `const char **s` and `const char **e`, and dereferences them:
```c
while (*s < *e && (**s == ' ' || ...)) (*s)++;
```
If `*s == NULL` or `*e == NULL`, this will crash.

**Fix**: Add null checks at the start:
```c
if (s == NULL || e == NULL || *s == NULL || *e == NULL) return;
```

---

**17. Potential integer overflow in length calculation — line 119**
```c
jc_sb_append_n(&tmp, is, (jc_size)(ie - is));
```
If `ie < is`, then `ie - is` is negative, and casting to `jc_size` (unsigned) will wrap to a huge number.

**Fix**: Ensure `ie >= is` before calling `jc_sb_append_n()`.

This is protected by `if (ie > is)` on line 117, so `ie - is > 0`, and `ie >= is + 1`. But there’s no check if `ie < is`.

Actually, since `ie` and `is` are derived from `inner` and `inner_end`, and `inner_end >= inner`, this should be safe — unless `trim()` or the CDATA logic modifies them in unexpected ways.

After trimming, `is <= ie` is maintained by the `trim()` function, but it’s still risky.

**Better**: Add an assertion or conditional check.

---

### Summary of Critical Issues:
- **Line 76**: `>=` should be `>` for buffer size check.
- **Line 81**: Off-by-one / out-of-bounds read risk.
- **Line 272**: Potential `p > end` after `item_end = end`.
- **Line 111**: `12` should be `9` for `<![CDATA[` prefix.
- **Line 45–54**: `trim()` lacks null pointer checks.

Let me now generate a clean final report.


---

**7. Length underflow in `jc_rss_to_text()` — line 312**
```c
jc_sb_append_n(out, s, (jc_size)(e - s));
```
When an item has no `<title>`, `e` points before `s`, the subtraction wraps, and the append reads far past the buffer.
