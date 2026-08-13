#include "inspect.hh"
#include "search.hh"   // fmt_duration
#include "i18n.hh"

#include <cstdio>

#include <api/service.hh>
#include <core/models.hh>

namespace inspect {

kb::Result<void> run_album(kb::QobuzApiService &svc, const std::string &album_id) {
    auto res = svc.get_album(album_id);
    if (!res.ok()) return res.error();
    const auto &album = res.value();

    std::string title  = album.title.value_or("?");
    std::string artist = (album.artist && album.artist->name) ? *album.artist->name : "?";
    std::string label  = (album.label && album.label->name)   ? *album.label->name  : "?";
    std::string date   = album.release_date_original.value_or("?");
    std::string genre  = (album.genre && album.genre->name)   ? *album.genre->name  : "?";
    int total_dur      = album.duration.value_or(0);
    int tracks_cnt     = album.tracks_count.value_or(0);

    bool hi = album.hires.value_or(false) || album.hires_streamable.value_or(false);
    const char *qual = "?";
    if (hi) {
        int bd = album.maximum_bit_depth.value_or(0);
        double sr = album.maximum_sampling_rate.value_or(0.0);
        if (bd >= 24 && sr >= 192.0)     qual = "flac-ultra";
        else if (bd >= 24 && sr >= 96.0) qual = "flac-hi";
        else                              qual = "flac";
    } else {
        qual = "mp3/flac";
    }

    std::printf("%s: %s\n", i18n::t("field_title"),  title.c_str());
    std::printf("%s: %s\n", i18n::t("field_artist"), artist.c_str());
    std::printf("%s: %s\n", i18n::t("field_label"),  label.c_str());
    std::printf("%s: %s\n", i18n::t("field_date"),   date.c_str());
    std::printf("%s: %s\n", i18n::t("field_genre"),  genre.c_str());
    std::printf("%s: %s\n", i18n::t("field_quality"), qual);
    std::printf("%s: %s (%d %s)\n", i18n::t("field_duration"),
                search::fmt_duration(total_dur).c_str(),
                tracks_cnt, i18n::t("tracks_suffix"));

    if (!album.tracks) {
        std::printf("\n%s\n", i18n::t("no_tracks"));
        return {};
    }

    auto &tr_res = *album.tracks;
    auto &items  = tr_res.items;
    if (!items || items->empty()) {
        std::printf("\n%s\n", i18n::t("no_tracks"));
        return {};
    }

    std::printf("\n");
    int media = -1;
    for (const auto &tp : *items) {
        if (!tp) continue;
        const auto &t = *tp;

        int disc = t.media_number.value_or(1);
        if (disc != media) {
            media = disc;
            std::printf("%s %d\n", i18n::t("disc"), disc);
        }

        int    num    = t.track_number.value_or(0);
        int    dur    = t.duration.value_or(0);
        bool   dl_ok  = t.downloadable.value_or(false);
        bool   hi_t   = t.hires.value_or(false);
        std::string ttitle = t.title.value_or("?");
        std::string tver   = t.version.value_or("");

        const char *avail = dl_ok ? "[DL]" : "    ";
        const char *hires = hi_t  ? " [Hi-Res]" : "";

        if (tver.empty())
            std::printf("  %2d. %-40s  %s  %s%s\n",
                num, ttitle.c_str(),
                search::fmt_duration(dur).c_str(), avail, hires);
        else
            std::printf("  %2d. %-40s  %s  %s%s\n",
                num, (ttitle + " (" + tver + ")").c_str(),
                search::fmt_duration(dur).c_str(), avail, hires);
    }
    return {};
}

kb::Result<void> run_track(kb::QobuzApiService &svc, int track_id) {
    auto res = svc.get_track(track_id);
    if (!res.ok()) return res.error();
    const auto &t = res.value();

    std::string title  = t.title.value_or("?");
    std::string tver   = t.version.value_or("");
    std::string artist = (t.performer && t.performer->name) ? *t.performer->name : "?";
    std::string album_t = (t.album && t.album->title) ? *t.album->title : "?";
    std::string date   = t.release_date_original.value_or("?");
    int         dur    = t.duration.value_or(0);
    bool        dl_ok  = t.downloadable.value_or(false);
    bool        hi_t   = t.hires.value_or(false);

    std::printf("%s: %s%s\n", i18n::t("field_title"),
                title.c_str(), tver.empty() ? "" : (" (" + tver + ")").c_str());
    std::printf("%s: %s\n", i18n::t("field_artist"),   artist.c_str());
    std::printf("%s: %s\n", i18n::t("field_album"),    album_t.c_str());
    std::printf("%s: %s\n", i18n::t("field_date"),     date.c_str());
    std::printf("%s: %s\n", i18n::t("field_duration"), search::fmt_duration(dur).c_str());
    std::printf("%s: %s\n", i18n::t("field_hires"),    hi_t  ? i18n::t("yes") : i18n::t("no"));
    std::printf("%s: %s\n", i18n::t("field_download"), dl_ok ? i18n::t("yes") : i18n::t("no"));
    return {};
}

static std::string esc_tsv(const std::string &s) {
    std::string r;
    for (char c : s) { if (c == '\t' || c == '\n' || c == '\r') r += ' '; else r += c; }
    return r;
}

kb::Result<void> run_album_tsv(kb::QobuzApiService &svc, const std::string &album_id) {
    auto res = svc.get_album(album_id);
    if (!res.ok()) return res.error();
    const auto &album = res.value();

    std::string title  = esc_tsv(album.title.value_or(""));
    std::string artist = (album.artist && album.artist->name) ? esc_tsv(*album.artist->name) : "";
    std::string year   = album.release_date_original.value_or("").substr(0, 4);
    std::string label  = (album.label && album.label->name) ? esc_tsv(*album.label->name) : "";
    std::string rtype  = esc_tsv(album.release_type.value_or(""));
    int hires_flag     = (album.hires.value_or(false) || album.hires_streamable.value_or(false)) ? 1 : 0;

    // Count locked (not downloadable) tracks
    int locked_count = 0;
    if (album.tracks && album.tracks->items) {
        for (auto &tp : *album.tracks->items) {
            if (tp && !tp->downloadable.value_or(true)) ++locked_count;
        }
    }

    std::printf("album\t%s\t%s\t%s\t%s\t%s\t%d\t%d\n",
        title.c_str(), artist.c_str(), year.c_str(),
        label.c_str(), rtype.c_str(), hires_flag, locked_count);

    if (!album.tracks || !album.tracks->items) return {};
    for (auto &tp : *album.tracks->items) {
        if (!tp) continue;
        const auto &t = *tp;
        std::string ttitle = esc_tsv(t.title.value_or(""));
        if (t.version) ttitle += " (" + esc_tsv(*t.version) + ")";
        std::printf("track\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\n",
            t.track_number.value_or(0),
            t.media_number.value_or(1),
            ttitle.c_str(),
            t.duration.value_or(0),
            t.streamable.value_or(false) ? 1 : 0,
            t.downloadable.value_or(false) ? 1 : 0,
            t.hires.value_or(false) ? 1 : 0,
            (t.parental_warning == std::optional<bool>{true}) ? 1 : 0);
    }
    return {};
}

} // namespace inspect
