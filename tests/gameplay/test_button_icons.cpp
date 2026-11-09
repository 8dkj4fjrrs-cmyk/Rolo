// Button pictures in text (rolo/gameplay/ButtonIcons.hpp): {button:...} is
// turned into icon tokens for the device in use, and tokens are split out for
// drawing.

#include "rolo/gameplay/ButtonIcons.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace rolo;

TEST_CASE("{button:action} shows what the action is bound to on the device in use", "[gameplay][icons]")
{
    Input input;
    input.defineAction("jump", {Binding::ofKey(Key::Space), Binding::ofPad(GamepadButton::A)});
    CHECK(resolveButtons("Press {button:jump} to jump", input) == "Press {icon:key:Space} to jump");

    GamepadState pad;
    pad.connected = true;
    pad.style = GamepadStyle::PlayStation;
    pad.buttons[static_cast<std::size_t>(GamepadButton::B)] = true;
    input.setGamepad(0, pad);   // the player picks up the gamepad
    CHECK(resolveButtons("Press {button:jump} to jump", input) == "Press {icon:pad:A:playstation} to jump");

    // A button or key by name, and names it does not know (left alone).
    CHECK(resolveButtons("{button:pad start} or {button:escape}", input) == "{icon:pad:Start:playstation} or {icon:key:Escape}");
    CHECK(resolveButtons("{button:nonsense} {gems}", input) == "{button:nonsense} {gems}");
    CHECK(resolveButtons("no buttons", input) == "no buttons");
}

TEST_CASE("Icon tokens are split from the words around them", "[gameplay][icons]")
{
    const auto pieces = splitIcons("Press {icon:pad:Y:nintendo} or {icon:mouse:Right}!");
    REQUIRE(pieces.size() == 5);
    CHECK(pieces[0].text == "Press ");
    REQUIRE(pieces[1].icon);
    CHECK(pieces[1].icon->binding == Binding::ofPad(GamepadButton::Y));
    CHECK(pieces[1].icon->style == GamepadStyle::Nintendo);
    CHECK(pieces[2].text == " or ");
    CHECK(pieces[3].icon->binding == Binding::ofMouse(MouseButton::Right));
    CHECK(pieces[4].text == "!");

    // Not tokens: kept as words.
    const auto plain = splitIcons("{icon:pad:Q} {icon:nope} {icon:key:Space");
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].text == "{icon:pad:Q} {icon:nope} {icon:key:Space");
    CHECK(splitIcons("").size() == 1);
    for (const ButtonIcon& icon : {ButtonIcon{Binding::ofKey(Key::LeftShift)}, ButtonIcon{Binding::ofMouse(MouseButton::Middle)},
                                   ButtonIcon{Binding::ofPad(GamepadButton::RightBumper), GamepadStyle::Xbox}}) {
        const auto back = splitIcons(iconToken(icon));
        REQUIRE(back.size() == 1);
        CHECK(back[0].icon == icon);
    }
}

TEST_CASE("Pad buttons are labelled as they are printed on each kind of pad", "[gameplay][icons]")
{
    CHECK(padButtonLabel(GamepadButton::A, GamepadStyle::Xbox) == "A");
    CHECK(padButtonLabel(GamepadButton::A, GamepadStyle::PlayStation) == "Cross");
    CHECK(padButtonLabel(GamepadButton::A, GamepadStyle::Nintendo) == "B");   // bottom button
    CHECK(padButtonLabel(GamepadButton::LeftBumper, GamepadStyle::PlayStation) == "L1");
    CHECK(padButtonLabel(GamepadButton::Start, GamepadStyle::Xbox) == "Menu");
    CHECK(padButtonLabel(GamepadButton::Up, GamepadStyle::Generic) == "Up");
}
