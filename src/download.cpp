#include "download.hh"
#include "url.hh"
#include "extras.hh"
#include "history.hh"
#include "i18n.hh"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

#include <ae/sanitize.hh>
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
    auto res = svc.cdn_client().download_to_file(cov, dest.string());
    if (res.ok())
        std::printf("%s %s\n", i18n::t("saved"), dest.string().c_str());
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
        base_dir = (fs::path(output_dir) / country).string();

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

            auto album_res = svc.get_album(aid, std::string("track_ids"));
            if (!album_res.ok()) {
                std::fprintf(stderr, "%s %s\n",
                             i18n::t("error_download"), album_res.error().message.c_str());
                return;
            }
            const kb::Album &al = album_res.value();

            std::string artist_name = (al.artist && al.artist->name)
                ? *al.artist->name : "Unknown Artist";
            std::string album_disp = al.title.value_or("Unknown");
            if (al.version && !al.version->empty())
                album_disp += " (" + *al.version + ")";

            int artist_id = (al.artist && al.artist->id) ? *al.artist->id : 0;
            fs::path artist_dir = fs::u8path(base_dir)
                / fs::u8path(ae::sanitize_filename(artist_name));

            std::vector<std::string> paths;

            if (is_single_release(al)) {
                // Singles → base_dir/Artist/Singles/Title/
                fs::path single_dir = artist_dir / "Singles"
                    / fs::u8path(ae::sanitize_filename(album_disp));

                std::error_code ec;
                fs::create_directories(single_dir, ec);
                if (ec) {
                    std::fprintf(stderr, "Cannot create directory %s: %s\n",
                                 single_dir.string().c_str(), ec.message().c_str());
                } else {
                    std::printf("%s %s ...\n", i18n::t("downloading_album"), aid.c_str());
                    std::vector<int> tids = al.track_ids.value_or(std::vector<int>{});
                    int total = static_cast<int>(tids.size());
                    auto opts = base_opts(concurrency, embed_metadata);
                    opts.progress = (total == 1) ? single_track_progress()
                                                 : album_progress(total);
                    for (int tid : tids) {
                        auto tres = kb::download_track(svc, tid, format_id,
                                                       single_dir.string(), opts);
                        if (tres.ok()) {
                            paths.push_back(tres.value());
                        } else {
                            std::fprintf(stderr, "%s %s\n", i18n::t("error_download"),
                                         tres.error().message.c_str());
                        }
                    }
                    success = !paths.empty();
                    if (save_extras && success) {
                        extras::save_album_extras(svc, aid, single_dir);
                        save_cover(svc, al, single_dir);
                        if (artist_id > 0)
                            extras::save_artist_extras(svc, artist_id, artist_dir);
                    }
                }

            } else {
                // Regular album → base_dir/Artist/Album (quality)/
                int total = al.tracks_count.value_or(
                    al.track_ids ? static_cast<int>(al.track_ids->size()) : 0);
                auto opts = base_opts(concurrency, embed_metadata);
                opts.progress = (total == 1) ? single_track_progress()
                                             : album_progress(total > 0 ? total : 1);
                std::printf("%s %s ...\n", i18n::t("downloading_album"), aid.c_str());
                auto res = kb::download_album(svc, aid, format_id, base_dir, opts);
                if (!res.ok()) {
                    std::fprintf(stderr, "%s %s\n", i18n::t("error_download"),
                                 res.error().message.c_str());
                } else {
                    paths  = res.value();
                    success = !paths.empty();
                    if (save_extras && success) {
                        fs::path album_dir = fs::path(paths.front()).parent_path();
                        extras::save_album_extras(svc, aid, album_dir);
                        save_cover(svc, al, album_dir);
                        if (artist_id > 0)
                            extras::save_artist_extras(svc, artist_id, artist_dir);
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
                if (save_extras && success) {
                    fs::path artist_dir = fs::path(base_dir);
                    if (!res.value().empty())
                        artist_dir = fs::path(res.value().front()).parent_path().parent_path();
                    extras::save_artist_extras(svc, arid, artist_dir);
                }
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
