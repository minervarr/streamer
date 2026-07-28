// Wayland skin of the GUI (host.hh), on the engine's raw-Wayland backend
// (framework/Vk_Canvas_Lb_LAW/platform/linux/). Single window, unlike
// scanersito's dual salesman/customer split — streamer only needs one.

#include "../host.hh"

#include "wayland_display.hh"
#include "wayland_platform.hh"
#include "wayland_window.hh"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <sys/wait.h>

namespace {

constexpr const char* kAppId = "io.nava.streamer";

class WaylandHost : public gui::AppHost {
public:
    bool init() override
    {
        display_ = std::make_unique<WaylandDisplay>();
        if (!display_->valid()) {
            std::fprintf(stderr, "[x] no Wayland compositor "
                                 "(WAYLAND_DISPLAY / xdg_wm_base missing)\n");
            return false;
        }
        window_ = std::make_unique<WaylandWindow>(
            *display_, "streamer", kAppId, 1280, 800);
        if (!window_->valid())
            return false;
        window_->take_resized(); // initial size is not a resize
        provider_ = std::make_unique<WaylandSurfaceProvider>(*display_, *window_);
        // Desktop on MAILBOX: 3 swapchain images (see renderer.hh's ctor note).
        renderer_ = std::make_unique<Renderer>(*provider_, assets_, 3);
        return true;
    }

    AssetReader& assets()   override { return assets_; }
    Renderer&    renderer() override { return *renderer_; }

    void pump(int timeout_ms, FrameInput& input) override
    {
        display_->set_sink(window_->surface(), &input);
        if (!display_->dispatch(timeout_ms))
            quit_ = true;   // connection died (compositor gone)

        if (window_->closed())
            quit_ = true;
        if (window_->take_resized()) {
            renderer_->notifyResized();
            dirty_ = true;
        }
    }

    bool quit_requested() override { return quit_; }
    bool take_dirty() override { bool d = dirty_; dirty_ = false; return d; }

    void beep() override
    {
        std::fputc('\a', stderr);
        std::fflush(stderr);
    }

    bool open_url(const std::string& url) override
    {
        // Reject anything that isn't a plain http(s) URL before it ever
        // reaches exec — this runs with the user's shell environment and
        // must never be able to smuggle flags/args into xdg-open.
        if (url.find("://") == std::string::npos) return false;
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;

        pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            execlp("xdg-open", "xdg-open", url.c_str(), (char*)nullptr);
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return true;
    }

    void pick_directory(std::function<void(const std::string&)> cb) override
    {
        // v1: no xdg-desktop-portal integration yet (Phase 6 follow-up).
        // Callers fall back to manual path text entry until this lands.
        cb("");
    }

    void set_clipboard_text(const std::string& utf8) override
    {
        display_->set_clipboard_text(utf8);
    }

    std::string get_clipboard_text() override
    {
        return display_->get_clipboard_text();
    }

private:
    FileAssetReader assets_;
    std::unique_ptr<WaylandDisplay>         display_;
    std::unique_ptr<WaylandWindow>          window_;
    std::unique_ptr<WaylandSurfaceProvider> provider_;
    std::unique_ptr<Renderer>               renderer_;

    bool quit_  = false;
    bool dirty_ = false;
};

} // namespace

namespace gui {
std::unique_ptr<AppHost> make_host() { return std::make_unique<WaylandHost>(); }
} // namespace gui
