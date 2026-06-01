#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include "Config.h"

static std::string ConfigPath() {
    char buf[MAX_PATH] = {};
    SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf);   // AppData\Roaming
    return std::string(buf) + "\\streamer\\config.toml";
}

static void EnsureDir(const std::string& path) {
    auto pos = path.rfind('\\');
    if (pos == std::string::npos) return;
    SHCreateDirectoryExA(nullptr, path.substr(0, pos).c_str(), nullptr);
}

// Convert backslashes to forward slashes for safe TOML storage
static std::string ToTomlPath(const std::string& p) {
    std::string s = p;
    for (auto& c : s) if (c == '\\') c = '/';
    return s;
}

Config Config::Load() {
    Config cfg;
    std::ifstream f(ConfigPath());
    if (!f) return cfg;

    std::string section, line;
    Account pending;
    bool inAccount = false;

    auto flushAccount = [&]() {
        if (inAccount) { cfg.accounts.push_back(pending); pending = {}; inAccount = false; }
    };

    while (std::getline(f, line)) {
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        while (!line.empty() && (line.back()  == ' ' || line.back()  == '\t' || line.back() == '\r')) line.pop_back();

        if (line.empty() || line[0] == '#') continue;

        // [[accounts]] array table
        if (line == "[[accounts]]") {
            flushAccount();
            inAccount = true;
            section = "accounts";
            continue;
        }
        if (line.front() == '[' && line.back() == ']' && line.size() > 2 && line[1] != '[') {
            flushAccount();
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ')  key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'')
            val = val.substr(1, val.size() - 2);

        if (section == "accounts") {
            if (key == "country")    pending.country    = val;
            if (key == "email")      pending.email      = val;
            if (key == "app_id")     pending.app_id     = val;
            if (key == "app_secret") pending.app_secret = val;
            if (key == "user_id")    pending.user_id    = val;
            if (key == "auth_token") pending.auth_token = val;
        } else if (section == "settings") {
            if (key == "download_dir")        cfg.download_dir    = val;
            if (key == "quality")             cfg.quality         = val;
            if (key == "concurrency")         try { cfg.concurrency     = std::stoi(val); } catch (...) {}
            if (key == "requests_per_minute") try { cfg.requests_per_min= std::stoi(val); } catch (...) {}
            if (key == "language")            cfg.language = val;
        }
    }
    flushAccount();
    return cfg;
}

void Config::Save() const {
    std::string path = ConfigPath();
    EnsureDir(path);
    std::ofstream f(path);

    for (const auto& a : accounts) {
        f << "[[accounts]]\n"
          << "country    = \"" << a.country    << "\"\n"
          << "email      = \"" << a.email      << "\"\n"
          << "app_id     = \"" << a.app_id     << "\"\n"
          << "app_secret = \"" << a.app_secret << "\"\n"
          << "user_id    = \"" << a.user_id    << "\"\n"
          << "auth_token = \"" << a.auth_token << "\"\n\n";
    }

    f << "[settings]\n"
      << "download_dir = \"" << ToTomlPath(download_dir) << "\"\n"
      << "quality = \""      << quality         << "\"\n"
      << "concurrency = "    << concurrency      << "\n"
      << "requests_per_minute = " << requests_per_min << "\n";
    if (!language.empty())
        f << "language = \"" << language << "\"\n";
}
