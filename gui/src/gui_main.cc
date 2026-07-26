// streamer GUI — portable skeleton. Owns the dirty-flag frame loop; the
// per-OS AppHost (host.hh, os/wayland_host.cc today) owns windows/input pump.
//
// Search screen is the default mode: query text field -> SearchController,
// sortable results table, cheatsheet panel, in-process download dispatch.
// Click/hover dispatch follows the engine's established idiom (scanersito's
// gui_main.cc): match the PREVIOUS frame's Hit rects against THIS frame's
// pointer state, then draw fresh Hit rects for the next iteration — this
// avoids needing draw() to return geometry before it has computed it.

#include "config.hh"
#include "download.hh"
#include "host.hh"
#include "query_dsl.hh"
#include "search_controller.hh"
#include "theme.hh"
#include "views.hh"

#include "canvas.hh"
#include "font.hh"
#include "frame_input.hh"
#include "keys.hh"
#include "msdf.hh"
#include "widgets.hh"

#include <api/service.hh>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// Headless smoke-test entry point (scanersito's convention): exercises
// pure-logic pieces with no window/GPU/network. query_dsl assertions cover
// every operator and field, including the exact accent-preservation
// scenario that motivated this port (see query_dsl.hh) — typing "Luísa
// Sonza" must match a row for "Luísa Sonza" without corruption, and an
// ALL-CAPS accented query must still fold correctly against a lowercase
// accented row.
int g_fail_count = 0;

void check(bool cond, const char* what) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail_count; }
}

int run_selftest() {
    using namespace query_dsl;

    SearchResult luisa;
    luisa.title = "Chico Não Vou Fazer Nada";
    luisa.artist = "Luísa Sonza";
    luisa.type = "track";
    luisa.year = 2023;
    luisa.duration = 195;
    luisa.hires = false;

    // Bare term, accent preserved through tokenizer + case-fold.
    check(Match(Parse("Sonza"), luisa), "bare term 'Sonza' matches artist");
    check(Match(Parse("Luísa Sonza"), luisa), "bare terms 'Luísa Sonza' match (accent intact)");
    check(Match(Parse("LUÍSA"), luisa), "uppercase accented 'LUÍSA' folds to match 'Luísa'");
    check(!Match(Parse("Shakira"), luisa), "unrelated term does not match");

    // Quoted filter value, accent preserved.
    check(Match(Parse("artist:\"Luísa Sonza\""), luisa), "artist:\"Luísa Sonza\" matches");
    check(!Match(Parse("artist:\"Anitta\""), luisa), "artist:\"Anitta\" does not match");

    // Numeric range and comparison.
    check(Match(Parse("year:2020-2024"), luisa), "year:2020-2024 matches 2023");
    check(!Match(Parse("year:2000-2010"), luisa), "year:2000-2010 does not match 2023");
    check(Match(Parse("duration:>100"), luisa), "duration:>100 matches 195");
    check(!Match(Parse("duration:<100"), luisa), "duration:<100 does not match 195");

    // Boolean AND (implicit)/OR/NOT.
    check(Match(Parse("sonza year:2023"), luisa), "implicit AND: sonza year:2023");
    check(!Match(Parse("sonza year:1999"), luisa), "implicit AND fails on wrong year");
    check(Match(Parse("type:album or type:track"), luisa), "OR: type:album or type:track");
    check(Match(Parse("not hires:true"), luisa), "NOT: not hires:true (hires is false)");
    check(!Match(Parse("not sonza"), luisa), "NOT: not sonza excludes a matching row");

    // Base term / type hint extraction (used to build the API query).
    check(ExtractBaseTerm(Parse("artist:\"Luísa Sonza\" year:2023")) == "Luísa Sonza",
         "ExtractBaseTerm picks the artist filter value");
    check(ExtractTypeHint(Parse("type:album sonza")) == "album",
         "ExtractTypeHint reads type: filter");
    check(ExtractTypeHint(Parse("sonza")).empty(), "ExtractTypeHint empty with no type: filter");

    // ── Keystroke pipeline: CharEvent codepoint -> FrameInput -> the search
    // box's actual text buffer. This is the path a real user's typing goes
    // through — the previous assertions above prove Match/Parse handle
    // accented strings correctly, but not that typing them in produces the
    // same bytes. "Luísa Sonza" has both a 2-byte UTF-8 codepoint (í,
    // U+00ED) and a plain-ASCII run, so this exercises the UTF-8-boundary-
    // safe insert/backspace/cursor-move logic in textFieldHandleInput, not
    // just an already-correct hardcoded std::string.
    {
        widgets::TextFieldState field;
        FrameInput in;
        auto type = [&](const std::u32string& s) {
            in.beginFrame();
            for (char32_t cp : s) in.onChar(CharEvent{(uint32_t)cp});
            widgets::textFieldHandleInput(field, in);
        };
        type(U"Lu");
        type(U"í");
        type(U"sa Sonza");
        check(field.text == "Luísa Sonza", "typed codepoints assemble to correct UTF-8 bytes");
        check(field.cursorByte == field.text.size(), "cursor ends at the buffer's end");

        // Backspace after 'í' must remove the whole 2-byte sequence, not one
        // byte (which would leave a dangling continuation byte / mojibake).
        widgets::TextFieldState field2;
        auto type2 = [&](const std::u32string& s) {
            in.beginFrame();
            for (char32_t cp : s) in.onChar(CharEvent{(uint32_t)cp});
            widgets::textFieldHandleInput(field2, in);
        };
        type2(U"Luí");
        in.beginFrame();
        in.onKey(KeyEvent{key::Backspace, true});
        widgets::textFieldHandleInput(field2, in);
        check(field2.text == "Lu", "backspace after an accented char removes the whole codepoint");
        check(field2.cursorByte == 2, "cursor lands after 'u', not mid-codepoint");
    }

    if (g_fail_count == 0) {
        std::printf("selftest: ok (%d assertions)\n", 19);
        return 0;
    }
    std::fprintf(stderr, "selftest: %d assertion(s) failed\n", g_fail_count);
    return 1;
}

// ── Dev-only fake data for --widget-preview (Phase 4's sortable-table check) ─

const std::vector<widgets::TableColumn>& previewColumns() {
    static const std::vector<widgets::TableColumn> cols = {
        {"Title", 2.2f}, {"Artist", 1.6f}, {"Label", 1.4f}, {"Date", 0.9f},
        {"Duration", 0.8f}, {"Genre", 1.1f}, {"Hi-Res", 0.7f}, {"Explicit", 0.8f},
        {"Type", 0.8f}, {"Country", 0.9f},
    };
    return cols;
}

std::string previewCell(int row, int col) {
    static const char* titles[] = {"Chico Não Vou Fazer Nada", "Cheguei", "Escândalo Íntimo",
                                   "Serena", "Anaconda", "Fallin'", "Doçura", "Boa Menina",
                                   "Modo Turbo", "Penhasco 2"};
    static const char* artists[] = {"Luísa Sonza", "MC Kevin", "Anitta", "Ivete Sangalo",
                                    "Marília Mendonça", "Ana Castela", "Pabllo Vittar",
                                    "Manu Bahtidão", "Wiu", "Djonga"};
    switch (col) {
        case 0: return titles[row % 10];
        case 1: return artists[row % 10];
        case 2: return "Warner Music";
        case 3: return std::to_string(2018 + (row % 7));
        case 4: return std::to_string(150 + row * 7);
        case 5: return "Pop";
        case 6: return (row % 3 == 0) ? "yes" : "no";
        case 7: return (row % 5 == 0) ? "yes" : "no";
        case 8: return "track";
        case 9: return "BR";
        default: return "";
    }
}

// ── Fonts (same pipeline as scanersito's gui_main.cc) ────────────────────────

Font     g_font;
bool     g_font_ok = false;
MsdfFont g_msdf;
bool     g_msdf_ok = false;

constexpr const char* kFontRegular = "fonts/NewCM10-Book.otf";
constexpr const char* kFontBold    = "fonts/NewCM10-Bold.otf";
constexpr const char* kFontItalic  = "fonts/NewCM10-BookItalic.otf";

void init_fonts(AssetReader& assets, const std::string& cache) {
    std::vector<uint8_t> bytes;
    if (assets.read(kFontRegular, bytes))
        g_font_ok = g_font.loadFromMemory(bytes.data(), bytes.size());

    if (g_msdf.generate(assets, kFontRegular, cache.c_str())) {
        bool added = false;
        if (!g_msdf.hasStyle(FontStyle::Bold)) {
            g_msdf.ensureAtlasLoaded(cache.c_str());
            added |= g_msdf.addStyle(assets, kFontBold, FontStyle::Bold);
        }
        if (!g_msdf.hasStyle(FontStyle::Italic)) {
            g_msdf.ensureAtlasLoaded(cache.c_str());
            added |= g_msdf.addStyle(assets, kFontItalic, FontStyle::Italic);
        }
        if (added) g_msdf.saveCache(cache.c_str());
        g_msdf_ok = g_msdf.valid();
    }
    if (!g_msdf_ok)
        std::fprintf(stderr, "[!] MSDF unavailable — curve/stroke text only\n");
}

void upload_msdf(Renderer& r, const std::string& cache) {
    if (!g_msdf_ok) return;
    g_msdf.ensureAtlasLoaded(cache.c_str());
    r.initMsdf(g_msdf);
    g_msdf.releaseAtlasPixels();
}

// Fire-and-forget background download: mirrors the CLI's `streamer download`
// dispatch (src/download.hh's dl::run), off the render thread so a slow
// transfer never blocks the UI. No progress UI in this phase — a Downloads
// screen is future work; this proves the in-process, no-subprocess wiring.
void download_async(const config::Account& account, std::string quality,
                    std::string download_dir, std::string country,
                    std::vector<std::string> ids) {
    std::thread([account, quality, download_dir, country, ids]() {
        kb::QobuzApiService::Config cfg;
        cfg.app_id = account.app_id;
        cfg.app_secret = account.app_secret;
        auto res = kb::QobuzApiService::with_credentials(cfg);
        if (!res.ok()) {
            std::fprintf(stderr, "[download] %s\n", res.error().message.c_str());
            return;
        }
        auto svc = res.take();
        if (!account.auth_token.empty())
            svc.login_with_token(account.user_id, account.auth_token);
        for (auto& id : ids) {
            bool ok = dl::run(svc, id, quality, download_dir, country, 1, true, true);
            std::fprintf(stderr, "[download] %s: %s\n", id.c_str(), ok ? "done" : "failed");
        }
    }).detach();
}

} // namespace

int main(int argc, char** argv) {
    bool theme_preview = false;
    bool widget_preview = false;
    bool demo = false;
    const char* capture_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) return run_selftest();
        if (std::strcmp(argv[i], "--theme-preview") == 0) theme_preview = true;
        if (std::strcmp(argv[i], "--widget-preview") == 0) widget_preview = true;
        if (std::strcmp(argv[i], "--demo") == 0) demo = true;
        if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture_path = argv[++i];
    }

    auto host = gui::make_host();
    if (!host || !host->init()) {
        std::fprintf(stderr, "[x] failed to initialize GUI host\n");
        return 1;
    }

    config::Config cfg = config::load();
    if (cfg.accounts.empty()) cfg.accounts.push_back({});
    const config::Account& account = cfg.accounts.front();

    std::string msdf_cache = (config::config_path().parent_path() / "msdf.cache").string();
    init_fonts(host->assets(), msdf_cache);
    upload_msdf(host->renderer(), msdf_cache);

    search::SearchController searchCtl(account);
    widgets::TextFieldState queryField;
    int typePickerIndex = 0;
    float tableScrollPx = 0.0f;

    if (demo) {
        // Dev tooling only (visual verification, not shipped functionality):
        // types an accented query through the real CharEvent->FrameInput->
        // textFieldHandleInput pipeline, opens the cheatsheet, and submits a
        // search — proving accented text survives end-to-end and the error
        // path (no account configured in this environment) renders cleanly.
        FrameInput synth;
        synth.beginFrame();
        for (char32_t cp : std::u32string(U"artist:\"Luísa Sonza\" year:2020-2024"))
            synth.onChar(CharEvent{(uint32_t)cp});
        widgets::textFieldHandleInput(queryField, synth);
        searchCtl.toggle_cheatsheet();
        searchCtl.search(queryField.text, "smart");
    }

    FrameInput input;
    bool first_frame = true;
    std::vector<gui::Hit> hits;  // filled by the PREVIOUS frame's draw

    while (!host->quit_requested()) {
        input.beginFrame();
        host->pump(/*timeout_ms=*/1000, input);

        bool dirty = host->take_dirty();
        bool interacted = input.pointerWentDown || input.wheelDelta != 0.0f ||
                          !input.typedCodepoints.empty() || !input.keysWentDown.empty();
        if (!dirty && !interacted && !first_frame) continue;
        first_frame = false;

        Renderer& r = host->renderer();
        float w = (float)r.width(), h = (float)r.height();
        float rowH = h * 0.045f;

        // ── Dispatch against LAST frame's hits before drawing this frame ────
        int hoverRow = -1, hoverHeaderCol = -1;
        for (auto& hit : hits) {
            if (!hit.rect.contains(input.pointerX, input.pointerY)) continue;
            if (hit.action >= gui::ActTableHeaderBase && hit.action < gui::ActTableRowBase)
                hoverHeaderCol = hit.action - gui::ActTableHeaderBase;
            else if (hit.action >= gui::ActTableRowBase)
                hoverRow = hit.action - gui::ActTableRowBase;

            if (!input.pointerWentDown) continue;
            if (hit.action == gui::ActToggleCheatsheet) {
                searchCtl.toggle_cheatsheet();
            } else if (hit.action == gui::ActSubmitSearch) {
                searchCtl.search(queryField.text, std::string(gui::kTypePickerOptions[(size_t)typePickerIndex]));
                tableScrollPx = 0.0f;
            } else if (hit.action >= gui::ActTypePickerBase && hit.action < gui::ActTableHeaderBase) {
                typePickerIndex = hit.action - gui::ActTypePickerBase;
            } else if (hit.action >= gui::ActTableHeaderBase && hit.action < gui::ActTableRowBase) {
                auto sc = gui::sortColumnForTableIndex(hit.action - gui::ActTableHeaderBase);
                if (sc != search::SortColumn::None) searchCtl.sort(sc);
            } else if (hit.action >= gui::ActTableRowBase) {
                searchCtl.toggle_selected(hit.action - gui::ActTableRowBase);
            } else if (hit.action == gui::ActDownloadSelected && !searchCtl.selected().empty()) {
                std::vector<std::string> ids;
                for (int idx : searchCtl.selected()) ids.push_back(searchCtl.results()[(size_t)idx].id);
                download_async(account, cfg.settings.quality, cfg.settings.download_dir.string(),
                              account.country, ids);
                searchCtl.clear_selection();
            }
        }
        // Enter submits the search regardless of pointer position.
        if (input.keyWentDown(key::Enter)) {
            searchCtl.search(queryField.text, std::string(gui::kTypePickerOptions[(size_t)typePickerIndex]));
            tableScrollPx = 0.0f;
        }
        widgets::textFieldHandleInput(queryField, input);
        if (input.wheelDelta != 0.0f) {
            Rect area = {w * 0.04f, h * 0.03f, w * 0.92f, h * 0.94f};
            tableScrollPx += input.wheelDelta * rowH;
            float maxScroll = std::max(0.0f, (float)searchCtl.results().size() * rowH - area.h * 0.5f);
            tableScrollPx = std::clamp(tableScrollPx, 0.0f, maxScroll);
        }

        std::vector<float> curves;
        std::vector<float> shapeVerts;
        std::vector<float> msdfQuads;
        Canvas canvas(curves, r.width(), r.height(), g_font_ok ? &g_font : nullptr,
                      /*insetTop=*/0, /*insetBottom=*/0,
                      /*insetLeft=*/0, /*insetRight=*/0);
        canvas.useShapes(&shapeVerts);
        if (g_msdf_ok) canvas.useMsdf(&g_msdf, &msdfQuads);
        canvas.clear(theme::kBackground);

        if (widget_preview) {
            static int sortCol = 1;
            static bool sortAsc = true;
            static float scrollPx = 0.0f;
            const auto& cols = previewColumns();
            const int rowCount = 24;
            Rect area = {w * 0.05f, h * 0.08f, w * 0.9f, h * 0.8f};
            float prowH = h * 0.05f;
            Rect header = widgets::tableHeaderRow(area, prowH);
            auto headerRects = widgets::tableHeaderColumnRects(header, cols);
            int hHover = -1;
            for (size_t i = 0; i < headerRects.size(); i++) {
                if (headerRects[i].contains(input.pointerX, input.pointerY)) {
                    hHover = (int)i;
                    if (input.pointerWentDown) {
                        if (sortCol == (int)i) sortAsc = !sortAsc;
                        else { sortCol = (int)i; sortAsc = true; }
                    }
                }
            }
            if (input.wheelDelta != 0.0f) {
                scrollPx += input.wheelDelta * prowH;
                float maxScroll = std::max(0.0f, rowCount * prowH - (area.h - prowH));
                scrollPx = std::clamp(scrollPx, 0.0f, maxScroll);
            }
            int rHover = -1;
            Rect body = {area.x, area.y + prowH, area.w, area.h - prowH};
            if (body.contains(input.pointerX, input.pointerY))
                rHover = (int)((input.pointerY - body.y + scrollPx) / prowH);
            widgets::drawSortableTable(canvas, area, cols, previewCell, rowCount,
                                       sortCol, sortAsc, scrollPx, prowH, rHover, hHover);
        } else if (theme_preview) {
            float bw = w * 0.18f, bh = h * 0.08f, gap = w * 0.02f;
            float x = w * 0.05f, y = h * 0.08f;
            for (int i = 0; i < 4; ++i)
                theme::accentButton(canvas, x + i * (bw + gap), y, bw, bh, "Button", bh * 0.25f);
            y += bh + h * 0.05f;
            theme::gradientRect(canvas, x, y, w * 0.9f, h * 0.04f, h * 0.02f);
        } else {
            Rect area = {w * 0.04f, h * 0.03f, w * 0.92f, h * 0.94f};
            hits.clear();
            gui::draw_search(canvas, area, searchCtl, queryField, /*queryFocused=*/true,
                             typePickerIndex, tableScrollPx, rowH,
                             hoverRow, hoverHeaderCol, hits);
        }

        r.draw(curves, /*overlay_rotation_deg=*/0, /*images=*/{}, /*foregroundImages=*/{},
              msdfQuads, shapeVerts);

        if (capture_path) {
            // Dev tooling only: dump the frame just drawn as raw RGBA8 —
            // this repo has no PNG encoder wired in yet, ffmpeg converts it.
            std::vector<uint8_t> rgba;
            uint32_t cw = 0, ch = 0;
            if (r.readbackLastFrame(rgba, cw, ch)) {
                FILE* f = std::fopen(capture_path, "wb");
                if (f) { std::fwrite(rgba.data(), 1, rgba.size(), f); std::fclose(f); }
                std::printf("captured %ux%u to %s\n", cw, ch, capture_path);
            }
            return 0;
        }
    }

    return 0;
}
