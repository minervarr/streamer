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
#include "service_factory.hh"
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
#include <chrono>
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
int g_check_count = 0;

void check(bool cond, const char* what) {
    ++g_check_count;
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

    // Diacritic-insensitive matching: a US/ASCII keyboard has no way to type
    // "í", so an unaccented "luisa sonza" must still find "Luísa Sonza".
    check(Match(Parse("Luisa Sonza"), luisa), "unaccented 'Luisa Sonza' matches accented 'Luísa Sonza'");
    check(Match(Parse("luisa"), luisa), "unaccented lowercase 'luisa' matches 'Luísa'");
    check(Match(Parse("artist:=Luisa Sonza"), luisa) == false,
         "artist:=Luisa Sonza (exact, unquoted) still splits into two terms — quoting note applies regardless of accents");
    check(Match(Parse("artist:=\"Luisa Sonza\""), luisa), "artist:=\"Luisa Sonza\" (exact, quoted, unaccented) matches accented 'Luísa Sonza'");

    // Quoted filter value, accent preserved.
    check(Match(Parse("artist:\"Luísa Sonza\""), luisa), "artist:\"Luísa Sonza\" matches");
    check(!Match(Parse("artist:\"Anitta\""), luisa), "artist:\"Anitta\" does not match");

    // Exact-match operator '=': field:value stays "contains", field:=value
    // is exact/case-insensitive — the bug this fixes is artist:"poppy"
    // matching both "Poppy" and "Poppy Ajudha" with no way to ask for just
    // the former.
    SearchResult poppy;
    poppy.title = "Poppy";
    poppy.artist = "Poppy";
    poppy.type = "artist";

    SearchResult poppyAjudha;
    poppyAjudha.title = "Trouble";
    poppyAjudha.artist = "Poppy Ajudha";
    poppyAjudha.type = "track";

    check(Match(Parse("artist:poppy"), poppy), "artist:poppy (contains) matches 'Poppy'");
    check(Match(Parse("artist:poppy"), poppyAjudha), "artist:poppy (contains) also matches 'Poppy Ajudha'");
    check(Match(Parse("artist:=poppy"), poppy), "artist:=poppy (exact) matches 'Poppy'");
    check(!Match(Parse("artist:=poppy"), poppyAjudha), "artist:=poppy (exact) does not match 'Poppy Ajudha'");
    check(Match(Parse("artist:=\"Luísa Sonza\""), luisa), "artist:=\"Luísa Sonza\" (exact, quoted) matches the full name");
    check(!Match(Parse("artist:=Luísa"), luisa), "artist:=Luísa (exact, partial) does not match 'Luísa Sonza'");

    // ── Multi-column stacked sort: CycleSortKey's 3-state-per-column cycle
    // (ascending -> descending -> removed), and ApplySort's stacked
    // comparator (earlier keys in `keys` take priority; later ones only
    // break ties). Pure functions — no SearchController/network needed.
    {
        using namespace search;

        std::vector<SortKey> keys;
        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 1 && keys[0].col == SortColumn::Artist && keys[0].asc,
             "1st click on Artist: appended, ascending");

        CycleSortKey(keys, SortColumn::Duration);
        check(keys.size() == 2 && keys[0].col == SortColumn::Artist &&
             keys[1].col == SortColumn::Duration && keys[1].asc,
             "1st click on Duration: appended as secondary, Artist stays primary");

        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 2 && keys[0].col == SortColumn::Artist && !keys[0].asc,
             "2nd click on Artist: flips to descending in place, Duration still secondary");

        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 1 && keys[0].col == SortColumn::Duration,
             "3rd click on Artist: removed from the stack, Duration promoted to primary");

        query_dsl::SearchResult ana, bea1, bea2;
        ana.artist = "Ana";  ana.duration = 300;
        bea1.artist = "Bea"; bea1.duration = 200;
        bea2.artist = "Bea"; bea2.duration = 100;
        std::vector<query_dsl::SearchResult> results = {bea1, ana, bea2};

        ApplySort(results, {{SortColumn::Artist, true}});
        check(results[0].artist == "Ana" && results[1].artist == "Bea" && results[2].artist == "Bea",
             "ApplySort: single key groups by Artist ascending");

        ApplySort(results, {{SortColumn::Artist, true}, {SortColumn::Duration, true}});
        check(results[0].artist == "Ana" &&
             results[1].artist == "Bea" && results[1].duration == 100 &&
             results[2].artist == "Bea" && results[2].duration == 200,
             "ApplySort: Artist stays primary, Duration breaks ties within the Bea group ascending");
    }

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
        std::printf("selftest: ok (%d assertions)\n", g_check_count);
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
                    int concurrency, std::vector<std::string> ids) {
    if (concurrency < 1) concurrency = 1;
    std::thread([account, quality, download_dir, country, concurrency, ids]() {
        auto res = qobuz::make_service(account);
        if (!res.ok()) {
            std::fprintf(stderr, "[download] %s\n", res.error().message.c_str());
            return;
        }
        auto svc = res.take();
        for (auto& id : ids) {
            bool ok = dl::run(svc, id, quality, download_dir, country, concurrency,
                              true, true);
            std::fprintf(stderr, "[download] %s: %s\n", id.c_str(), ok ? "done" : "failed");
        }
    }).detach();
}

} // namespace

int main(int argc, char** argv) {
    bool theme_preview = false;
    bool widget_preview = false;
    bool demo = false;
    bool start_settings = false;
    const char* capture_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) return run_selftest();
        if (std::strcmp(argv[i], "--theme-preview") == 0) theme_preview = true;
        if (std::strcmp(argv[i], "--widget-preview") == 0) widget_preview = true;
        if (std::strcmp(argv[i], "--demo") == 0) demo = true;
        if (std::strcmp(argv[i], "--settings") == 0) start_settings = true;
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
    kb::api::set_requests_per_minute(cfg.settings.requests_per_minute);

    std::string msdf_cache = (config::config_path().parent_path() / "msdf.cache").string();
    init_fonts(host->assets(), msdf_cache);
    upload_msdf(host->renderer(), msdf_cache);

    search::SearchController searchCtl(account);
    widgets::TextFieldState queryField;
    int typePickerIndex = 0;
    float tableScrollPx = 0.0f;

    settings::SettingsController settingsCtl;
    widgets::TextFieldState settingsFields[gui::FieldCount];
    settingsFields[gui::FieldDownloadDir].text = settingsCtl.config().settings.download_dir.string();
    int focusedField = -1;
    int loadedAccountIdx = -1;
    int accountListHover = -1;
    gui::TableInteraction tableInteraction;

    enum class Screen { Search, Settings };
    Screen activeScreen = start_settings ? Screen::Settings : Screen::Search;
    constexpr int kActNavSearch = 9000, kActNavSettings = 9001;

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
    auto lastFrameTime = std::chrono::steady_clock::now();
    float lastPointerX = 0.0f, lastPointerY = 0.0f;

    while (!host->quit_requested()) {
        input.beginFrame();
        host->pump(/*timeout_ms=*/1000, input);

        bool dirty = host->take_dirty();
        // Plain pointer motion (no click/wheel/key) never sets `dirty` — the
        // host only marks that on resize — so hover-only feedback (button
        // hover, table row hover, the ghost popup's hover timer) never
        // updated without an unrelated click first forcing a redraw. Treat a
        // moved pointer as its own reason to redraw.
        bool pointerMoved = input.pointerX != lastPointerX || input.pointerY != lastPointerY;
        lastPointerX = input.pointerX; lastPointerY = input.pointerY;
        bool interacted = input.pointerWentDown || input.wheelDelta != 0.0f ||
                          !input.typedCodepoints.empty() || !input.keysWentDown.empty() ||
                          pointerMoved;
        if (!dirty && !interacted && !first_frame) continue;
        first_frame = false;

        auto now = std::chrono::steady_clock::now();
        float dtSeconds = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;

        Renderer& r = host->renderer();
        float w = (float)r.width(), h = (float)r.height();
        float rowH = h * 0.045f;

        // ── Dispatch against LAST frame's hits before drawing this frame ────
        // (nav bar + whichever screen was active when those hits were built —
        // switching screens only takes effect below, after dispatch, so the
        // screen that drew `hits` last frame is still the right one to
        // interpret them against.)
        int hoverRow = -1, hoverHeaderCol = -1, hoveredAction = -1;
        for (auto& hit : hits) {
            if (!hit.rect.contains(input.pointerX, input.pointerY)) continue;
            hoveredAction = hit.action;
            if (activeScreen == Screen::Search) {
                if (hit.action >= gui::ActTableHeaderBase && hit.action < gui::ActTableRowBase)
                    hoverHeaderCol = hit.action - gui::ActTableHeaderBase;
                else if (hit.action >= gui::ActTableRowBase)
                    hoverRow = hit.action - gui::ActTableRowBase;
            } else if (hit.action >= gui::ActAccountListBase && hit.action < gui::ActQualityBase) {
                accountListHover = hit.action - gui::ActAccountListBase;
            }

            if (!input.pointerWentDown) continue;

            if (hit.action == kActNavSearch) { activeScreen = Screen::Search; continue; }
            if (hit.action == kActNavSettings) { activeScreen = Screen::Settings; continue; }

            if (activeScreen == Screen::Search) {
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
                    std::vector<std::string> ids(searchCtl.selected().begin(), searchCtl.selected().end());
                    download_async(account, settingsCtl.config().settings.quality,
                                  settingsCtl.config().settings.download_dir.string(),
                                  account.country,
                                  (int)settingsCtl.config().settings.concurrency, ids);
                    searchCtl.clear_selection();
                }
            } else {  // Screen::Settings
                if (hit.action >= gui::ActAccountListBase && hit.action < gui::ActQualityBase) {
                    settingsCtl.select_account(hit.action - gui::ActAccountListBase);
                } else if (hit.action == gui::ActAddAccount) {
                    settingsCtl.add_account();
                } else if (hit.action == gui::ActRemoveAccount) {
                    settingsCtl.remove_account(settingsCtl.current_account_index());
                } else if (hit.action == gui::ActLoginWithToken) {
                    settingsCtl.login_with_token(settingsFields[gui::FieldUserId].text,
                                                 settingsFields[gui::FieldAuthToken].text);
                } else if (hit.action == gui::ActExportAccounts) {
                    settingsCtl.export_to((config::config_path().parent_path() / "accounts-export.toml").string());
                } else if (hit.action == gui::ActImportAccounts) {
                    settingsCtl.import_from((config::config_path().parent_path() / "accounts-export.toml").string());
                } else if (hit.action == gui::ActBrowseDownloadDir) {
                    host->pick_directory([&](const std::string& path) {
                        if (!path.empty()) settingsFields[gui::FieldDownloadDir].text = path;
                    });
                } else if (hit.action >= gui::ActQualityBase && hit.action < gui::ActLanguageBase) {
                    settingsCtl.mutable_settings().quality = gui::kQualityValues[hit.action - gui::ActQualityBase];
                } else if (hit.action >= gui::ActLanguageBase && hit.action < gui::ActConcurrencyMinus) {
                    settingsCtl.mutable_settings().language = gui::kLanguageValues[hit.action - gui::ActLanguageBase];
                } else if (hit.action == gui::ActConcurrencyMinus) {
                    auto& s = settingsCtl.mutable_settings();
                    if (s.concurrency > 1) s.concurrency--;
                } else if (hit.action == gui::ActConcurrencyPlus) {
                    settingsCtl.mutable_settings().concurrency++;
                } else if (hit.action == gui::ActRpmMinus) {
                    auto& s = settingsCtl.mutable_settings();
                    if (s.requests_per_minute > 0) s.requests_per_minute--;
                } else if (hit.action == gui::ActRpmPlus) {
                    settingsCtl.mutable_settings().requests_per_minute++;
                } else if (hit.action == gui::ActSettingsSave) {
                    settingsCtl.mutable_settings().download_dir = settingsFields[gui::FieldDownloadDir].text;
                    settingsCtl.save();
                    kb::api::set_requests_per_minute(settingsCtl.config().settings.requests_per_minute);
                } else if (hit.action >= gui::ActFieldFocusBase && hit.action < gui::ActFieldFocusBase + gui::FieldCount) {
                    focusedField = hit.action - gui::ActFieldFocusBase;
                }
            }
        }

        if (activeScreen == Screen::Search) {
            // Enter submits the search regardless of pointer position.
            if (input.keyWentDown(key::Enter)) {
                searchCtl.search(queryField.text, std::string(gui::kTypePickerOptions[(size_t)typePickerIndex]));
                tableScrollPx = 0.0f;
            }
            widgets::textFieldHandleInput(queryField, input);
            if (input.wheelDelta != 0.0f) {
                Rect area = {w * 0.04f, h * 0.03f, w * 0.92f, h * 0.94f};
                // wheelDelta is positive for a physical "scroll up" (toward
                // earlier content) — that must DECREASE scrollPx (scrollPx=0
                // is the top of the list), so subtract, not add.
                tableScrollPx -= input.wheelDelta * rowH;
                float maxScroll = std::max(0.0f, (float)searchCtl.results().size() * rowH - area.h * 0.5f);
                tableScrollPx = std::clamp(tableScrollPx, 0.0f, maxScroll);
            }
        } else {
            // Reload the account edit fields from the controller whenever
            // the selected account changed (switching accounts must not
            // clobber in-progress edits, but must pick up the new one).
            if (settingsCtl.current_account_index() != loadedAccountIdx) {
                // current_account() (not accounts()[index]) because it
                // self-heals an empty account list (fresh install, no
                // config.toml yet) by lazily creating a blank account —
                // accounts() alone would still be empty here and indexing
                // it with current_account_index()'s default of 0 crashes.
                const config::Account& a = settingsCtl.current_account();
                settingsFields[gui::FieldCountry].text = a.country;
                settingsFields[gui::FieldEmail].text = a.email;
                settingsFields[gui::FieldAppId].text = a.app_id;
                settingsFields[gui::FieldAppSecret].text = a.app_secret;
                settingsFields[gui::FieldUserId].text = a.user_id;
                settingsFields[gui::FieldAuthToken].text = a.auth_token;
                for (int fi = gui::FieldCountry; fi <= gui::FieldAuthToken; fi++)
                    settingsFields[fi].cursorByte = settingsFields[fi].text.size();
                loadedAccountIdx = settingsCtl.current_account_index();
            }
            if (focusedField >= 0) widgets::textFieldHandleInput(settingsFields[focusedField], input);
            // Keep the account struct in sync with the edit fields every
            // frame (not just on Save) so Login with Token always sees the
            // latest typed app_id/app_secret/user_id/auth_token.
            config::Account& a = settingsCtl.current_account();
            a.country = settingsFields[gui::FieldCountry].text;
            a.email = settingsFields[gui::FieldEmail].text;
            a.app_id = settingsFields[gui::FieldAppId].text;
            a.app_secret = settingsFields[gui::FieldAppSecret].text;
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
                scrollPx -= input.wheelDelta * prowH;  // see the search table's note on sign
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
            // Dev tooling: one of each button kind/state, to eyeball the
            // OLED-friendly bar treatment instead of the old gradient fill.
            float bw = w * 0.18f, bh = h * 0.08f, gap = w * 0.02f;
            float x = w * 0.05f, y = h * 0.08f;
            theme::button(canvas, x + 0 * (bw + gap), y, bw, bh, "Primary",
                         theme::ButtonKind::Primary, {}, bh * 0.25f);
            theme::button(canvas, x + 1 * (bw + gap), y, bw, bh, "Secondary",
                         theme::ButtonKind::Secondary, {}, bh * 0.25f);
            theme::button(canvas, x + 2 * (bw + gap), y, bw, bh, "Danger",
                         theme::ButtonKind::Danger, {}, bh * 0.25f);
            theme::button(canvas, x + 3 * (bw + gap), y, bw, bh, "Hovered",
                         theme::ButtonKind::Primary, {/*hovered=*/true, false, false}, bh * 0.25f);
            y += bh + h * 0.03f;
            theme::button(canvas, x + 0 * (bw + gap), y, bw, bh, "Pressed",
                         theme::ButtonKind::Primary, {true, /*pressed=*/true, false}, bh * 0.25f);
            theme::button(canvas, x + 1 * (bw + gap), y, bw, bh, "Disabled",
                         theme::ButtonKind::Primary, {false, false, /*disabled=*/true}, bh * 0.25f);
        } else {
            hits.clear();

            // Nav bar (always visible, both screens).
            Rect navRow = {w * 0.04f, h * 0.02f, w * 0.2f, rowH};
            int navIdx = activeScreen == Screen::Search ? 0 : 1;
            widgets::drawSegmented(canvas, navRow, {"Search", "Settings"}, navIdx, theme::kSegmented);
            auto navRects = widgets::segmentRects(navRow, 2);
            hits.push_back({navRects[0], kActNavSearch});
            hits.push_back({navRects[1], kActNavSettings});

            Rect area = {w * 0.04f, navRow.y + navRow.h + h * 0.02f, w * 0.92f,
                        h * 0.94f - navRow.h - h * 0.02f};
            gui::PointerState ptr{input.pointerX, input.pointerY, input.pointerDown,
                                 input.pointerWentDown, input.pointerWentUp};
            if (activeScreen == Screen::Search) {
                gui::draw_search(canvas, area, searchCtl, queryField, /*queryFocused=*/true,
                                 typePickerIndex, tableScrollPx, rowH,
                                 hoverRow, hoverHeaderCol, hoveredAction,
                                 ptr, dtSeconds, tableInteraction, hits);
            } else {
                gui::draw_settings(canvas, area, settingsCtl, settingsFields, focusedField,
                                   accountListHover, hoveredAction, input.pointerDown, rowH, hits);
            }
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
