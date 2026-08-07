#include "library.hh"

#include "download.hh"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
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

// "327841514.27.flac" -> 27. 0 when the name is not ours.
int format_id_from_path(const std::string &path) {
    std::string stem = fs::u8path(path).filename().u8string();
    size_t first = stem.find('.');
    if (first == std::string::npos) return 0;
    size_t second = stem.find('.', first + 1);
    if (second == std::string::npos) return 0;
    try {
        return std::stoi(stem.substr(first + 1, second - first - 1));
    } catch (...) {
        return 0;
    }
}

// The sidecar files extras::save_* produce, and the asset kind each maps to.
const char *asset_kind_for(const std::string &filename) {
    if (filename == "cover.jpg")             return "cover";
    if (filename == "booklet.pdf")           return "booklet";
    if (filename == "album_description.txt") return "description";
    if (filename == "artist.jpg")            return "artist_image";
    if (filename == "artist_bio.txt")        return "artist_bio";
    return nullptr;
}

// "FR/<album_id>/<file>" -> "FR". Empty when there is no country tier.
std::string country_from_rel(const std::string &rel) {
    size_t first = rel.find('/');
    if (first == std::string::npos) return {};
    if (rel.find('/', first + 1) == std::string::npos) return {};
    return rel.substr(0, first);
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
    a.country       = col_text(st, 8);
    a.cover_path    = col_text(st, 9);
    a.bytes_on_disk = sqlite3_column_int64(st, 10);
    return a;
}

// The three correlated subqueries at the end are what the GUI's cover grid
// needs: the country tier (any file's will do — an album lives under exactly
// one), the cover asset's path, and the album's size on disk.
constexpr const char *kAlbumSelect =
    "SELECT al.id, al.title, al.version, ar.name, lb.name, al.release_date_original, "
    "al.tracks_count, "
    "(SELECT COUNT(*) FROM files f JOIN tracks t ON t.id=f.track_id "
    " WHERE t.album_id=al.id), "
    "(SELECT f.country FROM files f JOIN tracks t ON t.id=f.track_id "
    " WHERE t.album_id=al.id AND f.country IS NOT NULL LIMIT 1), "
    "(SELECT a.rel_path FROM assets a "
    " WHERE a.album_id=al.id AND a.kind='cover' LIMIT 1), "
    "(SELECT IFNULL(SUM(f.bytes),0) FROM files f JOIN tracks t ON t.id=f.track_id "
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

DeleteReport delete_album(const std::string &root, const std::string &album_id, bool dry_run) {
    DeleteReport report;
    if (album_id.empty() || root.empty()) return report;

    // Not guarded(): the caller asked for something to be gone and needs to
    // be told when it isn't. A thrown catalog error becomes a failed entry
    // rather than a warning on stderr nobody reads.
    try {
        Db db(db_path(root));

        struct Doomed {
            std::string rel;
            int64_t bytes = 0;
            bool asset = false;
        };
        std::vector<Doomed> doomed;
        {
            Stmt s(db.db, "SELECT f.rel_path, IFNULL(f.bytes,0) FROM files f "
                          "JOIN tracks t ON t.id=f.track_id WHERE t.album_id=?");
            s.bind(album_id);
            while (s.step())
                doomed.push_back({col_text(s.st, 0), sqlite3_column_int64(s.st, 1), false});
        }
        {
            Stmt s(db.db, "SELECT rel_path, IFNULL(bytes,0) FROM assets WHERE album_id=?");
            s.bind(album_id);
            while (s.step())
                doomed.push_back({col_text(s.st, 0), sqlite3_column_int64(s.st, 1), true});
        }

        // An album the catalog has never heard of is not an error to report,
        // it is simply nothing to do — but don't confuse it with an album row
        // that legitimately has no files left.
        if (doomed.empty()) {
            Stmt s(db.db, "SELECT 1 FROM albums WHERE id=?");
            s.bind(album_id);
            if (!s.step()) return report;
        }

        const fs::path rootp = fs::u8path(root);
        std::set<fs::path> touched_dirs;
        std::vector<std::string> unlinked;   // rel_paths whose file is now gone

        for (const auto &d : doomed) {
            const fs::path abs = rootp / fs::u8path(d.rel);
            touched_dirs.insert(abs.parent_path());

            std::error_code ec;
            const bool present = fs::exists(abs, ec);
            if (dry_run) {
                if (!present) continue;
                (d.asset ? report.assets_removed : report.files_removed)++;
                if (!d.asset) report.bytes_freed += d.bytes;
                continue;
            }
            if (present && !fs::remove(abs, ec)) {
                report.failed.push_back(d.rel);
                continue;
            }
            unlinked.push_back(d.rel);
            // A file the catalog knew about but disk had already lost costs
            // nothing to "free" — the row still has to go, so it counts as
            // removed but contributes no bytes.
            if (present) {
                (d.asset ? report.assets_removed : report.files_removed)++;
                if (!d.asset) report.bytes_freed += d.bytes;
            }
        }

        // Only ever rmdir, never remove_all: a directory that still holds
        // something the catalog didn't list stays, along with whatever it is.
        // Walks up so an emptied country tier goes too, stopping at the root.
        if (!dry_run) {
            for (fs::path dir : touched_dirs) {
                std::error_code ec;
                while (dir != rootp && dir.has_relative_path() &&
                       dir.filename() != ".streamer" &&
                       fs::is_directory(dir, ec) && fs::is_empty(dir, ec) && !ec) {
                    if (!fs::remove(dir, ec)) break;
                    dir = dir.parent_path();
                }
            }
        }

        if (dry_run) {
            report.rows_removed = report.failed.empty();
            return report;
        }

        Transaction tx(db.db);
        if (report.failed.empty()) {
            // foreign_keys=ON, and tracks/files/assets/album_artists all
            // cascade from albums, so one delete takes the whole album.
            Stmt s(db.db, "DELETE FROM albums WHERE id=?");
            s.bind(album_id);
            s.run();
            report.rows_removed = true;
        } else {
            // Partial delete: drop only the rows whose file actually went, so
            // the catalog keeps matching the disk instead of claiming files
            // that are still there.
            for (const auto &rel : unlinked) {
                Stmt f(db.db, "DELETE FROM files WHERE rel_path=?");
                f.bind(rel);
                f.run();
                Stmt a(db.db, "DELETE FROM assets WHERE rel_path=?");
                a.bind(rel);
                a.run();
            }
        }
    } catch (const std::exception &e) {
        report.failed.push_back(std::string("catalog: ") + e.what());
    } catch (...) {
        report.failed.push_back("catalog: unknown error");
    }
    return report;
}

bool catalog_looks_lost(const std::string &root) {
    bool has_albums_on_disk = false;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(fs::u8path(root), ec)) {
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().u8string();
        if (name == ".streamer") continue;
        // Either a country tier or an album directory; both mean content.
        has_albums_on_disk = true;
        break;
    }
    if (!has_albums_on_disk) return false;

    bool catalog_empty = true;
    guarded("probe catalog", [&] {
        Db db(db_path(root));
        Stmt s(db.db, "SELECT COUNT(*) FROM files");
        if (s.step()) catalog_empty = sqlite3_column_int(s.st, 0) == 0;
    });
    return catalog_empty;
}

ScanReport scan(const std::string &root, const AlbumFetcher &fetch_album, bool dry_run) {
    ScanReport report;
    guarded("scan", [&] {
        Db db(db_path(root));

        // Everything the catalog currently claims, so anything left over at
        // the end is a row whose file has gone.
        std::map<std::string, int64_t> claimed;   // rel_path -> track_id
        {
            Stmt s(db.db, "SELECT rel_path, track_id, bytes FROM files");
            while (s.step()) claimed.emplace(col_text(s.st, 0), sqlite3_column_int64(s.st, 1));
        }
        std::map<std::string, int64_t> recorded_bytes;
        {
            Stmt s(db.db, "SELECT rel_path, IFNULL(bytes,-1) FROM files");
            while (s.step()) recorded_bytes.emplace(col_text(s.st, 0), sqlite3_column_int64(s.st, 1));
        }

        std::set<int64_t> known_tracks;
        {
            Stmt s(db.db, "SELECT id FROM tracks");
            while (s.step()) known_tracks.insert(sqlite3_column_int64(s.st, 0));
        }

        std::map<std::string, std::optional<kb::Album>> fetched;   // album_id -> metadata
        struct PendingAsset {
            std::string abs_path;
            std::string album_id;   // empty for artist assets
            int artist_id;          // 0 for album assets
        };
        std::vector<PendingAsset> pending_assets;

        int64_t now = now_seconds();
        Transaction tx(db.db);

        std::error_code ec;
        fs::recursive_directory_iterator it(fs::u8path(root),
                                            fs::directory_options::skip_permission_denied, ec);
        if (ec) return;
        for (fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec)) {
                if (it->path().filename().u8string() == ".streamer") it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;

            std::string abs = it->path().u8string();
            std::string rel = relative_to(root, abs);
            ++report.files_seen;

            auto tid = track_id_from_path(abs);
            std::string album_id = it->path().parent_path().filename().u8string();
            if (!tid) {
                // Assets carry a foreign key onto albums, so they cannot be
                // written until every album row exists — deferred to a second
                // pass rather than relying on directory iteration order.
                if (asset_kind_for(it->path().filename().u8string())) {
                    pending_assets.push_back({abs, album_id, 0});
                } else {
                    ++report.unknown;
                }
                continue;
            }

            auto existing = claimed.find(rel);
            if (existing != claimed.end()) {
                int64_t on_disk = static_cast<int64_t>(fs::file_size(it->path(), ec));
                auto was = recorded_bytes.find(rel);
                if (!ec && was != recorded_bytes.end() && was->second != on_disk) {
                    if (!dry_run) {
                        upsert_file(db.db, root, *tid, abs,
                                    dl::format_id_to_quality(format_id_from_path(abs)),
                                    format_id_from_path(abs), country_from_rel(rel), now);
                    }
                    ++report.updated;
                }
                claimed.erase(existing);
                continue;
            }

            // Not in the catalog. Adopt it, re-fetching the album's metadata
            // when the catalog has never seen this track — which is exactly
            // the case after .streamer/ was deleted.
            if (known_tracks.find(*tid) == known_tracks.end()) {
                if (!fetch_album) {
                    ++report.unknown;
                    continue;
                }
                auto cached = fetched.find(album_id);
                if (cached == fetched.end()) {
                    cached = fetched.emplace(album_id, fetch_album(album_id)).first;
                    if (cached->second) ++report.fetched;
                }
                if (!cached->second) {
                    ++report.unknown;
                    continue;
                }
                if (!dry_run) {
                    upsert_album(db.db, *cached->second, now);
                    if (cached->second->tracks && cached->second->tracks->items) {
                        for (const auto &t : *cached->second->tracks->items) {
                            if (t && t->id) {
                                upsert_track(db.db, *t, album_id, now);
                                known_tracks.insert(*t->id);
                            }
                        }
                    }
                }
            }
            if (!dry_run) {
                upsert_file(db.db, root, *tid, abs,
                            dl::format_id_to_quality(format_id_from_path(abs)),
                            format_id_from_path(abs), country_from_rel(rel), now);
            }
            ++report.adopted;
        }

        // Artist assets live under .streamer/artists/<artist_id>/, which the
        // walk above deliberately skips, so collect them separately.
        {
            fs::path artists_dir = fs::u8path(root) / ".streamer" / "artists";
            std::error_code aec;
            for (const auto &dir : fs::directory_iterator(artists_dir, aec)) {
                if (aec) break;
                if (!dir.is_directory(aec)) continue;
                int artist_id = 0;
                try {
                    artist_id = std::stoi(dir.path().filename().u8string());
                } catch (...) {
                    continue;
                }
                for (const auto &f : fs::directory_iterator(dir.path(), aec)) {
                    if (aec) break;
                    if (!f.is_regular_file(aec)) continue;
                    if (asset_kind_for(f.path().filename().u8string())) {
                        pending_assets.push_back({f.path().u8string(), {}, artist_id});
                    }
                }
            }
        }

        // Second pass: every album row now exists, so the FK will hold.
        for (const auto &asset : pending_assets) {
            const char *kind = asset_kind_for(fs::u8path(asset.abs_path).filename().u8string());
            if (!kind) continue;
            // Both columns are foreign keys; inserting against a row that is
            // not there throws and would abort the whole scan.
            bool owner_known = false;
            if (!asset.album_id.empty()) {
                Stmt s(db.db, "SELECT 1 FROM albums WHERE id=?");
                s.bind(asset.album_id);
                owner_known = s.step();
            } else if (asset.artist_id != 0) {
                Stmt s(db.db, "SELECT 1 FROM artists WHERE id=?");
                s.bind(static_cast<int64_t>(asset.artist_id));
                owner_known = s.step();
            }
            if (!owner_known) {
                ++report.unknown;   // orphan sidecar we cannot attach to anything
                continue;
            }
            if (!dry_run) {
                std::error_code sec;
                auto size = fs::file_size(fs::u8path(asset.abs_path), sec);
                Stmt s(db.db,
                    "INSERT INTO assets (kind,album_id,artist_id,rel_path,bytes) "
                    "VALUES (?,?,?,?,?) ON CONFLICT(rel_path) DO UPDATE SET "
                    "kind=excluded.kind, bytes=excluded.bytes");
                s.bind(std::string(kind));
                if (asset.album_id.empty()) s.bind_null(); else s.bind(asset.album_id);
                if (asset.artist_id == 0) s.bind_null();
                else s.bind(static_cast<int64_t>(asset.artist_id));
                s.bind(relative_to(root, asset.abs_path));
                if (sec) s.bind_null(); else s.bind(static_cast<int64_t>(size));
                s.run();
            }
            ++report.assets;
        }

        // Whatever the catalog still claims was never found on disk.
        for (const auto &[rel, track_id] : claimed) {
            if (!dry_run) {
                Stmt s(db.db, "DELETE FROM files WHERE rel_path=?");
                s.bind(rel);
                s.run();
            }
            ++report.removed;
        }
        // Cover art, booklets and artist images go stale the same way.
        // Collected first, then deleted — do not mutate a table mid-SELECT.
        std::vector<std::string> stale_assets;
        {
            Stmt s(db.db, "SELECT rel_path FROM assets");
            while (s.step()) {
                std::string rel = col_text(s.st, 0);
                if (!fs::exists(fs::u8path(root) / fs::u8path(rel))) stale_assets.push_back(rel);
            }
        }
        for (const auto &rel : stale_assets) {
            if (!dry_run) {
                Stmt s(db.db, "DELETE FROM assets WHERE rel_path=?");
                s.bind(rel);
                s.run();
            }
            ++report.removed;
        }
    });
    return report;
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

namespace {
std::string in_placeholders(size_t n) {
    std::string out;
    for (size_t i = 0; i < n; i++) out += (i ? ",?" : "?");
    return out;
}
} // namespace

std::set<std::string> downloaded_track_ids(const std::string &root,
                                           const std::vector<std::string> &track_ids,
                                           int format_id) {
    std::set<std::string> out;
    if (track_ids.empty()) return out;
    guarded("downloaded track ids", [&] {
        Db db(db_path(root));
        std::string sql = "SELECT DISTINCT track_id FROM files WHERE format_id=? AND track_id IN (" +
                          in_placeholders(track_ids.size()) + ")";
        Stmt s(db.db, sql.c_str());
        s.bind(format_id);
        for (auto &id : track_ids) s.bind(id);
        while (s.step()) out.insert(std::to_string(sqlite3_column_int64(s.st, 0)));
    });
    return out;
}

std::set<std::string> downloaded_album_ids(const std::string &root,
                                           const std::vector<std::string> &album_ids,
                                           int format_id) {
    std::set<std::string> out;
    if (album_ids.empty()) return out;
    guarded("downloaded album ids", [&] {
        Db db(db_path(root));
        std::string sql =
            "SELECT t.album_id FROM files f JOIN tracks t ON t.id = f.track_id "
            "WHERE f.format_id=? AND t.album_id IN (" + in_placeholders(album_ids.size()) + ") "
            "GROUP BY t.album_id "
            "HAVING COUNT(DISTINCT f.track_id) = "
            "(SELECT tracks_count FROM albums WHERE id = t.album_id)";
        Stmt s(db.db, sql.c_str());
        s.bind(format_id);
        for (auto &id : album_ids) s.bind(id);
        while (s.step()) out.insert(col_text(s.st, 0));
    });
    return out;
}

} // namespace library
