#include "history.hh"
#include "config.hh"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include <sqlite3.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace history {

static fs::path db_path() {
    return config::config_path().parent_path() / "history.db";
}

struct Db {
    sqlite3 *db = nullptr;
    explicit Db(const fs::path &path) {
        fs::create_directories(path.parent_path());
        if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
            std::string msg = "Cannot open history db: ";
            msg += sqlite3_errmsg(db);
            sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error(msg);
        }
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS downloads ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp INTEGER NOT NULL,"
            "  qobuz_id TEXT NOT NULL,"
            "  item_type TEXT NOT NULL,"
            "  title TEXT,"
            "  artist_name TEXT,"
            "  artist_id INTEGER,"
            "  track_count INTEGER,"
            "  quality TEXT NOT NULL,"
            "  format_id INTEGER NOT NULL,"
            "  output_dir TEXT,"
            "  country TEXT,"
            "  success INTEGER NOT NULL DEFAULT 1"
            ");",
            nullptr, nullptr, nullptr);
    }
    ~Db() { if (db) sqlite3_close(db); }
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;
};

static Record row_to_record(sqlite3_stmt *st) {
    Record r;
    r.id         = sqlite3_column_int64(st, 0);
    r.timestamp  = sqlite3_column_int64(st, 1);
    r.qobuz_id   = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    r.item_type  = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
    if (sqlite3_column_type(st, 4) != SQLITE_NULL)
        r.title = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
    if (sqlite3_column_type(st, 5) != SQLITE_NULL)
        r.artist_name = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
    if (sqlite3_column_type(st, 6) != SQLITE_NULL)
        r.artist_id = sqlite3_column_int64(st, 6);
    if (sqlite3_column_type(st, 7) != SQLITE_NULL)
        r.track_count = sqlite3_column_int(st, 7);
    r.quality    = reinterpret_cast<const char*>(sqlite3_column_text(st, 8));
    r.format_id  = sqlite3_column_int(st, 9);
    if (sqlite3_column_type(st, 10) != SQLITE_NULL)
        r.output_dir = reinterpret_cast<const char*>(sqlite3_column_text(st, 10));
    if (sqlite3_column_type(st, 11) != SQLITE_NULL)
        r.country = reinterpret_cast<const char*>(sqlite3_column_text(st, 11));
    r.success    = sqlite3_column_int(st, 12) != 0;
    return r;
}

void record(
    const std::string &qobuz_id,
    const std::string &item_type,
    const std::string *title,
    const std::string *artist_name,
    const int64_t     *artist_id,
    const int32_t     *track_count,
    const std::string &quality,
    int32_t            format_id,
    const std::string *output_dir,
    const std::string *country,
    bool               success)
{
    try {
        Db db(db_path());
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        sqlite3_stmt *st = nullptr;
        sqlite3_prepare_v2(db.db,
            "INSERT INTO downloads (timestamp,qobuz_id,item_type,title,artist_name,"
            "artist_id,track_count,quality,format_id,output_dir,country,success) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            -1, &st, nullptr);

        auto bind_text = [&](int i, const std::string *s) {
            if (s) sqlite3_bind_text(st, i, s->c_str(), -1, SQLITE_TRANSIENT);
            else   sqlite3_bind_null(st, i);
        };
        sqlite3_bind_int64(st, 1, now);
        sqlite3_bind_text(st, 2, qobuz_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, item_type.c_str(), -1, SQLITE_TRANSIENT);
        bind_text(4, title);
        bind_text(5, artist_name);
        if (artist_id)  sqlite3_bind_int64(st, 6, *artist_id);
        else            sqlite3_bind_null(st, 6);
        if (track_count) sqlite3_bind_int(st, 7, *track_count);
        else             sqlite3_bind_null(st, 7);
        sqlite3_bind_text(st, 8, quality.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 9, format_id);
        bind_text(10, output_dir);
        bind_text(11, country);
        sqlite3_bind_int(st, 12, success ? 1 : 0);
        sqlite3_step(st);
        sqlite3_finalize(st);
    } catch (...) {}
}

std::vector<Record> list_recent(uint32_t limit) {
    Db db(db_path());
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(db.db,
        "SELECT id,timestamp,qobuz_id,item_type,title,artist_name,artist_id,"
        "track_count,quality,format_id,output_dir,country,success "
        "FROM downloads ORDER BY timestamp DESC LIMIT ?",
        -1, &st, nullptr);
    sqlite3_bind_int(st, 1, static_cast<int>(limit));

    std::vector<Record> out;
    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(row_to_record(st));
    sqlite3_finalize(st);
    return out;
}

std::string export_json() {
    Db db(db_path());
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(db.db,
        "SELECT id,timestamp,qobuz_id,item_type,title,artist_name,artist_id,"
        "track_count,quality,format_id,output_dir,country,success "
        "FROM downloads ORDER BY timestamp ASC",
        -1, &st, nullptr);

    nlohmann::json records = nlohmann::json::array();
    while (sqlite3_step(st) == SQLITE_ROW) {
        Record r = row_to_record(st);
        nlohmann::json obj;
        obj["timestamp"]   = r.timestamp;
        obj["qobuz_id"]    = r.qobuz_id;
        obj["type"]        = r.item_type;
        obj["title"]       = r.title.value_or("");
        obj["artist_name"] = r.artist_name.value_or("");
        if (r.artist_id)   obj["artist_id"]   = *r.artist_id;
        if (r.track_count) obj["track_count"] = *r.track_count;
        obj["quality"]    = r.quality;
        obj["format_id"]  = r.format_id;
        obj["output_dir"] = r.output_dir.value_or("");
        obj["country"]    = r.country.value_or("");
        obj["success"]    = r.success;
        records.push_back(std::move(obj));
    }
    sqlite3_finalize(st);

    // Build export time string
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

    nlohmann::json root;
    root["version"]     = 1;
    root["exported_at"] = std::string(buf);
    root["records"]     = std::move(records);
    return root.dump(2);
}

uint32_t import_json(const std::string &json) {
    auto root = nlohmann::json::parse(json);
    auto &records = root["records"];

    Db db(db_path());
    uint32_t imported = 0;

    for (auto &obj : records) {
        int64_t ts       = obj.value("timestamp", int64_t{0});
        std::string qid  = obj.value("qobuz_id", std::string{});

        // skip duplicates
        sqlite3_stmt *ck = nullptr;
        sqlite3_prepare_v2(db.db,
            "SELECT COUNT(*) FROM downloads WHERE qobuz_id=? AND timestamp=?",
            -1, &ck, nullptr);
        sqlite3_bind_text(ck, 1, qid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ck, 2, ts);
        bool exists = (sqlite3_step(ck) == SQLITE_ROW && sqlite3_column_int(ck, 0) > 0);
        sqlite3_finalize(ck);
        if (exists) continue;

        std::string title       = obj.value("title", std::string{});
        std::string artist_name = obj.value("artist_name", std::string{});
        std::string quality     = obj.value("quality", std::string{});
        std::string output_dir  = obj.value("output_dir", std::string{});
        std::string country     = obj.value("country", std::string{});
        std::string item_type   = obj.value("type", std::string{});
        int32_t format_id       = obj.value("format_id", int32_t{6});
        bool success            = obj.value("success", true);

        const std::string *ptitle   = title.empty()      ? nullptr : &title;
        const std::string *partist  = artist_name.empty()? nullptr : &artist_name;
        const std::string *pout     = output_dir.empty() ? nullptr : &output_dir;
        const std::string *pcountry = country.empty()    ? nullptr : &country;

        sqlite3_stmt *ins = nullptr;
        sqlite3_prepare_v2(db.db,
            "INSERT INTO downloads (timestamp,qobuz_id,item_type,title,artist_name,"
            "artist_id,track_count,quality,format_id,output_dir,country,success) "
            "VALUES (?,?,?,?,?,NULL,NULL,?,?,?,?,?)",
            -1, &ins, nullptr);
        sqlite3_bind_int64(ins, 1, ts);
        sqlite3_bind_text(ins, 2, qid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, item_type.c_str(), -1, SQLITE_TRANSIENT);
        if (ptitle)  sqlite3_bind_text(ins, 4, ptitle->c_str(), -1, SQLITE_TRANSIENT);
        else         sqlite3_bind_null(ins, 4);
        if (partist) sqlite3_bind_text(ins, 5, partist->c_str(), -1, SQLITE_TRANSIENT);
        else         sqlite3_bind_null(ins, 5);
        sqlite3_bind_text(ins, 6, quality.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 7, format_id);
        if (pout)     sqlite3_bind_text(ins, 8, pout->c_str(), -1, SQLITE_TRANSIENT);
        else          sqlite3_bind_null(ins, 8);
        if (pcountry) sqlite3_bind_text(ins, 9, pcountry->c_str(), -1, SQLITE_TRANSIENT);
        else          sqlite3_bind_null(ins, 9);
        sqlite3_bind_int(ins, 10, success ? 1 : 0);
        sqlite3_step(ins);
        sqlite3_finalize(ins);
        ++imported;
    }
    return imported;
}

uint32_t clear() {
    Db db(db_path());
    int64_t count = 0;
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(db.db, "SELECT COUNT(*) FROM downloads", -1, &st, nullptr);
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    sqlite3_exec(db.db, "DELETE FROM downloads", nullptr, nullptr, nullptr);
    return static_cast<uint32_t>(count);
}

void print_tsv(uint32_t limit) {
    auto recs = list_recent(limit);
    for (auto &r : recs) {
        std::printf("%lld\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
            (long long)r.timestamp,
            r.qobuz_id.c_str(),
            r.item_type.c_str(),
            r.title.value_or("").c_str(),
            r.artist_name.value_or("").c_str(),
            r.quality.c_str(),
            r.country.value_or("").c_str(),
            r.success ? "OK" : "FAIL");
    }
}

std::string format_record_line(const Record &rec) {
    std::time_t ts = static_cast<std::time_t>(rec.timestamp);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&ts));

    const char *status = rec.success ? "OK  " : "FAIL";
    std::string artist = rec.artist_name.value_or("?");
    std::string title  = rec.title.value_or("?");

    std::ostringstream oss;
    oss << "[" << buf << "] " << status << "  "
        << artist << " \xe2\x80\x94 " << title
        << "  (" << rec.item_type << ", " << rec.quality << ")";
    return oss.str();
}

} // namespace history
