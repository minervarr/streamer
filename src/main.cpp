#include "account_pool.hh"
#include "backup.hh"
#include "config.hh"
#include "download.hh"
#include "history.hh"
#include "i18n.hh"
#include "inspect.hh"
#include "library.hh"
#include "search.hh"
#include "service_factory.hh"
#include "url.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
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
    auto res = qobuz::make_service(acct, /*authenticate=*/false);
    if (!res.ok()) {
        std::fprintf(stderr, "Error: %s\n", res.error().message.c_str());
        std::exit(1);
    }
    return res.take();
}

// Acquires a logged-in service, walking the health-ranked accounts until one
// authenticates. Replaces the old "take accounts.front(), die on its 401"
// path, which failed every subcommand whenever the first stored token had
// expired — even with a perfectly good account one slot below.
//
// The chosen account is copied into `chosen_out` (callers need its country
// for the download tier). Builds services itself rather than through
// Pool::acquire because the CLI runs one command against one account and then
// exits; the pool's cross-call service cache only earns its keep in the GUI.
static kb::QobuzApiService acquire_service(config::Config &cfg,
                                           const std::string &country,
                                           int account_idx,
                                           config::Account &chosen_out) {
    account::Pool pool(cfg);
    account::Selector sel = (account_idx >= 0)
        ? account::Selector{account::Selector::Index, "", account_idx}
        : account::Selector::from_country(country);

    std::vector<int> cands = pool.candidates(sel);
    if (cands.empty()) {
        std::fprintf(stderr, "%s\n", i18n::t("err_not_authenticated"));
        std::exit(1);
    }

    for (int idx : cands) {
        const config::Account &acct = cfg.accounts[idx];
        if (acct.auth_token.empty()) continue;

        auto res = qobuz::make_service(acct);
        if (res.ok()) {
            pool.record_ok(idx);
            // Only worth narrating when something was actually skipped —
            // a healthy run stays as quiet as it always was.
            if (!pool.journal().empty()) {
                std::fputs(pool.report().c_str(), stderr);
                std::fprintf(stderr, "-> %s %s\n", i18n::t("acct_using"),
                             acct.country.empty() ? "(no country)" : acct.country.c_str());
            }
            chosen_out = acct;
            return res.take();
        }

        account::Failure f = account::classify(res.error());
        pool.record_fail(idx, f, res.error().message);
        if (!account::should_failover(f)) break;
    }

    std::fprintf(stderr, "%s\n", i18n::t("err_all_accounts_failed"));
    std::fputs(pool.report().c_str(), stderr);
    std::exit(1);
}

// Runs `fn` against the selected accounts until one *succeeds at the actual
// work* — not merely until one authenticates. That distinction is the point:
// availability is regional, so `inspect`/`search` failing with
// ResourceNotFound under FR is a reason to ask NZ, and only account::Pool can
// tell that apart from a network outage (where retrying elsewhere is useless).
//
// Prints the walk when anything was skipped, then returns the outcome rather
// than exiting on failure: "nothing found in any region" means different
// things per subcommand — a valid answer to a search, a failure for an
// inspect of one named album.
template <class T>
static kb::Result<T> run_with_failover(
    config::Config &cfg, const std::string &country, int account_idx,
    const std::function<kb::Result<T>(kb::QobuzApiService &, const config::Account &)> &fn) {
    account::Pool pool(cfg);
    account::Selector sel = (account_idx >= 0)
        ? account::Selector{account::Selector::Index, "", account_idx}
        : account::Selector::from_country(country);

    auto res = pool.with_service<T>(sel, fn);
    if (!pool.journal().empty()) std::fputs(pool.report().c_str(), stderr);
    return res;
}

// Runs `fn` against every account `--country all` names, in health order,
// skipping the ones that cannot authenticate. Exists because availability is
// regional: a release absent from one country's catalog may be present in
// another's, and the only way to see that is to ask each account in turn.
// Exits with a report if not one of them authenticated.
static void for_each_service(
    config::Config &cfg,
    const std::function<void(kb::QobuzApiService &, const config::Account &)> &fn) {
    account::Pool pool(cfg);
    bool any = false;

    for (int idx : pool.candidates(account::Selector{account::Selector::All})) {
        const config::Account &acct = cfg.accounts[idx];
        if (acct.auth_token.empty()) continue;

        auto res = qobuz::make_service(acct);
        if (!res.ok()) {
            pool.record_fail(idx, account::classify(res.error()), res.error().message);
            continue;
        }
        pool.record_ok(idx);
        any = true;
        auto svc = res.take();
        fn(svc, acct);
    }

    // Report what was skipped either way: on success it explains why a region
    // is missing from the output, and on failure it is the whole diagnosis.
    if (!pool.journal().empty()) std::fputs(pool.report().c_str(), stderr);
    if (!any) {
        std::fprintf(stderr, "%s\n", i18n::t("err_all_accounts_failed"));
        std::exit(1);
    }
}

// Library root for the `library` subcommands: --root wins, otherwise the
// configured download directory. File scope, not a lambda inside the
// subcommand block — CLI11 callbacks run after that block has exited.
static std::string library_root(const std::string &override_root) {
    if (!override_root.empty()) return override_root;
    std::string dir = config::load().settings.download_dir.u8string();
    return dir.empty() ? std::string(".") : dir;
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

// (Account-by-index lookup now lives in account::Pool, which needs it as a
// Selector mode rather than a bare reference — see acquire_service above.
// `select_account` stays because `login` writes *into* the account it finds.)

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    config::Config cfg = config::load();
    i18n::init(cfg.settings.language);
    kb::api::set_requests_per_minute(cfg.settings.requests_per_minute);

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
            config::Account acct;
            auto svc = acquire_service(cur, dl_country, dl_account, acct);

            std::string quality = dl_quality.empty() ? cur.settings.quality : dl_quality;
            std::string outdir  = dl_output.empty()
                ? cur.settings.download_dir.u8string() : dl_output;
            if (outdir.empty()) outdir = ".";
            int conc = (dl_concurrency > 0) ? dl_concurrency
                                            : static_cast<int>(cur.settings.concurrency);
            if (conc <= 0) conc = 8;

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
        sub->add_option("--country",  search_country, "Account country code, or `all` to search every account");
        sub->add_option("--account",  search_account, "Account index (GUI)");
        sub->add_flag("--tsv",        search_tsv,     "Tab-separated output");

        sub->callback([&]() {
            config::Config cur = config::load();
            i18n::init(cur.settings.language);

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
            // `--country all` asks every account and labels each block, so a
            // release missing from one region is visibly present in another.
            // Anything else is the normal single-account path with failover.
            if (account::Selector::from_country(search_country).mode == account::Selector::All) {
                for_each_service(cur, [&](kb::QobuzApiService &svc, const config::Account &a) {
                    // The TSV form is machine-read, so the country belongs in
                    // a column, not a banner the parser would choke on.
                    if (search_tsv) std::printf("# country\t%s\n", a.country.c_str());
                    else            std::printf("\n== %s ==\n", a.country.c_str());
                    auto r = search::run(svc, search_query, search_kind, search_tsv,
                                         search_limit, mn, mx);
                    // "Nothing here" is a real answer when you are comparing
                    // regions — silence would read as a broken account.
                    if (!r.ok() && !search_tsv)
                        std::printf("  %s\n", i18n::t("search_no_results"));
                });
                return;
            }

            // Failing over on a *result* miss, not just an auth one: a query
            // that finds nothing under one region's catalog routinely finds
            // plenty under another, and search::run reports that as
            // ResourceNotFound precisely so this loop can act on it.
            auto found = run_with_failover<int>(cur, search_country, search_account,
                [&](kb::QobuzApiService &svc, const config::Account &) {
                    return search::run(svc, search_query, search_kind, search_tsv,
                                       search_limit, mn, mx);
                });
            if (!found.ok()) {
                // A search that matches nothing is an answer, not a failure —
                // exiting non-zero here would break every script that greps
                // its own catalog. Anything else genuinely went wrong.
                if (account::classify(found.error()) == account::Failure::Unavailable) {
                    if (!search_tsv)
                        std::printf("%s\n", i18n::t("search_no_results_anywhere"));
                } else {
                    std::fprintf(stderr, "%s\n", found.error().message.c_str());
                    std::exit(1);
                }
            }
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
            auto tgt = url::parse(inspect_target);
            if (!tgt) {
                std::fprintf(stderr, "%s: %s\n",
                             i18n::t("error_parse_url"), inspect_target.c_str());
                std::exit(1);
            }

            // The clearest regional case there is: an album Qobuz will not
            // serve to FR is often perfectly available to NZ. Before this,
            // that printed "not found" and stopped.
            auto found = run_with_failover<bool>(cur, inspect_country, inspect_account,
                [&](kb::QobuzApiService &svc, const config::Account &) -> kb::Result<bool> {
                    kb::Result<void> r = kb::Result<void>();
                    std::visit([&](auto &&t) {
                        using T = std::decay_t<decltype(t)>;
                        if constexpr (std::is_same_v<T, url::Album>) {
                            r = inspect_tsv ? inspect::run_album_tsv(svc, t.id)
                                            : inspect::run_album(svc, t.id);
                        } else if constexpr (std::is_same_v<T, url::Track>) {
                            r = inspect::run_track(svc, t.id);
                        } else {
                            std::fprintf(stderr,
                                "inspect only supports album and track targets\n");
                            std::exit(1);
                        }
                    }, *tgt);
                    if (!r.ok()) return r.error();
                    return true;
                });
            // Here "not available in any region I can reach" IS the failure:
            // the user named one specific release.
            if (!found.ok()) {
                std::fprintf(stderr, "%s\n", found.error().message.c_str());
                std::exit(1);
            }
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
                        config::config_path().u8string().c_str());
            std::printf("%s %s\n", i18n::t("download_dir_label"),
                        cur.settings.download_dir.u8string().c_str());
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
            std::ofstream f(fs::u8path(outpath));
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
            std::ifstream f(fs::u8path(hist_import_path));
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

        // `sub` by value — see the note on the backup subcommand: by the time
        // CLI11 runs this, the block-local pointer variable is gone.
        sub->callback([&, sub]() {
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

    // ── library ───────────────────────────────────────────────────────────────
    // Downloads are ID-addressed on disk, so this is where the real names are.
    std::string lib_root, lib_key, lib_country;
    int lib_limit = 50, lib_account = -1;
    bool lib_tracks = false, lib_tsv = false, lib_dry_run = false, lib_offline = false;
    {
        auto *sub = app.add_subcommand("library", i18n::t("cmd_library"));
        sub->require_subcommand(1);
        sub->add_option("--root", lib_root,
            "Library root (default: the configured download directory)");
        sub->add_option("--country", lib_country, "Account country code (scan)");
        sub->add_option("--account", lib_account, "Account index (scan)");

        auto *ls = sub->add_subcommand("list", i18n::t("cmd_lib_list"));
        ls->add_flag("--tracks", lib_tracks, "List tracks instead of albums");
        ls->add_option("-n,--limit", lib_limit, "Maximum rows");
        ls->add_flag("--tsv", lib_tsv, "Tab-separated output for GUI");
        ls->callback([&]() {
            std::string root = library_root(lib_root);
            uint32_t limit = static_cast<uint32_t>(lib_limit > 0 ? lib_limit : 50);
            if (lib_tracks) {
                auto rows = library::list_tracks(root, limit);
                if (rows.empty()) {
                    std::printf("%s %s\n", i18n::t("lib_empty"), root.c_str());
                    return;
                }
                for (const auto &e : rows) {
                    if (lib_tsv)
                        std::printf("%lld\t%s\t%s\t%s\t%s\t%s\n", (long long)e.track_id,
                                    e.track_title.c_str(), e.album_title.c_str(),
                                    e.artist_name.c_str(), e.quality.c_str(),
                                    e.rel_path.c_str());
                    else
                        std::printf("%3d. %-40s  %s — %s  [%s]\n", e.track_number.value_or(0),
                                    e.track_title.c_str(), e.artist_name.c_str(),
                                    e.album_title.c_str(), e.quality.c_str());
                }
                return;
            }
            auto rows = library::list_albums(root, limit);
            if (rows.empty()) {
                if (library::catalog_looks_lost(root))
                    std::printf("%s\n", i18n::t("lib_catalog_lost"));
                else
                    std::printf("%s %s\n", i18n::t("lib_empty"), root.c_str());
                return;
            }
            for (const auto &a : rows) {
                if (lib_tsv)
                    std::printf("%s\t%s\t%s\t%s\t%d/%d\n", a.id.c_str(), a.title.c_str(),
                                a.artist_name.c_str(), a.release_date.c_str(),
                                a.files_on_disk, a.tracks_count.value_or(0));
                else
                    std::printf("%-14s %s — %s  (%s)  %d/%d tracks\n", a.id.c_str(),
                                a.artist_name.c_str(), a.title.c_str(),
                                a.release_date.c_str(), a.files_on_disk,
                                a.tracks_count.value_or(0));
            }
        });

        auto *sc = sub->add_subcommand("scan", i18n::t("cmd_lib_scan"));
        sc->add_flag("--dry-run", lib_dry_run, "Report what would change without writing");
        sc->add_flag("--offline", lib_offline,
            "Don't contact Qobuz; files the catalog has never seen stay unknown");
        sc->callback([&]() {
            std::string root = library_root(lib_root);

            // Only needed to re-learn albums the catalog has lost; an offline
            // scan still prunes deleted files and refreshes sizes.
            library::AlbumFetcher fetch;
            if (!lib_offline) {
                config::Config cur = config::load();
                config::Account acct;
                auto svc = std::make_shared<kb::QobuzApiService>(
                    acquire_service(cur, lib_country, lib_account, acct));
                fetch = [svc](const std::string &album_id) -> std::optional<kb::Album> {
                    auto res = svc->get_album(album_id, std::string("track_ids"));
                    if (!res.ok()) return std::nullopt;
                    return res.value();
                };
            }

            auto r = library::scan(root, fetch, lib_dry_run);
            std::printf("%s\n  %d file(s) on disk\n  %d adopted\n  %d stale entr(ies) removed\n"
                        "  %d size(s) updated\n  %d asset(s) registered\n"
                        "  %d album(s) re-fetched\n  %d unrecognised\n",
                        root.c_str(), r.files_seen, r.adopted, r.removed, r.updated,
                        r.assets, r.fetched, r.unknown);
            if (lib_dry_run) std::printf("%s\n", i18n::t("lib_scan_dry"));
        });

        auto *rv = sub->add_subcommand("resolve", i18n::t("cmd_lib_resolve"));
        rv->add_option("key", lib_key, "Album id, track id, or path")->required();
        rv->callback([&]() {
            std::string root = library_root(lib_root);
            auto found = library::resolve(root, lib_key);
            if (!found) {
                std::fprintf(stderr, "%s %s\n", i18n::t("lib_not_found"), lib_key.c_str());
                std::exit(1);
            }
            const auto &a = found->album;
            if (!a.id.empty()) {
                std::printf("%s\n  %s", a.title.c_str(), a.artist_name.c_str());
                if (!a.release_date.empty()) std::printf("  (%s)", a.release_date.c_str());
                if (!a.label_name.empty())   std::printf("  · %s", a.label_name.c_str());
                std::printf("\n  id %s\n", a.id.c_str());
            }
            for (const auto &t : found->tracks) {
                std::printf("  %3d. %-45s %s\n", t.track_number.value_or(0),
                            t.track_title.c_str(), t.rel_path.c_str());
            }
        });
    }

    // ── backup / restore ──────────────────────────────────────────────────────
    // One file that stands in for the whole library. It holds names and ids,
    // never audio: downloads are ID-addressed, so re-fetching is possible from
    // a list of ids, and a list of ids is small.
    std::string bk_file, bk_root, bk_list_file, bk_list_out;
    bool bk_force = false, bk_no_accounts = false, bk_list_tsv = false;
    {
        auto *sub = app.add_subcommand("backup", i18n::t("cmd_backup"));
        sub->require_subcommand(0);
        sub->add_option("file", bk_file, "Backup file to write (default: streamer-backup.db)");
        sub->add_option("--root", bk_root,
            "Library root (default: the configured download directory)");
        sub->add_flag("--force", bk_force, "Replace the file if it already exists");
        sub->add_flag("--no-accounts", bk_no_accounts,
            "Leave accounts and auth tokens out of the file");

        auto *ls = sub->add_subcommand("list", i18n::t("cmd_backup_list"));
        ls->add_option("file", bk_list_file, "Backup file (or a library.db)")->required();
        ls->add_flag("--tsv", bk_list_tsv, "One flat row per track, for a spreadsheet");
        ls->add_option("-o,--output", bk_list_out, "Write to this file instead of stdout");
        ls->callback([&]() {
            try {
                std::string text = backup::readable(bk_list_file, bk_list_tsv);
                if (bk_list_out.empty()) {
                    std::fwrite(text.data(), 1, text.size(), stdout);
                } else {
                    std::ofstream f(fs::u8path(bk_list_out), std::ios::binary);
                    if (!f) {
                        std::fprintf(stderr, "%s %s\n", i18n::t("warning_could_not_save"),
                                     bk_list_out.c_str());
                        std::exit(1);
                    }
                    f << text;
                    std::printf("%s %s\n", i18n::t("exported_to"), bk_list_out.c_str());
                }
            } catch (const std::exception &e) {
                std::fprintf(stderr, "Error: %s\n", e.what());
                std::exit(1);
            }
        });

        // `sub` by value: CLI11 runs this after the enclosing block has exited,
        // so capturing the block-local pointer by reference is a dangling read.
        sub->callback([&, sub]() {
            if (sub->got_subcommand("list")) return;
            backup::CreateOptions opts;
            opts.root             = bk_root;
            opts.overwrite        = bk_force;
            opts.include_accounts = !bk_no_accounts;
            std::string out = bk_file.empty() ? "streamer-backup.db" : bk_file;
            try {
                auto r = backup::create(out, opts);
                std::printf("%s %s\n", i18n::t("backup_written"), out.c_str());
                std::printf("  %d albums · %d tracks · %d files · %d assets · "
                            "%d accounts · %d history rows · %.1f MB\n",
                            r.albums, r.tracks, r.files, r.assets, r.accounts,
                            r.history_rows, r.bytes / 1048576.0);
                if (r.accounts > 0)
                    std::printf("%s\n", i18n::t("backup_token_warning"));
            } catch (const std::exception &e) {
                std::fprintf(stderr, "Error: %s\n", e.what());
                std::exit(1);
            }
        });
    }

    std::string rs_file, rs_dir, rs_quality;
    int rs_concurrency = 0;
    bool rs_dry_run = false, rs_no_config = false, rs_force_config = false;
    {
        auto *sub = app.add_subcommand("restore", i18n::t("cmd_restore"));
        sub->add_option("file", rs_file, "Backup file to restore from")->required();
        sub->add_option("--dir", rs_dir,
            "Where to put the library (default: the directory recorded in the backup)");
        sub->add_option("-q,--quality", rs_quality,
            "Download everything at this quality instead of the recorded one");
        sub->add_option("-j,--concurrency", rs_concurrency, "Concurrent downloads");
        sub->add_flag("--dry-run", rs_dry_run, "Show what would be downloaded, download nothing");
        sub->add_flag("--no-config", rs_no_config, "Do not touch config.toml");
        sub->add_flag("--force-config", rs_force_config,
            "Overwrite an existing config that already has accounts");

        sub->callback([&]() {
            backup::RestoreOptions opts;
            opts.dir              = rs_dir;
            opts.quality_override = rs_quality;
            opts.concurrency      = rs_concurrency;
            opts.apply_config     = !rs_no_config;
            opts.force_config     = rs_force_config;
            opts.dry_run          = rs_dry_run;

            try {
                auto r = backup::restore(rs_file, opts,
                    [&](const std::string &line, int done, int total) {
                        std::printf("[%d/%d] %s\n", done, total, line.c_str());
                        std::fflush(stdout);
                    });

                std::printf("\n%s %s\n", i18n::t("restore_root"), r.root.c_str());
                if (r.config_written)  std::printf("%s\n", i18n::t("restore_config_written"));
                if (r.catalog_written) std::printf("%s\n", i18n::t("restore_catalog_written"));
                std::printf("  %d %s · %d %s · %d %s · %d %s\n",
                            r.planned,    i18n::t("restore_planned"),
                            r.skipped,    i18n::t("restore_skipped"),
                            r.downloaded, i18n::t("restore_downloaded"),
                            r.failed,     i18n::t("restore_failed"));
                if (r.no_account > 0)
                    std::printf("%s (%d)\n", i18n::t("restore_no_account"), r.no_account);
                if (!r.failures_path.empty())
                    std::printf("%s %s\n", i18n::t("restore_failures_at"),
                                r.failures_path.c_str());
                if (r.failed > 0) std::exit(1);
            } catch (const std::exception &e) {
                std::fprintf(stderr, "Error: %s\n", e.what());
                std::exit(1);
            }
        });
    }

    // ── refresh-credentials ───────────────────────────────────────────────────
    // Manual escape hatch. Downloads already heal a rotated app_secret on
    // their own (QobuzApiService::get_track_file_url), so this exists for
    // forcing the issue or repairing a config out of band.
    std::string rc_country;
    int rc_account = -1, rc_track = 0;
    {
        auto *sub = app.add_subcommand("refresh-credentials",
                                       i18n::t("cmd_refresh_credentials"));
        sub->add_option("--country", rc_country, "Account country code");
        sub->add_option("--account", rc_account, "Account index (GUI)");
        sub->add_option("--track",   rc_track,
            "Track ID to validate the new secret against (default: first search hit)");

        sub->callback([&]() {
            config::Config cur = config::load();
            config::Account acct;
            auto svc = acquire_service(cur, rc_country, rc_account, acct);

            // The bundle carries one seed per timezone and only one of them
            // signs correctly, so a candidate is only trustworthy once it has
            // produced a working getFileUrl. Search is unsigned, so it still
            // works while the stored secret is dead.
            std::int64_t probe = rc_track;
            if (probe <= 0) {
                auto hits = svc.search_tracks("music", 1, std::nullopt);
                if (hits.ok() && hits.value().items && !hits.value().items->empty()) {
                    const auto &first = hits.value().items->front();
                    if (first && first->id) probe = *first->id;
                }
            }
            if (probe <= 0) {
                std::fprintf(stderr, "%s\n", i18n::t("creds_no_probe_track"));
                std::exit(1);
            }

            auto res = svc.refresh_app_credentials(probe, kb::quality::MP3_320);
            if (!res.ok()) {
                std::fprintf(stderr, "%s %s\n", i18n::t("creds_refresh_failed"),
                             res.error().message.c_str());
                std::exit(1);
            }

            // The service's listener already wrote config.toml.
            std::string secret = svc.app_secret();
            std::printf("%s\n  app_id     = %s\n  app_secret = %s\n",
                        i18n::t("creds_refreshed"), svc.app_id().c_str(),
                        (secret.substr(0, 6) + "…" +
                         secret.substr(secret.size() >= 4 ? secret.size() - 4 : 0)).c_str());
        });
    }

    CLI11_PARSE(app, argc, argv);
    return 0;
}
