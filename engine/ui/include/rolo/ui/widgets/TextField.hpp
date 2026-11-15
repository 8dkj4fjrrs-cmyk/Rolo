#pragma once

// Single-line text input.
//
// TextEditState holds the text, caret and selection and implements every
// editing operation. It knows nothing about drawing, so it is unit-tested
// directly. TextField is the widget: it maps keys and mouse actions (with the
// right conventions on macOS vs Windows/Linux) onto TextEditState and draws it.

#include "rolo/ui/Icons.hpp"
#include "rolo/ui/Widget.hpp"

#include <functional>
#include <optional>
#include <string>

namespace rolo::ui {

class TextEditState {
public:
    const std::string& text() const noexcept { return m_text; }
    /// Replaces everything and puts the caret at the end.
    void setText(std::string text);

    // Caret and selection are byte offsets, always on character boundaries.
    std::size_t caret() const noexcept { return m_caret; }
    std::size_t anchor() const noexcept { return m_anchor; }
    bool hasSelection() const noexcept { return m_caret != m_anchor; }
    std::size_t selectionStart() const noexcept { return std::min(m_caret, m_anchor); }
    std::size_t selectionEnd() const noexcept { return std::max(m_caret, m_anchor); }
    std::string selectedText() const;

    /// Moves the caret; with `extend` the selection grows instead of collapsing.
    void setCaret(std::size_t index, bool extend = false);
    void moveLeft(bool extend, bool byWord);
    void moveRight(bool extend, bool byWord);
    void moveHome(bool extend);
    void moveEnd(bool extend);
    void selectAll();
    void selectWordAt(std::size_t index);

    /// Typing: replaces the selection (if any) with `text`. Line breaks are
    /// turned into spaces (this is a single-line field). Returns true if changed.
    bool insert(std::string_view text);
    /// Backspace: deletes the selection, else the character (or word) before the caret.
    bool deleteBackward(bool byWord);
    /// Delete key: deletes the selection, else the character (or word) after the caret.
    bool deleteForward(bool byWord);
    /// Deletes everything from the caret to the start of the line (Cmd+Backspace on macOS).
    bool deleteToStart();

    /// Optional limit on length in characters (0 = no limit).
    void setMaxLength(std::size_t characters) { m_maxLength = characters; }

private:
    bool deleteRange(std::size_t from, std::size_t to);

    std::string m_text;
    std::size_t m_caret = 0;
    std::size_t m_anchor = 0;
    std::size_t m_maxLength = 0;
};

class TextField : public Widget {
public:
    explicit TextField(std::string placeholder = {});

    const std::string& text() const noexcept { return m_state.text(); }
    void setText(std::string text);
    void setPlaceholder(std::string placeholder) { m_placeholder = std::move(placeholder); }
    /// Shows an icon at the start (e.g. Icon::Search for filter fields).
    void setLeadingIcon(std::optional<Icon> icon) { m_icon = icon; }
    /// Shows a small "x" button that clears the field when it is not empty.
    void setClearButton(bool show) { m_clearButton = show; }

    /// Called after every change to the text.
    void setOnChange(std::function<void(const std::string&)> onChange) { m_onChange = std::move(onChange); }
    /// Called when Enter is pressed.
    void setOnSubmit(std::function<void(const std::string&)> onSubmit) { m_onSubmit = std::move(onSubmit); }

    /// "Commit" editing, as in property inspectors: `onCommit` runs when the
    /// user presses Enter or leaves the field after changing its text; Escape
    /// puts the text back as it was and leaves the field.
    void setOnCommit(std::function<void(const std::string&)> onCommit) { m_onCommit = std::move(onCommit); }
    /// Selects all the text when the field gets keyboard focus.
    void setSelectAllOnFocus(bool selectAll) { m_selectAllOnFocus = selectAll; }
    /// True while the field holds typing that has not been committed yet, so
    /// whoever owns the value should not overwrite it. A commit-style field
    /// (setOnCommit) stops "editing" once Enter commits, even if it keeps focus,
    /// so an undo can update it straight away.
    bool isEditing() const noexcept { return isFocused() && (!m_onCommit || m_state.text() != m_textAtFocus); }
    /// For passwords: shows a dot for every character, and does not let the
    /// text be copied or cut.
    void setSecret(bool secret) { m_secret = secret; }
    bool isSecret() const noexcept { return m_secret; }
    /// Keeps text clear of the right end (for a label drawn there, e.g. "X").
    void setRightInset(float inset) { m_rightInset = inset; }

    TextEditState& state() noexcept { return m_state; }

    Size measure(Context& ctx, Size available) override;
    void paint(Context& ctx, DrawList& draw) override;
    bool onPointerDown(Context& ctx, const PointerEvent& event) override;
    bool onPointerUp(Context& ctx, const PointerEvent& event) override;
    void onPointerMove(Context& ctx, const PointerEvent& event) override;
    bool onKeyDown(Context& ctx, const KeyEvent& event) override;
    bool onText(Context& ctx, std::string_view text) override;
    void onFocusChanged(Context& ctx, bool focused) override;
    CursorShape cursor(Vec2 point) const override;

protected:
    Rect textArea() const;

private:
    Rect clearButtonRect() const;
    std::size_t indexAt(Context& ctx, float x);
    void changed(Context& ctx);
    void keepCaretVisible(Context& ctx);

    TextEditState m_state;
    std::string m_placeholder;
    std::optional<Icon> m_icon;
    bool m_clearButton = false;
    std::function<void(const std::string&)> m_onChange;
    std::function<void(const std::string&)> m_onSubmit;
    std::function<void(const std::string&)> m_onCommit;
    std::string m_textAtFocus;
    float m_rightInset = 0.0f;
    bool m_selectAllOnFocus = false;
    bool m_secret = false;

    /// What is drawn: the text, or for a secret a '*' per character; and the
    /// place in it of a place in the text.
    std::string shownText() const;
    std::size_t shownIndex(std::size_t textIndex) const;

    void commit(Context& ctx);

    float m_scrollX = 0.0f;     // horizontal scroll when the text is wider than the field
    double m_caretTime = 0.0;   // restarts the caret blink after each action
    bool m_selectingWords = false;
    bool m_pressedClear = false;
};

} // namespace rolo::ui
