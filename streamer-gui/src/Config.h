#pragma once
#include <string>

struct Config {
    std::string app_id, app_secret, user_id, auth_token;
    std::string download_dir, quality = "flac";
    int         concurrency       = 4;
    int         requests_per_min  = 60;

    static Config Load();
    void Save() const;
};
