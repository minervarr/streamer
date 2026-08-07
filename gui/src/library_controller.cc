#include "library_controller.hh"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace libmgr {

std::string format_bytes(int64_t bytes) {
    if (bytes < 0) bytes = 0;
    static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int unit = 0;
    while (v >= 1024.0 && unit < 4) {
        v /= 1024.0;
        ++unit;
    }
    char buf[64];
    // Whole bytes never want a decimal point; everything else reads better
    // with one ("412.5 MB", not "412 MB" or "412.53 MB").
    std::snprintf(buf, sizeof(buf), unit == 0 ? "%.0f %s" : "%.1f %s", v, kUnits[unit]);
    return buf;
}

void LibraryController::set_root(std::string root) {
    if (root == root_) return;
    root_ = std::move(root);
    albums_.clear();
    selected_ids_.clear();
    delete_pending_ = false;
    clear_status();
    ++revision_;
}

void LibraryController::reload(uint32_t limit) {
    albums_.clear();
    catalog_lost_ = false;
    if (!root_.empty()) {
        albums_ = library::list_albums(root_, limit);
        if (albums_.empty()) catalog_lost_ = library::catalog_looks_lost(root_);
    }

    // A selection may not outlive the rows it points at: anything deleted (or
    // pruned by a scan) elsewhere is dropped here rather than silently
    // targeting whatever album now sits at that id.
    if (!selected_ids_.empty()) {
        std::set<std::string> live;
        for (const auto& a : albums_)
            if (selected_ids_.count(a.id)) live.insert(a.id);
        selected_ids_.swap(live);
    }
    ++revision_;
}

std::string LibraryController::cover_path(const library::AlbumEntry& album) const {
    if (root_.empty()) return {};
    if (!album.cover_path.empty())
        return (fs::u8path(root_) / fs::u8path(album.cover_path)).u8string();

    // No cover asset row. The file may still be there — an older download, or
    // one adopted by a scan that didn't re-register assets — and it is always
    // at the same place, so try the conventional path before giving up.
    if (album.country.empty() || album.id.empty()) return {};
    return (fs::u8path(root_) / fs::u8path(album.country) / fs::u8path(album.id) / "cover.jpg")
        .u8string();
}

void LibraryController::toggle_selected(int index) {
    if (index < 0 || static_cast<size_t>(index) >= albums_.size()) return;
    const std::string& id = albums_[static_cast<size_t>(index)].id;
    if (!selected_ids_.insert(id).second) selected_ids_.erase(id);
    ++revision_;
}

bool LibraryController::is_selected(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= albums_.size()) return false;
    return selected_ids_.count(albums_[static_cast<size_t>(index)].id) > 0;
}

void LibraryController::clear_selection() {
    if (selected_ids_.empty()) return;
    selected_ids_.clear();
    ++revision_;
}

void LibraryController::select_all() {
    for (const auto& a : albums_) selected_ids_.insert(a.id);
    ++revision_;
}

int64_t LibraryController::selection_bytes() const {
    int64_t total = 0;
    for (const auto& a : albums_)
        if (selected_ids_.count(a.id)) total += a.bytes_on_disk;
    return total;
}

void LibraryController::clear_status() {
    if (status_.empty() && !status_is_error_) return;
    status_.clear();
    status_is_error_ = false;
    ++revision_;
}

void LibraryController::request_delete() {
    if (selected_ids_.empty()) return;
    delete_pending_ = true;
    ++revision_;
}

void LibraryController::cancel_delete() {
    if (!delete_pending_) return;
    delete_pending_ = false;
    ++revision_;
}

void LibraryController::confirm_delete() {
    if (!delete_pending_) return;
    delete_pending_ = false;

    // Snapshot the ids: reload() below rebuilds albums_, and iterating
    // selected_ids_ while delete_album churns the catalog is asking for it.
    const std::vector<std::string> doomed(selected_ids_.begin(), selected_ids_.end());

    int albums_done = 0;
    int64_t freed = 0;
    std::vector<std::string> failed;
    for (const auto& id : doomed) {
        library::DeleteReport r = library::delete_album(root_, id);
        freed += r.bytes_freed;
        if (r.rows_removed && r.failed.empty()) {
            ++albums_done;
        } else {
            failed.insert(failed.end(), r.failed.begin(), r.failed.end());
        }
    }

    char buf[256];
    if (failed.empty()) {
        std::snprintf(buf, sizeof(buf), "Deleted %d album%s, %s freed", albums_done,
                      albums_done == 1 ? "" : "s", format_bytes(freed).c_str());
        status_is_error_ = false;
    } else {
        // Name the first failure rather than only counting them: "3 files
        // could not be removed" is not actionable, a path is.
        std::snprintf(buf, sizeof(buf), "Deleted %d album%s; %zu file%s could not be removed (%s)",
                      albums_done, albums_done == 1 ? "" : "s", failed.size(),
                      failed.size() == 1 ? "" : "s", failed.front().c_str());
        status_is_error_ = true;
    }
    status_ = buf;

    selected_ids_.clear();
    reload();
}

} // namespace libmgr
