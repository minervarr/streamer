#include "config.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <toml++/toml.hpp>

namespace fs = std::filesystem;

namespace config {

// Set once at startup by platforms whose answer cannot come from the
// environment; see the declaration in config.hh. Plain globals on purpose:
// written before any thread exists, read-only afterwards.
static fs::path g_platform_config_dir;
static fs::path g_platform_download_dir;

void set_platform_dirs(const fs::path &config_dir, const fs::path &download_dir) {
    g_platform_config_dir   = config_dir;
    g_platform_download_dir = download_dir;
}

static fs::path config_dir() {
    if (!g_platform_config_dir.empty()) return g_platform_config_dir;
#ifdef _WIN32
    const char *p = std::getenv("APPDATA");
    if (p) return fs::path(p) / "streamer";
    p = std::getenv("USERPROFILE");
    if (p) return fs::path(p) / "AppData" / "Roaming" / "streamer";
    return fs::path(".");
#else
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) return fs::path(xdg) / "streamer";
    const char *home = std::getenv("HOME");
    if (home) return fs::path(home) / ".config" / "streamer";
    return fs::path(".");
#endif
}

fs::path config_path() { return config_dir() / "config.toml"; }

static fs::path default_download_dir() {
    if (!g_platform_download_dir.empty()) return g_platform_download_dir;
#ifdef _WIN32
    const char *p = std::getenv("USERPROFILE");
    if (p) return fs::path(p) / "Music";
    return fs::path(".");
#else
    const char *home = std::getenv("HOME");
    if (home) {
        fs::path m = fs::path(home) / "Music";
        if (fs::exists(m)) return m;
        return fs::path(home);
    }
    return fs::path(".");
#endif
}

Config load() { return load(config_path()); }

Config load(const fs::path &path) {
    Config cfg;
    cfg.settings.download_dir = default_download_dir();

    if (!fs::exists(path)) return cfg;

    try {
        auto tbl = toml::parse_file(path.u8string());

        if (auto *arr = tbl.get_as<toml::array>("accounts")) {
            for (auto &elem : *arr) {
                if (auto *t = elem.as_table()) {
                    Account a;
                    a.country    = (*t)["country"   ].value<std::string>().value_or("");
                    a.email      = (*t)["email"      ].value<std::string>().value_or("");
                    a.app_id     = (*t)["app_id"     ].value<std::string>().value_or("");
                    a.app_secret = (*t)["app_secret" ].value<std::string>().value_or("");
                    a.user_id    = (*t)["user_id"    ].value<std::string>().value_or("");
                    a.auth_token = (*t)["auth_token" ].value<std::string>().value_or("");
                    // Absent on every config written before failover existed;
                    // 0/"" is the correct "no history yet" state, not an error.
                    a.last_ok     = (*t)["last_ok"    ].value<int64_t>().value_or(0);
                    a.last_fail   = (*t)["last_fail"  ].value<int64_t>().value_or(0);
                    a.fail_reason = (*t)["fail_reason"].value<std::string>().value_or("");
                    cfg.accounts.push_back(std::move(a));
                }
            }
        }

        if (auto *s = tbl.get_as<toml::table>("settings")) {
            std::string dir = (*s)["download_dir"].value<std::string>().value_or("");
            if (!dir.empty()) {
                while (!dir.empty() && (dir.front() == '\'' || dir.front() == '"'))
                    dir.erase(dir.begin());
                while (!dir.empty() && (dir.back()  == '\'' || dir.back()  == '"'))
                    dir.pop_back();
                if (!dir.empty()) cfg.settings.download_dir = fs::u8path(dir);
            }
            cfg.settings.quality             = (*s)["quality"            ].value<std::string>().value_or("flac");
            cfg.settings.requests_per_minute = static_cast<uint32_t>((*s)["requests_per_minute"].value<int64_t>().value_or(0));
            cfg.settings.concurrency         = static_cast<uint32_t>((*s)["concurrency"        ].value<int64_t>().value_or(4));
            cfg.settings.language            = (*s)["language"           ].value<std::string>().value_or("");
        }
    } catch (const toml::parse_error &e) {
        std::fprintf(stderr, "Warning: config parse error: %s\n", e.what());
    }
    return cfg;
}

// Quote a string as a TOML basic string.
static std::string toml_str(const std::string &s) {
    std::string r = "\"";
    for (char c : s) {
        if      (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else                r += c;
    }
    r += '"';
    return r;
}

void save(const Config &cfg) { save(cfg, config_path()); }

void save(const Config &cfg, const fs::path &path) {
    fs::create_directories(path.parent_path());

    std::ostringstream out;
    for (const auto &a : cfg.accounts) {
        out << "[[accounts]]\n";
        out << "country = "    << toml_str(a.country)    << "\n";
        out << "email = "      << toml_str(a.email)      << "\n";
        out << "app_id = "     << toml_str(a.app_id)     << "\n";
        out << "app_secret = " << toml_str(a.app_secret) << "\n";
        out << "user_id = "    << toml_str(a.user_id)    << "\n";
        out << "auth_token = " << toml_str(a.auth_token) << "\n";
        // Omitted while still at their defaults so a config the user has
        // never had a failure on stays as short as it was before.
        if (a.last_ok)   out << "last_ok = "   << a.last_ok   << "\n";
        if (a.last_fail) out << "last_fail = " << a.last_fail << "\n";
        if (!a.fail_reason.empty())
            out << "fail_reason = " << toml_str(a.fail_reason) << "\n";
        out << "\n";
    }
    out << "[settings]\n";
    out << "download_dir = "        << toml_str(cfg.settings.download_dir.u8string()) << "\n";
    out << "quality = "             << toml_str(cfg.settings.quality)               << "\n";
    out << "requests_per_minute = " << cfg.settings.requests_per_minute             << "\n";
    out << "concurrency = "         << cfg.settings.concurrency                     << "\n";
    if (!cfg.settings.language.empty())
        out << "language = " << toml_str(cfg.settings.language) << "\n";

    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write config: " + path.u8string());
    f << out.str();
}

void persist_app_credentials(const std::string &app_id, const std::string &app_secret) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);

    Config cfg = load();
    if (cfg.accounts.empty()) cfg.accounts.push_back({});
    for (auto &a : cfg.accounts) {
        a.app_id     = app_id;
        a.app_secret = app_secret;
    }

    try {
        save(cfg);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Warning: could not persist refreshed credentials: %s\n",
                     e.what());
    }
}

void persist_account_health(const std::string &country, const std::string &user_id,
                            int64_t last_ok, int64_t last_fail,
                            const std::string &fail_reason) {
    if (country.empty() && user_id.empty()) return;

    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);

    Config cfg = load();
    for (auto &a : cfg.accounts) {
        bool match = country.empty() ? (a.user_id == user_id) : (a.country == country);
        if (!match) continue;
        a.last_ok     = last_ok;
        a.last_fail   = last_fail;
        a.fail_reason = fail_reason;
        try {
            save(cfg);
        } catch (const std::exception &e) {
            // Health is an optimisation, never correctness — a read-only
            // config must not take the whole run down with it.
            std::fprintf(stderr, "Warning: could not persist account health: %s\n", e.what());
        }
        return;
    }
}

} // namespace config
