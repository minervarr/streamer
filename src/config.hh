#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace config {

struct Account {
    std::string country;
    std::string email;
    std::string app_id;
    std::string app_secret;
    std::string user_id;
    std::string auth_token;
};

struct Settings {
    std::filesystem::path download_dir;
    std::string  quality             = "flac";
    uint32_t     requests_per_minute = 0;
    uint32_t     concurrency         = 8;
    std::string  language;
};

struct Config {
    std::vector<Account> accounts;
    Settings settings;
};

std::filesystem::path config_path();
Config load();
void   save(const Config &cfg);

// Reload config.toml, write app_id/app_secret into *every* account, save back.
// These are global Qobuz web-player credentials shared by all accounts, not
// per-account secrets. Reload-modify-save rather than mutating a caller's
// Config so a refresh landing on a download worker thread cannot clobber
// unrelated edits; serialized internally, safe to call from any thread.
void persist_app_credentials(const std::string &app_id, const std::string &app_secret);

// Export/import to an arbitrary path (Settings screen's Export/Import
// Accounts) — same TOML shape as the main config file, just not read from
// or written to config_path().
Config load(const std::filesystem::path &path);
void   save(const Config &cfg, const std::filesystem::path &path);

} // namespace config
