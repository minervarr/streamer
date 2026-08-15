#include "library_view.hh"
#include "theme.hh"

#include "art_texture.hh"
#include "asset_reader.hh"
#include "msdf.hh"       // FontStyle
#include "text_util.hh"  // truncateToWidth
#include "widgets.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gui {

namespace {

// Grid geometry, derived in one place so draw_library and
// libraryContentHeight can never disagree about how tall the content is.
struct Grid {
    int   columns = 1;
    float tileW = 0.0f;     // cover square edge
    float tileH = 0.0f;     // cover + caption block
    float gap = 0.0f;
    float captionH = 0.0f;
    int   rows = 0;
    float contentH = 0.0f;
};

Grid gridFor(const Rect& gridArea, int albumCount, float rowH) {
    Grid g;
    g.gap = rowH * 0.35f;
    // Tiles want to be big enough for the art to be worth showing, but a wide
    // window should get more of them rather than enormous ones.
    const float wantW = std::max(rowH * 3.6f, 160.0f);
    g.columns = std::clamp(static_cast<int>((gridArea.w + g.gap) / (wantW + g.gap)), 1, 10);
    g.tileW = (gridArea.w - g.gap * (g.columns - 1)) / g.columns;
    g.captionH = rowH * 2.4f;   // title + artist + tracks/size
    g.tileH = g.tileW + g.captionH;
    g.rows = albumCount > 0 ? (albumCount + g.columns - 1) / g.columns : 0;
    g.contentH = g.rows > 0 ? g.rows * g.tileH + (g.rows - 1) * g.gap : 0.0f;
    return g;
}

// The grid occupies everything below the header block; both draw_library and
// libraryContentHeight need the same split.
Rect gridAreaFor(const Rect& area, float rowH) {
    const float headerH = rowH * 2.6f;
    return {area.x, area.y + headerH, area.w, std::max(0.0f, area.h - headerH - rowH * 1.4f)};
}

theme::ButtonState btnState(int hoveredAction, bool pointerDown, int action, bool disabled = false) {
    bool hov = !disabled && hoveredAction == action;
    return {hov, hov && pointerDown, disabled};
}

void drawTruncated(Canvas& c, const std::string& s, float x, float y, float maxW,
                   float size, Color color) {
    if (s.empty()) return;
    c.text(truncateToWidth(c, s, maxW, size, FontStyle::Roman), x, y, size, color);
}

// The no-artwork tile: a solid #000000 square carrying a message, so a
// missing cover reads as a deliberate state rather than a hole in the render.
// The app background is already black, so without the outline the tile's
// bounds would be invisible and the caption below it would look unattached.
void drawNoArtwork(Canvas& c, float x, float y, float side, float rowH) {
    constexpr Color kNoArt = {0.0f, 0.0f, 0.0f, 1.0f};   // #000000, as specified
    c.rect(x, y, side, side, kNoArt);

    const float t = std::max(1.0f, side * 0.006f);
    c.rect(x, y, side, t, theme::kTrack);
    c.rect(x, y + side - t, side, t, theme::kTrack);
    c.rect(x, y, t, side, theme::kTrack);
    c.rect(x + side - t, y, t, side, theme::kTrack);

    const float size = std::min(rowH * 0.42f, side * 0.11f);
    c.textCentered("No artwork", x + side * 0.5f, y + side * 0.5f - size * 0.5f, size,
                   theme::kDim);
}

} // namespace

// ── CoverCache ──────────────────────────────────────────────────────────────

void CoverCache::beginFrame() { loadsLeft_ = loadsPerFrame; }

void CoverCache::clear() {
    if (renderer_) {
        for (auto& [id, item] : items_)
            if (item.tex != kInvalidTexture) renderer_->destroy_texture(item.tex);
    }
    items_.clear();
}

void CoverCache::rebind(Renderer* renderer) {
    items_.clear();   // drop, do not destroy — see the header
    renderer_ = renderer;
    loadsLeft_ = 0;
}

void CoverCache::evictIfOverBudget() {
    if (items_.size() <= budget) return;
    // Least-recently-drawn first. Linear scans over a map capped at `budget`
    // entries, a few times per frame at worst — not worth an LRU list.
    while (items_.size() > budget) {
        auto oldest = items_.end();
        for (auto it = items_.begin(); it != items_.end(); ++it)
            if (oldest == items_.end() || it->second.lastUsed < oldest->second.lastUsed)
                oldest = it;
        if (oldest == items_.end()) break;
        if (oldest->second.tex != kInvalidTexture && renderer_)
            renderer_->destroy_texture(oldest->second.tex);
        items_.erase(oldest);
    }
}

TextureHandle CoverCache::get(const std::string& albumId, const std::string& path, int targetPx) {
    if (albumId.empty()) return kInvalidTexture;

    auto it = items_.find(albumId);
    if (it != items_.end()) {
        it->second.lastUsed = ++clock_;
        return it->second.tex;   // kInvalidTexture when the load already failed
    }
    if (path.empty()) {
        items_.emplace(albumId, Item{kInvalidTexture, ++clock_, true});
        return kInvalidTexture;
    }
    // Out of decode budget: leave it unrecorded so a later frame retries.
    // The tile draws its no-artwork state for now, which is also what the
    // user sees while scrolling fast — no stall, art fills in behind them.
    if (loadsLeft_ <= 0 || !renderer_) return kInvalidTexture;
    --loadsLeft_;

    // Not the host's AssetReader: that one resolves paths relative to the
    // exe's assets/ directory, and cover art lives at an absolute path
    // somewhere in the user's library.
    FileByteReader reader;
    Item item;
    item.lastUsed = ++clock_;
    item.tex = createTextureFromImageFile(*renderer_, reader, path.c_str(), targetPx, targetPx,
                                          nullptr, nullptr, /*mips=*/false);
    item.failed = item.tex == kInvalidTexture;
    items_.emplace(albumId, item);
    evictIfOverBudget();
    return item.tex;
}

// ── Drawing ─────────────────────────────────────────────────────────────────

float libraryContentHeight(const Rect& area, int albumCount, float rowH) {
    return gridFor(gridAreaFor(area, rowH), albumCount, rowH).contentH;
}

void draw_library(Canvas& c, const Rect& area, const libmgr::LibraryController& ctl,
                  CoverCache& covers, float scrollPx, float rowH,
                  int hoveredAction, bool pointerDown,
                  std::vector<Hit>& hits) {
    const auto& albums = ctl.albums();
    const size_t selected = ctl.selection_count();
    const bool modal = ctl.delete_pending();

    // ── Header ──────────────────────────────────────────────────────────────
    const float titleSize = rowH * 0.7f;
    c.text("Library", area.x, area.y, titleSize, theme::kText);

    int64_t totalBytes = 0;
    for (const auto& a : albums) totalBytes += a.bytes_on_disk;
    char summary[192];
    if (selected > 0)
        std::snprintf(summary, sizeof(summary), "%zu album%s · %s · %zu selected (%s)",
                      albums.size(), albums.size() == 1 ? "" : "s",
                      libmgr::format_bytes(totalBytes).c_str(), selected,
                      libmgr::format_bytes(ctl.selection_bytes()).c_str());
    else
        std::snprintf(summary, sizeof(summary), "%zu album%s · %s", albums.size(),
                      albums.size() == 1 ? "" : "s", libmgr::format_bytes(totalBytes).c_str());
    c.text(summary, area.x, area.y + titleSize * 1.35f, rowH * 0.4f, theme::kDim);

    // Buttons, right-aligned: Refresh / Select all / Clear / Delete.
    const float btnH = rowH * 1.1f;
    const float btnW = std::min(area.w * 0.15f, rowH * 4.2f);
    const float btnGap = rowH * 0.25f;
    const float btnY = area.y;
    const float radius = btnH * 0.2f;
    struct HeaderBtn { const char* label; int action; theme::ButtonKind kind; bool disabled; };
    const HeaderBtn headerBtns[] = {
        {"Refresh",    ActLibRefresh,        theme::ButtonKind::Secondary, false},
        {"Select all", ActLibSelectAll,      theme::ButtonKind::Secondary, albums.empty()},
        {"Clear",      ActLibClearSelection, theme::ButtonKind::Secondary, selected == 0},
        {"Delete",     ActLibDeleteSelected, theme::ButtonKind::Danger,    selected == 0},
    };
    constexpr int kHeaderBtnCount = 4;
    float bx = area.x + area.w - (btnW * kHeaderBtnCount + btnGap * (kHeaderBtnCount - 1));
    for (const auto& b : headerBtns) {
        theme::button(c, bx, btnY, btnW, btnH, b.label, b.kind,
                      btnState(hoveredAction, pointerDown, b.action, b.disabled), radius);
        if (!b.disabled) hits.push_back({{bx, btnY, btnW, btnH}, b.action});
        bx += btnW + btnGap;
    }

    // ── Grid ────────────────────────────────────────────────────────────────
    const Rect grid = gridAreaFor(area, rowH);
    const Grid g = gridFor(grid, static_cast<int>(albums.size()), rowH);

    if (albums.empty()) {
        const char* msg = ctl.catalog_lost()
            ? "The catalog is missing but albums are on disk — run `streamer library scan`"
            : "Nothing downloaded yet";
        c.textCentered(msg, grid.x + grid.w * 0.5f, grid.y + grid.h * 0.35f, rowH * 0.45f,
                       theme::kDim);
    } else {
        c.setClip(grid.x, grid.y, grid.w, grid.h);
        const int coverPx = std::max(64, static_cast<int>(g.tileW));

        for (size_t i = 0; i < albums.size(); i++) {
            const int row = static_cast<int>(i) / g.columns;
            const int col = static_cast<int>(i) % g.columns;
            const float tx = grid.x + col * (g.tileW + g.gap);
            const float ty = grid.y + row * (g.tileH + g.gap) - scrollPx;

            // Cull off-screen rows: the whole point of the decode budget is
            // that covers only load for tiles someone can actually see.
            if (ty + g.tileH < grid.y || ty > grid.y + grid.h) continue;

            const auto& album = albums[i];
            const bool isSelected = ctl.is_selected(static_cast<int>(i));
            const bool isHovered = hoveredAction == ActLibTileBase + static_cast<int>(i);

            // Cover art goes in the FOREGROUND image layer. Background images
            // composite before the vector overlay, and every screen starts
            // with a full-window clear() — art in the background layer is
            // painted over by that clear and never appears.
            //
            // The same ordering is why the modal suppresses art entirely: a
            // foreground image would otherwise draw on top of the dialog.
            // Under the scrim these placeholders are barely distinguishable
            // from the dimmed covers they stand in for.
            TextureHandle tex = modal ? kInvalidTexture
                                      : covers.get(album.id, ctl.cover_path(album), coverPx);
            if (modal)
                c.rect(tx, ty, g.tileW, g.tileW, theme::kPanel);
            else if (tex != kInvalidTexture)
                c.imageFg(tex, tx, ty, g.tileW, g.tileW);
            else
                drawNoArtwork(c, tx, ty, g.tileW, rowH);

            // Selection/hover marking goes around the art, never over it —
            // a translucent wash on top of a cover just makes the art muddy.
            if (isSelected || isHovered) {
                const Color edge = isSelected ? theme::kAccent : theme::kDim;
                const float t = std::max(2.0f, g.tileW * 0.012f);
                c.rect(tx - t, ty - t, g.tileW + t * 2.0f, t, edge);
                c.rect(tx - t, ty + g.tileW, g.tileW + t * 2.0f, t, edge);
                c.rect(tx - t, ty - t, t, g.tileW + t * 2.0f, edge);
                c.rect(tx + g.tileW, ty - t, t, g.tileW + t * 2.0f, edge);
            }
            // Caption.
            const float titleS = std::min(rowH * 0.42f, g.tileW * 0.11f);
            const float metaS = titleS * 0.82f;
            float cy = ty + g.tileW + titleS * 0.5f;

            // The selection tick lives on the title line, not over the art:
            // art is a foreground image, so anything drawn on the cover rect
            // ends up underneath it.
            float titleW = g.tileW;
            if (isSelected) {
                const float badge = titleS * 1.15f;
                const float bx = tx + g.tileW - badge, by = cy - badge * 0.12f;
                c.rect(bx, by, badge, badge, theme::kAccent);
                // The tick is drawn, not typed: U+2713 is not in the MSDF
                // atlas and comes out as an empty box.
                const float t = std::max(1.5f, badge * 0.12f);
                c.segment(bx + badge * 0.24f, by + badge * 0.52f,
                          bx + badge * 0.43f, by + badge * 0.72f, t, theme::kBackground);
                c.segment(bx + badge * 0.43f, by + badge * 0.72f,
                          bx + badge * 0.76f, by + badge * 0.28f, t, theme::kBackground);
                titleW -= badge + titleS * 0.25f;
            }
            drawTruncated(c, album.title, tx, cy, titleW, titleS, theme::kText);
            cy += titleS * 1.35f;
            drawTruncated(c, album.artist_name, tx, cy, g.tileW, metaS, theme::kDim);
            cy += metaS * 1.35f;

            char meta[128];
            std::snprintf(meta, sizeof(meta), "%d/%d · %s", album.files_on_disk,
                          album.tracks_count.value_or(album.files_on_disk),
                          libmgr::format_bytes(album.bytes_on_disk).c_str());
            // An album missing files is worth flagging: it is the state a
            // resumed or interrupted download leaves behind.
            const bool incomplete = album.tracks_count.has_value() &&
                                    album.files_on_disk < *album.tracks_count;
            drawTruncated(c, meta, tx, cy, g.tileW, metaS,
                          incomplete ? theme::kDanger : theme::kDim);

            hits.push_back({{tx, ty, g.tileW, g.tileH}, ActLibTileBase + static_cast<int>(i)});
        }
        c.clearClip();
    }

    // ── Status line from the last delete ────────────────────────────────────
    if (!ctl.status().empty()) {
        const float statusY = area.y + area.h - rowH * 1.0f;
        const float statusS = rowH * 0.42f;
        drawTruncated(c, ctl.status(), area.x, statusY, area.w * 0.8f, statusS,
                      ctl.status_is_error() ? theme::kDanger : theme::kAccent);
        const float dismissW = rowH * 2.2f, dismissH = rowH * 0.9f;
        const float dismissX = area.x + area.w - dismissW;
        theme::button(c, dismissX, statusY - dismissH * 0.2f, dismissW, dismissH, "Dismiss",
                      theme::ButtonKind::Secondary,
                      btnState(hoveredAction, pointerDown, ActLibDismissStatus), radius);
        hits.push_back({{dismissX, statusY - dismissH * 0.2f, dismissW, dismissH},
                        ActLibDismissStatus});
    }

    // ── Confirm modal ───────────────────────────────────────────────────────
    if (!ctl.delete_pending()) return;

    // A modal is modal: everything behind it, nav bar included, stops being
    // clickable. See this function's doc comment.
    hits.clear();

    const float panelW = std::min(area.w * 0.6f, rowH * 16.0f);
    const float lineH = rowH * 0.62f;
    std::vector<std::string> names;
    for (const auto& a : albums) {
        if (!ctl.selected().count(a.id)) continue;
        if (names.size() >= 8) break;
        // "·", not an em dash: the latter is not in the font atlas and
        // silently renders as a gap (the tile captions already use "·").
        names.push_back(a.artist_name.empty() ? a.title
                                              : a.artist_name + " \xC2\xB7 " + a.title);
    }
    const size_t hidden = ctl.selection_count() - names.size();
    const float listLines = static_cast<float>(names.size() + (hidden > 0 ? 1 : 0));
    const float panelH = rowH * 2.2f + listLines * lineH + rowH * 3.2f;
    const float px = area.x + (area.w - panelW) * 0.5f;
    const float py = area.y + (area.h - panelH) * 0.5f;

    // Dim the screen, then occlude: text already emitted composites in a
    // later pass than this panel, so without occlude() the grid's captions
    // would show straight through it.
    // The whole window, not just `area`: the nav bar the caller drew above
    // us is inert now (hits are cleared), and must look it.
    c.rect(c.left(), c.top(), c.w(), c.h(), {0.0f, 0.0f, 0.0f, 0.72f});
    c.occlude(c.left(), c.top(), c.w(), c.h());
    c.rect(px, py, panelW, panelH, theme::kPanel, rowH * 0.2f);
    c.rect(px, py, std::max(3.0f, rowH * 0.06f), panelH, theme::kDanger, rowH * 0.2f);

    const float padX = rowH * 0.7f;
    float ty = py + rowH * 0.5f;
    char headline[160];
    std::snprintf(headline, sizeof(headline), "Delete %zu album%s?", ctl.selection_count(),
                  ctl.selection_count() == 1 ? "" : "s");
    c.text(headline, px + padX, ty, rowH * 0.6f, theme::kText);
    ty += rowH * 1.0f;

    char detail[160];
    std::snprintf(detail, sizeof(detail), "%s will be removed from disk. This cannot be undone.",
                  libmgr::format_bytes(ctl.selection_bytes()).c_str());
    c.text(detail, px + padX, ty, rowH * 0.4f, theme::kDim);
    ty += rowH * 0.9f;

    for (const auto& n : names) {
        drawTruncated(c, n, px + padX, ty, panelW - padX * 2.0f, rowH * 0.42f, theme::kText);
        ty += lineH;
    }
    if (hidden > 0) {
        char more[64];
        std::snprintf(more, sizeof(more), "and %zu more", hidden);
        c.text(more, px + padX, ty, rowH * 0.42f, theme::kDim);
        ty += lineH;
    }

    const float mBtnW = (panelW - padX * 2.0f - rowH * 0.4f) * 0.5f;
    const float mBtnH = rowH * 1.2f;
    const float mBtnY = py + panelH - mBtnH - rowH * 0.5f;
    theme::button(c, px + padX, mBtnY, mBtnW, mBtnH, "Cancel", theme::ButtonKind::Secondary,
                  btnState(hoveredAction, pointerDown, ActLibCancelDelete), radius);
    hits.push_back({{px + padX, mBtnY, mBtnW, mBtnH}, ActLibCancelDelete});

    const float delX = px + padX + mBtnW + rowH * 0.4f;
    theme::button(c, delX, mBtnY, mBtnW, mBtnH, "Delete", theme::ButtonKind::Danger,
                  btnState(hoveredAction, pointerDown, ActLibConfirmDelete), radius);
    hits.push_back({{delX, mBtnY, mBtnW, mBtnH}, ActLibConfirmDelete});
}

} // namespace gui
