#include "rolo/gameplay/TouchControls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>

namespace rolo {

namespace {

bool isMoveAction(std::string_view name)
{
    return name == Input::kMoveLeft || name == Input::kMoveRight || name == Input::kMoveUp || name == Input::kMoveDown;
}

bool isMenuAction(std::string_view name)
{
    return name == "pause" || name == "menu";
}

// "jump" -> "Jump", "use item" -> "Use item".
std::string labelOf(std::string name)
{
    if (!name.empty()) {
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return name;
}

float distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

constexpr std::size_t kMaxButtons = 6;

} // namespace

void TouchControls::setActions(const Input& input, const std::vector<std::string>& only)
{
    std::vector<TouchButton> buttons;
    for (const std::string& name : only.empty() ? input.actions() : only) {
        if (isMoveAction(name) || !input.hasAction(name) || buttons.size() >= kMaxButtons) {
            continue;
        }
        if (std::any_of(buttons.begin(), buttons.end(), [&](const TouchButton& b) { return b.action == name; })) {
            continue;
        }
        TouchButton button;
        button.action = name;
        button.label = labelOf(name);
        button.menu = isMenuAction(name);
        buttons.push_back(std::move(button));
    }
    const auto same = [](const TouchButton& a, const TouchButton& b) { return a.action == b.action; };
    if (buttons.size() == m_buttons.size() && std::equal(buttons.begin(), buttons.end(), m_buttons.begin(), same)) {
        return;
    }
    m_buttons = std::move(buttons);
    m_held.clear();
    m_stick.held = false;
    layout(m_width, m_height);
}

std::vector<std::string> TouchControls::actions() const
{
    std::vector<std::string> names;
    for (const TouchButton& b : m_buttons) {
        names.push_back(b.action);
    }
    return names;
}

void TouchControls::layout(float width, float height)
{
    m_width = width;
    m_height = height;
    ++m_revision;
    ++m_layoutRevision;
    // Sizes follow the shorter side, so the controls are as large under a
    // thumb whether the screen is wide or tall.
    const float unit = std::min(width, height);
    const float margin = unit * 0.06f;

    m_stick.radius = unit * 0.13f;
    m_stick.rest = {margin + m_stick.radius * 1.25f, margin + m_stick.radius * 1.25f};
    if (!m_stick.held) {
        m_stick.base = m_stick.rest;
        m_stick.knob = m_stick.rest;
    }

    // The main buttons: the first at the bottom-right corner under the
    // thumb, the others around it: to its left, above it, then further out.
    const float big = unit * 0.095f;
    const float other = unit * 0.08f;
    const float menuRadius = unit * 0.05f;
    const Vec2 corner{width - margin - big, margin + big};
    const float ring = big + other + unit * 0.035f;
    struct Place {
        float degrees;
        float reach;   // in rings
    };
    static constexpr Place kPlaces[] = {{180.0f, 1.0f}, {90.0f, 1.0f}, {135.0f, 1.45f}, {180.0f, 2.0f}, {90.0f, 2.0f}};
    const auto menus = static_cast<float>(
        std::count_if(m_buttons.begin(), m_buttons.end(), [](const TouchButton& b) { return b.menu; }));
    int main = 0;
    int menu = 0;
    for (TouchButton& b : m_buttons) {
        if (b.menu) {
            // Top middle, side by side: clear of scores in the corners.
            b.radius = menuRadius;
            b.center = {width * 0.5f + (static_cast<float>(menu) - (menus - 1.0f) * 0.5f) * menuRadius * 2.6f,
                        height - margin - menuRadius};
            ++menu;
        } else if (main == 0) {
            b.radius = big;
            b.center = corner;
            ++main;
        } else {
            const Place& place = kPlaces[static_cast<std::size_t>(main - 1) % std::size(kPlaces)];
            const float angle = place.degrees * 3.14159265f / 180.0f;
            b.radius = other;
            b.center = {corner.x + std::cos(angle) * ring * place.reach, corner.y + std::sin(angle) * ring * place.reach};
            ++main;
        }
    }
}

bool TouchControls::touchBegan(int id, Vec2 p)
{
    if (owns(id)) {
        return true;
    }
    // A button, if the finger is on one (a little outside counts: thumbs are not exact).
    int best = -1;
    float bestDistance = 0.0f;
    for (std::size_t i = 0; i < m_buttons.size(); ++i) {
        const float d = distance(p, m_buttons[i].center);
        if (d <= m_buttons[i].radius * 1.2f && (best < 0 || d < bestDistance)) {
            best = static_cast<int>(i);
            bestDistance = d;
        }
    }
    if (best >= 0) {
        m_held.push_back({id, best});
        m_buttons[static_cast<std::size_t>(best)].down = true;
        ++m_revision;
        return true;
    }
    // The stick: anywhere in the bottom-left part of the screen; its base
    // moves under the finger, so the thumb need not find it.
    const bool stickFree = std::none_of(m_held.begin(), m_held.end(), [](const Held& h) { return h.button < 0; });
    if (stickFree && p.x < m_width * 0.4f && p.y < m_height * 0.65f) {
        m_held.push_back({id, -1});
        m_stick.held = true;
        m_stick.base = {std::clamp(p.x, m_stick.radius, m_width - m_stick.radius),
                        std::clamp(p.y, m_stick.radius, m_height - m_stick.radius)};
        m_stick.knob = p;
        ++m_revision;
        return true;
    }
    return false;
}

void TouchControls::touchMoved(int id, Vec2 p)
{
    for (const Held& h : m_held) {
        if (h.id == id && h.button < 0) {
            // The knob stays within the base's circle.
            const float d = distance(p, m_stick.base);
            const float k = d > m_stick.radius ? m_stick.radius / d : 1.0f;
            m_stick.knob = {m_stick.base.x + (p.x - m_stick.base.x) * k, m_stick.base.y + (p.y - m_stick.base.y) * k};
            ++m_revision;
        }
    }
}

void TouchControls::touchEnded(int id)
{
    for (auto it = m_held.begin(); it != m_held.end(); ++it) {
        if (it->id != id) {
            continue;
        }
        if (it->button < 0) {
            m_stick.held = false;
            m_stick.base = m_stick.rest;
            m_stick.knob = m_stick.rest;
        } else {
            const int button = it->button;
            // Still down if another finger is on the same button.
            const auto others = std::count_if(m_held.begin(), m_held.end(),
                                              [&](const Held& h) { return h.button == button && h.id != id; });
            m_buttons[static_cast<std::size_t>(button)].down = others > 0;
        }
        m_held.erase(it);
        ++m_revision;
        return;
    }
}

bool TouchControls::owns(int id) const noexcept
{
    return std::any_of(m_held.begin(), m_held.end(), [id](const Held& h) { return h.id == id; });
}

void TouchControls::releaseAll()
{
    m_held.clear();
    m_stick.held = false;
    m_stick.base = m_stick.rest;
    m_stick.knob = m_stick.rest;
    for (TouchButton& b : m_buttons) {
        b.down = false;
    }
    ++m_revision;
}

void TouchControls::apply(Input& input) const
{
    Vec2 direction;
    if (m_stick.held && m_stick.radius > 0.0f) {
        direction = {(m_stick.knob.x - m_stick.base.x) / m_stick.radius,
                     (m_stick.knob.y - m_stick.base.y) / m_stick.radius};
        // A small dead zone in the middle: a resting thumb does not walk.
        if (std::hypot(direction.x, direction.y) < 0.18f) {
            direction = {};
        }
    }
    input.setTouchStick(direction);
    for (const TouchButton& b : m_buttons) {
        input.setTouchAction(b.action, b.down);
    }
}

} // namespace rolo
