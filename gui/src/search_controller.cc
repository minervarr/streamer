#include "search_controller.hh"

#include "config.hh"
#include "download.hh"
#include "library.hh"
#include "search.hh"
#include "service_factory.hh"

#include <core/models.hh>

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace search {

SearchController::SearchController(account::Pool& pool) : pool_(&pool) {}

bool SearchController::has_service() const {
    return pool_ && !pool_->candidates(account::Selector{}).empty();
}

namespace {

std::string LowerAscii(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ── kb:: model -> query_dsl::SearchResult conversion ───────────────────────
// Field access mirrors src/search.cpp's run_one() (the CLI's proven-working
// TSV output path) exactly, so behavior stays consistent between the CLI
// and the GUI.

query_dsl::SearchResult FromTrack(const kb::Track& t) {
    query_dsl::SearchResult r;
    r.id = std::to_string(t.id.value_or(0));
    r.title = t.title.value_or("");
    if (!t.version.value_or("").empty()) r.title += " (" + t.version.value() + ")";
    r.artist = t.performer && t.performer->name ? *t.performer->name
             : (t.album && t.album->artist && t.album->artist->name ? *t.album->artist->name : "");
    r.label = (t.album && t.album->label && t.album->label->name) ? *t.album->label->name : "";
    r.date = (t.album && t.album->release_date_original) ? *t.album->release_date_original : "";
    r.duration = t.duration.value_or(0);
    r.genre = (t.album && t.album->genre && t.album->genre->name) ? *t.album->genre->name : "";
    r.hires = t.hires.value_or(false);
    r.explicit_ = t.parental_warning.value_or(false);
    r.type = "track";
    r.year = r.date.size() >= 4 ? std::atoi(r.date.substr(0, 4).c_str()) : 0;
    return r;
}

query_dsl::SearchResult FromAlbum(const kb::Album& a) {
    query_dsl::SearchResult r;
    r.id = a.id.value_or("");
    r.title = a.title.value_or("");
    if (!a.version.value_or("").empty()) r.title += " (" + a.version.value() + ")";
    r.artist = (a.artist && a.artist->name) ? *a.artist->name : "";
    r.label = (a.label && a.label->name) ? *a.label->name : "";
    r.date = a.release_date_original.value_or("");
    r.duration = a.duration.value_or(0);
    r.genre = (a.genre && a.genre->name) ? *a.genre->name : "";
    r.hires = a.hires.value_or(false);
    r.explicit_ = a.parental_warning.value_or(false);
    r.type = "album";
    r.year = r.date.size() >= 4 ? std::atoi(r.date.substr(0, 4).c_str()) : 0;
    return r;
}

query_dsl::SearchResult FromArtist(const kb::Artist& a) {
    query_dsl::SearchResult r;
    r.id = std::to_string(a.id.value_or(0));
    r.title = a.name.value_or("");
    r.artist = r.title;
    r.type = "artist";
    return r;
}

query_dsl::SearchResult FromPlaylist(const kb::Playlist& p) {
    query_dsl::SearchResult r;
    r.id = p.id.value_or("");
    r.title = p.name.value_or("");
    r.duration = p.duration.value_or(0);
    r.type = "playlist";
    return r;
}

std::string NormalizeKind(const std::string& kind) {
    std::string k = LowerAscii(kind);
    if (k == "album" || k == "albums") return "albums";
    if (k == "track" || k == "tracks") return "tracks";
    if (k == "artist" || k == "artists") return "artists";
    if (k == "playlist" || k == "playlists") return "playlists";
    if (k == "all") return "all";
    return "smart";
}

} // namespace

// Collapses the same release returned by several accounts into one row whose
// country lists every region that offered it — "FR, NZ". Without this a
// `country:all` search shows one duplicate per account, which buries the
// actual answer the user wanted: *where* can I get this?
//
// Order is preserved (first sighting wins its position) so the API's own
// relevance ranking survives the merge.
void MergeByIdAcrossCountries(std::vector<query_dsl::SearchResult>& rows) {
    std::vector<query_dsl::SearchResult> merged;
    merged.reserve(rows.size());
    // Keyed on type as well as id: ids are only unique within a kind, so an
    // album and a track could otherwise collide and swallow each other.
    std::map<std::pair<std::string, std::string>, size_t> seen;

    for (auto& r : rows) {
        auto key = std::make_pair(r.type, r.id);
        auto it = seen.find(key);
        if (it == seen.end()) {
            seen.emplace(key, merged.size());
            merged.push_back(std::move(r));
            continue;
        }
        std::string& countries = merged[it->second].country;
        if (r.country.empty()) continue;
        if (countries.find(r.country) == std::string::npos) {
            if (!countries.empty()) countries += ", ";
            countries += r.country;
        }
    }
    rows = std::move(merged);
}


bool SearchController::search(const std::string& query_text, const std::string& kind, int limit) {
    last_error_.clear();
    results_.clear();
    clear_selection();

    if (!pool_) {
        last_error_ = "No account configured (set app_id/app_secret, then run login).";
        ++revision_;
        return false;
    }

    query_dsl::QueryNode ast = query_dsl::Parse(query_text);
    std::string base_term = query_dsl::ExtractBaseTerm(ast);
    if (base_term.empty()) base_term = query_text; // no filters — search the raw text

    std::string k = NormalizeKind(kind);
    if (k == "smart") {
        std::string hint = query_dsl::ExtractTypeHint(ast);
        k = hint.empty() ? "all" : NormalizeKind(hint);
    }
    // NormalizeKind answers "smart" for anything it doesn't recognise, so an
    // unparseable type: hint can land back here. Searching everything is the
    // right response to "I don't know what you meant" — and it is what the
    // old catch-all else branch did before the endpoints became explicit.
    if (k != "albums" && k != "tracks" && k != "artists" && k != "playlists") k = "all";

    // A `country:` term in the query beats the search bar's picker for this
    // one search — typing is more specific than a sticky control.
    account::Selector sel = selector_;
    const std::string country_hint = query_dsl::ExtractCountryHint(ast);
    if (!country_hint.empty()) sel = account::Selector::from_country(country_hint);

    const std::vector<int> cands = pool_->candidates(sel);
    if (cands.empty()) {
        last_error_ = "No account configured (set app_id/app_secret, then run login).";
        ++revision_;
        return false;
    }
    const bool fan_out = (sel.mode == account::Selector::All);

    bool any_ok = false;
    std::string combined_error;

    // Runs the requested endpoint(s) against one account, tagging every row
    // with the country that served it. That tag is what makes the results
    // table's Country column (dead until now — nothing ever filled it in) and
    // `country:` filtering mean something.
    auto run_one_account = [&](kb::QobuzApiService& svc, const std::string& country) {
        bool ok = false;
        auto take = [&](auto& res, auto&& conv) {
            if (!res.ok()) { combined_error += res.error().message + " "; return; }
            ok = true;
            for (auto& item : search::value_or_empty(res.value().items))
                if (item) {
                    auto r = conv(*item);
                    r.country = country;
                    results_.push_back(std::move(r));
                }
        };
        if (k == "tracks" || k == "all") {
            auto res = svc.search_tracks(base_term, limit, {});
            take(res, [](const kb::Track& t) { return FromTrack(t); });
        }
        if (k == "albums" || k == "all") {
            auto res = svc.search_albums(base_term, limit, {});
            take(res, [](const kb::Album& a) { return FromAlbum(a); });
        }
        if (k == "artists" || k == "all") {
            auto res = svc.search_artists(base_term, limit, {});
            take(res, [](const kb::Artist& a) { return FromArtist(a); });
        }
        if (k == "playlists" || k == "all") {
            auto res = svc.search_playlists(base_term, limit, {});
            take(res, [](const kb::Playlist& p) { return FromPlaylist(p); });
        }
        return ok;
    };

    for (int idx : cands) {
        auto svc = pool_->acquire(idx);
        if (!svc.ok()) {
            account::Failure f = account::classify(svc.error());
            pool_->record_fail(idx, f, svc.error().message);
            combined_error += svc.error().message + " ";
            if (!account::should_failover(f)) break;
            continue;
        }

        const std::string country = pool_->config().accounts[idx].country;
        if (run_one_account(*svc.value(), country)) {
            any_ok = true;
            pool_->record_ok(idx);
        } else {
            pool_->record_fail(idx, account::Failure::Other, combined_error);
        }

        // Auto/Country stop at the first account that answers; only `all`
        // keeps going to collect every region's view of the catalog.
        if (!fan_out && any_ok) break;
    }

    if (!any_ok) { last_error_ = combined_error; ++revision_; return false; }

    if (fan_out) MergeByIdAcrossCountries(results_);

    // Client-side DSL filtering + relevance ranking (query_dsl::Match/Score),
    // same as the Win32 GUI's QueryParser-driven filtering.
    std::vector<std::pair<int, query_dsl::SearchResult>> scored;
    scored.reserve(results_.size());
    for (auto& r : results_)
        if (query_dsl::Match(ast, r)) scored.emplace_back(query_dsl::Score(ast, r), r);
    std::stable_sort(scored.begin(), scored.end(),
        [](auto& a, auto& b) { return a.first > b.first; });

    results_.clear();
    for (auto& [score, r] : scored) results_.push_back(std::move(r));

    sort_keys_.clear();
    ++revision_;
    return true;
}

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

namespace {
bool LessAt(SortColumn col, const query_dsl::SearchResult& a, const query_dsl::SearchResult& b) {
    switch (col) {
        case SortColumn::Title:    return LowerAscii(a.title)  < LowerAscii(b.title);
        case SortColumn::Artist:   return LowerAscii(a.artist) < LowerAscii(b.artist);
        case SortColumn::Label:    return LowerAscii(a.label)  < LowerAscii(b.label);
        case SortColumn::Date:     return a.date < b.date;
        case SortColumn::Duration: return a.duration < b.duration;
        case SortColumn::Genre:    return LowerAscii(a.genre)  < LowerAscii(b.genre);
        case SortColumn::HiRes:    return a.hires < b.hires;
        case SortColumn::Explicit: return a.explicit_ < b.explicit_;
        case SortColumn::Type:     return a.type < b.type;
        case SortColumn::Country:  return LowerAscii(a.country) < LowerAscii(b.country);
        default: return false;
    }
}
} // namespace

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

void SearchController::toggle_selected(int index) {
    if (index < 0 || (size_t)index >= results_.size()) return;
    const std::string& id = results_[(size_t)index].id;
    if (downloaded_ids_.count(id)) return;  // already on disk — not selectable
    if (selected_ids_.count(id)) selected_ids_.erase(id);
    else selected_ids_.insert(id);
    ++revision_;
}

bool SearchController::is_selected(int index) const {
    if (index < 0 || (size_t)index >= results_.size()) return false;
    return selected_ids_.count(results_[(size_t)index].id) != 0;
}

void SearchController::clear_selection() {
    if (!selected_ids_.empty()) { selected_ids_.clear(); ++revision_; }
}

void SearchController::refresh_downloaded(const std::string& download_dir,
                                          const std::string& quality) {
    std::vector<std::string> track_ids, album_ids;
    for (const auto& r : results_) {
        if (r.type == "track") track_ids.push_back(r.id);
        else if (r.type == "album") album_ids.push_back(r.id);
    }

    std::set<std::string> next;
    int format_id = dl::quality_to_format_id(quality);
    for (auto& id : library::downloaded_track_ids(download_dir, track_ids, format_id))
        next.insert(id);
    for (auto& id : library::downloaded_album_ids(download_dir, album_ids, format_id))
        next.insert(id);

    if (next != downloaded_ids_) {
        downloaded_ids_ = std::move(next);
        ++revision_;
    }
}

bool SearchController::is_downloaded(int index) const {
    if (index < 0 || (size_t)index >= results_.size()) return false;
    return downloaded_ids_.count(results_[(size_t)index].id) != 0;
}

} // namespace search
