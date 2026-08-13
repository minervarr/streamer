#pragma once
// Curve + rasterized-glyph text pipeline, shared by every entry point that
// needs to draw the GUI's screens (the interactive gui_main.cc loop and the
// headless tools/capture/main.cc snapshot tool) — factored out so it has
// external linkage instead of living in gui_main.cc's anonymous namespace.
//
// This used to be an MsdfFont: one MTSDF sheet with a hard 4096px ceiling, a
// baked charset that stopped at U+00FF, and a fallback list containing only
// CJK faces. Two bugs followed from that, both visible in the library:
//
//   * Russian titles came out spread far too wide. NewCM10 *does* carry
//     Cyrillic at proper widths (А = 0.750 em), but it was never baked, so
//     every Cyrillic letter was served by FandolHei — whose Cyrillic is
//     full-width CJK metric (1.000 em).
//   * Chinese/Japanese/Korean titles came out blank. bakeCodepoints() is
//     all-or-nothing and refuses the whole batch once the sheet passes 4096px,
//     and a glyph with no cell draws no ink AND no advance, which silently
//     corrupts truncation, centring and hit rects too.
//
// RasterFont (the same engine, already compiled into vk_canvas_core) fixes
// both by construction: it asks the primary face before any fallback, its
// atlas is unbounded pages rather than one sheet, and a glyph it was asked for
// and did not have is recorded and baked next frame instead of dropped.

#include "font.hh"
#include "raster_font.hh"

#include <functional>
#include <string>
#include <vector>

class AssetReader;
class Renderer;

extern Font       g_font;
extern bool       g_font_ok;
extern RasterFont g_text;
extern bool       g_text_ok;

// Opens the faces: NewCM10 Book/Bold/Italic plus the matched CJK chains.
// CPU-side only — no Renderer/GPU, and nothing is baked yet.
//
// `stale_cache` is the old MsdfFont atlas cache; it is deleted if present.
// The raster path has no disk cache on purpose — rasterizing costs 10-50us a
// glyph against MTSDF's 1-10ms, so there is nothing expensive to avoid
// recomputing, and therefore no way to serve a stale bake either.
void init_fonts(AssetReader& assets, const std::string& stale_cache);

// Pushes the atlas into `r`'s GPU textures. Safe (and intended) to call once
// per Renderer — e.g. once for the live windowed Renderer and again for a
// separate headless one.
void upload_glyphs(Renderer& r);

// Bakes the eager charset (Latin, Latin Ext-A/B, Greek, Cyrillic, the
// punctuation real metadata uses) plus every codepoint >= U+0100 found in
// `scan` — search results, library titles, whatever the user typed — at each
// size in `sizesPx`, then re-uploads if anything was added.
//
// Codepoints seen in earlier calls are remembered, so a size change can throw
// the sheet away and rebuild it without the caller having to re-supply the
// text. CJK is never baked in bulk: the Han block alone is 20,000+ codepoints,
// out of all proportion to what a music library's metadata contains.
void refresh_glyphs(Renderer& r, const std::vector<int>& sizesPx,
                    const std::vector<std::string>& scan);

// Bakes whatever the last frame asked for and did not have, and re-uploads.
//
// Call this at the TOP of the frame, before any quad is emitted: baking grows
// the atlas and moves cells, so quads built before it would sample a layout
// that no longer exists. A glyph is missing for exactly one frame at a size
// nobody predicted (button labels derive their size from box geometry), and
// never again.
void bake_glyph_misses(Renderer& r);

// Opens the real faces off disk (no GPU, no window) and asserts, through
// `check`, that each script is served by the face it should be — which is the
// whole bug: the advances are right or wrong depending on which face answers,
// and nothing about a wrong one looks like an error at runtime.
void glyph_selftest(const std::function<void(bool, const char*)>& check);
