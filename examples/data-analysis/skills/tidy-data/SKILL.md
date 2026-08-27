---
name: tidy-data
description: How to shape a dataset so it is easy and safe to analyze — one variable per column, one observation per row.
---
# Tidy data

Most analysis pain comes from badly-shaped data, not hard statistics. "Tidy"
data (Hadley Wickham's term) has a simple rule set that makes everything after it
easier:

1. **Each variable is a column.** Not "2023" and "2024" as two columns — a `year`
   column and a `value` column.
2. **Each observation is a row.** One measurement per row; no row that secretly
   holds two.
3. **Each type of observational unit is its own table.** Don't mix people and
   their orders in one sheet; relate two tidy tables by a key.

**Common messes and the fix:**

- *Wide when it should be long* (a column per month) → melt/`pandas.melt` into
  `key` + `value` columns.
- *Values in column headers* (a `male`/`female` column pair) → a `sex` column.
- *Multiple values in one cell* (`"5 kg, 2023"`) → split into typed columns.
- *Inconsistent categories* (`"NY"`, `"N.Y."`, `"new york"`) → normalize to one.

A tidy table is one where you can answer "group by X, summarize Y" without
reshaping first. Reshape once, up front; every later step gets simpler.
