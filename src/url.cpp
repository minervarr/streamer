#include "url.hh"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace url {

static std::vector<std::string> split_path(const std::string &s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, '/'))
        if (!tok.empty()) out.push_back(tok);
    return out;
}

static std::optional<DownloadTarget> parse_url(const std::string &url) {
    std::string u = url;
    if (!u.empty() && u.back() == '/') u.pop_back();

    auto qpos = u.find("qobuz.com/");
    if (qpos == std::string::npos) return std::nullopt;
    auto segs = split_path(u.substr(qpos + 10));

    bool is_open = (u.find("open.qobuz.com") != std::string::npos);

    if (is_open && segs.size() == 2) {
        const auto &type = segs[0], &id = segs[1];
        if (type == "album")    return Album{id};
        if (type == "track")    try { return Track{std::stoi(id)}; } catch (...) {}
        if (type == "artist")   try { return Artist{std::stoi(id)}; } catch (...) {}
        if (type == "playlist") return Playlist{id};
        return std::nullopt;
    }

    for (size_t i = 0; i < segs.size(); ++i) {
        auto next = [&](size_t off) -> std::string {
            size_t idx = i + off;
            return idx < segs.size() ? segs[idx] : std::string{};
        };
        const auto &seg = segs[i];
        if (seg == "album") {
            std::string id = next(2).empty() ? next(1) : next(2);
            if (!id.empty()) return Album{id};
        } else if (seg == "track") {
            std::string id = next(2).empty() ? next(1) : next(2);
            try { return Track{std::stoi(id)}; } catch (...) {}
        } else if (seg == "interpreter" || seg == "artist") {
            std::string id = next(2).empty() ? next(1) : next(2);
            try { return Artist{std::stoi(id)}; } catch (...) {}
        } else if (seg == "playlist" || seg == "playlists") {
            std::string id = next(2).empty() ? next(1) : next(2);
            if (!id.empty()) return Playlist{id};
        }
    }
    return std::nullopt;
}

std::optional<DownloadTarget> parse(const std::string &raw) {
    std::string s = raw;
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();

    if (s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0)
        return parse_url(s);

    bool all_digits = !s.empty() && std::all_of(s.begin(), s.end(),
        [](char c){ return std::isdigit((unsigned char)c); });
    if (all_digits) try { return Track{std::stoi(s)}; } catch (...) {}
    return Album{s};
}

} // namespace url
