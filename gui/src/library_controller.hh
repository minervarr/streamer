#pragma once
// LibraryController — pure logic, no rendering (Controller half of the
// Controller/View split this GUI follows; see search_controller.hh).
//
// Owns the album list read from the on-disk catalog (src/library.hh), the
// selection, the pending-delete confirmation, and the status line left behind
// by the last delete. Knows nothing about textures or pixels: the cover
// *path* is state, the cover *texture* is the view's problem (see
// library_view.hh's CoverCache).
//
// Constructible and drivable without a window — reload()/selection/delete are
// exercised headlessly by gui_main.cc's --selftest against a throwaway
// library root.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <library.hh>

namespace libmgr {

class LibraryController {
public:
    // `root` is the library root (the configured download_dir), the directory
    // holding .streamer/library.db. May be empty or nonexistent — that is the
    // "nothing downloaded yet" state, not an error.
    explicit LibraryController(std::string root) : root_(std::move(root)) {}

    void set_root(std::string root);
    const std::string& root() const { return root_; }

    // Re-reads the catalog. `limit` caps rows the same way `library list`
    // does. Drops any selection entry whose album no longer exists, so a
    // selection can never outlive what it points at.
    void reload(uint32_t limit = 2000);

    const std::vector<library::AlbumEntry>& albums() const { return albums_; }

    // True when the tree holds album directories but the catalog is empty —
    // i.e. library.db was lost and `streamer library scan` would rebuild it.
    // Worth saying out loud, since it looks identical to an empty library.
    bool catalog_lost() const { return catalog_lost_; }

    // Absolute path of `album`'s cover art, or "" when the catalog has no
    // cover asset for it. Existence on disk is not checked here — the view
    // finds that out when the decode fails, and treats both the same way.
    std::string cover_path(const library::AlbumEntry& album) const;

    // Selection is keyed by album id, not row index: reload() can reorder or
    // drop rows, and an index-based selection would silently follow whatever
    // lands in that slot. Same rationale as SearchController's.
    void toggle_selected(int index);
    bool is_selected(int index) const;
    void clear_selection();
    void select_all();
    const std::set<std::string>& selected() const { return selected_ids_; }
    size_t selection_count() const { return selected_ids_.size(); }

    // Total bytes the current selection occupies, for the confirm dialog.
    int64_t selection_bytes() const;

    // ── Delete, as a two-step confirmation ───────────────────────────────
    // Deletion is irreversible and touches the filesystem, so the controller
    // refuses to do it in one call: the view asks for confirmation first, and
    // only confirm_delete() actually unlinks anything.
    void request_delete();          // no-op when the selection is empty
    void cancel_delete();
    bool delete_pending() const { return delete_pending_; }

    // Deletes every selected album via library::delete_album, clears the
    // selection, reloads, and leaves a summary in status(). Does nothing
    // unless delete_pending(). Synchronous: this is a handful of unlink()
    // calls per album, unlike the download path that genuinely needs a thread.
    void confirm_delete();

    // Human-readable result of the last delete ("Deleted 3 albums, 412.5 MB
    // freed"), or a failure summary. Empty until something has been deleted.
    const std::string& status() const { return status_; }
    bool status_is_error() const { return status_is_error_; }
    void clear_status();

    // Monotonic counter bumped on any state change, so the view can drive
    // animations and cache invalidation without duplicating state.
    uint64_t revision() const { return revision_; }

private:
    std::string root_;
    std::vector<library::AlbumEntry> albums_;
    std::set<std::string> selected_ids_;
    std::string status_;
    bool status_is_error_ = false;
    bool catalog_lost_ = false;
    bool delete_pending_ = false;
    uint64_t revision_ = 0;
};

// "412.5 MB" / "1.2 GB" — shared with the view so the grid's per-album size
// and the confirm dialog's total read identically.
std::string format_bytes(int64_t bytes);

} // namespace libmgr
