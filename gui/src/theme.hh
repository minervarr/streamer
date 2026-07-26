#pragma once
// Streamer's visual identity — single source of truth, shared by every
// screen. Stub for the skeleton phase (Phase 1): just the background/text
// colors so gui_main.cc compiles. The real palette (TypeScale, gradient
// helper, shared button primitive) lands in Phase 2 once gradient-fill
// support in the engine has been investigated.

#include "canvas.hh"

namespace theme {

inline constexpr Color kBackground = {0.0f, 0.0f, 0.0f, 1.0f};  // #000000
inline constexpr Color kText       = {1.0f, 1.0f, 1.0f, 1.0f};  // #ffffff

} // namespace theme
