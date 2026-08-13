#include "search.hh"
#include "i18n.hh"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include <api/service.hh>

namespace search {

// ── Duration helpers ──────────────────────────────────────────────────────────

std::optional<std::string> parse_duration(const std::string &raw, int &out_secs) {
    std::string s = raw;
    out_secs = 0;

    if (s.find(':') != std::string::npos) {
        std::vector<std::string> parts;
        std::istringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, ':')) parts.push_back(tok);
        try {
            if (parts.size() == 2) {
                out_secs = std::stoi(parts[0]) * 60 + std::stoi(parts[1]);
            } else if (parts.size() == 3) {
                out_secs = std::stoi(parts[0]) * 3600
                         + std::stoi(parts[1]) * 60
                         + std::stoi(parts[2]);
            } else {
                return "invalid duration: " + raw;
            }
            return std::nullopt;
        } catch (...) {
            return "invalid duration: " + raw;
        }
    }

    int total = 0;
    std::string num_buf;
    bool has_unit = false;
    for (char ch : s) {
        if (std::isdigit((unsigned char)ch)) {
            num_buf += ch;
        } else {
            if (num_buf.empty()) return "invalid duration: " + raw;
            int n;
            try { n = std::stoi(num_buf); } catch (...) { return "invalid duration: " + raw; }
            num_buf.clear();
            switch (std::tolower((unsigned char)ch)) {
                case 'h': total += n * 3600; has_unit = true; break;
                case 'm': total += n * 60;   has_unit = true; break;
                case 's': total += n;        has_unit = true; break;
                default:  return std::string("unknown unit '") + ch + "' in: " + raw;
            }
        }
    }
    if (!num_buf.empty()) {
        try {
            int n = std::stoi(num_buf);
            if (has_unit) return "trailing number without unit: " + raw;
            total += n;
        } catch (...) { return "invalid duration: " + raw; }
    }
    out_secs = total;
    return std::nullopt;
}

std::string fmt_duration(int secs) {
    int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else        std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

// ── Type normalization ────────────────────────────────────────────────────────

static std::string normalize_type(const std::string &kind) {
    std::string lc = kind;
    for (char &c : lc) c = static_cast<char>(std::tolower((unsigned char)c));
    if (lc=="albums"||lc=="album"||lc=="\xc3\xa1lbumes"||lc=="albumes"||lc=="\xc3\xa1lbum")
        return "albums";
    if (lc=="tracks"||lc=="track"||lc=="pistas"||lc=="pista") return "tracks";
    if (lc=="artists"||lc=="artist"||lc=="artistas"||lc=="artista") return "artists";
    if (lc=="playlists"||lc=="playlist"||lc=="listas"||lc=="lista") return "playlists";
    if (lc=="all"||lc=="todos"||lc=="todo") return "all";
    return kind;
}

// TSV escaping: replace tabs and newlines so rows stay valid.
static std::string esc(const std::string &s) {
    std::string r;
    for (char c : s) {
        if (c == '\t' || c == '\n' || c == '\r') r += ' ';
        else r += c;
    }
    return r;
}
static std::string esc(const std::optional<std::string> &s) {
    return s ? esc(*s) : std::string{};
}

static bool passes(int dur, std::optional<int> mn, std::optional<int> mx) {
    if (mn && dur < *mn) return false;
    if (mx && dur > *mx) return false;
    return true;
}

// ── run_one ───────────────────────────────────────────────────────────────────

static kb::Result<int> run_one(kb::QobuzApiService &svc,
                    const std::string &query,
                    const std::string &kind,
                    bool tsv,
                    int limit,
                    std::optional<int> min_secs,
                    std::optional<int> max_secs)
{
    bool filtering = min_secs.has_value() || max_secs.has_value();
    int fetch_limit = filtering ? std::min((limit * 5), 500) : limit;

    if (kind == "tracks") {
        auto res = svc.search_tracks(query, fetch_limit, {});
        if (!res.ok()) return res.error();
        auto items = res.value().items.value_or({});
        std::vector<decltype(items)::value_type> filtered;
        for (auto &t : items) {
            if (passes(t->duration.value_or(0), min_secs, max_secs)) {
                filtered.push_back(t);
                if ((int)filtered.size() >= limit) break;
            }
        }
        if (tsv) {
            for (auto &t : filtered) {
                std::string id      = std::to_string(t->id.value_or(0));
                std::string raw_tit = esc(t->title);
                std::string ver     = t->version ? *t->version : "";
                std::string title   = ver.empty() ? raw_tit : raw_tit + " (" + esc(ver) + ")";
                std::string artist  = esc(t->performer ? t->performer->name
                                        : (t->album && t->album->artist ? t->album->artist->name
                                                                         : std::optional<std::string>{}));
                std::string label   = esc(t->album && t->album->label ? t->album->label->name
                                                                        : std::optional<std::string>{});
                std::string date    = esc(t->album ? t->album->release_date_original : std::optional<std::string>{});
                std::string dur     = std::to_string(t->duration.value_or(0));
                std::string genre   = esc(t->album && t->album->genre ? t->album->genre->name
                                                                        : std::optional<std::string>{});
                std::string hires   = t->hires.value_or(false) ? "true" : "false";
                std::string expl    = (t->parental_warning == std::optional<bool>{true}) ? "true" : "";
                std::printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\ttrack\t%s\t%s\n",
                    id.c_str(), title.c_str(), artist.c_str(), label.c_str(),
                    date.c_str(), dur.c_str(), genre.c_str(), hires.c_str(),
                    label.c_str(), expl.c_str());
            }
        } else {
            if (!filtered.empty())
            std::printf("%s (%d %s):\n", i18n::t("search_tracks"), (int)filtered.size(), i18n::t("results_suffix"));
            for (auto &t : filtered) {
                std::string title  = t->title.value_or("?");
                std::string artist = (t->performer && t->performer->name)
                    ? *t->performer->name
                    : (t->album && t->album->artist && t->album->artist->name)
                      ? *t->album->artist->name : "?";
                int id = t->id.value_or(0);
                if (filtering)
                    std::printf("  [%d] %s \xe2\x80\x94 %s  [%s]\n", id, title.c_str(), artist.c_str(),
                                fmt_duration(t->duration.value_or(0)).c_str());
                else
                    std::printf("  [%d] %s \xe2\x80\x94 %s\n", id, title.c_str(), artist.c_str());
            }
        }
        return (int)filtered.size();
    }

    if (kind == "artists") {
        auto res = svc.search_artists(query, limit, {});
        if (!res.ok()) return res.error();
        auto items = res.value().items.value_or({});
        if (tsv) {
            for (auto &a : items)
                std::printf("%d\t%s\t\t\t\t\t\t\tartist\t\t\n",
                    a->id.value_or(0), esc(a->name).c_str());
        } else {
            if (!items.empty())
            std::printf("%s (%d %s):\n", i18n::t("search_artists"), (int)items.size(), i18n::t("results_suffix"));
            for (auto &a : items)
                std::printf("  [%d] %s\n", a->id.value_or(0), a->name.value_or("?").c_str());
        }
        return (int)items.size();
    }

    if (kind == "playlists") {
        auto res = svc.search_playlists(query, fetch_limit, {});
        if (!res.ok()) return res.error();
        auto items = res.value().items.value_or({});
        std::vector<decltype(items)::value_type> filtered;
        for (auto &p : items) {
            if (passes(p->duration.value_or(0), min_secs, max_secs)) {
                filtered.push_back(p);
                if ((int)filtered.size() >= limit) break;
            }
        }
        if (tsv) {
            for (auto &p : filtered) {
                std::string id  = esc(p->id);
                std::string nm  = esc(p->name);
                std::string dur = std::to_string(p->duration.value_or(0));
                std::printf("%s\t%s\t\t\t\t%s\t\t\tplaylist\t\t\n",
                    id.c_str(), nm.c_str(), dur.c_str());
            }
        } else {
            if (!filtered.empty())
            std::printf("%s (%d %s):\n", i18n::t("search_playlists"), (int)filtered.size(), i18n::t("results_suffix"));
            for (auto &p : filtered) {
                std::string id = p->id.value_or("?");
                std::string nm = p->name.value_or("?");
                if (filtering)
                    std::printf("  [%s] %s  [%s]\n", id.c_str(), nm.c_str(),
                                fmt_duration(p->duration.value_or(0)).c_str());
                else
                    std::printf("  [%s] %s\n", id.c_str(), nm.c_str());
            }
        }
        return (int)filtered.size();
    }

    // albums (default)
    auto res = svc.search_albums(query, fetch_limit, {});
    if (!res.ok()) return res.error();
    auto items = res.value().items.value_or({});
    std::vector<decltype(items)::value_type> filtered;
    for (auto &a : items) {
        if (passes(a->duration.value_or(0), min_secs, max_secs)) {
            filtered.push_back(a);
            if ((int)filtered.size() >= limit) break;
        }
    }
    if (tsv) {
        for (auto &a : filtered) {
            std::string id      = esc(a->id);
            std::string raw_tit = esc(a->title);
            std::string ver     = a->version ? *a->version : "";
            std::string title   = ver.empty() ? raw_tit : raw_tit + " (" + esc(ver) + ")";
            std::string artist  = esc(a->artist ? a->artist->name : std::optional<std::string>{});
            std::string label   = esc(a->label ? a->label->name : std::optional<std::string>{});
            std::string date    = esc(a->release_date_original);
            std::string dur     = std::to_string(a->duration.value_or(0));
            std::string genre   = esc(a->genre ? a->genre->name : std::optional<std::string>{});
            std::string hires   = a->hires.value_or(false) ? "true" : "false";
            std::string expl    = (a->parental_warning == std::optional<bool>{true}) ? "true" : "";
            std::printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\talbum\t%s\t%s\n",
                id.c_str(), title.c_str(), artist.c_str(), label.c_str(),
                date.c_str(), dur.c_str(), genre.c_str(), hires.c_str(),
                label.c_str(), expl.c_str());
        }
    } else {
        if (!filtered.empty())
        std::printf("%s (%d %s):\n", i18n::t("search_albums"), (int)filtered.size(), i18n::t("results_suffix"));
        for (auto &a : filtered) {
            std::string title  = a->title.value_or("?");
            std::string artist = (a->artist && a->artist->name) ? *a->artist->name : "?";
            std::string year   = a->release_date_original.value_or("?");
            std::string id     = a->id.value_or("?");
            if (filtering)
                std::printf("  [%s] %s \xe2\x80\x94 %s (%s)  [%s]\n",
                    id.c_str(), title.c_str(), artist.c_str(), year.c_str(),
                    fmt_duration(a->duration.value_or(0)).c_str());
            else
                std::printf("  [%s] %s \xe2\x80\x94 %s (%s)\n",
                    id.c_str(), title.c_str(), artist.c_str(), year.c_str());
        }
    }
    return (int)filtered.size();
}

// ── Public entry point ────────────────────────────────────────────────────────

kb::Result<int> run(kb::QobuzApiService &svc,
                    const std::string &query,
                    const std::string &kind,
                    bool tsv,
                    int limit,
                    std::optional<int> min_secs,
                    std::optional<int> max_secs)
{
    std::string k = normalize_type(kind);
    int total = 0;
    std::optional<kb::Error> first_error;

    auto one = [&](const char *t) {
        auto res = run_one(svc, query, t, tsv, limit, min_secs, max_secs);
        if (res.ok()) total += res.value();
        else if (!first_error) first_error = res.error();
    };

    if (k == "all") {
        for (const char *t : {"albums", "tracks", "artists", "playlists"}) one(t);
    } else {
        one(k.c_str());
    }

    // A category erroring while another returned rows is not a failed search,
    // so the error is only fatal when nothing at all came back.
    if (total == 0 && first_error) return *first_error;

    // Nothing found, but nothing broke either. Reported as "not found" rather
    // than as an empty success so the caller's account failover treats it as
    // what it usually is on a multi-region setup: this catalog doesn't carry
    // it, ask the next country. See account::classify.
    if (total == 0) return kb::not_found_error("search results", query);

    return total;
}

} // namespace search
