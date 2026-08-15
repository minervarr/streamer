// streamer_gui_capture — headless screenshots of the REAL UI. No window, no
// compositor, no display. Debug-only dev tooling (see gui/CMakeLists.txt).
//
// This used to be a second program that rebuilt streamer's layout by hand —
// row heights, the navigation bar, the portrait/landscape split — out of the
// same numbers gui_main.cc uses. It drifted from them repeatedly during the
// Android work, and a capture that has drifted is worse than no capture: it is
// a screenshot of a UI nobody runs, offered as proof.
//
// So it does what Matrix Player's tools/ui_capture does instead: it gives the
// application a Host whose window is a VK_EXT_headless_surface, and then lets
// the application draw. The layout, the hit rects, the glyph baking, the
// controllers are all the ordinary ones, because they are the only ones.
// Nothing here can drift, because nothing here duplicates anything.
//
//   streamer_gui_capture --frame 1920x1080 --out shots
//   streamer_gui_capture --frame 720x1640 --out shots --screen search \
//                        --query 'artist:"Luísa Sonza"'
//
// With no --screen it captures every screen (search, library, settings) into
// --out in one run. It performs real network and disk access, exactly as the
// app does, so a capture reflects live account and library state.

#include "streamer_app.hh"

#include "app_paths.hh"        // app_shell: exeDir(), for the asset root
#include "headless.hh"         // vk_canvas: HeadlessSurfaceProvider
#include "wayland_platform.hh" // vk_canvas: FileAssetReader (exe-relative assets/)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

// ── The Host with no window ─────────────────────────────────────────────────
//
// Every method is either "answer honestly for an off-screen frame" or "do
// nothing, there is no window". Only two of them carry any weight:
//
//   surfaceProvider() hands back a HeadlessSurfaceProvider, so the Renderer's
//   swapchain, render pass and pipeline path run completely unchanged — the
//   swapchain simply presents to nothing.
//
//   pump() must RETURN. A real host blocks in the kernel until something
//   happens; here nothing ever will, and blocking would hang the tool forever
//   on the first frame it had no work for.
//
// Deliberately not offered back to app_shell yet. It would have to be general
// across two consumers to earn a place there, and there is currently one —
// guessing at the shape from a single example is how a library grows an API
// that fits nobody.
class HeadlessHost : public Host {
public:
    HeadlessHost(uint32_t w, uint32_t h) : surface_(w, h) {}

    std::string exeDir() const override { return app_paths::exeDir(); }

    bool init(AppView*) override { return true; }

    SurfaceProvider& surfaceProvider() override { return surface_; }
    AssetReader&     assetReader()     override { return assets_; }
    AssetReader&     dataReader()      override { return assets_; }

    void showWindow() override {}
    MonitorInfo primaryMonitor() const override { return {}; }
    void adaptToCurrentMonitor() override {}
    void snapToEdge(SnapEdge) override {}
    void invalidate() override {}
    void setCursor(CursorShape) override {}
    void setKeepAwake(bool) override {}

    // Nothing here runs a background thread that would post one, and there is
    // no loop to wake even if it did.
    void postAppEvent(int, intptr_t, intptr_t) override {}
    void startTimer(int, int) override {}
    void stopTimer(int) override {}

    // See the class comment: returning immediately is the whole contract.
    void pump(bool) override {}
    bool quitRequested() const override { return false; }

    void showErrorMessage(const std::string& title, const std::string& msg) override {
        std::fprintf(stderr, "[%s] %s\n", title.c_str(), msg.c_str());
    }

private:
    HeadlessSurfaceProvider surface_;
    FileAssetReader         assets_;
};

void usage() {
    std::fprintf(stderr,
        "usage: streamer_gui_capture [--frame WxH] [--out DIR]\n"
        "       [--screen search|library|settings] [--query STRING]\n"
        "       [--library-root DIR] [--library-select] [--library-confirm]\n"
        "       [--settings-scroll PX]\n"
        "  no --screen: captures every screen into DIR (default ./ui-capture)\n");
}

} // namespace

// app_shell's platform factory, which this tool has no use for: it builds its
// own HeadlessHost above and never calls app_shell_main(). The definition
// exists because gui_main.cc — linked here for the frame loop — names the
// symbol on a branch this binary does not take.
//
// Same shape and same reason as AndroidHost's, where the host must be injected
// because only android_main() ever sees the android_app*. Returning null and
// logging beats a link error that names a symbol nobody meant to call.
std::unique_ptr<Host> make_host() {
    std::fprintf(stderr, "[x] streamer_gui_capture builds its own host; "
                         "make_host() should never be reached\n");
    return nullptr;
}

int main(int argc, char** argv) {
    uint32_t frameW = 1920, frameH = 1080;
    std::string outDir = "ui-capture";
    StreamerApp::Options opts;

    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[x] %s wants a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--frame") == 0) {
            const char* v = next("--frame");
            if (std::sscanf(v, "%ux%u", &frameW, &frameH) != 2 || !frameW || !frameH) {
                std::fprintf(stderr, "[x] --frame wants WxH, e.g. 1920x1080\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--out") == 0) {
            outDir = next("--out");
        } else if (std::strcmp(argv[i], "--screen") == 0) {
            const char* v = next("--screen");
            if (std::strcmp(v, "search") && std::strcmp(v, "library") &&
                std::strcmp(v, "settings")) {
                std::fprintf(stderr, "[x] --screen wants search, library or settings\n");
                return 2;
            }
            opts.captureScreens.push_back(v);
        } else if (std::strcmp(argv[i], "--query") == 0) {
            opts.captureQuery = next("--query");
        } else if (std::strcmp(argv[i], "--library-root") == 0) {
            opts.captureLibraryRoot = next("--library-root");
        } else if (std::strcmp(argv[i], "--library-select") == 0) {
            opts.captureLibrarySelect = true;
        } else if (std::strcmp(argv[i], "--library-confirm") == 0) {
            opts.captureLibraryConfirm = true;
        } else if (std::strcmp(argv[i], "--settings-scroll") == 0) {
            opts.settingsScroll = (float)std::atof(next("--settings-scroll"));
        } else {
            usage();
            return 2;
        }
    }
    // Every screen, in the order a person reads them.
    if (opts.captureScreens.empty())
        opts.captureScreens = {"search", "library", "settings"};

    if (!headless_surface_supported()) {
        std::fprintf(stderr, "[x] VK_EXT_headless_surface not supported by this ICD "
                             "(Mesa lavapipe has it)\n");
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    opts.onCaptured = [&](const std::string& screen, const std::vector<uint8_t>& rgba,
                          uint32_t w, uint32_t h) {
        const std::string path = (std::filesystem::path(outDir) / (screen + ".png")).string();
        if (!stbi_write_png(path.c_str(), (int)w, (int)h, 4, rgba.data(), (int)w * 4)) {
            std::fprintf(stderr, "[x] failed to write %s\n", path.c_str());
            return false;
        }
        std::printf("[capture] %-9s %ux%u -> %s\n", screen.c_str(), w, h, path.c_str());
        return true;
    };

    HeadlessHost host(frameW, frameH);
    StreamerApp  app;
    if (!host.init(&app)) return 1;
    if (!app.create(&host)) return 1;
    return app.run(opts);
}
