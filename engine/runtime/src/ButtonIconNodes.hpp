#pragma once

// Draws button pictures (rolo/gameplay/ButtonIcons.hpp) as Axmol nodes: a
// keyboard key as a key cap with its name, a mouse with the button lit,
// gamepad buttons as they look on that kind of pad (Xbox's coloured letters,
// PlayStation's shapes, Nintendo's letters), bumpers, menu buttons, sticks
// and the d-pad. Everything is drawn with shapes and Rolo's own font, so no
// image files are needed (and nothing belongs to a console maker).

#include "rolo/gameplay/ButtonIcons.hpp"

#include <string>

namespace ax {
class Node;
}

namespace rolo::detail {

/// A node `height` tall (its width depends on the button) whose bottom-left
/// corner is at its origin; its content size is set. `font` is a .ttf file
/// for the labels.
ax::Node* makeButtonIcon(const ButtonIcon& icon, float height, const std::string& font);

} // namespace rolo::detail
