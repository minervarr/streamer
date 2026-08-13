#pragma once
#include <string>

#include <core/errors.hh>

namespace kb { class QobuzApiService; }

namespace inspect {

// These return the fetch error rather than printing it, because the caller
// needs to distinguish "this release isn't in *this* account's region" from
// a real failure: availability is regional, so a ResourceNotFound here is a
// reason to ask the next country's account, not a reason to give up. See
// account::classify and the CLI's failover loop.
kb::Result<void> run_album(kb::QobuzApiService &svc, const std::string &album_id);
kb::Result<void> run_track(kb::QobuzApiService &svc, int track_id);

// TSV format for GUI: album header + per-track rows
kb::Result<void> run_album_tsv(kb::QobuzApiService &svc, const std::string &album_id);

} // namespace inspect
