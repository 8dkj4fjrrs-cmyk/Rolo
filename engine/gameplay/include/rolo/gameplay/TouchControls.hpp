#pragma once

// On-screen controls for touch screens: a stick for moving and a round
// button for each of the game's actions ("jump", "interact", ...).
//
// They need nothing from the game's maker: the buttons are made from the
// actions the game defines, and pressing one is the same as pressing a key
// bound to that action, so a game that asks input.isDown("jump") or uses
// Rolo's own movement and dialogue is playable on a phone as it is.
//
// This class is the logic only (where the controls are, which finger holds
// which): the program running the game reports fingers to it in screen
// coordinates and draws what it describes. Fingers that are not on a
// control are passed on to the game (Input::touches).
//
// Screen coordinates here: (0, 0) is the bottom-left corner, y is up, and
// the screen is `width` × `height` in the game's own units (its resolution).

#include "rolo/gameplay/Input.hpp"
#include "rolo/scene/ScreenFit.hpp"

#include <string>
#include <vector>

namespace rolo {

struct TouchButton {
    std::string action;
    std::string label;      // what is written on it, e.g. "Jump"
    Vec2 center;
    float radius = 0.0f;
    bool menu = false;      // a menu button (pause): small, out of the way at the top
    bool down = false;
};

struct TouchStick {
    Vec2 rest;              // where its base is drawn while not held
    Vec2 base;              // where the base is now (under the finger's first touch)
    Vec2 knob;              // where the knob is now
    float radius = 0.0f;    // of the base
    bool held = false;
};

class TouchControls {
public:
    /// Chooses the buttons from the actions `input` knows: every action but
    /// the four move actions, in the order they were defined (at most six).
    /// Actions called "pause" or "menu" become small buttons at the top.
    /// `only`, if not empty, lists the actions to make buttons for instead.
    void setActions(const Input& input, const std::vector<std::string>& only = {});
    /// The actions buttons were made for.
    std::vector<std::string> actions() const;

    /// Places the controls on a screen of this size. Call again when the
    /// size changes or the actions do. `insets`: strips along the edges to
    /// keep clear (a phone's camera notch); the controls move in past them.
    void layout(float width, float height, ScreenInsets insets = {});

    /// A finger went down, moved, or lifted. touchBegan answers whether a
    /// control took it; such a finger is then not the game's.
    bool touchBegan(int id, Vec2 screen);
    void touchMoved(int id, Vec2 screen);
    void touchEnded(int id);
    /// True if a control holds this finger.
    bool owns(int id) const noexcept;
    /// Lets go of everything (the window lost focus, the scene changed).
    void releaseAll();

    /// Tells `input` what is held: the stick's direction and each action's button.
    void apply(Input& input) const;

    const std::vector<TouchButton>& buttons() const noexcept { return m_buttons; }
    const TouchStick& stick() const noexcept { return m_stick; }
    /// Goes up whenever what should be drawn changed; layoutRevision only
    /// when the controls themselves did (which buttons, where, how large).
    unsigned revision() const noexcept { return m_revision; }
    unsigned layoutRevision() const noexcept { return m_layoutRevision; }

private:
    struct Held {
        int id = 0;
        int button = -1;   // index into m_buttons, or -1: the stick
    };
    std::vector<TouchButton> m_buttons;
    TouchStick m_stick;
    std::vector<Held> m_held;
    float m_width = 0.0f;
    float m_height = 0.0f;
    ScreenInsets m_insets;
    unsigned m_revision = 0;
    unsigned m_layoutRevision = 0;
};

} // namespace rolo
