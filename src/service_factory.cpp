#include "service_factory.hh"

namespace qobuz {

// Written once at startup, read-only afterwards; see the header.
static std::string g_ca_bundle_path;

void set_ca_bundle_path(const std::string &path) { g_ca_bundle_path = path; }

kb::Result<kb::QobuzApiService> make_service(const config::Account &account,
                                             bool authenticate) {
    kb::QobuzApiService::Config cfg;
    cfg.app_id        = account.app_id;
    cfg.app_secret    = account.app_secret;
    cfg.ca_bundle_path = g_ca_bundle_path;

    auto res = kb::QobuzApiService::with_credentials(cfg);
    if (!res.ok()) return res.error();

    auto service = res.take();
    // Fires from whichever thread hit the stale secret, download workers
    // included; persist_app_credentials is safe there.
    service.set_credentials_listener(
        [](const std::string &app_id, const std::string &app_secret) {
            config::persist_app_credentials(app_id, app_secret);
        });

    if (authenticate && !account.auth_token.empty()) {
        auto login = service.login_with_token(account.user_id, account.auth_token);
        if (!login.ok()) return login.error();
    }
    return service;
}

} // namespace qobuz
