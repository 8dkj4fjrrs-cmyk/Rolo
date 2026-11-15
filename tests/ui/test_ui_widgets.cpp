#include "rolo/ui/Context.hpp"
#include "rolo/ui/widgets/Basic.hpp"
#include "rolo/ui/widgets/Box.hpp"
#include "rolo/ui/widgets/Dialog.hpp"
#include "rolo/ui/widgets/Menu.hpp"
#include "rolo/ui/widgets/Scroll.hpp"
#include "rolo/ui/widgets/TextField.hpp"
#include "rolo/ui/widgets/Tree.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace rolo::ui;

namespace {

// Puts one widget in a window-sized column and gives easy input helpers.
template <class T>
struct Harness {
    Context ctx;
    T* widget = nullptr;

    template <class... Args>
    explicit Harness(Args&&... args)
    {
        auto root = std::make_unique<Column>(0.0f, Insets::all(10.0f));
        auto w = std::make_unique<T>(std::forward<Args>(args)...);
        w->setStretch(1.0f);
        widget = &root->addChild(std::move(w));
        ctx.setRoot(std::move(root));
        ctx.setViewport({500, 400}, 1.0f);
        ctx.render();
    }

    void click(Vec2 p, Modifiers mods = {})
    {
        ctx.pointerMove(p);
        ctx.pointerButton(MouseButton::Left, true, mods);
        ctx.pointerButton(MouseButton::Left, false, mods);
        ctx.render();
    }
    void key(Key k, Modifiers mods = {})
    {
        ctx.keyDown(k, mods);
        ctx.render();
    }
    void type(std::u32string_view text)
    {
        for (char32_t c : text) {
            ctx.textInput(c);
        }
        ctx.render();
    }
};

Modifiers primary(const Platform& p)
{
    return p.macConventions ? Modifiers{.super = true} : Modifiers{.ctrl = true};
}

} // namespace

// --- Text editing -------------------------------------------------------------------------

TEST_CASE("Typing inserts at the caret and replaces a selection", "[ui][text]")
{
    TextEditState s;
    s.insert("hello");
    CHECK(s.text() == "hello");
    s.setCaret(0);
    s.insert("oh, ");
    CHECK(s.text() == "oh, hello");
    s.selectAll();
    s.insert("bye");
    CHECK(s.text() == "bye");
    CHECK(s.caret() == 3);
}

TEST_CASE("Backspace and Delete by character, word and selection", "[ui][text]")
{
    TextEditState s;
    s.setText("make a game");
    s.deleteBackward(false);
    CHECK(s.text() == "make a gam");
    s.deleteBackward(true);
    CHECK(s.text() == "make a ");
    s.setCaret(0);
    s.deleteForward(true);
    CHECK(s.text() == " a ");
    s.selectAll();
    s.deleteForward(false);
    CHECK(s.text().empty());
    CHECK_FALSE(s.deleteBackward(false)); // nothing left: no change
}

TEST_CASE("Caret moves by character and word, and selections extend", "[ui][text]")
{
    TextEditState s;
    s.setText("héllo wörld");
    s.moveHome(false);
    s.moveRight(false, false);
    s.moveRight(false, false);
    CHECK(s.caret() == 3); // after "hé" (é is two bytes)
    s.moveRight(true, true); // extend to end of word
    CHECK(s.selectedText() == "llo");
    s.moveLeft(false, false); // collapses to the selection's left edge
    CHECK(s.caret() == 3);
    CHECK_FALSE(s.hasSelection());
    s.moveEnd(true);
    CHECK(s.selectedText() == "llo wörld");
}

TEST_CASE("Double-click word selection and line breaks in pasted text", "[ui][text]")
{
    TextEditState s;
    s.setText("filter shader errors");
    s.selectWordAt(9);
    CHECK(s.selectedText() == "shader");
    s.insert("a\nb");
    CHECK(s.text() == "filter a b errors");
}

TEST_CASE("Length limit trims insertions", "[ui][text]")
{
    TextEditState s;
    s.setMaxLength(5);
    s.insert("abcdefgh");
    CHECK(s.text() == "abcde");
    CHECK_FALSE(s.insert("x"));
}

TEST_CASE("TextField: typing, shortcuts, clipboard and change notifications", "[ui][text]")
{
    Harness<TextField> h("Filter text");
    std::string clipboard;
    h.ctx.platform().getClipboard = [&] { return clipboard; };
    h.ctx.platform().setClipboard = [&](std::string_view s) { clipboard = std::string(s); };
    std::string lastChange;
    int changes = 0;
    h.widget->setOnChange([&](const std::string& t) {
        lastChange = t;
        ++changes;
    });

    h.click(h.widget->bounds().center());
    REQUIRE(h.widget->isFocused());
    h.type(U"shader");
    CHECK(h.widget->text() == "shader");
    CHECK(lastChange == "shader");
    CHECK(changes == 6);

    const Modifiers cmd = primary(h.ctx.platform());
    h.key(Key::A, cmd);
    h.key(Key::C, cmd);
    CHECK(clipboard == "shader");
    h.key(Key::Right);            // collapse selection to the end
    h.key(Key::V, cmd);
    CHECK(h.widget->text() == "shadershader");

    h.key(Key::Backspace, h.ctx.platform().macConventions ? Modifiers{.alt = true} : Modifiers{.ctrl = true});
    CHECK(h.widget->text().empty()); // word-delete removed the single word
}

TEST_CASE("TextField: a secret is typed as usual, shown as stars and never copied", "[ui][text]")
{
    Harness<TextField> h;
    std::string clipboard = "before";
    h.ctx.platform().getClipboard = [&] { return clipboard; };
    h.ctx.platform().setClipboard = [&](std::string_view s) { clipboard = std::string(s); };
    h.widget->setSecret(true);
    h.click(h.widget->bounds().center());
    h.type(U"p\u00e4ss w\u00f6rd");
    CHECK(h.widget->text() == "p\xc3\xa4ss w\xc3\xb6rd");

    const Modifiers cmd = primary(h.ctx.platform());
    h.key(Key::A, cmd);
    h.key(Key::C, cmd);
    h.key(Key::X, cmd);
    CHECK(clipboard == "before");
    CHECK(h.widget->text() == "p\xc3\xa4ss w\xc3\xb6rd");   // not cut either
    // Pasting into it works, and a click still places the caret between characters.
    h.key(Key::V, cmd);
    CHECK(h.widget->text() == "before");
    h.click({h.widget->bounds().x + 2.0f, h.widget->bounds().center().y});
    h.type(U"\u00e9");
    CHECK(h.widget->text().size() == 8);
    CHECK(h.widget->text().find("before") != std::string::npos);
}

TEST_CASE("TextField: Enter submits, and the clear button empties it", "[ui][text]")
{
    Harness<TextField> h;
    h.widget->setClearButton(true);
    std::string submitted;
    h.widget->setOnSubmit([&](const std::string& t) { submitted = t; });
    h.click(h.widget->bounds().center());
    h.type(U"go");
    h.key(Key::Enter);
    CHECK(submitted == "go");

    const Rect b = h.widget->bounds();
    h.click({b.right() - 12.0f, b.center().y}); // the clear button sits at the right
    CHECK(h.widget->text().empty());
}

TEST_CASE("TextField: clicking places the caret, dragging selects", "[ui][text]")
{
    Harness<TextField> h;
    h.widget->setText("rolo engine");
    h.ctx.render();
    const Rect b = h.widget->bounds();
    h.click({b.x + 9.0f, b.center().y}); // before the first letter
    CHECK(h.widget->state().caret() == 0);

    h.ctx.advanceTime(1.0); // a separate press, not a double-click
    h.ctx.pointerMove({b.x + 9.0f, b.center().y});
    h.ctx.pointerButton(MouseButton::Left, true, {});
    h.ctx.pointerMove({b.right() - 4.0f, b.center().y});
    h.ctx.pointerButton(MouseButton::Left, false, {});
    CHECK(h.widget->state().selectedText() == "rolo engine");
    CHECK(h.widget->cursor(b.center()) == CursorShape::IBeam);
}

// --- Buttons and checkboxes -----------------------------------------------------------------

TEST_CASE("Buttons click with the mouse and with Space/Enter", "[ui][button]")
{
    int clicks = 0;
    Harness<Button> h("Save", [&] { ++clicks; });
    h.click(h.widget->bounds().center());
    CHECK(clicks == 1);
    h.key(Key::Space);
    h.key(Key::Enter);
    CHECK(clicks == 3);

    // Pressing then releasing elsewhere cancels the click.
    h.ctx.pointerMove(h.widget->bounds().center());
    h.ctx.pointerButton(MouseButton::Left, true, {});
    h.ctx.pointerMove({490, 390});
    h.ctx.pointerButton(MouseButton::Left, false, {});
    CHECK(clicks == 3);

    h.widget->setEnabled(false);
    h.click(h.widget->bounds().center());
    CHECK(clicks == 3);
}

TEST_CASE("Checkboxes toggle and report the new value", "[ui][button]")
{
    bool value = false;
    Harness<Checkbox> h("Auto-scroll", false, [&](bool v) { value = v; });
    h.click(h.widget->bounds().center());
    CHECK(h.widget->isChecked());
    CHECK(value);
    h.key(Key::Space);
    CHECK_FALSE(h.widget->isChecked());
    CHECK_FALSE(value);
}

TEST_CASE("Wrapped labels grow taller as they get narrower", "[ui][label]")
{
    Context ctx;
    Label label("Select an object in the Hierarchy to see its properties.");
    label.setWrap(true);
    const float wide = label.measure(ctx, {1000, kUnbounded}).height;
    const float narrow = label.measure(ctx, {120, kUnbounded}).height;
    CHECK(narrow > wide * 2.0f);
}

// --- Lists and trees ---------------------------------------------------------------------------

TEST_CASE("ListView only draws the rows that are visible", "[ui][list]")
{
    Harness<ListView> h;
    int painted = 0;
    h.widget->setRowCount(100000);
    h.widget->setRowPainter([&](Context&, DrawList&, const RowInfo&) { ++painted; });
    h.ctx.render();
    CHECK(painted > 0);
    CHECK(painted < 40); // ~380 points / 24 per row, not 100000
}

TEST_CASE("ListView selection with mouse and keyboard; stick to end", "[ui][list]")
{
    Harness<ListView> h;
    std::optional<std::size_t> picked;
    h.widget->setSelectable(true);
    h.widget->setRowCount(50);
    h.widget->setRowPainter([](Context&, DrawList&, const RowInfo&) {});
    h.widget->setOnSelect([&](std::optional<std::size_t> r) { picked = r; });
    h.ctx.render();

    const Rect b = h.widget->bounds();
    h.click({b.x + 20.0f, b.y + 24.0f * 2.0f + 5.0f}); // third row
    CHECK(picked == std::optional<std::size_t>(2));
    h.key(Key::Down);
    CHECK(picked == std::optional<std::size_t>(3));
    h.key(Key::End);
    CHECK(picked == std::optional<std::size_t>(49));
    CHECK(h.widget->scroll().atEnd()); // scrolled to show it

    h.widget->setStickToEnd(true);
    h.widget->setRowCount(80);
    h.ctx.render();
    CHECK(h.widget->scroll().atEnd()); // followed the new rows
}

TEST_CASE("TreeView expands, collapses and navigates", "[ui][tree]")
{
    Harness<TreeView> h;
    h.widget->setItems({
        {1, 0, 0, "World", "Node", true},
        {2, 1, 1, "Background", "Shape", false},
        {3, 1, 1, "Rolo", "Node", true},
        {4, 3, 2, "Glow", "Shape", false},
        {5, 3, 2, "Flame", "Shape", false},
    });
    h.ctx.render();
    // Depth 0 and 1 start expanded, so everything is visible.
    CHECK(h.widget->visibleRows().size() == 5);

    std::uint64_t selected = 0;
    h.widget->setOnSelect([&](std::uint64_t id) { selected = id; });
    const Rect b = h.widget->bounds();
    h.click({b.x + 120.0f, b.y + 2.0f + 24.0f * 2.0f + 8.0f}); // "Rolo" row
    CHECK(selected == 3);

    h.key(Key::Left); // collapse Rolo
    CHECK(h.widget->visibleRows().size() == 3);
    h.key(Key::Left); // go to parent
    CHECK(selected == 1);
    h.key(Key::Down);
    h.key(Key::Down);
    CHECK(selected == 3);
    h.key(Key::Right); // expand again
    CHECK(h.widget->visibleRows().size() == 5);

    h.widget->setItems({{1, 0, 0, "World", "Node", false}}); // Rolo removed
    CHECK(h.widget->selectedId() == 0);
}

// --- Menus and dialogs ---------------------------------------------------------------------------

TEST_CASE("Menu bar opens a menu, highlights with arrows and runs the chosen item", "[ui][menu]")
{
    Harness<MenuBar> h;
    int resets = 0;
    bool consoleShown = true;
    h.widget->addMenu(Menu{"File", {MenuItem{"Quit", "⌘Q", [] {}}}});
    h.widget->addMenu(Menu{"View",
                           {MenuItem{"Console", {}, [&] { consoleShown = !consoleShown; }, [&] { return consoleShown; }},
                            MenuItem::divider(),
                            MenuItem{"Reset Layout", {}, [&] { ++resets; }}}});
    h.ctx.render();

    h.widget->open(h.ctx, 1);
    h.ctx.render();
    REQUIRE(h.ctx.hasOverlays());
    h.key(Key::Down);     // Console
    h.key(Key::Down);     // skips the divider: Reset Layout
    h.key(Key::Enter);
    CHECK(resets == 1);
    CHECK_FALSE(h.ctx.hasOverlays());
    CHECK_FALSE(h.widget->openMenu().has_value());

    h.widget->open(h.ctx, 0);
    h.ctx.render();
    h.key(Key::Escape);
    CHECK_FALSE(h.ctx.hasOverlays());
}

TEST_CASE("Dialogs close with their button, Enter or Escape", "[ui][dialog]")
{
    Harness<Label> h("background");
    int closes = 0;
    auto dialog = std::make_unique<Dialog>("About Rolo Engine");
    dialog->body().emplace<Label>("Version 0.2.0");
    dialog->addButton("Close", Button::Style::Primary, [&] { ++closes; });
    showDialog(h.ctx, std::move(dialog));
    h.ctx.render();
    CHECK(h.ctx.hasModal());
    h.key(Key::Enter);
    CHECK(closes == 1);
    CHECK_FALSE(h.ctx.hasOverlays());
}

TEST_CASE("Enter in a dialog's text field presses the dialog's main button", "[ui][dialog][text]")
{
    // Regression: the focused field kept Enter, so typing a name and pressing
    // Enter did nothing.
    Harness<Label> h("background");
    int creates = 0;
    std::string created;
    auto dialog = std::make_unique<Dialog>("New Clip");
    auto& name = dialog->body().emplace<TextField>("");
    dialog->addButton("Cancel");
    dialog->addButton("Create", Button::Style::Primary, [&] {
        ++creates;
        created = name.text();
    });
    showDialog(h.ctx, std::move(dialog));
    h.ctx.render();
    h.ctx.setFocus(&name);
    h.ctx.textInput(U'a');
    h.key(Key::Enter);
    CHECK(creates == 1);
    CHECK(created == "a");
    CHECK_FALSE(h.ctx.hasOverlays());
}

TEST_CASE("Flow wraps children onto new lines when they do not fit", "[ui][layout]")
{
    Context ctx;
    auto flow = std::make_unique<Flow>(10.0f, 4.0f);
    std::vector<Widget*> items;
    for (int i = 0; i < 4; ++i) {
        auto item = std::make_unique<Spacer>(0.0f);
        item->setFixedSize({100.0f, 20.0f});
        items.push_back(&flow->addChild(std::move(item)));
    }
    Flow& f = *flow;
    ctx.setRoot(std::move(flow));

    // Wide: one line.
    CHECK(f.measure(ctx, {500.0f, kUnbounded}).height == 20.0f);
    f.arrange(ctx, {0, 0, 500, 20});
    CHECK(items[3]->bounds().y == 0.0f);
    CHECK(items[3]->bounds().x == 330.0f);

    // Narrow: two per line, two lines.
    CHECK(f.measure(ctx, {250.0f, kUnbounded}).height == 44.0f);
    f.arrange(ctx, {0, 0, 250, 44});
    CHECK(items[1]->bounds().y == 0.0f);
    CHECK(items[2]->bounds().x == 0.0f);
    CHECK(items[2]->bounds().y == 24.0f);
}

TEST_CASE("A stretching child in a Flow line pushes the rest to the right", "[ui][layout]")
{
    Context ctx;
    auto flow = std::make_unique<Flow>(0.0f);
    flow->addChild(std::make_unique<Spacer>(0.0f)).setFixedSize({50.0f, 20.0f});
    flow->emplace<Spacer>(); // stretches
    Widget& last = flow->addChild(std::make_unique<Spacer>(0.0f));
    last.setFixedSize({50.0f, 20.0f});
    Flow& f = *flow;
    ctx.setRoot(std::move(flow));
    f.arrange(ctx, {0, 0, 400, 20});
    CHECK(last.bounds().x == 350.0f);
}

TEST_CASE("Revealing a tree item unfolds its parents", "[ui][tree]")
{
    Harness<TreeView> h;
    h.widget->setAutoExpandDepth(0);
    h.widget->setItems({
        {1, 0, 0, "World", "", true},
        {2, 1, 1, "Rolo", "", true},
        {3, 2, 2, "Flame", "", false},
    });
    h.ctx.render();
    CHECK(h.widget->visibleRows().size() == 1);
    h.widget->setSelectedId(3);
    h.widget->reveal(3);
    CHECK(h.widget->visibleRows().size() == 3);
}

// Regression: after Enter committed a value, an undo could not update the
// field because it still had focus and counted as "being edited".
TEST_CASE("A committed field shows values set afterwards, even while focused", "[ui][fields]")
{
    Harness<TextField> h;
    std::string committed;
    h.widget->setOnCommit([&](const std::string& text) { committed = text; });
    h.widget->setText("100");
    h.click(h.widget->bounds().center());
    REQUIRE(h.widget->isFocused());
    CHECK_FALSE(h.widget->isEditing()); // focused, but nothing typed yet

    h.key(Key::A, primary(h.ctx.platform()));
    h.type(U"300");
    CHECK(h.widget->isEditing()); // uncommitted typing: owners must not overwrite it
    h.key(Key::Enter);
    h.ctx.render(); // run the posted commit
    CHECK(committed == "300");
    CHECK(h.widget->isFocused());
    CHECK_FALSE(h.widget->isEditing());

    // An undo puts the old value back: the field may now show it.
    h.widget->setText("100");
    CHECK(h.widget->text() == "100");
    // ...and Escape returns to that value, not to the undone one.
    h.type(U"5");
    h.key(Key::Escape);
    CHECK(h.widget->text() == "100");
}
