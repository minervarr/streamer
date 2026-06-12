#pragma once
#include <filesystem>
#include <string>

// Forward-declare to avoid pulling in all kobuzapi headers here.
namespace kb { class QobuzApiService; }

namespace extras {

// Download album description + PDF booklet into album_dir if they don't exist.
void save_album_extras(const kb::QobuzApiService &svc,
                       const std::string &album_id,
                       const std::filesystem::path &album_dir);

// Download artist bio text + artist image into artist_dir if they don't exist.
void save_artist_extras(const kb::QobuzApiService &svc,
                        int artist_id,
                        const std::filesystem::path &artist_dir);

} // namespace extras
