#include "rolo/ui/widgets/TextField.hpp"

#include "rolo/ui/Context.hpp"
#include "rolo/ui/Utf8.hpp"
#include "rolo/ui/widgets/Basic.hpp"

#include <algorithm>
#include <cmath>

namespace rolo::ui {

// --- TextEditState ---------------------------------------------------------------------

void TextEditState::setText(std::string text)
{
    for (char& c : text) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    m_text = std::move(text);
    m_caret = m_anchor = m_text.size();
}

std::string TextEditState::selectedText() const
{
    return m_text.substr(selectionStart(), selectionEnd() - selectionStart());
}

void TextEditState::setCaret(std::size_t index, bool extend)
{
    m_caret = std::min(index, m_text.size());
    if (!extend) {
        m_anchor = m_caret;
    }
}

void TextEditState::moveLeft(bool extend, bool byWord)
{
    if (hasSelection() && !extend && !byWord) {
        setCaret(selectionStart()); // collapse to the left edge
        return;
    }
    setCaret(byWord ? utf8::previousWordStart(m_text, m_caret) : utf8::previous(m_text, m_caret), extend);
}

void TextEditState::moveRight(bool extend, bool byWord)
{
    if (hasSelection() && !extend && !byWord) {
        setCaret(selectionEnd());
        return;
    }
    setCaret(byWord ? utf8::nextWordEnd(m_text, m_caret) : utf8::next(m_text, m_caret), extend);
}

void TextEditState::moveHome(bool extend)
{
    setCaret(0, extend);
}

void TextEditState::moveEnd(bool extend)
{
    setCaret(m_text.size(), extend);
}

void TextEditState::selectAll()
{
    m_anchor = 0;
    m_caret = m_text.size();
}

void TextEditState::selectWordAt(std::size_t index)
{
    index = std::min(index, m_text.size());
    // Start: walk left while inside a word; end: walk right.
    std::size_t start = index;
    while (start > 0) {
        const std::size_t p = utf8::previous(m_text, start);
        std::size_t tmp = p;
        if (utf8::isWordSeparator(utf8::decode(m_text, tmp))) {
            break;
        }
        start = p;
    }
    std::size_t end = index;
    while (end < m_text.size()) {
        std::size_t tmp = end;
        if (utf8::isWordSeparator(utf8::decode(m_text, tmp))) {
            break;
        }
        end = tmp;
    }
    m_anchor = start;
    m_caret = end;
}

bool TextEditState::deleteRange(std::size_t from, std::size_t to)
{
    if (from >= to) {
        return false;
    }
    m_text.erase(from, to - from);
    m_caret = m_anchor = from;
    return true;
}

bool TextEditState::insert(std::string_view input)
{
    std::string clean(input);
    for (char& c : clean) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
    }
    if (m_maxLength > 0) {
        const std::size_t keep = utf8::length(m_text) - utf8::length(selectedText());
        if (keep >= m_maxLength) {
            return false;
        }
        // Trim the insertion to what still fits.
        std::size_t allowed = m_maxLength - keep;
        std::size_t cut = 0;
        while (cut < clean.size() && allowed > 0) {
            cut = utf8::next(clean, cut);
            --allowed;
        }
        clean.resize(cut);
    }
    if (clean.empty() && !hasSelection()) {
        return false;
    }
    const std::size_t start = selectionStart();
    m_text.replace(start, selectionEnd() - start, clean);
    m_caret = m_anchor = start + clean.size();
    return true;
}

bool TextEditState::deleteBackward(bool byWord)
{
    if (hasSelection()) {
        return deleteRange(selectionStart(), selectionEnd());
    }
    const std::size_t from = byWord ? utf8::previousWordStart(m_text, m_caret) : utf8::previous(m_text, m_caret);
    return deleteRange(from, m_caret);
}

bool TextEditState::deleteForward(bool byWord)
{
    if (hasSelection()) {
        return deleteRange(selectionStart(), selectionEnd());
    }
    const std::size_t to = byWord ? utf8::nextWordEnd(m_text, m_caret) : utf8::next(m_text, m_caret);
    return deleteRange(m_caret, to);
}

bool TextEditState::deleteToStart()
{
    if (hasSelection()) {
        return deleteRange(selectionStart(), selectionEnd());
    }
    return deleteRange(0, m_caret);
}

// --- TextField --------------------------------------------------------------------------------

namespace {
constexpr float kHorizontalPadding = 8.0f;
constexpr float kIconWidth = 18.0f;
} // namespace

TextField::TextField(std::string placeholder)
    : m_placeholder(std::move(placeholder))
{
    setFocusable(true);
}

void TextField::setText(std::string text)
{
    m_state.setText(std::move(text));
    m_textAtFocus = m_state.text(); // the new committed text (Escape returns to it)
    m_scrollX = 0.0f;
}

Size TextField::measure(Context& ctx, Size)
{
    return applyFixedSize({160.0f, ctx.theme().controlHeight});
}

Rect TextField::textArea() const
{
    const Rect b = bounds();
    const float left = kHorizontalPadding + (m_icon ? kIconWidth : 0.0f);
    const float right =
        kHorizontalPadding + m_rightInset + (m_clearButton && !m_state.text().empty() ? 18.0f : 0.0f);
    return Rect{b.x + left, b.y, std::max(0.0f, b.width - left - right), b.height};
}

Rect TextField::clearButtonRect() const
{
    const Rect b = bounds();
    return Rect{b.right() - 24.0f, b.y, 22.0f, b.height};
}

std::string TextField::shownText() const
{
    return m_secret ? std::string(utf8::length(m_state.text()), '*') : m_state.text();
}

std::size_t TextField::shownIndex(std::size_t textIndex) const
{
    // A secret shows one '*' per character, so a place in the text is as
    // many stars along as there are characters before it.
    return m_secret ? utf8::length(std::string_view(m_state.text()).substr(0, textIndex)) : textIndex;
}

std::size_t TextField::indexAt(Context& ctx, float x)
{
    const Theme& t = ctx.theme();
    const std::size_t shown = ctx.atlas().hitTest(t.font, t.fontSize, shownText(), x - textArea().x + m_scrollX);
    if (!m_secret) {
        return shown;
    }
    std::size_t index = 0;   // back from stars to the text: that many characters in
    for (std::size_t i = 0; i < shown && index < m_state.text().size(); ++i) {
        index = utf8::next(m_state.text(), index);
    }
    return index;
}

void TextField::changed(Context& ctx)
{
    m_caretTime = ctx.time();
    keepCaretVisible(ctx);
    if (m_onChange) {
        ctx.post([this] {
            if (m_onChange) {
                m_onChange(m_state.text());
            }
        });
    }
}

void TextField::keepCaretVisible(Context& ctx)
{
    const Theme& t = ctx.theme();
    const std::string shown = shownText();
    const float caretX = ctx.atlas().measure(t.font, t.fontSize, std::string_view(shown).substr(0, shownIndex(m_state.caret())));
    const float width = textArea().width;
    if (caretX - m_scrollX > width - 2.0f) {
        m_scrollX = caretX - width + 2.0f;
    } else if (caretX - m_scrollX < 0.0f) {
        m_scrollX = caretX;
    }
    const float textWidth = ctx.atlas().measure(t.font, t.fontSize, shown);
    m_scrollX = std::clamp(m_scrollX, 0.0f, std::max(0.0f, textWidth - width + 2.0f));
}

void TextField::paint(Context& ctx, DrawList& draw)
{
    const Theme& t = ctx.theme();
    const Rect b = bounds();
    const bool focused = isFocused();
    draw.fillRoundedRect(b, t.radius, focused || isHovered() ? t.fieldHover : t.field);
    if (focused || isHovered()) {
        draw.strokeRoundedRect(b, t.radius, focused ? t.focusRing : t.border);
    }

    if (m_icon) {
        drawIcon(draw, *m_icon, Rect{b.x + 6.0f, b.center().y - 8.0f, 16.0f, 16.0f}, t.textFaint);
    }

    const Rect area = textArea();
    const FontMetrics fm = ctx.atlas().metrics(t.font, t.fontSize);
    const float textY = b.y + std::round((b.height - fm.lineHeight) * 0.5f);
    const float originX = area.x - m_scrollX;
    const std::string text = shownText();

    draw.pushClip(area.inset(Insets{-1.0f, 2.0f, -1.0f, 2.0f}));
    if (text.empty() && !m_placeholder.empty()) {
        draw.text(t.font, t.fontSize, {area.x, textY}, t.textFaint, m_placeholder);
    } else {
        if (focused && m_state.hasSelection()) {
            const float x0 = ctx.atlas().measure(t.font, t.fontSize, std::string_view(text).substr(0, shownIndex(m_state.selectionStart())));
            const float x1 = ctx.atlas().measure(t.font, t.fontSize, std::string_view(text).substr(0, shownIndex(m_state.selectionEnd())));
            draw.fillRect(Rect{originX + x0, textY, x1 - x0, fm.lineHeight}, t.textSelection);
        }
        draw.text(t.font, t.fontSize, {originX, textY}, isEnabled() ? t.text : t.textFaint, text);
    }
    // Caret: blinks once a second, solid right after typing or moving.
    if (focused) {
        const bool on = std::fmod(ctx.time() - m_caretTime, 1.0) < 0.6;
        if (on) {
            const float caretX = ctx.atlas().measure(t.font, t.fontSize, std::string_view(text).substr(0, shownIndex(m_state.caret())));
            draw.fillRect(Rect{std::round(originX + caretX), textY, 1.5f, fm.lineHeight}, t.accent);
        }
    }
    draw.popClip();

    if (m_clearButton && !text.empty()) {
        const Rect cb = clearButtonRect();
        const bool hot = cb.contains(ctx.pointerPosition()) && (isHovered() || hasPointerCapture());
        const Rect circle{cb.center().x - 7.0f, cb.center().y - 7.0f, 14.0f, 14.0f};
        draw.fillRoundedRect(circle, 7.0f, hot ? t.textDim : t.textFaint);
        drawIcon(draw, Icon::Close, circle.inset(1.0f), t.control);
    }
}

bool TextField::onPointerDown(Context& ctx, const PointerEvent& event)
{
    if (event.button != MouseButton::Left) {
        return false;
    }
    m_caretTime = ctx.time();
    if (m_clearButton && !m_state.text().empty() && clearButtonRect().contains(event.position)) {
        m_pressedClear = true;
        return true;
    }
    const std::size_t index = indexAt(ctx, event.position.x);
    m_selectingWords = false;
    if (event.clickCount == 2) {
        m_state.selectWordAt(index);
        m_selectingWords = true;
    } else if (event.clickCount >= 3) {
        m_state.selectAll();
    } else {
        m_state.setCaret(index, event.modifiers.shift);
    }
    keepCaretVisible(ctx);
    return true;
}

void TextField::onPointerMove(Context& ctx, const PointerEvent& event)
{
    if (!hasPointerCapture() || m_pressedClear || m_selectingWords) {
        return;
    }
    m_state.setCaret(indexAt(ctx, event.position.x), true); // drag to select
    keepCaretVisible(ctx);
}

bool TextField::onPointerUp(Context& ctx, const PointerEvent& event)
{
    if (m_pressedClear) {
        m_pressedClear = false;
        if (clearButtonRect().contains(event.position)) {
            m_state.setText({});
            m_scrollX = 0.0f;
            changed(ctx);
        }
    }
    m_selectingWords = false;
    return true;
}

bool TextField::onKeyDown(Context& ctx, const KeyEvent& event)
{
    const Platform& platform = ctx.platform();
    const Modifiers& m = event.modifiers;
    const bool primary = platform.isPrimary(m);
    const bool word = platform.isWordModifier(m);
    const bool shift = m.shift;
    m_caretTime = ctx.time();

    switch (event.key) {
    case Key::Left:
        if (platform.macConventions && m.super) {
            m_state.moveHome(shift);
        } else {
            m_state.moveLeft(shift, word);
        }
        break;
    case Key::Right:
        if (platform.macConventions && m.super) {
            m_state.moveEnd(shift);
        } else {
            m_state.moveRight(shift, word);
        }
        break;
    case Key::Home:
    case Key::Up:
        m_state.moveHome(shift);
        break;
    case Key::End:
    case Key::Down:
        m_state.moveEnd(shift);
        break;
    case Key::Backspace: {
        const bool changedText = (platform.macConventions && m.super) ? m_state.deleteToStart()
                                                                       : m_state.deleteBackward(word);
        if (changedText) {
            changed(ctx);
        }
        break;
    }
    case Key::Delete:
        if (m_state.deleteForward(word)) {
            changed(ctx);
        }
        break;
    case Key::Enter:
        if (!m_onCommit && !m_onSubmit) {
            // Nothing here uses Enter: let it reach a dialog's main button
            // (e.g. typing a name, then Enter for "Create").
            return false;
        }
        if (m_onCommit) {
            commit(ctx);
            m_state.selectAll();
        }
        if (m_onSubmit) {
            ctx.post([this] {
                if (m_onSubmit) {
                    m_onSubmit(m_state.text());
                }
            });
        }
        break;
    case Key::Escape:
        if (m_onCommit) {
            // Cancel: back to the text from before editing, and leave the field.
            m_state.setText(m_textAtFocus);
            ctx.setFocus(nullptr);
            break;
        }
        if (m_state.hasSelection()) {
            m_state.setCaret(m_state.caret());
            break;
        }
        return false; // let popups / dialogs close
    case Key::A:
        if (!primary) {
            return false;
        }
        m_state.selectAll();
        break;
    case Key::C:
    case Key::X:
        if (!primary) {
            return false;
        }
        if (m_secret) {
            break;   // a password is never put on the clipboard
        }
        if (m_state.hasSelection() && platform.setClipboard) {
            platform.setClipboard(m_state.selectedText());
            if (event.key == Key::X && m_state.deleteBackward(false)) {
                changed(ctx);
            }
        }
        break;
    case Key::V:
        if (!primary) {
            return false;
        }
        if (platform.getClipboard && m_state.insert(platform.getClipboard())) {
            changed(ctx);
        }
        break;
    default:
        // Typing is delivered through onText(); other keys are not ours.
        return !primary && !m.ctrl && event.key != Key::Tab && event.key != Key::Escape
               && event.key >= Key::A && event.key <= Key::Space;
    }
    keepCaretVisible(ctx);
    return true;
}

bool TextField::onText(Context& ctx, std::string_view text)
{
    if (m_state.insert(text)) {
        changed(ctx);
    }
    return true;
}

void TextField::onFocusChanged(Context& ctx, bool focused)
{
    m_caretTime = ctx.time();
    if (focused) {
        m_textAtFocus = m_state.text();
        if (m_selectAllOnFocus) {
            m_state.selectAll();
        }
    } else {
        commit(ctx);
        m_state.setCaret(m_state.caret()); // drop the selection highlight
        m_scrollX = 0.0f;
    }
}

void TextField::commit(Context& ctx)
{
    if (!m_onCommit || m_state.text() == m_textAtFocus) {
        return;
    }
    m_textAtFocus = m_state.text();
    // Copy the callback: the field itself may be gone by the time it runs
    // (e.g. leaving the field selected another object and rebuilt the panel).
    ctx.post([onCommit = m_onCommit, text = m_state.text()] { onCommit(text); });
}

CursorShape TextField::cursor(Vec2 point) const
{
    if (m_clearButton && !m_state.text().empty() && clearButtonRect().contains(point)) {
        return CursorShape::Arrow;
    }
    return CursorShape::IBeam;
}

} // namespace rolo::ui
