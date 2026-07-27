#include "library.hh"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#include <sqlite3.h>

namespace fs = std::filesystem;

namespace library {

namespace {

constexpr int SCHEMA_VERSION = 1;

fs::path db_path(const std::string &root) {
    return fs::u8path(root) / ".streamer" / "library.db";
}

int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Mirrors src/history.cpp's Db: open, ensure schema, close on scope exit.
struct Db {
    sqlite3 *db = nullptr;

    explicit Db(const fs::path &path) {
        fs::create_directories(path.parent_path());
        if (sqlite3_open(path.u8string().c_str(), &db) != SQLITE_OK) {
            std::string msg = "Cannot open library db: ";
            msg += sqlite3_errmsg(db);
            sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error(msg);
        }
        sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
        migrate();
    }
    ~Db() { if (db) sqlite3_close(db); }
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;

    int user_version() {
        sqlite3_stmt *st = nullptr;
        int v = 0;
        if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        return v;
    }

    // Version 0 means "no schema yet". Later versions append their ALTERs
    // here as additional `if (v < N)` blocks, each bumping user_version.
    void migrate() {
        if (user_version() >= SCHEMA_VERSION) return;
        static const char *kSchema = R"sql(
CREATE TABLE IF NOT EXISTS artists (
  id INTEGER PRIMARY KEY, name TEXT NOT NULL, image_url TEXT, bio TEXT,
  albums_count INTEGER, updated_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS labels (id INTEGER PRIMARY KEY, name TEXT NOT NULL, slug TEXT);
CREATE TABLE IF NOT EXISTS genres (id INTEGER PRIMARY KEY, name TEXT NOT NULL, slug TEXT);

CREATE TABLE IF NOT EXISTS albums (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  version TEXT,
  artist_id INTEGER REFERENCES artists(id),
  label_id  INTEGER REFERENCES labels(id),
  genre_id  INTEGER REFERENCES genres(id),
  upc TEXT, copyright TEXT,
  release_date_original TEXT, release_date_stream TEXT, released_at INTEGER,
  duration INTEGER, tracks_count INTEGER, media_count INTEGER,
  product_type TEXT, release_type TEXT,
  parental_warning INTEGER, hires INTEGER,
  maximum_bit_depth INTEGER, maximum_sampling_rate REAL, maximum_channel_count INTEGER,
  image_thumbnail TEXT, image_large TEXT, image_max TEXT,
  updated_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS tracks (
  id INTEGER PRIMARY KEY,
  album_id TEXT REFERENCES albums(id) ON DELETE CASCADE,
  title TEXT NOT NULL,
  version TEXT, isrc TEXT,
  track_number INTEGER, media_number INTEGER, duration INTEGER,
  performer_id INTEGER REFERENCES artists(id),
  composer_id  INTEGER REFERENCES artists(id),
  parental_warning INTEGER, copyright TEXT,
  maximum_bit_depth INTEGER, maximum_sampling_rate REAL, maximum_channel_count INTEGER,
  updated_at INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS tracks_album ON tracks(album_id);

CREATE TABLE IF NOT EXISTS album_artists (
  album_id TEXT NOT NULL REFERENCES albums(id) ON DELETE CASCADE,
  artist_id INTEGER NOT NULL REFERENCES artists(id),
  role TEXT,
  PRIMARY KEY (album_id, artist_id, role));

CREATE TABLE IF NOT EXISTS files (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
  rel_path TEXT NOT NULL UNIQUE,
  format_id INTEGER NOT NULL, quality TEXT NOT NULL, country TEXT,
  bytes INTEGER, mtime INTEGER, downloaded_at INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS files_track ON files(track_id);

CREATE TABLE IF NOT EXISTS assets (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  kind TEXT NOT NULL,
  album_id TEXT REFERENCES albums(id) ON DELETE CASCADE,
  artist_id INTEGER REFERENCES artists(id),
  rel_path TEXT NOT NULL UNIQUE, bytes INTEGER);

CREATE VIEW IF NOT EXISTS view_library AS
SELECT f.rel_path, f.quality, f.format_id, f.country,
       t.id AS track_id, t.track_number, t.media_number, t.title AS track_title,
       t.duration, t.isrc,
       al.id AS album_id, al.title AS album_title, al.version AS album_version,
       al.release_date_original, al.maximum_bit_depth, al.maximum_sampling_rate,
       ar.name AS artist_name, lb.name AS label_name, g.name AS genre_name
FROM files f
JOIN tracks t        ON t.id = f.track_id
LEFT JOIN albums al  ON al.id = t.album_id
LEFT JOIN artists ar ON ar.id = al.artist_id
LEFT JOIN labels lb  ON lb.id = al.label_id
LEFT JOIN genres g   ON g.id = al.genre_id;
)sql";
        char *err = nullptr;
        if (sqlite3_exec(db, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("Cannot create library schema: " + msg);
        }
        sqlite3_exec(db, ("PRAGMA user_version=" + std::to_string(SCHEMA_VERSION)).c_str(),
                     nullptr, nullptr, nullptr);
    }
};

// Prepared-statement wrapper: 1-based binds that accept optionals directly,
// so an absent Qobuz field becomes SQL NULL rather than a sentinel.
struct Stmt {
    sqlite3_stmt *st = nullptr;
    int next = 1;

    Stmt(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("bad SQL: ") + sqlite3_errmsg(db));
        }
    }
    ~Stmt() { if (st) sqlite3_finalize(st); }
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;

    Stmt &bind(const std::string &v) {
        sqlite3_bind_text(st, next++, v.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    Stmt &bind(int64_t v) { sqlite3_bind_int64(st, next++, v); return *this; }
    Stmt &bind(int v)     { sqlite3_bind_int(st, next++, v);   return *this; }
    Stmt &bind(double v)  { sqlite3_bind_double(st, next++, v); return *this; }
    Stmt &bind_null()     { sqlite3_bind_null(st, next++);     return *this; }

    template <typename T>
    Stmt &bind(const std::optional<T> &v) {
        if (v) return bind(*v);
        return bind_null();
    }
    Stmt &bind(const std::optional<bool> &v) {
        if (v) return bind(*v ? 1 : 0);
        return bind_null();
    }

    bool step() { return sqlite3_step(st) == SQLITE_ROW; }

    // Throws rather than ignoring the result: a swallowed SQLITE_CONSTRAINT
    // is how a foreign-key violation turns into a silently empty table.
    void run() {
        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw std::runtime_error(std::string("write failed: ") +
                                     sqlite3_errmsg(sqlite3_db_handle(st)));
        }
    }
};

std::string col_text(sqlite3_stmt *st, int i) {
    if (sqlite3_column_type(st, i) == SQLITE_NULL) return {};
    return reinterpret_cast<const char *>(sqlite3_column_text(st, i));
}

std::optional<int> col_opt_int(sqlite3_stmt *st, int i) {
    if (sqlite3_column_type(st, i) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int(st, i);
}

// Path stored relative to the root so the library can be moved or mounted
// elsewhere without invalidating every row.
std::string relative_to(const std::string &root, const std::string &path) {
    std::error_code ec;
    fs::path rel = fs::relative(fs::u8path(path), fs::u8path(root), ec);
    if (ec || rel.empty()) return fs::u8path(path).u8string();
    return rel.generic_u8string();
}

// "327841514.27.flac" -> 327841514. Our own naming, so the leading integer is
// always the track id.
std::optional<int64_t> track_id_from_path(const std::string &path) {
    std::string stem = fs::u8path(path).filename().u8string();
    size_t dot = stem.find('.');
    if (dot == std::string::npos || dot == 0) return std::nullopt;
    try {
        return std::stoll(stem.substr(0, dot));
    } catch (...) {
        return std::nullopt;
    }
}

const std::string *artist_name_of(const kb::Artist *a) {
    return (a && a->name) ? &*a->name : nullptr;
}

void upsert_artist(sqlite3 *db, const kb::Artist *artist, int64_t now) {
    if (!artist || !artist->id) return;
    const std::string *name = artist_name_of(artist);
    Stmt s(db,
        "INSERT INTO artists (id,name,image_url,albums_count,updated_at) "
        "VALUES (?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "name=excluded.name, image_url=COALESCE(excluded.image_url,artists.image_url), "
        "albums_count=COALESCE(excluded.albums_count,artists.albums_count), "
        "updated_at=excluded.updated_at");
    s.bind(static_cast<int64_t>(*artist->id));
    s.bind(name ? *name : std::string("Unknown Artist"));
    if (artist->image && artist->image->large) s.bind(*artist->image->large);
    else s.bind_null();
    s.bind(artist->albums_count);
    s.bind(now);
    s.run();
}

void upsert_label(sqlite3 *db, const std::optional<kb::Label> &label) {
    if (!label || !label->id || !label->name) return;
    Stmt s(db, "INSERT INTO labels (id,name,slug) VALUES (?,?,?) "
               "ON CONFLICT(id) DO UPDATE SET name=excluded.name, slug=excluded.slug");
    s.bind(static_cast<int64_t>(*label->id)).bind(*label->name);
    s.bind(label->slug);
    s.run();
}

void upsert_genre(sqlite3 *db, const std::optional<kb::Genre> &genre) {
    if (!genre || !genre->id || !genre->name) return;
    Stmt s(db, "INSERT INTO genres (id,name,slug) VALUES (?,?,?) "
               "ON CONFLICT(id) DO UPDATE SET name=excluded.name, slug=excluded.slug");
    s.bind(static_cast<int64_t>(*genre->id)).bind(*genre->name);
    s.bind(genre->slug);
    s.run();
}

void upsert_album(sqlite3 *db, const kb::Album &al, int64_t now) {
    if (!al.id) return;

    upsert_artist(db, al.artist.get(), now);
    upsert_label(db, al.label);
    upsert_genre(db, al.genre);

    Stmt s(db,
        "INSERT INTO albums (id,title,version,artist_id,label_id,genre_id,upc,copyright,"
        "release_date_original,release_date_stream,released_at,duration,tracks_count,"
        "media_count,product_type,release_type,parental_warning,hires,maximum_bit_depth,"
        "maximum_sampling_rate,maximum_channel_count,image_thumbnail,image_large,"
        "image_max,updated_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "title=excluded.title, version=excluded.version, artist_id=excluded.artist_id, "
        "label_id=excluded.label_id, genre_id=excluded.genre_id, upc=excluded.upc, "
        "copyright=excluded.copyright, "
        "release_date_original=excluded.release_date_original, "
        "release_date_stream=excluded.release_date_stream, "
        "released_at=excluded.released_at, duration=excluded.duration, "
        "tracks_count=excluded.tracks_count, media_count=excluded.media_count, "
        "product_type=excluded.product_type, release_type=excluded.release_type, "
        "parental_warning=excluded.parental_warning, hires=excluded.hires, "
        "maximum_bit_depth=excluded.maximum_bit_depth, "
        "maximum_sampling_rate=excluded.maximum_sampling_rate, "
        "maximum_channel_count=excluded.maximum_channel_count, "
        "image_thumbnail=excluded.image_thumbnail, image_large=excluded.image_large, "
        "image_max=excluded.image_max, updated_at=excluded.updated_at");

    s.bind(*al.id);
    s.bind(al.title.value_or(*al.id));   // title is NOT NULL; id is the last resort
    s.bind(al.version);
    if (al.artist && al.artist->id) s.bind(static_cast<int64_t>(*al.artist->id));
    else s.bind_null();
    if (al.label && al.label->id) s.bind(static_cast<int64_t>(*al.label->id));
    else s.bind_null();
    if (al.genre && al.genre->id) s.bind(static_cast<int64_t>(*al.genre->id));
    else s.bind_null();
    s.bind(al.upc);
    s.bind(al.copyright);
    s.bind(al.release_date_original);
    s.bind(al.release_date_stream);
    s.bind(al.released_at);
    s.bind(al.duration);
    s.bind(al.tracks_count);
    s.bind(al.media_count);
    s.bind(al.product_type);
    s.bind(al.release_type);
    s.bind(al.parental_warning);
    s.bind(al.hires);
    s.bind(al.maximum_bit_depth);
    s.bind(al.maximum_sampling_rate);
    s.bind(al.maximum_channel_count);
    s.bind(al.image ? al.image->thumbnail : std::optional<std::string>{});
    s.bind(al.image ? al.image->large : std::optional<std::string>{});
    s.bind(al.image ? (al.image->mega ? al.image->mega : al.image->extra_large)
                    : std::optional<std::string>{});
    s.bind(now);
    s.run();

    // Every credited artist, so featured-artist queries need no schema change.
    if (al.artists) {
        for (const auto &a : *al.artists) {
            if (!a || !a->id) continue;
            upsert_artist(db, a.get(), now);
            Stmt link(db, "INSERT OR IGNORE INTO album_artists (album_id,artist_id,role) "
                          "VALUES (?,?,?)");
            link.bind(*al.id).bind(static_cast<int64_t>(*a->id));
            bool is_main = al.artist && al.artist->id && *a->id == *al.artist->id;
            link.bind(std::string(is_main ? "main" : "featured"));
            link.run();
        }
    }
}

void upsert_track(sqlite3 *db, const kb::Track &t, const std::string &album_id,
                  int64_t now) {
    if (!t.id) return;

    upsert_artist(db, t.performer.get(), now);
    upsert_artist(db, t.composer.get(), now);

    Stmt s(db,
        "INSERT INTO tracks (id,album_id,title,version,isrc,track_number,media_number,"
        "duration,performer_id,composer_id,parental_warning,copyright,maximum_bit_depth,"
        "maximum_sampling_rate,maximum_channel_count,updated_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "album_id=COALESCE(excluded.album_id,tracks.album_id), title=excluded.title, "
        "version=excluded.version, isrc=excluded.isrc, "
        "track_number=excluded.track_number, media_number=excluded.media_number, "
        "duration=excluded.duration, performer_id=excluded.performer_id, "
        "composer_id=excluded.composer_id, "
        "parental_warning=excluded.parental_warning, copyright=excluded.copyright, "
        "maximum_bit_depth=excluded.maximum_bit_depth, "
        "maximum_sampling_rate=excluded.maximum_sampling_rate, "
        "maximum_channel_count=excluded.maximum_channel_count, "
        "updated_at=excluded.updated_at");

    s.bind(static_cast<int64_t>(*t.id));
    if (album_id.empty()) s.bind_null(); else s.bind(album_id);
    s.bind(t.title.value_or("Unknown"));
    s.bind(t.version);
    s.bind(t.isrc);
    s.bind(t.track_number);
    s.bind(t.media_number);
    s.bind(t.duration);
    if (t.performer && t.performer->id) s.bind(static_cast<int64_t>(*t.performer->id));
    else s.bind_null();
    if (t.composer && t.composer->id) s.bind(static_cast<int64_t>(*t.composer->id));
    else s.bind_null();
    s.bind(t.parental_warning);
    s.bind(t.copyright);
    s.bind(t.maximum_bit_depth);
    s.bind(t.maximum_sampling_rate);
    s.bind(t.maximum_channel_count);
    s.bind(now);
    s.run();
}

void upsert_file(sqlite3 *db, const std::string &root, int64_t track_id,
                 const std::string &path, const std::string &quality, int format_id,
                 const std::string &country, int64_t now) {
    std::error_code ec;
    auto size = fs::file_size(fs::u8path(path), ec);

    // file_time_type's epoch is unspecified and is not the Unix epoch — on
    // libstdc++ writing time_since_epoch() straight out lands in the 1800s.
    // C++20 has file_clock::to_sys; this is the C++17 way.
    auto write_time = fs::last_write_time(fs::u8path(path), ec);
    auto as_system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        write_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    int64_t mtime = std::chrono::duration_cast<std::chrono::seconds>(
                        as_system.time_since_epoch())
                        .count();

    Stmt s(db,
        "INSERT INTO files (track_id,rel_path,format_id,quality,country,bytes,mtime,"
        "downloaded_at) VALUES (?,?,?,?,?,?,?,?) "
        "ON CONFLICT(rel_path) DO UPDATE SET track_id=excluded.track_id, "
        "format_id=excluded.format_id, quality=excluded.quality, "
        "country=excluded.country, bytes=excluded.bytes, mtime=excluded.mtime, "
        "downloaded_at=excluded.downloaded_at");
    s.bind(track_id);
    s.bind(relative_to(root, path));
    s.bind(format_id);
    s.bind(quality);
    if (country.empty()) s.bind_null(); else s.bind(country);
    if (ec) s.bind_null(); else s.bind(static_cast<int64_t>(size));
    s.bind(mtime);
    s.bind(now);
    s.run();
}

// A catalog problem must never cost a finished download, so every public
// entry point funnels through here.
template <typename Fn>
void guarded(const char *what, Fn fn) {
    try {
        fn();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Warning: library catalog (%s): %s\n", what, e.what());
    } catch (...) {
        std::fprintf(stderr, "Warning: library catalog (%s) failed\n", what);
    }
}

struct Transaction {
    sqlite3 *db;
    explicit Transaction(sqlite3 *d) : db(d) {
        sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    }
    ~Transaction() { sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr); }
};

Entry row_to_entry(sqlite3_stmt *st) {
    Entry e;
    e.rel_path     = col_text(st, 0);
    e.quality      = col_text(st, 1);
    e.format_id    = sqlite3_column_int(st, 2);
    e.track_id     = sqlite3_column_int64(st, 3);
    e.track_number = col_opt_int(st, 4);
    e.media_number = col_opt_int(st, 5);
    e.track_title  = col_text(st, 6);
    e.duration     = col_opt_int(st, 7);
    e.album_id     = col_text(st, 8);
    e.album_title  = col_text(st, 9);
    e.artist_name  = col_text(st, 10);
    return e;
}

constexpr const char *kEntrySelect =
    "SELECT rel_path,quality,format_id,track_id,track_number,media_number,track_title,"
    "duration,album_id,album_title,artist_name FROM view_library ";

AlbumEntry row_to_album(sqlite3_stmt *st) {
    AlbumEntry a;
    a.id           = col_text(st, 0);
    a.title        = col_text(st, 1);
    a.version      = col_text(st, 2);
    a.artist_name  = col_text(st, 3);
    a.label_name   = col_text(st, 4);
    a.release_date = col_text(st, 5);
    a.tracks_count = col_opt_int(st, 6);
    a.files_on_disk = sqlite3_column_int(st, 7);
    return a;
}

constexpr const char *kAlbumSelect =
    "SELECT al.id, al.title, al.version, ar.name, lb.name, al.release_date_original, "
    "al.tracks_count, "
    "(SELECT COUNT(*) FROM files f JOIN tracks t ON t.id=f.track_id "
    " WHERE t.album_id=al.id) "
    "FROM albums al "
    "LEFT JOIN artists ar ON ar.id=al.artist_id "
    "LEFT JOIN labels  lb ON lb.id=al.label_id ";

} // namespace

void record_download(const std::string &root, const kb::Album &album,
                     const std::vector<std::string> &paths, const std::string &quality,
                     int format_id, const std::string &country) {
    if (!album.id) return;
    guarded("record album", [&] {
        Db db(db_path(root));
        Transaction tx(db.db);
        int64_t now = now_seconds();

        upsert_album(db.db, album, now);
        if (album.tracks && album.tracks->items) {
            for (const auto &t : *album.tracks->items) {
                if (t) upsert_track(db.db, *t, *album.id, now);
            }
        }

        for (const auto &path : paths) {
            auto tid = track_id_from_path(path);
            if (!tid) continue;
            upsert_file(db.db, root, *tid, path, quality, format_id, country, now);
        }
    });
}

void record_track_download(const std::string &root, const kb::Track &track,
                           const std::string &path, const std::string &quality,
                           int format_id, const std::string &country) {
    if (!track.id) return;
    guarded("record track", [&] {
        Db db(db_path(root));
        Transaction tx(db.db);
        int64_t now = now_seconds();

        std::string album_id;
        if (track.album && track.album->id) {
            album_id = *track.album->id;
            upsert_album(db.db, *track.album, now);
        }
        upsert_track(db.db, track, album_id, now);
        upsert_file(db.db, root, *track.id, path, quality, format_id, country, now);
    });
}

void record_asset(const std::string &root, const std::string &kind,
                  const std::string *album_id, const int *artist_id,
                  const std::string &path) {
    guarded("record asset", [&] {
        std::error_code ec;
        auto size = fs::file_size(fs::u8path(path), ec);

        Db db(db_path(root));
        Stmt s(db.db,
            "INSERT INTO assets (kind,album_id,artist_id,rel_path,bytes) "
            "VALUES (?,?,?,?,?) ON CONFLICT(rel_path) DO UPDATE SET "
            "kind=excluded.kind, bytes=excluded.bytes");
        s.bind(kind);
        if (album_id) s.bind(*album_id); else s.bind_null();
        if (artist_id) s.bind(static_cast<int64_t>(*artist_id)); else s.bind_null();
        s.bind(relative_to(root, path));
        if (ec) s.bind_null(); else s.bind(static_cast<int64_t>(size));
        s.run();
    });
}

std::optional<Resolution> resolve(const std::string &root, const std::string &key) {
    std::optional<Resolution> out;
    guarded("resolve", [&] {
        Db db(db_path(root));

        // A path resolves through its file row; anything else is an id. Try
        // the album id first — album ids are alphanumeric and track ids are
        // numeric, so the two never collide.
        std::string album_id;
        std::optional<int64_t> only_track;

        if (key.find('/') != std::string::npos || key.find('\\') != std::string::npos) {
            Stmt s(db.db, "SELECT track_id, album_id FROM view_library WHERE rel_path=?");
            s.bind(relative_to(root, key));
            if (s.step()) {
                only_track = sqlite3_column_int64(s.st, 0);
                album_id   = col_text(s.st, 1);
            }
        } else {
            Stmt s(db.db, "SELECT id FROM albums WHERE id=?");
            s.bind(key);
            if (s.step()) {
                album_id = col_text(s.st, 0);
            } else {
                Stmt t(db.db, "SELECT id, album_id FROM tracks WHERE id=?");
                t.bind(key);
                if (t.step()) {
                    only_track = sqlite3_column_int64(t.st, 0);
                    album_id   = col_text(t.st, 1);
                }
            }
        }
        if (album_id.empty() && !only_track) return;

        Resolution r;
        if (!album_id.empty()) {
            Stmt s(db.db, (std::string(kAlbumSelect) + "WHERE al.id=?").c_str());
            s.bind(album_id);
            if (s.step()) r.album = row_to_album(s.st);
        }

        std::string sql = std::string(kEntrySelect);
        sql += only_track ? "WHERE track_id=?" : "WHERE album_id=?";
        sql += " ORDER BY media_number, track_number";
        Stmt s(db.db, sql.c_str());
        if (only_track) s.bind(*only_track); else s.bind(album_id);
        while (s.step()) r.tracks.push_back(row_to_entry(s.st));

        out = std::move(r);
    });
    return out;
}

std::vector<AlbumEntry> list_albums(const std::string &root, uint32_t limit) {
    std::vector<AlbumEntry> out;
    guarded("list albums", [&] {
        Db db(db_path(root));
        Stmt s(db.db, (std::string(kAlbumSelect) +
                       "ORDER BY al.updated_at DESC LIMIT ?").c_str());
        s.bind(static_cast<int>(limit));
        while (s.step()) out.push_back(row_to_album(s.st));
    });
    return out;
}

std::vector<Entry> list_tracks(const std::string &root, uint32_t limit) {
    std::vector<Entry> out;
    guarded("list tracks", [&] {
        Db db(db_path(root));
        Stmt s(db.db, (std::string(kEntrySelect) +
                       "ORDER BY album_id, media_number, track_number LIMIT ?").c_str());
        s.bind(static_cast<int>(limit));
        while (s.step()) out.push_back(row_to_entry(s.st));
    });
    return out;
}

} // namespace library
