#include "settings_controller.hh"

#include "service_factory.hh"

#include <api/service.hh>

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

} // namespace settings
