#pragma once
// Streamer's visual identity — single source of truth, shared by every
// screen (mirrors scanersito's gui/src/theme.hh pattern). Black background,
// white text, everything else (buttons, highlights, sliders, borders) drawn
// with the green->red accent gradient.
//
// Gradient fills need Canvas::useShapes() (the SDF fast path) — see
// Canvas::rectGradient()'s doc comment in canvas.hh. Callers must have set
// canvas.useShapes(&shapeVerts) before drawing gradient UI, and pass
// shapeVerts to Renderer::draw()'s shapeVerts argument.

#include "canvas.hh"
#include "widgets.hh"

namespace theme {

// ── Palette ──────────────────────────────────────────────────────────────────
inline constexpr Color kBackground = {0.0f, 0.0f, 0.0f, 1.0f};  // #000000
inline constexpr Color kText       = {1.0f, 1.0f, 1.0f, 1.0f};  // #ffffff
inline constexpr Color kAccentLo   = {0.0f, 1.0f, 0.0f, 1.0f};  // #00ff00
inline constexpr Color kAccentHi   = {1.0f, 0.0f, 0.0f, 1.0f};  // #ff0000

// Panels/tracks/dim text need to read against pure black without being
// invisible or clashing with the gradient — desaturated grays, not part of
// the accent family.
inline constexpr Color kPanel = {0.12f, 0.12f, 0.12f, 1.0f};
inline constexpr Color kTrack = {0.22f, 0.22f, 0.22f, 1.0f};
inline constexpr Color kDim   = {0.55f, 0.55f, 0.55f, 1.0f};

// Widget style structs wired to the palette above, ready to pass to any
// widgets:: draw call in place of the k*Default ones.
inline constexpr widgets::ToggleStyle kToggle{kAccentLo, kTrack, kText};
inline constexpr widgets::StepperStyle kStepper{kPanel, kText, kTrack, kText};
inline constexpr widgets::SliderStyle kSlider{kTrack, kAccentLo, kText, kDim};
inline constexpr widgets::SegmentedStyle kSegmented{kAccentLo, kPanel, kBackground, kDim};
inline constexpr widgets::DropdownStyle kDropdown{kText, kTrack, kPanel, kText, kDim};
inline constexpr widgets::ScrollListStyle kScrollList{
    /*background=*/kPanel, /*rowText=*/kText, /*hoverBg=*/kTrack,
    /*selection=*/widgets::ListSelectionStyle::Pill,
    /*pillColor=*/kAccentLo, /*pillText=*/kBackground,
    /*borderSelected=*/kAccentLo, /*borderUnselected=*/kDim,
};

// ── Type scale ───────────────────────────────────────────────────────────────
// One scale computed from window height (fractions, not fixed pixels) so
// text stays proportionate across window sizes — same idea as scanersito's
// TypeScale.
struct TypeScale {
  float display, title, body, small, caption;

  static TypeScale fromHeight(float h) {
    return {h * 0.050f, h * 0.032f, h * 0.022f, h * 0.017f, h * 0.013f};
  }
};

// ── Gradient helper ──────────────────────────────────────────────────────────
// Draws a rect filled with the accent gradient (green -> red). `t0`/`t1` let
// a caller show only a slice of the gradient (e.g. a progress bar fills
// green->red as it advances, rather than every bar showing the full range).
inline Color lerp(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

inline void gradientRect(Canvas& c, float x, float y, float w, float h,
                         float radius = 0.0f, float t0 = 0.0f, float t1 = 1.0f) {
  Color from = lerp(kAccentLo, kAccentHi, t0);
  Color to   = lerp(kAccentLo, kAccentHi, t1);
  c.rectGradient(x, y, w, h, from, to, Canvas::GradientDir::Horizontal, radius);
}

// Shared button primitive: gradient fill, white label, used for every
// primary action across both screens (Search's "Download Selected", "?"
// cheatsheet toggle, Settings' "Save", etc). Secondary/inactive buttons
// should use widgets::drawFitButton with kPanel/kTrack instead — the
// gradient is reserved for the primary action per screen so it doesn't lose
// meaning by appearing everywhere.
inline void accentButton(Canvas& c, float x, float y, float w, float h,
                         std::string_view label, float radius = 0.0f) {
  gradientRect(c, x, y, w, h, radius);
  float s = h * 0.34f;
  c.textCentered(label, x + w * 0.5f, y + (h - s) * 0.5f, s, kText);
}

} // namespace theme
