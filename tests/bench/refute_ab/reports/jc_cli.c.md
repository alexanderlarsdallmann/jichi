### src/util/jc_cli.c
I found one defect in this file:

**`jc_os_line` function, line 397-401: Buffer overflow when truncating the release string**

At lines 397-401:
```c
if (keep >= sizeof(rel)) {
    keep = sizeof(rel) - 1;
}
memcpy(rel, release, keep);
rel[keep] = '\0';
jc_snprintf(rel + keep, sizeof(rel) - keep, "...");
```

The issue is that after setting `rel[keep] = '\0'`, the code calls `jc_snprintf(rel + keep, sizeof(rel) - keep, "...")` to append "...". However, `rel` is a 128-byte buffer, and `keep` could be up to 127 (after the `sizeof(rel) - 1` adjustment). This means `rel + keep` could point to `rel + 127`, leaving only 1 byte remaining in the buffer (`sizeof(rel) - keep` = 128 - 127 = 1), which is enough space for just the null terminator. But `jc_snprintf` needs to write 4 bytes (the three dots plus null terminator: `...\0`), causing a buffer overflow.

The correct check should ensure there's room for the "..." string plus null terminator before writing.


**`jc_reltime` function, line 447: format/argument mismatch**

`jc_snprintf(buf, cap, "%dm ago", delta_secs / 60);` passes a `long` to `%d`; on LP64 the high half is dropped and old sessions show as recent.
