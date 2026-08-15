#include "fonts.hh"

#include "platform.hh"
#include "renderer.hh"
#include "utf8.hh"

// The concrete asset reader, for glyph_selftest() only — everything else here
// takes an AssetReader& from its caller. Same split the host layer uses.
// Android has no FileAssetReader — assets live inside the APK, reached through
// AAssetManager — and no selftest either: it is a desktop dev harness run from
// a terminal. So neither header exists there and neither is needed.
#ifndef __ANDROID__
#ifdef _WIN32
#include "win32_platform.hh"
#else
#include "wayland_platform.hh"
#endif
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

Font       g_font;
bool       g_font_ok = false;
RasterFont g_text;
bool       g_text_ok = false;

namespace {
constexpr const char* kFontRegular = "fonts/NewCM10-Book.otf";
constexpr const char* kFontBold    = "fonts/NewCM10-Bold.otf";
constexpr const char* kFontItalic  = "fonts/NewCM10-BookItalic.otf";

// Song / Mincho / Batang: the Ming-Mincho-Myeongjo serif tradition, whose
// stroke contrast and terminal serifs read as one family with New Computer
// Modern's serif Latin. The sans cuts bundled beside them (Hei, Gothic,
// Dotum) are deliberately not registered — whichever face won a codepoint
// would decide the look, and a result table would come out in two typefaces.
//
// Chinese -> Japanese -> Korean, and the order matters: all three cover Han
// and Kana, and only the Korean face has Hangul.
constexpr const char* kFallbacksRegular[] = {
    "fonts/FandolSong-Regular.otf",
    "fonts/HaranoAjiMincho-Regular.otf",
    "fonts/UnBatang.ttf",
};
// The matched Bold cuts. Table headers and album titles draw Bold, so without
// these a Korean or Chinese title renders at regular weight beside bold Latin
// — the text is there, at the wrong weight, which is easy to miss.
constexpr const char* kFallbacksBold[] = {
    "fonts/FandolSong-Bold.otf",
    "fonts/HaranoAjiMincho-Bold.otf",
    "fonts/UnBatangBold.ttf",
};

// The sizes currently baked, and every codepoint ever asked for. Both are
// process-wide because the atlas is: one RasterFont serves whichever Renderer
// is drawing.
std::vector<int>      g_sizes;
std::vector<uint32_t> g_seen;
bool                  g_seen_dirty = false;

// Codepoints worth baking whether or not any text on screen uses them yet:
// cheap, bounded, and enough to cover most European-language metadata outright
// with no scan involved. Cyrillic is here specifically because it was the bug
// — left out, it fell through to a CJK face's full-width metrics.
void appendBaseCharset(std::vector<uint32_t>& cps) {
    for (uint32_t cp = 0x0020; cp <= 0x00FF; cp++) cps.push_back(cp);  // ASCII + Latin-1
    for (uint32_t cp = 0x0100; cp <= 0x024F; cp++) cps.push_back(cp);  // Latin Ext-A/B
    for (uint32_t cp = 0x0370; cp <= 0x04FF; cp++) cps.push_back(cp);  // Greek + Cyrillic

    // General punctuation this UI's own prose and real metadata actually use.
    // A missing em dash is worse than a wrong glyph: nothing about a blank gap
    // says a character is missing, so the title just reads broken.
    static const uint32_t kPunct[] = {
        0x2010, 0x2013, 0x2014,          // hyphen, en dash, em dash
        0x2018, 0x2019, 0x201C, 0x201D,  // curly quotes (and the apostrophe
                                         // Unicode-correct metadata uses)
        0x2020, 0x2021, 0x2022, 0x2026,  // daggers, bullet, ellipsis
        0x2032, 0x2033, 0x2039, 0x203A,  // primes, single guillemets
        0x2190, 0x2192,                  // arrows
        0x20AC, 0x2212, 0x221A, 0x03C0,  // euro, minus, radical, pi
    };
    cps.insert(cps.end(), std::begin(kPunct), std::end(kPunct));
}

void dedupe(std::vector<uint32_t>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}
}  // namespace

void init_fonts(AssetReader& assets, const std::string& stale_cache) {
    // The MTSDF atlas cache this build no longer writes — tens of megabytes,
    // and a way to serve a bake made by a different font set.
    if (!stale_cache.empty()) {
        std::error_code ec;
        if (std::filesystem::remove(stale_cache, ec))
            std::printf("[fonts] removed stale MSDF atlas cache %s\n", stale_cache.c_str());
    }

    std::vector<uint8_t> bytes;
    if (assets.read(kFontRegular, bytes))
        g_font_ok = g_font.loadFromMemory(bytes.data(), bytes.size());

    if (g_text.open(assets, kFontRegular)) {
        g_text.addStyle(assets, kFontBold,   FontStyle::Bold);
        g_text.addStyle(assets, kFontItalic, FontStyle::Italic);
        for (const char* f : kFallbacksRegular) g_text.addFallback(assets, f);
        for (const char* f : kFallbacksBold)    g_text.addFallback(assets, f, FontStyle::Bold);
        g_text_ok = g_text.valid();
    }
    if (!g_text_ok)
        std::fprintf(stderr, "[!] glyph atlas unavailable — curve/stroke text only\n");
}

void upload_glyphs(Renderer& r) {
    if (!g_text_ok) return;
    r.initMsdf(g_text);
}

void refresh_glyphs(Renderer& r, const std::vector<int>& sizesPx,
                    const std::vector<std::string>& scan) {
    if (!g_text_ok) return;

    if (g_seen.empty()) {
        appendBaseCharset(g_seen);
        g_seen_dirty = true;
    }
    for (const std::string& s : scan) {
        for (size_t i = 0; i < s.size();) {
            uint32_t cp = ::utf8::nextCodepoint(s, i);
            // Below U+0100 is already in the base set; above it is what a scan
            // is for (CJK, Hangul, Kana, and anything else metadata carries).
            if (cp >= 0x100) { g_seen.push_back(cp); g_seen_dirty = true; }
        }
    }
    if (g_seen_dirty) dedupe(g_seen);

    // Cells are keyed by integer size, so a resize that moves the type scale
    // adds a whole new set and nothing ever removes the old one. Start the
    // sheet over instead: re-baking is tens of microseconds a glyph, and
    // unlike an LRU it cannot drop a cell a live quad still points at.
    bool sizes_changed = (sizesPx != g_sizes);
    if (sizes_changed) {
        if (!g_sizes.empty()) g_text.reset();
        g_sizes = sizesPx;
    }
    if (!sizes_changed && !g_seen_dirty && r.msdfReady()) return;
    g_seen_dirty = false;

    if (g_text.ensureGlyphs(g_seen, g_sizes) > 0 || !r.msdfReady())
        upload_glyphs(r);
}

void bake_glyph_misses(Renderer& r) {
    if (!g_text_ok || !g_text.hasMisses()) return;
    if (g_text.bakeMisses() > 0) upload_glyphs(r);
}

void glyph_selftest(const std::function<void(bool, const char*)>& check) {
#ifdef __ANDROID__
    // Desktop-only harness: it opens the faces off the filesystem, and there
    // is no --selftest entry point on Android to reach it anyway.
    (void)check;
#else
    FileAssetReader assets;
    RasterFont font;
    if (!font.open(assets, kFontRegular)) {
        check(false, "the primary face opens (run from a build dir with assets/fonts/)");
        return;
    }
    font.addStyle(assets, kFontBold, FontStyle::Bold);
    for (const char* f : kFallbacksRegular) font.addFallback(assets, f);
    for (const char* f : kFallbacksBold)    font.addFallback(assets, f, FontStyle::Bold);

    const float kSize = 64.0f;
    const std::vector<int> sizes = {(int)kSize};
    // 'A', 'А' (U+0410), 'я' (U+044F), 一, あ, 가.
    std::vector<uint32_t> cps = {0x41, 0x410, 0x44F, 0x4E00, 0x3042, 0xAC00};
    (void)font.ensureGlyphs(cps, sizes);

    auto em = [&](uint32_t cp) { return font.advance(cp, kSize) / kSize; };

    // The bug, stated as a number. NewCM10 gives U+0410 0.750 em; FandolSong
    // gives it a full-width 1.000 em, because a CJK face's Cyrillic is drawn on
    // the CJK body. Anything at or near 1.0 here means a fallback face won a
    // codepoint the primary covers, and every Russian title on screen is
    // spread ~1.5x too wide.
    check(em(0x410) > 0.6f && em(0x410) < 0.85f,
         "Cyrillic А is served by NewCM10's metrics, not a full-width CJK face");
    check(em(0x44F) > 0.4f && em(0x44F) < 0.65f,
         "Cyrillic я keeps its narrow advance (0.532 em), not 1.0");
    check(em(0x41) > 0.6f && em(0x41) < 0.85f,
         "Latin A is unchanged — the primary face still serves Latin");

    // Han/Kana/Hangul have no business being narrow, and had no business being
    // absent: under the old single-sheet atlas they baked nothing at all and
    // drew as zero-width blanks.
    check(em(0x4E00) > 0.9f, "Han 一 is full-width, i.e. a real CJK face served it");
    check(em(0x3042) > 0.9f, "Kana あ is full-width");
    check(em(0xAC00) > 0.9f, "Hangul 가 is full-width (only UnBatang has it)");
    check(font.hasCodepoint(0x4E00) && font.hasCodepoint(0x3042) && font.hasCodepoint(0xAC00),
         "Han, Kana and Hangul all reached the atlas");

    // A glyph with no cell draws no ink AND no advance, which silently breaks
    // truncation and centring as well as the text itself. Zero advance for
    // something we just baked would mean the bake did not take.
    check(font.cellCount() >= cps.size(), "every requested codepoint got a cell");
#endif // !__ANDROID__
}
