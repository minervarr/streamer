#pragma once
#include <string>

namespace kb { class QobuzApiService; }

namespace inspect {

void run_album(kb::QobuzApiService &svc, const std::string &album_id);
void run_track(kb::QobuzApiService &svc, int track_id);

// TSV format for GUI: album header + per-track rows
void run_album_tsv(kb::QobuzApiService &svc, const std::string &album_id);

} // namespace inspect
