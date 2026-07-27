#include "search_controller.hh"

#include "config.hh"
#include "service_factory.hh"

#include <core/models.hh>

#include <algorithm>
#include <cctype>

namespace search {

SearchController::SearchController(const config::Account& account) {
    auto res = qobuz::make_service(account);
    if (res.ok()) svc_ = res.take();
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

bool SearchController::search(const std::string& query_text, const std::string& kind, int limit) {
    last_error_.clear();
    results_.clear();
    clear_selection();

    if (!svc_) {
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

    bool any_ok = false;
    std::string combined_error;

    auto run_tracks = [&]() {
        auto res = svc_->search_tracks(base_term, limit, {});
        if (!res.ok()) { combined_error += res.error().message + " "; return; }
        any_ok = true;
        for (auto& t : res.value().items.value_or({}))
            if (t) results_.push_back(FromTrack(*t));
    };
    auto run_albums = [&]() {
        auto res = svc_->search_albums(base_term, limit, {});
        if (!res.ok()) { combined_error += res.error().message + " "; return; }
        any_ok = true;
        for (auto& a : res.value().items.value_or({}))
            if (a) results_.push_back(FromAlbum(*a));
    };
    auto run_artists = [&]() {
        auto res = svc_->search_artists(base_term, limit, {});
        if (!res.ok()) { combined_error += res.error().message + " "; return; }
        any_ok = true;
        for (auto& a : res.value().items.value_or({}))
            if (a) results_.push_back(FromArtist(*a));
    };
    auto run_playlists = [&]() {
        auto res = svc_->search_playlists(base_term, limit, {});
        if (!res.ok()) { combined_error += res.error().message + " "; return; }
        any_ok = true;
        for (auto& p : res.value().items.value_or({}))
            if (p) results_.push_back(FromPlaylist(*p));
    };

    if (k == "tracks") run_tracks();
    else if (k == "albums") run_albums();
    else if (k == "artists") run_artists();
    else if (k == "playlists") run_playlists();
    else { run_albums(); run_tracks(); run_artists(); run_playlists(); }

    if (!any_ok) { last_error_ = combined_error; ++revision_; return false; }

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

    sort_col_ = SortColumn::None;
    ++revision_;
    return true;
}

void SearchController::sort(SortColumn col) {
    if (sort_col_ == col) sort_asc_ = !sort_asc_;
    else { sort_col_ = col; sort_asc_ = true; }
    applySort();
    ++revision_;
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

void SearchController::applySort() {
    if (sort_col_ == SortColumn::None) return;
    bool asc = sort_asc_;
    std::stable_sort(results_.begin(), results_.end(),
        [&](const query_dsl::SearchResult& a, const query_dsl::SearchResult& b) {
            return asc ? LessAt(sort_col_, a, b) : LessAt(sort_col_, b, a);
        });
}

void SearchController::toggle_selected(int index) {
    if (index < 0 || (size_t)index >= results_.size()) return;
    const std::string& id = results_[(size_t)index].id;
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

} // namespace search
