#include "download.hh"
#include "url.hh"
#include "extras.hh"
#include "history.hh"
#include "i18n.hh"
#include "library.hh"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
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

// ── Progress reporting ──────────────────────────────────────────────────────
// Downloads are big and can be slow when the CDN edge is cold, so the line has
// to keep moving on its own — a counter that only ticks when a whole track
// lands looks identical to a hung process for minutes at a time.

using Clock = std::chrono::steady_clock;

static std::string format_bytes(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / 1073741824.0);
    else
        std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    return buf;
}

static std::string format_eta(double seconds) {
    if (!(seconds > 0) || seconds > 86400) return "--:--";
    int total = static_cast<int>(seconds + 0.5);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

namespace {

// Shared by both progress callbacks. Speed is an EWMA over a few seconds
// rather than a cumulative average, so the number reflects what the transfer
// is doing now instead of what it averaged since it started.
struct ProgressMeter {
    std::mutex mtx;
    std::unordered_map<int, std::pair<uint64_t, uint64_t>> per_track;  // id -> {done,total}
    std::unordered_set<int> finished;
    int track_count = 0;

    Clock::time_point started = Clock::now();
    Clock::time_point last_sample = Clock::now();
    Clock::time_point last_draw{};
    uint64_t last_bytes = 0;
    double speed = 0.0;      // bytes/sec, smoothed
    bool done_printed = false;

    uint64_t downloaded_locked() const {
        uint64_t sum = 0;
        for (const auto &[id, p] : per_track) sum += p.first;
        return sum;
    }

    // Album byte totals are only known for tracks that have started, so scale
    // what is known up to the full track count. Flagged approximate in output.
    uint64_t estimated_total_locked() const {
        uint64_t known = 0;
        size_t seen = 0;
        for (const auto &[id, p] : per_track) {
            if (p.second > 0) { known += p.second; ++seen; }
        }
        if (seen == 0) return 0;
        if (track_count <= 0 || seen >= static_cast<size_t>(track_count)) return known;
        return static_cast<uint64_t>(static_cast<double>(known) / seen * track_count);
    }

    void sample_speed_locked(uint64_t now_bytes) {
        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last_sample).count();
        if (dt < 0.25) return;
        double instant = (now_bytes - last_bytes) / dt;
        // ~5s time constant: chunked transfers hand bytes over in burstier
        // clumps than one long stream did, so a shorter window just jitters.
        double alpha = 1.0 - std::exp(-dt / 5.0);
        speed = (speed <= 0.0) ? instant : speed + alpha * (instant - speed);
        last_sample = now;
        last_bytes = now_bytes;
    }

    // Bytes per second over the whole transfer. This is what to print when a
    // download finishes: the live EWMA at that instant is measuring the last
    // few KB dribbling in, which reads as "0.1 MB/s" on an album that just
    // averaged twenty times that.
    double average_speed_locked(uint64_t done_bytes) const {
        double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        return elapsed > 0 ? done_bytes / elapsed : 0.0;
    }

    // An ETA needs both a settled speed estimate and enough of the transfer
    // done for the extrapolated total to mean anything. "ETA 13:51" on an
    // album that finished seconds later is worse than printing nothing.
    bool eta_is_meaningful_locked(uint64_t done_bytes, uint64_t est_total) const {
        double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        return elapsed >= 5.0 && speed > 0 && est_total > 0 &&
               done_bytes * 10 >= est_total;   // at least 10% in
    }

    bool should_draw_locked() {
        auto now = Clock::now();
        if (std::chrono::duration<double>(now - last_draw).count() < 0.2) return false;
        last_draw = now;
        return true;
    }
};

// The GUI's sink, if any. Read on every progress callback (i.e. from the
// download worker threads), written once at startup before any download
// exists — the mutex is what makes that "once" safe rather than assumed.
std::mutex g_sink_mtx;
dl::ProgressSink g_sink;

dl::ProgressSink current_sink() {
    std::lock_guard<std::mutex> lk(g_sink_mtx);
    return g_sink;
}

} // namespace

void set_progress_sink(ProgressSink sink) {
    std::lock_guard<std::mutex> lk(g_sink_mtx);
    g_sink = std::move(sink);
}

// Hands a failure to the front end. stderr still gets it too: the CLI is a
// terminal program and this must not change what it prints.
static void report_error(const std::string& what, const std::string& message) {
    std::fprintf(stderr, "%s %s\n", what.c_str(), message.c_str());
    if (auto sink = current_sink()) {
        Progress p;
        p.label = message;
        p.error = message;
        sink(p);
    }
}

// Single track: percentage plus live speed, so a stalled transfer is visible.
static kb::TrackProgressFn single_track_progress(std::string label) {
    auto m = std::make_shared<ProgressMeter>();
    m->track_count = 1;
    return [m, label](int track_id, uint64_t dl, uint64_t tot) {
        std::lock_guard<std::mutex> lk(m->mtx);
        m->per_track[track_id] = {dl, tot};
        m->sample_speed_locked(dl);
        bool complete = tot > 0 && dl >= tot;
        if (!complete && !m->should_draw_locked()) return;

        if (auto sink = current_sink()) {
            dl::Progress p;
            p.label        = label;
            p.done_tracks  = complete ? 1 : 0;
            p.total_tracks = 1;
            p.bytes        = dl;
            p.total_bytes  = tot;
            // The live EWMA at the finish line is measuring the last few KB
            // dribbling in; the average is what actually happened.
            p.speed_bps    = complete ? m->average_speed_locked(dl) : m->speed;
            if (!complete && m->eta_is_meaningful_locked(dl, tot) && m->speed > 0 && tot > dl)
                p.eta_seconds = static_cast<double>(tot - dl) / m->speed;
            p.complete     = complete;
            sink(p);
            return;
        }

        int pct = tot > 0 ? static_cast<int>(dl * 100 / tot) : 0;
        if (complete) {
            std::printf("\r  %3d%%  %s  %.1f MB/s average        ", pct,
                        format_bytes(dl).c_str(), m->average_speed_locked(dl) / 1048576.0);
        } else {
            double remaining = (m->speed > 0 && tot > dl) ? (tot - dl) / m->speed : 0;
            bool show_eta = m->eta_is_meaningful_locked(dl, tot);
            std::printf("\r  %3d%%  %s  %.1f MB/s  ETA %s   ", pct, format_bytes(dl).c_str(),
                        m->speed / 1048576.0,
                        show_eta ? format_eta(remaining).c_str() : "--:--");
        }
        std::fflush(stdout);
        if (complete && !m->done_printed) {
            m->done_printed = true;
            std::printf("\n");
        }
    };
}

// Album: aggregate bytes across every concurrent track, with speed and ETA.
static kb::TrackProgressFn album_progress(int total_tracks, std::string label) {
    auto m = std::make_shared<ProgressMeter>();
    m->track_count = total_tracks;
    return [m, label](int track_id, uint64_t dl, uint64_t tot) {
        std::lock_guard<std::mutex> lk(m->mtx);
        m->per_track[track_id] = {dl, tot};
        if (tot > 0 && dl >= tot) m->finished.insert(track_id);

        uint64_t done_bytes = m->downloaded_locked();
        m->sample_speed_locked(done_bytes);

        int n = static_cast<int>(m->finished.size());
        bool complete = n >= m->track_count;
        if (!complete && !m->should_draw_locked()) return;

        uint64_t est = m->estimated_total_locked();

        if (auto sink = current_sink()) {
            dl::Progress p;
            p.label        = label;
            p.done_tracks  = n;
            p.total_tracks = m->track_count;
            p.bytes        = done_bytes;
            p.total_bytes  = est;
            p.speed_bps    = complete ? m->average_speed_locked(done_bytes) : m->speed;
            if (!complete && m->eta_is_meaningful_locked(done_bytes, est) &&
                m->speed > 0 && est > done_bytes)
                p.eta_seconds = static_cast<double>(est - done_bytes) / m->speed;
            p.complete     = complete;
            sink(p);
            return;
        }

        if (complete) {
            std::printf("\r  [%2d/%d]  %s  %.1f MB/s average        ", n, m->track_count,
                        format_bytes(done_bytes).c_str(),
                        m->average_speed_locked(done_bytes) / 1048576.0);
        } else {
            double remaining =
                (m->speed > 0 && est > done_bytes) ? (est - done_bytes) / m->speed : 0;
            bool show_eta = m->eta_is_meaningful_locked(done_bytes, est);
            std::printf("\r  [%2d/%d]  %s/%s  %.1f MB/s  ETA %s   ", n, m->track_count,
                        format_bytes(done_bytes).c_str(), format_bytes(est).c_str(),
                        m->speed / 1048576.0,
                        show_eta ? format_eta(remaining).c_str() : "--:--");
        }
        std::fflush(stdout);
        if (complete && !m->done_printed) {
            m->done_printed = true;
            std::printf("\n");
        }
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
            opts.progress = single_track_progress("Track " + std::to_string(tid));
            auto res = kb::download_track(svc, tid, format_id, base_dir, opts);
            if (!res.ok()) {
                report_error(i18n::t("error_download"), res.error().message);
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
                report_error(i18n::t("error_download"), album_res.error().message);
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
            // The title, not the id: the id is what the filesystem needs, but
            // a progress line is for a person, who asked for a record by name.
            std::string label = al.title.value_or("Album " + aid);
            opts.progress = (total == 1) ? single_track_progress(label)
                                         : album_progress(total > 0 ? total : 1, label);
            std::printf("%s %s ...\n", i18n::t("downloading_album"), aid.c_str());

            std::vector<std::string> paths;
            auto res = kb::download_album(svc, aid, format_id, base_dir, opts);
            if (!res.ok()) {
                report_error(i18n::t("error_download"), res.error().message);
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
                report_error(i18n::t("error_download"), res.error().message);
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
                report_error(i18n::t("error_download"), res.error().message);
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
