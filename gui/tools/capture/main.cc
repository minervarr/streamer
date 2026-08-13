// streamer_gui_capture — headless, native-resolution UI screenshot tool.
// Debug-only dev tooling (see gui/CMakeLists.txt), Linux-only for now, same
// role as Matrix_Player's matrix_ui_capture: a standalone binary, built
// against the app's own controllers/views, that renders the app's real
// screens off-screen via VK_EXT_headless_surface at an exact requested WxH —
// a genuine relayout at that resolution, not the interactive window's size
// upscaled. It never runs alongside the interactive streamer_gui process.
//
// With no --screen given it captures every screen (search + library +
// settings) in one run, same convention as the framework's own Scenario-list
// capture_main — "capture everything" needs no per-screen bookkeeping from
// the caller.
//
//   streamer_gui_capture --frame 7680x4320
//     -> ./ui-capture/search.png, library.png, settings.png
//
//   streamer_gui_capture --frame 7680x4320 --out shots --screen search
//                         [--query 'artist:"Luísa Sonza" year:2020-2024']
//
// --library-root points the library screen at a tree other than the
// configured download dir; --library-select selects the first two albums, and
// --library-confirm additionally raises the delete confirmation — the states
// worth eyeballing that a one-shot capture cannot otherwise reach.

#include "account_pool.hh"
#include "config.hh"
#include "fonts.hh"
#include "library_controller.hh"
#include "library_view.hh"
#include "search_controller.hh"
#include "settings_controller.hh"
#include "theme.hh"
#include "views.hh"

#include "canvas.hh"
#include "headless.hh"
#include "renderer.hh"
#include "widgets.hh"
#include "wayland_platform.hh"  // FileAssetReader — no display/compositor needed

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Draws `screen` ("search" or "settings") into a fresh Canvas at the given
// Renderer's native size, submits it, and reads the pixels back.
bool captureScreen(Renderer& renderer, const std::string& screen,
                   search::SearchController& searchCtl,
                   const widgets::TextFieldState& queryField,
                   settings::SettingsController& settingsCtl,
                   widgets::TextFieldState settingsFields[gui::FieldCount],
                   libmgr::LibraryController& libraryCtl, gui::CoverCache& covers,
                   const std::vector<std::string_view>& countryOptions,
                   std::vector<uint8_t>& rgba, uint32_t& outW, uint32_t& outH) {
    float w = (float)renderer.width(), h = (float)renderer.height();
    float rowH = h * 0.045f;

    std::vector<float> curves, shapeVerts, msdfQuads;
    // Album art: the library screen is the only consumer of the image layer,
    // and it draws nothing without somewhere to put its ImageDraws. It draws
    // into the FOREGROUND layer — see draw_library on why.
    std::vector<ImageDraw> images, imagesFg;

    auto build = [&] {
        curves.clear(); shapeVerts.clear(); msdfQuads.clear();
        images.clear(); imagesFg.clear();

        Canvas canvas(curves, renderer.width(), renderer.height(),
                     g_font_ok ? &g_font : nullptr, 0, 0, 0, 0);
        canvas.useShapes(&shapeVerts);
        canvas.useImages(&images);
        canvas.useImagesFg(&imagesFg);
        if (g_text_ok) canvas.useMsdf(&g_text, &msdfQuads);
        canvas.clear(theme::kBackground);

        std::vector<gui::Hit> hits;
        Rect navRow = {w * 0.04f, h * 0.02f, w * 0.3f, rowH};
        int navIdx = screen == "search" ? 0 : screen == "library" ? 1 : 2;
        widgets::drawSegmented(canvas, navRow, {"Search", "Library", "Settings"}, navIdx,
                               theme::kSegmented);
        Rect area = {w * 0.04f, navRow.y + navRow.h + h * 0.02f, w * 0.92f,
                   h * 0.94f - navRow.h - h * 0.02f};

        gui::PointerState ptr{};
        gui::TableInteraction table;
        if (screen == "settings") {
            gui::draw_settings(canvas, area, settingsCtl, settingsFields, /*focusedField=*/-1,
                               /*accountListHover=*/-1, /*hoveredAction=*/-1, /*pointerDown=*/false,
                               rowH, hits);
        } else if (screen == "library") {
            // No frame budget in a one-shot capture: every visible cover must
            // load in this single frame, or the screenshot shows placeholders for
            // art that exists. Raised BEFORE beginFrame(), which is what actually
            // stocks the per-frame allowance.
            covers.loadsPerFrame = 4096;
            covers.beginFrame();
            gui::draw_library(canvas, area, libraryCtl, covers, /*scrollPx=*/0.0f, rowH,
                              /*hoveredAction=*/-1, /*pointerDown=*/false, hits);
        } else {
            gui::draw_search(canvas, area, searchCtl, queryField, /*queryFocused=*/false,
                             /*typePickerIndex=*/0, /*tableScrollPx=*/0.0f, rowH,
                             /*hoverRow=*/-1, /*hoverHeaderCol=*/-1, /*hoveredAction=*/-1,
                             ptr, /*dtSeconds=*/0.0f, table,
                             countryOptions, /*countryPickerIndex=*/0, hits);
        }
    };

    // The live app resolves a glyph it did not have on the NEXT frame; a
    // capture has no next frame, so it would screenshot exactly the missing
    // text this change exists to fix.
    //
    // It takes several rounds, not one: a glyph that was missing had no
    // advance either, so filling it moves every glyph after it on the line —
    // to a new subpixel phase, which is its own cell, which is a new miss.
    // Each round is strictly smaller (64 cells, then 31, then 17, ...) and
    // converges in about five.
    //
    // The loop ALWAYS ends with a build, never with a bake: quads emitted
    // before a bake describe an atlas layout that the bake has moved on from,
    // and the letters baked last are exactly the ones that come out blank.
    build();
    for (int pass = 0; pass < 12 && g_text.hasMisses(); ++pass) {
        bake_glyph_misses(renderer);
        build();
    }

    renderer.draw(curves, /*overlay_rotation_deg=*/0, images, imagesFg,
                 msdfQuads, shapeVerts);
    return renderer.readbackLastFrame(rgba, outW, outH);
}

}  // namespace

int main(int argc, char** argv) {
    uint32_t frame_w = 0, frame_h = 0;
    std::string out_dir = "ui-capture";
    std::vector<std::string> screens;  // empty = capture everything
    std::string query = "artist:\"Luísa Sonza\" year:2020-2024";
    std::string library_root;          // empty = the configured download dir
    bool library_confirm = false;
    bool library_select = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            ++i;
            if (std::sscanf(argv[i], "%ux%u", &frame_w, &frame_h) != 2) {
                std::fprintf(stderr, "[x] --frame wants WxH, e.g. 7680x4320\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "settings") == 0) screens = {"settings"};
            else if (std::strcmp(argv[i], "search") == 0) screens = {"search"};
            else if (std::strcmp(argv[i], "library") == 0) screens = {"library"};
            else {
                std::fprintf(stderr, "[x] --screen wants 'search', 'library' or 'settings'\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            query = argv[++i];
        } else if (std::strcmp(argv[i], "--library-root") == 0 && i + 1 < argc) {
            library_root = argv[++i];
        } else if (std::strcmp(argv[i], "--library-confirm") == 0) {
            library_confirm = true;
        } else if (std::strcmp(argv[i], "--library-select") == 0) {
            library_select = true;
        }
    }

    if (frame_w == 0 || frame_h == 0) {
        std::fprintf(stderr,
            "usage: streamer_gui_capture --frame WxH [--out DIR] "
            "[--screen search|library|settings] [--query STRING]\n"
            "       [--library-root DIR] [--library-select] [--library-confirm]\n"
            "  (no --screen: captures every screen into DIR; default DIR is ./ui-capture)\n");
        return 1;
    }
    if (screens.empty()) screens = {"search", "library", "settings"};

    if (!headless_surface_supported()) {
        std::fprintf(stderr, "[x] VK_EXT_headless_surface not supported by this ICD\n");
        return 1;
    }

    config::Config cfg = config::load();
    if (cfg.accounts.empty()) cfg.accounts.push_back({});
    // Same long-lived pool the real app builds (see gui_main.cc): the
    // controller picks and fails over per request rather than being bound to
    // one account for its lifetime.
    account::Pool accountPool(cfg);

    FileAssetReader assets;
    HeadlessSurfaceProvider surface(frame_w, frame_h);
    Renderer renderer(surface, assets, /*desiredSwapchainImages=*/1);

    std::string stale_msdf_cache = (config::config_path().parent_path() / "msdf.cache").string();
    init_fonts(assets, stale_msdf_cache);

    // The same sizes gui_main.cc bakes, from the same type scale, so a capture
    // exercises the cells the live app would.
    auto glyphSizes = [&] {
        theme::TypeScale ts = theme::TypeScale::fromHeight((float)renderer.height());
        std::vector<int> sizes = {(int)(ts.caption + 0.5f), (int)(ts.small + 0.5f),
                                  (int)(ts.body + 0.5f),    (int)(ts.title + 0.5f),
                                  (int)(ts.display + 0.5f)};
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        return sizes;
    };
    // Mirrors gui_main.cc's glyph bake, so a --query with non-Latin text
    // renders the same as it would live instead of going blank in the capture.
    refresh_glyphs(renderer, glyphSizes(), {query});

    search::SearchController searchCtl(accountPool);
    widgets::TextFieldState queryField;
    queryField.text = query;
    queryField.cursorByte = query.size();
    searchCtl.search(query, "smart");

    settings::SettingsController settingsCtl;
    widgets::TextFieldState settingsFields[gui::FieldCount];
    const config::Account& a = settingsCtl.current_account();
    settingsFields[gui::FieldDownloadDir].text = settingsCtl.config().settings.download_dir.string();
    settingsFields[gui::FieldCountry].text = a.country;
    settingsFields[gui::FieldEmail].text = a.email;
    settingsFields[gui::FieldAppId].text = a.app_id;
    settingsFields[gui::FieldAppSecret].text = a.app_secret;
    settingsFields[gui::FieldUserId].text = a.user_id;
    settingsFields[gui::FieldAuthToken].text = a.auth_token;

    libmgr::LibraryController libraryCtl(
        library_root.empty() ? settingsCtl.config().settings.download_dir.string()
                             : library_root);
    libraryCtl.reload();
    {
        // Album/artist names can be non-Latin just like a search result's,
        // and so can the columns of the results the search above returned.
        std::vector<std::string> scan;
        for (const auto& al : libraryCtl.albums()) {
            scan.push_back(al.title);
            scan.push_back(al.artist_name);
        }
        for (const auto& r : searchCtl.results()) {
            scan.push_back(r.title);
            scan.push_back(r.artist);
            scan.push_back(r.album);
            scan.push_back(r.label);
            scan.push_back(r.genre);
        }
        refresh_glyphs(renderer, glyphSizes(), scan);
    }
    if (library_confirm || library_select) {
        libraryCtl.toggle_selected(0);
        libraryCtl.toggle_selected(1);
        if (library_confirm) libraryCtl.request_delete();
    }
    gui::CoverCache covers(&renderer);
    covers.budget = 4096;   // a capture shows everything at once; nothing to evict

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);

    // Mirrors gui_main.cc's picker: Auto, All, then one entry per account, so
    // a capture shows the region control exactly as the live app draws it.
    std::vector<std::string> countryOptions{"Auto", "All"};
    for (const config::Account& a : cfg.accounts)
        countryOptions.push_back(a.country.empty() ? "?" : a.country);
    const std::vector<std::string_view> countryOptionViews(countryOptions.begin(),
                                                           countryOptions.end());

    for (const auto& screen : screens) {
        std::vector<uint8_t> rgba;
        uint32_t outW = 0, outH = 0;
        if (!captureScreen(renderer, screen, searchCtl, queryField, settingsCtl, settingsFields,
                           libraryCtl, covers, countryOptionViews, rgba, outW, outH)) {
            std::fprintf(stderr, "[x] readback failed for %s\n", screen.c_str());
            return 1;
        }
        std::string path = (std::filesystem::path(out_dir) / (screen + ".png")).string();
        if (!stbi_write_png(path.c_str(), (int)outW, (int)outH, 4, rgba.data(), (int)outW * 4)) {
            std::fprintf(stderr, "[x] failed to write %s\n", path.c_str());
            return 1;
        }
        std::printf("captured %ux%u to %s\n", outW, outH, path.c_str());
    }
    return 0;
}
