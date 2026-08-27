# zigodot — concrete fix plans for the two defects

*Companion to `zigodot-jichi-review.md`. These are plans for **you or a jichi agent**
to execute in the zigodot repo — this analysis did not modify zigodot source
(out of scope). All line numbers are as of the review; re-confirm before editing.
The gate for both is `zig build test` + the parity corpus, under Zig 0.16 / zls.*

---

## Defect A — `TextBuffer` cursor safety (crash fix) — *do this first; low risk*

**Files:** `src/editor/script_editor.zig`.

**Problem.** `delete_char` (`:162`) guards `if (self.current_column >= self.lines.items.len) return;` — that compares a *column* against the *line count* (wrong dimension) and never bounds-checks `current_line` before `self.lines.items[self.current_line]` → out-of-range cursor panics. The same "assume `current_line` valid" pattern repeats in `move_backward` (`:212`), `move_to_end` (`:223`), `move_up` (`:231`), `move_down` (`:241`), all of which index `self.lines.items[self.current_line]` and also panic on an **empty buffer** (`lines.len == 0`).

**Fix (minimal → proper):**
1. In `delete_char`, replace the bogus guard with a real line-bounds check:
   `if (self.current_line >= self.lines.items.len) return;`
   The existing `if (self.current_column >= line.len) { …merge next line… }` branch is correct — keep it; just index through the now-bounds-checked `current_line`.
2. Establish the cursor invariant explicitly: `current_line < lines.items.len` (or `lines.len == 0` with cursor at 0/0) and `current_column <= lines.items[current_line].len`. Add a small private helper (e.g. `fn clampCursor(self: *TextBuffer) void`) and call it after every mutation, **or** add `std.debug.assert`s at method entry/exit (debug builds).
3. Audit the cursor readers — `move_backward`/`move_to_end`/`move_up`/`move_down` — to guard `current_line`/empty-buffer before indexing. `set_text` (`:120`) resets to 0/0 (already safe); check `insert_text` (`:158`) doesn't push `current_column` past the line length.

**Tests (add to the TextBuffer tests):**
- `delete_char` with `current_line` past the end → no panic, no-op.
- `delete_char` on an empty buffer → no panic.
- `delete_char` at end-of-line still merges the next line (preserve existing behavior).
- `delete_char` mid-line removes exactly one char.
- A fuzz-ish sequence (insert/move/delete) leaves the invariant intact.

**Risk:** low, localized; no semantic change for valid cursors. Immediate payoff: removes an editor crash.

---

## Defect B — Resource cache content-hashing — *do second; needs an ownership audit*

**Files:** `src/editor/resource_manager.zig` (`ResourceCache`, `:290–360`) **and `src/editor/index.zig` (`:557–578`)** — the same bug is duplicated.

**Problem.** Both use `std.HashMap([]const u8, *Resource, std.hash_map.AutoHasher([]const u8), 32)`. `AutoHasher` hashes the **slice header (ptr+len), not the bytes**, so a `get(path)` with a different pointer to the same string **always misses**. Consequences *today*:
- `getResource` always returns `null` → every load re-reads from disk (perf; likely the `resource_manager.zig` redo-loop).
- `addResource`'s dedup/replace branch is dead → it `put`s a fresh entry every call → **unbounded growth**.
- `deinit`/`clear` free only values, never the `dupe`d keys → **key leak**.
- `removeResource` always returns `false`.
- **`index.zig:568` is worse:** `put(path, resource)` stores the caller's slice **without `dupe`** → even with content hashing, the key can dangle when the caller frees `path`.

**Fix:**
1. Change both maps to `std.StringHashMap(*Resource)` (content hashing). Confirm the Zig 0.16 ctor/API (`.init(allocator)`, `getOrPut`, `fetchRemove`).
2. Rework key ownership (StringHashMap does **not** own keys):
   - **Insert:** `dupe` the key (resource_manager already does; **add `dupe` in `index.zig`**). Use `getOrPut` so the replace path reuses the existing key instead of dupe-and-leak: on found+replace-mode → `deinit`/`destroy` the old value, set the new one (keep the key); on not-found → store a `dupe`d key.
   - **`deinit`/`clear`:** iterate and free `entry.key_ptr.*` **and** `deinit`+`destroy` `entry.value_ptr.*`.
   - **`removeResource`:** `fetchRemove(path)` → if present, free the key and (decide per ownership, below) `deinit`+`destroy` the value; return whether it was present.
3. **Decide + document the ownership contract** (put it in the module comment): the cache **owns** cached `*Resource`s (its `deinit` destroys them), so callers **borrow** — they must not `deinit`/`destroy` a pointer they got from `getResource`.
4. **Callers audit (the subtle risk).** A *working* cache returns **shared** pointers; today every `getResource` is `null`→fresh-load, so a caller that frees its result has been "safe" only by accident and will now double-free / dangle. Before shipping:
   `grep -rnE "getResource|addResource|removeResource" src/` → for each caller, confirm it treats the result as borrowed (no free/`deinit`/`destroy`).
5. **Consider unifying the two caches.** Two near-identical cache structs (`resource_manager.zig` + `index.zig`) is a maintenance smell and itself a likely source of edit churn — fold `index.zig`'s into `ResourceCache` if they serve the same role, or at least share one corrected implementation.

**Tests (use `std.testing.allocator` — it fails on leaks):**
- Put under one `[]const u8`, `get` with a **different-pointer same-content** path → returns the resource (the regression for this bug).
- `addResource` twice for the same path in replace mode → old value freed, single entry.
- `removeResource(present)` → entry gone, key+value freed, returns true; `removeResource(absent)` → false.
- Construct + populate + `deinit` → **no leaks** reported by the test allocator (proves key-freeing).

**Risk:** medium. The type change is small; the *ownership* change is the real work and can surface latent double-free/use-after-free in callers. Lean on the test allocator + parity corpus. Big upside: removes a real leak + per-load disk re-reads.

---

## Sequencing, verification, and how to run it

- **Order:** Defect A first (contained crash fix, fast win), then Defect B (ownership audit). Don't bundle them in one commit.
- **Gate:** `zig build test` + the GDScript/parity corpus after each. For Defect B, confirm the test allocator reports no leaks.
- **Execution options:**
  - **Hand-fix** with the steps above, or
  - **Dogfood through jichi** (recommended, and it re-exercises the exact files that caused the redo-loops): run the `zig-implementer`/`/port`-style flow scoped to one file at a time, with `zig build test` as the `verify` gate. First `./jichi learn apply` the reviewed `lessons.draft.md` so the agent carries the two gotchas (cursor invariant; StringHashMap key ownership) into the fix.
- **Rollback:** jichi snapshots (`/undo`) or git; the parity tests are the safety net. These are localized changes — easy to revert if a callers-audit surprise appears.

*Caveat: the `*std.mem.Allocator`-by-pointer style used throughout these files is unusual for Zig 0.16 (allocators are normally passed by value); it apparently builds, so leave it unless the compiler objects during the edit — just don't widen it.*
