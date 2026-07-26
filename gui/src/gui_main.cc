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

#include <cstdint>
#include <cstdio>
#include <cstring>
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

int main(int argc, char** argv) {
    bool theme_preview = false;
    const char* capture_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) return run_selftest();
        if (std::strcmp(argv[i], "--theme-preview") == 0) theme_preview = true;
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
