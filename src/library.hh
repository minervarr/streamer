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
#include <functional>
#include <optional>
#include <set>
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

    // Enough to find the album on disk and to show it: the country tier its
    // directory sits under (<root>/<country>/<album_id>/), the cover asset's
    // path relative to the root (empty when no cover was ever downloaded),
    // and the summed size of its files.
    std::string country;
    std::string cover_path;
    int64_t     bytes_on_disk = 0;
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

// What delete_album() did, or (dry_run) would have done. Unlike the rest of
// this header, deletion reports its failures instead of swallowing them: a
// download lost to a catalog hiccup is a nuisance, but a file the user asked
// to be gone that silently isn't is a lie about their disk.
struct DeleteReport {
    int     files_removed  = 0;   // audio files unlinked
    int     assets_removed = 0;   // cover.jpg / booklet.pdf / album_description.txt
    int64_t bytes_freed    = 0;
    std::vector<std::string> failed;   // paths that would not unlink
    bool    rows_removed   = false;    // the catalog rows are gone
};

// Removes an album from disk and from the catalog. Deleting only the rows
// would not stick — the next scan() re-adopts any file it finds — so this
// does both, files first.
//
// It never deletes a *path* recursively: it unlinks exactly the files the
// catalog lists for this album, then removes the album directory only if it
// came out empty. A stray file you put there survives, and a catalog that has
// lost track of an album cannot be talked into erasing a directory it no
// longer understands. Rows go in one transaction after the unlinks, so a
// half-failed delete leaves the catalog matching what is actually on disk.
DeleteReport delete_album(const std::string &root, const std::string &album_id,
                          bool dry_run = false);

std::vector<AlbumEntry> list_albums(const std::string &root, uint32_t limit);
std::vector<Entry>      list_tracks(const std::string &root, uint32_t limit);

// Which of `track_ids` already have a file on disk at exactly `format_id`.
// One batched query, cheap enough to call once per search result page —
// unlike resolve(), which rebuilds a whole Resolution per id.
std::set<std::string> downloaded_track_ids(const std::string &root,
                                           const std::vector<std::string> &track_ids,
                                           int format_id);

// Which of `album_ids` have every one of their tracks downloaded at exactly
// `format_id` (compares the count of tracks downloaded in that quality
// against albums.tracks_count).
std::set<std::string> downloaded_album_ids(const std::string &root,
                                           const std::vector<std::string> &album_ids,
                                           int format_id);

// True when the tree holds album directories but the catalog knows nothing —
// i.e. .streamer/library.db was lost. Distinguishes "empty library" from
// "library whose catalog needs rebuilding", which look identical otherwise.
bool catalog_looks_lost(const std::string &root);

struct ScanReport {
    int files_seen = 0;
    int removed    = 0;   // rows whose file is no longer on disk
    int adopted    = 0;   // files on disk the catalog did not know about
    int updated    = 0;   // rows whose recorded size no longer matched
    int assets     = 0;   // cover art / booklets / artist images re-registered
    int unknown    = 0;   // files whose name is not the ID-addressed pattern
    int fetched    = 0;   // albums whose metadata had to be re-fetched
};

// Supplies album metadata for an album id found on disk but missing from the
// catalog. May be null, in which case such files are counted as unknown
// rather than adopted — that is the difference between an offline scan and one
// that can rebuild a lost catalog from the network.
using AlbumFetcher = std::function<std::optional<kb::Album>(const std::string &album_id)>;

// Reconciles the catalog with what is actually in `root`. Nothing here talks
// to the filesystem during normal downloads, so this is the only thing that
// notices files deleted behind the app's back.
ScanReport scan(const std::string &root, const AlbumFetcher &fetch_album, bool dry_run);

} // namespace library
