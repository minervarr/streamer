#pragma once
// Library screen — pure drawing (View half of the Controller/View split;
// same convention as views.hh). Draws from LibraryController's state, decides
// nothing, and reports click targets into `hits`.
//
// Its one piece of owned state is CoverCache: cover art is a GPU resource
// with a lifetime longer than a frame, so it cannot live in the controller
// (which knows nothing about Vulkan) nor be rebuilt per frame.

#include "library_controller.hh"
#include "views.hh"   // Hit

#include "canvas.hh"
#include "renderer.hh"
#include "texture.hh"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gui {

// Library screen actions — its own numbering, like SettingsAction's.
enum LibraryAction : int {
    ActLibDeleteSelected = 1,
    ActLibSelectAll,
    ActLibClearSelection,
    ActLibRefresh,
    ActLibConfirmDelete,
    ActLibCancelDelete,
    ActLibDismissStatus,
    ActLibTileBase = 100,   // + tile index (toggles selection)
};

// Album covers uploaded as GPU textures, keyed by album id.
//
// Textures are created on demand for tiles that are actually on screen and
// evicted least-recently-drawn past `budget`, because a library of a few
// thousand albums would otherwise want a few thousand live textures. A decode
// that fails is remembered as a failure, so a missing or corrupt cover.jpg is
// not retried every single frame — it just draws the "no artwork" tile.
class CoverCache {
public:
    // `renderer` must outlive the cache. Textures are destroyed by clear().
    explicit CoverCache(Renderer* renderer) : renderer_(renderer) {}
    ~CoverCache() { clear(); }
    CoverCache(const CoverCache&) = delete;
    CoverCache& operator=(const CoverCache&) = delete;

    // Texture for `albumId`, loading `path` if this is the first sight of it.
    // kInvalidTexture means "draw the no-artwork tile": either the load has
    // failed, or this frame's decode budget is already spent and the cover
    // will appear on a later frame.
    TextureHandle get(const std::string& albumId, const std::string& path, int targetPx);

    // Call once per frame before drawing. Resets the per-frame decode budget
    // that keeps a fast scroll through an unseen part of the library from
    // stalling on dozens of JPEG decodes in a single frame.
    void beginFrame();

    // Drops everything (e.g. after a delete, or when the library root
    // changes). Safe to call with no live textures.
    void clear();

    // Point at a different Renderer after the previous one was destroyed
    // (Android surface loss). Deliberately NOT clear() + reassign: the old
    // textures died with their device, so destroying them — through either
    // Renderer — is a use-after-free. The handles are dropped, not freed, and
    // the covers reload on demand against the new one.
    void rebind(Renderer* renderer);

    size_t budget = 64;          // max live textures
    int loadsPerFrame = 4;       // max decodes per frame

private:
    struct Item {
        TextureHandle tex = kInvalidTexture;
        uint64_t lastUsed = 0;
        bool failed = false;
    };

    void evictIfOverBudget();

    Renderer* renderer_ = nullptr;
    std::unordered_map<std::string, Item> items_;
    uint64_t clock_ = 0;
    int loadsLeft_ = 0;
};

// Height of the scrollable album grid's content, so gui_main.cc can clamp
// wheel scrolling to it without duplicating the grid's layout math.
float libraryContentHeight(const Rect& area, int albumCount, float rowH);

// Draws the library screen: header (counts, Select all / Clear / Refresh /
// Delete), the scrolling grid of cover tiles, the status line from the last
// delete, and — when the controller has a delete pending — the confirmation
// modal over all of it.
//
// While the modal is up it CLEARS `hits` before pushing its own, so nothing
// behind it (including the nav bar the caller pushed) can be clicked. A modal
// that leaves the app navigable underneath is not a modal.
void draw_library(Canvas& c, const Rect& area, const libmgr::LibraryController& ctl,
                  CoverCache& covers, float scrollPx, float rowH,
                  int hoveredAction, bool pointerDown,
                  std::vector<Hit>& hits);

} // namespace gui
