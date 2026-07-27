# Search exact-match operator + multi-column sort — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `field:=value` exact-match operator to the GUI's search DSL, and turn the results-table header click into a multi-column stacked sort (secondary columns break ties instead of replacing the primary sort), with a 3-click-per-column cycle (ascending → descending → cleared) and no wall-clock timing.

**Architecture:** Two independent, self-contained changes inside `gui/src/`, no changes to the vendored `Vk_Canvas_Lb_LAW` engine. (1) `query_dsl.cc` gains one new token/operator threaded through the existing tokenizer → parser → `MatchFilter` pipeline. (2) `search_controller.{hh,cc}` replaces its single active sort column with an ordered stack of `SortKey{col, asc}`, exposed as two pure free functions (`CycleSortKey`, `ApplySort`) so the sort logic is unit-testable head­lessly with hand-built data — no network/account needed, unlike `SearchController` itself. `views.cc` reads the stack to draw a rank badge over any secondary sort columns.

**Tech Stack:** C++17, the project's existing `check()`/`run_selftest()` assertion harness in `gui/src/gui_main.cc` (built and run via `build/linux_native/gui/streamer_gui --selftest`, already configured with `-DSTREAMER_GUI=ON -DVCE_SLANGC=/opt/shader-slang-bin/bin/slangc`).

## Global Constraints

- No changes to `framework/Vk_Canvas_Lb_LAW` (vendored engine submodule) — it's consumed by other apps; see its own CLAUDE.md.
- No wall-clock/timestamp-based click detection — the reset gesture is a pure per-column state cycle (spec: `docs/superpowers/specs/2026-07-27-search-sort-design.md`).
- Every new behavior gets a headless `check()` assertion in `run_selftest()` before it's considered done — this project has no other GUI test harness.
- Build with: `cmake --build build/linux_native --target streamer_gui -j$(nproc)`. Run tests with: `build/linux_native/gui/streamer_gui --selftest`.

---

### Task 1: `field:=value` exact-match operator in the query DSL

**Files:**
- Modify: `gui/src/query_dsl.hh:11-15` (header comment — operator list)
- Modify: `gui/src/query_dsl.cc:136-137` (tokenizer `TokKind` enum)
- Modify: `gui/src/query_dsl.cc:141-198` (`Tokenize`, add the `=` branch)
- Modify: `gui/src/query_dsl.cc:92-97` (add `EqualsI` next to `ContainsI`)
- Modify: `gui/src/query_dsl.cc:281-286` (`Parser::parseAtom`'s operator branch)
- Modify: `gui/src/query_dsl.cc:313-323` (`MatchFilter`'s string-field branches)
- Modify: `gui/src/views.cc:152` (cheatsheet panel operator line)
- Modify: `gui/src/gui_main.cc:47-51` (add a check-count so the printed total never needs hand-maintaining again)
- Modify: `gui/src/gui_main.cc:70-73` (new selftest assertions, right after the existing quoted-filter block)
- Modify: `gui/src/gui_main.cc:132-135` (print the counted total instead of the hardcoded `19`)

**Interfaces:**
- Consumes: nothing new from other tasks.
- Produces: `query_dsl::MatchFilter` now understands `f.op == "="` for `title`/`artist`/`album`/`genre`/`label`/`type`/`country`. Task 2/3 don't depend on this.

- [ ] **Step 1: Write the failing selftest assertions**

In `gui/src/gui_main.cc`, right after the existing block:
```cpp
    // Quoted filter value, accent preserved.
    check(Match(Parse("artist:\"Luísa Sonza\""), luisa), "artist:\"Luísa Sonza\" matches");
    check(!Match(Parse("artist:\"Anitta\""), luisa), "artist:\"Anitta\" does not match");
```
add:
```cpp

    // Exact-match operator '=': field:value stays "contains", field:=value
    // is exact/case-insensitive — the bug this fixes is artist:"poppy"
    // matching both "Poppy" and "Poppy Ajudha" with no way to ask for just
    // the former.
    SearchResult poppy;
    poppy.title = "Poppy";
    poppy.artist = "Poppy";
    poppy.type = "artist";

    SearchResult poppyAjudha;
    poppyAjudha.title = "Trouble";
    poppyAjudha.artist = "Poppy Ajudha";
    poppyAjudha.type = "track";

    check(Match(Parse("artist:poppy"), poppy), "artist:poppy (contains) matches 'Poppy'");
    check(Match(Parse("artist:poppy"), poppyAjudha), "artist:poppy (contains) also matches 'Poppy Ajudha'");
    check(Match(Parse("artist:=poppy"), poppy), "artist:=poppy (exact) matches 'Poppy'");
    check(!Match(Parse("artist:=poppy"), poppyAjudha), "artist:=poppy (exact) does not match 'Poppy Ajudha'");
    check(Match(Parse("artist:=\"Luísa Sonza\""), luisa), "artist:=\"Luísa Sonza\" (exact, quoted) matches the full name");
    check(!Match(Parse("artist:=Luísa"), luisa), "artist:=Luísa (exact, partial) does not match 'Luísa Sonza'");
```

Also replace the fail-counter block at the top of the file:
```cpp
int g_fail_count = 0;

void check(bool cond, const char* what) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail_count; }
}
```
with:
```cpp
int g_fail_count = 0;
int g_check_count = 0;

void check(bool cond, const char* what) {
    ++g_check_count;
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail_count; }
}
```
and the final report:
```cpp
    if (g_fail_count == 0) {
        std::printf("selftest: ok (%d assertions)\n", 19);
        return 0;
    }
```
with:
```cpp
    if (g_fail_count == 0) {
        std::printf("selftest: ok (%d assertions)\n", g_check_count);
        return 0;
    }
```
(This removes the hand-maintained magic number `19` — every future `check()` call is counted automatically.)

- [ ] **Step 2: Build and run to verify the new assertions fail**

```bash
cmake --build build/linux_native --target streamer_gui -j$(nproc)
build/linux_native/gui/streamer_gui --selftest
```
Expected: builds clean (the `=` syntax doesn't exist yet, so `Parse` silently ignores it and treats these as plain contains — the exact-match assertions should print `FAIL: artist:=poppy (exact) does not match 'Poppy Ajudha'` and the one right below it, since with no `=` support `artist:=poppy` degrades to matching everything containing "poppy").

- [ ] **Step 3: Implement the `=` operator**

In `gui/src/query_dsl.cc`, add the token kind:
```cpp
enum class TokKind { Word, Quoted, Colon, Gt, Lt, Gte, Lte, Dash, Eq,
                     And, Or, Not, LParen, RParen, Eof };
```

In `Tokenize`, right after the `Dash` branch:
```cpp
        if (c == U'-') { toks.push_back({TokKind::Dash,   "-"}); ++i; continue; }
        if (c == U'=') { toks.push_back({TokKind::Eq,     "="}); ++i; continue; }
```

Add `EqualsI` right after `ContainsI`:
```cpp
bool EqualsI(const std::string& a, const std::string& b) {
    return ToLowerStr(Utf8Decode(a)) == ToLowerStr(Utf8Decode(b));
}
```

In `Parser::parseAtom`, extend the operator selection:
```cpp
            if (at(TokKind::Gt))       { n->filter.op = ">";  consume(); }
            else if (at(TokKind::Lt))  { n->filter.op = "<";  consume(); }
            else if (at(TokKind::Gte)) { n->filter.op = ">="; consume(); }
            else if (at(TokKind::Lte)) { n->filter.op = "<="; consume(); }
            else if (at(TokKind::Eq))  { n->filter.op = "=";  consume(); }
            else                        { n->filter.op = ":"; }
```

In `MatchFilter`, replace the string-field branches:
```cpp
    if (field == "title")  return ContainsI(r.title,  f.value);
    if (field == "artist") return ContainsI(r.artist, f.value);
    if (field == "album")  return ContainsI(r.album,  f.value);
    if (field == "genre")  return ContainsI(r.genre,  f.value);
    if (field == "label")  return ContainsI(r.label,  f.value);
    if (field == "type")    return ContainsI(r.type,    NormalizeType(f.value));
    if (field == "country") return ContainsI(r.country, f.value);
```
with:
```cpp
    if (field == "title")  return f.op == "=" ? EqualsI(r.title,  f.value) : ContainsI(r.title,  f.value);
    if (field == "artist") return f.op == "=" ? EqualsI(r.artist, f.value) : ContainsI(r.artist, f.value);
    if (field == "album")  return f.op == "=" ? EqualsI(r.album,  f.value) : ContainsI(r.album,  f.value);
    if (field == "genre")  return f.op == "=" ? EqualsI(r.genre,  f.value) : ContainsI(r.genre,  f.value);
    if (field == "label")  return f.op == "=" ? EqualsI(r.label,  f.value) : ContainsI(r.label,  f.value);
    if (field == "type")    return f.op == "=" ? EqualsI(r.type, NormalizeType(f.value))
                                                 : ContainsI(r.type, NormalizeType(f.value));
    if (field == "country") return f.op == "=" ? EqualsI(r.country, f.value) : ContainsI(r.country, f.value);
```
(Numeric/bool fields below are untouched — their existing default branch already treats any unrecognized op, including `"="`, as exact equality.)

Update the header comment in `gui/src/query_dsl.hh:11-15`:
```cpp
// Fields: artist, title, album, genre, label, country, type, year, duration,
// hires, explicit. Operators: ':' (contains / exact for numeric), '=' right
// after ':' (field:=value — exact, case-insensitive; a harmless synonym on
// numeric/bool fields, which are already exact), '>' '<' '>=' '<='
// (numeric), '-' (numeric range, e.g. year:2000-2010). Boolean AND
// (implicit or '&&'/"and"/"y"), OR ('||'/"or"/"o"), NOT ('!'/"not"),
// parentheses for grouping.
```

Update the cheatsheet panel text in `gui/src/views.cc:152`:
```cpp
            "Operators:  field:value (contains)   field:=value (exact)   field:>N  field:<N  field:>=N  field:<=N (numeric)   field:N-M (range)",
```

- [ ] **Step 4: Build and run to verify all assertions pass**

```bash
cmake --build build/linux_native --target streamer_gui -j$(nproc)
build/linux_native/gui/streamer_gui --selftest
```
Expected: `selftest: ok (<N> assertions)` with no `FAIL` lines.

- [ ] **Step 5: Commit**

```bash
git add gui/src/query_dsl.hh gui/src/query_dsl.cc gui/src/views.cc gui/src/gui_main.cc
git commit -m "gui: add field:=value exact-match search operator"
```

---

### Task 2: Multi-column stacked sort — pure logic (`CycleSortKey` / `ApplySort`)

**Files:**
- Modify: `gui/src/search_controller.hh` (replace `SortColumn`'s single-state API with a `SortKey` stack + two free functions)
- Modify: `gui/src/search_controller.cc` (implement `CycleSortKey`/`ApplySort`, rewire `SearchController::sort`/`search`)
- Modify: `gui/src/gui_main.cc` (selftest assertions for both free functions)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces (used by Task 3): `search::SortKey{SortColumn col; bool asc;}`, `search::SortDirection{None, Ascending, Descending}`, `SearchController::sort_keys() -> const std::vector<SortKey>&`, `SearchController::sort_direction(SortColumn) -> SortDirection`, `SearchController::sort_priority(SortColumn) -> int` (0-based rank, -1 if inactive).

- [ ] **Step 1: Write the failing selftest assertions**

In `gui/src/gui_main.cc`, add near the top of `run_selftest()` (right after the `#include`-driven `using namespace query_dsl;` block's assertions, before the keystroke-pipeline block — it needs `#include "search_controller.hh"`, already included at the top of the file):

```cpp
    // ── Multi-column stacked sort: CycleSortKey's 3-state-per-column cycle
    // (ascending -> descending -> removed), and ApplySort's stacked
    // comparator (earlier keys in `keys` take priority; later ones only
    // break ties). Pure functions — no SearchController/network needed.
    {
        using namespace search;

        std::vector<SortKey> keys;
        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 1 && keys[0].col == SortColumn::Artist && keys[0].asc,
             "1st click on Artist: appended, ascending");

        CycleSortKey(keys, SortColumn::Duration);
        check(keys.size() == 2 && keys[0].col == SortColumn::Artist &&
             keys[1].col == SortColumn::Duration && keys[1].asc,
             "1st click on Duration: appended as secondary, Artist stays primary");

        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 2 && keys[0].col == SortColumn::Artist && !keys[0].asc,
             "2nd click on Artist: flips to descending in place, Duration still secondary");

        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 1 && keys[0].col == SortColumn::Duration,
             "3rd click on Artist: removed from the stack, Duration promoted to primary");

        query_dsl::SearchResult ana, bea1, bea2;
        ana.artist = "Ana";  ana.duration = 300;
        bea1.artist = "Bea"; bea1.duration = 200;
        bea2.artist = "Bea"; bea2.duration = 100;
        std::vector<query_dsl::SearchResult> results = {bea1, ana, bea2};

        ApplySort(results, {{SortColumn::Artist, true}});
        check(results[0].artist == "Ana" && results[1].artist == "Bea" && results[2].artist == "Bea",
             "ApplySort: single key groups by Artist ascending");

        ApplySort(results, {{SortColumn::Artist, true}, {SortColumn::Duration, true}});
        check(results[0].artist == "Ana" &&
             results[1].artist == "Bea" && results[1].duration == 100 &&
             results[2].artist == "Bea" && results[2].duration == 200,
             "ApplySort: Artist stays primary, Duration breaks ties within the Bea group ascending");
    }
```

- [ ] **Step 2: Build to verify it fails to compile (the symbols don't exist yet)**

```bash
cmake --build build/linux_native --target streamer_gui -j$(nproc)
```
Expected: compile error — `CycleSortKey`/`ApplySort`/`SortKey` not declared in namespace `search`.

- [ ] **Step 3: Replace the single-column sort state with a stack, in `search_controller.hh`**

Replace:
```cpp
enum class SortColumn {
    None, Title, Artist, Label, Date, Duration, Genre, HiRes, Explicit, Type, Country
};
```
with:
```cpp
enum class SortColumn {
    None, Title, Artist, Label, Date, Duration, Genre, HiRes, Explicit, Type, Country
};

enum class SortDirection { None, Ascending, Descending };

// One entry in the active sort stack: `col` sorts on that column, `asc`
// is its current direction. Index 0 of the stack is the primary key;
// later entries only break ties left by earlier ones.
struct SortKey {
    SortColumn col;
    bool asc;
};

// Cycles `col`'s state in `keys` without touching any other key's position:
// absent -> appended at the end (lowest priority), ascending; ascending ->
// descending, same slot; descending -> erased. Pure function (no
// SearchController/network needed) so it can be exercised headlessly by
// gui_main.cc's --selftest.
void CycleSortKey(std::vector<SortKey>& keys, SortColumn col);

// Stable-sorts `results` by walking `keys` in order: the first key two rows
// disagree on decides their relative order; if every key agrees they're
// equal, the input's relative order is kept (stable_sort). A no-op when
// `keys` is empty. Pure function, same testability rationale as above.
void ApplySort(std::vector<query_dsl::SearchResult>& results, const std::vector<SortKey>& keys);
```

Replace:
```cpp
    void sort(SortColumn col);  // toggles ascending/descending if already active
    SortColumn sort_column() const { return sort_col_; }
    bool sort_ascending() const { return sort_asc_; }
```
with:
```cpp
    // See CycleSortKey's doc comment above — this drives the same 3-state
    // cycle against this controller's own results_.
    void sort(SortColumn col);
    const std::vector<SortKey>& sort_keys() const { return sort_keys_; }
    SortDirection sort_direction(SortColumn col) const;
    int sort_priority(SortColumn col) const;  // 0-based rank in sort_keys(), -1 if inactive
```

Replace:
```cpp
    SortColumn sort_col_ = SortColumn::None;
    bool sort_asc_ = true;
```
with:
```cpp
    std::vector<SortKey> sort_keys_;
```

Remove the now-unused private `void applySort();` declaration.

- [ ] **Step 4: Implement in `search_controller.cc`**

Replace the `sort_col_ = SortColumn::None;` reset inside `search()` (right before `++revision_; return true;`) with:
```cpp
    sort_keys_.clear();
```

Replace:
```cpp
void SearchController::sort(SortColumn col) {
    if (sort_col_ == col) sort_asc_ = !sort_asc_;
    else { sort_col_ = col; sort_asc_ = true; }
    applySort();
    ++revision_;
}
```
with:
```cpp
void SearchController::sort(SortColumn col) {
    CycleSortKey(sort_keys_, col);
    ApplySort(results_, sort_keys_);
    ++revision_;
}

SortDirection SearchController::sort_direction(SortColumn col) const {
    for (const auto& k : sort_keys_)
        if (k.col == col) return k.asc ? SortDirection::Ascending : SortDirection::Descending;
    return SortDirection::None;
}

int SearchController::sort_priority(SortColumn col) const {
    for (size_t i = 0; i < sort_keys_.size(); i++)
        if (sort_keys_[i].col == col) return (int)i;
    return -1;
}
```

Replace the trailing `applySort()` member function:
```cpp
void SearchController::applySort() {
    if (sort_col_ == SortColumn::None) return;
    bool asc = sort_asc_;
    std::stable_sort(results_.begin(), results_.end(),
        [&](const query_dsl::SearchResult& a, const query_dsl::SearchResult& b) {
            return asc ? LessAt(sort_col_, a, b) : LessAt(sort_col_, b, a);
        });
}
```
with the two free functions (still inside `namespace search`, outside the anonymous namespace so they're externally callable, but placed right after the anonymous namespace closes since they use `LessAt`):
```cpp
void CycleSortKey(std::vector<SortKey>& keys, SortColumn col) {
    auto it = std::find_if(keys.begin(), keys.end(),
                           [col](const SortKey& k) { return k.col == col; });
    if (it == keys.end())      keys.push_back({col, true});
    else if (it->asc)          it->asc = false;
    else                        keys.erase(it);
}

void ApplySort(std::vector<query_dsl::SearchResult>& results, const std::vector<SortKey>& keys) {
    if (keys.empty()) return;
    std::stable_sort(results.begin(), results.end(),
        [&](const query_dsl::SearchResult& a, const query_dsl::SearchResult& b) {
            for (const auto& key : keys) {
                bool ab = LessAt(key.col, a, b);
                bool ba = LessAt(key.col, b, a);
                if (ab == ba) continue;  // tied under this key — fall through to the next
                return key.asc ? ab : ba;
            }
            return false;  // equal under every key
        });
}
```

- [ ] **Step 5: Build and run to verify all assertions pass**

```bash
cmake --build build/linux_native --target streamer_gui -j$(nproc)
build/linux_native/gui/streamer_gui --selftest
```
Expected: `selftest: ok (<N> assertions)`, no `FAIL` lines. (This will also surface any remaining caller of the removed `sort_column()`/`sort_ascending()` accessors as a compile error — that's Task 3.)

- [ ] **Step 6: Commit**

```bash
git add gui/src/search_controller.hh gui/src/search_controller.cc gui/src/gui_main.cc
git commit -m "gui: multi-column stacked sort (CycleSortKey/ApplySort)"
```

---

### Task 3: Wire the results table to the sort stack + secondary-key rank badges

**Files:**
- Modify: `gui/src/views.cc:231-235` (pass the primary key to `drawSortableTable`)
- Modify: `gui/src/views.cc` (draw rank badges for secondary keys, inside the existing header-hit-rects block at `views.cc:239-258`)

**Interfaces:**
- Consumes: `search::SortKey`, `SearchController::sort_keys()` (Task 2).
- Produces: nothing further downstream.

- [ ] **Step 1: Fix the compile error left by Task 2**

`gui/src/views.cc:231-235` currently reads:
```cpp
    int rowCount = (int)ctl.results().size();
    auto cellFn = [&ctl](int row, int col) { return cellForResult(ctl, row, col); };
    auto visibleRows = widgets::drawSortableTable(
        c, tableArea, cols, cellFn, rowCount,
        indexForSortColumn(ctl.sort_column()), ctl.sort_ascending(),
        tableScrollPx, rowH, hoverRow, hoverHeaderCol, tstyle,
        kCellFit, &table.columnWidthsPx);
```
Replace with:
```cpp
    int rowCount = (int)ctl.results().size();
    auto cellFn = [&ctl](int row, int col) { return cellForResult(ctl, row, col); };

    // The widget only knows how to draw a glyph for one column — pass it
    // the primary (highest-priority) sort key, same as the single-column
    // behavior this replaces. Secondary keys get their own rank badge below,
    // drawn directly by this file against the widget's header rects.
    int primaryIdx = -1;
    bool primaryAsc = true;
    if (!ctl.sort_keys().empty()) {
        primaryIdx = indexForSortColumn(ctl.sort_keys()[0].col);
        primaryAsc = ctl.sort_keys()[0].asc;
    }
    auto visibleRows = widgets::drawSortableTable(
        c, tableArea, cols, cellFn, rowCount,
        primaryIdx, primaryAsc,
        tableScrollPx, rowH, hoverRow, hoverHeaderCol, tstyle,
        kCellFit, &table.columnWidthsPx);
```

- [ ] **Step 2: Build to verify it compiles clean**

```bash
cmake --build build/linux_native --target streamer_gui -j$(nproc)
```
Expected: no errors (the `sort_column()`/`sort_ascending()` compile error from Task 2 is now gone).

- [ ] **Step 3: Run the selftest to confirm no regression**

```bash
build/linux_native/gui/streamer_gui --selftest
```
Expected: `selftest: ok (<N> assertions)` — unchanged from Task 2's run (this task adds no new pure-logic assertions; the badge drawing is visual-only, verified in Step 5).

- [ ] **Step 4: Add secondary-key rank badges**

In `gui/src/views.cc`, inside the existing block that computes `liveCols` for the resize indicator and header hit-rects (`views.cc:239-258`), add the badge loop right after the header-hit-rects `for` loop and before the block's closing `}`:

```cpp
        // Rank badges for secondary sort keys: the widget drew a glyph only
        // for the primary key above, so any additional active columns get a
        // small "2"/"3".../direction-triangle drawn directly here, against
        // the same header rects — no changes to the vendored widget.
        if (ctl.sort_keys().size() > 1) {
            float badgeSize = rowH * 0.28f;
            for (size_t rank = 1; rank < ctl.sort_keys().size(); rank++) {
                const auto& key = ctl.sort_keys()[rank];
                int idx = indexForSortColumn(key.col);
                if (idx < 0 || (size_t)idx >= liveCols.size()) continue;
                const Rect& hc = liveCols[(size_t)idx];

                float bx = hc.x + hc.w - rowH * 0.75f;
                float by = hc.y + (hc.h - badgeSize) * 0.5f;
                c.text(std::to_string(rank + 1), bx, by, badgeSize, theme::kAccent);

                float cx = hc.x + hc.w - rowH * 0.18f;
                float cy = hc.y + hc.h * 0.5f;
                float aw = rowH * 0.09f, ah = rowH * 0.11f;
                if (key.asc)
                    c.triangle(cx - aw, cy + ah * 0.5f, cx + aw, cy + ah * 0.5f, cx, cy - ah * 0.5f, theme::kAccent);
                else
                    c.triangle(cx - aw, cy - ah * 0.5f, cx + aw, cy - ah * 0.5f, cx, cy + ah * 0.5f, theme::kAccent);
            }
        }
```

- [ ] **Step 5: Build, then visually verify with the `run` skill**

```bash
cmake --build build/linux_native --target streamer_gui -j$(nproc)
```
Then use the project's `run` skill to launch `build/linux_native/gui/streamer_gui`, run a search, click "Artist" then "Duration" in the results header, and confirm: rows stay grouped by artist while ordered by duration within each group, the Artist column shows the primary glyph, and the Duration column shows a small "2" badge with its own direction triangle. Click Artist two more times (descending, then cleared) and confirm Duration is promoted to the sole/primary glyph with no badge. This is a real Vulkan/Wayland window — there is a live `WAYLAND_DISPLAY` in this environment, so it can actually be launched and driven, not just compiled.

- [ ] **Step 6: Commit**

```bash
git add gui/src/views.cc
git commit -m "gui: wire results table to the multi-column sort stack"
```
