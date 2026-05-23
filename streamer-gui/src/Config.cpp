#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include "Config.h"

static std::string ConfigPath() {
    char buf[MAX_PATH] = {};
    SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, buf);
    return std::string(buf) + "\\.config\\streamer\\config.toml";
}

static void EnsureDir(const std::string& path) {
    auto pos = path.rfind('\\');
    if (pos == std::string::npos) return;
    std::string dir = path.substr(0, pos);
    SHCreateDirectoryExA(nullptr, dir.c_str(), nullptr);
}

Config Config::Load() {
    Config cfg;
    std::ifstream f(ConfigPath());
    if (!f) return cfg;

    std::string section, line;
    while (std::getline(f, line)) {
        // trim
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();

        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if (section == "credentials") {
            if (key == "app_id")     cfg.app_id     = val;
            if (key == "app_secret") cfg.app_secret = val;
            if (key == "user_id")    cfg.user_id    = val;
            if (key == "auth_token") cfg.auth_token = val;
        } else if (section == "settings") {
            if (key == "download_dir")       cfg.download_dir    = val;
            if (key == "quality")            cfg.quality         = val;
            if (key == "concurrency")        cfg.concurrency     = std::stoi(val);
            if (key == "requests_per_minute")cfg.requests_per_min= std::stoi(val);
        }
    }
    return cfg;
}

void Config::Save() const {
    std::string path = ConfigPath();
    EnsureDir(path);
    std::ofstream f(path);
    f << "[credentials]\n"
      << "app_id = \"" << app_id << "\"\n"
      << "app_secret = \"" << app_secret << "\"\n"
      << "user_id = \"" << user_id << "\"\n"
      << "auth_token = \"" << auth_token << "\"\n"
      << "\n[settings]\n"
      << "download_dir = \"" << download_dir << "\"\n"
      << "quality = \"" << quality << "\"\n"
      << "concurrency = " << concurrency << "\n"
      << "requests_per_minute = " << requests_per_min << "\n";
}
