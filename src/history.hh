#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace history {

struct Record {
    int64_t     id          = 0;
    int64_t     timestamp   = 0;
    std::string qobuz_id;
    std::string item_type;
    std::optional<std::string> title;
    std::optional<std::string> artist_name;
    std::optional<int64_t>     artist_id;
    std::optional<int32_t>     track_count;
    std::string quality;
    int32_t     format_id   = 0;
    std::optional<std::string> output_dir;
    std::optional<std::string> country;
    bool        success     = true;
};

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
    bool               success);

std::vector<Record> list_recent(uint32_t limit);
// TSV: timestamp\tqobuz_id\titem_type\ttitle\tartist\tquality\tcountry\tstatus
void                print_tsv(uint32_t limit);
std::string         export_json();
uint32_t            import_json(const std::string &json);
uint32_t            clear();

std::string format_record_line(const Record &rec);

} // namespace history
