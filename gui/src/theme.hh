#pragma once
// Streamer's visual identity — single source of truth, shared by every
// screen. Black background, white text; every button shares one flat
// treatment (a `kPanel` fill plus a thin accent-colored bar) instead of a
// gradient fill — a large solid-bright button is the worst case for OLED
// panels (constant per-pixel current draw, plus burn-in risk on static
// chrome like buttons and headers), so fills stay dark and only a 2-4px bar
// carries color. `kAccent` marks primary/interactive actions; `kDanger` is
// reserved for destructive actions and error text only.

#include "canvas.hh"
#include "widgets.hh"

#include <algorithm>

namespace theme {

// ── Palette ──────────────────────────────────────────────────────────────────
inline constexpr Color kBackground = {0.0f, 0.0f, 0.0f, 1.0f};  // #000000
inline constexpr Color kText       = {1.0f, 1.0f, 1.0f, 1.0f};  // #ffffff
inline constexpr Color kAccent     = col::green;  // single primary-action accent
inline constexpr Color kDanger     = col::red;    // destructive actions / errors only

// Panels/tracks/dim text need to read against pure black without being
// invisible or clashing with the accent — desaturated grays, not part of
// the accent family.
inline constexpr Color kPanel = {0.12f, 0.12f, 0.12f, 1.0f};
inline constexpr Color kTrack = {0.22f, 0.22f, 0.22f, 1.0f};
inline constexpr Color kDim   = {0.55f, 0.55f, 0.55f, 1.0f};

// Widget style structs wired to the palette above, ready to pass to any
// widgets:: draw call in place of the k*Default ones.
inline constexpr widgets::ToggleStyle kToggle{kAccent, kTrack, kText};
inline constexpr widgets::StepperStyle kStepper{kPanel, kText, kTrack, kText};
inline constexpr widgets::SliderStyle kSlider{kTrack, kAccent, kText, kDim};
inline constexpr widgets::SegmentedStyle kSegmented{kAccent, kPanel, kBackground, kDim};
inline constexpr widgets::DropdownStyle kDropdown{kText, kTrack, kPanel, kText, kDim};
inline constexpr widgets::TextFieldStyle kTextField{kTrack, kText, kDim, kAccent};
inline constexpr widgets::ScrollListStyle kScrollList{
    /*background=*/kPanel, /*rowText=*/kText, /*hoverBg=*/kTrack,
    /*selection=*/widgets::ListSelectionStyle::Pill,
    /*pillColor=*/kAccent, /*pillText=*/kBackground,
    /*borderSelected=*/kAccent, /*borderUnselected=*/kDim,
    /*selectedBar=*/kAccent,
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

// ── Buttons ──────────────────────────────────────────────────────────────────
// One flat, OLED-friendly treatment for every button in the app: a `kPanel`
// fill (never a large bright area) plus a thin accent-colored bar marking
// what kind of action it is — Primary (the one main action per screen/row),
// Secondary (everything else) or Danger (destructive). Hover/press lighten
// the bar and step the fill to `kTrack`, momentarily — nothing stays bright.
enum class ButtonKind { Primary, Secondary, Danger };

struct ButtonState {
  bool hovered  = false;
  bool pressed  = false;
  bool disabled = false;
};

// Nudges a color toward white by `t` (0..1) — used only to lift the accent
// bar a little on hover/press, never to fill the button itself.
inline Color brighten(Color c, float t) {
  return {c.r + (1.0f - c.r) * t, c.g + (1.0f - c.g) * t,
          c.b + (1.0f - c.b) * t, c.a};
}

inline void button(Canvas& c, float x, float y, float w, float h,
                   std::string_view label, ButtonKind kind = ButtonKind::Secondary,
                   ButtonState state = {}, float radius = 0.0f) {
  Color barColor = kind == ButtonKind::Primary ? kAccent
                  : kind == ButtonKind::Danger  ? kDanger
                                                : kDim;
  Color bg = kPanel;
  Color labelColor = kText;
  if (state.disabled) {
    barColor = kDim;
    labelColor = kDim;
  } else if (state.pressed) {
    bg = kTrack;
    barColor = brighten(barColor, 0.65f);
  } else if (state.hovered) {
    bg = kTrack;
    barColor = brighten(barColor, 0.35f);
  }
  c.rect(x, y, w, h, bg, radius);
  float barThick = std::clamp(h * 0.06f, 2.0f, 4.0f);
  c.rect(x, y, barThick, h, barColor, radius);
  float s = h * 0.34f;
  c.textCentered(label, x + w * 0.5f, y + (h - s) * 0.5f, s, labelColor);
}

} // namespace theme
