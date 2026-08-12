#include "backup.hh"

#include "config.hh"
#include "download.hh"
#include "library.hh"
#include "service_factory.hh"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include <api/service.hh>
#include <sqlite3.h>

namespace fs = std::filesystem;

namespace backup {

namespace {

// ── sqlite plumbing ───────────────────────────────────────────────────────
// Same shape as src/library.cpp's Db and src/history.cpp's: open, use, close
// on scope exit. This one does not migrate anything — a backup file's schema
// arrives whole, from the catalog it was copied out of.
struct Sq {
    sqlite3 *db = nullptr;

    Sq(const fs::path &path, int flags) {
        if (sqlite3_open_v2(path.u8string().c_str(), &db, flags, nullptr) != SQLITE_OK) {
            std::string msg = "Cannot open ";
            msg += path.u8string() + ": ";
            msg += db ? sqlite3_errmsg(db) : "out of memory";
            sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error(msg);
        }
    }
    ~Sq() { if (db) sqlite3_close(db); }
    Sq(const Sq &) = delete;
    Sq &operator=(const Sq &) = delete;

    void exec(const std::string &sql) {
        char *err = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown sqlite error";
            sqlite3_free(err);
            throw std::runtime_error(msg);
        }
    }

    int64_t count(const std::string &table) {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db, ("SELECT COUNT(*) FROM " + table).c_str(), -1, &st,
                               nullptr) != SQLITE_OK)
            return 0;
        int64_t n = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int64(st, 0) : 0;
        sqlite3_finalize(st);
        return n;
    }
};

const char *col(sqlite3_stmt *st, int i) {
    const unsigned char *p = sqlite3_column_text(st, i);
    return p ? reinterpret_cast<const char *>(p) : "";
}

// SQL string literal: the only escape SQLite needs is a doubled quote. Used
// for paths in VACUUM INTO / ATTACH, which take an expression, not a binding.
std::string quote(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += '\'';
        out += c;
    }
    out += '\'';
    return out;
}

std::string now_iso8601() {
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buf;
}

std::string resolved_root(const std::string &explicit_root) {
    if (!explicit_root.empty()) return explicit_root;
    std::string dir = config::load().settings.download_dir.u8string();
    return dir.empty() ? std::string(".") : dir;
}

fs::path history_db_path() {
    return config::config_path().parent_path() / "history.db";
}

// Views the backup carries so that restore() and readable() are each one
// query. Created in the backup file, and dropped again when the catalog is
// laid down as a live library.db.
const char *kBackupSchema = R"sql(
CREATE TABLE IF NOT EXISTS backup_meta (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS backup_settings (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS backup_accounts (
  country TEXT, email TEXT, app_id TEXT, app_secret TEXT,
  user_id TEXT, auth_token TEXT);
CREATE TABLE IF NOT EXISTS backup_history (
  timestamp INTEGER, qobuz_id TEXT, item_type TEXT, title TEXT, artist_name TEXT,
  artist_id INTEGER, track_count INTEGER, quality TEXT, format_id INTEGER,
  output_dir TEXT, country TEXT, success INTEGER);

CREATE VIEW IF NOT EXISTS view_restore AS
SELECT COALESCE(t.album_id, '')       AS album_id,
       f.track_id                     AS track_id,
       COALESCE(f.country, '')        AS country,
       f.format_id                    AS format_id,
       f.quality                      AS quality,
       COALESCE(al.title, '')         AS album_title,
       COALESCE(ar.name, '')          AS artist_name,
       COALESCE(t.title, '')          AS track_title,
       COALESCE(al.tracks_count, 0)   AS tracks_count
FROM files f
JOIN tracks t        ON t.id = f.track_id
LEFT JOIN albums al  ON al.id = t.album_id
LEFT JOIN artists ar ON ar.id = al.artist_id;

CREATE VIEW IF NOT EXISTS view_readable AS
SELECT COALESCE(ar.name, '')                  AS artist,
       COALESCE(al.title, '')                 AS album,
       COALESCE(al.version, '')               AS album_version,
       COALESCE(al.release_date_original, '') AS released,
       COALESCE(lb.name, '')                  AS label,
       COALESCE(g.name, '')                   AS genre,
       COALESCE(al.upc, '')                   AS upc,
       COALESCE(t.media_number, 1)            AS disc,
       COALESCE(t.track_number, 0)            AS track_no,
       COALESCE(t.title, '')                  AS title,
       COALESCE(t.version, '')                AS track_version,
       COALESCE(t.isrc, '')                   AS isrc,
       COALESCE(t.duration, 0)                AS duration,
       f.quality                              AS quality,
       COALESCE(f.country, '')                AS country,
       COALESCE(t.album_id, '')               AS album_id,
       f.track_id                             AS track_id,
       f.rel_path                             AS rel_path
FROM files f
JOIN tracks t        ON t.id = f.track_id
LEFT JOIN albums al  ON al.id = t.album_id
LEFT JOIN artists ar ON ar.id = al.artist_id
LEFT JOIN labels lb  ON lb.id = al.label_id
LEFT JOIN genres g   ON g.id = al.genre_id;
)sql";

// Everything create() adds on top of the catalog, undone. Turning a backup
// file back into a plain library.db is exactly this plus emptying the two
// tables that claim files exist on disk.
const char *kStripBackupTables = R"sql(
DROP VIEW  IF EXISTS view_restore;
DROP VIEW  IF EXISTS view_readable;
DROP TABLE IF EXISTS backup_meta;
DROP TABLE IF EXISTS backup_settings;
DROP TABLE IF EXISTS backup_accounts;
DROP TABLE IF EXISTS backup_history;
)sql";

void put_kv(sqlite3 *db, const char *table, const std::string &key,
            const std::string &value) {
    sqlite3_stmt *st = nullptr;
    std::string sql = std::string("INSERT OR REPLACE INTO ") + table +
                      " (key, value) VALUES (?,?)";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::map<std::string, std::string> read_kv(sqlite3 *db, const char *table) {
    std::map<std::string, std::string> out;
    sqlite3_stmt *st = nullptr;
    std::string sql = std::string("SELECT key, value FROM ") + table;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(st) == SQLITE_ROW) out[col(st, 0)] = col(st, 1);
    sqlite3_finalize(st);
    return out;
}

// A backup file's sidecar journals, if a crash left any behind.
void remove_db_files(const fs::path &p) {
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(p.u8string() + "-wal", ec);
    fs::remove(p.u8string() + "-shm", ec);
}

std::string canonical_url(const RestoreItem &it) {
    return it.track_id.empty()
        ? "https://open.qobuz.com/album/" + it.album_id
        : "https://open.qobuz.com/track/" + it.track_id;
}

std::string describe(const RestoreItem &it) {
    std::string s = it.artist_name.empty() ? std::string("?") : it.artist_name;
    s += " — ";
    s += it.track_id.empty()
        ? (it.album_title.empty() ? it.album_id : it.album_title)
        : (it.track_title.empty() ? it.track_id : it.track_title);
    if (!it.country.empty()) s += " [" + it.country + "]";
    return s;
}

} // namespace

std::string catalog_path(const std::string &root) {
    return (fs::u8path(root) / ".streamer" / "library.db").u8string();
}

// ── create ────────────────────────────────────────────────────────────────

CreateReport create(const std::string &file, const CreateOptions &opts) {
    const std::string root = resolved_root(opts.root);
    const fs::path src = fs::u8path(catalog_path(root));
    if (!fs::exists(src))
        throw std::runtime_error(
            "No catalog at " + src.u8string() +
            " — run 'streamer library scan' first to build one.");

    const fs::path dest = fs::u8path(file);
    if (fs::exists(dest)) {
        if (!opts.overwrite)
            throw std::runtime_error(dest.u8string() +
                                     " already exists (pass --force to replace it)");
        remove_db_files(dest);
    }
    if (dest.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
    }

    // One statement copies the whole catalog, compacted, consistently. The
    // destination must not exist, which is why it was removed above.
    {
        Sq in(src, SQLITE_OPEN_READONLY);
        in.exec("VACUUM INTO " + quote(dest.u8string()));
    }

    config::Config cfg = config::load();
    CreateReport rep;

    Sq out(dest, SQLITE_OPEN_READWRITE);
    out.exec(kBackupSchema);
    out.exec("BEGIN");

    put_kv(out.db, "backup_meta", "format_version", std::to_string(FORMAT_VERSION));
    put_kv(out.db, "backup_meta", "created_at", now_iso8601());
    put_kv(out.db, "backup_meta", "source_root", root);
    put_kv(out.db, "backup_meta", "accounts_included",
           opts.include_accounts ? "1" : "0");

    const auto &s = cfg.settings;
    put_kv(out.db, "backup_settings", "download_dir", s.download_dir.u8string());
    put_kv(out.db, "backup_settings", "quality", s.quality);
    put_kv(out.db, "backup_settings", "requests_per_minute",
           std::to_string(s.requests_per_minute));
    put_kv(out.db, "backup_settings", "concurrency", std::to_string(s.concurrency));
    put_kv(out.db, "backup_settings", "language", s.language);

    if (opts.include_accounts) {
        for (const auto &a : cfg.accounts) {
            sqlite3_stmt *st = nullptr;
            if (sqlite3_prepare_v2(out.db,
                    "INSERT INTO backup_accounts "
                    "(country,email,app_id,app_secret,user_id,auth_token) "
                    "VALUES (?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK)
                continue;
            sqlite3_bind_text(st, 1, a.country.c_str(),    -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, a.email.c_str(),      -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, a.app_id.c_str(),     -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, a.app_secret.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, a.user_id.c_str(),    -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 6, a.auth_token.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_DONE) ++rep.accounts;
            sqlite3_finalize(st);
        }
    }

    out.exec("COMMIT");

    // Outside the transaction on purpose: SQLite refuses ATTACH and DETACH
    // while one is open.
    const fs::path hist = history_db_path();
    if (fs::exists(hist)) {
        // A missing or unreadable history is a nuisance, not a reason to lose
        // the backup — the catalog is what actually rebuilds the library.
        try {
            out.exec("ATTACH " + quote(hist.u8string()) + " AS h");
            out.exec("INSERT INTO backup_history "
                     "(timestamp,qobuz_id,item_type,title,artist_name,artist_id,"
                     " track_count,quality,format_id,output_dir,country,success) "
                     "SELECT timestamp,qobuz_id,item_type,title,artist_name,artist_id,"
                     " track_count,quality,format_id,output_dir,country,success "
                     "FROM h.downloads");
            out.exec("DETACH h");
        } catch (const std::exception &e) {
            std::fprintf(stderr, "warning: history not included (%s)\n", e.what());
            sqlite3_exec(out.db, "DETACH h", nullptr, nullptr, nullptr);
        }
    }

    rep.albums       = static_cast<int>(out.count("albums"));
    rep.tracks       = static_cast<int>(out.count("tracks"));
    rep.files        = static_cast<int>(out.count("files"));
    rep.assets       = static_cast<int>(out.count("assets"));
    rep.history_rows = static_cast<int>(out.count("backup_history"));

    std::error_code ec;
    rep.bytes = static_cast<int64_t>(fs::file_size(dest, ec));
    return rep;
}

// ── restore ───────────────────────────────────────────────────────────────

namespace {

// The backup's file rows, grouped into the smallest set of download targets
// that covers them: a whole album where every track was there, single tracks
// otherwise.
std::vector<RestoreItem> plan_items(sqlite3 *db) {
    struct Group {
        std::string album_title, artist_name;
        int tracks_count = 0;
        std::vector<RestoreItem> tracks;
    };
    // Keyed on everything that has to match for one download to cover the
    // group: same album, same country, same format.
    std::map<std::tuple<std::string, std::string, int>, Group> groups;
    std::vector<RestoreItem> loose;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT album_id, track_id, country, format_id, quality, "
            "       album_title, artist_name, track_title, tracks_count "
            "FROM view_restore", -1, &st, nullptr) != SQLITE_OK)
        throw std::runtime_error(std::string("Cannot read the backup: ") +
                                 sqlite3_errmsg(db));

    while (sqlite3_step(st) == SQLITE_ROW) {
        RestoreItem it;
        it.album_id    = col(st, 0);
        it.track_id    = std::to_string(sqlite3_column_int64(st, 1));
        it.country     = col(st, 2);
        it.format_id   = sqlite3_column_int(st, 3);
        it.quality     = col(st, 4);
        it.album_title = col(st, 5);
        it.artist_name = col(st, 6);
        it.track_title = col(st, 7);
        int tracks_count = sqlite3_column_int(st, 8);

        if (it.album_id.empty()) {          // a track whose album we never saw
            loose.push_back(std::move(it));
            continue;
        }
        auto &g = groups[{it.album_id, it.country, it.format_id}];
        g.album_title = it.album_title;
        g.artist_name = it.artist_name;
        g.tracks_count = tracks_count;
        g.tracks.push_back(std::move(it));
    }
    sqlite3_finalize(st);

    std::vector<RestoreItem> out;
    for (auto &[key, g] : groups) {
        std::set<std::string> distinct;
        for (const auto &t : g.tracks) distinct.insert(t.track_id);
        bool complete = g.tracks_count > 0 &&
                        static_cast<int>(distinct.size()) >= g.tracks_count;
        if (complete) {
            RestoreItem it;
            it.album_id    = std::get<0>(key);
            it.country     = std::get<1>(key);
            it.format_id   = std::get<2>(key);
            it.quality     = g.tracks.front().quality;
            it.album_title = g.album_title;
            it.artist_name = g.artist_name;
            out.push_back(std::move(it));
        } else {
            for (auto &t : g.tracks) out.push_back(std::move(t));
        }
    }
    for (auto &t : loose) out.push_back(std::move(t));

    std::sort(out.begin(), out.end(), [](const RestoreItem &a, const RestoreItem &b) {
        if (a.artist_name != b.artist_name) return a.artist_name < b.artist_name;
        if (a.album_title != b.album_title) return a.album_title < b.album_title;
        return a.track_id < b.track_id;
    });
    return out;
}

// Lay the backup's catalog down as the target root's live library.db, minus
// the backup-only tables and minus `files`/`assets` — those rows describe the
// *old* machine's disk, and keeping them would make the skip check below
// believe every track was already downloaded. The names and release metadata
// stay, so the library reads correctly before a single byte is fetched.
bool seed_catalog(const fs::path &backup_file, const std::string &root) {
    const fs::path target = fs::u8path(catalog_path(root));
    if (fs::exists(target)) return false;

    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (!fs::copy_file(backup_file, target, ec)) return false;

    try {
        Sq db(target, SQLITE_OPEN_READWRITE);
        db.exec(kStripBackupTables);
        db.exec("DELETE FROM files; DELETE FROM assets;");
        db.exec("VACUUM");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "warning: could not seed the catalog (%s)\n", e.what());
        fs::remove(target, ec);
        return false;
    }
    return true;
}

std::vector<config::Account> read_accounts(sqlite3 *db) {
    std::vector<config::Account> out;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT country,email,app_id,app_secret,user_id,auth_token "
            "FROM backup_accounts", -1, &st, nullptr) != SQLITE_OK)
        return out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        config::Account a;
        a.country    = col(st, 0);
        a.email      = col(st, 1);
        a.app_id     = col(st, 2);
        a.app_secret = col(st, 3);
        a.user_id    = col(st, 4);
        a.auth_token = col(st, 5);
        out.push_back(std::move(a));
    }
    sqlite3_finalize(st);
    return out;
}

void write_failures(const fs::path &path, const std::vector<RestoreItem> &items) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << "artist\talbum\ttrack\tcountry\tquality\talbum_id\ttrack_id\terror\n";
    for (const auto &it : items) {
        f << it.artist_name << '\t' << it.album_title << '\t' << it.track_title << '\t'
          << it.country << '\t' << it.quality << '\t' << it.album_id << '\t'
          << (it.track_id.empty() ? std::string("*") : it.track_id) << '\t'
          << it.error << '\n';
    }
}

} // namespace

RestoreReport restore(const std::string &file, const RestoreOptions &opts,
                      const Progress &progress) {
    const fs::path backup_file = fs::u8path(file);
    if (!fs::exists(backup_file))
        throw std::runtime_error("No such backup file: " + backup_file.u8string());

    auto say = [&](const std::string &line, int done, int total) {
        if (progress) progress(line, done, total);
    };

    Sq bk(backup_file, SQLITE_OPEN_READONLY);
    auto meta = read_kv(bk.db, "backup_meta");
    if (meta.find("format_version") == meta.end())
        throw std::runtime_error(backup_file.u8string() +
                                 " is not a streamer backup file.");
    int version = std::atoi(meta["format_version"].c_str());
    if (version > FORMAT_VERSION)
        throw std::runtime_error(
            "This backup was written by a newer streamer (format " +
            std::to_string(version) + ", this build understands " +
            std::to_string(FORMAT_VERSION) + "). Update streamer and try again.");

    auto settings = read_kv(bk.db, "backup_settings");
    auto backup_accounts = read_accounts(bk.db);

    RestoreReport rep;
    rep.root = !opts.dir.empty()             ? opts.dir
             : !settings["download_dir"].empty() ? settings["download_dir"]
             : resolved_root("");

    // ── config ────────────────────────────────────────────────────────────
    config::Config cur = config::load();
    if (opts.apply_config && !backup_accounts.empty() && !opts.dry_run) {
        bool fresh = !fs::exists(config::config_path()) || cur.accounts.empty();
        if (fresh || opts.force_config) {
            cur.accounts = backup_accounts;
            cur.settings.download_dir = fs::u8path(rep.root);
            if (!settings["quality"].empty()) cur.settings.quality = settings["quality"];
            if (!settings["language"].empty()) cur.settings.language = settings["language"];
            if (!settings["concurrency"].empty())
                cur.settings.concurrency =
                    static_cast<uint32_t>(std::atoi(settings["concurrency"].c_str()));
            if (!settings["requests_per_minute"].empty())
                cur.settings.requests_per_minute =
                    static_cast<uint32_t>(std::atoi(settings["requests_per_minute"].c_str()));
            config::save(cur);
            rep.config_written = true;
        }
    }
    // Accounts to download with: whatever is configured now, falling back to
    // the file's own when the config was deliberately left alone.
    std::vector<config::Account> accounts =
        cur.accounts.empty() ? backup_accounts : cur.accounts;

    // ── catalog ───────────────────────────────────────────────────────────
    if (!opts.dry_run) rep.catalog_written = seed_catalog(backup_file, rep.root);

    // ── work list ─────────────────────────────────────────────────────────
    std::vector<RestoreItem> items = plan_items(bk.db);

    // With --quality, what lands on disk is that format, not the one the
    // backup recorded — so that is what "already there" has to be measured
    // against, or every rerun would download everything again.
    if (!opts.quality_override.empty()) {
        const int fmt = dl::quality_to_format_id(opts.quality_override);
        for (auto &it : items) {
            it.format_id = fmt;
            it.quality   = opts.quality_override;
        }
    }

    // Skip what is already downloaded, one batched query per format id rather
    // than one lookup per item.
    std::map<int, std::vector<std::string>> album_ids_by_fmt, track_ids_by_fmt;
    for (const auto &it : items) {
        if (it.track_id.empty()) album_ids_by_fmt[it.format_id].push_back(it.album_id);
        else                     track_ids_by_fmt[it.format_id].push_back(it.track_id);
    }
    std::set<std::string> have_albums, have_tracks;
    for (auto &[fmt, ids] : album_ids_by_fmt) {
        for (auto &id : library::downloaded_album_ids(rep.root, ids, fmt))
            have_albums.insert(std::to_string(fmt) + ":" + id);
    }
    for (auto &[fmt, ids] : track_ids_by_fmt) {
        for (auto &id : library::downloaded_track_ids(rep.root, ids, fmt))
            have_tracks.insert(std::to_string(fmt) + ":" + id);
    }

    std::vector<RestoreItem> todo;
    for (auto &it : items) {
        const std::string key = std::to_string(it.format_id) + ":" +
                                (it.track_id.empty() ? it.album_id : it.track_id);
        bool present = it.track_id.empty() ? have_albums.count(key) > 0
                                           : have_tracks.count(key) > 0;
        if (present) ++rep.skipped;
        else         todo.push_back(std::move(it));
    }
    rep.planned = static_cast<int>(todo.size());

    if (opts.dry_run) {
        int i = 0;
        for (const auto &it : todo)
            say(describe(it), ++i, rep.planned);
        return rep;
    }

    // ── download ──────────────────────────────────────────────────────────
    int concurrency = opts.concurrency > 0
        ? opts.concurrency
        : (settings["concurrency"].empty() ? 8 : std::atoi(settings["concurrency"].c_str()));
    if (concurrency <= 0) concurrency = 8;

    // One authenticated service per country, built on first use: logging in
    // once per album would be a round trip per album for nothing.
    struct Svc {
        std::unique_ptr<kb::QobuzApiService> svc;
        bool exact = false;      // an account for this very country
        bool tried = false;
        std::string error;
    };
    std::map<std::string, Svc> services;

    auto service_for = [&](const std::string &country) -> Svc & {
        Svc &slot = services[country];
        if (slot.tried) return slot;
        slot.tried = true;

        const config::Account *chosen = nullptr;
        for (const auto &a : accounts)
            if (!country.empty() && a.country == country) { chosen = &a; slot.exact = true; break; }
        if (!chosen && !accounts.empty()) chosen = &accounts.front();
        if (!chosen) { slot.error = "no accounts configured"; return slot; }

        auto res = qobuz::make_service(*chosen, /*authenticate=*/true);
        if (!res.ok()) { slot.error = res.error().message; return slot; }
        slot.svc = std::make_unique<kb::QobuzApiService>(res.take());
        return slot;
    };

    int done = 0;
    for (auto &it : todo) {
        Svc &slot = service_for(it.country);
        if (!slot.exact) ++rep.no_account;
        if (!slot.svc) {
            it.error = slot.error.empty() ? "no usable account" : slot.error;
            rep.failures.push_back(it);
            ++rep.failed;
            say("failed: " + describe(it) + " — " + it.error, ++done, rep.planned);
            continue;
        }

        const std::string quality =
            opts.quality_override.empty() ? it.quality : opts.quality_override;

        say(describe(it), done + 1, rep.planned);
        bool ok = false;
        try {
            ok = dl::run(*slot.svc, canonical_url(it), quality, rep.root, it.country,
                         concurrency, /*save_extras=*/true, /*embed_metadata=*/true);
        } catch (const std::exception &e) {
            it.error = e.what();
        }
        if (ok) {
            ++rep.downloaded;
        } else {
            if (it.error.empty()) it.error = "download failed";
            rep.failures.push_back(it);
            ++rep.failed;
        }
        ++done;
    }

    if (!rep.failures.empty()) {
        rep.failures_path = backup_file.u8string() + ".failed.tsv";
        write_failures(fs::u8path(rep.failures_path), rep.failures);
    }
    return rep;
}

// ── readable dump ─────────────────────────────────────────────────────────

std::string readable(const std::string &file, bool tsv) {
    const fs::path path = fs::u8path(file);
    if (!fs::exists(path))
        throw std::runtime_error("No such file: " + path.u8string());

    Sq db(path, SQLITE_OPEN_READONLY);
    // A live library.db has the catalog but not the view, so create it in
    // memory as a temporary — read-only connections still allow TEMP objects.
    sqlite3_stmt *probe = nullptr;
    bool has_view = sqlite3_prepare_v2(db.db, "SELECT 1 FROM view_readable LIMIT 1", -1,
                                       &probe, nullptr) == SQLITE_OK;
    sqlite3_finalize(probe);
    std::string source = "view_readable";
    if (!has_view) {
        std::string tmp = kBackupSchema;
        // Only the readable view is wanted here; the tables would fail on a
        // read-only connection.
        auto start = tmp.find("CREATE VIEW IF NOT EXISTS view_readable");
        if (start == std::string::npos)
            throw std::runtime_error("Cannot build the readable view");
        std::string sql = "CREATE TEMP " + tmp.substr(start + 7);  // skip "CREATE "
        db.exec(sql);
        source = "temp.view_readable";
    }

    sqlite3_stmt *st = nullptr;
    const std::string sql =
        "SELECT artist, album, album_version, released, label, genre, upc, "
        "       disc, track_no, title, track_version, isrc, duration, quality, "
        "       country, album_id, track_id "
        "FROM " + source +
        " ORDER BY artist, album, disc, track_no";
    if (sqlite3_prepare_v2(db.db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        throw std::runtime_error(std::string("Cannot read the catalog: ") +
                                 sqlite3_errmsg(db.db));

    auto mmss = [](int seconds) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
        return std::string(buf);
    };

    std::ostringstream out;
    if (tsv) {
        out << "artist\talbum\talbum_version\treleased\tlabel\tgenre\tupc\t"
               "disc\ttrack\ttitle\ttrack_version\tisrc\tduration\tquality\t"
               "country\talbum_id\ttrack_id\n";
        while (sqlite3_step(st) == SQLITE_ROW) {
            for (int i = 0; i < 17; ++i) {
                if (i) out << '\t';
                if (i == 12) out << mmss(sqlite3_column_int(st, i));
                else         out << col(st, i);
            }
            out << '\n';
        }
    } else {
        std::string cur_artist, cur_album;
        while (sqlite3_step(st) == SQLITE_ROW) {
            std::string artist = col(st, 0), album = col(st, 1);
            if (artist != cur_artist) {
                out << (cur_artist.empty() ? "" : "\n") << artist << "\n";
                cur_artist = artist;
                cur_album.clear();
            }
            if (album != cur_album) {
                std::string ver = col(st, 2), released = col(st, 3);
                std::string label = col(st, 4), upc = col(st, 6);
                out << "  " << album;
                if (!ver.empty())      out << " (" << ver << ")";
                if (!released.empty()) out << "  " << released.substr(0, 4);
                if (!label.empty())    out << "  · " << label;
                if (!upc.empty())      out << "  · UPC " << upc;
                out << "  · id " << col(st, 15) << "  · " << col(st, 14) << "\n";
                cur_album = album;
            }
            std::string title = col(st, 9), tver = col(st, 10), isrc = col(st, 11);
            char num[16];
            std::snprintf(num, sizeof(num), "%2d", sqlite3_column_int(st, 8));
            out << "    " << num << ". " << title;
            if (!tver.empty()) out << " (" << tver << ")";
            out << "  " << mmss(sqlite3_column_int(st, 12))
                << "  " << col(st, 13);
            if (!isrc.empty()) out << "  ISRC " << isrc;
            out << "\n";
        }
    }
    sqlite3_finalize(st);
    return out.str();
}

} // namespace backup
