#pragma once
#include <optional>
#include <string>

namespace kb { class QobuzApiService; }

namespace search {

// Parse "10m", "1h30m", "1:30:00", "90" → seconds. Returns error string on failure.
std::optional<std::string> parse_duration(const std::string &s, int &out_secs);

std::string fmt_duration(int secs);

void run(kb::QobuzApiService &svc,
         const std::string &query,
         const std::string &kind,
         bool tsv,
         int limit,
         std::optional<int> min_secs,
         std::optional<int> max_secs);

} // namespace search
