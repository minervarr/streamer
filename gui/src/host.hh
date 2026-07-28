#pragma once
// The platform seam of the GUI itself: gui_main.cc is the portable skeleton
// (backend, controllers, frame loop, drawing); an AppHost is the per-OS skin
// (window, event pump, beep). One implementation per platform in os/ —
// os/wayland_host.cc today, os/win32_host.cc later — each defining
// make_host().
//
// Contract mirrors the engine's event-driven loop: the skeleton calls
// input.beginFrame() and then pump(timeout), which sleeps until events/wake/
// timeout and feeds the window's input into the FrameInput.

#include "frame_input.hh"
#include "platform.hh"
#include "renderer.hh"

#include <functional>
#include <memory>
#include <string>

namespace gui {

class AppHost {
public:
    virtual ~AppHost() = default;

    // Creates the window + its Renderer. False = fatal (no display).
    virtual bool init() = 0;

    virtual AssetReader& assets() = 0;
    virtual Renderer&    renderer() = 0;

    // Sleep up to timeout_ms for window-system events, dispatch them into
    // `input`. Call input.beginFrame() before this, every frame.
    virtual void pump(int timeout_ms, FrameInput& input) = 0;

    // The window was closed by the system (compositor/user).
    virtual bool quit_requested() = 0;

    // True once when the window system wants a repaint (resize/expose).
    virtual bool take_dirty() = 0;

    // Audible refusal cue (e.g. rejecting an invalid search filter).
    virtual void beep() = 0;

    // Opens a URL in the user's default browser (login flow). Fire-and-forget;
    // returns false if no launcher mechanism is available on this host.
    virtual bool open_url(const std::string& url) = 0;

    // Opens a native directory picker; calls `cb` with the chosen path, or an
    // empty string if the user cancelled. May be synchronous or asynchronous
    // depending on the host; callers should not assume ordering relative to
    // the current frame.
    virtual void pick_directory(std::function<void(const std::string&)> cb) = 0;

    // System clipboard (text only). get_clipboard_text() is synchronous and
    // may block briefly (paste is a short, user-triggered round trip on
    // Wayland) — returns "" if the clipboard is empty, holds non-text
    // content, or the host doesn't support it yet.
    virtual void set_clipboard_text(const std::string& utf8) = 0;
    virtual std::string get_clipboard_text() = 0;
};

std::unique_ptr<AppHost> make_host();   // defined by the platform skin in os/

} // namespace gui
