#pragma once
#include <string>

namespace kb { class QobuzApiService; }

namespace dl {

// quality: "mp3" | "flac" | "flac-hi" | "flac-ultra"
int quality_to_format_id(const std::string &quality);
std::string format_id_to_quality(int fmt);

// Returns true on full or partial success (at least one file downloaded).
// country: when non-empty, a subdirectory with that name is created inside
// output_dir (e.g. "US" → <output_dir>/US/<artist>/…).
bool run(kb::QobuzApiService &svc,
         const std::string &target_url,
         const std::string &quality,
         const std::string &output_dir,
         const std::string &country,
         int concurrency,
         bool save_extras,
         bool embed_metadata);

} // namespace dl
