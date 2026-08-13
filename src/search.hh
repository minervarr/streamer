#pragma once
#include <optional>
#include <string>

#include <core/errors.hh>

namespace kb { class QobuzApiService; }

namespace search {

// Parse "10m", "1h30m", "1:30:00", "90" → seconds. Returns error string on failure.
std::optional<std::string> parse_duration(const std::string &s, int &out_secs);

std::string fmt_duration(int secs);

// Prints the results and returns how many rows it printed.
//
// Returns an error instead of printing one, so the caller can tell *why* a
// search came back empty and act on it — availability is regional, and a
// query that finds nothing under one account routinely finds plenty under
// another. A zero-result search is reported as ResourceNotFound for that
// reason (see account::classify), not as a successful search of nothing.
// Empty categories print no header at all, so a multi-account run does not
// stack up "Albums (0 results)" banners between the regions that did answer.
kb::Result<int> run(kb::QobuzApiService &svc,
                    const std::string &query,
                    const std::string &kind,
                    bool tsv,
                    int limit,
                    std::optional<int> min_secs,
                    std::optional<int> max_secs);

} // namespace search
