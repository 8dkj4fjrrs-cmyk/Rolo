#include "rolo/ui/widgets/Inspector.hpp"

#include "rolo/ui/Context.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rolo::ui {

namespace {

constexpr float kValueHeight = 22.0f;
constexpr float kValueSpacing = 4.0f;
constexpr float kHeaderHeight = 28.0f;
constexpr float kHeaderButton = 20.0f;

float textTop(Context& ctx, FontId font, float size, const Rect& rect)
{
    const FontMetrics fm = ctx.atlas().metrics(font, size);
    return std::round(rect.y + (rect.height - fm.lineHeight) * 0.5f);
}

// Greedy word wrap for short messages.
std::vector<std::string> wrap(Context& ctx, FontId font, float size, const std::string& text, float width)
{
    std::vector<std::string> lines;
    std::string line;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string word = text.substr(start, end - start);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && ctx.atlas().measure(font, size, candidate) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        start = end + 1;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

void paintTick(Context& ctx, DrawList& draw, Rect box, bool on)
{
    const Theme& t = ctx.theme();
    if (on) {
        draw.fillRoundedRect(box, 2.5f, t.accent);
        drawIcon(draw, Icon::Check, box.inset(-1.0f), t.textOnAccent);
    } else {
        draw.fillRoundedRect(box, 2.5f, t.field);
        draw.strokeRoundedRect(box, 2.5f, t.border);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ValueBox
// ---------------------------------------------------------------------------
ValueBox::ValueBox(std::string text, Kind kind)
    : m_text(std::move(text))
    , m_kind(kind)
{
    if (kind != Kind::Check) {
        setStretch(1.0f);
    }
}

Size ValueBox::measure(Context& ctx, Size)
{
    const Theme& t = ctx.theme();
    if (m_kind == Kind::Check) {
        return applyFixedSize({kValueHeight, kValueHeight});
    }
    float width = 12.0f + ctx.atlas().measure(t.font, t.fontSize, m_text);
    if (!m_suffix.empty()) {
        width += 8.0f + ctx.atlas().measure(t.font, t.smallFontSize, m_suffix);
    }
    if (m_kind == Kind::Choice) {
        width += 18.0f;
    }
    return applyFixedSize({std::max(40.0f, std::ceil(width)), kValueHeight});
}

void ValueBox::paint(Context& ctx, DrawList& draw)
{
    const Theme& t = ctx.theme();
    const Rect b = bounds();
    if (m_kind == Kind::Check) {
        paintTick(ctx, draw, Rect{b.x + 1.0f, b.center().y - 7.0f, 14.0f, 14.0f}, m_checked);
        return;
    }
    draw.fillRoundedRect(b, t.radius, t.field);

    float right = b.right() - 6.0f;
    if (m_kind == Kind::Choice) {
        drawIcon(draw, Icon::ChevronDown, Rect{right - 14.0f, b.center().y - 7.0f, 14.0f, 14.0f}, t.textDim);
        right -= 18.0f;
    }
    if (!m_suffix.empty()) {
        const float w = ctx.atlas().measure(t.font, t.smallFontSize, m_suffix);
        draw.text(t.font, t.smallFontSize, {right - w, textTop(ctx, t.font, t.smallFontSize, b)}, t.textFaint, m_suffix);
        right -= w + 6.0f;
    }
    draw.pushClip(b);
    draw.textEllipsized(t.font, t.fontSize, {b.x + 6.0f, textTop(ctx, t.font, t.fontSize, b)}, t.text, m_text,
                        std::max(0.0f, right - b.x - 6.0f));
    draw.popClip();
}

// ---------------------------------------------------------------------------
// PropertyField
// ---------------------------------------------------------------------------
PropertyField::PropertyField(std::string label)
    : m_label(std::move(label))
{
}

float PropertyField::labelWidth(float rowWidth)
{
    return std::clamp(std::round(rowWidth * 0.38f), 72.0f, 150.0f);
}

Size PropertyField::measure(Context& ctx, Size available)
{
    float height = ctx.theme().rowHeight + 6.0f;
    for (const auto& child : children()) {
        if (child->isVisible()) {
            height = std::max(height, child->measure(ctx, available).height + 6.0f);
        }
    }
    return applyFixedSize({available.width, height});
}

void PropertyField::arrange(Context& ctx, Rect rect)
{
    setBounds(rect);
    const float x0 = rect.x + labelWidth(rect.width);
    const float available = std::max(0.0f, rect.right() - x0);

    std::vector<Size> sizes;
    float fixed = 0.0f;
    float stretch = 0.0f;
    int count = 0;
    for (const auto& child : children()) {
        const Size s = child->isVisible() ? child->measure(ctx, {available, rect.height}) : Size{};
        sizes.push_back(s);
        if (!child->isVisible()) {
            continue;
        }
        ++count;
        if (child->stretch() > 0.0f) {
            stretch += child->stretch();
        } else {
            fixed += s.width;
        }
    }
    const float gaps = count > 1 ? kValueSpacing * static_cast<float>(count - 1) : 0.0f;
    const float leftover = std::max(0.0f, available - fixed - gaps);

    float x = x0;
    for (std::size_t i = 0; i < children().size(); ++i) {
        Widget& child = *children()[i];
        if (!child.isVisible()) {
            continue;
        }
        const float w = child.stretch() > 0.0f && stretch > 0.0f ? std::floor(leftover * child.stretch() / stretch)
                                                                  : sizes[i].width;
        const float h = std::min(sizes[i].height, rect.height);
        child.arrange(ctx, Rect{x, rect.y + std::round((rect.height - h) * 0.5f), w, h});
        x += w + kValueSpacing;
    }
}

void PropertyField::paint(Context& ctx, DrawList& draw)
{
    const Theme& t = ctx.theme();
    const Rect b = bounds();
    const float width = labelWidth(b.width) - 8.0f;
    if (m_marked) {
        // A bar in the margin left of the label, and the label in the accent colour.
        draw.fillRect({b.x - 8.0f, b.y + 4.0f, 2.0f, b.height - 8.0f}, t.accent);
    }
    draw.textEllipsized(t.font, t.fontSize, {b.x, textTop(ctx, t.font, t.fontSize, b)}, m_marked ? t.accent : t.textDim,
                        m_label, width);
    paintChildren(ctx, draw);
}

Rect PropertyField::labelRect() const
{
    const Rect b = bounds();
    return {b.x - 8.0f, b.y, labelWidth(b.width), b.height};
}

bool PropertyField::onPointerDown(Context&, const PointerEvent& event)
{
    m_labelPressed = m_marked && m_onLabelClick && event.button == MouseButton::Left
                     && labelRect().contains(event.position);
    return m_labelPressed;
}

bool PropertyField::onPointerUp(Context&, const PointerEvent& event)
{
    const bool click = m_labelPressed && m_marked && labelRect().contains(event.position);
    m_labelPressed = false;
    if (click && m_onLabelClick) {
        const auto action = m_onLabelClick;   // (the action may rebuild the panel this row is in)
        action();
    }
    return click;
}

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------
Section::Section(std::string title)
    : m_title(std::move(title))
{
    m_body = &emplace<Column>(0.0f, Insets{18.0f, 2.0f, 4.0f, 8.0f});
}

void Section::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_body->setVisible(expanded);
}

Rect Section::headerRect() const
{
    const Rect b = bounds();
    return {b.x, b.y, b.width, kHeaderHeight};
}

Rect Section::moreButton() const
{
    if (!m_onMore) {
        return {};
    }
    const Rect h = headerRect();
    return {h.right() - kHeaderButton - 2.0f, h.center().y - kHeaderButton * 0.5f, kHeaderButton, kHeaderButton};
}

Rect Section::helpButton() const
{
    if (!m_onHelp) {
        return {};
    }
    const Rect h = headerRect();
    const float right = m_onMore ? h.right() - kHeaderButton - 6.0f : h.right() - 2.0f;
    return {right - kHeaderButton, h.center().y - kHeaderButton * 0.5f, kHeaderButton, kHeaderButton};
}

Size Section::measure(Context& ctx, Size available)
{
    float height = kHeaderHeight;
    if (m_expanded) {
        height += m_body->measure(ctx, available).height;
    }
    return applyFixedSize({available.width, height});
}

void Section::arrange(Context& ctx, Rect rect)
{
    setBounds(rect);
    m_body->setVisible(m_expanded);
    if (m_expanded) {
        m_body->arrange(ctx, Rect{rect.x, rect.y + kHeaderHeight, rect.width,
                                  std::max(0.0f, rect.height - kHeaderHeight)});
    }
}

void Section::paint(Context& ctx, DrawList& draw)
{
    const Theme& t = ctx.theme();
    const Rect h = headerRect();
    const Vec2 pointer = ctx.pointerPosition();
    const bool hot = isHovered() && h.contains(pointer);

    draw.fillRect(Rect{h.x, h.bottom() - 1.0f, h.width, 1.0f}, t.border.fade(0.7f));
    float x = h.x;
    drawIcon(draw, m_expanded ? Icon::ChevronDown : Icon::ChevronRight, Rect{x, h.center().y - 8.0f, 16.0f, 16.0f},
             hot ? t.text : t.textDim);
    x += 20.0f;
    if (m_toggle) {
        paintTick(ctx, draw, Rect{x, h.center().y - 7.0f, 14.0f, 14.0f}, *m_toggle);
        x += 20.0f;
    }
    draw.textEllipsized(t.font, t.fontSize, {x, textTop(ctx, t.font, t.fontSize, h)}, t.text, m_title,
                        std::max(0.0f, h.right() - x - 56.0f));

    for (const auto& [rect, icon] : {std::pair{helpButton(), Icon::Help}, std::pair{moreButton(), Icon::More}}) {
        if (rect.isEmpty()) {
            continue;
        }
        const bool over = isHovered() && rect.contains(pointer);
        if (over) {
            draw.fillRoundedRect(rect, t.radius, t.controlHover);
        }
        drawIcon(draw, icon, rect.inset(2.0f), over ? t.text : t.textFaint);
    }
    paintChildren(ctx, draw);
}

Widget* Section::hitTest(Vec2 point)
{
    if (!isVisible() || !bounds().contains(point)) {
        return nullptr;
    }
    if (headerRect().contains(point)) {
        return this;
    }
    return Widget::hitTest(point);
}

bool Section::onPointerDown(Context&, const PointerEvent& event)
{
    if (event.button != MouseButton::Left || !headerRect().contains(event.position)) {
        return false;
    }
    m_pressed = helpButton().contains(event.position) ? Part::Help
                : moreButton().contains(event.position) ? Part::More
                                                       : Part::Header;
    return true;
}

bool Section::onPointerUp(Context& ctx, const PointerEvent& event)
{
    const Part pressed = std::exchange(m_pressed, Part::None);
    if (pressed == Part::Help && helpButton().contains(event.position)) {
        ctx.post(m_onHelp);
    } else if (pressed == Part::More && moreButton().contains(event.position)) {
        ctx.post(m_onMore);
    } else if (pressed == Part::Header && headerRect().contains(event.position)) {
        setExpanded(!m_expanded);
    }
    return true;
}

// ---------------------------------------------------------------------------
// EmptyState
// ---------------------------------------------------------------------------
EmptyState::EmptyState(std::optional<Icon> icon, std::string title, std::string message)
    : m_icon(icon)
    , m_title(std::move(title))
    , m_message(std::move(message))
{
    setStretch(1.0f);
}

Size EmptyState::measure(Context& ctx, Size available)
{
    const Theme& t = ctx.theme();
    const float lineHeight = ctx.atlas().metrics(t.font, t.fontSize).lineHeight;
    const float width = std::min(320.0f, std::max(0.0f, available.width - 2.0f * t.padding));
    const auto lines = wrap(ctx, t.font, t.smallFontSize, m_message, width);
    const float height = (m_icon ? 40.0f : 0.0f) + lineHeight + 6.0f + static_cast<float>(lines.size()) * 16.0f;
    return applyFixedSize({width, height + 2.0f * t.padding});
}

void EmptyState::paint(Context& ctx, DrawList& draw)
{
    const Theme& t = ctx.theme();
    const Rect b = bounds();
    const float width = std::min(320.0f, std::max(0.0f, b.width - 2.0f * t.padding));
    const auto lines = wrap(ctx, t.font, t.smallFontSize, m_message, width);
    const float lineHeight = ctx.atlas().metrics(t.font, t.fontSize).lineHeight;
    const float total = (m_icon ? 40.0f : 0.0f) + lineHeight + 6.0f + static_cast<float>(lines.size()) * 16.0f;
    // A little above the middle reads as centred.
    float y = std::round(b.y + std::max(t.padding, (b.height - total) * 0.42f));
    draw.pushClip(b);
    if (m_icon) {
        drawIcon(draw, *m_icon, Rect{b.center().x - 14.0f, y, 28.0f, 28.0f}, t.textFaint);
        y += 40.0f;
    }
    const float tw = ctx.atlas().measure(t.font, t.fontSize, m_title);
    draw.text(t.font, t.fontSize, {std::round(b.center().x - tw * 0.5f), y}, t.textDim, m_title);
    y += lineHeight + 6.0f;
    for (const std::string& line : lines) {
        const float lw = ctx.atlas().measure(t.font, t.smallFontSize, line);
        draw.text(t.font, t.smallFontSize, {std::round(b.center().x - lw * 0.5f), y}, t.textFaint, line);
        y += 16.0f;
    }
    draw.popClip();
}

} // namespace rolo::ui
