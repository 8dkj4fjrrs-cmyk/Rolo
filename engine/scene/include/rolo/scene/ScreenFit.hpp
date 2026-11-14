#pragma once

// A game on a screen of another shape (ADR 0027).
//
// A game is made for one size (the project's width × height, 1280 × 720
// unless changed): its "game size". Screens differ. A project chooses what
// happens on a screen of another shape:
//
//   Bars    the whole game size is shown, as large as fits, with bars
//           beside or above and below it. Every player sees exactly the same.
//   Expand  the picture fills the screen: the game size is always visible,
//           and a wider (or taller) screen shows more of the world beside
//           (or above and below) it.
//
// With Expand, things fixed to the camera (scores, menus: children of the
// Camera object) must follow the screen's edges; pinToScreen says how.

#include "rolo/scene/Types.hpp"

#include <optional>
#include <string_view>

namespace rolo {

enum class ScreenFit { Bars, Expand };

/// "bars" or "expand" (as written in project.rolo).
std::string_view screenFitName(ScreenFit fit);
std::optional<ScreenFit> screenFitFromName(std::string_view name);

/// How much of the world a window of this shape shows, in the game's own
/// units (at camera zoom 1). Bars: always the game size. Expand: the game
/// size grown along one side to the window's shape.
Vec2 visibleSize(Vec2 gameSize, Vec2 windowSize, ScreenFit fit);

/// How an object fixed to the camera moves when the screen shows more than
/// the game size.
struct ScreenPin {
    Vec2 offset;              // added to its place, in screen units (divide by the camera's zoom)
    Vec2 scale{1.0f, 1.0f};   // multiplied into its scale (full-screen rectangles stretch)
};

/// `place`: where the object is relative to the camera, in screen units
/// (its position × the camera's zoom). The screen is thought of as three
/// columns and three rows of the game size: an object in the left column
/// stays as far from the left edge as it was made, one in the right column
/// from the right edge, one in the middle stays in the middle; the same up
/// and down. `coverage`: the object's size if it is a filled rectangle
/// (else 0, 0): one that covers the whole game size along a side (a
/// darkening veil, a backdrop) is stretched to cover the whole screen.
ScreenPin pinToScreen(Vec2 place, Vec2 coverage, Vec2 gameSize, Vec2 visible);

} // namespace rolo
