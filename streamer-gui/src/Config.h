#pragma once
#include <string>
#include <vector>

struct Account {
    std::string country, email, app_id, app_secret, user_id, auth_token;
};

struct Config {
    std::vector<Account> accounts;
    std::string download_dir, quality = "flac";
    std::string language;
    int concurrency      = 4;
    int requests_per_min = 60;

    static Config Load();
    void Save() const;
};
