"""A tiny tag collector.

One of the tests in test_tags.py fails. The bug is in THIS file; the test
module is the truth. Fix the function, not the test.
"""


def add_tag(tag, tags=[]):
    """Append `tag` to `tags` and return the list.

    Called with no list, it should start a fresh one each time.
    """
    tags.append(tag)
    return tags
