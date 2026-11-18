#pragma once

// Building blocks for property inspectors, laid out the way desktop game
// editors do it:
//
//   v [x] Transform                              (?) (⋮)   <- Section header
//       Position        [ 640.0         X ] [ 360.0  Y ]   <- PropertyField
//       Visible         [x]                                    with ValueBoxes
//
// ValueBoxes display values. They are read-only for now: editable number
// fields, colour pickers and drop-downs arrive with scene editing.
//
// EmptyState is the centred icon + message shown by a panel with nothing to
// show yet ("No project open").

#include "rolo/ui/Icons.hpp"
#include "rolo/ui/Widget.hpp"
#include "rolo/ui/widgets/Box.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace rolo::ui {

/// A value in a field-styled box.
class ValueBox : public Widget {
public:
    enum class Kind {
        Text,    // a number or some text
        Check,   // a tick box
        Choice,  // one option from a list (shows a ▾)
    };

    explicit ValueBox(std::string text = {}, Kind kind = Kind::Text);

    void setText(std::string text) { m_text = std::move(text); }
    const std::string& text() const noexcept { return m_text; }
    void setChecked(bool checked) { m_checked = checked; }
    bool isChecked() const noexcept { return m_checked; }
    /// A short faint label at the right end, e.g. the axis "X".
    void setSuffix(std::string suffix) { m_suffix = std::move(suffix); }

    Size measure(Context& ctx, Size available) override;
    void paint(Context& ctx, DrawList& draw) override;

private:
    std::string m_text;
    std::string m_suffix;
    Kind m_kind;
    bool m_checked = false;
};

/// One inspector row: a label on the left, one or more values on the right.
class PropertyField : public Widget {
public:
    explicit PropertyField(std::string label);

    void setLabel(std::string label) { m_label = std::move(label); }

    /// Marks the row: its label is drawn in the accent colour behind a small
    /// bar. (The editor marks what a copy of a prefab has of its own.) A
    /// marked row's label can be clicked: see setOnLabelClick.
    void setMarked(bool marked) { m_marked = marked; }
    bool isMarked() const noexcept { return m_marked; }
    /// What a click on a marked row's label does (e.g. put the value back).
    void setOnLabelClick(std::function<void()> action) { m_onLabelClick = std::move(action); }
    /// The part of the row the label is in.
    Rect labelRect() const;

    template <class T>
    T& addValue(std::unique_ptr<T> value)
    {
        return addChild(std::move(value));
    }

    Size measure(Context& ctx, Size available) override;
    void arrange(Context& ctx, Rect rect) override;
    void paint(Context& ctx, DrawList& draw) override;
    bool onPointerDown(Context& ctx, const PointerEvent& event) override;
    bool onPointerUp(Context& ctx, const PointerEvent& event) override;

    /// Width of the label column for a row of this width.
    static float labelWidth(float rowWidth);

private:
    std::string m_label;
    bool m_marked = false;
    bool m_labelPressed = false;
    std::function<void()> m_onLabelClick;
};

/// A titled, collapsible group of rows.
class Section : public Widget {
public:
    explicit Section(std::string title);

    Column& body() noexcept { return *m_body; }

    void setTitle(std::string title) { m_title = std::move(title); }
    bool isExpanded() const noexcept { return m_expanded; }
    void setExpanded(bool expanded);
    /// Shows a tick box before the title (whether the component is on).
    void setToggle(std::optional<bool> on) { m_toggle = on; }
    /// Shows a (?) button; the action runs when it is clicked.
    void setHelpAction(std::function<void()> action) { m_onHelp = std::move(action); }
    /// Shows a (⋮) button; the action runs when it is clicked.
    void setMoreAction(std::function<void()> action) { m_onMore = std::move(action); }

    /// The header's buttons (for tests and for placing a menu under them).
    Rect helpButton() const;
    Rect moreButton() const;

    Size measure(Context& ctx, Size available) override;
    void arrange(Context& ctx, Rect rect) override;
    void paint(Context& ctx, DrawList& draw) override;
    Widget* hitTest(Vec2 point) override;
    bool onPointerDown(Context& ctx, const PointerEvent& event) override;
    bool onPointerUp(Context& ctx, const PointerEvent& event) override;

private:
    Rect headerRect() const;

    std::string m_title;
    Column* m_body = nullptr;
    bool m_expanded = true;
    std::optional<bool> m_toggle;
    std::function<void()> m_onHelp;
    std::function<void()> m_onMore;
    enum class Part { None, Header, Help, More };
    Part m_pressed = Part::None;
};

/// A centred message for a panel that has nothing to show yet.
class EmptyState : public Widget {
public:
    EmptyState(std::optional<Icon> icon, std::string title, std::string message = {});

    void setMessage(std::string message) { m_message = std::move(message); }

    Size measure(Context& ctx, Size available) override;
    void paint(Context& ctx, DrawList& draw) override;

private:
    std::optional<Icon> m_icon;
    std::string m_title;
    std::string m_message;
};

} // namespace rolo::ui
