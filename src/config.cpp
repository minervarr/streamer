#include "config.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <toml++/toml.hpp>

namespace fs = std::filesystem;

namespace config {

static fs::path config_dir() {
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

} // namespace config
