#pragma once

// The library catalog: a SQLite database living inside the library root, at
// <root>/.streamer/library.db.
//
// Downloads are ID-addressed on disk — album directories are Qobuz album ids,
// track files are "{track_id}.{format_id}.{ext}" — so that no filesystem's
// reserved-character rules can ever mangle a title and paths stay stable when
// upstream metadata is corrected. That makes this catalog the only place the
// real, byte-exact names live, plus the release metadata worth querying
// (dates, labels, genres, durations, bit depth).
//
// It travels with the music: put the drive in another machine and its names
// come with it. Schema version is tracked in PRAGMA user_version so later
// columns can arrive as ordered migrations.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <core/models.hh>

namespace library {

// One downloaded file, joined to the names it stands for.
struct Entry {
    std::string rel_path;                        // relative to the library root
    int64_t     track_id = 0;
    std::optional<int> track_number;
    std::optional<int> media_number;
    std::string track_title;                     // real title, byte-exact
    std::string album_id;
    std::string album_title;
    std::string artist_name;
    std::string quality;
    int         format_id = 0;
    std::optional<int> duration;
};

struct AlbumEntry {
    std::string id;
    std::string title;                           // real title, byte-exact
    std::string version;
    std::string artist_name;
    std::string label_name;
    std::string release_date;
    std::optional<int> tracks_count;
    int         files_on_disk = 0;
};

struct Resolution {
    AlbumEntry album;
    std::vector<Entry> tracks;
};

// Upsert an album, its tracks, and their artists/label/genre, then register
// each path in `paths` as a file on disk. One transaction. Titles are stored
// exactly as Qobuz returned them — never sanitized. Never throws: a catalog
// failure must not lose a finished download, so problems are warned about on
// stderr and swallowed.
void record_download(const std::string &root, const kb::Album &album,
                     const std::vector<std::string> &paths, const std::string &quality,
                     int format_id, const std::string &country);

// Same for a lone track download; the album stub carried by the track is
// upserted alongside it.
void record_track_download(const std::string &root, const kb::Track &track,
                           const std::string &path, const std::string &quality,
                           int format_id, const std::string &country);

// Cover art, booklets, artist images/bios. `kind` is free-form
// ("cover", "booklet", "artist_image", ...).
void record_asset(const std::string &root, const std::string &kind,
                  const std::string *album_id, const int *artist_id,
                  const std::string &path);

// `key` may be an album id, a track id, or a path to a downloaded file
// (absolute or relative to the root). Empty when nothing matches.
std::optional<Resolution> resolve(const std::string &root, const std::string &key);

std::vector<AlbumEntry> list_albums(const std::string &root, uint32_t limit);
std::vector<Entry>      list_tracks(const std::string &root, uint32_t limit);

} // namespace library
