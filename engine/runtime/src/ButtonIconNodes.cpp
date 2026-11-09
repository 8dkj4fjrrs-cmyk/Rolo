#include "ButtonIconNodes.hpp"

#include "axmol.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rolo::detail {

namespace {

ax::Color4F colour(unsigned hex, float alpha = 1.0f)
{
    return {static_cast<float>((hex >> 16) & 0xff) / 255.0f, static_cast<float>((hex >> 8) & 0xff) / 255.0f,
            static_cast<float>(hex & 0xff) / 255.0f, alpha};
}

const ax::Color4F kDark = colour(0x26262e);
const ax::Color4F kDarkEdge = colour(0x101014);
const ax::Color4F kCap = colour(0xececf1);
const ax::Color4F kCapShadow = colour(0x8f8f9c);
const ax::Color4F kInk = colour(0x1c1c22);
const ax::Color4F kWhite = colour(0xffffff);
const ax::Color4F kLit = colour(0xf2a93b);   // Rolo's orange: the part that is meant
const ax::Color4F kRim = colour(0xa4a4b4);   // a light edge, so dark buttons show on dark backgrounds

// A dark circle with a light rim.
void darkCircle(ax::DrawNode& d, ax::Vec2 c, float r)
{
    d.drawSolidCircle(c, r, 0.0f, 32, kRim);
    d.drawSolidCircle(c, r * 0.9f, 0.0f, 32, kDark);
}

void roundedRect(ax::DrawNode& d, float x0, float y0, float x1, float y1, float r, const ax::Color4F& c)
{
    r = std::min({r, (x1 - x0) * 0.5f, (y1 - y0) * 0.5f});
    d.drawSolidRect({x0 + r, y0}, {x1 - r, y1}, c);
    d.drawSolidRect({x0, y0 + r}, {x1, y1 - r}, c);
    for (const ax::Vec2 corner : {ax::Vec2(x0 + r, y0 + r), ax::Vec2(x1 - r, y0 + r), ax::Vec2(x0 + r, y1 - r),
                                  ax::Vec2(x1 - r, y1 - r)}) {
        d.drawSolidCircle(corner, r, 0.0f, 16, c);
    }
}

// A label centred at (x, y), at most `maxWidth` wide (scaled down if longer).
void addLabel(ax::Node& parent, const std::string& text, const std::string& font, float size, ax::Vec2 at,
              const ax::Color4F& c, float maxWidth = 0.0f)
{
    auto* label = ax::Label::createWithTTF(text, font, size);
    if (label == nullptr) {
        return;
    }
    label->setTextColor(ax::Color4B(c));
    label->setAnchorPoint({0.5f, 0.5f});
    label->setPosition(at);
    if (maxWidth > 0.0f && label->getContentSize().width > maxWidth) {
        label->setScale(maxWidth / label->getContentSize().width);
    }
    parent.addChild(label);
}

float labelWidth(const std::string& text, const std::string& font, float size)
{
    auto* label = ax::Label::createWithTTF(text, font, size);
    return label != nullptr ? label->getContentSize().width : size * 0.6f * static_cast<float>(text.size());
}

// What a key cap says: short names, and symbols where everyone knows them.
std::string keyCapText(Key key)
{
    switch (key) {
    case Key::Left: return "\xE2\x86\x90";    // ←
    case Key::Up: return "\xE2\x86\x91";      // ↑
    case Key::Right: return "\xE2\x86\x92";   // →
    case Key::Down: return "\xE2\x86\x93";    // ↓
    case Key::Enter: return "Enter";
    case Key::Escape: return "Esc";
    case Key::Backspace: return "\xE2\x8C\xAB";   // ⌫
    case Key::LeftShift:
    case Key::RightShift: return "Shift";
    case Key::LeftControl:
    case Key::RightControl: return "Ctrl";
    case Key::LeftAlt:
    case Key::RightAlt: return "Alt";
    default: return std::string(keyName(key));
    }
}

ax::Node* keyCap(Key key, float h, const std::string& font)
{
    const std::string text = keyCapText(key);
    const float size = h * 0.5f;
    const float w = std::max(h, labelWidth(text, font, size) + h * 0.55f);
    auto* d = ax::DrawNode::create();
    const float r = h * 0.18f;
    roundedRect(*d, 0.0f, 0.0f, w, h, r, kCapShadow);           // the key's side
    roundedRect(*d, 0.0f, h * 0.1f, w, h, r, kCap);             // its top
    addLabel(*d, text, font, size, {w * 0.5f, h * 0.56f}, kInk, w - h * 0.25f);
    d->setContentSize({w, h});
    return d;
}

ax::Node* mouse(MouseButton button, float h)
{
    const float w = h * 0.7f;
    auto* d = ax::DrawNode::create();
    roundedRect(*d, 0.0f, 0.0f, w, h, w * 0.5f, kCap);
    const float split = h * 0.58f;
    // The button meant, lit.
    if (button == MouseButton::Middle) {
        roundedRect(*d, w * 0.4f, split + h * 0.06f, w * 0.6f, h * 0.88f, w * 0.1f, kLit);
    } else {
        const float x0 = button == MouseButton::Left ? w * 0.1f : w * 0.5f;
        const float x1 = button == MouseButton::Left ? w * 0.5f : w * 0.9f;
        roundedRect(*d, x0, split, x1, h * 0.92f, w * 0.18f, kLit);
    }
    d->drawLine({w * 0.08f, split}, {w * 0.92f, split}, kCapShadow, h * 0.04f);
    d->drawLine({w * 0.5f, split}, {w * 0.5f, h * 0.97f}, kCapShadow, h * 0.04f);
    d->setContentSize({w, h});
    return d;
}

// A, B, X and Y.
ax::Node* faceButton(GamepadButton b, GamepadStyle style, float h, const std::string& font)
{
    auto* d = ax::DrawNode::create();
    const ax::Vec2 c{h * 0.5f, h * 0.5f};
    const float r = h * 0.48f;
    if (style == GamepadStyle::Xbox) {
        // Coloured buttons with white letters (dark on yellow).
        const unsigned fills[] = {0x45a845, 0xd8392f, 0x2f7bd0, 0xf0c419};
        const auto i = static_cast<std::size_t>(b);
        d->drawSolidCircle(c, r, 0.0f, 32, kDarkEdge);
        d->drawSolidCircle(c, r * 0.88f, 0.0f, 32, colour(fills[i]));
        addLabel(*d, std::string(gamepadButtonName(b)), font, h * 0.56f, c, b == GamepadButton::Y ? kInk : kWhite);
    } else if (style == GamepadStyle::PlayStation) {
        // Dark buttons with the four symbols, in their colours.
        darkCircle(*d, c, r);
        const float s = h * 0.2f;
        const float t = h * 0.065f;
        switch (b) {
        case GamepadButton::A:   // cross
            d->drawLine(c + ax::Vec2(-s, -s), c + ax::Vec2(s, s), colour(0x8fb4f0), t);
            d->drawLine(c + ax::Vec2(-s, s), c + ax::Vec2(s, -s), colour(0x8fb4f0), t);
            break;
        case GamepadButton::B:   // circle
            d->drawCircle(c, s * 1.05f, 0.0f, 32, false, colour(0xf07a7a), t);
            break;
        case GamepadButton::X:   // square
            d->drawRect(c + ax::Vec2(-s, -s), c + ax::Vec2(s, s), colour(0xf09ad0), t);
            break;
        default: {   // triangle
            const std::array<ax::Vec2, 3> tri{c + ax::Vec2(0.0f, s * 1.1f), c + ax::Vec2(s * 1.05f, -s * 0.75f),
                                              c + ax::Vec2(-s * 1.05f, -s * 0.75f)};
            for (std::size_t k = 0; k < 3; ++k) {
                d->drawLine(tri[k], tri[(k + 1) % 3], colour(0x6fe0b4), t);
            }
            break;
        }
        }
    } else {
        // Nintendo and others: dark buttons with the letter printed on that pad.
        darkCircle(*d, c, r);
        addLabel(*d, std::string(padButtonLabel(b, style)), font, h * 0.56f, c, kWhite);
    }
    d->setContentSize({h, h});
    return d;
}

ax::Node* dpad(GamepadButton direction, float h)
{
    auto* d = ax::DrawNode::create();
    const float arm = h * 0.36f;   // the width of each arm
    const float lo = (h - arm) * 0.5f;
    const float hi = lo + arm;
    const float rim = h * 0.05f;
    d->drawSolidRect({lo, 0.0f}, {hi, h}, kRim);
    d->drawSolidRect({0.0f, lo}, {h, hi}, kRim);
    d->drawSolidRect({lo + rim, rim}, {hi - rim, h - rim}, kDark);
    d->drawSolidRect({rim, lo + rim}, {h - rim, hi - rim}, kDark);
    const float pad = h * 0.09f;
    switch (direction) {
    case GamepadButton::Up: d->drawSolidRect({lo + pad, hi}, {hi - pad, h - pad}, kWhite); break;
    case GamepadButton::Down: d->drawSolidRect({lo + pad, pad}, {hi - pad, lo}, kWhite); break;
    case GamepadButton::Left: d->drawSolidRect({pad, lo + pad}, {lo, hi - pad}, kWhite); break;
    default: d->drawSolidRect({hi, lo + pad}, {h - pad, hi - pad}, kWhite); break;
    }
    d->setContentSize({h, h});
    return d;
}

ax::Node* padButton(GamepadButton b, GamepadStyle style, float h, const std::string& font)
{
    switch (b) {
    case GamepadButton::A:
    case GamepadButton::B:
    case GamepadButton::X:
    case GamepadButton::Y: return faceButton(b, style, h, font);
    case GamepadButton::Up:
    case GamepadButton::Down:
    case GamepadButton::Left:
    case GamepadButton::Right: return dpad(b, h);
    case GamepadButton::LeftStick:
    case GamepadButton::RightStick: {
        // A stick seen from above: a ring and its label.
        auto* d = ax::DrawNode::create();
        const ax::Vec2 c{h * 0.5f, h * 0.5f};
        darkCircle(*d, c, h * 0.48f);
        d->drawCircle(c, h * 0.32f, 0.0f, 32, false, colour(0x6b6b78), h * 0.05f);
        addLabel(*d, std::string(padButtonLabel(b, style)), font, h * 0.38f, c, kWhite, h * 0.6f);
        d->setContentSize({h, h});
        return d;
    }
    default: {
        // Bumpers and the menu buttons: a dark rounded bar with its name.
        const std::string text(padButtonLabel(b, style));
        const float size = h * 0.44f;
        const float w = std::max(h * 1.3f, labelWidth(text, font, size) + h * 0.6f);
        auto* d = ax::DrawNode::create();
        const bool bumper = b == GamepadButton::LeftBumper || b == GamepadButton::RightBumper;
        const float corner = bumper ? h * 0.12f : h * 0.38f;
        const float rim = h * 0.045f;
        roundedRect(*d, 0.0f, h * 0.12f, w, h * 0.88f, corner, kRim);
        roundedRect(*d, rim, h * 0.12f + rim, w - rim, h * 0.88f - rim, corner - rim, kDark);
        addLabel(*d, text, font, size, {w * 0.5f, h * 0.5f}, kWhite, w - h * 0.3f);
        d->setContentSize({w, h});
        return d;
    }
    }
}

} // namespace

ax::Node* makeButtonIcon(const ButtonIcon& icon, float height, const std::string& font)
{
    const Binding& b = icon.binding;
    switch (b.kind) {
    case Binding::Kind::Key: return keyCap(b.key, height, font);
    case Binding::Kind::Mouse: return mouse(b.mouse, height);
    case Binding::Kind::Pad: return padButton(b.pad, icon.style, height, font);
    }
    return ax::Node::create();
}

} // namespace rolo::detail
