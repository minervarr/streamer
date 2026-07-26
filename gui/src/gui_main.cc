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
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) return run_selftest();
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
        Canvas canvas(curves, r.width(), r.height(), /*font=*/nullptr,
                      /*insetTop=*/0, /*insetBottom=*/0,
                      /*insetLeft=*/0, /*insetRight=*/0);
        canvas.clear(theme::kBackground);

        r.draw(curves, /*overlay_rotation_deg=*/0);
    }

    return 0;
}
