#include "download.hh"
#include "url.hh"
#include "extras.hh"
#include "history.hh"
#include "i18n.hh"
#include "library.hh"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

#include <api/service.hh>
#include <core/models.hh>
#include <download/download.hh>
#include <metadata/config.hh>

namespace fs = std::filesystem;

namespace dl {

int quality_to_format_id(const std::string &quality) {
    if (quality == "mp3")        return kb::quality::MP3_320;
    if (quality == "flac")       return kb::quality::FLAC_16_44;
    if (quality == "flac-hi")    return kb::quality::FLAC_24_96;
    if (quality == "flac-ultra") return kb::quality::FLAC_24_192;
    return kb::quality::FLAC_16_44;
}

std::string format_id_to_quality(int fmt) {
    switch (fmt) {
        case kb::quality::MP3_320:    return "mp3";
        case kb::quality::FLAC_16_44: return "flac";
        case kb::quality::FLAC_24_96: return "flac-hi";
        case kb::quality::FLAC_24_192:return "flac-ultra";
        default: return "flac";
    }
}

static std::string best_cover_url(const kb::Image &img) {
    for (const auto *p : {&img.mega, &img.extra_large, &img.large, &img.medium, &img.small}) {
        if (p && p->has_value()) {
            std::string url = p->value();
            auto pos = url.rfind("600");
            if (pos != std::string::npos) url = url.substr(0, pos) + "org" + url.substr(pos + 3);
            return url;
        }
    }
    if (img.url) return img.url.value();
    return {};
}

// Progress callback for a single track: prints one updating percentage line.
static kb::TrackProgressFn single_track_progress() {
    auto last = std::make_shared<std::atomic<int>>(-1);
    return [last](int, uint64_t dl, uint64_t tot) {
        if (tot == 0) return;
        int pct = static_cast<int>(dl * 100 / tot);
        if (last->exchange(pct) == pct) return;
        std::printf("\r  %3d%%", pct);
        std::fflush(stdout);
        if (pct >= 100) std::printf("\n");
    };
}

// Progress callback for a multi-track album: prints [N/total] once per completed track.
static kb::TrackProgressFn album_progress(int total_tracks) {
    struct State {
        std::mutex mtx;
        std::unordered_set<int> done;
        int total;
    };
    auto s = std::make_shared<State>();
    s->total = total_tracks;
    return [s](int track_id, uint64_t dl, uint64_t tot) {
        if (tot == 0 || dl < tot) return;
        std::lock_guard<std::mutex> lk(s->mtx);
        if (!s->done.insert(track_id).second) return;
        int n = static_cast<int>(s->done.size());
        std::printf("\r  [%d/%d]", n, s->total);
        std::fflush(stdout);
        if (n >= s->total) std::printf("\n");
    };
}

static kb::DownloadOptions base_opts(int concurrency, bool embed_metadata) {
    kb::DownloadOptions opts;
    opts.concurrency = concurrency;
    if (embed_metadata) {
        kb::MetadataConfig meta;
        meta.set(kb::MetadataField::CoverArt, false);
        opts.metadata = meta;
    }
    return opts;
}

// Sidecar assets that belong to no single album live under the library root's
// .streamer/ directory, next to library.db — the top level of the library is
// nothing but album-id directories. `root` is the un-suffixed download dir, so
// artist assets are shared across country subtrees. Created on demand because
// extras::save_artist_extras only writes files.
static fs::path artist_asset_dir(const std::string &root, int artist_id) {
    fs::path dir = fs::u8path(root) / ".streamer" / "artists" / std::to_string(artist_id);
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// extras::save_* write straight to disk without reporting what they produced,
// so pick the results up afterwards by looking for the names they use.
static void record_assets(const std::string &root, const fs::path &dir,
                          const std::string *album_id, const int *artist_id,
                          std::initializer_list<std::pair<const char *, const char *>> names) {
    for (auto [filename, kind] : names) {
        fs::path p = dir / filename;
        if (fs::exists(p)) library::record_asset(root, kind, album_id, artist_id, p.u8string());
    }
}

static bool is_single_release(const kb::Album &album) {
    auto check = [](const std::optional<std::string> &s) {
        if (!s) return false;
        std::string lc = *s;
        for (char &c : lc) c = static_cast<char>(std::tolower((unsigned char)c));
        return lc == "single";
    };
    return check(album.release_type) || check(album.product_type);
}

static void save_cover(const kb::QobuzApiService &svc, const kb::Album &al, const fs::path &dir) {
    if (!al.image) return;
    std::string cov = best_cover_url(*al.image);
    if (cov.empty()) return;
    fs::path dest = dir / "cover.jpg";
    if (fs::exists(dest)) return;
    auto res = svc.cdn_client().download_to_file(cov, dest.u8string());
    if (res.ok())
        std::printf("%s %s\n", i18n::t("saved"), dest.u8string().c_str());
}

bool run(kb::QobuzApiService &svc,
         const std::string &target_url,
         const std::string &quality,
         const std::string &output_dir,
         const std::string &country,
         int concurrency,
         bool save_extras,
         bool embed_metadata)
{
    auto target_opt = url::parse(target_url);
    if (!target_opt) {
        std::fprintf(stderr, "%s: %s\n", i18n::t("error_parse_url"), target_url.c_str());
        return false;
    }

    std::string base_dir = output_dir;
    if (!country.empty())
        base_dir = (fs::path(output_dir) / country).u8string();

    int format_id = quality_to_format_id(quality);
    bool success = false;

    std::visit([&](auto &&target) {
        using T = std::decay_t<decltype(target)>;

        // ── single track URL ──────────────────────────────────────────────────
        if constexpr (std::is_same_v<T, url::Track>) {
            int tid = target.id;
            std::printf("%s %d ...\n", i18n::t("downloading_track"), tid);
            auto opts = base_opts(concurrency, embed_metadata);
            opts.progress = single_track_progress();
            auto res = kb::download_track(svc, tid, format_id, base_dir, opts);
            if (!res.ok()) {
                std::fprintf(stderr, "%s %s\n", i18n::t("error_download"),
                             res.error().message.c_str());
            } else {
                std::printf("%s %s\n", i18n::t("saved"), res.value().c_str());
                success = true;
            }
            auto track_res = svc.get_track(tid);
            std::string title_str, artist_str;
            if (track_res.ok()) {
                title_str  = track_res.value().title.value_or("");
                artist_str = (track_res.value().performer && track_res.value().performer->name)
                    ? *track_res.value().performer->name : "";
                if (success)
                    library::record_track_download(output_dir, track_res.value(),
                                                   res.value(), quality, format_id, country);
            }
            const std::string *ptitle   = title_str.empty()  ? nullptr : &title_str;
            const std::string *partist  = artist_str.empty() ? nullptr : &artist_str;
            const std::string *pout     = base_dir.empty()   ? nullptr : &base_dir;
            const std::string *pcountry = country.empty()    ? nullptr : &country;
            history::record(std::to_string(tid), "track",
                            ptitle, partist, nullptr, nullptr,
                            quality, format_id, pout, pcountry, success);

        // ── album or single URL ───────────────────────────────────────────────
        } else if constexpr (std::is_same_v<T, url::Album>) {
            const std::string &aid = target.id;

            // extra=track_ids also returns the full `tracks` array — titles,
            // numbers, ISRCs, durations — which is everything the catalog
            // needs now that the filesystem carries none of it. ("tracks" is
            // not an accepted extra; the array comes back regardless.)
            auto album_res = svc.get_album(aid, std::string("track_ids"));
            if (!album_res.ok()) {
                std::fprintf(stderr, "%s %s\n",
                             i18n::t("error_download"), album_res.error().message.c_str());
                return;
            }
            const kb::Album &al = album_res.value();

            int artist_id = (al.artist && al.artist->id) ? *al.artist->id : 0;

            // Singles are no longer a special case: with ID-addressed
            // directories a one-track release is just a short album.
            int total = al.tracks_count.value_or(
                (al.tracks && al.tracks->items)
                    ? static_cast<int>(al.tracks->items->size()) : 0);
            auto opts = base_opts(concurrency, embed_metadata);
            opts.progress = (total == 1) ? single_track_progress()
                                         : album_progress(total > 0 ? total : 1);
            std::printf("%s %s ...\n", i18n::t("downloading_album"), aid.c_str());

            std::vector<std::string> paths;
            auto res = kb::download_album(svc, aid, format_id, base_dir, opts);
            if (!res.ok()) {
                std::fprintf(stderr, "%s %s\n", i18n::t("error_download"),
                             res.error().message.c_str());
            } else {
                paths  = res.value();
                success = !paths.empty();
                // Before the extras: assets carry a foreign key onto albums,
                // so the album row has to exist first.
                library::record_download(output_dir, al, paths, quality, format_id,
                                         country);
                if (save_extras && success) {
                    fs::path album_dir = fs::u8path(paths.front()).parent_path();
                    extras::save_album_extras(svc, aid, album_dir);
                    save_cover(svc, al, album_dir);
                    record_assets(output_dir, album_dir, &aid, nullptr,
                                  {{"cover.jpg", "cover"},
                                   {"booklet.pdf", "booklet"},
                                   {"album_description.txt", "description"}});
                    if (artist_id > 0) {
                        fs::path adir = artist_asset_dir(output_dir, artist_id);
                        extras::save_artist_extras(svc, artist_id, adir);
                        record_assets(output_dir, adir, nullptr, &artist_id,
                                      {{"artist.jpg", "artist_image"},
                                       {"artist_bio.txt", "artist_bio"}});
                    }
                }
            }

            std::string title_str   = al.title.value_or("");
            std::string artist_str2 = (al.artist && al.artist->name) ? *al.artist->name : "";
            int track_cnt = al.tracks_count.value_or(0);
            const std::string *ptitle   = title_str.empty()   ? nullptr : &title_str;
            const std::string *partist  = artist_str2.empty() ? nullptr : &artist_str2;
            const std::string *pout     = base_dir.empty()    ? nullptr : &base_dir;
            const std::string *pcountry = country.empty()     ? nullptr : &country;
            const int32_t *ptc = (track_cnt > 0)
                ? reinterpret_cast<const int32_t *>(&track_cnt) : nullptr;
            history::record(aid, is_single_release(al) ? "single" : "album",
                            ptitle, partist, nullptr, ptc,
                            quality, format_id, pout, pcountry, success);

        // ── artist URL ────────────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<T, url::Artist>) {
            int arid = target.id;
            std::printf("%s %d ...\n", i18n::t("downloading_artist"), arid);
            auto opts = base_opts(concurrency, embed_metadata);
            auto res = kb::download_artist(svc, arid, format_id, base_dir, opts);
            if (!res.ok()) {
                std::fprintf(stderr, "%s %s\n", i18n::t("error_download"),
                             res.error().message.c_str());
            } else {
                success = !res.value().empty();
                if (save_extras && success)
                    extras::save_artist_extras(svc, arid, artist_asset_dir(output_dir, arid));
            }
            auto artist_info = svc.get_artist(arid);
            std::string artist_str;
            if (artist_info.ok()) artist_str = artist_info.value().name.value_or("");
            const std::string *partist  = artist_str.empty() ? nullptr : &artist_str;
            const std::string *pout     = base_dir.empty()   ? nullptr : &base_dir;
            const std::string *pcountry = country.empty()    ? nullptr : &country;
            history::record(std::to_string(arid), "artist", nullptr, partist, nullptr, nullptr,
                            quality, format_id, pout, pcountry, success);

        // ── playlist URL ──────────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<T, url::Playlist>) {
            const std::string &pid = target.id;
            std::printf("%s %s ...\n", i18n::t("downloading_playlist"), pid.c_str());
            auto opts = base_opts(concurrency, embed_metadata);
            auto res = kb::download_playlist(svc, pid, format_id, base_dir, opts);
            if (!res.ok()) {
                std::fprintf(stderr, "%s %s\n", i18n::t("error_download"),
                             res.error().message.c_str());
            } else {
                success = !res.value().empty();
            }
            auto pl_info = svc.get_playlist(pid);
            std::string title_str;
            if (pl_info.ok()) title_str = pl_info.value().name.value_or("");
            const std::string *ptitle   = title_str.empty() ? nullptr : &title_str;
            const std::string *pout     = base_dir.empty()  ? nullptr : &base_dir;
            const std::string *pcountry = country.empty()   ? nullptr : &country;
            history::record(pid, "playlist", ptitle, nullptr, nullptr, nullptr,
                            quality, format_id, pout, pcountry, success);
        }
    }, *target_opt);

    return success;
}

} // namespace dl
