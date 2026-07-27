#pragma once
// SearchController — pure logic, no rendering (Controller half of the
// Controller/View split). Owns query text, the parsed filter DSL, live
// results, sort/selection state, and the cheatsheet-panel toggle. Talks to
// kb::QobuzApiService directly, in-process — no subprocess, no argv
// round-trip, which is what fixes the accent-search bug (see query_dsl.hh's
// comment and the refactor plan).
//
// Constructible and drivable without a window: search()/sort()/selection
// methods are exercised headlessly by gui_main.cc's --selftest.

#include "query_dsl.hh"

#include <api/service.hh>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace config { struct Account; }

namespace search {

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

class SearchController {
public:
    // `account` may have empty app_id/app_secret (no account configured
    // yet) — that's a normal state, not an error: has_service() reports it,
    // and search() fails gracefully with last_error() rather than crashing
    // or requiring a valid service to exist up front.
    explicit SearchController(const config::Account& account);

    bool has_service() const { return svc_.has_value(); }

    // Parses `query_text` (the DSL), extracts a base search term and a
    // type hint, calls the matching kb:: search endpoint(s), applies
    // client-side query_dsl::Match/Score, stores results ranked by score
    // (ties broken by the API's own order), bumps revision(). `kind` is the
    // UI type-picker's selection ("smart"/"albums"/"tracks"/"artists"/
    // "playlists"/"all"); "smart" defers to the DSL's type: hint if present,
    // else searches everything. Returns false and fills last_error() on a
    // network/auth failure — never throws.
    bool search(const std::string& query_text, const std::string& kind, int limit = 50);

    const std::vector<query_dsl::SearchResult>& results() const { return results_; }
    const std::string& last_error() const { return last_error_; }

    // See CycleSortKey's doc comment above — this drives the same 3-state
    // cycle against this controller's own results_.
    void sort(SortColumn col);
    const std::vector<SortKey>& sort_keys() const { return sort_keys_; }
    SortDirection sort_direction(SortColumn col) const;
    int sort_priority(SortColumn col) const;  // 0-based rank in sort_keys(), -1 if inactive

    // Selection is keyed by each result's stable `id`, not its row index —
    // sort() reorders results_ in place, so an index-based selection would
    // silently follow whatever row lands at that position instead of the
    // item the user actually picked.
    void toggle_selected(int index);
    bool is_selected(int index) const;
    const std::set<std::string>& selected() const { return selected_ids_; }
    void clear_selection();

    void toggle_cheatsheet() { cheatsheet_open_ = !cheatsheet_open_; ++revision_; }
    bool cheatsheet_open() const { return cheatsheet_open_; }

    // Monotonic counter bumped on any state change — the view layer uses
    // this to drive animations without duplicating state (scanersito's
    // Controller pattern).
    uint64_t revision() const { return revision_; }

private:
    std::optional<kb::QobuzApiService> svc_;
    std::vector<query_dsl::SearchResult> results_;
    std::string last_error_;
    std::vector<SortKey> sort_keys_;
    std::set<std::string> selected_ids_;
    bool cheatsheet_open_ = false;
    uint64_t revision_ = 0;
};

} // namespace search
