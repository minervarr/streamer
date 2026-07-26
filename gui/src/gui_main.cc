// streamer GUI — portable skeleton. Owns the dirty-flag frame loop; the
// per-OS AppHost (host.hh, os/wayland_host.cc today) owns windows/input pump.
//
// Phase 1: no controllers/screens yet — just proves the pipeline (window
// opens, clears to black, closes cleanly). Later phases wire SearchController/
// SettingsController and their views in here.

#include "host.hh"
#include "theme.hh"

#include "canvas.hh"
#include "frame_input.hh"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Headless smoke-test entry point (scanersito's convention): exercises
// pure-logic pieces with no window/GPU. Currently a no-op placeholder —
// Phase 3 adds the query DSL assertions here.
int run_selftest() {
    std::printf("selftest: ok (no assertions registered yet)\n");
    return 0;
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
