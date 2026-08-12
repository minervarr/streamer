#pragma once

// One file that holds everything needed to rebuild a library on another
// machine: the catalog (every album, track, artist, label, genre and the
// byte-exact names behind the ID-addressed paths), the settings, every
// account, and the download history.
//
// The file *is* a SQLite database — a VACUUM INTO copy of <root>/.streamer/
// library.db with a handful of backup_* tables appended. Nothing here
// re-serializes the catalog into another format: the catalog already is the
// data, and copying it is one statement. Its own format version lives in
// backup_meta so a newer file can be refused politely instead of misread.
//
// The music itself is never copied. Downloads are ID-addressed
// (<root>/<country>/<album_id>/<track_id>.<format_id>.<ext>), so a list of
// ids is enough to fetch every byte again — a few hundred KB standing in for
// hundreds of GB. The country each album was downloaded under is part of that
// list, because availability is regional: restore() re-downloads each album
// with an account from the same country it originally came from.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace backup {

// Bump when the backup_* tables change shape. restore() refuses a file whose
// version is higher than this — an old binary must not half-read a new file.
constexpr int FORMAT_VERSION = 1;

// ── create ────────────────────────────────────────────────────────────────

struct CreateOptions {
    std::string root;                  // library root; empty = configured download_dir
    bool include_accounts = true;      // auth tokens travel in the file, in the clear
    bool overwrite        = false;     // VACUUM INTO refuses an existing file
};

struct CreateReport {
    int     albums       = 0;
    int     tracks       = 0;
    int     files        = 0;
    int     assets       = 0;
    int     accounts     = 0;
    int     history_rows = 0;
    int64_t bytes        = 0;          // size of the backup file itself
};

// Throws std::runtime_error on any failure (no catalog, unwritable path,
// destination exists without overwrite).
CreateReport create(const std::string &file, const CreateOptions &opts);

// ── restore ───────────────────────────────────────────────────────────────

struct RestoreOptions {
    std::string dir;                   // target root; empty = the backup's download_dir
    std::string quality_override;      // empty = the quality each file was saved at
    int  concurrency  = 0;             // 0 = the backup's setting
    bool apply_config = true;          // write settings+accounts when none exist yet
    bool force_config = false;         // overwrite an existing config that has accounts
    bool dry_run      = false;
};

// One unit of work: a whole album when every one of its tracks was present in
// the backup, otherwise a single track.
struct RestoreItem {
    std::string album_id;
    std::string track_id;              // empty for a whole-album item
    std::string album_title;
    std::string artist_name;
    std::string track_title;           // empty for a whole-album item
    std::string country;
    std::string quality;
    int         format_id = 0;
    std::string error;                 // filled in only on the failure list
};

struct RestoreReport {
    std::string root;                  // where things were actually written
    int planned      = 0;
    int skipped      = 0;              // already on disk
    int downloaded   = 0;
    int failed       = 0;
    int no_account   = 0;              // no account for that country; default used
    bool config_written  = false;
    bool catalog_written = false;
    std::vector<RestoreItem> failures;
    std::string failures_path;         // <file>.failed.tsv, when any failed
};

// `line` is a human-readable status; done/total count RestoreItems.
using Progress = std::function<void(const std::string &line, int done, int total)>;

// Throws std::runtime_error when the file is unreadable or too new; individual
// download failures are collected in the report instead. Rerunning a restore is
// safe and cheap: anything already on disk is skipped, so an interrupted run
// resumes where it stopped.
RestoreReport restore(const std::string &file, const RestoreOptions &opts,
                      const Progress &progress);

// ── readable dump ─────────────────────────────────────────────────────────

// The names, for a human. `file` may be a backup file or a live library.db —
// both carry the same catalog tables. `tsv` gives one flat row per track for a
// spreadsheet; otherwise a listing grouped by artist and album.
std::string readable(const std::string &file, bool tsv);

// <root>/.streamer/library.db, the live catalog for a library root.
std::string catalog_path(const std::string &root);

} // namespace backup
