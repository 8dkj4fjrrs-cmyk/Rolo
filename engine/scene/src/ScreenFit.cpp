#include "rolo/scene/ScreenFit.hpp"

#include <algorithm>

namespace rolo {

std::string_view screenFitName(ScreenFit fit)
{
    return fit == ScreenFit::Expand ? "expand" : "bars";
}

std::optional<ScreenFit> screenFitFromName(std::string_view name)
{
    if (name == "bars") {
        return ScreenFit::Bars;
    }
    if (name == "expand") {
        return ScreenFit::Expand;
    }
    return std::nullopt;
}

Vec2 visibleSize(Vec2 gameSize, Vec2 windowSize, ScreenFit fit)
{
    if (fit == ScreenFit::Bars || gameSize.x <= 0.0f || gameSize.y <= 0.0f || windowSize.x <= 0.0f
        || windowSize.y <= 0.0f) {
        return gameSize;
    }
    const float window = windowSize.x / windowSize.y;
    const float game = gameSize.x / gameSize.y;
    // Never more than three times the game size along a side: a very long
    // window would otherwise show absurdly much (and a strip of a game).
    if (window > game) {
        return {std::min(gameSize.y * window, gameSize.x * 3.0f), gameSize.y};
    }
    return {gameSize.x, std::min(gameSize.x / window, gameSize.y * 3.0f)};
}

ScreenPin pinToScreen(Vec2 place, Vec2 coverage, Vec2 gameSize, Vec2 visible)
{
    ScreenPin pin;
    const Vec2 extra{(visible.x - gameSize.x) * 0.5f, (visible.y - gameSize.y) * 0.5f};
    const auto side = [](float at, float size) { return at < -size / 6.0f ? -1.0f : at > size / 6.0f ? 1.0f : 0.0f; };
    const bool wide = coverage.x >= gameSize.x * 0.98f && gameSize.x > 0.0f;
    const bool tall = coverage.y >= gameSize.y * 0.98f && gameSize.y > 0.0f;
    if (wide) {
        pin.scale.x = visible.x / gameSize.x;   // covers the width: keeps covering it
    } else {
        pin.offset.x = side(place.x, gameSize.x) * extra.x;
    }
    if (tall) {
        pin.scale.y = visible.y / gameSize.y;
    } else {
        pin.offset.y = side(place.y, gameSize.y) * extra.y;
    }
    return pin;
}

} // namespace rolo
