"""Three small transformations, all written as accumulator loops.

Every one of them is green. Nothing here is broken -- that is the point: this
is a refactor under passing tests, so the tests are your safety net rather
than your task. Change HOW, never WHAT.

Rewrite all three as comprehensions (list, dict, set). The suite must stay
green, and the grader also checks the loops are actually gone.
"""


def even_squares(numbers):
    """The squares of the even numbers, in order."""
    out = []
    for n in numbers:
        if n % 2 == 0:
            out.append(n * n)
    return out


def name_lengths(names):
    """A mapping from each name to its length."""
    out = {}
    for name in names:
        out[name] = len(name)
    return out


def unique_initials(names):
    """The set of first letters, upper-cased."""
    out = set()
    for name in names:
        out.add(name[0].upper())
    return out
