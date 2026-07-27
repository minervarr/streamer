# Search exact-match operator + multi-column sort

Date: 2026-07-27

## Problem

Two GUI (`gui/`) usability gaps found while searching `artist:"poppy"`:

1. `artist:"poppy"` does a *contains* match, so it returns every artist whose
   name contains "poppy" (Poppy Ajudha, Poppy Ackroyd, ...) with no way to ask
   for an artist named exactly "Poppy".
2. Clicking a results-table header replaces the active sort column entirely.
   Sorting by Artist then clicking Duration loses the artist grouping instead
   of using Duration as a tie-breaker within it.

## 1. Exact-match search operator

`gui/src/query_dsl.{hh,cc}` gains a new operator token, valid only
immediately after `field:`, alongside the existing `>`, `<`, `>=`, `<=`:

- `field:value` — contains (unchanged).
- `field:=value` — exact, case-insensitive whole-string match.

Applies to every currently-`ContainsI`-matched field: `title`, `artist`,
`album`, `genre`, `label`, `country`, `type`. A new `EqualsI` helper
(same UTF-8 decode + `ToLowerStr` fold as `ContainsI`, full-string `==`
instead of `find`) backs it.

`year`/`duration` (numeric) and `hires`/`explicit` (boolean) already do exact
equality on bare `:` — `=` is accepted there as a harmless synonym via the
existing default branch in `MatchFilter`, no extra code path needed.

Grammar addition (`Parser::parseAtom`, right after the `>`/`<`/`>=`/`<=` checks):

```
else if (at(TokKind::Eq)) { n->filter.op = "="; consume(); }
```

Tokenizer: `=` not already followed by nothing special today (only checked
inside `>=`/`<=`) becomes its own `TokKind::Eq` single-char token.

Docs to update: the cheatsheet panel text in `gui/src/views.cc`
(`draw_search`'s `lines[]`) and the operator list in `query_dsl.hh`'s header
comment.

## 2. Multi-column stacked sort

`gui/src/search_controller.hh`'s single `sort_col_`/`sort_asc_` pair becomes
an ordered stack:

```cpp
struct SortKey { SortColumn col; bool asc; };
std::vector<SortKey> sort_keys_;  // index 0 = primary, later = tie-breakers
```

`sort(col)` cycles per-column state without touching other keys' order:

- `col` not in `sort_keys_` → append at the end (lowest priority), `asc = true`.
- `col` in `sort_keys_`, currently ascending → flip to descending, same slot.
- `col` in `sort_keys_`, currently descending → erase that entry (3rd click
  on a column clears just that key).

This is a per-column 3-state cycle (ascending → descending → cleared),
generalizing today's single-column toggle to N independently-cycling keys.
The column clicked *first* stays primary for as long as it's in the stack —
clicking a different column only ever appends/cycles, never reorders
existing keys. Removing the primary key promotes the next entry.

A fresh `search()` call still clears `sort_keys_` entirely (today's
`sort_col_ = SortColumn::None` reset becomes `sort_keys_.clear()`).

`applySort()` becomes one `stable_sort` whose comparator walks `sort_keys_`
in order, returning on the first key that disagrees (lexicographic
comparison over the key list) — this is what makes Artist stay the grouping
while Duration only breaks ties inside each artist's rows.

Public accessors change from `sort_column()`/`sort_ascending()` to something
like `sort_keys()` (returns `const std::vector<SortKey>&`) plus a small
helper `sort_state_for(SortColumn) -> {absent, ascending, descending}` and
`sort_priority_for(SortColumn) -> int` (0-based rank, -1 if absent) so the
view layer doesn't need to linear-scan the stack itself repeatedly.

### Visual feedback (`gui/src/views.cc`)

`drawSortableTable` (vendored in the `Vk_Canvas_Lb_LAW` submodule, consumed
by other apps per that repo's own CLAUDE.md) only draws one glyph for one
column and stays untouched — no submodule changes for a streamer-only
feature.

- The widget keeps being called with the **primary** key (`sort_keys_[0]`)
  exactly as today, so the common single-key case is visually unchanged.
- When `sort_keys_.size() >= 2`, `views.cc` draws small rank badges ("2",
  "3", ...) with their own up/down triangle over the other active header
  cells itself, reusing the header rects it already computes (`liveCols`)
  in the resize-boundary block. No widget/engine API changes.

## Out of scope

- No cap on stack depth (bounded naturally by the 10 sortable columns).
- No timing-based click detection — the 3-state cycle needs no timestamps.
- No changes to `widgets::drawSortableTable`'s signature.
