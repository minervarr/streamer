#pragma once
// SettingsController — pure logic, no rendering. Wraps src/config.hh
// directly in-process (no duplicate TOML parser, unlike streamer-gui's
// Config.h/.cpp) so the GUI and CLI always agree on what's on disk.

#include "config.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace settings {

class SettingsController {
public:
    // Loads config::load() immediately; construction never fails (a fresh
    // install with no config.toml yet is a normal, valid state).
    SettingsController();

    const config::Config& config() const { return cfg_; }
    config::Settings& mutable_settings() { cfg_dirty(); return cfg_.settings; }

    const std::vector<config::Account>& accounts() const { return cfg_.accounts; }
    int current_account_index() const { return current_; }
    void select_account(int idx);
    void add_account();
    void remove_account(int idx);
    // Mutable ref to the currently-selected account for the edit form;
    // always valid (adds a blank account if the list is empty).
    config::Account& current_account();

    // Attempts svc.login_with_token(user_id, auth_token) against the
    // current account's app_id/app_secret + the given credentials, and on
    // success fills in country. Mirrors the CLI's `streamer login --token`
    // path exactly. Returns false + fills last_error() on failure.
    bool login_with_token(const std::string& user_id, const std::string& auth_token);

    // Writes config::save(cfg_) to config::config_path(). Returns false +
    // fills last_error() on a filesystem error.
    bool save();
    bool export_to(const std::string& path);
    bool import_from(const std::string& path);

    const std::string& last_error() const { return last_error_; }
    bool dirty() const { return dirty_; }

    uint64_t revision() const { return revision_; }

private:
    config::Config cfg_;
    int current_ = 0;
    bool dirty_ = false;
    std::string last_error_;
    uint64_t revision_ = 0;

    void cfg_dirty() { dirty_ = true; ++revision_; }
};

} // namespace settings
