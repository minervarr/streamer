#pragma once
#include "frame_input_view.hh"   // app_shell: an AppView that fills a FrameInput
#include "host.hh"               // app_shell: the seam onto the OS
#include "renderer.hh"           // vk_canvas

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ── streamer, as app_shell sees it ──────────────────────────────────────────
//
// app_shell owns the platform BOOTSTRAP — the window, the message pump, the
// crash handler, main()/WinMain() themselves — and hands control back. It
// deliberately does not own the frame loop, because what counts as "work to
// do" is the application's question. So `run()` below is ours, and it is the
// same loop gui_main.cc always had.
//
// Two things changed in the move from streamer's own src/host.hh (now
// gui/src/legacy_host/, out of the build):
//
// 1. INPUT. The old seam filled a FrameInput the loop polled; app_shell's
//    dispatches into AppView callbacks. FrameInputView is the adapter between
//    them, so the loop still reads input().pointerWentDown and NOT ONE widget
//    changed. That adapter lives in app_shell rather than here because the
//    next immediate-mode consumer will want it too.
//
// 2. THE RENDERER IS OURS NOW. It used to belong to the host, behind
//    renderer()/renderable(). app_shell hands over a SurfaceProvider instead
//    and reports the surface coming and going — which is the more honest
//    split, because on Android the surface really does die every time the user
//    leaves the app, and the rule it enforces (CPU state survives, GPU state
//    does not) is invisible until the second visit.
class StreamerApp : public FrameInputView {
public:
    // Command-line switches, parsed before the host exists. Kept in one struct
    // so run() takes a single argument rather than five booleans in an order
    // nobody can remember.
    struct Options {
        bool themePreview  = false;
        bool widgetPreview = false;
        bool demo          = false;
        bool startSettings = false;
        const char* capturePath = nullptr;

        // ── Headless capture ────────────────────────────────────────────────
        //
        // What makes streamer_gui_capture drive the REAL application instead
        // of replicating it. It used to be a second program that rebuilt the
        // layout — row heights, the nav bar, the portrait/landscape split —
        // out of the same numbers by hand, and it drifted from this file
        // repeatedly during the Android work. A capture that has drifted is
        // worse than no capture: it is a screenshot of a UI nobody runs.
        //
        // Non-empty captureScreens puts run() in capture mode: it draws each
        // named screen, hands the pixels to onCaptured, and exits. Everything
        // in between — the layout, the hit rects, the glyph baking — is the
        // ordinary frame path, because it IS the ordinary frame path.
        std::vector<std::string> captureScreens;   // "search" | "library" | "settings"
        std::string captureQuery;
        std::string captureLibraryRoot;
        bool  captureLibrarySelect  = false;
        bool  captureLibraryConfirm = false;
        float settingsScroll = 0.0f;

        // Receives the frame just drawn, tightly packed RGBA8. Returning false
        // aborts the run.
        //
        // A callback rather than a file path so the PNG encoder stays in the
        // capture binary: the shipping GUI has no reason to link an image
        // writer, and the Debug-only tool is the only thing that wants one.
        std::function<bool(const std::string& screen,
                           const std::vector<uint8_t>& rgba,
                           uint32_t w, uint32_t h)> onCaptured;
    };

    // Builds the Renderer over the host's surface. False = fatal.
    //
    // Separate from run() because on Android there is no surface until well
    // after the process starts: Host::init() pumps until the window arrives,
    // and only then is there anything to build against.
    bool create(Host* host);

    // The frame loop. Returns the process exit code. Implemented in
    // gui_main.cc, beside the drawing it drives.
    int run(const Options& opts);

    // ── AppView ─────────────────────────────────────────────────────────────
    void onHostResized() override;
    void onHostLayoutInvalidated() override { dirty_ = true; }
    void onHostExposed() override { dirty_ = true; }
    void shutdown() override { running_ = false; }

    void onSurfaceLost() override;
    bool onSurfaceRecreated() override;

    // Any cross-thread nudge. streamer posts these purely to WAKE the loop —
    // a download reports progress from its own thread and would otherwise sit
    // invisible until the user happened to move the pointer. The id is not
    // read, because there is nothing to distinguish yet; marking the frame
    // dirty is the whole response.
    //
    // This replaces a 100 ms poll. The old loop asked its host for a timed
    // wake-up while a download ran, which meant either a percentage that
    // stepped once a second and read as a hang, or a thread waking ten times
    // a second to find nothing changed. Now the frame happens when, and only
    // when, there is something new to draw.
    void onAppEvent(int, intptr_t, intptr_t) override { dirty_ = true; }

    // ── What the loop asks ──────────────────────────────────────────────────

    Host* host() const { return host_; }

    // Only valid while renderable(). On Android the object behind this pointer
    // is destroyed and rebuilt across a visit, so nothing may cache it across
    // frames without re-checking identity — see the coverCache rebind in run().
    Renderer& renderer() const { return *renderer_; }
    bool renderable() const { return renderer_ != nullptr; }

    bool running() const { return running_; }

    // True once per reason-to-redraw, and cleared by asking. Resize, expose
    // and layout-invalidation all land here; ordinary pointer motion does not,
    // and the loop treats that separately.
    bool takeDirty() { bool d = dirty_; dirty_ = false; return d; }
    void markDirty() { dirty_ = true; }

private:
    bool buildRenderer();

    Host*                     host_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    bool dirty_   = true;
    bool running_ = true;
};

// Android's host is INJECTED, not manufactured.
//
// An AndroidHost needs the android_app* that only android_main() is ever
// handed, so app_shell's make_host() returns null there deliberately — a
// factory that cannot see its ingredient should say so rather than invent one.
// android_main.cc builds the host and leaves it here for app_shell_main() to
// find; on both desktops it stays null and make_host() answers instead.
extern Host* g_injected_host;

// What app_shell calls once its platform bootstrap is done. Defined in
// gui_main.cc.
int app_shell_main(int argc, char** argv);
