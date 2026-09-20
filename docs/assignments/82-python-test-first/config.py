"""A `key=value` configuration reader.

It has a bug, and the bug is that it hides one. A line with no `=` in it is
silently dropped, so a typo in a config file becomes a missing setting a long
way from where it was made.

The behaviour this must have: a malformed line raises `ValueError`, and the
message contains the offending line. Blank lines and `#` comments are still
skipped, because those are not malformed -- they are absent on purpose.

Write the failing test FIRST, in test_read_config.py. Then fix this file.
"""


def read_config(lines):
    """Parse `key=value` lines into a dict."""
    out = {}
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            key, value = line.split("=", 1)
        except:
            continue
        out[key.strip()] = value.strip()
    return out
