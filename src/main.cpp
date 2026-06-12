#include "config.hh"
#include "download.hh"
#include "history.hh"
#include "i18n.hh"
#include "inspect.hh"
#include "search.hh"
#include "url.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <CLI/CLI.hpp>
#include <api/service.hh>
#include <core/models.hh>

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

static kb::QobuzApiService build_api(const config::Account &acct) {
    kb::QobuzApiService::Config cfg;
    cfg.app_id     = acct.app_id;
    cfg.app_secret = acct.app_secret;
    auto res = kb::QobuzApiService::with_credentials(cfg);
    if (!res.ok()) {
        std::fprintf(stderr, "Error: %s\n", res.error().message.c_str());
        std::exit(1);
    }
    return res.take();
}

static kb::QobuzApiService build_authenticated_api(const config::Account &acct) {
    auto svc = build_api(acct);
    if (acct.auth_token.empty()) {
        std::fprintf(stderr, "%s\n", i18n::t("err_not_authenticated"));
        std::exit(1);
    }
    auto login_res = svc.login_with_token(acct.user_id, acct.auth_token);
    if (!login_res.ok()) {
        std::fprintf(stderr, "Auth error: %s\n", login_res.error().message.c_str());
        std::exit(1);
    }
    return svc;
}

// Find account by country code (or first if country is empty).
static config::Account &select_account(config::Config &cfg, const std::string &country) {
    if (!country.empty()) {
        for (auto &acct : cfg.accounts)
            if (acct.country == country) return acct;
        config::Account blank;
        blank.country = country;
        cfg.accounts.push_back(blank);
        return cfg.accounts.back();
    }
    if (cfg.accounts.empty()) cfg.accounts.push_back({});
    return cfg.accounts.front();
}

// Find account by index (GUI uses --account N).
static config::Account &select_account_idx(config::Config &cfg, int idx) {
    if (cfg.accounts.empty()) cfg.accounts.push_back({});
    if (idx >= 0 && idx < (int)cfg.accounts.size()) return cfg.accounts[idx];
    return cfg.accounts.front();
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    config::Config cfg = config::load();
    i18n::init(cfg.settings.language);

    CLI::App app{"streamer \xe2\x80\x94 Qobuz CLI downloader"};
    app.require_subcommand(1);

    // ── login ─────────────────────────────────────────────────────────────────
    // Option variables must be declared at this scope to outlive CLI11_PARSE.
    std::string login_email, login_password, login_token, login_user_id, login_country;
    {
        auto *sub = app.add_subcommand("login", i18n::t("cmd_login"));
        sub->add_option("--email",    login_email,    "Account e-mail");
        sub->add_option("--password", login_password, "Account password");
        sub->add_option("--token",    login_token,    "Existing auth token (skip login)");
        sub->add_option("--user-id",  login_user_id,  "User ID (required with --token)");
        sub->add_option("--country",  login_country,  "Country code for multi-account");

        sub->callback([&]() {
            config::Config cur = config::load();
            config::Account &acct = select_account(cur, login_country);

            if (acct.app_id.empty()) {
                std::fprintf(stderr, "%s\n", i18n::t("err_app_id_not_set"));
                std::exit(1);
            }

            auto svc = build_api(acct);

            if (!login_token.empty()) {
                if (login_user_id.empty()) {
                    std::fprintf(stderr, "Error: --user-id is required with --token\n");
                    std::exit(1);
                }
                auto res = svc.login_with_token(login_user_id, login_token);
                if (!res.ok()) {
                    std::fprintf(stderr, "Login error: %s\n", res.error().message.c_str());
                    std::exit(1);
                }
                acct.user_id    = login_user_id;
                acct.auth_token = login_token;
                acct.country    = res.value();
            } else {
                if (login_email.empty() || login_password.empty()) {
                    std::fprintf(stderr,
                        "Error: provide --email/--password or --token/--user-id\n");
                    std::exit(1);
                }
                auto res = svc.login(login_email, login_password);
                if (!res.ok()) {
                    std::fprintf(stderr, "Login error: %s\n", res.error().message.c_str());
                    std::exit(1);
                }
                acct.auth_token = res.value().first;
                acct.user_id    = std::to_string(res.value().second);
                acct.email      = login_email;

                auto vc = svc.login_with_token(acct.user_id, acct.auth_token);
                if (vc.ok()) acct.country = vc.value();
            }

            config::save(cur);
            std::printf("%s\n", i18n::t("login_success"));
        });
    }

    // ── download ──────────────────────────────────────────────────────────────
    std::string dl_url, dl_quality, dl_output, dl_country;
    int dl_concurrency = 0, dl_account = -1;
    bool dl_no_extras = false, dl_no_metadata = false;
    {
        auto *sub = app.add_subcommand("download", i18n::t("cmd_download"));
        sub->add_option("url", dl_url, "Qobuz URL or ID")->required();
        sub->add_option("-q,--quality",     dl_quality,     "Quality override");
        sub->add_option("-o,--output",      dl_output,      "Output directory");
        sub->add_option("-j,--concurrency", dl_concurrency, "Concurrent downloads");
        sub->add_option("--country",        dl_country,     "Account country code");
        sub->add_option("--account",        dl_account,     "Account index (GUI)");
        sub->add_flag("--no-extras",   dl_no_extras,   "Skip cover/booklet/bio extras");
        sub->add_flag("--no-metadata", dl_no_metadata, "Skip tag embedding");

        sub->callback([&]() {
            config::Config cur = config::load();
            i18n::init(cur.settings.language);
            const config::Account &acct = (dl_account >= 0)
                ? select_account_idx(cur, dl_account)
                : select_account(cur, dl_country);
            auto svc = build_authenticated_api(acct);

            std::string quality = dl_quality.empty() ? cur.settings.quality : dl_quality;
            std::string outdir  = dl_output.empty()
                ? cur.settings.download_dir.string() : dl_output;
            if (outdir.empty()) outdir = ".";
            int conc = (dl_concurrency > 0) ? dl_concurrency
                                            : static_cast<int>(cur.settings.concurrency);
            if (conc <= 0) conc = 4;

            bool ok = dl::run(svc, dl_url, quality, outdir, acct.country, conc,
                              !dl_no_extras, !dl_no_metadata);
            if (!ok) std::exit(1);
        });
    }

    // ── search ────────────────────────────────────────────────────────────────
    std::string search_query, search_kind = "albums", search_min_dur, search_max_dur, search_country;
    int search_limit = 10, search_account = -1;
    bool search_tsv = false;
    {
        auto *sub = app.add_subcommand("search", i18n::t("cmd_search"));
        sub->add_option("query", search_query, "Search query")->required();
        sub->add_option("-t,--type",  search_kind,    "albums|tracks|artists|playlists|all");
        sub->add_option("-n,--limit", search_limit,   "Max results per category");
        sub->add_option("--min-dur",  search_min_dur, "Min duration (e.g. 5m, 1:30)");
        sub->add_option("--max-dur",  search_max_dur, "Max duration");
        sub->add_option("--country",  search_country, "Account country code");
        sub->add_option("--account",  search_account, "Account index (GUI)");
        sub->add_flag("--tsv",        search_tsv,     "Tab-separated output");

        sub->callback([&]() {
            config::Config cur = config::load();
            i18n::init(cur.settings.language);
            const config::Account &acct = (search_account >= 0)
                ? select_account_idx(cur, search_account)
                : select_account(cur, search_country);
            auto svc = build_authenticated_api(acct);

            std::optional<int> mn, mx;
            if (!search_min_dur.empty()) {
                int s;
                auto err = search::parse_duration(search_min_dur, s);
                if (err) { std::fprintf(stderr, "Error: %s\n", err->c_str()); std::exit(1); }
                mn = s;
            }
            if (!search_max_dur.empty()) {
                int s;
                auto err = search::parse_duration(search_max_dur, s);
                if (err) { std::fprintf(stderr, "Error: %s\n", err->c_str()); std::exit(1); }
                mx = s;
            }
            search::run(svc, search_query, search_kind, search_tsv, search_limit, mn, mx);
        });
    }

    // ── inspect ───────────────────────────────────────────────────────────────
    std::string inspect_target, inspect_country;
    int inspect_account = -1;
    bool inspect_tsv = false;
    {
        auto *sub = app.add_subcommand("inspect", i18n::t("cmd_inspect"));
        sub->add_option("target", inspect_target, "Album ID/URL or track ID/URL")->required();
        sub->add_option("--country", inspect_country, "Account country code");
        sub->add_option("--account", inspect_account, "Account index (GUI)");
        sub->add_flag("--tsv",       inspect_tsv,     "TSV output for GUI");

        sub->callback([&]() {
            config::Config cur = config::load();
            i18n::init(cur.settings.language);
            const config::Account &acct = (inspect_account >= 0)
                ? select_account_idx(cur, inspect_account)
                : select_account(cur, inspect_country);
            auto svc = build_authenticated_api(acct);

            auto tgt = url::parse(inspect_target);
            if (!tgt) {
                std::fprintf(stderr, "%s: %s\n",
                             i18n::t("error_parse_url"), inspect_target.c_str());
                std::exit(1);
            }
            std::visit([&](auto &&t) {
                using T = std::decay_t<decltype(t)>;
                if constexpr (std::is_same_v<T, url::Album>) {
                    if (inspect_tsv) inspect::run_album_tsv(svc, t.id);
                    else             inspect::run_album(svc, t.id);
                } else if constexpr (std::is_same_v<T, url::Track>) {
                    inspect::run_track(svc, t.id);
                } else {
                    std::fprintf(stderr, "inspect only supports album and track targets\n");
                    std::exit(1);
                }
            }, *tgt);
        });
    }

    // ── config ────────────────────────────────────────────────────────────────
    std::string cfg_dir, cfg_quality;
    {
        auto *sub = app.add_subcommand("config", i18n::t("cmd_config"));
        sub->require_subcommand(1);

        auto *show = sub->add_subcommand("show", i18n::t("cmd_show"));
        show->callback([&]() {
            config::Config cur = config::load();
            std::printf("%s %s\n", i18n::t("config_file"),
                        config::config_path().string().c_str());
            std::printf("%s %s\n", i18n::t("download_dir_label"),
                        cur.settings.download_dir.string().c_str());
            std::printf("%s %s\n", i18n::t("quality_label"),
                        cur.settings.quality.c_str());
            std::printf("%s\n", i18n::t("accounts_label"));
            for (auto &a : cur.accounts) {
                bool auth = !a.auth_token.empty();
                std::printf("  [%s] %s (%s)\n",
                    a.country.empty() ? "?" : a.country.c_str(),
                    a.email.c_str(),
                    auth ? i18n::t("authenticated") : "");
            }
        });

        auto *sd = sub->add_subcommand("set-dir", i18n::t("cmd_set_dir"));
        sd->add_option("dir", cfg_dir, "Directory path")->required();
        sd->callback([&]() {
            config::Config cur = config::load();
            cur.settings.download_dir = cfg_dir;
            config::save(cur);
            std::printf("%s %s\n", i18n::t("download_dir_set"), cfg_dir.c_str());
        });

        auto *sq = sub->add_subcommand("set-quality", i18n::t("cmd_set_quality"));
        sq->add_option("quality", cfg_quality, "mp3|flac|flac-hi|flac-ultra")->required();
        sq->callback([&]() {
            config::Config cur = config::load();
            cur.settings.quality = cfg_quality;
            config::save(cur);
            std::printf("%s %s\n", i18n::t("default_quality_set"), cfg_quality.c_str());
        });
    }

    // ── history ───────────────────────────────────────────────────────────────
    int hist_limit = 20;
    bool hist_tsv = false;
    std::string hist_export_path, hist_import_path;
    {
        auto *sub = app.add_subcommand("history", i18n::t("cmd_history"));
        sub->require_subcommand(0);
        sub->add_option("-n,--limit", hist_limit, "Number of recent records to show");
        sub->add_flag("--tsv", hist_tsv, "Tab-separated output for GUI");

        auto *exp = sub->add_subcommand("export", i18n::t("cmd_export"));
        exp->add_option("file", hist_export_path,
            "Output JSON file (default: history_export.json)");
        exp->callback([&]() {
            std::string outpath = hist_export_path.empty() ? "history_export.json"
                                                           : hist_export_path;
            std::string json = history::export_json();
            std::ofstream f(outpath);
            if (!f) {
                std::fprintf(stderr, "%s %s\n",
                             i18n::t("warning_could_not_save"), outpath.c_str());
                std::exit(1);
            }
            f << json;
            std::printf("%s %s\n", i18n::t("exported_to"), outpath.c_str());
        });

        auto *imp = sub->add_subcommand("import", i18n::t("cmd_import"));
        imp->add_option("file", hist_import_path, "JSON file to import")->required();
        imp->callback([&]() {
            std::ifstream f(hist_import_path);
            if (!f) {
                std::fprintf(stderr, "%s %s\n",
                             i18n::t("warning_could_not_read"), hist_import_path.c_str());
                std::exit(1);
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            uint32_t n = history::import_json(ss.str());
            std::printf("%s %u %s\n", i18n::t("imported_records"), n,
                        i18n::t("new_records"));
        });

        auto *clr = sub->add_subcommand("clear", i18n::t("cmd_clear"));
        clr->callback([&]() {
            uint32_t n = history::clear();
            std::printf("%s %u %s\n", i18n::t("deleted_records"), n,
                        i18n::t("records_suffix"));
        });

        sub->callback([&]() {
            if (sub->got_subcommand("export") || sub->got_subcommand("import")
                    || sub->got_subcommand("clear")) return;
            if (hist_tsv) {
                history::print_tsv(static_cast<uint32_t>(hist_limit));
                return;
            }
            auto records = history::list_recent(static_cast<uint32_t>(hist_limit));
            if (records.empty()) {
                std::printf("%s\n", i18n::t("no_history"));
                return;
            }
            for (auto &r : records)
                std::printf("%s\n", history::format_record_line(r).c_str());
            std::printf("%zu %s\n", records.size(), i18n::t("records_shown"));
        });
    }

    CLI11_PARSE(app, argc, argv);
    return 0;
}
