// streamer GUI — portable skeleton. Owns the dirty-flag frame loop; the
// per-OS AppHost (host.hh, os/wayland_host.cc today) owns windows/input pump.
//
// Phase 1: no controllers/screens yet — just proves the pipeline (window
// opens, clears to black, closes cleanly). Later phases wire SearchController/
// SettingsController and their views in here.

#include "host.hh"
#include "query_dsl.hh"
#include "theme.hh"

#include "canvas.hh"
#include "frame_input.hh"
#include "widgets.hh"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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

    if (g_fail_count == 0) {
        std::printf("selftest: ok (%d assertions)\n", 15);
        return 0;
    }
    std::fprintf(stderr, "selftest: %d assertion(s) failed\n", g_fail_count);
    return 1;
}

} // namespace

namespace {

// Dev-only fake data for --widget-preview (Phase 4's sortable-table visual
// check): the same 10 columns streamer-gui's ListView used.
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

} // namespace

int main(int argc, char** argv) {
    bool theme_preview = false;
    bool widget_preview = false;
    const char* capture_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) return run_selftest();
        if (std::strcmp(argv[i], "--theme-preview") == 0) theme_preview = true;
        if (std::strcmp(argv[i], "--widget-preview") == 0) widget_preview = true;
        if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture_path = argv[++i];
    }

    auto host = gui::make_host();
    if (!host || !host->init()) {
        std::fprintf(stderr, "[x] failed to initialize GUI host\n");
        return 1;
    }

    FrameInput input;
    bool first_frame = true;

    while (!host->quit_requested()) {
        input.beginFrame();
        host->pump(/*timeout_ms=*/1000, input);

        bool dirty = host->take_dirty();
        if (!dirty && !first_frame) continue;
        first_frame = false;

        Renderer& r = host->renderer();
        std::vector<float> curves;
        std::vector<float> shapeVerts;
        Canvas canvas(curves, r.width(), r.height(), /*font=*/nullptr,
                      /*insetTop=*/0, /*insetBottom=*/0,
                      /*insetLeft=*/0, /*insetRight=*/0);
        canvas.useShapes(&shapeVerts);
        canvas.clear(theme::kBackground);

        if (widget_preview) {
            // Visual + interaction check for Phase 4: click a header cell to
            // sort by that column (toggling direction on repeated clicks),
            // scroll the body, confirm hover highlighting and the
            // sort-direction glyph render correctly.
            static int sortCol = 1;  // Artist, ascending — visible without interaction
            static bool sortAsc = true;
            static float scrollPx = 0.0f;
            const auto& cols = previewColumns();
            const int rowCount = 24;

            float w = (float)r.width(), h = (float)r.height();
            Rect area = {w * 0.05f, h * 0.08f, w * 0.9f, h * 0.8f};
            float rowH = h * 0.05f;

            Rect header = widgets::tableHeaderRow(area, rowH);
            auto headerRects = widgets::tableHeaderColumnRects(header, cols);
            int hoverHeaderCol = -1;
            for (size_t i = 0; i < headerRects.size(); i++) {
                if (headerRects[i].contains(input.pointerX, input.pointerY)) {
                    hoverHeaderCol = (int)i;
                    if (input.pointerWentDown) {
                        if (sortCol == (int)i) sortAsc = !sortAsc;
                        else { sortCol = (int)i; sortAsc = true; }
                    }
                }
            }
            if (input.wheelDelta != 0.0f) {
                scrollPx += input.wheelDelta * rowH;
                float maxScroll = std::max(0.0f, rowCount * rowH - (area.h - rowH));
                if (scrollPx < 0.0f) scrollPx = 0.0f;
                if (scrollPx > maxScroll) scrollPx = maxScroll;
            }

            int hoverRow = -1;
            Rect body = {area.x, area.y + rowH, area.w, area.h - rowH};
            if (body.contains(input.pointerX, input.pointerY))
                hoverRow = (int)((input.pointerY - body.y + scrollPx) / rowH);

            widgets::drawSortableTable(canvas, area, cols, previewCell, rowCount,
                                       sortCol, sortAsc, scrollPx, rowH,
                                       hoverRow, hoverHeaderCol);
        }

        if (theme_preview) {
            // Visual check for Phase 2: a row of gradient buttons/bars at
            // different slices of the accent gradient, plus the shared
            // toggle/stepper/slider/segmented/dropdown styles.
            float w = (float)r.width(), h = (float)r.height();
            float bw = w * 0.18f, bh = h * 0.08f, gap = w * 0.02f;
            float x = w * 0.05f, y = h * 0.08f;
            for (int i = 0; i < 4; ++i) {
                theme::accentButton(canvas, x + i * (bw + gap), y, bw, bh,
                                    "Button", bh * 0.25f);
            }
            y += bh + h * 0.05f;
            theme::gradientRect(canvas, x, y, w * 0.9f, h * 0.04f, h * 0.02f);
        }

        r.draw(curves, /*overlay_rotation_deg=*/0, /*images=*/{}, /*foregroundImages=*/{},
              /*msdfQuads=*/{}, shapeVerts);

        if (capture_path) {
            // Dev tooling only (Phase 2 gradient visual check): dump the
            // frame just drawn as raw RGBA8 next to a .txt sidecar with its
            // dimensions, since this repo has no PNG encoder wired in yet.
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
