#pragma once
#include <optional>
#include <string>
#include <variant>

namespace url {

struct Track    { int id; };
struct Album    { std::string id; };
struct Artist   { int id; };
struct Playlist { std::string id; };

using DownloadTarget = std::variant<Track, Album, Artist, Playlist>;

std::optional<DownloadTarget> parse(const std::string &input);

} // namespace url
