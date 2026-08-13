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

    // Health, written back by account::Pool so a dead token costs its 401
    // once rather than on every run. Unix epoch seconds; 0 means "never".
    // These only ever reorder candidates at selection time — the order of
    // [[accounts]] in config.toml is never rewritten, because the GUI and
    // user scripts address accounts positionally (`--account N`) and an
    // index that silently changes meaning between runs is a trap.
    int64_t     last_ok   = 0;
    int64_t     last_fail = 0;
    std::string fail_reason;   // "auth" | "unavailable" | "" — why last_fail
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

// Reload-modify-save of one account's health fields (see Account::last_ok).
// Identified by `country` — its stable identity here, the same key --country
// and select_account use — rather than a vector index, because the reload
// hands back a fresh vector whose indices need not line up with the caller's.
// Falls back to `user_id` for an account with no country yet; a no-op when
// both are empty. Same threading contract as persist_app_credentials: the
// account::Pool calls it from whichever thread hit the failure.
void persist_account_health(const std::string &country, const std::string &user_id,
                            int64_t last_ok, int64_t last_fail,
                            const std::string &fail_reason);

// Export/import to an arbitrary path (Settings screen's Export/Import
// Accounts) — same TOML shape as the main config file, just not read from
// or written to config_path().
Config load(const std::filesystem::path &path);
void   save(const Config &cfg, const std::filesystem::path &path);

} // namespace config
