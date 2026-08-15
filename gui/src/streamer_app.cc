#include "streamer_app.hh"

#include <cstdio>

namespace {

// Swapchain images. Three on a desktop, where MAILBOX is available and the
// extra image is what lets a frame be built while another waits to be shown;
// two on Android, where FIFO is the only present mode that exists and a third
// image buys nothing but memory. This is the one number that differed between
// streamer's old per-platform hosts, and it is the only reason this file needs
// to know which platform it is on at all.
#if defined(__ANDROID__)
constexpr uint32_t kSwapchainImages = 2;
#else
constexpr uint32_t kSwapchainImages = 3;
#endif

} // namespace

Host* g_injected_host = nullptr;

bool StreamerApp::buildRenderer() {
    renderer_ = std::make_unique<Renderer>(host_->surfaceProvider(),
                                           host_->assetReader(),
                                           kSwapchainImages);
    return renderer_ != nullptr;
}

bool StreamerApp::create(Host* host) {
    host_ = host;
    if (!host_) return false;
    if (!buildRenderer()) {
        std::fprintf(stderr, "[x] failed to create the Vulkan renderer\n");
        return false;
    }
    return true;
}

void StreamerApp::onHostResized() {
    // Tell the Renderer its surface changed, which is what makes the next
    // frame rebuild the swapchain at the new extent. Distinct from
    // onHostLayoutInvalidated(), which is a relayout and nothing more — the
    // two arrive for different reasons on Wayland, where a configure can turn
    // up without the drawable size having moved.
    if (renderer_) renderer_->notifyResized();
    dirty_ = true;
}

void StreamerApp::onSurfaceLost() {
    // Android only, and it is not an optimisation: the ANativeWindow is gone,
    // so every Vulkan object built against it — swapchain, pipelines, the
    // glyph atlas, every cover texture — is invalid. Destroying the Renderer
    // is what makes that unambiguous. Everything CPU-side (the config, the
    // search results, the library) survives untouched, which is the whole
    // point of the split.
    //
    // Nothing may draw between here and onSurfaceRecreated(); the loop checks
    // renderable() every frame for exactly that reason.
    renderer_.reset();
}

bool StreamerApp::onSurfaceRecreated() {
    // A NEW Renderer, at a new address. The loop compares the address it last
    // uploaded the atlas and the cover cache against, so both are rebuilt on
    // the first frame after this — see the boundRenderer check in run().
    if (!buildRenderer()) {
        std::fprintf(stderr, "[x] failed to rebuild the renderer after the "
                             "surface came back\n");
        return false;
    }
    dirty_ = true;
    return true;
}
