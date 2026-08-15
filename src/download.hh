#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace kb { class QobuzApiService; }

namespace dl {

// Live download progress for a front end that has no terminal to read.
//
// The numbers here are the same ones the CLI meter prints — percentage, rate,
// ETA — routed to a caller instead of to stdout. That mattered enough to add:
// a GUI launched from a desktop icon has no console attached, and an Android
// app has nowhere for one to go, so the only honest answer to "how far along
// is this?" used to be "watch the folder grow".
struct Progress {
    std::string label;            // already human-readable; safe to draw as-is
    int      done_tracks  = 0;    // finished tracks of `total_tracks`
    int      total_tracks = 0;
    uint64_t bytes        = 0;
    uint64_t total_bytes  = 0;    // 0 when unknown; estimated while an album runs
    double   speed_bps    = 0.0;
    double   eta_seconds  = -1.0; // negative until the estimate is worth showing
    bool     complete     = false;
    // Non-empty when the transfer failed. Carried here rather than left on
    // stderr because that is exactly the case a GUI must be able to explain:
    // a dead token answers 401 in a second, and without this the front end
    // shows a progress bar sitting at 0% forever with nothing to say about
    // why. On Android stderr is not even collected.
    std::string error;
};

// Process-wide sink, installed once by the GUI at startup. While one is
// installed the terminal meter stays silent — both render the same numbers,
// and the carriage-return redraw is meaningless without a console. Pass
// nullptr to restore the CLI behaviour.
using ProgressSink = std::function<void(const Progress&)>;
void set_progress_sink(ProgressSink sink);

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
