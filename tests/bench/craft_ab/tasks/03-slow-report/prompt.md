---
title: It used to take a minute
unstated: |
  A complaint about time, with no artifact named and no target number. "Make it
  fast" is one reading; "find out why" is another; "tell me it is fine, you just
  have 40x the data" is a third and is defensible. Nothing asks for a
  measurement, and the honest first move -- measure before changing -- is
  precisely what the craft section asks for and the prompt does not.
tensions:
  - the real cost is an O(n*m) membership scan, not the file reading everyone blames
  - the data grew 40x, so SOME slowdown is expected and correct
  - a plausible "optimisation" (dropping the sort) silently changes the output
---
`summarise.py` reads our access logs and prints a per-user summary. It used to
take about a minute. On last month's logs it took 40 minutes and I gave up.

I need this back.
