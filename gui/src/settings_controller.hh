#pragma once
// SettingsController — pure logic, no rendering. Wraps src/config.hh
// directly in-process (no duplicate TOML parser, unlike streamer-gui's
// Config.h/.cpp) so the GUI and CLI always agree on what's on disk.

#include "config.hh"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
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

    // ── Library backup ────────────────────────────────────────────────────
    // One file that can rebuild the whole library elsewhere (src/backup.hh).
    // All three run on a detached thread — a backup of a large catalog and a
    // restore that downloads for hours must never block the frame loop — and
    // report through backup_status(), which the settings view polls each frame.
    //
    // No path to choose: these always work on a fixed name in the user's home
    // folder. A file picker would put a dialog between the user and the one
    // thing they need, and then leave them wondering where the file went —
    // "it is in your home folder" is an answer they already know.
    static constexpr const char* kBackupFileName   = "streamer-backup.db";
    static constexpr const char* kReadableFileName = "streamer-library.txt";

    void backup_to();
    void restore_from();
    void write_readable();

    bool backup_busy() const { return backup_busy_.load(); }
    std::string backup_status() const;

    const std::string& last_error() const { return last_error_; }
    bool dirty() const { return dirty_; }

    uint64_t revision() const { return revision_; }

private:
    config::Config cfg_;
    int current_ = 0;
    bool dirty_ = false;
    std::string last_error_;
    uint64_t revision_ = 0;

    std::atomic<bool> backup_busy_{false};
    mutable std::mutex backup_mutex_;
    std::string backup_status_;

    void set_backup_status(const std::string& s);
    // Runs `work` on a detached thread unless one is already in flight,
    // funnelling any exception into the status line.
    void run_backup_job(std::function<void()> work);

    void cfg_dirty() { dirty_ = true; ++revision_; }
};

} // namespace settings
