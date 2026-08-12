#include "settings_controller.hh"

#include "backup.hh"
#include "service_factory.hh"

#include <filesystem>
#include <fstream>
#include <thread>

#include <api/service.hh>

namespace fs = std::filesystem;

namespace settings {

SettingsController::SettingsController() : cfg_(config::load()) {}

void SettingsController::select_account(int idx) {
    if (idx < 0 || idx >= (int)cfg_.accounts.size()) return;
    current_ = idx;
    ++revision_;
}

void SettingsController::add_account() {
    cfg_.accounts.push_back({});
    current_ = (int)cfg_.accounts.size() - 1;
    cfg_dirty();
}

void SettingsController::remove_account(int idx) {
    if (idx < 0 || idx >= (int)cfg_.accounts.size()) return;
    cfg_.accounts.erase(cfg_.accounts.begin() + idx);
    if (current_ >= (int)cfg_.accounts.size())
        current_ = (int)cfg_.accounts.size() - 1;
    cfg_dirty();
}

config::Account& SettingsController::current_account() {
    if (cfg_.accounts.empty()) { cfg_.accounts.push_back({}); current_ = 0; }
    if (current_ < 0 || current_ >= (int)cfg_.accounts.size()) current_ = 0;
    return cfg_.accounts[(size_t)current_];
}

bool SettingsController::login_with_token(const std::string& user_id, const std::string& auth_token) {
    last_error_.clear();
    config::Account& acct = current_account();
    if (acct.app_id.empty() || acct.app_secret.empty()) {
        last_error_ = "app_id/app_secret must be set for this account before logging in.";
        ++revision_;
        return false;
    }
    // authenticate=false: log in with the credentials the user just typed,
    // not with whatever token the account already has stored.
    auto svcRes = qobuz::make_service(acct, /*authenticate=*/false);
    if (!svcRes.ok()) { last_error_ = svcRes.error().message; ++revision_; return false; }
    auto svc = svcRes.take();

    auto res = svc.login_with_token(user_id, auth_token);
    if (!res.ok()) { last_error_ = res.error().message; ++revision_; return false; }

    acct.user_id = user_id;
    acct.auth_token = auth_token;
    acct.country = res.value();
    cfg_dirty();
    return true;
}

bool SettingsController::save() {
    last_error_.clear();
    try {
        config::save(cfg_);
        dirty_ = false;
        ++revision_;
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        ++revision_;
        return false;
    }
}

bool SettingsController::export_to(const std::string& path) {
    last_error_.clear();
    try {
        config::save(cfg_, path);
        ++revision_;
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        ++revision_;
        return false;
    }
}

bool SettingsController::import_from(const std::string& path) {
    last_error_.clear();
    config::Config imported = config::load(path);
    if (imported.accounts.empty()) {
        last_error_ = "No accounts found in " + path;
        ++revision_;
        return false;
    }
    for (auto& a : imported.accounts) cfg_.accounts.push_back(std::move(a));
    cfg_dirty();
    return true;
}

// ── Library backup ──────────────────────────────────────────────────────────

void SettingsController::set_backup_status(const std::string& s) {
    std::lock_guard<std::mutex> lock(backup_mutex_);
    backup_status_ = s;
}

std::string SettingsController::backup_status() const {
    std::lock_guard<std::mutex> lock(backup_mutex_);
    return backup_status_;
}

void SettingsController::run_backup_job(std::function<void()> work) {
    bool expected = false;
    if (!backup_busy_.compare_exchange_strong(expected, true)) return;  // one at a time
    std::thread([this, work = std::move(work)]() {
        try {
            work();
        } catch (const std::exception& e) {
            set_backup_status(std::string("Failed: ") + e.what());
        }
        backup_busy_.store(false);
    }).detach();
}

// The user's home folder, the same way src/config.cpp finds it.
static fs::path home_folder() {
#ifdef _WIN32
    if (const char* p = std::getenv("USERPROFILE")) return fs::path(p);
#else
    if (const char* p = std::getenv("HOME")) return fs::path(p);
#endif
    return fs::path(".");
}

void SettingsController::backup_to() {
    const std::string file = (home_folder() / kBackupFileName).u8string();
    run_backup_job([this, file]() {
        set_backup_status("Backing up to " + file + " ...");
        backup::CreateOptions opts;
        opts.overwrite = true;   // the GUI offers one fixed name; replacing it is the point
        auto r = backup::create(file, opts);
        set_backup_status(std::to_string(r.albums) + " albums, " +
                          std::to_string(r.tracks) + " tracks saved to " + file +
                          " (contains your auth tokens — keep it private)");
    });
}

void SettingsController::restore_from() {
    const std::string file = (home_folder() / kBackupFileName).u8string();
    if (!fs::exists(fs::u8path(file))) {
        // Naming the path beats "file not found": it tells the user exactly
        // where to drop the backup they brought from the old machine.
        set_backup_status("No backup found. Put your backup file at " + file +
                          " and press Restore again.");
        return;
    }
    run_backup_job([this, file]() {
        set_backup_status("Reading " + file + " ...");
        backup::RestoreOptions opts;
        auto r = backup::restore(file, opts,
            [this](const std::string& line, int done, int total) {
                set_backup_status("[" + std::to_string(done) + "/" +
                                  std::to_string(total) + "] " + line);
            });
        std::string s = std::to_string(r.downloaded) + " downloaded, " +
                        std::to_string(r.skipped) + " already there, " +
                        std::to_string(r.failed) + " failed";
        if (!r.failures_path.empty()) s += " — see " + r.failures_path;
        set_backup_status(s);
    });
}

void SettingsController::write_readable() {
    const std::string backup_file = (home_folder() / kBackupFileName).u8string();
    // The readable list is worth having whether or not a backup was made
    // here: fall back to the live catalog so the button always does something.
    const std::string live = backup::catalog_path(
        cfg_.settings.download_dir.u8string());
    const std::string out = (home_folder() / kReadableFileName).u8string();
    run_backup_job([this, backup_file, live, out]() {
        const std::string src = fs::exists(fs::u8path(backup_file)) ? backup_file : live;
        set_backup_status("Reading " + src + " ...");
        std::string text = backup::readable(src, /*tsv=*/false);
        std::ofstream f(fs::u8path(out), std::ios::binary | std::ios::trunc);
        if (!f) {
            set_backup_status("Could not write " + out);
            return;
        }
        f << text;
        set_backup_status("Readable list written to " + out);
    });
}

} // namespace settings
