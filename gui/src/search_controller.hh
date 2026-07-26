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

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace kb { class QobuzApiService; }

namespace search {

enum class SortColumn {
    None, Title, Artist, Label, Date, Duration, Genre, HiRes, Explicit, Type, Country
};

class SearchController {
public:
    explicit SearchController(kb::QobuzApiService& svc) : svc_(svc) {}

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

    void sort(SortColumn col);  // toggles ascending/descending if already active
    SortColumn sort_column() const { return sort_col_; }
    bool sort_ascending() const { return sort_asc_; }

    void toggle_selected(int index);
    bool is_selected(int index) const { return selected_.count(index) != 0; }
    const std::set<int>& selected() const { return selected_; }
    void clear_selection();

    void toggle_cheatsheet() { cheatsheet_open_ = !cheatsheet_open_; ++revision_; }
    bool cheatsheet_open() const { return cheatsheet_open_; }

    // Monotonic counter bumped on any state change — the view layer uses
    // this to drive animations without duplicating state (scanersito's
    // Controller pattern).
    uint64_t revision() const { return revision_; }

private:
    kb::QobuzApiService& svc_;
    std::vector<query_dsl::SearchResult> results_;
    std::string last_error_;
    SortColumn sort_col_ = SortColumn::None;
    bool sort_asc_ = true;
    std::set<int> selected_;
    bool cheatsheet_open_ = false;
    uint64_t revision_ = 0;

    void applySort();
};

} // namespace search
