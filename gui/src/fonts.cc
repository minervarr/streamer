#include "fonts.hh"

#include "platform.hh"
#include "renderer.hh"
#include "utf8.hh"

#include <cstdint>
#include <cstdio>
#include <vector>

Font     g_font;
bool     g_font_ok = false;
MsdfFont g_msdf;
bool     g_msdf_ok = false;

namespace {
constexpr const char* kFontRegular = "fonts/NewCM10-Book.otf";
constexpr const char* kFontBold    = "fonts/NewCM10-Bold.otf";
constexpr const char* kFontItalic  = "fonts/NewCM10-BookItalic.otf";

// NewCM10 only covers Latin + math, so search queries/results in Chinese,
// Japanese or Korean render blank without these. Tried in this order because
// FandolHei/HaranoAjiGothic both carry the common CJK Unified Ideographs
// block — whichever loads first would win them — so Han goes to Fandol
// (Simplified Chinese shapes) and HaranoAji only fills what's left (Kana,
// Japanese-specific forms); Hangul has no overlap so order doesn't matter.
constexpr const char* kFontCjkFallbacks[] = {
    "fonts/FandolHei-Regular.otf",
    "fonts/HaranoAjiGothic-Regular.otf",
    "fonts/UnDotum.ttf",
};
}  // namespace

void init_fonts(AssetReader& assets, const std::string& cache) {
    std::vector<uint8_t> bytes;
    if (assets.read(kFontRegular, bytes))
        g_font_ok = g_font.loadFromMemory(bytes.data(), bytes.size());

    if (g_msdf.generate(assets, kFontRegular, cache.c_str())) {
        bool added = false;
        if (!g_msdf.hasStyle(FontStyle::Bold)) {
            g_msdf.ensureAtlasLoaded(cache.c_str());
            added |= g_msdf.addStyle(assets, kFontBold, FontStyle::Bold);
        }
        if (!g_msdf.hasStyle(FontStyle::Italic)) {
            g_msdf.ensureAtlasLoaded(cache.c_str());
            added |= g_msdf.addStyle(assets, kFontItalic, FontStyle::Italic);
        }
        if (added) g_msdf.saveCache(cache.c_str());
        g_msdf_ok = g_msdf.valid();
    }
    if (!g_msdf_ok)
        std::fprintf(stderr, "[!] MSDF unavailable — curve/stroke text only\n");
}

void upload_msdf(Renderer& r, const std::string& cache) {
    if (!g_msdf_ok) return;
    g_msdf.ensureAtlasLoaded(cache.c_str());
    r.initMsdf(g_msdf);
    g_msdf.releaseAtlasPixels();
}

bool ensure_glyphs(AssetReader& assets, const std::string& cache, const std::string& utf8) {
    if (!g_msdf_ok) return false;

    std::vector<uint32_t> missing;
    for (size_t i = 0; i < utf8.size();) {
        uint32_t cp = ::utf8::nextCodepoint(utf8, i);
        if (cp >= 0x80 && !g_msdf.hasCodepoint(cp)) missing.push_back(cp);
    }
    if (missing.empty()) return false;

    g_msdf.ensureAtlasLoaded(cache.c_str());
    int newlyBaked = 0;
    for (const char* fallback : kFontCjkFallbacks)
        newlyBaked += g_msdf.bakeCodepoints(assets, fallback, missing);

    if (newlyBaked > 0) g_msdf.saveCache(cache.c_str());
    return newlyBaked > 0;
}
