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

/// The strips along a screen's edges that are not safe to put things in:
/// a phone's camera notch or hole is there. In the same units as the
/// screen's size they go with; all 0 on screens without such things.
struct ScreenInsets {
    float left = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float top = 0.0f;

    bool any() const noexcept { return left > 0.0f || right > 0.0f || bottom > 0.0f || top > 0.0f; }
    friend bool operator==(const ScreenInsets&, const ScreenInsets&) = default;
};

/// A screen's insets as the system gives them (`pixels`, for a screen of
/// `frame` pixels) in the units of what the screen shows (`visible`, drawn
/// as large as fits, centred). Where bars beside the picture already cover
/// a notch, nothing is left of that inset.
ScreenInsets insetsInView(ScreenInsets pixels, Vec2 frame, Vec2 visible);

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
/// `insets`: an object kept at an edge stays that much further in, clear
/// of a notch there. Stretched rectangles still cover the whole screen.
ScreenPin pinToScreen(Vec2 place, Vec2 coverage, Vec2 gameSize, Vec2 visible, ScreenInsets insets = {});

} // namespace rolo
