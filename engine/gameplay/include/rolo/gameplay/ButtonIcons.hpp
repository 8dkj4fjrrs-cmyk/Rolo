#pragma once

// Button pictures in text.
//
// A Text (or a dialogue line, or a script's setText) may say
//
//     "Press {button:jump} to jump"     an action: what it is bound to now
//     "{button:pad a}", "{button:space}" one button or key
//
// While the game runs, Game turns each {button:...} into an icon token for
// the device the player is using (the gamepad's button, drawn as it looks on
// that kind of pad, or the keyboard key), e.g. "{icon:pad:A:playstation}".
// The runtime draws icon tokens as small pictures in the line of text
// (SceneRenderer). This header is shared by both, and tested without a window.

#include "rolo/gameplay/Input.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

/// What one icon shows.
struct ButtonIcon {
    Binding binding;
    GamepadStyle style = GamepadStyle::Generic;   // pad buttons: drawn as on this kind of pad
    friend bool operator==(const ButtonIcon&, const ButtonIcon&) = default;
};

/// The token for an icon: "{icon:key:Space}", "{icon:mouse:Left}", "{icon:pad:A:xbox}".
std::string iconToken(const ButtonIcon& icon);

/// A piece of a line of text: words, or an icon.
struct TextPiece {
    std::string text;                 // when not an icon
    std::optional<ButtonIcon> icon;
    friend bool operator==(const TextPiece&, const TextPiece&) = default;
};

/// Splits text into words and icons. Text without icon tokens is one piece.
std::vector<TextPiece> splitIcons(std::string_view text);

/// What a pad button is called on a kind of pad, e.g. A on PlayStation is
/// "Cross", LB is "L1", Start is "Options". For drawing and for messages.
std::string_view padButtonLabel(GamepadButton button, GamepadStyle style);

/// Replaces each {button:name} in `text` with an icon token, using the
/// actions and the device last used in `input`. Unknown names stay as written.
std::string resolveButtons(std::string_view text, const Input& input);

} // namespace rolo
