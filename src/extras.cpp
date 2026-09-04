#include "extras.hh"
#include "i18n.hh"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>
#include <arc/http.hh>
#include <api/service.hh>
#include <api/requests.hh>

namespace fs = std::filesystem;

namespace extras {

// Download url to dest using the CDN client; skip if dest already exists.
static void download_file(const arc::HttpClient &client,
                          const std::string &url,
                          const fs::path &dest,
                          const char *label)
{
    if (fs::exists(dest)) return;
    auto res = client.download_to_file(url, dest.u8string());
    if (!res.ok())
        std::fprintf(stderr, "%s %s: %s\n", i18n::t("warning_could_not_download"),
                     label, res.error().message.c_str());
    else
        std::printf("%s %s\n", i18n::t("saved"), dest.u8string().c_str());
}

static void save_text(const std::string &text, const fs::path &dest) {
    if (fs::exists(dest)) return;
    std::ofstream f(dest);
    if (!f) {
        std::fprintf(stderr, "%s %s\n", i18n::t("warning_could_not_save"),
                     dest.u8string().c_str());
        return;
    }
    f << text;
    std::printf("%s %s\n", i18n::t("saved"), dest.u8string().c_str());
}

// Rewrite "…_600.jpg" → "…_org.jpg" to get full-resolution cover.
static std::string best_cover_url(const std::string &url) {
    auto pos = url.rfind("600");
    if (pos == std::string::npos) return url;
    return url.substr(0, pos) + "org" + url.substr(pos + 3);
}

void save_album_extras(const kb::QobuzApiService &svc,
                       const std::string &album_id,
                       const fs::path &album_dir)
{
    auto tok_r = svc.require_auth_token();
    if (!tok_r.ok()) return;
    auto auth = svc.request_auth(tok_r.value());

    kb::api::Params params = {{"album_id", album_id}};
    auto raw = kb::api::signed_get_raw(svc.http_client(), svc.base_url(),
                                       "/album/get", std::move(params), auth);
    if (!raw.ok()) {
        std::fprintf(stderr, "%s %s\n", i18n::t("warning_album_extras"),
                     raw.error().message.c_str());
        return;
    }

    auto j = nlohmann::json::parse(raw.value(), nullptr, false);
    if (j.is_discarded()) return;

    // Album description
    if (j.contains("description") && j["description"].is_string()) {
        std::string desc = j["description"].get<std::string>();
        if (!desc.empty())
            save_text(desc, album_dir / "album_description.txt");
    }

    // PDF booklet (goodies array)
    if (j.contains("goodies") && j["goodies"].is_array()) {
        for (auto &g : j["goodies"]) {
            std::string url_val = g.value("original_url", g.value("url", std::string{}));
            bool is_pdf = (g.value("file_format_id", 0) == 21)
                       || (!url_val.empty() && url_val.rfind(".pdf") == url_val.size() - 4);
            if (is_pdf && !url_val.empty()) {
                download_file(svc.cdn_client(), url_val,
                              album_dir / "booklet.pdf", "booklet");
                break;
            }
        }
    }
}

void save_artist_extras(const kb::QobuzApiService &svc,
                        int artist_id,
                        const fs::path &artist_dir)
{
    auto tok_r = svc.require_auth_token();
    if (!tok_r.ok()) return;
    auto auth = svc.request_auth(tok_r.value());

    kb::api::Params params = {{"artist_id", std::to_string(artist_id)}};
    auto raw = kb::api::signed_get_raw(svc.http_client(), svc.base_url(),
                                       "/artist/get", std::move(params), auth);
    if (!raw.ok()) {
        std::fprintf(stderr, "%s %s\n", i18n::t("warning_artist_extras"),
                     raw.error().message.c_str());
        return;
    }

    auto j = nlohmann::json::parse(raw.value(), nullptr, false);
    if (j.is_discarded()) return;

    // Artist bio
    if (j.contains("biography") && j["biography"].is_object()) {
        auto &bio = j["biography"];
        std::string text = bio.value("text", bio.value("summary", std::string{}));
        if (!text.empty())
            save_text(text, artist_dir / "artist_bio.txt");
    }

    // Artist image: try image.mega → extralarge → large → medium
    if (j.contains("image") && j["image"].is_object()) {
        auto &img = j["image"];
        for (const char *key : {"mega", "extralarge", "large", "medium"}) {
            if (img.contains(key) && img[key].is_string()) {
                std::string url_val = img[key].get<std::string>();
                if (!url_val.empty()) {
                    download_file(svc.cdn_client(), url_val,
                                  artist_dir / "artist.jpg", "artist image");
                    break;
                }
            }
        }
    }
}

} // namespace extras
